// Headless checks for dungeon tiles (dungeon/Tile.h, StarterTiles.h):
// the starter set is well made (sizes on the grid, doorways open where
// the sockets say and nowhere else, markers found), files round-trip,
// and turning a tile turns its sockets and markers with its voxels.
//
//     tile_test        (from the repo root: it reads data/registry; files go to a temp folder)
//     tile_test write  writes the starter set into data/dungeons/crypt/tiles (replacing
//                      the starter tiles there; the same as "tile starter crypt over" in game)
#include "TestWorld.h"
#include "dungeon/StarterTiles.h"
#include "game/Blueprint.h"
#include <cstdio>
#include <filesystem>

using namespace testworld;
using namespace dungeon;

// The middle of a socket's doorway, just inside the tile's edge, at
// head height: air if the doorway is open
static glm::ivec3 doorProbe(glm::ivec3 cell, int face, glm::ivec3 size) {
    glm::ivec3 o = cell * HALF;
    int y = o.y + FLOOR + 10, m = HALF / 2;
    switch (face) {
    case North: return { o.x + m, y, 0 };
    case South: return { o.x + m, y, size.z - 1 };
    case West:  return { 0, y, o.z + m };
    case East:  return { size.x - 1, y, o.z + m };
    default:    return { o.x + m, face == Up ? size.y - 1 : 0, o.z + m };
    }
}

// every face square on the outside of a tile
template <class F> static void eachFace(glm::ivec3 cells, F f) {
    for (int x = 0; x < cells.x; x++) for (int y = 0; y < cells.y; y++) for (int z = 0; z < cells.z; z++)
        for (int d = 0; d < 4; d++) {
            glm::ivec3 n = glm::ivec3(x, y, z) + dirStep(d);
            if (n.x < 0 || n.z < 0 || n.x >= cells.x || n.z >= cells.z) f(glm::ivec3(x, y, z), d);
        }
}

