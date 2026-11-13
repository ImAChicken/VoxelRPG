#pragma once
#include "Chunk.h"
#include "StructureGenerator.h"
#include "../noise/FastNoiseLite.h"
#include <algorithm>
#include <cmath>
#include <vector>

// ── TerrainGenerator ──────────────────────────────────────
// Builds one chunk at a time from noise. Fully deterministic:
// the same seed and chunk coordinates always give the same
// chunk, so unedited chunks never need to be saved.
//
// generateChunk() is const and touches no shared state, so
// worker threads can call it in parallel.
class TerrainGenerator {
public:
    // ── Tuning knobs ──────────────────────────────────────
    static constexpr int   BASE_HEIGHT   = 40;    // average ground level (voxels)
    static constexpr float HILL_HEIGHT   = 30.0f; // hills rise/fall this far from base
    static constexpr int   MIN_SURFACE   = 8;
    static constexpr int   MAX_SURFACE   = 72;
    static constexpr int   DIRT_DEPTH    = 6;     // voxels of dirt under the grass (2 ft)
    static constexpr int   TREE_CELL     = 32;    // one possible tree per 32×32 voxels
    static constexpr float TREE_DENSITY  = 0.05f; // lower = more forest, higher = more clearings

    TerrainGenerator(int seed = 42) : seed(seed) {
        // Broad rolling hills
        heightNoise.SetSeed(seed);
        heightNoise.SetNoiseType(
            FastNoiseLite::NoiseType_Perlin);
        heightNoise.SetFrequency(0.003f);
        heightNoise.SetFractalType(
            FastNoiseLite::FractalType_FBm);
        heightNoise.SetFractalOctaves(5);
        heightNoise.SetFractalLacunarity(2.0f);
        heightNoise.SetFractalGain(0.5f);

        // Small bumps on top of the hills
        detailNoise.SetSeed(seed + 1);
        detailNoise.SetNoiseType(
            FastNoiseLite::NoiseType_Perlin);
        detailNoise.SetFrequency(0.02f);

        // Where forests grow — large patches
        treeNoise.SetSeed(seed + 2);
        treeNoise.SetNoiseType(
            FastNoiseLite::NoiseType_Perlin);
        treeNoise.SetFrequency(0.004f);

        // Which kind of tree grows where
        typeNoise.SetSeed(seed + 3);
        typeNoise.SetNoiseType(
            FastNoiseLite::NoiseType_Perlin);
        typeNoise.SetFrequency(0.006f);

        // Build each tree once, keep only its solid voxels
        trees[TREE_OAK]     = makeTreeShape(*generateOakTree());
        trees[TREE_PINE]    = makeTreeShape(*generatePineTree());
        trees[TREE_DEAD]    = makeTreeShape(*generateDeadTree());
        trees[TREE_ANCIENT] = makeTreeShape(*generateAncientOak());

        maxTreeRadius = 0;
        for (const TreeShape& t : trees)
            maxTreeRadius = std::max(maxTreeRadius, t.radius);
    }

    // Returns the finished chunk, already RLE compressed
    ChunkPtr generateChunk(int cx, int cz) const {
        ChunkPtr chunk = makeChunk();
        uint16_t* vox  = chunk->rawData();

        const int D  = VOXEL_DIM;
        const int x0 = cx * D;     // world coords of this chunk's corner
        const int z0 = cz * D;

        // ── Step 1 — height map ───────────────────────────
        std::vector<int> height((size_t)D * D);
        int highest = 0;
        for (int x = 0; x < D; x++)
        for (int z = 0; z < D; z++) {
            int h = getSurfaceY(x0 + x, z0 + z);
            height[(size_t)x * D + z] = h;
            highest = std::max(highest, h);
        }

        // ── Step 2 — fill terrain one horizontal slice at a time
        // (the chunk array is Y-major, so this writes in memory order)
        // surfaceY is the first air voxel above the ground:
        //   surfaceY - 1            grass
        //   next DIRT_DEPTH voxels  dirt
        //   everything below        stone
        for (int y = 0; y < highest; y++) {
            uint16_t* slice = vox + (size_t)y * D * D;
            for (int x = 0; x < D; x++)
            for (int z = 0; z < D; z++) {
                int h = height[(size_t)x * D + z];
                uint16_t id;
                if      (y >= h)                  id = BLOCK_AIR;
                else if (y == h - 1)              id = BLOCK_GRASS;
                else if (y >= h - 1 - DIRT_DEPTH) id = BLOCK_DIRT;
                else                              id = BLOCK_STONE;
                slice[x * D + z] = id;
            }
        }

        // ── Step 3 — trees ────────────────────────────────
        // Tree positions come from a world-wide grid of cells,
        // not from this chunk, so a tree standing near a chunk
        // edge is stamped into both chunks and its canopy
        // carries across the seam instead of being cut off.
        int R = maxTreeRadius;
        int cellX0 = floorDiv(x0 - R,         TREE_CELL);
        int cellX1 = floorDiv(x0 + D - 1 + R, TREE_CELL);
        int cellZ0 = floorDiv(z0 - R,         TREE_CELL);
        int cellZ1 = floorDiv(z0 + D - 1 + R, TREE_CELL);

        for (int cellX = cellX0; cellX <= cellX1; cellX++)
        for (int cellZ = cellZ0; cellZ <= cellZ1; cellZ++) {
            int wx, wz, type;
            if (!treeInCell(cellX, cellZ, wx, wz, type))
                continue;
            stampTree(vox, trees[type],
                      wx - x0, getSurfaceY(wx, wz), wz - z0);
        }

        chunk->compress();
        return chunk;
    }

