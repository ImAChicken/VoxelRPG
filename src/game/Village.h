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
//            tagged "plaza"), roads running out from it, lanes
//            branching off them, a church facing the square (a
//            blueprint tagged "church"), and buildings along both
//            sides of every road and lane, turned so their fronts
//            (doors) face it; a well by the square, lantern posts
//            along the roads, a path from each door to its road.
//            Reads the ground, changes nothing.
//   build()  carries a plan out: levels each lot and places the
//            building (Blueprint::paste), then lays the roads and
//            paths (TerrainOps) following the ground, then sets
//            the well and lanterns on it.
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
    int      maxSlope  = 30;         // a lot whose ground varies more than this is skipped (the
                                     // foundations take up to their own height; more is cut and filled)
    float    roadGrade = 0.34f;      // the steepest a road climbs (rise per voxel: 1 in 3)
    int      roadSmooth = 12;        // voxels either way its level is averaged over
    int      verge     = 4;          // each side of a road or path slopes back to the ground this wide
    uint16_t roadBlock = BLOCK_GRAVEL;
    // Keep lots off this spot (the player starting there), within 'clearRadius'
    glm::ivec2 keepClear{ 0 };
    int        clearRadius = 0;
    BlueprintStyle style = sunk();   // applied to every building (foundations 2 ft into the ground)
                                     // and its palette: one for the whole village (pickPalette)
    static BlueprintStyle sunk() { BlueprintStyle s; s.sink = 6; return s; }
    // How often each kind of building comes up (by tag)
    std::vector<std::pair<std::string, int>> weights = {
        { "house", 6 }, { "storage", 2 }, { "work", 2 }, { "smithy", 1 }, { "hall", 1 } };
    int  lanes     = 2;              // side lanes off the roads (0: none)
    int  laneLen   = 70;             // voxels
    bool church    = true;           // one church facing the square, if a blueprint is tagged "church"
    bool well      = true;           // a well beside the square
    int  lightEvery = 42;            // a lantern post this far apart along the roads (0: none)
    bool paths     = true;           // a path from each front door to its road
    int  pathWidth = 3;
};

// One of the curated palettes for a village, by its seed (game/Palettes.h):
// every wall, post and door in it is made the same way
inline void pickPalette(Options& o, const std::string& name = "") {
    std::vector<const BuildPalette*> curated;
    for (const BuildPalette& p : gBuildPalettes.list()) if (p.curated && p.tags.empty()) curated.push_back(&p);
    const BuildPalette* pick = name.empty() ? nullptr : gBuildPalettes.find(name);
    if (!pick && !curated.empty()) pick = curated[(o.seed * 2654435761u >> 8) % curated.size()];
    if (pick) { o.style.palette = pick->spec; o.style.paletteName = pick->name; }
}

struct Lot {
    const Blueprint* bp = nullptr;
    glm::ivec3 lo{ 0 };              // low corner (y: the foundations' underside)
    int rot = 0;
    glm::ivec3 size{ 0 };            // turned
    int floor = 0;                   // the floor: the foundations' top (a path starts here)
};

// A single piece set on the ground (a well, a lantern post): low
// corner x/z, turn; its height is found when it's built (after the
// roads level the ground)
struct Decor {
    std::string type;
    glm::ivec2  lo{ 0 };
    int rot = 0;
};

// An axis-aligned strip: from a to b (one axis equal), 'width' wide
struct Road {
    glm::ivec2 a{ 0 }, b{ 0 };
    int width = 9;
    int startLevel = -1;             // >= 0: its first step at this level (a road off the square, a path at a door)
    glm::ivec2 lo() const { int h = width / 2; return glm::min(a, b) - glm::ivec2(h); }
    glm::ivec2 hi() const { int h = width / 2; return glm::max(a, b) + glm::ivec2(h); }
};

