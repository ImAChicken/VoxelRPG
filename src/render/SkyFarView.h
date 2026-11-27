#pragma once
#include "Shader.h"
#include "Sky.h"
#include "../chunk/Chunk.h"
#include "../chunk/TerrainGenerator.h"
#include "../world/SkyCells.h"
#include "../world/SurfaceNets.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// ══════════════════════════════════════════════════════════
// SkyFarView — the sky world seen from far away
// ══════════════════════════════════════════════════════════
// The height map far view (render/FarTerrain.h) can't draw islands
// that float: one ground per spot, no undersides. This keeps a coarse
// voxel copy of the world instead (ROADMAP "Mushroom Islands", step 2;
// Claude, 2026-11-25):
//
//   cells    one cell per 8 × 8 × 8 voxels, each solid or not, with a
//            colour (the top voxel's, so the grass shows from above)
//            and a glow flag (glowing blocks: drawn bright day and
//            night). Kept per chunk as runs up each column of cells.
//   source   places never visited: the generator, sampled at each
//            cell column's middle (cheap: no chunk is built). Chunks
//            that have been loaded: their real voxels, shrunk into
//            cells when their light is baked (so edits show: paint
//            one side of an island and it's that colour from afar),
//            and saved with the world (skyfar.dat).
//   regions  4 × 4 chunks a mesh. Near regions draw every cell; further
//            out cells merge 2 × 2 × 2 (16, 32, 64 voxels), a merged
//            cell solid if any of its cells is, so small islands stay
//            visible. Only the faces between solid and open cells.
//   near     where real chunks are drawn, the coarse copy steps aside
//            (a chunk mask, as the height map far view does).
//   smooth   (2026-11-27) the cells aren't drawn as cubes but skinned
//            with surface nets (world/SurfaceNets.h): a merged cell's
//            density is how much of it is solid, so a big coarse cell
//            half full comes out as a gentle slope, not a 64-voxel
//            step. Regions read a cell past their edges so their
//            seams meet. Settings > Sky far shape: Smooth / Blocky.
//   setting  Settings > Sky far view: Full / Tops only (only the faces
//            looking up: cheap) / Off.
// Glowing mushrooms' light reaching the ground round them, and sun
// shadows from this copy, come later (ROADMAP).
class SkyFarView {
public:
    static constexpr int CELL = skycells::CELL;             // voxels a cell
    static constexpr int CPC  = skycells::CPC;              // cells a chunk side (16)
    static constexpr int CY   = skycells::CY;               // cells up (128)
    static constexpr int REGION_CHUNKS = 4;
    static constexpr float NEAR_PLANE = 16.0f, FAR_PLANE = 120000.0f;

    using BlockLook = skycells::BlockLook;
    using LookTable = skycells::LookTable;
    using Run = skycells::Run;
    using ChunkCells = skycells::ChunkCells;

    SkyFarView(const TerrainGenerator& gen, int chunksX, int chunksZ, std::shared_ptr<const LookTable> looks)
        : gen(gen), chunksX(chunksX), chunksZ(chunksZ), looks(std::move(looks)) {
        cells.resize((size_t)chunksX * chunksZ);
        regionsX = (chunksX + REGION_CHUNKS - 1) / REGION_CHUNKS;
        regionsZ = (chunksZ + REGION_CHUNKS - 1) / REGION_CHUNKS;
        regions.resize((size_t)regionsX * regionsZ);
        worker = std::thread([this] { work(); });
    }
    ~SkyFarView() {
        { std::lock_guard<std::mutex> l(m); stopping = true; }
        wake.notify_all();
        if (worker.joinable()) worker.join();
        for (Region& r : regions) r.release();
        if (maskTex) glDeleteTextures(1, &maskTex);
    }
    SkyFarView(const SkyFarView&) = delete;
    SkyFarView& operator=(const SkyFarView&) = delete;

