#pragma once
#include "../entity/Player.h"
#include "../npc/NpcManager.h"
#include "../npc/Pathfinder.h"
#include "../npc/Encounters.h"
#include "../game/CombatHits.h"
#include "BodyPoses.h"
#include "../physics/IPhysics.h"
#include "../world/Woodcutting.h"
#include "../chunk/ChunkManager.h"
#include "../voxel/Voxel.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Simulation — the game itself, without a screen
// ══════════════════════════════════════════════════════════
// Owns every body in the world (players and NPCs) and runs one
// fixed physics tick at a time: movement, ragdolls, the enemy
// brains, combat, falling logs, death and respawn. It knows
// nothing about windows, cameras, input devices or drawing.
//
// That split is what multiplayer needs:
//   server  runs a Simulation; each connected player sends
//           their HumanIntent every tick (setIntent)
//   client  runs the same code to predict its own player, and
//           draws what the Simulation holds
//   tests   drive it headless (tests/sim_test.cpp)
//
// Entities are referred to by id (EntityId = Entity::uuid),
// never by pointer, in anything that outlives a tick (events,
// the encounter registry, saves, network messages): bodies come
// and go. find() turns an id back into a body.
//
// What happens is reported as events (takeEvents); the client
// turns the ones about its own player into messages and sounds.
using EntityId = uint64_t;

struct SimEvent {
    enum class Kind : uint8_t {
        Hit,        // a: attacker, b: target, attack, amount (damage), blocked, guardBroken
        Killed,     // a: attacker, b: the one who died
        Respawned,  // b: the player
        Pickup,     // b: the player, item, count
        MenuClosed, // a: the NPC, b: the player it was talking to
        EncounterStarted,   // encounter: its id
        EncounterMerged,    // encounter: the one kept, a: the one merged into it
        EncounterEnded,     // encounter: its id (Simulation::encounters() has the review)
    };
    uint32_t   encounter = 0;
    Kind       kind = Kind::Hit;
    EntityId   a = 0, b = 0;
    AttackKind attack = AttackKind::None;
    float      amount = 0.0f;
    bool       blocked = false, guardBroken = false, poiseBroken = false, knockedDown = false;
    uint16_t   item = 0;
    int        count = 0;
};

class Simulation : public NpcWorld {
public:
    static constexpr float RESPAWN_AFTER = 3.0f;   // s a dead player lies there

    // ── The world it runs in ──────────────────────────────
    // Set when a world opens, cleared when it closes. Without
    // physics the simulation does nothing.
    void attachWorld(IPhysics* p, Woodcutting* w) {
        physics = p; wood = w;
        fights.hooks.now = [this] { return clock; };
        fights.hooks.standNear = [this](glm::vec3 want, glm::vec3& out) {
            if (!physics) return false;
            nav::Pathfinder pf(*physics);
            static const int R[9][2] = { {0,0}, {3,0}, {-3,0}, {0,3}, {0,-3}, {3,3}, {-3,3}, {3,-3}, {-3,-3} };
            for (auto& o : R) {
                int x = (int)std::floor(want.x) + o[0], z = (int)std::floor(want.z) + o[1], y;
                if (pf.standAt(x, z, (int)std::floor(want.y + 0.01f), y)) { out = glm::vec3(x + 0.5f, (float)y, z + 0.5f); return true; }
            }
            return false;
        };
    }
    void detachWorld() {
        physics = nullptr;
        wood = nullptr;
        npcList.clear();
        intents.clear();
        noises.clear();
        fights.clear();
    }
    bool hasWorld() const { return physics != nullptr; }

    // ── Bodies ────────────────────────────────────────────
    Player& addPlayer() {
        playerList.push_back(std::make_unique<Player>());
        return *playerList.back();
    }
    void removePlayer(EntityId id) {
        playerList.erase(std::remove_if(playerList.begin(), playerList.end(),
                         [&](const auto& p) { return p->uuid == id; }), playerList.end());
        intents.erase(id);
    }
    const std::vector<std::unique_ptr<Player>>& players() const { return playerList; }
    NpcManager& npcs() { return npcList; }

    Human* find(EntityId id) override {
        for (auto& p : playerList) if (p->uuid == id) return p.get();
        for (auto& n : npcList.all()) if (n->uuid == id) return n.get();
        return nullptr;
    }
    Player* findPlayer(EntityId id) {
        for (auto& p : playerList) if (p->uuid == id) return p.get();
        return nullptr;
    }
    // Every body that is in the world right now (players without
    // a body — spectators — are not)
    std::vector<Human*> bodies() override {
        std::vector<Human*> out;
        for (auto& p : playerList) if (p->rules().hasBody) out.push_back(p.get());
        for (auto& n : npcList.all()) out.push_back(n.get());
        return out;
    }

