#pragma once
// ══════════════════════════════════════════════════════════
// A gate: a portal's frame in the world (docs/dungeons.md "Portals")
// ══════════════════════════════════════════════════════════
// An obsidian arch standing on a little cobblestone footing, its
// opening INNER_W wide and INNER_H tall. Open, the opening is filled
// with the veil (portal_veil: no collision, a violet glow); walk into
// it and you're taken through (the game: main.cpp travel). Shut, the
// opening is air.
//
//   base   the middle of the bottom of the opening (the voxel the
//          veil starts in, on the ground)
//   axis   0: the opening spans x and faces +z / -z;
//          1: it spans z and faces +x / -x
//
// The first version (one world loaded at a time): the gate by the
// main world's spawn (world/Realm.h), the one in a dungeon's portal
// room and its exit (dungeon/Run.h).

#include "../chunk/ChunkManager.h"
#include "../voxel/VoxelRegistry.h"
#include "../world/WorldEditor.h"
#include "../entity/Human.h"
#include "../world/PortalLinks.h"
#include <glm/glm.hpp>
#include <cmath>

struct Gate {
    static constexpr int INNER_W = 10, INNER_H = 22, FRAME = 2;   // (a body is 17 tall)
    static constexpr int CLEAR = 4;            // voxels cleared round it (and its footing) either side
    static constexpr float TRIGGER = 0.9f;     // vox from the veil's plane a body's middle must come within

    glm::ivec3 base{ 0 };
    int axis = 0;
    int side = 1;                              // +1: it faces +z (+x); -1: -z (-x)

