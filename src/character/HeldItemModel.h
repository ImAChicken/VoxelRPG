#pragma once
#include "CharacterModel.h"
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// HeldItemModel — things drawn in the right hand
// ══════════════════════════════════════════════════════════
// Built out of voxels the way the hair is (CharacterModel::
// meshVoxelShape: a role grid, meshed as voxels or shards per
// the hair mesh setting), in the right wrist's frame, so the
// renderer draws them with that bone's matrix.
//
// Which model an item shows comes from items.toml
// (held_model = "axe" / "sword").
//
// The axe's geometry is also used for its hits: bladePoints()
// gives points along the cutting edge, which the swing traces
// through the world (game/AxeSwing.h).
enum class HeldModel : uint8_t { None, Axe, Sword };

inline HeldModel heldModelFromKey(const std::string& k) {
    return k == "axe" ? HeldModel::Axe : k == "sword" ? HeldModel::Sword : HeldModel::None;
}

// ── The axe ───────────────────────────────────────────────
// A Valheim-sized one-handed axe: a 24-inch haft held near its
// end, a wrapped grip, and a forged head with a flared edge.
// Inches; in the axe's own frame:
//   u  along the haft, from the fist toward the head
//   v  from the haft toward the cutting edge
//   w  across (the head's thickness)
struct AxeGeometry {
    static constexpr float HAFT_BACK   = 2.5f;    // haft behind the fist
    static constexpr float HAFT_LEN    = 21.5f;   // fist to the top of the haft
    static constexpr float HAFT_R      = 0.75f;
    static constexpr float GRIP_LEN    = 4.5f;    // leather wrap around the fist
    static constexpr float HEAD_U      = 18.5f;   // centre of the head along the haft
    static constexpr float HEAD_BACK   = 1.6f;    // the poll behind the haft
    static constexpr float BLADE_REACH = 5.5f;    // haft to the cutting edge
    static constexpr float CHEEK_H     = 2.2f;    // head height at the haft
    static constexpr float EDGE_H      = 5.6f;    // head height at the edge (flared)
    static constexpr float HEAD_T      = 1.3f;    // thickness at the haft
    static constexpr float VOXEL       = 0.5f;    // model voxel size

    // The axe's frame in the wrist's frame (inches): the fist
    // holds it where the hand is, the haft points forward out
    // of the fist (tilted down along the forearm), the edge
    // faces the way the arm swings.
    static glm::vec3 grip(const CharacterModel& m) {
        float s = m.heightIn / 67.4f;
        return glm::vec3(0.0f, -2.6f * s, 0.6f * s);
    }
    static glm::vec3 haftDir() { return glm::normalize(glm::vec3(0.0f, -0.5f, 1.0f)); }
    static glm::vec3 edgeDir() { return glm::normalize(glm::vec3(0.0f, -1.0f, -0.5f)); }
    static glm::vec3 acrossDir() { return glm::vec3(1.0f, 0.0f, 0.0f); }

    // Axe frame (u, v, w) → wrist frame
    static glm::vec3 toWrist(const CharacterModel& m, glm::vec3 uvw) {
        return grip(m) + haftDir() * uvw.x + edgeDir() * uvw.y + acrossDir() * uvw.z;
    }

    // Points along the cutting edge (wrist frame, inches):
    // the edge's two horns and its middle
    static std::array<glm::vec3, 3> bladePoints(const CharacterModel& m) {
        return {
            toWrist(m, { HEAD_U - EDGE_H * 0.4f, BLADE_REACH - 0.3f, 0.0f }),
            toWrist(m, { HEAD_U,                 BLADE_REACH,        0.0f }),
            toWrist(m, { HEAD_U + EDGE_H * 0.4f, BLADE_REACH - 0.3f, 0.0f }),
        };
    }

