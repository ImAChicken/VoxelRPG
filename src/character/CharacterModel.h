#pragma once
#include "Appearance.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

// ══════════════════════════════════════════════════════════
// CharacterModel — a stylised low-poly human
// ══════════════════════════════════════════════════════════
// Built entirely from an Appearance, nothing hand-modelled:
//
//   body      faceted tubes ("lofts": rings of points joined
//             up) for every body part, shaped by build, height
//             and the sliders, with the outfit's colours and
//             extra pieces (sleeves, skirts, bibs, belts …)
//   hair      a voxel grid filled from simple shapes (cones,
//             a cap, slabs), then turned into angled shards
//             by surface nets — or left as cubes
//   extras    hats, ears, horns, headscarf: their OWN voxel
//             grid and mesh, so they never fuse with the hair
//   face      not geometry: a painted card on the front of the
//             head (FaceComposer.h)
//
// Parts hang on the same skeleton the animator drives
// (CharacterAnimator.h). Units are inches; the renderer scales
// by 1/4 into world voxels. Model space: +Y up, +Z forward
// (the way the body faces), +X to the body's left.

enum Bone : int {
    B_ROOT,                       // pelvis (hip height)
    B_SPINE,                      // torso, from the top of the pelvis
    B_HEAD,                       // from the top of the neck
    B_SHOULDER_L, B_ELBOW_L, B_WRIST_L,
    B_SHOULDER_R, B_ELBOW_R, B_WRIST_R,
    B_HIP_L, B_KNEE_L, B_ANKLE_L,
    B_HIP_R, B_KNEE_R, B_ANKLE_R,
    BONE_COUNT
};

enum class HairMesh : int { Voxels, Shards, SoftShards };

// One vertex: position (inches from its joint), flat face
// normal, colour
struct PartVertex {
    float   x, y, z;
    int8_t  nx, ny, nz, pad;
    uint8_t r, g, b, a;
};

struct CharacterModel {
    struct BoneDef {
        int       parent = -1;
        glm::vec3 offset = {0, 0, 0};   // joint position in the parent's frame (inches)
    };
    std::array<BoneDef, BONE_COUNT>                 bones;
    std::array<std::vector<PartVertex>, BONE_COUNT> parts;   // one mesh per bone

    float heightIn   = 68.0f;   // standing height (inches)
    float rootHeight = 34.0f;   // root joint above the soles when standing straight
    float footHeight = 2.5f;    // ankle joint above the sole
    float thighLen   = 15.0f, shinLen = 15.5f;

    // The painted face card, in the head bone's frame
    glm::vec3 faceCenter = {0, 4, 4.7f};
    float     faceSize   = 8.0f;

    size_t triangleCount() const {
        size_t n = 0;
        for (auto& p : parts) n += p.size() / 3;
        return n;
    }

