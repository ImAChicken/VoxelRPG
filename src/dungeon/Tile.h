#pragma once
// ══════════════════════════════════════════════════════════
// Dungeon tiles (docs/dungeons.md)
// ══════════════════════════════════════════════════════════
// A tile is a hand-built (or generated) piece of dungeon: a box of
// voxels on the dungeon grid, with what its edges join to (sockets)
// and the things in it that come alive in a dungeon (markers). The
// generator (later: dungeon/Generator.h) grows a dungeon by fitting
// tiles together edge to edge where their sockets match.
//
// The grid
//   HALF = 16 voxels: the unit of the grid. A tile is a whole number
//   of half cells on each side (a narrow passage 1 wide, a corridor
//   1 x 2 tall, a room 4 x 2 x 4, a tall hall 4 x 3 x 4). A "cell" of
//   32 is 2 half cells: the size most things are built to
//   Every tile's floor is FLOOR voxels thick at its bottom; doorways
//   stand on it, so tiles placed on the grid line up
//
// Sockets: each half-cell square on the outside of a tile, one per
// face of each edge cell, says what's there:
//   wall    solid (the default: anything not listed)
//   door    a doorway DOOR_W wide, DOOR_H tall, centred on that square
//           (standing on that cell's floor)
//   open    no wall at all: one space carried on into the next tile
//   shaft   (up / down faces) a hole through the floor or ceiling
//   A socket joins one of the same type facing it. A wall can face
//   solid rock (nothing placed there); nothing else can
//
// Markers: special blocks (blocks.toml, marker_*) placed like any
// other block. Each becomes the real thing when the tile is put in a
// dungeon (a spawner, a chest, the portal …). A marker's settings
// (what a spawner spawns) are in the tile's [[marker]] list by its
// position. Kinds: spawner, chest, portal, exit, boss, coffin; and
// secret: every marker_secret block together is a wall that breaks
// (no settings, not listed one by one)
//
// File: a blueprint (game/Blueprint.h) with a [tile] table and a
// voxels file beside it (<name>.vxvol, world/VoxelVolume.h). Tiles
// live in data/dungeons/<theme>/tiles/
//
// Directions: 0 north (-z), 1 east (+x), 2 south (+z), 3 west (-x),
// 4 up (+y), 5 down (-y). A quarter turn clockwise (seen from above)
// takes north to east, as VoxelVolume::rotatedY does.

#include "../world/VoxelVolume.h"
#include "../voxel/VoxelRegistry.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace dungeon {

constexpr int HALF   = 16;            // voxels: the grid's unit
constexpr int CELL   = 32;            // voxels: a standard cell (2 half cells)
constexpr int FLOOR  = 2;             // voxels of floor at the bottom of every tile
constexpr int WALL   = 2;             // voxels: walls in the starter tiles
constexpr int DOOR_W = 12;            // a doorway: wide …
constexpr int DOOR_H = 24;            // … and tall, standing on the floor

enum Dir { North = 0, East = 1, South = 2, West = 3, Up = 4, Down = 5 };
inline const char* dirName(int d) {
    static const char* n[6] = { "n", "e", "s", "w", "up", "down" };
    return d >= 0 && d < 6 ? n[d] : "?";
}
inline int dirFromName(const std::string& s) {
    if (s == "n" || s == "north") return North;
    if (s == "e" || s == "east") return East;
    if (s == "s" || s == "south") return South;
    if (s == "w" || s == "west") return West;
    if (s == "up" || s == "u") return Up;
    if (s == "down" || s == "d") return Down;
    return -1;
}
inline int opposite(int d) { return d < 4 ? (d + 2) & 3 : (d == Up ? Down : Up); }
inline glm::ivec3 dirStep(int d) {
    static const glm::ivec3 s[6] = { {0,0,-1}, {1,0,0}, {0,0,1}, {-1,0,0}, {0,1,0}, {0,-1,0} };
    return s[d];
}

// Socket types
inline const std::vector<std::string>& socketTypes() {
    static const std::vector<std::string> t = { "wall", "door", "open", "shaft" };
    return t;
}

struct Socket {
    glm::ivec3  cell{0};        // which half cell of the tile (x, y, z from its low corner)
    int         face = North;   // which of its faces (one on the tile's outside)
    std::string type = "door";
};

// A marker's settings, by where it is in the tile
struct Marker {
    glm::ivec3  at{0};          // voxel in the tile
    std::string kind;           // spawner, chest, portal, exit, boss, coffin, secret
    std::map<std::string, std::string> props;   // spawn = "undead", count = "2 4", rule = "enter" …
    std::string prop(const std::string& k, const std::string& def = "") const {
        auto it = props.find(k); return it == props.end() ? def : it->second;
    }
};

