#pragma once
#include "Chunk.h"

inline ChunkPtr generateTestChunk() {
    ChunkPtr chunk = makeChunk();

    for (int x = 0; x < VOXEL_DIM; x++)
    for (int z = 0; z < VOXEL_DIM; z++) {
        for (int y = 0; y < 29; y++)
            chunk->setBlock(x, y, z, BLOCK_STONE);

        chunk->setBlock(x, 29, z, BLOCK_DIRT);
        chunk->setBlock(x, 30, z, BLOCK_DIRT);
        chunk->setBlock(x, 31, z, BLOCK_GRASS);
    }

    // Log pillar
    chunk->setBlock(10, 32, 50, BLOCK_LOG_Y);
    chunk->setBlock(10, 33, 50, BLOCK_LOG_Y);
    chunk->setBlock(10, 34, 50, BLOCK_LOG_Y);
    chunk->setBlock(11, 32, 50, BLOCK_PLANKS);
    chunk->setBlock(12, 32, 50, BLOCK_PLANKS);

    // Another cluster
    chunk->setBlock(30, 32, 20, BLOCK_LOG_Y);
    chunk->setBlock(30, 33, 20, BLOCK_LOG_Y);
    chunk->setBlock(31, 32, 20, BLOCK_PLANKS);

    // Lone plank
    chunk->setBlock(50, 32, 40, BLOCK_PLANKS);

    return chunk;
}