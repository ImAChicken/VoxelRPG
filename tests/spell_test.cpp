// Headless checks for spellbooks (items.toml category "spellbook")
// and the ice spike (movesets.toml ice_spike_cast): a book is a
// weapon type that goes in the combat slots and is held while it
// casts; the ice spike flies (Simulation::launch / flyProjectiles),
// hurts the first body it meets, and where it meets the ground the
// world sets down ice (GameSession::tickWorld, structures.toml
// ice_spike, kind "effect") that melts after its 15 s lifetime
// (StructureManager::tick). Makes a flat world in saves/_spell_test
// and deletes it. Run from the repo root.
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include "game/CombatKit.h"
#include "character/HeldItemModel.h"
#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace testworld;
namespace fs = std::filesystem;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    gHeldModels.loadDir("data/models/held");
    const char* NAME = "_spell_test";
    std::error_code ec;
    fs::remove_all(std::string("saves/") + NAME, ec);

    const uint16_t WIND = gItems.find("windburst"), ICE = gItems.find("ice_spike"), SWORD = gItems.find("iron_sword");
    const uint16_t ICE_BLOCK = gRegistry.getIDByName("ice");
    const uint16_t ICE_TYPE = gStructures.find("ice_spike");

    std::printf("spellbooks\n");
    {
        const ItemDef& w = gItems.get(WIND);
        const ItemDef& i = gItems.get(ICE);
        check(w.is(IC_SPELLBOOK) && i.is(IC_SPELLBOOK) && w.cast == "windburst_cast" && i.cast == "ice_spike_cast",
              "Windburst and Ice Spike are spellbooks, each casting its spell");
        check(gHeldModels.find(w.heldModel) && gHeldModels.find(i.heldModel), "  each with a book to hold (data/models/held)");
        check(CombatKit::fitsButton(ICE) && CombatKit::fitsOff(ICE) && !CombatKit::fitsCharm(ICE),
              "  they go in the X / Y / B slots and the off hand");
        Inventory inv;
        inv.add(SWORD, 1); inv.add(WIND, 1); inv.add(ICE, 1);
        inv.equipFrom(inv.find(SWORD), EquipSlot::Weapon1);
        CombatKit k;
        k.tidy(inv);
        check(k.slotItem(0) == WIND && k.sets[0].slots[0][1] == ICE, "  a new kit puts both in X (4 steps between them)");
        check(ICE_BLOCK && ICE_TYPE && gStructures.get(ICE_TYPE)->kind == StructureKind::Effect &&
              std::fabs(gStructures.get(ICE_TYPE)->lifetime - 15.0f) < 0.01f, "the ice it leaves: a structure with a 15 s lifetime");
    }

    Simulation sim;
    GameSession session(sim);
    FlatOptions flat; flat.plants = false;
    session.beginNew(NAME, 5, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
    session.buildGenerator();
    session.attach();
    ChunkKeeper keeper(*session.world, session.chunkSource());
    glm::vec3 spawn = glm::vec3(session.gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z));
    keeper.loadNow({ spawn, spawn + glm::vec3(120, 0, 0), spawn + glm::vec3(-120, 0, 0), spawn + glm::vec3(0, 0, 120) });
    auto tick = [&](int n) { for (int i = 0; i < n; i++) { sim.step(DT); session.tickWorld(DT); session.updateEdits(50.0f); } };
    auto countIce = [&] { int n = 0; session.structs->forEachInstance([&](const StructureInstance& s) { n += s.type == ICE_TYPE; }); return n; };

    Player& p = sim.addPlayer();
    p.setMode(GameMode::Survival);
    p.position = spawn;
    p.inventory.add(ICE, 1);
    p.inventory.add(WIND, 1);
    tick(30);

    std::printf("casting\n");
    p.facing = glm::vec3(1, 0, 0);
    {
        HumanIntent in; in.useItem = ICE;
        sim.setIntent(p.uuid, in);
        sim.step(DT);
        std::string held; bool combat = false;
        Simulation::handsOf(p, held, combat);
        check(p.attack.active() && p.attack.kind == AttackKind::Cast && held == "spellbook_ice",
              "using it plays the cast, the book in hand (\"" + held + "\")");
        bool flew = false;
        for (int i = 0; i < 40 && !flew; i++) { sim.step(DT); flew = !sim.projectiles().empty(); }
        check(flew, "  and an ice spike leaves her");
    }

    std::printf("missing: ice on the ground\n");
    glm::vec3 landed(0.0f);
    {
        int before = countIce();
        bool down = false;
        for (int i = 0; i < 240 && !down; i++) { tick(1); down = countIce() > before; }
        check(down, "it falls to the ground and leaves ice there");
        glm::ivec3 o(0);
        session.structs->forEachInstance([&](const StructureInstance& s) { if (s.type == ICE_TYPE) o = s.origin; });
        landed = glm::vec3(o);
        float dist = glm::length(glm::vec2(o.x - spawn.x, o.z - spawn.z));
        std::printf("    landed %.0f voxels away\n", dist);
        check(dist > 20.0f && dist < 90.0f && std::abs(o.z - (int)spawn.z) < 3, "  ahead of her, along her facing");
        check(session.world->typeAtVoxel(o.x, o.y, o.z) == ICE_BLOCK, "  ice voxels where it stands");
        // saved with its age; the text sent to clients leaves it out
        std::ostringstream withAge, sent;
        session.structs->write(withAge);
        session.structs->write(sent, false);
        StructureManager back(*session.world, *session.editor, *session.gen);
        std::istringstream in(withAge.str());
        back.read(in);
        float age = -1.0f;
        back.forEachInstance([&](const StructureInstance& s) { if (s.type == ICE_TYPE) age = s.age; });
        check(age > 0.0f && withAge.str() != sent.str(), "  its age is saved (" + std::to_string(age).substr(0, 4) + " s), not sent");
        // melts
        tick(60 * 13);
        check(countIce() == before + 1, "  still there at 13 s");
        tick(60 * 3);
        check(countIce() == before && session.world->typeAtVoxel(o.x, o.y, o.z) == BLOCK_AIR, "  melted by 16 s: gone, air again");
    }

    std::printf("hitting someone\n");
    {
        p.attack = AttackState();
        p.stamina = p.maxStamina;
        Npc* b = sim.npcs().spawn("dummy", p.position + glm::vec3(0, 0, 30), glm::vec3(0, 0, -1));   // stands there
        p.facing = glm::normalize(glm::vec3(0.12f, 0, 1));  // a little off: the aim finds it
        tick(5);
        float hp = b->health;
        int ice = countIce();
        HumanIntent in; in.useItem = ICE;
        sim.setIntent(p.uuid, in);
        bool hit = false;
        bool cast = false;
        for (int i = 0; i < 120 && !hit; i++) {
            tick(1);
            for (const SimEvent& e : sim.takeEvents()) {
                if (e.kind == SimEvent::Kind::Hit && e.a == p.uuid && e.b == b->uuid && e.attack == AttackKind::Cast) cast = true;
            }
            hit = b->health < hp;
        }
        check(hit && cast && sim.projectileHits > 0, "it hits a training dummy a little off her line (" + std::to_string((int)(hp - b->health)) + " damage)");
        tick(60);
        check(countIce() == ice, "  and leaves no ice: it stopped at the dummy");
    }

    std::printf("never over a body\n");
    {
        Simulation::Landing l;
        l.owner = p.uuid; l.place = "ice_spike";
        l.at = p.position + glm::vec3(1.0f, 1.0f, 0.0f);
        int ice = countIce();
        bool put = session.placeEffect(ICE_TYPE, *gStructures.get(ICE_TYPE), l);
        check(!put && countIce() == ice, "ice that would shut her in isn't set down");
        l.at = p.position + glm::vec3(40.0f, 1.0f, 0.0f);
        check(session.placeEffect(ICE_TYPE, *gStructures.get(ICE_TYPE), l), "  a few steps away it is");
    }

    session.close();
    fs::remove_all(std::string("saves/") + NAME, ec);
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
