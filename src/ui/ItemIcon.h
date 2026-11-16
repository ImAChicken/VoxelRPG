#pragma once
#include "UIRenderer.h"
#include "../game/ItemRegistry.h"
#include "../voxel/VoxelRegistry.h"
#include "../render/ResourcePack.h"
#include <glm/glm.hpp>
#include <algorithm>

// ══════════════════════════════════════════════════════════
// Item icons for every menu (HUD hotbar, inventory, dev menu)
// ══════════════════════════════════════════════════════════
//   blocks    a little cube in the block's own textures from
//             the world atlas (top, and two shaded sides)
//   sprites   flowers, reeds: their flat picture
//   others    a coloured square with a letter (T tool, W weapon,
//             X both, A armour, D dev) until items get icons
//
// main.cpp points gIconAtlas at the world atlas every frame (it
// changes when a resource pack is reloaded).
struct IconAtlas {
    GLuint tex = 0;
    float  tileU = 0.0f, tileV = 0.0f;     // one tile in UVs
};
inline IconAtlas gIconAtlas;

inline const char* itemMark(const ItemDef& d) {
    return d.is(IC_DEV) ? "D" : d.canFight() && d.is(IC_TOOL) ? "X" : d.is(IC_WEAPON) ? "W"
         : d.is(IC_TOOL) ? "T" : d.is(IC_WORN) ? "A" : nullptr;
}

// Draw an item's icon in the square (x, y, s). alpha 0-255.
// Returns false if it fell back to the coloured square.
inline bool drawItemIcon(UIRenderer& ui, const ItemDef& d, float x, float y, float s,
                         float alpha = 255.0f, const char* markOverride = nullptr) {
    if (d.is(IC_BLOCK) && d.blockID && gIconAtlas.tex && gIconAtlas.tileU > 0.0f) {
        const VoxelTypeDef& b = gRegistry.get(d.blockID);
        FaceUV f = gResourcePacks.getFaceUV(b.name);
        const float tu = gIconAtlas.tileU, tv = gIconAtlas.tileV;
        auto tile = [&](int tx, int ty, int w, int h, glm::vec2 uv[4]) {
            float u0 = tx * tu, v0 = ty * tv, u1 = (tx + w) * tu, v1 = (ty + h) * tv;
            uv[0] = { u0, v0 }; uv[1] = { u1, v0 }; uv[2] = { u1, v1 }; uv[3] = { u0, v1 };
        };
        glm::vec2 uv[4];
        if (b.sprite) {
            // the flat picture, as tall as it is in the world
            int w = std::max(1, f.spanW), h = std::max(1, f.spanH);
            float ph = s, pw = s * (float)w / (float)h;
            if (pw > s) { ph = s * (float)h / (float)w; pw = s; }
            float px = x + (s - pw) * 0.5f, py = y + (s - ph);
            glm::vec2 p[4] = { { px, py }, { px + pw, py }, { px + pw, py + ph }, { px, py + ph } };
            tile(f.sideX, f.sideY, w, h, uv);
            ui.drawTexturedQuad(gIconAtlas.tex, p, uv, 1.0f, alpha);
            return true;
        }
        // a cube seen from above: top, left side, right side
        float cx = x + s * 0.5f;
        glm::vec2 T(cx, y + s * 0.04f), R(x + s * 0.94f, y + s * 0.27f), B(cx, y + s * 0.50f),
                  L(x + s * 0.06f, y + s * 0.27f), BL(x + s * 0.06f, y + s * 0.73f),
                  BB(cx, y + s * 0.96f), BR(x + s * 0.94f, y + s * 0.73f);
        glm::vec2 top[4] = { T, R, B, L };
        tile(f.topX, f.topY, 1, 1, uv);
        ui.drawTexturedQuad(gIconAtlas.tex, top, uv, 1.0f, alpha);
        glm::vec2 left[4] = { L, B, BB, BL };
        tile(f.sideX, f.sideY, 1, 1, uv);
        ui.drawTexturedQuad(gIconAtlas.tex, left, uv, 0.78f, alpha);
        glm::vec2 right[4] = { B, R, BR, BB };
        ui.drawTexturedQuad(gIconAtlas.tex, right, uv, 0.60f, alpha);
        return true;
    }
    // the stand-in: a coloured square with a letter
    float c[4] = { d.color[0] * 255.0f, d.color[1] * 255.0f, d.color[2] * 255.0f, alpha };
    float edge[4] = { 0, 0, 0, 200.0f * alpha / 255.0f };
    ui.drawRect(x, y, s, s, c);
    ui.drawBorder(x, y, s, s, edge, 1.0f);
    const char* mark = markOverride ? markOverride : itemMark(d);
    if (mark) {
        float ink[4] = { 10, 10, 10, alpha };
        ui.drawTextCentered(mark, x, y, s, s, ink, s >= 26 ? 2 : 1);
    }
    return false;
}
