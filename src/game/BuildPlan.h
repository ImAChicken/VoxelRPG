#pragma once
// ══════════════════════════════════════════════════════════
// BuildPlan — a building kept as a plan (the hammer's Build tab)
// ══════════════════════════════════════════════════════════
// Sims-style building (Eddie, 2026-11-25; ROADMAP "Build mode"):
// a building is kept as what it's made of, not just its voxels:
//   foundations   rectangles of grid cells (a 3 ft grid), all at the
//                 plan's floor height (its 'floor' voxels over the
//                 ground it was set on)
//   walls         segments between grid points, on the floor: any
//                 angle the tools allow (straight, 45°, the 2-to-1
//                 "22.5°"), 3 voxels thick by default, their own height,
//                 a paint for each side
// The voxels are made from it (voxels()): change a wall's height or
// the floor's and only that is made again; the rest stands. What a
// hand does to the building's voxels (a block broken out of a wall,
// a chest set on the floor) is kept as an override, laid over the
// plan every time it's made (world/PlanManager.h), so nothing done
// by hand is lost when the plan changes.
//
// Inside and outside: the walls' footprint is flood-filled from
// outside; the ground it can't reach is inside a room. A wall voxel
// beside outside ground is the outer face (the palette's wall block,
// "fill"), beside a room the inner face (the palette's inside
// "paint"), else the core ("fill"). A wall between two rooms has two
// inside faces. Each side of each wall can be painted its own block.
// The foundation's top under a room is the palette's floor.
//
// Coordinates: plan-local voxels, x and z from the plan's origin (a
// world voxel on the 3 ft grid, at the ground the plan was set on),
// grid point k at voxel k * GRID: a wall on a grid line is centred on
// that voxel column, so a 3-thick wall takes the column and one each
// side. A wall's ends are voxel columns (on the grid unless snapped to
// a part off it). Pillars stand through walls, in their place.
// Headless. (Claude, 2026-11-25)
#include "../voxel/Voxel.h"
#include "../voxel/VoxelRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace plan {

constexpr int GRID = 9;          // voxels per grid cell (3 ft)
constexpr int THICK = 3;         // a wall's thickness (voxels)
constexpr int WALL_H = 30;       // a wall's height (10 ft)
constexpr int FLOOR = 3;         // a foundation's height over the ground (1 ft)
constexpr int SINK = 2;          // how far a foundation goes into the ground under it
constexpr int MAX_WALL_H = 90;
constexpr int MAX_FLOOR = 24;
constexpr int DECK = 2;          // an upper floor's thickness (the deck on the walls below)
constexpr int MAX_LEVELS = 6;

// The blocks a plan is made of: a palette's roles (game/Palettes.h)
struct Looks {
    uint16_t fill = 0;           // a wall's outside face and core
    uint16_t paint = 0;          // a wall's inside face
    uint16_t base = 0;           // a foundation
    uint16_t floor = 0;          // a foundation's top inside a room
    uint16_t post = 0;           // pillars
    uint16_t frame = 0;          // round a door or window (the palette's beam)
    uint16_t glass = 0;          // a window's pane (0: none, left open)
    uint16_t roof = 0;           // roofs
};

struct Foundation {
    glm::ivec2 a{ 0 }, b{ 0 };   // grid cells, both corners (inclusive)
    glm::ivec2 lo() const { return glm::min(a, b); }
    glm::ivec2 hi() const { return glm::max(a, b); }
    // voxel bounds (plan-local, x and z): from the first cell's grid
    // line to the last's, plus half a wall either side
    glm::ivec2 vlo() const { return lo() * GRID - glm::ivec2(THICK / 2); }
    glm::ivec2 vhi() const { return (hi() + glm::ivec2(1)) * GRID + glm::ivec2(THICK / 2); }
    bool covers(glm::ivec2 v) const { glm::ivec2 l = vlo(), h = vhi(); return v.x >= l.x && v.x <= h.x && v.y >= l.y && v.y <= h.y; }
};

// A hole in a wall (a frame): 'along' voxels from the wall's first end
// to its middle, 'width' across, 'height' up from 'sill' over the
// floor. What's in it: nothing, a door (a door piece, placed by the
// PlanManager: 'door' is its type, 'doorId' the one standing) or a
// window (the palette's glass in the wall's middle)
enum class Fill : uint8_t { Open = 0, Door = 1, Window = 2 };
struct Opening {
    int along = 0, width = 10, height = 24, sill = 0;
    Fill fill = Fill::Open;
    std::string door;            // a door's type name ("": the plan's default)
    uint32_t doorId = 0;         // the door piece standing in it (StructureManager)
    int lo() const { return along - width / 2; }                    // its first column along the wall
    bool holds(float t, int y) const { return t >= lo() - 0.5f && t < lo() + width - 0.5f && y >= sill && y < sill + height; }
    // the frame round it: a voxel's width at the sides and over it (and under a window)
    bool frames(float t, int y) const {
        bool side = t >= lo() - 1.5f && t < lo() + width + 0.5f;
        bool rise = y >= sill - (sill > 0 ? 1 : 0) && y <= sill + height;
        return side && rise && !holds(t, y);
    }
    bool overlaps(const Opening& o) const { return lo() - 1 < o.lo() + o.width + 1 && o.lo() - 1 < lo() + width + 1; }
};

