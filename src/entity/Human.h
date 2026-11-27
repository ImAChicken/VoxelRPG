#pragma once
#include "Entity.h"
#include "BodyMass.h"
#include "../physics/Ragdoll.h"
#include "../game/Combat.h"
#include "../game/GrappleState.h"
#include "../game/Spells.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

// ══════════════════════════════════════════════════════════
// Human — a two-legged body: player, NPC, enemy
// ══════════════════════════════════════════════════════════
// All numbers are in voxels (1 voxel = 4 inches) and seconds.
// This file is the single place to tune how movement feels.

// ── What a human is trying to do this physics tick ────────
// Filled in by a controller: PlayerController reads the
// keyboard, an AI would write the same struct, and a server
// receives it over the network. Physics turns
// (state + intent) into the next state — nothing else goes
// in, which is what keeps it deterministic.
struct HumanIntent {
    // Desired direction of travel on the ground plane (x, z).
    // Zero = no movement input. Length is ignored.
    glm::vec2 move   = {0, 0};

    bool sprint = false;   // held
    bool crouch = false;   // held
    float speedScale = 1.0f;   // < 1: held back (swinging an axe)

    // One-shot requests. The controller sets them, physics
    // consumes them on the next tick, then they are cleared.
    bool jump  = false;    // ground: jump   air: double jump
    bool dodge = false;    // ground: roll (moving) or backstep
                           // (still)   air: aerial dodge

    // Fly mode (dev): no gravity, no collision
    bool      fly     = false;
    glm::vec3 flyMove = {0, 0, 0};   // full 3D direction
    bool      flyFast = false;

    // Combat (game/Combat.h): one-shots like jump, and the
    // guard, held
    bool attackLight = false;
    bool attackHeavy = false;
    bool heavyHeld   = false;  // the heavy button held: a heavy charges (game/Combat.h charge)
    bool castHeld    = false;  // a spell's button held: a charge powers up, a channel goes on (game/Spells.h)
    bool guard       = false;
    // Use an item (one-shot): a spell from an X / Y / B slot or the
    // off hand (game/CombatKit.h); plays the item's 'cast' attack.
    // The simulation checks the player really carries it.
    uint16_t useItem = 0;
    // An attack by name from the moveset's specials (one-shot; NPCs:
    // a boss's charge). Not sent over the network
    const AttackDef* special = nullptr;

    // The grapple (game/Grapple.h): thrown (one-shot) at a point or a
    // body, worked out where the player aims (the game: grapple::aim),
    // checked by the simulation (it has to carry one, in reach); held
    // past a tap, it swings instead of pulling all the way
    bool      grapple     = false;
    bool      grappleHeld = false;
    glm::vec3 grappleAt{ 0.0f };
    uint64_t  grappleOn   = 0;         // a body's id (0: the block at grappleAt)
    bool      grappleSwing = true;     // that body can be swung from (a flyer's grapple point). Not sent: the server works it out

    void clearOneShots() { jump = false; dodge = false; attackLight = false; attackHeavy = false; useItem = 0; special = nullptr; grapple = false; }
};

class Human : public Entity {
public:
    // ── Body ──────────────────────────────────────────────
    static constexpr float RADIUS         = 1.5f;   // 6 in
    static constexpr float STAND_HEIGHT   = 17.0f;  // 5'8"
    static constexpr float CROUCH_HEIGHT  = 10.0f;  // 3'4"
    static constexpr float STAND_EYE      = 16.0f;  // default eye height, 5'4"
    static constexpr float CROUCH_EYE     = 8.0f;
    // The eye height in use (Settings: Eye height); crouching
    // lowers the eyes by the same amount whatever it is
    static inline float eyeStand = STAND_EYE;
    static float eyeCrouch() { return eyeStand - (STAND_EYE - CROUCH_EYE); }

    // ── Ground movement ───────────────────────────────────
    static constexpr float WALK_SPEED     = 30.0f;
    static constexpr float SPRINT_SPEED   = 60.0f;  // twice a walk (Eddie, 2026-11-20: was 50, not felt enough)
    static constexpr float CROUCH_SPEED   = 15.0f;
    static constexpr float GROUND_ACCEL   = 900.0f; // how fast you reach speed / stop
    static constexpr float AIR_ACCEL      = 30.0f;  // steering while airborne
    static constexpr float STEP_UP        = 5.0f;   // climbed without jumping
    // Slopes (VoxelPhysics::slopeFactor): walking up a grade g
    // (rise over run) goes at 1 / (1 + g * climb * CLIMB_COST) of
    // the flat speed, never below MIN_CLIMB_SPEED; downhill a
    // little faster, up to DOWNHILL_BONUS
    static constexpr float CLIMB_COST      = 1.0f;
    static constexpr float MIN_CLIMB_SPEED = 0.35f;
    static constexpr float DOWNHILL_BONUS  = 0.10f;
    static constexpr float STEP_DOWN      = 2.0f;   // stay glued going down small steps

