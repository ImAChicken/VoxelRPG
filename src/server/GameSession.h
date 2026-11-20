#pragma once
#include "SaveStaging.h"
#include "../chunk/ChunkLoader.h"
#include "../chunk/ChunkManager.h"
#include "../physics/VoxelPhysics.h"
#include "../sim/Simulation.h"
#include "../world/Chests.h"
#include "../world/DayClock.h"
#include "../world/StructureManager.h"
#include "../dungeon/Run.h"
#include "../world/Woodcutting.h"
#include "../world/WorldEditor.h"
#include "../world/WorldSaveManager.h"
#include "../world/TerrainOps.h"
#include "../game/Village.h"
#include "../game/Villagers.h"
#include "../worldgen/WorldType.h"
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// GameSession — one open world, with no graphics
// ══════════════════════════════════════════════════════════
// Everything a world is made of while it runs: the generator,
// the voxels (ChunkManager), physics, the editor, structures,
// chests and other containers, felled trees and wood on the
// ground, and the Simulation (players, NPCs, spawners, fights).
// The headless server (server/ServerMain.cpp) runs one; so does
// the game, for single player — the same code, so a world plays
// the same either way.
//
// What the session does NOT own: anything drawn (meshes, the far
// view, the sky), and how chunks get loaded (the game's
// WorldStreamer around the camera, the server's ChunkKeeper
// around every player). Both load through chunkSource().
//
// Opening is three steps so the slow one can run on a worker
// thread while the game shows a progress screen:
//   begin(name, dev)    read world.vxrp (and finish or set
//                       aside an interrupted save)
//   buildGenerator()    the generator (island worlds: the erosion
//                       bake, seconds the first time)
//   attach()            everything else, loaded from the folder
//
// Saving (server/SaveStaging.h): save(commit) writes the world
// into its staging folder; commit = true puts it in place. A
// server checkpoints (commit = false) and commits on shutdown
// or a forced save; single player commits every time.
class GameSession {
public:
    explicit GameSession(Simulation& s) : sim(s) {}
    ~GameSession() { close(); }

    Simulation& sim;
    std::unique_ptr<TerrainGenerator> gen;
    std::unique_ptr<ChunkManager>     world;
    std::unique_ptr<IPhysics>         physics;
    std::unique_ptr<WorldEditor>      editor;
    std::unique_ptr<StructureManager> structs;
    ChestStore                        chests;
    std::unique_ptr<Woodcutting>      wood;
    std::function<void(const std::string&)> log = [](const std::string&) {};
    std::string recovered;      // what begin() did about an interrupted save ("" = nothing)

    bool open() const { return (bool)world; }
    const std::string& folder() const { return path; }

