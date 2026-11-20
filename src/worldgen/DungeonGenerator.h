#pragma once
// ══════════════════════════════════════════════════════════
// The Dungeon world type (docs/dungeons.md, step 2)
// ══════════════════════════════════════════════════════════
// Solid rock with a dungeon grown from tiles (dungeon/Layout.h) in
// the middle of its height, and nothing else: no surface to reach,
// so you're boxed in. You arrive in the portal room. Lit only by what
// the tiles hold (torches): the rock above keeps the sky out.
//
// The layout is made once, when the world opens, from the theme's
// tiles (data/dungeons/<theme>/tiles) and the world's seed; every
// chunk then stamps whatever tiles reach into it. Markers turn into
// air for now (step 3 brings them alive), a secret wall into cracked
// stone, and a door that nothing could be put behind is walled up.

#include "../chunk/TerrainGenerator.h"
#include "../dungeon/Layout.h"
#include <memory>
#include <string>

class DungeonGenerator : public TerrainGenerator {
public:
    static constexpr int SIZE_CHUNKS = 16;     // the world, chunks a side (2048 voxels)
    static constexpr int GRID_Y      = 384;    // world y of the grid's bottom
    static constexpr int ROCK_TOP    = 896;    // the rock reaches this high

    DungeonGenerator(uint64_t seed, int sizeChunks, const std::string& theme = "crypt")
        : TerrainGenerator((int)(seed % 1000000)), themeName(theme) {
        decorSet = nullptr;                    // no plants or decorations
        tiles.load(theme, &loadReport);
        ok = tiles.all().size() > 0 && layout.generate(tiles, seed);
        int side = sizeChunks * VOXEL_DIM;
        origin = glm::ivec3(side / 2 - layout.p.grid.x * dungeon::HALF / 2, GRID_Y, side / 2 - layout.p.grid.z * dungeon::HALF / 2);
        wall = gRegistry.getIDByName("stone_bricks");
        cracked = gRegistry.getIDByName("cracked_stone");
        stoneId = gRegistry.getIDByName("stone");
        if (!wall) wall = BLOCK_STONE;
        if (!cracked) cracked = wall;
        if (!stoneId) stoneId = BLOCK_STONE;
        // each tile's box in world voxels, for the chunks to find them fast
        for (const auto& t : layout.tiles) {
            const auto& v = layout.variantOf(t);
            glm::ivec3 lo = origin + t.at * dungeon::HALF;
            boxes.push_back({ lo, lo + v.vol.size });
        }
    }

    bool generated() const { return ok; }
    const dungeon::Layout& dungeonLayout() const { return layout; }
    const std::string& theme() const { return themeName; }
    glm::ivec3 gridOrigin() const { return origin; }
    const std::string& tileReport() const { return loadReport; }
    // A tile's low corner in world voxels
    glm::ivec3 worldOf(const dungeon::PlacedTile& t) const { return origin + t.at * dungeon::HALF; }

    TerrainColumn column(int wx, int wz) const override {
        (void)wx; (void)wz;
        TerrainColumn c;
        c.ground = ROCK_TOP;
        c.top = c.sub = c.deep = stoneId;
        c.subDepth = 0;
        c.waterLevel = 0;
        c.eco = decor::ECO_NONE;
        return c;
    }
    int getSurfaceY(int wx, int wz) const override { (void)wx; (void)wz; return ROCK_TOP; }
    bool treeInCell(int, int, int&, int&, int&) const override { return false; }

    bool carvesInto(int cx, int cz, int sy) const override {
        glm::ivec3 lo(cx * VOXEL_DIM, sy * VOXEL_DIM, cz * VOXEL_DIM), hi = lo + glm::ivec3(VOXEL_DIM);
        for (const Box& b : boxes)
            if (b.lo.x < hi.x && b.hi.x > lo.x && b.lo.y < hi.y && b.hi.y > lo.y && b.lo.z < hi.z && b.hi.z > lo.z) return true;
        return false;
    }

