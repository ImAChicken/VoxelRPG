#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <thread>
#include <vector>

// ══════════════════════════════════════════════════════════
// TerrainBake — the whole island's ground, worked out once
// ══════════════════════════════════════════════════════════
// Noise alone gives soft, blobby hills. Real land is shaped by
// water: rain runs downhill, picks up soil, carves gullies and
// valleys, sharpens ridges and drops what it carries where it
// slows down; loose stones slide until they come to rest in
// scree slopes. Water does that over the whole slope above a
// spot, so it can't be worked out column by column. Instead the
// ground is laid out on a grid (one sample every STEP voxels),
// eroded once when the world opens, and read from then on.
//
//   1  base      the generator's own height (chart + biome relief)
//   2  rain      the rain is routed downhill and each spot is cut
//                down in proportion to the water passing it and its
//                slope (stream-power erosion): big flows cut valleys,
//                small ones gullies, ridges stay sharp. How hard each
//                spot erodes comes from its biome (mountains a lot,
//                plains a little, beaches and villages not at all)
//   3  scree     ground left steeper than loose rock can stand
//                slides down and piles at the foot (thermal erosion)
//   4  lakes     hollows fill with water up to where they would
//                spill over; tiny pits are filled in with ground
//   5  flow      how much rain passes each spot on its way to
//                the sea
//   6  rivers    lines where the flow is big enough, traced from
//                source to mouth; width grows with the flow, the
//                water surface always steps down toward the sea
//
// Runs on one thread in a fixed order, so the same seed always
// gives the same ground. Everything is read-only afterwards, so
// the chunk workers can read it freely.
class TerrainBake {
public:
    static constexpr int STEP = 8;            // voxels per sample

    struct RiverPoint { float x, z, surface, halfWidth; };
    using RiverLine = std::vector<RiverPoint>;

    struct Settings {
        float seaLevel   = 160.0f;
        int   erosionPasses = 5;
        float streamPower   = 0.03f;          // how hard flowing water cuts (× √flow)
        float riverFlow  = 2600.0f;           // samples draining into a spot before it's a river
        int   thermalPasses = 4;
        float minLakeDepth  = 14.0f;          // voxels, for a natural lake to stay
        int   minLakeCells  = 200;            // samples
        int   maxNaturalLakes = 4;            // besides the designed ones
        float fillShare = 0.5f;               // of an unwanted hollow's depth filled in; the rest is cut through its rim
    };

    // Statistics for the log
    struct Stats { int lakes = 0; int rivers = 0; };

    TerrainBake() = default;

    //   worldVoxels    world width (and depth) in voxels
    //   baseHeight     ground height at a world point, before erosion
    //   erodibility    0..1 at a world point: how much it erodes
    //   lakeAt         0 none, 1 a designed lake, 2 a designed lava lake
    //   stopRiver      true at world points where traced rivers
    //                  should end (they've joined a designed river)
    void build(int worldVoxels, const Settings& st,
               const std::function<float(int, int)>& baseHeight,
               const std::function<float(int, int)>& erodibility,
               const std::function<int(int, int)>& lakeAt,
               const std::function<bool(int, int)>& stopRiver,
               uint32_t seed);

    bool ready() const { return S > 0; }
    int  size()  const { return S; }

    // Ground height at a world point (smooth, between samples)
    float height(float wx, float wz) const { return cubic(h, wx, wz); }
    // Change from erosion: + where material was laid down, − where cut
    float change(float wx, float wz) const { return bilinear(dh, wx, wz); }
    // Lake water: depth above the ground (0 = none) and surface
    bool lake(float wx, float wz, float& surface, bool& lava) const;
    // Steepness (rise over run) of the eroded ground
    float slope(float wx, float wz) const {
        float a = height(wx - 4, wz), b = height(wx + 4, wz);
        float c = height(wx, wz - 4), d = height(wx, wz + 4);
        return std::max(std::fabs(b - a), std::fabs(d - c)) / 8.0f;
    }

