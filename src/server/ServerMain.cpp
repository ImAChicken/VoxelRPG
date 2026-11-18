// ══════════════════════════════════════════════════════════
// VoxelRPG-Server — a world with no window
// ══════════════════════════════════════════════════════════
// Runs one world headless: the same GameSession the game uses
// (server/GameSession.h), ticked at 60 Hz, with chunks kept
// loaded around everyone in it (ChunkKeeper).
//
//   VoxelRPG-Server [world] [--dev] [--port N] [--ticks N]
//
// If the world isn't there it's made (server.toml new_world:
// flat by default), so the server can run with no game beside it.
//
// Settings come from server.toml (made with the defaults the
// first time); the command line overrides them. Run from the
// game folder (it reads data/ and the world folders).
//
// Saving (server/SaveStaging.h): the world on disk is never
// written while the server runs. Autosaves are checkpoints into
// the world's staging folder; the files are replaced only when
// you stop the server with a save, or type 'save'.
//
// Commands (type them in the server window):
//   status        players, NPCs, chunks, time of day
//   save          save and commit now
//   stop          save, commit and quit   (Ctrl+C does the same)
//   quit-nosave   quit without committing (the checkpoints are
//                 set aside next time the world opens)
#include "GameSession.h"
#include "ChunkKeeper.h"
#include "ServerHost.h"
#include "../net/EnetTransport.h"
#include "../worldgen/WorldType.h"
#include <toml++/toml.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::atomic<bool> gStop{ false };
void onSignal(int) { gStop = true; }

struct ServerConfig {
    std::string world = "server";
    bool        dev = false;
    int         port = 27015;
    int         maxPlayers = 20;
    float       checkpointSeconds = 300.0f;     // autosave into staging this often
    std::string defaultTrust = "standard";      // full | standard | thin (phase 4)
    std::vector<std::string> starterKit;         // empty: the game's kit (data/registry/kits.toml)
    std::string newWorld = "flat";              // made if the world isn't there: flat | classic | island | none
    uint64_t    newSeed = 0;                    // 0: from the clock

    void load(const char* path) {
        std::ifstream probe(path);
        if (!probe) { write(path); std::cout << "Made " << path << " with the defaults\n"; return; }
        try {
            auto t = toml::parse_file(path);
            world = t["world"].value_or(world);
            dev = t["dev_world"].value_or(dev);
            port = (int)t["port"].value_or((int64_t)port);
            maxPlayers = (int)t["max_players"].value_or((int64_t)maxPlayers);
            checkpointSeconds = (float)t["checkpoint_seconds"].value_or((double)checkpointSeconds);
            defaultTrust = t["default_trust"].value_or(defaultTrust);
            newWorld = t["new_world"].value_or(newWorld);
            newSeed = (uint64_t)t["new_world_seed"].value_or((int64_t)newSeed);
            if (auto* k = t["starter_kit"].as_array()) {
                starterKit.clear();
                for (auto& x : *k) if (auto v = x.value<std::string>()) starterKit.push_back(*v);
            }
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << " (using the defaults)\n";
        }
    }
    void write(const char* path) const {
        std::ofstream f(path);
        f << "# VoxelRPG server settings (the command line overrides them)\n"
          << "world = \"" << world << "\"            # a folder in saves/ (or devworlds/ with dev_world)\n"
          << "dev_world = " << (dev ? "true" : "false") << "\n"
          << "port = " << port << "\n"
          << "max_players = " << maxPlayers << "\n"
          << "checkpoint_seconds = " << checkpointSeconds << "   # autosave into staging; files are replaced only on stop / save\n"
          << "default_trust = \"" << defaultTrust << "\"   # full | standard | thin\n"
          << "starter_kit = [";
        for (size_t i = 0; i < starterKit.size(); i++) f << (i ? ", " : "") << "\"" << starterKit[i] << "\"";
        f << "]   # what a new player starts with (empty: the game's kit, data/registry/kits.toml)\n"
          << "new_world = \"" << newWorld << "\"        # if the world isn't there, make one: flat | classic | island | none\n"
          << "new_world_seed = " << newSeed << "         # 0: a new seed each time\n";
    }
};

// Lines typed into the server window, read on their own thread
struct ConsoleInput {
    std::mutex m;
    std::deque<std::string> lines;
    void start() {
        std::thread([this] {
            std::string l;
            while (std::getline(std::cin, l)) { std::lock_guard<std::mutex> g(m); lines.push_back(l); }
        }).detach();
    }
    bool next(std::string& out) {
        std::lock_guard<std::mutex> g(m);
        if (lines.empty()) return false;
        out = lines.front(); lines.pop_front();
        return true;
    }
};

