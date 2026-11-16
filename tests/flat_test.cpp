// Headless checks for flat worlds (worldgen/FlatGenerator.h):
// a level field of the chosen block, and trees, plants and decor
// each only when switched on. Run from the repo root.
#include "TestWorld.h"
#include "worldgen/WorldType.h"
#include "world/WorldSaveManager.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <sstream>

using namespace testworld;

static std::unique_ptr<TerrainGenerator> flatGen(const FlatOptions& o) {
    return makeGenerator(WorldType::Flat, 7, CLASSIC_WORLD_SIZE, -1, TerrainStyle::GuidedRivers, "", WorldGenParams(), o);
}
static bool isWood(uint16_t t) { return t == BLOCK_LOG_Y || t == BLOCK_LOG_X || t == BLOCK_LOG_Z; }

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    decor::current() = decor::Loader::load(gRegistry, "data/registry/decorations.toml");

    std::printf("a bare field\n");
    {
        FlatOptions o; o.ground = "sand"; o.trees = o.plants = o.decor = false;
        auto g = flatGen(o);
        check(g->getSurfaceY(100, 900) == 40 && g->getSurfaceY(3000, 10) == 40, "level everywhere");
        auto ch = g->generateChunk(3, 4);
        int sand = 0, other = 0;
        for (int x = 0; x < 128; x++)
            for (int z = 0; z < 128; z++) {
                if (ch->typeAt(x, 39, z) == BLOCK_SAND) sand++; else other++;
                for (int y = 40; y < 200; y += 7) if (ch->typeAt(x, y, z) != BLOCK_AIR) other++;
            }
        check(sand == 128 * 128 && other == 0, "a sand field with nothing on it");
        check(ch->typeAt(5, 36, 5) == BLOCK_SAND && ch->typeAt(5, 20, 5) == BLOCK_STONE, "sand a few deep, stone under it");
        glm::ivec3 sp = g->spawnPoint(CLASSIC_WORLD_SIZE, CLASSIC_WORLD_SIZE);
        check(sp.y == 40 && sp.x == CLASSIC_WORLD_SIZE * 64, "spawn in the middle, on the field");
        check(g->forestCover(500, 500) == 0.0f, "no forest for the far view");
    }

    std::printf("what grows\n");
    {
        FlatOptions all; all.ground = "grass"; all.trees = all.plants = all.decor = true;
        auto g = flatGen(all);
        check(g->column(10, 10).top == BLOCK_GRASS && g->column(10, 10).sub == BLOCK_DIRT, "grass over dirt");
        auto count = [](TerrainGenerator& gen, int& plants, int& wood, int& decorPieces) {
            plants = wood = decorPieces = 0;
            for (int cx = 4; cx < 8; cx++) {
                auto c = gen.generateChunk(cx, 5);
                for (int x = 0; x < 128; x++)
                    for (int z = 0; z < 128; z++) {
                        uint16_t t = c->typeAt(x, 40, z);
                        if (t != BLOCK_AIR && !isWood(t)) plants++;
                        for (int y = 40; y < 80; y++) if (isWood(c->typeAt(x, y, z))) wood++;
                    }
            }
        };
        int plants, wood, d;
        count(*g, plants, wood, d);
        std::printf("  ..  all on: %d things on the ground, %d wood voxels\n", plants, wood);
        check(plants > 500 && wood > 100, "trees and plants grow when on");
        FlatOptions treesOnly = all; treesOnly.plants = treesOnly.decor = false;
        auto g2 = flatGen(treesOnly);
        int p2, w2, d2;
        count(*g2, p2, w2, d2);
        check(w2 > 100 && p2 == 0, "trees only: trees and nothing else");
        FlatOptions plantsOnly = all; plantsOnly.trees = plantsOnly.decor = false;
        auto g3 = flatGen(plantsOnly);
        int p3, w3, d3;
        count(*g3, p3, w3, d3);
        check(p3 > 500 && w3 == 0, "plants only: no trees");
    }

    std::printf("saving\n");
    {
        FlatOptions o; o.ground = "gravel"; o.trees = true; o.plants = false; o.decor = true; o.biome = island::Forest;
        std::ostringstream out;
        o.write(out);
        FlatOptions back;
        std::istringstream in(out.str());
        std::string line;
        while (std::getline(in, line)) { size_t e = line.find('='); back.read(line.substr(0, e), line.substr(e + 1)); }
        check(back.ground == "gravel" && back.trees && !back.plants && back.decor && back.biome == island::Forest,
              "the options save and load");
        check(worldTypeFromKey(worldTypeKey(WorldType::Flat)) == WorldType::Flat &&
              worldSizeFor(WorldType::Flat, 128) == CLASSIC_WORLD_SIZE, "a flat world is the Classic size");
    }

    std::printf("dev worlds (world/WorldSaveManager.h)\n");
    {
        namespace fs = std::filesystem;
        fs::path home = fs::current_path();
        fs::path tmp = fs::temp_directory_path() / "voxelrpg_devworld_test";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        fs::current_path(tmp);                 // saves/ and devworlds/ in here, not the repo
        WorldSaveManager m;
        FlatOptions o; o.ground = "stone";
        check(m.createWorld("Arena", 5, GameMode::Dev, WorldType::Flat, 32, -1, TerrainStyle::GuidedRivers,
                            WorldGenParams(), o, true) && m.isDevWorld() && m.getWorldPath() == "devworlds/Arena",
              "a new dev world goes in devworlds/");
        { std::ofstream f("devworlds/Arena/structures.dat"); f << "structures 1\ninst 1 training_post 10 40 10 0 0 0 0 0 0 0\n"; }
        fs::create_directories("devworlds/not a world");      // no world.vxrp: not listed
        check(WorldSaveManager::list(true) == std::vector<std::string>{ "Arena" }, "listed with the dev worlds");
        check(WorldSaveManager::list(false).empty(), "not with the ordinary ones");
        std::string why;
        check(WorldSaveManager::copyWorld(true, "Arena", false, "Copy", why), "copied into a new world");
        check(fs::exists("saves/Copy/structures.dat"), "with everything in it");
        WorldSaveManager m2;
        check(m2.loadWorld("Copy", false) && m2.getName() == "Copy" && !m2.isDevWorld() &&
              m2.getWorldType() == WorldType::Flat && m2.getFlatOptions().ground == "stone", "under its new name, as an ordinary world");
        check(!WorldSaveManager::copyWorld(true, "Arena", false, "Copy", why), "never over a world that's there");
        check(WorldSaveManager::uniqueName(false, "Copy") == "Copy 2", "a free name is found");
        WorldSaveManager m3;
        check(m3.loadWorld("Arena", true) && m3.isDevWorld(), "the dev world still opens as one");
        fs::current_path(home);
        fs::remove_all(tmp);
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
