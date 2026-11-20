#pragma once
#include "../entity/Human.h"
#include "../character/Appearance.h"
#include "NpcTypes.h"
#include "ContextSteering.h"
#include "VillageLife.h"
#include "../game/Container.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Npc — a person driven by a brain
// ══════════════════════════════════════════════════════════
// An Npc is a Human like the player: same body, physics,
// ragdoll and moveset. Its brain fills its HumanIntent each
// tick, where the player's comes from the keyboard.
//
// The brain is a state machine whose states are plug-ins
// (npc/Brain.h): each is a small class registered by name, and
// an NPC type (npcs.toml) lists which ones it may use. States
// never sense the world themselves: perception (npc/Senses.h)
// turns what happens into EVENTS — saw an enemy, lost it,
// heard a noise, got hurt, an ally died, a menu opened — and
// the current state reacts to them (or the brain's defaults do).
// New senses or states don't change the others.
//
// What an NPC may ask of the game goes through NpcWorld (the
// Simulation implements it), so brains run headless and on a
// server.
using EntityId = uint64_t;

// ── Noises ────────────────────────────────────────────────
// Anything that can be heard: footsteps, fighting, a battle
// cry. A battle cry carries knowledge: where the enemy is.
struct Noise {
    enum class Kind : uint8_t { Step, Fight, BattleCry, Break };
    Kind      kind = Kind::Fight;
    glm::vec3 at{0};
    float     range = 30.0f;          // heard up to this far (× the listener's hearing)
    EntityId  source = 0;
    int       faction = -1;           // the source's (battle cries: who it calls)
    EntityId  about = 0;              // battle cry: the enemy it's about
    glm::vec3 aboutAt{0};             // … and where they were last known
};

// ── Events the brain reacts to ────────────────────────────
struct NpcEvent {
    enum class Kind : uint8_t { SawEnemy, LostSight, HeardNoise, Damaged, AllyDied, MenuOpened, MenuClosed };
    Kind      kind = Kind::SawEnemy;
    EntityId  who = 0;                // the enemy / attacker / ally / player at the menu
    glm::vec3 at{0};
    Noise     noise;
};

class Npc;

// ── What a brain may ask of the game ──────────────────────
class NpcWorld {
public:
    virtual ~NpcWorld() = default;
    virtual Human* find(EntityId id) = 0;
    virtual std::vector<Human*> bodies() = 0;
    virtual bool clearLine(glm::vec3 a, glm::vec3 b) const = 0;
    virtual bool hostile(const Npc& who, const Human& other) const = 0;
    virtual bool friendly(const Npc& who, const Human& other) const = 0;
    virtual void makeNoise(const Noise& n) = 0;
    virtual float now() const = 0;                           // s since the world started
    virtual float hourOfDay() const { return 12.0f; }        // 0 - 24 (world/DayClock.h)
    // Open (true) or shut a door (a structure id); false if it can't
    // (no such door, blocked, or the game didn't say how)
    virtual bool setDoor(uint32_t door, bool open) { (void)door; (void)open; return false; }
    // Where to walk next toward 'goal': the next point of a path
    // around obstacles (npc/Pathfinder.h). May set who.intent.jump
    // for a step too high to walk up.
    virtual glm::vec3 steerToward(Npc& who, glm::vec3 goal) = 0;

    // ── The fight it's in (npc/Encounters.h) ──────────────
    virtual void joinFight(Npc& who, EntityId target) = 0;
    virtual void leaveFight(Npc& who, const std::string& reason) = 0;
    virtual void sawTarget(Npc& who, EntityId target) = 0;
    // Its place around the target: true + where to stand, and
    // whether it may close in to attack (holds a token)
    virtual bool slotSpot(Npc& who, const Human& target, glm::vec3& out, bool& engage, float dt) = 0;
    virtual bool requestAttack(Npc& who) = 0;          // may it swing now?
    // The last stretch to 'goal' in a fight (npc/ContextSteering.h):
    // a direction times strength for intent.move, avoiding the
    // others in the fight, walls and drops and the target's swing.
    // strafe -1 / +1: the way it likes to circle once there
    virtual glm::vec2 contextSteer(Npc& who, glm::vec3 goal, const Human& target, bool engage, float strike,
                                   float strafe) = 0;
    virtual void releaseAttack(Npc& who) = 0;
    // Breadcrumbs: where a player was (false: none / not a player)
    virtual bool latestCrumb(EntityId player, glm::vec3& at) = 0;
    virtual bool crumbAfter(EntityId player, float time, glm::vec3& at, float& when) = 0;
};

// ── A brain state (plug-in) ───────────────────────────────
class NpcState {
public:
    virtual ~NpcState() = default;
    virtual const char* name() const = 0;
    virtual void enter(Npc&, NpcWorld&) {}
    virtual void exit(Npc&, NpcWorld&) {}
    virtual void tick(Npc&, NpcWorld&, float dt) = 0;
    // true: handled (the brain's defaults don't run)
    virtual bool onEvent(Npc&, NpcWorld&, const NpcEvent&) { return false; }
    // a line for the debug overlay
    virtual std::string debug(const Npc&) const { return name(); }
};

