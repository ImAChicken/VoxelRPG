#pragma once
#include "Inventory.h"
#include "LootTables.h"
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ══════════════════════════════════════════════════════════
// Container — an inventory any entity can carry
// ══════════════════════════════════════════════════════════
// A chest has one (world/Chests.h keeps them by structure
// instance), an NPC has one (Npc::pack), and anything else can
// take one the same way: a capacity and, optionally, a loot
// table (game/LootTables.h).
//
// Until something READS it, a container is only its loot table
// and a seed: no Inventory exists, nothing is rolled, nothing is
// saved but those. The first read() rolls the table into a real
// inventory (a store: every slot a plain slot), and from then on
// it's ordinary items that save and load. Reading means opening
// it, searching a body, or breaking a chest (its loot spills).
// A body that fades unsearched never rolls at all.
struct Container {
    int         capacity = Inventory::SLOTS;
    std::string loot;          // loot table rolled on the first read ("" = starts empty)
    uint32_t    seed = 0;
    std::string label;         // a name for it ("Weapons"): shown when you aim at it and as its title
    bool        showcase = false;   // dev worlds: taking from it gives a copy (game/DevKit.h)

    Container() = default;
    Container(int cap, std::string table, uint32_t s) : capacity(cap), loot(std::move(table)), seed(s) {}
    Container(Container&&) = default;
    Container& operator=(Container&&) = default;

    bool exists() const { return capacity > 0; }
    // Has it been read (rolled) yet?
    bool opened() const { return (bool)inv; }
    // Could there be anything in it? (unread with a table: maybe)
    bool mayHoldItems() const { return exists() && (inv ? !inv->isEmpty() : !loot.empty()); }

    // The items, rolling the loot table the first time
    Inventory& read() {
        if (!inv) {
            inv = std::make_unique<Inventory>();
            inv->storage  = true;
            inv->capacity = std::clamp(capacity, 1, Inventory::SLOTS);
            inv->showcase = showcase;
            if (!loot.empty())
                for (auto& [item, n] : gLoot.roll(loot, seed)) inv->add(item, n);   // what doesn't fit is lost
        }
        return *inv;
    }
    // The items if it's been read, without rolling (null: not yet)
    const Inventory* peek() const { return inv.get(); }

    void setShowcase(bool on) { showcase = on; if (inv) inv->showcase = on; }

    // Everything out (it's going: broken, dropped): reads it first.
    // A showcase gives nothing (its items are only on show)
    std::vector<std::pair<uint16_t, int>> takeAll() {
        std::vector<std::pair<uint16_t, int>> out;
        if (!exists() || showcase) return out;
        for (ItemStack& s : read().slots)
            if (!s.empty()) { out.push_back({ s.itemID, s.count }); s.clear(); }
        return out;
    }

    // Back to unread with a (new) table: the dev 'loot' command
    void reset(const std::string& table, uint32_t newSeed) { inv.reset(); loot = table; seed = newSeed; }
    // Read and empty: a chest you built yourself starts like this
    void makeEmpty() { loot.clear(); inv.reset(); read(); }

    // ── Saving ────────────────────────────────────────────
    //   <prefix>.box=<capacity> <loot or -> <seed> <0|1 read>
    //   <prefix>.label=Weapons   <prefix>.show=1   (if set)
    //   <prefix>.i3=planks 20 …          (once read: Inventory::write)
    void write(std::ostream& out, const std::string& prefix) const {
        out << prefix << ".box=" << capacity << " " << (loot.empty() ? "-" : loot) << " " << seed << " " << (inv ? 1 : 0) << "\n";
        if (!label.empty()) out << prefix << ".label=" << label << "\n";
        if (showcase) out << prefix << ".show=1\n";
        if (inv) inv->write(out, prefix);
    }
    bool readLine(const std::string& key, const std::string& value, const std::string& prefix) {
        if (key == prefix + ".box") {
            std::istringstream in(value);
            int r = 0; std::string table;
            in >> capacity >> table >> seed >> r;
            loot = table == "-" ? "" : table;
            inv.reset();
            if (r) { std::string keep = loot; loot.clear(); read(); loot = keep; }   // read, but don't roll
            return true;
        }
        if (key == prefix + ".label") { label = value; return true; }
        if (key == prefix + ".show")  { setShowcase(value == "1"); return true; }
        return inv && inv->read(key, value, prefix);
    }
    void finishLoad() { if (inv) inv->finishLoad(); }

private:
    std::unique_ptr<Inventory> inv;   // null until read
};
