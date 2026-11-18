#pragma once
#include "Brain.h"
#include "../game/Combat.h"
#include <algorithm>
#include <cmath>
#include <string>

// ══════════════════════════════════════════════════════════
// The built-in brain states (npc/Brain.h explains the system)
// ══════════════════════════════════════════════════════════
//   idle         stands (facing where it was put) or wanders
//                about home, by type; waits for something
//   unavailable  in a menu with a player: stands, faces them.
//                Attacked: "protected" types ignore it (hits
//                don't land), others drop the menu and react
//   patrol       walks its waypoints around home, pausing at each
//   alert        has noticed something: stops, faces it, and
//                after its alert time decides — fight (crying for
//                help first, if it can), look for it, or flee
//   combat       fights its target (game/Combat.h moveset):
//                closes in, circles, attacks, guards, rolls away.
//                Loses sight for long → search; past its leash →
//                return
//   search       goes to where the target was last known and
//                looks around; gives up → return
//   return       walks home and starts over (ignores enemies
//                until back inside its territory)
//   flee         runs from the threat, then returns (a shell for
//                now: friendly NPCs and spells will use it)
//   dead         nothing
//
// Register more with npcStates().add(name, factory) before the
// first NPC spawns (registerBuiltinStates shows how).
namespace npcstates {

using K = NpcEvent::Kind;

// ── idle ──────────────────────────────────────────────────
class Idle : public NpcState {
public:
    const char* name() const override { return "idle"; }
    void enter(Npc& n, NpcWorld&) override { wait = n.randRange(1.0f, 4.0f); going = false; }
    void tick(Npc& n, NpcWorld& w, float dt) override {
        if (!n.type || !n.type->wander) {
            // stand; drift back home if pushed away
            if (glm::length(glm::vec2(n.position.x - n.home.x, n.position.z - n.home.z)) > 3.0f) n.walkTo(w, n.home, 0.5f);
            else n.face(n.position + n.homeFacing);
            return;
        }
        if (going) {
            if (n.walkTo(w, goal, 0.45f) < 1.5f || n.inState > 12.0f) { going = false; wait = n.randRange(2.0f, 6.0f); n.inState = 0; }
            return;
        }
        wait -= dt;
        if (wait <= 0.0f) {
            float a = n.randRange(0.0f, 6.2832f), r = n.randRange(0.3f, 1.0f) * n.type->wanderRadius;
            goal = n.home + glm::vec3(std::cos(a) * r, 0, std::sin(a) * r);
            going = true;
            n.inState = 0;
        }
    }
    std::string debug(const Npc&) const override { return going ? "idle (wander)" : "idle"; }
private:
    float wait = 2.0f;
    bool going = false;
    glm::vec3 goal{0};
};

// ── unavailable (in a menu) ───────────────────────────────
class Unavailable : public NpcState {
public:
    const char* name() const override { return "unavailable"; }
    void enter(Npc& n, NpcWorld&) override { n.untouchable = n.type && n.type->menuProtected; }
    void exit(Npc& n, NpcWorld&) override { n.untouchable = false; n.menuWith = 0; }
    void tick(Npc& n, NpcWorld& w, float) override {
        Human* p = w.find(n.menuWith);
        if (!p || p->dead() || glm::length(p->position - n.position) > 24.0f) { brain::change(n, w, brain::startState(n)); return; }
        n.face(p->position);
    }
    bool onEvent(Npc& n, NpcWorld& w, const NpcEvent& e) override {
        if (e.kind == K::MenuClosed) { brain::change(n, w, brain::startState(n)); return true; }
        if (e.kind == K::Damaged) return n.untouchable;          // protected: ignore; else defaults (fight / flee)
        return e.kind != K::Damaged;                            // busy: sees and hears nothing
    }
};

// ── patrol ────────────────────────────────────────────────
class Patrol : public NpcState {
public:
    const char* name() const override { return "patrol"; }
    void enter(Npc& n, NpcWorld&) override { pause = 0.0f; if (n.patrol.empty()) n.patrol.push_back(n.home); }
    void tick(Npc& n, NpcWorld& w, float dt) override {
        if (pause > 0.0f) { pause -= dt; return; }
        glm::vec3 to = n.patrol[(size_t)n.patrolAt % n.patrol.size()];
        if (n.walkTo(w, to, 0.5f) < 2.0f || n.inState > 30.0f) {
            n.patrolAt = (n.patrolAt + 1) % (int)n.patrol.size();
            pause = n.randRange(1.0f, 3.0f);
            n.inState = 0;
        }
    }
private:
    float pause = 0.0f;
};

// ── alert ─────────────────────────────────────────────────
class Alert : public NpcState {
public:
    const char* name() const override { return "alert"; }
    void enter(Npc& n, NpcWorld&) override {
        decideIn = n.type ? n.randRange(n.type->alertMin, n.type->alertMax) : 0.8f;
    }
    void tick(Npc& n, NpcWorld& w, float) override {
        n.face(n.lastKnown);
        if (n.inState < decideIn) return;
        // decide
        Human* t = w.find(n.target);
        bool sees = n.seesTarget && t && !t->dead();
        if (!n.hasState("combat")) { brain::change(n, w, sees && n.hasState("flee") ? "flee" : "return"); return; }
        if (sees) {
            // a battle cry first, if it has one and it's ready
            if (n.type && n.type->cryRange > 0.0f && w.now() >= n.cryReadyAt) {
                Noise c;
                c.kind = Noise::Kind::BattleCry;
                c.at = n.position;
                c.range = n.type->cryRange;
                c.source = n.uuid;
                c.faction = n.faction;
                c.about = n.target;
                c.aboutAt = n.lastKnown;
                w.latestCrumb(n.target, c.aboutAt);              // where the player is now (their last breadcrumb)
                w.makeNoise(c);
                n.cryReadyAt = w.now() + n.type->cryCooldown;
            }
            brain::change(n, w, "combat");
        } else brain::change(n, w, "search");
    }
    bool onEvent(Npc& n, NpcWorld& w, const NpcEvent& e) override {
        if (e.kind == K::SawEnemy) { brain::know(n, w, e.who, e.at); return true; }   // keeps deciding
        return false;
    }
    std::string debug(const Npc& n) const override {
        char b[48]; std::snprintf(b, sizeof(b), "alert %.1f/%.1f", n.inState, decideIn); return b;
    }
private:
    float decideIn = 0.8f;
};

// ── combat ────────────────────────────────────────────────
// Sub-states: approach, circle, attack, guard, recover.
class Combat : public NpcState {
public:
    static constexpr float FIGHT_RANGE = 13.0f;   // vox: close enough to swing
    static constexpr float STEER_RANGE = 24.0f;   // vox from its spot: closer than this, context steering takes over from paths
    static constexpr float KEEP_RANGE  = 16.0f;   // vox: circles at about this distance
    static constexpr float LOSE_AFTER  = 3.0f;    // s out of sight → search

