#pragma once
#include "../character/CharacterModel.h"
#include "../entity/BodyMass.h"
#include "VoxelBall.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

// ══════════════════════════════════════════════════════════
// Ragdoll — an active ragdoll on top of the animation
// ══════════════════════════════════════════════════════════
// The animation stays in charge. The ragdoll is a second copy
// of the body made of 20 weighted points (joints plus head,
// hands and toes) tied together by fixed-length links, which
// the physics can push around.
//
//   Animated    normal play: the points simply follow the
//               animated skeleton (no simulation), so the body
//               looks exactly as the animation says
//   Reacting    after a hit: the points are simulated. Muscles
//               pull each joint back toward its animated angle
//               and a balance pull brings the core back over
//               the feet; both drop on impact and recover over
//               about half a second, then it is Animated again
//   Limp        knocked down: muscles at a faint tone, no
//               balance; the body falls and tumbles on the
//               voxels until it settles
//   Recovering  getting up: strength and balance return and
//               the drawn body blends back to the animation
//
// Weight matters everywhere: each point carries its share of
// the body and gear (entity/BodyMass.h). A hit's speed change
// is impulse / total mass, so heavy bodies stagger less and
// fall over less often; muscles have a force limit, so a
// heavy weapon makes its arm slower to come back.
//
// Muscles work on joint ANGLES (each limb relative to its
// parent), not world positions, so a limp body still keeps a
// faint shape and an active one keeps its pose while the
// whole body is pushed. Units: voxels, seconds, kilograms.
//
// Headless: no GL here. The renderer asks renderBones() for
// skeleton matrices while the ragdoll is visible.

enum RagPoint : int {
    RP_PELVIS, RP_WAIST, RP_NECK, RP_HEAD,
    RP_SHOULDER_L, RP_ELBOW_L, RP_WRIST_L, RP_HAND_L,
    RP_SHOULDER_R, RP_ELBOW_R, RP_WRIST_R, RP_HAND_R,
    RP_HIP_L, RP_KNEE_L, RP_ANKLE_L, RP_TOE_L,
    RP_HIP_R, RP_KNEE_R, RP_ANKLE_R, RP_TOE_R,
    RP_COUNT
};

using BoneMats = std::array<glm::mat4, BONE_COUNT>;

class Ragdoll {
public:
    enum class State : uint8_t { Animated, Reacting, Limp, Recovering };

    // ── Tuning ────────────────────────────────────────────
    static constexpr float GRAVITY       = 80.0f;   // vox/s²: twice the walking body's; a falling body reads better fast
    static constexpr int   SUBSTEPS      = 2;       // per physics tick (120 Hz)
    static constexpr int   ITERATIONS    = 6;       // constraint passes per substep
    static constexpr float DAMPING       = 0.996f;  // velocity kept per substep (air drag)
    static constexpr float FRICTION      = 0.55f;   // sliding speed lost per substep on contact
    static constexpr float MAX_SPEED     = 110.0f;  // vox/s, so points never skip through a voxel
    static constexpr float KNOCKDOWN_DV  = 35.0f;   // body speed change (vox/s) that knocks it down
    static constexpr float HIT_SPREAD    = 4.0f;    // vox: how far a hit spreads from where it lands
    static constexpr float MUSCLE_GAIN   = 0.35f;   // share of a joint's angle error fixed per substep
    static constexpr float MUSCLE_FORCE  = 1400.0f; // kg·vox/s² per joint at full strength (~holds 17 kg out at arm's length)
    static constexpr float BALANCE_GAIN  = 0.015f;
    static constexpr float BALANCE_ACCEL = 150.0f;  // vox/s² most the core is pulled toward the animated body
    static constexpr float LIMP_TONE     = 0.06f;   // muscle strength while limp
    static constexpr float REACT_RECOVER = 3.0f;    // per second, strength back toward 1 after a hit
    static constexpr float LIMP_MIN      = 1.2f;    // s down before getting up, once still
    static constexpr float LIMP_MAX      = 4.0f;    // s down at most
    static constexpr float SETTLED_SPEED = 4.0f;    // vox/s: below this the body is still
    static constexpr float RECOVER_TIME  = 0.8f;    // s to get back up
    static constexpr float FADE_TIME     = 0.15f;   // s to hand the drawn body back to the animation

