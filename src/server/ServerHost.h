#pragma once
#include "GameSession.h"
#include "SaveStaging.h"
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
//                 tick), Bye
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
    };

    int chunkRadius = 8;           // columns around a player it may need (its view)
    int chunksPerTick = 2;         // whole columns sent to one client a tick, at most
    int maxPlayers = 20;
    std::vector<std::string> starterKit;   // items a new player starts with (server.toml)
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

    // Each joined player's next input becomes its intent
    void beforeStep() {
        for (auto& cp : list) {
            Client& c = *cp;
            if (!c.joined || !c.player) continue;
            HumanIntent in = c.held;
            in.clearOneShots();
            auto it = c.inputs.begin();
            if (it != c.inputs.end()) {
                in = it->second.intent;
                glm::vec3 f = it->second.facing;
                f.y = 0.0f;
                if (glm::length(f) > 1e-3f) c.player->facing = glm::normalize(f);
                c.lastUsed = it->first;
                // lag compensation: its swings meet bodies where it saw them
                if (it->second.viewTick)
                    session.sim.setViewDelay(c.player->uuid, (int)serverTick - (int)it->second.viewTick);
                c.inputs.erase(it);
                // a client far behind (a stall): skip to its newest
                while (c.inputs.size() > 6) { c.lastUsed = c.inputs.begin()->first; c.inputs.erase(c.inputs.begin()); }
            }
            c.held = in;
            session.sim.setIntent(c.player->uuid, in);
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
            session.structs->write(os);
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
                c.invSent = invText(*c.player);
            } else {
                c.invSent.clear();               // refused: it's sent what the server has
            }
            break;
        }
        case proto::Msg::Bye: c.conn->close(); break;
        default: break;          // unknown: ignored
        }
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
            for (const std::string& item : starterKit)
                if (uint16_t id = gItems.find(item)) {
                    p.inventory.add(id, 1);
                    const ItemDef& d = gItems.get(id);
                    int slot = p.inventory.find(id);
                    if (d.canFight() && p.inventory.equip[(int)EquipSlot::Weapon1] < 0) p.inventory.equipFrom(slot, EquipSlot::Weapon1);
                    else if (d.worn >= 0) p.inventory.equipFrom(slot, equipForWorn((WornSlot)d.worn));
                }
        }
        c.invSent = invText(p);
        c.joined = true;
        proto::Welcome w;
        w.you = p.uuid;
        w.worldName = gWorldSave.getName();
        { std::ifstream wf(session.folder() + "/world.vxrp"); std::stringstream ss; ss << wf.rdbuf(); w.worldInfo = ss.str(); }
        w.player = proto::kvText([&](std::ostream& o) { p.write(o); });
        w.serverTick = serverTick;
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
