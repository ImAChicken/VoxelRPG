#pragma once
// ══════════════════════════════════════════════════════════
// Particles — sparks, puffs, dust and splats
// ══════════════════════════════════════════════════════════
// Small, short-lived, purely for show: nothing in the game reads
// them back, and every machine makes its own (they're not sent over
// the network: each client spawns them from the same events).
//
// What they look like comes from data/registry/particles.toml: each
// [[emitter]] is one burst (how many, how fast, which way, colours,
// sizes, how long they live). The game fires them by id:
//   - on a hit (SimEvent::Hit, at the blade's contact): onEvent()
//   - from watching bodies move (a lunge's step, a dodge, a landing):
//     watch()
//   - by hand: burst(id, at, dir), the console's "particle <id>"
//
// This file is only the simulation (no GL, so tests can use it);
// render/ParticleRenderer.h draws them, all in two batched draws.
// Settings > Graphics > Particles: off, fewer (half), all.

#include "game/Settings.h"
#include "sim/Simulation.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace fx {

enum class Shape : uint8_t {
    Puff,     // a square facing the camera (smoke, dust, splat drops)
    Spark,    // a streak along its velocity (sparks, flecks)
};

struct Emitter {
    std::string id;
    int   countMin = 6, countMax = 10;
    float speedMin = 8.0f, speedMax = 20.0f;   // vox/s
    float spread   = 60.0f;                    // degrees around the direction (180: all ways)
    float up       = 0.0f;                     // added upward speed (vox/s)
    float gravity  = 30.0f;                    // vox/s² down (negative floats up)
    float drag     = 2.0f;                     // 1/s
    float lifeMin  = 0.25f, lifeMax = 0.5f;    // s
    float sizeStart = 0.6f, sizeEnd = 0.1f;    // vox
    float stretch  = 0.04f;                    // a spark's length: s of its velocity
    float jitter   = 0.3f;                     // vox: spawn spread around the point
    glm::vec4 colorStart{1.0f}, colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec3 colorVary{0.0f};                 // ± per particle (rgb)
    bool  additive = false;                    // glows (sparks): added, not laid over
    bool  bounce   = false;                    // stops on the ground instead of falling through
    Shape shape    = Shape::Puff;
};

struct Particle {
    glm::vec3 p{0.0f}, v{0.0f};
    glm::vec4 c0{1.0f}, c1{1.0f};
    float life = 0.0f, maxLife = 1.0f;
    float s0 = 0.5f, s1 = 0.1f, stretch = 0.0f;
    float gravity = 0.0f, drag = 0.0f;
    float light = 1.0f;                        // the light where it began (dark in caves)
    bool  additive = false, bounce = false;
    Shape shape = Shape::Puff;

    float age01() const { return 1.0f - std::clamp(life / std::max(maxLife, 1e-3f), 0.0f, 1.0f); }
    glm::vec4 color() const { return glm::mix(c0, c1, age01()); }
    float size() const { return glm::mix(s0, s1, age01()); }
};

class Particles {
public:
    static constexpr size_t MAX = 4000;        // past this, new bursts are trimmed

