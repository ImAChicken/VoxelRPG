#pragma once
#include "../chunk/TerrainGenerator.h"
#include "../voxel/VoxelRegistry.h"
#include "IslandChart.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// The sky world: Mushroom Islands (ROADMAP "Mushroom Islands")
// ══════════════════════════════════════════════════════════
// Islands floating over a void. The layout is designed, the details
// come from the seed: data/worlds/mushroom_islands.toml lists the
// islands (their roles, rough places, sizes and heights); the seed
// shapes each one (its outline, its top, the rock hanging under it)
// and scatters small islands between them.
//
// An island is built from parts (Claude, 2026-11-25):
//   body     its ground: a top surface and an underside, from its
//            outline: thick under the middle, thinning to the rim,
//            the bottom ragged with hanging rock. A column of the
//            world holds the island above it as one span (floor ..
//            ground, chunk/TerrainGenerator.h TerrainColumn::floor)
//   shape    how its top rises by role: "start" and "brush" (small,
//            a gentle dome), "body" (rolling), "bowl" (a ring of
//            mountains round a low floor: phase 1 step 4)
// Later parts (arches, rings, spires, caves, the mine, the giant
// mushroom) are 3D shapes added or cut per chunk (partsTop /
// carveSection).
//
// Falling below the void height kills (Simulation: the sky world's
// void kills, it doesn't rescue).
class SkyGenerator : public TerrainGenerator {
public:
    struct Island {
        std::string id, role;          // role: start, brush, body, bowl, village …
        float x = 0, z = 0;            // middle (world voxels)
        float y = 400;                 // its ground's base height
        float radius = 60;             // reach of its outline (voxels)
        float rise = 8;                // how high its top rises over y
        float depth = 0;               // how far its rock hangs under the middle (0: from its size)
        uint32_t salt = 0;             // its own noise
        float wobble = 0.22f;          // how far its outline wanders (stones: little, so the gaps hold)
        float notch = 0.0f;            // a bowl: the angle its way in faces (radians, atan2(z, x))
        std::string chain;             // a stepping stone: the chain it's on ("start>bowl")
    };
    struct Chain { std::string from, to; float rLo = 9, rHi = 16, swing = 0.35f; };
    struct Scatter { int count = 0; float rLo = 8, rHi = 30, yLo = 250, yHi = 700, margin = 300; };
    struct Layout {
        int   size = 48;               // chunks a side
        float voidY = 150;             // falling below this kills
        std::vector<Island> islands;
        std::vector<Chain> chains;     // stepping stones (made with the island shapes: SkyGenerator)
        Scatter scatter;
        uint64_t seed = 0;
        bool  ok = false;
    };

    // The layout file: the default world, or another (tests)
    static Layout loadLayout(const std::string& path, uint64_t seed) {
        Layout L;
        toml::table t;
        try { t = toml::parse_file(path); }
        catch (const std::exception& e) { std::cerr << "Sky world: can't read " << path << ": " << e.what() << "\n"; return L; }
        L.size = std::clamp((int)t["size"].value_or(48), 8, 256);
        L.voidY = (float)t["void_y"].value_or(150.0);
        uint32_t n = 0;
        if (auto* arr = t["island"].as_array())
            for (auto& e : *arr) {
                auto* it = e.as_table();
                if (!it) continue;
                Island is;
                is.id = (*it)["id"].value_or(std::string("island"));
                is.role = (*it)["role"].value_or(std::string("body"));
                if (auto* a = (*it)["at"].as_array(); a && a->size() >= 2) {
                    is.x = (float)a->get(0)->value_or(0.0);
                    is.z = (float)a->get(1)->value_or(0.0);
                }
                is.y = (float)(*it)["y"].value_or(400.0);
                is.radius = std::max(6.0f, (float)(*it)["radius"].value_or(60.0));
                is.rise = (float)(*it)["rise"].value_or((double)std::min(40.0f, is.radius * 0.12f));
                is.depth = (float)(*it)["depth"].value_or(0.0);
                // the seed nudges it (within 'jitter' voxels; the start island stays put)
                float jitter = (float)(*it)["jitter"].value_or(is.role == "start" ? 0.0 : 40.0);
                is.salt = (uint32_t)(seed * 2654435761u) ^ (++n * 0x9E3779B9u);
                is.x += (hash01(is.salt, 1) * 2.0f - 1.0f) * jitter;
                is.z += (hash01(is.salt, 2) * 2.0f - 1.0f) * jitter;
                is.y += (hash01(is.salt, 3) * 2.0f - 1.0f) * jitter * 0.25f;
                L.islands.push_back(is);
            }
        // a bowl's way in faces the island its chain comes from
        if (auto* arr = t["chain"].as_array())
            for (auto& e : *arr)
                if (auto* ct = e.as_table()) {
                    std::string from = (*ct)["from"].value_or(std::string()), to = (*ct)["to"].value_or(std::string());
                    Island* A = nullptr; Island* B = nullptr;
                    for (Island& is : L.islands) { if (is.id == from) A = &is; if (is.id == to) B = &is; }
                    if (A && B && B->role == "bowl") B->notch = std::atan2(A->z - B->z, A->x - B->x);
                }
        if (auto* arr = t["chain"].as_array())
            for (auto& e : *arr)
                if (auto* ct = e.as_table()) {
                    Chain c;
                    c.from = (*ct)["from"].value_or(std::string()); c.to = (*ct)["to"].value_or(std::string());
                    if (auto* a = (*ct)["radius"].as_array(); a && a->size() >= 2) { c.rLo = (float)a->get(0)->value_or(9.0); c.rHi = (float)a->get(1)->value_or(16.0); }
                    c.swing = (float)(*ct)["swing"].value_or(0.35);
                    L.chains.push_back(c);
                }
        if (auto* sc = t["scatter"].as_table()) {
            Scatter& S = L.scatter;
            S.count = (int)(*sc)["count"].value_or(0);
            if (auto* a = (*sc)["radius"].as_array(); a && a->size() >= 2) { S.rLo = (float)a->get(0)->value_or(8.0); S.rHi = (float)a->get(1)->value_or(30.0); }
            if (auto* a = (*sc)["y"].as_array(); a && a->size() >= 2) { S.yLo = (float)a->get(0)->value_or(250.0); S.yHi = (float)a->get(1)->value_or(700.0); }
            S.margin = (float)(*sc)["margin"].value_or(300.0);
        }
        L.seed = seed;
        L.ok = !L.islands.empty();
        return L;
    }

