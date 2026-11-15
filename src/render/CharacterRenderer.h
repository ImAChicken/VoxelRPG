#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>
#include "../character/CharacterAnimator.h"
#include "../character/FaceComposer.h"

// ══════════════════════════════════════════════════════════
// Character — one human's look, meshes, face and animation
// ══════════════════════════════════════════════════════════
// Owns the GPU copy of a CharacterModel and its painted face.
// setAppearance() rebuilds both when the look changes.
class Character {
public:
    Character() = default;
    ~Character() { release(); }
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;

    // Rebuild the body (main thread, GL context current).
    // Does nothing if neither the look nor the hair mesh mode
    // has changed.
    void setAppearance(const Appearance& a, HairMesh mode = HairMesh::Shards, bool force = false) {
        if (!force && built && a == look && mode == hairMode) return;
        look = a;
        hairMode = mode;
        model = CharacterModel::build(a, mode);
        upload();
        built = true;
    }

    const Pose& animate(const AnimInput& in, float dt) {
        blinkClock += dt;
        return animator.update(model, in, dt);
    }

    const Appearance&     appearance() const { return look; }
    const CharacterModel& getModel()   const { return model; }
    const Pose&           pose()       const { return animator.current(); }
    bool                  ready()      const { return built && vao != 0; }
    void                  resetAnimation()   { animator.reset(); }

    // Eyes close for a moment every few seconds
    bool blinking() const { return std::fmod(blinkClock, 3.6f) < 0.13f; }

    // ── Used by CharacterRenderer ─────────────────────────
    GLuint vao = 0, vbo = 0;
    GLuint faceTex[2] = {0, 0};      // open, blink
    std::array<GLint, BONE_COUNT>   first{};
    std::array<GLsizei, BONE_COUNT> count{};

private:
    Appearance        look;
    HairMesh          hairMode = HairMesh::Shards;
    CharacterModel    model;
    CharacterAnimator animator;
    bool              built = false;
    float             blinkClock = 0.0f;

    void upload() {
        std::vector<PartVertex> all;
        for (int b = 0; b < BONE_COUNT; b++) {
            first[b] = (GLint)all.size();
            count[b] = (GLsizei)model.parts[b].size();
            all.insert(all.end(), model.parts[b].begin(), model.parts[b].end());
        }
        if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(all.size() * sizeof(PartVertex)), all.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PartVertex), (void*)offsetof(PartVertex, x));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_BYTE, GL_TRUE, sizeof(PartVertex), (void*)offsetof(PartVertex, nx));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PartVertex), (void*)offsetof(PartVertex, r));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(PartVertex), (void*)offsetof(PartVertex, a));
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);

        // the painted face, open and blinking
        std::vector<uint8_t> px;
        for (int k = 0; k < 2; k++) {
            gFaces.compose(look, k == 1, px);
            if (!faceTex[k]) glGenTextures(1, &faceTex[k]);
            glBindTexture(GL_TEXTURE_2D, faceTex[k]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);   // soft low-res paint
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FaceComposer::R, FaceComposer::R, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    void release() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (faceTex[0]) glDeleteTextures(2, faceTex);
        vao = vbo = 0;
        faceTex[0] = faceTex[1] = 0;
    }
};

// How the toon edge lines are drawn (Settings)
struct EdgeStyle {
    int mode  = 1;     // 0 off, 1 lines, 2 lines + colour echo
    int color = 0;     // 0 solid black, 1 tinted by the surface
};

