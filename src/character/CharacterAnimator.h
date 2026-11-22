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
    // ── Hit reactions (the simulation decides them: game/CombatHits.h)
    React react = React::None;         // the reaction playing
    float reactAge  = 0.0f;            // s into it
    float reactLeft = 0.0f;            // s of hitstun left
    glm::vec3 reactDir{0.0f};          // the way it was pushed (world)
    bool  reactHigh = false;           // a hit on the head
    bool  guardBroken = false;         // the reaction is a broken guard
    float exposed = 0.0f, exposedFull = 1.0f;   // parried: s left open, of how long
    // the lean spring: kicked each time hurtCount changes
    int   hurtCount = 0;
    glm::vec3 hurtDir{0.0f};           // the way the hit pushed (world)
    glm::vec3 hurtOffset{0.0f};        // where it landed, from the feet (world voxels)
    float hurtSize = 0.0f;
    float down  = -1.0f;               // s knocked down (the ragdoll lying limp), < 0: not
    bool  dead  = false;
    float getUp = -1.0f;               // 0..1 getting up off its back (the get-up clip), < 0: not
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
    m = glm::scale(m, glm::vec3(model.worldScale));                            // inches → world voxels (a giant: more)
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
        if (firstFrame || (in.getUp >= 0.0f && lastGetUp < 0.0f)) { pose.bodyYaw = target; firstFrame = false; }

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

        // ── Hit reactions ────────────────────────────────
        bool ownRoot = reactions(model, in, dt);

        // ── Inertialization: no snaps between motions ────
        // When what plays changes at once (an attack starts, a combo
        // goes to its next swing, a dodge cancels a swing, a roll
        // ends), the new pose starts where the body actually was: the
        // difference is kept as an offset that fades out over
        // INERT_TIME, so the limbs carry on into the new motion
        // instead of jumping to it
        {
            bool changed = in.attack != lastAttack || (in.attack && in.attackT + 1e-4f < lastAttackT) ||
                           in.move != lastMove || in.grounded != lastGrounded ||
                           in.react != lastReact || (in.react != React::None && in.reactAge + 1e-4f < lastReactAge) ||
                           (in.exposed > 0.0f) != lastExposed;
            // (not into a fall or a get-up: the ragdoll covers those)
            if (changed && haveLast) {
                for (int b = 0; b < BONE_COUNT; b++) inertOff[b] = angleDiff(lastOut[b], shown.rot[b]);
                inertT = 0.0f;
            }
            lastAttack = in.attack; lastAttackT = in.attackT; lastMove = in.move; lastGrounded = in.grounded;
            lastReact = in.react; lastReactAge = in.reactAge; lastExposed = in.exposed > 0.0f;
            lastGetUp = in.getUp;
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
        if (in.grounded && in.move != AnimInput::Move::Roll && !in.flying && !ownRoot) {
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
    void reset() {
        pose = shown = Pose(); phase = 0.0f; guardW = 0.0f; firstFrame = true; haveLast = false; inertT = 1.0f;
        leanA = leanV = headA = headV = glm::vec3(0.0f); haveHurt = false;
    }
    // the lean spring now (degrees: spine pitch, twist, roll), for tests
    glm::vec3 lean() const { return leanA; }

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

    // ── Hit reactions over the pose ───────────────────────
    // Two layers (Claude, 2026-11-21):
    //  1. a lean spring, kicked by every hit that lands: the body
    //     rocks away from the push and twists with where it landed,
    //     the head a beat behind, the arms flung out a little. Alone
    //     it is a flinch; under a clip it gives the clip its direction
    //  2. reaction clips (anim_map.toml [reactions]): a stumble, a
    //     broken guard, parried and open; knocked down and dead, the
    //     shape the ragdoll's limbs reach for; getting up off the back
    // Returns true when a clip places the hips itself (no foot planting)
    bool reactions(const CharacterModel& model, const AnimInput& in, float dt) {
        const AnimMap& M = gAnimMap;
        // ── 1. the lean spring ──
        if (!haveHurt) { lastHurt = in.hurtCount; haveHurt = true; }
        if (in.hurtCount != lastHurt) {
            lastHurt = in.hurtCount;
            float yaw = glm::radians(shown.bodyYaw);
            glm::vec3 F(std::cos(yaw), 0.0f, std::sin(yaw)), L(std::sin(yaw), 0.0f, -std::cos(yaw));   // model +Z, +X in the world
            glm::vec3 d = in.hurtDir;
            if (glm::length(d) > 1e-4f) d = glm::normalize(d);
            float pz = glm::dot(d, F), px = glm::dot(d, L);
            float s = (in.hurtSize > 0.0f ? in.hurtSize : 1.0f) * LEAN_KICK * M.lean;
            // where it landed against the chest: off to one side twists
            float scale = std::max(model.worldScale, 1e-3f);
            glm::vec3 off = in.hurtOffset / scale;                   // world voxels → inches
            float rz = glm::dot(off, F), rx = glm::dot(off, L);
            float chest = model.heightIn * 0.7f;
            float high = std::clamp((off.y - chest) / (model.heightIn * 0.2f), 0.0f, 1.0f);
            float twist = std::clamp((rz * px - rx * pz) / std::max(model.heightIn * 0.12f, 1.0f), -1.0f, 1.0f);
            leanV += glm::vec3(pz, twist * 0.8f, -px) * s;
            headV += glm::vec3(pz, twist * 0.5f, -px) * s * (0.5f + 0.8f * (in.reactHigh ? 1.0f : high));
        }
        // stiff and quick to settle (about 0.4 s); the head looser
        auto spring = [&](glm::vec3& a, glm::vec3& v, float w, float z) {
            for (float left = dt; left > 1e-5f; ) {
                float h = std::min(left, 1.0f / 240.0f);
                v += (-w * w * a - 2.0f * z * w * v) * h;
                a += v * h;
                left -= h;
            }
            a = glm::clamp(a, glm::vec3(-LEAN_MAX), glm::vec3(LEAN_MAX));
        };
        spring(leanA, leanV, 14.0f, 0.45f);
        spring(headA, headV, 10.0f, 0.4f);

        // ── 2. reaction clips ──
        bool ownRoot = false;
        int clip = -1; float ct = 0.0f, w = 0.0f; bool upperOnly = false, withRoot = false;
        auto ss = [](float a0, float b0, float x) { float v = std::clamp((x - a0) / std::max(b0 - a0, 1e-4f), 0.0f, 1.0f); return v * v * (3 - 2 * v); };
        if (M.useClips && gAnims.count() > 0) {
            if (in.getUp >= 0.0f && M.getUp >= 0) {
                // off its back: the get-up clip, all of it, the hips too
                float D = gAnims.info(M.getUp).duration, from = std::min(M.getUpFrom, D * 0.5f);
                clip = M.getUp; ct = from + in.getUp * (D - from); w = 1.0f; withRoot = true;
            } else if (in.down >= 0.0f) {
                // down: the shape of a fall (the ragdoll's muscles reach for it, faintly)
                int c = in.dead && M.death >= 0 ? M.death : M.knockdown;
                if (c >= 0) { clip = c; ct = std::min(in.down + (in.dead ? 0.9f : 0.2f), gAnims.info(c).duration); w = 1.0f; withRoot = true; }
            } else if (in.react != React::None && in.react != React::Knockdown) {
                float full = std::max(in.reactAge + in.reactLeft, 0.2f), a = in.reactAge;
                // a hit from behind or the side: less of the clip (it reels
                // back) and more of the spring (which leans the right way)
                float front = in.reactDir == glm::vec3(0.0f) ? 1.0f
                            : glm::dot(-glm::normalize(in.reactDir), glm::vec3(std::cos(glm::radians(shown.bodyYaw)), 0.0f, std::sin(glm::radians(shown.bodyYaw))));
                float facing = 0.45f + 0.55f * std::clamp(front, 0.0f, 1.0f);
                if (in.guardBroken && M.guardBreak >= 0) {
                    float D = gAnims.info(M.guardBreak).duration;
                    clip = M.guardBreak; ct = std::min(a / full, 1.0f) * D * 0.75f;
                    w = 1.0f - ss(full - 0.15f, full, a);
                } else if (in.react == React::Stumble || in.react == React::Launch || in.react == React::Slam) {
                    int c = in.reactHigh && M.staggerHigh >= 0 ? M.staggerHigh : M.stagger;
                    if (c >= 0) {
                        float D = gAnims.info(c).duration;
                        clip = c;
                        // in the air (launched, slammed): held reeling back
                        ct = in.react == React::Stumble ? std::min(a / full, 1.0f) * D : 0.0f;
                        w = (1.0f - ss(full - 0.15f, full, a)) * facing;
                        upperOnly = in.react != React::Stumble || !in.grounded;
                    }
                }
            } else if (in.exposed > 0.0f && M.exposed >= 0) {
                // parried: the reel played out slowly over the opening
                float full = std::max(in.exposedFull, 0.2f), a = full - in.exposed;
                clip = M.exposed; ct = std::clamp(a / full, 0.0f, 1.0f) * gAnims.info(M.exposed).duration;
                w = 1.0f - ss(full - 0.2f, full, a);
                upperOnly = !in.grounded;
            }
        }
        if (clip >= 0 && w > 0.0f) {
            std::array<glm::vec3, BONE_COUNT> r = shown.rot; glm::vec3 ro(0.0f);
            if (gAnims.sample(clip, ct, model, r, ro)) {
                for (int b = upperOnly ? B_SPINE : 0; b < BONE_COUNT; b++) {
                    if (upperOnly && b > B_WRIST_R) break;
                    if (gAnims.drives(b)) shown.rot[b] = blendRot(shown.rot[b], r[b], w);
                }
                if (withRoot) {
                    shown.rootOffset += (ro - shown.rootOffset) * w;
                    shown.spinPitch = 0.0f;
                    ownRoot = true;
                }
            }
        }
        // the spring on top (not while down: that's the ragdoll's)
        if (in.down < 0.0f && in.getUp < 0.0f) {
            shown.rot[B_SPINE] += leanA;
            shown.rot[B_HEAD]  += headA;
            float fling = std::min(glm::length(leanA), LEAN_MAX) * 0.6f;
            shown.rot[B_SHOULDER_L].z += fling; shown.rot[B_SHOULDER_R].z -= fling;
            shown.rot[B_ELBOW_L].x -= fling * 0.5f; shown.rot[B_ELBOW_R].x -= fling * 0.5f;
        } else {
            leanA = leanV = headA = headV = glm::vec3(0.0f);
        }
        return ownRoot;
    }
    static constexpr float LEAN_KICK = 230.0f;   // deg/s a size-1 hit starts the lean at (peaks near 11°)
    static constexpr float LEAN_MAX  = 35.0f;    // deg it never leans past
    glm::vec3 leanA{0.0f}, leanV{0.0f}, headA{0.0f}, headV{0.0f};
    int   lastHurt = 0;
    bool  haveHurt = false;
    React lastReact = React::None;
    float lastReactAge = 0.0f, lastGetUp = -1.0f;
    bool  lastExposed = false;

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
