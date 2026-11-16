#pragma once
#include "Combat.h"
#include "../entity/Human.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// ══════════════════════════════════════════════════════════
// CombatMoves — starting and running attacks (game/Combat.h)
// ══════════════════════════════════════════════════════════
// Called by physics (VoxelPhysics::moveHuman) each tick for
// every human, so the player and enemies follow the same rules.
// Headless: the combat test drives these directly.
namespace combat {

inline glm::vec3 flatFacing(const Human& h) {
    glm::vec2 f(h.facing.x, h.facing.z);
    float l = glm::length(f);
    f = l > 1e-6f ? f / l : glm::vec2(0, -1);
    return glm::vec3(f.x, 0.0f, f.y);
}

inline void beginAttack(Human& h, AttackKind k) {
    h.attack = AttackState();
    h.attack.kind = k;
    h.attack.dir = flatFacing(h);
    h.move = Human::Move::None;                      // an air dodge burst ends
    h.sprinting = false;
    if (!h.grounded) h.hasAirAttack = false;
    if (k == AttackKind::AirLight) h.velocity.y = std::max(h.velocity.y, LURCH_UP);
    if (k == AttackKind::AirHeavy) h.velocity = glm::vec3(0.0f);
}

// Start one from the intent, or queue the next light swing of
// a combo (a press once the swing is active). Ground: light / heavy. Air: lurch / plunge, once per
// jump.
inline void startAttack(Human& h, const HumanIntent& in) {
    if (!(in.attackLight || in.attackHeavy) || h.weaponDamage <= 0.0f || h.dead()) return;
    if (h.move == Human::Move::Roll || h.move == Human::Move::Backstep) return;
    if (h.flying) return;
    AttackState& a = h.attack;
    if (a.active()) {
        const AttackDef& d = a.def();
        if (in.attackLight && d.comboFrom >= 0.0f && a.t >= d.activeStart) a.queuedLight = true;
        return;
    }
    AttackKind k = h.grounded ? (in.attackHeavy ? AttackKind::Heavy : AttackKind::Light1)
                              : (in.attackHeavy ? AttackKind::AirHeavy : AttackKind::AirLight);
    if (!h.grounded && !h.hasAirAttack) return;
    if (!h.spend(def(k).stamina)) return;
    beginAttack(h, k);
}

// One tick of the attack: its clock, the combo, the plunge.
// Physics calls this after moving the body (so it knows if the
// plunge has landed).
inline void advanceAttack(Human& h, float dt) {
    AttackState& a = h.attack;
    if (!a.active()) return;
    if (a.stop > 0.0f) { a.stop -= dt; a.prevT = a.t; return; }   // hit-stop
    a.prevT = a.t;
    const AttackDef& d = a.def();
    if (a.t < d.activeStart) a.dir = flatFacing(h);  // it tracks while winding up
    if (a.kind == AttackKind::AirHeavy && !a.landed) {
        if (a.t < PLUNGE_HANG) a.t = std::min(a.t + dt, PLUNGE_HANG);
        else if (h.grounded) { a.plunging = false; a.landed = true; a.t = a.prevT = SLAM_AT; }
        else a.plunging = true;
        return;
    }
    a.t += dt;
    // the combo: a queued press cuts the recovery short
    if (a.queuedLight && h.grounded && d.comboFrom >= 0.0f && a.t >= d.comboFrom) {
        AttackKind next = a.kind == AttackKind::Light1 ? AttackKind::Light2 : AttackKind::Light1;
        if (h.spend(def(next).stamina)) { beginAttack(h, next); return; }
        a.queuedLight = false;
    }
    if (a.t >= d.duration) a = AttackState();
}

// Stamina comes back after a pause; slower while guarding, not
// at all during an attack or a dodge
inline void regainStamina(Human& h, float dt) {
    if (h.infiniteStamina) { h.stamina = h.maxStamina; return; }
    if (h.attack.active() || h.move != Human::Move::None) return;
    h.staminaWait -= dt;
    if (h.staminaWait > 0.0f) return;
    h.stamina = std::min(h.maxStamina, h.stamina + STAMINA_REGEN * dt * (h.guarding ? 0.4f : 1.0f));
}

} // namespace combat
