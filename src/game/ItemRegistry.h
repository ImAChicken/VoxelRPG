#pragma once
#include "../voxel/VoxelRegistry.h"
#include "../world/Decorations.h"
#include "../entity/BodyMass.h"
#include "../character/GearLook.h"
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
// combat). (The iron axe is a tool only: see items.toml.)
// What each category means in play:
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
constexpr uint32_t IC_DECOR  = 1 << 4;   // a decoration from decorations.toml: places it
constexpr uint32_t IC_WORN   = 1 << 5;   // gear worn on the body (helmet, armour, pack)
constexpr uint32_t IC_SHIELD = 1 << 8;   // a shield: the off hand's guard (shield = { … } sets it too)
constexpr uint32_t IC_CHARM  = 1 << 7;   // a charm: goes in a loadout's charm slots (game/CombatKit.h)
constexpr uint32_t IC_SPELLBOOK = 1 << 6;   // a spellbook (a weapon type): used from an X / Y / B slot or the off hand, casts 'cast', held while it does
constexpr uint32_t IC_SPELL  = IC_SPELLBOOK;    // (the old name)

// IDs below this belong to block items
constexpr uint16_t FIRST_NON_BLOCK_ITEM = 1000;
constexpr uint16_t FIRST_DECOR_ITEM     = 3000;   // one per decoration, in registry order

