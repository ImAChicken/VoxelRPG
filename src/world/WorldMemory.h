#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "LightBake.h"
#include "../lz4/lz4.h"

// ══════════════════════════════════════════════════════════
// WorldMemory — what the world remembers of chunks out of reach
// ══════════════════════════════════════════════════════════
// Each time a column's light is baked, the bake also hands back
// a low-res copy of its ground and the glowing blocks in it.
// Kept here (and saved with the world, lowres.dat) after the
// chunk itself is unloaded, so the distant view keeps showing
// what was really there: edited ground, buildings, and lights,
// which shine as points in the dark from across the island.
class WorldMemory {
public:
    struct Column {
        std::shared_ptr<const light::ColumnLight> summary;   // topY / topId only
        std::vector<light::Emitter> lights;                   // merged: one per 8 × 8 × 8 voxels
    };
    std::unordered_map<int, Column> columns;    // key: cz * WORLD_SIZE_X + cx
    uint32_t version = 0;

    void clear() { columns.clear(); version++; }

    // A freshly baked column (main thread). Returns true if its
    // ground copy differs from what was remembered.
    bool remember(int key, const std::shared_ptr<const light::ColumnLight>& sum,
                  const std::vector<light::Emitter>& emitters) {
        Column& c = columns[key];
        bool changed = !c.summary || c.summary->topY != sum->topY || c.summary->topId != sum->topId;
        c.summary = sum;
        // merge glowing cells into one light per 8 voxels
        c.lights.clear();
        std::unordered_map<int, size_t> bins;
        for (const auto& e : emitters) {
            int k = ((e.y >> 3) * 4096) + ((e.x >> 3) & 63) * 64 + ((e.z >> 3) & 63);
            auto it = bins.find(k);
            if (it == bins.end()) { bins[k] = c.lights.size(); c.lights.push_back(e); }
            else {
                auto& l = c.lights[it->second];
                l.r = std::max(l.r, e.r); l.g = std::max(l.g, e.g); l.b = std::max(l.b, e.b);
            }
        }
        version++;
        return changed;
    }

