#pragma once
#include "UIScreen.h"
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

// ── Pause menu (Esc) ──────────────────────────────────────
// A column of buttons. Adding an entry: one line in
// buttons() and a ScreenResult for it.
class PauseMenuScreen : public UIScreen {
public:
    PauseMenuScreen(GLFWwindow* win) : window(win) {}

    // The Dev Menu button only shows outside Survival: the
    // dev inventory is not reachable from Survival
    void setDevMenuAllowed(bool allowed) { devMenuAllowed = allowed; }

    void onEnter() override {
        // Ignore the click that may still be held from the
        // screen we came from
        mousePressed = mousePrevPressed = true;
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        const auto& theme = gResourcePacks.getTheme();
        int sw = ui.getWidth();
        int sh = ui.getHeight();

        handleInput();
        ui.begin();

        // Semi transparent overlay over game
        ui.drawRect(0, 0, (float)sw, (float)sh, theme.overlay);

        struct Btn { const char* label; ScreenResult result; };
        std::vector<Btn> list = { { "Resume", ScreenResult::ResumeGame } };
        if (devMenuAllowed)
            list.push_back({ "Dev Menu", ScreenResult::OpenDevMenu });
        list.push_back({ "Controls",      ScreenResult::OpenControls });
        list.push_back({ "Settings",      ScreenResult::OpenSettings });
        list.push_back({ "Save and Quit", ScreenResult::SaveAndQuit });

        float bw  = (float)theme.buttonW;
        float bh  = (float)theme.buttonH;
        float gap = 10.0f;
        float pw  = 300.0f;
        float ph  = 90.0f + list.size() * (bh + gap);
        float px  = (sw - pw) * 0.5f;
        float py  = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);

        float pad = (float)theme.panelPad;
        ui.drawText("Paused", px + pad, py + pad, theme.title, 2);

        float bx = px + (pw - bw) * 0.5f;
        float by = py + 60.0f;
        ScreenResult result = ScreenResult::Continue;
        for (const Btn& b : list) {
            bool hov = isHovered(bx, by, bw, bh);
            drawButton(ui, b.label, bx, by, hov);
            if (hov && justClicked()) result = b.result;
            by += bh + gap;
        }

        ui.end();
        return result;
    }

private:
    GLFWwindow* window;
    bool devMenuAllowed = false;
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
