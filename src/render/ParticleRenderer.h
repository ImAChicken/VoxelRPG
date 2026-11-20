#pragma once
// Draws render/Particles.h: every particle is one instance of a quad,
// built in the vertex shader (a square facing the camera, or a streak
// along its velocity for a spark). Two draws a frame: the laid-over
// ones (puffs, dust, splats), then the glowing ones added on top.
// Depth-tested against the world, never written (so they don't cut
// holes in each other), and fogged like the world.

#include "render/Particles.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

class ParticleRenderer {
public:
    bool init() {
        const char* vs = R"(#version 460 core
layout(location=0) in vec4 iPos;      // xyz, size
layout(location=1) in vec4 iVel;      // xyz velocity, stretch (s) — 0: a square
layout(location=2) in vec4 iColor;
uniform mat4 uViewProj;
uniform vec3 uCam;
out vec4 vColor;
out vec2 vUV;
out float vDist;
void main() {
    vec2 c = vec2((gl_VertexID & 1) != 0 ? 1.0 : -1.0, (gl_VertexID & 2) != 0 ? 1.0 : -1.0);
    vec3 toCam = normalize(uCam - iPos.xyz);
    vec3 p;
    float len = length(iVel.xyz) * iVel.w;
    if (iVel.w > 0.0 && len > 1e-3) {
        // a streak: long along its motion, thin across, turned to face you
        vec3 along = normalize(iVel.xyz);
        vec3 across = normalize(cross(along, toCam));
        p = iPos.xyz + along * (c.y * max(len, iPos.w) * 0.5) + across * (c.x * iPos.w * 0.5);
    } else {
        vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), toCam));
        if (abs(dot(toCam, vec3(0.0, 1.0, 0.0))) > 0.99) right = vec3(1.0, 0.0, 0.0);
        vec3 up = cross(toCam, right);
        p = iPos.xyz + (right * c.x + up * c.y) * (iPos.w * 0.5);
    }
    vColor = iColor;
    vUV = c;
    vDist = length(uCam - p);
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";
        const char* fs = R"(#version 460 core
in vec4 vColor;
in vec2 vUV;
in float vDist;
uniform int uAdditive;
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 outColor;
void main() {
    // chunky: a square with its corners softened a little, matching the voxels
    float edge = max(abs(vUV.x), abs(vUV.y));
    float a = vColor.a * (1.0 - smoothstep(0.8, 1.0, edge));
    float fog = exp(-vDist * uFogDensity);       // how much shows through (as the props fog)
    if (uAdditive != 0) outColor = vec4(vColor.rgb * a * fog, 1.0);
    else outColor = vec4(mix(uFogColor, vColor.rgb, fog), a);
    if (a <= 0.003) discard;
}
)";
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) return false;
        program = glCreateProgram();
        glAttachShader(program, v); glAttachShader(program, f);
        glLinkProgram(program);
        glDeleteShader(v); glDeleteShader(f);
        GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(program, 512, nullptr, log); std::cerr << "particles: " << log << "\n"; return false; }
        uViewProj = glGetUniformLocation(program, "uViewProj");
        uCam = glGetUniformLocation(program, "uCam");
        uAdditive = glGetUniformLocation(program, "uAdditive");
        uFogColor = glGetUniformLocation(program, "uFogColor");
        uFogDensity = glGetUniformLocation(program, "uFogDensity");

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        for (int i = 0; i < 3; i++) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (void*)(sizeof(glm::vec4) * i));
            glVertexAttribDivisor(i, 1);
        }
        glBindVertexArray(0);
        return true;
    }

    void draw(const fx::Particles& ps, const glm::mat4& viewProj, glm::vec3 cam, glm::vec3 fogColor, float fogDensity) {
        if (!program || ps.count() == 0) return;
        solid.clear(); glow.clear();
        for (const fx::Particle& p : ps.all()) {
            glm::vec4 c = p.color();
            if (!p.additive) c = glm::vec4(glm::vec3(c) * (0.25f + 0.75f * p.light), c.a);
            else c.a *= 0.4f + 0.6f * p.light;
            Inst in{ glm::vec4(p.p, p.size()), glm::vec4(p.v, p.shape == fx::Shape::Spark ? p.stretch : 0.0f), c };
            (p.additive ? glow : solid).push_back(in);
        }
        glUseProgram(program);
        glUniformMatrix4fv(uViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3fv(uCam, 1, glm::value_ptr(cam));
        glUniform3fv(uFogColor, 1, glm::value_ptr(fogColor));
        glUniform1f(uFogDensity, fogDensity);
        glBindVertexArray(vao);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        auto batch = [&](const std::vector<Inst>& v, bool add) {
            if (v.empty()) return;
            glBlendFunc(add ? GL_ONE : GL_SRC_ALPHA, add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            glUniform1i(uAdditive, add ? 1 : 0);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Inst), v.data(), GL_STREAM_DRAW);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
        };
        batch(solid, false);
        batch(glow, true);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glBindVertexArray(0);
    }

    ~ParticleRenderer() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }

private:
    struct Inst { glm::vec4 pos, vel, color; };
    std::vector<Inst> solid, glow;
    GLuint program = 0, vao = 0, vbo = 0;
    GLint uViewProj = -1, uCam = -1, uAdditive = -1, uFogColor = -1, uFogDensity = -1;

    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); std::cerr << "particles shader: " << log << "\n"; glDeleteShader(s); return 0; }
        return s;
    }
};
