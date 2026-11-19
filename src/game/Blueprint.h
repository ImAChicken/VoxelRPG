#pragma once
#include "StructureRegistry.h"
#include "ItemRegistry.h"
#include "../world/StructureManager.h"
#include "../world/Chests.h"
#include "../world/TerrainOps.h"
#include <glm/glm.hpp>
#include <toml++/toml.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Blueprints — whole buildings, saved and placed as one
// ══════════════════════════════════════════════════════════
// A blueprint is a list of build pieces (structure types from
// structures.toml) with where each sits, not a box of voxels:
// so a pasted house is made of real pieces again (doors open,
// chests hold things, pieces can be swapped or taken down),
// and its materials can be restyled when it goes down
// ("stone walls, tile roof") to suit a village.
//
//   capture   click any piece of a building with the hammer's
//             Copy tab: every piece touching it, and touching
//             those, is saved (Blueprint::capture)
//   paste     the Blueprints tab: aim, T to turn, click. The
//             ground under it is levelled first (TerrainOps)
//   generate  the village generator places the same files
//
// Files: data/blueprints/<name>.toml (see the header written by
// save()). Each part keeps its type name, the low corner of its
// box from the blueprint's low corner, its turn, and for chests
// the items in them, or a loot table to roll instead.
//
// Positions: a part's 'at' is the low corner of its closed
// shape's box (shape(0, rot)), the same box the hammer places.

struct BlueprintPart {
    std::string type;                 // structure type name
    glm::ivec3  at  = { 0, 0, 0 };    // low corner, from the blueprint's low corner
    int         rot = 0;
    std::string spec;                 // a made piece's own sizes (and blocks, in older files) (game/PieceSpec.h)
    int         palette = -1;         // which of the blueprint's palettes it's in (-1: none)
    // chests (structure types with slots)
    std::vector<std::pair<std::string, int>> items;   // item name, count
    std::string loot;                 // a loot table to roll when placed
    std::string label;                // the chest's name, if it had one
};

// How to dress a blueprint when placing it: a material per
// part group ("wall" → "stone", "roof" → "tile"). A group not
// listed, or a material a piece doesn't come in, keeps the
// piece as saved.
struct BlueprintStyle {
    std::map<std::string, std::string> materials;
    bool rollLoot = false;            // chests: roll their loot table (if any) instead of the saved items
    bool sitePrep = true;             // level the ground under it first
    int  sink = 0;                    // voxels its foundations sit below the ground (3 = 1 ft)
    // A palette: a block per role, and a wall finish (outside / inside),
    // laid over every made piece (walls, posts, doors) as it goes down,
    // so one village is whitewash and dark timber, the next fieldstone
    // (data/registry/palettes.toml). Empty: as saved
    PieceSpec palette;
    std::string paletteName;          // its name, for the menu (game/Palettes.h)

    std::string materialFor(const std::string& group) const {
        auto it = materials.find(group);
        return it == materials.end() ? std::string() : it->second;
    }
};

struct BlueprintPlacement {
    uint16_t   type = 0;
    glm::ivec3 origin = { 0, 0, 0 };  // what StructureManager::place takes
    int        rot = 0;
    glm::ivec3 lo = { 0, 0, 0 }, hi = { 0, 0, 0 };   // its box in the world
    size_t     part = 0;              // index into Blueprint::parts
    std::string spec;                 // its own sizes and blocks, the style's palette laid over
};

struct Blueprint {
    std::string name;                 // file name, no extension
    std::string display;
    std::vector<std::string> tags;    // "house", "small", "village", "farm" …
    glm::ivec3  size = { 0, 0, 0 };
    int         front = 2;            // the side its door faces: 0 north (-z), 1 east, 2 south, 3 west
    std::vector<BlueprintPart> parts;
    // The palettes its pieces are made in (blocks and wall finish,
    // game/Palettes.h): the Copy tool keeps one per different look.
    // Ones that never disagree (say the walls' palette and the doors')
    // can be merged into one when the player says so (mergePalettes)
    std::vector<PieceSpec> palettes;

    bool hasTag(const std::string& t) const { return std::find(tags.begin(), tags.end(), t) != tags.end(); }

