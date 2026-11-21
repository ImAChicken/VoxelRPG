// Headless checks for seamless portals (world/PortalLinks.h,
// game/PortalGun.h; docs/dungeons.md step 7): the way through (a body
// in front of one end comes out in front of the other, turned), the
// portal gun putting ends on walls, a body walking into a wall where a
// portal is and out of another wall facing another way, still moving;
// one way portals. (What's drawn through them: render/PortalView.h, seen
// with VOXELRPG_SCRIPT and shot.) Makes saves/_seamless_test and deletes
// it. Run from the repo root.
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include "game/PortalGun.h"
#include "game/Portals.h"
#include "server/Worlds.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <filesystem>

using namespace testworld;
namespace fs = std::filesystem;

static bool near(glm::vec3 a, glm::vec3 b, float e = 1e-3f) { return glm::length(a - b) < e; }

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    const char* NAME = "_seamless_test";
    std::error_code ec;
    fs::remove_all(std::string("saves/") + NAME, ec);

    std::printf("the way through\n");
    {
        PortalEnd a, b;
        a.centre = { 0, 11, 0 }; a.normal = { 0, 0, 1 };        // faces +z
        b.centre = { 100, 31, 50 }; b.normal = { -1, 0, 0 };    // faces -x, higher up
        glm::mat4 M = PortalLink::through(a, b);
        glm::vec3 p = glm::vec3(M * glm::vec4(a.centre + glm::vec3(0, 0, 0.2f), 1.0f));
        check(near(p, b.centre + glm::vec3(-0.2f, 0, 0) * -1.0f) || near(p, b.centre - b.normal * 0.2f),
              "the middle of one opening, just in front, is the middle of the other, just behind");
        glm::vec3 in = glm::mat3(M) * glm::vec3(0, 0, -1);      // walking into a
        check(near(in, b.normal), "  walking into one, you walk out of the other");
        glm::vec3 left = a.centre + a.right() * 2.0f;
        glm::vec3 l2 = glm::vec3(M * glm::vec4(left, 1.0f));
        check(near(l2, b.centre - b.right() * 2.0f), "  left stays left as you go through (a half turn, not a mirror)");
        glm::mat4 back = PortalLink::through(b, a);
        check(near(glm::vec3(back * M * glm::vec4(3, 4, 5, 1)), glm::vec3(3, 4, 5)), "  and back the other way undoes it");
    }

    std::printf("both sides\n");
    {
        auto into = [](const PortalLinks& L, glm::vec3 p0, glm::vec3 p1, glm::vec3& out) {
            PortalLinks::Crossing c = L.crossed(p0, p1);
            if (!c.link) return false;
            out = glm::vec3(c.through * glm::vec4(p1, 1.0f));
            return true;
        };
        PortalEnd a, b;
        a.centre = { 0, 11, 0 }; a.normal = { 0, 0, 1 };
        b.centre = { 100, 11, 50 }; b.normal = { -1, 0, 0 };
        glm::vec3 out;
        PortalLinks L;
        L.add(a, b, true, "t");
        check(!into(L, { 0, 11, -1 }, { 0, 11, 1 }, out), "back Wall: in at the back, nothing");
        a.back = b.back = PortalEnd::Back::Through;
        L.clear(); L.add(a, b, true, "t");
        check(into(L, { 0, 11, 1 }, { 0, 11, -0.5f }, out) && b.local(out).z > 0.0f, "back Through: in at the front, out of the other's front");
        check(into(L, { 0, 11, -1 }, { 0, 11, 0.5f }, out) && b.local(out).z < 0.0f, "  in at the back, out of the other's back");
        check(into(L, { 100, 11, 51 }, { 100, 11, 50 }, out) == false, "  (beside the opening: nothing)");
        check(into(L, { 101, 11, 50 }, { 99.5f, 11, 50 }, out) && a.local(out).z < 0.0f, "  the other one's back too, back to this one's back");
        a.back = PortalEnd::Back::Turn; b.back = PortalEnd::Back::Wall;
        L.clear(); L.add(a, b, false, "t");
        check(into(L, { 0, 11, -1 }, { 0, 11, 0.5f }, out) && b.local(out).z > 0.0f, "back Turn: in at the back, out of the other's front too");
        PortalLinks::Crossing c = L.crossed({ 0, 11, -1 }, { 0, 11, 0.5f });
        glm::vec3 v = glm::mat3(c.through) * glm::vec3(0, 0, 1);    // walking into the back (+z)
        check(near(v, b.normal), "  walking on out of it, not back into it");
        check(!into(L, { 99, 11, 50 }, { 100.5f, 11, 50 }, out), "  (one way: the other's back is a wall)");
    }

    Simulation sim;
    GameSession session(sim);
    FlatOptions flat; flat.plants = false;
    session.beginNew(NAME, 5, GameMode::Dev, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
    session.info.setRoamingSpawns(false);
    session.buildGenerator();
    session.attach();
    sim.roaming.enabled = false;
    glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(session.world->sizeX(), session.world->sizeZ()));
    const int G = (int)spawn.y;
    ChunkKeeper keeper(*session.world, session.chunkSource());
    keeper.loadNow({ spawn, spawn + glm::vec3(80, 0, 0), spawn + glm::vec3(80, 0, 100) });
    auto tick = [&](int n) { for (int i = 0; i < n; i++) { sim.step(DT); session.updateEdits(50.0f); } };
    // two walls: one across x (facing -x), one across z (facing -z)
    {
        EditBatch e; e.source = EditSource::Dev; e.undoable = false;
        uint16_t BR = gRegistry.getIDByName("bricks");
        for (int y = G; y < G + 36; y++) {
            for (int z = (int)spawn.z - 20; z <= (int)spawn.z + 20; z++) e.set((int)spawn.x + 40, y, z, BR);
            for (int x = (int)spawn.x + 60; x <= (int)spawn.x + 100; x++) e.set(x, y, (int)spawn.z + 100, BR);
        }
        session.editor->submit(std::move(e));
        session.updateEdits(1e6f);
    }

    std::printf("the portal gun\n");
    PortalGun gun;
    {
        PortalEnd e; std::string why;
        glm::vec3 eye = spawn + glm::vec3(10, 15, 0);
        check(PortalGun::aim(*session.world, eye, glm::vec3(1, 0, 0), e, why), "a shot at a wall puts an end there" + (why.empty() ? "" : " (" + why + ")"));
        check(near(e.normal, glm::vec3(-1, 0, 0)) && std::fabs(e.centre.x - (spawn.x + 40)) < 0.01f, "  on the wall's face, facing back at you");
        check(std::fabs((e.centre.y - e.half.y) - (float)G) < 0.01f, "  standing on the floor (aimed at eye height)");
        gun.place(0, e, session.portals);
        check(session.portals.empty() && gun.have[0], "  one end alone is no portal yet");
        PortalEnd e2;
        check(!PortalGun::aim(*session.world, eye, glm::vec3(0, 1, 0), e2, why), "the sky: no");
        check(!PortalGun::aim(*session.world, eye, glm::vec3(-1, 0, 0), e2, why), "nothing there: no");
        glm::vec3 eye2 = spawn + glm::vec3(80, 15, 80);
        check(PortalGun::aim(*session.world, eye2, glm::vec3(0, 0, 1), e2, why) && near(e2.normal, glm::vec3(0, 0, -1)), "the other wall: the orange end");
        gun.place(1, e2, session.portals);
        check(session.portals.links.size() == 1 && gun.linkId, "  with both up: a portal");
        check(session.portals.links[0].a.back == PortalEnd::Back::Wall && session.portals.links[0].b.back == PortalEnd::Back::Wall, "  on walls: one sided");
        // standing on the ground
        PortalGun g2;
        PortalEnd s1, s2;
        glm::vec3 e3 = spawn + glm::vec3(-10, 15, -30);
        why.clear();
        check(PortalGun::aim(*session.world, e3, glm::vec3(0, -15, 20), s1, why), "a shot at the ground stands an end there" + (why.empty() ? "" : " (" + why + ")"));
        check(std::fabs((s1.centre.y - s1.half.y) - (float)G) < 0.01f && glm::dot(s1.normal, glm::vec3(0, 0, -1)) > 0.99f, "  on the ground, facing you");
        check(PortalGun::aim(*session.world, e3, glm::vec3(-20, -15, 0), s2, why), "  and another");
        g2.place(0, s1, session.portals); g2.place(1, s2, session.portals);
        const PortalLink* sl = session.portals.find(g2.linkId);
        check(sl && sl->a.back == PortalEnd::Back::Through && sl->b.back == PortalEnd::Back::Through, "  both standing: through from either side");
        g2.place(1, e2, session.portals);
        sl = session.portals.find(g2.linkId);
        check(sl && sl->a.back == PortalEnd::Back::Turn && sl->b.back == PortalEnd::Back::Wall, "  one standing, one on a wall: either side out of the wall's");
        g2.clear(session.portals);
        PortalEnd s3;
        check(!PortalGun::aim(*session.world, spawn + glm::vec3(36, 15, 0), glm::vec3(1, -4, 0), s3, why), "standing against a wall: no room");
    }

    std::printf("walking through\n");
    {
        Player& p = sim.addPlayer();
        p.setMode(GameMode::Dev);
        const PortalEnd& A = gun.ends[0];
        const PortalEnd& B = gun.ends[1];
        p.position = glm::vec3(A.centre.x - 12.0f, (float)G, A.centre.z);
        p.facing = glm::vec3(1, 0, 0);
        tick(20);
        check(session.physics->capsuleIsClear(glm::vec3(A.centre.x + 0.2f, (float)G, A.centre.z), Human::RADIUS, Human::STAND_HEIGHT), "the wall behind the opening lets a body in");
        uint32_t trips = p.portalTrips;
        bool through = false;
        for (int i = 0; i < 60 * 3 && !through; i++) {
            HumanIntent in; in.move = glm::vec2(1, 0);
            sim.setIntent(p.uuid, in);
            tick(1);
            through = p.portalTrips != trips;
        }
        check(through, "walk into the blue end ...");
        glm::vec3 l = B.local(p.position + glm::vec3(0, p.height * 0.5f, 0));
        check(l.z > -0.01f && l.z < 2.0f && std::fabs(l.x) < B.half.x, "  ... and out of the orange one, on the other wall");
        check(glm::dot(glm::normalize(glm::vec3(p.velocity.x, 0, p.velocity.z)), B.normal) > 0.9f && glm::length(p.velocity) > 5.0f,
              "  still walking, now the way the orange end faces");
        check(glm::dot(p.facing, B.normal) > 0.9f, "  facing that way too");
        for (int i = 0; i < 30; i++) { HumanIntent in; in.move = glm::vec2(0, -1); sim.setIntent(p.uuid, in); tick(1); }
        check(B.local(p.position).z > 3.0f && p.portalTrips == trips + 1, "  and on out into the room, not back through");
        // and back
        trips = p.portalTrips;
        through = false;
        for (int i = 0; i < 60 * 3 && !through; i++) {
            HumanIntent in; in.move = glm::vec2(0, 1);           // (+z: into the orange end)
            sim.setIntent(p.uuid, in);
            tick(1);
            through = p.portalTrips != trips;
        }
        check(through && A.local(p.position).z > -0.01f, "back in the orange end, out of the blue (two way)");

        std::printf("one way\n");
        gun.setTwoWay(false, session.portals);
        p.position = glm::vec3(B.centre.x, (float)G, B.centre.z - 10.0f);
        p.velocity = glm::vec3(0.0f);
        tick(5);
        trips = p.portalTrips;
        for (int i = 0; i < 60 * 2; i++) { HumanIntent in; in.move = glm::vec2(0, 1); sim.setIntent(p.uuid, in); tick(1); }
        check(p.portalTrips == trips && B.local(p.position).z > 0.5f, "the orange end of a one way portal is a wall");
        p.position = glm::vec3(A.centre.x - 10.0f, (float)G, A.centre.z);
        tick(5);
        through = false;
        for (int i = 0; i < 60 * 3 && !through; i++) { HumanIntent in; in.move = glm::vec2(1, 0); sim.setIntent(p.uuid, in); tick(1); through = p.portalTrips != trips; }
        check(through, "  the blue end still goes through");
        gun.clear(session.portals);
        check(session.portals.empty() && !session.portals.passable((int)A.centre.x - 1, G + 5, (int)A.centre.z), "taken away: the walls are walls again");
    }

    session.close();
    fs::remove_all(std::string("saves/") + NAME, ec);

    std::printf("the gate into the crypt, seamless\n");
    {
        const char* MAIN = "_seamless_main";
        fs::remove_all(std::string("saves/") + MAIN, ec);
        realm::gDungeonKinds.load();
        Worlds worlds;
        GameSession* m = worlds.create(MAIN, 3, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
        m->info.setRoamingSpawns(false);
        m->buildGenerator(); m->attach();
        m->sim.roaming.enabled = false;
        worlds.pin(m);
        PortalDirector P;
        P.realm.load(MAIN, false);
        P.beforeRemove = [&](const std::string& n) { worlds.close(n, false); };
        auto lookup = [&](const std::string& n) { return worlds.get(n); };
        glm::vec3 sp = glm::vec3(m->gen->spawnPoint(m->world->sizeX(), m->world->sizeZ()));
        Player& p = m->sim.addPlayer();
        p.setMode(GameMode::Survival);
        p.position = sp;
        std::vector<std::string> said;
        auto say = [&](const std::string& t) { said.push_back(t); };
        GameSession* here = m;
        HumanIntent walk;
        auto frame = [&](int n) {
            for (int i = 0; i < n; i++) {
                P.syncGates(lookup);
                P.tick(DT, *here, p, true, say);
                for (const auto& w : P.wanted(*here, p.position))
                    if (GameSession* s2 = worlds.open(w.world, false)) {
                        glm::vec3 at = w.point;
                        if (w.entry) if (const dungeon::Run* r = s2->dungeon(); r && r->hasEntryGate()) at = r->entryGate().arrival();
                        worlds.watch(s2->name(), at);
                    }
                here->sim.setIntent(p.uuid, walk);
                worlds.tick(DT, 1, DT);
                // (a player through a portal moves in Worlds::tick; here = wherever it is now)
                for (GameSession* w : worlds.all()) for (auto& pl : w->sim.players()) if (pl.get() == &p && w != here) { here = w; P.arrivedSeamless(w->name()); }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        for (int i = 0; i < 400 && !P.realm.gateBuilt; i++) frame(1);
        check(P.realm.gateBuilt, "the main world's gate stands");
        P.options = { { "", "open:crypt" } };
        P.choose(0);
        realm::Instance* inst = P.realm.gateInstance();
        check(inst != nullptr, "a crypt opened at it");
        Gate mg = P.mainGate();
        p.position = mg.arrival(12.0f);
        bool ready = false;
        for (int i = 0; i < 1500 && !ready; i++) { frame(1); ready = P.seamlessMain; }
        GameSession* I = inst ? worlds.get(P.realm.worldName(*inst)) : nullptr;
        check(ready && I, "walking up to it: the crypt opens in the background, its gate stands, and the gate goes seamless");
        PortalLink* gl = m->portals.findKind("gate");
        check(gl && I && gl->b.world == I->name() && !gl->twoWay, "  the main gate is a portal into the crypt");
        check(!mg.isOpen(*m->world), "  its veil gone: an open arch you see through");
        check(I && I->portals.findKind("gate") && I->portals.findKind("gate")->b.world == MAIN, "  and the crypt's gate one back");
        // walk in
        walk = HumanIntent(); walk.move = glm::vec2(-mg.normal().x, -mg.normal().z);
        bool in = false;
        for (int i = 0; i < 60 * 6 && !in; i++) { frame(1); in = here == I; }
        check(in, "walk into the gate: you're in the crypt (its simulation), no fade");
        if (in) {
            const Gate& eg = I->dungeon()->entryGate();
            glm::vec3 l = eg.end().local(p.position + glm::vec3(0, p.height * 0.5f, 0));
            check(l.z > -0.01f && l.z < 3.0f && std::fabs(l.x) < 5.0f, "  just in front of the crypt's own gate");
            check(glm::dot(p.facing, eg.normal()) > 0.9f, "  facing out of it, into the portal room");
            check(P.inInstance() && P.realm.here == inst->id, "  the realm knows you're in it");
            check(m->sim.players().empty() && I->sim.players().size() == 1, "  gone from the main world's simulation");
            // an NPC after you comes through too
            Npc* d = m->sim.npcs().spawn("bandit", mg.arrival(3.0f), -mg.normal());
            Simulation::Transfer t; t.id = d ? d->uuid : 0; t.world = I->name();
            t.through = PortalLink::through(gl->a, gl->b);
            Human* moved = d ? Worlds::transfer(*m, *I, t) : nullptr;
            check(moved && I->sim.npcs().find(t.id) && !m->sim.npcs().find(t.id), "an NPC through the gate goes into the crypt's simulation");
            // and back out
            walk = HumanIntent(); walk.move = glm::vec2(0, 0);
            for (int i = 0; i < 90; i++) frame(1);                        // (away from the gate first)
            p.position = eg.arrival(6.0f);
            walk.move = glm::vec2(-eg.normal().x, -eg.normal().z);
            bool out = false;
            for (int i = 0; i < 60 * 6 && !out; i++) { frame(1); out = here == m; }
            check(out && !P.inInstance(), "back through the crypt's gate: out in the main world");
            if (out) {
                glm::vec3 l2 = mg.end().local(p.position + glm::vec3(0, p.height * 0.5f, 0));
                check(l2.z > -0.01f && l2.z < 3.0f, "  in front of the main gate");
            }
        }
        worlds.closeAll(false);
        fs::remove_all(std::string("saves/") + MAIN, ec);
    }
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
