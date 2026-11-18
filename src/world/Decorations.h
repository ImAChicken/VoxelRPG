#pragma once
#include <toml++/toml.hpp>
#include "../voxel/Voxel.h"
#include "../voxel/VoxelRegistry.h"
#include "../worldgen/IslandChart.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Decorations — plants, bushes, ground patches and landmarks
// ══════════════════════════════════════════════════════════
// Everything the world dresses its ground with, loaded from
// data/registry/decorations.toml. Each entry names a RECIPE (the
// code that builds its shape: "flower", "fern", "willow" …), the
// blocks the recipe uses, and how dense it is in each biome.
// Every recipe is built in a few random variants at load time,
// so no two flowers in a drift look quite the same.
//
// Four layers, each on its own grid of the world:
//
//   [[plant]]     small plants: grass tufts, flowers, ferns, reeds,
//                 mushrooms, lily pads. One possible plant per 4 × 4
//                 voxels (16 in), kept inside its square, so plants
//                 never merge and breaking one picks up exactly that
//                 plant. Made of "plant" blocks: walk-through.
//   [[decor]]     bigger pieces: bushes, fallen logs, stone piles,
//                 thorn bushes, ember vents. One per 10 × 10 voxels.
//   [[ground]]    patches of a different top block (moss, leaf
//                 litter, sulphur crust), shaped by noise.
//   [[landmark]]  rare one-offs: a lone willow, standing stones, a
//                 stone circle, a cairn. One per 256 × 256 voxels at
//                 most. One landmark can be marked spawn = true: it
//                 always grows beside the world spawn.
//
// Each layer's density is a chance per square of its grid, per
// biome; World Settings scales them (Plants, Bushes and logs,
// Landmarks). Every decoration is also an item (game/ItemRegistry.h),
// listed in the dev menu's Decorations tab, and can be placed by hand.
namespace decor {

constexpr int PLANT_CELL    = 4;
constexpr int LARGE_CELL    = 10;
constexpr int LANDMARK_CELL = 256;
constexpr int BIOMES        = 64;      // ≥ island::BIOME_COUNT

// Classic worlds have no biome map: their columns say plains or forest
constexpr uint8_t ECO_NONE = 255;

// One voxel of a shape, relative to its base (the first air voxel
// above the ground it stands on). replace: overwrite the ground
// there (an ember crack) instead of only filling air.
struct Vox { int8_t dx, dz; int16_t dy; uint16_t id; uint8_t replace; };
struct Shape {
    std::vector<Vox> v;
    int top = 0, bottom = 0, radius = 0;     // dy range [bottom, top); reach from the base
};

enum class Layer : uint8_t { Plant, Large, Ground, Landmark, Tree };
enum class Water : uint8_t { Dry, Shore, Shallow, Surface };
//   Dry      on dry ground
//   Shore    on dry ground within a few voxels of water
//   Shallow  standing in water 1-3 voxels deep (reeds), growing out of it
//   Surface  floating on water at least 2 deep (lily pads)

struct Def {
    std::string name, display, recipe;
    Layer layer = Layer::Plant;
    std::vector<uint16_t> blocks;            // what the recipe builds with, in its order
    int   hMin = 1, hMax = 3, size = 1;
    float patch = 0.0f, patchSize = 24.0f;   // 0 even … 1 strongly clumped into drifts
    std::vector<uint16_t> on;                // ground it grows on
    Water water = Water::Dry;
    float maxSlope = 1.0f;                   // rise per voxel across it
    std::array<float, BIOMES> density{};
    bool  spawn = false;                     // landmark beside the world spawn
    float scale = 1.0f;                      // trees and landmarks: overall size
    float twist = 1.0f;                      // twisted_oak: how crooked (0 straight … 1 gnarled)
    std::string replaces;                    // trees: grows wherever this built-in tree would (e.g. oak_tree)
    float meadow = 0.0f;                     // landmarks: radius of flowers around it
    std::vector<int> meadowPlants;
    float color[3] = { 0.5f, 0.5f, 0.5f };   // menu swatch
    std::vector<Shape> variants;
};

// ── Small helpers ─────────────────────────────────────────
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
inline float hash01(uint32_t a, uint32_t b, uint32_t c) { return (hash3(a, b, c) & 0xFFFFFF) / 16777216.0f; }

// Smooth 0..1 value noise, for drifts and patches
inline float valueNoise(int seed, float x, float z, float scale, uint32_t salt) {
    float fx = x / scale, fz = z / scale;
    int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    float tx = fx - ix, tz = fz - iz;
    tx = tx * tx * (3 - 2 * tx); tz = tz * tz * (3 - 2 * tz);
    auto h = [&](int a, int b) { return hash01((uint32_t)a ^ ((uint32_t)seed * 7919u), (uint32_t)b, salt); };
    float a = h(ix, iz), b = h(ix + 1, iz), c = h(ix, iz + 1), d = h(ix + 1, iz + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

inline std::string biomeKey(int b) {
    std::string s = island::biomeInfo(b).name;
    for (char& ch : s) ch = ch == ' ' ? '_' : (char)std::tolower((unsigned char)ch);
    return s;
}

// ── Recipes ───────────────────────────────────────────────
class ShapeBuilder {
public:
    Shape s;
    void add(int x, int y, int z, uint16_t id, bool replace = false) {
        if (!id) return;
        uint64_t k = ((uint64_t)(uint16_t)(int16_t)x << 32) | ((uint64_t)(uint16_t)(int16_t)z << 16) | (uint16_t)(int16_t)y;
        auto it = at.find(k);
        if (it != at.end()) { s.v[it->second].id = id; s.v[it->second].replace = replace; return; }
        at[k] = s.v.size();
        s.v.push_back({ (int8_t)x, (int8_t)z, (int16_t)y, id, (uint8_t)replace });
    }
    bool has(int x, int y, int z) const {
        uint64_t k = ((uint64_t)(uint16_t)(int16_t)x << 32) | ((uint64_t)(uint16_t)(int16_t)z << 16) | (uint16_t)(int16_t)y;
        return at.count(k) != 0;
    }
    Shape finish() {
        s.top = 0; s.bottom = 0; s.radius = 0;
        for (auto& v : s.v) {
            s.top = std::max(s.top, v.dy + 1);
            s.bottom = std::min(s.bottom, (int)v.dy);
            s.radius = std::max({ s.radius, std::abs((int)v.dx), std::abs((int)v.dz) });
        }
        return std::move(s);
    }
private:
    std::unordered_map<uint64_t, size_t> at;
};

class Recipes {
public:
    // Builds variant 'var' of a decoration
    static Shape build(const Def& d, int var) {
        uint32_t h = hash3((uint32_t)std::hash<std::string>{}(d.name), (uint32_t)var, 77);
        auto rnd = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
        auto rint = [&](int lo, int hi) { return lo + (int)(rnd() * (hi - lo + 1)) % std::max(1, hi - lo + 1); };
        auto B = [&](size_t i) -> uint16_t { return i < d.blocks.size() ? d.blocks[i] : 0; };
        ShapeBuilder sb;
        // the 3 × 3 around the base, centre first
        static const int RING[9][2] = { {0,0},{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1} };
        auto pickRing = [&](int n) {
            std::vector<int> idx = { 0 };
            std::vector<int> rest = { 1,2,3,4,5,6,7,8 };
            while ((int)idx.size() < n && !rest.empty()) {
                int k = (int)(rnd() * rest.size()) % (int)rest.size();
                idx.push_back(rest[(size_t)k]); rest.erase(rest.begin() + k);
            }
            return idx;
        };
        const std::string& r = d.recipe;

        if (r == "sprite") {
            // a stack of one sprite block: drawn as one flat picture as
            // tall as the stack (see VoxelRegistry 'render = "sprite"')
            int hh = rint(d.hMin, d.hMax);
            for (int y = 0; y < hh; y++) sb.add(0, y, 0, B(0));
        }
        else if (r == "tuft" || r == "tall_grass") {
            bool tall = r == "tall_grass";
            int n = tall ? rint(3, 6) : rint(2, 4);
            auto cols = pickRing(n);
            for (size_t k = 0; k < cols.size(); k++) {
                int x = RING[cols[k]][0], z = RING[cols[k]][1];
                int hh = k == 0 ? d.hMax : rint(d.hMin, std::max(d.hMin, d.hMax - 1));
                for (int y = 0; y < hh; y++) sb.add(x, y, z, (y == hh - 1 && B(1)) ? B(1) : B(0));
                // tall grass leans: the tip steps over one
                if (tall && hh >= 3 && rnd() < 0.4f) {
                    int lx = std::clamp(x + (rnd() < 0.5f ? 1 : -1), -1, 1);
                    sb.add(lx, hh, z, B(1) ? B(1) : B(0));
                }
            }
        }
        else if (r == "flower") {
            int hh = rint(d.hMin, d.hMax);
            for (int y = 0; y < hh; y++) sb.add(0, y, 0, B(0));
            if (hh >= 2 && rnd() < 0.6f) {              // a leaf
                int k = rint(1, 4);
                sb.add(RING[k][0], rint(0, hh / 2), RING[k][1], B(2) ? B(2) : B(0));
            }
            sb.add(0, hh, 0, B(1));
            if (d.size >= 2)
                for (int k = 1; k <= 4; k++) sb.add(RING[k][0], hh, RING[k][1], B(1));
            if (d.size >= 3) sb.add(0, hh + 1, 0, B(1));
        }
        else if (r == "fern") {
            int hh = rint(d.hMin, d.hMax);
            for (int y = 0; y < hh; y++) sb.add(0, y, 0, B(0));
            auto fr = pickRing(rint(5, 9));
            for (size_t k = 1; k < fr.size(); k++) {
                int x = RING[fr[k]][0], z = RING[fr[k]][1];
                int y = std::max(0, hh - 1 - rint(0, 1));
                sb.add(x, y, z, B(0));
                if (rnd() < 0.5f && y > 0) sb.add(x, y - 1, z, B(0));   // frond tips droop
            }
        }
        else if (r == "reed") {
            int n = rint(1, 3);
            auto cols = pickRing(n);
            int tallest = 0;
            for (size_t k = 0; k < cols.size(); k++) {
                int x = RING[cols[k]][0], z = RING[cols[k]][1];
                int hh = k == 0 ? d.hMax : rint(d.hMin, d.hMax);
                tallest = std::max(tallest, hh);
                for (int y = 0; y < hh; y++) sb.add(x, y, z, B(0));
                if (B(1) && hh >= 4 && (k == 0 || rnd() < 0.4f)) {      // cattail head below the tip
                    sb.add(x, hh - 3, z, B(1)); sb.add(x, hh - 2, z, B(1));
                }
            }
        }
        else if (r == "lily") {
            for (int k = 0; k < 9; k++) {
                if (k >= 5 && rnd() < 0.5f) continue;        // ragged corners
                sb.add(RING[k][0], 0, RING[k][1], B(0));
            }
            if (B(1) && rnd() < 0.35f) sb.add(0, 1, 0, B(1));
        }
        else if (r == "mushroom") {
            int count = d.size >= 2 ? 1 : rint(1, 3);
            auto at = pickRing(count);
            for (size_t k = 0; k < at.size(); k++) {
                int x = RING[at[k]][0], z = RING[at[k]][1];
                int hh = k == 0 ? rint(d.hMin, d.hMax) : d.hMin;
                for (int y = 0; y < hh; y++) sb.add(x, y, z, B(1));
                sb.add(x, hh, z, B(0));
                if (d.size >= 2 && k == 0)
                    for (int q = 1; q <= 4; q++) sb.add(RING[q][0], hh, RING[q][1], B(0));
            }
        }
        else if (r == "cushion") {
            for (int k = 0; k < 9; k++) {
                if (k >= 5 && rnd() < 0.4f) continue;
                int x = RING[k][0], z = RING[k][1];
                sb.add(x, 0, z, (B(1) && rnd() < 0.3f) ? B(1) : B(0));
                if (d.hMax >= 2 && (k == 0 || (k <= 4 && rnd() < 0.5f)))
                    sb.add(x, 1, z, (B(1) && rnd() < 0.45f) ? B(1) : B(0));
            }
        }
        else if (r == "pebble") {
            int n = rint(1, 2);
            auto at = pickRing(n);
            for (int k : at) sb.add(RING[k][0], 0, RING[k][1], B(0));
        }
        // ── Larger decorations ────────────────────────────
        else if (r == "bush") {
            float rad = d.size >= 2 ? 2.8f + rnd() * 1.2f : 1.6f + rnd() * 1.0f;
            float hgt = d.size >= 2 ? 3.0f + rnd() * 1.5f : 1.8f + rnd() * 1.0f;
            int R = (int)std::ceil(rad) + 1;
            float ox = rnd() * 100.0f;
            for (int y = 0; y <= (int)hgt + 1; y++)
            for (int z = -R; z <= R; z++)
            for (int x = -R; x <= R; x++) {
                float dy = (y - hgt * 0.45f) / (hgt * 0.6f);
                float dd = std::sqrt((x * x + z * z) / (rad * rad) + dy * dy) +
                           (valueNoise(var, x + ox, z + y * 3.0f, 1.3f, 5) - 0.5f) * 0.5f;
                if (dd < 1.0f) sb.add(x, y, z, (B(1) && dd > 0.7f && rnd() < 0.18f) ? B(1) : B(0));
            }
            if (d.size >= 2) sb.add(0, 0, 0, BLOCK_LOG_Y);
        }
        else if (r == "log") {
            bool alongX = (var & 1) == 0;
            int len = rint(6, 13);
            uint16_t id = alongX ? B(0) : (B(1) ? B(1) : B(0));
            bool thick = rnd() < 0.5f;
            for (int i = -len / 2; i <= len / 2; i++)
                for (int a = 0; a <= (thick ? 1 : 0); a++)
                    for (int y = 0; y <= (thick ? 1 : 0); y++)
                        sb.add(alongX ? i : a, y, alongX ? a : i, id);
            if (B(2) && rnd() < 0.5f)
                for (int k = 0; k < 4; k++) {
                    int i = len / 2 - rint(0, 2), a = rint(-1, 1);
                    sb.add(alongX ? i : a, thick ? 2 : 1, alongX ? a : i, B(2));
                }
        }
        else if (r == "stones") {
            int count = rint(3, 6);
            for (int k = 0; k < count; k++) {
                int x = rint(-2, 2), z = rint(-2, 2);
                sb.add(x, -1, z, B(0));
                sb.add(x, 0, z, B(0));
                if (rnd() < 0.35f) sb.add(x, 1, z, B(1) ? B(1) : B(0));
            }
        }
        else if (r == "thorn") {
            int branches = rint(3, 5);
            for (int b = 0; b < branches; b++) {
                float a = rnd() * 6.283f, x = 0, z = 0, y = 0;
                int len = rint(3, 6);
                for (int k = 0; k < len; k++) {
                    x += std::cos(a) * 0.7f; z += std::sin(a) * 0.7f; y += 0.6f + rnd() * 0.3f;
                    a += (rnd() - 0.5f) * 0.9f;
                    sb.add((int)std::round(x), (int)y, (int)std::round(z), B(0));
                    if (B(1) && rnd() < 0.15f) sb.add((int)std::round(x), (int)y + 1, (int)std::round(z), B(1));
                }
            }
            sb.add(0, 0, 0, B(0));
        }
        else if (r == "shard") {
            int n = rint(1, 3);
            for (int k = 0; k < n; k++) {
                int x = k == 0 ? 0 : rint(-2, 2), z = k == 0 ? 0 : rint(-2, 2);
                int hh = k == 0 ? rint(d.hMin, d.hMax) : rint(2, std::max(2, d.hMin));
                int lean = rnd() < 0.5f ? 1 : -1;
                bool alongX = rnd() < 0.5f;
                for (int y = -1; y < hh; y++) {
                    int off = (y > hh / 2) ? lean : 0;
                    sb.add(x + (alongX ? off : 0), y, z + (alongX ? 0 : off), B(0));
                    if (y < hh / 2) sb.add(x + (alongX ? 0 : 1), y, z + (alongX ? 1 : 0), B(0));
                }
            }
        }
        else if (r == "vent") {
            // a glowing crack in the ground, crusted at the edges
            float a = rnd() * 3.1416f;
            int len = rint(4, 8);
            for (int i = -len / 2; i <= len / 2; i++) {
                int x = (int)std::round(std::cos(a) * i), z = (int)std::round(std::sin(a) * i + std::sin(i * 1.3f) * 0.6f);
                sb.add(x, -1, z, B(1), true);
                sb.add(x, -2, z, B(1), true);
                for (int q = 1; q <= 4; q++) {
                    int ex = x + RING[q][0], ez = z + RING[q][1];
                    if (sb.has(ex, -1, ez)) continue;
                    if (B(2) && rnd() < 0.4f) sb.add(ex, -1, ez, B(2), true);
                    else if (rnd() < 0.3f) sb.add(ex, 0, ez, B(0));
                }
            }
        }
        // ── Landmarks ─────────────────────────────────────
        else if (r == "willow")          willow(sb, d, rnd);
        else if (r == "twisted_oak" || r == "snag") twistedOak(sb, d, rnd);
        else if (r == "birch")           birch(sb, d, rnd);
        else if (r == "pillar")          pillar(sb, d, rnd);
        else if (r == "hoodoo")          hoodoo(sb, d, rnd);
        else if (r == "fumarole")        fumarole(sb, d, rnd);
        else if (r == "maple")           maple(sb, d, rnd);
        else if (r == "cypress")         cypress(sb, d, rnd);
        else if (r == "mangrove")        mangrove(sb, d, rnd);
        else if (r == "standing_stones") standingStones(sb, d, rnd, rint);
        else if (r == "stone_circle")    stoneCircle(sb, d, rnd, rint);
        else if (r == "cairn")           cairn(sb, d, rnd);
        else {
            std::cerr << "decorations.toml: " << d.name << ": unknown recipe '" << r << "'\n";
            sb.add(0, 0, 0, B(0) ? B(0) : BLOCK_STONE);
        }
        return sb.finish();
    }

private:
    // A weeping willow: a stout trunk, arching boughs, a domed
    // crown, and curtains of leaves that hang almost to the ground
    template <class RndF>
    static void willow(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t wood = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_LOG_Y;
        uint16_t leaf = d.blocks.size() > 1 ? d.blocks[1] : BLOCK_LEAVES;
        float S = (std::max(1, d.size) * 0.5f + 0.5f) * d.scale;   // size 1 ≈ 30 ft wide
        int trunkH = (int)((9 + rnd() * 3) * S);
        // trunk with root flare, sunk into the ground
        for (int y = -3; y < trunkH; y++) {
            float rr = y < 2 ? 2.4f - y * 0.25f : (y < trunkH - 3 ? 1.6f : 1.2f);
            int R = (int)std::ceil(rr);
            for (int z = -R; z <= R; z++)
                for (int x = -R; x <= R; x++)
                    if (x * x + z * z <= rr * rr) sb.add(x, y, z, wood);
        }
        // boughs: out and up from the top of the trunk
        int boughs = 5 + (int)(rnd() * 3);
        float reach = (7.0f + rnd() * 2.0f) * S, rise = (5.0f + rnd() * 2.0f) * S;
        for (int b = 0; b < boughs; b++) {
            float a = b * 6.283f / boughs + rnd() * 0.6f;
            float len = reach * (0.75f + rnd() * 0.3f);
            for (float t = 0; t <= 1.0f; t += 0.04f) {
                float x = std::cos(a) * len * t, z = std::sin(a) * len * t;
                float y = trunkH - 1 + rise * std::sin(t * 1.5708f);
                sb.add((int)std::round(x), (int)std::round(y), (int)std::round(z), wood);
                if (t < 0.35f) sb.add((int)std::round(x), (int)std::round(y) + 1, (int)std::round(z), wood);
            }
        }
        // crown: a squashed dome shell
        float Rc = reach + 2.0f, Hc = rise + 2.5f;
        int RI = (int)std::ceil(Rc) + 1;
        float cy = trunkH + 1.0f;
        for (int y = (int)cy; y <= (int)(cy + Hc + 1); y++)
        for (int z = -RI; z <= RI; z++)
        for (int x = -RI; x <= RI; x++) {
            float q = std::sqrt((x * x + z * z) / (Rc * Rc) + ((y - cy) * (y - cy)) / (Hc * Hc));
            float jag = (valueNoise(7, x * 1.0f, z + y * 2.0f, 2.2f, 9) - 0.5f) * 0.25f;
            if (q + jag < 1.0f && q + jag > 0.72f && rnd() < 0.8f) sb.add(x, y, z, leaf);
        }
        // curtains: thin strands hanging from the crown's outer part,
        // in uneven skirts with gaps, longest at the rim
        for (int z = -RI; z <= RI; z++)
        for (int x = -RI; x <= RI; x++) {
            float rr = std::sqrt((float)(x * x + z * z));
            if (rr < Rc * 0.45f || rr > Rc) continue;
            if (((x + z) & 1) && rnd() < 0.7f) continue;      // stringy, not a wall
            float ang = std::atan2((float)z, (float)x);
            float skirt = valueNoise(3, ang * 4.0f, 0.0f, 1.0f, 21);   // 0..1 around the tree
            if (rnd() > 0.25f + 0.35f * skirt) continue;
            int y0 = -1;
            for (int y = (int)cy; y <= (int)(cy + Hc + 1); y++) if (sb.has(x, y, z)) { y0 = y; break; }
            if (y0 < 0) continue;
            float outer = (rr - Rc * 0.45f) / (Rc * 0.55f);
            int len = (int)((y0 - 3) * (0.25f + 0.6f * outer) * (0.45f + 0.55f * skirt) * (0.7f + rnd() * 0.3f));
            for (int k = 1; k <= len; k++) sb.add(x, y0 - k, z, leaf);
        }
    }

    // ── Tree helpers ──────────────────────────────────────
    // A round limb between two points, tapering from r0 to r1
    static void limb(ShapeBuilder& sb, uint16_t wood, float ax, float ay, float az,
                     float bx, float by, float bz, float r0, float r1) {
        float len = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay) + (bz - az) * (bz - az));
        int steps = std::max(1, (int)(len * 2.0f));
        for (int k = 0; k <= steps; k++) {
            float t = (float)k / steps;
            float x = ax + (bx - ax) * t, y = ay + (by - ay) * t, z = az + (bz - az) * t;
            float r = r0 + (r1 - r0) * t;
            int R = (int)std::ceil(r);
            for (int dz = -R; dz <= R; dz++) for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++)
                if (dx * dx + dy * dy + dz * dz <= r * r + 0.3f)
                    sb.add((int)std::round(x) + dx, (int)std::round(y) + dy, (int)std::round(z) + dz, wood);
        }
    }
    // A lumpy clump of leaves; only fills where there's no wood
    template <class RndF>
    static void clump(ShapeBuilder& sb, const std::vector<uint16_t>& leaves, float cx, float cy, float cz,
                      float rx, float ry, RndF& rnd, float fill = 0.85f) {
        uint16_t leaf = leaves[(size_t)(rnd() * leaves.size()) % leaves.size()];
        int RX = (int)std::ceil(rx) + 1, RY = (int)std::ceil(ry) + 1;
        float o = rnd() * 50.0f;
        for (int y = -RY; y <= RY; y++) for (int z = -RX; z <= RX; z++) for (int x = -RX; x <= RX; x++) {
            float q = std::sqrt((x * x + z * z) / (rx * rx) + (y * y) / (ry * ry));
            float jag = (valueNoise(11, x + cx + o, z + cz + y * 1.7f, 1.6f, 4) - 0.5f) * 0.45f;
            if (q + jag < 1.0f && (q > 0.55f || rnd() < 0.5f) && rnd() < fill) {
                int px = (int)std::round(cx) + x, py = (int)std::round(cy) + y, pz = (int)std::round(cz) + z;
                if (!sb.has(px, py, pz)) sb.add(px, py, pz, leaf);
            }
        }
    }
    // Strands hanging from the underside of the leaves (moss, vines)
    template <class RndF>
    static void drape(ShapeBuilder& sb, uint16_t strand, int R, int yLo, int yHi, float chance, float maxLen, RndF& rnd) {
        if (!strand) return;
        for (int z = -R; z <= R; z++) for (int x = -R; x <= R; x++) {
            if (rnd() > chance) continue;
            int y0 = -1;
            for (int y = yLo; y <= yHi; y++) if (sb.has(x, y, z)) { y0 = y; break; }
            if (y0 < 2) continue;
            int len = (int)(maxLen * (0.3f + 0.7f * rnd()));
            for (int k = 1; k <= len && y0 - k > 1; k++) if (!sb.has(x, y0 - k, z)) sb.add(x, y0 - k, z, strand);
        }
    }

