#pragma once
#include "UIScreen.h"
#include "../game/GameMode.h"
#include "../worldgen/IslandChart.h"
#include "../worldgen/WorldType.h"
#include "ItemIcon.h"
#include "../world/WorldSaveManager.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
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
            result = drawNewWorldPanel(ui, (sw - nw) * 0.5f, std::min(146.0f, std::max(10.0f, (float)sh - 570.0f)),
                                       nw, theme);
        } else if (state == State::LoadWorld) {
            result = drawLoadWorldPanel(ui, panelX, 180.0f,
                                        panelW, theme);
        } else if (state == State::Join) {
            result = drawJoinPanel(ui, panelX, 190.0f, panelW, theme);
        }

        ui.end();

        return result;
    }

    // Join Server: where, and as whom (remembered in saves/last_server.txt)
    std::string getJoinAddress() const { return joinAddress.empty() ? std::string("127.0.0.1") : joinAddress; }
    std::string getJoinName() const { return joinName; }
    // Back on the Join page with a line saying what went wrong
    void showJoinError(const std::string& why) { state = State::Join; joinMessage = why; mousePrevPressed = mousePressed = true; }

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
    const WorldGenParams& getNewWorldParams() const { return genParams; }
    const FlatOptions& getNewWorldFlat() const { return flat; }

    // Coming back from a world: always start at the main
    // page, not whichever panel was open before
    void onEnter() override {
        state = State::Main;
        mousePrevPressed = mousePressed = true;
    }
    std::string getLoadWorldName() const { return selectedWorld; }
    bool        getLoadWorldDev() const { return selectedDev; }
    // New World: in devworlds/? a copy of which dev world ("" = new terrain)?
    bool        getNewWorldDev() const { return newDev; }
    std::string getNewWorldCopyOf() const { return copyFrom && !devSaves.empty() ? copyOf : std::string(); }
    // New dev world on new terrain: lay out the showcase chests (game/DevKit.h)?
    bool        getNewWorldKit() const { return newDev && !copyFrom && kit; }
    bool        getNewWorldVillage() const { return !copyFrom && worldType == WorldType::Flat && flat.village; }

