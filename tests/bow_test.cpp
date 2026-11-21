// Headless checks for ranged combat (docs/dungeons.md step 4): the
// bow (movesets.toml "bow": a quick light, a heavy held to draw),
// arrows (Simulation::shoot / flyProjectiles, combat::shotHit), and
// the skeleton archer (npcs.toml skeleton_archer, style "ranged":
// npc/States.h Combat::rangedFight). Makes a flat world in
// saves/_bow_test and deletes it. Run from the repo root.
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include "character/HeldItemModel.h"
#include "character/CharacterModel.h"
#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace testworld;
namespace fs = std::filesystem;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    gHeldModels.loadDir("data/models/held");
    const char* NAME = "_bow_test";
    std::error_code ec;
    fs::remove_all(std::string("saves/") + NAME, ec);

    const uint16_t BOW = gItems.find("short_bow"), DAGGER = gItems.find("bone_dagger");
    std::printf("the bow and the dagger\n");
    {
        check(BOW && gItems.get(BOW).moveset == "bow" && gHeldModels.find(gItems.get(BOW).heldModel), "a short bow: the bow moveset, a bow in hand");
        check(DAGGER && gItems.get(DAGGER).moveset == "dagger" && gHeldModels.find(gItems.get(DAGGER).heldModel), "a bone dagger: quick stabs");
        const Moveset* m = gMovesets.find("bow");
        check(m && m->light.size() == 1 && m->heavy && m->heavy->chargeAt >= 0.0f && m->heavy->effect == "arrow" &&
              m->light[0]->effect == "arrow", "  light: a quick shot; heavy: an arrow drawn (a charge)");
        check(m && m->heavy->cancelFrom == 0.0f, "  a dodge cancels the draw");
    }

    std::printf("a skeleton\n");
    {
        const LookPreset* lp = gNpcTypes.look("skeleton");
        Appearance a = lp ? lp->make(7) : Appearance();
        check(lp && a.skeleton, "the skeleton look: bones (look_bones)");
        CharacterModel body = CharacterModel::build(Appearance::random(7)), bones = CharacterModel::build(a);
        check(bones.triangleCount() > 200 && bones.triangleCount() < body.triangleCount(),
              "  a lighter body: " + std::to_string(bones.triangleCount()) + " triangles (a person: " + std::to_string(body.triangleCount()) + ")");
        check(std::fabs(bones.heightIn - body.heightIn) < 6.0f, "  on the same frame (it moves and is hit like anyone)");
        std::ostringstream o; a.write(o);
        Appearance back; std::istringstream in(o.str()); std::string line;
        while (std::getline(in, line)) { auto e = line.find('='); if (e != std::string::npos) back.read(line.substr(0, e), line.substr(e + 1)); }
        check(back.skeleton, "  saved and read back");
    }

    Simulation sim;
    GameSession session(sim);
    FlatOptions flat; flat.plants = false;
    session.beginNew(NAME, 5, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
    session.buildGenerator();
    session.attach();
    ChunkKeeper keeper(*session.world, session.chunkSource());
    glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(session.world->sizeX(), session.world->sizeZ()));
    keeper.loadNow({ spawn, spawn + glm::vec3(150, 0, 0), spawn + glm::vec3(-150, 0, 0), spawn + glm::vec3(0, 0, 150), spawn + glm::vec3(0, 0, -150) });
    auto tick = [&](int n) { for (int i = 0; i < n; i++) { sim.step(DT); session.tickWorld(DT); } };

    Player& p = sim.addPlayer();
    p.setMode(GameMode::Survival);
    p.position = spawn;
    p.inventory.add(BOW, 1);
    p.inventory.equipFrom(p.inventory.find(BOW), EquipSlot::Weapon1);
    p.combatStance = true;
    p.infiniteStamina = true;
    tick(30);
    p.refreshWeapon();
    check(p.moveset == gMovesets.find("bow"), "drawn in the combat stance");

    // shoot at a dummy 'dist' ahead; light (false) or drawn for 'hold' s
    auto shootAt = [&](Npc* d, bool heavy, float hold, float pitch, SimEvent* hitOut) {
        p.attack = AttackState();
        p.aimPitch = pitch;
        glm::vec3 to = d->position - p.position; to.y = 0;
        p.facing = glm::normalize(to);
        HumanIntent in;
        if (heavy) { in.attackHeavy = true; in.heavyHeld = true; } else in.attackLight = true;
        sim.setIntent(p.uuid, in);
        float held = 0.0f;
        bool hit = false;
        for (int i = 0; i < 300 && !hit; i++) {
            HumanIntent k; k.heavyHeld = heavy && held < hold;
            held += DT;
            sim.step(DT); session.tickWorld(DT);
            if (i == 0) sim.setIntent(p.uuid, k); else sim.setIntent(p.uuid, k);
            for (const SimEvent& e : sim.takeEvents())
                if (e.kind == SimEvent::Kind::Hit && e.a == p.uuid && e.b == d->uuid && e.attack == AttackKind::Shot) { hit = true; if (hitOut) *hitOut = e; }
        }
        return hit;
    };

    std::printf("shooting\n");
    {
        Npc* d = sim.npcs().spawn("dummy", p.position + glm::vec3(60, 0, 0), glm::vec3(-1, 0, 0));
        tick(5);
        float hp = d->health;
        SimEvent quick;
        bool hit = shootAt(d, false, 0.0f, 0.0f, &quick);
        float qd = hp - d->health;
        check(hit && qd > 0.0f, "a quick shot hits a dummy 60 voxels off (" + std::to_string((int)qd) + " damage)");
        check(quick.charge < 0.01f && quick.react != React::Stumble, "  undrawn: no stumble");
        tick(60);
        d->health = d->maxHealth; d->poise = d->maxPoise; hp = d->health;
        SimEvent full;
        hit = shootAt(d, true, 2.0f, 0.0f, &full);
        float fd = hp - d->health;
        check(hit && fd > qd * 2.0f, "  fully drawn it hits much harder (" + std::to_string((int)fd) + ")");
        check(full.charge > 0.99f && (full.react == React::Stumble || full.react == React::Launch || full.poiseBroken),
              std::string("  and stumbles it (") + reactName(full.react) + ")");
        { EntityId id = d->uuid; sim.npcs().removeIf([&](const Npc& n) { return n.uuid == id; }); }
        tick(5);
    }

    std::printf("aiming up and down\n");
    {
        // nothing to hit: where it lands goes by the pitch
        auto landFrom = [&](float pitch) {
            p.attack = AttackState();
            p.facing = glm::vec3(0, 0, 1);
            p.aimPitch = pitch;
            size_t before = sim.stuckArrows().size();
            HumanIntent in; in.attackLight = true;
            sim.setIntent(p.uuid, in);
            for (int i = 0; i < 400; i++) {
                sim.step(DT); sim.setIntent(p.uuid, HumanIntent());
                if (sim.stuckArrows().size() > before) return sim.stuckArrows().back().pos;
            }
            return glm::vec3(-1.0f);
        };
        glm::vec3 low = landFrom(-20.0f), flatShot = landFrom(4.0f);
        float dl = glm::length(glm::vec2(low.x - p.position.x, low.z - p.position.z));
        float df = glm::length(glm::vec2(flatShot.x - p.position.x, flatShot.z - p.position.z));
        std::printf("    down 20 deg: %.0f vox, up 4 deg: %.0f vox\n", dl, df);
        check(low.y > 0.0f && dl < 60.0f, "aimed down it sticks in the ground close by");
        check(flatShot.y > 0.0f && df > dl * 2.0f, "  aimed up a little it flies much further");
        check(sim.stuckArrows().size() >= 2, "  arrows stay stuck where they hit");
    }

    std::printf("dodging and guarding\n");
    {
        Npc* d = sim.npcs().spawn("dummy", p.position + glm::vec3(50, 0, 0), glm::vec3(-1, 0, 0));
        tick(5);
        // a guard facing the arrow takes it (the dummy's brain drops a
        // guard every tick, so straight to the hit)
        d->guarding = true;
        d->facing = glm::vec3(-1, 0, 0);
        float hp = d->health;
        combat::HitEvent e;
        bool hit = combat::shotHit(*d, p.uuid, 10.0f, 10.0f, 10.0f, React::Flinch, 0.0f, d->bodyPoint(0.6f), glm::vec3(1, 0, 0), e);
        check(hit && e.blocked && d->health == hp, "a guard facing it stops it: no damage");
        d->facing = glm::vec3(1, 0, 0);
        hit = combat::shotHit(*d, p.uuid, 10.0f, 10.0f, 10.0f, React::Flinch, 0.0f, d->bodyPoint(0.6f), glm::vec3(1, 0, 0), e);
        check(hit && !e.blocked && d->health < hp, "  from behind it doesn't");
        d->guarding = false;
        d->health = d->maxHealth;
        tick(90);                       // (over the flinch: its ragdoll moves in the simulation now, BodyPoses)
        // a roll lets it through
        p.attack = AttackState();
        p.aimPitch = 0.0f;
        p.facing = glm::vec3(1, 0, 0);
        HumanIntent in; in.attackLight = true;
        sim.setIntent(p.uuid, in);
        hp = d->health;
        bool rolled = false;
        for (int i = 0; i < 90; i++) {
            if (!sim.projectiles().empty() && !rolled) {
                // just before it arrives
                const auto& a = sim.projectiles().front();
                if (glm::length(a.pos - d->position) < 25.0f) {
                    d->move = Human::Move::Roll; d->moveTimer = Human::ROLL_TIME * 0.7f; rolled = true;
                }
            }
            sim.step(DT); sim.setIntent(p.uuid, HumanIntent());
        }
        check(rolled && d->health == hp, "a roll at the right moment lets it pass through");
        { EntityId id = d->uuid; sim.npcs().removeIf([&](const Npc& n) { return n.uuid == id; }); }
        tick(5);
    }

    std::printf("the skeleton archer\n");
    {
        p.position = spawn;
        p.combatStance = false;
        p.health = p.maxHealth = 10000.0f;
        Npc* a = sim.npcs().spawn("skeleton_archer", spawn + glm::vec3(0, 0, 50), glm::vec3(0, 0, -1));
        check(a && a->appearance.skeleton && a->weaponItem == "short_bow", "spawns as bones, with a bow");
        // it shoots from where it stands; arrows come and hit
        int shots = 0, hits = 0;
        float closest = 1e9f, farthest = 0.0f;
        for (int i = 0; i < 60 * 12; i++) {
            tick(1);
            for (const SimEvent& e : sim.takeEvents()) {
                if (e.kind == SimEvent::Kind::Shot && e.a == a->uuid) shots++;
                if (e.kind == SimEvent::Kind::Hit && e.a == a->uuid && e.b == p.uuid && e.attack == AttackKind::Shot) hits++;
            }
            float dd = glm::length(glm::vec2(a->position.x - p.position.x, a->position.z - p.position.z));
            if (i > 120) { closest = std::min(closest, dd); farthest = std::max(farthest, dd); }
        }
        std::printf("    %d shots, %d hits, kept %.0f - %.0f vox away\n", shots, hits, closest, farthest);
        check(shots >= 3, "it shoots (" + std::to_string(shots) + " arrows in 12 s)");
        check(hits >= shots / 2, "  and mostly hits a target standing still");
        check(closest > 15.0f, "  from a distance");

        // walk at it: it backs off
        float before = glm::length(a->position - p.position);
        for (int i = 0; i < 60 * 3; i++) {
            glm::vec3 to = a->position - p.position; to.y = 0;
            HumanIntent in; in.move = glm::normalize(glm::vec2(to.x, to.z)); in.sprint = true;
            p.facing = glm::normalize(to);
            sim.setIntent(p.uuid, in);
            tick(1);
            sim.takeEvents();
        }
        std::string st = a->state ? a->state->debug(*a) : "";
        std::printf("    after 3 s chasing: %.0f vox (was %.0f); %s\n", glm::length(a->position - p.position), before, st.c_str());
        // pinned close: the dagger
        bool dagger = false;
        for (int i = 0; i < 60 * 8 && !dagger; i++) {
            glm::vec3 to = a->position - p.position; to.y = 0;
            HumanIntent in; in.move = glm::normalize(glm::vec2(to.x, to.z)); in.sprint = true;
            p.facing = glm::normalize(to);
            sim.setIntent(p.uuid, in);
            if (glm::length(to) < 6.0f) p.position = a->position - glm::normalize(to) * 5.0f;
            tick(1);
            sim.takeEvents();
            dagger = a->weaponItem == "bone_dagger";
        }
        check(dagger, "caught close it pulls its dagger");
        // let it go: back to the bow
        p.position = a->position + glm::vec3(0, 0, -60);
        bool bow = false;
        for (int i = 0; i < 60 * 6 && !bow; i++) { sim.setIntent(p.uuid, HumanIntent()); tick(1); sim.takeEvents(); bow = a->weaponItem == "short_bow"; }
        check(bow, "  with room again, the bow");
    }

    session.close();
    fs::remove_all(std::string("saves/") + NAME, ec);
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
