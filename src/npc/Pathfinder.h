#pragma once
#include "../physics/IPhysics.h"
#include "../entity/Human.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// Pathfinder — walking routes over the voxels
// ══════════════════════════════════════════════════════════
// A* over standing spots on a 2-voxel grid. From a spot, each of
// the 8 neighbours is reachable if there is ground to stand on
// there that a body fits on (IPhysics::capsuleIsClear) within:
//   up    STEP_UP (walks up) … CLIMB (needs a jump)
//   down  DROP (walks / falls down)
// Diagonals need both sides open (no cutting corners). Costs:
// distance, plus extra for climbing and more for a jump. The
// search is bounded (MAX_NODES), so an unreachable goal costs a
// fixed amount and returns the closest place it got to.
//
// No caching of the world: chunks change (digging, building,
// felled logs), and a search of a few hundred nodes is cheap.
// Callers spread searches over ticks (Simulation::steerToward).
namespace nav {

constexpr int   CELL      = 2;          // voxels per grid step
constexpr float STEP_UP   = Human::STEP_UP;
constexpr int   CLIMB     = 12;         // highest step a jump gets up (JUMP_HEIGHT 15 less a margin)
constexpr int   DROP      = 14;         // highest drop it walks off
constexpr float BUMP      = 4.5f;       // ledges this high inside the body's radius don't block a spot (a stair's 16 in step; under STEP_UP: it steps up them)
constexpr int   MAX_NODES = 2500;
constexpr float MAX_RANGE = 160.0f;     // goals farther than this are steered at in a straight line

struct Result {
    std::vector<glm::vec3> points;      // standing spots, from the first step to the goal (or closest)
    bool reached = false;
    int  expanded = 0;
};

class Pathfinder {
public:
    explicit Pathfinder(const IPhysics& p) : phys(p) {}
    // What a voxel of climbing costs, in voxels of walking: the
    // body's own slowdown uphill (Human::climb x CLIMB_COST), so a
    // longer switchback wins when it's quicker
    float climbCost = Human::CLIMB_COST;

    // Highest place to stand in column (x, z) between y-DROP and
    // y+CLIMB; false if none. Standing = solid below the feet and
    // room for the body.
    bool standAt(int x, int z, int y, int& out) const {
        for (int sy = y + CLIMB; sy >= y - DROP; sy--) {
            if (!phys.solidAt(x, sy - 1, z) || phys.solidAt(x, sy, z)) continue;
            // room for the body, from a little above the feet: a
            // ledge of a voxel or two inside its radius (a terraced
            // slope) is stepped up, not a wall
            glm::vec3 p((float)x + 0.5f, (float)sy + BUMP, (float)z + 0.5f);
            if (phys.capsuleIsClear(p, Human::RADIUS + 0.4f, Human::STAND_HEIGHT - BUMP)) { out = sy; return true; }
            return false;          // the top here has no room: blocked
        }
        return false;
    }

