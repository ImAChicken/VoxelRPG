#pragma once
#include "IslandChart.h"
#include "BiomeCollapse.h"
#include "BiomeTable.h"
#include "TerrainBake.h"
#include "WorldGenParams.h"
#include <chrono>
#include <string>
#include <iostream>
#include "../chunk/TerrainGenerator.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

// ══════════════════════════════════════════════════════════
// IslandGenerator — the "Island" world type
// ══════════════════════════════════════════════════════════
// Turns an IslandChart (one cell per chunk, made by the biome
// collapse in BiomeCollapse.h) into voxels. The chart decides
// WHAT each chunk is; this decides how it looks up close:
//
//   height     each cell's chart height, blended smoothly
//              between cell centres (Catmull-Rom), then each
//              biome's own relief (BiomeTable.h): plains mostly
//              flat with scattered hills, forest rolling,
//              mountains ridged … blended across the borders.
//              Steep sea cliffs, and valleys cut along the rivers
//   edges      where two biomes meet, their surfaces mix in a
//              ragged band whose width depends on the biomes
//              (wide for swamp and forest, narrow for ash);
//              beaches follow the height of the ground instead
//   coastline  the blend crosses sea level between land and
//              sea cells; a gentle domain warp keeps it from
//              following the chunk grid
//   materials  from the nearest cell's biome (sand on beaches,
//              ash in the volcanic quarter …), bare rock on
//              steep slopes and snow above the snow line
//   water      sea level everywhere below it; lakes at their
//              own level; rivers cut as channels along the
//              chart's river cells
//   roads      gravel along the chart's road, plank bridges
//              where it crosses water
//   features   village plazas, hot spring pools, lava pools,
//              cave mouths
//   trees      density and kind by biome
//
// Heights: sea level 160 (53 ft of ocean below it), the
// summit around 950 — roughly 260 ft above the waves, in a
// world 1024 voxels (341 ft) tall.
// World options (New World screen, saved with the world)
struct IslandOptions {
    std::string bakeCache;      // where to keep the bake (world folder); "" = don't
    WorldGenParams params;      // the New World screen's sliders
    bool erosion      = true;   // bake the ground with erosion (TerrainBake.h)
    bool guidedRivers = true;   // the chart's designed river leads; streams join it.
                                // Off: rivers go wherever the water runs
};

class IslandGenerator : public TerrainGenerator {
    struct Shape;
public:
    static constexpr int   SEA_LEVEL   = 160;   // water surface
    static constexpr int   PEAK_HEIGHT = 900;   // chart height 1.0, before crags
    static constexpr int   MAX_GROUND  = 1000;  // nothing rises above this
    static constexpr float WARP        = 40.0f; // coastline wobble (voxels)
    static constexpr float SNOW_LINE   = SEA_LEVEL + 0.70f * (PEAK_HEIGHT - SEA_LEVEL);

    using Options = IslandOptions;
    static inline int dbgOff = 0;   // testing: 1 bank settle, 2 bank slope, 4 lake rim
    // Bump when the chart, relief or anything feeding the bake
    // changes what a world looks like (cached bakes rebuild)
    static constexpr int GEN_VERSION = 6;   // 5: canyons  6: two canyons, mesa, rivers end at canyons

    //   singleBiome  ≥ 0: a single-biome world of that biome
    IslandGenerator(int seed, int chartSize, int singleBiome = -1, Options opt = Options())
        : TerrainGenerator(seed),
          gp(opt.params),
          chart(singleBiome >= 0 ? island::singleBiomeChart(seed % 1000000, singleBiome, chartSize)
                                 : island::collapseIsland(seed % 1000000, chartSize, opt.params)) {
        warpX.SetSeed(seed + 101);
        warpX.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        warpX.SetFrequency(1.0f / 180.0f);
        warpZ.SetSeed(seed + 202);
        warpZ.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        warpZ.SetFrequency(1.0f / 180.0f);
        detail.SetSeed(seed + 303);
        detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        detail.SetFrequency(1.0f / 40.0f);
        detail.SetFractalType(FastNoiseLite::FractalType_FBm);
        detail.SetFractalOctaves(3);
        mix.SetSeed(seed + 404);
        mix.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        mix.SetFrequency(1.0f / 24.0f);
        hills.SetSeed(seed + 505);
        hills.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        hills.SetFrequency(1.0f / 320.0f);
        hills.SetFractalType(FastNoiseLite::FractalType_FBm);
        hills.SetFractalOctaves(4);
        ridges.SetSeed(seed + 606);
        ridges.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        ridges.SetFrequency(1.0f / 420.0f);
        ridges.SetFractalType(FastNoiseLite::FractalType_Ridged);
        ridges.SetFractalOctaves(4);
        ridges.SetFractalGain(0.42f);       // fewer fine crags; the big ridges stay
        reliefN.SetSeed(seed + 707);
        reliefN.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        reliefN.SetFrequency(1.0f / 240.0f);
        reliefN.SetFractalType(FastNoiseLite::FractalType_FBm);
        reliefN.SetFractalOctaves(3);
        options = opt;
        overhangN.SetSeed(seed + 808);
        overhangN.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        overhangN.SetFrequency(0.11f);
        prepare();
        if (options.erosion) bakeTerrain();
        buildRocks();
        plantScale    = gp.get("plants");
        largeScale    = gp.get("decor");
        landmarkScale = gp.get("landmarks");
    }

    const island::Chart& getChart() const { return chart; }

    // ── Column ────────────────────────────────────────────
    TerrainColumn column(int wx, int wz) const override {
        using namespace island;
        TerrainColumn c;
        const int S = chart.size;

        // Ground height with hills, crags, cliffs and valleys
        Shape sh = shape(wx, wz);
        float h = sh.h;
        float lakeW = sh.lakeW, lakeLvl = sh.lakeLvl;
        uint16_t lakeFluid = sh.lakeFluid;
        int ni = sh.ni, nj = sh.nj, biome = sh.biome;
        float m = mix.GetNoise((float)wx, (float)wz);   // -1..1 for mixing

        // ── Features centred on their cell ───────────────
        float cx = (ni + 0.5f) * VOXEL_DIM, cz = (nj + 0.5f) * VOXEL_DIM;
        float dc = std::hypot(wx - cx, wz - cz);        // distance to cell centre
        // Level the ground round each village site (by distance to the
        // site, across cell borders): flat out to VILLAGE_FLAT, easing
        // back to the land by VILLAGE_FLAT + VILLAGE_EASE. The village
        // itself is built from blueprints there (GameSession::tickVillages)
        bool levelled = false;
        for (const VillageGround& v : villageGround()) {
            float d = std::hypot(wx - v.x, wz - v.z);
            if (d >= VILLAGE_FLAT + VILLAGE_EASE) continue;
            float t = std::clamp((d - VILLAGE_FLAT) / VILLAGE_EASE, 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t);
            if (h < v.flat - 2.0f && h < sh.lakeLvl) continue;      // (not across a lake or the sea)
            h = v.flat + (h - v.flat) * t;
            levelled = true;
        }

        c.ground = (int)std::floor(h);
        const int eco = ecoAt(wx, wz, h);
        c.eco = (uint8_t)eco;
        materials(eco, m, c);
        const island::TileDef* ecoTile = tileFor(eco);
        // lowland and upland biomes only show rock on really steep ground
        const float rockSlope = (ecoTile && ecoTile->lvHi <= 3) ? 1.8f : 1.0f;

        // ── Rock on steep ground, snow up high ───────────
        if (h > SEA_LEVEL + 40.0f && !flatBiome(biome) && !levelled) {
            float slope = baked ? bake.slope((float)wx, (float)wz)
                                : std::max(std::fabs(shape(wx + 3, wz).h - h),
                                           std::fabs(shape(wx, wz + 3).h - h)) / 3.0f;
            // where erosion has worked: loose stone piled below
            // steep ground (scree), bare rock where soil was cut away
            if (baked && ecoTile && ecoTile->lvHi >= 3) {
                float ch = bake.change((float)wx, (float)wz);
                if (ch > 2.5f && slope > 0.3f)       { c.top = BLOCK_GRAVEL; c.sub = BLOCK_GRAVEL; c.subDepth = 3; }
                else if (ch < -8.0f && ecoTile->lvHi >= 4) { c.top = BLOCK_STONE; c.sub = BLOCK_STONE; c.subDepth = 2; }
            }
            if (slope > rockSlope + 0.4f) { c.top = BLOCK_STONE; c.sub = BLOCK_STONE; c.subDepth = 2; }
            else if (slope > rockSlope)   { c.top = m > 0.0f ? BLOCK_STONE : BLOCK_GRAVEL; c.sub = BLOCK_STONE; c.subDepth = 3; }
            if (h + m * 25.0f > SNOW_LINE && slope < 1.1f) c.top = BLOCK_SNOW;
        }

        if (biome == Village || biome == PlainsVillage) {
            // (levelled above; the village itself, from blueprints, is
            // built there when a player first comes near: GameSession)
        } else if (biome == HotSpring) {
            if (dc < 14) { c.waterLevel = c.ground; c.ground -= 3; c.top = BLOCK_GRAVEL; }
            else if (dc < 20) { c.top = BLOCK_GRAVEL; }
        } else if (biome == Cave) {
            // A cave mouth: a stone pit (the caves themselves
            // come with the vertical rework)
            if (dc < 22) { c.top = BLOCK_STONE; c.sub = BLOCK_STONE; }
            if (dc < 8)  { c.ground -= 30; c.top = BLOCK_VOLCANIC_ROCK; }
        }

        // ── Karst, canyons, autumn uplands, ashlands ─────
        surfaceDetail(eco, wx, wz, h, m, c);
        canyonSurface(sh, eco, wx, wz, m, c);

        // ── Sea and lakes ────────────────────────────────
        if (c.ground <= SEA_LEVEL) c.waterLevel = std::max(c.waterLevel, SEA_LEVEL + 1);
        // Swamp and marsh: mud around the water, grass on the
        // hummocks that stand clear of it
        if (c.top == BLOCK_MUD || c.top == BLOCK_GRASS) {
            if (isSwampy(eco)) {
                c.top = c.ground >= SEA_LEVEL + 3 ? BLOCK_GRASS : BLOCK_MUD;
                c.sub = BLOCK_MUD;
            }
        }
        if (baked) {
            float surf; bool lava;
            if (bake.lake((float)wx, (float)wz, surf, lava) && c.ground < (int)surf) {
                c.waterLevel = std::max(c.waterLevel, (int)surf);
                c.fluid = lava ? BLOCK_LAVA : BLOCK_WATER;
                if (lava) { c.top = BLOCK_VOLCANIC_ROCK; c.sub = BLOCK_VOLCANIC_ROCK; }
                else if (c.top == BLOCK_GRASS) c.top = BLOCK_DIRT;
            }
        } else if (lakeW > 0.35f && lakeLvl > 0 && c.ground < (int)lakeLvl) {
            c.waterLevel = std::max(c.waterLevel, (int)lakeLvl);
            c.fluid = lakeFluid;
            if (lakeFluid == BLOCK_LAVA) { c.top = BLOCK_VOLCANIC_ROCK; c.sub = BLOCK_VOLCANIC_ROCK; }
        }

        // ── Rivers: a channel along the chart's river ────
        float rd, rw, rs;
        if (!baked) {
            if (nearestSegment(riverSegs, (float)wx, (float)wz, ni, nj, rd, rw, rs) && rd < rw) {
                int surface = std::max(SEA_LEVEL + 1, std::min(c.ground, (int)std::floor(rs)) - 2);
                float t = rd / rw;
                int depth = (int)std::round(6.0f * (1.0f - t * t)) + 1;
                int bed = surface - depth;
                if (bed < c.ground) {
                    c.ground = bed;
                    c.top = t > 0.7f ? BLOCK_GRAVEL : BLOCK_SAND;
                    c.sub = BLOCK_GRAVEL;
                }
                c.waterLevel = std::max(c.waterLevel, surface);
                c.fluid = BLOCK_WATER;
            }
        } else {
            // Water sits level with its banks: the surface is flat
            // across the river, the banks within a few voxels are
            // brought to one voxel above it (raised where they dip,
            // lowered where they stand high) and rise gently further
            // out. Rock keeps its walls (a gorge), and so does a
            // real cliff.
            const bool rocky = c.top == BLOCK_STONE || c.top == BLOCK_VOLCANIC_ROCK ||
                               (ecoTile && ecoTile->lvLo >= 5);
            float lsf; bool llv;
            const bool inLake = bake.lake((float)wx, (float)wz, lsf, llv);
            if (!inLake && nearestSegment(riverSegs, (float)wx, (float)wz, ni, nj, rd, rw, rs)) {
                int surface = std::max(SEA_LEVEL + 1, (int)std::floor(rs));
                float bankW = (5.0f + rw * 0.9f) * gp.get("banks");
                if (rd < rw) {
                    float t = rd / rw;
                    float deep = std::clamp(rw * 0.45f, 2.0f, 7.0f);
                    int bed = surface - ((int)std::round(deep * (1.0f - t * t)) + 1);
                    if (bed < c.ground) {
                        c.ground = bed;
                        c.top = t > 0.7f ? BLOCK_GRAVEL : BLOCK_SAND;
                        c.sub = BLOCK_GRAVEL;
                    } else {
                        c.ground = std::min(c.ground, surface - 1);   // never a dry island in the stream
                    }
                    c.waterLevel = std::max(c.waterLevel, surface);
                    c.fluid = BLOCK_WATER;
                } else if (rd < rw + bankW * 2.0f + 14.0f) {
                    int target = surface + 1;
                    float g = (float)c.ground;
                    // Rock walls (a gorge) and real cliffs keep their height.
                    // Blended, not switched, so no step is left where the
                    // flattening stops
                    const float above = g - target;
                    float keep = std::max(rocky ? smooth01(2.0f, 6.0f, above) : 0.0f,
                                          smooth01(10.0f, 20.0f, above) * smooth01(0.9f, 1.5f, slopeAt(wx, wz)));
                    if (sh.canyon > 1.0f) keep = 1.0f;          // canyon walls are cut to their own profile
                    if (keep < 0.999f && bankW > 0.01f) {
                        const float g0 = g;
                        const float flat = rw + 3.0f * std::min(1.0f, gp.get("banks"));
                        const float outer = rw + bankW * 2.0f + 14.0f;
                        if (rd < flat) g = (float)target;
                        else if (g > target && rd < rw + bankW * 2.0f) g = target + (g - target) * smooth01(rw + 3.0f, rw + bankW * 2.0f, rd);
                        // low ground past the flat bank slopes up to it, so
                        // the water is never held by a thin wall (fading out
                        // before the edge of the zone, so no step there)
                        else if (g < target && !(dbgOff & 2)) {
                            float lift = std::max(0.0f, target - (rd - flat) * 0.55f - g);
                            g += lift * (1.0f - smooth01(outer - 10.0f, outer, rd));
                        }
                        g = g + (g0 - g) * keep;
                        c.ground = (int)std::floor(g);
                        if (c.top == BLOCK_GRAVEL && !isSwampy(eco)) c.top = BLOCK_GRASS;   // no scree on a flat bank
                    }
                }
            }
            // lake shores too: flat rim just above the water
            if (c.waterLevel <= c.ground) {
                float sf; bool lv;
                auto nearLake = [&](float o, float& surf) {
                    return bake.lake(wx + o, (float)wz, surf, lv) || bake.lake(wx - o, (float)wz, surf, lv) ||
                           bake.lake((float)wx, wz + o, surf, lv) || bake.lake((float)wx, wz - o, surf, lv);
                };
                float ring = 0.0f;
                if (nearLake(3.0f, sf)) ring = 1.0f;
                else if (nearLake(7.0f, sf)) ring = 0.6f;
                else if (nearLake(12.0f, sf)) ring = 0.25f;
                ring *= std::min(1.0f, gp.get("banks"));
                if (ring > 0.0f && !lv) {
                    int target = (int)sf + 1;
                    float g = (float)c.ground;
                    const float above = g - target;
                    float keep = std::max(rocky ? smooth01(2.0f, 6.0f, above) : 0.0f, smooth01(10.0f, 20.0f, above));
                    // low spots just past the rim are lifted only if shallow,
                    // fading out with depth so no rim wall is built
                    if (above < 0.0f) keep = std::max(keep, smooth01(3.0f, 7.0f, -above));
                    if (keep < 0.999f && !(dbgOff & 4)) {
                        const float g0 = g;
                        if (ring >= 1.0f) g = (float)target;
                        else if (g > target) g = target + (g - target) * (1.0f - ring);
                        else g = std::max(g, target - (ring >= 0.6f ? 1.2f : 4.0f));
                        g = g + (g0 - g) * keep;
                        c.ground = (int)std::floor(g);
                    }
                }
            }
        }

        // ── Waterfalls: a rock step across the valley, a sheet
        // of water down it, a plunge pool at its foot ───────
        if (baked && !fallTable.empty()) {
            for (int fi : fallTable[cellIndex(ni, nj)]) {
                const Fall& f = falls[fi];
                float dx = wx - f.x, dz = wz - f.z;
                float a = dx * f.dx + dz * f.dz;               // downstream
                float cr = std::fabs(-dx * f.dz + dz * f.dx);  // across
                float reach = f.hw + 5.0f + f.hw * 0.9f;        // the banks either side
                if (cr < reach && a > -4.0f && a < 2.0f) {     // the lip and face are rock
                    c.top = BLOCK_STONE; c.sub = BLOCK_STONE; c.subDepth = 8;
                }
                if (cr < f.hw * 0.95f && a > -0.5f && a < 1.5f) {
                    c.ground = std::min(c.ground, (int)f.bottom - 4);
                    c.waterLevel = std::max(c.waterLevel, (int)f.top);
                    c.fluid = BLOCK_WATER;
                } else if (cr < f.hw * 1.5f && a >= 1.5f && a < f.hw * 2.4f + 4.0f) {
                    float t = (a - 1.5f) / (f.hw * 2.4f + 2.5f);
                    int bed = (int)f.bottom - 4 + (int)std::round(t * 3.0f);
                    if (bed < c.ground) { c.ground = bed; c.top = BLOCK_GRAVEL; c.sub = BLOCK_STONE; }
                    c.waterLevel = std::max(c.waterLevel, (int)f.bottom);
                    c.fluid = BLOCK_WATER;
                }
            }
        }

        // ── Wet ground along streams and lake shores ─────
        // Damp banks: mud and bare earth in the grass near the
        // water's edge, moss on wet rock
        if (baked && c.waterLevel <= c.ground && c.ground > SEA_LEVEL + 1) {
            float bank = -1.0f;
            if (nearestSegment(riverSegs, (float)wx, (float)wz, ni, nj, rd, rw, rs) && rd >= rw)
                bank = (rd - rw) / (5.0f + rw * 0.9f);              // 0 at the water's edge … 1 dry
            if (bank < 0.0f || bank > 1.0f) {
                float sf; bool lv;
                const float o = 5.0f;
                if (bake.lake(wx + o, (float)wz, sf, lv) || bake.lake(wx - o, (float)wz, sf, lv) ||
                    bake.lake((float)wx, wz + o, sf, lv) || bake.lake((float)wx, wz - o, sf, lv))
                    bank = lv ? -1.0f : 0.3f;
            }
            if (bank >= 0.0f && bank <= 1.0f) {
                float wet = (1.0f - bank) + m * 0.45f;            // ragged edge
                if (c.top == BLOCK_GRASS) {
                    if (wet > 0.95f)      c.top = BLOCK_MUD;
                    else if (wet > 0.7f)  c.top = BLOCK_DIRT;
                } else if (c.top == BLOCK_STONE || c.top == BLOCK_GRAVEL) {
                    if (wet > 0.75f) c.top = BLOCK_MOSS_STONE;
                } else if (c.top == BLOCK_SAND && wet > 1.0f) {
                    c.top = BLOCK_MUD;
                }
            }
        }

        // ── Roads and bridges ────────────────────────────
        float dd, dw, ds;
        if (nearestSegment(roadSegs, (float)wx, (float)wz, ni, nj, dd, dw, ds) && dd < dw) {
            if (c.waterLevel > c.ground) {
                // Over water: a plank deck above the surface
                c.deckY = c.waterLevel + 2;
                c.deck  = BLOCK_PLANKS;
            } else {
                c.top = dd < dw * 0.7f ? BLOCK_GRAVEL : BLOCK_DIRT;
            }
        }
        return c;
    }

