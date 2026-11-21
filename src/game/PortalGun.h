#pragma once
// ══════════════════════════════════════════════════════════
// The portal gun: a dev tool for trying seamless portals anywhere
// ══════════════════════════════════════════════════════════
// (items.toml dev_portal_gun, dev_tool = "portal"; Test Play)
//   left click    puts the blue end on the wall you're looking at
//   right click   the orange end
//   middle click  takes both away
// With both ends up they're a portal link (world/PortalLinks.h): look
// through one to see out of the other, walk in one and out the other.
// Console: portalgun [oneway|twoway|clear] (one way: only blue opens;
// orange is a plain wall from its side).
//
// Aimed at the ground, an end stands there like a doorframe, facing
// you, and opens on both sides: with both ends standing, in at one's
// back is out of the other's back (PortalEnd::Back::Through); with the
// other end on a wall, either side of it leads out of the wall's
// (Turn). It needs air all through and a little either side.
//
// Aimed at a wall, an end goes on it (one sided: the wall's behind it),
// a 10 x 22 opening standing on the floor below where you aimed (or
// centred on it in mid-wall), slid along the wall a little if it
// doesn't quite fit; the whole opening needs wall behind it and air in
// front. Not on ceilings.

#include "../world/PortalLinks.h"
#include "../chunk/ChunkManager.h"
#include "../voxel/VoxelRegistry.h"
#include <glm/glm.hpp>
#include <cmath>
#include <string>

struct PortalGun {
    static constexpr float RANGE = 240.0f;           // vox
    static constexpr glm::vec2 HALF{ 5.0f, 11.0f };  // the opening: 10 wide, 22 tall (a body is 17)
    static constexpr int FLOOR_SEARCH = 24;          // vox below the aim to look for a floor

    bool      have[2] = { false, false };
    PortalEnd ends[2];
    bool      twoWay = true;
    uint32_t  linkId = 0;

    static bool solidAt(const ChunkManager& w, int x, int y, int z) {
        uint16_t id = w.typeAtVoxel(x, y, z);
        return id != BLOCK_AIR && gRegistry.get(id).isSolid();
    }

    // Where an end goes for a shot from 'eye' along 'dir' (false and why)
    static bool aim(const ChunkManager& w, glm::vec3 eye, glm::vec3 dir, PortalEnd& out, std::string& why) {
        dir = glm::normalize(dir);
        // walk the voxels to the first solid one
        glm::ivec3 cell(glm::floor(eye)), step(0);
        glm::vec3 tMax(1e30f), tDelta(1e30f);
        for (int a = 0; a < 3; a++) {
            if (dir[a] > 0.0f) { step[a] = 1; tDelta[a] = 1.0f / dir[a]; tMax[a] = ((float)cell[a] + 1.0f - eye[a]) / dir[a]; }
            else if (dir[a] < 0.0f) { step[a] = -1; tDelta[a] = -1.0f / dir[a]; tMax[a] = ((float)cell[a] - eye[a]) / dir[a]; }
        }
        float t = 0.0f;
        glm::ivec3 n(0);
        bool hit = false;
        while (t <= RANGE) {
            if (solidAt(w, cell.x, cell.y, cell.z)) { hit = true; break; }
            int a = tMax.x < tMax.y ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
            t = tMax[a]; tMax[a] += tDelta[a]; cell[a] += step[a];
            n = glm::ivec3(0); n[a] = -step[a];
        }
        if (!hit) { why = "nothing to put it on"; return false; }
        if (n.y > 0) return stand(w, eye, eye + dir * t, cell.y + 1, out, why);
        if (n.y != 0 || n == glm::ivec3(0)) { why = "not on ceilings"; return false; }
        glm::vec3 normal(n);
        glm::vec3 hitAt = eye + dir * t;
        // the surface: the face of the voxel hit
        const int axis = n.x != 0 ? 0 : 2;
        float surface = (float)cell[axis] + (n[axis] > 0 ? 1.0f : 0.0f);
        glm::vec3 up(0, 1, 0), right = glm::normalize(glm::cross(up, normal));
        // the voxel in front of the wall at height y, along-wall offset u from 'base'
        auto frontVox = [&](glm::vec3 base, float u, float y) {
            glm::vec3 p = base + right * u; p[axis] = surface + normal[axis] * 0.5f; p.y = y;
            return glm::ivec3(glm::floor(p));
        };
        auto wallVox = [&](glm::vec3 base, float u, float y) {
            glm::vec3 p = base + right * u; p[axis] = surface - normal[axis] * 0.5f; p.y = y;
            return glm::ivec3(glm::floor(p));
        };
        // stand it on the floor below the aim, if there's one near
        float bottom = std::floor(hitAt.y - HALF.y);
        for (int dy = 0; dy <= FLOOR_SEARCH; dy++) {
            glm::ivec3 v = frontVox(hitAt, 0.0f, std::floor(hitAt.y) - dy + 0.5f);
            if (solidAt(w, v.x, v.y, v.z)) { bottom = (float)v.y + 1.0f; break; }
        }
        // does it fit (slid along the wall a little if it must)?
        auto fits = [&](glm::vec3 base, std::string& whyNot) {
            for (float u = -HALF.x + 0.5f; u < HALF.x; u += 1.0f)
                for (float y = bottom + 0.5f; y < bottom + 2.0f * HALF.y; y += 1.0f) {
                    glm::ivec3 wv = wallVox(base, u, y), fv = frontVox(base, u, y);
                    if (!solidAt(w, wv.x, wv.y, wv.z)) { whyNot = "not enough wall there"; return false; }
                    if (solidAt(w, fv.x, fv.y, fv.z)) { whyNot = "something's in the way in front"; return false; }
                }
            return true;
        };
        std::string whyNot;
        for (float slide : { 0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 3.0f, -3.0f, 4.0f, -4.0f }) {
            glm::vec3 base = hitAt + right * slide;
            // snap along the wall to the voxel grid's middle, so it lines up with the blocks
            glm::vec3 snapped = base;
            snapped[2 - axis] = std::floor(base[2 - axis]) + 0.5f;
            if (!fits(snapped, whyNot)) continue;
            out = PortalEnd();
            out.normal = normal; out.up = up; out.half = HALF;
            out.centre = snapped;
            out.centre[axis] = surface;
            out.centre.y = bottom + HALF.y;
            return true;
        }
        why = whyNot;
        return false;
    }

