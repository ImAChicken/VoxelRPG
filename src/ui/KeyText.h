#pragma once
#include "PadGlyphs.h"
#include "UIRenderer.h"
#include "../input/InputManager.h"
#include "../input/PadLayout.h"
#include <algorithm>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// KeyText — prompts that name the button you'd actually press
// ══════════════════════════════════════════════════════════
// Write a prompt with the ACTION in braces, never a key:
//
//     "Plank Door   {interact}: open"
//
// The name is the action's keyboard.toml name (input/InputManager.h
// actionTable: interact, use_left, rotate_piece, tool_cancel …), plus
// {nudge} for the four move keys, {click} for a menu click, and
// {button:A} for one pad button by name (in pad-only lines). Drawn, each one becomes what is
// bound to it right now: the key's name with the keyboard, the
// button's picture with the controller (both buttons of a combo:
// Start + LB for interact, Select + Start for the inventory; Y + up
// for nudge_up), in the layer in force. So a prompt is right after a
// rebind, on either device, in combat or out. plain() gives the
// words alone (for the console / logs).
namespace keytext {

struct Piece {
    std::string text;               // plain words, or the key's name (keyboard)
    bool key = false;               // a {token}
    std::vector<int> pads;          // its pad buttons (pad last used)
};

// The pad buttons that do 'a' now: the button in the layers in
// force, or the combo that reaches it (the quick / tool alt button
// first), or nothing
inline std::vector<int> padButtonsFor(InputAction a) {
    if (a == InputAction::Inventory) return { pad::drawButton(), pad::quickButton() };
    const pad::Layer now = gInput.layerNow();
    for (const pad::Map& m : pad::layout())
        if (m.action == a && pad::inLayer(m.layer, now, m.button)) return { (int)m.button };
    for (const pad::Map& m : pad::layout()) {
        if (m.action != a) continue;
        if (m.layer == pad::Layer::Quick)   return { (int)pad::quickButton(), (int)m.button };
        if (m.layer == pad::Layer::ToolAlt) return { (int)pad::toolAltButton(), (int)m.button };
        return { (int)m.button };
    }
    return {};
}

inline InputAction actionNamed(const std::string& n, bool& found) {
    for (const ActionInfo& i : InputManager::actionTable())
        if (n == i.key) { found = true; return i.action; }
    found = false;
    return InputAction::Jump;
}

inline std::vector<Piece> parse(const std::string& s) {
    std::vector<Piece> out;
    const bool pad = gInput.padLastUsed();
    size_t i = 0;
    while (i < s.size()) {
        size_t open = s.find('{', i);
        size_t close = open == std::string::npos ? open : s.find('}', open);
        if (open == std::string::npos || close == std::string::npos) { out.push_back({ s.substr(i), false, {} }); break; }
        if (open > i) out.push_back({ s.substr(i, open - i), false, {} });
        std::string name = s.substr(open + 1, close - open - 1);
        Piece p;
        p.key = true;
        if (name.rfind("button:", 0) == 0) {            // a pad button by name ({button:A}): for pad-only lines
            int b = pad::buttonFromName(name.substr(7));
            p.text = name.substr(7);
            if (b >= 0) p.pads = { b };
        } else if (name == "click") {                           // in a menu: the mouse, or the pad's A (ui/PadNav.h)
            p.text = "click";
            if (pad) p.pads = { pad::A };
        } else if (name == "nudge") {                    // the four move keys
            p.text = "ARROWS";
            if (pad) p.pads = { pad::Up };
        } else {
            bool found = false;
            InputAction a = actionNamed(name, found);
            if (!found) p.text = name;
            else {
                p.text = InputManager::bindingLabel(gInput.getBinding(a));
                if (pad) p.pads = padButtonsFor(a);
            }
        }
        out.push_back(p);
        i = close + 1;
    }
    return out;
}

// Glyphs: a text line's height and a half, nudged up to sit on it
inline float glyphSize(UIRenderer& ui, int scale) { return std::max(20.0f, ui.textHeight(scale) * 1.5f); }   // small text: still a readable picture

inline float width(UIRenderer& ui, const std::string& s, int scale = 2) {
    float w = 0.0f;
    bool sheet = padglyph::sheet().tex != 0 || !padglyph::sheet().tried;
    for (const Piece& p : parse(s)) {
        if (p.key && !p.pads.empty() && sheet) w += p.pads.size() * (glyphSize(ui, scale) + 2.0f);
        else if (p.key && !p.pads.empty()) {
            for (int b : p.pads) w += ui.textWidth(pad::buttonName(b), scale) + ui.textWidth(" ", scale);
        } else w += ui.textWidth(p.text, scale);
    }
    return w;
}

// Draw at (x, y); keys in keyColor (the text's colour if null).
// Returns the width used.
inline float draw(UIRenderer& ui, const std::string& s, float x, float y, const float* color, int scale = 2,
                  const float* keyColor = nullptr) {
    const float* kc = keyColor ? keyColor : color;
    float x0 = x, g = glyphSize(ui, scale), th = (float)ui.textHeight(scale);
    for (const Piece& p : parse(s)) {
        if (p.key && !p.pads.empty()) {
            for (int b : p.pads) {
                // shaded with the text's alpha, so a fading notice fades its pictures
                if (padglyph::draw(ui, b, x, y + th * 0.5f - g * 0.5f, g, 1.0f, color[3])) x += g + 2.0f;
                else {
                    std::string n = std::string(pad::buttonName(b)) + " ";
                    ui.drawText(n, x, y, kc, scale);
                    x += ui.textWidth(n, scale);
                }
            }
        } else {
            ui.drawText(p.text, x, y, p.key ? kc : color, scale);
            x += ui.textWidth(p.text, scale);
        }
    }
    return x - x0;
}

// Centred on cx
inline float drawCentred(UIRenderer& ui, const std::string& s, float cx, float y, const float* color, int scale = 2,
                         const float* keyColor = nullptr) {
    float w = width(ui, s, scale);
    draw(ui, s, cx - w * 0.5f, y, color, scale, keyColor);
    return w;
}

// The words alone, keys by name (console, logs)
inline std::string plain(const std::string& s) {
    std::string out;
    for (const Piece& p : parse(s)) out += p.text;
    return out;
}

} // namespace keytext
