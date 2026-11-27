// Headless checks for buildings kept as plans (the hammer's Build tab:
// game/BuildPlan.h, world/PlanManager.h): foundations and walls on the
// 3 ft grid, rooms found inside them (inside and outside faces), the
// palette's blocks, angled walls, a wall's height and the floor's
// changed without the rest rebuilt, hand edits kept through it,
// painting, undo, saving. Run from the repo root.
#include "TestWorld.h"
#include "world/PlanManager.h"
#include "chunk/TerrainGenerator.h"
#include <cstdio>
#include <cstdlib>

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    if (!gStructures.load("data/registry/structures.toml")) { std::printf("structures.toml didn't load\n"); return 1; }
    gBuildPalettes.load();
    World w;
    WorldEditor editor(w.chunks);
    TerrainGenerator gen(1);
    StructureManager sm(w.chunks, editor, gen);
    PlanManager plans(w.chunks, editor, &sm);
    editor.onEdit = [&](glm::ivec3 p, uint16_t id, const std::string& label) { plans.noteEdit(p, id, label); };
    auto at = [&](int x, int y, int z) { return w.chunks.typeAtVoxel(x, y, z); };
    plan::Looks L = plans.looksFor(plan::Plan());
    std::printf("the palette's blocks: fill %s, paint %s, base %s, floor %s\n", gRegistry.get(L.fill).name.c_str(),
                gRegistry.get(L.paint).name.c_str(), gRegistry.get(L.base).name.c_str(), gRegistry.get(L.floor).name.c_str());
    check(L.fill && L.paint && L.base && L.floor && L.fill != L.paint, "the default palette gives every role a block");

    std::printf("a foundation\n");
    // cells 20..23 x 20..22 (world voxels 180..215, 180..206)
    uint32_t id = plans.addFoundation({ 20, 20 }, { 23, 22 }, GROUND, plan::FLOOR, "");
    const plan::Plan* p = plans.get(id);
    check(p && p->foundations.size() == 1, "laid as a plan");
    const int fy = GROUND + plan::FLOOR;
    check(at(190, fy - 1, 190) == L.base && at(190, fy, 190) == BLOCK_AIR, "its top is the floor height, 3 over the ground");
    check(at(179, fy - 1, 190) == L.base && at(177, fy - 1, 190) == BLOCK_AIR, "  half a wall past the grid line, no more");
    uint32_t id2 = plans.addFoundation({ 24, 20 }, { 25, 21 }, GROUND, plan::FLOOR, "");
    check(id2 == id && plans.get(id)->foundations.size() == 2, "one laid beside it joins it");

    std::printf("a room\n");
    // the grid points 20..24 x 20..23 (voxels 180..216 x 180..207): walls round the first foundation
    plans.addRoom({ 180, 180 }, { 216, 207 }, GROUND, plan::WALL_H, plan::THICK, "");
    p = plans.get(id);
    check(p->walls.size() == 4, "four walls");
    plan::Rooms r = plan::rooms(*p);
    check(r.roomCells() > 25 * 30, "the ground inside them is a room (" + std::to_string(r.roomCells()) + " cells)");
    // the wall on grid line z = 20 (world z 180): 3 thick (179..181)
    check(at(190, fy, 179) == L.fill && at(190, fy, 180) == L.fill && at(190, fy, 181) == L.paint && at(190, fy, 182) == BLOCK_AIR,
          "a wall 3 thick: the outer face, the core, the inner face painted inside");
    check(at(190, fy + plan::WALL_H - 1, 180) != BLOCK_AIR && at(190, fy + plan::WALL_H, 180) == BLOCK_AIR, "  30 voxels (10 ft) tall");
    check(at(190, fy - 1, 190) == L.floor && at(175 + 50, fy - 1, 185) == L.base, "the floor inside the room; plain foundation outside it");

    std::printf("a wall's height, and the floor's\n");
    int wallZ = -1;
    for (size_t i = 0; i < p->walls.size(); i++) if (p->walls[i].a.y == 0 && p->walls[i].b.y == 0) wallZ = (int)i;
    // a hole knocked in that wall by hand, and a block set on the floor
    EditBatch hand; hand.source = EditSource::Player; hand.label = "break";
    hand.set(190, fy + 5, 180, BLOCK_AIR);
    hand.set(195, fy, 195, gRegistry.getIDByName("torch") ? gRegistry.getIDByName("torch") : BLOCK_STONE);
    editor.apply(std::move(hand));
    check(plans.overrideCount(id) == 2, "hand edits inside the plan are kept (2)");
    uint16_t placed = at(195, fy, 195);
    size_t before = plans.builtCount(id);
    plans.setWallHeight(id, wallZ, plan::WALL_H + 9);
    std::printf("    raising one wall 9 wrote %zu voxels (of %zu)\n", plans.lastWritten, before);
    check(at(190, fy + plan::WALL_H + 8, 180) != BLOCK_AIR && at(190, fy + plan::WALL_H + 8, 190 + 27) == BLOCK_AIR,
          "that wall is taller, the one across isn't");
    check(plans.lastWritten > 0 && plans.lastWritten < 50 * 3 * 10, "  only its new voxels were written");
    check(at(190, fy + 5, 180) == BLOCK_AIR, "  the hole knocked in it is still there");
    plans.setFloor(id, plan::FLOOR + 6);
    const int fy2 = fy + 6;
    check(at(190, fy2 - 1, 190) == L.floor && at(190, fy2, 179) == L.fill, "the floor raised 6: foundation and walls with it");
    check(at(190, fy2 + 5, 180) == BLOCK_AIR && at(190, fy + 5, 180) != BLOCK_AIR, "  the hole rose with its wall");
    check(at(195, fy2, 195) == placed && at(195, fy, 195) != placed, "  and the block set on the floor rose with it");
    check(at(190, fy2 + plan::WALL_H + 8, 180) != BLOCK_AIR && at(190, fy2 + plan::WALL_H + 9, 180) == BLOCK_AIR,
          "  the taller wall kept its height over the new floor");

    std::printf("painting, angles, removing, undo\n");
    uint16_t brick = gRegistry.getIDByName("bricks");
    plan::Plan* pm = plans.get(id);
    int outSide = pm->walls[(size_t)wallZ].side(glm::vec2(190 - pm->origin.x + 0.5f, 178 - pm->origin.z + 0.5f));
    plans.paint(id, wallZ, outSide, brick);
    check(at(190, fy2 + 2, 179) == brick && at(190, fy2 + 2, 181) == L.paint, "a wall's outer side painted bricks; its inside isn't");
    // walls at 45° and 2-to-1, on the ground beside the house
    uint32_t g = plans.addWall({ 270, 180 }, { 306, 216 }, GROUND, 12, 3, "");
    const plan::Plan* gp = plans.get(g);
    check(g != id && gp && !gp->hasFoundation(), "a wall off the foundation: its own plan, on the ground");
    bool solid45 = true;
    for (int i = 0; i <= 36; i++) if (at(270 + i, GROUND + 1, 180 + i) == BLOCK_AIR) solid45 = false;
    check(solid45, "a 45° wall: unbroken along its line");
    check(plan::snapEnd({ 0, 0 }, { 4.2f, 1.9f }) == glm::ivec2(4, 2) && plan::snapEnd({ 0, 0 }, { 3.1f, 3.3f }) == glm::ivec2(3, 3) &&
          plan::snapEnd({ 0, 0 }, { 5.0f, 0.4f }) == glm::ivec2(5, 0), "walls snap to straight, 45° and 2-to-1");
    size_t undos = plans.undoCount();
    check(plans.removeAt(glm::vec3(285.5f, GROUND + 2, 195.5f)) && !plans.get(g) && at(285, GROUND + 2, 195) == BLOCK_AIR,
          "removing the angled wall: it's gone, and its plan with it");
    plans.undoLast();
    check(plans.get(g) && at(285, GROUND + 2, 195) != BLOCK_AIR && plans.undoCount() == undos, "undo: it's back");

    std::printf("pillars and snapping\n");
    uint16_t post = plans.looksFor(*plans.get(id)).post;
    plans.addPillar({ 180, 180 }, GROUND, 5, false, 0, "");
    check(post && at(178, fy2 + 4, 178) == post && at(182, fy2 + 4, 182) == post && at(184, fy2 + 4, 180) != post,
          "a pillar at the room's corner: 5 across, through the walls there");
    check(at(180, fy2 + plan::WALL_H + 8, 180) == post, "  as tall as the tallest wall it's in");
    plans.addPillar({ 198, 225 }, GROUND, 5, true, 0, "");
    check(at(198, fy2 + 10, 225) == post && at(196, fy2 + 10, 223) != post, "a round pillar on its own, in front: round");
    check(plans.removeAt(glm::vec3(198.5f, fy2 + 5, 225.5f)) && at(198, fy2 + 10, 225) != post, "  taken away again");
    check(plans.removeAt(glm::vec3(180.5f, fy2 + 4, 180.5f)) && at(182, fy2 + 4, 181) == L.paint && at(178, fy2 + 4, 178) == BLOCK_AIR, "the corner pillar taken away: the wall's back");
    PlanManager::Snap sn = plans.snap(glm::vec3(199.6f, 50.0f, 181.2f), false);    // near the middle of the wall at z 180
    check(sn.kind == 1 && sn.at == glm::ivec2(198, 180), "a cursor near a wall's middle snaps to it");
    sn = plans.snap(glm::vec3(235.0f, 50.0f, 236.0f), false);
    check(sn.kind == 0 && sn.at == glm::ivec2(234, 234), "  away from everything: the grid's nearest point");
    sn = plans.snap(glm::vec3(235.0f, 50.0f, 236.0f), true);
    check(sn.kind == 2 && sn.at == glm::ivec2(235, 236), "  free: the column under it");
    sn = plans.snap(glm::vec3(262.2f, 50.0f, 262.8f), false, { { 263, 262 } });
    check(sn.kind == 1 && sn.at == glm::ivec2(263, 262), "  an old piece's corner pulls it too");
    uint32_t off = plans.addWall({ 199, 230 }, { 223, 242 }, GROUND, 12, 3, "");
    check(off && at(211, GROUND + 2, 236) != BLOCK_AIR, "a wall between off-grid points (snapped to parts)");

    std::printf("openings: frames, doors, windows\n");
    {
        const plan::Plan* hp = plans.get(id);
        const plan::Wall& wl = hp->walls[(size_t)wallZ];
        const int fy3 = hp->floorY();
        glm::ivec2 u = glm::sign(wl.b - wl.a);
        auto col = [&](int k) { return glm::ivec2(hp->origin.x, hp->origin.z) + wl.a + u * k; };   // world column k along it
        int hole = plans.addOpening(id, wallZ, 14, 10, 24, 0, plan::Fill::Door);
        check(hole >= 0 && plans.get(id)->walls[(size_t)wallZ].openings.size() == 1, "a door frame in the wall");
        glm::ivec2 m = col(14);
        uint16_t beam = L.frame;
        check(at(m.x, fy3 + 5, m.y - 1) == BLOCK_AIR && at(m.x, fy3 + 5, m.y + 1) == BLOCK_AIR, "  its hole goes through");
        glm::ivec2 e0 = col(14 - 5 - 1), e1 = col(14 + 5);
        check(at(e0.x, fy3 + 5, e0.y - 1) == beam && at(e1.x, fy3 + 5, e1.y + 1) == beam && at(m.x, fy3 + 24, m.y) == beam,
              "  a frame of beams round it (sides, lintel)");
        check(at(m.x, fy3 + 25, m.y) != BLOCK_AIR && at(m.x, fy3 + 25, m.y) != beam, "  the wall over the lintel");
        const StructureInstance* door = sm.instanceAt({ m.x, fy3 + 5, m.y });
        const StructureType* dt = door ? sm.typeOf(*door) : nullptr;
        check(dt && dt->kind == StructureKind::Door, "  a door piece in it (" + std::string(dt ? dt->name : "none") + ")");
        check(plans.overrideCount(id) == 2, "  the door's voxels aren't kept as hand edits");
        // a window in the wall across
        int across = -1;
        for (size_t i = 0; i < hp->walls.size(); i++) if (hp->walls[i].a.y == 27 && hp->walls[i].b.y == 27) across = (int)i;
        const plan::Wall& wa = plans.get(id)->walls[(size_t)across];
        glm::ivec2 ua = glm::sign(wa.b - wa.a);
        glm::ivec2 wm = glm::ivec2(hp->origin.x, hp->origin.z) + wa.a + ua * 18;
        check(plans.addOpening(id, across, 18, 9, 12, 9, plan::Fill::Window) >= 0, "a window frame across the room");
        check(at(wm.x, fy3 + 12, wm.y) == L.glass && at(wm.x, fy3 + 12, wm.y + 1) == BLOCK_AIR && at(wm.x, fy3 + 12, wm.y - 1) == BLOCK_AIR,
              "  glass in the wall's middle, open either side");
        check(at(wm.x, fy3 + 8, wm.y) == beam && at(wm.x, fy3 + 21, wm.y) == beam && at(wm.x, fy3 + 7, wm.y) != beam, "  a sill and a lintel");
        check(plans.addOpening(id, across, 20, 9, 12, 9, plan::Fill::Open) >= 0 && plans.get(id)->walls[(size_t)across].openings.size() == 1 &&
              at(wm.x, fy3 + 12, wm.y) == BLOCK_AIR, "  a frame put over it replaces it (left open)");
        // the floor raised: the door goes up with it, one door still
        uint32_t doorId = plans.get(id)->walls[(size_t)wallZ].openings[0].doorId;
        plans.setFloor(id, plans.get(id)->floor + 3);
        const StructureInstance* d2 = sm.instanceAt({ m.x, fy3 + 3 + 5, m.y });
        uint32_t nowId = plans.get(id)->walls[(size_t)wallZ].openings[0].doorId;
        check(d2 && d2->id == nowId && !sm.get(doorId) && at(m.x, fy3 + 1, m.y) != BLOCK_AIR,
              "the floor raised: the door rose with it (the old one gone, the foundation under it)");
        plans.setFloor(id, plans.get(id)->floor - 3);
        // removing the frame: the wall's whole again, the door gone
        uint32_t last = plans.get(id)->walls[(size_t)wallZ].openings[0].doorId;
        check(plans.removeAt(glm::vec3(m.x + 0.5f, fy3 + 5, m.y + 0.5f)) && plans.get(id)->walls[(size_t)wallZ].openings.empty() &&
              !sm.get(last) && at(m.x, fy3 + 5, m.y) == L.fill && plans.get(id)->walls.size() == 4,
              "removing the frame (not the wall): the wall's closed, its door gone");
        plans.undoLast();
        check(plans.get(id)->walls[(size_t)wallZ].openings.size() == 1 && sm.instanceAt({ m.x, fy3 + 5, m.y }), "  undo: frame and door back");
        // an angled wall takes a window (not a door: a door piece turns square)
        uint32_t g2 = plans.addWall({ 300, 240 }, { 336, 276 }, GROUND, 30, 3, "");
        const plan::Plan* gp2 = plans.get(g2);
        int dw = plans.addOpening(g2, 0, 25, 10, 24, 0, plan::Fill::Door);
        check(dw >= 0 && plans.get(g2)->walls[0].openings[0].doorId == 0, "a door frame in a 45° wall stays open");
        (void)gp2;
    }

    std::printf("roofs\n");
    {
        plans.setWallHeight(id, wallZ, plan::WALL_H);
        uint16_t roofB = L.roof;
        check(roofB != 0, "the palette has a roof block (" + gRegistry.get(roofB).name + ")");
        check(plans.roofPlan(id, plan::RoofStyle::Gable, -1, 12, 3) == id && plans.get(id)->roofs.size() == 1, "a gable roof over the house");
        const plan::Plan* rp = plans.get(id);
        const plan::Roof& rf = rp->roofs[0];
        check(rf.axis == 0, "  its ridge along the longer side (x)");
        if (std::getenv("DUMP")) {
            std::printf("origin %d %d %d floorY %d base %d roof %d,%d-%d,%d\n", rp->origin.x, rp->origin.y, rp->origin.z, rp->floorY(), rp->roofBase(rf), rf.a.x, rf.a.y, rf.b.x, rf.b.y);
            for (int xx : { 198, 180, 179 }) {
                std::printf("x=%d\n", xx);
                for (int y = rp->floorY() + 45; y >= rp->floorY() + 24; y--) {
                    std::printf("%3d ", y - rp->floorY());
                    for (int z = 175; z <= 212; z++) { uint16_t b = at(xx, y, z); std::printf("%c", b == 0 ? '.' : b == L.roof ? 'R' : b == L.fill ? 'F' : b == L.paint ? 'P' : '#'); }
                    std::printf("\n");
                }
            }
        }
        const int base = rp->floorY() + plan::WALL_H;
        check(at(198, base + 11, 193) == roofB && at(198, base + 12, 193) == BLOCK_AIR, "  the ridge 11 over the eaves, in the middle");
        check(at(198, base, 180) == roofB && at(198, base - 2, 180) == L.fill, "  the eave sits on the wall");
        check(at(198, base - 3, 177) == roofB && at(198, base - 4, 176) == BLOCK_AIR, "  and overhangs it by 3, sloping on down");
        check(at(180, base + 5, 193) != BLOCK_AIR && at(180, base + 5, 193) != roofB && at(179, base + 5, 193) == L.fill,
              "  the end wall carried up into a gable (its outside face the wall's)");
        bool solid = true;
        for (int z = 177; z <= 210; z++) {
            bool any = false;
            for (int y = base - 6; y <= base + 13; y++) any = any || at(198, y, z) == roofB;
            solid = solid && any;
        }
        check(solid, "  no gap in the slope front to back");
        plan::Roof r2 = rf; r2.peak = 18;
        plans.setRoof(id, 0, r2);
        check(at(198, base + 17, 193) == roofB && at(198, base + 18, 193) == BLOCK_AIR && plans.lastWritten < plans.builtCount(id) / 2,
              "the peak raised to 18 (" + std::to_string(plans.lastWritten) + " voxels written)");
        r2.style = plan::RoofStyle::Hip;
        plans.setRoof(id, 0, r2);
        check(at(180, base + 5, 193) == BLOCK_AIR && at(198, base + 17, 193) == roofB && at(181, base + 1, 193) == roofB, "a hip roof: no gables, slopes all round");
        r2.style = plan::RoofStyle::Shed; r2.peak = 9;
        plans.setRoof(id, 0, r2);
        check(at(198, base + 8, 206) == roofB && at(198, base, 182) == roofB && at(198, base + 3, 182) == BLOCK_AIR, "a shed roof rising to the back");
        plan::RoofStyle back = plan::RoofStyle::Gable;
        r2.style = back; r2.peak = 12;
        plans.setRoof(id, 0, r2);
        plans.setFloor(id, plans.get(id)->floor + 3);
        check(at(198, base + 3 + 11, 193) == roofB && at(198, base + 3 + 12, 193) == BLOCK_AIR, "the floor raised 3: the roof with it");
        plans.setFloor(id, plans.get(id)->floor - 3);
        check(plans.removeAt(glm::vec3(198.5f, base + 11, 193.5f)) && plans.get(id)->roofs.empty() && at(198, base + 11, 193) == BLOCK_AIR &&
              at(180, base + 5, 193) == BLOCK_AIR, "the roof taken away: the gable with it");
        plans.undoLast();
        check(plans.get(id)->roofs.size() == 1 && at(198, base + 11, 193) == roofB, "  undo: back");
        plans.removeAt(glm::vec3(198.5f, base + 11, 193.5f));
        check(plans.roofRoom({ 195, 195 }, plan::RoofStyle::Gable, -1, 12, 3) == id && plans.get(id)->roofs.size() == 1 &&
              plans.get(id)->roofs[0].lo() == glm::ivec2(0, 0) && plans.get(id)->roofs[0].hi() == glm::ivec2(36, 27),
              "a click in the room: a roof over its walls");
    }

    std::printf("storeys, stairs, fences\n");
    {
        uint32_t h2 = plans.addFoundation({ 2, 30 }, { 5, 33 }, GROUND, plan::FLOOR, "");
        plans.addRoom({ 18, 270 }, { 54, 306 }, GROUND, plan::WALL_H, 3, "");
        check(plans.addRoom({ 18, 270 }, { 54, 306 }, GROUND, plan::WALL_H, 3, "", 1) == h2, "a room on the floor above");
        const plan::Plan* hp = plans.get(h2);
        const int fy = hp->floorY(), ly1 = hp->levelY(1);
        check(ly1 == fy + plan::WALL_H + plan::DECK && hp->decks.size() == 1, "  its floor (a deck) on the walls below: 30 + 2 up");
        check(at(36, ly1 - 1, 288) == L.floor && at(36, ly1 - 2, 288) == L.floor && at(36, ly1 - 3, 288) == BLOCK_AIR,
              "  the deck 2 thick over the room below");
        if (std::getenv("DUMP")) for (int x = 16; x <= 22; x++) std::printf("x%d: %d %d %d   paint %d fill %d\n", x, at(x, ly1 + 5, 288), at(x, ly1 + plan::WALL_H, 288), at(x, ly1 + plan::WALL_H - 1, 288), L.paint, L.fill);
        check(at(18, ly1 + 5, 288) != BLOCK_AIR && at(18, ly1 + plan::WALL_H, 288) == BLOCK_AIR && at(19, ly1 + 5, 288) == L.paint,
              "  its walls standing on it, painted inside");
        check(plans.addStair({ 30, 272 }, 1, 9, 0) == h2, "a stair up from the ground floor");
        check(at(30, fy + 3, 272) == L.floor && at(30, fy + 4, 272) == BLOCK_AIR && at(26, fy + 3, 272) == L.floor && at(35, fy + 3, 272) == BLOCK_AIR,
              "  its first step 4 up, 9 wide");
        check(at(30, ly1 - 1, 303) == L.floor && at(30, ly1, 303) == BLOCK_AIR, "  its last step level with the floor above");
        check(at(30, ly1 - 1, 290) == BLOCK_AIR && at(40, ly1 - 1, 290) == L.floor, "  the deck cut away over it (the stairwell)");
        check(plans.addFence({ 34, 272 }, { 34, 303 }, GROUND, plan::FenceStyle::Baluster, 9, 0) == h2, "a railing along the stair's side");
        uint16_t beamB = L.frame;
        check(at(34, fy + 20 + 8, 290) == beamB && at(34, fy + 20 + 3, 290) == beamB && at(34, fy + 20 + 3, 291) == BLOCK_AIR,
              "  on the steps: a top rail, balusters with gaps");
        plans.setLevelHeight(h2, 0, 36);
        hp = plans.get(h2);
        check(hp->levelY(1) == ly1 + 6 && at(36, ly1 + 5, 288) == L.floor && at(36, ly1 - 1, 288) == BLOCK_AIR,
              "the ground floor's walls 6 taller: the floor above goes up with them");
        check(at(18, ly1 + 6 + 5, 288) != BLOCK_AIR && at(18, fy + 35, 288) != BLOCK_AIR, "  walls on both levels with it");
        plans.setLevelHeight(h2, 0, plan::WALL_H);
        check(at(36, ly1 - 1, 288) == L.floor, "  and back down");
        uint32_t g3 = plans.addFence({ 60, 270 }, { 60, 300 }, GROUND, plan::FenceStyle::Picket, 9);
        check(g3 && g3 != h2 && at(60, GROUND + 5, 272) != BLOCK_AIR && at(60, GROUND + 5, 273) == BLOCK_AIR && at(60, GROUND + 2, 273) != BLOCK_AIR,
              "a picket fence in the garden: pickets with gaps, rails across");
        check(at(60, GROUND + 9, 270) != BLOCK_AIR && at(60, GROUND + 9, 272) == BLOCK_AIR && at(61, GROUND + 5, 272) == BLOCK_AIR,
              "  posts a little taller, one voxel wide");
        plans.addFence({ 64, 270 }, { 64, 300 }, GROUND, plan::FenceStyle::Rail, 9);
        check(at(64, GROUND + 3, 275) != BLOCK_AIR && at(64, GROUND + 5, 275) == BLOCK_AIR && at(64, GROUND + 5, 276) != BLOCK_AIR,
              "a rail fence: posts every 6, open between its rails");
        check(plans.removeAt(glm::vec3(30.5f, fy + 10, 285.5f)) && plans.get(h2)->stairs.empty() && at(30, ly1 - 1, 290) == L.floor,
              "the stair taken away: the deck closes over it");
        plans.undoLast();
        check(plans.get(h2)->stairs.size() == 1, "  undo: back");

        std::printf("railings of its own\n");
        // the stairwell: x 26..34, z 272..303, its top step's way off +z
        const int RH = plan::Plan::RAIL_H;
        check(at(25, ly1 + RH - 1, 290) != BLOCK_AIR && at(25, ly1 + RH, 290) == BLOCK_AIR && at(35, ly1 + RH - 1, 290) != BLOCK_AIR,
              "  round the stairwell, both sides, 3 ft 4 in tall");
        check(at(25, ly1 + 4, 291) != BLOCK_AIR && at(25, ly1 + 4, 290) == BLOCK_AIR, "  balusters with a gap between");
        check(at(25, ly1 + RH - 1, 302) != BLOCK_AIR, "  beside its top step too");
        {
            plan::Plan q;
            plan::Deck d; d.a = { 0, 0 }; d.b = { 5, 5 }; d.level = 1; q.decks.push_back(d);
            plan::Stair t; t.at = { 20, 5 }; t.dir = 1; q.stairs.push_back(t);   // z 5..36, x 16..24
            bool foot = false, side = false, exit = false;
            for (const auto& [v, k] : q.railColumns(1)) {
                foot = foot || v == glm::ivec2(20, 4);
                side = side || v == glm::ivec2(15, 20);
                exit = exit || (v.y == 37 && v.x >= 16 && v.x <= 24);
            }
            check(foot && side && !exit, "  across its foot and along its sides, but open off its top step");
        }
        check(at(19, ly1 + RH - 1, 288) == L.paint, "  none along the walls");
        check(plans.setRails(h2, false) && at(25, ly1 + RH - 1, 290) == BLOCK_AIR, "  turned off, gone");
        plans.setRails(h2, true);
        // a balcony: a deck past the walls, open on three sides
        check(plans.addDeck({ 6, 30 }, { 7, 33 }, 1) == h2, "  a balcony beside the room upstairs");
        check(at(73, ly1 + RH - 1, 288) != BLOCK_AIR && at(73, ly1 + RH - 1, 289) != BLOCK_AIR && at(72, ly1 + RH - 1, 288) == BLOCK_AIR,
              "  railed along its open edge");
        plans.undoLast();

        std::printf("L, U and spiral stairs\n");
        {
            plan::Stair t; t.shape = plan::StairShape::L; t.width = 9; t.dir = 0;
            const int rise = 32, N = t.count(rise);
            check(N == 9 && t.stepAt({ 0, 0 }, rise) == 0 && t.stepAt({ 20, 0 }, rise) == 4, "  L: a flight to a landing (step 5 of 9)");
            check(t.stepAt(t.toPlan(20, t.jhi() + 1), rise) == 5 && t.stepAt(t.toPlan(20, t.jhi() + 16), rise) == 8 &&
                  t.stepAt(t.toPlan(20, t.jhi() + 17), rise) == -1, "  then a flight turned toward its side, to the top");
            check(t.top(8, rise) == rise && t.top(0, rise) <= plan::Stair::STEP, "  each step under 4 up, the last the floor above");
            t.turn = -1;
            check(t.stepAt({ 20, -5 }, rise) == 5 && t.stepAt({ 20, 5 }, rise) == -1, "  turned the other way");
            t.turn = 1; t.shape = plan::StairShape::U;
            check(t.stepAt(t.toPlan(15, t.jhi() + 1), rise) == 5 && t.stepAt(t.toPlan(0, t.jhi() + 1), rise) == 8 &&
                  t.stepAt(t.toPlan(20, t.jhi() + 5), rise) == 4, "  U: a landing across both, the second flight back beside the first");
            t.shape = plan::StairShape::Spiral;
            const int Ns = t.count(rise);
            check(t.stepAt({ 0, 0 }, rise) == Ns - 1 && t.stepAt({ 5, 0 }, rise) == 0 && t.stepAt({ 0, 5 }, rise) == Ns / 4 &&
                  t.stepAt({ -5, 0 }, rise) == Ns / 2 && t.stepAt({ 11, 0 }, rise) == -1, "  spiral: round its post, a full turn");
            glm::ivec2 lo, hi;
            t.box(rise, lo, hi);
            check(lo == glm::ivec2(-9) && hi == glm::ivec2(9), "  9 out from the post");
        }
        uint32_t us = plans.addStair({ 100, 300 }, 0, 9, 0, GROUND, "", plan::StairShape::U, 1);
        const plan::Plan* up = plans.get(us);
        check(us && up->stairs.size() == 1 && up->stairs[0].shape == plan::StairShape::U, "  a U stair in the garden");
        {
            const plan::Stair& t = up->stairs[0];
            const int rise = up->stairRise(0), wy = up->levelY(0);
            glm::ivec2 v = t.toPlan(0, t.jhi() + 1) + glm::ivec2(up->origin.x, up->origin.z);
            check(at(v.x, wy + rise - 1, v.y) != BLOCK_AIR && at(v.x, wy + rise, v.y) == BLOCK_AIR, "  its top step beside its foot, a storey up");
            glm::ivec2 f = t.toPlan(0, 0) + glm::ivec2(up->origin.x, up->origin.z);
            check(at(f.x, wy + t.top(0, rise) - 1, f.y) != BLOCK_AIR && at(f.x, wy + t.top(0, rise), f.y) == BLOCK_AIR, "  its foot one step up");
        }
        uint32_t sp = plans.addStair({ 140, 300 }, 0, 6, 0, GROUND, "", plan::StairShape::Spiral, -1);
        check(sp && plans.get(sp)->stairs[0].turn == -1, "  a spiral one turning left (both kept through saving, below)");
    }

    std::printf("saving\n");
    plans.save("saves/_plan_test.dat");
    PlanManager again(w.chunks, editor, &sm);
    again.load("saves/_plan_test.dat");
    std::remove("saves/_plan_test.dat");
    check(again.all().size() == plans.all().size() && again.overrideCount(id) == plans.overrideCount(id),
          "plans and their hand edits read back");
    bool same = true;
    for (const plan::Plan& q : plans.all()) {
        plan::Voxels a = plans.target(q), b = again.target(*again.get(q.id));
        same = same && a == b;
    }
    check(same, "  they'd stand the same");
    again.setWallHeight(off, 0, 15);
    check(again.lastWritten > 0 && again.lastWritten < 50 * 3 * 10, "  and change only what changes, after loading");

    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
