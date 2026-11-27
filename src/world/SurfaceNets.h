#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ══════════════════════════════════════════════════════════
// Surface nets — a smooth skin over a grid of samples
// ══════════════════════════════════════════════════════════
// (Claude, 2026-11-27; Eddie: "the lowest resolution chunks in the
// background of the floating islands look really bad. Maybe we need
// to do surface nets now and use those to sample the low poly
// background chunks")
//
// The grid holds a density at each sample (solid where it's over
// the iso level) and a colour. One vertex goes in every cube of 8
// samples the surface passes through, at the average of where it
// crosses the cube's 12 edges; every edge between a solid sample and
// an open one makes a quad of the 4 cubes round it. Normals come from
// the density's slope, so the shading is smooth too.
//
// Headless (tests use it): render/SkyFarView.h turns the sky world's
// coarse cells into a grid and draws what this makes. Who does what:
//   Grid           the samples (sample (x, y, z) sits at point (x, y, z))
//   mesh()         the vertices and quads; only the edges whose first
//                  sample is in [lo, hi] make quads, so neighbouring
//                  grids that overlap by a sample share their seam
//                  without drawing it twice
//   Quad.owner     the sample that made it (the far view's chunk mask)
namespace surfacenets {

struct Sample {
    float   d = 0.0f;                 // density: solid over the iso level
    uint8_t r = 0, g = 0, b = 0, glow = 0;
    int16_t topY = -1;                // which solid sample's colour a vertex takes: the highest
};

struct Grid {
    int nx = 0, ny = 0, nz = 0;
    std::vector<Sample> s;
    void resize(int x, int y, int z) { nx = x; ny = y; nz = z; s.assign((size_t)x * y * z, Sample()); }
    Sample&       at(int x, int y, int z)       { return s[((size_t)y * nz + z) * nx + x]; }
    const Sample& at(int x, int y, int z) const { return s[((size_t)y * nz + z) * nx + x]; }
};

struct Vert { glm::vec3 p{ 0.0f }, n{ 0.0f, 1.0f, 0.0f }; uint8_t r = 0, g = 0, b = 0, glow = 0; };
struct Quad { uint32_t v[4]{}; glm::ivec3 owner{ 0 }; int axis = 0, dir = 1; };   // dir: which way it faces along axis
struct Mesh { std::vector<Vert> verts; std::vector<Quad> quads; };

// tops: only the quads facing straight up (the far view's cheap mode)
inline void mesh(const Grid& g, float iso, glm::ivec3 lo, glm::ivec3 hi, Mesh& out, bool tops = false) {
    out.verts.clear(); out.quads.clear();
    const int cx = g.nx - 1, cy = g.ny - 1, cz = g.nz - 1;              // cubes a side
    if (cx < 1 || cy < 1 || cz < 1) return;
    std::vector<int32_t> vid((size_t)cx * cy * cz, -1);
    auto cube = [&](int x, int y, int z) -> int32_t& { return vid[((size_t)y * cz + z) * cx + x]; };
    static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };

