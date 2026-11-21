#pragma once
#include "IRenderer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iostream>
#include "../render/Shader.h"
#include "../render/Sky.h"
#include "../render/Wind.h"
#include "../game/Settings.h"
#include "../render/ChunkMesh.h"
#include "../render/AtlasTexture.h"
#include "../render/BoxRenderer.h"
#include "../ui/UIRenderer.h"
#include "../render/Camera.h"
#include "../chunk/Chunk.h"

// OpenGL mesh-based renderer
// Implements IRenderer using traditional rasterization
// Future: RaytracingRenderer implements same interface
// Swap renderer by changing one line in main.cpp

class OpenGLRenderer : public IRenderer {
public:
    bool init(int screenW, int screenH) override {
        width  = screenW;
        height = screenH;

        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (!shader.load("shaders/chunk.vert",
                         "shaders/chunk.frag")) {
            std::cerr << "OpenGLRenderer: shader load failed\n";
            return false;
        }

        // Look the uniforms up once instead of every draw call
        locView        = glGetUniformLocation(shader.id, "uView");
        locProjection  = glGetUniformLocation(shader.id, "uProjection");
        locChunkOrigin = glGetUniformLocation(shader.id, "uChunkOrigin");
        locAtlas       = glGetUniformLocation(shader.id, "uAtlas");
        locTileUV      = glGetUniformLocation(shader.id, "uTileUV");
        locVoxelsPerTile =
            glGetUniformLocation(shader.id, "uVoxelsPerTile");
        locCamPos      = glGetUniformLocation(shader.id, "uCamPos");
        locFogColor    = glGetUniformLocation(shader.id, "uFogColor");
        locFogDensity  = glGetUniformLocation(shader.id, "uFogDensity");

        if (!uiRenderer.init(screenW, screenH)) {
            std::cerr << "OpenGLRenderer: UI init failed\n";
            return false;
        }

        if (!uiRenderer.loadFont()) {
            std::cerr << "OpenGLRenderer: font load failed\n";
            return false;
        }

        if (!boxRenderer.init()) {
            std::cerr << "OpenGLRenderer: box renderer init failed\n";
            return false;
        }

        std::cout << "OpenGLRenderer initialized "
                  << screenW << "x" << screenH << "\n";
        return true;
    }

    void shutdown() override {
        atlas.cleanup();
        boxRenderer.cleanup();
        uiRenderer.cleanup();
    }

    void resize(int screenW, int screenH) override {
        width  = screenW;
        height = screenH;
        glViewport(0, 0, width, height);
        uiRenderer.resize(screenW, screenH);
    }