    // ── Input ─────────────────────────────────────────────
    // What a player wants this tick. One-shots (jump, dodge,
    // attacks) are used by the next step and then cleared.
    void setIntent(EntityId player, const HumanIntent& in) {
        HumanIntent& mine = intents[player];
        bool j = mine.jump, d = mine.dodge, l = mine.attackLight, h = mine.attackHeavy;
        mine = in;
        mine.jump |= j; mine.dodge |= d; mine.attackLight |= l; mine.attackHeavy |= h;
    }

    // ── One fixed tick ────────────────────────────────────
    void step(float dt) {
        if (!physics) return;

        // players
        for (auto& pp : playerList) {
            Player& p = *pp;
            if (!p.rules().hasBody) continue;
            p.refreshMass();
            p.refreshWeapon();
            HumanIntent& in = intents[p.uuid];
            physics->moveHuman(p, in, dt);
            physics->stepRagdoll(p, dt);
            in.clearOneShots();
        }

        // NPCs: they hear the noises made last tick
        clock += dt;
        replansLeft = REPLANS_PER_TICK;
        std::vector<Noise> heard;
        heard.swap(noises);
        std::vector<glm::vec3> where;
        for (auto& p : playerList) if (p->rules().hasBody) where.push_back(p->position);
        npcList.tick(dt, *physics, *this, heard, where);
        for (EntityId id : npcList.takeClosedMenus()) {
            SimEvent e; e.kind = SimEvent::Kind::MenuClosed; e.a = id;
            events.push_back(e);
        }

        // every nearby body posed for this tick (where blades and
        // limbs are), then every attack against every body; then
        // bodies apart
        poseBodies(dt);
        std::vector<Human*> all;
        for (Human* h : bodies()) if (!untouchable(*h)) all.push_back(h);
        std::vector<combat::HitEvent> hits;
        for (auto& r : sight) r.age += dt;
        sight.erase(std::remove_if(sight.begin(), sight.end(), [](const SightRay& r) { return r.age > 2.0f; }), sight.end());
        combat::resolveHits(all, [&](glm::vec3 a, glm::vec3 b) {
            bool c = clearLine(a, b);
            if (sight.size() < 64) sight.push_back({ a, b, c, 0.0f });   // for the debug overlay
            return c;
        }, hits, [this](const Human& a, const Human& b) {
            // NPCs only hit what they're hostile to; players hit anyone
            return a.isPlayer() || hostile(static_cast<const Npc&>(a), b);
        }, [this](const Human& h, hitshape::Posed& out) { return poses.posed(h.uuid, out); });
        combat::separate(all, [&](glm::vec3 pos) {
            return physics->capsuleIsClear(pos, Human::RADIUS, Human::STAND_HEIGHT);
        });
        for (const auto& h : hits) {
            hitConsequences(h.attacker, h.target, h.point);
            fights.hit(h.attacker, h.target, h.damage);
            if (h.killed) fights.killed(h.attacker, h.target, xpFor(h.target));
            SimEvent e;
            e.kind = SimEvent::Kind::Hit;
            e.a = h.attacker; e.b = h.target;
            e.attack = h.kind; e.amount = h.damage;
            e.blocked = h.blocked; e.guardBroken = h.guardBroken;
            e.poiseBroken = h.poiseBroken; e.knockedDown = h.knockedDown;
            events.push_back(e);
            if (h.killed) {
                SimEvent k; k.kind = SimEvent::Kind::Killed; k.a = h.attacker; k.b = h.target;
                events.push_back(k);
            }
        }

        // falling logs hit bodies; dropped wood is picked up
        if (wood) {
            std::vector<Player*> pickers;
            for (auto& p : playerList) if (p->rules().hasBody) pickers.push_back(p.get());
            wood->tick(dt, [&](int x, int y, int z) { return physics->worldSolidAt(x, y, z); }, all, pickers);
            for (const auto& pk : wood->takePickups()) {
                SimEvent e; e.kind = SimEvent::Kind::Pickup; e.b = pk.player; e.item = pk.item; e.count = pk.count;
                events.push_back(e);
            }
        }

        // footsteps: walking is heard close by, sprinting farther,
        // sneaking (crouched) not at all
        stepClock += dt;
        if (stepClock >= STEP_EVERY) {
            stepClock = 0.0f;
            for (auto& p : playerList) {
                if (!p->rules().hasBody || p->dead() || !p->grounded || p->crouching) continue;
                float speed = glm::length(glm::vec2(p->velocity.x, p->velocity.z));
                if (speed < 3.0f) continue;
                Noise z; z.kind = Noise::Kind::Step; z.at = p->position; z.source = p->uuid;
                z.range = p->sprinting ? STEP_RUN : STEP_WALK;
                noises.push_back(z);
            }
        }

        // breadcrumbs, and fights that opened, merged or ended
        for (auto& p : playerList) if (p->rules().hasBody && !p->dead()) fights.dropCrumbs(p->uuid, p->position, clock);
        for (uint32_t id : fights.takeOpened()) { SimEvent e; e.kind = SimEvent::Kind::EncounterStarted; e.encounter = id; events.push_back(e); }
        for (auto& [into, from] : fights.takeMerged()) {
            SimEvent e; e.kind = SimEvent::Kind::EncounterMerged; e.encounter = into; e.a = from; events.push_back(e);
        }
        for (uint32_t id : fights.update([this](EntityId p) { Player* pl = findPlayer(p); return !pl || pl->dead(); })) {
            SimEvent e; e.kind = SimEvent::Kind::EncounterEnded; e.encounter = id; events.push_back(e);
        }

        // after the tick: Test Play can't be hurt, falling out of
        // the world, death and respawn
        for (auto& pp : playerList) {
            Player& p = *pp;
            if (!p.rules().hasBody) continue;
            p.sinceHurt += dt;
            if (!p.rules().tracksHealth) p.health = p.maxHealth;
            if (p.fellOutOfWorld) rescue(p);
            if (p.dead()) {
                p.deadFor += dt;
                if (p.deadFor > RESPAWN_AFTER) respawn(p);
            } else p.deadFor = 0.0f;
        }
    }

