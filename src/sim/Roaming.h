#pragma once
#include "../npc/NpcManager.h"
#include "../npc/Pathfinder.h"
#include "../physics/IPhysics.h"
#include "../worldgen/IslandChart.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Roaming — enemies that turn up out in the world
// ══════════════════════════════════════════════════════════
// Every so often, for each player with a body, a group from
// data/registry/roaming.toml (the ones whose biome, time of day and
// distance from the world's spawn fit) appears out of sight around
// them: past min_distance from every player, under open sky, on
// dry ground, away from anything built and from villagers. A
// roaming NPC far from every player for a while goes again. Their brains are their types' own (bandits patrol where
// they appeared, the dead and slimes wander).
//
// A world setting (WorldSaveManager::roamingSpawns, console
// `roaming on|off`); the Simulation runs it when it's on. The world
// answers what it can't know through hooks (GameSession sets them):
// the biome of a column, and whether a spot is no place for one
// (water, buildings near).

struct RoamGroupDef {
    std::string id;
    std::vector<std::string> biomes;                 // keys (lower case), groups (capitalised), or "any"
    std::vector<std::pair<std::string, float>> npcs; // type, weight
    int   minSize = 1, maxSize = 1;
    float weight = 1.0f;
    enum class Time : uint8_t { Any, Day, Night } time = Time::Any;
    float fromSpawnMin = 0.0f, fromSpawnMax = 0.0f;  // 0: no limit
    float spread = 6.0f;
};

struct RoamSettings {
    float checkEvery = 6.0f, chance = 0.45f;
    float cooldownMin = 60.0f, cooldownMax = 120.0f, nightCooldownMin = 35.0f, nightCooldownMax = 75.0f;
    int   maxNear = 6, maxNearNight = 9;
    float countRadius = 180.0f, minDistance = 70.0f, maxDistance = 120.0f, ahead = 0.6f;
    float openSky = 48.0f, safeRadius = 64.0f, spawnSafe = 160.0f;
    float despawnDistance = 260.0f, despawnAfter = 30.0f;
    float nightFrom = 20.0f, nightTo = 6.0f;
};

class RoamingTable {
public:
    static constexpr const char* PATH = "data/registry/roaming.toml";
    RoamSettings settings;
    std::vector<RoamGroupDef> groups;
    bool loaded = false;