    // Size once turned q quarter turns
    glm::ivec3 sizeTurned(int q) const { return (q & 1) ? glm::ivec3(size.z, size.y, size.x) : size; }
    // Which way the front faces once turned
    int frontTurned(int q) const { return (front + q) & 3; }

    // What placing it would cost: the parts' costs added up
    // (for the day players may place a whole hut or tent)
    std::map<uint16_t, int> cost(const BlueprintStyle* style = nullptr) const {
        std::map<uint16_t, int> out;
        for (const BlueprintPart& p : parts) {
            uint16_t id = styled(p, style);
            if (const StructureType* t = gStructures.get(id))
                for (auto& [item, n] : gStructures.costOf(*t, specFor(p, *t, style))) out[item] += n;
        }
        return out;
    }

    // A part's spec under a style: its own, the palette's blocks (the
    // roles its kind of piece has) and wall finish laid over
    std::string specFor(const BlueprintPart& p, const StructureType& t, const BlueprintStyle* style) const {
        if (!t.sizable()) return "";
        PieceSpec s = PieceSpec::parse(p.spec);
        if (p.palette >= 0 && p.palette < (int)palettes.size()) {
            PieceSpec pal = palettes[p.palette];
            // restyled into another material (stone walls for plank): the
            // roles that were just the old type's own take the new type's
            const StructureType* was = gStructures.get(gStructures.find(p.type));
            if (was && was->id != t.id)
                for (auto it = pal.blocks.begin(); it != pal.blocks.end();)
                    it = was->defaults.block(it->first) == it->second ? pal.blocks.erase(it) : std::next(it);
            s = gStructures.applyPalette(t, s, pal);
        }
        if (style && !style->palette.empty()) s = gStructures.applyPalette(t, s, style->palette);
        return gStructures.normalize(t, s);
    }

    // ── Palettes ──────────────────────────────────────────
    // Two palettes that never set one role (or finish) two ways
    static bool agree(const PieceSpec& a, const PieceSpec& b) {
        for (auto& [role, block] : a.blocks) {
            auto it = b.blocks.find(role);
            if (it != b.blocks.end() && it->second != block) return false;
        }
        if (!a.outside.empty() && !b.outside.empty() && a.outside != b.outside) return false;
        if (!a.inside.empty() && !b.inside.empty() && a.inside != b.inside) return false;
        return true;
    }
    // Which palettes would go if the ones that agree were merged, as
    // a map old index → new (fewer when some can merge). Greedy: each
    // joins the first earlier group it agrees with all of
    std::vector<int> mergePlan(std::vector<PieceSpec>* merged = nullptr) const {
        std::vector<int> to(palettes.size(), -1);
        std::vector<PieceSpec> groups;
        for (size_t i = 0; i < palettes.size(); i++) {
            for (size_t g = 0; g < groups.size() && to[i] < 0; g++)
                if (agree(groups[g], palettes[i])) { groups[g] = PieceSpec::over(groups[g], palettes[i]); to[i] = (int)g; }
            if (to[i] < 0) { to[i] = (int)groups.size(); groups.push_back(palettes[i]); }
        }
        if (merged) *merged = groups;
        return to;
    }
    // How many palettes merging would save (0: nothing to merge)
    int mergeable() const {
        std::vector<PieceSpec> g;
        mergePlan(&g);
        return (int)palettes.size() - (int)g.size();
    }
    // Merge the palettes that agree. The pieces look the same as
    // before (no two of them disagreed), only grouped; asked for by the
    // player, never done on its own
    int mergePalettes() {
        std::vector<PieceSpec> g;
        std::vector<int> to = mergePlan(&g);
        int saved = (int)palettes.size() - (int)g.size();
        for (BlueprintPart& p : parts) if (p.palette >= 0 && p.palette < (int)to.size()) p.palette = to[p.palette];
        palettes = g;
        return saved;
    }

    // The type a part becomes under a style
    static uint16_t styled(const BlueprintPart& p, const BlueprintStyle* style) {
        uint16_t id = gStructures.find(p.type);
        if (!id || !style) return id;
        const StructureType* t = gStructures.get(id);
        std::string m = style->materialFor(t->group);
        if (m.empty() || m == t->material) return id;
        uint16_t sw = gStructures.swap(id, m);
        return sw ? sw : id;
    }

