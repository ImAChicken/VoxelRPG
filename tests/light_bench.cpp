// A profile, not a test: how long light::Baker::bake (world/LightBake.h)
// takes on real columns, in a dungeon world (torches in rooms) and on an
// island. Build with -DLIGHT_MARGIN=32 or 48 to see what a longer light
// reach costs (the margin is how far light can come from the next
// columns, so it's what bounds the reach). Run from the repo root.
// Measured 2026-11-20 (2 cores, -O3): margin 16 (now) 37 ms dungeon /
// 45 ms island; 32: 63 / 112; 48: 95 / 118.
#include "TestWorld.h"
#include "worldgen/WorldType.h"
#include "world/LightBake.h"
#include <chrono>
#include <cstdio>
using namespace testworld;
static double benchGen(const TerrainGenerator& gen, std::vector<glm::ivec2> cols) {
    auto table = light::LightTable::build(gRegistry);
    double total = 0; int n = 0;
    for (glm::ivec2 c : cols) {
        std::array<std::array<ChunkPtr, 3>, 3> ch;
        std::array<std::array<light::Snap, 3>, 3> snaps;
        light::Snap9 nine{};
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            ch[dz + 1][dx + 1] = gen.generateChunk(c.x + dx, c.y + dz);
            for (int s = 0; s < SECTIONS; s++) snaps[dz + 1][dx + 1][s] = ch[dz + 1][dx + 1]->snapshot(s);
            nine[dz + 1][dx + 1] = &snaps[dz + 1][dx + 1];
        }
        light::ColumnLight out;
        light::Baker::bake(nine, *table, c.x, c.y, out);     // warm up
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 3; k++) light::Baker::bake(nine, *table, c.x, c.y, out);
        total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 3; n++;
    }
    return total / n;
}
int main() {
    loadData();
    DungeonGenerator dg(7, DungeonGenerator::SIZE_CHUNKS);
    std::vector<glm::ivec2> dc;
    for (const auto& t : dg.dungeonLayout().tiles) { glm::ivec3 w = dg.worldOf(t); glm::ivec2 c(w.x / 128, w.z / 128); if (std::find(dc.begin(), dc.end(), c) == dc.end()) dc.push_back(c); }
    if (dc.size() > 4) dc.resize(4);
    std::printf("margin %d: dungeon column %.1f ms\n", light::MARGIN, benchGen(dg, dc));
    auto ig = makeGenerator(WorldType::Island, 42, 72);
    std::vector<glm::ivec2> ic = { {30, 30}, {36, 36}, {40, 30}, {30, 40} };
    std::printf("margin %d: island column %.1f ms\n", light::MARGIN, benchGen(*ig, ic));
}
