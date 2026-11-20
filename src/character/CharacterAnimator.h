#pragma once
#include "CharacterModel.h"
#include "../game/AxeSwing.h"
#include "../game/Combat.h"
#include "AnimMap.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>

// ══════════════════════════════════════════════════════════
// CharacterAnimator — poses a body from what it is doing
// ══════════════════════════════════════════════════════════
// No keyframes yet: every pose is worked out from the body's
// state each frame — limbs swing on a cycle timed to the
// ground speed, and each joint eases toward its target so
// changes (start running, land, crouch) blend instead of
// snapping. Keyframed animations can replace any of these
// later without changing the model or renderer.
//
// Angles are degrees in model space (+Z forward, +X left):
//   x  pitch — negative swings a limb forward
//   y  yaw   — turn
//   z  roll  — positive tips a hanging limb toward +X

// What the body is doing this frame (filled from a Human,
// or set by hand for the character creator preview)
struct AnimInput {
    glm::vec3 velocity  = {0, 0, 0};   // voxels per second
    float facingYawDeg  = 0.0f;        // where the body faces (world yaw)
    float lookPitchDeg  = 0.0f;        // camera pitch, for the head
    bool  grounded  = true;
    bool  crouching = false;
    bool  sprinting = false;
    bool  flying    = false;
    bool  lying     = false;           // lying on its back (asleep in bed): the standing pose tipped over
    bool  combat    = false;
    bool  frozen    = false;           // double-jump / air-dodge freeze
    bool  stopped   = false;           // hit-stop: the pose holds exactly as it is
    enum class Move { None, Roll, Backstep, AirDodge } move = Move::None;
    float moveProgress = 0.0f;         // 0 → 1 through the move
    float swing = -1.0f;               // axe swing: seconds into it, < 0 = none (game/AxeSwing.h)
    bool  carryAxe = false;            // an axe in the right hand: carried head-down beside the leg
    const AttackDef* attack = nullptr;  // game/Combat.h: the attack playing (its pose)
    int   previewClip = -1;            // the animation viewer: play this clip as it is (AnimClips.h)
    float previewTime = 0.0f;          // … at this time (s)
    float attackT = 0.0f;              // seconds into it
    bool  guard   = false;             // guarding (held)
    bool  shield  = false;             // … behind a shield (its own guard clip)
    float walkSpeed    = 30.0f;        // for scaling the stride
    float sprintSpeed  = 50.0f;
};

struct Pose {
    std::array<glm::vec3, BONE_COUNT> rot{};   // degrees per joint
    glm::vec3 rootOffset = {0, 0, 0};          // inches, added to the root joint
    float bodyYaw   = 0.0f;                    // world yaw the body faces
    float spinPitch = 0.0f;                    // whole-body somersault (rolls)
    float heldTurn  = 0.0f;                    // 0..1: the held item turned for a thrust (HeldModelDef::turn)
};

// Where the model sits in the world: feet at 'feet' (voxels),
// turned to the pose's yaw, scaled from inches to voxels, with
// the roll's somersault. world bone = characterRoot × mats[b].
inline glm::mat4 characterRoot(const CharacterModel& model, const Pose& p, glm::vec3 feet) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), feet);
    m = glm::rotate(m, glm::radians(90.0f - p.bodyYaw), glm::vec3(0, 1, 0));   // model +Z forward; yaw 0 faces +X
    m = glm::scale(m, glm::vec3(0.25f));                                       // inches → world voxels
    if (p.spinPitch != 0.0f) {
        float mid = model.heightIn * 0.35f;
        m = glm::translate(m, glm::vec3(0, mid, 0));
        m = glm::rotate(m, glm::radians(p.spinPitch), glm::vec3(1, 0, 0));
        m = glm::translate(m, glm::vec3(0, -mid, 0));
    }
    return m;
}

