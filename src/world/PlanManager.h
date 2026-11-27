#pragma once
// ══════════════════════════════════════════════════════════
// PlanManager — a world's buildings kept as plans
// ══════════════════════════════════════════════════════════
// The hammer's Build tab (game/BuildPlan.h) edits plans; this keeps
// them, makes their voxels, and keeps what hands do to them:
//   rebuild     a plan's voxels made again; only the ones that differ
//               from what's standing are written (one EditBatch,
//               label "plan"). Raise a wall, drop the floor: the rest
//               of the building isn't touched
//   overrides   any other edit inside a plan (a block broken out of a
//               wall, a torch on it, a chest on the floor) is kept, in
//               the building's own frame (x, z from its origin, y from
//               its floor), and laid over the plan whenever it's made,
//               so it rises with the floor and survives every change
//   undo        the Build tab's own: the plans before each change
//   doors       a door in an opening is a door piece (StructureManager:
//               it opens and closes like any door); the plan places,
//               moves and removes its own (straight walls only: a
//               door piece turns in quarter turns)
// The grid is the world's (3 ft cells from world 0), so plans side by
// side line up and a foundation laid against one joins it.
// Saved as plans.dat in the world folder (GameSession). Single player
// and the host for now: a server's clients can't send plan edits yet.
// (Claude, 2026-11-25)
#include "../game/BuildPlan.h"
#include "../game/Palettes.h"
#include "../chunk/ChunkManager.h"
#include "WorldEditor.h"
#include "StructureManager.h"
#include <fstream>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

class PlanManager {
public:
    static constexpr size_t UNDO_MAX = 40;
    static constexpr int LEVEL_CLEAR = 36;          // a new foundation clears this much ground over its floor
    static constexpr const char* LABEL = "plan";
    static constexpr const char* LEVEL_LABEL = "plan-level";

    PlanManager(ChunkManager& w, WorldEditor& e, StructureManager* s = nullptr) : world(w), editor(e), structs(s) {}
    StructureManager* structs = nullptr;            // where doors go (none: openings stay open)
    std::string defaultDoor = "plank_ledged_door";

    const std::vector<plan::Plan>& all() const { return plans; }
    plan::Plan* get(uint32_t id) { for (plan::Plan& p : plans) if (p.id == id) return &p; return nullptr; }
    const plan::Plan* get(uint32_t id) const { for (const plan::Plan& p : plans) if (p.id == id) return &p; return nullptr; }
    size_t overrideCount(uint32_t id) const { auto it = overrides.find(id); return it == overrides.end() ? 0 : it->second.size(); }
    size_t builtCount(uint32_t id) const { auto it = built.find(id); return it == built.end() ? 0 : it->second.size(); }

    // ── The grid ──────────────────────────────────────────
    static int gridOf(int voxel) { return (int)std::floor((voxel + plan::GRID * 0.5f) / plan::GRID); }   // nearest grid point
    static int cellOf(int voxel) { return (int)std::floor((float)voxel / plan::GRID); }                  // the cell it's in
    static glm::ivec2 gridPoint(glm::vec3 p) { return { gridOf((int)std::floor(p.x)), gridOf((int)std::floor(p.z)) }; }
    static glm::ivec2 cell(glm::vec3 p) { return { cellOf((int)std::floor(p.x)), cellOf((int)std::floor(p.z)) }; }
    // a plan's local grid ↔ the world's
    static glm::ivec2 toLocal(const plan::Plan& p, glm::ivec2 worldGrid) { return worldGrid - glm::ivec2(p.origin.x, p.origin.z) / plan::GRID; }
    static glm::ivec2 toWorld(const plan::Plan& p, glm::ivec2 localGrid) { return localGrid + glm::ivec2(p.origin.x, p.origin.z) / plan::GRID; }

    // ── Finding ───────────────────────────────────────────
    // The plan whose footprint (foundations, walls) a world voxel column is over
    plan::Plan* planOver(int x, int z, int y = INT32_MIN) {
        for (plan::Plan& p : plans) {
            glm::ivec2 lo, hi;
            p.bounds(lo, hi, 0);
            glm::ivec2 v(x - p.origin.x, z - p.origin.z);
            if (v.x < lo.x || v.y < lo.y || v.x > hi.x || v.y > hi.y) continue;
            if (y != INT32_MIN && (y < p.origin.y - plan::SINK - 1 || y > p.topY() + 2)) continue;
            return &p;
        }
        return nullptr;
    }
    // The wall a world voxel is part of: its plan, its index, the side
    // of it the voxel is on (or the point is near)
    struct WallHit { plan::Plan* plan = nullptr; int wall = -1; int side = 0; };
    WallHit wallAt(glm::vec3 at) {
        WallHit h;
        int x = (int)std::floor(at.x), y = (int)std::floor(at.y), z = (int)std::floor(at.z);
        float best = 1e9f;
        for (plan::Plan& p : plans) {
            if (y < p.floorY() - 1 || y > p.topY() + 1) continue;
            glm::vec2 c(x - p.origin.x + 0.5f, z - p.origin.z + 0.5f);
            for (size_t i = 0; i < p.walls.size(); i++) {
                const plan::Wall& w = p.walls[i];
                float d = w.distance(c);
                const int base = p.levelY(w.level);
                if (y < base - 1 || y >= base + p.wallTop(w, glm::ivec2(x - p.origin.x, z - p.origin.z))) continue;
                if (d <= w.thick * 0.5f + 1.0f && d < best) {
                    best = d; h.plan = &p; h.wall = (int)i;
                    h.side = w.side(glm::vec2(at.x - p.origin.x, at.z - p.origin.z));
                }
            }
        }
        return h;
    }