    // ── Data ─────────────────────────────────────────────
    bool load(const std::string& path) {
        emitters.clear();
        try {
            toml::table t = toml::parse_file(path);
            if (auto* arr = t["emitter"].as_array())
                for (auto& node : *arr) {
                    auto* e = node.as_table();
                    if (!e) continue;
                    Emitter m;
                    m.id = (*e)["id"].value_or(std::string());
                    range(*e, "count", m.countMin, m.countMax);
                    rangef(*e, "speed", m.speedMin, m.speedMax);
                    rangef(*e, "life", m.lifeMin, m.lifeMax);
                    rangef(*e, "size", m.sizeStart, m.sizeEnd);
                    m.spread   = (float)(*e)["spread"].value_or((double)m.spread);
                    m.up       = (float)(*e)["up"].value_or((double)m.up);
                    m.gravity  = (float)(*e)["gravity"].value_or((double)m.gravity);
                    m.drag     = (float)(*e)["drag"].value_or((double)m.drag);
                    m.stretch  = (float)(*e)["stretch"].value_or((double)m.stretch);
                    m.jitter   = (float)(*e)["jitter"].value_or((double)m.jitter);
                    m.additive = (*e)["additive"].value_or(false);
                    m.bounce   = (*e)["bounce"].value_or(false);
                    m.shape    = (*e)["shape"].value_or(std::string("puff")) == "spark" ? Shape::Spark : Shape::Puff;
                    color(*e, "color", m.colorStart);
                    m.colorEnd = glm::vec4(glm::vec3(m.colorStart), 0.0f);
                    color(*e, "color_end", m.colorEnd);
                    if (auto* c = (*e)["color_vary"].as_array(); c && c->size() >= 3)
                        m.colorVary = glm::vec3((float)(*c)[0].value_or(0.0), (float)(*c)[1].value_or(0.0), (float)(*c)[2].value_or(0.0));
                    m.countMin = std::max(0, m.countMin); m.countMax = std::max(m.countMin, m.countMax);
                    m.lifeMin = std::max(0.02f, m.lifeMin); m.lifeMax = std::max(m.lifeMin, m.lifeMax);
                    if (!m.id.empty()) emitters[m.id] = m;
                }
        } catch (const toml::parse_error& err) {
            std::cerr << path << ": " << err.description() << "\n";
            return false;
        }
        return true;
    }
    const Emitter* emitter(const std::string& id) const { auto it = emitters.find(id); return it == emitters.end() ? nullptr : &it->second; }
    size_t emitterCount() const { return emitters.size(); }

    // ── Spawning ─────────────────────────────────────────
    // A burst of emitter 'id' at 'at', thrown along 'dir' (any length;
    // zero: every way). 'light' 0..1 dims it (a cave); 'scale' its count.
    // Returns how many began.
    int burst(const std::string& id, glm::vec3 at, glm::vec3 dir = glm::vec3(0.0f), float light = 1.0f, float scale = 1.0f, float sizeScale = 1.0f) {
        const Emitter* e = emitter(id);
        if (!e || gSettings.particles <= 0) return 0;
        if (gSettings.particles == 1) scale *= 0.5f;
        int n = (int)std::round(randf((float)e->countMin, (float)e->countMax + 0.999f) * scale);
        n = std::min<int>(n, (int)(MAX > list.size() ? MAX - list.size() : 0));
        glm::vec3 d = glm::length(dir) > 1e-4f ? glm::normalize(dir) : glm::vec3(0.0f);
        for (int i = 0; i < n; i++) {
            Particle p;
            p.p = at + glm::vec3(randf(-1, 1), randf(-1, 1), randf(-1, 1)) * e->jitter;
            p.v = direction(d, e->spread) * randf(e->speedMin, e->speedMax) + glm::vec3(0.0f, e->up, 0.0f);
            glm::vec3 vary(randf(-1, 1) * e->colorVary.r, randf(-1, 1) * e->colorVary.g, randf(-1, 1) * e->colorVary.b);
            p.c0 = glm::clamp(e->colorStart + glm::vec4(vary, 0.0f), 0.0f, 4.0f);
            p.c1 = glm::clamp(e->colorEnd + glm::vec4(vary, 0.0f), 0.0f, 4.0f);
            p.maxLife = p.life = randf(e->lifeMin, e->lifeMax);
            float sz = randf(0.75f, 1.25f);
            p.s0 = e->sizeStart * sz * sizeScale; p.s1 = e->sizeEnd * sz * sizeScale;
            p.stretch = e->stretch; p.gravity = e->gravity; p.drag = e->drag;
            p.light = std::clamp(light, 0.0f, 1.0f);
            p.additive = e->additive; p.bounce = e->bounce; p.shape = e->shape;
            list.push_back(p);
        }
        return n;
    }

