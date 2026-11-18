// Headless checks for blueprints (game/Blueprint.h) and the ground
// levelling they use (world/TerrainOps.h): build houses with the
// hammer's snapping, capture them, save and load the file, paste
// them turned and restyled, and keep chest contents.
//
// It also makes the village buildings in data/blueprints: each is
// built here piece by piece, the way a player would with the hammer,
// so every file is known to place cleanly.
//     blueprint_test            check (files go to a temp folder)
//     blueprint_test --write    also rewrite data/blueprints/*.toml
// Run from the repo root.
#include "TestWorld.h"
#include "game/Blueprint.h"
#include "game/Village.h"
#include "world/StructureManager.h"
#include "world/WorldEditor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>

using namespace testworld;

namespace {

// Builds with the hammer's snapping, like a player aiming
struct Builder {
    StructureManager& sm;
    WorldEditor& ed;
    int placed = 0;
    std::string lastWhy;

    StructureInstance* put(const std::string& name, glm::ivec3 aimed, glm::ivec3 normal = { 0, 1, 0 },
                           glm::vec3 look = { 0, 0, 1 }, int rotPick = 0) {
        uint16_t type = gStructures.find(name);
        if (!type) { lastWhy = "no type " + name; std::printf("    (no type %s)\n", name.c_str()); return nullptr; }
        glm::ivec3 spot; int rot;
        if (!sm.snapTo(type, rotPick, aimed, normal, look, spot, rot)) { lastWhy = "nothing to snap to"; std::printf("    (%s: nothing to snap to)\n", name.c_str()); return nullptr; }
        uint32_t sup = 0;
        lastWhy = sm.checkPlace(type, spot, rot, true, &sup);
        if (!lastWhy.empty()) {
            const VoxelVolume& v = gStructures.get(type)->shape(0, rot);
            glm::ivec3 o = spot - v.anchor;
            std::string who;
            for (int y = 0; y < v.size.y && who.empty(); y++) for (int z = 0; z < v.size.z && who.empty(); z++)
                for (int x = 0; x < v.size.x && who.empty(); x++)
                    if (v.at(x, y, z) && ed.worldRef().typeAtVoxel(o.x + x, o.y + y, o.z + z)) {
                        const StructureInstance* s = sm.instanceAt(o + glm::ivec3(x, y, z));
                        who = s ? gStructures.get(s->type)->name : "ground";
                        who += " at " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
                    }
            std::printf("    (%s: %s; box %d,%d,%d size %d,%d,%d rot %d; %s)\n", name.c_str(), lastWhy.c_str(),
                        o.x, o.y, o.z, v.size.x, v.size.y, v.size.z, rot, who.c_str());
            return nullptr;
        }
        StructureInstance* s = sm.place(type, spot, rot, EditSource::Command, sup);
        ed.flush();
        placed++;
        return s;
    }
    StructureInstance* foundation(const std::string& name, glm::ivec3 at) {
        uint16_t type = gStructures.find(name);
        if (!sm.checkPlace(type, at, 0, true).empty()) return nullptr;
        StructureInstance* s = sm.place(type, at, 0, EditSource::Command, 0);
        ed.flush();
        placed++;
        return s;
    }
    struct Box { glm::ivec3 lo, hi; };
    Box box(const StructureInstance* s) const { Box b; sm.bounds(*s, b.lo, b.hi); return b; }

