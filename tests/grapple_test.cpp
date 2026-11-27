// Headless checks for the grapple (game/Grapple.h; VoxelPhysics
// grappleTick; Simulation::grappleOk) and the Hywirl, the flier you
// can swing from (npcs.toml hywirl; npc/States.h flyerFight). Run
// from the repo root.
#include "TestWorld.h"
#include "net/Protocol.h"
#include "game/CombatKit.h"
#include <sstream>
#include "character/MonsterModel.h"
#include <cstdio>

using namespace testworld;

static void put(World& w, int x, int y, int z, uint16_t id) {
    if (Chunk* c = w.chunks.getChunk(x / VOXEL_DIM, z / VOXEL_DIM)) c->setBlock(x % VOXEL_DIM, y, z % VOXEL_DIM, id);
}
static glm::vec3 mid(int x, int y, int z) { return glm::vec3(x, y, z) + glm::vec3(0.5f); }

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gMonsters.load("data/models/monsters");
    const uint16_t HOOK = gItems.find("grappling_hook");
    const uint16_t POINT = gRegistry.getIDByName("grapple_point");
    const uint16_t STONE = gRegistry.getIDByName("stone");
    check(HOOK && gItems.get(HOOK).grappleRange > 0.0f, "the grappling hook is an item with a reach");
    check(POINT && gRegistry.get(POINT).isGrapple(), "grapple_point blocks have the grapple flag");

    std::printf("aiming\n");
    {
        World w;
        put(w, 256, GROUND + 40, 300, POINT);              // up ahead
        put(w, 200, GROUND + 30, 300, POINT);              // off to the side
        glm::vec3 eye(256.5f, GROUND + 16.0f, 256.5f);
        glm::vec3 at = mid(256, GROUND + 40, 300);
        grapple::Target t = grapple::aim(w.chunks, eye, glm::normalize(at - eye), 120.0f);
        check(t.ok && t.on == 0 && glm::length(t.at - at) < 0.01f, "looking at a grapple point finds it");
        glm::vec3 near = glm::normalize(at + glm::vec3(1.5f, 0.0f, 0.0f) - eye);
        t = grapple::aim(w.chunks, eye, near, 120.0f);
        check(t.ok && glm::length(t.at - at) < 0.01f, "… and a little off it too (the aim helps)");
        t = grapple::aim(w.chunks, eye, glm::vec3(0, 0, -1), 120.0f);
        check(!t.ok, "looking well away: nothing");
        t = grapple::aim(w.chunks, eye, glm::normalize(at - eye), 30.0f);
        check(!t.ok, "out of reach: nothing");
        // a wall between
        for (int x = 250; x < 263; x++) for (int y = GROUND; y < GROUND + 60; y++) put(w, x, y, 280, STONE);
        t = grapple::aim(w.chunks, eye, glm::normalize(at - eye), 120.0f);
        check(!t.ok, "behind a wall: nothing");
        // a body near the aim wins over nothing
        grapple::Candidate c; c.id = 7; c.point = eye + glm::vec3(0, 0, 18); c.radius = 3.0f;
        t = grapple::aim(w.chunks, eye, glm::vec3(0, 0, 1), 120.0f, { c });
        check(t.ok && t.on == 7, "a body where you look is a target");
    }

    std::printf("aim assist\n");
    {
        World w;
        glm::vec3 eye(256.5f, GROUND + 16.0f, 256.5f);
        glm::ivec3 A(256, GROUND + 40, 320), B(290, GROUND + 30, 320);   // A ~21 deg up, B ~28 deg off to the side
        put(w, A.x, A.y, A.z, POINT);
        put(w, B.x, B.y, B.z, POINT);
        std::vector<glm::ivec3> pts;
        grapple::scanPoints(w.chunks, eye, 120.0f, pts);
        check(pts.size() == 2, "scanPoints finds both points from the runs (" + std::to_string(pts.size()) + ")");
        grapple::Aim a; a.eye = eye; a.hand = eye; a.reach = 120.0f;
        a.dir = glm::vec3(0, 0, 1);                                    // looking level, straight ahead
        grapple::Target t = grapple::aim(w.chunks, a, pts);
        check(t.ok && glm::length(t.at - mid(A.x, A.y, A.z)) < 0.01f, "looking level ahead, the point well above the crosshair is caught");
        a.dir = glm::normalize(mid(B.x, B.y, B.z) - eye + glm::vec3(-6, 0, 0));
        t = grapple::aim(w.chunks, a, pts);
        check(t.ok && glm::length(t.at - mid(B.x, B.y, B.z)) < 0.01f, "looking near the other, it's that one");
        // half way between: the marked one keeps it (no flicker)
        glm::vec3 dA = glm::normalize(mid(A.x, A.y, A.z) - eye), dB = glm::normalize(mid(B.x, B.y, B.z) - eye);
        a.dir = glm::normalize(dA + dB);
        grapple::Target fromA = t; fromA.at = mid(A.x, A.y, A.z);
        grapple::Target fromB = t; fromB.at = mid(B.x, B.y, B.z);
        a.prev = &fromA; grapple::Target tA = grapple::aim(w.chunks, a, pts);
        a.prev = &fromB; grapple::Target tB = grapple::aim(w.chunks, a, pts);
        check(tA.ok && tB.ok && glm::length(tA.at - fromA.at) < 0.01f && glm::length(tB.at - fromB.at) < 0.01f, "half way between two, the one marked stays marked");
        a.prev = nullptr;
        // the reach is from the hand
        a.dir = dA; a.hand = eye + glm::vec3(0, 0, -100); 
        check(!grapple::aim(w.chunks, a, pts).ok, "out of the hand's reach: nothing, however near the view");
        a.hand = eye;
        // behind you: nothing
        a.dir = glm::vec3(0, 0, -1);
        check(!grapple::aim(w.chunks, a, pts).ok, "behind you: nothing");
        // a raw (being edited) section is scanned too
        Chunk* ch = w.chunks.getChunk(A.x / VOXEL_DIM, A.z / VOXEL_DIM);
        ch->section((A.y) / VOXEL_DIM).rawData();
        grapple::scanPoints(w.chunks, eye, 120.0f, pts);
        check(pts.size() == 2, "… and an expanded section's points too");
        // the cache rescans when it's moved
        grapple::PointCache cache;
        cache.update(w.chunks, eye, 120.0f, 0.016f);
        check(cache.points.size() == 2, "the cache finds them");
        put(w, 256, GROUND + 50, 300, POINT);
        cache.update(w.chunks, eye, 120.0f, 0.016f);
        check(cache.points.size() == 2, "… and keeps them a moment");
        cache.update(w.chunks, eye, 120.0f, 0.3f);
        check(cache.points.size() == 3, "… then looks again (a new point)");
    }

    std::printf("equipped like a spellbook\n");
    {
        check(CombatKit::fitsAbility(HOOK) && CombatKit::fitsOff(HOOK), "it goes in the ability slots, the quick options and the off hand");
        check(CombatKit::itemKind(HOOK).rfind("Grapple", 0) == 0, "the inventory calls it a grapple");
        Inventory inv; inv.add(HOOK, 1);
        CombatKit kit; kit.tidy(inv);
        check(kit.sets[0].slots[2][0] == HOOK && kit.sets[1].slots[2][0] == HOOK, "carried, it goes in B by itself");
        int b = -1;
        check(kit.grappleReady(true, &b) == HOOK && b == 2, "in combat, B throws it");
        check(!kit.grappleReady(false), "out of combat, not from B");
        kit.quick[3] = HOOK;
        check(kit.grappleReady(false, &b) == HOOK && b == 7, "a quick option: in combat or out");
        kit.quick[3] = 0;
        kit.sets[0].slots[2][0] = kit.sets[1].slots[2][0] = 0; kit.tidy(inv);
        check(!kit.has(HOOK), "taken out, it stays out");
        kit.sets[0].off = HOOK;
        uint16_t cast = 0;
        check(kit.special(0, &cast) == CombatKit::Special::Cast && cast == HOOK, "in the off hand, LT throws it");
        std::stringstream ss; kit.write(ss);
        CombatKit k2; std::string line;
        while (std::getline(ss, line)) { size_t e = line.find('='); if (e != std::string::npos) k2.read(line.substr(0, e), line.substr(e + 1)); }
        k2.tidy(inv);
        check(k2.hooked && !k2.sets[0].slots[2][0], "saved and loaded, it isn't put back");
    }

    auto throwAt = [](World& w, Player& p, glm::vec3 at, uint64_t on, bool hold, float seconds, auto each) {
        HumanIntent in; in.grapple = true; in.grappleHeld = hold; in.grappleAt = at; in.grappleOn = on;
        w.sim.setIntent(p.uuid, in);
        w.sim.step(DT);
        HumanIntent keep; keep.grappleHeld = hold;
        for (int i = 0; i < (int)(seconds / DT); i++) { w.sim.setIntent(p.uuid, keep); w.sim.step(DT); if (each(i)) break; }
    };

    std::printf("pulled to a point (a tap)\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        w.run(0.2f);
        put(w, 256, GROUND + 45, 330, POINT);
        glm::vec3 at = mid(256, GROUND + 45, 330);
        // without one, nothing
        throwAt(w, p, at, 0, false, 0.3f, [&](int) { return false; });
        check(!p.grapple.active() && p.position.z < 260.0f, "without a grappling hook: nothing happens");
        p.inventory.add(HOOK, 1);
        // at a block that isn't a grapple point: nothing
        throwAt(w, p, glm::vec3(256.5f, GROUND - 0.5f, 300.5f), 0, false, 0.3f, [&](int) { return false; });
        check(!p.grapple.active(), "at plain stone: nothing");
        float closest = 1e9f, topY = -1e9f; bool hopped = false;
        throwAt(w, p, at, 0, false, 3.0f, [&](int) {
            closest = std::min(closest, glm::length(grapple::hand(p) - at));
            if (!p.grapple.active() && p.velocity.y > 0.0f) hopped = true;
            topY = std::max(topY, p.position.y);
            return hopped;
        });
        std::printf("    came within %.1f of it; rose to %.0f (the point's at %.0f)\n", closest, topY, at.y);
        check(closest < grapple::ARRIVE + 1.0f, "pulled all the way to it");
        check(hopped, "… and let go with a hop up over it");
        check(p.hasDoubleJump && p.hasAirDodge, "off the rope: the air moves come back");
    }

    std::printf("swinging (held)\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND + 40, 256));
        p.inventory.add(HOOK, 1);
        // a high point to swing from, the body hanging off to the side
        put(w, 256, GROUND + 100, 290, POINT);
        glm::vec3 at = mid(256, GROUND + 100, 290);
        float maxLen = 0.0f, len0 = -1.0f; bool swung = false; float zMax = 0.0f;
        throwAt(w, p, at, 0, true, 2.5f, [&](int) {
            if (p.grapple.phase == GrappleState::Phase::Swing) {
                if (len0 < 0.0f) len0 = p.grapple.length;
                swung = true;
                maxLen = std::max(maxLen, glm::length(grapple::hand(p) - at));
                zMax = std::max(zMax, p.position.z);
            }
            return false;
        });
        std::printf("    rope %.1f long, the body as far as %.1f from the anchor; swung out to z %.0f\n", len0, maxLen, zMax);
        check(swung, "held past a tap: it swings");
        check(maxLen <= len0 + 0.6f, "… and the rope holds it at its length");
        check(zMax > at.z + 3.0f, "… out past under the anchor (a pendulum)");
        // let go: off with its speed
        HumanIntent hold; hold.grappleHeld = true;
        for (int i = 0; i < 300 && glm::length(p.velocity) < 12.0f; i++) { w.sim.setIntent(p.uuid, hold); w.sim.step(DT); }
        glm::vec3 v = p.velocity;
        std::printf("    speed on the rope %.1f\n", glm::length(v));
        HumanIntent none; w.sim.setIntent(p.uuid, none); w.sim.step(DT);
        check(!p.grapple.active() && glm::length(p.velocity) > glm::length(v) * 0.7f && glm::length(v) > 5.0f, "let go: off it with its speed");
    }

    std::printf("a lunge at an enemy\n");
    {
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.inventory.add(HOOK, 1);
        const uint16_t SWORD = gItems.find("iron_sword");
        p.inventory.add(SWORD, 1);
        p.inventory.equipFrom(p.inventory.find(SWORD), EquipSlot::Weapon1);
        p.combatStance = true;
        p.refreshWeapon();
        w.run(0.2f);
        Npc* m = w.sim.npcs().spawn("mushnub", glm::vec3(256, GROUND, 320), glm::vec3(0, 0, -1));
        check(m != nullptr, "a Mushnub out ahead");
        if (!m) return 1;
        w.run(0.2f);
        HumanIntent in; in.grapple = true; in.grappleAt = m->bodyPoint(0.5f); in.grappleOn = m->uuid;
        w.sim.setIntent(p.uuid, in); w.sim.step(DT);
        check(p.grapple.active() && !p.grapple.swingable, "thrown at it (a body on the ground: no swing)");
        // the attack pressed on the way comes out on arrival
        HumanIntent atk; atk.attackLight = true;
        w.sim.setIntent(p.uuid, atk); w.sim.step(DT);
        bool attacked = false; float gap = 0.0f;
        for (int i = 0; i < 120 && !attacked; i++) {
            w.sim.setIntent(p.uuid, HumanIntent()); w.sim.step(DT);
            if (!p.grapple.active() && p.attack.active()) { attacked = true; gap = glm::length(glm::vec2(m->position.x - p.position.x, m->position.z - p.position.z)); }
        }
        std::printf("    arrived %.1f from it\n", gap);
        check(attacked, "the light attack pressed on the way comes out as it gets there");
        check(gap < grapple::LUNGE_STOP + 6.0f && gap > 1.0f, "… stopping short of it, in reach");
        // villagers (friendly) can't be grappled
        Npc* v = w.sim.npcs().spawn("villager", glm::vec3(300, GROUND, 256), glm::vec3(-1, 0, 0));
        if (v) {
            HumanIntent fi; fi.grapple = true; fi.grappleAt = v->bodyPoint(0.5f); fi.grappleOn = v->uuid;
            p.grapple = GrappleState();
            w.sim.setIntent(p.uuid, fi); w.sim.step(DT);
            check(!p.grapple.active(), "a villager (not after her): no");
        }
    }

    std::printf("the hywirl\n");
    {
        const NpcType* hy = gNpcTypes.find("hywirl");
        check(hy && hy->flies && hy->grapplePoint, "the Hywirl flies and has a grapple point");
        if (!hy) return 1;
        const MonsterModel* model = gMonsters.find(hy->model);
        check(model && model->clip(hy->clips.idle) >= 0 && model->clip(hy->clips.run) >= 0 && model->clip(hy->clips.death) >= 0,
              "its model and flying clips are there");
        World w;
        Player& p = w.player(glm::vec3(256, GROUND, 256));
        p.inventory.add(HOOK, 1);
        w.run(0.2f);
        Npc* h = w.sim.npcs().spawn("hywirl", glm::vec3(256, GROUND + 30, 340), glm::vec3(0, 0, -1));
        check(h && h->flier, "spawned in the air");
        if (!h) return 1;
        float lowest = 1e9f;
        w.run(1.0f);
        bool fights = w.until(6.0f, [&] { lowest = std::min(lowest, h->position.y); return h->stateName == "combat"; });
        check(fights, "it sees her and comes for her");
        float hp = p.health;
        int dives = 0;
        bool hurt = w.until(25.0f, [&] { lowest = std::min(lowest, h->position.y); if (h->swing == 0 || h->swing == 1) dives++; return p.health < hp; });
        std::printf("    lowest it flew: %.0f (ground %d); %.0f damage\n", lowest, GROUND, hp - p.health);
        check(lowest > GROUND + 1.0f, "it never lands (it flies)");
        check(hurt, "it dives in and hits her");
        // grapple it: swing from it
        p.health = p.maxHealth;
        w.run(3.0f);                                    // (up again, if it knocked her down)
        std::printf("    she's %.0f from it\n", glm::length(grappleAnchor(*h) - grapple::hand(p)));
        glm::vec3 gp = grappleAnchor(*h);
        HumanIntent in; in.grapple = true; in.grappleHeld = true; in.grappleAt = gp; in.grappleOn = h->uuid;
        w.sim.setIntent(p.uuid, in); w.sim.step(DT);
        check(p.grapple.active() && p.grapple.on == h->uuid && p.grapple.swingable, "grappled: its grapple point holds");
        bool followed = false;
        HumanIntent keep; keep.grappleHeld = true;
        for (int i = 0; i < 60; i++) {
            w.sim.setIntent(p.uuid, keep); w.sim.step(DT);
            if (p.grapple.active() && glm::length(p.grapple.anchor - grappleAnchor(*h)) < 2.0f && glm::length(p.grapple.anchor - gp) > 0.5f) followed = true;
        }
        check(followed, "the rope's end goes where it goes");
        h->health = 0.0f;
        w.run(0.1f);
        check(!p.grapple.active(), "it dies: the rope drops");
        bool fell = w.until(6.0f, [&] { return h->position.y < GROUND + 2.0f; });
        check(fell || h->fellOutOfWorld, "dead, it falls");
    }

    std::printf("over the network\n");
    {
        HumanIntent in; in.grapple = true; in.grappleHeld = true; in.grappleAt = glm::vec3(1, 2, 3); in.grappleOn = 0x123456789ull;
        wire::Writer wr; proto::putIntent(wr, in);
        wire::Reader rd(wr.buf.data(), wr.buf.size());
        HumanIntent out = proto::getIntent(rd);
        check(out.grapple && out.grappleHeld && out.grappleAt == in.grappleAt && out.grappleOn == in.grappleOn, "a throw goes to the server whole");
    }

    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
