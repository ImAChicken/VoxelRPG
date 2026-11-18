#pragma once
#include <cstdio>
#include <cstdlib>
#include "../chunk/ChunkManager.h"
#include "../entity/Player.h"
#include "../net/Protocol.h"
#include "../net/Transport.h"
#include "../physics/VoxelPhysics.h"
#include "../server/GameSession.h"
#include "../world/StructureManager.h"
#include "../world/WorldEditor.h"
#include "../world/WorldSaveManager.h"
#include "../world/DayClock.h"
#include "../worldgen/WorldType.h"
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// ClientWorld — a player's copy of a server's world
// ══════════════════════════════════════════════════════════
// What a client keeps while it plays on a server (net/Protocol.h):
//
//   the ground    generated from the world's seed with the same
//                 generator (the settings come in Welcome), except
//                 the columns the server lists as MODIFIED: those
//                 arrive whole (ChunkData), then voxel by voxel
//   structures    the server's list, read into a StructureManager
//                 (natural trees come from the seed as usual)
//   bodies        every other player and NPC in view, as a short
//                 history of snapshots: sample() gives where one
//                 was INTERP_TICKS ago, between two snapshots, so
//                 they move smoothly however the packets arrive
//   you           predicted: each tick your intent goes to the
//                 server AND moves you here at once (the same
//                 physics). When a snapshot says where the server
//                 had you after one of your inputs, the difference
//                 from where you'd predicted is added back.
//
// Nothing here draws; the game renders from it (phase 3), and the
// tests drive it headless.
class ClientWorld {
public:
    enum class State { Connecting, Joined, Refused, Closed };
    static constexpr int INTERP_TICKS = 6;        // 100 ms behind the newest snapshot
    static constexpr float SNAP_ERROR = 0.05f;    // voxels of misprediction worth correcting
    static constexpr float BIG_ERROR  = 6.0f;     // past this a correction is a jump (a teleport), not eased

    struct Remote {
        proto::SpawnMsg info;
        Appearance look;
        std::deque<std::pair<uint32_t, proto::BodyState>> history;   // by server tick
    };

    // On its own (tests, bots) it keeps everything itself. The game
    // gives it its Player and its GameSession: the mirror is built
    // into the session's parts, so the game draws and predicts on it
    // exactly as it does a local world (and gWorldSave describes it).
    ClientWorld(std::shared_ptr<net::IConnection> c, std::string cacheRoot = "servercache",
                Player* player = nullptr, GameSession* into = nullptr)
        : me(player ? *player : ownMe), info(into ? gWorldSave : ownInfo),
          gen(into ? into->gen : own.gen), world(into ? into->world : own.world),
          physics(into ? into->physics : own.physics), editor(into ? into->editor : own.editor),
          structs(into ? into->structs : own.structs),
          conn(std::move(c)), cache(std::move(cacheRoot)), session(into) {}

    void join(const std::string& name, const Appearance& look) {
        proto::Hello h;
        h.name = name;
        h.look = proto::kvText([&](std::ostream& o) { look.write(o); });
        conn->send(proto::hello(h), net::Channel::Reliable);
    }
    void leave() { conn->send(std::vector<uint8_t>{ (uint8_t)proto::Msg::Bye }, net::Channel::Reliable); }
    // Keep the connection alive without reading anything (while
    // the generator is being built on another thread)
    void keepAlive() { conn->poll(); }

    State state = State::Connecting;
    std::string refusedWhy;
    std::vector<std::string> notices;
    std::vector<proto::EventMsg> events;      // the game takes these
    proto::IdMap ids;                         // the server's block / item numbers ↔ ours (from the Welcome)

    // A change to the world, for the server to do (game/WorldActions.h)
    void request(const WorldRequest& q) {
        if (state == State::Joined) conn->send(proto::request(q, &ids), net::Channel::Reliable);
    }

    Player& me;
    uint64_t myId = 0;
    uint32_t tick = 0;                        // this client's tick
    uint32_t serverTick = 0;                  // the newest snapshot's
    float    clockTime = 0.35f;

    WorldSaveManager& info;                   // the server's world settings
    std::unique_ptr<TerrainGenerator>& gen;
    std::unique_ptr<ChunkManager>& world;
    std::unique_ptr<IPhysics>& physics;
    std::unique_ptr<WorldEditor>& editor;     // unused for edits; StructureManager needs one
    std::unique_ptr<StructureManager>& structs;
    std::unordered_map<uint64_t, Remote> bodies;
    const std::string& cacheFolder() const { return folder; }
    float pingMs() const { return conn->pingMs(); }

    // ── Each frame ────────────────────────────────────────
    void receive() {
        net::Packet pk;
        while (conn->receive(pk)) handle(pk);
        if (!conn->connected() && state != State::Refused) state = State::Closed;
    }

