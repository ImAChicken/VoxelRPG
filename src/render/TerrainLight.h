#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <GLFW/glfw3.h>
#include "../chunk/TerrainGenerator.h"
#include "Sky.h"
#include "Wind.h"
#include "../game/Settings.h"

// ══════════════════════════════════════════════════════════
// TerrainLight — sun and shade from the shape of the land
// ══════════════════════════════════════════════════════════
// A coarse height map of the whole world (one cell per CELL_MIN
// voxels or more, at most MAX_CELLS a side), from the generator,
// built once on a thread of its own. From it, for the light of the
// moment (sun or moon):
//
//   sun shading      the ground's smooth normal (the height map's
//                    slope): a hillside facing the sun is bright,
//                    one facing away dim, changing through the day.
//                    The voxels' top faces near the ground take it
//                    instead of straight up (shaders/terrainlight.glsl
//                    terrainNormal); the distant terrain too.
//   terrain shadows  per cell, the "shadow height": anything lower
//                    is behind a hill from the light. Swept across
//                    the map toward the light, each cell from its
//                    neighbour on the light's side (S = max(height,
//                    S beyond − drop over a cell)), so a sun angle
//                    costs one pass; worked out again (on the
//                    thread) when the light has turned RETURN_DEG.
//                    Long shadows into the valleys at dawn and dusk.
//   cloud shadows    drifting patches (shader only, with the wind).
//
// Coarse by design: mountains and ridges shade valleys; a house
// doesn't cast one (player builds and edits aren't in the map).
// Each is a setting (Settings > Graphics: Sun shading, Terrain
// shadows, Cloud shadows); off is the plain look.
//
// Owned by the world's FarTerrain (same lifetime as the generator).
// gFrameLight.terrain points at the one in view; every world shader
// gets it through bindUniforms (no map: everything lit).
namespace terrainlight {

class Map {
public:
    static constexpr int   CELL_MIN   = 8;       // voxels per cell at least
    static constexpr int   MAX_CELLS  = 1024;    // per side at most
    static constexpr float RETURN_DEG = 0.35f;   // the light turned this much: shadows again
    static constexpr float MIN_RISE   = 0.03f;   // (the light's slope never below this: low sun, very long shadows)

    Map(const TerrainGenerator& gen, int chunksX, int chunksZ) : gen(gen) {
        const int wx = chunksX * VOXEL_DIM, wz = chunksZ * VOXEL_DIM;
        cell = std::max(CELL_MIN, (std::max(wx, wz) + MAX_CELLS - 1) / MAX_CELLS);
        nx = std::max(1, (wx + cell - 1) / cell);
        nz = std::max(1, (wz + cell - 1) / cell);
        worker = std::thread([this] { run(); });
    }
    ~Map() {
        { std::lock_guard<std::mutex> l(m); stopping = true; }
        wake.notify_all();
        if (worker.joinable()) worker.join();
        if (texH) glDeleteTextures(1, &texH);
        if (texS) glDeleteTextures(1, &texS);
    }
    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;

    // Each frame, before drawing: uploads what the thread finished,
    // asks for shadows again when the light has turned
    void update(glm::vec3 lightDir) {
        std::lock_guard<std::mutex> l(m);
        if (heightsDone && !texH) {
            texH = upload(h);
            // ground lit straight from above until the first shadows come
            texS = upload(h);
            sShown = h;
        }
        if (!heightsDone) return;
        if (sDone) {
            sDone = false;
            glBindTexture(GL_TEXTURE_2D, texS);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, nx, nz, GL_RED, GL_FLOAT, sNew.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            sShown.swap(sNew);
            haveShadows = true;
        }
        if (!asked && glm::dot(glm::normalize(lightDir), forDir) < std::cos(glm::radians(RETURN_DEG))) {
            forDir = glm::normalize(lightDir);
            asked = true;
            wake.notify_all();
        }
    }

    bool ready() const { return texH != 0; }
    GLuint heightTexture() const { return texH; }
    GLuint shadowTexture() const { return texS; }
    int cellSize() const { return cell; }
    glm::vec2 worldSize() const { return glm::vec2((float)(nx * cell), (float)(nz * cell)); }

