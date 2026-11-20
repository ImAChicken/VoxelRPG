#pragma once
#include "GameSession.h"
#include "SaveStaging.h"
#include "../game/Kits.h"
#include "../net/Protocol.h"
#include "../net/Transport.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ══════════════════════════════════════════════════════════
// ServerHost — the server's side of the protocol
// ══════════════════════════════════════════════════════════
// Serves one GameSession to the clients that connect through a
// listener (loopback or network: net/Transport.h). Each tick:
//
//   poll()        new connections, and what clients sent: Hello
//                 (a player joins), Input (queued by the client's
//                 tick), Request (a change to the world: checked,
//                 then done with game/WorldActions.h, the same code
//                 single player runs), Loadout, Bye
//   beforeStep()  each player's next input becomes its intent
//   (the host steps the simulation)
//   afterStep()   events out; voxel edits out; every
//                 SNAPSHOT_EVERY ticks a snapshot to each client of
//                 the bodies near it; chunks that differ from the
//                 seed sent as clients come near them; structures
//                 resent when they change
//
// Players are saved per name: <world>/players/<name>.dat, through
// the session's staging (GameSession::save's extra), and when they
// leave. A new name starts at the spawn in the world's default mode.
//
// What a client generates itself: every chunk the seed makes. What
// it's sent: the chunks that differ (MODIFIED: saved in the world,
// or edited since), as whole columns once, then voxel by voxel.
class ServerHost {
public:
    struct Client {
        std::shared_ptr<net::IConnection> conn;
        std::string name;
        Player*     player = nullptr;
        bool        joined = false;
        std::map<uint32_t, proto::InputFrame> inputs;   // waiting, by client tick
        uint32_t    lastUsed = 0;                       // the last input tick applied
        HumanIntent held;                               // last intent (repeated when an input is late)
        std::unordered_map<uint64_t, std::string> known;   // bodies it's been sent a Spawn for, and the gear it showed
        std::set<std::pair<int, int>> chunks;           // modified columns it's been sent
        std::string structuresSent;                     // the text last sent
        std::string invSent;                            // its inventory as it last knew it
        WorldActions hands;                             // does its requests (what it says goes back as Notice)
        float       requestBudget = REQUEST_BURST;      // requests it may still make now
        uint32_t    lastRequestTick = 0;
        uint32_t    lastHoe = 0, lastBlast = 0;         // ticks (their cooldowns)
    };

    // Requests: at most REQUESTS_PER_SECOND, in bursts of
    // REQUEST_BURST; aimed no further than the hands reach (plus
    // REACH_SLACK: the third-person camera, and the body having
    // moved since the client aimed)
    static constexpr float REQUESTS_PER_SECOND = 20.0f, REQUEST_BURST = 30.0f;
    static constexpr float REACH_SLACK = 20.0f;
    static constexpr float BUILD_SLACK = 64.0f;          // a hammer's locked, nudged spot
    size_t requestsDone = 0, requestsRefused = 0;       // (tests read these)

    int chunkRadius = 8;           // columns around a player it may need (its view)
    int chunksPerTick = 2;         // whole columns sent to one client a tick, at most
    int maxPlayers = 20;
    std::vector<std::string> starterKit;   // items a new player starts with (server.toml; empty: kits.toml)
    std::string hostName = "host";         // a player not from a connection (the game hosting)
    std::function<void(const std::string&)> log = [](const std::string& s) { std::cout << s << "\n"; };

