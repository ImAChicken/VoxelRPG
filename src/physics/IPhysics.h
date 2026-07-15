#pragma once
#include <glm/glm.hpp>

// Forward declarations
class ChunkManager;
struct Player;

// Raycast result
struct RaycastResult {
    bool     hit        = false;
    glm::vec3 position  = {0,0,0};   // hit position
    glm::vec3 normal    = {0,0,0};   // hit face normal
    glm::ivec3 blockPos = {0,0,0};   // block coordinate hit
    float    distance   = 0.0f;
    int      blockID    = 0;
};

// Pure interface for all physics implementations
// VoxelPhysics implements this for voxel collision
// Could swap to PhysX, Bullet, or Jolt later
// Server runs physics without renderer
// Client runs same physics for prediction

class IPhysics {
public:
    virtual ~IPhysics() = default;

    // Initialize with world reference
    virtual bool init(ChunkManager& world) = 0;
    virtual void shutdown() = 0;

    // Update world reference when chunks change
    virtual void setWorld(ChunkManager& world) = 0;

    // Player movement
    // Reads player input state, updates player position
    // dt = delta time in seconds
    virtual void movePlayer(Player& player,
                            float dt) = 0;

    // Query
    virtual bool isGrounded(const Player& player) const = 0;
    virtual bool isOnWall(const Player& player)   const = 0;

    // Raycast from origin in direction
    // maxDist in voxels
    virtual RaycastResult raycast(
        glm::vec3 origin,
        glm::vec3 direction,
        float maxDist) const = 0;

    // Check if a capsule at position is clear
    // radius and height in voxels
    virtual bool capsuleIsClear(
        glm::vec3 position,
        float radius,
        float height) const = 0;

    // Apply explosion force at world position
    // radius and force in voxels
    virtual void applyExplosion(
        glm::vec3 center,
        float radius,
        float force) = 0;
};