// Joint transforms in model space (inches). mats[b] maps a
// point in bone b's part to model space.
inline void boneMatrices(const CharacterModel& m, const Pose& pose,
                         std::array<glm::mat4, BONE_COUNT>& mats) {
    for (int b = 0; b < BONE_COUNT; b++) {
        const auto& def = m.bones[b];
        glm::mat4 local = glm::translate(glm::mat4(1.0f),
                                         def.offset + (b == B_ROOT ? pose.rootOffset : glm::vec3(0)));
        const glm::vec3& r = pose.rot[b];
        local = glm::rotate(local, glm::radians(r.y), glm::vec3(0, 1, 0));
        local = glm::rotate(local, glm::radians(r.x), glm::vec3(1, 0, 0));
        local = glm::rotate(local, glm::radians(r.z), glm::vec3(0, 0, 1));
        mats[b] = def.parent < 0 ? local : mats[def.parent] * local;   // parents come first in Bone order
    }
}

// The same, placed in the world (voxels): what the ragdoll
// follows and what the renderer draws
inline void worldBoneMatrices(const CharacterModel& m, const Pose& pose, glm::vec3 feet,
                              std::array<glm::mat4, BONE_COUNT>& mats) {
    boneMatrices(m, pose, mats);
    glm::mat4 root = characterRoot(m, pose, feet);
    for (auto& M : mats) M = root * M;
}