    // Has Welcome come, and is the generator still to be built?
    // (Island worlds bake: the game runs it on a worker thread.)
    bool needsGenerator() const { return state == State::Joined && !gen; }
    void buildGenerator() {
        gen = makeGenerator(info.getWorldType(), info.getSeed(), info.getWorldSize(), info.getWorldBiome(),
                            info.getTerrainStyle(), folder + "/terrain.bake", info.getGenParams(), info.getFlatOptions());
        WORLD_SIZE_X = WORLD_SIZE_Z = info.getWorldSize();
        world = std::make_unique<ChunkManager>();
        physics = std::make_unique<VoxelPhysics>();
        physics->init(*world);
        editor = std::make_unique<WorldEditor>(*world);
        structs = std::make_unique<StructureManager>(*world, *editor, *gen);
        if (!pendingStructures.empty()) { std::istringstream in(pendingStructures); structs->read(in); }
        for (auto& [col, ch] : received) world->setChunk(col.first, col.second, std::move(ch));
        received.clear();
        if (session) session->attachRemote(folder);
    }

    // Where a column's voxels come from (worker threads): the seed,
    // unless the server has said it's modified (then null: it
    // arrives as ChunkData)
    std::function<ChunkPtr(int, int)> chunkSource() {
        return [this](int cx, int cz) -> ChunkPtr {
            { std::lock_guard<std::mutex> g(m); if (modified.count({ cx, cz })) return nullptr; }
            return gen->generateChunk(cx, cz);
        };
    }
    bool isModified(int cx, int cz) { std::lock_guard<std::mutex> g(m); return modified.count({ cx, cz }) > 0; }

    // Has the server placed you, and is the ground under you here
    // yet? Until both, no input is
    // sent or predicted (you'd fall through it here while the
    // server has you standing): both sides wait together.
    bool groundReady() const {
        return placed && world && world->isLoaded(ChunkManager::chunkCoord((int)std::floor(me.position.x)),
                                        ChunkManager::chunkCoord((int)std::floor(me.position.z)));
    }

    // One tick of your own: sent, and predicted here
    void step(const HumanIntent& in, glm::vec3 facing, float pitch, float dt) {
        if (state != State::Joined || !physics || !groundReady()) return;
        sendInput(in, facing, pitch);
        glm::vec3 fl(facing.x, 0.0f, facing.z);
        if (glm::length(fl) > 1e-3f) me.facing = glm::normalize(fl);
        HumanIntent pin = in;
        physics->moveHuman(me, pin, dt);
        afterPrediction();
    }
    // The same in two halves, for the game: it sends the tick's
    // input, runs its own simulation for the prediction (attacks,
    // stamina and all), then says so
    void sendInput(const HumanIntent& in, glm::vec3 facing, float pitch) {
        tick++;
        proto::InputFrame f; f.tick = tick; f.intent = in; f.facing = facing; f.pitch = pitch;
        f.viewTick = (uint32_t)std::max(0.0f, renderTick - (float)INTERP_TICKS);    // what it's showing of others
        sentFrames.push_back(f);
        while (sentFrames.size() > (size_t)proto::INPUT_REDUNDANCY) sentFrames.pop_front();
        conn->send(proto::input(std::vector<proto::InputFrame>(sentFrames.begin(), sentFrames.end()), &ids), net::Channel::Unreliable);
        // how the inventory is arranged, when it's changed
        std::string now = loadoutKey();
        if (now != loadoutSent) { conn->send(proto::loadout(me.inventory, me.combatStance, &ids), net::Channel::Reliable); loadoutSent = now; }
    }
    void afterPrediction() {
        predicted[tick] = me.position;
        predictedVel[tick] = me.velocity;
        while (predictedVel.size() > 240) predictedVel.erase(predictedVel.begin());
        while (predicted.size() > 240) predicted.erase(predicted.begin());
        renderTick += 1.0f;
    }

    // Another body, where it was INTERP_TICKS before the newest
    // snapshot (between the two snapshots around that moment)
    bool sample(uint64_t id, proto::BodyState& out) const {
        auto it = bodies.find(id);
        if (it == bodies.end() || it->second.history.empty()) return false;
        const auto& h = it->second.history;
        float t = renderTick - (float)INTERP_TICKS;
        if (t <= (float)h.front().first) { out = h.front().second; return true; }
        for (size_t i = 1; i < h.size(); i++)
            if (t <= (float)h[i].first) {
                const auto& a = h[i - 1]; const auto& b = h[i];
                float k = (t - (float)a.first) / std::max(1.0f, (float)(b.first - a.first));
                out = b.second;
                out.pos = glm::mix(a.second.pos, b.second.pos, k);
                glm::vec3 fa = a.second.facing, fb = b.second.facing;
                glm::vec3 f = fa + (fb - fa) * k;
                out.facing = glm::length(f) > 1e-3f ? glm::normalize(f) : fb;
                return true;
            }
        out = h.back().second;           // ahead of the newest: hold it
        return true;
    }

