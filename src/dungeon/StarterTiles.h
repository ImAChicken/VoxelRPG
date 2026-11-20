#pragma once
// ══════════════════════════════════════════════════════════
// The starter tiles: a plain but complete set, made in code
// ══════════════════════════════════════════════════════════
// So the generator has something to grow a dungeon from before
// anyone has built tiles by hand (docs/dungeons.md: "a generated
// starter set … hand-built tiles replace them over time").
// Console: tile starter [theme]  writes them to
// data/dungeons/<theme>/tiles/, leaving alone any tile already
// there with the same name unless told to (tile starter crypt over).
//
// Every tile here follows the same rules, which hand-built ones
// should too: walls WALL thick on the tile's own edges, a floor
// FLOOR thick, a ceiling WALL thick, doorways DOOR_W × DOOR_H
// centred on their half cell and standing on the floor.

#include "Tile.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dungeon {

class TileBuilder {
public:
    Tile t;

    TileBuilder(const std::string& name, const std::string& theme, const std::string& kind, glm::ivec3 cells) {
        t.name = name;
        t.info.theme = theme;
        t.info.kind = kind;
        t.info.cells = cells;
        t.vol.resize(cells * HALF);
        t.vol.anchor = { 0, 0, 0 };
        wall = id("stone_bricks"); wall2 = id("moss_stone"); wall3 = id("cracked_stone");
        floorB = id("cobblestone"); ceil = id("stone"); torchB = id("torch");
        // solid all through, then carve
        glm::ivec3 s = t.vol.size;
        for (int y = 0; y < s.y; y++) for (int z = 0; z < s.z; z++) for (int x = 0; x < s.x; x++) set(x, y, z, wallAt(x, y, z));
    }

    // the room inside its walls, floor to ceiling
    TileBuilder& hollow() {
        glm::ivec3 s = t.vol.size;
        carve({ WALL, FLOOR, WALL }, { s.x - WALL, s.y - WALL, s.z - WALL });
        for (int z = 0; z < s.z; z++) for (int x = 0; x < s.x; x++)
            for (int y = 0; y < FLOOR; y++) set(x, y, z, floorB);
        return *this;
    }
    // air in [lo, hi)
    TileBuilder& carve(glm::ivec3 lo, glm::ivec3 hi) {
        for (int y = lo.y; y < hi.y; y++) for (int z = lo.z; z < hi.z; z++) for (int x = lo.x; x < hi.x; x++) set(x, y, z, BLOCK_AIR);
        return *this;
    }
    TileBuilder& fill(glm::ivec3 lo, glm::ivec3 hi, uint16_t b = 0) {
        for (int y = lo.y; y < hi.y; y++) for (int z = lo.z; z < hi.z; z++) for (int x = lo.x; x < hi.x; x++)
            set(x, y, z, b ? b : wallAt(x, y, z));
        return *this;
    }