    // A twisted oak: the trunk wanders as it rises, then splits
    // into crooked limbs that end in heavy, lumpy clumps of leaves
    template <class RndF>
    static void twistedOak(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t wood = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_LOG_Y;
        std::vector<uint16_t> leaves{ d.blocks.size() > 1 ? d.blocks[1] : BLOCK_LEAVES };
        uint16_t strand = d.blocks.size() > 2 ? d.blocks[2] : 0;
        float S = (std::max(1, d.size) * 0.4f + 0.6f) * d.scale;
        int H = (int)((10 + rnd() * 4) * S * (1.0f + (1.0f - d.twist) * 0.35f));
        float x = 0, z = 0, lean = rnd() * 6.283f, px = 0, pz = 0;
        for (int y = -2; y < H; y++) {             // the trunk, drifting and thinning
            lean += (rnd() - 0.5f) * 0.5f;
            if (y > 2) { x += std::cos(lean) * 0.5f * d.twist; z += std::sin(lean) * 0.5f * d.twist; }
            float r = (y < 2 ? 2.6f - y * 0.3f : 1.9f - 0.6f * y / H) * S * 0.85f;
            limb(sb, wood, px, (float)y, pz, x, (float)y + 1, z, r, r);
            px = x; pz = z;
        }
        int limbs = 4 + (int)(rnd() * 3);
        float spread = 9.0f * S;
        for (int b = 0; b < limbs; b++) {
            float a = b * 6.283f / limbs + rnd() * 0.8f;
            // limbs leave the trunk at different heights, low ones reach far
            float ly = (float)H * (0.55f + 0.45f * rnd()), lx = x * ly / H, lz = z * ly / H;
            float dir = a, len = spread * (0.6f + rnd() * 0.6f) * (1.25f - 0.4f * ly / H);
            int segs = 3;
            float r = 1.4f * S;
            for (int k = 0; k < segs; k++) {           // crooked: three kinked pieces
                dir += (rnd() - 0.5f) * 1.2f * d.twist;
                float seg = len / segs;
                float nx = lx + std::cos(dir) * seg, nz = lz + std::sin(dir) * seg;
                float up = (1.0f - d.twist) * 0.45f;           // straighter trees reach up more
                float ny = ly + seg * (k == 0 ? 0.35f + up + rnd() * 0.4f : -0.1f + up + rnd() * 0.35f);
                limb(sb, wood, lx, ly, lz, nx, ny, nz, std::max(r, 0.9f), std::max(r * 0.7f, 0.75f));   // never thinner than a voxel
                r *= 0.7f;
                lx = nx; ly = ny; lz = nz;
                if (k >= 1 && d.recipe != "snag") clump(sb, leaves, lx, ly + 1.0f, lz, (3.0f + rnd() * 1.5f) * S, (2.0f + rnd()) * S, rnd);
            }
        }
        if (d.recipe == "snag") return;          // bare: a dead, charred tree
        clump(sb, leaves, x, (float)H + 2.0f * S, z, 4.5f * S, 2.8f * S, rnd);   // crown over the fork
        drape(sb, strand, (int)(spread * 1.3f + 6 * S), 0, H + (int)(14 * S), 0.12f, 6.0f * S, rnd);
    }

