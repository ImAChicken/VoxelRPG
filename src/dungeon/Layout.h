#pragma once
// ══════════════════════════════════════════════════════════
// Growing a dungeon from tiles (docs/dungeons.md, step 2)
// ══════════════════════════════════════════════════════════
// The dungeon is laid out on a 3D grid of half cells (16 voxels).
// Every tile placed must agree with everything around it, the rule
// wave function collapse works by: each of its outside face squares
// meets the same socket on the other side (a door meets a door, a
// wall a wall or untouched rock). Among the tiles and turns that fit
// a spot, one is picked by weight; when nothing fits, that stretch is
// undone and tried another way, and when the whole attempt can't be
// finished, it starts again with the next seed.
//
// In order:
//   1. the portal room, in the middle of the top level
//   2. the main path: from it, 1-3 corridors then a room, again and
//      again (MAIN rooms), going down a stair halfway, ending at the
//      boss's arena. Each step leaves by the door furthest from the
//      start, so the path wanders outward
//   3. side rooms: off the main path's spare doors, a corridor or
//      two and a room with one way in (SIDE of them)
//   4. caps: every door still open gets a dead end; one with no room
//      even for that is sealed (filled with wall when stamped)
//   5. checks: everything reachable from the portal room
// The same tiles and the same seed always give the same dungeon.

#include "Tile.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace dungeon {

struct LayoutParams {
    glm::ivec3 grid{ 64, 16, 64 };      // half cells (1024 x 256 x 1024 voxels)
    int  topFloor  = 9;                 // grid y of the first level's floor
    int  levelStep = 2;                 // half cells down per level (a stair's drop)
    int  levels    = 2;
    int  mainMin = 7, mainMax = 9;      // rooms on the main path (the stair counts as one)
    int  sideMin = 3, sideMax = 5;
    int  corridorMin = 1, corridorMax = 3;   // corridors between main path rooms
    int  attempts = 60;
};

struct PlacedTile {
    int        variant = -1;            // index into TileSet::allVariants()
    glm::ivec3 at{0};                   // grid half cell of its low corner
    std::string role;                   // portal, main, corridor, stair, boss, side, cap
    int        level = 0;
    int        pathIndex = -1;          // order along the main path (-1: off it)
};

// A doorway left open: a socket on a placed tile facing an empty cell
struct OpenDoor { glm::ivec3 cell; int face = 0; int owner = -1; std::string type; };

class Layout {
public:
    LayoutParams p;
    uint64_t seed = 0;
    int attemptsUsed = 0;
    std::vector<PlacedTile> tiles;
    std::vector<OpenDoor> sealed;       // doors nothing could be put behind: walled up when stamped
    std::vector<int> mainPath;          // tile indices from the portal room to the boss, in order
    int portal = -1, boss = -1;
    int mainRooms = 0, sideRooms = 0;
    std::string failure;                // why the last attempt gave up (when generate fails)

    const TileVariant& variantOf(const PlacedTile& t) const { return set->allVariants()[(size_t)t.variant]; }
    const TileSet* tileSet() const { return set; }

    // ── Generating ─────────────────────────────────────────
    bool generate(const TileSet& tiles_, uint64_t seed_, const LayoutParams& params = LayoutParams()) {
        set = &tiles_; p = params; seed = seed_;
        for (int a = 0; a < p.attempts; a++) {
            attemptsUsed = a + 1;
            rng.seed((uint32_t)(seed_ * 2654435761ull + (uint64_t)a * 40503ull + 12345u));
            if (attempt()) return true;
        }
        return false;
    }