// What the marker blocks are (blocks.toml: marker_<kind>)
inline const std::vector<std::string>& markerKinds() {
    static const std::vector<std::string> k = { "spawner", "chest", "portal", "exit", "boss", "coffin", "secret" };
    return k;
}
inline std::string markerKindOf(uint16_t block) {
    if (!block || block >= gRegistry.size()) return "";
    const std::string& n = gRegistry.get(block).name;
    return n.rfind("marker_", 0) == 0 ? n.substr(7) : "";
}
inline uint16_t markerBlock(const std::string& kind) { return gRegistry.getIDByName("marker_" + kind); }

// Tile kinds (what the generator uses each for)
//   portal_room  where you arrive and leave (one per dungeon)
//   corridor     joins things
//   room         on the main path, a fight or a pause
//   side         off the main path (treasure, a harder fight)
//   stair        joins two levels
//   boss         the boss's arena (one, at the end)
//   filler       anything else
struct TileInfo {
    std::string theme = "crypt";
    std::string kind  = "room";
    glm::ivec3  cells{2, 2, 2};       // half cells (x, y, z): the tile is cells * HALF voxels
    float       weight = 1.0f;        // how often it's picked, against others that fit
    int         minCount = 0, maxCount = -1;   // per dungeon (-1: any number)
    int         depthMin = 0, depthMax = 99;   // which levels it can be on
    bool        rotate = true;        // may be turned (all four ways)
    bool        mirror = false;       // may be mirrored too
    std::vector<Socket> sockets;      // everything not listed is wall
    std::vector<Marker> markers;

    glm::ivec3 voxels() const { return cells * HALF; }

    std::string socketAt(glm::ivec3 cell, int face) const {
        for (const Socket& s : sockets) if (s.cell == cell && s.face == face) return s.type;
        return "wall";
    }
    void setSocket(glm::ivec3 cell, int face, const std::string& type) {
        sockets.erase(std::remove_if(sockets.begin(), sockets.end(),
            [&](const Socket& s) { return s.cell == cell && s.face == face; }), sockets.end());
        if (type != "wall") sockets.push_back({ cell, face, type });
    }
    // Is this face of this cell on the tile's outside
    bool outside(glm::ivec3 c, int face) const {
        glm::ivec3 n = c + dirStep(face);
        return n.x < 0 || n.y < 0 || n.z < 0 || n.x >= cells.x || n.y >= cells.y || n.z >= cells.z;
    }
    Marker* markerAt(glm::ivec3 at) {
        for (Marker& m : markers) if (m.at == at) return &m;
        return nullptr;
    }
    const Marker* markerAt(glm::ivec3 at) const {
        for (const Marker& m : markers) if (m.at == at) return &m;
        return nullptr;
    }

    // ── File: the [tile] table of a blueprint ─────────────
    void write(std::ostream& f) const {
        f << "\n[tile]   # a dungeon tile (dungeon/Tile.h)\n";
        f << "theme = \"" << theme << "\"\n";
        f << "kind = \"" << kind << "\"\n";
        f << "cells = [" << cells.x << ", " << cells.y << ", " << cells.z << "]   # half cells of 16 voxels\n";
        f << "weight = " << weight << "\n";
        if (minCount > 0) f << "min = " << minCount << "\n";
        if (maxCount >= 0) f << "max = " << maxCount << "\n";
        if (depthMin > 0) f << "depth_min = " << depthMin << "\n";
        if (depthMax < 99) f << "depth_max = " << depthMax << "\n";
        if (!rotate) f << "rotate = false\n";
        if (mirror) f << "mirror = true\n";
        f << "sockets = [\n";
        for (const Socket& s : sockets)
            f << "  { cell = [" << s.cell.x << ", " << s.cell.y << ", " << s.cell.z << "], face = \"" << dirName(s.face)
              << "\", type = \"" << s.type << "\" },\n";
        f << "]\n";
        for (const Marker& m : markers) {
            f << "\n[[tile.marker]]\nat = [" << m.at.x << ", " << m.at.y << ", " << m.at.z << "]\nkind = \"" << m.kind << "\"\n";
            for (auto& [k, v] : m.props) f << k << " = \"" << v << "\"\n";
        }
    }
    bool read(const toml::table& t) {
        theme = t["theme"].value_or(theme);
        kind  = t["kind"].value_or(kind);
        if (auto* a = t["cells"].as_array(); a && a->size() == 3)
            cells = glm::max(glm::ivec3((*a)[0].value_or(1), (*a)[1].value_or(1), (*a)[2].value_or(1)), glm::ivec3(1));
        weight   = (float)t["weight"].value_or(1.0);
        minCount = (int)t["min"].value_or(0);
        maxCount = (int)t["max"].value_or(-1);
        depthMin = (int)t["depth_min"].value_or(0);
        depthMax = (int)t["depth_max"].value_or(99);
        rotate   = t["rotate"].value_or(true);
        mirror   = t["mirror"].value_or(false);
        sockets.clear();
        if (auto* arr = t["sockets"].as_array())
            for (auto& n : *arr)
                if (auto* s = n.as_table()) {
                    Socket k;
                    if (auto* c = (*s)["cell"].as_array(); c && c->size() == 3)
                        k.cell = { (*c)[0].value_or(0), (*c)[1].value_or(0), (*c)[2].value_or(0) };
                    k.face = dirFromName((*s)["face"].value_or(std::string("n")));
                    k.type = (*s)["type"].value_or(std::string("door"));
                    if (k.face >= 0 && k.type != "wall") sockets.push_back(k);
                }
        markers.clear();
        if (auto* arr = t["marker"].as_array())
            for (auto& n : *arr)
                if (auto* m = n.as_table()) {
                    Marker k;
                    if (auto* a = (*m)["at"].as_array(); a && a->size() == 3)
                        k.at = { (*a)[0].value_or(0), (*a)[1].value_or(0), (*a)[2].value_or(0) };
                    k.kind = (*m)["kind"].value_or(std::string());
                    for (auto& [key, v] : *m) {
                        std::string ks(key.str());
                        if (ks == "at" || ks == "kind") continue;
                        if (auto sv = v.value<std::string>()) k.props[ks] = *sv;
                        else if (auto iv = v.value<int64_t>()) k.props[ks] = std::to_string(*iv);
                        else if (auto dv = v.value<double>()) k.props[ks] = std::to_string(*dv);
                    }
                    markers.push_back(k);
                }
        return true;
    }
};

