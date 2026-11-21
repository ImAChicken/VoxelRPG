#pragma once
// ══════════════════════════════════════════════════════════
// Portal links: openings in one place that are another place
// ══════════════════════════════════════════════════════════
// docs/dungeons.md "Portals", step 7: seamless portals. A link joins
// two ends; an end is a flat rectangular opening on a surface (a wall
// for now), seen from the side its normal points to. What's behind
// one end is drawn as the room in front of the other (main.cpp: a
// second camera, clipped to the opening), and a body walking into one
// comes out of the other, still moving, turned to match.
//
//   one way   only end a can be gone through, and only a shows the far
//             side; b is a plain wall from its own room (join portals:
//             "show the other side from one wall")
//   two way   both
//
// An end has two faces. Its front always leads to the other end's
// front; what its back does is the end's 'back' (PortalEnd::Back):
//   Wall      nothing: from behind it's whatever the portal stands in
//             (a wall; a gate's arch)
//   Through   straight through, as a doorway standing in the open
//             would: in at A's back, out of B's back. Both ends need
//             room on both sides (the portal gun's, standing on the
//             ground)
//   Turn      either face of A leads out of B's front (B against a
//             wall: the dungeon gate by the spawn)
// A back that goes somewhere else entirely is a second link whose end
// is the same opening turned round (PortalEnd::flipped()), back Wall.
// faces() lists each way in: the face gone into and the one come out of;
// everything (crossing, holes, drawing) goes by faces.
//
// An end's 'world' is "" for this world, or another open one
// (server/Worlds.h): the dungeon gates.
//
// Bodies have to reach the opening's plane to cross it, so the wall
// behind an end has a hole for bodies only (passable(): the voxels are
// still there, drawn and hit by rays; physics walks through them).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

struct PortalEnd {
    std::string world;                       // "" : this world
    glm::vec3   centre{ 0.0f };              // middle of the opening, on the surface
    glm::vec3   normal{ 0.0f, 0.0f, 1.0f };  // out of the surface, into the room it's seen from
    glm::vec3   up{ 0.0f, 1.0f, 0.0f };
    glm::vec2   half{ 5.0f, 11.0f };         // half its width and height (vox)
    enum class Back : uint8_t { Wall, Through, Turn };
    Back        back = Back::Wall;

    // the same opening seen from behind (its back face)
    PortalEnd flipped() const { PortalEnd f = *this; f.normal = -normal; return f; }

    glm::vec3 right() const { return glm::normalize(glm::cross(up, normal)); }
    // local (right, up, normal) → world
    glm::mat4 frame() const {
        glm::mat4 m(1.0f);
        m[0] = glm::vec4(right(), 0.0f);
        m[1] = glm::vec4(up, 0.0f);
        m[2] = glm::vec4(normal, 0.0f);
        m[3] = glm::vec4(centre, 1.0f);
        return m;
    }
    // a point in the opening's terms: x across, y up, z out (in front > 0)
    glm::vec3 local(glm::vec3 p) const {
        glm::vec3 d = p - centre;
        return { glm::dot(d, right()), glm::dot(d, up), glm::dot(d, normal) };
    }
    bool inside(glm::vec3 l, float margin = 0.0f) const {
        return std::fabs(l.x) <= half.x + margin && std::fabs(l.y) <= half.y + margin;
    }
    // the plane, as (n, -n·c): in front ≥ 0
    glm::vec4 plane() const { return glm::vec4(normal, -glm::dot(normal, centre)); }
    glm::vec3 corner(float sx, float sy) const { return centre + right() * (sx * half.x) + up * (sy * half.y); }
};

struct PortalLink {
    uint32_t  id = 0;
    PortalEnd a, b;
    bool      twoWay = true;
    std::string kind = "gun";                // what made it ("gun", later doors, windows, gates)

    // The way through: something in front of 'from', moving into it,
    // comes out in front of 'to' moving out of it (a half turn about
    // the opening's up between the two frames)
    static glm::mat4 through(const PortalEnd& from, const PortalEnd& to) {
        glm::mat4 half(1.0f);
        half[0][0] = -1.0f; half[2][2] = -1.0f;   // x → -x, z → -z
        return to.frame() * half * glm::inverse(from.frame());
    }
    // may this end be gone through / show the far side?
    bool opens(int end) const { return end == 0 || twoWay; }
    const PortalEnd& end(int i) const { return i == 0 ? a : b; }
    const PortalEnd& other(int i) const { return i == 0 ? b : a; }