class CharacterAnimator {
public:
    // Advance one frame and return the pose to draw
    const Pose& update(const CharacterModel& model, const AnimInput& in, float dt) {
        dt = std::clamp(dt, 0.0f, 0.1f);
        if (in.stopped && !firstFrame) return shown;     // a hit's stop: held still
        time += dt;

        // ── The animation viewer: a clip exactly as it is ──
        if (in.previewClip >= 0) {
            Pose p;
            p.bodyYaw = in.facingYawDeg;
            if (gAnims.sample(in.previewClip, in.previewTime, model, p.rot, p.rootOffset)) {
                pose = shown = p;
                firstFrame = true;
                return shown;
            }
        }
        glm::vec2 flat(in.velocity.x, in.velocity.z);
        float speed = glm::length(flat);

        // ── Turn the body toward where it faces ──────────
        float target = in.facingYawDeg;
        float diff = std::remainder(target - pose.bodyYaw, 360.0f);
        pose.bodyYaw += diff * (1.0f - std::exp(-dt * 14.0f));
        if (firstFrame) { pose.bodyYaw = target; firstFrame = false; }

        std::array<glm::vec3, BONE_COUNT> t{};    // target angles
        glm::vec3 rootOff(0.0f);
        float spin = 0.0f;
        float legLenVox = (model.thighLen + model.shinLen) / 4.0f;

        auto both = [&](Bone l, Bone r, glm::vec3 v) {      // mirror a side-to-side pose
            t[l] = v; t[r] = glm::vec3(v.x, -v.y, -v.z);
        };

        if (in.move == AnimInput::Move::Roll) {
            // ── Roll: tuck and somersault ────────────────
            spin = -360.0f * std::clamp(in.moveProgress, 0.0f, 1.0f);
            both(B_HIP_L, B_HIP_R, {-100, 0, 4});
            both(B_KNEE_L, B_KNEE_R, {130, 0, 0});
            both(B_SHOULDER_L, B_SHOULDER_R, {-70, 0, 10});
            both(B_ELBOW_L, B_ELBOW_R, {-100, 0, 0});
            t[B_SPINE] = {40, 0, 0};
            t[B_HEAD]  = {30, 0, 0};
            rootOff.y = -model.rootHeight * 0.45f;
        } else if (in.move == AnimInput::Move::Backstep) {
            float k = std::sin(std::clamp(in.moveProgress, 0.0f, 1.0f) * 3.14159f);
            t[B_SPINE] = {-12.0f * k, 0, 0};
            both(B_HIP_L, B_HIP_R, {-25.0f * k, 0, 3});
            t[B_HIP_R].x = 20.0f * k;
            both(B_KNEE_L, B_KNEE_R, {30.0f * k, 0, 0});
            both(B_SHOULDER_L, B_SHOULDER_R, {-20.0f * k, 0, 15});
            both(B_ELBOW_L, B_ELBOW_R, {-40, 0, 0});
        } else if (in.move == AnimInput::Move::AirDodge || in.frozen) {
            // ── Tucked in the air ────────────────────────
            both(B_HIP_L, B_HIP_R, {-70, 0, 6});
            both(B_KNEE_L, B_KNEE_R, {100, 0, 0});
            both(B_SHOULDER_L, B_SHOULDER_R, {-40, 0, 20});
            both(B_ELBOW_L, B_ELBOW_R, {-80, 0, 0});
            t[B_SPINE] = {20, 0, 0};
        } else if (in.flying) {
            // ── Flying: lean into the motion, legs trailing
            float lean = std::min(35.0f, speed * 0.4f);
            t[B_ROOT]  = {lean, 0, 0};
            both(B_HIP_L, B_HIP_R, {10, 0, 4});
            both(B_KNEE_L, B_KNEE_R, {25, 0, 0});
            both(B_SHOULDER_L, B_SHOULDER_R, {8, 0, 12 + 2.0f * std::sin(time * 2.0f)});
            both(B_ELBOW_L, B_ELBOW_R, {-15, 0, 0});
            t[B_HEAD] = {-lean * 0.8f, 0, 0};
            rootOff.y = std::sin(time * 1.6f) * 0.8f;
        } else if (!in.grounded) {
            // ── Jumping and falling ──────────────────────
            if (in.velocity.y > 0.0f) {
                t[B_HIP_L] = {-45, 0, 3};  t[B_KNEE_L] = {70, 0, 0};
                t[B_HIP_R] = {10, 0, -3};  t[B_KNEE_R] = {25, 0, 0};
                both(B_SHOULDER_L, B_SHOULDER_R, {-35, 0, 25});
                both(B_ELBOW_L, B_ELBOW_R, {-30, 0, 0});
            } else {
                both(B_HIP_L, B_HIP_R, {-20, 0, 6});
                both(B_KNEE_L, B_KNEE_R, {35, 0, 0});
                both(B_SHOULDER_L, B_SHOULDER_R, {-10, 0, 45});
                both(B_ELBOW_L, B_ELBOW_R, {-20, 0, 0});
            }
        } else {
            // ── On the ground: idle, walk, run, crouch ───
            // Stride: one full cycle (two steps) per
            // ~2.4 leg lengths travelled
            phase += dt * speed / (legLenVox * 2.4f) * 6.2832f;
            float move = std::clamp(speed / in.walkSpeed, 0.0f, 1.0f);
            float run  = std::clamp((speed - in.walkSpeed) / std::max(1.0f, in.sprintSpeed - in.walkSpeed), 0.0f, 1.0f);
            float amp  = move * 32.0f + run * 16.0f;
            if (in.crouching) amp *= 0.45f;
            if (in.combat)    amp *= 0.7f;
            float sp = std::sin(phase), cp = std::cos(phase);

            t[B_HIP_L] = {-amp * sp, 0, 2};
            t[B_HIP_R] = { amp * sp, 0, -2};
            t[B_KNEE_L] = {5 + amp * 1.1f * std::max(0.0f, std::sin(phase + 1.3f)), 0, 0};
            t[B_KNEE_R] = {5 + amp * 1.1f * std::max(0.0f, std::sin(phase + 1.3f + 3.1416f)), 0, 0};
            t[B_ANKLE_L] = {-amp * 0.3f * cp, 0, 0};
            t[B_ANKLE_R] = { amp * 0.3f * cp, 0, 0};

            float armAmp = amp * (0.75f + 0.25f * run);
            t[B_SHOULDER_L] = { armAmp * sp, 0, 5};
            t[B_SHOULDER_R] = {-armAmp * sp, 0, -5};
            float elbow = -10.0f - 15.0f * move - 60.0f * run;
            t[B_ELBOW_L] = {elbow, 0, 0};
            t[B_ELBOW_R] = {elbow, 0, 0};

            t[B_SPINE] = {move * 4.0f + run * 10.0f, amp * 0.25f * sp, 0};
            t[B_HEAD]  = {-(move * 4.0f + run * 10.0f), -amp * 0.2f * sp, 0};

            // Idle: breathe and sway a little
            float idle = 1.0f - move;
            t[B_SPINE].x     += idle * 1.2f * std::sin(time * 1.8f);
            t[B_SHOULDER_L].z += idle * (2.0f + 1.5f * std::sin(time * 1.8f));
            t[B_SHOULDER_R].z -= idle * (2.0f + 1.5f * std::sin(time * 1.8f));

            if (in.combat) {
                // Guard: arms up, feet apart, knees soft
                t[B_HIP_L].z += 10; t[B_HIP_R].z -= 10;
                t[B_KNEE_L].x += 18; t[B_KNEE_R].x += 18;
                t[B_HIP_L].x -= 10; t[B_HIP_R].x -= 10;
                t[B_SPINE].x += 8;
                t[B_SHOULDER_L] = {-55 + armAmp * 0.2f * sp, 0, 15};
                t[B_SHOULDER_R] = {-65 - armAmp * 0.2f * sp, 0, -10};
                t[B_ELBOW_L] = {-85, 0, 0};
                t[B_ELBOW_R] = {-75, 0, 0};
            }
            if (in.crouching) {
                // Deep and hunched, to fit the 10-voxel (40 in)
                // crouching collision body
                t[B_HIP_L].x -= 105; t[B_HIP_R].x -= 105;
                t[B_KNEE_L].x += 135; t[B_KNEE_R].x += 135;
                t[B_ANKLE_L].x -= 32; t[B_ANKLE_R].x -= 32;
                t[B_SPINE].x += 50;
                t[B_HEAD].x  -= 38;
                t[B_SHOULDER_L].x -= 20; t[B_SHOULDER_R].x -= 20;
                t[B_ELBOW_L].x -= 30; t[B_ELBOW_R].x -= 30;
            }
        }

        // ── Animation clips over the procedural targets ──
        // (data/animations/anim_map.toml; a state without a
        // clip keeps the procedural pose above)
        bool clipRoll = false;
        if (gAnimMap.useClips && gAnims.count() > 0) clipTargets(model, in, dt, speed, t, rootOff, clipRoll);
        if (clipRoll) spin = 0.0f;                       // the clip somersaults by itself
        // Lying down: the body tipped onto its back about the hips,
        // head toward where it faces from (Routine sets the feet)
        // (about +x, model +z forward: -90 tips the head back, face up)
        if (in.lying) {
            spin = -90.0f; rootOff = glm::vec3(0.0f);
            // laid out straight: arms at the sides, legs together
            for (int b = 0; b < BONE_COUNT; b++) t[b] = glm::vec3(0.0f);
            both(B_HIP_L, B_HIP_R, { 0, 0, 1 });
            both(B_KNEE_L, B_KNEE_R, { 4, 0, 0 });
            both(B_SHOULDER_L, B_SHOULDER_R, { 0, 0, 6 });
            both(B_ELBOW_L, B_ELBOW_R, { -12, 0, 0 });
            t[B_SPINE].x = 1.0f * std::sin(time * 0.9f);      // breathing, slowly
        }

        // Carrying an axe: the fist turned so the head hangs
        // forward beside the leg, the arm a little out
        if (in.carryAxe && !in.combat) {
            t[B_WRIST_R].x += 55.0f;
            t[B_SHOULDER_R].z -= 6.0f;
            t[B_ELBOW_R].x -= 10.0f;
        }

        // The head follows the camera's up/down look
        t[B_HEAD].x += std::clamp(-in.lookPitchDeg, -40.0f, 40.0f) * 0.7f;

        // ── Ease every joint toward its target ───────────
        float k = 1.0f - std::exp(-dt * 14.0f);
        for (int b = 0; b < BONE_COUNT; b++) pose.rot[b] = blendRot(pose.rot[b], t[b], k);
        pose.rootOffset += (rootOff - pose.rootOffset) * k;
        pose.spinPitch = spin;

        // ── Actions over the base pose ───────────────────
        // An axe swing (game/AxeSwing.h) is keyed, not eased:
        // it is laid over the eased pose as it is
        // A guard eases in and out; an attack (game/Combat.h)
        // is keyed like the swing
        guardW += ((in.guard ? 1.0f : 0.0f) - guardW) * (1.0f - std::exp(-dt * 16.0f));
        shown = pose;
        if (in.swing >= 0.0f) axeswing::apply(in.swing, shown.rot, in.lookPitchDeg);
        int guardClip = !gAnimMap.useClips ? -1 : (in.shield && gAnimMap.guardShield >= 0) ? gAnimMap.guardShield : gAnimMap.guard;
        if (guardW > 0.01f && guardClip >= 0) {
            std::array<glm::vec3, BONE_COUNT> g = shown.rot; glm::vec3 ro(0.0f);
            gAnims.sample(guardClip, gAnims.info(guardClip).duration * 0.5f, model, g, ro);
            for (int b = B_SPINE; b < BONE_COUNT; b++)
                if (b <= B_WRIST_R && gAnims.drives(b)) shown.rot[b] = blendRot(shown.rot[b], g[b], guardW);   // upper body only
        } else if (guardW > 0.01f) {
            // the blade held up across the body, the off hand behind it
            auto put = [&](int b, glm::vec3 v) { shown.rot[b] += (v - shown.rot[b]) * guardW; };
            put(B_SHOULDER_R, {-80, 35, -30});
            put(B_ELBOW_R,    {-95, 0, 0});
            put(B_WRIST_R,    {-10, 0, -55});
            put(B_SHOULDER_L, {-70, -20, 25});
            put(B_ELBOW_L,    {-100, 0, 0});
            put(B_SPINE,      {10, 0, 0});
        }
        // a thrust turns the held item to point along it: in over
        // the wind-up, held through the hit window, out in recovery
        shown.heldTurn = 0.0f;
        if (in.attack && in.attack->thrust) {
            float t = in.attackT, a0 = in.attack->activeStart, a1 = in.attack->activeEnd, d = in.attack->duration;
            float up = std::clamp(t / std::max(a0 * 0.8f, 1e-3f), 0.0f, 1.0f);
            float down = 1.0f - std::clamp((t - a1) / std::max((d - a1) * 0.7f, 1e-3f), 0.0f, 1.0f);
            float w = std::min(up, down);
            shown.heldTurn = w * w * (3 - 2 * w);
        }
        int atkClip = in.attack ? gAnimMap.attackClip(in.attack->id) : -1;
        if (in.attack && atkClip >= 0) {
            // the clip over the attack's duration, its marked strike
            // on the attack's hit window (AnimMap::attackClipAt),
            // eased in and out of the base pose
            float d = std::max(in.attack->duration, 1e-3f);
            float u = gAnimMap.attackClipAt(in.attack->id, in.attackT, d, in.attack->activeStart, in.attack->activeEnd);
            std::array<glm::vec3, BONE_COUNT> a = shown.rot; glm::vec3 ro(0.0f);
            gAnims.sample(atkClip, u * gAnims.info(atkClip).duration, model, a, ro);
            auto ss = [](float a0, float b0, float x) { float v = std::clamp((x - a0) / (b0 - a0), 0.0f, 1.0f); return v * v * (3 - 2 * v); };
            float w = ss(0.0f, 0.06f, in.attackT) * (1.0f - ss(d - 0.12f, d, in.attackT));
            for (int b = 0; b < BONE_COUNT; b++) if (gAnims.drives(b)) shown.rot[b] = blendRot(shown.rot[b], a[b], w);
        } else if (in.attack) {
            float drop = 0.0f;
            shown.heldTurn = 0.0f;
            combatanim::apply(in.attack->pose, in.attack->duration, in.attackT, shown.rot, drop);
            if (!in.grounded) shown.rootOffset.y -= drop;
        }

        // ── Inertialization: no snaps between motions ────
        // When what plays changes at once (an attack starts, a combo
        // goes to its next swing, a dodge cancels a swing, a roll
        // ends), the new pose starts where the body actually was: the
        // difference is kept as an offset that fades out over
        // INERT_TIME, so the limbs carry on into the new motion
        // instead of jumping to it
        {
            bool changed = in.attack != lastAttack || (in.attack && in.attackT + 1e-4f < lastAttackT) ||
                           in.move != lastMove || in.grounded != lastGrounded;
            if (changed && haveLast) {
                for (int b = 0; b < BONE_COUNT; b++) inertOff[b] = angleDiff(lastOut[b], shown.rot[b]);
                inertT = 0.0f;
            }
            lastAttack = in.attack; lastAttackT = in.attackT; lastMove = in.move; lastGrounded = in.grounded;
            inertT += dt;
            if (inertT < INERT_TIME) {
                float u = std::clamp(inertT / INERT_TIME, 0.0f, 1.0f);
                float f = 1.0f - u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);    // smootherstep down to 0
                for (int b = 0; b < BONE_COUNT; b++) shown.rot[b] += inertOff[b] * f;
            }
            lastOut = shown.rot;
            haveLast = true;
        }