    // A birch or aspen: a slender pale trunk with a slight lean, short
    // branches reaching up, and a tall narrow crown of small clumps
    template <class RndF>
    static void birch(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t wood = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_LOG_Y;
        std::vector<uint16_t> leaves;
        for (size_t i = 1; i < d.blocks.size(); i++) leaves.push_back(d.blocks[i]);
        if (leaves.empty()) leaves.push_back(BLOCK_LEAVES);
        float S = (std::max(1, d.size) * 0.4f + 0.6f) * d.scale;
        int H = (int)((14 + rnd() * 5) * S);
        float lean = rnd() * 6.283f, lx = std::cos(lean) * 0.12f, lz = std::sin(lean) * 0.12f;
        limb(sb, wood, 0, -2, 0, lx * H, (float)H, lz * H, 0.95f * S, 0.55f * S);
        int branches = 5 + (int)(rnd() * 4);
        for (int b = 0; b < branches; b++) {
            float t = 0.4f + 0.55f * b / std::max(1, branches - 1);
            float y = H * t, a = rnd() * 6.283f, len = (3.0f + rnd() * 2.5f) * S * (1.2f - t * 0.5f);
            float bx = lx * y, bz = lz * y;
            limb(sb, wood, bx, y, bz, bx + std::cos(a) * len, y + len * 0.8f, bz + std::sin(a) * len, 0.45f * S, 0.3f);
            clump(sb, leaves, bx + std::cos(a) * len, y + len * 0.9f, bz + std::sin(a) * len, (2.2f + rnd()) * S, (2.4f + rnd()) * S, rnd, 0.85f);
        }
        clump(sb, leaves, lx * H, H + 1.5f * S, lz * H, 2.6f * S, 3.2f * S, rnd, 0.85f);
    }

