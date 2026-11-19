// Roaming spawns (sim/Roaming.h, data/registry/roaming.toml): off,
// nothing comes; on, groups turn up around the player out of sight,
// the right ones for the biome and the time of day, never more near
// the player than the cap, never in a safe place; far from everyone
// they go again; they save as roaming; the world setting saves.
//
// Run from the repo root. VERBOSE=1 prints each group.
#include "TestWorld.h"
#include "world/WorldSaveManager.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

using namespace testworld;

static float flat(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

int main() {
    if (!loadData() || !gRoaming.load()) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }
    const bool verbose = std::getenv("VERBOSE") != nullptr;
    std::printf("the table\n");
    check(gRoaming.groups.size() >= 4, std::to_string(gRoaming.groups.size()) + " groups in roaming.toml");
    bool typesKnown = true;
    for (const RoamGroupDef& g : gRoaming.groups)
        for (auto& [t, wgt] : g.npcs) if (!gNpcTypes.find(t)) { typesKnown = false; std::printf("    %s: no npc type %s\n", g.id.c_str(), t.c_str()); }
    check(typesKnown, "every group's NPCs are npcs.toml types");
    check(gRoaming.settings.minDistance > 40.0f && gRoaming.settings.maxDistance > gRoaming.settings.minDistance, "spawn distances read");

    // quicker for the test: a try every second, always a group, short cooldowns
    RoamSettings& s = gRoaming.settings;
    s.checkEvery = 1.0f; s.chance = 1.0f;
    s.cooldownMin = s.cooldownMax = 4.0f; s.nightCooldownMin = s.nightCooldownMax = 4.0f;

    World w;
    gClock.frozen = true;
    gClock.time = 12.0f / 24.0f;
    const glm::vec3 centre(256.5f, (float)GROUND, 256.5f);
    Player& pl = w.player(centre, GameMode::Dev);      // (Test Play: hits land, nobody dies)
    Roaming& r = w.sim.roaming;
    r.biomeAt = [](int, int) { return (int)island::Plains; };
    auto roamers = [&] { int n = 0; for (auto& x : w.sim.npcs().all()) n += x->roamGroup && !x->dead(); return n; };

    std::printf("off\n");
    r.enabled = false;
    w.run(30.0f);
    check(roamers() == 0, "nothing comes while it's off");

    std::printf("on, plains, midday\n");
    r.enabled = true;
    // watch every NPC as it appears: where, what
    std::set<uint64_t> seen;
    float nearest = 1e9f;
    int maxNear = 0;
    std::set<std::string> dayTypes;
    auto watch = [&] {
        for (auto& x : w.sim.npcs().all()) {
            if (!x->roamGroup || seen.count(x->uuid)) continue;
            seen.insert(x->uuid);
            nearest = std::min(nearest, flat(x->position, pl.position));
            dayTypes.insert(x->typeId);
        }
        maxNear = std::max(maxNear, r.nearCount(w.sim.npcs(), pl.position));
    };
    for (int i = 0; i < 90 * 60; i++) { w.sim.step(DT); if (i % 6 == 0) watch(); }
    pl.health = pl.maxHealth;
    if (verbose) std::printf("    %d groups, last: %s\n", r.groupsMade, r.lastGroup.c_str());
    check(r.groupsMade > 0 && !seen.empty(), "groups come: " + std::to_string(r.groupsMade) + " (" + std::to_string(seen.size()) + " NPCs)");
    check(nearest >= s.minDistance * 0.8f, "out of sight when they appear: nearest " + std::to_string((int)nearest) + " voxels away");
    int largest = 0;
    for (const RoamGroupDef& g : gRoaming.groups) largest = std::max(largest, g.maxSize);
    check(maxNear <= s.maxNear + largest, "never too many near: at most " + std::to_string(maxNear));
    check(!dayTypes.count("undead"), "no restless dead by day");
    {
        std::string t;
        for (auto& x : dayTypes) t += x + " ";
        check(dayTypes.count("bandit") > 0, "plains by day: " + t);
    }

    std::printf("night\n");
    r.clear(w.sim.npcs());
    gClock.time = 23.0f / 24.0f;
    std::set<std::string> nightTypes;
    for (int round = 0; round < 12 && !nightTypes.count("undead"); round++) {    // (cleared each round: the cap)
        r.clear(w.sim.npcs());
        w.run(6.0f);
        for (auto& x : w.sim.npcs().all()) if (x->roamGroup) nightTypes.insert(x->typeId);
    }
    check(nightTypes.count("undead") > 0, "the dead walk at night");
    auto why = [&] { const Roaming::PlayerState* ps = r.stateFor(pl.uuid); std::printf("    (last try: %s; %d roaming)\n", ps ? ps->why.c_str() : "-", roamers()); };
    if (verbose) why();

    std::printf("biomes\n");
    r.clear(w.sim.npcs());
    gClock.time = 12.0f / 24.0f;
    r.biomeAt = [](int, int) { return (int)island::Swamp; };
    std::set<std::string> swampTypes;
    for (int round = 0; round < 8; round++) {
        r.clear(w.sim.npcs());
        w.run(6.0f);
        for (auto& x : w.sim.npcs().all()) if (x->roamGroup) swampTypes.insert(x->typeId);
    }
    {
        std::string t;
        for (auto& x : swampTypes) t += x + " ";
        check(!swampTypes.empty() && !swampTypes.count("bandit"), "a swamp by day: slimes, no bandits (" + t + ")");
    }
    if (verbose) why();
    r.clear(w.sim.npcs());
    r.biomeAt = [](int, int) { return (int)island::Ocean; };
    int before = r.groupsMade;
    w.run(20.0f);
    check(r.groupsMade == before, "nothing comes out of the sea");

    std::printf("safe places\n");
    r.biomeAt = [](int, int) { return (int)island::Plains; };
    const glm::vec3 village(256.5f, (float)GROUND, 256.5f);
    r.spotBlocked = [&](glm::vec3 p) { return flat(p, village) < 100.0f ? std::string("near something built") : std::string(); };
    seen.clear();
    float inside = 1e9f;
    for (int i = 0; i < 40 * 60; i++) {
        w.sim.step(DT);
        for (auto& x : w.sim.npcs().all())
            if (x->roamGroup && !seen.count(x->uuid)) { seen.insert(x->uuid); inside = std::min(inside, flat(x->position, village)); }
    }
    check(!seen.empty() && inside >= 100.0f - gRoaming.groups[0].spread - 3.0f,
          "none appear in a safe place (nearest " + std::to_string((int)inside) + " from its middle)");
    r.spotBlocked = nullptr;
    pl.health = pl.maxHealth;

    std::printf("going again\n");
    r.enabled = false;                                   // (no new ones while we watch the old go)
    s.despawnDistance = 150.0f; s.despawnAfter = 5.0f;
    int had = roamers();
    pl.position = glm::vec3(100.5f, (float)GROUND, 100.5f);
    pl.flying = true;
    // a roaming NPC near the player stays
    Npc* stay = w.sim.npcs().spawn("bandit", glm::vec3(130.5f, (float)GROUND, 130.5f), glm::vec3(0, 0, 1));
    stay->roamGroup = 999;
    uint64_t stayId = stay->uuid;
    if (verbose)
        for (int k = 0; k < 12; k++) {
            w.run(5.0f);
            Npc* q = w.sim.npcs().find(stayId);
            std::printf("    stay %2d s: %s  player (%.0f %.0f %.0f) hp %.0f\n", k * 5 + 5, q ? (q->stateName + " (" + std::to_string((int)q->position.x) + " " + std::to_string((int)q->position.z) + ") hp " + std::to_string((int)q->health) + " away " + std::to_string(q->roamAway)).c_str() : "gone",
                        pl.position.x, pl.position.y, pl.position.z, pl.health);
        }
    bool gone = w.until(60.0f, [&] { return roamers() == 1; });
    check(had > 0 && gone, "far from everyone, they go: " + std::to_string(had) + " → " + std::to_string(roamers()));
    check(w.sim.npcs().find(stayId) != nullptr, "one near a player stays");
    if (verbose)
        for (auto& x : w.sim.npcs().all())
            std::printf("    %s %llu at (%.0f %.0f) %s away %.1f roam %u\n", x->typeId.c_str(), (unsigned long long)x->uuid, x->position.x, x->position.z,
                        x->stateName.c_str(), x->roamAway, x->roamGroup);

    std::printf("saving\n");
    w.sim.npcs().save("/tmp/roaming_test_npcs.dat");
    NpcManager again;
    again.load("/tmp/roaming_test_npcs.dat");
    bool kept = false;
    for (auto& x : again.all()) kept |= x->roamGroup == 999;
    check(kept, "a roaming NPC loads as one");

    std::printf("the world setting\n");
    {
        WorldSaveManager ws;
        const std::string name = "_roaming_test_tmp";
        ws.createWorld(name, 1, GameMode::Survival, WorldType::Classic, 32);
        bool def = ws.getRoamingSpawns();
        ws.setRoamingSpawns(false);
        WorldSaveManager ws2;
        ws2.loadWorld(name);
        bool off = !ws2.getRoamingSpawns();
        ws2.setRoamingSpawns(true);
        WorldSaveManager ws3;
        ws3.loadWorld(name);
        bool on = ws3.getRoamingSpawns();
        std::string folder = ws.getWorldPath();
        ws.createWorld(name, 1, GameMode::Survival, WorldType::Flat, 32);
        bool flatOff = !ws.getRoamingSpawns();
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        check(def, "on by default in a new world");
        check(off && on, "turned off and on, and it stays so after a reload");
        check(flatOff, "off by default in a flat world");
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