    size_t chunksReceived() const { return chunksIn; }

private:
    Player ownMe;
    WorldSaveManager ownInfo;
    struct Parts {
        std::unique_ptr<TerrainGenerator> gen;
        std::unique_ptr<ChunkManager> world;
        std::unique_ptr<IPhysics> physics;
        std::unique_ptr<WorldEditor> editor;
        std::unique_ptr<StructureManager> structs;
    } own;
    std::shared_ptr<net::IConnection> conn;
    std::string cache, folder;
    GameSession* session = nullptr;
    std::string loadoutSent;
    std::string loadoutKey() const {
        return proto::kvText([&](std::ostream& o) { me.inventory.write(o, "inv"); }) + "stance=" + (me.combatStance ? "1" : "0") + "\n";
    }
    std::mutex m;
    std::set<std::pair<int, int>> modified;
    std::map<std::pair<int, int>, ChunkPtr> received;     // before the world exists
    std::string pendingStructures;
    std::deque<proto::InputFrame> sentFrames;
    std::map<uint32_t, glm::vec3> predicted;
    std::map<uint32_t, glm::vec3> predictedVel;            // and how fast you were going then
    float renderTick = 0.0f;                               // in server ticks, for sampling bodies
    size_t chunksIn = 0;

    void handle(const net::Packet& pk) {
        wire::Reader r(pk.data);
        auto kind = (proto::Msg)r.u8();
        switch (kind) {
        case proto::Msg::Welcome: {
            proto::Welcome w = proto::readWelcome(r);
            if (!r.ok()) return;
            myId = w.you;
            me.uuid = w.you;
            // its numbers for blocks and items → ours; what we lack, said once
            ids.build(w.names);
            auto listOf = [](const std::vector<std::string>& v) {
                std::string out;
                for (size_t i = 0; i < v.size() && i < 6; i++) out += (i ? ", " : "") + v[i];
                if (v.size() > 6) out += " and " + std::to_string(v.size() - 6) + " more";
                return out;
            };
            if (!ids.missingBlocks.empty())
                notices.push_back("this server has blocks your game doesn't: " + listOf(ids.missingBlocks) + " (shown as stone or air)");
            if (!ids.missingItems.empty())
                notices.push_back("this server has items your game doesn't: " + listOf(ids.missingItems) + " - update to use them");
            proto::kvRead(w.player, [&](const std::string& k, const std::string& v) { me.read(k, v); });
            me.inventory.finishLoad();
            me.devInventory.finishLoad();
            loadoutSent = loadoutKey();
            folder = cache + "/" + ServerSafe(w.worldName);
            std::filesystem::create_directories(folder);
            { std::ofstream f(folder + "/world.vxrp"); f << w.worldInfo; }
            info.loadFolder(folder);
            serverTick = w.serverTick;
            renderTick = (float)serverTick;
            state = State::Joined;
            break;
        }
        case proto::Msg::Refused:
            refusedWhy = r.str(1024);
            state = State::Refused;
            conn->close();
            break;
        case proto::Msg::Notice: notices.push_back(r.str(4096)); break;
        case proto::Msg::PlayerState: {
            // the inventory as the server has it
            std::string text = r.str(1 << 16);
            if (!r.ok()) return;
            me.inventory = Inventory();
            proto::kvRead(text, [&](const std::string& k, const std::string& v) {
                if (k == "stance") me.combatStance = v == "1";
                else me.inventory.read(k, v, "inv");
            });
            me.inventory.finishLoad();
            loadoutSent = loadoutKey();
            break;
        }
        case proto::Msg::Modified: {
            auto cols = proto::readModified(r);
            std::lock_guard<std::mutex> g(m);
            for (auto& c : cols) modified.insert(c);
            break;
        }
        case proto::Msg::ChunkData: {
            int cx, cz;
            ChunkPtr ch = makeChunk();
            if (!proto::readChunkData(r, cx, cz, *ch, &ids) || !ChunkManager::inWorld(cx, cz)) return;
            { std::lock_guard<std::mutex> g(m); modified.insert({ cx, cz }); }
            chunksIn++;
            if (world) { world->setChunk(cx, cz, std::move(ch)); world->markDirty(cx, cz); world->clearDirty(cx, cz); }
            else received[{ cx, cz }] = std::move(ch);
            break;
        }
        case proto::Msg::Edits: {
            for (const proto::VoxelEdit& e : proto::readEdits(r, &ids)) {
                if (!world) continue;
                int cx = ChunkManager::chunkCoord(e.p.x), cz = ChunkManager::chunkCoord(e.p.z);
                Chunk* c = world->getChunk(cx, cz);
                if (!c || e.p.y < 0 || e.p.y >= WORLD_HEIGHT) continue;
                c->setBlock(e.p.x - cx * VOXEL_DIM, e.p.y, e.p.z - cz * VOXEL_DIM, e.block);
                world->markDirty(cx, cz, ChunkManager::sectionBit(e.p.y));
                world->clearDirty(cx, cz);           // remeshed, not saved: the server keeps the world
            }
            break;
        }
        case proto::Msg::Structures: {
            std::string text = r.str(64 << 20);
            if (structs) { std::istringstream in(text); structs->read(in); }
            else pendingStructures = text;
            break;
        }
        case proto::Msg::Spawn: {
            proto::SpawnMsg s = proto::readSpawn(r);
            if (!r.ok()) return;
            Remote& b = bodies[s.id];           // (sent again when its gear changes: the history stays)
            b.info = s;
            b.look = Appearance();
            proto::kvRead(s.look, [&](const std::string& k, const std::string& v) { b.look.read(k, v); });
            break;
        }
        case proto::Msg::Despawn: bodies.erase(r.u64()); break;
        case proto::Msg::Snapshot: {
            proto::Snapshot s = proto::readSnapshot(r);
            if (!r.ok() || s.serverTick <= lastSnapshot) return;          // late or repeated: older news
            lastSnapshot = s.serverTick;
            serverTick = s.serverTick;
            if (renderTick < (float)serverTick - 30.0f || renderTick > (float)serverTick + 30.0f) renderTick = (float)serverTick;
            clockTime = s.clock;
            for (const proto::BodyState& b : s.bodies) {
                auto it = bodies.find(b.id);
                if (it == bodies.end()) continue;                       // its Spawn first
                it->second.history.push_back({ s.serverTick, b });
                while (it->second.history.size() > 20) it->second.history.pop_front();
            }
            reconcile(s);
            break;
        }
        case proto::Msg::Event: events.push_back(proto::readEvent(r)); break;
        default: break;
        }
    }
    uint32_t lastSnapshot = 0;