    // 0..1: how much of the light reaches p past the hills (characters;
    // the main thread's copy of the shadows last uploaded)
    float sunAt(glm::vec3 p, float bias = 6.0f) const {
        if (!haveShadows) return 1.0f;
        float s = sample(sShown, p.x, p.z);
        float t = std::clamp((p.y - (s - bias - 6.0f)) / 6.0f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    float groundAt(float x, float z) const { return ready() ? sample(h, x, z) : 0.0f; }

private:
    const TerrainGenerator& gen;
    int cell = CELL_MIN, nx = 1, nz = 1;
    std::vector<float> h;          // ground (or water) height per cell
    std::vector<float> sNew;       // the thread's latest shadows (waiting to upload)
    std::vector<float> sShown;     // the ones on the GPU
    GLuint texH = 0, texS = 0;
    bool heightsDone = false, sDone = false, asked = false, stopping = false, haveShadows = false;
    glm::vec3 forDir{ 0.0f, -1.0f, 0.0f };   // (an impossible light: the first update asks)
    std::mutex m;
    std::condition_variable wake;
    std::thread worker;

    GLuint upload(const std::vector<float>& v) const {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, nx, nz, 0, GL_RED, GL_FLOAT, v.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return t;
    }
    // bilinear, cell centres at (i + 0.5) × cell, clamped at the edges
    float sample(const std::vector<float>& v, float x, float z) const {
        if (v.empty()) return 0.0f;
        float fx = std::clamp(x / (float)cell - 0.5f, 0.0f, (float)(nx - 1));
        float fz = std::clamp(z / (float)cell - 0.5f, 0.0f, (float)(nz - 1));
        int i0 = (int)fx, j0 = (int)fz, i1 = std::min(i0 + 1, nx - 1), j1 = std::min(j0 + 1, nz - 1);
        float tx = fx - (float)i0, tz = fz - (float)j0;
        auto at = [&](int i, int j) { return v[(size_t)j * nx + i]; };
        return (at(i0, j0) * (1 - tx) + at(i1, j0) * tx) * (1 - tz) + (at(i0, j1) * (1 - tx) + at(i1, j1) * tx) * tz;
    }

    void run() {
        // the heights (cell centres)
        std::vector<float> hh((size_t)nx * nz);
        for (int j = 0; j < nz; j++) {
            for (int i = 0; i < nx; i++) {
                TerrainColumn c = gen.column(i * cell + cell / 2, j * cell + cell / 2);
                hh[(size_t)j * nx + i] = (float)std::max(c.ground, c.waterLevel);
            }
            if (j % 64 == 0) { std::lock_guard<std::mutex> l(m); if (stopping) return; }
        }
        {
            std::lock_guard<std::mutex> l(m);
            h = std::move(hh);
            heightsDone = true;
        }
        for (;;) {
            glm::vec3 dir;
            {
                std::unique_lock<std::mutex> l(m);
                wake.wait(l, [&] { return stopping || asked; });
                if (stopping) return;
                dir = forDir;
            }
            std::vector<float> s = shadows(dir);
            std::lock_guard<std::mutex> l(m);
            sNew = std::move(s);
            sDone = true;
            asked = false;
        }
    }

    // One sweep toward the light: along the axis the light's mostly
    // across (major), each row from its neighbour one cell nearer the
    // light, between two cells of it on the other axis
    std::vector<float> shadows(glm::vec3 L) const {
        std::vector<float> s = h;
        glm::vec2 d(L.x, L.z);
        float across = glm::length(d);
        if (across < 1e-3f) return s;                         // straight overhead: no shadows
        d /= across;
        const float rise = std::max(MIN_RISE, L.y) / across;  // height gained per voxel toward the light
        const bool alongX = std::abs(d.x) >= std::abs(d.y);
        const int nMaj = alongX ? nx : nz, nMin = alongX ? nz : nx;
        const float dMaj = alongX ? d.x : d.y, dMin = alongX ? d.y : d.x;
        const int step = dMaj > 0 ? 1 : -1;                   // toward the light
        const float slide = dMin / std::abs(dMaj);            // minor cells per major cell
        const float drop = (float)cell * std::sqrt(1.0f + slide * slide) * rise;
        auto idx = [&](int maj, int mn) { return alongX ? (size_t)mn * nx + maj : (size_t)maj * nx + mn; };
        const int first = step > 0 ? nMaj - 2 : 1;            // (the row on the light's edge has nothing past it)
        for (int a = first; a >= 0 && a < nMaj; a -= step) {
            const int b = a + step;
            for (int k = 0; k < nMin; k++) {
                float f = (float)k + slide;
                if (f < 0.0f || f > (float)(nMin - 1)) continue;  // (past the map's side: open sea)
                int k0 = (int)f, k1 = std::min(k0 + 1, nMin - 1);
                float t = f - (float)k0;
                float beyond = s[idx(b, k0)] * (1.0f - t) + s[idx(b, k1)] * t;
                float& me = s[idx(a, k)];
                me = std::max(me, beyond - drop);
            }
        }
        return s;
    }
};

// The uniforms every world shader takes (shaders/terrainlight.glsl):
// the two maps on texture units 6 and 7. map null: all of it off.
inline void bindUniforms(GLuint prog, const Map* map, int sunShading, int terrainShadows, int cloudShadows,
                         float time, glm::vec2 wind) {
    const bool on = map && map->ready();
    if (on) {
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, map->heightTexture());
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, map->shadowTexture());
        glActiveTexture(GL_TEXTURE0);
    }
    glUniform1i(glGetUniformLocation(prog, "uTerrainH"), 6);
    glUniform1i(glGetUniformLocation(prog, "uTerrainS"), 7);
    glm::vec2 size = on ? map->worldSize() : glm::vec2(1.0f);
    glUniform4f(glGetUniformLocation(prog, "uTerrainMap"), 1.0f / size.x, 1.0f / size.y,
                on ? (float)map->cellSize() : 8.0f, on ? 1.0f : 0.0f);
    glUniform1i(glGetUniformLocation(prog, "uSunShading"), on ? sunShading : 0);
    glUniform1i(glGetUniformLocation(prog, "uTerrainShadows"), on ? terrainShadows : 0);
    glUniform1i(glGetUniformLocation(prog, "uCloudShadows"), cloudShadows);
    glUniform3f(glGetUniformLocation(prog, "uClouds"), wind.x, wind.y, time);
}

// The same for the frame (gFrameLight.terrain, the settings)
inline void bindFrame(GLuint prog) {
    double now = glfwGetTime();
    wind::Now w = wind::at(now);
    bindUniforms(prog, gFrameLight.terrain, gSettings.sunShading, gSettings.terrainShadows, gSettings.cloudShadows,
                 (float)std::fmod(now, 20000.0), glm::vec2(w.dir.x, w.dir.y));
}

} // namespace terrainlight
