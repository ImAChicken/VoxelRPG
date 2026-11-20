#pragma once
#include "../character/CharacterModel.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Combat — a souls-like moveset, for any body, from data
// ══════════════════════════════════════════════════════════
// Every Human (the player, an enemy) attacks the same way. A
// controller (keyboard, AI, network) only asks through
// HumanIntent (attackLight / attackHeavy / guard); physics runs
// the attack (VoxelPhysics::moveHuman → game/CombatMoves.h:
// timing, stamina, movement) and the hits are found for every
// body against every other by game/CombatHits.h.
//
// WHAT an attack does comes from data/registry/movesets.toml:
// a weapon item names a moveset (items.toml: moveset = "sword"),
// the moveset fills the slots below with attacks, and each
// attack has its timing, damage, poise damage, stamina, reach,
// arc, knock and pose. A new weapon is a data entry (and a model
// file, data/models/held/), not code.
//
//   slot        input                    behaviour (code)
//   Light       left click, on the ground  the light chain:
//               (again during a swing → the next one)
//   Heavy       right click, on the ground
//   AirLight    left click in the air    lurches forward
//   AirHeavy    right click in the air   hangs, plunges, slams
//
// Each attack is committed: a wind-up, a hit window, recovery.
// Only a dodge from cancel_from cancels it.
//
// POISE: every body has a poise meter (Human::poise). Each hit
// takes the attack's poise damage off it. While it holds, a hit
// only flinches the body (a small ragdoll jolt) and its own
// attack goes on; when it breaks, the body staggers — or is
// knocked down, if the attack says so — and its attack is
// interrupted; then the meter refills. It also refills after a
// few seconds without hits. A hit during the attacker's hyper
// armour (armor_from) can't break its poise.
//
// Angles: degrees around the attacker's facing, positive to its
// left. Heights: voxels above its feet. Knock: vox/s given to the
// target's ragdoll.
enum class AttackKind : uint8_t { None, Light, Heavy, AirLight, AirHeavy, Cast, Shot };   // Cast: an item's (a spell's) own attack; Shot: an arrow's hit

// What a hit does to the body it lands on (DMC / Rising style: a
// regular enemy reacts to every hit). Ordered by strength: a body
// reacts only to what beats its armor (Human::armor, npcs.toml)
enum class React : uint8_t { None, Flinch, Stumble, Launch, Slam, Knockdown };
inline const char* reactName(React r) {
    switch (r) {
    case React::Flinch: return "flinch"; case React::Stumble: return "stumble"; case React::Launch: return "launch";
    case React::Slam: return "slam"; case React::Knockdown: return "knockdown"; default: return "none";
    }
}
inline React reactFromName(const std::string& s) {
    if (s == "flinch") return React::Flinch; if (s == "stumble") return React::Stumble; if (s == "launch") return React::Launch;
    if (s == "slam") return React::Slam; if (s == "knockdown") return React::Knockdown; return React::None;
}
// how strong a reaction is (armor 1 shrugs off flinches, 2 stumbles too …)
inline int reactTier(React r) {
    switch (r) {
    case React::Flinch: return 1; case React::Stumble: return 2; case React::Launch: case React::Slam: return 3;
    case React::Knockdown: return 4; default: return 0;
    }
}