    // ── one vertex a cube the surface passes through ──
    for (int y = 0; y < cy; y++)
    for (int z = 0; z < cz; z++)
    for (int x = 0; x < cx; x++) {
        const Sample* c[8];
        float d[8];
        int mask = 0;
        for (int k = 0; k < 8; k++) {                            // corner k: x + 2y... as bits (x, y, z) = (k&1, k>>1&1, k>>2&1)
            c[k] = &g.at(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1));
            d[k] = c[k]->d;
            if (d[k] > iso) mask |= 1 << k;
        }
        if (mask == 0 || mask == 255) continue;
        glm::vec3 sum(0.0f); int n = 0;
        for (const auto& e : E) {
            bool a = (mask >> e[0]) & 1, b = (mask >> e[1]) & 1;
            if (a == b) continue;
            float t = (iso - d[e[0]]) / (d[e[1]] - d[e[0]]);
            glm::vec3 pa((float)(e[0] & 1), (float)((e[0] >> 1) & 1), (float)((e[0] >> 2) & 1));
            glm::vec3 pb((float)(e[1] & 1), (float)((e[1] >> 1) & 1), (float)((e[1] >> 2) & 1));
            sum += pa + (pb - pa) * std::clamp(t, 0.0f, 1.0f);
            n++;
        }
        Vert v;
        v.p = glm::vec3((float)x, (float)y, (float)z) + sum / (float)n;
        // the slope: density falls going out, so the normal is against it
        glm::vec3 grad(0.0f);
        for (int k = 0; k < 8; k++) {
            grad.x += (k & 1) ? d[k] : -d[k];
            grad.y += ((k >> 1) & 1) ? d[k] : -d[k];
            grad.z += ((k >> 2) & 1) ? d[k] : -d[k];
        }
        v.n = glm::length(grad) > 1e-5f ? -glm::normalize(grad) : glm::vec3(0.0f, 1.0f, 0.0f);
        // the colour: the highest solid corner's (grass shows from above)
        int best = -1, bestKey = -1;
        for (int k = 0; k < 8; k++) {
            if (!((mask >> k) & 1)) continue;
            int key = ((k >> 1) & 1) * 100000 + c[k]->topY + 1;
            if (key > bestKey) { bestKey = key; best = k; }
        }
        v.r = c[best]->r; v.g = c[best]->g; v.b = c[best]->b; v.glow = c[best]->glow;
        cube(x, y, z) = (int32_t)out.verts.size();
        out.verts.push_back(v);
    }

    // ── a quad every edge from solid to open ──
    for (int y = std::max(lo.y, 0); y <= std::min(hi.y, g.ny - 1); y++)
    for (int z = std::max(lo.z, 0); z <= std::min(hi.z, g.nz - 1); z++)
    for (int x = std::max(lo.x, 0); x <= std::min(hi.x, g.nx - 1); x++) {
        const bool s0 = g.at(x, y, z).d > iso;
        for (int a = 0; a < 3; a++) {
            if (tops && a != 1) continue;
            glm::ivec3 p(x, y, z), q = p; q[a]++;
            if (q.x >= g.nx || q.y >= g.ny || q.z >= g.nz) continue;
            const bool s1 = g.at(q.x, q.y, q.z).d > iso;
            if (s0 == s1) continue;
            if (tops && !s0) continue;                            // (faces down)
            const int b = (a + 1) % 3, c = (a + 2) % 3;           // b × c = a
            // the 4 cubes round the edge, counter-clockwise seen from +a
            // (an edge on the grid's rim has fewer: no quad)
            static const int ORD[4][2] = { {-1,-1}, {0,-1}, {0,0}, {-1,0} };
            uint32_t v[4]; bool ok = true;
            for (int k = 0; k < 4; k++) {
                glm::ivec3 cc = p;
                cc[b] += ORD[k][0]; cc[c] += ORD[k][1];
                if (cc[b] < 0 || cc[c] < 0 || cc.x >= cx || cc.y >= cy || cc.z >= cz) { ok = false; break; }
                int32_t id = cube(cc.x, cc.y, cc.z);
                if (id < 0) { ok = false; break; }
                v[k] = (uint32_t)id;
            }
            if (!ok) continue;
            Quad qd; qd.owner = p; qd.axis = a; qd.dir = s0 ? 1 : -1;
            if (s0) { qd.v[0] = v[0]; qd.v[1] = v[1]; qd.v[2] = v[2]; qd.v[3] = v[3]; }
            else    { qd.v[0] = v[0]; qd.v[1] = v[3]; qd.v[2] = v[2]; qd.v[3] = v[1]; }   // facing -a: the other way round
            out.quads.push_back(qd);
        }
    }
}

// A quad as two triangles, split along its shorter diagonal (fewer
// slivers on a bent quad)
inline void triangles(const Mesh& m, const Quad& q, uint32_t base, std::vector<uint32_t>& idx) {
    const glm::vec3 &a = m.verts[q.v[0]].p, &b = m.verts[q.v[1]].p, &c = m.verts[q.v[2]].p, &d = m.verts[q.v[3]].p;
    if (glm::dot(a - c, a - c) <= glm::dot(b - d, b - d))
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    else
        idx.insert(idx.end(), { base, base + 1, base + 3, base + 1, base + 2, base + 3 });
}

} // namespace surfacenets
