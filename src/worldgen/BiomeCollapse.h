#pragma once
#include "BiomeTable.h"
#include "IslandChart.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// BiomeCollapse — the island chart by wave function collapse
// ══════════════════════════════════════════════════════════
// Replaces the old pass-by-pass chart (generateIsland). One
// cell is one chunk. The steps:
//
//   outline     the island mask (same shape as before)
//   quadrants   mountain = seed % 4, plains opposite, volcanic
//               and forest on the sides; blended along a warped
//               angle so the borders aren't straight
//   pinned      the ocean edge, the peak (deepest land in the
//               mountain quarter), the swamp (forest coast,
//               nearest the plains)
//   collapse    lowest entropy first; neighbours (diagonals too)
//               must fit the tile rules in BiomeTable.h. Weights
//               come from a target level per cell — coast versus
//               peak distance, shaped by quarter — and each tile's
//               own patch noise, so look-alikes form patches
//   elevation   each cell starts at its target height and is
//               smoothed while staying inside its biome's range
//   water       mountain lake → low mountain lake → swamp; a
//               branch forks at mid height through a plains lake
//               to a delta. Climbing is so costly that rivers
//               follow the land down. Banks become riparian
//   sites       forest lake, lava lake, hot springs, caves,
//               clifftop village, spawn, plains hamlet, and the
//               road village → mountain pass → hamlet
//
// Rivers, lakes, roads and settlements go into chart.biomes
// over the land (so older code still reads them); the land
// itself stays in chart.base.
//
// The browser preview (Island Collapse Lab) follows the same
// rules, but its random numbers differ, so the same seed gives
// a different island there.
namespace island {

namespace collapse_detail {

// mulberry32, as in the preview
struct Rng {
    uint32_t a;
    explicit Rng(uint32_t s) : a(s) {}
    double operator()() {
        a += 0x6D2B79F5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return (double)(t ^ (t >> 14)) / 4294967296.0;
    }
};

inline int popcount(uint32_t m) { int c = 0; while (m) { m &= m - 1; c++; } return c; }
inline int lowestBit(uint32_t m) { int t = 0; while (!(m & 1u)) { m >>= 1; t++; } return t; }

static const int N8[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};

// May a's neighbour be b? (checked both ways)
inline bool sideOK(int a, int b) {
    if ((a == Swamp || a == SwampBeach) && isSeaBiome(b)) return b == SwampShallows;
    if (a == SwampShallows && !isSeaBiome(b)) return b == SwampBeach || b == Swamp || b == Marsh || b == MarshBeach;
    if (a == RockyShallows && !isSeaBiome(b)) return b == SeaCliffs;
    if (a == Peak) return b == Peak || b == HighMountain;
    if (a == Swamp && !isSeaBiome(b))
        return b == Swamp || b == Marsh || b == SwampBeach || b == MarshBeach || b == Forest || b == Shrubland || b == Beach;
    if (a == SwampBeach && !isSeaBiome(b))
        return b == Swamp || b == Marsh || b == SwampBeach || b == MarshBeach || b == Beach;
    return true;
}

// For each tile, the tiles allowed next to it (bit masks)
inline const std::vector<uint32_t>& compat() {
    static const std::vector<uint32_t> C = [] {
        const auto& T = tiles();
        std::vector<uint32_t> c(T.size(), 0);
        for (size_t i = 0; i < T.size(); i++)
            for (size_t j = 0; j < T.size(); j++) {
                int gap = std::max(T[i].lvLo, T[j].lvLo) - std::min(T[i].lvHi, T[j].lvHi);
                if (gap > 1) continue;
                if (!sideOK(T[i].biome, T[j].biome) || !sideOK(T[j].biome, T[i].biome)) continue;
                c[i] |= 1u << j;
            }
        return c;
    }();
    return C;
}

inline std::vector<int> bfsDist(int S, const std::function<bool(int)>& from) {
    std::vector<int> d((size_t)S * S, 9999), q;
    for (int i = 0; i < S * S; i++) if (from(i)) { d[i] = 0; q.push_back(i); }
    for (size_t h = 0; h < q.size(); h++) {
        int i = q[h], x = i % S, y = i / S;
        for (auto& o : N8) {
            int nx = x + o[0], ny = y + o[1];
            if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
            int j = ny * S + nx;
            if (d[j] > d[i] + 1) { d[j] = d[i] + 1; q.push_back(j); }
        }
    }
    return d;
}

// A* (Dijkstra with a goal test) over the chart, 8-way
inline std::vector<int> path(int S, const std::vector<int>& starts, const std::function<bool(int)>& goal,
                             const std::function<double(int, int, double)>& cost) {
    const int n = S * S;
    std::vector<double> g(n, std::numeric_limits<double>::infinity());
    std::vector<int> came(n, -1);
    std::vector<uint8_t> done(n, 0);
    using E = std::pair<double, int>;
    std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
    for (int s : starts) { g[s] = 0; pq.push({ 0, s }); }
    while (!pq.empty()) {
        int i = pq.top().second; pq.pop();
        if (done[i]) continue;
        done[i] = 1;
        if (goal(i)) {
            std::vector<int> p;
            for (int c = i; c >= 0; c = came[c]) p.push_back(c);
            std::reverse(p.begin(), p.end());
            return p;
        }
        int x = i % S, y = i / S;
        for (auto& o : N8) {
            int nx = x + o[0], ny = y + o[1];
            if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
            int j = ny * S + nx;
            double c = cost(i, j, (o[0] && o[1]) ? 1.414 : 1.0);
            if (!std::isfinite(c)) continue;
            if (g[i] + c < g[j]) { g[j] = g[i] + c; came[j] = i; pq.push({ g[j], j }); }
        }
    }
    return {};
}

// The collapse itself. Returns false on a contradiction.
inline bool runCollapse(int S, const std::vector<uint32_t>& domain0, const std::vector<float>& weights,
                        Rng& rand, std::vector<uint8_t>& out, bool& initialFail) {
    const auto& T = tiles();
    const int NT = (int)T.size(), n = S * S;
    const auto& C = compat();
    std::vector<uint32_t> dom = domain0;
    std::vector<uint32_t> stamp(n, 0);
    std::unordered_map<uint32_t, uint32_t> memo;
    auto allowed = [&](uint32_t m) {
        auto it = memo.find(m);
        if (it != memo.end()) return it->second;
        uint32_t u = 0;
        for (int t = 0; t < NT; t++) if (m & (1u << t)) u |= C[t];
        memo[m] = u;
        return u;
    };
    auto entropy = [&](int i) {
        double s = 0, sl = 0;
        for (int t = 0; t < NT; t++) if (dom[i] & (1u << t)) {
            double w = weights[(size_t)i * NT + t] + 1e-6;
            s += w; sl += w * std::log(w);
        }
        return std::log(s) - sl / s;
    };
    struct E { double h; int i; uint32_t st; bool operator>(const E& o) const { return h > o.h; } };
    std::priority_queue<E, std::vector<E>, std::greater<E>> heap;
    auto push = [&](int i) { if (popcount(dom[i]) > 1) heap.push({ entropy(i) + rand() * 1e-4, i, ++stamp[i] }); };
    std::vector<int> stack;
    auto propagate = [&]() -> bool {
        while (!stack.empty()) {
            int i = stack.back(); stack.pop_back();
            uint32_t allow = allowed(dom[i]);
            int x = i % S, y = i / S;
            for (auto& o : N8) {
                int nx = x + o[0], ny = y + o[1];
                if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
                int j = ny * S + nx;
                uint32_t nd = dom[j] & allow;
                if (nd == dom[j]) continue;
                if (!nd) return false;
                dom[j] = nd;
                stack.push_back(j);
                push(j);
            }
        }
        return true;
    };
    for (int i = 0; i < n; i++) stack.push_back(i);
    if (!propagate()) { initialFail = true; return false; }
    for (int i = 0; i < n; i++) push(i);
    while (!heap.empty()) {
        E e = heap.top(); heap.pop();
        if (e.st != stamp[e.i] || popcount(dom[e.i]) <= 1) continue;
        int i = e.i;
        double total = 0;
        for (int t = 0; t < NT; t++) if (dom[i] & (1u << t)) total += weights[(size_t)i * NT + t] + 1e-6;
        double r = rand() * total;
        int pick = -1;
        for (int t = 0; t < NT; t++) if (dom[i] & (1u << t)) {
            r -= weights[(size_t)i * NT + t] + 1e-6;
            pick = t;
            if (r <= 0) break;
        }
        dom[i] = 1u << pick;
        stack.push_back(i);
        if (!propagate()) return false;
    }
    out.resize(n);
    for (int i = 0; i < n; i++) out[i] = T[lowestBit(dom[i])].biome;
    return true;
}

inline float levelToHeight(float L) {
    static const float mid[11] = { 70, 122, 152, 165, 190, 250, 350, 480, 630, 780, 925 };   // levels -2 … 8
    float f = std::clamp(L, -2.0f, 8.0f) + 2.0f;
    int lo = std::min(9, (int)std::floor(f));
    return mid[lo] + (mid[lo + 1] - mid[lo]) * (f - lo);
}

} // namespace collapse_detail

// ══════════════════════════════════════════════════════════
inline Chart collapseIsland(int seed, int S) {
    using namespace collapse_detail;
    const auto& T = tiles();
    const int NT = (int)T.size(), n = S * S;
    auto tileBit = [&](int biome) { return 1u << tileIndex(biome); };
    Chart ch;
    ch.size = S; ch.seed = seed; ch.collapsed = true;

    // ── outline: the old chart's island mask ──────────────
    std::vector<uint8_t> land(n, 0);
    const double cx = (S - 1) / 2.0, cy = (S - 1) / 2.0, rx = S * 0.36, ry = S * 0.33;
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        double wx = (noise(x * 0.07, y * 0.07, seed) - 0.5) * 0.38;
        double wy = (noise(x * 0.07 + 9, y * 0.07, seed + 4) - 0.5) * 0.38;
        double dx = (x - cx) / rx + wx, dy = (y - cy) / ry + wy, edge = noise(x * 0.16, y * 0.16, seed + 9);
        land[y * S + x] = dx * dx + dy * dy < 0.9 + (edge - 0.5) * 0.32 ? 1 : 0;
    }
    {   // keep the largest piece, fill inland holes
        std::vector<int> comp(n, -1);
        int best = -1, bestSize = 0, c = 0;
        for (int i = 0; i < n; i++) if (land[i] && comp[i] < 0) {
            std::vector<int> q{ i }; comp[i] = c;
            for (size_t h = 0; h < q.size(); h++) {
                int k = q[h], x = k % S, y = k / S;
                for (auto& o : N8) {
                    int nx = x + o[0], ny = y + o[1];
                    if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
                    int j = ny * S + nx;
                    if (land[j] && comp[j] < 0) { comp[j] = c; q.push_back(j); }
                }
            }
            if ((int)q.size() > bestSize) { bestSize = (int)q.size(); best = c; }
            c++;
        }
        for (int i = 0; i < n; i++) if (land[i] && comp[i] != best) land[i] = 0;
        std::vector<uint8_t> open(n, 0);
        std::vector<int> q;
        for (int i = 0; i < n; i++) {
            int x = i % S, y = i / S;
            if (!land[i] && (x == 0 || y == 0 || x == S - 1 || y == S - 1)) { open[i] = 1; q.push_back(i); }
        }
        const int N4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (size_t h = 0; h < q.size(); h++) {
            int k = q[h], x = k % S, y = k / S;
            for (auto& o : N4) {
                int nx = x + o[0], ny = y + o[1];
                if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
                int j = ny * S + nx;
                if (!land[j] && !open[j]) { open[j] = 1; q.push_back(j); }
            }
        }
        for (int i = 0; i < n; i++) if (!land[i] && !open[i]) land[i] = 1;
    }
    std::vector<int> landDist  = bfsDist(S, [&](int i) { return !land[i]; });
    std::vector<int> waterDist = bfsDist(S, [&](int i) { return land[i] != 0; });

