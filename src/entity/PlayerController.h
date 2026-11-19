#pragma once
#include "Player.h"
#include "../input/InputManager.h"
#include "../render/Camera.h"
#include "../physics/IPhysics.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// ══════════════════════════════════════════════════════════
// PlayerController — keyboard and mouse → HumanIntent
// ══════════════════════════════════════════════════════════
// The only place that knows about input. It never moves the
// player itself: it fills in 'intent', physics does the rest.
// An AI or a network connection would fill the same struct.
//
// What the buttons MEAN depends on the player's game mode
// and stance (see game/GameMode.h): the same left click
// places a block in Survival and swings a weapon in Combat.
//
// Each frame, in this order:
//   readInput()     once
//   beforeTick() → physics → afterTick()   0 or more times
//   updateCamera()  once

// What the player just asked to do with the combat buttons.
// STUB: combat actions do nothing yet — the HUD shows the
// last one. (World interaction — break / place — is real and
// lives in game/PlayerInteraction.h.)
enum class PlayerAction : uint8_t {
    None,
    // Survival stance — world interactions
    UseItem,        // left click:  use / place
    RemoveBlock,    // right click: remove
    Interact,       // E: use / open / talk
    // Combat stance — combat actions
    Attack,         // left click
    HeavyAttack,    // right click
    Guard,          // Q
};

inline const char* playerActionName(PlayerAction a) {
    switch (a) {
    case PlayerAction::UseItem:     return "use / place";
    case PlayerAction::RemoveBlock: return "remove";
    case PlayerAction::Interact:    return "interact";
    case PlayerAction::Attack:      return "attack";
    case PlayerAction::HeavyAttack: return "heavy attack";
    case PlayerAction::Guard:       return "guard";
    default:                        return "";
    }
}

class PlayerController {
public:
    // Let go of the roll button within this long and it is a
    // tap (roll / backstep). Keep holding and it is a sprint.
    static constexpr float TAP_WINDOW = 0.2f;

    // Third-person camera: how far behind the eyes it sits,
    // how far above them it pivots, and how close it may be
    // pushed by a wall
    static constexpr float THIRD_PERSON_DISTANCE = 28.0f;
    static constexpr float THIRD_PERSON_RAISE    = 6.0f;
    static constexpr float THIRD_PERSON_MIN      = 2.0f;

    HumanIntent intent;
    bool        flyMode = false;   // V toggles

    // The player's own camera choice (F5). Combat overrides
    // it — see usingThirdPerson().
    bool thirdPersonWanted = false;

    // Most recent action button, and how long ago (seconds)
    PlayerAction lastAction    = PlayerAction::None;
    float        lastActionAge = 0.0f;

    // True while the guard button is held in combat
    bool guarding = false;

    // Set by the game while a prompt is using the number
    // keys (the copy tool's "corner 1 or 2?")
    bool slotKeysBlocked = false;

    // Where to draw the body this frame (feet centre),
    // blended between physics ticks. Set by updateCamera.
    glm::vec3 renderPosition = {0, 0, 0};

    // Is the camera behind the player right now?
    bool usingThirdPerson(const GameModeRules& rules) const {
        return rules.forceThirdPerson || thirdPersonWanted;
    }

    // Is the camera far enough back to see the body?
    bool bodyVisible() const { return cameraBack > 5.0f; }

    // ── Read this frame's input ───────────────────────────
    void readInput(Player& player, const Camera& camera,
                   float frameDt) {
        const GameModeRules rules = player.rules();

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

        // the controller's left stick: a gentle tilt walks slower
        float tilt = 1.0f;
        glm::vec2 stick = gInput.padMove();
        if (glm::length(stick) > 0.0f && ahead == 0.0f && side == 0.0f) {
            ahead = stick.y; side = stick.x;
            tilt = std::clamp(glm::length(stick), 0.35f, 1.0f);
        }

        intent.move   = forward * ahead + right * side;
        // L3 (crouch) is a toggle on the controller; the key is held
        bool l3 = gInput.padConnected() && gInput.padNow().down[pad::L3];
        if (l3 && !padCrouchWas) padCrouch = !padCrouch;
        padCrouchWas = l3;
        if (!gInput.padConnected()) padCrouch = false;
        intent.crouch = gInput.isHeld(InputAction::Crouch) || padCrouch;
        // Swinging an axe: a slow walk (game/AxeSwing.h)
        intent.speedScale = (player.swing.active() ? AxeSwing::MOVE_FACTOR : 1.0f) * tilt;

        // One-shots stay set until a physics tick has used
        // them (afterTick clears), so a press is never lost
        // on a frame that happens to run no tick
        if (gInput.isPressed(InputAction::Jump))
            intent.jump = true;

        readRollButton(player, frameDt);

        readModeButtons(player, rules, frameDt);

        // ── Fly mode (V) — only where the mode allows ─────
        if (!rules.canFly)
            flyMode = false;
        else if (gInput.isPressed(InputAction::ToggleFly))
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
        // a server correction: the same, in all three directions
        netOffset          += player.renderOffset;
        player.renderOffset = glm::vec3(0.0f);

        // A teleport (respawn, popped out of the ground):
        // do not glide the camera across the gap
        if (glm::length(player.position - prevPosition) > 8.0f)
            snap(player);
    }

