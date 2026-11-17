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

    // Total mass in kg, gear included. Pushes, hits and
    // explosions change velocity by impulse / mass, so heavy
    // things are harder to move. Humans keep it in step with
    // their BodyMass (see Human.h).
    float     mass     = 70.0f;

    // Ids are handed out by whoever runs the world (the server,
    // or the game's own server in single player) and sent to
    // clients, so a counter is enough. An entity loaded with a
    // saved id keeps it; reserveId makes sure no new one gets it.
    static void reserveId(uint64_t id) { if (id >= idCounter()) idCounter() = id + 1; }

private:
    static uint64_t& idCounter() { static uint64_t counter = 1; return counter; }
    static uint64_t nextUUID() { return idCounter()++; }
};