    // What fills each point of the axe (0 empty, 1 haft wood,
    // 2 leather grip, 3 iron head, 4 bright edge)
    static int role(glm::vec3 p) {
        float u = p.x, v = p.y, w = p.z;
        // head: a wedge from the poll to the flared edge
        if (v >= -HEAD_BACK && v <= BLADE_REACH) {
            float t = std::clamp((v - 0.0f) / BLADE_REACH, 0.0f, 1.0f);
            float half = 0.5f * (CHEEK_H + (EDGE_H - CHEEK_H) * t * t);    // flares toward the edge
            float thick = 0.5f * HEAD_T * (1.0f - 0.75f * t);              // thins toward the edge
            if (v < 0.0f) { half = 0.5f * CHEEK_H * 0.85f; thick = 0.5f * HEAD_T; }
            // a curved edge: the horns sweep back a little
            float horn = (v > BLADE_REACH - 1.2f) ? std::fabs(u - HEAD_U) * 0.25f : 0.0f;
            if (std::fabs(u - HEAD_U) <= half && std::fabs(w) <= std::max(thick, 0.3f) &&
                v <= BLADE_REACH - horn)
                return v > BLADE_REACH - 1.0f - horn ? 4 : 3;
        }
        // haft
        if (u >= -HAFT_BACK && u <= HAFT_LEN && v * v + w * w <= HAFT_R * HAFT_R)
            return (u >= -GRIP_LEN * 0.5f && u <= GRIP_LEN * 0.5f) ? 2 : 1;
        return 0;
    }

    static std::vector<PartVertex> mesh(const CharacterModel& m, HairMesh mode) {
        const glm::vec3 colours[5] = {
            glm::vec3(0), glm::vec3(138, 92, 56), glm::vec3(78, 54, 40),
            glm::vec3(118, 122, 130), glm::vec3(214, 220, 226),
        };
        std::vector<PartVertex> out;
        glm::vec3 lo(-HAFT_BACK - 0.5f, -HEAD_BACK - 0.5f, -HEAD_T), hi(HAFT_LEN + 0.5f, BLADE_REACH + 0.5f, HEAD_T);
        CharacterModel::meshVoxelShape(lo, hi, VOXEL, role, colours, mode, out);
        // axe frame → wrist frame; normals turn with it
        const glm::mat3 B(haftDir(), edgeDir(), acrossDir());
        for (PartVertex& pv : out) {
            glm::vec3 p = toWrist(m, { pv.x, pv.y, pv.z });
            glm::vec3 n = B * glm::vec3(pv.nx, pv.ny, pv.nz);
            pv.x = p.x; pv.y = p.y; pv.z = p.z;
            pv.nx = (int8_t)std::lround(std::clamp(n.x, -127.0f, 127.0f));
            pv.ny = (int8_t)std::lround(std::clamp(n.y, -127.0f, 127.0f));
            pv.nz = (int8_t)std::lround(std::clamp(n.z, -127.0f, 127.0f));
            pv.a = 3;                     // its own part, so edge lines outline it
        }
        return out;
    }
};

// ── The sword ─────────────────────────────────────────────
// A one-handed arming sword: a 30-inch blade with a fuller and
// a point, a straight crossguard, a wrapped grip for one fist
// and a round pommel. Same frame as the axe:
//   u  along the blade, from the fist toward the point
//   v  across the blade, edge to edge (the crossguard's way)
//   w  the blade's flat (its thickness)
struct SwordGeometry {
    static constexpr float GRIP_LEN  = 5.0f;    // centred on the fist
    static constexpr float POMMEL_U  = -3.4f;
    static constexpr float POMMEL_R  = 1.0f;
    static constexpr float GUARD_U   = 3.1f;    // the crossguard, just in front of the fist
    static constexpr float GUARD_HALF= 3.6f;
    static constexpr float BLADE_U0  = 3.6f;
    static constexpr float BLADE_LEN = 30.0f;
    static constexpr float BLADE_HALF= 0.85f;   // half its width at the guard
    static constexpr float BLADE_T   = 0.25f;   // half its thickness
    static constexpr float POINT_LEN = 4.0f;    // the taper to the point
    static constexpr float VOXEL     = 0.5f;

    static glm::vec3 grip(const CharacterModel& m) { return AxeGeometry::grip(m); }
    // The blade points ahead out of the fist, its edges up and down
    static glm::vec3 bladeDir() { return glm::normalize(glm::vec3(0.0f, -0.45f, 1.0f)); }
    static glm::vec3 edgeDir()  { return glm::normalize(glm::cross(acrossDir(), bladeDir())) * -1.0f; }
    static glm::vec3 acrossDir() { return glm::vec3(1.0f, 0.0f, 0.0f); }