    const std::vector<RiverLine>& rivers() const { return lines; }
    const Stats& stats() const { return stat; }

    // Water surface (filled height) at a world point — the level
    // a river or lake here sits at
    float filled(float wx, float wz) const { return bilinear(F, wx, wz); }

private:
    int S = 0;                       // samples per side
    std::vector<float>   h;          // eroded ground, voxels
    std::vector<float>   dh;         // h − base
    std::vector<float>   F;          // water level (h, raised in hollows)
    std::vector<uint8_t> lakeFlag;   // 1 water, 2 lava
    std::vector<RiverLine> lines;
    Stats stat;

    int cellOf(float wx, float wz) const {
        int x = (int)std::floor(wx / STEP + 0.5f), z = (int)std::floor(wz / STEP + 0.5f);
        if (x < 0 || z < 0 || x >= S || z >= S) return -1;
        return z * S + x;
    }
    float at(const std::vector<float>& g, int x, int z) const {
        x = std::clamp(x, 0, S - 1); z = std::clamp(z, 0, S - 1);
        return g[(size_t)z * S + x];
    }
    float bilinear(const std::vector<float>& g, float wx, float wz) const {
        float fx = wx / STEP, fz = wz / STEP;
        int x0 = (int)std::floor(fx), z0 = (int)std::floor(fz);
        float tx = fx - x0, tz = fz - z0;
        float a = at(g, x0, z0), b = at(g, x0 + 1, z0), c = at(g, x0, z0 + 1), d = at(g, x0 + 1, z0 + 1);
        return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz;
    }
    // Catmull-Rom over 4 × 4 samples, clamped to the middle four
    // so it never overshoots into bumps
    float cubic(const std::vector<float>& g, float wx, float wz) const {
        float fx = wx / STEP, fz = wz / STEP;
        int x0 = (int)std::floor(fx), z0 = (int)std::floor(fz);
        float tx = fx - x0, tz = fz - z0;
        auto w = [](float t, float o[4]) {
            float t2 = t * t, t3 = t2 * t;
            o[0] = 0.5f * (-t3 + 2 * t2 - t);
            o[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
            o[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
            o[3] = 0.5f * (t3 - t2);
        };
        float wx4[4], wz4[4];
        w(tx, wx4); w(tz, wz4);
        float v = 0, lo = 1e9f, hi = -1e9f;
        for (int j = 0; j < 4; j++)
            for (int i = 0; i < 4; i++) {
                float s = at(g, x0 - 1 + i, z0 - 1 + j);
                v += wx4[i] * wz4[j] * s;
                if (i >= 1 && i <= 2 && j >= 1 && j <= 2) { lo = std::min(lo, s); hi = std::max(hi, s); }
            }
        return std::clamp(v, lo, hi);
    }
};

// ── Implementation ────────────────────────────────────────
inline bool TerrainBake::lake(float wx, float wz, float& surface, bool& lava) const {
    if (!S) return false;
    int x0 = (int)std::floor(wx / STEP), z0 = (int)std::floor(wz / STEP);
    // only near lake samples
    int flag = 0;
    for (int j = 0; j <= 1; j++) for (int i = 0; i <= 1; i++) {
        int x = std::clamp(x0 + i, 0, S - 1), z = std::clamp(z0 + j, 0, S - 1);
        flag = std::max(flag, (int)lakeFlag[(size_t)z * S + x]);
    }
    if (!flag) return false;
    float f = bilinear(F, wx, wz), g = bilinear(h, wx, wz);
    if (f - g < 0.6f) return false;
    surface = f;
    lava = flag == 2;
    return true;
}

inline void TerrainBake::build(int worldVoxels, const Settings& st,
                               const std::function<float(int, int)>& baseHeight,
                               const std::function<float(int, int)>& erodibility,
                               const std::function<int(int, int)>& lakeAt,
                               const std::function<bool(int, int)>& stopRiver,
                               uint32_t seed) {
    const float step = (float)STEP;
    S = worldVoxels / STEP + 1;
    const int n = S * S;
    std::vector<float> base(n), hn(n);
    std::vector<uint8_t> mask(n);
    const float seaN = st.seaLevel / step;
    // heights are worked in sample units (voxels / STEP), so a
    // slope of 1 means 45°, whatever the grid spacing
    // (rows on several threads: each sample stands alone, so the
    // result is the same however the work is split)
    {
        auto rows = [&](int z0, int z1) {
            for (int z = z0; z < z1; z++)
                for (int x = 0; x < S; x++) {
                    int i = z * S + x;
                    base[i] = baseHeight(x * STEP, z * STEP);
                    hn[i] = base[i] / step;
                    mask[i] = (uint8_t)std::lround(std::clamp(erodibility(x * STEP, z * STEP), 0.0f, 1.0f) * 255.0f);
                }
        };
        int threads = (int)std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; t++)
            pool.emplace_back(rows, S * t / threads, S * (t + 1) / threads);
        for (auto& th : pool) th.join();
    }

    // ── 2 rain: stream-power erosion ──────────────────────
    // Each pass: route the rain (every sample sends its water to
    // its lowest way out, found by flooding up from the sea),
    // count how much passes each sample, then lower each sample
    // toward the one it drains to — more where more water passes
    // (Braun & Willett's implicit scheme, solved from the sea
    // upward, so it stays stable however strong). Big flows cut
    // deep valleys, small ones gullies, and the ridges between
    // stay sharp.
    static const int N8[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
    auto route = [&](std::vector<int>& recv, std::vector<int>& order, std::vector<float>* Fout) {
        recv.assign(n, -1);
        order.clear(); order.reserve(n);
        std::vector<uint8_t> seen(n, 0);
        std::vector<float> Fl(n);
        using E = std::pair<float, int>;
        std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
        for (int i = 0; i < n; i++) {
            int x = i % S, z = i / S;
            if (hn[i] <= seaN || x == 0 || z == 0 || x == S - 1 || z == S - 1) {
                Fl[i] = hn[i]; seen[i] = 1; pq.push({ Fl[i], i });
            }
        }
        while (!pq.empty()) {
            auto [f, i] = pq.top(); pq.pop();
            order.push_back(i);
            int x = i % S, z = i / S;
            for (auto& o : N8) {
                int nx2 = x + o[0], nz2 = z + o[1];
                if (nx2 < 0 || nz2 < 0 || nx2 >= S || nz2 >= S) continue;
                int j = nz2 * S + nx2;
                if (seen[j]) continue;
                seen[j] = 1;
                Fl[j] = std::max(hn[j], f + 1e-5f);
                recv[j] = i;
                pq.push({ Fl[j], j });
            }
        }
        if (Fout) Fout->swap(Fl);
    };
    std::vector<int> recv, order;
    std::vector<float> A(n);
    for (int pass = 0; pass < st.erosionPasses; pass++) {
        route(recv, order, nullptr);
        for (int i = 0; i < n; i++) A[i] = hn[i] > seaN ? 1.0f : 0.0f;
        for (int k = (int)order.size() - 1; k >= 0; k--) {
            int i = order[k];
            if (recv[i] >= 0) A[recv[i]] += A[i];
        }
        for (int i : order) {
            int r = recv[i];
            if (r < 0 || hn[i] <= seaN || mask[i] == 0) continue;
            int dxr = std::abs(r % S - i % S), dzr = std::abs(r / S - i / S);
            float dist = (dxr && dzr) ? 1.414f : 1.0f;
            float f = st.streamPower * (mask[i] / 255.0f) * std::sqrt(A[i]) / dist;
            float hNew = (hn[i] + f * hn[r]) / (1.0f + f);
            if (hNew < hn[i]) hn[i] = std::max(hNew, seaN + 0.05f);   // only cut down, never below the sea
        }
    }

    // ── 3 scree ───────────────────────────────────────────
    // Where cutting left ground steeper than loose rock can stand
    // (on erodible ground only), the top slides down and piles at
    // the foot: valley walls ease into a V with scree below
    {
        const float talus = 1.6f;     // ~58°
        for (int pass = 0; pass < st.thermalPasses; pass++)
            for (int z = 1; z < S - 1; z++)
                for (int x = 1; x < S - 1; x++) {
                    int i = z * S + x;
                    if (mask[i] < 40 || hn[i] <= seaN) continue;
                    float loose = 1e9f;
                    int lowJ = -1; float lowD = 0;
                    for (auto& o : N8) {
                        int j = (z + o[1]) * S + x + o[0];
                        float d = (hn[i] - hn[j]) / ((o[0] && o[1]) ? 1.414f : 1.0f);
                        if (d > lowD) { lowD = d; lowJ = j; }
                    }
                    if (lowJ < 0 || lowD <= talus) continue;
                    float move = std::min(loose, (lowD - talus) * 0.25f * (mask[i] / 255.0f));
                    hn[i] -= move; hn[lowJ] += move;
                }
    }

    // ── 4 lakes and 5 flow ────────────────────────────────
    // Priority flood from the sea and the map edge: every sample
    // is reached from a lower neighbour, which is where its water
    // goes. Hollows get a water level at their spill point.
    std::vector<float> Fn;
    route(recv, order, &Fn);
    // lakes: the designed ones, plus the few biggest natural
    // basins; every other hollow is filled with ground (it becomes
    // a flat valley floor) so the land drains like real land
    lakeFlag.assign(n, 0);
    {
        struct Hollow { std::vector<int> cells; float deepest = 0, volume = 0; bool designed = false, lava = false; };
        std::vector<Hollow> hollows;
        std::vector<int> comp(n, -1);
        static const int N4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (int i = 0; i < n; i++) {
            if (comp[i] >= 0 || Fn[i] - hn[i] < 0.02f || hn[i] <= seaN) continue;
            Hollow hw;
            hw.cells.push_back(i);
            comp[i] = i;
            for (size_t k = 0; k < hw.cells.size(); k++) {
                int c = hw.cells[k], x = c % S, z = c / S;
                float d = Fn[c] - hn[c];
                hw.deepest = std::max(hw.deepest, d);
                hw.volume += d;
                int la = lakeAt(x * STEP, z * STEP);
                if (la) hw.designed = true;
                if (la == 2) hw.lava = true;
                for (auto& o : N4) {
                    int nx2 = x + o[0], nz2 = z + o[1];
                    if (nx2 < 0 || nz2 < 0 || nx2 >= S || nz2 >= S) continue;
                    int j = nz2 * S + nx2;
                    if (comp[j] < 0 && Fn[j] - hn[j] >= 0.02f && hn[j] > seaN) { comp[j] = i; hw.cells.push_back(j); }
                }
            }
            hollows.push_back(std::move(hw));
        }
        // natural candidates, biggest first
        std::vector<int> natural;
        for (int k = 0; k < (int)hollows.size(); k++) {
            const Hollow& hw = hollows[k];
            if (!hw.designed && hw.deepest * step >= st.minLakeDepth && (int)hw.cells.size() >= st.minLakeCells)
                natural.push_back(k);
        }
        std::sort(natural.begin(), natural.end(), [&](int a, int b) { return hollows[a].volume > hollows[b].volume; });
        std::vector<uint8_t> keep(hollows.size(), 0);
        for (int k = 0; k < (int)natural.size() && k < st.maxNaturalLakes; k++) keep[natural[k]] = 1;
        for (int k = 0; k < (int)hollows.size(); k++) {
            Hollow& hw = hollows[k];
            if (hw.designed && hw.deepest * step >= 3.0f && hw.cells.size() >= 12) keep[k] = 1;
            if (keep[k]) {
                stat.lakes++;
                for (int c : hw.cells) lakeFlag[c] = hw.lava ? 2 : 1;
            } else {
                // Drain it: fill the bottom part of the hollow, then
                // cut a notch through the rim down to that level,
                // following the way the water would leave
                int low = hw.cells[0];
                for (int c : hw.cells) if (hn[c] < hn[low]) low = c;
                float spill = Fn[low];
                float level = hn[low] + (spill - hn[low]) * st.fillShare;
                for (int c : hw.cells) hn[c] = std::max(hn[c], std::min(Fn[c], level));
                float L = level;
                for (int c = recv[low], guard = 0; c >= 0 && guard < 4096; c = recv[c], guard++) {
                    L -= 0.002f;
                    if (hn[c] <= L) break;            // past the rim: downhill again
                    hn[c] = L;
                }
            }
        }
    }
    std::vector<float> flow(n, 0.0f);
    for (int i = 0; i < n; i++) if (hn[i] > seaN) flow[i] = 1.0f;
    for (int k = (int)order.size() - 1; k >= 0; k--) {
        int i = order[k];
        if (recv[i] >= 0) flow[recv[i]] += flow[i];
    }

    // ── 6 rivers ──────────────────────────────────────────
    // Sources: big-enough flow with no big-enough flow upstream.
    // Each is followed downstream to the sea, a lake, a river
    // already traced (a confluence) or a designed river.
    {
        std::vector<uint8_t> isUp(n, 0);     // has an upstream river cell
        for (int i = 0; i < n; i++)
            if (flow[i] >= st.riverFlow && recv[i] >= 0) isUp[recv[i]] = 1;
        std::vector<uint8_t> traced(n, 0);
        std::vector<int> sources;
        for (int i = 0; i < n; i++)
            if (flow[i] >= st.riverFlow && !isUp[i] && hn[i] > seaN) sources.push_back(i);
        // biggest first, so main stems are traced before tributaries
        std::sort(sources.begin(), sources.end(), [&](int a, int b) { return flow[a] > flow[b]; });
        for (int s : sources) {
            std::vector<int> cells;
            int c = s;
            bool joined = false;
            while (c >= 0) {
                int x = c % S, z = c / S;
                if (stopRiver(x * STEP, z * STEP)) { cells.push_back(c); joined = true; break; }
                if (lakeFlag[c] == 2) break;            // never through lava
                cells.push_back(c);
                if (traced[c]) { joined = true; break; }
                traced[c] = 1;
                if (hn[c] <= seaN) break;
                c = recv[c];
            }
            (void)joined;
            if (cells.size() < 4) continue;
            // every third sample, then corner-cut twice: a smooth line
            std::vector<RiverPoint> pts;
            for (size_t k = 0; k < cells.size(); k++) {
                if (k % 3 && k + 1 != cells.size()) continue;
                int ci = cells[k];
                float f = flow[ci] / st.riverFlow;
                pts.push_back({ (float)(ci % S) * step, (float)(ci / S) * step, Fn[ci] * step,
                                std::clamp(2.5f + 4.0f * std::sqrt(std::max(0.0f, f - 0.99f)), 2.5f, 24.0f) });
            }
            for (int it = 0; it < 2 && pts.size() > 2; it++) {
                std::vector<RiverPoint> out{ pts.front() };
                for (size_t k = 0; k + 1 < pts.size(); k++) {
                    const auto &a = pts[k], &b = pts[k + 1];
                    for (float t : { 0.25f, 0.75f })
                        out.push_back({ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t,
                                        a.surface + (b.surface - a.surface) * t, a.halfWidth + (b.halfWidth - a.halfWidth) * t });
                }
                out.push_back(pts.back());
                pts.swap(out);
            }
            // the surface only steps down
            for (size_t k = 1; k < pts.size(); k++) pts[k].surface = std::min(pts[k].surface, pts[k - 1].surface);
            lines.push_back(std::move(pts));
        }
        stat.rivers = (int)lines.size();
    }

    // results in voxels
    h.resize(n); dh.resize(n); F.resize(n);
    for (int i = 0; i < n; i++) {
        h[i] = hn[i] * step;
        dh[i] = h[i] - base[i];
        F[i] = std::max(Fn[i], hn[i]) * step;
    }
}