struct Wall {
    std::vector<Opening> openings;
    int level = 0;               // which storey it stands on (0: the ground floor)
    glm::ivec2 a{ 0 }, b{ 0 };   // its ends: voxel columns (plan-local; on the grid, k * GRID, unless snapped to a part)
    int height = 0;              // its own height; 0: its level's (Plan::levelHeight)
    int thick = THICK;
    uint16_t paint[2] = { 0, 0 };   // a block for each side (0: the palette's): [0] left of a→b, [1] right
    // its ends' column centres
    glm::vec2 va() const { return glm::vec2(a) + glm::vec2(0.5f); }
    glm::vec2 vb() const { return glm::vec2(b) + glm::vec2(0.5f); }
    float length() const { return glm::length(glm::vec2(b - a)); }
    // how far a voxel column's centre is from the wall's line (its capsule)
    float distance(glm::vec2 p) const {
        glm::vec2 A = va(), B = vb(), ab = B - A;
        float l2 = glm::dot(ab, ab);
        float u = l2 > 1e-6f ? std::clamp(glm::dot(p - A, ab) / l2, 0.0f, 1.0f) : 0.0f;
        return glm::length(p - (A + ab * u));
    }
    bool covers(glm::vec2 p) const { return distance(p) <= thick * 0.5f + 0.01f; }
    // which side a point is on: 0 left of a→b, 1 right
    int side(glm::vec2 p) const {
        glm::vec2 A = va(), ab = vb() - A, ap = p - A;
        return ab.x * ap.y - ab.y * ap.x > 0.0f ? 0 : 1;
    }
    bool sameAs(const Wall& o) const { return (a == o.a && b == o.b) || (a == o.b && b == o.a); }
    // how far along it a column centre is, from the first end (voxels)
    float along(glm::vec2 p) const {
        glm::vec2 ab = vb() - va();
        float l = glm::length(ab);
        return l > 1e-4f ? glm::dot(p - va(), ab / l) : 0.0f;
    }
    bool straight() const { return a.x == b.x || a.y == b.y; }       // along x or z (a door piece fits)
    // the opening at a point along it (with its frame), or -1
    int openingAt(float t, int y = -1) const {
        for (size_t i = 0; i < openings.size(); i++) {
            const Opening& o = openings[i];
            if (t >= o.lo() - 1.5f && t < o.lo() + o.width + 0.5f && (y < 0 || (y >= o.sill - 1 && y <= o.sill + o.height))) return (int)i;
        }
        return -1;
    }
};

// A pillar: a post standing on the plan, through any wall there (it
// takes the wall's place where it stands): square or round, 'size'
// voxels across, its own height (0: the tallest wall it's in, else a
// wall's), its own block (0: the palette's posts)
struct Pillar {
    glm::ivec2 at{ 0 };          // its middle column (plan-local)
    int level = 0;
    int size = 5;
    bool round = false;
    int height = 0;
    uint16_t block = 0;
    bool covers(glm::ivec2 v) const {
        glm::vec2 d = glm::vec2(v - at);
        float r = size * 0.5f;
        return round ? glm::length(d) <= r - 0.25f + 0.01f : std::fabs(d.x) <= (float)(size / 2) && std::fabs(d.y) <= (float)(size / 2);
    }
};

// A roof over a rectangle of wall lines (its eaves on them), like
// Sims 4's: gable (a ridge, two slopes, the end walls carried up to
// it), hip (four slopes), shed (one slope, low side to high) or flat;
// 'peak' how far the ridge rises over the eaves (adjustable after),
// 'axis' which way the ridge runs (0 along x, 1 along z; a shed rises
// across it), 'overhang' past the walls. Its eaves sit on the walls
// under it ('base' over the floor; 0: the tallest of them)
enum class RoofStyle : uint8_t { Gable, Hip, Shed, Flat, COUNT };
inline const char* roofStyleName(RoofStyle s) {
    static const char* N[] = { "gable", "hip", "shed", "flat" };
    return N[(int)s < (int)RoofStyle::COUNT ? (int)s : 0];
}
struct Roof {
    glm::ivec2 a{ 0 }, b{ 0 };   // the corners' wall columns (plan-local)
    RoofStyle style = RoofStyle::Gable;
    int axis = 0;                // gable: 0 / 1; shed: 0..3 (which way it rises)
    int peak = 15;
    int overhang = 3;
    int thick = 2;
    int base = 0;
    int level = 0;               // the storey whose walls carry it
    uint16_t block = 0;          // 0: the palette's roof
    glm::ivec2 lo() const { return glm::min(a, b); }
    glm::ivec2 hi() const { return glm::max(a, b); }
    bool over(glm::ivec2 v) const {
        glm::ivec2 l = lo() - glm::ivec2(overhang), h = hi() + glm::ivec2(overhang);
        return v.x >= l.x && v.y >= l.y && v.x <= h.x && v.y <= h.y;
    }
    bool inside(glm::ivec2 v, int margin = 0) const {
        glm::ivec2 l = lo() - glm::ivec2(margin), h = hi() + glm::ivec2(margin);
        return v.x >= l.x && v.y >= l.y && v.x <= h.x && v.y <= h.y;
    }
    // Its slopes, each a plane over the eaves: a * x + b * z + c (plan-local,
    // continuous: a column's centre is v + 0.5). The roof is the lowest of
    // them everywhere (a gable's two meet at the ridge, a hip's four at
    // the ridge and the hips)
    std::vector<glm::vec3> facets() const {
        glm::vec2 L = glm::vec2(lo()) + 0.5f, H = glm::vec2(hi()) + 0.5f, m = (L + H) * 0.5f, half = glm::max((H - L) * 0.5f, glm::vec2(1.0f));
        std::vector<glm::vec3> f;
        auto alongZ = [&](float k) { f.push_back({ 0, k, k * (half.y - m.y) }); f.push_back({ 0, -k, k * (half.y + m.y) }); };
        auto alongX = [&](float k) { f.push_back({ k, 0, k * (half.x - m.x) }); f.push_back({ -k, 0, k * (half.x + m.x) }); };
        switch (style) {
        case RoofStyle::Gable:
            if ((axis & 1) == 0) alongZ(peak / half.y); else alongX(peak / half.x);
            break;
        case RoofStyle::Hip: {
            float k = peak / std::min(half.x, half.y);
            alongZ(k); alongX(k);
            break;
        }
        case RoofStyle::Shed: {
            // rising toward +z, +x, -z, -x (axis 0..3)
            float D = (axis & 1) == 0 ? H.y - L.y : H.x - L.x, s0 = (axis & 1) == 0 ? L.y : L.x;
            float k = peak / std::max(D, 1.0f);
            glm::vec3 g = (axis & 1) == 0 ? glm::vec3(0, k, -k * s0) : glm::vec3(k, 0, -k * s0);
            if (axis >= 2) g = glm::vec3(-g.x, -g.y, (float)peak - g.z);
            f.push_back(g);
            break;
        }
        default: f.push_back({ 0, 0, 0 }); break;
        }
        return f;
    }
    static float at(const std::vector<glm::vec3>& f, glm::vec2 c) {
        float r = 1e9f;
        for (const glm::vec3& q : f) r = std::min(r, q.x * c.x + q.y * c.y + q.z);
        return r;
    }
    // the top's height over the eaves at a column (below 0 out in the overhang)
    float rise(glm::ivec2 v) const { return at(facets(), glm::vec2(v) + 0.5f); }
    // its top voxel at a column, over the eaves
    int top(glm::ivec2 v) const { return (int)std::floor(rise(v) + 1e-3f); }
};