    // ── Stepping stones ───────────────────────────────────
    // How far a jump carries, from the player's real movement
    // (entity/Human.h): walking speed, jump height and gravity. A gap
    // between stones is at most COMFORT of what a walking jump clears
    // for its rise, so every one can be crossed without sprinting.
    static float jumpReach(float rise, float speed) {
        const float g = 40.0f, h = 15.0f;                  // Human::GRAVITY, Human::JUMP_HEIGHT
        float v0 = std::sqrt(2.0f * g * h);
        float disc = v0 * v0 - 2.0f * g * rise;
        if (disc < 0.0f) return 0.0f;                      // too high to reach
        return speed * (v0 + std::sqrt(disc)) / g;
    }
    static constexpr float WALK = 30.0f;                   // Human::WALK_SPEED
    static constexpr float COMFORT = 0.75f;

    static constexpr const char* LAYOUT_FILE = "data/worlds/mushroom_islands.toml";
    static int sizeChunks(const std::string& path = LAYOUT_FILE) {
        toml::table t;
        try { t = toml::parse_file(path); } catch (...) { return 48; }
        return std::clamp((int)t["size"].value_or(48), 8, 256);
    }

    SkyGenerator(uint64_t seed, int sizeChunks, const std::string& layoutPath = LAYOUT_FILE)
        : TerrainGenerator((int)(seed % 1000000)) {
        layout = loadLayout(layoutPath, seed);
        (void)sizeChunks;
        decorSet = nullptr;                          // (plants and decorations: step 5)
        topId   = idOr("moss", BLOCK_GRASS);
        subId   = BLOCK_DIRT;
        deepId  = BLOCK_STONE;
        underA  = idOr("lichen_stone", BLOCK_STONE);
        underB  = idOr("moss_stone", BLOCK_STONE);
        rockTop = idOr("slate", BLOCK_STONE);
        mycelium = idOr("mycelium", topId);
        capIds[0] = idOr("glowcap_blue", BLOCK_STONE); capIds[1] = idOr("glowcap_pink", BLOCK_STONE); capIds[2] = idOr("glowcap_green", BLOCK_STONE);
        stemId = idOr("shroom_stem", BLOCK_STONE);
        grappleId = idOr("grapple_point", BLOCK_PLANKS);
        lanternId = idOr("lantern", 0);
        makeMushroomTrees();
        outline.SetSeed(seed_ + 11); outline.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2); outline.SetFrequency(1.0f);
        surface.SetSeed(seed_ + 12); surface.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        surface.SetFractalType(FastNoiseLite::FractalType_FBm); surface.SetFractalOctaves(4); surface.SetFrequency(0.012f);
        spikes.SetSeed(seed_ + 13); spikes.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
        spikes.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance); spikes.SetFrequency(0.06f);
        mottle.SetSeed(seed_ + 14); mottle.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2); mottle.SetFrequency(0.05f);
        ridge.SetSeed(seed_ + 15); ridge.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ridge.SetFractalType(FastNoiseLite::FractalType_FBm); ridge.SetFractalOctaves(4); ridge.SetFrequency(0.006f);
        side = layout.size * VOXEL_DIM;
        // stepping stones, measured against the islands' real shapes;
        // then the small islands scattered round everything
        uint32_t cs = (uint32_t)(layout.seed * 40503u) ^ 0x51u;
        for (const Chain& c : layout.chains) addChain(c, cs++);
        scatterIslands();
        // the islands' boxes on a coarse grid, for the columns to find them fast
        gridN = std::max(1, side / GRID);
        grid.assign((size_t)gridN * gridN, {});
        for (size_t i = 0; i < layout.islands.size(); i++) {
            const Island& is = layout.islands[i];
            float r = is.radius * 1.3f + 4.0f;
            int gx0 = std::max(0, (int)((is.x - r) / GRID)), gx1 = std::min(gridN - 1, (int)((is.x + r) / GRID));
            int gz0 = std::max(0, (int)((is.z - r) / GRID)), gz1 = std::min(gridN - 1, (int)((is.z + r) / GRID));
            for (int gz = gz0; gz <= gz1; gz++) for (int gx = gx0; gx <= gx1; gx++) grid[(size_t)gz * gridN + gx].push_back((int)i);
        }
        findSites();                                 // (reads the ground: after the grid)
    }

    const Layout& skyLayout() const { return layout; }
    bool ok() const { return layout.ok; }
    float voidY() const { return layout.voidY; }
    const Island* island(const std::string& id) const {
        for (const Island& is : layout.islands) if (is.id == id) return &is;
        return nullptr;
    }

    // The island over a column, if any: its top and its underside
    // (world voxels; ground = first air above it, floor = its lowest
    // solid voxel) and how far in from its rim (0 at the rim, 1 in the
    // middle)
    struct Span { int island = -1; float top = 0, bottom = 0, in = 0; };
    Span spanAt(float wx, float wz) const {
        Span best;
        if (wx < 0 || wz < 0 || wx >= side || wz >= side) return best;
        const auto& cell = grid[(size_t)std::min(gridN - 1, (int)(wz / GRID)) * gridN + std::min(gridN - 1, (int)(wx / GRID))];
        for (int i : cell) {
            Span s = shapeOf(layout.islands[(size_t)i], wx, wz);
            if (s.island < 0) continue;
            s.island = i;
            if (best.island < 0 || s.top > best.top) best = s;      // (islands over islands: the upper one, for now)
        }
        return best;
    }

    TerrainColumn column(int wx, int wz) const override {
        TerrainColumn c;
        Span s = spanAt((float)wx + 0.5f, (float)wz + 0.5f);
        if (s.island < 0) {                       // the void
            c.ground = 1; c.floor = 1;
            c.eco = decor::ECO_NONE;
            return c;
        }
        int top = (int)std::floor(s.top), bottom = (int)std::floor(s.bottom);
        if (top - bottom < 2) bottom = top - 2;
        c.ground = std::clamp(top, 2, WORLD_HEIGHT);
        c.floor = std::clamp(bottom, 1, c.ground - 1);
        c.top = topId;
        c.sub = subId;
        c.subDepth = 4;
        c.deep = deepId;
        // the rock seen from below: lichen and moss in patches
        c.under = mottle.GetNoise((float)wx, (float)wz) > 0.1f ? underA : underB;
        c.underDepth = std::min(3, c.ground - c.floor - 1);
        // steep rims and the very edge: bare rock on top
        if (s.in < 0.04f) c.top = rockTop;
        c.eco = decor::ECO_NONE;
        const Island& is = layout.islands[(size_t)s.island];
        if (is.role == "bowl") {
            float t = 1.0f - s.in;
            if (t < BOWL_RIDGE_T + 0.02f && s.in >= 0.04f) c.top = mycelium;
            // the mountains: bare rock where they climb steeply, slate
            // up on the crests
            if (t > BOWL_RIDGE_T + 0.02f && t < BOWL_OUTER_T) {
                float hOver = s.top - (is.y + 60.0f);
                if (hOver > 8.0f) { c.top = hOver > 120.0f ? rockTop : BLOCK_STONE; c.sub = BLOCK_STONE; c.subDepth = 3; }
            }
            // the ring road round the inner rim
            if (bowlRoad(is, (float)wx, (float)wz, t)) { c.top = BLOCK_GRAVEL; c.sub = BLOCK_GRAVEL; c.subDepth = 2; }
            // the lake
            int lakeTop = (int)(is.y + 3.0f);
            if (c.ground < lakeTop && t < BOWL_FLOOR_T) { c.waterLevel = lakeTop; c.top = BLOCK_GRAVEL; }
            // its floor counts as dense forest for what roams there
            // (roaming.toml: the Mushnubs). No plants come of it: this
            // world has no decoration set yet.
            if (t < BOWL_RIDGE_T) c.eco = island::DenseForest;
        } else if (is.role == "body" && s.in > 0.1f) {
            c.eco = island::Forest;
        }
        return c;
    }
    int getSurfaceY(int wx, int wz) const override {
        Span s = spanAt((float)wx + 0.5f, (float)wz + 0.5f);
        return s.island < 0 ? 0 : (int)std::floor(s.top);
    }
    // ── Giant glowing mushrooms (phase 1 step 5) ──────────
    // Grown like trees (one possible per TREE_CELL square): on the
    // bowl's floor, thickest in a ring round the lake, thinner on the
    // other big islands. Kept off the road, the lake, the pass, the
    // bonfire and the mine's mouth.
    bool treeInCell(int cellX, int cellZ, int& wx, int& wz, int& type) const override {
        if (mushFirst < 0) return false;
        const int margin = 5, span = TREE_CELL - margin * 2;
        wx = cellX * TREE_CELL + margin + (int)(cellRandom(cellX, cellZ, 1) * (float)span);
        wz = cellZ * TREE_CELL + margin + (int)(cellRandom(cellX, cellZ, 2) * (float)span);
        Span sp = spanAt((float)wx + 0.5f, (float)wz + 0.5f);
        if (sp.island < 0) return false;
        const Island& is = layout.islands[(size_t)sp.island];
        float chance = 0.0f;
        int size = 0;                                       // 0 small, 1 medium, 2 large
        float roll = cellRandom(cellX, cellZ, 4);
        if (is.role == "bowl") {
            float t = 1.0f - sp.in;
            if (t > BOWL_FLOOR_T + 0.03f) return false;
            if (std::fabs(t - BOWL_ROAD_T) * is.radius < BOWL_ROAD_W + 10.0f) return false;
            float lx = is.x + is.radius * 0.06f, lz = is.z - is.radius * 0.04f;
            float ld = std::hypot((float)wx - lx, (float)wz - lz) / (is.radius * BOWL_LAKE_R);
            if (ld < 1.15f) return false;                              // the lake
            chance = ld < 2.4f ? 0.6f : 0.22f;                         // a ring of them round the lake
            size = ld < 2.4f ? (roll < 0.45f ? 2 : 1) : (roll < 0.25f ? 1 : 0);
            if (sites.ok) {
                auto near2 = [&](glm::vec3 p, float r) { return std::hypot((float)wx - p.x, (float)wz - p.z) < r; };
                if (near2(sites.bonfire, 30.0f) || near2(sites.king, 60.0f) || near2(sites.mineIn, 40.0f)) return false;
            }
            float a = std::atan2((float)wz - is.z, (float)wx - is.x);
            if (std::fabs(std::remainder(a - is.notch, 6.2831853f)) * t * is.radius < BOWL_PASS_W * 1.5f) return false;
        } else if (is.role == "body") {
            if (sp.in < 0.15f) return false;
            chance = 0.12f; size = roll < 0.3f ? 1 : 0;
        } else return false;
        if (cellRandom(cellX, cellZ, 3) > chance) return false;
        int colour = (int)(cellRandom(cellX, cellZ, 5) * 3.0f) % 3;
        int variant = (int)(cellRandom(cellX, cellZ, 6) * MUSH_VARIANTS) % MUSH_VARIANTS;
        type = mushFirst + ((size * 3 + colour) * MUSH_VARIANTS + variant);
        if ((int)std::floor(sp.top) + trees[(size_t)type].height > WORLD_HEIGHT) return false;
        return true;
    }

    // ── The bowl's places (phase 1 steps 6, 7) ────────────
    //   bonfire   just inside the pass: rest, heal, come back here
    //   mine      a tunnel through the mountains on the far side,
    //             toward the next island (the village); its inner mouth
    //             boarded up until the Mushroom King falls
    //   king      in front of the mine's mouth: the way on is his
    struct Sites {
        bool ok = false;
        glm::vec3 bonfire{0}, king{0}, mineIn{0}, mineOut{0};
        float mineAngle = 0.0f;
        // the boards across the mine (world voxels, inclusive)
        glm::ivec3 sealLo{0}, sealHi{0};
    };
    const Sites& bowlSites() const { return sites; }
    // The mine's tunnel and its boards (TerrainGenerator::carveSection)
    bool carvesInto(int cx, int cz, int sy) const override {
        if (!sites.ok) return false;
        glm::vec2 lo(std::min(sites.mineIn.x, sites.mineOut.x) - 8, std::min(sites.mineIn.z, sites.mineOut.z) - 8);
        glm::vec2 hi(std::max(sites.mineIn.x, sites.mineOut.x) + 8, std::max(sites.mineIn.z, sites.mineOut.z) + 8);
        float x0 = (float)(cx * VOXEL_DIM), z0 = (float)(cz * VOXEL_DIM);
        if (x0 > hi.x || x0 + VOXEL_DIM < lo.x || z0 > hi.y || z0 + VOXEL_DIM < lo.y) return false;
        float ylo = std::min(sites.mineIn.y, sites.mineOut.y) - 2, yhi = std::max(sites.mineIn.y, sites.mineOut.y) + MINE_H + 2;
        return sy * VOXEL_DIM < yhi && (sy + 1) * VOXEL_DIM > ylo;
    }
    void carveSection(uint16_t* vox, int x0, int z0, int y0, const std::vector<TerrainColumn>& cols) const override {
        (void)cols;
        if (!sites.ok) return;
        const int D = VOXEL_DIM;
        glm::vec2 a(sites.mineIn.x, sites.mineIn.z), b(sites.mineOut.x, sites.mineOut.z);
        glm::vec2 ab = b - a;
        float len = glm::length(ab);
        if (len < 1.0f) return;
        glm::vec2 dir = ab / len, side(-dir.y, dir.x);
        // quick reject: the section's box against the tunnel's
        float ylo = std::min(sites.mineIn.y, sites.mineOut.y) - 2, yhi = std::max(sites.mineIn.y, sites.mineOut.y) + MINE_H + 2;
        if (y0 > yhi || y0 + D < ylo) return;
        for (int x = 0; x < D; x++)
        for (int z = 0; z < D; z++) {
            glm::vec2 p((float)(x0 + x) + 0.5f, (float)(z0 + z) + 0.5f);
            float along = glm::dot(p - a, dir), across = glm::dot(p - a, side);
            if (along < -2.0f || along > len + 2.0f || std::fabs(across) > MINE_W * 0.5f + 1.5f) continue;
            float u = std::clamp(along / len, 0.0f, 1.0f);
            int floorY = (int)std::lround(sites.mineIn.y + (sites.mineOut.y - sites.mineIn.y) * u);
            bool inside = std::fabs(across) <= MINE_W * 0.5f;
            bool frame = std::fmod(std::max(along, 0.0f), MINE_FRAME) < 1.0f && along > 4.0f && along < len - 4.0f;
            for (int y = std::max(y0, floorY - 1); y < std::min(y0 + D, floorY + (int)MINE_H + 1); y++) {
                uint16_t& v = vox[(size_t)Chunk::index(x, y - y0, z)];
                int up = y - floorY;
                if (up < 0) { if (inside && v == BLOCK_AIR) v = BLOCK_PLANKS; continue; }      // (a floor where it opens out)
                if (!inside) {
                    // the timber frame's posts, at the walls
                    if (frame && up < MINE_H && v != BLOCK_AIR) v = BLOCK_LOG_Y;
                    continue;
                }
                if (up >= MINE_H) { if (frame) v = BLOCK_PLANKS; continue; }                // the frame's beam
                v = BLOCK_AIR;
                if (frame && up == MINE_H - 1 && std::fabs(across) < 0.6f && lanternId) v = lanternId;   // a lamp from each beam
                // the boards across its inner mouth (taken down when the King falls)
                if (along >= MINE_SEAL_AT && along < MINE_SEAL_AT + 2.0f) v = BLOCK_PLANKS;
            }
        }
    }
    float forestCover(int, int) const override { return 0.0f; }

    // By the portal on the start island (the first one with role start)
    glm::ivec3 spawnPoint(int, int) const override {
        for (const Island& is : layout.islands)
            if (is.role == "start") {
                int x = (int)is.x, z = (int)is.z;
                return { x, getSurfaceY(x, z), z };
            }
        int c = side / 2;
        return { c, getSurfaceY(c, c), c };
    }

