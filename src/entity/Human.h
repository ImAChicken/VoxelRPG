#pragma once
#include "Entity.h"
#include "BodyMass.h"
#include "../physics/Ragdoll.h"
#include "../game/Combat.h"
#include <vector>
#include <algorithm>

// ══════════════════════════════════════════════════════════
// Human — a two-legged body: player, NPC, enemy
// ══════════════════════════════════════════════════════════
// All numbers are in voxels (1 voxel = 4 inches) and seconds.
// This file is the single place to tune how movement feels.

// ── What a human is trying to do this physics tick ────────
// Filled in by a controller: PlayerController reads the
// keyboard, an AI would write the same struct, and a server
// receives it over the network. Physics turns
// (state + intent) into the next state — nothing else goes
// in, which is what keeps it deterministic.
struct HumanIntent {
    // Desired direction of travel on the ground plane (x, z).
    // Zero = no movement input. Length is ignored.
    glm::vec2 move   = {0, 0};

    bool sprint = false;   // held
    bool crouch = false;   // held
    float speedScale = 1.0f;   // < 1: held back (swinging an axe)

    // One-shot requests. The controller sets them, physics
    // consumes them on the next tick, then they are cleared.
    bool jump  = false;    // ground: jump   air: double jump
    bool dodge = false;    // ground: roll (moving) or backstep
                           // (still)   air: aerial dodge

    // Fly mode (dev): no gravity, no collision
    bool      fly     = false;
    glm::vec3 flyMove = {0, 0, 0};   // full 3D direction
    bool      flyFast = false;

    // Combat (game/Combat.h): one-shots like jump, and the
    // guard, held
    bool attackLight = false;
    bool attackHeavy = false;
    bool guard       = false;

    void clearOneShots() { jump = false; dodge = false; attackLight = false; attackHeavy = false; }
};

class Human : public Entity {
public:
    // ── Body ──────────────────────────────────────────────
    static constexpr float RADIUS         = 1.5f;   // 6 in
    static constexpr float STAND_HEIGHT   = 17.0f;  // 5'8"
    static constexpr float CROUCH_HEIGHT  = 10.0f;  // 3'4"
    static constexpr float STAND_EYE      = 16.0f;  // default eye height, 5'4"
    static constexpr float CROUCH_EYE     = 8.0f;
    // The eye height in use (Settings: Eye height); crouching
    // lowers the eyes by the same amount whatever it is
    static inline float eyeStand = STAND_EYE;
    static float eyeCrouch() { return eyeStand - (STAND_EYE - CROUCH_EYE); }

    // ── Ground movement ───────────────────────────────────
    static constexpr float WALK_SPEED     = 30.0f;
    static constexpr float SPRINT_SPEED   = 50.0f;
    static constexpr float CROUCH_SPEED   = 15.0f;
    static constexpr float GROUND_ACCEL   = 900.0f; // how fast you reach speed / stop
    static constexpr float AIR_ACCEL      = 30.0f;  // steering while airborne
    static constexpr float STEP_UP        = 5.0f;   // climbed without jumping
    static constexpr float STEP_DOWN      = 2.0f;   // stay glued going down small steps

    // ── Gravity ───────────────────────────────────────────
    static constexpr float GRAVITY        = 40.0f;
    static constexpr float MAX_FALL_SPEED = 80.0f;

    // ── Jump ──────────────────────────────────────────────
    static constexpr float JUMP_HEIGHT        = 15.0f;
    static constexpr float DOUBLE_JUMP_HEIGHT = 20.0f;  // 35 total from the peak
    static constexpr float DOUBLE_JUMP_FREEZE = 0.15f;

    // ── Dodges ────────────────────────────────────────────
    // Each covers a fixed distance in a fixed time
    static constexpr float ROLL_DISTANCE      = 20.0f;
    static constexpr float ROLL_TIME          = 0.5f;
    static constexpr float BACKSTEP_DISTANCE  = 10.0f;
    static constexpr float BACKSTEP_TIME      = 0.3f;
    static constexpr float AIR_DODGE_DISTANCE = 12.0f;
    static constexpr float AIR_DODGE_TIME     = 0.15f;
    static constexpr float AIR_DODGE_FREEZE   = 0.12f;

    // ── Fly mode ──────────────────────────────────────────
    static constexpr float FLY_SPEED      = 22.0f;
    static constexpr float FLY_SPEED_FAST = 240.0f;   // 4x: crossing a big island

    // ── Health ────────────────────────────────────────────
    static constexpr float MAX_HEALTH = 100.0f;

    // ── State ─────────────────────────────────────────────
    float height    = STAND_HEIGHT;   // current collision height
    float eyeHeight = STAND_EYE;      // eases between stand and crouch

