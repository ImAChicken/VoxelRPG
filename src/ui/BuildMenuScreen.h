#pragma once
#include "UIScreen.h"
#include "../entity/Player.h"
#include "../game/BuildTool.h"
#include "../game/StructureRegistry.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Build menu — what the Builder's Hammer can place
// ══════════════════════════════════════════════════════════
// Opened by right clicking with the hammer (Survival and
// Test Play). Lists every structure marked menu = true in
// structures.toml: foundations, floors, walls, doors …
// Click one to pick it and go straight back to building.
// Esc or right click closes without changing anything.
class BuildMenuScreen : public UIScreen {
public:
    explicit BuildMenuScreen(GLFWwindow* win) : window(win) {}

    void setContext(Player* p, BuildTool* b) { player = p; build = b; }

    void onEnter() override {
        mouseDown = mousePrev = true;      // ignore the opening click
        rightDown = rightPrev = true;
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        if (!player || !build) return ScreenResult::CloseMenu;
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        mousePrev = mouseDown; rightPrev = rightDown;
        glfwGetCursorPos(window, &mx, &my);
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
        rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool survival = player->mode == GameMode::Survival;

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);
        std::vector<uint16_t> list = gStructures.buildMenu();
        const float tileW = 470.0f, tileH = 58.0f, gap = 8.0f;
        int   cols = 2;
        int   rows = ((int)list.size() + cols - 1) / cols;
        float pw = cols * tileW + (cols - 1) * gap + 32.0f;
        float ph = 70.0f + rows * (tileH + gap) + 30.0f;
        float px = (sw - pw) * 0.5f, py = std::max(10.0f, (sh - ph) * 0.5f);
        drawPanel(ui, px, py, pw, ph);
        ui.drawText("Build", px + 16, py + 14, theme.title, 2);
        ui.drawText(survival ? "costs come from your inventory" : "Test Play: free",
                    px + 120, py + 16, theme.textDim, 2);

        ScreenResult result = ScreenResult::Continue;
        for (size_t i = 0; i < list.size(); i++) {
            const StructureType* t = gStructures.get(list[i]);
            if (!t) continue;
            float x = px + 16 + (i % cols) * (tileW + gap);
            float y = py + 56 + (i / cols) * (tileH + gap);
            bool hov = hovered(x, y, tileW, tileH);
            bool on  = build->selected == t->id;
            bool canPay = !survival || affordable(*t);
            ui.drawRect(x, y, tileW, tileH, hov ? theme.bgHover : theme.inputBg);
            ui.drawBorder(x, y, tileW, tileH, on ? theme.accent : theme.border,
                          on ? 2.0f : 1.0f);
            const VoxelTypeDef& b = gRegistry.get(t->block);
            float c[4] = { b.color[0] * 255, b.color[1] * 255, b.color[2] * 255, 255 };
            ui.drawRect(x + 8, y + 8, 42, 42, c);
            ui.drawText(t->displayName, x + 62, y + 9,
                        hov ? theme.textHover : theme.text, 2);
            ui.drawText(t->feet() + "   " + t->costText(), x + 62, y + 34,
                        canPay ? theme.textDim : RED, 1);
            if (hov && clicked()) {
                build->select(t->id);
                result = ScreenResult::CloseMenu;
            }
        }
        ui.drawText("click to choose   T turns it   arrows / Page Up / Page Down fine-tune",
                    px + 16, py + ph - 22, theme.textDim, 1);
        if (rightDown && !rightPrev) result = ScreenResult::CloseMenu;
        ui.end();
        return result;
    }

private:
    GLFWwindow* window;
    Player*     player = nullptr;
    BuildTool*  build  = nullptr;
    double mx = 0, my = 0;
    bool mouseDown = false, mousePrev = false, rightDown = false, rightPrev = false;
    const float RED[4] = {255, 110, 90, 255};

    bool affordable(const StructureType& t) const {
        for (auto& [item, n] : t.cost)
            if (player->inventory.count(item) < n) return false;
        return true;
    }
    bool clicked() const { return mouseDown && !mousePrev; }
    bool hovered(float x, float y, float w, float h) const {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }
};
