#pragma once
#include "Combat.h"
#include "ItemRegistry.h"
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

inline bool beginAttack(Human& h, AttackKind k, int chain = 0, const AttackDef* use = nullptr) {
    if (!h.moveset) return false;
    const AttackDef* d = use ? use : h.moveset->get(k, chain);
    if (!d) return false;
    WeightScales w = weightScales(*h.moveset, h.weaponWeight);
    if (!h.spend(d->stamina * w.stamina)) return false;
    // sprinting into it: the run carries on into the swing (fading)
    glm::vec2 carry(0.0f);
    if (h.sprinting && h.grounded && (k == AttackKind::Light || k == AttackKind::Heavy))
        carry = glm::vec2(h.velocity.x, h.velocity.z) * CARRY_KEEP;
    h.attack = AttackState();
    h.attack.carry = carry;
    h.attack.kind = k;
    h.attack.chain = chain;
    h.attack.d = d;
    h.attack.speed = w.speed * h.attackSpeed;
    h.attack.poiseScale = w.poise;
    h.attack.dir = flatFacing(h);
    h.move = Human::Move::None;                      // an air dodge burst ends
    h.sprinting = false;
    if (!h.grounded) h.hasAirAttack = false;
    if (k == AttackKind::AirLight) h.velocity.y = std::max(h.velocity.y, LURCH_UP);
    if (k == AttackKind::AirHeavy) h.velocity = glm::vec3(0.0f);
    return true;
}

// Start one from the intent, or queue the next light swing of
// a combo (a press once the swing is active). Ground: light /
// heavy. Air: lurch / plunge, once per jump.
// An item's own attack (a spell's cast: ItemDef::cast). Needs
// no weapon; can't start mid-attack, rolling or in the air.
inline bool beginCast(Human& h, const AttackDef* d, uint16_t item = 0) {
    if (!d || h.dead() || h.attack.active() || h.exposed > 0.0f || h.flying) return false;
    if (h.move == Human::Move::Roll || h.move == Human::Move::Backstep) return false;
    if (!h.spend(d->stamina)) return false;
    h.attack = AttackState();
    h.attack.kind = AttackKind::Cast;
    h.attack.d = d;
    h.attack.item = item;
    h.attack.dir = flatFacing(h);
    h.move = Human::Move::None;
    h.sprinting = false;
    return true;
}

inline void startAttack(Human& h, const HumanIntent& in) {
    if (in.useItem) {
        const ItemDef& item = gItems.get(in.useItem);
        if (item.valid() && !item.cast.empty()) beginCast(h, gMovesets.attack(item.cast), in.useItem);
        return;
    }
    if (in.special && !h.attack.active() && h.moveset && h.weaponDamage > 0.0f && !h.dead() && h.grounded && h.exposed <= 0.0f &&
        h.move != Human::Move::Roll && h.move != Human::Move::Backstep && !h.flying) {
        beginAttack(h, AttackKind::Heavy, 0, in.special);              // a special: as a heavy, by name
        return;
    }
    if (!(in.attackLight || in.attackHeavy) || !h.moveset || h.weaponDamage <= 0.0f || h.dead()) return;
    if (h.exposed > 0.0f) return;                   // parried: reeling, can't attack yet
    if (h.move == Human::Move::Roll || h.move == Human::Move::Backstep) return;
    if (h.flying) return;
    AttackState& a = h.attack;
    if (a.active()) {
        const AttackDef& d = a.def();
        if (in.attackLight && d.comboFrom >= 0.0f && a.t >= d.activeStart) a.queuedLight = true;
        return;
    }
    AttackKind k = h.grounded ? (in.attackHeavy ? AttackKind::Heavy : AttackKind::Light)
                              : (in.attackHeavy ? AttackKind::AirHeavy : AttackKind::AirLight);
    if (!h.grounded && !h.hasAirAttack) return;
    beginAttack(h, k);
}

// One tick of the attack: its clock (scaled by the weapon's
// weight), the combo, the plunge. Physics calls this after
// moving the body (so it knows if the plunge has landed).
inline void advanceAttack(Human& h, float dt) {
    AttackState& a = h.attack;
    if (!a.active()) return;
    if (a.stop > 0.0f) { a.stop -= dt; a.prevT = a.t; return; }   // hit-stop
    a.prevT = a.t;
    const AttackDef& d = a.def();
    // the rotation window: while winding up it turns toward its
    // target (or where its body faces), no faster than 'track';
    // from the hit window on it's locked
    if (a.t < d.activeStart) {
        glm::vec3 want = flatFacing(h);
        if (a.hasAim) {
            glm::vec3 to = a.aim - h.position; to.y = 0.0f;
            if (glm::length(to) > 0.5f) want = glm::normalize(to);
        }
        float rate = glm::radians(d.track) * (h.isPlayer() ? 1.0f : NPC_TRACK) * dt;
        float cur = std::atan2(a.dir.z, a.dir.x), tgt = std::atan2(want.z, want.x);
        float diff = std::remainder(tgt - cur, 6.2831853f);
        cur += std::clamp(diff, -rate, rate);
        a.dir = glm::vec3(std::cos(cur), 0.0f, std::sin(cur));
    }
    // a charge: held at charge_at while the button is down
    if (a.charging()) { a.charged += dt; return; }
    if (a.kind == AttackKind::AirHeavy && !a.landed) {
        if (a.t < PLUNGE_HANG) a.t = std::min(a.t + dt, PLUNGE_HANG);
        else if (h.grounded) { a.plunging = false; a.landed = true; a.t = a.prevT = SLAM_AT; }
        else a.plunging = true;
        return;
    }
    a.t += dt * a.speed;
    // the combo: a queued press cuts the recovery short
    if (a.queuedLight && h.grounded && d.comboFrom >= 0.0f && a.t >= d.comboFrom && a.kind == AttackKind::Light) {
        if (beginAttack(h, AttackKind::Light, a.chain + 1)) return;
        a.queuedLight = false;
    }
    if (a.t >= d.duration) a = AttackState();
}

// Poise refills after a few seconds without hits
inline void regainPoise(Human& h, float dt) {
    h.exposed = std::max(0.0f, h.exposed - dt);     // a parried body recovers
    h.sincePoiseHit += dt;
    if (h.sincePoiseHit > POISE_DELAY) h.poise = std::min(h.maxPoise, h.poise + POISE_REFILL * dt);
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
