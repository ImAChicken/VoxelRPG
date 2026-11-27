#pragma once
#include "Inventory.h"
#include "ItemRegistry.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <sstream>
#include <string>

// ══════════════════════════════════════════════════════════
// CombatKit — what the combat buttons hold (Eddie's layout)
// ══════════════════════════════════════════════════════════
// Two LOADOUTS, set up on the right of the inventory screen:
//   primary     the weapon (the inventory's Weapon 1 / Weapon 2
//               equipment slot: loadout 1 / 2)
//   off hand    a shield, a second sword, a spell, a grapple, or nothing
//               (nothing: LT is the primary's block / parry)
//   X, Y, B     3 items each; the D-pad steps through the ones set
//               (keys 4 / 5 / 6), X / Y / B use the one up (1 / 2 / 3)
//   charms      2 (worn while the loadout is in hand)
// and, for both, the 8 QUICK options picked with Start held
// (keys: hold Left Alt, 1 to 8). D-pad down (key X) switches
// loadout.
//
// Items are never moved: the kit only names them (by item id),
// so one item can be in several places, and anything that
// leaves the inventory just drops out (tidy).
//
// Saved in player.dat ("kit.*").
struct WeaponSet {
    uint16_t off = 0;
    std::array<std::array<uint16_t, 3>, 3> slots{};   // [X / Y / B][0..2]
    std::array<int, 3> at{ 0, 0, 0 };                 // which of each is up
    std::array<uint16_t, 2> charms{};
};

struct CombatKit {
    static constexpr int SETS = 2, PER_SLOT = 3, CHARMS = 2, QUICK = 8;
    std::array<WeaponSet, SETS> sets;
    int setAt = 0;                                    // follows the inventory's drawn weapon
    std::array<uint16_t, QUICK> quick{};              // D-pad left/up/right/down, X/Y/B/A
    bool made = false;                                // the defaults were filled in once
    bool hooked = false;                              // a grapple carried was put in a slot once (B)

    const WeaponSet& current() const { return sets[(size_t)std::clamp(setAt, 0, SETS - 1)]; }
    WeaponSet& current() { return sets[(size_t)std::clamp(setAt, 0, SETS - 1)]; }

    // Which of button i's three is up: the one chosen, or (if that
    // was cleared) the first set; -1 if none
    int upIndex(int i) const {
        if (i < 0 || i > 2) return -1;
        const WeaponSet& w = current();
        int a = w.at[i] % PER_SLOT;
        if (w.slots[i][a]) return a;
        for (int n = 0; n < PER_SLOT; n++) if (w.slots[i][n]) return n;
        return -1;
    }
    // The item up in button i (0 X, 1 Y, 2 B), or 0
    uint16_t slotItem(int i) const {
        int u = upIndex(i);
        return u < 0 ? 0 : current().slots[i][u];
    }
    // Step to the next item set in button i
    void cycleSlot(int i) {
        int u = upIndex(i);
        if (u < 0) return;
        WeaponSet& w = current();
        for (int k = 1; k <= PER_SLOT; k++) {
            int n = (u + k) % PER_SLOT;
            if (w.slots[i][n]) { w.at[i] = n; return; }
        }
    }
    int count(int i) const {
        int n = 0;
        for (uint16_t x : current().slots[i]) n += x != 0;
        return n;
    }

    // Loadout 1 / 2 is the inventory's Weapon 1 / 2
    void follow(const Inventory& inv) { setAt = std::clamp(inv.activeWeapon, 0, SETS - 1); }
    uint16_t primary(const Inventory& inv, int set) const {
        return inv.equipped(set == 0 ? EquipSlot::Weapon1 : EquipSlot::Weapon2).itemID;
    }

    // Drop what's no longer carried; the first time, fill in the
    // spells carried
    void tidy(const Inventory& inv) {
        // gone from the inventory, or no longer fits (an old save, a
        // changed rule): out
        auto keep = [&](uint16_t& id, bool (*fits)(uint16_t)) { if (id && (inv.find(id) < 0 || !fits(id))) id = 0; };
        for (WeaponSet& w : sets) {
            keep(w.off, fitsOff);
            for (auto& s : w.slots) for (uint16_t& id : s) keep(id, fitsAbility);
            for (uint16_t& id : w.charms) keep(id, fitsCharm);
        }
        for (uint16_t& q : quick) keep(q, fitsAbility);
        if (!made) makeDefaults(inv);
        if (!hooked) placeGrapple(inv);
        follow(inv);
    }
    void makeDefaults(const Inventory& inv) {
        made = true;
        // the first shield carried: both loadouts' off hand
        for (const ItemStack& s : inv.slots)
            if (!s.empty() && gItems.get(s.itemID).shield) {
                for (WeaponSet& w : sets) if (!w.off) w.off = s.itemID;
                break;
            }
        int n = 0;
        for (const ItemStack& s : inv.slots) {
            if (s.empty() || !gItems.get(s.itemID).is(IC_SPELL) || n >= PER_SLOT) continue;
            bool have = false;
            for (uint16_t x : sets[0].slots[0]) have = have || x == s.itemID;
            if (have) continue;
            for (WeaponSet& w : sets) w.slots[0][n] = s.itemID;
            n++;
        }
    }