    // A karst pillar: a tall, fissured column of limestone, slightly
    // waisted, with moss and a bush on its top
    template <class RndF>
    static void pillar(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t rock = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_STONE;
        uint16_t cap  = d.blocks.size() > 1 ? d.blocks[1] : BLOCK_MOSS_STONE;
        uint16_t leaf = d.blocks.size() > 2 ? d.blocks[2] : 0;
        float S = std::max(1, d.size) * d.scale;
        int H = (int)((d.hMin + rnd() * (d.hMax - d.hMin)) * S);
        float R0 = (4.0f + rnd() * 2.6f) * S, o = rnd() * 100.0f;
        int RI = (int)std::ceil(R0 * 1.4f) + 1;
        float lean = (rnd() - 0.5f) * 0.12f, la = rnd() * 6.283f;
        int top = -1;
        for (int y = -4; y < H; y++) {
            float t = (float)std::max(0, y) / H;
            float r = R0 * (1.15f - 0.35f * std::sin(t * 3.1416f) * 0.6f - 0.2f * t);   // waisted, narrowing
            float cx = std::cos(la) * lean * y, cz = std::sin(la) * lean * y;
            for (int z = -RI; z <= RI; z++) for (int x = -RI; x <= RI; x++) {
                float a = std::atan2(z - cz, x - cx), dd = std::hypot(x - cx, z - cz);
                float wob = 1.0f + 0.22f * (valueNoise(5, a * 3.0f + o, y * 0.15f, 1.0f, 3) - 0.5f) * 2.0f;
                float fissure = std::fabs(std::sin(a * 3.0f + o)) < 0.12f ? 0.75f : 1.0f;      // vertical cracks
                if (dd <= r * wob * fissure) { sb.add(x, y, z, rock); top = std::max(top, y); }
            }
        }
        // mossy cap and a bush clinging on top
        for (int z = -RI; z <= RI; z++) for (int x = -RI; x <= RI; x++)
            for (int y = top; y >= top - 1; y--) if (sb.has(x, y, z) && !sb.has(x, y + 1, z)) { sb.add(x, y + 1, z, cap); break; }
        if (leaf) clump(sb, std::vector<uint16_t>{ leaf }, std::cos(la) * lean * H, top + 2.5f, std::sin(la) * lean * H, R0 * 0.7f, 2.2f, rnd, 0.9f);
    }