    // ── Placing ───────────────────────────────────────────
    // Where every part goes with the blueprint's low corner at
    // world voxel 'at', turned q quarter turns (clockwise from
    // above, like VoxelVolume::rotatedY). Parts whose type no
    // longer exists are skipped and named in 'missing'.
    std::vector<BlueprintPlacement> plan(glm::ivec3 at, int q, const BlueprintStyle* style = nullptr,
                                         std::vector<std::string>* missing = nullptr) const {
        q &= 3;
        std::vector<BlueprintPlacement> out;
        auto turn = [&](int x, int z) -> glm::ivec2 {
            switch (q) {
            case 1:  return { size.z - 1 - z, x };
            case 2:  return { size.x - 1 - x, size.z - 1 - z };
            case 3:  return { z, size.x - 1 - x };
            default: return { x, z };
            }
        };
        for (size_t i = 0; i < parts.size(); i++) {
            const BlueprintPart& p = parts[i];
            uint16_t id = styled(p, style);
            const StructureType* t = gStructures.get(id);
            if (!t) { if (missing) missing->push_back(p.type); continue; }
            const std::string spec = specFor(p, *t, style);
            glm::ivec3 s = gStructures.shape(id, spec, 0, p.rot).size;
            glm::ivec2 a = turn(p.at.x, p.at.z), b = turn(p.at.x + s.x - 1, p.at.z + s.z - 1);
            glm::ivec3 lo(std::min(a.x, b.x), p.at.y, std::min(a.y, b.y));
            int r = (p.rot + q) & 3;
            // a wall with an outside: turned so it faces away from the
            // building's middle (a half-turn keeps its box)
            if (gStructures.faced(*t, spec)) {
                glm::ivec2 mid(sizeTurned(q).x, sizeTurned(q).z);
                glm::ivec3 sz = gStructures.shape(id, spec, 0, r).size;
                glm::ivec2 c2 = glm::ivec2(lo.x, lo.z) * 2 + glm::ivec2(sz.x, sz.z) - mid;   // from the middle, doubled
                glm::ivec2 out = StructureManager::outsideDir(r);
                if (out.x * c2.x + out.y * c2.y < 0) r = (r + 2) & 3;
            }
            const VoxelVolume& v = gStructures.shape(id, spec, 0, r);
            BlueprintPlacement pl;
            pl.type = id; pl.rot = r; pl.part = i; pl.spec = spec;
            pl.lo = at + lo;
            pl.hi = pl.lo + v.size - 1;
            pl.origin = pl.lo + v.anchor;
            out.push_back(pl);
        }
        return out;
    }

    // Can it go there? "" if so. With site prep on, the ground
    // will be levelled first, so only other buildings are in
    // the way; without, every piece must fit as it stands.
    static std::string check(const StructureManager& sm, const std::vector<BlueprintPlacement>& plan,
                             bool sitePrep) {
        if (plan.empty()) return "the blueprint is empty";
        // other buildings near it (by their boxes first: cheap)
        glm::ivec3 elo, ehi;
        extent(plan, elo, ehi);
        auto overlap = [](glm::ivec3 alo, glm::ivec3 ahi, glm::ivec3 blo, glm::ivec3 bhi) {
            return glm::all(glm::lessThanEqual(alo, bhi)) && glm::all(glm::lessThanEqual(blo, ahi));
        };
        struct Near { const StructureInstance* s; glm::ivec3 lo, hi; };
        std::vector<Near> near;
        if (sitePrep)
            sm.forEachInstance([&](const StructureInstance& s) {
                if (s.natural) return;
                Near n{ &s, {}, {} };
                sm.bounds(s, n.lo, n.hi);
                if (overlap(n.lo, n.hi, elo, ehi)) near.push_back(n);
            });
        for (const BlueprintPlacement& p : plan) {
            const StructureType* t = gStructures.get(p.type);
            if (!sitePrep) {
                std::string why = sm.checkPlace(p.type, p.origin, p.rot, false, nullptr, p.spec);
                if (!why.empty()) return t->displayName + ": " + why;
                continue;
            }
            if (p.lo.y < 0 || p.hi.y >= WORLD_HEIGHT) return "out of the world's height";
            const VoxelVolume& v = gStructures.shape(p.type, p.spec, 0, p.rot);
            for (const Near& n : near) {
                if (!overlap(n.lo, n.hi, p.lo, p.hi)) continue;
                glm::ivec3 a = glm::max(n.lo, p.lo), b = glm::min(n.hi, p.hi);
                for (int y = a.y; y <= b.y; y++)
                for (int z = a.z; z <= b.z; z++)
                for (int x = a.x; x <= b.x; x++) {
                    glm::ivec3 q = glm::ivec3(x, y, z) - p.lo;
                    if (v.at(q.x, q.y, q.z) != BLOCK_AIR && sm.owns(*n.s, { x, y, z }))
                        return t->displayName + ": another building is in the way";
                }
            }
        }
        return "";
    }