    // A doorway in the face 'face' of half cell 'cell' (cut through
    // the wall, from the tile's edge into the room)
    TileBuilder& door(glm::ivec3 cell, int face, const std::string& type = "door") {
        t.info.setSocket(cell, face, type);
        glm::ivec3 o = cell * HALF;
        int y0 = o.y + FLOOR, y1 = y0 + DOOR_H;
        int a0 = (HALF - DOOR_W) / 2, a1 = a0 + DOOR_W;
        glm::ivec3 s = t.vol.size;
        int depth = WALL + 1;
        switch (face) {
        case North: carve({ o.x + a0, y0, 0 },            { o.x + a1, y1, depth }); break;
        case South: carve({ o.x + a0, y0, s.z - depth },  { o.x + a1, y1, s.z }); break;
        case West:  carve({ 0, y0, o.z + a0 },            { depth, y1, o.z + a1 }); break;
        case East:  carve({ s.x - depth, y0, o.z + a0 },  { s.x, y1, o.z + a1 }); break;
        default: break;
        }
        return *this;
    }
    // A doorway filled with a secret wall (it still joins: break it to get through)
    TileBuilder& secretDoor(glm::ivec3 cell, int face) {
        door(cell, face, "door");
        glm::ivec3 o = cell * HALF;
        int y0 = o.y + FLOOR, y1 = y0 + DOOR_H, a0 = (HALF - DOOR_W) / 2, a1 = a0 + DOOR_W;
        glm::ivec3 s = t.vol.size;
        uint16_t sec = markerBlock("secret");
        auto put = [&](glm::ivec3 lo, glm::ivec3 hi) { fill(lo, hi, sec); };
        switch (face) {
        case North: put({ o.x + a0, y0, WALL - 1 }, { o.x + a1, y1, WALL }); break;
        case South: put({ o.x + a0, y0, s.z - WALL }, { o.x + a1, y1, s.z - WALL + 1 }); break;
        case West:  put({ WALL - 1, y0, o.z + a0 }, { WALL, y1, o.z + a1 }); break;
        case East:  put({ s.x - WALL, y0, o.z + a0 }, { s.x - WALL + 1, y1, o.z + a1 }); break;
        default: break;
        }
        return *this;
    }
    TileBuilder& torch(int x, int y, int z) { set(x, y, z, torchB); return *this; }
    // torches on the inside of the walls, every 'every' voxels, at 'h' up
    TileBuilder& torchesAround(int every, int h = 14) {
        glm::ivec3 s = t.vol.size;
        for (int x = WALL + every / 2; x < s.x - WALL; x += every) { torchIfWall(x, FLOOR + h, WALL, 0, -1); torchIfWall(x, FLOOR + h, s.z - WALL - 1, 0, 1); }
        for (int z = WALL + every / 2; z < s.z - WALL; z += every) { torchIfWall(WALL, FLOOR + h, z, -1, 0); torchIfWall(s.x - WALL - 1, FLOOR + h, z, 1, 0); }
        return *this;
    }
    TileBuilder& pillar(int x, int z, int w) {
        return fill({ x, FLOOR, z }, { x + w, t.vol.size.y - WALL, z + w }, wall);
    }
    TileBuilder& marker(const std::string& kind, glm::ivec3 at, std::map<std::string, std::string> props = {}) {
        set(at.x, at.y, at.z, markerBlock(kind));
        Marker m; m.at = at; m.kind = kind; m.props = std::move(props);
        t.info.markers.push_back(m);
        return *this;
    }
    TileBuilder& spawner(glm::ivec3 at, const std::string& what, int lo, int hi, const std::string& rule = "enter") {
        return marker("spawner", at, { { "spawn", what }, { "count", std::to_string(lo) + " " + std::to_string(hi) }, { "rule", rule } });
    }
    TileBuilder& weight(float w) { t.info.weight = w; return *this; }
    TileBuilder& counts(int lo, int hi) { t.info.minCount = lo; t.info.maxCount = hi; return *this; }
    TileBuilder& noTurn() { t.info.rotate = false; return *this; }

    glm::ivec3 size() const { return t.vol.size; }
    void set(int x, int y, int z, uint16_t b) { t.vol.set(x, y, z, b); }
    uint16_t at(int x, int y, int z) const { return t.vol.at(x, y, z); }

private:
    uint16_t wall = 0, wall2 = 0, wall3 = 0, floorB = 0, ceil = 0, torchB = 0;
    static uint16_t id(const char* n) { uint16_t i = gRegistry.getIDByName(n); return i ? i : BLOCK_STONE; }
    // the wall's stones: mostly bricks, some mossy or cracked (the same
    // pattern wherever the tile goes: by position in the tile)
    uint16_t wallAt(int x, int y, int z) const {
        uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(z * 83492791);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        int r = (int)(h % 100);
        if (y >= t.vol.size.y - WALL) return r < 85 ? ceil : wall3;
        return r < 8 ? wall2 : r < 14 ? wall3 : wall;
    }
    void torchIfWall(int x, int y, int z, int dx, int dz) {
        (void)dx; (void)dz;
        if (at(x, y, z) == BLOCK_AIR && at(x + dx, y, z + dz) != BLOCK_AIR && at(x, y - 1, z) == BLOCK_AIR) torch(x, y, z);
    }
};

