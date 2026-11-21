// Headless checks for portals and instances (docs/dungeons.md step 6,
// first version): the realm (world/Realm.h: instances in the save, their
// clocks, saving), gates (game/Gate.h), and the portal logic
// (game/Portals.h): the main world's gate placed and built by the
// spawn, its menu opening a crypt, walking into it asking for the trip,
// the instance's own gate, the boss shortening its clock, closing and
// deleting. The trip itself (the fade, reopening the world) is the
// game's (main.cpp); here the far world is opened by hand the same way.
// Makes saves/_portal_test and deletes it. Run from the repo root.
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include "game/Portals.h"
#include <cstdio>
#include <filesystem>

using namespace testworld;
namespace fs = std::filesystem;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    const char* NAME = "_portal_test";
    std::error_code ec;
    fs::remove_all(std::string("saves/") + NAME, ec);

    std::printf("dungeons a gate can open\n");
    check(realm::gDungeonKinds.load() && realm::gDungeonKinds.find("crypt") &&
          std::fabs(realm::gDungeonKinds.find("crypt")->minutes - 60.0f) < 0.01f, "portals.toml: the Undead Crypt, an hour");
    check(gRegistry.getIDByName("portal_veil") && !gRegistry.get(gRegistry.getIDByName("portal_veil")).isSolid(), "the veil: a block you walk through");

    PortalDirector P;
    std::vector<std::string> said;
    auto say = [&](const std::string& s) { said.push_back(s); };
    std::string instName, instId;

    std::printf("the main world's gate\n");
    {
        Simulation sim;
        GameSession session(sim);
        FlatOptions flat; flat.plants = false;
        session.beginNew(NAME, 5, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
        check(session.info.getSpawnPortal(), "a new world has a portal at its spawn (world.vxrp portal, New World > Portal)");
        session.buildGenerator();
        session.attach();
        sim.roaming.enabled = false;
        ChunkKeeper keeper(*session.world, session.chunkSource());
        glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(session.world->sizeX(), session.world->sizeZ()));
        keeper.loadNow({ spawn, spawn + glm::vec3(60, 0, 0), spawn + glm::vec3(-60, 0, 0), spawn + glm::vec3(0, 0, 60), spawn + glm::vec3(0, 0, -60) });
        Player& p = sim.addPlayer();
        p.setMode(GameMode::Survival);
        p.position = spawn;
        P.realm.load(NAME, false);
        auto tick = [&](int n) { for (int i = 0; i < n; i++) { sim.step(DT); P.tick(DT, session, p, session.info.getSpawnPortal(), say); session.updateEdits(50.0f); } };
        tick(10);
        Gate g = P.mainGate();
        check(P.realm.hasGate && P.realm.gateBuilt && g.isBuilt(*session.world), "built by the spawn once its ground is in (" +
              std::to_string((int)glm::length(glm::vec2(g.base.x - spawn.x, g.base.z - spawn.z))) + " voxels off)");
        check(!g.isOpen(*session.world), "  dark: nothing open yet");
        glm::vec3 toSpawn = spawn - g.arrival(); toSpawn.y = 0;
        check(glm::dot(glm::normalize(toSpawn), g.normal()) > 0.3f, "  you come out of it facing the spawn");
        // its menu
        p.position = g.arrival();
        check(P.interact(session, p) && P.menuOpen, "close to it, Interact opens its menu");
        int open = -1;
        for (size_t i = 0; i < P.options.size(); i++) if (P.options[i].action == "open:crypt") open = (int)i;
        check(open >= 0, "  offering the Undead Crypt");
        std::string r = P.choose(open);
        realm::Instance* in = P.realm.gateInstance();
        check(in && fs::exists(P.realm.instanceFolder(*in) + "/world.vxrp") && r.find("opens") != std::string::npos,
              "  picked: an instance is made in the save (" + (in ? P.realm.instanceFolder(*in) : std::string("-")) + ")");
        tick(3);
        check(g.isOpen(*session.world), "  and the gate opens");
        // walking in
        p.position = glm::vec3(g.base) + glm::vec3(0.5f, 0.0f, 0.5f) + g.normal() * 3.0f;
        tick(1);
        check(!P.travel.pending, "  standing in front of it: nothing");
        p.position = glm::vec3(g.base) + glm::vec3(0.5f, 0.0f, 0.5f);
        tick(1);
        check(P.travel.pending && P.travel.toEntry && in && P.travel.world == P.realm.worldName(*in), "  walking into the veil: the trip into the crypt");
        instName = P.travel.world; instId = in ? in->id : "";
        // saved: the realm knows its instance and gate
        P.realm.save();
        realm::Realm again; again.load(NAME, false);
        check(again.instances.size() == 1 && again.hasGate && again.gateBuilt && again.gate == P.realm.gate, "the realm is saved (realm.dat)");
        session.close();
    }

    std::printf("inside the instance\n");
    {
        Simulation sim;
        GameSession session(sim);
        check(session.begin(instName, false), "the instance opens like any world");
        session.buildGenerator();
        session.attach();
        check(session.dungeon() && session.dungeon()->hasEntryGate(), "  a crypt, with a gate in its portal room");
        glm::vec3 facing(0.0f);
        glm::vec3 at = P.arrival(session, facing);
        P.arrived();
        check(P.realm.here == instId && P.inInstance(), "  you're in it");
        ChunkKeeper keeper(*session.world, session.chunkSource());
        keeper.loadNow({ at, at + glm::vec3(40, 0, 0), at + glm::vec3(-40, 0, 0), at + glm::vec3(0, 0, 40), at + glm::vec3(0, 0, -40) });
        Player& p = sim.addPlayer();
        p.setMode(GameMode::Survival);
        p.position = at;
        auto tick = [&](int n) {
            for (int i = 0; i < n; i++) {
                sim.step(DT);
                session.tickDungeon({ &p }, DT);
                P.tick(DT, session, p, session.info.getSpawnPortal(), say);
                session.updateEdits(50.0f);
            }
        };
        tick(5);
        const Gate& eg = session.dungeon()->entryGate();
        check(eg.isBuilt(*session.world) && eg.isOpen(*session.world), "  its gate is built, open: the way home");
        check(session.world->typeAtVoxel((int)at.x, (int)at.y, (int)at.z) == BLOCK_AIR &&
              session.world->typeAtVoxel((int)at.x, (int)at.y - 1, (int)at.z) != BLOCK_AIR, "  you arrive standing in front of it");
        check(!P.travel.pending, "  and don't go straight back");
        std::string clk = P.timerText();
        check(clk.find("Undead Crypt") != std::string::npos && (clk.find("59:") != std::string::npos || clk.find("60:00") != std::string::npos), "  the clock: " + clk);
        // the boss falls: a shorter clock
        for (auto& n : sim.npcs().all()) (void)n;
        realm::Instance* in = P.realm.current();
        P.realm.beaten(instId);
        check(in && in->cleared && in->left <= 300.0f, "  the boss beaten: it closes in 5 minutes");
        // close it early from inside
        P.openMenu();
        int close = -1;
        for (size_t i = 0; i < P.options.size(); i++) if (P.options[i].action == "close") close = (int)i;
        check(close >= 0, "  its gate's menu: close it now");
        P.choose(close);
        check(in && in->closing && in->left <= 30.0f, "  closed: everyone out in 30 s");
        // the clock runs out: the trip home, then it goes
        for (int i = 0; i < 60 * 31 && !P.travel.pending; i++) tick(1);
        check(P.travel.pending && !P.travel.toEntry && P.travel.thenRemove == instId && P.travel.world == NAME, "  at zero: pulled back out to the main world");
        session.close();
    }

    std::printf("back home\n");
    {
        Simulation sim;
        GameSession session(sim);
        session.begin(NAME, false);
        session.buildGenerator();
        session.attach();
        glm::vec3 f(0.0f);
        glm::vec3 at = P.arrival(session, f);
        std::string folder = P.realm.folder() + "/instances/" + instId;
        P.arrived();
        check(!P.inInstance() && glm::length(at - P.mainGate().arrival()) < 0.01f, "out in front of the main world's gate");
        check(P.realm.instances.empty() && !fs::exists(folder), "the closed instance is deleted (" + folder + ")");
        session.close();
    }

    std::printf("the clocks\n");
    {
        realm::Realm R;
        R.load(NAME, false);
        realm::Instance* i = R.create("crypt", 77);
        check(i && std::fabs(i->left - 3600.0f) < 1.0f, "a new instance: an hour");
        std::string id = i->id;
        std::vector<std::string> warn;
        bool closed = false;
        for (int s = 0; s < 3600 + 5 && !closed; s++)
            for (auto& e : R.tick(1.0f)) {
                if (e.kind == realm::Realm::Event::Kind::Warning) warn.push_back(e.text);
                else closed = true;
            }
        check(warn.size() == 2 && warn[0].find("10 minutes") != std::string::npos && warn[1].find("1 minute") != std::string::npos,
              "warnings at 10 minutes and 1 minute");
        check(closed, "  and closed at an hour");
        check(R.remove(id) && R.instances.empty(), "  removed");
        check(!R.create("no_such_dungeon", 1), "an unknown dungeon isn't made");
    }

    fs::remove_all(std::string("saves/") + NAME, ec);
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
