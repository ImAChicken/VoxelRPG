#pragma once
#include "SaveStaging.h"
#include "../chunk/ChunkLoader.h"
#include "../chunk/ChunkManager.h"
#include "../physics/VoxelPhysics.h"
#include "../sim/Simulation.h"
#include "../world/Chests.h"
#include "../world/DayClock.h"
#include "../world/StructureManager.h"
#include "../world/Woodcutting.h"
#include "../world/WorldEditor.h"
#include "../world/WorldSaveManager.h"
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
        if (int n = sim.npcs().load(path + "/npcs.dat")) std::cout << n << " NPCs loaded\n";
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
        structs->forEachInstance([this](const StructureInstance& si) {
            const StructureType* t = gStructures.get(si.type);
            if (t && t->kind == StructureKind::Spawner)
                placed.push_back({ si.id, si.type, glm::vec3(si.origin) + glm::vec3(0.5f, 0.0f, 0.5f) });
        });
        sim.syncSpawners(placed);
    }
    // Queued edits a few milliseconds a frame
    void updateEdits(float budgetMs = 4.0f) { if (editor) editor->update(budgetMs); }

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
        if (extra) extra(stage);
        if (chunks) std::cout << "Saved " << chunks << " chunks" << (commit ? "" : " (checkpoint)") << "\n";
        if (commit) ok = savestage::commit(path) && ok;
        return ok;
    }

    // Let go of the world without saving (save first if wanted)
    void close() {
        if (world || physics) sim.detachWorld();
        wood.reset();
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
};
