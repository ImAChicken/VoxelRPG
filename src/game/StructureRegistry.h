#pragma once
#include "ItemRegistry.h"
#include "../voxel/VoxelRegistry.h"
#include "../world/VoxelVolume.h"
#include "../world/StructureCatalog.h"
#include "PieceSpec.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// StructureRegistry — kinds of multi-block structure
// ══════════════════════════════════════════════════════════
// Loaded from data/registry/structures.toml. A structure type
// says what a piece looks like (its voxel shape, per state),
// how it behaves (build piece, door, tree), what it costs to
// build and what it drops.
//
// Placed copies are STRUCTURE INSTANCES (world/Structure
// Manager.h). The voxels in the world stay ordinary blocks;
// the instance remembers which voxels belong to it.
//
// Spawners are structures too: a voxel model that keeps NPCs
// coming while a player is near (sim/Spawners.h). What it
// spawns, how many and how often are in its entry (spawn =).

enum class StructureKind : uint8_t { Build, Door, Tree, Spawner, Chest, Station, Effect };   // Effect: left by a spell (ice), goes after its lifetime

// What a spawner structure spawns (structures.toml: spawn = { … })
struct SpawnDef {
    std::vector<std::pair<std::string, float>> npcs;   // npcs.toml type, weight
    int   maxAlive  = 3;        // alive at once
    int   total     = 0;        // ever (0 = no limit)
    float radius    = 10.0f;    // voxels around it they appear in
    float activate  = 96.0f;    // a player this close wakes it
    float respawn   = 30.0f;    // s after one is gone before the next
    float minPlayer = 12.0f;    // never appear closer than this to a player
    bool  home      = true;     // its NPCs live around it (home, territory)
};

struct StructureType {
    uint16_t      id = 0;
    std::string   name;            // "foundation"
    std::string   displayName;     // "Stone Foundation"
    StructureKind kind = StructureKind::Build;
    bool          inBuildMenu  = false;
    bool          slope = false;   // a roof plane (shape = "slope"): drawn smooth over its
    int           slopeThick = 2;  // voxels (render/RoofRenderer.h); its thickness
    bool          faced = false;   // a wall with an outside face (shape = "wall"): the
                                   // unturned piece's outside faces -z (outsideDir)
    // A made piece (StructureRegistry::make): "wall", "pillar", "door"
    // or "" (a fixed shape). Each placed copy may have its own sizes
    // and blocks (game/PieceSpec.h); 'defaults' is the type's own
    std::string   model, outside, inside, opening, style;
    PieceSpec     defaults;
    bool sizable() const { return !model.empty(); }
    bool          foundation   = false;
    bool          needsSupport = false;
    int           snap = 0;        // grid while following the crosshair
    uint16_t      block = BLOCK_STONE;
    glm::ivec3    size  = {1, 1, 1};

    // How long one lasts once placed, s (0: for good). The world
    // takes it away after that (StructureManager::tick): a spell's
    // ice melts
    float    lifetime  = 0.0f;

    // Trees
    float    health    = 0.0f;
    uint16_t dropItem  = 0;
    int      dropCount = 0;

    // Survival cost: (item, count) pairs. Only charged while the
    // registry's costsEnabled is on (structures.toml: costs = true)
    std::vector<std::pair<uint16_t, int>> cost;

    // ── Part tags (building, blueprints, villages) ────────
    //   part      what the piece is: foundation, wall, wall_door,
    //             wall_window, gable, floor, roof, pillar, beam,
    //             stairs, door, station, light, furniture …
    //   group     the part's broad group: the part up to the first
    //             '_' (wall_door → wall). Swaps work per group
    //   material  what it's made of: stone, slate, plank, brick,
    //             thatch, tile …
    //   family    the piece regardless of material (wall_4,
    //             foundation_8): two types in one family are the
    //             same shape in different materials, so one can
    //             stand in for the other (StructureRegistry::swap)
    //   tags      anything else, for generators to filter on
    std::string part, group, material, family;
    std::vector<std::string> tags;
    bool hasTag(const std::string& tg) const {
        return std::find(tags.begin(), tags.end(), tg) != tags.end();
    }

    // Stations (kind "station"): E opens their menu
    //   station   what it is (workbench, campfire, hearth, forge …)
    //   title     the menu's heading
    std::string station, title;

    // Spawners
    SpawnDef spawn;

    // Storage (world/Chests.h, game/Container.h): how many slots
    // it holds (0: none; a chest: 50 unless set; any kind may
    // set it), and a loot table rolled the first time one is
    // opened (one built with the hammer starts empty instead)
    int slots = 0;
    std::string loot;

    // Shape per state (doors: 0 closed, 1 open; others: 0),
    // and per rotation (quarter turns 0..3). Built on load.
    std::array<std::array<std::shared_ptr<const VoxelVolume>, 4>, 2> shapes;
    int states = 1;

    const VoxelVolume& shape(int state, int rot) const {
        return *shapes[state % states][rot & 3];
    }

    // Size in feet, for menus: "4 x 4 x 4 ft"
    std::string feet() const {
        auto f = [](int v) {
            char b[16];
            if (v % 3 == 0) std::snprintf(b, sizeof(b), "%d", v / 3);
            else            std::snprintf(b, sizeof(b), "%.1f", v / 3.0f);
            return std::string(b);
        };
        return f(size.x) + " x " + f(size.y) + " x " + f(size.z) + " ft";
    }

    std::string costText(bool charged = true) const {
        if (cost.empty() || !charged) return "free";
        std::string t;
        for (auto& [item, n] : cost) {
            if (!t.empty()) t += ", ";
            t += std::to_string(n) + " " + gItems.get(item).displayName;
        }
        return t;
    }
};

class StructureRegistry {
public:
    // ══ Made pieces: walls, pillars, doors (game/PieceSpec.h) ══
    // A type with a model is drawn by a generator from a PieceSpec,
    // so each placed copy may have its own sizes and blocks. The
    // registry's own shapes (shape(state, rot)) are the type's
    // defaults; shape(type, spec, state, rot) gives any other spec's,
    // made once and kept.

