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

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
