#pragma once
#include "ItemRegistry.h"
#include "../voxel/VoxelRegistry.h"
#include "../world/VoxelVolume.h"
#include "../world/StructureCatalog.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
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

enum class StructureKind : uint8_t { Build, Door, Tree, Spawner, Chest, Station };

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
    bool          foundation   = false;
    bool          needsSupport = false;
    int           snap = 0;        // grid while following the crosshair
    uint16_t      block = BLOCK_STONE;
    glm::ivec3    size  = {1, 1, 1};

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
    bool load(const char* path) {
        types.clear();
        index.clear();
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
                                           : StructureKind::Build;
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
                } else if (s.kind == StructureKind::Door) {
                    // Closed: the panel runs along X from the
                    // hinge. Open: it runs along Z from the same
                    // hinge. Both anchored on the hinge corner.
                    VoxelVolume closed(s.size), open({ s.size.z, s.size.y, s.size.x });
                    std::fill(closed.data.begin(), closed.data.end(), s.block);
                    std::fill(open.data.begin(),   open.data.end(),   s.block);
                    closed.anchor = open.anchor = { 0, 0, 0 };
                    base.push_back(closed);
                    base.push_back(open);
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
};

extern StructureRegistry gStructures;
