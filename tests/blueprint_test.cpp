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
#include "game/Palettes.h"
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

    // A piece's own sizes as the hammer's Pieces tab would set them
    // ("h=48;ow=12"), kept to what works
    static std::string specOf(uint16_t type, const std::string& spec) {
        const StructureType* t = gStructures.get(type);
        return t && !spec.empty() ? gStructures.normalize(*t, PieceSpec::parse(spec)) : std::string();
    }
    StructureInstance* put(const std::string& name, glm::ivec3 aimed, glm::ivec3 normal = { 0, 1, 0 },
                           glm::vec3 look = { 0, 0, 1 }, int rotPick = 0, const std::string& spec0 = "") {
        uint16_t type = gStructures.find(name);
        if (!type) { lastWhy = "no type " + name; std::printf("    (no type %s)\n", name.c_str()); return nullptr; }
        glm::ivec3 spot; int rot;
        std::string spec = specOf(type, spec0);
        if (!sm.snapTo(type, rotPick, aimed, normal, look, spot, rot, &spec)) { lastWhy = "nothing to snap to"; std::printf("    (%s: nothing to snap to)\n", name.c_str()); return nullptr; }
        return finish(name, type, spot, rot, spec);
    }
    // Set down exactly here (low corner), turned rot: what the hammer
    // does once a piece is locked and nudged with the arrow keys
    StructureInstance* at(const std::string& name, glm::ivec3 lo, int rot = 0, const std::string& spec0 = "") {
        uint16_t type = gStructures.find(name);
        if (!type) { lastWhy = "no type " + name; std::printf("    (no type %s)\n", name.c_str()); return nullptr; }
        std::string spec = specOf(type, spec0);
        return finish(name, type, lo + gStructures.shape(type, spec, 0, rot & 3).anchor, rot & 3, spec);
    }
    // The turn that points a piece's front (its unturned -z side: a
    // chair's seat, a wardrobe's doors) toward dir
    static int facing(glm::ivec2 dir) {
        for (int q = 0; q < 4; q++) if (StructureManager::outsideDir(q) == dir) return q;
        return 0;
    }
    StructureInstance* finish(const std::string& name, uint16_t type, glm::ivec3 spot, int rot, const std::string& spec) {
        uint32_t sup = 0;
        lastWhy = sm.checkPlace(type, spot, rot, true, &sup, spec);
        if (!lastWhy.empty()) {
            const VoxelVolume& v = gStructures.shape(type, spec, 0, rot);
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
        StructureInstance* s = sm.place(type, spot, rot, EditSource::Command, sup, spec);
        ed.flush();
        placed++;
        return s;
    }
    StructureInstance* foundation(const std::string& name, glm::ivec3 at, const std::string& spec0 = "") {
        uint16_t type = gStructures.find(name);
        std::string spec = specOf(type, spec0);
        if (!sm.checkPlace(type, at, 0, true, nullptr, spec).empty()) return nullptr;
        StructureInstance* s = sm.place(type, at, 0, EditSource::Command, 0, spec);
        ed.flush();
        placed++;
        return s;
    }
    struct Box { glm::ivec3 lo, hi; };
    Box box(const StructureInstance* s) const { Box b; sm.bounds(*s, b.lo, b.hi); return b; }

    // A wall-like piece on one edge of a floor or foundation top:
    // side 0 north, 1 east, 2 south, 3 west; 'along' voxels from
    // the edge's low end
    StructureInstance* edge(const std::string& name, const StructureInstance* base, int side, int along,
                            const std::string& spec = "") {
        Box b = box(base);
        glm::ivec3 a(0, b.hi.y, 0);
        switch (side) {
        case 0: a.x = b.lo.x + along; a.z = b.lo.z + 1; break;
        case 1: a.x = b.hi.x - 1;     a.z = b.lo.z + along; break;
        case 2: a.x = b.lo.x + along; a.z = b.hi.z - 1; break;
        default: a.x = b.lo.x + 1;    a.z = b.lo.z + along; break;
        }
        return put(name, a, { 0, 1, 0 }, { 0, 0, 1 }, 0, spec);
    }
    // Something set down on a floor at (x, z) from its low corner
    StructureInstance* on(const std::string& name, const StructureInstance* base, int x, int z, int rotPick = 0,
                          const std::string& spec = "") {
        Box b = box(base);
        return put(name, { b.lo.x + x, b.hi.y, b.lo.z + z }, { 0, 1, 0 }, { 0, 0, 1 }, rotPick, spec);
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
    StructureInstance* ceilingOn(const std::string& name, const StructureInstance* wall, const std::string& spec = "") {
        Box b = box(wall);
        return put(name, { b.lo.x + 3, b.hi.y, b.lo.z + 1 }, { 0, 1, 0 }, { 0, 0, 1 }, 0, spec);
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

StructureInstance* fillChest(StructureInstance* c, ChestStore& chests, std::vector<std::pair<std::string, int>> items,
                             const std::string& label);
StructureInstance* chestWith(Builder& B, const StructureInstance* floor, int x, int z, ChestStore& chests,
                             std::vector<std::pair<std::string, int>> items, const std::string& label = "") {
    return fillChest(B.on("wooden_chest", floor, x, z), chests, items, label);
}
// A chest set down exactly at lo, turned rot
StructureInstance* chestAt(Builder& B, glm::ivec3 lo, int rot, ChestStore& chests,
                           std::vector<std::pair<std::string, int>> items, const std::string& label = "") {
    return fillChest(B.at("wooden_chest", lo, rot), chests, items, label);
}
StructureInstance* fillChest(StructureInstance* c, ChestStore& chests, std::vector<std::pair<std::string, int>> items,
                             const std::string& label) {
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
    ok = ok && B.put("wall_sconce", { A.x + 12, A.y + 32, A.z + 1 }, { 0, 0, 1 }) &&
         B.put("wall_lantern", { A.x + 3, A.y + 30, A.z + 23 }, { 0, 0, 1 });
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
    ok = ok && B.on("hearth", f1, 12, 12) && B.on("table", f2, 9, 9) && B.on("bench", f2, 9, 3);
    ok = ok && loot(B.on("wooden_chest", f2, 19, 9, 1), chests, "village_house");
    ok = ok && B.put("wall_lantern", { A.x + 1, A.y + 30, A.z + 12 }, { 1, 0, 0 }) &&
         B.put("wall_lantern", { A.x + 27, A.y + 30, A.z + 23 }, { 0, 0, 1 });
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
    // (the back room's door swings in: the workbench and chest stand clear of it)
    ok = ok && B.on("forge", front, 7, 16) && B.on("anvil", front, 17, 16) && B.on("workbench", back, 12, 5);
    StructureInstance* ch = B.on("wooden_chest", back, 5, 14, 1);
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


// ── Church and homes (furnished) ──────────────────────────
// Built with plain walls (or the arched and stained glass ones): the
// palette a village gives them (Chapel Stone for the church) sets
// every finish and block.

// A church, 24 x 40 ft on fifteen 8 ft foundations: 16 ft walls,
// stained glass down both sides and a tall window behind the altar,
// an arched door, one ceiling and a slate roof over a 24 ft span.
// Inside: a dais with the altar, candles and a lectern, two rows of
// pews either side of a red runner
StructureInstance* church(Builder& B, ChestStore&, glm::ivec3 A) {
    StructureInstance* F[5][3] = {};
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (!(F[r][c] = B.foundation("stone_foundation", A + glm::ivec3(24 * c, 0, 24 * r)))) return nullptr;
    const std::string H = "h=48", LANCET = "h=48;ow=10;oh=33;sill=9";
    StructureInstance* nw = B.edge("stone_wall_8", F[0][0], 0, 12, H);
    bool ok = nw && B.edge("stone_lancet_8", F[0][1], 0, 12, "h=48;ow=12;oh=36;sill=6") &&
              B.edge("stone_wall_8", F[0][2], 0, 12, H);
    for (int r = 0; r < 5; r++) {                       // stained glass in four bays, plain by the door
        const char* piece = r < 4 ? "stone_lancet_8" : "stone_wall_8";
        ok = ok && B.edge(piece, F[r][0], 3, 12, r < 4 ? LANCET : H) && B.edge(piece, F[r][2], 1, 12, r < 4 ? LANCET : H);
    }
    StructureInstance* dw = B.edge("stone_arched_doorway_8", F[4][1], 2, 12, "h=48;ow=16;oh=36");
    ok = ok && dw && B.edge("stone_wall_8", F[4][0], 2, 12, H) && B.edge("stone_wall_8", F[4][2], 2, 12, H);
    ok = ok && B.door("dark_ledged_door", dw);
    StructureInstance* c = nw ? B.ceilingOn("plank_floor_8", nw, "w=72;d=120") : nullptr;
    ok = ok && c && B.on("slate_roof_8x12", c, 2, 60, 0, "w=120") && B.on("slate_roof_8x12", c, 69, 60, 0, "w=120");
    ok = ok && B.edge("stone_gable_24", c, 0, 36) && B.edge("stone_gable_24", c, 2, 36);
    if (!ok) return nullptr;
    // the chancel: a dais, the altar facing the pews, candles, a lectern
    StructureInstance* dais = B.on("chancel_dais", F[0][1], 12, 9);
    ok = dais && B.on("altar", dais, 24, 11, B.facing({ 0, 1 })) && B.on("candle_stand", dais, 4, 7) &&
         B.on("candle_stand", dais, 44, 7) && B.on("lectern", dais, 6, 12, B.facing({ 0, 1 }));
    // pews either side of the aisle, facing the altar; the runner up the middle
    const glm::ivec3 floor = A + glm::ivec3(0, 12, 0);
    for (int k = 0; k < 8 && ok; k++)
        ok = B.at("oak_pew", floor + glm::ivec3(3, 0, 26 + 9 * k)) && B.at("oak_pew", floor + glm::ivec3(45, 0, 26 + 9 * k));
    // (clear of the door's swing: the last 6 yards inside it stay bare)
    for (int k = 0; k < 2 && ok; k++) ok = B.at("aisle_runner", floor + glm::ivec3(33, 0, 20 + 30 * k));
    ok = ok && B.at("candle_stand", floor + glm::ivec3(28, 0, 96)) && B.at("candle_stand", floor + glm::ivec3(41, 0, 96));
    // chandeliers down the nave, sconces by the door
    const int under = c->origin.y - 1;
    for (int k = 0; k < 3 && ok; k++) ok = B.put("chandelier", { A.x + 36, under + 1, A.z + 30 + 30 * k }, { 0, -1, 0 }) != nullptr;
    ok = ok && B.put("wall_sconce", { A.x + 1, A.y + 30, A.z + 108 }, { 1, 0, 0 }) &&
         B.put("wall_sconce", { A.x + 70, A.y + 30, A.z + 108 }, { -1, 0, 0 });
    return ok ? F[0][0] : nullptr;
}

// A family home, 16 x 16 ft: four foundations, a door and windows on
// every side, one ceiling, a thatch roof with gable ends. Bedroom at
// the back (a double bed, a child's bed, nightstand, bookcase,
// wardrobe, a chest), the hearth, table and chairs at the front
StructureInstance* familyHome(Builder& B, ChestStore& chests, glm::ivec3 A) {
    StructureInstance* F[2][2] = {};
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 2; c++)
            if (!(F[r][c] = B.foundation("stone_foundation", A + glm::ivec3(24 * c, 0, 24 * r)))) return nullptr;
    StructureInstance* n = B.edge("plank_wall_8", F[0][0], 0, 12);
    bool ok = n && B.edge("plank_window_8", F[0][1], 0, 12) && B.edge("plank_window_8", F[0][0], 3, 12) &&
              B.edge("plank_wall_8", F[1][0], 3, 12) && B.edge("plank_wall_8", F[0][1], 1, 12) &&
              B.edge("plank_window_8", F[1][1], 1, 12) && B.edge("plank_window_8", F[1][1], 2, 12);
    StructureInstance* dw = B.edge("plank_doorway_8", F[1][0], 2, 12);
    ok = ok && dw && B.door("plank_ledged_door", dw);
    StructureInstance* c = n ? B.ceilingOn("plank_floor_8", n, "w=48;d=48") : nullptr;
    ok = ok && c && B.on("thatch_roof_8x8", c, 24, 1, 0, "w=48") && B.on("thatch_roof_8x8", c, 24, 46, 0, "w=48");
    ok = ok && B.edge("plank_gable_16", c, 3, 24) && B.edge("plank_gable_16", c, 1, 24);
    if (!ok) return nullptr;
    const glm::ivec3 fl = A + glm::ivec3(0, 12, 0);
    const int south = B.facing({ 0, 1 }), north = B.facing({ 0, -1 }), west = B.facing({ -1, 0 });
    // the bedroom
    ok = B.at("red_double_bed", fl + glm::ivec3(3, 0, 2), 2) && B.at("nightstand", fl + glm::ivec3(16, 0, 2)) &&
         B.at("bookcase", fl + glm::ivec3(20, 0, 2), south) && B.at("blue_bed", fl + glm::ivec3(37, 0, 2), 2) &&
         B.at("wardrobe", fl + glm::ivec3(41, 0, 21), west);
    ok = ok && chestAt(B, fl + glm::ivec3(5, 0, 20), 0, chests, { { "planks", 8 }, { "torch", 4 } }, "Linen chest");
    ok = ok && B.at("large_rug", fl + glm::ivec3(18, 0, 20));
    // the hearth, table and chairs
    StructureInstance* table = B.at("table", fl + glm::ivec3(4, 0, 31));
    ok = ok && table && B.at("hearth", fl + glm::ivec3(30, 0, 30)) &&
         B.at("oak_chair", fl + glm::ivec3(6, 0, 27), south) && B.at("oak_chair", fl + glm::ivec3(12, 0, 27), south) &&
         B.at("bench", fl + glm::ivec3(4, 0, 40)) && B.on("table_candle", table, 9, 4) &&
         B.at("barrel", fl + glm::ivec3(25, 0, 42)) && B.at("crate", fl + glm::ivec3(20, 0, 42));
    // lights on the walls: a lantern by the table, a sconce over the bed,
    // a lantern outside by the door
    ok = ok && B.put("wall_lantern", { A.x + 1, A.y + 30, A.z + 34 }, { 1, 0, 0 }) &&
         B.put("wall_sconce", { A.x + 9, A.y + 34, A.z + 1 }, { 0, 0, 1 }) &&
         B.put("wall_lantern", { A.x + 3, A.y + 30, A.z + 47 }, { 0, 0, 1 });
    (void)north;
    return ok ? F[0][0] : nullptr;
}

// A two-storey townhouse, 16 x 12 ft on one foundation: a kitchen
// below (hearth, table, barrels), stairs along the back wall, a
// bedroom above under a tile roof with gable ends
StructureInstance* townhouse(Builder& B, ChestStore& chests, glm::ivec3 A) {
    StructureInstance* f = B.foundation("stone_foundation", A, "w=48;d=36");
    if (!f) return nullptr;
    const std::string W4 = "w=12";
    StructureInstance* n = B.edge("plank_wall_8", f, 0, 12);
    bool ok = n && B.edge("plank_window_8", f, 0, 36) && B.edge("plank_window_8", f, 2, 36) &&
              B.edge("plank_wall_8", f, 3, 12) && B.edge("plank_wall_8", f, 3, 30, W4) &&
              B.edge("plank_window_8", f, 1, 12) && B.edge("plank_wall_8", f, 1, 30, W4);
    StructureInstance* dw = B.edge("plank_doorway_8", f, 2, 12);
    ok = ok && dw && B.door("plank_ledged_door", dw);
    if (!ok) return nullptr;
    // stairs up the back, rising west; the upper floor in four pieces
    // round the stairwell
    const glm::ivec3 g = A + glm::ivec3(0, 12, 0), up = A + glm::ivec3(0, 42, 0);
    ok = B.at("plank_stairs", g + glm::ivec3(3, 0, 3), B.facing({ 1, 0 })) != nullptr;   // the low end east
    StructureInstance* P1 = B.at("plank_floor_8", up + glm::ivec3(0, 0, 15), 0, "w=48;d=21");
    // (the stairwell runs to x 30: a full body's headroom over every step)
    ok = ok && P1 && B.at("plank_floor_8", up + glm::ivec3(30, 0, 0), 0, "w=18;d=15") &&
         B.at("plank_floor_8", up + glm::ivec3(0, 0, 0), 0, "w=24;d=3") &&
         B.at("plank_floor_8", up + glm::ivec3(0, 0, 3), 0, "w=3;d=12");
    // the upper storey's walls, on the floor's edges
    const glm::ivec3 u = up + glm::ivec3(0, 2, 0);
    ok = ok && B.at("plank_wall_8", u) && B.at("plank_window_8", u + glm::ivec3(24, 0, 0)) &&
         B.at("plank_window_8", u + glm::ivec3(0, 0, 34)) && B.at("plank_wall_8", u + glm::ivec3(24, 0, 34)) &&
         B.at("plank_wall_8", u, 1) && B.at("plank_wall_8", u + glm::ivec3(0, 0, 24), 1, W4) &&
         B.at("plank_window_8", u + glm::ivec3(46, 0, 0), 1) && B.at("plank_wall_8", u + glm::ivec3(46, 0, 24), 1, W4);
    // the roof: a ceiling, two slopes over 12 ft, gable ends
    StructureInstance* c = ok ? B.at("plank_floor_8", u + glm::ivec3(0, 30, 0), 0, "w=48;d=36") : nullptr;
    ok = ok && c && B.on("tile_roof_8x6", c, 24, 1, 0, "w=48") && B.on("tile_roof_8x6", c, 24, 34, 0, "w=48") &&
         B.edge("plank_gable_12", c, 3, 18) && B.edge("plank_gable_12", c, 1, 18);
    if (!ok) return nullptr;
    const int south = B.facing({ 0, 1 }), north = B.facing({ 0, -1 });
    // the kitchen
    // (the table stands clear of the front wall: room to walk past it)
    StructureInstance* table = B.at("table", g + glm::ivec3(6, 0, 19));
    ok = table && B.at("hearth", g + glm::ivec3(31, 0, 17)) && B.at("oak_chair", g + glm::ivec3(8, 0, 15), south) &&
         B.at("oak_chair", g + glm::ivec3(13, 0, 15), south) && B.at("oak_stool", g + glm::ivec3(2, 0, 23)) &&
         B.on("table_candle", table, 6, 4) && B.at("barrel", g + glm::ivec3(41, 0, 30)) &&
         B.at("barrel", g + glm::ivec3(36, 0, 30)) && B.at("crate", g + glm::ivec3(25, 0, 30));
    // the bedroom
    ok = ok && B.at("green_double_bed", u + glm::ivec3(31, 0, 2), 2) && B.at("nightstand", u + glm::ivec3(26, 0, 16)) &&
         B.at("wardrobe", u + glm::ivec3(6, 0, 29), north) && B.at("bookcase", u + glm::ivec3(34, 0, 31), north) &&
         B.at("rug", u + glm::ivec3(18, 0, 26));
    ok = ok && chestWith(B, P1, 18, 6, chests, { { "planks", 6 }, { "torch", 2 } }, "Clothes chest");
    // a lantern in the kitchen, a sconce in the bedroom
    ok = ok && B.put("wall_lantern", { A.x + 1, A.y + 28, A.z + 30 }, { 1, 0, 0 }) &&
         B.put("wall_lantern", { A.x + 3, A.y + 30, A.z + 35 }, { 0, 0, 1 }) &&
         B.put("wall_sconce", { A.x + 30, A.y + 62, A.z + 34 }, { 0, 0, -1 });
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
        // the hoe's shapes
        terrain::FlattenOptions ho;
        ed.apply(terrain::shiftDisc(w.chunks, 400, 400, 6.0f, 1, GROUND + 20, ho, EditSource::Player));
        check(terrain::groundLevel(w.chunks, 400, 400, GROUND + 20) == GROUND + 1 &&
              terrain::groundLevel(w.chunks, 410, 400, GROUND + 20) == GROUND, "the hoe raises a step in the middle, not past its edge");
        ed.apply(terrain::shiftDisc(w.chunks, 430, 400, 6.0f, -1, GROUND + 20, ho, EditSource::Player));
        check(terrain::groundLevel(w.chunks, 430, 400, GROUND + 20) == GROUND - 1, "and lowers one");
        ed.apply(terrain::paintDisc(w.chunks, 460, 400, 3.0f, BLOCK_GRAVEL, GROUND + 20, ho, EditSource::Player));
        check(w.chunks.typeAtVoxel(460, GROUND - 1, 400) == BLOCK_GRAVEL && terrain::groundLevel(w.chunks, 460, 400, GROUND + 20) == GROUND,
              "and lays a path where the ground is");
    }

    std::vector<Design> designs = {
        { "cottage_small", "Small Cottage", { "house", "village", "small" }, smallCottage },
        { "longhouse",     "Longhouse",     { "hall", "village", "large" }, longhouse },
        { "smithy",        "Smithy",        { "smithy", "village", "work" }, smithy },
        { "storehouse",    "Storehouse",    { "storage", "village" },        storehouse },
        { "hut_workshop",  "Workshop Hut",  { "work", "village", "small" },  hut },
        { "village_square", "Village Square", { "plaza", "village" },        square },
        { "church",        "Church",        { "church", "village", "large" }, church },
        { "home_family",   "Family Home",   { "house", "home", "village", "large" }, familyHome },
        { "townhouse",     "Townhouse",     { "house", "home", "village" }, townhouse },
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
        // every door swings open and shut (nothing in its way: villagers use them)
        {
            std::vector<uint32_t> doors;
            sm.forEachInstance([&](const StructureInstance& s) { if (sm.typeOf(s)->kind == StructureKind::Door) doors.push_back(s.id); });
            std::string why;
            for (uint32_t id : doors) {
                StructureInstance* dr = sm.get(id);
                std::string a2 = sm.toggleDoor(*dr, EditSource::Script, nullptr);
                std::string b2 = a2.empty() ? sm.toggleDoor(*dr, EditSource::Script, nullptr) : a2;
                if (!b2.empty() && why.empty()) why = b2;
            }
            ed.flush();
            if (!doors.empty()) check(why.empty(), "  its doors swing" + (why.empty() ? std::string() : ": " + why));
        }

        // VOXELRPG_DUMP=<folder>: each building's voxels as text
        // ("x y z block" lines) for a picture
        if (const char* dump = std::getenv("VOXELRPG_DUMP")) {
            std::ofstream out(std::string(dump) + "/" + d.name + ".txt");
            for (int y = A.y - 2; y < A.y + 110; y++) for (int z = A.z - 4; z < A.z + 130; z++) for (int x = A.x - 4; x < A.x + 80; x++)
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

        // VOXELRPG_DUMP with a palette as a village would dress it
        // (the church in Chapel Stone, the rest in VOXELRPG_PALETTE or Tudor)
        if (const char* dump = std::getenv("VOXELRPG_DUMP")) {
            gBuildPalettes.load();
            const char* want = std::getenv("VOXELRPG_PALETTE");
            const BuildPalette* pal = gBuildPalettes.find(d.name == std::string("church") ? "chapel_stone" : want ? want : "tudor");
            World w3;
            WorldEditor ed3(w3.chunks);
            StructureManager sm3(w3.chunks, ed3, gen);
            BlueprintStyle st;
            if (pal) { st.palette = pal->spec; st.paletteName = pal->name; }
            back.paste(sm3, ed3, nullptr, back.plan(A, 0, &st), EditSource::Command, &st);
            ed3.flush();
            std::ofstream out(std::string(dump) + "/" + d.name + "_styled.txt");
            for (int y = A.y - 2; y < A.y + 110; y++) for (int z = A.z - 4; z < A.z + 130; z++) for (int x = A.x - 4; x < A.x + 80; x++)
                if (uint16_t id = w3.chunks.typeAtVoxel(x, y, z)) out << x - A.x << " " << y - A.y << " " << z - A.z << " " << id << "\n";
        }

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
        gBuildPalettes.load(PaletteLibrary::REGISTRY, "/nonexistent");
        village::Options o;
        o.seed = 3; o.roads = 4;
        o.style.materials = { { "roof", "tile" } };
        // the foundations sit 2 ft into the ground
        const glm::ivec3 C(256, GROUND, 256);
        village::Plan p = village::plan(w.chunks, lib, C, o);
        check(!p.lots.empty() && p.lots[0].bp->hasTag("plaza"), "a square in the middle");
        check(p.mainRoads == 4, "four roads out of it");
        check((int)p.roads.size() > p.mainRoads, "lanes off them: " + std::to_string(p.roads.size() - p.mainRoads));
        bool church = false;
        for (const village::Lot& l : p.lots) church = church || l.bp->hasTag("church");
        check(church, "a church");
        check(!p.paths.empty(), "paths from doors to the roads: " + std::to_string(p.paths.size()));
        int wells = 0, posts = 0;
        for (const village::Decor& d : p.decor) { wells += d.type == "well"; posts += d.type == "lantern_post"; }
        check(wells == 1 && posts >= 4, "a well and lantern posts: " + std::to_string(posts));
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
        check(b.decor + b.decorSkipped == (int)p.decor.size() && b.decor >= (int)p.decor.size() - 2,
              "the well and posts set down: " + std::to_string(b.decor) + " of " + std::to_string(p.decor.size()));
        int chapel = 0;
        sm.forEachInstance([&](const StructureInstance& s) {
            const StructureType* t = sm.typeOf(s);
            if (t && t->part == "wall_window" && s.spec.find("limestone") != std::string::npos) chapel++;
        });
        check(chapel > 0, "the church in its own palette (Chapel Stone)");
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

    std::printf("on a slope\n");
    {
        // a house on a hillside: the foundations sink in uphill and show
        // downhill; the ground between their underside and the floor isn't
        // touched
        World w;
        WorldEditor ed(w.chunks);
        TerrainGenerator gen(1);
        StructureManager sm(w.chunks, ed, gen);
        EditBatch hill;
        for (int z = 80; z < 340; z++) for (int x = 80; x < 240; x++) {
            int h = std::max(0, (x - 80) / 6);                 // rises 1 in 6 toward +x (8 over a house)
            for (int y = 0; y < h; y++) hill.set(x, GROUND + y, z, BLOCK_DIRT);
        }
        ed.apply(std::move(hill));
        const Blueprint* home = lib.find("cottage_small");
        check(home != nullptr && home->foundationHeight() == 12, "a cottage's foundations are 4 ft (12)");
        if (home) {
            glm::ivec3 at(130, 0, 300);
            int gmin = terrain::groundLevel(w.chunks, at.x, at.z + 12, GROUND + 60), gmax = terrain::groundLevel(w.chunks, at.x + 23, at.z + 12, GROUND + 60);
            at.y = gmax - 12;                                   // the floor at the high side
            auto plan = home->plan(at, 0);
            check(Blueprint::floorOf(plan) == gmax, "  its floor is the top of its foundations");
            std::string why;
            auto made = home->paste(sm, ed, nullptr, plan, EditSource::Command, nullptr, &why);
            check(made.size() == home->parts.size(), "  it all goes down" + (why.empty() ? std::string() : ": " + why));
            int lowSide = terrain::groundLevel(w.chunks, at.x - 1, at.z + 12, GROUND + 60);
            check(lowSide >= gmin - 1 && lowSide <= gmin + 1, "  the low side's ground isn't built up (" + std::to_string(lowSide - GROUND) + ")");
            int highSide = terrain::groundLevel(w.chunks, at.x + 24, at.z + 12, GROUND + 60);
            check(highSide >= gmax - 1 && highSide <= gmax + 2, "  nor the high side cut down");
            check(sm.instanceAt({ at.x + 1, gmin + 1, at.z + 12 }) != nullptr, "  the foundation shows on the low side");
        }
        // a village over the hill: roads climb evenly, paths reach the doors
        village::Options o;
        o.seed = 5; o.roads = 2; o.lanes = 0; o.church = false;
        village::Plan p = village::plan(w.chunks, lib, { 150, GROUND, 150 }, o);
        village::Built b = village::build(p, sm, ed, nullptr, o, EditSource::Command);
        check(b.buildings >= 2 && b.failed == 0, "a village on the slope: " + std::to_string(b.buildings) + " buildings" +
              (b.failed ? ", " + std::to_string(b.failed) + " failed: " + b.why : std::string()));
        int worst = 0;
        for (int i = 0; i < p.mainRoads; i++) {
            const village::Road& r = p.roads[(size_t)i];
            glm::ivec2 d = glm::sign(r.b - r.a);
            int len = std::max(std::abs(r.b.x - r.a.x), std::abs(r.b.y - r.a.y));
            int prev = -1;
            for (int k = 0; k <= len; k++) {
                glm::ivec2 c = r.a + d * k;
                int g = terrain::groundLevel(w.chunks, c.x, c.y, GROUND + 80);
                if (prev >= 0) worst = std::max(worst, std::abs(g - prev));
                prev = g;
            }
        }
        check(worst <= 1, "the roads climb without steps (worst step " + std::to_string(worst) + ")");
        int doorsteps = 0, reached = 0;
        for (const village::Road& path : p.paths) {
            doorsteps++;
            int g = terrain::groundLevel(w.chunks, path.a.x, path.a.y, GROUND + 80);
            reached += std::abs(g - path.startLevel) <= 1;
        }
        check(doorsteps > 0 && reached == doorsteps, "every path starts at its doorstep: " + std::to_string(reached) + " of " + std::to_string(doorsteps));
        if (const char* dump = std::getenv("VOXELRPG_DUMP")) {
            std::ofstream out(std::string(dump) + "/slope_village.txt");
            for (int y = GROUND - 2; y < GROUND + 90; y++) for (int z = 40; z < 300; z++) for (int x = 60; x < 260; x++)
                if (uint16_t id = w.chunks.typeAtVoxel(x, y, z)) {
                    bool hidden = w.chunks.typeAtVoxel(x, y + 1, z) && w.chunks.typeAtVoxel(x + 1, y, z) && w.chunks.typeAtVoxel(x, y, z + 1) &&
                                  w.chunks.typeAtVoxel(x - 1, y, z) && w.chunks.typeAtVoxel(x, y, z - 1);
                    if (!hidden) out << x << " " << y - GROUND << " " << z << " " << id << "\n";
                }
        }
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
