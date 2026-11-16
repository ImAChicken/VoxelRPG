#pragma once
#include "../character/CharacterModel.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ══════════════════════════════════════════════════════════
// IconRaster — a little picture of a mesh, drawn on the CPU
// ══════════════════════════════════════════════════════════
// Item icons for tools, weapons and gear (ui/ItemIcon.h) are
// their real models drawn once at load: the triangles turned by
// a view, fitted to the square, lit by a fixed light, drawn at
// twice the size and averaged down (smooth edges), then given a
// dark outline like the characters' edge lines. No GL here, so
// tests can check them.
namespace iconraster {

struct Image {
    int size = 0;
    std::vector<uint8_t> rgba;          // size * size * 4, top row first
    bool empty() const { return rgba.empty(); }
    int coverage() const { int n = 0; for (size_t i = 3; i < rgba.size(); i += 4) n += rgba[i] > 128; return n; }
};

// view: turns model space into the picture's (x right, y up,
// z toward you). margin: empty border, a fraction of the size.
inline Image draw(const std::vector<PartVertex>& tris, const glm::mat3& view, int size, float margin = 0.08f) {
    Image out;
    if (tris.size() < 3 || size <= 0) return out;
    const int S = size * 2;                       // drawn at twice the size
    std::vector<glm::vec3> p(tris.size());
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (size_t i = 0; i < tris.size(); i++) {
        p[i] = view * glm::vec3(tris[i].x, tris[i].y, tris[i].z);
        lo = glm::min(lo, p[i]); hi = glm::max(hi, p[i]);
    }
    float span = std::max(hi.x - lo.x, hi.y - lo.y);
    if (span <= 1e-5f) return out;
    float scale = S * (1.0f - 2.0f * margin) / span;
    glm::vec2 mid((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
    auto toPx = [&](glm::vec3 q) { return glm::vec3((q.x - mid.x) * scale + S * 0.5f, S * 0.5f - (q.y - mid.y) * scale, q.z); };

    std::vector<float> depth((size_t)S * S, -1e9f);
    std::vector<glm::vec3> col((size_t)S * S, glm::vec3(0));
    std::vector<uint8_t> hit((size_t)S * S, 0);
    const glm::vec3 light = glm::normalize(glm::vec3(-0.45f, 0.75f, 0.55f));
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        glm::vec3 a = toPx(p[t]), b = toPx(p[t + 1]), c = toPx(p[t + 2]);
        float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f) continue;
        glm::vec3 n = glm::normalize(view * glm::vec3(tris[t].nx, tris[t].ny, tris[t].nz) + glm::vec3(1e-6f));
        if (n.z < -0.05f) continue;               // facing away
        float lit = 0.5f + 0.5f * std::max(0.0f, glm::dot(n, light)) + 0.12f * n.z;
        glm::vec3 base(tris[t].r, tris[t].g, tris[t].b);
        glm::vec3 shade = glm::min(base * lit, glm::vec3(255.0f));
        int x0 = std::max(0, (int)std::floor(std::min({ a.x, b.x, c.x }))), x1 = std::min(S - 1, (int)std::ceil(std::max({ a.x, b.x, c.x })));
        int y0 = std::max(0, (int)std::floor(std::min({ a.y, b.y, c.y }))), y1 = std::min(S - 1, (int)std::ceil(std::max({ a.y, b.y, c.y })));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = (b.x - px) * (c.y - py) - (b.y - py) * (c.x - px);
                float w1 = (c.x - px) * (a.y - py) - (c.y - py) * (a.x - px);
                float w2 = (a.x - px) * (b.y - py) - (a.y - py) * (b.x - px);
                if (area > 0 ? (w0 < 0 || w1 < 0 || w2 < 0) : (w0 > 0 || w1 > 0 || w2 > 0)) continue;
                float z = (w0 * a.z + w1 * b.z + w2 * c.z) / area;
                size_t i = (size_t)y * S + x;
                if (z <= depth[i]) continue;
                depth[i] = z; col[i] = shade; hit[i] = 1;
            }
    }
    // outline: empty pixels touching the figure go dark
    std::vector<uint8_t> edge((size_t)S * S, 0);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            if (hit[(size_t)y * S + x]) continue;
            for (int dy = -2; dy <= 2 && !edge[(size_t)y * S + x]; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int qx = x + dx, qy = y + dy;
                    if (qx < 0 || qy < 0 || qx >= S || qy >= S || dx * dx + dy * dy > 5) continue;
                    if (hit[(size_t)qy * S + qx]) { edge[(size_t)y * S + x] = 1; break; }
                }
        }
    // average 2 x 2 down to the icon
    out.size = size;
    out.rgba.assign((size_t)size * size * 4, 0);
    const glm::vec3 ink(26, 20, 18);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            glm::vec3 sum(0); float a = 0;
            for (int k = 0; k < 4; k++) {
                size_t i = (size_t)(y * 2 + k / 2) * S + (x * 2 + k % 2);
                if (hit[i])       { sum += col[i]; a += 1; }
                else if (edge[i]) { sum += ink; a += 1; }
            }
            uint8_t* o = &out.rgba[((size_t)y * size + x) * 4];
            if (a > 0) { glm::vec3 c = sum / a; o[0] = (uint8_t)c.r; o[1] = (uint8_t)c.g; o[2] = (uint8_t)c.b; }
            o[3] = (uint8_t)std::lround(255.0f * a / 4.0f);
        }
    return out;
}

