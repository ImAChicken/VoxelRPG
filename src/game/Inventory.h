#pragma once
#include "ItemRegistry.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Inventory
// ══════════════════════════════════════════════════════════
// 50 slots. The first 10 are the hotbar:
//   slots 0, 1   off-hand (keys 1, 2): pressing the key puts
//                that item in the left hand, pressing again
//                takes it out; one at a time
//   slots 2..9   main hand (keys 3..0, or the scroll wheel)
//   slots 10..49 the pack
//
// Equipment (the right side of the inventory screen) does NOT
// hold items of its own: each equipment slot points at the
// inventory slot of the item that is equipped. An equipped
// item still takes its inventory slot and is only marked as
// equipped. So an axe can be Weapon 1 and sit in the hotbar
// at the same time.
//
// Weapons 1 and 2: the drawn weapon (R) is the active one of
// the two; the swap key switches between them.
//
// A player has TWO of these (see entity/Player.h): the player
// inventory, used in Survival and kept when switching modes,
// and the dev inventory, used in Test Play.
struct ItemStack {
    uint16_t itemID = 0;    // 0 = empty slot
    uint16_t count  = 0;

    bool empty() const { return itemID == 0 || count == 0; }
    void clear()       { itemID = 0; count = 0; }
};

enum class EquipSlot : int {
    Weapon1, Weapon2,
    Head, Torso, Hands, Legs, Feet, Back,     // in WornSlot order
    Ring1, Ring2, Amulet,
    COUNT
};
constexpr int EQUIP_SLOTS = (int)EquipSlot::COUNT;
constexpr int EQUIP_WORN_FIRST = (int)EquipSlot::Head;

inline const char* equipSlotKey(EquipSlot s) {
    static const char* K[] = { "weapon1", "weapon2", "head", "torso", "hands", "legs",
                               "feet", "back", "ring1", "ring2", "amulet" };
    return (int)s >= 0 && (int)s < EQUIP_SLOTS ? K[(int)s] : "?";
}
inline const char* equipSlotName(EquipSlot s) {
    static const char* N[] = { "Weapon 1", "Weapon 2", "Head", "Torso", "Hands", "Legs",
                               "Feet", "Back", "Ring", "Ring", "Amulet" };
    return (int)s >= 0 && (int)s < EQUIP_SLOTS ? N[(int)s] : "?";
}
inline int equipSlotFromKey(const std::string& k) {
    for (int i = 0; i < EQUIP_SLOTS; i++) if (k == equipSlotKey((EquipSlot)i)) return i;
    return -1;
}
inline EquipSlot equipForWorn(WornSlot w) { return (EquipSlot)(EQUIP_WORN_FIRST + (int)w); }

struct Inventory {
    static constexpr int SLOTS      = 50;
    static constexpr int HOTBAR     = 10;
    static constexpr int OFFHAND    = 2;    // hotbar slots 0 and 1
    static constexpr int FIRST_MAIN = 2;    // first main-hand hotbar slot

    ItemStack slots[SLOTS];
    int       selected = FIRST_MAIN;   // main hand: FIRST_MAIN .. HOTBAR-1
    int       offhand  = -1;           // 0 or 1 while an off-hand slot is in use
    int       equip[EQUIP_SLOTS];      // inventory slot of each equipped item, -1 = none
    int       activeWeapon = 0;        // 0 = Weapon 1, 1 = Weapon 2

    // In the dev inventory, items never run out: placing a
    // block does not use it up
    bool infinite = false;

    // A store (a chest: world/Chests.h): every slot is just a
    // slot — no hotbar or off-hand to keep free
    bool storage = false;
    // How many slots it uses (a store can be smaller than 50:
    // an NPC's pack, a crate). Slots past it stay empty.
    int  capacity = SLOTS;
    // A showcase store (a dev world chest): what's taken out of
    // it is a copy, so it never runs out. Putting things in, and
    // moving them about inside it, work as usual.
    bool showcase = false;

    Inventory() { std::fill(std::begin(equip), std::end(equip), -1); }

    static const ItemStack& none() { static const ItemStack e; return e; }
    static bool valid(int slot) { return slot >= 0 && slot < SLOTS; }