    // ── Editing (each is one undo step) ───────────────────
    // A foundation over world cells a..b (inclusive), on ground at groundY.
    // Joins the plan it touches (overlaps or shares an edge with), else
    // makes a new one. Returns the plan's id (0: refused)
    uint32_t addFoundation(glm::ivec2 cellA, glm::ivec2 cellB, int groundY, int floor, const std::string& palette) {
        snapshot();
        glm::ivec2 lo = glm::min(cellA, cellB), hi = glm::max(cellA, cellB);
        plan::Plan* p = nullptr;
        for (plan::Plan& q : plans) {
            if (!q.hasFoundation()) continue;
            for (const plan::Foundation& f : q.foundations) {
                glm::ivec2 fl = f.lo() + toWorld(q, glm::ivec2(0)), fh = f.hi() + toWorld(q, glm::ivec2(0));
                if (lo.x <= fh.x + 1 && hi.x >= fl.x - 1 && lo.y <= fh.y + 1 && hi.y >= fl.y - 1) { p = &q; break; }
            }
            if (p) break;
        }
        if (!p) {
            // walls standing on the ground there: they get this foundation
            for (plan::Plan& q : plans) if (!q.hasFoundation() && planTouches(q, lo, hi)) { p = &q; break; }
        }
        if (!p) {
            plan::Plan np;
            np.id = ++nextId;
            np.origin = glm::ivec3(lo.x * plan::GRID, groundY, lo.y * plan::GRID);
            np.floor = std::clamp(floor, 1, plan::MAX_FLOOR);
            np.palette = palette;
            plans.push_back(np);
            p = &plans.back();
        } else if (!p->hasFoundation()) {
            p->floor = std::clamp(floor, 1, plan::MAX_FLOOR);
        }
        plan::Foundation f;
        f.a = toLocal(*p, lo); f.b = toLocal(*p, hi);
        p->foundations.push_back(f);
        clearOver(*p, f);
        rebuild(*p);
        return p->id;
    }
    // A wall between world voxel columns a and b (the tools snap them:
    // the grid, or a part) on the plan under it, or a new plan on the
    // ground at groundY. Returns the plan's id (0: none made)
    // On a level over the ground floor it needs a plan there already.
    // height: its own (0 or its level's: it follows the level's)
    uint32_t addWall(glm::ivec2 a, glm::ivec2 b, int groundY, int height, int thick, const std::string& palette, int level = 0) {
        if (a == b) return 0;
        plan::Plan* p = level > 0 ? planUpstairs(a, b) : nullptr;
        if (level > 0 && !p) return 0;
        snapshot();
        if (!p) p = planFor(a, b, groundY, palette);
        plan::Wall w;
        w.a = local(*p, a); w.b = local(*p, b);
        w.level = level;
        w.height = ownHeight(*p, level, height);
        w.thick = std::clamp(thick, 1, 7);
        for (const plan::Wall& o : p->walls) if (o.level == w.level && o.sameAs(w)) { undo.pop_back(); return p->id; }   // already there
        p->walls.push_back(w);
        rebuild(*p);
        return p->id;
    }
    // Four walls round a rectangle (world voxel columns at its corners)
    // Upstairs it gets a deck under it (its floor) where there's none
    uint32_t addRoom(glm::ivec2 a, glm::ivec2 b, int groundY, int height, int thick, const std::string& palette, int level = 0) {
        glm::ivec2 lo = glm::min(a, b), hi = glm::max(a, b);
        if (hi.x - lo.x < 3 || hi.y - lo.y < 3) return 0;
        plan::Plan* p = level > 0 ? planUpstairs(lo, hi) : nullptr;
        if (level > 0 && !p) return 0;
        snapshot();
        if (!p) p = planFor(lo, hi, groundY, palette);
        if (level > 0) {
            plan::Deck d;
            d.level = level;
            glm::ivec2 la = local(*p, lo), lb = local(*p, hi);
            d.a = glm::ivec2((int)std::floor((float)la.x / plan::GRID), (int)std::floor((float)la.y / plan::GRID));
            d.b = glm::ivec2((int)std::ceil((float)lb.x / plan::GRID) - 1, (int)std::ceil((float)lb.y / plan::GRID) - 1);
            bool have = false;
            for (const plan::Deck& o : p->decks) have = have || (o.level == level && o.lo() == d.lo() && o.hi() == d.hi());
            if (!have) p->decks.push_back(d);
        }
        const glm::ivec2 c[4] = { lo, { hi.x, lo.y }, hi, { lo.x, hi.y } };
        for (int i = 0; i < 4; i++) {
            plan::Wall w;
            w.a = local(*p, c[i]); w.b = local(*p, c[(i + 1) % 4]);
            w.level = level;
            w.height = ownHeight(*p, level, height);
            w.thick = std::clamp(thick, 1, 7);
            bool dup = false;
            for (const plan::Wall& o : p->walls) dup = dup || (o.level == w.level && o.sameAs(w));
            if (!dup) p->walls.push_back(w);
        }
        rebuild(*p);
        return p->id;
    }
    // A pillar at a world voxel column: through any wall there
    uint32_t addPillar(glm::ivec2 at, int groundY, int size, bool round, int height, const std::string& palette, int level = 0) {
        plan::Plan* p = level > 0 ? planUpstairs(at, at) : nullptr;
        if (level > 0 && !p) return 0;
        snapshot();
        if (!p) p = planFor(at, at, groundY, palette);
        plan::Pillar q;
        q.at = local(*p, at);
        q.level = level;
        q.size = std::clamp(size, 1, 15);
        q.round = round;
        q.height = height > 0 ? std::clamp(height, 3, plan::MAX_WALL_H) : 0;
        for (const plan::Pillar& o : p->pillars) if (o.at == q.at && o.level == q.level) { undo.pop_back(); return p->id; }
        p->pillars.push_back(q);
        rebuild(*p);
        return p->id;
    }

