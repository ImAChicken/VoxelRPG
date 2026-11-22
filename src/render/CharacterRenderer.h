#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <map>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>
#include "../character/CharacterAnimator.h"
#include "../character/HeldItemModel.h"
#include "../character/FaceComposer.h"
#include "../character/MonsterModel.h"

// ══════════════════════════════════════════════════════════
// Character — one human's look, meshes, face and animation
// ══════════════════════════════════════════════════════════
// Owns the GPU copy of a CharacterModel and its painted face.
// setAppearance() rebuilds both when the look changes.
// Upload a part mesh into a VAO (positions, normals, colours,
// part id), creating the buffers the first time
inline void uploadPartMesh(const std::vector<PartVertex>& v, GLuint& vao, GLuint& vbo) {
    if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(PartVertex)), v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PartVertex), (void*)offsetof(PartVertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_BYTE, GL_TRUE, sizeof(PartVertex), (void*)offsetof(PartVertex, nx));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PartVertex), (void*)offsetof(PartVertex, r));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(PartVertex), (void*)offsetof(PartVertex, a));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

class Character {
public:
    Character() = default;
    ~Character() { release(); }
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;

    // Rebuild the body (main thread, GL context current).
    // Does nothing if neither the look nor the hair mesh mode
    // has changed.
    // gear: what it wears (GearLook.h), built onto the body
    void setAppearance(const Appearance& a, HairMesh mode = HairMesh::Shards, bool force = false,
                       const std::vector<GearLook>& gear = {}) {
        if (!force && built && a == look && mode == hairMode && gear == wearing) return;
        look = a;
        hairMode = mode;
        wearing = gear;
        model = CharacterModel::build(a, mode, &wearing);
        upload();
        built = true;
    }

    const Pose& animate(const AnimInput& in, float dt) {
        blinkClock += dt;
        animatedOnce = true;
        return animator.update(model, in, dt);
    }
    bool posed() const { return animatedOnce; }

    const Appearance&     appearance() const { return look; }
    const CharacterModel& getModel()   const { return model; }
    const Pose&           pose()       const { return animator.current(); }
    bool                  ready()      const { return built && vao != 0; }
    bool                  animatedOnce = false;
    void                  resetAnimation()   { animator.reset(); }

    // What the right hand holds (character/HeldItemModel.h).
    // Rebuilt only when it, the body or the hair mesh mode
    // changes.
    void setHeldModel(const std::string& which) {
        if (which == heldKind && heldFor == model.heightIn && heldMode == hairMode && heldGen == gHeldModels.generation()) return;
        heldKind = which; heldFor = model.heightIn; heldMode = hairMode; heldGen = gHeldModels.generation();
        const HeldModelDef* d = which.empty() ? nullptr : gHeldModels.find(which);
        std::vector<PartVertex> v = d ? d->mesh(model, hairMode) : std::vector<PartVertex>();
        heldCount = (GLsizei)v.size();
        if (heldCount) uploadPartMesh(v, heldVao, heldVbo);
    }
    GLuint  heldVao = 0, heldVbo = 0;
    GLsizei heldCount = 0;
    // The held item's thrust turn for the current pose
    glm::mat4 heldTurn() const {
        const HeldModelDef* d = heldKind.empty() ? nullptr : gHeldModels.find(heldKind);
        return d ? d->turn(model, animator.current().heldTurn) : glm::mat4(1.0f);
    }

    // What the left hand holds (the active off-hand slot): the
    // same models, mirrored onto the left wrist
    void setOffhandModel(const std::string& which) {
        if (which == offKind && offFor == model.heightIn && offMode == hairMode && offGen == gHeldModels.generation()) return;
        offKind = which; offFor = model.heightIn; offMode = hairMode; offGen = gHeldModels.generation();
        const HeldModelDef* d = which.empty() ? nullptr : gHeldModels.find(which);
        std::vector<PartVertex> v = d ? d->mesh(model, hairMode) : std::vector<PartVertex>();
        for (PartVertex& p : v) { p.x = -p.x; p.nx = (int8_t)-p.nx; }
        for (size_t i = 0; i + 2 < v.size(); i += 3) std::swap(v[i + 1], v[i + 2]);   // keep the winding
        offCount = (GLsizei)v.size();
        if (offCount) uploadPartMesh(v, offVao, offVbo);
    }
    GLuint  offVao = 0, offVbo = 0;
    GLsizei offCount = 0;

