// ══════════════════════════════════════════════════════════
// VoxelRPG-Server — a world with no window
// ══════════════════════════════════════════════════════════
// Runs a realm headless (server/RealmServer.h): its main world and the
// instances its gate opens, each the same GameSession the game uses
// (server/GameSession.h), ticked at 60 Hz, with chunks kept loaded
// around everyone in them (Worlds: a ChunkKeeper per world).
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
#include "app/CrashReport.h"
#include "app/Log.h"
#include <ctime>
#include "GameSession.h"
#include "ChunkKeeper.h"
#include "ServerHost.h"
#include "RealmServer.h"
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
#include <sstream>
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
    gBlueprints.load();          // villages at the world's sites (GameSession::tickVillages)
    gBuildPalettes.load();
    gVillagers.load();
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
    // logs/server.txt and crash reports in logs/ (app/Log.h, app/CrashReport.h);
    // not for test runs (--ticks), which would fill the repo's logs/
    if (maxTicks < 0) applog::start("server.txt");
    crash::install("VoxelRPG-Server", false);
    {
        std::time_t now = std::time(nullptr);
        char when[32];
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::cout << "VoxelRPG server started " << when << ", network version " << proto::VERSION << "\n";
    }
    if (!loadData()) { std::cerr << "Couldn't load data/ (run from the game folder)\n"; return 1; }

    RealmServer server;
    server.log = [](const std::string& s) { std::cout << s << "\n"; };
    server.worlds.log = server.log;
    server.maxPlayers = cfg.maxPlayers;
    server.starterKit = cfg.starterKit;
    GameSession* mainWorld = server.worlds.begin(cfg.world, cfg.dev);
    if (!mainWorld) {
        // not there: make one (server.toml new_world), so a machine
        // with only the server can start from nothing
        WorldType type = cfg.newWorld == "island" ? WorldType::Island : cfg.newWorld == "classic" ? WorldType::Classic :
                         cfg.newWorld == "dungeon" ? WorldType::Dungeon : WorldType::Flat;
        if (cfg.newWorld != "flat" && cfg.newWorld != "classic" && cfg.newWorld != "island" && cfg.newWorld != "dungeon") {
            std::cerr << "No world '" << cfg.world << "' in " << WorldSaveManager::rootFor(cfg.dev)
                      << "/ (copy one in, make one in the game, or set new_world in server.toml)\n";
            return 1;
        }
        uint64_t seed = cfg.newSeed ? cfg.newSeed : (uint64_t)std::chrono::system_clock::now().time_since_epoch().count();
        std::cout << "No world '" << cfg.world << "' yet: making a " << cfg.newWorld << " one (seed " << seed << ")"
                  << (type == WorldType::Island ? " - shaping the land takes a minute\n" : "\n");
        mainWorld = server.worlds.create(cfg.world, seed, GameMode::Survival, type, worldSizeFor(type, 96), -1,
                                         type == WorldType::Island ? TerrainStyle::GuidedRivers : TerrainStyle::NoErosion,
                                         WorldGenParams(), FlatOptions(), cfg.dev);
        if (!mainWorld) { std::cerr << "Couldn't make the world folder\n"; return 1; }
    }
    GameSession& session = *mainWorld;
    std::cout << "Opening " << session.folder() << " (" << worldTypeName(session.info.getWorldType()) << ")\n";
    session.buildGenerator();
    session.attach();
    std::cout << session.world->sizeX() << " x " << session.world->sizeZ() << " chunks\n";
    {
        glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(session.world->sizeX(), session.world->sizeZ()));
        ChunkKeeper first(*session.world, session.chunkSource());
        first.loadNow({ spawn });
        first.stop();
    }
    auto listener = net::enetListen(cfg.port, cfg.maxPlayers);
    if (!listener) { std::cerr << "Couldn't listen on port " << cfg.port << " (is another server running?)\n"; return 1; }
    server.start(session, cfg.dev, *listener);
    std::cout << "Ready: port " << cfg.port << " (UDP), up to " << cfg.maxPlayers << " players\n"
              << "Type status, save, portal, stop or quit-nosave\n";

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
        server.tick(DT);
        Simulation& sim = session.sim;

        sinceCheckpoint += DT;
        if (sinceCheckpoint >= cfg.checkpointSeconds) { server.save(false); sinceCheckpoint = 0.0f; }

        std::string cmd;
        while (console.next(cmd)) {
            if (cmd == "status") {
                std::printf("players %zu  worlds %zu  time %.2f\n", server.players(), server.worlds.count(), sim.day.time);
                for (GameSession* w : server.worlds.all()) {
                    std::printf("  %s: players %zu  npcs %zu  chunks %d\n", w->name().c_str(), w->sim.players().size(),
                                w->sim.npcs().all().size(), w->world ? w->world->loadedCount() : 0);
                    if (ServerHost* h = server.hostOf(w))
                        for (auto& c : h->clients()) if (c->joined) std::printf("    %s  %.0f ms\n", c->name.c_str(), c->conn->pingMs());
                }
            }
            else if (cmd == "save") server.save(true);
            else if (cmd == "portal" || cmd.rfind("portal ", 0) == 0) {     // the realm's gate and instances (game/Portals.h)
                std::vector<std::string> a;
                std::istringstream ss(cmd.substr(6));
                for (std::string w; ss >> w;) a.push_back(w);
                std::cout << server.portalCommand(a) << "\n";
            }
            else if (cmd == "roaming on" || cmd == "roaming off") {         // roaming enemy spawns (sim/Roaming.h)
                sim.roaming.enabled = cmd == "roaming on";
                sim.roaming.resetTimers();
                session.info.setRoamingSpawns(sim.roaming.enabled);
                std::cout << "roaming spawns " << (sim.roaming.enabled ? "on" : "off") << "\n";
            }
            else if (cmd == "roaming reload") std::cout << (gRoaming.load() ? "roaming.toml reloaded\n" : "roaming.toml didn't load\n");
            else if (cmd == "roaming") {
                std::cout << "roaming spawns " << (sim.roaming.enabled ? "on" : "off") << ", " << sim.roaming.alive(sim.npcs())
                          << " roaming in the world, " << sim.roaming.groupsMade << " groups made";
                if (!sim.roaming.lastGroup.empty()) std::cout << "; last: " << sim.roaming.lastGroup;
                std::cout << "\n";
            }
            else if (cmd == "stop") gStop = true;
            else if (cmd == "quit-nosave") { save = false; gStop = true; }
            else if (!cmd.empty()) std::cout << "commands: status, save, portal [open <dungeon>|close|time <min>], roaming [on|off|reload], stop, quit-nosave\n";
        }
        if (maxTicks >= 0 && ++ticks >= maxTicks) break;

        next += std::chrono::microseconds(16667);
        auto now = clock::now();
        if (next > now) std::this_thread::sleep_until(next);
        else if (now - next > std::chrono::milliseconds(250)) next = now;      // fell behind: don't spiral
    }
    server.shutdown("the server is stopping");   // their player files go into the save
    listener->poll();
    if (save) { std::cout << "Saving...\n"; server.save(true); }
    server.worlds.closeAll(false);
    std::cout << "Server stopped\n";
    return 0;
}
