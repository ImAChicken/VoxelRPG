#pragma once
#include <toml++/toml.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Factions — who is on whose side (data/registry/factions.toml)
// ══════════════════════════════════════════════════════════
// A standing from -100 to 100 between two sides:
//   NPC ↔ NPC     the faction table (relation)
//   NPC ↔ player  that player's own reputation with the faction
//                 (Player::reputation, saved per player), which
//                 starts at the faction's player_standing
// Hostile at or below hostileBelow, friendly at or above
// friendlyAbove, neutral between. A faction is always friendly
// to itself.
//
// Factions are referred to by a small index (fast to compare,
// cheap to send over the network); -1 = none.
class FactionRegistry {
public:
    struct Faction { std::string id, name; float playerStanding = 0.0f; };

    float hostileBelow = -30.0f, friendlyAbove = 30.0f;

    bool load(const char* path) {
        std::vector<Faction> fs;
        std::unordered_map<std::string, int> idx;
        std::vector<std::vector<float>> rel;
        try {
            auto tbl = toml::parse_file(path);
            hostileBelow = (float)tbl["hostile_below"].value_or(-30.0);
            friendlyAbove = (float)tbl["friendly_above"].value_or(30.0);
            if (auto arr = tbl["faction"].as_array())
                for (auto& e : *arr) {
                    auto* t = e.as_table(); if (!t) continue;
                    Faction f;
                    f.id = (*t)["id"].value_or(std::string());
                    f.name = (*t)["name"].value_or(f.id);
                    f.playerStanding = (float)(*t)["player_standing"].value_or(0.0);
                    if (f.id.empty()) continue;
                    idx[f.id] = (int)fs.size();
                    fs.push_back(f);
                }
            rel.assign(fs.size(), std::vector<float>(fs.size(), 0.0f));
            for (size_t i = 0; i < fs.size(); i++) rel[i][i] = 100.0f;
            if (auto arr = tbl["relation"].as_array())
                for (auto& e : *arr) {
                    auto* t = e.as_table(); if (!t) continue;
                    std::string a = (*t)["a"].value_or(std::string()), b = (*t)["b"].value_or(std::string());
                    auto ia = idx.find(a), ib = idx.find(b);
                    if (ia == idx.end() || ib == idx.end()) { std::cerr << path << ": unknown faction in " << a << "/" << b << "\n"; continue; }
                    float s = (float)(*t)["standing"].value_or(0.0);
                    rel[ia->second][ib->second] = rel[ib->second][ia->second] = s;
                }
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << "\n";
            return false;
        }
        factions = std::move(fs); index = std::move(idx); relations = std::move(rel);
        std::cout << "Factions: " << factions.size() << " from " << path << "\n";
        return true;
    }

    int find(const std::string& id) const { auto it = index.find(id); return it == index.end() ? -1 : it->second; }
    int count() const { return (int)factions.size(); }
    const Faction* get(int i) const { return i >= 0 && i < count() ? &factions[i] : nullptr; }
    std::string idOf(int i) const { return get(i) ? factions[i].id : std::string(); }

    float standing(int a, int b) const {
        if (a < 0 || b < 0 || a >= count() || b >= count()) return 0.0f;
        return relations[a][b];
    }
    bool hostile(float s) const { return s <= hostileBelow; }
    bool friendly(float s) const { return s >= friendlyAbove; }

private:
    std::vector<Faction> factions;
    std::unordered_map<std::string, int> index;
    std::vector<std::vector<float>> relations;
};
inline FactionRegistry gFactions;