class Npc : public Human {
public:
    const NpcType* type = nullptr;
    std::string typeId;
    std::string name;
    Appearance  appearance;
    std::string textureSet;            // look preset's own sheets (not drawn yet)
    int         faction = -1;          // FactionRegistry index
    std::string weaponItem;
    std::string offhandItem;           // its type's offhand (a shield: it guards with it)
    std::string offModel;              // puppets: the left hand's model, as the server says
    HumanIntent intent;
    float       deadFor = 0.0f;        // s since it died (removed after a while)
    glm::vec3   prevPosition{0.0f};    // before the last tick (drawing blends the two)

    // ── Home and territory ────────────────────────────────
    glm::vec3   home{0.0f};
    glm::vec3   homeFacing{0, 0, 1};
    std::vector<glm::vec3> patrol;     // waypoints
    int         patrolAt = 0;
    uint32_t    spawner = 0;           // the spawner structure it came from (instance id; 0 = none)
    uint32_t    roamGroup = 0;         // a roaming spawn's group (sim/Roaming.h; 0 = not one)
    float       roamAway = 0.0f;       // s it has been far from every player (roaming: goes after a while)
    VillageLife life;                  // a villager: its bed, job and haunts (npc/Routine.h)
    Container   pack;                  // what it carries (unread: just its type's loot table).
                                       // Searched on its body with E once it's dead

    // ── What it knows ─────────────────────────────────────
    EntityId    target = 0;            // who it's after
    glm::vec3   lastKnown{0.0f};       // where the target was last seen (or heard of)
    float       lastSeenAt = -1e9f;    // world time
    bool        seesTarget = false;
    std::vector<EntityId> visible;     // hostile bodies it can see now (Senses)
    std::vector<EntityId> grudges;     // hostile to these whatever the factions say
    float       healthSeen = -1.0f;    // to notice damage
    float       senseTimer = 0.0f;
    float       cryReadyAt = 0.0f;     // world time its battle cry is ready
    EntityId    menuWith = 0;          // the player it's talking to (Unavailable)

    // ── Walking somewhere (Simulation::steerToward) ───────
    std::vector<glm::vec3> path;       // standing spots to walk through
    size_t      pathAt = 0;
    glm::vec3   pathGoal{0.0f};
    float       pathAge = 99.0f;       // s since it was planned
    bool        pathReached = false;   // the plan gets all the way
    bool        pathTried = false;     // a search has been made for pathGoal (Simulation::steerToward)
    uint32_t    restTicks = 0;         // ticks standing still with its physics left out (NpcManager)
    int         pathBudget = 0;        // spots a search may look at (0: nav::MAX_NODES); a stroll needs fewer
    glm::vec3   stuckFrom{0.0f};
    float       stuckFor = 0.0f;
    bool        untouchable = false;   // in a protected menu: hits don't land
    float       contactReadyAt = 0.0f; // world time it may hurt by touch again (Simulation::contactDamage)

    // ── Context steering in a fight (npc/ContextSteering.h) ─
    steer::Map  steerMap;              // the last answer (shown by 'debug steering')
    glm::vec3   steerGoal{0.0f};
    float       steerAt = -99.0f;      // world time it was worked out
    int         steerTick = 0;

    // ── The brain ─────────────────────────────────────────
    std::unique_ptr<NpcState> state;
    std::string stateName;
    float       inState = 0.0f;        // s in the current state
    std::vector<NpcEvent> inbox;       // events since the last tick

    std::vector<std::string> wearing;  // puppets of players: the worn items the server says they have on
    bool     puppet = false;           // a body shown from a server's snapshots (client): never thinks or moves itself
    uint8_t  tier = 0;                 // NpcManager::Tier (how much of it is simulated)
    uint32_t lookSeed = 0;             // its look (saved, so it looks the same next time)
    uint32_t rng = 1;
    float rand01() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); }
    float randRange(float a, float b) { return a + (b - a) * rand01(); }

    bool isPlayer() const override { return false; }
    bool hasState(const std::string& s) const { return type ? type->has(s) : s == "idle" || s == "dead"; }
    void post(const NpcEvent& e) { inbox.push_back(e); }

    // Steering helpers for states: walk / run toward a point,
    // face something
    void face(glm::vec3 at) {
        glm::vec2 d(at.x - position.x, at.z - position.z);
        if (glm::length(d) > 1e-3f) { d = glm::normalize(d); facing = glm::vec3(d.x, 0, d.y); }
    }
    // Sets intent.move (a world x/z direction, as physics reads
    // it) toward 'to'; returns the flat distance left
    float walkTo(NpcWorld& w, glm::vec3 to, float speed = 1.0f, bool run = false, float stopAt = 1.5f) {
        intent.speedScale = speed;               // (steering reads it: a stroll isn't stuck)
        glm::vec3 next = w.steerToward(*this, to);
        glm::vec2 d(next.x - position.x, next.z - position.z);
        float left = glm::length(glm::vec2(to.x - position.x, to.z - position.z));
        if (left <= stopAt || glm::length(d) < 1e-3f) { intent.move = glm::vec2(0); return left; }
        d = glm::normalize(d);
        face(position + glm::vec3(d.x, 0, d.y));
        intent.move = d;
        intent.speedScale = speed;
        intent.sprint = run && !hop.on;          // a hopper's sprint is its lunge
        return left;
    }
};
