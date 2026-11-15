#pragma once
#include "IslandChart.h"
#include "../chunk/TerrainGenerator.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

// ══════════════════════════════════════════════════════════
// IslandGenerator — the "Island" world type
// ══════════════════════════════════════════════════════════
// Turns an IslandChart (one cell per chunk) into voxels.
// The chart decides WHAT each chunk is; this decides how it
// looks up close:
//
//   height     each cell's chart height, blended smoothly
//              between cell centres (Catmull-Rom), then rolling
//              hills on low ground, ridged crags on the
//              mountain, steep sea cliffs, and valleys cut
//              along the rivers
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
class IslandGenerator : public TerrainGenerator {
public:
    static constexpr int   SEA_LEVEL   = 160;   // water surface
    static constexpr int   PEAK_HEIGHT = 900;   // chart height 1.0, before crags
    static constexpr int   MAX_GROUND  = 1000;  // nothing rises above this
    static constexpr float WARP        = 40.0f; // coastline wobble (voxels)
    static constexpr float SNOW_LINE   = SEA_LEVEL + 0.70f * (PEAK_HEIGHT - SEA_LEVEL);

    IslandGenerator(int seed, int chartSize)
        : TerrainGenerator(seed),
          chart(island::generateIsland(seed % 1000000, chartSize)) {
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
        ridges.SetFractalOctaves(5);
        prepare();
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
        if (biome == Village || biome == PlainsVillage) {
            float flat = cellHeight[cellIndex(ni, nj)];
            float t = std::clamp((dc - 30.0f) / 30.0f, 0.0f, 1.0f);
            h = flat + (h - flat) * t;                  // level the plaza
        }

        c.ground = (int)std::floor(h);
        materials(biome, m, c);

        // ── Rock on steep ground, snow up high ───────────
        if (h > SEA_LEVEL + 40.0f && !flatBiome(biome)) {
            float slope = std::max(std::fabs(shape(wx + 3, wz).h - h),
                                   std::fabs(shape(wx, wz + 3).h - h)) / 3.0f;
            if (slope > 1.4f)       { c.top = BLOCK_STONE; c.sub = BLOCK_STONE; c.subDepth = 2; }
            else if (slope > 1.0f)  { c.top = m > 0.0f ? BLOCK_STONE : BLOCK_GRAVEL; c.sub = BLOCK_STONE; c.subDepth = 3; }
            if (h + m * 25.0f > SNOW_LINE && slope < 1.1f) c.top = BLOCK_SNOW;
        }

        if (biome == Village || biome == PlainsVillage) {
            if (dc < 34) { c.top = BLOCK_COBBLESTONE; c.sub = BLOCK_DIRT; }
        } else if (biome == HotSpring) {
            if (dc < 14) { c.waterLevel = c.ground; c.ground -= 3; c.top = BLOCK_GRAVEL; }
            else if (dc < 20) { c.top = BLOCK_GRAVEL; }
        } else if (biome == Cave) {
            // A cave mouth: a stone pit (the caves themselves
            // come with the vertical rework)
            if (dc < 22) { c.top = BLOCK_STONE; c.sub = BLOCK_STONE; }
            if (dc < 8)  { c.ground -= 30; c.top = BLOCK_VOLCANIC_ROCK; }
        }

        // ── Sea and lakes ────────────────────────────────
        if (c.ground <= SEA_LEVEL) c.waterLevel = std::max(c.waterLevel, SEA_LEVEL + 1);
        if (lakeW > 0.35f && lakeLvl > 0 && c.ground < (int)lakeLvl) {
            c.waterLevel = std::max(c.waterLevel, (int)lakeLvl);
            c.fluid = lakeFluid;
            if (lakeFluid == BLOCK_LAVA) { c.top = BLOCK_VOLCANIC_ROCK; c.sub = BLOCK_VOLCANIC_ROCK; }
        }

        // ── Rivers: a channel along the chart's river ────
        float rd, rw, rs;
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

        int biome = biomeAt(wx, wz);
        float chance = 0.0f;
        if (!biomeTrees(biome, cellRandom(cellX, cellZ, 4), chance, type)) return false;
        if (cellRandom(cellX, cellZ, 3) > chance) return false;

        // Only on dry land, clear of roads and rivers
        TerrainColumn c = column(wx, wz);
        if (c.waterLevel > c.ground || c.deckY >= 0) return false;
        if (c.top == BLOCK_GRAVEL || c.top == BLOCK_COBBLESTONE) return false;

        // Must fit under the top of the world
        if (c.top == BLOCK_STONE || c.top == BLOCK_SNOW) return false;   // bare rock, snowfield
        if (c.ground + trees[type].height > WORLD_HEIGHT) type = TREE_PINE;
        if (c.ground + trees[type].height > WORLD_HEIGHT) type = TREE_DEAD;
        return c.ground + trees[type].height <= WORLD_HEIGHT;
    }

public:
    int seaLevel() const override { return SEA_LEVEL; }