    ServerHost(GameSession& s, net::IListener& l) : session(s), listener(l) {
        // every changed voxel: remembered, and sent on
        session.editor->onVoxel = [this](glm::ivec3 p, uint16_t id) {
            int cx = ChunkManager::chunkCoord(p.x), cz = ChunkManager::chunkCoord(p.z);
            modified.insert({ cx, cz });
            pendingEdits.push_back({ p, id });
        };
        // columns saved in the world (or staged) differ from the seed
        for (const std::string& dir : { session.folder() + "/chunks", savestage::stageDir(session.folder()).string() + "/chunks" }) {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec)) continue;
            for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
                int cx, cz;
                if (std::sscanf(e.path().filename().string().c_str(), "c_%d_0_%d.vxrp", &cx, &cz) == 2) modified.insert({ cx, cz });
            }
        }
    }
    ~ServerHost() { if (session.editor) session.editor->onVoxel = nullptr; }

    const std::vector<std::unique_ptr<Client>>& clients() const { return list; }
    uint32_t tick() const { return serverTick; }
    size_t players() const { size_t n = 0; for (auto& c : list) n += c->joined; return n; }

    // ── Incoming ──────────────────────────────────────────
    void poll() {
        listener.poll();
        while (auto c = listener.accept()) {
            auto cl = std::make_unique<Client>();
            cl->conn = c;
            list.push_back(std::move(cl));
        }
        for (size_t i = 0; i < list.size();) {
            Client& c = *list[i];
            net::Packet pk;
            while (c.conn->connected() && c.conn->receive(pk)) handle(c, pk);
            if (!c.conn->connected()) { leave(c); list.erase(list.begin() + i); continue; }
            i++;
        }
    }

    // Each joined player's inputs for this tick. One body step per
    // input the client sent, never one it didn't (Simulation::
    // setInputs): a client whose frames are late waits a tick
    // where it is; one that's behind (a hitch, then a burst) gets
    // two steps a tick until it's caught up. Its prediction made
    // exactly those steps, so hitches no longer pull it back.
    // Only a long stall (over a second queued) drops inputs.
    static constexpr size_t INPUT_SLACK = 3;     // queued beyond this: catch up, two a tick
    static constexpr size_t INPUT_STALL = 60;    // queued beyond this: skip to the newest

    void beforeStep() {
        for (auto& cp : list) {
            Client& c = *cp;
            if (!c.joined || !c.player) continue;
            while (c.inputs.size() > INPUT_STALL) { c.lastUsed = c.inputs.begin()->first; c.inputs.erase(c.inputs.begin()); }
            size_t take = c.inputs.empty() ? 0 : c.inputs.size() > INPUT_SLACK ? 2 : 1;
            // the next one missing: wait for it, unless enough came after
            // it that it's surely lost
            if (take && c.lastUsed != 0 && c.inputs.begin()->first > c.lastUsed + 1 && c.inputs.size() <= INPUT_SLACK)
                take = 0;
            std::vector<HumanIntent> steps;
            for (size_t k = 0; k < take; k++) {
                auto it = c.inputs.begin();
                // a gap in the ticks (a lost packet the redundancy
                // didn't cover): the held intent stands in for it
                if (it->first > c.lastUsed + 1 && c.lastUsed != 0) {
                    HumanIntent in = c.held;
                    in.clearOneShots();
                    steps.push_back(in);
                    c.lastUsed++;
                    continue;
                }
                HumanIntent in = it->second.intent;
                glm::vec3 f = it->second.facing;
                f.y = 0.0f;
                if (glm::length(f) > 1e-3f) c.player->facing = glm::normalize(f);
                c.player->aimPitch = std::clamp(it->second.pitch, -89.0f, 89.0f);   // where a bow aims
                c.lastUsed = it->first;
                // lag compensation: its swings meet bodies where it saw them
                if (it->second.viewTick)
                    session.sim.setViewDelay(c.player->uuid, (int)serverTick - (int)it->second.viewTick);
                c.inputs.erase(it);
                c.held = in;
                steps.push_back(in);
            }
            if (!steps.empty()) session.sim.setIntent(c.player->uuid, steps.back());
            session.sim.setInputs(c.player->uuid, std::move(steps));
        }
    }

    // ── Outgoing ──────────────────────────────────────────
    void afterStep() {
        serverTick++;
        // events, to everyone joined
        for (const SimEvent& e : session.sim.takeEvents()) {
            proto::EventMsg m;
            m.kind = (uint8_t)e.kind; m.a = e.a; m.b = e.b; m.attack = (uint8_t)e.attack; m.amount = e.amount;
            m.flags = (e.blocked ? 1 : 0) | (e.guardBroken ? 2 : 0) | (e.poiseBroken ? 4 : 0) | (e.knockedDown ? 8 : 0) |
                      (e.parried ? 16 : 0) | (e.riposte ? 32 : 0);
            m.at = e.at;
            events.push_back(e);
            auto bytes = proto::event(m);
            for (auto& c : list) if (c->joined) c->conn->send(bytes, net::Channel::Reliable);
        }
        // voxel edits, to those who have the column (others get it whole when near)
        if (!pendingEdits.empty()) {
            for (auto& cp : list) {
                Client& c = *cp;
                if (!c.joined) continue;
                std::vector<proto::VoxelEdit> mine;
                for (const auto& e : pendingEdits)
                    if (c.chunks.count({ ChunkManager::chunkCoord(e.p.x), ChunkManager::chunkCoord(e.p.z) })) mine.push_back(e);
                if (!mine.empty()) c.conn->send(proto::edits(mine), net::Channel::Reliable);
            }
            pendingEdits.clear();
        }
        // whole columns that differ from the seed, nearest first
        for (auto& cp : list) if (cp->joined) sendChunks(*cp);
        // structures, when they've changed (4 times a second at most)
        if (serverTick % 15 == 0) {
            std::ostringstream os;
            session.structs->write(os, false);
            std::string now = os.str();
            for (auto& c : list)
                if (c->joined && c->structuresSent != now) { c->conn->send(proto::text(proto::Msg::Structures, now), net::Channel::Reliable); c->structuresSent = now; }
        }
        if (serverTick % proto::SNAPSHOT_EVERY == 0)
            for (auto& c : list) if (c->joined) sendSnapshot(*c);
    }

    // What happened this tick (after afterStep), for a host that
    // also plays (the game's own player)
    std::vector<SimEvent> takeEvents() { std::vector<SimEvent> e; e.swap(events); return e; }

    // Every joined player's file, into a save's staging folder
    void savePlayers(const std::string& stage) {
        for (auto& c : list) if (c->joined && c->player) writePlayer(stage, *c);
    }
    // Disconnect everyone (shutdown)
    void kickAll(const std::string& why) {
        for (auto& c : list) { c->conn->send(proto::text(proto::Msg::Notice, why), net::Channel::Reliable); }
        for (auto& c : list) { leave(*c); c->conn->close(); }
        list.clear();
    }

    static std::string safeName(const std::string& n) {
        std::string out;
        for (char ch : n) if (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-') out += ch;
        return out.substr(0, 24);
    }

private:
    GameSession& session;
    net::IListener& listener;
    std::vector<std::unique_ptr<Client>> list;
    std::set<std::pair<int, int>> modified;
    std::vector<proto::VoxelEdit> pendingEdits;
    std::vector<SimEvent> events;
    uint32_t serverTick = 0;

    void refuse(Client& c, const std::string& why) {
        c.conn->send(proto::text(proto::Msg::Refused, why), net::Channel::Reliable);
        log("refused " + (c.name.empty() ? std::string("a client") : c.name) + ": " + why);
        c.conn->close();
    }

    void handle(Client& c, const net::Packet& pk) {
        wire::Reader r(pk.data);
        auto kind = (proto::Msg)r.u8();
        switch (kind) {
        case proto::Msg::Hello: {
            if (c.joined) return;
            proto::Hello h = proto::readHello(r);
            if (!r.ok()) return refuse(c, "garbled hello");
            if (h.version != proto::VERSION)
                return refuse(c, "version mismatch (server " + std::to_string(proto::VERSION) + ", you " + std::to_string(h.version) + ")");
            std::string name = safeName(h.name);
            if (name.empty()) return refuse(c, "a name is needed (letters, digits, - and _)");
            for (auto& o : list) if (o.get() != &c && o->joined && o->name == name) return refuse(c, name + " is already playing");
            if ((int)players() >= maxPlayers) return refuse(c, "the server is full");
            join(c, name, h.look);
            break;
        }
        case proto::Msg::Input: {
            if (!c.joined) return;
            for (const proto::InputFrame& f : proto::readInput(r))
                if (f.tick > c.lastUsed && !c.inputs.count(f.tick) && c.inputs.size() < 120) c.inputs[f.tick] = f;
            break;
        }
        case proto::Msg::Loadout: {
            if (!c.joined || !c.player) return;
            proto::LoadoutMsg m = proto::readLoadout(r);
            if (!r.ok()) return;
            if (proto::applyLoadout(c.player->inventory, m)) {
                c.player->combatStance = m.combatStance;
                // each loadout's off hand: only something carried that fits there
                for (int i = 0; i < CombatKit::SETS; i++) {
                    uint16_t off = m.kitOff[i];
                    c.player->kit.sets[(size_t)i].off =
                        off && c.player->inventory.find(off) >= 0 && CombatKit::fitsOff(off) ? off : 0;
                }
                c.invSent = invText(*c.player);
            } else {
                c.invSent.clear();               // refused: it's sent what the server has
            }
            break;
        }
        case proto::Msg::Request: {
            if (!c.joined || !c.player) return;
            WorldRequest q;
            if (!proto::readRequest(r, q)) return;
            std::string why = allowed(c, q);
            if (!why.empty()) {
                requestsRefused++;
                c.conn->send(proto::text(proto::Msg::Notice, why), net::Channel::Reliable);
                return;
            }
            requestsDone++;
            c.hands.act(q, *c.player, *session.editor, *session.structs);
            break;
        }
        case proto::Msg::Bye: c.conn->close(); break;
        default: break;          // unknown: ignored
        }
    }

    // May this player do this? "" if so, else why not (sent back)
    std::string allowed(Client& c, const WorldRequest& q) {
        Player& p = *c.player;
        // the budget fills with time
        float since = (float)(serverTick - c.lastRequestTick) / (float)proto::TICK_HZ;
        c.lastRequestTick = serverTick;
        c.requestBudget = std::min(REQUEST_BURST, c.requestBudget + since * REQUESTS_PER_SECOND);
        if (c.requestBudget < 1.0f) return "";                          // too fast: dropped quietly
        c.requestBudget -= 1.0f;
        if (!p.rules().hasBody || p.dead()) return "you can't do that now";
        using Op = WorldRequest::Op;
        // the item used: carried (the server's inventory), and the
        // right kind for what's asked
        const Inventory& inv = p.activeInventory();
        const ItemDef& d = gItems.get(q.item);
        if (q.item && !inv.infinite && inv.count(q.item) <= 0) return "you don't have that";
        if (q.item && !d.valid()) return "unknown item";
        bool hammer = d.is(IC_TOOL) && d.tool == ToolKind::Hammer;
        switch (q.op) {
        case Op::Place:     if (!d.is(IC_BLOCK) && !d.is(IC_DECOR)) return "nothing to place"; break;
        case Op::Piece:
        case Op::Blueprint: if (!hammer) return "that needs the hammer"; break;
        case Op::Hoe:       if (d.tool != ToolKind::Hoe) return "that needs the hoe"; break;
        case Op::Blast:     if (d.tool != ToolKind::Blast) return "that needs the blast wand"; break;
        case Op::AxeHit:    if (d.tool != ToolKind::Axe) return "that needs an axe"; break;
        case Op::BreakCube: if (d.tool != ToolKind::Pickaxe || d.cubeSize <= 0) return "that needs a pickaxe"; break;
        default: break;
        }
        // cooldowns the client keeps too
        auto cool = [&](uint32_t& last, float s) {
            if (last && serverTick - last < (uint32_t)(s * proto::TICK_HZ) - 2) return false;
            last = serverTick;
            return true;
        };
        if (q.op == Op::Hoe && !cool(c.lastHoe, WorldActions::HOE_EVERY)) return "";
        if (q.op == Op::Blast && !cool(c.lastBlast, WorldActions::BLAST_COOLDOWN)) return "";
        // in reach of the eyes
        bool survival = p.mode == GameMode::Survival;
        float reach = (survival ? WorldActions::SURVIVAL_REACH : WorldActions::DEV_REACH) + REACH_SLACK;
        glm::vec3 eye = p.position + glm::vec3(0.0f, p.height * 0.9f, 0.0f);
        auto far = [&](glm::vec3 at, float limit) { return glm::length(at - eye) > limit; };
        if (q.op == Op::AxeHit ? far(q.point, reach) : far(glm::vec3(q.pos) + 0.5f, reach)) return "too far away";
        if ((q.op == Op::Piece || q.op == Op::Blueprint) && far(glm::vec3(q.lo), reach + BUILD_SLACK)) return "too far away";
        if (q.op == Op::BreakCube && (far(glm::vec3(q.lo), reach + d.cubeSize) || far(glm::vec3(q.hi), reach + 2.0f * d.cubeSize)))
            return "too far away";
        return "";
    }

    std::string playerPath(const Client& c, bool forWrite, const std::string& stage = "") const {
        std::filesystem::path rel = std::filesystem::path("players") / (c.name + ".dat");
        if (forWrite) return (std::filesystem::path(stage) / rel).string();
        return savestage::readPath(session.folder(), rel).string();
    }
    void writePlayer(const std::string& stage, const Client& c) {
        std::filesystem::create_directories(std::filesystem::path(stage) / "players");
        std::ofstream f(playerPath(c, true, stage));
        c.player->write(f);
    }

    void join(Client& c, const std::string& name, const std::string& look) {
        c.name = name;
        Player& p = session.sim.addPlayer();
        c.player = &p;
        glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z));
        p.spawnPoint = spawn;
        p.setMode(gWorldSave.getDefaultMode());
        p.position = spawn + glm::vec3(0.0f, 2.0f, 0.0f);
        std::ifstream f(playerPath(c, false));
        bool returning = (bool)f;
        if (returning) {
            std::stringstream ss; ss << f.rdbuf();
            proto::kvRead(ss.str(), [&](const std::string& k, const std::string& v) { p.read(k, v); });
            p.inventory.finishLoad();
            p.devInventory.finishLoad();
        } else {
            proto::kvRead(look, [&](const std::string& k, const std::string& v) { p.appearance.read(k, v); });
            // server.toml's starter_kit, or the game's kit (kits.toml)
            std::vector<KitEntry> kit;
            for (const std::string& item : starterKit) kit.push_back({ item, 1 });
            Kits::give(p.inventory, kit.empty() ? gKits.player : kit, true);
        }
        c.invSent = invText(p);
        c.joined = true;
        // its requests: done for it, what's said sent back
        c.hands.chests = &session.chests;
        c.hands.wood = nullptr;                    // (clients don't see falling logs yet: the wood goes to the player)
        auto conn = c.conn;
        c.hands.onSay = [conn](const std::string& s) { conn->send(proto::text(proto::Msg::Notice, s), net::Channel::Reliable); };
        proto::Welcome w;
        w.you = p.uuid;
        w.worldName = gWorldSave.getName();
        { std::ifstream wf(session.folder() + "/world.vxrp"); std::stringstream ss; ss << wf.rdbuf(); w.worldInfo = ss.str(); }
        w.player = proto::kvText([&](std::ostream& o) { p.write(o); });
        w.serverTick = serverTick;
        w.names = proto::localNames();
        c.conn->send(proto::welcome(w), net::Channel::Reliable);
        // the columns that differ from the seed: the client waits for these
        c.conn->send(proto::modified(std::vector<std::pair<int, int>>(modified.begin(), modified.end())), net::Channel::Reliable);
        log(name + (returning ? " joined (welcome back)" : " joined (new)") + ", " + std::to_string(players()) + " playing");
    }

    void leave(Client& c) {
        if (!c.joined || !c.player) return;
        std::string stage = savestage::stageDir(session.folder()).string();
        writePlayer(stage, c);                       // staged, committed with the world's next commit
        auto gone = proto::despawn(c.player->uuid);
        for (auto& o : list) if (o.get() != &c && o->joined && o->known.erase(c.player->uuid)) o->conn->send(gone, net::Channel::Reliable);
        session.sim.removePlayer(c.player->uuid);
        log(c.name + " left");
        c.player = nullptr;
        c.joined = false;
    }

    void sendChunks(Client& c) {
        if (!c.player) return;
        int pcx = ChunkManager::chunkCoord((int)std::floor(c.player->position.x));
        int pcz = ChunkManager::chunkCoord((int)std::floor(c.player->position.z));
        std::vector<std::pair<int, std::pair<int, int>>> want;
        for (auto& col : modified) {
            int d = std::max(std::abs(col.first - pcx), std::abs(col.second - pcz));
            if (d <= chunkRadius && !c.chunks.count(col)) want.push_back({ d, col });
        }
        std::sort(want.begin(), want.end());
        int sent = 0;
        for (auto& [d, col] : want) {
            if (sent >= chunksPerTick) break;
            ChunkPtr owned;
            Chunk* ch = session.world->getChunk(col.first, col.second);
            if (!ch) { owned = session.chunkSource()(col.first, col.second); ch = owned.get(); }
            if (!ch) continue;
            c.conn->send(proto::chunkData(col.first, col.second, *ch), net::Channel::Reliable);
            c.chunks.insert(col);
            sent++;
        }
    }

    static proto::BodyState stateOf(const Human& h) {
        proto::BodyState b;
        b.id = h.uuid; b.pos = h.position; b.vel = h.velocity; b.facing = h.facing;
        b.health = h.health; b.maxHealth = h.maxHealth; b.stamina = h.stamina;
        std::string held; bool combat = false;
        Simulation::handsOf(h, held, combat);
        b.held = held;
        b.off = Simulation::offHandOf(h);
        b.flags = (h.grounded ? proto::BodyState::F_GROUNDED : 0) | (h.crouching ? proto::BodyState::F_CROUCH : 0) |
                  (h.sprinting ? proto::BodyState::F_SPRINT : 0) | (h.flying ? proto::BodyState::F_FLY : 0) |
                  (h.guarding ? proto::BodyState::F_GUARD : 0) | (h.dead() ? proto::BodyState::F_DEAD : 0) |
                  (combat ? proto::BodyState::F_COMBAT : 0) | (h.ragdoll.controlsLocked() ? proto::BodyState::F_LIMP : 0);
        b.move = (uint8_t)h.move; b.moveTimer = h.moveTimer;
        if (h.attack.active()) { b.attack = h.attack.def().id; b.attackT = h.attack.t; b.attackDir = h.attack.dir; }
        return b;
    }

    static std::string invText(const Player& p) {
        return proto::kvText([&](std::ostream& o) { p.inventory.write(o, "inv"); }) + "stance=" + (p.combatStance ? "1" : "0") + "\n";
    }

    void sendSnapshot(Client& c) {
        if (!c.player) return;
        // its inventory, when it's not what the client has (a pickup, a refused arrangement)
        if (std::string now = invText(*c.player); now != c.invSent) {
            c.conn->send(proto::text(proto::Msg::PlayerState, now), net::Channel::Reliable);
            c.invSent = now;
        }
        proto::Snapshot s;
        s.serverTick = serverTick;
        s.ackTick = c.lastUsed;
        s.clock = gClock.time;
        s.you = stateOf(*c.player);
        std::unordered_set<uint64_t> seen;
        auto consider = [&](const Human& h, uint8_t kind, const std::string& name, const std::string& type, const Appearance& look,
                            const std::string& gear) {
            if (h.uuid == c.player->uuid) return;
            if (glm::length(h.position - c.player->position) > proto::VIEW_RANGE) return;
            seen.insert(h.uuid);
            auto kn = c.known.find(h.uuid);
            if (kn == c.known.end() || kn->second != gear) {       // new in view, or dressed differently
                proto::SpawnMsg sp;
                sp.id = h.uuid; sp.kind = kind; sp.name = name; sp.type = type; sp.gear = gear;
                sp.look = proto::kvText([&](std::ostream& o) { look.write(o); });
                c.conn->send(proto::spawn(sp), net::Channel::Reliable);
                c.known[h.uuid] = gear;
            }
            s.bodies.push_back(stateOf(h));
        };
        for (auto& pl : session.sim.players()) {
            if (!pl->rules().hasBody) continue;                     // a spectator
            std::string name = hostName;
            for (auto& oc : list) if (oc->player == pl.get()) name = oc->name;
            std::string gear;
            for (int ws = 0; ws < WORN_SLOTS; ws++)
                if (const ItemStack& st = pl->activeInventory().worn((WornSlot)ws); !st.empty()) gear += gItems.get(st.itemID).name + " ";
            consider(*pl, 0, name, "player", pl->appearance, gear);
        }
        for (auto& np : session.sim.npcs().all()) consider(*np, 1, np->name, np->typeId, np->appearance, "");
        for (auto it = c.known.begin(); it != c.known.end();)
            if (!seen.count(it->first)) { c.conn->send(proto::despawn(it->first), net::Channel::Reliable); it = c.known.erase(it); }
            else ++it;
        c.conn->send(proto::snapshot(s), net::Channel::Unreliable);
    }
};