// An upper floor: grid cells like a foundation's, on top of the walls
// of the level under it (DECK thick, its top the level's floor)
struct Deck : Foundation {
    int level = 1;
};

// A stair from one level's floor up to the next's, 'width' across,
// steps of STEP along and at most STEP up (each under the height a body
// climbs without jumping). It cuts its way through the deck above.
// Shapes:
//   straight  one flight
//   L         a flight, a square landing, a flight turned a quarter
//   U         a flight, a landing twice as wide, a flight back beside it
//   spiral    round a post, 'width' out from it, a full turn
// 'turn' is the way an L, U or spiral turns: +1 toward side(), -1 away
enum class StairShape : uint8_t { Straight, L, U, Spiral, COUNT };
inline const char* stairShapeName(StairShape s) {
    static const char* N[] = { "straight", "L", "U", "spiral" };
    return N[(int)s < (int)StairShape::COUNT ? (int)s : 0];
}
struct Stair {
    static constexpr int STEP = 4;
    glm::ivec2 at{ 0 };          // the bottom step's first column, in the middle of its width (a spiral's post) (plan-local)
    int dir = 0;                 // the way it climbs first: 0 +x, 1 +z, 2 -x, 3 -z
    int width = 9;
    int level = 0;               // from this level up to the next
    uint16_t block = 0;          // 0: the palette's floor
    StairShape shape = StairShape::Straight;
    int turn = 1;
    glm::ivec2 fwd() const { static const glm::ivec2 D[4] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1} }; return D[dir & 3]; }
    glm::ivec2 side() const { glm::ivec2 f = fwd(); return { -f.y, f.x }; }
    static int steps(int rise) { return std::max(1, (rise + STEP - 1) / STEP); }
    static int length(int rise) { return steps(rise) * STEP; }
    // the top of the column i along a straight one, over its level's floor (the last is the floor above)
    static int topAt(int i, int rise) { int n = steps(rise); return (int)std::lround((double)(i / STEP + 1) * rise / n); }

    // ── Any shape: its steps, numbered from the bottom ───
    int count(int rise) const {
        switch (shape) {
        case StairShape::L: case StairShape::U: return steps(rise) + 1;   // the landing is one more
        case StairShape::Spiral: return std::max(8, steps(rise) * 2);
        default: return steps(rise);
        }
    }
    // a step's top over its level's floor (the last: the floor above)
    int top(int step, int rise) const { return (int)std::lround((double)(step + 1) * rise / count(rise)); }
    int jlo() const { return -width / 2; }
    int jhi() const { return width - width / 2 - 1; }
    // plan-local column of (i along the first flight, j across it, mirrored by turn)
    glm::ivec2 toPlan(int i, int j) const {
        if (turn < 0 && shape != StairShape::Straight) j = jlo() + jhi() - j;
        return at + fwd() * i + side() * j;
    }
    // which step a column is (-1: none)
    int stepAt(glm::ivec2 v, int rise) const {
        glm::ivec2 d = v - at;
        const glm::ivec2 f = fwd(), sd = side();
        const int i = d.x * f.x + d.y * f.y;
        int j = d.x * sd.x + d.y * sd.y;
        const int N = count(rise), lo = jlo(), hi = jhi();
        if (shape == StairShape::Spiral) {
            const float r = (float)width + 0.5f, dist = std::sqrt((float)(d.x * d.x + d.y * d.y));
            if (dist > r) return -1;
            if (dist < 1.0f) return N - 1;                       // the post
            float a = std::atan2((float)(j * (turn < 0 ? -1 : 1)), (float)i);
            if (a < 0) a += 6.2831853f;
            return std::min(N - 1, (int)(a / 6.2831853f * (float)N));
        }
        if (turn < 0 && shape != StairShape::Straight) j = lo + hi - j;
        if (shape == StairShape::Straight)
            return (i >= 0 && i < N * STEP && j >= lo && j <= hi) ? i / STEP : -1;
        const int n1 = (N - 1) / 2, n2 = N - 1 - n1, A = n1 * STEP, B = A + width;
        if (j >= lo && j <= hi) {
            if (i >= 0 && i < A) return i / STEP;
            if (i >= A && i < B) return n1;
        }
        if (shape == StairShape::L) {
            if (j > hi && i >= A && i < B) { int k = j - hi - 1; if (k < n2 * STEP) return n1 + 1 + k / STEP; }
            return -1;
        }
        // U: the landing runs across both flights, the second comes back beside the first
        if (j > hi && j <= hi + width) {
            if (i >= A && i < B) return n1;
            int k = A - 1 - i;
            if (k >= 0 && k < n2 * STEP) return n1 + 1 + k / STEP;
        }
        return -1;
    }
    bool covers(glm::ivec2 v, int rise) const { return stepAt(v, rise) >= 0; }
    // the way off its top step (a spiral: none, -1)
    int exitDir() const {
        switch (shape) {
        case StairShape::Straight: return dir & 3;
        case StairShape::L: { glm::ivec2 s = side() * (turn < 0 ? -1 : 1); return s.x > 0 ? 0 : s.y > 0 ? 1 : s.x < 0 ? 2 : 3; }
        case StairShape::U: return (dir + 2) & 3;
        default: return -1;
        }
    }
    // its plan-local bounds (x z, inclusive)
    void box(int rise, glm::ivec2& lo, glm::ivec2& hi) const {
        if (shape == StairShape::Spiral) { lo = at - glm::ivec2(width); hi = at + glm::ivec2(width); return; }
        const int N = count(rise), n1 = (N - 1) / 2, n2 = N - 1 - n1, A = n1 * STEP, B = A + width;
        int i0 = 0, i1 = N * STEP - 1, j0 = jlo(), j1 = jhi();
        if (shape == StairShape::L) { i1 = B - 1; j1 = jhi() + n2 * STEP; }
        if (shape == StairShape::U) { i0 = std::min(0, A - n2 * STEP); i1 = B - 1; j1 = jhi() + width; }
        lo = glm::ivec2(INT32_MAX); hi = glm::ivec2(INT32_MIN);
        for (int i : { i0, i1 }) for (int j : { j0, j1 }) { glm::ivec2 c = toPlan(i, j); lo = glm::min(lo, c); hi = glm::max(hi, c); }
    }
};

