#pragma once
#include "Combat.h"
#include "HitShapes.h"
#include "../entity/Human.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// ══════════════════════════════════════════════════════════
// CombatHits — who an attack hits (game/Combat.h)
// ══════════════════════════════════════════════════════════
// Run once per physics tick, after every body has moved and
// been posed, over every fighting body: the player, enemies,
// anything Human.
//
// Each attack in its active window moves its weapon from where
// the posed wrist held it last tick to where it holds it now, in
// small steps (an inch or so of blade travel each). At each step
// the weapon's boxes (its voxels, grown by hit_margin: game/
// HitShapes.h) are tested against every other body's limb boxes;
// the first one touched, by a body it hasn't hit yet, is hit
// there, unless a solid voxel is between the attacker's chest
// and that point. Nothing else counts: not reach, arc or height
// band.
//
// Without a pose or a weapon model (an unarmed NPC, a far body,
// a headless test with no models) and for the plunge's slam
// (an area hit) the old rule stands: the slice of the attack's
// arc covered since the last tick, in reach and in the height
// band.
//
// A body hit:
//   rolling or dodging (i-frames)   nothing
//   guard raised within PARRY_WINDOW, facing the attacker:
//                                   PARRIED. No damage; the
//                                   attacker's swing is knocked
//                                   aside and it's EXPOSED for
//                                   EXPOSED_TIME (can't attack or
//                                   guard). Not the plunge's slam
//   exposed (was parried)           a RIPOSTE: RIPOSTE_DAMAGE ×,
//                                   knocked down, no guard
//   guarding, facing the attacker   GUARD_KEEP of the damage,
//                                   stamina pays for the rest;
//                                   out of stamina = guard broken
//   otherwise                       full damage and poise damage
//                                   (poiseHit): a flinch while
//                                   poise holds; broken = stagger
//                                   or knockdown, attack interrupted
// Health at zero: dead. The body goes limp and stays down.
namespace combat {

struct HitEvent {
    uint64_t   attacker = 0;      // Entity::uuid
    uint64_t   target   = 0;
    AttackKind kind     = AttackKind::None;
    float      damage   = 0.0f;   // what got through
    bool       blocked  = false;
    bool       guardBroken = false;
    bool       parried  = false;       // the target parried: the ATTACKER is exposed, nothing landed
    bool       riposte  = false;       // the target was exposed: a critical hit
    bool       poiseBroken = false;    // staggered (or knocked down)
    bool       knockedDown = false;
    bool       killed   = false;
    glm::vec3  point{0.0f};
    int        bone     = -1;         // the limb the blade touched (-1: the old arc rule)
    // for whoever listens (damage numbers, the debug view, sounds …)
    React      react    = React::None;   // what it did to the body (None: it shrugged it off)
    float      hitstun  = 0.0f;       // s the body is stuck reacting
    glm::vec3  dir{0.0f};             // the way it pushed the body
    int        combo    = 0;          // the attacker's hits in a row, this one included
    float      charge   = 0.0f;       // 0..1: how charged the attack was
    int        window   = 0;          // which of the swing's hit windows
    bool       wall     = false;      // (Simulation) knocked into a wall and pinned
    bool       perfectDodge = false;  // nothing landed: the target dodged at the last moment
};

// Where a blade touched a body
struct Contact {
    glm::vec3 point{0.0f};            // on the limb
    glm::vec3 dir{0.0f};              // the blade's travel there (voxels this step)
    int       bone = -1;
};

// What the simulation knows of a body's pose (BodyPoses::posed)
using PoseFn = std::function<bool(const Human&, hitshape::Posed&)>;

// true when nothing solid is between the two points
using ClearFn = std::function<bool(glm::vec3, glm::vec3)>;

// Invulnerable right now? (rolls, backsteps, air dodges)
inline bool invulnerable(const Human& h) {
    if (h.getUpGrace > 0.0f) return true;                // just got up
    if (h.move == Human::Move::Roll) {
        float done = 1.0f - h.moveTimer / Human::ROLL_TIME;
        return done >= ROLL_IFRAME_FROM && done <= ROLL_IFRAME_TO;
    }
    if (h.move == Human::Move::Backstep)
        return 1.0f - h.moveTimer / Human::BACKSTEP_TIME <= BACKSTEP_IFRAME_TO;
    return h.move == Human::Move::AirDodge || h.freeze == Human::Freeze::AirDodge;
}

// A dodge just begun (within PERFECT_WINDOW) that a hit would have
// met: a perfect dodge, once per dodge
inline bool perfectDodge(Human& h) {
    if (h.perfectUsed) return false;
    float since = h.move == Human::Move::Roll ? Human::ROLL_TIME - h.moveTimer
                : h.move == Human::Move::Backstep ? Human::BACKSTEP_TIME - h.moveTimer : 99.0f;
    if (since > PERFECT_WINDOW) return false;
    h.perfectUsed = true;
    return true;
}
inline void dodged(Human& a, Human& b, std::vector<HitEvent>& out) {
    if (!perfectDodge(b)) return;
    HitEvent ev; ev.attacker = a.uuid; ev.target = b.uuid; ev.kind = a.attack.kind; ev.perfectDodge = true;
    ev.point = b.bodyPoint(0.6f);
    out.push_back(ev);
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
    if (dist > d.reach + b.hitRadius) return false;
    // height: the band against the target's body
    float bandLo = a.position.y + d.yLo, bandHi = a.position.y + d.yHi;
    if (b.position.y > bandHi || b.position.y + std::max(b.height, b.hitHeight) < bandLo) return false;
    if (d.arcFrom <= -179.0f && d.arcTo >= 179.0f) return true;   // all around
    if (dist < b.hitRadius * 1.2f) return true;                 // too close to miss
    float span = std::max(d.activeEnd - d.activeStart, 1e-3f);
    float u0 = (lo - d.activeStart) / span, u1 = (hi - d.activeStart) / span;
    float a0 = d.arcFrom + (d.arcTo - d.arcFrom) * u0;
    float a1 = d.arcFrom + (d.arcTo - d.arcFrom) * u1;
    float pad = glm::degrees(std::atan2(b.hitRadius, dist));    // the target's width
    float ang = sideAngle(a.attack.dir, off);
    return ang >= std::min(a0, a1) - pad && ang <= std::max(a0, a1) + pad;
}

// A body with no pose: its standing capsule as one upright box
inline hitshape::OBB standingBox(const Human& b) {
    hitshape::OBB o;
    float h = b.hitHeight > 0.0f ? b.hitHeight : b.height;
    o.c = b.position + glm::vec3(0.0f, h * 0.5f, 0.0f);
    o.h = glm::vec3(b.hitRadius, h * 0.5f, b.hitRadius);
    return o;
}

// The limbs of a body for this tick (world boxes, with their bones)
inline void limbBoxes(const Human& b, const hitshape::Posed* pb,
                      std::vector<std::pair<int, hitshape::OBB>>& out) {
    out.clear();
    if (pb && pb->now && pb->body) {
        for (int i = 0; i < BONE_COUNT; i++)
            if (pb->body->has[i]) out.push_back({ i, pb->body->world(i, *pb->now) });
    }
    if (out.empty()) out.push_back({ -1, standingBox(b) });
}

// Does a's weapon touch body b during the part (s0..s1) of this
// tick's motion? First contact in time. 'limbs' from limbBoxes.
inline bool bladeContact(const hitshape::Posed& pa, float s0, float s1,
                         const std::vector<std::pair<int, hitshape::OBB>>& limbs, Contact& c) {
    if (!pa.now || !pa.held || !pa.model || limbs.empty()) return false;
    const glm::mat4 Wn = (*pa.now)[B_WRIST_R] * pa.held->turn(*pa.model, pa.turnNow);
    const glm::mat4 Wp = pa.prev ? (*pa.prev)[B_WRIST_R] * pa.held->turn(*pa.model, pa.turnPrev) : Wn;
    // the limbs' bounding sphere, for a quick reject
    glm::vec3 bc(0.0f);
    for (auto& l : limbs) bc += l.second.c;
    bc /= (float)limbs.size();
    float br = 0.0f;
    for (auto& l : limbs) br = std::max(br, glm::length(l.second.c - bc) + glm::length(l.second.h));
    // steps: the weapon's farthest box moves about a quarter voxel each
    std::vector<hitshape::OBB> wa, wb;
    hitshape::weaponBoxes(*pa.held, *pa.model, Wp, wa);
    hitshape::weaponBoxes(*pa.held, *pa.model, Wn, wb);
    float travel = 0.0f;
    for (size_t i = 0; i < wa.size() && i < wb.size(); i++) travel = std::max(travel, glm::length(wb[i].c - wa[i].c));
    if (!pa.prev) s0 = s1;
    int steps = std::clamp((int)std::ceil(travel * (s1 - s0) / 0.25f), 1, 64);
    std::vector<hitshape::OBB> w, wLast;
    for (int k = 0; k <= steps; k++) {
        if (k == 0 && s0 == s1 && steps > 0) continue;
        float s = s0 + (s1 - s0) * (float)k / (float)steps;
        glm::mat4 W = hitshape::between(Wp, Wn, s);
        hitshape::weaponBoxes(*pa.held, *pa.model, W, w);
        glm::vec3 wc(W[3]);
        float wr = 0.0f;
        for (auto& o : w) wr = std::max(wr, glm::length(o.c - wc) + glm::length(o.h));
        if (glm::length(wc - bc) > wr + br) { wLast = w; continue; }
        for (auto& l : limbs)
            for (size_t i = 0; i < w.size(); i++)
                if (hitshape::overlap(w[i], l.second)) {
                    c.point = hitshape::closest(l.second, w[i].c);
                    c.bone = l.first;
                    c.dir = (i < wLast.size()) ? w[i].c - wLast[i].c
                          : (i < wb.size() && i < wa.size() ? wb[i].c - wa[i].c : glm::vec3(0.0f));
                    return true;
                }
        wLast = w;
    }
    return false;
}

// How a hit lands on a body, after guard and damage: poise
// decides between a flinch, a stagger and a knockdown. Shared by
// weapons and tools (the axe), so everything breaks poise the
// same way.
//   poiseDmg   poise damage of the hit
//   knock      vox/s the attack would give on a stagger
//   knockdown  a break knocks down
// Returns what happened (sets ev.poiseBroken / ev.knockedDown).
inline float poiseHit(Human& b, float poiseDmg, float knock, bool knockdown, HitEvent& ev) {
    b.sincePoiseHit = 0.0f;
    bool armored = b.attack.active() && b.attack.armored();
    if (!armored) b.poise -= poiseDmg;
    if (b.dead()) { ev.knockedDown = true; return KNOCKDOWN_KNOCK; }
    if (b.poise > 0.0f) return std::min(knock, FLINCH_KNOCK);          // holds: a flinch
    ev.poiseBroken = true;
    b.poise = b.maxPoise;                                               // broken: refills
    b.attack = AttackState();                                           // and interrupted
    if (knockdown) { ev.knockedDown = true; return std::max(knock, KNOCKDOWN_KNOCK); }
    return std::clamp(knock, STAGGER_MIN, KNOCKDOWN_KNOCK * 0.7f);      // a stagger, never a fall
}

// The reaction an attack gives when its def doesn't say
inline React defaultReact(AttackKind k) {
    switch (k) {
    case AttackKind::Heavy:    return React::Stumble;
    case AttackKind::AirHeavy: return React::Slam;
    case AttackKind::None:     return React::Flinch;      // a tool (the axe)
    default:                   return React::Flinch;
    }
}

// Put a body into a reaction: interrupted, stuck for its hitstun,
// thrown by it (the shove and lift wait for the hit-stop to end;
// physics plays them out). A knockdown is the ragdoll's: returns
// the knock to give it (0 for the rest)
inline float startReaction(Human& b, React r, glm::vec3 push, glm::vec3 point, float stunOverride, float charge, HitEvent& ev) {
    if (r == React::Slam && b.grounded) r = React::Knockdown;     // nothing to drive into the ground: it falls
    b.attack = AttackState();
    b.guarding = false;
    ev.react = r;
    if (r == React::Knockdown) { ev.knockedDown = true; b.hitstun = 0.0f; b.react = React::None; return KNOCKDOWN_KNOCK; }
    int ri = (int)r;
    glm::vec3 flat(push.x, 0.0f, push.z);
    flat = glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(0.0f);
    float stun = stunOverride >= 0.0f ? stunOverride : REACT_STUN[ri];
    if (b.isPlayer()) stun *= PLAYER_STUN;
    b.hitstun = std::max(b.hitstun, stun);
    b.react = r; b.reactDir = flat; b.reactAge = 0.0f;
    ev.hitstun = b.hitstun;
    // a heavier body is thrown less
    float weight = std::clamp(75.0f / std::max(b.mass, 20.0f), 0.6f, 1.3f);
    float shove = REACT_SHOVE[ri] * weight * (1.0f + 0.3f * charge), lift = 0.0f;
    const bool air = !b.grounded;
    switch (r) {
    case React::Flinch: case React::Stumble:
        if (air) { lift = b.juggles < JUGGLE_MAX ? JUGGLE_POP : 0.0f; shove *= 0.4f; b.juggles++; }
        break;
    case React::Launch:
        lift = air ? JUGGLE_POP * 1.6f : std::sqrt(2.0f * Human::GRAVITY * LAUNCH_HEIGHT);
        if (air) b.juggles++;
        break;
    case React::Slam:
        lift = -SLAM_DOWN; shove *= 0.3f;
        break;
    default: break;
    }
    b.pendingShove = flat * shove;
    b.pendingLift = lift;
    b.pendingSlide = REACT_SLIDE[ri];
    // the limbs give way with it (the ragdoll shakes, never falls)
    glm::vec3 wob = glm::normalize(flat + glm::vec3(0.0f, r == React::Launch ? 0.6f : 0.15f, 0.0f));
    float sp = Ragdoll::KNOCKDOWN_DV * REACT_WOBBLE * (r == React::Flinch ? 0.6f : 1.0f);
    b.hit(point, wob * sp * std::max(b.mass, 1.0f), true);
    return 0.0f;
}

// Give the body its knock: a speed change of 'knock' vox/s
// whatever its mass (poise already accounts for toughness)
inline void knockBody(Human& b, glm::vec3 point, glm::vec3 dir, float lift, float knock) {
    if (knock <= 0.0f) return;
    glm::vec3 J = glm::normalize(dir + glm::vec3(0.0f, lift, 0.0f)) * knock * b.mass;
    b.hit(point, J);
}

// Apply one landed hit (at a blade contact, if there is one)
inline HitEvent land(Human& a, Human& b, const Contact* contact = nullptr) {
    const AttackDef& d = a.attack.def();
    HitEvent ev;
    ev.attacker = a.uuid; ev.target = b.uuid; ev.kind = a.attack.kind;
    // a charged heavy hits harder (game/Combat.h charge)
    const float charge = a.attack.charge();
    float dmg = a.weaponDamage * d.damage * (1.0f + (d.chargeDamage - 1.0f) * charge);
    float poiseDmg = d.poise * a.attack.poiseScale * (1.0f + (d.chargePoise - 1.0f) * charge);
    // the hit-stop both bodies take (the attack's own, or by its kind)
    float stopFor = d.hitstop >= 0.0f ? d.hitstop
                  : (a.attack.kind == AttackKind::Heavy || a.attack.kind == AttackKind::AirHeavy) ? HITSTOP_HEAVY : HITSTOP_LIGHT;
    stopFor *= 1.0f + 0.5f * charge;
    glm::vec3 off = b.position - a.position; off.y = 0.0f;
    glm::vec3 away = glm::length(off) > 1e-3f ? glm::normalize(off) : a.attack.dir;
    // AirHeavy throws outward from the slam; the rest along the swing
    glm::vec3 push = a.attack.kind == AttackKind::AirHeavy ? away : glm::normalize(a.attack.dir * 0.6f + away * 0.4f);
    ev.point = b.bodyPoint(a.attack.kind == AttackKind::Heavy ? 0.85f : 0.6f);
    if (contact) {
        // the blade's way at the contact carries the body
        ev.point = contact->point;
        ev.bone = contact->bone;
        glm::vec3 flat(contact->dir.x, 0.0f, contact->dir.z);
        if (glm::length(flat) > 1e-3f) push = glm::normalize(glm::normalize(flat) * 0.6f + away * 0.4f);
    }

    // guard: held, standing, the attacker in front
    glm::vec2 bf(b.facing.x, b.facing.z);
    const Human::GuardStyle& gs = b.guardStyle;
    bool facing = glm::length(bf) > 1e-3f && glm::dot(glm::normalize(bf), -glm::vec2(away.x, away.z)) > gs.facing;
    float knock;
    bool reacted = false;
    ev.charge = charge;
    ev.dir = push;
    ev.window = a.attack.window;
    a.combo = a.sinceLanded < 1.5f ? a.combo + 1 : 1;
    a.sinceLanded = 0.0f;
    b.combo = 0;
    ev.combo = a.combo;
    if (b.guarding && facing && b.guardTime <= gs.parry && a.attack.kind != AttackKind::AirHeavy) {
        // parried: the swing is knocked aside, the attacker reels
        ev.parried = ev.blocked = true;
        ev.damage = 0.0f;
        b.lastHitBy = a.uuid;
        a.attack = AttackState();
        a.exposed = EXPOSED_TIME;
        glm::vec3 back = -away;
        knockBody(a, a.bodyPoint(0.7f), back, 0.1f, PARRY_KNOCK);
        b.guardTime = gs.parry + 1.0f;               // one parry per raise
        a.hitStop = b.hitStop = HITSTOP_PARRY;       // the clash holds both
        return ev;
    }
    if (b.exposed > 0.0f) {
        // a riposte on a parried body: critical, and it goes down
        ev.riposte = true;
        b.exposed = 0.0f;
        b.health = std::max(0.0f, b.health - dmg * RIPOSTE_DAMAGE);
        ev.poiseBroken = ev.knockedDown = true;
        b.poise = b.maxPoise;
        b.attack = AttackState();
        knock = std::max(d.knock, KNOCKDOWN_KNOCK);
        dmg *= RIPOSTE_DAMAGE;
        ev.react = React::Knockdown;
    } else if (b.guarding && facing) {
        ev.blocked = true;
        float cost = dmg * (1.0f - gs.keep) * GUARD_COST * gs.cost;
        if (!b.infiniteStamina) { b.stamina -= cost; b.staminaWait = STAMINA_DELAY; }
        if (b.stamina < 0.0f) {                       // guard broken: staggers, takes half
            ev.guardBroken = true;
            b.stamina = 0.0f;
            dmg *= 0.5f;
            knock = startReaction(b, React::Stumble, push, ev.point, -1.0f, 0.0f, ev);   // reels, wide open
            reacted = true;
        } else {
            dmg *= gs.keep;
            knock = std::min(d.knock * (gs.shield ? 0.15f : 0.25f), FLINCH_KNOCK);   // pushed back a little (less behind a shield)
        }
        b.health = std::max(0.0f, b.health - dmg);
        if (b.dead()) { knock = KNOCKDOWN_KNOCK; ev.knockedDown = true; }
    } else {
        b.health = std::max(0.0f, b.health - dmg);
        // what it does to the body: the attack's reaction (a full
        // charge one stronger). A body reacts to anything that beats
        // its armor; poise still wears down, and when it breaks the
        // body reels (or falls, for an attack that knocks down)
        React r = d.react != React::None ? d.react : defaultReact(a.attack.kind);
        if (charge >= 0.99f && (r == React::Flinch || r == React::Stumble)) r = (React)((int)r + 1);
        bool armoredNow = b.attack.active() && b.attack.armored();
        b.sincePoiseHit = 0.0f;
        if (!armoredNow) b.poise -= poiseDmg;
        if (b.poise <= 0.0f) {
            ev.poiseBroken = true;
            b.poise = b.maxPoise;
            if (d.knockdown) r = React::Knockdown;
            else if (reactTier(r) < reactTier(React::Stumble)) r = React::Stumble;
        }
        if (b.dead()) { ev.knockedDown = true; knock = std::max(d.knock, KNOCKDOWN_KNOCK); }
        else if (ev.poiseBroken || (!armoredNow && reactTier(r) > b.armor)) {
            knock = startReaction(b, r, push, ev.point, d.hitstun, charge, ev);
            reacted = true;
        } else knock = std::min(d.knock, FLINCH_KNOCK);                  // shrugged off: a flinch of the limbs
    }
    b.sinceHurt = 0.0f;
    b.lastHitBy = a.uuid;
    ev.damage = dmg;
    if (b.dead()) {
        ev.killed = true;
        b.attack = AttackState();
        b.ragdoll.stayDown = true;
    }
    knockBody(b, ev.point - push * 1.0f, push, d.lift, knock);
    // a bigger reaction holds a little longer
    if (ev.react == React::Launch || ev.react == React::Slam) stopFor *= 1.3f;
    // both bodies hold for a moment (the attacker's swing too); then
    // a hit its poise held (or a guard took) shoves the target back,
    // a quick slide; a broken poise is the ragdoll's
    float stop = stopFor * (ev.blocked ? HITSTOP_BLOCKED : 1.0f);
    a.attack.prevT = a.attack.t;                 // (held: no sweep while it holds)
    a.hitStop = std::max(a.hitStop, stop);
    b.hitStop = std::max(b.hitStop, stop);
    if (!reacted && !ev.knockedDown && !ev.killed && !b.noRagdoll) {
        glm::vec3 flat(push.x, 0.0f, push.z);
        if (glm::length(flat) > 1e-3f)
            b.pendingShove = glm::normalize(flat) * d.knock * SHOVE * (1.0f + 0.5f * charge) * (ev.blocked ? SHOVE_BLOCKED : 1.0f);
    }
    return ev;
}

// A shot (an arrow) landing on a body: as land() but from a flying
// thing, so no attacker body is needed (it may be gone). A guard
// held facing the shot takes it (stamina, no damage; no parry); a
// rolling body it passes through (returns false). 'r' is what it
// does to the body (a full draw: Stumble), against the body's
// armor like a swing; poise still wears down.
inline bool shotHit(Human& b, uint64_t attacker, float dmg, float poiseDmg, float knock, React r,
                    float charge, glm::vec3 point, glm::vec3 dir, HitEvent& ev) {
    if (invulnerable(b)) return false;
    ev = HitEvent();
    ev.attacker = attacker; ev.target = b.uuid; ev.kind = AttackKind::Shot;
    ev.point = point; ev.charge = charge;
    glm::vec3 flat(dir.x, 0.0f, dir.z);
    flat = glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(0, 0, 1);
    ev.dir = flat;
    glm::vec2 bf(b.facing.x, b.facing.z);
    const Human::GuardStyle& gs = b.guardStyle;
    bool facing = glm::length(bf) > 1e-3f && glm::dot(glm::normalize(bf), -glm::vec2(flat.x, flat.z)) > gs.facing;
    if (b.guarding && facing) {
        ev.blocked = true;
        float cost = dmg * (1.0f - gs.keep) * GUARD_COST * gs.cost;
        if (!b.infiniteStamina) { b.stamina -= cost; b.staminaWait = STAMINA_DELAY; }
        if (b.stamina < 0.0f) { b.stamina = 0.0f; ev.guardBroken = true; startReaction(b, React::Stumble, flat, point, -1.0f, 0.0f, ev); }
        ev.damage = 0.0f;
        b.hitStop = std::max(b.hitStop, HITSTOP_LIGHT * HITSTOP_BLOCKED);
        return true;
    }
    b.health = std::max(0.0f, b.health - dmg);
    ev.damage = dmg;
    b.combo = 0;
    bool armoredNow = b.attack.active() && b.attack.armored();
    b.sincePoiseHit = 0.0f;
    if (!armoredNow) b.poise -= poiseDmg;
    if (b.poise <= 0.0f) {
        ev.poiseBroken = true;
        b.poise = b.maxPoise;
        if (reactTier(r) < reactTier(React::Stumble)) r = React::Stumble;
    }
    float k = 0.0f;
    if (b.dead()) { ev.knockedDown = ev.killed = true; k = std::max(knock, KNOCKDOWN_KNOCK); b.attack = AttackState(); b.ragdoll.stayDown = true; }
    else if (ev.poiseBroken || (!armoredNow && reactTier(r) > b.armor)) k = startReaction(b, r, flat, point, -1.0f, charge, ev);
    else k = std::min(knock, FLINCH_KNOCK);
    knockBody(b, point, flat, 0.1f, k);
    b.sinceHurt = 0.0f;
    b.lastHitBy = attacker;
    b.hitStop = std::max(b.hitStop, HITSTOP_LIGHT * (1.0f + 0.5f * charge));
    return true;
}

// Every attack in its active window against every other body
// canHit(attacker, target): false skips the pair (an NPC's swing
// passes through its friends)
using CanHitFn = std::function<bool(const Human&, const Human&)>;
// targetPose(attacker, target, out): where to check the target's
// limbs for this attacker (lag compensation: as the attacker saw
// it); null: as it is now (poseOf)
using TargetPoseFn = std::function<bool(const Human&, const Human&, hitshape::Posed&)>;
// defer: a hit a moment from being judged (true: it's taken, not landed
// now; Simulation's pending hits: a player on a server, whose guard
// may already be up on their screen)
using DeferFn = std::function<bool(Human& a, Human& b, const Contact* c)>;
inline void resolveHits(const std::vector<Human*>& bodies, const ClearFn& clear, std::vector<HitEvent>& out,
                        const CanHitFn& canHit = nullptr, const PoseFn& poseOf = nullptr,
                        const TargetPoseFn& targetPose = nullptr, const DeferFn& defer = nullptr) {
    std::vector<std::pair<int, hitshape::OBB>> limbs;
    for (Human* a : bodies) {
        if (!a || !a->attack.active() || a->dead()) continue;
        AttackState& st = a->attack;
        if (st.kind == AttackKind::AirHeavy && !st.landed) continue;   // only the slam hits
        if (!st.def().blade) continue;                         // a spell: its effect does the work (Simulation::castEffects)
        float t0 = st.prevT, t1 = st.t;
        // the blade: posed, with a weapon model (not the slam)
        hitshape::Posed pa;
        bool blade = poseOf && st.kind != AttackKind::AirHeavy && poseOf(*a, pa) && pa.held;
        const AttackDef& d = st.def();
        // which hit window this step is in: a new one can hit the
        // same bodies again
        float lo = 0.0f, hi = -1.0f;
        bool inWindow = false;
        for (int wi = 0; wi < d.windowCount(); wi++) {
            glm::vec2 w = d.window(wi);
            float l = std::max(t0, w.x), h = std::min(t1, w.y);
            if (h >= l && t1 >= w.x && t0 <= w.y) {
                if (wi > st.window) { st.window = wi; st.hitIds.clear(); }
                lo = l; hi = h; inWindow = true;
                break;
            }
        }
        if (!inWindow && !blade && d.windowCount() > 1) continue;   // between hits
        float s0 = 0.0f, s1 = 1.0f;
        if (blade) {
            if (!inWindow || t1 - t0 < 1e-6f) continue;    // outside the hit window, or held in hit-stop
            s0 = std::clamp((lo - t0) / (t1 - t0), 0.0f, 1.0f);
            s1 = std::clamp((hi - t0) / (t1 - t0), 0.0f, 1.0f);
        }
        for (Human* b : bodies) {
            if (!b || b == a || b->dead()) continue;
            if (canHit && !canHit(*a, *b)) continue;
            if (std::find(st.hitIds.begin(), st.hitIds.end(), b->uuid) != st.hitIds.end()) continue;
            if (blade) {
                hitshape::Posed pb;
                bool bp = (targetPose && targetPose(*a, *b, pb)) || poseOf(*b, pb);
                limbBoxes(*b, bp ? &pb : nullptr, limbs);
                Contact c;
                if (!bladeContact(pa, s0, s1, limbs, c)) continue;
                if (invulnerable(*b)) { dodged(*a, *b, out); continue; }
                if (clear && !clear(a->bodyPoint(0.6f), c.point)) continue;   // no hits through walls
                st.hitIds.push_back(b->uuid);
                if (defer && defer(*a, *b, &c)) continue;
                out.push_back(land(*a, *b, &c));
                if (!a->attack.active()) break;         // parried: the swing is over
                continue;
            }
            if (!sweeps(*a, *b, t0, t1)) continue;
            if (invulnerable(*b)) { dodged(*a, *b, out); continue; }   // stays live: can still hit when the roll ends
            if (clear && !clear(a->bodyPoint(0.6f), b->bodyPoint(0.6f))) continue;
            st.hitIds.push_back(b->uuid);
            if (defer && defer(*a, *b, nullptr)) continue;
            out.push_back(land(*a, *b));
            if (!a->attack.active()) break;             // parried: the swing is over
        }
    }
}

// Bodies don't pass through each other: any two standing ones
// that overlap are pushed apart (by mass), where there is room.
// fits(position) says a body could stand there.
inline void separate(const std::vector<Human*>& bodies, const std::function<bool(glm::vec3)>& fits) {
    for (size_t i = 0; i < bodies.size(); i++)
        for (size_t j = i + 1; j < bodies.size(); j++) {
            Human& a = *bodies[i];
            Human& b = *bodies[j];
            const float minD = a.radius() + b.radius();
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
