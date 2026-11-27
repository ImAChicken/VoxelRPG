#pragma once
#include <algorithm>
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
constexpr uint32_t VF_PLANT        = 1 << 5;   // part of a plant (decorations.toml): walk-through, picked up whole
constexpr uint32_t VF_GRAPPLE      = 1 << 6;   // a grapple point: the grapple catches on it (game/Grapple.h)

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

    // Light emission — 0-31 per channel (older field)
    uint8_t emittedLight[3] = {0, 0, 0};

    // Light for the baked light layer (world/LightBake.h):
    //   light      what it gives off, 0-15 per colour
    //   lightPass  how light gets through it: 0 clear, 1-14 dims
    //              it by that many levels per cell, 15 blocks it
    uint8_t light[3]  = {0, 0, 0};
    uint8_t lightPass = 15;
    bool    glows() const { return light[0] | light[1] | light[2]; }

    // render = "sprite": drawn as two crossed flat pictures (the
    // atlas tile, with see-through pixels) standing in the voxel,
    // like Minecraft flowers. A stack of the same sprite block is
    // one picture as tall as the stack. spriteWidth: how many voxels
    // wide the picture is (1 or 3), centred on the voxel.
    bool    sprite      = false;
    uint8_t spriteWidth = 1;

    // render = "pane": one flat see-through sheet through the middle
    // of the voxel (window glass), turned along the wall it's in, drawn
    // after the solid world (render/Mesher.h panes). filter = [r, g, b]
    // (0-1): what it lets through of each colour: light baked through it
    // is tinted (world/LightBake.h, like leaves tint the sky green), and
    // the world seen through it too. 1, 1, 1 (the default): clear
    bool    pane        = false;
    float   filter[3]   = {1, 1, 1};
    bool filters() const { return filter[0] < 0.999f || filter[1] < 0.999f || filter[2] < 0.999f; }

    bool isSolid()       const { return flags & VF_SOLID; }
    bool isFluid()       const { return flags & VF_FLUID; }
    bool isDestructible()const { return flags & VF_DESTRUCTIBLE; }
    bool emitsLight()    const { return flags & VF_EMITS_LIGHT; }
    bool isPlant()       const { return flags & VF_PLANT; }
    bool isGrapple()     const { return flags & VF_GRAPPLE; }
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
            air.lightPass = 0;
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
                        else if (fs == "plant")        def.flags |= VF_PLANT;
                        else if (fs == "grapple")      def.flags |= VF_GRAPPLE;
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

                // Light: "light = [r, g, b]" 0-15; older files give
                // "emitted_light" 0-31, which is halved
                if (auto* l2 = (*tbl2)["light"].as_array()) {
                    for (int k = 0; k < 3 && k < (int)l2->size(); k++)
                        def.light[k] = (uint8_t)std::min<int64_t>(15, (*l2)[k].value_or((int64_t)0));
                } else {
                    for (int k = 0; k < 3; k++) def.light[k] = (uint8_t)std::min(15, def.emittedLight[k] / 2);
                }
                if (def.glows()) def.flags |= VF_EMITS_LIGHT;
                // how light passes: from opacity unless given
                def.lightPass = def.opacity == VoxelOpacity::Full ? 15 : def.opacity == VoxelOpacity::Partial ? 1
                              : def.opacity == VoxelOpacity::Translucent ? 1 : 0;
                def.sprite = (*tbl2)["render"].value_or(std::string()) == "sprite";
                def.pane = (*tbl2)["render"].value_or(std::string()) == "pane";
                if (auto* f = (*tbl2)["filter"].as_array())
                    for (int k = 0; k < 3 && k < (int)f->size(); k++)
                        def.filter[k] = std::clamp((float)(*f)[k].value_or(1.0), 0.0f, 1.0f);
                def.spriteWidth = (uint8_t)std::clamp<int64_t>((*tbl2)["sprite_width"].value_or((int64_t)1), 1, 5) | 1;
                if (auto lp = (*tbl2)["light_pass"].value<std::string>()) {
                    if (*lp == "clear") def.lightPass = 0;
                    else if (*lp == "block") def.lightPass = 15;
                    else if (*lp == "dim") def.lightPass = 2;
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