    // First air voxel above the ground at a world position
    int getSurfaceY(int wx, int wz) const {
        float h = heightNoise.GetNoise((float)wx, (float)wz);
        float d = detailNoise.GetNoise((float)wx, (float)wz)
                  * 0.08f;
        int surfaceY = BASE_HEIGHT +
                       (int)std::floor((h + d) * HILL_HEIGHT);
        return std::max(MIN_SURFACE,
                        std::min(surfaceY, MAX_SURFACE));
    }

private:
    enum { TREE_OAK, TREE_PINE, TREE_DEAD, TREE_ANCIENT, TREE_COUNT };

    // One solid voxel of a tree, relative to the trunk base
    struct TreeVoxel {
        int8_t   dx, dz;
        uint8_t  dy;
        uint16_t id;
    };

    struct TreeShape {
        std::vector<TreeVoxel> voxels;
        int height = 0;   // tallest voxel + 1
        int radius = 0;   // furthest reach from the trunk
    };

    int seed;
    FastNoiseLite heightNoise;
    FastNoiseLite detailNoise;
    FastNoiseLite treeNoise;
    FastNoiseLite typeNoise;

    TreeShape trees[TREE_COUNT];
    int       maxTreeRadius = 0;

    static int floorDiv(int a, int b) {
        return (a >= 0) ? a / b : -((-a + b - 1) / b);
    }

    // Deterministic 0..1 random number for a grid cell
    float cellRandom(int cellX, int cellZ, uint32_t salt) const {
        uint32_t h = (uint32_t)seed * 0x9E3779B1u;
        h ^= (uint32_t)cellX * 0x85EBCA77u;
        h  = (h << 13) | (h >> 19);
        h ^= (uint32_t)cellZ * 0xC2B2AE3Du;
        h  = (h << 15) | (h >> 17);
        h ^= salt * 0x27D4EB2Fu;
        h ^= h >> 15;  h *= 0x2C1B3C6Du;
        h ^= h >> 12;  h *= 0x297A2D39u;
        h ^= h >> 15;
        return (float)(h & 0xFFFFFF) / (float)0x1000000;
    }

    // Does this cell hold a tree? If so: where, and what kind.
    bool treeInCell(int cellX, int cellZ,
                    int& wx, int& wz, int& type) const {
        // Jittered position inside the cell, kept away from
        // the cell edges so trunks never touch
        int margin = 5;
        int span   = TREE_CELL - margin * 2;
        wx = cellX * TREE_CELL + margin +
             (int)(cellRandom(cellX, cellZ, 1) * (float)span);
        wz = cellZ * TREE_CELL + margin +
             (int)(cellRandom(cellX, cellZ, 2) * (float)span);

        // Forest patches: the noise decides how likely a cell
        // is to be filled, from dense woods to open clearings
        float forest = treeNoise.GetNoise((float)wx, (float)wz);
        float chance = (forest - TREE_DENSITY) * 2.5f + 0.35f;
        if (cellRandom(cellX, cellZ, 3) > chance) return false;

        // Tree kind follows a second noise field so the same
        // kinds grow together in groves
        float t    = typeNoise.GetNoise((float)wx, (float)wz);
        float roll = cellRandom(cellX, cellZ, 4);
        if      (t < -0.25f) type = (roll < 0.80f) ? TREE_PINE : TREE_DEAD;
        else if (t <  0.25f) type = (roll < 0.70f) ? TREE_OAK
                                  : (roll < 0.90f) ? TREE_PINE : TREE_ANCIENT;
        else                 type = (roll < 0.60f) ? TREE_OAK
                                  : (roll < 0.90f) ? TREE_ANCIENT : TREE_DEAD;

        // A tree must fit under the top of the chunk — on high
        // ground fall back to a shorter kind
        int ground = getSurfaceY(wx, wz);
        if (ground + trees[type].height > VOXEL_DIM)
            type = TREE_OAK;
        if (ground + trees[type].height > VOXEL_DIM)
            type = TREE_DEAD;
        return ground + trees[type].height <= VOXEL_DIM;
    }

    // Reduce a structure chunk to a list of its solid voxels
    static TreeShape makeTreeShape(const Chunk& src) {
        TreeShape shape;
        for (int y = 0; y < VOXEL_DIM; y++)
        for (int x = 0; x < VOXEL_DIM; x++)
        for (int z = 0; z < VOXEL_DIM; z++) {
            uint16_t id = src.typeAt(x, y, z);
            if (id == BLOCK_AIR) continue;
            int dx = x - STRUCTURE_ORIGIN_X;
            int dz = z - STRUCTURE_ORIGIN_Z;
            shape.voxels.push_back(
                { (int8_t)dx, (int8_t)dz, (uint8_t)y, id });
            shape.height = std::max(shape.height, y + 1);
            shape.radius = std::max(shape.radius,
                std::max(std::abs(dx), std::abs(dz)));
        }
        return shape;
    }

    // Write a tree into the chunk with its trunk base at local
    // (lx, ly, lz). The base may lie outside this chunk — only
    // the voxels that fall inside are written.
    static void stampTree(uint16_t* vox, const TreeShape& tree,
                          int lx, int ly, int lz) {
        const int D = VOXEL_DIM;
        for (const TreeVoxel& v : tree.voxels) {
            int x = lx + v.dx;
            int y = ly + v.dy;
            int z = lz + v.dz;
            if (x < 0 || x >= D || z < 0 || z >= D ||
                y < 0 || y >= D) continue;

            uint16_t& dst = vox[Chunk::index(x, y, z)];
            // Leaves only grow into open air; wood replaces
            // leaves so trunks stay whole where crowns overlap
            if (dst == BLOCK_AIR ||
                (dst == BLOCK_LEAVES && v.id != BLOCK_LEAVES))
                dst = v.id;
        }
    }
};
