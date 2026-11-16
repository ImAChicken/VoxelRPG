// Headless checks for the combat moveset (game/Combat.h,
// CombatMoves.h, CombatHits.h) and the enemy brain (Npcs.h).
// No world and no physics: bodies stand on flat ground and the
// attack clock is driven the way VoxelPhysics::moveHuman does.
//
//   cmake -DVOXELRPG_TESTS=ON ... && build/bin/combat_test
#include "game/CombatMoves.h"
#include "game/CombatHits.h"
#include "game/Npcs.h"
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

static const float DT = 1.0f / 60.0f;

// A body standing at (x, 0, z) facing f, holding a 12-damage sword
static void stand(Human& h, glm::vec3 at, glm::vec3 f) {
    h.position = at;
    h.facing = glm::normalize(f);
    h.grounded = true;
    h.weaponDamage = 12.0f;
    h.health = h.maxHealth = 100.0f;
    h.stamina = h.maxStamina;
}
static HumanIntent press(bool light, bool heavy = false) {
    HumanIntent in; in.attackLight = light; in.attackHeavy = heavy; return in;
}
// One tick for everyone: start (from intents), advance, resolve
static std::vector<combat::HitEvent> tick(std::vector<Human*> bodies, std::vector<HumanIntent> ins = {}) {
    for (size_t i = 0; i < bodies.size(); i++) {
        HumanIntent in = i < ins.size() ? ins[i] : HumanIntent();
        combat::startAttack(*bodies[i], in);
        bodies[i]->guarding = in.guard && !bodies[i]->attack.active();
        combat::advanceAttack(*bodies[i], DT);
        combat::regainStamina(*bodies[i], DT);
        if (bodies[i]->move != Human::Move::None) {
            bodies[i]->moveTimer -= DT;
            if (bodies[i]->moveTimer <= 0.0f) bodies[i]->move = Human::Move::None;
        }
    }
    std::vector<combat::HitEvent> ev;
    combat::resolveHits(bodies, nullptr, ev);
    return ev;
}
static std::vector<combat::HitEvent> runFor(float s, std::vector<Human*> bodies, std::vector<HumanIntent> first = {}) {
    std::vector<combat::HitEvent> all;
    for (int i = 0; i < (int)std::lround(s / DT); i++) {
        auto ev = tick(bodies, i == 0 ? first : std::vector<HumanIntent>{});
        all.insert(all.end(), ev.begin(), ev.end());
    }
    return all;
}
static float hitSpeed(const Human& h) {
    float best = 0.0f;
    for (const auto& x : h.hits) best = std::max(best, glm::length(x.impulse) / h.mass);
    return best;
}

