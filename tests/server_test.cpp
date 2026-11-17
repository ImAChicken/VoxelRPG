// Headless checks for the server side (server/GameSession.h,
// SaveStaging.h, ChunkKeeper.h): a world opened with no
// graphics, chunks kept around a point, staged saves that only
// replace the world's files when committed, recovery after a
// crash, and NPC ids that survive a reload. Makes a small flat
// world in saves/_server_test and deletes it. Run from the repo root.
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include <cstdio>
#include <filesystem>

using namespace testworld;
namespace fs = std::filesystem;

static const char* NAME = "_server_test";

static bool openSession(GameSession& s, bool fresh) {
    bool ok;
    if (fresh) {
        FlatOptions flat; flat.plants = false;
        ok = s.beginNew(NAME, 7, GameMode::Survival, WorldType::Flat, 32, -1, TerrainStyle::NoErosion, WorldGenParams(), flat, false);
    } else ok = s.begin(NAME, false);
    if (!ok) return false;
    s.buildGenerator();
    s.attach();
    return true;
}

int main() {
    if (!loadData()) { std::printf("run from the repo root (needs data/registry)\n"); return 1; }
    gStructures.load("data/registry/structures.toml");
    std::error_code ec;
    fs::remove_all(std::string("saves/") + NAME, ec);
    const std::string W = std::string("saves/") + NAME;
    const uint16_t CHEST = gStructures.find("wooden_chest"), STONE = gRegistry.getIDByName("stone");
    const uint16_t PLANKS = gItems.find("planks");

    std::printf("a world with no graphics\n");
    glm::vec3 spawn;
    glm::ivec3 chestAt, edit;
    uint64_t banditId = 0;
    {
        Simulation sim;
        GameSession s(sim);
        check(openSession(s, true) && s.open(), "a new flat world opens headless");
        ChunkKeeper keeper(*s.world, s.chunkSource());
        spawn = glm::vec3(s.gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z));
        keeper.loadNow({ spawn });
        int cx = ChunkManager::chunkCoord((int)spawn.x), cz = ChunkManager::chunkCoord((int)spawn.z);
        check(s.world->isLoaded(cx, cz) && s.world->isLoaded(cx + 2, cz - 2) && !s.world->isLoaded(cx + 4, cz),
              "the keeper loads columns within 2 of the spawn, not further");
        int ground = (int)spawn.y;
        chestAt = glm::ivec3((int)spawn.x + 10, ground, (int)spawn.z);
        StructureInstance* c = s.structs->place(CHEST, chestAt, 0, EditSource::Player, 0);
        s.chests.get(c->id, *gStructures.get(CHEST)).read().add(PLANKS, 33);
        edit = glm::ivec3((int)spawn.x - 6, ground + 3, (int)spawn.z);
        EditBatch b; b.source = EditSource::Command; b.label = "test";
        b.set(edit.x, edit.y, edit.z, STONE);
        s.editor->submit(std::move(b));
        s.editor->flush();
        Npc* n = sim.npcs().spawn("bandit", spawn + glm::vec3(20, 0, 0), glm::vec3(0, 0, 1));
        banditId = n->uuid;
        for (int i = 0; i < 30; i++) { s.syncSpawners(); sim.step(DT); }

        std::printf("checkpoints don't touch the world\n");
        check(s.save(false), "a checkpoint saves");
        check(savestage::hasStaged(W) && fs::exists(savestage::stageDir(W) / "structures.dat"), "into the staging folder");
        check(!fs::exists(fs::path(W) / "structures.dat"), "the world's own files are untouched");
        // the edited chunk is let go, then read back: from staging
        keeper.update({ glm::vec3(0, 0, 0) });
        int ecx = ChunkManager::chunkCoord(edit.x), ecz = ChunkManager::chunkCoord(edit.z);
        check(!s.world->isLoaded(ecx, ecz), "no one near: the saved chunk is let go");
        ChunkPtr back = s.chunkSource()(ecx, ecz);
        check(back && back->typeAt(edit.x - ecx * VOXEL_DIM, edit.y, edit.z - ecz * VOXEL_DIM) == STONE,
              "loaded again, it comes from the checkpoint (not the generator)");
        keeper.stop();
        s.close();
    }

    std::printf("a crash before committing\n");
    {
        Simulation sim;
        GameSession s(sim);
        check(openSession(s, false), "the world opens again");
        check(s.recovered.find("set aside") != std::string::npos, "the uncommitted save is set aside");
        int count = 0;
        s.structs->forEachInstance([&](const StructureInstance&) { count++; });
        check(count == 0 && sim.npcs().all().empty(), "the world is as last committed (nothing)");
        bool aside = false;
        for (auto& e : fs::directory_iterator(W)) aside = aside || e.path().filename().string().rfind(".staging-abandoned-", 0) == 0;
        check(aside && !savestage::hasStaged(W), "kept beside it, for anyone who wants it");
        s.close();
    }

    std::printf("committed\n");
    {
        Simulation sim;
        GameSession s(sim);
        openSession(s, false);
        ChunkKeeper keeper(*s.world, s.chunkSource());
        keeper.loadNow({ spawn });
        StructureInstance* c = s.structs->place(CHEST, chestAt, 0, EditSource::Player, 0);
        s.chests.get(c->id, *gStructures.get(CHEST)).read().add(PLANKS, 33);
        banditId = sim.npcs().spawn("bandit", spawn + glm::vec3(20, 0, 0), glm::vec3(0, 0, 1))->uuid;
        check(s.save(true), "saved and committed");
        check(!savestage::hasStaged(W) && fs::exists(fs::path(W) / "structures.dat"), "the files are in place, staging gone");
        keeper.stop();
        s.close();
    }
    {
        Simulation sim;
        GameSession s(sim);
        // more ids handed out since: a reload must still give the bandit its own
        for (int i = 0; i < 5; i++) sim.npcs().spawn("dummy", glm::vec3(0), glm::vec3(0, 0, 1));
        sim.npcs().clear();
        openSession(s, false);
        int count = 0; uint32_t cid = 0;
        s.structs->forEachInstance([&](const StructureInstance& si) { count++; cid = si.id; });
        Container* box = s.chests.find(cid);
        check(count == 1 && box && box->peek() && box->peek()->count(PLANKS) == 33, "reopened: the chest and what's in it");
        check(sim.npcs().all().size() == 1 && sim.npcs().all()[0]->uuid == banditId, "the bandit, with the same id");
        Npc* fresh = sim.npcs().spawn("dummy", glm::vec3(0), glm::vec3(0, 0, 1));
        check(fresh->uuid > banditId, "and new ids come after it");

        std::printf("a crash in the middle of a commit\n");
        s.chests.get(cid, *gStructures.get(CHEST)).read().add(PLANKS, 10);
        s.save(false);
        std::ofstream(savestage::stageDir(W) / savestage::MARKER) << "complete\n";   // marked, never applied
        s.close();
    }
    {
        Simulation sim;
        GameSession s(sim);
        openSession(s, false);
        check(s.recovered.find("finished") != std::string::npos, "the next open finishes it");
        uint32_t cid = 0;
        s.structs->forEachInstance([&](const StructureInstance& si) { cid = si.id; });
        Container* box = s.chests.find(cid);
        check(box && box->peek() && box->peek()->count(PLANKS) == 43, "with what was saved");
        check(fs::exists(fs::path(W) / ".backup-1" / "containers.dat"), "and the files it replaced are in .backup-1");
        s.close();
    }
    fs::remove_all(W, ec);

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
