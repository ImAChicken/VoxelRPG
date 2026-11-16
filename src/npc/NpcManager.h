#pragma once
#include "Npc.h"
#include "Brain.h"
#include "States.h"
#include "Factions.h"
#include "NpcTypes.h"
#include "../physics/IPhysics.h"
#include "../game/ItemRegistry.h"
#include "../game/Combat.h"
#include <glm/glm.hpp>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// NpcManager — every NPC in the world
// ══════════════════════════════════════════════════════════
// Spawns NPCs from their type (npcs.toml), and runs each one's
// tick for the Simulation: notice damage and deaths, senses
// (staggered, a few times a second), think, move, ragdoll.
class NpcManager {
public:
    static constexpr float REMOVE_DEAD_AFTER = 20.0f;
    static constexpr float ALLY_DEATH_RANGE  = 60.0f;

    // A new NPC of a type at a spot; nullptr if the type is unknown
    Npc* spawn(const std::string& typeId, glm::vec3 at, glm::vec3 facing, uint32_t seed = 0) {
        registerBuiltinStates();
        const NpcType* t = gNpcTypes.find(typeId);
        if (!t) return nullptr;
        auto n = std::make_unique<Npc>();
        n->type = t;
        n->typeId = typeId;
        n->position = n->prevPosition = n->home = at;
        glm::vec2 f(facing.x, facing.z);
        f = glm::length(f) > 1e-4f ? glm::normalize(f) : glm::vec2(0, 1);
        n->facing = n->homeFacing = glm::vec3(f.x, 0, f.y);
        n->lookSeed = seed ? seed : 0x9e3779b9u ^ (uint32_t)(n->uuid * 2654435761u);   // the seed keeps its look across saves
        n->rng = n->lookSeed ^ 0x5bd1e995u;
        if (const LookPreset* p = gNpcTypes.look(t->look)) { n->appearance = p->make(n->lookSeed); n->textureSet = p->textureSet; }
        else n->appearance = Appearance::random(n->lookSeed);
        n->name = t->name;
        n->faction = gFactions.find(t->faction);
        n->setBodyMass(BodyMass::bodyKgFor(n->appearance.build, n->appearance.heightVoxels, n->appearance.shoulders,
                                           n->appearance.chest, n->appearance.hips, n->appearance.limbs));
        n->weaponItem = t->weapon;
        if (uint16_t wid = t->weapon.empty() ? 0 : gItems.find(t->weapon)) {
            const ItemDef& w = gItems.get(wid);
            n->weaponDamage = w.damage;
            n->moveset = gMovesets.find(w.moveset);
            n->weaponWeight = w.weight;
            n->body.addGear(SEG_HAND_R, w.weight);
            n->mass = n->body.total();
        }
        n->maxHealth = n->health = t->health;
        n->maxPoise = n->poise = t->poise;
        n->healthSeen = n->health;
        n->senseTimer = (float)(list.size() % 8) / (8.0f * brain::SENSE_HZ);   // staggered
        // patrol waypoints around home
        if (t->patrolPoints > 0) {
            float r = t->territoryRadius > 0.0f ? t->territoryRadius * 0.6f : 30.0f;
            float a0 = n->randRange(0.0f, 6.2832f);
            for (int i = 0; i < t->patrolPoints; i++) {
                float a = a0 + 6.2832f * i / t->patrolPoints;
                float rr = r * n->randRange(0.6f, 1.0f);
                n->patrol.push_back(at + glm::vec3(std::cos(a) * rr, 0, std::sin(a) * rr));
            }
        }
        list.push_back(std::move(n));
        return list.back().get();
    }

    // How much of an NPC is simulated, by distance to the nearest
    // player with a body:
    //   Full    everything, every tick
    //   Lite    body every tick, senses at LITE_SENSE_HZ (far
    //           enough that nobody notices the difference)
    //   Frozen  nothing: held where it is (too far, or the ground
    //           under it isn't loaded — it would fall through)
    enum class Tier : uint8_t { Full, Lite, Frozen };
    static constexpr float FULL_RANGE = 192.0f, LITE_RANGE = 400.0f, LITE_SENSE_HZ = 2.0f;

