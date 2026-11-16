#pragma once
#include "../entity/Human.h"
#include "../character/Appearance.h"
#include "../physics/IPhysics.h"
#include "ItemRegistry.h"
#include "Combat.h"
#include "CombatHits.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Npcs — other people in the world (for now: enemies)
// ══════════════════════════════════════════════════════════
// An Npc is a Human like the player: the same body, physics,
// ragdoll and moveset (game/Combat.h). Only what drives it is
// different: a brain fills its HumanIntent each tick, where the
// player's comes from the keyboard.
//
//   Dummy       stands there with a sword up and takes hits
//               (to test damage, knock and poise); gets up again
//   Swordsman   walks up, circles, attacks with combos and
//               heavies, guards, and rolls out of the way
//
// Spawned with the 'spawn' command; not saved with the world.
enum class NpcKind : uint8_t { Dummy, Swordsman };

inline const char* npcKindName(NpcKind k) { return k == NpcKind::Dummy ? "dummy" : "swordsman"; }

class Npc : public Human {
public:
    NpcKind     kind = NpcKind::Swordsman;
    std::string name;
    Appearance  appearance;
    std::string weaponItem = "iron_sword";
    HumanIntent intent;
    float       deadFor = 0.0f;    // s since it died (removed after a while)
    glm::vec3   prevPosition{0.0f};// before the last tick (drawing blends the two)

    // ── The brain's state ─────────────────────────────────
    enum class Plan : uint8_t { Idle, Approach, Circle, Guard, Attack, Recover };
    Plan     plan = Plan::Idle;
    float    planTime = 0.0f;      // s left in this plan
    float    circleSide = 1.0f;    // +1 left, −1 right
    int      comboLeft = 0;        // light presses still to make
    uint32_t rng = 1;

    float rand01() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); }
};

namespace npcbrain {

constexpr float SEE_RANGE   = 90.0f;   // vox: notices the target
constexpr float FIGHT_RANGE = 13.0f;   // vox: close enough to swing
constexpr float KEEP_RANGE  = 16.0f;   // vox: circles at about this distance

inline glm::vec2 flat(glm::vec3 v) { return glm::vec2(v.x, v.z); }

// Fill the intent for one tick. 'target' may be null (nobody to fight).
inline void think(Npc& n, const Human* target, float dt) {
    HumanIntent& in = n.intent;
    in.move = glm::vec2(0.0f);
    in.sprint = false;
    in.crouch = false;
    in.guard = false;
    in.speedScale = 1.0f;
    if (n.dead() || n.ragdoll.controlsLocked()) { n.plan = Npc::Plan::Idle; return; }

    if (n.kind == NpcKind::Dummy) {
        in.guard = false;
        if (target) {
            glm::vec2 d = flat(target->position - n.position);
            if (glm::length(d) > 1e-3f) { d = glm::normalize(d); n.facing = glm::vec3(d.x, 0, d.y); }
        }
        return;
    }

    if (!target || target->dead()) { n.plan = Npc::Plan::Idle; return; }
    glm::vec2 off = flat(target->position - n.position);
    float dist = glm::length(off);
    if (dist > SEE_RANGE) { n.plan = Npc::Plan::Idle; return; }
    glm::vec2 to = dist > 1e-3f ? off / dist : glm::vec2(0, -1);
    glm::vec2 left(to.y, -to.x);
    n.facing = glm::vec3(to.x, 0.0f, to.y);    // always faces the target (the attack locks its own way)

    n.planTime -= dt;
    bool busy = n.attack.active() || n.move != Human::Move::None;

    // Danger: the target is winding up an attack close by → maybe roll away
    if (!busy && target->attack.active() && dist < FIGHT_RANGE + 4.0f) {
        const AttackDef& d = target->attack.def();
        float until = d.activeStart - target->attack.t;
        if (until > 0.0f && until < 0.25f && n.plan != Npc::Plan::Guard) {
            float r = n.rand01();
            if (r < 0.35f && n.stamina > combat::ROLL_STAMINA) {
                in.move = (n.rand01() < 0.5f ? left : -left) * 0.7f - to * 0.7f;   // back and to a side
                in.dodge = true;
                n.plan = Npc::Plan::Recover; n.planTime = 0.5f;
                return;
            }
            if (r < 0.65f) { n.plan = Npc::Plan::Guard; n.planTime = 0.6f + 0.6f * n.rand01(); }
        }
    }

    // Chain the combo: press again inside the window
    if (n.attack.active()) {
        const AttackDef& d = n.attack.def();
        if (n.comboLeft > 0 && d.comboFrom >= 0.0f && n.attack.t >= d.activeEnd && !n.attack.queuedLight) {
            in.attackLight = true;
            n.comboLeft--;
        }
        return;
    }

    switch (n.plan) {
    case Npc::Plan::Idle:
    case Npc::Plan::Approach:
        if (dist > FIGHT_RANGE) {
            n.plan = Npc::Plan::Approach;
            in.move = to;
            in.sprint = dist > 40.0f && n.stamina > 40.0f;
            break;
        }
        n.plan = Npc::Plan::Circle;
        n.planTime = 0.4f + 0.9f * n.rand01();
        n.circleSide = n.rand01() < 0.5f ? 1.0f : -1.0f;
        [[fallthrough]];
    case Npc::Plan::Circle:
        in.move = left * n.circleSide * 0.6f + to * std::clamp((dist - KEEP_RANGE) * 0.2f, -0.6f, 0.6f);
        in.speedScale = 0.6f;
        if (dist > FIGHT_RANGE + 12.0f) { n.plan = Npc::Plan::Approach; break; }
        if (n.planTime <= 0.0f) {
            float r = n.rand01();
            if (n.stamina < 35.0f)  { n.plan = Npc::Plan::Guard; n.planTime = 1.0f; }
            else if (r < 0.75f)     { n.plan = Npc::Plan::Attack; n.planTime = 1.2f; }
            else                    { n.plan = Npc::Plan::Guard; n.planTime = 0.5f + 0.8f * n.rand01(); }
        }
        break;
    case Npc::Plan::Attack:
        if (dist > FIGHT_RANGE - 2.0f) { in.move = to; break; }   // step in
        if (n.rand01() < 0.25f) in.attackHeavy = true;
        else { in.attackLight = true; n.comboLeft = n.rand01() < 0.5f ? 1 : 0; }
        n.plan = Npc::Plan::Recover;
        n.planTime = 0.3f + 0.5f * n.rand01();
        break;
    case Npc::Plan::Guard:
        in.guard = true;
        in.move = left * n.circleSide * 0.4f;
        if (n.planTime <= 0.0f) { n.plan = Npc::Plan::Circle; n.planTime = 0.4f + 0.6f * n.rand01(); }
        break;
    case Npc::Plan::Recover:
        in.move = -to * 0.4f + left * n.circleSide * 0.3f;
        in.speedScale = 0.6f;
        if (n.planTime <= 0.0f) { n.plan = Npc::Plan::Circle; n.planTime = 0.3f + 0.6f * n.rand01(); }
        break;
    }
}

} // namespace npcbrain