    // ── Hotbar ────────────────────────────────────────────
    // key = 0..9 for keys 1..0
    void pressHotbarKey(int key) {
        if (key < 0 || key >= HOTBAR) return;
        if (key < OFFHAND) toggleOffhand(key);
        else selected = key;
    }
    void select(int slot) { if (slot >= FIRST_MAIN && slot < HOTBAR) selected = slot; }
    void selectNext() { selected = FIRST_MAIN + (selected - FIRST_MAIN + 1) % (HOTBAR - FIRST_MAIN); }
    void selectPrev() { selected = FIRST_MAIN + (selected - FIRST_MAIN + HOTBAR - FIRST_MAIN - 1) % (HOTBAR - FIRST_MAIN); }
    // Off-hand slot k on (the other goes off), or off again;
    // an empty slot can't be taken in hand
    // The left hand: off-hand 1, then 2, then nothing (empty slots skipped)
    void cycleOffhand() {
        for (int k = offhand + 1; k < OFFHAND; k++)
            if (!slots[k].empty()) { offhand = k; return; }
        offhand = -1;
    }
    void toggleOffhand(int k) {
        if (k < 0 || k >= OFFHAND) return;
        if (offhand == k) offhand = -1;
        else if (!slots[k].empty()) offhand = k;
    }

    // In the right hand (the selected hotbar slot)
    const ItemStack& held() const { return slots[selected]; }
    const ItemDef&   heldDef() const { return gItems.get(held().itemID); }
    // In the left hand, or empty
    const ItemStack& offhandItem() const { return offhand >= 0 ? slots[offhand] : none(); }

    // ── Equipment ─────────────────────────────────────────
    const ItemStack& equipped(EquipSlot e) const {
        int s = equip[(int)e];
        return valid(s) ? slots[s] : none();
    }
    // The drawn weapon: the active one of Weapon 1 / 2
    const ItemStack& weapon() const { return equipped((EquipSlot)((int)EquipSlot::Weapon1 + activeWeapon)); }
    EquipSlot weaponSlot() const { return (EquipSlot)((int)EquipSlot::Weapon1 + activeWeapon); }
    void swapWeapons() { activeWeapon = 1 - activeWeapon; }
    const ItemStack& worn(WornSlot w) const { return equipped(equipForWorn(w)); }

    // What equipment slot inventory slot s is in, or -1
    int equippedAs(int s) const {
        for (int e = 0; e < EQUIP_SLOTS; e++) if (equip[e] == s) return e;
        return -1;
    }
    bool isEquipped(int s) const { return equippedAs(s) >= 0; }

    // Can this item go in that equipment slot
    static bool fits(uint16_t itemID, EquipSlot e) {
        const ItemDef& d = gItems.get(itemID);
        if (!d.valid()) return false;
        int i = (int)e;
        if (e == EquipSlot::Weapon1 || e == EquipSlot::Weapon2) return d.canFight();
        if (i >= EQUIP_WORN_FIRST && i < EQUIP_WORN_FIRST + WORN_SLOTS) return d.worn == i - EQUIP_WORN_FIRST;
        if (e == EquipSlot::Ring1 || e == EquipSlot::Ring2) return d.accessory == "ring";
        if (e == EquipSlot::Amulet) return d.accessory == "amulet";
        return false;
    }
    // The equipment slot an item would go to with a shift-click
    // (weapons: an empty weapon slot first, else the active one)
    int slotFor(uint16_t itemID) const {
        const ItemDef& d = gItems.get(itemID);
        if (!d.valid()) return -1;
        if (d.canFight()) {
            if (equip[(int)EquipSlot::Weapon1] < 0) return (int)EquipSlot::Weapon1;
            if (equip[(int)EquipSlot::Weapon2] < 0) return (int)EquipSlot::Weapon2;
            return (int)weaponSlot();
        }
        if (d.worn >= 0 && d.worn < WORN_SLOTS) return EQUIP_WORN_FIRST + d.worn;
        if (d.accessory == "ring") return equip[(int)EquipSlot::Ring1] < 0 || equip[(int)EquipSlot::Ring2] >= 0
                                          ? (int)EquipSlot::Ring1 : (int)EquipSlot::Ring2;
        if (d.accessory == "amulet") return (int)EquipSlot::Amulet;
        return -1;
    }
    // Equip the item in inventory slot s; whatever was there
    // comes off (stays in its own slot). One item fills one
    // equipment slot. False if it doesn't fit.
    bool equipFrom(int s, EquipSlot e) {
        if (!valid(s) || slots[s].empty() || !fits(slots[s].itemID, e)) return false;
        int was = equippedAs(s);
        if (was >= 0) equip[was] = -1;
        equip[(int)e] = s;
        return true;
    }
    void unequip(EquipSlot e) { equip[(int)e] = -1; }
    // Shift-click: equip it, or take it off if it already is
    bool toggleEquip(int s) {
        if (!valid(s) || slots[s].empty()) return false;
        int was = equippedAs(s);
        if (was >= 0) { equip[was] = -1; return true; }
        int e = slotFor(slots[s].itemID);
        return e >= 0 && equipFrom(s, (EquipSlot)e);
    }
    // Equip an item by id: the one already carried, else into a
    // free slot first (dev menu, commands). False if it doesn't fit.
    bool equipItem(uint16_t itemID, int e = -1) {
        if (e < 0) e = slotFor(itemID);
        if (e < 0 || !fits(itemID, (EquipSlot)e)) return false;
        int s = find(itemID, true);
        if (s < 0) s = find(itemID, false);
        if (s < 0) { s = freeSlot(); if (s < 0) return false; slots[s] = { itemID, 1 }; }
        return equipFrom(s, (EquipSlot)e);
    }
    // Drop links to slots that emptied or no longer fit
    void tidy() {
        for (int e = 0; e < EQUIP_SLOTS; e++) {
            int s = equip[e];
            if (s >= 0 && (!valid(s) || slots[s].empty() || !fits(slots[s].itemID, (EquipSlot)e))) equip[e] = -1;
        }
        if (offhand >= 0 && slots[offhand].empty()) offhand = -1;
    }