    // Dev: simulate all the time (muscles at full strength), so
    // weight and collisions show in ordinary movement too
    bool alwaysActive = false;
    // Dead: once limp, never gets up (cleared by reset())
    bool stayDown = false;

    // ── Setup, every frame ────────────────────────────────
    // The body's proportions (call when the look changes;
    // cheap, so calling every frame is fine)
    void setModel(const CharacterModel& m) {
        float s = m.heightIn / 67.4f;
        float headH = m.heightIn - m.rootHeight - m.bones[B_SPINE].offset.y - m.bones[B_HEAD].offset.y;
        glm::vec3 head(0.0f, headH * 0.5f, 0.0f);
        glm::vec3 hand(0.0f, -2.5f * s, 0.0f);
        glm::vec3 toe(0.0f, -m.footHeight * 0.6f, 4.5f * s);
        if (head != headLocal || hand != handLocal || toe != toeLocal) linksBuilt = false;
        headLocal = head; handLocal = hand; toeLocal = toe;

        auto dir = [](glm::vec3 v) { float l = glm::length(v); return l > 1e-5f ? v / l : glm::vec3(0, -1, 0); };
        rest[B_HEAD]       = dir(headLocal);
        rest[B_SHOULDER_L] = dir(m.bones[B_ELBOW_L].offset);
        rest[B_ELBOW_L]    = dir(m.bones[B_WRIST_L].offset);
        rest[B_WRIST_L]    = dir(handLocal);
        rest[B_SHOULDER_R] = dir(m.bones[B_ELBOW_R].offset);
        rest[B_ELBOW_R]    = dir(m.bones[B_WRIST_R].offset);
        rest[B_WRIST_R]    = dir(handLocal);
        rest[B_HIP_L]      = dir(m.bones[B_KNEE_L].offset);
        rest[B_KNEE_L]     = dir(m.bones[B_ANKLE_L].offset);
        rest[B_ANKLE_L]    = dir(toeLocal);
        rest[B_HIP_R]      = dir(m.bones[B_KNEE_R].offset);
        rest[B_KNEE_R]     = dir(m.bones[B_ANKLE_R].offset);
        rest[B_ANKLE_R]    = dir(toeLocal);

        // collision sizes, in voxels, scaled with the body
        const float R[RP_COUNT] = {
            1.5f, 1.4f, 0.9f, 1.25f,
            0.8f, 0.6f, 0.5f, 0.5f,
            0.8f, 0.6f, 0.5f, 0.5f,
            0.9f, 0.7f, 0.5f, 0.45f,
            0.9f, 0.7f, 0.5f, 0.45f,
        };
        for (int i = 0; i < RP_COUNT; i++) radius[i] = R[i] * s;
        haveModel = true;
    }

    // How heavy each point is (from the body and its gear)
    void setMass(const BodyMass& bm) {
        mass.fill(0.0f);
        auto put = [&](int p, float kg) { mass[p] += kg; };
        auto seg = [&](int s) { return bm.segment(s); };
        put(RP_HEAD, seg(SEG_HEAD));
        float th = seg(SEG_THORAX);
        put(RP_WAIST, th * 0.35f); put(RP_NECK, th * 0.15f);
        put(RP_SHOULDER_L, th * 0.25f); put(RP_SHOULDER_R, th * 0.25f);
        float pv = seg(SEG_PELVIS);
        put(RP_PELVIS, pv * 0.5f); put(RP_HIP_L, pv * 0.25f); put(RP_HIP_R, pv * 0.25f);
        for (int side = 0; side < 2; side++) {
            int o = side * 3;                 // segment offset (L then R)
            int sh = side ? RP_SHOULDER_R : RP_SHOULDER_L;
            float ua = seg(SEG_UPPER_ARM_L + o), fa = seg(SEG_FOREARM_L + o), ha = seg(SEG_HAND_L + o);
            put(sh, ua * 0.5f);     put(sh + 1, ua * 0.5f + fa * 0.5f);
            put(sh + 2, fa * 0.5f + ha * 0.3f); put(sh + 3, ha * 0.7f);
            int hp = side ? RP_HIP_R : RP_HIP_L;
            float tg = seg(SEG_THIGH_L + o), sk = seg(SEG_SHANK_L + o), ft = seg(SEG_FOOT_L + o);
            put(hp, tg * 0.5f);     put(hp + 1, tg * 0.5f + sk * 0.5f);
            put(hp + 2, sk * 0.5f + ft * 0.4f); put(hp + 3, ft * 0.6f);
        }
        totalMass = 0.0f;
        for (int i = 0; i < RP_COUNT; i++) {
            mass[i] = std::max(mass[i], 0.05f);
            invMass[i] = 1.0f / mass[i];
            totalMass += mass[i];
        }
    }