    // A hoodoo: a spire of banded sandstone with a narrow neck under a
    // wider, darker caprock
    template <class RndF>
    static void hoodoo(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        float S = std::max(1, d.size) * d.scale;
        int H = (int)((d.hMin + rnd() * (d.hMax - d.hMin)) * S);
        float R0 = (3.4f + rnd() * 1.8f) * S;
        int RI = (int)std::ceil(R0 * 1.6f) + 1;
        size_t nb = d.blocks.size() > 1 ? d.blocks.size() - 1 : 1;
        uint16_t capB = d.blocks.empty() ? BLOCK_STONE : d.blocks.back();
        int bandOff = (int)(rnd() * 9);
        for (int y = -3; y < H; y++) {
            float t = (float)std::max(0, y) / H;
            float r = R0 * (1.0f - 0.25f * t);
            if (t > 0.72f && t < 0.86f) r *= 0.6f;          // the neck
            bool cap = t >= 0.86f;
            if (cap) r = R0 * 1.25f;
            uint16_t id = cap ? capB : d.blocks[((y + bandOff) / 3 % (int)nb + nb) % nb];
            for (int z = -RI; z <= RI; z++) for (int x = -RI; x <= RI; x++) {
                float wob = 1.0f + 0.15f * (valueNoise(9, x * 0.7f + y * 0.3f, z * 0.7f, 1.0f, 2) - 0.5f) * 2.0f;
                if (x * x + z * z <= r * r * wob * wob) sb.add(x, y, z, id);
            }
        }
    }

    // A fumarole: a low crusted mound around a glowing vent
    template <class RndF>
    static void fumarole(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t crust = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_GRAVEL;
        uint16_t glow  = d.blocks.size() > 1 ? d.blocks[1] : BLOCK_LAVA;
        uint16_t ring  = d.blocks.size() > 2 ? d.blocks[2] : crust;
        float R = 3.0f + rnd() * 2.0f;
        int RI = (int)std::ceil(R) + 1;
        for (int z = -RI; z <= RI; z++) for (int x = -RI; x <= RI; x++) {
            float dd = std::hypot((float)x, (float)z);
            if (dd > R) continue;
            int hgt = (int)std::round((1.0f - dd / R) * 3.0f);
            uint16_t id = dd < 1.2f ? glow : (dd > R - 1.2f && rnd() < 0.6f ? ring : crust);
            sb.add(x, -1, z, id, true);
            for (int y = 0; y < hgt; y++) if (dd >= 1.2f) sb.add(x, y, z, id);
        }
        sb.add(0, -2, 0, glow, true);
    }

