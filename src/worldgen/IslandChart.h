#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// IslandChart — the coarse layout of one island
// ══════════════════════════════════════════════════════════
// A C++ port of the island preview (generate.ts / biomes.ts
// from the design workspace). The integer hashing copies
// JavaScript's Math.imul and >>> exactly, so a seed gives the
// same island here as in the preview.
//
// One chart cell is one chunk (128 voxels). The chart says
// what each chunk IS — biome, height from 0 (sea) to 1
// (peak), which quadrant — plus markers (spawn, villages,
// caves …). The voxel generator (IslandGenerator) turns it
// into terrain and adds the fine detail.
//
// Pass order (from the design notes):
//   1 island mask, distance from the sea
//   2 quadrants: mountain = seed % 4, plains opposite,
//     volcanic and forest on the two sides
//   3 elevation: low coast, ridge round the peak
//   4 biomes by mountain ring, quadrant, coast
//   5 mountain lakes   6 swamp, marsh, forest lake
//   7 clifftop village, spawn, plains hamlet
//   8 hot springs, lava, caves
//   9 river: mountain lake → lower lake → fork → swamp
//     branch and plains branch (through a lake to a delta)
//  10 road: clifftop village → mountain pass → hamlet

namespace island {

// Biome ids — must not be renumbered (shared with the preview)
enum Biome : uint8_t {
    DeepOcean = 0, Ocean, Shallows, RockyShallows, SwampShallows,
    Beach, SwampBeach, MarshBeach, SeaCliffs, Plains, Grassland,
    Shrubland, Forest, DenseForest, Swamp, Marsh, Riparian,
    Foothills, LowMountain, MidMountain, HighMountain, Plateau, Peak,
    MountainLake, LowMountainLake, PlainsLake, ForestLake,
    MountainRiver, LowlandRiver, SwampRiver, Delta, RiverFork,
    Ashlands, HighlandMoor, Scrubland, HotSpring, LavaLake, Cave,
    Village, PlainsVillage, Road, Roadside, Bridge, MountainPass,
    BIOME_COUNT
};

struct BiomeInfo { const char* name; const char* group; uint32_t color; };

inline const BiomeInfo& biomeInfo(int id) {
    static const BiomeInfo table[BIOME_COUNT] = {
        {"Deep ocean","Coast",0x0c3348}, {"Ocean","Coast",0x14506a},
        {"Shallows","Coast",0x2f86a4}, {"Rocky shallows","Coast",0x3d6d78},
        {"Swamp shallows","Coast",0x1d4a3c}, {"Beach","Coast",0xd2c09a},
        {"Swamp beach","Coast",0x6d5a3c}, {"Marsh beach","Coast",0x8d7b56},
        {"Sea cliffs","Volcanic",0x6a6156}, {"Plains","Plains",0x9bb85a},
        {"Grassland","Plains",0x6f9444}, {"Shrubland","Forest",0x5d7a3c},
        {"Forest","Forest",0x2c5a34}, {"Dense forest","Forest",0x17361f},
        {"Swamp","Forest",0x3e4c2c}, {"Marsh","Forest",0x6a7340},
        {"Riparian","Water",0x4e8a52}, {"Foothills","Mountain",0x7a7848},
        {"Low mountain","Mountain",0x5a6848}, {"Mid mountain","Mountain",0x4e5c58},
        {"High mountain","Mountain",0x8b97a2}, {"Plateau","Mountain",0x6d7c74},
        {"Peak","Mountain",0xe7eef2}, {"Mountain lake","Water",0x2a6e9c},
        {"Low mountain lake","Water",0x3c82b0}, {"Plains lake","Water",0x3a90b4},
        {"Forest lake","Water",0x1a4e58}, {"Mountain river","Water",0x3aa0c8},
        {"Lowland river","Water",0x2f86b8}, {"Swamp river","Water",0x2a5c4c},
        {"Delta","Water",0x8aa06a}, {"River fork","Water",0xd4b36a},
        {"Ashlands","Volcanic",0x5a4038}, {"Highland moor","Volcanic",0x6e665c},
        {"Scrubland","Volcanic",0x8a7448}, {"Hot spring","Volcanic",0x3ec4bc},
        {"Lava lake","Volcanic",0xe25818}, {"Cave opening","Volcanic",0x1a1614},
        {"Clifftop village","Settlement",0xc46a3a}, {"Plains village","Settlement",0xd08a48},
        {"Road","Settlement",0xb49670}, {"Roadside","Settlement",0x8c7458},
        {"Bridge","Settlement",0xefe4cc}, {"Mountain pass","Mountain",0xc4b08a},
    };
    return table[(id >= 0 && id < BIOME_COUNT) ? id : 0];
}

inline bool isLocked(int b) {
    switch (b) {
    case Peak: case MountainLake: case LowMountainLake: case PlainsLake:
    case ForestLake: case MountainRiver: case LowlandRiver: case SwampRiver:
    case Delta: case RiverFork: case HotSpring: case LavaLake: case Cave:
    case Village: case PlainsVillage: case Road: case Bridge: case MountainPass:
        return true;
    default: return false;
    }
}

struct Marker { int x, y; std::string label, kind; };

struct Chart {
    int size = 0;
    int seed = 0;
    std::vector<uint8_t> biomes;     // y * size + x
    std::vector<float>   elev;       // 0 sea .. 1 peak
    std::vector<uint8_t> quadrant;   // 0 NW 1 NE 2 SE 3 SW, 255 ocean
    int roleMountain = 0, rolePlains = 0, roleVolcanic = 0, roleForest = 0;
    std::vector<Marker> markers;

