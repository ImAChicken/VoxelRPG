#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "../chunk/Chunk.h"
#include "../voxel/VoxelRegistry.h"

// ══════════════════════════════════════════════════════════
// LightBake — the baked light layer
// ══════════════════════════════════════════════════════════
// Light is kept apart from the voxels, on a grid of 2 × 2 × 2
// voxel cells (8 inches). Each cell holds four 0–15 values:
//
//   sky       how much open sky reaches it. Stored as an
//             amount, not a colour: the shader multiplies it by
//             the sun or moon of the moment, so day and night
//             never need a rebake
//   r, g, b   light from glowing blocks (blocks.toml "light"),
//             spread one level fainter per cell, each colour on
//             its own, so red and blue lamps mix to violet
//
// A column is baked with a 16-cell (32-voxel) margin taken
// from its eight neighbours, which is as far as a 15 light can
// reach. So each column's light stands on its own and can be
// baked on any worker thread in any order.
//
// Cells: a cell blocks light if most of its voxels do (5 of 8);
// otherwise it passes light, dimmed by the dimmest voxel in it
// (leaves, water). It glows with the brightest voxel in it.
//
// Packed per cell as RGBA4 for the GPU: r<<12 | g<<8 | b<<4 | sky.
namespace light {

constexpr int CELL    = 2;                       // voxels per cell side
constexpr int SC      = VOXEL_DIM / CELL;        // 64 cells across a section
constexpr int MARGIN  = 16;                      // cells taken from neighbours
constexpr int RX      = SC + 2 * MARGIN;         // 96
constexpr int RY      = WORLD_HEIGHT / CELL;     // 512
constexpr int TEX     = SC + 2;                  // 66: a section's cells plus one either side

inline uint16_t pack(int sky, int r, int g, int b) {
    return (uint16_t)((r << 12) | (g << 8) | (b << 4) | sky);
}
inline int skyOf(uint16_t v) { return v & 15; }
inline int rOf(uint16_t v)   { return (v >> 12) & 15; }
inline int gOf(uint16_t v)   { return (v >> 8) & 15; }
inline int bOf(uint16_t v)   { return (v >> 4) & 15; }

// One section's light, ready for a 3D texture (TEX³ cells,
// x fastest, then y, then z — GL's order). A section that is the
// same everywhere (open sky, solid rock) keeps one value.
struct SectionLight {
    bool     uniform = true;
    uint16_t value   = pack(15, 0, 0, 0);
    std::vector<uint16_t> cells;      // TEX³ when not uniform
    static size_t at(int x, int y, int z) { return ((size_t)z * TEX + y) * TEX + x; }
};

// A glowing spot, for distant lights: world position of the
// cell's centre and its colour
struct Emitter { int x, y, z; uint8_t r, g, b; };

struct ColumnLight {
    std::array<SectionLight, SECTIONS> sections;
    std::vector<Emitter> emitters;    // in this column only
    // Low-res copy for the far view: the top voxel of each 4 × 4
    // voxel square (height and block), 32 × 32
    static constexpr int SUM = VOXEL_DIM / 4;
    std::array<uint16_t, SUM * SUM> topY{};
    std::array<uint16_t, SUM * SUM> topId{};
};

// What the baker needs per block ID (built once from the registry)
struct LightTable {
    std::vector<uint8_t>  pass;       // 0 clear … 15 blocks
    std::vector<uint16_t> glow;       // packed r<<8|g<<4|b
    static std::shared_ptr<const LightTable> build(const VoxelRegistry& reg) {
        auto t = std::make_shared<LightTable>();
        t->pass.assign(std::max<size_t>(reg.size(), 1), 15);
        t->glow.assign(t->pass.size(), 0);
        for (size_t i = 0; i < reg.size(); i++) {
            const VoxelTypeDef& d = reg.get((uint16_t)i);
            t->pass[i] = i == 0 ? 0 : d.lightPass;
            t->glow[i] = (uint16_t)((d.light[0] << 8) | (d.light[1] << 4) | d.light[2]);
        }
        return t;
    }
};

// The nine columns around the one being baked: [dz+1][dx+1]
using Snap  = std::array<std::shared_ptr<const RleData>, SECTIONS>;
using Snap9 = std::array<std::array<const Snap*, 3>, 3>;

class Baker {
public:
    // cx, cz: the column (for emitter world positions)
    static void bake(const Snap9& cols, const LightTable& lt, int cx, int cz, ColumnLight& out) {
        thread_local std::vector<uint8_t>  opq, passv;
        thread_local std::vector<uint16_t> glowv, L;
        const size_t N = (size_t)RX * RX * RY;
        opq.assign(N, 0); passv.assign(N, 0); glowv.assign(N, 0);
        auto ci = [](int x, int y, int z) { return ((size_t)y * RX + x) * RX + z; };
        int topCell = 0;
        out.topY.fill(0); out.topId.fill(0); out.emitters.clear();

        // ── voxels → cells ────────────────────────────────
        const int D = VOXEL_DIM;
        for (int oz = -1; oz <= 1; oz++)
        for (int ox = -1; ox <= 1; ox++) {
            const Snap* col = cols[oz + 1][ox + 1];
            if (!col) continue;                                  // edge of the world: open air
            // this column's voxels inside the region, in its own coordinates
            int xa = std::max(0, -MARGIN * CELL - ox * D), xb = std::min(D, D + MARGIN * CELL - ox * D);
            int za = std::max(0, -MARGIN * CELL - oz * D), zb = std::min(D, D + MARGIN * CELL - oz * D);
            if (xa >= xb || za >= zb) continue;
            const bool center = ox == 0 && oz == 0;
            for (int sy = 0; sy < SECTIONS; sy++) {
                const auto& rle = (*col)[sy];
                if (!rle) continue;
                const int y0 = sy * D;
                auto addVoxel = [&](int x, int y, int z, uint16_t id) {
                    // offset first so the division never sees a negative (it rounds toward zero)
                    int rx = (ox * D + x + MARGIN * CELL) / CELL, rz = (oz * D + z + MARGIN * CELL) / CELL, ry = (y0 + y) / CELL;
                    size_t c = ci(rx, ry, rz);
                    uint8_t p = id < lt.pass.size() ? lt.pass[id] : 15;
                    if (p >= 15) opq[c]++;
                    else passv[c] = std::max(passv[c], p);
                    uint16_t g = id < lt.glow.size() ? lt.glow[id] : 0;
                    if (g) {
                        uint16_t& e = glowv[c];
                        e = (uint16_t)((std::max(e >> 8, g >> 8) << 8) | (std::max((e >> 4) & 15, (g >> 4) & 15) << 4) |
                                       std::max(e & 15, g & 15));
                    }
                    topCell = std::max(topCell, ry);
                    if (center && id != BLOCK_LEAVES && id != BLOCK_LOG_Y && id != BLOCK_LOG_X && id != BLOCK_LOG_Z) {
                        int s = (z / 4) * ColumnLight::SUM + x / 4;
                        if (y0 + y + 1 > out.topY[s]) { out.topY[s] = (uint16_t)(y0 + y + 1); out.topId[s] = id; }
                    }
                };
                if (rle->types.size() == 1) {
                    uint16_t id = rle->types[0];
                    if (id == BLOCK_AIR) continue;
                    // a whole section of one block: fill its cells directly
                    uint8_t p = id < lt.pass.size() ? lt.pass[id] : 15;
                    for (int y = 0; y < D; y += CELL)
                    for (int x = xa; x < xb; x += CELL)
                    for (int z = za; z < zb; z += CELL) {
                        size_t c = ci((ox * D + x + MARGIN * CELL) / CELL, (y0 + y) / CELL, (oz * D + z + MARGIN * CELL) / CELL);
                        if (p >= 15) opq[c] = 8; else passv[c] = std::max(passv[c], p);
                    }
                    topCell = std::max(topCell, (y0 + D) / CELL - 1);
                    if (center)
                        for (auto& t : out.topY) t = (uint16_t)std::max<int>(t, y0 + D);
                    if (center) for (size_t s = 0; s < out.topId.size(); s++) if (out.topY[s] == y0 + D) out.topId[s] = id;
                    continue;
                }
                uint32_t start = 0;
                for (size_t r = 0; r < rle->ends.size(); r++) {
                    uint32_t end = rle->ends[r];
                    uint16_t id = rle->types[r];
                    if (id != BLOCK_AIR) {
                        for (uint32_t i = start; i < end;) {
                            int z = (int)(i % D), x = (int)((i / D) % D), y = (int)(i / (D * D));
                            uint32_t rowEnd = std::min(end, i + (uint32_t)(D - z));
                            if (x >= xa && x < xb) {
                                int z1 = z + (int)(rowEnd - i);
                                for (int zz = std::max(z, za); zz < std::min(z1, zb); zz++) addVoxel(x, y, zz, id);
                            }
                            i = rowEnd;
                        }
                    }
                    start = end;
                }
            }
        }
        topCell = std::min(RY - 1, topCell + 2);

        // a cell's final pass: blocks if most voxels block
        for (size_t c = 0; c < N; c++) passv[c] = opq[c] >= 5 ? 15 : passv[c];

        // ── sky: straight down, then sideways ─────────────
        L.assign(N, 0);
        std::vector<int> q;
        q.reserve(1 << 16);
        for (int x = 0; x < RX; x++)
        for (int z = 0; z < RX; z++) {
            int s = 15;
            for (int y = RY - 1; y >= 0; y--) {
                size_t c = ci(x, y, z);
                if (y <= topCell) {
                    int p = passv[c];
                    s = p >= 15 ? 0 : std::max(0, s - p);
                }
                L[c] = (uint16_t)s;
                if (s == 0 && y < topCell) {
                    // rest of the column below stays dark (already 0)
                    break;
                }
            }
        }
        // seeds: lit cells next to a darker open cell
        static const int N6[6][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
        for (int y = 0; y <= topCell; y++)
        for (int x = 0; x < RX; x++)
        for (int z = 0; z < RX; z++) {
            size_t c = ci(x, y, z);
            int s = L[c] & 15;
            if (s < 2) continue;
            for (auto& o : N6) {
                int nx = x + o[0], ny = y + o[1], nz = z + o[2];
                if (nx < 0 || nz < 0 || nx >= RX || nz >= RX || ny < 0 || ny >= RY) continue;
                size_t n = ci(nx, ny, nz);
                if (passv[n] < 15 && (L[n] & 15) < s - 1) { q.push_back((int)c); break; }
            }
        }
        spread(q, passv, L, [](uint16_t v) { return (int)(v & 15); },
               [](uint16_t v, int s) { return (uint16_t)((v & ~15) | s); }, ci);

        // ── glowing blocks: each colour floods out ────────
        q.clear();
        for (int y = 0; y <= topCell; y++)
        for (int x = 0; x < RX; x++)
        for (int z = 0; z < RX; z++) {
            size_t c = ci(x, y, z);
            uint16_t g = glowv[c];
            if (!g) continue;
            L[c] = (uint16_t)((L[c] & 15) | ((g >> 8) << 12) | (((g >> 4) & 15) << 8) | ((g & 15) << 4));
            q.push_back((int)c);
            if (x >= MARGIN && x < MARGIN + SC && z >= MARGIN && z < MARGIN + SC)
                out.emitters.push_back({ cx * D + (x - MARGIN) * CELL + 1, y * CELL + 1, cz * D + (z - MARGIN) * CELL + 1,
                                         (uint8_t)(g >> 8), (uint8_t)((g >> 4) & 15), (uint8_t)(g & 15) });
        }
        spreadRGB(q, passv, L, ci);

        // ── out: one texture's worth per section ──────────
        for (int sy = 0; sy < SECTIONS; sy++) {
            SectionLight& sl = out.sections[(size_t)sy];
            int cy0 = sy * SC - 1;
            auto get = [&](int x, int y, int z) -> uint16_t {
                if (y >= RY) return pack(15, 0, 0, 0);
                if (y < 0) return 0;
                return L[ci(x, y, z)];
            };
            uint16_t first = get(MARGIN - 1, cy0, MARGIN - 1);
            bool same = true;
            if (cy0 > topCell + 1) same = first == pack(15, 0, 0, 0);
            else {
                for (int y = 0; y < TEX && same; y++)
                for (int x = 0; x < TEX && same; x++)
                for (int z = 0; z < TEX; z++)
                    if (get(MARGIN - 1 + x, cy0 + y, MARGIN - 1 + z) != first) { same = false; break; }
            }
            sl.uniform = same;
            sl.value = first;
            if (same) { sl.cells.clear(); sl.cells.shrink_to_fit(); continue; }
            sl.cells.resize((size_t)TEX * TEX * TEX);
            for (int z = 0; z < TEX; z++)
            for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                sl.cells[SectionLight::at(x, y, z)] = get(MARGIN - 1 + x, cy0 + y, MARGIN - 1 + z);
        }
    }

private:
    template <class Get, class Set, class CI>
    static void spread(std::vector<int>& q, const std::vector<uint8_t>& passv, std::vector<uint16_t>& L,
                       Get get, Set set, CI ci) {
        static const int N6[6][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
        for (size_t h = 0; h < q.size(); h++) {
            int c = q[h];
            int z = c % RX, x = (c / RX) % RX, y = c / (RX * RX);
            int v = get(L[(size_t)c]);
            if (v <= 1) continue;
            for (auto& o : N6) {
                int nx = x + o[0], ny = y + o[1], nz = z + o[2];
                if (nx < 0 || nz < 0 || nx >= RX || nz >= RX || ny < 0 || ny >= RY) continue;
                size_t n = ci(nx, ny, nz);
                int p = passv[n];
                if (p >= 15) continue;
                int nv = v - 1 - p;
                if (nv > get(L[n])) { L[n] = set(L[n], nv); q.push_back((int)n); }
            }
        }
    }
    template <class CI>
    static void spreadRGB(std::vector<int>& q, const std::vector<uint8_t>& passv, std::vector<uint16_t>& L, CI ci) {
        static const int N6[6][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
        for (size_t h = 0; h < q.size(); h++) {
            int c = q[h];
            int z = c % RX, x = (c / RX) % RX, y = c / (RX * RX);
            uint16_t v = L[(size_t)c];
            int r = rOf(v), g = gOf(v), b = bOf(v);
            if (r <= 1 && g <= 1 && b <= 1) continue;
            for (auto& o : N6) {
                int nx = x + o[0], ny = y + o[1], nz = z + o[2];
                if (nx < 0 || nz < 0 || nx >= RX || nz >= RX || ny < 0 || ny >= RY) continue;
                size_t n = ci(nx, ny, nz);
                int p = passv[n];
                if (p >= 15) continue;
                uint16_t w = L[n];
                int nr = std::max(rOf(w), r - 1 - p), ng = std::max(gOf(w), g - 1 - p), nb = std::max(bOf(w), b - 1 - p);
                if (nr != rOf(w) || ng != gOf(w) || nb != bOf(w)) {
                    L[n] = pack(skyOf(w), nr, ng, nb);
                    q.push_back((int)n);
                }
            }
        }
    }
};

} // namespace light