private:
    enum class State { Main, NewWorld, LoadWorld, Join };

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
    WorldGenParams genParams;                       // the World Settings sliders
    FlatOptions flat;                               // flat worlds
    bool        genOpen  = false;                   // World Settings showing
    int         genPage  = 0;                       // 0 Terrain, 1 Features, 2 Biomes
    int         genDrag  = -1;                      // slider being dragged
    uint64_t    previewParams = 0;
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
    bool        selectedDev = false;
    std::vector<std::string> devSaves;   // devworlds/
    bool        newDev = false;          // New World: save in devworlds/
    bool        copyFrom = false;        // New World: start as a copy of a dev world
    std::string copyOf;
    bool        kit = true;                  // new dev worlds: start with the showcase chests
    std::string copyInfoFor;                 // the preview's cached facts about copyOf
    std::vector<std::string> copyInfo;

    // Join Server
    std::string joinAddress, joinName, joinMessage;
    int  joinField = 0;                  // 0 address, 1 name
    bool joinEnter = false;

    ScreenResult drawJoinPanel(UIRenderer& ui, float px, float py, float pw, const UITheme& theme) {
        const float pad = (float)theme.panelPad, bh = (float)theme.buttonH;
        float ph = 250.0f;
        drawPanel(ui, px, py, pw, ph);
        ui.drawText("Join Server", px + pad, py + pad, theme.title, 2);
        float labW = 110.0f, cx = px + pad + labW, cw = pw - 2 * pad - labW, y = py + 54.0f;
        cursorTimer += 0.016f;
        bool cur = std::fmod(cursorTimer, 1.0f) < 0.5f;
        ui.drawText("Address", px + pad, y + (bh - 16) * 0.5f, theme.text, 2);
        if (isHovered(cx, y, cw, bh) && justClicked()) joinField = 0;
        drawInput(ui, joinAddress, "127.0.0.1", cx, y, cw, joinField == 0, cur);
        y += bh + 10.0f;
        ui.drawText("Name", px + pad, y + (bh - 16) * 0.5f, theme.text, 2);
        if (isHovered(cx, y, cw, bh) && justClicked()) joinField = 1;
        drawInput(ui, joinName, "your name", cx, y, cw, joinField == 1, cur);
        y += bh + 8.0f;
        ui.drawText(joinMessage.empty() ? "address or address:port   your name is your character on that server" : joinMessage,
                    px + pad, y, joinMessage.empty() ? theme.textDim : theme.accent, 1);
        y += 26.0f;
        float bw = (pw - 2 * pad - 12.0f) * 0.5f;
        bool hj = isHovered(px + pad, y, bw, bh), hb = isHovered(px + pad + bw + 12.0f, y, bw, bh);
        drawButton(ui, "Join", px + pad, y, hj, 2, bw);
        drawButton(ui, "Back", px + pad + bw + 12.0f, y, hb, 2, bw);
        bool go = (hj && justClicked()) || joinEnter;
        joinEnter = false;
        if (hb && justClicked()) state = State::Main;
        if (go) {
            if (joinName.empty()) { joinMessage = "type a name first"; joinField = 1; return ScreenResult::Continue; }
            std::filesystem::create_directories("saves");
            std::ofstream f("saves/last_server.txt");
            f << joinAddress << "\n" << joinName << "\n";
            return ScreenResult::JoinServer;
        }
        return ScreenResult::Continue;
    }

    // Button hover states
    int hoveredBtn = -1;

    // Mouse state
    double mouseX = 0, mouseY = 0;
    bool   mousePressed = false;
    bool   mousePrevPressed = false;

    void scanSaves() {
        saves = WorldSaveManager::list(false);
        devSaves = WorldSaveManager::list(true);
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
            { "Quit",       py + (bh + gap) * 3 },
            { "Join Server", py + (bh + gap) * 2 },
        };

        for (int i = 0; i < 4; i++) {
            bool hov = isHovered(bx, buttons[i].y, bw, bh);
            drawButton(ui, buttons[i].label,
                       bx, buttons[i].y, hov);
            if (hov && justClicked()) {
                if (i == 0) {
                    state = State::NewWorld;
                    scanSaves();
                    newDev = false; copyFrom = false;
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
                else if (i == 3) {
                    state = State::Join;
                    joinMessage.clear();
                    joinField = joinName.empty() ? 1 : 0;
                    if (joinAddress.empty() && joinName.empty()) {
                        std::ifstream f("saves/last_server.txt");
                        std::getline(f, joinAddress);
                        std::getline(f, joinName);
                    }
                }
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
        // one text size for the whole row: large if every label fits
        int scale = 2;
        for (const auto& l : labels) if (ui.textWidth(l, 2) > cw - 6) scale = 1;
        for (size_t i = 0; i < labels.size(); i++) {
            float cx  = x + (cw + gap) * i;
            bool  hov = enabled && isHovered(cx, y, cw, ch);
            bool  on  = enabled && (int)i == selected;
            ui.drawRect(cx, y, cw, ch, (on || hov) ? theme.buttonBgHover : theme.buttonBg);
            ui.drawBorder(cx, y, cw, ch, on ? theme.accent : theme.buttonBorder,
                          on ? 2.0f : 1.0f);
            ui.drawTextCentered(labels[i], cx, y, cw, ch,
                                !enabled ? theme.textDim : on ? theme.textHover : theme.text, scale);
            if (hov && justClicked()) clicked = (int)i;
        }
        return clicked;
    }

    // A label at the left of a row (dimmed when the row is off)
    void rowLabel(UIRenderer& ui, const UITheme& theme, const char* text, float x, float y, float h, bool enabled) {
        ui.drawText(text, x, y + (h - ui.textHeight(2)) * 0.5f, enabled ? theme.text : theme.textDim, 2);
    }
    bool smallButton(UIRenderer& ui, const UITheme& theme, const char* text, float x, float y, float w, float h) {
        bool hov = isHovered(x, y, w, h);
        ui.drawRect(x, y, w, h, hov ? theme.buttonBgHover : theme.buttonBg);
        ui.drawBorder(x, y, w, h, theme.buttonBorder, 1.0f);
        ui.drawTextCentered(text, x, y, w, h, hov ? theme.textHover : theme.text, ui.textWidth(text, 2) <= w - 8 ? 2 : 1);
        return hov && justClicked();
    }
    // < value >: click the left third for the previous one,
    // anywhere else for the next. Returns the (new) value.
    template <class NameFn>
    int cyclePicker(UIRenderer& ui, const UITheme& theme, float x, float y, float w, float h,
                    const std::vector<int>& values, int current, NameFn name) {
        if (values.empty()) return current;
        int at = 0;
        for (int k = 0; k < (int)values.size(); k++) if (values[(size_t)k] == current) at = k;
        bool hov = isHovered(x, y, w, h);
        ui.drawRect(x, y, w, h, hov ? theme.buttonBgHover : theme.inputBg);
        ui.drawBorder(x, y, w, h, hov ? theme.accent : theme.border, 1.0f);
        ui.drawText("<", x + 8, y + (h - ui.textHeight(2)) * 0.5f, theme.textDim, 2);
        ui.drawText(">", x + w - 8 - ui.textWidth(">", 2), y + (h - ui.textHeight(2)) * 0.5f, theme.textDim, 2);
        std::string n = name(values[(size_t)at]);
        ui.drawTextCentered(n, x, y, w, h, hov ? theme.textHover : theme.accent, ui.textWidth(n, 2) <= w - 50 ? 2 : 1);
        if (hov && justClicked()) {
            int nv = (int)values.size();
            at = (mouseX < x + w / 3.0f) ? (at + nv - 1) % nv : (at + 1) % nv;
        }
        return values[(size_t)at];
    }

    ScreenResult drawNewWorldPanel(UIRenderer& ui,
                                    float px, float py,
                                    float pw,
                                    const UITheme& theme) {
        float pad = (float)theme.panelPad;
        float ph  = 560.0f;
        drawPanel(ui, px, py, pw, ph);

        float lx = px + pad;
        float ly = py + pad;
        float iw = 440.0f;                      // left column width

        if (genOpen) {
            drawWorldSettings(ui, theme, lx, ly, iw, py + ph - pad);
            float mapX = lx + iw + 30.0f;
            drawPreview(ui, theme, mapX, py + pad, px + pw - pad - mapX, ph - pad * 2);
            return ScreenResult::Continue;
        }

        ui.drawText("New World", lx, ly, theme.title, 2);
        ly += 30.0f;
        const float bh = (float)theme.buttonH, rowGap = 8.0f;
        const float labW = 128.0f, cx = lx + labW, cw = iw - labW;

        // ── Name and seed ─────────────────────────────────
        rowLabel(ui, theme, "Name", lx, ly, bh, true);
        if (isHovered(cx, ly, cw, bh) && justClicked()) nameActive = true;
        drawInput(ui, worldName, "My World", cx, ly, cw, nameActive, showCursor);
        ly += bh + rowGap;

        // ── Where it's kept, and what it starts as ────────
        rowLabel(ui, theme, "Save in", lx, ly, bh, true);
        {
            int c = choiceRow(ui, theme, cx, ly, cw, { "Worlds", "Dev worlds" }, newDev ? 1 : 0);
            if (c >= 0) newDev = c == 1;
        }
        ly += bh + rowGap;
        rowLabel(ui, theme, "Start", lx, ly, bh, true);
        {
            int c = choiceRow(ui, theme, cx, ly, cw, { "New terrain", "Copy a dev world" }, copyFrom ? 1 : 0);
            if (c >= 0) copyFrom = c == 1;
        }
        ly += bh + rowGap;
        if (newDev && !copyFrom) {
            rowLabel(ui, theme, "Kit", lx, ly, bh, true);
            int c = choiceRow(ui, theme, cx, ly, cw, { "None", "Showcase chests" }, kit ? 1 : 0);
            if (c >= 0) kit = c == 1;
            ly += bh + rowGap;
        }
        if (copyFrom) {
            rowLabel(ui, theme, "Copy of", lx, ly, bh, !devSaves.empty());
            if (devSaves.empty()) {
                fitText(ui, "no dev worlds yet", cx + 8, ly + (bh - 16) * 0.5f, cw, theme.textDim);
            } else {
                std::vector<int> idx;
                int cur = 0;
                for (int k = 0; k < (int)devSaves.size(); k++) { idx.push_back(k); if (devSaves[(size_t)k] == copyOf) cur = k; }
                int k = cyclePicker(ui, theme, cx, ly, cw, bh, idx, cur, [this](int i) { return devSaves[(size_t)i]; });
                copyOf = devSaves[(size_t)k];
            }
            ly += bh + 10.0f;
            wrapText(ui, std::string("Everything in it comes along: the ground and what is built on it, spawners, NPCs, ") +
                     "your inventory and where you stand. The copy is a new world under the name above" +
                     (newDev ? ", kept with the dev worlds." : "; it saves as you play."), cx, ly, cw, theme.textDim);
        }

        if (!copyFrom) {
        rowLabel(ui, theme, "Seed", lx, ly, bh, true);
        {
            float rw = 110.0f;
            if (isHovered(cx, ly, cw - rw - 6, bh) && justClicked()) nameActive = false;
            drawInput(ui, worldSeed, "42", cx, ly, cw - rw - 6, !nameActive, showCursor);
            if (smallButton(ui, theme, "Random", cx + cw - rw, ly, rw, bh)) {
                std::random_device rd;
                worldSeed = std::to_string(rd() % 1000000u);
            }
        }
        ly += bh + rowGap;

        // ── Mode players start in ─────────────────────────
        rowLabel(ui, theme, "Mode", lx, ly, bh, true);
        {
            const GameMode choices[3] = { GameMode::Survival, GameMode::Dev, GameMode::Spectator };
            int sel = worldMode == GameMode::Survival ? 0 : worldMode == GameMode::Dev ? 1 : 2;
            int c = choiceRow(ui, theme, cx, ly, cw,
                              { rulesFor(choices[0], false).name, rulesFor(choices[1], false).name,
                                rulesFor(choices[2], false).name }, sel);
            if (c >= 0) worldMode = choices[c];
        }
        ly += bh + rowGap;

        // ── World type, and what it is ────────────────────
        rowLabel(ui, theme, "Type", lx, ly, bh, true);
        {
            static const WorldType ORDER[] = { WorldType::Island, WorldType::SingleBiome, WorldType::Classic, WorldType::Flat };
            std::vector<std::string> names;
            int sel = 0;
            for (int k = 0; k < 4; k++) { names.push_back(worldTypeName(ORDER[k])); if (ORDER[k] == worldType) sel = k; }
            int c = choiceRow(ui, theme, cx, ly, cw, names, sel);
            if (c >= 0) worldType = ORDER[c];
        }
        ly += bh + 6.0f;
        ly += wrapText(ui, worldTypeBlurb(worldType), cx, ly, cw, theme.textDim) + 4.0f;

        // ── Options of this type only ─────────────────────
        {
            float lineY = ly + 6.0f;
            float lineCol[4] = { theme.border[0], theme.border[1], theme.border[2], theme.border[3] };
            ui.drawRect(lx, lineY, iw, 1.0f, lineCol);
            ly += 16.0f;
        }
        if (worldType == WorldType::Island) {
            rowLabel(ui, theme, "Size", lx, ly, bh, true);
            int sel = islandSize <= 72 ? 0 : islandSize <= 96 ? 1 : 2;
            int c = choiceRow(ui, theme, cx, ly, cw, { "Small", "Medium", "Large" }, sel);
            if (c >= 0) islandSize = ISLAND_SIZES[c];
            ly += bh + rowGap;
        }
        if (worldType == WorldType::SingleBiome) {
            rowLabel(ui, theme, "Biome", lx, ly, bh, true);
            std::vector<int> all = singleBiomeChoices();
            singleBiome = cyclePicker(ui, theme, cx, ly, cw, bh, all, singleBiome,
                                      [](int b) { return std::string(island::biomeInfo(b).name); });
            ly += bh + rowGap;
        }
        if (worldTypeShaped(worldType)) {
            rowLabel(ui, theme, "Rivers", lx, ly, bh, true);
            std::vector<std::string> opts;
            for (int t = 0; t < (int)TerrainStyle::COUNT; t++) opts.push_back(terrainStyleName((TerrainStyle)t));
            int c = choiceRow(ui, theme, cx, ly, cw, opts, (int)terrain);
            if (c >= 0) terrain = (TerrainStyle)c;
            ly += bh + rowGap;
            std::string label = genParams.isDefault() ? "World settings..." : "World settings...  (changed)";
            if (smallButton(ui, theme, label.c_str(), cx, ly, cw, bh)) { genOpen = true; genDrag = -1; }
            ly += bh + rowGap;
        }
        if (worldType == WorldType::Classic) {
            fitText(ui, "No options: 32 x 32 chunks of hills and forest.", lx, ly + 6, iw, theme.textDim);
        }
        if (worldType == WorldType::Flat) {
            rowLabel(ui, theme, "Ground", lx, ly, bh, true);
            {
                const auto& G = FlatOptions::grounds();
                std::vector<int> idx;
                int cur = 0;
                for (int k = 0; k < (int)G.size(); k++) { idx.push_back(k); if (flat.ground == G[(size_t)k].first) cur = k; }
                int k = cyclePicker(ui, theme, cx, ly, cw, bh, idx, cur,
                                    [&G](int i) { return std::string(G[(size_t)i].second); });
                flat.ground = G[(size_t)k].first;
            }
            ly += bh + rowGap;
            // what grows: three toggles in a row
            rowLabel(ui, theme, "Grows", lx, ly, bh, true);
            {
                bool* v[3] = { &flat.trees, &flat.plants, &flat.decor };
                const char* names[3] = { "Trees", "Plants", "Decor" };
                float g = 6.0f, w3 = (cw - g * 2) / 3.0f;
                for (int k = 0; k < 3; k++) {
                    float bx = cx + k * (w3 + g);
                    bool hov = isHovered(bx, ly, w3, bh), on = *v[k];
                    ui.drawRect(bx, ly, w3, bh, (on || hov) ? theme.buttonBgHover : theme.buttonBg);
                    ui.drawBorder(bx, ly, w3, bh, on ? theme.accent : theme.buttonBorder, on ? 2.0f : 1.0f);
                    ui.drawTextCentered(names[k], bx, ly, w3, bh, on ? theme.textHover : theme.textDim, 2);
                    if (hov && justClicked()) *v[k] = !*v[k];
                }
            }
            ly += bh + 4.0f;
            ly += wrapText(ui, "plants: grass and flowers. decor: bushes, logs, stones", cx, ly, cw, theme.textDim) + 4.0f;
            bool any = flat.anyDecoration();
            rowLabel(ui, theme, "Biome", lx, ly, bh, any);
            if (any) {
                std::vector<int> all = singleBiomeChoices();
                flat.biome = cyclePicker(ui, theme, cx, ly, cw, bh, all, flat.biome,
                                         [](int b) { return std::string(island::biomeInfo(b).name); });
            } else {
                fitText(ui, "nothing grows", cx + 8, ly + (bh - 16) * 0.5f, cw, theme.textDim);
            }
            ly += bh + rowGap;
            // a village to test building in, from the blueprints
            rowLabel(ui, theme, "Village", lx, ly, bh, true);
            {
                bool hov = isHovered(cx, ly, cw, bh), on = flat.village;
                ui.drawRect(cx, ly, cw, bh, (on || hov) ? theme.buttonBgHover : theme.buttonBg);
                ui.drawBorder(cx, ly, cw, bh, on ? theme.accent : theme.buttonBorder, on ? 2.0f : 1.0f);
                ui.drawTextCentered(on ? "Ahead of you" : "None", cx, ly, cw, bh, on ? theme.textHover : theme.textDim, 2);
                if (hov && justClicked()) flat.village = !flat.village;
            }
            ly += bh + rowGap;
        }

        }   // !copyFrom

        // ── Buttons ───────────────────────────────────────
        float bw = (float)theme.buttonW;
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

        if (createHov && justClicked() && !(copyFrom && devSaves.empty())) {
            if (worldName.empty()) worldName = copyFrom ? copyOf : "New World";
            return ScreenResult::NewWorld;
        }
        if (backHov && justClicked()) state = State::Main;

        handleTextInput();
        return ScreenResult::Continue;
    }

    // ── World Settings ────────────────────────────────────
    // Sliders for every generation value, defaults as tuned.
    // Pages: Terrain, Features, Biomes (a weight per biome)
    void drawWorldSettings(UIRenderer& ui, const UITheme& theme, float lx, float ly, float iw, float bottom) {
        ui.drawText("World Settings", lx, ly, theme.title, 2);
        ly += 28.0f;
        {   // page tabs
            static const char* PAGES[3] = { "Terrain", "Features", "Biomes" };
            float tw = (iw - 8) / 3.0f;
            for (int i = 0; i < 3; i++) {
                float tx = lx + i * (tw + 4);
                bool on = genPage == i, hov = isHovered(tx, ly, tw, 28);
                ui.drawRect(tx, ly, tw, 28, on || hov ? theme.buttonBgHover : theme.buttonBg);
                ui.drawBorder(tx, ly, tw, 28, on ? theme.accent : theme.buttonBorder, on ? 2.0f : 1.0f);
                ui.drawTextCentered(PAGES[i], tx, ly, tw, 28, on ? theme.textHover : theme.text, 2);
                if (hov && justClicked()) { genPage = i; genDrag = -1; }
            }
        }
        ly += 40.0f;
        if (!mousePressed) genDrag = -1;

        // one slider: label left, value right, track under
        auto slider = [&](int id, const char* label, float& v, float lo, float hi, float def, bool whole,
                          float x, float y, float w, int scale) {
            bool changed = v != def;
            ui.drawText(label, x, y, changed ? theme.accent : theme.text, scale);
            char b[24];
            if (whole) std::snprintf(b, sizeof(b), lo >= 30.0f ? "%d deg" : "%d", (int)(v + 0.5f));
            else       std::snprintf(b, sizeof(b), "x%.2f", v);
            ui.drawText(b, x + w - ui.textWidth(b, scale), y, changed ? theme.accent : theme.textDim, scale);
            float ty = y + (scale == 2 ? 20.0f : 12.0f), th = 6.0f;
            float t = (v - lo) / (hi - lo);
            ui.drawRect(x, ty, w, th, theme.inputBg);
            ui.drawRect(x, ty, w * t, th, theme.buttonBg);
            float dmark = x + w * (def - lo) / (hi - lo);
            ui.drawRect(dmark - 1, ty - 3, 2, th + 6, theme.border);          // the default
            ui.drawBorder(x, ty, w, th, theme.border, 1.0f);
            ui.drawRect(x + w * t - 5, ty - 5, 10, th + 10, genDrag == id ? theme.textHover : theme.accent);
            if (justClicked() && isHovered(x - 6, ty - 8, w + 12, th + 16)) genDrag = id;
            if (genDrag == id) {
                float f = std::clamp((float)(mouseX - x) / w, 0.0f, 1.0f);
                v = lo + f * (hi - lo);
                if (whole) v = (float)(int)(v + 0.5f);
                else if (std::fabs(v - def) < (hi - lo) * 0.02f) v = def;   // snap to the default
            }
        };

        if (genPage < 2) {
            const char* page = genPage == 0 ? "Terrain" : "Features";
            const auto& P = WorldGenParams::params();
            int id = 0;
            for (size_t i = 0; i < P.size(); i++) {
                if (std::string(P[i].page) != page) continue;
                slider((int)i, P[i].name, genParams.v[i], P[i].lo, P[i].hi, P[i].def, P[i].whole, lx, ly, iw, 2);
                ly += 44.0f;
                id++;
            }
        } else {
            ui.drawText("How often each biome is chosen", lx, ly, theme.textDim, 1);
            ly += 16.0f;
            std::vector<int> all = singleBiomeChoices();
            int rows = ((int)all.size() + 1) / 2;
            float cw = (iw - 16) / 2.0f;
            for (int k = 0; k < (int)all.size(); k++) {
                int b = all[k];
                float x = lx + (k / rows) * (cw + 16), y = ly + (k % rows) * 30.0f;
                slider(100 + b, island::biomeInfo(b).name, genParams.biomeWeight[(size_t)b], 0.0f, 3.0f, 1.0f, false,
                       x, y, cw, 1);
            }
        }
        genParams.clamp();

        // Defaults / Done
        float bh = 32.0f, bw = (iw - 10) / 2.0f, by = bottom - bh;
        bool defHov = isHovered(lx, by, bw, bh), doneHov = isHovered(lx + bw + 10, by, bw, bh);
        for (int k = 0; k < 2; k++) {
            float x = lx + k * (bw + 10);
            bool hov = k ? doneHov : defHov;
            ui.drawRect(x, by, bw, bh, hov ? theme.buttonBgHover : theme.buttonBg);
            ui.drawBorder(x, by, bw, bh, theme.buttonBorder, 1.0f);
            ui.drawTextCentered(k ? "Done" : (genPage == 2 ? "Reset biomes" : "Reset page"), x, by, bw, bh,
                                hov ? theme.textHover : theme.text, 2);
        }
        if (defHov && justClicked()) {
            if (genPage == 2) genParams.biomeWeight.fill(1.0f);
            else {
                const char* page = genPage == 0 ? "Terrain" : "Features";
                for (size_t i = 0; i < genParams.v.size(); i++)
                    if (std::string(WorldGenParams::params()[i].page) == page) genParams.v[i] = WorldGenParams::params()[i].def;
            }
        }
        if (doneHov && justClicked()) { genOpen = false; genDrag = -1; }
    }

    // The right-hand column: the island map, or a note for
    // world types without a map
    void drawPreview(UIRenderer& ui, const UITheme& theme, float x, float y, float w, float h) {
        if (copyFrom) {
            // the dev world being copied: what its world file says
            ui.drawText("Copy of a dev world", x, y, theme.title, 2);
            if (devSaves.empty()) {
                wrapText(ui, "There are no dev worlds yet. Make one: New World, Save in: Dev worlds. "
                             "Stage enemies, spawners and items in it, then Esc, Save.", x, y + 34, w, theme.textDim);
                return;
            }
            if (copyInfoFor != copyOf) {
                copyInfoFor = copyOf;
                copyInfo.clear();
                std::ifstream f(std::string(WorldSaveManager::rootFor(true)) + "/" + copyOf + "/world.vxrp");
                for (std::string l; std::getline(f, l);) {
                    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                    if (l.rfind("type=", 0) == 0) copyInfo.push_back(std::string("type: ") + worldTypeName(worldTypeFromKey(l.substr(5))));
                    else if (l.rfind("size=", 0) == 0) copyInfo.push_back("size: " + l.substr(5) + " x " + l.substr(5) + " chunks");
                    else if (l.rfind("mode=", 0) == 0) copyInfo.push_back("starts in: " + l.substr(5));
                    else if (l.rfind("flat.ground=", 0) == 0) copyInfo.push_back("ground: " + std::string(FlatOptions::groundLabel(l.substr(12))));
                }
                std::error_code ec;
                int structs = 0, npcs = 0;
                std::ifstream sf(std::string(WorldSaveManager::rootFor(true)) + "/" + copyOf + "/structures.dat");
                for (std::string l; std::getline(sf, l);) if (l.rfind("inst ", 0) == 0) structs++;
                std::ifstream nf(std::string(WorldSaveManager::rootFor(true)) + "/" + copyOf + "/npcs.dat");
                for (std::string l; std::getline(nf, l);) if (!l.empty() && l.rfind("npcs ", 0) != 0) npcs++;
                copyInfo.push_back(std::to_string(structs) + " structures, " + std::to_string(npcs) + " NPCs");
            }
            fitText(ui, copyOf, x, y + 34, w, theme.accent);
            float ty = y + 66;
            for (const std::string& l : copyInfo) { fitText(ui, l, x, ty, w, theme.text); ty += 24; }
            return;
        }
        if (worldType == WorldType::Classic) {
            ui.drawText("Classic", x, y, theme.title, 2);
            ui.drawText("Rolling hills and forest.", x, y + 30, theme.text, 2);
            ui.drawText("32 x 32 chunks, 0.26 mi", x, y + 52, theme.textDim, 2);
            return;
        }
        if (worldType == WorldType::Flat) {
            // the field: its ground block, and what grows on it
            ui.drawText("Flat", x, y, theme.title, 2);
            float iy = y + 34;
            uint16_t block = gRegistry.getIDByName(flat.ground);
            uint16_t item = block ? gItems.itemForBlock(block) : 0;
            float s = std::min(140.0f, w * 0.5f);
            if (item) drawItemIcon(ui, gItems.get(item), x, iy, s);
            float tx = x + s + 18, ty = iy + 8;
            fitText(ui, std::string(FlatOptions::groundLabel(flat.ground)) + " field", tx, ty, w - s - 18, theme.text);
            ty += 24;
            fitText(ui, "32 x 32 chunks, 0.26 mi", tx, ty, w - s - 18, theme.textDim);
            ty += 22;
            fitText(ui, "no hills, water or structures", tx, ty, w - s - 18, theme.textDim);
            iy += s + 24;
            auto line = [&](const char* what, bool on) {
                fitText(ui, what, x, iy, w, on ? theme.text : theme.textDim);
                iy += 24;
            };
            line(flat.trees ? "trees in patches" : "no trees", flat.trees);
            line(flat.plants ? "grass and flowers" : "no grass or flowers", flat.plants);
            line(flat.decor ? "bushes, logs and stones" : "no bushes, logs or stones", flat.decor);
            if (flat.anyDecoration())
                fitText(ui, std::string("as they grow in: ") + island::biomeInfo(flat.biome).name, x, iy + 6, w, theme.accent);
            return;
        }

        // Rebuild the chart when the seed, size or biome changes
        uint64_t seed = getNewWorldSeed() % 1000000ull;
        int wantSize  = worldType == WorldType::SingleBiome ? CLASSIC_WORLD_SIZE : islandSize;
        int wantBiome = worldType == WorldType::SingleBiome ? singleBiome : -1;
        if (seed != previewSeed || wantSize != previewSize || wantBiome != previewBiome ||
            (!mousePressed && genParams.hash() != previewParams)) {
            previewParams = genParams.hash();
            auto t0 = std::chrono::steady_clock::now();
            preview = wantBiome >= 0 ? island::singleBiomeChart((int)seed, wantBiome, wantSize)
                                     : island::collapseIsland((int)seed, wantSize, genParams);
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

    // Text at scale 1 wrapped to maxW; returns the height used
    float wrapText(UIRenderer& ui, const std::string& t, float x, float y, float maxW, const float* col) {
        std::string line, word;
        float lh = (float)ui.textHeight(1) + 5.0f, used = 0.0f;
        auto flush = [&]() { if (!line.empty()) { ui.drawText(line, x, y + used, col, 1); used += lh; line.clear(); } };
        for (size_t i = 0; i <= t.size(); i++) {
            if (i == t.size() || t[i] == ' ') {
                std::string cand = line.empty() ? word : line + " " + word;
                if (!line.empty() && ui.textWidth(cand, 1) > maxW) { flush(); line = word; }
                else line = cand;
                word.clear();
            } else word += t[i];
        }
        flush();
        return used;
    }

    // Text at scale 2, dropping to scale 1 if it would run past maxW
    void fitText(UIRenderer& ui, const std::string& t, float x, float y, float maxW, const float* col) {
        int scale = ui.textWidth(t, 2) <= maxW ? 2 : 1;
        ui.drawText(t, x, y, col, scale);
    }

    // Two lists side by side: ordinary worlds (saves/) and
    // dev worlds (devworlds/: stages that save only when asked)
    ScreenResult drawLoadWorldPanel(UIRenderer& ui,
                                     float px, float py,
                                     float pw,
                                     const UITheme& theme) {
        (void)px; (void)pw;
        float pad  = (float)theme.panelPad;
        float rowH = 34.0f, gap = 4.0f;
        float W = std::min(860.0f, (float)ui.getWidth() - 20.0f);
        float X = ((float)ui.getWidth() - W) * 0.5f;
        size_t most = std::max(saves.size(), devSaves.size());
        int fit = std::max(4, (int)(((float)ui.getHeight() - py - 140.0f) / (rowH + gap)));
        float ph = pad * 2 + 70.0f + (float)std::min<size_t>(std::max<size_t>(most, 1), (size_t)fit) * (rowH + gap) + 50.0f;
        drawPanel(ui, X, py, W, ph);
        ui.drawText("Load World", X + pad, py + pad, theme.title, 2);
        float colW = (W - pad * 3) * 0.5f;
        ScreenResult res = ScreenResult::Continue;
        auto column = [&](const char* title, const char* note, const std::vector<std::string>& list, bool dev, float cx) {
            float y = py + pad + 34.0f;
            ui.drawText(title, cx, y, theme.text, 2);
            ui.drawText(note, cx, y + 20, theme.textDim, 1);
            y += 38.0f;
            if (list.empty()) { ui.drawText(dev ? "none yet: New World, Save in: Dev worlds" : "No saved worlds found.", cx, y + 8, theme.textDim, 1); return; }
            for (int i = 0; i < (int)list.size() && i < fit; i++) {
                bool hov = isHovered(cx, y, colW, rowH);
                ui.drawRect(cx, y, colW, rowH, hov ? theme.buttonBgHover : theme.buttonBg);
                ui.drawBorder(cx, y, colW, rowH, dev ? theme.accent : theme.buttonBorder, 1.0f);
                fitText(ui, list[(size_t)i], cx + 10, y + (rowH - 16) * 0.5f, colW - 20, hov ? theme.textHover : theme.text);
                if (hov && justClicked()) { selectedWorld = list[(size_t)i]; selectedDev = dev; res = ScreenResult::LoadWorld; }
                y += rowH + gap;
            }
        };
        column("Worlds", "saved as you play", saves, false, X + pad);
        column("Dev worlds", "stages: save only when you do", devSaves, true, X + pad * 2 + colW);

        float bw = (float)theme.buttonW, bh = (float)theme.buttonH;
        float bx = X + (W - bw) * 0.5f, by = py + ph - pad - bh;
        bool backHov = isHovered(bx, by, bw, bh);
        drawButton(ui, "Back", bx, by, backHov);
        if (backHov && justClicked()) state = State::Main;
        return res;
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
        if (state == State::Join) {
            std::string& t = joinField == 0 ? joinAddress : joinName;
            bool ok = joinField == 0 ? (std::isalnum((int)codepoint) || codepoint == '.' || codepoint == ':' || codepoint == '-')
                                     : (std::isalnum((int)codepoint) || codepoint == '_' || codepoint == '-');
            if (ok && t.size() < (joinField == 0 ? 60u : 24u)) t += (char)codepoint;
            return;
        }
        std::string& target = nameActive ? worldName : worldSeed;
        if (target.size() < 32)
            target += (char)codepoint;
    }

    void onKey(int key) {
        if (state == State::Join) {
            std::string& t = joinField == 0 ? joinAddress : joinName;
            if (key == GLFW_KEY_BACKSPACE && !t.empty()) t.pop_back();
            if (key == GLFW_KEY_TAB) joinField = 1 - joinField;
            if (key == GLFW_KEY_ENTER) joinEnter = true;
            return;
        }
        std::string& target = nameActive ? worldName : worldSeed;
        if (key == GLFW_KEY_BACKSPACE && !target.empty())
            target.pop_back();
        if (key == GLFW_KEY_TAB)
            nameActive = !nameActive;
    }
};