struct Plan {
    glm::ivec3 center{ 0 };
    std::vector<Lot>  lots;          // lots[0] is the square, if there is one
    std::vector<Road> roads;         // the roads first, then the lanes off them
    std::vector<Road> paths;         // from front doors to the roads (laid like roads, narrow)
    std::vector<int>  pathOf;        // per lot: its path (index into paths), -1 none
    std::vector<int>  pathRoad;      // per path: the road it meets
    std::vector<Decor> decor;
    int mainRoads = 0;               // roads[0 .. mainRoads) come out of the square
};

// Where a piece's +x side points after q quarter turns (the way the
// shapes are turned: VoxelVolume::rotatedY)
inline glm::ivec2 plusXTurned(int q) {
    VoxelVolume m({ 2, 1, 1 });
    m.set(1, 0, 0, 1);
    VoxelVolume r = m.rotatedY(q);
    for (int z = 0; z < r.size.z; z++) for (int x = 0; x < r.size.x; x++)
        if (r.at(x, 0, z) == 1) return r.size.x > 1 ? glm::ivec2(x == 0 ? -1 : 1, 0) : glm::ivec2(0, z == 0 ? -1 : 1);
    return { 1, 0 };
}

// The palette for one kind of building: one tagged with any of its
// tags ("church": Chapel Stone), or "" for the village's own
inline std::string paletteFor(const Blueprint& bp) {
    for (const BuildPalette& p : gBuildPalettes.list())
        for (const std::string& t : p.tags)
            if (bp.hasTag(t)) return p.name;
    return "";
}

