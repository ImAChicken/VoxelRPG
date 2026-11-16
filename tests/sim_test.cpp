// Headless checks for the Simulation (sim/Simulation.h): the real
// physics (VoxelPhysics) on a small flat world, players driven
// by intents the way a network client would, enemies, combat,
// events by entity id, death and respawn.
//
// Run from the repo root (it loads data/registry/*.toml).
#include "sim/Simulation.h"
#include "physics/VoxelPhysics.h"
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}
static const float DT = 1.0f / 60.0f;
static const int GROUND = 40;   // the top of the ground: bodies stand at y = GROUND

// Four chunks of flat stone around (128, 128)
static void flatWorld(ChunkManager& world) {
    uint16_t stone = gRegistry.getIDByName("stone");
    for (int cx = 0; cx < 2; cx++)
        for (int cz = 0; cz < 2; cz++) {
            ChunkPtr c = makeChunk();
            for (int x = 0; x < VOXEL_DIM; x++)
                for (int z = 0; z < VOXEL_DIM; z++)
                    for (int y = 0; y < GROUND; y++) c->setBlock(x, y, z, stone);
            world.setChunk(cx, cz, std::move(c));
        }
}
static void run(Simulation& sim, float seconds, EntityId who = 0, HumanIntent in = HumanIntent()) {
    for (int i = 0; i < (int)std::lround(seconds / DT); i++) {
        if (who) sim.setIntent(who, in);
        sim.step(DT);
    }
}
static HumanIntent once(bool light, bool heavy = false) {
    HumanIntent in; in.attackLight = light; in.attackHeavy = heavy; return in;
}

int main() {
    if (!gRegistry.load("data/registry/blocks.toml") || !gItems.load(gRegistry, "data/registry/items.toml") ||
        !gMovesets.load("data/registry/movesets.toml") || !gFactions.load("data/registry/factions.toml") ||
        !gNpcTypes.load("data/registry/npcs.toml", "data/characters/presets.toml")) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }
    ChunkManager world;
    flatWorld(world);
    VoxelPhysics physics;
    physics.init(world);

    Simulation sim;
    sim.attachWorld(&physics, nullptr);
    Player& p = sim.addPlayer();
    p.setMode(GameMode::Survival);
    p.position = glm::vec3(128, GROUND + 0.5f, 128);
    p.spawnPoint = glm::vec3(128, GROUND, 128);
    p.facing = glm::vec3(0, 0, -1);
    if (uint16_t s = gItems.find("iron_sword")) p.inventory.setSlot(Inventory::FIRST_MAIN, s, 1);
    p.inventory.select(Inventory::FIRST_MAIN);

    std::printf("players by intent\n");
    {
        run(sim, 0.5f);
        check(p.grounded && std::fabs(p.position.y - GROUND) < 0.05f, "lands on the ground");
        HumanIntent walk; walk.move = glm::vec2(0, 1);
        glm::vec3 from = p.position;
        run(sim, 1.0f, p.uuid, walk);
        check(glm::length(p.position - from) > 10.0f, "walks where the intent says");
        run(sim, 0.5f, p.uuid, HumanIntent());
        check(p.weaponDamage > 0.0f, "the sword in hand is the weapon");
        p.position = glm::vec3(128, GROUND, 128); p.resetMotion();
        run(sim, 0.2f);
    }

    std::printf("jumping\n");
    {
        auto jumpPeak = [&](bool second) {
            p.position = glm::vec3(128, GROUND, 128); p.resetMotion();
            run(sim, 0.3f);
            HumanIntent j; j.jump = true;
            sim.setIntent(p.uuid, j); sim.step(DT);
            float top = p.position.y;
            for (int i = 0; i < 120; i++) {
                if (second && i == 20) sim.setIntent(p.uuid, j);
                sim.step(DT);
                top = std::max(top, p.position.y);
            }
            return top - GROUND;
        };
        float one = jumpPeak(false), two = jumpPeak(true);
        check(one > 2.0f, "jumps");
        check(std::fabs(two - one) < 0.05f, "no double jump by default");
        p.canDoubleJump = true;
        float three = jumpPeak(true);
        check(three > one + 1.0f, "unless the body has the ability");
        p.canDoubleJump = false;
        p.position = glm::vec3(128, GROUND, 128); p.resetMotion();
        run(sim, 0.5f);
    }

    std::printf("an enemy, hit through the simulation\n");
    {
        Npc& d = *sim.npcs().spawn("dummy", glm::vec3(128, GROUND, 120), glm::vec3(0, 0, 1));
        EntityId dummy = d.uuid;
        run(sim, 0.3f);
        check(sim.find(dummy) == &d, "found by id");
        sim.setIntent(p.uuid, once(true));
        bool hit = false;
        for (int i = 0; i < 60 && !hit; i++) {
            sim.step(DT);
            for (const SimEvent& e : sim.takeEvents())
                if (e.kind == SimEvent::Kind::Hit && e.a == p.uuid && e.b == dummy) hit = true;
        }
        check(hit, "the player's light attack hits it (event by id)");
        check(d.health < d.maxHealth, "and hurts it");
        // a tool swung by hand (the axe) hits through the same rules
        float before = d.poise;
        sim.toolHit(p.uuid, dummy, 6.0f, 20.0f, d.bodyPoint(0.6f), glm::vec3(0, 0, -1));
        bool toolEvent = false;
        for (const SimEvent& e : sim.takeEvents()) if (e.kind == SimEvent::Kind::Hit && e.b == dummy) toolEvent = true;
        check(toolEvent && d.poise < before, "a tool hit wears poise and is reported");
        sim.npcs().clear();
        check(sim.find(dummy) == nullptr, "gone once removed: the id finds nothing");
    }

    std::printf("death and respawn\n");
    {
        p.setMode(GameMode::Survival);
        p.position = glm::vec3(140, GROUND, 140); p.resetMotion();
        Npc& s = *sim.npcs().spawn("swordsman", glm::vec3(140, GROUND, 130), glm::vec3(0, 0, 1));
        p.health = 1.0f;
        bool died = false, back = false;
        for (int i = 0; i < 60 * 30 && !(died && back); i++) {
            sim.step(DT);
            for (const SimEvent& e : sim.takeEvents()) {
                if (e.kind == SimEvent::Kind::Killed && e.b == p.uuid) died = true;
                if (e.kind == SimEvent::Kind::Respawned && e.b == p.uuid) back = true;
            }
        }
        (void)s;
        check(died, "a swordsman kills a weak player");
        check(back && !p.dead() && glm::length(glm::vec2(p.position.x - 128, p.position.z - 128)) < 4.0f,
              "who comes back at the spawn point");
        sim.npcs().clear();
    }

    std::printf("Test Play\n");
    {
        p.setMode(GameMode::Dev);
        p.position = glm::vec3(128, GROUND, 128); p.resetMotion();
        sim.npcs().spawn("swordsman", glm::vec3(128, GROUND, 118), glm::vec3(0, 0, 1));
        run(sim, 6.0f);
        check(p.health == p.maxHealth, "can't be hurt");
        sim.npcs().clear();
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