    // The box the plan covers (x/z) and its lowest y
    static void extent(const std::vector<BlueprintPlacement>& plan, glm::ivec3& lo, glm::ivec3& hi) {
        lo = glm::ivec3(1 << 30); hi = glm::ivec3(-(1 << 30));
        for (const BlueprintPlacement& p : plan) { lo = glm::min(lo, p.lo); hi = glm::max(hi, p.hi); }
    }

    // Level the ground under it (TerrainOps). Separate so the
    // village generator can prepare a site its own way.
    static EditBatch sitePrep(const StructureManager& sm, const ChunkManager& world,
                              const std::vector<BlueprintPlacement>& plan, EditSource src, int feather = 4,
                              int sink = 0) {
        glm::ivec3 lo, hi;
        extent(plan, lo, hi);
        terrain::FlattenOptions o;
        o.clearAbove = hi.y - lo.y + 8;
        o.maxFill    = 24;
        o.feather    = feather;
        o.keep = [&sm](glm::ivec3 p) { const StructureInstance* s = sm.instanceAt(p); return s && !s->natural; };
        return terrain::flatten(world, { lo.x, lo.z }, { hi.x, hi.z }, lo.y + sink, o, src, "level for a building");
    }

    // Put it in the world. Returns the instances made (parallel
    // to how many parts went down). Chests get their saved
    // items, or their loot table rolled if style->rollLoot.
    std::vector<uint32_t> paste(StructureManager& sm, WorldEditor& editor, ChestStore* chests,
                                const std::vector<BlueprintPlacement>& plan, EditSource src,
                                const BlueprintStyle* style = nullptr, std::string* why = nullptr) const {
        std::vector<uint32_t> made;
        if (style ? style->sitePrep : true) {
            editor.submit(sitePrep(sm, editor.worldRef(), plan, src, 4, style ? style->sink : 0));
            editor.flush();
        }
        int failed = 0;
        for (const BlueprintPlacement& p : plan) {
            uint32_t sup = 0;
            std::string w = sm.checkPlace(p.type, p.origin, p.rot, false, &sup, p.spec);
            if (!w.empty()) {
                if (why && why->empty()) *why = gStructures.get(p.type)->displayName + ": " + w;
                failed++;
                continue;
            }
            StructureInstance* s = sm.place(p.type, p.origin, p.rot, src, sup, p.spec);
            if (!s) continue;
            made.push_back(s->id);
            const BlueprintPart& part = parts[p.part];
            const StructureType* t = gStructures.get(p.type);
            if (chests && ChestStore::holds(t)) {
                Container& c = chests->get(s->id, *t);
                if (!part.label.empty()) c.label = part.label;
                if (style && style->rollLoot && !part.loot.empty()) c.reset(part.loot, chests->seedFor(s->id));
                else if (!part.items.empty()) {
                    c.makeEmpty();
                    for (auto& [name, n] : part.items)
                        if (uint16_t item = gItems.find(name)) c.read().add(item, n);
                } else if (!part.loot.empty()) c.reset(part.loot, chests->seedFor(s->id));
            }
        }
        editor.flush();
        if (why && failed && why->find("pieces") == std::string::npos)
            *why = std::to_string(failed) + " pieces did not fit (" + *why + ")";
        return made;
    }