// ══════════════════════════════════════════════════════════
// CharacterRenderer — draws Characters
// ══════════════════════════════════════════════════════════
//   colour pass   faceted body with toon bands (a few hard
//                 light steps), then the face card
//   edge pass     the characters again into an off-screen
//                 buffer of normals and depth; a full-screen
//                 pass draws lines where depth or surface
//                 direction jumps, and around silhouettes.
//                 The lines carry the character's depth, so
//                 walls in front of a character hide them.
class CharacterRenderer {
public:
    // How the next characters drawn are lit (set per character
    // from the baked light where it stands; the creator's
    // preview uses the defaults)
    glm::vec3 lightDir  = glm::normalize(glm::vec3(0.33f, 0.74f, 0.58f));
    glm::vec3 skyTint   = glm::vec3(1.0f);
    glm::vec3 blockTint = glm::vec3(0.0f);
    bool init() {
        body = link(R"(#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uViewProj; uniform mat4 uModel; uniform vec3 uCamPos;
out vec3 vColor; out vec3 vNormal; out float vDist;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor; vDist = length(world.xyz - uCamPos);
    gl_Position = uViewProj * world;
})", R"(#version 460 core
in vec3 vColor; in vec3 vNormal; in float vDist;
uniform vec3 uFogColor; uniform float uFogDensity;
uniform vec3 uLightDir;     // toward the sun or moon
uniform vec3 uSkyTint;      // sky light reaching the character (baked light × sun/moon)
uniform vec3 uBlockTint;    // glowing blocks' light reaching it
out vec4 outColor;
void main() {
    // toon: a soft sky/ground fill plus the sun or moon in three
    // hard steps, gentle for a muted look; nearby lamps add their colour
    vec3 n = normalize(vNormal);
    float d = max(dot(n, uLightDir), 0.0);
    float ramp = d < 0.25 ? 0.49 : (d < 0.5 ? 0.76 : 1.0);
    vec3 fill = mix(vec3(0.80, 0.75, 0.93), vec3(1.0), n.y * 0.5 + 0.5);
    vec3 c = vColor * (uSkyTint * fill * (0.55 + 0.45 * ramp) + uBlockTint);
    float fog = 1.0 - exp(-vDist * uFogDensity);
    outColor = vec4(mix(c, uFogColor, fog), 1.0);
})");
        face = link(R"(#version 460 core
layout(location = 0) in vec2 aCorner;       // -0.5 .. 0.5
uniform mat4 uViewProj; uniform mat4 uModel; uniform vec3 uCenter; uniform float uSize;
out vec2 vUv;
void main() {
    vUv = vec2(aCorner.x + 0.5, 0.5 - aCorner.y);
    gl_Position = uViewProj * uModel * vec4(uCenter + vec3(aCorner * uSize, 0.0), 1.0);
})", R"(#version 460 core
in vec2 vUv; uniform sampler2D uFace; uniform vec3 uTint;
out vec4 outColor;
void main() {
    vec4 c = texture(uFace, vUv);
    if (c.a < 0.35) discard;
    outColor = vec4(c.rgb * uTint, 1.0);
})");
        normals = link(R"(#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aPart;
uniform mat4 uViewProj; uniform mat4 uModel; uniform mat4 uView;
out vec3 vN; out vec3 vColor; flat out float vPart;
void main() {
    vN = mat3(uView) * (mat3(uModel) * aNormal);
    vColor = aColor; vPart = aPart;
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
})", R"(#version 460 core
in vec3 vN; in vec3 vColor; flat in float vPart;
layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;   // a = which part (body, hair, hat …)
void main() { outNormal = vec4(normalize(vN) * 0.5 + 0.5, 1.0); outAlbedo = vec4(vColor, vPart / 255.0); }
)");
        edges = link(R"(#version 460 core
const vec2 P[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main() { gl_Position = vec4(P[gl_VertexID], 0.0, 1.0); }
)", R"(#version 460 core
uniform sampler2D tNormal, tAlbedo, tDepth;
uniform vec2 uInvSize; uniform int uMode, uColor; uniform float uThick;
out vec4 outColor;
float sil(vec2 uv) { return texture(tNormal, uv).a; }
void main() {
    vec2 uv = gl_FragCoord.xy * uInvSize;
    vec4 n0 = texture(tNormal, uv);
    float d0 = texture(tDepth, uv).r;
    float edge = 0.0, depth = n0.a > 0.5 ? d0 : 1.0;
    vec4 a0 = texture(tAlbedo, uv);
    vec3 under = a0.rgb;
    vec2 o[4] = vec2[4](vec2(1,0), vec2(-1,0), vec2(0,1), vec2(0,-1));
    for (int i = 0; i < 4; i++) {
        vec2 q = uv + o[i] * uInvSize * uThick;
        vec4 n = texture(tNormal, q);
        float d = texture(tDepth, q).r;
        if (n0.a > 0.5 && n.a > 0.5) {
            // depth jump (compare in a roughly linear form) or a sharp crease
            float l0 = 1.0 / (1.0 - d0 + 1e-6), l1 = 1.0 / (1.0 - d + 1e-6);
            float de = abs(l1 - l0) / max(l0, 1e-6);
            float ne = 1.0 - dot(normalize(n0.rgb * 2.0 - 1.0), normalize(n.rgb * 2.0 - 1.0));
            edge = max(edge, max(step(0.045, de), step(0.55, ne)));
            // where hair meets a hat (or the body), always a line
            if (abs(texture(tAlbedo, q).a - a0.a) > 0.002) { edge = 1.0; depth = min(depth, d); }
        } else if (n0.a > 0.5 || n.a > 0.5) {
            edge = 1.0;                                     // silhouette, both sides
            if (n.a > 0.5) { depth = min(depth, d); under = texture(tAlbedo, q).rgb; }
        }
    }
    vec3 ink = vec3(0.05, 0.04, 0.06);
    vec4 c = vec4(0.0);
    if (uMode >= 1 && edge > 0.5)
        c = vec4(uColor == 1 ? mix(under * 0.35, ink, 0.55) : ink, 1.0);
    if (uMode == 2 && c.a == 0.0 && n0.a < 0.5) {
        // colour echo behind the figure, like a print misregistration
        vec2 qa = uv + vec2(-7.0, 5.0) * uInvSize, qb = uv + vec2(7.0, -3.0) * uInvSize;
        if (sil(qa) > 0.5)      { c = vec4(0.84, 0.64, 0.74, 0.6); depth = texture(tDepth, qa).r; }
        else if (sil(qb) > 0.5) { c = vec4(0.62, 0.72, 0.84, 0.6); depth = texture(tDepth, qb).r; }
    }
    if (c.a == 0.0) discard;
    gl_FragDepth = depth - 0.00002;
    outColor = c;
})");
        if (!body || !face || !normals || !edges) return false;

        // face card corners
        const float q[12] = { -0.5f,-0.5f, 0.5f,-0.5f, 0.5f,0.5f, -0.5f,-0.5f, 0.5f,0.5f, -0.5f,0.5f };
        glGenVertexArrays(1, &quadVao); glGenBuffers(1, &quadVbo);
        glBindVertexArray(quadVao);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glGenVertexArrays(1, &emptyVao);
        glBindVertexArray(0);
        return true;
    }

