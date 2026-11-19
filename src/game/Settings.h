#pragma once
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
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
    static constexpr float DEF_EYE_HEIGHT = 16.0f;  // voxels (5'4"); the body is 17 tall
    float eyeHeight        = DEF_EYE_HEIGHT;        // 13 .. 17.5 voxels

    int  chunkDistanceInt() const { return (int)std::lround(chunkDistance); }
    bool renderAll()        const { return renderDistance >= RENDER_ALL - 0.5f; }

    // Characters
    int charEdges     = 1;   // 0 off, 1 lines, 2 lines + colour echo
    int charLineColor = 0;   // 0 black, 1 tinted by the surface
    int charHairMesh  = 1;   // 0 voxels, 1 shards, 2 soft shards

    // World look: every shader effect can go back to the plain
    // look, for speed and for building (Eddie, 2026-11-18)
    int plantStyle    = 1;   // 0 plain crossed pictures, 1 natural: each plant a little off its
                             // spot, rounded light, swaying in the wind (shaders/chunk.vert)
    int waterStyle    = 1;   // 0 plain, 1 moving: rivers run downstream, still water ripples
    int glassStyle    = 1;   // 0 plain cubes, 1 panes: one see-through sheet, tinting what's behind
    int roofStyle     = 1;   // 0 plain voxels, 1 smooth: one sloped surface with courses and overhanging eaves

    // Saving: ordinary worlds save themselves every few minutes
    // (dev worlds never do: see world/WorldSaveManager.h)
    int autosave = 1;        // 0 off, 1 on
    int controlHints = 1;    // 0 off, 1 on: what the buttons do now, bottom right (out of combat)

    // Sound (audio/AudioEngine.h). Every in-world effect can go back
    // to plain too: soundWalls 0 = only distance and direction
    float masterVolume   = 0.8f;   // 0 .. 1
    float effectsVolume  = 1.0f;   // steps, hits, blocks
    float voiceVolume    = 1.0f;   // talk and barks
    float ambienceVolume = 0.7f;   // the wind
    int   soundWalls     = 1;      // 0 plain, 1 muffled through walls by material

    std::filesystem::path path = "data/settings.toml";

    void resetToDefaults() {
        toolbarScale     = DEF_TOOLBAR_SCALE;
        toolbarOpacity   = DEF_TOOLBAR_OPACITY;
        mouseSensitivity = DEF_SENSITIVITY;
        chunkDistance    = DEF_CHUNK_DISTANCE;
        renderDistance   = DEF_RENDER_DISTANCE;
        eyeHeight        = DEF_EYE_HEIGHT;
        charEdges = 1; charLineColor = 0; charHairMesh = 1;
        plantStyle = 1; waterStyle = 1; roofStyle = 1; glassStyle = 1;
        autosave = 1;
        controlHints = 1;
        masterVolume = 0.8f; effectsVolume = 1.0f; voiceVolume = 1.0f; ambienceVolume = 0.7f; soundWalls = 1;
    }

    void clamp() {
        toolbarScale     = std::clamp(toolbarScale,     0.5f,  2.0f);
        toolbarOpacity   = std::clamp(toolbarOpacity,   0.1f,  1.0f);
        mouseSensitivity = std::clamp(mouseSensitivity, 0.02f, 0.4f);
        chunkDistance    = std::clamp(chunkDistance, 2.0f, 16.0f);
        renderDistance   = std::clamp(renderDistance, RENDER_MIN, RENDER_ALL);
        eyeHeight        = std::clamp(eyeHeight, 13.0f, 17.5f);
        charEdges     = std::clamp(charEdges, 0, 2);
        charLineColor = std::clamp(charLineColor, 0, 1);
        charHairMesh  = std::clamp(charHairMesh, 0, 2);
        plantStyle    = std::clamp(plantStyle, 0, 1);
        waterStyle    = std::clamp(waterStyle, 0, 1);
        roofStyle     = std::clamp(roofStyle, 0, 1);
        glassStyle    = std::clamp(glassStyle, 0, 1);
        autosave      = std::clamp(autosave, 0, 1);
        controlHints  = std::clamp(controlHints, 0, 1);
        for (float* v : { &masterVolume, &effectsVolume, &voiceVolume, &ambienceVolume }) *v = std::clamp(*v, 0.0f, 1.0f);
        soundWalls    = std::clamp(soundWalls, 0, 1);
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
            eyeHeight      = (float)t["view"]["eye_height"].value_or((double)eyeHeight);
            charEdges     = (int)t["characters"]["edges"].value_or((int64_t)charEdges);
            charLineColor = (int)t["characters"]["line_color"].value_or((int64_t)charLineColor);
            charHairMesh  = (int)t["characters"]["hair_mesh"].value_or((int64_t)charHairMesh);
            plantStyle    = (int)t["world"]["plants"].value_or((int64_t)plantStyle);
            waterStyle    = (int)t["world"]["water"].value_or((int64_t)waterStyle);
            roofStyle     = (int)t["world"]["roofs"].value_or((int64_t)roofStyle);
            glassStyle    = (int)t["world"]["glass"].value_or((int64_t)glassStyle);
            autosave      = (int)t["game"]["autosave"].value_or((int64_t)autosave);
            controlHints  = (int)t["game"]["control_hints"].value_or((int64_t)controlHints);
            masterVolume   = (float)t["sound"]["master"].value_or((double)masterVolume);
            effectsVolume  = (float)t["sound"]["effects"].value_or((double)effectsVolume);
            voiceVolume    = (float)t["sound"]["voices"].value_or((double)voiceVolume);
            ambienceVolume = (float)t["sound"]["ambience"].value_or((double)ambienceVolume);
            soundWalls     = (int)t["sound"]["walls"].value_or((int64_t)soundWalls);
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
          << "render_distance = " << (int)std::lround(renderDistance) << "   # distant terrain, chunks, 16 - 160 (160 = whole world)\n"
          << "eye_height      = " << eyeHeight << "   # first-person eye height, voxels (4 in each), 13 - 17.5\n\n"
          << "[characters]\n"
          << "edges      = " << charEdges     << "   # 0 off, 1 lines, 2 lines + colour echo\n"
          << "line_color = " << charLineColor << "   # 0 black, 1 tinted\n"
          << "hair_mesh  = " << charHairMesh  << "   # 0 voxels, 1 shards, 2 soft shards\n\n"
          << "[world]\n"
          << "plants = " << plantStyle << "   # 0 plain pictures, 1 natural: off-grid, rounded light, wind\n"
          << "water  = " << waterStyle << "   # 0 plain, 1 moving: rivers run downstream\n"
          << "glass  = " << glassStyle << "   # 0 plain cubes, 1 panes: see-through sheets that tint what's behind\n"
          << "roofs  = " << roofStyle << "   # 0 plain voxels, 1 smooth: sloped, with courses and overhanging eaves\n\n"
          << "[game]\n"
          << "autosave = " << autosave << "   # 0 off, 1 on: worlds save every few minutes (dev worlds never do)\n"
          << "control_hints = " << controlHints << "   # 0 off, 1 on: what the buttons do now, bottom right\n\n"
          << "[sound]\n"
          << "master   = " << masterVolume << "   # 0 - 1\n"
          << "effects  = " << effectsVolume << "\n"
          << "voices   = " << voiceVolume << "\n"
          << "ambience = " << ambienceVolume << "   # the wind\n"
          << "walls    = " << soundWalls << "   # 0 plain, 1 muffled through walls by what they're made of\n";
        return (bool)f;
    }
};

// The one settings object (C++17 inline variable)
inline GameSettings gSettings;
