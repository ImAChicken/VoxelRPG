#pragma once
#include "Npc.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// ══════════════════════════════════════════════════════════
// Brain — the state machine, its registry, and the senses
// ══════════════════════════════════════════════════════════
// States are plug-ins: a class derived from NpcState, registered
// once by name (npcStates().add("patrol", …)). An NPC type lists
// the names it may use (npcs.toml); brain::change() only enters
// a state the type has, otherwise it falls back (see FALLBACK).
// Adding a state = writing the class, registering it, naming it
// in the data.
//
// Each tick for each NPC:
//   1. senses (a few times a second, staggered): sight, and
//      hearing of this tick's noises → events in its inbox;
//      damage and deaths are noticed every tick
//   2. events go to the current state; what it doesn't handle
//      gets the brain's defaults (defaultReaction)
//   3. the state's tick fills the intent
class StateRegistry {
public:
    using Factory = std::function<std::unique_ptr<NpcState>()>;
    void add(const std::string& name, Factory f) { factories[name] = std::move(f); }
    std::unique_ptr<NpcState> make(const std::string& name) const {
        auto it = factories.find(name);
        return it == factories.end() ? nullptr : it->second();
    }
    bool has(const std::string& name) const { return factories.count(name) != 0; }
private:
    std::unordered_map<std::string, Factory> factories;
};
inline StateRegistry& npcStates() { static StateRegistry r; return r; }

namespace brain {

constexpr float SENSE_HZ   = 8.0f;    // sight checks a second, per NPC
constexpr float EYE_HEIGHT = 15.0f;   // voxels above the feet

// A state the type lacks → what it does instead
inline std::string fallback(const Npc& n, const std::string& want) {
    if (n.hasState(want)) return want;
    if (want == "patrol" || want == "return" || want == "unavailable") return n.hasState("patrol") && want != "patrol" ? "patrol" : "idle";
    if (want == "search") return n.hasState("combat") ? "return" : "idle";
    if (want == "combat") return n.hasState("flee") ? "flee" : "idle";
    if (want == "alert") return n.hasState("combat") ? "combat" : "idle";
    return "idle";
}

// Enter a state (or its fallback). Returns false if nothing changed.
inline bool change(Npc& n, NpcWorld& w, const std::string& want) {
    std::string to = fallback(n, want);
    if (to == n.stateName && n.state) return false;
    // leaving the fight (combat / search) for anything else
    bool wasFighting = n.stateName == "combat" || n.stateName == "search";
    bool stillFighting = to == "combat" || to == "search";
    if (wasFighting && !stillFighting) w.leaveFight(n, to == "flee" ? "fled" : to);
    auto s = npcStates().make(to);
    if (!s) s = npcStates().make("idle");
    if (!s) return false;
    if (n.state) n.state->exit(n, w);
    n.state = std::move(s);
    n.stateName = to;
    n.inState = 0.0f;
    n.intent = HumanIntent();
    n.state->enter(n, w);
    return true;
}

inline std::string startState(const Npc& n) {
    if (n.life.on && n.hasState("routine")) return "routine";      // a villager's day
    return n.type && !n.type->start.empty() ? n.type->start : "idle";
}

// Remember where the target is
inline void know(Npc& n, NpcWorld& w, EntityId who, glm::vec3 at) {
    n.target = who;
    n.lastKnown = at;
    n.lastSeenAt = w.now();
}

// ── Senses ────────────────────────────────────────────────
inline bool canSee(const Npc& n, NpcWorld& w, const Human& o) {
    if (!n.type || n.type->sightRange <= 0.0f) return false;
    glm::vec3 eye = n.position + glm::vec3(0, EYE_HEIGHT, 0);
    glm::vec3 at = o.position + glm::vec3(0, o.height * 0.6f, 0);
    glm::vec3 d = at - eye;
    float dist = glm::length(d);
    if (dist > n.type->sightRange) return false;
    // close by, it notices you whatever way it faces
    if (dist > 6.0f) {
        glm::vec2 f(n.facing.x, n.facing.z), dd(d.x, d.z);
        if (glm::length(f) > 1e-4f && glm::length(dd) > 1e-4f) {
            float ang = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(f), glm::normalize(dd)), -1.0f, 1.0f)));
            if (ang > n.type->sightFov * 0.5f) return false;
        }
    }
    return w.clearLine(eye, at);
}

