#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Mesher.h"
#include "Shader.h"
#include "Sky.h"
#include "AtlasTexture.h"
#include "../physics/RigidVoxelBody.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ══════════════════════════════════════════════════════════
// PropRenderer — loose voxel objects, drawn like the world
// ══════════════════════════════════════════════════════════
// Felled logs, wood on the ground, leaves and chips
// (world/Woodcutting.h). They use the block atlas and the
// chunk shader's lighting (shaders/prop.*), so a log looks like
// the tree it was:
//   - each face shows its block's atlas tile (bark on the sides,
//     rings on the cut ends), laid out from the object's own
//     position so the texture turns with it
//   - each face is lit by the world's baked light where it is
//     now: sampled half a voxel out in front of it (block RGB,
//     sky, canopy shade) whenever the object has moved, so a log
//     lying in shade is dark and one by a lamp is lit by it
//   - small things (a piece of wood, a leaf) take one light
//     sample for the whole object
// Uses the frame's sky, sun and haze (render/Sky.h gFrameLight).

// What the world's light is at a point (from the baked layer):
// block r, g, b and sky as 0..15, and canopy shade 0..1
struct PropLightSample { uint8_t r = 0, g = 0, b = 0, sky = 15; float shade = 0.0f; };
using PropLightFn = std::function<PropLightSample(glm::vec3)>;

class PropRenderer {
public:
    // One mesh: a log's voxels, or a single block cube
    struct Mesh {
        GLuint  vao = 0, vbo = 0, lightVbo = 0;
        GLsizei count = 0;
        // per face (6 vertices each): where to sample the light,
        // in object space, and the face's normal
        std::vector<glm::vec3> samplePos, sampleNormal;
        uint32_t litFor = 0xFFFFFFFFu;     // the object's 'moved' count when last lit
        void release() {
            if (vbo) glDeleteBuffers(1, &vbo);
            if (lightVbo) glDeleteBuffers(1, &lightVbo);
            if (vao) glDeleteVertexArrays(1, &vao);
            vao = vbo = lightVbo = 0; count = 0;
        }
    };

    bool init() {
        if (!shader.load("shaders/prop.vert", "shaders/prop.frag")) return false;
        return true;
    }
    void cleanup() {
        for (auto& [k, m] : cubes) m.release();
        cubes.clear();
    }

    // Tile per block and face, from the resource pack (rebuild
    // when the pack changes)
    void setBlocks(std::shared_ptr<const BlockRenderTable> t) { table = std::move(t); for (auto& [k, m] : cubes) m.release(); cubes.clear(); }

    // A log: every voxel face with no voxel beside it, in the
    // voxels' original world positions (RigidVoxelBody::transform
    // moves them)
    void buildVoxels(Mesh& m, const std::vector<VoxelPiece>& pieces) {
        std::unordered_set<int64_t> has;
        for (const VoxelPiece& v : pieces) has.insert(key(v.p));
        std::vector<PropVertex> verts;
        m.samplePos.clear(); m.sampleNormal.clear();
        for (const VoxelPiece& v : pieces)
            for (int f = 0; f < 6; f++) {
                if (has.count(key(v.p + NORMAL[f]))) continue;
                addFace(verts, glm::vec3(v.p), f, v.id);
                m.samplePos.push_back(glm::vec3(v.p) + 0.5f + glm::vec3(NORMAL[f]) * 1.0f);
                m.sampleNormal.push_back(glm::vec3(NORMAL[f]));
            }
        upload(m, verts);
    }

    // Re-sample the light of each face where the object is now
    void relight(Mesh& m, const glm::mat4& model, const PropLightFn& lightAt) {
        if (!m.count) return;
        std::vector<PropLight> L(m.count);
        for (size_t f = 0; f < m.samplePos.size(); f++) {
            PropLightSample s = lightAt(glm::vec3(model * glm::vec4(m.samplePos[f], 1.0f)));
            PropLight pl{ (uint8_t)(s.r * 17), (uint8_t)(s.g * 17), (uint8_t)(s.b * 17), (uint8_t)(s.sky * 17),
                          std::clamp(s.shade, 0.0f, 1.0f) };
            for (int k = 0; k < 6; k++) L[f * 6 + k] = pl;
        }
        glBindBuffer(GL_ARRAY_BUFFER, m.lightVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(L.size() * sizeof(PropLight)), L.data(), GL_DYNAMIC_DRAW);
    }

