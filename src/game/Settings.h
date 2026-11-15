#pragma once
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

// ══════════════════════════════════════════════════════════
// Settings — player preferences (Esc -> Settings)
// ══════════════════════════════════════════════════════════
// Saved to data/settings.toml whenever the Settings screen
// changes something. Adding a setting: a field here, a line
// in load() and save(), and a row in ui/SettingsScreen.h.
struct GameSettings {
    // Defaults — also what "Reset to defaults" restores
    static constexpr float DEF_TOOLBAR_SCALE   = 1.0f;
    static constexpr float DEF_TOOLBAR_OPACITY = 1.0f;
    static constexpr float DEF_SENSITIVITY     = 0.1f;
    // View: real voxel chunks out to chunkDistance; the
    // distant terrain out to renderDistance (in chunks).
    // renderDistance at RENDER_ALL or more = the whole world.
    static constexpr float DEF_CHUNK_DISTANCE  = 6.0f;
    static constexpr float RENDER_MIN = 16.0f, RENDER_ALL = 160.0f;
    static constexpr float DEF_RENDER_DISTANCE = RENDER_ALL;

    float toolbarScale     = DEF_TOOLBAR_SCALE;     // 0.5 .. 2.0
    float toolbarOpacity   = DEF_TOOLBAR_OPACITY;   // 0.1 .. 1.0
    float mouseSensitivity = DEF_SENSITIVITY;       // degrees per pixel
    float chunkDistance    = DEF_CHUNK_DISTANCE;    // 2 .. 16 chunks
    float renderDistance   = DEF_RENDER_DISTANCE;   // 16 .. 160 chunks (160 = all)

    int  chunkDistanceInt() const { return (int)std::lround(chunkDistance); }
    bool renderAll()        const { return renderDistance >= RENDER_ALL - 0.5f; }

    // Characters
    int charEdges     = 1;   // 0 off, 1 lines, 2 lines + colour echo
    int charLineColor = 0;   // 0 black, 1 tinted by the surface
    int charHairMesh  = 1;   // 0 voxels, 1 shards, 2 soft shards

    std::filesystem::path path = "data/settings.toml";

    void resetToDefaults() {
        toolbarScale     = DEF_TOOLBAR_SCALE;
        toolbarOpacity   = DEF_TOOLBAR_OPACITY;
        mouseSensitivity = DEF_SENSITIVITY;
        chunkDistance    = DEF_CHUNK_DISTANCE;
        renderDistance   = DEF_RENDER_DISTANCE;
        charEdges = 1; charLineColor = 0; charHairMesh = 1;
    }

    void clamp() {
        toolbarScale     = std::clamp(toolbarScale,     0.5f,  2.0f);
        toolbarOpacity   = std::clamp(toolbarOpacity,   0.1f,  1.0f);
        mouseSensitivity = std::clamp(mouseSensitivity, 0.02f, 0.4f);
        chunkDistance    = std::clamp(chunkDistance, 2.0f, 16.0f);
        renderDistance   = std::clamp(renderDistance, RENDER_MIN, RENDER_ALL);
        charEdges     = std::clamp(charEdges, 0, 2);
        charLineColor = std::clamp(charLineColor, 0, 1);
        charHairMesh  = std::clamp(charHairMesh, 0, 2);
    }

    bool load() {
        if (!std::filesystem::exists(path)) return false;
        try {
            auto t = toml::parse_file(path.string());
            toolbarScale     = (float)t["hud"]["toolbar_scale"].value_or((double)toolbarScale);
            toolbarOpacity   = (float)t["hud"]["toolbar_opacity"].value_or((double)toolbarOpacity);
            mouseSensitivity = (float)t["controls"]["mouse_sensitivity"].value_or((double)mouseSensitivity);
            chunkDistance  = (float)t["view"]["chunk_distance"].value_or((double)chunkDistance);
            renderDistance = (float)t["view"]["render_distance"].value_or((double)renderDistance);
            charEdges     = (int)t["characters"]["edges"].value_or((int64_t)charEdges);
            charLineColor = (int)t["characters"]["line_color"].value_or((int64_t)charLineColor);
            charHairMesh  = (int)t["characters"]["hair_mesh"].value_or((int64_t)charHairMesh);
            clamp();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "settings.toml error: " << e.what() << "\n";
            return false;
        }
    }

    bool save() const {
        std::ofstream f(path);
        if (!f) return false;
        f << "# VoxelRPG settings — change in game: Esc -> Settings\n\n"
          << "[hud]\n"
          << "toolbar_scale   = " << toolbarScale   << "   # 0.5 - 2.0\n"
          << "toolbar_opacity = " << toolbarOpacity << "   # 0.1 - 1.0\n\n"
          << "[controls]\n"
          << "mouse_sensitivity = " << mouseSensitivity << "   # 0.02 - 0.4\n\n"
          << "[view]\n"
          << "chunk_distance  = " << chunkDistanceInt() << "   # chunks drawn as voxels, 2 - 16\n"
          << "render_distance = " << (int)std::lround(renderDistance) << "   # distant terrain, chunks, 16 - 160 (160 = whole world)\n\n"
          << "[characters]\n"
          << "edges      = " << charEdges     << "   # 0 off, 1 lines, 2 lines + colour echo\n"
          << "line_color = " << charLineColor << "   # 0 black, 1 tinted\n"
          << "hair_mesh  = " << charHairMesh  << "   # 0 voxels, 1 shards, 2 soft shards\n";
        return (bool)f;
    }
};

// The one settings object (C++17 inline variable)
inline GameSettings gSettings;
