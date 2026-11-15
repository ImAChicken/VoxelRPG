#pragma once
#include "UIRenderer.h"
#include "../render/ResourcePack.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// Console — the typed command line
// ══════════════════════════════════════════════════════════
// ` opens and closes it. Enter runs the line (see
// game/Commands.h), Up/Down walk back through earlier lines,
// Escape closes. While it is open the game ignores the
// keyboard.
//
// Messages from commands, dev tools and the editor are
// printed here. When the console is closed the newest few
// still show for a few seconds in the top-left corner.
class Console {
public:
    static constexpr size_t MAX_LINES   = 200;
    static constexpr float  SHOW_CLOSED = 6.0f;   // seconds

    bool open = false;

    void print(const std::string& text) {
        // Long messages wrap so nothing runs off screen
        const size_t width = 76;
        std::string rest = text;
        while (rest.size() > width) {
            size_t cut = rest.rfind(' ', width);
            if (cut == std::string::npos || cut < width / 2) cut = width;
            lines.push_back({ rest.substr(0, cut), 0.0f });
            rest = rest.substr(cut + (rest[cut] == ' ' ? 1 : 0));
        }
        lines.push_back({ rest, 0.0f });
        while (lines.size() > MAX_LINES) lines.pop_front();
    }

    void toggle() {
        open = !open;
        input.clear();
        historyPos = (int)history.size();
        swallowNextChar = open;   // the ` that opened it
    }

    // Typed characters (GLFW char callback)
    void onChar(unsigned int c) {
        if (!open) return;
        if (swallowNextChar) { swallowNextChar = false; if (c == '`') return; }
        if (c == '`') return;
        if (c >= 32 && c < 127 && input.size() < 200) input += (char)c;
    }

    // Special keys (GLFW key callback). Returns true and
    // fills 'submitted' when Enter is pressed on a line.
    //   repeat — the key is being held (auto-repeat)
    //   mods   — GLFW modifier bits (Shift etc.)
    bool onKey(int key, bool repeat, int mods, std::string& submitted) {
        if (!open) return false;
        swallowNextChar = false;
        switch (key) {
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            if (input.empty()) return false;
            submitted = input;
            if (history.empty() || history.back() != input)
                history.push_back(input);
            historyPos = (int)history.size();
            print("> " + input);
            input.clear();
            return true;
        case GLFW_KEY_BACKSPACE:
            if (!input.empty()) input.pop_back();
            return false;
        case GLFW_KEY_UP:
            if (historyPos > 0) input = history[--historyPos];
            return false;
        case GLFW_KEY_DOWN:
            if (historyPos < (int)history.size()) historyPos++;
            input = historyPos < (int)history.size()
                  ? history[historyPos] : "";
            return false;
        case GLFW_KEY_GRAVE_ACCENT:
            // Shift + ` types "~" (used in coordinates), and
            // holding ` must not close what it just opened
            if (repeat || (mods & GLFW_MOD_SHIFT)) return false;
            open = false;
            input.clear();
            return false;
        case GLFW_KEY_ESCAPE:
            open = false;
            input.clear();
            return false;
        default:
            return false;
        }
    }

    void update(float dt) {
        for (auto& l : lines) l.age += dt;
        blink += dt;
    }

    void draw(UIRenderer& ui) {
        const auto& theme = gResourcePacks.getTheme();
        float sw = (float)ui.getWidth();
        float sh = (float)ui.getHeight();
        const float lineH = 18.0f;

        ui.begin();
        if (open) {
            // Box across the lower part of the screen
            const int shown = 14;
            float h = lineH * (shown + 1) + 16.0f;
            float y = sh - h - 70.0f;
            ui.drawRect(8.0f, y, sw - 16.0f, h, BACK);
            ui.drawBorder(8.0f, y, sw - 16.0f, h, theme.border, 1.0f);

            size_t first = lines.size() > (size_t)shown
                         ? lines.size() - shown : 0;
            float ly = y + 8.0f;
            for (size_t i = first; i < lines.size(); i++) {
                ui.drawText(lines[i].text, 16.0f, ly, colorFor(lines[i].text), 2);
                ly += lineH;
            }
            float iy = y + h - lineH - 6.0f;
            ui.drawRect(12.0f, iy - 3.0f, sw - 24.0f, lineH + 4.0f, theme.inputBg);
            std::string shownInput = "/ " + input;
            ui.drawText(shownInput, 16.0f, iy, theme.textHover, 2);
            if (((int)(blink * 2.0f)) % 2 == 0)
                ui.drawRect(16.0f + ui.textWidth(shownInput, 2), iy,
                            2.0f, 16.0f, theme.inputCursor);
        } else {
            // Newest few messages, fading out
            float ly = 46.0f;
            size_t first = lines.size() > 5 ? lines.size() - 5 : 0;
            for (size_t i = first; i < lines.size(); i++) {
                if (lines[i].age > SHOW_CLOSED) continue;
                float fade = 1.0f - lines[i].age / SHOW_CLOSED;
                float c[4] = {235, 235, 235, 255.0f * std::min(1.0f, fade * 2.0f)};
                float bg[4] = {0, 0, 0, 110.0f * std::min(1.0f, fade * 2.0f)};
                ui.drawRect(10.0f, ly - 2.0f,
                            (float)ui.textWidth(lines[i].text, 2) + 8.0f,
                            lineH, bg);
                ui.drawText(lines[i].text, 14.0f, ly, c, 2);
                ly += lineH;
            }
        }
        ui.end();
    }

private:
    struct Line { std::string text; float age; };
    std::deque<Line>         lines;
    std::vector<std::string> history;
    int         historyPos = 0;
    std::string input;
    bool        swallowNextChar = false;
    float       blink = 0.0f;

    const float BACK[4]  = {8, 8, 12, 215};
    const float TYPED[4] = {150, 200, 255, 255};
    const float NORMAL[4]= {225, 225, 225, 255};
    const float WARN[4]  = {255, 170, 110, 255};

    const float* colorFor(const std::string& s) const {
        if (s.rfind("> ", 0) == 0) return TYPED;
        if (s.find("unknown") != std::string::npos ||
            s.find("usage") != std::string::npos ||
            s.find("no ") == 0 || s.find("skipped") != std::string::npos)
            return WARN;
        return NORMAL;
    }
};