struct AttackDef {
    std::string id, pose;
    std::string effect;                  // what happens when the active window opens instead of a blade hit ("windburst")
    float effectRange = 20.0f, effectArc = 60.0f;   // effect: voxels, half-angle in degrees
    // A projectile effect (ice_spike): what it does when it hits a
    // body, how it flies, and the structure it leaves where it hits
    // the ground (structures.toml)
    float effectDamage = 0.0f, effectPoise = 0.0f, effectKnock = 0.0f;
    float effectSpeed = 60.0f, effectGravity = 20.0f;   // vox/s, vox/s²
    std::string effectPlace;
    // blade: the swing hits too (an effect usually replaces it; a
    // boss's slam does both); effect_react: what the effect does to
    // a body it hits (a shockwave)
    bool  blade = true;
    React effectReact = React::None;
    float duration = 0.5f;               // s, wind-up + active + recovery
    float activeStart = 0.2f, activeEnd = 0.3f;   // s: the hit window (all of them, when there are several)
    // several hits in one swing (a spin, a double thrust): each window
    // can hit the same body again. Empty: just [activeStart, activeEnd]
    std::vector<glm::vec2> windows;
    // what a hit does to a body that reacts (None: by kind: a light
    // flinches, a heavy stumbles, an air heavy slams); hitstun: s the
    // body is stuck in it (< 0: the reaction's own, REACT_STUN)
    React react = React::None;
    float hitstun = -1.0f;
    float damage = 1.0f;                 // × the weapon's damage
    float poise = 10.0f;                 // poise damage
    float stamina = 10.0f;               // cost
    float reach = 10.0f;                 // vox from the body's middle line
    float arcFrom = -60.0f, arcTo = 60.0f;   // degrees, swept over the active window
    float yLo = 0.0f, yHi = 16.0f;       // band it hits, above the feet
    float knock = 14.0f;                 // vox/s to the target's ragdoll when it staggers
    float lift = 0.15f;                  // share of the knock that goes up
    bool  knockdown = false;             // a poise break knocks down
    float lunge = 0.0f;                  // ground: forward speed during the lunge (vox/s) (old: step says it better)
    float lungeFrom = 0.0f, lungeTo = 0.0f;
    // Root motion: the step the attack carries the body, voxels over
    // lunge_time, fast at first and stopping sharply (step_curve: the
    // higher, the sharper). With a target in front it stretches or
    // shortens (up to STEP_STRETCH ×) to stop just inside its reach.
    // < 0: from lunge × lunge_time
    float step = -1.0f;
    float stepCurve = 1.5f;
    float hitstop = -1.0f;               // s both bodies freeze on a hit (< 0: by kind, HITSTOP_*)
    float track = 720.0f;                // deg/s it turns toward its target while winding up; locked from the hit window
    // a heavy held: the wind-up stops at charge_at while the button is
    // held, for up to charge_time s; a full charge gives these ×
    float chargeAt = -1.0f, chargeTime = 1.0f;
    float chargeDamage = 1.6f, chargePoise = 1.6f;
    float comboFrom = -1.0f;             // s: a queued light press chains from here (< 0: no chain);
                                         //    presses are taken (buffered) from activeStart on
    float cancelFrom = 0.4f;             // s: a dodge cancels it from here
    float armorFrom = -1.0f;             // s: hyper armour from here to activeEnd (< 0: none)
    bool  thrust = false;                // grip = "thrust": the held item turns to point along the thrust

    float stepDistance() const { return step >= 0.0f ? step : lunge * std::max(0.0f, lungeTo - lungeFrom); }
    // the hit windows, always at least one
    int windowCount() const { return windows.empty() ? 1 : (int)windows.size(); }
    glm::vec2 window(int i) const { return windows.empty() ? glm::vec2(activeStart, activeEnd) : windows[(size_t)i]; }
};

struct Moveset {
    std::string id;
    float refWeight = 1.5f;
    std::vector<const AttackDef*> light;
    const AttackDef* heavy = nullptr;
    const AttackDef* airLight = nullptr;
    const AttackDef* airHeavy = nullptr;
    // attacks with no button: an NPC picks one by name
    // (HumanIntent::special: a boss's charge)
    std::vector<const AttackDef*> specials;
    const AttackDef* special(const std::string& id) const {
        for (const AttackDef* a : specials) if (a->id == id) return a;
        return nullptr;
    }