    bool initGL() {
        if (!shader.load("shaders/skyfar.vert", "shaders/skyfar.frag")) return false;
        mask.assign((size_t)chunksX * chunksZ, 0);
        glGenTextures(1, &maskTex);
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, chunksX, chunksZ, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    // 0 off, 1 tops only, 2 full (Settings)
    void setMode(int m) {
        m = std::clamp(m, 0, 2);
        if (m == mode) return;
        mode = m;
        for (Region& r : regions) r.dirty = true;
    }
    int getMode() const { return mode; }
    // surface nets (true) or the cells as cubes (Settings > Sky far shape)
    void setSmooth(bool on) {
        if (on == smooth) return;
        smooth = on;
        for (Region& r : regions) r.dirty = true;
    }
    bool getSmooth() const { return smooth; }
    void setMaxDistance(float v) { maxDistance = std::max(0.0f, v); }

    // Which chunks are drawn from voxels this frame (cz * chunksX + cx)
    void setNearMask(const std::vector<uint8_t>& mk) {
        if (mk == mask || !maskTex) return;
        mask = mk;
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chunksX, chunksZ, GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // A chunk's real voxels, after it's been built or changed (main
    // thread: takes its sections, shrinks them on the worker)
    void chunkChanged(int cx, int cz, Chunk& c) {
        if (cx < 0 || cz < 0 || cx >= chunksX || cz >= chunksZ) return;
        Job j; j.cx = cx; j.cz = cz;
        for (int s = 0; s < SECTIONS; s++) j.sections[(size_t)s] = c.snapshot(s);
        { std::lock_guard<std::mutex> l(m); jobs.push_front(std::move(j)); }
        wake.notify_one();
    }

    // Once a frame: hand finished meshes to the GPU, ask for the ones
    // the camera's distance now wants
    void update(glm::vec3 cam) {
        std::vector<Built> done;
        { std::lock_guard<std::mutex> l(m); done.swap(built); }
        for (Built& b : done) {
            Region& r = regions[(size_t)b.index];
            r.building = false;
            r.upload(b);
            r.level = b.level;
            if (b.stamp != r.stamp) r.dirty = true;          // changed while it was being made
        }
        if (mode == 0) return;
        int asked = 0;
        for (int rz = 0; rz < regionsZ; rz++)
        for (int rx = 0; rx < regionsX; rx++) {
            Region& r = regions[(size_t)(rz * regionsX + rx)];
            if (r.building) continue;
            float d = distanceTo(rx, rz, cam);
            if (maxDistance > 0.0f && d > maxDistance + 512.0f) { if (r.count) r.release(); r.level = -1; continue; }
            int want = d < 1400.0f ? 0 : d < 2800.0f ? 1 : d < 5200.0f ? 2 : 3;
            if (want == r.level && !r.dirty) continue;
            if (!filledRegion(rx, rz)) continue;              // (its cells aren't all known yet)
            if (asked >= 2) continue;
            r.building = true; r.dirty = false;
            Mesh job; job.index = rz * regionsX + rx; job.level = want; job.stamp = r.stamp; job.mode = mode; job.smooth = smooth;
            { std::lock_guard<std::mutex> l(m); meshJobs.push_back(job); }
            wake.notify_one();
            asked++;
        }
    }

    void draw(const glm::mat4& view, float fovDeg, float aspect, glm::vec3 cam) {
        if (!shader.id || mode == 0) return;
        glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, NEAR_PLANE, FAR_PLANE);
        glm::mat4 vp = proj * view;
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
        shader.use();
        GLuint p = shader.id;
        glUniformMatrix4fv(glGetUniformLocation(p, "uViewProj"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniform3f(glGetUniformLocation(p, "uCamPos"), cam.x, cam.y, cam.z);
        glUniform2i(glGetUniformLocation(p, "uWorldChunks"), chunksX, chunksZ);
        const FrameLight& fl = gFrameLight;
        auto v3 = [&](const char* n, glm::vec3 v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); };
        auto f1 = [&](const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); };
        v3("uSkyLight", fl.sky.skyLight); v3("uAmbient", fl.sky.ambient);
        v3("uFogHorizon", fl.sky.horizon); v3("uFogSunGlow", fl.sky.sunGlow); v3("uSunDir", fl.sky.sunDir);
        f1("uFogDensity", fl.fogDensity); f1("uNight", fl.sky.night);
        v3("uLightDir", fl.sky.lightDir());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, maskTex);
        glUniform1i(glGetUniformLocation(p, "uNearMask"), 1);
        drawn = 0;
        for (int rz = 0; rz < regionsZ; rz++)
        for (int rx = 0; rx < regionsX; rx++) {
            const Region& r = regions[(size_t)(rz * regionsX + rx)];
            if (!r.count) continue;
            if (maxDistance > 0.0f && distanceTo(rx, rz, cam) > maxDistance) continue;
            glBindVertexArray(r.vao);
            glDrawElements(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, nullptr);
            drawn++;
        }
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

    // How many chunks' cells are known (from the generator or voxels), of all
    int known() const { return knownCount.load(); }
    int total() const { return chunksX * chunksZ; }
    int regionsDrawn() const { return drawn; }

    // A chunk's cells (tests; null: not made yet)
    std::shared_ptr<const ChunkCells> cellsOf(int cx, int cz) const {
        std::lock_guard<std::mutex> l(m);
        return cells[(size_t)cz * chunksX + cx];
    }
    // Wait until every chunk's cells are known (tests)
    void waitKnown() const { while (knownCount.load() < chunksX * chunksZ) std::this_thread::yield(); }

    // ── Saving: the chunks made from real voxels ──────────
    void save(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        if (!f) return;
        f.write("SKYF", 4);
        uint32_t v = 1, n = 0;
        std::vector<std::pair<int, std::shared_ptr<const ChunkCells>>> list;
        {
            std::lock_guard<std::mutex> l(m);
            for (size_t i = 0; i < cells.size(); i++)
                if (cells[i] && cells[i]->fromVoxels) list.push_back({ (int)i, cells[i] });
        }
        n = (uint32_t)list.size();
        f.write((const char*)&v, 4); f.write((const char*)&n, 4);
        for (auto& [i, c] : list) {
            uint32_t idx = (uint32_t)i, nr = (uint32_t)c->runs.size();
            f.write((const char*)&idx, 4); f.write((const char*)&nr, 4);
            f.write((const char*)c->start.data(), (std::streamsize)(c->start.size() * 4));
            f.write((const char*)c->runs.data(), (std::streamsize)(nr * sizeof(Run)));
        }
    }
    void load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return;
        char magic[4]; uint32_t v = 0, n = 0;
        f.read(magic, 4); f.read((char*)&v, 4); f.read((char*)&n, 4);
        if (!f || std::memcmp(magic, "SKYF", 4) != 0 || v != 1) return;
        std::lock_guard<std::mutex> l(m);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t idx = 0, nr = 0;
            f.read((char*)&idx, 4); f.read((char*)&nr, 4);
            if (!f || idx >= cells.size() || nr > 1000000) return;
            auto c = std::make_shared<ChunkCells>();
            c->start.resize(CPC * CPC + 1);
            c->runs.resize(nr);
            f.read((char*)c->start.data(), (std::streamsize)(c->start.size() * 4));
            f.read((char*)c->runs.data(), (std::streamsize)(nr * sizeof(Run)));
            if (!f) return;
            c->fromVoxels = true;
            if (!cells[idx]) knownCount++;
            cells[idx] = c;
        }
    }

    static std::shared_ptr<ChunkCells> fromGenerator(const TerrainGenerator& g, const LookTable& L, int cx, int cz) { return skycells::fromGenerator(g, L, cx, cz); }
    static std::shared_ptr<ChunkCells> fromVoxels(const std::array<std::shared_ptr<const RleData>, SECTIONS>& secs, const LookTable& L) { return skycells::fromVoxels(secs, L); }