    // A wall-like piece on one edge of a floor or foundation top:
    // side 0 north, 1 east, 2 south, 3 west; 'along' voxels from
    // the edge's low end
    StructureInstance* edge(const std::string& name, const StructureInstance* base, int side, int along) {
        Box b = box(base);
        glm::ivec3 a(0, b.hi.y, 0);
        switch (side) {
        case 0: a.x = b.lo.x + along; a.z = b.lo.z + 1; break;
        case 1: a.x = b.hi.x - 1;     a.z = b.lo.z + along; break;
        case 2: a.x = b.lo.x + along; a.z = b.hi.z - 1; break;
        default: a.x = b.lo.x + 1;    a.z = b.lo.z + along; break;
        }
        return put(name, a);
    }
    // Something set down on a floor at (x, z) from its low corner
    StructureInstance* on(const std::string& name, const StructureInstance* base, int x, int z, int rotPick = 0) {
        Box b = box(base);
        return put(name, { b.lo.x + x, b.hi.y, b.lo.z + z }, { 0, 1, 0 }, { 0, 0, 1 }, rotPick);
    }
    // A door into a doorway (aimed at its frame, off the corners)
    StructureInstance* door(const std::string& name, const StructureInstance* doorway, int rotPick = 0) {
        Box b = box(doorway);
        for (int y = b.lo.y; y <= b.hi.y; y++) for (int z = b.lo.z; z <= b.hi.z; z++) for (int x = b.lo.x; x <= b.hi.x; x++)
            if (sm.instanceAt({ x, y, z }) == doorway) return put(name, { x, y, z }, { 0, 0, 1 }, { 0, 0, 1 }, rotPick);
        return nullptr;
    }
    // A ceiling on a wall's top (covers the room the wall is
    // the north/west side of)
    StructureInstance* ceilingOn(const std::string& name, const StructureInstance* wall) {
        Box b = box(wall);
        return put(name, { b.lo.x + 3, b.hi.y, b.lo.z + 1 });
    }
    // Another ceiling beside one, aimed at its side face
    StructureInstance* beside(const std::string& name, const StructureInstance* piece, glm::ivec3 dir) {
        Box b = box(piece);
        glm::ivec3 mid = (b.lo + b.hi) / 2;
        glm::ivec3 a = mid;
        for (int k = 0; k < 3; k++) if (dir[k] > 0) a[k] = b.hi[k]; else if (dir[k] < 0) a[k] = b.lo[k];
        a.y = b.lo.y;
        return put(name, a, dir);
    }
};

// A gabled roof over one 8 ft ceiling: two 8 x 4 halves meeting
// over its middle (ridge along x), and gable ends if asked
bool gableRoof(Builder& B, const StructureInstance* ceil, const std::string& m, bool westEnd, bool eastEnd,
               const std::string& gableMat) {
    bool ok = B.on(m + "_roof_8x4", ceil, 12, 1) && B.on(m + "_roof_8x4", ceil, 12, 22);
    if (westEnd) ok = B.edge(gableMat + "_gable_8", ceil, 3, 12) && ok;
    if (eastEnd) ok = B.edge(gableMat + "_gable_8", ceil, 1, 12) && ok;
    return ok;
}

StructureInstance* loot(StructureInstance* c, ChestStore& chests, const std::string& table) {
    if (c) chests.get(c->id, *gStructures.get(c->type)).reset(table, 7);
    return c;
}

StructureInstance* chestWith(Builder& B, const StructureInstance* floor, int x, int z, ChestStore& chests,
                             std::vector<std::pair<std::string, int>> items, const std::string& label = "") {
    StructureInstance* c = B.on("wooden_chest", floor, x, z);
    if (!c) return nullptr;
    const StructureType* t = gStructures.get(c->type);
    Container& box = chests.get(c->id, *t);
    box.makeEmpty();
    for (auto& [n, k] : items) box.read().add(gItems.find(n), k);
    box.label = label;
    return c;
}

// ── The village buildings ─────────────────────────────────
// Each builds at A and returns its first foundation (or null)

// One room, 8 x 8 ft: plank walls, a door south, a window east,
// thatch roof, bed and a chest
StructureInstance* smallCottage(Builder& B, ChestStore& chests, glm::ivec3 A) {
    StructureInstance* f = B.foundation("stone_foundation", A);
    if (!f) return nullptr;
    StructureInstance* n = B.edge("plank_wall_8", f, 0, 12);
    bool ok = n && B.edge("plank_wall_8", f, 3, 12) && B.edge("plank_window_8", f, 1, 12);
    StructureInstance* dw = B.edge("plank_doorway_8", f, 2, 12);
    ok = ok && dw && B.door("plank_door", dw);
    StructureInstance* c = n ? B.ceilingOn("plank_floor_8", n) : nullptr;
    ok = ok && c && gableRoof(B, c, "thatch", true, true, "plank");
    ok = ok && B.on("bed", f, 6, 12) && chestWith(B, f, 16, 5, chests, { { "planks", 12 }, { "torch", 4 } }, "Linen chest");
    return ok ? f : nullptr;
}

// Two rooms long (16 x 8 ft): stone walls, a hearth, a table and
// benches, tile roof
StructureInstance* longhouse(Builder& B, ChestStore& chests, glm::ivec3 A) {
    (void)chests;
    StructureInstance* f1 = B.foundation("stone_foundation", A);
    StructureInstance* f2 = B.foundation("stone_foundation", A + glm::ivec3(24, 0, 0));
    if (!f1 || !f2) return nullptr;
    StructureInstance* n1 = B.edge("stone_wall_8", f1, 0, 12);
    bool ok = n1 && B.edge("stone_window_8", f2, 0, 12) && B.edge("stone_wall_8", f1, 3, 12) &&
              B.edge("stone_wall_8", f2, 1, 12) && B.edge("stone_window_8", f1, 2, 12);
    StructureInstance* dw = B.edge("stone_doorway_8", f2, 2, 12);
    ok = ok && dw && B.door("plank_door", dw);
    StructureInstance* c1 = n1 ? B.ceilingOn("plank_floor_8", n1) : nullptr;
    StructureInstance* c2 = c1 ? B.beside("plank_floor_8", c1, { 1, 0, 0 }) : nullptr;
    ok = ok && c1 && c2 && gableRoof(B, c1, "tile", true, false, "stone") && gableRoof(B, c2, "tile", false, true, "stone");
    ok = ok && B.on("hearth", f1, 12, 12) && B.on("table", f2, 9, 9) && B.on("bench", f2, 9, 16);
    ok = ok && loot(B.on("wooden_chest", f2, 19, 9, 1), chests, "village_house");
    return ok ? f1 : nullptr;
}

// A smithy: a closed stone room with an open forge floor in front,
// slate foundations and roof
StructureInstance* smithy(Builder& B, ChestStore& chests, glm::ivec3 A) {
    StructureInstance* back  = B.foundation("slate_foundation", A);
    StructureInstance* front = B.foundation("slate_foundation", A + glm::ivec3(0, 0, 24));
    if (!back || !front) return nullptr;
    StructureInstance* n = B.edge("stone_wall_8", back, 0, 12);
    bool ok = n && B.edge("stone_wall_8", back, 3, 12) && B.edge("stone_wall_8", back, 1, 12);
    StructureInstance* dw = B.edge("stone_doorway_8", back, 2, 12);
    ok = ok && dw && B.door("plank_door", dw);
    ok = ok && B.on("wood_post", front, 1, 22) && B.on("wood_post", front, 22, 22);
    StructureInstance* c = n ? B.ceilingOn("plank_floor_8", n) : nullptr;
    StructureInstance* c2 = c ? B.beside("plank_floor_8", c, { 0, 0, 1 }) : nullptr;
    ok = ok && c && c2 && B.on("slate_roof_8x8", c, 12, 1) && B.on("slate_roof_8x8", c2, 12, 22);
    ok = ok && B.edge("stone_gable_16", c, 3, 12) && B.edge("stone_gable_16", c, 1, 12);
    ok = ok && B.on("forge", front, 7, 10) && B.on("anvil", front, 17, 12) && B.on("workbench", back, 11, 16);
    StructureInstance* ch = B.on("wooden_chest", back, 17, 6);
    if (ch) chests.get(ch->id, *gStructures.get(ch->type)).reset("village_smithy", 1);
    return ok && ch ? back : nullptr;
}

// A storehouse: brick walls, slate floor upstairs ceiling, chests
StructureInstance* storehouse(Builder& B, ChestStore& chests, glm::ivec3 A) {
    StructureInstance* f = B.foundation("stone_foundation", A);
    if (!f) return nullptr;
    StructureInstance* n = B.edge("brick_wall_8", f, 0, 12);
    bool ok = n && B.edge("brick_wall_8", f, 3, 12) && B.edge("brick_wall_8", f, 1, 12);
    StructureInstance* dw = B.edge("brick_doorway_8", f, 2, 12);
    ok = ok && dw && B.door("plank_door", dw);
    StructureInstance* c = n ? B.ceilingOn("plank_floor_8", n) : nullptr;
    ok = ok && c && gableRoof(B, c, "tile", true, true, "stone");
    const int spots[3][3] = { { 6, 5, 0 }, { 17, 5, 0 }, { 5, 16, 1 } };
    for (int i = 0; i < 3; i++) {
        StructureInstance* ch = B.on("wooden_chest", f, spots[i][0], spots[i][1], spots[i][2]);
        if (ch) chests.get(ch->id, *gStructures.get(ch->type)).reset("village_house", 10 + i);
        ok = ok && ch;
    }
    return ok ? f : nullptr;
}

// The village square: a 10 x 10 ft slate pad with a campfire,
// benches and lantern posts (the generator puts one in the middle)
StructureInstance* square(Builder& B, ChestStore&, glm::ivec3 A) {
    StructureInstance* f = B.foundation("slate_foundation_10", A);
    if (!f) return nullptr;
    bool ok = B.on("campfire", f, 15, 15) && B.on("bench", f, 15, 4) && B.on("bench", f, 15, 26) &&
              B.on("lantern_post", f, 2, 2) && B.on("lantern_post", f, 27, 27);
    return ok ? f : nullptr;
}

// A plaster workshop on a 6 x 6 ft footprint, open-fronted, with a
// flat plank roof
StructureInstance* hut(Builder& B, ChestStore&, glm::ivec3 A) {
    StructureInstance* f = B.foundation("stone_foundation_6", A);
    if (!f) return nullptr;
    StructureInstance* n = B.edge("plaster_wall_4", f, 0, 4);
    bool ok = n && B.edge("plaster_wall_2", f, 0, 15) && B.edge("plaster_wall_4", f, 3, 4) && B.edge("plaster_wall_2", f, 3, 15) &&
              B.edge("plaster_wall_4", f, 1, 4) && B.edge("plaster_wall_2", f, 1, 15) && B.edge("plaster_wall_2", f, 2, 15);
    // the roof: one 4 ft ceiling and five 2 ft ones around it
    StructureInstance* c = n ? B.ceilingOn("plank_floor_4", n) : nullptr;
    StructureInstance* e1 = c ? B.beside("plank_floor_2", c, { 1, 0, 0 }) : nullptr;
    StructureInstance* e2 = e1 ? B.beside("plank_floor_2", e1, { 0, 0, 1 }) : nullptr;
    StructureInstance* s1 = c ? B.beside("plank_floor_2", c, { 0, 0, 1 }) : nullptr;
    StructureInstance* s2 = s1 ? B.beside("plank_floor_2", s1, { 1, 0, 0 }) : nullptr;
    StructureInstance* s3 = s2 ? B.beside("plank_floor_2", s2, { 1, 0, 0 }) : nullptr;
    ok = ok && c && e1 && e2 && s1 && s2 && s3;
    ok = ok && B.on("workbench", f, 8, 9, 1);
    return ok ? f : nullptr;
}

struct Design {
    const char* name;
    const char* display;
    std::vector<std::string> tags;
    std::function<StructureInstance*(Builder&, ChestStore&, glm::ivec3)> build;
};

} // namespace