    const AttackDef* get(AttackKind k, int chain = 0) const {
        switch (k) {
        case AttackKind::Light: return light.empty() ? nullptr : light[(size_t)chain % light.size()];
        case AttackKind::Heavy: return heavy;
        case AttackKind::AirLight: return airLight;
        case AttackKind::AirHeavy: return airHeavy;
        default: return nullptr;
        }
    }
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
constexpr float BOW_MOVE        = 0.4f;    // × walking speed while a bow is drawn or loosing (VoxelPhysics)
constexpr float GUARD_COST      = 2.5f;    // stamina per point of damage blocked
constexpr float HITSTOP         = 0.06f;   // s the attacker's swing pauses on a hit (old default)
// hit-stop: both bodies freeze this long on a hit (an attack's
// 'hitstop' overrides); a block shows less, a full charge more
constexpr float HITSTOP_LIGHT   = 0.07f;
constexpr float HITSTOP_HEAVY   = 0.13f;
constexpr float HITSTOP_BLOCKED = 0.6f;    // × on a block
constexpr float HITSTOP_PARRY   = 0.16f;
// knockback: a hit the target's poise holds still shoves it, a
// quick slide (the ragdoll is for a poise break); SHOVE × the
// attack's knock = the shove's speed (vox/s), easing out over
// SHOVE_TIME (it covers about speed / SHOVE_DECAY voxels)
constexpr float SHOVE           = 4.5f;
constexpr float SHOVE_DECAY     = 10.0f;
constexpr float SHOVE_TIME      = 0.25f;
constexpr float SHOVE_BLOCKED   = 0.6f;    // × behind a guard
// root motion
constexpr float STEP_STRETCH    = 1.6f;    // a step toward a target goes up to this × its own length
constexpr float AIM_CONE        = 0.5f;    // cos: a player's attack takes a target within ±60° of its facing
constexpr float NPC_TRACK       = 0.6f;    // × how fast an NPC's swing turns (they read you less perfectly)
// an NPC's step stops this far into its reach (x reach), never stretched
// (Simulation::aimAttacks): a crowd round you doesn't pile into you
constexpr float NPC_STOP = 0.8f;
// sprinting into an attack: its speed carries on, fading at this rate (1/s)
constexpr float CARRY_DECAY     = 3.5f;
constexpr float CARRY_KEEP      = 0.9f;
// parry: a hit that lands within PARRY_WINDOW of raising the guard
constexpr float PARRY_WINDOW    = 0.18f;   // s after the guard goes up
constexpr float GUARD_FACING    = 0.35f;   // cos: a weapon's guard covers ±70° in front
constexpr float PARRY_KNOCK     = 20.0f;   // vox/s the parried attacker is thrown back
constexpr float EXPOSED_TIME    = 1.1f;    // s the parried attacker is open (no attacks, no guard)
constexpr float RIPOSTE_DAMAGE  = 2.5f;    // × damage of a hit on an exposed body (and it falls)
// poise
constexpr float POISE_BASE      = 30.0f;   // a body's poise with nothing worn
constexpr float POISE_PER_KG    = 1.5f;    // … plus this per kg of worn gear
constexpr float POISE_DELAY     = 3.0f;    // s without hits before it refills
constexpr float POISE_REFILL    = 40.0f;   // per second, once refilling
constexpr float FLINCH_KNOCK    = 5.0f;    // vox/s a hit gives a body whose poise holds
constexpr float STAGGER_MIN     = 14.0f;   // a poise break moves the body at least this much…
constexpr float KNOCKDOWN_KNOCK = 42.0f;   // …and a knockdown at least this (above Ragdoll::KNOCKDOWN_DV)
// air attacks
constexpr float LURCH_SPEED     = 46.0f;   // air light: forward
constexpr float LURCH_UP        = 10.0f;
constexpr float LURCH_GRAVITY   = 0.4f;    // gravity while lurching
constexpr float PLUNGE_HANG     = 0.18f;   // air heavy: s hanging before the plunge
constexpr float PLUNGE_SPEED    = 120.0f;  // vox/s straight down
constexpr float SLAM_AT         = 0.20f;   // s into the attack when it lands (the slam's window follows)
// reactions (React): s stuck in each, and how fast it throws the body
constexpr float REACT_STUN[6]  = { 0.0f, 0.38f, 0.65f, 0.9f, 0.8f, 0.0f };   // by React
constexpr float REACT_SHOVE[6] = { 0.0f, 16.0f, 30.0f, 6.0f, 0.0f, 0.0f };   // vox/s along the hit
constexpr float REACT_SLIDE[6] = { 0.0f, 0.2f, 0.45f, 0.0f, 0.0f, 0.0f };    // s it slides before it can stop
constexpr float REACT_DECAY    = 5.0f;     // 1/s: how fast a reaction's slide slows (a flinch ~3 voxels, a stumble ~6)
constexpr float LAUNCH_HEIGHT  = 16.0f;    // vox a launch throws a body up
constexpr float JUGGLE_POP     = 14.0f;    // vox/s up when hit in the air (kept aloft)
constexpr int   JUGGLE_MAX     = 5;        // air hits before it falls regardless
constexpr float JUGGLE_GRAVITY = 0.55f;    // × gravity on a body stuck in the air
constexpr float SLAM_DOWN      = 90.0f;    // vox/s a slam drives an airborne body down
constexpr float SLAM_BOUNCE    = 18.0f;    // vox/s it bounces back up off the ground
constexpr float PLAYER_STUN    = 0.65f;    // × hitstun on a player (they get control back sooner)
constexpr float WALL_SPEED     = 20.0f;    // vox/s: a body knocked into a wall this fast is pinned
constexpr float WALL_PIN       = 0.6f;     // s pinned to it
constexpr float REACT_WOBBLE   = 0.55f;    // × the ragdoll's knockdown speed: how hard a reaction shakes the limbs
// getting up: s untouchable once on its feet
constexpr float GETUP_GRACE    = 0.6f;
// a perfect dodge: a hit that would land in the first this-many s of a
// roll or backstep slows the attacker (and foes near) for PERFECT_SLOW s
constexpr float PERFECT_WINDOW = 0.18f;
constexpr float PERFECT_SLOW   = 1.6f;
constexpr float PERFECT_RATE   = 0.3f;     // × their time while slowed
constexpr float PERFECT_RADIUS = 40.0f;
// i-frames: share of the move's time that is invulnerable
constexpr float ROLL_IFRAME_FROM = 0.08f, ROLL_IFRAME_TO = 0.72f;
constexpr float BACKSTEP_IFRAME_TO = 0.6f;

// How a weapon's weight bends its moveset: heavier than the
// moveset's ref_weight → slower, dearer, harder on poise
struct WeightScales { float speed = 1.0f, stamina = 1.0f, poise = 1.0f; };
inline WeightScales weightScales(const Moveset& m, float weightKg) {
    float d = weightKg - m.refWeight;
    WeightScales w;
    w.speed   = std::clamp(1.0f - 0.06f * d, 0.6f, 1.3f);
    w.stamina = std::clamp(1.0f + 0.12f * d, 0.5f, 2.0f);
    w.poise   = std::clamp(1.0f + 0.15f * d, 0.5f, 2.5f);
    return w;
}

inline const char* kindName(AttackKind k) {
    switch (k) {
    case AttackKind::Light:    return "light";
    case AttackKind::Heavy:    return "heavy";
    case AttackKind::AirLight: return "air light";
    case AttackKind::AirHeavy: return "plunge";
    case AttackKind::Cast:     return "cast";
    case AttackKind::Shot:     return "shot";
    default:                   return "none";
    }
}

// ── The registry (movesets.toml) ──────────────────────────
class MovesetRegistry {
public:
    bool load(const char* path) {
        std::unordered_map<std::string, std::unique_ptr<AttackDef>> atk;
        std::unordered_map<std::string, std::unique_ptr<Moveset>> sets;
        try {
            auto tbl = toml::parse_file(path);
            auto pair = [](const toml::node_view<toml::node>& n, float& a, float& b) {
                if (auto arr = n.as_array(); arr && arr->size() == 2) {
                    a = (float)arr->get(0)->value_or(0.0); b = (float)arr->get(1)->value_or(0.0);
                }
            };
            if (auto arr = tbl["attack"].as_array())
                for (auto& e : *arr) {
                    auto* t = e.as_table(); if (!t) continue;
                    auto d = std::make_unique<AttackDef>();
                    toml::table& v = *t;
                    d->id = v["id"].value_or(std::string());
                    d->pose = v["pose"].value_or(d->id);
                    d->duration = (float)v["duration"].value_or(0.5);
                    pair(v["active"], d->activeStart, d->activeEnd);
                    if (auto* hw = v["hits"].as_array()) {
                        for (auto& w : *hw)
                            if (auto* p = w.as_array(); p && p->size() == 2)
                                d->windows.push_back({ (float)p->get(0)->value_or(0.0), (float)p->get(1)->value_or(0.0) });
                        std::sort(d->windows.begin(), d->windows.end(), [](glm::vec2 a, glm::vec2 b) { return a.x < b.x; });
                        if (!d->windows.empty()) { d->activeStart = d->windows.front().x; d->activeEnd = d->windows.back().y; }
                    }
                    d->react = reactFromName(v["react"].value_or(std::string()));
                    d->hitstun = (float)v["hitstun"].value_or(-1.0);
                    d->damage = (float)v["damage"].value_or(1.0);
                    d->poise = (float)v["poise"].value_or(10.0);
                    d->stamina = (float)v["stamina"].value_or(10.0);
                    d->reach = (float)v["reach"].value_or(10.0);
                    pair(v["arc"], d->arcFrom, d->arcTo);
                    pair(v["band"], d->yLo, d->yHi);
                    d->knock = (float)v["knock"].value_or(14.0);
                    d->lift = (float)v["lift"].value_or(0.15);
                    d->knockdown = v["knockdown"].value_or(false);
                    d->lunge = (float)v["lunge"].value_or(0.0);
                    pair(v["lunge_time"], d->lungeFrom, d->lungeTo);
                    pair(v["step_time"], d->lungeFrom, d->lungeTo);
                    d->step = (float)v["step"].value_or(-1.0);
                    d->stepCurve = (float)v["step_curve"].value_or(1.5);
                    d->hitstop = (float)v["hitstop"].value_or(-1.0);
                    d->track = (float)v["track"].value_or(720.0);
                    pair(v["charge"], d->chargeAt, d->chargeTime);
                    if (!v["charge"]) d->chargeAt = -1.0f;
                    d->chargeDamage = (float)v["charge_damage"].value_or(1.6);
                    d->chargePoise = (float)v["charge_poise"].value_or(1.6);
                    d->comboFrom = (float)v["combo_from"].value_or(-1.0);
                    d->cancelFrom = (float)v["cancel_from"].value_or((double)d->duration);
                    d->armorFrom = (float)v["armor_from"].value_or(-1.0);
                    d->thrust = v["grip"].value_or(std::string()) == "thrust";
                    d->effect = v["effect"].value_or(std::string());
                    d->effectRange = (float)v["effect_range"].value_or(20.0);
                    d->effectArc = (float)v["effect_arc"].value_or(60.0);
                    d->effectDamage = (float)v["effect_damage"].value_or(0.0);
                    d->effectPoise = (float)v["effect_poise"].value_or(0.0);
                    d->effectKnock = (float)v["effect_knock"].value_or(0.0);
                    d->effectSpeed = (float)v["effect_speed"].value_or(60.0);
                    d->effectGravity = (float)v["effect_gravity"].value_or(20.0);
                    d->effectPlace = v["effect_place"].value_or(std::string());
                    d->blade = v["blade"].value_or(d->effect.empty());
                    d->effectReact = reactFromName(v["effect_react"].value_or(std::string()));
                    if (d->id.empty()) { std::cerr << path << ": an attack without an id\n"; continue; }
                    atk[d->id] = std::move(d);
                }
            auto ref = [&](const std::string& set, const std::string& id) -> const AttackDef* {
                if (id.empty()) return nullptr;
                auto it = atk.find(id);
                if (it == atk.end()) { std::cerr << path << ": moveset " << set << ": no attack '" << id << "'\n"; return nullptr; }
                return it->second.get();
            };
            if (auto arr = tbl["moveset"].as_array())
                for (auto& e : *arr) {
                    auto* t = e.as_table(); if (!t) continue;
                    toml::table& v = *t;
                    auto m = std::make_unique<Moveset>();
                    m->id = v["id"].value_or(std::string());
                    m->refWeight = (float)v["ref_weight"].value_or(1.5);
                    if (auto l = v["light"].as_array())
                        for (auto& x : *l) if (const AttackDef* a = ref(m->id, x.value_or(std::string()))) m->light.push_back(a);
                    m->heavy = ref(m->id, v["heavy"].value_or(std::string()));
                    m->airLight = ref(m->id, v["air_light"].value_or(std::string()));
                    m->airHeavy = ref(m->id, v["air_heavy"].value_or(std::string()));
                    if (auto l = v["specials"].as_array())
                        for (auto& x : *l) if (const AttackDef* a = ref(m->id, x.value_or(std::string()))) m->specials.push_back(a);
                    sets[m->id] = std::move(m);
                }
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << "\n";
            return false;
        }
        // Keep the old objects alive: bodies may point at them
        // mid-swing (a reload during a fight)
        for (auto& [k, v] : attacks) retired.push_back(std::move(v));
        for (auto& [k, v] : movesets) retiredSets.push_back(std::move(v));
        attacks = std::move(atk);
        movesets = std::move(sets);
        std::cout << "Movesets: " << movesets.size() << " (" << attacks.size() << " attacks) from " << path << "\n";
        return true;
    }
    const Moveset* find(const std::string& id) const {
        auto it = movesets.find(id);
        return it == movesets.end() ? nullptr : it->second.get();
    }
    const AttackDef* attack(const std::string& id) const {
        auto it = attacks.find(id);
        return it == attacks.end() ? nullptr : it->second.get();
    }
    size_t size() const { return movesets.size(); }

private:
    std::unordered_map<std::string, std::unique_ptr<AttackDef>> attacks;
    std::unordered_map<std::string, std::unique_ptr<Moveset>> movesets;
    std::vector<std::unique_ptr<AttackDef>> retired;
    std::vector<std::unique_ptr<Moveset>> retiredSets;
};

} // namespace combat

