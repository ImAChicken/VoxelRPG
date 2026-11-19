#pragma once
#include "UIScreen.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>
#include <vector>

// ── The pause menu's buttons ──────────────────────────────
// One list, two places: the pause menu (Esc) and the left panel
// of the inventory (Select + Start on the pad, which has no pause
// button of its own). main.cpp acts on the results the same way.
struct PauseButton { const char* label; ScreenResult result; };
inline std::vector<PauseButton> pauseButtons(bool remote, bool devMenuAllowed, bool devWorld) {
    std::vector<PauseButton> list = { { "Resume", ScreenResult::ResumeGame } };
    if (remote) {
        list.push_back({ "Controls",   ScreenResult::OpenControls });
        list.push_back({ "Settings",   ScreenResult::OpenSettings });
        list.push_back({ "Disconnect", ScreenResult::QuitNoSave });
        return list;
    }
    if (devMenuAllowed) list.push_back({ "Dev Menu", ScreenResult::OpenDevMenu });
    list.push_back({ "Character",     ScreenResult::OpenCharacter });
    list.push_back({ "Controls",      ScreenResult::OpenControls });
    list.push_back({ "Settings",      ScreenResult::OpenSettings });
    list.push_back({ "Save",          ScreenResult::SaveGame });
    list.push_back({ "Save and Quit", ScreenResult::SaveAndQuit });
    if (devWorld) list.push_back({ "Quit without saving", ScreenResult::QuitNoSave });
    return list;
}

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
    // Playing on a server: nothing to save here, only to leave
    void setRemote(bool r) { remote = r; }
    bool isRemote() const { return remote; }
    bool devMenuIsAllowed() const { return devMenuAllowed; }
    bool isDevWorld() const { return devWorld; }
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

        std::vector<PauseButton> list = pauseButtons(remote, devMenuAllowed, devWorld);

        float bw  = (float)theme.buttonW;
        for (const PauseButton& b : list) bw = std::max(bw, (float)ui.textWidth(b.label, 2) + 28.0f);   // the longest label fits
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
        else if (remote)
            ui.drawText("on a server - the world keeps running", px + pad, py + pad + 24, theme.textDim, 1);
        else if (devWorld)
            ui.drawText("dev world - saves only when you do", px + pad, py + pad + 24, theme.textDim, 1);

        float bx = px + (pw - bw) * 0.5f;
        float by = py + 60.0f;
        ScreenResult result = ScreenResult::Continue;
        for (const PauseButton& b : list) {
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
    bool remote = false;
    std::string noticeText;
    float noticeAge = 99.0f;
    double mouseX = 0, mouseY = 0;
    bool mousePressed = false, mousePrevPressed = false;

    void handleInput() {
        mousePrevPressed = mousePressed;
        gNav.cursor(window, &mouseX, &mouseY);
        mousePressed =
            gNav.button(window, GLFW_MOUSE_BUTTON_LEFT);
    }

    bool justClicked() const {
        return mousePressed && !mousePrevPressed;
    }

    bool isHovered(float x, float y, float w, float h) const {
        gNav.add(x, y, w, h);                    // every rectangle asked about: the controller can reach it
        return mouseX >= x && mouseX <= x+w &&
               mouseY >= y && mouseY <= y+h;
    }
};
