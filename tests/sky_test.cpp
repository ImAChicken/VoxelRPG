// Headless checks for the sky world, Mushroom Islands (worldgen/
// SkyGenerator.h, data/worlds/mushroom_islands.toml) and its far view
// (render/SkyFarView.h), and the surface nets that skin it from afar
// (world/SurfaceNets.h). Run from the repo root.
#include "TestWorld.h"
#include "worldgen/WorldType.h"
#include "world/SkyCells.h"
#include "world/SurfaceNets.h"
#include <map>
#include <cstdio>

using namespace testworld;


int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }

    std::printf("the world type\n");
    check(worldTypeFromKey("sky") == WorldType::Sky && std::string(worldTypeKey(WorldType::Sky)) == "sky", "'sky' names the Mushroom Islands type");
    const int size = worldSizeFor(WorldType::Sky, 0);
    check(size == 48, "its size comes from the layout file (" + std::to_string(size) + " chunks)");
    auto gen = makeGenerator(WorldType::Sky, 7, size);
    auto* sky = dynamic_cast<SkyGenerator*>(gen.get());
    check(sky && sky->ok(), "the layout reads");
    if (!sky) return 1;
    const auto& L = sky->skyLayout();
    int scattered = 0;
    for (const auto& is : L.islands) if (is.id.rfind("scatter", 0) == 0) scattered++;
    std::printf("    %zu islands (%d scattered), void at %.0f\n", L.islands.size(), scattered, L.voidY);
    check(sky->island("start") && sky->island("bowl"), "the start and the bowl are there");
    check(scattered >= 20, "small islands scattered between");

    std::printf("the islands float\n");
    const auto* st = sky->island("start");
    glm::ivec3 sp = gen->spawnPoint(size, size);
    check(std::abs(sp.x - (int)st->x) < 2 && std::abs(sp.z - (int)st->z) < 2, "you start on the start island");
    check(sp.y > 380 && sp.y < 520, "… up in the sky (ground at " + std::to_string(sp.y) + ")");
    TerrainColumn mid = gen->column((int)st->x, (int)st->z);
    check(mid.floor > 150 && mid.ground - mid.floor > 30, "under its middle: rock hanging down, air below it (floor " +
          std::to_string(mid.floor) + ", ground " + std::to_string(mid.ground) + ")");
    TerrainColumn rim = gen->column((int)(st->x + st->radius * 0.85f), (int)st->z);
    check(rim.ground - rim.floor < mid.ground - mid.floor, "thinner toward its rim");
    TerrainColumn none = gen->column(40, 40);
    check(none.ground <= none.floor, "out in the void: nothing");
    // the chunk: solid at the island, air under it
    int cx = (int)st->x / VOXEL_DIM, cz = (int)st->z / VOXEL_DIM;
    ChunkPtr c = gen->generateChunk(cx, cz);
    int lx = (int)st->x - cx * VOXEL_DIM, lz = (int)st->z - cz * VOXEL_DIM;
    check(c->typeAt(lx, mid.ground - 1, lz) != BLOCK_AIR && c->typeAt(lx, mid.floor, lz) != BLOCK_AIR, "the chunk holds the island");
    check(c->typeAt(lx, mid.floor - 1, lz) == BLOCK_AIR && c->typeAt(lx, 5, lz) == BLOCK_AIR, "… and air under it, down to the bottom");
    check(c->typeAt(lx, mid.ground, lz) == BLOCK_AIR, "… and over it");
    const auto* bowl = sky->island("bowl");
    float gap = bowl->x - bowl->radius * 1.25f - (st->x + st->radius * 1.25f);
    check(gap > 100.0f, "a gap to cross between the start and the bowl (" + std::to_string((int)gap) + " voxels)");

    std::printf("the stepping stones\n");
    {
        std::vector<const SkyGenerator::Island*> path = { sky->island("start") };
        for (const auto& is : L.islands) if (is.chain == "start>bowl") path.push_back(&is);
        path.push_back(sky->island("bowl"));
        check(path.size() >= 6, std::to_string(path.size() - 2) + " stones from the start to the bowl");
        // every jump, measured on the real ground: the gap between the
        // last ground of one and the first of the next, and the rise
        int bad = 0; float worst = 0.0f;
        for (size_t i = 0; i + 1 < path.size(); i++) {
            const auto* A = path[i]; const auto* B = path[i + 1];
            glm::vec2 a(A->x, A->z), b(B->x, B->z), d = glm::normalize(b - a);
            float len = glm::length(b - a);
            // leave from A's last ground toward B, land on B's first
            float leave = -1, landAt = -1; int hA = 0, hB = 0;
            for (float u = 0; u < len; u += 1.0f) {
                glm::vec2 q = a + d * u;
                auto sp2 = sky->spanAt(q.x, q.y);
                bool onA = sp2.island >= 0 && &L.islands[(size_t)sp2.island] == A;
                bool onB = sp2.island >= 0 && &L.islands[(size_t)sp2.island] == B;
                if (onA) { leave = u; hA = (int)sp2.top; }
                if (onB && landAt < 0) { landAt = u; hB = (int)sp2.top; }
            }
            if (leave < 0 || landAt < 0) { bad++; std::printf("    jump %zu (%s to %s): no ground found (%.0f, %.0f)\n", i, A->id.c_str(), B->id.c_str(), leave, landAt); continue; }
            float gap = landAt - leave, rise = (float)(hB - hA);
            float reach = SkyGenerator::jumpReach(std::max(rise, 0.0f), SkyGenerator::WALK);
            worst = std::max(worst, gap / std::max(reach, 1.0f));
            if (gap > reach || rise > 12.0f) {
                bad++;
                std::printf("    jump %zu: %.0f across, %+.0f up (a walking jump clears %.0f)\n", i, gap, rise, reach);
            }
        }
        std::printf("    the longest jump is %.0f%% of what a walking jump clears\n", worst * 100.0f);
        check(bad == 0, "every jump can be made walking");
    }

    std::printf("the bowl's sites\n");
    {
        const SkyGenerator::Sites& S = sky->bowlSites();
        check(S.ok, "the bonfire, the King and the mine are placed");
        std::printf("    bonfire (%.0f, %.0f), King (%.0f, %.0f), mine (%.0f, %.0f) to (%.0f, %.0f), boards y %d..%d\n",
                    S.bonfire.x, S.bonfire.z, S.king.x, S.king.z, S.mineIn.x, S.mineIn.z, S.mineOut.x, S.mineOut.z, S.sealLo.y, S.sealHi.y);
        auto onFloor = [&](glm::vec3 p) {
            auto sp2 = sky->spanAt(p.x, p.z);
            return sp2.island >= 0 && L.islands[(size_t)sp2.island].id == "bowl" && 1.0f - sp2.in < 0.66f;   // (inside the ridge)
        };
        check(onFloor(S.bonfire) && onFloor(S.king), "the bonfire and the King stand on the bowl's floor");
        TerrainColumn kc = gen->column((int)S.king.x, (int)S.king.z);
        check(kc.eco == island::DenseForest, "the floor counts as dense forest (the Mushnubs roam there)");
        // the boards across the mine, in its chunk
        int planks = 0;
        for (int x = S.sealLo.x; x <= S.sealHi.x; x++)
        for (int z = S.sealLo.z; z <= S.sealHi.z; z++) {
            int ccx = x / VOXEL_DIM, ccz = z / VOXEL_DIM;
            static ChunkPtr held; static int hx = -1, hz = -1;
            if (ccx != hx || ccz != hz) { held = gen->generateChunk(ccx, ccz); hx = ccx; hz = ccz; }
            for (int y = S.sealLo.y + 1; y <= S.sealHi.y; y++)
                if (held->typeAt(x - ccx * VOXEL_DIM, y, z - ccz * VOXEL_DIM) == BLOCK_PLANKS) planks++;
        }
        check(planks > 30, "boards seal the mine (" + std::to_string(planks) + " planks)");
        // the tunnel: near level, open through the mountains
        float run = glm::length(glm::vec2(S.mineOut.x - S.mineIn.x, S.mineOut.z - S.mineIn.z));
        check(S.mineOut.y - S.mineIn.y <= run * 0.12f + 1.0f && S.mineOut.y >= S.mineIn.y, "the mine climbs gently (" +
              std::to_string((int)(S.mineOut.y - S.mineIn.y)) + " up over " + std::to_string((int)run) + ")");
        int open = 0, tried = 0;
        for (float u = 0.2f; u < 0.85f; u += 0.1f) {
            glm::vec3 q = S.mineIn + (S.mineOut - S.mineIn) * u;
            int fy = (int)std::lround(S.mineIn.y + (S.mineOut.y - S.mineIn.y) * u);
            int qx = (int)q.x, qz = (int)q.z, ccx = qx / VOXEL_DIM, ccz = qz / VOXEL_DIM;
            ChunkPtr ch = gen->generateChunk(ccx, ccz);
            tried++;
            if (ch->typeAt(qx - ccx * VOXEL_DIM, fy + 3, qz - ccz * VOXEL_DIM) == BLOCK_AIR) open++;
        }
        check(open == tried, "the tunnel is open the whole way (" + std::to_string(open) + "/" + std::to_string(tried) + ")");
    }

    std::printf("the void kills\n");
    {
        World w;
        w.physics.setKillY((float)GROUND + 60.0f);
        w.sim.setVoidKills(true);
        Player& p = w.player(glm::vec3(256, GROUND + 50, 256));     // (under the void height)
        w.sim.step(DT);
        check(p.dead(), "below the void you die");
        w.physics.setKillY(-64.0f);                 // (its spawn point is under that test height)
        w.run(6.0f);
        check(!p.dead() && glm::distance(p.position, p.spawnPoint) < 80.0f, "… and come back at your spawn point");
    }
    {
        World w;
        w.physics.setKillY(-64.0f);
        w.sim.setVoidKills(true);
        Player& q = w.player(glm::vec3(300, GROUND, 300));
        w.run(0.2f);
        check(!q.dead(), "(on the ground over it you don't)");
    }

    std::printf("the far view's cells\n");
    {
        skycells::LookTable looks(gRegistry.size());
        for (size_t id = 1; id < gRegistry.size(); id++) {
            const VoxelTypeDef& d = gRegistry.get((uint16_t)id);
            looks[id].kind = d.isFluid() ? 2 : (d.isPlant() || d.opacity == VoxelOpacity::None) ? 0 : 1;
            looks[id].color = glm::u8vec3((uint8_t)(d.color[0] * 255), (uint8_t)(d.color[1] * 255), (uint8_t)(d.color[2] * 255));
            looks[id].glow = d.glows() && looks[id].kind == 1;
        }
        auto fromGen = skycells::fromGenerator(*gen, looks, cx, cz);
        std::array<std::shared_ptr<const RleData>, SECTIONS> secs;
        for (int s = 0; s < SECTIONS; s++) secs[(size_t)s] = c->snapshot(s);
        auto fromVox = skycells::fromVoxels(secs, looks);
        int col = (lz / skycells::CELL) * skycells::CPC + lx / skycells::CELL;
        auto span = [&](const skycells::ChunkCells& cc, int& lo, int& hi) {
            lo = 999; hi = -1;
            for (uint32_t k = cc.start[(size_t)col]; k < cc.start[(size_t)col + 1]; k++) { lo = std::min(lo, (int)cc.runs[k].y0); hi = std::max(hi, (int)cc.runs[k].y1); }
        };
        int g0, g1, v0, v1;
        span(*fromGen, g0, g1);
        span(*fromVox, v0, v1);
        std::printf("    the start island's middle: cells %d..%d from the generator, %d..%d from its voxels\n", g0, g1, v0, v1);
        TerrainColumn at = gen->column(cx * VOXEL_DIM + (lx / skycells::CELL) * skycells::CELL + skycells::CELL / 2,
                                       cz * VOXEL_DIM + (lz / skycells::CELL) * skycells::CELL + skycells::CELL / 2);
        check(g1 > g0 && g0 == at.floor / skycells::CELL && g1 == (at.ground - 1) / skycells::CELL + 1,
              "from the generator: the island's span of cells");
        // (the underside is ragged: the real voxels reach a little lower somewhere in the cell)
        check(v1 == g1 && v0 <= g0 && g0 - v0 <= 3, "from the real voxels: the same span (the bottom within a few cells)");
        const skycells::Run& top = fromVox->runs[fromVox->start[(size_t)col + 1] - 1];
        glm::u8vec3 moss = looks[gRegistry.getIDByName("moss")].color;
        check(top.r == moss.r && top.g == moss.g && top.b == moss.b, "its top cell takes the top block's colour (moss)");
        auto empty = skycells::fromGenerator(*gen, looks, 0, 0);
        check(empty->runs.empty(), "a chunk of void has no cells");
    }


    std::printf("surface nets (the far view's smooth skin)\n");
    {
        using namespace surfacenets;
        // a ball of density, radius 5, in a 16^3 grid
        Grid g; g.resize(16, 16, 16);
        const glm::vec3 C(7.5f, 7.5f, 7.5f);
        for (int y = 0; y < 16; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            float r = glm::length(glm::vec3(x, y, z) - C);
            Sample& sm = g.at(x, y, z);
            sm.d = std::clamp(0.5f + (5.0f - r) * 0.25f, 0.0f, 1.0f);
            sm.r = 200; sm.topY = (int16_t)y;
        }
        Mesh m; mesh(g, 0.5f, glm::ivec3(0), glm::ivec3(15), m);
        check(!m.quads.empty(), "a ball makes a skin (" + std::to_string(m.quads.size()) + " quads)");
        // closed: every edge of the triangles used twice, once each way
        std::vector<uint32_t> idx;
        for (const Quad& q : m.quads) { idx.insert(idx.end(), { q.v[0], q.v[1], q.v[2], q.v[0], q.v[2], q.v[3] }); }
        std::map<std::pair<uint32_t, uint32_t>, int> edges;
        for (size_t t = 0; t < idx.size(); t += 3)
            for (int k = 0; k < 3; k++) edges[{ idx[t + k], idx[t + (k + 1) % 3] }]++;
        bool closed = true;
        for (auto& [e, n] : edges) if (n != 1 || !edges.count({ e.second, e.first })) { closed = false; break; }
        check(closed, "… closed, each edge shared by two triangles facing the same way");
        // on the ball, facing out
        bool round = true, outward = true, normals = true;
        for (const Vert& v : m.verts) {
            float r = glm::length(v.p - C);
            if (std::fabs(r - 5.0f) > 0.6f) round = false;
            if (glm::dot(v.n, v.p - C) <= 0.0f) normals = false;
        }
        for (size_t t = 0; t < idx.size(); t += 3) {
            glm::vec3 a = m.verts[idx[t]].p, b = m.verts[idx[t + 1]].p, c = m.verts[idx[t + 2]].p;
            if (glm::dot(glm::cross(b - a, c - a), (a + b + c) / 3.0f - C) <= 0.0f) { outward = false; break; }
        }
        check(round, "its vertices lie on the ball (within 0.6)");
        check(outward, "its triangles wind counter-clockwise seen from outside (the far view culls the back)");
        check(normals, "its normals point out");
        // two grids overlapping by a sample, each making its own half: the same skin
        Mesh a, b;
        mesh(g, 0.5f, glm::ivec3(0), glm::ivec3(7, 15, 15), a);
        mesh(g, 0.5f, glm::ivec3(8, 0, 0), glm::ivec3(15), b);
        check(a.quads.size() + b.quads.size() == m.quads.size(), "split in two by owner, the halves make the whole (no seam drawn twice, none missed)");
        // tops only
        Mesh t; mesh(g, 0.5f, glm::ivec3(0), glm::ivec3(15), t, true);
        bool up = !t.quads.empty();
        for (const Quad& q : t.quads) if (q.axis != 1 || q.dir != 1) up = false;
        check(up && t.quads.size() < m.quads.size() / 3, "tops only: just the quads facing up");
        // one lone solid sample still shows (a small island stays a lump)
        Grid one; one.resize(5, 5, 5);
        one.at(2, 2, 2).d = 0.5f;
        Mesh lump; mesh(one, 0.4f, glm::ivec3(0), glm::ivec3(4), lump);
        check(lump.quads.size() == 6, "a lone cell just over the iso level: a small closed lump (6 quads)");
        // the colour: the highest solid corner's
        Grid col; col.resize(3, 4, 3);
        for (int z = 0; z < 3; z++) for (int x = 0; x < 3; x++) for (int y = 0; y < 2; y++) {
            Sample& sm = col.at(x, y, z); sm.d = 1.0f; sm.topY = (int16_t)y;
            sm.g = y == 1 ? 200 : 40;                       // grass over dirt
        }
        Mesh cm; mesh(col, 0.5f, glm::ivec3(0), glm::ivec3(2, 3, 2), cm);
        bool grass = !cm.verts.empty();
        for (const Vert& v : cm.verts) if (v.p.y > 1.4f && v.g != 200) grass = false;
        check(grass, "the top of a slab takes its top cells' colour");
    }

    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