// A tile as loaded: its info and its voxels
struct Tile {
    std::string name;
    TileInfo    info;
    VoxelVolume vol;            // cells * HALF on each side; air is part of it (it carves)
};

// One way a tile can be placed: turned (and maybe mirrored), with its
// sockets and markers turned to match. What the generator places
struct TileVariant {
    const Tile* tile = nullptr;
    int         rot = 0;        // quarter turns clockwise
    bool        mirrored = false;
    glm::ivec3  cells{1};
    VoxelVolume vol;
    std::vector<Socket> sockets;
    std::vector<Marker> markers;

    std::string socketAt(glm::ivec3 cell, int face) const {
        for (const Socket& s : sockets) if (s.cell == cell && s.face == face) return s.type;
        return "wall";
    }
};

// Turning a cell position and a direction q quarter turns (the
// VoxelVolume rule, on a grid of 'size')
inline glm::ivec3 turnCell(glm::ivec3 c, glm::ivec3 size, int q) {
    switch (q & 3) {
    case 1:  return { size.z - 1 - c.z, c.y, c.x };
    case 2:  return { size.x - 1 - c.x, c.y, size.z - 1 - c.z };
    case 3:  return { c.z, c.y, size.x - 1 - c.x };
    default: return c;
    }
}
inline int turnDir(int d, int q) { return d < 4 ? (d + q) & 3 : d; }
inline glm::ivec3 turnedSize(glm::ivec3 s, int q) { return (q & 1) ? glm::ivec3(s.z, s.y, s.x) : s; }

inline TileVariant makeVariant(const Tile& t, int q, bool mirror) {
    TileVariant v;
    v.tile = &t; v.rot = q & 3; v.mirrored = mirror;
    glm::ivec3 cs = t.info.cells, vs = t.vol.size;
    // mirror first (along x), then turn
    VoxelVolume base = mirror ? t.vol.mirrored('x') : t.vol;
    std::vector<Socket> socks = t.info.sockets;
    std::vector<Marker> marks = t.info.markers;
    if (mirror) {
        for (Socket& s : socks) {
            s.cell.x = cs.x - 1 - s.cell.x;
            if (s.face == East) s.face = West; else if (s.face == West) s.face = East;
        }
        for (Marker& m : marks) m.at.x = vs.x - 1 - m.at.x;
    }
    v.vol = base.rotatedY(q);
    v.cells = turnedSize(cs, q);
    for (Socket& s : socks) { s.cell = turnCell(s.cell, cs, q); s.face = turnDir(s.face, q); }
    for (Marker& m : marks) m.at = turnCell(m.at, vs, q);
    v.sockets = socks;
    v.markers = marks;
    return v;
}

// ── A theme's tiles ───────────────────────────────────────
class TileSet {
public:
    static std::string dirFor(const std::string& theme) { return "data/dungeons/" + theme + "/tiles"; }

