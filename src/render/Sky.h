#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

// ══════════════════════════════════════════════════════════
// Sky — time of day, sun and moon, the sky dome, fog colour
// ══════════════════════════════════════════════════════════
// One clock drives everything that changes with the hour:
//
//   sun / moon   direction and the colour of their light. The
//                baked light layer stores how much sky a spot
//                sees; shaders multiply that by skyLight, so
//                noon, dusk and a blue moonlit night are only a
//                change of colour, never a rebake
//   sky          a gradient dome, horizon to zenith, with the
//                sun, its glow, the moon and stars
//   fog          the horizon colour in the direction you look
//                (warmer toward a low sun), so haze matches the
//                sky behind it. Kept thin: distance stays clear
//
// time: 0 midnight · 0.25 sunrise · 0.5 noon · 0.75 sunset
struct DayClock {
    float time = 0.35f;              // starts mid-morning
    float dayLengthSeconds = 1200.0f; // 20 minutes
    bool  frozen = false;
    void update(float dt) {
        if (frozen || dayLengthSeconds <= 0) return;
        time = std::fmod(time + dt / dayLengthSeconds, 1.0f);
    }
};

struct SkyState {
    glm::vec3 sunDir{0, 1, 0};        // toward the sun
    glm::vec3 moonDir{0, -1, 0};
    glm::vec3 skyLight{1};            // what full sky light looks like now (sun or moon)
    glm::vec3 ambient{0.02f};         // the floor in enclosed places
    glm::vec3 zenith{0.3f, 0.5f, 0.9f}, horizon{0.6f, 0.75f, 0.95f};
    glm::vec3 sunGlow{1.0f, 0.8f, 0.6f};
    float     night = 0.0f;           // 0 day … 1 full night
    float     sunUp = 1.0f;           // 0..1 how far the sun is above the horizon

    // The light coming down from the sky (the side the light layer
    // calls 'sky') is from the sun by day, the moon by night
    glm::vec3 lightDir() const { return night > 0.5f ? moonDir : sunDir; }

    static glm::vec3 mix3(glm::vec3 a, glm::vec3 b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); }
    static float smooth(float a, float b, float x) {
        float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }

    static SkyState at(float time) {
        SkyState s;
        const float TAU = 6.2831853f;
        float a = (time - 0.25f) * TAU;               // 0 at sunrise
        // the sun crosses the sky from east (+X) to west, tilted a little to the south
        s.sunDir  = glm::normalize(glm::vec3(std::cos(a), std::sin(a), -0.35f));
        s.moonDir = -s.sunDir;
        float h = s.sunDir.y;                          // sun height, -1..1
        s.sunUp = smooth(-0.05f, 0.25f, h);
        float dusk = smooth(-0.15f, 0.0f, h) * (1.0f - smooth(0.0f, 0.3f, h));   // low sun
        s.night = 1.0f - smooth(-0.18f, 0.02f, h);

        const glm::vec3 dayZenith(0.28f, 0.50f, 0.90f), dayHorizon(0.62f, 0.76f, 0.95f);
        const glm::vec3 duskZenith(0.24f, 0.28f, 0.52f), duskHorizon(0.98f, 0.58f, 0.34f);
        const glm::vec3 nightZenith(0.012f, 0.02f, 0.055f), nightHorizon(0.04f, 0.06f, 0.13f);
        glm::vec3 z = mix3(nightZenith, dayZenith, 1.0f - s.night);
        glm::vec3 hz = mix3(nightHorizon, dayHorizon, 1.0f - s.night);
        s.zenith  = mix3(z, duskZenith, dusk * 0.6f);
        s.horizon = mix3(hz, duskHorizon, dusk);
        s.sunGlow = mix3(glm::vec3(1.0f, 0.92f, 0.8f), glm::vec3(1.0f, 0.5f, 0.22f), dusk);

        // light from the sky: white-warm noon, orange dusk, blue moon
        glm::vec3 noon(1.0f, 0.97f, 0.9f), duskLight(1.0f, 0.62f, 0.38f), moon(0.20f, 0.27f, 0.50f);
        glm::vec3 sunLight = mix3(noon, duskLight, dusk) * (0.35f + 0.65f * s.sunUp);
        s.skyLight = mix3(sunLight, moon, s.night);
        s.ambient  = mix3(glm::vec3(0.035f, 0.035f, 0.04f), glm::vec3(0.012f, 0.014f, 0.025f), s.night);
        return s;
    }
};

