#pragma once
// ══════════════════════════════════════════════════════════
// The grapple's state on a body (game/Grapple.h does the rest)
// ══════════════════════════════════════════════════════════
// Kept apart so Human.h can hold it without pulling in the world.
// (Claude, 2026-11-25, ROADMAP Mushroom Islands phase 2)
#include <cstdint>
#include <glm/glm.hpp>

struct GrappleState {
    // Throw   the hook flying out (the body moves as it was)
    // Zip     reeled in: pulled straight at the anchor
    // Swing   the rope held at its length: a pendulum under it
    enum class Phase : uint8_t { None, Throw, Zip, Swing };
    Phase     phase  = Phase::None;
    glm::vec3 anchor{ 0.0f };     // where the hook holds (a body's: kept up to date by the simulation)
    glm::vec3 from{ 0.0f };       // where it was thrown from (the rope's flight)
    uint64_t  on     = 0;         // the body it's on (0: a block)
    bool      swingable = true;   // hold to swing from it (a grapple point, a flyer's; not a body on the ground)
    float     t      = 0.0f;      // s since it was thrown
    float     length = 0.0f;      // the rope's length while swinging
    float     stuck  = 0.0f;      // s the zip has been held up by something in the way
    float     speed  = 90.0f;     // vox/s it pulls (the item's)
    float     pull   = 0.0f;      // vox/s it's pulling now (up to speed)
    float     reach  = 120.0f;    // vox it reaches (the item's)
    bool      held   = true;      // the button still held since the throw (a tap lets go of it early)
    // pressed on the way in: played when it gets there (a light or
    // heavy attack, or an item used: a spell)
    bool      queuedLight = false, queuedHeavy = false;
    uint16_t  queuedItem = 0;

    bool active() const { return phase != Phase::None; }
    // the hook's tip this moment (it flies out while thrown)
    glm::vec3 tip(float throwTime) const {
        if (phase != Phase::Throw || throwTime <= 0.0f) return anchor;
        float k = t / throwTime;
        return from + (anchor - from) * (k < 1.0f ? k : 1.0f);
    }
};
