#pragma once
#include "CharacterAnimator.h"
#include "AnimClips.h"
#include "../entity/Human.h"
#include "../game/AxeSwing.h"
#include "../game/Combat.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// AnimPreview — what the animation viewer can play
// ══════════════════════════════════════════════════════════
// The Character screen's Animations tab plays every animation on
// the character being edited:
//   Game states   what the game itself shows for a state (walk,
//                 sprint, roll, a sword attack, the axe swing …),
//                 through the same animator the game uses — so it
//                 shows clips where anim_map.toml maps one, and
//                 the procedural animation where it doesn't
//   <category>    every clip in data/animations, as it is
namespace animpreview {

struct State { const char* name; float duration; };

inline const std::vector<State>& states() {
    static const std::vector<State> S = {
        { "Idle", 4.0f }, { "Walk - slow", 2.0f }, { "Walk", 2.0f }, { "Sprint", 2.0f },
        { "Crouch", 3.0f }, { "Crouch walk", 2.0f }, { "Jump - rising", 1.5f }, { "Falling", 1.5f },
        { "Roll", Human::ROLL_TIME }, { "Backstep", Human::BACKSTEP_TIME }, { "Air dodge", Human::AIR_DODGE_TIME },
        { "Combat stance", 3.0f }, { "Guard", 3.0f },
        { "Sword light 1", 0.0f }, { "Sword light 2", 0.0f }, { "Sword heavy", 0.0f },
        { "Sword air light", 0.0f }, { "Sword plunge", 0.0f }, { "Axe swing", AxeSwing::DURATION },
    };
    return S;
}
inline const AttackDef* stateAttack(int i) {
    static const char* IDS[] = { "sword_light1", "sword_light2", "sword_heavy", "sword_air_light", "sword_plunge" };
    if (i < 13 || i > 17) return nullptr;
    return gMovesets.attack(IDS[i - 13]);
}
inline float stateDuration(int i) {
    if (const AttackDef* a = stateAttack(i)) return a->duration;
    return i >= 0 && i < (int)states().size() ? states()[i].duration : 1.0f;
}

// The animator's input for game state i, t seconds in (the body
// stands at the origin facing +Z, toward the viewer's camera)
inline AnimInput stateInput(int i, float t) {
    AnimInput in;
    in.facingYawDeg = 90.0f;
    in.grounded = true;
    in.walkSpeed = Human::WALK_SPEED;
    in.sprintSpeed = Human::SPRINT_SPEED;
    glm::vec3 fwd(0, 0, 1);
    float d = stateDuration(i);
    float u = d > 0.0f ? std::fmod(t, d) / d : 0.0f;
    switch (i) {
    case 1: in.velocity = fwd * (Human::WALK_SPEED * 0.4f); break;
    case 2: in.velocity = fwd * Human::WALK_SPEED; break;
    case 3: in.velocity = fwd * Human::SPRINT_SPEED; in.sprinting = true; break;
    case 4: in.crouching = true; break;
    case 5: in.crouching = true; in.velocity = fwd * Human::CROUCH_SPEED; break;
    case 6: in.grounded = false; in.velocity = glm::vec3(0, 20, 0); break;
    case 7: in.grounded = false; in.velocity = glm::vec3(0, -20, 0); break;
    case 8: in.move = AnimInput::Move::Roll; in.moveProgress = u; break;
    case 9: in.move = AnimInput::Move::Backstep; in.moveProgress = u; break;
    case 10: in.grounded = false; in.move = AnimInput::Move::AirDodge; in.moveProgress = u; break;
    case 11: in.combat = true; break;
    case 12: in.combat = true; in.guard = true; break;
    case 18: in.swing = u * AxeSwing::DURATION; in.carryAxe = true; break;
    default: break;
    }
    if (const AttackDef* a = stateAttack(i)) {
        in.combat = true;
        in.attack = a;
        in.attackT = u * a->duration;
        in.grounded = i != 16 && i != 17;
    }
    return in;
}

} // namespace animpreview