    // Loads every tile in data/dungeons/<theme>/tiles (the blueprints
    // with a [tile] table and a voxels file) and makes their variants
    int load(const std::string& theme, std::string* report = nullptr) { return loadDir(dirFor(theme), theme, report); }
    int loadDir(const std::string& dir, const std::string& theme, std::string* report = nullptr) {
        tiles.clear(); variants.clear();
        themeName = theme;
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        for (auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".toml") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        std::ostringstream why;
        for (auto& f : files) {
            auto t = std::make_unique<Tile>();
            std::string err;
            if (!loadTile(f, *t, err)) { why << f.filename().string() << ": " << err << "\n"; continue; }
            tiles.push_back(std::move(t));
        }
        for (auto& t : tiles) {
            int turns = t->info.rotate ? 4 : 1;
            for (int m = 0; m < (t->info.mirror ? 2 : 1); m++)
                for (int q = 0; q < turns; q++) {
                    // a turn that comes out the same as one already made is skipped
                    TileVariant v = makeVariant(*t, q, m == 1);
                    bool dup = false;
                    for (auto& o : variants)
                        if (o.tile == t.get() && o.cells == v.cells && sameSockets(o, v) && o.vol.data == v.vol.data) { dup = true; break; }
                    if (!dup) variants.push_back(std::move(v));
                }
        }
        if (report) *report = why.str();
        return (int)tiles.size();
    }

    static bool loadTile(const std::filesystem::path& file, Tile& t, std::string& why) {
        try {
            toml::table tbl = toml::parse_file(file.string());
            auto* tt = tbl["tile"].as_table();
            if (!tt) { why = "no [tile] table"; return false; }
            t.name = tbl["name"].value_or(file.stem().string());
            t.info.read(*tt);
            std::string vox = tbl["voxels"].value_or(file.stem().string() + ".vxvol");
            if (!t.vol.load(file.parent_path() / vox)) { why = "can't read " + vox; return false; }
            if (t.vol.size != t.info.voxels()) {
                why = "voxels are " + std::to_string(t.vol.size.x) + "x" + std::to_string(t.vol.size.y) + "x" +
                      std::to_string(t.vol.size.z) + ", cells say " + std::to_string(t.info.voxels().x) + "x" +
                      std::to_string(t.info.voxels().y) + "x" + std::to_string(t.info.voxels().z);
                return false;
            }
            // markers: every marker block in the voxels, with its settings
            // from the file (a marker block with none listed still counts)
            std::vector<Marker> found;
            for (int y = 0; y < t.vol.size.y; y++)
            for (int z = 0; z < t.vol.size.z; z++)
            for (int x = 0; x < t.vol.size.x; x++) {
                std::string k = markerKindOf(t.vol.at(x, y, z));
                if (k.empty() || k == "secret") continue;      // a secret wall is all its blocks, not one marker
                Marker m;
                if (const Marker* listed = t.info.markerAt({ x, y, z })) m = *listed;
                m.at = { x, y, z }; m.kind = k;
                found.push_back(m);
            }
            t.info.markers = found;
            return true;
        } catch (const toml::parse_error& e) {
            why = std::string(e.description());
            return false;
        }
    }

    // Writes a tile: <dir>/<name>.toml (a blueprint with [tile]) and <name>.vxvol
    static bool saveTile(const Tile& t, const std::string& dir, std::string& why) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::filesystem::path base = std::filesystem::path(dir) / t.name;
        if (!t.vol.save(base.string() + ".vxvol")) { why = "can't write the voxels"; return false; }
        std::ofstream f(base.string() + ".toml");
        if (!f) { why = "can't write the file"; return false; }
        f << "# A dungeon tile (dungeon/Tile.h, docs/dungeons.md): a blueprint whose\n"
             "# voxels are in the .vxvol beside it. Edit it in game: tile edit " << t.name << "\n\n";
        f << "name = \"" << t.name << "\"\n";
        f << "display = \"" << t.name << "\"\n";
        f << "tags = [\"dungeon\", \"" << t.info.theme << "\", \"" << t.info.kind << "\"]\n";
        f << "size = [" << t.vol.size.x << ", " << t.vol.size.y << ", " << t.vol.size.z << "]\n";
        f << "voxels = \"" << t.name << ".vxvol\"\n";
        t.info.write(f);
        return (bool)f;
    }

    const std::string& theme() const { return themeName; }
    const std::vector<std::unique_ptr<Tile>>& all() const { return tiles; }
    const std::vector<TileVariant>& allVariants() const { return variants; }
    const Tile* find(const std::string& name) const {
        for (auto& t : tiles) if (t->name == name) return t.get();
        return nullptr;
    }

private:
    std::string themeName;
    std::vector<std::unique_ptr<Tile>> tiles;
    std::vector<TileVariant> variants;

    static bool sameSockets(const TileVariant& a, const TileVariant& b) {
        if (a.sockets.size() != b.sockets.size()) return false;
        for (const Socket& s : a.sockets) if (b.socketAt(s.cell, s.face) != s.type) return false;
        return true;
    }
};

} // namespace dungeon
