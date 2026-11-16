#pragma once
#include "CharacterModel.h"
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// AnimClips — animation clips, played on our skeleton
// ══════════════════════════════════════════════════════════
// Clips come from animation packs converted to ozz-animation
// files (data/animations/<pack>/: skeleton.ozz and one .ozz per
// clip; tools/import_animations.md says how to add a pack). They
// are sampled with ozz and RETARGETED onto our 15-bone skeleton
// by data/animations/retarget.toml: each of our bones follows a
// joint of the pack's skeleton, as a rotation from the pack's
// rest pose, lined up with the pack bone's direction at rest
// (so a T-pose pack drives our arms-down skeleton correctly).
//
// The result is the same joint angles the procedural animator
// writes (CharacterAnimator's Pose: degrees per bone, Y·X·Z), so
// everything built on poses — the ragdoll, the held items, the
// attack overlays — works on clips unchanged.
//
// No ozz types in this header: ozz is only in AnimClips.cpp.
struct ClipInfo {
    std::string id;          // "<pack>/<name>", unique
    std::string name;        // as in the pack: "Sword_Regular_A"
    std::string pack;        // "ual1"
    std::string category;    // for the viewer: Locomotion, Combat, …
    float duration = 0.0f;   // s
    bool  loop = false;      // a cycle (names ending in _Loop)
};

class AnimLibrary {
public:
    AnimLibrary();
    ~AnimLibrary();

    // Every pack folder under 'dir' (one with a skeleton.ozz);
    // retarget: the bone map (retarget.toml). Returns clips loaded.
    int load(const std::string& dir, const std::string& retarget);

    int count() const;
    const ClipInfo& info(int clip) const;
    int find(const std::string& idOrName) const;      // -1: none

    // The clip's pose at 'time' (s; wrapped if it loops, else
    // held at the end) on this body: joint angles for the bones
    // the retarget table maps (others untouched), and the root's
    // offset (inches, our scale). False if the clip doesn't exist.
    bool sample(int clip, float time, const CharacterModel& model,
                std::array<glm::vec3, BONE_COUNT>& rot, glm::vec3& rootOffset) const;
    // Which of our bones the clips drive
    bool drives(int bone) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

extern AnimLibrary gAnims;
