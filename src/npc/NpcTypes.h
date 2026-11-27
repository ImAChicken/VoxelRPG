#pragma once
#include "../game/Spells.h"
#include "../audio/Voice.h"
#include "../character/Appearance.h"
#include "../character/MonsterAnim.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <map>
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
    std::string offhand;               // left hand: a shield (items.toml shield = …) it guards with
    // A second weapon it switches to when an enemy is on top of it
    // (an archer's dagger: style "ranged")
    std::string sidearm;
    // How it sounds (audio/Voice.h): npcs.toml voice = { … }. Each NPC
    // of the type a little different (voice_vary). None given: a person
    // uses their look's voice. barks: short lines it says on its own —
    // spot (it sees an enemy), hurt, die, greet (talked to), idle
    bool  hasVoice = false;
    Voice voice;
    float voiceVary = 0.08f;
    std::map<std::string, std::vector<std::string>> barks;
    const std::vector<std::string>* barksFor(const std::string& when) const {
        auto it = barks.find(when);
        return it == barks.end() || it->second.empty() ? nullptr : &it->second;
    }
    float health = 100.0f, poise = 35.0f;
    int   armor = 0;                   // reactions it shrugs off: 0 none, 1 flinches, 2 stumbles too, 3 launches too (game/Combat.h React)
    float climb = 1.0f;                // how much slopes slow it (0: not at all; Human::climb)
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
    // how quick it is in a fight (npc/States.h Combat): the wind-up it
    // must see before it can answer an attack (s), the odds it does
    // (rolls away or guards), and how fast it turns to follow (deg/s)
    float reaction = 0.3f, readChance = 0.6f, turnSpeed = 540.0f;
    // how it fights in a group (npc/Encounters.h): style sets the
    // rest unless npcs.toml says otherwise
    //   duelist  one attacks at a time, a pause between turns, the
    //            rest stand back watching (bandits, guards)
    //   pack     two at a time, quick turns (wolves)
    //   horde    many at once, no turns, pressing in close (undead)
    //   ranged   keeps its distance and shoots (a bow); backs off from
    //            anyone too close, and with its back to a wall (or
    //            caught very close) fights with its sidearm
    std::string style = "duelist";
    // ranged: the distance it shoots from (vox), how long it holds
    // the draw (s: the telegraph; a full draw is about 1.5) and the
    // pause between shots
    float rangeMin = 22.0f, rangeMax = 70.0f;
    // A boss (boss = true: a health bar while you fight it; style
    // "boss" picks its moves by distance): below 'phase' of its health
    // its second phase (phase_speed × its attack speed; the arena's
    // phase-two coffins open: dungeon/Run.h)
    bool  boss = false;
    float phase = 0.5f, phaseSpeed = 1.25f;
    float drawMin = 0.8f, drawMax = 1.5f;
    float shotGapMin = 0.8f, shotGapMax = 1.8f;
    int   attackers = 1;                     // how many of its style attack one target at once
    float gapMin = 0.8f, gapMax = 1.5f;      // s after an attack ends before the next may start
    float engageDist = 9.0f, holdDist = 24.0f;   // vox from the target: attacking, waiting a turn
    bool  menuProtected = false;
    float healsAfter = 0.0f;           // > 0: back to full health after this long unhurt
    bool  tracks = false;              // follows a trail of breadcrumbs past where it lost you
    float xp = -1.0f;                  // XP for killing it (< 0: health / 4)
    int   pack = 0;                    // slots it carries (0: none; game/Container.h)
    std::string loot;                  // loot table its pack is rolled from when first searched
    std::vector<std::string> wear;     // worn items shown on it (items.toml: gear)

    // ── Its body (body = { … }; left out: a person) ───────
    std::string shape = "human";       // "human", or "slime": a hopping blob (no skeleton)
    float bodyHeight = 0.0f;           // vox (0: a person's)
    float bodyWidth = 0.0f;            // vox across, for hits and drawing (0: a person's)
    float hitHeight = 0.0f;            // vox: how tall its hit box is (0: its height)
    float bodyKg = 0.0f;               // its weight (0: from its look)
    glm::vec3 color{ 0.4f, 0.8f, 0.4f };
    // drawn as a model with its own skeleton and clips instead of the
    // shape (data/models/monsters/<model>.vmon; character/MonsterModel.h)
    std::string model;
    MonsterClipNames clips;            // body.clips = { idle, walk, run, jump, attack, attack2, hit, death, walk_speed, run_speed }
    // ── Hopping (hop = { … }; physics/VoxelPhysics.h hopTick)
    bool  hops = false;
    float hopEvery = 0.45f, hopSpeed = 18.0f, hopHeight = 3.0f, hopClimb = 9.0f;
    float lungeSpeed = 15.0f, lungeHeight = 5.0f;
    // ── Contact damage (contact = { … }; Simulation::contactDamage):
    // whatever not on its side it touches is hurt, once every 'every' s
    float contactDamage = 0.0f, contactPoise = 0.0f, contactKnock = 0.0f, contactEvery = 0.8f;
    float lungeRange = 0.0f;           // vox: how far its lunge carries (worked out at load)
    // ── Swings (swings = [ { … } ]; Simulation::swingTick): a body
    // with no weapon of its own (a monster model) still swings: one
    // of these, picked by weight, played as its clip; at hit_at (s into
    // the clip) everything in front of it within reach and the arc is
    // hit once. speed: how fast the clip plays (under 1: a slower,
    // easier to read wind-up). A guard raised toward it blocks.
    struct Swing {
        std::string clip;
        float damage = 20.0f, poise = 30.0f, knock = 18.0f;
        float reach = 10.0f, arc = 70.0f;          // vox from its middle; degrees either side of where it faces
        float hitAt = 0.33f, length = 0.87f, speed = 1.0f, weight = 1.0f;
        bool  knockdown = false;
        std::string spell;                         // a spell cast at hit_at instead of a blow (spells.toml)
    };
    std::vector<Swing> swings;
    // ── Spells (casts = [ { spell, range = [min, max], every } ]):
    // in a fight it casts one now and then, when the target's that far
    // (npc/States.h); resist = { fire = 0.5 }: less of that damage type
    struct Cast { std::string spell; float rangeMin = 0.0f, rangeMax = 60.0f, every = 4.0f; };
    std::vector<Cast> casts;
    float resist[(int)spell::Element::COUNT] = {};
    // ── Flying (fly = { … }; VoxelPhysics::flierTick, npc/States.h
    // flyerFight): no gravity, it goes where it likes at 'speed' (vox/s;
    // 'fast' darting in), keeping 'hover' vox over the ground when it
    // wanders and about that over its target when it fights.
    // grapple_point = [x, y, z]: where on it a grapple can catch (vox
    // from its feet, x forward), to swing from it (game/Grapple.h)
    bool  flies = false;
    float flySpeed = 20.0f, flyFast = 42.0f, hover = 18.0f;
    bool  grapplePoint = false;
    glm::vec3 grappleAt{ 0.0f };

    bool person() const { return shape == "human"; }

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
                    d->offhand = t["offhand"].value_or(std::string());
                    if (auto* vt = t["voice"].as_table()) {
                        Voice& v = d->voice;
                        auto f = [&](const char* k, float& out) { if (auto x = (*vt)[k].value<double>()) out = (float)*x; };
                        f("pitch", v.pitch); f("range", v.range); f("size", v.size); f("speed", v.speed);
                        f("breath", v.breath); f("rough", v.rough); f("nasal", v.nasal); f("volume", v.volume);
                        std::string st = (*vt)["style"].value_or(std::string("soft"));
                        v.style = st == "chirpy" ? 1 : st == "grunty" ? 2 : 0;
                        v.clamp();
                        d->hasVoice = true;
                    }
                    d->voiceVary = (float)t["voice_vary"].value_or(0.08);
                    if (auto* bt = t["barks"].as_table())
                        for (auto&& [k, v] : *bt)
                            if (auto* a = v.as_array())
                                for (auto& x : *a) if (auto line = x.value<std::string>()) d->barks[std::string(k.str())].push_back(*line);
                    d->loot = t["loot"].value_or(std::string());
                    if (auto* w = t["wear"].as_array())
                        for (auto& x : *w) if (auto v = x.value<std::string>()) d->wear.push_back(*v);
                    d->pack = std::clamp((int)t["pack"].value_or((int64_t)(d->loot.empty() ? 0 : 20)), 0, 50);
                    d->health = (float)t["health"].value_or(100.0);
                    d->climb = (float)t["climb"].value_or(1.0);
                    d->poise = (float)t["poise"].value_or(35.0);
                    d->armor = std::clamp((int)t["armor"].value_or((int64_t)0), 0, 4);
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
                    d->reaction   = (float)t["reaction"].value_or(0.3);
                    d->readChance = std::clamp((float)t["read_chance"].value_or(0.6), 0.0f, 1.0f);
                    d->turnSpeed  = std::max(30.0f, (float)t["turn_speed"].value_or(540.0));
                    d->style = t["style"].value_or(std::string("duelist"));
                    if (d->style == "pack")       { d->attackers = 2; d->gapMin = 0.3f; d->gapMax = 0.7f; d->engageDist = 9.0f; d->holdDist = 18.0f; }
                    else if (d->style == "horde") { d->attackers = 5; d->gapMin = 0.0f; d->gapMax = 0.0f; d->engageDist = 7.0f; d->holdDist = 10.0f; }
                    else if (d->style == "boss") {}
                    else if (d->style == "ranged") { d->attackers = 3; d->gapMin = 0.0f; d->gapMax = 0.0f; d->holdDist = 40.0f; }
                    else d->style = "duelist";
                    pair(t["range"], d->rangeMin, d->rangeMax);
                    pair(t["draw"], d->drawMin, d->drawMax);
                    pair(t["shot_gap"], d->shotGapMin, d->shotGapMax);
                    d->sidearm = t["sidearm"].value_or(std::string());
                    d->boss = t["boss"].value_or(false);
                    d->phase = (float)t["phase"].value_or(0.5);
                    d->phaseSpeed = (float)t["phase_speed"].value_or(1.25);
                    if (d->style == "boss") { d->attackers = 1; d->gapMin = 0.0f; d->gapMax = 0.0f; }
                    d->attackers = std::max(1, (int)t["attackers"].value_or((int64_t)d->attackers));
                    pair(t["attack_gap"], d->gapMin, d->gapMax);
                    d->engageDist = (float)t["engage_dist"].value_or((double)d->engageDist);
                    d->holdDist = (float)t["hold_dist"].value_or((double)d->holdDist);
                    d->menuProtected = t["on_menu_attack"].value_or(std::string("interrupt")) == "protected";
                    d->healsAfter = (float)t["heals_after"].value_or(0.0);
                    d->tracks = t["tracks"].value_or(false);
                    d->xp = (float)t["xp"].value_or(-1.0);
                    if (auto* b = t["body"].as_table()) {
                        d->shape = (*b)["shape"].value_or(std::string("human"));
                        d->bodyHeight = (float)(*b)["height"].value_or(0.0);
                        d->bodyWidth = (float)(*b)["width"].value_or(0.0);
                        d->hitHeight = (float)(*b)["hit_height"].value_or(0.0);
                        d->bodyKg = (float)(*b)["mass"].value_or(0.0);
                        if (auto* c = (*b)["color"].as_array(); c && c->size() == 3)
                            d->color = { (float)c->get(0)->value_or(0.4), (float)c->get(1)->value_or(0.8), (float)c->get(2)->value_or(0.4) };
                        d->model = (*b)["model"].value_or(std::string());
                        if (auto* cl = (*b)["clips"].as_table()) {
                            MonsterClipNames& n = d->clips;
                            auto get = [&](const char* k, std::string& into) { if (auto v = (*cl)[k].value<std::string>()) into = *v; };
                            get("idle", n.idle); get("walk", n.walk); get("run", n.run); get("jump", n.jump);
                            get("attack", n.attack); get("attack2", n.attack2); get("hit", n.hit); get("death", n.death); get("cast", n.cast);
                            n.walkSpeed = (float)(*cl)["walk_speed"].value_or((double)n.walkSpeed);
                            n.runSpeed = (float)(*cl)["run_speed"].value_or((double)n.runSpeed);
                        }
                    }
                    if (auto* h = t["hop"].as_table()) {
                        d->hops = true;
                        d->hopEvery = (float)(*h)["every"].value_or(0.45);
                        d->hopSpeed = (float)(*h)["speed"].value_or(18.0);
                        d->hopHeight = (float)(*h)["height"].value_or(3.0);
                        d->hopClimb = (float)(*h)["climb"].value_or(9.0);
                        d->lungeSpeed = (float)(*h)["lunge_speed"].value_or(15.0);
                        d->lungeHeight = (float)(*h)["lunge_height"].value_or(5.0);
                        // flat ground: up and down again at the lunge's speed (gravity 40)
                        d->lungeRange = d->lungeSpeed * 2.0f * std::sqrt(2.0f * d->lungeHeight / 40.0f);
                    }
                    if (auto* ca = t["casts"].as_array())
                        for (auto& e : *ca)
                            if (auto* ct = e.as_table()) {
                                NpcType::Cast c;
                                c.spell = (*ct)["spell"].value_or(std::string());
                                if (auto* r = (*ct)["range"].as_array(); r && r->size() == 2) {
                                    c.rangeMin = (float)(*r)[0].value_or(0.0); c.rangeMax = (float)(*r)[1].value_or(60.0);
                                }
                                c.every = (float)(*ct)["every"].value_or(4.0);
                                if (!c.spell.empty()) d->casts.push_back(c);
                            }
                    if (auto* rs = t["resist"].as_table())
                        for (auto& [k, v] : *rs) d->resist[(int)spell::elementFrom(std::string(k.str()))] = (float)v.value_or(0.0);
                    if (auto* f = t["fly"].as_table()) {
                        d->flies = true;
                        d->flySpeed = (float)(*f)["speed"].value_or(20.0);
                        d->flyFast = (float)(*f)["fast"].value_or(42.0);
                        d->hover = (float)(*f)["hover"].value_or(18.0);
                        if (auto* gp = (*f)["grapple_point"].as_array(); gp && gp->size() >= 3) {
                            d->grapplePoint = true;
                            d->grappleAt = glm::vec3((float)(*gp)[0].value_or(0.0), (float)(*gp)[1].value_or(0.0), (float)(*gp)[2].value_or(0.0));
                        }
                    }
                    if (auto* sw = t["swings"].as_array())
                        for (auto& e : *sw)
                            if (auto* st = e.as_table()) {
                                NpcType::Swing s;
                                s.clip = (*st)["clip"].value_or(std::string());
                                s.damage = (float)(*st)["damage"].value_or((double)s.damage);
                                s.poise = (float)(*st)["poise"].value_or((double)s.poise);
                                s.knock = (float)(*st)["knock"].value_or((double)s.knock);
                                s.reach = (float)(*st)["reach"].value_or((double)s.reach);
                                s.arc = (float)(*st)["arc"].value_or((double)s.arc);
                                s.hitAt = (float)(*st)["hit_at"].value_or((double)s.hitAt);
                                s.length = std::max(0.1f, (float)(*st)["length"].value_or((double)s.length));
                                s.speed = std::max(0.1f, (float)(*st)["speed"].value_or((double)s.speed));
                                s.weight = std::max(0.0f, (float)(*st)["weight"].value_or((double)s.weight));
                                s.knockdown = (*st)["knockdown"].value_or(false);
                                s.spell = (*st)["spell"].value_or(std::string());
                                d->swings.push_back(s);
                            }
                    if (auto* c = t["contact"].as_table()) {
                        d->contactDamage = (float)(*c)["damage"].value_or(0.0);
                        d->contactPoise = (float)(*c)["poise"].value_or(0.0);
                        d->contactKnock = (float)(*c)["knock"].value_or(0.0);
                        d->contactEvery = (float)(*c)["every"].value_or(0.8);
                    }
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