    // ── Gravity ───────────────────────────────────────────
    static constexpr float GRAVITY        = 40.0f;
    static constexpr float MAX_FALL_SPEED = 80.0f;

    // ── Jump ──────────────────────────────────────────────
    static constexpr float JUMP_HEIGHT        = 15.0f;
    static constexpr float DOUBLE_JUMP_HEIGHT = 20.0f;  // 35 total from the peak
    static constexpr float DOUBLE_JUMP_FREEZE = 0.15f;

    // ── Dodges ────────────────────────────────────────────
    // Each covers a fixed distance in a fixed time
    static constexpr float ROLL_DISTANCE      = 20.0f;
    static constexpr float ROLL_TIME          = 0.5f;
    static constexpr float BACKSTEP_DISTANCE  = 10.0f;
    static constexpr float BACKSTEP_TIME      = 0.3f;
    static constexpr float AIR_DODGE_DISTANCE = 12.0f;
    static constexpr float AIR_DODGE_TIME     = 0.15f;
    static constexpr float AIR_DODGE_FREEZE   = 0.12f;

    // ── Fly mode ──────────────────────────────────────────
    static constexpr float FLY_SPEED      = 22.0f;
    static constexpr float FLY_SPEED_FAST = 240.0f;   // 4x: crossing a big island

    // ── Health ────────────────────────────────────────────
    static constexpr float MAX_HEALTH = 100.0f;

    // ── State ─────────────────────────────────────────────
    float height    = STAND_HEIGHT;   // current collision height
    float eyeHeight = STAND_EYE;      // eases between stand and crouch

    // Health lives here rather than on Player because
    // enemies and NPCs are humans too. Nothing deals damage
    // yet; game modes that track health just display it.
    float health    = MAX_HEALTH;
    float maxHealth = MAX_HEALTH;

    bool grounded  = false;
    bool crouching = false;
    bool sprinting = false;
    bool flying    = false;
    bool lying     = false;   // lying down (a villager asleep in bed): drawn on its back, not moved by physics

    // Air moves — each usable once, both refill on landing
    bool hasDoubleJump = true;
    bool hasAirDodge   = true;

    // The freeze: momentum is paused and stored, gravity is
    // off, and a short window opens (later: for an attack).
    enum class Freeze : uint8_t { None, DoubleJump, AirDodge };
    Freeze    freeze         = Freeze::None;
    float     freezeTimer    = 0.0f;
    glm::vec3 storedVelocity = {0, 0, 0};

    // A committed move in progress
    enum class Move : uint8_t { None, Roll, Backstep, AirDodge };
    Move      move      = Move::None;
    float     moveTimer = 0.0f;       // seconds left
    glm::vec3 moveDir   = {0, 0, 0};  // unit direction

    // ── Set by physics for whoever is watching ────────────
    // How far a step up/down just moved the body vertically.
    // The camera uses it to glide over steps instead of
    // snapping. Whoever reads it resets it to zero.
    float stepJolt = 0.0f;
    // A server corrected where this body is by this much (minus):
    // the camera and the drawn body ease over it instead of jumping
    // (client/ClientWorld.h reconcile). Whoever reads it resets it.
    glm::vec3 renderOffset = { 0.0f, 0.0f, 0.0f };