    // The ways in at end e: the face gone into, the face come out of
    // (0 to 2 of them: front, back)
    struct Face { PortalEnd in, out; bool back = false; };
    int faces(int e, Face out[2]) const {
        if (!opens(e)) return 0;
        const PortalEnd& E = end(e);
        const PortalEnd& O = other(e);
        int n = 0;
        out[n++] = { E, O, false };
        if (E.back == PortalEnd::Back::Through) out[n++] = { E.flipped(), O.flipped(), true };
        else if (E.back == PortalEnd::Back::Turn) out[n++] = { E.flipped(), O, true };
        return n;
    }
};

class PortalLinks {
public:
    static constexpr float HOLE_DEPTH = 3.0f;   // vox behind an end bodies may go (the body's half width and more)
    static constexpr float BODY_BAND  = 4.0f;   // vox from a face a body's middle is still part way through it (drawn cut, and a copy)

    std::vector<PortalLink> links;

    PortalLink& add(const PortalEnd& a, const PortalEnd& b, bool twoWay, const std::string& kind) {
        PortalLink l; l.id = ++nextId; l.a = a; l.b = b; l.twoWay = twoWay; l.kind = kind;
        links.push_back(l);
        return links.back();
    }
    PortalLink* find(uint32_t id) { for (auto& l : links) if (l.id == id) return &l; return nullptr; }
    PortalLink* findKind(const std::string& kind) { for (auto& l : links) if (l.kind == kind) return &l; return nullptr; }
    void removeKind(const std::string& kind) { links.erase(std::remove_if(links.begin(), links.end(), [&](const PortalLink& l) { return l.kind == kind; }), links.end()); }
    void remove(uint32_t id) { links.erase(std::remove_if(links.begin(), links.end(), [&](const PortalLink& l) { return l.id == id; }), links.end()); }
    void clear() { links.clear(); }
    bool empty() const { return links.empty(); }

    // A body's middle moved from p0 to p1: did it go into an end it may
    // go through? (which link, which end; where it crossed)
    //   world   the other end's world ("": this one): the body goes there
    //           (server/Worlds.h moves it; Simulation queues it)
    struct Crossing { const PortalLink* link = nullptr; int end = 0; bool back = false; glm::mat4 through{ 1.0f }; std::string world;
                     glm::vec3 exitCentre{ 0.0f }, exitNormal{ 0.0f }; };
    Crossing crossed(glm::vec3 p0, glm::vec3 p1) const {
        Crossing c;
        PortalLink::Face f[2];
        for (const PortalLink& l : links)
            for (int e = 0; e < 2; e++) {
                if (!l.end(e).world.empty()) continue;          // (an end in another world: crossed there)
                for (int i = 0, n = l.faces(e, f); i < n; i++) {
                    const PortalEnd& E = f[i].in;
                    glm::vec3 a = E.local(p0), b = E.local(p1);
                    if (!(a.z > 0.0f && b.z <= 0.0f)) continue; // in front, then at or behind it
                    float t = a.z / std::max(a.z - b.z, 1e-6f);
                    glm::vec3 at = a + (b - a) * t;
                    if (!E.inside(at, -0.3f)) continue;
                    c.link = &l; c.end = e; c.back = f[i].back;
                    c.through = PortalLink::through(E, f[i].out);
                    c.world = f[i].out.world;
                    c.exitCentre = f[i].out.centre; c.exitNormal = f[i].out.normal;
                    return c;
                }
            }
        return c;
    }
    // Behind an end that opens: a hole in the wall, for bodies only
    bool passable(int x, int y, int z) const {
        if (links.empty()) return false;
        glm::vec3 p(x + 0.5f, y + 0.5f, z + 0.5f);
        PortalLink::Face f[2];
        for (const PortalLink& l : links)
            for (int e = 0; e < 2; e++) {
                if (!l.end(e).world.empty()) continue;
                for (int i = 0, n = l.faces(e, f); i < n; i++) {
                    glm::vec3 q = f[i].in.local(p);
                    if (q.z < 0.5f && q.z > -HOLE_DEPTH && f[i].in.inside(q, -0.5f)) return true;
                }
            }
        return false;
    }
private:
    uint32_t nextId = 0;
};
