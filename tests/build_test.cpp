// Headless checks for building with the Builder's Hammer: the 8 x 8 x 4 ft
// stone foundation, and foundations lining up with each other
// (StructureManager::foundationFit / alignFoundation). Run from the
// repo root.
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
    const uint16_t F = gStructures.find("stone_foundation");
    const StructureType* t = gStructures.get(F);
    std::printf("the foundation\n");
    check(t && t->foundation && t->inBuildMenu, "a stone foundation, in the build menu");
    if (!t) return 1;
    check(t->shape(0, 0).size == glm::ivec3(24, 12, 24), "8 x 8 ft, 4 ft tall (24 x 12 x 24 voxels)");
    const StructureType* old = gStructures.get(gStructures.find("foundation"));
    check(old && !old->inBuildMenu, "the old 4 ft foundation still loads, out of the menu");

    World w;
    WorldEditor ed(w.chunks);
    TerrainGenerator gen(1);
    StructureManager sm(w.chunks, ed, gen);
    const glm::ivec3 A(100, GROUND, 100);
    auto place = [&](glm::ivec3 at) {
        StructureInstance* s = sm.checkPlace(F, at, 0, true).empty() ? sm.place(F, at, 0, EditSource::Command, 0) : nullptr;
        ed.flush();
        return s;
    };

    std::printf("anywhere on its own\n");
    check(sm.checkPlace(F, A, 0, true).empty(), "the first one goes anywhere");
    check(place(A) != nullptr, "placed");
    check(sm.checkPlace(F, A + glm::ivec3(60, 0, 3), 0, true).empty(), "and so does one well away from it (20 ft, off its grid)");

    std::printf("near one: lined up\n");
    check(sm.checkPlace(F, A + glm::ivec3(24, 0, 0), 0, true).empty(), "side by side, edges flush: fine");
    check(sm.checkPlace(F, A + glm::ivec3(0, 0, -24), 0, true).empty(), "on any side");
    check(sm.checkPlace(F, A + glm::ivec3(24, 0, 24), 0, true).empty(), "corner to corner");
    check(sm.checkPlace(F, A + glm::ivec3(0, 12, 0), 0, true).empty(), "stacked on top");
    check(!sm.checkPlace(F, A + glm::ivec3(27, 0, 0), 0, true).empty(), "a 1 ft gap: refused");
    check(sm.checkPlace(F, A + glm::ivec3(24, 0, 6), 0, true).empty(), "shifted 2 ft along: fine, on the 2 ft module");
    check(!sm.checkPlace(F, A + glm::ivec3(24, 0, 4), 0, true).empty(), "shifted 16 in along: refused");
    check(!sm.checkPlace(F, A + glm::ivec3(24, 3, 0), 0, true).empty(), "a foot higher: refused");
    check(!sm.checkPlace(F, A + glm::ivec3(10, 0, 0), 0, true).empty(), "overlapping: refused");
    check(sm.checkPlace(F, A + glm::ivec3(24 + 13, 0, 5), 0, true).empty(), "more than 4 ft away: free again");

    std::printf("the hammer snaps\n");
    const glm::ivec3 up(0, 1, 0), east(1, 0, 0);
    glm::ivec3 spot = A + glm::ivec3(30, 0, 4);                        // aimed at the ground near it, east
    check(sm.alignFoundation(F, 0, spot - up, up, spot) && spot == A + glm::ivec3(24, 0, 0), "aimed at the ground beside it: the slot beside it");
    spot = A + glm::ivec3(-20, 0, 21);
    check(sm.alignFoundation(F, 0, spot - up, up, spot) && spot == A + glm::ivec3(-24, 0, 24), "or diagonally");
    spot = A + glm::ivec3(24, 5, 9);                                   // aimed at its east face
    check(sm.alignFoundation(F, 0, A + glm::ivec3(23, 5, 9), east, spot) && spot == A + glm::ivec3(24, 0, 0), "aimed at its east face: the slot east");
    spot = A + glm::ivec3(7, 12, 7);                                   // aimed at its top
    check(sm.alignFoundation(F, 0, A + glm::ivec3(7, 11, 7), up, spot) && spot == A + glm::ivec3(0, 12, 0), "aimed at its top: stacked");
    spot = A + glm::ivec3(90, 0, 90);
    glm::ivec3 before = spot;
    check(!sm.alignFoundation(F, 0, spot - up, up, spot) && spot == before, "far from any: left where aimed");

    std::printf("a row\n");
    for (int i = 1; i < 4; i++) {
        glm::ivec3 s = A + glm::ivec3(24 * i + 5, 0, 2);
        sm.alignFoundation(F, 0, s - up, up, s);
        check(place(s) != nullptr, "foundation " + std::to_string(i + 1) + " snaps on and goes down");
    }
    int n = 0;
    sm.forEachInstance([&](const StructureInstance& si) { n += si.type == F && (si.origin.x - A.x) % 24 == 0 && si.origin.z == A.z; });
    check(n == 4, "four in a row, edges flush");

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