    // One physics tick for every NPC. players: where the players
    // with bodies are (for the tiers)
    void tick(float dt, IPhysics& physics, NpcWorld& world, const std::vector<Noise>& noises,
              const std::vector<glm::vec3>& players) {
        for (auto& np : list) {
            Npc& n = *np;
            float near = 1e9f;
            for (const glm::vec3& p : players) near = std::min(near, glm::length(glm::vec2(p.x - n.position.x, p.z - n.position.z)));
            bool loaded = physics.columnLoaded((int)std::floor(n.position.x), (int)std::floor(n.position.z));
            Tier tier = (!loaded || near > LITE_RANGE) ? Tier::Frozen : near > FULL_RANGE ? Tier::Lite : Tier::Full;
            n.tier = (uint8_t)tier;
            if (tier == Tier::Frozen) { n.prevPosition = n.position; continue; }
            n.prevPosition = n.position;
            n.sinceHurt += dt;
            if (n.dead()) { n.deadFor += dt; }
            // damage since last tick
            if (n.healthSeen >= 0.0f && n.health < n.healthSeen) {
                NpcEvent e; e.kind = NpcEvent::Kind::Damaged; e.who = n.lastHitBy;
                if (Human* h = world.find(n.lastHitBy)) e.at = h->position;
                n.post(e);
                if (n.dead()) died(n, world);
            }
            n.healthSeen = n.health;
            if (n.type && n.type->healsAfter > 0.0f && !n.dead() && n.sinceHurt > n.type->healsAfter)
                n.health = n.healthSeen = n.maxHealth;
            // senses
            n.senseTimer -= dt;
            if (n.senseTimer <= 0.0f && !n.dead()) {
                n.senseTimer += 1.0f / (tier == Tier::Full ? brain::SENSE_HZ : LITE_SENSE_HZ);
                brain::look(n, world);
            }
            if (!n.dead()) brain::hear(n, noises);
            // think, then move like any body
            std::string before = n.stateName;
            brain::think(n, world, dt);
            if (before == "unavailable" && n.stateName != "unavailable") closedMenus.push_back(n.uuid);
            physics.moveHuman(n, n.intent, dt);
            physics.stepRagdoll(n, dt);
            n.intent.clearOneShots();
        }
        for (size_t i = 0; i < list.size();)
            if (list[i]->deadFor > REMOVE_DEAD_AFTER || list[i]->fellOutOfWorld) list.erase(list.begin() + i);
            else i++;
    }

    // A player starts / ends talking to an NPC. Returns false if
    // it can't talk now (no 'unavailable' state, fighting, dead).
    bool openMenu(Npc& n, EntityId player) {
        if (n.dead() || !n.hasState("unavailable")) return false;
        if (n.stateName == "combat" || n.stateName == "alert" || n.stateName == "flee") return false;
        NpcEvent e; e.kind = NpcEvent::Kind::MenuOpened; e.who = player;
        n.post(e);
        return true;
    }
    void closeMenu(Npc& n) { NpcEvent e; e.kind = NpcEvent::Kind::MenuClosed; n.post(e); }
    std::vector<EntityId> takeClosedMenus() { std::vector<EntityId> v; v.swap(closedMenus); return v; }

    // ── Saving (the world's npcs.dat) ─────────────────────
    // One line each: type, position, home, facing, health, patrol
    // points. They come back in their type's start state; what
    // they were doing and knew is not kept.
    void save(const std::string& path) const {
        std::ofstream f(path);
        f << "npcs 1\n";
        for (const auto& n : list) {
            if (n->dead()) continue;
            f << n->typeId << " " << n->position.x << " " << n->position.y << " " << n->position.z << " "
              << n->home.x << " " << n->home.y << " " << n->home.z << " " << n->homeFacing.x << " " << n->homeFacing.z << " "
              << n->health << " " << n->lookSeed << " " << n->patrol.size();
            for (const auto& p : n->patrol) f << " " << p.x << " " << p.y << " " << p.z;
            f << "\n";
        }
    }
    int load(const std::string& path) {
        std::ifstream f(path);
        std::string line;
        if (!std::getline(f, line) || line.rfind("npcs 1", 0) != 0) return 0;
        int count = 0;
        while (std::getline(f, line)) {
            std::istringstream s(line);
            std::string type; glm::vec3 pos, home; glm::vec2 face; float health; uint32_t rng; size_t np;
            if (!(s >> type >> pos.x >> pos.y >> pos.z >> home.x >> home.y >> home.z >> face.x >> face.y >> health >> rng >> np)) continue;
            Npc* n = spawn(type, home, glm::vec3(face.x, 0, face.y), rng);
            if (!n) continue;
            n->position = n->prevPosition = pos;
            n->health = n->healthSeen = std::min(health, n->maxHealth);
            n->patrol.clear();
            for (size_t i = 0; i < np; i++) { glm::vec3 p; if (s >> p.x >> p.y >> p.z) n->patrol.push_back(p); }
            count++;
        }
        return count;
    }

    void clear() { list.clear(); }
    std::vector<std::unique_ptr<Npc>>& all() { return list; }
    Npc* find(EntityId id) { for (auto& n : list) if (n->uuid == id) return n.get(); return nullptr; }

private:
    std::vector<std::unique_ptr<Npc>> list;
    std::vector<EntityId> closedMenus;

    void died(Npc& dead, NpcWorld& world) {
        for (auto& o : list) {
            if (o.get() == &dead || o->dead()) continue;
            if (glm::length(o->position - dead.position) > ALLY_DEATH_RANGE) continue;
            if (!world.friendly(*o, dead)) continue;
            NpcEvent e; e.kind = NpcEvent::Kind::AllyDied; e.who = dead.uuid; e.at = dead.position;
            o->post(e);
        }
    }
};
