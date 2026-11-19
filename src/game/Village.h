#pragma once
#include "Blueprint.h"
#include "Palettes.h"
#include "../world/StructureManager.h"
#include "../world/TerrainOps.h"
#include "../world/WorldEditor.h"
#include "../world/Chests.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Village — a village laid out from blueprints
// ══════════════════════════════════════════════════════════
// Two steps, so each can be swapped or driven by hand later:
//   plan()   decides: a square in the middle (a blueprint
//            tagged "plaza"), roads running out from it, and
//            buildings along both sides of each road, turned so
//            their fronts (doors) face the road. Reads the
//            ground, changes nothing.
//   build()  carries a plan out: levels each lot and places the
//            building (Blueprint::paste), then lays the roads
//            (TerrainOps), following the ground.
// Every building is an ordinary blueprint, so anything a player
// copies with the hammer (tag it "house", "work" …) can show up
// in villages. A style dresses the whole village alike (stone
// walls and tile roofs here, plank and thatch there).
//
// Console: village [seed] [roads] (game/Commands.h). Later the
// world generator calls the same two functions.
namespace village {

struct Options {
    uint32_t seed      = 1;
    int      roads     = 3;          // 1–4 roads out of the square
    int      roadLen   = 110;        // voxels, from the square's edge
    int      roadWidth = 9;          // 3 ft
    int      setback   = 4;          // gap between a road and a building
    int      gap       = 14;         // gap between buildings (over 4 ft: foundations further apart than that needn't line up)
    int      maxSlope  = 14;         // a lot whose ground varies more than this is skipped
    uint16_t roadBlock = BLOCK_GRAVEL;
    // Keep lots off this spot (the player starting there), within 'clearRadius'
    glm::ivec2 keepClear{ 0 };
    int        clearRadius = 0;
    BlueprintStyle style = sunk();   // applied to every building (foundations 2 ft into the ground)
                                     // and its palette: one for the whole village (pickPalette)
    static BlueprintStyle sunk() { BlueprintStyle s; s.sink = 6; return s; }
    // How often each kind of building comes up (by tag)
    std::vector<std::pair<std::string, int>> weights = {
        { "house", 6 }, { "storage", 2 }, { "work", 2 }, { "smithy", 1 } };
};

// One of the curated palettes for a village, by its seed (game/Palettes.h):
// every wall, post and door in it is made the same way
inline void pickPalette(Options& o, const std::string& name = "") {
    std::vector<const BuildPalette*> curated;
    for (const BuildPalette& p : gBuildPalettes.list()) if (p.curated) curated.push_back(&p);
    const BuildPalette* pick = name.empty() ? nullptr : gBuildPalettes.find(name);
    if (!pick && !curated.empty()) pick = curated[(o.seed * 2654435761u >> 8) % curated.size()];
    if (pick) { o.style.palette = pick->spec; o.style.paletteName = pick->name; }
}

struct Lot {
    const Blueprint* bp = nullptr;
    glm::ivec3 lo{ 0 };              // low corner (y: ground level)
    int rot = 0;
    glm::ivec3 size{ 0 };            // turned
};

// An axis-aligned strip: from a to b (one axis equal), 'width' wide
struct Road {
    glm::ivec2 a{ 0 }, b{ 0 };
    int width = 9;
    glm::ivec2 lo() const { int h = width / 2; return glm::min(a, b) - glm::ivec2(h); }
    glm::ivec2 hi() const { int h = width / 2; return glm::max(a, b) + glm::ivec2(h); }
};

struct Plan {
    glm::ivec3 center{ 0 };
    std::vector<Lot>  lots;          // lots[0] is the square, if there is one
    std::vector<Road> roads;
};

// Ground level near (x, z), looking down from 'from'
inline int groundAt(const ChunkManager& w, int x, int z, int from) {
    return terrain::groundLevel(w, x, z, from, from);
}

inline Plan plan(const ChunkManager& w, const BlueprintLibrary& lib, glm::ivec3 center, const Options& o) {
    Plan p;
    p.center = center;
    std::mt19937 rng(o.seed * 2654435761u + 17u);
    auto rnd = [&](int n) { return n > 0 ? int(rng() % (uint32_t)n) : 0; };
    const int probe = center.y + 96;

    struct Box { glm::ivec2 lo, hi; };
    std::vector<Box> taken, roadBoxes;
    auto clear = [](const std::vector<Box>& boxes, glm::ivec2 lo, glm::ivec2 hi, int margin) {
        for (const Box& b : boxes)
            if (lo.x <= b.hi.x + margin && b.lo.x <= hi.x + margin &&
                lo.y <= b.hi.y + margin && b.lo.y <= hi.y + margin) return false;
        return true;
    };
    // apart from other buildings by the gap, off every road
    auto free = [&](glm::ivec2 lo, glm::ivec2 hi, int margin) {
        if (o.clearRadius > 0 && !clear({ { o.keepClear, o.keepClear } }, lo, hi, o.clearRadius)) return false;
        return clear(taken, lo, hi, margin) && clear(roadBoxes, lo, hi, 1);
    };
    // Ground level for a lot: the middle; null if too steep
    auto lotLevel = [&](glm::ivec2 lo, glm::ivec2 hi, int& level) {
        int lv[5] = { groundAt(w, (lo.x + hi.x) / 2, (lo.y + hi.y) / 2, probe),
                      groundAt(w, lo.x, lo.y, probe), groundAt(w, hi.x, lo.y, probe),
                      groundAt(w, lo.x, hi.y, probe), groundAt(w, hi.x, hi.y, probe) };
        int mn = *std::min_element(lv, lv + 5), mx = *std::max_element(lv, lv + 5);
        if (mn < 0 || mx - mn > o.maxSlope) return false;
        level = lv[0];
        return true;
    };

    // The square
    std::vector<const Blueprint*> plazas = lib.withTag("plaza");
    int half = 15;
    if (!plazas.empty()) {
        const Blueprint* sq = plazas[rnd((int)plazas.size())];
        Lot l;
        l.bp = sq; l.rot = 0; l.size = sq->sizeTurned(0);
        glm::ivec2 lo(center.x - l.size.x / 2, center.z - l.size.z / 2);
        int level = groundAt(w, center.x, center.z, probe);
        l.lo = glm::ivec3(lo.x, (level < 0 ? center.y : level) - o.style.sink, lo.y);
        p.lots.push_back(l);
        taken.push_back({ lo, lo + glm::ivec2(l.size.x, l.size.z) - 1 });
        half = std::max(l.size.x, l.size.z) / 2;
    }

    // Buildings to choose from, by weight
    std::vector<std::pair<const Blueprint*, int>> pool;
    for (const Blueprint& bp : lib.list()) {
        if (bp.hasTag("plaza")) continue;
        int wgt = 0;
        for (auto& [tag, n] : o.weights) if (bp.hasTag(tag)) wgt = std::max(wgt, n);
        if (wgt > 0) pool.push_back({ &bp, wgt });
    }
    auto pick = [&]() -> const Blueprint* {
        int total = 0;
        for (auto& e : pool) total += e.second;
        int r = rnd(total);
        for (auto& e : pool) { if (r < e.second) return e.first; r -= e.second; }
        return nullptr;
    };

    // Roads out of the square: north, east, south, west (shuffled)
    const glm::ivec2 dirs[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    int order[4] = { 0, 1, 2, 3 };
    std::shuffle(order, order + 4, rng);
    int nRoads = std::clamp(o.roads, 1, 4);
    for (int k = 0; k < nRoads; k++) {
        glm::ivec2 d = dirs[order[k]];
        glm::ivec2 c(center.x, center.z);
        Road r;
        r.width = o.roadWidth;
        r.a = c + d * (half + 1);
        r.b = c + d * (half + 1 + o.roadLen);
        p.roads.push_back(r);
    }
    for (const Road& r : p.roads) roadBoxes.push_back({ r.lo(), r.hi() });

    // Buildings along each road, both sides, fronts to the road
    if (!pool.empty())
        for (const Road& r : p.roads) {
            glm::ivec2 d = glm::sign(r.b - r.a);                // along the road
            for (int side = -1; side <= 1; side += 2) {
                glm::ivec2 n(-d.y * side, d.x * side);         // out from the road, to this side
                // the way a building here faces: back toward the road
                int faceDir = n.y < 0 ? 2 : n.x > 0 ? 3 : n.y > 0 ? 0 : 1;
                int along = 6;
                int tries = 0;
                while (along < o.roadLen - 8 && tries++ < 40) {
                    const Blueprint* bp = pick();
                    if (!bp) break;
                    int rot = (faceDir - bp->front) & 3;
                    glm::ivec3 sz = bp->sizeTurned(rot);
                    int alongSize = d.x != 0 ? sz.x : sz.z;
                    int depth     = d.x != 0 ? sz.z : sz.x;
                    if (along + alongSize > o.roadLen) { along += 6; continue; }   // runs past the road's end
                    // the lot: 'along' to 'along + alongSize' down the road, and
                    // from the road's edge (plus the setback) outward 'depth'
                    int off = r.width / 2 + 1 + o.setback;
                    glm::ivec2 c0 = r.a + d * along + n * off;
                    glm::ivec2 c1 = r.a + d * (along + alongSize - 1) + n * (off + depth - 1);
                    glm::ivec2 lo = glm::min(c0, c1), hi = glm::max(c0, c1);
                    int level;
                    if (free(lo, hi, o.gap) && lotLevel(lo, hi, level)) {
                        Lot l;
                        l.bp = bp; l.rot = rot; l.size = sz;
                        l.lo = glm::ivec3(lo.x, level - o.style.sink, lo.y);
                        p.lots.push_back(l);
                        taken.push_back({ lo, hi });
                        along += alongSize + o.gap + rnd(6);
                    } else along += 6;
                }
            }
        }
    return p;
}

// Lay one road: each cross-section levelled to the smoothed
// ground along it, topped with the road block
inline EditBatch layRoad(const ChunkManager& w, const StructureManager& sm, const Road& r, const Options& o,
                         int probe, EditSource src) {
    EditBatch b;
    b.source = src;
    b.label  = "road";
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    glm::ivec2 d = glm::sign(r.b - r.a);
    int len = std::max(std::abs(r.b.x - r.a.x), std::abs(r.b.y - r.a.y)) + 1;
    std::vector<int> ground(len);
    for (int i = 0; i < len; i++) {
        glm::ivec2 c = r.a + d * i;
        ground[i] = groundAt(w, c.x, c.y, probe);
    }
    // smooth it (a moving average) so the road doesn't step
    std::vector<int> level(len);
    for (int i = 0; i < len; i++) {
        int sum = 0, n = 0;
        for (int k = -6; k <= 6; k++) {
            int j = std::clamp(i + k, 0, len - 1);
            if (ground[j] >= 0) { sum += ground[j]; n++; }
        }
        level[i] = n ? (sum + n / 2) / n : -1;
    }
    terrain::FlattenOptions fo;
    fo.surface    = o.roadBlock;
    fo.clearAbove = 12;
    fo.maxFill    = 8;
    fo.keep = [&sm](glm::ivec3 p) { const StructureInstance* s = sm.instanceAt(p); return s && !s->natural; };
    glm::ivec2 n(-d.y, d.x);
    for (int i = 0; i < len; i++) {
        if (level[i] < 0) continue;
        for (int k = -r.width / 2; k <= r.width / 2; k++) {
            glm::ivec2 c = r.a + d * i + n * k;
            terrain::levelColumn(w, b, c.x, c.y, level[i], fo);
        }
    }
    return b;
}

struct Built { int buildings = 0, pieces = 0, failed = 0, roads = 0; std::string why; };

// Carry out a plan. Buildings first (each levels its own lot),
// then the roads, which keep clear of what was built.
inline Built build(const Plan& p, StructureManager& sm, WorldEditor& editor, ChestStore* chests,
                   const Options& o, EditSource src) {
    Built out;
    for (const Lot& l : p.lots) {
        auto placements = l.bp->plan(l.lo, l.rot, &o.style);
        std::string bad = Blueprint::check(sm, placements, true);
        if (!bad.empty()) { out.failed++; if (out.why.empty()) out.why = l.bp->name + ": " + bad; continue; }
        std::string why;
        auto made = l.bp->paste(sm, editor, chests, placements, src, &o.style, &why);
        out.pieces += (int)made.size();
        if (!made.empty()) out.buildings++;
        if (!why.empty()) { out.failed++; if (out.why.empty()) out.why = l.bp->name + ": " + why; }
    }
    for (const Road& r : p.roads) {
        editor.submit(layRoad(editor.worldRef(), sm, r, o, p.center.y + 96, src));
        out.roads++;
    }
    editor.flush();
    return out;
}

} // namespace village
