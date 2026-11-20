#pragma once
// Slash trails: a short ribbon behind a blade while its hit window is
// open (a little before and after too), so a swing reads at a glance.
// Each tick the game adds where the blade's base and tip are
// (Trails::sample, from the simulation's poses); the ribbon is the
// last TRAIL_TIME s of them, fading with age, added (glows) over the
// world. Settings > Graphics > Particles off turns them off too.

#include "game/Settings.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <unordered_map>
#include <vector>

class Trails {
public:
    static constexpr float TRAIL_TIME = 0.14f;   // s a sample lasts

    struct Sample { glm::vec3 base, tip; float age = 0.0f; };

    // where a body's blade is now; 'strength' 0..1 (a heavy brighter);
    // a gap of more than a tick since its last sample starts a new ribbon
    void sample(uint64_t id, glm::vec3 base, glm::vec3 tip, glm::vec3 color, float strength) {
        if (gSettings.particles <= 0) return;
        Ribbon& r = ribbons[id];
        if (!r.wasLive) r.s.clear();                      // a new swing: a new ribbon
        r.live = true; r.color = color; r.strength = strength;
        r.s.push_back({ base, tip, 0.0f });
        while (r.s.size() > 48) r.s.pop_front();
    }
    void update(float dt) {
        for (auto it = ribbons.begin(); it != ribbons.end();) {
            Ribbon& r = it->second;
            for (Sample& s : r.s) s.age += dt;
            while (!r.s.empty() && r.s.front().age > TRAIL_TIME) r.s.pop_front();
            r.wasLive = r.live; r.live = false;              // sampled since the last update?
            if (r.s.empty()) it = ribbons.erase(it); else ++it;
        }
    }
    void clear() { ribbons.clear(); }
    size_t count() const { return ribbons.size(); }

    // ── Drawing ─────────────────────────────────────────
    bool init() {
        const char* vs = R"(#version 460 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";
        const char* fs = R"(#version 460 core
in vec4 vColor;
out vec4 outColor;
void main() { outColor = vec4(vColor.rgb * vColor.a, 1.0); }
)";
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) return false;
        program = glCreateProgram();
        glAttachShader(program, v); glAttachShader(program, f);
        glLinkProgram(program);
        glDeleteShader(v); glDeleteShader(f);
        GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) return false;
        uViewProj = glGetUniformLocation(program, "uViewProj");
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)sizeof(glm::vec3));
        glBindVertexArray(0);
        return true;
    }
    void draw(const glm::mat4& viewProj) {
        if (!program || ribbons.empty()) return;
        verts.clear();
        for (auto& [id, r] : ribbons) {
            if (r.s.size() < 2) continue;
            for (size_t i = 0; i + 1 < r.s.size(); i++) {
                const Sample& a = r.s[i];
                const Sample& b = r.s[i + 1];
                float fa = fade(a.age) * r.strength, fb = fade(b.age) * r.strength;
                // brighter toward the tip, nothing at the base
                glm::vec4 ca(r.color, fa), cb(r.color, fb), za(r.color, 0.0f), zb(r.color, 0.0f);
                verts.push_back({ a.base, za }); verts.push_back({ a.tip, ca }); verts.push_back({ b.tip, cb });
                verts.push_back({ a.base, za }); verts.push_back({ b.tip, cb }); verts.push_back({ b.base, zb });
            }
        }
        if (verts.empty()) return;
        glUseProgram(program);
        glUniformMatrix4fv(uViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vert), verts.data(), GL_STREAM_DRAW);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glBindVertexArray(0);
    }
    ~Trails() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }

private:
    struct Ribbon { std::deque<Sample> s; glm::vec3 color{1.0f}; float strength = 1.0f; bool live = false, wasLive = false; };
    struct Vert { glm::vec3 p; glm::vec4 c; };
    std::unordered_map<uint64_t, Ribbon> ribbons;
    std::vector<Vert> verts;
    GLuint program = 0, vao = 0, vbo = 0;
    GLint uViewProj = -1;

    static float fade(float age) { float u = std::clamp(age / TRAIL_TIME, 0.0f, 1.0f); return (1.0f - u) * (1.0f - u) * 0.55f; }
    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); std::cerr << "trails shader: " << log << "\n"; glDeleteShader(s); return 0; }
        return s;
    }
};
