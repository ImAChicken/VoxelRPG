#pragma once
#include "UIScreen.h"
#include "../game/Settings.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Settings — sliders for player preferences
// ══════════════════════════════════════════════════════════
// Esc -> Settings. Drag a slider; the change shows at once
// (the toolbar is drawn behind the menu so you can see it)
// and is saved to data/settings.toml when you let go.
//
// Adding a setting: one addSlider() line in the constructor
// (plus the field in game/Settings.h).
class SettingsScreen : public UIScreen {
public:
    explicit SettingsScreen(GLFWwindow* win) : window(win) {
        addSlider("Toolbar size",    &gSettings.toolbarScale,     0.5f,  2.0f,
                  [](float v) { return times(v); });
        addSlider("Toolbar opacity", &gSettings.toolbarOpacity,   0.1f,  1.0f,
                  [](float v) { return outOf100(v); });
        addSlider("Mouse sensitivity", &gSettings.mouseSensitivity, 0.02f, 0.4f,
                  [](float v) { return times(v / GameSettings::DEF_SENSITIVITY); });
    }

    void onEnter() override {
        dragging = -1;
        mouseDown = mousePrev = true;
    }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        mousePrev = mouseDown;
        glfwGetCursorPos(window, &mx, &my);
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        ui.begin();
        // Lighter overlay than other menus, so the toolbar
        // behind stays visible while you adjust it
        float dim[4] = {0, 0, 0, 90};
        ui.drawRect(0, 0, sw, sh, dim);
        float pw = 620.0f, ph = 120.0f + sliders.size() * 64.0f;
        float px = (sw - pw) * 0.5f, py = std::max(20.0f, (sh - ph) * 0.5f - 80.0f);
        drawPanel(ui, px, py, pw, ph);
        ui.drawText("Settings", px + 16, py + 14, theme.title, 2);

        float y = py + 56;
        for (size_t i = 0; i < sliders.size(); i++) {
            Slider& s = sliders[i];
            ui.drawText(s.label, px + 20, y, theme.text, 2);
            std::string val = s.format(*s.value);
            ui.drawText(val, px + pw - 20 - ui.textWidth(val, 2), y, theme.accent, 2);

            float tx = px + 20, tw = pw - 40, ty = y + 26, th = 8;
            float t = (*s.value - s.lo) / (s.hi - s.lo);
            ui.drawRect(tx, ty, tw, th, theme.inputBg);
            ui.drawRect(tx, ty, tw * t, th, theme.buttonBgHover);
            ui.drawBorder(tx, ty, tw, th, theme.border, 1.0f);
            float kx = tx + tw * t;
            ui.drawRect(kx - 6, ty - 6, 12, th + 12,
                        dragging == (int)i ? theme.textHover : theme.accent);

            if (clicked() && mx >= tx - 8 && mx <= tx + tw + 8 &&
                my >= ty - 10 && my <= ty + th + 10)
                dragging = (int)i;
            if (dragging == (int)i) {
                float f = std::clamp((float)(mx - tx) / tw, 0.0f, 1.0f);
                *s.value = s.lo + f * (s.hi - s.lo);
            }
            y += 64;
        }
        if (!mouseDown && dragging >= 0) {     // let go → save
            dragging = -1;
            gSettings.clamp();
            gSettings.save();
        }

        float bw = (float)theme.buttonW, bh = (float)theme.buttonH;
        float by = py + ph - bh - 14;
        float resetX = px + 16, backX = px + pw - bw - 16;
        bool resetHov = hovered(resetX, by, bw + 110, bh);
        bool backHov  = hovered(backX, by, bw, bh);
        ui.drawRect(resetX, by, bw + 110, bh, resetHov ? theme.buttonBgHover : theme.buttonBg);
        ui.drawBorder(resetX, by, bw + 110, bh, theme.buttonBorder, 1.0f);
        ui.drawTextCentered("Reset to defaults", resetX, by, bw + 110, bh,
                            resetHov ? theme.textHover : theme.text, 2);
        drawButton(ui, "Back", backX, by, backHov);

        ScreenResult result = ScreenResult::Continue;
        if (resetHov && clicked()) {
            gSettings.resetToDefaults();
            gSettings.save();
        }
        if (backHov && clicked()) {
            gSettings.save();
            result = ScreenResult::CloseMenu;
        }
        ui.end();
        return result;
    }

private:
    struct Slider {
        std::string label;
        float* value;
        float  lo, hi;
        std::function<std::string(float)> format;
    };
    std::vector<Slider> sliders;
    GLFWwindow* window;
    int dragging = -1;
    double mx = 0, my = 0;
    bool mouseDown = false, mousePrev = false;

    void addSlider(const std::string& label, float* v, float lo, float hi,
                   std::function<std::string(float)> fmt) {
        sliders.push_back({ label, v, lo, hi, std::move(fmt) });
    }
    // The UI font has no % sign, so: "x1.25" and "60 / 100"
    static std::string times(float v) {
        char b[16];
        std::snprintf(b, sizeof(b), "x%.2f", v);
        return b;
    }
    static std::string outOf100(float v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%d / 100", (int)(v * 100.0f + 0.5f));
        return b;
    }
    bool clicked() const { return mouseDown && !mousePrev; }
    bool hovered(float x, float y, float w, float h) const {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }
};
