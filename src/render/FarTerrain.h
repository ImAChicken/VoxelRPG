#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "Shader.h"
#include "../chunk/TerrainGenerator.h"

// ══════════════════════════════════════════════════════════
// FarTerrain — the distant view
// ══════════════════════════════════════════════════════════
// Real voxel chunks are only drawn MESH_DISTANCE chunks out.
// Everything past that is drawn from here: a coloured height
// map built straight from the world generator, so it costs no
// voxel memory and shows the whole island from anywhere.
//
//   regions   the world is cut into squares of 8 × 8 chunks
//             (1024 voxels). Each is one mesh.
//   detail    a region near the camera samples the ground
//             every 4 voxels; further out every 8, 16, 32,
//             then 64. Regions rebuild on worker threads as
//             the camera moves, keeping the old mesh on screen
//             until the new one is ready.
//   skirts    each region hangs a short wall down from its
//             edges, so the small gaps where a fine region
//             meets a coarse one never show the sky.
//   colour    the top block's average atlas colour, tinted
//             toward leaves where there is forest (and raised
//             by the canopy), shaded by slope so steep ground
//             reads like the voxels' darker side faces.
//   ocean     a world type with a sea gets a flat ocean from
//             the edge of the world out to the horizon.
//
// Drawn BEFORE the voxel chunks, with its own near and far
// planes, then the depth buffer is cleared: the voxels always
// draw over it. Where a chunk is drawn from voxels, the far
// terrain sinks well below the ground (the "near mask", see
// shaders/far.vert), so it never pokes through the real
// ground, and its edge hangs down as a curtain that hides any
// gap where the two meet.
//
// Not yet: player edits do not show in the far view (it is
// rebuilt from the seed).
class FarTerrain {
public:
    static constexpr int   REGION_CHUNKS = 8;
    static constexpr int   REGION        = REGION_CHUNKS * VOXEL_DIM;   // voxels
    static constexpr float NEAR_PLANE    = 16.0f;
    static constexpr float FAR_PLANE     = 120000.0f;
    static constexpr float OCEAN_EXTENT  = 100000.0f;

    // Per block ID: its colour seen from far away (0..255)
    using ColorTable = std::vector<glm::u8vec3>;

    FarTerrain(const TerrainGenerator& gen, int chunksX, int chunksZ,
               std::shared_ptr<const ColorTable> colors)
        : gen(gen), chunksX(chunksX), chunksZ(chunksZ), colors(std::move(colors)) {
        regionsX = (chunksX + REGION_CHUNKS - 1) / REGION_CHUNKS;
        regionsZ = (chunksZ + REGION_CHUNKS - 1) / REGION_CHUNKS;
        regions.resize((size_t)regionsX * regionsZ);
        for (int i = 0; i < 2; i++)
            workers.emplace_back([this] { workerLoop(); });
    }

