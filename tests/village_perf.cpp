// Profile (not a test): an island village with its people, a player standing
// in it, through a day: ms per tick (and the worst), path searches, who
// searched most. village_perf [site] [from hour] [to hour]; SPIKES=1 lists
// ticks over 15 ms. Run from the repo root. Under valgrind --tool=callgrind
// for where the time goes
#include "TestWorld.h"
#include "server/GameSession.h"
#include "server/ChunkKeeper.h"
#include "game/Palettes.h"
#include <chrono>
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_START_INSTRUMENTATION
#define CALLGRIND_STOP_INSTRUMENTATION
#endif
#include <map>
using namespace testworld;
int main(int argc, char** argv) {
    loadData(); gStructures.load("data/registry/structures.toml");
    gBlueprints.load(); gBuildPalettes.load(PaletteLibrary::REGISTRY, "/nonexistent"); gVillagers.load();
    std::error_code ec; std::filesystem::remove_all("saves/_perf_island", ec);
    Simulation sim; GameSession s(sim);
    s.beginNew("_perf_island", 12345, GameMode::Survival, WorldType::Island, 72, -1, TerrainStyle::NoErosion, WorldGenParams(), FlatOptions(), false);
    s.buildGenerator(); s.attach();
    int site = argc > 1 ? std::atoi(argv[1]) : 0;
    auto vs = s.villageSites()[(size_t)site];
    glm::vec3 at(vs.at.x + 0.5f, 400.0f, vs.at.y + 0.5f);
    ChunkKeeper keeper(*s.world, s.chunkSource());
    keeper.loadNow({ at });
    s.tickVillages({ at + glm::vec3(300, 0, 0) });
    s.syncSpawners();
    int g = terrain::groundLevel(*s.world, vs.at.x + 20, vs.at.y + 20, 400, 400);
    Player& pl = sim.addPlayer(); pl.setMode(GameMode::Dev); pl.position = glm::vec3(vs.at.x + 20.5f, g + 30.0f, vs.at.y + 20.5f); pl.flying = true;
    sim.roaming.enabled = false;
    gClock.frozen = true;
    std::map<std::string, int> who; std::map<std::string, long> nodes; std::map<std::string, int> fails, stucks;
    sim.pathLog = [&](const Npc& n, const nav::Result& r, bool stuck) { who[n.name]++; nodes[n.name] += r.expanded; fails[n.name] += !r.reached; stucks[n.name] += stuck; };
    {   // one door, opened and shut ten times
        uint32_t door = 0;
        s.structs->forEachInstance([&](const StructureInstance& si) { const StructureType* t = s.structs->typeOf(si); if (!door && t && t->kind == StructureKind::Door) door = si.id; });
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; i++) { s.structs->toggleDoor(*s.structs->get(door), EditSource::Script, nullptr); s.editor->flush(); }
        std::printf("a door toggle: %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10.0);
    }
    float h0 = argc > 2 ? (float)std::atof(argv[2]) : 5.0f, h1 = argc > 3 ? (float)std::atof(argv[3]) : 24.0f;
    CALLGRIND_START_INSTRUMENTATION;
    for (float hour = h0; hour < h1; hour += 1.0f) {
        gClock.time = hour / 24.0f;
        int before = sim.pathsPlanned;
        auto t0 = std::chrono::steady_clock::now();
        double worst = 0;
        for (int i = 0; i < 600; i++) {
            auto a = std::chrono::steady_clock::now();
            s.syncSpawners();
            sim.step(1.0f / 60.0f);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            worst = std::max(worst, ms);
            if (ms > 15.0 && std::getenv("SPIKES")) std::printf("    spike tick %d: %.1f ms, paths so far %d, nodes %ld\n", i, ms, sim.pathsPlanned, sim.pathNodes);
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 600.0;
        int lying = 0, walking = 0; for (auto& n : sim.npcs().all()) { lying += n->lying; walking += n->stateName == "routine" && n->intent.move != glm::vec2(0); }
        std::printf("hour %4.1f: %6.3f ms/tick (worst %6.2f), %3d paths, %zu npcs, %d lying, %d moving\n", hour, ms, worst, sim.pathsPlanned - before,
                    sim.npcs().all().size(), lying, walking);
    }
    CALLGRIND_STOP_INSTRUMENTATION;
    for (auto& [k, v] : who) std::printf("  %-26s %4d plans, %7ld nodes, %3d failed, %3d stuck\n", k.c_str(), v, nodes[k], fails[k], stucks[k]);
    keeper.stop(); s.close();
    std::filesystem::remove_all("saves/_perf_island", ec);
}
