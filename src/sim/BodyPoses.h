#pragma once
#include "../character/HumanAnim.h"
#include "../character/HeldItemModel.h"
#include "../entity/Player.h"
#include "../npc/Npc.h"
#include "../game/HitShapes.h"
#include "../physics/Ragdoll.h"
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

// ══════════════════════════════════════════════════════════
// BodyPoses — where every body's limbs and blade are, each tick
// ══════════════════════════════════════════════════════════
// The simulation's own copy of each nearby body's skeleton, run
// by the same animator and the same input (character/HumanAnim.h)
// as what is drawn, once per tick. Hits come from these
// (combat::resolveHits), so a server can judge them without
// drawing anything. Knocked-down bodies use their ragdoll's bones.
//
// Kept for players and Full-tier NPCs; others are dropped and
// rebuilt when they come close again.
struct BodyPose {
    Appearance        look;
    bool              built = false;
    std::unique_ptr<CharacterModel> model;
    CharacterAnimator anim;
    hitshape::BodyBoxes boxes;
    BoneMats          now{}, prev{};
    bool              hasNow = false, hasPrev = false;
    const HeldModelDef* held = nullptr;
    std::string       heldId;
    uint32_t          heldGen = 0;
    float             turnNow = 0.0f, turnPrev = 0.0f;   // the held item's thrust turn (Pose::heldTurn)
    // The last HISTORY ticks' skeletons (lag compensation: a remote
    // player's swing is checked against where bodies were on their
    // screen). By tick number, oldest first.
    std::deque<std::pair<uint32_t, BoneMats>> history;
};

class BodyPoses {
public:
    static constexpr int HISTORY = 30;             // ticks kept (0.5 s)
    uint32_t tick = 0;                             // advanced by endTick
    // Pose one body for this tick
    //   look     its appearance
    //   heldId   held model in the right hand ("" = none)
    //   combat   fighting stance
    // (also the ragdoll's target: the pose the body would have, so the
    // simulation drives its ragdoll, a server's with no renderer too)
    void update(Human& h, const Appearance& look, const std::string& heldId, bool combat, float dt) {
        BodyPose& p = poses[h.uuid];
        seen.insert(h.uuid);
        if (!p.built || !(p.look == look)) {
            p.look = look;
            p.model = std::make_unique<CharacterModel>(CharacterModel::build(look));
            p.boxes = hitshape::BodyBoxes::from(*p.model);
            p.built = true;
            p.hasNow = p.hasPrev = false;
        }
        if (heldId != p.heldId || p.heldGen != gHeldModels.generation()) {
            p.heldId = heldId;
            p.heldGen = gHeldModels.generation();
            p.held = heldId.empty() ? nullptr : gHeldModels.find(heldId);
            if (p.held && p.held->hitBoxes.empty()) p.held = nullptr;
        }
        const Pose& pose = p.anim.update(*p.model, animInputFor(h, combat, 0.0f), dt);
        p.prev = p.now;
        p.hasPrev = p.hasNow;
        p.turnPrev = p.turnNow;
        p.turnNow = pose.heldTurn;
        worldBoneMatrices(*p.model, pose, h.position, p.now);
        h.ragdoll.setModel(*p.model);
        h.ragdoll.setTarget(p.now);
        BoneMats rag;
        if (h.ragdoll.renderBones(1.0f, p.now, rag)) p.now = rag;     // knocked down: where it lies
        p.hasNow = true;
        p.history.push_back({ tick, p.now });
        while (p.history.size() > (size_t)HISTORY) p.history.pop_front();
    }

    // After a tick's updates: forget bodies that weren't posed
    void endTick() {
        for (auto it = poses.begin(); it != poses.end();)
            if (!seen.count(it->first)) it = poses.erase(it); else ++it;
        seen.clear();
        tick++;
    }

    // Body 'id' as it was 'ticksAgo' ticks back (at most HISTORY):
    // that tick's skeleton and the one before it. False if there's
    // no pose, or none that old (then use posed()).
    bool posedAgo(EntityId id, int ticksAgo, hitshape::Posed& out) const {
        const BodyPose* p = get(id);
        if (!p || ticksAgo <= 0 || p->history.size() < 2) return false;
        uint32_t want = tick - 1 - (uint32_t)std::min(ticksAgo, HISTORY - 1);
        size_t i = p->history.size();
        while (i > 1 && p->history[i - 1].first > want) i--;
        if (i < 2) i = 2;
        out.now = &p->history[i - 1].second;
        out.prev = &p->history[i - 2].second;
        out.body = &p->boxes;
        out.model = p->model.get();
        out.held = p->held;
        out.turnNow = out.turnPrev = p->turnNow;
        return true;
    }

    const BodyPose* get(EntityId id) const {
        auto it = poses.find(id);
        return it == poses.end() || !it->second.hasNow ? nullptr : &it->second;
    }

    // For the hit code
    bool posed(EntityId id, hitshape::Posed& out) const {
        const BodyPose* p = get(id);
        if (!p) return false;
        out.now = &p->now;
        out.prev = p->hasPrev ? &p->prev : nullptr;
        out.body = &p->boxes;
        out.model = p->model.get();
        out.held = p->held;
        out.turnNow = p->turnNow;
        out.turnPrev = p->hasPrev ? p->turnPrev : p->turnNow;
        return true;
    }

    size_t size() const { return poses.size(); }

private:
    std::unordered_map<EntityId, BodyPose> poses;
    std::unordered_set<EntityId> seen;
};
