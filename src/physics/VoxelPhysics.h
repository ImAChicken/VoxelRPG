#pragma once
#include "IPhysics.h"
#include "../chunk/ChunkManager.h"
#include "../player/Player.h"
#include <iostream>
#include <cmath>

// Voxel-based physics implementation
// Custom capsule collision against voxel world
// Deterministic — same inputs = same outputs
// Safe to run on both client (prediction) and server (authority)

class VoxelPhysics : public IPhysics {
public:

    bool init(ChunkManager& world) override {
        this->world = &world;
        std::cout << "VoxelPhysics initialized\n";
        return true;
    }

    void shutdown() override {}

    void setWorld(ChunkManager& world) override {
        this->world = &world;
    }

    void movePlayer(Player& player, float dt) override {
        if (!world) return;

        // Apply gravity if not grounded and not flying
        if (!player.isFlying && !player.isGrounded) {
            player.velocity.y -= GRAVITY * dt;
            // Terminal velocity
            if (player.velocity.y < -MAX_FALL_SPEED)
                player.velocity.y = -MAX_FALL_SPEED;
        }

        // Handle freeze states (double jump / aerial dodge)
        if (player.freezeTimer > 0.0f) {
            player.freezeTimer -= dt;
            if (player.freezeTimer <= 0.0f) {
                player.freezeTimer = 0.0f;
                applyFreezeRelease(player);
            }
            return; // no movement during freeze
        }

        // Move and collide
        glm::vec3 delta = player.velocity * dt;
        resolveCollision(player, delta);

        // Step up
        if (!player.isFlying)
            tryStepUp(player, dt);

        // Ground check
        player.isGrounded = checkGrounded(player);
        if (player.isGrounded) {
            if (player.velocity.y < 0.0f)
                player.velocity.y = 0.0f;
            // Reset air mechanics on landing
            player.hasDoubleJump  = true;
            player.hasAerialDodge = true;
        }
    }

    bool isGrounded(const Player& player) const override {
        return checkGrounded(player);
    }

    bool isOnWall(const Player& player) const override {
        // TODO J1 — ledge grab phase
        (void)player;
        return false;
    }

    RaycastResult raycast(glm::vec3 origin,
                          glm::vec3 direction,
                          float maxDist) const override {
        RaycastResult result;
        if (!world) return result;

        // DDA raycast through voxel grid
        glm::vec3 pos = origin;
        glm::vec3 dir = glm::normalize(direction);

        float dist = 0.0f;
        float step = 0.1f; // voxel fraction

        while (dist < maxDist) {
            pos  += dir * step;
            dist += step;

            int bx = (int)std::floor(pos.x);
            int by = (int)std::floor(pos.y);
            int bz = (int)std::floor(pos.z);

            // Convert world voxel to chunk + local
            int cx = bx / VOXEL_DIM;
            int lx = bx % VOXEL_DIM;
            int cz = bz / VOXEL_DIM;
            int lz = bz % VOXEL_DIM;

            if (lx < 0) { cx--; lx += VOXEL_DIM; }
            if (lz < 0) { cz--; lz += VOXEL_DIM; }

            uint16_t id = world->typeAtWorld(
                cx, cz, lx, by, lz);

            if (id != BLOCK_AIR) {
                result.hit      = true;
                result.position = pos;
                result.blockPos = {bx, by, bz};
                result.distance = dist;
                result.blockID  = id;

                // Approximate face normal
                glm::vec3 prev = pos - dir * step;
                glm::vec3 diff = pos - prev;
                if (std::abs(diff.x) > std::abs(diff.y) &&
                    std::abs(diff.x) > std::abs(diff.z))
                    result.normal = {diff.x > 0 ? -1.f : 1.f,
                                     0, 0};
                else if (std::abs(diff.y) > std::abs(diff.z))
                    result.normal = {0,
                                     diff.y > 0 ? -1.f : 1.f,
                                     0};
                else
                    result.normal = {0, 0,
                                     diff.z > 0 ? -1.f : 1.f};
                return result;
            }
        }

        return result;
    }

    bool capsuleIsClear(glm::vec3 position,
                        float radius,
                        float height) const override {
        if (!world) return false;

        // Check a grid of points inside capsule
        for (float dy = 0; dy < height; dy += 1.0f) {
            for (float dx = -radius; dx <= radius;
                 dx += radius) {
                for (float dz = -radius; dz <= radius;
                     dz += radius) {
                    glm::vec3 p = position +
                        glm::vec3(dx, dy, dz);

                    int bx = (int)std::floor(p.x);
                    int by = (int)std::floor(p.y);
                    int bz = (int)std::floor(p.z);

                    int cx = bx / VOXEL_DIM;
                    int lx = bx % VOXEL_DIM;
                    int cz = bz / VOXEL_DIM;
                    int lz = bz % VOXEL_DIM;

                    if (lx < 0) { cx--; lx += VOXEL_DIM; }
                    if (lz < 0) { cz--; lz += VOXEL_DIM; }

                    uint16_t id = world->typeAtWorld(
                        cx, cz, lx, by, lz);
                    if (id != BLOCK_AIR) return false;
                }
            }
        }
        return true;
    }

