#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// Player state — plain data struct
// No methods that touch rendering or physics directly
// Safe to serialize for network transmission
// Physics reads and writes this each frame

struct Player {

    // ── Position + movement ─────────────────────────────
    glm::vec3 position  = {256.0f, 60.0f, 256.0f};
    glm::vec3 velocity  = {0, 0, 0};
    glm::vec3 facing    = {0, 0, -1};  // normalized look dir

    // ── Capsule dimensions (in voxels) ──────────────────
    // At 4 inches per voxel:
    //   height 17 = 5'8"   radius 1.5 = 6 inches
    float capsuleHeight = 17.0f;
    float capsuleRadius = 1.5f;

    // ── Movement constants (voxels/sec) ─────────────────
    static constexpr float WALK_SPEED        =  5.0f;
    static constexpr float RUN_SPEED         = 11.0f;
    static constexpr float SPRINT_SPEED      = 15.0f;
    static constexpr float CROUCH_SPEED      =  3.0f;
    static constexpr float FLY_SPEED         = 22.0f;
    static constexpr float FLY_SPEED_FAST    = 60.0f;

    // ── Jump constants (voxels) ─────────────────────────
    static constexpr float JUMP_HEIGHT          = 15.0f;
    static constexpr float DOUBLE_JUMP_BOOST    = 20.0f;
    static constexpr float AERIAL_DODGE_BURST   = 12.0f;

    // ── Freeze durations (seconds) ──────────────────────
    static constexpr float DOUBLE_JUMP_FREEZE   = 0.15f;
    static constexpr float AERIAL_DODGE_FREEZE  = 0.12f;

    // ── State flags ─────────────────────────────────────
    bool isGrounded     = false;
    bool isCrouching    = false;
    bool isSprinting    = false;
    bool isFlying       = false;    // dev fly mode
    bool isInCombat     = false;    // weapon drawn

    // ── Air mechanics ────────────────────────────────────
    bool  hasDoubleJump  = true;   // resets on land
    bool  hasAerialDodge = true;   // resets on land
    int   jumpCount      = 0;      // 0 = not jumped

    // ── Freeze system ────────────────────────────────────
    enum class FreezeType : uint8_t {
        None        = 0,
        DoubleJump  = 1,
        AerialDodge = 2,
    };

    FreezeType freezeType      = FreezeType::None;
    float      freezeTimer     = 0.0f;
    glm::vec3  storedVelocity  = {0, 0, 0};
    glm::vec3  dodgeBurstDirection = {0, 1, 0};

    // ── Camera ───────────────────────────────────────────
    float yaw   = -90.0f;
    float pitch =   0.0f;

    // ── Eye height offset from position.y ───────────────
    // Standing: 15 voxels   Crouching: 8 voxels
    float getEyeHeight() const {
        return isCrouching ? 8.0f : 15.0f;
    }

    glm::vec3 getEyePosition() const {
        return position +
               glm::vec3(0, getEyeHeight(), 0);
    }

    // ── Crouch height ────────────────────────────────────
    float getCurrentHeight() const {
        return isCrouching ? 10.0f : capsuleHeight;
    }

    // ── Serialization size (for netcode) ─────────────────
    // Position(12) + Velocity(12) + Facing(12)
    // + Flags(1) + Yaw(4) + Pitch(4) = ~45 bytes
    // Kept small intentionally
};