    // The skeletons the hits use (debug overlay, tests)
    const BodyPoses& bodyPoses() const { return poses; }

    // What a body holds in its right hand (held model id) and
    // whether it stands ready to fight, for its pose
    static void handsOf(const Human& h, std::string& held, bool& combat) {
        held.clear();
        if (h.isPlayer()) {
            const Player& p = static_cast<const Player&>(h);
            const ItemDef* w = p.wielded();
            combat = p.combatStance || w;
            held = p.combatStance ? (w ? w->heldModel : std::string()) : p.activeInventory().heldDef().heldModel;
        } else {
            const Npc& n = static_cast<const Npc&>(h);
            uint16_t id = gItems.find(n.weaponItem);
            held = id ? gItems.get(id).heldModel : std::string();
            combat = true;
        }
    }

    // A living body (other than 'except') with a limb (or, with
    // no pose, its capsule) at p
    EntityId bodyAt(glm::vec3 p, EntityId except) {
        for (Human* h : bodies()) {
            if (h->uuid == except || h->dead() || untouchable(*h)) continue;
            hitshape::Posed ph;
            if (poses.posed(h->uuid, ph)) {
                for (int i = 0; i < BONE_COUNT; i++)
                    if (ph.body->has[i] && hitshape::contains(ph.body->world(i, *ph.now), p, 0.15f)) return h->uuid;
                continue;
            }
            if (p.y < h->position.y || p.y > h->position.y + h->height) continue;
            glm::vec2 d(p.x - h->position.x, p.z - h->position.z);
            if (glm::length(d) <= Human::RADIUS + 0.4f) return h->uuid;
        }
        return 0;
    }
    // A hit from something other than a moveset attack — a tool
    // swung by hand (the axe). Same damage, poise and knock rules
    // as weapons (combat::poiseHit), reported as a Hit event.
    // (Moves into the simulation's own weapon traces once swings
    // are sampled from animation clips.)
    void toolHit(EntityId attacker, EntityId target, float damage, float poiseDmg,
                 glm::vec3 point, glm::vec3 dir, bool knockdown = false) {
        Human* b = find(target);
        if (!b || b->dead() || untouchable(*b)) return;
        hitConsequences(attacker, target, point);
        fights.hit(attacker, target, damage);
        combat::HitEvent ev;
        ev.attacker = attacker; ev.target = target; ev.point = point;
        b->health = std::max(0.0f, b->health - damage);
        b->sinceHurt = 0.0f;
        b->lastHitBy = attacker;
        float knock = combat::poiseHit(*b, poiseDmg, 18.0f, knockdown, ev);
        if (b->dead()) { ev.killed = true; b->attack = AttackState(); b->ragdoll.stayDown = true; fights.killed(attacker, target, xpFor(target)); }
        glm::vec3 flat(dir.x, 0.0f, dir.z);
        combat::knockBody(*b, point, glm::length(flat) > 1e-3f ? glm::normalize(flat) : glm::vec3(0, 0, 1), 0.15f, knock);
        ev.damage = damage;
        SimEvent e;
        e.kind = SimEvent::Kind::Hit; e.a = attacker; e.b = target; e.amount = damage;
        e.poiseBroken = ev.poiseBroken; e.knockedDown = ev.knockedDown;
        events.push_back(e);
        if (ev.killed) { SimEvent k; k.kind = SimEvent::Kind::Killed; k.a = attacker; k.b = target; events.push_back(k); }
    }