// Where a lot's front door is (the middle of its doorway on the
// ground, x/z), from its placed pieces; false if it has none
inline bool doorOf(const Lot& l, glm::ivec2& out) {
    for (const BlueprintPlacement& p : l.bp->plan(l.lo, l.rot)) {
        const StructureType* t = gStructures.get(p.type);
        if (t && t->kind == StructureKind::Door && p.lo.y <= l.lo.y + 16) {
            out = glm::ivec2((p.lo.x + p.hi.x) / 2, (p.lo.z + p.hi.z) / 2);
            return true;
        }
    }
    return false;
}

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
    // Where a lot's foundations go (their underside, 'base'), from the
    // ground over its footprint. Not one flat level: the floor goes
    // where the foundations can take the slope, buried uphill and
    // showing downhill (Blueprint::sitePrep seats it):
    //   flat ground   the foundations 'sink' into the lowest ground
    //   a gentle slope, up to their height: the floor at the highest
    //                 ground, so nothing is cut and the low side shows
    //   steeper       halfway: the top cut down, the bottom built up
    // False (skipped) if over the lot's ground varies more than maxSlope
    auto lotLevel = [&](const Blueprint* bp, glm::ivec2 lo, glm::ivec2 hi, int& base) {
        int mn = 1 << 30, mx = -1;
        for (int z = lo.y; z <= hi.y + 3; z += 4)
            for (int x = lo.x; x <= hi.x + 3; x += 4) {
                int g = groundAt(w, std::min(x, hi.x), std::min(z, hi.y), probe);
                if (g < 0) return false;                   // not loaded (or no ground): not here
                mn = std::min(mn, g); mx = std::max(mx, g);
            }
        if (mx - mn > o.maxSlope) return false;
        int H = bp->foundationHeight();
        if (H <= 0) { base = mn - o.style.sink; return true; }
        int sink = std::clamp(o.style.sink, 0, H);
        int floor = mn + H - sink;                          // flat: 'sink' buried
        if (mx > floor) floor = mx - mn <= H ? mx : (mn + H + mx + 1) / 2;
        base = floor - H;
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
        int base;
        if (!lotLevel(sq, lo, lo + glm::ivec2(l.size.x, l.size.z) - 1, base)) {
            int level = groundAt(w, center.x, center.z, probe);
            base = (level < 0 ? center.y : level) - o.style.sink;
        }
        l.lo = glm::ivec3(lo.x, base, lo.y);
        l.floor = base + std::max(0, sq->foundationHeight());
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
        if (!p.lots.empty()) r.startLevel = p.lots[0].floor;     // level with the square
        p.roads.push_back(r);
    }
    p.mainRoads = (int)p.roads.size();
    // Lanes: off a road partway along, out to one side or both
    for (int k = 0; k < o.lanes && p.mainRoads > 0; k++) {
        const Road& r = p.roads[(size_t)(k % p.mainRoads)];
        glm::ivec2 d = glm::sign(r.b - r.a);
        int len = std::max(std::abs(r.b.x - r.a.x), std::abs(r.b.y - r.a.y));
        int at = len * (45 + rnd(30)) / 100;
        int side = (k / std::max(1, p.mainRoads)) % 2 == 0 ? (rnd(2) ? 1 : -1) : -1;
        glm::ivec2 n(-d.y * side, d.x * side);
        Road l;
        l.width = std::max(5, o.roadWidth - 2);
        l.a = r.a + d * at + n * (r.width / 2 + 1);
        l.b = l.a + n * o.laneLen;
        // not across another road or lane
        bool clearOfRoads = true;
        for (const Road& e : p.roads) {
            if (&e == &r) continue;
            glm::ivec2 lo = glm::min(l.a, l.b) - glm::ivec2(l.width / 2), hi = glm::max(l.a, l.b) + glm::ivec2(l.width / 2);
            if (lo.x <= e.hi().x + 8 && e.lo().x <= hi.x + 8 && lo.y <= e.hi().y + 8 && e.lo().y <= hi.y + 8) clearOfRoads = false;
        }
        if (clearOfRoads) p.roads.push_back(l);
    }
    for (const Road& r : p.roads) roadBoxes.push_back({ r.lo(), r.hi() });

    // The church: across the square from... a side no road leaves by,
    // its door to the square; else the first lot on a road
    const Blueprint* churchBp = nullptr;
    if (o.church) {
        std::vector<const Blueprint*> ch = lib.withTag("church");
        if (!ch.empty()) churchBp = ch[rnd((int)ch.size())];
    }
    if (churchBp && nRoads < 4) {
        int side = order[nRoads];
        glm::ivec2 d = dirs[side];
        int faceDir = (side + 2) & 3;                       // back toward the square
        int rot = (faceDir - churchBp->front) & 3;
        glm::ivec3 sz = churchBp->sizeTurned(rot);
        int depth = d.x != 0 ? sz.x : sz.z;
        glm::ivec2 mid = glm::ivec2(center.x, center.z) + d * (half + 1 + o.roadWidth + o.setback + depth / 2);
        glm::ivec2 lo = mid - glm::ivec2(sz.x / 2, sz.z / 2), hi = lo + glm::ivec2(sz.x, sz.z) - 1;
        int base;
        if (free(lo, hi, o.gap) && lotLevel(churchBp, lo, hi, base)) {
            Lot l;
            l.bp = churchBp; l.rot = rot; l.size = sz;
            l.lo = glm::ivec3(lo.x, base, lo.y);
            l.floor = base + churchBp->foundationHeight();
            p.lots.push_back(l);
            taken.push_back({ lo, hi });
            // a short way from the square to its door
            Road w;
            w.width = o.roadWidth;
            w.a = glm::ivec2(center.x, center.z) + d * (half + 1);
            w.b = glm::ivec2(center.x, center.z) + d * (half + o.setback + o.roadWidth);
            if (p.lots.size() > 1) w.startLevel = p.lots[0].floor;
            p.roads.push_back(w);
            roadBoxes.push_back({ w.lo(), w.hi() });
            churchBp = nullptr;
        }
    }

    // Buildings along each road and lane, both sides, fronts to it
    if (!pool.empty())
        for (const Road& r : p.roads) {
            glm::ivec2 d = glm::sign(r.b - r.a);                // along the road
            int len = std::max(std::abs(r.b.x - r.a.x), std::abs(r.b.y - r.a.y));
            if (len < 30) continue;                             // the church's short way
            for (int side = -1; side <= 1; side += 2) {
                glm::ivec2 n(-d.y * side, d.x * side);         // out from the road, to this side
                // the way a building here faces: back toward the road
                int faceDir = n.y < 0 ? 2 : n.x > 0 ? 3 : n.y > 0 ? 0 : 1;
                int along = 6;
                int tries = 0;
                while (along < len - 8 && tries++ < 40) {
                    const Blueprint* bp = churchBp ? churchBp : pick();
                    if (!bp) break;
                    int rot = (faceDir - bp->front) & 3;
                    glm::ivec3 sz = bp->sizeTurned(rot);
                    int alongSize = d.x != 0 ? sz.x : sz.z;
                    int depth     = d.x != 0 ? sz.z : sz.x;
                    if (along + alongSize > len) { along += 6; continue; }   // runs past the road's end
                    // the lot: 'along' to 'along + alongSize' down the road, and
                    // from the road's edge (plus the setback) outward 'depth'
                    int off = r.width / 2 + 1 + o.setback;
                    glm::ivec2 c0 = r.a + d * along + n * off;
                    glm::ivec2 c1 = r.a + d * (along + alongSize - 1) + n * (off + depth - 1);
                    glm::ivec2 lo = glm::min(c0, c1), hi = glm::max(c0, c1);
                    int base;
                    if (free(lo, hi, o.gap) && lotLevel(bp, lo, hi, base)) {
                        Lot l;
                        l.bp = bp; l.rot = rot; l.size = sz;
                        l.lo = glm::ivec3(lo.x, base, lo.y);
                        l.floor = base + bp->foundationHeight();
                        p.lots.push_back(l);
                        taken.push_back({ lo, hi });
                        along += alongSize + o.gap + rnd(6);
                        if (bp == churchBp) churchBp = nullptr;
                    } else along += 6;
                }
            }
        }

    // Paths: from each front door straight out to the nearest road
    p.pathOf.assign(p.lots.size(), -1);
    if (o.paths)
        for (size_t i = 0; i < p.lots.size(); i++) {
            const Lot& l = p.lots[i];
            glm::ivec2 door;
            if (l.bp->hasTag("plaza") || !doorOf(l, door)) continue;
            glm::ivec2 f = dirs[l.bp->frontTurned(l.rot)];
            glm::ivec2 a = door;                                // out of the lot first
            for (int k = 0; k < 40 && a.x >= l.lo.x && a.x < l.lo.x + l.size.x && a.y >= l.lo.z && a.y < l.lo.z + l.size.z; k++)
                a += f;
            glm::ivec2 b = a;
            bool reached = false;
            int meets = -1;
            for (int k = 0; k < 48 && !reached; k++) {
                b = a + f * k;
                for (size_t ri = 0; ri < roadBoxes.size(); ri++) {
                    const Box& rb = roadBoxes[ri];
                    if (b.x >= rb.lo.x && b.x <= rb.hi.x && b.y >= rb.lo.y && b.y <= rb.hi.y) { reached = true; meets = (int)ri; break; }
                }
            }
            if (!reached || b == a) continue;
            Road path;
            path.width = o.pathWidth;
            path.a = a; path.b = b;
            path.startLevel = l.floor;                          // at the doorstep
            p.pathOf[i] = (int)p.paths.size();
            p.pathRoad.push_back(meets);                        // roadBoxes are in the order of p.roads
            p.paths.push_back(path);
        }

    // A well beside the square, on a corner no road leaves by
    if (o.well && gStructures.find("well")) {
        const glm::ivec2 corners[4] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
        int start = rnd(4);
        for (int k = 0; k < 4; k++) {
            glm::ivec2 cd = corners[(start + k) % 4];
            glm::ivec2 mid = glm::ivec2(center.x, center.z) + cd * (half + 10);
            glm::ivec2 lo = mid - glm::ivec2(6), hi = lo + glm::ivec2(11);
            if (!free(lo, hi, 2)) continue;
            p.decor.push_back({ "well", lo, rnd(4) });
            taken.push_back({ lo, hi });
            break;
        }
    }
    // Lantern posts along the roads (not the lanes' far ends), every so
    // often, either side by turns, at the road's edge
    if (o.lightEvery > 0 && gStructures.find("lantern_post"))
        for (const Road& r : p.roads) {
            glm::ivec2 d = glm::sign(r.b - r.a);
            int len = std::max(std::abs(r.b.x - r.a.x), std::abs(r.b.y - r.a.y));
            if (len < 30) continue;
            int side = 1;
            for (int at = 24; at < len; at += o.lightEvery, side = -side) {
                glm::ivec2 n(-d.y * side, d.x * side);
                glm::ivec2 c = r.a + d * at + n * (r.width / 2 + 2);
                glm::ivec2 lo = c - glm::ivec2(1), hi = c + glm::ivec2(2);
                if (!clear(taken, lo, hi, 1)) continue;
                // its lamp (unturned: out along +x) over the road
                int rot = 0;
                for (int q = 0; q < 4; q++) if (plusXTurned(q) == -n) rot = q;
                p.decor.push_back({ "lantern_post", lo, rot });
                taken.push_back({ lo, hi });
            }
        }
    return p;
}