    bool load(const char* path = PATH) {
        toml::table tbl;
        try { tbl = toml::parse_file(path); }
        catch (const toml::parse_error& e) { std::cerr << "roaming.toml: " << e.description() << "\n"; return false; }
        RoamSettings s;
        if (auto* t = tbl["settings"].as_table()) {
            auto num = [&](const char* k, float& v) { if (auto x = (*t)[k].value<double>()) v = (float)*x; };
            auto inum = [&](const char* k, int& v) { if (auto x = (*t)[k].value<int64_t>()) v = (int)*x; };
            auto pair = [&](const char* k, float& a, float& b) {
                if (auto* arr = (*t)[k].as_array(); arr && arr->size() >= 2) {
                    a = (float)arr->get(0)->value<double>().value_or(a);
                    b = (float)arr->get(1)->value<double>().value_or(b);
                }
            };
            num("check_every", s.checkEvery); num("chance", s.chance);
            pair("cooldown", s.cooldownMin, s.cooldownMax); pair("cooldown_night", s.nightCooldownMin, s.nightCooldownMax);
            inum("max_near", s.maxNear); inum("max_near_night", s.maxNearNight);
            num("count_radius", s.countRadius); num("min_distance", s.minDistance); num("max_distance", s.maxDistance);
            num("ahead", s.ahead); num("open_sky", s.openSky); num("safe_radius", s.safeRadius); num("spawn_safe", s.spawnSafe);
            num("despawn_distance", s.despawnDistance); num("despawn_after", s.despawnAfter);
            pair("night", s.nightFrom, s.nightTo);
            s.maxDistance = std::max(s.maxDistance, s.minDistance + 8.0f);
            s.checkEvery = std::max(0.5f, s.checkEvery);
        }
        std::vector<RoamGroupDef> gs;
        if (auto* arr = tbl["group"].as_array())
            for (auto& node : *arr) {
                auto* g = node.as_table();
                if (!g) continue;
                RoamGroupDef d;
                d.id = (*g)["id"].value_or(std::string("group"));
                if (auto* b = (*g)["biomes"].as_array())
                    for (auto& x : *b) if (auto v = x.value<std::string>()) d.biomes.push_back(*v);
                if (d.biomes.empty()) d.biomes.push_back("any");
                if (auto* n = (*g)["npcs"].as_table())
                    for (auto& [k, v] : *n) d.npcs.push_back({ std::string(k.str()), (float)v.value<double>().value_or(1.0) });
                if (auto* sz = (*g)["size"].as_array(); sz && sz->size() >= 2) {
                    d.minSize = (int)sz->get(0)->value<int64_t>().value_or(1);
                    d.maxSize = (int)sz->get(1)->value<int64_t>().value_or(d.minSize);
                }
                d.minSize = std::max(1, d.minSize); d.maxSize = std::max(d.minSize, d.maxSize);
                d.weight = (float)(*g)["weight"].value<double>().value_or(1.0);
                std::string tm = (*g)["time"].value_or(std::string("any"));
                d.time = tm == "day" ? RoamGroupDef::Time::Day : tm == "night" ? RoamGroupDef::Time::Night : RoamGroupDef::Time::Any;
                if (auto* fs = (*g)["from_spawn"].as_array(); fs && fs->size() >= 2) {
                    d.fromSpawnMin = (float)fs->get(0)->value<double>().value_or(0.0);
                    d.fromSpawnMax = (float)fs->get(1)->value<double>().value_or(0.0);
                }
                d.spread = (float)(*g)["spread"].value<double>().value_or(6.0);
                if (!d.npcs.empty()) gs.push_back(d);
            }
        settings = s;
        groups = gs;
        loaded = true;
        return true;
    }

    // Does a biome (island::Biome id; -1 or 255: unknown) match a list?
    static bool biomeMatches(const RoamGroupDef& g, int biome) {
        for (const std::string& b : g.biomes) {
            if (b == "any") return true;
            if (biome < 0 || biome >= island::BIOME_COUNT) continue;
            // a biome's key is lower case ("forest"); a whole group is
            // capitalised as in IslandChart ("Forest": the swamps too)
            if (b == key(biome) || b == island::biomeInfo(biome).group) return true;
        }
        return false;
    }
    // Biomes nothing roams in: open water and the villages
    static bool barren(int biome) {
        using namespace island;
        switch (biome) {
        case DeepOcean: case Ocean: case Shallows: case RockyShallows: case SwampShallows:
        case MountainLake: case LowMountainLake: case PlainsLake: case ForestLake:
        case MountainRiver: case LowlandRiver: case SwampRiver: case RiverFork:
        case LavaLake: case HotSpring: case Village: case PlainsVillage: case Bridge:
            return true;
        default: return false;
        }
    }
    static std::string key(int biome) {
        std::string s = island::biomeInfo(biome).name;
        for (char& c : s) c = c == ' ' ? '_' : (char)std::tolower((unsigned char)c);
        return s;
    }
    static std::string lower(std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }
};
inline RoamingTable gRoaming;

class Roaming {
public:
    bool enabled = false;
    // the world's answers (GameSession): the biome of a column (-1:
    // unknown), and why a spot is no place for one ("" if it's fine)
    std::function<int(int, int)> biomeAt;
    std::function<std::string(glm::vec3)> spotBlocked;
    glm::vec3 worldSpawn{ 0.0f };
    bool hasWorldSpawn = false;

    struct Who { uint64_t id; glm::vec3 pos; glm::vec3 heading; };   // a player with a body
    struct PlayerState { uint64_t id = 0; float check = 0.0f, cooldown = 0.0f; std::string why; };

    // what it has done (for `roaming`)
    int groupsMade = 0, npcsMade = 0, npcsGone = 0;
    std::string lastGroup;
    // the spots it tried lately (the debug overlay's "spawns" layer):
    // where, why it was turned down ("" = a group came), how long ago
    struct Try { glm::vec3 at; std::string why; float age = 0.0f; };
    std::vector<Try> tries;
    static constexpr float TRY_KEEP = 20.0f;

