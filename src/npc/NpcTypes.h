#pragma once
#include "../character/Appearance.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// NpcTypes — kinds of NPC (data/registry/npcs.toml) and how
// they look (data/characters/presets.toml)
// ══════════════════════════════════════════════════════════
// Everything about an NPC that isn't code: faction, look,
// weapon, toughness, senses, territory, which brain states it
// may use and how it fights. See npcs.toml for each field.

// Places around a target in a fight (npc/Encounter.h)
enum class Slot : uint8_t { Front, FrontLeft, FrontRight, Left, Right, RearLeft, RearRight, RearFlank, Ring, Count };
inline Slot slotFromName(const std::string& n) {
    if (n == "front") return Slot::Front;
    if (n == "front_left") return Slot::FrontLeft;
    if (n == "front_right") return Slot::FrontRight;
    if (n == "left") return Slot::Left;
    if (n == "right") return Slot::Right;
    if (n == "rear_left") return Slot::RearLeft;
    if (n == "rear_right") return Slot::RearRight;
    if (n == "rear_flank" || n == "rear") return Slot::RearFlank;
    return Slot::Ring;
}
inline const char* slotName(Slot s) {
    static const char* N[] = { "front", "front_left", "front_right", "left", "right", "rear_left", "rear_right", "rear_flank", "ring", "?" };
    return N[(int)s];
}

struct NpcType {
    std::string id, name;
    std::string faction;
    std::string look;
    std::string weapon;
    float health = 100.0f, poise = 35.0f;
    std::vector<std::string> states;
    std::string start;
    bool  wander = false;
    float wanderRadius = 16.0f;
    float territoryRadius = 0.0f;      // 0: none
    float leash = 0.0f;                // 0: no limit
    int   patrolPoints = 0;
    float sightRange = 60.0f, sightFov = 140.0f;
    float hearing = 1.0f;
    float alertMin = 0.6f, alertMax = 1.2f;
    float cryRange = 0.0f, cryCooldown = 20.0f;   // cryRange 0: no battle cry
    std::vector<Slot> slots;
    float aggression = 0.7f;
    bool  menuProtected = false;
    float healsAfter = 0.0f;           // > 0: back to full health after this long unhurt
    bool  tracks = false;              // follows a trail of breadcrumbs past where it lost you
    float xp = -1.0f;                  // XP for killing it (< 0: health / 4)
    int   pack = 0;                    // slots it carries (0: none; game/Container.h)
    std::string loot;                  // loot table its pack is rolled from when first searched
    std::vector<std::string> wear;     // worn items shown on it (items.toml: gear)

    bool has(const std::string& state) const {
        return state == "dead" || std::find(states.begin(), states.end(), state) != states.end();
    }
};

// ── Appearance presets ────────────────────────────────────
struct LookPreset {
    std::string id;
    bool random = true;
    std::vector<std::pair<std::string, std::string>> keys;               // look_* = value
    std::vector<std::pair<std::string, std::vector<std::string>>> choices; // look key → one of
    std::string textureSet;

    Appearance make(uint32_t seed) const {
        Appearance a = random ? Appearance::random(seed) : Appearance();
        for (auto& [k, v] : keys) a.read(k, v);
        uint32_t r = seed * 2654435761u + 7u;
        for (auto& [k, list] : choices) {
            if (list.empty()) continue;
            r = r * 1664525u + 1013904223u;
            a.read(k, list[(r >> 8) % list.size()]);
        }
        return a;
    }
};

