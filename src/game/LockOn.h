#pragma once
#include "../npc/Npc.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// ══════════════════════════════════════════════════════════
// LockOn — the camera held on one enemy (TAB)
// ══════════════════════════════════════════════════════════
// TAB picks the body nearest the middle of the view (within
// RANGE, in front of you), preferring ones hostile to you; TAB
// again lets go. While locked:
//   - the camera turns to keep the target ahead, so you face it
//     and A / D strafe round it, S backs away (the body always
//     faces where the camera looks)
//   - a flick of the mouse sideways moves to the next body that
//     way; up / down still tilts the camera
//   - it lets go when the target dies, goes past BREAK_RANGE, or
//     is out of sight for LOST_TIME
// A marker sits on the target (HudScreen::lockMarker).
struct LockOn {
    static constexpr float RANGE       = 70.0f;   // voxels to pick a target
    static constexpr float BREAK_RANGE = 100.0f;  // and to keep it
    static constexpr float LOST_TIME   = 2.0f;    // s out of sight before letting go
    static constexpr float FLICK       = 90.0f;   // px of sideways mouse within FLICK_TIME: next target
    static constexpr float FLICK_TIME  = 0.25f;
    static constexpr float TURN_RATE   = 10.0f;   // how fast the camera swings onto the target (1/s)

    EntityId target = 0;
    float    lostFor = 0.0f;
    float    flick = 0.0f, flickAge = 0.0f, flickRest = 0.0f;

    explicit operator bool() const { return target != 0; }
    void clear() { target = 0; lostFor = 0.0f; flick = 0.0f; }

    static glm::vec3 aimPoint(const Npc& n) { return n.position + glm::vec3(0.0f, Human::STAND_HEIGHT * 0.6f, 0.0f); }

    // The best body to lock onto from the eye looking along
    // 'front'. side = 0: nearest the middle of the view; ±1: the
    // nearest one to that side of 'from' (switching targets).
    // prefer(n): true for bodies hostile to you (picked first).
    static Npc* pick(const std::vector<std::unique_ptr<Npc>>& npcs, glm::vec3 eye, glm::vec3 front,
                     const std::function<bool(const Npc&)>& prefer, EntityId skip = 0, int side = 0,
                     glm::vec3 from = glm::vec3(0.0f)) {
        glm::vec2 f2 = glm::normalize(glm::vec2(front.x, front.z) + glm::vec2(1e-6f));
        glm::vec2 fromDir = glm::normalize(glm::vec2(from.x - eye.x, from.z - eye.z) + glm::vec2(1e-6f));
        Npc* best = nullptr; float bestScore = 1e9f;
        for (const auto& np : npcs) {
            const Npc& n = *np;
            if (n.dead() || n.uuid == skip) continue;
            glm::vec3 d = aimPoint(n) - eye;
            float dist = glm::length(d);
            if (dist > RANGE || dist < 1.0f) continue;
            glm::vec2 d2 = glm::normalize(glm::vec2(d.x, d.z));
            float score;
            if (side == 0) {
                float ang = std::acos(std::clamp(glm::dot(d2, f2), -1.0f, 1.0f));
                if (ang > glm::radians(60.0f)) continue;              // in front of you
                score = ang * 40.0f + dist * 0.3f;
            } else {
                // signed angle from the current target to this one (+ = to the right)
                float cross = fromDir.x * d2.y - fromDir.y * d2.x;
                float ang = std::atan2(cross, glm::dot(fromDir, d2));
                if (ang * (float)side <= glm::radians(2.0f) || std::fabs(ang) > glm::radians(120.0f)) continue;
                score = std::fabs(ang) * 40.0f + dist * 0.1f;
            }
            if (prefer && !prefer(n)) score += 1000.0f;                // hostile ones first
            if (score < bestScore) { bestScore = score; best = np.get(); }
        }
        return best;
    }

    // Mouse sideways while locked: returns -1 / +1 for a flick
    // to switch targets, else 0 (the movement is used up either way)
    int flicked(float dx, float dt) {
        flickRest = std::max(0.0f, flickRest - dt);
        flickAge += dt;
        if (flickAge > FLICK_TIME) { flick = 0.0f; flickAge = 0.0f; }
        flick += dx;
        if (flickRest <= 0.0f && std::fabs(flick) > FLICK) {
            int s = flick > 0 ? 1 : -1;
            flick = 0.0f; flickAge = 0.0f; flickRest = 0.35f;
            return s;
        }
        return 0;
    }

    // The camera yaw (degrees) that looks from 'eye' at the target,
    // and the yaw to use this frame (swinging toward it)
    static float yawTo(glm::vec3 eye, glm::vec3 at) {
        return glm::degrees(std::atan2(at.z - eye.z, at.x - eye.x));
    }
    static float turnToward(float yaw, float want, float dt) {
        float d = std::fmod(want - yaw + 540.0f, 360.0f) - 180.0f;
        return yaw + d * std::min(1.0f, TURN_RATE * dt);
    }
};