    // ── Body shape (npcs.toml body) ───────────────────────
    // People are the default. A slime is short, a little wider
    // to hit, has no ragdoll (hits just push it) and gets about
    // by hopping (VoxelPhysics::hopTick)
    float standHeight = STAND_HEIGHT;  // collision height standing
    // How big the body is against an ordinary person (a giant boss:
    // 1.6). Its collision width and height, eyes, model and weapon
    // all grow with it (Appearance::scale is the look's side of it)
    float scale = 1.0f;
    float attackSpeed = 1.0f;          // × how fast its attacks play (a boss's second phase)
    // Portals (world/PortalLinks.h): its middle before this tick's move,
    // and the last trip through one (the game turns the camera by it)
    glm::vec3 portalPrev{ 0.0f };
    bool      portalPrevSet = false;
    uint32_t  portalTrips = 0;
    glm::mat4 lastPortal{ 1.0f };
    // the face it last came out of (centre, normal; normal 0: none yet),
    // and the world it went in from ("": the same one): its trailing
    // half is still drawn going in there (main.cpp's portal bodies)
    glm::vec3 lastExitCentre{ 0.0f }, lastExitNormal{ 0.0f };
    // a puppet's axe swing as the server says (seconds in, < 0 none): its pose
    float puppetSwing = -1.0f;
    std::string lastPortalFrom;
    float radius() const { return RADIUS * scale; }
    void setScale(float s) {
        scale = s;
        standHeight = height = STAND_HEIGHT * s;
        hitRadius = RADIUS * s;
        eyeHeight = STAND_EYE * s;
    }
    float hitRadius   = RADIUS;        // half width of its hit box when it has no limbs
    float hitHeight   = 0.0f;          // how tall that box is (0: its height). A slime's is taller than
                                       // it is: swings at a person's height still meet it
    bool  noRagdoll   = false;         // nothing goes limp: a knock is only a push
    struct Hopper {
        bool  on = false;
        float every = 0.45f;           // s on the ground between hops
        float speed = 18.0f;           // vox/s along the ground while in the air
        float height = 3.0f;           // vox: an ordinary hop
        float climb = 9.0f;            // vox: a hop up a ledge (when its path says jump)
        float lungeSpeed = 15.0f;      // the lunge at its target (intent.sprint while hopping)
        float lungeHeight = 5.0f;
        float wait = 0.0f;             // s left before the next hop
        bool  lunging = false;         // in the air on a lunge
        float climbFor = 0.0f;         // s a jump asked for is still waiting to be the next hop
        bool  climbing = false;        // in the air on a hop up a ledge: it keeps pushing on over the edge
        float carryX = 0.0f, carryZ = 0.0f;   // that hop's way along the ground
        int   hops = 0;                // hops made (tests, the squash when drawn)
    } hop;
    // A flier (npcs.toml fly = { … }): no gravity while alive; it
    // goes where intent.flyMove points (its length: how fast, up to
    // flySpeed; sprint: flyFast), colliding with the world
    // (VoxelPhysics::flierTick). Dead, it falls.
    bool  flier = false;
    float flySpeed = 20.0f, flyFast = 40.0f;
    // ── Spells on this body (sim/SpellSystem.h) ───────────
    // Statuses (spell::Status): burn and poison hurt each second,
    // slow and haste change its speed, freeze holds it still, silence
    // stops its casts. A ward soaks up damage until it's gone or its
    // time is up. An aura: a toggle spell kept on. Cooldowns by spell.
    // Resistances by damage type (spell::Element): 0.5 takes half.
    struct StatusOn {
        spell::Status kind = spell::Status::None;
        float left = 0.0f, amount = 0.0f, tick = 0.0f;
        uint64_t from = 0;
    };
    std::vector<StatusOn> statuses;
    float ward = 0.0f, wardLeft = 0.0f;
    const spell::Def* aura = nullptr;
    float auraTick = 0.0f;
    float resist[(int)spell::Element::COUNT] = {};
    std::vector<std::pair<const spell::Def*, float>> cooldowns;
    bool has(spell::Status s) const { for (const StatusOn& x : statuses) if (x.kind == s && x.left > 0.0f) return true; return false; }
    float statusAmount(spell::Status s) const { float a = 0.0f; for (const StatusOn& x : statuses) if (x.kind == s && x.left > 0.0f) a = std::max(a, x.amount); return a; }
    float cooldownOf(const spell::Def* d) const { for (auto& c : cooldowns) if (c.first == d) return c.second; return 0.0f; }
    // Damage taken: a ward soaks it up first. Returns what got through
    float takeDamage(float dmg) {
        if (ward > 0.0f && dmg > 0.0f) { float soak = std::min(ward, dmg); ward -= soak; dmg -= soak; }
        health = std::max(0.0f, health - dmg);
        return dmg;
    }
    // How fast it moves (statuses): 0 frozen
    float statusSpeed() const {
        if (has(spell::Status::Freeze)) return 0.0f;
        float k = 1.0f;
        if (has(spell::Status::Slow)) k *= std::clamp(1.0f - statusAmount(spell::Status::Slow), 0.2f, 1.0f);
        if (has(spell::Status::Haste)) k *= 1.0f + std::clamp(statusAmount(spell::Status::Haste), 0.0f, 1.0f);
        return k;
    }

