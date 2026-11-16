#pragma once
#include "CharacterAnimator.h"
#include "../entity/Human.h"
#include <cmath>

// ══════════════════════════════════════════════════════════
// What any human is doing, as the animator's input
// ══════════════════════════════════════════════════════════
// One function for the renderer (main.cpp: what is drawn) and the
// simulation (sim/BodyPoses.h: where blades and limbs are for
// hits), so both pose a body from exactly the same state.
//   combat   the fighting stance (a drawn or held weapon; NPCs)
//   pitch    the camera's pitch, for the head (0 for NPCs and in
//            the simulation)
inline AnimInput animInputFor(const Human& h, bool combat, float pitch) {
    AnimInput in;
    in.velocity     = h.velocity;
    glm::vec3 f     = h.attack.active() ? h.attack.dir : h.facing;   // an attack faces its swing
    in.facingYawDeg = glm::degrees(std::atan2(f.z, f.x));
    in.lookPitchDeg = pitch;
    in.grounded     = h.grounded;
    in.crouching    = h.crouching;
    in.sprinting    = h.sprinting;
    in.flying       = h.flying;
    in.combat       = combat;
    in.frozen       = h.freeze != Human::Freeze::None;
    in.walkSpeed    = Human::WALK_SPEED;
    in.sprintSpeed  = Human::SPRINT_SPEED;
    in.attack       = h.attack.active() ? h.attack.d : nullptr;
    in.attackT      = h.attack.t;
    in.guard        = h.guarding;
    switch (h.move) {
    case Human::Move::Roll:
        in.move = AnimInput::Move::Roll;
        in.moveProgress = 1.0f - h.moveTimer / Human::ROLL_TIME; break;
    case Human::Move::Backstep:
        in.move = AnimInput::Move::Backstep;
        in.moveProgress = 1.0f - h.moveTimer / Human::BACKSTEP_TIME; break;
    case Human::Move::AirDodge:
        in.move = AnimInput::Move::AirDodge;
        in.moveProgress = 1.0f - h.moveTimer / Human::AIR_DODGE_TIME; break;
    default: break;
    }
    return in;
}