    int getSurfaceY(int wx, int wz) const override {
        return column(wx, wz).ground;
    }

    glm::ivec3 spawnPoint(int, int) const override {
        const island::Marker* s = chart.marker("spawn");
        int x = s ? s->x * VOXEL_DIM + VOXEL_DIM / 2 : chart.size * VOXEL_DIM / 2;
        int z = s ? s->y * VOXEL_DIM + VOXEL_DIM / 2 : chart.size * VOXEL_DIM / 2;
        TerrainColumn c = column(x, z);
        return { x, std::max(c.ground, c.waterLevel), z };
    }

    // The chart's villages (markers of kind "village"), at their cell's
    // middle: the plaza the generator levels there
    std::vector<VillageSite> villageSites() const override {
        std::vector<VillageSite> out;
        for (const island::Marker& m : chart.markers) {
            if (m.kind != "village") continue;
            VillageSite s;
            s.at = { m.x * VOXEL_DIM + VOXEL_DIM / 2, m.y * VOXEL_DIM + VOXEL_DIM / 2 };
            s.name = m.label;
            s.small = m.label.find("amlet") != std::string::npos;
            out.push_back(s);
        }
        return out;
    }

protected:
    // Trees by biome: how likely a tree cell is filled, and
    // which kinds grow there
    bool treeInCell(int cellX, int cellZ, int& wx, int& wz, int& type) const override {
        using namespace island;
        int margin = 5, span = TREE_CELL - margin * 2;
        wx = cellX * TREE_CELL + margin + (int)(cellRandom(cellX, cellZ, 1) * (float)span);
        wz = cellZ * TREE_CELL + margin + (int)(cellRandom(cellX, cellZ, 2) * (float)span);
        if (wx < 0 || wz < 0 || wx >= chart.size * VOXEL_DIM || wz >= chart.size * VOXEL_DIM)
            return false;

        int biome = ecoAt(wx, wz, (float)shape(wx, wz).h);
        float chance = 0.0f;
        if (!biomeTrees(biome, cellRandom(cellX, cellZ, 4), chance, type)) return false;
        if (cellRandom(cellX, cellZ, 3) > chance * gp.get("trees")) return false;

        // Only on dry land, clear of roads and rivers — except the
        // swamp's trees that stand in shallow water (cypresses …)
        TerrainColumn c = column(wx, wz);
        const int depth = c.waterLevel - c.ground;
        const bool wet = depth > 0;
        if (c.deckY >= 0 || depth > 3 || (wet && (!isSwampy(c.eco) || c.fluid != BLOCK_WATER))) return false;
        if (c.top == BLOCK_GRAVEL || c.top == BLOCK_COBBLESTONE) return false;

        if (c.top == BLOCK_STONE || c.top == BLOCK_SNOW) return false;   // bare rock, snowfield
        if (nearLandmark(wx, wz, 6)) return false;                        // landmarks stand clear

        // Some of the biome's trees are decorations.toml trees
        // (twisted oaks, maples, cypresses …) in place of the usual
        if (decorSet && !firstTreeOfDef.empty()) {
            float roll = cellRandom(cellX, cellZ, 5), acc = 0.0f;
            for (const auto& [di, share] : decorSet->list(decor::Layer::Tree, c.eco)) {
                acc += share;
                if (roll < acc) {
                    int first = firstTreeOfDef[(size_t)di];
                    int nv = (int)decorSet->defs[(size_t)di].variants.size();
                    if (wet && decorSet->defs[(size_t)di].water != decor::Water::Shallow) break;
                    if (first >= 0 && nv > 0) {
                        int t = first + (int)(cellRandom(cellX, cellZ, 6) * nv) % nv;
                        if (c.ground + trees[(size_t)t].height <= WORLD_HEIGHT) { type = t; return true; }
                    }
                    break;
                }
            }
        }

        if (wet) return false;                   // the usual trees keep their feet dry

        type = replaceTree(type, cellX, cellZ);

        // Must fit under the top of the world
        if (c.ground + trees[type].height > WORLD_HEIGHT) type = TREE_PINE;
        if (c.ground + trees[type].height > WORLD_HEIGHT) type = TREE_DEAD;
        return c.ground + trees[type].height <= WORLD_HEIGHT;
    }

public:
    // A single-biome world has no sea around it
    int seaLevel() const override { return chart.singleBiome >= 0 ? -1 : SEA_LEVEL; }

    // Rivers run from the high end of their segments to the low
    // (lines are traced from the source, so a level stretch runs on
    // the way the line goes); faster where they drop. Canyon rivers
    // too. Anything else: still.
    glm::vec2 flowAt(int wx, int wz) const override {
        const int S = chart.size;
        int ci = std::clamp(wx / VOXEL_DIM, 0, S - 1), cj = std::clamp(wz / VOXEL_DIM, 0, S - 1);
        float best = 1e9f;
        glm::vec2 out(0.0f);
        const size_t k = (size_t)cellIndex(ci, cj);
        for (int which = 0; which < 2; which++) {           // rivers, then canyons
            const auto& table = which == 0 ? riverSegs : canyonSegs;
            if (k >= table.size()) continue;
            for (const Seg& sg : table[k]) {
                float vx = sg.bx - sg.ax, vz = sg.bz - sg.az;
                float len2 = vx * vx + vz * vz;
                if (len2 < 1e-4f) continue;
                float t = std::clamp(((wx - sg.ax) * vx + (wz - sg.az) * vz) / len2, 0.0f, 1.0f);
                float dx = wx - (sg.ax + vx * t), dz = wz - (sg.az + vz * t);
                float d = std::sqrt(dx * dx + dz * dz) - sg.halfWidth;
                if (d > 2.0f || d >= best) continue;
                best = d;
                float len = std::sqrt(len2);
                glm::vec2 dir(vx / len, vz / len);
                if (sg.hb > sg.ha + 0.01f) dir = -dir;           // runs to the lower end
                float drop = std::fabs(sg.ha - sg.hb) / len;
                out = dir * std::clamp(0.45f + drop * 12.0f, 0.45f, 1.0f);
            }
        }
        return out;
    }

    float forestCover(int wx, int wz) const override {
        float chance = 0.0f;
        int type;
        if (!biomeTrees(ecoAt(wx, wz, (float)shape(wx, wz).h), 0.5f, chance, type)) return 0.0f;
        return chance;
    }

protected:
    // Which trees grow in a biome: how likely a tree cell is
    // filled, and (from a 0..1 roll) which kind
    bool biomeTrees(int biome, float roll, float& chance, int& type) const {
        using namespace island;
        switch (biome) {
        case DenseForest: chance = 0.85f; type = roll < 0.75f ? TREE_OAK : TREE_ANCIENT; break;
        case Forest:      chance = 0.60f; type = roll < 0.80f ? TREE_OAK : (roll < 0.95f ? TREE_PINE : TREE_ANCIENT); break;
        case Shrubland:   chance = 0.25f; type = roll < 0.6f ? TREE_OAK : TREE_DEAD; break;
        case Riparian:    chance = 0.40f; type = TREE_OAK; break;
        case Grassland:   chance = 0.10f; type = TREE_OAK; break;
        case Plains:      chance = 0.05f; type = TREE_OAK; break;
        case Roadside:    chance = 0.08f; type = TREE_OAK; break;
        case Swamp:       chance = 0.50f; type = roll < 0.7f ? TREE_DEAD : TREE_OAK; break;
        case Marsh:       chance = 0.20f; type = TREE_DEAD; break;
        case Foothills:   chance = 0.30f; type = roll < 0.6f ? TREE_PINE : TREE_OAK; break;
        case LowMountain: chance = 0.35f; type = TREE_PINE; break;
        case MidMountain: chance = 0.15f; type = TREE_PINE; break;
        case Canyons:   chance = 0.06f; type = TREE_DEAD; break;
        case AutumnUplands: chance = 0.55f; type = TREE_OAK; break;
        case CanyonMesa:    chance = 0.12f; type = TREE_OAK; break;
        case HighlandMoor:chance = 0.04f; type = TREE_DEAD; break;
        case Karst:    chance = 0.12f; type = TREE_PINE; break;
        case Ashlands: chance = 0.03f; type = TREE_DEAD; break;
        default:          return false;
        }
        return true;
    }