    // ── Moving items (the inventory screen) ───────────────
    // Drag from a to b: stack onto the same item, else swap.
    // Equipment follows its item. half = move half the stack
    // (into an empty slot or onto the same item).
    void move(int a, int b, bool half = false) {
        if (!valid(a) || !valid(b) || a == b || slots[a].empty()) return;
        ItemStack& A = slots[a];
        ItemStack& B = slots[b];
        int cap = std::max<int>(1, gItems.get(A.itemID).maxStack);
        int want = half ? std::max(1, A.count / 2) : A.count;
        if (B.itemID == A.itemID && cap > 1) {
            int n = std::min(want, cap - (int)B.count);
            B.count += (uint16_t)n; A.count -= (uint16_t)n;
            if (A.count == 0) A.clear();
        } else if (half && B.empty() && want < A.count) {
            B = { A.itemID, (uint16_t)want };
            A.count -= (uint16_t)want;
        } else {
            std::swap(A, B);
            for (int& s : equip) { if (s == a) s = b; else if (s == b) s = a; }
            if (offhand == a) offhand = b; else if (offhand == b) offhand = a;
        }
        tidy();
    }

    // ── Between two inventories (a chest and a player) ────
    // Drag slot a of A onto slot b of B: stack onto the same
    // item, else swap. half = half the stack. Whatever leaves
    // an inventory leaves its equipment and hands too.
    static void moveAcross(Inventory& A, int a, Inventory& B, int b, bool half = false) {
        if (&A == &B) { A.move(a, b, half); return; }
        if (!valid(a) || !valid(b) || a >= A.capacity || b >= B.capacity || A.slots[a].empty()) return;
        ItemStack& X = A.slots[a];
        ItemStack& Y = B.slots[b];
        int cap = std::max<int>(1, gItems.get(X.itemID).maxStack);
        int want = half ? std::max(1, X.count / 2) : X.count;
        if (A.showcase) {                     // a copy comes out; the showcase keeps its own
            if (Y.empty()) Y = { X.itemID, (uint16_t)want };
            else if (Y.itemID == X.itemID) Y.count = (uint16_t)std::min(cap, (int)Y.count + want);
            else B.add(X.itemID, want);       // that slot's taken: wherever it fits
            B.tidy();
            return;
        }
        if (Y.itemID == X.itemID && cap > 1) {
            int n = std::min(want, cap - (int)Y.count);
            Y.count += (uint16_t)n; X.count -= (uint16_t)n;
            if (X.count == 0) X.clear();
        } else if (half && Y.empty() && want < X.count) {
            Y = { X.itemID, (uint16_t)want };
            X.count -= (uint16_t)want;
        } else {
            std::swap(X, Y);
            A.unlinkSlot(a);
            B.unlinkSlot(b);
        }
        A.tidy(); B.tidy();
    }
    // Shift-click: as much of slot a as fits, into B (stacks
    // first, then free slots). Returns how many moved.
    static int sendAcross(Inventory& A, int a, Inventory& B) {
        if (!valid(a) || A.slots[a].empty()) return 0;
        ItemStack& X = A.slots[a];
        int left = B.add(X.itemID, X.count);
        int moved = X.count - left;
        if (A.showcase) { B.tidy(); return moved; }     // a copy: the showcase keeps its own
        X.count = (uint16_t)left;
        if (X.count == 0) { X.clear(); A.unlinkSlot(a); }
        A.tidy();
        return moved;
    }
    // Slot s holds something else now: it's no longer equipped
    // or in the left hand
    void unlinkSlot(int s) {
        for (int& e : equip) if (e == s) e = -1;
        if (offhand == s) offhand = -1;
    }