// Lay one road or path (TerrainOps strips): its level along it
// averaged from the ground and kept to an even grade, its start
// pinned where it leaves the square or a doorstep, its end on the
// ground it meets (a path: the road), verges sloping back to the
// ground either side
inline EditBatch layRoad(const ChunkManager& w, const StructureManager& sm, const Road& r, const Options& o,
                         int probe, EditSource src, bool path = false) {
    terrain::FlattenOptions fo;
    fo.surface    = o.roadBlock;
    fo.clearAbove = 16;
    fo.maxFill    = 12;
    fo.keep = [&sm](glm::ivec3 p) { const StructureInstance* s = sm.instanceAt(p); return s && !s->natural; };
    int end = path ? groundAt(w, r.b.x, r.b.y, probe) : -1;
    std::vector<int> lv = terrain::stripProfile(w, r.a, r.b, r.width, probe, path ? 1 : o.roadSmooth,
                                                path ? 1.0f : o.roadGrade, r.startLevel, end);
    EditBatch b = terrain::layStrip(w, r.a, r.b, r.width, lv, path ? std::max(1, o.verge / 2) : o.verge, fo, src,
                                    path ? "path" : "road");
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    return b;
}

// Which way a door opens out, once built: not always the lot's
// front (a smithy's door opens onto its open-sided forge). Inside is
// the side walled in all round: from a step in on each side, look
// each way for a wall. c: the door's middle; reach: how far out to
// look from; y: body height; prefer: the front, on a tie
inline glm::ivec2 doorFacing(const ChunkManager& w, glm::ivec2 c, int reach, int y, glm::ivec2 prefer) {
    static const glm::ivec2 dirs[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    auto solid = [&](int x, int yy, int z) {
        uint16_t id = w.typeAtVoxel(x, yy, z);
        return id != BLOCK_AIR && id != BLOCK_WATER && gRegistry.get(id).isSolid();
    };
    int bestScore = -1;
    glm::ivec2 bestIn = -prefer;
    for (const glm::ivec2& d : dirs) {
        glm::ivec2 p = c + d * reach;
        if (solid(p.x, y, p.y)) continue;
        int score = 0;
        for (const glm::ivec2& r : dirs) {
            if (r == -d) { score++; continue; }             // the door's own wall, behind
            for (int k = 1; k <= 40; k++)
                if (solid(p.x + r.x * k, y, p.y + r.y * k)) { score++; break; }
        }
        if (score > bestScore || (score == bestScore && d == -prefer)) { bestScore = score; bestIn = d; }
    }
    return -bestIn;
}

// A doorstep: the floor sits above the ground (a foundation sunk only
// part way), so a ramp of the path's stuff runs down from the door
// sill to the ground — or, where the lot's path leaves from this
// side, level out to where the path starts
inline void layDoorstep(const Plan& p, size_t li, const std::vector<uint32_t>& made, StructureManager& sm,
                        WorldEditor& editor, const Options& o, EditSource src) {
    const Lot& l = p.lots[li];
    const ChunkManager& w = editor.worldRef();
    const StructureInstance* door = nullptr;
    for (uint32_t id : made) {
        const StructureInstance* s = sm.get(id);
        const StructureType* t = s ? sm.typeOf(*s) : nullptr;
        if (t && t->kind == StructureKind::Door && (!door || s->origin.y < door->origin.y)) door = s;
    }
    if (!door) return;
    static const glm::ivec2 dirs[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    glm::ivec2 front = dirs[l.bp->frontTurned(l.rot)];
    glm::ivec3 lo, hi;
    sm.bounds(*door, lo, hi);
    glm::ivec2 c((lo.x + hi.x + 1) / 2, (lo.z + hi.z + 1) / 2);
    int reach = std::max(hi.x - lo.x, hi.z - lo.z) + 4;
    int floor = lo.y;
    glm::ivec2 f = doorFacing(w, c, reach, floor + 10, front);
    auto solid = [&](int x, int y, int z) {
        uint16_t id = w.typeAtVoxel(x, y, z);
        return id != BLOCK_AIR && id != BLOCK_WATER && gRegistry.get(id).isSolid();
    };
    // the shut door's leaf, in the wall: then out across the
    // foundation's lip to the first step off it
    glm::ivec2 a = c;
    {
        int best = INT32_MIN;
        for (int z = lo.z; z <= hi.z; z++)
            for (int x = lo.x; x <= hi.x; x++)
                if (sm.instanceAt({ x, floor + 2, z }) == door) {
                    int along = x * f.x + z * f.y;
                    if (along > best) { best = along; a = f.x ? glm::ivec2(x, c.y) : glm::ivec2(c.x, z); }
                }
        a += f;
        for (int k = 0; k < reach * 2 && solid(a.x, floor - 1, a.y); k++) a += f;
    }
    int ground = terrain::groundLevel(w, a.x, a.y, floor + 8, 64);
    if (ground < 0 || floor - ground <= 1) return;     // near enough level already
    int width = std::abs(f.x) ? (hi.z - lo.z + 1) : (hi.x - lo.x + 1);
    width = std::max(5, std::min(width, 9)) | 1;
    // where the path leaves (when it leaves from this side)
    int flatTo = 0;
    if (li < p.pathOf.size() && p.pathOf[li] >= 0 && f == front) {
        glm::ivec2 pa = p.paths[(size_t)p.pathOf[li]].a;
        flatTo = std::max(0, glm::dot(glm::vec2(pa - a), glm::vec2(f)) > 0 ? (int)glm::dot(glm::vec2(pa - a), glm::vec2(f)) : 0);
    }
    int len = flatTo > 0 ? flatTo + 1 : (floor - ground) + 2;
    std::vector<int> lv((size_t)len);
    for (int i = 0; i < len; i++) lv[(size_t)i] = flatTo > 0 ? floor : std::max(ground, floor - i);
    terrain::FlattenOptions fo;
    fo.surface    = o.roadBlock;
    fo.clearAbove = 0;
    fo.maxFill    = 12;
    fo.keep = [&sm](glm::ivec3 q) { const StructureInstance* s = sm.instanceAt(q); return s && !s->natural; };
    EditBatch b = terrain::layStrip(w, a, a + f * (len - 1), width, lv, 1, fo, src, "doorstep");
    b.undoable = src == EditSource::Dev || src == EditSource::Command;
    editor.submit(std::move(b));
    editor.flush();
}

struct Built {
    int buildings = 0, pieces = 0, failed = 0, roads = 0, paths = 0, decor = 0, decorSkipped = 0;
    std::string why;
    std::vector<std::vector<uint32_t>> made;        // per lot (as Plan::lots): the pieces it put down
};

// Carry out a plan. Buildings first (each levels its own lot),
// then the roads, which keep clear of what was built.
inline Built build(const Plan& p, StructureManager& sm, WorldEditor& editor, ChestStore* chests,
                   const Options& o, EditSource src) {
    Built out;
    out.made.resize(p.lots.size());
    for (size_t li = 0; li < p.lots.size(); li++) {
        const Lot& l = p.lots[li];
        // the village's style; a church its own palette (one tagged "church")
        BlueprintStyle style = o.style;
        std::string own = paletteFor(*l.bp);
        if (const BuildPalette* pal = own.empty() ? nullptr : gBuildPalettes.find(own)) {
            style.palette = pal->spec; style.paletteName = pal->name;
        }
        auto placements = l.bp->plan(l.lo, l.rot, &style);
        std::string bad = Blueprint::check(sm, placements, true);
        if (!bad.empty()) { out.failed++; if (out.why.empty()) out.why = l.bp->name + ": " + bad; continue; }
        std::string why;
        auto made = l.bp->paste(sm, editor, chests, placements, src, &style, &why);
        out.made[li] = made;
        out.pieces += (int)made.size();
        if (!made.empty()) out.buildings++;
        if (!why.empty()) { out.failed++; if (out.why.empty()) out.why = l.bp->name + ": " + why; }
    }
    for (size_t i = 0; i < p.roads.size(); i++) {
        Road r = p.roads[i];
        // a lane starts level with the road it leaves (laid just before)
        if (r.startLevel < 0 && (int)i >= p.mainRoads) {
            glm::ivec2 d = glm::sign(r.b - r.a);
            r.startLevel = groundAt(editor.worldRef(), r.a.x - d.x, r.a.y - d.y, p.center.y + 96);
        }
        editor.submit(layRoad(editor.worldRef(), sm, r, o, p.center.y + 96, src));
        editor.flush();
        out.roads++;
    }
    for (const Road& r : p.paths) {
        editor.submit(layRoad(editor.worldRef(), sm, r, o, p.center.y + 96, src, true));
        editor.flush();                                 // the next path reads the ground this one left
        out.paths++;
    }
    // doorsteps, after the paths (a path's first stretch is level with
    // the floor: the step runs out to meet it)
    for (size_t li = 0; li < p.lots.size(); li++)
        if (!out.made[li].empty() && !p.lots[li].bp->hasTag("plaza")) layDoorstep(p, li, out.made[li], sm, editor, o, src);
    editor.flush();
    // the well and lanterns, on the ground as the roads left it
    for (const Decor& d : p.decor) {
        uint16_t type = gStructures.find(d.type);
        const StructureType* t = gStructures.get(type);
        if (!t) continue;
        const VoxelVolume& v = t->shape(0, d.rot & 3);
        int top = -1;
        for (int z = 0; z < v.size.z; z++) for (int x = 0; x < v.size.x; x++)
            top = std::max(top, groundAt(editor.worldRef(), d.lo.x + x, d.lo.y + z, p.center.y + 96));
        if (top < 0) continue;
        glm::ivec3 origin = glm::ivec3(d.lo.x, top, d.lo.y) + v.anchor;
        if (!sm.checkPlace(type, origin, d.rot & 3, false).empty()) { out.decorSkipped++; continue; }
        sm.place(type, origin, d.rot & 3, src, 0);
        out.decor++;
    }
    editor.flush();
    return out;
}

} // namespace village