    // The biome at a world position: the nearest chart cell
    // to the warped point
    int biomeAt(int wx, int wz) const {
        float u = (wx + warpX.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        float v = (wz + warpZ.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        return chart.at(std::clamp((int)std::floor(u), 0, chart.size - 1),
                        std::clamp((int)std::floor(v), 0, chart.size - 1));
    }

    // ── The biome that decides surface blocks and trees ──
    // A cell's land biome, or the overlay where that changes
    // the ground (banks, villages, springs, caves, lakes)
    int ecoCell(int idx) const {
        using namespace island;
        int ov = chart.biomes[idx];
        switch (ov) {
        case Riparian: case Village: case PlainsVillage: case HotSpring: case Cave:
        case LavaLake: case MountainLake: case LowMountainLake: case PlainsLake: case ForestLake:
            return ov;
        default:
            return chart.base[idx];
        }
    }

    // Where biomes meet, which one a column belongs to. Each
    // biome sets how wide a band it mixes over (TileDef::edge);
    // inside the band a column picks one side by a ragged noise.
    // Beaches and the sea follow height instead: sand wherever
    // the ground is near the water, sea floor below it.
    int ecoAt(int wx, int wz, float h) const {
        using namespace island;
        float u = (wx + warpX.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        float v = (wz + warpZ.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        float fu = u - 0.5f, fv = v - 0.5f;
        int i0 = (int)std::floor(fu), j0 = (int)std::floor(fv);
        float tu = fu - i0, tv = fv - j0;
        int b[4];
        float band = 0.0f;
        bool same = true;
        for (int k = 0; k < 4; k++) {
            b[k] = ecoCell(cellIndex(i0 + (k & 1), j0 + (k >> 1)));
            if (b[k] != b[0]) same = false;
            const TileDef* t = tileFor(b[k]);
            band = std::max(band, (t ? t->edge : 0.2f) * gp.get("edges"));
        }
        int pick;
        if (same) pick = b[0];
        else {
            band = std::max(band, 0.04f);
            float wu = smooth01(0.5f - band, 0.5f + band, tu), wv = smooth01(0.5f - band, 0.5f + band, tv);
            float w[4] = { (1 - wu) * (1 - wv), wu * (1 - wv), (1 - wu) * wv, wu * wv };
            // ragged: a smooth noise for the outline plus a fine
            // hash so the very edge breaks up into scattered blocks
            uint32_t hsh = (uint32_t)wx * 73856093u ^ (uint32_t)wz * 19349663u ^ (uint32_t)seed * 83492791u;
            hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
            float r = 0.5f + mix.GetNoise((float)wx * 1.7f, (float)wz * 1.7f) * 0.35f + ((hsh & 1023) / 1023.0f - 0.5f) * 0.3f;
            r = std::clamp(r, 0.0f, 0.999f);
            float acc = 0.0f;
            pick = b[3];
            for (int k = 0; k < 4; k++) { acc += w[k]; if (r < acc) { pick = b[k]; break; } }
        }
        // height rules for the coast
        auto isBeach = [](int x) { return x == Beach || x == SwampBeach || x == MarshBeach; };
        int beach = -1, landB = -1, seaB = -1;
        for (int k = 0; k < 4; k++) {
            if (isBeach(b[k])) beach = b[k];
            else if (isSeaBiome(b[k])) seaB = b[k];
            else landB = b[k];
        }
        if (isSeaBiome(pick) && h > SEA_LEVEL + 1) pick = beach >= 0 ? beach : landB >= 0 ? landB : pick;
        else if (!isSeaBiome(pick) && h < SEA_LEVEL - 2 && seaB >= 0) pick = seaB;
        else if (!isBeach(pick) && !isSeaBiome(pick) && beach >= 0 && h < SEA_LEVEL + 6 && pick != SeaCliffs) pick = beach;
        else if (isBeach(pick) && h > SEA_LEVEL + 9 && landB >= 0) pick = landB;
        return surfaceBiome(pick, wx, wz);
    }

    // Block ids by name (0 → the fallback, e.g. before the registry loads)
    static uint16_t blockId(const char* name, uint16_t fallback) {
        uint16_t id = gRegistry.getIDByName(name);
        return id ? id : fallback;
    }

    // Canyon walls in layered rock (limestone, sandstone in the canyon
    // country), grass or red sand on the ledges, gravel and grass on
    // the floor
    void canyonSurface(const Shape& sh, int eco, int wx, int wz, float m, TerrainColumn& c) const {
        using namespace island;
        if (sh.canyonPart == 0 || sh.canyon < 1.0f) return;
        static const uint16_t RSAND = blockId("red_sand", BLOCK_SAND);
        const int kind = (eco == Canyons) ? 1 : 2;
        c.strata = (uint8_t)kind;
        c.strataOff = (int8_t)std::round(mix.GetNoise((float)wx * 0.03f, (float)wz * 0.03f) * 4.0f);
        c.deep = strataBlock(0, kind);
        switch (sh.canyonPart) {
        case 1:   // floor
            c.top = m > 0.15f ? BLOCK_GRAVEL : BLOCK_GRASS; c.strata = 0; c.sub = BLOCK_GRAVEL; c.subDepth = 3;
            break;
        case 2:   // wall
            c.top = strataBlock(c.ground - 1 + c.strataOff, kind);
            break;
        case 3:   // ledge
            c.top = kind == 1 ? (m > 0.0f ? RSAND : strataBlock(c.ground - 1 + c.strataOff, 1))
                              : (m > -0.3f ? BLOCK_GRASS : BLOCK_MOSS_STONE);
            break;
        }
    }

    // The look of the four upland surfaces, on top of materials()
    void surfaceDetail(int eco, int wx, int wz, float h, float m, TerrainColumn& c) const {
        using namespace island;
        if (eco != Karst && eco != Canyons && eco != AutumnUplands && eco != Ashlands && eco != CanyonMesa) return;
        static const uint16_t LIME   = blockId("limestone", BLOCK_STONE);
        static const uint16_t RSAND  = blockId("red_sand", BLOCK_SAND);
        static const uint16_t ASH    = blockId("ash", BLOCK_GRAVEL);
        static const uint16_t CINDER = blockId("cinder", BLOCK_GRAVEL);
        const float slope = slopeAt(wx, wz);
        switch (eco) {
        case Karst: {
            // pale limestone wherever rock shows, and limestone pavement:
            // flat slabs split by a warped grid of cracks (grikes) with
            // grass in them
            if (c.top == BLOCK_STONE || c.top == BLOCK_GRAVEL || slope > 0.55f) c.top = LIME;
            if (c.sub == BLOCK_STONE || slope > 0.55f) c.sub = LIME;
            c.deep = LIME;
            float pave = mix.GetNoise((float)wx * 0.35f, (float)wz * 0.35f);
            if (slope < 0.6f && pave > 0.12f) {
                c.top = LIME; c.sub = LIME; c.subDepth = 3;
                float gx = std::fmod(std::fabs(wx + 2.5f * std::sin(wz * 0.11f)), 7.0f);
                float gz = std::fmod(std::fabs(wz + 2.5f * std::sin(wx * 0.13f)), 9.0f);
                if (gx < 1.0f || gz < 1.0f) { c.ground -= 1; c.top = BLOCK_GRASS; }
            }
            break;
        }
        case Canyons:
            // red ground and dry grass; banded sandstone only on real
            // cliffs (the canyon walls are dressed in canyonSurface)
            if (slope > 1.1f) {
                c.strata = 1;
                c.strataOff = (int8_t)std::round(mix.GetNoise((float)wx * 0.04f, (float)wz * 0.04f) * 5.0f);
                c.top = strataBlock(c.ground - 1 + c.strataOff, 1);
            } else c.top = m > -0.2f ? RSAND : BLOCK_GRASS;
            break;
        case CanyonMesa:
            // open grassland with limestone breaking through here and there
            if (c.top == BLOCK_STONE || c.top == BLOCK_GRAVEL || (m > 0.62f && slope < 0.5f)) c.top = LIME;
            c.deep = LIME;
            break;
        case AutumnUplands:
            if (c.top == BLOCK_STONE && m > 0.0f) c.top = BLOCK_MOSS_STONE;
            if (c.top == BLOCK_GRAVEL) c.top = BLOCK_DIRT;
            break;
        case Ashlands: {
            // basalt ridges and steep faces, pale ash drifted into the
            // hollows, cinders between
            float n = mix.GetNoise((float)wx * 0.8f, (float)wz * 0.8f) + (detail.GetNoise((float)wx * 3.0f, (float)wz * 3.0f)) * 0.25f;
            if (slope > 0.8f || n > 0.3f) c.top = BLOCK_VOLCANIC_ROCK;
            else if (n < -0.2f)           c.top = ASH;
            else                          c.top = CINDER;
            c.sub = BLOCK_VOLCANIC_ROCK; c.deep = BLOCK_VOLCANIC_ROCK; c.subDepth = 3;
            break;
        }
        default: break;
        }
    }

    // Surfaces that don't come from the chart: ground near a lava lake
    // is ashlands, and big patches of the canyon country are autumn
    // uplands instead (golden woods on the gentler ground)
    int surfaceBiome(int pick, int wx, int wz) const {
        using namespace island;
        if (isSeaBiome(pick) || pick == LavaLake || pick == HotSpring || pick == Cave ||
            pick == Village || pick == PlainsVillage) return pick;
        if (!lavaCells.empty()) {
            float jag = mix.GetNoise((float)wx * 0.6f, (float)wz * 0.6f) * 0.35f;
            for (const auto& lc : lavaCells) {
                float d = std::hypot(wx - lc.x, wz - lc.y) / VOXEL_DIM;
                if (d < 2.2f + jag) return Ashlands;
            }
        }
        if (!mesaCell.empty()) {
            float u = (wx + warpX.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
            float v = (wz + warpZ.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
            if (mesaCell[cellIndex((int)std::floor(u), (int)std::floor(v))]) return CanyonMesa;
        }
        if (pick == Canyons) {
            float n = autumnN.GetNoise((float)wx, (float)wz);
            if (n > 0.05f) return AutumnUplands;
        }
        return pick;
    }

    // ── After the bake ────────────────────────────────────
    // Biome and cell from the chart; height and lakes from the bake
    void bakedShape(int wx, int wz, Shape& out) const {
        using namespace island;
        const int S = chart.size;
        float u = (wx + warpX.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        float v = (wz + warpZ.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        out.ni = std::clamp((int)std::floor(u), 0, S - 1);
        out.nj = std::clamp((int)std::floor(v), 0, S - 1);
        out.biome = chart.at(out.ni, out.nj);
        float h = bake.height((float)wx, (float)wz);
        // fine bumps, as before, scaled by how rough the biome is
        const TileDef* t = tileFor(chart.base[cellIndex(out.ni, out.nj)]);
        float amp = t ? t->relief : 10.0f;
        h += detail.GetNoise((float)wx, (float)wz) * std::clamp(amp * 0.02f, 0.5f, 3.0f);
        { float cut; h = canyonCut(wx, wz, out.ni, out.nj, h, false, cut, &out.canyonPart); out.canyon = cut; }
        const float knee = MAX_GROUND - 60.0f;
        if (h > knee) h = knee + (h - knee) * 0.4f;
        out.h = std::min(h, (float)MAX_GROUND);
    }

    // How easily the ground at a world point erodes
    float erodibilityAt(int wx, int wz) const {
        using namespace island;
        float u = wx / (float)VOXEL_DIM, v = wz / (float)VOXEL_DIM;
        int idx = cellIndex((int)std::floor(u), (int)std::floor(v));
        int ov = chart.biomes[idx];
        if (ov == Village || ov == PlainsVillage || ov == HotSpring || ov == Cave || ov == MountainPass) return 0.0f;
        float d, w, f;
        if (nearestSegment(roadSegs, (float)wx, (float)wz, idx % chart.size, idx / chart.size, d, w, f) && d < w + 6) return 0.0f;
        if (!canyonSegs.empty())
            for (const Seg& sg : canyonSegs[idx]) {
                float t;
                if (segDistance(sg, (float)wx, (float)wz, t) < CANYON_REACH) return 0.06f;   // sheer walls stay sheer
            }
        switch (chart.base[idx]) {
        case Peak: case HighMountain:              return 1.0f;
        case MidMountain: case LowMountain:        return 0.9f;
        case Plateau:                              return 0.35f;
        case Foothills: case HighlandMoor:         return 0.7f;
        case Karst: case Canyons:             return 0.6f;
        case SeaCliffs:                            return 0.12f;   // the faces stay sheer
        case DenseForest: case Forest: case Shrubland: return 0.45f;
        case Grassland: case Plains:               return 0.25f;
        case Marsh:                                return 0.1f;
        default:                                   return 0.0f;   // sea, beaches, swamp
        }
    }

    void bakeTerrain() {
        using namespace island;
        auto t0 = std::chrono::steady_clock::now();
        const int worldVoxels = chart.size * VOXEL_DIM;
        // natural rivers: no guide channels in the ground
        if (!options.guidedRivers) for (auto& v : riverSegs) v.clear();
        TerrainBake::Settings st;
        st.seaLevel = (float)SEA_LEVEL + 0.5f;
        st.maxNaturalLakes = (int)std::lround(std::max(1, chart.size / 24) * gp.get("lakes"));
        st.streamPower    *= gp.get("erosion");
        st.erosionPasses   = (int)gp.get("passes");
        st.talus           = std::tan(gp.get("scree") * 3.14159265f / 180.0f);
        st.riverFlow      /= gp.get("streams");
        // guide zone: traced streams end where they reach a designed river
        auto nearGuide = [&](int wx, int wz) {
            if (!options.guidedRivers) return false;
            int ci = std::clamp(wx / VOXEL_DIM, 0, chart.size - 1), cj = std::clamp(wz / VOXEL_DIM, 0, chart.size - 1);
            for (const Seg& sg : riverSegs[cellIndex(ci, cj)]) {
                float t, d = segDistance(sg, (float)wx, (float)wz, t);
                if (d < sg.halfWidth + 20.0f) return true;
            }
            return false;
        };
        // A saved bake is used if it was made from exactly this
        // world: seed, size, biome, river option and code version
        uint64_t key = 1469598103934665603ull;
        for (uint64_t v : { (uint64_t)GEN_VERSION, (uint64_t)TerrainBake::VERSION, (uint64_t)(uint32_t)seed,
                            (uint64_t)chart.size, (uint64_t)(chart.singleBiome + 1), (uint64_t)options.guidedRivers, gp.hash() })
            key = (key ^ v) * 1099511628211ull;
        bool cached = !options.bakeCache.empty() && bake.load(options.bakeCache, key);
        if (!cached) {
        bake.build(worldVoxels, st,
            [&](int x, int z) { return shape(x, z, true).h; },
            [&](int x, int z) { return erodibilityAt(x, z); },
            [&](int x, int z) {
                int b = chart.biomes[cellIndex(x / VOXEL_DIM, z / VOXEL_DIM)];
                return b == LavaLake ? 2 : (b == MountainLake || b == LowMountainLake || b == PlainsLake || b == ForestLake) ? 1 : 0;
            },
            nearGuide, (uint32_t)seed);
            if (!options.bakeCache.empty()) bake.save(options.bakeCache, key);
        }

        // Rivers from now on: the designed ones (guided), their
        // water surface taken from the eroded ground, plus every
        // stream the water found
        for (auto& v : riverSegs) v.clear();
        std::vector<TerrainBake::RiverLine> all;
        if (options.guidedRivers)
            for (auto g : guideLines) {
                float run = 1e9f;
                for (auto& p : g) {
                    run = std::min({ run, bake.filled(p.x, p.z), bake.height(p.x, p.z) });
                    p.surface = run;
                }
                all.push_back(std::move(g));
            }
        size_t guidedPieces = all.size();
        drainCanyonLakes();
        for (const auto& l : bake.rivers()) all.push_back(l);
        // A river ends at a lake's edge and starts again where the
        // water leaves it, instead of running on across the lake
        {
            std::vector<TerrainBake::RiverLine> split;
            size_t guided = 0;
            for (size_t li = 0; li < all.size(); li++) {
                const auto& l = all[li];
                TerrainBake::RiverLine run;
                bool prevIn = false;
                TerrainBake::RiverPoint lastIn{};
                auto flush = [&]() {
                    if (run.size() >= 2) { split.push_back(run); if (li < guidedPieces) guided++; }
                    run.clear();
                };
                const bool canyonLine = options.guidedRivers && li < canyonGuides;
                bool ended = false;
                for (const auto& p : l) {
                    if (ended) break;
                    // reaching a canyon (from above its floor): the river
                    // ends there, pouring over the rim
                    if (!canyonLine && crossesCanyon(p.x, p.z, p.surface, p.halfWidth + 8.0f)) { ended = true; break; }
                    float sf; bool lv;
                    bool in = bake.lake(p.x, p.z, sf, lv);
                    if (in) {
                        if (!prevIn && !run.empty()) { run.push_back(p); flush(); }   // the mouth
                        prevIn = true; lastIn = p;
                        continue;
                    }
                    if (prevIn) { run.clear(); run.push_back(lastIn); }              // the outlet
                    prevIn = false;
                    run.push_back(p);
                }
                flush();
            }
            all.swap(split);
            guidedPieces = guided;
        }
        // Waterfalls: where the surface drops 9+ voxels (3 ft) within
        // ~18 along the river, hold it level to the middle of the
        // drop, then fall in one step
        falls.clear();
        fallTable.assign((size_t)chart.size * chart.size, {});
        const size_t firstTraced = options.guidedRivers ? guidedPieces : 0;
        for (size_t li = firstTraced; li < all.size(); li++) {   // traced streams follow the real ground
            auto& l = all[li];
            for (size_t k = 0; k + 1 < l.size(); k++) {
                size_t j = k + 1; float run = 0;
                while (j < l.size()) {
                    run += std::hypot(l[j].x - l[j - 1].x, l[j].z - l[j - 1].z);
                    if (run > 30.0f) break;
                    if (l[k].surface - l[j].surface >= gp.get("falls")) break;
                    j++;
                }
                if (j >= l.size() || run > 30.0f) continue;
                float top = l[k].surface, bottom = l[j].surface;
                // only where the ground itself steps down, and not
                // where a river just runs into a lake or the sea
                {
                    float sf; bool lv;
                    float gTop = bake.height(l[k].x, l[k].z), gBot = bake.height(l[j].x, l[j].z);
                    bool lakeAt = bake.lake(l[k].x, l[k].z, sf, lv) || bake.lake(l[j].x, l[j].z, sf, lv) ||
                                  bake.lake((l[k].x + l[j].x) * 0.5f, (l[k].z + l[j].z) * 0.5f, sf, lv);
                    if (lakeAt || gTop < top - 2.0f || gBot > bottom + 4.0f || gTop - gBot < 8.0f) continue;
                }
                size_t mid = (k + j + 1) / 2;
                for (size_t q = k; q < mid; q++) l[q].surface = top;
                for (size_t q = mid; q <= j; q++) l[q].surface = bottom;
                Fall f;
                f.x = l[mid].x; f.z = l[mid].z;
                float ddx = l[j].x - l[k].x, ddz = l[j].z - l[k].z, len = std::max(0.01f, std::hypot(ddx, ddz));
                f.dx = ddx / len; f.dz = ddz / len;
                f.top = top; f.bottom = bottom; f.hw = std::max(l[mid].halfWidth, 2.5f);
                if (f.bottom <= SEA_LEVEL + 1) continue;
                int ci = std::clamp((int)(f.x / VOXEL_DIM), 0, chart.size - 1), cj = std::clamp((int)(f.z / VOXEL_DIM), 0, chart.size - 1);
                for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++)
                    fallTable[cellIndex(ci + di, cj + dj)].push_back((int)falls.size());
                falls.push_back(f);
                k = j;
            }
        }
        // Flat water: every river is held level and steps down
        // (at least 2 voxels at a time) instead of sloping, so the
        // water meets its banks evenly
        for (auto& l : all) {
            if (l.empty()) continue;
            float level = std::floor(l[0].surface);
            for (auto& p : l) {
                if (p.surface < level - 1.5f) level = std::floor(p.surface);
                p.surface = level;
            }
        }
        // segments: where the level changes, split in the middle so
        // the step is sharp instead of a ramp
        std::vector<TerrainBake::RiverLine> stepped;
        for (const auto& l : all) {
            TerrainBake::RiverLine o;
            for (size_t k = 0; k < l.size(); k++) {
                if (k > 0 && l[k].surface != l[k - 1].surface) {
                    TerrainBake::RiverPoint m{ (l[k].x + l[k - 1].x) * 0.5f, (l[k].z + l[k - 1].z) * 0.5f,
                                               l[k - 1].surface, l[k].halfWidth };
                    o.push_back(m);
                    TerrainBake::RiverLine done = o;
                    stepped.push_back(std::move(done));
                    o.clear();
                    m.surface = l[k].surface;
                    o.push_back(m);
                }
                o.push_back(l[k]);
            }
            if (o.size() > 1) stepped.push_back(std::move(o));
        }
        settleRiverBanks(stepped);
        for (const auto& l : stepped)
            for (size_t k = 0; k + 1 < l.size(); k++) {
                const auto &a = l[k], &b = l[k + 1];
                Seg sg{ a.x, a.z, b.x, b.z, std::max(a.halfWidth, b.halfWidth), a.surface, b.surface };
                int ca = cellIndex((int)std::floor(a.x / VOXEL_DIM), (int)std::floor(a.z / VOXEL_DIM));
                int cb = cellIndex((int)std::floor(b.x / VOXEL_DIM), (int)std::floor(b.z / VOXEL_DIM));
                std::vector<int> cells{ ca };
                for (int c : { ca, cb }) for (int nb : neighbors(chart.size, c)) cells.push_back(nb);
                if (cb != ca) cells.push_back(cb);
                std::sort(cells.begin(), cells.end());
                cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
                for (int c : cells) riverSegs[c].push_back(sg);
            }
        baked = true;
        float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "Terrain baked: " << bake.size() << "^2 samples, "
                  << bake.stats().lakes << " lakes, " << falls.size() << " waterfalls, " << bake.stats().rivers << " streams, " << (int)ms << " ms ("
                  << (options.guidedRivers ? "guided" : "natural") << " rivers" << (cached ? ", from the saved bake" : "") << ")\n";
    }

    // ── River banks ───────────────────────────────────────
    // A river is held one voxel below its banks. Where the land
    // beside it dips lower than the water, the bank would be a thin
    // wall with the river perched on it. Each such dip is settled
    // on the bake's grid, one of two ways:
    //   enclosed (a hollow next to the river)  it fills with water
    //       at the river's level: a pond opening off the river, a
    //       wider bend
    //   open (the river runs along a slope, or the dip runs off)
    //       the ground is built up into a sloped embankment, flat
    //       by the water and falling away at about 1 in 2
    void settleRiverBanks(const std::vector<TerrainBake::RiverLine>& lines) {
        if (!bake.ready() || (dbgOff & 1)) return;
        const int S = bake.size(), ST = TerrainBake::STEP;
        const size_t n = (size_t)S * S;
        std::vector<float> dist(n, 1e9f), surf(n, 0.0f), half(n, 0.0f);
        const float REACH = 64.0f;            // how far from a river a dip is looked at
        for (const auto& l : lines)
            for (size_t k = 0; k + 1 < l.size(); k++) {
                const auto &a = l[k], &b = l[k + 1];
                float len = std::hypot(b.x - a.x, b.z - a.z);
                int steps = std::max(1, (int)(len / 3.0f));
                for (int q = 0; q <= steps; q++) {
                    float t = (float)q / steps;
                    float px = a.x + (b.x - a.x) * t, pz = a.z + (b.z - a.z) * t;
                    float hw = a.halfWidth + (b.halfWidth - a.halfWidth) * t;
                    float sfc = std::floor(t < 0.5f ? a.surface : b.surface);
                    float R = hw + REACH;
                    int x0 = std::max(0, (int)std::floor((px - R) / ST)), x1 = std::min(S - 1, (int)std::ceil((px + R) / ST));
                    int z0 = std::max(0, (int)std::floor((pz - R) / ST)), z1 = std::min(S - 1, (int)std::ceil((pz + R) / ST));
                    for (int z = z0; z <= z1; z++)
                        for (int x = x0; x <= x1; x++) {
                            float d = std::hypot(x * (float)ST - px, z * (float)ST - pz);
                            size_t i = (size_t)z * S + x;
                            if (d < dist[i]) { dist[i] = d; half[i] = hw; if (surf[i] == 0.0f) surf[i] = sfc; }
                            // the lowest water close by: never build up to a
                            // higher stretch of river (a step, a confluence)
                            if (d < hw + 12.0f) surf[i] = surf[i] == 0.0f ? sfc : std::min(surf[i], sfc);
                        }
                }
            }
        const float sea = (float)SEA_LEVEL + 1.0f;
        std::vector<uint8_t> seen(n, 0);
        std::vector<int> q;
        int ponds = 0, banks = 0;
        for (size_t i = 0; i < n; i++) {
            if (seen[i] || dist[i] > half[i] + 10.0f || dist[i] < half[i]) continue;
            int x = (int)(i % S), z = (int)(i / S);
            float s0 = surf[i];
            if (bake.sampleLake(x, z) || bake.sampleHeight(x, z) >= s0 - 0.75f || s0 <= sea) continue;
            // flood the dip: lower than the water, off the channel, near the river
            q.clear(); q.push_back((int)i); seen[i] = 1;
            bool open = false, wetEdge = false;
            float deepest = 0.0f;
            for (size_t h = 0; h < q.size(); h++) {
                int c = q[h], cx = c % S, cz = c / S;
                static const int N4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (auto& o : N4) {
                    int nx = cx + o[0], nz = cz + o[1];
                    if (nx < 0 || nz < 0 || nx >= S || nz >= S) { open = true; continue; }
                    size_t j = (size_t)nz * S + nx;
                    if (seen[j] || bake.sampleLake(nx, nz)) continue;
                    float hj = bake.sampleHeight(nx, nz);
                    if (hj >= s0 - 0.25f) continue;                   // the rim
                    if (dist[j] < half[j]) continue;                  // the river itself
                    if (hj <= sea || dist[j] > half[j] + REACH) { open = true; if (hj <= sea) wetEdge = true; continue; }
                    seen[j] = 1; q.push_back((int)j);
                }
                if (q.size() > 4000) { open = true; break; }
            }
            for (int c : q) {
                deepest = std::max(deepest, s0 - bake.sampleHeight(c % S, c / S));
                int cx = c % S, cz = c / S;
                for (int dz = -1; dz <= 1 && !wetEdge; dz++) for (int dx = -1; dx <= 1; dx++) {
                    int nx = cx + dx, nz = cz + dz;
                    if (nx >= 0 && nz >= 0 && nx < S && nz < S && bake.sampleLake(nx, nz)) { wetEdge = true; break; }
                }
            }
            if (!open && q.size() >= 3) {
                // a pond at the river's level, opened to the river
                ponds++;
                for (int c : q) bake.makeLakeSample(c % S, c / S, s0);
                for (int c : q) {
                    int cx = c % S, cz = c / S;
                    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
                        int nx = cx + dx, nz = cz + dz;
                        if (nx < 0 || nz < 0 || nx >= S || nz >= S) continue;
                        size_t j = (size_t)nz * S + nx;
                        if (dist[j] < half[j] + 6.0f && !bake.sampleLake(nx, nz) && bake.sampleHeight(nx, nz) < s0 + 2.0f) {
                            bake.setSampleHeight(nx, nz, std::min(bake.sampleHeight(nx, nz), s0 - 1.5f));
                            bake.makeLakeSample(nx, nz, s0);
                        }
                    }
                }
            } else if (!wetEdge && deepest <= 4.0f && q.size() <= 400) {
                // a shallow dip: built up into a gentle bank. Deep
                // drops, and land falling away to other water (the sea,
                // a lake), are left as they are: no walls
                banks++;
                for (int c : q) {
                    int cx = c % S, cz = c / S;
                    float d = dist[(size_t)c] - half[(size_t)c];
                    float target = s0 + 1.0f - std::max(0.0f, d - 4.0f) * 0.55f;
                    if (bake.sampleHeight(cx, cz) < target) bake.setSampleHeight(cx, cz, target);
                }
            }
        }
        if (ponds || banks)
            std::cout << "River banks: " << ponds << " dips became ponds, " << banks << " built up\n";
    }

    // ── Rock formations ───────────────────────────────────
    // Made once from noise; placed by rockInCell() below
    enum RockKind { RockBoulder, RockBigBoulder, RockOutcrop, RockSpire, RockLedge, ROCK_KINDS };
    enum RockStone { StonePlain, StoneVolcanic, StoneMossy, StoneLimestone, StoneSandstone, ROCK_STONES };
    static constexpr int ROCK_VARIANTS = 4;    // per kind and stone (ledges: one per direction)
    int rockIndex(int stone, int kind, int variant) const {
        return (stone * ROCK_KINDS + kind) * ROCK_VARIANTS + variant;
    }

    void buildRocks() {
        FastNoiseLite n3;
        n3.SetSeed(seed + 909);
        n3.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        n3.SetFrequency(0.18f);
        rockShapes.assign((size_t)ROCK_STONES * ROCK_KINDS * ROCK_VARIANTS, TreeShape());
        for (int stone = 0; stone < ROCK_STONES; stone++)
        for (int kind = 0; kind < ROCK_KINDS; kind++)
        for (int var = 0; var < ROCK_VARIANTS; var++) {
            uint32_t h = (uint32_t)(stone * 131 + kind * 17 + var * 7 + 1);
            auto rnd = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
            const int R = 16, H = 34;
            std::vector<uint8_t> g((size_t)(2 * R + 1) * (2 * R + 1) * H, 0);
            auto set = [&](int x, int y, int z) {
                if (x < -R || x > R || z < -R || z > R || y < 0 || y >= H) return;
                g[((size_t)(z + R) * (2 * R + 1) + (x + R)) * H + y] = 1;
            };
            auto filled = [&](int x, int y, int z) {
                if (x < -R || x > R || z < -R || z > R || y < 0 || y >= H) return false;
                return g[((size_t)(z + R) * (2 * R + 1) + (x + R)) * H + y] != 0;
            };
            float ox = var * 53.0f + kind * 11.0f, oz = stone * 37.0f;
            auto blob = [&](float cx, float cy, float cz, float rx, float ry, float rz, float rough) {
                for (int y = 0; y < H; y++) for (int z = -R; z <= R; z++) for (int x = -R; x <= R; x++) {
                    float dx = (x - cx) / rx, dy = (y - cy) / ry, dz = (z - cz) / rz;
                    float d = std::sqrt(dx * dx + dy * dy + dz * dz) + n3.GetNoise(x + ox, y * 1.3f, z + oz) * rough;
                    if (d < 1.0f) set(x, y, z);
                }
            };
            switch (kind) {
            case RockBoulder: {
                float r = 2.6f + rnd() * 1.6f;
                blob(0, r * 0.55f, 0, r, r * 0.75f, r * (0.8f + rnd() * 0.3f), 0.25f);
                break;
            }
            case RockBigBoulder: {
                float r = 4.5f + rnd() * 2.5f;
                blob(0, r * 0.6f, 0, r, r * 0.8f, r * (0.75f + rnd() * 0.3f), 0.3f);
                break;
            }
            case RockOutcrop: {
                int parts = 3 + (int)(rnd() * 3);
                for (int k = 0; k < parts; k++) {
                    float a = rnd() * 6.283f, d = rnd() * 5.0f;
                    float r = 3.5f + rnd() * 3.5f;
                    blob(std::cos(a) * d, r * (0.6f + rnd() * 0.8f), std::sin(a) * d, r, r * (0.9f + rnd() * 0.6f), r * 0.9f, 0.35f);
                }
                break;
            }
            case RockSpire: {
                float h0 = 14.0f + rnd() * 14.0f, r0 = 2.8f + rnd() * 1.8f;
                float lx = (rnd() - 0.5f) * 0.25f, lz = (rnd() - 0.5f) * 0.25f;
                for (int y = 0; y < (int)h0 && y < H; y++) {
                    float t = y / h0, r = r0 * (1.0f - 0.65f * t) + 0.6f;
                    float cx = lx * y, cz = lz * y;
                    for (int z = -R; z <= R; z++) for (int x = -R; x <= R; x++) {
                        float d = std::hypot(x - cx, z - cz) + n3.GetNoise(x + ox, y * 0.7f, z + oz) * 1.2f;
                        if (d < r) set(x, y, z);
                    }
                }
                break;
            }
            case RockLedge: {
                // a slab that juts out: x < 0 sinks into the slope,
                // x > 0 hangs over the drop. Variant = direction
                float L = 9.0f + (h % 5), W = 5.0f + (h % 3), T = 4.0f;
                for (int y = 0; y < 8; y++) for (int z = -R; z <= R; z++) for (int x = -R; x <= R; x++) {
                    float fx = (float)x, fz = (float)z;
                    for (int k = 0; k < var; k++) { float t = fx; fx = fz; fz = -t; }   // turn to face the variant's way
                    if (fx < -7.0f || fx > L) continue;
                    float tip = std::clamp((fx - L * 0.4f) / (L * 0.6f), 0.0f, 1.0f);
                    float w = W * (1.0f - 0.35f * tip) + n3.GetNoise(fx + ox, fz + oz, y * 0.5f) * 1.4f;
                    float top = T * (1.0f - 0.5f * tip) + 1.0f + n3.GetNoise(fx * 0.7f + oz, fz * 0.7f, ox) * 1.0f;
                    if (std::fabs(fz) < w && y < top) set(x, y, z);
                }
                break;
            }
            }
            // into a stamp: the top of each column gets the stone's
            // top block (moss on mossy stone)
            uint16_t body = stone == StoneVolcanic ? BLOCK_VOLCANIC_ROCK
                          : stone == StoneLimestone ? blockId("limestone", BLOCK_STONE)
                          : stone == StoneSandstone ? blockId("sandstone_orange", BLOCK_STONE) : BLOCK_STONE;
            uint16_t topB = stone == StoneMossy ? BLOCK_MOSS_STONE
                          : stone == StoneLimestone ? BLOCK_MOSS_STONE
                          : stone == StoneSandstone ? blockId("sandstone_cream", BLOCK_STONE) : body;
            TreeShape& sh = rockShapes[(size_t)rockIndex(stone, kind, var)];
            for (int y = 0; y < H; y++) for (int z = -R; z <= R; z++) for (int x = -R; x <= R; x++) {
                if (!filled(x, y, z)) continue;
                bool exposedTop = !filled(x, y + 1, z);
                sh.voxels.push_back({ (int8_t)x, (int8_t)z, (uint8_t)y, exposedTop ? topB : body });
                sh.height = std::max(sh.height, y + 1);
                sh.radius = std::max({ sh.radius, std::abs(x), std::abs(z) });
            }
        }
        maxRockRadius = 0;
        for (const TreeShape& t : rockShapes) maxRockRadius = std::max(maxRockRadius, t.radius);
    }

    // Steepness at a point (rise over run)
    float slopeAt(int wx, int wz) const {
        if (baked) return bake.slope((float)wx, (float)wz);
        float h = shape(wx, wz).h;
        return std::max(std::fabs(shape(wx + 3, wz).h - h), std::fabs(shape(wx, wz + 3).h - h)) / 3.0f;
    }

    // Where rocks stand: outcrops and overhanging ledges on steep
    // ground (the cliff mask), boulders on scree, and a few loose
    // boulders, spires and outcrops by biome
    bool rockInCell(int cellX, int cellZ, RockPlace& out) const override {
        using namespace island;
        const int margin = 3, span = ROCK_CELL - margin * 2;
        int wx = cellX * ROCK_CELL + margin + (int)(cellRandom(cellX, cellZ, 21) * span);
        int wz = cellZ * ROCK_CELL + margin + (int)(cellRandom(cellX, cellZ, 22) * span);
        const int W = chart.size * VOXEL_DIM;
        if (wx < 8 || wz < 8 || wx >= W - 8 || wz >= W - 8) return false;
        float roll = cellRandom(cellX, cellZ, 23), pick = cellRandom(cellX, cellZ, 24), var = cellRandom(cellX, cellZ, 25);
        roll /= std::max(gp.get("rocks"), 1e-4f);          // the Rocks slider
        if (roll > 0.6f) return false;                         // cheap early out: no cell rolls above this
        Shape sh = shape(wx, wz);
        int b = sh.biome;
        if (b == Village || b == PlainsVillage || b == HotSpring || b == Cave || b == MountainPass || isSeaBiome(b)) return false;
        float d, w, f;
        if (nearestSegment(roadSegs, (float)wx, (float)wz, sh.ni, sh.nj, d, w, f) && d < w + 5.0f) return false;
        if (nearestSegment(riverSegs, (float)wx, (float)wz, sh.ni, sh.nj, d, w, f) && d < w + 2.0f) return false;
        int eco = ecoAt(wx, wz, sh.h);
        const TileDef* t = tileFor(eco);
        if (!t || isSwampy(eco)) return false;
        if (sh.h <= SEA_LEVEL + 1) return false;
        if (baked) { float sf; bool lv; if (bake.lake((float)wx, (float)wz, sf, lv)) return false; }
        float slope = slopeAt(wx, wz);
        float change = baked ? bake.change((float)wx, (float)wz) : 0.0f;

        int stone = eco == Ashlands ? StoneVolcanic : eco == Karst ? StoneLimestone : eco == Canyons ? StoneSandstone
                  : eco == AutumnUplands ? StoneMossy
                  : (eco == Forest || eco == DenseForest || eco == Shrubland || eco == HighlandMoor || eco == Riparian ||
                     eco == Plains || eco == Grassland || eco == Marsh) ? StoneMossy : StonePlain;
        bool cliff = (slope > 1.1f && t->lvHi >= 3) || (eco == SeaCliffs && slope > 0.8f);
        bool scree = change > 2.0f && slope > 0.3f;
        int kind = -1;
        if (cliff) {
            if (roll < 0.55f) kind = (slope > 1.45f && pick < 0.5f) ? RockLedge : RockOutcrop;
        } else if (scree) {
            if (roll < 0.35f) kind = pick < 0.3f ? RockBigBoulder : RockBoulder;
        } else {
            float chance = 0.0f;
            switch (eco) {
            case Karst:     chance = 0.12f; break;
            case Ashlands:  chance = 0.06f; break;
            case AutumnUplands: chance = 0.03f; break;
            case HighlandMoor: chance = 0.10f; break;
            case Canyons:    chance = 0.06f; break;
            case Foothills:    chance = 0.07f; break;
            case LowMountain: case MidMountain: case HighMountain: chance = 0.12f; break;
            case Plateau:      chance = 0.05f; break;
            case Shrubland:    chance = 0.03f; break;
            case Forest: case DenseForest: chance = 0.025f; break;
            case Plains: case Grassland: chance = 0.012f; break;
            case Beach:        chance = 0.01f; break;
            default: break;
            }
            if (roll < chance) {
                if (eco == Karst && pick < 0.25f) kind = RockOutcrop;
                else if ((eco == HighlandMoor || t->lvLo >= 5) && pick < 0.35f) kind = RockOutcrop;
                else kind = pick < 0.75f ? RockBoulder : RockBigBoulder;
            }
        }
        if (kind < 0) return false;

        int variant = (int)(var * ROCK_VARIANTS) % ROCK_VARIANTS;
        int ground = (int)std::floor(sh.h);
        if (kind == RockLedge) {
            // face the way the ground falls away
            float gx = shape(wx + 4, wz).h - shape(wx - 4, wz).h;
            float gz = shape(wx, wz + 4).h - shape(wx, wz - 4).h;
            // variant 0 juts +X, 1 +Z, 2 −X, 3 −Z (see buildRocks)
            if (std::fabs(gx) > std::fabs(gz)) variant = gx < 0 ? 0 : 2;
            else                               variant = gz < 0 ? 1 : 3;
        }
        const TreeShape& rs = rockShapes[(size_t)rockIndex(stone, kind, variant)];
        int sink = kind == RockLedge ? 1 : kind == RockSpire ? 2 : (int)(rs.height * (kind == RockOutcrop ? 0.3f : 0.35f));
        out.shape = rockIndex(stone, kind, variant);
        out.wx = wx; out.wz = wz;
        out.wy = ground - sink;
        return out.wy + rs.height <= WORLD_HEIGHT;
    }

    // ── Overhangs and alcoves ─────────────────────────────
    // On steep rock faces, pockets are hollowed into the face a
    // few voxels below the top, under a solid lip: the lip hangs
    // out over them. Only voxels near the open side of the face
    // are taken, so nothing is left floating.
    void carveSection(uint16_t* vox, int x0, int z0, int y0,
                      const std::vector<TerrainColumn>& cols) const override {
        if (!baked) return;
        const int D = VOXEL_DIM;
        // real ground (with its fine bumps): this chunk's columns,
        // or worked out for points just outside it
        auto groundAt = [&](float fx, float fz) {
            int lx = (int)std::floor(fx) - x0, lz = (int)std::floor(fz) - z0;
            if (lx >= 0 && lz >= 0 && lx < D && lz < D) return (float)cols[(size_t)lx * D + lz].ground;
            Shape o; bakedShape((int)std::floor(fx), (int)std::floor(fz), o);
            return std::floor(o.h);
        };
        std::vector<int> carved;            // voxel indices taken out
        for (int x = 0; x < D; x++)
        for (int z = 0; z < D; z++) {
            const TerrainColumn& c = cols[(size_t)x * D + z];
            if (c.top != BLOCK_STONE && c.top != BLOCK_VOLCANIC_ROCK && c.top != BLOCK_MOSS_STONE) continue;
            if (c.waterLevel > c.ground) continue;
            const float wx = (float)(x0 + x), wz = (float)(z0 + z);
            // face direction: the way the ground falls
            float gx = bake.height(wx + 3, wz) - bake.height(wx - 3, wz);
            float gz = bake.height(wx, wz + 3) - bake.height(wx, wz - 3);
            float glen = std::sqrt(gx * gx + gz * gz);
            if (glen / 6.0f < 1.0f) continue;                  // not steep enough
            float dx = -gx / glen, dz = -gz / glen;
            int g = c.ground;
            int yTop = g - 4;                                  // a solid lip at least 4 thick
            int yBot = std::max((int)bake.height(wx + dx * 10, wz + dz * 10) + 2, SEA_LEVEL + 3);
            if (yTop <= yBot || yTop < y0 || yBot >= y0 + D) continue;
            for (int y = std::max(yBot, y0); y <= std::min(yTop, y0 + D - 1); y++) {
                // how far in from the open air this voxel sits
                int dist = 99;
                for (int d = 1; d <= 8; d++)
                    if (groundAt(wx + dx * d, wz + dz * d) <= (float)y) { dist = d; break; }
                if (dist > 8) continue;
                // pocket depth from slow 3D noise: wide, few alcoves
                float n = overhangN.GetNoise(wx * 0.45f, y * 0.8f, wz * 0.45f);
                float depth = std::clamp((n - 0.2f) * 16.0f, 0.0f, 7.0f) * gp.get("alcoves");
                // taper at the top and bottom of the pocket
                float v = std::min((yTop - y) / 3.0f, (y - yBot) / 2.0f);
                depth *= std::clamp(v, 0.0f, 1.0f);
                if (dist <= depth) {
                    int vi = Chunk::index(x, y - y0, z);
                    if (vox[vi] != BLOCK_AIR) { vox[vi] = BLOCK_AIR; carved.push_back(vi); }
                }
            }
        }
        if (carved.empty()) return;
        // Keep only pockets that open onto the air outside the
        // rock; anything sealed off is filled back in
        std::vector<uint8_t> state((size_t)D * D * D, 0);   // 1 carved, 2 carved and open
        for (int vi : carved) state[vi] = 1;
        std::vector<int> q;
        auto decode = [&](int vi, int& x, int& y, int& z) {   // Chunk::index: (y * D + x) * D + z
            z = vi % D; x = (vi / D) % D; y = vi / (D * D);
        };
        static const int N6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int vi : carved) {
            int x, y, z; decode(vi, x, y, z);
            for (auto& o : N6) {
                int a = x + o[0], b = y + o[1], cz = z + o[2];
                if (a < 0 || cz < 0 || a >= D || cz >= D || b < 0 || b >= D) continue;
                int j = Chunk::index(a, b, cz);
                // open air: not carved, and above that column's ground
                if (state[j] == 0 && vox[j] == BLOCK_AIR && y0 + b >= cols[(size_t)a * D + cz].ground) {
                    state[vi] = 2; q.push_back(vi); break;
                }
            }
        }
        for (size_t h = 0; h < q.size(); h++) {
            int x, y, z; decode(q[h], x, y, z);
            for (auto& o : N6) {
                int a = x + o[0], b = y + o[1], cz = z + o[2];
                if (a < 0 || cz < 0 || a >= D || cz >= D || b < 0 || b >= D) continue;
                int j = Chunk::index(a, b, cz);
                if (state[j] == 1) { state[j] = 2; q.push_back(j); }
            }
        }
        for (int vi : carved) if (state[vi] == 1) vox[vi] = cols[0].deep == BLOCK_VOLCANIC_ROCK ? BLOCK_VOLCANIC_ROCK : BLOCK_STONE;
    }

    // ── Decorations (world/Decorations.h) ─────────────────
    // Bushes, logs, stones and the like: which one a 10-voxel
    // cell holds, from decorations.toml's [[decor]] entries
    bool decorInCell(int cellX, int cellZ, DecorPlace& out) const override {
        using namespace island;
        if (!decorSet || largeScale <= 0.0f) return false;
        const int margin = 1, span = DECOR_CELL - margin * 2;
        float roll = cellRandom(cellX, cellZ, 31) / largeScale;
        if (roll > 0.6f) return false;
        int wx = cellX * DECOR_CELL + margin + (int)(cellRandom(cellX, cellZ, 32) * span);
        int wz = cellZ * DECOR_CELL + margin + (int)(cellRandom(cellX, cellZ, 33) * span);
        const int W = chart.size * VOXEL_DIM;
        if (wx < 4 || wz < 4 || wx >= W - 4 || wz >= W - 4) return false;
        TerrainColumn c = column(wx, wz);
        if (c.waterLevel > c.ground || c.deckY >= 0) return false;
        int eco = c.eco;
        const auto& list = decorSet->list(decor::Layer::Large, eco);
        if (list.empty()) return false;
        float acc = 0.0f;
        int pick = -1;
        for (const auto& [di, dens] : list) { acc += dens; if (roll < acc) { pick = di; break; } }
        if (pick < 0) return false;
        const decor::Def& d = decorSet->defs[(size_t)pick];
        if (d.variants.empty()) return false;
        if (std::find(d.on.begin(), d.on.end(), c.top) == d.on.end()) return false;
        Shape sh = shape(wx, wz);
        int b = sh.biome;
        if (b == Village || b == PlainsVillage || b == HotSpring || b == Cave || b == MountainPass) return false;
        float dd, w, f;
        if (nearestSegment(roadSegs, (float)wx, (float)wz, sh.ni, sh.nj, dd, w, f) && dd < w + 2.0f) return false;
        if (slopeAt(wx, wz) > d.maxSlope) return false;
        if (isSwampy(eco) && c.ground < SEA_LEVEL + 3 && d.recipe != "log") return false;   // hummocks clear of the water
        if (nearLandmark(wx, wz, 3)) return false;
        int var = (int)(cellRandom(cellX, cellZ, 35) * d.variants.size()) % (int)d.variants.size();
        out.shape = &d.variants[(size_t)var];
        out.wx = wx; out.wz = wz; out.wy = c.ground;
        return out.wy + out.shape->top <= WORLD_HEIGHT;
    }

    // Landmarks keep off roads, villages and other set pieces
    bool landmarkSpotOk(int wx, int wz) const override {
        using namespace island;
        const int W = chart.size * VOXEL_DIM;
        if (wx < 40 || wz < 40 || wx >= W - 40 || wz >= W - 40) return false;
        Shape sh = shape(wx, wz);
        int b = sh.biome;
        if (b == Village || b == PlainsVillage || b == HotSpring || b == Cave || b == MountainPass ||
            b == Road || b == Bridge) return false;
        float dd, w, f;
        if (nearestSegment(roadSegs, (float)wx, (float)wz, sh.ni, sh.nj, dd, w, f) && dd < w + 24.0f) return false;
        if (nearestSegment(riverSegs, (float)wx, (float)wz, sh.ni, sh.nj, dd, w, f) && dd < w + 10.0f) return false;
        const island::Marker* s = chart.marker("spawn");
        if (s) {
            float sx = s->x * VOXEL_DIM + VOXEL_DIM / 2.0f, sz = s->y * VOXEL_DIM + VOXEL_DIM / 2.0f;
            if (std::hypot(wx - sx, wz - sz) < 80.0f) return false;      // the spawn has its own
        }
        return true;
    }

    // The spawn willow: a few steps from where players appear, on
    // the first dry, level spot in a ring around it
    bool spawnLandmarkSpot(int& wx, int& wz) const override {
        glm::ivec3 sp = spawnPoint(chart.size, chart.size);
        for (int ring : { 11, 14, 18, 23 })
            for (int k = 0; k < 12; k++) {
                float a = k * 0.5236f + 0.3f;
                int x = sp.x + (int)std::round(std::cos(a) * ring), z = sp.z + (int)std::round(std::sin(a) * ring);
                TerrainColumn c = column(x, z);
                if (c.waterLevel > c.ground || c.deckY >= 0 || c.top == BLOCK_COBBLESTONE) continue;
                int hi = c.ground, lo = c.ground;
                bool wet = false;
                for (int q = 0; q < 4; q++) {
                    TerrainColumn o = column(x + (q == 0 ? 5 : q == 1 ? -5 : 0), z + (q == 2 ? 5 : q == 3 ? -5 : 0));
                    wet |= o.waterLevel > o.ground;
                    hi = std::max(hi, o.ground); lo = std::min(lo, o.ground);
                }
                if (wet || hi - lo > 4) continue;
                wx = x; wz = z;
                return true;
            }
        return false;
    }

public:
    // Canyon river segments (x0, z0, x1, z1, floor0, floor1), for tests and tools
    std::vector<std::array<float, 6>> canyonLines() const {
        std::vector<std::array<float, 6>> v;
        for (const auto& list : canyonSegs) for (const Seg& sg : list) {
            std::array<float, 6> e{ sg.ax, sg.az, sg.bx, sg.bz, sg.ha, sg.hb };
            if (std::find(v.begin(), v.end(), e) == v.end()) v.push_back(e);
        }
        return v;
    }
    const TerrainBake& getBake() const { return bake; }
    bool isBaked() const { return baked; }

private:
    WorldGenParams gp;
    island::Chart chart;
    FastNoiseLite warpX, warpZ, detail, mix, hills, ridges, reliefN, overhangN, autumnN;
    std::vector<glm::vec2> lavaCells;    // centres of lava lake cells (world voxels)
    Options options;
    TerrainBake bake;
    bool baked = false;

    // The ground levelled round each village site (column()): worked
    // out once, the first time it's asked for (worker threads too)
    static constexpr float VILLAGE_FLAT = 110.0f, VILLAGE_EASE = 60.0f;
    struct VillageGround { float x, z, flat; };
    mutable std::once_flag villageOnce;
    mutable std::vector<VillageGround> villageGroundList;
    const std::vector<VillageGround>& villageGround() const {
        std::call_once(villageOnce, [this] {
            for (const VillageSite& s : villageSites()) {
                int ci = std::clamp(s.at.x / VOXEL_DIM, 0, chart.size - 1), cj = std::clamp(s.at.y / VOXEL_DIM, 0, chart.size - 1);
                float flat = baked ? bake.height((float)s.at.x, (float)s.at.y) : cellHeight[cellIndex(ci, cj)];
                villageGroundList.push_back({ (float)s.at.x, (float)s.at.y, flat });
            }
        });
        return villageGroundList;
    }
    std::vector<TerrainBake::RiverLine> guideLines;   // designed rivers, smoothed
    struct Fall { float x, z, dx, dz, top, bottom, hw; };
    std::vector<Fall> falls;
    std::vector<std::vector<int>> fallTable;           // per chart cell: falls nearby


    // Per chart cell
    std::vector<float> cellHeight;   // ground height (voxels)
    std::vector<float> lakeLevel;    // lake surface, or -1

    // Line segments (world voxels) for rivers and roads,
    // listed under every chart cell they pass near
    struct Seg { float ax, az, bx, bz, halfWidth, ha, hb; };   // ha/hb: valley floor at each end
    std::vector<std::vector<Seg>> riverSegs, roadSegs;
    // Canyons: their rivers' paths, listed under nearby cells like
    // riverSegs, but kept after the bake (the ground is cut along
    // them every time a column is built)
    std::vector<std::vector<Seg>> canyonSegs;
    size_t canyonGuides = 0;                 // the first guideLines are the canyons' rivers
    std::vector<uint8_t> mesaCell;           // chart cells between the two canyons
    static constexpr float CANYON_REACH = 110.0f;   // furthest a canyon wall reaches from its river

    int cellIndex(int ci, int cj) const {
        ci = std::clamp(ci, 0, chart.size - 1);
        cj = std::clamp(cj, 0, chart.size - 1);
        return cj * chart.size + ci;
    }

    static bool isRiver(int b) {
        using namespace island;
        return b == MountainRiver || b == LowlandRiver || b == SwampRiver ||
               b == Delta || b == RiverFork;
    }
    static bool isLake(int b) {
        using namespace island;
        return b == MountainLake || b == LowMountainLake || b == PlainsLake ||
               b == ForestLake || b == LavaLake;
    }
    static bool isRoad(int b) {
        using namespace island;
        return b == Road || b == Bridge || b == MountainPass;
    }
    static bool isSea(int b) {
        using namespace island;
        return b == DeepOcean || b == Ocean || b == Shallows ||
               b == RockyShallows || b == SwampShallows;
    }
    static bool flatBiome(int b) {
        using namespace island;
        return b == Village || b == PlainsVillage || b == Beach || b == SwampBeach ||
               b == MarshBeach || b == Swamp || b == Marsh || isLake(b) || isRiver(b);
    }
    static bool mountainBiome(int b) {
        using namespace island;
        return b == LowMountain || b == MidMountain || b == HighMountain ||
               b == Plateau || b == Peak || b == SeaCliffs;
    }

    // Chart height (0..1) → voxels. Low ground stays low, so
    // coasts are gentle and the mountain rises steeply.
    static float landHeight(float e) {
        return SEA_LEVEL + 2.0f + std::pow(std::max(e, 0.0f), 1.6f) *
               (float)(PEAK_HEIGHT - SEA_LEVEL - 2);
    }

    static float smooth01(float a, float b, float x) {
        float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // ── The shape of the ground ──────────────────────────
    // Everything about a column's height that does not depend
    // on its materials. Also used at neighbouring points to
    // measure the slope.
    struct Shape {
        float h = 0;                       // ground height (voxels)
        int   ni = 0, nj = 0, biome = 0;   // nearest chart cell
        float lakeW = 0, lakeLvl = -1;
        uint16_t lakeFluid = BLOCK_WATER;
        float canyon = 0;                  // how far a canyon cut the ground down here (voxels)
        uint8_t canyonPart = 0;            // 0 outside, 1 floor, 2 wall, 3 ledge
    };

    //   forBake  the ground before erosion: no fine bumps, and
    //            (guided rivers) only a shallow guide channel
    Shape shape(int wx, int wz, bool forBake = false) const {
        using namespace island;
        Shape out;
        const int S = chart.size;
        if (baked && !forBake) { bakedShape(wx, wz, out); return out; }

        // Warped position in chart cells
        float u = (wx + warpX.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        float v = (wz + warpZ.GetNoise((float)wx, (float)wz) * WARP) / (float)VOXEL_DIM;
        float fu = u - 0.5f, fv = v - 0.5f;
        int   i0 = (int)std::floor(fu), j0 = (int)std::floor(fv);
        float tu = fu - i0, tv = fv - j0;

        // ── Catmull-Rom over the 4 × 4 nearest cell centres:
        // smooth slopes with no creases at the cell edges.
        // Clamped to the four nearest cells so a sudden drop
        // (cliff to sea) cannot overshoot into a bump.
        auto cr = [](float t, float w[4]) {
            float t2 = t * t, t3 = t2 * t;
            w[0] = 0.5f * (-t3 + 2 * t2 - t);
            w[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
            w[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
            w[3] = 0.5f * (t3 - t2);
        };
        float wu[4], wv[4];
        cr(tu, wu); cr(tv, wv);
        float h = 0.0f;
        for (int b = 0; b < 4; b++)
            for (int a = 0; a < 4; a++)
                h += wu[a] * wv[b] * cellHeight[cellIndex(i0 - 1 + a, j0 - 1 + b)];
        float lo = 1e9f, hi = -1e9f;
        float cliffW = 0.0f, cliffTop = 0.0f;
        float su = tu * tu * (3 - 2 * tu), sv = tv * tv * (3 - 2 * tv);
        for (int k = 0; k < 4; k++) {
            int ci = i0 + (k & 1), cj = j0 + (k >> 1);
            int idx = cellIndex(ci, cj);
            float w = ((k & 1) ? su : 1 - su) * ((k >> 1) ? sv : 1 - sv);
            lo = std::min(lo, cellHeight[idx]);
            hi = std::max(hi, cellHeight[idx]);
            if (lakeLevel[idx] >= 0) {
                out.lakeW += w;
                if (lakeLevel[idx] > out.lakeLvl) {
                    out.lakeLvl = lakeLevel[idx];
                    out.lakeFluid = chart.biomes[idx] == LavaLake ? BLOCK_LAVA : BLOCK_WATER;
                }
            }
            if (chart.base[idx] == SeaCliffs) {
                cliffW  += w;
                cliffTop = std::max(cliffTop, cellHeight[idx]);
            }
        }
        h = std::clamp(h, lo, hi);

        out.ni = std::clamp((int)std::floor(u), 0, S - 1);
        out.nj = std::clamp((int)std::floor(v), 0, S - 1);
        out.biome = chart.at(out.ni, out.nj);
        const int biome = out.biome;

        // ── Sea cliffs: squeeze the slope from the sea up to
        // the clifftop into a steep face
        if (cliffW > 0.0f && cliffTop > SEA_LEVEL) {
            float base = SEA_LEVEL - 12.0f;
            float t = std::clamp((h - base) / (cliffTop - base), 0.0f, 1.0f);
            float sharp = base + smooth01(0.38f, 0.62f, t) * (cliffTop - base);
            h += (sharp - h) * std::min(1.0f, cliffW * 2.0f);
        }

        // ── Each biome's relief, blended across borders ──
        // The relief noise is spread evenly over 0..1; each
        // biome's curve reshapes it (BiomeTable.h). The four
        // nearest cells are mixed smoothly, so a mountain's
        // crags fade out into the foothills' rolling ground.
        {
            float fx = (float)wx, fz = (float)wz;
            float nRel = std::clamp(0.5f + reliefN.GetNoise(fx, fz) * 0.85f, 0.0f, 1.0f);
            float rRid = (ridges.GetNoise(fx, fz) + 1.0f) * 0.5f;
            float rel = 0.0f, amp = 0.0f;
            for (int k = 0; k < 4; k++) {
                int idx = cellIndex(i0 + (k & 1), j0 + (k >> 1));
                float w = ((k & 1) ? su : 1 - su) * ((k >> 1) ? sv : 1 - sv);
                const TileDef* t = tileFor(chart.base[idx]);
                if (!t) continue;
                float damp = 1.0f;
                int ov = chart.biomes[idx];
                if (ov == Village || ov == PlainsVillage || ov == HotSpring || ov == Cave) damp = 0.3f;
                else if (ov == Riparian) damp = 0.6f;
                float scale = gp.get("hills") * (t->curve == Relief::Ridged ? gp.get("relief_mtn") : 1.0f);
                rel += w * damp * scale * reliefHeight(*t, nRel, rRid);
                amp += w * damp * t->relief;
            }
            h += rel;
            // small bumps everywhere, more where the land is rough
            if (!forBake) h += detail.GetNoise(fx, fz) * std::clamp(amp * 0.02f, 0.5f, 3.0f);
        }

        // ── River valleys ────────────────────────────────
        // Every river segment nearby cuts its own valley, one
        // after another, so where two segments meet the ground
        // stays continuous
        if (h > SEA_LEVEL) {
            for (const Seg& sg : riverSegs[cellIndex(out.ni, out.nj)]) {
                float t, d = segDistance(sg, (float)wx, (float)wz, t);
                float floorH = sg.ha + (sg.hb - sg.ha) * t;
                if (h <= floorH) continue;
                // before erosion only a guide: a narrower trough that
                // collects the rain, which then cuts the real valley
                float valley = forBake ? std::min(sg.halfWidth + 30.0f + 1.0f * (h - floorH), 160.0f)
                                       : std::min(sg.halfWidth + 60.0f + 2.2f * (h - floorH), 420.0f);
                if (d < valley) h = floorH + (h - floorH) * smooth01(sg.halfWidth, valley, d);
            }
        }

        // ── Canyons ───────────────────────────────────────
        { float cut; h = canyonCut(wx, wz, out.ni, out.nj, h, forBake, cut, &out.canyonPart); out.canyon = cut; }

        // ── Lake basins ──────────────────────────────────
        // The crags around a mountain lake can dip below its
        // surface, which would leave a wall of standing water.
        // So the ground near a lake rises to a rim just above
        // the water, and inside the lake it drops into a bowl.
        // The shore follows one profile: just above the water
        // at the edge of the lake's area, sloping down to the
        // lake bed in the middle. Low ground is raised to it
        // near the edge; high ground is cut down to it in a
        // sloped bank.
        if (out.lakeW > 0.0f && out.lakeLvl > 0.0f) {
            float w = out.lakeW, lvl = out.lakeLvl;
            float profile = lvl + 2.0f - 17.0f * smooth01(0.3f, 0.9f, w);
            if (h < profile && w <= 0.45f)
                h += (profile - h) * smooth01(0.0f, 0.2f, w);
            else if (h > profile)
                h += (profile - h) * smooth01(0.02f, 0.5f, w);
        }

        // Soft cap: the summit rounds off below the sky
        const float knee = MAX_GROUND - 60.0f;
        if (h > knee) h = knee + (h - knee) * 0.4f;
        out.h = std::min(h, (float)MAX_GROUND);
        return out;
    }

    void prepare() {
        using namespace island;
        const int S = chart.size, n = S * S;
        lavaCells.clear();
        for (int i = 0; i < n; i++)
            if (chart.biomes[i] == LavaLake)
                lavaCells.push_back({ (i % S + 0.5f) * VOXEL_DIM, (i / S + 0.5f) * VOXEL_DIM });
        autumnN.SetSeed(seed + 1111);
        autumnN.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        autumnN.SetFrequency(1.0f / 380.0f);
        autumnN.SetFractalType(FastNoiseLite::FractalType_FBm);
        autumnN.SetFractalOctaves(2);
        cellHeight.assign(n, 0.0f);
        lakeLevel.assign(n, -1.0f);
        if (chart.collapsed) cellHeight = chart.height;   // already smoothed into each biome's range
        else for (int i = 0; i < n; i++) {
            int b = chart.biomes[i];
            float h;
            switch (b) {
            case DeepOcean: h = SEA_LEVEL - 110.0f; break;
            case Ocean:     h = SEA_LEVEL - 60.0f; break;
            case Shallows: case RockyShallows: case SwampShallows:
                            h = SEA_LEVEL - 14.0f; break;
            case Beach: case SwampBeach: case MarshBeach:
                            h = SEA_LEVEL + 3.0f; break;
            case Swamp: case Marsh:
                            h = SEA_LEVEL + 2.0f; break;
            case SeaCliffs: h = landHeight(chart.elev[i]) + 75.0f; break;   // ~25 ft of extra cliff
            default:        h = landHeight(chart.elev[i]); break;
            }
            cellHeight[i] = h;
        }
        // Soften the steps between inland cells (one blur
        // pass), leaving the coast, flat biomes and cliffs alone
        if (!chart.collapsed) {
            std::vector<float> blurred = cellHeight;
            for (int i = 0; i < n; i++) {
                int b = chart.biomes[i];
                if (isSea(b) || flatBiome(b) || b == SeaCliffs) continue;
                float sum = 0; int cnt = 0;
                for (int nb : neighbors(S, i)) {
                    int bb = chart.biomes[nb];
                    if (isSea(bb) || flatBiome(bb)) continue;
                    sum += cellHeight[nb]; cnt++;
                }
                if (cnt) blurred[i] = 0.5f * cellHeight[i] + 0.5f * sum / cnt;
            }
            cellHeight.swap(blurred);
        }
        // Lakes: one water level for each whole lake (all its
        // connected cells), a little below the land around it.
        // A quarter of the shore may be lower than the water;
        // the ground there is raised into a rim (see shape()),
        // so the lake never spills or stands as a wall.
        {
            std::vector<int> comp(n, -1);
            for (int start = 0; start < n; start++) {
                if (!isLake(chart.biomes[start]) || comp[start] >= 0) continue;
                bool lava = chart.biomes[start] == LavaLake;
                std::vector<int> cells{ start }, stack{ start };
                std::vector<float> shore;
                comp[start] = start;
                while (!stack.empty()) {
                    int c = stack.back(); stack.pop_back();
                    for (int nb : neighbors(S, c)) {
                        int b = chart.biomes[nb];
                        if (isLake(b) && (b == LavaLake) == lava) {
                            if (comp[nb] < 0) { comp[nb] = start; cells.push_back(nb); stack.push_back(nb); }
                        } else if (!isLake(b) && !isSea(b)) {
                            shore.push_back(cellHeight[nb]);
                        }
                    }
                }
                float lvl;
                if (!shore.empty()) {
                    std::sort(shore.begin(), shore.end());
                    lvl = shore[shore.size() / 4] - 2.0f;
                } else {
                    lvl = 1e9f;
                    for (int c : cells) lvl = std::min(lvl, cellHeight[c]);
                }
                lvl = std::max((float)SEA_LEVEL + 1.0f, lvl);
                for (int c : cells) lakeLevel[c] = lvl;
            }
        }
        std::vector<float> landH = cellHeight;   // before lake beds, for rivers
        for (int i = 0; i < n; i++)
            if (lakeLevel[i] >= 0) cellHeight[i] = lakeLevel[i] - 16.0f;

        // Segments between neighbouring cells of the same
        // network (each pair once), plus spurs into the lakes
        // and sea that rivers flow into
        riverSegs.assign(n, {});
        roadSegs.assign(n, {});
        auto centre = [&](int i, float& x, float& z) {
            x = (i % S + 0.5f) * VOXEL_DIM;
            z = (i / S + 0.5f) * VOXEL_DIM;
        };
        // Valley floor at a cell: the land there, or just above
        // the water a river runs into
        auto floorAt = [&](int i) {
            int b = chart.biomes[i];
            if (isSea(b)) return SEA_LEVEL + 3.0f;
            if (lakeLevel[i] >= 0) return lakeLevel[i] + 2.0f;
            return landH[i];
        };
        auto addSeg = [&](std::vector<std::vector<Seg>>& table, int a, int b, float hw) {
            Seg s;
            centre(a, s.ax, s.az);
            centre(b, s.bx, s.bz);
            s.halfWidth = hw;
            s.ha = floorAt(a);
            s.hb = floorAt(b);
            // list it under both cells and their neighbours, so
            // a lookup in any nearby cell finds it
            for (int c : { a, b }) {
                table[c].push_back(s);
                for (int nb : neighbors(S, c)) table[nb].push_back(s);
            }
        };
        auto riverWidth = [&](int b) {
            switch (b) {
            case MountainRiver: return 9.0f;    // ~6 ft across
            case SwampRiver:    return 12.0f;
            case Delta:         return 24.0f;
            default:            return 14.0f;   // ~9 ft across
            }
        };
        if (chart.collapsed) {
            // Straight from the chart's paths: each river runs
            // cell to cell from source to mouth, the road from
            // village to hamlet
            // The cell-to-cell path is smoothed into a curve
            // (Chaikin corner cutting, twice) so rivers and the
            // road bend instead of turning at right angles
            struct P { float x, z, floor, hw; };
            auto smoothPath = [&](const std::vector<int>& path, bool river,
                                  std::vector<std::vector<Seg>>& table,
                                  const std::vector<float>* floors = nullptr) {
                if (path.size() < 2) return;
                std::vector<P> pts;
                for (size_t k = 0; k < path.size(); k++) {
                    int c = path[k];
                    P p; centre(c, p.x, p.z);
                    p.floor = floors ? (*floors)[k] : floorAt(c);
                    p.hw = floors ? 10.0f : river ? riverWidth(chart.biomes[c]) : 7.0f;
                    // wander off the cell centres (not at the ends),
                    // so long straight runs bend
                    if (k > 0 && k + 1 < path.size()) {
                        float amp = river ? 38.0f : 24.0f;
                        p.x += warpX.GetNoise(p.x * 3.1f, p.z * 3.1f) * amp;
                        p.z += warpZ.GetNoise(p.x * 3.1f, p.z * 3.1f) * amp;
                    }
                    pts.push_back(p);
                }
                // and a midpoint in each step, also wandering, so
                // even a straight run of cells curves
                {
                    std::vector<P> dense{ pts.front() };
                    for (size_t k = 0; k + 1 < pts.size(); k++) {
                        P m{ (pts[k].x + pts[k + 1].x) * 0.5f, (pts[k].z + pts[k + 1].z) * 0.5f,
                             (pts[k].floor + pts[k + 1].floor) * 0.5f, std::max(pts[k].hw, pts[k + 1].hw) };
                        float dx = pts[k + 1].x - pts[k].x, dz = pts[k + 1].z - pts[k].z;
                        float len = std::max(1.0f, std::sqrt(dx * dx + dz * dz));
                        float side = mix.GetNoise(m.x * 0.7f, m.z * 0.7f) * (river ? 34.0f : 18.0f);
                        m.x += -dz / len * side; m.z += dx / len * side;
                        dense.push_back(m);
                        dense.push_back(pts[k + 1]);
                    }
                    pts.swap(dense);
                }
                for (int it = 0; it < 2; it++) {
                    std::vector<P> out{ pts.front() };
                    for (size_t k = 0; k + 1 < pts.size(); k++) {
                        const P &a = pts[k], &b = pts[k + 1];
                        auto lerp = [&](float t) {
                            return P{ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t,
                                      a.floor + (b.floor - a.floor) * t, std::max(a.hw, b.hw) };
                        };
                        out.push_back(lerp(0.25f));
                        out.push_back(lerp(0.75f));
                    }
                    out.push_back(pts.back());
                    pts.swap(out);
                }
                // a river that reaches a canyon ends at its rim: it pours in,
                // it never runs across
                if (river && !floors)
                    for (size_t k = 0; k < pts.size(); k++)
                        if (crossesCanyon(pts[k].x, pts[k].z, pts[k].floor, pts[k].hw + 8.0f)) { pts.resize(std::max<size_t>(k, 2)); break; }
                if (river) {
                    TerrainBake::RiverLine g;
                    for (const P& p : pts) g.push_back({ p.x, p.z, p.floor, p.hw });
                    guideLines.push_back(std::move(g));
                }
                for (size_t k = 0; k + 1 < pts.size(); k++) {
                    Seg sg{ pts[k].x, pts[k].z, pts[k + 1].x, pts[k + 1].z,
                            std::max(pts[k].hw, pts[k + 1].hw), pts[k].floor, pts[k + 1].floor };
                    // listed under every cell near either end
                    int ca = cellIndex((int)std::floor(sg.ax / VOXEL_DIM), (int)std::floor(sg.az / VOXEL_DIM));
                    int cb = cellIndex((int)std::floor(sg.bx / VOXEL_DIM), (int)std::floor(sg.bz / VOXEL_DIM));
                    std::vector<int> cells{ ca };
                    if (cb != ca) cells.push_back(cb);
                    for (int c : { ca, cb }) for (int nb : neighbors(S, c)) cells.push_back(nb);
                    std::sort(cells.begin(), cells.end());
                    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
                    for (int c : cells) table[c].push_back(sg);
                }
            };
            // canyons first (their own table; the guide lines carry their
            // water), so the other rivers can end where they meet one
            canyonSegs.assign(n, {});
            auto canyons = planCanyons(landH);
            for (const auto& cp : canyons) smoothPath(cp.first, true, canyonSegs, &cp.second);
            canyonGuides = guideLines.size();
            for (const auto& rp : chart.rivers) smoothPath(rp, true, riverSegs);
            smoothPath(chart.road, false, roadSegs);
            planMesa(canyons);
        }
        else for (int i = 0; i < n; i++) {
            int b = chart.biomes[i];
            if (isRiver(b)) {
                for (int nb : neighbors(S, i)) {
                    int nbB = chart.biomes[nb];
                    if (isRiver(nbB) && nb > i)
                        addSeg(riverSegs, i, nb, std::max(riverWidth(b), riverWidth(nbB)));
                    else if ((isLake(nbB) || isSea(nbB) || nbB == Swamp) && nb != i)
                        addSeg(riverSegs, i, nb, riverWidth(b));
                }
            }
            if (isRoad(b)) {
                for (int nb : neighbors(S, i)) {
                    int nbB = chart.biomes[nb];
                    if ((isRoad(nbB) && nb > i) || nbB == Village || nbB == PlainsVillage)
                        addSeg(roadSegs, i, nb, 7.0f);   // ~4.5 ft wide
                }
            }
        }
    }

    // ── Canyons ───────────────────────────────────────────
    // Up to three canyons through the uplands of the volcanic
    // quadrant (karst, canyon country, moor): each starts high, near
    // the middle of the island, and runs to the sea along the cheapest
    // mostly-downhill route over the chart. Returned as cell paths
    // with the floor height at each cell: cut deep in the middle
    // (up to ~55 voxels below the land), coming out at sea level.
    std::vector<std::pair<std::vector<int>, std::vector<float>>> planCanyons(const std::vector<float>& landH) const {
        using namespace island;
        std::vector<std::pair<std::vector<int>, std::vector<float>>> out;
        const int S = chart.size, n = S * S;
        auto upland = [&](int i) { int b = chart.base[i]; return b == Karst || b == Canyons || b == HighlandMoor; };
        auto passable = [&](int i) {
            int b = chart.biomes[i];
            if (isSeaBiome(b)) return true;
            return b != LavaLake && b != Village && b != PlainsVillage && b != HotSpring && b != Cave &&
                   b != MountainLake && b != LowMountainLake && b != PlainsLake && b != ForestLake && b != Road && b != Bridge;
        };
        // Dijkstra inland from the sea: climbing is free, dropping is
        // dear, so the route back out to sea is mostly downhill
        std::vector<float> cost(n, 1e30f);
        std::vector<int> pred(n, -1);
        using E = std::pair<float, int>;
        std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
        for (int i = 0; i < n; i++) if (isSeaBiome(chart.biomes[i])) { cost[i] = 0; pq.push({ 0.0f, i }); }
        while (!pq.empty()) {
            auto [c, i] = pq.top(); pq.pop();
            if (c > cost[i]) continue;
            for (int j : neighbors(S, i)) {
                if (!passable(j) || isSeaBiome(chart.biomes[j])) continue;
                float drop = std::max(0.0f, landH[i] - landH[j]);          // inland step going down = route going up
                float rivers = isRiver(chart.biomes[j]) ? 30.0f : 0.0f;       // keep apart from the designed rivers
                float nc = c + 1.0f + drop * 0.35f + rivers + (upland(j) ? 0.0f : 2.0f);
                if (nc < cost[j]) { cost[j] = nc; pred[j] = i; pq.push({ nc, j }); }
            }
        }
        // Exactly two canyons, side by side with land between them (the
        // mesa). Tried with strict rules first, then looser ones, so
        // every island of this type gets its two.
        struct Rules { int sepMin, sepMax, len, uplands; bool anyLand; };
        const Rules tries[] = { { 6, 16, 8, 5, false }, { 5, 20, 6, 3, false }, { 4, 26, 5, 2, false }, { 3, 40, 4, 1, true } };
        auto pathFrom = [&](int src) {
            std::vector<int> path;
            for (int c = src; c >= 0; c = pred[c]) {
                path.push_back(c);
                if (isSeaBiome(chart.biomes[c])) break;
            }
            return path;
        };
        auto minDist2 = [&](const std::vector<int>& a, const std::vector<int>& b) {
            int best = 1 << 30;
            for (int x : a) for (int y : b) {
                if (isSeaBiome(chart.biomes[x]) || isSeaBiome(chart.biomes[y])) continue;
                int dx = x % S - y % S, dz = x / S - y / S;
                best = std::min(best, dx * dx + dz * dz);
            }
            return best;
        };
        std::vector<std::vector<int>> chosen;
        for (const Rules& r : tries) {
            std::vector<int> cand;
            for (int i = 0; i < n; i++)
                if ((r.anyLand ? !isSeaBiome(chart.biomes[i]) : upland(i)) && chart.biomes[i] == chart.base[i] && cost[i] < 1e29f)
                    cand.push_back(i);
            std::sort(cand.begin(), cand.end(), [&](int a, int b) { return landH[a] > landH[b]; });
            auto ok = [&](const std::vector<int>& path) {
                int up = 0;
                for (int c : path) up += upland(c);
                return (int)path.size() >= r.len && up >= r.uplands;
            };
            chosen.clear();
            // the best pair: most open upland between the two (the mesa)
            std::vector<std::vector<int>> paths;
            for (size_t i = 0; i < cand.size() && paths.size() < 40; i++) {
                auto pa = pathFrom(cand[i]);
                if (ok(pa)) paths.push_back(pa);
            }
            std::vector<std::vector<int>> dist(paths.size());
            auto distOf = [&](size_t k) -> const std::vector<int>& {
                if (dist[k].empty()) {
                    dist[k].assign(n, 1 << 20);
                    std::vector<int> q;
                    for (int c : paths[k]) if (!isSeaBiome(chart.biomes[c])) { dist[k][c] = 0; q.push_back(c); }
                    for (size_t h = 0; h < q.size(); h++)
                        for (int j : neighbors(S, q[h]))
                            if (dist[k][j] > dist[k][q[h]] + 1 && !isSeaBiome(chart.biomes[j])) { dist[k][j] = dist[k][q[h]] + 1; q.push_back(j); }
                }
                return dist[k];
            };
            auto mesaGround = [&](int i) {
                int b = chart.base[i];
                if (b != Karst && b != Canyons && b != HighlandMoor && b != Foothills) return false;
                for (const auto& lc : lavaCells)
                    if (std::hypot((i % S + 0.5f) * VOXEL_DIM - lc.x, (i / S + 0.5f) * VOXEL_DIM - lc.y) < 4.5f * VOXEL_DIM) return false;
                return true;
            };
            int bestScore = -1;
            for (size_t a = 0; a < paths.size(); a++)
                for (size_t b = a + 1; b < paths.size(); b++) {
                    int sa = paths[a][0], sb = paths[b][0];
                    int dx = sa % S - sb % S, dz = sa / S - sb / S;
                    if (dx * dx + dz * dz > r.sepMax * r.sepMax) continue;
                    // the two may meet near the sea, but not up in the hills
                    std::vector<int> aUp(paths[a].begin(), paths[a].begin() + std::max<size_t>(1, paths[a].size() * 2 / 3));
                    std::vector<int> bUp(paths[b].begin(), paths[b].begin() + std::max<size_t>(1, paths[b].size() * 2 / 3));
                    if (minDist2(aUp, bUp) < r.sepMin * r.sepMin) continue;
                    const auto& dA = distOf(a); const auto& dB = distOf(b);
                    int gap = 1 << 20;
                    for (int i = 0; i < n; i++) gap = std::min(gap, dA[i] + dB[i]);
                    int score = 0;
                    for (int i = 0; i < n; i++)
                        if (dA[i] && dB[i] && dA[i] + dB[i] <= gap + 2 && dA[i] <= 9 && dB[i] <= 9 && mesaGround(i)) score++;
                    if (score > bestScore) { bestScore = score; chosen = { paths[a], paths[b] }; }
                }
            if (chosen.size() == 2 && bestScore < 12 && &r != &tries[3]) chosen.clear();   // try looser rules for a better mesa
            if (chosen.size() == 2) break;
        }
        for (auto& path : chosen) {
            // where the second canyon reaches the first, it ends (a fork)
            if (&path == &chosen[1]) {
                std::vector<int> cut;
                for (int c : path) {
                    cut.push_back(c);
                    if (std::find(chosen[0].begin(), chosen[0].end(), c) != chosen[0].end()) break;
                }
                path.swap(cut);
            }
            const size_t L = path.size();
            if (L < 2) continue;
            // floors: below the real ground along the route (the lowest
            // of a few samples round each cell centre), deep in the
            // middle, never rising downstream, out at the sea
            std::vector<float> fl(path.size());
            for (size_t k = 0; k < L; k++) {
                float cx = (path[k] % S + 0.5f) * VOXEL_DIM, cz = (path[k] / S + 0.5f) * VOXEL_DIM;
                float g = 1e9f;
                for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++)
                    g = std::min(g, shape((int)(cx + a * 36.0f), (int)(cz + b * 36.0f), true).h);
                float depth = std::min(70.0f, 16.0f + 9.0f * k) * std::clamp((float)(L - 1 - k) / 3.0f, 0.15f, 1.0f);
                float f = g - depth;
                if (k > 0) f = std::min(f, fl[k - 1] - 0.4f);
                fl[k] = std::max(f, (float)SEA_LEVEL + 3.0f);
            }
            fl[L - 1] = SEA_LEVEL + 3.0f;
            // a fork: it comes out on the first canyon's floor
            if (!out.empty() && !isSeaBiome(chart.biomes[path.back()])) {
                const auto& first = out[0];
                auto it = std::find(first.first.begin(), first.first.end(), path.back());
                if (it != first.first.end()) {
                    float joinF = first.second[(size_t)(it - first.first.begin())];
                    for (size_t k = 0; k < L; k++) fl[k] = std::max(fl[k], joinF + 0.4f * (L - 1 - k));
                    fl[L - 1] = joinF;
                }
            }
            out.push_back({ path, fl });
        }
        return out;
    }

    // A lake that a canyon cuts through can't hold its water: drained
    // (it becomes a dry hollow beside the canyon)
    void drainCanyonLakes() {
        if (canyonSegs.empty() || !bake.ready()) return;
        const int S = bake.size(), ST = TerrainBake::STEP;
        std::vector<uint8_t> seen((size_t)S * S, 0);
        int drained = 0;
        for (int z = 0; z < S; z++) for (int x = 0; x < S; x++) {
            size_t i = (size_t)z * S + x;
            if (seen[i] || !bake.sampleLake(x, z)) continue;
            std::vector<int> q{ (int)i };
            seen[i] = 1;
            bool cut = false;
            for (size_t h = 0; h < q.size(); h++) {
                int cx = q[h] % S, cz = q[h] / S;
                if (!cut && crossesCanyon((float)(cx * ST), (float)(cz * ST), bake.sampleWater(cx, cz), 4.0f)) cut = true;
                static const int N4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (auto& o : N4) {
                    int nx = cx + o[0], nz = cz + o[1];
                    if (nx < 0 || nz < 0 || nx >= S || nz >= S) continue;
                    size_t j = (size_t)nz * S + nx;
                    if (!seen[j] && bake.sampleLake(nx, nz)) { seen[j] = 1; q.push_back((int)j); }
                }
            }
            if (!cut) continue;
            drained++;
            for (int c : q) bake.clearLakeSample(c % S, c / S);
        }
        if (drained) std::cout << "Canyons drained " << drained << " lake(s)\n";
    }

    // Would water at this height, here, be running over a canyon
    // (above its floor, inside its walls)?
    //   margin  extra room (a river's own width, so its water stops short of the rim)
    bool crossesCanyon(float x, float z, float surface, float margin = 0.0f) const {
        if (canyonSegs.empty()) return false;
        int ci = std::clamp((int)(x / VOXEL_DIM), 0, chart.size - 1), cj = std::clamp((int)(z / VOXEL_DIM), 0, chart.size - 1);
        for (const Seg& sg : canyonSegs[cellIndex(ci, cj)]) {
            float t, d = segDistance(sg, x, z, t);
            float floorH = sg.ha + (sg.hb - sg.ha) * t;
            if (surface > floorH + 3.0f && d < sg.halfWidth + 13.0f + margin + 0.45f * (surface - floorH)) return true;
        }
        return false;
    }

    // The land between the two canyons: the canyon mesa, with a
    // village at its heart. Cells no more than a few cells from both
    // canyons and lying between them (the two distances add up to
    // little more than the gap between the canyons).
    void planMesa(const std::vector<std::pair<std::vector<int>, std::vector<float>>>& canyons) {
        using namespace island;
        const int S = chart.size, n = S * S;
        mesaCell.assign(n, 0);
        if (canyons.size() < 2) return;
        auto distFrom = [&](const std::vector<int>& path) {
            std::vector<int> d(n, 1 << 20);
            std::vector<int> q;
            for (int c : path) if (!isSeaBiome(chart.biomes[c])) { d[c] = 0; q.push_back(c); }
            for (size_t h = 0; h < q.size(); h++)
                for (int j : neighbors(S, q[h]))
                    if (d[j] > d[q[h]] + 1 && !isSeaBiome(chart.biomes[j])) { d[j] = d[q[h]] + 1; q.push_back(j); }
            return d;
        };
        auto dA = distFrom(canyons[0].first), dB = distFrom(canyons[1].first);
        int gap = 1 << 20;
        for (int i = 0; i < n; i++) gap = std::min(gap, dA[i] + dB[i]);
        int best = -1, bestScore = -1;
        for (int i = 0; i < n; i++) {
            if (isSeaBiome(chart.biomes[i]) || dA[i] == 0 || dB[i] == 0) continue;
            if (dA[i] + dB[i] > gap + 2 || dA[i] > 9 || dB[i] > 9) continue;
            { int b = chart.base[i];   // uplands only, not the mountains the canyons start in
              if (b != Karst && b != Canyons && b != HighlandMoor && b != Foothills) continue; }
            bool nearLava = false;      // the ashlands keep their ground
            for (const auto& lc : lavaCells)
                if (std::hypot((i % S + 0.5f) * VOXEL_DIM - lc.x, (i / S + 0.5f) * VOXEL_DIM - lc.y) < 4.5f * VOXEL_DIM) nearLava = true;
            if (nearLava) continue;
            mesaCell[i] = 1;
            int b = chart.biomes[i];
            bool free = b == chart.base[i] && !isRiver(b);
            int score = std::min(dA[i], dB[i]) * 4 - std::abs(dA[i] - dB[i]);
            if (free && std::min(dA[i], dB[i]) >= 2 && score > bestScore) { bestScore = score; best = i; }
        }
        if (best >= 0) {
            chart.biomes[best] = Village;
            chart.markers.push_back({ best % S, best / S, "Canyon village", "village" });
        }
        int cells = 0; long sx = 0, sz = 0;
        for (int i = 0; i < n; i++) if (mesaCell[i]) { cells++; sx += i % S; sz += i / S; }
        std::cout << "Canyon mesa: " << cells << " cells";
        if (cells) std::cout << " around " << (sx / cells) * VOXEL_DIM + 64 << " " << (sz / cells) * VOXEL_DIM + 64;
        std::cout << (best >= 0 ? ", village placed" : ", no room for a village") << "\n";
    }

    // Cut the ground at (wx, wz) down to the canyon profile: a flat
    // floor either side of the river, then walls of steep rock with a
    // ledge every 13 voxels of height. coarse: a plain slope instead
    // (before erosion, at the bake's 8-voxel spacing). Returns the new
    // height; 'cut' gets how far it went down.
    float canyonCut(int wx, int wz, int ci, int cj, float h, bool coarse, float& cut, uint8_t* part = nullptr) const {
        cut = 0.0f;
        if (part) *part = 0;
        if (canyonSegs.empty()) return h;
        float target = h;
        uint8_t bestPart = 0;
        for (const Seg& sg : canyonSegs[cellIndex(ci, cj)]) {
            float t, d = segDistance(sg, (float)wx, (float)wz, t);
            if (d > CANYON_REACH) continue;
            float floorH = sg.ha + (sg.hb - sg.ha) * t;
            if (h <= floorH) continue;
            float half = sg.halfWidth + 9.0f;
            float x = d - half;
            float tgt;
            uint8_t pt = 1;
            if (x <= 0.0f) tgt = floorH;
            else if (coarse) tgt = floorH + x * 4.0f;     // just a steep channel: the land stays high to the rim
            else {
                x += detail.GetNoise((float)wx * 0.9f, (float)wz * 0.9f) * 3.5f;    // ragged walls
                x = std::max(0.0f, x);
                const float rise = 14.0f, steep = 6.0f, ledge = 3.5f;
                const float run = rise / steep, period = run + ledge;
                float k = std::floor(x / period), fx = x - k * period;
                tgt = floorH + k * rise + std::min(fx * steep, rise);
                pt = fx * steep >= rise ? 3 : 2;
            }
            if (tgt < target) { target = tgt; bestPart = pt; }
        }
        cut = h - target;
        if (part && cut > 0.5f) *part = bestPart;
        return target;
    }

    // Distance from a point to a segment; t = where along it
    static float segDistance(const Seg& s, float x, float z, float& t) {
        float vx = s.bx - s.ax, vz = s.bz - s.az;
        float len2 = vx * vx + vz * vz;
        t = len2 > 0 ? ((x - s.ax) * vx + (z - s.az) * vz) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        float dx = x - (s.ax + vx * t), dz = z - (s.az + vz * t);
        return std::sqrt(dx * dx + dz * dz);
    }

    // Distance to the nearest segment listed under chart cell
    // (ci, cj); also returns that segment's half width and its
    // valley floor height at the nearest point
    bool nearestSegment(const std::vector<std::vector<Seg>>& table, float x, float z,
                        int ci, int cj, float& dist, float& halfWidth, float& floorH) const {
        const auto& list = table[cellIndex(ci, cj)];
        if (list.empty()) return false;
        dist = 1e9f;
        halfWidth = 0.0f;
        floorH = 0.0f;
        for (const Seg& s : list) {
            float vx = s.bx - s.ax, vz = s.bz - s.az;
            float len2 = vx * vx + vz * vz;
            float t = len2 > 0 ? ((x - s.ax) * vx + (z - s.az) * vz) / len2 : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            float dx = x - (s.ax + vx * t), dz = z - (s.az + vz * t);
            float d = std::sqrt(dx * dx + dz * dz);
            // Compare by distance relative to width, so a wide
            // delta wins over a narrow branch next to it
            if (d - s.halfWidth < dist - halfWidth) {
                dist = d; halfWidth = s.halfWidth;
                floorH = s.ha + (s.hb - s.ha) * t;
            }
        }
        return true;
    }

    static std::vector<int> neighbors(int S, int i) { return island::neighbors(S, i, true); }

    // Surface blocks by biome
    static void materials(int biome, float m, TerrainColumn& c) {
        using namespace island;
        c.top = BLOCK_GRASS; c.sub = BLOCK_DIRT; c.subDepth = 6; c.deep = BLOCK_STONE;
        switch (biome) {
        case DeepOcean: case Ocean:
            c.top = BLOCK_GRAVEL; c.sub = BLOCK_SAND; break;
        case Shallows: case Beach: case Delta:
            c.top = BLOCK_SAND; c.sub = BLOCK_SAND; break;
        case RockyShallows: case SeaCliffs:
            c.top = m > 0.2f ? BLOCK_GRAVEL : BLOCK_STONE; c.sub = BLOCK_STONE; break;
        case SwampShallows: case SwampBeach: case Swamp:
            c.top = m > 0.3f ? BLOCK_GRASS : BLOCK_MUD; c.sub = BLOCK_MUD; break;
        case MarshBeach: case Marsh:
            c.top = m > -0.2f ? BLOCK_GRASS : BLOCK_MUD; c.sub = BLOCK_MUD; break;
        case Foothills: case LowMountain:
            if (m > 0.45f) c.top = BLOCK_STONE; break;
        case MidMountain:
            c.top = m > 0.0f ? BLOCK_STONE : BLOCK_GRASS; c.sub = BLOCK_STONE; c.subDepth = 3; break;
        case HighMountain: case Plateau:
            c.top = m > 0.3f ? BLOCK_GRAVEL : BLOCK_STONE; c.sub = BLOCK_STONE; break;
        case Peak:
            c.top = BLOCK_SNOW; c.sub = BLOCK_STONE; break;
        case MountainPass:
            c.top = BLOCK_GRAVEL; c.sub = BLOCK_STONE; break;
        case Karst:          // grassy limestone upland; the rock shows through (see column)
            c.top = BLOCK_GRASS; c.sub = BLOCK_DIRT; c.subDepth = 3; break;
        case Ashlands:       // ash, cinders and basalt (see column)
            c.top = BLOCK_VOLCANIC_ROCK; c.sub = BLOCK_VOLCANIC_ROCK; c.deep = BLOCK_VOLCANIC_ROCK; break;
        case AutumnUplands: case CanyonMesa:
            c.top = BLOCK_GRASS; c.sub = BLOCK_DIRT; break;
        case HighlandMoor:
            c.top = m > 0.1f ? BLOCK_MOSS_STONE : BLOCK_GRASS; c.sub = BLOCK_STONE; break;
        case Canyons:        // red sand and dry grass over banded sandstone (see column)
            c.top = BLOCK_GRASS; c.sub = BLOCK_DIRT; c.subDepth = 2; break;
        case LavaLake: case Cave:
            c.top = BLOCK_VOLCANIC_ROCK; c.sub = BLOCK_VOLCANIC_ROCK; break;
        default: break;
        }
    }
};
