#pragma once
#include <string>
#include <unordered_map>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <toml++/toml.hpp>

struct FaceUV {
    int topX,  topY;
    int sideX, sideY;
    int botX,  botY;
};

struct AtlasConfig {
    int tileSize  = 16;
    int atlasCols = 16;
    int atlasRows = 16;
    std::unordered_map<std::string, FaceUV> faceMaps;

    bool load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return false;
        try {
            auto tbl = toml::parse_file(path.string());
            tileSize  = tbl["tile_size"].value_or(16);
            atlasCols = tbl["atlas_cols"].value_or(16);
            atlasRows = tbl["atlas_rows"].value_or(16);

            auto* arr = tbl["face_map"].as_array();
            if (!arr) return false;

            for (auto& entry : *arr) {
                auto* t = entry.as_table();
                if (!t) continue;
                std::string name = (*t)["name"].value_or("");
                if (name.empty()) continue;
                FaceUV uv{};
                if (auto* top = (*t)["top"].as_array()) {
                    if (top->size() >= 2) {
                        uv.topX = (int)(*top)[0].value_or(0);
                        uv.topY = (int)(*top)[1].value_or(0);
                    }
                }
                if (auto* side = (*t)["side"].as_array()) {
                    if (side->size() >= 2) {
                        uv.sideX = (int)(*side)[0].value_or(0);
                        uv.sideY = (int)(*side)[1].value_or(0);
                    }
                }
                if (auto* bot = (*t)["bottom"].as_array()) {
                    if (bot->size() >= 2) {
                        uv.botX = (int)(*bot)[0].value_or(0);
                        uv.botY = (int)(*bot)[1].value_or(0);
                    }
                }
                faceMaps[name] = uv;
            }
            return true;
        } catch (...) { return false; }
    }
};

struct UITheme {
    float bg[4]            = {20,20,28,220};
    float bgHover[4]       = {30,30,42,240};
    float border[4]        = {80,80,100,255};
    float borderHover[4]   = {120,140,180,255};
    float text[4]          = {220,220,220,255};
    float textHover[4]     = {255,255,255,255};
    float textDim[4]       = {140,140,150,255};
    float accent[4]        = {80,160,220,255};
    float buttonBg[4]      = {40,60,90,240};
    float buttonBgHover[4] = {60,90,140,255};
    float buttonBorder[4]  = {80,120,180,255};
    float inputBg[4]       = {15,15,22,255};
    float inputBorder[4]   = {60,80,120,255};
    float inputCursor[4]   = {180,200,255,255};
    float title[4]         = {180,220,255,255};
    float overlay[4]       = {0,0,0,160};

    int buttonW   = 200;
    int buttonH   = 32;
    int buttonPad = 8;
    int panelPad  = 16;
    int fontScale  = 2;
    int titleScale = 3;
    int borderWidth= 1;

    bool load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return false;
        try {
            auto tbl = toml::parse_file(path.string());
            auto& c = *tbl["colors"].as_table();

            auto loadColor = [&](const char* key, float* out) {
                if (auto* arr = c[key].as_array()) {
                    if (arr->size() >= 4) {
                        for (int i = 0; i < 4; i++)
                            out[i] = (float)(*arr)[i].value_or(255);
                    }
                }
            };

            loadColor("background",       bg);
            loadColor("background_hover", bgHover);
            loadColor("border",           border);
            loadColor("border_hover",     borderHover);
            loadColor("text",             text);
            loadColor("text_hover",       textHover);
            loadColor("text_dim",         textDim);
            loadColor("accent",           accent);
            loadColor("button_bg",        buttonBg);
            loadColor("button_bg_hover",  buttonBgHover);
            loadColor("button_border",    buttonBorder);
            loadColor("input_bg",         inputBg);
            loadColor("input_border",     inputBorder);
            loadColor("input_cursor",     inputCursor);
            loadColor("title",            title);
            loadColor("overlay",          overlay);

            if (auto* s = tbl["sizing"].as_table()) {
                buttonW    = (*s)["button_w"].value_or(200);
                buttonH    = (*s)["button_h"].value_or(32);
                buttonPad  = (*s)["button_padding"].value_or(8);
                panelPad   = (*s)["panel_padding"].value_or(16);
                fontScale  = (*s)["font_scale"].value_or(2);
                titleScale = (*s)["title_scale"].value_or(3);
            }
            return true;
        } catch (...) { return false; }
    }

    static void normalize(const float* in, float* out) {
        for (int i = 0; i < 4; i++) out[i] = in[i] / 255.0f;
    }
};

struct FontConfig {
    int charW     = 8;
    int charH     = 8;
    int cols      = 16;
    int rows      = 8;
    int firstChar = 32;