    void cleanup() {
        for (GLuint p : { body, face, normals, edges }) if (p) glDeleteProgram(p);
        body = face = normals = edges = 0;
        if (quadVbo) glDeleteBuffers(1, &quadVbo);
        if (quadVao) glDeleteVertexArrays(1, &quadVao);
        if (emptyVao) glDeleteVertexArrays(1, &emptyVao);
        quadVbo = quadVao = emptyVao = 0;
        freeTargets();
    }

    // Model-to-world for a character standing at 'feet' (world voxels)
    static glm::mat4 rootTransform(const Character& ch, glm::vec3 feet) {
        const Pose& p = ch.pose();
        glm::mat4 m = glm::translate(glm::mat4(1.0f), feet);
        m = glm::rotate(m, glm::radians(90.0f - p.bodyYaw), glm::vec3(0, 1, 0));   // model +Z forward; yaw 0 faces +X
        m = glm::scale(m, glm::vec3(0.25f));                                       // inches → world voxels
        if (p.spinPitch != 0.0f) {
            float mid = ch.getModel().heightIn * 0.35f;
            m = glm::translate(m, glm::vec3(0, mid, 0));
            m = glm::rotate(m, glm::radians(p.spinPitch), glm::vec3(1, 0, 0));
            m = glm::translate(m, glm::vec3(0, -mid, 0));
        }
        return m;
    }

