#pragma once
// A small flat world for headless tests: four chunks of stone
// around (128, 128), the real VoxelPhysics, a Simulation, and the
// data registries. Run tests from the repo root.
#include "sim/Simulation.h"
#include "physics/VoxelPhysics.h"
#include <cstdio>
#include <string>

namespace testworld {

inline int failures = 0;
inline void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}
inline const float DT = 1.0f / 60.0f;
inline const int GROUND = 40;     // bodies stand at y = GROUND

inline bool loadData() {
    return gRegistry.load("data/registry/blocks.toml") && gItems.load(gRegistry, "data/registry/items.toml") &&
           gMovesets.load("data/registry/movesets.toml") && gFactions.load("data/registry/factions.toml") &&
           gNpcTypes.load("data/registry/npcs.toml", "data/characters/presets.toml");
}

struct World {
    ChunkManager chunks;
    VoxelPhysics physics;
    Simulation sim;
    World() {
        uint16_t stone = gRegistry.getIDByName("stone");
        for (int cx = 0; cx < 4; cx++)
            for (int cz = 0; cz < 4; cz++) {
                ChunkPtr c = makeChunk();
                for (int x = 0; x < VOXEL_DIM; x++)
                    for (int z = 0; z < VOXEL_DIM; z++)
                        for (int y = 0; y < GROUND; y++) c->setBlock(x, y, z, stone);
                chunks.setChunk(cx, cz, std::move(c));
            }
        physics.init(chunks);
        sim.attachWorld(&physics, nullptr);
    }
    void wall(int x0, int x1, int z0, int z1, int h = 30) {
        uint16_t stone = gRegistry.getIDByName("stone");
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++)
                for (int y = GROUND; y < GROUND + h; y++)
                    if (Chunk* c = chunks.getChunk(x / VOXEL_DIM, z / VOXEL_DIM)) c->setBlock(x % VOXEL_DIM, y, z % VOXEL_DIM, stone);
    }
    void run(float seconds) { for (int i = 0; i < (int)(seconds / DT + 0.5f); i++) sim.step(DT); }
    // run until cond() or the time is up; returns whether cond() became true
    template <class F> bool until(float seconds, F cond) {
        for (int i = 0; i < (int)(seconds / DT + 0.5f); i++) { sim.step(DT); if (cond()) return true; }
        return false;
    }
    Player& player(glm::vec3 at, GameMode mode = GameMode::Survival) {
        Player& p = sim.addPlayer();
        p.setMode(mode);
        p.position = at;
        p.spawnPoint = at;
        return p;
    }
};

} // namespace testworld
