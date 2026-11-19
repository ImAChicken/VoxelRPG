#pragma once
#include <glm/glm.hpp>
#include <cmath>

// ══════════════════════════════════════════════════════════
// Wind — which way it blows and how hard, for swaying plants
// ══════════════════════════════════════════════════════════
// One wind over the world that turns slowly and comes and goes
// (minutes, from the clock, so it's the same every frame without
// state). Where it's felt is the shader's: shaders/chunk.vert
// weighs it by the place,
//   open sky       (the baked sky light: walls, cliffs and
//                  overhangs shelter what grows beside them)
//   under leaves   (the canopy texture: a forest floor is still)
//   height         (ridges and high ground catch more of it)
// and lays gusts over it that roll across the land downwind.
namespace wind {

struct Now {
    glm::vec2 dir{ 1.0f, 0.0f };   // the way it blows (x, z), unit length
    float     strength = 0.6f;     // 0 calm .. 1 strong
};

inline Now at(double seconds) {
    double t = seconds;
    float ang = 0.7f + 0.9f * (float)std::sin(t / 173.0) + 0.3f * (float)std::sin(t / 41.0);
    Now n;
    n.dir = glm::vec2(std::cos(ang), std::sin(ang));
    n.strength = 0.55f + 0.30f * (float)std::sin(t / 61.0 + 1.3) + 0.15f * (float)std::sin(t / 17.0);
    n.strength = glm::clamp(n.strength, 0.1f, 1.0f);
    return n;
}

} // namespace wind
