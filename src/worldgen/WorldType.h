#pragma once
#include "../chunk/TerrainGenerator.h"
#include "IslandGenerator.h"
#include <memory>
#include <string>

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
//             joined by a river and a road (worldgen/IslandChart.h)
enum class WorldType : uint8_t { Classic = 0, Island = 1, COUNT };

inline const char* worldTypeKey(WorldType t) {
    return t == WorldType::Island ? "island" : "classic";
}
inline const char* worldTypeName(WorldType t) {
    return t == WorldType::Island ? "Island" : "Classic";
}
inline WorldType worldTypeFromKey(const std::string& k) {
    return k == "island" ? WorldType::Island : WorldType::Classic;
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

// The generator for a world
inline std::unique_ptr<TerrainGenerator> makeGenerator(WorldType t, uint64_t seed, int size) {
    if (t == WorldType::Island)
        return std::make_unique<IslandGenerator>((int)(seed % 1000000), size);
    return std::make_unique<TerrainGenerator>((int)seed);
}
