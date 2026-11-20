#pragma once
#include "Npc.h"
#include "Brain.h"
#include "States.h"
#include "Routine.h"
#include "Factions.h"
#include "NpcTypes.h"
#include "../physics/IPhysics.h"
#include "../game/ItemRegistry.h"
#include "../game/Combat.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <functional>
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
    static constexpr float KEEP_LOOT_BODY    = 300.0f;   // a body that may still hold items stays this long
    static constexpr float ALLY_DEATH_RANGE  = 60.0f;

    // A new NPC of a type at a spot; nullptr if the type is unknown
    Npc* spawn(const std::string& typeId, glm::vec3 at, glm::vec3 facing, uint32_t seed = 0) {
        registerBuiltinStates();
        if (!npcStates().has("routine")) npcStates().add("routine", [] { return std::make_unique<npcstates::Routine>(); });
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
        n->pack = Container(t->pack, t->loot, n->lookSeed ^ 0x27d4eb2fu);   // unread until searched
        if (const LookPreset* p = gNpcTypes.look(t->look)) { n->appearance = p->make(n->lookSeed); n->textureSet = p->textureSet; }
        else n->appearance = Appearance::random(n->lookSeed);
        n->name = t->name;
        n->faction = gFactions.find(t->faction);
        // a giant (look_scale): bigger every way, and heavier by the cube
        const float sc = n->appearance.scale;
        n->setBodyMass(BodyMass::bodyKgFor(n->appearance.build, n->appearance.heightVoxels, n->appearance.shoulders,
                                           n->appearance.chest, n->appearance.hips, n->appearance.limbs) * sc * sc * sc);
        if (sc != 1.0f) n->setScale(sc);
        n->mainWeapon = t->weapon;
        if (wield(*n, t->weapon)) {
            n->body.addGear(SEG_HAND_R, n->weaponWeight);
            n->mass = n->body.total();
        }
        n->maxHealth = n->health = t->health;
        n->offhandItem = t->offhand;
        if (uint16_t sid = t->offhand.empty() ? 0 : gItems.find(t->offhand)) {
            const ItemDef& s = gItems.get(sid);
            if (s.shield) n->setGuard(1.0f - s.shieldBlock, s.shieldCost, s.shieldArc, s.shieldParry, true);
            n->body.addGear(SEG_HAND_L, s.weight);
            n->mass = n->body.total();
        }
        applyBody(*n, *t);
        n->climb = t->climb;
        n->maxPoise = n->poise = t->poise;
        n->armor = t->armor;
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

    // Its body from its type: a person, or a slime (short, no
    // ragdoll, hops). Also for a client's copies of a server's
    // NPCs (puppets), so they're drawn and collide the same.
    static void applyBody(Npc& n, const NpcType& t) {
        if (t.bodyHeight > 0.0f) n.standHeight = n.height = t.bodyHeight;
        if (t.bodyWidth > 0.0f) n.hitRadius = t.bodyWidth * 0.5f;
        n.hitHeight = t.hitHeight;
        n.noRagdoll = !t.person();
        if (t.bodyKg > 0.0f) n.setBodyMass(t.bodyKg);
        n.hop.on = t.hops;
        n.hop.every = t.hopEvery; n.hop.speed = t.hopSpeed; n.hop.height = t.hopHeight; n.hop.climb = t.hopClimb;
        n.hop.lungeSpeed = t.lungeSpeed; n.hop.lungeHeight = t.lungeHeight;
        if (!t.person()) n.eyeHeight = std::max(1.0f, n.height - 1.0f);
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
            if (n.puppet) continue;                // driven from outside (a client's copy of a server's body)
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
                if (n.life.on) callForHelp(n, world);       // a villager: the guards come
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
            if (n.lying) { n.velocity = glm::vec3(0.0f); n.grounded = true; }   // in bed: held where it lies
            else if (restingStill(n) && ((++n.restTicks + (uint32_t)n.uuid) % 30u) != 0u) {
                // standing still on firm ground with nothing to do: the
                // body's physics is left out (it's the most of an idle
                // NPC's cost), but for a look every half second
            }
            else { n.restTicks = 0; physics.moveHuman(n, n.intent, dt); }
            physics.stepRagdoll(n, dt);
            n.intent.clearOneShots();
        }
        for (size_t i = 0; i < list.size();)
            if (list[i]->deadFor > (list[i]->pack.mayHoldItems() ? KEEP_LOOT_BODY : REMOVE_DEAD_AFTER) ||
                list[i]->fellOutOfWorld) list.erase(list.begin() + i);   // what it held (if unsearched: never rolled) goes with it
            else i++;
    }

    // Nothing to move it: on the ground, not trying to go anywhere,
    // not moving, not hit, not swinging, not ragdolled
    static bool restingStill(const Npc& n) {
        // (an attack asked for from a standstill must reach physics: the
        // archer's draw, a boss's swing, anything that attacks standing)
        return n.grounded && !n.dead() && n.intent.move == glm::vec2(0.0f) && !n.intent.jump && !n.intent.dodge &&
               !n.intent.attackLight && !n.intent.attackHeavy && !n.intent.special && !n.intent.useItem && !n.intent.guard &&
               glm::dot(n.velocity, n.velocity) < 1e-4f && n.hits.empty() && !n.attack.active() && !n.ragdoll.simulating() &&
               !n.flying && n.freeze == Human::Freeze::None;
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
    // they were doing and knew is not kept. A pack that's been
    // read follows its line as "p.…" lines (Container::write);
    // an unread one is only its type's loot table, so nothing.
    // The dead aren't saved (nor what's on them).
    void save(const std::string& path) const {
        std::ofstream f(path);
        f << "npcs 1\n";
        for (const auto& n : list) {
            if (n->dead()) continue;
            f << n->typeId << " " << n->position.x << " " << n->position.y << " " << n->position.z << " "
              << n->home.x << " " << n->home.y << " " << n->home.z << " " << n->homeFacing.x << " " << n->homeFacing.z << " "
              << n->health << " " << n->lookSeed << " " << n->patrol.size();
            for (const auto& p : n->patrol) f << " " << p.x << " " << p.y << " " << p.z;
            if (n->spawner) f << " sp " << n->spawner;     // which spawner it belongs to
            if (n->roamGroup) f << " roam " << n->roamGroup;   // a roaming spawn (sim/Roaming.h)
            f << " id " << n->uuid;                        // its id, the same after a reload
            if (!n->name.empty() && n->type && n->name != n->type->name) {
                std::string nm = n->name;
                for (char& c : nm) if (c == ' ') c = '_';
                f << " name " << nm;
            }
            n->life.write(f);                              // a villager's places (" life …", last on the line)
            f << "\n";
            if (n->pack.opened() || n->pack.loot != (n->type ? n->type->loot : "")) n->pack.write(f, "p");   // its pack, once read
        }
    }
    int load(const std::string& path) {
        std::ifstream f(path);
        std::string line;
        if (!std::getline(f, line) || line.rfind("npcs 1", 0) != 0) return 0;
        {   // saved ids first, so no NPC made while loading takes one
            std::ifstream scan(path);
            std::string l, w;
            while (std::getline(scan, l)) {
                std::istringstream ls(l);
                while (ls >> w) if (w == "id") { uint64_t id = 0; if (ls >> id) Entity::reserveId(id); }
            }
        }
        int count = 0;
        Npc* last = nullptr;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.rfind("p.", 0) == 0) {                 // the pack of the NPC above
                size_t eq = line.find('=');
                if (last && eq != std::string::npos) last->pack.readLine(line.substr(0, eq), line.substr(eq + 1), "p");
                continue;
            }
            last = nullptr;
            std::istringstream s(line);
            std::string type; glm::vec3 pos, home; glm::vec2 face; float health; uint32_t rng; size_t np;
            if (!(s >> type >> pos.x >> pos.y >> pos.z >> home.x >> home.y >> home.z >> face.x >> face.y >> health >> rng >> np)) continue;
            Npc* n = spawn(type, home, glm::vec3(face.x, 0, face.y), rng);
            if (!n) continue;
            n->position = n->prevPosition = pos;
            n->health = n->healthSeen = std::min(health, n->maxHealth);
            n->patrol.clear();
            for (size_t i = 0; i < np; i++) { glm::vec3 p; if (s >> p.x >> p.y >> p.z) n->patrol.push_back(p); }
            std::string tag;
            while (s >> tag) {
                if (tag == "sp") s >> n->spawner;
                else if (tag == "roam") s >> n->roamGroup;
                else if (tag == "id") {
                    uint64_t id = 0;
                    if (s >> id && id && !find(id)) { n->uuid = id; Entity::reserveId(id); }
                }
                else if (tag == "name") { s >> n->name; for (char& c : n->name) if (c == '_') c = ' '; }
                else if (tag == "life") n->life.read(s);
            }
            last = n;
            count++;
        }
        for (auto& n : list) n->pack.finishLoad();
        return count;
    }

    void clear() { list.clear(); }
    // Remove the ones a test picks; how many went
    int removeIf(const std::function<bool(const Npc&)>& pick) {
        size_t before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const std::unique_ptr<Npc>& n) { return pick(*n); }), list.end());
        return (int)(before - list.size());
    }
    std::vector<std::unique_ptr<Npc>>& all() { return list; }
    Npc* find(EntityId id) { for (auto& n : list) if (n->uuid == id) return n.get(); return nullptr; }

private:
    std::vector<std::unique_ptr<Npc>> list;
    std::vector<EntityId> closedMenus;

    // A villager is hurt: every friendly NPC near that can fight (the
    // village's guards) goes for whoever did it — a player too (a
    // grudge: hostile to them whatever the factions say)
    static constexpr float HELP_RANGE = 160.0f;
    void callForHelp(Npc& hurt, NpcWorld& world) {
        Human* attacker = world.find(hurt.lastHitBy);
        if (!attacker || attacker == &hurt) return;
        for (auto& o : list) {
            if (o.get() == &hurt || o->dead() || !o->hasState("combat") || o->uuid == attacker->uuid) continue;
            if (glm::length(o->position - hurt.position) > HELP_RANGE) continue;
            if (!world.friendly(*o, hurt)) continue;
            if (!world.hostile(*o, *attacker) &&
                std::find(o->grudges.begin(), o->grudges.end(), attacker->uuid) == o->grudges.end())
                o->grudges.push_back(attacker->uuid);
            NpcEvent e; e.kind = NpcEvent::Kind::SawEnemy; e.who = attacker->uuid; e.at = attacker->position;
            o->post(e);
        }
    }
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