    // Sizes a piece may take (voxels: 3 to a foot)
    static constexpr int DOOR_MIN_W = 9, DOOR_MIN_H = 21;     // 3 x 7 ft: a person walks through
    static constexpr int WALL_MAX_W = 48, WALL_MAX_H = 60;    // 16 ft, 20 ft
    static constexpr int PILLAR_MAX_H = 90;

    // A block a palette may use for a role: solid building stuff, not
    // a plant, liquid, chest or station top
    static bool paletteBlock(uint16_t id) {
        if (!id || id >= gRegistry.size()) return false;
        const VoxelTypeDef& d = gRegistry.get(id);
        if (!d.isSolid() || d.isPlant() || d.sprite || d.material == "fluid") return false;
        if (d.name.rfind("chest", 0) == 0 || d.name == "workbench_top") return false;
        return true;
    }

    // The type's defaults with 'over' laid on top, every size set and
    // kept to what works:
    //   walls    width 3 .. 48 and height 9 .. 60, in feet (3s);
    //            a doorway at least 9 x 21 (3 x 7 ft) with a 3 high
    //            lintel over it and a post each side; a window at
    //            least 3 x 3, 3 off the floor, 2 under the top
    //   doors    9 .. 24 wide, 21 .. 45 high
    //   pillars  2 .. 6 thick, 3 .. 90 high
    // Blocks a palette can't use fall back to the type's.
    PieceSpec resolve(const StructureType& t, const PieceSpec& over) const {
        PieceSpec p = PieceSpec::over(t.defaults, over);
        for (auto it = p.blocks.begin(); it != p.blocks.end();) {
            bool ok = it->second == "none" ? it->first == "glass" : paletteBlock(gRegistry.getIDByName(it->second));
            if (!ok) {
                auto d = t.defaults.blocks.find(it->first);
                if (d != t.defaults.blocks.end()) { it->second = d->second; ++it; }
                else it = p.blocks.erase(it);
            } else ++it;
        }
        auto known = [](const std::vector<std::string>& l, const std::string& v) { return std::find(l.begin(), l.end(), v) != l.end(); };
        if (t.model == "wall") {
            if (p.outside.empty() || !known(wallOutsides(), p.outside)) p.outside = t.outside;
            if (p.inside.empty() || !known(wallInsides(), p.inside)) p.inside = t.inside;
        } else p.outside.clear(), p.inside.clear();
        auto feet = [](int v) { return (v + 1) / 3 * 3; };      // the nearest foot
        auto up = [](int v) { return (v + 2) / 3 * 3; };        // the next whole foot
        if (t.model == "wall") {
            p.w = std::clamp(feet(p.w), 3, WALL_MAX_W);
            p.h = std::clamp(feet(p.h), 9, WALL_MAX_H);
            if (t.opening == "door") {
                p.openH = std::clamp(p.openH, DOOR_MIN_H, std::max(DOOR_MIN_H, WALL_MAX_H - 3));
                p.h = std::max(p.h, up(p.openH + 3));
                p.openW = std::max(p.openW, DOOR_MIN_W);
                p.w = std::max(p.w, up(p.openW + 2));
                p.w = std::min(p.w, WALL_MAX_W);
                p.openW = std::min(p.openW, p.w - 2);
                p.openH = std::min(p.openH, p.h - 3);
            } else if (t.opening == "window") {
                p.w = std::max(p.w, 6);
                p.openW = std::clamp(p.openW, 3, p.w - 2);
                p.openH = std::clamp(p.openH, 3, p.h - 6);
                p.sill = std::clamp(p.sill < 0 ? 12 : p.sill, 3, p.h - 2 - p.openH);
            } else { p.openW = p.openH = 0; p.sill = -1; }
        } else if (t.model == "door") {
            p.w = std::clamp(p.w, DOOR_MIN_W, 24);
            p.h = std::clamp(p.h, DOOR_MIN_H, 45);
            p.openW = p.openH = 0; p.sill = -1;
        } else if (t.model == "foundation") {
            // any size in whole feet (Eddie): 1 to 20 ft square, 1 to 8 ft deep
            p.w = std::clamp(feet(p.w), 3, 60);
            p.d = std::clamp(feet(p.d), 3, 60);
            p.h = std::clamp(feet(p.h), 3, 24);
            p.openW = p.openH = 0; p.sill = -1;
        } else if (t.model == "pillar") {
            p.w = std::clamp(p.w, 2, 6);
            p.h = std::clamp(p.h, 3, PILLAR_MAX_H);
            p.openW = p.openH = 0; p.sill = -1;
        }
        return p;
    }

    // The spec as placed pieces keep it: only what differs from the
    // type ("" for the type exactly as it is)
    std::string normalize(const StructureType& t, const PieceSpec& over) const {
        if (t.model.empty() || over.empty()) return "";
        PieceSpec full = resolve(t, over), def = resolve(t, PieceSpec());
        PieceSpec d;
        if (full.w != def.w) d.w = full.w;
        if (full.h != def.h) d.h = full.h;
        if (full.d != def.d) d.d = full.d;
        if (full.openW != def.openW) d.openW = full.openW;
        if (full.openH != def.openH) d.openH = full.openH;
        if (full.sill != def.sill) d.sill = full.sill;
        if (full.outside != def.outside) d.outside = full.outside;
        if (full.inside != def.inside) d.inside = full.inside;
        for (auto& [k, v] : full.blocks) {
            auto it = def.blocks.find(k);
            if (it == def.blocks.end() || it->second != v) d.blocks[k] = v;
        }
        return d.text();
    }

    // The shape a placed piece with this spec has (normalized text;
    // "" is the type's own). Made the first time it's asked for
    const VoxelVolume& shape(uint16_t type, const std::string& spec, int state, int rot) const {
        const StructureType& t = types[type < types.size() ? type : 0];
        if (spec.empty() || t.model.empty() || !t.shapes[0][0]) return t.shape(state, rot);
        std::lock_guard<std::mutex> g(cacheLock);
        std::string key = std::to_string(type) + "|" + spec;
        auto it = madeShapes.find(key);
        if (it == madeShapes.end()) {
            Shapes sh;
            std::vector<VoxelVolume> base = make(t, resolve(t, PieceSpec::parse(spec)));
            sh.states = (int)base.size();
            for (int st = 0; st < sh.states; st++)
                for (int r = 0; r < 4; r++) sh.v[st][r] = std::make_shared<const VoxelVolume>(base[st].rotatedY(r));
            if (madeShapes.size() > 4096) madeShapes.clear();   // (never in a real world: a guard)
            it = madeShapes.emplace(key, std::move(sh)).first;
        }
        return *it->second.v[state % it->second.states][rot & 3];
    }