    // A skeleton from outside the animation (the ragdoll), in
    // world voxels; drawn instead of the animated pose until
    // cleared. The animation keeps running underneath.
    void setWorldPose(const std::array<glm::mat4, BONE_COUNT>& m) { worldMats = m; useWorld = true; }
    void clearWorldPose()                                          { useWorld = false; }
    bool hasWorldPose() const                                      { return useWorld; }
    const std::array<glm::mat4, BONE_COUNT>& worldPose() const     { return worldMats; }

    // Eyes close for a moment every few seconds
    bool blinking() const { return std::fmod(blinkClock, 3.6f) < 0.13f; }

    // ── Used by CharacterRenderer ─────────────────────────
    GLuint vao = 0, vbo = 0;
    // the painted face: eyes open / blinking × mouth shut … wide
    // (FaceComposer::MOUTHS), [blink * MOUTHS + mouth]
    GLuint faceTex[2 * FaceComposer::MOUTHS] = {};
    // How open the mouth is (0 .. 1), set each frame while it talks
    // (audio: AudioEngine::mouthOf)
    void setTalk(float open) { talkOpen = open; }
    int mouthShape() const { return talkOpen < 0.18f ? 0 : talkOpen < 0.55f ? 1 : 2; }
    GLuint faceNow() const { return faceTex[(blinking() ? 1 : 0) * FaceComposer::MOUTHS + mouthShape()]; }
    std::array<GLint, BONE_COUNT>   first{};
    std::array<GLsizei, BONE_COUNT> count{};

private:
    Appearance        look;
    std::vector<GearLook> wearing;
    HairMesh          hairMode = HairMesh::Shards;
    CharacterModel    model;
    CharacterAnimator animator;
    bool              built = false;
    float             blinkClock = 0.0f;
    float             talkOpen = 0.0f;
    std::array<glm::mat4, BONE_COUNT> worldMats{};
    bool              useWorld = false;
    std::string       heldKind;
    uint32_t          heldGen = 0;
    float             heldFor = 0.0f;
    HairMesh          heldMode = HairMesh::Shards;
    std::string       offKind;
    uint32_t          offGen = 0;
    float             offFor = 0.0f;
    HairMesh          offMode = HairMesh::Shards;