// The sky dome: one full-screen triangle drawn behind everything
class SkyRenderer {
public:
    bool init() {
        const char* vs = R"(#version 460 core
const vec2 P[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
out vec2 vNdc;
void main() { vNdc = P[gl_VertexID]; gl_Position = vec4(P[gl_VertexID], 0.9999, 1.0); })";
        const char* fs = R"(#version 460 core
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform vec3 uZenith, uHorizon, uSunGlow, uSunDir, uMoonDir;
uniform float uNight;
out vec4 outColor;
float hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
void main() {
    vec4 w = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(w.xyz / w.w - (uInvViewProj * vec4(vNdc, -1.0, 1.0)).xyz / (uInvViewProj * vec4(vNdc, -1.0, 1.0)).w);
    float up = clamp(d.y, -1.0, 1.0);
    vec3 col = mix(uHorizon, uZenith, pow(max(up, 0.0), 0.55));
    if (up < 0.0) col = mix(uHorizon, uHorizon * 0.7, min(1.0, -up * 3.0));     // below the horizon (ocean edge)
    float s = max(dot(d, uSunDir), 0.0);
    col += uSunGlow * (pow(s, 8.0) * 0.35 + pow(s, 64.0) * 0.4) * (1.0 - uNight * 0.85);
    if (s > 0.9994) col = mix(col, vec3(1.0, 0.97, 0.88), 1.0 - uNight);          // sun disc
    float m = dot(d, uMoonDir);
    if (m > 0.9990) col = mix(col, vec3(0.82, 0.86, 0.95), uNight);               // moon disc
    col += vec3(0.25, 0.3, 0.45) * pow(max(m, 0.0), 32.0) * 0.15 * uNight;          // moon glow
    if (uNight > 0.01 && up > 0.02) {                                              // stars
        vec3 g = floor(d * 380.0);
        float h = hash(g);
        if (h > 0.9965) col += vec3(0.75 + 0.25 * hash(g + 3.1)) * uNight * (h - 0.9965) / 0.0035 * smoothstep(0.02, 0.2, up);
    }
    outColor = vec4(col, 1.0);
})";
        prog = link(vs, fs);
        glGenVertexArrays(1, &vao);
        return prog != 0;
    }
    void draw(const glm::mat4& viewProj, const SkyState& s) {
        if (!prog) return;
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glUseProgram(prog);
        glm::mat4 inv = glm::inverse(viewProj);
        glUniformMatrix4fv(glGetUniformLocation(prog, "uInvViewProj"), 1, GL_FALSE, glm::value_ptr(inv));
        auto v3 = [&](const char* n, glm::vec3 v) { glUniform3f(glGetUniformLocation(prog, n), v.x, v.y, v.z); };
        v3("uZenith", s.zenith); v3("uHorizon", s.horizon); v3("uSunGlow", s.sunGlow);
        v3("uSunDir", s.sunDir); v3("uMoonDir", s.moonDir);
        glUniform1f(glGetUniformLocation(prog, "uNight"), s.night);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }
    void cleanup() {
        if (prog) glDeleteProgram(prog);
        if (vao) glDeleteVertexArrays(1, &vao);
        prog = vao = 0;
    }
private:
    GLuint prog = 0, vao = 0;
    static GLuint compile(GLenum t, const char* src) {
        GLuint s = glCreateShader(t);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); std::cerr << "Sky shader: " << log << "\n"; return 0; }
        return s;
    }
    static GLuint link(const char* vs, const char* fs) {
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) return 0;
        GLuint p = glCreateProgram();
        glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
        glDeleteShader(v); glDeleteShader(f);
        return p;
    }
};

// Lighting values shared by every shader this frame
struct FrameLight {
    SkyState sky;
    float fogDensity = 0.0f;       // the thin, clear haze
    float seamStart  = 0.0f;       // voxels: where the haze at the voxel / far-terrain border begins
    float seamEnd    = 0.0f;       //         … and reaches its full strength
    float seamHaze   = 0.0f;       // how much haze from there on (0..1)
};
inline FrameLight gFrameLight;
inline DayClock   gClock;