inline combat::MovesetRegistry gMovesets;

// The attack a body is doing (on Human)
struct AttackState {
    AttackKind       kind = AttackKind::None;
    int              chain = 0;          // which swing of the light chain
    const AttackDef* d = nullptr;
    float      t = 0.0f;           // s into it
    float      prevT = 0.0f;       // where it was last tick (the hit sweep runs prevT → t)
    float      stop = 0.0f;        // hit-stop left
    float      speed = 1.0f;       // weapon weight: time runs this much faster
    float      poiseScale = 1.0f;  // weapon weight: × poise damage
    glm::vec3  dir{0, 0, -1};      // the way it swings (follows facing during the wind-up)
    bool       queuedLight = false;// a light press waiting to chain
    bool       plunging = false;   // air heavy: falling
    bool       landed = false;     // air heavy: has slammed
    std::vector<uint64_t> hitIds;  // bodies this swing (this hit window) has hit already
    int        window = 0;         // which hit window the sweep is in (AttackDef::windows)
    uint16_t   item = 0;           // a cast: the item (spellbook) it came from, held while it plays
    // root motion and aim (Simulation::aimAttacks fills these)
    glm::vec3  aim{0.0f};          // the target's feet, while it has one
    bool       hasAim = false;
    uint64_t   aimId = 0;
    float      stepLen = -1.0f;    // the step it will take (< 0: its own length)
    glm::vec2  carry{0.0f};        // a sprint's speed, carried on (fading)
    // a charge (a heavy held)
    bool       held = false;       // the button is down (from the intent)
    float      charged = 0.0f;     // s held at charge_at
    float      charge() const { return d && d->chargeAt >= 0.0f ? std::clamp(charged / std::max(d->chargeTime, 1e-3f), 0.0f, 1.0f) : 0.0f; }
    bool       charging() const { return d && d->chargeAt >= 0.0f && held && t >= d->chargeAt && charged < d->chargeTime; }

