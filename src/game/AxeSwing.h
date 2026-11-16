#pragma once
#include "../character/CharacterModel.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

// ══════════════════════════════════════════════════════════
// AxeSwing — a Valheim-style chop
// ══════════════════════════════════════════════════════════
// Left click with an axe in the hand. One swing takes a second:
//
//   0.00 – 0.30  wind up: the axe goes back over the right
//                shoulder, the torso twists right, weight back,
//                the left foot steps forward
//   0.38 – 0.52  strike: the torso unwinds and the arm whips the
//                blade across and down; it is fastest at the end
//   0.52 – 0.66  follow through toward the left hip
//   0.66 – 1.00  recover to the ready pose
//
// Holding the button chains swings. The body faces where the
// camera looks (as always) and walks at a third of its speed
// while swinging.
//
// The hit is not scripted: during the strike the blade's edge
// points (character/HeldItemModel.h) are traced from where they
// were last frame to where they are now. The first solid voxel
// they pass through is what the axe hits, and the edge's speed
// at that moment sets the damage, so a swing that connects early
// (still slow) does less than one that lands at full speed. A
// hit stops the swing dead for a moment (hit-stop), then it
// follows through. Leaves and plants don't stop the blade.
struct AxeSwing {
    static constexpr float DURATION     = 1.0f;
    static constexpr float STRIKE_START = 0.38f;   // the blade can hit from here…
    static constexpr float STRIKE_END   = 0.66f;   // …to here
    static constexpr float HITSTOP      = 0.09f;   // s frozen on a hit
    static constexpr float MOVE_FACTOR  = 0.35f;   // walking speed while swinging
    // Edge speed (voxels/s) that does the axe's full tool power;
    // measured from the animation (tests/woodcut_test.cpp)
    static constexpr float FULL_SPEED   = 300.0f;

    float t = -1.0f;          // seconds into the swing, < 0 = not swinging
    float stop = 0.0f;        // hit-stop left
    bool  hitDone = false;    // this swing has hit something
    bool  traceValid = false; // last frame's blade points are usable
    float lastStep = 0.0f;    // swing time the last advance() moved (0 in hit-stop)
    std::array<glm::vec3, 3> lastBlade{};

    bool active()   const { return t >= 0.0f; }
    bool striking() const { return t >= STRIKE_START && t <= STRIKE_END && !hitDone; }
    void start()          { t = 0.0f; stop = 0.0f; hitDone = false; traceValid = false; }
    void cancel()         { t = -1.0f; stop = 0.0f; traceValid = false; }

    // Advance by one frame (dt clamped, so a slow frame never
    // skips the whole strike)
    void advance(float dt) {
        if (!active()) return;
        dt = std::min(dt, 1.0f / 30.0f);
        if (stop > 0.0f) { stop -= dt; lastStep = 0.0f; return; }
        t += dt;
        lastStep = dt;
        if (t >= DURATION) { t = -1.0f; traceValid = false; }
    }

    // Damage for a hit at this edge speed
    static float damageFor(float toolPower, float edgeSpeed) {
        return toolPower * std::clamp(edgeSpeed / FULL_SPEED, 0.35f, 1.3f);
    }
};

