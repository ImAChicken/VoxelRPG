// Headless checks for made pieces (game/PieceSpec.h): walls, posts and
// doors with their own sizes and blocks, palettes (game/Palettes.h),
// and the Copy tool's palettes in a blueprint. Run from the repo root.
#include "TestWorld.h"
#include "game/Blueprint.h"
#include "game/Palettes.h"
#include "world/StructureManager.h"
#include "world/WorldEditor.h"
#include <cstdio>
#include <sstream>

using namespace testworld;

namespace {
// A fixed shape the way the registry used to make it: all one block,
// a box of air cut out
VoxelVolume oldShape(glm::ivec3 size, uint16_t block, glm::ivec3 hp = { 0, 0, 0 }, glm::ivec3 hs = { 0, 0, 0 }) {
    VoxelVolume v(size);
    std::fill(v.data.begin(), v.data.end(), block);
    for (int y = hp.y; y < hp.y + hs.y; y++) for (int z = hp.z; z < hp.z + hs.z; z++) for (int x = hp.x; x < hp.x + hs.x; x++)
        v.set(x, y, z, BLOCK_AIR);
    return v;
}
bool same(const VoxelVolume& a, const VoxelVolume& b) { return a.size == b.size && a.data == b.data; }
uint16_t B(const char* n) { return gRegistry.getIDByName(n); }
}