int main() {
    const glm::vec3 F(0, 0, -1);   // everyone attacks toward −z

    std::printf("light attack\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        auto ev = runFor(0.05f, {&a, &b}, {press(true)});
        check(a.attack.kind == AttackKind::Light1, "starts a light attack");
        check(std::fabs(a.stamina - (100.0f - combat::def(AttackKind::Light1).stamina)) < 0.01f, "costs stamina");
        ev = runFor(1.0f, {&a, &b});
        check(ev.size() == 1, "hits the body in front exactly once (" + std::to_string(ev.size()) + ")");
        check(std::fabs(b.health - 88.0f) < 0.01f, "for the weapon's damage (" + std::to_string(b.health) + ")");
        float sp = hitSpeed(b);
        check(sp > 5.0f && sp < Ragdoll::KNOCKDOWN_DV, "staggers, doesn't knock down (" + std::to_string(sp) + ")");
        check(!a.attack.active(), "and ends");
    }

    std::printf("reach and arc\n");
    {
        Human a, behind, far, high;
        stand(a, {0, 0, 0}, F);
        stand(behind, {0, 0, 7}, F);
        stand(far, {0, 0, -20}, F);
        stand(high, {3, 30, -6}, F);
        auto ev = runFor(1.0f, {&a, &behind, &far, &high}, {press(true)});
        check(ev.empty(), "misses behind, out of reach and far above");
    }

    std::printf("combo\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        tick({&a, &b}, {press(true)});
        while (a.attack.t < combat::def(AttackKind::Light1).activeEnd) tick({&a, &b});
        tick({&a, &b}, {press(true)});
        check(a.attack.queuedLight, "a press once the swing is active is queued");
        while (a.attack.kind == AttackKind::Light1) tick({&a, &b});
        check(a.attack.kind == AttackKind::Light2, "and chains into the backhand");
        check(a.attack.t < 0.05f, "cutting the recovery short");
        std::vector<combat::HitEvent> ev;
        ev = runFor(1.0f, {&a, &b});
        check(std::fabs(b.health - 76.0f) < 0.01f, "both swings hit (" + std::to_string(b.health) + ")");
        Human c;
        stand(c, {0, 0, 0}, F);
        tick({&c}, {press(true)});
        tick({&c}, {press(true)});          // too early: dropped
        runFor(1.0f, {&c});
        check(!c.attack.active(), "a press during the wind-up doesn't chain");
    }

    std::printf("heavy\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        auto ev = runFor(1.3f, {&a, &b}, {press(false, true)});
        check(a.attack.kind == AttackKind::None && ev.size() == 1, "one heavy, one hit");
        check(std::fabs(b.health - (100.0f - 12.0f * combat::def(AttackKind::Heavy).damage)) < 0.01f, "hits harder");
        check(hitSpeed(b) >= Ragdoll::KNOCKDOWN_DV, "knocks down (" + std::to_string(hitSpeed(b)) + ")");
    }

    std::printf("poise\n");
    {
        // b's heavy, wound up past its poise, takes a light hit and goes on
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        tick({&a, &b}, {HumanIntent(), press(false, true)});
        float lightHit = combat::def(AttackKind::Light1).activeStart;
        float wait = combat::def(AttackKind::Heavy).poiseFrom - lightHit + 0.02f;
        runFor(wait, {&a, &b});
        auto ev = runFor(lightHit + 0.1f, {&a, &b}, {press(true)});
        check(!ev.empty() && ev[0].target == &b, "a light hit lands on the heavy");
        check(b.attack.kind == AttackKind::Heavy, "the heavy isn't interrupted");
        // but a light attack is
        Human c, d;
        stand(c, {0, 0, 0}, F); stand(d, {0, 0, -8}, -F);
        tick({&c, &d}, {HumanIntent(), press(true)});
        runFor(0.02f, {&c, &d});
        tick({&c, &d}, {press(false, true)});
        // d's light lands first and interrupts c's heavy wind-up
        runFor(0.4f, {&c, &d});
        check(c.attack.kind == AttackKind::None || c.health < 100.0f, "a wind-up without poise is interrupted");
    }

    std::printf("guard\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        HumanIntent g; g.guard = true;
        std::vector<combat::HitEvent> ev;
        for (int i = 0; i < 60; i++) {
            auto e = tick({&a, &b}, {i == 0 ? press(true) : HumanIntent(), g});
            ev.insert(ev.end(), e.begin(), e.end());
        }
        check(ev.size() == 1 && ev[0].blocked, "blocked");
        check(std::fabs(b.health - (100.0f - 12.0f * combat::GUARD_KEEP)) < 0.01f, "a little gets through");
        check(b.stamina < 100.0f, "costs stamina");
        // from behind the guard does nothing
        Human c, d;
        stand(c, {0, 0, 0}, F); stand(d, {0, 0, -8}, F);  // d faces away
        ev.clear();
        for (int i = 0; i < 60; i++) {
            auto e = tick({&c, &d}, {i == 0 ? press(true) : HumanIntent(), g});
            ev.insert(ev.end(), e.begin(), e.end());
        }
        check(ev.size() == 1 && !ev[0].blocked, "not from behind");
        // out of stamina it breaks
        Human e, f;
        stand(e, {0, 0, 0}, F); stand(f, {0, 0, -8}, -F);
        f.stamina = 5.0f;
        ev.clear();
        for (int i = 0; i < 80; i++) {
            auto x = tick({&e, &f}, {i == 0 ? press(false, true) : HumanIntent(), g});
            ev.insert(ev.end(), x.begin(), x.end());
        }
        check(ev.size() == 1 && ev[0].guardBroken, "the guard breaks without stamina");
    }

    std::printf("dodging\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        tick({&a, &b}, {press(true)});
        // b rolls just as the swing becomes active
        runFor(combat::def(AttackKind::Light1).activeStart - 0.1f, {&a, &b});
        b.move = Human::Move::Roll; b.moveTimer = Human::ROLL_TIME;
        auto ev = runFor(combat::def(AttackKind::Light1).activeEnd - combat::def(AttackKind::Light1).activeStart + 0.15f, {&a, &b});
        check(ev.empty() && b.health == 100.0f, "a roll's i-frames dodge it");
    }

    std::printf("stamina\n");
    {
        Human a;
        stand(a, {0, 0, 0}, F);
        a.stamina = 0.0f;
        tick({&a}, {press(true)});
        check(!a.attack.active(), "no stamina, no attack");
        runFor(combat::STAMINA_DELAY + 1.0f, {&a});
        check(a.stamina > 20.0f, "comes back after a pause");
        a.infiniteStamina = true;
        for (int i = 0; i < 20; i++) { tick({&a}, {press(true)}); runFor(0.7f, {&a}); }
        check(a.stamina == a.maxStamina, "infinite in Test Play");
    }

    std::printf("air attacks\n");
    {
        Human a, b;
        stand(a, {0, 6, 0}, F); a.grounded = false;
        stand(b, {0, 0, -8}, -F);
        tick({&a, &b}, {press(true)});
        check(a.attack.kind == AttackKind::AirLight, "light in the air: the lurch");
        check(a.velocity.y >= combat::LURCH_UP - 0.01f, "with a little lift");
        runFor(0.8f, {&a, &b});
        tick({&a, &b}, {press(true)});
        check(!a.attack.active(), "one air attack per jump");

        Human p, n1, n2;
        stand(p, {0, 8, 0}, F); p.grounded = false; p.hasAirAttack = true;
        stand(n1, {0, 0, -6}, -F); stand(n2, {0, 0, 6}, F);   // one in front, one behind
        tick({&p, &n1, &n2}, {HumanIntent(), HumanIntent(), HumanIntent()});
        tick({&p, &n1, &n2}, {press(false, true)});
        check(p.attack.kind == AttackKind::AirHeavy, "heavy in the air: the plunge");
        auto ev = runFor(0.5f, {&p, &n1, &n2});
        check(ev.empty() && p.attack.plunging, "hangs, then falls, hitting nothing on the way");
        p.position.y = 0.0f; p.grounded = true;               // lands
        ev = runFor(0.3f, {&p, &n1, &n2});
        check(p.attack.landed, "slams on landing");
        check(ev.size() == 2, "hits all around (" + std::to_string(ev.size()) + ")");
        check(hitSpeed(n2) >= Ragdoll::KNOCKDOWN_DV, "and knocks them down");
    }

    std::printf("death\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        b.health = 5.0f;
        auto ev = runFor(1.0f, {&a, &b}, {press(true)});
        check(ev.size() == 1 && ev[0].killed && b.dead(), "dies at zero");
        check(b.ragdoll.stayDown, "and stays down");
        ev = runFor(1.0f, {&a, &b}, {press(true)});
        check(ev.empty(), "the dead aren't hit again");
    }

    std::printf("line of sight\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        std::vector<combat::HitEvent> ev;
        tick({&a, &b}, {press(true)});
        for (int i = 0; i < 60; i++) {
            combat::advanceAttack(a, DT);
            combat::resolveHits({&a, &b}, [](glm::vec3, glm::vec3) { return false; }, ev);
        }
        check(ev.empty(), "a wall in between stops it");
    }

    std::printf("enemies use the same moves\n");
    {
        Npc n;
        stand(n, {0, 0, 0}, F);
        n.kind = NpcKind::Swordsman;
        Human target;
        stand(target, {0, 0, -60}, -F);
        npcbrain::think(n, &target, DT);
        check(glm::dot(n.intent.move, glm::vec2(0, -1)) > 0.9f, "walks toward a target out of reach");
        target.position = {0, 0, -10};
        bool attacked = false;
        for (int i = 0; i < 60 * 6 && !attacked; i++) {
            npcbrain::think(n, &target, DT);
            combat::startAttack(n, n.intent);
            combat::advanceAttack(n, DT);
            combat::regainStamina(n, DT);
            n.intent.clearOneShots();
            attacked = n.attack.active();
        }
        check(attacked, "attacks a target in reach within a few seconds");
        std::vector<combat::HitEvent> ev;
        for (int i = 0; i < 90; i++) {
            combat::advanceAttack(n, DT);
            combat::resolveHits({&n, &target}, nullptr, ev);
        }
        check(!ev.empty() && target.health < 100.0f, "and its hits land like the player's");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