    // ══════════════════════════════════════════════════════
    static CharacterModel build(const Appearance& c, HairMesh hairMode = HairMesh::Shards) {
        CharacterModel m;
        using V3 = glm::vec3;
        const float H = c.heightVoxels * 4.0f;
        const float s = H / 67.4f;      // the chain below adds up to 67.4 in at s = 1
        const float widthF = std::array<float, 3>{0.86f, 1.0f, 1.16f}[c.build];
        const float limbF  = std::array<float, 3>{0.8f, 1.0f, 1.25f}[c.build] * (1 + 0.18f * c.limbs);
        const float depthF = std::array<float, 3>{0.88f, 1.0f, 1.15f}[c.build];
        const float headF  = 1 + 0.12f * c.head;
        const int   FS     = c.face;

        float chain[7] = { 2.5f, 15.5f, 15.0f, 5.0f, 15.5f, 2.2f, 12.3f };
        for (float& v : chain) v *= s;
        chain[6] *= headF * (FS == 4 ? 1.1f : 1.0f);
        const float footH = chain[0], shinL = chain[1], thighL = chain[2], pelvH = chain[3],
                    torsoH = chain[4], neckH = chain[5], headH = chain[6];
        const float hipW = (12 * widthF + 3.2f * c.hips) * s, pelvD = (7.2f + 0.8f * c.hips) * s * depthF;
        const float shoulderW = (13.5f * widthF + 2.2f * c.shoulders) * s, waistW = 10 * widthF * s, torsoD = 7.2f * s * depthF;
        const float uArmL = 11 * s, fArmL = 10 * s, handL = 5 * s, uArmT = 3.3f * s * limbF, fArmT = 2.9f * s * limbF;
        const float thighT = 5.2f * s * limbF * (1 + 0.12f * c.hips), shinT = 3.9f * s * limbF;
        const float hw = 9.4f * s * headF * (FS == 4 ? 0.92f : FS == 2 ? 1.04f : 1.0f), hh = headH, hd = 10 * s * headF;
        const float chestR = c.chest * 3.2f * s;

        // the hip joints sit a little above the root, so the
        // root is that much lower to keep the soles on the ground
        const float hipUp = 0.6f * s;
        m.heightIn   = footH + shinL + thighL - hipUp + pelvH + torsoH + neckH + headH;
        m.rootHeight = footH + shinL + thighL - hipUp;
        m.footHeight = footH;
        m.thighLen   = thighL;
        m.shinLen    = shinL;
        m.faceSize   = hw * 0.92f;
        m.faceCenter = { 0.0f, hh * 0.38f, hd * 0.47f + 0.08f };

        // ── Colours (0..255) ──────────────────────────────
        const V3 skin(c.skin), top(c.topColor), bot(c.bottomColor), hairC(c.hair);
        const V3 LINEN = col("#e6dcc6"), LEATHER = col("#5f4636"), WOOD = col("#9a7550"),
                 OFFWHITE = col("#ece7de"), BRASS = col("#bfa466");
        const int T = c.top, B = c.bottom;
        // what each outfit piece puts where (skin where none)
        const V3 belly = T == 2 ? skin : T == 4 ? LINEN : top;
        const V3 chestC = (T == 4 || T == 5) ? LINEN : top;
        const V3 upper = (T == 0 || T == 5) ? skin : T == 4 ? LINEN : top;
        const V3 fore  = (T == 1 || T == 2 || T == 3) ? top : skin;
        const bool hasCuff = T == 1 || T == 2 || T == 3;
        const V3 cuff = T == 1 ? shade(top, 0.8f) : T == 2 ? OFFWHITE : shade(top, 0.85f);
        const V3 legs = (B == 2 || B == 3) ? bot : skin;

        // ── Skeleton ──────────────────────────────────────
        auto bone = [&](Bone b, int parent, V3 off) { m.bones[b] = { parent, off }; };
        bone(B_ROOT,  -1,     {0, m.rootHeight, 0});
        bone(B_SPINE, B_ROOT, {0, pelvH, 0});
        bone(B_HEAD,  B_SPINE,{0, torsoH + neckH, 0.3f * s});
        for (int sx : { -1, 1 }) {
            bool L = sx > 0;
            bone(L ? B_SHOULDER_L : B_SHOULDER_R, B_SPINE, { sx * (shoulderW * 0.5f + uArmT * 0.2f), torsoH * 0.9f, 0.2f * s });
            bone(L ? B_ELBOW_L : B_ELBOW_R, L ? B_SHOULDER_L : B_SHOULDER_R, {0, -uArmL, 0});
            bone(L ? B_WRIST_L : B_WRIST_R, L ? B_ELBOW_L : B_ELBOW_R, {0, -fArmL, 0});
            bone(L ? B_HIP_L : B_HIP_R, B_ROOT, { sx * hipW * 0.24f, hipUp, 0 });
            bone(L ? B_KNEE_L : B_KNEE_R, L ? B_HIP_L : B_HIP_R, {0, -thighL, 0});
            bone(L ? B_ANKLE_L : B_ANKLE_R, L ? B_KNEE_L : B_KNEE_R, {0, -shinL, 0});
        }

        auto& root = m.parts[B_ROOT];
        auto& spine = m.parts[B_SPINE];
        auto& head = m.parts[B_HEAD];

        // pelvis
        loft(root, {{0, hipW * 0.46f, pelvD * 0.5f}, {pelvH, waistW * 0.5f, torsoD * 0.46f}}, 8, B == 0 ? skin : bot);
        // torso: belly and chest, so a cropped top can show the midriff
        const float bellyTop = torsoH * 0.42f;
        loft(spine, {{0, waistW * 0.5f, torsoD * 0.46f}, {bellyTop, waistW * 0.52f, torsoD * 0.48f}}, 8, belly);
        // the bust is part of the chest's shape: a forward bulge in its rings
        auto bust = [&](float k) {
            return std::vector<Ring>{
                {bellyTop, waistW * 0.52f * k, torsoD * 0.48f * k},
                {torsoH * 0.6f, (waistW * 0.52f + (shoulderW * 0.46f - waistW * 0.52f) * 0.6f + chestR * 0.15f) * k,
                 (torsoD * 0.5f + chestR * 0.35f) * k, 0, 0.2f * s + chestR * 0.35f},
                {torsoH * 0.76f, (shoulderW * 0.46f + chestR * 0.1f) * k, (torsoD * 0.52f + chestR * 0.45f) * k, 0, 0.25f * s + chestR * 0.45f},
                {torsoH * 0.97f, shoulderW * 0.46f * k, torsoD * 0.42f * k},
            };
        };
        { auto r = bust(1.0f); r.push_back({torsoH, 2.0f * s, 2.0f * s}); loft(spine, r, 8, chestC); }
        if (T == 2)   // crop jacket hem band
            loft(spine, {{bellyTop - 0.6f * s, waistW * 0.56f, torsoD * 0.53f}, {bellyTop + 1.2f * s, waistW * 0.58f, torsoD * 0.55f}}, 8, OFFWHITE);
        if (T == 1)   // hoodie: hood folded behind the neck
            loft(spine, {{torsoH * 0.9f, 3.4f * s, 2.4f * s, 0, -2.6f * s}, {torsoH + 1.5f * s, 3.0f * s, 2.0f * s, 0, -3.2f * s}}, 6, shade(top, 0.85f));
        if (T == 3) { // tunic: hem over the hips, belt, buckle, laced neck
            loft(root, {{pelvH + 0.4f, waistW * 0.56f, torsoD * 0.52f}, {-thighL * 0.4f, hipW * 0.7f, pelvD * 0.78f}}, 10, top, 0.05f);
            loft(spine, {{-0.4f * s, waistW * 0.58f, torsoD * 0.55f}, {1.1f * s, waistW * 0.58f, torsoD * 0.55f}}, 10, LEATHER);
            box(spine, {1.6f * s, 1.3f * s, 0.5f * s}, {0, 0.35f * s, torsoD * 0.56f}, BRASS);
            box(spine, {0.35f * s, 3.2f * s, 0.3f * s}, {0, torsoH * 0.86f, torsoD * 0.5f + 0.2f * s}, LEATHER);
        }
        if (T == 4) { // vest open over a linen shirt
            auto r = bust(1.07f); r.insert(r.begin(), Ring{-0.2f * s, waistW * 0.57f, torsoD * 0.54f});
            loft(spine, r, 8, top);
            float fz = torsoD * 0.6f + 0.25f * s + chestR * 0.8f;
            box(spine, {2.2f * s, torsoH * 0.85f, 0.4f * s}, {0, torsoH * 0.44f, fz}, LINEN);
            for (int i = 0; i < 3; i++) box(spine, {0.6f * s, 0.6f * s, 0.4f * s}, {1.4f * s, torsoH * (0.2f + i * 0.22f), fz + 0.1f}, BRASS);
        }
        if (T == 5) { // bodice laced over a linen blouse
            auto b = bust(1.06f);
            loft(spine, {{-0.2f * s, waistW * 0.56f, torsoD * 0.53f}, b[0], b[1]}, 8, top, 0, true, false);
            float fz = torsoD * 0.56f + 0.15f * s;
            for (int i = 0; i < 4; i++) box(spine, {2.6f * s, 0.32f * s, 0.25f * s}, {0, torsoH * (0.08f + i * 0.13f), fz}, LINEN);
        }
        loft(spine, {{torsoH - 0.5f, 1.6f * s, 1.6f * s}, {torsoH + neckH + 0.8f, 1.5f * s, 1.5f * s}}, 6, skin);   // neck

        if (B == 3) { // overalls: bib, pocket, straps, buttons
            float bibZ = torsoD * 0.5f + 0.25f * s + chestR * 0.85f;
            box(spine, {waistW * 0.78f, torsoH * 0.66f, 0.7f * s}, {0, torsoH * 0.33f, bibZ - 0.2f * s}, bot);
            box(spine, {waistW * 0.36f, torsoH * 0.18f, 0.4f * s}, {0, torsoH * 0.42f, bibZ + 0.25f * s}, shade(bot, 0.85f));
            for (int sx : { -1, 1 }) {
                float x0 = sx * waistW * 0.3f;
                box(spine, {1.1f * s, torsoH * 0.32f, 0.4f * s}, {x0, torsoH * 0.8f, bibZ - 0.4f * s}, bot);
                box(spine, {1.1f * s, torsoH * 0.95f, 0.4f * s}, {x0, torsoH * 0.5f, -torsoD * 0.52f}, bot);
                box(spine, {1.1f * s, 0.4f * s, torsoD * 1.05f}, {x0, torsoH * 0.97f, 0}, bot);
                box(spine, {0.8f * s, 0.8f * s, 0.4f * s}, {x0, torsoH * 0.64f, bibZ + 0.25f * s}, BRASS);
            }
        }

        // head: chin and jaw by face shape
        static const float SHAPES[5][5] = {
            {0.16f, 0.00f, 0.43f, 0.26f, 0.52f},   // Soft: wide cheeks, small chin
            {0.10f, 0.00f, 0.38f, 0.24f, 0.50f},   // Pointed: sharp V
            {0.26f, 0.04f, 0.48f, 0.24f, 0.52f},   // Round: full lower face
            {0.32f, 0.03f, 0.50f, 0.20f, 0.51f},   // Square: strong jaw
            {0.14f, 0.00f, 0.38f, 0.22f, 0.48f},   // Long: narrow and tall
        };
        const float* F = SHAPES[FS];
        loft(head, {{hh * F[1], hw * F[0], hd * 0.18f, 0, hd * 0.2f}, {hh * F[3], hw * F[2], hd * 0.43f, 0, hd * 0.06f},
                    {hh * 0.48f, hw * F[4], hd * 0.5f}, {hh * 0.78f, hw * 0.52f, hd * 0.52f}, {hh * 0.98f, hw * 0.38f, hd * 0.42f}}, 8, skin);
        for (int sx : { -1, 1 })   // ears
            loft(head, {{hh * 0.42f, 0.7f * s, 1.1f * s, sx * hw * 0.5f}, {hh * 0.6f, 0.5f * s, 0.9f * s, sx * hw * 0.52f}}, 5, shade(skin, 0.96f));

        // hair and extras: two separate voxel grids and meshes
        buildHair(c, hw, hh, hd, hairMode, head);

        // arms
        for (int sx : { -1, 1 }) {
            bool L = sx > 0;
            auto& sh = m.parts[L ? B_SHOULDER_L : B_SHOULDER_R];
            auto& el = m.parts[L ? B_ELBOW_L : B_ELBOW_R];
            auto& wr = m.parts[L ? B_WRIST_L : B_WRIST_R];
            loft(sh, {{0, uArmT * 0.62f, uArmT * 0.62f}, {-uArmL, uArmT * 0.48f, uArmT * 0.48f}}, 6, upper);
            if (T == 0)   // short sleeve shell, a little bigger than the arm
                loft(sh, {{0.6f, uArmT * 0.75f, uArmT * 0.75f}, {-uArmL * 0.45f, uArmT * 0.72f, uArmT * 0.72f}}, 6, top);
            if (T == 5)   // gathered puff sleeve
                loft(sh, {{0.8f, uArmT * 0.6f, uArmT * 0.6f}, {-uArmL * 0.18f, uArmT * 1.05f, uArmT * 1.0f}, {-uArmL * 0.42f, uArmT * 0.7f, uArmT * 0.7f}}, 7, LINEN);
            if (T == 4)   // shirt sleeve rolled to the elbow
                loft(sh, {{-uArmL * 0.78f, uArmT * 0.72f, uArmT * 0.72f}, {-uArmL - 0.3f, uArmT * 0.68f, uArmT * 0.68f}}, 6, shade(LINEN, 0.9f));
            loft(el, {{0, fArmT * 0.55f, fArmT * 0.55f}, {-fArmL, fArmT * 0.45f, fArmT * 0.45f}}, 6, fore);
            if (hasCuff)
                loft(el, {{-fArmL * 0.7f, fArmT * 0.62f, fArmT * 0.62f}, {-fArmL - 0.4f, fArmT * 0.72f, fArmT * 0.72f}}, 6, cuff);
            // hand: a flat mitten with a thumb
            loft(wr, {{0, 1.3f * s, 0.75f * s}, {-handL * 0.7f, 1.45f * s, 0.7f * s}, {-handL, 0.9f * s, 0.5f * s}}, 6, skin);
            loft(wr, {{-0.6f * s, 0.4f * s, 0.4f * s, sx * -0.5f * s, 0.9f * s}, {-2.4f * s, 0.3f * s, 0.3f * s, sx * -0.4f * s, 1.4f * s}}, 5, skin);
        }

        // legs and shoes
        V3 shoeUp, shoeSole;
        if (c.shoes == 0) { shoeUp = OFFWHITE; shoeSole = top; }
        else if (c.shoes == 1) { shoeUp = LEATHER; shoeSole = col("#2a1d18"); }
        else { shoeUp = WOOD; shoeSole = shade(WOOD, 0.7f); }
        for (int sx : { -1, 1 }) {
            bool L = sx > 0;
            auto& hp = m.parts[L ? B_HIP_L : B_HIP_R];
            auto& kn = m.parts[L ? B_KNEE_L : B_KNEE_R];
            auto& an = m.parts[L ? B_ANKLE_L : B_ANKLE_R];
            loft(hp, {{0, thighT * 0.5f, thighT * 0.52f}, {-thighL, shinT * 0.55f, shinT * 0.57f}}, 6, legs);
            if (B == 1) loft(hp, {{0.5f, thighT * 0.62f, thighT * 0.64f}, {-thighL * 0.38f, thighT * 0.6f, thighT * 0.62f}}, 6, bot);
            loft(kn, {{0, shinT * 0.55f, shinT * 0.57f}, {-shinL * 0.55f, shinT * 0.52f, shinT * 0.6f, 0, -0.3f * s}, {-shinL, shinT * 0.36f, shinT * 0.38f}}, 6, legs);
            if (B == 3) loft(kn, {{-shinL * 0.82f, shinT * 0.6f, shinT * 0.62f}, {-shinL + 0.6f, shinT * 0.58f, shinT * 0.6f}}, 6, shade(bot, 0.8f));
            if (c.shoes == 1) loft(kn, {{-shinL * 0.48f, shinT * 0.66f, shinT * 0.7f}, {-shinL - 0.2f, shinT * 0.55f, shinT * 0.6f}}, 6, shoeUp);
            float fl = (c.shoes == 2 ? 9.2f : 8.4f) * s, fw = (c.shoes == 2 ? 4.0f : 3.6f) * s;
            box(an, {fw * 1.05f, footH * 0.45f, fl * 1.04f}, {0, -footH + footH * 0.22f, fl * 0.3f}, shoeSole);
            loft(an, {{-footH * 0.55f, fw * 0.5f, fl * 0.5f, 0, fl * 0.3f}, {footH * (c.shoes == 2 ? 0.6f : 0.35f), fw * 0.42f, fl * 0.32f, 0, fl * 0.12f}}, 6, shoeUp);
            if (c.shoes == 2)   // clog toe turns up
                box(an, {fw * 0.7f, footH * 0.5f, fl * 0.22f}, {0, -footH * 0.25f, fl * 0.82f}, shoeUp, -0.35f);
        }

        // skirts
        if (B == 0) {
            loft(root, {{pelvH + 0.6f, waistW * 0.55f, torsoD * 0.53f}, {-thighL * 0.45f, hipW * 0.85f, pelvD * 0.95f}}, 14, bot, 0.1f);
            loft(root, {{pelvH - 0.2f, waistW * 0.57f, torsoD * 0.55f}, {pelvH + 1.0f, waistW * 0.57f, torsoD * 0.55f}}, 10, shade(bot, 0.75f));
        }
        if (B == 4) { // long village skirt with an apron
            float hemY = -(thighL + shinL * 0.82f), rzTop = torsoD * 0.53f, rzHem = pelvD * 1.25f;
            loft(root, {{pelvH + 0.6f, waistW * 0.55f, rzTop}, {-thighL * 0.3f, hipW * 0.78f, pelvD * 0.9f}, {hemY, hipW * 1.05f, rzHem}}, 14, bot, 0.07f);
            loft(root, {{pelvH - 0.2f, waistW * 0.58f, torsoD * 0.56f}, {pelvH + 1.0f, waistW * 0.58f, torsoD * 0.56f}}, 10, LINEN);
            float apronTop = pelvH + 0.2f, apronBot = -thighL * 1.15f, ah = apronTop - apronBot;
            float zTop = rzTop + 0.5f, zBot = pelvD * 1.2f + 1.0f;
            box(root, {hipW * 0.78f, ah, 0.3f * s}, {0, (apronTop + apronBot) / 2, (zTop + zBot) / 2}, LINEN,
                -std::atan2(zBot - zTop, ah));
        }
        return m;
    }