    bool active() const { return kind != AttackKind::None && d; }
    const AttackDef& def() const { static const AttackDef NONE; return d ? *d : NONE; }
    const char* name() const { return d ? d->id.c_str() : "none"; }
    bool inWindow(float a, float b) const { return t >= a && t <= b; }
    bool armored() const { return d && d->armorFrom >= 0.0f && t >= d->armorFrom && t <= d->activeEnd; }
};

// ── The pose of each attack ───────────────────────────────
// Upper-body joint angles (degrees) at key times, eased between,
// laid over the base pose like the axe swing (game/AxeSwing.h).
// Named by the attack's 'pose'; animation clips replace these
// later. Sword in the right hand; the plunge brings the left
// hand in.
namespace combatanim {

struct Key {
    float t;
    glm::vec3 spine, head, shR, elR, wrR, shL, elL;
    float hipL, kneeL, kneeR, drop;       // legs: a step, knees, how far the body sinks (inches)
};

inline const std::vector<Key>& keys(const std::string& pose) {
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
    if (pose == "sword_light1")    return L1;
    if (pose == "sword_light2")    return L2;
    if (pose == "sword_heavy")     return HV;
    if (pose == "sword_air_light") return AL;
    if (pose == "sword_plunge")    return AH;
    return NONE;
}

// Lay the attack over the joint angles; rootDrop (inches) sinks
// the body (a slam's crouch)
inline void apply(const std::string& pose, float duration, float t, std::array<glm::vec3, BONE_COUNT>& rot, float& rootDrop) {
    const auto& K = keys(pose);
    if (K.size() < 2 || t < 0.0f) return;
    size_t i = 0;
    while (i + 2 < K.size() && t > K[i + 1].t) i++;
    const Key& a = K[i];
    const Key& b = K[i + 1];
    float u = std::clamp((t - a.t) / std::max(b.t - a.t, 1e-4f), 0.0f, 1.0f);
    u = u * u * (3.0f - 2.0f * u);
    // in and out of the attack from the base pose
    auto ss = [](float a0, float b0, float x) { float v = std::clamp((x - a0) / (b0 - a0), 0.0f, 1.0f); return v * v * (3 - 2 * v); };
    float w = ss(0.0f, 0.06f, t) * (1.0f - ss(duration - 0.18f, duration, t));
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