    void tick(float dt, const IPhysics& phys, NpcManager& npcs, const std::vector<Who>& players, float hour) {
        if (!gRoaming.loaded) gRoaming.load();
        const RoamSettings& s = gRoaming.settings;
        despawn(dt, npcs, players);
        for (Try& t : tries) t.age += dt;
        tries.erase(std::remove_if(tries.begin(), tries.end(), [](const Try& t) { return t.age > TRY_KEEP; }), tries.end());
        if (!enabled) return;
        // the players' timers
        for (auto it = state.begin(); it != state.end();)
            if (std::none_of(players.begin(), players.end(), [&](const Who& w) { return w.id == it->id; })) it = state.erase(it);
            else ++it;
        bool night = isNight(hour);
        for (const Who& w : players) {
            PlayerState& ps = stateOf(w.id);
            ps.cooldown -= dt;
            ps.check -= dt;
            if (ps.check > 0.0f) continue;
            ps.check = s.checkEvery * (0.75f + 0.5f * rand01());
            if (ps.cooldown > 0.0f) { ps.why = "cooling down (" + std::to_string((int)ps.cooldown) + " s)"; continue; }
            int near = nearCount(npcs, w.pos);
            if (near >= (night ? s.maxNearNight : s.maxNear)) { ps.why = std::to_string(near) + " roaming near already"; continue; }
            if (rand01() > s.chance) { ps.why = "rolled nothing this time"; continue; }
            std::string made = trySpawn(phys, npcs, w, players, hour, ps.why);
            if (!made.empty()) {
                ps.why = "made " + made;
                ps.cooldown = night ? s.nightCooldownMin + rand01() * (s.nightCooldownMax - s.nightCooldownMin)
                                    : s.cooldownMin + rand01() * (s.cooldownMax - s.cooldownMin);
            }
        }
    }

    // Try a group for this player now, cooldown and caps aside
    // (`roaming now`). What came, or why nothing did
    std::string forceFor(const IPhysics& phys, NpcManager& npcs, const Who& w, const std::vector<Who>& players, float hour) {
        if (!gRoaming.loaded) gRoaming.load();
        std::string why;
        std::string made = trySpawn(phys, npcs, w, players, hour, why);
        PlayerState& ps = stateOf(w.id);
        if (!made.empty()) ps.why = "made " + made;
        return made.empty() ? "nothing: " + why : made;
    }

    // Every roaming NPC goes (`roaming clear`)
    int clear(NpcManager& npcs) {
        int n = npcs.removeIf([](const Npc& x) { return x.roamGroup != 0; });
        npcsGone += n;
        return n;
    }

    bool isNight(float hour) const {
        const RoamSettings& s = gRoaming.settings;
        return s.nightFrom > s.nightTo ? (hour >= s.nightFrom || hour < s.nightTo) : (hour >= s.nightFrom && hour < s.nightTo);
    }
    int nearCount(NpcManager& npcs, glm::vec3 p) const {
        int c = 0;
        for (auto& n : npcs.all())
            if (n->roamGroup && !n->dead() && flat(n->position - p) <= gRoaming.settings.countRadius) c++;
        return c;
    }
    int alive(NpcManager& npcs) const {
        int c = 0;
        for (auto& n : npcs.all()) c += n->roamGroup && !n->dead();
        return c;
    }
    const PlayerState* stateFor(uint64_t id) const {
        for (const PlayerState& p : state) if (p.id == id) return &p;
        return nullptr;
    }
    void resetTimers() { state.clear(); }

private:
    std::vector<PlayerState> state;
    uint32_t rng = 0x6d2b79f5u;
    uint32_t nextGroup = 1;

    static float flat(glm::vec3 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / (float)0x1000000; }

    void note(glm::vec3 at, const std::string& why) {
        if (tries.size() >= 96) tries.erase(tries.begin());
        tries.push_back({ at, why, 0.0f });
    }

    PlayerState& stateOf(uint64_t id) {
        for (PlayerState& p : state) if (p.id == id) return p;
        PlayerState p; p.id = id;
        p.check = gRoaming.settings.checkEvery * rand01();
        p.cooldown = 0.0f;
        state.push_back(p);
        return state.back();
    }

