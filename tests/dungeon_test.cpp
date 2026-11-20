// Headless checks for growing dungeons (dungeon/Layout.h) and the
// Dungeon world type (worldgen/DungeonGenerator.h), on the crypt's
// tiles in data/dungeons/crypt/tiles:
//   - every seed grows one, with one portal room, one boss arena, the
//     main path and side rooms in range, two levels
//   - every doorway meets a doorway (or is walled up), nothing overlaps,
//     everything can be reached from the portal room
//   - the same seed, the same dungeon
//   - the world: the portal room is hollow where you arrive, rock all
//     round the dungeon
//
//     dungeon_test           (from the repo root)
//     dungeon_test map <seed>  prints the layout level by level
#include "TestWorld.h"
#include "worldgen/DungeonGenerator.h"
#include "npc/Pathfinder.h"
#include "dungeon/Run.h"
#include "game/CombatMoves.h"
#include <cstdio>
#include <cstdlib>

using namespace testworld;
using namespace dungeon;

static void printMap(const Layout& L) {
    int levels = 0;
    for (auto& t : L.tiles) levels = std::max(levels, t.level + 1);
    glm::ivec3 lo(1 << 20), hi(-(1 << 20));
    for (auto& t : L.tiles) { lo = glm::min(lo, t.at); hi = glm::max(hi, t.at + L.variantOf(t).cells); }
    for (int lv = 0; lv < levels; lv++) {
        std::printf("level %d\n", lv + 1);
        for (int z = lo.z; z < hi.z; z++) {
            std::string row;
            for (int x = lo.x; x < hi.x; x++) {
                char ch = ' ';
                for (int y = lo.y; y < hi.y; y++) {
                    int t = L.tileAt({ x, y, z });
                    if (t < 0 || L.tiles[(size_t)t].level != lv) continue;
                    const std::string& r = L.tiles[(size_t)t].role;
                    ch = r == "portal" ? 'P' : r == "boss" ? 'B' : r == "stair" ? 'S' : r == "main" ? 'R' : r == "side" ? 's' :
                         r == "cap" ? 'x' : L.tiles[(size_t)t].pathIndex >= 0 ? '#' : '.';
                }
                row += ch;
            }
            std::printf("  %s\n", row.c_str());
        }
    }
}

