// Headless checks for the building parts in structures.toml: material
// variants and swaps, foundation sizes, wall headroom, and building a
// whole house with the hammer's snapping (StructureManager::snapTo),
// shared wall corners and removal. Run from the repo root.
#include "TestWorld.h"
#include "world/StructureManager.h"
#include "world/WorldEditor.h"
#include <cstdio>

using namespace testworld;

int main() {
    if (!loadData() || !gStructures.load("data/registry/structures.toml")) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }
    auto F = [](const char* n) { return gStructures.find(n); };

    std::printf("parts and materials\n");
    const uint16_t pw = F("plank_wall_8"), sw = F("stone_wall_8");
    check(pw && sw, "plank and stone 8 ft walls exist");
    check(gStructures.swap(pw, "stone") == sw, "a plank wall swaps to the stone one");
    check(gStructures.swap(F("thatch_roof_8x4"), "tile") == F("tile_roof_8x4"), "thatch roof swaps to tile");
    check(gStructures.swap(F("stone_foundation"), "slate") == F("slate_foundation"), "stone foundation swaps to slate");
    check(gStructures.swap(pw, "nothing") == 0, "no such material: no swap");
    check(gStructures.get(pw)->group == "wall" && gStructures.get(F("plank_doorway_8"))->group == "wall",
          "walls and doorways are both in the wall group");
    check(!gStructures.withPart("roof", "slate").empty(), "slate roofs listed by part");
    check(!gStructures.charges(*gStructures.get(pw)), "building is free while costs are off");

    std::printf("foundations: 4 ft deep, 2 to 10 ft square\n");
    const char* fnd[] = { "stone_foundation_2", "stone_foundation_4", "stone_foundation_6", "stone_foundation", "stone_foundation_10" };
    for (int i = 0; i < 5; i++) {
        const StructureType* t = gStructures.get(F(fnd[i]));
        int s = 6 * (i + 1);
        check(t && t->foundation && t->shape(0, 0).size == glm::ivec3(s, 12, s), std::string(fnd[i]) + " is " + std::to_string(s / 3) + " ft square");
    }

    std::printf("headroom\n");
    check(gStructures.get(pw)->shape(0, 0).size.y == 30, "walls are 10 ft tall");
    {
        World w0; WorldEditor e0(w0.chunks); TerrainGenerator g0(1); StructureManager s0(w0.chunks, e0, g0);
        StructureInstance d; d.type = F("plank_doorway_8"); d.origin = gStructures.get(d.type)->shape(0, 0).anchor;
        glm::ivec3 lo, hi;
        check(s0.holeOf(d, lo, hi) && hi.y - lo.y + 1 == 24 && hi.x - lo.x + 1 == 10, "the doorway is 8 ft tall, 3 ft 4 in wide");
    }

    std::printf("a house, by snapping\n");
    World w;
    WorldEditor ed(w.chunks);
    TerrainGenerator gen(1);
    StructureManager sm(w.chunks, ed, gen);
    const glm::ivec3 A(100, GROUND, 100);
    const glm::vec3 lookN(0, 0, 1);
    int placed = 0;
    auto put = [&](uint16_t type, glm::ivec3 aimed, glm::ivec3 normal, glm::vec3 look, int rotPick = 0) -> StructureInstance* {
        glm::ivec3 spot; int rot;
        if (!sm.snapTo(type, rotPick, aimed, normal, look, spot, rot)) return nullptr;
        uint32_t sup = 0;
        std::string why = sm.checkPlace(type, spot, rot, true, &sup);
        if (!why.empty()) { std::printf("    (%s: %s)\n", gStructures.get(type)->name.c_str(), why.c_str()); return nullptr; }
        StructureInstance* s = sm.place(type, spot, rot, EditSource::Command, sup);
        ed.flush();
        placed++;
        return s;
    };
    const uint16_t FD = F("stone_foundation");
    check(sm.checkPlace(FD, A, 0, true).empty(), "foundation fits");
    StructureInstance* fd = sm.place(FD, A, 0, EditSource::Command, 0);
    ed.flush();
    glm::ivec3 lo, hi;
    sm.bounds(*fd, lo, hi);
    const int top = hi.y;
    const glm::ivec3 up(0, 1, 0);

    StructureInstance* n = put(pw, { lo.x + 12, top, lo.z + 1 }, up, lookN);
    check(n != nullptr, "a wall on the north edge");
    glm::ivec3 nlo, nhi;
    if (n) sm.bounds(*n, nlo, nhi);
    check(n && nlo == glm::ivec3(lo.x, top + 1, lo.z) && nhi.x == hi.x && nhi.z == lo.z + 1, "  sits flush on the edge, full length");
    StructureInstance* wW = put(pw, { lo.x + 1, top, lo.z + 12 }, up, lookN);
    check(wW && wW->rot % 2 == 1, "a wall on the west edge turns itself");
    check(put(pw, { hi.x - 1, top, lo.z + 12 }, up, lookN) != nullptr, "the east wall shares its corners");
    StructureInstance* dw = put(F("plank_doorway_8"), { lo.x + 12, top, hi.z - 1 }, up, lookN);
    check(dw != nullptr, "a doorway on the south edge");
    glm::ivec3 hlo, hhi;
    StructureInstance* door = nullptr;
    if (dw) {                                        // aim at the doorway's frame
        glm::ivec3 frame, dhi; sm.bounds(*dw, frame, dhi);
        door = put(F("plank_door"), frame + glm::ivec3(3, 0, 0), glm::ivec3(0, 0, 1), lookN);   // off the shared corner
    }
    check(door != nullptr, "a door snaps into the doorway");
    if (door && dw) {
        const VoxelVolume& dv = gStructures.get(door->type)->shape(0, door->rot);   // closed
        glm::ivec3 dlo = door->origin - dv.anchor, dhi = dlo + dv.size - 1; sm.holeOf(*dw, hlo, hhi);
        check(dlo.y == hlo.y && dlo.x >= hlo.x && dhi.x <= hhi.x && dlo.z >= hlo.z && dhi.z <= hhi.z, "  inside the hole");
    }

    StructureInstance* ceil = put(F("plank_floor_8"), { lo.x + 12, top + 30, lo.z + 1 }, up, lookN);
    check(ceil != nullptr, "a ceiling on the walls");
    glm::ivec3 clo, chi;
    if (ceil) sm.bounds(*ceil, clo, chi);
    check(ceil && clo.x == lo.x && clo.z == lo.z && chi.x == hi.x && chi.z == hi.z && clo.y == top + 31, "  covering the house, 10 ft up");
    StructureInstance* r1 = put(F("thatch_roof_8x4"), { lo.x + 12, chi.y, lo.z + 2 }, up, lookN);
    StructureInstance* r2 = put(F("thatch_roof_8x4"), { lo.x + 12, chi.y, hi.z - 2 }, up, lookN);
    check(r1 && r2, "two roof halves on the ceiling's edges");
    if (r1 && r2) {
        glm::ivec3 a, b, c, d;
        sm.bounds(*r1, a, b); sm.bounds(*r2, c, d);
        check(a.z == lo.z && d.z == hi.z && b.z + 1 == c.z, "  eaves on the edges, meeting at the ridge");
    }
    StructureInstance* bench = put(F("workbench"), { lo.x + 12, top, lo.z + 12 }, up, lookN);
    check(bench != nullptr, "a workbench on the floor inside");

    std::printf("taking it apart\n");
    if (wW && n) {
        glm::ivec3 corner(lo.x, top + 5, lo.z);
        uint16_t before = w.chunks.typeAtVoxel(corner.x, corner.y, corner.z);
        uint32_t wid = wW->id;
        auto gone = sm.remove(wid, EditSource::Command);
        ed.flush();
        check(!gone.empty(), "the west wall comes down");
        check(before != BLOCK_AIR && w.chunks.typeAtVoxel(corner.x, corner.y, corner.z) != BLOCK_AIR,
              "  the shared corner stays with the north wall");
        check(w.chunks.typeAtVoxel(lo.x, top + 5, lo.z + 12) == BLOCK_AIR, "  its middle is gone");
    }
    std::printf("  %d pieces placed\n", placed);

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
