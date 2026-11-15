#pragma once
#include "UIScreen.h"
#include "../input/InputManager.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>

// ══════════════════════════════════════════════════════════
// Controls — see and change every key binding
// ══════════════════════════════════════════════════════════
// Esc -> Controls. One row per action (from the input
// manager's action table). Click a binding, then press the
// new key, mouse button or turn the wheel. Esc cancels a
// rebind. Bindings shared by two actions are shown in red
// (except pairs meant to share, like sprint / roll).
//
// Every change is written straight to keyboard.toml.
// "Reset to defaults" puts every binding back.
class ControlsScreen : public UIScreen {
public:
    explicit ControlsScreen(GLFWwindow* win) : window(win) {}

    void onEnter() override {
        capturing = -1;
        scroll = 0.0f;
        mouseDown = mousePrev = true;     // ignore the opening click
    }

    // From the GLFW key callback (presses only)
    void onKey(int key) {
        if (capturing < 0) {
            if (key == GLFW_KEY_ESCAPE) wantBack = true;
            return;
        }
        const ActionInfo& info = InputManager::actionTable()[capturing];
        // Esc cancels — unless Esc is what's being bound to Pause
        if (key == GLFW_KEY_ESCAPE && info.action != InputAction::Pause) {
            capturing = -1;
            return;
        }
        InputBinding b;
        b.type = InputBinding::Type::Key;
        b.code = key;
        assign(b);
    }

    // From the GLFW scroll callback
    void onScroll(float dy) {
        if (capturing < 0) { scrollInput += dy; return; }
        InputBinding b;
        b.type = dy > 0 ? InputBinding::Type::MouseScrollUp
                        : InputBinding::Type::MouseScrollDown;
        assign(b);
    }

    bool isCapturing() const { return capturing >= 0; }

