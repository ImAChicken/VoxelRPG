#pragma once
#include "../game/StructureRegistry.h"
#include "../npc/NpcManager.h"
#include "../npc/Pathfinder.h"
#include "../physics/IPhysics.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

// ══════════════════════════════════════════════════════════
// Spawners — structures that keep NPCs coming
// ══════════════════════════════════════════════════════════
// A spawner is a structure of kind "spawner" (structures.toml):
// a voxel model placed by hand (dev menu → Builder's Hammer, or
// the console's place) and saved with the world's structures.
// Its entry's spawn = { … } says what it makes:
//   npcs        npcs.toml types and their weights
//   max_alive   how many of its NPCs live at once
//   total       how many it ever makes (0 = no limit)
//   radius      they appear within this many voxels of it
//   activate    it only works while a player is this close
//   respawn     s after one is gone before the next appears
//   min_player_distance   never appear closer than this to a player
//   home        its NPCs live around it (their home and territory)
//
// When it first wakes it fills up at once; after that it replaces
// the fallen one at a time. Each NPC remembers its spawner (saved
// in npcs.dat), so a reloaded world doesn't double up.
//
// Runs in the simulation: main.cpp hands it the placed spawner
// structures (sync), the simulation ticks it.
struct SpawnerState {
    uint32_t  id = 0;            // the structure instance
    uint16_t  type = 0;          // its structure type
    glm::vec3 at{0.0f};          // its foot, centre
    float     timer = 0.0f;      // s until the next one may appear
    int       spawned = 0;       // made so far (for 'total')
    int       alive = 0;         // living ones now
    bool      filled = false;    // has woken and filled up once
    bool      active = false;    // a player is near
};

class Spawners {
public:
    struct Placed { uint32_t id; uint16_t type; glm::vec3 at; };

    // The placed spawner structures now: new ones start, gone ones stop
    // (their NPCs stay, no longer tied to anything)
    void sync(const std::vector<Placed>& placed, NpcManager& npcs) {
        for (const Placed& p : placed) {
            auto it = std::find_if(list.begin(), list.end(), [&](const SpawnerState& s) { return s.id == p.id; });
            if (it == list.end()) {
                SpawnerState s; s.id = p.id; s.type = p.type; s.at = p.at;
                list.push_back(s);
            } else { it->type = p.type; it->at = p.at; }
        }
        for (auto it = list.begin(); it != list.end();) {
            bool still = std::any_of(placed.begin(), placed.end(), [&](const Placed& p) { return p.id == it->id; });
            if (!still) {
                for (auto& n : npcs.all()) if (n->spawner == it->id) n->spawner = 0;
                it = list.erase(it);
            } else ++it;
        }
    }

    void tick(float dt, const IPhysics& phys, NpcManager& npcs, const std::vector<glm::vec3>& players) {
        for (SpawnerState& s : list) {
            const StructureType* t = gStructures.get(s.type);
            if (!t || t->kind != StructureKind::Spawner || t->spawn.npcs.empty()) { s.active = false; continue; }
            const SpawnDef& d = t->spawn;
            s.alive = 0;
            for (auto& n : npcs.all()) if (n->spawner == s.id && !n->dead()) s.alive++;
            if (s.alive > 0) s.filled = true;           // a reloaded world: it already has its NPCs
            s.active = phys.columnLoaded((int)std::floor(s.at.x), (int)std::floor(s.at.z)) &&
                       std::any_of(players.begin(), players.end(), [&](glm::vec3 p) { return flat(p - s.at) <= d.activate; });
            if (!s.active) continue;
            if (d.total > 0 && s.spawned >= d.total) continue;
            if (s.alive >= d.maxAlive) { s.timer = d.respawn; continue; }   // full: the wait starts when one is gone
            int want = 0;
            if (!s.filled) want = d.maxAlive - s.alive;  // first wake: fill up
            else if ((s.timer -= dt) <= 0.0f) want = 1;  // then one at a time
            for (int i = 0; i < want; i++) {
                if (d.total > 0 && s.spawned >= d.total) break;
                if (!spawnOne(s, d, phys, npcs, players)) break;
                s.alive++;
                s.spawned++;
            }
            if (want > 0) { s.filled = true; s.timer = d.respawn; }
        }
    }

    const std::vector<SpawnerState>& all() const { return list; }
    void clear() { list.clear(); }

private:
    std::vector<SpawnerState> list;
    uint32_t rng = 0x2545F491u;

    static float flat(glm::vec3 v) { return std::sqrt(v.x * v.x + v.z * v.z); }
    float rand01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / (float)0x1000000; }

    bool spawnOne(const SpawnerState& s, const SpawnDef& d, const IPhysics& phys, NpcManager& npcs,
                  const std::vector<glm::vec3>& players) {
        // which type
        float sum = 0.0f;
        for (auto& [id, w] : d.npcs) sum += std::max(0.0f, w);
        if (sum <= 0.0f) return false;
        float r = rand01() * sum;
        std::string type = d.npcs.back().first;
        for (auto& [id, w] : d.npcs) { if (r < w) { type = id; break; } r -= std::max(0.0f, w); }
        // where: a spot with ground to stand on, around it, away from players
        nav::Pathfinder pf(phys);
        for (int tries = 0; tries < 16; tries++) {
            float ang = rand01() * 6.2831853f;
            float dist = 3.0f + rand01() * std::max(0.0f, d.radius - 3.0f);
            int x = (int)std::floor(s.at.x + std::cos(ang) * dist), z = (int)std::floor(s.at.z + std::sin(ang) * dist), y;
            if (!phys.columnLoaded(x, z)) continue;
            if (!pf.standAt(x, z, (int)std::floor(s.at.y), y)) continue;
            glm::vec3 spot(x + 0.5f, (float)y, z + 0.5f);
            bool seen = std::any_of(players.begin(), players.end(), [&](glm::vec3 p) { return glm::length(p - spot) < d.minPlayer; });
            if (seen) continue;
            glm::vec3 facing(std::cos(ang + 3.14159f), 0.0f, std::sin(ang + 3.14159f));
            // its home: the spawner (patrols and territory around it), or where it appeared
            Npc* n = npcs.spawn(type, d.home ? s.at : spot, facing);
            if (!n) return false;
            n->position = n->prevPosition = spot;
            n->spawner = s.id;
            return true;
        }
        return false;
    }
};