class NpcTypeRegistry {
public:
    bool load(const char* typesPath, const char* presetsPath) {
        std::unordered_map<std::string, std::unique_ptr<NpcType>> t2;
        std::unordered_map<std::string, std::unique_ptr<LookPreset>> p2;
        try {
            auto tbl = toml::parse_file(presetsPath);
            if (auto arr = tbl["preset"].as_array())
                for (auto& e : *arr) {
                    auto* t = e.as_table(); if (!t) continue;
                    auto p = std::make_unique<LookPreset>();
                    p->id = (*t)["id"].value_or(std::string());
                    p->random = (*t)["random"].value_or(true);
                    p->textureSet = (*t)["texture_set"].value_or(std::string());
                    for (auto& [k, v] : *t) {
                        std::string key(k.str());
                        if (key.rfind("look_", 0) == 0) { if (auto s = v.value<std::string>()) p->keys.push_back({ key, *s }); }
                        else if (key.size() > 8 && key.compare(key.size() - 8, 8, "_choices") == 0) {
                            std::vector<std::string> list;
                            if (auto a = v.as_array()) for (auto& x : *a) if (auto s = x.value<std::string>()) list.push_back(*s);
                            p->choices.push_back({ "look_" + key.substr(0, key.size() - 8), list });
                        }
                    }
                    if (!p->id.empty()) p2[p->id] = std::move(p);
                }
        } catch (const toml::parse_error& e) {
            std::cerr << presetsPath << ": " << e.description() << "\n";
        }
        try {
            auto tbl = toml::parse_file(typesPath);
            auto pair = [](const toml::node_view<toml::node>& n, float& a, float& b) {
                if (auto arr = n.as_array(); arr && arr->size() == 2) {
                    a = (float)arr->get(0)->value_or(0.0); b = (float)arr->get(1)->value_or(0.0);
                }
            };
            if (auto arr = tbl["npc"].as_array())
                for (auto& e : *arr) {
                    auto* tp = e.as_table(); if (!tp) continue;
                    toml::table& t = *tp;
                    auto d = std::make_unique<NpcType>();
                    d->id = t["id"].value_or(std::string());
                    d->name = t["name"].value_or(d->id);
                    d->faction = t["faction"].value_or(std::string());
                    d->look = t["look"].value_or(std::string());
                    d->weapon = t["weapon"].value_or(std::string());
                    d->loot = t["loot"].value_or(std::string());
                    if (auto* w = t["wear"].as_array())
                        for (auto& x : *w) if (auto v = x.value<std::string>()) d->wear.push_back(*v);
                    d->pack = std::clamp((int)t["pack"].value_or((int64_t)(d->loot.empty() ? 0 : 20)), 0, 50);
                    d->health = (float)t["health"].value_or(100.0);
                    d->poise = (float)t["poise"].value_or(35.0);
                    if (auto s = t["states"].as_array())
                        for (auto& x : *s) if (auto v = x.value<std::string>()) d->states.push_back(*v);
                    d->start = t["start"].value_or(std::string(d->has("patrol") ? "patrol" : "idle"));
                    d->wander = t["idle"].value_or(std::string("stand")) == "wander";
                    d->wanderRadius = (float)t["wander_radius"].value_or(16.0);
                    d->territoryRadius = (float)t["territory"]["radius"].value_or(0.0);
                    d->leash = (float)t["territory"]["leash"].value_or(0.0);
                    d->patrolPoints = (int)t["patrol_points"].value_or((int64_t)0);
                    d->sightRange = (float)t["sight"]["range"].value_or(60.0);
                    d->sightFov = (float)t["sight"]["fov"].value_or(140.0);
                    d->hearing = (float)t["hearing"].value_or(1.0);
                    pair(t["alert_time"], d->alertMin, d->alertMax);
                    d->cryRange = (float)t["battle_cry"]["range"].value_or(0.0);
                    d->cryCooldown = (float)t["battle_cry"]["cooldown"].value_or(20.0);
                    if (auto s = t["slots"].as_array())
                        for (auto& x : *s) if (auto v = x.value<std::string>()) d->slots.push_back(slotFromName(*v));
                    if (d->slots.empty()) d->slots = { Slot::Front, Slot::Left, Slot::Right, Slot::RearFlank, Slot::Ring };
                    d->aggression = (float)t["aggression"].value_or(0.7);
                    d->menuProtected = t["on_menu_attack"].value_or(std::string("interrupt")) == "protected";
                    d->healsAfter = (float)t["heals_after"].value_or(0.0);
                    d->tracks = t["tracks"].value_or(false);
                    d->xp = (float)t["xp"].value_or(-1.0);
                    if (d->id.empty()) continue;
                    t2[d->id] = std::move(d);
                }
        } catch (const toml::parse_error& e) {
            std::cerr << typesPath << ": " << e.description() << "\n";
            return false;
        }
        for (auto& [k, v] : types) retired.push_back(std::move(v));
        types = std::move(t2);
        presets = std::move(p2);
        std::cout << "NPC types: " << types.size() << ", looks: " << presets.size() << "\n";
        return true;
    }
    const NpcType* find(const std::string& id) const { auto it = types.find(id); return it == types.end() ? nullptr : it->second.get(); }
    const LookPreset* look(const std::string& id) const { auto it = presets.find(id); return it == presets.end() ? nullptr : it->second.get(); }
    std::vector<std::string> ids() const {
        std::vector<std::string> v;
        for (auto& [k, x] : types) v.push_back(k);
        std::sort(v.begin(), v.end());
        return v;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<NpcType>> types;
    std::unordered_map<std::string, std::unique_ptr<LookPreset>> presets;
    std::vector<std::unique_ptr<NpcType>> retired;
};
inline NpcTypeRegistry gNpcTypes;