    float forestCover(int wx, int wz) const override {
        float chance = 0.0f;
        int type;
        if (!biomeTrees(biomeAt(wx, wz), 0.5f, chance, type)) return 0.0f;
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
        case Swamp:       chance = 0.35f; type = roll < 0.7f ? TREE_DEAD : TREE_OAK; break;
        case Marsh:       chance = 0.10f; type = TREE_DEAD; break;
        case Foothills:   chance = 0.30f; type = roll < 0.6f ? TREE_PINE : TREE_OAK; break;
        case LowMountain: chance = 0.35f; type = TREE_PINE; break;
        case MidMountain: chance = 0.15f; type = TREE_PINE; break;
        case Scrubland:   chance = 0.05f; type = TREE_DEAD; break;
        case HighlandMoor:chance = 0.04f; type = TREE_DEAD; break;
        case Ashlands:    chance = 0.02f; type = TREE_DEAD; break;
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

private:
    island::Chart chart;
    FastNoiseLite warpX, warpZ, detail, mix, hills, ridges;

    // Per chart cell
    std::vector<float> cellHeight;   // ground height (voxels)
    std::vector<float> lakeLevel;    // lake surface, or -1

    // Line segments (world voxels) for rivers and roads,
    // listed under every chart cell they pass near
    struct Seg { float ax, az, bx, bz, halfWidth, ha, hb; };   // ha/hb: valley floor at each end
    std::vector<std::vector<Seg>> riverSegs, roadSegs;

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
    };

    Shape shape(int wx, int wz) const {
        using namespace island;
        Shape out;
        const int S = chart.size;

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
            if (chart.biomes[idx] == SeaCliffs) {
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

        // ── Hills, crags and detail on land ──────────────
        if (h > SEA_LEVEL) {
            float e     = std::clamp((h - SEA_LEVEL) / (float)(PEAK_HEIGHT - SEA_LEVEL), 0.0f, 1.0f);
            float shore = smooth01(SEA_LEVEL + 2.0f, SEA_LEVEL + 24.0f, h);   // beaches stay flat
            float tame  = flatBiome(biome) ? 0.15f : 1.0f;
            float fx = (float)wx, fz = (float)wz;

            // rolling hills on the low ground, ~13 ft high
            h += hills.GetNoise(fx, fz) * 40.0f * (1.0f - smooth01(0.45f, 0.9f, e)) * shore * tame;
            // ridged crags on the mountain
            float r = (ridges.GetNoise(fx, fz) + 1.0f) * 0.5f;
            h += (r - 0.35f) * 170.0f * smooth01(0.22f, 0.7f, e) * tame;
            // small bumps everywhere, rougher up high
            h += detail.GetNoise(fx, fz) * (2.0f + 10.0f * smooth01(0.3f, 0.8f, e)) * shore * tame;
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
                float valley = std::min(sg.halfWidth + 60.0f + 2.2f * (h - floorH), 420.0f);
                if (d < valley) h = floorH + (h - floorH) * smooth01(sg.halfWidth, valley, d);
            }
        }

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
        cellHeight.assign(n, 0.0f);
        lakeLevel.assign(n, -1.0f);
        for (int i = 0; i < n; i++) {
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
        {
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
        for (int i = 0; i < n; i++) {
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
        case Ashlands:
            c.top = BLOCK_VOLCANIC_ROCK; c.sub = BLOCK_VOLCANIC_ROCK; c.deep = BLOCK_VOLCANIC_ROCK; break;
        case HighlandMoor:
            c.top = m > 0.1f ? BLOCK_MOSS_STONE : BLOCK_GRASS; c.sub = BLOCK_STONE; break;
        case Scrubland:
            c.top = m > 0.2f ? BLOCK_SAND : BLOCK_DIRT; break;
        case LavaLake: case Cave:
            c.top = BLOCK_VOLCANIC_ROCK; c.sub = BLOCK_VOLCANIC_ROCK; break;
        default: break;
        }
    }
};
