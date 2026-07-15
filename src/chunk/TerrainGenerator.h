#pragma once
#include "Chunk.h"
#include "StructureGenerator.h"
#include "../noise/FastNoiseLite.h"

class TerrainGenerator {
public:
    TerrainGenerator(int seed = 42) {
        heightNoise.SetSeed(seed);
        heightNoise.SetNoiseType(
            FastNoiseLite::NoiseType_Perlin);
        heightNoise.SetFrequency(0.008f);
        heightNoise.SetFractalType(
            FastNoiseLite::FractalType_FBm);
        heightNoise.SetFractalOctaves(4);
        heightNoise.SetFractalLacunarity(2.0f);
        heightNoise.SetFractalGain(0.5f);

        detailNoise.SetSeed(seed + 1);
        detailNoise.SetNoiseType(
            FastNoiseLite::NoiseType_Perlin);
        detailNoise.SetFrequency(0.05f);

        treeNoise.SetSeed(seed + 2);
        treeNoise.SetNoiseType(
            FastNoiseLite::NoiseType_Perlin);
        treeNoise.SetFrequency(0.08f);

        // Generate tree structures once
        oakTree  = generateOakTree();
        pineTree = generatePineTree();
    }

    ChunkPtr generateChunk(int cx, int cz) {
        ChunkPtr chunk = makeChunk();

        // Step 1 — terrain
        for (int x = 0; x < VOXEL_DIM; x++)
        for (int z = 0; z < VOXEL_DIM; z++) {
            float wx = (float)(cx * VOXEL_DIM + x);
            float wz = (float)(cz * VOXEL_DIM + z);

            float h = heightNoise.GetNoise(wx, wz);
            float d = detailNoise.GetNoise(wx, wz) * 0.15f;

            int surfaceY = 20 + (int)((h + d) * 12.0f);
            surfaceY = std::max(4,
                std::min(surfaceY, VOXEL_DIM - 8));

            for (int y = 0; y < VOXEL_DIM; y++) {
                if (y == 0) {
                    chunk->setBlock(x, y, z, BLOCK_STONE);
                } else if (y < surfaceY - 3) {
                    chunk->setBlock(x, y, z, BLOCK_STONE);
                } else if (y < surfaceY - 1) {
                    chunk->setBlock(x, y, z, BLOCK_DIRT);
                } else if (y == surfaceY - 1) {
                    chunk->setBlock(x, y, z, BLOCK_GRASS);
                }
            }
        }

        // Step 2 — trees baked directly into chunk
        for (int lx = 4; lx < VOXEL_DIM; lx += 8)
        for (int lz = 4; lz < VOXEL_DIM; lz += 8) {
            int wx = cx * VOXEL_DIM + lx;
            int wz = cz * VOXEL_DIM + lz;

            if (shouldPlaceTree(wx, wz)) {
                int surfY = getSurfaceY(wx, wz);
                bool isPine = ((wx + wz) % 3 == 0);

                // Paste directly into this chunk
                // Trees near chunk edges may be clipped
                // — acceptable for now, fixed in M16
                const Chunk& src = isPine
                    ? *pineTree : *oakTree;

                pasteIntoChunk(*chunk, src,
                    lx, surfY, lz);
            }
        }

        return chunk;
    }

    bool shouldPlaceTree(int wx, int wz) {
        float v = treeNoise.GetNoise(
            (float)wx, (float)wz);
        return v > 0.30f;
    }

    int getSurfaceY(int wx, int wz) {
        float h = heightNoise.GetNoise(
            (float)wx, (float)wz);
        float d = detailNoise.GetNoise(
            (float)wx, (float)wz) * 0.15f;
        int surfaceY = 20 + (int)((h + d) * 12.0f);
        return std::max(4,
            std::min(surfaceY, VOXEL_DIM - 8));
    }

private:
    FastNoiseLite heightNoise;
    FastNoiseLite detailNoise;
    FastNoiseLite treeNoise;

    ChunkPtr oakTree;
    ChunkPtr pineTree;

    // Paste structure into a single chunk
    // No cross-chunk overflow — trees near edges get clipped
    void pasteIntoChunk(Chunk& dest,
                        const Chunk& src,
                        int lx, int ly, int lz) {
        const int halfW  = 4;
        const int height = 12;
        const int ox     = 32; // source origin
        const int oz     = 32;

        for (int dy = 0; dy < height; dy++)
        for (int dx = -halfW; dx <= halfW; dx++)
        for (int dz = -halfW; dz <= halfW; dz++) {
            uint16_t srcID = src.typeAt(
                ox + dx, dy, oz + dz);
            if (srcID == BLOCK_AIR) continue;

            int wx = lx + dx;
            int wy = ly + dy;
            int wz = lz + dz;

            // Clamp to chunk bounds
            if (wx < 0 || wx >= VOXEL_DIM) continue;
            if (wz < 0 || wz >= VOXEL_DIM) continue;
            if (wy < 0 || wy >= VOXEL_DIM) continue;

            dest.setBlock(wx, wy, wz, srcID);
        }
    }
};