    // ── Place the camera at the player's eyes ─────────────
    // alpha = how far we are between the last two physics
    // ticks (0..1). Physics runs at 60 Hz; blending between
    // ticks keeps motion smooth at any frame rate.
    // physics is used to stop the third-person camera going
    // through walls; pass nullptr to skip that.
    void updateCamera(const Player& player, Camera& camera,
                      float alpha, float frameDt,
                      const IPhysics* physics) {
        // Ease the step offset back to zero
        stepOffset *= std::exp(-14.0f * frameDt);
        if (std::fabs(stepOffset) < 0.01f) stepOffset = 0.0f;
        netOffset *= std::exp(-20.0f * frameDt);             // about a tenth of a second
        if (glm::length(netOffset) < 0.01f) netOffset = glm::vec3(0.0f);

        glm::vec3 pos = glm::mix(prevPosition,
                                 player.position, alpha) + netOffset;
        float eye = glm::mix(prevEye, player.eyeHeight, alpha);

        renderPosition = pos;

        // The ragdoll moves the head when the body is hit or
        // knocked down; both cameras follow it, so a shove
        // jolts the view and a knocked-down body stays in frame
        glm::vec3 eyes = pos +
            glm::vec3(0.0f, eye + stepOffset, 0.0f) +
            player.ragdoll.headOffset(alpha);

        // ── First person: camera at the eyes ──────────────
        if (!usingThirdPerson(player.rules())) {
            camera.position = eyes;
            cameraBack      = 0.0f;
            return;
        }

        // ── Third person: behind and slightly above ───────
        // The camera looks the same way as before; it just
        // backs away along its own line of sight.
        // The pivot sits a little above the head, so the
        // body is framed below the middle of the screen —
        // but never up inside a low ceiling
        float raise = THIRD_PERSON_RAISE;
        if (physics) {
            RaycastResult roof = physics->raycast(
                eyes, glm::vec3(0.0f, 1.0f, 0.0f), raise);
            if (roof.hit)
                raise = std::max(0.0f, roof.distance - 0.5f);
        }
        glm::vec3 pivot = eyes + glm::vec3(0.0f, raise, 0.0f);
        float back = THIRD_PERSON_DISTANCE;

        // Something solid between the player and the camera?
        // Pull the camera in front of it.
        if (physics) {
            RaycastResult hit = physics->raycast(
                pivot, -camera.front, THIRD_PERSON_DISTANCE);
            if (hit.hit)
                back = std::max(THIRD_PERSON_MIN,
                                hit.distance - 1.0f);
        }

        // Ease outwards, snap inwards: a wall must never be
        // between you and the camera, even for one frame
        if (back < cameraBack) cameraBack = back;
        else cameraBack += (back - cameraBack) *
                 (1.0f - std::exp(-10.0f * frameDt));

        camera.position = pivot - camera.front * cameraBack;
    }

    // Forget any in-between state — call after moving the
    // player by hand (mode switch, respawn, new world)
    void snap(const Player& player) {
        prevPosition = player.position;
        prevEye      = player.eyeHeight;
        stepOffset   = 0.0f;
        netOffset    = glm::vec3(0.0f);
    }

    void reset(const Player& player) {
        intent    = HumanIntent();
        flyMode   = false;
        guarding  = false;
        lastAction     = PlayerAction::None;
        lastActionAge  = 0.0f;
        cameraBack     = 0.0f;
        renderPosition = player.position;
        rollArmed = false;
        rollHeld  = 0.0f;
        snap(player);
    }

private:
    glm::vec3 prevPosition = {0, 0, 0};
    glm::vec3 netOffset    = {0, 0, 0};    // eases a server's correction (Human::renderOffset)
    float     prevEye      = Human::eyeStand;
    float     stepOffset   = 0.0f;
    float     cameraBack   = 0.0f;   // current third-person distance

    bool  padCrouch = false, padCrouchWas = false;   // L3 toggles sneaking
    bool  rollArmed = false;   // a press is waiting to become a tap
    float rollHeld  = 0.0f;    // how long the button has been down

    void note(PlayerAction a) {
        lastAction    = a;
        lastActionAge = 0.0f;
    }