    void beginFrame() override {
        drawCallCount  = 0;
        vertexCount    = 0;

        glClearColor(FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT |
                GL_DEPTH_BUFFER_BIT);

        if (wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    void endFrame() override {
        // nothing needed — swap handled by main
    }

    void setCamera(const Camera& camera,
                   float aspectRatio) override {
        viewMatrix = camera.getViewMatrix();
        projMatrix = camera.getProjectionMatrix(
                         aspectRatio);
        cameraPos  = camera.position;
    }

    void drawChunk(const Chunk& chunk,
                   glm::vec3 worldPos) override {
        // Kept for interface compliance. Chunks are drawn
        // from pre-built meshes — see drawMesh below.
        (void)chunk; (void)worldPos;
    }

    // ── World pass ────────────────────────────────────────
    // Call once per frame before drawing chunk meshes.
    // Sets everything that is the same for every chunk:
    // shader, camera matrices, atlas, depth test and
    // back-face culling.
    void beginWorldDraw() {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Chunk meshes are wound counter-clockwise when seen
        // from outside, so the inside faces can be skipped.
        // Roughly halves the pixels the GPU has to fill.
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        shader.use();
        glUniformMatrix4fv(locView, 1, GL_FALSE,
                           glm::value_ptr(viewMatrix));
        glUniformMatrix4fv(locProjection, 1, GL_FALSE,
                           glm::value_ptr(projMatrix));

        // Texture tiling: how big one atlas tile is in UVs,
        // and how many voxels it stretches over
        glUniform1i(locAtlas, 0);
        glUniform2f(locTileUV, atlas.tileU(), atlas.tileV());
        glUniform1f(locVoxelsPerTile, atlas.voxelsPerTile());
        glUniform3f(locCamPos, cameraPos.x, cameraPos.y, cameraPos.z);
        {   // light and haze for this frame (render/Sky.h)
            const FrameLight& fl = gFrameLight;
            GLuint p = shader.id;
            auto v3 = [&](const char* n, glm::vec3 v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); };
            auto f1 = [&](const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); };
            v3("uSkyLight", fl.sky.skyLight); v3("uLightDir", fl.sky.lightDir()); v3("uAmbient", fl.sky.ambient);
            v3("uFogHorizon", fl.sky.horizon); v3("uFogSunGlow", fl.sky.sunGlow); v3("uSunDir", fl.sky.sunDir);
            f1("uFogDensity", fl.fogDensity); f1("uSeamStart", fl.seamStart); f1("uSeamEnd", fl.seamEnd);
            f1("uSeamHaze", fl.seamHaze); f1("uNight", fl.sky.night);
            // plants: off-grid, rounded light and wind (render/Wind.h), or plain
            double now = glfwGetTime();
            wind::Now w = wind::at(now);
            f1("uTime", (float)std::fmod(now, 3600.0));
            glUniform3f(glGetUniformLocation(p, "uWind"), w.dir.x, w.dir.y, w.strength);
            glUniform1i(glGetUniformLocation(p, "uPlantStyle"), gSettings.plantStyle);
            glUniform1i(glGetUniformLocation(p, "uLightCurve"), gSettings.lightCurve);
            // water: moving along its flow (world/LightBake.h flow), or plain
            glUniform1i(glGetUniformLocation(p, "uFlow"), 3);
            glUniform1i(glGetUniformLocation(p, "uWaterStyle"), gSettings.waterStyle);
            {
                FaceUV wf = gResourcePacks.getFaceUV("water");
                glUniform4f(glGetUniformLocation(p, "uWaterTiles"), (float)wf.topX, (float)wf.topY, (float)wf.sideX, (float)wf.sideY);
            }
            glUniform1i(glGetUniformLocation(p, "uLight"), 1);
            glUniform1i(glGetUniformLocation(p, "uCanopy"), 2);
            glUniform1i(glGetUniformLocation(p, "uTint"), 4);
            glUniform1i(glGetUniformLocation(p, "uPanePass"), 0);
        }
        atlas.bind(0);

        extractFrustum(projMatrix * viewMatrix);
    }

    // The block atlas (props draw with it too)
    const AtlasTexture& getAtlas() const { return atlas; }
    bool hasAtlas() const { return atlasLoaded; }

    // Draw one chunk mesh whose corner sits at 'origin'.
    // Chunks completely outside the camera's view are skipped.
    //   lightTex  the section's baked light (3D texture), or 0
    //   tintTex   the sky's colour through window glass (3D), or 0
    void drawMesh(const ChunkMesh& mesh, glm::vec3 origin, GLuint lightTex = 0, GLuint canopyTex = 0, GLuint flowTex = 0,
                  GLuint tintTex = 0) {
        if (!atlasLoaded || mesh.isEmpty()) return;

        glm::vec3 size((float)VOXEL_DIM);
        if (!boxInFrustum(origin, origin + size)) return;
        bindSection(origin, lightTex, canopyTex, flowTex, tintTex);

        mesh.drawOpaque();

        // Leaves: two-sided, so the far side of a canopy
        // shows through the gaps in the near side
        if (mesh.hasCutout()) {
            glDisable(GL_CULL_FACE);
            mesh.drawCutout();
            glEnable(GL_CULL_FACE);
        }

        drawCallCount++;
        vertexCount += (int)mesh.vertexCount();
    }

