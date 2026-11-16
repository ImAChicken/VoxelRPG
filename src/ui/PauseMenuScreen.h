#pragma once
#include "UIScreen.h"
#include <GLFW/glfw3.h>
#include <algorithm>
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
    // A dev world: never saved by itself, so it also offers
    // leaving without saving
    void setDevWorld(bool dev) { devWorld = dev; }
    // A line under the title for a moment ("world saved")
    void notice(const std::string& text) { noticeText = text; noticeAge = 0.0f; }

    void onEnter() override {
        // Ignore the click that may still be held from the
        // screen we came from
        mousePressed = mousePrevPressed = true;
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        noticeAge += dt;
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
        list.push_back({ "Character",     ScreenResult::OpenCharacter });
        list.push_back({ "Controls",      ScreenResult::OpenControls });
        list.push_back({ "Settings",      ScreenResult::OpenSettings });
        list.push_back({ "Save",          ScreenResult::SaveGame });
        list.push_back({ "Save and Quit", ScreenResult::SaveAndQuit });
        if (devWorld) list.push_back({ "Quit without saving", ScreenResult::QuitNoSave });

        float bw  = (float)theme.buttonW;
        for (const Btn& b : list) bw = std::max(bw, (float)ui.textWidth(b.label, 2) + 28.0f);   // the longest label fits
        float bh  = (float)theme.buttonH;
        float gap = 10.0f;
        float pw  = std::max(300.0f, bw + 60.0f);
        float ph  = 90.0f + list.size() * (bh + gap);
        float px  = (sw - pw) * 0.5f;
        float py  = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);

        float pad = (float)theme.panelPad;
        ui.drawText("Paused", px + pad, py + pad, theme.title, 2);
        if (!noticeText.empty() && noticeAge < 2.5f)
            ui.drawText(noticeText, px + pad, py + pad + 24, theme.accent, 1);
        else if (devWorld)
            ui.drawText("dev world - saves only when you do", px + pad, py + pad + 24, theme.textDim, 1);

        float bx = px + (pw - bw) * 0.5f;
        float by = py + 60.0f;
        ScreenResult result = ScreenResult::Continue;
        for (const Btn& b : list) {
            bool hov = isHovered(bx, by, bw, bh);
            drawButton(ui, b.label, bx, by, hov, 2, bw);
            if (hov && justClicked()) result = b.result;
            by += bh + gap;
        }

        ui.end();
        return result;
    }

private:
    GLFWwindow* window;
    bool devMenuAllowed = false;
    bool devWorld = false;
    std::string noticeText;
    float noticeAge = 99.0f;
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