    void carveSection(uint16_t* vox, int x0, int z0, int y0, const std::vector<TerrainColumn>& cols) const override {
        (void)cols;
        const int D = VOXEL_DIM;
        glm::ivec3 slo(x0, y0, z0), shi = slo + glm::ivec3(D);
        for (size_t i = 0; i < layout.tiles.size(); i++) {
            const Box& b = boxes[i];
            glm::ivec3 lo = glm::max(b.lo, slo), hi = glm::min(b.hi, shi);
            if (lo.x >= hi.x || lo.y >= hi.y || lo.z >= hi.z) continue;
            const VoxelVolume& vol = layout.variantOf(layout.tiles[i]).vol;
            for (int y = lo.y; y < hi.y; y++)
            for (int z = lo.z; z < hi.z; z++)
            for (int x = lo.x; x < hi.x; x++) {
                uint16_t v = vol.at(x - b.lo.x, y - b.lo.y, z - b.lo.z);
                std::string mk = dungeon::markerKindOf(v);
                if (!mk.empty()) v = mk == "secret" ? cracked : BLOCK_AIR;
                vox[((size_t)(y - y0) * D + (x - x0)) * D + (z - z0)] = v;
            }
        }
        // doors nothing could go behind: walled up
        for (const dungeon::OpenDoor& d : layout.sealed) {
            glm::ivec3 c = origin + d.cell * dungeon::HALF;
            const int H = dungeon::HALF, a0 = (H - dungeon::DOOR_W) / 2, a1 = a0 + dungeon::DOOR_W;
            int y0d = c.y + dungeon::FLOOR, y1d = y0d + dungeon::DOOR_H, dep = dungeon::WALL + 1;
            glm::ivec3 lo, hi;
            switch (d.face) {
            case dungeon::North: lo = { c.x + a0, y0d, c.z };           hi = { c.x + a1, y1d, c.z + dep }; break;
            case dungeon::South: lo = { c.x + a0, y0d, c.z + H - dep }; hi = { c.x + a1, y1d, c.z + H }; break;
            case dungeon::West:  lo = { c.x, y0d, c.z + a0 };           hi = { c.x + dep, y1d, c.z + a1 }; break;
            case dungeon::East:  lo = { c.x + H - dep, y0d, c.z + a0 }; hi = { c.x + H, y1d, c.z + a1 }; break;
            default: continue;
            }
            lo = glm::max(lo, slo); hi = glm::min(hi, shi);
            for (int y = lo.y; y < hi.y; y++) for (int z = lo.z; z < hi.z; z++) for (int x = lo.x; x < hi.x; x++)
                vox[((size_t)(y - y0) * D + (x - x0)) * D + (z - z0)] = wall;
        }
    }

    // You arrive in the portal room, beside its portal
    glm::ivec3 spawnPoint(int worldSizeX, int worldSizeZ) const override {
        if (layout.portal < 0) return { worldSizeX * VOXEL_DIM / 2, ROCK_TOP, worldSizeZ * VOXEL_DIM / 2 };
        const auto& t = layout.tiles[(size_t)layout.portal];
        const auto& v = layout.variantOf(t);
        glm::ivec3 lo = worldOf(t);
        for (const dungeon::Marker& m : v.markers)
            if (m.kind == "portal") return lo + m.at + glm::ivec3(0, 0, 0) + glm::ivec3(6, 0, 6);
        return lo + glm::ivec3(v.vol.size.x / 2, dungeon::FLOOR, v.vol.size.z / 2);
    }

private:
    struct Box { glm::ivec3 lo, hi; };
    std::string themeName, loadReport;
    dungeon::TileSet tiles;
    dungeon::Layout layout;
    bool ok = false;
    glm::ivec3 origin{0};
    std::vector<Box> boxes;
    uint16_t wall = 0, cracked = 0, stoneId = 0;
};

// The open world's generator when it's a Dungeon world (set each frame
// by the game; the debug view and the console read it)
inline const DungeonGenerator* gDungeonWorld = nullptr;
