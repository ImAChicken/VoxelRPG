// Headless checks for several worlds open at once (server/Worlds.h,
// docs/dungeons.md step 7): each world its own size, settings, clock and
// simulation; all running together; a player moving from one to
// another; a world nobody's in going to sleep and closing; the realm's
// main world pinned open. Makes saves/_worlds_a and _worlds_b and
// deletes them. Run from the repo root.
#include "TestWorld.h"
#include "server/Worlds.h"
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace testworld;
namespace fs = std::filesystem;

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    std::error_code ec;
    fs::remove_all("saves/_worlds_a", ec);
    fs::remove_all("saves/_worlds_b", ec);

    Worlds worlds;
    std::printf("two worlds at once\n");
    FlatOptions flat; flat.plants = false;
    GameSession* a = worlds.create("_worlds_a", 5, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
    a->info.setRoamingSpawns(false);
    a->buildGenerator(); a->attach();
    GameSession* b = worlds.create("_worlds_b", 9, GameMode::Spectator, WorldType::Dungeon, 0, -1, TerrainStyle::NoErosion, WorldGenParams(), FlatOptions(), false);
    b->buildGenerator(); b->attach();
    check(a && b && worlds.count() == 2, "a flat world and a crypt, both open");
    check(a->world->sizeX() == 32 && b->world->sizeX() == 16, "  each its own size (" + std::to_string(a->world->sizeX()) + " and " + std::to_string(b->world->sizeX()) + " chunks)");
    check(a->info.getWorldType() == WorldType::Flat && b->info.getWorldType() == WorldType::Dungeon &&
          a->info.getDefaultMode() == GameMode::Survival && b->info.getDefaultMode() == GameMode::Spectator,
          "  each its own settings (type, mode)");
    check(b->dungeon() != nullptr && a->dungeon() == nullptr, "  the crypt has its markers running, the flat world none");
    a->sim.day.time = 0.25f; b->sim.day.time = 0.75f;
    a->sim.day.frozen = true;
    worlds.tick(1.0f, 60, DT);
    check(std::fabs(a->sim.day.time - 0.25f) < 1e-4f && b->sim.day.time > 0.75f, "  each its own clock (one frozen, one running)");

    std::printf("both running\n");
    {
        glm::vec3 sa = glm::vec3(a->gen->spawnPoint(a->world->sizeX(), a->world->sizeZ()));
        glm::vec3 sb = glm::vec3(b->gen->spawnPoint(b->world->sizeX(), b->world->sizeZ()));
        Player& p = a->sim.addPlayer();
        p.setMode(GameMode::Survival);
        p.position = sa + glm::vec3(0, 1, 0);
        worlds.watch(b->name(), sb);
        Npc* n = b->sim.npcs().spawn("bandit", sb + glm::vec3(10, 0, 0), glm::vec3(1, 0, 0));
        glm::vec3 start = n ? n->position : glm::vec3(0.0f);
        // (the keepers load on worker threads: real time passes)
        for (int i = 0; i < 60 * 4; i++) { worlds.watch(b->name(), sb); worlds.tick(DT, 1, DT); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
        check(a->world->isColumnLoaded((int)sa.x, (int)sa.z) && b->world->isColumnLoaded((int)sb.x, (int)sb.z),
              "the ground round a player (in one) and a watched point (in the other) loads in both");
        check(n && p.grounded && std::fabs(p.position.y - sa.y) < 2.0f, "a player stands on the ground in one");
        check(n && !n->grounded && glm::length(n->position - start) < 0.01f, "  an NPC in the other waits, with no player near it (NPCs sleep far from players)");

        std::printf("moving between them\n");
        EntityId id = p.uuid;
        Player* moved = Worlds::move(id, *a, *b);
        check(moved == &p && a->sim.players().empty() && b->sim.players().size() == 1, "a player moves from one world's simulation to the other's (the same body)");
        p.position = sb + glm::vec3(0, 1, 0);
        p.velocity = glm::vec3(0.0f);
        for (int i = 0; i < 60 * 2; i++) { worlds.tick(DT, 1, DT); std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
        check(p.grounded && std::fabs(p.position.y - sb.y) < 2.0f, "  and stands in the crypt");
        check(n && n->grounded, "  where the NPC near it wakes and lands, in the background world (not the game's focus)");
        Worlds::move(id, *b, worlds.lobby());
        check(worlds.lobby().sim.players().size() == 1, "  the lobby holds it with no world open");
    }

    std::printf("sleeping\n");
    {
        worlds.pin(a);
        for (int i = 0; i < 65; i++) worlds.tick(1.0f, 1, DT);
        check(worlds.get("_worlds_a") && !worlds.get("_worlds_b"), "after a minute with nobody there the crypt closes; the pinned world stays");
        check(fs::exists("saves/_worlds_b/npcs.dat"), "  saved as it closed");
        GameSession* again = worlds.open("_worlds_b", false);
        check(again && again->dungeon() && again->sim.npcs().all().size() >= 1, "  open it again: as it was (its NPCs back)");
        worlds.setFocus(again);
        for (int i = 0; i < 65; i++) worlds.tick(1.0f, 1, DT);
        check(worlds.get("_worlds_b"), "  the world the game shows (the focus) never sleeps");
    }
    worlds.closeAll(false);
    check(worlds.count() == 0, "all closed");

    fs::remove_all("saves/_worlds_a", ec);
    fs::remove_all("saves/_worlds_b", ec);
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
