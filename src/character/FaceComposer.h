#pragma once
#include "Appearance.h"
#include "../render/ResourcePack.h"
#include "../vendor/stb_image.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// FaceComposer — paints a face from the resource pack's parts
// ══════════════════════════════════════════════════════════
// Faces are small painted images (80 × 80), put together from
// parts in <pack>/characters/:
//
//   face_eyes.png    eye shapes, lashes, whites, highlights
//                    row = eye style, column = state × side
//                    (open, surprised, closed, happy) × (L, R)
//   face_iris.png    same layout: where the eye colour goes,
//                    as a brightness (grey 127 = the colour
//                    itself, brighter = lighter)
//   face_brows.png   row = eye style; columns: normal,
//                    surprised, smug — tinted by hair colour
//   face_mouths.png  row 0 = classic styles, row 1 = anime and
//                    storybook styles; column = expression
//   face_extras.png  nose, light blush, classic blush
//
// A resource pack can repaint any of these; the layout is all
// the game needs. "L" here is the left of the picture (the
// character's right eye).
class FaceComposer {
public:
    static constexpr int R = 80;

    bool load() {
        bool ok = true;
        ok &= read("characters/face_eyes.png", eyes);
        ok &= read("characters/face_iris.png", iris);
        ok &= read("characters/face_brows.png", brows);
        ok &= read("characters/face_mouths.png", mouths);
        ok &= read("characters/face_extras.png", extras);
        loaded = ok;
        if (!ok) std::cerr << "Face parts missing from the resource pack — faces will be blank\n";
        return ok;
    }
    bool isLoaded() const { return loaded; }

    // blink = both eyes closed. out: R*R*4 bytes, RGBA.
    void compose(const Appearance& a, bool blink, std::vector<uint8_t>& out) const {
        out.assign((size_t)R * R * 4, 0);
        if (!loaded) return;
        const int st = a.eyeStyle, ex = a.expression;
        const bool modern = st >= 4;
        // blush, nose, mouth
        over(out, extras, modern ? 1 : 2, 0);
        if (modern) over(out, extras, 0, 0);
        over(out, mouths, ex, modern ? 1 : 0);
        // brows in the hair colour
        overTint(out, brows, ex == 3 ? 1 : ex == 4 ? 2 : 0, st, glm::vec3(a.hair));
        // eyes: shapes, then the iris in the eye colour
        for (int side = 0; side < 2; side++) {
            int state = blink ? 2 : ex == 3 ? 1 : (ex == 2 && side == 0) ? 3 : 0;
            int col = state * 2 + side;
            over(out, eyes, col, st);
            overTint(out, iris, col, st, glm::vec3(a.eyes));
        }
    }

private:
    struct Img { int w = 0, h = 0; std::vector<uint8_t> px; };
    Img eyes, iris, brows, mouths, extras;
    bool loaded = false;

    static bool read(const std::string& rel, Img& img) {
        auto path = gResourcePacks.findFile(rel);
        int w, h, n;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* d = stbi_load(path.string().c_str(), &w, &h, &n, 4);
        if (!d) { std::cerr << "Cannot load " << path << "\n"; return false; }
        img.w = w; img.h = h;
        img.px.assign(d, d + (size_t)w * h * 4);
        stbi_image_free(d);
        return true;
    }

    // Straight-alpha "over" of one atlas cell onto the face
    static void blend(std::vector<uint8_t>& out, size_t o, const uint8_t* s, float sr, float sg, float sb) {
        float sa = s[3] / 255.0f;
        if (sa <= 0.0f) return;
        float da = out[o + 3] / 255.0f, oa = sa + da * (1 - sa);
        auto mix = [&](float sc, uint8_t dc) { return (uint8_t)std::clamp((sc * sa + dc * da * (1 - sa)) / oa, 0.0f, 255.0f); };
        out[o] = mix(sr, out[o]); out[o + 1] = mix(sg, out[o + 1]); out[o + 2] = mix(sb, out[o + 2]);
        out[o + 3] = (uint8_t)std::clamp(oa * 255.0f, 0.0f, 255.0f);
    }
    static const uint8_t* cell(const Img& img, int col, int row, int x, int y) {
        int px = col * R + x, py = row * R + y;
        if (px >= img.w || py >= img.h) return nullptr;
        return &img.px[((size_t)py * img.w + px) * 4];
    }
    static void over(std::vector<uint8_t>& out, const Img& img, int col, int row) {
        for (int y = 0; y < R; y++) for (int x = 0; x < R; x++)
            if (const uint8_t* s = cell(img, col, row, x, y))
                blend(out, ((size_t)y * R + x) * 4, s, s[0], s[1], s[2]);
    }
    // Brightness map × colour (grey 127.5 = exactly the colour)
    static void overTint(std::vector<uint8_t>& out, const Img& img, int col, int row, glm::vec3 c) {
        for (int y = 0; y < R; y++) for (int x = 0; x < R; x++)
            if (const uint8_t* s = cell(img, col, row, x, y)) {
                float k = s[0] / 127.5f;
                blend(out, ((size_t)y * R + x) * 4, s, std::min(255.0f, c.r * k), std::min(255.0f, c.g * k), std::min(255.0f, c.b * k));
            }
    }
};

inline FaceComposer gFaces;