    // The first grapple carried, put in B (both loadouts) if B's
    // free, once — it isn't on a key of its own (an older save
    // gets it too). Taken out, it stays out.
    void placeGrapple(const Inventory& inv) {
        uint16_t hook = 0;
        for (const ItemStack& s : inv.slots)
            if (!s.empty() && gItems.get(s.itemID).grappleRange > 0.0f) { hook = s.itemID; break; }
        if (!hook) return;
        hooked = true;
        if (has(hook)) return;
        for (WeaponSet& w : sets)
            for (uint16_t& id : w.slots[2]) if (!id) { id = hook; break; }
    }
    // Is it anywhere in the kit?
    bool has(uint16_t id) const {
        if (!id) return false;
        for (const WeaponSet& w : sets) {
            if (w.off == id) return true;
            for (auto& s : w.slots) for (uint16_t x : s) if (x == id) return true;
        }
        for (uint16_t q : quick) if (q == id) return true;
        return false;
    }
    // The grapple a button would throw now: in combat, one up in X /
    // Y / B or in the off hand; in combat or out, a quick option.
    // (Out of combat it's also thrown held from the hotbar: main.cpp)
    // button: 0..2 X / Y / B, 3 the off hand (LT), 4 + i quick i; -1
    uint16_t grappleReady(bool combat, int* button = nullptr) const {
        auto hook = [](uint16_t id) { return id && gItems.get(id).grappleRange > 0.0f; };
        if (button) *button = -1;
        if (combat) {
            for (int i = 0; i < 3; i++) if (hook(slotItem(i))) { if (button) *button = i; return slotItem(i); }
            if (hook(current().off)) { if (button) *button = 3; return current().off; }
        }
        for (int i = 0; i < QUICK; i++) if (hook(quick[(size_t)i])) { if (button) *button = 4 + i; return quick[(size_t)i]; }
        return 0;
    }

    // ── What goes where ───────────────────────────────────
    // The slots by name (shown in game, ItemKind below):
    //   Weapon slot     a loadout's primary (the Weapon 1 / 2
    //                   equipment slots): weapons, and tools that
    //                   fight (the axe: Inventory::fits, canFight)
    //   Off hand        shields, weapons, spellbooks
    //   Ability slots   X / Y / B and the 8 quick options: things
    //                   that DO something when used — spellbooks,
    //                   grapples, and anything with a cast. Not other
    //                   tools (one day an enchanted tool may get a
    //                   cast, and then fit)
    //   Charm slots     charms
    static bool fitsOff(uint16_t id)   { const ItemDef& d = gItems.get(id); return d.valid() && (d.shield || d.is(IC_WEAPON) || d.is(IC_SPELL) || d.grappleRange > 0.0f); }
    static bool fitsCharm(uint16_t id) { return gItems.get(id).is(IC_CHARM); }
    static bool fitsAbility(uint16_t id) {
        const ItemDef& d = gItems.get(id);
        return d.valid() && !d.is(IC_DEV) && (d.is(IC_SPELLBOOK) || d.grappleRange > 0.0f || !d.cast.empty() || !d.spell.empty());
    }
    static bool fitsButton(uint16_t id) { return fitsAbility(id); }   // (the old name)

    // What kind of item it is, and where it goes, in a few words
    // (the inventory shows it under the item's name)
    static std::string itemKind(uint16_t id) {
        const ItemDef& d = gItems.get(id);
        if (!d.valid()) return std::string();
        if (d.is(IC_DEV))       return "Dev tool - Test Play only, from the hotbar";
        if (d.shield)           return "Shield - goes in a loadout's off hand";
        if (d.is(IC_SPELLBOOK)) return "Spellbook - ability slots, quick options or off hand";
        if (d.is(IC_CHARM))     return "Charm - charm slots";
        if (d.grappleRange > 0.0f) return "Grapple - ability slots, quick options or off hand";
        if (d.is(IC_TOOL) && d.canFight()) return "Tool and weapon - the hotbar, or a Weapon slot";
        if (d.is(IC_TOOL))      return "Tool - used from the hotbar";
        if (d.is(IC_WEAPON))    return "Weapon - a Weapon slot or the off hand";
        if (!d.cast.empty() || !d.spell.empty()) return "Usable - ability slots or quick options";
        if (d.worn >= 0 || !d.accessory.empty()) return "Worn - its equipment slot";
        if (d.is(IC_BLOCK) || d.is(IC_DECOR)) return "Placeable - from the hotbar";
        return std::string();
    }
    // Why it won't go in a slot (said when a drop is refused)
    static std::string whyNot(uint16_t id, const char* slotName) {
        std::string k = itemKind(id);
        std::string what = k.substr(0, k.find(" - "));
        for (char& c : what) c = (char)std::tolower((unsigned char)c);
        // (the UI font has no brackets or apostrophes)
        return gItems.get(id).displayName + " is a " + (what.empty() ? "item" : what) + " - it does not go in " + slotName;
    }