// A held model (its own frame: u along, v edge, w across) laid
// on the diagonal: grip low left, business end high right, the
// flat side toward you, tipped a little to show its thickness
inline glm::mat3 heldView() {
    const float c = std::cos(0.7853982f), s = std::sin(0.7853982f);
    glm::mat3 diag(glm::vec3(c, s, 0), glm::vec3(-s, c, 0), glm::vec3(0, 0, 1));    // u → up-right, v → up-left
    const float t = 0.35f;                                                              // tip about the diagonal
    glm::vec3 ax = glm::normalize(glm::vec3(c, s, 0));
    glm::mat3 K(0, ax.z, -ax.y, -ax.z, 0, ax.x, ax.y, -ax.x, 0);
    glm::mat3 tip = glm::mat3(1.0f) + std::sin(t) * K + (1 - std::cos(t)) * K * K;
    return tip * diag;
}
// Gear on a body (x to the body's left, y up, z forward): seen
// from the front, a little from the side and above
inline glm::mat3 gearView() {
    float yaw = glm::radians(-28.0f), pitch = glm::radians(16.0f);
    glm::mat3 Y(glm::vec3(std::cos(yaw), 0, -std::sin(yaw)), glm::vec3(0, 1, 0), glm::vec3(std::sin(yaw), 0, std::cos(yaw)));
    glm::mat3 X(glm::vec3(1, 0, 0), glm::vec3(0, std::cos(pitch), std::sin(pitch)), glm::vec3(0, -std::sin(pitch), std::cos(pitch)));
    glm::mat3 flipX(glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1));   // body's left on the picture's right
    return X * Y * flipX;
}

// Gear pieces moved from their bones to where the bones stand
// at rest, all in body space
inline std::vector<PartVertex> gearAtRest(const CharacterModel& m, const GearLook& g) {
    auto parts = CharacterModel::gearParts(m.fit, { g });
    std::vector<PartVertex> out;
    const bool one = g.shape == "gauntlets";      // the hands hang far apart: show one
    for (int b = 0; b < BONE_COUNT; b++) {
        if (one && (b == B_SHOULDER_L || b == B_ELBOW_L || b == B_WRIST_L)) continue;
        glm::vec3 at(0);
        for (int j = b; j >= 0; j = m.bones[j].parent) at += m.bones[j].offset;
        for (PartVertex v : parts[b]) { v.x += at.x; v.y += at.y; v.z += at.z; out.push_back(v); }
    }
    return out;
}

} // namespace iconraster
