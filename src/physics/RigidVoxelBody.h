#pragma once
#include "VoxelBall.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ══════════════════════════════════════════════════════════
// RigidVoxelBody — a loose, heavy piece of voxels (a felled tree)
// ══════════════════════════════════════════════════════════
// Made from voxels taken out of the world. Simulated as a few
// dozen to ~160 weighted balls (each standing for a 3×3×3 or
// larger cell of voxels) that are integrated, pushed out of the
// voxel world, then snapped back into the body's exact shape at
// the best-fitting rotation ("shape matching"). So it falls,
// tips over, hits the ground and rolls like one rigid thing,
// and stays stable on any terrain.
//
// Its voxels never change while it moves: it is drawn from its
// original voxels with transform(), and occupies() says whether
// a world point is inside it (so bodies, the axe and dropped
// items can touch it). Once still it sleeps until woken; it
// stays a loose object for good (it is never written back into
// the world's voxels).
//
// Units: voxels, seconds, kilograms. Headless.
struct VoxelPiece {
    glm::ivec3 p;      // where the voxel was in the world
    uint16_t   id;     // its block
};

class RigidVoxelBody {
public:
    // ── Tuning ────────────────────────────────────────────
    static constexpr float KG_PER_VOXEL = 0.6f;     // a 4-inch cube of green wood
    static constexpr float GRAVITY      = 80.0f;    // vox/s², as the ragdoll
    static constexpr int   SUBSTEPS     = 2;
    static constexpr int   ITERATIONS   = 3;
    static constexpr float DAMPING      = 0.998f;   // per substep
    static constexpr float FRICTION     = 0.5f;     // sliding speed lost per substep on contact
    static constexpr float STICK_SPEED  = 3.0f;     // vox/s: slower sliding than this stops dead (static friction)
    static constexpr int   MAX_POINTS   = 160;
    static constexpr int   REST_CONTACTS = 3;       // points touching before rest damping applies
    static constexpr float REST_SPEED   = 1e6f;     // grounded bodies are always damped: a heavy log lands with a thud
    static constexpr float REST_DAMP    = 0.92f;    // speed kept per substep while resting
    static constexpr float STUCK_WEIGHT = 30.0f;    // how much more a point held by friction counts in the fit
    // The hinge: the uncut wood at the base holds the cut to the
    // stump while the tree tips, so it rotates over its base
    // instead of spinning free; it breaks once the trunk leans
    // past HINGE_BREAK (cos of the angle from upright), touches
    // the ground away from the cut, or after HINGE_TIME, and the
    // log is free
    static constexpr float HINGE_BREAK  = 0.26f;    // ~75°
    static constexpr float HINGE_TIME   = 3.0f;
    static constexpr float STILL_SPEED  = 1.2f;     // vox/s
    static constexpr float STILL_TIME   = 0.6f;     // s still before it counts as settled
    static constexpr float MAX_LIFE     = 15.0f;    // s, settles regardless

