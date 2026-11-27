#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "PropRenderer.h"
#include "Shader.h"
#include "Sky.h"
#include "AtlasTexture.h"
#include "../world/StructureManager.h"
#include "../world/PlanManager.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

// ══════════════════════════════════════════════════════════
// RoofRenderer — roofs as one sloped surface, eaves overhanging
// ══════════════════════════════════════════════════════════
// A roof piece (structures.toml shape = "slope") is a staircase
// of voxels: that's what you stand on, build against, break. With
// Settings → Roofs: Smooth this draws over it what a roof looks
// like (shaders/roof.*):
//   - one sloped top surface through the steps' outer edges, and a
//     smooth underside through their inner ones, so the steps are
//     hidden inside a slab (inside the house the ceiling is smooth)
//   - courses: rows of tiles, slates or thatch running along the
//     eave, each a little tilted (light catches its lower lip, the
//     row above shades its top), tiles and slates staggered with
//     joints, thatch in combed streaks; the block's own texture under
//     it, offset row to row so it doesn't repeat
//   - overhang: the eave runs on 3 voxels past the wall and down the
//     slope, the ends (verges) 2 past the gable, with a fascia, barge
//     boards and a soffit under them. Where another roof piece carries
//     on (along the ridge, or down the slope) there's no overhang
// Roofs of the hammer's Build tab (world/PlanManager.h, plan::Roof) are
// drawn the same way: each of their slopes a plane, cut where it meets
// the others (ridge, hips), edged with a fascia all round.
// Only drawn: the voxels are as they were (the plain setting shows
// them). Lit like props: the world's baked light per vertex (block
// RGB, sky, canopy), sampled just off the surface.
class RoofRenderer {
public:
    bool init() { return shader.load("shaders/roof.vert", "shaders/roof.frag"); }
    void cleanup() {
        for (auto& [id, m] : meshes) m.release();
        for (auto& [id, m] : planMeshes) m.release();
        meshes.clear(); planMeshes.clear(); seen = ~0ull; seenPlans = ~0ull;
    }
    void setBlocks(std::shared_ptr<const BlockRenderTable> t) { table = std::move(t); cleanup(); }

    // Keep the meshes in step with the roofs placed (cheap when
    // nothing changed), relight a few each frame, draw them all
    void draw(const StructureManager& sm, const PlanManager* plans, const glm::mat4& view, const glm::mat4& proj, glm::vec3 camPos,
              const AtlasTexture& atlas, const PropLightFn& lightAt) {
        if (sm.revision() != seen) { rebuild(sm); seen = sm.revision(); }
        if (plans && plans->revision() != seenPlans) { rebuildPlans(*plans); seenPlans = plans->revision(); }
        if (!plans && !planMeshes.empty()) { for (auto& [id, m] : planMeshes) m.release(); planMeshes.clear(); seenPlans = ~0ull; }
        if (meshes.empty() && planMeshes.empty()) return;
        // relight round-robin: light changes slowly (lamps, chunks loading)
        int n = 0;
        for (auto* all : { &meshes, &planMeshes })
            for (auto& [id, m] : *all) {
                if (n >= 6) break;
                if (m.relightIn-- <= 0) {
                    relight(m, lightAt);
                    // just made: soon again (the world's light is baked again after the edit)
                    m.relightIn = m.fresh > 0 ? 12 : 90 + (int)(id % 30);
                    if (m.fresh > 0) m.fresh--;
                    n++;
                }
            }
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);               // thin edges are seen from both sides
        shader.use();
        GLuint p = shader.id;
        auto v3 = [&](const char* nm, glm::vec3 v) { glUniform3f(glGetUniformLocation(p, nm), v.x, v.y, v.z); };
        auto f1 = [&](const char* nm, float v) { glUniform1f(glGetUniformLocation(p, nm), v); };
        glUniformMatrix4fv(glGetUniformLocation(p, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(p, "uProjection"), 1, GL_FALSE, glm::value_ptr(proj));
        v3("uCamPos", camPos);
        glUniform1i(glGetUniformLocation(p, "uAtlas"), 0);
        glUniform2f(glGetUniformLocation(p, "uTileUV"), atlas.tileU(), atlas.tileV());
        f1("uVoxelsPerTile", atlas.voxelsPerTile());
        const FrameLight& fl = gFrameLight;
        v3("uSkyLight", fl.sky.skyLight); v3("uLightDir", fl.sky.lightDir()); v3("uAmbient", fl.sky.ambient);
        glUniform1i(glGetUniformLocation(shader.id, "uLightCurve"), gSettings.lightCurve);   // block light's fade (Settings)
        v3("uFogHorizon", fl.sky.horizon); v3("uFogSunGlow", fl.sky.sunGlow); v3("uSunDir", fl.sky.sunDir);
        f1("uFogDensity", fl.fogDensity); f1("uNight", fl.sky.night);
        terrainlight::bindFrame(p);
        atlas.bind(0);
        const float far2 = 900.0f * 900.0f;
        for (auto* all : { &meshes, &planMeshes })
            for (auto& [id, m] : *all) {
                if (!m.count) continue;
                if (all == &planMeshes && !cuts.empty()) {      // the build camera's cutaway
                    auto c = cuts.find((uint32_t)(id >> 16));
                    if (c != cuts.end() && m.baseY >= (float)c->second) continue;
                }
                glm::vec3 d = m.centre - camPos;
                if (glm::dot(d, d) > far2) continue;
                glBindVertexArray(m.vao);
                glDrawArrays(GL_TRIANGLES, 0, m.count);
            }
        glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
    }

