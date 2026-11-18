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

} // namespace terrain
