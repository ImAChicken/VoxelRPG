#pragma once
#include "ItemRegistry.h"
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <string>

// ══════════════════════════════════════════════════════════
// Inventory
// ══════════════════════════════════════════════════════════
// Eight toolbar slots plus one weapon slot. Item IDs come
// from the item registry (game/ItemRegistry.h).
//
// The weapon slot is separate on purpose: weapons are used
// only in the combat stance (R), never swung from the toolbar
// like a first-person tool. An item tagged both tool and
// weapon (the axe) can sit in either place.
//
// A player has TWO of these (see entity/Player.h): the player
// inventory, used in Survival and kept when switching modes,
// and the dev inventory, used in Test Play.
//
// STUB: no backpack or inventory screen yet — that comes
// with the survival UI.
struct ItemStack {
    uint16_t itemID = 0;    // 0 = empty slot
    uint16_t count  = 0;

    bool empty() const { return itemID == 0 || count == 0; }
    void clear()       { itemID = 0; count = 0; }
};

struct Inventory {
    static constexpr int TOOLBAR_SLOTS = 8;

    ItemStack toolbar[TOOLBAR_SLOTS];
    ItemStack weapon;
    int       selected = 0;     // 0..7

    // In the dev inventory, items never run out: placing a
    // block does not use it up
    bool infinite = false;

    void select(int slot) {
        if (slot >= 0 && slot < TOOLBAR_SLOTS) selected = slot;
    }
    void selectNext() { selected = (selected + 1) % TOOLBAR_SLOTS; }
    void selectPrev() {
        selected = (selected + TOOLBAR_SLOTS - 1) % TOOLBAR_SLOTS;
    }

    const ItemStack& held() const { return toolbar[selected]; }
    const ItemDef&   heldDef() const { return gItems.get(held().itemID); }

    // Put items in: tops up matching stacks first, then empty
    // slots. Returns how many did NOT fit.
    int add(uint16_t itemID, int count) {
        const ItemDef& d = gItems.get(itemID);
        if (!d.valid() || count <= 0) return count;
        int cap = std::max<int>(1, d.maxStack);
        for (auto& s : toolbar)
            if (s.itemID == itemID && s.count < cap) {
                int n = std::min(cap - (int)s.count, count);
                s.count += (uint16_t)n;
                count   -= n;
                if (!count) return 0;
            }
        for (auto& s : toolbar)
            if (s.empty()) {
                int n = std::min(cap, count);
                s.itemID = itemID;
                s.count  = (uint16_t)n;
                count   -= n;
                if (!count) return 0;
            }
        return count;
    }

    // How many of an item the toolbar holds
    int count(uint16_t itemID) const {
        int n = 0;
        for (auto& s : toolbar) if (s.itemID == itemID) n += s.count;
        return n;
    }

    // Take items out (from any slots). False, and nothing
    // taken, if there are not enough.
    bool take(uint16_t itemID, int n) {
        if (infinite) return true;
        if (count(itemID) < n) return false;
        for (auto& s : toolbar) {
            if (s.itemID != itemID || n == 0) continue;
            int k = std::min<int>(s.count, n);
            s.count -= (uint16_t)k;
            n -= k;
            if (s.count == 0) s.clear();
        }
        return true;
    }

    // Use up one of the held item (placing a block).
    // Infinite inventories never run out.
    void consumeHeld() {
        if (infinite) return;
        ItemStack& s = toolbar[selected];
        if (s.count > 0 && --s.count == 0) s.clear();
    }

    // Put exactly this in a slot (dev menu). slot = -1 means
    // the weapon slot.
    void setSlot(int slot, uint16_t itemID, int count) {
        ItemStack& s = (slot < 0) ? weapon : toolbar[slot];
        s.itemID = itemID;
        s.count  = (uint16_t)std::max(0, count);
        if (s.count == 0) s.clear();
    }

    // ── Save / load as text ───────────────────────────────
    // Items are stored by NAME so a save survives item IDs
    // being renumbered. One line per slot:
    //   <prefix>.slot3=stone 64
    //   <prefix>.weapon=iron_sword 1
    //   <prefix>.selected=2
    void write(std::ostream& out, const std::string& prefix) const {
        out << prefix << ".selected=" << selected << "\n";
        for (int i = 0; i < TOOLBAR_SLOTS; i++)
            if (!toolbar[i].empty())
                out << prefix << ".slot" << i << "="
                    << gItems.get(toolbar[i].itemID).name << " "
                    << toolbar[i].count << "\n";
        if (!weapon.empty())
            out << prefix << ".weapon="
                << gItems.get(weapon.itemID).name << " "
                << weapon.count << "\n";
    }

    // Feed one "key=value" line; returns true if it was ours
    bool read(const std::string& key, const std::string& value,
              const std::string& prefix) {
        if (key.compare(0, prefix.size() + 1, prefix + ".") != 0)
            return false;
        std::string field = key.substr(prefix.size() + 1);
        if (field == "selected") {
            select(std::atoi(value.c_str()));
            return true;
        }
        std::istringstream in(value);
        std::string name;
        int count = 1;
        in >> name >> count;
        uint16_t id = gItems.find(name);   // unknown names: dropped
        if (field == "weapon") {
            if (id) setSlot(-1, id, count);
            return true;
        }
        if (field.compare(0, 4, "slot") == 0) {
            int slot = std::atoi(field.c_str() + 4);
            if (id && slot >= 0 && slot < TOOLBAR_SLOTS)
                setSlot(slot, id, count);
            return true;
        }
        return false;
    }
};
