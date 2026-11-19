#pragma once
#include "PieceSpec.h"
#include "StructureRegistry.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Palettes — what a building is made of, picked once
// ══════════════════════════════════════════════════════════
// A palette is a block for each role of a made piece (the wall,
// its posts, beams, base, inlay, inside paint and panels, window
// glass, door boards) and a finish for plain walls (half-timbered
// outside, painted inside …). The Builder's Hammer equips one
// (build menu, Palettes tab) and every wall, post and door it
// places takes it; a village takes one for all its buildings.
//
//   data/registry/palettes.toml   the curated ones, and the blocks
//                                 the editor offers for each role
//   data/palettes/<name>.toml     a player's own: new ones, and
//                                 their edits of curated ones (Reset
//                                 deletes the file: the curated one
//                                 is back)
//
// Only the look: what a piece costs doesn't change with its blocks.
struct BuildPalette {
    std::string name;                 // file name: a-z 0-9 _
    std::string display;
    PieceSpec   spec;                 // blocks (role → block) and outside / inside
    std::vector<std::string> tags;    // "church": a village uses it for that kind of building
    bool curated = false;             // in palettes.toml
    bool edited  = false;             // a player's own file (a new one, or over a curated one)
};

class PaletteLibrary {
public:
    static constexpr const char* REGISTRY = "data/registry/palettes.toml";
    static constexpr const char* DIR = "data/palettes";

    int load(const std::string& registry = REGISTRY, const std::string& dir = DIR) {
        all.clear(); choices.clear(); stock.clear();
        userDir = dir;
        try {
            auto t = toml::parse_file(registry);
            if (auto* c = t["choices"].as_table())
                for (auto& [k, v] : *c)
                    if (auto* a = v.as_array())
                        for (auto& n : *a) if (auto s = n.value<std::string>()) choices[std::string(k.str())].push_back(*s);
            if (auto* arr = t["palette"].as_array())
                for (auto& node : *arr)
                    if (auto* p = node.as_table()) {
                        BuildPalette pal = read(*p, "");
                        pal.curated = true;
                        if (!pal.name.empty()) { stock[pal.name] = pal; all.push_back(pal); }
                    }
        } catch (const toml::parse_error& e) {
            std::cerr << "palettes.toml: " << e.description() << "\n";
        }
        // a player's own: new ones, or their edits of curated ones
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            std::vector<std::filesystem::path> files;
            for (auto& e : std::filesystem::directory_iterator(dir, ec))
                if (e.path().extension() == ".toml") files.push_back(e.path());
            std::sort(files.begin(), files.end());
            for (auto& f : files) {
                try {
                    BuildPalette p = read(toml::parse_file(f.string()), f.stem().string());
                    p.edited = true;
                    p.curated = stock.count(p.name) > 0;
                    if (p.curated && p.tags.empty()) p.tags = stock[p.name].tags;   // an edit keeps what it's for
                    put(p);
                } catch (const toml::parse_error& e) {
                    std::cerr << "palette " << f.string() << ": " << e.description() << "\n";
                }
            }
        }
        std::cout << "Palettes: " << all.size() << "\n";
        return (int)all.size();
    }

    const std::vector<BuildPalette>& list() const { return all; }
    const BuildPalette* find(const std::string& name) const {
        for (const BuildPalette& p : all) if (p.name == name) return &p;
        return nullptr;
    }
    // The blocks the editor offers for a role (palettes.toml [choices])
    const std::vector<std::string>& options(const std::string& role) const {
        static const std::vector<std::string> none;
        auto it = choices.find(role);
        return it == choices.end() ? none : it->second;
    }

    // Save a player's change (or a new one) to their folder
    bool save(BuildPalette p) {
        p.edited = true;
        p.curated = stock.count(p.name) > 0;
        if (p.curated && p.tags.empty()) p.tags = stock[p.name].tags;
        std::error_code ec;
        std::filesystem::create_directories(userDir, ec);
        std::ofstream f(userDir + "/" + p.name + ".toml");
        if (!f) return false;
        f << "# A building palette (game/Palettes.h), saved by the hammer's Palettes tab\n";
        f << "name = \"" << p.name << "\"\ndisplay = \"" << p.display << "\"\n";
        write(f, p.spec);
        put(p);
        return (bool)f;
    }
    // A curated one as it came; a player's own one is deleted
    void reset(const std::string& name) {
        std::error_code ec;
        std::filesystem::remove(userDir + "/" + name + ".toml", ec);
        auto st = stock.find(name);
        if (st != stock.end()) put(st->second);
        else all.erase(std::remove_if(all.begin(), all.end(), [&](const BuildPalette& p) { return p.name == name; }), all.end());
    }
    // A free name: base, base_2 …
    std::string freeName(const std::string& base) const {
        if (!find(base)) return base;
        for (int i = 2;; i++) if (!find(base + "_" + std::to_string(i))) return base + "_" + std::to_string(i);
    }

    // As toml lines: outside / inside / one line per role
    static void write(std::ostream& f, const PieceSpec& s) {
        if (!s.outside.empty()) f << "outside = \"" << s.outside << "\"\n";
        if (!s.inside.empty()) f << "inside = \"" << s.inside << "\"\n";
        for (auto& [k, v] : s.blocks) f << k << " = \"" << v << "\"\n";
    }
    static PieceSpec readSpec(const toml::table& t) {
        PieceSpec s;
        s.outside = t["outside"].value_or(std::string());
        s.inside = t["inside"].value_or(std::string());
        for (const PieceRole& r : pieceRoles())
            if (auto v = t[r.id].value<std::string>()) s.blocks[r.id] = *v;
        // the same checks as any spec (names only, no odd text)
        return PieceSpec::parse(s.text());
    }

private:
    std::vector<BuildPalette> all;
    std::map<std::string, BuildPalette> stock;                     // the curated ones as they came
    std::map<std::string, std::vector<std::string>> choices;  // role → blocks
    std::string userDir = DIR;

    static BuildPalette read(const toml::table& t, const std::string& stem) {
        BuildPalette p;
        p.name = t["name"].value_or(stem);
        p.display = t["display"].value_or(p.name);
        p.spec = readSpec(t);
        if (auto* a = t["tags"].as_array())
            for (auto& n : *a) if (auto v = n.value<std::string>()) p.tags.push_back(*v);
        return p;
    }
    void put(const BuildPalette& p) {
        for (BuildPalette& o : all) if (o.name == p.name) { o = p; return; }
        all.push_back(p);
    }
};

inline PaletteLibrary gBuildPalettes;
