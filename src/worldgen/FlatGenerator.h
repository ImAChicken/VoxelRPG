#pragma once
#include "../chunk/TerrainGenerator.h"
#include "../voxel/VoxelRegistry.h"
#include "BiomeTable.h"
#include <cstdlib>
#include <ostream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Flat worlds
// ══════════════════════════════════════════════════════════
// One level field at the Classic size: no hills, water, villages
// or landmarks. A base for test worlds (fights, building, tools).
// The New World screen sets:
//   ground    the block the field is made of (grass has dirt
//             under it; everything rests on stone)
//   trees     trees in patches, as in Classic forests
//   plants    grass and flowers (and the biome's ground patches)
//   decor     bushes, logs and stones
//   biome     which biome's trees, plants and decor grow
// Saved with the world (world.vxrp: flat.* lines).
struct FlatOptions {
    std::string ground = "grass";
    bool trees = false, plants = true, decor = false;
    int  biome = island::Plains;
    int  height = 40;               // the field's surface (voxels up)

    // Blocks a field can be made of (registry names) and their labels
    static const std::vector<std::pair<const char*, const char*>>& grounds() {
        static const std::vector<std::pair<const char*, const char*>> G = {
            { "grass", "Grass" }, { "dirt", "Dirt" }, { "sand", "Sand" }, { "red_sand", "Red sand" },
            { "gravel", "Gravel" }, { "stone", "Stone" }, { "cobblestone", "Cobblestone" },
            { "snow", "Snow" }, { "moss", "Moss" }, { "planks", "Planks" },
        };
        return G;
    }
    static const char* groundLabel(const std::string& name) {
        for (auto& g : grounds()) if (name == g.first) return g.second;
        return "?";
    }
    bool anyDecoration() const { return trees || plants || decor; }

    void write(std::ostream& f) const {
        f << "flat.ground=" << ground << "\n"
          << "flat.trees=" << (trees ? 1 : 0) << "\n"
          << "flat.plants=" << (plants ? 1 : 0) << "\n"
          << "flat.decor=" << (decor ? 1 : 0) << "\n"
          << "flat.biome=" << island::biomeInfo(biome).name << "\n"
          << "flat.height=" << height << "\n";
    }
    bool read(const std::string& key, const std::string& value) {
        if (key == "flat.ground")  { ground = value; return true; }
        if (key == "flat.trees")   { trees = value == "1"; return true; }
        if (key == "flat.plants")  { plants = value == "1"; return true; }
        if (key == "flat.decor")   { decor = value == "1"; return true; }
        if (key == "flat.height")  { height = std::atoi(value.c_str()); return true; }
        if (key == "flat.biome") {
            for (int b = 0; b < island::BIOME_COUNT; b++) if (value == island::biomeInfo(b).name) biome = b;
            return true;
        }
        return false;
    }
};

class FlatGenerator : public TerrainGenerator {
public:
    FlatGenerator(int seed, int sizeChunks, const FlatOptions& o)
        : TerrainGenerator(seed), opt(o), size(sizeChunks) {
        opt.height = std::clamp(opt.height, 8, WORLD_HEIGHT - 64);
        groundId = gRegistry.getIDByName(opt.ground);
        if (!groundId) groundId = BLOCK_GRASS;
        plantScale = opt.plants ? 1.0f : 0.0f;
        largeScale = opt.decor ? 1.0f : 0.0f;
        landmarkScale = 0.0f;                     // no landmarks: a plain field
    }

    TerrainColumn column(int wx, int wz) const override {
        (void)wx; (void)wz;
        TerrainColumn c;
        c.ground   = opt.height;
        c.top      = groundId;
        c.sub      = groundId == BLOCK_GRASS ? BLOCK_DIRT : groundId;
        c.subDepth = DIRT_DEPTH;
        c.deep     = BLOCK_STONE;
        // the biome only matters for what grows (and its ground patches)
        c.eco      = (opt.plants || opt.decor) ? (uint8_t)opt.biome : decor::ECO_NONE;
        return c;
    }
    int getSurfaceY(int wx, int wz) const override { (void)wx; (void)wz; return opt.height; }
    float forestCover(int wx, int wz) const override {
        return opt.trees ? TerrainGenerator::forestCover(wx, wz) : 0.0f;
    }
    glm::ivec3 spawnPoint(int worldSizeX, int worldSizeZ) const override {
        return { worldSizeX * VOXEL_DIM / 2, opt.height, worldSizeZ * VOXEL_DIM / 2 };
    }

    const FlatOptions& options() const { return opt; }

protected:
    // Trees: Classic's forest patches; the biome's own trees where
    // it has them (decorations.toml), else the usual kinds
    bool treeInCell(int cellX, int cellZ, int& wx, int& wz, int& type) const override {
        if (!opt.trees) return false;
        if (!TerrainGenerator::treeInCell(cellX, cellZ, wx, wz, type)) return false;
        if (!inside(wx, wz, 8)) return false;
        if (decorSet && !firstTreeOfDef.empty()) {
            float roll = cellRandom(cellX, cellZ, 5), acc = 0.0f;
            for (const auto& [di, share] : decorSet->list(decor::Layer::Tree, (uint8_t)opt.biome)) {
                acc += share;
                if (roll >= acc) continue;
                int first = firstTreeOfDef[(size_t)di];
                int nv = (int)decorSet->defs[(size_t)di].variants.size();
                if (first >= 0 && nv > 0) {
                    int t = first + (int)(cellRandom(cellX, cellZ, 6) * nv) % nv;
                    if (opt.height + trees[(size_t)t].height <= WORLD_HEIGHT) type = t;
                }
                break;
            }
        }
        return true;
    }

    // Bushes, logs and stones: the biome's [[decor]] entries
    bool decorInCell(int cellX, int cellZ, DecorPlace& out) const override {
        if (!decorSet || largeScale <= 0.0f) return false;
        const int margin = 1, span = DECOR_CELL - margin * 2;
        float roll = cellRandom(cellX, cellZ, 31) / largeScale;
        if (roll > 0.6f) return false;
        int wx = cellX * DECOR_CELL + margin + (int)(cellRandom(cellX, cellZ, 32) * span);
        int wz = cellZ * DECOR_CELL + margin + (int)(cellRandom(cellX, cellZ, 33) * span);
        if (!inside(wx, wz, 4)) return false;
        const auto& list = decorSet->list(decor::Layer::Large, (uint8_t)opt.biome);
        float acc = 0.0f;
        int pick = -1;
        for (const auto& [di, dens] : list) { acc += dens; if (roll < acc) { pick = di; break; } }
        if (pick < 0) return false;
        const decor::Def& d = decorSet->defs[(size_t)pick];
        if (d.variants.empty()) return false;
        if (std::find(d.on.begin(), d.on.end(), groundId) == d.on.end()) return false;   // grows on this ground?
        int var = (int)(cellRandom(cellX, cellZ, 35) * d.variants.size()) % (int)d.variants.size();
        out.shape = &d.variants[(size_t)var];
        out.wx = wx; out.wz = wz; out.wy = opt.height;
        return out.wy + out.shape->top <= WORLD_HEIGHT;
    }

private:
    FlatOptions opt;
    int size = 32;
    uint16_t groundId = BLOCK_GRASS;

    bool inside(int wx, int wz, int margin) const {
        const int W = size * VOXEL_DIM;
        return wx >= margin && wz >= margin && wx < W - margin && wz < W - margin;
    }
};
