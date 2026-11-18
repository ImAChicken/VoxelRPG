#pragma once
#include "Inventory.h"
#include "ItemRegistry.h"
#include <toml++/toml.hpp>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Kits — what a new player starts with (data/registry/kits.toml)
// ══════════════════════════════════════════════════════════
// One list for the player's own inventory and one for the Test
// Play dev inventory. The game, the server (unless its
// server.toml lists its own) and tests all hand out the same
// kit, so changing what players spawn with is one file.
struct KitEntry {
    std::string item;
    int count = 1;
};

class Kits {
public:
    std::vector<KitEntry> player, dev;

    bool load(const char* path) {
        player.clear(); dev.clear();
        try {
            auto t = toml::parse_file(path);
            read(t["player"]["items"].as_array(), player);
            read(t["dev"]["items"].as_array(), dev);
            return true;
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << "\n";
            return false;
        }
    }

    // Names only (server.toml's starter_kit is a list of names)
    std::vector<std::string> playerNames() const {
        std::vector<std::string> out;
        for (const KitEntry& e : player) out.push_back(e.item);
        return out;
    }

    // Into an inventory: the hotbar in order, then the pack.
    // equip: weapons into the first weapon slot, armour worn.
    static void give(Inventory& inv, const std::vector<KitEntry>& kit, bool equip) {
        for (const KitEntry& e : kit) {
            uint16_t id = gItems.find(e.item);
            if (!id) { std::cerr << "kits.toml: no item " << e.item << "\n"; continue; }
            inv.add(id, e.count);
            if (!equip) continue;
            const ItemDef& d = gItems.get(id);
            int slot = inv.find(id);
            if (slot < 0) continue;
            // the first weapon is loadout 1's primary, the second loadout 2's
            if (d.canFight() && d.tool == ToolKind::None && inv.equip[(int)EquipSlot::Weapon1] < 0)
                inv.equipFrom(slot, EquipSlot::Weapon1);
            else if (d.canFight() && d.tool == ToolKind::None && inv.equip[(int)EquipSlot::Weapon2] < 0)
                inv.equipFrom(slot, EquipSlot::Weapon2);
            else if (d.worn >= 0) inv.equipFrom(slot, equipForWorn((WornSlot)d.worn));
        }
    }

private:
    static void read(const toml::array* a, std::vector<KitEntry>& out) {
        if (!a) return;
        for (auto& n : *a) {
            if (auto s = n.value<std::string>()) out.push_back({ *s, 1 });
            else if (auto* t = n.as_table())
                out.push_back({ (*t)["item"].value_or(std::string()), (int)(*t)["count"].value_or((int64_t)1) });
        }
    }
};

inline Kits gKits;