    // Colour pass for one character
    void draw(const Character& ch, const glm::mat4& viewProj, glm::vec3 feet, glm::vec3 camPos,
              glm::vec3 fogColor, float fogDensity) {
        if (!body || !ch.ready()) return;
        std::array<glm::mat4, BONE_COUNT> mats;
        boneMatrices(ch.getModel(), ch.pose(), mats);
        glm::mat4 root = rootTransform(ch, feet);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
        glUseProgram(body);
        glUniformMatrix4fv(loc(body, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3f(loc(body, "uCamPos"), camPos.x, camPos.y, camPos.z);
        glUniform3f(loc(body, "uFogColor"), fogColor.r, fogColor.g, fogColor.b);
        glUniform1f(loc(body, "uFogDensity"), fogDensity);
        glUniform3f(loc(body, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
        glUniform3f(loc(body, "uSkyTint"), skyTint.x, skyTint.y, skyTint.z);
        glUniform3f(loc(body, "uBlockTint"), blockTint.x, blockTint.y, blockTint.z);
        GLint lm = loc(body, "uModel");
        glBindVertexArray(ch.vao);
        for (int b = 0; b < BONE_COUNT; b++) {
            if (!ch.count[b]) continue;
            glm::mat4 m = root * mats[b];
            glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(m));
            glDrawArrays(GL_TRIANGLES, ch.first[b], ch.count[b]);
        }
        // face card on the front of the head
        glDisable(GL_CULL_FACE);
        glUseProgram(face);
        glm::mat4 hm = root * mats[B_HEAD];
        glUniformMatrix4fv(loc(face, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniformMatrix4fv(loc(face, "uModel"), 1, GL_FALSE, glm::value_ptr(hm));
        const CharacterModel& m = ch.getModel();
        glUniform3f(loc(face, "uCenter"), m.faceCenter.x, m.faceCenter.y, m.faceCenter.z);
        glUniform1f(loc(face, "uSize"), m.faceSize);
        {
            glm::vec3 t = glm::min(skyTint * 0.9f + blockTint, glm::vec3(1.2f));
            glUniform3f(loc(face, "uTint"), t.x, t.y, t.z);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ch.faceTex[ch.blinking() ? 1 : 0]);
        glUniform1i(loc(face, "uFace"), 0);
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    struct Placed { const Character* ch; glm::vec3 feet; };

    // Edge lines for the characters just drawn. The viewport is
    // the one they were drawn in; fbW/fbH the whole framebuffer.
    void drawEdges(const std::vector<Placed>& list, const glm::mat4& view, const glm::mat4& proj,
                   int vx, int vy, int vw, int vh, int fbW, int fbH, EdgeStyle style) {
        if (style.mode == 0 || list.empty() || !normals) return;
        ensureTargets(fbW, fbH);
        GLint prevFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(vx, vy, vw, vh);
        glEnable(GL_SCISSOR_TEST); glScissor(vx, vy, vw, vh);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
        glDisable(GL_BLEND);
        glUseProgram(normals);
        glm::mat4 vp = proj * view;
        glUniformMatrix4fv(loc(normals, "uViewProj"), 1, GL_FALSE, glm::value_ptr(vp));
        glUniformMatrix4fv(loc(normals, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        GLint lm = loc(normals, "uModel");
        for (const Placed& p : list) {
            if (!p.ch || !p.ch->ready()) continue;
            std::array<glm::mat4, BONE_COUNT> mats;
            boneMatrices(p.ch->getModel(), p.ch->pose(), mats);
            glm::mat4 root = rootTransform(*p.ch, p.feet);
            glBindVertexArray(p.ch->vao);
            for (int b = 0; b < BONE_COUNT; b++) {
                if (!p.ch->count[b]) continue;
                glm::mat4 m = root * mats[b];
                glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(m));
                glDrawArrays(GL_TRIANGLES, p.ch->first[b], p.ch->count[b]);
            }
        }
        glDisable(GL_CULL_FACE);

        // composite onto the screen, depth-tested against the world
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glViewport(vx, vy, vw, vh);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(edges);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texNormal);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texAlbedo);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texDepth);
        glUniform1i(loc(edges, "tNormal"), 0);
        glUniform1i(loc(edges, "tAlbedo"), 1);
        glUniform1i(loc(edges, "tDepth"), 2);
        glUniform2f(loc(edges, "uInvSize"), 1.0f / fbW, 1.0f / fbH);
        glUniform1i(loc(edges, "uMode"), style.mode);
        glUniform1i(loc(edges, "uColor"), style.color);
        glUniform1f(loc(edges, "uThick"), fbH >= 1000 ? 1.6f : 1.1f);
        glBindVertexArray(emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
    }

    // A character alone in a rectangle of the screen (pixels,
    // origin top-left) — the creator's preview. Clears depth
    // there and leaves the viewport as it found it.
    void drawPreview(const Character& ch, int x, int y, int w, int h, int screenW, int screenH,
                     float turnDeg, EdgeStyle style, float focus = 0.0f) {
        if (!body || !ch.ready() || w <= 0 || h <= 0) return;
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        int gy = screenH - y - h;
        glViewport(x, gy, w, h);
        glEnable(GL_SCISSOR_TEST); glScissor(x, gy, w, h);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        // The camera is framed for the tallest body (6'0"), not
        // this one, so shorter characters really look shorter.
        // focus 0: the whole body; 1: close on this one's head
        const float frame = 18.0f;
        float tall = ch.getModel().heightIn / 4.0f;
        glm::vec3 target(0.0f, frame * 0.55f + (tall * 0.92f - frame * 0.55f) * focus, 0.0f);
        float dist = frame * (2.3f - 1.6f * focus);
        glm::vec3 eye = target + glm::vec3(std::sin(glm::radians(turnDeg)) * dist, frame * (0.25f - 0.17f * focus),
                                           std::cos(glm::radians(turnDeg)) * dist);
        glm::mat4 proj = glm::perspective(glm::radians(32.0f), (float)w / (float)h, 0.5f, 500.0f);
        glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        draw(ch, proj * view, glm::vec3(0.0f), eye, glm::vec3(0.0f), 0.0f);
        drawEdges({ { &ch, glm::vec3(0.0f) } }, view, proj, x, gy, w, h, screenW, screenH, style);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
    }

private:
    GLuint body = 0, face = 0, normals = 0, edges = 0;
    GLuint quadVao = 0, quadVbo = 0, emptyVao = 0;
    GLuint fbo = 0, texNormal = 0, texAlbedo = 0, texDepth = 0;
    int    fbW = 0, fbH = 0;

    static GLint loc(GLuint p, const char* n) { return glGetUniformLocation(p, n); }

    void ensureTargets(int w, int h) {
        if (fbo && w == fbW && h == fbH) return;
        freeTargets();
        fbW = w; fbH = h;
        auto tex = [&](GLuint& t, GLint internal, GLenum fmt, GLenum type) {
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, type, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        tex(texNormal, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        tex(texAlbedo, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        tex(texDepth, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texNormal, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texAlbedo, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texDepth, 0);
        const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, bufs);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "CharacterRenderer: edge buffer incomplete\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    void freeTargets() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        GLuint t[3] = { texNormal, texAlbedo, texDepth };
        for (GLuint x : t) if (x) glDeleteTextures(1, &x);
        fbo = texNormal = texAlbedo = texDepth = 0;
        fbW = fbH = 0;
    }

    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::cerr << "CharacterRenderer: shader error: " << log << "\n";
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
    static GLuint link(const char* vs, const char* fs) {
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) return 0;
        GLuint p = glCreateProgram();
        glAttachShader(p, v); glAttachShader(p, f);
        glLinkProgram(p);
        glDeleteShader(v); glDeleteShader(f);
        GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) { std::cerr << "CharacterRenderer: link failed\n"; glDeleteProgram(p); return 0; }
        return p;
    }
};
