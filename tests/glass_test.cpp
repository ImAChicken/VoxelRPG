// Headless checks for window glass (blocks.toml render = "pane",
// filter): the mesher draws one sheet through the voxel, not a cube,
// and the light bake colours the sky and lamp light that come through
// it (world/LightBake.h). Run from the repo root.
#include "TestWorld.h"
#include "render/Mesher.h"
#include "world/LightBake.h"
#include <cstdio>
#include <vector>

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    const uint16_t STONE = gRegistry.getIDByName("stone"), RED = gRegistry.getIDByName("stained_glass_red");
    const uint16_t LAMP = gRegistry.getIDByName("lantern");
    check(RED && gRegistry.get(RED).pane && gRegistry.get(RED).filters(), "stained glass is a pane with a filter");
    check(gRegistry.get(gRegistry.getIDByName("glass")).pane, "plain glass is a pane");

    // one column: stone up to y 128, a stone room on it (x, z 40..60,
    // y 128..160) closed but for a red window in its west wall
    std::vector<uint16_t> raw((size_t)VOXEL_COUNT, BLOCK_AIR);
    auto set = [&](int x, int y, int z, uint16_t id) { raw[(size_t)Chunk::index(x, y, z)] = id; };
    // (walls 2 thick: the bake's cells are 2 voxels, and a cell blocks
    // light when most of it is solid)
    for (int y = 0; y <= 33; y++)
        for (int x = 40; x <= 61; x++)
            for (int z = 40; z <= 61; z++) {
                bool shell = x <= 41 || x >= 60 || z <= 41 || z >= 60 || y <= 1 || y >= 32;
                if (shell) set(x, y, z, STONE);
            }
    for (int y = 10; y < 18; y++) for (int z = 46; z < 54; z++) { set(40, y, z, BLOCK_AIR); set(41, y, z, RED); }
    light::Snap col;
    col[0] = [] { std::vector<uint16_t> r((size_t)VOXEL_COUNT, 1); return RleData::build(r.data()); }();
    col[1] = RleData::build(raw.data());
    for (int s = 2; s < SECTIONS; s++) col[s] = RleData::allAir();
    light::Snap9 nine{};
    nine[1][1] = &col;
    auto table = light::LightTable::build(gRegistry);
    light::ColumnLight out;
    light::Baker::bake(nine, *table, 0, 0, out);

    const light::SectionLight& sl = out.sections[1];
    check(!sl.tint.empty(), "the section with the window has a tint");
    auto at = [&](int x, int y, int z) {          // world voxel → the section's texel (cells, with the border)
        int tx = x / 2 + 1, ty = (y + 128) / 2 - (64 - 1), tz = z / 2 + 1;
        return light::SectionLight::at(tx, ty, tz);
    };
    if (!sl.tint.empty()) {
        uint16_t inside = sl.tint[at(48, 14, 50)], outside = sl.tint[at(20, 14, 50)];
        int r = (inside >> 12) & 15, g = (inside >> 8) & 15, b = (inside >> 4) & 15;
        std::printf("    inside the room: tint r%d g%d b%d, sky %d\n", r, g, b, light::skyOf(sl.cells.empty() ? sl.value : sl.cells[at(48, 14, 50)]));
        check(r > g + 6 && r > b + 6, "inside, the sky through the red window is red");
        check(outside == light::CLEAR, "outside, it's clear");
        int sky = light::skyOf(sl.cells.empty() ? sl.value : sl.cells[at(48, 14, 50)]);
        check(sky > 0, "and some sky gets in");
    }

    // a lamp behind green glass: its light past the glass is green
    if (LAMP) {
        std::vector<uint16_t> raw2((size_t)VOXEL_COUNT, BLOCK_AIR);
        auto set2 = [&](int x, int y, int z, uint16_t id) { raw2[(size_t)Chunk::index(x, y, z)] = id; };
        const uint16_t GREEN = gRegistry.getIDByName("stained_glass_green");
        // a sealed stone box round a lamp, one side glass
        for (int y = 0; y <= 10; y++) for (int x = 60; x <= 70; x++) for (int z = 60; z <= 70; z++)
            if (x == 60 || x == 70 || z == 60 || z == 70 || y == 0 || y == 10) set2(x, y, z, STONE);
        for (int y = 1; y < 10; y++) for (int z = 61; z < 70; z++) set2(70, y, z, GREEN);
        set2(65, 5, 65, LAMP);
        light::Snap c2 = col;
        c2[1] = RleData::build(raw2.data());
        light::Snap9 n2{};
        n2[1][1] = &c2;
        light::ColumnLight o2;
        light::Baker::bake(n2, *table, 0, 0, o2);
        const light::SectionLight& s2 = o2.sections[1];
        uint16_t past = s2.cells.empty() ? s2.value : s2.cells[at(74, 5, 65)];
        std::printf("    past the green glass: r%d g%d b%d\n", light::rOf(past), light::gOf(past), light::bOf(past));
        const VoxelTypeDef& L = gRegistry.get(LAMP);
        check(light::gOf(past) > 0 && (L.light[0] == 0 || light::rOf(past) < light::gOf(past)), "a lamp's light through green glass is green");
    }

    // the mesher: a sheet, not a cube
    {
        std::vector<uint16_t> m((size_t)VOXEL_COUNT, BLOCK_AIR);
        for (int x = 10; x < 20; x++) for (int y = 10; y < 20; y++) m[(size_t)Chunk::index(x, y, 30)] = STONE;
        for (int x = 13; x < 16; x++) for (int y = 13; y < 16; y++) m[(size_t)Chunk::index(x, y, 30)] = RED;
        MeshInput in;
        in.center = RleData::build(m.data());
        in.bottomOfWorld = true;
        ResourcePackManager packs;
        auto rt = std::make_shared<BlockRenderTable>();
        rt->blocks.resize(gRegistry.size());
        for (size_t i = 1; i < gRegistry.size(); i++) {
            const VoxelTypeDef& d = gRegistry.get((uint16_t)i);
            rt->blocks[i].opaque = d.opacity == VoxelOpacity::Full;
            rt->blocks[i].pane = d.pane;
        }
        in.table = rt;
        ChunkMeshData out;
        ChunkMesher::build(in, out);
        int panes = (int)out.paneCount / 6;
        bool facing = true;
        for (size_t i = out.vertices.size() - out.paneCount; i < out.vertices.size(); i++)
            facing = facing && out.vertices[i].face == 8 && out.vertices[i].z == 30;
        check(panes == 3, "the 3 x 3 window is 3 sheets, one per row (" + std::to_string(panes) + ")");
        check(facing, "  across the wall (face 8, in the wall's plane)");
    }

    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
