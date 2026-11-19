// Villagers (game/Villagers.h, npc/Routine.h): a village built on
// a flat test world gets its people; through a day they walk to
// work through their doors, go out at midday, home in the evening
// and lie down in their beds at night; doors shut behind them;
// a hurt sleeper wakes and runs; they save and load with their
// places.
//
// Run from the repo root.
#include "TestWorld.h"
#include "game/Blueprint.h"
#include "game/Palettes.h"
#include "game/Village.h"
#include "game/Villagers.h"
#include "world/StructureManager.h"
#include "world/WorldEditor.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace testworld;

static float flat(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

int main() {
    if (!loadData() || !gStructures.load("data/registry/structures.toml") || !gVillagers.load()) {
        std::printf("run from the repo root (needs data/registry)\n");
        return 1;
    }
    gBuildPalettes.load(PaletteLibrary::REGISTRY, "/nonexistent");
    BlueprintLibrary lib;
    lib.load("data/blueprints");

    World w;
    WorldEditor ed(w.chunks);
    TerrainGenerator gen(1);
    StructureManager sm(w.chunks, ed, gen);
    w.sim.doorHook = [&](uint32_t id, bool open, const std::function<bool(glm::ivec3)>& bodyBlocks) {
        StructureInstance* d = sm.get(id);
        if (!d) return false;
        if ((d->state != 0) == open) return true;
        return sm.toggleDoor(*d, EditSource::Script, bodyBlocks).empty();
    };
    gClock.frozen = true;
    gClock.time = 7.0f / 24.0f;

    std::printf("a village and its people\n");
    village::Options o;
    o.seed = std::getenv("SEED") ? (uint32_t)std::atoi(std::getenv("SEED")) : 11u;   // SEED=n: try another village
    o.roads = 3; o.lanes = 0; o.roadLen = 90;
    glm::ivec3 centre(256, GROUND, 256);
    village::Plan p = village::plan(w.chunks, lib, centre, o);
    village::Built b = village::build(p, sm, ed, nullptr, o, EditSource::Command);
    check(b.buildings >= 5 && b.failed == 0, "the village goes up: " + std::to_string(b.buildings) + " buildings");
    villagers::Populated pop = villagers::populate(p, b, sm, w.chunks, w.sim.npcs(), o.seed);
    int beds = 0;
    sm.forEachInstance([&](const StructureInstance& s) {
        const StructureType* t = sm.typeOf(s);
        if (t && t->hasTag("bed")) beds += t->size.x >= 11 ? 2 : 1;   // a double bed (13 wide) sleeps two
    });
    check(pop.villagers > 0 && pop.villagers == beds, "a villager for every place in a bed: " + std::to_string(pop.villagers) +
          " in " + std::to_string(pop.homes) + " homes");
    check(pop.jobs >= 1, "some have jobs: " + std::to_string(pop.jobs));
    std::vector<Npc*> folk;
    for (auto& n : w.sim.npcs().all()) folk.push_back(n.get());
    bool named = true, placed = true;
    for (Npc* n : folk) {
        named = named && !n->name.empty() && n->name != "Villager";
        placed = placed && n->life.on && n->life.bed.valid && n->life.bed.door && n->life.bed.lie && n->life.work.valid;
    }
    check(named, "each has a name (" + folk[0]->name + ", ...)");
    check(placed, "each has a bed behind a door, and somewhere to work");
    for (Npc* n : folk) std::printf("    %-24s %s\n", n->name.c_str(), n->life.job.empty() ? "(keeps house)" : n->life.job.c_str());

    if (const char* dump = std::getenv("VOXELRPG_DUMP")) {
        std::FILE* f = std::fopen((std::string(dump) + "/villagers_top.txt").c_str(), "w");
        for (int z = centre.z - 200; z < centre.z + 200; z++)
            for (int x = centre.x - 200; x < centre.x + 200; x++) {
                int g = terrain::groundLevel(w.chunks, x, z, GROUND + 100, 120);
                if (g >= 0) std::fprintf(f, "%d %d %d %d\n", x, g, z, w.chunks.typeAtVoxel(x, g - 1, z));
            }
        for (Npc* n : folk) {
            const VillageLife& L = n->life;
            for (const LifePlace* lp : { &L.bed, &L.work })
                std::fprintf(f, "P %s %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n", n->name.c_str(), lp->out.x, lp->out.z, lp->in.x, lp->in.z,
                             lp->spot.x, lp->spot.z, lp->spot.y, n->position.x, n->position.z);
        }
        std::fclose(f);
    }
    if (std::getenv("VERBOSE"))
        for (Npc* n : folk) {
            nav::Pathfinder pf(w.physics);
            nav::Result r = pf.find(n->life.bed.in, n->life.bed.spot);
            std::printf("    path %s: bed (%.0f %.0f %.0f) -> in (%.0f %.0f %.0f): %s, %zu points, %d expanded\n", n->name.c_str(),
                        n->life.bed.spot.x, n->life.bed.spot.y, n->life.bed.spot.z, n->life.bed.in.x, n->life.bed.in.y, n->life.bed.in.z,
                        r.reached ? "reached" : "NOT reached", r.points.size(), r.expanded);
            if (!r.points.empty()) { auto q = r.points.back(); std::printf("      ends at (%.0f %.0f %.0f)\n", q.x, q.y, q.z); }
            if (n->life.work.valid)
                std::printf("    %s work door: out (%.0f %.0f %.0f) in (%.0f %.0f %.0f)\n", n->name.c_str(), n->life.work.out.x, n->life.work.out.y, n->life.work.out.z,
                            n->life.work.in.x, n->life.work.in.y, n->life.work.in.z);
            if (n->life.work.valid) {
                nav::Result r2 = pf.find(n->life.work.in, n->life.work.spot);
                std::printf("    path %s: work in -> spot (%.0f %.0f %.0f): %s, %zu points\n", n->name.c_str(), n->life.work.spot.x, n->life.work.spot.y, n->life.work.spot.z,
                            r2.reached ? "reached" : "NOT reached", r2.points.size());
                for (auto q : r2.points) std::printf("      (%.0f %.0f %.0f)\n", q.x, q.y, q.z);
            }
        }
    if (std::getenv("PF")) {
        float v[6];
        std::sscanf(std::getenv("PF"), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
        nav::Pathfinder pf(w.physics);
        nav::Result r = pf.find({ v[0], v[1], v[2] }, { v[3], v[4], v[5] });
        std::printf("    PF %s, %d expanded\n", r.reached ? "reached" : "NOT reached", r.expanded);
        for (auto q : r.points) std::printf("      (%.0f %.0f %.0f)\n", q.x, q.y, q.z);
    }
    if (std::getenv("PIECES")) {
        int x0, z0, x1, z1;
        std::sscanf(std::getenv("PIECES"), "%d,%d,%d,%d", &x0, &z0, &x1, &z1);
        sm.forEachInstance([&](const StructureInstance& s) {
            glm::ivec3 lo, hi;
            sm.bounds(s, lo, hi);
            if (hi.x < x0 || lo.x > x1 || hi.z < z0 || lo.z > z1) return;
            const StructureType* t = sm.typeOf(s);
            std::printf("    %-22s (%d %d %d)-(%d %d %d) rot %d\n", t ? t->displayName.c_str() : "?", lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, (int)s.rot);
        });
    }
    if (std::getenv("TOPS")) {
        // highest solid under y 75 above the floor (46): one hex digit = (top-45)/2
        int tx = std::atoi(std::getenv("TOPS")), tz = std::getenv("TOPZ") ? std::atoi(std::getenv("TOPZ")) : 180;
        if (tx < 10) tx = 216;
        for (int z = tz; z < tz + 42; z++) {
            std::string row = "    " + std::to_string(z) + " ";
            for (int x = tx; x < tx + 46; x++) {
                int top = 0;
                for (int y = 75; y >= 30; y--) { uint16_t id = w.chunks.typeAtVoxel(x, y, z); if (id && gRegistry.get(id).isSolid()) { top = y; break; } }
                row += top ? "0123456789abcdefghijklmnopqrstuvwxyz"[std::clamp((top - 30) / 2, 0, 35)] : '.';
            }
            std::printf("%s\n", row.c_str());
        }
    }
    if (std::getenv("SLICE")) {
        // x-y slices: for z in a range, print solid columns x 220..245, y 44..84
        int z0 = std::atoi(std::getenv("SLICE"));
        for (int z = z0; z < z0 + 3; z++) {
            std::printf("    slice z %d (x 218..245 across, y 84 down to 44)\n", z);
            for (int y = 84; y >= 44; y--) {
                std::string row = "    " + std::to_string(y) + " ";
                for (int x = 218; x < 246; x++) {
                    uint16_t id = w.chunks.typeAtVoxel(x, y, z);
                    row += (id && gRegistry.get(id).isSolid()) ? '#' : '.';
                }
                std::printf("%s\n", row.c_str());
            }
        }
    }
    if (std::getenv("NAVMAP")) {
        nav::Pathfinder pf(w.physics);
        int ny = std::getenv("NAVY") ? std::atoi(std::getenv("NAVY")) : 46;
        for (int y0 : { ny }) {
            std::printf("    stand map from y %d (x 222..262, z 186..226, every voxel; # = solid at floor+1): digit = height above %d / 4, . none\n", y0, y0 - 12);
            int nx = std::getenv("NAVX") ? std::atoi(std::getenv("NAVX")) : 222, nz = std::getenv("NAVZ") ? std::atoi(std::getenv("NAVZ")) : 186;
            for (int z = nz; z < nz + 40; z++) {
                std::string row = "    ";
                for (int x = nx; x < nx + 40; x++) {
                    if (w.chunks.typeAtVoxel(x, y0 + 1, z) && gRegistry.get(w.chunks.typeAtVoxel(x, y0 + 1, z)).isSolid()) { row += '#'; continue; }
                    int y;
                    if (pf.standAt(x, z, y0, y)) { int k = (y - (y0 - 12)) / 4; row += (char)('0' + std::clamp(k, 0, 9)); }
                    else row += '.';
                }
                std::printf("%s\n", row.c_str());
            }
        }
    }
    // every door opens and shuts
    {
        int doors = 0, swing = 0;
        std::string why;
        sm.forEachInstance([&](const StructureInstance& s) {
            const StructureType* t = sm.typeOf(s);
            if (!t || t->kind != StructureKind::Door) return;
            doors++;
            StructureInstance* d = sm.get(s.id);
            std::string a = sm.toggleDoor(*d, EditSource::Script, nullptr);
            std::string b2 = a.empty() ? sm.toggleDoor(*d, EditSource::Script, nullptr) : a;
            if (a.empty() && b2.empty()) swing++;
            else if (why.empty()) why = t->displayName + ": " + (a.empty() ? b2 : a);
        });
        ed.flush();
        check(doors > 0 && swing == doors, "every door opens and shuts: " + std::to_string(swing) + " of " + std::to_string(doors) +
              (why.empty() ? std::string() : " (" + why + ")"));
    }

    // someone to make them live (NPCs far from every player are frozen)
    Player& pl = w.player(glm::vec3(centre.x + 0.5f, GROUND + 20.0f, centre.z + 0.5f), GameMode::Dev);
    pl.flying = true;

    std::printf("morning: to work\n");
    gClock.time = 9.5f / 24.0f;
    std::vector<Npc*> workers;
    for (Npc* n : folk) if (!n->life.job.empty()) workers.push_back(n);
    if (std::getenv("VERBOSE"))
        for (int k = 0; k < 30; k++) {
            w.run(5.0f);
            for (Npc* n : workers) {
                const StructureInstance* d = sm.get(n->life.work.door);
                std::printf("    t%3d %-16s (%.0f %.0f %.0f) %s  door %s  path %zu/%zu\n", k * 5, n->name.c_str(), n->position.x, n->position.y, n->position.z,
                            n->state ? n->state->debug(*n).c_str() : "-", d ? (d->state ? "open" : "shut") : "-", n->pathAt, n->path.size());
            }
        }
    bool atWork = w.until(150.0f, [&] {
        for (Npc* n : workers) if (flat(n->position, n->life.work.spot) > 6.0f) return false;
        return true;
    });
    int there = 0;
    for (Npc* n : workers) there += flat(n->position, n->life.work.spot) <= 6.0f;
    check(atWork, "everyone with a job gets to work: " + std::to_string(there) + " of " + std::to_string(workers.size()));
    for (Npc* n : workers)
        if (flat(n->position, n->life.work.spot) > 6.0f)
        {
            const StructureInstance* d = sm.get(n->life.work.door);
            std::printf("    %s is %.0f from work (%s); at (%.0f %.0f %.0f), out (%.0f %.0f %.0f) in (%.0f %.0f %.0f) spot (%.0f %.0f %.0f), door %s\n",
                        n->name.c_str(), flat(n->position, n->life.work.spot), n->state ? n->state->debug(*n).c_str() : "-",
                        n->position.x, n->position.y, n->position.z, n->life.work.out.x, n->life.work.out.y, n->life.work.out.z,
                        n->life.work.in.x, n->life.work.in.y, n->life.work.in.z, n->life.work.spot.x, n->life.work.spot.y, n->life.work.spot.z,
                        d ? (d->state ? "open" : "shut") : "none");
        }
    // they went through doors and shut them
    w.run(4.0f);
    int doorsOpen = 0, doors = 0;
    sm.forEachInstance([&](const StructureInstance& s) {
        const StructureType* t = sm.typeOf(s);
        if (t && t->kind == StructureKind::Door) { doors++; doorsOpen += s.state != 0; }
    });
    check(doors > 0 && doorsOpen <= doors / 3, "doors shut behind them: " + std::to_string(doorsOpen) + " of " + std::to_string(doors) + " open");

    std::printf("night: in bed\n");
    gClock.time = 23.0f / 24.0f;
    if (std::getenv("VERBOSE"))
        for (int k = 0; k < 44; k++) {
            w.run(5.0f);
            for (Npc* n : folk) {
                const StructureInstance* d = sm.get(n->life.bed.door);
                std::printf("    t%3d %-16s (%.0f %.0f %.0f) %s  door %s  path %zu/%zu\n", k * 5, n->name.c_str(), n->position.x, n->position.y, n->position.z,
                            n->state ? n->state->debug(*n).c_str() : "-", d ? (d->state ? "open" : "shut") : "-", n->pathAt, n->path.size());
            }
        }
    bool abed = w.until(220.0f, [&] {
        for (Npc* n : folk) if (!n->lying) return false;
        return true;
    });
    int lying = 0, inTheirBed = 0;
    for (Npc* n : folk) {
        lying += n->lying;
        inTheirBed += n->lying && glm::length(n->position - n->life.bed.lieAt) < 0.5f;
    }
    check(abed, "everyone lies down: " + std::to_string(lying) + " of " + std::to_string(folk.size()));
    check(inTheirBed == lying, "each in their own bed");
    for (Npc* n : folk)
        if (!n->lying)
            std::printf("    %s is %.0f from bed (%s)\n", n->name.c_str(), flat(n->position, n->life.bed.spot),
                        n->state ? n->state->debug(*n).c_str() : "-");
    w.run(4.0f);
    doorsOpen = 0;
    sm.forEachInstance([&](const StructureInstance& s) {
        const StructureType* t = sm.typeOf(s);
        if (t && t->kind == StructureKind::Door) {
            doorsOpen += s.state != 0;
            if (s.state != 0) std::printf("    open: %s at (%d %d %d)\n", t->displayName.c_str(), s.origin.x, s.origin.y, s.origin.z);
        }
    });
    check(doorsOpen == 0, "every door shut for the night: " + std::to_string(doorsOpen) + " open");

    std::printf("saving\n");
    {
        const std::string path = "/tmp/villager_test_npcs.dat";
        w.sim.npcs().save(path);
        NpcManager back;
        int n = back.load(path);
        check(n == (int)folk.size(), "they load: " + std::to_string(n));
        bool same = n > 0;
        for (auto& b2 : back.all()) {
            Npc* a = w.sim.npcs().find(b2->uuid);
            same = same && a && b2->life.on && b2->name == a->name && b2->life.job == a->life.job &&
                   glm::length(b2->life.bed.lieAt - a->life.bed.lieAt) < 0.01f && b2->life.bed.door == a->life.bed.door &&
                   b2->life.haunts.size() == a->life.haunts.size();
        }
        check(same, "  with their names, jobs, beds and haunts");
    }

    std::printf("woken\n");
    {
        Npc* n = folk[0];
        n->health -= 5.0f;
        n->lastHitBy = pl.uuid;
        w.run(1.0f);
        check(!n->lying && (n->stateName == "flee" || n->stateName == "alert"), "a hurt sleeper gets up and runs (" + n->stateName + ")");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