    ~FarTerrain() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (auto& t : workers) t.join();
        for (auto& r : regions) r.gpu.release();
        ocean.release();
        if (maskTex) glDeleteTextures(1, &maskTex);
    }

    FarTerrain(const FarTerrain&) = delete;
    FarTerrain& operator=(const FarTerrain&) = delete;

    // Main thread, with the GL context current
    bool initGL() {
        if (!shader.load("shaders/far.vert", "shaders/far.frag")) return false;
        locViewProj   = glGetUniformLocation(shader.id, "uViewProj");
        locCamPos     = glGetUniformLocation(shader.id, "uCamPos");
        locMask       = glGetUniformLocation(shader.id, "uNearMask");
        locWorld      = glGetUniformLocation(shader.id, "uWorldChunks");
        locFogColor   = glGetUniformLocation(shader.id, "uFogColor");
        locFogDensity = glGetUniformLocation(shader.id, "uFogDensity");

        mask.assign((size_t)chunksX * chunksZ, 0);
        glGenTextures(1, &maskTex);
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, chunksX, chunksZ, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        buildOcean();
        return true;
    }

    // Which chunks are drawn from voxels this frame
    // (index cz * chunksX + cx, 1 = voxels)
    void setNearMask(const std::vector<uint8_t>& m) {
        if (m == mask || !maskTex) return;
        mask = m;
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chunksX, chunksZ,
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Once per frame: take finished regions, ask for new ones
    void update(glm::vec3 cam) {
        std::vector<Result> done;
        {
            std::lock_guard<std::mutex> lock(mutex);
            done.swap(results);
        }
        for (Result& r : done) {
            Region& reg = regions[(size_t)r.index];
            reg.building = false;
            inFlight--;
            if (r.step != reg.wanted) continue;      // camera moved on
            reg.gpu.upload(r.verts, r.indices);
            reg.step = r.step;
            reg.minY = r.minY;
            reg.maxY = r.maxY;
        }

        // Pick the detail each region should have, nearest first
        struct Cand { float d; int index, step; };
        std::vector<Cand> cands;
        for (int rz = 0; rz < regionsZ; rz++)
        for (int rx = 0; rx < regionsX; rx++) {
            int i = rz * regionsX + rx;
            Region& reg = regions[(size_t)i];
            float d = distanceTo(rx, rz, cam);
            int want = stepFor(d);
            // Hysteresis: stay at the current detail while
            // the camera is near a boundary
            if (reg.step > 0 && reg.step >= stepFor(d - 150.0f) && reg.step <= stepFor(d + 150.0f))
                want = reg.step;
            reg.wanted = want;
            if (want != reg.step && !reg.building) cands.push_back({ d, i, want });
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });

        std::vector<Job> jobsOut;
        for (const Cand& c : cands) {
            if (inFlight >= MAX_IN_FLIGHT) break;
            regions[(size_t)c.index].building = true;
            inFlight++;
            jobsOut.push_back({ c.index, c.step });
        }
        if (!jobsOut.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto& j : jobsOut) jobs.push_back(j);
            wake.notify_all();
        }
    }

    // Draw everything. The caller clears the depth buffer
    // afterwards and draws the voxel chunks on top.
    void draw(const glm::mat4& view, float fovDeg, float aspect, glm::vec3 cam,
              glm::vec3 fogColor, float fogDensity) {
        if (!shader.id) return;
        glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, NEAR_PLANE, FAR_PLANE);
        glm::mat4 vp = proj * view;
        extractFrustum(vp);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        // Two-sided: skirts face every way, and a height map
        // hides its own underside with the depth test anyway
        glDisable(GL_CULL_FACE);

        shader.use();
        glUniformMatrix4fv(locViewProj, 1, GL_FALSE, glm::value_ptr(vp));
        glUniform3f(locCamPos, cam.x, cam.y, cam.z);
        glUniform2i(locWorld, chunksX, chunksZ);
        glUniform3f(locFogColor, fogColor.r, fogColor.g, fogColor.b);
        glUniform1f(locFogDensity, fogDensity);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glUniform1i(locMask, 1);

        drawn = 0;
        for (int rz = 0; rz < regionsZ; rz++)
        for (int rx = 0; rx < regionsX; rx++) {
            const Region& reg = regions[(size_t)(rz * regionsX + rx)];
            if (!reg.gpu.count) continue;
            glm::vec3 lo((float)(rx * REGION), reg.minY, (float)(rz * REGION));
            glm::vec3 hi(lo.x + REGION, reg.maxY, lo.z + REGION);
            if (!boxInFrustum(lo, hi)) continue;
            reg.gpu.draw();
            drawn++;
        }
        if (ocean.count) ocean.draw();

        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

    int regionsDrawn() const { return drawn; }
    int regionCount()  const { return (int)regions.size(); }
    int pending()      const { return inFlight; }

