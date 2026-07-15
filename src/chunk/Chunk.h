#pragma once
#include "voxel/Voxel.h"
#include <memory>

struct Chunk {
    // Heap allocated — 512KB is too large for the stack
    Voxel voxels[VOXEL_DIM][VOXEL_DIM][VOXEL_DIM];

    // Safe accessor — returns AIR for out of bounds
    uint16_t typeAt(int x, int y, int z) const {
        if (x < 0 || x >= VOXEL_DIM ||
            y < 0 || y >= VOXEL_DIM ||
            z < 0 || z >= VOXEL_DIM)
            return BLOCK_AIR;
        return voxels[x][y][z].typeID;
    }

    void setBlock(int x, int y, int z, uint16_t id) {
        if (x < 0 || x >= VOXEL_DIM ||
            y < 0 || y >= VOXEL_DIM ||
            z < 0 || z >= VOXEL_DIM)
            return;
        voxels[x][y][z].typeID = id;
    }
};

// Always use this — never declare Chunk on the stack
using ChunkPtr = std::unique_ptr<Chunk>;

inline ChunkPtr makeChunk() {
    return std::make_unique<Chunk>();
}