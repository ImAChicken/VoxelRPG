#pragma once
#include "../character/CharacterModel.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// ══════════════════════════════════════════════════════════
// Combat — a souls-like moveset, for any body
// ══════════════════════════════════════════════════════════
// Every Human (the player, an enemy) has the same attacks. A
// controller (keyboard, AI, network) only asks for them through
// HumanIntent (attackLight / attackHeavy / guard); physics runs
// them (VoxelPhysics::moveHuman: movement, timing, stamina) and
// the hits are found by combat::resolveHits (game/CombatHits.h)
// for every body against every other, the same for all.
//
// Each attack is committed: a WIND-UP, an ACTIVE window when it
// can hit, then RECOVERY. Only late in recovery can a dodge
// cancel it. A light press once a light swing is active is
// remembered, and the next swing of the combo starts as soon as
// the first allows (comboFrom), cutting its recovery short.
//
//   Light 1     a right-to-left slash, a step in
//   Light 2     the backhand, left to right (chains from light 1)
//   Heavy       an overhead chop: slow, hits hard, knocks down,
//               and doesn't flinch from light hits once wound up
//   Air light   in the air: lurches forward with a wide slash
//   Air heavy   in the air: hangs a moment, plunges straight
//               down, and slams the ground: hits all around
//
// A hit lands on a body that is inside the swing's reach, in
// the arc the blade sweeps during that tick, at the right
// height, with nothing solid in between. Damage is the
// weapon's (items.toml damage) times the attack's. The knock
// goes to the ragdoll (light hits stagger, heavy ones knock
// down) and interrupts the target's own attack unless it has
// poise. Guarding (held) in front of the hit blocks most of it
// at the cost of stamina; out of stamina, the guard breaks.
// Rolls and dodges are invulnerable for most of their length.
//
// Angles: degrees around the attacker's facing, positive to its
// left. Heights: voxels above its feet. Knock: speed it would
// give a 70 kg body (vox/s).
enum class AttackKind : uint8_t { None, Light1, Light2, Heavy, AirLight, AirHeavy };

struct AttackDef {
    float duration;               // s, wind-up + active + recovery
    float activeStart, activeEnd; // s: the hit window
    float damage;                 // × the weapon's damage
    float stamina;                // cost
    float reach;                  // vox from the body's middle line
    float arcFrom, arcTo;         // degrees, swept over the active window
    float yLo, yHi;               // band it hits, above the feet
    float knock;                  // vox/s on a 70 kg body
    float lift;                   // share of the knock that goes up
    float lunge;                  // ground: forward speed while it winds up and swings (vox/s)
    float lungeFrom, lungeTo;     // s
    float comboFrom;              // s: a queued light press chains from here (< 0: no chain);
                                  //    presses are taken (buffered) from activeStart on
    float cancelFrom;             // s: a dodge from here cancels it
    float poiseFrom;              // s: from here (to activeEnd) light hits don't interrupt it (< 0: never)
};