    // The server's word on you: health and the rest as given; where
    // you are corrected by how far the prediction was off
    void reconcile(const proto::Snapshot& s) {
        me.health = s.you.health;
        me.maxHealth = s.you.maxHealth;
        me.stamina = s.you.stamina;
        auto it = predicted.find(s.ackTick);
        if (it == predicted.end()) {
            if (s.ackTick == 0 || predicted.empty()) { me.position = s.you.pos; me.velocity = s.you.vel; placed = true; }
            return;
        }
        glm::vec3 err = s.you.pos - it->second;
        worstError = std::max(worstError, glm::length(err));
        if (glm::length(err) > SNAP_ERROR) {
            // where you are, and how fast you're going: put right by
            // the same difference (a fall or a jump otherwise keeps
            // drifting). The game eases the camera over the jump
            // (renderOffset), so a small correction isn't seen as one.
            me.position += err;
            auto v = predictedVel.find(s.ackTick);
            if (v != predictedVel.end()) {
                glm::vec3 verr = s.you.vel - v->second;
                me.velocity += verr;
                for (auto jt = v; jt != predictedVel.end(); ++jt) jt->second += verr;
            }
            for (auto jt = it; jt != predicted.end(); ++jt) jt->second += err;
            if (glm::length(err) < BIG_ERROR) me.renderOffset -= err;
            corrections++;
            if (std::getenv("VRPG_NETDEBUG")) std::fprintf(stderr, "correction tick %u err %.3f (%.2f %.2f %.2f) server pos %.2f %.2f %.2f vel %.2f %.2f %.2f\n", s.ackTick, glm::length(err), err.x, err.y, err.z, s.you.pos.x, s.you.pos.y, s.you.pos.z, s.you.vel.x, s.you.vel.y, s.you.vel.z);
        }
        predictedVel.erase(predictedVel.begin(), predictedVel.lower_bound(s.ackTick));
        predicted.erase(predicted.begin(), it);
    }
public:
    int corrections = 0;                       // how often the prediction was put right (tests, the debug overlay)
    float worstError = 0.0f;                   // the largest misprediction seen (voxels)
    bool  placed = false;                      // the server has said where you are (nothing is predicted before)
private:
    static std::string ServerSafe(const std::string& n) {
        std::string out;
        for (char ch : n) out += (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-') ? ch : '_';
        return out.empty() ? std::string("world") : out;
    }
};