    // The last line-of-sight checks of attacks (debug overlay)
    struct SightRay { glm::vec3 a, b; bool clear; float age; };
    const std::vector<SightRay>& sightLog() const { return sight; }

    std::vector<SimEvent> takeEvents() { std::vector<SimEvent> e; e.swap(events); return e; }

    // Nothing solid between two points (sight, swings)
    bool clearLine(glm::vec3 a, glm::vec3 b) const override {
        if (!physics) return true;
        glm::vec3 d = b - a;
        int steps = (int)(glm::length(d) / 0.5f);
        for (int i = 1; i < steps; i++) {
            glm::vec3 p = a + d * ((float)i / steps);
            if (physics->worldSolidAt((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z)))
                return false;
        }
        return true;
    }

    // ── NpcWorld (npc/Npc.h): what brains may ask ─────────
    bool hostile(const Npc& n, const Human& o) const override {
        if (o.dead() || &o == &n) return false;
        if (std::find(n.grudges.begin(), n.grudges.end(), o.uuid) != n.grudges.end()) return true;
        if (o.isPlayer()) {
            const Player& p = static_cast<const Player&>(o);
            return p.rules().hasBody && gFactions.hostile(standing(p, n.faction));
        }
        const Npc& m = static_cast<const Npc&>(o);
        if (std::find(m.grudges.begin(), m.grudges.end(), n.uuid) != m.grudges.end()) return true;
        return gFactions.hostile(gFactions.standing(n.faction, m.faction));
    }
    bool friendly(const Npc& n, const Human& o) const override {
        if (o.dead() || &o == &n) return false;
        if (std::find(n.grudges.begin(), n.grudges.end(), o.uuid) != n.grudges.end()) return false;
        if (o.isPlayer()) return gFactions.friendly(standing(static_cast<const Player&>(o), n.faction));
        return gFactions.friendly(gFactions.standing(n.faction, static_cast<const Npc&>(o).faction));
    }
    void makeNoise(const Noise& z) override { noises.push_back(z); }
    void joinFight(Npc& n, EntityId target) override {
        Human* t = find(target);
        if (!t || !t->isPlayer()) return;               // encounters are around players (for now)
        fights.join(n.uuid, n.name, n.type, target);
    }
    void leaveFight(Npc& n, const std::string& reason) override { fights.leave(n.uuid, reason); }
    void sawTarget(Npc& n, EntityId target) override { fights.seen(n.uuid, target); }
    bool slotSpot(Npc& n, const Human& t, glm::vec3& out, bool& engage, float dt) override {
        Slot s;
        return fights.slotSpot(n.uuid, t.position, t.facing, n.position, out, engage, s, dt);
    }
    bool requestAttack(Npc& n) override { return fights.requestAttack(n.uuid); }
    void releaseAttack(Npc& n) override { fights.releaseAttack(n.uuid); }
    bool latestCrumb(EntityId p, glm::vec3& at) override {
        const encounter::Crumb* c = fights.latestCrumb(p);
        if (c) at = c->at;
        return c != nullptr;
    }
    bool crumbAfter(EntityId p, float time, glm::vec3& at, float& when) override {
        const encounter::Crumb* c = fights.crumbAfter(p, time);
        if (c) { at = c->at; when = c->time; }
        return c != nullptr;
    }
    encounter::Registry& encounters() { return fights; }
    float xpFor(EntityId id) {
        Npc* n = npcList.find(id);
        if (!n || !n->type) return 0.0f;
        return n->type->xp >= 0.0f ? n->type->xp : n->type->health / 4.0f;
    }
    float now() const override { return clock; }
    // Pathfinding (npc/Pathfinder.h). A straight walk when the way
    // is open and flat; otherwise a planned path, replanned when
    // the goal moves, the plan is old, or the NPC is stuck. Plans
    // are rationed per tick (REPLANS_PER_TICK) so a crowd doesn't
    // stall a frame; one that waits keeps its old plan.
    glm::vec3 steerToward(Npc& n, glm::vec3 goal) override {
        if (!physics) return goal;
        glm::vec3 d = goal - n.position;
        float flatDist = glm::length(glm::vec2(d.x, d.z));
        if (flatDist > nav::MAX_RANGE) return goal;
        // stuck: hardly moving while trying to walk
        if (glm::length(n.position - n.stuckFrom) > 1.0f) { n.stuckFrom = n.position; n.stuckFor = 0.0f; }
        else n.stuckFor += PHYS_DT;
        bool stuck = n.stuckFor > 1.0f;
        // open and flat: straight there
        if (!stuck && flatDist < 24.0f && std::fabs(d.y) < nav::STEP_UP && walkable(n.position, goal)) {
            n.path.clear();
            return goal;
        }
        n.pathAge += PHYS_DT;
        bool replan = n.path.empty() || n.pathAt >= n.path.size() || glm::length(goal - n.pathGoal) > 4.0f ||
                      n.pathAge > 2.0f || stuck;
        if (replan && replansLeft > 0) {
            replansLeft--;
            nav::Pathfinder pf(*physics);
            nav::Result r = pf.find(n.position, goal);
            n.path = std::move(r.points);
            n.pathAt = 0;
            n.pathGoal = goal;
            n.pathAge = 0.0f;
            n.pathReached = r.reached;
            if (stuck) { n.stuckFor = 0.0f; if (n.grounded) n.intent.jump = true; }
            pathsPlanned++;
        }
        if (n.path.empty()) return goal;
        // skip points already reached, and ones it can see past
        while (n.pathAt < n.path.size() &&
               glm::length(glm::vec2(n.path[n.pathAt].x - n.position.x, n.path[n.pathAt].z - n.position.z)) < 1.6f)
            n.pathAt++;
        while (n.pathAt + 1 < n.path.size() && std::fabs(n.path[n.pathAt + 1].y - n.position.y) < 1.0f &&
               walkable(n.position, n.path[n.pathAt + 1]))
            n.pathAt++;
        if (n.pathAt >= n.path.size()) return goal;
        glm::vec3 next = n.path[n.pathAt];
        if (next.y - n.position.y > nav::STEP_UP - 0.5f && n.grounded &&
            glm::length(glm::vec2(next.x - n.position.x, next.z - n.position.z)) < 4.0f)
            n.intent.jump = true;                       // too high to walk up
        return next;
    }
    int pathsPlanned = 0;                 // for the debug overlay