    // ── Storeys, stairs, fences ───────────────────────────
    // A level's wall height: every wall on it that follows it changes,
    // and every level over it moves up or down with it
    bool setLevelHeight(uint32_t id, int level, int height) {
        plan::Plan* p = get(id);
        if (!p || level < 0 || level >= plan::MAX_LEVELS) return false;
        int h = std::clamp(height, 6, plan::MAX_WALL_H);
        if (p->levelHeight(level) == h) return false;
        snapshot();
        p = get(id);
        if ((int)p->levelH.size() <= level) p->levelH.resize((size_t)level + 1, 0);
        p->levelH[(size_t)level] = h;
        rebuild(*p);
        return true;
    }
    // A plan's own railings round its stairwells and open deck edges, on or off
    bool setRails(uint32_t id, bool on) {
        plan::Plan* p = get(id);
        if (!p || p->rails == on) return false;
        snapshot();
        p = get(id);
        p->rails = on;
        rebuild(*p);
        return true;
    }
    // ... every plan's, and new plans' (one undo step)
    bool railsDefault = true;
    void setRailsAll(bool on) {
        railsDefault = on;
        bool any = false;
        for (const plan::Plan& p : plans) any = any || p.rails != on;
        if (!any) return;
        snapshot();
        for (plan::Plan& p : plans) if (p.rails != on) { p.rails = on; rebuild(p); }
    }
    // A floor on an upper level over world cells a..b (the plan under them)
    uint32_t addDeck(glm::ivec2 cellA, glm::ivec2 cellB, int level) {
        if (level < 1 || level >= plan::MAX_LEVELS) return 0;
        glm::ivec2 lo = glm::min(cellA, cellB), hi = glm::max(cellA, cellB);
        plan::Plan* p = planUpstairs(lo * plan::GRID, (hi + glm::ivec2(1)) * plan::GRID);
        if (!p) return 0;
        snapshot();
        p = get(p->id);
        plan::Deck d;
        d.level = level;
        d.a = toLocal(*p, lo); d.b = toLocal(*p, hi);
        p->decks.push_back(d);
        rebuild(*p);
        return p->id;
    }
    // A stair up from a level at a world column, climbing toward dir
    // (0 +x, 1 +z, 2 -x, 3 -z)
    // An L, U or spiral turns toward 'turn' (+1: toward plan::Stair::side())
    uint32_t addStair(glm::ivec2 at, int dir, int width, int level, int groundY = 0, const std::string& palette = "",
                      plan::StairShape shape = plan::StairShape::Straight, int turn = 1) {
        if (level < 0 || level + 1 >= plan::MAX_LEVELS) return 0;
        plan::Plan* p = planUpstairs(at, at);
        if (!p && level > 0) return 0;
        snapshot();
        if (!p) p = planFor(at, at, groundY, palette);
        plan::Stair t;
        t.at = local(*p, at);
        t.dir = dir & 3;
        t.width = std::clamp(width, 3, 30);
        t.level = level;
        t.shape = shape;
        t.turn = turn < 0 ? -1 : 1;
        p->stairs.push_back(t);
        rebuild(*p);
        return p->id;
    }
    // A fence or railing between world columns (on the plan there, or a
    // new one on the ground: a garden fence)
    uint32_t addFence(glm::ivec2 a, glm::ivec2 b, int groundY, plan::FenceStyle style, int height, int level = 0,
                      const std::string& palette = "", uint16_t block = 0) {
        if (a == b) return 0;
        plan::Plan* p = level > 0 ? planUpstairs(a, b) : nullptr;
        if (level > 0 && !p) return 0;
        snapshot();
        if (!p) p = planFor(a, b, groundY, palette);
        plan::Fence f;
        f.a = local(*p, a); f.b = local(*p, b);
        f.level = level;
        f.style = style;
        f.height = std::clamp(height, 3, 30);
        f.block = block;
        p->fences.push_back(f);
        rebuild(*p);
        return p->id;
    }

    // ── Snapping (the tools' cursor) ──────────────────────
    // The world voxel column a wall or pillar end goes to, near a point:
    // a part of a plan close by (a wall's end or middle, a pillar, a
    // foundation's corner) or another point given (an old build piece's
    // corner), else the grid's nearest point. free: none of it, the
    // column under the point
    static constexpr float SNAP_R = 4.0f;           // vox: how close a part pulls
    struct Snap { glm::ivec2 at{ 0 }; int kind = 0; };   // kind: 0 grid, 1 a part, 2 free
    Snap snap(glm::vec3 p, bool free, const std::vector<glm::ivec2>& extra = {}) const {
        Snap out;
        glm::ivec2 raw((int)std::floor(p.x), (int)std::floor(p.z));
        if (free) { out.at = raw; out.kind = 2; return out; }
        float best = SNAP_R;
        auto consider = [&](glm::ivec2 c) {
            float d = glm::length(glm::vec2(c) + glm::vec2(0.5f) - glm::vec2(p.x, p.z));
            if (d < best) { best = d; out.at = c; out.kind = 1; }
        };
        for (const plan::Plan& q : plans) {
            glm::ivec2 o(q.origin.x, q.origin.z);
            for (const plan::Wall& w : q.walls) { consider(o + w.a); consider(o + w.b); consider(o + (w.a + w.b) / 2); }
            for (const plan::Pillar& pl : q.pillars) consider(o + pl.at);
            for (const plan::Foundation& f : q.foundations) {
                glm::ivec2 lo = o + f.lo() * plan::GRID, hi = o + (f.hi() + glm::ivec2(1)) * plan::GRID;
                consider(lo); consider(hi); consider({ lo.x, hi.y }); consider({ hi.x, lo.y });
            }
        }
        for (glm::ivec2 c : extra) consider(c);
        if (out.kind == 0) out.at = glm::ivec2(gridOf(raw.x), gridOf(raw.y)) * plan::GRID;
        return out;
    }