    // Build from voxels. push: the way it should tip (only the
    // horizontal part counts); pivot: where it is cut (it tips
    // over this point); tip: starting angular speed, rad/s.
    void build(const std::vector<VoxelPiece>& voxels, glm::vec3 push, glm::vec3 pivot, float tip = 0.6f) {
        pieces = voxels;
        occupied.clear();
        for (const VoxelPiece& v : pieces) occupied.insert(key(v.p));
        totalMass = KG_PER_VOXEL * (float)pieces.size();

        // centre of mass (voxel centres)
        c0 = glm::vec3(0.0f);
        for (const VoxelPiece& v : pieces) c0 += glm::vec3(v.p) + 0.5f;
        c0 /= std::max<size_t>(pieces.size(), 1);

        // cluster into cells, growing them until few enough points
        cell = 3;
        for (;;) {
            std::unordered_map<int64_t, std::pair<glm::vec3, int>> cells;
            for (const VoxelPiece& v : pieces) {
                glm::ivec3 c = glm::ivec3(glm::floor(glm::vec3(v.p) / (float)cell));
                auto& e = cells[key(c)];
                e.first += glm::vec3(v.p) + 0.5f;
                e.second++;
            }
            if ((int)cells.size() <= MAX_POINTS || cell >= 12) {
                rest.clear(); mass.clear();
                for (auto& [k, e] : cells) {
                    rest.push_back(e.first / (float)e.second - c0);
                    mass.push_back(KG_PER_VOXEL * e.second);
                }
                break;
            }
            cell++;
        }
        radius = cell * 0.45f;
        boundR = 0.0f;
        for (const VoxelPiece& v : pieces) boundR = std::max(boundR, glm::length(glm::vec3(v.p) + 0.5f - c0));
        boundR += 1.0f;
        const size_t n = rest.size();
        x.resize(n); old.resize(n); contact.assign(n, glm::vec3(0.0f)); weight.assign(n, 1.0f);
        for (size_t i = 0; i < n; i++) x[i] = c0 + rest[i];

        // starting tip: the top moves along 'push' about the cut
        glm::vec3 dir(push.x, 0.0f, push.z);
        float l = glm::length(dir);
        dir = l > 1e-4f ? dir / l : glm::vec3(1, 0, 0);
        old = x;

        // the hinge: turns about a level axis through the cut, so
        // the top goes along 'dir'
        hingeAt = pivot;
        hingeAxis = glm::cross(glm::vec3(0, 1, 0), dir);
        hingeAngle = 0.0f;
        hingeSpin = tip;
        hingeInertia = 0.0f;
        for (size_t i = 0; i < n; i++) {
            glm::vec3 r = c0 + rest[i] - pivot;
            glm::vec3 perp = r - glm::dot(r, hingeAxis) * hingeAxis;
            hingeInertia += mass[i] * (glm::dot(perp, perp) + radius * radius * 0.4f);
        }
        hinged = true;
        c = prevC = c0;
        q = prevQ = glm::quat(1, 0, 0, 0);
        life = stillFor = 0.0f;
        isSettled = false;
    }

    // One physics tick
    void step(float dt, const SolidFn& solid) {
        if (isSettled || rest.empty()) return;
        prevC = c; prevQ = q;
        if (hinged) { hingeStep(dt, solid); life += dt; return; }
        const float h = dt / SUBSTEPS;
        const glm::vec3 g(0.0f, -GRAVITY * h * h, 0.0f);
        float moved = 0.0f;
        std::vector<glm::vec3> start = x;
        for (int sub = 0; sub < SUBSTEPS; sub++) {
            for (size_t i = 0; i < x.size(); i++) {
                glm::vec3 v = (x[i] - old[i]) * DAMPING;
                old[i] = x[i];
                x[i] += v + g;
            }
            std::fill(contact.begin(), contact.end(), glm::vec3(0.0f));
            std::fill(weight.begin(), weight.end(), 1.0f);
            for (int it = 0; it < ITERATIONS; it++) {
                for (size_t i = 0; i < x.size(); i++) {
                    glm::vec3 n;
                    if (!collideBall(x[i], radius, solid, n)) continue;
                    contact[i] = n;
                    // friction on positions: this substep's sideways
                    // movement along the surface is undone (sticking)
                    // or cut (sliding), so contact pushes can't
                    // ratchet the body around
                    glm::vec3 d = x[i] - old[i];
                    glm::vec3 dt_ = d - glm::dot(d, n) * n;
                    bool stick = glm::length(dt_) < STICK_SPEED * h;
                    x[i] -= stick ? dt_ : dt_ * FRICTION;
                    if (stick) weight[i] = STUCK_WEIGHT;
                }
                match();
            }
            // Resting: lying on the ground and slow. Contact
            // corrections shared across the rigid shape can feed
            // a slow creep; bleed it away so a log comes to rest.
            {
                int touching = 0;
                float mean = 0.0f;
                for (size_t i = 0; i < x.size(); i++) {
                    if (contact[i] != glm::vec3(0.0f)) touching++;
                    mean += glm::length(x[i] - old[i]);
                }
                mean /= (float)x.size() * h;
                if (touching >= REST_CONTACTS && mean < REST_SPEED)
                    for (size_t i = 0; i < x.size(); i++) old[i] = x[i] - (x[i] - old[i]) * REST_DAMP;
            }
            for (size_t i = 0; i < x.size(); i++) {      // friction where it touches
                glm::vec3 n = contact[i];
                if (n == glm::vec3(0.0f)) continue;
                glm::vec3 v = x[i] - old[i];
                float vn = glm::dot(v, n);
                glm::vec3 vt = v - vn * n;
                vt = glm::length(vt) < STICK_SPEED * h ? glm::vec3(0.0f) : vt * (1.0f - FRICTION);
                v = vt;                                    // no bounce: the push-out is not kept as speed
                old[i] = x[i] - v;
            }
        }
        for (size_t i = 0; i < x.size(); i++) moved = std::max(moved, glm::length(x[i] - start[i]));
        life += dt;

        stillFor = moved / dt < STILL_SPEED ? stillFor + dt : 0.0f;
        if (stillFor >= STILL_TIME || life >= MAX_LIFE) isSettled = true;
    }