    // The animated skeleton this tick, in world voxels
    // (CharacterRenderer::rootTransform × boneMatrices)
    void setTarget(const BoneMats& bones) {
        if (!haveModel) return;
        targetBones = bones;
        pointsFrom(bones, target);
        if (!hasTarget) {
            pos = old = snap = target;
            lastRoot = target[RP_PELVIS];
            hasTarget = true;
        }
        if (!linksBuilt) buildLinks();
    }

    // Forget the simulated body (teleport, respawn, fly mode).
    // The next target snaps it back into place.
    void reset() {
        stayDown = false;
        st = State::Animated;
        hasTarget = false;
        renderWeight = 0.0f;
        kick.fill(glm::vec3(0.0f));
        standUp = false;
    }

    // ── Hits ──────────────────────────────────────────────
    // An impulse (kg·vox/s) landing at a world point. Spread
    // over the nearby points so they all get the same speed
    // change; total momentum is exactly the impulse.
    // totalKg = the whole body with gear: decides stagger vs
    // knockdown.
    void hit(glm::vec3 point, glm::vec3 impulse, float totalKg) {
        if (!hasTarget) return;
        float dv = glm::length(impulse) / std::max(totalKg, 1.0f);
        if (st == State::Animated || st == State::Reacting) {
            if (dv >= KNOCKDOWN_DV) {
                st = State::Limp;
                timer = 0.0f;
            } else {
                st = State::Reacting;
                timer = 0.0f;
                float k = std::clamp(1.0f - 1.2f * dv / KNOCKDOWN_DV, 0.1f, 0.8f);
                strength = std::min(strength, k);
                balance  = std::min(balance, k * k);   // balance is lost faster than strength
            }
            renderWeight = 1.0f;
        } else if (st == State::Recovering && dv >= KNOCKDOWN_DV * 0.5f) {
            st = State::Limp;               // knocked back down while getting up
            timer = 0.0f;
        }
        float denom = 0.0f;
        std::array<float, RP_COUNT> w{};
        for (int i = 0; i < RP_COUNT; i++) {
            glm::vec3 d = pos[i] - point;
            w[i] = std::exp(-glm::dot(d, d) / (HIT_SPREAD * HIT_SPREAD));
            denom += mass[i] * w[i];
        }
        if (denom < 1e-6f) {                // far from every point: push the whole body
            for (int i = 0; i < RP_COUNT; i++) w[i] = 1.0f;
            denom = totalMass;
        }
        for (int i = 0; i < RP_COUNT; i++) kick[i] += impulse * (w[i] / denom);
    }

    // ── One physics tick ──────────────────────────────────
    void step(float dt, const SolidFn& solid) {
        if (!hasTarget || !linksBuilt) return;
        snap = pos;
        const float h = dt / SUBSTEPS;

        switch (st) {
        case State::Animated:
            if (!alwaysActive) {
                old = pos;
                pos = target;                     // follow the animation exactly
                lastRoot = target[RP_PELVIS];
                renderWeight = std::max(0.0f, renderWeight - dt / FADE_TIME);
                return;
            }
            strength = balance = 1.0f;
            renderWeight = 1.0f;
            break;
        case State::Reacting: {
            timer += dt;
            float k = 1.0f - std::exp(-dt * REACT_RECOVER);
            strength += (1.0f - strength) * k;
            balance  += (1.0f - balance) * k;
            break;
        }
        case State::Limp:
            timer += dt;
            strength = LIMP_TONE;
            balance  = 0.0f;
            break;
        case State::Recovering: {
            timer += dt;
            float t = std::clamp(timer / RECOVER_TIME, 0.0f, 1.0f);
            strength = LIMP_TONE + (1.0f - LIMP_TONE) * t;
            balance  = t;
            renderWeight = 1.0f - t * t * (3.0f - 2.0f * t);
            break;
        }
        }

        // Staggering (or always active), the body is still on its
        // own feet: it rides along with the animated body's
        // movement (running, jumping) and the hit is a wobble on
        // top. Limp, it is on its own.
        if (st == State::Reacting || st == State::Animated) {
            glm::vec3 d = target[RP_PELVIS] - lastRoot;
            for (int i = 0; i < RP_COUNT; i++) { pos[i] += d; old[i] += d; snap[i] += d; }
        }
        lastRoot = target[RP_PELVIS];

        for (int sub = 0; sub < SUBSTEPS; sub++) substep(h, solid, sub == 0);

        // ── State changes ─────────────────────────────────
        if (st == State::Reacting) {
            if ((timer > 0.5f && strength > 0.95f && poseError() < 0.4f) || timer > 2.5f)
                handBack();
        } else if (st == State::Limp) {
            if (stayDown) {
                // dead: lies where it falls
            } else if ((timer > LIMP_MIN && maxSpeed(dt) < SETTLED_SPEED) || timer > LIMP_MAX) {
                st = State::Recovering;
                timer = 0.0f;
                standUp = true;                 // physics puts the capsule back on its feet
            }
        } else if (st == State::Recovering) {
            if (timer >= RECOVER_TIME) handBack();
        } else if (st == State::Animated && alwaysActive) {
            // nothing: stays simulated
        }
    }

