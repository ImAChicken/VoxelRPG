// Headless checks for slopes (VoxelPhysics::slopeFactor): walking
// up a hill is slower than across flat ground, down a little faster,
// and a body's 'climb' scales it; NPC paths weigh climbing the same
// way (nav::Pathfinder::climbCost). Run from the repo root.
#include "TestWorld.h"
#include "npc/Pathfinder.h"
#include <cstdio>

using namespace testworld;

// A ramp along +x from x0: rises 'rise' voxels per 'run' voxels
static void ramp(World& w, int x0, int z0, int len, int run, int rise) {
    uint16_t stone = gRegistry.getIDByName("stone");
    for (int i = 0; i < len; i++) {
        int h = (i / run) * rise;
        for (int z = z0 - 6; z <= z0 + 6; z++)
            for (int y = GROUND; y < GROUND + h; y++)
                if (Chunk* c = w.chunks.getChunk((x0 + i) / VOXEL_DIM, z / VOXEL_DIM))
                    c->setBlock((x0 + i) % VOXEL_DIM, y, z % VOXEL_DIM, stone);
    }
}

// How long a body takes to walk 'dist' voxels along +x
static float walkTime(World& w, Human& h, EntityId id, float dist, bool sprint, bool isPlayer) {
    float x0 = h.position.x;
    HumanIntent in;
    in.move = glm::vec2(1.0f, 0.0f);
    in.sprint = sprint;
    for (int i = 0; i < 60 * 30; i++) {
        if (isPlayer) w.sim.setIntent(id, in);
        w.sim.step(DT);
        if (h.position.x - x0 >= dist) return i * DT;
    }
    return 999.0f;
}

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }

    std::printf("walking\n");
    float flat, up45, upGentle, down;
    {
        World w;
        Player& p = w.player(glm::vec3(100, GROUND, 100));
        w.run(0.5f);
        flat = walkTime(w, p, p.uuid, 48.0f, true, true);
    }
    {
        World w;
        ramp(w, 104, 100, 100, 1, 1);            // 45 degrees: a voxel up per voxel along
        Player& p = w.player(glm::vec3(100, GROUND, 100));
        w.run(0.5f);
        up45 = walkTime(w, p, p.uuid, 48.0f, true, true);
    }
    {
        World w;
        ramp(w, 104, 100, 100, 3, 1);            // 1 in 3
        Player& p = w.player(glm::vec3(100, GROUND, 100));
        w.run(0.5f);
        upGentle = walkTime(w, p, p.uuid, 48.0f, true, true);
    }
    {
        World w;
        ramp(w, 104, 100, 100, 1, 1);
        Player& p = w.player(glm::vec3(204 - 52, GROUND + 49, 100));   // on the slope, walking down it (-x)
        w.run(0.5f);
        float x0 = p.position.x;
        HumanIntent in; in.move = glm::vec2(-1.0f, 0.0f); in.sprint = true;
        int i = 0;
        for (; i < 60 * 30 && x0 - p.position.x < 40.0f; i++) { w.sim.setIntent(p.uuid, in); w.sim.step(DT); }
        down = i * DT;
    }
    std::printf("    48 voxels: flat %.2f s, 1 in 3 up %.2f s, 45 degrees up %.2f s; 40 down %.2f s\n", flat, upGentle, up45, down);
    check(up45 > flat * 1.7f && up45 < flat * 2.6f, "up a 45 degree slope takes about twice as long");
    check(upGentle > flat * 1.15f && upGentle < up45, "a gentle slope slows you less");
    check(down * 48.0f / 40.0f < flat, "downhill is a little quicker");

    std::printf("climb factor\n");
    {
        World w;
        ramp(w, 104, 100, 100, 1, 1);
        Player& p = w.player(glm::vec3(100, GROUND, 100));
        p.climb = 0.2f;                           // a goat
        w.run(0.5f);
        float goat = walkTime(w, p, p.uuid, 48.0f, true, true);
        check(goat < up45 * 0.75f && goat > flat, "a good climber (climb 0.2) barely slows: " + std::to_string(goat) + " s");
    }

    std::printf("paths\n");
    {
        World w;
        // a long low ridge between A and B (8 voxels high, 1 in 5
        // each side): over it is shorter, round its end is flat
        uint16_t stone = gRegistry.getIDByName("stone");
        for (int x = 105; x <= 155; x++)
            for (int z = 60; z <= 140; z++) {
                int h = std::max(0, 8 - std::abs(z - 100) / 5);
                for (int y = GROUND; y < GROUND + h; y++)
                    if (Chunk* c = w.chunks.getChunk(x / VOXEL_DIM, z / VOXEL_DIM)) c->setBlock(x % VOXEL_DIM, y, z % VOXEL_DIM, stone);
            }
        nav::Pathfinder pf(w.physics);
        auto over = [&](const nav::Result& r) { float top = 0; for (auto& q : r.points) top = std::max(top, q.y); return top; };
        pf.climbCost = 0.1f;
        nav::Result goat = pf.find({ 130, GROUND, 50 }, { 130, GROUND, 150 });
        pf.climbCost = 10.0f;
        nav::Result heavy = pf.find({ 130, GROUND, 50 }, { 130, GROUND, 150 });
        std::printf("    climber: reached %d top %.0f; poor climber: reached %d top %.0f\n", goat.reached, over(goat) - GROUND,
                    heavy.reached, over(heavy) - GROUND);
        check(goat.reached && over(goat) > GROUND + 6, "a climber's path goes over the hill");
        check(heavy.reached && over(heavy) < GROUND + 4, "a poor climber's path goes round it");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