    // A maple: a straight trunk forking into a vase of limbs under
    // a tall, round, dense crown — patchy with autumn colours
    template <class RndF>
    static void maple(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t wood = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_LOG_Y;
        std::vector<uint16_t> leaves;
        for (size_t i = 1; i < d.blocks.size(); i++) leaves.push_back(d.blocks[i]);
        if (leaves.empty()) leaves.push_back(BLOCK_LEAVES);
        float S = (std::max(1, d.size) * 0.4f + 0.6f) * d.scale;
        int H = (int)((10 + rnd() * 3) * S);
        limb(sb, wood, 0, -2, 0, 0, 2, 0, 2.0f * S, 1.4f * S);
        limb(sb, wood, 0, 2, 0, (rnd() - 0.5f), (float)H, (rnd() - 0.5f), 1.4f * S, 1.0f * S);
        float crownY = H + 7.0f * S, crownR = 7.0f * S, crownH = 9.0f * S;
        int limbs = 4 + (int)(rnd() * 3);
        for (int b = 0; b < limbs; b++) {
            float a = b * 6.283f / limbs + rnd() * 0.5f;
            float ex = std::cos(a) * crownR * 0.6f, ez = std::sin(a) * crownR * 0.6f;
            float ey = crownY - crownH * 0.2f + rnd() * 3.0f;
            limb(sb, wood, 0, (float)H - 1, 0, ex, ey, ez, 0.9f * S, 0.5f);
        }
        // the crown: overlapping clumps filling an upright oval
        int clumps = (int)(14 * S);
        for (int k = 0; k < clumps; k++) {
            float a = rnd() * 6.283f, rr = std::sqrt(rnd()) * crownR * 0.75f;
            float y = crownY + (rnd() - 0.45f) * crownH * 0.9f;
            float shrink = std::sqrt(std::max(0.2f, 1.0f - std::pow((y - crownY) / (crownH * 0.75f), 2.0f)));
            clump(sb, leaves, std::cos(a) * rr * shrink, y, std::sin(a) * rr * shrink,
                  (3.2f + rnd() * 1.6f) * S, (2.6f + rnd() * 1.2f) * S, rnd, 0.9f);
        }
    }

    // A bald cypress: a flared, buttressed foot standing in the wet,
    // knees poking up around it, a tall trunk and flat tiers of
    // feathery leaves, hung with moss
    template <class RndF>
    static void cypress(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t wood = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_LOG_Y;
        std::vector<uint16_t> leaves{ d.blocks.size() > 1 ? d.blocks[1] : BLOCK_LEAVES };
        uint16_t strand = d.blocks.size() > 2 ? d.blocks[2] : 0;
        float S = (std::max(1, d.size) * 0.4f + 0.6f) * d.scale;
        int H = (int)((20 + rnd() * 8) * S);
        for (int y = -3; y < H; y++) {
            float flare = y < 7 ? (7 - y) / 7.0f : 0.0f;
            float r = (1.4f + 3.2f * flare * flare) * S;
            int R = (int)std::ceil(r) + 1;
            for (int z = -R; z <= R; z++) for (int x = -R; x <= R; x++) {
                float a = std::atan2((float)z, (float)x);
                float ridge = 1.0f + 0.35f * flare * std::cos(a * 5.0f);    // buttresses
                if (x * x + z * z <= r * r * ridge * ridge) sb.add(x, y, z, wood);
            }
        }
        // knees around the foot
        int knees = 4 + (int)(rnd() * 5);
        for (int k = 0; k < knees; k++) {
            float a = rnd() * 6.283f, rr = (5.0f + rnd() * 4.0f) * S;
            int kx = (int)std::round(std::cos(a) * rr), kz = (int)std::round(std::sin(a) * rr);
            int kh = 1 + (int)(rnd() * 3);
            for (int y = -2; y < kh; y++) sb.add(kx, y, kz, wood);
        }
        // tiers: short branches carrying flat pads of leaves
        int tiers = 4 + (int)(rnd() * 3);
        for (int t = 0; t < tiers; t++) {
            float y = H * (0.45f + 0.55f * t / std::max(1, tiers - 1));
            int arms = 3 + (int)(rnd() * 2);
            float reach = (6.5f - t * 0.6f) * S;
            for (int a = 0; a < arms; a++) {
                float ang = a * 6.283f / arms + rnd() * 0.9f + t;
                float ex = std::cos(ang) * reach, ez = std::sin(ang) * reach;
                limb(sb, wood, 0, y, 0, ex, y + 1.0f, ez, 0.6f * S, 0.4f);
                clump(sb, leaves, ex * 0.8f, y + 1.5f, ez * 0.8f, (3.6f + rnd()) * S, 1.3f * S, rnd, 0.9f);
            }
        }
        clump(sb, leaves, 0, H + 1.0f, 0, 3.0f * S, 1.8f * S, rnd);
        drape(sb, strand, (int)(8 * S), (int)(H * 0.4f), H + 4, 0.10f, 7.0f * S, rnd);
    }

    // A mangrove: a trunk raised on arching stilt roots, a broad
    // uneven crown, aerial roots and moss hanging from it
    template <class RndF>
    static void mangrove(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t wood = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_LOG_Y;
        std::vector<uint16_t> leaves{ d.blocks.size() > 1 ? d.blocks[1] : BLOCK_LEAVES };
        uint16_t strand = d.blocks.size() > 2 ? d.blocks[2] : 0;
        float S = (std::max(1, d.size) * 0.4f + 0.6f) * d.scale;
        int lift = (int)(5 * S), H = lift + (int)((7 + rnd() * 4) * S);
        int roots = 6 + (int)(rnd() * 4);
        for (int k = 0; k < roots; k++) {           // stilts: out and down in an arch
            float a = k * 6.283f / roots + rnd() * 0.5f, rr = (5.0f + rnd() * 3.0f) * S;
            float px = 0, py = (float)lift + 1, pz = 0;
            for (int q = 1; q <= 8; q++) {
                float t = q / 8.0f;
                float nx = std::cos(a) * rr * t, nz = std::sin(a) * rr * t;
                float ny = (lift + 1) * (1.0f - t * t) - 2.0f * t;
                limb(sb, wood, px, py, pz, nx, ny, nz, 0.6f * S, 0.5f * S);
                px = nx; py = ny; pz = nz;
            }
        }
        limb(sb, wood, 0, (float)lift, 0, 0, (float)H, 0, 1.3f * S, 1.0f * S);
        int limbs = 4 + (int)(rnd() * 3);
        float spread = 8.0f * S;
        for (int b = 0; b < limbs; b++) {
            float a = b * 6.283f / limbs + rnd() * 0.7f;
            float ex = std::cos(a) * spread * (0.6f + rnd() * 0.4f), ez = std::sin(a) * spread * (0.6f + rnd() * 0.4f);
            float ey = H + 1.0f + rnd() * 3.0f;
            limb(sb, wood, 0, (float)H - 2, 0, ex, ey, ez, 0.8f * S, 0.5f);
            clump(sb, leaves, ex, ey + 1.0f, ez, (3.8f + rnd() * 1.5f) * S, (2.2f + rnd()) * S, rnd);
            // an aerial root dropping from the limb
            if (rnd() < 0.6f) for (int y = (int)ey - 1; y > -1; y--) sb.add((int)std::round(ex * 0.7f), y, (int)std::round(ez * 0.7f), wood);
        }
        clump(sb, leaves, 0, H + 3.0f, 0, 4.5f * S, 2.5f * S, rnd);
        drape(sb, strand, (int)(spread + 5 * S), lift, H + 10, 0.10f, 5.0f * S, rnd);
    }

