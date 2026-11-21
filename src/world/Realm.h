#pragma once
// ══════════════════════════════════════════════════════════
// A realm: a main world and the instance dungeons opened from it
// ══════════════════════════════════════════════════════════
// docs/dungeons.md "Worlds, dimensions and saves" and "Instance
// dungeons", first version (one world loaded at a time; travel is a
// short fade). The main world's folder is the realm:
//
//   saves/<world>/              the main world, as always
//     realm.dat                 its instances, where the player is, its gate
//     instances/<id>/           an open instance: a world save of its own
//
// so an old save is already a realm with one world, and an instance is
// opened like any world (by the name "<world>/instances/<id>"). The
// player belongs to the realm: their file stays in the main world's
// folder wherever they are.
//
// An instance counts down while the game runs (in any of the realm's
// worlds): 'minutes' from portals.toml, 'after_boss' once its boss
// falls, 'close_warn' s once someone closes it at a gate. At zero
// whoever's inside is moved out and it's deleted (tick() says when;
// the game does the moving and calls remove()).

#include "WorldSaveManager.h"
#include "../worldgen/WorldType.h"
#include <toml++/toml.hpp>
#include <glm/glm.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace realm {

// ── The dungeons a gate can open (data/registry/portals.toml) ─
struct DungeonKind {
    std::string id, name, theme = "crypt";
    WorldType   type = WorldType::Dungeon;
    float minutes = 60.0f, afterBoss = 5.0f, closeWarn = 30.0f;
};

class DungeonKinds {
public:
    bool load(const char* path = "data/registry/portals.toml") {
        std::vector<DungeonKind> out;
        try {
            toml::table t = toml::parse_file(path);
            if (auto* arr = t["dungeon"].as_array())
                for (auto& e : *arr) {
                    auto* d = e.as_table(); if (!d) continue;
                    DungeonKind k;
                    k.id = (*d)["id"].value_or(std::string());
                    k.name = (*d)["name"].value_or(k.id);
                    k.theme = (*d)["theme"].value_or(std::string("crypt"));
                    k.type = worldTypeFromKey((*d)["type"].value_or(std::string("dungeon")));
                    k.minutes = (float)(*d)["minutes"].value_or(60.0);
                    k.afterBoss = (float)(*d)["after_boss"].value_or(5.0);
                    k.closeWarn = (float)(*d)["close_warn"].value_or(30.0);
                    if (!k.id.empty()) out.push_back(k);
                }
        } catch (const toml::parse_error& e) {
            std::cerr << path << ": " << e.description() << "\n";
            return false;
        }
        list = out;
        std::cout << "Dungeons: " << list.size() << " from " << path << "\n";
        return true;
    }
    const DungeonKind* find(const std::string& id) const {
        for (const DungeonKind& k : list) if (k.id == id) return &k;
        return nullptr;
    }
    const std::vector<DungeonKind>& all() const { return list; }
private:
    std::vector<DungeonKind> list;
};
inline DungeonKinds gDungeonKinds;

// ── One open instance ─────────────────────────────────────
struct Instance {
    std::string id;              // its folder under instances/ ("crypt-1a2b3c")
    std::string kind;            // portals.toml id
    uint64_t    seed = 0;
    float       left = 3600.0f;  // s until it closes
    bool        cleared = false; // its boss is beaten
    bool        closing = false; // closed at a gate: 'left' is the warning
    bool        warned10 = false, warned1 = false;
};

// ── The realm ─────────────────────────────────────────────
class Realm {
public:
    std::string main;            // the main world's name (its folder)
    bool        dev = false;     // in devworlds/
    std::vector<Instance> instances;
    std::string here;            // the world the player is in: "" the main one, else an instance id
    // the gate by the main world's spawn (game/Gate.h): where, which
    // way it faces, built yet
    bool        hasGate = false, gateBuilt = false;
    glm::ivec3  gate{ 0 };
    int         gateAxis = 0;

    bool loaded() const { return !main.empty(); }
    std::string folder() const { return std::string(WorldSaveManager::rootFor(dev)) + "/" + main; }
    std::string worldName(const Instance& i) const { return main + "/instances/" + i.id; }
    std::string instanceFolder(const Instance& i) const { return folder() + "/instances/" + i.id; }
    Instance* find(const std::string& id) {
        for (Instance& i : instances) if (i.id == id) return &i;
        return nullptr;
    }
    Instance* current() { return here.empty() ? nullptr : find(here); }
    // the instance the main world's gate leads to (the first one open)
    Instance* gateInstance() { return instances.empty() ? nullptr : &instances.front(); }
    const DungeonKind* kindOf(const Instance& i) const { return gDungeonKinds.find(i.kind); }

