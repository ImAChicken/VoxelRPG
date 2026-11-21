// Headless checks for the first boss (docs/dungeons.md step 5): the
// Crypt Lord (npcs.toml crypt_lord, style "boss": npc/States.h
// Combat::bossFight), a giant (Appearance::scale / Human::scale), its
// greatsword (movesets.toml greatsword: sweeps, the slam's shockwave
// Simulation::quake, the charge as a special), and its second phase.
// The arena (bars, coffins, clearing) is in dungeon_test. Makes a flat
// world in saves/_boss_test and deletes it. Run from the repo root.
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include "character/HeldItemModel.h"
#include "character/CharacterModel.h"
#include "character/CharacterAnimator.h"
#include "game/CombatMoves.h"
#include <cstdio>
#include <filesystem>

using namespace testworld;
namespace fs = std::filesystem;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    gHeldModels.loadDir("data/models/held");
    const char* NAME = "_boss_test";
    std::error_code ec;
    fs::remove_all(std::string("saves/") + NAME, ec);

    std::printf("a giant\n");
    {
        const LookPreset* lp = gNpcTypes.look("crypt_lord");
        Appearance a = lp ? lp->make(3) : Appearance();
        check(lp && std::fabs(a.scale - 1.6f) < 0.01f, "the crypt_lord look: 1.6 times a person (look_scale)");
        CharacterModel big = CharacterModel::build(a);
        Appearance small = a; small.scale = 1.0f;
        CharacterModel norm = CharacterModel::build(small);
        Pose p;
        std::array<glm::mat4, BONE_COUNT> mb, mn;
        worldBoneMatrices(big, p, glm::vec3(0.0f), mb);
        worldBoneMatrices(norm, p, glm::vec3(0.0f), mn);
        float hb = glm::vec3(mb[B_HEAD][3]).y, hn = glm::vec3(mn[B_HEAD][3]).y;
        check(std::fabs(hb / hn - 1.6f) < 0.02f, "  drawn and hit 1.6 times as big (head at " + std::to_string(hb).substr(0, 4) + " voxels)");
        const uint16_t GS = gItems.find("crypt_greatsword");
        check(GS && gHeldModels.find(gItems.get(GS).heldModel) && gItems.get(GS).moveset == "greatsword", "  with a greatsword to swing");
        const Moveset* m = gMovesets.find("greatsword");
        check(m && m->special("gs_charge") && m->heavy && m->heavy->effect == "shockwave" && m->heavy->blade,
              "  its moves: sweeps, a slam that hits and sends a shockwave, a charge (a special)");
    }

    Simulation sim;
    GameSession session(sim);
    FlatOptions flat; flat.plants = false;
    session.beginNew(NAME, 5, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
    session.buildGenerator();
    session.attach();
    sim.roaming.enabled = false;
    ChunkKeeper keeper(*session.world, session.chunkSource());
    glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(session.world->sizeX(), session.world->sizeZ()));
    keeper.loadNow({ spawn, spawn + glm::vec3(150, 0, 0), spawn + glm::vec3(-150, 0, 0), spawn + glm::vec3(0, 0, 150), spawn + glm::vec3(0, 0, -150) });
    auto tick = [&](int n) { for (int i = 0; i < n; i++) { sim.step(DT); session.tickWorld(DT); } };

    Player& p = sim.addPlayer();
    p.setMode(GameMode::Survival);
    p.position = spawn;
    p.health = p.maxHealth = 100000.0f;
    tick(20);

    Npc* lord = sim.npcs().spawn("crypt_lord", spawn + glm::vec3(0, 0, 70), glm::vec3(0, 0, -1));
    check(lord && lord->type->boss && std::fabs(lord->scale - 1.6f) < 0.01f && std::fabs(lord->radius() - Human::RADIUS * 1.6f) < 0.01f,
          "the Crypt Lord spawns a giant: wider and taller (" + std::to_string(lord ? lord->height : 0).substr(0, 4) + " voxels)");
    check(lord && lord->mass > p.mass * 3.0f, "  and heavy (" + std::to_string((int)(lord ? lord->mass : 0)) + " kg)");
    tick(60);
    check(lord && lord->grounded && std::fabs(lord->position.y - spawn.y) < 1.5f, "  it stands on the ground");

    std::printf("its moves\n");
    {
        // far off: it charges
        bool charged = false;
        float startD = glm::length(lord->position - p.position);
        for (int i = 0; i < 60 * 10 && !charged; i++) {
            p.position = spawn; p.velocity = glm::vec3(0.0f);
            sim.setIntent(p.uuid, HumanIntent());
            tick(1);
            sim.takeEvents();
            charged = lord->attack.active() && lord->attack.d && lord->attack.d->id == "gs_charge";
        }
        check(charged, "from " + std::to_string((int)startD) + " voxels it charges");
        float before = glm::length(glm::vec2(lord->position.x - p.position.x, lord->position.z - p.position.z));
        bool hitByCharge = false;
        for (int i = 0; i < 90; i++) {
            p.position.x = spawn.x; p.position.z = spawn.z;
            tick(1);
            for (auto& e : sim.takeEvents()) if (e.kind == SimEvent::Kind::Hit && e.a == lord->uuid && e.b == p.uuid) hitByCharge = true;
        }
        float after = glm::length(glm::vec2(lord->position.x - p.position.x, lord->position.z - p.position.z));
        std::printf("    %.0f -> %.0f voxels away\n", before, after);
        check(after < before - 15.0f, "  covering ground fast");
        (void)hitByCharge;
    }

    // the slam's shockwave: drive the slam by hand, the target out of the blade's reach
    auto slamAt = [&](float dist, bool jump) {
        lord->attack = AttackState();
        lord->stamina = lord->maxStamina;
        lord->hitstun = 0.0f; lord->move = Human::Move::None;
        p.attack = AttackState(); p.hitstun = 0.0f; p.react = React::None;
        lord->position = spawn + glm::vec3(0, 0, dist);
        lord->prevPosition = lord->position;
        lord->velocity = glm::vec3(0.0f);
        lord->facing = glm::vec3(0, 0, -1);
        p.position = spawn; p.velocity = glm::vec3(0.0f);
        tick(5);
        sim.takeEvents();
        lord->facing = glm::vec3(0, 0, -1);
        combat::beginAttack(*lord, AttackKind::Heavy);
        bool hit = false, quake = false, jumped = false;
        for (int i = 0; i < 150; i++) {
            lord->intent = HumanIntent();
            HumanIntent in;
            // jump as the front gets near
            if (jump && !jumped && !sim.waves().empty() && sim.waves()[0].radius > dist - 30.0f) { in.jump = true; jumped = true; }
            sim.setIntent(p.uuid, in);
            p.position.x = spawn.x; p.position.z = spawn.z;
            lord->position.x = spawn.x; lord->position.z = spawn.z + dist;
            tick(1);
            for (auto& e : sim.takeEvents()) {
                if (e.kind == SimEvent::Kind::Quake && e.a == lord->uuid) quake = true;
                if (e.kind == SimEvent::Kind::Hit && e.a == lord->uuid && e.b == p.uuid) hit = true;
            }
        }
        return std::make_pair(quake, hit);
    };
    {
        auto [quake, hit] = slamAt(40.0f, false);
        check(quake, "the slam sends a shockwave along the ground");
        check(hit, "  it reaches you 40 voxels off, standing");
        auto [q2, hit2] = slamAt(40.0f, true);
        check(q2 && !hit2, "  jump it and it passes under you");
        auto [q3, hit3] = slamAt(80.0f, false);
        check(q3 && !hit3, "  it dies out before 80 voxels");
    }

    std::printf("its second phase\n");
    {
        lord->attack = AttackState();
        lord->position = spawn + glm::vec3(0, 0, 20);
        lord->health = lord->maxHealth * 0.45f;
        sim.setIntent(p.uuid, HumanIntent());
        tick(10);
        check(lord->phaseTwo && lord->attackSpeed > 1.1f, "below half its health: its second phase, faster (" + std::to_string(lord->attackSpeed).substr(0, 4) + "x)");
        // it fights: swings land on a player standing close
        int hits = 0;
        for (int i = 0; i < 60 * 8; i++) {
            p.position = spawn; p.velocity = glm::vec3(0.0f);
            sim.setIntent(p.uuid, HumanIntent());
            tick(1);
            for (auto& e : sim.takeEvents()) if (e.kind == SimEvent::Kind::Hit && e.a == lord->uuid && e.b == p.uuid) hits++;
        }
        check(hits >= 2, "  and it keeps on swinging (" + std::to_string(hits) + " hits in 8 s)");
    }

    session.close();
    fs::remove_all(std::string("saves/") + NAME, ec);
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