    Result find(glm::vec3 from, glm::vec3 to) const {
        Result r;
        auto cellOf = [](float v) { return (int)std::floor(v / CELL); };
        auto centre = [](int c) { return c * CELL + CELL / 2; };
        Node start{ cellOf(from.x), cellOf(from.z), (int)std::floor(from.y + 0.01f) };
        int gx = cellOf(to.x), gz = cellOf(to.z), gy = (int)std::floor(to.y + 0.01f);

        std::unordered_map<int64_t, Info> seen;
        std::unordered_map<int64_t, int> memo;          // (cell, from height) → standing height or NONE
        const int NONE = INT32_MIN;
        struct Q { float f; int64_t k; bool operator<(const Q& o) const { return f > o.f; } };
        std::priority_queue<Q> open;
        auto h = [&](const Node& n) {
            float dx = (float)(n.x - gx) * CELL, dz = (float)(n.z - gz) * CELL, dy = (float)(n.y - gy);
            return std::sqrt(dx * dx + dz * dz + dy * dy * 0.25f);
        };
        int64_t sk = key(start);
        seen[sk] = { start, 0.0f, -1, false };
        open.push({ h(start), sk });
        int64_t best = sk; float bestH = h(start);

        static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
        static const int DZ[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        while (!open.empty() && r.expanded < MAX_NODES) {
            Q q = open.top(); open.pop();
            Info& cur = seen[q.k];
            if (cur.closed) continue;
            cur.closed = true;
            r.expanded++;
            Node n = cur.n;
            float hn = h(n);
            if (hn < bestH) { bestH = hn; best = q.k; }
            if (n.x == gx && n.z == gz && std::abs(n.y - gy) <= CLIMB) { best = q.k; r.reached = true; break; }
            int ys[8]; bool ok[8];
            for (int i = 0; i < 8; i++) {
                ok[i] = false;
                if (i >= 4 && !(ok[DX[i] > 0 ? 0 : 1] && ok[DZ[i] > 0 ? 2 : 3])) continue;   // no corner cutting
                // a cell is looked at from up to 8 sides: remember the answer
                int64_t mk = key({ n.x + DX[i], n.z + DZ[i], n.y });
                auto m = memo.find(mk);
                int y = 0;
                bool can;
                if (m != memo.end()) { can = m->second != NONE; y = m->second; }
                else { can = standAt(centre(n.x + DX[i]), centre(n.z + DZ[i]), n.y, y); memo[mk] = can ? y : NONE; }
                if (!can) continue;
                ok[i] = true; ys[i] = y;
            }
            // Up a ledge: the cell at its foot is often no place to
            // stand (the body won't fit against the side), so a jump
            // can reach over it to the top, two cells on
            Node over[4]; bool hasOver[4] = { false, false, false, false };
            for (int i = 0; i < 4; i++) {
                if (ok[i]) continue;
                int y = 0;
                if (standAt(centre(n.x + 2 * DX[i]), centre(n.z + 2 * DZ[i]), n.y, y) && y - n.y > STEP_UP) {
                    over[i] = { n.x + 2 * DX[i], n.z + 2 * DZ[i], y };
                    hasOver[i] = true;
                }
            }
            for (int i = 0; i < 4; i++) {
                if (!hasOver[i]) continue;
                const Node& m = over[i];
                float g = cur.g + 2.0f * CELL + (m.y - n.y) * climbCost + 6.0f;
                int64_t mk = key(m);
                auto it = seen.find(mk);
                if (it != seen.end() && (it->second.closed || it->second.g <= g)) continue;
                seen[mk] = { m, g, q.k, false };
                open.push({ g + h(m), mk });
            }
            for (int i = 0; i < 8; i++) {
                if (!ok[i]) continue;
                Node m{ n.x + DX[i], n.z + DZ[i], ys[i] };
                int dy = m.y - n.y;
                float cost = (i < 4 ? 1.0f : 1.4142f) * CELL;
                if (dy > 0) cost += dy * climbCost + (dy > STEP_UP ? 6.0f : 0.0f);   // climbing (slower, Human::CLIMB_COST); a jump costs more
                else if (dy < -4) cost += 2.0f;                                   // a drop
                int64_t mk = key(m);
                float g = cur.g + cost;
                auto it = seen.find(mk);
                if (it != seen.end() && (it->second.closed || it->second.g <= g)) continue;
                seen[mk] = { m, g, q.k, false };
                open.push({ g + h(m), mk });
            }
        }
        // walk back from the goal (or the closest node reached)
        std::vector<glm::vec3> back;
        for (int64_t k = best; k != -1; k = seen[k].parent) {
            const Node& n = seen[k].n;
            back.push_back(glm::vec3((float)centre(n.x), (float)n.y, (float)centre(n.z)));
        }
        std::reverse(back.begin(), back.end());
        if (!back.empty()) back.erase(back.begin());       // the start cell
        if (r.reached && !back.empty()) back.back() = glm::vec3(to.x, back.back().y, to.z);
        r.points = std::move(back);
        return r;
    }

private:
    struct Node { int x, z, y; };
    struct Info { Node n; float g; int64_t parent; bool closed; };
    static int64_t key(const Node& n) {
        return ((int64_t)(n.x & 0xFFFFF) << 40) | ((int64_t)(n.z & 0xFFFFF) << 20) | (int64_t)(n.y & 0xFFFFF);
    }
    const IPhysics& phys;
};

} // namespace nav