int main() {
    if (!loadData() || !gStructures.load("data/registry/structures.toml")) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }
    auto T = [](const char* n) { return gStructures.get(gStructures.find(n)); };

    std::printf("the plain pieces look as they always did (old saves and blueprints)\n");
    {
        uint16_t planks = B("planks"), cob = B("cobblestone");
        check(same(T("plank_wall_8")->shape(0, 0), oldShape({ 24, 30, 2 }, planks)), "a plank wall");
        check(same(T("stone_doorway_8")->shape(0, 0), oldShape({ 24, 30, 2 }, cob, { 7, 0, 0 }, { 10, 24, 2 })), "a stone doorway (10 x 24 hole)");
        check(same(T("plank_doorway_4")->shape(0, 0), oldShape({ 12, 30, 2 }, planks, { 1, 0, 0 }, { 10, 24, 2 })), "a 4 ft doorway");
        check(same(T("stone_window_8")->shape(0, 0), oldShape({ 24, 30, 2 }, cob, { 6, 12, 0 }, { 12, 9, 2 })), "a window wall");
        check(same(T("plank_window_4")->shape(0, 0), oldShape({ 12, 30, 2 }, planks, { 3, 12, 0 }, { 6, 9, 2 })), "a 4 ft window wall");
        check(same(T("plank_door")->shape(0, 0), oldShape({ 10, 24, 1 }, planks)) &&
              same(T("plank_door")->shape(1, 0), oldShape({ 1, 24, 10 }, planks)), "a door, closed and open");
        check(same(T("wood_post")->shape(0, 0), oldShape({ 3, 30, 3 }, B("log_y"))), "a post");
        check(same(T("wooden_door")->shape(0, 0), oldShape({ 9, 21, 1 }, planks)), "the old 3 x 7 ft door");
    }

    std::printf("sizes, kept to what works\n");
    {
        const StructureType& dw = *T("plank_doorway_8");
        PieceSpec p; p.openW = 4; p.openH = 12;
        PieceSpec r = gStructures.resolve(dw, p);
        check(r.openW == 9 && r.openH == 21, "a doorway is never under 3 x 7 ft (9 x 21)");
        p = PieceSpec(); p.h = 12;
        r = gStructures.resolve(dw, p);
        check(r.h >= r.openH + 3, "a doorway wall stays tall enough for its door and a lintel (" + std::to_string(r.h) + ")");
        p = PieceSpec(); p.w = 6;
        r = gStructures.resolve(dw, p);
        check(r.w >= r.openW + 2 && r.w % 3 == 0, "and wide enough for a post each side, in whole feet (" + std::to_string(r.w) + ")");
        const StructureType& door = *T("plank_door");
        p = PieceSpec(); p.w = 3; p.h = 6;
        r = gStructures.resolve(door, p);
        check(r.w == 9 && r.h == 21, "a door is never under 3 x 7 ft");
        const StructureType& win = *T("plank_window_8");
        p = PieceSpec(); p.h = 12; p.openH = 20; p.sill = 30;
        r = gStructures.resolve(win, p);
        check(r.sill >= 3 && r.sill + r.openH <= r.h - 2, "a window keeps a sill and a top in a low wall");
        p = PieceSpec(); p.w = 36; p.h = 45;
        std::string spec = gStructures.normalize(win, p);
        check(gStructures.shape(win.id, spec, 0, 0).size == glm::ivec3(36, 45, 2), "a 12 x 15 ft window wall");
        check(gStructures.shape(win.id, spec, 0, 1).size == glm::ivec3(2, 45, 36), "  turned");
        check(gStructures.normalize(win, PieceSpec()).empty() && gStructures.normalize(win, gStructures.resolve(win, PieceSpec())).empty(),
              "the type's own sizes are an empty spec");
        PieceSpec back = PieceSpec::parse(spec);
        check(back.w == 36 && back.h == 45 && PieceSpec::parse(back.text()) == back, "as text and back");
        check(PieceSpec::parse("w=12;post=Bad Name;x=1;fill=../etc").blocks.empty(), "odd text is left out");
        auto cost = gStructures.costOf(win, spec);
        check(!cost.empty() && cost[0].second > win.cost[0].second, "a bigger wall costs more (" + std::to_string(cost[0].second) + ")");
    }

    std::printf("palettes\n");
    {
        check(gBuildPalettes.load(PaletteLibrary::REGISTRY, "/nonexistent") >= 5, "the curated palettes load");
        bool legal = true;
        std::string bad;
        for (const BuildPalette& p : gBuildPalettes.list())
            for (auto& [role, b] : p.spec.blocks)
                if (b != "none" && !StructureRegistry::paletteBlock(B(b.c_str()))) { legal = false; bad = p.name + "." + role + "=" + b; }
        for (const PieceRole& r : pieceRoles())
            for (const std::string& b : gBuildPalettes.options(r.id))
                if (b != "none" && !StructureRegistry::paletteBlock(B(b.c_str()))) { legal = false; bad = std::string(r.id) + " choice " + b; }
        check(legal, "every block they use is a building block " + bad);
        const BuildPalette* tudor = gBuildPalettes.find("tudor");
        check(tudor != nullptr, "Tudor is one");
        if (tudor) {
            const StructureType& w = *T("plank_wall_8");
            const StructureType& tw = *T("plank_inlay_wall_8");
            const StructureType& door = *T("plank_ledged_door");
            PieceSpec a = gStructures.applyPalette(w, PieceSpec(), tudor->spec);
            check(a.outside == "timber" && a.block("fill") == "whitewash", "a plain wall takes its finish and blocks");
            PieceSpec b = gStructures.applyPalette(tw, PieceSpec(), tudor->spec);
            check(b.outside.empty() && b.block("inlay") == "herringbone_bricks", "an inlaid wall keeps its own finish, in its blocks");
            PieceSpec c = gStructures.applyPalette(door, PieceSpec(), tudor->spec);
            check(c.block("door") == "dark_planks" && c.block("fill").empty(), "a door takes only a door's roles");
            const VoxelVolume& v = gStructures.shape(w.id, gStructures.normalize(w, a), 0, 0);
            check(v.at(0, 15, 0) == B("timber_post") && v.at(5, 20, 1) == B("plaster"), "half-timbered outside, plaster inside");
        }
    }

    std::printf("placed pieces keep their own\n");
    World w;
    WorldEditor ed(w.chunks);
    TerrainGenerator gen(1);
    StructureManager sm(w.chunks, ed, gen);
    const uint16_t F = gStructures.find("stone_foundation");
    const glm::ivec3 A(100, GROUND, 100);
    check(sm.checkPlace(F, A, 0, true).empty() && sm.place(F, A, 0, EditSource::Command, 0), "a foundation");
    ed.flush();
    const uint16_t DW = gStructures.find("plank_doorway_8");
    PieceSpec big; big.h = 36; big.openW = 12; big.openH = 27;
    big.blocks["fill"] = "whitewash";
    std::string spec = gStructures.normalize(*gStructures.get(DW), big);
    glm::ivec3 spot; int rot = 0;
    std::string snapSpec = spec;
    bool snapped = sm.snapTo(DW, 0, A + glm::ivec3(12, 11, 1), { 0, 1, 0 }, { 0, 0, 1 }, spot, rot, &snapSpec);
    check(snapped && snapSpec == spec, "a tall doorway snaps onto the foundation's edge");
    uint32_t sup = 0;
    check(sm.checkPlace(DW, spot, rot, true, &sup, spec).empty(), "  and fits");
    StructureInstance* dwI = sm.place(DW, spot, rot, EditSource::Command, sup, spec);
    ed.flush();
    check(dwI && dwI->spec == spec, "  placed with its spec");
    glm::ivec3 lo, hi;
    sm.bounds(*dwI, lo, hi);
    check(hi.y - lo.y + 1 == 36, "  12 ft tall");
    check(w.chunks.typeAtVoxel(lo.x, lo.y + 30, lo.z) == B("whitewash"), "  whitewashed");
    const uint32_t dwId = dwI->id;

    // a door into it: sized to the hole
    const uint16_t D = gStructures.find("plank_ledged_door");
    std::string dspec;
    glm::ivec3 hlo, hhi;
    sm.holeOf(*sm.get(dwId), hlo, hhi);
    bool doorSnap = sm.snapTo(D, 0, hlo - glm::ivec3(1, 0, 0), { 0, 0, -1 }, { 0, 0, 1 }, spot, rot, &dspec);
    PieceSpec dfull = gStructures.resolve(*gStructures.get(D), PieceSpec::parse(dspec));
    check(doorSnap && dfull.w == 12 && dfull.h == 27, "a door snapped into it takes the doorway's size (" +
          std::to_string(dfull.w) + " x " + std::to_string(dfull.h) + ")");
    StructureInstance* door = sm.checkPlace(D, spot, rot, true, &sup, dspec).empty() ? sm.place(D, spot, rot, EditSource::Command, sup, dspec) : nullptr;
    ed.flush();
    check(door != nullptr, "  and goes in");
    if (door) {
        std::string why = sm.toggleDoor(*door, EditSource::Command, nullptr);
        ed.flush();
        check(why.empty() && door->state == 1, "  opens");
        why = sm.toggleDoor(*door, EditSource::Command, nullptr);
        ed.flush();
        check(why.empty() && door->state == 0, "  and shuts");
    }

    // saved and read back
    std::ostringstream out;
    sm.write(out);
    StructureManager sm2(w.chunks, ed, gen);
    std::istringstream in(out.str());
    sm2.read(in);
    const StructureInstance* again = sm2.get(dwId);
    check(again && again->spec == spec, "saved and loaded with its spec");
    glm::ivec3 lo2, hi2;
    if (again) sm2.bounds(*again, lo2, hi2);
    check(again && lo2 == lo && hi2 == hi, "  the same box");

    // copied: palettes found, merging asked for, never done alone
    std::printf("the Copy tool's palettes\n");
    {
        // a second wall in the same whitewash, but a dark door: the door's
        // palette agrees with the walls' (different roles), so they can merge
        Blueprint bp;
        std::string why;
        check(Blueprint::capture(sm, dwId, nullptr, bp, why), "copied");
        check(bp.palettes.size() >= 2, "the doorway and the door: " + std::to_string(bp.palettes.size()) + " palettes, kept apart");
        check(bp.mergeable() >= 1, "  they never disagree: " + std::to_string(bp.mergeable()) + " could merge");
        const StructureInstance* d0 = sm.get(dwId);
        check(d0 && d0->spec == spec, "  the building itself is untouched");
        Blueprint m = bp;
        m.mergePalettes();
        check(m.palettes.size() == bp.palettes.size() - (size_t)bp.mergeable(), "merged when asked");
        // pasted, merged or not: the same pieces
        bool samePieces = true;
        auto a = bp.plan({ 300, GROUND, 300 }, 0), b = m.plan({ 300, GROUND, 300 }, 0);
        for (size_t i = 0; i < a.size() && i < b.size(); i++) samePieces = samePieces && a[i].spec == b[i].spec && a[i].lo == b[i].lo;
        check(samePieces && a.size() == b.size(), "  and places exactly the same pieces");
        bool keeps = false;
        for (auto& p : a) if (p.type == DW) keeps = p.spec == spec;
        check(keeps, "  the doorway as it was (12 ft, whitewash)");
        // a palette laid over it when placed
        BlueprintStyle st;
        st.palette = gBuildPalettes.find("brick_townhouse") ? gBuildPalettes.find("brick_townhouse")->spec : PieceSpec();
        auto c = bp.plan({ 300, GROUND, 300 }, 0, &st);
        bool bricks = false;
        for (auto& p : c) if (p.type == DW) bricks = PieceSpec::parse(p.spec).block("fill") == "bricks" && PieceSpec::parse(p.spec).h == 36;
        check(bricks, "a style's palette restyles it (brick), keeping its size");
        // as a file and back
        Blueprint r;
        check(r.fromText(m.text(), why) && r.palettes.size() == m.palettes.size() && r.parts.size() == m.parts.size(), "as a file and back");
    }

    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