    // The grapple on this body (game/Grapple.h), and the one it
    // carries: how far it reaches and how fast it pulls (0: none;
    // Player::refreshGrapple)
    GrappleState grapple;
    float grappleReach = 0.0f, grappleSpeed = 0.0f;

    // ── Slopes ────────────────────────────────────────────
    float climb = 1.0f;               // how much slopes slow this body (NPC types: npcs.toml climb)
    float grade = 0.0f;               // the recent rise over run while walking (+ up, - down)
    float gradeRise = 0.0f, gradeRun = 0.0f;   // their running averages
    glm::vec3 gradeFrom = { 0.0f, 0.0f, 0.0f };   // where it was last tick
    bool  gradeGrounded = false;

    // Chunks under the body are not loaded yet — holding still
    bool waitingForWorld = false;

    // Fell below the bottom of the world — needs a respawn
    bool fellOutOfWorld = false;

    // ── Combat (game/Combat.h) ────────────────────────────
    // Any human fights the same way: the player, an enemy.
    //   weaponDamage  the weapon in hand (0: nothing to attack with)
    //   stamina       spent by attacks, dodges and blocking;
    //                 comes back after a short pause
    float       weaponDamage = 0.0f;
    // degrees up (+) or down the body aims (a player: the camera's
    // pitch; bows shoot along it). NPCs aim at their target instead
    float       aimPitch = 0.0f;
    const Moveset* moveset = nullptr;      // the weapon's (game/Combat.h); nullptr: can't attack
    float       weaponWeight = 0.0f;       // kg, bends the moveset (combat::weightScales)
    // poise: hits wear it down; broken = stagger / knockdown
    float       poise = combat::POISE_BASE, maxPoise = combat::POISE_BASE;
    float       sincePoiseHit = 99.0f;
    bool        canDoubleJump = false;     // an ability, off by default
    float       stamina = combat::MAX_STAMINA, maxStamina = combat::MAX_STAMINA;
    float       staminaWait = 0.0f;
    bool        infiniteStamina = false;   // Test Play
    bool        guarding = false;          // set by physics from the intent
    float       guardTime = 0.0f;          // s the guard has been up (a hit early on is parried)
    // How its guard holds (CombatHits::land): a weapon's, or a
    // shield's (items.toml shield = { … }) when one is in the off hand
    struct GuardStyle {
        bool  shield = false;                  // a shield is up: guarding needs no weapon
        float keep   = combat::GUARD_KEEP;     // damage that gets through
        float cost   = 1.0f;                   // × stamina per point blocked (GUARD_COST)
        float facing = combat::GUARD_FACING;   // cos of the half-angle it covers
        float parry  = combat::PARRY_WINDOW;   // s after raising it that a hit is parried
    } guardStyle;
    void setGuard(float keep, float cost, float arcDeg, float parry, bool shield) {
        guardStyle.shield = shield; guardStyle.keep = keep; guardStyle.cost = cost;
        guardStyle.facing = std::cos(arcDeg * 0.0174533f); guardStyle.parry = parry;
    }
    void weaponGuard() { guardStyle = GuardStyle(); }
    float       exposed = 0.0f;            // s left open to a riposte, after being parried
    float       hitStop = 0.0f;            // s frozen by a hit landing (both bodies: game/CombatHits.h)
    glm::vec3   pendingShove{0.0f};        // a hit's shove, given when the hit-stop ends
    float       shoveTime = 0.0f;          // s still sliding from a shove
    // ── Reacting to hits (game/Combat.h React) ────────────
    float       hitstun = 0.0f;            // s stuck reacting: no control (a flinch, a stumble, a launch …)
    React react = React::None;   // the reaction playing (for the animation and the debug view)
    glm::vec3   reactDir{0.0f};            // the way the hit pushed it
    float       reactAge = 0.0f;           // s since it began
    int         reactBone = -1;            // the limb the hit landed on (B_HEAD: a high hit; -1 unknown)
    bool        guardBroke = false;        // the reaction is a broken guard (its own clip)
    // Every hit that lands (blocked, shrugged off or reacting) kicks
    // the body's lean spring (character/CharacterAnimator.h): the
    // animator sees hurtCount change and reads the rest
    uint16_t    hurtCount = 0;
    glm::vec3   hurtDir{0.0f};             // the way it pushed (world)
    glm::vec3   hurtPoint{0.0f};           // where it landed (world voxels)
    float       hurtSize = 0.0f;           // how hard: 0.3 a blocked hit … 1.6 a launch
    void hurt(glm::vec3 point, glm::vec3 dir, float size) {
        if (size <= 0.0f) return;
        hurtCount++; hurtPoint = point; hurtDir = dir; hurtSize = size;
    }
    float       pendingLift = 0.0f;        // vox/s up, given with the shove (a launch, a juggle pop, a slam)
    float       pendingSlide = 0.0f;       // s the shove slides before control returns to the legs
    int         juggles = 0;               // hits taken in the air since it last stood (JUGGLE_MAX)
    int         armor = 0;                 // reactions up to this tier don't move it (npcs.toml armor; 0: all do)
    bool        wallHit = false;           // set by physics when a knock pins it to a wall (Simulation turns it into an event)
    float       getUpGrace = 0.0f;         // s untouchable after standing up
    float       slowed = 0.0f;             // s its time runs slow (a perfect dodge against it)
    bool        perfectUsed = false;       // this dodge already made a perfect dodge
    int         combo = 0;                 // hits it has landed in a row (reset when it's hit or 1.5 s pass)
    float       sinceLanded = 9.0f;        // s since it last landed a hit
    bool stunned() const { return hitstun > 0.0f; }
    bool        hasAirAttack = true;       // one per jump, back on landing
    AttackState attack;
    float       sinceHurt = 99.0f;         // s since last damaged (HUD)
    uint64_t    lastHitBy = 0;

