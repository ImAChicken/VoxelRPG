#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "Shader.h"
#include <cmath>
#include <string>
#include <vector>

// ══════════════════════════════════════════════════════════
// DebugDraw — lines and labels for the debug overlay
// ══════════════════════════════════════════════════════════
// Anything can queue lines (world space) and labels (a world
// point with text) during a frame; flush() draws the lines once,
// on top of the world but dimmed where something is in front, and
// the HUD draws the labels (they are projected there).
//
// Nothing here knows about the game: game/DebugView.h decides
// what to show.
class DebugDraw {
public:
    struct Label { glm::vec3 at; std::string text; glm::vec3 color; };

    bool init() {
        if (!shader.load("shaders/debugline.vert", "shaders/debugline.frag")) return false;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(float) * 3));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        ready = true;
        return true;
    }
    void cleanup() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vbo = vao = 0;
        ready = false;
    }

    // ── Queueing ──────────────────────────────────────────
    void line(glm::vec3 a, glm::vec3 b, glm::vec3 c, float alpha = 1.0f) {
        verts.push_back({ a, glm::vec4(c, alpha) });
        verts.push_back({ b, glm::vec4(c, alpha) });
    }
    void box(glm::vec3 lo, glm::vec3 hi, glm::vec3 c, float alpha = 1.0f) {
        glm::vec3 p[8];
        for (int i = 0; i < 8; i++) p[i] = glm::vec3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
        const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
        for (auto& x : e) line(p[x[0]], p[x[1]], c, alpha);
    }
    // A horizontal circle (or arc from a0 to a1 radians, around +x toward +z)
    void circle(glm::vec3 centre, float r, glm::vec3 c, float alpha = 1.0f, int seg = 24) {
        arc(centre, r, 0.0f, glm::two_pi<float>(), c, alpha, seg);
    }
    void arc(glm::vec3 centre, float r, float a0, float a1, glm::vec3 c, float alpha = 1.0f, int seg = 16) {
        glm::vec3 prev = centre + glm::vec3(std::cos(a0), 0, std::sin(a0)) * r;
        for (int i = 1; i <= seg; i++) {
            float a = a0 + (a1 - a0) * (float)i / seg;
            glm::vec3 p = centre + glm::vec3(std::cos(a), 0, std::sin(a)) * r;
            line(prev, p, c, alpha);
            prev = p;
        }
    }
    // An upright capsule from the feet: two rings and four sides
    void capsule(glm::vec3 feet, float r, float h, glm::vec3 c, float alpha = 1.0f) {
        circle(feet, r, c, alpha, 16);
        circle(feet + glm::vec3(0, h, 0), r, c, alpha, 16);
        for (int i = 0; i < 4; i++) {
            float a = i * glm::half_pi<float>();
            glm::vec3 o(std::cos(a) * r, 0, std::sin(a) * r);
            line(feet + o, feet + o + glm::vec3(0, h, 0), c, alpha);
        }
    }
    void cross(glm::vec3 p, float s, glm::vec3 c, float alpha = 1.0f) {
        line(p - glm::vec3(s, 0, 0), p + glm::vec3(s, 0, 0), c, alpha);
        line(p - glm::vec3(0, s, 0), p + glm::vec3(0, s, 0), c, alpha);
        line(p - glm::vec3(0, 0, s), p + glm::vec3(0, 0, s), c, alpha);
    }
    void arrow(glm::vec3 a, glm::vec3 b, glm::vec3 c, float alpha = 1.0f) {
        line(a, b, c, alpha);
        glm::vec3 d = b - a;
        float l = glm::length(d);
        if (l < 1e-3f) return;
        d /= l;
        glm::vec3 side = glm::normalize(glm::cross(d, std::fabs(d.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
        float s = std::min(1.5f, l * 0.3f);
        line(b, b - d * s + side * s * 0.5f, c, alpha);
        line(b, b - d * s - side * s * 0.5f, c, alpha);
    }
    void label(glm::vec3 at, std::string text, glm::vec3 c = glm::vec3(1)) {
        labels.push_back({ at, std::move(text), c });
    }

    // ── Drawing ───────────────────────────────────────────
    // Lines: once dimmed through everything, once at full
    // strength where visible
    void flush(const glm::mat4& viewProj) {
        if (!ready || verts.empty()) { verts.clear(); return; }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STREAM_DRAW);
        shader.use();
        shader.setMat4("uViewProj", viewProj);
        glBindVertexArray(vao);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        shader.setFloat("uAlpha", 0.3f);
        glDrawArrays(GL_LINES, 0, (GLsizei)verts.size());
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        shader.setFloat("uAlpha", 1.0f);
        glDrawArrays(GL_LINES, 0, (GLsizei)verts.size());
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        if (!depthOn) glDisable(GL_DEPTH_TEST);
        glBindVertexArray(0);
        verts.clear();
    }
    std::vector<Label> takeLabels() { std::vector<Label> l; l.swap(labels); return l; }
    void clear() { verts.clear(); labels.clear(); }

private:
    struct Vertex { glm::vec3 p; glm::vec4 c; };
    Shader shader;
    GLuint vao = 0, vbo = 0;
    bool ready = false;
    std::vector<Vertex> verts;
    std::vector<Label> labels;
};
