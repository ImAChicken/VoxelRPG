#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// ══════════════════════════════════════════════════════════
// BodyMass — how heavy a body is, and where
// ══════════════════════════════════════════════════════════
// Every human body is split into 15 segments. Each segment
// carries its own flesh (a share of the body's mass, from
// standard human proportions) plus whatever gear sits on it:
// a helmet on the head, a cuirass on the thorax, a sword in
// the right hand, a sheathed weapon on the back (thorax).
//
// Used by:
//   physics knockback   a hit's speed change = impulse / total
//   the ragdoll         each point weighs its segments' share,
//                       so a heavy weapon drags the arm down and
//                       a heavy body is harder to knock over
//
// All masses are in kilograms. Positions and speeds elsewhere
// stay in voxels (1 voxel = 4 inches), so an impulse is in
// kg·voxels/s.

enum BodySeg : int {
    SEG_HEAD,
    SEG_THORAX,                         // chest and belly
    SEG_PELVIS,
    SEG_UPPER_ARM_L, SEG_FOREARM_L, SEG_HAND_L,
    SEG_UPPER_ARM_R, SEG_FOREARM_R, SEG_HAND_R,
    SEG_THIGH_L, SEG_SHANK_L, SEG_FOOT_L,
    SEG_THIGH_R, SEG_SHANK_R, SEG_FOOT_R,
    SEG_COUNT
};

// Where worn gear sits. Saved by name in items.toml
// (worn = "head" | "torso" | "hands" | "legs" | "feet" | "back").
enum class WornSlot : int { Head, Torso, Hands, Legs, Feet, Back, COUNT };
constexpr int WORN_SLOTS = (int)WornSlot::COUNT;

inline const char* wornSlotKey(WornSlot s) {
    switch (s) {
    case WornSlot::Head:  return "head";
    case WornSlot::Torso: return "torso";
    case WornSlot::Hands: return "hands";
    case WornSlot::Legs:  return "legs";
    case WornSlot::Feet:  return "feet";
    case WornSlot::Back:  return "back";
    default:              return "?";
    }
}
inline int wornSlotFromKey(const char* k) {
    for (int i = 0; i < WORN_SLOTS; i++) {
        const char* a = wornSlotKey((WornSlot)i);
        const char* b = k;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

struct BodyMass {
    // Share of body mass per segment (Winter / de Leva tables;
    // the trunk split into thorax+abdomen and pelvis). Adds up to 1.
    static constexpr std::array<float, SEG_COUNT> FRACTION = {
        0.081f,                 // head and neck
        0.355f, 0.142f,         // thorax + abdomen, pelvis
        0.028f, 0.016f, 0.006f, // left arm: upper, fore, hand
        0.028f, 0.016f, 0.006f, // right arm
        0.100f, 0.0465f, 0.0145f,   // left leg: thigh, shank, foot
        0.100f, 0.0465f, 0.0145f,   // right leg
    };

    // A 5'8" average build weighs this much
    static constexpr float BASE_KG = 68.0f;

    std::array<float, SEG_COUNT> flesh{};   // the body itself
    std::array<float, SEG_COUNT> gear{};    // what is worn or held there
    float bodyKg = BASE_KG;
    float gearKg = 0.0f;

    BodyMass() { setBody(BASE_KG); }

    float segment(int s) const { return flesh[s] + gear[s]; }
    float total()        const { return bodyKg + gearKg; }

    void setBody(float kg) {
        bodyKg = std::max(1.0f, kg);
        for (int s = 0; s < SEG_COUNT; s++) flesh[s] = bodyKg * FRACTION[s];
    }

    void clearGear() { gear.fill(0.0f); gearKg = 0.0f; }

    // Gear on one segment
    void addGear(int seg, float kg) {
        if (seg < 0 || seg >= SEG_COUNT || kg <= 0.0f) return;
        gear[seg] += kg;
        gearKg    += kg;
    }

    // Worn gear, spread over the segments the slot covers
    void addWorn(WornSlot slot, float kg) {
        switch (slot) {
        case WornSlot::Head:  addGear(SEG_HEAD, kg); break;
        case WornSlot::Torso:                       // most on the chest
            addGear(SEG_THORAX, kg * 0.6f);  addGear(SEG_PELVIS, kg * 0.2f);
            addGear(SEG_UPPER_ARM_L, kg * 0.1f); addGear(SEG_UPPER_ARM_R, kg * 0.1f);
            break;
        case WornSlot::Hands:
            addGear(SEG_HAND_L, kg * 0.35f); addGear(SEG_HAND_R, kg * 0.35f);
            addGear(SEG_FOREARM_L, kg * 0.15f); addGear(SEG_FOREARM_R, kg * 0.15f);
            break;
        case WornSlot::Legs:
            addGear(SEG_THIGH_L, kg * 0.3f); addGear(SEG_THIGH_R, kg * 0.3f);
            addGear(SEG_SHANK_L, kg * 0.2f); addGear(SEG_SHANK_R, kg * 0.2f);
            break;
        case WornSlot::Feet:  addGear(SEG_FOOT_L, kg * 0.5f); addGear(SEG_FOOT_R, kg * 0.5f); break;
        case WornSlot::Back:  addGear(SEG_THORAX, kg); break;
        default: break;
        }
    }

    // Body mass from the look (character creator):
    //   build   0 slim, 1 average, 2 sturdy
    //   height  16, 17 or 18 voxels (5'4", 5'8", 6'0")
    //   sliders -1..1 (chest 0..1); bigger shapes weigh more
    static float bodyKgFor(int build, int heightVoxels,
                           float shoulders, float chest, float hips, float limbs) {
        static constexpr float BUILD[3] = { 0.84f, 1.0f, 1.22f };
        float k = BUILD[std::clamp(build, 0, 2)];
        k *= std::pow(std::max(heightVoxels, 1) / 17.0f, 2.5f);   // taller is heavier, faster than linear
        k *= 1.0f + 0.04f * shoulders + 0.05f * chest + 0.04f * hips + 0.08f * limbs;
        return BASE_KG * k;
    }
};
