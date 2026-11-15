#pragma once
#include "VoxelVolume.h"
#include "../chunk/StructureGenerator.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// StructureCatalog — every placeable multi-block structure
// ══════════════════════════════════════════════════════════
// Feeds the dev menu's Structures page. Two kinds:
//   - built in: the trees the terrain generator grows, built
//     by the same functions (chunk/StructureGenerator.h), so
//     a placed tree is identical to a natural one
//   - saved:    data/structures/*.vxvol, made with /save
//
// Each entry builds a VoxelVolume on demand; the dev menu
// puts it in the clipboard and the Paste tool places it.

// A structure chunk → just the box around its blocks, with
// the anchor on the trunk base (the structure's origin), so
// the trunk lands on the block you paste onto.
inline VoxelVolume volumeFromStructureChunk(const Chunk& c) {
    glm::ivec3 lo(VOXEL_DIM), hi(-1);
    for (int y = 0; y < VOXEL_DIM; y++)
    for (int x = 0; x < VOXEL_DIM; x++)
    for (int z = 0; z < VOXEL_DIM; z++)
        if (c.typeAt(x, y, z) != BLOCK_AIR) {
            lo = glm::min(lo, glm::ivec3(x, y, z));
            hi = glm::max(hi, glm::ivec3(x, y, z));
        }
    if (hi.x < 0) return VoxelVolume();          // empty
    VoxelVolume vol(hi - lo + glm::ivec3(1));
    for (int y = lo.y; y <= hi.y; y++)
    for (int z = lo.z; z <= hi.z; z++)
    for (int x = lo.x; x <= hi.x; x++)
        vol.set(x - lo.x, y - lo.y, z - lo.z, c.typeAt(x, y, z));
    vol.anchor = glm::ivec3(STRUCTURE_ORIGIN_X, 0, STRUCTURE_ORIGIN_Z) - lo;
    return vol;
}

struct StructureEntry {
    std::string name;       // shown in the menu
    bool        builtIn;    // a tree (true) or a saved file
    std::string file;       // saved: name without .vxvol
    std::function<ChunkPtr()> make;   // built in: generator
};

inline std::vector<StructureEntry> structureCatalog() {
    std::vector<StructureEntry> list = {
        { "Oak Tree",    true, "", [] { return generateOakTree();    } },
        { "Pine Tree",   true, "", [] { return generatePineTree();   } },
        { "Dead Tree",   true, "", [] { return generateDeadTree();   } },
        { "Ancient Oak", true, "", [] { return generateAncientOak(); } },
    };
    std::error_code ec;
    std::vector<std::string> saved;
    for (auto& e : std::filesystem::directory_iterator("data/structures", ec))
        if (e.path().extension() == ".vxvol")
            saved.push_back(e.path().stem().string());
    std::sort(saved.begin(), saved.end());
    for (auto& n : saved) list.push_back({ n, false, n, nullptr });
    return list;
}