    ScreenResult update(UIRenderer& ui, float dt) override {
        (void)dt;
        const auto& theme = gResourcePacks.getTheme();
        const auto& table = InputManager::actionTable();
        float sw = (float)ui.getWidth(), sh = (float)ui.getHeight();
        pollMouse();

        // A mouse button pressed while waiting becomes the
        // binding (after the click that started the wait has
        // been let go)
        if (capturing >= 0) {
            if (!anyButtonDown()) captureArmed = true;
            else if (captureArmed) {
                for (int btn = 0; btn < 5; btn++)
                    if (glfwGetMouseButton(window, btn) == GLFW_PRESS) {
                        InputBinding b;
                        b.type = InputBinding::Type::MouseButton;
                        b.code = btn;
                        assign(b);
                        mouseDown = mousePrev = true;   // don't also click a row
                        break;
                    }
            }
        }

        ui.begin();
        ui.drawRect(0, 0, sw, sh, theme.overlay);
        float pw = std::min(1000.0f, sw - 40.0f), ph = std::min(660.0f, sh - 30.0f);
        float px = (sw - pw) * 0.5f, py = (sh - ph) * 0.5f;
        drawPanel(ui, px, py, pw, ph);
        ui.drawText("Controls", px + 16, py + 14, theme.title, 2);
        ui.drawText(capturing >= 0 ? "Press a key, mouse button or wheel   (Esc cancels)"
                                   : "Click a binding to change it",
                    px + 200, py + 16, capturing >= 0 ? theme.accent : theme.textDim, 2);

        // ── Rows ──────────────────────────────────────────
        const float rowH = 30.0f, headH = 28.0f;
        float listX = px + 16, listY = py + 50, listW = pw - 32;
        float listH = ph - 50 - 60;

        // Total height for scrolling
        float total = 0.0f;
        std::string sec;
        for (const ActionInfo& a : table) {
            if (sec != a.section) { sec = a.section; total += headH; }
            total += rowH;
        }
        if (capturing < 0) {
            scroll -= scrollInput * 40.0f;
        }
        scrollInput = 0.0f;
        scroll = std::clamp(scroll, 0.0f, std::max(0.0f, total - listH));

        float y = listY - scroll;
        sec.clear();
        for (size_t i = 0; i < table.size(); i++) {
            const ActionInfo& a = table[i];
            if (sec != a.section) {
                sec = a.section;
                if (visible(y, headH, listY, listH))
                    ui.drawText(sectionTitle(sec), listX, y + 8, theme.accent, 2);
                y += headH;
            }
            if (visible(y, rowH, listY, listH)) {
                InputBinding b = gInput.getBinding(a.action);
                std::string clash = conflictWith(i);
                ui.drawText(a.label, listX + 12, y + 8,
                            clash.empty() ? theme.text : RED, 2);

                float bx = listX + listW - 300, bw = 290, bh = rowH - 4;
                bool hov = hovered(bx, y + 2, bw, bh) && capturing < 0;
                bool waiting = (capturing == (int)i);
                ui.drawRect(bx, y + 2, bw, bh,
                            waiting ? theme.buttonBgHover : (hov ? theme.bgHover : theme.inputBg));
                ui.drawBorder(bx, y + 2, bw, bh,
                              waiting ? theme.accent : (clash.empty() ? theme.border : RED),
                              waiting ? 2.0f : 1.0f);
                ui.drawTextCentered(waiting ? "..." : InputManager::bindingLabel(b),
                                    bx, y + 2, bw, bh, theme.textHover, 2);
                if (!clash.empty())
                    ui.drawText("also " + clash, bx - 12 - ui.textWidth("also " + clash, 1),
                                y + 11, RED, 1);
                if (hov && clicked()) { capturing = (int)i; captureArmed = false; }
            }
            y += rowH;
        }

        // ── Buttons ───────────────────────────────────────
        float bw = (float)theme.buttonW, bh = (float)theme.buttonH;
        float by = py + ph - bh - 14;
        float resetX = px + 16, backX = px + pw - bw - 16;
        bool resetHov = hovered(resetX, by, bw + 110, bh) && capturing < 0;
        bool backHov  = hovered(backX, by, bw, bh) && capturing < 0;
        ui.drawRect(resetX, by, bw + 110, bh, resetHov ? theme.buttonBgHover : theme.buttonBg);
        ui.drawBorder(resetX, by, bw + 110, bh, theme.buttonBorder, 1.0f);
        ui.drawTextCentered("Reset to defaults", resetX, by, bw + 110, bh,
                            resetHov ? theme.textHover : theme.text, 2);
        drawButton(ui, "Back", backX, by, backHov);

        ScreenResult result = ScreenResult::Continue;
        if (resetHov && clicked()) {
            gInput.resetToDefaults();
            gInput.saveKeyboard();
        }
        if ((backHov && clicked()) || wantBack) {
            wantBack = false;
            capturing = -1;
            result = ScreenResult::CloseMenu;
        }
        ui.end();
        return result;
    }

private:
    GLFWwindow* window;
    int   capturing    = -1;     // row waiting for a new key
    bool  captureArmed = false;  // mouse released since the wait began
    bool  wantBack     = false;
    float scroll = 0.0f, scrollInput = 0.0f;

    double mx = 0, my = 0;
    bool mouseDown = false, mousePrev = false;

    const float RED[4] = {255, 110, 90, 255};

    void assign(const InputBinding& b) {
        const ActionInfo& info = InputManager::actionTable()[capturing];
        gInput.rebind(info.action, b);
        gInput.saveKeyboard();
        capturing = -1;
    }

    void pollMouse() {
        mousePrev = mouseDown;
        glfwGetCursorPos(window, &mx, &my);
        mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    }
    bool clicked() const { return mouseDown && !mousePrev; }
    bool hovered(float x, float y, float w, float h) const {
        return mx >= x && mx <= x + w && my >= y && my <= y + h;
    }
    bool anyButtonDown() const {
        for (int b = 0; b < 5; b++)
            if (glfwGetMouseButton(window, b) == GLFW_PRESS) return true;
        return false;
    }
    static bool visible(float y, float h, float top, float height) {
        return y >= top - 1 && y + h <= top + height + 1;
    }

    static std::string sectionTitle(const std::string& s) {
        if (s == "movement") return "Movement";
        if (s == "actions")  return "Actions";
        if (s == "camera")   return "Camera";
        if (s == "dev")      return "Dev / building";
        if (s == "system")   return "System";
        return s;
    }

    // Label of another action using the same input, if any
    std::string conflictWith(size_t i) const {
        const auto& table = InputManager::actionTable();
        InputBinding mine = gInput.getBinding(table[i].action);
        for (size_t j = 0; j < table.size(); j++) {
            if (j == i) continue;
            if (InputManager::allowedToShare(table[i].action, table[j].action)) continue;
            if (mine.sameAs(gInput.getBinding(table[j].action)))
                return table[j].label;
        }
        return "";
    }
};
