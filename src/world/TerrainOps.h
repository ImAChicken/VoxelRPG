#pragma once
#include "WorldEditor.h"
#include "../chunk/ChunkManager.h"
#include "../voxel/VoxelRegistry.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

// ══════════════════════════════════════════════════════════
// TerrainOps — shaping the ground (flatten, level, feather)
// ══════════════════════════════════════════════════════════
// Plain functions that build an EditBatch and touch nothing
// else, so every tool can share them: the blueprint paste's
// site prep, the village generator, the dev console (flatten),
// a dev brush and, later, the player's hoe. The caller submits
// the batch with its own EditSource (Player / Dev / Generator),
// which decides undo and cost.
//
// Heights are "ground level": the first air voxel above the
// ground. Flattening to level y leaves solid ground at y-1 and
// air from y up.
namespace terrain {

struct FlattenOptions {
    int      clearAbove = 40;   // how far up to clear above the level (voxels)
    int      maxFill    = 24;   // how far down to fill holes under it
    int      feather    = 0;    // a ring this wide around the patch that slopes back to the old ground
    uint16_t surface    = 0;    // the top block (0: keep each column's own top, else grass)
    uint16_t under      = BLOCK_DIRT;   // what fills below the top
    // Voxels that must not change (structures, say). Empty: none
    std::function<bool(glm::ivec3)> keep;
};

// Is this voxel ground the level rests on? (not plants, leaves
// or water)
inline bool isGround(uint16_t id) {
    if (id == BLOCK_AIR || id == BLOCK_LEAVES || id == BLOCK_WATER) return false;
    const VoxelTypeDef& d = gRegistry.get(id);
    return d.isSolid() && !d.sprite;
}

// Ground level of a column: the first air voxel above the top
// ground voxel, looking down from 'from'. -1: none within 'depth'
inline int groundLevel(const ChunkManager& w, int x, int z, int from, int depth = 256) {
    for (int y = from; y >= std::max(0, from - depth); y--)
        if (isGround(w.typeAtVoxel(x, y, z))) return y + 1;
    return -1;
}

// One column to level y: fill below, clear above
inline void levelColumn(const ChunkManager& w, EditBatch& b, int x, int z, int y,
                        const FlattenOptions& o) {
    auto kept = [&](int py) { return o.keep && o.keep({ x, py, z }); };
    int old = groundLevel(w, x, z, y + o.clearAbove, o.clearAbove + o.maxFill + 1);
    uint16_t top = o.surface;
    if (!top) {
        top = old > 0 ? w.typeAtVoxel(x, old - 1, z) : BLOCK_GRASS;
        if (!isGround(top) || top == BLOCK_STONE) top = BLOCK_GRASS;
    }
    // clear above the level
    for (int py = y; py < y + o.clearAbove; py++) {
        if (py < 0 || py >= WORLD_HEIGHT) continue;
        uint16_t id = w.typeAtVoxel(x, py, z);
        if (id != BLOCK_AIR && id != BLOCK_WATER && !kept(py)) b.set(x, py, z, BLOCK_AIR);
    }
    // fill below, down to the old ground
    int bottom = std::max({ std::min(old, y - 1), y - o.maxFill, 0 });
    for (int py = y - 1; py >= bottom; py--) {
        if (kept(py)) continue;
        uint16_t want = py == y - 1 ? top : o.under;
        uint16_t id = w.typeAtVoxel(x, py, z);
        if (id != want && (py == y - 1 || !isGround(id))) b.set(x, py, z, want);
    }
}

// Level the box [lo, hi] (x and z, inclusive) to ground level y
inline EditBatch flatten(const ChunkManager& w, glm::ivec2 lo, glm::ivec2 hi, int y,
                         const FlattenOptions& o, EditSource src,
                         const std::string& label = "flatten") {
    EditBatch b;
    b.source = src;
    b.label  = label;
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    if (lo.x > hi.x) std::swap(lo.x, hi.x);
    if (lo.y > hi.y) std::swap(lo.y, hi.y);
    const int f = std::max(0, o.feather);
    for (int z = lo.y - f; z <= hi.y + f; z++)
    for (int x = lo.x - f; x <= hi.x + f; x++) {
        int dx = std::max({ lo.x - x, x - hi.x, 0 });
        int dz = std::max({ lo.y - z, z - hi.y, 0 });
        float d = std::sqrt(float(dx * dx + dz * dz));
        if (d == 0.0f) { levelColumn(w, b, x, z, y, o); continue; }
        if (d > f) continue;
        // the ring: blend from the level back to the old ground
        int old = groundLevel(w, x, z, y + o.clearAbove, o.clearAbove + o.maxFill + 1);
        if (old < 0) continue;
        float k = d / float(f + 1);
        int target = (int)std::lround(y + (old - y) * k);
        if (target != old) levelColumn(w, b, x, z, target, o);
    }
    return b;
}

// A round flatten brush: level a disc of radius r around
// (cx, cz) to level y (the hoe and the dev brush)
inline EditBatch flattenDisc(const ChunkManager& w, int cx, int cz, float r, int y,
                             const FlattenOptions& o, EditSource src) {
    EditBatch b;
    b.source = src;
    b.label  = "flatten";
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    int ri = (int)std::ceil(r);
    for (int z = -ri; z <= ri; z++)
    for (int x = -ri; x <= ri; x++)
        if (x * x + z * z <= r * r) levelColumn(w, b, cx + x, cz + z, y, o);
    return b;
}

// Raise (delta > 0) or lower (delta < 0) the ground in a disc,
// full height in the middle, fading to nothing at the edge (the
// hoe's raise and lower)
inline EditBatch shiftDisc(const ChunkManager& w, int cx, int cz, float r, int delta, int probeFrom,
                           const FlattenOptions& o, EditSource src,
                           int lowest = -1, int highest = WORLD_HEIGHT) {   // levels it won't go past
    EditBatch b;
    b.source = src;
    b.label  = delta > 0 ? "raise ground" : "lower ground";
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    int ri = (int)std::ceil(r);
    for (int z = -ri; z <= ri; z++)
    for (int x = -ri; x <= ri; x++) {
        float d = std::sqrt(float(x * x + z * z));
        if (d > r) continue;
        int step = (int)std::lround(delta * std::min(1.0f, 1.5f * (1.0f - d / (r + 1.0f))));
        if (step == 0) continue;
        int g = groundLevel(w, cx + x, cz + z, probeFrom, 96);
        if (g < 0) continue;
        int to = std::clamp(g + step, std::min(lowest, g), std::max(highest, g));
        if (to != g) levelColumn(w, b, cx + x, cz + z, to, o);
    }
    return b;
}

// Top a disc of ground with another block, where it lies (the
// hoe's path)
inline EditBatch paintDisc(const ChunkManager& w, int cx, int cz, float r, uint16_t top, int probeFrom,
                           const FlattenOptions& o, EditSource src) {
    EditBatch b;
    b.source = src;
    b.label  = "path";
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    int ri = (int)std::ceil(r);
    for (int z = -ri; z <= ri; z++)
    for (int x = -ri; x <= ri; x++) {
        if (x * x + z * z > r * r) continue;
        int g = groundLevel(w, cx + x, cz + z, probeFrom, 96);
        if (g <= 0) continue;
        glm::ivec3 p(cx + x, g - 1, cz + z);
        if (o.keep && o.keep(p)) continue;
        if (w.typeAtVoxel(p.x, p.y, p.z) != top) b.set(p.x, p.y, p.z, top);
        // plants on it go
        uint16_t above = w.typeAtVoxel(p.x, g, p.z);
        if (above != BLOCK_AIR && !isGround(above) && !(o.keep && o.keep({ p.x, g, p.z }))) b.set(p.x, g, p.z, BLOCK_AIR);
    }
    return b;
}

// ── Seating a building on sloping ground ──────────────────
// A building's footprint [lo, hi] (x/z) is not levelled flat:
// ground above 'top' (its floor, the foundations' top) is cut down
// to it, ground below 'bottom' (the foundations' underside) is
// filled up to it, and anything between is left alone: the
// foundations sink into it (uphill they are buried, downhill they
// show). A ring 'feather' wide blends any cut or fill back into the
// ground around.
inline EditBatch seat(const ChunkManager& w, glm::ivec2 lo, glm::ivec2 hi, int bottom, int top,
                      const FlattenOptions& o, EditSource src, const std::string& label = "seat a building") {
    EditBatch b;
    b.source = src;
    b.label  = label;
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    if (lo.x > hi.x) std::swap(lo.x, hi.x);
    if (lo.y > hi.y) std::swap(lo.y, hi.y);
    if (top < bottom) std::swap(top, bottom);
    const int f = std::max(0, o.feather);
    const int from = top + o.clearAbove, depth = o.clearAbove + o.maxFill + (top - bottom) + 1;
    const int W = hi.x - lo.x + 1, D = hi.y - lo.y + 1;
    std::vector<int> inside((size_t)W * D);
    for (int z = 0; z < D; z++) for (int x = 0; x < W; x++)
        inside[(size_t)z * W + x] = groundLevel(w, lo.x + x, lo.y + z, from, depth);
    for (int z = lo.y - f; z <= hi.y + f; z++)
    for (int x = lo.x - f; x <= hi.x + f; x++) {
        int cx = std::clamp(x, lo.x, hi.x), cz = std::clamp(z, lo.y, hi.y);
        int oldIn = inside[(size_t)(cz - lo.y) * W + (cx - lo.x)];
        int t = oldIn < 0 ? bottom : std::clamp(oldIn, bottom, top);
        if (cx == x && cz == z) {                       // inside the footprint
            if (t != oldIn) levelColumn(w, b, x, z, t, o);
            continue;
        }
        if (t == oldIn) continue;                       // nothing cut or filled at the edge here
        float d = std::sqrt(float((x - cx) * (x - cx) + (z - cz) * (z - cz)));
        if (d > f) continue;
        int old = groundLevel(w, x, z, from, depth);
        if (old < 0) continue;
        // only bring the ring toward the edge's new height, never past its own
        float k = d / float(f + 1);
        int target = (int)std::lround(t + (old - t) * k);
        if ((t > oldIn && target > old) || (t < oldIn && target < old)) levelColumn(w, b, x, z, target, o);
    }
    return b;
}

// ── Strips: roads and paths ───────────────────────────────
// A level for each step along a straight strip from a to b (along
// x or z), from the ground under it: averaged across its width,
// smoothed over 'window' steps either way, and its grade kept to
// 'maxGrade' (rise per voxel along it) so a road climbs a hill in
// an even slope instead of steps. startLevel / endLevel >= 0 pin
// the ends (a path starts at a doorstep).
inline std::vector<int> stripProfile(const ChunkManager& w, glm::ivec2 a, glm::ivec2 b, int width, int probe,
                                     int window, float maxGrade, int startLevel = -1, int endLevel = -1) {
    glm::ivec2 d = glm::sign(b - a);
    if (d == glm::ivec2(0)) d = { 1, 0 };
    glm::ivec2 n(-d.y, d.x);
    int len = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)) + 1;
    std::vector<float> g(len, -1.0f);
    for (int i = 0; i < len; i++) {
        float sum = 0; int cnt = 0;
        for (int k = -width / 2; k <= width / 2; k++) {
            glm::ivec2 c = a + d * i + n * k;
            int h = groundLevel(w, c.x, c.y, probe, 192);
            if (h >= 0) { sum += (float)h; cnt++; }
        }
        if (cnt) g[i] = sum / cnt;
    }
    // fill gaps from the neighbours
    for (int i = 1; i < len; i++) if (g[i] < 0) g[i] = g[i - 1];
    for (int i = len - 2; i >= 0; i--) if (g[i] < 0) g[i] = g[i + 1];
    std::vector<float> lv(len);
    for (int i = 0; i < len; i++) {
        float sum = 0; int cnt = 0;
        for (int k = -window; k <= window; k++) {
            int j = std::clamp(i + k, 0, len - 1);
            if (g[j] >= 0) { sum += g[j]; cnt++; }
        }
        lv[i] = cnt ? sum / cnt : -1.0f;
    }
    if (startLevel >= 0) lv[0] = (float)startLevel;
    if (endLevel >= 0) lv[len - 1] = (float)endLevel;
    // keep the grade: from each pinned end outward, then back
    for (int i = 1; i < len; i++) lv[i] = std::clamp(lv[i], lv[i - 1] - maxGrade, lv[i - 1] + maxGrade);
    if (endLevel >= 0) lv[len - 1] = (float)endLevel;
    for (int i = len - 2; i >= 0; i--) lv[i] = std::clamp(lv[i], lv[i + 1] - maxGrade, lv[i + 1] + maxGrade);
    if (startLevel >= 0) lv[0] = (float)startLevel;
    std::vector<int> out(len);
    for (int i = 0; i < len; i++) out[i] = lv[i] < 0 ? -1 : (int)std::lround(lv[i]);
    return out;
}

