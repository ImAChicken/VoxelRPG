#pragma once
#include "../voxel/VoxelRegistry.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// ItemRegistry — every thing that can sit in an inventory
// ══════════════════════════════════════════════════════════
// The item equivalent of the block registry. Two sources:
//
//   1. Every block in blocks.toml becomes an item
//      automatically. Block items use the block's own ID
//      (1–999), so item 1 is stone, item 12 is bricks.
//   2. data/registry/items.toml adds tools, weapons and dev
//      tools, with IDs from 1000 up.
//
// Categories are flags, so one item can be several things:
// an axe is a tool (chops wood) AND a weapon (used in
// combat). What each category means in play:
//
//   block    placed into the world
//   tool     used in first person in Survival to dig
//            (pickaxe, shovel, axe, hoe)
//   weapon   used only in the combat stance, from the weapon
//            slot — never swung like a first-person tool
//   dev      a dev tool; only usable in Test Play

constexpr uint32_t IC_BLOCK  = 1 << 0;
constexpr uint32_t IC_TOOL   = 1 << 1;
constexpr uint32_t IC_WEAPON = 1 << 2;
constexpr uint32_t IC_DEV    = 1 << 3;

// IDs below this belong to block items
constexpr uint16_t FIRST_NON_BLOCK_ITEM = 1000;

// What a digging tool is for
enum class ToolKind : uint8_t { None, Pickaxe, Shovel, Axe, Hoe, Hammer };

// Which behaviour a dev tool has (see game/DevTools.h)
enum class DevToolKind : uint8_t {
    None,
    Wand,       // left: corner 1   right: corner 2
    Fill,       // left: fill selection   right: clear selection
    Replace,    // left: replace clicked block type in selection
    Brush,      // left: sphere of material   right: carve sphere
    Copy,       // left: copy selection
    Paste,      // left: paste at target   right: rotate clipboard
    Explode,    // left: explosion at target
    Breaker,    // left: break one block   right: break a 3×3×3
};

struct ItemDef {
    uint16_t    id          = 0;
    std::string name;            // unique key: "stone", "iron_pickaxe"
    std::string displayName;     // shown in menus: "Iron Pickaxe"
    uint32_t    categories  = 0;
    uint16_t    maxStack    = 1;

    uint16_t    blockID     = 0;            // block items

    ToolKind    tool        = ToolKind::None;
    float       toolPower   = 1.0f;         // dig speed multiplier

    std::string weaponClass;                // "sword", "axe", "spear"
    float       damage      = 0.0f;

    DevToolKind devTool     = DevToolKind::None;

    float       color[3]    = {0.7f, 0.7f, 0.7f};   // menu swatch

    bool is(uint32_t category) const { return (categories & category) != 0; }
    bool valid() const { return id != 0; }
};

class ItemRegistry {
public:
    // Rebuild everything: block items first, then items.toml.
    // Call after the block registry has loaded (and again
    // after F6 reloads it).
    bool load(const VoxelRegistry& blocks, const char* path) {
        items.clear();
        nameIndex.clear();
        buildBlockItems(blocks);
        return loadFile(path);
    }

    const ItemDef& get(uint16_t id) const {
        static const ItemDef none;
        if (id < items.size() && items[id].valid()) return items[id];
        return none;
    }

    // 0 if no item has that name
    uint16_t find(const std::string& name) const {
        auto it = nameIndex.find(name);
        return it != nameIndex.end() ? it->second : 0;
    }

    // Every item ID in one category, in ID order
    std::vector<uint16_t> inCategory(uint32_t category) const {
        std::vector<uint16_t> out;
        for (const ItemDef& d : items)
            if (d.valid() && d.is(category)) out.push_back(d.id);
        return out;
    }

    // The item for a block (0 for air or unknown)
    uint16_t itemForBlock(uint16_t blockID) const {
        if (blockID == 0) return 0;
        return get(blockID).is(IC_BLOCK) ? blockID : 0;
    }

    size_t count() const {
        size_t n = 0;
        for (const ItemDef& d : items) n += d.valid();
        return n;
    }

private:
    std::vector<ItemDef> items;   // indexed by ID; gaps are invalid
    std::unordered_map<std::string, uint16_t> nameIndex;

    void add(const ItemDef& d) {
        if (items.size() <= d.id) items.resize(d.id + 1);
        items[d.id] = d;
        nameIndex[d.name] = d.id;
    }

    // "cracked_stone" → "Cracked Stone"
    static std::string prettify(const std::string& s) {
        std::string out;
        bool up = true;
        for (char c : s) {
            if (c == '_') { out += ' '; up = true; continue; }
            out += up ? (char)std::toupper((unsigned char)c) : c;
            up = false;
        }
        return out;
    }