    // Health lives here rather than on Player because
    // enemies and NPCs are humans too. Nothing deals damage
    // yet; game modes that track health just display it.
    float health    = MAX_HEALTH;
    float maxHealth = MAX_HEALTH;

    bool grounded  = false;
    bool crouching = false;
    bool sprinting = false;
    bool flying    = false;

    // Air moves — each usable once, both refill on landing
    bool hasDoubleJump = true;
    bool hasAirDodge   = true;

    // The freeze: momentum is paused and stored, gravity is
    // off, and a short window opens (later: for an attack).
    enum class Freeze : uint8_t { None, DoubleJump, AirDodge };
    Freeze    freeze         = Freeze::None;
    float     freezeTimer    = 0.0f;
    glm::vec3 storedVelocity = {0, 0, 0};

    // A committed move in progress
    enum class Move : uint8_t { None, Roll, Backstep, AirDodge };
    Move      move      = Move::None;
    float     moveTimer = 0.0f;       // seconds left
    glm::vec3 moveDir   = {0, 0, 0};  // unit direction

    // ── Set by physics for whoever is watching ────────────
    // How far a step up/down just moved the body vertically.
    // The camera uses it to glide over steps instead of
    // snapping. Whoever reads it resets it to zero.
    float stepJolt = 0.0f;

    // Chunks under the body are not loaded yet — holding still
    bool waitingForWorld = false;

    // Fell below the bottom of the world — needs a respawn
    bool fellOutOfWorld = false;

    // ── Combat (game/Combat.h) ────────────────────────────
    // Any human fights the same way: the player, an enemy.
    //   weaponDamage  the weapon in hand (0: nothing to attack with)
    //   stamina       spent by attacks, dodges and blocking;
    //                 comes back after a short pause
    float       weaponDamage = 0.0f;
    float       stamina = combat::MAX_STAMINA, maxStamina = combat::MAX_STAMINA;
    float       staminaWait = 0.0f;
    bool        infiniteStamina = false;   // Test Play
    bool        guarding = false;          // set by physics from the intent
    bool        hasAirAttack = true;       // one per jump, back on landing
    AttackState attack;
    float       sinceHurt = 99.0f;         // s since last damaged (HUD)
    uint64_t    lastHitBy = 0;

    bool dead() const { return health <= 0.0f; }
    bool spend(float s) {                  // false if out of stamina
        if (infiniteStamina) return true;
        if (stamina <= 0.0f) return false;
        stamina = std::max(stamina - s, -20.0f);   // the last action may overdraw a little
        staminaWait = combat::STAMINA_DELAY;
        return true;
    }

    // ── Weight and the active ragdoll ─────────────────────
    // body     how heavy each part is, flesh plus gear
    //          (entity/BodyMass.h); 'mass' (Entity) is its total
    // ragdoll  the physical body drawn over the animation when
    //          hit (physics/Ragdoll.h); while it is limp or
    //          getting up, the body cannot be steered
    // hits     waiting to be applied by physics on its next tick
    //          (IPhysics::stepRagdoll), so anything — a command,
    //          an explosion, later a sword — just queues one
    struct Hit {
        glm::vec3 point;      // where it lands, world voxels
        glm::vec3 impulse;    // kg·voxels/s
    };
    BodyMass         body;
    Ragdoll          ragdoll;
    std::vector<Hit> hits;

    // A shove of 'speed' voxels/s on an average (70 kg) body;
    // heavier bodies are moved less, lighter ones more
    static constexpr float REFERENCE_MASS = 70.0f;

    void hit(glm::vec3 point, glm::vec3 impulse) { hits.push_back({ point, impulse }); }

    // A point on the body by height: 0 feet, 1 top of the head
    glm::vec3 bodyPoint(float frac) const {
        return position + glm::vec3(0.0f, height * frac, 0.0f);
    }

    void setBodyMass(float bodyKg) {
        body.setBody(bodyKg);
        mass = body.total();
    }

    glm::vec3 eyePosition() const {
        return position + glm::vec3(0.0f, eyeHeight, 0.0f);
    }

    // Stop dead and clear every move in progress
    // (respawn, teleport, switching game mode)
    void resetMotion() {
        velocity       = {0, 0, 0};
        storedVelocity = {0, 0, 0};
        freeze         = Freeze::None;
        freezeTimer    = 0.0f;
        move           = Move::None;
        moveTimer      = 0.0f;
        hasDoubleJump  = true;
        hasAirDodge    = true;
        grounded       = false;
        sprinting      = false;
        stepJolt       = 0.0f;
        fellOutOfWorld = false;
        hits.clear();
        ragdoll.reset();
        attack = AttackState();
        guarding = false;
    }
};
