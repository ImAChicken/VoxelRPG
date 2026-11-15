#pragma once
#include <unordered_set>
#include <glm/glm.hpp>
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
//
// This class is also the BASE for other world types
// (worldgen/IslandGenerator.h). A world type overrides:
//   column()       what one column of the world is made of
//   getSurfaceY()  ground height (spawning, structures)
//   treeInCell()   where trees grow and which kind
//   spawnPoint()   where new players appear
// and inherits chunk building and tree stamping from here.
//
// The base behaviour is the CLASSIC world: rolling hills.

// One vertical column of the world, bottom to top:
//   deep  below  (ground - 1 - subDepth)
//   sub   the next subDepth voxels
//   top   the voxel at ground - 1
//   fluid from ground up to waterLevel (water / lava), if any
//   deck  one optional layer above that (bridges)
struct TerrainColumn {
    int      ground     = 0;           // first voxel above solid ground
    uint16_t top        = BLOCK_GRASS;
    uint16_t sub        = BLOCK_DIRT;
    int      subDepth   = 6;
    uint16_t deep       = BLOCK_STONE;
    int      waterLevel = 0;           // fluid fills ground..waterLevel-1
    uint16_t fluid      = BLOCK_WATER;
    int      deckY      = -1;          // -1 = no deck
    uint16_t deck       = BLOCK_PLANKS;
};

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

    virtual ~TerrainGenerator() = default;

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

        // For naturalTreeAt(): which offsets from the trunk
        // base are wood, per tree kind
        for (int k = 0; k < TREE_COUNT; k++)
            for (const TreeVoxel& v : trees[k].voxels)
                if (v.id != BLOCK_LEAVES)
                    woodAt[k].insert(packOffset(v.dx, v.dy, v.dz));
    }

    // ── Natural trees as objects ──────────────────────────
    // A tree the generator grew. Not stored anywhere — it can
    // be found again from the seed at any time.
    struct NaturalTree {
        int cellX = 0, cellZ = 0;   // its grid cell (unique key)
        glm::ivec3 base = {0, 0, 0}; // trunk base, world voxels
        const char* typeName = "";  // structure registry name
    };

    // Is world voxel p part of the wood of a generated tree?
    // (Leaves are not checked: chopping is done on wood.)
    bool naturalTreeAt(glm::ivec3 p, NaturalTree& out) const {
        int R = maxTreeRadius;
        for (int cx = floorDiv(p.x - R, TREE_CELL); cx <= floorDiv(p.x + R, TREE_CELL); cx++)
        for (int cz = floorDiv(p.z - R, TREE_CELL); cz <= floorDiv(p.z + R, TREE_CELL); cz++) {
            int wx, wz, type;
            if (!treeInCell(cx, cz, wx, wz, type)) continue;
            int gy = getSurfaceY(wx, wz);
            int dx = p.x - wx, dy = p.y - gy, dz = p.z - wz;
            if (std::abs(dx) > 127 || std::abs(dz) > 127 || dy < 0 || dy > 255) continue;
            if (!woodAt[type].count(packOffset(dx, dy, dz))) continue;
            static const char* names[TREE_COUNT] =
                { "oak_tree", "pine_tree", "dead_tree", "ancient_oak" };
            out.cellX = cx; out.cellZ = cz;
            out.base = { wx, gy, wz };
            out.typeName = names[type];
            return true;
        }
        return false;
    }

    // Returns the finished chunk, already RLE compressed.
    // The column is built one 128³ section at a time in a
    // per-thread scratch buffer, so generating never needs
    // the whole 32 MB column uncompressed. Sections that are
    // all air, or entirely below the ground in one rock type,
    // are written as a single run without touching voxels.
    ChunkPtr generateChunk(int cx, int cz) const {
        ChunkPtr chunk = makeChunk();

        const int D  = VOXEL_DIM;
        const int x0 = cx * D;     // world coords of this chunk's corner
        const int z0 = cz * D;

        // ── Step 1 — what each column is made of ──────────
        std::vector<TerrainColumn> cols((size_t)D * D);
        int highest = 0;
        int lowestRock = WORLD_HEIGHT;     // lowest top of the 'deep' layer
        uint16_t deepId = 0;
        bool oneDeep = true;
        for (int x = 0; x < D; x++)
        for (int z = 0; z < D; z++) {
            TerrainColumn c = column(x0 + x, z0 + z);
            c.ground     = std::clamp(c.ground, 1, WORLD_HEIGHT);
            c.waterLevel = std::min(c.waterLevel, WORLD_HEIGHT);
            cols[(size_t)x * D + z] = c;
            highest = std::max({ highest, c.ground, c.waterLevel,
                                 c.deckY >= 0 ? c.deckY + 1 : 0 });
            lowestRock = std::min(lowestRock, c.ground - 1 - c.subDepth);
            if (x == 0 && z == 0) deepId = c.deep;
            else if (c.deep != deepId) oneDeep = false;
        }

        // ── Trees that reach into this column ─────────────
        // Tree positions come from a world-wide grid of cells,
        // not from this chunk, so a tree standing near a chunk
        // edge is stamped into both chunks and its canopy
        // carries across the seam instead of being cut off.
        struct Placed { const TreeShape* shape; int lx, ly, lz; };
        std::vector<Placed> placed;
        int R = maxTreeRadius;
        int cellX0 = floorDiv(x0 - R,         TREE_CELL);
        int cellX1 = floorDiv(x0 + D - 1 + R, TREE_CELL);
        int cellZ0 = floorDiv(z0 - R,         TREE_CELL);
        int cellZ1 = floorDiv(z0 + D - 1 + R, TREE_CELL);
        int treeTop = 0;
        for (int cellX = cellX0; cellX <= cellX1; cellX++)
        for (int cellZ = cellZ0; cellZ <= cellZ1; cellZ++) {
            int wx, wz, type;
            if (!treeInCell(cellX, cellZ, wx, wz, type)) continue;
            int gy = getSurfaceY(wx, wz);
            placed.push_back({ &trees[type], wx - x0, gy, wz - z0 });
            treeTop = std::max(treeTop, gy + trees[type].height);
        }
        int contentTop = std::max(highest, treeTop);

        // ── Step 2 — each section ─────────────────────────
        static thread_local std::vector<uint16_t> scratch;
        for (int sy = 0; sy < SECTIONS; sy++) {
            int y0 = sy * D, y1 = y0 + D;
            ChunkSection& sec = chunk->section(sy);
            if (y0 >= contentTop) {                       // open sky
                sec.setRle(ChunkSection::allAirShared());
                continue;
            }
            if (oneDeep && y1 <= lowestRock) {            // solid rock
                auto r = std::make_shared<RleData>();
                r->ends.push_back((uint32_t)VOXEL_COUNT);
                r->types.push_back(deepId);
                sec.setRle(r);
                continue;
            }

            if (scratch.size() != (size_t)VOXEL_COUNT) scratch.resize((size_t)VOXEL_COUNT);
            uint16_t* vox = scratch.data();
            // fill terrain one horizontal slice at a time
            // (Y-major, so this writes in memory order)
            for (int y = y0; y < y1; y++) {
                uint16_t* slice = vox + (size_t)(y - y0) * D * D;
                if (y >= highest) {
                    std::memset(slice, 0, (size_t)D * D * sizeof(uint16_t));
                    continue;
                }
                for (int x = 0; x < D; x++)
                for (int z = 0; z < D; z++) {
                    const TerrainColumn& c = cols[(size_t)x * D + z];
                    int h = c.ground;
                    uint16_t id;
                    if (y >= h) {
                        if (y < c.waterLevel)  id = c.fluid;
                        else if (y == c.deckY) id = c.deck;
                        else                   id = BLOCK_AIR;
                    }
                    else if (y == h - 1)              id = c.top;
                    else if (y >= h - 1 - c.subDepth) id = c.sub;
                    else                              id = c.deep;
                    slice[x * D + z] = id;
                }
            }
            for (const Placed& p : placed)
                stampTree(vox, *p.shape, p.lx, p.ly - y0, p.lz);
            sec.setRle(RleData::build(vox));
        }
        return chunk;
    }

    // What a column of the world is made of (classic hills)
    virtual TerrainColumn column(int wx, int wz) const {
        TerrainColumn c;
        c.ground   = getSurfaceY(wx, wz);
        c.subDepth = DIRT_DEPTH;
        return c;
    }

    // Where new players appear (world voxels, feet)
    virtual glm::ivec3 spawnPoint(int worldSizeX, int worldSizeZ) const {
        int x = worldSizeX * VOXEL_DIM / 2, z = worldSizeZ * VOXEL_DIM / 2;
        return { x, getSurfaceY(x, z), z };
    }

    // ── For the distant terrain (render/FarTerrain.h) ─────
    // Water surface around the world, or -1 for none: the
    // far view draws an ocean out to the horizon at this height
    virtual int seaLevel() const { return -1; }

    // 0..1: how much of the ground near here is under trees.
    // The far view tints and raises forests by this.
    virtual float forestCover(int wx, int wz) const {
        float forest = treeNoise.GetNoise((float)wx, (float)wz);
        return std::clamp((forest - TREE_DENSITY) * 2.5f + 0.35f, 0.0f, 1.0f);
    }

    // First air voxel above the ground at a world position
    virtual int getSurfaceY(int wx, int wz) const {
        float h = heightNoise.GetNoise((float)wx, (float)wz);
        float d = detailNoise.GetNoise((float)wx, (float)wz)
                  * 0.08f;
        int surfaceY = BASE_HEIGHT +
                       (int)std::floor((h + d) * HILL_HEIGHT);
        return std::max(MIN_SURFACE,
                        std::min(surfaceY, MAX_SURFACE));
    }

protected:
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
    std::unordered_set<uint32_t> woodAt[TREE_COUNT];

    static uint32_t packOffset(int dx, int dy, int dz) {
        return ((uint32_t)(dx + 128) << 16) | ((uint32_t)(dz + 128) << 8) | (uint32_t)dy;
    }
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
    virtual bool treeInCell(int cellX, int cellZ,
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

        // A tree must fit under the top of the world — on high
        // ground fall back to a shorter kind
        int ground = getSurfaceY(wx, wz);
        if (ground + trees[type].height > WORLD_HEIGHT)
            type = TREE_OAK;
        if (ground + trees[type].height > WORLD_HEIGHT)
            type = TREE_DEAD;
        return ground + trees[type].height <= WORLD_HEIGHT;
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