// Sight: who it can see now; new ones and lost ones become events
inline void look(Npc& n, NpcWorld& w) {
    std::vector<EntityId> now;
    for (Human* o : w.bodies()) {
        if (o == &n || o->dead()) continue;
        if (!w.hostile(n, *o)) continue;
        if (!canSee(n, w, *o)) continue;
        now.push_back(o->uuid);
        if (o->uuid == n.target || !n.target) know(n, w, o->uuid, o->position);
        if (o->uuid == n.target) w.sawTarget(n, o->uuid);
        if (std::find(n.visible.begin(), n.visible.end(), o->uuid) == n.visible.end()) {
            NpcEvent e; e.kind = NpcEvent::Kind::SawEnemy; e.who = o->uuid; e.at = o->position;
            n.post(e);
        }
    }
    for (EntityId was : n.visible)
        if (std::find(now.begin(), now.end(), was) == now.end()) {
            NpcEvent e; e.kind = NpcEvent::Kind::LostSight; e.who = was; e.at = n.lastKnown;
            n.post(e);
        }
    n.visible = std::move(now);
    n.seesTarget = n.target && std::find(n.visible.begin(), n.visible.end(), n.target) != n.visible.end();
}

// Hearing: this tick's noises within range × hearing
inline void hear(Npc& n, const std::vector<Noise>& noises) {
    if (!n.type || n.type->hearing <= 0.0f) return;
    for (const Noise& z : noises) {
        if (z.source == n.uuid) continue;
        if (glm::length(z.at - n.position) > z.range * n.type->hearing) continue;
        NpcEvent e; e.kind = NpcEvent::Kind::HeardNoise; e.who = z.source; e.at = z.at; e.noise = z;
        n.post(e);
    }
}

// ── The brain's defaults ──────────────────────────────────
// What happens to an event the current state didn't handle
inline void defaultReaction(Npc& n, NpcWorld& w, const NpcEvent& e) {
    using K = NpcEvent::Kind;
    if (n.stateName == "dead") return;
    switch (e.kind) {
    case K::Damaged:
        if (e.who) {
            if (Human* h = w.find(e.who)) know(n, w, e.who, h->position);
            // hit by someone it wasn't hostile to: now it is
            if (Human* h = w.find(e.who); h && !w.hostile(n, *h)) n.grudges.push_back(e.who);
        }
        if (n.stateName != "combat") change(n, w, n.hasState("combat") ? "combat" : "flee");
        break;
    case K::SawEnemy:
        if (n.stateName == "idle" || n.stateName == "patrol" || n.stateName == "search" || n.stateName == "return") {
            know(n, w, e.who, e.at);
            change(n, w, n.stateName == "search" ? "combat" : "alert");
        }
        break;
    case K::HeardNoise:
        if (n.stateName == "idle" || n.stateName == "patrol" || n.stateName == "return") {
            const Noise& z = e.noise;
            if (z.kind == Noise::Kind::BattleCry && z.about) {
                // a friend calls: it learns where the enemy is
                if (Human* src = w.find(z.source); src && w.friendly(n, *src)) {
                    know(n, w, z.about, z.aboutAt);
                    n.lastSeenAt = w.now() - 1.0f;      // known, not seen
                    change(n, w, "alert");
                }
            } else if (z.kind == Noise::Kind::Fight) {
                n.lastKnown = z.at;
                change(n, w, "alert");
            } else if (z.kind == Noise::Kind::Step) {
                // footsteps of an enemy: turns to look
                if (Human* src = w.find(z.source); src && w.hostile(n, *src)) {
                    n.target = z.source;
                    n.lastKnown = z.at;
                    n.lastSeenAt = w.now() - 1.0f;
                    change(n, w, "alert");
                }
            }
        }
        break;
    case K::AllyDied:
        if (n.stateName == "idle" || n.stateName == "patrol") { n.lastKnown = e.at; change(n, w, "alert"); }
        break;
    case K::MenuOpened:
        n.menuWith = e.who;
        change(n, w, "unavailable");
        break;
    default:
        break;
    }
}

// One tick of thinking (after the senses filled the inbox)
inline void think(Npc& n, NpcWorld& w, float dt) {
    if (!n.state) change(n, w, startState(n));
    if (n.dead() && n.stateName != "dead") change(n, w, "dead");
    std::vector<NpcEvent> events;
    events.swap(n.inbox);
    for (const NpcEvent& e : events) {
        if (n.state && n.state->onEvent(n, w, e)) continue;
        defaultReaction(n, w, e);
    }
    n.inState += dt;
    n.intent.move = glm::vec2(0);
    n.intent.sprint = false;
    n.intent.crouch = false;
    n.intent.guard = false;
    n.intent.speedScale = 1.0f;
    if (n.state && !n.ragdoll.controlsLocked()) n.state->tick(n, w, dt);
}

} // namespace brain