    bool setWallHeight(uint32_t id, int wall, int height) {
        plan::Plan* p = get(id);
        if (!p || wall < 0 || wall >= (int)p->walls.size()) return false;
        int h = std::clamp(height, 3, plan::MAX_WALL_H);
        if (p->walls[(size_t)wall].height == h) return false;
        snapshot();
        p = get(id);
        p->walls[(size_t)wall].height = h;
        rebuild(*p);
        return true;
    }
    bool setFloor(uint32_t id, int floor) {
        plan::Plan* p = get(id);
        if (!p || !p->hasFoundation()) return false;
        int f = std::clamp(floor, 1, plan::MAX_FLOOR);
        if (p->floor == f) return false;
        snapshot();
        p = get(id);
        p->floor = f;
        rebuild(*p);
        return true;
    }
    bool paint(uint32_t id, int wall, int side, uint16_t block) {
        plan::Plan* p = get(id);
        if (!p || wall < 0 || wall >= (int)p->walls.size()) return false;
        if (p->walls[(size_t)wall].paint[side & 1] == block) return false;
        snapshot();
        p = get(id);
        p->walls[(size_t)wall].paint[side & 1] = block;
        rebuild(*p);
        return true;
    }
    bool setPalette(uint32_t id, const std::string& palette) {
        plan::Plan* p = get(id);
        if (!p || p->palette == palette) return false;
        snapshot();
        p = get(id);
        p->palette = palette;
        rebuild(*p);
        return true;
    }
    // ── Roofs ─────────────────────────────────────────────
    // A roof over world wall columns a..b (the plan under them gets it;
    // none: 0). axis -1: the ridge along the longer side. Returns the plan
    uint32_t addRoof(glm::ivec2 a, glm::ivec2 b, plan::RoofStyle style, int axis, int peak, int overhang, uint16_t block = 0, int level = 0) {
        glm::ivec2 m = (a + b) / 2;
        plan::Plan* p = nullptr;
        for (plan::Plan& q : plans) {
            glm::ivec2 lo, hi; q.bounds(lo, hi, 0);
            glm::ivec2 v = m - glm::ivec2(q.origin.x, q.origin.z);
            if (v.x >= lo.x && v.y >= lo.y && v.x <= hi.x && v.y <= hi.y) { p = &q; break; }
        }
        if (!p) return 0;
        glm::ivec2 o(p->origin.x, p->origin.z);
        plan::Roof r;
        r.a = glm::min(a, b) - o; r.b = glm::max(a, b) - o;
        glm::ivec2 sz = r.hi() - r.lo();
        if (sz.x < 4 || sz.y < 4) return 0;
        r.style = style;
        r.axis = axis >= 0 ? axis : (sz.x >= sz.y ? 0 : 1);
        r.peak = std::clamp(peak, 0, MAX_PEAK);
        r.overhang = std::clamp(overhang, 0, 9);
        r.block = block;
        r.level = level;
        snapshot();
        p = get(p->id);
        // one roof over the same walls: replaced
        for (size_t i = 0; i < p->roofs.size();)
            if (p->roofs[i].lo() == r.lo() && p->roofs[i].hi() == r.hi() && p->roofs[i].level == level) p->roofs.erase(p->roofs.begin() + (long)i); else i++;
        p->roofs.push_back(r);
        rebuild(*p);
        return p->id;
    }
    // A roof over a plan's walls (all of them: their outside line)
    uint32_t roofPlan(uint32_t id, plan::RoofStyle style, int axis, int peak, int overhang, uint16_t block = 0, int level = 0) {
        const plan::Plan* p = get(id);
        if (!p || p->walls.empty()) return 0;
        glm::ivec2 lo(INT32_MAX), hi(INT32_MIN);
        for (const plan::Wall& w : p->walls) if (w.level == level) { lo = glm::min(lo, glm::min(w.a, w.b)); hi = glm::max(hi, glm::max(w.a, w.b)); }
        glm::ivec2 o(p->origin.x, p->origin.z);
        if (lo.x > hi.x) return 0;
        return addRoof(lo + o, hi + o, style, axis, peak, overhang, block, level);
    }
    // A roof over the room a world column is in (its walls' lines); not in
    // a room: over all the plan's walls
    uint32_t roofRoom(glm::ivec2 col, plan::RoofStyle style, int axis, int peak, int overhang, uint16_t block = 0, int level = 0) {
        plan::Plan* p = planOver(col.x, col.y);
        if (!p) p = planUpstairs(col, col);
        if (!p) return 0;
        plan::Rooms r = plan::rooms(*p, level);
        glm::ivec2 v = col - glm::ivec2(p->origin.x, p->origin.z);
        if (r.at(v) != 2) return roofPlan(p->id, style, axis, peak, overhang, block, level);
        glm::ivec2 lo = v, hi = v;
        std::vector<glm::ivec2> stack{ v };
        std::vector<uint8_t> seen(r.cell.size(), 0);
        auto idx = [&](glm::ivec2 q) { return (size_t)((q.y - r.lo.y) * r.w + (q.x - r.lo.x)); };
        seen[idx(v)] = 1;
        while (!stack.empty()) {
            glm::ivec2 q = stack.back(); stack.pop_back();
            lo = glm::min(lo, q); hi = glm::max(hi, q);
            for (glm::ivec2 d : { glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1) }) {
                glm::ivec2 n = q + d;
                if (r.at(n) != 2 || seen[idx(n)]) continue;
                seen[idx(n)] = 1; stack.push_back(n);
            }
        }
        // the room's cells stop half a wall in from the wall lines
        int half = plan::THICK / 2 + 1;
        for (const plan::Wall& w : p->walls) half = std::max(half, w.thick / 2 + 1);
        glm::ivec2 o(p->origin.x, p->origin.z);
        return addRoof(lo - glm::ivec2(half) + o, hi + glm::ivec2(half) + o, style, axis, peak, overhang, block, level);
    }
    struct RoofHit { plan::Plan* plan = nullptr; int roof = -1; };
    // The roof a world voxel is in (or just on / under)
    RoofHit roofAt(glm::vec3 at) {
        RoofHit h;
        int x = (int)std::floor(at.x), y = (int)std::floor(at.y), z = (int)std::floor(at.z);
        for (plan::Plan& p : plans) {
            glm::ivec2 v(x - p.origin.x, z - p.origin.z);
            for (size_t i = 0; i < p.roofs.size(); i++) {
                const plan::Roof& r = p.roofs[i];
                if (!r.over(v)) continue;
                int t = p.levelY(r.level) + p.roofBase(r) + r.top(v);
                if (y >= t - r.thick - 3 && y <= t + 1) { h.plan = &p; h.roof = (int)i; return h; }
            }
        }
        return h;
    }
    // Change a roof (its peak, style, ridge, overhang, block): one undo step
    bool setRoof(uint32_t id, int roof, const plan::Roof& to) {
        plan::Plan* p = get(id);
        if (!p || roof < 0 || roof >= (int)p->roofs.size()) return false;
        plan::Roof r = to;
        r.peak = std::clamp(r.peak, 0, MAX_PEAK);
        r.overhang = std::clamp(r.overhang, 0, 9);
        snapshot();
        p = get(id);
        p->roofs[(size_t)roof] = r;
        rebuild(*p);
        return true;
    }
    static constexpr int MAX_PEAK = 60;

    // ── Openings (frames, doors, windows) ─────────────────
    // A frame in a wall, centred 'along' voxels from its first end (a
    // door at least as big as a door piece goes). One already there,
    // or overlapping, is replaced. Kept off the wall's ends and under
    // its top (a lintel stays). -1: no room for it
    int addOpening(uint32_t id, int wall, int along, int width, int height, int sill, plan::Fill fill, const std::string& door = "") {
        plan::Plan* p = get(id);
        if (!p || wall < 0 || wall >= (int)p->walls.size()) return -1;
        const plan::Wall& wl = p->walls[(size_t)wall];
        plan::Opening h;
        h.fill = fill;
        h.door = door;
        h.sill = fill == plan::Fill::Window ? std::max(1, sill) : std::max(0, sill);
        h.width = std::max(fill == plan::Fill::Door ? 9 : 2, width);
        h.height = std::max(fill == plan::Fill::Door ? 21 : 2, height);
        {
            // under the wall's top where it goes (a gable end is taller in the middle)
            glm::vec2 u = glm::length(glm::vec2(wl.b - wl.a)) > 0 ? glm::normalize(glm::vec2(wl.b - wl.a)) : glm::vec2(1, 0);
            auto colAt = [&](int t) { return glm::ivec2(glm::floor(wl.va() + u * (float)t)); };
            const int alongC = std::clamp(along, 0, (int)wl.length());
            int top = std::min(p->wallTop(wl, colAt(alongC - h.width / 2 - 1)), p->wallTop(wl, colAt(alongC + h.width - h.width / 2)));
            h.height = std::min(h.height, top - 1 - h.sill);
        }
        int len = (int)std::floor(wl.length());
        int edge = wl.thick / 2 + 2;                     // clear of a wall meeting its end, and the frame
        if (h.height < 2 || h.width > len - 2 * edge) return -1;
        if (fill == plan::Fill::Door && h.height < 21) return -1;
        h.along = std::clamp(along, edge + h.width / 2, len - edge - (h.width - h.width / 2));
        snapshot();
        p = get(id);
        auto& os = p->walls[(size_t)wall].openings;
        for (size_t i = 0; i < os.size();) { if (os[i].overlaps(h)) os.erase(os.begin() + (long)i); else i++; }
        os.push_back(h);
        std::sort(os.begin(), os.end(), [](const plan::Opening& x, const plan::Opening& y) { return x.along < y.along; });
        rebuild(*p);
        for (size_t i = 0; i < os.size(); i++) if (os[i].along == h.along) return (int)i;
        return -1;
    }
    // What fills an opening: nothing, a door, a window
    bool setOpeningFill(uint32_t id, int wall, int hole, plan::Fill fill) {
        plan::Plan* p = get(id);
        if (!p || wall < 0 || wall >= (int)p->walls.size() || hole < 0 || hole >= (int)p->walls[(size_t)wall].openings.size()) return false;
        if (p->walls[(size_t)wall].openings[(size_t)hole].fill == fill) return false;
        snapshot();
        p = get(id);
        p->walls[(size_t)wall].openings[(size_t)hole].fill = fill;
        rebuild(*p);
        return true;
    }

    // Take away what's at a world voxel: a wall, else the foundation
    // under it. An empty plan goes. true: something went
    bool removeAt(glm::vec3 at) {
        RoofHit rh = roofAt(at);
        if (rh.plan) {
            snapshot();
            plan::Plan* q = get(rh.plan->id);
            q->roofs.erase(q->roofs.begin() + rh.roof);
            rebuild(*q);
            dropIfEmpty(q->id);
            return true;
        }
        for (plan::Plan& p : plans) {
            glm::ivec2 v((int)std::floor(at.x) - p.origin.x, (int)std::floor(at.z) - p.origin.z);
            for (size_t i = 0; i < p.pillars.size(); i++)
                if (glm::length(glm::vec2(v - p.pillars[i].at)) <= p.pillars[i].size * 0.5f + 0.8f &&
                    at.y >= p.levelY(p.pillars[i].level) - 1 && at.y <= p.levelY(p.pillars[i].level) + p.pillarHeight(p.pillars[i]) + 1) {
                    snapshot();
                    plan::Plan* q = get(p.id);
                    q->pillars.erase(q->pillars.begin() + (long)i);
                    rebuild(*q);
                    dropIfEmpty(q->id);
                    return true;
                }
        }
        // a stair or a fence where it is
        {
            int x = (int)std::floor(at.x), y = (int)std::floor(at.y), z = (int)std::floor(at.z);
            for (plan::Plan& p : plans) {
                glm::ivec2 v(x - p.origin.x, z - p.origin.z);
                for (size_t i = 0; i < p.stairs.size(); i++) {
                    const plan::Stair& t = p.stairs[i];
                    const int st = t.stepAt(v, p.stairRise(t.level));
                    if (st < 0) continue;
                    int base = p.levelY(t.level);
                    if (y < base - 1 || y > base + t.top(st, p.stairRise(t.level)) + 1) continue;
                    snapshot();
                    plan::Plan* q = get(p.id);
                    q->stairs.erase(q->stairs.begin() + (long)i);
                    rebuild(*q); dropIfEmpty(q->id);
                    return true;
                }
                for (size_t i = 0; i < p.fences.size(); i++) {
                    const plan::Fence& f = p.fences[i];
                    bool on = false;
                    for (glm::ivec2 c : f.columns()) on = on || (std::abs(c.x - v.x) <= 1 && std::abs(c.y - v.y) <= 1);
                    int base = p.levelY(f.level) + p.footAt(f.level, v);
                    if (!on || y < base - 1 || y > base + f.height + 1) continue;
                    snapshot();
                    plan::Plan* q = get(p.id);
                    q->fences.erase(q->fences.begin() + (long)i);
                    rebuild(*q); dropIfEmpty(q->id);
                    return true;
                }
            }
        }
        WallHit h = wallAt(at);
        if (h.plan) {
            const plan::Wall& wl = h.plan->walls[(size_t)h.wall];
            glm::vec2 c((int)std::floor(at.x) - h.plan->origin.x + 0.5f, (int)std::floor(at.z) - h.plan->origin.z + 0.5f);
            int hole = wl.openingAt(wl.along(c), (int)std::floor(at.y) - h.plan->levelY(wl.level));
            snapshot();
            if (hole >= 0) {
                plan::Plan* p = get(h.plan->id);
                auto& os = p->walls[(size_t)h.wall].openings;
                os.erase(os.begin() + hole);
                rebuild(*p);
                return true;
            }
            plan::Plan* p = get(h.plan->id);
            p->walls.erase(p->walls.begin() + h.wall);
            rebuild(*p);
            dropIfEmpty(p->id);
            return true;
        }
        int x = (int)std::floor(at.x), z = (int)std::floor(at.z);
        // an upper floor (a deck) under the point
        for (plan::Plan& q : plans) {
            glm::ivec2 v(x - q.origin.x, z - q.origin.z);
            for (size_t i = 0; i < q.decks.size(); i++) {
                const plan::Deck& d = q.decks[i];
                int top = q.levelY(d.level);
                if (!d.covers(v) || at.y < top - plan::DECK - 1 || at.y > top + 1) continue;
                snapshot();
                plan::Plan* pp = get(q.id);
                pp->decks.erase(pp->decks.begin() + (long)i);
                rebuild(*pp); dropIfEmpty(pp->id);
                return true;
            }
        }
        plan::Plan* p = planOver(x, z, (int)std::floor(at.y));
        if (!p) return false;
        glm::ivec2 v(x - p->origin.x, z - p->origin.z);
        for (size_t i = 0; i < p->foundations.size(); i++)
            if (p->foundations[i].covers(v)) {
                snapshot();
                p = get(p->id);
                p->foundations.erase(p->foundations.begin() + (long)i);
                rebuild(*p);
                dropIfEmpty(p->id);
                return true;
            }
        return false;
    }

    // ── Undo (the Build tab's) ────────────────────────────
    bool undoLast() {
        if (undo.empty()) return false;
        std::vector<plan::Plan> before = std::move(undo.back());
        undo.pop_back();
        // every plan that was or is: made again as it was
        std::vector<uint32_t> ids;
        for (const plan::Plan& p : plans) ids.push_back(p.id);
        for (const plan::Plan& p : before) if (std::find(ids.begin(), ids.end(), p.id) == ids.end()) ids.push_back(p.id);
        plans = std::move(before);
        for (uint32_t id : ids) {
            if (plan::Plan* p = get(id)) rebuild(*p);
            else unbuild(id);
        }
        return true;
    }
    size_t undoCount() const { return undo.size(); }

    // ── Hands on a plan's voxels ──────────────────────────
    // Every edit that isn't the plan's own: inside a plan, it's kept.
    // (WorldEditor::onEdit; GameSession hooks it up)
    void noteEdit(glm::ivec3 pos, uint16_t id, const std::string& label) {
        if (label == LABEL || label == LEVEL_LABEL) return;
        if (label == "open door" || label == "close door" || quiet.count(label)) return;   // the plan's doors
        for (plan::Plan& p : plans) {
            glm::ivec2 lo, hi;
            p.bounds(lo, hi, 0);
            glm::ivec2 v(pos.x - p.origin.x, pos.z - p.origin.z);
            if (v.x < lo.x || v.y < lo.y || v.x > hi.x || v.y > hi.y) continue;
            if (pos.y < p.origin.y - plan::SINK || pos.y > p.topY() + 8) continue;
            overrides[p.id][plan::key(v.x, pos.y - p.floorY(), v.y)] = id;
            built[p.id][plan::key(pos.x, pos.y, pos.z)] = id;        // (what stands now)
            return;
        }
    }

    // ── Making the voxels ─────────────────────────────────
    plan::Looks looksFor(const plan::Plan& p) const {
        plan::Looks L;
        const BuildPalette* pal = gBuildPalettes.find(p.palette.empty() ? defaultPalette : p.palette);
        auto pick = [&](const char* role, const char* fallback) {
            std::string n = pal ? pal->spec.block(role) : std::string();
            uint16_t id = n.empty() ? 0 : gRegistry.getIDByName(n);
            return id ? id : gRegistry.getIDByName(fallback);
        };
        L.fill = pick("fill", "planks");
        L.paint = pick("paint", "planks");
        L.base = pick("base", "cobblestone");
        L.floor = pick("floor", "planks");
        L.post = pick("post", "dark_log");
        L.frame = pick("beam", "dark_planks");
        L.roof = pick("roof", "roof_tiles");
        std::string g = pal ? pal->spec.block("glass") : std::string("glass");
        L.glass = g == "none" ? 0 : (gRegistry.getIDByName(g.empty() ? "glass" : g));
        return L;
    }
    // What the plan stands as: its voxels with what hands did laid over
    plan::Voxels target(const plan::Plan& p) const {
        plan::Voxels v = plan::voxels(p, looksFor(p));
        auto it = overrides.find(p.id);
        if (it != overrides.end())
            for (const auto& [k, id] : it->second) {
                glm::ivec3 r = plan::unkey(k);
                v[plan::key(p.origin.x + r.x, p.floorY() + r.y, p.origin.z + r.z)] = id;
            }
        return v;
    }
    // Make it again: only what's different from what stands is written
    void rebuild(plan::Plan& p) {
        std::vector<DoorWant> want = doorsWanted(p);
        dropDoors(p.id, &want);                     // before: a door that moves leaves its old place clear first
        writeDiff(p);
        placeDoors(p, want);                        // after: into the holes just cut
    }
    void writeDiff(const plan::Plan& p) {
        plan::Voxels now = target(p);
        plan::Voxels& was = built[p.id];
        EditBatch b;
        b.source = EditSource::Command;
        b.label = LABEL;
        b.undoable = false;
        size_t n = 0;
        for (const auto& [k, id] : was)
            if (!now.count(k)) { glm::ivec3 v = plan::unkey(k); b.set(v.x, v.y, v.z, BLOCK_AIR); n++; }
        for (const auto& [k, id] : now) {
            auto it = was.find(k);
            if (it != was.end() && it->second == id) continue;
            glm::ivec3 v = plan::unkey(k);
            b.set(v.x, v.y, v.z, id);
            n++;
        }
        was = std::move(now);
        lastWritten = n;
        ++rev;
        if (n) editor.submit(std::move(b));
    }
    // Every plan written out whole again (what stands made to match its
    // plans: an old save, or a world edited outside the game)
    void refreshAll() {
        for (plan::Plan& p : plans) { built[p.id].clear(); rebuild(p); }
    }
    size_t lastWritten = 0;                         // voxels the last rebuild wrote (tests)
    uint64_t revision() const { return rev; }       // changes whenever a plan's made again (the roof renderer)
    uint64_t rev = 1;

    // ── Saving (plans.dat) ────────────────────────────────
    void save(const std::string& path) const {
        std::ofstream f(path);
        f << "plans 3 " << nextId << "\n";
        for (const plan::Plan& p : plans) {
            p.write(f);
            auto it = overrides.find(p.id);
            if (it != overrides.end())
                for (const auto& [k, id] : it->second) {
                    glm::ivec3 r = plan::unkey(k);
                    f << "o " << p.id << " " << r.x << " " << r.y << " " << r.z << " " << id << "\n";
                }
        }
    }
    // Read back; what stands is taken as made (the chunks were saved with it)
    void load(const std::string& path) {
        plans.clear(); overrides.clear(); built.clear(); undo.clear(); nextId = 0; ++rev;
        int version = 1;
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("plans", 0) == 0) { std::istringstream h(line); std::string k; h >> k >> version >> nextId; continue; }
            if (line.rfind("plan ", 0) == 0) {
                plan::Plan p;
                if (plan::Plan::read(f, line, p)) {
                    if (version < 2) for (plan::Wall& w : p.walls) { w.a *= plan::GRID; w.b *= plan::GRID; }   // (walls were in grid points)
                    nextId = std::max(nextId, p.id);
                    plans.push_back(p);
                }
                continue;
            }
            if (line.rfind("o ", 0) == 0) {
                std::istringstream l(line); std::string k; uint32_t id; int x, y, z, b;
                if (l >> k >> id >> x >> y >> z >> b) overrides[id][plan::key(x, y, z)] = (uint16_t)b;
            }
        }
        for (const plan::Plan& p : plans) {
            // (before version 3 plans had no railings of their own: what stands
            // has none, so the next time one's made again they're put up)
            if (version < 3) { plan::Plan q = p; q.rails = false; built[p.id] = target(q); }
            else built[p.id] = target(p);
            for (const plan::Wall& w : p.walls)
                for (const plan::Opening& h : w.openings)
                    if (h.doorId) doors[p.id].push_back(h.doorId);
        }
    }

    std::string defaultPalette = "tudor";