    // A player's standing with a faction (their reputation, or
    // the faction's starting standing)
    static float standing(const Player& p, int faction) {
        const FactionRegistry::Faction* f = gFactions.get(faction);
        if (!f) return 0.0f;
        auto it = p.reputation.find(f->id);
        return it == p.reputation.end() ? f->playerStanding : it->second;
    }

    // A player starts / stops talking to an NPC
    bool openMenu(EntityId npc, EntityId player) {
        Npc* n = npcList.find(npc);
        return n && npcList.openMenu(*n, player);
    }
    void closeMenu(EntityId npc) { if (Npc* n = npcList.find(npc)) npcList.closeMenu(*n); }

    static constexpr float PHYS_DT         = 1.0f / 60.0f;
    static constexpr int   REPLANS_PER_TICK = 2;
    static constexpr float REP_HIT_NEUTRAL = 15.0f;   // reputation lost hitting someone not hostile to you
    static constexpr float FIGHT_NOISE     = 45.0f;   // how far the sound of a hit carries
    static constexpr float STEP_EVERY      = 0.5f;    // s between footstep noises
    static constexpr float STEP_WALK       = 14.0f;   // how far walking is heard
    static constexpr float STEP_RUN        = 32.0f;   // … and sprinting

private:
    BodyPoses poses;