    // What one costs with this spec: the type's cost, by its area
    // against the type's own (rounded up; at least 1 of each)
    std::vector<std::pair<uint16_t, int>> costOf(const StructureType& t, const std::string& spec) const {
        if (spec.empty() || t.model.empty()) return t.cost;
        PieceSpec def = resolve(t, PieceSpec()), p = resolve(t, PieceSpec::parse(spec));
        auto size = [](const PieceSpec& s) { return (double)std::max(1, s.w) * std::max(1, s.h) * std::max(1, s.d); };
        double k = size(p) / size(def);
        std::vector<std::pair<uint16_t, int>> out = t.cost;
        for (auto& [item, n] : out) n = std::max(1, (int)std::ceil(n * k - 1e-6));
        return out;
    }

    // Does this kind of piece have this palette role (PieceRole::models)?
    static bool usesRole(const StructureType& t, const std::string& role) {
        char m = t.model == "wall" ? 'w' : t.model == "pillar" ? 'p' : t.model == "door" ? 'd' : 0;
        if (!m) return false;
        for (const PieceRole& r : pieceRoles()) if (role == r.id) return std::strchr(r.models, m) != nullptr;
        return false;
    }

    // ── Palettes (game/Palettes.h) ────────────────────────
    // A palette is a block per role and a wall finish. Laid on a
    // piece: the roles its kind has; the finish only on a wall that
    // has none of its own (a plain wall: the timber or inlay walls
    // keep theirs, in the palette's blocks)
    static bool takesFinish(const StructureType& t) {
        return t.model == "wall" && t.outside == "plain" && t.inside == "plain";
    }
    PieceSpec applyPalette(const StructureType& t, PieceSpec own, const PieceSpec& pal) const {
        if (!t.sizable()) return own;
        for (auto& [role, block] : pal.blocks)
            if (usesRole(t, role)) own.blocks[role] = block;
        if (takesFinish(t)) {
            if (!pal.outside.empty()) own.outside = pal.outside;
            if (!pal.inside.empty()) own.inside = pal.inside;
        }
        return own;
    }
    // What a placed piece shows of a palette: every role its kind has
    // (as it really is, defaults too) and its finish if it takes one.
    // The Copy tool gathers these into a blueprint's palettes
    PieceSpec paletteOf(const StructureType& t, const std::string& spec) const {
        PieceSpec out;
        if (!t.sizable()) return out;
        PieceSpec full = resolve(t, PieceSpec::parse(spec));
        for (auto& [role, block] : full.blocks) if (usesRole(t, role)) out.blocks[role] = block;
        if (takesFinish(t)) { out.outside = full.outside; out.inside = full.inside; }
        return out;
    }
    // A placed piece's spec without its palette: sizes, and a finish
    // that isn't a palette's to give
    PieceSpec shapeOnly(const StructureType& t, const std::string& spec) const {
        PieceSpec p = PieceSpec::parse(spec);
        p.blocks.clear();
        if (takesFinish(t)) { p.outside.clear(); p.inside.clear(); }
        return p;
    }

    // Has this piece an outside and an inside (so it's turned to face out)?
    bool faced(const StructureType& t, const std::string& spec) const {
        if (t.model != "wall") return false;
        if (spec.empty()) return t.faced;
        PieceSpec p = resolve(t, PieceSpec::parse(spec));
        return p.outside != p.inside || p.outside != "plain";
    }

    // The voxel volumes (unturned, per state) for a full spec
    static std::vector<VoxelVolume> make(const StructureType& t, const PieceSpec& p) {
        auto id = [&](const std::string& role, uint16_t def) {
            std::string n = p.block(role);
            if (n == "none") return (uint16_t)BLOCK_AIR;
            uint16_t b = n.empty() ? 0 : gRegistry.getIDByName(n);
            return b ? b : def;
        };
        std::vector<VoxelVolume> out;
        if (t.model == "wall") {
            VoxelVolume v = wallModel(p.outside.empty() ? t.outside : p.outside, p.inside.empty() ? t.inside : p.inside,
                                      t.opening, p, t.size.z, id);
            v.anchor = { 0, 0, 0 };
            out.push_back(std::move(v));
        } else if (t.model == "pillar") {
            out.push_back(pillarModel(t.style, p, id));
        } else if (t.model == "foundation") {
            VoxelVolume v({ std::max(1, p.w), std::max(1, p.h), std::max(1, p.d) });
            std::fill(v.data.begin(), v.data.end(), id("fill", BLOCK_STONE));
            v.anchor = { 0, 0, 0 };
            out.push_back(std::move(v));
        } else if (t.model == "door") {
            VoxelVolume closed = doorModel(t.style, p, id);
            VoxelVolume open({ 1, closed.size.y, closed.size.x });
            for (int y = 0; y < closed.size.y; y++)
                for (int x = 0; x < closed.size.x; x++) open.set(0, y, x, closed.at(x, y, 0));
            closed.anchor = open.anchor = { 0, 0, 0 };
            out.push_back(std::move(closed));
            out.push_back(std::move(open));
        }
        return out;
    }