    // A voxel grid filled from a shape and meshed the way the hair
    // is (voxels or shards, per the hair mesh setting). Used for
    // things held in the hands (character/HeldItemModel.h).
    //   role(p)   0 = empty, else an index into colours (0..4)
    //   lo, hi    the box to fill; vox = voxel size (inches)
    static void meshVoxelShape(glm::vec3 lo, glm::vec3 hi, float vox, const std::function<int(glm::vec3)>& role,
                               const glm::vec3* colours, HairMesh mode, std::vector<PartVertex>& out) {
        Grid g(lo, hi, vox);
        g.fill([&](glm::vec3 p) { return role(p); });
        meshGrid(g, colours, mode, out);
    }

private:
    using V3 = glm::vec3;

    struct Ring {
        float y, rx, rz, x = 0, z = 0;
        Ring(float y, float rx, float rz, float x = 0, float z = 0) : y(y), rx(rx), rz(rz), x(x), z(z) {}
    };

    static V3 col(const char* hex) { return V3(look::hex(hex)); }
    static V3 shade(V3 c, float k) { return glm::min(c * k, V3(255.0f)); }

    static void tri(std::vector<PartVertex>& out, V3 a, V3 b, V3 c, V3 colour) {
        V3 n = glm::cross(b - a, c - a);
        float len = glm::length(n);
        if (len < 1e-8f) return;
        n /= len;
        glm::u8vec3 cc(glm::clamp(colour, V3(0.0f), V3(255.0f)));
        for (const V3& p : { a, b, c })
            out.push_back({ p.x, p.y, p.z, (int8_t)std::lround(n.x * 127), (int8_t)std::lround(n.y * 127),
                            (int8_t)std::lround(n.z * 127), 0, cc.r, cc.g, cc.b, 255 });
    }