    enum class Sub : uint8_t { Approach, Circle, Attack, Guard, Recover };

    const char* name() const override { return "combat"; }
    void enter(Npc& n, NpcWorld& w) override {
        sub = Sub::Approach; subTime = 0; comboLeft = 0; side = n.rand01() < 0.5f ? 1.0f : -1.0f;
        if (n.target) w.joinFight(n, n.target);
    }
    void exit(Npc& n, NpcWorld& w) override { w.releaseAttack(n); }
    void tick(Npc& n, NpcWorld& w, float dt) override {
        Human* t = w.find(n.target);
        if (!t || t->dead() || !w.hostile(n, *t)) { n.target = 0; brain::change(n, w, "return"); return; }
        if (!n.seesTarget && w.now() - n.lastSeenAt > LOSE_AFTER) { brain::change(n, w, "search"); return; }
        if (n.type && n.type->leash > 0.0f &&
            glm::length(glm::vec2(n.position.x - n.home.x, n.position.z - n.home.z)) > n.type->leash) {
            n.target = 0; brain::change(n, w, "return"); return;
        }
        fight(n, w, *t, dt);
    }
    bool onEvent(Npc& n, NpcWorld& w, const NpcEvent& e) override {
        if (e.kind == K::Damaged) {
            // hit by an enemy: that's who it's after now (not by a friend's stray swing)
            if (Human* h = w.find(e.who); h && w.hostile(n, *h)) brain::know(n, w, e.who, h->position);
            return true;
        }
        if (e.kind == K::SawEnemy || e.kind == K::HeardNoise || e.kind == K::AllyDied) return true;   // busy
        return false;
    }
    std::string debug(const Npc&) const override {
        static const char* S[] = { "approach", "circle", "attack", "guard", "recover" };
        return std::string("combat: ") + S[(int)sub];
    }

private:
    Sub sub = Sub::Approach;
    float subTime = 0.0f, side = 1.0f;
    int comboLeft = 0;
    bool wasAttacking = false;