    // ── Capturing ─────────────────────────────────────────
    // Every built piece connected to 'seed' (touching, side or
    // top), up to 'limit' pieces. Trees and other natural things
    // are left out. False (and why) if there is nothing to save.
    static bool capture(const StructureManager& sm, uint32_t seed, const ChestStore* chests,
                        Blueprint& out, std::string& why, size_t limit = 600,
                        glm::ivec3* worldLo = nullptr) {
        struct Box { uint32_t id; glm::ivec3 lo, hi; };
        const StructureInstance* s0 = sm.get(seed);
        if (!s0) { why = "aim at a building piece"; return false; }
        auto boxOf = [&](const StructureInstance& s, Box& b) {
            const StructureType* t = sm.typeOf(s);
            if (!t || s.natural || t->kind == StructureKind::Tree) return false;
            const VoxelVolume& v = StructureManager::shapeOf(s, 0);
            b.id = s.id;
            b.lo = s.origin - v.anchor;
            b.hi = b.lo + v.size - 1;
            return true;
        };
        Box first;
        if (!boxOf(*s0, first)) { why = "that is not a building piece"; return false; }
        // candidates: built pieces within reach of the first
        std::vector<Box> near;
        sm.forEachInstance([&](const StructureInstance& s) {
            Box b;
            if (!boxOf(s, b)) return;
            if (glm::any(glm::greaterThan(glm::abs(b.lo - first.lo), glm::ivec3(256)))) return;
            near.push_back(b);
        });
        auto touch = [](const Box& a, const Box& b) {
            for (int k = 0; k < 3; k++)
                if (a.hi[k] + 1 < b.lo[k] || b.hi[k] + 1 < a.lo[k]) return false;
            return true;
        };
        std::vector<Box> got;
        std::set<uint32_t> seen{ first.id };
        std::queue<Box> todo;
        todo.push(first);
        while (!todo.empty() && got.size() < limit) {
            Box b = todo.front(); todo.pop();
            got.push_back(b);
            for (const Box& n : near)
                if (!seen.count(n.id) && touch(b, n)) { seen.insert(n.id); todo.push(n); }
        }
        glm::ivec3 lo(1 << 30), hi(-(1 << 30));
        for (const Box& b : got) { lo = glm::min(lo, b.lo); hi = glm::max(hi, b.hi); }

        out.parts.clear();
        out.palettes.clear();
        out.size = hi - lo + 1;
        if (worldLo) *worldLo = lo;
        for (const Box& b : got) {
            const StructureInstance* s = sm.get(b.id);
            const StructureType* t = sm.typeOf(*s);
            BlueprintPart p;
            p.type = t->name;
            p.at   = b.lo - lo;
            p.rot  = s->rot;
            // its palette (the same look as another piece: the same one),
            // and its own sizes
            if (t->sizable()) {
                PieceSpec look = gStructures.paletteOf(*t, s->spec);
                auto it = std::find(out.palettes.begin(), out.palettes.end(), look);
                p.palette = (int)(it - out.palettes.begin());
                if (it == out.palettes.end()) out.palettes.push_back(look);
                p.spec = gStructures.shapeOnly(*t, s->spec).text();
            }
            if (chests && ChestStore::holds(t))
                if (const Container* c = const_cast<ChestStore*>(chests)->find(s->id)) {
                    p.label = c->label;
                    if (const Inventory* inv = c->peek()) {
                        for (const ItemStack& st : inv->slots)
                            if (!st.empty()) p.items.push_back({ gItems.get(st.itemID).name, st.count });
                    } else p.loot = c->loot;
                }
            out.parts.push_back(p);
        }
        sortParts(out);
        out.front = guessFront(out);
        return true;
    }