// What a digging tool is for
enum class ToolKind : uint8_t { None, Pickaxe, Shovel, Axe, Hoe, Hammer, Blast };

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
    Cube,       // left: place a cube of material   right: break a cube
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
    int         cubeSize    = 0;            // pickaxes: breaks a cube this many voxels wide

    std::string weaponClass;                // "sword", "axe", "spear"
    float       damage      = 0.0f;
    std::string moveset;                    // weapons: how it fights (movesets.toml)
    std::string damageType;                 // "slash", "pierce", "blunt" (resistances later)
    float       poiseDamage = 0.0f;         // tools that hit bodies (the axe): poise damage of a full-speed hit

    DevToolKind devTool     = DevToolKind::None;

    // Combat layout (game/CombatKit.h)
    std::string cast;                       // used (X / Y / B, or LT in the off hand): the attack it plays (movesets.toml), e.g. a spell's cast
    std::string special;                    // LT with this in hand: "guard" (block, timed = parry), "heavy", or "" (weapons: guard)
    bool        shield      = false;        // an off-hand item that guards (LT: block and parry)
    // shield = { block = 1.0, cost = 0.6, arc = 100, parry = 0.22 }:
    // how much of a hit it stops, × stamina per point stopped, the
    // half-angle it covers (degrees), the parry window (s)
    float       shieldBlock = 1.0f, shieldCost = 0.6f, shieldArc = 100.0f, shieldParry = 0.22f;

    int         decor       = -1;           // decoration items: index in decor::Set

    float       weight      = 0.0f;         // kg, one of it (worn, held or sheathed: entity/BodyMass.h)
    std::string heldModel;                  // drawn in the hand ("axe"), "" = nothing
    int         worn        = -1;           // worn items: which WornSlot (-1: not worn)
    std::string accessory;                  // "ring" or "amulet": goes in those equipment slots
    GearLook    gear;                       // worn items: how it shows on the body (shape "" = not drawn)

    float       color[3]    = {0.7f, 0.7f, 0.7f};   // menu swatch

    bool is(uint32_t category) const { return (categories & category) != 0; }
    // Can it go in a weapon slot and fight when drawn (R): a
    // weapon, or a tool with a moveset (the axe)
    bool canFight() const { return is(IC_WEAPON) || !moveset.empty(); }
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
    // One item per decoration (plants, bushes, landmarks …), after
    // load(). Plants stack; the rest are one at a time.
    void addDecor(const decor::Set& set) {
        for (size_t i = 0; i < set.defs.size(); i++) {
            const decor::Def& dd = set.defs[i];
            if (dd.layer == decor::Layer::Ground) continue;     // patches are not things to hold
            ItemDef d;
            d.id          = (uint16_t)(FIRST_DECOR_ITEM + i);
            d.name        = dd.name;
            if (find(d.name)) d.name += "_decor";               // never shadow a block item
            d.displayName = dd.display;
            d.categories  = IC_DECOR;
            d.maxStack    = dd.layer == decor::Layer::Plant ? 99 : 1;
            d.decor       = (int)i;
            for (int k = 0; k < 3; k++) d.color[k] = dd.color[k];
            add(d);
        }
    }
    uint16_t itemForDecor(int decorIndex) const {
        uint16_t id = (uint16_t)(FIRST_DECOR_ITEM + decorIndex);
        return get(id).decor == decorIndex ? id : 0;
    }

    uint16_t itemForBlock(uint16_t blockID) const {
        if (blockID == 0) return 0;
        return get(blockID).is(IC_BLOCK) ? blockID : 0;
    }

    size_t count() const {
        size_t n = 0;
        for (const ItemDef& d : items) n += d.valid();
        return n;
    }

    // Every slot, indexed by ID (gaps are invalid: !valid())
    const std::vector<ItemDef>& all() const { return items; }

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
            if (b.isPlant()) continue;         // plant parts: the plant is the item (decorations.toml)
            ItemDef d;
            d.id          = (uint16_t)id;
            d.name        = b.name;
            // "log_y" → "Log": the axis suffix only says which way
            // the grain runs when placed
            std::string shown = b.name;
            if (shown.size() > 2 && shown[shown.size() - 2] == '_' &&
                (shown.back() == 'x' || shown.back() == 'y' || shown.back() == 'z'))
                shown.resize(shown.size() - 2);
            d.displayName = prettify(shown);
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
                        else if (s == "spellbook" || s == "spell") d.categories |= IC_SPELLBOOK;
                        else if (s == "charm")  d.categories |= IC_CHARM;
                        else if (s == "shield") d.categories |= IC_SHIELD;
                        else if (s == "weapon") d.categories |= IC_WEAPON;
                        else if (s == "dev")    d.categories |= IC_DEV;
                        else if (s == "worn")   d.categories |= IC_WORN;
                    }

                std::string tool = (*t)["tool"].value_or("");
                if      (tool == "pickaxe") d.tool = ToolKind::Pickaxe;
                else if (tool == "shovel")  d.tool = ToolKind::Shovel;
                else if (tool == "axe")     d.tool = ToolKind::Axe;
                else if (tool == "hoe")     d.tool = ToolKind::Hoe;
                else if (tool == "hammer")  d.tool = ToolKind::Hammer;
                else if (tool == "blast")   d.tool = ToolKind::Blast;
                d.toolPower = (float)(*t)["tool_power"].value_or(1.0);
                d.cast    = (*t)["cast"].value_or(std::string());
                d.special = (*t)["special"].value_or(std::string());
                d.shield  = (*t)["shield"].value_or(false);
                if (d.is(IC_SHIELD)) d.shield = true;
                if (d.shield) d.categories |= IC_SHIELD;
                if (auto* sh = (*t)["shield"].as_table()) {
                    d.shield = true;
                    d.categories |= IC_SHIELD;
                    d.shieldBlock = (float)(*sh)["block"].value_or(1.0);
                    d.shieldCost  = (float)(*sh)["cost"].value_or(0.6);
                    d.shieldArc   = (float)(*sh)["arc"].value_or(100.0);
                    d.shieldParry = (float)(*sh)["parry"].value_or(0.22);
                }
                d.cubeSize  = (int)(*t)["cube"].value_or((int64_t)0);

                d.weaponClass = (*t)["weapon_class"].value_or(std::string());
                d.damage      = (float)(*t)["damage"].value_or(0.0);
                d.moveset     = (*t)["moveset"].value_or(std::string());
                d.damageType  = (*t)["damage_type"].value_or(std::string());
                d.poiseDamage = (float)(*t)["poise_damage"].value_or(0.0);

                d.weight = (float)(*t)["weight"].value_or(0.0);
                d.heldModel = (*t)["held_model"].value_or(std::string());
                d.accessory = (*t)["accessory"].value_or(std::string());
                if (auto* g = (*t)["gear"].as_table()) {
                    auto hex = [](const std::string& h, glm::vec3 def) {
                        if (h.size() != 7 || h[0] != '#') return def;
                        unsigned v = (unsigned)std::stoul(h.substr(1), nullptr, 16);
                        return glm::vec3((float)((v >> 16) & 255), (float)((v >> 8) & 255), (float)(v & 255));
                    };
                    d.gear.shape = (*g)["shape"].value_or(std::string());
                    d.gear.color = hex((*g)["color"].value_or(std::string()), d.gear.color);
                    d.gear.trim  = hex((*g)["trim"].value_or(std::string()), d.gear.trim);
                }
                {
                    std::string w = (*t)["worn"].value_or(std::string());
                    if (!w.empty()) {
                        d.worn = wornSlotFromKey(w.c_str());
                        if (d.worn < 0)
                            std::cerr << "items.toml: " << d.name << ": unknown worn slot '" << w << "'\n";
                        else
                            d.categories |= IC_WORN;
                    }
                }

                std::string dev = (*t)["dev_tool"].value_or("");
                if      (dev == "wand")    d.devTool = DevToolKind::Wand;
                else if (dev == "fill")    d.devTool = DevToolKind::Fill;
                else if (dev == "replace") d.devTool = DevToolKind::Replace;
                else if (dev == "brush")   d.devTool = DevToolKind::Brush;
                else if (dev == "copy")    d.devTool = DevToolKind::Copy;
                else if (dev == "paste")   d.devTool = DevToolKind::Paste;
                else if (dev == "explode") d.devTool = DevToolKind::Explode;
                else if (dev == "breaker") d.devTool = DevToolKind::Breaker;
                else if (dev == "cube")    d.devTool = DevToolKind::Cube;

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
