// Headless checks for weapon hits from poses (game/CombatHits.h,
// HitShapes.h, sim/BodyPoses.h): the sword's voxel boxes, swung by
// the animated arm, against a dummy's limb boxes.
//
// Run from the repo root (it reads data/registry, data/models and
// data/animations).
#include "TestWorld.h"
#include "character/AnimClips.h"
#include "render/OutlineFx.h"
#include "game/LockOn.h"
#include <cstdio>

using namespace testworld;

// One swing at a dummy standing at 'offset' from the player (who
// faces -Z). Returns whether the swing hit, and where.
struct Swing { bool hit = false; int bone = -1; glm::vec3 point{0}; glm::vec3 dummyAt{0}; };
static Swing swingAt(glm::vec3 offset, bool heavy = false, bool wallBetween = false, const char* weapon = "iron_sword") {
    World w;
    glm::vec3 at(64.5f, (float)GROUND, 64.5f);
    if (wallBetween) w.wall(60, 69, 62, 62, 20);
    Player& p = w.player(at);
    p.facing = glm::vec3(0, 0, -1);
    p.inventory.setSlot(Inventory::FIRST_MAIN, gItems.find(weapon), 1);
    p.inventory.select(Inventory::FIRST_MAIN);
    Npc& d = *w.sim.npcs().spawn("dummy", at + offset, glm::vec3(0, 0, 1));
    w.run(0.4f);
    w.sim.takeEvents();
    Swing s;
    s.dummyAt = d.position;
    HumanIntent in; in.attackLight = !heavy; in.attackHeavy = heavy;
    w.sim.setIntent(p.uuid, in);
    for (int i = 0; i < 100 && !s.hit; i++) {
        p.facing = glm::vec3(0, 0, -1);
        w.sim.step(DT);
        for (const SimEvent& e : w.sim.takeEvents())
            if (e.kind == SimEvent::Kind::Hit && e.a == p.uuid) { s.hit = true; }
    }
    // the contact (point, bone) as the hit code saw it
    if (s.hit) {
        s.point = d.position;   // filled below if the pose says more
    }
    return s;
}

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gHeldModels.loadDir("data/models/held");
    int clips = gAnims.load("data/animations", "data/animations/retarget.toml");
    gAnimMap.load("data/animations/anim_map.toml");
    std::printf("loaded %d clips\n", clips);

    std::printf("weapon boxes\n");
    {
        const HeldModelDef* sw = gHeldModels.find("sword");
        check(sw && !sw->hitBoxes.empty() && sw->hitBoxes.size() < 40,
              "the sword's voxels merge into a few boxes (" + std::to_string(sw ? sw->hitBoxes.size() : 0) + ")");
        // every voxel centre is inside one of the boxes
        bool inside = true;
        if (sw)
            for (auto& v : sw->voxels) {
                glm::vec3 c = sw->origin + (glm::vec3(v.p) + 0.5f) * sw->voxel;
                bool any = false;
                for (auto& b : sw->hitBoxes) any = any || (glm::all(glm::greaterThanEqual(c, b.first)) && glm::all(glm::lessThanEqual(c, b.second)));
                inside = inside && any;
            }
        check(inside, "and the boxes cover every voxel");
        size_t total = 0;
        if (sw) for (auto& b : sw->hitBoxes) { glm::vec3 e = (b.second - b.first) / sw->voxel; total += (size_t)std::lround(e.x * e.y * e.z); }
        check(sw && total == sw->voxels.size(), "and nothing else (" + std::to_string(total) + " voxels)");
    }

    std::printf("strike marks (anim_map.toml)\n");
    {
        // for every marked attack: the blade tip is fastest inside
        // the hit window (leaving out the blend into the attack and
        // back out of it)
        CharacterModel model = CharacterModel::build(Appearance{});
        for (const auto& [id, st] : gAnimMap.strikes) {
            const AttackDef* a = gMovesets.attack(id);
            if (!a) continue;
            uint16_t item = 0;
            for (const char* w : { "iron_sword", "iron_axe", "wooden_club", "spear", "quarterstaff" }) {
                uint16_t i = gItems.find(w);
                if (i && gMovesets.find(gItems.get(i).moveset) && id.rfind(gItems.get(i).moveset + "_", 0) == 0) item = i;
            }
            const HeldModelDef* hm = item ? gHeldModels.find(gItems.get(item).heldModel) : nullptr;
            if (!hm) { check(false, id + ": a weapon model to swing"); continue; }
            CharacterAnimator an;
            AnimInput in; in.facingYawDeg = -90.0f; in.combat = true;
            in.grounded = a->id.find("air") == std::string::npos;
            for (int i = 0; i < 40; i++) an.update(model, in, 1.0f / 60.0f);
            float inPeak = 0.0f, outPeak = 0.0f, inAt = 0.0f, outAt = 0.0f;
            glm::vec3 last(0.0f);
            bool have = false;
            const float step = 1.0f / 120.0f;
            for (float t = 0.0f; t <= a->duration; t += step) {
                in.attack = a; in.attackT = t;
                const Pose& p = an.update(model, in, step);
                std::array<glm::mat4, BONE_COUNT> M;
                worldBoneMatrices(model, p, glm::vec3(0.0f), M);
                std::vector<hitshape::OBB> wb;
                hitshape::weaponBoxes(*hm, model, M[B_WRIST_R] * hm->turn(model, p.heldTurn), wb);
                glm::vec3 tip(0.0f); float far = -1.0f;
                for (auto& o : wb) {
                    glm::vec3 c[8]; hitshape::corners(o, c);
                    for (auto& x : c) { float d = glm::length(x - glm::vec3(M[B_WRIST_R][3])); if (d > far) { far = d; tip = x; } }
                }
                float v = have ? glm::length(tip - last) / step : 0.0f;
                last = tip; have = true;
                if (t < 0.07f || t > a->duration - 0.13f) continue;
                bool inside = t >= a->activeStart && t <= a->activeEnd;
                if (inside && v > inPeak) { inPeak = v; inAt = t; }
                if (!inside && v > outPeak) { outPeak = v; outAt = t; }
            }
            char b[160];
            std::snprintf(b, sizeof(b), "%s: fastest in its window (%.0f vox/s at %.2f s; outside %.0f at %.2f s)",
                          id.c_str(), inPeak, inAt, outPeak, outAt);
            check(inPeak > outPeak, b);
        }
    }

    std::printf("poses in the simulation\n");
    {
        World w;
        Player& p = w.player(glm::vec3(64.5f, (float)GROUND, 64.5f));
        p.inventory.setSlot(Inventory::FIRST_MAIN, gItems.find("iron_sword"), 1);
        p.inventory.select(Inventory::FIRST_MAIN);
        Npc& d = *w.sim.npcs().spawn("dummy", glm::vec3(64.5f, (float)GROUND, 58.5f), glm::vec3(0, 0, 1));
        w.run(0.3f);
        hitshape::Posed pp, pd;
        check(w.sim.bodyPoses().posed(p.uuid, pp) && pp.held, "the player is posed, holding the sword");
        check(w.sim.bodyPoses().posed(d.uuid, pd), "the dummy is posed");
        if (pp.now) {
            glm::vec3 head((*pp.now)[B_HEAD][3]), feet = p.position;
            check(head.y > feet.y + 9.0f && head.y < feet.y + 17.0f, "the head is where a head goes (" + std::to_string(head.y - feet.y) + " up)");
        }
        check(w.sim.bodyAt(d.position + glm::vec3(0, 8, 0), p.uuid) == d.uuid, "a point in the dummy's chest is the dummy");
        check(w.sim.bodyAt(d.position + glm::vec3(2.6f, 8, 0), p.uuid) == 0, "a point beside its chest is not (the old capsule said it was)");
    }

    std::printf("swings\n");
    {
        check(swingAt({ 0, 0, -4 }).hit, "a light swing hits a dummy 4 voxels ahead");
        check(swingAt({ 0, 0, -6 }).hit, "and 6 ahead");
        check(!swingAt({ 0, 0, 5 }).hit, "not one behind");
        check(!swingAt({ 0, 0, -24 }).hit, "nor one far ahead");
        check(swingAt({ 0, 0, -5 }, true).hit, "a heavy swing hits ahead");
        check(!swingAt({ 0, 0, -5 }, false, true).hit, "a wall between stops it");
        // how far the blade really reaches straight ahead
        // how far the blade really reaches straight ahead (to the
        // dummy's middle; its own arm is held out in front of it)
        float far = 0.0f;
        bool gap = false;
        for (float z = 4.0f; z <= 26.0f; z += 0.5f) {
            bool h = swingAt({ 0, 0, -z }).hit;
            if (h) far = z;
            if (!h && far > 0.0f && z > far + 2.0f) gap = true;
        }
        std::printf("  ..  light 1 reaches %.1f voxels ahead, lunge included (moveset reach %.1f)\n", far,
                    gMovesets.attack("sword_light1") ? gMovesets.attack("sword_light1")->reach : 0.0f);
        check(far >= 6.0f && gap, "the reach ends where the blade ends");
        check(swingAt({ 0, 0, -(far + 1.5f) }).hit == false, "a step past it is a miss");
    }

    std::printf("club, spear and quarterstaff\n");
    {
        auto reachOf = [](const char* weapon, bool heavy) {
            float far = 0.0f;
            for (float z = 3.0f; z <= 34.0f; z += 1.0f) if (swingAt({ 0, 0, -z }, heavy, false, weapon).hit) far = z;
            return far;
        };
        for (const char* wpn : { "wooden_club", "spear", "quarterstaff" }) {
            const ItemDef& d = gItems.get(gItems.find(wpn));
            check(d.canFight() && gMovesets.find(d.moveset) != nullptr, std::string(wpn) + ": fights with the " + d.moveset + " moveset");
            check(swingAt({ 0, 0, -6 }, false, false, wpn).hit, std::string(wpn) + ": a light attack hits 6 ahead");
            check(swingAt({ 0, 0, -6 }, true, false, wpn).hit, std::string(wpn) + ": a heavy attack hits 6 ahead");
        }
        float sword = reachOf("iron_sword", false), spear = reachOf("spear", false), staff = reachOf("quarterstaff", false), club = reachOf("wooden_club", false);
        std::printf("  ..  light reach: sword %.0f, club %.0f, staff %.0f, spear %.0f voxels\n", sword, club, staff, spear);
        check(spear >= sword + 5.0f, "the spear's thrust reaches well past the sword (it turns to point along the thrust)");
        check(staff > sword, "the quarterstaff reaches past the sword");
        check(!swingAt({ 9, 0, -4 }, false, false, "spear").hit, "a thrust is narrow: a dummy off to the side is missed");
        check(swingAt({ 0, 0, -5 }, true, false, "wooden_club").hit && swingAt({ 0, 0, -10 }, true, false, "wooden_club").hit,
              "the club's overhead lands from 5 to 10 ahead");
        const AttackDef* th = gMovesets.attack("spear_thrust");
        check(th && th->thrust && gAnimMap.attackClip("spear_thrust") >= 0, "spear thrusts turn the spear and play Punch_Cross");
    }

    std::printf("lag compensation (BodyPoses history, Simulation::setViewDelay)\n");
    {
        // a dummy stands ahead; just as the swing starts it moves away
        // (as a remote target would have, by the time the attacker's
        // input reaches the server)
        auto swingAfterMove = [](int delayTicks) {
            World w;
            glm::vec3 at(64.5f, (float)GROUND, 64.5f);
            Player& p = w.player(at);
            p.facing = glm::vec3(0, 0, -1);
            p.inventory.setSlot(Inventory::FIRST_MAIN, gItems.find("iron_sword"), 1);
            p.inventory.select(Inventory::FIRST_MAIN);
            Npc& d = *w.sim.npcs().spawn("dummy", at + glm::vec3(0, 0, -6), glm::vec3(0, 0, 1));
            w.run(0.6f);
            w.sim.takeEvents();
            w.sim.setViewDelay(p.uuid, delayTicks);
            HumanIntent in; in.attackLight = true;
            w.sim.setIntent(p.uuid, in);
            bool hit = false;
            for (int i = 0; i < 60 && !hit; i++) {
                if (i == 1) { d.position += glm::vec3(30, 0, 0); d.prevPosition = d.position; }
                p.facing = glm::vec3(0, 0, -1);
                w.sim.step(DT);
                for (const SimEvent& e : w.sim.takeEvents()) hit = hit || (e.kind == SimEvent::Kind::Hit && e.a == p.uuid);
            }
            return hit;
        };
        check(!swingAfterMove(0), "with no view delay, a target that moved away is missed");
        check(swingAfterMove(25), "seen 25 ticks late (lag + interpolation), the swing meets it where it was on screen");
        check(!swingAfterMove(3), "but only that far back: 3 ticks of delay doesn't reach before the move");
    }

    std::printf("lock-on (game/LockOn.h)\n");
    {
        World w;
        glm::vec3 eye(64.5f, (float)GROUND + 15.0f, 64.5f), front(0, 0, -1);   // looking along -Z
        auto& npcs = w.sim.npcs();
        Npc* ahead = npcs.spawn("dummy", { 64.5f, (float)GROUND, 34.5f }, { 0, 0, 1 });
        Npc* right = npcs.spawn("dummy", { 84.5f, (float)GROUND, 40.5f }, { 0, 0, 1 });   // +X is to the right looking along -Z
        Npc* left  = npcs.spawn("bandit", { 44.5f, (float)GROUND, 40.5f }, { 0, 0, 1 });
        npcs.spawn("dummy", { 64.5f, (float)GROUND, 94.5f }, { 0, 0, 1 });               // behind
        npcs.spawn("dummy", { 64.5f, (float)GROUND, -60.0f }, { 0, 0, 1 });              // too far
        Npc* t = LockOn::pick(npcs.all(), eye, front, nullptr);
        check(t == ahead, "TAB picks the body nearest the middle of the view");
        Npc* h = LockOn::pick(npcs.all(), eye, front, [&](const Npc& n) { return n.typeId == "bandit"; });
        check(h == left, "a hostile one is picked before others");
        Npc* r = LockOn::pick(npcs.all(), eye, front, nullptr, ahead->uuid, +1, ahead->position);
        Npc* l = LockOn::pick(npcs.all(), eye, front, nullptr, ahead->uuid, -1, ahead->position);
        check(r == right && l == left, "a flick right / left switches to the next body that way");
        LockOn lk;
        int f0 = lk.flicked(40.0f, 0.02f), f1 = lk.flicked(60.0f, 0.02f), f2 = lk.flicked(200.0f, 0.02f);
        check(f0 == 0 && f1 == 1 && f2 == 0, "a quick sideways flick switches once (then rests)");
        float yaw = LockOn::yawTo(eye, ahead->position);
        float y = 170.0f;
        for (int i = 0; i < 120; i++) y = LockOn::turnToward(y, yaw, 1.0f / 60.0f);
        check(std::fabs(std::fmod(y - yaw + 540.0f, 360.0f) - 180.0f) < 1.0f, "the camera swings onto the target (the short way round)");
    }

    std::printf("outline effects (render/OutlineFx.h)\n");
    {
        OutlineFx fx;
        check(fx.load("data/registry/outline.toml") && fx.effect("hurt") && fx.effect("charge"), "outline.toml loads");
        World w;
        Player& p = w.player(glm::vec3(64.5f, (float)GROUND, 64.5f));
        check(fx.glowFor(p).a == 0.0f, "nothing glows by itself");
        SimEvent e; e.kind = SimEvent::Kind::Hit; e.b = p.uuid; e.amount = 10.0f;
        fx.onEvent(e);
        glm::vec4 g = fx.glowFor(p);
        check(g.a > 0.5f && g.r > 0.9f && g.g < 0.3f, "taking damage flashes red");
        e.blocked = true;
        fx.onEvent(e);
        g = fx.glowFor(p);
        check(g.r > 0.9f && g.r > 0.9f, "a hit still showing red wins over a block (priority)");
        fx.update(0.5f);
        check(fx.glowFor(p).a == 0.0f, "and both fade out");
        fx.onEvent(e);
        g = fx.glowFor(p);
        check(g.a > 0.5f && g.g > 0.9f && g.b > 0.9f, "a blocked hit flashes white");
        fx.update(1.0f);
        e.guardBroken = true;
        fx.onEvent(e);
        check(fx.glowFor(p).a > 0.5f, "a guard break flashes too");
        fx.update(1.0f);
        // charge: the heavy's wind-up glows yellow and builds; a light doesn't
        p.attack.kind = AttackKind::Heavy;
        p.attack.d = gMovesets.attack("sword_heavy");
        p.attack.t = 0.05f;
        float early = fx.glowFor(p).a;
        p.attack.t = p.attack.d->activeStart * 0.75f;
        g = fx.glowFor(p);
        check(early > 0.0f && g.a > early && g.r > 0.9f && g.g > 0.7f && g.b < 0.4f, "a heavy's wind-up glows yellow, building up");
        p.attack.t = p.attack.d->activeEnd + 0.5f;
        check(fx.glowFor(p).a == 0.0f, "and is gone once the swing is through");
        p.attack.kind = AttackKind::Light;
        p.attack.d = gMovesets.attack("sword_light1");
        p.attack.t = 0.1f;
        check(fx.glowFor(p).a == 0.0f, "a quick light attack doesn't glow");
        p.attack.kind = AttackKind::None; p.attack.d = nullptr;
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