    // Standing on the ground at 'at' (floor top 'floorY'), facing 'eye'
    static bool stand(const ChunkManager& w, glm::vec3 eye, glm::vec3 at, int floorY, PortalEnd& out, std::string& why) {
        glm::vec3 n = eye - at; n.y = 0.0f;
        if (glm::length(n) < 0.5f) { why = "too close"; return false; }
        n = glm::normalize(n);
        PortalEnd e;
        e.normal = n; e.up = glm::vec3(0, 1, 0); e.half = HALF;
        e.centre = glm::vec3(std::floor(at.x) + 0.5f, (float)floorY + HALF.y, std::floor(at.z) + 0.5f);
        e.back = PortalEnd::Back::Through;
        // air all through it and a little either side
        for (float u = -HALF.x + 0.5f; u < HALF.x; u += 1.0f)
            for (float y = 0.5f; y < 2.0f * HALF.y; y += 1.0f)
                for (float d : { -1.5f, -0.5f, 0.5f, 1.5f }) {
                    glm::vec3 p = e.centre + e.right() * u + n * d; p.y = (float)floorY + y;
                    glm::ivec3 v(glm::floor(p));
                    if (solidAt(w, v.x, v.y, v.z)) { why = "something's in the way"; return false; }
                }
        out = e;
        return true;
    }

    // Put end 0 (blue) or 1 (orange) there; with both up, the link
    void place(int which, const PortalEnd& e, PortalLinks& links) {
        ends[which] = e; have[which] = true;
        relink(links);
    }
    void clear(PortalLinks& links) {
        if (linkId) links.remove(linkId);
        linkId = 0; have[0] = have[1] = false;
    }
    void setTwoWay(bool on, PortalLinks& links) { twoWay = on; relink(links); }
    void relink(PortalLinks& links) {
        if (linkId) links.remove(linkId);
        linkId = 0;
        if (!(have[0] && have[1])) return;
        PortalEnd a = ends[0], b = ends[1];
        // a standing end's back: straight through to the other's back if
        // that one stands too, else out of the wall one's front
        auto backOf = [](const PortalEnd& e, const PortalEnd& o) {
            if (e.back == PortalEnd::Back::Wall) return PortalEnd::Back::Wall;
            return o.back == PortalEnd::Back::Wall ? PortalEnd::Back::Turn : PortalEnd::Back::Through;
        };
        a.back = backOf(ends[0], ends[1]);
        b.back = backOf(ends[1], ends[0]);
        linkId = links.add(a, b, twoWay, "gun").id;
    }
};