    // ── Adding and taking ─────────────────────────────────
    // A free slot: the main hotbar first, then the pack (never
    // the off-hand slots: those are filled by hand)
    int freeSlot() const {
        for (int i = storage ? 0 : FIRST_MAIN; i < capacity; i++) if (slots[i].empty()) return i;
        return -1;
    }
    // A slot holding the item (preferring an unequipped one), or -1
    int find(uint16_t itemID, bool unequippedOnly = false) const {
        for (int i = 0; i < SLOTS; i++)
            if (slots[i].itemID == itemID && !slots[i].empty() && (!unequippedOnly || !isEquipped(i))) return i;
        return -1;
    }

    // Put items in: tops up matching stacks first, then empty
    // slots. Returns how many did NOT fit.
    int add(uint16_t itemID, int count) {
        const ItemDef& d = gItems.get(itemID);
        if (!d.valid() || count <= 0) return count;
        int cap = std::max<int>(1, d.maxStack);
        for (auto& s : slots)
            if (s.itemID == itemID && s.count > 0 && s.count < cap) {
                int n = std::min(cap - (int)s.count, count);
                s.count += (uint16_t)n;
                count   -= n;
                if (!count) return 0;
            }
        for (int i = storage ? 0 : FIRST_MAIN; i < capacity && count > 0; i++)
            if (slots[i].empty()) {
                int n = std::min(cap, count);
                slots[i] = { itemID, (uint16_t)n };
                count -= n;
            }
        return count;
    }

    bool isEmpty() const { for (auto& s : slots) if (!s.empty()) return false; return true; }

    // How many of an item there are
    int count(uint16_t itemID) const {
        int n = 0;
        for (auto& s : slots) if (s.itemID == itemID) n += s.count;
        return n;
    }

    // Take items out (unequipped slots first). False, and
    // nothing taken, if there are not enough.
    bool take(uint16_t itemID, int n) {
        if (infinite) return true;
        if (count(itemID) < n) return false;
        for (int pass = 0; pass < 2 && n > 0; pass++)
            for (int i = SLOTS - 1; i >= 0 && n > 0; i--) {
                ItemStack& s = slots[i];
                if (s.itemID != itemID || isEquipped(i) != (pass == 1)) continue;
                int k = std::min<int>(s.count, n);
                s.count -= (uint16_t)k;
                n -= k;
                if (s.count == 0) s.clear();
            }
        tidy();
        return true;
    }

    // Use up one of the held item (placing a block).
    // Infinite inventories never run out.
    void consumeHeld() {
        if (infinite) return;
        ItemStack& s = slots[selected];
        if (s.count > 0 && --s.count == 0) s.clear();
        tidy();
    }

    // Put exactly this in a slot (dev menu, starting kit)
    void setSlot(int slot, uint16_t itemID, int count) {
        if (!valid(slot)) return;
        ItemStack& s = slots[slot];
        s.itemID = itemID;
        s.count  = (uint16_t)std::max(0, count);
        if (s.count == 0) s.clear();
        tidy();
    }
    void clearAll() {
        for (auto& s : slots) s.clear();
        tidy();
    }

    // Put on a worn item (items.toml: worn = "<slot>"), taking it
    // from the inventory or adding one; whatever was in that slot
    // comes off. False if the item is not worn gear (or no room).
    bool wear(uint16_t itemID) {
        const ItemDef& d = gItems.get(itemID);
        if (!d.valid() || d.worn < 0 || d.worn >= WORN_SLOTS) return false;
        return equipItem(itemID, EQUIP_WORN_FIRST + d.worn);
    }
    void takeOff(WornSlot slot) { unequip(equipForWorn(slot)); }

