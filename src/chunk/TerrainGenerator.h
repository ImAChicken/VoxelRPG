#pragma once
#include <unordered_set>
#include <glm/glm.hpp>
#include "Chunk.h"
#include "StructureGenerator.h"
#include "../noise/FastNoiseLite.h"
#include "../world/Decorations.h"
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <string>
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
    uint8_t  eco        = decor::ECO_NONE;   // biome for decorations (island::Biome)
    // Layered rock: the sub and deep layers take a block by height
    // instead (canyon walls show bands). strataOff tilts the bands.
    uint8_t  strata     = 0;                  // 0 none, 1 sandstone, 2 limestone
    int8_t   strataOff  = 0;
};

// Sandstone bands by height: a repeating 37-voxel sequence of beds
// of different thickness (red, orange, cream, a thin dark seam)
// Limestone (kind 2): thick pale beds with thinner grey ones
inline uint16_t strataBlock(int y, int kind = 1) {
    static uint16_t ids[6] = { 0, 0, 0, 0, 0, 0 };
    if (!ids[0]) {
        const char* n[6] = { "sandstone_red", "sandstone_orange", "sandstone_cream", "sandstone_dark", "limestone", "limestone_dark" };
        for (int k = 0; k < 6; k++) { ids[k] = gRegistry.getIDByName(n[k]); if (!ids[k]) ids[k] = BLOCK_STONE; }
    }
    if (kind == 2) {
        static const uint8_t lime[] = { 4,4,4,4,4,4,4,5,5,4,4,4,4,4,5,4,4,4,4,4,4,4,4,5,5,5,4,4,4 };
        int k = ((y % 29) + 29) % 29;
        return ids[lime[k]];
    }
    static const uint8_t beds[] = { 0,0,0,0,0,0,1,1,1,1,0,0,0,2,2,2,2,2,1,1,1,1,1,1,3,0,0,0,0,2,2,2,1,1,0,0,3 };
    int k = ((y % 37) + 37) % 37;
    return ids[beds[k]];
}

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
        trees.resize(TREE_COUNT);
        trees[TREE_OAK]     = makeTreeShape(*generateOakTree());
        trees[TREE_PINE]    = makeTreeShape(*generatePineTree());
        trees[TREE_DEAD]    = makeTreeShape(*generateDeadTree());
        trees[TREE_ANCIENT] = makeTreeShape(*generateAncientOak());
        treeNames = { "oak_tree", "pine_tree", "dead_tree", "ancient_oak" };
        // ... then the trees from decorations.toml ([[tree]]): each
        // variant of each is a kind of its own, named <name>_<variant>
        // (the structure registry makes a matching type for felling)
        if (decorSet) {
            firstTreeOfDef.assign(decorSet->defs.size(), -1);
            for (size_t i = 0; i < decorSet->defs.size(); i++) {
                const decor::Def& d = decorSet->defs[i];
                if (d.layer != decor::Layer::Tree || d.variants.empty()) continue;
                firstTreeOfDef[i] = (int)trees.size();
                for (size_t v = 0; v < d.variants.size(); v++) {
                    TreeShape t;
                    for (const decor::Vox& q : d.variants[v].v) {
                        t.voxels.push_back({ q.dx, q.dz, q.dy, q.id, (uint8_t)(isWood(q.id)) });
                        t.height = std::max(t.height, q.dy + 1);
                        t.radius = std::max({ t.radius, std::abs((int)q.dx), std::abs((int)q.dz) });
                    }
                    trees.push_back(std::move(t));
                    treeNames.push_back(d.name + "_" + std::to_string(v));
                }
            }
            // trees that take over from a built-in one (replaces = "oak_tree")
            replacedBy.assign(TREE_COUNT, -1);
            for (size_t i = 0; i < decorSet->defs.size(); i++) {
                const decor::Def& d = decorSet->defs[i];
                if (d.layer != decor::Layer::Tree || d.replaces.empty() || firstTreeOfDef[i] < 0) continue;
                for (int k = 0; k < TREE_COUNT; k++) if (treeNames[(size_t)k] == d.replaces) replacedBy[(size_t)k] = (int)i;
            }
        }

        maxTreeRadius = 0;
        for (const TreeShape& t : trees)
            maxTreeRadius = std::max(maxTreeRadius, t.radius);

        // For naturalTreeAt(): which offsets from the trunk
        // base are wood, per tree kind
        woodAt.resize(trees.size());
        for (size_t k = 0; k < trees.size(); k++)
            for (const TreeVoxel& v : trees[k].voxels)
                if (isWood(v.id))
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
            if (std::abs(dx) > 127 || std::abs(dz) > 127 || dy < -32 || dy > 223) continue;
            if (!woodAt[(size_t)type].count(packOffset(dx, dy, dz))) continue;
            out.cellX = cx; out.cellZ = cz;
            out.base = { wx, gy, wz };
            out.typeName = treeNames[(size_t)type].c_str();
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
        // ── Rock formations (world types that have them) ──
        // Same idea as trees: one possible rock per ROCK_CELL
        // square of the world, stamped into every chunk it reaches
        if (!rockShapes.empty()) {
            int RR = maxRockRadius;
            int rx0 = floorDiv(x0 - RR, ROCK_CELL), rx1 = floorDiv(x0 + D - 1 + RR, ROCK_CELL);
            int rz0 = floorDiv(z0 - RR, ROCK_CELL), rz1 = floorDiv(z0 + D - 1 + RR, ROCK_CELL);
            for (int cellX = rx0; cellX <= rx1; cellX++)
            for (int cellZ = rz0; cellZ <= rz1; cellZ++) {
                RockPlace rp;
                if (!rockInCell(cellX, cellZ, rp)) continue;
                const TreeShape& sh = rockShapes[(size_t)rp.shape];
                placed.push_back({ &sh, rp.wx - x0, rp.wy, rp.wz - z0 });
                treeTop = std::max(treeTop, rp.wy + sh.height);
            }
        }
        // ── Decorations (world/Decorations.h) ─────────────
        // Landmarks and bushes / logs / stones: stamped like trees,
        // from world-wide grids, into every chunk they reach
        struct DPlaced { const decor::Shape* shape; int lx, ly, lz; };
        std::vector<DPlaced> dplaced;
        std::vector<LandmarkPlace> landmarks;
        if (decorSet) {
            int LR = decorSet->maxLandmarkRadius + 40;      // + meadows
            landmarksNear(x0 - LR, z0 - LR, x0 + D - 1 + LR, z0 + D - 1 + LR, landmarks);
            for (const LandmarkPlace& lm : landmarks) {
                int r = lm.shape->radius;
                if (lm.wx + r < x0 || lm.wx - r >= x0 + D || lm.wz + r < z0 || lm.wz - r >= z0 + D) continue;
                dplaced.push_back({ lm.shape, lm.wx - x0, lm.wy, lm.wz - z0 });
                treeTop = std::max(treeTop, lm.wy + lm.shape->top);
            }
            int RR = decorSet->maxLargeRadius;
            int dx0 = floorDiv(x0 - RR, DECOR_CELL), dx1 = floorDiv(x0 + D - 1 + RR, DECOR_CELL);
            int dz0 = floorDiv(z0 - RR, DECOR_CELL), dz1 = floorDiv(z0 + D - 1 + RR, DECOR_CELL);
            for (int cellX = dx0; cellX <= dx1; cellX++)
            for (int cellZ = dz0; cellZ <= dz1; cellZ++) {
                DecorPlace dp;
                if (!decorInCell(cellX, cellZ, dp) || !dp.shape) continue;
                dplaced.push_back({ dp.shape, dp.wx - x0, dp.wy, dp.wz - z0 });
                treeTop = std::max(treeTop, dp.wy + dp.shape->top);
            }
            groundPatches(cols, x0, z0);
        }
        std::vector<PlantPlace> plants;
        if (decorSet) placePlants(cols, x0, z0, landmarks, plants, treeTop);
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
            if (oneDeep && y1 <= lowestRock && !carvesInto(cx, cz, sy)) {   // solid rock
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
                    else if (c.strata)                id = strataBlock(y + c.strataOff, c.strata);
                    else if (y >= h - 1 - c.subDepth) id = c.sub;
                    else                              id = c.deep;
                    slice[x * D + z] = id;
                }
            }
            carveSection(vox, x0, z0, y0, cols);
            for (const Placed& p : placed)
                stampTree(vox, *p.shape, p.lx, p.ly - y0, p.lz);
            for (const DPlaced& p : dplaced)
                decor::stamp(vox, *p.shape, p.lx, p.ly - y0, p.lz, false);
            stampPlants(vox, plants, y0);
            sec.setRle(RleData::build(vox));
        }
        return chunk;
    }

    // What a column of the world is made of (classic hills)
    virtual TerrainColumn column(int wx, int wz) const {
        TerrainColumn c;
        c.ground   = getSurfaceY(wx, wz);
        c.subDepth = DIRT_DEPTH;
        // decorations: forest under the trees, plains elsewhere
        c.eco = forestCover(wx, wz) > 0.5f ? 12 /* Forest */ : 9 /* Plains */;
        return c;
    }

    // Where new players appear (world voxels, feet)
    virtual glm::ivec3 spawnPoint(int worldSizeX, int worldSizeZ) const {
        int x = worldSizeX * VOXEL_DIM / 2, z = worldSizeZ * VOXEL_DIM / 2;
        return { x, getSurfaceY(x, z), z };
    }

    // Where villages stand (world x, z of the square; GameSession
    // builds one from blueprints at each, game/Village.h, when a player
    // first comes near). small: a hamlet. None by default
    struct VillageSite { glm::ivec2 at{ 0 }; std::string name; bool small = false; };
    virtual std::vector<VillageSite> villageSites() const { return {}; }

    // ── For the distant terrain (render/FarTerrain.h) ─────
    // Water surface around the world, or -1 for none: the
    // far view draws an ocean out to the horizon at this height
    virtual int seaLevel() const { return -1; }

    // Which way the water runs here, and how fast (x, z; length
    // 0..1): rivers downstream, still water (lakes, the sea) zero.
    // The chunk shader moves the water's surface along it
    // (world/LightBake.h flow). Called from worker threads.
    virtual glm::vec2 flowAt(int wx, int wz) const { (void)wx; (void)wz; return glm::vec2(0.0f); }

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
        int16_t  dy;       // a little below the base too (roots, a flared foot)
        uint16_t id;
        uint8_t  wood = 0; // trunks and roots also push water aside (trees standing in a swamp)
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

    std::vector<TreeShape>   trees;        // TREE_COUNT built in, then decorations.toml trees
    std::vector<std::string> treeNames;    // structure registry names
    std::vector<int>         firstTreeOfDef;   // decor def → its first tree kind (-1: not a tree)
    std::vector<int>         replacedBy;       // built-in kind → decor def that grows instead (-1: none)

    // A built-in tree kind swapped for the decorations.toml tree that
    // replaces it, if there is one (a variant picked per cell)
    int replaceTree(int type, int cellX, int cellZ) const {
        if (type < 0 || type >= (int)replacedBy.size() || replacedBy[(size_t)type] < 0) return type;
        int di = replacedBy[(size_t)type];
        int nv = (int)decorSet->defs[(size_t)di].variants.size();
        return firstTreeOfDef[(size_t)di] + (int)(cellRandom(cellX, cellZ, 7) * nv) % nv;
    }

    // Rock formations: shapes made by the world type, placed by
    // rockInCell() on a grid of ROCK_CELL voxels. Stamped like
    // trees, filling only air, so a rock's base can sit sunk in
    // the ground and a ledge can jut out over a drop.
    static constexpr int ROCK_CELL = 20;
    std::vector<TreeShape> rockShapes;
    int maxRockRadius = 0;
    struct RockPlace { int shape = 0; int wx = 0, wy = 0, wz = 0; };
    // A world type may hollow out the filled terrain of a section
    // (overhangs, alcoves) before trees and rocks go in.
    //   vox  the section, Y-major (Chunk::index)   y0  its bottom
    // A world type that carves deep down (the Dungeon type) says which
    // sections it reaches, so those aren't written as solid rock unseen
    virtual bool carvesInto(int cx, int cz, int sy) const { (void)cx; (void)cz; (void)sy; return false; }
    virtual void carveSection(uint16_t* vox, int x0, int z0, int y0,
                              const std::vector<TerrainColumn>& cols) const {
        (void)vox; (void)x0; (void)z0; (void)y0; (void)cols;
    }

    // ── Decorations (world/Decorations.h) ─────────────────
    // The registry this world was opened with, and how dense each
    // layer is (World Settings)
    std::shared_ptr<const decor::Set> decorSet = decor::current();
    float plantScale = 1.0f, largeScale = 1.0f, landmarkScale = 1.0f;

    // Bushes, logs and stones: one possible piece per DECOR_CELL
    // square, chosen by the world type
    static constexpr int DECOR_CELL = decor::LARGE_CELL;
    struct DecorPlace { const decor::Shape* shape = nullptr; int wx = 0, wy = 0, wz = 0; };
    virtual bool decorInCell(int cellX, int cellZ, DecorPlace& out) const {
        (void)cellX; (void)cellZ; (void)out;
        return false;
    }

    // ── Landmarks ─────────────────────────────────────────
    struct LandmarkPlace {
        int def = -1;
        const decor::Shape* shape = nullptr;
        int wx = 0, wy = 0, wz = 0;
        float meadow = 0.0f;
    };
    // Can a landmark stand here? (world types rule out roads, villages …)
    virtual bool landmarkSpotOk(int wx, int wz) const { (void)wx; (void)wz; return true; }
    // Where the spawn landmark grows, if this world type has a spawn
    virtual bool spawnLandmarkSpot(int& wx, int& wz) const { (void)wx; (void)wz; return false; }

    // Does this landmark cell hold a landmark? Cached: a world has
    // few landmark cells and each is asked about many times.
    bool landmarkInCell(int cellX, int cellZ, LandmarkPlace& out) const {
        int64_t key = ((int64_t)cellX << 32) ^ (uint32_t)cellZ;
        {
            std::lock_guard<std::mutex> lock(lmMutex);
            auto it = lmCache.find(key);
            if (it != lmCache.end()) { out = it->second; return out.shape != nullptr; }
        }
        LandmarkPlace lp = findLandmark(cellX, cellZ);
        std::lock_guard<std::mutex> lock(lmMutex);
        lmCache[key] = lp;
        out = lp;
        return lp.shape != nullptr;
    }

    // Every landmark whose cell overlaps the box (world voxels), plus
    // the spawn landmark if it is near
    void landmarksNear(int xa, int za, int xb, int zb, std::vector<LandmarkPlace>& out) const {
        if (!decorSet) return;
        const int LC = decor::LANDMARK_CELL;
        for (int cx = floorDiv(xa, LC) - 1; cx <= floorDiv(xb, LC) + 1; cx++)
        for (int cz = floorDiv(za, LC) - 1; cz <= floorDiv(zb, LC) + 1; cz++) {
            LandmarkPlace lp;
            if (!landmarkInCell(cx, cz, lp)) continue;
            int reach = std::max(lp.shape->radius, (int)lp.meadow);
            if (lp.wx + reach < xa || lp.wx - reach > xb || lp.wz + reach < za || lp.wz - reach > zb) continue;
            out.push_back(lp);
        }
        LandmarkPlace sp = spawnLandmark();
        if (sp.shape) {
            int reach = std::max(sp.shape->radius, (int)sp.meadow);
            if (!(sp.wx + reach < xa || sp.wx - reach > xb || sp.wz + reach < za || sp.wz - reach > zb)) out.push_back(sp);
        }
    }

    // Is (wx, wz) within a landmark's reach (its shape plus pad, or
    // its meadow)? Trees and bushes keep clear of landmarks.
    bool nearLandmark(int wx, int wz, int pad) const {
        std::vector<LandmarkPlace> v;
        int R = (decorSet ? decorSet->maxLandmarkRadius : 0) + 40;
        landmarksNear(wx - R, wz - R, wx + R, wz + R, v);
        for (const LandmarkPlace& lm : v) {
            float r = std::max((float)lm.shape->radius + pad, lm.meadow);
            float dx = (float)(wx - lm.wx), dz = (float)(wz - lm.wz);
            if (dx * dx + dz * dz < r * r) return true;
        }
        return false;
    }

    LandmarkPlace spawnLandmark() const {
        std::call_once(spawnOnce, [this] {
            if (!decorSet || decorSet->spawnLandmark < 0) return;
            int wx, wz;
            if (!spawnLandmarkSpot(wx, wz)) return;
            const decor::Def& d = decorSet->defs[(size_t)decorSet->spawnLandmark];
            if (d.variants.empty()) return;
            spawnLm.def = decorSet->spawnLandmark;
            spawnLm.shape = &d.variants[0];
            spawnLm.wx = wx; spawnLm.wz = wz;
            spawnLm.wy = column(wx, wz).ground;
            spawnLm.meadow = d.meadow;
        });
        return spawnLm;
    }

    // ── Plants ────────────────────────────────────────────
    struct PlantPlace {
        const decor::Shape* shape; int lx, ly, lz;
        decor::Water water;
        int8_t ok;              // -1 not decided yet, 0 no room, 1 grows
    };

    // Ground patches: another top block where the patch noise is high
    void groundPatches(std::vector<TerrainColumn>& cols, int x0, int z0) const {
        const int D = VOXEL_DIM;
        for (int x = 0; x < D; x++)
        for (int z = 0; z < D; z++) {
            TerrainColumn& c = cols[(size_t)x * D + z];
            if (c.eco >= decor::BIOMES || c.waterLevel > c.ground) continue;
            for (const auto& [di, dens] : decorSet->list(decor::Layer::Ground, c.eco)) {
                const decor::Def& d = decorSet->defs[(size_t)di];
                if (d.blocks.empty() || std::find(d.on.begin(), d.on.end(), c.top) == d.on.end()) continue;
                float n = decor::valueNoise(seed, (float)(x0 + x), (float)(z0 + z), d.patchSize, (uint32_t)di * 31u + 5u);
                float n2 = decor::valueNoise(seed, (float)(x0 + x), (float)(z0 + z), d.patchSize * 0.35f, (uint32_t)di * 31u + 6u);
                if (n * 0.75f + n2 * 0.25f > 1.0f - dens * 0.6f) { c.top = d.blocks[0]; break; }
            }
        }
    }

    // One possible plant per PLANT_CELL square, kept inside it
    void placePlants(const std::vector<TerrainColumn>& cols, int x0, int z0,
                     const std::vector<LandmarkPlace>& landmarks,
                     std::vector<PlantPlace>& out, int& top) const {
        if (plantScale <= 0.0f) return;
        const int D = VOXEL_DIM, PC = decor::PLANT_CELL, N = D / PC;
        auto colAt = [&](int x, int z) -> const TerrainColumn& {
            return cols[(size_t)std::clamp(x, 0, D - 1) * D + std::clamp(z, 0, D - 1)];
        };
        for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            int gx = floorDiv(x0, PC) + i, gz = floorDiv(z0, PC) + j;
            int lx = i * PC + 1 + (cellRandom(gx, gz, 102) < 0.5f ? 0 : 1);
            int lz = j * PC + 1 + (cellRandom(gx, gz, 103) < 0.5f ? 0 : 1);
            const TerrainColumn& c = colAt(lx, lz);
            if (c.eco >= decor::BIOMES) continue;
            int wx = x0 + lx, wz = z0 + lz;
            float roll = cellRandom(gx, gz, 101);
            int pick = -1;
            // inside a landmark's meadow: flowers, thinning toward its edge
            for (const LandmarkPlace& lm : landmarks) {
                if (lm.meadow <= 0.0f) continue;
                const decor::Def& ld = decorSet->defs[(size_t)lm.def];
                if (ld.meadowPlants.empty()) continue;
                float dx = (float)(wx - lm.wx), dz = (float)(wz - lm.wz);
                float t = std::sqrt(dx * dx + dz * dz) / lm.meadow;
                if (t >= 1.0f) continue;
                float dens = 0.55f * (1.0f - t * t);
                if (roll < dens) {
                    float n = decor::valueNoise(seed, (float)wx, (float)wz, 6.0f, 900u + (uint32_t)lm.def);
                    pick = ld.meadowPlants[(size_t)((int)(n * 997.0f) % (int)ld.meadowPlants.size())];
                } else roll = (roll - dens) / (1.0f - dens);
                break;
            }
            if (pick < 0) {
                float acc = 0.0f;
                for (const auto& [di, dens] : decorSet->list(decor::Layer::Plant, c.eco)) {
                    const decor::Def& d = decorSet->defs[(size_t)di];
                    float p = dens * plantScale;
                    if (d.patch > 0.0f) {
                        float n = decor::valueNoise(seed, (float)wx, (float)wz, d.patchSize, (uint32_t)di * 7919u + 13u);
                        p *= std::max(0.0f, 1.0f - d.patch + d.patch * (n - 0.3f) * 3.0f);
                    }
                    acc += p;
                    if (roll < acc) { pick = di; break; }
                }
            }
            if (pick < 0) continue;
            const decor::Def& d = decorSet->defs[(size_t)pick];
            if (d.variants.empty()) continue;
            int depth = c.waterLevel - c.ground, base = c.ground;
            switch (d.water) {
            case decor::Water::Dry:
            case decor::Water::Shore:
                if (depth > 0 || c.deckY >= 0) continue;
                if (d.water == decor::Water::Shore) {
                    bool wet = false;
                    for (int q = -4; q <= 4 && !wet; q += 2)
                        for (int r = -4; r <= 4 && !wet; r += 2) {
                            const TerrainColumn& o = colAt(lx + q, lz + r);
                            wet = o.waterLevel > o.ground;
                        }
                    if (!wet) continue;
                }
                break;
            case decor::Water::Shallow:
                if (depth < 1 || depth > 3 || c.fluid != BLOCK_WATER) continue;
                break;
            case decor::Water::Surface:
                if (depth < 2 || c.fluid != BLOCK_WATER) continue;
                base = c.waterLevel;
                break;
            }
            if (d.water != decor::Water::Surface &&
                std::find(d.on.begin(), d.on.end(), c.top) == d.on.end()) continue;
            if (d.water == decor::Water::Dry || d.water == decor::Water::Shore) {
                int g = c.ground, rise = 0;
                rise = std::max({ std::abs(colAt(lx + 1, lz).ground - g), std::abs(colAt(lx - 1, lz).ground - g),
                                  std::abs(colAt(lx, lz + 1).ground - g), std::abs(colAt(lx, lz - 1).ground - g) });
                if ((float)rise > d.maxSlope + 0.01f) continue;
            }
            int var = (int)(cellRandom(gx, gz, 104) * (float)d.variants.size()) % (int)d.variants.size();
            const decor::Shape& sh = d.variants[(size_t)var];
            out.push_back({ &sh, lx, base, lz, d.water, -1 });
            top = std::max(top, base + sh.top);
        }
    }

    // Write the plants into a section. Whether a plant has room is
    // decided in the section holding its base (sections go bottom
    // up), so a plant never comes out half grown across a seam.
    static void stampPlants(uint16_t* vox, std::vector<PlantPlace>& plants, int y0) {
        const int D = VOXEL_DIM;
        for (PlantPlace& p : plants) {
            int ly = p.ly - y0;
            if (ly + p.shape->top <= 0 || ly + p.shape->bottom >= D) continue;
            if (p.ok < 0) {
                if (ly < 0 || ly >= D) continue;          // base below this section: decided there
                uint16_t at = vox[Chunk::index(p.lx, ly, p.lz)];
                uint16_t below = ly > 0 ? vox[Chunk::index(p.lx, ly - 1, p.lz)] : (uint16_t)BLOCK_STONE;
                bool room;
                switch (p.water) {
                case decor::Water::Shallow: room = at == BLOCK_WATER; break;
                case decor::Water::Surface: room = at == BLOCK_AIR && below == BLOCK_WATER; break;
                default: room = at == BLOCK_AIR && below != BLOCK_AIR && below != BLOCK_WATER && below != BLOCK_LAVA; break;
                }
                p.ok = room ? 1 : 0;
            }
            if (p.ok == 1) decor::stamp(vox, *p.shape, p.lx, ly, p.lz, p.water == decor::Water::Shallow);
        }
    }

    mutable std::mutex lmMutex;
    mutable std::unordered_map<int64_t, LandmarkPlace> lmCache;
    mutable std::once_flag spawnOnce;
    mutable LandmarkPlace spawnLm;

    LandmarkPlace findLandmark(int cellX, int cellZ) const {
        LandmarkPlace none;
        if (!decorSet || landmarkScale <= 0.0f) return none;
        const int LC = decor::LANDMARK_CELL;
        int wx = cellX * LC + LC / 4 + (int)(cellRandom(cellX, cellZ, 71) * (LC / 2));
        int wz = cellZ * LC + LC / 4 + (int)(cellRandom(cellX, cellZ, 72) * (LC / 2));
        TerrainColumn c = column(wx, wz);
        if (c.eco >= decor::BIOMES) return none;
        float roll = cellRandom(cellX, cellZ, 73) / landmarkScale, acc = 0.0f;
        int pick = -1;
        for (const auto& [di, dens] : decorSet->list(decor::Layer::Landmark, c.eco)) {
            acc += dens;
            if (roll < acc) { pick = di; break; }
        }
        if (pick < 0) return none;
        const decor::Def& d = decorSet->defs[(size_t)pick];
        if (d.variants.empty()) return none;
        if (c.waterLevel > c.ground || c.deckY >= 0) return none;
        if (std::find(d.on.begin(), d.on.end(), c.top) == d.on.end()) return none;
        // level enough across its footprint
        int r = std::max(4, d.variants[0].radius / 2);
        int hi = c.ground, lo = c.ground;
        for (int k = 0; k < 4; k++) {
            TerrainColumn o = column(wx + (k == 0 ? r : k == 1 ? -r : 0), wz + (k == 2 ? r : k == 3 ? -r : 0));
            if (o.waterLevel > o.ground) return none;
            hi = std::max(hi, o.ground); lo = std::min(lo, o.ground);
        }
        if ((float)(hi - lo) > d.maxSlope * 2.0f * r) return none;
        if (!landmarkSpotOk(wx, wz)) return none;
        LandmarkPlace lp;
        lp.def = pick;
        lp.shape = &d.variants[(size_t)((int)(cellRandom(cellX, cellZ, 74) * d.variants.size()) % (int)d.variants.size())];
        lp.wx = wx; lp.wz = wz; lp.wy = lo;
        lp.meadow = d.meadow;
        return lp;
    }

    virtual bool rockInCell(int cellX, int cellZ, RockPlace& out) const {
        (void)cellX; (void)cellZ; (void)out;
        return false;
    }
    std::vector<std::unordered_set<uint32_t>> woodAt;

    // Logs, or any block the registry calls wood (safe before it loads)
    static bool isWood(uint16_t id) {
        if (id == BLOCK_LOG_Y || id == BLOCK_LOG_X || id == BLOCK_LOG_Z) return true;
        return id < gRegistry.size() && gRegistry.get(id).material == "wood";
    }
    static uint32_t packOffset(int dx, int dy, int dz) {
        return ((uint32_t)(dx + 128) << 16) | ((uint32_t)(dz + 128) << 8) | (uint32_t)((dy + 32) & 0xFF);
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

        type = replaceTree(type, cellX, cellZ);
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
                (dst == BLOCK_LEAVES && v.id != BLOCK_LEAVES) ||
                (v.wood && dst == BLOCK_WATER))
                dst = v.id;
        }
    }
};