    static glm::vec3 toWrist(const CharacterModel& m, glm::vec3 uvw) {
        return grip(m) + bladeDir() * uvw.x + edgeDir() * uvw.y + acrossDir() * uvw.z;
    }
    // Points along the blade (wrist frame): base, middle, point
    static std::array<glm::vec3, 3> bladePoints(const CharacterModel& m) {
        return { toWrist(m, { BLADE_U0 + 2.0f, 0, 0 }),
                 toWrist(m, { BLADE_U0 + BLADE_LEN * 0.55f, 0, 0 }),
                 toWrist(m, { BLADE_U0 + BLADE_LEN, 0, 0 }) };
    }

    // 0 empty, 1 grip, 2 guard and pommel, 3 blade, 4 fuller (darker), 5 edge
    static int role(glm::vec3 p) {
        float u = p.x, v = p.y, w = p.z;
        float av = std::fabs(v), aw = std::fabs(w);
        // blade
        if (u >= BLADE_U0 && u <= BLADE_U0 + BLADE_LEN) {
            float t = (u - BLADE_U0) / BLADE_LEN;
            float half = BLADE_HALF * (1.0f - 0.25f * t);                 // narrows a little
            float tipStart = BLADE_U0 + BLADE_LEN - POINT_LEN;
            if (u > tipStart) half *= 1.0f - (u - tipStart) / POINT_LEN;
            if (av <= std::max(half, 0.2f) && aw <= BLADE_T + 0.05f) {
                if (av > half - 0.3f) return 5;
                if (av < 0.3f && u < BLADE_U0 + BLADE_LEN * 0.7f) return 4;
                return 3;
            }
            return 0;
        }
        // crossguard
        if (std::fabs(u - GUARD_U) <= 0.5f && av <= GUARD_HALF && aw <= 0.55f) return 2;
        // grip
        if (u >= -GRIP_LEN * 0.5f - 0.3f && u <= GUARD_U && av * av + aw * aw <= 0.6f * 0.6f) return 1;
        // pommel
        glm::vec3 d(u - POMMEL_U, v, w);
        if (glm::dot(d, d) <= POMMEL_R * POMMEL_R) return 2;
        return 0;
    }

    static std::vector<PartVertex> mesh(const CharacterModel& m, HairMesh mode) {
        const glm::vec3 colours[6] = {
            glm::vec3(0), glm::vec3(70, 48, 36), glm::vec3(150, 128, 82),
            glm::vec3(176, 182, 192), glm::vec3(128, 134, 146), glm::vec3(226, 232, 238),
        };
        std::vector<PartVertex> out;
        glm::vec3 lo(POMMEL_U - POMMEL_R - 0.5f, -GUARD_HALF - 0.5f, -POMMEL_R - 0.5f);
        glm::vec3 hi(BLADE_U0 + BLADE_LEN + 0.5f, GUARD_HALF + 0.5f, POMMEL_R + 0.5f);
        CharacterModel::meshVoxelShape(lo, hi, VOXEL, role, colours, mode, out);
        const glm::mat3 B(bladeDir(), edgeDir(), acrossDir());
        for (PartVertex& pv : out) {
            glm::vec3 p = toWrist(m, { pv.x, pv.y, pv.z });
            glm::vec3 n = B * glm::vec3(pv.nx, pv.ny, pv.nz);
            pv.x = p.x; pv.y = p.y; pv.z = p.z;
            pv.nx = (int8_t)std::lround(std::clamp(n.x, -127.0f, 127.0f));
            pv.ny = (int8_t)std::lround(std::clamp(n.y, -127.0f, 127.0f));
            pv.nz = (int8_t)std::lround(std::clamp(n.z, -127.0f, 127.0f));
            pv.a = 3;
        }
        return out;
    }
};

inline std::vector<PartVertex> heldItemMesh(HeldModel which, const CharacterModel& m, HairMesh mode) {
    switch (which) {
    case HeldModel::Axe:   return AxeGeometry::mesh(m, mode);
    case HeldModel::Sword: return SwordGeometry::mesh(m, mode);
    default:             return {};
    }
}