    // ── Walls ─────────────────────────────────────────────
    // In layers through its thickness (Eddie: exterior walls 3
    // voxels — an inside face, a core, an outside face):
    //   z = 0       the OUTSIDE face: outside = "timber" (dark posts at
    //               the ends and every 4 ft, sill, mid rail, top plate,
    //               braces in the lower bays, the wall between),
    //               "plinth" (a stone base under a rail), "inlay" (a
    //               band of diamonds at waist height between rails),
    //               "frame" (posts at the ends, sill, top) or "plain"
    //   middle      the wall's own material (the core)
    //   z = last    the INSIDE face: inside = "paint", "panel"
    //               (boards to waist height under a rail, the paint
    //               above, trim at the top) or "plain"
    //   opening     "door": a hole through it, ow x oh, centred, on
    //               the floor (a door piece fits it); "window": ow x
    //               oh, sill up, a cross outside, the glass set back
    //               in the core. Framed on any face that isn't plain
    // Roles: fill (the wall), post, beam, base, inlay, paint, panel,
    // glass. The unturned piece faces its outside to -z; the hammer
    // turns it so the outside faces away from the floor
    // (StructureManager::snapTo). Two thick: no core; one: outside.
    template <class Id>
    static VoxelVolume wallModel(const std::string& outside, const std::string& inside, const std::string& opening,
                                 const PieceSpec& p, int depth, Id id) {
        const uint16_t fill = id("fill", BLOCK_STONE);
        const uint16_t post = id("post", fill), beam = id("beam", fill), base = id("base", fill);
        const uint16_t panel = id("panel", fill), paint = id("paint", fill), inlay = id("inlay", fill);
        const uint16_t glass = id("glass", BLOCK_AIR);
        const int W = std::max(1, p.w), H = std::max(1, p.h), D = std::max(1, depth);
        const int OUT = 0, IN = D - 1;
        VoxelVolume v({ W, H, D });
        std::fill(v.data.begin(), v.data.end(), fill);
        auto put = [&](int x, int y, int z, uint16_t b) {
            if (x >= 0 && y >= 0 && z >= 0 && x < W && y < H && z < D) v.set(x, y, z, b);
        };
        auto row = [&](int z, int y, uint16_t b) { for (int x = 0; x < W; x++) put(x, y, z, b); };
        auto col = [&](int z, int x, int y0, int y1, uint16_t b) { for (int y = y0; y < y1; y++) put(x, y, z, b); };

        // ── the outside face ──
        if (outside == "timber") {
            const int mid = H / 2 - 1;
            std::vector<int> posts = { 0 };                       // the ends and every 4 ft, 2 wide
            for (int x = 12; x < W - 2; x += 12) posts.push_back(x);
            if (W >= 4) posts.push_back(W - 2);
            for (size_t b = 0; b + 1 < posts.size(); b++) {       // braces first: rails and posts go over their ends
                int x0 = posts[b] + 2, x1 = posts[b + 1] - 1, y0 = 2, y1 = mid - 1;
                if (x1 - x0 < 3 || y1 - y0 < 3) continue;
                for (int x = x0; x <= x1; x++) {
                    float f = (float)(x - x0) / (x1 - x0);
                    if (b % 2) f = 1.0f - f;
                    int y = y0 + (int)std::lround(f * (y1 - y0));
                    put(x, y, OUT, beam); put(x, y + 1, OUT, beam);
                }
            }
            for (int y : { 0, 1, mid, mid + 1, H - 2, H - 1 }) row(OUT, y, beam);
            for (int px : posts) { col(OUT, px, 0, H, post); col(OUT, px + 1, 0, H, post); }
        } else if (outside == "plinth") {
            const int top = std::min(H - 2, 8);
            for (int z = 0; z < std::max(1, D - 1); z++)            // the base is solid stone through to the inside face
                for (int y = 0; y < top; y++) row(z, y, base);
            row(OUT, top, beam);
            row(OUT, H - 1, beam);
        } else if (outside == "inlay") {
            // a band at waist height: rails, and diamonds of the inlay block
            const int lo = H >= 18 ? 8 : 2, hi = lo + 7;
            if (hi < H - 1) {
                row(OUT, lo, beam); row(OUT, hi, beam);
                for (int y = lo + 1; y < hi; y++)
                    for (int x = 0; x < W; x++) {
                        int cy = (lo + hi) / 2, dx = std::abs(((x + 3) % 6) - 3), dy = std::abs(y - cy);
                        if (dx + dy <= 2) put(x, y, OUT, inlay);
                    }
            }
            row(OUT, 0, beam); row(OUT, H - 1, beam);
            col(OUT, 0, 0, H, post); col(OUT, W - 1, 0, H, post);
        } else if (outside == "frame") {
            row(OUT, 0, beam); row(OUT, H - 1, beam);
            col(OUT, 0, 0, H, post); col(OUT, W - 1, 0, H, post);
        }

        // ── the inside face ──
        if (D >= 2) {
            if (inside == "paint" || inside == "panel") for (int y = 0; y < H; y++) row(IN, y, paint);
            if (inside == "panel") {
                const int rail = std::min(H - 2, 10);
                for (int y = 0; y < rail; y++) row(IN, y, panel);
                row(IN, rail, beam);
                row(IN, H - 1, beam);
            }
        }

        // ── the opening, framed ──
        if ((opening == "door" || opening == "window") && p.openW > 0 && p.openH > 0) {
            const bool door = opening == "door";
            const int hw = p.openW, hh = p.openH;
            const int hx = (W - hw) / 2, hy = door ? 0 : std::max(0, p.sill);
            for (int z : { OUT, IN }) {
                if (z == IN && D < 2) break;
                if ((z == OUT ? outside : inside) == "plain") continue;
                col(z, hx - 1, hy, hy + hh + 1, post); col(z, hx + hw, hy, hy + hh + 1, post);
                for (int x = hx - 1; x <= hx + hw; x++) {
                    put(x, hy + hh, z, beam);
                    if (door && z == OUT) put(x, hy + hh + 1, z, beam);    // the lintel
                    if (!door) put(x, hy - 1, z, beam);                    // the sill
                }
            }
            for (int z = 0; z < D; z++)
                for (int y = hy; y < hy + hh; y++)
                    for (int x = hx; x < hx + hw; x++) {
                        bool core = z > 0 && z < D - 1;
                        put(x, y, z, !door && core ? glass : BLOCK_AIR);  // panes in the core, set back
                    }
            if (!door && outside != "plain") {
                // a cross outside: four panes
                col(OUT, hx + hw / 2, hy, hy + hh, post);
                if (hw % 2 == 0 && hw >= 8) col(OUT, hx + hw / 2 - 1, hy, hy + hh, post);
                for (int x = hx; x < hx + hw; x++) put(x, hy + hh / 2, OUT, beam);
            }
            if (!door && D == 2 && glass) for (int y = hy; y < hy + hh; y++) for (int x = hx; x < hx + hw; x++)
                if (v.at(x, y, IN) == BLOCK_AIR) put(x, y, IN, glass);   // too thin for a core: panes inside
        }
        return v;
    }

