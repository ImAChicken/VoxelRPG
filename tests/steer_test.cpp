// Headless checks for context steering (npc/ContextSteering.h) and
// its use in fights: the straight line wins when it's open, walls
// and other NPCs are steered round, a crowd around one player
// spreads out instead of standing in a heap. Run from the repo root.
#include "TestWorld.h"
#include "npc/ContextSteering.h"
#include <cstdio>

using namespace testworld;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    auto open = [](glm::vec2) { return 0.0f; };

    std::printf("the maps\n");
    {
        steer::Inputs in;
        in.self = { 0, 0 }; in.goal = { 20, 0 }; in.target = { 40, 0 }; in.strike = 8; in.engage = true;
        steer::Map m = steer::compute(in, open);
        check(m.move.x > 0.95f, "open ground: straight at the goal");
        // a wall straight ahead
        steer::Map w = steer::compute(in, [](glm::vec2 d) { return d.x > 0.8f ? 1.0f : 0.0f; });
        check(w.move.x > 0.2f && std::fabs(w.move.y) > 0.3f, "a wall ahead: round it, still toward the goal");
        // another NPC right in the way
        in.others = { { 2, 0 } };
        steer::Map c = steer::compute(in, open);
        check(glm::length(c.move) > 0.05f && std::fabs(glm::normalize(c.move).y) > 0.5f && c.move.x > 0.0f,
              "another NPC in the way: steps past it, still going on");
        // at its spot: settles (or circles when asked)
        steer::Inputs at;
        at.self = { 20, 0 }; at.goal = { 20, 0 }; at.target = { 30, 0 }; at.strike = 8;
        check(glm::length(steer::compute(at, open).move) < 0.05f, "at its spot: stands");
        at.strafe = 1.0f;
        steer::Map s = steer::compute(at, open);
        check(glm::length(s.move) > 0.2f && std::fabs(s.move.x) < 0.5f, "asked to circle: moves sideways round the target");
        // too close and waiting its turn: backs off, never toward the target
        steer::Inputs near;
        near.self = { 0, 0 }; near.goal = { -10, 0 }; near.target = { 4, 0 }; near.strike = 8; near.engage = false;
        near.targetSwinging = true;
        check(steer::compute(near, open).move.x < 0.0f, "the target swinging close by: away from it");
    }

    std::printf("a crowd round one player\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        // five bandits all coming from one side, bunched up
        std::vector<Npc*> crowd;
        for (int i = 0; i < 5; i++)
            crowd.push_back(w.sim.npcs().spawn("bandit", glm::vec3(256 + 40 + i, GROUND, 256 + i * 1.5f), glm::vec3(-1, 0, 0)));
        int steered = 0;
        float closest = 1e9f, farthest = 0.0f;
        for (int k = 0; k < 60 * 12; k++) {
            HumanIntent in;                                    // the player stands still
            w.sim.setIntent(p.uuid, in);
            w.sim.step(DT);
            p.health = p.maxHealth;                            // keep the fight going
            if (k > 60 * 8) {
                for (size_t i = 0; i < crowd.size(); i++)
                    for (size_t j = i + 1; j < crowd.size(); j++) {
                        if (crowd[i]->dead() || crowd[j]->dead()) continue;
                        glm::vec2 d(crowd[i]->position.x - crowd[j]->position.x, crowd[i]->position.z - crowd[j]->position.z);
                        closest = std::min(closest, glm::length(d));
                    }
                for (Npc* n : crowd)
                    farthest = std::max(farthest, glm::length(glm::vec2(n->position.x - p.position.x, n->position.z - p.position.z)));
            }
        }
        for (Npc* n : crowd) steered += n->steerAt > 0.0f;
        std::printf("    closest two %.1f voxels apart, farthest from the player %.1f\n", closest, farthest);
        check(steered >= 4, "they steer (" + std::to_string(steered) + " of 5 used context steering)");
        // bodies can't overlap (2 x radius = 3): touching for a moment
        // in the scrum is allowed, standing inside one another isn't
        check(closest > 2.9f, "and never stand on top of each other");
        check(farthest < 40.0f, "while staying in the fight round the player");
    }

    std::printf("taking turns (npcs.toml style: duelists one at a time, a horde all at once)\n");
    {
        auto fight = [](const char* type, int count, int& most, float& shortestGap, float& waitDist) {
            World w;
            Player& p = w.player(glm::vec3(256, GROUND, 256));
            std::vector<Npc*> crowd;
            for (int i = 0; i < count; i++)
                crowd.push_back(w.sim.npcs().spawn(type, glm::vec3(256 + 30 + i * 3, GROUND, 256 + i * 4.0f), glm::vec3(-1, 0, 0)));
            most = 0; shortestGap = 99.0f;
            float lastEnd = -1.0f, t = 0.0f, waitSum = 0.0f; int waitN = 0;
            int was = 0;
            for (int k = 0; k < 60 * 16; k++) {
                HumanIntent in;
                w.sim.setIntent(p.uuid, in);
                w.sim.step(DT);
                t += DT;
                p.health = p.maxHealth;
                int now = 0;
                for (Npc* n : crowd) if (!n->dead() && n->attack.active()) now++;
                if (k > 60 * 4) {
                    most = std::max(most, now);
                    if (was > 0 && now == 0) lastEnd = t;
                    if (was == 0 && now > 0 && lastEnd > 0.0f) shortestGap = std::min(shortestGap, t - lastEnd);
                    for (Npc* n : crowd)
                        if (!n->dead() && !n->attack.active() && !w.sim.encounters().holdsToken(n->uuid)) {
                            waitSum += glm::length(glm::vec2(n->position.x - p.position.x, n->position.z - p.position.z)); waitN++;
                        }
                }
                was = now;
            }
            waitDist = waitN ? waitSum / waitN : 0.0f;
        };
        int most; float gap, wait;
        fight("bandit", 4, most, gap, wait);
        std::printf("    bandits: at most %d swinging at once, shortest pause %.2f s, the rest wait %.1f voxels off\n", most, gap, wait);
        check(most == 1, "duelists: one swings at a time");
        check(gap >= 0.5f && gap < 90.0f, "with a pause between turns");
        check(wait > 15.0f, "the rest wait well back");
        fight("undead", 6, most, gap, wait);
        std::printf("    undead: at most %d swinging at once, the rest %.1f voxels off\n", most, wait);
        check(most >= 2, "a horde: several swing at once");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