    // ── What LT does ──────────────────────────────────────
    enum class Special { None, Guard, Heavy, Cast };
    Special special(uint16_t mainWeapon, uint16_t* castItem) const {
        uint16_t off = current().off;
        if (off) {
            const ItemDef& d = gItems.get(off);
            if (!d.cast.empty() || !d.spell.empty() || d.grappleRange > 0.0f) { if (castItem) *castItem = off; return Special::Cast; }
            if (d.special == "heavy") return Special::Heavy;
            return Special::Guard;                 // a shield, or a second blade held to guard
        }
        if (!mainWeapon) return Special::None;
        const ItemDef& d = gItems.get(mainWeapon);
        if (d.special == "heavy") return Special::Heavy;
        if (!d.cast.empty() || !d.spell.empty()) { if (castItem) *castItem = mainWeapon; return Special::Cast; }
        return Special::Guard;                     // no off hand: the primary's block / parry
    }

    // ── Save / load ───────────────────────────────────────
    //   kit.set0.off=wooden_shield
    //   kit.set0.x=windburst,-,-     kit.set0.at=0 0 0
    //   kit.set0.charms=-,-          kit.quick=torch,-,-,-,-,-,-,-
    void write(std::ostream& out) const {
        auto name = [](uint16_t id) { return id ? gItems.get(id).name : std::string("-"); };
        auto list = [&](const uint16_t* v, int n) {
            std::string s;
            for (int i = 0; i < n; i++) s += (i ? "," : "") + name(v[i]);
            return s;
        };
        out << "kit.made=" << (made ? 1 : 0) << "\n";
        out << "kit.hooked=" << (hooked ? 1 : 0) << "\n";
        for (int i = 0; i < SETS; i++) {
            const WeaponSet& w = sets[i];
            std::string p = "kit.set" + std::to_string(i) + ".";
            out << p << "off=" << name(w.off) << "\n";
            const char* k[3] = { "x", "y", "b" };
            for (int j = 0; j < 3; j++) out << p << k[j] << "=" << list(w.slots[j].data(), PER_SLOT) << "\n";
            out << p << "at=" << w.at[0] << " " << w.at[1] << " " << w.at[2] << "\n";
            out << p << "charms=" << list(w.charms.data(), CHARMS) << "\n";
        }
        out << "kit.quick=" << list(quick.data(), QUICK) << "\n";
    }
    bool read(const std::string& key, const std::string& value) {
        if (key.rfind("kit.", 0) != 0) return false;
        std::string f = key.substr(4);
        auto id = [](const std::string& n) -> uint16_t { return n == "-" || n.empty() ? 0 : gItems.find(n); };
        auto list = [&](const std::string& v, uint16_t* out, int n) {
            std::stringstream ss(v);
            std::string s;
            for (int i = 0; i < n && std::getline(ss, s, ','); i++) out[i] = id(s);
        };
        if (f == "made") { made = value == "1"; return true; }
        if (f == "hooked") { hooked = value == "1"; return true; }
        if (f == "quick") { list(value, quick.data(), QUICK); return true; }
        if (f.rfind("set", 0) == 0 && f.size() > 5 && f[4] == '.') {
            int i = f[3] - '0';
            if (i < 0 || i >= SETS) return true;
            WeaponSet& w = sets[i];
            std::string k = f.substr(5);
            if (k == "off") w.off = id(value);
            else if (k == "x") list(value, w.slots[0].data(), PER_SLOT);
            else if (k == "y") list(value, w.slots[1].data(), PER_SLOT);
            else if (k == "b") list(value, w.slots[2].data(), PER_SLOT);
            else if (k == "charms") list(value, w.charms.data(), CHARMS);
            else if (k == "at") std::sscanf(value.c_str(), "%d %d %d", &w.at[0], &w.at[1], &w.at[2]);
            for (int& a : w.at) a = std::clamp(a, 0, PER_SLOT - 1);
        }
        return true;
    }
};