    void to(Sub s, float time) { sub = s; subTime = time; }

    void fight(Npc& n, NpcWorld& w, const Human& t, float dt) {
        HumanIntent& in = n.intent;
        // it goes by what it knows: where it sees the target, or
        // where it last saw it
        glm::vec3 tp = n.seesTarget ? t.position : n.lastKnown;
        glm::vec2 off(tp.x - n.position.x, tp.z - n.position.z);
        float dist = glm::length(off);
        glm::vec2 dir = dist > 1e-3f ? off / dist : glm::vec2(0, -1);
        glm::vec2 left(dir.y, -dir.x);
        n.facing = glm::vec3(dir.x, 0.0f, dir.y);      // faces the target (an attack locks its own way)
        subTime -= dt;
        bool busy = n.attack.active() || n.move != Human::Move::None;
        float aggression = n.type ? n.type->aggression : 0.7f;
        // its place in the fight (npc/Encounters.h): where to stand,
        // and whether it holds an attack token (may close in)
        glm::vec3 spot = tp;
        bool engage = true;
        bool slotted = n.seesTarget && w.slotSpot(n, t, spot, engage, dt);
        if (!n.attack.active() && wasAttacking) { w.releaseAttack(n); }
        wasAttacking = n.attack.active();

        // the target winds up close by → maybe roll away or guard
        if (!busy && t.attack.active() && dist < FIGHT_RANGE + 4.0f) {
            const AttackDef& d = t.attack.def();
            float until = d.activeStart - t.attack.t;
            if (until > 0.0f && until < 0.25f && sub != Sub::Guard) {
                float r = n.rand01();
                if (r < 0.35f && n.stamina > combat::ROLL_STAMINA) {
                    in.move = (n.rand01() < 0.5f ? left : -left) * 0.7f - dir * 0.7f;
                    in.dodge = true;
                    to(Sub::Recover, 0.5f);
                    return;
                }
                if (r < 0.65f) to(Sub::Guard, n.randRange(0.6f, 1.2f));
            }
        }
        // chain the combo: press again once the swing is active
        if (n.attack.active()) {
            const AttackDef& d = n.attack.def();
            if (comboLeft > 0 && d.comboFrom >= 0.0f && n.attack.t >= d.activeEnd && !n.attack.queuedLight) {
                in.attackLight = true;
                comboLeft--;
            }
            return;
        }
        // how far it is from where it should be
        glm::vec2 toSpot(spot.x - n.position.x, spot.z - n.position.z);
        float offSpot = glm::length(toSpot);
        switch (sub) {
        case Sub::Approach:
            if (slotted && offSpot > 4.0f && !(engage && dist <= FIGHT_RANGE)) {
                if (offSpot > STEER_RANGE) n.walkTo(w, spot, 1.0f, offSpot > 30.0f && n.stamina > 40.0f, 2.0f);   // far: a path around obstacles
                else {
                    // near: context steering for the last stretch (npc/ContextSteering.h)
                    in.move = w.contextSteer(n, spot, t, engage, FIGHT_RANGE, 0.0f);
                    in.speedScale = 1.0f;
                    in.sprint = false;
                }
                n.facing = glm::vec3(dir.x, 0.0f, dir.y);
                break;
            }
            if (!slotted && dist > FIGHT_RANGE) {
                n.walkTo(w, tp, 1.0f, dist > 40.0f && n.stamina > 40.0f, FIGHT_RANGE - 1.0f);
                break;
            }
            to(Sub::Circle, n.randRange(0.4f, 1.3f));
            side = n.rand01() < 0.5f ? 1.0f : -1.0f;
            [[fallthrough]];
        case Sub::Circle: {
            // hold its place: drift around it, keep facing the target
            if (slotted) {
                // hold its place: steered to its spot, circling a little,
                // clear of the others and out of the target's swing
                in.move = w.contextSteer(n, spot, t, engage, FIGHT_RANGE, side);
                in.speedScale = 0.6f * std::clamp(glm::length(in.move) * 1.5f, 0.3f, 1.0f);   // eases in as it settles
            } else {
                glm::vec2 keep = dir * std::clamp((dist - KEEP_RANGE) * 0.2f, -0.6f, 0.6f);
                in.move = left * side * 0.35f + keep;
                in.speedScale = 0.6f;
            }
            if ((slotted && offSpot > 12.0f) || (!slotted && dist > FIGHT_RANGE + 12.0f)) { to(Sub::Approach, 0); break; }
            if (subTime <= 0.0f) {
                float r = n.rand01();
                if (n.stamina < 35.0f) to(Sub::Guard, 1.0f);
                else if (r < aggression && w.requestAttack(n)) to(Sub::Attack, 2.0f);   // its turn
                else to(Sub::Guard, n.randRange(0.5f, 1.3f));
            }
            break;
        }
        case Sub::Attack:
            if (subTime <= 0.0f) { w.releaseAttack(n); to(Sub::Circle, 0.5f); break; }   // took too long
            if (dist > FIGHT_RANGE - 2.0f) { n.walkTo(w, tp, 1.0f, false, FIGHT_RANGE - 3.0f); n.facing = glm::vec3(dir.x, 0, dir.y); break; }
            if (n.rand01() < 0.25f) in.attackHeavy = true;
            else { in.attackLight = true; comboLeft = n.rand01() < 0.5f ? 1 : 0; }
            wasAttacking = true;
            to(Sub::Recover, n.randRange(0.3f, 0.8f));
            break;
        case Sub::Guard:
            in.guard = true;
            in.move = left * side * 0.4f;
            if (subTime <= 0.0f) to(Sub::Circle, n.randRange(0.4f, 1.0f));
            break;
        case Sub::Recover:
            in.move = -dir * 0.4f + left * side * 0.3f;
            in.speedScale = 0.6f;
            if (subTime <= 0.0f) to(Sub::Circle, n.randRange(0.3f, 0.9f));
            break;
        }
    }
};

// ── search ────────────────────────────────────────────────
class Search : public NpcState {
public:
    static constexpr float LOOK_AROUND = 8.0f;   // s looking about once there
    const char* name() const override { return "search"; }
    void enter(Npc&, NpcWorld&) override { arrived = false; lookFor = 0.0f; trailTime = -1.0f; trailSteps = 0; }
    void tick(Npc& n, NpcWorld& w, float dt) override {
        if (!arrived) {
            if (n.walkTo(w, n.lastKnown, 0.8f, false, 2.5f) < 2.5f || n.inState > 25.0f) { arrived = true; n.inState = 0; next(n); }
            return;
        }
        // trackers follow the trail of breadcrumbs past where it lost you
        if (n.type && n.type->tracks && n.target && trailSteps < MAX_TRAIL) {
            glm::vec3 c; float when;
            if (w.crumbAfter(n.target, trailTime < 0.0f ? n.lastSeenAt : trailTime, c, when)) {
                trailTime = when; trailSteps++;
                n.lastKnown = c; arrived = false; lookFor = 0.0f; n.inState = 0;
                return;
            }
        }
        lookFor += dt;
        if (lookFor > LOOK_AROUND) { n.target = 0; brain::change(n, w, "return"); return; }
        if (n.walkTo(w, peek, 0.45f) < 1.5f) next(n);
    }
    bool onEvent(Npc& n, NpcWorld& w, const NpcEvent& e) override {
        if (e.kind == K::SawEnemy) { brain::know(n, w, e.who, e.at); brain::change(n, w, "combat"); return true; }
        if (e.kind == K::HeardNoise && e.noise.kind != Noise::Kind::Step) {
            n.lastKnown = e.noise.kind == Noise::Kind::BattleCry ? e.noise.aboutAt : e.at;
            arrived = false; lookFor = 0; n.inState = 0;
            return true;
        }
        return false;
    }
    std::string debug(const Npc&) const override { return arrived ? "search (looking)" : "search (going)"; }
private:
    static constexpr int MAX_TRAIL = 20;      // crumbs a tracker follows
    bool arrived = false;
    float lookFor = 0.0f;
    float trailTime = -1.0f;
    int trailSteps = 0;
    glm::vec3 peek{0};
    void next(Npc& n) {
        float a = n.randRange(0.0f, 6.2832f), r = n.randRange(4.0f, 12.0f);
        peek = n.lastKnown + glm::vec3(std::cos(a) * r, 0, std::sin(a) * r);
    }
};

// ── return ────────────────────────────────────────────────
class Return : public NpcState {
public:
    const char* name() const override { return "return"; }
    void tick(Npc& n, NpcWorld& w, float) override {
        glm::vec3 to = n.patrol.empty() ? n.home : n.patrol[(size_t)n.patrolAt % n.patrol.size()];
        if (n.walkTo(w, to, 0.6f) < 2.0f || n.inState > 40.0f) {
            n.health = std::max(n.health, n.type ? n.type->health : n.maxHealth);   // reset, like souls enemies
            n.grudges.clear();
            brain::change(n, w, brain::startState(n));
        }
    }
    bool onEvent(Npc& n, NpcWorld&, const NpcEvent& e) override {
        // leashed: ignores enemies until back inside its territory
        float r = n.type && n.type->territoryRadius > 0.0f ? n.type->territoryRadius * 0.5f : 1e9f;
        bool far = glm::length(glm::vec2(n.position.x - n.home.x, n.position.z - n.home.z)) > r;
        return far && (e.kind == K::SawEnemy || e.kind == K::HeardNoise);
    }
};

// ── flee (shell) ──────────────────────────────────────────
class Flee : public NpcState {
public:
    const char* name() const override { return "flee"; }
    void tick(Npc& n, NpcWorld& w, float) override {
        glm::vec3 from = n.lastKnown;
        if (Human* t = w.find(n.target)) from = t->position;
        glm::vec3 away = n.position - from; away.y = 0;
        away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec3(1, 0, 0);
        n.walkTo(w, n.position + away * 10.0f, 1.0f, true, 0.0f);
        if (n.inState > 6.0f && glm::length(n.position - from) > 40.0f) { n.target = 0; brain::change(n, w, "return"); }
    }
    bool onEvent(Npc&, NpcWorld&, const NpcEvent& e) override { return e.kind != K::MenuOpened; }
};

// ── dead ──────────────────────────────────────────────────
class Dead : public NpcState {
public:
    const char* name() const override { return "dead"; }
    void tick(Npc& n, NpcWorld&, float) override { n.intent = HumanIntent(); }
    bool onEvent(Npc&, NpcWorld&, const NpcEvent&) override { return true; }
};

} // namespace npcstates

inline void registerBuiltinStates() {
    static bool done = false;
    if (done) return;
    done = true;
    StateRegistry& r = npcStates();
    r.add("idle",        [] { return std::make_unique<npcstates::Idle>(); });
    r.add("unavailable", [] { return std::make_unique<npcstates::Unavailable>(); });
    r.add("patrol",      [] { return std::make_unique<npcstates::Patrol>(); });
    r.add("alert",       [] { return std::make_unique<npcstates::Alert>(); });
    r.add("combat",      [] { return std::make_unique<npcstates::Combat>(); });
    r.add("search",      [] { return std::make_unique<npcstates::Search>(); });
    r.add("return",      [] { return std::make_unique<npcstates::Return>(); });
    r.add("flee",        [] { return std::make_unique<npcstates::Flee>(); });
    r.add("dead",        [] { return std::make_unique<npcstates::Dead>(); });
}