    // ── Opening ───────────────────────────────────────────
    bool begin(const std::string& name, bool dev) {
        close();
        if (!gWorldSave.loadWorld(name, dev)) return false;
        path = gWorldSave.getWorldPath();
        recovered = savestage::recover(path);
        if (!recovered.empty()) std::cout << name << ": " << recovered << "\n";
        WORLD_SIZE_X = WORLD_SIZE_Z = gWorldSave.getWorldSize();
        return true;
    }
    // A new world: made on disk, then opened like any other
    bool beginNew(const std::string& name, uint64_t seed, GameMode mode, WorldType type, int size, int biome,
                  TerrainStyle terrain, const WorldGenParams& params, const FlatOptions& flat, bool dev) {
        close();
        if (!gWorldSave.createWorld(name, seed, mode, type, size, biome, terrain, params, flat, dev)) return false;
        path = gWorldSave.getWorldPath();
        WORLD_SIZE_X = WORLD_SIZE_Z = gWorldSave.getWorldSize();
        return true;
    }
    void buildGenerator() {
        gen = makeGenerator(gWorldSave.getWorldType(), gWorldSave.getSeed(), gWorldSave.getWorldSize(),
                            gWorldSave.getWorldBiome(), gWorldSave.getTerrainStyle(), path + "/terrain.bake",
                            gWorldSave.getGenParams(), gWorldSave.getFlatOptions());
    }
    void attach() {
        world = std::make_unique<ChunkManager>();
        physics = std::make_unique<VoxelPhysics>();
        physics->init(*world);
        editor = std::make_unique<WorldEditor>(*world);
        structs = std::make_unique<StructureManager>(*world, *editor, *gen);
        structs->log = log;
        structs->load(path);
        chests.worldSeed = (uint32_t)gWorldSave.getSeed();
        chests.load(path);
        wood = std::make_unique<Woodcutting>();
        wood->load(path);
        // a chest (anything with storage) that goes (broken,
        // removed, collapsed) spills what it held as pickups —
        // its loot rolled now if it was never opened
        structs->onRemoved = [this](const StructureInstance& si) {
            const StructureType* t = gStructures.get(si.type);
            glm::vec3 at = glm::vec3(si.origin) + glm::vec3(0.5f, 2.0f, 0.5f);
            for (auto& [item, n] : chests.take(si.id, t)) wood->drop(item, n, at, glm::vec3(0.0f));
        };
        // one a player builds starts empty (no loot from building
        // chests); a command or the world rolls its loot table
        structs->onPlaced = [this](const StructureInstance& si, EditSource src) {
            const StructureType* t = gStructures.get(si.type);
            if (ChestStore::holds(t) && (src == EditSource::Player || src == EditSource::Dev)) chests.makeEmpty(si.id, *t);
        };
        // bodies collide with fallen logs; raycasts hit them
        physics->setLooseSolids([this](int x, int y, int z) { return wood && wood->solidAt(x, y, z); });
        sim.attachWorld(physics.get(), wood.get());
        // NPCs open and shut doors (villagers going home)
        sim.doorHook = [this](uint32_t id, bool open, const std::function<bool(glm::ivec3)>& bodyBlocks) {
            StructureInstance* d = structs ? structs->get(id) : nullptr;
            if (!d) return false;
            if ((d->state != 0) == open) return true;
            return structs->toggleDoor(*d, EditSource::Script, bodyBlocks).empty();
        };
        // roaming spawns (sim/Roaming.h): on or off is the world's
        // setting; the world says what biome a spot is and whether
        // it's no place for one (water, near anything built, a villager)
        sim.roaming.enabled = gWorldSave.getRoamingSpawns();
        sim.roaming.resetTimers();
        sim.roaming.worldSpawn = glm::vec3(gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z));
        sim.roaming.hasWorldSpawn = true;
        sim.roaming.biomeAt = [this](int x, int z) {
            int e = gen ? (int)gen->column(x, z).eco : -1;
            return e == 255 ? -1 : e;
        };
        sim.roaming.spotBlocked = [this](glm::vec3 p) -> std::string {
            int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
            for (int dy : { 0, -1, 1 }) {
                uint16_t id = world ? world->typeAtVoxel(x, y + dy, z) : 0;
                if (id == BLOCK_WATER || id == BLOCK_LAVA) return "in water";
            }
            float r = gRoaming.settings.safeRadius, r2 = r * r;
            for (const glm::vec3& b : built) {
                float dx = b.x - p.x, dz = b.z - p.z;
                if (dx * dx + dz * dz < r2) return "near something built";
            }
            for (auto& n : sim.npcs().all()) {
                if (n->dead() || !(n->life.on || n->typeId == "guard")) continue;
                float dx = n->position.x - p.x, dz = n->position.z - p.z;
                if (dx * dx + dz * dz < r2) return "near a villager";
            }
            return "";
        };
        if (int n = sim.npcs().load(path + "/npcs.dat")) std::cout << n << " NPCs loaded\n";
        // the world's village sites, and the ones already built
        sites = gen->villageSites();
        sitesBuilt.assign(sites.size(), false);
        {
            std::ifstream vf(path + "/villages.dat");
            int x, z;
            while (vf >> x >> z)
                for (size_t i = 0; i < sites.size(); i++)
                    if (sites[i].at == glm::ivec2(x, z)) sitesBuilt[i] = true;
        }
        // a Dungeon world: its markers come alive as players get to them
        dungeonRun.reset();
        if (auto* dg = dynamic_cast<const DungeonGenerator*>(gen.get()); dg && dg->generated()) {
            dungeonRun = std::make_unique<dungeon::Run>(*dg);
            dungeonRun->load(path);
        }
        std::ifstream tf(path + "/time.txt");
        float t; gClock.time = (tf >> t) ? std::fmod(std::fabs(t), 1.0f) : 0.35f;
    }

    // A server's world, mirrored (client/ClientWorld.h builds gen,
    // world, physics, editor and structs here): the local simulation
    // runs on it for this player's own prediction. Nothing is saved.
    void attachRemote(const std::string& cacheFolder) {
        path = cacheFolder;
        chests.clear();
        wood = std::make_unique<Woodcutting>();
        physics->setLooseSolids([this](int x, int y, int z) { return wood && wood->solidAt(x, y, z); });
        sim.attachWorld(physics.get(), wood.get());
        sim.roaming.enabled = false;            // the server spawns them
        sim.roaming.biomeAt = nullptr;
        sim.roaming.spotBlocked = nullptr;
    }

    // Where a chunk's voxels come from (worker threads may call
    // it): its saved file (a staged one first), else the seed
    std::function<ChunkPtr(int, int)> chunkSource() const {
        const TerrainGenerator* g = gen.get();
        std::string folder = path;
        return [g, folder](int cx, int cz) -> ChunkPtr {
            std::filesystem::path rel = WorldSaveManager::chunkPath("", cx, cz).relative_path();
            std::filesystem::path p = savestage::readPath(folder, rel);
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                ChunkPtr saved = makeChunk();
                if (ChunkLoader::load(p, *saved)) return saved;
            }
            return g->generateChunk(cx, cz);
        };
    }

    // ── Running ───────────────────────────────────────────
    // The spawner structures placed in the world, for the
    // simulation to run (sim/Spawners.h). Every frame.
    void syncSpawners() {
        if (!structs) return;
        placed.clear();
        built.clear();
        structs->forEachInstance([this](const StructureInstance& si) {
            const StructureType* t = gStructures.get(si.type);
            if (t && t->kind == StructureKind::Spawner)
                placed.push_back({ si.id, si.type, glm::vec3(si.origin) + glm::vec3(0.5f, 0.0f, 0.5f) });
            if (!si.natural) built.push_back(glm::vec3(si.origin));     // (no roaming spawns near these)
        });
        sim.syncSpawners(placed);
    }
    // The world's own changes, every tick or frame (not on a
    // client: its server does them):
    //   where spells hit the ground, what they leave (a spell's ice:
    //   effect_place in movesets.toml), set down on the ground there
    //   unless a body is in the way;
    //   structures with a lifetime (structures.toml lifetime) aged,
    //   and taken away when it's up
    int tickWorld(float dt) {
        int placedNow = 0;
        for (const Simulation::Landing& l : sim.takeLandings()) {
            if (!structs) continue;
            uint16_t type = gStructures.find(l.place);
            const StructureType* t = gStructures.get(type);
            if (!t) continue;
            if (placeEffect(type, *t, l)) placedNow++;
        }
        if (structs) structs->tick(dt);
        return placedNow;
    }
    // Queued edits a few milliseconds a frame
    void updateEdits(float budgetMs = 4.0f) { if (editor) editor->update(budgetMs); }

    // A spell's leavings at a landing: on the ground under where it
    // hit (a wall: the ground at its foot); up a voxel or two if
    // that's what it takes to fit. Not
    // where it would shut a body in
    bool placeEffect(uint16_t type, const StructureType& t, const Simulation::Landing& l) {
        glm::ivec3 at((int)std::floor(l.at.x), (int)std::floor(l.at.y), (int)std::floor(l.at.z));
        for (int i = 0; i < 12 && at.y > 0 && !physics->solidAt(at.x, at.y - 1, at.z); i++) at.y--;   // down to the ground
        if (!physics->solidAt(at.x, at.y - 1, at.z)) return false;                                     // nothing to stand on
        const VoxelVolume& v = t.shape(0, 0);
        for (int up = 0; up <= 2; up++) {
            glm::ivec3 o = at + glm::ivec3(0, up, 0);
            if (!structs->checkPlace(type, o, 0, false).empty()) continue;
            glm::vec3 lo = glm::vec3(o - v.anchor), hi = lo + glm::vec3(v.size);
            bool body = false;
            for (Human* b : sim.bodies()) {
                if (b->dead()) continue;
                if (b->position.x + Human::RADIUS > lo.x && b->position.x - Human::RADIUS < hi.x &&
                    b->position.z + Human::RADIUS > lo.z && b->position.z - Human::RADIUS < hi.z &&
                    b->position.y < hi.y && b->position.y + b->height > lo.y) { body = true; break; }
            }
            if (body) return false;
            structs->place(type, o, 0, EditSource::Command, 0);
            return true;
        }
        return false;
    }

    // ── Villages at the world's sites ─────────────────────
    // The generator's village sites (TerrainGenerator::villageSites:
    // the island's villages and hamlets) are built from blueprints
    // (game/Village.h) with their people (game/Villagers.h) the first
    // time a player comes within SITE_NEAR, once the ground round the
    // site has loaded (a lot over unloaded ground would be skipped).
    // Remembered in villages.dat, so each is built once. Not on a
    // client: its server builds them. 'say' hears what was built.
    static constexpr float SITE_NEAR = 420.0f;
    static constexpr int   SITE_REACH = 200;   // ground that must be loaded round the square
    void tickVillages(const std::vector<glm::vec3>& players, const std::function<void(const std::string&)>& say = nullptr) {
        if (!world || !structs || !editor || !gen) return;
        for (size_t i = 0; i < sites.size(); i++) {
            if (sitesBuilt[i]) continue;
            const glm::ivec2 at = sites[i].at;
            float nearest = 1e9f;
            glm::vec3 who(0.0f);
            for (const glm::vec3& p : players) {
                float d = glm::length(glm::vec2(p.x - at.x, p.z - at.y));
                if (d < nearest) { nearest = d; who = p; }
            }
            if (nearest > SITE_NEAR) continue;
            auto loaded = [&](int dx, int dz) { return world->isColumnLoaded(at.x + dx, at.y + dz); };
            const int R = SITE_REACH;
            if (!(loaded(0, 0) && loaded(R, 0) && loaded(-R, 0) && loaded(0, R) && loaded(0, -R) &&
                  loaded(R, R) && loaded(-R, -R) && loaded(R, -R) && loaded(-R, R))) continue;
            int y = terrain::groundLevel(*world, at.x, at.y, 400, 400);
            if (y < 0) continue;
            sitesBuilt[i] = true;
            // a few layouts (seeds, road lengths: hilly ground takes
            // shorter roads); the one with the most buildings is built
            village::Options vo;
            village::Plan vp;
            for (int attempt = 0; attempt < 6; attempt++) {
                village::Options o;
                o.seed = (uint32_t)gWorldSave.getSeed() * 2654435761u + 977u * (uint32_t)(i + 1) + 7919u * (uint32_t)attempt;
                if (sites[i].small) { o.roads = 2 + attempt % 2; o.roadLen = 110 - 10 * (attempt / 2); o.lanes = 2; }
                else { o.roads = 3 + (int)(o.seed % 2); o.roadLen = 100 - 10 * (attempt / 2); }
                if (nearest < 160.0f) { o.keepClear = { (int)who.x, (int)who.z }; o.clearRadius = 15; }   // not on top of anyone
                village::pickPalette(o);
                village::Plan p = village::plan(*world, gBlueprints, glm::ivec3(at.x, y, at.y), o);
                if (attempt == 0 || p.lots.size() > vp.lots.size()) { vp = p; vo = o; }
                if (vp.lots.size() >= (sites[i].small ? 6u : 10u)) break;
            }
            village::Built vb = village::build(vp, *structs, *editor, &chests, vo, EditSource::Script);
            villagers::Populated pop = villagers::populate(vp, vb, *structs, *world, sim.npcs(), vo.seed);
            std::string line = sites[i].name + ": " + std::to_string(vb.buildings) + " buildings, " + std::to_string(pop.villagers) +
                               " villagers" + (pop.guards ? ", " + std::to_string(pop.guards) + " guards" : std::string());
            std::cout << "village built at " << at.x << " " << y << " " << at.y << ": " << line << " (" << vp.lots.size() << " lots, " << vp.roads.size()
                      << " roads" << (vb.failed ? ", " + std::to_string(vb.failed) + " didn't fit: " + vb.why : std::string()) << ")\n";
            if (say) say(line);
        }
    }
    // A Dungeon world's markers (dungeon/Run.h): spawners, coffins, the
    // boss, chests, secret walls. Every tick; not on a client. What it
    // returns is for the game to show (particles, a line)
    std::vector<dungeon::Run::Effect> tickDungeon(const std::vector<Human*>& players, float dt = 1.0f / 60.0f) {
        if (!dungeonRun || !world || !structs || !editor) return {};
        return dungeonRun->tick(players, sim, *world, *structs, chests, *editor, dt);
    }
    const dungeon::Run* dungeon() const { return dungeonRun.get(); }
    const std::vector<TerrainGenerator::VillageSite>& villageSites() const { return sites; }
    bool villageBuilt(size_t i) const { return i < sitesBuilt.size() && sitesBuilt[i]; }

    // ── Saving ────────────────────────────────────────────
    // extra(folder): anything else to write into the same
    // staging folder (the game's player file and far-view
    // memory; the server's player files)
    bool save(bool commit, const std::function<void(const std::string& stageFolder)>& extra = nullptr) {
        if (!world) return false;
        if (editor) editor->flush();
        std::string stage = savestage::stageDir(path).string();
        std::filesystem::create_directories(stage);
        int chunks = 0;
        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
            for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
                if (!world->isDirty(cx, cz)) continue;
                Chunk* c = world->getChunk(cx, cz);
                if (!c) continue;
                std::filesystem::path rel = WorldSaveManager::chunkPath("", cx, cz).relative_path();
                ChunkLoader::save(*c, savestage::writePath(path, rel), cx, 0, cz);
                world->clearDirty(cx, cz);
                chunks++;
            }
        bool ok = true;
        if (structs) ok = structs->save(stage) && ok;
        ok = chests.save(stage) && ok;
        if (wood) ok = wood->save(stage) && ok;
        sim.npcs().save(stage + "/npcs.dat");
        { std::ofstream tf(stage + "/time.txt"); tf << gClock.time << "\n"; }
        {   // the village sites built so far
            std::ofstream vf(stage + "/villages.dat");
            for (size_t i = 0; i < sites.size(); i++) if (sitesBuilt[i]) vf << sites[i].at.x << " " << sites[i].at.y << "\n";
        }
        if (dungeonRun) ok = dungeonRun->save(stage) && ok;
        if (extra) extra(stage);
        if (chunks) std::cout << "Saved " << chunks << " chunks" << (commit ? "" : " (checkpoint)") << "\n";
        if (commit) ok = savestage::commit(path) && ok;
        return ok;
    }

    // Let go of the world without saving (save first if wanted)
    void close() {
        if (world || physics) sim.detachWorld();
        wood.reset();
        dungeonRun.reset();
        structs.reset();
        chests.clear();
        editor.reset();
        physics.reset();
        world.reset();
        gen.reset();
        path.clear();
    }

private:
    std::string path;
    std::vector<Spawners::Placed> placed;
    std::vector<glm::vec3> built;              // every piece anyone built (syncSpawners), for roaming spawns
    std::vector<TerrainGenerator::VillageSite> sites;   // the generator's village sites
    std::vector<bool> sitesBuilt;                        // … built yet (villages.dat)
    std::unique_ptr<dungeon::Run> dungeonRun;            // a Dungeon world's markers (dungeon.dat)
};