    // ── Buttons whose meaning depends on mode and stance ──
    void readModeButtons(Player& player,
                         const GameModeRules& rules,
                         float frameDt) {
        lastActionAge += frameDt;

        // R — draw / sheathe the weapon. Only Survival has
        // a combat stance, so the button does nothing
        // elsewhere.
        if (player.mode == GameMode::Survival &&
            gInput.isPressed(InputAction::DrawWeapon)) {
            player.combatStance = !player.combatStance;
            if (player.combatStance) player.kit.tidy(player.inventory);   // the loadouts, up to date
        }

        // F5 — first / third person. Remembered while combat
        // forces third person, and restored on sheathing.
        if (rules.hasBody &&
            gInput.isPressed(InputAction::CameraToggle))
            thirdPersonWanted = !thirdPersonWanted;

        // Re-read: R may have just changed the stance
        const GameModeRules now = player.rules();

        // ── Combat stance: combat actions ─────────────────
        // Attacks are one-shots in the intent (kept until a
        // physics tick uses them); the guard is held
        guarding = false;
        // Start held (Left Alt): the 8 quick options (Survival: the
        // kit is the player's own), drawn or not
        const bool kitQuick = now.hasBody && player.mode == GameMode::Survival &&
                              gInput.isHeld(InputAction::QuickLayer);
        if (kitQuick && !now.combatActions) quickButtons(player);   // so the slotted spells don't need combat mode
        if (now.combatActions) {
            if (gInput.isPressed(InputAction::UseLeft)) {
                note(PlayerAction::Attack);
                intent.attackLight = true;
            }
            if (gInput.isPressed(InputAction::UseRight)) {
                note(PlayerAction::HeavyAttack);
                intent.attackHeavy = true;
            }
            if (kitQuick) quickButtons(player);
            else combatKitButtons(player);
        }
        intent.guard = guarding;

        // ── Toolbar — not while a weapon is drawn ─────────
        // Survival scrolls the player inventory, Test Play
        // the dev inventory (a weapon held in the toolbar can
        // be switched away from)
        if (now.hasBody && !player.combatStance) {
            Inventory& inv = player.activeInventory();
            static const InputAction slots[] = {
                InputAction::ToolbarSlot1, InputAction::ToolbarSlot2,
                InputAction::ToolbarSlot3, InputAction::ToolbarSlot4,
                InputAction::ToolbarSlot5, InputAction::ToolbarSlot6,
                InputAction::ToolbarSlot7, InputAction::ToolbarSlot8,
                InputAction::ToolbarSlot9, InputAction::ToolbarSlot10,
            };
            // 1 and 2 put the off-hand item in the left hand (or
            // take it out); 3..0 and the wheel pick the right hand
            for (int i = 0; i < Inventory::HOTBAR && !slotKeysBlocked && !kitQuick; i++)   // (held: the slot keys pick quick options)
                if (gInput.isPressed(slots[i]))
                    inv.pressHotbarKey(i);
            if (gInput.isPressed(InputAction::ToolbarNext))
                inv.selectNext();
            if (gInput.isPressed(InputAction::ToolbarPrev))
                inv.selectPrev();
        }
    }

    // ── The combat layout (game/CombatKit.h) ──────────────
    // LT (key Q): the special — guard, a heavy, or a cast — from
    // the off-hand item or the weapon. X / Y / B (1 / 2 / 3) use
    // the item up in that slot; the D-pad (4 / 5 / 6) cycles it.
    // Start held (Left Alt): the 8 quick options instead.
    void useKitItem(uint16_t item) {
        if (!item) return;
        const ItemDef& d = gItems.get(item);
        if (!d.cast.empty()) { intent.useItem = item; note(PlayerAction::UseItem); }
    }
    // The quick layer: the slot buttons (D-pad, X / Y / B / A; keys
    // 1..8) use the 8 quick options. In combat or out.
    void quickButtons(Player& player) {
        for (int i = 0; i < CombatKit::QUICK; i++)
            if (gInput.isPressed((InputAction)((int)InputAction::ToolbarSlot1 + i))) useKitItem(player.kit.quick[i]);
    }
    void combatKitButtons(Player& player) {
        // A weapon held in the toolbar (not drawn with R): Q guards,
        // the number keys stay the hotbar's
        if (!player.combatStance) {
            if (gInput.isPressed(InputAction::Block)) note(PlayerAction::Guard);
            guarding = gInput.isHeld(InputAction::Block);
            return;
        }
        CombatKit& kit = player.kit;
        kit.follow(player.inventory);                    // loadout 1 / 2: the drawn weapon
        uint16_t castItem = 0;
        CombatKit::Special sp = kit.special(player.inventory.weapon().itemID, &castItem);
        if (gInput.isPressed(InputAction::Block)) {
            if (sp == CombatKit::Special::Heavy) { intent.attackHeavy = true; note(PlayerAction::HeavyAttack); }
            else if (sp == CombatKit::Special::Cast) useKitItem(castItem);
            else note(PlayerAction::Guard);
        }
        guarding = sp == CombatKit::Special::Guard && gInput.isHeld(InputAction::Block);
        const InputAction useKeys[3] = { InputAction::UseSlotX, InputAction::UseSlotY, InputAction::UseSlotB };
        const InputAction cycleKeys[3] = { InputAction::CycleSlotX, InputAction::CycleSlotY, InputAction::CycleSlotB };
        for (int i = 0; i < 3; i++) {
            if (gInput.isPressed(useKeys[i])) useKitItem(kit.slotItem(i));
            if (gInput.isPressed(cycleKeys[i])) kit.cycleSlot(i);
        }
    }

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