    // Build order: foundations, then upward; doors after the
    // doorways they hang in
    static void sortParts(Blueprint& bp) {
        auto rank = [](const BlueprintPart& p) {
            const StructureType* t = gStructures.get(gStructures.find(p.type));
            if (!t) return 3;
            if (t->foundation) return 0;
            if (t->kind == StructureKind::Door || t->kind == StructureKind::Chest) return 2;
            return 1;
        };
        std::stable_sort(bp.parts.begin(), bp.parts.end(), [&](const BlueprintPart& a, const BlueprintPart& b) {
            int ra = rank(a), rb = rank(b);
            if ((ra == 0) != (rb == 0)) return ra == 0;
            if (a.at.y != b.at.y) return a.at.y < b.at.y;
            return ra < rb;
        });
    }

    // The side with a door (or doorway) nearest it
    static int guessFront(const Blueprint& bp) {
        for (int pass = 0; pass < 2; pass++)
            for (const BlueprintPart& p : bp.parts) {
                const StructureType* t = gStructures.get(gStructures.find(p.type));
                if (!t) continue;
                bool hit = pass == 0 ? t->kind == StructureKind::Door : t->part == "wall_door";
                if (!hit) continue;
                glm::ivec3 s = gStructures.shape(t->id, p.spec, 0, p.rot).size;
                int c[4] = { p.at.z, bp.size.x - (p.at.x + s.x), bp.size.z - (p.at.z + s.z), p.at.x };
                return int(std::min_element(c, c + 4) - c);
            }
        return 2;
    }

    // ── Files ─────────────────────────────────────────────
    bool save(const std::string& path) const {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream f(path);
        if (!f) return false;
        write(f);
        return (bool)f;
    }
    // As text (the file's contents: a server is sent this)
    std::string text() const { std::ostringstream s; write(s); return s.str(); }
    bool fromText(const std::string& t, std::string& why) {
        try { readTable(toml::parse(t), "", why); return true; }
        catch (const toml::parse_error& e) { why = std::string(e.description()); return false; }
    }
    void write(std::ostream& f) const {
        f << "# Blueprint: a building made of build pieces (game/Blueprint.h).\n"
             "# Saved by the hammer's Copy tab; placed from its Blueprints tab and\n"
             "# by the village generator. Each part: its structure type, the low\n"
             "# corner of its box from the blueprint's low corner, its turn; a made\n"
             "# piece (wall, post, door) its own sizes and blocks (spec); chests\n"
             "# also keep items, or a loot table rolled when placed.\n"
             "#   front: the side the door faces, 0 north, 1 east, 2 south, 3 west\n\n";
        f << "name = \"" << name << "\"\n";
        f << "display = \"" << (display.empty() ? name : display) << "\"\n";
        f << "tags = [";
        for (size_t i = 0; i < tags.size(); i++) f << (i ? ", " : "") << "\"" << tags[i] << "\"";
        f << "]\n";
        f << "size = [" << size.x << ", " << size.y << ", " << size.z << "]\n";
        f << "front = " << front << "\n";
        for (size_t i = 0; i < palettes.size(); i++)
            f << "\n[[palette]]   # " << i << "\nspec = \"" << palettes[i].text() << "\"\n";
        for (const BlueprintPart& p : parts) {
            f << "\n[[part]]\ntype = \"" << p.type << "\"\nat = [" << p.at.x << ", " << p.at.y << ", " << p.at.z
              << "]\nrot = " << p.rot << "\n";
            if (!p.spec.empty()) f << "spec = \"" << p.spec << "\"\n";
            if (p.palette >= 0) f << "palette = " << p.palette << "\n";
            if (!p.items.empty()) {
                f << "items = [";
                for (size_t i = 0; i < p.items.size(); i++)
                    f << (i ? ", " : "") << "[\"" << p.items[i].first << "\", " << p.items[i].second << "]";
                f << "]\n";
            }
            if (!p.loot.empty())  f << "loot = \"" << p.loot << "\"\n";
            if (!p.label.empty()) f << "label = \"" << p.label << "\"\n";
        }
    }