// Lay a strip at those levels: 'width' wide, topped with
// o.surface, and a 'feather' wide verge each side that slopes back
// to the ground (no cliff beside a road cut into a hill, no bank
// beside one built up over a dip)
inline EditBatch layStrip(const ChunkManager& w, glm::ivec2 a, glm::ivec2 b, int width, const std::vector<int>& level,
                          int feather, const FlattenOptions& o, EditSource src, const std::string& label = "road") {
    EditBatch out;
    out.source = src;
    out.label  = label;
    out.undoable = src == EditSource::Dev || src == EditSource::Command;
    glm::ivec2 d = glm::sign(b - a);
    if (d == glm::ivec2(0)) d = { 1, 0 };
    glm::ivec2 n(-d.y, d.x);
    const int half = width / 2;
    FlattenOptions verge = o;
    verge.surface = 0;                                  // the verge keeps its own top (grass)
    for (int i = 0; i < (int)level.size(); i++) {
        if (level[i] < 0) continue;
        for (int k = -half - feather; k <= half + feather; k++) {
            glm::ivec2 c = a + d * i + n * k;
            int off = std::abs(k) - half;
            if (off <= 0) { levelColumn(w, out, c.x, c.y, level[i], o); continue; }
            int old = groundLevel(w, c.x, c.y, level[i] + o.clearAbove, o.clearAbove + o.maxFill + 1);
            if (old < 0) continue;
            int target = (int)std::lround(level[i] + (old - level[i]) * (off / float(feather + 1)));
            if (target != old) levelColumn(w, out, c.x, c.y, target, verge);
        }
    }
    return out;
}

} // namespace terrain
