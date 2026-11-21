#pragma once
#include "IRenderer.h"
#include <glad/glad.h>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iostream>
#include "../render/Shader.h"
#include "../render/Sky.h"
#include "../render/TerrainLight.h"
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
        facesSkipped   = 0;
        occSkipped = 0;

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
            terrainlight::bindFrame(p);
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

        const int skipped = (int)mesh.opaqueVertices() - (int)mesh.drawOpaque(cameraPos - origin);
        facesSkipped += skipped;

        // Leaves: two-sided, so the far side of a canopy
        // shows through the gaps in the near side
        if (mesh.hasCutout()) {
            glDisable(GL_CULL_FACE);
            mesh.drawCutout();
            glEnable(GL_CULL_FACE);
        }

        drawCallCount++;
        vertexCount += (int)mesh.vertexCount() - skipped;
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
    int getFacesSkipped() const { return facesSkipped; }
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
    // ── Occlusion culling (gSettings.occlusion) ───────────
    // Each section in view gets a query after the world pass: its box
    // drawn against the depth buffer (nothing written), "any sample
    // passed?". The answer is read a frame or two later, never waited
    // for; a section none of whose box showed isn't drawn until a query
    // says it shows again (a frame late: a section coming out from
    // behind a hill can pop in for a frame). The one round the camera,
    // and one with no answer yet, always draw.
    //   occludedSection   before drawing: skip it?
    //   queueOcclusion    it's in view: test it after the pass
    //   runOcclusion      after the world pass: the queries
    struct OccQuery { GLuint q = 0; bool visible = true, pending = false; uint32_t used = 0; };
    std::unordered_map<uint64_t, OccQuery> occ;
    std::vector<std::pair<uint64_t, glm::vec3>> occTests;
    uint32_t occFrame = 0;
    int occSkipped = 0;
    static uint64_t sectionKey(int cx, int sy, int cz) {
        return ((uint64_t)(uint32_t)cx << 40) ^ ((uint64_t)(uint32_t)cz << 8) ^ (uint64_t)(uint32_t)sy;
    }
    bool sectionInView(glm::vec3 origin) const { return boxInFrustum(origin, origin + glm::vec3((float)VOXEL_DIM)); }
    bool occludedSection(uint64_t key, glm::vec3 origin) {
        auto it = occ.find(key);
        if (it == occ.end()) return false;
        OccQuery& o = it->second;
        if (o.pending) {
            GLuint ready = 0;
            glGetQueryObjectuiv(o.q, GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready) { GLuint any = 0; glGetQueryObjectuiv(o.q, GL_QUERY_RESULT, &any); o.visible = any != 0; o.pending = false; }
        }
        // round the camera (its near plane can cut the box): always
        const glm::vec3 lo = origin - glm::vec3(4.0f), hi = origin + glm::vec3((float)VOXEL_DIM + 4.0f);
        if (glm::all(glm::greaterThanEqual(cameraPos, lo)) && glm::all(glm::lessThanEqual(cameraPos, hi))) return false;
        if (!o.visible) occSkipped++;
        return !o.visible;
    }
    void queueOcclusion(uint64_t key, glm::vec3 origin) { occTests.push_back({ key, origin }); }
    void runOcclusion(const glm::mat4& viewProj) {
        occFrame++;
        if (occTests.empty()) return;
        if (!occProg) makeOccProgram();
        glUseProgram(occProg);
        glBindVertexArray(occVao);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        GLint lm = glGetUniformLocation(occProg, "uMVP");
        for (auto& [key, origin] : occTests) {
            OccQuery& o = occ[key];
            o.used = occFrame;
            if (o.pending) continue;                     // (its last one not answered yet)
            if (!o.q) glGenQueries(1, &o.q);
            glm::mat4 m = viewProj * glm::translate(glm::mat4(1.0f), origin - glm::vec3(0.5f)) * glm::scale(glm::mat4(1.0f), glm::vec3((float)VOXEL_DIM + 1.0f));
            glUniformMatrix4fv(lm, 1, GL_FALSE, glm::value_ptr(m));
            glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, o.q);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glEndQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE);
            o.pending = true;
        }
        occTests.clear();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glBindVertexArray(0);
        // sections not in view a while: their queries go
        if (occFrame % 120 == 0)
            for (auto it = occ.begin(); it != occ.end();)
                if (occFrame - it->second.used > 600) { if (it->second.q) glDeleteQueries(1, &it->second.q); it = occ.erase(it); } else ++it;
    }
    // ── Render scale (gSettings.renderScale) ─────────────
    // The 3D scene is drawn into an offscreen buffer the window's size,
    // using only a part of it (scale × scale), then stretched over the
    // window before the HUD, which stays sharp. Fewer pixels to shade on
    // a weak GPU. "Auto" times the scene on the GPU and steers the scale
    // between AUTO_MIN and 1 to fit a 60 fps frame.
    //   beginScene(scale)   before the scene: draw into the buffer (1: the window itself)
    //   endScene()          after it: onto the window
    //   sceneW/H            the part drawn into (the window's size at 1)
    //   targetW/H           the whole buffer (for passes that size their own)
    static constexpr float AUTO_MIN = 0.5f, AUTO_BUDGET_MS = 13.0f, AUTO_SLACK_MS = 9.0f;
    float autoScale = 1.0f;
    int sceneW() const { return sceneActive ? sw : width; }
    int sceneH() const { return sceneActive ? sh : height; }
    int targetW() const { return sceneActive ? tw : width; }
    int targetH() const { return sceneActive ? th : height; }
    // the scale for a setting (0 full, 1 auto, 2.. fixed)
    float scaleFor(int setting) const {
        static const float fixed[] = { 1.0f, 1.0f, 0.85f, 0.7f, 0.5f };
        if (setting == 1) return autoScale;
        return fixed[std::clamp(setting, 0, 4)];
    }
    void beginScene(float scale, bool timeIt) {
        timing = timeIt;
        if (timing) {
            if (!sceneQ[0]) glGenQueries(4, sceneQ);
            if (!sceneQBusy[qAt]) { glBeginQuery(GL_TIME_ELAPSED, sceneQ[qAt]); sceneQBusy[qAt] = true; qOpen = true; }
            else qOpen = false;
        }
        sceneActive = scale < 0.999f;
        if (!sceneActive) return;
        if (!sceneFbo || tw != width || th != height) makeSceneTarget();
        sw = std::max(1, (int)std::round(width * scale));
        sh = std::max(1, (int)std::round(height * scale));
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glViewport(0, 0, sw, sh);
        glClearColor(FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    void endScene() {
        if (sceneActive) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, sw, sh, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            sceneActive = false;
        }
        if (timing) {
            if (qOpen) glEndQuery(GL_TIME_ELAPSED);
            qAt = (qAt + 1) % 4;
            // the oldest answer (three frames ago): never waited for
            if (sceneQBusy[qAt]) {
                GLuint ready = 0;
                glGetQueryObjectuiv(sceneQ[qAt], GL_QUERY_RESULT_AVAILABLE, &ready);
                if (ready) {
                    GLuint64 ns = 0; glGetQueryObjectui64v(sceneQ[qAt], GL_QUERY_RESULT, &ns);
                    sceneQBusy[qAt] = false;
                    steer((float)ns / 1.0e6f);
                }
            }
        }
    }
    float sceneMs() const { return lastSceneMs; }

    // Everything drawn again (the world changed under it: a new world, a teleport)
    void resetOcclusion() { for (auto& [k, o] : occ) { o.visible = true; } }
    // Called before the sections: a camera that jumped (a teleport, a
    // portal) starts again with everything shown
    glm::vec3 occCam{ 0.0f };
    void occlusionFrom(glm::vec3 cam) {
        if (glm::length(cam - occCam) > 12.0f) resetOcclusion();
        occCam = cam;
    }
    int occludedLastFrame() const { return occSkipped; }   // (read at the frame's end)

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
    int  facesSkipped  = 0;       // solid vertices not drawn this frame: their faces all faced away (ChunkMesh::drawOpaque)
    GLuint sceneFbo = 0, sceneColor = 0, sceneDepth = 0;
    int    tw = 0, th = 0, sw = 0, sh = 0;
    bool   sceneActive = false, timing = false, qOpen = false;
    GLuint sceneQ[4] = {};
    bool   sceneQBusy[4] = {};
    int    qAt = 0, steerFrames = 0;
    float  steerSum = 0.0f, lastSceneMs = 0.0f;
    void makeSceneTarget() {
        if (sceneFbo) { glDeleteFramebuffers(1, &sceneFbo); glDeleteRenderbuffers(1, &sceneColor); glDeleteRenderbuffers(1, &sceneDepth); }
        tw = width; th = height;
        glGenRenderbuffers(1, &sceneColor);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneColor);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, tw, th);
        glGenRenderbuffers(1, &sceneDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, tw, th);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glGenFramebuffers(1, &sceneFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColor);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "OpenGLRenderer: scene buffer incomplete\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    // Auto scale: half a second of scene times, then a small step
    void steer(float ms) {
        lastSceneMs = ms;
        steerSum += ms; steerFrames++;
        if (steerFrames < 30) return;
        // (the time is for the scale it was drawn at: per pixel, roughly)
        const float avg = steerSum / (float)steerFrames;
        steerSum = 0.0f; steerFrames = 0;
        if (avg > AUTO_BUDGET_MS)     autoScale = std::max(AUTO_MIN, autoScale - (avg > AUTO_BUDGET_MS * 1.4f ? 0.1f : 0.05f));
        else if (avg < AUTO_SLACK_MS) autoScale = std::min(1.0f, autoScale + 0.05f);
    }
    GLuint occProg = 0, occVao = 0, occVbo = 0;
    void makeOccProgram() {
        auto compile = [](GLenum type, const char* src) {
            GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s); return s;
        };
        GLuint vs = compile(GL_VERTEX_SHADER, "#version 460 core\nlayout(location=0) in vec3 aPos; uniform mat4 uMVP;\nvoid main(){ gl_Position = uMVP * vec4(aPos, 1.0); }\n");
        GLuint fs = compile(GL_FRAGMENT_SHADER, "#version 460 core\nout vec4 o; void main(){ o = vec4(1.0); }\n");
        occProg = glCreateProgram(); glAttachShader(occProg, vs); glAttachShader(occProg, fs); glLinkProgram(occProg);
        glDeleteShader(vs); glDeleteShader(fs);
        // a unit cube's 12 triangles
        const float c[8][3] = { {0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1} };
        const int f[36] = { 0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1, 3,2,6, 3,6,7, 0,3,7, 0,7,4, 1,5,6, 1,6,2 };
        std::vector<float> v;
        for (int i : f) { v.push_back(c[i][0]); v.push_back(c[i][1]); v.push_back(c[i][2]); }
        glGenVertexArrays(1, &occVao); glGenBuffers(1, &occVbo);
        glBindVertexArray(occVao); glBindBuffer(GL_ARRAY_BUFFER, occVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr); glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }
    bool atlasLoaded   = false;
    bool wireframe     = false;
    bool fogEnabled    = false;
    float fogDistance  = 256.0f;
};