// ── The swing's pose ──────────────────────────────────────
// Upper-body joint angles (degrees, as CharacterAnimator uses
// them) at each key time, eased between; legs add a step.
namespace axeswing {

struct Key {
    float t;
    glm::vec3 spine, head, shR, elR, wrR, shL, elL;
    float hipL, kneeL, hipR;
};

inline const std::array<Key, 6>& keys() {
    static const std::array<Key, 6> k = {{
        //  t     spine            head          right shoulder     elbow       wrist        left shoulder     elbow       hipL kneeL hipR
        { 0.00f, { 5,   0, 0}, {0,   0, 0}, { -40,   0, -10}, {-60,0,0}, {  0,  0,0}, {-20,  0, 10}, {-40,0,0},   0,   5,   0 },
        { 0.30f, {-4, -40, 0}, {0,  32, 0}, {-122, -45, -52}, {-95,0,0}, {-20,-45,0}, {-55, 20, 25}, {-60,0,0}, -18,  22,  -4 },
        { 0.38f, {-5, -44, 0}, {0,  35, 0}, {-126, -50, -54}, {-98,0,0}, {-24,-45,0}, {-58, 22, 26}, {-62,0,0}, -20,  24,  -4 },
        { 0.52f, {16,  10, 0}, {0,  -8, 0}, { -72,  28,  -8}, {-15,0,0}, { 28,-45,0}, {-25,  0, 15}, {-40,0,0}, -26,  26,   6 },
        { 0.66f, {20,  32, 0}, {0, -26, 0}, { -35,  50,  12}, {-25,0,0}, { 38,-30,0}, { -5,  0, 20}, {-30,0,0}, -22,  20,   8 },
        { 0.84f, { 8,  10, 0}, {0,  -8, 0}, { -45,  15,  -5}, {-50,0,0}, { 15,-10,0}, {-15,  0, 12}, {-40,0,0},  -8,  10,   2 },
    }};
    return k;
}

// How much of the swing pose shows (0 at the start and end)
inline float weight(float t) {
    auto ss = [](float a, float b, float x) { float u = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return u * u * (3 - 2 * u); };
    return ss(0.0f, 0.22f, t) * (1.0f - ss(0.84f, 1.0f, t));
}

// Blend the swing over the joint angles in 'rot' at time t.
// lookPitch: the camera's pitch (degrees, down < 0). Looking
// down bends the body forward and lowers the arm, so the chop
// follows the aim, as in Valheim: a log on the ground is hit by
// looking down at it.
inline void apply(float t, std::array<glm::vec3, BONE_COUNT>& rot, float lookPitch = 0.0f) {
    if (t < 0.0f) return;
    const auto& K = keys();
    size_t i = 0;
    while (i + 2 < K.size() && t > K[i + 1].t) i++;
    const Key& a = K[i];
    const Key& b = K[i + 1];
    float u = std::clamp((t - a.t) / (b.t - a.t), 0.0f, 1.0f);
    // the strike speeds up into the hit; everything else eases
    if (i == 2)      u = u * u;                          // 0.38 → 0.52: the strike
    else if (i == 3) u = 1.0f - (1.0f - u) * (1.0f - u);  // 0.52 → 0.66: follow through
    else                   u = u * u * (3.0f - 2.0f * u);
    auto L = [&](const glm::vec3& x, const glm::vec3& y) { return x + (y - x) * u; };
    float w = weight(t);
    auto put = [&](int bone, glm::vec3 v) { rot[bone] += (v - rot[bone]) * w; };
    put(B_SPINE, L(a.spine, b.spine));
    put(B_HEAD,  L(a.head, b.head));
    put(B_SHOULDER_R, L(a.shR, b.shR));
    put(B_ELBOW_R,    L(a.elR, b.elR));
    put(B_WRIST_R,    L(a.wrR, b.wrR));
    put(B_SHOULDER_L, L(a.shL, b.shL));
    put(B_ELBOW_L,    L(a.elL, b.elL));
    // aim: bend into a downward chop
    float down = std::clamp(-lookPitch, 0.0f, 50.0f);
    rot[B_SPINE].x      += down * 0.75f * w;
    rot[B_SHOULDER_R].x += down * 0.55f * w;
    rot[B_HEAD].x       -= down * 0.4f * w;
    // the step: added to whatever the legs are doing
    rot[B_HIP_L].x  += (a.hipL + (b.hipL - a.hipL) * u) * w;
    rot[B_KNEE_L].x += (a.kneeL + (b.kneeL - a.kneeL) * u) * w;
    rot[B_HIP_R].x  += (a.hipR + (b.hipR - a.hipR) * u) * w;
}

// ── Tracing the blade ─────────────────────────────────────
struct BladeHit {
    bool       hit = false;
    glm::ivec3 voxel{0};
    glm::vec3  point{0};      // where the edge entered it
    glm::vec3  dir{0};        // which way the edge was moving
    float      speed = 0.0f;  // edge speed, voxels/s
};

// Trace the edge points from 'from' to 'to' (world voxels) and
// return the first voxel 'blocks' says stops the blade. dt is
// the time between the two (for the speed).
inline BladeHit trace(const std::array<glm::vec3, 3>& from, const std::array<glm::vec3, 3>& to, float dt,
                      const std::function<bool(glm::ivec3)>& blocks) {
    BladeHit best;
    float bestS = 2.0f;
    for (int k = 0; k < 3; k++) {
        glm::vec3 d = to[k] - from[k];
        float len = glm::length(d);
        int steps = std::max(1, (int)std::ceil(len / 0.25f));
        for (int i = 0; i <= steps; i++) {
            float s = (float)i / steps;
            if (s >= bestS) break;
            glm::vec3 p = from[k] + d * s;
            glm::ivec3 v = glm::ivec3(glm::floor(p));
            if (!blocks(v)) continue;
            bestS = s;
            best.hit = true;
            best.voxel = v;
            best.point = p;
            best.dir = len > 1e-5f ? d / len : glm::vec3(0);
            best.speed = glm::length(to[1] - from[1]) / std::max(dt, 1e-4f);
            break;
        }
    }
    return best;
}

} // namespace axeswing
