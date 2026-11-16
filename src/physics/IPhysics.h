#pragma once
#include <glm/glm.hpp>
#include <functional>

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

    // Advance a human's body by one tick, after moveHuman:
    // applies the hits queued on it (Human::hit) as knockback
    // (impulse / mass) and to its ragdoll, then steps the
    // ragdoll. While the ragdoll is limp the capsule follows
    // it; when it gets up the capsule is stood back up.
    virtual void stepRagdoll(Human& human, float dt) = 0;

    // Queries
    // Does something solid stop bodies here: a solid voxel, or a
    // loose object (a fallen log)? False where the world is not
    // loaded.
    virtual bool solidAt(int x, int y, int z) const = 0;
    // The world's voxels only, without loose objects
    virtual bool worldSolidAt(int x, int y, int z) const = 0;
    // Loose objects that bodies collide with, raycasts hit, etc.
    // (world/Woodcutting.h logs). Empty = none.
    virtual void setLooseSolids(std::function<bool(int, int, int)> fn) = 0;
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
