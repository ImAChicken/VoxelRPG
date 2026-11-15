#pragma once
#include "IslandChart.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// ══════════════════════════════════════════════════════════
// BiomeTable — what each land and sea biome is
// ══════════════════════════════════════════════════════════
// Used twice:
//   the collapse (BiomeCollapse.h)   which biomes may touch,
//                                    which quarter favours them,
//                                    and the height range a cell
//                                    of it is smoothed into
//   the voxels (IslandGenerator.h)   how the ground rolls inside
//                                    the biome, and how its edge
//                                    meets its neighbours
//
// Height LEVELS (sea level is y = 160, 3 voxels = 1 ft):
//   -2 deep ocean  -1 ocean  0 shallows  1 coast (beach, swamp)
//    2 lowlands    3 uplands 4 highlands 5 low mountain
//    6 mid mountain / plateau  7 high mountain  8 peak
// Two biomes may touch only if their level ranges come within
// one of each other. Sea cliffs span 1–4: the one place the
// coast meets the highlands directly.
namespace island {

// How the ground rolls inside a biome. The relief noise is
// 0..1, evenly spread; the curve reshapes it:
//   Linear   as is — rolling ground
//   Flat     the lower two thirds pressed nearly flat, the top
//            third rising into scattered hills (plains, moor)
//   Ridged   sharp crests from ridged noise (mountains)
//   Plateau  a flat top that breaks off at the edges
//   Hummock  small bumps around a mean, so the low ones sit
//            under water (swamp)
enum class Relief : uint8_t { None, Linear, Flat, Ridged, Plateau, Hummock };

// How a biome's surface meets a neighbour's: the width of the
// ragged band where the two mix, as a fraction of a cell
// (0.25 ≈ 32 voxels either side). 0 = follow height instead
// (beaches: sand wherever the ground is near the water).
struct TileDef {
    uint8_t biome;
    int8_t  lvLo, lvHi;          // height levels
    int16_t hLo, hHi;            // height range, voxels
    float   aff[4];              // theme weight: mountain, plains, volcanic, forest
    float   relief;              // relief height, voxels (peak to trough)
    Relief  curve;
    float   edge;                // mixing band at the edge, fraction of a cell
    const char* note;
};

inline const std::array<TileDef, 25>& tiles() {
    static const std::array<TileDef, 25> T = {{
        { DeepOcean,     -2,-2,  40, 100, {1,1,1,1},          20, Relief::Linear,  0.25f, "Only next to ocean" },
        { Ocean,         -1,-1, 100, 145, {1,1,1,1},          14, Relief::Linear,  0.25f, "" },
        { Shallows,       0, 0, 145, 159, {1,1,0.4f,1},        6, Relief::Linear,  0.25f, "" },
        { RockyShallows,  0, 0, 145, 159, {0.5f,0.05f,2,0.05f},6, Relief::Linear,  0.15f, "Land neighbours must be sea cliffs" },
        { SwampShallows,  0, 0, 148, 159, {1,1,1,1},           4, Relief::Linear,  0.35f, "Land neighbours: swamp, marsh or their beaches" },
        { Beach,          1, 1, 158, 170, {1,2,0.1f,1.5f},     4, Relief::Flat,    0.0f,  "Coast cells only" },
        { SwampBeach,     1, 1, 158, 164, {1,1,1,1},           3, Relief::Hummock, 0.35f, "Near the swamp; sea side must be swamp shallows" },
        { MarshBeach,     1, 1, 158, 166, {1,1,1,1},           4, Relief::Flat,    0.3f,  "Near the swamp" },
        { SeaCliffs,      1, 4, 158, 420, {0.6f,0.02f,3,0.02f},0, Relief::None,    0.1f,  "Coast cells only; spans levels 1-4" },
        { Plains,         2, 2, 164, 205, {0.5f,3,0.05f,0.4f}, 24, Relief::Flat,   0.25f, "" },
        { Grassland,      2, 2, 166, 215, {0.5f,3,0.05f,0.4f}, 30, Relief::Flat,   0.25f, "" },
        { Shrubland,      2, 3, 170, 300, {1,1,0.2f,3},        40, Relief::Linear, 0.3f,  "Height rises across it, plains to uplands" },
        { Forest,         2, 3, 170, 300, {1,0.5f,0.2f,3},     45, Relief::Linear, 0.3f,  "Height rises across it, plains to uplands" },
        { DenseForest,    3, 3, 200, 300, {0.6f,0.2f,0.1f,3},  55, Relief::Linear, 0.3f,  "" },
        { Swamp,          1, 1, 158, 163, {1,1,1,1},            8, Relief::Hummock,0.35f, "Beach height; land neighbours: swamp, marsh, forest, shrubland, beaches" },
        { Marsh,          1, 2, 160, 176, {1,1,1,1},           10, Relief::Hummock,   0.35f, "Near the swamp" },
        { Foothills,      4, 4, 280, 420, {2,1,1,1},           80, Relief::Linear, 0.25f, "" },
        { LowMountain,    5, 5, 400, 560, {1,1,1,1},          140, Relief::Ridged, 0.2f,  "" },
        { MidMountain,    6, 6, 540, 720, {1,1,1,1},          200, Relief::Ridged, 0.2f,  "" },
        { HighMountain,   7, 7, 700, 860, {1,1,1,1},          240, Relief::Ridged, 0.2f,  "" },
        { Plateau,        6, 6, 560, 700, {1,1,1,1},           30, Relief::Plateau,0.2f,  "Flat-topped" },
        { Peak,           8, 8, 850,1000, {1,1,1,1},          200, Relief::Ridged, 0.2f,  "Pinned; only next to high mountain" },
        { Karst,       4, 4, 280, 420, {0.3f,0.02f,3,0.02f},50, Relief::Linear, 0.15f, "" },
        { HighlandMoor,   4, 4, 280, 420, {0.6f,0.02f,3,0.05f},60, Relief::Flat,   0.2f,  "" },
        { Canyons,      3, 3, 200, 300, {0.3f,0.1f,3,0.1f},  40, Relief::Flat,   0.2f,  "" },
    }};
    return T;
}

// Index into tiles() for a biome, or -1 (river, lake, road …)
inline int tileIndex(int biome) {
    static const std::array<int8_t, BIOME_COUNT> idx = [] {
        std::array<int8_t, BIOME_COUNT> a{};
        a.fill(-1);
        for (int i = 0; i < (int)tiles().size(); i++) a[tiles()[i].biome] = (int8_t)i;
        return a;
    }();
    return (biome >= 0 && biome < BIOME_COUNT) ? idx[biome] : -1;
}
inline const TileDef* tileFor(int biome) {
    int i = tileIndex(biome);
    return i >= 0 ? &tiles()[i] : nullptr;
}

inline bool isSeaBiome(int b) {
    return b == DeepOcean || b == Ocean || b == Shallows || b == RockyShallows || b == SwampShallows;
}
inline bool isSwampy(int b) {
    return b == Swamp || b == Marsh || b == SwampBeach || b == MarshBeach || b == SwampShallows;
}

// Relief at a point, in voxels, added to the smooth ground.
//   n  relief noise, 0..1      r  ridged noise, 0..1
// Each curve has its own resting point, so flat biomes only
// rise (their range is their floor) and rolling ones swing
// both ways.
inline float reliefHeight(const TileDef& t, float n, float r) {
    n = std::clamp(n, 0.0f, 1.0f);
    switch (t.curve) {
    case Relief::None:    return 0.0f;
    case Relief::Linear:  return (n - 0.5f) * t.relief;
    case Relief::Flat: {
        // lower 2/3 of the noise → the bottom tenth of the
        // relief; the top third climbs smoothly to the full height
        const float k = 2.0f / 3.0f;
        float q = (n - k) / (1.0f - k);
        float f = n < k ? n / k * 0.1f
                        : 0.1f + 0.9f * q * q * (3.0f - 2.0f * q);   // eases in and out: rounded hills
        return f * t.relief;
    }
    case Relief::Ridged:  return (r - 0.35f) * t.relief;
    case Relief::Plateau: {
        float f = n > 0.3f ? 1.0f - 0.08f * (1.0f - n) : n / 0.3f * 0.92f;
        return (f - 0.85f) * t.relief;
    }
    case Relief::Hummock: return (n - 0.5f) * t.relief;
    }
    return 0.0f;
}

} // namespace island