    // Pose players with a body and Full-tier NPCs; the rest are
    // too far for their limbs to matter (they keep the capsule)
    void poseBodies(float dt) {
        std::string held;
        bool combat = false;
        for (auto& p : playerList) {
            if (!p->rules().hasBody) continue;
            handsOf(*p, held, combat);
            poses.update(*p, p->appearance, held, combat, dt);
        }
        for (auto& n : npcList.all()) {
            if (n->tier != (uint8_t)NpcManager::Tier::Full) continue;
            handsOf(*n, held, combat);
            poses.update(*n, n->appearance, held, combat, dt);
        }
        poses.endTick();
    }
    int replansLeft = REPLANS_PER_TICK;
    // Can a body walk straight from a to b? (level ground, nothing
    // in the way at the knees or the chest, no holes)
    bool walkable(glm::vec3 a, glm::vec3 b) const {
        // the body's width: both sides of it, at the knees and the chest
        glm::vec3 d = b - a;
        glm::vec2 f(d.x, d.z);
        if (glm::length(f) < 1e-3f) return true;
        f = glm::normalize(f);
        glm::vec3 side(-f.y * (Human::RADIUS + 0.3f), 0.0f, f.x * (Human::RADIUS + 0.3f));
        for (float h : { 3.0f, 12.0f })
            for (float k : { -1.0f, 0.0f, 1.0f })
                if (!clearLine(a + side * k + glm::vec3(0, h, 0), b + side * k + glm::vec3(0, h, 0))) return false;
        int steps = (int)(glm::length(glm::vec2(d.x, d.z)) / 2.0f);
        for (int i = 1; i <= steps; i++) {
            glm::vec3 p = a + d * ((float)i / (steps + 1));
            int x = (int)std::floor(p.x), z = (int)std::floor(p.z), y = (int)std::floor(p.y + 0.01f);
            if (!physics->solidAt(x, y - 1, z) && !physics->solidAt(x, y - 2, z) && !physics->solidAt(x, y - 3, z)) return false;
        }
        return true;
    }
    bool untouchable(const Human& h) const {
        return !h.isPlayer() && static_cast<const Npc&>(h).untouchable;
    }
    // What a hit means beyond damage: it's heard, and hitting
    // someone who wasn't hostile costs reputation and makes them
    // (and their friends, by the noise) want you
    void hitConsequences(EntityId attacker, EntityId target, glm::vec3 at) {
        Noise z; z.kind = Noise::Kind::Fight; z.at = at; z.range = FIGHT_NOISE; z.source = attacker;
        noises.push_back(z);
        Human* a = find(attacker);
        Human* b = find(target);
        if (!a || !b || !a->isPlayer() || b->isPlayer()) return;
        Player& p = static_cast<Player&>(*a);
        Npc& n = static_cast<Npc&>(*b);
        if (hostile(n, p)) return;
        if (const FactionRegistry::Faction* f = gFactions.get(n.faction)) {
            float s = standing(p, n.faction);
            p.reputation[f->id] = std::max(-100.0f, s - REP_HIT_NEUTRAL);
        }
    }

    // Fell off the edge of the world → the nearest ground inside
    // it, or the spawn point
    void rescue(Player& p) {
        float maxX = (float)(WORLD_SIZE_X * VOXEL_DIM) - 8.0f;
        float maxZ = (float)(WORLD_SIZE_Z * VOXEL_DIM) - 8.0f;
        float x = std::clamp(p.position.x, 8.0f, maxX);
        float z = std::clamp(p.position.z, 8.0f, maxZ);
        glm::vec3 spot;
        if (physics->findStandingSpot(x, z, Human::RADIUS, Human::STAND_HEIGHT, spot) ||
            physics->findStandingSpot(p.spawnPoint.x, p.spawnPoint.z, Human::RADIUS, Human::STAND_HEIGHT, spot)) {
            p.position = spot;
            p.resetMotion();
        }
    }
    void respawn(Player& p) {
        glm::vec3 spot;
        if (physics->findStandingSpot(p.spawnPoint.x, p.spawnPoint.z, Human::RADIUS, Human::STAND_HEIGHT, spot))
            p.position = spot;
        p.resetMotion();
        p.health = p.maxHealth;
        p.stamina = p.maxStamina;
        p.deadFor = 0.0f;
        SimEvent e; e.kind = SimEvent::Kind::Respawned; e.b = p.uuid;
        events.push_back(e);
    }

    IPhysics*    physics = nullptr;
    Woodcutting* wood = nullptr;
    std::vector<std::unique_ptr<Player>> playerList;
    NpcManager   npcList;
    std::unordered_map<EntityId, HumanIntent> intents;
    std::vector<SimEvent> events;
    std::vector<SightRay> sight;
    std::vector<Noise> noises;        // made this tick, heard next tick
    encounter::Registry fights;       // encounters, slots, tokens, breadcrumbs
    float clock = 0.0f;
    float stepClock = 0.0f;
};