        // ── Keep the feet on the ground ──────────────────
        // Lower the body until the lower foot touches down,
        // so bent knees (crouch, stride, a swing's step) never
        // float
        if (in.grounded && in.move != AnimInput::Move::Roll && !in.flying) {
            std::array<glm::mat4, BONE_COUNT> mats;
            Pose probe = shown;
            probe.rootOffset.y = 0.0f;
            boneMatrices(model, probe, mats);
            float low = std::min(mats[B_ANKLE_L][3].y, mats[B_ANKLE_R][3].y);
            float want = model.footHeight - low;                // ≤ 0 when the legs bend
            pose.rootOffset.y = shown.rootOffset.y = std::min(0.0f, want);
        }
        return shown;
    }

    const Pose& current() const { return shown; }
    void reset() { pose = shown = Pose(); phase = 0.0f; guardW = 0.0f; firstFrame = true; haveLast = false; inertT = 1.0f; }

    // a - b, each angle the short way round (degrees)
    static glm::vec3 angleDiff(const glm::vec3& a, const glm::vec3& b) {
        return glm::vec3(std::remainder(a.x - b.x, 360.0f), std::remainder(a.y - b.y, 360.0f), std::remainder(a.z - b.z, 360.0f));
    }

    // A joint rotation (degrees, applied Y then X then Z, as
    // boneMatrices does) as a quaternion, and back
    static glm::quat toQuat(const glm::vec3& r) {
        return glm::angleAxis(glm::radians(r.y), glm::vec3(0, 1, 0)) *
               glm::angleAxis(glm::radians(r.x), glm::vec3(1, 0, 0)) *
               glm::angleAxis(glm::radians(r.z), glm::vec3(0, 0, 1));
    }
    static glm::vec3 fromQuat(const glm::quat& q) {
        glm::mat3 M = glm::mat3_cast(q);
        float t1 = std::atan2(M[2][0], M[2][2]);
        float c2 = std::sqrt(M[0][1] * M[0][1] + M[1][1] * M[1][1]);
        float t2 = std::atan2(-M[2][1], c2);
        float s1 = std::sin(t1), c1 = std::cos(t1);
        float t3 = std::atan2(s1 * M[1][2] - c1 * M[1][0], c1 * M[0][0] - s1 * M[0][2]);
        return glm::degrees(glm::vec3(t2, t1, t3));
    }
    // From rotation a toward b by w (0..1), the shortest way: two
    // sets of angles for nearly the same rotation (a clip passing
    // a gimbal point) blend smoothly instead of swinging the limb
    // all the way round
    static glm::vec3 blendRot(const glm::vec3& a, const glm::vec3& b, float w) {
        if (w <= 0.0f) return a;
        if (w >= 1.0f) return b;
        return fromQuat(glm::slerp(toQuat(a), toQuat(b), w));
    }

