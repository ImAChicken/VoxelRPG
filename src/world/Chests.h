#pragma once
#include "../game/Container.h"
#include "../game/StructureRegistry.h"
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Chests — the containers of placed structures
// ══════════════════════════════════════════════════════════
// Any structure type with slots (structures.toml: a chest has 50
// by default; any kind may set 'slots', and 'loot' for a loot
// table) holds a Container (game/Container.h), kept here by its
// structure instance id. E on one opens it beside your own
// inventory (ui/InventoryScreen.h).
//
// A container is made the first time it's asked for, from its
// type: unread, with the type's loot table, so a chest a world
// or a command puts down rolls its loot when first opened. One
// you build with the Builder's Hammer starts read and empty
// (makeEmpty), so building chests is no way to make loot.
//
// When a structure goes (broken, removed, collapsed), main.cpp
// takes everything out of here (rolling the loot if it was never
// opened) and spills it as pickups.
//
// Saved per world in containers.dat:
//   box <instance id>
//   c.box=50 bandit_camp 1234 1     (Container::write, prefix "c")
//   c.i3=planks 20
class ChestStore {
public:
    uint32_t worldSeed = 0;    // mixed into each container's loot seed

    static bool holds(const StructureType* t) { return t && t->slots > 0; }

    // The container of instance 'id' of type t (made unread from
    // the type the first time)
    Container& get(uint32_t id, const StructureType& t) {
        auto it = boxes.find(id);
        if (it == boxes.end())
            it = boxes.emplace(id, Container(t.slots, t.loot, seedFor(id))).first;
        return it->second;
    }
    Container* find(uint32_t id) {
        auto it = boxes.find(id);
        return it == boxes.end() ? nullptr : &it->second;
    }
    // Built by a player: read and empty
    void makeEmpty(uint32_t id, const StructureType& t) { get(id, t).makeEmpty(); }

    // It's gone: everything it held (its loot rolled if unread)
    std::vector<std::pair<uint16_t, int>> take(uint32_t id, const StructureType* t) {
        std::vector<std::pair<uint16_t, int>> out;
        if (!holds(t) && !boxes.count(id)) return out;
        Container& c = t ? get(id, *t) : boxes[id];
        out = c.takeAll();
        boxes.erase(id);
        return out;
    }
    void clear() { boxes.clear(); }
    size_t size() const { return boxes.size(); }

    uint32_t seedFor(uint32_t id) const { return (id * 2654435761u) ^ (worldSeed * 0x9E3779B9u) ^ 0x5bd1e995u; }

    bool save(const std::string& worldFolder) const {
        std::ofstream f(worldFolder + "/containers.dat");
        if (!f) return false;
        f << "containers 1\n";
        for (const auto& [id, c] : boxes) {
            f << "box " << id << "\n";
            c.write(f, "c");
        }
        return (bool)f;
    }
    void load(const std::string& worldFolder) {
        boxes.clear();
        std::ifstream f(worldFolder + "/containers.dat");
        std::string line;
        Container* cur = nullptr;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.rfind("box ", 0) == 0) {
                cur = &boxes[(uint32_t)std::stoul(line.substr(4))];
                continue;
            }
            size_t eq = line.find('=');
            if (cur && eq != std::string::npos) cur->readLine(line.substr(0, eq), line.substr(eq + 1), "c");
        }
        for (auto& [id, c] : boxes) c.finishLoad();
    }

private:
    std::unordered_map<uint32_t, Container> boxes;
};
