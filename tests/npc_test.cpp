// Headless checks for NPCs (npc/): types from data, factions and
// reputation, the brain's states and events, senses, battle cry,
// menus, leash and search. Real physics on a flat world.
//
// Run from the repo root.
#include "TestWorld.h"
#include <cmath>

using namespace testworld;

static float flat(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/)\n"); return 1; }

    std::printf("types from data\n");
    {
        World w;
        w.player(glm::vec3(200, GROUND, 30), GameMode::Dev);   // someone in the world (NPCs far from every player are frozen), out of sight
        Npc* b = w.sim.npcs().spawn("bandit", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, 1));
        check(b && b->name == "Bandit" && b->moveset && b->maxHealth == 100.0f, "a bandit: name, sword moveset, health");
        check(b->faction == gFactions.find("bandits"), "in the bandits' faction");
        check(b->patrol.size() == 4, "with 4 patrol points around home");
        check(w.sim.npcs().spawn("no_such_type", glm::vec3(0), glm::vec3(0, 0, 1)) == nullptr, "an unknown type spawns nothing");
        w.run(0.2f);
        check(b->stateName == "patrol", "starts on patrol (it has the state)");
        Npc* s = w.sim.npcs().spawn("swordsman", glm::vec3(300, GROUND, 300), glm::vec3(0, 0, 1));
        w.run(0.2f);
        check(s->stateName == "idle", "a type without patrol starts idle");
        w.run(3.0f);
        float moved = flat(b->position, b->home);
        check(moved > 3.0f, "the patroller walks its route");
    }

    std::printf("seeing, alert, battle cry, combat\n");
    {
        World w;
        Player& p = w.player(glm::vec3(200, GROUND, 160));
        Npc* a = w.sim.npcs().spawn("swordsman", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, -1));   // facing the player
        Npc* friendBehindWall = w.sim.npcs().spawn("bandit", glm::vec3(260, GROUND, 230), glm::vec3(1, 0, 0));
        Npc* farAway = w.sim.npcs().spawn("bandit", glm::vec3(460, GROUND, 460), glm::vec3(1, 0, 0));
        w.wall(240, 242, 200, 260);                                      // the friend can't see the player
        bool alerted = w.until(1.0f, [&] { return a->stateName == "alert"; });
        check(alerted, "a hostile in sight: alert");
        check(a->target == p.uuid, "it knows who");
        bool fighting = w.until(2.0f, [&] { return a->stateName == "combat"; });
        check(fighting, "decides to fight after its alert time");
        bool heard = w.until(1.0f, [&] { return friendBehindWall->stateName != "patrol"; });
        check(heard && friendBehindWall->target == p.uuid, "an ally out of sight hears the battle cry and learns about you");
        check(farAway->stateName == "patrol", "one out of earshot doesn't");
        glm::vec3 before = friendBehindWall->position;
        w.run(3.0f);
        check(flat(friendBehindWall->position, p.position) < flat(before, p.position), "the ally comes toward you");
        float hp = p.health;
        w.until(8.0f, [&] { return p.health < hp; });
        check(p.health < hp, "and the fighter attacks you");
    }

    std::printf("hearing footsteps\n");
    {
        World w;
        // behind its back, out of sight: walking close is heard, sneaking isn't
        Npc* b = w.sim.npcs().spawn("swordsman", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, 1));
        Player& p = w.player(glm::vec3(200, GROUND, 190));
        w.run(0.3f);
        HumanIntent sneak; sneak.move = glm::vec2(1, 0); sneak.crouch = true;
        for (int i = 0; i < 60; i++) { w.sim.setIntent(p.uuid, sneak); w.sim.step(DT); }
        check(b->stateName == "idle", "sneaking behind it: not heard");
        HumanIntent walk; walk.move = glm::vec2(-1, 0);
        bool heard = false;
        for (int i = 0; i < 90 && !heard; i++) { w.sim.setIntent(p.uuid, walk); w.sim.step(DT); heard = b->stateName != "idle"; }
        check(heard, "walking behind it: heard, it turns to look");
    }

    std::printf("factions and reputation\n");
    {
        World w;
        Player& p = w.player(glm::vec3(200, GROUND, 180));
        Npc* v = w.sim.npcs().spawn("villager", glm::vec3(200, GROUND, 192), glm::vec3(0, 0, -1));
        Npc* g = w.sim.npcs().spawn("guard", glm::vec3(220, GROUND, 192), glm::vec3(0, 0, -1));
        w.run(2.0f);
        check(v->stateName == "idle" && g->stateName == "patrol", "neutral and friendly NPCs leave you alone");
        check(!w.sim.hostile(*v, p) && !w.sim.hostile(*g, p), "nobody is hostile to you yet");
        float rep = Simulation::standing(p, v->faction);
        w.sim.toolHit(p.uuid, v->uuid, 5.0f, 5.0f, v->bodyPoint(0.6f), glm::vec3(0, 0, 1));
        w.run(0.1f);
        check(Simulation::standing(p, v->faction) < rep, "hitting a villager costs reputation with villagers");
        check(w.sim.hostile(*v, p), "and that villager holds a grudge");
        w.run(0.3f);
        check(v->stateName == "flee", "a villager can't fight: it flees");
        check(g->stateName != "patrol", "a guard nearby hears the fight");
        p.reputation["bandits"] = 50;
        Npc* b = w.sim.npcs().spawn("bandit", glm::vec3(180, GROUND, 180), glm::vec3(1, 0, 0));
        w.run(0.5f);
        check(!w.sim.hostile(*b, p), "a good reputation with bandits: they don't attack");
    }

    std::printf("menus (unavailable)\n");
    {
        World w;
        Player& p = w.player(glm::vec3(200, GROUND, 180));
        Npc* g = w.sim.npcs().spawn("guard", glm::vec3(200, GROUND, 188), glm::vec3(0, 0, -1));
        Npc* v = w.sim.npcs().spawn("villager", glm::vec3(220, GROUND, 188), glm::vec3(0, 0, -1));
        Npc* b = w.sim.npcs().spawn("bandit", glm::vec3(300, GROUND, 300), glm::vec3(0, 0, 1));
        w.run(0.2f);
        check(!w.sim.openMenu(b->uuid, p.uuid), "a bandit won't talk (no 'unavailable' state)");
        check(w.sim.openMenu(v->uuid, p.uuid), "a villager talks");
        w.run(0.2f);
        float hp = v->health;
        w.sim.toolHit(p.uuid, v->uuid, 5.0f, 5.0f, v->bodyPoint(0.6f), glm::vec3(0, 0, 1));
        w.run(0.2f);
        check(v->health == hp && v->stateName == "unavailable", "protected in a menu: the hit doesn't land");
        w.sim.closeMenu(v->uuid);
        w.run(0.2f);
        check(v->stateName == "idle", "menu closed: back to what it was doing");
        check(w.sim.openMenu(g->uuid, p.uuid), "a guard will");
        w.run(0.2f);
        check(g->stateName == "unavailable", "and waits, unavailable");
        w.sim.toolHit(p.uuid, g->uuid, 5.0f, 5.0f, g->bodyPoint(0.6f), glm::vec3(0, 0, 1));
        w.run(0.2f);
        bool closed = false;
        for (auto& e : w.sim.takeEvents()) if (e.kind == SimEvent::Kind::MenuClosed && e.a == g->uuid) closed = true;
        check(g->stateName == "combat" && closed, "attacked: it drops the menu (event) and fights");

    }

    std::printf("losing you, search, leash\n");
    {
        World w;
        Player& p = w.player(glm::vec3(200, GROUND, 170));
        Npc* b = w.sim.npcs().spawn("bandit", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, -1));
        w.until(3.0f, [&] { return b->stateName == "combat"; });
        check(b->stateName == "combat", "fighting");
        // the player vanishes behind a wall far off
        p.position = glm::vec3(330, GROUND, 330); p.resetMotion();
        w.wall(300, 302, 280, 380);
        bool searching = w.until(6.0f, [&] { return b->stateName == "search"; });
        check(searching, "lost sight for a while: searches");
        check(flat(b->lastKnown, glm::vec3(200, GROUND, 170)) < 20.0f, "where it last saw you");
        bool back = w.until(40.0f, [&] { return b->stateName == "patrol"; });
        check(back, "gives up and goes back to its patrol");

        // leash: chased past it, it turns back
        Player& q = w.player(glm::vec3(200, GROUND, 185));
        b->home = b->position;
        bool chasing = w.until(4.0f, [&] { return b->stateName == "combat" && b->target == q.uuid; });
        check(chasing, "fights a new player");
        for (int i = 0; i < 60 * 12 && b->stateName == "combat"; i++) {
            q.position += glm::vec3(0, 0, -30.0f * DT);   // runs away (faster than it walks while circling)
            q.resetMotion();
            w.sim.step(DT);
        }
        check(b->stateName == "return" || b->stateName == "search", "leashed: stops chasing (" + b->stateName + ")");
    }

    std::printf("pathfinding\n");
    {
        // a wall straight across the way home, open at one end
        World w;
        w.player(glm::vec3(30, GROUND, 30), GameMode::Dev);
        w.wall(180, 240, 220, 222);
        Npc* s = w.sim.npcs().spawn("swordsman", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, 1));
        w.run(0.2f);
        s->home = glm::vec3(200, GROUND, 250);
        brain::change(*s, w.sim, "return");
        bool home = w.until(25.0f, [&] { return flat(s->position, s->home) < 3.0f; });
        check(home, "walks around a wall to get home (" + std::to_string(flat(s->position, s->home)) + " left)");
        check(w.sim.pathsPlanned > 0, "it planned a path to do it");

        // a ledge too high to walk up: it jumps
        World w2;
        w2.player(glm::vec3(30, GROUND, 30), GameMode::Dev);
        uint16_t stone = gRegistry.getIDByName("stone");
        for (int x = 190; x < 260; x++) for (int z = 230; z < 300; z++) for (int y = GROUND; y < GROUND + 8; y++)
            w2.chunks.getChunk(x / VOXEL_DIM, z / VOXEL_DIM)->setBlock(x % VOXEL_DIM, y, z % VOXEL_DIM, stone);
        Npc* j = w2.sim.npcs().spawn("swordsman", glm::vec3(220, GROUND, 210), glm::vec3(0, 0, 1));
        w2.run(0.2f);
        j->home = glm::vec3(220, GROUND + 8, 250);
        brain::change(*j, w2.sim, "return");
        bool up = w2.until(15.0f, [&] { return j->position.y > GROUND + 7.5f && flat(j->position, j->home) < 3.0f; });
        check(up, "jumps up an 8-voxel ledge to reach a goal on top");

        // nowhere to go: a goal inside a closed box
        World w3;
        w3.player(glm::vec3(30, GROUND, 30), GameMode::Dev);
        w3.wall(300, 330, 300, 301); w3.wall(300, 330, 330, 331); w3.wall(300, 301, 300, 330); w3.wall(329, 330, 300, 330);
        Npc* b = w3.sim.npcs().spawn("swordsman", glm::vec3(250, GROUND, 250), glm::vec3(0, 0, 1));
        w3.run(0.2f);
        b->home = glm::vec3(315, GROUND, 315);
        brain::change(*b, w3.sim, "return");
        w3.run(6.0f);
        check(!b->pathReached && flat(b->position, glm::vec3(315, GROUND, 315)) < 40.0f,
              "an unreachable goal: it gets as close as it can, and the game goes on");
    }

    std::printf("encounters\n");
    {
        World w;
        Player& p = w.player(glm::vec3(200, GROUND, 160), GameMode::Dev);   // Test Play: can't die mid-test
        p.facing = glm::vec3(0, 0, 1);                                        // facing them
        Npc* a = w.sim.npcs().spawn("swordsman", glm::vec3(196, GROUND, 196), glm::vec3(0, 0, -1));
        Npc* b = w.sim.npcs().spawn("swordsman", glm::vec3(204, GROUND, 196), glm::vec3(0, 0, -1));
        Npc* c = w.sim.npcs().spawn("swordsman", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, -1));
        uint32_t opened = 0;
        w.until(3.0f, [&] {
            for (auto& e : w.sim.takeEvents()) if (e.kind == SimEvent::Kind::EncounterStarted) opened = e.encounter;
            return a->stateName == "combat" && b->stateName == "combat" && c->stateName == "combat";
        });
        check(opened != 0, "a fight opens an encounter (event)");
        encounter::Encounter* e = w.sim.encounters().ofPlayer(p.uuid);
        check(e && e->active.size() == 3 && e->players.size() == 1, "with the three swordsmen and you in it");
        int front = 0, rear = 0, other = 0;
        if (e) for (auto& cl : e->claims) { if (cl.slot == Slot::Front) front++; else if (cl.slot == Slot::RearFlank) rear++; else other++; }
        check(front == 1 && rear == 1 && other == 1, "one in front, the next at the rear flank, the third elsewhere");
        int most = 0;
        for (int i = 0; i < 60 * 8; i++) {
            w.sim.step(DT);
            if (auto* x = w.sim.encounters().ofPlayer(p.uuid)) most = std::max(most, (int)x->tokens[p.uuid].size());
        }
        check(most >= 1 && most <= encounter::TOKENS_PER_TARGET, "no more than two attack you at once (" + std::to_string(most) + ")");
        // the rear flank holder ends up behind you
        glm::vec2 f(0), r(0);
        if (e = w.sim.encounters().ofPlayer(p.uuid); e) {
            for (auto& cl : e->claims) {
                Npc* n = w.sim.npcs().find(cl.npc);
                glm::vec2 off(n->position.x - p.position.x, n->position.z - p.position.z);
                if (cl.slot == Slot::Front) f = off; else if (cl.slot == Slot::RearFlank) r = off;
            }
        }
        check(a->stateName == "combat" && b->stateName == "combat" && c->stateName == "combat",
              "all three still on you (their swings don't hit each other)");
        check(glm::length(f) > 0 && glm::length(r) > 0 && glm::dot(glm::normalize(f), glm::normalize(r)) < -0.3f,
              "front and rear flank stand on opposite sides of you");
        // breadcrumbs
        auto* trail = w.sim.encounters().trail(p.uuid);
        check(trail && trail->size() >= 9, "you drop a breadcrumb every second");
        // they die: it ends, with a review
        uint32_t id = e ? e->id : 0;
        for (Npc* n : { a, b, c }) w.sim.toolHit(p.uuid, n->uuid, 500.0f, 100.0f, n->bodyPoint(0.6f), glm::vec3(0, 0, 1));
        bool ended = false;
        w.until(1.0f, [&] {
            for (auto& ev : w.sim.takeEvents()) if (ev.kind == SimEvent::Kind::EncounterEnded && ev.encounter == id) ended = true;
            return ended;
        });
        check(ended, "all of them dead: the encounter ends (event)");
        encounter::Encounter* done = w.sim.encounters().byId(id);
        check(done && done->tally[p.uuid].kills == 3 && done->tally[p.uuid].xp > 0.0f, "your review: 3 kills and XP");
    }
    {
        // two players apart: two encounters; one enemy fighting both: one
        World w;
        Player& p = w.player(glm::vec3(150, GROUND, 150), GameMode::Dev);
        Player& q = w.player(glm::vec3(350, GROUND, 350), GameMode::Dev);
        Npc* a = w.sim.npcs().spawn("swordsman", glm::vec3(150, GROUND, 170), glm::vec3(0, 0, -1));
        Npc* b = w.sim.npcs().spawn("swordsman", glm::vec3(350, GROUND, 370), glm::vec3(0, 0, -1));
        w.until(3.0f, [&] { return a->stateName == "combat" && b->stateName == "combat"; });
        auto* ep = w.sim.encounters().ofPlayer(p.uuid);
        auto* eq = w.sim.encounters().ofPlayer(q.uuid);
        check(ep && eq && ep != eq, "players apart: an encounter each");
        uint32_t keep = ep && eq ? std::min(ep->id, eq->id) : 0;
        w.sim.joinFight(*b, p.uuid);       // b now fights p too
        w.sim.takeEvents();
        w.sim.step(DT);
        ep = w.sim.encounters().ofPlayer(p.uuid);
        eq = w.sim.encounters().ofPlayer(q.uuid);
        bool merged = false;
        for (auto& ev : w.sim.takeEvents()) if (ev.kind == SimEvent::Kind::EncounterMerged) merged = true;
        check(ep && ep == eq && ep->id == keep && merged, "fighting the same enemy: they merge (the older one stays)");
        check(ep && ep->players.size() == 2 && ep->active.size() == 2, "one encounter, both players, both enemies");
    }
    {
        // getting away, and trackers
        World w;
        Player& p = w.player(glm::vec3(200, GROUND, 160));
        Npc* u = w.sim.npcs().spawn("undead", glm::vec3(200, GROUND, 190), glm::vec3(0, 0, -1));
        w.until(4.0f, [&] { return u->stateName == "combat"; });
        check(u->stateName == "combat", "the undead fights");
        // run off behind a wall, leaving a trail
        w.wall(150, 260, 140, 141);
        for (int i = 0; i < 60 * 6; i++) {
            p.position += glm::vec3(0, 0, -12.0f * DT);
            p.position.x += 6.0f * DT;
            p.resetMotion();
            w.sim.step(DT);
        }
        glm::vec3 endAt = p.position;
        w.until(20.0f, [&] { return flat(u->position, endAt) < 25.0f || u->stateName == "combat"; });
        check(flat(u->position, endAt) < 25.0f || u->stateName == "combat", "a tracker follows your trail past where it lost you");
        bool escaped = false;
        p.position = glm::vec3(480, GROUND, 40); p.resetMotion();
        w.wall(440, 442, 0, 120);
        w.until(40.0f, [&] {
            for (auto& ev : w.sim.takeEvents())
                if (ev.kind == SimEvent::Kind::EncounterEnded)
                    if (auto* e = w.sim.encounters().byId(ev.encounter)) escaped = e->tally[p.uuid].escaped || e->active.empty();
            return escaped;
        });
        check(escaped, "out of sight long enough: you got away, the encounter ends");
    }

    std::printf("saving, and far away\n");
    {
        World w;
        Npc* g = w.sim.npcs().spawn("guard", glm::vec3(210, GROUND, 220), glm::vec3(1, 0, 0));
        w.run(0.5f);
        g->health = 77.0f;
        glm::u8vec3 skin = g->appearance.skin, top = g->appearance.topColor;
        std::vector<glm::vec3> route = g->patrol;
        w.sim.npcs().save("/tmp/npc_test_npcs.dat");
        w.sim.npcs().clear();
        int n = w.sim.npcs().load("/tmp/npc_test_npcs.dat");
        Npc* back = w.sim.npcs().all().empty() ? nullptr : w.sim.npcs().all()[0].get();
        check(n == 1 && back && back->typeId == "guard", "an NPC saved and loaded");
        check(back && back->health == 77.0f && back->patrol.size() == route.size() && glm::length(back->patrol[0] - route[0]) < 0.01f,
              "with its health and patrol route");
        check(back && back->appearance.skin == skin && back->appearance.topColor == top, "and the same face and clothes");
        // with a player far away it is frozen in place
        w.sim.npcs().clear();
        Player& p = w.player(glm::vec3(20, GROUND, 20), GameMode::Dev);
        Npc* far = w.sim.npcs().spawn("bandit", glm::vec3(480, GROUND, 480), glm::vec3(1, 0, 0));
        w.run(0.2f);
        glm::vec3 at = far->position;
        w.run(3.0f);
        check(far->tier == 2 && glm::length(far->position - at) < 0.01f, "a far one is frozen (no time spent on it)");
        p.position = glm::vec3(300, GROUND, 300); p.resetMotion();
        w.run(3.0f);
        check(far->tier != 2 && glm::length(far->position - at) > 1.0f, "and wakes when a player comes closer");
    }

    std::printf("dead\n");
    {
        World w;
        Player& p = w.player(glm::vec3(200, GROUND, 170));
        Npc* b = w.sim.npcs().spawn("bandit", glm::vec3(200, GROUND, 200), glm::vec3(0, 0, -1));
        w.run(0.2f);
        w.sim.toolHit(p.uuid, b->uuid, 500.0f, 100.0f, b->bodyPoint(0.6f), glm::vec3(0, 0, 1));
        w.run(0.2f);
        check(b->stateName == "dead", "killed: dead state");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
