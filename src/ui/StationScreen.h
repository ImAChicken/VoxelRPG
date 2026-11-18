#pragma once
#include "UIScreen.h"
#include "../game/StructureRegistry.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// StationScreen — the menu of a crafting station (E on one)
// ══════════════════════════════════════════════════════════
// Workbench, forge, anvil, campfire, hearth: any structure of
// kind "station" (structures.toml). For now a placeholder that
// names the station and what it will be for; the real recipes
// go here once crafting exists. Each station type gets its own
// lines in placeholderFor().
class StationScreen : public UIScreen {
public:
    explicit StationScreen(GLFWwindow* win) : window(win) {}

    void setStation(const StructureType* t) { type = t; }

    void onEnter() override { mouseDown = mousePrev = true; }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        mousePrev = mouseDown;
        glfwGetCursorPos(window, &mx, &my);
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);
        const float pw = 620.0f, ph = 300.0f;
        float px = (sw - pw) * 0.5f, py = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);
        std::string title = type ? type->title : std::string("Station");
        ui.drawText(title, px + 20, py + 16, theme.title, 3);
        float y = py + 70;
        for (const std::string& line : placeholderFor(type ? type->station : std::string())) {
            ui.drawText(line, px + 20, y, theme.text, 2);
            y += 28;
        }
        ui.drawText("Crafting is not in the game yet.", px + 20, py + ph - 90, theme.textDim, 2);

        ScreenResult result = ScreenResult::Continue;
        float bw = 160.0f, bx = px + pw - bw - 20, by = py + ph - 56;
        bool hov = mx >= bx && mx <= bx + bw && my >= by && my <= by + theme.buttonH;
        drawButton(ui, "Close", bx, by, hov, 2, bw);
        if (hov && mouseDown && !mousePrev) result = ScreenResult::CloseMenu;
        ui.end();
        return result;
    }

private:
    GLFWwindow* window;
    const StructureType* type = nullptr;
    double mx = 0, my = 0;
    bool mouseDown = false, mousePrev = false;

    static std::vector<std::string> placeholderFor(const std::string& station) {
        if (station == "workbench") return { "Wooden tools, furniture and building parts", "will be made here." };
        if (station == "forge")     return { "Smelt ore into bars, and heat metal", "for the anvil." };
        if (station == "anvil")     return { "Forge weapons, armour and metal tools", "from bars." };
        if (station == "campfire")  return { "Cook food and rest here.", "Its light keeps the night back." };
        if (station == "hearth")    return { "Cook meals for the household", "and warm the house." };
        return { "This station's recipes will be listed here." };
    }
};
