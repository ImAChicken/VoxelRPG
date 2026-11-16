#pragma once
#include "ItemRegistry.h"
#include "StructureRegistry.h"
#include "../voxel/VoxelRegistry.h"
#include "../world/Chests.h"
#include "../world/StructureManager.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// DevKit — chests for staging dev worlds
// ══════════════════════════════════════════════════════════
// A dev world is a stage for enemy and item showcases. These
// helpers fill a chest with every item of a kind, and lay out a
// row of labelled SHOWCASE chests (taking from one gives you a
// copy; it never runs out: game/Container.h) — Weapons, Armour,
// Tools, Blocks, Decor. Used by the 'chest' console command and
// by New World → Kit: Showcase chests.
namespace devkit {

// "weapons", "armor" / "armour", "tools", "blocks", "decor",
// "dev", "all" → item categories (0: not a category)
inline uint32_t categoryFromName(std::string n) {
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    if (n == "weapons" || n == "weapon") return IC_WEAPON;
    if (n == "armor" || n == "armour" || n == "worn" || n == "gear") return IC_WORN;
    if (n == "tools" || n == "tool") return IC_TOOL;
    if (n == "blocks" || n == "block") return IC_BLOCK;
    if (n == "decor" || n == "plants") return IC_DECOR;
    if (n == "dev") return IC_DEV;
    if (n == "all") return IC_WEAPON | IC_WORN | IC_TOOL | IC_BLOCK | IC_DECOR;
    return 0;
}

// A stack of an item worth showing: one tool, up to 99 blocks
inline int showCount(uint16_t id) { return std::clamp<int>(gItems.get(id).maxStack, 1, 99); }

// One stack of each item in 'cats' not already in it, until it's
// full. Returns how many stacks went in.
inline int fill(Inventory& inv, uint32_t cats) {
    int n = 0;
    for (uint16_t id : gItems.inCategory(cats)) {
        if (inv.count(id) > 0) continue;
        if (inv.freeSlot() < 0) break;
        if (inv.add(id, showCount(id)) == 0) n++;
    }
    return n;
}

struct Shelf { const char* label; uint32_t cats; };
inline const std::vector<Shelf>& shelves() {
    static const std::vector<Shelf> s = {
        { "Weapons", IC_WEAPON }, { "Armour", IC_WORN }, { "Tools", IC_TOOL },
        { "Blocks", IC_BLOCK },   { "Decor", IC_DECOR },
    };
    return s;
}

// The quarter turn that puts a chest's lock toward 'toward'
// (a flat direction)
inline int turnFacing(const StructureType& t, glm::vec3 toward) {
    uint16_t lock = gRegistry.getIDByName("chest_lock");
    int best = 0; float bestDot = -2.0f;
    for (int r = 0; r < 4; r++) {
        const VoxelVolume& v = t.shape(0, r);
        glm::vec3 sum(0.0f); int cnt = 0;
        for (int y = 0; y < v.size.y; y++)
            for (int z = 0; z < v.size.z; z++)
                for (int x = 0; x < v.size.x; x++)
                    if (v.at(x, y, z) == lock) { sum += glm::vec3(x, 0, z); cnt++; }
        if (!cnt) return 0;
        glm::vec3 d = sum / (float)cnt - glm::vec3(v.size.x - 1, 0, v.size.z - 1) * 0.5f;
        if (glm::length(d) < 1e-3f) continue;
        float dot = glm::dot(glm::normalize(d), toward);
        if (dot > bestDot) { bestDot = dot; best = r; }
    }
    return best;
}

// Lay out the showcase row in front of 'feet' (looking along
// 'front'), each chest's lock toward you. Spots that are blocked
// are skipped. Returns how many were placed.
inline int placeKit(StructureManager& structs, ChestStore& chests, glm::vec3 feet, glm::vec3 front,
                    const std::function<void(const std::string&)>& log) {
    uint16_t type = gStructures.find("wooden_chest");
    const StructureType* t = gStructures.get(type);
    if (!t) { if (log) log("no wooden_chest in structures.toml"); return 0; }
    glm::vec3 f(front.x, 0.0f, front.z);
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    glm::vec3 right(-f.z, 0.0f, f.x);
    int rot = turnFacing(*t, -f);
    std::vector<Shelf> list;                       // the shelves with something to show
    for (const Shelf& sh : shelves()) if (!gItems.inCategory(sh.cats).empty()) list.push_back(sh);
    int placed = 0;
    for (size_t i = 0; i < list.size(); i++) {
        float side = ((float)i - (float)(list.size() - 1) * 0.5f) * 15.0f;   // 5 ft apart
        glm::vec3 p = feet + f * 24.0f + right * side;
        glm::ivec3 at((int)std::floor(p.x), (int)std::floor(feet.y), (int)std::floor(p.z));
        if (!structs.checkPlace(type, at, rot, false).empty()) { if (log) log(std::string("no room for ") + list[i].label); continue; }
        StructureInstance* s = structs.place(type, at, rot, EditSource::Command, 0);
        if (!s) continue;
        Container& c = chests.get(s->id, *t);
        c.makeEmpty();
        c.label = list[i].label;
        c.setShowcase(true);
        fill(c.read(), list[i].cats);
        placed++;
    }
    return placed;
}

} // namespace devkit
