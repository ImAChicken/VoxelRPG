#pragma once
#include "Chunk.h"
#include "../voxel/Voxel.h"

// Generates a simple oak tree structure chunk
// Trunk centered at x=32, z=32 (center of chunk)
// Y=0 is the base of the trunk — paste origin point
inline ChunkPtr generateOakTree() {
    ChunkPtr chunk = makeChunk();

    const int cx = 32;  // center x
    const int cz = 32;  // center z

    // Trunk — 5 blocks tall starting at y=0
    for (int y = 0; y < 5; y++)
        chunk->setBlock(cx, y, cz, BLOCK_LOG_Y);

    // Leaf canopy — rough sphere around y=4 to y=7
    // Radius varies per layer
    struct Layer { int y; int radius; };
    Layer layers[] = {
        { 4, 3 },   // bottom of canopy — wide
        { 5, 3 },   // middle
        { 6, 2 },   // upper middle
        { 7, 2 },   // top
        { 8, 1 },   // tip
    };

    for (auto& layer : layers) {
        for (int dx = -layer.radius; dx <= layer.radius; dx++)
        for (int dz = -layer.radius; dz <= layer.radius; dz++) {
            // Round the corners slightly
            if (abs(dx) == layer.radius &&
                abs(dz) == layer.radius)
                continue;

            int x = cx + dx;
            int z = cz + dz;

            if (x < 0 || x >= VOXEL_DIM ||
                z < 0 || z >= VOXEL_DIM)
                continue;

            // Don't overwrite the trunk
            if (chunk->typeAt(x, layer.y, z) == BLOCK_AIR)
                chunk->setBlock(x, layer.y, z, BLOCK_LEAVES);
        }
    }

    return chunk;
}

// Generates a pine tree — taller, narrower
inline ChunkPtr generatePineTree() {
    ChunkPtr chunk = makeChunk();

    const int cx = 32;
    const int cz = 32;

    // Taller trunk — 7 blocks
    for (int y = 0; y < 7; y++)
        chunk->setBlock(cx, y, cz, BLOCK_LOG_Y);

    // Narrow layered canopy — pyramid shape
    struct Layer { int y; int radius; };
    Layer layers[] = {
        { 4, 3 },
        { 5, 2 },
        { 6, 2 },
        { 7, 1 },
        { 8, 1 },
        { 9, 1 },
        { 10, 0 }, // tip — single leaf block
    };

    for (auto& layer : layers) {
        for (int dx = -layer.radius; dx <= layer.radius; dx++)
        for (int dz = -layer.radius; dz <= layer.radius; dz++) {
            int x = cx + dx;
            int z = cz + dz;

            if (x < 0 || x >= VOXEL_DIM ||
                z < 0 || z >= VOXEL_DIM)
                continue;

            if (chunk->typeAt(x, layer.y, z) == BLOCK_AIR)
                chunk->setBlock(x, layer.y, z, BLOCK_LEAVES);
        }
    }

    return chunk;
}