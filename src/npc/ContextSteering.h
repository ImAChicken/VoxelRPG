#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// ══════════════════════════════════════════════════════════
// Context steering — how an NPC moves the last few steps in a
// fight
// ══════════════════════════════════════════════════════════
// The encounter (npc/Encounters.h) says WHERE each NPC should be:
// its slot around the target, close in when it holds an attack
// token. Pathfinding gets it near. Context steering does the
// last stretch, every few ticks, from two small maps of
// DIRS directions around the NPC:
//
//   interest  how much it wants to go each way: toward its spot,
//             strongest straight at it (the direct line wins when
//             nothing's in the way), a little sideways (circling)
//   danger    how much each way should be avoided: crowding into
//             another NPC (standing on top of each other costs a
//             lot, being close a little, so overlapping slots
//             still spread out), walls and drops in the voxels
//             ahead, stepping into the target's swing, and getting
//             closer than striking distance
//
// The NPC takes the most interesting direction among those with
// the least danger (with a little blending to neighbours, so the
// motion is smooth), and how strongly: interest there.
//
// Cheap because of the voxel world and the encounter:
//   - DIRS is 16: a handful of multiplies per direction
//   - walls and drops: ONE probe per direction (the caller's
//     'blocked', a few voxel reads), not a raycast fan
//   - the others to avoid come from the encounter's own list
//     (who's in this fight), not a world search
//   - it runs every STEER_EVERY ticks per NPC, staggered; between,
//     the last answer stands
// An encounter can bias it later (formation breadcrumbs, a flank
// order) by adding interest toward a point: 'extra' below.
namespace steer {

constexpr int   DIRS        = 16;
constexpr int   STEER_EVERY = 3;        // ticks between updates (staggered by id)
constexpr float PERSONAL    = 5.0f;     // voxels: closer than this to another NPC is crowding
constexpr float LOOK_AHEAD  = 3.0f;     // voxels ahead the walls-and-drops probe looks

inline glm::vec2 dirOf(int i) {
    float a = i * 6.2831853f / DIRS;
    return { std::cos(a), std::sin(a) };
}

struct Map {
    float interest[DIRS] = {};
    float danger[DIRS] = {};
    int   chosen = -1;
    glm::vec2 move{ 0.0f };             // the answer: direction times strength (0..1)
};

struct Inputs {
    glm::vec2 self{ 0.0f };             // x, z
    glm::vec2 goal{ 0.0f };             // where it wants to be (its slot)
    glm::vec2 target{ 0.0f };           // who it's fighting
    float strike = 8.0f;                // its striking distance
    bool  engage = false;               // closing in to strike (holds a token)
    bool  targetSwinging = false;       // the target's attack is coming
    float strafe = 0.0f;                // -1 / +1: which way it likes to circle (0: not)
    std::vector<glm::vec2> others;      // the other NPCs in its fight
    std::vector<std::pair<glm::vec2, float>> extra;   // more interest: (point, weight) — formations, flanks
};

// blocked(dir) → 0 open … 1 a wall or drop that way (the caller
// reads the voxels)
inline Map compute(const Inputs& in, const std::function<float(glm::vec2)>& blocked) {
    Map m;
    glm::vec2 toGoal = in.goal - in.self;
    float goalDist = glm::length(toGoal);
    glm::vec2 gdir = goalDist > 1e-3f ? toGoal / goalDist : glm::vec2(0.0f);
    glm::vec2 toTarget = in.target - in.self;
    float tdist = glm::length(toTarget);
    glm::vec2 tdir = tdist > 1e-3f ? toTarget / tdist : glm::vec2(0.0f);
    glm::vec2 side(tdir.y, -tdir.x);
    // how much it still wants to move toward the spot (eases off
    // when it's there, so it settles instead of jittering)
    float want = std::min(1.0f, goalDist / 4.0f);

    for (int i = 0; i < DIRS; i++) {
        glm::vec2 d = dirOf(i);
        // ── interest ──
        float g = glm::dot(d, gdir);
        float v = g > 0.0f ? g * g * want : 0.0f;              // squared: the straight line is preferred
        if (in.strafe != 0.0f) v = std::max(v, std::max(0.0f, glm::dot(d, side * in.strafe)) * 0.35f);
        for (auto& [p, w] : in.extra) {
            glm::vec2 e = p - in.self;
            float el = glm::length(e);
            if (el > 1e-3f) v = std::max(v, std::max(0.0f, glm::dot(d, e / el)) * w);
        }
        m.interest[i] = v;

        // ── danger ──
        float dg = 0.0f;
        for (const glm::vec2& o : in.others) {
            glm::vec2 to = o - in.self;
            float od = glm::length(to);
            if (od < 1e-3f || od > PERSONAL * 2.0f) continue;
            float c = (glm::dot(d, to / od) - 0.5f) * 2.0f;   // a cone of +-60 degrees toward it, so it can step past
            if (c <= 0.0f) continue;
            // on top of each other: strong; close: mild
            float k = od < PERSONAL ? 1.0f - od / (PERSONAL * 2.0f) : 0.5f * (1.0f - (od - PERSONAL) / PERSONAL);
            dg = std::max(dg, c * k);
        }
        float toward = glm::dot(d, tdir);
        if (toward > 0.0f) {
            if (tdist < in.strike - 2.0f) dg = std::max(dg, toward * 0.8f);                 // closer than it needs
            if (in.targetSwinging && tdist < in.strike + 4.0f) dg = std::max(dg, toward);   // into the swing
            if (!in.engage && tdist < in.strike + 6.0f) dg = std::max(dg, toward * 0.5f);   // waiting its turn: not too near
        }
        dg = std::max(dg, blocked(d));
        m.danger[i] = std::clamp(dg, 0.0f, 1.0f);
    }

    // choose: among the directions with (nearly) the least danger,
    // the most interesting
    float least = 1.0f;
    for (int i = 0; i < DIRS; i++) least = std::min(least, m.danger[i]);
    float best = 0.0f;
    for (int i = 0; i < DIRS; i++) {
        if (m.danger[i] > least + 0.15f) continue;
        float v = m.interest[i] * (1.0f - m.danger[i]);
        if (v > best) { best = v; m.chosen = i; }
    }
    if (m.chosen < 0 || best < 0.02f) return m;
    // a little toward the better neighbour, for smooth turning
    int a = (m.chosen + DIRS - 1) % DIRS, b = (m.chosen + 1) % DIRS;
    float va = m.danger[a] > least + 0.15f ? 0.0f : m.interest[a] * (1.0f - m.danger[a]);
    float vb = m.danger[b] > least + 0.15f ? 0.0f : m.interest[b] * (1.0f - m.danger[b]);
    glm::vec2 dir = dirOf(m.chosen) * best + dirOf(a) * va * 0.5f + dirOf(b) * vb * 0.5f;
    if (glm::length(dir) > 1e-3f) dir = glm::normalize(dir);
    m.move = dir * std::min(1.0f, best);
    return m;
}

} // namespace steer
