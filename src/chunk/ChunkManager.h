#pragma once
#include "Chunk.h"
#include "ChunkGenerator.h"
#include <array>
#include <memory>

constexpr int WORLD_SIZE_X = 8;
constexpr int WORLD_SIZE_Z = 8;

class ChunkManager {
public:
    // 8x8 flat grid of chunks — Y is always 0 for now
    std::array<ChunkPtr, WORLD_SIZE_X * WORLD_SIZE_Z> chunks;

    ChunkManager() {
        // Generate all 64 chunks on startup
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

    // World-space voxel query across chunk boundaries
    // cx,cz = chunk coords, x,y,z = local voxel coords (can be out of bounds)
    uint16_t typeAtWorld(int cx, int cz, int x, int y, int z) {
        // Handle overflow into neighbour chunks
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

    // World position of a chunk's origin in voxel space
    glm::vec3 chunkOrigin(int cx, int cz) const {
        return glm::vec3(
            cx * VOXEL_DIM,
            0.0f,
            cz * VOXEL_DIM
        );
    }
};