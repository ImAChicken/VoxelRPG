#pragma once
#include "../render/DebugDraw.h"
#include "../sim/Simulation.h"
#include "../world/Woodcutting.h"
#include "Combat.h"
#include "CombatHits.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// DebugView — what the debug overlay shows (F3)
// ══════════════════════════════════════════════════════════
// Reads the simulation and queues lines and labels into a
// DebugDraw. Each layer can be switched on its own (console:
// debug <layer> [on|off], or 'debug' to list them).
//
//   bodies   collision capsules (colour = state), facing and
//            velocity arrows
//   ragdoll  the 20 ragdoll points and their links, coloured by
//            ragdoll state (white animated, yellow reacting,
//            red limp, blue recovering)
//   combat   what hits: every posed body's limb boxes (dim
//            white; orange on the one a blade just touched) and
//            the weapon's boxes in its hand (yellow; red while
//            the attack's hit window is open). Attacks without a
//            posed blade (the plunge's slam, unarmed, far
//            bodies) show their reach and arc instead (dim: the
//            whole swing, bright red: swept so far), with the
//            height band. Guard cones, i-frames (green capsule)
//   labels   over each body: health, stamina, attack and its
//            time, move, ragdoll state, mass; NPC kind and plan
//   props    fallen logs (bounds, awake / asleep / hinged,
//            health), wood drops, the axe's traced blade
//   sight    the last line-of-sight checks of attacks (green
//            clear, red blocked)
//   world    chunk borders around the player
//   ai       NPC brains: state, home, territory (grey) and leash
//            (red), patrol route, sight cone (red: sees its
//            target), last known spot of its target, its path
//            (cyan; magenta: can't get all the way); players'
//            breadcrumbs (yellow); each NPC's slot in its
//            encounter, a red ring = holds an attack token;
//            spawner structures: where they spawn (green: awake,
//            grey: asleep), how near a player must be (dim), and
//            a label with their count and next spawn
//   steering context steering in fights (npc/ContextSteering.h):
//            16 rays round each NPC — green out to its interest,
//            red out to its danger — the way it chose (yellow,
//            long) and a line to the spot it steers for (white)
//   pad      the controller, bottom left of the screen: its
//            buttons lit while held — or while the key bound to the
//            same action is (input/PadLayout.h) — sticks, layer
//
// Colours of a capsule: cyan player, white NPC, orange
// attacking, blue guarding, green invulnerable, grey dead.
struct DebugLayers {
    bool on = false;
    bool bodies = true, ragdoll = true, combat = true, labels = true,
         props = true, sight = true, world = true, ai = true, steering = true, pad = true;

    bool* find(const std::string& n) {
        if (n == "bodies") return &bodies;
        if (n == "ragdoll") return &ragdoll;
        if (n == "combat") return &combat;
        if (n == "labels") return &labels;
        if (n == "props") return &props;
        if (n == "sight") return &sight;
        if (n == "world") return &world;
        if (n == "ai") return &ai;
        if (n == "steering") return &steering;
        if (n == "pad") return &pad;
        return nullptr;
    }
    std::string list() const {
        auto f = [](const char* n, bool b) { return std::string(n) + (b ? " on" : " off"); };
        return f("bodies", bodies) + ", " + f("ragdoll", ragdoll) + ", " + f("combat", combat) + ", " +
               f("labels", labels) + ", " + f("props", props) + ", " + f("sight", sight) + ", " + f("world", world) + ", " +
               f("ai", ai) + ", " + f("steering", steering) + ", " + f("pad", pad);
    }
};
inline DebugLayers gDebug;