int main(int argc, char** argv) {
    bool write = argc > 1 && std::strcmp(argv[1], "--write") == 0;
    if (!loadData() || !gStructures.load("data/registry/structures.toml")) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }

    std::printf("levelling ground (TerrainOps)\n");
    {
        World w;
        WorldEditor ed(w.chunks);
        // a hill and a hole
        EditBatch b;
        for (int z = 0; z < 6; z++) for (int x = 0; x < 6; x++) for (int y = 0; y < 5; y++) b.set(200 + x, GROUND + y, 200 + z, BLOCK_DIRT);
        for (int z = 0; z < 4; z++) for (int x = 0; x < 4; x++) for (int y = 1; y <= 4; y++) b.set(210 + x, GROUND - y, 200 + z, BLOCK_AIR);
        ed.apply(std::move(b));
        terrain::FlattenOptions o;
        o.feather = 0;
        ed.apply(terrain::flatten(w.chunks, { 198, 198 }, { 216, 208 }, GROUND, o, EditSource::Command));
        bool flat = true;
        for (int z = 198; z <= 208; z++) for (int x = 198; x <= 216; x++)
            flat = flat && terrain::groundLevel(w.chunks, x, z, GROUND + 20) == GROUND;
        check(flat, "a hill cut down and a hole filled: level");
        check(w.chunks.typeAtVoxel(203, GROUND - 1, 203) == BLOCK_DIRT || w.chunks.typeAtVoxel(203, GROUND - 1, 203) == BLOCK_GRASS,
              "  topped with the ground that was there");
        o.feather = 6;
        ed.apply(terrain::flatten(w.chunks, { 300, 300 }, { 305, 305 }, GROUND + 6, o, EditSource::Command));
        int mid = terrain::groundLevel(w.chunks, 302, 302, GROUND + 30);
        int ring = terrain::groundLevel(w.chunks, 309, 302, GROUND + 30);
        int out = terrain::groundLevel(w.chunks, 320, 302, GROUND + 30);
        check(mid == GROUND + 6 && ring > GROUND && ring < GROUND + 6 && out == GROUND, "a raised pad slopes back down to the ground around it");
    }

    std::vector<Design> designs = {
        { "cottage_small", "Small Cottage", { "house", "village", "small" }, smallCottage },
        { "longhouse",     "Longhouse",     { "house", "village", "large" }, longhouse },
        { "smithy",        "Smithy",        { "smithy", "village", "work" }, smithy },
        { "storehouse",    "Storehouse",    { "storage", "village" },        storehouse },
        { "hut_workshop",  "Workshop Hut",  { "work", "village", "small" },  hut },
        { "village_square", "Village Square", { "plaza", "village" },        square },
    };
    std::string dir = write ? std::string(BlueprintLibrary::DIR)
                            : (std::filesystem::temp_directory_path() / "voxelrpg_blueprints").string();

    for (const Design& d : designs) {
        std::printf("%s\n", d.display);
        World w;
        WorldEditor ed(w.chunks);
        TerrainGenerator gen(1);
        StructureManager sm(w.chunks, ed, gen);
        ChestStore chests;
        Builder B{ sm, ed };
        const glm::ivec3 A(100, GROUND, 100);
        StructureInstance* f = d.build(B, chests, A);
        check(f != nullptr, "built with the hammer's snapping (" + std::to_string(B.placed) + " pieces)");
        if (!f) continue;
        int built = 0;
        sm.forEachInstance([&](const StructureInstance&) { built++; });

        // VOXELRPG_DUMP=<folder>: each building's voxels as text
        // ("x y z block" lines) for a picture
        if (const char* dump = std::getenv("VOXELRPG_DUMP")) {
            std::ofstream out(std::string(dump) + "/" + d.name + ".txt");
            for (int y = A.y - 2; y < A.y + 90; y++) for (int z = A.z - 4; z < A.z + 60; z++) for (int x = A.x - 4; x < A.x + 60; x++)
                if (uint16_t id = w.chunks.typeAtVoxel(x, y, z)) out << x - A.x << " " << y - A.y << " " << z - A.z << " " << id << "\n";
        }
        Blueprint bp;
        std::string why;
        check(Blueprint::capture(sm, f->id, &chests, bp, why), "captured from one click on its foundation");
        bp.name = d.name; bp.display = d.display; bp.tags = d.tags;
        check((int)bp.parts.size() == built, "  every piece: " + std::to_string(bp.parts.size()) + " of " + std::to_string(built));
        BlueprintLibrary lib;
        check(lib.save(bp, dir), "  saved");
        Blueprint back;
        check(back.load(dir + "/" + d.name + ".toml", why) && back.parts.size() == bp.parts.size() && back.size == bp.size,
              "  loads back the same");

        // Paste it turned every way, restyled, into fresh worlds
        for (int q = 0; q < 4; q++) {
            World w2;
            WorldEditor ed2(w2.chunks);
            StructureManager sm2(w2.chunks, ed2, gen);
            ChestStore chests2;
            BlueprintStyle style;
            if (q == 1) style.materials = { { "wall", "stone" }, { "roof", "slate" }, { "foundation", "slate" } };
            if (q == 3) style.rollLoot = true;
            auto plan = back.plan(A + glm::ivec3(40, 0, 40), q, &style);
            std::string bad = Blueprint::check(sm2, plan, true);
            std::string pw;
            auto made = back.paste(sm2, ed2, &chests2, plan, EditSource::Command, &style, &pw);
            if (!pw.empty()) std::printf("    (%s)\n", pw.c_str());
            check(bad.empty() && made.size() == back.parts.size(),
                  "  pasted turned " + std::to_string(q * 90) + (q == 1 ? ", stone and slate" : "") + ": every piece fits");
            if (q == 0) check(!Blueprint::check(sm2, plan, true).empty(), "    a second copy on top of it is refused");
            if (q == 1) {
                int stone = 0, walls = 0;
                for (uint32_t id : made) {
                    const StructureType* t = sm2.typeOf(*sm2.get(id));
                    if (t->group == "wall") { walls++; stone += t->material == "stone"; }
                }
                check(walls == 0 || (stone == walls || d.name == std::string("hut_workshop")),
                      "    walls restyled: " + std::to_string(stone) + " of " + std::to_string(walls) + " stone");
            }
            // doors still sit in their doorways
            for (uint32_t id : made) {
                const StructureInstance* s = sm2.get(id);
                const StructureType* t = sm2.typeOf(*s);
                if (t->kind != StructureKind::Door) continue;
                const VoxelVolume& v = t->shape(0, s->rot);
                glm::ivec3 lo = s->origin - v.anchor;
                const StructureInstance* hole = nullptr;
                for (uint32_t o : made) {
                    const StructureInstance* x = sm2.get(o);
                    glm::ivec3 hl, hh;
                    if (sm2.typeOf(*x)->part == "wall_door" && sm2.holeOf(*x, hl, hh) &&
                        glm::all(glm::greaterThanEqual(lo, hl)) && glm::all(glm::lessThanEqual(lo + v.size - 1, hh)))
                        hole = x;
                }
                check(hole != nullptr, "    the door is in its doorway");
            }
            // chests
            int saved = 0, got = 0;
            for (const BlueprintPart& p : back.parts) for (auto& it : p.items) saved += it.second;
            for (uint32_t id : made) {
                const StructureType* t = sm2.typeOf(*sm2.get(id));
                if (!ChestStore::holds(t)) continue;
                if (Container* c = chests2.find(id)) {
                    if (q == 3 || !c->opened()) continue;
                    for (const ItemStack& s : c->read().slots) got += s.count;
                }
            }
            if (q == 0 && saved) check(got == saved, "    chest items kept: " + std::to_string(got));
        }
        // the front is where the door is
        if (d.name == std::string("cottage_small") || d.name == std::string("longhouse"))
            check(back.front == 2, "  its front is south, where the door is");
    }

    std::printf("the library\n");
    BlueprintLibrary lib;
    check(lib.load(dir) >= (int)designs.size(), "loads every file in the folder");
    check(!lib.withTag("house").empty() && lib.find("village_square"), "finds them by tag and name");
    if (const Blueprint* c = lib.find("cottage_small")) check(!c->cost().empty(), "a blueprint adds up its parts' costs");

    std::printf("a village\n");
    {
        World w;
        WorldEditor ed(w.chunks);
        TerrainGenerator gen(1);
        StructureManager sm(w.chunks, ed, gen);
        ChestStore chests;
        // a gentle hill on one side
        EditBatch hill;
        for (int z = 300; z < 420; z++) for (int x = 300; x < 420; x++) {
            int h = std::max(0, 6 - (std::abs(x - 360) + std::abs(z - 360)) / 10);
            for (int y = 0; y < h; y++) hill.set(x, GROUND + y, z, BLOCK_DIRT);
        }
        ed.apply(std::move(hill));
        village::Options o;
        o.seed = 3; o.roads = 4;
        o.style.materials = { { "roof", "tile" } };
        // the foundations sit 2 ft into the ground
        const glm::ivec3 C(256, GROUND, 256);
        village::Plan p = village::plan(w.chunks, lib, C, o);
        check(!p.lots.empty() && p.lots[0].bp->hasTag("plaza"), "a square in the middle");
        check(p.roads.size() == 4, "four roads out of it");
        check(p.lots.size() >= 8, "buildings along the roads: " + std::to_string(p.lots.size() - 1));
        bool facing = true;
        for (size_t i = 1; i < p.lots.size(); i++) {
            const village::Lot& l = p.lots[i];
            // the front's middle should be the side nearest a road
            glm::ivec2 mid(l.lo.x + l.size.x / 2, l.lo.z + l.size.z / 2);
            const glm::ivec2 dir[4] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
            int fd = l.bp->frontTurned(l.rot);
            glm::ivec2 f = mid + dir[fd] * ((fd & 1 ? l.size.x : l.size.z) / 2 + o.setback + o.roadWidth / 2 + 1);
            bool near = false;
            for (const village::Road& r : p.roads)
                near = near || (f.x >= r.lo().x - 2 && f.x <= r.hi().x + 2 && f.y >= r.lo().y - 2 && f.y <= r.hi().y + 2);
            if (!near) std::printf("    %s rot %d front %d at %d,%d size %d,%d\n", l.bp->name.c_str(), l.rot, fd, l.lo.x, l.lo.z, l.size.x, l.size.z);
            facing = facing && near;
        }
        check(facing, "every building faces a road");
        village::Built b = village::build(p, sm, ed, &chests, o, EditSource::Command);
        check(b.buildings == (int)p.lots.size() && b.failed == 0,
              "all built: " + std::to_string(b.buildings) + " buildings, " + std::to_string(b.pieces) + " pieces" +
              (b.failed ? ", " + std::to_string(b.failed) + " failed: " + b.why : std::string()));
        const village::Road& r = p.roads[0];
        glm::ivec2 mid = (r.a + r.b) / 2;
        int lv = terrain::groundLevel(w.chunks, mid.x, mid.y, GROUND + 60);
        check(lv > 0 && w.chunks.typeAtVoxel(mid.x, lv - 1, mid.y) == BLOCK_GRAVEL, "the roads are laid");
        int tiles = 0;
        sm.forEachInstance([&](const StructureInstance& s) { tiles += sm.typeOf(s)->material == "tile"; });
        check(tiles > 0, "the village style: tile roofs");
        if (const char* dump = std::getenv("VOXELRPG_DUMP")) {
            std::ofstream out(std::string(dump) + "/village.txt");
            for (int y = GROUND - 2; y < GROUND + 80; y++) for (int z = 96; z < 416; z++) for (int x = 96; x < 416; x++)
                if (uint16_t id = w.chunks.typeAtVoxel(x, y, z)) {
                    bool hidden = y < GROUND + 12 && w.chunks.typeAtVoxel(x, y + 1, z) && w.chunks.typeAtVoxel(x + 1, y, z) &&
                                  w.chunks.typeAtVoxel(x, y, z + 1);
                    if (!hidden) out << x - 256 << " " << y - GROUND << " " << z - 256 << " " << id << "\n";
                }
        }
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
