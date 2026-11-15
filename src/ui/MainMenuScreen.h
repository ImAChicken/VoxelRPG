#pragma once
#include "UIScreen.h"
#include "../game/GameMode.h"
#include "../worldgen/IslandChart.h"
#include "../worldgen/WorldType.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

class MainMenuScreen : public UIScreen {
public:
    MainMenuScreen(GLFWwindow* win) : window(win) {
        scanSaves();
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        const auto& theme = gResourcePacks.getTheme();
        int sw = ui.getWidth();
        int sh = ui.getHeight();

        // Handle input
        handleInput();

        ui.begin();

        // Full screen dark overlay
        ui.drawRect(0, 0, (float)sw, (float)sh, theme.overlay);

        // Title
        std::string title = "VoxelRPG";
        int titleW = ui.textWidth(title, theme.titleScale);
        float titleX = (sw - titleW) * 0.5f;
        ui.drawText(title, titleX, 80.0f,
                    theme.title, theme.titleScale);

        // Subtitle
        std::string sub = "A voxel adventure";
        int subW = ui.textWidth(sub, 2);
        ui.drawText(sub, (sw - subW) * 0.5f, 80.0f +
                    theme.titleScale * 8 + 12,
                    theme.textDim, 2);

        float panelW = 500.0f;
        float panelX = (sw - panelW) * 0.5f;

        if (state == State::Main) {
            result = drawMainButtons(ui, panelX, 200.0f,
                                     panelW, theme);
        } else if (state == State::NewWorld) {
            // Wider: settings on the left, island map on the right
            float nw = std::min(1000.0f, (float)sw - 20.0f);
            result = drawNewWorldPanel(ui, (sw - nw) * 0.5f, 140.0f,
                                       nw, theme);
        } else if (state == State::LoadWorld) {
            result = drawLoadWorldPanel(ui, panelX, 180.0f,
                                        panelW, theme);
        }

        ui.end();

        return result;
    }

    // After NewWorld is returned, caller gets these
    std::string getNewWorldName() const { return worldName; }
    uint64_t    getNewWorldSeed() const {
        if (worldSeed.empty()) return 42;
        try { return std::stoull(worldSeed); }
        catch (...) {
            // Use string hash as seed
            uint64_t h = 14695981039346656037ULL;
            for (char c : worldSeed) {
                h ^= (uint8_t)c;
                h *= 1099511628211ULL;
            }
            return h;
        }
    }
    GameMode    getNewWorldMode() const { return worldMode; }
    WorldType   getNewWorldType() const { return worldType; }
    int         getNewWorldSize() const { return worldSizeFor(worldType, islandSize); }
    int         getNewWorldBiome() const { return worldType == WorldType::SingleBiome ? singleBiome : -1; }
    TerrainStyle getNewWorldTerrain() const { return terrain; }

    // Coming back from a world: always start at the main
    // page, not whichever panel was open before
    void onEnter() override {
        state = State::Main;
        mousePrevPressed = mousePressed = true;
    }
    std::string getLoadWorldName() const { return selectedWorld; }

private:
    enum class State { Main, NewWorld, LoadWorld };

    GLFWwindow* window;
    State       state       = State::Main;
    ScreenResult result     = ScreenResult::Continue;

    // New world inputs
    std::string worldName;
    std::string worldSeed;
    GameMode    worldMode   = GameMode::Survival;  // default mode
    WorldType   worldType   = WorldType::Island;
    int         islandSize  = 96;                  // chunks per side
    int         singleBiome = island::Forest;      // single-biome worlds
    TerrainStyle terrain    = TerrainStyle::GuidedRivers;
    int         previewBiome = -1;

    // The island map shown on the right, rebuilt whenever
    // the seed or size changes
    island::Chart preview;
    uint64_t      previewSeed = ~0ull;
    int           previewSize = 0;
    float         previewMs   = 0.0f;
    bool        nameActive  = true;   // which input is focused
    float       cursorTimer = 0.0f;
    bool        showCursor  = true;

    // Load world list
    std::vector<std::string> saves;
    int         hoveredSave = -1;
    std::string selectedWorld;

    // Button hover states
    int hoveredBtn = -1;

    // Mouse state
    double mouseX = 0, mouseY = 0;
    bool   mousePressed = false;
    bool   mousePrevPressed = false;

    void scanSaves() {
        saves.clear();
        if (!std::filesystem::exists("saves")) return;
        for (auto& e :
             std::filesystem::directory_iterator("saves")) {
            if (e.is_directory())
                saves.push_back(
                    e.path().filename().string());
        }
        std::sort(saves.begin(), saves.end());
    }