    // world voxel at (u across, y up, d through) from the base
    glm::ivec3 at(int u, int y, int d) const {
        return axis == 0 ? base + glm::ivec3(u, y, d) : base + glm::ivec3(d, y, u);
    }
    glm::vec3 normal() const { return (axis == 0 ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0)) * (float)side; }
    glm::vec3 centre() const { return glm::vec3(base) + glm::vec3(0.5f, INNER_H * 0.5f, 0.5f); }
    // where someone coming through stands: a few steps out in front
    glm::vec3 arrival(float out = 7.0f) const {
        return glm::vec3(base) + (axis == 0 ? glm::vec3(0.0f, 0.0f, 0.5f) : glm::vec3(0.5f, 0.0f, 0.0f)) + normal() * out;
    }
    // the facing they arrive with (away from the gate)
    glm::vec3 arrivalFacing() const { return normal(); }

    static bool inner(int u, int y) { return u >= -INNER_W / 2 && u < INNER_W / 2 && y >= 0 && y < INNER_H; }
    static bool frame(int u, int y) {
        return u >= -INNER_W / 2 - FRAME && u < INNER_W / 2 + FRAME && y >= -1 && y < INNER_H + FRAME && !inner(u, y);
    }

    // Is a body walking into the veil? (its middle at the plane,
    // inside the opening)
    bool touches(const Human& h) const {
        glm::vec3 p = h.position;
        glm::vec3 c = glm::vec3(base) + (axis == 0 ? glm::vec3(0.0f, 0.0f, 0.5f) : glm::vec3(0.5f, 0.0f, 0.0f));
        float across = axis == 0 ? p.x - c.x : p.z - c.z;
        float through = axis == 0 ? p.z - c.z : p.x - c.x;
        float up = p.y - (float)base.y;
        return std::fabs(across) < INNER_W * 0.5f - 0.5f && std::fabs(through) < TRIGGER && up > -1.0f && up < INNER_H - 4.0f;
    }
    // close enough to use (interact): within USE_NEAR of its middle, or
    // within USE_REACH looking at it ('look': where the camera looks;
    // zero: distance only)
    static constexpr float USE_NEAR = 12.0f, USE_REACH = 36.0f;   // vox (a body is 17 tall)
    bool near(glm::vec3 p, glm::vec3 look = glm::vec3(0.0f)) const {
        if (std::fabs(p.y - (float)base.y) > INNER_H) return false;
        float d = glm::length(glm::vec2(p.x, p.z) - glm::vec2(centre().x, centre().z));
        if (d < USE_NEAR) return true;
        if (d > USE_REACH || glm::length(look) < 1e-3f) return false;
        glm::vec3 eye = p + glm::vec3(0.0f, 15.0f, 0.0f);
        glm::vec3 to = glm::normalize(centre() - eye);
        return glm::dot(to, glm::normalize(look)) > 0.8f;
    }
    // are its chunks here, to build in?
    bool loaded(const ChunkManager& w) const {
        const int r = INNER_W / 2 + FRAME + CLEAR;
        for (int u : { -r, r }) for (int d : { -CLEAR - 1, CLEAR + 1 }) {
            glm::ivec3 v = at(u, 0, d);
            if (!w.isColumnLoaded(v.x, v.z)) return false;
        }
        return true;
    }

    // ── Building ──────────────────────────────────────────
    // The arch, cleared round, on a footing down to the ground (open:
    // with its veil)
    //   clear  voxels cleared round it (less inside a built room)
    void build(EditBatch& b, const ChunkManager& w, bool open, int clear = CLEAR) const {
        const uint16_t OBS = gRegistry.getIDByName("obsidian"), COBBLE = gRegistry.getIDByName("cobblestone");
        const int r = INNER_W / 2 + FRAME + clear;
        for (int u = -r; u < r; u++)
            for (int d = -clear; d <= clear; d++) {
                // the footing: cobblestone under it down to solid ground (8 at most)
                for (int y = -1; y >= -8; y--) {
                    glm::ivec3 v = at(u, y, d);
                    uint16_t id = w.typeAtVoxel(v.x, v.y, v.z);
                    if (id != BLOCK_AIR && id != BLOCK_WATER && gRegistry.get(id).isSolid()) break;
                    b.set(v.x, v.y, v.z, COBBLE);
                }
                // air above, the arch in it
                for (int y = 0; y < INNER_H + FRAME + 3; y++) {
                    glm::ivec3 v = at(u, y, d);
                    uint16_t want = BLOCK_AIR;
                    if ((d == 0 || d == -1) && frame(u, y)) want = OBS;
                    else if (d == 0 && inner(u, y) && open) want = veilId();
                    b.set(v.x, v.y, v.z, want);
                }
                glm::ivec3 f = at(u, -1, d);
                b.set(f.x, f.y, f.z, (d == 0 || d == -1) && frame(u, -1) ? OBS : COBBLE);
            }
    }
    // Open or shut the veil (the arch stays)
    void setOpen(EditBatch& b, const ChunkManager& w, bool open) const {
        const uint16_t V = veilId();
        for (int u = -INNER_W / 2; u < INNER_W / 2; u++)
            for (int y = 0; y < INNER_H; y++) {
                glm::ivec3 v = at(u, y, 0);
                uint16_t now = w.typeAtVoxel(v.x, v.y, v.z);
                if (open && now == BLOCK_AIR) b.set(v.x, v.y, v.z, V);
                if (!open && now == V) b.set(v.x, v.y, v.z, BLOCK_AIR);
            }
    }
    bool isOpen(const ChunkManager& w) const {
        glm::ivec3 v = at(0, INNER_H / 2, 0);
        return w.typeAtVoxel(v.x, v.y, v.z) == veilId();
    }
    bool isBuilt(const ChunkManager& w) const {
        glm::ivec3 v = at(-INNER_W / 2 - 1, INNER_H / 2, 0);
        return w.typeAtVoxel(v.x, v.y, v.z) == gRegistry.getIDByName("obsidian");
    }
    // The opening as a portal end (world/PortalLinks.h): its front face,
    // the side you come out on
    PortalEnd end() const {
        PortalEnd e;
        // (the opening spans u = -W/2 .. W/2 voxels across: its middle is on
        // the base voxel's edge across, its middle through)
        e.centre = glm::vec3(base) + (axis == 0 ? glm::vec3(0.0f, INNER_H * 0.5f, 0.5f) : glm::vec3(0.5f, INNER_H * 0.5f, 0.0f));
        e.normal = normal();
        e.up = glm::vec3(0, 1, 0);
        e.half = glm::vec2(INNER_W * 0.5f, INNER_H * 0.5f);
        return e;
    }
    static uint16_t veilId() { static const uint16_t v = gRegistry.getIDByName("portal_veil"); return v; }
};
