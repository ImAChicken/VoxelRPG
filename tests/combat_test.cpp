// Headless checks for the combat moveset (game/Combat.h,
// CombatMoves.h, CombatHits.h) and the enemy brain (Npcs.h).
// No world and no physics: bodies stand on flat ground and the
// attack clock is driven the way VoxelPhysics::moveHuman does.
//
//   cmake -DVOXELRPG_TESTS=ON ... && build/bin/combat_test
#include "game/CombatMoves.h"
#include "game/CombatHits.h"
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

static const float DT = 1.0f / 60.0f;

static const Moveset* SWORD = nullptr;
static const AttackDef& atk(AttackKind k, int chain = 0) { return *SWORD->get(k, chain); }

// A body standing at (x, 0, z) facing f, holding a 12-damage
// sword at the moveset's own weight
static void stand(Human& h, glm::vec3 at, glm::vec3 f) {
    h.position = at;
    h.facing = glm::normalize(f);
    h.grounded = true;
    h.weaponDamage = 12.0f;
    h.moveset = SWORD;
    h.weaponWeight = SWORD->refWeight;
    h.health = h.maxHealth = 100.0f;
    h.stamina = h.maxStamina;
    h.poise = h.maxPoise = combat::POISE_BASE;
}
static HumanIntent press(bool light, bool heavy = false) {
    HumanIntent in; in.attackLight = light; in.attackHeavy = heavy; return in;
}
// One tick for everyone: start (from intents), advance, resolve
static std::vector<combat::HitEvent> tick(std::vector<Human*> bodies, std::vector<HumanIntent> ins = {}) {
    for (size_t i = 0; i < bodies.size(); i++) {
        HumanIntent in = i < ins.size() ? ins[i] : HumanIntent();
        combat::startAttack(*bodies[i], in);
        bodies[i]->guarding = in.guard && !bodies[i]->attack.active() && bodies[i]->exposed <= 0.0f;
        bodies[i]->guardTime = bodies[i]->guarding ? bodies[i]->guardTime + DT : 0.0f;   // as physics does
        combat::advanceAttack(*bodies[i], DT);
        combat::regainStamina(*bodies[i], DT);
        combat::regainPoise(*bodies[i], DT);
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
    if (!gMovesets.load("data/registry/movesets.toml") || !(SWORD = gMovesets.find("sword"))) {
        std::printf("run from the repo root (needs data/registry/movesets.toml)\n");
        return 1;
    }
    const glm::vec3 F(0, 0, -1);   // everyone attacks toward −z

    std::printf("light attack\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        auto ev = runFor(0.05f, {&a, &b}, {press(true)});
        check(a.attack.kind == AttackKind::Light && a.attack.chain == 0, "starts a light attack");
        check(std::fabs(a.stamina - (100.0f - atk(AttackKind::Light).stamina)) < 0.01f, "costs stamina");
        ev = runFor(1.0f, {&a, &b});
        check(ev.size() == 1, "hits the body in front exactly once (" + std::to_string(ev.size()) + ")");
        check(std::fabs(b.health - 88.0f) < 0.01f, "for the weapon's damage (" + std::to_string(b.health) + ")");
        float sp = hitSpeed(b);
        check(ev.size() == 1 && ev[0].react == React::Flinch && b.react == React::Flinch && b.hitstun > 0.2f,
              "a regular body reacts: a flinch, stuck in it (" + std::to_string(b.hitstun) + " s)");
        check(sp > 1.0f && sp < Ragdoll::KNOCKDOWN_DV, "its limbs give way, it doesn't fall (" + std::to_string(sp) + ")");
        check(std::fabs(b.poise - (combat::POISE_BASE - atk(AttackKind::Light).poise)) < 0.01f, "and wears poise down");
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
        while (a.attack.t < atk(AttackKind::Light).activeEnd) tick({&a, &b});
        tick({&a, &b}, {press(true)});
        check(a.attack.queuedLight, "a press once the swing is active is queued");
        while (a.attack.active() && a.attack.chain == 0) tick({&a, &b});
        check(a.attack.kind == AttackKind::Light && a.attack.chain == 1, "and chains into the backhand");
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
        check(std::fabs(b.health - (100.0f - 12.0f * atk(AttackKind::Heavy).damage)) < 0.01f, "hits harder");
        check(b.react == React::Launch && b.pendingLift > 10.0f && hitSpeed(b) < Ragdoll::KNOCKDOWN_DV,
              "the sword's heavy launches a plain body (" + std::to_string(b.pendingLift) + " vox/s up)");
        Human c, t;
        stand(c, {0, 0, 0}, F); stand(t, {0, 0, -8}, -F);
        t.poise = t.maxPoise = 60.0f;                      // a tough one, armored against stumbles
        t.armor = 3;
        runFor(1.3f, {&c, &t}, {press(false, true)});
        check(hitSpeed(t) <= combat::FLINCH_KNOCK + 0.01f && t.poise > 0.0f && t.react == React::None,
              "an armored body with poise left shrugs it off: no reaction");
    }

    std::printf("poise\n");
    {
        // lights wear it down; the hit that breaks it staggers
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        a.infiniteStamina = true;
        int hits = 0; bool broke = false;
        for (int n = 0; n < 6 && !broke; n++) {
            auto ev = runFor(0.7f, {&a, &b}, {press(true)});
            for (auto& e : ev) { hits++; if (e.poiseBroken) broke = true; }
            if (!broke) check(hitSpeed(b) < Ragdoll::KNOCKDOWN_DV && b.react == React::Flinch, "hit " + std::to_string(hits) + ": a flinch");
            b.hitstun = 0.0f; b.react = React::None;
            b.hits.clear();
        }
        check(broke && hits == 3, "the third light hit breaks 30 poise (" + std::to_string(hits) + ")");
        check(b.poise == b.maxPoise, "which refills once broken");
        // after a while without hits it refills on its own
        b.poise = 5.0f; b.sincePoiseHit = 0.0f;
        runFor(combat::POISE_DELAY + 1.0f, {&b});
        check(b.poise == b.maxPoise, "and refills after a few seconds without hits");

        // hyper armour: a light hit on a heavy past armor_from doesn't break it
        Human c, d;
        stand(c, {0, 0, 0}, F); stand(d, {0, 0, -8}, -F);
        d.poise = 5.0f; d.sincePoiseHit = 0.0f;
        tick({&c, &d}, {HumanIntent(), press(false, true)});
        float lightHit = atk(AttackKind::Light).activeStart;
        float wait = atk(AttackKind::Heavy).armorFrom - lightHit + 0.02f;
        runFor(wait, {&c, &d});
        auto ev = runFor(lightHit + 0.1f, {&c, &d}, {press(true)});
        check(!ev.empty() && ev[0].target == d.uuid, "a light hit lands on the heavy");
        check(d.attack.kind == AttackKind::Heavy, "hyper armour: the heavy goes on with almost no poise");
        // without armour, the same hit breaks low poise and interrupts
        Human e2, f2;
        stand(e2, {0, 0, 0}, F); stand(f2, {0, 0, -8}, -F);
        f2.poise = 5.0f; f2.sincePoiseHit = 0.0f;
        tick({&e2, &f2}, {press(true), press(true)});
        ev = runFor(0.25f, {&e2, &f2});
        bool interrupted = false;
        for (auto& x : ev) if (x.target == f2.uuid && x.poiseBroken) interrupted = true;
        check(interrupted && !f2.attack.active(), "a swing without armour is interrupted when poise breaks");
    }

    std::printf("weapon weight\n");
    {
        Human light, heavy;
        stand(light, {0, 0, 0}, F); stand(heavy, {20, 0, 0}, F);
        heavy.weaponWeight = SWORD->refWeight + 4.0f;
        tick({&light, &heavy}, {press(true), press(true)});
        check(heavy.stamina < light.stamina, "a heavier sword costs more stamina");
        runFor(0.3f, {&light, &heavy});
        check(heavy.attack.t < light.attack.t, "and swings slower");
        check(heavy.attack.poiseScale > 1.0f, "and is harder on poise");
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

    std::printf("parry\n");
    {
        // the guard goes up just before the blade arrives
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        HumanIntent g; g.guard = true;
        std::vector<combat::HitEvent> ev;
        float hitAt = atk(AttackKind::Light).activeStart;
        int raise = (int)std::lround((hitAt - 0.08f) / DT);
        for (int i = 0; i < 40; i++) {
            auto e = tick({&a, &b}, {i == 0 ? press(true) : HumanIntent(), i >= raise ? g : HumanIntent()});
            ev.insert(ev.end(), e.begin(), e.end());
        }
        check(ev.size() == 1 && ev[0].parried && ev[0].damage == 0.0f, "a guard raised just in time parries");
        check(b.health == 100.0f && b.stamina >= 99.0f, "nothing gets through, no stamina spent");
        check(!a.attack.active() && a.exposed > 0.0f, "the attacker's swing is knocked aside: exposed");
        tick({&a, &b}, {press(true)});
        check(!a.attack.active(), "exposed: can't attack");
        tick({&a, &b}, {g});
        check(!a.guarding, "nor guard");
        // a riposte while it reels
        ev.clear();
        for (int i = 0; i < 40 && ev.empty(); i++) {
            auto e = tick({&b, &a}, {i == 0 ? press(true) : HumanIntent()});
            ev.insert(ev.end(), e.begin(), e.end());
        }
        check(ev.size() == 1 && ev[0].riposte && ev[0].knockedDown, "a riposte on the exposed body: critical, knocked down");
        check(std::fabs(a.health - (100.0f - 12.0f * combat::RIPOSTE_DAMAGE)) < 0.01f, "for RIPOSTE_DAMAGE times the damage");
        check(a.exposed == 0.0f, "and it's no longer exposed");
        // too early (guard held long before) is an ordinary block
        Human c, d;
        stand(c, {0, 0, 0}, F); stand(d, {0, 0, -8}, -F);
        ev.clear();
        for (int i = 0; i < 20; i++) tick({&c, &d}, {HumanIntent(), g});
        for (int i = 0; i < 40; i++) {
            auto e = tick({&c, &d}, {i == 0 ? press(true) : HumanIntent(), g});
            ev.insert(ev.end(), e.begin(), e.end());
        }
        check(ev.size() == 1 && ev[0].blocked && !ev[0].parried, "a guard held up a while blocks (no parry)");
        // exposure wears off
        Human x, y;
        stand(x, {0, 0, 0}, F); stand(y, {0, 0, -40}, -F);
        x.exposed = combat::EXPOSED_TIME;
        runFor(combat::EXPOSED_TIME + 0.1f, {&x, &y});
        tick({&x, &y}, {press(true)});
        check(x.exposed == 0.0f && x.attack.active(), "exposure wears off: it can attack again");
    }

    std::printf("dodging\n");
    {
        Human a, b;
        stand(a, {0, 0, 0}, F); stand(b, {0, 0, -8}, -F);
        tick({&a, &b}, {press(true)});
        // b rolls just as the swing becomes active
        runFor(atk(AttackKind::Light).activeStart - 0.1f, {&a, &b});
        b.move = Human::Move::Roll; b.moveTimer = Human::ROLL_TIME;
        auto ev = runFor(atk(AttackKind::Light).activeEnd - atk(AttackKind::Light).activeStart + 0.15f, {&a, &b});
        bool landed = false; for (auto& e : ev) landed = landed || !e.perfectDodge;
        check(!landed && b.health == 100.0f, "a roll's i-frames dodge it");
        check(ev.size() == 1 && ev[0].perfectDodge, "rolled just as it came: a perfect dodge");
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

    std::printf("reactions and multi-hit swings\n");
    {
        // the staff's third light spins through twice: two hits on one body
        const Moveset* staff = gMovesets.find("staff");
        const AttackDef* l3 = staff ? staff->get(AttackKind::Light, 2) : nullptr;
        check(l3 && l3->windowCount() == 2, "staff_light3 has two hit windows");
        if (l3) {
            Human a, b;
            stand(a, {0, 0, 0}, F); stand(b, {0, 0, -7}, -F);
            a.moveset = staff;
            a.attack = AttackState(); a.attack.kind = AttackKind::Light; a.attack.chain = 2; a.attack.d = l3;
            a.attack.dir = F;
            std::vector<combat::HitEvent> ev;
            for (int i = 0; i < 60 && a.attack.active(); i++) {
                combat::advanceAttack(a, DT);
                combat::resolveHits({&a, &b}, nullptr, ev);
            }
            check(ev.size() == 2 && ev[0].window == 0 && ev[1].window == 1,
                  "it hits the same body once in each (" + std::to_string(ev.size()) + ")");
            check(ev.size() == 2 && ev[1].combo == 2, "and counts them as a combo");
        }
        // a guard's armor: lights only wear its poise
        Human a, g;
        stand(a, {0, 0, 0}, F); stand(g, {0, 0, -8}, -F);
        g.armor = 1; g.poise = g.maxPoise = 50.0f;
        auto ev = runFor(1.0f, {&a, &g}, {press(true)});
        check(ev.size() == 1 && ev[0].react == React::None && !g.stunned(), "armor 1: a light doesn't move it");
        // a hit gets everything a listener needs
        check(ev.size() == 1 && ev[0].damage > 0.0f && ev[0].combo == 1 && glm::length(ev[0].dir) > 0.5f,
              "the hit says its damage, combo and direction");
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

    // enemies using these moves: tests/npc_test.cpp

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