namespace debugview {

const glm::vec3 CYAN(0.2f, 0.9f, 1.0f), WHITE(1.0f), ORANGE(1.0f, 0.55f, 0.1f), BLUE(0.3f, 0.5f, 1.0f),
                GREEN(0.2f, 1.0f, 0.3f), GREY(0.5f), RED(1.0f, 0.2f, 0.15f), YELLOW(1.0f, 0.9f, 0.2f),
                MAGENTA(1.0f, 0.3f, 1.0f);

inline const char* moveName(Human::Move m) {
    switch (m) {
    case Human::Move::Roll: return "roll";
    case Human::Move::Backstep: return "backstep";
    case Human::Move::AirDodge: return "air dodge";
    default: return "";
    }
}
inline const char* ragName(Ragdoll::State s) {
    switch (s) {
    case Ragdoll::State::Reacting: return "reacting";
    case Ragdoll::State::Limp: return "limp";
    case Ragdoll::State::Recovering: return "recovering";
    default: return "";
    }
}
inline glm::vec3 ragColor(Ragdoll::State s) {
    switch (s) {
    case Ragdoll::State::Reacting: return YELLOW;
    case Ragdoll::State::Limp: return RED;
    case Ragdoll::State::Recovering: return BLUE;
    default: return WHITE;
    }
}
// A direction at 'deg' (positive left) around a facing
inline glm::vec3 around(glm::vec3 facing, float deg) {
    glm::vec2 f(facing.x, facing.z);
    f = glm::length(f) > 1e-4f ? glm::normalize(f) : glm::vec2(0, -1);
    glm::vec3 F(f.x, 0, f.y), L(f.y, 0, -f.x);
    float r = glm::radians(deg);
    return F * std::cos(r) + L * std::sin(r);
}
// An arc of 'deg0'..'deg1' around a facing, at radius r and height y
inline void sector(DebugDraw& dd, glm::vec3 at, glm::vec3 facing, float deg0, float deg1, float r,
                   glm::vec3 c, float alpha, bool spokes) {
    const int SEG = 16;
    glm::vec3 prev = at + around(facing, deg0) * r;
    for (int i = 1; i <= SEG; i++) {
        glm::vec3 p = at + around(facing, deg0 + (deg1 - deg0) * i / SEG) * r;
        dd.line(prev, p, c, alpha);
        prev = p;
    }
    if (spokes) {
        dd.line(at, at + around(facing, deg0) * r, c, alpha);
        dd.line(at, at + around(facing, deg1) * r, c, alpha);
    }
}

// An oriented box as twelve lines
inline void obb(DebugDraw& dd, const hitshape::OBB& o, glm::vec3 c, float alpha) {
    glm::vec3 k[8];
    hitshape::corners(o, k);
    static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (auto& e : E) dd.line(k[e[0]], k[e[1]], c, alpha);
}

// The shapes hits use (sim/BodyPoses.h, game/HitShapes.h)
inline void hitShapes(DebugDraw& dd, const Simulation& sim, const Human& h) {
    hitshape::Posed p;
    if (!sim.bodyPoses().posed(h.uuid, p)) return;
    bool recent = h.sinceHurt < 0.25f;
    for (int i = 0; i < BONE_COUNT; i++)
        if (p.body->has[i]) obb(dd, p.body->world(i, *p.now), recent ? ORANGE : WHITE, recent ? 0.8f : 0.25f);
    if (p.held && p.model) {
        const AttackDef& d = h.attack.def();
        bool live = h.attack.active() && h.attack.kind != AttackKind::AirHeavy &&
                    h.attack.t >= d.activeStart && h.attack.t <= d.activeEnd;
        std::vector<hitshape::OBB> w;
        hitshape::weaponBoxes(*p.held, *p.model, (*p.now)[B_WRIST_R] * p.held->turn(*p.model, p.turnNow), w);
        for (auto& o : w) obb(dd, o, live ? RED : YELLOW, live ? 1.0f : 0.5f);
    }
}

inline void body(DebugDraw& dd, const Human& h, bool isPlayer, const std::string& title, bool posedBlade = false) {
    bool inv = combat::invulnerable(h);
    glm::vec3 c = h.dead() ? GREY : inv ? GREEN : h.guarding ? BLUE : h.attack.active() ? ORANGE
                : isPlayer ? CYAN : WHITE;

    if (gDebug.bodies) {
        dd.capsule(h.position, Human::RADIUS, h.height, c);
        glm::vec3 mid = h.position + glm::vec3(0, h.height * 0.6f, 0);
        dd.arrow(mid, mid + around(h.facing, 0) * 5.0f, YELLOW);
        glm::vec3 v = h.velocity;
        if (glm::length(v) > 0.5f) dd.arrow(h.position + glm::vec3(0, 0.3f, 0), h.position + glm::vec3(0, 0.3f, 0) + v * 0.15f, MAGENTA);
    }

    if (gDebug.ragdoll) {
        static const int LINKS[][2] = {
            {RP_PELVIS, RP_WAIST}, {RP_WAIST, RP_NECK}, {RP_NECK, RP_HEAD},
            {RP_NECK, RP_SHOULDER_L}, {RP_SHOULDER_L, RP_ELBOW_L}, {RP_ELBOW_L, RP_WRIST_L}, {RP_WRIST_L, RP_HAND_L},
            {RP_NECK, RP_SHOULDER_R}, {RP_SHOULDER_R, RP_ELBOW_R}, {RP_ELBOW_R, RP_WRIST_R}, {RP_WRIST_R, RP_HAND_R},
            {RP_PELVIS, RP_HIP_L}, {RP_HIP_L, RP_KNEE_L}, {RP_KNEE_L, RP_ANKLE_L}, {RP_ANKLE_L, RP_TOE_L},
            {RP_PELVIS, RP_HIP_R}, {RP_HIP_R, RP_KNEE_R}, {RP_KNEE_R, RP_ANKLE_R}, {RP_ANKLE_R, RP_TOE_R},
        };
        glm::vec3 rc = ragColor(h.ragdoll.state());
        bool any = false;
        for (int i = 0; i < RP_COUNT; i++) if (glm::length(h.ragdoll.point(i)) > 0.0f) { any = true; break; }
        if (any) {
            for (auto& l : LINKS) dd.line(h.ragdoll.point(l[0]), h.ragdoll.point(l[1]), rc, 0.8f);
            for (int i = 0; i < RP_COUNT; i++) dd.cross(h.ragdoll.point(i), h.ragdoll.pointRadius(i), rc, 0.6f);
        }
    }

    if (gDebug.combat) {
        if (h.attack.active() && !(posedBlade && h.attack.kind != AttackKind::AirHeavy)) {
            const AttackDef& d = h.attack.def();
            glm::vec3 mid = h.position + glm::vec3(0, (d.yLo + d.yHi) * 0.5f, 0);
            bool live = h.attack.t >= d.activeStart && h.attack.t <= d.activeEnd;
            float reach = d.reach + Human::RADIUS;
            bool full = d.arcFrom <= -179.0f && d.arcTo >= 179.0f;
            sector(dd, mid, h.attack.dir, d.arcFrom, d.arcTo, reach, ORANGE, 0.35f, !full);
            // the band it hits
            sector(dd, h.position + glm::vec3(0, d.yLo, 0), h.attack.dir, d.arcFrom, d.arcTo, reach, ORANGE, 0.2f, false);
            sector(dd, h.position + glm::vec3(0, d.yHi, 0), h.attack.dir, d.arcFrom, d.arcTo, reach, ORANGE, 0.2f, false);
            if (live) {
                float u = (h.attack.t - d.activeStart) / std::max(d.activeEnd - d.activeStart, 1e-3f);
                float now = d.arcFrom + (d.arcTo - d.arcFrom) * u;
                sector(dd, mid, h.attack.dir, d.arcFrom, now, reach, RED, 1.0f, true);
            }
        }
        if (h.guarding) {
            float half = glm::degrees(std::acos(0.35f));
            sector(dd, h.position + glm::vec3(0, h.height * 0.6f, 0), h.facing, -half, half, 4.0f, BLUE, 1.0f, true);
        }
    }

    if (gDebug.labels) {
        char b[200];
        std::string act = h.attack.active() ? std::string(h.attack.name()) : "";
        if (h.attack.active()) { char t[24]; std::snprintf(t, sizeof(t), " %.2fs", h.attack.t); act += t; }
        std::snprintf(b, sizeof(b), "%s  hp %.0f/%.0f  st %.0f  poise %.0f/%.0f  %.0fkg", title.c_str(), h.health, h.maxHealth,
                      h.stamina, h.poise, h.maxPoise, h.mass);
        std::string line2 = act;
        auto add = [&](const char* s) { if (*s) { if (!line2.empty()) line2 += "  "; line2 += s; } };
        add(moveName(h.move));
        add(ragName(h.ragdoll.state()));
        if (h.guarding) add("guard");
        if (combat::invulnerable(h)) add("i-frames");
        if (!h.grounded) add("air");
        glm::vec3 top = h.position + h.ragdoll.headOffset(1.0f) + glm::vec3(0, h.height + 4.5f, 0);
        dd.label(top, b, c);
        if (!line2.empty()) dd.label(top - glm::vec3(0, 1.6f, 0), line2, c);
    }
}

// Everything, for one frame
inline void build(DebugDraw& dd, Simulation& sim, const Woodcutting* wood, const Player& local,
                  const std::vector<std::array<glm::vec3, 3>>& axeBlade) {
    if (!gDebug.on) return;
    auto blade = [&](const Human& h) { hitshape::Posed p; return sim.bodyPoses().posed(h.uuid, p) && p.held; };
    if (gDebug.ai)
        for (const SpawnerState& s : sim.spawners().all()) {
            const StructureType* t = gStructures.get(s.type);
            if (!t) continue;
            glm::vec3 at = s.at + glm::vec3(0, 0.3f, 0);
            dd.circle(at, t->spawn.radius, s.active ? GREEN : GREY, 0.8f, 40);
            dd.circle(at, t->spawn.activate, GREY, 0.2f, 64);
            char b[128];
            if (!s.active)                                  std::snprintf(b, sizeof(b), "%s  %d/%d  asleep", t->displayName.c_str(), s.alive, t->spawn.maxAlive);
            else if (s.alive >= t->spawn.maxAlive)          std::snprintf(b, sizeof(b), "%s  %d/%d  full", t->displayName.c_str(), s.alive, t->spawn.maxAlive);
            else if (t->spawn.total && s.spawned >= t->spawn.total) std::snprintf(b, sizeof(b), "%s  %d/%d  used up", t->displayName.c_str(), s.alive, t->spawn.maxAlive);
            else                                            std::snprintf(b, sizeof(b), "%s  %d/%d  next %.0fs", t->displayName.c_str(), s.alive, t->spawn.maxAlive, std::max(0.0f, s.timer));
            dd.label(s.at + glm::vec3(0, (float)t->size.y + 3.0f, 0), b, s.active ? GREEN : GREY);
        }
    for (const auto& p : sim.players())
        if (p->rules().hasBody) {
            body(dd, *p, true, p.get() == &local ? "you" : "player", blade(*p));
            if (gDebug.combat) hitShapes(dd, sim, *p);
        }
    for (const auto& n : sim.npcs().all()) {
        if (gDebug.combat) hitShapes(dd, sim, *n);
        std::string t = n->name + "  [" + (n->state ? n->state->debug(*n) : std::string("-")) + "]";
        if (n->tier == 1) t += "  lite"; else if (n->tier == 2) t += "  frozen";
        if (const FactionRegistry::Faction* f = gFactions.get(n->faction)) t += "  " + f->id;
        body(dd, *n, false, t, blade(*n));
        if (gDebug.ai) {
            // what it knows: home and patrol, territory, sight, last known spot
            glm::vec3 h = n->home + glm::vec3(0, 0.2f, 0);
            dd.cross(h, 1.5f, GREY);
            if (n->type && n->type->territoryRadius > 0.0f) dd.circle(h, n->type->territoryRadius, GREY, 0.4f, 48);
            if (n->type && n->type->leash > 0.0f) dd.circle(h, n->type->leash, RED, 0.25f, 48);
            for (size_t i = 0; i < n->patrol.size(); i++)
                dd.line(n->patrol[i] + glm::vec3(0, 0.3f, 0), n->patrol[(i + 1) % n->patrol.size()] + glm::vec3(0, 0.3f, 0), GREY, 0.5f);
            if (n->type && n->type->sightRange > 0.0f && !n->dead()) {
                float half = n->type->sightFov * 0.5f;
                sector(dd, n->position + glm::vec3(0, 15, 0), n->facing, -half, half, n->type->sightRange, n->seesTarget ? RED : YELLOW, 0.25f, true);
            }
            // its path (npc/Pathfinder.h)
            if (!n->path.empty() && n->pathAt < n->path.size()) {
                glm::vec3 prev = n->position + glm::vec3(0, 0.5f, 0);
                for (size_t i = n->pathAt; i < n->path.size(); i++) {
                    glm::vec3 q = n->path[i] + glm::vec3(0, 0.5f, 0);
                    dd.line(prev, q, n->pathReached ? CYAN : MAGENTA, 0.8f);
                    prev = q;
                }
            }
            if (n->target) {
                dd.cross(n->lastKnown + glm::vec3(0, 1, 0), 1.2f, n->seesTarget ? RED : ORANGE);
                dd.line(n->position + glm::vec3(0, 15, 0), n->lastKnown + glm::vec3(0, 1, 0), n->seesTarget ? RED : ORANGE, 0.5f);
            }
        }
    }

    if (gDebug.steering)
        for (const auto& n : sim.npcs().all()) {
            if (n->dead() || sim.now() - n->steerAt > 0.5f) continue;       // not steering just now
            const steer::Map& m = n->steerMap;
            glm::vec3 o = n->position + glm::vec3(0, 1.0f, 0);
            const float R = 8.0f;                                            // a full ray: 8 voxels
            for (int i = 0; i < steer::DIRS; i++) {
                glm::vec2 d = steer::dirOf(i);
                glm::vec3 d3(d.x, 0, d.y);
                if (m.interest[i] > 0.01f) dd.line(o, o + d3 * (R * m.interest[i]), GREEN, 0.9f);
                if (m.danger[i] > 0.01f) dd.line(o + glm::vec3(0, 0.6f, 0), o + glm::vec3(0, 0.6f, 0) + d3 * (R * m.danger[i]), RED, 0.9f);
            }
            if (m.chosen >= 0) {
                glm::vec3 c(m.move.x, 0, m.move.y);
                if (glm::length(c) > 1e-3f) dd.line(o + glm::vec3(0, 1.2f, 0), o + glm::vec3(0, 1.2f, 0) + glm::normalize(c) * (R * 1.5f), YELLOW, 1.0f);
            }
            dd.line(o, n->steerGoal + glm::vec3(0, 1.0f, 0), WHITE, 0.35f);
            dd.cross(n->steerGoal + glm::vec3(0, 0.5f, 0), 1.0f, WHITE, 0.7f);
        }

    if (gDebug.ai) {
        // breadcrumbs (npc/Encounters.h): the truth of where players went
        for (const auto& p : sim.players())
            if (const auto* t = sim.encounters().trail(p->uuid)) {
                const encounter::Crumb* prev = nullptr;
                for (const auto& c : *t) {
                    dd.cross(c.at + glm::vec3(0, 0.3f, 0), 0.6f, YELLOW, 0.6f);
                    if (prev) dd.line(prev->at + glm::vec3(0, 0.3f, 0), c.at + glm::vec3(0, 0.3f, 0), YELLOW, 0.3f);
                    prev = &c;
                }
            }
        // encounters: each NPC's slot, attack tokens
        for (const auto& e : sim.encounters().all()) {
            if (e.ended >= 0.0f) continue;
            for (const auto& c : e.claims) {
                Npc* n = sim.npcs().find(c.npc);
                Human* t = sim.find(c.target);
                if (!n || !t) continue;
                auto it = e.tokens.find(c.target);
                bool token = it != e.tokens.end() && std::find(it->second.begin(), it->second.end(), c.npc) != it->second.end();
                if (token) dd.circle(n->position + glm::vec3(0, 0.2f, 0), Human::RADIUS + 1.0f, RED, 1.0f, 12);
                if (gDebug.labels) dd.label(n->position + glm::vec3(0, -1.0f, 0), std::string(slotName(c.slot)) + (token ? " [token]" : ""), token ? RED : GREY);
            }
            for (EntityId p : e.players)
                if (Human* t = sim.find(p)) {
                    char b[64]; std::snprintf(b, sizeof(b), "encounter %u: %zu fighting", e.id, e.active.size());
                    if (gDebug.labels) dd.label(t->position + glm::vec3(0, -2.5f, 0), b, ORANGE);
                }
        }
    }

    if (gDebug.sight)
        for (const auto& r : sim.sightLog())
            dd.line(r.a, r.b, r.clear ? GREEN : RED, std::max(0.2f, 1.0f - r.age * 0.5f));

    if (gDebug.props && wood) {
        for (const auto& lg : wood->allLogs()) {
            const RigidVoxelBody& rb = lg->body;
            glm::vec3 c = rb.isHinged() ? ORANGE : rb.settled() ? GREY : YELLOW;
            float r = rb.boundRadius();
            dd.box(rb.centre() - glm::vec3(r), rb.centre() + glm::vec3(r), c, 0.5f);
            dd.cross(rb.centre(), 1.5f, c);
            if (gDebug.labels) {
                char b[120];
                std::snprintf(b, sizeof(b), "%s  hp %.0f/%.0f  wood %d/%d  %.0fkg  %s", lg->name.c_str(), lg->health,
                              lg->maxHealth, lg->woodLeft, lg->totalWood, rb.massKg(),
                              rb.isHinged() ? "hinged" : rb.settled() ? "asleep" : "awake");
                dd.label(rb.centre() + glm::vec3(0, r * 0.5f + 2.0f, 0), b, c);
            }
        }
        for (const auto& d : wood->itemDrops())
            dd.box(d.pos - glm::vec3(0.6f), d.pos + glm::vec3(0.6f), d.resting ? GREY : YELLOW, 0.8f);
        for (size_t i = 1; i < axeBlade.size(); i++)
            for (int k = 0; k < 3; k++) dd.line(axeBlade[i - 1][k], axeBlade[i][k], RED);
    }

    if (gDebug.world && local.rules().hasBody) {
        int cx = (int)std::floor(local.position.x / VOXEL_DIM), cz = (int)std::floor(local.position.z / VOXEL_DIM);
        float y0 = local.position.y - 20.0f, y1 = local.position.y + 40.0f;
        for (int i = 0; i <= 1; i++)
            for (int k = 0; k <= 1; k++) {
                float x = (float)((cx + i) * VOXEL_DIM), z = (float)((cz + k) * VOXEL_DIM);
                dd.line({ x, y0, z }, { x, y1, z }, YELLOW, 0.6f);
            }
        for (float y : { std::floor(local.position.y) }) {
            float x0 = (float)(cx * VOXEL_DIM), x1 = x0 + VOXEL_DIM, z0 = (float)(cz * VOXEL_DIM), z1 = z0 + VOXEL_DIM;
            dd.line({ x0, y, z0 }, { x1, y, z0 }, YELLOW, 0.4f);
            dd.line({ x1, y, z0 }, { x1, y, z1 }, YELLOW, 0.4f);
            dd.line({ x1, y, z1 }, { x0, y, z1 }, YELLOW, 0.4f);
            dd.line({ x0, y, z1 }, { x0, y, z0 }, YELLOW, 0.4f);
        }
    }
}

} // namespace debugview