    // Which tile is at a grid cell (-1: rock)
    int tileAt(glm::ivec3 c) const {
        if (!inGrid(c)) return -1;
        return occ[index(c)];
    }
    bool inGrid(glm::ivec3 c) const {
        return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < p.grid.x && c.y < p.grid.y && c.z < p.grid.z;
    }
    // Tiles joined to a tile through doorways
    std::vector<int> neighbours(int t) const {
        std::vector<int> out;
        const PlacedTile& pt = tiles[(size_t)t];
        const TileVariant& v = variantOf(pt);
        for (const Socket& s : v.sockets) {
            if (s.type == "wall") continue;
            glm::ivec3 n = pt.at + s.cell + dirStep(s.face);
            int o = tileAt(n);
            if (o >= 0 && o != t && std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
        }
        return out;
    }
    // Every tile reachable from the portal room
    int reachable() const {
        if (portal < 0) return 0;
        std::vector<char> seen(tiles.size(), 0);
        std::vector<int> q{ portal };
        seen[(size_t)portal] = 1;
        int n = 0;
        while (!q.empty()) {
            int t = q.back(); q.pop_back(); n++;
            for (int o : neighbours(t)) if (!seen[(size_t)o]) { seen[(size_t)o] = 1; q.push_back(o); }
        }
        return n;
    }
    int levelOf(int gridY) const { return std::max(0, (p.topFloor - gridY + p.levelStep - 1) / p.levelStep); }

private:
    const TileSet* set = nullptr;
    std::mt19937 rng;
    std::vector<int> occ;               // per grid cell: the tile there, -1 rock
    std::vector<int> used;              // per Tile (by its index in the set): how many placed

    size_t index(glm::ivec3 c) const { return ((size_t)c.y * p.grid.z + c.z) * p.grid.x + c.x; }
    float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    int randInt(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }
    int tileIndex(const Tile* t) const {
        const auto& all = set->all();
        for (size_t i = 0; i < all.size(); i++) if (all[i].get() == t) return (int)i;
        return -1;
    }

    // ── Placing and taking away ───────────────────────────
    bool fits(const TileVariant& v, glm::ivec3 at) const {
        for (int x = 0; x < v.cells.x; x++) for (int y = 0; y < v.cells.y; y++) for (int z = 0; z < v.cells.z; z++) {
            glm::ivec3 c = at + glm::ivec3(x, y, z);
            if (!inGrid(c) || occ[index(c)] >= 0) return false;
        }
        // every outside face agrees with what's beyond it
        for (int x = 0; x < v.cells.x; x++) for (int y = 0; y < v.cells.y; y++) for (int z = 0; z < v.cells.z; z++)
            for (int d = 0; d < 6; d++) {
                glm::ivec3 lc(x, y, z), n = lc + dirStep(d);
                if (n.x >= 0 && n.y >= 0 && n.z >= 0 && n.x < v.cells.x && n.y < v.cells.y && n.z < v.cells.z) continue;
                std::string mine = v.socketAt(lc, d);
                glm::ivec3 wc = at + n;
                if (!inGrid(wc)) { if (mine != "wall") return false; continue; }
                int o = occ[index(wc)];
                if (o < 0) continue;                                   // rock for now: a door here is left open
                const PlacedTile& ot = tiles[(size_t)o];
                if (variantOf(ot).socketAt(wc - ot.at, opposite(d)) != mine) return false;
            }
        const Tile* t = v.tile;
        int ti = tileIndex(t);
        if (t->info.maxCount >= 0 && ti >= 0 && used[(size_t)ti] >= t->info.maxCount) return false;
        int lvl = levelOf(at.y);
        if (lvl < t->info.depthMin || lvl > t->info.depthMax) return false;
        return true;
    }
    int place(int variant, glm::ivec3 at, const std::string& role) {
        const TileVariant& v = set->allVariants()[(size_t)variant];
        int id = (int)tiles.size();
        PlacedTile pt; pt.variant = variant; pt.at = at; pt.role = role; pt.level = levelOf(at.y);
        tiles.push_back(pt);
        for (int x = 0; x < v.cells.x; x++) for (int y = 0; y < v.cells.y; y++) for (int z = 0; z < v.cells.z; z++)
            occ[index(at + glm::ivec3(x, y, z))] = id;
        int ti = tileIndex(v.tile);
        if (ti >= 0) used[(size_t)ti]++;
        return id;
    }
    void undoTo(size_t count) {
        while (tiles.size() > count) {
            const PlacedTile& pt = tiles.back();
            const TileVariant& v = variantOf(pt);
            for (int x = 0; x < v.cells.x; x++) for (int y = 0; y < v.cells.y; y++) for (int z = 0; z < v.cells.z; z++)
                occ[index(pt.at + glm::ivec3(x, y, z))] = -1;
            int ti = tileIndex(v.tile);
            if (ti >= 0) used[(size_t)ti]--;
            tiles.pop_back();
        }
    }
    // The doors of a tile that face empty cells
    std::vector<OpenDoor> openDoorsOf(int t) const {
        std::vector<OpenDoor> out;
        const PlacedTile& pt = tiles[(size_t)t];
        const TileVariant& v = variantOf(pt);
        for (const Socket& s : v.sockets) {
            if (s.type == "wall") continue;
            glm::ivec3 c = pt.at + s.cell, n = c + dirStep(s.face);
            if (inGrid(n) && occ[index(n)] < 0) out.push_back({ c, s.face, t, s.type });
        }
        return out;
    }

    // Every way to put a tile of these kinds behind an open door
    struct Option { int variant; glm::ivec3 at; float weight; };
    std::vector<Option> options(const OpenDoor& door, const std::vector<std::string>& kinds, bool goingDown = false,
                                int maxDoors = 99, int exactDoors = -1) const {
        std::vector<Option> out;
        const auto& vs = set->allVariants();
        glm::ivec3 into = door.cell + dirStep(door.face);
        for (size_t i = 0; i < vs.size(); i++) {
            const TileVariant& v = vs[i];
            if (std::find(kinds.begin(), kinds.end(), v.tile->info.kind) == kinds.end()) continue;
            int doors = 0;
            for (const Socket& s : v.sockets) doors += s.type != "wall";
            if (doors > maxDoors || (exactDoors >= 0 && doors != exactDoors)) continue;
            int topY = 0;
            for (const Socket& s : v.sockets) topY = std::max(topY, s.cell.y);
            for (const Socket& s : v.sockets) {
                if (s.type != door.type || s.face != opposite(door.face)) continue;
                if (goingDown && s.cell.y != topY) continue;              // a stair: entered at its top
                if (!goingDown && v.tile->info.kind == "stair") continue;  // (only ever going down)
                glm::ivec3 at = into - s.cell;
                if (fits(v, at)) out.push_back({ (int)i, at, std::max(0.01f, v.tile->info.weight) });
            }
        }
        return out;
    }
    int pick(const std::vector<Option>& o, const std::function<float(const Option&)>& bias = nullptr) {
        if (o.empty()) return -1;
        float total = 0.0f;
        std::vector<float> w(o.size());
        for (size_t i = 0; i < o.size(); i++) { w[i] = o[i].weight * (bias ? bias(o[i]) : 1.0f); total += w[i]; }
        float r = rand01() * total;
        for (size_t i = 0; i < o.size(); i++) { r -= w[i]; if (r <= 0.0f) return (int)i; }
        return (int)o.size() - 1;
    }

    glm::vec3 centreOf(int t) const {
        const PlacedTile& pt = tiles[(size_t)t];
        return glm::vec3(pt.at) + glm::vec3(variantOf(pt).cells) * 0.5f;
    }
    // The open door of a tile that leads furthest from the start (with
    // a little chance), for the path to carry on through
    int onwardDoor(const std::vector<OpenDoor>& ds, glm::vec3 from) {
        if (ds.empty()) return -1;
        int best = 0; float bestScore = -1e9f;
        for (size_t i = 0; i < ds.size(); i++) {
            glm::vec3 out = glm::vec3(ds[i].cell + dirStep(ds[i].face));
            float s = glm::length(glm::vec2(out.x - from.x, out.z - from.z)) + rand01() * 6.0f;
            if (s > bestScore) { bestScore = s; best = (int)i; }
        }
        return best;
    }

    // A run of corridors then one tile of 'kinds' from an open door.
    // Returns the last tile placed, -1 when it couldn't (nothing left placed)
    int segment(OpenDoor from, int corridors, const std::vector<std::string>& kinds, const std::string& role,
                bool goingDown, glm::vec3 start, int maxDoorsEnd = 99, int exactDoorsEnd = -1) {
        size_t mark = tiles.size();
        for (int tries = 0; tries < 6; tries++) {
            undoTo(mark);
            OpenDoor at = from;
            bool ok = true;
            int n = std::max(0, corridors - tries / 2);
            for (int k = 0; k < n && ok; k++) {
                // plain corridors mostly (two doors), now and then a branch
                auto o = options(at, { "corridor" }, false, rand01() < 0.25f ? 4 : 2, -1);
                o.erase(std::remove_if(o.begin(), o.end(), [&](const Option& x) {
                    int doors = 0; for (auto& s : set->allVariants()[(size_t)x.variant].sockets) doors += s.type != "wall";
                    return doors < 2; }), o.end());
                int i = pick(o);
                if (i < 0) { ok = false; break; }
                int t = place(o[(size_t)i].variant, o[(size_t)i].at, "corridor");
                auto ds = openDoorsOf(t);
                int d = onwardDoor(ds, start);
                if (d < 0) { ok = false; break; }
                at = ds[(size_t)d];
            }
            if (!ok) continue;
            auto o = options(at, kinds, goingDown, maxDoorsEnd, exactDoorsEnd);
            int i = pick(o, [&](const Option& x) {
                // a room that leaves another way out is better on the path
                int doors = 0; for (auto& s : set->allVariants()[(size_t)x.variant].sockets) doors += s.type != "wall";
                return role == "main" ? (doors >= 2 ? 1.0f : 0.05f) : 1.0f;
            });
            if (i < 0) continue;
            return place(o[(size_t)i].variant, o[(size_t)i].at, role);
        }
        undoTo(mark);
        return -1;
    }

    bool attempt() {
        tiles.clear(); sealed.clear(); mainPath.clear();
        portal = boss = -1; mainRooms = sideRooms = 0;
        occ.assign((size_t)p.grid.x * p.grid.y * p.grid.z, -1);
        used.assign(set->all().size(), 0);
        failure.clear();

        // 1. the portal room in the middle of the top level
        std::vector<int> portals;
        for (size_t i = 0; i < set->allVariants().size(); i++)
            if (set->allVariants()[i].tile->info.kind == "portal_room") portals.push_back((int)i);
        if (portals.empty()) { failure = "no portal_room tile"; return false; }
        const TileVariant& pv = set->allVariants()[(size_t)portals[(size_t)randInt(0, (int)portals.size() - 1)]];
        glm::ivec3 pat(p.grid.x / 2 - pv.cells.x / 2, p.topFloor, p.grid.z / 2 - pv.cells.z / 2);
        if (!fits(pv, pat)) { failure = "the portal room doesn't fit"; return false; }
        portal = place((int)(&pv - &set->allVariants()[0]), pat, "portal");
        tiles[(size_t)portal].pathIndex = 0;
        mainPath.push_back(portal);
        glm::vec3 start = centreOf(portal);

        // 2. the main path
        int rooms = randInt(p.mainMin, p.mainMax);
        int stairAt = p.levels > 1 ? std::max(1, rooms / 2) : -1;
        int last = portal;
        for (int r = 0; r < rooms + 1; r++) {
            bool isBoss = r == rooms, isStair = r == stairAt && !isBoss;
            auto ds = openDoorsOf(last);
            if (ds.empty()) { failure = "the path ran into a dead end"; return false; }
            int placed = -1;
            size_t before = tiles.size();
            // try the doors of the last tile, furthest first
            std::vector<int> order;
            for (size_t i = 0; i < ds.size(); i++) order.push_back((int)i);
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                auto far = [&](const OpenDoor& d) { glm::vec3 o(d.cell + dirStep(d.face)); return glm::length(glm::vec2(o.x - start.x, o.z - start.z)); };
                return far(ds[(size_t)a]) > far(ds[(size_t)b]);
            });
            for (int di : order) {
                int corr = randInt(p.corridorMin, p.corridorMax);
                if (isBoss) placed = segment(ds[(size_t)di], corr, { "boss" }, "boss", false, start);
                else if (isStair) placed = segment(ds[(size_t)di], corr, { "stair" }, "stair", true, start);
                else placed = segment(ds[(size_t)di], corr, { "room" }, "main", false, start, 99, -1);
                if (placed >= 0) break;
            }
            if (placed < 0) { failure = isBoss ? "no room for the boss's arena" : isStair ? "no room for the stair" : "the path couldn't go on"; return false; }
            // the corridors just placed are on the path too
            for (size_t t = before; t < tiles.size(); t++) mainPath.push_back((int)t);
            if (!isBoss) mainRooms++;
            last = placed;
            if (isBoss) boss = placed;
        }
        for (size_t i = 0; i < mainPath.size(); i++) tiles[(size_t)mainPath[i]].pathIndex = (int)i;