    void upload() {
        std::vector<PartVertex> all;
        for (int b = 0; b < BONE_COUNT; b++) {
            first[b] = (GLint)all.size();
            count[b] = (GLsizei)model.parts[b].size();
            all.insert(all.end(), model.parts[b].begin(), model.parts[b].end());
        }
        uploadPartMesh(all, vao, vbo);

        // the painted face, open and blinking, each with its mouth shapes
        std::vector<uint8_t> px;
        for (int k = 0; k < 2 * FaceComposer::MOUTHS; k++) {
            gFaces.compose(look, k / FaceComposer::MOUTHS == 1, px, k % FaceComposer::MOUTHS);
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
        if (heldVbo) glDeleteBuffers(1, &heldVbo);
        if (heldVao) glDeleteVertexArrays(1, &heldVao);
        if (offVbo) glDeleteBuffers(1, &offVbo);
        if (offVao) glDeleteVertexArrays(1, &offVao);
        if (faceTex[0]) glDeleteTextures(2 * FaceComposer::MOUTHS, faceTex);
        vao = vbo = heldVao = heldVbo = offVao = offVbo = 0;
        heldCount = offCount = 0; heldKind.clear(); offKind.clear();
        for (GLuint& t : faceTex) t = 0;
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
//                 Each character can also carry a glow
//                 (render/OutlineFx.h: hurt, charging …): its
//                 colour goes into its lines, a soft halo around
//                 it and both tones of the echo.
class CharacterRenderer {
public:
    // How the next characters drawn are lit (set per character
    // from the baked light where it stands; the creator's
    // preview uses the defaults)
    glm::vec3 lightDir  = glm::normalize(glm::vec3(0.33f, 0.74f, 0.58f));
    glm::vec3 skyTint   = glm::vec3(1.0f);
    glm::vec3 blockTint = glm::vec3(0.0f);
    // At a portal (world/PortalLinks.h; main.cpp's portal bodies), for
    // the next draws until reset:
    //   xform   moves the whole body (a copy of one going through,
    //           drawn where it comes out)
    //   clip    a plane (n, d): only what's in front (n·p + d ≥ 0) is
    //           drawn; (0,0,0,1) keeps everything
    glm::mat4 xform = glm::mat4(1.0f);
    glm::vec4 clip  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    void resetPortal() { xform = glm::mat4(1.0f); clip = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); }
    bool init() {
        body = link(R"(#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uViewProj; uniform mat4 uModel; uniform vec3 uCamPos; uniform vec4 uClip;
// the whole body in one draw: its bones' matrices, and where each
// bone's vertices start (they're one after another, in bone order)
uniform int uSkinned; uniform mat4 uBones[15]; uniform int uFirst[15];
out vec3 vColor; out vec3 vNormal; out float vDist;
mat4 modelOf() {
    if (uSkinned == 0) return uModel;
    int b = 0;
    for (int i = 1; i < 15; i++) if (gl_VertexID >= uFirst[i]) b = i;
    return uBones[b];
}
void main() {
    mat4 M = modelOf();
    vec4 world = M * vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(vec4(world.xyz, 1.0), uClip);
    vNormal = mat3(M) * aNormal;
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
uniform mat4 uViewProj; uniform mat4 uModel; uniform vec3 uCenter; uniform float uSize; uniform vec4 uClip;
out vec2 vUv;
void main() {
    vUv = vec2(aCorner.x + 0.5, 0.5 - aCorner.y);
    vec4 world = uModel * vec4(uCenter + vec3(aCorner * uSize, 0.0), 1.0);
    gl_ClipDistance[0] = dot(vec4(world.xyz, 1.0), uClip);
    gl_Position = uViewProj * world;
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
uniform mat4 uViewProj; uniform mat4 uModel; uniform mat4 uView; uniform vec4 uClip;
uniform int uSkinned; uniform mat4 uBones[15]; uniform int uFirst[15];   // (as the body pass)
out vec3 vN; out vec3 vColor; flat out float vPart;
mat4 modelOf() {
    if (uSkinned == 0) return uModel;
    int b = 0;
    for (int i = 1; i < 15; i++) if (gl_VertexID >= uFirst[i]) b = i;
    return uBones[b];
}
void main() {
    mat4 M = modelOf();
    vN = mat3(uView) * (mat3(M) * aNormal);
    vColor = aColor; vPart = aPart;
    vec4 world = M * vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(vec4(world.xyz, 1.0), uClip);
    gl_Position = uViewProj * world;
})", R"(#version 460 core
in vec3 vN; in vec3 vColor; flat in float vPart;
uniform vec4 uGlow;                        // the character's outline glow (rgb, a = strength)
layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;   // a = which part (body, hair, hat …)
layout(location = 2) out vec4 outGlow;
void main() { outNormal = vec4(normalize(vN) * 0.5 + 0.5, 1.0); outAlbedo = vec4(vColor, vPart / 255.0); outGlow = uGlow; }
)");
        // ── Monsters (character/MonsterModel.h): a mesh skinned to its own
        // skeleton, up to 4 bones a vertex; the same toon bands, and into
        // the same edge buffer as the characters
        static const char* MON_VS = R"(#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in uvec4 aBone;
layout(location = 4) in vec4 aWeight;
uniform mat4 uViewProj; uniform mat4 uView; uniform vec3 uCamPos; uniform vec4 uClip;
uniform mat4 uSkin[64];          // per bone: rest → posed, in the world
out vec3 vColor; out vec3 vNormal; out vec3 vViewN; out float vDist;
void main() {
    mat4 M = uSkin[aBone.x] * aWeight.x + uSkin[aBone.y] * aWeight.y + uSkin[aBone.z] * aWeight.z + uSkin[aBone.w] * aWeight.w;
    vec4 world = M * vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(vec4(world.xyz, 1.0), uClip);
    vNormal = mat3(M) * aNormal;
    vViewN = mat3(uView) * vNormal;
    vColor = aColor; vDist = length(world.xyz - uCamPos);
    gl_Position = uViewProj * world;
})";
        monBody = link(MON_VS, R"(#version 460 core
in vec3 vColor; in vec3 vNormal; in vec3 vViewN; in float vDist;
uniform vec3 uFogColor; uniform float uFogDensity;
uniform vec3 uLightDir; uniform vec3 uSkyTint; uniform vec3 uBlockTint;
uniform vec4 uFlash;             // rgb, a = how much (a hit's flash)
out vec4 outColor;
void main() {
    // (the characters' toon light: three hard steps of the sun or moon over a soft fill)
    vec3 n = normalize(vNormal);
    float d = max(dot(n, uLightDir), 0.0);
    float ramp = d < 0.25 ? 0.49 : (d < 0.5 ? 0.76 : 1.0);
    vec3 fill = mix(vec3(0.80, 0.75, 0.93), vec3(1.0), n.y * 0.5 + 0.5);
    vec3 c = vColor * (uSkyTint * fill * (0.55 + 0.45 * ramp) + uBlockTint);
    c = mix(c, uFlash.rgb, uFlash.a);
    float fog = 1.0 - exp(-vDist * uFogDensity);
    outColor = vec4(mix(c, uFogColor, fog), 1.0);
})");
        monNormals = link(MON_VS, R"(#version 460 core
in vec3 vColor; in vec3 vNormal; in vec3 vViewN; in float vDist;
uniform vec4 uGlow;
layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outGlow;
void main() { outNormal = vec4(normalize(vViewN) * 0.5 + 0.5, 1.0); outAlbedo = vec4(vColor, 0.0); outGlow = uGlow; }
)");
        edges = link(R"(#version 460 core
const vec2 P[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
void main() { gl_Position = vec4(P[gl_VertexID], 0.0, 1.0); }
)", R"(#version 460 core
uniform sampler2D tNormal, tAlbedo, tDepth, tGlow;
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
    vec4 glow = n0.a > 0.5 ? texture(tGlow, uv) : vec4(0.0);   // the glow of the figure this line belongs to
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
            if (n.a > 0.5) {
                depth = min(depth, d); under = texture(tAlbedo, q).rgb;
                vec4 g = texture(tGlow, q);
                if (g.a > glow.a) glow = g;
            }
        }
    }
    vec3 ink = vec3(0.05, 0.04, 0.06);
    vec4 c = vec4(0.0);
    if (edge > 0.5 && (uMode >= 1 || glow.a > 0.02)) {
        vec3 line = uColor == 1 ? mix(under * 0.35, ink, 0.55) : ink;
        // a glowing figure's lines take its colour (they stay ink
        // when lines are off and only the glow shows)
        c = vec4(mix(line, glow.rgb, glow.a), uMode >= 1 ? 1.0 : glow.a);
    }
    if (uMode == 2 && c.a == 0.0 && n0.a < 0.5) {
        // colour echo behind the figure, like a print misregistration;
        // a glowing figure echoes in two tones of its glow
        vec2 qa = uv + vec2(-7.0, 5.0) * uInvSize, qb = uv + vec2(7.0, -3.0) * uInvSize;
        if (sil(qa) > 0.5) {
            vec4 g = texture(tGlow, qa);
            c = vec4(mix(vec3(0.84, 0.64, 0.74), g.rgb, g.a), 0.6 + 0.3 * g.a); depth = texture(tDepth, qa).r;
        } else if (sil(qb) > 0.5) {
            vec4 g = texture(tGlow, qb);
            c = vec4(mix(vec3(0.62, 0.72, 0.84), g.rgb * 0.55, g.a), 0.6 + 0.3 * g.a); depth = texture(tDepth, qb).r;
        }
    }
    if (c.a == 0.0 && n0.a < 0.5) {
        // the halo: a glowing figure within a few pixels
        float best = 0.0; vec3 hc = vec3(0.0); float hd = 1.0;
        for (int r = 1; r <= 2; r++) {
            float rad = uThick * (r == 1 ? 2.5 : 4.5), fall = r == 1 ? 0.7 : 0.35;
            for (int i = 0; i < 8; i++) {
                float an = 0.7854 * float(i) + 0.39 * float(r);
                vec2 q = uv + vec2(cos(an), sin(an)) * rad * uInvSize;
                if (sil(q) < 0.5) continue;
                vec4 g = texture(tGlow, q);
                if (g.a * fall > best) { best = g.a * fall; hc = g.rgb; hd = texture(tDepth, q).r; }
            }
        }
        if (best > 0.01) { c = vec4(hc, best); depth = hd; }
    }
    if (c.a == 0.0) discard;
    gl_FragDepth = depth - 0.00002;
    outColor = c;
})");
        if (!body || !face || !normals || !edges || !monBody || !monNormals) return false;

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
        for (GLuint p : { body, face, normals, edges, monBody, monNormals }) if (p) glDeleteProgram(p);
        body = face = normals = edges = monBody = monNormals = 0;
        for (auto& [m, g] : monMeshes) { if (g.vbo) glDeleteBuffers(1, &g.vbo); if (g.vao) glDeleteVertexArrays(1, &g.vao); }
        monMeshes.clear();
        if (quadVbo) glDeleteBuffers(1, &quadVbo);
        if (quadVao) glDeleteVertexArrays(1, &quadVao);
        if (emptyVao) glDeleteVertexArrays(1, &emptyVao);
        quadVbo = quadVao = emptyVao = 0;
        freeTargets();
    }

    // Model-to-world for a character standing at 'feet' (world voxels)
    static glm::mat4 rootTransform(const Character& ch, glm::vec3 feet) {
        return characterRoot(ch.getModel(), ch.pose(), feet);
    }

    // Each part's world matrix: the ragdoll's skeleton when the
    // character has one (Character::setWorldPose), otherwise
    // the animation placed at the feet
    static void bodyMatrices(const Character& ch, glm::vec3 feet, std::array<glm::mat4, BONE_COUNT>& out) {
        if (ch.hasWorldPose()) { out = ch.worldPose(); return; }
        worldBoneMatrices(ch.getModel(), ch.pose(), feet, out);
    }

    // Colour pass for one character
    void draw(const Character& ch, const glm::mat4& viewProj, glm::vec3 feet, glm::vec3 camPos,
              glm::vec3 fogColor, float fogDensity) {
        if (!body || !ch.ready()) return;
        std::array<glm::mat4, BONE_COUNT> mats;
        bodyMatrices(ch, feet, mats);
        for (glm::mat4& m : mats) m = xform * m;
        glEnable(GL_CLIP_DISTANCE0);

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
        glUniform4fv(loc(body, "uClip"), 1, glm::value_ptr(clip));
        GLint lm = loc(body, "uModel");
        glBindVertexArray(ch.vao);
        drawBones(body, ch, mats, lm);
        if (ch.heldCount) {                       // the tool in the right hand
            glBindVertexArray(ch.heldVao);
            glm::mat4 hm = mats[B_WRIST_R] * ch.heldTurn();                 // a thrust turns the item
            glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(hm));
            glDrawArrays(GL_TRIANGLES, 0, ch.heldCount);
            glBindVertexArray(ch.vao);
        }
        if (ch.offCount) {                        // the off-hand item
            glBindVertexArray(ch.offVao);
            glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(mats[B_WRIST_L]));
            glDrawArrays(GL_TRIANGLES, 0, ch.offCount);
            glBindVertexArray(ch.vao);
        }
        // face card on the front of the head
        glDisable(GL_CULL_FACE);
        glUseProgram(face);
        glm::mat4 hm = mats[B_HEAD];
        glUniformMatrix4fv(loc(face, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniformMatrix4fv(loc(face, "uModel"), 1, GL_FALSE, glm::value_ptr(hm));
        const CharacterModel& m = ch.getModel();
        glUniform3f(loc(face, "uCenter"), m.faceCenter.x, m.faceCenter.y, m.faceCenter.z);
        glUniform1f(loc(face, "uSize"), m.faceSize);
        glUniform4fv(loc(face, "uClip"), 1, glm::value_ptr(clip));
        {
            glm::vec3 t = glm::min(skyTint * 0.9f + blockTint, glm::vec3(1.2f));
            glUniform3f(loc(face, "uTint"), t.x, t.y, t.z);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ch.faceNow());
        glUniform1i(loc(face, "uFace"), 0);
        glBindVertexArray(quadVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_CLIP_DISTANCE0);
    }

    // (xform, clip: as above, for a body at a portal)
    // A monster instead of a character: its model and its bones'
    // skinning matrices in the world (ch null)
    struct Placed {
        const Character* ch; glm::vec3 feet; glm::vec4 glow = glm::vec4(0.0f);
        glm::mat4 xform = glm::mat4(1.0f); glm::vec4 clip = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const MonsterModel* mon = nullptr; const std::vector<glm::mat4>* skin = nullptr;
    };

    // Colour pass for a monster: skin = each bone's rest → posed matrix
    // in the world (MonsterModel::skin, then placed); flash: a hit's
    // flash (rgb, a = how much)
    void drawMonster(const MonsterModel& m, const std::vector<glm::mat4>& skin, const glm::mat4& viewProj,
                     glm::vec3 camPos, glm::vec3 fogColor, float fogDensity, glm::vec4 flash = glm::vec4(0.0f)) {
        const MonGpu* g = monMesh(m);
        if (!monBody || !g || skin.empty()) return;
        glEnable(GL_CLIP_DISTANCE0);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
        glUseProgram(monBody);
        glUniformMatrix4fv(loc(monBody, "uViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3f(loc(monBody, "uCamPos"), camPos.x, camPos.y, camPos.z);
        glUniform3f(loc(monBody, "uFogColor"), fogColor.r, fogColor.g, fogColor.b);
        glUniform1f(loc(monBody, "uFogDensity"), fogDensity);
        glUniform3f(loc(monBody, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
        glUniform3f(loc(monBody, "uSkyTint"), skyTint.x, skyTint.y, skyTint.z);
        glUniform3f(loc(monBody, "uBlockTint"), blockTint.x, blockTint.y, blockTint.z);
        glUniform4fv(loc(monBody, "uClip"), 1, glm::value_ptr(clip));
        glUniform4fv(loc(monBody, "uFlash"), 1, glm::value_ptr(flash));
        uploadSkin(monBody, skin);
        glBindVertexArray(g->vao);
        glDrawArrays(GL_TRIANGLES, 0, g->count);
        glBindVertexArray(0);
        glDisable(GL_CLIP_DISTANCE0);
    }

    // Edge lines for the characters just drawn. The viewport is
    // the one they were drawn in; fbW/fbH the whole framebuffer.
    void drawEdges(const std::vector<Placed>& list, const glm::mat4& view, const glm::mat4& proj,
                   int vx, int vy, int vw, int vh, int fbW, int fbH, EdgeStyle style) {
        if (list.empty() || !normals) return;
        if (style.mode == 0) {      // lines off: only glowing figures need the pass
            bool any = false;
            for (const Placed& p : list) any = any || p.glow.a > 0.004f;
            if (!any) return;
        }
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
        GLint lg = loc(normals, "uGlow");
        GLint lc = loc(normals, "uClip");
        glEnable(GL_CLIP_DISTANCE0);
        for (const Placed& p : list) {
            if (p.mon && p.skin) {                     // a monster: its own program
                const MonGpu* g = monMesh(*p.mon);
                if (!g) continue;
                glUseProgram(monNormals);
                glUniformMatrix4fv(loc(monNormals, "uViewProj"), 1, GL_FALSE, glm::value_ptr(vp));
                glUniformMatrix4fv(loc(monNormals, "uView"), 1, GL_FALSE, glm::value_ptr(view));
                glUniform4fv(loc(monNormals, "uGlow"), 1, glm::value_ptr(p.glow));
                glUniform4fv(loc(monNormals, "uClip"), 1, glm::value_ptr(p.clip));
                uploadSkin(monNormals, *p.skin);
                glBindVertexArray(g->vao);
                glDrawArrays(GL_TRIANGLES, 0, g->count);
                glUseProgram(normals);
                continue;
            }
            if (!p.ch || !p.ch->ready()) continue;
            glUniform4fv(lg, 1, glm::value_ptr(p.glow));
            glUniform4fv(lc, 1, glm::value_ptr(p.clip));
            std::array<glm::mat4, BONE_COUNT> mats;
            bodyMatrices(*p.ch, p.feet, mats);
            for (glm::mat4& m : mats) m = p.xform * m;
            glBindVertexArray(p.ch->vao);
            drawBones(normals, *p.ch, mats, lm);
            if (p.ch->heldCount) {
                glBindVertexArray(p.ch->heldVao);
                glm::mat4 hm = mats[B_WRIST_R] * p.ch->heldTurn();
                glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(hm));
                glDrawArrays(GL_TRIANGLES, 0, p.ch->heldCount);
            }
            if (p.ch->offCount) {
                glBindVertexArray(p.ch->offVao);
                glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(mats[B_WRIST_L]));
                glDrawArrays(GL_TRIANGLES, 0, p.ch->offCount);
            }
        }
        glDisable(GL_CLIP_DISTANCE0);
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
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, texGlow);
        glUniform1i(loc(edges, "tGlow"), 3);
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
    GLuint body = 0, face = 0, normals = 0, edges = 0, monBody = 0, monNormals = 0;
    // each monster model's mesh on the GPU, made the first time it's drawn
    struct MonGpu { GLuint vao = 0, vbo = 0; GLsizei count = 0; };
    std::map<const MonsterModel*, MonGpu> monMeshes;
    const MonGpu* monMesh(const MonsterModel& m) {
        auto it = monMeshes.find(&m);
        if (it != monMeshes.end()) return it->second.count ? &it->second : nullptr;
        MonGpu& g = monMeshes[&m];
        if (m.verts.empty()) return nullptr;
        glGenVertexArrays(1, &g.vao); glGenBuffers(1, &g.vbo);
        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m.verts.size() * sizeof(MonsterVertex)), m.verts.data(), GL_STATIC_DRAW);
        const GLsizei st = sizeof(MonsterVertex);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(MonsterVertex, x));
        glVertexAttribPointer(1, 3, GL_BYTE, GL_TRUE, st, (void*)offsetof(MonsterVertex, nx));
        glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(MonsterVertex, r));
        glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, st, (void*)offsetof(MonsterVertex, bone));
        glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(MonsterVertex, weight));
        for (int i = 0; i < 5; i++) glEnableVertexAttribArray(i);
        glBindVertexArray(0);
        g.count = (GLsizei)m.verts.size();
        return &g;
    }
    void uploadSkin(GLuint prog, const std::vector<glm::mat4>& skin) {
        GLsizei n = (GLsizei)std::min<size_t>(skin.size(), 64);
        glUniformMatrix4fv(loc(prog, "uSkin"), n, GL_FALSE, glm::value_ptr(skin[0]));
    }
    GLuint quadVao = 0, quadVbo = 0, emptyVao = 0;
    GLuint fbo = 0, texNormal = 0, texAlbedo = 0, texDepth = 0, texGlow = 0;
    int    fbW = 0, fbH = 0;

    // (looked up once each: the names are string literals, one address each)
    std::map<std::pair<GLuint, const char*>, GLint> locs;
    GLint loc(GLuint p, const char* n) {
        auto k = std::make_pair(p, n);
        auto it = locs.find(k);
        if (it != locs.end()) return it->second;
        return locs[k] = glGetUniformLocation(p, n);
    }
    // A body's bones in one draw (prog's uSkinned path), or a draw a
    // bone if its parts aren't one run in bone order; then uSkinned off
    // again for the held items (uModel)
    void drawBones(GLuint prog, const Character& ch, const std::array<glm::mat4, BONE_COUNT>& mats, GLint lm) {
        bool run = BONE_COUNT == 15;
        for (int b = 1; b < BONE_COUNT && run; b++) run = ch.first[b] == ch.first[b - 1] + ch.count[b - 1];
        if (run) {
            GLint f[BONE_COUNT];
            for (int b = 0; b < BONE_COUNT; b++) f[b] = ch.first[b];
            glUniform1i(loc(prog, "uSkinned"), 1);
            glUniformMatrix4fv(loc(prog, "uBones"), BONE_COUNT, GL_FALSE, glm::value_ptr(mats[0]));
            glUniform1iv(loc(prog, "uFirst"), BONE_COUNT, f);
            const GLsizei total = ch.first[BONE_COUNT - 1] + ch.count[BONE_COUNT - 1] - ch.first[0];
            if (total > 0) glDrawArrays(GL_TRIANGLES, ch.first[0], total);
            glUniform1i(loc(prog, "uSkinned"), 0);
            return;
        }
        for (int b = 0; b < BONE_COUNT; b++) {
            if (!ch.count[b]) continue;
            glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(mats[b]));
            glDrawArrays(GL_TRIANGLES, ch.first[b], ch.count[b]);
        }
    }

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
        tex(texGlow, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        tex(texDepth, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texNormal, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texAlbedo, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, texGlow, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texDepth, 0);
        const GLenum bufs[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        glDrawBuffers(3, bufs);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "CharacterRenderer: edge buffer incomplete\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    void freeTargets() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        GLuint t[4] = { texNormal, texAlbedo, texDepth, texGlow };
        for (GLuint x : t) if (x) glDeleteTextures(1, &x);
        fbo = texNormal = texAlbedo = texDepth = texGlow = 0;
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