    // ── Pillars ───────────────────────────────────────────
    // w x h x w, by the corner. style "plain": all post; "based": a
    // foot of the base block (1 ft), a cap of the beam block on top
    template <class Id>
    static VoxelVolume pillarModel(const std::string& style, const PieceSpec& p, Id id) {
        const int W = std::max(1, p.w), H = std::max(1, p.h);
        const uint16_t post = id("post", BLOCK_STONE);
        VoxelVolume v({ W, H, W });
        std::fill(v.data.begin(), v.data.end(), post);
        if (style == "based" && H >= 9) {
            const uint16_t base = id("base", post), cap = id("beam", post);
            for (int z = 0; z < W; z++) for (int x = 0; x < W; x++) {
                for (int y = 0; y < 3; y++) v.set(x, y, z, base);
                v.set(x, H - 1, z, cap);
                v.set(x, H - 2, z, cap);
            }
        }
        v.anchor = { 0, 0, 0 };
        return v;
    }

    // ── Doors ─────────────────────────────────────────────
    // A panel w x h x 1, from the hinge corner. style "plain": all
    // boards; "ledged": boards with three ledges and a brace between
    // them (the beam block), "framed": a frame round the edge and a
    // middle rail
    template <class Id>
    static VoxelVolume doorModel(const std::string& style, const PieceSpec& p, Id id) {
        const int W = std::max(1, p.w), H = std::max(1, p.h);
        const uint16_t boards = id("door", BLOCK_STONE), ledge = id("beam", boards);
        VoxelVolume v({ W, H, 1 });
        std::fill(v.data.begin(), v.data.end(), boards);
        auto row = [&](int y) { for (int x = 0; x < W; x++) v.set(x, y, 0, ledge); };
        if (style == "ledged" && W >= 4 && H >= 12) {
            int lo = 2, mid = H / 2, hi = H - 4;
            for (int y : { lo, mid, hi }) row(y);
            for (auto [a, b] : { std::pair<int, int>{ lo, mid }, { mid, hi } }) {   // a brace, hinge-side low
                for (int x = 0; x < W; x++) {
                    int y = a + 1 + (int)std::lround((double)(W - 1 - x) / std::max(1, W - 1) * (b - a - 2));
                    if (y > a && y < b) v.set(x, y, 0, ledge);
                }
            }
        } else if (style == "framed" && W >= 4 && H >= 6) {
            for (int x = 0; x < W; x++) { v.set(x, 0, 0, ledge); v.set(x, H - 1, 0, ledge); v.set(x, H / 2, 0, ledge); }
            for (int y = 0; y < H; y++) { v.set(0, y, 0, ledge); v.set(W - 1, y, 0, ledge); }
        }
        v.anchor = { 0, 0, 0 };
        return v;
    }