    size_t count() const { return meshes.size() + planMeshes.size(); }
    // plan id → world height: its roofs from there up aren't drawn (the build camera's cutaway)
    std::unordered_map<uint32_t, int> cuts;

    // ── The shape (public for tests) ──────────────────────
    // A roof piece's overhangs: 0 where another roof piece carries on
    struct Overhang { float eave = 0, left = 0, right = 0; };
    static constexpr float EAVE = 3.0f, VERGE = 2.0f;

    // Is the voxel at p part of another roof piece?
    static bool roofAt(const StructureManager& sm, glm::ivec3 p, uint32_t self) {
        const StructureInstance* o = sm.instanceAt(p);
        if (!o || o->id == self) return false;
        const StructureType* t = gStructures.get(o->type);
        return t && t->slope;
    }
    // Local (unturned) corner coordinates → world, for a piece W wide, R run
    static glm::vec3 toWorld(glm::vec3 lo, int rot, int W, int R, float x, float y, float z) {
        switch (rot & 3) {
        case 1:  return lo + glm::vec3(R - z, y, x);
        case 2:  return lo + glm::vec3(W - x, y, R - z);
        case 3:  return lo + glm::vec3(z, y, W - x);
        default: return lo + glm::vec3(x, y, z);
        }
    }
    static glm::vec3 dirToWorld(int rot, glm::vec3 n) {
        switch (rot & 3) {
        case 1:  return { -n.z, n.y, n.x };
        case 2:  return { -n.x, n.y, -n.z };
        case 3:  return { n.z, n.y, -n.x };
        default: return n;
        }
    }
    // The low corner of a placed roof's box. A piece's origin is where
    // its unturned (0,0,0) voxel went; after a turn that's another
    // corner of the box, and toWorld() works from the low one
    static glm::vec3 cornerOf(const StructureInstance& s, const StructureType& t) {
        return glm::vec3(s.origin - gStructures.shape(t.id, s.spec, 0, s.rot).anchor);
    }
    // A placed roof's own (unturned) size: its width can be its own (PieceSpec w)
    static glm::ivec3 sizeOf(const StructureInstance& s, const StructureType& t) {
        return gStructures.shape(t.id, s.spec, 0, 0).size;
    }
    static Overhang overhangOf(const StructureManager& sm, const StructureInstance& s, const StructureType& t) {
        const glm::ivec3 sz = sizeOf(s, t);
        const int W = sz.x, R = sz.z;
        const float k = (float)sz.y / std::max(1, R);
        const glm::vec3 lo = cornerOf(s, t);
        auto voxel = [&](float x, float y, float z) {   // the voxel whose corner-space centre is (x, y, z)
            return glm::ivec3(glm::floor(toWorld(lo, s.rot, W, R, x, y, z)));
        };
        const int zc = R / 2;
        const float yc = k * (zc + 1) - 0.5f;           // inside the slab at that row
        Overhang o;
        o.left  = roofAt(sm, voxel(-0.5f, yc, zc + 0.5f), s.id) ? 0.0f : VERGE;
        o.right = roofAt(sm, voxel(W + 0.5f, yc, zc + 0.5f), s.id) ? 0.0f : VERGE;
        o.eave  = (roofAt(sm, voxel(W * 0.5f, -0.5f, -0.5f), s.id) ||
                   roofAt(sm, voxel(W * 0.5f, -1.5f, -0.5f), s.id)) ? 0.0f : EAVE;
        return o;
    }

private:
    // pos (world), uv (voxels: along the eave, up the slope), normal,
    // atlas tile, style (0 tiles, 1 slate, 2 thatch, 3 trim), light
    struct Vert {
        float x, y, z, u, v, nx, ny, nz;
        uint8_t tileX, tileY, style, pad;
        uint8_t r, g, b, sky;
        float shade;
    };
    struct Mesh {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        std::vector<Vert> verts;
        glm::vec3 centre{ 0.0f };
        float baseY = 0.0f;            // a plan roof's eaves (world)
        uint64_t sig = 0;
        int relightIn = 0;
        int fresh = 0;                 // relights left at the quick pace
        void release() {
            if (vbo) glDeleteBuffers(1, &vbo);
            if (vao) glDeleteVertexArrays(1, &vao);
            vao = vbo = 0; count = 0;
        }
    };