    bool load(const std::string& path, std::string& why) {
        try {
            readTable(toml::parse_file(path), std::filesystem::path(path).stem().string(), why);
            return true;
        } catch (const toml::parse_error& e) {
            why = std::string(e.description());
            return false;
        }
    }
    void readTable(const toml::table& tbl, const std::string& stem, std::string& why) {
        (void)why;
        {
            name    = tbl["name"].value_or(stem);
            display = tbl["display"].value_or(name);
            tags.clear();
            if (auto* a = tbl["tags"].as_array())
                for (auto& n : *a) if (auto s = n.value<std::string>()) tags.push_back(*s);
            if (auto* a = tbl["size"].as_array(); a && a->size() == 3)
                size = { (*a)[0].value_or(0), (*a)[1].value_or(0), (*a)[2].value_or(0) };
            front = tbl["front"].value_or(2) & 3;
            parts.clear();
            palettes.clear();
            if (auto* arr = tbl["palette"].as_array())
                for (auto& node : *arr)
                    if (auto* t = node.as_table()) palettes.push_back(PieceSpec::parse((*t)["spec"].value_or(std::string())));
            if (auto* arr = tbl["part"].as_array())
                for (auto& node : *arr) {
                    auto* t = node.as_table();
                    if (!t) continue;
                    BlueprintPart p;
                    p.type = (*t)["type"].value_or(std::string());
                    if (auto* a = (*t)["at"].as_array(); a && a->size() == 3)
                        p.at = { (*a)[0].value_or(0), (*a)[1].value_or(0), (*a)[2].value_or(0) };
                    p.rot   = (*t)["rot"].value_or(0) & 3;
                    p.loot  = (*t)["loot"].value_or(std::string());
                    p.label = (*t)["label"].value_or(std::string());
                    p.spec  = (*t)["spec"].value_or(std::string());
                    p.palette = (*t)["palette"].value_or(-1);
                    if (auto* items = (*t)["items"].as_array())
                        for (auto& it : *items)
                            if (auto* pr = it.as_array(); pr && pr->size() == 2)
                                p.items.push_back({ (*pr)[0].value_or(std::string()), (*pr)[1].value_or(1) });
                    if (!p.type.empty()) parts.push_back(p);
                }
            // a hand-written file may leave the size out
            if (size == glm::ivec3(0)) {
                for (const BlueprintPart& p : parts)
                    if (const StructureType* t = gStructures.get(gStructures.find(p.type)))
                        size = glm::max(size, p.at + gStructures.shape(t->id, p.spec, 0, p.rot).size);
            }
        }
    }
};

// ══════════════════════════════════════════════════════════
// The blueprints on disk (data/blueprints), reloaded with F6
// ══════════════════════════════════════════════════════════
class BlueprintLibrary {
public:
    static constexpr const char* DIR = "data/blueprints";

    int load(const std::string& dir = DIR) {
        all.clear();
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return 0;
        std::vector<std::filesystem::path> files;
        for (auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".toml") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (auto& f : files) {
            Blueprint bp;
            std::string why;
            if (bp.load(f.string(), why)) all.push_back(std::move(bp));
            else std::cerr << "blueprint " << f.string() << ": " << why << "\n";
        }
        std::cout << "Blueprints: " << all.size() << " from " << dir << "\n";
        return (int)all.size();
    }

    const std::vector<Blueprint>& list() const { return all; }
    const Blueprint* find(const std::string& name) const {
        for (const Blueprint& b : all) if (b.name == name) return &b;
        return nullptr;
    }
    std::vector<const Blueprint*> withTag(const std::string& tag) const {
        std::vector<const Blueprint*> out;
        for (const Blueprint& b : all) if (b.hasTag(tag)) out.push_back(&b);
        return out;
    }

    // Save (or replace) one and keep it in the list
    bool save(Blueprint bp, const std::string& dir = DIR) {
        if (!bp.save(dir + "/" + bp.name + ".toml")) return false;
        for (Blueprint& b : all) if (b.name == bp.name) { b = std::move(bp); return true; }
        all.push_back(std::move(bp));
        return true;
    }

    // A free name: base, base_2, base_3 …
    std::string freeName(const std::string& base) const {
        if (!find(base)) return base;
        for (int i = 2;; i++) if (!find(base + "_" + std::to_string(i))) return base + "_" + std::to_string(i);
    }

private:
    std::vector<Blueprint> all;
};

inline BlueprintLibrary gBlueprints;