    bool load(const char* path) {
        types.clear();
        index.clear();
        { std::lock_guard<std::mutex> g(cacheLock); madeShapes.clear(); }
        types.emplace_back();            // id 0 = none
        try {
            auto tbl = toml::parse_file(path);
            auto* arr = tbl["structure"].as_array();
            if (!arr) return false;
            costsEnabled = tbl["costs"].value_or(false);
            for (auto& e : *arr) {
                auto* t = e.as_table();
                if (!t) continue;
                // materials = { plank = "planks", stone = "cobblestone" }:
                // one type per material, named by putting the material
                // where {m} is in the name ({M} in the display: capitalised)
                std::vector<std::pair<std::string, std::string>> mats;
                if (auto* mt = (*t)["materials"].as_table())
                    for (auto& [k, v] : *mt) mats.push_back({ std::string(k.str()), v.value_or(std::string()) });
                if (mats.empty()) mats.push_back({ (*t)["material"].value_or(std::string()), std::string() });
                for (auto& [matKey, matBlock] : mats) {
                StructureType s;
                s.id          = (uint16_t)types.size();
                std::string nameT = (*t)["name"].value_or(std::string("structure"));
                s.name        = fillMaterial(nameT, matKey, false);
                s.displayName = fillMaterial((*t)["display"].value_or(nameT), matKey, true);
                s.material    = matKey;
                s.part        = (*t)["part"].value_or(std::string());
                s.group       = s.part.substr(0, s.part.find('_'));
                s.family      = (*t)["family"].value_or(familyOf(nameT));
                if (auto* tg = (*t)["tags"].as_array())
                    for (auto& x : *tg) s.tags.push_back(x.value_or(std::string()));
                s.station     = (*t)["station"].value_or(std::string());
                s.title       = (*t)["title"].value_or(s.displayName);
                std::string kind = (*t)["kind"].value_or(std::string("build"));
                s.kind = kind == "door"    ? StructureKind::Door
                       : kind == "tree"    ? StructureKind::Tree
                       : kind == "spawner" ? StructureKind::Spawner
                       : kind == "chest"   ? StructureKind::Chest
                       : kind == "station" ? StructureKind::Station
                       : kind == "effect"  ? StructureKind::Effect
                                           : StructureKind::Build;
                s.lifetime = (float)(*t)["lifetime"].value_or(0.0);
                s.loot  = (*t)["loot"].value_or(std::string());
                s.slots = std::clamp((int)(*t)["slots"].value_or((int64_t)(s.kind == StructureKind::Chest || !s.loot.empty() ? 50 : 0)), 0, 50);
                if (auto* sp = (*t)["spawn"].as_table()) {
                    SpawnDef& d = s.spawn;
                    if (auto* n = (*sp)["npcs"].as_table())
                        for (auto& [k, v] : *n) d.npcs.push_back({ std::string(k.str()), (float)v.value_or(1.0) });
                    d.maxAlive  = std::max(1, (int)(*sp)["max_alive"].value_or((int64_t)d.maxAlive));
                    d.total     = std::max(0, (int)(*sp)["total"].value_or((int64_t)d.total));
                    d.radius    = (float)(*sp)["radius"].value_or((double)d.radius);
                    d.activate  = (float)(*sp)["activate"].value_or((double)d.activate);
                    d.respawn   = (float)(*sp)["respawn"].value_or((double)d.respawn);
                    d.minPlayer = (float)(*sp)["min_player_distance"].value_or((double)d.minPlayer);
                    d.home      = (*sp)["home"].value_or(d.home);
                }
                if (s.kind == StructureKind::Spawner && s.spawn.npcs.empty())
                    std::cerr << "structures.toml: spawner " << s.name << " has no spawn.npcs\n";
                s.inBuildMenu  = (*t)["menu"].value_or(false);
                s.foundation   = (*t)["foundation"].value_or(false);
                s.needsSupport = (*t)["needs_support"].value_or(false);
                s.snap         = (int)(*t)["snap"].value_or(0);
                std::string blk = matBlock.empty() ? (*t)["block"].value_or(std::string("stone")) : matBlock;
                s.block = gRegistry.getIDByName(blk);
                if (!s.block) s.block = BLOCK_STONE;
                if (auto* sz = (*t)["size"].as_array(); sz && sz->size() == 3)
                    s.size = { (int)(*sz)[0].value_or(1), (int)(*sz)[1].value_or(1),
                               (int)(*sz)[2].value_or(1) };
                s.health    = (float)(*t)["health"].value_or(0.0);
                s.dropItem  = gItems.find((*t)["drop"].value_or(std::string()));
                s.dropCount = (int)(*t)["drop_count"].value_or(0);
                if (auto* c = (*t)["cost"].as_table())
                    for (auto& [k, v] : *c)
                        if (uint16_t item = gItems.find(std::string(k.str())))
                            s.cost.push_back({ item, (int)v.value_or(1) });

                // ── Made pieces (walls, pillars, doors) ───
                readModel(s, *t);

                // ── Shapes ────────────────────────────────
                std::vector<VoxelVolume> base;
                if (s.kind == StructureKind::Tree) {
                    std::string gen = (*t)["generator"].value_or(std::string("oak"));
                    ChunkPtr c = gen == "pine"    ? generatePineTree()
                               : gen == "dead"    ? generateDeadTree()
                               : gen == "ancient" ? generateAncientOak()
                                                  : generateOakTree();
                    base.push_back(volumeFromStructureChunk(*c));
                    s.size = base[0].size;
                } else if ((*t)["layers"].as_array()) {
                    // a drawn model: legend + layers of rows
                    VoxelVolume v;
                    std::string why = modelFrom(*t, v, s.block);
                    if (!why.empty()) {
                        std::cerr << "structures.toml: " << s.name << ": " << why << "\n";
                        v = VoxelVolume({ 1, 1, 1 });
                        v.set(0, 0, 0, s.block);
                    }
                    s.size = v.size;
                    if (s.kind == StructureKind::Build) v.anchor = { 0, 0, 0 };   // build pieces by the corner
                    if (auto* an = (*t)["anchor"].as_array(); an && an->size() == 3)
                        v.anchor = { (int)(*an)[0].value_or(0), (int)(*an)[1].value_or(0), (int)(*an)[2].value_or(0) };
                    base.push_back(v);
                } else if (!s.model.empty()) {
                    base = make(s, resolve(s, PieceSpec()));
                } else if (std::string shp = (*t)["shape"].value_or(std::string()); shp == "slope" || shp == "gable" || shp == "stairs") {
                    // slope: a roof plane rising along z, 'thickness' thick
                    //        (size = [width, rise, run])
                    // gable: the triangle that closes a roof's end
                    //        (size = [span, height, thickness]); it
                    //        stops 'inset' short of the roof planes
                    //        (default the roof's 2) so they never overlap
                    // stairs: solid steps rising along z, 'step' high
                    //        (size = [width, rise, run])
                    int th = std::max(1, (int)(*t)["thickness"].value_or((int64_t)2));
                    if (shp == "slope") { s.slope = true; s.slopeThick = th; }
                    VoxelVolume v(s.size);
                    uint16_t edge = gRegistry.getIDByName((*t)["edge"].value_or(std::string()));
                    for (int z = 0; z < s.size.z; z++)
                    for (int x = 0; x < s.size.x; x++)
                    for (int y = 0; y < s.size.y; y++) {
                        bool in;
                        if (shp == "slope") {
                            float top = (z + 1) * (float)s.size.y / s.size.z;   // surface over this row
                            in = y < top && y >= top - th - 0.001f;
                        } else if (shp == "stairs") {
                            int step = std::max(1, (int)(*t)["step"].value_or((int64_t)3));
                            int stepsN = std::max(1, s.size.y / step);
                            int k = std::min(stepsN - 1, z * stepsN / s.size.z);
                            in = y < (k + 1) * step;
                        } else {
                            int inset = (int)(*t)["inset"].value_or((int64_t)2);
                            // distance in from the nearer end, in voxels; the roof
                            // plane over it is that high (45°, scaled by height/half)
                            float half = s.size.x * 0.5f;
                            float in_ = std::min(x + 1.0f, (float)s.size.x - x);
                            float h = in_ * (float)s.size.y / half;
                            in = y < h - inset;
                        }
                        if (in) v.set(x, y, z, (edge && shp == "slope" && (x == 0 || x == s.size.x - 1)) ? edge : s.block);
                    }
                    v.anchor = { 0, 0, 0 };
                    base.push_back(v);
                } else {
                    VoxelVolume v(s.size);
                    std::fill(v.data.begin(), v.data.end(), s.block);
                    if (auto* h = (*t)["hole"].as_array(); h && h->size() == 6) {
                        int hx = (int)(*h)[0].value_or(0), hy = (int)(*h)[1].value_or(0);
                        int hz = (int)(*h)[2].value_or(0), hw = (int)(*h)[3].value_or(0);
                        int hh = (int)(*h)[4].value_or(0), hd = (int)(*h)[5].value_or(0);
                        for (int y = hy; y < hy + hh; y++)
                        for (int z = hz; z < hz + hd; z++)
                        for (int x = hx; x < hx + hw; x++)
                            v.set(x, y, z, BLOCK_AIR);
                    }
                    // Build pieces are placed by the corner, so
                    // they line up on the grid and with each other
                    v.anchor = { 0, 0, 0 };
                    base.push_back(v);
                }
                s.states = (int)base.size();
                for (int st = 0; st < s.states; st++)
                    for (int r = 0; r < 4; r++)
                        s.shapes[st][r] = std::make_shared<const VoxelVolume>(
                            base[st].rotatedY(r));

                if (index.count(s.name)) std::cerr << "structures.toml: " << s.name << " defined twice\n";
                index[s.name] = s.id;
                types.push_back(std::move(s));
                }   // each material
            }
            std::cout << "Structure registry: " << types.size() - 1
                      << " types from " << path << "\n";
            return true;
        } catch (const toml::parse_error& e) {
            std::cerr << "structures.toml parse error: " << e << "\n";
            return false;
        }
    }

