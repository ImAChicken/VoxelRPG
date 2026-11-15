#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <toml++/toml.hpp>

// Flags
constexpr uint32_t VF_SOLID        = 1 << 0;
constexpr uint32_t VF_FLUID        = 1 << 1;
constexpr uint32_t VF_DESTRUCTIBLE = 1 << 2;
constexpr uint32_t VF_CLIMBABLE    = 1 << 3;
constexpr uint32_t VF_EMITS_LIGHT  = 1 << 4;

// Opacity
enum class VoxelOpacity : uint8_t {
    Full        = 0,
    Partial     = 1,
    Translucent = 2,
    None        = 3,
};

struct VoxelTypeDef {
    uint16_t    id          = 0;
    std::string name        = "air";
    uint32_t    flags       = 0;
    uint8_t     hardness    = 0;
    uint16_t    onDestroyID = 0;
    // What it is made of — decides which tool digs it
    // fastest (stone → pickaxe, dirt → shovel, wood → axe)
    std::string material    = "none";
    VoxelOpacity opacity    = VoxelOpacity::None;

    // Render color RGB 0-1
    float color[3]          = {0, 0, 0};

    // Light emission — 0-31 per channel
    uint8_t emittedLight[3] = {0, 0, 0};

    bool isSolid()       const { return flags & VF_SOLID; }
    bool isFluid()       const { return flags & VF_FLUID; }
    bool isDestructible()const { return flags & VF_DESTRUCTIBLE; }
    bool emitsLight()    const { return flags & VF_EMITS_LIGHT; }
};

class VoxelRegistry {
public:
    // Load from TOML file
    bool load(const char* path) {
        try {
            auto tbl = toml::parse_file(path);

            // Air is always ID 0 — hardcoded
            VoxelTypeDef air;
            air.id      = 0;
            air.name    = "air";
            air.opacity = VoxelOpacity::None;
            ensureSize(1);
            types[0] = air;

            auto blocks = tbl["block"].as_array();
            if (!blocks) {
                std::cerr << "No blocks found in " << path << "\n";
                return false;
            }

            for (auto& entry : *blocks) {
                auto* tbl2 = entry.as_table();
                if (!tbl2) continue;

                VoxelTypeDef def;
                def.id   = (uint16_t)(*tbl2)["id"].value_or(0);
                def.name = (*tbl2)["name"].value_or("unknown");
                def.hardness    = (uint8_t)(*tbl2)["hardness"].value_or(0);
                def.onDestroyID = (uint16_t)(*tbl2)["on_destroy"].value_or(0);
                def.material    = (*tbl2)["material"].value_or("none");

                // Opacity
                std::string op = (*tbl2)["opacity"].value_or("full");
                if      (op == "full")        def.opacity = VoxelOpacity::Full;
                else if (op == "partial")     def.opacity = VoxelOpacity::Partial;
                else if (op == "translucent") def.opacity = VoxelOpacity::Translucent;
                else                          def.opacity = VoxelOpacity::None;

                // Flags
                def.flags = 0;
                if (auto* arr = (*tbl2)["flags"].as_array()) {
                    for (auto& f : *arr) {
                        std::string fs = f.value_or("");
                        if      (fs == "solid")        def.flags |= VF_SOLID;
                        else if (fs == "fluid")        def.flags |= VF_FLUID;
                        else if (fs == "destructible") def.flags |= VF_DESTRUCTIBLE;
                        else if (fs == "climbable")    def.flags |= VF_CLIMBABLE;
                        else if (fs == "emits_light")  def.flags |= VF_EMITS_LIGHT;
                    }
                }

                // Color
                if (auto* carr = (*tbl2)["color"].as_array()) {
                    if (carr->size() >= 3) {
                        def.color[0] = (float)(*carr)[0].value_or(0.0);
                        def.color[1] = (float)(*carr)[1].value_or(0.0);
                        def.color[2] = (float)(*carr)[2].value_or(0.0);
                    }
                }

                // Emitted light
                if (auto* larr = (*tbl2)["emitted_light"].as_array()) {
                    if (larr->size() >= 3) {
                        def.emittedLight[0] = (uint8_t)(*larr)[0].value_or(0);
                        def.emittedLight[1] = (uint8_t)(*larr)[1].value_or(0);
                        def.emittedLight[2] = (uint8_t)(*larr)[2].value_or(0);
                    }
                }

                ensureSize(def.id + 1);
                types[def.id] = def;
                nameIndex[def.name] = def.id;
            }

            std::cout << "Registry loaded: "
                      << types.size() << " block types from "
                      << path << "\n";
            return true;

        } catch (const toml::parse_error& e) {
            std::cerr << "TOML parse error: " << e << "\n";
            return false;
        }
    }

    const VoxelTypeDef& get(uint16_t id) const {
        if (id >= types.size()) return types[0];
        return types[id];
    }

    uint16_t getIDByName(const std::string& name) const {
        auto it = nameIndex.find(name);
        return it != nameIndex.end() ? it->second : 0;
    }

    size_t size() const { return types.size(); }

private:
    std::vector<VoxelTypeDef> types;
    std::unordered_map<std::string, uint16_t> nameIndex;

    void ensureSize(size_t n) {
        if (types.size() < n) types.resize(n);
    }
};

extern VoxelRegistry gRegistry;