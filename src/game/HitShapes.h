#pragma once
#include "../character/CharacterModel.h"
#include "../character/HeldItemModel.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// ══════════════════════════════════════════════════════════
// HitShapes — what a blade and a body are, for hits
// ══════════════════════════════════════════════════════════
// A weapon is the boxes of its voxel map (character/
// HeldItemModel.h: voxels merged into a few boxes when the model
// loads, each grown by the model's hit_margin). A body is one
// box per bone, the bounds of that bone's part of the mesh. Both
// ride the posed skeleton, so a hit is a blade box touching a
// limb box: no reach, arc or height band.
//
// Everything here is an oriented box (OBB) in world voxels.
namespace hitshape {

struct OBB {
    glm::vec3 c{0.0f};                        // centre
    glm::vec3 ax[3] = { {1,0,0}, {0,1,0}, {0,0,1} };   // unit axes
    glm::vec3 h{0.0f};                        // half sizes along them
};

// A box given in some frame (lo..hi along the frame's axes),
// placed by a matrix that is a rotation and one uniform scale
// plus a move (bone and wrist matrices are)
inline OBB place(const glm::mat4& M, glm::vec3 lo, glm::vec3 hi) {
    OBB o;
    o.c = glm::vec3(M * glm::vec4((lo + hi) * 0.5f, 1.0f));
    glm::vec3 half = (hi - lo) * 0.5f;
    for (int i = 0; i < 3; i++) {
        glm::vec3 col(M[i]);
        float len = glm::length(col);
        o.ax[i] = len > 1e-6f ? col / len : glm::vec3(i == 0, i == 1, i == 2);
        o.h[i] = half[i] * len;
    }
    return o;
}

// Do two boxes overlap (separating axis test)
inline bool overlap(const OBB& A, const OBB& B) {
    const float EPS = 1e-5f;
    float R[3][3], AR[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) { R[i][j] = glm::dot(A.ax[i], B.ax[j]); AR[i][j] = std::fabs(R[i][j]) + EPS; }
    glm::vec3 d = B.c - A.c;
    float t[3] = { glm::dot(d, A.ax[0]), glm::dot(d, A.ax[1]), glm::dot(d, A.ax[2]) };
    for (int i = 0; i < 3; i++)
        if (std::fabs(t[i]) > A.h[i] + B.h[0] * AR[i][0] + B.h[1] * AR[i][1] + B.h[2] * AR[i][2]) return false;
    for (int j = 0; j < 3; j++) {
        float ra = A.h[0] * AR[0][j] + A.h[1] * AR[1][j] + A.h[2] * AR[2][j];
        if (std::fabs(t[0] * R[0][j] + t[1] * R[1][j] + t[2] * R[2][j]) > ra + B.h[j]) return false;
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            float ra = A.h[i1] * AR[i2][j] + A.h[i2] * AR[i1][j];
            float rb = B.h[j1] * AR[i][j2] + B.h[j2] * AR[i][j1];
            if (std::fabs(t[i2] * R[i1][j] - t[i1] * R[i2][j]) > ra + rb) return false;
        }
    return true;
}

// The point of box B nearest to p (where a hit lands)
inline glm::vec3 closest(const OBB& B, glm::vec3 p) {
    glm::vec3 d = p - B.c, q = B.c;
    for (int i = 0; i < 3; i++) q += B.ax[i] * std::clamp(glm::dot(d, B.ax[i]), -B.h[i], B.h[i]);
    return q;
}
inline bool contains(const OBB& B, glm::vec3 p, float grow = 0.0f) {
    glm::vec3 d = p - B.c;
    for (int i = 0; i < 3; i++) if (std::fabs(glm::dot(d, B.ax[i])) > B.h[i] + grow) return false;
    return true;
}
inline void corners(const OBB& o, glm::vec3 out[8]) {
    for (int k = 0; k < 8; k++)
        out[k] = o.c + o.ax[0] * (k & 1 ? o.h[0] : -o.h[0]) + o.ax[1] * (k & 2 ? o.h[1] : -o.h[1])
                     + o.ax[2] * (k & 4 ? o.h[2] : -o.h[2]);
}

// ── Bodies ────────────────────────────────────────────────
// One box per bone (bone frame, inches): the bounds of the
// vertices of that bone's part of the mesh (body, clothes, hair)
struct BodyBoxes {
    std::array<glm::vec3, BONE_COUNT> lo{}, hi{};
    std::array<bool, BONE_COUNT> has{};

    static BodyBoxes from(const CharacterModel& m) {
        BodyBoxes b;
        for (int i = 0; i < BONE_COUNT; i++) {
            const auto& part = m.parts[i];
            if (part.empty()) continue;
            glm::vec3 lo(1e9f), hi(-1e9f);
            for (const PartVertex& v : part) { glm::vec3 p(v.x, v.y, v.z); lo = glm::min(lo, p); hi = glm::max(hi, p); }
            b.lo[i] = lo; b.hi[i] = hi; b.has[i] = true;
        }
        return b;
    }
    // In the world, for a skeleton (world bone matrices)
    OBB world(int bone, const std::array<glm::mat4, BONE_COUNT>& mats) const {
        return place(mats[bone], lo[bone], hi[bone]);
    }
};

// ── Weapons ───────────────────────────────────────────────
// The weapon's boxes in the world for a wrist matrix
inline void weaponBoxes(const HeldModelDef& hm, const CharacterModel& m, const glm::mat4& wrist,
                        std::vector<OBB>& out) {
    out.clear();
    // model frame (u, v, w) → wrist frame: grip + along·u + edge·v + across·w
    glm::mat4 F(1.0f);
    F[0] = glm::vec4(hm.along, 0.0f);
    F[1] = glm::vec4(hm.edge, 0.0f);
    F[2] = glm::vec4(hm.across, 0.0f);
    F[3] = glm::vec4(heldGrip(m), 1.0f);
    glm::mat4 M = wrist * F;
    for (const auto& b : hm.hitBoxes) out.push_back(place(M, b.first - glm::vec3(hm.hitMargin), b.second + glm::vec3(hm.hitMargin)));
}

// ── A posed body, as the hit code sees it ─────────────────
// (filled by the simulation: sim/BodyPoses.h)
struct Posed {
    const std::array<glm::mat4, BONE_COUNT>* now  = nullptr;   // this tick's skeleton (world)
    const std::array<glm::mat4, BONE_COUNT>* prev = nullptr;   // last tick's, or null
    const BodyBoxes*      body  = nullptr;
    const CharacterModel* model = nullptr;
    const HeldModelDef*   held  = nullptr;                     // in the right hand, or null
};

// A wrist (or any bone) matrix between two ticks: position
// mixed, rotation slerped, scale kept
inline glm::mat4 between(const glm::mat4& a, const glm::mat4& b, float s) {
    if (s <= 0.0f) return a;
    if (s >= 1.0f) return b;
    float sa = glm::length(glm::vec3(a[0])), sb = glm::length(glm::vec3(b[0]));
    float sc = glm::mix(sa, sb, s);
    glm::mat3 ra(glm::vec3(a[0]) / sa, glm::vec3(a[1]) / sa, glm::vec3(a[2]) / sa);
    glm::mat3 rb(glm::vec3(b[0]) / sb, glm::vec3(b[1]) / sb, glm::vec3(b[2]) / sb);
    glm::mat3 r = glm::mat3_cast(glm::slerp(glm::quat_cast(ra), glm::quat_cast(rb), s));
    glm::mat4 out(r * sc);
    out[3] = glm::mix(a[3], b[3], s);
    return out;
}

} // namespace hitshape