    template <class R, class I>
    static void slab(ShapeBuilder& sb, uint16_t stone, uint16_t top, int cx, int cz, int w, int t, int h,
                     bool alongX, R& rnd, I& rint) {
        int lean = rnd() < 0.3f ? (rnd() < 0.5f ? 1 : -1) : 0;
        for (int y = -4; y < h; y++)
            for (int a = 0; a < w; a++)
                for (int b = 0; b < t; b++) {
                    if (y == h - 1 && (a == 0 || a == w - 1) && rnd() < 0.5f) continue;   // worn corners
                    int off = (lean && y > h * 2 / 3) ? lean : 0;
                    int x = cx + (alongX ? a - w / 2 : b - t / 2 + off);
                    int z = cz + (alongX ? b - t / 2 + off : a - w / 2);
                    sb.add(x, y, z, (top && y >= h - 3 && rnd() < 0.45f) ? top : stone);
                }
        (void)rint;
    }

    template <class R, class I>
    static void standingStones(ShapeBuilder& sb, const Def& d, R& rnd, I& rint) {
        uint16_t stone = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_STONE;
        uint16_t top   = d.blocks.size() > 1 ? d.blocks[1] : 0;
        int n = rint(3, 5);
        float a = rnd() * 3.1416f, bend = (rnd() - 0.5f) * 0.5f;
        std::vector<std::pair<int,int>> spots;
        for (int k = 0; k < n; k++) {
            float t = (k - (n - 1) * 0.5f) * 5.5f;
            float aa = a + bend * k;
            int x = (int)std::round(std::cos(aa) * t), z = (int)std::round(std::sin(aa) * t);
            spots.push_back({ x, z });
            int h = rint(d.hMin, d.hMax);
            slab(sb, stone, top, x, z, rint(2, 3), rint(1, 2), h, std::fabs(std::cos(a)) > 0.7f, rnd, rint);
        }
        // sometimes a lintel across the tallest pair
        if (d.size >= 2 && n >= 2) {
            auto [x0, z0] = spots[0]; auto [x1, z1] = spots[1];
            int y = d.hMin;
            for (float t = 0; t <= 1.0f; t += 0.1f)
                for (int dy = 0; dy < 2; dy++)
                    sb.add((int)std::round(x0 + (x1 - x0) * t), y + dy, (int)std::round(z0 + (z1 - z0) * t), stone);
        }
    }

    template <class RndF, class I>
    static void stoneCircle(ShapeBuilder& sb, const Def& d, RndF& rnd, I& rint) {
        uint16_t stone = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_STONE;
        uint16_t top   = d.blocks.size() > 1 ? d.blocks[1] : 0;
        int n = rint(8, 11);
        float R = 9.0f + rnd() * 3.0f;
        int fallen = rint(0, n - 1);
        for (int k = 0; k < n; k++) {
            float a = k * 6.283f / n + (rnd() - 0.5f) * 0.15f;
            int x = (int)std::round(std::cos(a) * R), z = (int)std::round(std::sin(a) * R);
            if (k == fallen) {                       // one has toppled outward
                for (int t = 0; t < d.hMin; t++)
                    for (int w = 0; w < 2; w++)
                        sb.add(x + (int)std::round(std::cos(a) * t) + w, 0, z + (int)std::round(std::sin(a) * t), top ? top : stone);
                continue;
            }
            slab(sb, stone, top, x, z, 2, rint(1, 2), rint(d.hMin, d.hMax), std::fabs(std::sin(a)) > 0.7f, rnd, rint);
        }
        // a low flat stone at the centre
        for (int z = -1; z <= 1; z++)
            for (int x = -2; x <= 2; x++) { sb.add(x, -1, z, stone); sb.add(x, 0, z, stone); }
    }

    template <class RndF>
    static void cairn(ShapeBuilder& sb, const Def& d, RndF& rnd) {
        uint16_t body = d.blocks.size() > 0 ? d.blocks[0] : BLOCK_COBBLESTONE;
        uint16_t cap  = d.blocks.size() > 1 ? d.blocks[1] : BLOCK_STONE;
        float R = 3.0f + d.size * 0.6f, H = (float)(d.hMin + (int)(rnd() * (d.hMax - d.hMin + 1)));
        int RI = (int)std::ceil(R) + 1;
        for (int y = -2; y < (int)H; y++)
            for (int z = -RI; z <= RI; z++)
                for (int x = -RI; x <= RI; x++) {
                    float rr = R * (1.0f - std::max(0, y) / H) + (rnd() - 0.5f) * 0.8f;
                    if (x * x + z * z <= rr * rr) sb.add(x, y, z, body);
                }
        sb.add(0, (int)H, 0, cap);
        sb.add(0, (int)H + 1, 0, cap);
    }
};

// ── The registry ──────────────────────────────────────────
struct Set {
    std::vector<Def> defs;
    // per biome: (def index, density) for each layer
    std::array<std::vector<std::pair<int, float>>, BIOMES> byBiome[5];
    int spawnLandmark = -1;
    int maxLargeRadius = 0, maxLandmarkRadius = 0;

    int find(const std::string& n) const {
        for (size_t i = 0; i < defs.size(); i++) if (defs[i].name == n) return (int)i;
        return -1;
    }
    const Def* get(int i) const { return (i >= 0 && i < (int)defs.size()) ? &defs[(size_t)i] : nullptr; }
    const std::vector<std::pair<int, float>>& list(Layer l, int biome) const {
        static const std::vector<std::pair<int, float>> none;
        return (biome >= 0 && biome < BIOMES) ? byBiome[(int)l][(size_t)biome] : none;
    }

    // Which plant a set of plant blocks belongs to (the blocks of a
    // plant that was just broken): the plant whose block list holds
    // all of them and is best covered by them. -1 if none.
    int plantFromBlocks(const std::vector<uint16_t>& ids) const {
        int best = -1; float bestScore = -1;
        for (size_t i = 0; i < defs.size(); i++) {
            const Def& d = defs[i];
            if (d.layer != Layer::Plant) continue;
            bool all = true; int hit = 0;
            for (uint16_t id : ids) {
                if (std::find(d.blocks.begin(), d.blocks.end(), id) == d.blocks.end()) { all = false; break; }
            }
            if (!all) continue;
            for (uint16_t b : d.blocks) if (std::find(ids.begin(), ids.end(), b) != ids.end()) hit++;
            float score = d.blocks.empty() ? 0 : (float)hit / (float)d.blocks.size();
            if (score > bestScore) { bestScore = score; best = (int)i; }
        }
        return best;
    }
};

