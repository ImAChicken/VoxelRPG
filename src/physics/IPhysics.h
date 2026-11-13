#pragma once
#include <glm/glm.hpp>

// Forward declarations
class ChunkManager;
class Human;
struct HumanIntent;

// Raycast result
struct RaycastResult {
    bool       hit      = false;
    glm::vec3  position = {0,0,0};   // exact point that was hit
    glm::vec3  normal   = {0,0,0};   // which face (points back at the ray)
    glm::ivec3 blockPos = {0,0,0};   // world voxel that was hit
    float      distance = 0.0f;
    int        blockID  = 0;
};

// ── IPhysics ──────────────────────────────────────────────
// Pure interface for all physics implementations.
// VoxelPhysics implements it with voxel collision; it could
// be swapped for PhysX, Bullet or Jolt later.
//
// Entities are plain state. Physics is the only thing that
// moves them, so a server can run it with no renderer and a
// client can run the very same code to predict movement.
class IPhysics {
public:
    virtual ~IPhysics() = default;

    virtual bool init(ChunkManager& world) = 0;
    virtual void shutdown() = 0;
    virtual void setWorld(ChunkManager& world) = 0;

    // Advance one human by one tick.
    //   intent  what the human is trying to do (from the
    //           keyboard, an AI, or the network)
    //   dt      tick length in seconds — call with a fixed
    //           step (see PHYSICS_STEP) so results repeat
    //           exactly on every machine
    virtual void moveHuman(Human& human,
                           const HumanIntent& intent,
                           float dt) = 0;

    // Queries
    virtual bool isGrounded(const Human& human) const = 0;
    virtual bool isOnWall(const Human& human)   const = 0;

    // Cast a ray through the voxel grid. Hits any block,
    // including ones you can walk through (leaves).
    // maxDist in voxels.
    virtual RaycastResult raycast(
        glm::vec3 origin,
        glm::vec3 direction,
        float maxDist) const = 0;

    // Is a body of this size free of solid blocks here?
    // position = centre of the feet
    virtual bool capsuleIsClear(
        glm::vec3 position,
        float radius,
        float height) const = 0;

    // Find somewhere to stand at or near (x, z): on the
    // ground, with room for a body of this size. Returns
    // false if that part of the world is not loaded yet.
    virtual bool findStandingSpot(
        float x, float z,
        float radius, float height,
        glm::vec3& outPosition) const = 0;

    // Apply explosion force at world position
    virtual void applyExplosion(
        glm::vec3 center,
        float radius,
        float force) = 0;
};

// Physics runs at a fixed 60 ticks per second regardless of
// frame rate. Fast falls cannot skip through thin floors, and
// the same inputs always give the same result.
constexpr float PHYSICS_STEP = 1.0f / 60.0f;