    // ── Save / load (world folder, lowres.dat) ────────────
    bool save(const std::string& path) const {
        std::vector<char> raw;
        auto put = [&](const void* p, size_t n) { raw.insert(raw.end(), (const char*)p, (const char*)p + n); };
        uint32_t n = (uint32_t)columns.size(); put(&n, 4);
        for (const auto& [key, c] : columns) {
            int k = key; put(&k, 4);
            uint8_t hasSum = c.summary ? 1 : 0; put(&hasSum, 1);
            if (hasSum) { put(c.summary->topY.data(), sizeof(c.summary->topY)); put(c.summary->topId.data(), sizeof(c.summary->topId)); }
            uint32_t nl = (uint32_t)c.lights.size(); put(&nl, 4);
            put(c.lights.data(), nl * sizeof(light::Emitter));
        }
        std::vector<char> packed((size_t)LZ4_compressBound((int)raw.size()));
        int got = LZ4_compress_default(raw.data(), packed.data(), (int)raw.size(), (int)packed.size());
        if (got <= 0) return false;
        std::ofstream f(path, std::ios::binary);
        uint32_t magic = 0x4D454D57u, ver = 1; uint64_t rawSize = raw.size();
        f.write((const char*)&magic, 4); f.write((const char*)&ver, 4); f.write((const char*)&rawSize, 8);
        f.write(packed.data(), got);
        return (bool)f;
    }
    bool load(const std::string& path) {
        columns.clear(); version++;
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        uint32_t magic = 0, ver = 0; uint64_t rawSize = 0;
        f.read((char*)&magic, 4); f.read((char*)&ver, 4); f.read((char*)&rawSize, 8);
        if (!f || magic != 0x4D454D57u || ver != 1 || rawSize > (1ull << 31)) return false;
        std::vector<char> packed((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<char> raw(rawSize);
        if (LZ4_decompress_safe(packed.data(), raw.data(), (int)packed.size(), (int)raw.size()) != (int)rawSize) return false;
        size_t off = 0;
        auto take = [&](void* p, size_t n) { if (off + n > raw.size()) return false; std::memcpy(p, raw.data() + off, n); off += n; return true; };
        uint32_t n = 0; if (!take(&n, 4)) return false;
        for (uint32_t i = 0; i < n; i++) {
            int k; uint8_t hasSum; if (!take(&k, 4) || !take(&hasSum, 1)) return false;
            Column c;
            if (hasSum) {
                auto s = std::make_shared<light::ColumnLight>();
                if (!take(s->topY.data(), sizeof(s->topY)) || !take(s->topId.data(), sizeof(s->topId))) return false;
                c.summary = s;
            }
            uint32_t nl; if (!take(&nl, 4)) return false;
            c.lights.resize(nl);
            if (!take(c.lights.data(), nl * sizeof(light::Emitter))) return false;
            columns[k] = std::move(c);
        }
        return true;
    }
};

// ── Distant lights ────────────────────────────────────────
// Remembered glowing blocks beyond the voxel chunks, drawn as
// soft glowing points over the distant terrain (depth-tested
// against it, so hills hide them). Faint by day, bright at night.
class DistantLights {
public:
    bool init() {
        const char* vs = R"(#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProj; uniform vec3 uCamPos; uniform float uNear;
out vec3 vColor;
void main() {
    float d = length(aPos - uCamPos);
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = d < uNear ? 0.0 : clamp(5200.0 / d, 2.0, 9.0);
})";
        const char* fs = R"(#version 460 core
in vec3 vColor; uniform float uStrength;
out vec4 outColor;
void main() {
    vec2 q = gl_PointCoord * 2.0 - 1.0;
    float r = dot(q, q);
    if (r > 1.0) discard;
    float a = (1.0 - r) * (1.0 - r) * uStrength;
    outColor = vec4(vColor * a, a);
})";
        auto comp = [](GLenum t, const char* src) {
            GLuint s = glCreateShader(t); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
            GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); std::cerr << "DistantLights: " << log << "\n"; }
            return s;
        };
        GLuint v = comp(GL_VERTEX_SHADER, vs), f = comp(GL_FRAGMENT_SHADER, fs);
        prog = glCreateProgram(); glAttachShader(prog, v); glAttachShader(prog, f); glLinkProgram(prog);
        glDeleteShader(v); glDeleteShader(f);
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12); glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        return true;
    }
    void rebuild(const WorldMemory& mem) {
        std::vector<float> v;
        for (const auto& [k, c] : mem.columns)
            for (const auto& l : c.lights) {
                v.insert(v.end(), { (float)l.x, (float)l.y, (float)l.z, l.r / 15.0f, l.g / 15.0f, l.b / 15.0f });
            }
        count = (GLsizei)(v.size() / 6);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_DYNAMIC_DRAW);
        builtFor = mem.version;
    }
    // nearLimit: points closer than this are real blocks, drawn by the chunks
    void draw(const WorldMemory& mem, const glm::mat4& viewProj, glm::vec3 cam, float nearLimit, float night) {
        if (!prog) return;
        if (builtFor != mem.version) rebuild(mem);
        if (!count) return;
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog);
        glUniformMatrix4fv(glGetUniformLocation(prog, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3f(glGetUniformLocation(prog, "uCamPos"), cam.x, cam.y, cam.z);
        glUniform1f(glGetUniformLocation(prog, "uNear"), nearLimit);
        glUniform1f(glGetUniformLocation(prog, "uStrength"), 0.2f + 0.8f * night);
        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, count);
        glBindVertexArray(0);
        glDisable(GL_BLEND); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
    }
    void cleanup() {
        if (prog) glDeleteProgram(prog);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        prog = vbo = vao = 0;
    }
private:
    GLuint prog = 0, vao = 0, vbo = 0;
    GLsizei count = 0;
    uint32_t builtFor = 0xFFFFFFFFu;
};
