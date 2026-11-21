#pragma once
// ══════════════════════════════════════════════════════════
// Drawing through a portal (world/PortalLinks.h)
// ══════════════════════════════════════════════════════════
// The world is drawn as usual; then for each opening facing the camera:
//   begin()   the opening is marked in the stencil (where it isn't
//             hidden behind something) and its depth there pushed to
//             the far plane, so the far side can draw into it
//   …         the far side, from cameraThrough(): where the camera
//             would be on the other side, with its near plane leaned
//             onto the other end (oblique(): the wall that end stands
//             on, and anything behind it, never shows), stencil-tested
//             so it only lands inside the opening
//   end()     the opening's own depth put back, so whatever is drawn
//             after it (particles, the HUD's 3D bits) sits right
// One level deep: a portal seen through a portal is a plain wall.
// The rim round each opening (drawRim) is the portal's colour.

#include "BoxRenderer.h"
#include "Camera.h"
#include "../world/PortalLinks.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdlib>

namespace portalview {

// Facing the camera, near enough to bother?
inline bool facing(const PortalEnd& E, glm::vec3 eye, float range) {
    glm::vec3 to = eye - E.centre;
    return glm::dot(to, E.normal) > 0.02f && glm::length(to) < range;
}

// The camera on the far side: moved and turned by the way through
inline Camera cameraThrough(const Camera& c, const glm::mat4& through) {
    Camera v = c;
    v.position = glm::vec3(through * glm::vec4(c.position, 1.0f));
    glm::vec3 f = glm::normalize(glm::mat3(through) * c.front);
    float yaw = glm::degrees(std::atan2(f.z, f.x));
    float pitch = glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f)));
    v.setLook(yaw, pitch);
    return v;
}

// A projection whose near plane is the given world plane (n, d: kept
// where n·x + d ≥ 0), for this view (Lengyel's oblique near plane)
inline glm::mat4 oblique(glm::mat4 P, const glm::mat4& view, glm::vec4 planeWorld) {
    glm::vec4 c = glm::transpose(glm::inverse(view)) * planeWorld;   // the plane in view space
    if (c.w > 0.0f) return P;   // the camera is in front of it: nothing to cut (keep the usual near plane)
    auto sgn = [](float x) { return x > 0.0f ? 1.0f : x < 0.0f ? -1.0f : 0.0f; };
    glm::vec4 q = glm::inverse(P) * glm::vec4(sgn(c.x), sgn(c.y), 1.0f, 1.0f);
    c *= 2.0f / glm::dot(c, q);
    P[0][2] = c.x; P[1][2] = c.y; P[2][2] = c.z + 1.0f; P[3][2] = c.w;
    return P;
}

// The opening as a thin box on the surface (BoxRenderer: base is the
// bottom middle, +X along the yaw)
//   out     how far in front of the surface its face is
//   behind  how deep it goes back behind the surface: a box, not a
//           flat card, so walking through (the camera's near plane
//           crossing the opening) it still covers the view and the far
//           side never blinks out
inline void drawOpening(BoxRenderer& boxes, const glm::mat4& vp, const PortalEnd& E, float out, float behind = 0.0f) {
    glm::vec3 r = E.right();
    float yaw = glm::degrees(std::atan2(r.z, r.x));
    float depth = out + behind;
    glm::vec3 base = E.centre - E.up * E.half.y + E.normal * (out - depth * 0.5f);
    const float in = behind > 0.0f ? 0.15f : 0.0f;          // (a hair inside the frame's own faces)
    base += E.up * in;
    boxes.draw(vp, base, { E.half.x * 2.0f - in * 2.0f, E.half.y * 2.0f - in * 2.0f, std::max(depth, 0.02f) }, yaw, glm::vec3(0.0f), true);
}
// The planes a view through O (from eye, on its far side) can see
// between: O's own surface and the four from the eye past its edges.
// 0 when the eye is too near the surface to tell (cull by the screen).
inline int openingPlanes(const PortalEnd& O, glm::vec3 eye, glm::vec4 out[5]) {
    glm::vec4 pl = O.plane();
    float d = glm::dot(glm::vec3(pl), eye) + pl.w;
    if (std::abs(d) < 0.75f) return 0;
    if (d > 0.0f) pl = -pl;                                  // (keep what's past the surface from the eye)
    out[0] = pl;
    glm::vec3 r = O.right() * O.half.x, u = O.up * O.half.y;
    glm::vec3 c[4] = { O.centre - r - u, O.centre + r - u, O.centre + r + u, O.centre - r + u };
    glm::vec3 inside = O.centre + (O.centre - eye);
    for (int i = 0; i < 4; i++) {
        glm::vec3 n = glm::cross(c[i] - eye, c[(i + 1) % 4] - eye);
        float w = -glm::dot(n, eye);
        if (glm::dot(n, inside) + w < 0.0f) { n = -n; w = -w; }
        out[1 + i] = glm::vec4(n, w);
    }
    return 5;
}
constexpr float OPENING_OUT = 0.05f, OPENING_BEHIND = 1.2f;