    // Faceted tube through the rings; angle 0 faces +Z (the
    // front). 'jag' pulls every other point in: pleats.
    static void loft(std::vector<PartVertex>& out, const std::vector<Ring>& rings, int sides, V3 colour,
                     float jag = 0.0f, bool capStart = true, bool capEnd = true) {
        const float phase = 3.14159265f / sides;
        auto pt = [&](const Ring& r, int i) {
            float a = phase + (float)i / sides * 6.2831853f;
            float k = (jag > 0 && (i % 2)) ? 1 - jag : 1;
            return V3(r.x + std::sin(a) * r.rx * k, r.y, r.z + std::cos(a) * r.rz * k);
        };
        for (size_t k = 0; k + 1 < rings.size(); k++) {
            const Ring &r0 = rings[k], &r1 = rings[k + 1];
            bool up = r1.y >= r0.y;
            for (int i = 0; i < sides; i++) {
                V3 a = pt(r0, i), b = pt(r0, i + 1), c = pt(r1, i + 1), d = pt(r1, i);
                if (up) { tri(out, a, b, c, colour); tri(out, a, c, d, colour); }
                else    { tri(out, a, c, b, colour); tri(out, a, d, c, colour); }
            }
        }
        auto cap = [&](const Ring& r, bool topCap) {
            V3 c(r.x, r.y, r.z);
            for (int i = 0; i < sides; i++) {
                V3 a = pt(r, i), b = pt(r, i + 1);
                if (topCap) tri(out, c, a, b, colour); else tri(out, c, b, a, colour);
            }
        };
        bool ascending = rings.back().y >= rings.front().y;
        if (capStart) cap(rings.front(), !ascending);
        if (capEnd)   cap(rings.back(), ascending);
    }

    // A box of size 'size' centred on 'at', optionally tipped
    // forward about X
    static void box(std::vector<PartVertex>& out, V3 size, V3 at, V3 colour, float rotX = 0.0f) {
        static const float C[6][4][3] = {
            {{1,0,0},{1,1,0},{1,1,1},{1,0,1}}, {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},
            {{0,1,0},{0,1,1},{1,1,1},{1,1,0}}, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
            {{1,0,1},{1,1,1},{0,1,1},{0,0,1}}, {{0,0,0},{0,1,0},{1,1,0},{1,0,0}},
        };
        float cs = std::cos(rotX), sn = std::sin(rotX);
        auto P = [&](const float* c) {
            V3 p = (V3(c[0], c[1], c[2]) - 0.5f) * size;
            V3 r(p.x, p.y * cs - p.z * sn, p.y * sn + p.z * cs);
            return r + at;
        };
        for (auto& f : C) {
            V3 a = P(f[0]), b = P(f[1]), c = P(f[2]), d = P(f[3]);
            tri(out, a, b, c, colour); tri(out, a, c, d, colour);
        }
    }

    // ── Voxel grids for hair and extras ───────────────────
    struct Grid {
        V3 min; float v; int n[3];
        std::vector<uint8_t> r;
        Grid(V3 lo, V3 hi, float v) : min(lo), v(v) {
            for (int a = 0; a < 3; a++) n[a] = (int)std::ceil((hi[a] - lo[a]) / v) + 2;
            r.assign((size_t)n[0] * n[1] * n[2], 0);
        }
        size_t idx(int x, int y, int z) const { return ((size_t)z * n[1] + y) * n[0] + x; }
        uint8_t get(int x, int y, int z) const {
            if (x < 0 || y < 0 || z < 0 || x >= n[0] || y >= n[1] || z >= n[2]) return 0;
            return r[idx(x, y, z)];
        }
        V3 centre(int x, int y, int z) const { return min + (V3(x, y, z) - 0.5f) * v; }
        template <class F> void fill(F fn) {
            for (int z = 1; z < n[2] - 1; z++)
                for (int y = 1; y < n[1] - 1; y++)
                    for (int x = 1; x < n[0] - 1; x++)
                        if (int role = fn(centre(x, y, z))) r[idx(x, y, z)] = (uint8_t)role;
        }
        bool empty() const { for (uint8_t b : r) if (b) return false; return true; }
    };

    // tapered cone from a (radius r0) to b (radius r1); sz
    // squashes it front to back
    static bool cone(V3 p, V3 a, V3 b, float r0, float r1, float sz = 1.0f) {
        V3 ab = b - a, ap = p - a;
        float t = std::clamp(glm::dot(ap, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f, 1.0f);
        V3 d = p - (a + ab * t);
        d.z /= sz;
        return glm::length(d) <= r0 + (r1 - r0) * t;
    }

    // like cone(), but cut flat at both ends instead of rounded —
    // for hat crowns, whose rounded bottom would bulge down into
    // the hair
    static bool frustum(V3 p, V3 a, V3 b, float r0, float r1, float sz = 1.0f) {
        V3 ab = b - a;
        float t = glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-9f);
        return t >= 0.0f && t <= 1.0f && cone(p, a, b, r0, r1, sz);
    }

    using Shape = std::function<int(V3)>;   // role at a point (0 = empty)