    // A tree type for each variant of each decorations.toml tree,
    // named like the generator names them (<name>_<variant>), so
    // they can be chopped and felled like the built-in trees. Call
    // after load() and after the decorations are loaded.
    void addDecorTrees(const decor::Set& set) {
        uint16_t log = gItems.find("log_y");
        for (const decor::Def& d : set.defs) {
            if (d.layer != decor::Layer::Tree) continue;
            for (size_t v = 0; v < d.variants.size(); v++) {
                const decor::Shape& sh = d.variants[v];
                glm::ivec3 lo(1 << 20), hi(-(1 << 20));
                int wood = 0;
                for (const decor::Vox& q : sh.v) {
                    lo = glm::min(lo, glm::ivec3(q.dx, q.dy, q.dz));
                    hi = glm::max(hi, glm::ivec3(q.dx, q.dy, q.dz));
                    if (gRegistry.get(q.id).material == "wood") wood++;
                }
                if (sh.v.empty()) continue;
                VoxelVolume vol(hi - lo + glm::ivec3(1));
                for (const decor::Vox& q : sh.v) vol.set(q.dx - lo.x, q.dy - lo.y, q.dz - lo.z, q.id);
                vol.anchor = -lo;
                StructureType st;
                st.id          = (uint16_t)types.size();
                st.name        = d.name + "_" + std::to_string(v);
                st.displayName = d.display;
                st.kind        = StructureKind::Tree;
                st.size        = vol.size;
                st.health      = std::clamp(wood / 8.0f, 30.0f, 160.0f);
                st.dropItem    = log;
                st.dropCount   = std::clamp(wood / 12, 8, 96);
                for (int r = 0; r < 4; r++) st.shapes[0][r] = std::make_shared<const VoxelVolume>(vol.rotatedY(r));
                st.states = 1;
                index[st.name] = st.id;
                types.push_back(std::move(st));
            }
        }
    }

    // A model drawn in the entry:
    //   legend = { c = "cobblestone", f = "fire" }   ('.' and ' ' are air)
    //   layers = [ ["c.c", ...], { rows = ["..d"], repeat = 8 }, ... ]
    // bottom layer first; in each layer the rows run along z, the
    // characters along x. Rows may be shorter than the widest.
    // Returns "" or what's wrong.
    //   legend value "$": the entry's block (its material, with materials = …)
    static std::string modelFrom(const toml::table& t, VoxelVolume& out, uint16_t material = BLOCK_STONE) {
        std::unordered_map<char, uint16_t> legend;
        if (auto* lg = t["legend"].as_table())
            for (auto& [k, v] : *lg) {
                std::string key(k.str());
                std::string blk = v.value_or(std::string());
                uint16_t id = blk == "$" ? material : gRegistry.getIDByName(blk);
                if (key.size() != 1) return "legend keys must be one character ('" + key + "')";
                if (!id) return "unknown block '" + blk + "' in the legend";
                legend[key[0]] = id;
            }
        std::vector<std::vector<std::string>> layers;
        for (auto& node : *t["layers"].as_array()) {
            std::vector<std::string> rows;
            int repeat = 1;
            const toml::array* ra = node.as_array();
            if (auto* lt = node.as_table()) { ra = (*lt)["rows"].as_array(); repeat = std::max(1, (int)(*lt)["repeat"].value_or((int64_t)1)); }
            if (!ra) return "a layer must be a list of rows or { rows = [...], repeat = n }";
            for (auto& r : *ra) rows.push_back(r.value_or(std::string()));
            for (int i = 0; i < repeat; i++) layers.push_back(rows);
        }
        if (layers.empty()) return "no layers";
        int sx = 0, sz = 0;
        for (auto& l : layers) { sz = std::max(sz, (int)l.size()); for (auto& r : l) sx = std::max(sx, (int)r.size()); }
        if (sx == 0 || sz == 0) return "empty layers";
        out.resize({ sx, (int)layers.size(), sz });           // anchor: bottom centre
        for (int y = 0; y < (int)layers.size(); y++)
            for (int z = 0; z < (int)layers[(size_t)y].size(); z++) {
                const std::string& r = layers[(size_t)y][(size_t)z];
                for (int x = 0; x < (int)r.size(); x++) {
                    char ch = r[(size_t)x];
                    if (ch == '.' || ch == ' ') continue;
                    auto it = legend.find(ch);
                    if (it == legend.end()) return std::string("'") + ch + "' is not in the legend";
                    out.set(x, y, z, it->second);
                }
            }
        return "";
    }