// The deep box behind the surface is only for the camera passing
// through it (its near plane cutting the opening): from further off its
// sides would show the far side past the edges of the frame, where no
// line of sight goes through the opening (a sliver beside a gate's arch)
inline bool passingThrough(const PortalEnd& E, glm::vec3 eye) {
    glm::vec3 l = E.local(eye);
    return l.z < 1.0f && l.z > -OPENING_BEHIND && E.inside(l, 0.0f);
}
// The camera can be a little past an opening before the body is (the
// eye ahead of the body's middle, which is what crosses): until the body
// goes through, the view through the opening fills that tunnel behind it
// (main.cpp: a 'tunnel' view; begin's behind)
constexpr float TUNNEL = 4.0f;
inline void begin(BoxRenderer& boxes, const glm::mat4& vp, const PortalEnd& E, glm::vec3 eye, float behindOverride = -1.0f) {
    const float behind = behindOverride >= 0.0f ? behindOverride : passingThrough(E, eye) ? OPENING_BEHIND : 0.0f;
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    // 1. mark the opening where it shows
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -8.0f);          // (in front of the wall it's on, even far off)
    // (only its face where you're in front; the box behind the surface
    // only once you're close, walking through)
    drawOpening(boxes, vp, E, OPENING_OUT, behind);
    glDisable(GL_POLYGON_OFFSET_FILL);
    // 2. there, the depth goes to the far plane
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_ALWAYS);
    glDepthRange(1.0, 1.0);
    drawOpening(boxes, vp, E, OPENING_OUT, behind);
    glDepthRange(0.0, 1.0);
    glDepthFunc(GL_LESS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // (the far side draws with the stencil test on: EQUAL 1)
}

inline void end(BoxRenderer& boxes, const glm::mat4& vp, const PortalEnd& E) {
    // the opening's own depth back over what the far side left
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_ALWAYS);
    drawOpening(boxes, vp, E, OPENING_OUT, 0.0f);
    glDepthFunc(GL_LESS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
}

// A glowing rim round the opening, in the portal's colour
inline void drawRim(BoxRenderer& boxes, const glm::mat4& vp, const PortalEnd& E, glm::vec3 col) {
    glm::vec3 r = E.right();
    float yaw = glm::degrees(std::atan2(r.z, r.x));
    const float T = 0.5f, D = 0.25f;
    glm::vec3 out = E.normal * 0.06f;
    // the sides
    for (float s : { -1.0f, 1.0f }) {
        glm::vec3 base = E.centre + r * (s * (E.half.x + T * 0.5f)) - E.up * (E.half.y + T) + out;
        boxes.draw(vp, base, { T, E.half.y * 2.0f + T * 2.0f, D }, yaw, col);
    }
    // top and bottom
    for (float s : { -1.0f, 1.0f }) {
        glm::vec3 base = E.centre + E.up * (s > 0 ? E.half.y : -E.half.y - T) + out;
        boxes.draw(vp, base, { E.half.x * 2.0f, T, D }, yaw, col);
    }
}

} // namespace portalview