private:
    ChunkManager& world;
    WorldEditor& editor;
    std::vector<plan::Plan> plans;
    std::map<uint32_t, plan::Voxels> built;                                  // what each plan stands as
    std::map<uint32_t, std::unordered_map<int64_t, uint16_t>> overrides;     // hands' edits, in each building's frame
    std::vector<std::vector<plan::Plan>> undo;
    uint32_t nextId = 0;

    void snapshot() {
        undo.push_back(plans);
        if (undo.size() > UNDO_MAX) undo.erase(undo.begin());
    }
    bool planTouches(const plan::Plan& q, glm::ivec2 lo, glm::ivec2 hi) const {
        glm::ivec2 a, b;
        q.bounds(a, b, 0);
        glm::ivec2 o(q.origin.x, q.origin.z);
        glm::ivec2 vlo = lo * plan::GRID - o, vhi = (hi + glm::ivec2(1)) * plan::GRID - o;
        return vlo.x <= b.x && vhi.x >= a.x && vlo.y <= b.y && vhi.y >= a.y;
    }
    static glm::ivec2 local(const plan::Plan& p, glm::ivec2 worldVoxel) { return worldVoxel - glm::ivec2(p.origin.x, p.origin.z); }
    // the plan a wall (or pillar) between world voxel columns goes on: the
    // one whose foundation it's on, else one on the ground it touches,
    // else a new one on the ground
