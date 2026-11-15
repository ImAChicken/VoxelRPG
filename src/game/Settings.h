#pragma once
#include <toml++/toml.hpp>
#include <algorithm>
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

    float toolbarScale     = DEF_TOOLBAR_SCALE;     // 0.5 .. 2.0
    float toolbarOpacity   = DEF_TOOLBAR_OPACITY;   // 0.1 .. 1.0
    float mouseSensitivity = DEF_SENSITIVITY;       // degrees per pixel

    std::filesystem::path path = "data/settings.toml";

    void resetToDefaults() {
        toolbarScale     = DEF_TOOLBAR_SCALE;
        toolbarOpacity   = DEF_TOOLBAR_OPACITY;
        mouseSensitivity = DEF_SENSITIVITY;
    }

    void clamp() {
        toolbarScale     = std::clamp(toolbarScale,     0.5f,  2.0f);
        toolbarOpacity   = std::clamp(toolbarOpacity,   0.1f,  1.0f);
        mouseSensitivity = std::clamp(mouseSensitivity, 0.02f, 0.4f);
    }

    bool load() {
        if (!std::filesystem::exists(path)) return false;
        try {
            auto t = toml::parse_file(path.string());
            toolbarScale     = (float)t["hud"]["toolbar_scale"].value_or((double)toolbarScale);
            toolbarOpacity   = (float)t["hud"]["toolbar_opacity"].value_or((double)toolbarOpacity);
            mouseSensitivity = (float)t["controls"]["mouse_sensitivity"].value_or((double)mouseSensitivity);
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
          << "mouse_sensitivity = " << mouseSensitivity << "   # 0.02 - 0.4\n";
        return (bool)f;
    }
};

// The one settings object (C++17 inline variable)
inline GameSettings gSettings;
