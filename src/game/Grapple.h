#pragma once
// ══════════════════════════════════════════════════════════
// The grapple: a hook on a rope (ROADMAP Mushroom Islands phase 2)
// ══════════════════════════════════════════════════════════
// Thrown at a grapple point (a block with the "grapple" flag, like the
// ones under the glowing mushrooms' caps) or a body, it pulls you in:
//   tap    pulled all the way. At a point you come up over it with a
//          hop; at a body it's a lunge, and an attack or a spell
//          pressed on the way comes out as you get there
//   hold   swing on the rope instead (a point, or a flyer's grapple
//          point): let go of the button to fly off with your speed,
//          jump to let go with a hop, crouch to climb the rope
// A hit that staggers you, or the rope stretched far past its reach,
// drops it.
//
// Who does what:
//   the item           goes in an ability slot (X / Y / B), a quick
//                      option or a loadout's off hand, like a
//                      spellbook: that button throws it (CombatKit.h);
//                      out of combat, also held from the hotbar
//   grapple::aim       the aim assist: the grapple point or body
//                      nearest the middle of the view, in reach (the
//                      game, every frame, for the marker and the
//                      throw); headless, so tests use it
//   HumanIntent        carries the throw (grappleAt / grappleOn)
//   Simulation         checks it (carrying one, in reach, a real point
//                      or a body it may grapple), keeps a body's
//                      anchor on the body (grapple::follow)
//   VoxelPhysics       moves the body (grappleTick)
//   the game           draws the rope (main.cpp, BoxRenderer::beam)
// (Claude, 2026-11-25; equipped like a spellbook, aim assist 2026-11-26)
#include "GrappleState.h"
#include "../entity/Human.h"
#include "../chunk/ChunkManager.h"
#include "../voxel/VoxelRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace grapple {

constexpr float THROW_SPEED  = 500.0f;   // vox/s the hook flies out
constexpr float ZIP_ACCEL    = 260.0f;   // vox/s² up to the item's speed
constexpr float TAP          = 0.18f;    // s held past the throw before it's a swing, not a tap
constexpr float ARRIVE       = 3.0f;     // vox from a point (to the body's middle): there
constexpr float LUNGE_STOP   = 6.0f;     // vox from a body's skin: there (a lunge stops short, in reach to hit it)
constexpr float ARRIVE_HOP   = 16.0f;    // vox: how high the hop at a point carries you
constexpr float ARRIVE_CARRY = 22.0f;    // vox/s on along the way you came, after the hop
constexpr float SWING_CLEAR  = 5.0f;     // vox of air under the feet before a held rope swings (nearer the ground it pulls you up)
constexpr float SWING_MIN    = 6.0f;     // vox: the shortest rope (climbing it)
constexpr float CLIMB_SPEED  = 18.0f;    // vox/s up the rope (crouch held)
constexpr float SWING_PUMP   = 28.0f;    // vox/s² steering adds while swinging
constexpr float SWING_DRAG   = 0.08f;    // a little of the speed lost each second
constexpr float MAX_SPEED    = 130.0f;   // vox/s, never faster on the rope
constexpr float SNAP         = 1.35f;    // × the reach: further than that from the anchor, the rope lets go
constexpr float STUCK_TIME   = 0.3f;     // s held up on the way in: give up
constexpr float HAND_UP      = 0.62f;    // the rope's end on the body: this far up it

inline bool isPoint(uint16_t id) { return id && id < gRegistry.size() && gRegistry.get(id).isGrapple(); }

// The rope's end on a body (its middle, a little high)
inline glm::vec3 hand(const Human& h) { return h.position + glm::vec3(0.0f, h.height * HAND_UP, 0.0f); }

// A body it could grapple, as the game sees it
struct Candidate {
    uint64_t  id = 0;
    glm::vec3 point{ 0.0f };      // where the hook would hold (a flyer's grapple point, or its middle)
    float     radius = 2.0f;      // how big it is (a near miss on a big body still counts)
    bool      swingable = false;  // a flyer's grapple point: hold to swing from it
};

// What the grapple would catch
struct Target {
    bool      ok = false;
    glm::vec3 at{ 0.0f };         // a point's middle, or the body's grapple point
    uint64_t  on = 0;             // a body (0: a point)
    bool      swingable = true;
    float     dist = 0.0f;
};