// A fence or railing: one voxel wide, from column a to b (any angle),
// 'height' tall, on its level's floor (on a stair's steps where it runs
// over one, so a stair gets its handrail). Styles:
//   picket   pickets with a gap between each, two rails, posts every 8
//   rail     posts every 6 with two rails between (open, a farm fence)
//   panel    solid boards, posts every 8 (a privacy fence)
//   baluster a top rail on balusters with a gap between each (stairs, balconies)
enum class FenceStyle : uint8_t { Picket, Rail, Panel, Baluster, COUNT };
inline const char* fenceStyleName(FenceStyle s) {
    static const char* N[] = { "picket", "rail", "panel", "baluster" };
    return N[(int)s < (int)FenceStyle::COUNT ? (int)s : 0];
}
struct Fence {
    glm::ivec2 a{ 0 }, b{ 0 };   // its ends' columns (plan-local)
    int level = 0;
    FenceStyle style = FenceStyle::Picket;
    int height = 9;              // 3 ft
    uint16_t block = 0;          // 0: the palette's beam (rails, pickets) and post
    // its columns, in order from a (a line one voxel wide)
    std::vector<glm::ivec2> columns() const {
        glm::ivec2 d = b - a;
        int n = std::max(std::abs(d.x), std::abs(d.y));
        std::vector<glm::ivec2> out;
        for (int i = 0; i <= n; i++) {
            float t = n ? (float)i / n : 0.0f;
            out.push_back(a + glm::ivec2((int)std::lround(d.x * t), (int)std::lround(d.y * t)));
        }
        return out;
    }
    // which voxels of column i (of n) are solid, y from its foot: post, rail / picket, or gap
    // returns 0 air, 1 post, 2 board
    int at(int i, int n, int y) const {
        const int h = height;
        auto post = [&](int every) { return i == 0 || i == n - 1 || i % every == 0; };
        switch (style) {
        case FenceStyle::Picket:
            if (post(8)) return y <= h ? 1 : 0;
            if (y == 2 || y == h - 3) return 2;
            return (i % 2 == 0 && y < h) ? 2 : 0;
        case FenceStyle::Rail:
            if (post(6)) return y <= h ? 1 : 0;
            return (y == h / 3 || y == h - 2) ? 2 : 0;
        case FenceStyle::Panel:
            if (post(8)) return y <= h ? 1 : 0;
            return y < h ? 2 : 0;
        default:   // baluster
            if (i == 0 || i == n - 1) return y <= h ? 1 : 0;
            if (y == h - 1) return 2;
            return (i % 2 == 0 && y < h - 1) ? 2 : 0;
        }
    }
};

// The directions a wall may run, by the tools: along the grid, 45°,
// and the 2-to-1 ("22.5°") between them
inline glm::ivec2 snapDirection(glm::vec2 d) {
    static const glm::ivec2 DIRS[] = { {1, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 1}, {-1, 2}, {-1, 1}, {-2, 1},
                                       {-1, 0}, {-2, -1}, {-1, -1}, {-1, -2}, {0, -1}, {1, -2}, {1, -1}, {2, -1} };
    if (glm::length(d) < 1e-4f) return glm::ivec2(1, 0);
    glm::vec2 n = glm::normalize(d);
    glm::ivec2 best = DIRS[0]; float bd = -2.0f;
    for (glm::ivec2 c : DIRS) { float k = glm::dot(n, glm::normalize(glm::vec2(c))); if (k > bd) { bd = k; best = c; } }
    return best;
}
// A wall from grid point a toward where the cursor is (grid units):
// along the nearest allowed direction, as many steps as come closest
// step: the steps come in multiples of it (GRID keeps a wall from a grid
// point ending on one; 1 for a free end)
inline glm::ivec2 snapEnd(glm::ivec2 a, glm::vec2 to, int step = 1) {
    glm::ivec2 dir = snapDirection(to - glm::vec2(a));
    glm::vec2 d(dir);
    float steps = glm::dot(to - glm::vec2(a), d) / glm::dot(d, d);
    int n = (int)std::round(steps / (float)std::max(step, 1)) * std::max(step, 1);
    return a + dir * std::max(0, n);
}
// Does a → b run along one of the allowed directions?
inline bool allowedRun(glm::ivec2 a, glm::ivec2 b) {
    glm::ivec2 d = b - a;
    if (d == glm::ivec2(0)) return false;
    glm::ivec2 s = snapDirection(glm::vec2(d));
    return d.x * s.y - d.y * s.x == 0 && (d.x * s.x + d.y * s.y) > 0;
}

struct Plan {
    uint32_t id = 0;
    glm::ivec3 origin{ 0 };      // world voxel of grid point (0, 0), at the ground it was set on
    int floor = FLOOR;           // the foundation's height (voxels over origin.y); walls stand on it
    std::string palette;         // its blocks (game/Palettes.h); "": the default
    std::vector<Foundation> foundations;
    std::vector<Wall> walls;
    std::vector<Pillar> pillars;
    std::vector<Roof> roofs;
    std::vector<Deck> decks;
    std::vector<Stair> stairs;
    std::vector<Fence> fences;
    std::vector<int> levelH;     // each storey's wall height (0 or missing: WALL_H); [0] the ground floor
    bool rails = true;           // railings of their own round stairwells and along open deck edges
    static constexpr int RAIL_H = 10;
    bool hasFoundation() const { return !foundations.empty(); }
    int floorY() const { return origin.y + (hasFoundation() ? floor : 0); }
    bool empty() const { return foundations.empty() && walls.empty() && pillars.empty() && roofs.empty() && decks.empty() && stairs.empty() && fences.empty(); }

