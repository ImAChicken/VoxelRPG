#pragma once
#include "UIScreen.h"
#include <GLFW/glfw3.h>

class PauseMenuScreen : public UIScreen {
public:
    PauseMenuScreen(GLFWwindow* win) : window(win) {}

    ScreenResult update(UIRenderer& ui, float dt) override {
        const auto& theme = gResourcePacks.getTheme();
        int sw = ui.getWidth();
        int sh = ui.getHeight();

        handleInput();

        ui.begin();

        // Semi transparent overlay over game
        ui.drawRect(0, 0, (float)sw, (float)sh,
                    theme.overlay);

        // Panel
        float pw = 300.0f;
        float ph = 200.0f;
        float px = (sw - pw) * 0.5f;
        float py = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);

        // Title
        float pad = (float)theme.panelPad;
        ui.drawText("Paused",
            px + pad, py + pad, theme.title, 2);

        // Buttons
        float bw = (float)theme.buttonW;
        float bh = (float)theme.buttonH;
        float bx = px + (pw - bw) * 0.5f;
        float by = py + 60.0f;
        float gap = 10.0f;

        struct Btn { std::string label; float y; };
        Btn buttons[] = {
            { "Resume",        by },
            { "Save and Quit", by + bh + gap },
        };

        for (int i = 0; i < 2; i++) {
            bool hov = isHovered(bx, buttons[i].y, bw, bh);
            drawButton(ui, buttons[i].label,
                       bx, buttons[i].y, hov);
            if (hov && justClicked()) {
                if (i == 0) return ScreenResult::ResumeGame;
                if (i == 1) return ScreenResult::SaveAndQuit;
            }
        }

        ui.end();
        return ScreenResult::Continue;
    }

private:
    GLFWwindow* window;
    double mouseX = 0, mouseY = 0;
    bool mousePressed = false, mousePrevPressed = false;

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
};