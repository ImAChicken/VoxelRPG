#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// ── Entity ────────────────────────────────────────────────
// Base for everything that lives in the world: the player,
// NPCs, enemies, later Kenneth and Illy.
//
// Entities are STATE ONLY. They hold numbers; they do not
// move themselves. Movement is done by IPhysics, so the same
// physics code runs on a client (prediction) and a server
// (authority), and a headless server never needs a renderer.
class Entity {
public:
    virtual ~Entity() = default;

    // Unique ID — how the network will refer to this entity
    uint64_t  uuid   = nextUUID();
    bool      active = true;       // false = pending removal

    // position is the centre of the feet, in voxels
    glm::vec3 position = {0, 0, 0};
    glm::vec3 velocity = {0, 0, 0};   // voxels per second
    glm::vec3 facing   = {0, 0, -1};  // horizontal look direction

private:
    // Simple counter for now — swap for real UUIDs when
    // multiplayer needs IDs that are unique across machines
    static uint64_t nextUUID() {
        static uint64_t counter = 1;
        return counter++;
    }
};