    // ── Save / load as text ───────────────────────────────
    // Items are stored by NAME so a save survives item IDs
    // being renumbered:
    //   <prefix>.i12=stone 64           slot 12
    //   <prefix>.eq.weapon1=4           Weapon 1 is slot 4
    //   <prefix>.sel=3   <prefix>.off=0   <prefix>.wset=1
    // Old saves (8 toolbar slots + weapon + worn) still load:
    // the toolbar moves to slots 2..9 and the weapon and worn
    // gear go into the pack, equipped.
    void write(std::ostream& out, const std::string& prefix) const {
        out << prefix << ".sel=" << selected << "\n";
        out << prefix << ".off=" << offhand << "\n";
        out << prefix << ".wset=" << activeWeapon << "\n";
        for (int i = 0; i < SLOTS; i++)
            if (!slots[i].empty())
                out << prefix << ".i" << i << "=" << gItems.get(slots[i].itemID).name << " " << slots[i].count << "\n";
        for (int e = 0; e < EQUIP_SLOTS; e++)
            if (equip[e] >= 0)
                out << prefix << ".eq." << equipSlotKey((EquipSlot)e) << "=" << equip[e] << "\n";
    }

    // Feed one "key=value" line; returns true if it was ours.
    // Call finishLoad() after the last line.
    bool read(const std::string& key, const std::string& value,
              const std::string& prefix) {
        if (key.compare(0, prefix.size() + 1, prefix + ".") != 0)
            return false;
        std::string field = key.substr(prefix.size() + 1);
        int num = std::atoi(value.c_str());
        if (field == "sel")  { select(num); return true; }
        if (field == "off")  { offhand = (num >= 0 && num < OFFHAND) ? num : -1; return true; }
        if (field == "wset") { activeWeapon = num == 1 ? 1 : 0; return true; }
        if (field.compare(0, 3, "eq.") == 0) {
            int e = equipSlotFromKey(field.substr(3));
            if (e >= 0 && valid(num)) equip[e] = num;
            return true;
        }
        if (field == "selected") { select(num + FIRST_MAIN); return true; }   // old saves
        std::istringstream in(value);
        std::string name;
        int count = 1;
        in >> name >> count;
        uint16_t id = gItems.find(name);   // unknown names: dropped
        if (field.size() > 1 && field[0] == 'i' && std::isdigit((unsigned char)field[1])) {
            int s = std::atoi(field.c_str() + 1);
            if (id && valid(s)) slots[s] = { id, (uint16_t)std::max(1, count) };
            return true;
        }
        // old saves
        if (field.compare(0, 4, "slot") == 0) {
            int s = std::atoi(field.c_str() + 4);
            if (id && s >= 0 && s < HOTBAR - FIRST_MAIN) slots[s + FIRST_MAIN] = { id, (uint16_t)std::max(1, count) };
            return true;
        }
        if (field == "weapon") { if (id) pending.push_back({ id, (int)EquipSlot::Weapon1 }); return true; }
        if (field.compare(0, 5, "worn.") == 0) {
            int w = wornSlotFromKey(field.c_str() + 5);
            if (id && w >= 0) pending.push_back({ id, EQUIP_WORN_FIRST + w });
            return true;
        }
        return false;
    }
    void finishLoad() {
        for (auto& p : pending) {
            int s = freeSlot();
            if (s < 0) break;
            slots[s] = { p.first, 1 };
            equipFrom(s, (EquipSlot)p.second);
        }
        pending.clear();
        tidy();
    }

private:
    std::vector<std::pair<uint16_t, int>> pending;   // old-save weapon / worn gear
};

// How the gear it has on shows on a body (head to back)
inline std::vector<GearLook> gearLooksOf(const Inventory& inv) {
    std::vector<GearLook> out;
    for (int w = 0; w < WORN_SLOTS; w++) {
        const ItemStack& st = inv.worn((WornSlot)w);
        if (st.empty()) continue;
        const ItemDef& d = gItems.get(st.itemID);
        if (!d.gear.shape.empty()) out.push_back(d.gear);
    }
    return out;
}