// Is the way from a to b clear (solid blocks; the last voxel, the
// target's own, doesn't count)?
inline bool clearLine(const ChunkManager& w, glm::vec3 a, glm::vec3 b, glm::ivec3 skip = glm::ivec3(INT32_MIN)) {
    glm::vec3 d = b - a;
    float len = glm::length(d);
    if (len < 1e-3f) return true;
    int n = (int)std::ceil(len / 0.5f);
    for (int i = 1; i < n; i++) {
        glm::vec3 p = a + d * ((float)i / n);
        glm::ivec3 v((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z));
        if (v == skip) continue;
        uint16_t id = w.typeAtVoxel(v.x, v.y, v.z);
        if (id && gRegistry.get(id).isSolid()) return false;
    }
    return true;
}

// ── Finding grapple points ────────────────────────────────
// Every grapple point within r of c, read straight from the
// sections' runs (a run that isn't a grapple point is skipped
// whole), so it's cheap enough to do a few times a second
inline void scanPoints(const ChunkManager& w, glm::vec3 c, float r, std::vector<glm::ivec3>& out) {
    out.clear();
    glm::ivec3 lo = glm::ivec3(glm::floor(c - glm::vec3(r))), hi = glm::ivec3(glm::floor(c + glm::vec3(r)));
    lo.y = std::max(lo.y, 0); hi.y = std::min(hi.y, WORLD_HEIGHT - 1);
    if (lo.y > hi.y) return;
    const float r2 = r * r;
    auto keep = [&](glm::ivec3 v) {
        glm::vec3 d = glm::vec3(v) + glm::vec3(0.5f) - c;
        if (glm::dot(d, d) <= r2) out.push_back(v);
    };
    for (int cx = ChunkManager::chunkCoord(lo.x); cx <= ChunkManager::chunkCoord(hi.x); cx++)
    for (int cz = ChunkManager::chunkCoord(lo.z); cz <= ChunkManager::chunkCoord(hi.z); cz++) {
        const Chunk* ch = w.getChunk(cx, cz);
        if (!ch) continue;
        const int ox = cx * VOXEL_DIM, oz = cz * VOXEL_DIM;
        for (int sec = lo.y / VOXEL_DIM; sec <= hi.y / VOXEL_DIM; sec++) {
            const ChunkSection& S = ch->section(sec);
            const int oy = sec * VOXEL_DIM;
            if (const RleData* R = S.runs()) {
                uint32_t start = 0;
                for (size_t i = 0; i < R->ends.size(); start = R->ends[i], i++) {
                    if (!isPoint(R->types[i])) continue;
                    for (uint32_t k = start; k < R->ends[i]; k++) {      // index = (y * DIM + x) * DIM + z
                        int z = (int)(k % VOXEL_DIM), x = (int)((k / VOXEL_DIM) % VOXEL_DIM), y = (int)(k / (VOXEL_DIM * VOXEL_DIM));
                        keep(glm::ivec3(ox + x, oy + y, oz + z));
                    }
                }
            } else if (const uint16_t* raw = S.rawConst()) {           // being edited: the box, voxel by voxel
                int x0 = std::max(lo.x - ox, 0), x1 = std::min(hi.x - ox, VOXEL_DIM - 1);
                int z0 = std::max(lo.z - oz, 0), z1 = std::min(hi.z - oz, VOXEL_DIM - 1);
                int y0 = std::max(lo.y - oy, 0), y1 = std::min(hi.y - oy, VOXEL_DIM - 1);
                for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    const uint16_t* row = raw + ((size_t)y * VOXEL_DIM + x) * VOXEL_DIM;
                    for (int z = z0; z <= z1; z++)
                        if (row[z] && isPoint(row[z])) keep(glm::ivec3(ox + x, oy + y, oz + z));
                }
            }
        }
    }
}

// The grapple points around the player, found again a few times a
// second (or when it's moved a way): what aim() picks from
struct PointCache {
    std::vector<glm::ivec3> points;
    glm::vec3 at{ 1e9f };
    float radius = 0.0f, age = 1e9f;
    void update(const ChunkManager& w, glm::vec3 c, float r, float dt) {
        age += dt;
        if (age < 0.25f && glm::length(c - at) < 8.0f && r <= radius) return;
        r += 10.0f;                                    // a margin: it's good till it moves that far
        scanPoints(w, c, r, points);
        at = c; radius = r; age = 0.0f;
    }
    void clear() { points.clear(); age = 1e9f; }
};

