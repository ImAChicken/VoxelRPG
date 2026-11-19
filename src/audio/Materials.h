#pragma once
#include "Sfx.h"
#include "../voxel/VoxelRegistry.h"
#include <toml++/toml.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Materials — how each kind of stuff sounds, and stops sound
// ══════════════════════════════════════════════════════════
// Keyed by a block's material (blocks.toml material = …), so every
// block — and any block added later — sounds right with no work:
//
//   tone     what it sounds like struck, stepped on, broken
//            (audio/Sfx.h Tone)
//   muffle   how much sound one voxel of it stops, passing through
//            (audio/Acoustics.h): stone a lot, wood less, leaves
//            and grass almost nothing, water a lot and dull
//   echo     how much it reflects (a stone room rings, a wooden
//            one is warmer, open ground is dry)
//
// data/audio/materials.toml changes any of these or adds a material;
// a block can name its own sound kind (blocks.toml sound = "snow").
// The defaults below are what's used without the file.
namespace sound {

struct Material {
    sfx::Tone tone;
    float muffle = 1.0f;     // per voxel crossed (1: stone)
    float echo = 0.5f;       // 0 .. 1
};

class MaterialTable {
public:
    MaterialTable() { defaults(); }

    const Material& get(const std::string& name) const {
        auto it = byName.find(name);
        if (it != byName.end()) return it->second;
        return byName.at("none");
    }
    // A block's: its own sound kind if it names one, else its material's
    const Material& forBlock(uint16_t id) const {
        if (id < cache.size() && cache[id]) return *cache[id];
        return get(nameFor(id));
    }
    static std::string nameFor(uint16_t id) {
        if (id == 0 || id >= gRegistry.size()) return "air";
        const VoxelTypeDef& d = gRegistry.get(id);
        if (d.name == "snow") return "snow";
        if (d.name == "glass") return "glass";
        if (d.name == "sand" || d.name == "gravel") return d.name;
        if (d.name.find("leaves") != std::string::npos) return "leaves";
        if (d.flags & VF_PLANT) return "plant";
        return d.material;
    }
    // The table for every block id (call after the registry loads)
    void bind() {
        cache.assign(gRegistry.size(), nullptr);
        for (size_t id = 0; id < cache.size(); id++) {
            std::string n = nameFor((uint16_t)id);
            auto it = byName.find(n);
            cache[id] = it != byName.end() ? &it->second : &byName.at("none");
        }
    }

    bool load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return false;
        try {
            auto t = toml::parse_file(path.string());
            for (auto&& [k, v] : t) {
                auto* tb = v.as_table();
                if (!tb) continue;
                Material m = byName.count(std::string(k.str())) ? byName[std::string(k.str())] : byName["none"];
                auto f = [&](const char* key, float& out) { if (auto x = (*tb)[key].value<double>()) out = (float)*x; };
                f("click_hz", m.tone.clickHz); f("body_hz", m.tone.body); f("body_q", m.tone.bodyQ);
                f("noise_hz", m.tone.noiseHz); f("decay", m.tone.decay); f("crackle", m.tone.crackle);
                f("wet", m.tone.wet); f("ring", m.tone.ring); f("muffle", m.muffle); f("echo", m.echo);
                byName[std::string(k.str())] = m;
            }
            bind();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "audio materials: " << e.what() << "\n";
            return false;
        }
    }

private:
    std::unordered_map<std::string, Material> byName;
    std::vector<const Material*> cache;

    void defaults() {
        auto add = [&](const char* n, float click, float body, float q, float noise, float decay, float crackle, float wet, float ring,
                       float muffle, float echo) {
            Material m;
            m.tone = { click, body, q, noise, decay, crackle, wet, ring };
            m.muffle = muffle; m.echo = echo;
            byName[n] = m;
        };
        //   name       click   body   q    noise  decay crackle wet ring  muffle echo
        add("stone",   4200,  1100,  5,  2600, 0.035f, 0.0f, 0, 0,    1.00f, 0.85f);
        add("gravel",  3800,   700,  0,  2200, 0.05f,  0.8f, 0, 0,    0.90f, 0.60f);
        add("glass",   6000,  2400, 14,  4000, 0.04f,  0.0f, 0, 0.3f, 0.45f, 0.90f);
        add("metal",   5000,  1300, 20,  3000, 0.05f,  0.0f, 0, 1.0f, 1.00f, 0.90f);
        add("wood",    2500,   420,  9,  1200, 0.06f,  0.0f, 0, 0,    0.60f, 0.55f);
        add("dirt",    1200,   180,  2,   600, 0.06f,  0.1f, 0, 0,    0.90f, 0.25f);
        add("sand",    2500,   200,  0,  1600, 0.07f,  0.5f, 0, 0,    0.90f, 0.20f);
        add("snow",    1800,   200,  0,  1200, 0.08f,  0.9f, 0, 0,    0.70f, 0.10f);
        add("organic", 2000,   300,  0,  2500, 0.06f,  0.7f, 0, 0,    0.15f, 0.15f);
        add("leaves",  2400,   300,  0,  3200, 0.07f,  1.0f, 0, 0,    0.12f, 0.10f);
        add("plant",   2400,   300,  0,  3000, 0.06f,  0.9f, 0, 0,    0.03f, 0.05f);
        add("fluid",   1500,   300,  0,  1800, 0.10f,  0.0f, 1, 0,    0.70f, 0.40f);
        add("none",    2000,   400,  0,  1500, 0.05f,  0.2f, 0, 0,    0.50f, 0.30f);
        add("slime",    900,   170,  3,   700, 0.09f,  0.0f, 0.6f, 0, 0.30f, 0.10f);
        add("air",     2000,   400,  0,  1500, 0.05f,  0.0f, 0, 0,    0.00f, 0.00f);
    }
};

inline MaterialTable gMaterials;

} // namespace sound