    // ── Queries ───────────────────────────────────────────
    State state()          const { return st; }
    bool  isLimp()         const { return st == State::Limp; }
    // Limp or getting up: no walking, jumping or dodging
    bool  controlsLocked() const { return st == State::Limp || st == State::Recovering; }
    // getting up, far enough along to roll out of it (a dodge pressed now)
    bool  canRollOut()     const { return st == State::Recovering && timer >= RECOVER_TIME * 0.35f; }
    void  rollOut()              { if (canRollOut()) handBack(); }
    bool  simulating()     const { return st != State::Animated || alwaysActive; }
    float strengthNow()    const { return strength; }
    float bodyMass()       const { return totalMass; }
    glm::vec3 point(int i) const { return pos[i]; }
    float     pointRadius(int i) const { return radius[i]; }

    // How far the ragdoll has moved the head from where the
    // animation has it (blended between ticks, faded with the
    // drawn ragdoll). The first-person camera adds this, so a
    // shove jolts the view and a knockdown takes it down too.
    glm::vec3 headOffset(float alpha) const {
        if (!hasTarget || renderWeight <= 0.0f) return glm::vec3(0.0f);
        glm::vec3 h = glm::mix(snap[RP_HEAD], pos[RP_HEAD], alpha);
        return (h - target[RP_HEAD]) * renderWeight;
    }

    // True once, when the body should stand up again
    bool takeStandUp() { bool s = standUp; standUp = false; return s; }

    // Lowest point of the body (its bottom), for placing the
    // capsule under a limp body
    float bottom() const {
        float y = pos[0].y - radius[0];
        for (int i = 1; i < RP_COUNT; i++) y = std::min(y, pos[i].y - radius[i]);
        return y;
    }