    // ── Window glass ──────────────────────────────────────
    // After every solid chunk, far to near: beginPanes, drawPanes for
    // each section with any, endPanes. A pane both shows its own colour
    // (the lead, a sheen) and filters what's behind it: dual-source
    // blending, out = pane + behind * filter (shaders/chunk.frag)
    void beginPanes() {
        shader.use();
        atlas.bind(0);
        glEnable(GL_DEPTH_TEST);
        glUniform1i(glGetUniformLocation(shader.id, "uPanePass"), 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_SRC1_COLOR);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
    }
    void drawPanes(const ChunkMesh& mesh, glm::vec3 origin, GLuint lightTex = 0, GLuint canopyTex = 0, GLuint flowTex = 0,
                   GLuint tintTex = 0) {
        if (!atlasLoaded || !mesh.hasPanes()) return;
        if (!boxInFrustum(origin, origin + glm::vec3((float)VOXEL_DIM))) return;
        bindSection(origin, lightTex, canopyTex, flowTex, tintTex);
        mesh.drawPanes();
    }
    void endPanes() {
        glUniform1i(glGetUniformLocation(shader.id, "uPanePass"), 0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
    }

    void bindSection(glm::vec3 origin, GLuint lightTex, GLuint canopyTex, GLuint flowTex, GLuint tintTex) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_3D, tintTex ? tintTex : clearTintTexture());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, lightTex ? lightTex : fullSkyTexture());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, canopyTex ? canopyTex : noCanopyTexture());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, flowTex ? flowTex : noFlowTexture());
        glActiveTexture(GL_TEXTURE0);
        glUniform3f(locChunkOrigin, origin.x, origin.y, origin.z);
    }

    // Call after the last chunk, before any UI
    void endWorldDraw() {
        glBindVertexArray(0);
        glDisable(GL_CULL_FACE);
    }

    // A plain coloured box in the world — a stand-in for
    // anything without a model yet (see BoxRenderer.h).
    // Call after endWorldDraw() and before any UI.
    void drawBox(glm::vec3 base, glm::vec3 size,
                 float yawDeg, glm::vec3 color) {
        boxRenderer.draw(projMatrix * viewMatrix,
                         base, size, yawDeg, color);
        drawCallCount++;
    }

    UIRenderer* getUIRenderer() override {
        return &uiRenderer;
    }

    bool loadAtlas(const std::string& path,
                   int tileSize,
                   int cols) override {
        if (!atlas.load(path, tileSize, cols)) {
            std::cerr << "OpenGLRenderer: atlas load failed: "
                      << path << "\n";
            return false;
        }
        atlasLoaded = true;
        return true;
    }

    void setWireframe(bool enabled) override {
        wireframe = enabled;
    }

    void setFogEnabled(bool enabled) override {
        fogEnabled = enabled;
        // TODO M8 — pass to shader uniform
    }

    void setFogDistance(float dist) override {
        fogDistance = dist;
        // TODO M8 — pass to shader uniform
    }

    int getDrawCalls()   const override { return drawCallCount; }
    int getVertexCount() const override { return vertexCount; }
    int getScreenWidth() const override { return width; }
    int getScreenHeight()const override { return height; }

    // Direct shader access for main loop
    Shader& getShader() { return shader; }

    // ── Sky and distance haze ─────────────────────────────
    // The sky is the fog colour, so far land fades into it.
    // Density: about 75% haze at 12,000 voxels (4,000 ft).
    static constexpr glm::vec3 FOG_COLOR   = glm::vec3(0.58f, 0.72f, 0.93f);
    static constexpr float     FOG_DENSITY = 1.15e-4f;
    // The fog used this frame: thicker than FOG_DENSITY when
    // the render distance is short, so the world fades out
    // before it ends. Set by main from the settings.
    static inline float        fogDensity  = FOG_DENSITY;

    const glm::mat4& getViewMatrix() const { return viewMatrix; }
    // Inside the world pass (after beginWorldDraw): draw with another
    // projection than the one culling went by (a portal's view: the
    // near plane leaned onto the far opening, render/PortalView.h)
    // More planes to cull sections by, until the next beginWorldDraw:
    // a portal view only sees through its opening
    void narrowFrustum(const glm::vec4* planes, int n) {
        for (int i = 0; i < n && frustumPlanes < 11; i++) frustum[frustumPlanes++] = planes[i];
    }
    void overrideProjection(const glm::mat4& p) {
        glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(p));
    }

    // A block's colour seen from far away: the average of its
    // top tile in the atlas
    glm::u8vec3 tileAverage(int tx, int ty) const { return atlas.averageColor(tx, ty); }

