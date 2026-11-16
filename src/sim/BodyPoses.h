#pragma once
#include "../character/HumanAnim.h"
#include "../character/HeldItemModel.h"
#include "../entity/Player.h"
#include "../npc/Npc.h"
#include "../game/HitShapes.h"
#include "../physics/Ragdoll.h"
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
};

class BodyPoses {
public:
    // Pose one body for this tick
    //   look     its appearance
    //   heldId   held model in the right hand ("" = none)
    //   combat   fighting stance
    void update(const Human& h, const Appearance& look, const std::string& heldId, bool combat, float dt) {
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
        worldBoneMatrices(*p.model, pose, h.position, p.now);
        BoneMats rag;
        if (h.ragdoll.renderBones(1.0f, p.now, rag)) p.now = rag;     // knocked down: where it lies
        p.hasNow = true;
    }

    // After a tick's updates: forget bodies that weren't posed
    void endTick() {
        for (auto it = poses.begin(); it != poses.end();)
            if (!seen.count(it->first)) it = poses.erase(it); else ++it;
        seen.clear();
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
        return true;
    }

    size_t size() const { return poses.size(); }

private:
    std::unordered_map<EntityId, BodyPose> poses;
    std::unordered_set<EntityId> seen;
};