    // ── Drawing ───────────────────────────────────────────
    void begin(const glm::mat4& view, const glm::mat4& proj, glm::vec3 camPos, const AtlasTexture& atlas) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
        shader.use();
        GLuint p = shader.id;
        auto v3 = [&](const char* n, glm::vec3 v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); };
        auto f1 = [&](const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); };
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
        atlas.bind(0);
        glUniform1f(glGetUniformLocation(p, "uAlpha"), 1.0f);
        locAlpha = glGetUniformLocation(p, "uAlpha");
        locModel = glGetUniformLocation(p, "uModel");
        locFlat = glGetUniformLocation(p, "uFlat");
        locFlatLight = glGetUniformLocation(p, "uFlatLight");
        locFlatShade = glGetUniformLocation(p, "uFlatShade");
    }
    // A mesh with its own per-face light
    void draw(const Mesh& m, const glm::mat4& model) {
        if (!m.count) return;
        glUniform1i(locFlat, 0);
        glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(m.vao);
        glDrawArrays(GL_TRIANGLES, 0, m.count);
    }
    // One block as a cube (centred on 0, 1 voxel; scale with
    // the model), lit by one sample
    void drawBlock(uint16_t block, const glm::mat4& model, const PropLightSample& s) {
        Mesh& m = cubeFor(block);
        if (!m.count) return;
        glUniform1i(locFlat, 1);
        glUniform4f(locFlatLight, s.r / 15.0f, s.g / 15.0f, s.b / 15.0f, s.sky / 15.0f);
        glUniform1f(locFlatShade, s.shade);
        glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(m.vao);
        glDrawArrays(GL_TRIANGLES, 0, m.count);
    }
    // A see-through mesh (a ghost: the hammer's preview), drawn over
    // the world without hiding what's behind it
    void drawGhost(const Mesh& m, const glm::mat4& model, float alpha) {
        if (!m.count) return;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform1f(locAlpha, alpha);
        draw(m, model);
        glUniform1f(locAlpha, 1.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    void end() { glBindVertexArray(0); }

private:
    struct PropVertex { float x, y, z; uint8_t face, tileX, tileY, glow; };
    struct PropLight  { uint8_t r, g, b, sky; float shade; };
    static constexpr glm::ivec3 NORMAL[6] = { {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {-1,0,0}, {1,0,0} };

    Shader shader;
    std::shared_ptr<const BlockRenderTable> table;
    std::unordered_map<uint16_t, Mesh> cubes;
    GLint locAlpha = -1;
    GLint locModel = -1, locFlat = -1, locFlatLight = -1, locFlatShade = -1;

    static int64_t key(glm::ivec3 p) {
        return ((int64_t)(p.x & 0x1FFFFF) << 42) | ((int64_t)(p.y & 0x1FFFFF) << 21) | (int64_t)(p.z & 0x1FFFFF);
    }

    // The face of the voxel at 'lo', counter-clockwise from outside
    void addFace(std::vector<PropVertex>& out, glm::vec3 lo, int f, uint16_t id) {
        static const float C[6][4][3] = {
            {{0,1,0},{0,1,1},{1,1,1},{1,1,0}},   // +Y
            {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},   // -Y
            {{1,0,1},{1,1,1},{0,1,1},{0,0,1}},   // +Z
            {{0,0,0},{0,1,0},{1,1,0},{1,0,0}},   // -Z
            {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},   // -X
            {{1,0,0},{1,1,0},{1,1,1},{1,0,1}},   // +X
        };
        uint8_t tx = 0, ty = 0, glow = 0;
        if (table) {
            const BlockRenderInfo& b = table->get(id);
            tx = b.tileX[f]; ty = b.tileY[f]; glow = b.glow ? 1 : 0;
        }
        auto v = [&](int k) {
            glm::vec3 p = lo + glm::vec3(C[f][k][0], C[f][k][1], C[f][k][2]);
            out.push_back({ p.x, p.y, p.z, (uint8_t)f, tx, ty, glow });
        };
        v(0); v(1); v(2); v(0); v(2); v(3);
    }

    Mesh& cubeFor(uint16_t block) {
        Mesh& m = cubes[block];
        if (!m.count) {
            std::vector<PropVertex> verts;
            for (int f = 0; f < 6; f++) addFace(verts, glm::vec3(-0.5f), f, block);
            upload(m, verts);
        }
        return m;
    }

    static void upload(Mesh& m, const std::vector<PropVertex>& verts) {
        if (!m.vao) { glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo); glGenBuffers(1, &m.lightVbo); }
        m.count = (GLsizei)verts.size();
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(PropVertex)), verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PropVertex), (void*)offsetof(PropVertex, x));
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, sizeof(PropVertex), (void*)offsetof(PropVertex, face));
        glEnableVertexAttribArray(1);
        // light: full sky, no block light, until relit
        std::vector<PropLight> L(verts.size(), PropLight{ 0, 0, 0, 255, 0.0f });
        glBindBuffer(GL_ARRAY_BUFFER, m.lightVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(L.size() * sizeof(PropLight)), L.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PropLight), (void*)offsetof(PropLight, r));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(PropLight), (void*)offsetof(PropLight, shade));
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);
        m.litFor = 0xFFFFFFFFu;
    }
};