namespace combat {

// ── Tuning ────────────────────────────────────────────────
constexpr float MAX_STAMINA     = 100.0f;
constexpr float STAMINA_REGEN   = 38.0f;   // per second
constexpr float STAMINA_DELAY   = 0.7f;    // s after using it before it comes back
constexpr float ROLL_STAMINA    = 18.0f;
constexpr float BACKSTEP_STAMINA= 10.0f;
constexpr float AIRDODGE_STAMINA= 14.0f;
constexpr float GUARD_SPEED     = 0.5f;    // walking speed while guarding
constexpr float GUARD_KEEP      = 0.1f;    // damage that gets through a guard
constexpr float GUARD_COST      = 2.5f;    // stamina per point of damage blocked
constexpr float POISE           = 12.0f;   // knock (vox/s) a body shrugs off without being interrupted
constexpr float HEAVY_POISE     = 30.0f;   // … while a heavy attack has poise
constexpr float HITSTOP         = 0.06f;   // s the attacker's swing pauses on a hit
// air attacks
constexpr float LURCH_SPEED     = 46.0f;   // air light: forward
constexpr float LURCH_UP        = 10.0f;
constexpr float LURCH_GRAVITY   = 0.4f;    // gravity while lurching
constexpr float PLUNGE_HANG     = 0.18f;   // air heavy: s hanging before the plunge
constexpr float PLUNGE_SPEED    = 120.0f;  // vox/s straight down
constexpr float SLAM_AT         = 0.20f;   // s into the attack when it lands (the slam's active window follows)
// i-frames: share of the move's time that is invulnerable
constexpr float ROLL_IFRAME_FROM = 0.08f, ROLL_IFRAME_TO = 0.72f;
constexpr float BACKSTEP_IFRAME_TO = 0.6f;

inline const AttackDef& def(AttackKind k) {
    //                         dur   act0  act1  dmg  stam reach  arc0   arc1  yLo  yHi knock lift lunge l0    l1    combo cancel poise
    static const AttackDef L1{ 0.60f, 0.20f, 0.32f, 1.0f, 15, 10.0f, -70,  60,   3, 16, 14, 0.15f, 12, 0.08f, 0.32f, 0.40f, 0.38f, -1 };
    static const AttackDef L2{ 0.58f, 0.16f, 0.28f, 1.0f, 15, 10.0f,  60, -70,   3, 16, 14, 0.15f, 12, 0.04f, 0.28f, 0.38f, 0.36f, -1 };
    static const AttackDef HV{ 1.10f, 0.52f, 0.66f, 2.2f, 28, 11.0f, -20,  20,   0, 18, 40, 0.30f, 16, 0.40f, 0.66f, -1.0f, 0.80f, 0.25f };
    static const AttackDef AL{ 0.55f, 0.08f, 0.26f, 1.1f, 16, 10.0f, -80,  80,  -4, 14, 20, 0.20f,  0, 0.0f,  0.0f,  -1.0f, 0.45f, -1 };
    static const AttackDef AH{ 0.85f, 0.20f, 0.30f, 2.0f, 25, 12.0f, -180, 180, -3,  7, 42, 0.45f,  0, 0.0f,  0.0f,  -1.0f, 0.65f, 0.0f };
    static const AttackDef NONE{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, -1 };
    switch (k) {
    case AttackKind::Light1:   return L1;
    case AttackKind::Light2:   return L2;
    case AttackKind::Heavy:    return HV;
    case AttackKind::AirLight: return AL;
    case AttackKind::AirHeavy: return AH;
    default:                   return NONE;
    }
}

inline const char* name(AttackKind k) {
    switch (k) {
    case AttackKind::Light1:   return "light";
    case AttackKind::Light2:   return "light 2";
    case AttackKind::Heavy:    return "heavy";
    case AttackKind::AirLight: return "air light";
    case AttackKind::AirHeavy: return "plunge";
    default:                   return "none";
    }
}

} // namespace combat

// The attack a body is doing (on Human)
struct AttackState {
    AttackKind kind = AttackKind::None;
    float      t = 0.0f;           // s into it
    float      prevT = 0.0f;       // where it was last tick (the hit sweep runs prevT → t)
    float      stop = 0.0f;        // hit-stop left
    glm::vec3  dir{0, 0, -1};      // the way it swings (follows facing during the wind-up)
    bool       queuedLight = false;// a light press waiting to chain
    bool       plunging = false;   // air heavy: falling
    bool       landed = false;     // air heavy: has slammed
    std::vector<uint64_t> hitIds;  // bodies this swing has hit already

    bool active() const { return kind != AttackKind::None; }
    const AttackDef& def() const { return combat::def(kind); }
    bool inWindow(float a, float b) const { return t >= a && t <= b; }
};