    // ── Queries ───────────────────────────────────────────
    bool  settled()  const { return isSettled; }
    float massKg()   const { return totalMass; }
    float age()      const { return life; }
    const std::vector<VoxelPiece>& voxels() const { return pieces; }
    size_t    pointCount()        const { return x.size(); }
    glm::vec3 point(size_t i)     const { return x[i]; }
    float     pointRadius()       const { return radius; }
    float     pointMass(size_t i) const { return mass[i]; }
    glm::vec3 pointVelocity(size_t i, float dt) const { return (x[i] - old[i]) * (float)SUBSTEPS / dt; }

    // Original world coordinates → where they are now; alpha
    // blends between the last two ticks (for drawing)
    glm::mat4 transform(float alpha = 1.0f) const {
        glm::vec3 cc = glm::mix(prevC, c, alpha);
        glm::quat qq = glm::slerp(prevQ, q, alpha);
        return glm::translate(glm::mat4(1.0f), cc) * glm::mat4_cast(qq) * glm::translate(glm::mat4(1.0f), -c0);
    }
    // Where the body's own "up" (the trunk) points now
    glm::vec3 axisUp() const { return glm::mat3_cast(q) * glm::vec3(0, 1, 0); }

    // Is world point p inside one of the body's voxels?
    bool occupies(glm::vec3 p) const {
        glm::vec3 d = p - c;
        if (glm::dot(d, d) > boundR * boundR) return false;
        glm::vec3 local = glm::transpose(glm::mat3_cast(q)) * d + c0;
        return occupied.count(key(glm::ivec3(glm::floor(local)))) > 0;
    }
    float     boundRadius() const { return boundR; }
    glm::vec3 centre()      const { return c; }
    glm::quat rotation()    const { return q; }
    bool      isHinged()    const { return hinged; }

    // Put it at a pose and let it sleep (loading a save)
    void setPose(glm::vec3 centre, glm::quat rot) {
        c = prevC = centre;
        q = prevQ = glm::normalize(rot);
        glm::mat3 R = glm::mat3_cast(q);
        for (size_t i = 0; i < x.size(); i++) old[i] = x[i] = c + R * rest[i];
        hinged = false;
        isSettled = true;
        life = MAX_LIFE;
    }

    // Wake a sleeping body (something it rested on is gone)
    void wake() {
        if (!isSettled) return;
        isSettled = false;
        stillFor = 0.0f;
        life = HINGE_TIME + 0.01f;
    }

private:
    std::vector<VoxelPiece> pieces;
    std::unordered_set<int64_t> occupied;
    std::vector<glm::vec3> rest, x, old, contact;
    std::vector<float> mass, weight;
    glm::vec3 c0{0}, c{0}, prevC{0};
    glm::quat q{1, 0, 0, 0}, prevQ{1, 0, 0, 0};
    float totalMass = 0.0f, radius = 1.0f, life = 0.0f, stillFor = 0.0f, boundR = 1.0f;
    int   cell = 3;
    bool  isSettled = false;
    bool  hinged = false;
    glm::vec3 hingeAt{0}, hingeAxis{1, 0, 0};
    float hingeAngle = 0.0f, hingeSpin = 0.0f, hingeInertia = 1.0f;