    void handleInput() {
        mousePrevPressed = mousePressed;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        mousePressed =
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)
            == GLFW_PRESS;
    }

    bool justClicked() const {
        return mousePressed && !mousePrevPressed;
    }

    bool isHovered(float x, float y, float w, float h) const {
        return mouseX >= x && mouseX <= x+w &&
               mouseY >= y && mouseY <= y+h;
    }

    ScreenResult drawMainButtons(UIRenderer& ui,
                                  float px, float py,
                                  float pw,
                                  const UITheme& theme) {
        float bw = (float)theme.buttonW;
        float bh = (float)theme.buttonH;
        float bx = px + (pw - bw) * 0.5f;
        float gap = 12.0f;

        struct Btn { std::string label; float y; };
        Btn buttons[] = {
            { "New World",  py },
            { "Load World", py + (bh + gap) },
            { "Quit",       py + (bh + gap) * 2 },
        };

        for (int i = 0; i < 3; i++) {
            bool hov = isHovered(bx, buttons[i].y, bw, bh);
            drawButton(ui, buttons[i].label,
                       bx, buttons[i].y, hov);
            if (hov && justClicked()) {
                if (i == 0) {
                    state = State::NewWorld;
                    worldName.clear();
                    worldSeed.clear();
                    worldMode  = GameMode::Survival;
                    worldType  = WorldType::Island;
                    islandSize = 96;
                    nameActive = true;
                }
                else if (i == 1) {
                    state = State::LoadWorld;
                    scanSaves();
                }
                else if (i == 2) return ScreenResult::Quit;
            }
        }
        return ScreenResult::Continue;
    }

    // A row of choices; returns the index clicked, or -1
    int choiceRow(UIRenderer& ui, const UITheme& theme, float x, float y, float w,
                  const std::vector<std::string>& labels, int selected, bool enabled = true) {
        float gap = 6.0f;
        float cw  = (w - gap * (labels.size() - 1)) / labels.size();
        float ch  = (float)theme.buttonH;
        int clicked = -1;
        for (size_t i = 0; i < labels.size(); i++) {
            float cx  = x + (cw + gap) * i;
            bool  hov = enabled && isHovered(cx, y, cw, ch);
            bool  on  = enabled && (int)i == selected;
            ui.drawRect(cx, y, cw, ch, (on || hov) ? theme.buttonBgHover : theme.buttonBg);
            ui.drawBorder(cx, y, cw, ch, on ? theme.accent : theme.buttonBorder,
                          on ? 2.0f : 1.0f);
            ui.drawTextCentered(labels[i], cx, y, cw, ch,
                                !enabled ? theme.textDim : on ? theme.textHover : theme.text,
                                ui.textWidth(labels[i], 2) <= cw + 4 ? 2 : 1);
            if (hov && justClicked()) clicked = (int)i;
        }
        return clicked;
    }

    ScreenResult drawNewWorldPanel(UIRenderer& ui,
                                    float px, float py,
                                    float pw,
                                    const UITheme& theme) {
        float pad = (float)theme.panelPad;
        float ph  = 480.0f;
        drawPanel(ui, px, py, pw, ph);

        float lx = px + pad;
        float ly = py + pad;
        float iw = 440.0f;                      // left column width

        ui.drawText("New World", lx, ly, theme.title, 2);
        ly += 26.0f;

        // ── Name ──────────────────────────────────────────
        ui.drawText("World Name:", lx, ly, theme.text, 2);
        ly += 20.0f;
        bool nameHov = isHovered(lx, ly, iw, (float)theme.buttonH);
        if (nameHov && justClicked()) nameActive = true;
        drawInput(ui, worldName, "My World", lx, ly, iw, nameActive, showCursor);
        ly += theme.buttonH + 10.0f;

        // ── Seed, with a Random button ────────────────────
        ui.drawText("Seed:", lx, ly, theme.text, 2);
        ly += 20.0f;
        float rw = 120.0f;
        bool seedHov = isHovered(lx, ly, iw - rw - 8, (float)theme.buttonH);
        if (seedHov && justClicked()) nameActive = false;
        drawInput(ui, worldSeed, "42", lx, ly, iw - rw - 8, !nameActive, showCursor);
        float rx = lx + iw - rw;
        bool randHov = isHovered(rx, ly, rw, (float)theme.buttonH);
        ui.drawRect(rx, ly, rw, (float)theme.buttonH, randHov ? theme.buttonBgHover : theme.buttonBg);
        ui.drawBorder(rx, ly, rw, (float)theme.buttonH, theme.buttonBorder, 1.0f);
        ui.drawTextCentered("Random", rx, ly, rw, (float)theme.buttonH,
                            randHov ? theme.textHover : theme.text, 2);
        if (randHov && justClicked()) {
            std::random_device rd;
            worldSeed = std::to_string(rd() % 1000000u);
        }
        ly += theme.buttonH + 10.0f;

        // ── World type ────────────────────────────────────
        ui.drawText("World Type:", lx, ly, theme.text, 2);
        ly += 20.0f;
        {
            std::vector<std::string> types;
            for (int t = 0; t < (int)WorldType::COUNT; t++)
                types.push_back(worldTypeName((WorldType)t));
            int c = choiceRow(ui, theme, lx, ly, iw, types, (int)worldType);
            if (c >= 0) worldType = (WorldType)c;
        }
        ly += theme.buttonH + 10.0f;

        // ── Island size, or the biome of a single-biome world
        if (worldType == WorldType::SingleBiome) {
            ui.drawText("Biome:", lx, ly, theme.text, 2);
            ly += 20.0f;
            // < name >: click the left third for the previous
            // biome, anywhere else for the next
            std::vector<int> all = singleBiomeChoices();
            int at = 0;
            for (int k = 0; k < (int)all.size(); k++) if (all[k] == singleBiome) at = k;
            float bh = (float)theme.buttonH;
            bool hov = mouseX >= lx && mouseX <= lx + iw && mouseY >= ly && mouseY <= ly + bh;
            ui.drawRect(lx, ly, iw, bh, hov ? theme.buttonBgHover : theme.inputBg);
            ui.drawBorder(lx, ly, iw, bh, hov ? theme.accent : theme.border, 1.0f);
            ui.drawText("<", lx + 8, ly + (bh - ui.textHeight(2)) * 0.5f, theme.textDim, 2);
            ui.drawText(">", lx + iw - 8 - ui.textWidth(">", 2), ly + (bh - ui.textHeight(2)) * 0.5f, theme.textDim, 2);
            ui.drawTextCentered(island::biomeInfo(singleBiome).name, lx, ly, iw, bh, hov ? theme.textHover : theme.accent, 2);
            if (hov && justClicked()) {
                int nAll = (int)all.size();
                at = (mouseX < lx + iw / 3.0f) ? (at + nAll - 1) % nAll : (at + 1) % nAll;
                singleBiome = all[at];
            }
        } else {
            bool isIsland = worldType == WorldType::Island;
            ui.drawText("Island Size:", lx, ly, isIsland ? theme.text : theme.textDim, 2);
            ly += 20.0f;
            int sel = islandSize <= 72 ? 0 : islandSize <= 96 ? 1 : 2;
            int c = choiceRow(ui, theme, lx, ly, iw, { "Small", "Medium", "Large" }, sel, isIsland);
            if (c >= 0) islandSize = ISLAND_SIZES[c];
        }
        ly += theme.buttonH + 10.0f;

        // ── Default mode ──────────────────────────────────
        ui.drawText("Default Mode:", lx, ly, theme.text, 2);
        ly += 20.0f;
        {
            const GameMode choices[3] = { GameMode::Survival, GameMode::Dev, GameMode::Spectator };
            int sel = worldMode == GameMode::Survival ? 0 : worldMode == GameMode::Dev ? 1 : 2;
            int c = choiceRow(ui, theme, lx, ly, iw,
                              { rulesFor(choices[0], false).name, rulesFor(choices[1], false).name,
                                rulesFor(choices[2], false).name }, sel);
            if (c >= 0) worldMode = choices[c];
        }
        ly += theme.buttonH + 10.0f;

        // ── Terrain: erosion, and how rivers are chosen ───
        {
            bool shaped = worldType != WorldType::Classic;
            ui.drawText("Terrain:", lx, ly, shaped ? theme.text : theme.textDim, 2);
            ly += 20.0f;
            std::vector<std::string> opts;
            for (int t = 0; t < (int)TerrainStyle::COUNT; t++) opts.push_back(terrainStyleName((TerrainStyle)t));
            int c = choiceRow(ui, theme, lx, ly, iw, opts, (int)terrain, shaped);
            if (c >= 0) terrain = (TerrainStyle)c;
        }
        ly += theme.buttonH + 18.0f;

        // ── Buttons ───────────────────────────────────────
        float bw = (float)theme.buttonW;
        float bh = (float)theme.buttonH;
        ly = py + ph - pad - bh;                // buttons sit on the panel's bottom edge
        float createX = lx;
        float backX   = lx + iw - bw;
        bool createHov = isHovered(createX, ly, bw, bh);
        bool backHov   = isHovered(backX,   ly, bw, bh);
        drawButton(ui, "Create World", createX, ly, createHov);
        drawButton(ui, "Back",         backX,   ly, backHov);

        // ── Preview, right column ─────────────────────────
        float mapX = lx + iw + 30.0f;
        float mapW = px + pw - pad - mapX;
        drawPreview(ui, theme, mapX, py + pad, mapW, ph - pad * 2);

        if (createHov && justClicked()) {
            if (worldName.empty()) worldName = "New World";
            return ScreenResult::NewWorld;
        }
        if (backHov && justClicked()) state = State::Main;

        handleTextInput();
        return ScreenResult::Continue;
    }

    // The right-hand column: the island map, or a note for
    // world types without a map
    void drawPreview(UIRenderer& ui, const UITheme& theme, float x, float y, float w, float h) {
        if (worldType == WorldType::Classic) {
            ui.drawText("Classic", x, y, theme.title, 2);
            ui.drawText("Rolling hills and forest.", x, y + 30, theme.text, 2);
            ui.drawText("32 x 32 chunks, 0.26 mi", x, y + 52, theme.textDim, 2);
            return;
        }

        // Rebuild the chart when the seed, size or biome changes
        uint64_t seed = getNewWorldSeed() % 1000000ull;
        int wantSize  = worldType == WorldType::SingleBiome ? CLASSIC_WORLD_SIZE : islandSize;
        int wantBiome = worldType == WorldType::SingleBiome ? singleBiome : -1;
        if (seed != previewSeed || wantSize != previewSize || wantBiome != previewBiome) {
            auto t0 = std::chrono::steady_clock::now();
            preview = wantBiome >= 0 ? island::singleBiomeChart((int)seed, wantBiome, wantSize)
                                     : island::collapseIsland((int)seed, wantSize);
            previewMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            previewSeed = seed;
            previewSize = wantSize;
            previewBiome = wantBiome;
        }

        const int S = preview.size;
        float side = std::min(w, h - 50.0f);
        float cell = side / S;
        float map  = side;
        float mx = x + (w - map) * 0.5f, my = y;

        // One rectangle per cell, coloured by biome
        for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            uint32_t c = island::biomeInfo(preview.at(i, j)).color;
            // Hill shading: lit from the north-west, so the
            // mountain and the valleys stand out
            auto el = [&](int x, int y) {
                x = std::clamp(x, 0, S - 1); y = std::clamp(y, 0, S - 1);
                return preview.elev[(size_t)y * S + x];
            };
            float shade = std::clamp(1.0f + (el(i - 1, j - 1) - el(i + 1, j + 1)) * 3.0f, 0.6f, 1.35f);
            float col[4] = { std::min(255.0f, ((c >> 16) & 255) * shade),
                             std::min(255.0f, ((c >> 8) & 255) * shade),
                             std::min(255.0f, (c & 255) * shade), 255.0f };
            float x0 = std::floor(mx + i * cell), y0 = std::floor(my + j * cell);
            float x1 = std::floor(mx + (i + 1) * cell), y1 = std::floor(my + (j + 1) * cell);
            ui.drawRect(x0, y0, x1 - x0, y1 - y0, col);
        }
        ui.drawBorder(mx, my, map, map, theme.border, 1.0f);

        // Markers
        for (const auto& m : preview.markers) {
            float c[4] = {255, 255, 255, 255};
            if (m.kind == "village") { c[0] = 255; c[1] = 160; c[2] = 60; }
            else if (m.kind == "cave") { c[0] = 20; c[1] = 20; c[2] = 20; }
            else if (m.kind == "spring") { c[0] = 80; c[1] = 230; c[2] = 220; }
            else if (m.kind == "lava") { c[0] = 255; c[1] = 90; c[2] = 20; }
            else if (m.kind == "fork") { c[0] = 250; c[1] = 220; c[2] = 120; }
            float r = std::max(3.0f, cell * 0.8f);
            float cx = mx + (m.x + 0.5f) * cell, cy = my + (m.y + 0.5f) * cell;
            float edge[4] = {0, 0, 0, 255};
            ui.drawRect(cx - r - 1, cy - r - 1, r * 2 + 2, r * 2 + 2, edge);
            ui.drawRect(cx - r, cy - r, r * 2, r * 2, c);
            if (m.kind == "spawn" || m.kind == "peak")
                ui.drawText(m.label, cx + r + 3, cy - 6, theme.textHover, 1);
        }

        // Under the map: size, and what the mouse is over
        float ty = my + map + 8;
        char buf[160];
        double miles = S * 128 * 4.0 / 12.0 / 5280.0;
        std::snprintf(buf, sizeof(buf), "%d x %d chunks, %.1f mi", S, S, miles);
        fitText(ui, buf, mx, ty, map, theme.textDim);
        ty += 20;
        if (mouseX >= mx && mouseY >= my && mouseX < mx + map && mouseY < my + map) {
            int ci = (int)((mouseX - mx) / cell), cj = (int)((mouseY - my) / cell);
            int b = preview.at(ci, cj);
            int lb = preview.baseAt(ci, cj);
            float hv = preview.height.empty() ? 0.0f : preview.height[(size_t)cj * S + ci];
            if (lb != b)
                std::snprintf(buf, sizeof(buf), "%s + %s, %d ft", island::biomeInfo(lb).name,
                              island::biomeInfo(b).name, (int)std::lround((hv - 160.0f) / 3.0f));
            else
                std::snprintf(buf, sizeof(buf), "%s, %d ft", island::biomeInfo(b).name,
                              (int)std::lround((hv - 160.0f) / 3.0f));
            fitText(ui, buf, mx, ty, map, theme.textHover);
        } else {
            fitText(ui, "Point at the map for details", mx, ty, map, theme.textDim);
        }
    }

    // Text at scale 2, dropping to scale 1 if it would run past maxW
    void fitText(UIRenderer& ui, const std::string& t, float x, float y, float maxW, const float* col) {
        int scale = ui.textWidth(t, 2) <= maxW ? 2 : 1;
        ui.drawText(t, x, y, col, scale);
    }

    ScreenResult drawLoadWorldPanel(UIRenderer& ui,
                                     float px, float py,
                                     float pw,
                                     const UITheme& theme) {
        float pad  = (float)theme.panelPad;
        float rowH = 36.0f;
        float ph   = pad * 2 + rowH * (saves.size() + 1) + 50.0f;
        ph = std::max(ph, 200.0f);
        drawPanel(ui, px, py, pw, ph);

        float lx = px + pad;
        float ly = py + pad;

        ui.drawText("Load World", lx, ly, theme.title, 2);
        ly += 28.0f;

        if (saves.empty()) {
            ui.drawText("No saved worlds found.",
                lx, ly, theme.textDim, 2);
            ly += 24.0f;
        } else {
            for (int i = 0; i < (int)saves.size(); i++) {
                bool hov = isHovered(lx, ly, pw - pad*2, rowH);
                const float* bg   = hov
                    ? theme.buttonBgHover : theme.buttonBg;
                const float* text = hov
                    ? theme.textHover : theme.text;
                ui.drawRect(lx, ly, pw - pad*2, rowH, bg);
                ui.drawBorder(lx, ly, pw-pad*2, rowH,
                              theme.buttonBorder, 1.0f);
                ui.drawText(saves[i],
                    lx + pad, ly + (rowH - 16) * 0.5f,
                    text, 2);
                if (hov && justClicked()) {
                    selectedWorld = saves[i];
                    return ScreenResult::LoadWorld;
                }
                ly += rowH + 4.0f;
            }
        }

        // Back button
        float bw = (float)theme.buttonW;
        float bh = (float)theme.buttonH;
        float bx = px + (pw - bw) * 0.5f;
        bool backHov = isHovered(bx, ly + 8, bw, bh);
        drawButton(ui, "Back", bx, ly + 8, backHov);
        if (backHov && justClicked()) {
            state = State::Main;
        }

        return ScreenResult::Continue;
    }

    void handleTextInput() {
        // Cursor blink
        cursorTimer += 0.016f;
        if (cursorTimer > 0.5f) {
            cursorTimer = 0.0f;
            showCursor = !showCursor;
        }
    }

public:
    // Called by GLFW char callback
    void onChar(unsigned int codepoint) {
        if (codepoint < 32 || codepoint > 126) return;
        std::string& target = nameActive ? worldName : worldSeed;
        if (target.size() < 32)
            target += (char)codepoint;
    }

    void onKey(int key) {
        std::string& target = nameActive ? worldName : worldSeed;
        if (key == GLFW_KEY_BACKSPACE && !target.empty())
            target.pop_back();
        if (key == GLFW_KEY_TAB)
            nameActive = !nameActive;
    }
};