private:
    Shader      shader;
    AtlasTexture atlas;
    UIRenderer  uiRenderer;
    BoxRenderer boxRenderer;

    glm::mat4   viewMatrix = glm::mat4(1.0f);
    glm::mat4   projMatrix = glm::mat4(1.0f);

    // A 1³ light texture of open sky, for sections whose light
    // isn't baked yet
    GLuint fullSky = 0;
    GLuint noCanopy = 0;
    GLuint noFlow = 0;
    GLuint noFlowTexture() {
        if (!noFlow) {
            int8_t v[2] = { 0, 0 };
            glGenTextures(1, &noFlow);
            glBindTexture(GL_TEXTURE_2D, noFlow);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8_SNORM, 1, 1, 0, GL_RG, GL_BYTE, v);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        return noFlow;
    }
    GLuint noCanopyTexture() {
        if (!noCanopy) {
            uint16_t v[3] = { 0, 0, 0 };
            glGenTextures(1, &noCanopy);
            glBindTexture(GL_TEXTURE_2D, noCanopy);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16UI, 1, 1, 0, GL_RGB_INTEGER, GL_UNSIGNED_SHORT, v);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        return noCanopy;
    }
    GLuint clearTint = 0;
    GLuint clearTintTexture() {
        if (!clearTint) {
            uint16_t v = 0xFFFF;   // every colour gets through
            glGenTextures(1, &clearTint);
            glBindTexture(GL_TEXTURE_3D, clearTint);
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA4, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, &v);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_3D, 0);
        }
        return clearTint;
    }
    GLuint fullSkyTexture() {
        if (!fullSky) {
            uint16_t v = 15;   // sky 15, no block light
            glGenTextures(1, &fullSky);
            glBindTexture(GL_TEXTURE_3D, fullSky);
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA4, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, &v);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        return fullSky;
    }
    GLint locView = -1, locProjection = -1, locChunkOrigin = -1;
    GLint locAtlas = -1, locTileUV = -1, locVoxelsPerTile = -1;
    GLint locCamPos = -1, locFogColor = -1, locFogDensity = -1;
    glm::vec3 cameraPos = glm::vec3(0.0f);

    // The six planes of the camera's view volume, each stored
    // as (normal, distance). A point is inside when
    // dot(normal, point) + distance >= 0 for all six.
    // (and up to 5 more: narrowFrustum, a portal's opening)
    glm::vec4 frustum[11];
    int frustumPlanes = 6;

    void extractFrustum(const glm::mat4& m) {
        glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
        glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
        glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
        glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
        frustum[0] = r3 + r0;   // left
        frustum[1] = r3 - r0;   // right
        frustum[2] = r3 + r1;   // bottom
        frustum[3] = r3 - r1;   // top
        frustum[4] = r3 + r2;   // near
        frustum[5] = r3 - r2;   // far
        frustumPlanes = 6;
    }

    // True unless the box is entirely behind one of the planes
    bool boxInFrustum(glm::vec3 lo, glm::vec3 hi) const {
        for (int i = 0; i < frustumPlanes; i++) {
            const glm::vec4& p = frustum[i];
            // The box corner furthest along the plane normal
            glm::vec3 v(p.x >= 0 ? hi.x : lo.x,
                        p.y >= 0 ? hi.y : lo.y,
                        p.z >= 0 ? hi.z : lo.z);
            if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0)
                return false;
        }
        return true;
    }

    int  width         = 1280;
    int  height        = 720;
    int  drawCallCount = 0;
    int  vertexCount   = 0;
    bool atlasLoaded   = false;
    bool wireframe     = false;
    bool fogEnabled    = false;
    float fogDistance  = 256.0f;
};