    static int64_t key(glm::ivec3 p) {
        return ((int64_t)(p.x & 0x1FFFFF) << 42) | ((int64_t)(p.y & 0x1FFFFF) << 21) | (int64_t)(p.z & 0x1FFFFF);
    }

    // The hinged phase: the tree turns about the cut as one piece,
    // sped up by gravity's pull on its centre of mass (torque /
    // moment of inertia about the hinge). Released, with the
    // motion it has, once it leans past HINGE_BREAK, something
    // away from the cut touches the world, or HINGE_TIME passes.
    void hingeStep(float dt, const SolidFn& solid) {
        const float h = dt / SUBSTEPS;
        float M = 0.0f;
        for (float m : mass) M += m;
        auto place = [&](float angle, std::vector<glm::vec3>& out) {
            glm::mat3 R = glm::mat3_cast(glm::angleAxis(angle, hingeAxis));
            for (size_t i = 0; i < out.size(); i++) out[i] = hingeAt + R * (c0 + rest[i] - hingeAt);
        };
        bool release = false;
        for (int sub = 0; sub < SUBSTEPS && !release; sub++) {
            glm::mat3 R = glm::mat3_cast(glm::angleAxis(hingeAngle, hingeAxis));
            glm::vec3 com = hingeAt + R * (c0 - hingeAt);
            float torque = glm::dot(hingeAxis, glm::cross(com - hingeAt, glm::vec3(0, -GRAVITY * M, 0)));
            hingeSpin += torque / hingeInertia * h;
            hingeAngle += hingeSpin * h;
            place(hingeAngle, x);
            // touching anything away from the cut?
            for (size_t i = 0; i < x.size() && !release; i++) {
                if (glm::length(c0 + rest[i] - hingeAt) < radius + 4.0f) continue;
                glm::vec3 p = x[i], n;
                if (collideBall(p, radius, solid, n)) release = true;
            }
        }
        q = glm::angleAxis(hingeAngle, hingeAxis);
        c = hingeAt + glm::mat3_cast(q) * (c0 - hingeAt);
        // Verlet history = where it was a substep ago, so it keeps
        // its spin when released
        place(hingeAngle - hingeSpin * h, old);
        if (release || std::cos(hingeAngle) < HINGE_BREAK || life > HINGE_TIME) hinged = false;
    }

    // Shape matching: move the points onto the body's shape at
    // the rotation that best fits where they are (Müller et al.,
    // "A Robust Method to Extract the Rotational Part of
    // Deformations", 2016). Weighted by mass, and points held
    // by friction count much more, so the fit keeps them put.
    void match() {
        float W = 0.0f;
        glm::vec3 xm(0.0f), rm(0.0f);
        for (size_t i = 0; i < x.size(); i++) {
            float w = mass[i] * weight[i];
            xm += x[i] * w; rm += rest[i] * w; W += w;
        }
        xm /= W; rm /= W;
        glm::mat3 A(0.0f);
        for (size_t i = 0; i < x.size(); i++)
            A += glm::outerProduct(x[i] - xm, rest[i] - rm) * (mass[i] * weight[i]);
        for (int k = 0; k < 12; k++) {
            glm::mat3 R = glm::mat3_cast(q);
            glm::vec3 w = (glm::cross(R[0], A[0]) + glm::cross(R[1], A[1]) + glm::cross(R[2], A[2])) /
                          (std::fabs(glm::dot(R[0], A[0]) + glm::dot(R[1], A[1]) + glm::dot(R[2], A[2])) + 1e-9f);
            float a = glm::length(w);
            if (a < 1e-7f) break;
            q = glm::normalize(glm::angleAxis(a, w / a) * q);
        }
        glm::mat3 R = glm::mat3_cast(q);
        c = xm - R * rm;                 // the body's centre of mass (rest offsets are about it)
        for (size_t i = 0; i < x.size(); i++) x[i] = c + R * rest[i];
    }
};