    static void buildHair(const Appearance& c, float w, float h, float d, HairMesh mode, std::vector<PartVertex>& out) {
        const int s = c.hairStyle, ex = c.extra;
        const float tall = ex == 3 ? 2.15f : 1.6f, wide = ex == 4 ? 1.45f : 1.25f;
        const V3 C(0, 0.56f * h, -0.02f * d), R(0.54f * w, 0.52f * h, 0.55f * d);
        auto ell = [=](V3 p) { return glm::length((p - C) / R); };
        auto P = [=](float x, float y, float z) { return V3(x * w, y * h, z * d); };
        uint32_t seed = 7 + s * 31;
        auto rand = [&]() { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0f; };
        // The base cap: hairline high at the front, low at the back.
        //   thick     how far the cap stands off the scalp
        //   backLow   lowest point at the back (fraction of head height)
        //   sideLow   lowest point over the ears
        //   floor     nothing below this anywhere (undercut sides)
        float thick = 0.2f, backLow = 0.08f, sideLow = 0.32f, frontLow = 0.8f, floor = -1.0f;
        bool  capOn = true;
        switch (s) {
        case 1:  thick = 0.12f; backLow = 0.35f; sideLow = 0.55f; break;            // Spiky
        case 8:  backLow = 0.45f; sideLow = 0.55f; break;                           // Bun
        case 10: thick = 0.14f; backLow = 0.3f; sideLow = 0.55f; break;             // Shonen
        case 11: thick = 0.16f; backLow = 0.3f; sideLow = 0.45f; break;             // Pixie
        case 12: thick = 0.13f; backLow = 0.28f; sideLow = 0.42f; frontLow = 0.86f; break;   // Crew cut
        case 13: thick = 0.15f; floor = 0.64f; frontLow = 0.86f; break;             // Undercut
        case 14: thick = 0.14f; backLow = 0.14f; sideLow = 0.36f; frontLow = 0.87f; break;   // Slicked back
        case 15: thick = 0.16f; backLow = 0.14f; sideLow = 0.34f; break;            // Curtains
        case 16: thick = 0.12f; floor = 0.7f; frontLow = 0.9f; break;              // Mohawk (a narrow strip, below)
        case 17: thick = 0.13f; backLow = 0.2f; sideLow = 0.42f; frontLow = 0.85f; break;    // Mullet
        case 18: capOn = false; break;                                              // Bald
        }
        auto cap = [=](V3 p) {
            if (!capOn) return false;
            float e = ell(p);
            // short styles start right at the scalp so their thin
            // shell is still solid at this voxel size
            if (e < (s >= 12 ? 0.93f : 0.98f) || e > 1 + thick || p.y < floor * h) return false;
            if (s == 16 && std::fabs(p.x) > 0.16f * w) return false;
            float t = std::clamp((p.z / d + 0.2f) / 0.7f, 0.0f, 1.0f);
            float low = (backLow + (frontLow - backLow) * t * t) * h;
            if (std::fabs(p.x) > 0.42f * w && p.z < 0.25f * d) low = std::min(low, sideLow * h);
            return p.y > low;
        };

        std::vector<Shape> shapes;
        auto add = [&](V3 a, V3 b, float r0, float r1, float sz = 1.0f, int role = 1) {
            shapes.push_back([=](V3 p) { return cone(p, a, b, r0, r1, sz) ? role : 0; });
        };
        auto fringe = [&](int count, float len, float spread = 0.78f) {
            for (int i = 0; i < count; i++) {
                float fx = ((float)i / (count - 1) - 0.5f) * spread, L = len * (0.8f + 0.4f * rand());
                add(P(fx, 1.0f, 0.42f), P(fx * 1.08f, 1.0f - L, 0.6f), 0.17f * w, 0.0f, 0.7f);
            }
        };
        auto sideLocks = [&](float y1, float r0 = 0.2f) {
            for (int sx : { -1, 1 }) add(P(0.52f * sx, 0.78f, 0.18f), P(0.6f * sx, y1, 0.3f), r0 * w, 0.01f * w, 0.8f);
        };
        auto spike = [&](V3 dir, float from, float len, float r0) {
            add(C + dir * R * from, C + dir * R * len, r0 * w, 0.0f);
        };

        switch (s) {
        case 0:   // Bob
            fringe(5, 0.36f); sideLocks(0.12f, 0.24f);
            for (int sx : { -1, 0, 1 }) add(P(0.3f * sx, 0.7f, -0.4f), P(0.42f * sx, 0.1f, -0.5f), 0.3f * w, 0.08f * w);
            break;
        case 1: { // Spiky
            const int N = 15;
            for (int i = 0; i < N; i++) {
                float yy = 1 - (i + 0.5f) / N * 1.15f, r = std::sqrt(std::max(0.0f, 1 - yy * yy)), th = i * 2.399f;
                V3 dir(std::cos(th) * r, std::max(0.05f, yy), std::sin(th) * r * 0.9f - 0.15f);
                if (dir.z > 0.55f && dir.y < 0.6f) continue;
                float len = 1.5f + 0.35f * rand();
                spike(dir, 0.6f, len, 0.24f);
            }
            fringe(3, 0.32f, 0.5f);
            break;
        }
        case 2:   // Long
            fringe(6, 0.42f, 0.85f); sideLocks(-0.75f, 0.2f);
            shapes.push_back([=](V3 p) {
                float y = p.y / h; if (y > 0.7f || y < -1.0f) return 0;
                float halfW = 0.5f + 0.06f * std::sin(y * 3);
                if (std::fabs(p.x) > halfW * w || p.z > -0.38f * d || p.z < -0.68f * d) return 0;
                float f = std::fmod(p.x / w * 4.5f, 1.0f); if (f < 0) f += 1;
                return y > -0.98f + 0.22f * std::fabs(f - 0.5f) * 2 ? 1 : 0;
            });
            break;
        case 3:   // Twin tails
            fringe(5, 0.34f); sideLocks(0.2f, 0.17f);
            for (int sx : { -1, 1 }) {
                add(P(0.62f * sx, 0.9f, -0.15f), P(0.95f * sx, 0.2f, -0.25f), 0.26f * w, 0.2f * w);
                add(P(0.95f * sx, 0.2f, -0.25f), P(1.05f * sx, -0.85f, -0.2f), 0.2f * w, 0.0f);
                add(P(0.6f * sx, 0.92f, -0.15f), P(0.7f * sx, 0.92f, -0.15f), 0.17f * w, 0.17f * w, 1, 2);
            }
            break;
        case 4:   // Ponytail
            fringe(4, 0.3f, 0.7f); sideLocks(0.25f, 0.16f);
            add(P(0, 0.95f, -0.55f), P(0, 0.35f, -1.0f), 0.24f * w, 0.18f * w);
            add(P(0, 0.35f, -1.0f), P(0, -0.7f, -0.85f), 0.18f * w, 0.0f);
            add(P(0, 0.95f, -0.55f), P(0, 0.88f, -0.66f), 0.15f * w, 0.15f * w, 1, 2);
            break;
        case 6:   // Hime
            shapes.push_back([=](V3 p) { float e = ell(p);
                return (p.y > 0.62f * h && p.y < 1.02f * h && std::fabs(p.x) < 0.5f * w && p.z > 0.22f * d && e > 0.95f && e < 1.3f) ? 1 : 0; });
            shapes.push_back([=](V3 p) {
                return (std::fabs(p.x) > 0.38f * w && std::fabs(p.x) < 0.64f * w && p.z > -0.05f * d && p.z < 0.42f * d &&
                        p.y > -0.05f * h && p.y < 0.8f * h && ell(p) > 0.95f) ? 1 : 0; });
            shapes.push_back([=](V3 p) { float y = p.y / h;
                return (y <= 0.7f && y >= -0.95f && std::fabs(p.x) < 0.52f * w && p.z < -0.38f * d && p.z > -0.66f * d) ? 1 : 0; });
            break;
        case 7:   // Side swept
            for (int i = 0; i < 5; i++) { float x0 = -0.45f + i * 0.2f;
                add(P(x0, 1.03f, 0.4f), P(x0 + 0.3f, 0.62f - i * 0.015f, 0.62f), 0.2f * w, 0.0f, 0.7f); }
            add(P(0.35f, 0.95f, 0.38f), P(0.56f, 0.22f, 0.5f), 0.18f * w, 0.01f * w, 0.8f);
            add(P(-0.5f, 0.8f, 0.15f), P(-0.58f, 0.3f, 0.25f), 0.18f * w, 0.02f * w, 0.8f);
            for (int sx : { -1, 0, 1 }) add(P(0.3f * sx, 0.7f, -0.4f), P(0.45f * sx, -0.05f, -0.5f), 0.3f * w, 0.05f * w);
            break;
        case 8:   // Bun
            fringe(4, 0.24f, 0.7f);
            for (int sx : { -1, 1 }) add(P(0.5f * sx, 0.75f, 0.25f), P(0.55f * sx, 0.12f, 0.32f), 0.09f * w, 0.0f, 0.8f);
            add(P(0, 1.14f, -0.42f), P(0, 1.16f, -0.44f), 0.34f * w, 0.34f * w);
            add(P(0, 0.96f, -0.36f), P(0, 1.02f, -0.4f), 0.22f * w, 0.22f * w, 1, 2);
            break;
        case 9:   // Braids
            for (int sx : { -1, 1 }) add(P(0.06f * sx, 1.03f, 0.42f), P(0.44f * sx, 0.56f, 0.5f), 0.2f * w, 0.02f * w, 0.7f);
            for (int sx : { -1, 1 }) {
                V3 a(0.5f * sx, 0.45f, -0.05f), b(0.4f * sx, -0.75f, 0.3f);
                for (int k = 0; k < 7; k++) {
                    float t = k / 6.0f, wob = (k % 2 ? 0.06f : -0.06f) * sx;
                    V3 q = P(a.x + (b.x - a.x) * t + wob, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
                    float r = (0.17f - 0.05f * t) * w;
                    add(q, q + V3(0, -0.01f, 0), r, r);
                }
                add(P(b.x, b.y - 0.05f, b.z), P(b.x, b.y - 0.1f, b.z), 0.1f * w, 0.1f * w, 1, 2);
                add(P(b.x, b.y - 0.1f, b.z), P(b.x * 1.05f, b.y - 0.32f, b.z + 0.05f), 0.11f * w, 0.0f);
            }
            break;
        case 10: { // Shonen
            const float dirs[10][3] = {{0, 0.9f, -0.6f}, {0.55f, 0.6f, -0.65f}, {-0.55f, 0.6f, -0.65f}, {0.3f, 0.2f, -1}, {-0.3f, 0.2f, -1},
                                      {0, -0.1f, -1}, {0.85f, 0.3f, -0.35f}, {-0.85f, 0.3f, -0.35f}, {0.2f, 1, 0.1f}, {-0.25f, 1, -0.05f}};
            for (auto& dv : dirs) spike(glm::normalize(V3(dv[0], dv[1], dv[2])), 0.5f, 1.75f + 0.25f * rand(), 0.3f);
            for (int sx : { -1, 0, 1 }) add(P(0.22f * sx, 0.98f, 0.35f), P(0.32f * sx, 0.68f, 0.66f), 0.17f * w, 0.0f, 0.7f);
            break;
        }
        case 11:  // Pixie
            for (int i = 0; i < 4; i++) { float x0 = -0.35f + i * 0.22f;
                add(P(x0, 1.0f, 0.42f), P(x0 + 0.14f, 0.74f, 0.6f), 0.17f * w, 0.0f, 0.7f); }
            for (int sx : { -1, 1 }) add(P(0.2f * sx, 0.45f, -0.45f), P(0.26f * sx, 0.22f, -0.58f), 0.14f * w, 0.0f);
            break;
        case 12:  // Crew cut: short all over, a hint of fringe
            for (int i = 0; i < 5; i++) { float fx = (i / 4.0f - 0.5f) * 0.6f;
                add(P(fx, 1.0f, 0.4f), P(fx, 0.9f, 0.56f), 0.13f * w, 0.0f, 0.7f); }
            break;
        case 13:  // Undercut: shaved sides, long top swept back with a quiff
            for (int i = 0; i < 5; i++) { float fx = (i / 4.0f - 0.5f) * 0.62f;
                add(P(fx, 1.0f, 0.38f), P(fx * 1.1f, 1.08f, -0.35f), 0.22f * w, 0.12f * w); }
            add(P(0.0f, 0.98f, 0.42f), P(0.06f, 1.2f, 0.36f), 0.22f * w, 0.1f * w);
            add(P(0.06f, 1.2f, 0.36f), P(0.1f, 1.18f, 0.0f), 0.12f * w, 0.0f);
            break;
        case 14:  // Slicked back: smooth strands to the nape, a flick at the end
            for (int i = 0; i < 6; i++) { float fx = (i / 5.0f - 0.5f) * 0.8f;
                add(P(fx, 0.98f, 0.4f), P(fx * 1.05f, 0.95f, -0.5f), 0.15f * w, 0.12f * w); }
            for (int sx : { -1, 1 }) add(P(0.15f * sx, 0.5f, -0.55f), P(0.22f * sx, 0.28f, -0.62f), 0.13f * w, 0.0f);
            break;
        case 15:  // Curtains: middle part, fringe falling to both sides
            for (int sx : { -1, 1 })
                for (int i = 0; i < 3; i++) { float x0 = 0.04f + i * 0.12f;
                    add(P(x0 * sx, 1.02f, 0.4f), P((0.36f + i * 0.08f) * sx, 0.58f - i * 0.04f, 0.58f), 0.17f * w, 0.0f, 0.7f); }
            for (int sx : { -1, 0, 1 }) add(P(0.3f * sx, 0.6f, -0.45f), P(0.38f * sx, 0.24f, -0.52f), 0.22f * w, 0.05f * w);
            break;
        case 16: { // Mohawk: a crest of spikes front to back
            for (int i = 0; i < 7; i++) {
                float a = -0.35f + i * 0.33f;                       // front … over the top … back
                V3 dir(0.0f, std::cos(a), std::sin(a) * -1.0f + 0.25f);
                dir = glm::normalize(dir);
                spike(dir, 0.8f, 1.6f + 0.14f * (i % 2), 0.26f);
            }
            break;
        }
        case 17:  // Mullet: short on top, long at the back
            fringe(4, 0.16f, 0.6f);
            shapes.push_back([=](V3 p) {
                float y = p.y / h; if (y > 0.62f || y < -0.35f) return 0;
                if (std::fabs(p.x) > 0.44f * w || p.z > -0.34f * d || p.z < -0.64f * d) return 0;
                float f = std::fmod(p.x / w * 5.0f, 1.0f); if (f < 0) f += 1;
                return y > -0.32f + 0.18f * std::fabs(f - 0.5f) * 2 ? 1 : 0;
            });
            break;
        case 18:  // Bald
            break;
        default: { // 5: Messy, with a sprout on top
            fringe(5, 0.4f); sideLocks(0.1f, 0.2f);
            for (int i = 0; i < 8; i++) {
                float th = rand() * 6.2831853f, yy = 0.2f + rand() * 0.5f;
                V3 dir(std::cos(th) * 0.9f, yy, std::sin(th) * 0.9f - 0.3f);
                add(C + dir * R * 0.8f, C + dir * R * V3(1.4f, 1.3f, 1.4f), 0.2f * w, 0.0f);
            }
            add(P(0.05f, 1.0f, 0.1f), P(0.15f, 1.3f, 0.3f), 0.07f * w, 0.05f * w);
            add(P(0.15f, 1.3f, 0.3f), P(0.35f, 1.38f, 0.55f), 0.05f * w, 0.0f);
            break;
        }
        }

        // extras: roles 3 (main colour) and 4 (second colour)
        std::vector<Shape> extra;
        if (ex == 1) {            // cat ears
            for (int sx : { -1, 1 })
                extra.push_back([=](V3 p) {
                    if (!cone(p, P(0.36f * sx, 0.95f, -0.05f), P(0.55f * sx, 1.5f, -0.05f), 0.24f * w, 0.0f, 0.6f)) return 0;
                    bool inner = p.z > 0 && std::fabs(p.x) < 0.52f * w && p.y < 1.4f * h && std::fabs(p.x - 0.43f * sx * w) < 0.12f * w;
                    return inner ? 4 : 3;
                });
        } else if (ex == 2) {     // horns
            for (int sx : { -1, 1 })
                extra.push_back([=](V3 p) {
                    return (cone(p, P(0.3f * sx, 0.95f, 0.15f), P(0.5f * sx, 1.25f, -0.05f), 0.12f * w, 0.09f * w) ||
                            cone(p, P(0.5f * sx, 1.25f, -0.05f), P(0.78f * sx, 1.35f, -0.35f), 0.09f * w, 0.0f)) ? 3 : 0;
                });
        } else if (ex == 3) {     // pointed hat
            extra.push_back([=](V3 p) {
                float y = p.y / h;
                if (y > 0.92f && y < 1.0f && std::hypot(p.x / w, p.z / d * 1.1f) < 1.15f) return 4;
                if (frustum(p, P(0, 0.95f, -0.05f), P(0, 1.55f, -0.3f), 0.62f * w, 0.32f * w)) return y < 1.08f ? 4 : 3;
                if (y >= 1.5f && cone(p, P(0, 1.55f, -0.3f), P(0.35f, 2.05f, -0.7f), 0.32f * w, 0.0f)) return 3;
                return 0;
            });
        } else if (ex == 4) {     // straw hat
            extra.push_back([=](V3 p) {
                float y = p.y / h, rr = std::hypot(p.x / w, (p.z + 0.04f * d) / d);
                if (y > 0.9f && y < 1.0f && rr < 1.38f) return 3;
                if (frustum(p, P(0, 0.92f, -0.04f), P(0, 1.34f, -0.04f), 0.66f * w, 0.56f * w)) return y < 1.08f ? 4 : 3;
                return 0;
            });
        } else if (ex == 5) {     // headscarf
            extra.push_back([=](V3 p) {
                float e = ell(p), t = std::clamp((p.z / d + 0.2f) / 0.7f, 0.0f, 1.0f);
                if (e > 1.1f && e < 1.36f && p.y > (0.28f + 0.56f * t * t) * h) return 3;
                if (cone(p, P(0, 0.36f, -0.72f), P(0, 0.38f, -0.74f), 0.17f * w, 0.17f * w)) return 4;
                for (int sx : { -1, 1 }) if (cone(p, P(0.05f * sx, 0.3f, -0.75f), P(0.16f * sx, -0.2f, -0.82f), 0.1f * w, 0.0f)) return 3;
                return 0;
            });
        }
        auto extraAt = [&](V3 p) { for (auto& f : extra) if (int r = f(p)) return r; return 0; };
        const bool hat = ex == 3 || ex == 4;

        V3 lo(-wide * w, -1.05f * h, -std::max(1.1f, wide) * d), hi(wide * w, tall * h, std::max(0.95f, wide) * d);
        const float vox = w / 10.0f;    // coarse voxels → big, bold shards

        // Hair grid: where an extra sits, the hair steps aside, so
        // the two stay separate meshes that never fuse
        Grid hairG(lo, hi, vox);
        // one voxel of clearance around the extra, so the hair's
        // surface never touches (or pokes through) the hat's
        const float gap = w / 10.0f;
        auto nearExtra = [&](V3 p) {
            if (extraAt(p)) return true;
            for (int a = 0; a < 3; a++) for (int sgn : { -1, 1 }) {
                if (a == 1 && sgn > 0) continue;      // hair may tuck right up under a brim
                V3 q = p; q[a] += sgn * gap;
                if (extraAt(q)) return true;
            }
            return false;
        };
        hairG.fill([&](V3 p) {
            if (!extra.empty() && nearExtra(p)) return 0;
            if (hat && p.y > 0.9f * h) return 0;     // a hat replaces the top of the hair
            if (cap(p)) return 1;
            for (auto& f : shapes) if (int r = f(p)) return r;
            return 0;
        });
        Grid extraG(lo, hi, vox);
        if (!extra.empty()) extraG.fill(extraAt);
        for (Grid* g : { &hairG, &extraG })        // never inside the head itself
            for (int z = 0; z < g->n[2]; z++) for (int y = 0; y < g->n[1]; y++) for (int x = 0; x < g->n[0]; x++)
                if (g->r[g->idx(x, y, z)] && ell(g->centre(x, y, z)) < 0.93f) g->r[g->idx(x, y, z)] = 0;
        for (Grid* g : { &hairG, &extraG })        // lone voxels (tips of thin locks) read as specks
            for (int z = 0; z < g->n[2]; z++) for (int y = 0; y < g->n[1]; y++) for (int x = 0; x < g->n[0]; x++)
                if (g->r[g->idx(x, y, z)] && !g->get(x + 1, y, z) && !g->get(x - 1, y, z) && !g->get(x, y + 1, z) &&
                    !g->get(x, y - 1, z) && !g->get(x, y, z + 1) && !g->get(x, y, z - 1))
                    g->r[g->idx(x, y, z)] = 0;

        // colours by role
        const V3 hairC(c.hair);
        const V3 accent = s == 3 ? col("#b5607a") : (s == 9 || s == 8) ? col("#8e4d3a") : col("#ece7de");
        V3 exA(255), exB(255);
        switch (ex) {
        case 1: exA = hairC; exB = col("#d9a9b4"); break;
        case 2: exA = exB = col("#ddd5c6"); break;
        case 3: exA = col("#4f4570"); exB = col("#cdb46a"); break;
        case 4: exA = col("#d6bb7c"); exB = col("#8e4d3a"); break;
        case 5: exA = V3(c.bottomColor); exB = shade(exA, 0.75f); break;
        }
        const V3 colours[5] = { V3(0), hairC, accent, exA, exB };

        size_t hairStart = out.size();
        meshGrid(hairG, colours, mode, out);
        if (mode != HairMesh::Voxels) {           // anime-style shine band across the crown
            float lum = (0.3f * hairC.r + 0.59f * hairC.g + 0.11f * hairC.b) / 255.0f;
            V3 shine = hairC + (V3(255) - hairC) * (0.15f + 0.35f * std::min(1.0f, lum * 1.6f));
            glm::u8vec3 hc8(hairC);
            for (size_t i = hairStart; i + 2 < out.size(); i += 3) {
                float y = (out[i].y + out[i + 1].y + out[i + 2].y) / 3.0f;
                if (y < h * 0.86f || y > h * 1.04f) continue;
                if (out[i].ny < -13 || out[i].nz < -38) continue;
                if (out[i].r != hc8.r || out[i].g != hc8.g || out[i].b != hc8.b) continue;
                for (int k = 0; k < 3; k++) { out[i + k].r = (uint8_t)shine.r; out[i + k].g = (uint8_t)shine.g; out[i + k].b = (uint8_t)shine.b; }
            }
        }
        for (size_t i = hairStart; i < out.size(); i++) out[i].a = 1;
        size_t extraStart = out.size();
        meshGrid(extraG, colours, mode, out);
        for (size_t i = extraStart; i < out.size(); i++) out[i].a = 2;
    }

    static void meshGrid(const Grid& g, const V3* colours, HairMesh mode, std::vector<PartVertex>& out) {
        if (g.empty()) return;
        if (mode == HairMesh::Voxels) meshCubes(g, colours, out);
        else meshShards(g, colours, mode == HairMesh::SoftShards ? 2 : 0, out);
    }

    static void meshCubes(const Grid& g, const V3* colours, std::vector<PartVertex>& out) {
        static const int N[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        static const float C[6][4][3] = {
            {{1,0,0},{1,1,0},{1,1,1},{1,0,1}}, {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},
            {{0,1,0},{0,1,1},{1,1,1},{1,1,0}}, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
            {{1,0,1},{1,1,1},{0,1,1},{0,0,1}}, {{0,0,0},{0,1,0},{1,1,0},{1,0,0}},
        };
        for (int z = 0; z < g.n[2]; z++) for (int y = 0; y < g.n[1]; y++) for (int x = 0; x < g.n[0]; x++) {
            int role = g.get(x, y, z); if (!role) continue;
            V3 base = g.centre(x, y, z) - 0.5f * g.v;
            for (int f = 0; f < 6; f++) {
                if (g.get(x + N[f][0], y + N[f][1], z + N[f][2])) continue;
                V3 q[4];
                for (int i = 0; i < 4; i++) q[i] = base + V3(C[f][i][0], C[f][i][1], C[f][i][2]) * g.v;
                tri(out, q[0], q[1], q[2], colours[role]); tri(out, q[0], q[2], q[3], colours[role]);
            }
        }
    }

    // Surface nets: one vertex per surface cell at the mean of its
    // edge crossings, one quad per voxel edge that crosses the
    // surface. Voxel stairs become angled facets: shards.
    static void meshShards(const Grid& g, const V3* colours, int relax, std::vector<PartVertex>& out) {
        const int nx = g.n[0], ny = g.n[1], nz = g.n[2];
        auto ci = [&](int x, int y, int z) { return ((size_t)z * ny + y) * nx + x; };
        std::vector<int> cellV((size_t)nx * ny * nz, -1);
        std::vector<V3> verts;
        static const int E[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
        for (int z = 0; z < nz - 1; z++) for (int y = 0; y < ny - 1; y++) for (int x = 0; x < nx - 1; x++) {
            int mask = 0;
            for (int k = 0; k < 8; k++) mask |= (g.get(x + (k & 1), y + ((k >> 1) & 1), z + (k >> 2)) ? 1 : 0) << k;
            if (mask == 0 || mask == 255) continue;
            V3 sum(0); int cnt = 0;
            for (auto& e : E) {
                int a = e[0], b = e[1];
                if (((mask >> a) & 1) == ((mask >> b) & 1)) continue;
                sum += V3(((a & 1) + (b & 1)) * 0.5f, (((a >> 1) & 1) + ((b >> 1) & 1)) * 0.5f, ((a >> 2) + (b >> 2)) * 0.5f);
                cnt++;
            }
            cellV[ci(x, y, z)] = (int)verts.size();
            verts.push_back(V3(x, y, z) + sum / (float)cnt);
        }
        for (int it = 0; it < relax; it++) {     // pull toward neighbours, kept inside the cell
            std::vector<V3> next = verts;
            for (int z = 0; z < nz - 1; z++) for (int y = 0; y < ny - 1; y++) for (int x = 0; x < nx - 1; x++) {
                int id = cellV[ci(x, y, z)]; if (id < 0) continue;
                V3 sum(0); int cnt = 0;
                static const int D[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
                for (auto& dd : D) {
                    int X = x + dd[0], Y = y + dd[1], Z = z + dd[2];
                    if (X < 0 || Y < 0 || Z < 0 || X >= nx - 1 || Y >= ny - 1 || Z >= nz - 1) continue;
                    int j = cellV[ci(X, Y, Z)]; if (j < 0) continue;
                    sum += verts[j]; cnt++;
                }
                if (!cnt) continue;
                V3 lo = V3(x, y, z) + 0.05f;
                next[id] = glm::clamp(verts[id] * 0.5f + sum / (float)cnt * 0.5f, lo, lo + 0.9f);
            }
            verts.swap(next);
        }
        auto world = [&](V3 v) { return g.min + (v - 0.5f) * g.v; };
        for (int z = 1; z < nz - 1; z++) for (int y = 1; y < ny - 1; y++) for (int x = 1; x < nx - 1; x++) {
            int here = g.get(x, y, z);
            for (int a = 0; a < 3; a++) {
                int p[3] = { x, y, z }; p[a]++;
                int there = g.get(p[0], p[1], p[2]);
                if ((here != 0) == (there != 0)) continue;
                int role = here ? here : there;
                int u = (a + 1) % 3, w = (a + 2) % 3;
                int ids[4];
                const int DU[4] = {0, 1, 1, 0}, DW[4] = {0, 0, 1, 1};
                bool ok = true;
                for (int k = 0; k < 4; k++) {
                    int cc[3] = { x, y, z };
                    cc[u] -= 1 - DU[k]; cc[w] -= 1 - DW[k];
                    ids[k] = cellV[ci(cc[0], cc[1], cc[2])];
                    if (ids[k] < 0) ok = false;
                }
                if (!ok) continue;
                V3 q[4] = { world(verts[ids[0]]), world(verts[ids[1]]), world(verts[ids[2]]), world(verts[ids[3]]) };
                if (!here) std::swap(q[1], q[3]);       // face away from the solid side
                tri(out, q[0], q[1], q[2], colours[role]); tri(out, q[0], q[2], q[3], colours[role]);
            }
        }
    }
};