    void applyExplosion(glm::vec3 center,
                        float radius,
                        float force) override {
        // TODO K1 — combat phase
        (void)center; (void)radius; (void)force;
    }

private:
    ChunkManager* world = nullptr;

    // Physics constants
    static constexpr float GRAVITY       = 40.0f;
    static constexpr float MAX_FALL_SPEED= 80.0f;
    static constexpr float STEP_UP_MAX   = 5.0f;

    // Check if voxel at world position is solid
    bool isSolid(float wx, float wy, float wz) const {
        int bx = (int)std::floor(wx);
        int by = (int)std::floor(wy);
        int bz = (int)std::floor(wz);

        int cx = bx / VOXEL_DIM;
        int lx = bx % VOXEL_DIM;
        int cz = bz / VOXEL_DIM;
        int lz = bz % VOXEL_DIM;

        if (lx < 0) { cx--; lx += VOXEL_DIM; }
        if (lz < 0) { cz--; lz += VOXEL_DIM; }
        if (by < 0 || by >= VOXEL_DIM) return false;

        uint16_t id = world->typeAtWorld(
            cx, cz, lx, by, lz);
        return id != BLOCK_AIR;
    }

    // Resolve capsule collision for a movement delta
    void resolveCollision(Player& player,
                          glm::vec3 delta) {
        float r = player.capsuleRadius;
        float h = player.capsuleHeight;

        // Move X
        glm::vec3 tryX = player.position +
            glm::vec3(delta.x, 0, 0);
        if (!capsuleOverlapsWorld(tryX, r, h))
            player.position.x = tryX.x;
        else
            player.velocity.x = 0;

        // Move Z
        glm::vec3 tryZ = player.position +
            glm::vec3(0, 0, delta.z);
        if (!capsuleOverlapsWorld(tryZ, r, h))
            player.position.z = tryZ.z;
        else
            player.velocity.z = 0;

        // Move Y
        glm::vec3 tryY = player.position +
            glm::vec3(0, delta.y, 0);
        if (!capsuleOverlapsWorld(tryY, r, h))
            player.position.y = tryY.y;
        else
            player.velocity.y = 0;
    }

    // Check if capsule at position overlaps any solid voxel
    bool capsuleOverlapsWorld(glm::vec3 pos,
                              float r, float h) const {
        // Sample points around capsule cylinder
        float checkR = r + 0.1f; // small margin
        for (float dy = 0.1f; dy < h - 0.1f; dy += 0.5f) {
            float cy = pos.y + dy;
            // Check 8 points around circumference
            for (int i = 0; i < 8; i++) {
                float angle = (float)i / 8.0f *
                              6.28318f;
                float cx = pos.x + std::cos(angle) * checkR;
                float cz = pos.z + std::sin(angle) * checkR;
                if (isSolid(cx, cy, cz)) return true;
            }
        }
        return false;
    }

    // Auto step up small ledges
    void tryStepUp(Player& player, float dt) {
        if (player.velocity.x == 0.0f &&
            player.velocity.z == 0.0f) return;

        float r = player.capsuleRadius;
        float h = player.capsuleHeight;

        // Try lifting player by up to STEP_UP_MAX voxels
        for (float step = 1.0f;
             step <= STEP_UP_MAX; step += 1.0f) {
            glm::vec3 raised = player.position +
                glm::vec3(0, step, 0);
            if (!capsuleOverlapsWorld(raised, r, h)) {
                player.position.y += step;
                return;
            }
        }
    }

    // Check if player is standing on solid ground
    bool checkGrounded(const Player& player) const {
        float r = player.capsuleRadius * 0.8f;
        float groundY = player.position.y - 0.2f;

        // Check points under feet
        for (int i = 0; i < 4; i++) {
            float angle = (float)i / 4.0f * 6.28318f;
            float cx = player.position.x +
                       std::cos(angle) * r;
            float cz = player.position.z +
                       std::sin(angle) * r;
            if (isSolid(cx, groundY, cz)) return true;
        }
        // Check center
        return isSolid(player.position.x,
                       groundY,
                       player.position.z);
    }

    // Apply stored momentum after freeze ends
    void applyFreezeRelease(Player& player) {
        switch (player.freezeType) {
            case Player::FreezeType::DoubleJump:
                // Restore stored momentum + upward boost
                player.velocity = player.storedVelocity;
                player.velocity.y +=
                    player.DOUBLE_JUMP_BOOST;
                break;

            case Player::FreezeType::AerialDodge:
                // Restore stored momentum + directional burst
                player.velocity = player.storedVelocity;
                player.velocity +=
                    player.dodgeBurstDirection *
                    player.AERIAL_DODGE_BURST;
                break;

            case Player::FreezeType::None:
            default:
                break;
        }
        player.freezeType = Player::FreezeType::None;
    }
};