class Loader {
public:
    static std::shared_ptr<const Set> load(const VoxelRegistry& reg, const char* path) {
        auto set = std::make_shared<Set>();
        try {
            auto tbl = toml::parse_file(path);
            read(*set, reg, tbl, "plant",    Layer::Plant);
            read(*set, reg, tbl, "decor",    Layer::Large);
            read(*set, reg, tbl, "ground",   Layer::Ground);
            read(*set, reg, tbl, "landmark", Layer::Landmark);
            read(*set, reg, tbl, "tree",     Layer::Tree);
        } catch (const toml::parse_error& e) {
            std::cerr << "decorations.toml parse error: " << e << "\n";
        }
        // meadow plants by name, now every plant is known
        for (auto& d : set->defs)
            for (const std::string& n : pendingMeadow()[d.name]) {
                int i = set->find(n);
                if (i >= 0) d.meadowPlants.push_back(i);
                else std::cerr << "decorations.toml: " << d.name << ": meadow plant '" << n << "' not found\n";
            }
        pendingMeadow().clear();
        for (size_t i = 0; i < set->defs.size(); i++) {
            Def& d = set->defs[i];
            int nVar = (d.layer == Layer::Landmark || d.layer == Layer::Tree) ? 3 : d.layer == Layer::Ground ? 0 : 8;
            for (int v = 0; v < nVar; v++) {
                Shape s = Recipes::build(d, v);
                if (d.layer == Layer::Plant) {
                    // a plant must stay inside its 3 × 3 so it fits its square
                    s.v.erase(std::remove_if(s.v.begin(), s.v.end(),
                              [](const Vox& q) { return std::abs(q.dx) > 1 || std::abs(q.dz) > 1; }), s.v.end());
                    s.radius = std::min(s.radius, 1);
                }
                if (d.layer == Layer::Large) set->maxLargeRadius = std::max(set->maxLargeRadius, s.radius);
                if (d.layer == Layer::Landmark) set->maxLandmarkRadius = std::max(set->maxLandmarkRadius, s.radius);
                d.variants.push_back(std::move(s));
            }
            if (d.layer == Layer::Landmark && d.spawn) set->spawnLandmark = (int)i;
            for (int b = 0; b < BIOMES; b++)
                if (d.density[(size_t)b] > 0) set->byBiome[(int)d.layer][(size_t)b].push_back({ (int)i, d.density[(size_t)b] });
        }
        size_t n[5] = {};
        for (auto& d : set->defs) n[(int)d.layer]++;
        std::cout << "Decorations: " << n[0] << " plants, " << n[1] << " decor, " << n[2] << " ground patches, "
                  << n[3] << " landmarks, " << n[4] << " trees from " << path << "\n";
        return set;
    }

private:
    static std::unordered_map<std::string, std::vector<std::string>>& pendingMeadow() {
        static std::unordered_map<std::string, std::vector<std::string>> m;
        return m;
    }
    static std::string prettify(const std::string& s) {
        std::string out; bool up = true;
        for (char c : s) {
            if (c == '_') { out += ' '; up = true; continue; }
            out += up ? (char)std::toupper((unsigned char)c) : c;
            up = false;
        }
        return out;
    }
    static void read(Set& set, const VoxelRegistry& reg, toml::table& tbl, const char* key, Layer layer) {
        auto* arr = tbl[key].as_array();
        if (!arr) return;
        for (auto& e : *arr) {
            auto* t = e.as_table();
            if (!t) continue;
            Def d;
            d.layer  = layer;
            d.name   = (*t)["name"].value_or(std::string("decoration"));
            d.display = (*t)["display"].value_or(prettify(d.name));
            d.recipe = (*t)["recipe"].value_or(std::string(layer == Layer::Ground ? "patch" : "tuft"));
            auto blockId = [&](const std::string& n) -> uint16_t {
                int id = reg.getIDByName(n);
                if (id <= 0) { std::cerr << "decorations.toml: " << d.name << ": unknown block '" << n << "'\n"; return 0; }
                return (uint16_t)id;
            };
            if (auto* b = (*t)["blocks"].as_array())
                for (auto& x : *b) d.blocks.push_back(blockId(x.value_or(std::string())));
            if (auto* h = (*t)["height"].as_array(); h && h->size() >= 2) {
                d.hMin = (int)(*h)[0].value_or((int64_t)1);
                d.hMax = std::max(d.hMin, (int)(*h)[1].value_or((int64_t)1));
            }
            d.size      = (int)(*t)["size"].value_or((int64_t)1);
            d.patch     = (float)(*t)["patch"].value_or(0.0);
            d.patchSize = (float)(*t)["patch_size"].value_or(24.0);
            d.maxSlope  = (float)(*t)["slope"].value_or(1.0);
            d.spawn     = (*t)["spawn"].value_or(false);
            d.scale     = (float)(*t)["scale"].value_or(1.0);
            d.twist     = (float)(*t)["twist"].value_or(1.0);
            d.replaces  = (*t)["replaces"].value_or(std::string());
            d.meadow    = (float)(*t)["meadow"].value_or(0.0);
            std::string w = (*t)["water"].value_or(std::string("dry"));
            d.water = w == "shore" ? Water::Shore : w == "shallow" ? Water::Shallow : w == "surface" ? Water::Surface : Water::Dry;
            if (auto* o = (*t)["on"].as_array())
                for (auto& x : *o) if (uint16_t id = blockId(x.value_or(std::string()))) d.on.push_back(id);
            if (d.on.empty() && d.water != Water::Surface) d.on = { BLOCK_GRASS };
            if (auto* m = (*t)["meadow_plants"].as_array())
                for (auto& x : *m) pendingMeadow()[d.name].push_back(x.value_or(std::string()));
            if (auto* bt = (*t)["biomes"].as_table())
                for (auto& [k, v] : *bt) {
                    std::string bk(k.str());
                    float dens = (float)v.value_or(0.0);
                    bool found = false;
                    for (int b = 0; b < island::BIOME_COUNT; b++)
                        if (bk == "all" || biomeKey(b) == bk) { d.density[(size_t)b] = dens; found = true; }
                    if (!found) std::cerr << "decorations.toml: " << d.name << ": unknown biome '" << bk << "'\n";
                }
            // swatch: the most telling block (a flower's head, a mushroom's cap)
            bool second = d.recipe == "flower" || d.recipe == "willow";
            uint16_t sw = d.blocks.empty() ? 0 : d.blocks[second && d.blocks.size() > 1 ? 1 : 0];
            if (d.recipe == "tuft" || d.recipe == "reed") sw = d.blocks.size() > 1 && d.recipe == "reed" ? d.blocks[1] : sw;
            if (sw) for (int k = 0; k < 3; k++) d.color[k] = reg.get(sw).color[k];
            set.defs.push_back(std::move(d));
        }
    }
};

// The registry in use. Generators take their own copy of the
// pointer when a world opens, so a reload (F6) never changes a
// world that is being generated.
inline std::shared_ptr<const Set>& current() {
    static std::shared_ptr<const Set> s = std::make_shared<Set>();
    return s;
}

// Write a shape into a section (Y-major, Chunk::index) with its base
// at local (lx, ly, lz); only the voxels inside are written.
//   fillWater  water counts as room (reeds standing in a pond)
inline void stamp(uint16_t* vox, const Shape& s, int lx, int ly, int lz, bool fillWater) {
    const int D = VOXEL_DIM;
    for (const Vox& v : s.v) {
        int x = lx + v.dx, y = ly + v.dy, z = lz + v.dz;
        if (x < 0 || x >= D || z < 0 || z >= D || y < 0 || y >= D) continue;
        uint16_t& dst = vox[(size_t)(y * D + x) * D + z];
        if (v.replace) { if (dst != BLOCK_AIR && dst != BLOCK_WATER && dst != BLOCK_LAVA) dst = v.id; }
        else if (dst == BLOCK_AIR || (fillWater && dst == BLOCK_WATER) ||
                 (dst == BLOCK_LEAVES && v.id != BLOCK_LEAVES && s.top > 12)) dst = v.id;
    }
}

} // namespace decor