// ── All of them ───────────────────────────────────────────
class NpcManager {
public:
    static constexpr float REMOVE_DEAD_AFTER = 20.0f;

    Npc& spawn(NpcKind kind, glm::vec3 at, glm::vec3 facing) {
        auto n = std::make_unique<Npc>();
        n->kind = kind;
        n->position = n->prevPosition = at;
        n->facing = facing;
        n->rng = 0x9e3779b9u ^ (uint32_t)(n->uuid * 2654435761u);
        n->appearance = Appearance::random(n->rng);
        n->name = std::string(npcKindName(kind)) + " " + std::to_string(n->uuid);
        n->setBodyMass(BodyMass::bodyKgFor(n->appearance.build, n->appearance.heightVoxels, n->appearance.shoulders,
                                           n->appearance.chest, n->appearance.hips, n->appearance.limbs));
        uint16_t wid = gItems.find(n->weaponItem);
        const ItemDef* w = wid ? &gItems.get(wid) : nullptr;
        n->weaponDamage = w ? w->damage : 12.0f;
        if (w) { n->body.addGear(SEG_HAND_R, w->weight); n->mass = n->body.total(); }
        if (kind == NpcKind::Dummy) { n->maxHealth = n->health = 300.0f; }
        list.push_back(std::move(n));
        return *list.back();
    }

    // One physics tick for every NPC: think, move, the body.
    void tick(float dt, IPhysics& physics, const Human* target) {
        for (auto& n : list) {
            n->prevPosition = n->position;
            if (n->dead()) n->deadFor += dt;
            n->sinceHurt += dt;
            if (n->kind == NpcKind::Dummy && !n->dead() && n->sinceHurt > 4.0f)
                n->health = n->maxHealth;                  // the dummy heals
            npcbrain::think(*n, target, dt);
            physics.moveHuman(*n, n->intent, dt);
            physics.stepRagdoll(*n, dt);
            n->intent.clearOneShots();
        }
        for (size_t i = 0; i < list.size();)
            if (list[i]->deadFor > REMOVE_DEAD_AFTER || list[i]->fellOutOfWorld) list.erase(list.begin() + i);
            else i++;
    }

    void clear() { list.clear(); }
    std::vector<std::unique_ptr<Npc>>& all() { return list; }

private:
    std::vector<std::unique_ptr<Npc>> list;
};