    void buildBlockItems(const VoxelRegistry& blocks) {
        for (size_t id = 1; id < blocks.size() &&
                            id < FIRST_NON_BLOCK_ITEM; id++) {
            const VoxelTypeDef& b = blocks.get((uint16_t)id);
            if (b.id == 0) continue;           // gap in blocks.toml
            ItemDef d;
            d.id          = (uint16_t)id;
            d.name        = b.name;
            d.displayName = prettify(b.name);
            d.categories  = IC_BLOCK;
            d.maxStack    = 999;
            d.blockID     = (uint16_t)id;
            for (int k = 0; k < 3; k++) d.color[k] = b.color[k];
            add(d);
        }
    }

    bool loadFile(const char* path) {
        try {
            auto tbl = toml::parse_file(path);
            auto* arr = tbl["item"].as_array();
            if (!arr) return true;   // nothing extra is fine
            for (auto& entry : *arr) {
                auto* t = entry.as_table();
                if (!t) continue;
                ItemDef d;
                d.id = (uint16_t)(*t)["id"].value_or(0);
                if (d.id < FIRST_NON_BLOCK_ITEM) {
                    std::cerr << "items.toml: id " << d.id
                              << " is below " << FIRST_NON_BLOCK_ITEM
                              << " (reserved for blocks) - skipped\n";
                    continue;
                }
                d.name        = (*t)["name"].value_or(std::string("item"));
                d.displayName = (*t)["display"].value_or(prettify(d.name));
                d.maxStack    = (uint16_t)(*t)["max_stack"].value_or(1);

                if (auto* cats = (*t)["categories"].as_array())
                    for (auto& c : *cats) {
                        std::string s = c.value_or("");
                        if      (s == "block")  d.categories |= IC_BLOCK;
                        else if (s == "tool")   d.categories |= IC_TOOL;
                        else if (s == "weapon") d.categories |= IC_WEAPON;
                        else if (s == "dev")    d.categories |= IC_DEV;
                    }

                std::string tool = (*t)["tool"].value_or("");
                if      (tool == "pickaxe") d.tool = ToolKind::Pickaxe;
                else if (tool == "shovel")  d.tool = ToolKind::Shovel;
                else if (tool == "axe")     d.tool = ToolKind::Axe;
                else if (tool == "hoe")     d.tool = ToolKind::Hoe;
                else if (tool == "hammer")  d.tool = ToolKind::Hammer;
                d.toolPower = (float)(*t)["tool_power"].value_or(1.0);

                d.weaponClass = (*t)["weapon_class"].value_or(std::string());
                d.damage      = (float)(*t)["damage"].value_or(0.0);

                std::string dev = (*t)["dev_tool"].value_or("");
                if      (dev == "wand")    d.devTool = DevToolKind::Wand;
                else if (dev == "fill")    d.devTool = DevToolKind::Fill;
                else if (dev == "replace") d.devTool = DevToolKind::Replace;
                else if (dev == "brush")   d.devTool = DevToolKind::Brush;
                else if (dev == "copy")    d.devTool = DevToolKind::Copy;
                else if (dev == "paste")   d.devTool = DevToolKind::Paste;
                else if (dev == "explode") d.devTool = DevToolKind::Explode;
                else if (dev == "breaker") d.devTool = DevToolKind::Breaker;

                if (auto* col = (*t)["color"].as_array())
                    for (int k = 0; k < 3 && k < (int)col->size(); k++)
                        d.color[k] = (float)(*col)[k].value_or(0.7);
                add(d);
            }
            std::cout << "Item registry: " << count()
                      << " items (blocks + " << path << ")\n";
            return true;
        } catch (const toml::parse_error& e) {
            std::cerr << "items.toml parse error: " << e << "\n";
            return false;
        }
    }
};

extern ItemRegistry gItems;

// ── Digging speed ─────────────────────────────────────────
// Which block materials a tool is good at
inline bool toolSuits(ToolKind tool, const std::string& material) {
    switch (tool) {
    case ToolKind::Pickaxe: return material == "stone";
    case ToolKind::Shovel:  return material == "dirt";
    case ToolKind::Axe:     return material == "wood";
    case ToolKind::Hoe:     return material == "organic";
    default:                return false;
    }
}

// Seconds to break one block in Survival. Bare hands work on
// everything, slowly; the right tool divides the time by its
// power. Hardness comes from blocks.toml.
inline float breakSeconds(const VoxelTypeDef& block, const ItemDef& held) {
    constexpr float SECONDS_PER_HARDNESS = 0.15f;
    float t = block.hardness * SECONDS_PER_HARDNESS;
    if (held.is(IC_TOOL) && toolSuits(held.tool, block.material))
        t /= std::max(held.toolPower, 0.1f);
    return t;
}
