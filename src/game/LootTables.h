#pragma once
#include "ItemRegistry.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// LootTables — what a container holds before anyone looks
// (data/registry/loot.toml)
// ══════════════════════════════════════════════════════════
// A container (game/Container.h: a chest, an NPC's pack) can
// name a loot table instead of holding items. Nothing is made
// until something reads the container: the first time it's
// opened, searched or broken, the table is rolled once and the
// items are put in. After that it's an ordinary inventory.
//
// A table:
//   rolls   [min, max] picks from 'pick' (or one number)
//   pick    weighted entries; one is chosen per roll
//   always  entries each rolled once on their own 'chance'
// An entry:
//   item    an item name (items.toml, or a block)
//   table   or: roll another table (up to 4 deep)
//   count   [min, max] or one number (default 1)
//   weight  for 'pick' (default 1); an entry with neither
//           item nor table is "nothing"
//   chance  0..1 (default 1)
// Rolls are seeded: the same table and seed give the same items.
struct LootEntry {
    std::string item, table;
    int   countMin = 1, countMax = 1;
    float weight = 1.0f;
    float chance = 1.0f;
};
struct LootTable {
    std::string id;
    int rollsMin = 1, rollsMax = 1;
    std::vector<LootEntry> pick, always;
};

class LootTables {
public:
    using Items = std::vector<std::pair<uint16_t, int>>;   // item, count

    bool load(const char* path) {
        tables.clear();
        try {
            auto tbl = toml::parse_file(path);
            auto range = [](const toml::node_view<toml::node>& n, int& lo, int& hi, int def) {
                if (auto a = n.as_array(); a && a->size() == 2) {
                    lo = (int)a->get(0)->value_or((int64_t)def);
                    hi = (int)a->get(1)->value_or((int64_t)lo);
                } else lo = hi = (int)n.value_or((int64_t)def);
                lo = std::max(0, lo); hi = std::max(lo, hi);
            };
            auto entries = [&](const toml::node_view<toml::node>& n, std::vector<LootEntry>& out, const std::string& where) {
                auto* arr = n.as_array();
                if (!arr) return;
                for (auto& e : *arr) {
                    auto* t = e.as_table();
                    if (!t) continue;
                    LootEntry le;
                    le.item   = (*t)["item"].value_or(std::string());
                    le.table  = (*t)["table"].value_or(std::string());
                    le.weight = (float)(*t)["weight"].value_or(1.0);
                    le.chance = (float)(*t)["chance"].value_or(1.0);
                    toml::node_view<toml::node> c{ t->get("count") };
                    range(c, le.countMin, le.countMax, 1);
                    if (!le.item.empty() && !gItems.find(le.item))
                        std::cerr << "loot.toml: " << where << ": unknown item " << le.item << "\n";
                    out.push_back(le);
                }
            };
            if (auto arr = tbl["table"].as_array())
                for (auto& node : *arr) {
                    auto* t = node.as_table();
                    if (!t) continue;
                    LootTable lt;
                    lt.id = (*t)["id"].value_or(std::string());
                    if (lt.id.empty()) continue;
                    toml::node_view<toml::node> r{ t->get("rolls") };
                    range(r, lt.rollsMin, lt.rollsMax, 1);
                    entries(toml::node_view<toml::node>{ t->get("pick") }, lt.pick, lt.id);
                    entries(toml::node_view<toml::node>{ t->get("always") }, lt.always, lt.id);
                    tables[lt.id] = lt;
                }
            for (auto& [id, t] : tables)
                for (auto* list : { &t.pick, &t.always })
                    for (auto& e : *list)
                        if (!e.table.empty() && !tables.count(e.table))
                            std::cerr << "loot.toml: " << id << ": unknown table " << e.table << "\n";
        } catch (const toml::parse_error& e) {
            std::cerr << "loot.toml: " << e.description() << "\n";
            return false;
        }
        std::cout << "Loot tables: " << tables.size() << "\n";
        return true;
    }

    const LootTable* get(const std::string& id) const {
        auto it = tables.find(id);
        return it == tables.end() ? nullptr : &it->second;
    }
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        for (auto& [id, t] : tables) out.push_back(id);
        return out;
    }

    // Roll a table: the items it gives (same item merged, in the
    // order first rolled). Unknown table: nothing.
    Items roll(const std::string& id, uint32_t seed) const {
        Rng rng{ seed ? seed : 0x2545F491u };
        std::map<uint16_t, int> got;
        std::vector<uint16_t> order;
        rollInto(id, rng, got, order, 0);
        Items out;
        for (uint16_t i : order) out.push_back({ i, got[i] });
        return out;
    }

private:
    struct Rng {
        uint64_t s;
        uint32_t next() {   // splitmix64
            uint64_t z = (s += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return (uint32_t)((z ^ (z >> 31)) >> 32);
        }
        float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
        int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
    };

    void give(const LootEntry& e, Rng& rng, std::map<uint16_t, int>& got, std::vector<uint16_t>& order, int depth) const {
        if (!e.table.empty()) { rollInto(e.table, rng, got, order, depth + 1); return; }
        if (e.item.empty()) return;                     // "nothing"
        uint16_t id = gItems.find(e.item);
        int n = rng.range(e.countMin, e.countMax);
        if (!id || n <= 0) return;
        if (!got.count(id)) order.push_back(id);
        got[id] += n;
    }
    void rollInto(const std::string& id, Rng& rng, std::map<uint16_t, int>& got, std::vector<uint16_t>& order, int depth) const {
        const LootTable* t = get(id);
        if (!t || depth > 4) return;
        for (const LootEntry& e : t->always)
            if (rng.unit() < e.chance) give(e, rng, got, order, depth);
        float total = 0.0f;
        for (const LootEntry& e : t->pick) total += std::max(0.0f, e.weight);
        if (total <= 0.0f) return;
        int rolls = rng.range(t->rollsMin, t->rollsMax);
        for (int r = 0; r < rolls; r++) {
            float x = rng.unit() * total;
            for (const LootEntry& e : t->pick) {
                x -= std::max(0.0f, e.weight);
                if (x < 0.0f) { if (rng.unit() < e.chance) give(e, rng, got, order, depth); break; }
            }
        }
    }

    std::unordered_map<std::string, LootTable> tables;
};

inline LootTables gLoot;
