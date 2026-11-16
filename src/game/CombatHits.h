#pragma once
#include "Combat.h"
#include "../entity/Human.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// ══════════════════════════════════════════════════════════
// CombatHits — who an attack hits (game/Combat.h)
// ══════════════════════════════════════════════════════════
// Run once per physics tick, after every body has moved, over
// every fighting body: the player, enemies, anything Human.
// Each attack in its active window sweeps the slice of its arc
// covered since the last tick; any body in that slice, in reach
// and in the height band, that it hasn't already hit and can
// see (no solid voxel between), is hit:
//   rolling or dodging (i-frames)   nothing
//   guarding, facing the attacker   GUARD_KEEP of the damage,
//                                   stamina pays for the rest;
//                                   out of stamina = guard broken
//   otherwise                       full damage, the knock goes
//                                   to the ragdoll, and it
//                                   interrupts the target's own
//                                   attack unless it has poise
// Health at zero: dead. The body goes limp and stays down.
namespace combat {

struct HitEvent {
    Human*     attacker = nullptr;
    Human*     target   = nullptr;
    AttackKind kind     = AttackKind::None;
    float      damage   = 0.0f;   // what got through
    bool       blocked  = false;
    bool       guardBroken = false;
    bool       killed   = false;
    glm::vec3  point{0.0f};
};

// true when nothing solid is between the two points
using ClearFn = std::function<bool(glm::vec3, glm::vec3)>;

// Invulnerable right now? (rolls, backsteps, air dodges)
inline bool invulnerable(const Human& h) {
    if (h.move == Human::Move::Roll) {
        float done = 1.0f - h.moveTimer / Human::ROLL_TIME;
        return done >= ROLL_IFRAME_FROM && done <= ROLL_IFRAME_TO;
    }
    if (h.move == Human::Move::Backstep)
        return 1.0f - h.moveTimer / Human::BACKSTEP_TIME <= BACKSTEP_IFRAME_TO;
    return h.move == Human::Move::AirDodge || h.freeze == Human::Freeze::AirDodge;
}

// Angle (degrees, positive to the left) of a horizontal offset
// around a facing
inline float sideAngle(glm::vec3 facing, glm::vec3 d) {
    glm::vec2 f(facing.x, facing.z);
    float fl = glm::length(f);
    f = fl > 1e-4f ? f / fl : glm::vec2(0, -1);
    glm::vec2 left(f.y, -f.x);
    glm::vec2 o(d.x, d.z);
    return glm::degrees(std::atan2(glm::dot(o, left), glm::dot(o, f)));
}

// Would this attack, swept over (t0, t1], reach body b?
inline bool sweeps(const Human& a, const Human& b, float t0, float t1) {
    const AttackDef& d = a.attack.def();
    float lo = std::max(t0, d.activeStart), hi = std::min(t1, d.activeEnd);
    if (hi < lo || t1 < d.activeStart) return false;
    glm::vec3 off = b.position - a.position;
    float dist = std::sqrt(off.x * off.x + off.z * off.z);
    if (dist > d.reach + Human::RADIUS) return false;
    // height: the band against the target's body
    float bandLo = a.position.y + d.yLo, bandHi = a.position.y + d.yHi;
    if (b.position.y > bandHi || b.position.y + b.height < bandLo) return false;
    if (d.arcFrom <= -179.0f && d.arcTo >= 179.0f) return true;   // all around
    if (dist < Human::RADIUS * 1.2f) return true;                 // too close to miss
    float span = std::max(d.activeEnd - d.activeStart, 1e-3f);
    float u0 = (lo - d.activeStart) / span, u1 = (hi - d.activeStart) / span;
    float a0 = d.arcFrom + (d.arcTo - d.arcFrom) * u0;
    float a1 = d.arcFrom + (d.arcTo - d.arcFrom) * u1;
    float pad = glm::degrees(std::atan2(Human::RADIUS, dist));    // the target's width
    float ang = sideAngle(a.attack.dir, off);
    return ang >= std::min(a0, a1) - pad && ang <= std::max(a0, a1) + pad;
}

// Apply one landed hit
inline HitEvent land(Human& a, Human& b) {
    const AttackDef& d = a.attack.def();
    HitEvent ev;
    ev.attacker = &a; ev.target = &b; ev.kind = a.attack.kind;
    float dmg = a.weaponDamage * d.damage;
    glm::vec3 off = b.position - a.position; off.y = 0.0f;
    glm::vec3 away = glm::length(off) > 1e-3f ? glm::normalize(off) : a.attack.dir;
    // AirHeavy throws outward from the slam; the rest along the swing
    glm::vec3 push = a.attack.kind == AttackKind::AirHeavy ? away : glm::normalize(a.attack.dir * 0.6f + away * 0.4f);
    float knock = d.knock;
    ev.point = b.bodyPoint(a.attack.kind == AttackKind::Heavy ? 0.85f : 0.6f);

    // guard: held, standing, the attacker in front
    glm::vec2 bf(b.facing.x, b.facing.z);
    bool facing = glm::length(bf) > 1e-3f && glm::dot(glm::normalize(bf), -glm::vec2(away.x, away.z)) > 0.35f;
    if (b.guarding && facing) {
        ev.blocked = true;
        float cost = dmg * (1.0f - GUARD_KEEP) * GUARD_COST;
        if (!b.infiniteStamina) { b.stamina -= cost; b.staminaWait = STAMINA_DELAY; }
        if (b.stamina < 0.0f) {                       // guard broken: staggers, takes half
            ev.guardBroken = true;
            b.stamina = 0.0f;
            dmg *= 0.5f;
            knock = std::max(knock, 24.0f);
            b.attack = AttackState();
        } else {
            dmg *= GUARD_KEEP;
            knock *= 0.25f;                           // pushed back a little
        }
    }
    b.health = std::max(0.0f, b.health - dmg);
    b.sinceHurt = 0.0f;
    b.lastHitBy = a.uuid;
    ev.damage = dmg;
    if (b.dead()) {
        ev.killed = true;
        b.attack = AttackState();
        b.ragdoll.stayDown = true;
        knock = std::max(knock, 42.0f);              // falls
    }
    // poise: the target's own attack goes on through a small hit
    if (b.attack.active() && !ev.blocked) {
        const AttackDef& bd = b.attack.def();
        bool armoured = bd.poiseFrom >= 0.0f && b.attack.t >= bd.poiseFrom && b.attack.t <= bd.activeEnd;
        if (knock > (armoured ? HEAVY_POISE : POISE)) b.attack = AttackState();
    }
    glm::vec3 J = glm::normalize(push + glm::vec3(0.0f, d.lift, 0.0f)) * knock * Human::REFERENCE_MASS;
    if (knock > 0.0f) b.hit(ev.point - push * 1.0f, J);
    a.attack.stop = HITSTOP;
    return ev;
}

// Every attack in its active window against every other body
inline void resolveHits(const std::vector<Human*>& bodies, const ClearFn& clear, std::vector<HitEvent>& out) {
    for (Human* a : bodies) {
        if (!a || !a->attack.active() || a->dead()) continue;
        AttackState& st = a->attack;
        if (st.kind == AttackKind::AirHeavy && !st.landed) continue;   // only the slam hits
        float t0 = st.prevT, t1 = st.t;
        for (Human* b : bodies) {
            if (!b || b == a || b->dead()) continue;
            if (std::find(st.hitIds.begin(), st.hitIds.end(), b->uuid) != st.hitIds.end()) continue;
            if (!sweeps(*a, *b, t0, t1)) continue;
            if (invulnerable(*b)) continue;          // stays live: can still hit when the roll ends
            if (clear && !clear(a->bodyPoint(0.6f), b->bodyPoint(0.6f))) continue;
            st.hitIds.push_back(b->uuid);
            out.push_back(land(*a, *b));
        }
    }
}

// Bodies don't pass through each other: any two standing ones
// that overlap are pushed apart (by mass), where there is room.
// fits(position) says a body could stand there.
inline void separate(const std::vector<Human*>& bodies, const std::function<bool(glm::vec3)>& fits) {
    const float minD = Human::RADIUS * 2.0f;
    for (size_t i = 0; i < bodies.size(); i++)
        for (size_t j = i + 1; j < bodies.size(); j++) {
            Human& a = *bodies[i];
            Human& b = *bodies[j];
            if (a.ragdoll.controlsLocked() || b.ragdoll.controlsLocked()) continue;   // a fallen body is a ragdoll
            if (a.flying || b.flying) continue;
            glm::vec3 d = b.position - a.position;
            if (d.y > a.height || -d.y > b.height) continue;
            glm::vec2 h(d.x, d.z);
            float l = glm::length(h);
            if (l >= minD) continue;
            glm::vec2 n = l > 1e-4f ? h / l : glm::vec2(1.0f, 0.0f);
            float push = minD - l;
            float wa = b.mass / std::max(a.mass + b.mass, 1.0f);
            glm::vec3 pa = a.position - glm::vec3(n.x, 0, n.y) * push * wa;
            glm::vec3 pb = b.position + glm::vec3(n.x, 0, n.y) * push * (1.0f - wa);
            bool oka = !fits || fits(pa), okb = !fits || fits(pb);
            if (oka && okb) { a.position = pa; b.position = pb; }
            else if (oka) a.position -= glm::vec3(n.x, 0, n.y) * push;
            else if (okb) b.position += glm::vec3(n.x, 0, n.y) * push;
        }
}

} // namespace combat
