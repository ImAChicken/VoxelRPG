#pragma once
#include "Chunk.h"
#include "ChunkGenerator.h"
#include <array>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

constexpr int WORLD_SIZE_X = 8;
constexpr int WORLD_SIZE_Z = 8;

class ChunkManager {
public:
    std::vector<ChunkPtr> chunks;
    std::vector<bool>     dirty;   // true = mesh needs rebuild

    ChunkManager() {
        chunks.resize(WORLD_SIZE_X * WORLD_SIZE_Z);
        dirty.resize(WORLD_SIZE_X * WORLD_SIZE_Z, false);

        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
        for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
            chunks[index(cx, cz)] = generateTestChunk();
        }
    }

    Chunk* getChunk(int cx, int cz) {
        if (cx < 0 || cx >= WORLD_SIZE_X ||
            cz < 0 || cz >= WORLD_SIZE_Z)
            return nullptr;
        return chunks[index(cx, cz)].get();
    }

    void markDirty(int cx, int cz) {
        if (cx < 0 || cx >= WORLD_SIZE_X ||
            cz < 0 || cz >= WORLD_SIZE_Z)
            return;
        dirty[index(cx, cz)] = true;
    }

    bool isDirty(int cx, int cz) const {
        if (cx < 0 || cx >= WORLD_SIZE_X ||
            cz < 0 || cz >= WORLD_SIZE_Z)
            return false;
        return dirty[index(cx, cz)];
    }

    void clearDirty(int cx, int cz) {
        if (cx < 0 || cx >= WORLD_SIZE_X ||
            cz < 0 || cz >= WORLD_SIZE_Z)
            return;
        dirty[index(cx, cz)] = false;
    }

    uint16_t typeAtWorld(int cx, int cz, int x, int y, int z) {
        int ncx = cx, ncz = cz;
        int nx = x,   nz = z;

        if (x < 0)          { ncx--; nx = x + VOXEL_DIM; }
        if (x >= VOXEL_DIM) { ncx++; nx = x - VOXEL_DIM; }
        if (z < 0)          { ncz--; nz = z + VOXEL_DIM; }
        if (z >= VOXEL_DIM) { ncz++; nz = z - VOXEL_DIM; }

        Chunk* c = getChunk(ncx, ncz);
        if (!c) return BLOCK_AIR;
        return c->typeAt(nx, y, nz);
    }

    int index(int cx, int cz) const {
        return cx * WORLD_SIZE_Z + cz;
    }

    glm::vec3 chunkOrigin(int cx, int cz) const {
        return glm::vec3(
            cx * VOXEL_DIM,
            0.0f,
            cz * VOXEL_DIM
        );
    }
};