    Shader shader;
    std::shared_ptr<const BlockRenderTable> table;
    std::unordered_map<uint64_t, Mesh> meshes;            // roof pieces, by instance id
    std::unordered_map<uint64_t, Mesh> planMeshes;     // (plan << 16) | roof
    uint64_t seen = ~0ull, seenPlans = ~0ull;

    static uint8_t styleOf(const std::string& block) {
        if (block.find("thatch") != std::string::npos) return 2;
        if (block.find("slate") != std::string::npos) return 1;
        return 0;
    }

    void rebuild(const StructureManager& sm) {
        std::unordered_map<uint32_t, bool> alive;
        sm.forEachInstance([&](const StructureInstance& s) {
            const StructureType* t = gStructures.get(s.type);
            if (!t || !t->slope) return;
            alive[s.id] = true;
            Overhang o = overhangOf(sm, s, *t);
            uint64_t sig = (uint64_t)s.type * 1000003ull ^ ((uint64_t)(s.origin.x & 0xFFFFF) << 20) ^
                           ((uint64_t)(s.origin.z & 0xFFFFF) << 40) ^ ((uint64_t)s.origin.y * 7919ull) ^ ((uint64_t)s.rot << 60) ^
                           ((uint64_t)(o.eave > 0) << 61) ^ ((uint64_t)(o.left > 0) << 62) ^ ((uint64_t)(o.right > 0) << 63);
            Mesh& m = meshes[s.id];
            if (m.count && m.sig == sig) return;
            m.sig = sig;
            build(m, s, *t, o);
        });
        for (auto it = meshes.begin(); it != meshes.end();)
            if (!alive.count(it->first)) { it->second.release(); it = meshes.erase(it); }
            else ++it;
    }