    int at(int x, int y) const { return biomes[(size_t)y * size + x]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size; }
    const Marker* marker(const std::string& kind) const {
        for (auto& m : markers) if (m.kind == kind) return &m;
        return nullptr;
    }
};

// ── JavaScript-exact helpers ──────────────────────────────
inline int32_t imul(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

inline double hash(double ixd, double iyd, double seedd) {
    // In the original these are always whole numbers
    int32_t ix = (int32_t)(int64_t)ixd, iy = (int32_t)(int64_t)iyd, seed = (int32_t)(int64_t)seedd;
    // JavaScript adds these as numbers and then wraps to 32
    // bits; unsigned addition wraps the same way (and signed
    // overflow would be undefined in C++)
    int32_t n = (int32_t)((uint32_t)imul(ix, 374761393) + (uint32_t)imul(iy, 668265263) +
                          (uint32_t)imul(seed, 1442695041));
    n = imul(n ^ (int32_t)((uint32_t)n >> 13), 1274126177);
    uint32_t u = (uint32_t)(n ^ (int32_t)((uint32_t)n >> 16));
    return (double)u / 4294967296.0;
}

inline double noise(double x, double y, double seed) {
    double x0 = std::floor(x), y0 = std::floor(y);
    double fx = x - x0, fy = y - y0;
    double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    double a = hash(x0, y0, seed), b = hash(x0 + 1, y0, seed);
    double c = hash(x0, y0 + 1, seed), d = hash(x0 + 1, y0 + 1, seed);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

inline double fbm(double x, double y, double seed) {
    return noise(x, y, seed) * 0.65 + noise(x * 2.1, y * 2.1, seed + 11) * 0.35;
}

inline int sector(double angle) {
    const double PI = 3.14159265358979323846;
    if (angle >= -PI / 2 && angle < 0) return 1;
    if (angle >= 0 && angle < PI / 2) return 2;
    if (angle >= PI / 2 && angle <= PI) return 3;
    return 0;
}

inline double dist(double ax, double ay, double bx, double by) {
    return std::hypot(ax - bx, ay - by);
}

// A* over the chart, matching the original's heap exactly
template <typename Pass, typename Cost, typename Step>
std::vector<int> findPath(int size, int start, int goal, Pass passable, Cost stepCost,
                          Step canStep, bool hasCanStep) {
    if (start == goal) return { start };
    int n = size * size;
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> g(n, INF), f(n, INF);
    std::vector<int> came(n, -1);
    std::vector<uint8_t> closed(n, 0);
    int gx = goal % size, gy = goal / size;
    g[start] = 0;
    f[start] = std::hypot((double)(start % size - gx), (double)(start / size - gy));
    std::vector<int> heap = { start };
    auto push = [&](int i) {
        heap.push_back(i);
        int k = (int)heap.size() - 1;
        while (k > 0) {
            int p = (k - 1) >> 1;
            if (f[heap[p]] <= f[heap[k]]) break;
            std::swap(heap[p], heap[k]);
            k = p;
        }
    };
    auto pop = [&]() -> int {
        int top = heap[0];
        int last = heap.back();
        heap.pop_back();
        if (heap.empty()) return top;
        heap[0] = last;
        int k = 0;
        for (;;) {
            int l = k * 2 + 1, r = l + 1, m = k;
            if (l < (int)heap.size() && f[heap[l]] < f[heap[m]]) m = l;
            if (r < (int)heap.size() && f[heap[r]] < f[heap[m]]) m = r;
            if (m == k) break;
            std::swap(heap[k], heap[m]);
            k = m;
        }
        return top;
    };
    int guard = 0;
    while (!heap.empty() && guard++ < n * 6) {
        int cur = pop();
        if (closed[cur]) continue;
        if (cur == goal) {
            std::vector<int> path = { cur };
            int p = came[cur];
            while (p >= 0) { path.push_back(p); p = came[p]; }
            std::reverse(path.begin(), path.end());
            return path;
        }
        closed[cur] = 1;
        int x = cur % size, y = cur / size;
        for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
            int ni = ny * size + nx;
            if (ni != goal && !passable(ni)) continue;
            if (hasCanStep && !canStep(cur, ni)) continue;
            double step = ((dx && dy) ? 1.414 : 1.0) * stepCost(ni, cur);
            double ng = g[cur] + step;
            if (ng < g[ni]) {
                came[ni] = cur;
                g[ni] = ng;
                f[ni] = ng + std::hypot((double)(nx - gx), (double)(ny - gy)) * 1.05;
                push(ni);
            }
        }
    }
    return {};
}

inline std::vector<int> neighbors(int size, int i, bool diag = true) {
    int x = i % size, y = i / size;
    std::vector<int> out;
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
        if (!dx && !dy) continue;
        if (!diag && dx && dy) continue;
        int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
        out.push_back(ny * size + nx);
    }
    return out;
}

// ══════════════════════════════════════════════════════════
// generateIsland — port of generate.ts, pass for pass
// ══════════════════════════════════════════════════════════
inline Chart generateIsland(int seed, int size) {
    const int n = size * size;
    std::vector<uint8_t> land(n, 0), biomes(n, 0), quadrant(n, 255);
    std::vector<float> elev(n, 0.0f);
    const double cx = (size - 1) / 2.0, cy = (size - 1) / 2.0;
    const double rx = size * 0.36, ry = size * 0.33;
    auto X = [&](int i) { return i % size; };
    auto Y = [&](int i) { return i / size; };

    // 1 — island mask
    for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
        int i = y * size + x;
        double wx = (noise(x * 0.07, y * 0.07, seed) - 0.5) * 0.38;
        double wy = (noise(x * 0.07 + 9, y * 0.07, seed + 4) - 0.5) * 0.38;
        double dx = (x - cx) / rx + wx, dy = (y - cy) / ry + wy;
        double edge = noise(x * 0.16, y * 0.16, seed + 9);
        land[i] = dx * dx + dy * dy < 0.9 + (edge - 0.5) * 0.32 ? 1 : 0;
    }

