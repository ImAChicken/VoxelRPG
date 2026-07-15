#pragma once
#include "UIScreen.h"
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

        float panelW = 420.0f;
        float panelX = (sw - panelW) * 0.5f;

        if (state == State::Main) {
            result = drawMainButtons(ui, panelX, 200.0f,
                                     panelW, theme);
        } else if (state == State::NewWorld) {
            result = drawNewWorldPanel(ui, panelX, 180.0f,
                                       panelW, theme);
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
    std::string getLoadWorldName() const { return selectedWorld; }

private:
    enum class State { Main, NewWorld, LoadWorld };

    GLFWwindow* window;
    State       state       = State::Main;
    ScreenResult result     = ScreenResult::Continue;

    // New world inputs
    std::string worldName;
    std::string worldSeed;
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

    ScreenResult drawNewWorldPanel(UIRenderer& ui,
                                    float px, float py,
                                    float pw,
                                    const UITheme& theme) {
        float pad = (float)theme.panelPad;
        float ph  = 240.0f;
        drawPanel(ui, px, py, pw, ph);

        float lx = px + pad;
        float ly = py + pad;
        float iw = pw - pad * 2;

        // Title
        ui.drawText("New World", lx, ly, theme.title, 2);
        ly += 24.0f;

        // World name input
        ui.drawText("World Name:", lx, ly, theme.text, 2);
        ly += 20.0f;
        bool nameHov = isHovered(lx, ly, iw,
                                  (float)theme.buttonH);
        if (nameHov && justClicked()) nameActive = true;
        drawInput(ui, worldName, "My World",
                  lx, ly, iw, nameActive, showCursor);
        ly += theme.buttonH + 10.0f;

        // Seed input
        ui.drawText("Seed (optional):", lx, ly, theme.text, 2);
        ly += 20.0f;
        bool seedHov = isHovered(lx, ly, iw,
                                  (float)theme.buttonH);
        if (seedHov && justClicked()) nameActive = false;
        drawInput(ui, worldSeed, "42",
                  lx, ly, iw, !nameActive, showCursor);
        ly += theme.buttonH + 16.0f;

        // Buttons
        float bw = (float)theme.buttonW;
        float bh = (float)theme.buttonH;
        float createX = px + pad;
        float backX   = px + pw - pad - bw;

        bool createHov = isHovered(createX, ly, bw, bh);
        bool backHov   = isHovered(backX,   ly, bw, bh);

        drawButton(ui, "Create World", createX, ly, createHov);
        drawButton(ui, "Back",         backX,   ly, backHov);

        if (createHov && justClicked()) {
            if (worldName.empty()) worldName = "New World";
            return ScreenResult::NewWorld;
        }
        if (backHov && justClicked()) {
            state = State::Main;
        }

        // Text input handling
        // (character input handled via glfwSetCharCallback)
        handleTextInput();

        return ScreenResult::Continue;
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