    void build(Mesh& m, const StructureInstance& s, const StructureType& t, const Overhang& o) {
        const glm::ivec3 own = sizeOf(s, t);
        const int W = own.x, R = own.z, H = own.y;
        const float k = (float)H / std::max(1, R), th = (float)t.slopeThick;
        const float L = std::sqrt(1.0f + k * k);         // slope length per voxel of run
        const glm::vec3 lo = cornerOf(s, t);
        const uint16_t block = gStructures.roleBlock(t, s.spec, "roof");   // the palette's roof, if it has one
        const std::string bname = gRegistry.get(block).name;
        const uint8_t style = styleOf(bname);
        uint8_t tx = 0, ty = 0, ex = 0, ey = 0;
        if (table) {
            const BlockRenderInfo& b = table->get(block);
            tx = b.tileX[0]; ty = b.tileY[0];             // the top tile for the courses
            ex = b.tileX[2]; ey = b.tileY[2];             // the side tile for the edges
        }
        const float eps = 0.04f;
        auto top = [&](float z) { return k * z + 1.0f + eps; };
        auto under = [&](float z) { return k * z - th - eps; };
        const float x0 = -o.left, x1 = W + o.right, z0 = -o.eave, z1 = (float)R;
        // along-the-eave and up-the-slope texture coordinates, from the
        // world so neighbouring pieces' courses line up
        const glm::vec3 along = dirToWorld(s.rot, { 1, 0, 0 });
        auto uvAt = [&](glm::vec3 w, float yLocalTop) {
            float u = glm::dot(w, along);
            float v = (lo.y + yLocalTop) * (L / std::max(k, 0.01f));
            return glm::vec2(u, v);
        };
        std::vector<Vert>& out = m.verts;
        out.clear();
        auto put = [&](glm::vec3 local, glm::vec3 nLocal, glm::vec2 uv, uint8_t st, bool edge) {
            glm::vec3 w = toWorld(lo, s.rot, W, R, local.x, local.y, local.z);
            glm::vec3 n = glm::normalize(dirToWorld(s.rot, nLocal));
            out.push_back({ w.x, w.y, w.z, uv.x, uv.y, n.x, n.y, n.z,
                            edge ? ex : tx, edge ? ey : ty, st, 0, 0, 0, 0, 255, 0.0f });
        };
        auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n,
                        glm::vec2 ua, glm::vec2 ub, glm::vec2 uc, glm::vec2 ud, uint8_t st, bool edge) {
            put(a, n, ua, st, edge); put(b, n, ub, st, edge); put(c, n, uc, st, edge);
            put(a, n, ua, st, edge); put(c, n, uc, st, edge); put(d, n, ud, st, edge);
        };
        // the top and the underside, in 3 x 3 voxel cells (the light is per vertex)
        const glm::vec3 nTop = glm::normalize(glm::vec3(0, 1, -k)), nUnder = -nTop;
        const int cx = std::max(1, (int)std::ceil((x1 - x0) / 3.0f)), cz = std::max(1, (int)std::ceil((z1 - z0) / 3.0f));
        for (int i = 0; i < cx; i++)
            for (int j = 0; j < cz; j++) {
                float xa = x0 + (x1 - x0) * i / cx, xb = x0 + (x1 - x0) * (i + 1) / cx;
                float za = z0 + (z1 - z0) * j / cz, zb = z0 + (z1 - z0) * (j + 1) / cz;
                glm::vec3 a(xa, top(za), za), b(xb, top(za), za), c(xb, top(zb), zb), d(xa, top(zb), zb);
                auto uv = [&](glm::vec3 p) { return uvAt(toWorld(lo, s.rot, W, R, p.x, p.y, p.z), p.y); };
                quad(a, b, c, d, nTop, uv(a), uv(b), uv(c), uv(d), style, false);
                glm::vec3 a2(xa, under(za), za), b2(xb, under(za), za), c2(xb, under(zb), zb), d2(xa, under(zb), zb);
                quad(a2, d2, c2, b2, nUnder, uv(a2), uv(d2), uv(c2), uv(b2), 3, true);
            }
        // the fascia along the eave, and the ridge end
        auto band = [&](float z, glm::vec3 n) {
            glm::vec3 a(x0, under(z), z), b(x1, under(z), z), c(x1, top(z), z), d(x0, top(z), z);
            quad(a, b, c, d, n, { x0, 0 }, { x1, 0 }, { x1, top(z) - under(z) }, { x0, top(z) - under(z) }, 3, true);
        };
        band(z0, { 0, 0, -1 });
        band(z1, { 0, 0, 1 });
        // the barge boards at each end
        for (int side = 0; side < 2; side++) {
            float x = side ? x1 : x0;
            glm::vec3 n(side ? 1.0f : -1.0f, 0, 0);
            const int seg = std::max(1, (int)std::ceil((z1 - z0) / 3.0f));
            for (int j = 0; j < seg; j++) {
                float za = z0 + (z1 - z0) * j / seg, zb = z0 + (z1 - z0) * (j + 1) / seg;
                glm::vec3 a(x, under(za), za), b(x, under(zb), zb), c(x, top(zb), zb), d(x, top(za), za);
                quad(a, b, c, d, n, { za * L, 0 }, { zb * L, 0 }, { zb * L, th + 1 }, { za * L, th + 1 }, 3, true);
            }
        }
        glm::vec3 c = toWorld(lo, s.rot, W, R, W * 0.5f, H * 0.5f, R * 0.5f);
        m.centre = c;
        upload(m);
        m.relightIn = 0;
        m.fresh = 5;
    }

    // ── Plan roofs (the Build tab) ────────────────────────
    void rebuildPlans(const PlanManager& pm) {
        std::unordered_map<uint64_t, bool> alive;
        for (const plan::Plan& p : pm.all())
            for (size_t i = 0; i < p.roofs.size(); i++) {
                const plan::Roof& r = p.roofs[i];
                uint64_t key = ((uint64_t)p.id << 16) | (uint64_t)i;
                alive[key] = true;
                uint16_t block = r.block ? r.block : pm.looksFor(p).roof;
                uint64_t sig = 1469598103934665603ull;
                for (int v : { r.a.x, r.a.y, r.b.x, r.b.y, (int)r.style, r.axis, r.peak, r.overhang, r.thick, p.roofBase(r), p.levelY(r.level),
                               p.origin.x, p.origin.z, (int)block })
                    sig = (sig ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
                Mesh& m = planMeshes[key];
                if (m.count && m.sig == sig) continue;
                m.sig = sig;
                buildPlanRoof(m, p, r, block);
            }
        for (auto it = planMeshes.begin(); it != planMeshes.end();)
            if (!alive.count(it->first)) { it->second.release(); it = planMeshes.erase(it); }
            else ++it;
    }
    // a convex polygon (x, z) cut to where a * x + b * z + c <= 0
    static std::vector<glm::vec2> clipPoly(const std::vector<glm::vec2>& in, glm::vec3 h) {
        std::vector<glm::vec2> out;
        for (size_t i = 0; i < in.size(); i++) {
            glm::vec2 p = in[i], q = in[(i + 1) % in.size()];
            float dp = h.x * p.x + h.y * p.y + h.z, dq = h.x * q.x + h.y * q.y + h.z;
            if (dp <= 0) out.push_back(p);
            if ((dp < 0) != (dq < 0) && std::fabs(dp - dq) > 1e-6f) out.push_back(p + (q - p) * (dp / (dp - dq)));
        }
        return out;
    }
    void buildPlanRoof(Mesh& m, const plan::Plan& p, const plan::Roof& r, uint16_t block) {
        const std::vector<glm::vec3> F = r.facets();
        float kmax = 0.0f;
        for (const glm::vec3& f : F) kmax = std::max(kmax, glm::length(glm::vec2(f.x, f.y)));
        const glm::vec3 O((float)p.origin.x, (float)(p.levelY(r.level) + p.roofBase(r)), (float)p.origin.z);
        m.baseY = O.y;
        const float eps = 0.04f;
        const float upTop = 1.0f + kmax * 0.5f + eps, downUnder = (float)r.thick - 1.0f + std::max(1.0f, kmax) + eps;
        const glm::vec2 lo = glm::vec2(r.lo() - glm::ivec2(r.overhang)), hi = glm::vec2(r.hi() + glm::ivec2(r.overhang + 1));
        const std::string bname = gRegistry.get(block).name;
        const uint8_t style = styleOf(bname);
        uint8_t tx = 0, ty = 0, ex = 0, ey = 0;
        if (table) {
            const BlockRenderInfo& b = table->get(block);
            tx = b.tileX[0]; ty = b.tileY[0]; ex = b.tileX[2]; ey = b.tileY[2];
        }
        std::vector<Vert>& out = m.verts;
        out.clear();
        auto put = [&](glm::vec3 w, glm::vec3 n, glm::vec2 uv, uint8_t st, bool edge) {
            out.push_back({ w.x, w.y, w.z, uv.x, uv.y, n.x, n.y, n.z, edge ? ex : tx, edge ? ey : ty, st, 0, 0, 0, 0, 255, 0.0f });
        };
        const std::vector<glm::vec2> rect = { lo, { hi.x, lo.y }, hi, { lo.x, hi.y } };
        for (size_t i = 0; i < F.size(); i++) {
            // this slope's part: where it's the lowest
            std::vector<glm::vec2> poly = rect;
            for (size_t j = 0; j < F.size() && poly.size() >= 3; j++)
                if (j != i) poly = clipPoly(poly, F[i] - F[j]);
            if (poly.size() < 3) continue;
            const glm::vec3 f = F[i];
            const glm::vec2 g(f.x, f.y);
            const float k = glm::length(g);
            const glm::vec2 up = k > 1e-4f ? g / k : glm::vec2(0, 1), along(-up.y, up.x);
            const float L = std::sqrt(1.0f + k * k);
            const glm::vec3 nTop = glm::normalize(glm::vec3(-f.x, 1.0f, -f.y));
            auto h = [&](glm::vec2 q) { return f.x * q.x + f.y * q.y + f.z; };
            auto uv = [&](glm::vec2 q) { glm::vec2 w = q + glm::vec2(O.x, O.z); return glm::vec2(glm::dot(w, along), glm::dot(w, up) * L); };
            // in 3 x 3 cells (the light is per vertex)
            glm::vec2 plo(1e9f), phi(-1e9f);
            for (glm::vec2 q : poly) { plo = glm::min(plo, q); phi = glm::max(phi, q); }
            for (float cz = std::floor(plo.y / 3.0f) * 3.0f; cz < phi.y; cz += 3.0f)
                for (float cx = std::floor(plo.x / 3.0f) * 3.0f; cx < phi.x; cx += 3.0f) {
                    std::vector<glm::vec2> c = poly;
                    c = clipPoly(c, { -1, 0, cx }); if (c.size() < 3) continue;
                    c = clipPoly(c, { 1, 0, -(cx + 3.0f) }); if (c.size() < 3) continue;
                    c = clipPoly(c, { 0, -1, cz }); if (c.size() < 3) continue;
                    c = clipPoly(c, { 0, 1, -(cz + 3.0f) }); if (c.size() < 3) continue;
                    for (size_t t = 1; t + 1 < c.size(); t++) {
                        glm::vec2 a = c[0], b = c[t], d = c[t + 1];
                        for (glm::vec2 q : { a, b, d }) put(O + glm::vec3(q.x, h(q) + upTop, q.y), nTop, uv(q), style, false);
                        for (glm::vec2 q : { a, d, b }) put(O + glm::vec3(q.x, h(q) - downUnder, q.y), -nTop, uv(q), 3, true);
                    }
                }
            // the fascia and barge boards: its edges on the roof's outline
            for (size_t e = 0; e < poly.size(); e++) {
                glm::vec2 a = poly[e], b = poly[(e + 1) % poly.size()];
                glm::vec2 n(0);
                if (std::fabs(a.x - lo.x) < 1e-3f && std::fabs(b.x - lo.x) < 1e-3f) n = { -1, 0 };
                else if (std::fabs(a.x - hi.x) < 1e-3f && std::fabs(b.x - hi.x) < 1e-3f) n = { 1, 0 };
                else if (std::fabs(a.y - lo.y) < 1e-3f && std::fabs(b.y - lo.y) < 1e-3f) n = { 0, -1 };
                else if (std::fabs(a.y - hi.y) < 1e-3f && std::fabs(b.y - hi.y) < 1e-3f) n = { 0, 1 };
                if (n == glm::vec2(0)) continue;
                const int seg = std::max(1, (int)std::ceil(glm::length(b - a) / 3.0f));
                for (int j = 0; j < seg; j++) {
                    glm::vec2 qa = a + (b - a) * ((float)j / seg), qb = a + (b - a) * ((float)(j + 1) / seg);
                    glm::vec3 A = O + glm::vec3(qa.x, h(qa) - downUnder, qa.y), B = O + glm::vec3(qb.x, h(qb) - downUnder, qb.y);
                    glm::vec3 C = O + glm::vec3(qb.x, h(qb) + upTop, qb.y), D = O + glm::vec3(qa.x, h(qa) + upTop, qa.y);
                    glm::vec3 nn(n.x, 0, n.y);
                    float ua = glm::dot(qa, glm::vec2(-n.y, n.x)), ub = glm::dot(qb, glm::vec2(-n.y, n.x)), th = upTop + downUnder;
                    put(A, nn, { ua, 0 }, 3, true); put(B, nn, { ub, 0 }, 3, true); put(C, nn, { ub, th }, 3, true);
                    put(A, nn, { ua, 0 }, 3, true); put(C, nn, { ub, th }, 3, true); put(D, nn, { ua, th }, 3, true);
                }
            }
        }
        m.centre = O + glm::vec3((lo.x + hi.x) * 0.5f, r.peak * 0.5f, (lo.y + hi.y) * 0.5f);
        upload(m);
        m.relightIn = 0;
        m.fresh = 5;
    }

    void relight(Mesh& m, const PropLightFn& lightAt) {
        if (!m.count) return;
        for (Vert& v : m.verts) {
            glm::vec3 n(v.nx, v.ny, v.nz);
            PropLightSample s = lightAt(glm::vec3(v.x, v.y, v.z) + n * 0.9f);
            v.r = (uint8_t)(s.r * 17); v.g = (uint8_t)(s.g * 17); v.b = (uint8_t)(s.b * 17);
            v.sky = (uint8_t)(s.sky * 17); v.shade = std::clamp(s.shade, 0.0f, 1.0f);
        }
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m.verts.size() * sizeof(Vert)), m.verts.data(), GL_DYNAMIC_DRAW);
    }

    static void upload(Mesh& m) {
        if (!m.vao) { glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo); }
        m.count = (GLsizei)m.verts.size();
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m.verts.size() * sizeof(Vert)), m.verts.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)offsetof(Vert, x));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)offsetof(Vert, u));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)offsetof(Vert, nx));
        glEnableVertexAttribArray(2);
        glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, sizeof(Vert), (void*)offsetof(Vert, tileX));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vert), (void*)offsetof(Vert, r));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)offsetof(Vert, shade));
        glEnableVertexAttribArray(5);
        glBindVertexArray(0);
    }
};