private:
    static constexpr int MAX_IN_FLIGHT = 4;

    struct Vertex {
        float   x, y, z;
        uint8_t r, g, b, a;
    };

    // One mesh on the GPU
    struct GpuMesh {
        GLuint  vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;

        void upload(const std::vector<Vertex>& v, const std::vector<uint32_t>& idx) {
            if (v.empty() || idx.empty()) { release(); return; }
            if (!vao) {
                glGenVertexArrays(1, &vao);
                glGenBuffers(1, &vbo);
                glGenBuffers(1, &ebo);
            }
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(Vertex)), v.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));
            glEnableVertexAttribArray(1);
            glBindVertexArray(0);
            count = (GLsizei)idx.size();
        }
        void draw() const {
            glBindVertexArray(vao);
            glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
        }
        void release() {
            if (vao) glDeleteVertexArrays(1, &vao);
            if (vbo) glDeleteBuffers(1, &vbo);
            if (ebo) glDeleteBuffers(1, &ebo);
            vao = vbo = ebo = 0;
            count = 0;
        }
    };

    struct Region {
        GpuMesh gpu;
        int   step = 0;         // detail on screen (0 = nothing yet)
        int   wanted = 0;
        bool  building = false;
        float minY = 0, maxY = 0;
    };

    struct Job { int index, step; };
    struct Result {
        int index = 0, step = 0;
        std::vector<Vertex>   verts;
        std::vector<uint32_t> indices;
        float minY = 0, maxY = 0;
    };

    const TerrainGenerator& gen;
    int chunksX, chunksZ, regionsX = 0, regionsZ = 0;
    std::shared_ptr<const ColorTable> colors;
    std::vector<Region> regions;
    int inFlight = 0, drawn = 0;

    Shader  shader;
    GLint   locViewProj = -1, locCamPos = -1, locMask = -1, locWorld = -1,
            locFogColor = -1, locFogDensity = -1;
    GLuint  maskTex = 0;
    std::vector<uint8_t> mask;
    GpuMesh ocean;
    glm::vec4 frustum[6];

    std::vector<std::thread> workers;
    std::mutex               mutex;
    std::condition_variable  wake;
    std::deque<Job>          jobs;
    std::vector<Result>      results;
    bool                     stopping = false;

    // Ground spacing by distance (voxels)
    static int stepFor(float d) {
        if (d < 1200.0f)  return 4;
        if (d < 2600.0f)  return 8;
        if (d < 5500.0f)  return 16;
        if (d < 11000.0f) return 32;
        return 64;
    }

    // From the camera to the nearest point of a region (3D,
    // so flying high lowers the detail below)
    float distanceTo(int rx, int rz, glm::vec3 cam) const {
        float x0 = (float)(rx * REGION), z0 = (float)(rz * REGION);
        float dx = std::max({ x0 - cam.x, 0.0f, cam.x - (x0 + REGION) });
        float dz = std::max({ z0 - cam.z, 0.0f, cam.z - (z0 + REGION) });
        float dy = std::max(0.0f, cam.y - 400.0f) * 0.5f;
        return std::sqrt(dx * dx + dz * dz + dy * dy);
    }

    glm::u8vec3 colorOf(uint16_t id) const {
        if (colors && id < colors->size()) return (*colors)[id];
        return { 128, 128, 128 };
    }

    // ── Worker ────────────────────────────────────────────
    void workerLoop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || !jobs.empty(); });
                if (stopping) return;
                job = jobs.front();
                jobs.pop_front();
            }
            Result r;
            r.index = job.index;
            r.step  = job.step;
            buildRegion(job.index % regionsX, job.index / regionsX, job.step, r);
            {
                std::lock_guard<std::mutex> lock(mutex);
                results.push_back(std::move(r));
            }
        }
    }

    // One region's mesh. Samples a grid one point wider on
    // every side than the mesh, so slopes on the edges shade
    // the same as in the middle.
    void buildRegion(int rx, int rz, int step, Result& out) const {
        const int N  = REGION / step;          // quads per side
        const int G  = N + 3;                  // samples per side, with the ring
        const int x0 = rx * REGION, z0 = rz * REGION;

        std::vector<float>       h((size_t)G * G);
        std::vector<glm::u8vec3> col((size_t)G * G);
        const glm::u8vec3 leaf  = colorOf(BLOCK_LEAVES);
        const glm::u8vec3 water = colorOf(BLOCK_WATER);
        const glm::u8vec3 lava  = colorOf(BLOCK_LAVA);

        for (int j = 0; j < G; j++)
        for (int i = 0; i < G; i++) {
            int wx = x0 + (i - 1) * step, wz = z0 + (j - 1) * step;
            TerrainColumn c = gen.column(wx, wz);
            float y;
            glm::vec3 rgb;
            if (c.waterLevel > c.ground) {
                y = (float)c.waterLevel;
                glm::u8vec3 w = c.fluid == BLOCK_LAVA ? lava : water;
                rgb = glm::vec3(w);
            } else {
                y = (float)c.ground;
                rgb = glm::vec3(colorOf(c.top));
                float cover = gen.forestCover(wx, wz);
                if (cover > 0.0f) {
                    // canopy: darker leaf colour, raised
                    rgb = glm::mix(rgb, glm::vec3(leaf) * 0.85f, std::min(1.0f, cover * 1.1f));
                    y  += cover * 40.0f;
                }
            }
            h[(size_t)j * G + i]   = y;
            col[(size_t)j * G + i] = glm::u8vec3(glm::clamp(rgb, glm::vec3(0), glm::vec3(255)));
        }

        // Soften the colours a little (3 × 3 average): fine
        // patterns like mixed sand and dirt would otherwise
        // alias into stripes at this spacing
        {
            std::vector<glm::u8vec3> soft = col;
            for (int j = 1; j < G - 1; j++)
            for (int i = 1; i < G - 1; i++) {
                glm::vec3 sum(0.0f);
                for (int dj = -1; dj <= 1; dj++)
                    for (int di = -1; di <= 1; di++)
                        sum += glm::vec3(col[(size_t)(j + dj) * G + (i + di)]) * ((di == 0 && dj == 0) ? 2.0f : 1.0f);
                soft[(size_t)j * G + i] = glm::u8vec3(sum / 10.0f);
            }
            col.swap(soft);
        }

        // Vertices for the inner (N+1)² grid, shaded by slope
        auto H = [&](int i, int j) { return h[(size_t)j * G + i]; };
        out.verts.reserve((size_t)(N + 1) * (N + 1) + 4 * (N + 1));
        out.minY = 1e9f; out.maxY = -1e9f;
        for (int j = 0; j <= N; j++)
        for (int i = 0; i <= N; i++) {
            int gi = i + 1, gj = j + 1;
            glm::vec3 n = glm::normalize(glm::vec3(H(gi - 1, gj) - H(gi + 1, gj),
                                                   2.0f * step,
                                                   H(gi, gj - 1) - H(gi, gj + 1)));
            // Flat ground = top-face brightness, cliffs = side
            // faces, with a little light from the south-west
            float shade = 0.70f + 0.30f * n.y * n.y + 0.08f * (n.x * 0.6f + n.z * 0.4f);
            glm::vec3 c = glm::vec3(col[(size_t)gj * G + gi]) * std::clamp(shade, 0.4f, 1.05f);
            float y = H(gi, gj);
            out.verts.push_back({ (float)(x0 + i * step), y, (float)(z0 + j * step),
                                  (uint8_t)std::min(255.0f, c.r), (uint8_t)std::min(255.0f, c.g),
                                  (uint8_t)std::min(255.0f, c.b), 255 });
            out.minY = std::min(out.minY, y);
            out.maxY = std::max(out.maxY, y);
        }
        auto vid = [&](int i, int j) { return (uint32_t)(j * (N + 1) + i); };
        out.indices.reserve((size_t)N * N * 6 + 4 * N * 6);
        for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            uint32_t a = vid(i, j), b = vid(i + 1, j), c = vid(i + 1, j + 1), d = vid(i, j + 1);
            // counter-clockwise seen from above (+Y)
            out.indices.insert(out.indices.end(), { a, d, c, a, c, b });
        }

        // Skirts: a copy of each edge row pushed down, joined
        // to the edge, facing outward
        float drop = step * 3.0f + 8.0f;
        auto skirt = [&](int i0, int j0, int di, int dj, bool flip) {
            uint32_t base = (uint32_t)out.verts.size();
            for (int k = 0; k <= N; k++) {
                Vertex v = out.verts[vid(i0 + di * k, j0 + dj * k)];
                v.y -= drop;
                v.r = (uint8_t)(v.r * 0.8f); v.g = (uint8_t)(v.g * 0.8f); v.b = (uint8_t)(v.b * 0.8f);
                out.verts.push_back(v);
            }
            for (int k = 0; k < N; k++) {
                uint32_t t0 = vid(i0 + di * k, j0 + dj * k), t1 = vid(i0 + di * (k + 1), j0 + dj * (k + 1));
                uint32_t b0 = base + k, b1 = base + k + 1;
                if (flip) out.indices.insert(out.indices.end(), { t0, b1, b0, t0, t1, b1 });
                else      out.indices.insert(out.indices.end(), { t0, b0, b1, t0, b1, t1 });
            }
        };
        skirt(0, 0, 1, 0, false);   // south edge (z = z0), faces -Z
        skirt(0, N, 1, 0, true);    // north edge, faces +Z
        skirt(0, 0, 0, 1, true);    // west edge, faces -X
        skirt(N, 0, 0, 1, false);   // east edge, faces +X
        out.minY -= drop;
    }

    // Flat sea from the edge of the world to the horizon:
    // four quads framing the world square
    void buildOcean() {
        int sea = gen.seaLevel();
        if (sea < 0) return;
        float y  = (float)sea + 1.0f;
        float W  = (float)(chunksX * VOXEL_DIM), D = (float)(chunksZ * VOXEL_DIM);
        float E  = OCEAN_EXTENT;
        glm::u8vec3 c = colorOf(BLOCK_WATER);
        std::vector<Vertex> v;
        std::vector<uint32_t> idx;
        auto quad = [&](float xa, float za, float xb, float zb) {
            uint32_t b = (uint32_t)v.size();
            v.push_back({ xa, y, za, c.r, c.g, c.b, 255 });
            v.push_back({ xb, y, za, c.r, c.g, c.b, 255 });
            v.push_back({ xb, y, zb, c.r, c.g, c.b, 255 });
            v.push_back({ xa, y, zb, c.r, c.g, c.b, 255 });
            idx.insert(idx.end(), { b, b + 3, b + 2, b, b + 2, b + 1 });
        };
        quad(-E, -E, W + E, 0);        // south strip
        quad(-E, D, W + E, D + E);     // north strip
        quad(-E, 0, 0, D);             // west
        quad(W, 0, W + E, D);          // east
        ocean.upload(v, idx);
    }

    void extractFrustum(const glm::mat4& m) {
        glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
        glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
        glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
        glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
        frustum[0] = r3 + r0; frustum[1] = r3 - r0;
        frustum[2] = r3 + r1; frustum[3] = r3 - r1;
        frustum[4] = r3 + r2; frustum[5] = r3 - r2;
    }
    bool boxInFrustum(glm::vec3 lo, glm::vec3 hi) const {
        for (const glm::vec4& p : frustum) {
            glm::vec3 v(p.x >= 0 ? hi.x : lo.x, p.y >= 0 ? hi.y : lo.y, p.z >= 0 ? hi.z : lo.z);
            if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
        }
        return true;
    }
};