    // The skeleton to draw. alpha blends between the last two
    // ticks; animBones is the plain animated skeleton at the
    // drawn position, blended in while the ragdoll hands back.
    // Returns false when the plain animation should be drawn.
    bool renderBones(float alpha, const BoneMats& animBones, BoneMats& out) const {
        if (!hasTarget || !linksBuilt || renderWeight <= 0.0f) return false;
        std::array<glm::vec3, RP_COUNT> q;
        for (int i = 0; i < RP_COUNT; i++) q[i] = glm::mix(snap[i], pos[i], alpha);
        std::array<glm::mat3, BONE_COUNT> Fq, Ft;
        frames(q, Fq);
        frames(target, Ft);
        for (int b = 0; b < BONE_COUNT; b++) {
            int j = JOINT[b];
            glm::mat3 R = Fq[b] * glm::transpose(Ft[b]);
            glm::mat4 M = glm::translate(glm::mat4(1.0f), q[j]) * glm::mat4(R) *
                          glm::translate(glm::mat4(1.0f), -target[j]) * targetBones[b];
            out[b] = renderWeight >= 1.0f ? M : blend(animBones[b], M, renderWeight);
        }
        return true;
    }

private:
    // The point at each bone's joint, and the point its part
    // reaches toward (-1: the bone is a cluster, see frames())
    static constexpr int JOINT[BONE_COUNT] = {
        RP_PELVIS, RP_WAIST, RP_NECK,
        RP_SHOULDER_L, RP_ELBOW_L, RP_WRIST_L,
        RP_SHOULDER_R, RP_ELBOW_R, RP_WRIST_R,
        RP_HIP_L, RP_KNEE_L, RP_ANKLE_L,
        RP_HIP_R, RP_KNEE_R, RP_ANKLE_R,
    };
    static constexpr int CHILD[BONE_COUNT] = {
        -1, -1, RP_HEAD,
        RP_ELBOW_L, RP_WRIST_L, RP_HAND_L,
        RP_ELBOW_R, RP_WRIST_R, RP_HAND_R,
        RP_KNEE_L, RP_ANKLE_L, RP_TOE_L,
        RP_KNEE_R, RP_ANKLE_R, RP_TOE_R,
    };
    static constexpr int PARENT[BONE_COUNT] = {
        -1, B_ROOT, B_SPINE,
        B_SPINE, B_SHOULDER_L, B_ELBOW_L,
        B_SPINE, B_SHOULDER_R, B_ELBOW_R,
        B_ROOT, B_HIP_L, B_KNEE_L,
        B_ROOT, B_HIP_R, B_KNEE_R,
    };
    // Joint limits: how far each part may swing from its
    // parent's rest direction (degrees)
    static constexpr float CONE[BONE_COUNT] = {
        0, 65, 55,              // pelvis, torso over pelvis, head over torso
        180, 0, 75,             // shoulder (free), elbow (hinge), wrist
        180, 0, 75,
        115, 0, 45,             // hip, knee (hinge), ankle
        115, 0, 45,
    };

    struct Link { int a, b; float len; };

    State st = State::Animated;
    bool  hasTarget = false, linksBuilt = false, haveModel = false, standUp = false;
    float timer = 0.0f, strength = 1.0f, balance = 1.0f, renderWeight = 0.0f;
    float totalMass = 70.0f;

    std::array<glm::vec3, RP_COUNT> pos{}, old{}, snap{}, target{}, kick{};
    std::array<float, RP_COUNT> mass{}, invMass{}, radius{};
    std::array<glm::vec3, BONE_COUNT> rest{};
    std::array<glm::vec3, RP_COUNT> contact{};   // last contact normal (0 = none)
    BoneMats targetBones{};
    glm::vec3 lastRoot{0};                       // the animated pelvis last tick
    glm::vec3 headLocal{0}, handLocal{0}, toeLocal{0};
    std::vector<Link> links;

public:
    Ragdoll() {
        mass.fill(3.5f);
        invMass.fill(1.0f / 3.5f);
        radius.fill(0.6f);
    }

private:
    // ── Points from a skeleton ────────────────────────────
    void pointsFrom(const BoneMats& B, std::array<glm::vec3, RP_COUNT>& p) const {
        auto J = [&](int b) { return glm::vec3(B[b][3]); };
        auto at = [&](int b, glm::vec3 local) { return glm::vec3(B[b] * glm::vec4(local, 1.0f)); };
        p[RP_PELVIS] = J(B_ROOT);  p[RP_WAIST] = J(B_SPINE);
        p[RP_NECK]   = J(B_HEAD);  p[RP_HEAD]  = at(B_HEAD, headLocal);
        p[RP_SHOULDER_L] = J(B_SHOULDER_L); p[RP_ELBOW_L] = J(B_ELBOW_L);
        p[RP_WRIST_L] = J(B_WRIST_L);       p[RP_HAND_L]  = at(B_WRIST_L, handLocal);
        p[RP_SHOULDER_R] = J(B_SHOULDER_R); p[RP_ELBOW_R] = J(B_ELBOW_R);
        p[RP_WRIST_R] = J(B_WRIST_R);       p[RP_HAND_R]  = at(B_WRIST_R, handLocal);
        p[RP_HIP_L] = J(B_HIP_L);   p[RP_KNEE_L] = J(B_KNEE_L);
        p[RP_ANKLE_L] = J(B_ANKLE_L); p[RP_TOE_L] = at(B_ANKLE_L, toeLocal);
        p[RP_HIP_R] = J(B_HIP_R);   p[RP_KNEE_R] = J(B_KNEE_R);
        p[RP_ANKLE_R] = J(B_ANKLE_R); p[RP_TOE_R] = at(B_ANKLE_R, toeLocal);
    }