bool loadData() {
    if (!gRegistry.load("data/registry/blocks.toml")) return false;
    gItems.load(gRegistry, "data/registry/items.toml");
    gMovesets.load("data/registry/movesets.toml");
    gHeldModels.loadDir("data/models/held");
    gFactions.load("data/registry/factions.toml");
    gLoot.load("data/registry/loot.toml");
    gKits.load("data/registry/kits.toml");
    gNpcTypes.load("data/registry/npcs.toml", "data/characters/presets.toml");
    gAnims.load("data/animations", "data/animations/retarget.toml");     // poses for hits (sim/BodyPoses.h)
    gAnimMap.load("data/animations/anim_map.toml");
    decor::current() = decor::Loader::load(gRegistry, "data/registry/decorations.toml");
    gItems.addDecor(*decor::current());
    gStructures.load("data/registry/structures.toml");
    gStructures.addDecorTrees(*decor::current());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "VoxelRPG server\n";
    ServerConfig cfg;
    cfg.load("server.toml");
    long maxTicks = -1;                       // --ticks N: run N ticks, then stop (tests)
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--dev") cfg.dev = true;
        else if (a == "--port" && i + 1 < argc) cfg.port = std::atoi(argv[++i]);
        else if (a == "--ticks" && i + 1 < argc) maxTicks = std::atol(argv[++i]);
        else if (a.rfind("--", 0) != 0) cfg.world = a;
    }
    if (!loadData()) { std::cerr << "Couldn't load data/ (run from the game folder)\n"; return 1; }

    Simulation sim;
    GameSession session(sim);
    session.log = [](const std::string& s) { std::cout << s << "\n"; };
    if (!session.begin(cfg.world, cfg.dev)) {
        // not there: make one (server.toml new_world), so a machine
        // with only the server can start from nothing
        WorldType type = cfg.newWorld == "island" ? WorldType::Island : cfg.newWorld == "classic" ? WorldType::Classic : WorldType::Flat;
        if (cfg.newWorld != "flat" && cfg.newWorld != "classic" && cfg.newWorld != "island") {
            std::cerr << "No world '" << cfg.world << "' in " << WorldSaveManager::rootFor(cfg.dev)
                      << "/ (copy one in, make one in the game, or set new_world in server.toml)\n";
            return 1;
        }
        uint64_t seed = cfg.newSeed ? cfg.newSeed : (uint64_t)std::chrono::system_clock::now().time_since_epoch().count();
        std::cout << "No world '" << cfg.world << "' yet: making a " << cfg.newWorld << " one (seed " << seed << ")"
                  << (type == WorldType::Island ? " - shaping the land takes a minute\n" : "\n");
        if (!session.beginNew(cfg.world, seed, GameMode::Survival, type, worldSizeFor(type, 96), -1,
                              type == WorldType::Island ? TerrainStyle::GuidedRivers : TerrainStyle::NoErosion,
                              WorldGenParams(), FlatOptions(), cfg.dev)) {
            std::cerr << "Couldn't make the world folder\n";
            return 1;
        }
    }
    std::cout << "Opening " << session.folder() << " (" << worldTypeName(gWorldSave.getWorldType()) << ", "
              << WORLD_SIZE_X << " x " << WORLD_SIZE_Z << " chunks)\n";
    session.buildGenerator();
    session.attach();
    ChunkKeeper keeper(*session.world, session.chunkSource());
    glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z));
    keeper.loadNow({ spawn });
    auto listener = net::enetListen(cfg.port, cfg.maxPlayers);
    if (!listener) { std::cerr << "Couldn't listen on port " << cfg.port << " (is another server running?)\n"; return 1; }
    ServerHost host(session, *listener);
    host.maxPlayers = cfg.maxPlayers;
    host.starterKit = cfg.starterKit;
    auto saveAll = [&](bool commit) { return session.save(commit, [&](const std::string& st) { host.savePlayers(st); }); };
    std::cout << "Ready: port " << cfg.port << " (UDP), up to " << cfg.maxPlayers << " players\n"
              << "Type status, save, stop or quit-nosave\n";

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    ConsoleInput console;
    if (maxTicks < 0) console.start();

    using clock = std::chrono::steady_clock;
    const float DT = 1.0f / 60.0f;
    auto next = clock::now();
    float sinceCheckpoint = 0.0f;
    long ticks = 0;
    bool save = true;
    while (!gStop) {
        // ground around the spawn and every player
        std::vector<glm::vec3> points{ spawn };
        for (const auto& p : sim.players()) points.push_back(p->position);
        host.poll();
        keeper.update(points);
        host.beforeStep();
        session.syncSpawners();
        session.updateEdits(2.0f);
        sim.step(DT);
        gClock.update(DT);
        host.afterStep();
        host.takeEvents();

        sinceCheckpoint += DT;
        if (sinceCheckpoint >= cfg.checkpointSeconds) { saveAll(false); sinceCheckpoint = 0.0f; }

        std::string cmd;
        while (console.next(cmd)) {
            if (cmd == "status") {
                std::printf("players %zu  npcs %zu  chunks %d (loading %zu)  time %.2f\n", sim.players().size(),
                            sim.npcs().all().size(), session.world->loadedCount(), keeper.loading(), gClock.time);
                for (auto& c : host.clients()) if (c->joined) std::printf("  %s  %.0f ms\n", c->name.c_str(), c->conn->pingMs());
            }
            else if (cmd == "save") saveAll(true);
            else if (cmd == "stop") gStop = true;
            else if (cmd == "quit-nosave") { save = false; gStop = true; }
            else if (!cmd.empty()) std::cout << "commands: status, save, stop, quit-nosave\n";
        }
        if (maxTicks >= 0 && ++ticks >= maxTicks) break;

        next += std::chrono::microseconds(16667);
        auto now = clock::now();
        if (next > now) std::this_thread::sleep_until(next);
        else if (now - next > std::chrono::milliseconds(250)) next = now;      // fell behind: don't spiral
    }
    keeper.stop();                            // its workers use the generator
    host.kickAll("the server is stopping");   // their player files go into the save
    listener->poll();
    if (save) { std::cout << "Saving...\n"; saveAll(true); }
    session.close();
    std::cout << "Server stopped\n";
    return 0;
}
