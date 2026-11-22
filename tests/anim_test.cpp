// Headless checks for animation clips (character/AnimClips.h):
// the converted Quaternius packs load, and retargeting puts our
// skeleton where the source skeleton is.
//
// Run from the repo root (it reads data/animations).
#include "character/AnimClips.h"
#include "character/CharacterAnimator.h"
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

// Direction of a bone (model space) for a set of joint angles
static glm::vec3 dirOf(const CharacterModel& m, const Pose& p, int bone, glm::vec3 local) {
    std::array<glm::mat4, BONE_COUNT> M;
    boneMatrices(m, p, M);
    return glm::normalize(glm::vec3(M[bone] * glm::vec4(local, 0.0f)));
}
static glm::vec3 posOf(const CharacterModel& m, const Pose& p, int bone) {
    std::array<glm::mat4, BONE_COUNT> M;
    boneMatrices(m, p, M);
    return glm::vec3(M[bone][3]);
}

int main() {
    int n = gAnims.load("data/animations", "data/animations/retarget.toml");
    std::printf("loaded\n");
    check(n >= 80, "both Quaternius packs load (" + std::to_string(n) + " clips)");
    for (int b = 0; b < BONE_COUNT; b++) if (!gAnims.drives(b)) check(false, "bone " + std::to_string(b) + " is driven");
    CharacterModel model = CharacterModel::build(Appearance{});
    const glm::vec3 DOWN(0, -1, 0);

    std::printf("T-pose\n");
    {
        int c = gAnims.find("ual1/A_TPose");
        check(c >= 0, "the T-pose clip is there");
        Pose p;
        gAnims.sample(c, 0.0f, model, p.rot, p.rootOffset);
        glm::vec3 l = dirOf(model, p, B_SHOULDER_L, DOWN), r = dirOf(model, p, B_SHOULDER_R, DOWN);
        std::printf("    left arm %.2f %.2f %.2f, right arm %.2f %.2f %.2f\n", l.x, l.y, l.z, r.x, r.y, r.z);
        check(l.x > 0.95f && r.x < -0.95f, "our arms stand straight out to the sides");
        glm::vec3 t = dirOf(model, p, B_HIP_L, DOWN);
        check(t.y < -0.95f, "legs straight down");
        check(glm::length(p.rootOffset) < 0.5f, "hips where they rest");
    }

    std::printf("idle and walk\n");
    {
        int idle = gAnims.find("Idle_Loop");
        Pose p;
        gAnims.sample(idle, 0.5f, model, p.rot, p.rootOffset);
        glm::vec3 l = dirOf(model, p, B_SHOULDER_L, DOWN);
        check(l.y < -0.8f, "idle: arms hang down (" + std::to_string(l.y) + ")");
        int walk = gAnims.find("Walk_Loop");
        check(walk >= 0 && gAnims.info(walk).loop, "walk is a loop");
        float dur = gAnims.info(walk).duration;
        float maxAhead = -1, minAhead = 1, bothSame = 0;
        for (int i = 0; i < 20; i++) {
            Pose q;
            gAnims.sample(walk, dur * i / 20.0f, model, q.rot, q.rootOffset);
            glm::vec3 leg = dirOf(model, q, B_HIP_L, DOWN), arm = dirOf(model, q, B_SHOULDER_L, DOWN);
            maxAhead = std::max(maxAhead, leg.z); minAhead = std::min(minAhead, leg.z);
            bothSame += (leg.z * arm.z > 0.02f) ? 1 : 0;
        }
        check(maxAhead > 0.2f && minAhead < -0.1f, "the left leg swings forward and back");
        check(bothSame < 6, "the left arm swings against the left leg");
        Pose a, b;
        gAnims.sample(walk, 0.0f, model, a.rot, a.rootOffset);
        gAnims.sample(walk, dur, model, b.rot, b.rootOffset);
        float d = 0; for (int i = 0; i < BONE_COUNT; i++) d = std::max(d, glm::length(a.rot[i] - b.rot[i]));
        check(d < 3.0f, "it loops back to where it started");
    }

    std::printf("sword\n");
    {
        int c = gAnims.find("Sword_Regular_A");
        check(c >= 0 && !gAnims.info(c).loop && gAnims.info(c).category == "Combat", "a sword attack, one-shot, in Combat");
        float dur = gAnims.info(c).duration;
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i <= 30; i++) {
            Pose p;
            gAnims.sample(c, dur * i / 30.0f, model, p.rot, p.rootOffset);
            glm::vec3 hand = posOf(model, p, B_WRIST_R);
            lo = std::min(lo, hand.x); hi = std::max(hand.x, hi);
        }
        check(hi - lo > 10.0f, "the sword hand sweeps across the body (" + std::to_string(hi - lo) + " in)");
    }

    // ── Hit reactions (CharacterAnimator::reactions) ──
    std::printf("hit reactions\n");
    {
        check(gAnimMap.load("data/animations/anim_map.toml"), "anim_map.toml loads");
        check(gAnimMap.stagger >= 0 && gAnimMap.getUp >= 0 && gAnimMap.knockdown >= 0 && gAnimMap.guardBreak >= 0,
              "the reaction clips are there (stagger, get up, knockdown, guard break)");
        const float DT = 1.0f / 60.0f;
        // facing +X (yaw 0): a hit from in front pushes toward -X
        auto settle = [&](CharacterAnimator& an, AnimInput& in) { for (int i = 0; i < 60; i++) an.update(model, in, DT); };
        {
            CharacterAnimator an; AnimInput in; settle(an, in);
            float rest = an.current().rot[B_SPINE].x;
            in.hurtCount = 1; in.hurtDir = glm::vec3(-1, 0, 0); in.hurtOffset = glm::vec3(0, 4.0f, 0); in.hurtSize = 1.0f;
            float most = 0.0f;
            for (int i = 0; i < 12; i++) { an.update(model, in, DT); most = std::min(most, an.lean().x); }
            std::printf("    a hit from in front: leans back %.1f deg (spine %.1f -> %.1f)\n", -most, rest, an.current().rot[B_SPINE].x);
            check(most < -6.0f && most > -25.0f, "a hit from in front leans the body back, a little");
            for (int i = 0; i < 60; i++) an.update(model, in, DT);
            check(std::fabs(an.lean().x) < 1.0f, "and it settles within a second");
        }
        {
            CharacterAnimator an; AnimInput in; settle(an, in);
            // pushed toward the body's left (model +X = world -Z at yaw 0)
            in.hurtCount = 1; in.hurtDir = glm::vec3(0, 0, -1); in.hurtSize = 1.0f;
            float most = 0.0f;
            for (int i = 0; i < 12; i++) { an.update(model, in, DT); most = std::min(most, an.lean().z); }
            check(most < -5.0f, "a push to the left tips the body to the left (roll " + std::to_string(most) + ")");
            // the same count again: no new kick
            for (int i = 0; i < 90; i++) an.update(model, in, DT);
            check(glm::length(an.lean()) < 1.0f, "no kick while the count stays the same");
        }
        {
            // a stumble: the clip shows (differs from the stance), gone at the end
            CharacterAnimator base, an; AnimInput in; in.combat = true;
            settle(base, in); settle(an, in);
            in.react = React::Stumble; in.reactDir = glm::vec3(-1, 0, 0);
            const float full = 0.65f;
            float most = 0.0f;
            for (int i = 0; i * DT < full; i++) {
                in.reactAge = i * DT; in.reactLeft = full - in.reactAge;
                const Pose& p = an.update(model, in, DT);
                AnimInput b2 = in; b2.react = React::None;
                const Pose& q = base.update(model, b2, DT);
                for (int b = 0; b < BONE_COUNT; b++) most = std::max(most, glm::length(CharacterAnimator::angleDiff(p.rot[b], q.rot[b])));
            }
            check(most > 8.0f, "a stumble plays its clip (" + std::to_string(most) + " deg off the stance)");
            in.react = React::None; in.reactAge = 0.0f; in.reactLeft = 0.0f;
            for (int i = 0; i < 30; i++) { an.update(model, in, DT); base.update(model, in, DT); }
            float after = 0.0f;
            for (int b = 0; b < BONE_COUNT; b++) after = std::max(after, glm::length(CharacterAnimator::angleDiff(an.current().rot[b], base.current().rot[b])));
            check(after < 2.0f, "and back to the stance after (" + std::to_string(after) + ")");
        }
        {
            // getting up off the back: lying at the start, standing at the end
            CharacterAnimator an; AnimInput in; settle(an, in);
            in.getUp = 0.0f; in.facingYawDeg = 90.0f;
            const Pose& p0 = an.update(model, in, DT);
            float lowY = posOf(model, p0, B_HEAD).y;
            check(std::fabs(p0.bodyYaw - 90.0f) < 0.5f, "it turns to lie the way the body lies at once");
            for (int i = 0; i <= 66; i++) { in.getUp = std::min(1.0f, i / 66.0f); an.update(model, in, DT); }
            float highY = posOf(model, an.current(), B_HEAD).y;
            std::printf("    getting up: head %.1f in -> %.1f in\n", lowY, highY);
            check(lowY < model.heightIn * 0.35f, "it starts lying down");
            check(highY > model.heightIn * 0.75f, "and ends on its feet");
        }
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