private:
    const TerrainGenerator& gen;
    int chunksX, chunksZ, regionsX = 0, regionsZ = 0;
    std::shared_ptr<const LookTable> looks;
    std::vector<std::shared_ptr<const ChunkCells>> cells;
    std::atomic<int> knownCount{ 0 };
    int mode = 2;
    bool smooth = true;
    float maxDistance = 0.0f;
    int drawn = 0;
    Shader shader;
    GLuint maskTex = 0;
    std::vector<uint8_t> mask;

    struct Vertex { float x, y, z; uint8_t r, g, b, a; int8_t nx, ny, nz, pad; int16_t cx, cz; };
    struct Built { int index = 0, level = 0; uint32_t stamp = 0; std::vector<Vertex> verts; std::vector<uint32_t> idx; };
    struct Region {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
        int level = -1;
        bool building = false, dirty = false;
        uint32_t stamp = 0;                  // bumped when its cells change
        void upload(const Built& b) {
            if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo); }
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(b.verts.size() * sizeof(Vertex)), b.verts.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(b.idx.size() * 4), b.idx.data(), GL_STATIC_DRAW);
            const GLsizei st = sizeof(Vertex);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Vertex, x));
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(Vertex, r));
            glVertexAttribPointer(2, 3, GL_BYTE, GL_TRUE, st, (void*)offsetof(Vertex, nx));
            glVertexAttribIPointer(3, 2, GL_SHORT, st, (void*)offsetof(Vertex, cx));
            for (int i = 0; i < 4; i++) glEnableVertexAttribArray(i);
            glBindVertexArray(0);
            count = (GLsizei)b.idx.size();
        }
        void release() {
            if (vao) glDeleteVertexArrays(1, &vao);
            if (vbo) glDeleteBuffers(1, &vbo);
            if (ebo) glDeleteBuffers(1, &ebo);
            vao = vbo = ebo = 0; count = 0;
        }
    };
    std::vector<Region> regions;

    struct Job { int cx = 0, cz = 0; std::array<std::shared_ptr<const RleData>, SECTIONS> sections; };
    struct Mesh { int index = 0, level = 0, mode = 2; uint32_t stamp = 0; bool smooth = true; };
    mutable std::mutex m;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::deque<Mesh> meshJobs;
    std::vector<Built> built;
    bool stopping = false;
    std::thread worker;
    int genNext = 0;                         // next chunk the generator fills in (nearest first would be nicer)

    float distanceTo(int rx, int rz, glm::vec3 cam) const {
        float S = (float)(REGION_CHUNKS * VOXEL_DIM);
        float x0 = rx * S, z0 = rz * S;
        float dx = std::max({ x0 - cam.x, 0.0f, cam.x - (x0 + S) });
        float dz = std::max({ z0 - cam.z, 0.0f, cam.z - (z0 + S) });
        return std::sqrt(dx * dx + dz * dz);
    }
    bool filledRegion(int rx, int rz) const {
        std::lock_guard<std::mutex> l(m);
        for (int z = rz * REGION_CHUNKS; z < std::min(chunksZ, (rz + 1) * REGION_CHUNKS); z++)
            for (int x = rx * REGION_CHUNKS; x < std::min(chunksX, (rx + 1) * REGION_CHUNKS); x++)
                if (!cells[(size_t)z * chunksX + x]) return false;
        return true;
    }
    void touched(int cx, int cz) {
        Region& r = regions[(size_t)((cz / REGION_CHUNKS) * regionsX + cx / REGION_CHUNKS)];
        r.stamp++;
        r.dirty = true;
    }

    void work() {
        for (;;) {
            Job job; Mesh mesh; int kind = 0;
            {
                std::unique_lock<std::mutex> l(m);
                wake.wait(l, [&] { return stopping || !jobs.empty() || !meshJobs.empty() || genNext < chunksX * chunksZ; });
                if (stopping) return;
                if (!jobs.empty()) { job = std::move(jobs.front()); jobs.pop_front(); kind = 1; }
                else if (!meshJobs.empty()) { mesh = meshJobs.front(); meshJobs.pop_front(); kind = 2; }
                else kind = 3;
            }
            if (kind == 1) {
                auto c = fromVoxels(job.sections, *looks);
                std::lock_guard<std::mutex> l(m);
                size_t i = (size_t)job.cz * chunksX + job.cx;
                if (!cells[i]) knownCount++;
                cells[i] = c;
                touched(job.cx, job.cz);
            } else if (kind == 2) {
                Built b = buildMesh(mesh);
                std::lock_guard<std::mutex> l(m);
                built.push_back(std::move(b));
            } else {
                // the generator: a batch of chunks not known yet
                for (int k = 0; k < 8; k++) {
                    int i;
                    {
                        std::lock_guard<std::mutex> l(m);
                        while (genNext < chunksX * chunksZ && cells[(size_t)genNext]) genNext++;
                        if (genNext >= chunksX * chunksZ) break;
                        i = genNext++;
                    }
                    auto c = fromGenerator(gen, *looks, i % chunksX, i / chunksX);
                    std::lock_guard<std::mutex> l(m);
                    if (!cells[(size_t)i]) { cells[(size_t)i] = c; knownCount++; touched(i % chunksX, i / chunksX); }
                }
            }
        }
    }

    // One region's mesh at a level (cells 8 << level voxels)
    Built buildMesh(const Mesh& job) const { return job.smooth ? buildSmooth(job) : buildBlocky(job); }

    // ── Smooth: the cells as densities, skinned with surface nets ──
    // The grid is the region's coarse cells and one more all round
    // (from the neighbouring chunks; nothing below the bottom or over
    // the top), a coarse cell's density 0 if empty, else 0.5 and up
    // to 1 as more of its fine cells are solid. The iso level sits
    // under 0.5, so a coarse cell with any solid in it still shows (a
    // small island stays a small lump, as the cubes kept it).
    static constexpr float ISO = 0.4f;
    Built buildSmooth(const Mesh& job) const {
        Built out; out.index = job.index; out.level = job.level; out.stamp = job.stamp;
        const int rx = job.index % regionsX, rz = job.index / regionsX;
        const int F = 1 << job.level;                         // fine cells a coarse cell
        const int N = REGION_CHUNKS * CPC / F, NY = CY / F;   // coarse cells a side, up
        const int P = 1;                                      // the border read from the neighbours
        surfacenets::Grid g;
        g.resize(N + 2 * P, NY + 2 * P, N + 2 * P);
        std::vector<uint16_t> count(g.s.size(), 0);
        // the chunks it reads: its own 4 × 4 and the ring round them
        const int c0x = rx * REGION_CHUNKS - 1, c0z = rz * REGION_CHUNKS - 1, CW = REGION_CHUNKS + 2;
        std::vector<std::shared_ptr<const ChunkCells>> mine((size_t)CW * CW);
        {
            std::lock_guard<std::mutex> l(m);
            for (int z = 0; z < CW; z++) for (int x = 0; x < CW; x++) {
                int cx = c0x + x, cz = c0z + z;
                if (cx >= 0 && cz >= 0 && cx < chunksX && cz < chunksZ) mine[(size_t)(z * CW + x)] = cells[(size_t)cz * chunksX + cx];
            }
        }
        auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        for (int z = 0; z < CW; z++) for (int x = 0; x < CW; x++) {
            const ChunkCells* cc = mine[(size_t)(z * CW + x)].get();
            if (!cc) continue;
            for (int fz = 0; fz < CPC; fz++) for (int fx = 0; fx < CPC; fx++) {
                // this fine column, counted from the region's corner
                int gx = floorDiv((x - 1) * CPC + fx, F) + P, gz = floorDiv((z - 1) * CPC + fz, F) + P;
                if (gx < 0 || gz < 0 || gx >= g.nx || gz >= g.nz) continue;
                int col = fz * CPC + fx;
                for (uint32_t k = cc->start[(size_t)col]; k < cc->start[(size_t)col + 1]; k++) {
                    const Run& r = cc->runs[k];
                    for (int y = r.y0; y < r.y1; y++) {
                        int gy = y / F + P;
                        size_t i = ((size_t)gy * g.nz + gz) * g.nx + gx;
                        surfacenets::Sample& sm = g.s[i];
                        if (!count[i] || y >= sm.topY) { sm.r = r.r; sm.g = r.g; sm.b = r.b; sm.topY = (int16_t)y; }
                        count[i]++;
                        if (r.glow) sm.glow = 1;
                    }
                }
            }
        }
        const float full = (float)(F * F * F);
        for (size_t i = 0; i < g.s.size(); i++)
            if (count[i]) g.s[i].d = 0.5f + 0.5f * std::min(1.0f, count[i] / full);
        surfacenets::Mesh sn;
        surfacenets::mesh(g, ISO, glm::ivec3(P, 0, P), glm::ivec3(N - 1 + P, g.ny - 1, N - 1 + P), sn, job.mode == 1);
        // to the world: sample (x, y, z) is the middle of coarse cell (x - P, y - P, z - P)
        const float S = (float)(CELL * F);
        const glm::vec3 o((float)(rx * REGION_CHUNKS * VOXEL_DIM) + (0.5f - P) * S, (0.5f - P) * S,
                          (float)(rz * REGION_CHUNKS * VOXEL_DIM) + (0.5f - P) * S);
        out.verts.reserve(sn.quads.size() * 4);
        out.idx.reserve(sn.quads.size() * 6);
        for (const surfacenets::Quad& q : sn.quads) {
            // its chunk (the near mask): the cell that made it
            int16_t cx = (int16_t)(rx * REGION_CHUNKS + ((q.owner.x - P) * F) / CPC);
            int16_t cz = (int16_t)(rz * REGION_CHUNKS + ((q.owner.z - P) * F) / CPC);
            uint32_t base = (uint32_t)out.verts.size();
            for (int k = 0; k < 4; k++) {
                const surfacenets::Vert& v = sn.verts[q.v[k]];
                glm::vec3 w = o + v.p * S;
                out.verts.push_back({ w.x, w.y, w.z, v.r, v.g, v.b, (uint8_t)(v.glow ? 0 : 255),
                                      (int8_t)std::lround(v.n.x * 127.0f), (int8_t)std::lround(v.n.y * 127.0f), (int8_t)std::lround(v.n.z * 127.0f),
                                      0, cx, cz });
            }
            surfacenets::triangles(sn, q, base, out.idx);
        }
        return out;
    }

    // ── Blocky: each cell's open faces (Settings > Sky far shape) ──
    Built buildBlocky(const Mesh& job) const {
        Built out; out.index = job.index; out.level = job.level; out.stamp = job.stamp;
        const int rx = job.index % regionsX, rz = job.index / regionsX;
        const int L = job.level, F = 1 << L;                 // F fine cells a coarse cell
        const int N = REGION_CHUNKS * CPC / F;               // coarse cells a side
        const int NY = CY / F;
        struct C { uint8_t on = 0, r = 0, g = 0, b = 0, glow = 0; uint8_t topY = 0; };
        std::vector<C> grid((size_t)N * N * NY);
        auto at = [&](int x, int y, int z) -> C& { return grid[((size_t)y * N + z) * N + x]; };
        std::vector<std::shared_ptr<const ChunkCells>> mine;
        {
            std::lock_guard<std::mutex> l(m);
            for (int z = 0; z < REGION_CHUNKS; z++) for (int x = 0; x < REGION_CHUNKS; x++) {
                int cx = rx * REGION_CHUNKS + x, cz = rz * REGION_CHUNKS + z;
                mine.push_back(cx < chunksX && cz < chunksZ ? cells[(size_t)cz * chunksX + cx] : nullptr);
            }
        }
        for (int z = 0; z < REGION_CHUNKS; z++) for (int x = 0; x < REGION_CHUNKS; x++) {
            const ChunkCells* cc = mine[(size_t)(z * REGION_CHUNKS + x)].get();
            if (!cc) continue;
            for (int cz = 0; cz < CPC; cz++) for (int cxx = 0; cxx < CPC; cxx++) {
                int col = cz * CPC + cxx;
                for (uint32_t k = cc->start[(size_t)col]; k < cc->start[(size_t)col + 1]; k++) {
                    const Run& r = cc->runs[k];
                    int gx = (x * CPC + cxx) / F, gz = (z * CPC + cz) / F;
                    for (int y = r.y0; y < r.y1; y++) {
                        C& c = at(gx, y / F, gz);
                        // the colour of its highest fine cell (tops read from above)
                        if (!c.on || y >= c.topY) { c.r = r.r; c.g = r.g; c.b = r.b; c.topY = (uint8_t)y; }
                        c.on = 1;
                        if (r.glow) c.glow = 1;
                    }
                }
            }
        }
        const float S = (float)(CELL * F);
        const float ox = (float)(rx * REGION_CHUNKS * VOXEL_DIM), oz = (float)(rz * REGION_CHUNKS * VOXEL_DIM);
        static const int DIRS[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        for (int y = 0; y < NY; y++)
        for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            const C& c = at(x, y, z);
            if (!c.on) continue;
            for (int d = 0; d < 6; d++) {
                if (job.mode == 1 && d != 2) continue;                    // tops only
                int nx = x + DIRS[d][0], ny = y + DIRS[d][1], nz = z + DIRS[d][2];
                bool open = nx < 0 || nz < 0 || nx >= N || nz >= N || ny < 0 || ny >= NY || !at(nx, ny, nz).on;
                if (!open) continue;
                face(out, x, y, z, d, S, ox, oz, c, rx, rz, F);
            }
        }
        return out;
    }
    template <class Cell>
    static void face(Built& out, int x, int y, int z, int d, float S, float ox, float oz, const Cell& c, int rx, int rz, int F) {
        // the cell's corners
        float x0 = ox + x * S, x1 = x0 + S, y0 = y * S, y1 = y0 + S, z0 = oz + z * S, z1 = z0 + S;
        glm::vec3 q[4];
        switch (d) {
        case 0: q[0] = {x1,y0,z0}; q[1] = {x1,y1,z0}; q[2] = {x1,y1,z1}; q[3] = {x1,y0,z1}; break;
        case 1: q[0] = {x0,y0,z1}; q[1] = {x0,y1,z1}; q[2] = {x0,y1,z0}; q[3] = {x0,y0,z0}; break;
        case 2: q[0] = {x0,y1,z0}; q[1] = {x0,y1,z1}; q[2] = {x1,y1,z1}; q[3] = {x1,y1,z0}; break;
        case 3: q[0] = {x0,y0,z0}; q[1] = {x1,y0,z0}; q[2] = {x1,y0,z1}; q[3] = {x0,y0,z1}; break;
        case 4: q[0] = {x1,y0,z1}; q[1] = {x1,y1,z1}; q[2] = {x0,y1,z1}; q[3] = {x0,y0,z1}; break;
        default:q[0] = {x0,y0,z0}; q[1] = {x0,y1,z0}; q[2] = {x1,y1,z0}; q[3] = {x1,y0,z0}; break;
        }
        static const int8_t NRM[6][3] = { {127,0,0}, {-127,0,0}, {0,127,0}, {0,-127,0}, {0,0,127}, {0,0,-127} };
        // which chunk it's in (the near mask)
        int16_t cx = (int16_t)(rx * REGION_CHUNKS + (x * F) / CPC), cz = (int16_t)(rz * REGION_CHUNKS + (z * F) / CPC);
        uint32_t base = (uint32_t)out.verts.size();
        for (int k = 0; k < 4; k++)
            out.verts.push_back({ q[k].x, q[k].y, q[k].z, c.r, c.g, c.b, (uint8_t)(c.glow ? 0 : 255),
                                  NRM[d][0], NRM[d][1], NRM[d][2], 0, cx, cz });
        out.idx.insert(out.idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
};