    // ── quadrants ─────────────────────────────────────────
    const int mountain = seed % 4, plains = (mountain + 2) % 4, volcanic = (mountain + 1) % 4, forest = (mountain + 3) % 4;
    ch.roleMountain = mountain; ch.rolePlains = plains; ch.roleVolcanic = volcanic; ch.roleForest = forest;
    const double PI = 3.14159265358979323846;
    const double QA[4] = { -PI * 0.75, -PI * 0.25, PI * 0.25, PI * 0.75 };   // NW NE SE SW
    std::vector<float> qw((size_t)n * 4);
    ch.quadrant.assign(n, 255);
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        int i = y * S + x;
        double a = std::atan2(y - cy, x - cx) + (noise(x * 0.05, y * 0.05, seed + 21) - 0.5) * 0.9;
        double sum = 0; int bestK = 0; double bestW = -1;
        for (int k = 0; k < 4; k++) {
            double d = std::fabs(std::fmod(std::fmod(a - QA[k], 2 * PI) + 3 * PI, 2 * PI) - PI);
            double w = std::pow(std::max(0.0, std::cos(std::min(d, PI / 2))), 3) + 1e-4;
            qw[(size_t)i * 4 + k] = (float)w; sum += w;
            if (w > bestW) { bestW = w; bestK = k; }
        }
        for (int k = 0; k < 4; k++) qw[(size_t)i * 4 + k] = (float)(qw[(size_t)i * 4 + k] / sum);
        if (land[i]) ch.quadrant[i] = (uint8_t)bestK;
    }
    auto role = [&](int i, int r) { return qw[(size_t)i * 4 + r]; };

    // ── pinned cells ──────────────────────────────────────
    Rng R((uint32_t)(seed * 7 + 3));
    int peak = -1; double peakScore = -1;
    for (int i = 0; i < n; i++) if (land[i] && ch.quadrant[i] == mountain) {
        double s = landDist[i] + R() * 0.5;
        if (s > peakScore) { peakScore = s; peak = i; }
    }
    if (peak < 0) for (int i = 0; i < n; i++) if (land[i] && (peak < 0 || landDist[i] > landDist[peak])) peak = i;
    const int px = peak % S, py = peak / S;
    int swampC = -1; double swampScore = -1e9;
    for (int i = 0; i < n; i++) if (land[i] && ch.quadrant[i] == forest && landDist[i] >= 3 && landDist[i] <= 4) {
        double s = role(i, plains) * 4 - std::hypot(i % S - px, i / S - py) * 0.002 + R() * 0.3;
        if (s > swampScore) { swampScore = s; swampC = i; }
    }
    const double swampR = S >= 120 ? 3.2 : 2.4;
    auto swampDist = [&](int i) { return swampC < 0 ? 999.0 : std::hypot(i % S - swampC % S, i / S - swampC / S); };
    auto peakDist  = [&](int i) { return std::hypot(i % S - px, i / S - py); };

    // ── target levels and weights ─────────────────────────
    std::vector<float> target(n);
    for (int i = 0; i < n; i++) {
        if (!land[i]) { target[i] = waterDist[i] > 6 ? -2.0f : waterDist[i] > 2 ? -1.0f : 0.0f; continue; }
        double t = landDist[i] / (landDist[i] + peakDist(i) * 0.9);
        float qM = role(i, mountain), qP = role(i, plains), qV = role(i, volcanic), qF = role(i, forest);
        // the mountain quarter climbs sooner, so the massif fills it
        float L = 1.0f + 7.2f * (float)std::pow(t, 1.25 - 0.55 * qM);
        L = L + (std::min(L, 2.1f) - L) * qP * 0.9f;                                              // plains stay low
        L = L + ((landDist[i] >= 1 ? std::max(4.0f, std::min(L, 4.4f)) : L) - L) * qV;             // volcanic plateau
        L = L + (std::max(2.0f, std::min(L, 3.3f)) - L) * qF * 0.8f;                               // forest 2–3
        target[i] = L;
    }
    uint32_t seaMask = 0, landMask = 0;
    for (int t = 0; t < NT; t++) (isSeaBiome(T[t].biome) ? seaMask : landMask) |= 1u << t;
    std::vector<float> weights((size_t)n * NT);
    std::vector<uint32_t> domain(n);
    for (int i = 0; i < n; i++) {
        int x = i % S, y = i / S;
        double sd = swampDist(i);
        domain[i] = land[i] ? landMask : seaMask;
        if (x < 2 || y < 2 || x >= S - 2 || y >= S - 2) domain[i] = tileBit(DeepOcean);
        float r[4] = { role(i, mountain), role(i, plains), role(i, volcanic), role(i, forest) };
        for (int t = 0; t < NT; t++) {
            const TileDef& tl = T[t];
            float d = std::max({ 0.0f, tl.lvLo - target[i], target[i] - tl.lvHi });
            double w = std::exp(-d * d / (isSeaBiome(tl.biome) ? 0.08 : 0.5));
            w *= std::exp(5.0 * (noise(x * 0.11 + t * 17.3, y * 0.11 - t * 9.1, seed + 50 + t) - 0.5));
            w *= r[0] * tl.aff[0] + r[1] * tl.aff[1] + r[2] * tl.aff[2] + r[3] * tl.aff[3];
            if (tl.biome == Beach || tl.biome == SeaCliffs) w *= landDist[i] <= 1 ? 1.0 : 0.005;
            if (tl.biome == RockyShallows) w *= waterDist[i] <= 1 ? 1.0 : 0.02;
            if (isSwampy(tl.biome)) w *= sd < swampR + 4 ? 1.5 : 0.002;
            if (tl.biome == Marsh) w *= sd < swampR + 3 ? 3.0 : 0.01;
            if (tl.biome == Plateau) w *= 0.5;
            weights[(size_t)i * NT + t] = (float)w;
        }
    }
    domain[peak] = tileBit(Peak);
    if (swampC >= 0)
        for (int i = 0; i < n; i++)
            if (land[i] && swampDist(i) <= swampR && landDist[i] >= 1) domain[i] &= tileBit(Swamp);

    // ── collapse, retrying with the next stream ───────────
    std::vector<uint8_t> base;
    bool ok = false;
    for (int attempt = 1; attempt <= 30 && !ok; attempt++) {
        Rng cr((uint32_t)(seed * 131 + attempt));
        bool initialFail = false;
        ok = runCollapse(S, domain, weights, cr, base, initialFail);
        if (initialFail) break;
    }
    if (!ok) {
        // Should not happen with these rules; fall back to a
        // plain island so the world still opens
        base.assign(n, DeepOcean);
        for (int i = 0; i < n; i++) if (land[i]) base[i] = landDist[i] <= 1 ? Beach : Plains;
        base[peak] = Peak;
    }
    ch.base = base;

    // ── elevation ─────────────────────────────────────────
    std::vector<float> lo(n), hi(n), h(n), tmp(n);
    for (int i = 0; i < n; i++) {
        const TileDef* tl = tileFor(base[i]);
        lo[i] = tl->hLo; hi[i] = tl->hHi;
        h[i] = std::clamp(levelToHeight(target[i]), lo[i], hi[i]);
    }
    for (int it = 0; it < 60; it++) {
        for (int i = 0; i < n; i++) {
            int x = i % S, y = i / S; float s = 0; int c = 0;
            for (auto& o : N8) {
                int nx = x + o[0], ny = y + o[1];
                if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
                s += h[ny * S + nx]; c++;
            }
            tmp[i] = std::clamp(h[i] * 0.5f + (s / c) * 0.5f, lo[i], hi[i]);
        }
        h.swap(tmp);
    }
    // a sea cliff cell is the clifftop: as high as the land behind it
    for (int i = 0; i < n; i++) if (base[i] == SeaCliffs) {
        float top = h[i];
        int x = i % S, y = i / S;
        for (auto& o : N8) {
            int nx = x + o[0], ny = y + o[1];
            if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
            int j = ny * S + nx;
            if (land[j] && base[j] != SeaCliffs) top = std::max(top, h[j]);
        }
        h[i] = std::min(top, (float)tileFor(SeaCliffs)->hHi);
    }

    // ── water and settlements over the land ───────────────
    std::vector<uint8_t> ov(n, 0);
    auto lvl = [&](int i) { return (int)tileFor(base[i])->lvLo; };
    auto stampDisk = [&](int c, double r, int biome, const std::function<bool(int)>& okFn) {
        int x0 = c % S, y0 = c / S, rr = (int)std::ceil(r);
        for (int y = y0 - rr; y <= y0 + rr; y++) for (int x = x0 - rr; x <= x0 + rr; x++) {
            if (x < 0 || y < 0 || x >= S || y >= S) continue;
            int j = y * S + x;
            if (std::hypot(x - x0, y - y0) <= r && land[j] && okFn(j)) ov[j] = (uint8_t)biome;
        }
    };
    auto pickBest = [&](const std::function<bool(int)>& filter, const std::function<double(int)>& score) {
        int b = -1; double bs = -1e18;
        for (int i = 0; i < n; i++) if (filter(i)) { double s = score(i); if (s > bs) { bs = s; b = i; } }
        return b;
    };
    auto cellDist = [&](int a, int b) { return std::hypot(a % S - b % S, a / S - b / S); };
    auto isLakeOv = [](int b) { return b == MountainLake || b == LowMountainLake || b == PlainsLake || b == ForestLake || b == LavaLake; };

    int mlake = pickBest([&](int i) { return land[i] && (base[i] == Plateau || base[i] == MidMountain); },
        [&](int i) { return (base[i] == Plateau ? 2 : 0) - std::fabs(peakDist(i) - 5) - swampDist(i) * 0.05 + R() * 0.5; });
    if (mlake >= 0) stampDisk(mlake, 1.2, MountainLake, [&](int j) { return lvl(j) >= 5; });

    auto riverCost = [&](int a, int b, double d) -> double {
        if (!land[b] || base[b] == Peak) return std::numeric_limits<double>::infinity();
        double up = std::max(0.0f, h[b] - h[a]);
        double wander = noise((b % S) * 0.35, (b / S) * 0.35, seed + 77) * 1.5;   // so rivers meander
        bool crossing = ov[b] && !isLakeOv(ov[b]);
        return d * (1 + up * 0.8 + wander) + (crossing ? 0.5 : 0);
    };
    std::vector<int> mainR, branchR;
    if (mlake >= 0) {
        mainR = path(S, { mlake }, [&](int i) { return swampC >= 0 ? base[i] == Swamp : !land[i]; }, riverCost);
        if (!mainR.empty()) {
            for (int i : mainR)
                if (base[i] == LowMountain && landDist[i] >= 3) { stampDisk(i, 1.1, LowMountainLake, [&](int j) { return lvl(j) >= 4; }); break; }
            int forkIdx = -1;
            for (size_t k = 0; k < mainR.size(); k++) if (h[mainR[k]] < 330) { forkIdx = (int)k; break; }
            if (forkIdx > 0) {
                int fork = mainR[forkIdx];
                std::vector<uint8_t> nearMain(n, 0);
                for (int m : mainR) {
                    int x0 = m % S, y0 = m / S;
                    for (int y = y0 - 2; y <= y0 + 2; y++) for (int x = x0 - 2; x <= x0 + 2; x++)
                        if (x >= 0 && y >= 0 && x < S && y < S && !nearMain[y * S + x]) nearMain[y * S + x] = 1;
                }
                for (int m : mainR) nearMain[m] = 2;
                branchR = path(S, { fork }, [&](int i) { return land[i] && base[i] == Beach && ch.quadrant[i] == plains; },
                    [&](int a, int b, double d) {
                        double c = riverCost(a, b, d);
                        if (b == fork) return c;
                        return c + (nearMain[b] == 2 ? 12 : nearMain[b] == 1 ? 3 : 0);
                    });
                if (!branchR.empty()) {
                    ov[fork] = RiverFork;
                    for (int i : branchR)
                        if ((base[i] == Plains || base[i] == Grassland) && landDist[i] >= 3) {
                            stampDisk(i, 1.3, PlainsLake, [&](int j) { return lvl(j) <= 2 && base[j] != Swamp; });
                            break;
                        }
                    stampDisk(branchR.back(), 1.5, Delta, [&](int j) { return landDist[j] <= 2 && lvl(j) <= 2; });
                }
            }
        }
    }
    // river cells by stretch; the bed only ever goes down
    for (auto* rp : { &mainR, &branchR }) {
        if (rp->empty()) continue;
        float run = 1e9f;
        for (int i : *rp) {
            run = std::min(run, h[i]);
            h[i] = run;
            if (isLakeOv(ov[i]) || ov[i] == Delta || ov[i] == RiverFork) continue;
            ov[i] = (uint8_t)(isSwampy(base[i]) ? SwampRiver : lvl(i) >= 5 ? MountainRiver : LowlandRiver);
        }
    }
    // the delta's mouth reaches the sea
    if (!branchR.empty()) {
        int m = branchR.back(), x0 = m % S, y0 = m / S;
        for (auto& o : N8) {
            int nx = x0 + o[0], ny = y0 + o[1];
            if (nx >= 0 && ny >= 0 && nx < S && ny < S && !land[ny * S + nx]) { branchR.push_back(ny * S + nx); break; }
        }
    }
    for (auto* rp : { &mainR, &branchR })
        for (int i : *rp) {
            if (!land[i] || lvl(i) > 3) continue;
            int x0 = i % S, y0 = i / S;
            for (auto& o : N8) {
                int nx = x0 + o[0], ny = y0 + o[1];
                if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
                int j = ny * S + nx;
                if (!land[j] || ov[j] || isSwampy(base[j]) || base[j] == Beach) continue;
                int b = base[j];
                if (b == Plains || b == Grassland || b == Forest || b == Shrubland || b == DenseForest) ov[j] = Riparian;
            }
        }
    if (!mainR.empty())   ch.rivers.push_back(mainR);
    if (!branchR.empty()) ch.rivers.push_back(branchR);

    auto distToOv = [&](int i) {
        double d = 99; int x0 = i % S, y0 = i / S;
        for (int y = y0 - 8; y <= y0 + 8; y++) for (int x = x0 - 8; x <= x0 + 8; x++)
            if (x >= 0 && y >= 0 && x < S && y < S && ov[y * S + x]) d = std::min(d, std::hypot(x - x0, y - y0));
        return d;
    };
    int flake = pickBest([&](int i) { return land[i] && (base[i] == DenseForest || base[i] == Forest) && !ov[i] && landDist[i] >= 4; },
        [&](int i) { return role(i, forest) * 3 + std::min(8.0, distToOv(i)) * 0.3 + R() * 0.4; });
    if (flake >= 0) stampDisk(flake, 1.5, ForestLake, [&](int j) { return (base[j] == Forest || base[j] == DenseForest || base[j] == Shrubland) && !ov[j]; });

    // settlements
    auto onCliffEdge = [&](int i) {
        int x0 = i % S, y0 = i / S;
        for (auto& o : N8) {
            int nx = x0 + o[0], ny = y0 + o[1];
            if (nx >= 0 && ny >= 0 && nx < S && ny < S && base[ny * S + nx] == SeaCliffs) return true;
        }
        return false;
    };
    int village = pickBest([&](int i) { return land[i] && ch.quadrant[i] == volcanic && lvl(i) == 4 && !ov[i] && onCliffEdge(i); },
                           [&](int i) { return -peakDist(i) * 0.02 + R(); });
    if (village >= 0) { stampDisk(village, 0.8, Village, [&](int j) { return !ov[j]; }); ch.markers.push_back({ village % S, village / S, "Clifftop village", "village" }); }
    int spawn = pickBest([&](int i) { return land[i] && ch.quadrant[i] == plains && (base[i] == Plains || base[i] == Grassland) && landDist[i] == 2 && !ov[i]; },
                         [&](int i) { return role(i, plains) + R() * 0.2; });
    if (spawn < 0) spawn = pickBest([&](int i) { return land[i] && !ov[i] && lvl(i) <= 3; }, [&](int i) { return -landDist[i] + R(); });
    if (spawn >= 0) ch.markers.push_back({ spawn % S, spawn / S, "Spawn", "spawn" });
    int hamlet = pickBest([&](int i) { return land[i] && ch.quadrant[i] == plains && (base[i] == Plains || base[i] == Grassland) && landDist[i] >= 4 && !ov[i]; },
                          [&](int i) { return -std::fabs(landDist[i] - 6.0) + R() * 0.5 - (spawn >= 0 ? std::max(0.0, 6 - cellDist(i, spawn)) : 0); });
    if (hamlet >= 0) { stampDisk(hamlet, 0.8, PlainsVillage, [&](int j) { return !ov[j]; }); ch.markers.push_back({ hamlet % S, hamlet / S, "Plains hamlet", "village" }); }

    auto volc = [&](int i) { return land[i] && (base[i] == Ashlands || base[i] == HighlandMoor) && !ov[i]; };
    int lava = pickBest([&](int i) { return volc(i) && base[i] == Ashlands; }, [&](int i) { return landDist[i] + R(); });
    if (lava >= 0) { stampDisk(lava, 1.2, LavaLake, volc); ch.markers.push_back({ lava % S, lava / S, "Lava lake", "lava" }); }
    std::vector<int> placed;
    auto farFrom = [&](int i, double d) { for (int j : placed) if (cellDist(i, j) < d) return false; return true; };
    for (int k = 0; k < 2; k++) {
        int hs = pickBest([&](int i) { return volc(i) && farFrom(i, 6) && (village < 0 || cellDist(i, village) >= 4); }, [&](int) { return R(); });
        if (hs >= 0) { ov[hs] = HotSpring; placed.push_back(hs); ch.markers.push_back({ hs % S, hs / S, "Hot spring", "spring" }); }
    }
    if (village >= 0) {
        int x0 = village % S, y0 = village / S;
        for (auto& o : N8) {
            int nx = x0 + o[0], ny = y0 + o[1];
            if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
            int j = ny * S + nx;
            if (land[j] && !ov[j] && lvl(j) == 4) { ov[j] = HotSpring; placed.push_back(j); ch.markers.push_back({ nx, ny, "Hot spring", "spring" }); break; }
        }
    }
    for (int k = 0; k < 3; k++) {
        int cv = pickBest([&](int i) {
                return land[i] && !ov[i] && farFrom(i, 5) &&
                       (k < 2 ? (base[i] == Ashlands || base[i] == HighlandMoor || base[i] == Scrubland) : base[i] == SeaCliffs);
            }, [&](int) { return R(); });
        if (cv >= 0) { ov[cv] = Cave; placed.push_back(cv); ch.markers.push_back({ cv % S, cv / S, "Cave", "cave" }); }
    }

    // the road: village → mountain pass → hamlet
    if (village >= 0 && hamlet >= 0) {
        int pass = pickBest([&](int i) { return land[i] && base[i] == LowMountain && !ov[i]; },
                            [&](int i) { return -(h[i] - 400) * 0.08 - cellDist(i, village) - cellDist(i, hamlet); });
        auto roadCost = [&](int a, int b, double d) -> double {
            if (!land[b] || lvl(b) >= 7 || ov[b] == LavaLake || ov[b] == Cave) return std::numeric_limits<double>::infinity();
            int o = ov[b];
            bool water = o == LowlandRiver || o == MountainRiver || o == SwampRiver || isLakeOv(o) || o == Delta;
            return d * (1 + std::fabs(h[b] - h[a]) * 0.05) + (water ? 6 : 0) + (isSwampy(base[b]) ? 3 : 0);
        };
        std::vector<int> road;
        if (pass >= 0) {
            auto a = path(S, { village }, [&](int i) { return i == pass; }, roadCost);
            auto b = path(S, { pass }, [&](int i) { return i == hamlet; }, roadCost);
            if (!a.empty() && !b.empty()) {
                road = a; road.insert(road.end(), b.begin() + 1, b.end());
                ov[pass] = MountainPass;
                ch.markers.push_back({ pass % S, pass / S, "Mountain pass", "pass" });
            }
        }
        if (road.empty()) road = path(S, { village }, [&](int i) { return i == hamlet; }, roadCost);
        for (int i : road) if (!ov[i]) ov[i] = Road;
        ch.road = road;
    }
    ch.markers.push_back({ px, py, "Peak", "peak" });

    // ── into the chart ────────────────────────────────────
    ch.biomes.resize(n);
    ch.elev.resize(n);
    for (int i = 0; i < n; i++) {
        ch.biomes[i] = ov[i] ? ov[i] : base[i];
        ch.elev[i] = std::clamp((h[i] - 160.0f) / (900.0f - 160.0f), 0.0f, 1.0f);
    }
    ch.height = h;
    return ch;
}