    bool dead() const { return health <= 0.0f; }
    virtual bool isPlayer() const { return false; }
    bool spend(float s) {                  // false if out of stamina
        if (infiniteStamina) return true;
        if (stamina <= 0.0f) return false;
        stamina = std::max(stamina - s, -20.0f);   // the last action may overdraw a little
        staminaWait = combat::STAMINA_DELAY;
        return true;
    }

    // ── Weight and the active ragdoll ─────────────────────
    // body     how heavy each part is, flesh plus gear
    //          (entity/BodyMass.h); 'mass' (Entity) is its total
    // ragdoll  the physical body drawn over the animation when
    //          hit (physics/Ragdoll.h); while it is limp or
    //          getting up, the body cannot be steered
    // hits     waiting to be applied by physics on its next tick
    //          (IPhysics::stepRagdoll), so anything — a command,
    //          an explosion, later a sword — just queues one
    struct Hit {
        glm::vec3 point;      // where it lands, world voxels
        glm::vec3 impulse;    // kg·voxels/s
        bool visual = false;  // only shakes the limbs (a reaction moves the capsule itself)
    };
    BodyMass         body;
    Ragdoll          ragdoll;
    std::vector<Hit> hits;

    // A shove of 'speed' voxels/s on an average (70 kg) body;
    // heavier bodies are moved less, lighter ones more
    static constexpr float REFERENCE_MASS = 70.0f;

    void hit(glm::vec3 point, glm::vec3 impulse, bool visual = false) { hits.push_back({ point, impulse, visual }); }

    // A point on the body by height: 0 feet, 1 top of the head
    glm::vec3 bodyPoint(float frac) const {
        return position + glm::vec3(0.0f, height * frac, 0.0f);
    }

    void setBodyMass(float bodyKg) {
        body.setBody(bodyKg);
        mass = body.total();
    }

    glm::vec3 eyePosition() const {
        return position + glm::vec3(0.0f, eyeHeight, 0.0f);
    }

    // Stop dead and clear every move in progress
    // (respawn, teleport, switching game mode)
    void resetMotion() {
        velocity       = {0, 0, 0};
        storedVelocity = {0, 0, 0};
        freeze         = Freeze::None;
        freezeTimer    = 0.0f;
        move           = Move::None;
        moveTimer      = 0.0f;
        hasDoubleJump  = true;
        hasAirDodge    = true;
        grounded       = false;
        sprinting      = false;
        stepJolt       = 0.0f;
        fellOutOfWorld = false;
        hits.clear();
        ragdoll.reset();
        attack = AttackState();
        guarding = false;
        guardTime = 0.0f;
        exposed = 0.0f;
        hitStop = 0.0f; pendingShove = glm::vec3(0.0f); shoveTime = 0.0f;
        hitstun = 0.0f; react = React::None; reactBone = -1; guardBroke = false; pendingLift = 0.0f; pendingSlide = 0.0f;
        juggles = 0; wallHit = false; getUpGrace = 0.0f; slowed = 0.0f; perfectUsed = false;
    }
};
