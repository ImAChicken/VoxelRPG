// Headless checks for the mushroom monsters (npcs.toml mushnub,
// mushnub_evolved, mushroom_king): bodies drawn as monster models
// (character/MonsterModel.h). The Mushnubs get about like slimes; the
// King walks and fights with his own swings (Simulation::swingTick),
// never goes limp from a hit, and dies on his own clip. Run from the
// repo root.
#include "TestWorld.h"
#include "character/MonsterAnim.h"
#include <cstdio>

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gMonsters.load("data/models/monsters");

    std::printf("the types\n");
    const NpcType* mn = gNpcTypes.find("mushnub");
    const NpcType* ev = gNpcTypes.find("mushnub_evolved");
    const NpcType* kg = gNpcTypes.find("mushroom_king");
    check(mn && ev && kg, "mushnub, mushnub_evolved, mushroom_king are in the registry");
    if (!mn || !ev || !kg) return 1;
    for (const NpcType* t : { mn, ev, kg }) {
        const MonsterModel* m = gMonsters.find(t->model);
        check(m != nullptr, t->id + ": its model (" + t->model + ") is there");
        if (!m) continue;
        bool clips = m->clip(t->clips.idle) >= 0 && m->clip(t->clips.walk) >= 0 && m->clip(t->clips.death) >= 0;
        for (const NpcType::Swing& s : t->swings) clips = clips && m->clip(s.clip) >= 0;
        check(clips, "  the clips it names are in the model");
    }
    check(mn->hops && mn->contactDamage > 0.0f && !mn->person(), "a Mushnub hops and hurts by touch, like a slime");
    check(!kg->person() && !kg->hops && kg->swings.size() == 2, "the King walks and has two swings");

    std::printf("the King's fight\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.facing = glm::vec3(0, 0, 1);
        w.run(0.3f);
        Npc* k = w.sim.npcs().spawn("mushroom_king", glm::vec3(256, GROUND, 300), glm::vec3(0, 0, -1));
        check(k && k->noRagdoll && std::fabs(k->height - 26.0f) < 0.01f, "spawned big, with no ragdoll");
        if (!k) return 1;
        bool fought = w.until(5.0f, [&] { return k->stateName == "combat"; });
        check(fought, "he sees her and fights");
        float hp = p.health;
        int swings = 0; bool was = false;
        bool hurt = w.until(20.0f, [&] { if (k->swing >= 0 && !was) swings++; was = k->swing >= 0; return p.health < hp; });
        std::printf("    %d swing(s) before one landed; %.0f damage\n", swings, hp - p.health);
        check(hurt && swings > 0, "he closes in and his swing hurts her");

        // her guard raised toward him takes it
        p.health = p.maxHealth;
        const uint16_t SWORD = gItems.find("iron_sword");
        p.inventory.add(SWORD, 1);
        p.inventory.equipFrom(p.inventory.find(SWORD), EquipSlot::Weapon1);
        p.combatStance = true;
        p.refreshWeapon();
        w.run(3.0f);                                    // (up again, if he knocked her down)
        float before = p.health;
        int blocked = 0;
        HumanIntent g; g.guard = true;
        for (int i = 0; i < 60 * 15; i++) {
            glm::vec3 d = k->position - p.position;
            if (glm::length(glm::vec2(d.x, d.z)) > 1e-3f) p.facing = glm::normalize(glm::vec3(d.x, 0, d.z));
            w.sim.setIntent(p.uuid, g);
            p.stamina = p.maxStamina;
            w.sim.step(DT);
            for (const SimEvent& e : w.sim.takeEvents()) if (e.kind == SimEvent::Kind::Hit && e.a == k->uuid && e.blocked) blocked++;
        }
        before = std::min(before, p.maxHealth);
        check(blocked > 0 && p.health >= before - 0.01f, "a guard toward him blocks his swings (" + std::to_string(blocked) + " blocked)");

        // hit hard, he's only pushed: never limp
        bool limp = false;
        for (int i = 0; i < 10; i++) {
            k->hit(k->bodyPoint(0.5f), glm::vec3(0, 0, 1) * 60.0f * k->mass);
            w.sim.step(DT);
            limp = limp || k->ragdoll.isLimp();
        }
        check(!limp && !k->dead(), "hits push him; he never goes limp");
        k->health = 0.0f;
        w.run(0.5f);
        check(k->dead() && !k->ragdoll.isLimp(), "he dies without a ragdoll (his Death clip shows it)");
    }

    std::printf("which clip plays\n");
    {
        const MonsterModel* m = gMonsters.find("mushroomking");
        MonsterAnimator an;
        MonsterAnimInput in;
        for (int i = 0; i < 10; i++) an.update(*m, kg->clips, in, DT);
        check(an.now() == MonsterAnimator::Role::Idle, "standing: Idle");
        in.speed = 9.0f;
        for (int i = 0; i < 10; i++) an.update(*m, kg->clips, in, DT);
        check(an.now() == MonsterAnimator::Role::Walk, "walking: Walk");
        in.attacking = true; in.attackClip = &kg->swings[1].clip; in.attackRate = 0.5f;
        in.sinceHurt = 0.0f;
        an.update(*m, kg->clips, in, DT);
        check(an.now() == MonsterAnimator::Role::Attack, "swinging (even as a hit lands): the swing's clip");
        in.attacking = false;
        for (int i = 0; i < 3; i++) an.update(*m, kg->clips, in, DT);
        check(an.now() == MonsterAnimator::Role::Hit, "hit: HitReact");
        in.dead = true;
        an.update(*m, kg->clips, in, DT);
        check(an.now() == MonsterAnimator::Role::Dead, "dead: Death");
    }

    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