    // Fixed lengths, measured from the animated skeleton (the
    // parts are rigid, so any pose gives the same lengths).
    // The pelvis and the chest are each a rigid group of four.
    void buildLinks() {
        links.clear();
        auto add = [&](int a, int b) { links.push_back({ a, b, glm::length(target[a] - target[b]) }); };
        const int pel[4] = { RP_PELVIS, RP_WAIST, RP_HIP_L, RP_HIP_R };
        const int tor[4] = { RP_WAIST, RP_NECK, RP_SHOULDER_L, RP_SHOULDER_R };
        for (int i = 0; i < 4; i++)
            for (int j = i + 1; j < 4; j++) { add(pel[i], pel[j]); add(tor[i], tor[j]); }
        add(RP_NECK, RP_HEAD);
        for (int b = 0; b < BONE_COUNT; b++)
            if (CHILD[b] >= 0 && b != B_HEAD) add(JOINT[b], CHILD[b]);
        linksBuilt = true;
    }

    // ── Frames ────────────────────────────────────────────
    // An orientation for each part, built the same way from
    // the animated points and the simulated points; the
    // difference between the two is how far the ragdoll has
    // turned that part. The pelvis and chest use their four
    // points; every limb is its parent's frame turned the
    // shortest way onto the limb (so twist follows the parent).
    static glm::vec3 safeNorm(glm::vec3 v, glm::vec3 fallback) {
        float l = glm::length(v);
        return l > 1e-6f ? v / l : fallback;
    }
    static glm::mat3 basis(glm::vec3 up, glm::vec3 left) {
        glm::vec3 y = safeNorm(up, glm::vec3(0, 1, 0));
        glm::vec3 x = left - glm::dot(left, y) * y;
        x = safeNorm(x, std::fabs(y.x) < 0.9f ? glm::normalize(glm::cross(y, glm::vec3(0, 0, 1)))
                                               : glm::normalize(glm::cross(y, glm::vec3(1, 0, 0))));
        return glm::mat3(x, y, glm::cross(x, y));
    }
    // Shortest rotation taking unit vector a onto unit vector b
    static glm::mat3 arc(glm::vec3 a, glm::vec3 b) {
        float c = glm::dot(a, b);
        if (c > 0.99999f) return glm::mat3(1.0f);
        if (c < -0.99999f) {
            glm::vec3 axis = std::fabs(a.x) < 0.9f ? glm::cross(a, glm::vec3(1, 0, 0)) : glm::cross(a, glm::vec3(0, 1, 0));
            return glm::mat3_cast(glm::angleAxis(3.14159265f, glm::normalize(axis)));
        }
        glm::vec3 axis = glm::cross(a, b);
        glm::quat q(1.0f + c, axis.x, axis.y, axis.z);
        return glm::mat3_cast(glm::normalize(q));
    }
    void frames(const std::array<glm::vec3, RP_COUNT>& p, std::array<glm::mat3, BONE_COUNT>& F) const {
        F[B_ROOT]  = basis(p[RP_WAIST] - p[RP_PELVIS], p[RP_HIP_L] - p[RP_HIP_R]);
        F[B_SPINE] = basis(p[RP_NECK] - p[RP_WAIST], p[RP_SHOULDER_L] - p[RP_SHOULDER_R]);
        for (int b = B_HEAD; b < BONE_COUNT; b++) {     // parents come first in Bone order
            const glm::mat3& Fp = F[PARENT[b]];
            glm::vec3 a = safeNorm(p[CHILD[b]] - p[JOINT[b]], Fp * rest[b]);
            F[b] = arc(Fp * rest[b], a) * Fp;
        }
    }