    // ── Combat ───────────────────────────────────────────
    // A hit (or block, parry …) sprays at the blade's contact, away
    // from the attacker
    void onEvent(const SimEvent& ev, Simulation& sim) {
        if (ev.kind == SimEvent::Kind::Killed) {
            if (Human* b = sim.find(ev.b)) burst(b->noRagdoll ? "death_slime" : "death_dust", b->position + glm::vec3(0, 2, 0), glm::vec3(0, 1, 0), lightAt(b->position));
            return;
        }
        if (ev.kind == SimEvent::Kind::WallHit) {
            if (Human* b = sim.find(ev.b)) burst("wall_dust", ev.at, glm::vec3(0, 0.3f, 0), lightAt(ev.at));
            return;
        }
        if (ev.kind == SimEvent::Kind::PerfectDodge) {
            if (Human* b = sim.find(ev.b)) burst("perfect_dodge", b->position + glm::vec3(0, 7, 0), glm::vec3(0.0f), 1.0f);
            return;
        }
        if (ev.kind != SimEvent::Kind::Hit) return;
        Human* a = sim.find(ev.a);
        Human* b = sim.find(ev.b);
        if (!b) return;
        glm::vec3 at = ev.at != glm::vec3(0.0f) ? ev.at : b->position + glm::vec3(0.0f, b->height * 0.6f, 0.0f);
        glm::vec3 dir = a ? b->position - a->position : glm::vec3(0.0f);
        dir.y = 0.0f;
        dir = glm::length(dir) > 1e-3f ? glm::normalize(dir) : glm::vec3(0.0f);
        float L = lightAt(at);
        bool heavy = ev.attack == AttackKind::Heavy || ev.attack == AttackKind::AirHeavy;
        if (ev.parried) {
            burst("parry_flash", at, -dir, L);
            burst("block_sparks", at, -dir + glm::vec3(0, 0.4f, 0), L, 1.5f);
        } else if (ev.blocked) {
            burst(b->guardStyle.shield ? "block_wood" : "block_sparks", at, -dir + glm::vec3(0, 0.3f, 0), L, heavy ? 1.5f : 1.0f);
            if (ev.guardBroken) burst("guard_break", at, glm::vec3(0, 1, 0), L);
        } else if (ev.amount > 0.0f) {
            // a flash at the contact, bigger for a bigger reaction
            float big = ev.react == React::Launch || ev.react == React::Slam || ev.react == React::Knockdown ? 1.6f
                      : ev.react == React::Stumble ? 1.25f : 1.0f;
            burst("impact_flash", at, glm::vec3(0.0f), L, 1.0f, big + 0.5f * ev.charge);
            if (ev.react == React::Launch && b->grounded) burst("land_dust", b->position + glm::vec3(0, 0.3f, 0), glm::vec3(0, 1, 0), L);
            if (b->noRagdoll) burst("hit_slime", at, dir + glm::vec3(0, 0.5f, 0), L, heavy ? 1.6f : 1.0f);
            else {
                burst("hit_spark", at, dir, L, heavy ? 1.6f : 1.0f);
                burst("hit_puff", at, dir, L, heavy ? 1.5f : 1.0f);
            }
            if (ev.poiseBroken || ev.knockedDown) burst("poise_break", at, glm::vec3(0, 1, 0), L);
        }
    }

    // ── Bodies moving ────────────────────────────────────
    // Once a tick (or frame): dust where a body lands hard, rolls,
    // or an attack's step kicks off
    void watch(const std::vector<Human*>& bodies) {
        std::unordered_map<EntityId, Seen> next;
        next.reserve(bodies.size());
        for (Human* h : bodies) {
            if (!h || h->dead()) continue;
            Seen now;
            now.grounded = h->grounded;
            now.fallSpeed = h->grounded ? 0.0f : std::max(0.0f, -h->velocity.y);
            now.move = (uint8_t)h->move;
            now.attackT = h->attack.active() ? h->attack.t : -1.0f;
            now.attackDef = h->attack.active() ? h->attack.d : nullptr;
            auto it = seen.find(h->uuid);
            if (it != seen.end()) {
                const Seen& was = it->second;
                glm::vec3 feet = h->position + glm::vec3(0.0f, 0.3f, 0.0f);
                float L = lightAt(feet + glm::vec3(0, 1, 0));
                // a landing: dust in a ring
                if (now.grounded && !was.grounded && was.fallSpeed > 25.0f)
                    burst("land_dust", feet, glm::vec3(0, 1, 0), L, std::clamp(was.fallSpeed / 50.0f, 0.6f, 2.0f));
                // a dodge: kicked up behind
                if (now.move != was.move && (h->move == Human::Move::Roll || h->move == Human::Move::Backstep) && h->grounded)
                    burst("dodge_dust", feet, -h->moveDir, L);
                // an attack's step: a scuff as it pushes off
                if (now.attackDef && h->grounded) {
                    const AttackDef& d = *now.attackDef;
                    bool started = was.attackDef != now.attackDef || was.attackT > now.attackT;
                    float from = started ? -1.0f : was.attackT;
                    if (d.stepDistance() >= 6.0f && from < d.lungeFrom && now.attackT >= d.lungeFrom)
                        burst("step_dust", feet, -h->attack.dir, L, std::clamp(d.stepDistance() / 10.0f, 0.6f, 1.8f));
                }
            }
            next[h->uuid] = now;
        }
        seen.swap(next);
    }

