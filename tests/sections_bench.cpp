// A profile, not a test: what a column costs with the build's SECTIONS
// (Voxel.h; build with -DVOXEL_SECTIONS=16 to try a 2048-voxel world).
// Times generating an island column and baking its light, and counts
// the memory a column's sections and light take. Run from the repo root.
// Measured 2026-11-25 (3 cores, -O3, an island column):
//   8 sections:  generate 34-39 ms, light 57 ms, sections 237 KB, light 1128 KB
//   16 sections: generate 33 ms,    light 95 ms, sections 237 KB, light 1128 KB
// Empty sections cost no memory; the light bake goes through every
// section, so a taller world bakes ~65% slower until it learns to skip
// the open sky above everything. Kept at 8 for now (ROADMAP Mushroom
// Islands, step 0).
#include "TestWorld.h"
#include "worldgen/WorldType.h"
#include "world/LightBake.h"
#include <chrono>
#include <cstdio>
using namespace testworld;
int main() {
    loadData();
    auto gen = makeGenerator(WorldType::Island, 4242, 72);
    auto table = light::LightTable::build(gRegistry);
    std::vector<glm::ivec2> cols = { {30, 30}, {36, 36}, {40, 28}, {33, 40} };
    double genMs = 0, bakeMs = 0; size_t rle = 0, lightB = 0; int n = 0;
    for (glm::ivec2 c : cols) {
        std::array<std::array<ChunkPtr, 3>, 3> ch;
        std::array<std::array<light::Snap, 3>, 3> snaps;
        light::Snap9 nine{};
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            auto t0 = std::chrono::steady_clock::now();
            ch[dz + 1][dx + 1] = gen->generateChunk(c.x + dx, c.y + dz);
            genMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            for (int s = 0; s < SECTIONS; s++) snaps[dz + 1][dx + 1][s] = ch[dz + 1][dx + 1]->snapshot(s);
            nine[dz + 1][dx + 1] = &snaps[dz + 1][dx + 1];
        }
        for (int s = 0; s < SECTIONS; s++) { auto r = ch[1][1]->snapshot(s); if (r) rle += r->ends.size() * 6; }
        light::ColumnLight out;
        light::Baker::bake(nine, *table, c.x, c.y, out);
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 3; k++) light::Baker::bake(nine, *table, c.x, c.y, out);
        bakeMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 3;
        for (const auto& sl : out.sections) lightB += sl.cells.size() * 2 + sl.tint.size() * 2;
        lightB += sizeof(light::ColumnLight);
        n++;
    }
    std::printf("SECTIONS %d (%d voxels tall): generate a column %.1f ms, bake its light %.1f ms; "
                "a column's sections %.0f KB, its light %.0f KB\n",
                SECTIONS, WORLD_HEIGHT, genMs / (n * 9), bakeMs / n, rle / 1024.0 / n, lightB / 1024.0 / n);
    return 0;
}