int main(int argc, char** argv) {
    if (!loadData()) { std::printf("can't load data (run from the repo root)\n"); return 1; }
    if (argc > 1 && std::string(argv[1]) == "write") {
        std::string report;
        writeStarterTiles("crypt", true, report);
        std::printf("%s\n", report.c_str());
        return 0;
    }
    for (const std::string& k : markerKinds()) check(markerBlock(k) != 0, "marker block marker_" + k + " exists");

    std::printf("the crypt's starter tiles\n");
    std::vector<Tile> ts = starterCrypt();
    // the copies the game loads (data/dungeons/crypt/tiles) must be
    // the starter set as built now: "tile_test write" after changing it
    {
        TileSet disk;
        std::string why;
        disk.loadDir("data/dungeons/crypt/tiles", "crypt", &why);
        int stale = 0;
        std::string which;
        for (const Tile& t : ts) {
            const Tile* d = nullptr;
            for (const auto& x : disk.all()) if (x->name == t.name) d = x.get();
            auto key = [](const Tile& k) {
                std::vector<std::string> ms;
                for (const Marker& m : k.info.markers) {
                    std::string one = m.kind + std::to_string(m.at.x) + "," + std::to_string(m.at.y) + "," + std::to_string(m.at.z);
                    for (auto& [a, v] : m.props) one += a + v;
                    ms.push_back(one);
                }
                std::sort(ms.begin(), ms.end());
                std::string out = std::to_string(k.info.cells.x) + std::to_string(k.info.cells.y) + std::to_string(k.info.cells.z);
                for (auto& m : ms) out += m + ";";
                return out;
            };
            if (!d || key(*d) != key(t) || d->vol.size != t.vol.size) { stale++; which += " " + t.name; }
        }
        check(stale == 0, "data/dungeons/crypt/tiles matches the starter set" + (stale ? " (stale:" + which + "; run tile_test write)" : std::string()));
    }
    check(ts.size() >= 15, std::to_string(ts.size()) + " tiles");
    auto count = [&](const std::string& kind) { int n = 0; for (auto& t : ts) n += t.info.kind == kind; return n; };
    check(count("portal_room") == 1 && count("boss") == 1 && count("stair") >= 1 && count("corridor") >= 5 &&
          count("room") >= 4 && count("side") >= 3, "every kind the generator needs");
    for (const Tile& t : ts) {
        bool sized = t.vol.size == t.info.cells * HALF;
        bool doorsOpen = true, wallsShut = true, floors = true;
        eachFace(t.info.cells, [&](glm::ivec3 c, int d) {
            std::string s = t.info.socketAt(c, d);
            glm::ivec3 p = doorProbe(c, d, t.vol.size);
            bool air = t.vol.at(p.x, p.y, p.z) == BLOCK_AIR || markerKindOf(t.vol.at(p.x, p.y, p.z)) == "secret";
            if (s == "door" && !air) doorsOpen = false;
            if (s == "wall" && air) wallsShut = false;
            if (s == "door") {
                glm::ivec3 f = p; f.y = c.y * HALF + FLOOR - 1;      // something to stand on in the doorway
                if (t.vol.at(f.x, f.y, f.z) == BLOCK_AIR) floors = false;
            }
        });
        int markerBlocks = 0;
        for (int y = 0; y < t.vol.size.y; y++) for (int z = 0; z < t.vol.size.z; z++) for (int x = 0; x < t.vol.size.x; x++)
            markerBlocks += !markerKindOf(t.vol.at(x, y, z)).empty();
        int secretBlocks = 0;
        for (const Marker& m : t.info.markers) (void)m;
        for (int y = 0; y < t.vol.size.y; y++) for (int z = 0; z < t.vol.size.z; z++) for (int x = 0; x < t.vol.size.x; x++)
            secretBlocks += markerKindOf(t.vol.at(x, y, z)) == "secret";
        check(sized && doorsOpen && wallsShut && floors && (int)t.info.markers.size() == markerBlocks - secretBlocks &&
              !t.info.sockets.empty(),
              t.name + ": " + std::to_string(t.info.sockets.size()) + " sockets, " + std::to_string(t.info.markers.size()) + " markers" +
              (sized ? "" : " WRONG SIZE") + (doorsOpen ? "" : " A DOOR IS SHUT") + (wallsShut ? "" : " A WALL HAS A HOLE") +
              (floors ? "" : " NO FLOOR IN A DOORWAY"));
    }

    std::printf("files\n");
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "voxelrpg_tile_test";
    std::filesystem::remove_all(dir);
    bool saved = true;
    for (const Tile& t : ts) { std::string why; saved = saved && TileSet::saveTile(t, dir.string(), why); }
    check(saved, "all saved");
    TileSet set;
    std::string why;
    int n = set.loadDir(dir.string(), "crypt", &why);
    check(n == (int)ts.size() && why.empty(), "all load back (" + std::to_string(n) + ")" + (why.empty() ? "" : ": " + why));
    bool same = true;
    for (const Tile& t : ts) {
        const Tile* l = set.find(t.name);
        if (!l || l->vol.data != t.vol.data || l->info.sockets.size() != t.info.sockets.size() || l->info.kind != t.info.kind ||
            l->info.markers.size() != t.info.markers.size()) { same = false; continue; }
        for (const Socket& s : t.info.sockets) if (l->info.socketAt(s.cell, s.face) != s.type) same = false;
        for (const Marker& m : t.info.markers) {
            const Marker* lm = l->info.markerAt(m.at);
            if (!lm || lm->kind != m.kind || lm->props != m.props) same = false;
        }
    }
    check(same, "voxels, sockets and markers the same after saving and loading");
    Blueprint bp;
    check(bp.load((dir / "room_hall.toml").string(), why) && bp.isTile && bp.voxels == "room_hall.vxvol" && bp.tile.kind == "room",
          "a tile is a blueprint (with its voxels and [tile] table)");

    std::printf("turning\n");
    check(set.allVariants().size() > ts.size() * 2, std::to_string(set.allVariants().size()) + " ways to place them");
    bool turned = true;
    std::string bad;
    for (const TileVariant& v : set.allVariants()) {
        eachFace(v.cells, [&](glm::ivec3 c, int d) {
            std::string s = v.socketAt(c, d);
            glm::ivec3 p = doorProbe(c, d, v.vol.size);
            uint16_t b = v.vol.at(p.x, p.y, p.z);
            bool air = b == BLOCK_AIR || markerKindOf(b) == "secret";
            if ((s == "door") != air) { turned = false; bad = v.tile->name + " turned " + std::to_string(v.rot); }
        });
        for (const Marker& m : v.markers)
            if (markerKindOf(v.vol.at(m.at.x, m.at.y, m.at.z)) != m.kind) { turned = false; bad = v.tile->name + " marker"; }
    }
    check(turned, "every turn: doorways where its sockets say, markers on their blocks" + (bad.empty() ? "" : " (" + bad + ")"));
    // a turn of a corridor going north-south goes east-west
    const Tile* straight = set.find("corridor_straight");
    if (straight) {
        TileVariant q1 = makeVariant(*straight, 1, false);
        check(q1.cells == glm::ivec3(2, 2, 1) && q1.socketAt({ 1, 0, 0 }, East) == "door" && q1.socketAt({ 0, 0, 0 }, West) == "door",
              "a quarter turn: north-south becomes east-west");
    }
    std::filesystem::remove_all(dir);

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