    // ── One substep ───────────────────────────────────────
    void substep(float h, const SolidFn& solid, bool first) {
        // integrate: Verlet, with gravity and the hits waiting
        const glm::vec3 g(0.0f, -GRAVITY * h * h, 0.0f);
        const float vmax = MAX_SPEED * h;
        for (int i = 0; i < RP_COUNT; i++) {
            glm::vec3 v = (pos[i] - old[i]) * DAMPING;
            if (first) { v += kick[i] * invMass[i] * h; kick[i] = glm::vec3(0.0f); }
            float l = glm::length(v);
            if (l > vmax) v *= vmax / l;
            old[i] = pos[i];
            pos[i] += v + g;
        }

        std::array<glm::mat3, BONE_COUNT> Ft, Fs;
        frames(target, Ft);
        frames(pos, Fs);
        muscles(h, Ft, Fs);
        balancePull(h);

        contact.fill(glm::vec3(0.0f));
        for (int it = 0; it < ITERATIONS; it++) {
            for (const Link& L : links) {
                glm::vec3 d = pos[L.b] - pos[L.a];
                float len = glm::length(d);
                if (len < 1e-6f) continue;
                float wsum = invMass[L.a] + invMass[L.b];
                glm::vec3 corr = d * ((len - L.len) / (len * wsum));
                pos[L.a] += corr * invMass[L.a];
                pos[L.b] -= corr * invMass[L.b];
            }
            frames(pos, Fs);
            limits(Fs);
            for (int i = 0; i < RP_COUNT; i++) {
                glm::vec3 n;
                if (collide(i, solid, n)) contact[i] = n;
            }
        }

        // friction: points touching the ground lose sliding speed
        // and stop moving into the surface
        for (int i = 0; i < RP_COUNT; i++) {
            glm::vec3 n = contact[i];
            if (n == glm::vec3(0.0f)) continue;
            glm::vec3 v = pos[i] - old[i];
            float vn = glm::dot(v, n);
            glm::vec3 vt = v - vn * n;
            v = std::max(vn, 0.0f) * n + vt * (1.0f - FRICTION);
            old[i] = pos[i] - v;
        }
    }

    // Muscles: pull each part back toward its animated angle
    // relative to its parent. Force-limited, so a part made
    // heavy by gear comes back more slowly; the parent takes
    // the equal and opposite push.
    void muscles(float h, const std::array<glm::mat3, BONE_COUNT>& Ft,
                 const std::array<glm::mat3, BONE_COUNT>& Fs) {
        if (strength <= 0.0f) return;
        const float gain = MUSCLE_GAIN * strength;
        const float maxF = MUSCLE_FORCE * strength * h * h;
        auto drive = [&](int c, int j, glm::vec3 want) {
            glm::vec3 d = (want - pos[c]) * gain;
            float lim = maxF * invMass[c], l = glm::length(d);
            if (l > lim) d *= lim / l;
            pos[c] += d;
            pos[j] -= d * (mass[c] * invMass[j]);
        };
        // the chest over the pelvis
        {
            glm::mat3 R = Fs[B_ROOT] * glm::transpose(Ft[B_ROOT]);
            for (int c : { RP_NECK, RP_SHOULDER_L, RP_SHOULDER_R })
                drive(c, RP_WAIST, pos[RP_WAIST] + R * (target[c] - target[RP_WAIST]));
        }
        // the head and limbs over their parents
        for (int b = B_HEAD; b < BONE_COUNT; b++) {
            int p = PARENT[b], j = JOINT[b], c = CHILD[b];
            glm::mat3 R = Fs[p] * glm::transpose(Ft[p]);
            drive(c, j, pos[j] + R * (target[c] - target[j]));
        }
    }

    // Balance: the core and feet pulled toward the animated body
    // in the world (the "standing up" part of an active body).
    // Arms and head are left to their muscles.
    void balancePull(float h) {
        if (balance <= 0.0f) return;
        static constexpr int CORE[] = { RP_PELVIS, RP_WAIST, RP_NECK, RP_SHOULDER_L, RP_SHOULDER_R,
                                        RP_HIP_L, RP_HIP_R, RP_KNEE_L, RP_KNEE_R,
                                        RP_ANKLE_L, RP_ANKLE_R, RP_TOE_L, RP_TOE_R };
        const float lim = BALANCE_ACCEL * balance * h * h;
        for (int i : CORE) {
            glm::vec3 d = (target[i] - pos[i]) * (BALANCE_GAIN * balance);
            float l = glm::length(d);
            if (l > lim) d *= lim / l;
            pos[i] += d;
        }
    }