// ── Aiming (aim assist) ───────────────────────────────────
// The grapple catches what's near where you look, not only what's
// under the crosshair: any grapple point or body within ASSIST_DEG
// of the view, in reach of the hand, with a clear line. The one
// nearest the middle of the view wins (a little for being near),
// and the one already marked keeps it unless another is clearly
// better, so the marker doesn't flicker between two.
constexpr float ASSIST_DEG   = 28.0f;    // how far from the view's middle a target may be
constexpr float NEAR_WEIGHT  = 6.0f;     // degrees added at the far end of the reach
constexpr float STICKY_DEG   = 5.0f;     // the marked one's head start
constexpr int   MAX_LINES    = 12;       // clear-line checks per aim (best first)

struct Aim {
    glm::vec3 eye{ 0.0f }, dir{ 0.0f, 0.0f, 1.0f };   // the camera (dir: unit)
    glm::vec3 hand{ 0.0f };                         // the rope's end on the body
    float     reach = 120.0f;                       // from the hand
    float     cone = ASSIST_DEG;
    const Target* prev = nullptr;                   // marked last frame
};

inline Target aim(const ChunkManager& w, const Aim& a, const std::vector<glm::ivec3>& points,
                  const std::vector<Candidate>& bodies = {}) {
    struct Opt { float score; glm::vec3 at; uint64_t on; bool swing; glm::ivec3 skip; };
    std::vector<Opt> opts;
    auto consider = [&](glm::vec3 p, float size, uint64_t on, bool swing, glm::ivec3 skip) {
        float fromHand = glm::length(p - a.hand);
        if (fromHand > a.reach || fromHand < 2.0f) return;
        glm::vec3 off = p - a.eye;
        float d = glm::length(off);
        if (d < 1e-3f) return;
        float along = glm::dot(off, a.dir);
        if (along < 1.0f) return;                                  // behind the camera
        float ang = glm::degrees(std::acos(std::clamp(along / d, -1.0f, 1.0f)));
        ang = std::max(0.0f, ang - glm::degrees(std::atan(size / d)));   // a big body: its edge counts
        if (ang > a.cone) return;
        float score = ang + NEAR_WEIGHT * fromHand / std::max(1.0f, a.reach);
        if (a.prev && a.prev->ok && a.prev->on == on && (on || glm::length(a.prev->at - p) < 0.01f)) score -= STICKY_DEG;
        opts.push_back({ score, p, on, swing, skip });
    };
    for (const glm::ivec3& v : points) consider(glm::vec3(v) + glm::vec3(0.5f), 0.5f, 0, true, v);
    for (const Candidate& c : bodies) consider(c.point, c.radius, c.id, c.swingable, glm::ivec3(INT32_MIN));
    std::sort(opts.begin(), opts.end(), [](const Opt& x, const Opt& y) { return x.score < y.score; });
    Target best;
    int checks = 0;
    for (const Opt& o : opts) {
        if (checks++ >= MAX_LINES) break;
        if (!clearLine(w, a.eye, o.at, o.skip)) continue;
        best.ok = true; best.at = o.at; best.on = o.on; best.swingable = o.swing; best.dist = glm::length(o.at - a.hand);
        break;
    }
    return best;
}

// The same from a standing eye (tests, a quick look): the points
// found around it, reach measured from the eye
inline Target aim(const ChunkManager& w, glm::vec3 eye, glm::vec3 dir, float range,
                  const std::vector<Candidate>& bodies = {}) {
    std::vector<glm::ivec3> pts;
    scanPoints(w, eye, range, pts);
    Aim a; a.eye = eye; a.dir = glm::normalize(dir); a.hand = eye; a.reach = range;
    return aim(w, a, pts, bodies);
}

// Keep a body's anchor on the body it holds (each tick, before it
// moves): find gives the body's grapple point, false if it's gone
// (dead, out of the world): then the rope drops
inline void follow(Human& h, const std::function<bool(uint64_t, glm::vec3&)>& find) {
    GrappleState& g = h.grapple;
    if (!g.active() || !g.on) return;
    glm::vec3 p;
    if (find(g.on, p)) g.anchor = p;
    else g = GrappleState();
}

} // namespace grapple