private:
    // Targets from clips (AnimMap): idle / walk / run / sprint
    // blended by speed, crouching, the combat stance, in the
    // air, rolling. Bones the clips don't drive keep t[].
    void clipTargets(const CharacterModel& model, const AnimInput& in, float dt, float speed,
                     std::array<glm::vec3, BONE_COUNT>& t, glm::vec3& rootOff, bool& rolling) {
        const AnimMap& M = gAnimMap;
        struct Part { int clip; float w; float time; };
        std::vector<Part> parts;
        auto cycle = [&](int clip) { return clip >= 0 ? cycleT * gAnims.info(clip).duration : 0.0f; };
        bool airDodge = in.move == AnimInput::Move::AirDodge || in.frozen;
        if (in.flying || in.move == AnimInput::Move::Backstep || airDodge) return;    // procedural
        if (in.move == AnimInput::Move::Roll) {
            if (M.roll < 0) return;
            parts.push_back({ M.roll, 1.0f, std::clamp(in.moveProgress, 0.0f, 1.0f) * gAnims.info(M.roll).duration });
            rolling = true;
        } else if (!in.grounded) {
            int c = in.velocity.y > 0.0f ? M.airUp : M.airDown;
            if (c < 0) return;
            float tt = in.velocity.y > 0.0f ? gAnims.info(c).duration * 0.6f : (airT += dt);
            parts.push_back({ c, 1.0f, tt });
        } else {
            airT = 0.0f;
            // which moving clip, and how fast it plays
            int idle = in.crouching ? M.crouchIdle : (in.combat && M.combatIdle >= 0 ? M.combatIdle : M.idle);
            int a = -1, b = -1; float ra = 1, rb = 1, blend = 0;
            auto hand = [&](float at, float v) { return std::clamp((v - (at - M.blendWidth)) / (2.0f * M.blendWidth), 0.0f, 1.0f); };
            if (in.crouching) { a = M.crouchWalk; ra = M.crouchSpeed; }
            else if (speed < M.runToSprint - M.blendWidth) {
                a = M.walk; ra = M.walkSpeed; b = M.run; rb = M.runSpeed; blend = hand(M.walkToRun, speed);
            } else {
                a = M.run; ra = M.runSpeed; b = M.sprint; rb = M.sprintSpeed; blend = hand(M.runToSprint, speed);
            }
            if (a < 0 && b >= 0) { a = b; ra = rb; b = -1; }
            if (b < 0) blend = 0.0f;
            float moving = std::clamp(speed / 3.0f, 0.0f, 1.0f);       // idle below ~3 vox/s
            if (a < 0) moving = 0.0f;
            // one shared cycle so the two stay in step while blending;
            // each plays at the speed that matches the ground
            if (moving > 0.0f) {
                auto rate = [&](int c, float natural) { return std::clamp(speed / natural, 0.5f, 2.0f) / gAnims.info(c).duration; };
                float r = (1 - blend) * rate(a, ra) + (b >= 0 ? blend * rate(b, rb) : 0.0f);
                cycleT = std::fmod(cycleT + dt * r, 1.0f);
            }
            if (idle >= 0 && moving < 1.0f) parts.push_back({ idle, 1.0f - moving, idleT += dt });
            if (moving > 0.0f) {
                parts.push_back({ a, moving * (1.0f - blend), cycle(a) });
                if (b >= 0 && blend > 0.0f) parts.push_back({ b, moving * blend, cycle(b) });
            }
            if (parts.empty()) return;
        }
        // blend the parts (weights add to 1), each from the first
        std::array<glm::vec3, BONE_COUNT> acc = t; glm::vec3 accRoot(0.0f);
        float total = 0.0f;
        for (const Part& p : parts) {
            if (p.w <= 1e-4f) continue;
            std::array<glm::vec3, BONE_COUNT> r = t; glm::vec3 ro(0.0f);
            if (!gAnims.sample(p.clip, p.time, model, r, ro)) continue;
            float f = p.w / (total + p.w);
            for (int bn = 0; bn < BONE_COUNT; bn++)
                if (gAnims.drives(bn)) acc[bn] = total == 0.0f ? r[bn] : blendRot(acc[bn], r[bn], f);
            accRoot = total == 0.0f ? ro : accRoot + (ro - accRoot) * f;
            total += p.w;
        }
        if (total <= 0.0f) return;
        // keep what the procedural pose adds on top: looking up and down
        for (int bn = 0; bn < BONE_COUNT; bn++) if (gAnims.drives(bn)) t[bn] = acc[bn];
        rootOff.x = accRoot.x; rootOff.z = accRoot.z;
        if (!in.grounded || rolling) rootOff.y = accRoot.y;
    }

    float cycleT = 0.0f, idleT = 0.0f, airT = 0.0f;
    // inertialization (update): what was drawn last, the offset fading out
    static constexpr float INERT_TIME = 0.2f;
    std::array<glm::vec3, BONE_COUNT> lastOut{}, inertOff{};
    float inertT = 1.0f;
    bool  haveLast = false;
    const AttackDef* lastAttack = nullptr;
    float lastAttackT = 0.0f;
    AnimInput::Move lastMove = AnimInput::Move::None;
    bool  lastGrounded = true;
    Pose  pose;           // the eased base pose
    Pose  shown;          // with actions laid over it: what is drawn
    float phase = 0.0f, time = 0.0f;
    float guardW = 0.0f;
    bool  firstFrame = true;
};