public:
    // the plan under world columns a..b, for building upstairs (none: 0)
    plan::Plan* planUpstairs(glm::ivec2 a, glm::ivec2 b) {
        glm::ivec2 lo = glm::min(a, b), hi = glm::max(a, b);
        for (plan::Plan& q : plans) {
            glm::ivec2 l, h;
            q.bounds(l, h, 2);
            glm::ivec2 la = local(q, lo), lb = local(q, hi);
            if (la.x <= h.x && lb.x >= l.x && la.y <= h.y && lb.y >= l.y) return &q;
        }
        return nullptr;
    }
    // a new wall's own height: none (0) when it's its level's; a level
    // built on for the first time takes it as its height
    static int ownHeight(plan::Plan& p, int level, int height) {
        if (height <= 0) return 0;
        int h = std::clamp(height, 3, plan::MAX_WALL_H);
        bool used = false;
        for (const plan::Wall& w : p.walls) used = used || w.level == level;
        if (!used && level < plan::MAX_LEVELS) {
            if ((int)p.levelH.size() <= level) p.levelH.resize((size_t)level + 1, 0);
            p.levelH[(size_t)level] = h;
        }
        return h == p.levelHeight(level) ? 0 : h;
    }
private:
    plan::Plan* planFor(glm::ivec2 a, glm::ivec2 b, int groundY, const std::string& palette) {
        glm::ivec2 mid = (a + b) / 2;
        for (plan::Plan& q : plans)
            if (q.onFoundation(local(q, mid)) || q.onFoundation(local(q, a)) || q.onFoundation(local(q, b))) return &q;
        for (plan::Plan& q : plans) {
            if (q.hasFoundation()) continue;
            glm::ivec2 lo, hi;
            q.bounds(lo, hi, 0);
            glm::ivec2 la = local(q, glm::min(a, b)), lb = local(q, glm::max(a, b));
            if (la.x <= hi.x && lb.x >= lo.x && la.y <= hi.y && lb.y >= lo.y && std::abs(q.origin.y - groundY) <= 3) return &q;
        }
        plan::Plan np;
        np.id = ++nextId;
        glm::ivec2 lo = glm::min(a, b);
        np.origin = glm::ivec3(gridOf(lo.x) * plan::GRID, groundY, gridOf(lo.y) * plan::GRID);
        np.palette = palette;
        np.rails = railsDefault;
        plans.push_back(np);
        return &plans.back();
    }
    // a new foundation's ground cleared above its floor (hills inside
    // the house), once; not part of the plan
    void clearOver(const plan::Plan& p, const plan::Foundation& f) {
        glm::ivec2 lo = f.vlo(), hi = f.vhi();
        EditBatch b;
        b.source = EditSource::Command;
        b.label = LEVEL_LABEL;
        b.undoable = false;
        int y0 = p.floorY();
        for (int z = lo.y; z <= hi.y; z++)
            for (int x = lo.x; x <= hi.x; x++)
                for (int y = y0; y < y0 + LEVEL_CLEAR; y++) {
                    int wx = p.origin.x + x, wz = p.origin.z + z;
                    if (world.typeAtVoxel(wx, y, wz) != BLOCK_AIR) b.set(wx, y, wz, BLOCK_AIR);
                }
        if (!b.empty()) editor.apply(std::move(b));
    }
    void unbuild(uint32_t id) {
        ++rev;
        dropDoors(id, nullptr);
        auto it = built.find(id);
        if (it == built.end()) return;
        EditBatch b;
        b.source = EditSource::Command; b.label = LABEL; b.undoable = false;
        for (const auto& [k, v] : it->second) { glm::ivec3 p = plan::unkey(k); b.set(p.x, p.y, p.z, BLOCK_AIR); }
        built.erase(it);
        if (!b.empty()) editor.submit(std::move(b));
    }
    void dropIfEmpty(uint32_t id) {
        for (size_t i = 0; i < plans.size(); i++)
            if (plans[i].id == id && plans[i].empty()) {
                unbuild(id);
                overrides.erase(id);
                plans.erase(plans.begin() + (long)i);
                return;
            }
    }

    // ── Doors (door pieces in openings) ───────────────────
    std::map<uint32_t, std::vector<uint32_t>> doors;     // plan → the door pieces it placed
    std::set<std::string> quiet;                          // their edits' labels (not hand edits)
    struct DoorWant { uint16_t type = 0; glm::ivec3 spot{ 0 }; int rot = 0; std::string spec; size_t wall = 0, hole = 0; };
    std::vector<DoorWant> doorsWanted(const plan::Plan& p) const {
        std::vector<DoorWant> out;
        if (!structs) return out;
        const BuildPalette* pal = gBuildPalettes.find(p.palette.empty() ? defaultPalette : p.palette);
        for (size_t wi = 0; wi < p.walls.size(); wi++) {
            const plan::Wall& wl = p.walls[wi];
            if (!wl.straight()) continue;
            for (size_t hi = 0; hi < wl.openings.size(); hi++) {
                const plan::Opening& h = wl.openings[hi];
                if (h.fill != plan::Fill::Door) continue;
                DoorWant d;
                d.type = gStructures.find(h.door.empty() ? defaultDoor : h.door);
                const StructureType* t = gStructures.get(d.type);
                if (!t) continue;
                PieceSpec ps;
                ps.w = h.width; ps.h = h.height;
                if (pal) ps = gStructures.applyPalette(*t, ps, pal->spec);
                d.spec = gStructures.normalize(*t, ps);
                const bool alongX = wl.a.y == wl.b.y;
                d.rot = alongX ? 0 : 1;
                glm::ivec2 u = glm::sign(wl.b - wl.a);
                glm::ivec2 c0 = wl.a + u * h.lo(), c1 = wl.a + u * (h.lo() + h.width - 1);
                glm::ivec2 mn = glm::min(c0, c1);
                const VoxelVolume& sh = gStructures.shape(d.type, d.spec, 0, d.rot);
                glm::ivec3 lo(p.origin.x + mn.x, p.levelY(wl.level) + h.sill, p.origin.z + mn.y);
                if (alongX) lo.z = p.origin.z + wl.a.y - sh.size.z / 2;
                else        lo.x = p.origin.x + wl.a.x - sh.size.x / 2;
                d.spot = lo + sh.anchor;
                d.wall = wi; d.hole = hi;
                out.push_back(d);
            }
        }
        return out;
    }
    static bool sameDoor(const StructureInstance& s, const DoorWant& d) {
        return s.type == d.type && s.origin == d.spot && s.rot == d.rot && s.spec == d.spec;
    }
    // Remove the plan's doors that aren't wanted any more (keep: what
    // is; null: all of them)
    void dropDoors(uint32_t id, const std::vector<DoorWant>* keep) {
        auto it = doors.find(id);
        if (it == doors.end() || !structs) return;
        std::vector<uint32_t> left;
        for (uint32_t d : it->second) {
            const StructureInstance* s = structs->get(d);
            if (!s) continue;
            bool wanted = false;
            if (keep) for (const DoorWant& w : *keep) wanted = wanted || sameDoor(*s, w);
            if (wanted) { left.push_back(d); continue; }
            if (const StructureType* t = gStructures.get(s->type)) quiet.insert("remove " + t->displayName);
            structs->remove(d, EditSource::Command);
        }
        if (left.empty()) doors.erase(it); else it->second = left;
    }
    void placeDoors(plan::Plan& p, const std::vector<DoorWant>& want) {
        for (plan::Wall& w : p.walls) for (plan::Opening& h : w.openings) h.doorId = 0;
        if (!structs) return;
        std::vector<uint32_t>& mine = doors[p.id];
        for (const DoorWant& w : want) {
            uint32_t have = 0;
            for (uint32_t d : mine) if (const StructureInstance* s = structs->get(d)) if (sameDoor(*s, w)) have = d;
            if (!have) {
                if (const StructureType* t = gStructures.get(w.type)) quiet.insert(t->displayName);
                StructureInstance* s = structs->place(w.type, w.spot, w.rot, EditSource::Command, 0, w.spec);
                if (!s) continue;
                have = s->id;
                mine.push_back(have);
            }
            p.walls[w.wall].openings[w.hole].doorId = have;
        }
        if (mine.empty()) doors.erase(p.id);
    }
};