// ══════════════════════════════════════════════════════════
// A single-biome world: the Classic size, one biome all over,
// with the features that belong to that biome
inline Chart singleBiomeChart(int seed, int biome, int S = 32) {
    using namespace collapse_detail;
    const TileDef* tl = tileFor(biome);
    if (!tl || isSeaBiome(biome)) { biome = Forest; tl = tileFor(Forest); }
    const int n = S * S;
    Chart ch;
    ch.size = S; ch.seed = seed; ch.collapsed = true; ch.singleBiome = biome;
    ch.base.assign(n, (uint8_t)biome);
    ch.quadrant.assign(n, 255);
    std::vector<float> h(n);
    for (int i = 0; i < n; i++) {
        int x = i % S, y = i / S;
        double f = noise(x * 0.09, y * 0.09, seed) * 0.7 + noise(x * 0.23, y * 0.23, seed + 3) * 0.3;
        h[i] = (float)(tl->hLo + (tl->hHi - tl->hLo) * (0.15 + 0.7 * f));
    }
    std::vector<uint8_t> ov(n, 0);
    Rng R((uint32_t)(seed * 11 + 5));
    auto has = [&](std::initializer_list<int> l) { for (int b : l) if (b == biome) return true; return false; };
    auto randCell = [&](int m) { return (m + (int)(R() * (S - 2 * m))) + (m + (int)(R() * (S - 2 * m))) * S; };
    auto stampDisk = [&](int c, double r, int b) {
        int x0 = c % S, y0 = c / S;
        for (int y = y0 - 2; y <= y0 + 2; y++) for (int x = x0 - 2; x <= x0 + 2; x++)
            if (x >= 0 && y >= 0 && x < S && y < S && std::hypot(x - x0, y - y0) <= r) ov[y * S + x] = (uint8_t)b;
    };
    if (has({ Plains, Grassland, Forest, DenseForest, Shrubland, Swamp, Marsh, Foothills, LowMountain, MidMountain })) {
        auto edge = [&](int i) { int x = i % S, y = i / S; return x == 0 || y == 0 || x == S - 1 || y == S - 1; };
        int hiE = -1; for (int i = 0; i < n; i++) if (edge(i) && (hiE < 0 || h[i] > h[hiE])) hiE = i;
        int hx = hiE % S, hy = hiE / S;
        auto opposite = [&](int i) {
            int x = i % S, y = i / S;
            if (hy == 0) return y == S - 1;
            if (hy == S - 1) return y == 0;
            if (hx == 0) return x == S - 1;
            return x == 0;
        };
        int loE = -1; for (int i = 0; i < n; i++) if (opposite(i) && (loE < 0 || h[i] < h[loE])) loE = i;
        auto rp = path(S, { hiE }, [&](int i) { return i == loE; }, [&](int a, int c, double d) {
            int x = c % S, y = c / S;
            bool nearEdge = std::min({ x, y, S - 1 - x, S - 1 - y }) < 2 && c != loE;
            double wander = noise(x * 0.35, y * 0.35, seed + 77) * 2.5;
            return d * (1 + std::max(0.0f, h[c] - h[a]) * 0.8 + wander) + (nearEdge && a != hiE ? 4 : 0);
        });
        float run = 1e9f;
        for (int i : rp) {
            run = std::min(run, h[i]); h[i] = run;
            ov[i] = (uint8_t)(isSwampy(biome) ? SwampRiver : tl->lvLo >= 5 ? MountainRiver : LowlandRiver);
        }
        if (!rp.empty()) ch.rivers.push_back(rp);
    }
    if (has({ Forest, DenseForest, Shrubland })) stampDisk(randCell(4), 1.6, ForestLake);
    if (has({ Plains, Grassland })) {
        stampDisk(randCell(4), 1.4, PlainsLake);
        int v = randCell(6); stampDisk(v, 0.8, PlainsVillage);
        ch.markers.push_back({ v % S, v / S, "Hamlet", "village" });
    }
    if (has({ LowMountain, MidMountain, Plateau, HighMountain })) stampDisk(randCell(4), 1.3, MountainLake);
    if (has({ Ashlands })) stampDisk(randCell(4), 1.4, LavaLake);
    if (has({ Ashlands, HighlandMoor, Scrubland })) {
        for (int k = 0; k < 2; k++) ov[randCell(4)] = HotSpring;
        ov[randCell(4)] = Cave;
    }
    if (has({ Foothills, HighlandMoor })) {
        int v = randCell(6); stampDisk(v, 0.8, Village);
        ch.markers.push_back({ v % S, v / S, "Village", "village" });
    }
    ch.markers.push_back({ S / 2, S / 2, "Spawn", "spawn" });
    ch.biomes.resize(n);
    ch.elev.resize(n);
    for (int i = 0; i < n; i++) {
        ch.biomes[i] = ov[i] ? ov[i] : (uint8_t)biome;
        ch.elev[i] = std::clamp((h[i] - 160.0f) / (900.0f - 160.0f), 0.0f, 1.0f);
    }
    ch.height = h;
    return ch;
}

} // namespace island