    // ── Moving them ──────────────────────────────────────
    // 'solid': is this point inside the ground (for those that bounce);
    // may be empty
    void update(float dt, const std::function<bool(glm::vec3)>& solid = {}) {
        dt = std::clamp(dt, 0.0f, 0.1f);
        for (Particle& p : list) {
            p.life -= dt;
            p.v.y -= p.gravity * dt;
            p.v *= std::exp(-p.drag * dt);
            glm::vec3 to = p.p + p.v * dt;
            if (p.bounce && solid && solid(to)) {
                // settle on the top it hit: lose most of the speed
                if (!solid(glm::vec3(to.x, p.p.y, to.z))) { to.y = p.p.y; p.v.y = -p.v.y * 0.25f; p.v.x *= 0.5f; p.v.z *= 0.5f; }
                else { to = p.p; p.v = glm::vec3(0.0f); }
            }
            p.p = to;
        }
        list.erase(std::remove_if(list.begin(), list.end(), [](const Particle& p) { return p.life <= 0.0f; }), list.end());
    }

    void clear() { list.clear(); seen.clear(); }
    const std::vector<Particle>& all() const { return list; }
    size_t count() const { return list.size(); }

    // the light at a point 0..1 (set by the game: the baked light, so
    // a puff in a cave isn't bright); unset, everything is lit
    std::function<float(glm::vec3)> lightAt = [](glm::vec3) { return 1.0f; };

private:
    struct Seen {
        bool grounded = true; float fallSpeed = 0.0f; uint8_t move = 0;
        float attackT = -1.0f; const AttackDef* attackDef = nullptr;
    };
    std::unordered_map<std::string, Emitter> emitters;
    std::vector<Particle> list;
    std::unordered_map<EntityId, Seen> seen;
    std::mt19937 rng{ 1234567u };

    float randf(float a, float b) { return a + (b - a) * std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
    // a unit vector within 'spreadDeg' of d (d zero: any way)
    glm::vec3 direction(glm::vec3 d, float spreadDeg) {
        if (d == glm::vec3(0.0f) || spreadDeg >= 179.0f) {
            float z = randf(-1, 1), a = randf(0, 6.2831853f), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            return glm::vec3(r * std::cos(a), z, r * std::sin(a));
        }
        glm::vec3 side = std::fabs(d.y) < 0.9f ? glm::normalize(glm::cross(d, glm::vec3(0, 1, 0))) : glm::vec3(1, 0, 0);
        glm::vec3 up = glm::cross(side, d);
        float cosMax = std::cos(glm::radians(spreadDeg));
        float c = randf(cosMax, 1.0f), s = std::sqrt(std::max(0.0f, 1.0f - c * c)), a = randf(0, 6.2831853f);
        return glm::normalize(d * c + (side * std::cos(a) + up * std::sin(a)) * s);
    }
    static void range(const toml::table& t, const char* k, int& a, int& b) {
        if (auto* arr = t[k].as_array(); arr && arr->size() >= 2) { a = (int)(*arr)[0].value_or((int64_t)a); b = (int)(*arr)[1].value_or((int64_t)b); }
        else if (auto v = t[k].value<int64_t>()) a = b = (int)*v;
    }
    static void rangef(const toml::table& t, const char* k, float& a, float& b) {
        if (auto* arr = t[k].as_array(); arr && arr->size() >= 2) { a = (float)(*arr)[0].value_or((double)a); b = (float)(*arr)[1].value_or((double)b); }
        else if (auto v = t[k].value<double>()) a = b = (float)*v;
    }
    static void color(const toml::table& t, const char* k, glm::vec4& c) {
        if (auto* arr = t[k].as_array(); arr && arr->size() >= 3)
            c = glm::vec4((float)(*arr)[0].value_or(1.0), (float)(*arr)[1].value_or(1.0), (float)(*arr)[2].value_or(1.0),
                          arr->size() >= 4 ? (float)(*arr)[3].value_or(1.0) : 1.0f);
    }
};

} // namespace fx

inline fx::Particles gParticles;
