#pragma once
#include "../chunk/TerrainGenerator.h"
#include "IslandGenerator.h"
#include <memory>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// World types
// ══════════════════════════════════════════════════════════
// Chosen on the New World screen and saved in world.vxrp.
// Adding a type: an entry in the enum and the three tables
// below, and a generator class (derive from TerrainGenerator
// and override column / getSurfaceY / treeInCell / spawn).
//
//   Classic — endless-style rolling hills and forest
//   Island  — one designed island: a mountain quarter, plains,
//             a volcanic coast and a forest with a swamp,
//             joined by a river and a road (worldgen/BiomeCollapse.h)
//   Single biome — one biome all over, at the Classic size, with
//             the features that belong to it. For trying out a
//             biome's ground, blocks and decorations
enum class WorldType : uint8_t { Classic = 0, Island = 1, SingleBiome = 2, COUNT };

inline const char* worldTypeKey(WorldType t) {
    return t == WorldType::Island ? "island" : t == WorldType::SingleBiome ? "single" : "classic";
}
inline const char* worldTypeName(WorldType t) {
    return t == WorldType::Island ? "Island" : t == WorldType::SingleBiome ? "One biome" : "Classic";
}
inline WorldType worldTypeFromKey(const std::string& k) {
    return k == "island" ? WorldType::Island : k == "single" ? WorldType::SingleBiome : WorldType::Classic;
}

// The biomes a single-biome world can be (every land biome)
inline std::vector<int> singleBiomeChoices() {
    std::vector<int> out;
    for (const auto& t : island::tiles())
        if (!island::isSeaBiome(t.biome)) out.push_back(t.biome);
    return out;
}

// World size in chunks on each side
constexpr int CLASSIC_WORLD_SIZE = 32;
constexpr int ISLAND_SIZES[3]    = { 72, 96, 128 };   // small, medium, large
inline const char* islandSizeName(int s) {
    return s <= 72 ? "Small" : s <= 96 ? "Medium" : "Large";
}

inline int worldSizeFor(WorldType t, int requested) {
    if (t != WorldType::Island) return CLASSIC_WORLD_SIZE;
    for (int s : ISLAND_SIZES) if (requested <= s) return s;
    return ISLAND_SIZES[2];
}

// How the ground of an Island or One biome world is shaped
// (New World screen, saved with the world)
enum class TerrainStyle : uint8_t { GuidedRivers = 0, NaturalRivers = 1, NoErosion = 2, COUNT };
inline const char* terrainStyleKey(TerrainStyle s) {
    return s == TerrainStyle::NaturalRivers ? "natural" : s == TerrainStyle::NoErosion ? "plain" : "guided";
}
inline const char* terrainStyleName(TerrainStyle s) {
    return s == TerrainStyle::NaturalRivers ? "Natural rivers" : s == TerrainStyle::NoErosion ? "No erosion" : "Guided rivers";
}
inline TerrainStyle terrainStyleFromKey(const std::string& k) {
    return k == "natural" ? TerrainStyle::NaturalRivers : k == "plain" ? TerrainStyle::NoErosion : TerrainStyle::GuidedRivers;
}

// The generator for a world
//   biome — single-biome worlds: which biome
inline std::unique_ptr<TerrainGenerator> makeGenerator(WorldType t, uint64_t seed, int size, int biome = -1,
                                                       TerrainStyle style = TerrainStyle::GuidedRivers) {
    IslandOptions opt;
    opt.erosion      = style != TerrainStyle::NoErosion;
    opt.guidedRivers = style != TerrainStyle::NaturalRivers;
    if (t == WorldType::Island)
        return std::make_unique<IslandGenerator>((int)(seed % 1000000), size, -1, opt);
    if (t == WorldType::SingleBiome)
        return std::make_unique<IslandGenerator>((int)(seed % 1000000), size, biome >= 0 ? biome : island::Forest, opt);
    return std::make_unique<TerrainGenerator>((int)seed);
}