    // distance from the sea, and from land
    std::vector<int16_t> landDist(n, 999), waterDist(n, 999);
    {
        std::vector<int> q;
        for (int i = 0; i < n; i++) if (!land[i]) { landDist[i] = 0; q.push_back(i); }
        for (size_t h = 0; h < q.size(); h++) {
            int cur = q[h], d = landDist[cur];
            for (int ni : neighbors(size, cur, false))
                if (landDist[ni] > d + 1) { landDist[ni] = (int16_t)(d + 1); q.push_back(ni); }
        }
        q.clear();
        for (int i = 0; i < n; i++) if (land[i]) { waterDist[i] = 0; q.push_back(i); }
        for (size_t h = 0; h < q.size(); h++) {
            int cur = q[h], d = waterDist[cur];
            for (int ni : neighbors(size, cur, false))
                if (waterDist[ni] > d + 1) { waterDist[ni] = (int16_t)(d + 1); q.push_back(ni); }
        }
    }

    // 2 — quadrants
    const int mountain = seed % 4;
    const int plains   = (mountain + 2) % 4;
    const bool volcanicFirst = hash(seed, 7, 3) > 0.5;
    const int adjA = (mountain + 1) % 4, adjB = (mountain + 3) % 4;
    const int volcanic = volcanicFirst ? adjA : adjB;
    const int forest   = volcanicFirst ? adjB : adjA;
    for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
        int i = y * size + x;
        if (!land[i]) continue;
        double ang = std::atan2((double)(y - cy), (double)(x - cx)) +
                     (noise(x * 0.05, y * 0.05, seed + 21) - 0.5) * 0.45;
        quadrant[i] = (uint8_t)sector(ang);
    }

    // peak
    int peak = -1;
    double peakScore = -std::numeric_limits<double>::infinity();
    static const int corners[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
    for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
        int i = y * size + x;
        if (quadrant[i] != mountain || landDist[i] < 7) continue;
        double toward = ((x - cx) / rx) * corners[mountain][0] + ((y - cy) / ry) * corners[mountain][1];
        double score = toward * 4 + landDist[i] * 0.05;
        if (score > peakScore) { peakScore = score; peak = i; }
    }
    if (peak < 0) for (int i = 0; i < n; i++) if (land[i]) peak = i;
    if (peak < 0) peak = n / 2;
    const int px = X(peak), py = Y(peak);
    const double reach = size * 0.2;

    // 3 — elevation
    for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
        int i = y * size + x;
        if (!land[i]) { elev[i] = 0; continue; }
        double e = 0.1 + std::min<int>(landDist[i], 16) / 16.0 * 0.18;
        e += (fbm(x * 0.08, y * 0.08, seed + 40) - 0.5) * 0.12;
        double d = dist(x, y, px, py);
        if (d < reach) {
            double t = 1 - d / reach;
            double ridge = 0.2 + t * t * 0.8;
            if (d > reach * 0.22 && d < reach * 0.42) ridge = std::min(ridge, 0.7);
            e = std::max(e, ridge);
        }
        elev[i] = (float)std::max(0.05, std::min(0.98, e));
    }
    elev[peak] = 1;

    // 4 — biomes
    for (int i = 0; i < n; i++) {
        if (!land[i]) { biomes[i] = waterDist[i] > 6 ? DeepOcean : Ocean; continue; }
        int x = X(i), y = Y(i);
        double d = dist(x, y, px, py);
        int q = quadrant[i];
        double grain = fbm(x * 0.18, y * 0.18, seed + 70);
        if (d < 1.4) biomes[i] = Peak;
        else if (d < reach * 0.22) biomes[i] = HighMountain;
        else if (d < reach * 0.42) biomes[i] = Plateau;
        else if (d < reach * 0.66) biomes[i] = MidMountain;
        else if (d < reach * 0.9)  biomes[i] = LowMountain;
        else if (d < reach * 1.15 && (q == mountain || elev[i] > 0.42)) biomes[i] = Foothills;
        else if (q == plains) biomes[i] = grain > 0.58 ? Grassland : Plains;
        else if (q == forest) {
            if (grain > 0.62 && landDist[i] > 4) biomes[i] = DenseForest;
            else if (grain < 0.38) biomes[i] = Shrubland;
            else biomes[i] = Forest;
        } else if (q == volcanic) {
            if (landDist[i] <= 2) biomes[i] = SeaCliffs;
            else if (d < reach * 1.45) biomes[i] = grain > 0.55 ? Ashlands : HighlandMoor;
            else biomes[i] = grain > 0.5 ? Scrubland : HighlandMoor;
        } else biomes[i] = Foothills;
    }
    for (int i = 0; i < n; i++) {
        if (!land[i]) {
            if (waterDist[i] > 2 && waterDist[i] <= 6) biomes[i] = Ocean;
            continue;
        }
        if (landDist[i] != 1) continue;
        if (biomes[i] == SeaCliffs || biomes[i] == Peak) continue;
        biomes[i] = quadrant[i] == volcanic ? SeaCliffs : Beach;
    }
    for (int i = 0; i < n; i++) {
        if (land[i] || waterDist[i] > 2 || waterDist[i] == 0) continue;
        bool volcanicCoast = false;
        for (int ni : neighbors(size, i))
            if (biomes[ni] == SeaCliffs || quadrant[ni] == volcanic) volcanicCoast = true;
        biomes[i] = volcanicCoast ? RockyShallows : Shallows;
    }

    auto inRing = [&](double minR, double maxR, int biome) {
        int best = -1;
        double bestE = std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; i++) {
            if (biomes[i] != biome) continue;
            double d = dist(X(i), Y(i), px, py);
            if (d < minR || d > maxR) continue;
            if (elev[i] < bestE) { bestE = elev[i]; best = i; }
        }
        return best;
    };
    auto isLake = [](int b) {
        return b == MountainLake || b == LowMountainLake || b == PlainsLake ||
               b == ForestLake || b == LavaLake;
    };
    auto stampDisk = [&](int center, double radius, int biome, bool lockedOk) {
        if (center < 0) return;
        int x0 = X(center), y0 = Y(center), r = (int)std::ceil(radius);
        for (int y = y0 - r; y <= y0 + r; y++)
        for (int x = x0 - r; x <= x0 + r; x++) {
            if (x < 0 || y < 0 || x >= size || y >= size) continue;
            if (dist(x, y, x0, y0) > radius) continue;
            int i = y * size + x;
            if (!land[i] && biome != MountainLake && biome != LowMountainLake) continue;
            if (!lockedOk && isLocked(biomes[i])) continue;
            biomes[i] = (uint8_t)biome;
            if (isLake(biome)) land[i] = 0;
        }
    };

    // 5 — mountain lakes
    int mountainLake = inRing(reach * 0.24, reach * 0.4, Plateau);
    stampDisk(mountainLake, 1.6, MountainLake, true);
    int lowLake = inRing(reach * 0.68, reach * 0.88, LowMountain);
    stampDisk(lowLake >= 0 ? lowLake : inRing(reach * 0.6, reach * 1, MidMountain), 1.4, LowMountainLake, true);

    // 6 — swamp, marsh, forest lake
    int swampCenter = -1;
    double swampScore = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; i++) {
        if (quadrant[i] != forest || !land[i]) continue;
        if (landDist[i] < 3 || landDist[i] > 9) continue;
        int nearMountain = 999;
        for (int ni : neighbors(size, i)) if (quadrant[ni] == mountain) nearMountain = std::min(nearMountain, 1);
        double dPeak = dist(X(i), Y(i), px, py);
        double score = nearMountain * 20 + std::abs(landDist[i] - 5) + std::max(0.0, 14 - dPeak);
        if (score < swampScore) { swampScore = score; swampCenter = i; }
    }
    if (swampCenter < 0) for (int i = 0; i < n; i++) if (quadrant[i] == forest && land[i]) swampCenter = i;
    const int swampR = std::max(4, (int)std::round(size * 0.055));
    if (swampCenter >= 0) {
        int sx = X(swampCenter), sy = Y(swampCenter);
        for (int y = sy - swampR - 3; y <= sy + swampR + 3; y++)
        for (int x = sx - swampR - 3; x <= sx + swampR + 3; x++) {
            if (x < 0 || y < 0 || x >= size || y >= size) continue;
            int i = y * size + x;
            double d = dist(x, y, sx, sy);
            if (!land[i] || isLocked(biomes[i])) continue;
            if (dist(x, y, px, py) < reach * 0.45) continue;
            if (d <= swampR) biomes[i] = Swamp;
            else if (d <= swampR + 2.4 && biomes[i] != SeaCliffs) biomes[i] = Marsh;
        }
    }
    int forestLake = -1;
    if (swampCenter >= 0) {
        int sx = X(swampCenter), sy = Y(swampCenter);
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; i++) {
            if (!land[i] || quadrant[i] != forest) continue;
            if (biomes[i] != Forest && biomes[i] != DenseForest && biomes[i] != Marsh) continue;
            double d = dist(X(i), Y(i), sx, sy);
            if (d < swampR + 1 || d > swampR + 7) continue;
            if (d < best) { best = d; forestLake = i; }
        }
        stampDisk(forestLake, 1.5, ForestLake, true);
    }
    auto swampSide = [&](int i) {
        if (swampCenter < 0) return false;
        return dist(X(i), Y(i), X(swampCenter), Y(swampCenter)) < swampR + 8;
    };
    for (int i = 0; i < n; i++) {
        if (!swampSide(i)) continue;
        if (biomes[i] == Beach) biomes[i] = SwampBeach;
        else if (biomes[i] == Shallows || biomes[i] == RockyShallows) biomes[i] = SwampShallows;
    }
    for (int i = 0; i < n; i++) {
        if (biomes[i] != Beach && biomes[i] != Shallows) continue;
        bool near = false;
        for (int ni : neighbors(size, i))
            if (biomes[ni] == SwampBeach || biomes[ni] == SwampShallows || biomes[ni] == Swamp) near = true;
        if (near && biomes[i] == Beach) biomes[i] = MarshBeach;
    }

    // 7 — clifftop village
    int village = -1;
    double villageScore = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; i++) {
        if (quadrant[i] != volcanic || !land[i]) continue;
        if (landDist[i] < 2 || landDist[i] > 5) continue;
        if (dist(X(i), Y(i), px, py) < reach * 0.85) continue;
        bool cliff = false;
        for (int ni : neighbors(size, i)) if (biomes[ni] == SeaCliffs) cliff = true;
        if (!cliff) continue;
        double score = std::abs(landDist[i] - 3) + elev[i];
        if (score < villageScore) { villageScore = score; village = i; }
    }
    if (village >= 0) {
        int vx = X(village), vy = Y(village);
        for (int y = vy - 2; y <= vy + 2; y++)
        for (int x = vx - 2; x <= vx + 2; x++) {
            if (x < 0 || y < 0 || x >= size || y >= size) continue;
            int i = y * size + x;
            if (!land[i] || biomes[i] == SeaCliffs) continue;
            if (dist(x, y, vx, vy) > 2.3) continue;
            biomes[i] = Village;
            elev[i] = elev[village];
        }
    }

    // 8 — hot springs, lava, caves
    std::vector<int> springs;
    if (village >= 0) {
        int vx = X(village), vy = Y(village);
        for (int ni : neighbors(size, village, false))
            if (biomes[ni] == Village) { biomes[ni] = HotSpring; springs.push_back(ni); break; }
        std::vector<int> pool;
        for (int i = 0; i < n; i++) {
            if (biomes[i] != Ashlands && biomes[i] != HighlandMoor) continue;
            if (dist(X(i), Y(i), vx, vy) > 7) continue;
            pool.push_back(i);
        }
        std::stable_sort(pool.begin(), pool.end(), [&](int a, int b) {
            return hash(a, seed, 5) < hash(b, seed, 5);
        });
        for (int cell : pool) {
            if (springs.size() >= 3) break;
            bool close = false;
            for (int s : springs) if (dist(X(s), Y(s), X(cell), Y(cell)) < 2) close = true;
            if (close) continue;
            biomes[cell] = HotSpring;
            springs.push_back(cell);
        }
    }
    std::vector<int> lavas;
    if (village >= 0) {
        int vx = X(village), vy = Y(village);
        std::vector<int> pool;
        for (int i = 0; i < n; i++) {
            if (biomes[i] != Ashlands && biomes[i] != HighlandMoor && biomes[i] != Scrubland) continue;
            if (dist(X(i), Y(i), vx, vy) < 4) continue;
            bool nearSpring = false;
            for (int s : springs) if (dist(X(i), Y(i), X(s), Y(s)) < 3) nearSpring = true;
            if (nearSpring) continue;
            pool.push_back(i);
        }
        std::stable_sort(pool.begin(), pool.end(), [&](int a, int b) {
            return dist(X(b), Y(b), vx, vy) < dist(X(a), Y(a), vx, vy);   // furthest first
        });
        for (int cell : pool) {
            if (lavas.size() >= 2) break;
            bool close = false;
            for (int s : lavas) if (dist(X(s), Y(s), X(cell), Y(cell)) < 4) close = true;
            if (close) continue;
            stampDisk(cell, 1.1, LavaLake, true);
            lavas.push_back(cell);
        }
    }
    std::vector<int> caves;
    auto takeCaves = [&](size_t want, auto pred) {
        std::vector<int> pool;
        for (int i = 0; i < n; i++) if (pred(i)) pool.push_back(i);
        std::stable_sort(pool.begin(), pool.end(), [&](int a, int b) {
            return hash(a, seed, 9) < hash(b, seed, 9);
        });
        for (int cell : pool) {
            if (caves.size() >= want) break;
            if (village >= 0 && dist(X(cell), Y(cell), X(village), Y(village)) < 8) continue;
            biomes[cell] = Cave;
            caves.push_back(cell);
        }
    };
    size_t caveStart = caves.size();
    takeCaves(caveStart + 2, [&](int i) { return biomes[i] == SeaCliffs && quadrant[i] == volcanic; });
    takeCaves(caveStart + 4, [&](int i) {
        if (biomes[i] != Ashlands) return false;
        for (int l : lavas) if (dist(X(i), Y(i), X(l), Y(l)) < 4) return true;
        return false;
    });

    // spawn and hamlet
    int spawn = -1;
    double spawnScore = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; i++) {
        if (quadrant[i] != plains || !land[i]) continue;
        if (landDist[i] < 4 || landDist[i] > 10) continue;
        if (biomes[i] != Plains && biomes[i] != Grassland && biomes[i] != Beach) continue;
        int x = X(i), y = Y(i);
        double toward = ((x - cx) / rx) * corners[plains][0] + ((y - cy) / ry) * corners[plains][1];
        double score = toward * 5 - std::abs(landDist[i] - 6);
        if (score > spawnScore) { spawnScore = score; spawn = i; }
    }
    if (spawn < 0) for (int i = 0; i < n; i++) if (quadrant[i] == plains && land[i]) spawn = i;
    int hamlet = -1;
    if (spawn >= 0) {
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; i++) {
            if (!land[i] || quadrant[i] != plains) continue;
            if (biomes[i] != Plains && biomes[i] != Grassland) continue;
            double d = dist(X(i), Y(i), X(spawn), Y(spawn));
            if (d < 4 || d > 8) continue;
            if (d < best) { best = d; hamlet = i; }
        }
        if (hamlet >= 0) stampDisk(hamlet, 1.2, PlainsVillage, true);
    }

    // 9 — river
    auto landPass = [&](int i) { return land[i] == 1 && biomes[i] != LavaLake && biomes[i] != Cave; };
    auto isRiver = [](int b) { return b == MountainRiver || b == LowlandRiver || b == SwampRiver; };
    auto riverCost = [&](int style) {
        return [&, style](int i, int prev) {
            double up = std::max(0.0, (double)elev[i] - elev[prev]);
            double jitter = noise((i % size) * 0.22, (i / size) * 0.22, seed + 90 + style);
            double waterish = isRiver(biomes[i]) ? 3 : 0;
            return 1 + elev[i] * (style == 0 ? 0.4 : 1.4) + up * (style == 0 ? 6 : 9) + jitter * 0.85 + waterish;
        };
    };
    auto pickCell = [&](auto pred, auto score) {
        int best = -1;
        double bestS = std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; i++) {
            if (!pred(i)) continue;
            double s = score(i);
            if (s < bestS) { bestS = s; best = i; }
        }
        return best;
    };
    auto lakeShore = [&](int biome) {
        int fallback = -1;
        for (int i = 0; i < n; i++) {
            if (biomes[i] != biome) continue;
            fallback = i;
            for (int ni : neighbors(size, i)) if (land[ni] == 1) return i;
        }
        return fallback;
    };
    int source = lakeShore(MountainLake);
    int poolCell = lakeShore(LowMountainLake);

    int fork = -1;
    if (source >= 0 && spawn >= 0 && swampCenter >= 0) {
        fork = pickCell(
            [&](int i) {
                return land[i] == 1 && (biomes[i] == Foothills || biomes[i] == LowMountain ||
                                        biomes[i] == Shrubland || biomes[i] == Plains);
            },
            [&](int i) {
                int x = X(i), y = Y(i);
                double dPeak = dist(x, y, px, py);
                if (dPeak < reach * 0.75 || dPeak > reach * 1.35) return 999.0;
                double midX = (X(spawn) + X(swampCenter)) / 2.0;
                double midY = (Y(spawn) + Y(swampCenter)) / 2.0;
                return dist(x, y, midX, midY) + std::abs(elev[i] - 0.38) * 20;
            });
    }
    auto paintPath = [&](const std::vector<int>& path, int biome) {
        for (int cell : path) {
            if (isLocked(biomes[cell])) continue;
            biomes[cell] = (uint8_t)biome;
            land[cell] = 0;
        }
    };
    auto banks = [&](const std::vector<int>& path, int radius) {
        for (int cell : path) {
            int x0 = X(cell), y0 = Y(cell);
            for (int y = y0 - radius; y <= y0 + radius; y++)
            for (int x = x0 - radius; x <= x0 + radius; x++) {
                if (x < 0 || y < 0 || x >= size || y >= size) continue;
                double d = dist(x, y, x0, y0);
                if (d > radius || d < 0.5) continue;
                int i = y * size + x;
                if (!land[i] || isLocked(biomes[i])) continue;
                if (biomes[i] == Beach || biomes[i] == SeaCliffs || biomes[i] == SwampBeach) continue;
                biomes[i] = Riparian;
            }
        }
    };
    auto noStep = [](int, int) { return true; };

    if (source >= 0 && poolCell >= 0 && fork >= 0 && spawn >= 0 && swampCenter >= 0) {
        auto stemPass = [&](int i) { return landPass(i) && biomes[i] != Swamp && biomes[i] != Marsh; };
        auto toPool = findPath(size, source, poolCell, stemPass, riverCost(0), noStep, false);
        auto toFork = findPath(size, poolCell, fork, stemPass, riverCost(0), noStep, false);
        paintPath(toPool, MountainRiver);
        paintPath(toFork, MountainRiver);
        banks(toPool, 1);
        banks(toFork, 1);

        std::vector<uint8_t> blocked(n, 0);
        auto blockPath = [&](const std::vector<int>& path) {
            for (int cell : path) if (cell != fork) blocked[cell] = 1;
        };
        blockPath(toPool);
        blockPath(toFork);

        auto openWater = [&](int i) {
            return !blocked[i] && (landPass(i) || biomes[i] == Swamp || biomes[i] == Marsh);
        };
        auto missesRiver = [&](int from, int to) {
            int x = from % size, y = from / size;
            int dx = to % size - x, dy = to / size - y;
            if (!dx || !dy) return !blocked[to];
            int flankA = y * size + (x + dx), flankB = (y + dy) * size + x;
            return !blocked[to] && !(blocked[flankA] == 1 && blocked[flankB] == 1);
        };

        int mouth = pickCell(
            [&](int i) { return land[i] == 1 && biomes[i] == Beach && quadrant[i] == plains && !blocked[i]; },
            [&](int i) { return dist(X(i), Y(i), X(spawn), Y(spawn)); });
        if (mouth < 0) mouth = spawn;

        auto diskClear = [&](int center, double radius) {
            int x0 = X(center), y0 = Y(center);
            int reachR = (int)std::ceil(radius + 1);
            for (int y = y0 - reachR; y <= y0 + reachR; y++)
            for (int x = x0 - reachR; x <= x0 + reachR; x++) {
                if (x < 0 || y < 0 || x >= size || y >= size) return false;
                if (dist(x, y, x0, y0) > radius + 1) continue;
                int i = y * size + x;
                if (blocked[i] || !land[i] || i == fork) return false;
            }
            return true;
        };
        int fx = X(fork), fy = Y(fork), mx = X(mouth), my = Y(mouth);
        int lakeAt = pickCell(
            [&](int i) { return quadrant[i] == plains && land[i] == 1 && landDist[i] >= 3 && diskClear(i, 2.2); },
            [&](int i) {
                int x = X(i), y = Y(i);
                if (hamlet >= 0 && dist(x, y, X(hamlet), Y(hamlet)) < 5) return 900.0;
                return dist(x, y, (fx + mx) / 2.0, (fy + my) / 2.0);
            });

        int swampGoal = swampCenter;
        if (swampGoal < 0 || blocked[swampGoal] || (biomes[swampGoal] != Swamp && biomes[swampGoal] != Marsh))
            swampGoal = pickCell(
                [&](int i) { return (biomes[i] == Swamp || biomes[i] == Marsh) && !blocked[i]; },
                [&](int i) { return dist(X(i), Y(i), fx, fy); });
        std::vector<int> branchB;
        if (swampGoal >= 0)
            branchB = findPath(size, fork, swampGoal,
                               [&](int i) { return i == swampGoal || openWater(i); },
                               riverCost(1), missesRiver, true);
        if (!branchB.empty()) {
            bool entered = false;
            for (int cell : branchB) {
                if (biomes[cell] == Swamp || biomes[cell] == Marsh || quadrant[cell] == forest) entered = true;
                if (isLocked(biomes[cell])) continue;
                biomes[cell] = entered ? SwampRiver : LowlandRiver;
                land[cell] = 0;
                blocked[cell] = 1;
            }
            banks(branchB, 2);
        }

        std::vector<int> toLake, fromLake, branchA;
        if (lakeAt >= 0)
            toLake = findPath(size, fork, lakeAt, [&](int i) { return i == lakeAt || openWater(i); },
                              riverCost(1), missesRiver, true);
        if (!toLake.empty()) blockPath(toLake);
        if (lakeAt >= 0 && !toLake.empty())
            fromLake = findPath(size, lakeAt, mouth, [&](int i) { return i == mouth || openWater(i); },
                                riverCost(1), missesRiver, true);
        if (!toLake.empty() && !fromLake.empty()) {
            branchA = toLake;
            branchA.insert(branchA.end(), fromLake.begin() + 1, fromLake.end());
        } else {
            branchA = findPath(size, fork, mouth, [&](int i) { return i == mouth || openWater(i); },
                               riverCost(1), missesRiver, true);
        }
        paintPath(branchA, LowlandRiver);
        banks(branchA, 1);
        blockPath(branchA);

        if (lakeAt >= 0) {
            std::vector<uint8_t> armA(n, 0);
            for (int cell : branchA) armA[cell] = 1;
            int x0 = X(lakeAt), y0 = Y(lakeAt);
            for (int y = y0 - 3; y <= y0 + 3; y++)
            for (int x = x0 - 3; x <= x0 + 3; x++) {
                if (x < 0 || y < 0 || x >= size || y >= size) continue;
                if (dist(x, y, x0, y0) > 2.2) continue;
                int i = y * size + x;
                if (quadrant[i] != plains || i == fork) continue;
                if (blocked[i] && !armA[i]) continue;
                if (biomes[i] == PlainsVillage || biomes[i] == Village) continue;
                bool touchesOther = false;
                for (int ni : neighbors(size, i))
                    if (ni != fork && blocked[ni] && !armA[ni]) touchesOther = true;
                if (touchesOther) continue;
                if (!land[i] && biomes[i] != LowlandRiver && biomes[i] != Riparian) continue;
                biomes[i] = PlainsLake;
                land[i] = 0;
            }
        }

        biomes[fork] = RiverFork;
        land[fork] = 0;
        for (int k = (int)branchA.size() - 1; k >= 0 && k > (int)branchA.size() - 6; k--) {
            int cell = branchA[k];
            if (landDist[cell] <= 2 && biomes[cell] == LowlandRiver) biomes[cell] = Delta;
        }
    }

    // 10 — road
    int hamletTarget = hamlet >= 0 ? hamlet : spawn;
    if (village >= 0 && hamletTarget >= 0) {
        int passCell = pickCell(
            [&](int i) {
                return land[i] == 1 && (biomes[i] == LowMountain || biomes[i] == Foothills ||
                                        biomes[i] == MidMountain);
            },
            [&](int i) {
                int x = X(i), y = Y(i);
                double d = dist(x, y, px, py);
                if (d < reach * 0.72 || d > reach * 1.15) return 999.0;
                double mx = (X(village) + X(hamletTarget)) / 2.0;
                double my = (Y(village) + Y(hamletTarget)) / 2.0;
                return elev[i] * 40 + dist(x, y, mx, my);
            });
        auto roadCost = [&](int i, int prev) {
            double steep = std::max(0.0, (double)elev[i] - elev[prev]) * 4;
            double high = (biomes[i] == HighMountain || biomes[i] == Plateau || biomes[i] == Peak) ? 8 : 0;
            return 1 + elev[i] * 1.4 + steep + high;
        };
        auto passable = [&](int i) {
            return land[i] == 1 && biomes[i] != LavaLake && biomes[i] != MountainLake &&
                   biomes[i] != LowMountainLake && biomes[i] != Peak;
        };
        std::vector<int> first, second;
        if (passCell >= 0) {
            first  = findPath(size, village, passCell, passable, roadCost, noStep, false);
            second = findPath(size, passCell, hamletTarget, passable, roadCost, noStep, false);
        } else {
            second = findPath(size, village, hamletTarget, passable, roadCost, noStep, false);
        }
        std::vector<int> roadPath;
        if (!first.empty() && !second.empty()) {
            roadPath = first;
            roadPath.insert(roadPath.end(), second.begin() + 1, second.end());
        } else {
            roadPath = second;
        }
        if (!roadPath.empty()) {
            for (int cell : roadPath) {
                if (biomes[cell] == Village || biomes[cell] == PlainsVillage) continue;
                int b = biomes[cell];
                bool onWater = b == LowlandRiver || b == MountainRiver || b == SwampRiver ||
                               b == Delta || b == RiverFork || b == PlainsLake;
                if (onWater) { biomes[cell] = Bridge; continue; }
                if (isLocked(b)) continue;
                biomes[cell] = Road;
                for (int ni : neighbors(size, cell, false)) {
                    if (!land[ni] || isLocked(biomes[ni]) || biomes[ni] == Road) continue;
                    if (biomes[ni] == Beach || biomes[ni] == SeaCliffs || biomes[ni] == SwampBeach) continue;
                    biomes[ni] = Roadside;
                }
            }
            if (passCell >= 0) biomes[passCell] = MountainPass;
        }
    }

    Chart c;
    c.size = size;
    c.seed = seed;
    c.biomes   = std::move(biomes);
    c.elev     = std::move(elev);
    c.quadrant = std::move(quadrant);
    c.roleMountain = mountain; c.rolePlains = plains;
    c.roleVolcanic = volcanic; c.roleForest = forest;
    auto mark = [&](int cell, const char* label, const char* kind) {
        if (cell >= 0) c.markers.push_back({ cell % size, cell / size, label, kind });
    };
    mark(peak, "Peak", "peak");
    mark(spawn, "Spawn", "spawn");
    mark(fork, "Fork", "fork");
    mark(village, "Clifftop village", "village");
    mark(hamlet, "Plains village", "village");
    for (int s : springs) mark(s, "Hot spring", "spring");
    for (int l : lavas)   mark(l, "Lava lake", "lava");
    for (int cv : caves)  mark(cv, "Cave", "cave");
    return c;
}

} // namespace island