    bool load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return false;
        try {
            auto tbl = toml::parse_file(path.string());
            charW     = tbl["char_width"].value_or(8);
            charH     = tbl["char_height"].value_or(8);
            cols      = tbl["cols"].value_or(16);
            rows      = tbl["rows"].value_or(8);
            firstChar = tbl["first_char"].value_or(32);
            return true;
        } catch (...) { return false; }
    }
};

class ResourcePackManager {
public:
    bool load(const std::filesystem::path& packsRoot) {
        root = packsRoot;

        // Start clean so a reload (F7) can switch back from a
        // custom pack to the default one
        hasActivePack  = false;
        activePackName = "default";
        activeAtlas    = AtlasConfig();
        activeTheme    = UITheme();
        activeFont     = FontConfig();

        if (!loadPack("default", defaultAtlas,
                      defaultTheme, defaultFont)) {
            std::cerr << "Default resource pack missing\n";
            return false;
        }
        std::cout << "Default pack loaded\n";

        std::string activeName = "default";
        auto activeFile = packsRoot / "active_pack.txt";
        if (std::filesystem::exists(activeFile)) {
            std::ifstream f(activeFile);
            std::getline(f, activeName);
            while (!activeName.empty() &&
                   (activeName.back() == '\n' ||
                    activeName.back() == '\r' ||
                    activeName.back() == ' '))
                activeName.pop_back();
        }

        if (activeName != "default") {
            if (loadPack(activeName, activeAtlas,
                         activeTheme, activeFont)) {
                activePackName = activeName;
                hasActivePack  = true;
                std::cout << "Active pack: "
                          << activeName << "\n";
            } else {
                std::cout << "Pack '" << activeName
                          << "' not found, using default\n";
            }
        } else {
            std::cout << "Using default resource pack\n";
        }
        return true;
    }

    FaceUV getFaceUV(const std::string& name) const {
        if (hasActivePack) {
            auto it = activeAtlas.faceMaps.find(name);
            if (it != activeAtlas.faceMaps.end())
                return it->second;
        }
        auto it = defaultAtlas.faceMaps.find(name);
        if (it != defaultAtlas.faceMaps.end())
            return it->second;
        return FaceUV{0,0,0,0,0,0};
    }

    // Any file in the active pack, falling back to the default
    // pack's copy (e.g. "characters/face_eyes.png")
    std::filesystem::path findFile(const std::string& rel) const {
        if (hasActivePack) {
            auto p = root / activePackName / rel;
            if (std::filesystem::exists(p)) return p;
        }
        return root / "default" / rel;
    }

    std::filesystem::path getAtlasPath() const {
        if (hasActivePack) {
            auto p = root / activePackName / "atlas.png";
            if (std::filesystem::exists(p)) return p;
        }
        return root / "default" / "atlas.png";
    }

    std::filesystem::path getFontPath() const {
        if (hasActivePack) {
            auto p = root / activePackName / "ui" / "font.png";
            if (std::filesystem::exists(p)) return p;
        }
        return root / "default" / "ui" / "font.png";
    }

    const UITheme& getTheme() const {
        if (hasActivePack) return activeTheme;
        return defaultTheme;
    }

    const FontConfig& getFontConfig() const {
        if (hasActivePack &&
            std::filesystem::exists(
                root / activePackName / "ui" / "font_config.toml"))
            return activeFont;
        return defaultFont;
    }

    int getTileSize() const {
        return hasActivePack
            ? activeAtlas.tileSize : defaultAtlas.tileSize;
    }

    int getAtlasCols() const {
        return hasActivePack
            ? activeAtlas.atlasCols : defaultAtlas.atlasCols;
    }

    void listPacks() const {
        std::cout << "Available resource packs:\n";
        for (auto& e :
             std::filesystem::directory_iterator(root)) {
            if (e.is_directory())
                std::cout << "  "
                    << e.path().filename().string()
                    << (e.path().filename() == activePackName
                        ? " (active)" : "")
                    << "\n";
        }
    }

    std::string activePackName = "default";

private:
    std::filesystem::path root;
    AtlasConfig defaultAtlas, activeAtlas;
    UITheme     defaultTheme, activeTheme;
    FontConfig  defaultFont,  activeFont;
    bool hasActivePack = false;

    bool loadPack(const std::string& name,
                  AtlasConfig& atlas,
                  UITheme& theme,
                  FontConfig& font) {
        bool ok = atlas.load(
            root / name / "atlas_config.toml");
        theme.load(root / name / "ui" / "theme.toml");
        font.load( root / name / "ui" / "font_config.toml");
        return ok;
    }
};

extern ResourcePackManager gResourcePacks;