    // ── Storeys ───────────────────────────────────────────
    int levelHeight(int k) const { return k >= 0 && k < (int)levelH.size() && levelH[(size_t)k] > 0 ? levelH[(size_t)k] : WALL_H; }
    // a storey's floor over the ground floor's: the storeys under it and their decks
    int levelBase(int k) const { int b = 0; for (int j = 0; j < k; j++) b += levelHeight(j) + DECK; return b; }
    int levelY(int k) const { return floorY() + levelBase(k); }
    int heightOf(const Wall& w) const { return w.height > 0 ? w.height : levelHeight(w.level); }
    // how far a stair from level k climbs
    int stairRise(int k) const { return levelHeight(k) + DECK; }
    int levelCount() const {
        int n = 1;
        for (const Wall& w : walls) n = std::max(n, w.level + 1);
        for (const Deck& d : decks) n = std::max(n, d.level + 1);
        for (const Pillar& q : pillars) n = std::max(n, q.level + 1);
        for (const Roof& r : roofs) n = std::max(n, r.level + 1);
        for (const Stair& t : stairs) n = std::max(n, t.level + 2);
        for (const Fence& f : fences) n = std::max(n, f.level + 1);
        return n;
    }
    // where a fence's column stands: its level's floor, or the step under it
    int footAt(int k, glm::ivec2 v) const {
        int y = 0;
        for (const Stair& t : stairs)
            if (t.level == k) { int s = t.stepAt(v, stairRise(k)); if (s >= 0) y = std::max(y, t.top(s, stairRise(k))); }
        return y;
    }
    // is a column of level k's deck cut out for a stair coming up through it?
    bool stairwell(int k, glm::ivec2 v) const {
        for (const Stair& t : stairs) if (t.level + 1 == k && t.covers(v, stairRise(t.level))) return true;
        return false;
    }
    // level k's own railing: deck columns round its stairwells (but the
    // way off a stair's top) and along its decks' open edges (but where a
    // wall stands). Each: a post (1, at ends and corners) or a rail (2)
    std::vector<std::pair<glm::ivec2, int>> railColumns(int k) const {
        std::vector<std::pair<glm::ivec2, int>> out;
        if (!rails || k < 1) return out;
        auto deckAt = [&](glm::ivec2 v) { for (const Deck& d : decks) if (d.level == k && d.covers(v)) return true; return false; };
        auto wallAt = [&](glm::ivec2 v) {
            for (const Wall& w : walls) if (w.level == k && w.covers(glm::vec2(v) + 0.5f)) return true;
            return false;
        };
        static const glm::ivec2 N8[8] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };
        std::unordered_set<int64_t> on;
        auto kk = [](glm::ivec2 v) { return ((int64_t)v.x << 32) ^ (int64_t)(uint32_t)v.y; };
        std::vector<glm::ivec2> cols;
        for (const Deck& d : decks) {
            if (d.level != k) continue;
            glm::ivec2 lo = d.vlo(), hi = d.vhi();
            for (int z = lo.y; z <= hi.y; z++)
                for (int x = lo.x; x <= hi.x; x++) {
                    const glm::ivec2 v(x, z);
                    if (on.count(kk(v)) || stairwell(k, v) || wallAt(v)) continue;
                    bool rail = false, exit = false;
                    for (int n = 0; n < 8; n++) {
                        const glm::ivec2 u = v + N8[n];
                        if (stairwell(k, u)) {
                            rail = true;
                            for (const Stair& t : stairs) {
                                if (t.level + 1 != k) continue;
                                const int rise = stairRise(t.level), s = t.stepAt(u, rise), last = t.count(rise) - 1;
                                if (s < 0) continue;
                                const int e = t.exitDir();
                                if (e < 0) { if (s >= last - 1) exit = true; continue; }
                                static const glm::ivec2 D[4] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1} };
                                if (s == last && N8[n] == -D[e]) exit = true;   // right behind it: the way off
                            }
                        } else if (n < 4 && !deckAt(u) && !wallAt(u)) rail = true;
                    }
                    if (rail && !exit) { on.insert(kk(v)); cols.push_back(v); }
                }
        }
        for (glm::ivec2 v : cols) {
            bool e = on.count(kk(v + glm::ivec2(1, 0))), w = on.count(kk(v - glm::ivec2(1, 0)));
            bool nn = on.count(kk(v + glm::ivec2(0, 1))), s = on.count(kk(v - glm::ivec2(0, 1)));
            int links = (int)e + (int)w + (int)nn + (int)s;
            bool straight = links == 2 && ((e && w) || (nn && s));
            out.push_back({ v, straight || links > 2 ? 2 : 1 });
        }
        return out;
    }
    // is a wall along a roof's edge (an eave, or an end it carries a gable up from)?
    static bool alongEdge(const Wall& w, const Roof& r) {
        glm::ivec2 l = r.lo(), h = r.hi();
        return (w.a.x == w.b.x && (w.a.x == l.x || w.a.x == h.x)) || (w.a.y == w.b.y && (w.a.y == l.y || w.a.y == h.y));
    }
    // a wall's top at one of its columns: its height, or up to a roof's
    // underside where it's a gable end
    int wallTop(const Wall& wl, glm::ivec2 v) const {
        int t = heightOf(wl);
        for (const Roof& rf : roofs)
            if (rf.level == wl.level && alongEdge(wl, rf) && rf.inside(v, wl.thick / 2)) t = std::max(t, roofBase(rf) + rf.top(v) - rf.thick + 1);
        return t;
    }
    // a roof's eaves over the floor: its own, else the tallest wall along its edges
    int roofBase(const Roof& r) const {
        if (r.base > 0) return r.base;
        int b = 0;
        for (const Wall& w : walls) if (w.level == r.level && alongEdge(w, r) && r.over(w.a) && r.over(w.b)) b = std::max(b, heightOf(w));
        return b > 0 ? b : levelHeight(r.level);
    }

    // plan-local bounds of everything (voxels, x z), with a margin
    void bounds(glm::ivec2& lo, glm::ivec2& hi, int margin = 2) const {
        lo = glm::ivec2(INT32_MAX); hi = glm::ivec2(INT32_MIN);
        for (const Foundation& f : foundations) { lo = glm::min(lo, f.vlo()); hi = glm::max(hi, f.vhi()); }
        for (const Wall& w : walls) {
            lo = glm::min(lo, glm::min(w.a, w.b) - glm::ivec2(w.thick)); hi = glm::max(hi, glm::max(w.a, w.b) + glm::ivec2(w.thick));
        }
        for (const Pillar& q : pillars) { lo = glm::min(lo, q.at - glm::ivec2(q.size)); hi = glm::max(hi, q.at + glm::ivec2(q.size)); }
        for (const Roof& r : roofs) { lo = glm::min(lo, r.lo() - glm::ivec2(r.overhang)); hi = glm::max(hi, r.hi() + glm::ivec2(r.overhang)); }
        for (const Deck& d : decks) { lo = glm::min(lo, d.vlo()); hi = glm::max(hi, d.vhi()); }
        for (const Fence& f : fences) { lo = glm::min(lo, glm::min(f.a, f.b)); hi = glm::max(hi, glm::max(f.a, f.b)); }
        for (const Stair& t : stairs) {
            glm::ivec2 l, h;
            t.box(stairRise(t.level), l, h);
            lo = glm::min(lo, l); hi = glm::max(hi, h);
        }
        if (lo.x > hi.x) { lo = hi = glm::ivec2(0); }
        lo -= glm::ivec2(margin); hi += glm::ivec2(margin);
    }
    int topY() const {
        int t = floorY();
        for (const Wall& w : walls) t = std::max(t, levelY(w.level) + heightOf(w));
        for (const Pillar& q : pillars) t = std::max(t, levelY(q.level) + pillarHeight(q));
        for (const Roof& r : roofs) t = std::max(t, levelY(r.level) + roofBase(r) + r.peak + 1);
        for (const Deck& d : decks) t = std::max(t, levelY(d.level));
        for (const Stair& s2 : stairs) t = std::max(t, levelY(s2.level + 1));
        for (const Fence& f : fences) t = std::max(t, levelY(f.level + 1) + f.height + 1);
        return t;
    }
    // a pillar's height: its own, else the tallest wall through its middle, else a wall's
    int pillarHeight(const Pillar& q) const {
        if (q.height > 0) return q.height;
        int h = 0;
        for (const Wall& w : walls)
            if (w.level == q.level && w.distance(glm::vec2(q.at) + glm::vec2(0.5f)) <= w.thick * 0.5f + q.size * 0.5f) h = std::max(h, heightOf(w));
        return h > 0 ? h : levelHeight(q.level);
    }
    bool onFoundation(glm::ivec2 v) const { for (const Foundation& f : foundations) if (f.covers(v)) return true; return false; }

    // ── As text (plans.dat) ───────────────────────────────
    void write(std::ostream& o) const {
        o << "plan " << id << " " << origin.x << " " << origin.y << " " << origin.z << " " << floor << " "
          << (palette.empty() ? "-" : palette) << "\n";
        for (const Foundation& f : foundations) o << " f " << f.a.x << " " << f.a.y << " " << f.b.x << " " << f.b.y << "\n";
        for (const Wall& w : walls)
            o << " w " << w.a.x << " " << w.a.y << " " << w.b.x << " " << w.b.y << " " << w.height << " " << w.thick << " "
              << w.paint[0] << " " << w.paint[1] << " " << w.level << "\n";
        for (size_t i = 0; i < walls.size(); i++)
            for (const Opening& h : walls[i].openings)
                o << " h " << i << " " << h.along << " " << h.width << " " << h.height << " " << h.sill << " " << (int)h.fill << " "
                  << (h.door.empty() ? "-" : h.door) << " " << h.doorId << "\n";
        for (const Pillar& q : pillars)
            o << " p " << q.at.x << " " << q.at.y << " " << q.size << " " << (q.round ? 1 : 0) << " " << q.height << " " << q.block << " " << q.level << "\n";
        for (const Roof& r : roofs)
            o << " r " << r.a.x << " " << r.a.y << " " << r.b.x << " " << r.b.y << " " << (int)r.style << " " << r.axis << " " << r.peak << " "
              << r.overhang << " " << r.thick << " " << r.base << " " << r.block << " " << r.level << "\n";
        for (const Deck& d : decks) o << " d " << d.a.x << " " << d.a.y << " " << d.b.x << " " << d.b.y << " " << d.level << "\n";
        for (const Stair& t : stairs) o << " s " << t.at.x << " " << t.at.y << " " << t.dir << " " << t.width << " " << t.level << " " << t.block << " "
                                         << (int)t.shape << " " << t.turn << "\n";
        for (const Fence& f : fences)
            o << " n " << f.a.x << " " << f.a.y << " " << f.b.x << " " << f.b.y << " " << f.level << " " << (int)f.style << " " << f.height << " " << f.block << "\n";
        if (!levelH.empty()) { o << " L"; for (int h : levelH) o << " " << h; o << "\n"; }
        if (!rails) o << " R 0\n";
        o << "end\n";
    }
    // the lines after "plan …" up to "end"
    static bool read(std::istream& in, const std::string& head, Plan& p) {
        std::istringstream h(head);
        std::string k, pal;
        if (!(h >> k >> p.id >> p.origin.x >> p.origin.y >> p.origin.z >> p.floor >> pal) || k != "plan") return false;
        p.palette = pal == "-" ? std::string() : pal;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream l(line);
            std::string t; l >> t;
            if (t == "end") return true;
            if (t == "f") { Foundation f; l >> f.a.x >> f.a.y >> f.b.x >> f.b.y; p.foundations.push_back(f); }
            else if (t == "w") {
                Wall w; l >> w.a.x >> w.a.y >> w.b.x >> w.b.y >> w.height >> w.thick >> w.paint[0] >> w.paint[1];
                if (!(l >> w.level)) w.level = 0;
                p.walls.push_back(w);
            }
            else if (t == "d") { Deck d; l >> d.a.x >> d.a.y >> d.b.x >> d.b.y >> d.level; p.decks.push_back(d); }
            else if (t == "s") {
                Stair st; int sh = 0, tn = 1;
                l >> st.at.x >> st.at.y >> st.dir >> st.width >> st.level >> st.block;
                if (l >> sh >> tn) { st.shape = (StairShape)std::clamp(sh, 0, (int)StairShape::COUNT - 1); st.turn = tn < 0 ? -1 : 1; }
                p.stairs.push_back(st);
            }
            else if (t == "n") {
                Fence f; int st = 0;
                l >> f.a.x >> f.a.y >> f.b.x >> f.b.y >> f.level >> st >> f.height >> f.block;
                f.style = (FenceStyle)std::clamp(st, 0, (int)FenceStyle::COUNT - 1);
                p.fences.push_back(f);
            }
            else if (t == "R") { int on = 1; l >> on; p.rails = on != 0; }
            else if (t == "L") { int h; p.levelH.clear(); while (l >> h) p.levelH.push_back(h); }
            else if (t == "h") {
                Opening h; size_t wi = 0; int f = 0;
                l >> wi >> h.along >> h.width >> h.height >> h.sill >> f >> h.door >> h.doorId;
                h.fill = (Fill)std::clamp(f, 0, 2);
                if (h.door == "-") h.door.clear();
                if (wi < p.walls.size()) p.walls[wi].openings.push_back(h);
            }
            else if (t == "r") {
                Roof r; int st = 0;
                l >> r.a.x >> r.a.y >> r.b.x >> r.b.y >> st >> r.axis >> r.peak >> r.overhang >> r.thick >> r.base >> r.block;
                if (!(l >> r.level)) r.level = 0;
                r.style = (RoofStyle)std::clamp(st, 0, (int)RoofStyle::COUNT - 1);
                p.roofs.push_back(r);
            }
            else if (t == "p") {
                Pillar q; int r = 0; l >> q.at.x >> q.at.y >> q.size >> r >> q.height >> q.block;
                if (!(l >> q.level)) q.level = 0;
                q.round = r != 0; p.pillars.push_back(q);
            }
        }
        return true;
    }
};