        // 3. side rooms off the main path's spare doors
        int wantSide = randInt(p.sideMin, p.sideMax);
        for (int tries = 0; tries < 40 && sideRooms < wantSide; tries++) {
            std::vector<OpenDoor> spare;
            for (int t : mainPath) if (t != boss) for (const OpenDoor& d : openDoorsOf(t)) spare.push_back(d);
            if (spare.empty()) break;
            const OpenDoor& d = spare[(size_t)randInt(0, (int)spare.size() - 1)];
            int got = segment(d, randInt(0, 2), { "side" }, "side", false, centreOf(d.owner), 1, 1);
            if (got >= 0) sideRooms++;
        }
        if (sideRooms < p.sideMin) { failure = "only " + std::to_string(sideRooms) + " side rooms fit"; return false; }

        // 4. caps on every door still open (and the corridors leading off)
        for (int round = 0; round < 6; round++) {
            bool any = false;
            for (size_t t = 0; t < tiles.size(); t++)
                for (const OpenDoor& d : openDoorsOf((int)t)) {
                    any = true;
                    // a dead end; a side room while there's room for more of them
                    auto o = options(d, sideRooms < p.sideMax ? std::vector<std::string>{ "corridor", "side" } : std::vector<std::string>{ "corridor" }, false, 1, 1);
                    int i = pick(o, [&](const Option& x) { return set->allVariants()[(size_t)x.variant].tile->info.kind == "corridor" ? 3.0f : 1.0f; });
                    if (i >= 0) {
                        int c = place(o[(size_t)i].variant, o[(size_t)i].at, "cap");
                        if (variantOf(tiles[(size_t)c]).tile->info.kind == "side") { tiles[(size_t)c].role = "side"; sideRooms++; }
                    }
                }
            if (!any) break;
        }
        // anything still open is walled up
        for (size_t t = 0; t < tiles.size(); t++)
            for (const OpenDoor& d : openDoorsOf((int)t)) sealed.push_back(d);

        // 5. checks
        if (boss < 0) { failure = "no boss"; return false; }
        for (const Tile* req : requiredTiles())
            if (used[(size_t)tileIndex(req)] < req->info.minCount) { failure = "too few " + req->name; return false; }
        if (reachable() != (int)tiles.size()) { failure = "not everything can be reached"; return false; }
        return true;
    }
    std::vector<const Tile*> requiredTiles() const {
        std::vector<const Tile*> out;
        for (auto& t : set->all()) if (t->info.minCount > 0) out.push_back(t.get());
        return out;
    }
};

} // namespace dungeon
