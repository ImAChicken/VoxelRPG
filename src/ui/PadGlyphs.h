#pragma once
#include "UIRenderer.h"
#include "../input/InputManager.h"
#include "../input/PadLayout.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include <string>

// ══════════════════════════════════════════════════════════
// PadGlyphs — pictures of controller buttons
// ══════════════════════════════════════════════════════════
// Switch style: each picture shows WHERE the button is (the
// diamond of face buttons with one filled in, the D-pad with one
// arm, the shoulder meant), so it reads right on any controller,
// whatever it calls its buttons. data/ui/pad_glyphs.png, made by
// tools/gen_pad_glyphs.py: one CELL square per picture in
// pad::Button order, then the two sticks. Swap in drawn art any
// time (same cells, same order).
//
// keyOrPad() is what the HUD and menus use: the button's picture
// when the controller was used last, else the key bound to the
// same action, as text.
namespace padglyph {

constexpr int CELL = 48, COLS = 8;
enum Extra : int { LStick = pad::COUNT, RStick, GLYPHS };

struct Sheet {
    GLuint tex = 0;
    int    w = 0, h = 0;
    bool   tried = false;
};
inline Sheet& sheet() { static Sheet s; return s; }

// Load it (once; GL must be up). False: no file, text is used instead
inline bool load(const char* path = "data/ui/pad_glyphs.png") {
    Sheet& s = sheet();
    if (s.tried) return s.tex != 0;
    s.tried = true;
    int ch = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path, &s.w, &s.h, &ch, 4);
    if (!data) { std::cerr << "no controller button pictures: " << path << " (names are shown)\n"; return false; }
    glGenTextures(1, &s.tex);
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.w, s.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return true;
}

// Draw picture 'which' (a pad::Button, or LStick / RStick) in the
// square (x, y, size). shade < 1 dims it. False if there's no sheet.
inline bool draw(UIRenderer& ui, int which, float x, float y, float size, float shade = 1.0f, float alpha = 255.0f) {
    Sheet& s = sheet();
    if (!s.tried) load();
    if (!s.tex || which < 0 || which >= GLYPHS) return false;
    int cx = which % COLS, cy = which / COLS;
    if ((cy + 1) * CELL > s.h || (cx + 1) * CELL > s.w) return false;
    float u0 = (float)(cx * CELL) / s.w, v0 = (float)(cy * CELL) / s.h;
    float u1 = (float)((cx + 1) * CELL) / s.w, v1 = (float)((cy + 1) * CELL) / s.h;
    glm::vec2 p[4] = { { x, y }, { x + size, y }, { x + size, y + size }, { x, y + size } };
    glm::vec2 uv[4] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
    ui.drawTexturedQuad(s.tex, p, uv, shade, alpha);
    return true;
}

// The button for 'action' in this layer (the pad's current layout,
// input/PadLayout.h), or -1
inline int buttonFor(InputAction action, pad::Layer layer) {
    for (const pad::Map& m : pad::layout())
        if (m.action == action && pad::inLayer(m.layer, layer, m.button)) return (int)m.button;
    for (const pad::Map& m : pad::layout())
        if (m.action == action) return (int)m.button;
    return -1;
}

// A button's label: its picture (pad last used) or the key's name.
// Draws at (x, y), 'h' tall; returns the width used. 'padButton'
// < 0: looked up from the action.
inline float keyOrPad(UIRenderer& ui, int padButton, InputAction keyAction, float x, float y, float h,
                      const float* textColor, int textScale = 1) {
    if (gInput.padLastUsed()) {
        int b = padButton >= 0 ? padButton : buttonFor(keyAction, gInput.layerNow());
        if (b >= 0 && draw(ui, b, x, y + 4.0f - h * 0.75f, h * 1.5f)) return h * 1.5f;   // centred on a line of text
        std::string n = b >= 0 ? pad::buttonName(b) : "?";
        ui.drawText(n, x, y, textColor, textScale);
        return (float)ui.textWidth(n, textScale);
    }
    std::string k = InputManager::bindingLabel(gInput.getBinding(keyAction));
    ui.drawText(k, x, y, textColor, textScale);
    return (float)ui.textWidth(k, textScale);
}
// Its width only (for laying out before drawing)
inline float keyOrPadWidth(UIRenderer& ui, InputAction keyAction, float h, int textScale = 1) {
    if (gInput.padLastUsed() && sheet().tex) return h * 1.5f;
    if (gInput.padLastUsed()) return (float)ui.textWidth("LB", textScale);
    return (float)ui.textWidth(InputManager::bindingLabel(gInput.getBinding(keyAction)), textScale);
}

} // namespace padglyph