// ── The pose of each attack ───────────────────────────────
// Upper-body joint angles (degrees) at key times, eased between,
// laid over the base pose like the axe swing (game/AxeSwing.h).
// Sword in the right hand; the plunge brings the left hand in.
namespace combatanim {

struct Key {
    float t;
    glm::vec3 spine, head, shR, elR, wrR, shL, elL;
    float hipL, kneeL, kneeR, drop;       // legs: a step, knees, how far the body sinks (inches)
};

inline const std::vector<Key>& keys(AttackKind k) {
    //                  t      spine           head          shoulder R          elbow R      wrist R       shoulder L       elbow L     hipL kneeL kneeR drop
    static const std::vector<Key> L1 = {
        { 0.00f, { 5,   0, 0}, {0,  0, 0}, { -55,   0, -15}, { -70,0,0}, {  0,  0, 0}, {-30,  0, 15}, {-60,0,0},   0,  10,  10, 0 },
        { 0.17f, {-4, -38, 0}, {0, 30, 0}, { -85, -55, -45}, { -60,0,0}, {-20,-60, 0}, {-45, 25, 25}, {-70,0,0}, -15,  25,  15, 1 },
        { 0.32f, {14,  34, 0}, {0,-24, 0}, { -80,  65, -10}, { -10,0,0}, { 30,-60, 0}, {-15,-10, 20}, {-40,0,0}, -28,  30,  18, 2 },
        { 0.60f, { 5,   0, 0}, {0,  0, 0}, { -55,   0, -15}, { -70,0,0}, {  0,  0, 0}, {-30,  0, 15}, {-60,0,0},   0,  10,  10, 0 },
    };
    static const std::vector<Key> L2 = {
        { 0.00f, {14,  30, 0}, {0,-22, 0}, { -80,  60, -10}, { -30,0,0}, { 20,-60, 0}, {-15,-10, 20}, {-40,0,0}, -20,  25,  15, 1 },
        { 0.13f, {16,  38, 0}, {0,-28, 0}, { -75,  75,  -5}, { -80,0,0}, {-10, 60, 0}, {-15,-10, 20}, {-40,0,0}, -20,  25,  15, 1 },
        { 0.28f, {-2, -36, 0}, {0, 28, 0}, { -85, -60, -50}, { -15,0,0}, { 20, 60, 0}, {-45, 25, 25}, {-70,0,0}, -28,  30,  18, 2 },
        { 0.58f, { 5,   0, 0}, {0,  0, 0}, { -55,   0, -15}, { -70,0,0}, {  0,  0, 0}, {-30,  0, 15}, {-60,0,0},   0,  10,  10, 0 },
    };
    static const std::vector<Key> HV = {
        { 0.00f, { 5,   0, 0}, {0,  0, 0}, { -55,   0, -15}, { -70,0,0}, {  0,  0, 0}, {-30,  0, 15}, {-60,0,0},   0,  10,  10, 0 },
        { 0.45f, {-14, -8, 0}, {0,  6, 0}, {-172,   0, -12}, {-105,0,0}, {-40,  0, 0}, {-165, 0, 15}, {-100,0,0}, -10,  15,  10, 0 },
        { 0.52f, {-15,-10, 0}, {0,  8, 0}, {-176,   0, -12}, {-110,0,0}, {-45,  0, 0}, {-168, 0, 15}, {-105,0,0}, -10,  15,  10, 0 },
        { 0.66f, {32,   4, 0}, {0, -4, 0}, { -38,   0, -10}, { -10,0,0}, { 45,  0, 0}, { -40, 0, 10}, {-20,0,0}, -40,  50,  35, 6 },
        { 1.10f, { 5,   0, 0}, {0,  0, 0}, { -55,   0, -15}, { -70,0,0}, {  0,  0, 0}, {-30,  0, 15}, {-60,0,0},   0,  10,  10, 0 },
    };
    static const std::vector<Key> AL = {
        { 0.00f, {10, -30, 0}, {0, 24, 0}, { -85, -55, -45}, { -60,0,0}, {-20,-60, 0}, {-45, 25, 25}, {-70,0,0}, -40,  60,  30, 0 },
        { 0.26f, {28,  36, 0}, {0,-26, 0}, { -80,  70,  -8}, { -10,0,0}, { 30,-60, 0}, {-15,-10, 20}, {-40,0,0}, -50,  70,  40, 0 },
        { 0.55f, {10,   0, 0}, {0,  0, 0}, { -55,   0, -15}, { -70,0,0}, {  0,  0, 0}, {-30,  0, 15}, {-60,0,0}, -20,  40,  20, 0 },
    };
    static const std::vector<Key> AH = {
        { 0.00f, {-8,   0, 0}, {0,  0, 0}, {-170,   0, -10}, { -60,0,0}, { 80,  0, 0}, {-170, 0, 10}, {-60,0,0}, -50,  80,  60, 0 },
        { 0.18f, {20,   0, 0}, {0, 10, 0}, { -70,   0, -10}, { -20,0,0}, { 85,  0, 0}, { -70, 0, 10}, {-20,0,0}, -30,  40,  30, 0 },
        { 0.24f, {40,   0, 0}, {0, 15, 0}, { -40,   0, -10}, { -10,0,0}, { 80,  0, 0}, { -40, 0, 10}, {-10,0,0}, -80, 110,  90, 14 },
        { 0.85f, { 5,   0, 0}, {0,  0, 0}, { -55,   0, -15}, { -70,0,0}, {  0,  0, 0}, {-30,  0, 15}, {-60,0,0},   0,  10,  10, 0 },
    };
    static const std::vector<Key> NONE;
    switch (k) {
    case AttackKind::Light1:   return L1;
    case AttackKind::Light2:   return L2;
    case AttackKind::Heavy:    return HV;
    case AttackKind::AirLight: return AL;
    case AttackKind::AirHeavy: return AH;
    default:                   return NONE;
    }
}

// Lay the attack over the joint angles; rootDrop (inches) sinks
// the body (a slam's crouch)
inline void apply(AttackKind kind, float t, std::array<glm::vec3, BONE_COUNT>& rot, float& rootDrop) {
    const auto& K = keys(kind);
    if (K.size() < 2 || t < 0.0f) return;
    size_t i = 0;
    while (i + 2 < K.size() && t > K[i + 1].t) i++;
    const Key& a = K[i];
    const Key& b = K[i + 1];
    float u = std::clamp((t - a.t) / std::max(b.t - a.t, 1e-4f), 0.0f, 1.0f);
    u = u * u * (3.0f - 2.0f * u);
    // in and out of the attack from the base pose
    const AttackDef& d = combat::def(kind);
    auto ss = [](float a0, float b0, float x) { float v = std::clamp((x - a0) / (b0 - a0), 0.0f, 1.0f); return v * v * (3 - 2 * v); };
    float w = ss(0.0f, 0.06f, t) * (1.0f - ss(d.duration - 0.18f, d.duration, t));
    auto L = [&](const glm::vec3& x, const glm::vec3& y) { return x + (y - x) * u; };
    auto put = [&](int bone, glm::vec3 v) { rot[bone] += (v - rot[bone]) * w; };
    put(B_SPINE, L(a.spine, b.spine));
    put(B_HEAD, L(a.head, b.head));
    put(B_SHOULDER_R, L(a.shR, b.shR));
    put(B_ELBOW_R, L(a.elR, b.elR));
    put(B_WRIST_R, L(a.wrR, b.wrR));
    put(B_SHOULDER_L, L(a.shL, b.shL));
    put(B_ELBOW_L, L(a.elL, b.elL));
    rot[B_HIP_L].x  += (a.hipL + (b.hipL - a.hipL) * u) * w;
    rot[B_KNEE_L].x += (a.kneeL + (b.kneeL - a.kneeL) * u) * w;
    rot[B_KNEE_R].x += (a.kneeR + (b.kneeR - a.kneeR) * u) * w;
    rootDrop += (a.drop + (b.drop - a.drop) * u) * w;
}

} // namespace combatanim