private:
    Layout layout;
    int side = 0, gridN = 1;
    static constexpr int GRID = 256;
    std::vector<std::vector<int>> grid;
    FastNoiseLite outline, surface, spikes, mottle, ridge;
    int seed_ = seed;
    uint16_t topId = 0, subId = 0, deepId = 0, underA = 0, underB = 0, rockTop = 0;
    uint16_t mycelium = 0, stemId = 0, grappleId = 0, lanternId = 0;
    uint16_t capIds[3] = { 0, 0, 0 };
    Sites sites;
    int mushFirst = -1;                              // the first giant mushroom's tree kind
    static constexpr int MUSH_VARIANTS = 3;
    static constexpr float MINE_W = 7.0f, MINE_H = 9.0f, MINE_FRAME = 10.0f, MINE_SEAL_AT = 3.0f;

    // Where the bowl's places go (after the islands are laid out)
    void findSites() {
        const Island* B = island("bowl");
        if (!B) return;
        const Island* V = island("village");
        sites.mineAngle = V ? std::atan2(V->z - B->z, V->x - B->x) : B->notch + 3.14159f;
        auto at = [&](float ang, float t, float sideways) {
            float r = t * B->radius;
            glm::vec2 d(std::cos(ang), std::sin(ang)), sd(-d.y, d.x);
            glm::vec2 p = glm::vec2(B->x, B->z) + d * r + sd * sideways;
            Span sp = spanAt(p.x, p.y);
            return glm::vec3(p.x, sp.island >= 0 ? std::floor(sp.top) : B->y, p.y);
        };
        sites.bonfire = at(B->notch, BOWL_FLOOR_T + 0.035f, 14.0f);
        sites.mineIn  = at(sites.mineAngle, BOWL_RIDGE_T - 0.005f, 0.0f);
        // the tunnel runs level (a gentle climb at most) and comes out
        // where the mountains' outer slope drops to its floor
        sites.mineOut = at(sites.mineAngle, 0.97f, 0.0f);
        for (float t = BOWL_OUTER_T; t < 1.0f; t += 0.002f) {
            glm::vec3 q = at(sites.mineAngle, t, 0.0f);
            if (q.y <= sites.mineIn.y + 2.0f) { sites.mineOut = q; break; }
        }
        {
            float run = glm::length(glm::vec2(sites.mineOut.x - sites.mineIn.x, sites.mineOut.z - sites.mineIn.z));
            sites.mineOut.y = std::clamp(sites.mineOut.y, sites.mineIn.y, sites.mineIn.y + run * 0.12f);
        }
        sites.king    = at(sites.mineAngle, BOWL_RIDGE_T - 0.07f, 0.0f);
        // the boards' box, round the tunnel MINE_SEAL_AT in
        glm::vec2 a(sites.mineIn.x, sites.mineIn.z), b(sites.mineOut.x, sites.mineOut.z);
        glm::vec2 dir = glm::normalize(b - a), side(-dir.y, dir.x);
        glm::vec2 c0 = a + dir * MINE_SEAL_AT - side * (MINE_W * 0.5f + 1.0f), c1 = a + dir * (MINE_SEAL_AT + 2.0f) + side * (MINE_W * 0.5f + 1.0f);
        glm::vec2 c2 = a + dir * MINE_SEAL_AT + side * (MINE_W * 0.5f + 1.0f), c3 = a + dir * (MINE_SEAL_AT + 2.0f) - side * (MINE_W * 0.5f + 1.0f);
        glm::vec2 lo = glm::min(glm::min(c0, c1), glm::min(c2, c3)), hi = glm::max(glm::max(c0, c1), glm::max(c2, c3));
        int fy = (int)std::lround(sites.mineIn.y);
        sites.sealLo = glm::ivec3((int)std::floor(lo.x), fy - 1, (int)std::floor(lo.y));
        sites.sealHi = glm::ivec3((int)std::ceil(hi.x), fy + (int)MINE_H + 2, (int)std::ceil(hi.y));
        sites.ok = true;
    }

    // The giant mushrooms' shapes: three sizes × three cap colours ×
    // MUSH_VARIANTS, added to the generator's tree kinds
    void makeMushroomTrees() {
        mushFirst = (int)trees.size();
        for (int size = 0; size < 3; size++)
        for (int colour = 0; colour < 3; colour++)
        for (int v = 0; v < MUSH_VARIANTS; v++) {
            uint32_t h = (uint32_t)(size * 97 + colour * 13 + v * 7 + 1);
            float rnd = hash01(h, 1), rnd2 = hash01(h, 2), rnd3 = hash01(h, 3);
            int height = size == 0 ? 12 + (int)(rnd * 8) : size == 1 ? 24 + (int)(rnd * 12) : 42 + (int)(rnd * 18);
            float capR = size == 0 ? 5.0f + rnd2 * 3.0f : size == 1 ? 9.0f + rnd2 * 4.0f : 14.0f + rnd2 * 5.0f;
            float stemR = size == 0 ? 1.2f : size == 1 ? 2.0f : 3.2f;
            // the stem leans and curves a little
            float leanX = (rnd3 - 0.5f) * height * 0.35f, leanZ = (hash01(h, 4) - 0.5f) * height * 0.35f;
            TreeShape t;
            auto put = [&](int dx, int dy, int dz, uint16_t id, bool wood) {
                if (std::abs(dx) > 120 || std::abs(dz) > 120) return;
                t.voxels.push_back({ (int8_t)dx, (int8_t)dz, (int16_t)dy, id, (uint8_t)(wood ? 1 : 0) });
                t.height = std::max(t.height, dy + 1);
                t.radius = std::max({ t.radius, std::abs(dx), std::abs(dz) });
            };
            for (int y = -3; y < height; y++) {
                float u = std::max(0.0f, (float)y / height);
                float cx = leanX * u * u, cz = leanZ * u * u;
                float r = stemR * (1.0f + 0.5f * (1.0f - u) * (1.0f - u));          // a flared foot
                int R = (int)std::ceil(r);
                for (int dx = -R; dx <= R; dx++) for (int dz = -R; dz <= R; dz++)
                    if ((dx - (cx - std::round(cx))) * (dx - (cx - std::round(cx))) + dz * dz <= r * r)
                        put(dx + (int)std::round(cx), y, dz + (int)std::round(cz), stemId, true);
            }
            // the cap: a wide, flattened dome over the stem's top
            int topX = (int)std::round(leanX), topZ = (int)std::round(leanZ);
            int thick = size == 0 ? 3 : size == 1 ? 4 : 6;
            int R = (int)std::ceil(capR);
            uint16_t cap = capIds[colour];
            for (int dx = -R; dx <= R; dx++)
            for (int dz = -R; dz <= R; dz++) {
                float d = std::sqrt((float)(dx * dx + dz * dz)) / capR;
                if (d > 1.0f) continue;
                int top = height + (int)std::round(thick * std::sqrt(std::max(0.0f, 1.0f - d * d)));   // domed top
                int bottom = height - (int)std::round(d * 2.0f);                                     // the rim curls down
                for (int y = bottom; y <= top; y++) put(topX + dx, y, topZ + dz, cap, false);
            }
            // something to grapple to, in the gills (bigger mushrooms only)
            if (size > 0) {
                int gx = topX + (int)std::round(capR * 0.55f), gz = topZ;
                put(gx, height - 2, gz, grappleId, false);
                if (size == 2) put(topX - (int)std::round(capR * 0.55f), height - 2, topZ, grappleId, false);
            }
            trees.push_back(std::move(t));
            treeNames.push_back(std::string("glowshroom_") + (size == 0 ? "small" : size == 1 ? "medium" : "large"));
        }
        // the base's lookup tables cover the new kinds too
        maxTreeRadius = 0;
        for (const TreeShape& t : trees) maxTreeRadius = std::max(maxTreeRadius, t.radius);
        woodAt.resize(trees.size());
        for (size_t k = (size_t)mushFirst; k < trees.size(); k++)
            for (const TreeVoxel& v : trees[k].voxels)
                if (v.wood) woodAt[k].insert(packOffset(v.dx, v.dy, v.dz));
    }

    static uint16_t idOr(const char* name, uint16_t fallback) {
        uint16_t id = gRegistry.getIDByName(name);
        return id ? id : fallback;
    }
    static float hash01(uint32_t s, uint32_t k) {
        uint32_t h = s ^ (k * 0x9E3779B1u);
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
        return (float)(h & 0xFFFFFF) / (float)0x1000000;
    }

    // ── The bowl (ROADMAP phase 1 step 4) ─────────────────
    // A ring of mountains round a low floor. Across its radius t (0 the
    // middle, 1 the rim):
    //   floor     0 .. FLOOR_T: dipping gently to a lake near the middle
    //   foot      FLOOR_T .. RIDGE_T: rising to the mountains' foot,
    //             where the road runs round (ROAD_T)
    //   ridge     RIDGE_T .. OUTER_T: the mountains, sharp-crested
    //   outside   OUTER_T .. 1: falling steeply to a low rim
    // One notch through the ring faces the stepping stones: the way in
    static constexpr float BOWL_FLOOR_T = 0.50f, BOWL_ROAD_T = 0.575f, BOWL_RIDGE_T = 0.66f, BOWL_OUTER_T = 0.88f;
    static constexpr float BOWL_LAKE_R = 0.12f;            // × radius
    static constexpr float BOWL_ROAD_W = 4.0f;             // voxels either side of the road's middle
    static constexpr float BOWL_PASS_W = 34.0f;            // voxels: half the way in's width
    float bowlTop(const Island& is, float wx, float wz, float t, float n) const {
        const float R = is.radius;
        float h;
        if (t < BOWL_FLOOR_T) {
            float u = t / BOWL_FLOOR_T;
            h = 4.0f + 14.0f * u * u + n * 3.0f;
        } else if (t < BOWL_RIDGE_T) {
            float u = (t - BOWL_FLOOR_T) / (BOWL_RIDGE_T - BOWL_FLOOR_T);
            h = 18.0f + 42.0f * u * u + n * 4.0f * u;
        } else if (t < BOWL_OUTER_T) {
            float u = (t - BOWL_RIDGE_T) / (BOWL_OUTER_T - BOWL_RIDGE_T);       // 0 foot … 1 outer edge
            float crest = std::sin(u * 3.14159f);                                // highest mid-ring
            float ridged = 1.0f - std::fabs(ridge.GetNoise(wx, wz));            // sharp crests
            float peaks = 140.0f + 160.0f * ridged * ridged;
            h = 60.0f + (peaks - 60.0f) * std::pow(crest, 0.7f) + n * 10.0f;
            if (u > 0.5f) h = std::max(h, 60.0f * (1.0f - (u - 0.5f) * 2.0f) + 24.0f);
        } else {
            float u = (t - BOWL_OUTER_T) / (1.0f - BOWL_OUTER_T);
            h = 24.0f - 8.0f * u + n * 3.0f;
        }
        // the lake: a hollow near the middle
        float lx = is.x + R * 0.06f, lz = is.z - R * 0.04f;
        float ld = std::hypot(wx - lx, wz - lz) / (R * BOWL_LAKE_R);
        if (ld < 1.0f) h -= 14.0f * (1.0f - ld * ld);
        // the way in: a pass through the ring, facing the stones
        if (t > BOWL_FLOOR_T) {
            float a = std::atan2(wz - is.z, wx - is.x);
            float da = std::remainder(a - is.notch, 6.2831853f);
            float across = std::fabs(da) * t * R;                              // voxels off the pass's middle
            if (across < BOWL_PASS_W * 2.0f) {
                float passH = t < BOWL_RIDGE_T ? h : 24.0f + 6.0f * (1.0f - (t - BOWL_RIDGE_T) / (1.0f - BOWL_RIDGE_T));
                passH = std::min(passH, std::max(18.0f + 40.0f * (t - BOWL_FLOOR_T), 24.0f));
                float k = std::clamp((across - BOWL_PASS_W) / BOWL_PASS_W, 0.0f, 1.0f);  // 0 in the pass, 1 its walls
                k = k * k * (3.0f - 2.0f * k);
                h = passH + (h - passH) * k;
            }
        }
        return is.y + h;
    }
    // Is (wx, wz) on the bowl's ring road, and the road's height there
    bool bowlRoad(const Island& is, float wx, float wz, float t) const {
        return std::fabs(t - BOWL_ROAD_T) * is.radius < BOWL_ROAD_W;
    }

    // How far an island's ground reaches from its middle along a
    // direction (its real outline), and its top there
    float edgeAlong(const Island& is, glm::vec2 d, float& topThere) const {
        float last = 0.0f; topThere = is.y;
        for (float r = 0.0f; r < is.radius * 1.5f; r += 1.0f) {
            Span sp = shapeOf(is, is.x + d.x * r, is.z + d.y * r);
            if (sp.island < 0) break;
            last = r; topThere = sp.top;
        }
        return last;
    }
    // A chain of stepping stones from one island to another
    void addChain(const Chain& ch, uint32_t salt) {
        int ia = -1, ib = -1;
        for (size_t i = 0; i < layout.islands.size(); i++) { if (layout.islands[i].id == ch.from) ia = (int)i; if (layout.islands[i].id == ch.to) ib = (int)i; }
        if (ia < 0 || ib < 0) { std::cerr << "Sky world: chain " << ch.from << " > " << ch.to << ": no such island\n"; return; }
        const Island A = layout.islands[(size_t)ia], B = layout.islands[(size_t)ib];
        // where it leaves A and where it lands on B (a bowl: the pass's mouth)
        glm::vec2 a(A.x, A.z), b(B.x, B.z), dir = glm::normalize(b - a);
        float hA, hB;
        glm::vec2 p = a + dir * edgeAlong(A, dir, hA);
        glm::vec2 land = b - dir * edgeAlong(B, -dir, hB);
        float h = hA;
        std::vector<Island> stones;
        for (int i = 0; i < 80; i++) {
            float left = glm::length(land - p);
            float lastRise = std::max(hB - h, 0.0f);
            if (left <= jumpReach(lastRise, WALK) * COMFORT && hB - h <= 8.0f) break;     // the last jump lands on B
            Island s;
            s.role = "stone";
            s.wobble = 0.06f;
            s.chain = ch.from + ">" + ch.to;
            s.id = "stone_" + ch.from + "_" + ch.to + "_" + std::to_string(i);
            s.radius = ch.rLo + hash01(salt, (uint32_t)i * 7 + 1) * (ch.rHi - ch.rLo);
            s.rise = 2.0f;
            s.depth = s.radius * 1.6f;
            s.salt = salt * 31u + (uint32_t)i;
            // up or down a little each time, drifting toward B's height
            int jumpsLeft = std::max(1, (int)(left / (jumpReach(0.0f, WALK) * 0.6f + ch.rHi * 2.0f)));
            float want = std::clamp((hB - h) / (float)jumpsLeft, -10.0f, 5.0f);
            float dy = std::clamp(want + (hash01(salt, (uint32_t)i * 7 + 2) * 2.0f - 1.0f) * 3.0f, -10.0f, 5.0f);
            float gap = (0.45f + 0.5f * hash01(salt, (uint32_t)i * 7 + 3)) * jumpReach(std::max(dy, 0.0f), WALK) * COMFORT;
            glm::vec2 toLand = glm::normalize(land - p);
            float ang = (hash01(salt, (uint32_t)i * 7 + 4) * 2.0f - 1.0f) * ch.swing;
            glm::vec2 d(toLand.x * std::cos(ang) - toLand.y * std::sin(ang), toLand.x * std::sin(ang) + toLand.y * std::cos(ang));
            s.y = h + dy - s.rise;
            glm::vec2 c = p + d * (gap + s.radius);
            s.x = c.x; s.z = c.y;
            // its real near edge: pull it in until the gap is what was planned
            for (int k = 0; k < 8; k++) {
                float top;
                float r = edgeAlong(s, -d, top);
                float real = glm::length(c - p) - r;
                if (real <= gap + 0.5f) break;
                c -= d * (real - gap);
                s.x = c.x; s.z = c.y;
            }
            stones.push_back(s);
            float top;
            p = c + toLand * edgeAlong(s, toLand, top);                 // the next jump leaves from its far side
            h = top;
        }
        for (Island& s : stones) layout.islands.push_back(s);
    }
    // Small islands scattered round everything, for the view
    void scatterIslands() {
        const Scatter& S = layout.scatter;
        const float sideF = (float)(layout.size * VOXEL_DIM);
        uint32_t s = (uint32_t)(layout.seed * 7919u) ^ 0xA5A5u;
        for (int i = 0, tries = 0; i < S.count && tries < S.count * 40; tries++) {
            Island is;
            is.id = "scatter" + std::to_string(i);
            is.role = "brush";
            is.x = S.margin + hash01(s, tries * 5 + 0) * (sideF - 2 * S.margin);
            is.z = S.margin + hash01(s, tries * 5 + 1) * (sideF - 2 * S.margin);
            is.radius = S.rLo + hash01(s, tries * 5 + 2) * (S.rHi - S.rLo);
            is.y = S.yLo + hash01(s, tries * 5 + 3) * (S.yHi - S.yLo);
            is.rise = is.radius * 0.25f;
            is.salt = s ^ (uint32_t)(tries * 0x85EBCA77u);
            bool clear = true;
            for (const Island& o : layout.islands) {
                float d = std::hypot(o.x - is.x, o.z - is.z);
                if (d < (o.radius + is.radius) * 1.35f + 30.0f) { clear = false; break; }
            }
            if (!clear) continue;
            layout.islands.push_back(is);
            i++;
        }
    }

    // One island's span over (wx, wz), or island = -1 outside it
    Span shapeOf(const Island& is, float wx, float wz) const {
        Span s;
        float dx = wx - is.x, dz = wz - is.z;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d > is.radius * 1.3f) return s;
        // the outline: the radius wanders with the angle (noise round
        // a circle, so it closes), and a little with the spot itself
        float a = std::atan2(dz, dx);
        float ox = (float)(is.salt % 997u), oz = (float)((is.salt / 997u) % 997u);
        float wob = (outline.GetNoise(ox + std::cos(a) * 1.6f, oz + std::sin(a) * 1.6f)
                  + outline.GetNoise(ox + std::cos(a) * 4.0f, oz + std::sin(a) * 4.0f) * 0.36f) * is.wobble;
        float R = is.radius * (1.0f + wob);
        float t = d / std::max(R, 1.0f);              // 0 middle … 1 rim
        if (t >= 1.0f) return s;
        float in = 1.0f - t;                           // 1 middle … 0 rim
        s.island = 0;
        s.in = in;
        // the top: by role
        float n = surface.GetNoise(wx + ox * 13.0f, wz + oz * 13.0f);
        float top = is.y;
        if (is.role == "start" || is.role == "stone") {
            // a flat top (the portal; a landing), rounding off near the rim
            top += is.rise * std::sqrt(std::min(1.0f, in * 3.0f)) + n * 1.5f;
        } else if (is.role == "bowl") {
            top = bowlTop(is, wx, wz, t, n);
        } else if (is.role == "brush") {
            top += is.rise * std::sqrt(in) + n * is.rise * 0.35f;
        } else {
            // rolling: gentle in the middle, rounding down at the rim
            float edge = std::sqrt(std::min(1.0f, in * 4.0f));
            top += edge * (is.rise * 0.6f + n * is.rise);
        }
        // the underside: thick under the middle, thin at the rim, with
        // rock hanging from it
        float depth = is.depth > 0.0f ? is.depth : std::clamp(is.radius * 0.55f, 10.0f, 260.0f);
        float body = depth * std::pow(in, 0.65f);
        // (cellular distance comes as -1 at a cell's point up to about 0)
        float cell = std::clamp(spikes.GetNoise(wx + ox * 7.0f, wz + oz * 7.0f) + 1.0f, 0.0f, 1.0f);
        float spike = std::clamp(1.0f - cell / 0.45f, 0.0f, 1.0f);                // 1 at a spike's tip
        body += spike * spike * depth * 0.45f * std::min(1.0f, in * 2.5f);
        body += n * depth * 0.08f;
        s.top = top;
        s.bottom = top - std::max(3.0f, body + 3.0f);
        return s;
    }
};
