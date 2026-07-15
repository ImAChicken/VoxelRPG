#pragma once
#include "Chunk.h"
#include "../noise/FastNoiseLite.h"

class TerrainGenerator {
public:
    TerrainGenerator(int seed = 42) {
        // Base heightmap — gentle rolling hills
        heightNoise.SetSeed(seed);
        heightNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        heightNoise.SetFrequency(0.008f);
        heightNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
        heightNoise.SetFractalOctaves(4);
        heightNoise.SetFractalLacunarity(2.0f);
        heightNoise.SetFractalGain(0.5f);

        // Detail noise — small surface variation
        detailNoise.SetSeed(seed + 1);
        detailNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        detailNoise.SetFrequency(0.05f);

        // Tree placement noise
        treeNoise.SetSeed(seed + 2);
        treeNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        treeNoise.SetFrequency(0.08f);
    }

    // Generate a chunk at world chunk coordinates cx, cz
    ChunkPtr generateChunk(int cx, int cz) {
        ChunkPtr chunk = makeChunk();

        for (int x = 0; x < VOXEL_DIM; x++)
        for (int z = 0; z < VOXEL_DIM; z++) {
            // World voxel position
            float wx = (float)(cx * VOXEL_DIM + x);
            float wz = (float)(cz * VOXEL_DIM + z);

            // Sample height — base + detail
            float h = heightNoise.GetNoise(wx, wz);
            float d = detailNoise.GetNoise(wx, wz) * 0.15f;

            // Map noise (-1 to 1) to voxel height
            // Base height 20, variation +/- 12
            int surfaceY = 20 + (int)((h + d) * 12.0f);
            surfaceY = std::max(4, std::min(surfaceY, VOXEL_DIM - 8));

            // Fill layers
            for (int y = 0; y < VOXEL_DIM; y++) {
                if (y == 0) {
                    // Bedrock at bottom
                    chunk->setBlock(x, y, z, BLOCK_STONE);
                }
                else if (y < surfaceY - 3) {
                    // Deep stone
                    chunk->setBlock(x, y, z, BLOCK_STONE);
                }
                else if (y < surfaceY - 1) {
                    // Dirt layer
                    chunk->setBlock(x, y, z, BLOCK_DIRT);
                }
                else if (y == surfaceY - 1) {
                    // Surface — grass
                    chunk->setBlock(x, y, z, BLOCK_GRASS);
                }
                // Above surface stays air
            }
        }

        return chunk;
    }

    // Returns true if a tree should be placed at this world position
    bool shouldPlaceTree(int wx, int wz) {
        float v = treeNoise.GetNoise((float)wx, (float)wz);
        // Perlin noise returns -1 to 1
        // Threshold 0.3 gives roughly 1 tree per 8x8 sample area
        return v > 0.30f;
    }

    // Get surface Y at a world voxel position
    int getSurfaceY(int wx, int wz) {
        float h = heightNoise.GetNoise((float)wx, (float)wz);
        float d = detailNoise.GetNoise((float)wx, (float)wz) * 0.15f;
        int surfaceY = 20 + (int)((h + d) * 12.0f);
        return std::max(4, std::min(surfaceY, VOXEL_DIM - 8));
    }

private:
    FastNoiseLite heightNoise;
    FastNoiseLite detailNoise;
    FastNoiseLite treeNoise;
};