int main(int argc, char** argv) {
    if (!loadData() || !gStructures.load("data/registry/structures.toml")) { std::printf("can't load data (run from the repo root)\n"); return 1; }
    TileSet set;
    std::string why;
    int n = set.load("crypt", &why);
    if (argc > 2 && std::string(argv[1]) == "map") {
        Layout L;
        bool ok = L.generate(set, std::strtoull(argv[2], nullptr, 10));
        std::printf("seed %s: %s (%d attempts) %s\n", argv[2], ok ? "ok" : "failed", L.attemptsUsed, L.failure.c_str());
        if (ok) printMap(L);
        return 0;
    }
    check(n >= 15 && why.empty(), "the crypt's tiles load (" + std::to_string(n) + ")" + (why.empty() ? "" : ": " + why));

    std::printf("growing (seeds 1-60)\n");
    int grown = 0, maxAttempts = 0, doorsOk = 0, reach = 0, shaped = 0, overlapFree = 0, levelsOk = 0;
    int minMain = 99, maxMain = 0, minSide = 99, maxSide = 0, maxTiles = 0, sealedTotal = 0;
    for (uint64_t seed = 1; seed <= 60; seed++) {
        Layout L;
        if (!L.generate(set, seed)) { std::printf("    seed %llu failed: %s\n", (unsigned long long)seed, L.failure.c_str()); continue; }
        grown++;
        maxAttempts = std::max(maxAttempts, L.attemptsUsed);
        int portals = 0, bosses = 0, levels = 0;
        for (auto& t : L.tiles) {
            portals += L.variantOf(t).tile->info.kind == "portal_room";
            bosses += L.variantOf(t).tile->info.kind == "boss";
            levels = std::max(levels, t.level + 1);
        }
        minMain = std::min(minMain, L.mainRooms); maxMain = std::max(maxMain, L.mainRooms);
        minSide = std::min(minSide, L.sideRooms); maxSide = std::max(maxSide, L.sideRooms);
        maxTiles = std::max(maxTiles, (int)L.tiles.size());
        sealedTotal += (int)L.sealed.size();
        shaped += portals == 1 && bosses == 1 && L.mainRooms >= 7 && L.mainRooms <= 9 && L.sideRooms >= 3;
        levelsOk += levels == 2 && L.tiles[(size_t)L.boss].level == 1 && L.tiles[(size_t)L.portal].level == 0;
        reach += L.reachable() == (int)L.tiles.size();
        // every face that touches another tile agrees; doors facing rock are only the sealed ones
        bool agree = true, overlap = false;
        int openToRock = 0;
        std::vector<int> cover((size_t)L.p.grid.x * L.p.grid.y * L.p.grid.z, 0);
        for (size_t i = 0; i < L.tiles.size(); i++) {
            const auto& t = L.tiles[i];
            const auto& v = L.variantOf(t);
            for (int x = 0; x < v.cells.x; x++) for (int y = 0; y < v.cells.y; y++) for (int z = 0; z < v.cells.z; z++) {
                glm::ivec3 c = t.at + glm::ivec3(x, y, z);
                if (cover[((size_t)c.y * L.p.grid.z + c.z) * L.p.grid.x + c.x]++) overlap = true;
                for (int d = 0; d < 6; d++) {
                    glm::ivec3 lc(x, y, z), nn = lc + dirStep(d);
                    if (nn.x >= 0 && nn.y >= 0 && nn.z >= 0 && nn.x < v.cells.x && nn.y < v.cells.y && nn.z < v.cells.z) continue;
                    std::string mine = v.socketAt(lc, d);
                    int o = L.tileAt(t.at + nn);
                    if (o < 0) { openToRock += mine != "wall"; continue; }
                    const auto& ot = L.tiles[(size_t)o];
                    if (L.variantOf(ot).socketAt(t.at + nn - ot.at, opposite(d)) != mine) agree = false;
                }
            }
        }
        doorsOk += agree && openToRock == (int)L.sealed.size();
        overlapFree += !overlap;
    }
    std::printf("    main path rooms %d-%d, side rooms %d-%d, up to %d tiles, up to %d attempts, %d doors walled up in all\n",
                minMain, maxMain, minSide, maxSide, maxTiles, maxAttempts, sealedTotal);
    check(grown == 60, "every seed grows one (" + std::to_string(grown) + "/60)");
    check(shaped == grown, "one portal room, one boss, 7-9 rooms on the path, 3+ side rooms");
    check(levelsOk == grown, "two levels: the portal on the first, the boss on the second");
    check(doorsOk == grown, "every doorway meets a doorway (or is walled up)");
    check(overlapFree == grown, "nothing overlaps");
    check(reach == grown, "everything can be reached from the portal room");

    Layout a, b;
    a.generate(set, 1234); b.generate(set, 1234);
    bool same = a.tiles.size() == b.tiles.size();
    for (size_t i = 0; same && i < a.tiles.size(); i++)
        same = a.tiles[i].variant == b.tiles[i].variant && a.tiles[i].at == b.tiles[i].at;
    check(same, "the same seed, the same dungeon");
    Layout c; c.generate(set, 1235);
    check(c.tiles.size() != a.tiles.size() || c.tiles[1].at != a.tiles[1].at || c.tiles[1].variant != a.tiles[1].variant,
          "another seed, another dungeon");

    std::printf("the world\n");
    DungeonGenerator gen(77, DungeonGenerator::SIZE_CHUNKS);
    check(gen.generated(), "the Dungeon world type grows its layout");
    glm::ivec3 sp = gen.spawnPoint(DungeonGenerator::SIZE_CHUNKS, DungeonGenerator::SIZE_CHUNKS);
    ChunkPtr ch = gen.generateChunk(sp.x / VOXEL_DIM, sp.z / VOXEL_DIM);
    int lx = sp.x - (sp.x / VOXEL_DIM) * VOXEL_DIM, lz = sp.z - (sp.z / VOXEL_DIM) * VOXEL_DIM;
    bool room = true;
    for (int y = sp.y; y < sp.y + 20; y++) room = room && ch->typeAt(lx, y, lz) == BLOCK_AIR;
    check(room && ch->typeAt(lx, sp.y - 1, lz) != BLOCK_AIR, "where you arrive: room to stand, a floor under you");
    check(ch->typeAt(lx, DungeonGenerator::ROCK_TOP - 5, lz) != BLOCK_AIR && ch->typeAt(lx, 100, lz) != BLOCK_AIR,
          "rock above and below");
    ChunkPtr corner = gen.generateChunk(0, 0);
    bool solid = true;
    for (int y = 0; y < DungeonGenerator::ROCK_TOP; y += 7) solid = solid && corner->typeAt(5, y, 5) != BLOCK_AIR;
    check(solid, "the world's edge is solid rock: boxed in");
    // every tile's middle at head height is open (it was carved)
    int open = 0;
    const Layout& L = gen.dungeonLayout();
    for (const auto& t : L.tiles) {
        glm::ivec3 w = gen.worldOf(t);
        const auto& v = L.variantOf(t);
        glm::ivec3 m = w + glm::ivec3(v.vol.size.x / 2, FLOOR + 12, v.vol.size.z / 2);
        // look round the middle for air (a pillar may stand there)
        bool any = false;
        ChunkPtr cc = gen.generateChunk(m.x / VOXEL_DIM, m.z / VOXEL_DIM);
        for (int dx = -4; dx <= 4 && !any; dx++) for (int dz = -4; dz <= 4 && !any; dz++) {
            int x = m.x + dx, z = m.z + dz;
            if (x / VOXEL_DIM != m.x / VOXEL_DIM || z / VOXEL_DIM != m.z / VOXEL_DIM) continue;
            any = cc->typeAt(x - (m.x / VOXEL_DIM) * VOXEL_DIM, m.y, z - (m.z / VOXEL_DIM) * VOXEL_DIM) == BLOCK_AIR;
        }
        open += any;
        if (!any && getenv("DBG")) std::printf("    shut: %s (%s) at %d %d %d\n", v.tile->name.c_str(), t.role.c_str(), m.x, m.y, m.z);
    }
    check(open == (int)L.tiles.size(), "every tile is hollowed out in the world (" + std::to_string(open) + "/" + std::to_string(L.tiles.size()) + ")");

    std::printf("walking it (the NPC pathfinder, tile to tile along the main path)\n");
    {
        // the chunks the dungeon reaches, generated, in a chunk manager
        ChunkManager chunks;
        glm::ivec3 lo(1 << 30), hi(-(1 << 30));
        for (const auto& t : L.tiles) { lo = glm::min(lo, gen.worldOf(t)); hi = glm::max(hi, gen.worldOf(t) + L.variantOf(t).vol.size); }
        for (int cx = lo.x / VOXEL_DIM; cx <= (hi.x - 1) / VOXEL_DIM; cx++)
            for (int cz = lo.z / VOXEL_DIM; cz <= (hi.z - 1) / VOXEL_DIM; cz++) chunks.setChunk(cx, cz, gen.generateChunk(cx, cz));
        VoxelPhysics phys;
        phys.init(chunks);
        nav::Pathfinder pf(phys);
        pf.maxNodes = 60000;
        // a place to stand in a tile: the open floor nearest its middle
        auto standIn = [&](const PlacedTile& t, glm::vec3& out) {
            const auto& v = L.variantOf(t);
            glm::ivec3 w = gen.worldOf(t);
            for (int r = 0; r < 40; r++)
                for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    // the lowest floor in this column of the tile (a stair: its bottom)
                    for (int y = FLOOR; y < v.vol.size.y - 18; y++) {
                        glm::ivec3 q(v.vol.size.x / 2 + dx, y, v.vol.size.z / 2 + dz);
                        if (q.x < 3 || q.z < 3 || q.x >= v.vol.size.x - 3 || q.z >= v.vol.size.z - 3) continue;
                        if (v.vol.at(q.x, y - 1, q.z) == BLOCK_AIR) continue;
                        bool room = true;
                        for (int k = 0; k < 18 && room; k++) for (int ox = -2; ox <= 2 && room; ox++) for (int oz = -2; oz <= 2 && room; oz++)
                            room = v.vol.at(q.x + ox, y + k, q.z + oz) == BLOCK_AIR;
                        if (room) { out = glm::vec3(w + q) + glm::vec3(0.5f, 0.0f, 0.5f); return true; }
                    }
                }
            return false;
        };
        int legs = 0, walked = 0;
        std::string stuck;
        for (size_t i = 1; i < L.mainPath.size(); i++) {
            glm::vec3 a, b;
            if (!standIn(L.tiles[(size_t)L.mainPath[i - 1]], a) || !standIn(L.tiles[(size_t)L.mainPath[i]], b)) { stuck = "no floor in a tile"; continue; }
            legs++;
            nav::Result r = pf.find(a, b);
            if (r.reached) walked++;
            else if (stuck.empty()) stuck = L.variantOf(L.tiles[(size_t)L.mainPath[i - 1]]).tile->name + " -> " + L.variantOf(L.tiles[(size_t)L.mainPath[i]]).tile->name;
        }
        check(legs == (int)L.mainPath.size() - 1 && walked == legs,
              "from the portal room to the boss on foot: " + std::to_string(walked) + "/" + std::to_string(L.mainPath.size() - 1) + " steps" +
              (stuck.empty() ? "" : " (stuck: " + stuck + ")"));
    }

    std::printf("coming alive (dungeon/Run.h)\n");
    {
        // a seed whose crypt has a secret room, to break its wall too
        uint64_t seedWith = 77;
        for (uint64_t sd = 77; sd < 300; sd++) {
            Layout t;
            if (!t.generate(set, sd)) continue;
            bool secret = false;
            for (auto& pt : t.tiles) secret = secret || t.variantOf(pt).tile->name == "side_secret";
            if (secret) { seedWith = sd; break; }
        }
        DungeonGenerator gen(seedWith, DungeonGenerator::SIZE_CHUNKS);
        const Layout& L = gen.dungeonLayout();
        ChunkManager chunks;
        glm::ivec3 lo(1 << 30), hi(-(1 << 30));
        for (const auto& t : L.tiles) { lo = glm::min(lo, gen.worldOf(t)); hi = glm::max(hi, gen.worldOf(t) + L.variantOf(t).vol.size); }
        for (int cx = lo.x / VOXEL_DIM; cx <= (hi.x - 1) / VOXEL_DIM; cx++)
            for (int cz = lo.z / VOXEL_DIM; cz <= (hi.z - 1) / VOXEL_DIM; cz++) chunks.setChunk(cx, cz, gen.generateChunk(cx, cz));
        VoxelPhysics phys; phys.init(chunks);
        WorldEditor editor(chunks);
        StructureManager structs(chunks, editor, gen);
        ChestStore chests;
        Simulation sim;
        sim.attachWorld(&phys, nullptr);
        sim.roaming.enabled = false;
        Player& p = sim.addPlayer();
        p.setMode(GameMode::Survival);
        p.position = glm::vec3(gen.spawnPoint(DungeonGenerator::SIZE_CHUNKS, DungeonGenerator::SIZE_CHUNKS));
        dungeon::Run run(gen);
        auto tickAt = [&](glm::vec3 at) {
            p.position = at;
            auto fx = run.tick({ &p }, sim, chunks, structs, chests, editor);
            editor.flush();
            return fx;
        };
        auto inside = [&](int tile) {
            const auto& t = L.tiles[(size_t)tile];
            return glm::vec3(gen.worldOf(t)) + glm::vec3(L.variantOf(t).vol.size) * glm::vec3(0.5f, 0.0f, 0.5f) + glm::vec3(0, FLOOR, 0);
        };
        tickAt(p.position);
        int chestsWanted = 0, chestsMade = 0;
        for (auto& sp : run.allSpots()) if (sp.m.kind == "chest") { chestsWanted++; chestsMade += sp.done; }
        check(sim.npcs().all().empty(), "in the portal room: nothing has spawned");
        check(chestsWanted > 0 && chestsMade == chestsWanted, "the chests are put down (" + std::to_string(chestsMade) + "/" + std::to_string(chestsWanted) + ")");
        int structsCount = 0; structs.forEachInstance([&](const StructureInstance&) { structsCount++; });
        check(structsCount >= chestsWanted, "as real chests in the world");
        // walk the main path: rooms with spawners fill as they're entered
        int roomWith = -1;
        for (int t : L.mainPath) for (auto& sp : run.allSpots()) if (sp.tile == t && sp.m.kind == "spawner" && roomWith < 0) roomWith = t;
        size_t before = sim.npcs().all().size();
        tickAt(inside(roomWith));
        size_t after = sim.npcs().all().size();
        check(after > before, "stepping into a room with a spawner: its dead appear (" + std::to_string(after - before) + ")");
        tickAt(inside(roomWith));
        check(sim.npcs().all().size() == after, "only once");
        // the boss's arena: the boss (and its coffins)
        before = sim.npcs().all().size();
        auto fx = tickAt(inside(L.boss));
        bool lord = false;
        for (auto& n : sim.npcs().all()) lord = lord || n->typeId == "crypt_lord";
        check(lord && sim.npcs().all().size() > before + 1, "into the arena: the Crypt Lord rises, and its coffins open");
        // saved and loaded: done stays done
        std::filesystem::path dir = std::filesystem::temp_directory_path() / "voxelrpg_dungeon_test";
        std::filesystem::create_directories(dir);
        run.save(dir.string());
        dungeon::Run again(gen);
        again.load(dir.string());
        int same = 0, total = 0;
        for (size_t i = 0; i < run.allSpots().size(); i++) { total++; same += run.allSpots()[i].done == again.allSpots()[i].done; }
        check(same == total && again.wasEntered(L.boss), "what happened is saved (dungeon.dat)");
        std::filesystem::remove_all(dir);
        // the arena: barred while the boss lives and someone's inside
        {
            Npc* lordNpc = nullptr;
            for (auto& n : sim.npcs().all()) if (n->typeId == "crypt_lord") lordNpc = n.get();
            const auto& bars = run.barVoxels();
            auto barsUp = [&] { int n = 0; for (auto& v : bars) n += chunks.typeAtVoxel(v.x, v.y, v.z) == gRegistry.getIDByName("iron_block"); return n; };
            check(!bars.empty() && run.isBarred() && barsUp() > (int)bars.size() / 2,
                  "the arena's doorway is barred while the Crypt Lord lives (" + std::to_string(barsUp()) + " bars)");
            check(lordNpc && std::fabs(lordNpc->scale - 1.6f) < 0.01f && lordNpc->height > 25.0f, "  the Crypt Lord: a giant (" + std::to_string(lordNpc ? lordNpc->height : 0.0f).substr(0, 4) + " voxels tall)");
            // its second phase: the other coffins
            size_t before2 = sim.npcs().all().size();
            if (lordNpc) { lordNpc->phaseTwo = true; }
            fx = tickAt(inside(L.boss));
            check(sim.npcs().all().size() > before2, "  its second phase opens more coffins (" + std::to_string(sim.npcs().all().size() - before2) + ")");
            // everyone out (dead, back at the portal): the bars lift
            glm::vec3 away = inside(L.mainPath[0]);
            for (int i = 0; i < 3 * 60; i++) { p.position = away; run.tick({ &p }, sim, chunks, structs, chests, editor); editor.flush(); }
            check(!run.isBarred() && barsUp() == 0, "  with nobody left inside, the bars lift");
            tickAt(inside(L.boss));
            check(run.isBarred(), "  back in: barred again");
            // it falls: cleared
            if (lordNpc) { lordNpc->health = 0.0f; }
            bool said = false;
            for (auto& e : tickAt(inside(L.boss))) said = said || e.line.find("falls") != std::string::npos;
            check(run.isCleared() && said, "the Crypt Lord falls: the dungeon is cleared");
            check(!run.isBarred() && barsUp() == 0, "  and the bars lift for good");
            std::filesystem::create_directories(dir);
            run.save(dir.string());
            dungeon::Run third(gen);
            third.load(dir.string());
            check(third.isCleared(), "  cleared is saved");
            std::filesystem::remove_all(dir);
        }
        // a secret wall: hit it until it breaks
        check(!run.allWalls().empty(), "seed " + std::to_string(seedWith) + " has a secret wall");
        if (!run.allWalls().empty()) {
            const auto& w = run.allWalls()[0];
            glm::ivec3 b0 = w.blocks[0];
            check(chunks.typeAtVoxel(b0.x, b0.y, b0.z) != BLOCK_AIR, "a secret wall stands in the world");
            // stand in front of it, facing it
            glm::vec3 c = w.centre;
            glm::vec3 dir(0.0f);
            for (int d = 0; d < 4; d++) {
                glm::vec3 probe = c + glm::vec3(dirStep(d)) * 6.0f;
                if (chunks.typeAtVoxel((int)probe.x, (int)c.y, (int)probe.z) == BLOCK_AIR) { dir = glm::vec3(dirStep(d)); break; }
            }
            glm::vec3 stand = glm::vec3(c.x, 0.0f, c.z) + dir * 6.0f;
            stand.y = (float)(gen.worldOf(L.tiles[(size_t)w.tile]).y + FLOOR);
            int swings = 0;
            p.inventory.setSlot(Inventory::FIRST_MAIN, gItems.find("iron_sword"), 1);
            p.inventory.select(Inventory::FIRST_MAIN);
            while (!run.allWalls()[0].broken && swings < 10) {
                p.facing = -dir;
                p.attack = AttackState();
                p.moveset = gMovesets.find("sword");
                combat::beginAttack(p, AttackKind::Light);
                p.attack.t = p.attack.def().activeStart + 0.01f;
                tickAt(stand);
                p.attack = AttackState();
                tickAt(stand);
                swings++;
            }
            check(run.allWalls()[0].broken, "a secret wall breaks after " + std::to_string(swings) + " lights");
            check(chunks.typeAtVoxel(b0.x, b0.y, b0.z) == BLOCK_AIR, "and the way is open");
        }
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