    const StructureType* get(uint16_t id) const {
        return (id > 0 && id < types.size()) ? &types[id] : nullptr;
    }
    uint16_t find(const std::string& name) const {
        auto it = index.find(name);
        return it != index.end() ? it->second : 0;
    }
    // ── Parts ─────────────────────────────────────────────
    // The same piece in another material (same family), or 0
    uint16_t swap(uint16_t type, const std::string& material) const {
        const StructureType* t = get(type);
        if (!t || t->family.empty() || material.empty()) return 0;
        if (t->material == material) return type;
        for (const auto& o : types)
            if (o.id && o.family == t->family && o.material == material) return o.id;
        return 0;
    }
    // Every material a piece's family comes in (its own first)
    std::vector<std::string> materialsOf(uint16_t type) const {
        std::vector<std::string> out;
        const StructureType* t = get(type);
        if (!t) return out;
        out.push_back(t->material);
        for (const auto& o : types)
            if (o.id && !t->family.empty() && o.family == t->family &&
                std::find(out.begin(), out.end(), o.material) == out.end()) out.push_back(o.material);
        return out;
    }
    // Every material used by any piece of a group (wall, roof …)
    std::vector<std::string> materialsOfGroup(const std::string& group) const {
        std::vector<std::string> out;
        for (const auto& o : types)
            if (o.id && o.group == group && !o.material.empty() &&
                std::find(out.begin(), out.end(), o.material) == out.end()) out.push_back(o.material);
        return out;
    }
    // Types with a part (or a tag) for generators
    std::vector<uint16_t> withPart(const std::string& part, const std::string& material = "") const {
        std::vector<uint16_t> out;
        for (const auto& o : types)
            if (o.id && o.part == part && (material.empty() || o.material == material)) out.push_back(o.id);
        return out;
    }

    // Build costs on or off (structures.toml: costs = true). Off
    // while there's no dependable way to gather materials
    bool costsEnabled = false;
    bool charges(const StructureType& t) const { return costsEnabled && !t.cost.empty(); }

    std::vector<uint16_t> buildMenu() const {
        std::vector<uint16_t> out;
        for (auto& t : types) if (t.id && t.inBuildMenu) out.push_back(t.id);
        return out;
    }
    std::vector<uint16_t> all() const {
        std::vector<uint16_t> out;
        for (auto& t : types) if (t.id) out.push_back(t.id);
        return out;
    }

private:
    // "{m}_wall_4" → "plank_wall_4"; display "{M} Wall" → "Plank Wall"
    static std::string fillMaterial(std::string s, const std::string& m, bool display) {
        std::string mm = m;
        if (display && !mm.empty()) mm[0] = (char)std::toupper((unsigned char)mm[0]);
        for (const char* key : { "{m}", "{M}" }) {
            size_t at;
            while ((at = s.find(key)) != std::string::npos) {
                std::string rep = (std::string(key) == "{M}" && !mm.empty()) ? mm : (display ? mm : m);
                s.replace(at, 3, rep);
            }
        }
        // clean up a doubled or dangling separator when m is empty
        while (s.find("__") != std::string::npos) s.replace(s.find("__"), 2, "_");
        while (!s.empty() && (s.front() == '_' || s.front() == ' ')) s.erase(s.begin());
        return s;
    }
    // The name with the material slot taken out: "{m}_wall_4" → "wall_4"
    static std::string familyOf(const std::string& nameT) {
        if (nameT.find("{m}") == std::string::npos) return "";
        std::string f = fillMaterial(nameT, "", false);
        while (!f.empty() && f.back() == '_') f.pop_back();
        return f;
    }

    std::vector<StructureType> types;
    std::unordered_map<std::string, uint16_t> index;

    // Shapes made for placed pieces' own specs ("type|spec")
    struct Shapes { std::array<std::array<std::shared_ptr<const VoxelVolume>, 4>, 2> v; int states = 1; };
    mutable std::unordered_map<std::string, Shapes> madeShapes;
    mutable std::mutex cacheLock;

    // A made piece's generator and its defaults (structures.toml):
    //   shape = "wall": outside / inside / opening, and the role
    //   blocks post, beam, base, panel, paint, inlay, glass (the wall
    //   itself is the entry's block); open_w / open_h / sill
    //   shape = "pillar" (style "plain" | "based"): post is the block
    //   kind = "door" (style "plain" | "ledged" | "framed"): door is
    //   the block, beam the ledges
    static void readModel(StructureType& s, const toml::table& t) {
        std::string shp = t["shape"].value_or(std::string());
        if (s.kind == StructureKind::Door) s.model = "door";
        else if (shp == "wall" || shp == "pillar") s.model = shp;
        else if (s.foundation && shp.empty() && !t["layers"].as_array() && !t["hole"].as_array()) s.model = "foundation";
        else return;
        PieceSpec& d = s.defaults;
        d.w = s.size.x; d.h = s.size.y;
        s.style = t["style"].value_or(std::string(s.model == "door" ? "plain" : "plain"));
        const std::string self = gRegistry.get(s.block).name;
        auto role = [&](const char* key, const std::string& def) {
            std::string n = t[key].value_or(def);
            if (!n.empty()) d.blocks[key] = n;
        };
        if (s.model == "wall") {
            s.outside = t["outside"].value_or(t["style"].value_or(std::string("plain")));
            s.inside  = t["inside"].value_or(std::string("plain"));
            s.opening = t["opening"].value_or(std::string());
            s.faced   = s.outside != s.inside || s.outside != "plain";
            d.blocks["fill"] = self;
            role("post", "timber_post"); role("beam", "timber_beam"); role("base", "stone_bricks");
            role("panel", "wainscot"); role("paint", "plaster"); role("inlay", "bricks");
            d.blocks["glass"] = t["glass"].value_or(std::string("none"));
            if (s.opening == "door") { d.openW = 10; d.openH = 24; }
            if (s.opening == "window") { d.openW = std::max(2, s.size.x / 2); d.openH = 9; d.sill = 12; }
            if (auto v = t["open_w"].value<int64_t>()) d.openW = (int)*v;
            if (auto v = t["open_h"].value<int64_t>()) d.openH = (int)*v;
            if (auto v = t["sill"].value<int64_t>()) d.sill = (int)*v;
        } else if (s.model == "foundation") {
            d.d = s.size.z;
            d.blocks["fill"] = self;
        } else if (s.model == "pillar") {
            d.w = s.size.x;
            d.blocks["post"] = self;
            role("base", self); role("beam", self);
        } else {
            d.blocks["door"] = self;
            role("beam", s.style == "plain" ? self : std::string("timber_beam"));
        }
    }
};

extern StructureRegistry gStructures;
