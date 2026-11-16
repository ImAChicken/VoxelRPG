// Headless checks for spawner structures (structures.toml kind =
// "spawner", sim/Spawners.h): their drawn models, waking only
// with a player near, filling up, replacing the fallen after the
// respawn time, being removed, and their NPCs being saved.
// Run from the repo root.
#include "TestWorld.h"
#include "world/StructureManager.h"
#include "world/WorldEditor.h"
#include <cstdio>
#include <cstdlib>

using namespace testworld;

static int countFrom(Simulation& sim, uint32_t id, bool aliveOnly = true) {
    int n = 0;
    for (auto& x : sim.npcs().all()) if (x->spawner == id && (!aliveOnly || !x->dead())) n++;
    return n;
}

int main() {
    if (!loadData() || !gStructures.load("data/registry/structures.toml")) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }

    std::printf("the registry\n");
    const uint16_t CAMP = gStructures.find("bandit_camp"), POST = gStructures.find("training_post");
    const StructureType* camp = gStructures.get(CAMP);
    const StructureType* post = gStructures.get(POST);
    check(camp && post && gStructures.get(gStructures.find("grave")), "three spawners load");
    if (!camp || !post) return 1;
    check(camp->kind == StructureKind::Spawner && camp->spawn.maxAlive == 3 && camp->spawn.npcs.size() == 1 &&
          camp->spawn.npcs[0].first == "bandit" && camp->spawn.respawn == 45.0f, "the camp's spawn settings");
    const VoxelVolume& v = camp->shape(0, 0);
    check(v.size == glm::ivec3(7, 14, 7), "the camp's model is 7 x 14 x 7");
    check(v.at(3, 1, 3) == gRegistry.getIDByName("fire") && v.at(1, 0, 1) == BLOCK_COBBLESTONE &&
          v.at(6, 13, 6) == gRegistry.getIDByName("dark_log") && v.at(3, 9, 6) == gRegistry.getIDByName("maple_leaves_red"),
          "drawn layers land where they're drawn (fire, ring, pole, flag)");
    check(v.anchor == glm::ivec3(3, 0, 3), "placed by its bottom centre");
    check(post->spawn.npcs[0].first == "dummy" && post->spawn.maxAlive == 2, "the training post makes dummies");

    std::printf("placing\n");
    {
        World w;
        WorldEditor ed(w.chunks);
        TerrainGenerator gen(1);
        StructureManager sm(w.chunks, ed, gen);
        glm::ivec3 at(200, GROUND, 200);
        uint16_t grass = gRegistry.getIDByName("short_grass");
        auto put = [&](int x, int y, int z, uint16_t id) {
            w.chunks.getChunk(x / VOXEL_DIM, z / VOXEL_DIM)->setBlock(x % VOXEL_DIM, y, z % VOXEL_DIM, id);
        };
        for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) put(at.x + dx, at.y, at.z + dz, grass);
        check(sm.checkPlace(POST, at, 0, false).empty(), "a spawner can go down on grass (the grass makes way)");
        sm.place(POST, at, 0, EditSource::Command, 0);
        ed.flush();
        check(w.chunks.typeAtVoxel(at.x, at.y, at.z) == BLOCK_PLANKS && w.chunks.typeAtVoxel(at.x, at.y + 5, at.z) == BLOCK_LOG_Y,
              "and stands where it was put");
        int placed = 0;
        sm.forEachInstance([&](const StructureInstance& si) { if (si.type == POST) placed++; });
        check(placed == 1, "as one structure instance");
        put(at.x + 20, at.y, at.z, BLOCK_STONE);
        check(!sm.checkPlace(POST, at + glm::ivec3(20, 0, 0), 0, false).empty(), "but not into stone");
    }

    std::printf("waking, filling, replacing\n");
    {
        World w;
        glm::vec3 spot(256.5f, (float)GROUND, 256.5f);
        Player& p = w.player(spot + glm::vec3(0, 0, 120));          // too far: it sleeps
        w.sim.syncSpawners({ { 7, POST, spot } });
        w.run(1.0f);
        check(countFrom(w.sim, 7) == 0 && !w.sim.spawners().all()[0].active, "asleep with no player near");
        p.position = spot + glm::vec3(0, 0, 30);                   // within 64
        w.run(0.2f);
        check(countFrom(w.sim, 7) == 2, "a player near: it fills up at once (2)");
        bool inside = true, homed = true;
        for (auto& x : w.sim.npcs().all())
            if (x->spawner == 7) {
                float d = glm::length(glm::vec2(x->position.x - spot.x, x->position.z - spot.z));
                inside = inside && d <= post->spawn.radius + 1.0f && d >= 2.0f;
                homed = homed && glm::length(x->home - spot) < 0.01f;
            }
        check(inside, "they appear around it, within its radius");
        check(homed, "and live around it (their home is the spawner)");
        w.run(10.0f);
        check(countFrom(w.sim, 7) == 2, "full: no more");
        // fell one
        EntityId victim = 0;
        for (auto& x : w.sim.npcs().all()) if (x->spawner == 7) { victim = x->uuid; break; }
        Human* h = w.sim.find(victim);
        w.sim.toolHit(p.uuid, victim, 9999.0f, 999.0f, h->bodyPoint(0.6f), glm::vec3(1, 0, 0));
        w.run(0.5f);
        check(countFrom(w.sim, 7) == 1, "one falls");
        w.run(post->spawn.respawn - 1.0f);
        check(countFrom(w.sim, 7) == 1, "no replacement before the respawn time");
        w.run(1.5f);
        check(countFrom(w.sim, 7) == 2, "replaced after it");
        check(w.sim.spawners().all()[0].spawned == 3, "three made in all");
        // never right by a player
        bool clear = true;
        for (auto& x : w.sim.npcs().all())
            if (x->spawner == 7 && x->uuid != victim)
                clear = clear && glm::length(x->position - p.position) >= post->spawn.minPlayer - 0.5f;
        check(clear, "never closer to the player than min_player_distance");

        // saved and loaded: they still belong to it, so it doesn't double up
        w.sim.npcs().save("/tmp/spawner_test_npcs.dat");
        World w2;
        Player& p2 = w2.player(spot + glm::vec3(0, 0, 30));
        (void)p2;
        int loaded = w2.sim.npcs().load("/tmp/spawner_test_npcs.dat");
        w2.sim.syncSpawners({ { 7, POST, spot } });
        w2.run(1.0f);
        check(loaded == 2 && countFrom(w2.sim, 7) == 2, "a reloaded world keeps them and makes no more");
        std::remove("/tmp/spawner_test_npcs.dat");

        // removed: it stops, its NPCs stay
        w.sim.syncSpawners({});
        int stayed = 0;
        for (auto& x : w.sim.npcs().all()) if (!x->dead() && x->spawner == 0) stayed++;
        check(w.sim.spawners().all().empty() && stayed == 2, "removed: it stops, its NPCs stay on their own");
    }

    std::printf("a camp\n");
    {
        World w;
        glm::vec3 spot(256.5f, (float)GROUND, 256.5f);
        w.player(spot + glm::vec3(0, 0, 60));
        w.sim.syncSpawners({ { 9, CAMP, spot } });
        w.run(0.5f);
        int bandits = 0;
        for (auto& x : w.sim.npcs().all()) if (x->spawner == 9 && x->typeId == "bandit") bandits++;
        check(bandits == 3, "three bandits around the camp");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
