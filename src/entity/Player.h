#pragma once
#include "Human.h"

// ── Player ────────────────────────────────────────────────
// The human the person at the keyboard controls. Everything
// about how the body moves lives in Human; this adds only
// what is specific to the player.
class Player : public Human {
public:
    // Where the player comes back to after falling out of
    // the world
    glm::vec3 spawnPoint = {0, 0, 0};
};