// The crypt's starter tiles
inline std::vector<Tile> starterCrypt() {
    const std::string th = "crypt";
    std::vector<Tile> out;
    auto keep = [&](TileBuilder& b) { out.push_back(b.t); };
    auto mid = [](TileBuilder& b) { glm::ivec3 s = b.size(); return glm::ivec3(s.x / 2, FLOOR, s.z / 2); };

    // ── corridors: 1 half cell wide (12 inside), 2 tall ──
    { TileBuilder b("corridor_straight", th, "corridor", { 1, 2, 2 });
      b.hollow().door({ 0, 0, 0 }, North).door({ 0, 0, 1 }, South).torch(WALL, FLOOR + 14, 16).weight(3.0f); keep(b); }
    { TileBuilder b("corridor_long", th, "corridor", { 1, 2, 4 });
      b.hollow().door({ 0, 0, 0 }, North).door({ 0, 0, 3 }, South).torch(WALL, FLOOR + 14, 20).torch(16 - WALL - 1, FLOOR + 14, 44).weight(1.5f); keep(b); }
    { TileBuilder b("corridor_corner", th, "corridor", { 1, 2, 1 });
      b.hollow().door({ 0, 0, 0 }, North).door({ 0, 0, 0 }, East).weight(2.0f); keep(b); }
    { TileBuilder b("corridor_t", th, "corridor", { 1, 2, 1 });
      b.hollow().door({ 0, 0, 0 }, North).door({ 0, 0, 0 }, East).door({ 0, 0, 0 }, West).weight(1.0f); keep(b); }
    { TileBuilder b("corridor_cross", th, "corridor", { 1, 2, 1 });
      b.hollow().door({ 0, 0, 0 }, North).door({ 0, 0, 0 }, East).door({ 0, 0, 0 }, South).door({ 0, 0, 0 }, West).weight(0.5f); keep(b); }
    { TileBuilder b("corridor_end", th, "corridor", { 1, 2, 2 });
      // a dead end with a coffin in the wall: someone's still in it
      b.hollow().door({ 0, 0, 1 }, South).marker("coffin", { 8, FLOOR, WALL }, { { "spawn", "undead" }, { "rule", "enter" } }).weight(0.4f); keep(b); }

    // ── rooms on the main path ───────────────────────────
    { TileBuilder b("room_small", th, "room", { 2, 2, 2 });
      b.hollow().door({ 0, 0, 0 }, North).door({ 1, 0, 1 }, South).torchesAround(14)
       .spawner(mid(b), "undead", 2, 3).weight(1.5f); keep(b); }
    { TileBuilder b("room_hall", th, "room", { 4, 2, 4 });
      b.hollow().door({ 1, 0, 0 }, North).door({ 2, 0, 3 }, South).door({ 3, 0, 1 }, East).door({ 0, 0, 2 }, West)
       .pillar(18, 18, 4).pillar(42, 18, 4).pillar(18, 42, 4).pillar(42, 42, 4).torchesAround(16)
       .spawner(mid(b), "undead", 3, 5).weight(1.0f); keep(b); }
    { TileBuilder b("room_pillars", th, "room", { 4, 3, 4 });
      b.hollow().door({ 1, 0, 0 }, North).door({ 2, 0, 3 }, South);
      for (int x = 12; x < 56; x += 14) for (int z = 12; z < 56; z += 14) b.pillar(x, z, 3);
      b.torchesAround(14, 18).spawner(mid(b), "undead", 4, 6).weight(0.8f); keep(b); }
    { TileBuilder b("room_gallery", th, "room", { 2, 2, 6 });
      // a long burial gallery: coffins in niches down both walls
      b.hollow().door({ 0, 0, 0 }, North).door({ 1, 0, 5 }, South);
      for (int z = 14; z < 84; z += 16) {
          b.carve({ 0, FLOOR, z }, { WALL, FLOOR + 8, z + 6 }).carve({ 32 - WALL, FLOOR, z }, { 32, FLOOR + 8, z + 6 });
          b.fill({ 0, FLOOR, z }, { 1, FLOOR + 8, z + 6 }).fill({ 31, FLOOR, z }, { 32, FLOOR + 8, z + 6 });
          b.marker("coffin", { 1, FLOOR, z + 3 }, { { "spawn", "undead" }, { "rule", "enter" } });
          b.marker("coffin", { 30, FLOOR, z + 3 }, { { "spawn", "undead" }, { "rule", "enter" } });
      }
      b.torchesAround(20).weight(0.8f); keep(b); }
    { TileBuilder b("room_junction", th, "room", { 2, 2, 2 });
      b.hollow().door({ 0, 0, 0 }, North).door({ 1, 0, 0 }, East).door({ 1, 0, 1 }, South).door({ 0, 0, 1 }, West)
       .torchesAround(14).spawner(mid(b), "undead", 1, 3).weight(1.0f); keep(b); }

    // ── side rooms: one way in ────────────────────────────
    { TileBuilder b("side_treasure", th, "side", { 3, 2, 3 });
      b.hollow().door({ 1, 0, 2 }, South).torchesAround(14)
       .marker("chest", { 24, FLOOR, WALL + 2 }).spawner({ 24, FLOOR, 28 }, "undead", 3, 5).weight(1.0f); keep(b); }
    { TileBuilder b("side_ossuary", th, "side", { 2, 2, 3 });
      b.hollow().door({ 0, 0, 2 }, South).torchesAround(16);
      for (int z = 8; z < 40; z += 10) b.marker("coffin", { WALL, FLOOR, z }, { { "spawn", "undead" }, { "rule", "enter" } });
      b.weight(0.8f); keep(b); }
    { TileBuilder b("side_secret", th, "side", { 2, 2, 2 });
      // behind a wall that breaks: a chest, nobody home
      b.hollow().secretDoor({ 0, 0, 1 }, South).torch(WALL, FLOOR + 14, 16).marker("chest", { 16, FLOOR, WALL + 2 }).weight(0.5f).counts(0, 2); keep(b); }

    // ── the stair down: in at the bottom (south), out at the top (north) ──
    { TileBuilder b("stair_down", th, "stair", { 1, 4, 4 });
      glm::ivec3 s = b.size();
      for (int z = WALL; z < s.z - WALL; z++) {
          // flat landings at both ends, steps of 2 up every 3 between
          int up = std::clamp((s.z - WALL - 8 - z) / 3, 0, 16) * 2;     // 0 at the south end .. 32 at the north
          int fy = FLOOR + up;
          for (int x = 0; x < s.x; x++) for (int y = 0; y < fy; y++) if (x >= WALL && x < s.x - WALL) b.set(x, y, z, gRegistry.getIDByName("cobblestone"));
          b.carve({ WALL, fy, z }, { s.x - WALL, std::min(fy + 28, s.y - WALL), z + 1 });
      }
      b.door({ 0, 0, 3 }, South).door({ 0, 2, 0 }, North).torch(WALL, FLOOR + 30, 32).counts(0, 3).weight(1.0f); keep(b); }

    // ── the portal room: you arrive here, and leave ──────
    { TileBuilder b("portal_room", th, "portal_room", { 4, 2, 4 });
      b.hollow().door({ 1, 0, 3 }, South).pillar(10, 10, 4).pillar(50, 10, 4).pillar(10, 50, 4).pillar(50, 50, 4)
       .torchesAround(12).marker("portal", { 32, FLOOR, 24 }).counts(1, 1).weight(1.0f); keep(b); }

    // ── the boss's arena ──────────────────────────────────
    { TileBuilder b("boss_arena", th, "boss", { 6, 3, 6 });
      b.hollow().door({ 2, 0, 5 }, South);
      for (int i = 0; i < 8; i++) {
          float a = i * 0.785398f;
          int x = 48 + (int)std::round(std::cos(a) * 30.0f) - 2, z = 48 + (int)std::round(std::sin(a) * 30.0f) - 2;
          b.pillar(x, z, 4);
      }
      for (int x : { 12, 84 }) for (int z : { 20, 48, 76 })
          b.marker("coffin", { x, FLOOR, z }, { { "spawn", "undead" }, { "rule", "boss" } });
      b.torchesAround(12, 20).marker("boss", { 48, FLOOR, 40 }, { { "spawn", "crypt_lord" } })
       .marker("exit", { 48, FLOOR, 14 }).counts(1, 1).weight(1.0f); keep(b); }
    return out;
}

inline std::vector<Tile> starterTiles(const std::string& theme) {
    if (theme == "crypt") return starterCrypt();
    return {};
}

// Writes them out; 'over': replace ones already there. Returns how many written
inline int writeStarterTiles(const std::string& theme, bool over, std::string& report) {
    std::vector<Tile> ts = starterTiles(theme);
    if (ts.empty()) { report = "no starter tiles for " + theme; return 0; }
    std::string dir = TileSet::dirFor(theme);
    int n = 0, skipped = 0;
    for (const Tile& t : ts) {
        std::error_code ec;
        if (!over && std::filesystem::exists(std::filesystem::path(dir) / (t.name + ".toml"), ec)) { skipped++; continue; }
        std::string why;
        if (TileSet::saveTile(t, dir, why)) n++;
        else report += t.name + ": " + why + "\n";
    }
    report += std::to_string(n) + " written to " + dir + (skipped ? ", " + std::to_string(skipped) + " already there (tile starter " + theme + " over replaces them)" : "");
    return n;
}

} // namespace dungeon
