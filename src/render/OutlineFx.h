#pragma once
#include "../entity/Human.h"
#include "../game/Combat.h"
#include "../sim/Simulation.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// OutlineFx — outlines that glow or flash
// ══════════════════════════════════════════════════════════
// What each character's outline shows right now: a colour and a
// strength (0 = the plain outline), drawn by the character
// renderer's edge pass into the lines, a halo and the two-tone
// echo. The looks are data (data/registry/outline.toml); the
// triggers are here:
//   hurt, blocked, guard_break   from the simulation's Hit events
//   charge                       from a body's attack wind-up
//
// Purely what is drawn: each machine works it out from the events
// and states it already has.
class OutlineFx {
public:
    struct Effect {
        std::string id;
        glm::vec3   color{1.0f};
        float       time = 0.3f, strength = 1.0f, minWindup = 0.0f;
        int         blinks = 0, priority = 0;
    };

    bool load(const std::string& path) {
        effects.clear();
        active.clear();          // they point into 'effects'
        try {
            toml::table t = toml::parse_file(path);
            if (auto* arr = t["effect"].as_array())
                for (auto& node : *arr) {
                    auto* e = node.as_table();
                    if (!e) continue;
                    Effect f;
                    f.id = (*e)["id"].value_or(std::string());
                    if (auto* c = (*e)["color"].as_array(); c && c->size() == 3)
                        f.color = glm::vec3((float)(*c)[0].value_or(1.0), (float)(*c)[1].value_or(1.0), (float)(*c)[2].value_or(1.0));
                    f.time = std::max(0.01f, (float)(*e)["time"].value_or(0.3));
                    f.strength = std::clamp((float)(*e)["strength"].value_or(1.0), 0.0f, 1.0f);
                    f.blinks = std::max(0, (int)(*e)["blinks"].value_or((int64_t)0));
                    f.priority = (int)(*e)["priority"].value_or((int64_t)0);
                    f.minWindup = (float)(*e)["min_windup"].value_or(0.0);
                    if (!f.id.empty()) effects[f.id] = f;
                }
        } catch (const toml::parse_error& err) {
            std::cerr << path << ": " << err.description() << "\n";
            return false;
        }
        return true;
    }

    // A simulation event: start the flashes it causes
    void onEvent(const SimEvent& e) {
        if (e.kind != SimEvent::Kind::Hit || !e.b) return;
        if (e.parried)          { start(e.b, "parry"); start(e.a, "exposed"); return; }
        if (e.riposte)          start(e.b, "riposte");
        else if (e.guardBroken) start(e.b, "guard_break");
        else if (e.blocked)     start(e.b, "blocked");
        else if (e.amount > 0)  start(e.b, "hurt");
        if (e.a && !e.blocked && e.amount > 0) start(e.a, "struck");   // the attacker's own flash, with the stop
    }
    // Start an effect on a body by hand (console, tests, later triggers)
    void start(EntityId who, const std::string& id) {
        auto it = effects.find(id);
        if (it == effects.end()) return;
        auto& list = active[who];
        for (auto& a : list) if (a.fx == &it->second) { a.left = it->second.time; return; }   // restart
        list.push_back({ &it->second, it->second.time });
    }

    void update(float dt) {
        for (auto it = active.begin(); it != active.end();) {
            auto& list = it->second;
            for (auto& a : list) a.left -= dt;
            list.erase(std::remove_if(list.begin(), list.end(), [](const Running& a) { return a.left <= 0.0f; }), list.end());
            if (list.empty()) it = active.erase(it); else ++it;
        }
    }

    // The outline for a body now: rgb colour, a = strength (0..1)
    glm::vec4 glowFor(const Human& h) const {
        if (h.dead()) return glm::vec4(0.0f);
        glm::vec4 best(0.0f);
        int bestPri = -1000;
        auto offer = [&](const Effect& f, float s) {
            if (s <= 0.004f) return;
            if (f.priority > bestPri || (f.priority == bestPri && s > best.a)) { best = glm::vec4(f.color, s); bestPri = f.priority; }
        };
        auto it = active.find(h.uuid);
        if (it != active.end())
            for (const Running& a : it->second) {
                const Effect& f = *a.fx;
                float u = std::clamp(a.left / f.time, 0.0f, 1.0f);          // 1 → 0
                float s = f.strength * u;
                if (f.blinks > 0) {
                    // n flashes over the time: on for the first part of each
                    float phase = (1.0f - u) * (float)f.blinks;
                    s *= (phase - std::floor(phase)) < 0.55f ? 1.0f : 0.15f;
                }
                offer(f, s);
            }
        // charge: an attack's wind-up builds up to its hit window
        auto ch = effects.find("charge");
        if (ch != effects.end() && h.attack.active()) {
            const Effect& f = ch->second;
            const AttackDef& d = h.attack.def();
            float t = h.attack.t;
            if (d.activeStart >= f.minWindup && d.activeStart > 0.0f) {
                float s = 0.0f;
                if (t < d.activeStart) {
                    float u = t / d.activeStart;
                    s = f.strength * (0.25f + 0.75f * u * u);
                    if (u > 0.8f) s *= 0.75f + 0.25f * std::cos((u - 0.8f) * 120.0f);   // a shimmer just before
                } else {
                    s = f.strength * std::max(0.0f, 1.0f - (t - d.activeStart) / f.time);
                }
                offer(f, s);
            }
        }
        return best;
    }

    const Effect* effect(const std::string& id) const {
        auto it = effects.find(id);
        return it == effects.end() ? nullptr : &it->second;
    }

private:
    struct Running { const Effect* fx; float left; };
    std::unordered_map<std::string, Effect> effects;
    std::unordered_map<EntityId, std::vector<Running>> active;
};

inline OutlineFx gOutlineFx;