// ── Voxels from a plan ────────────────────────────────────
inline int64_t key(int x, int y, int z) {
    return ((int64_t)(x & 0x3FFFFF) << 42) | ((int64_t)(y & 0xFFFFF) << 22) | (int64_t)(z & 0x3FFFFF);
}
inline glm::ivec3 unkey(int64_t k) {
    auto s = [](int64_t v, int bits) { int64_t m = 1ll << (bits - 1); return (int)((v ^ m) - m); };
    return { s((k >> 42) & 0x3FFFFF, 22), s((k >> 22) & 0xFFFFF, 20), s(k & 0x3FFFFF, 22) };
}
using Voxels = std::unordered_map<int64_t, uint16_t>;   // world voxel → block

// What's inside a room: the plan's footprint, flood-filled from
// outside around its walls. inside[(z - lo.y) * w + (x - lo.x)]
struct Rooms {
    glm::ivec2 lo{ 0 }, hi{ 0 };
    int w = 0, h = 0;
    std::vector<uint8_t> cell;    // 0 outside, 1 wall, 2 inside a room
    uint8_t at(glm::ivec2 v) const {
        if (v.x < lo.x || v.y < lo.y || v.x > hi.x || v.y > hi.y) return 0;
        return cell[(size_t)((v.y - lo.y) * w + (v.x - lo.x))];
    }
    int roomCells() const { int n = 0; for (uint8_t c : cell) n += c == 2; return n; }
};
inline Rooms rooms(const Plan& p, int level = 0) {
    Rooms r;
    p.bounds(r.lo, r.hi, 2);
    r.w = r.hi.x - r.lo.x + 1; r.h = r.hi.y - r.lo.y + 1;
    r.cell.assign((size_t)r.w * r.h, 2);
    for (const Wall& wl : p.walls) {
        if (wl.level != level) continue;
        glm::ivec2 a = glm::min(wl.a, wl.b) - glm::ivec2(wl.thick), b = glm::max(wl.a, wl.b) + glm::ivec2(wl.thick);
        for (int z = std::max(a.y, r.lo.y); z <= std::min(b.y, r.hi.y); z++)
            for (int x = std::max(a.x, r.lo.x); x <= std::min(b.x, r.hi.x); x++)
                if (wl.covers(glm::vec2(x + 0.5f, z + 0.5f))) r.cell[(size_t)((z - r.lo.y) * r.w + (x - r.lo.x))] = 1;
    }
    // outside: everything the edge reaches without crossing a wall
    std::vector<int> stack;
    auto push = [&](int x, int z) {
        size_t i = (size_t)(z * r.w + x);
        if (r.cell[i] != 2) return;
        r.cell[i] = 0; stack.push_back((int)i);
    };
    for (int x = 0; x < r.w; x++) { push(x, 0); push(x, r.h - 1); }
    for (int z = 0; z < r.h; z++) { push(0, z); push(r.w - 1, z); }
    while (!stack.empty()) {
        int i = stack.back(); stack.pop_back();
        int x = i % r.w, z = i / r.w;
        if (x > 0) push(x - 1, z);
        if (x + 1 < r.w) push(x + 1, z);
        if (z > 0) push(x, z - 1);
        if (z + 1 < r.h) push(x, z + 1);
    }
    return r;
}