    // ── Saving (realm.dat) ────────────────────────────────
    // Lines "key value…"; anything unknown is skipped
    bool load(const std::string& mainName, bool devWorld) {
        main = mainName; dev = devWorld;
        instances.clear(); here.clear();
        hasGate = gateBuilt = false; gate = glm::ivec3(0); gateAxis = 0;
        std::ifstream f(folder() + "/realm.dat");
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            std::string k; ls >> k;
            if (k == "instance") {
                Instance i; int c = 0, cl = 0, w10 = 0, w1 = 0;
                if (ls >> i.id >> i.kind >> i.seed >> i.left >> c >> cl >> w10 >> w1) {
                    i.cleared = c; i.closing = cl; i.warned10 = w10; i.warned1 = w1;
                    if (validId(i.id) && std::filesystem::exists(instanceFolder(i) + "/world.vxrp")) instances.push_back(i);
                }
            } else if (k == "here") { ls >> here; }
            else if (k == "gate") { int b = 0; if (ls >> gate.x >> gate.y >> gate.z >> gateAxis >> b) { hasGate = true; gateBuilt = b; } }
        }
        if (!here.empty() && !find(here)) here.clear();
        return true;
    }
    bool save() const {
        if (!loaded()) return false;
        std::ofstream f(folder() + "/realm.dat");
        f << "# the realm (world/Realm.h): open instances (id kind seed seconds-left cleared closing warned10 warned1),\n"
             "# where the player is, the spawn gate (x y z axis built)\n";
        for (const Instance& i : instances)
            f << "instance " << i.id << " " << i.kind << " " << i.seed << " " << i.left << " " << (int)i.cleared << " "
              << (int)i.closing << " " << (int)i.warned10 << " " << (int)i.warned1 << "\n";
        if (!here.empty()) f << "here " << here << "\n";
        if (hasGate) f << "gate " << gate.x << " " << gate.y << " " << gate.z << " " << gateAxis << " " << (int)gateBuilt << "\n";
        return (bool)f;
    }

    // ── Opening and closing instances ─────────────────────
    // A new instance of a dungeon: its world made on disk (opened
    // later, when someone goes through). nullptr if it can't be
    Instance* create(const std::string& kind, uint64_t seed, std::string* why = nullptr) {
        const DungeonKind* k = gDungeonKinds.find(kind);
        if (!k) { if (why) *why = "no dungeon '" + kind + "'"; return nullptr; }
        Instance i;
        char hex[16]; std::snprintf(hex, sizeof(hex), "%06llx", (unsigned long long)(seed & 0xffffff));
        i.id = kind + "-" + hex;
        for (int n = 2; find(i.id) || std::filesystem::exists(instanceFolder(i)); n++) i.id = kind + "-" + hex + "-" + std::to_string(n);
        i.kind = kind; i.seed = seed; i.left = k->minutes * 60.0f;
        WorldSaveManager w;             // its own (every open world has its own)
        if (!w.createWorld(worldName(i), seed, GameMode::Survival, k->type, 0, -1, TerrainStyle::NoErosion,
                           WorldGenParams(), FlatOptions(), dev)) {
            if (why) *why = "couldn't make its folder";
            return nullptr;
        }
        instances.push_back(i);
        save();
        return &instances.back();
    }
    // Delete it (its folder and all): only when nobody is in it
    bool remove(const std::string& id) {
        Instance* i = find(id);
        if (!i || here == id) return false;
        std::error_code ec;
        if (validId(id)) std::filesystem::remove_all(instanceFolder(*i), ec);
        instances.erase(instances.begin() + (i - instances.data()));
        save();
        return true;
    }
    // Its boss is beaten: what's left is at most 'after_boss'
    void beaten(const std::string& id) {
        Instance* i = find(id);
        if (!i || i->cleared) return;
        i->cleared = true;
        const DungeonKind* k = kindOf(*i);
        i->left = std::min(i->left, (k ? k->afterBoss : 5.0f) * 60.0f);
        i->warned10 = true;
        save();
    }
    // Closed at a gate: everyone out after 'close_warn' s
    void close(const std::string& id) {
        Instance* i = find(id);
        if (!i || i->closing) return;
        const DungeonKind* k = kindOf(*i);
        i->closing = true;
        i->left = std::min(i->left, k ? k->closeWarn : 30.0f);
        i->warned10 = i->warned1 = true;
        save();
    }

    // ── Every frame the game runs ─────────────────────────
    struct Event {
        enum class Kind { Warning, Closed } kind = Kind::Warning;
        std::string id, text;
    };
    std::vector<Event> tick(float dt) {
        std::vector<Event> out;
        for (Instance& i : instances) {
            i.left = std::max(0.0f, i.left - dt);
            const DungeonKind* k = kindOf(i);
            std::string name = k ? k->name : i.kind;
            if (!i.warned10 && i.left <= 600.0f) { i.warned10 = true; out.push_back({ Event::Kind::Warning, i.id, name + " closes in 10 minutes" }); }
            if (!i.warned1 && i.left <= 60.0f) { i.warned1 = true; out.push_back({ Event::Kind::Warning, i.id, name + " closes in 1 minute" }); }
            if (i.left <= 0.0f) out.push_back({ Event::Kind::Closed, i.id, name + " has closed" });
        }
        return out;
    }

    // "54:07"
    static std::string clock(float s) {
        int t = (int)std::ceil(std::max(0.0f, s));
        char b[16]; std::snprintf(b, sizeof(b), "%d:%02d", t / 60, t % 60);
        return b;
    }
    // ids are made here: letters, digits and '-' only (never a path)
    static bool validId(const std::string& id) {
        if (id.empty() || id.size() > 64) return false;
        for (char c : id) if (!(std::isalnum((unsigned char)c) || c == '-')) return false;
        return true;
    }
};

} // namespace realm
