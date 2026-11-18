// Headless checks for the slime (npcs.toml "slime"): an NPC type
// with a body that isn't a person. It gets about by hopping (still
// on the ground, moving only in the air: VoxelPhysics::hopTick),
// paths and takes its place in a fight like the others (npc/States.h
// Combat, npc/Encounters.h), lunges at its target, and hurts what it
// touches (Simulation::contactDamage). Run from the repo root.
#include "TestWorld.h"
#include <cstdio>
#include <set>

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }

    std::printf("the type\n");
    const NpcType* t = gNpcTypes.find("slime");
    check(t && !t->person() && t->hops && t->contactDamage > 0.0f, "slime: in the NPC registry, a hopping body that hurts by touch");
    if (!t) return 1;
    check(t->lungeRange > 6.0f && t->lungeRange < 30.0f, "  its lunge carries " + std::to_string((int)t->lungeRange) + " voxels: its fighting range");

    std::printf("hopping\n");
    {
        World w;
        Npc* s = w.sim.npcs().spawn("slime", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, 1));
        check(s && s->hop.on && s->noRagdoll && std::fabs(s->height - t->bodyHeight) < 0.01f &&
              s->faction == gFactions.find("slimes"), "spawned with its body: short, no ragdoll, hops, the slimes' faction");
        // walk it somewhere by hand: the brain's walkTo, as any state does
        w.run(0.5f);
        glm::vec3 goal = s->position + glm::vec3(0, 0, 60);
        float groundMove = 0.0f, airMove = 0.0f;
        int groundTicks = 0, airTicks = 0;
        for (int i = 0; i < 60 * 5; i++) {
            glm::vec3 before = s->position;
            bool wasGround = s->grounded;
            s->state.reset();                   // no brain: just the walking
            s->walkTo(w.sim, goal, 1.0f, true);
            w.physics.moveHuman(*s, s->intent, DT);
            s->intent.clearOneShots();
            float d = glm::length(glm::vec2(s->position.x - before.x, s->position.z - before.z));
            if (wasGround && s->grounded) { groundMove += d; groundTicks++; }
            else { airMove += d; airTicks++; }
        }
        float went = s->position.z - (goal.z - 60);
        std::printf("    %d hops, %.0f voxels in 5 s; %.1f of them on the ground\n", s->hop.hops, went, groundMove);
        check(s->hop.hops >= 4 && went > 25.0f, "it gets there by hopping");
        check(groundMove < went * 0.1f && groundTicks > 30 && airTicks > 30, "  moving only in the air: still between hops");
        check(!s->hop.lunging, "  a walk's hops aren't lunges, even running");
    }

    std::printf("a fight\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, -1);
        w.run(0.3f);
        Npc* s = w.sim.npcs().spawn("slime", glm::vec3(256, GROUND, 296), glm::vec3(0, 0, -1));
        float hp = p.health;
        bool fought = w.until(4.0f, [&] { return s->stateName == "combat"; });
        check(fought, "it sees her (40 voxels) and fights");
        int lunges = 0;
        bool wasLunging = false;
        auto countLunge = [&] { if (s->hop.lunging && !wasLunging) lunges++; wasLunging = s->hop.lunging; };
        bool hurt = w.until(12.0f, [&] { countLunge(); return p.health < hp; });
        check(hurt && w.sim.contactHits > 0, "it reaches her and hurts her by touching (" +
              std::to_string((int)(hp - p.health)) + " damage)");
        for (int i = 0; i < 60 * 12; i++) { w.sim.step(DT); countLunge(); }
        check(lunges > 0, "  it lunges at her (" + std::to_string(lunges) + " in the fight's first 12 s or so)");
        // her sword meets it, low as it is
        const uint16_t SWORD = gItems.find("iron_sword");
        p.inventory.add(SWORD, 1);
        p.inventory.equipFrom(p.inventory.find(SWORD), EquipSlot::Weapon1);
        p.combatStance = true;
        p.refreshWeapon();
        float shp = s->health;
        bool hit = false;
        for (int k = 0; k < 12 && !hit; k++) {
            glm::vec3 d = s->position - p.position;
            if (glm::length(glm::vec2(d.x, d.z)) > 1e-3f) p.facing = glm::normalize(glm::vec3(d.x, 0, d.z));
            if (glm::length(glm::vec2(d.x, d.z)) < 12.0f) {
                HumanIntent in; in.attackLight = true;
                w.sim.setIntent(p.uuid, in);
            }
            hit = w.until(0.8f, [&] { return s->health < shp; });
        }
        check(hit, "her sword swung at a person's height still hits it (" + std::to_string((int)(shp - s->health)) + " damage)");
        // it dies like any NPC
        s->health = 1.0f;
        p.health = p.maxHealth;
        bool died = false;
        for (int k = 0; k < 20 && !died; k++) {
            glm::vec3 d = s->position - p.position;
            if (glm::length(glm::vec2(d.x, d.z)) > 1e-3f) p.facing = glm::normalize(glm::vec3(d.x, 0, d.z));
            HumanIntent in; in.attackLight = true;
            w.sim.setIntent(p.uuid, in);
            died = w.until(0.8f, [&] { return s->dead(); });
        }
        check(died, "  and kills it");
    }

    std::printf("a guard blocks it\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        const uint16_t SWORD = gItems.find("iron_sword");
        p.inventory.add(SWORD, 1);
        p.inventory.equipFrom(p.inventory.find(SWORD), EquipSlot::Weapon1);
        p.combatStance = true;
        p.refreshWeapon();
        p.facing = glm::vec3(0, 0, 1);
        w.run(0.3f);
        Npc* s = w.sim.npcs().spawn("slime", glm::vec3(256, GROUND, 276), glm::vec3(0, 0, -1));
        HumanIntent g; g.guard = true;
        float hp = p.health;
        int blocked = 0;
        for (int i = 0; i < 60 * 10; i++) {
            p.facing = glm::vec3(0, 0, 1);
            glm::vec3 d = s->position - p.position;
            if (glm::length(glm::vec2(d.x, d.z)) > 1e-3f) p.facing = glm::normalize(glm::vec3(d.x, 0, d.z));
            w.sim.setIntent(p.uuid, g);
            w.sim.step(DT);
            for (const SimEvent& e : w.sim.takeEvents()) if (e.kind == SimEvent::Kind::Hit && e.a == s->uuid && e.blocked) blocked++;
        }
        check(blocked > 0 && p.health == hp, "her guard, raised toward it, blocks every touch (" + std::to_string(blocked) + ")");
    }

    std::printf("places in the fight\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.health = p.maxHealth = 100000.0f;               // she stands there and takes it
        w.run(0.3f);
        std::vector<Npc*> ss;
        for (int i = 0; i < 4; i++)
            ss.push_back(w.sim.npcs().spawn("slime", glm::vec3(236 + i * 12, GROUND, 290), glm::vec3(0, 0, -1)));
        w.run(8.0f);
        int inFight = 0;
        for (Npc* s : ss) inFight += s->stateName == "combat";
        std::set<int> slots;
        if (const encounter::Encounter* e = w.sim.encounters().of(ss[0]->uuid))
            for (const auto& c : e->claims) slots.insert((int)c.slot);
        check(inFight == 4 && slots.size() >= 3, "four slimes in one fight, at " + std::to_string(slots.size()) + " different places around her");
        // they don't hurt each other
        bool hurtKin = false;
        for (Npc* s : ss) hurtKin = hurtKin || s->health < s->maxHealth;
        check(!hurtKin, "  bumping into each other does no harm");
        // spread around her: the widest gap between them, seen from her
        std::vector<float> ang;
        for (Npc* s : ss) ang.push_back(std::atan2(s->position.z - p.position.z, s->position.x - p.position.x));
        std::sort(ang.begin(), ang.end());
        float widest = 0.0f;
        for (size_t i = 0; i < ang.size(); i++) {
            float a = ang[(i + 1) % ang.size()] - ang[i];
            if (i + 1 == ang.size()) a += 6.2832f;
            widest = std::max(widest, a);
        }
        std::printf("    widest gap around her: %.0f degrees\n", glm::degrees(widest));
        check(widest < glm::radians(250.0f), "  spread around her, not in a queue");
    }

    std::printf("pathing round a wall\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        w.wall(230, 282, 270, 271, 20);                    // a long wall between them, open at the ends
        w.run(0.3f);
        Npc* s = w.sim.npcs().spawn("slime", glm::vec3(256, GROUND, 284), glm::vec3(0, 0, -1));
        s->lastKnown = p.position;                         // it knows where she is (the wall hides her)
        s->target = p.uuid;
        brain::change(*s, w.sim, "combat");
        float hp = p.health;
        bool got = w.until(20.0f, [&] { return p.health < hp; });
        check(got, "it hops round the wall to her");
        // and up a ledge
        World w2;
        Player& q = w2.player(glm::vec3(256, GROUND + 6, 256));
        w2.wall(216, 296, 226, 286, 6);                    // she stands on a rise 6 high, 80 across
        w2.run(0.5f);
        Npc* s2 = w2.sim.npcs().spawn("slime", glm::vec3(256, GROUND, 300), glm::vec3(0, 0, -1));
        float qh = q.health;
        bool up = w2.until(20.0f, [&] { return q.health < qh; });
        check(up && s2->position.y > GROUND + 4.0f, "  and up a ledge 2 ft high");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