    // Joint limits: cones, and hinges for knees and elbows (a
    // knee only bends backward, an elbow only forward)
    void limits(const std::array<glm::mat3, BONE_COUNT>& F) {
        auto place = [&](int c, int j, glm::vec3 dir) {
            float len = glm::length(pos[c] - pos[j]);
            glm::vec3 want = pos[j] + dir * len;
            glm::vec3 d = want - pos[c];
            float wsum = invMass[c] + invMass[j];
            pos[c] += d * (invMass[c] / wsum);
            pos[j] -= d * (invMass[j] / wsum);
        };
        // chest over pelvis: the neck's direction from the waist
        {
            glm::vec3 up = F[B_ROOT][1];
            glm::vec3 a = safeNorm(pos[RP_NECK] - pos[RP_WAIST], up);
            glm::vec3 dir;
            if (cone(up, a, CONE[B_SPINE], dir)) place(RP_NECK, RP_WAIST, dir);
        }
        for (int b = B_HEAD; b < BONE_COUNT; b++) {
            int j = JOINT[b], c = CHILD[b];
            const glm::mat3& Fp = F[PARENT[b]];
            glm::vec3 r = Fp * rest[b];
            glm::vec3 a = safeNorm(pos[c] - pos[j], r);
            bool knee  = b == B_KNEE_L || b == B_KNEE_R;
            bool elbow = b == B_ELBOW_L || b == B_ELBOW_R;
            if (knee || elbow) {
                // in the parent's frame: rest points down (-y),
                // +z is forward, +x to the body's left
                glm::vec3 l = glm::transpose(Fp) * a;
                glm::vec3 k = l;
                if (knee)  { k.z = std::min(k.z, 0.1f);  k.x = std::clamp(k.x, -0.2f, 0.2f); k.y = std::min(k.y, 0.85f); }
                else       { k.z = std::max(k.z, -0.1f); k.x = std::clamp(k.x, -0.3f, 0.3f); k.y = std::min(k.y, 0.9f); }
                if (k != l) place(c, j, Fp * safeNorm(k, glm::vec3(0, -1, 0)));
            } else if (CONE[b] < 180.0f) {
                glm::vec3 dir;
                if (cone(r, a, CONE[b], dir)) place(c, j, dir);
            }
        }
    }
    // If a is more than maxDeg from r, the direction maxDeg from
    // r toward a
    static bool cone(glm::vec3 r, glm::vec3 a, float maxDeg, glm::vec3& out) {
        float c = std::clamp(glm::dot(r, a), -1.0f, 1.0f);
        float maxRad = glm::radians(maxDeg);
        if (std::acos(c) <= maxRad) return false;
        glm::vec3 axis = glm::cross(r, a);
        float l = glm::length(axis);
        if (l < 1e-6f) axis = std::fabs(r.x) < 0.9f ? glm::cross(r, glm::vec3(1, 0, 0)) : glm::cross(r, glm::vec3(0, 1, 0));
        out = glm::mat3_cast(glm::angleAxis(maxRad, glm::normalize(axis))) * r;
        return true;
    }

    // ── Collision with the voxels ─────────────────────────
    // Each point is a ball (physics/VoxelBall.h)
    bool collide(int i, const SolidFn& solid, glm::vec3& nOut) {
        return collideBall(pos[i], radius[i], solid, nOut);
    }

    // ── Helpers ───────────────────────────────────────────
    float poseError() const {
        float e = 0.0f;
        for (int i = 0; i < RP_COUNT; i++) e += glm::length(pos[i] - target[i]);
        return e / RP_COUNT;
    }
    float maxSpeed(float dt) const {
        float v = 0.0f;
        for (int i = 0; i < RP_COUNT; i++) v = std::max(v, glm::length(pos[i] - snap[i]));
        return v / std::max(dt, 1e-6f);
    }
    void handBack() {
        st = State::Animated;
        timer = 0.0f;
        strength = balance = 1.0f;
        // renderWeight fades to 0 over FADE_TIME (step), unless
        // the ragdoll stays active
    }

    // Blend two bone matrices (both scale × rotation + move)
    static glm::mat4 blend(const glm::mat4& a, const glm::mat4& b, float t) {
        float sa = glm::length(glm::vec3(a[0])), sb = glm::length(glm::vec3(b[0]));
        glm::quat qa = glm::quat_cast(glm::mat3(a) / std::max(sa, 1e-6f));
        glm::quat qb = glm::quat_cast(glm::mat3(b) / std::max(sb, 1e-6f));
        glm::quat q = glm::slerp(qa, qb, t);
        float s = sa + (sb - sa) * t;
        glm::mat4 m = glm::mat4(glm::mat3_cast(q) * s);
        m[3] = glm::vec4(glm::mix(glm::vec3(a[3]), glm::vec3(b[3]), t), 1.0f);
        return m;
    }
};
