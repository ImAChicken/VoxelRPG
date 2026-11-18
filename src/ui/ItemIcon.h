#pragma once
#include "UIRenderer.h"
#include "../render/IconRaster.h"
#include "../character/HeldItemModel.h"
#include <unordered_map>
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
//   models    tools, weapons and worn gear: their own model drawn
//             once at load (render/IconRaster.h) into one sheet
//             (gItemIcons: buildItemIcons, again on F6)
//   others    a coloured square with a letter (T tool, W weapon,
//             X both, A armour, D dev)
//
// main.cpp points gIconAtlas at the world atlas every frame (it
// changes when a resource pack is reloaded).
struct IconAtlas {
    GLuint tex = 0;
    float  tileU = 0.0f, tileV = 0.0f;     // one tile in UVs
};
inline IconAtlas gIconAtlas;

// Pictures of items drawn from their models, in one texture
struct ItemIconSheet {
    GLuint tex = 0;
    int    cell = 64, cols = 16, rows = 0;
    std::unordered_map<uint16_t, int> slot;      // item id → cell
};
inline ItemIconSheet gItemIcons;

// The picture of one item from its model (held model, or its gear
// on a reference body); empty if it has none
inline iconraster::Image itemModelIcon(const ItemDef& d, const CharacterModel& ref, int size) {
    if (!d.heldModel.empty())
        if (const HeldModelDef* h = gHeldModels.find(d.heldModel))
            return iconraster::draw(h->meshLocal(HairMesh::Voxels), iconraster::heldView(), size, 0.06f);
    if (!d.gear.shape.empty())
        return iconraster::draw(iconraster::gearAtRest(ref, d.gear), iconraster::gearView(), size, 0.1f);
    return {};
}

// Draw every item that has a model into the sheet (GL context
// current). Call after items and held models load, and on F6.
inline void buildItemIcons() {
    ItemIconSheet& sh = gItemIcons;
    sh.slot.clear();
    CharacterModel ref = CharacterModel::build(Appearance());
    std::vector<std::pair<uint16_t, iconraster::Image>> pics;
    for (uint16_t id : gItems.inCategory(IC_TOOL | IC_WEAPON | IC_SPELLBOOK | IC_SHIELD | IC_DEV | IC_WORN)) {
        iconraster::Image im = itemModelIcon(gItems.get(id), ref, sh.cell);
        if (!im.empty() && im.coverage() > 0) pics.push_back({ id, std::move(im) });
    }
    sh.rows = std::max(1, ((int)pics.size() + sh.cols - 1) / sh.cols);
    const int W = sh.cols * sh.cell, H = sh.rows * sh.cell;
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    for (size_t n = 0; n < pics.size(); n++) {
        int cx = (int)(n % sh.cols) * sh.cell, cy = (int)(n / sh.cols) * sh.cell;
        const iconraster::Image& im = pics[n].second;
        for (int y = 0; y < sh.cell; y++)
            std::copy_n(&im.rgba[(size_t)y * sh.cell * 4], sh.cell * 4, &px[((size_t)(cy + y) * W + cx) * 4]);
        sh.slot[pics[n].first] = (int)n;
    }
    if (!sh.tex) glGenTextures(1, &sh.tex);
    glBindTexture(GL_TEXTURE_2D, sh.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

inline const char* itemMark(const ItemDef& d) {
    return d.is(IC_DEV) ? "D" : d.canFight() && d.is(IC_TOOL) ? "X" : d.is(IC_SPELLBOOK) ? "S" : d.shield ? "H" : d.is(IC_WEAPON) ? "W"
         : d.is(IC_TOOL) ? "T" : d.is(IC_WORN) ? "A" : nullptr;
}

// Draw an item's icon in the square (x, y, s). alpha 0-255.
// Returns false if it fell back to the coloured square.
inline bool drawItemIcon(UIRenderer& ui, const ItemDef& d, float x, float y, float s,
                         float alpha = 255.0f, const char* markOverride = nullptr) {
    if (gItemIcons.tex) {
        auto it = gItemIcons.slot.find(d.id);
        if (it != gItemIcons.slot.end()) {
            const ItemIconSheet& sh = gItemIcons;
            float u0 = (float)(it->second % sh.cols) / sh.cols, v0 = (float)(it->second / sh.cols) / sh.rows;
            float u1 = u0 + 1.0f / sh.cols, v1 = v0 + 1.0f / sh.rows;
            glm::vec2 p[4] = { { x, y }, { x + s, y }, { x + s, y + s }, { x, y + s } };
            glm::vec2 uv[4] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
            ui.drawTexturedQuad(sh.tex, p, uv, 1.0f, alpha);
            return true;
        }
    }
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
