#pragma once
#include "Player.h"
#include "../input/InputManager.h"
#include "../render/Camera.h"
#include <cmath>
#include <glm/glm.hpp>

// ══════════════════════════════════════════════════════════
// PlayerController — keyboard and mouse → HumanIntent
// ══════════════════════════════════════════════════════════
// The only place that knows about input. It never moves the
// player itself: it fills in 'intent', physics does the rest.
// An AI or a network connection would fill the same struct.
//
// Each frame, in this order:
//   readInput()     once
//   beforeTick() → physics → afterTick()   0 or more times
//   updateCamera()  once
class PlayerController {
public:
    // Let go of the roll button within this long and it is a
    // tap (roll / backstep). Keep holding and it is a sprint.
    static constexpr float TAP_WINDOW = 0.2f;

    HumanIntent intent;
    bool        flyMode = false;   // V toggles

    // ── Read this frame's input ───────────────────────────
    void readInput(Player& player, const Camera& camera,
                   float frameDt) {
        // Directions on the ground plane, from where the
        // camera is looking (pitch ignored)
        float yaw = glm::radians(camera.yaw);
        glm::vec2 forward(std::cos(yaw), std::sin(yaw));
        glm::vec2 right(-forward.y, forward.x);
        player.facing = glm::vec3(forward.x, 0.0f, forward.y);

        float ahead = 0.0f, side = 0.0f;
        if (gInput.isHeld(InputAction::MoveForward))  ahead += 1.0f;
        if (gInput.isHeld(InputAction::MoveBackward)) ahead -= 1.0f;
        if (gInput.isHeld(InputAction::StrafeRight))  side  += 1.0f;
        if (gInput.isHeld(InputAction::StrafeLeft))   side  -= 1.0f;

        intent.move   = forward * ahead + right * side;
        intent.crouch = gInput.isHeld(InputAction::Crouch);

        // One-shots stay set until a physics tick has used
        // them (afterTick clears), so a press is never lost
        // on a frame that happens to run no tick
        if (gInput.isPressed(InputAction::Jump))
            intent.jump = true;

        readRollButton(player, frameDt);

        // ── Fly mode (V) ──────────────────────────────────
        if (gInput.isPressed(InputAction::ToggleFly))
            flyMode = !flyMode;

        intent.fly = flyMode;
        if (flyMode) {
            glm::vec3 camRight = glm::normalize(
                glm::cross(camera.front, camera.up));
            float lift = 0.0f;
            if (gInput.isHeld(InputAction::Jump))   lift += 1.0f;
            if (gInput.isHeld(InputAction::Crouch)) lift -= 1.0f;
            intent.flyMove = camera.front * ahead
                           + camRight     * side
                           + camera.up    * lift;
            intent.flyFast = gInput.isHeld(InputAction::Sprint);
            intent.clearOneShots();   // no jumps or rolls in flight
        }
    }

    // ── Around each physics tick ──────────────────────────
    void beforeTick(const Player& player) {
        prevPosition = player.position;
        prevEye      = player.eyeHeight;
    }

    void afterTick(Player& player) {
        intent.clearOneShots();

        // The body just stepped up or down a ledge. Shift
        // the camera the opposite way by the same amount so
        // the view does not jump, then let it ease back.
        stepOffset     -= player.stepJolt;
        player.stepJolt = 0.0f;

        // A teleport (respawn, popped out of the ground):
        // do not glide the camera across the gap
        if (glm::length(player.position - prevPosition) > 8.0f)
            snap(player);
    }

    // ── Place the camera at the player's eyes ─────────────
    // alpha = how far we are between the last two physics
    // ticks (0..1). Physics runs at 60 Hz; blending between
    // ticks keeps motion smooth at any frame rate.
    void updateCamera(const Player& player, Camera& camera,
                      float alpha, float frameDt) {
        // Ease the step offset back to zero
        stepOffset *= std::exp(-14.0f * frameDt);
        if (std::fabs(stepOffset) < 0.01f) stepOffset = 0.0f;

        glm::vec3 pos = glm::mix(prevPosition,
                                 player.position, alpha);
        float eye = glm::mix(prevEye, player.eyeHeight, alpha);

        camera.position = pos +
            glm::vec3(0.0f, eye + stepOffset, 0.0f);
    }

    // Forget any in-between state — call after moving the
    // player by hand (mode switch, respawn, new world)
    void snap(const Player& player) {
        prevPosition = player.position;
        prevEye      = player.eyeHeight;
        stepOffset   = 0.0f;
    }

    void reset(const Player& player) {
        intent    = HumanIntent();
        flyMode   = false;
        rollArmed = false;
        rollHeld  = 0.0f;
        snap(player);
    }

private:
    glm::vec3 prevPosition = {0, 0, 0};
    float     prevEye      = Human::STAND_EYE;
    float     stepOffset   = 0.0f;

    bool  rollArmed = false;   // a press is waiting to become a tap
    float rollHeld  = 0.0f;    // how long the button has been down

    // One button, three jobs:
    //   in the air, press          → aerial dodge (at once)
    //   on the ground, tap         → roll if moving,
    //                                backstep if standing still
    //   on the ground, hold        → sprint
    // A tap can only be told from a hold when the button
    // comes back up, so rolls fire on release.
    void readRollButton(const Player& player, float frameDt) {
        if (gInput.isPressed(InputAction::Roll)) {
            rollHeld  = 0.0f;
            rollArmed = true;
            if (!player.grounded) {
                intent.dodge = true;
                rollArmed    = false;
            }
        }

        if (gInput.isHeld(InputAction::Roll))
            rollHeld += frameDt;

        if (gInput.isReleased(InputAction::Roll)) {
            if (rollArmed && rollHeld <= TAP_WINDOW)
                intent.dodge = true;
            rollArmed = false;
        }

        // Sprint once the hold has outlasted the tap window.
        // (If Sprint is rebound to its own key it works
        // immediately, because Roll is then not held.)
        intent.sprint =
            gInput.isHeld(InputAction::Sprint) &&
            (!gInput.isHeld(InputAction::Roll) ||
             rollHeld > TAP_WINDOW);
    }
};