    // Far from everyone for a while: gone (even mid-fight: one with
    // another faction far off, or a chase the player outran — and
    // out there it's frozen, so it would never stop on its own)
    void despawn(float dt, NpcManager& npcs, const std::vector<Who>& players) {
        const RoamSettings& s = gRoaming.settings;
        for (auto& n : npcs.all()) {
            if (!n->roamGroup || n->dead()) continue;
            float nearest = 1e9f;
            for (const Who& w : players) nearest = std::min(nearest, flat(n->position - w.pos));
            if (nearest > s.despawnDistance) n->roamAway += dt;
            else n->roamAway = 0.0f;
        }
        int gone = npcs.removeIf([&](const Npc& x) { return x.roamGroup && !x.dead() && x.roamAway > s.despawnAfter; });
        npcsGone += gone;
    }

    // Pick a spot round the player and a group that fits it; spawn
    // it. The group's description, or "" with why not
    std::string trySpawn(const IPhysics& phys, NpcManager& npcs, const Who& w, const std::vector<Who>& players,
                         float hour, std::string& why) {
        const RoamSettings& s = gRoaming.settings;
        if (gRoaming.groups.empty()) { why = "no groups in roaming.toml"; return ""; }
        bool night = isNight(hour);
        nav::Pathfinder pf(phys);
        std::string lastWhy = "no spot found";
        glm::vec2 head(w.heading.x, w.heading.z);
        bool hasHead = glm::length(head) > 0.1f;
        if (hasHead) head = glm::normalize(head);
        for (int tries = 0; tries < 24; tries++) {
            float ang;
            if (hasHead && rand01() < s.ahead) ang = std::atan2(head.y, head.x) + (rand01() - 0.5f) * 2.1f;   // ±60°
            else ang = rand01() * 6.2831853f;
            float dist = s.minDistance + rand01() * (s.maxDistance - s.minDistance);
            int x = (int)std::floor(w.pos.x + std::cos(ang) * dist), z = (int)std::floor(w.pos.z + std::sin(ang) * dist);
            glm::vec3 spot(x + 0.5f, w.pos.y, z + 0.5f);
            auto turnDown = [&](const std::string& why) { lastWhy = why; note(spot, why); };
            std::string bad = place(phys, pf, x, z, w.pos.y, players, spot);
            if (!bad.empty()) { turnDown(bad); continue; }
            int biome = biomeAt ? biomeAt(x, z) : -1;
            if (biome >= 0 && biome < island::BIOME_COUNT && RoamingTable::barren(biome)) {
                turnDown("the spot is " + std::string(island::biomeInfo(biome).name));
                continue;
            }
            float fromSpawn = hasWorldSpawn ? flat(spot - worldSpawn) : 1e9f;
            if (hasWorldSpawn && fromSpawn < s.spawnSafe) { turnDown("too near the world spawn"); continue; }
            // the groups that fit here and now
            std::vector<const RoamGroupDef*> fit;
            float sum = 0.0f;
            for (const RoamGroupDef& g : gRoaming.groups) {
                if (g.time == RoamGroupDef::Time::Day && night) continue;
                if (g.time == RoamGroupDef::Time::Night && !night) continue;
                if (g.fromSpawnMin > 0.0f && fromSpawn < g.fromSpawnMin) continue;
                if (g.fromSpawnMax > 0.0f && fromSpawn > g.fromSpawnMax) continue;
                if (!RoamingTable::biomeMatches(g, biome)) continue;
                fit.push_back(&g);
                sum += std::max(0.0f, g.weight);
            }
            if (fit.empty() || sum <= 0.0f) {
                turnDown("no group for " + (biome >= 0 && biome < island::BIOME_COUNT ? RoamingTable::key(biome) : std::string("this ground")) +
                         (night ? " at night" : " by day"));
                continue;
            }
            float r = rand01() * sum;
            const RoamGroupDef* g = fit.back();
            for (const RoamGroupDef* f : fit) { if (r < f->weight) { g = f; break; } r -= std::max(0.0f, f->weight); }
            int made = spawnGroup(*g, phys, pf, npcs, spot, players, w.pos);
            if (made == 0) { turnDown(g->id + ": no room for its members"); continue; }
            groupsMade++;
            npcsMade += made;
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s x%d at %.0f %.0f %.0f (%s, %.0f away)", g->id.c_str(), made, spot.x, spot.y, spot.z,
                          biome >= 0 && biome < island::BIOME_COUNT ? RoamingTable::key(biome).c_str() : "-", flat(spot - w.pos));
            lastGroup = buf;
            note(spot, "+" + g->id + " x" + std::to_string(made));      // (a '+': it came)
            return buf;
        }
        why = lastWhy;
        return "";
    }

    // Ground to stand on at column (x, z), looking down from above
    // the player: open sky over it, dry, nothing built near, out of
    // every player's reach. "" and the spot, or why not
    std::string place(const IPhysics& phys, const nav::Pathfinder& pf, int x, int z, float aroundY,
                      const std::vector<Who>& players, glm::vec3& spot) const {
        const RoamSettings& s = gRoaming.settings;
        if (!phys.columnLoaded(x, z)) return "the ground there isn't loaded";
        int top = (int)aroundY + 80, bottom = (int)aroundY - 120, ground = INT32_MIN;
        for (int y = top; y > bottom; y--)
            if (phys.solidAt(x, y - 1, z) && !phys.solidAt(x, y, z)) { ground = y; break; }
        if (ground == INT32_MIN) return "no ground";
        // open sky: nothing solid over it (a roof, a cave)
        for (int y = ground + 2; y < ground + (int)s.openSky; y++)
            if (phys.solidAt(x, y, z)) return "under a roof";
        int standY;
        if (!pf.standAt(x, z, ground, standY)) return "no room to stand";
        spot = glm::vec3(x + 0.5f, (float)standY, z + 0.5f);
        for (const Who& o : players)
            if (flat(o.pos - spot) < s.minDistance) return "too near a player";
        if (spotBlocked) { std::string b = spotBlocked(spot); if (!b.empty()) return b; }
        return "";
    }

    int spawnGroup(const RoamGroupDef& g, const IPhysics& phys, const nav::Pathfinder& pf, NpcManager& npcs, glm::vec3 at,
                   const std::vector<Who>& players, glm::vec3 forPlayer) {
        (void)phys;
        int size = g.minSize + (int)(rand01() * (float)(g.maxSize - g.minSize + 1));
        size = std::min(size, g.maxSize);
        float sum = 0.0f;
        for (auto& [id, wgt] : g.npcs) sum += std::max(0.0f, wgt);
        if (sum <= 0.0f) return 0;
        uint32_t group = nextGroup++;
        if (nextGroup == 0) nextGroup = 1;
        int made = 0;
        for (int i = 0; i < size; i++) {
            float r = rand01() * sum;
            std::string type = g.npcs.back().first;
            for (auto& [id, wgt] : g.npcs) { if (r < wgt) { type = id; break; } r -= std::max(0.0f, wgt); }
            // a place near the group's middle (the first stands on it)
            glm::vec3 spot = at;
            bool ok = i == 0;
            for (int t = 0; t < 10 && !ok; t++) {
                float a = rand01() * 6.2831853f, d = 2.0f + rand01() * g.spread;
                int x = (int)std::floor(at.x + std::cos(a) * d), z = (int)std::floor(at.z + std::sin(a) * d), y;
                if (!pf.standAt(x, z, (int)std::floor(at.y), y)) continue;
                spot = glm::vec3(x + 0.5f, (float)y, z + 0.5f);
                ok = std::none_of(players.begin(), players.end(), [&](const Who& o) { return flat(o.pos - spot) < gRoaming.settings.minDistance * 0.8f; });
            }
            if (!ok) continue;
            glm::vec3 face = forPlayer - spot;
            face.y = 0.0f;
            Npc* n = npcs.spawn(type, spot, glm::length(face) > 0.1f ? glm::normalize(face) : glm::vec3(0, 0, 1));
            if (!n) continue;
            n->position = n->prevPosition = spot;
            n->roamGroup = group;
            made++;
        }
        return made;
    }
};