// Every voxel the plan makes (world positions), its blocks from looks
inline Voxels voxels(const Plan& p, const Looks& L) {
    Voxels out;
    const glm::ivec3 o = p.origin;
    const glm::ivec2 N[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    // foundations: from a little under the ground to the floor; their
    // top under a room is the floor
    if (p.hasFoundation()) {
        Rooms r = rooms(p, 0);
        const int y0 = o.y - SINK, yTop = p.floorY() - 1;
        std::unordered_set<int64_t> seen;
        for (const Foundation& f : p.foundations) {
            glm::ivec2 lo = f.vlo(), hi = f.vhi();
            for (int z = lo.y; z <= hi.y; z++)
                for (int x = lo.x; x <= hi.x; x++) {
                    if (!seen.insert(key(x, 0, z)).second) continue;
                    bool room = r.at({ x, z }) == 2;
                    for (int y = y0; y <= yTop; y++)
                        out[key(o.x + x, y, o.z + z)] = (y == yTop && room && L.floor) ? L.floor : L.base;
                }
        }
    }
    // storey by storey: its deck (above the ground floor), walls, openings, pillars
    const int levels = p.levelCount();
    for (int lv = 0; lv < levels; lv++) {
        const int wy = p.levelY(lv);
        bool any = false;
        for (const Wall& w : p.walls) any = any || w.level == lv;
        Rooms r = any ? rooms(p, lv) : Rooms();
        for (const Deck& d : p.decks) {
            if (d.level != lv) continue;
            glm::ivec2 lo = d.vlo(), hi = d.vhi();
            for (int z = lo.y; z <= hi.y; z++)
                for (int x = lo.x; x <= hi.x; x++) {
                    if (p.stairwell(lv, { x, z })) continue;
                    for (int y = wy - DECK; y < wy; y++) out[key(o.x + x, y, o.z + z)] = L.floor ? L.floor : L.base;
                }
        }
        // walls: each column it covers, the face by what's beside it
        for (const Wall& wl : p.walls) {
            if (wl.level != lv) continue;
            glm::ivec2 a = glm::min(wl.a, wl.b) - glm::ivec2(wl.thick), b = glm::max(wl.a, wl.b) + glm::ivec2(wl.thick);
            for (int z = a.y; z <= b.y; z++)
                for (int x = a.x; x <= b.x; x++) {
                    glm::vec2 c(x + 0.5f, z + 0.5f);
                    if (!wl.covers(c)) continue;
                    // beside outside ground (an outer face), a room (an inner one), or neither (the core)
                    bool outside = false, inside = false;
                    for (glm::ivec2 n : N) {
                        uint8_t k = r.at(glm::ivec2(x, z) + n);
                        outside = outside || k == 0;
                        inside = inside || k == 2;
                    }
                    uint16_t own = wl.paint[wl.side(c)];
                    uint16_t id = outside ? (own ? own : L.fill) : inside ? (own ? own : L.paint) : L.fill;
                    // a wall along a roof's edge goes up to its underside (a gable end)
                    const int wallTop = p.wallTop(wl, { x, z });
                    for (int y = wy; y < wy + wallTop; y++) {
                        int64_t k = key(o.x + x, y, o.z + z);
                        // where walls cross, an outer face wins over a core
                        auto it = out.find(k);
                        if (it == out.end() || it->second == L.fill || !outside) out[k] = id;
                    }
                }
        }
        // openings: their holes cut through their own wall (not a wall
        // crossing it), a frame round each, glass in a window's middle
        for (size_t wi = 0; wi < p.walls.size(); wi++) {
            const Wall& wl = p.walls[wi];
            if (wl.level != lv || wl.openings.empty()) continue;
            glm::ivec2 a = glm::min(wl.a, wl.b) - glm::ivec2(wl.thick), b = glm::max(wl.a, wl.b) + glm::ivec2(wl.thick);
            for (int z = a.y; z <= b.y; z++)
                for (int x = a.x; x <= b.x; x++) {
                    glm::vec2 c(x + 0.5f, z + 0.5f);
                    if (!wl.covers(c)) continue;
                    bool other = false;
                    for (size_t j = 0; j < p.walls.size() && !other; j++) other = j != wi && p.walls[j].level == lv && p.walls[j].covers(c);
                    if (other) continue;
                    float t = wl.along(c);
                    if (wl.openingAt(t) < 0) continue;
                    bool middle = wl.distance(c) <= 0.51f;
                    for (const Opening& h : wl.openings)
                        for (int y = h.sill - 1; y <= h.sill + h.height; y++) {
                            if (y < 0 || y >= p.wallTop(wl, { x, z })) continue;
                            int64_t k = key(o.x + x, wy + y, o.z + z);
                            if (h.holds(t, y)) {
                                if (h.fill == Fill::Window && middle && L.glass) out[k] = L.glass;
                                else out.erase(k);
                            } else if (h.frames(t, y) && L.frame) out[k] = L.frame;
                        }
                }
        }
        // pillars: through whatever's there, from the floor up
        for (const Pillar& q : p.pillars) {
            if (q.level != lv) continue;
            int h = p.pillarHeight(q), rr = q.size / 2 + 1;
            uint16_t id = q.block ? q.block : (L.post ? L.post : L.fill);
            for (int z = q.at.y - rr; z <= q.at.y + rr; z++)
                for (int x = q.at.x - rr; x <= q.at.x + rr; x++) {
                    if (!q.covers({ x, z })) continue;
                    for (int y = wy; y < wy + h; y++) out[key(o.x + x, y, o.z + z)] = id;
                }
        }
    }
    // stairs: solid steps from their level's floor to the next's (the deck
    // above is cut away over them)
    for (const Stair& t : p.stairs) {
        const int rise = p.stairRise(t.level), wy = p.levelY(t.level);
        const uint16_t id = t.block ? t.block : (L.floor ? L.floor : L.base);
        glm::ivec2 lo, hi;
        t.box(rise, lo, hi);
        for (int z = lo.y; z <= hi.y; z++)
            for (int x = lo.x; x <= hi.x; x++) {
                int st = t.stepAt({ x, z }, rise);
                if (st < 0) continue;
                const int top = t.top(st, rise);
                for (int y = wy; y < wy + top; y++) out[key(o.x + x, y, o.z + z)] = id;
            }
    }
    // fences and railings
    for (const Fence& f : p.fences) {
        const std::vector<glm::ivec2> cols = f.columns();
        const int n = (int)cols.size(), wy = p.levelY(f.level);
        const uint16_t board = f.block ? f.block : (L.frame ? L.frame : L.fill), post = f.block ? f.block : (L.post ? L.post : board);
        for (int i = 0; i < n; i++) {
            const glm::ivec2 v = cols[(size_t)i];
            const int foot = wy + p.footAt(f.level, v);
            for (int y = 0; y <= f.height; y++) {
                int k = f.at(i, n, y);
                if (k) out[key(o.x + v.x, foot + y, o.z + v.y)] = k == 1 ? post : board;
            }
        }
    }
    // the plan's own railings (posts at ends and corners, balusters with a gap between)
    for (int lv = 1; lv < p.levelCount(); lv++) {
        const int wy = p.levelY(lv), h = Plan::RAIL_H;
        const uint16_t board = L.frame ? L.frame : L.fill, post = L.post ? L.post : board;
        for (const auto& [v, kind] : p.railColumns(lv))
            for (int y = 0; y < h; y++) {
                uint16_t id = 0;
                if (kind == 1) id = post;
                else if (y == h - 1 || y == 0) id = board;
                else if (((v.x + v.y) & 1) == 0) id = board;
                if (id) out[key(o.x + v.x, wy + y, o.z + v.y)] = id;
            }
    }
    // roofs: a slab over each column, thick enough to close the steps
    // between it and its neighbours
    for (const Roof& rf : p.roofs) {
        const uint16_t id = rf.block ? rf.block : (L.roof ? L.roof : L.fill);
        const int by = p.levelY(rf.level) + p.roofBase(rf);
        glm::ivec2 lo = rf.lo() - glm::ivec2(rf.overhang), hi = rf.hi() + glm::ivec2(rf.overhang);
        const std::vector<glm::vec3> F = rf.facets();
        auto topOf = [&](glm::ivec2 v) { return (int)std::floor(Roof::at(F, glm::vec2(v) + 0.5f) + 1e-3f); };
        for (int z = lo.y; z <= hi.y; z++)
            for (int x = lo.x; x <= hi.x; x++) {
                int t = topOf({ x, z });
                int low = t - rf.thick + 1;
                for (glm::ivec2 n : N) {
                    glm::ivec2 q(x + n.x, z + n.y);
                    if (rf.over(q)) low = std::min(low, topOf(q) - rf.thick + 2);
                }
                for (int y = low; y <= t; y++) out[key(o.x + x, by + y, o.z + z)] = id;
            }
    }
    return out;
}

} // namespace plan
