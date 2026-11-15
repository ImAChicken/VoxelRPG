#pragma once
#include "IRenderer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iostream>
#include "../render/Shader.h"
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
        glUniform3f(locFogColor, FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b);
        glUniform1f(locFogDensity, FOG_DENSITY);
        atlas.bind(0);

        extractFrustum(projMatrix * viewMatrix);
    }

    // Draw one chunk mesh whose corner sits at 'origin'.
    // Chunks completely outside the camera's view are skipped.
    void drawMesh(const ChunkMesh& mesh, glm::vec3 origin) {
        if (!atlasLoaded || mesh.isEmpty()) return;

        glm::vec3 size((float)VOXEL_DIM);
        if (!boxInFrustum(origin, origin + size)) return;

        glUniform3f(locChunkOrigin, origin.x, origin.y, origin.z);

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

    const glm::mat4& getViewMatrix() const { return viewMatrix; }

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

    GLint locView = -1, locProjection = -1, locChunkOrigin = -1;
    GLint locAtlas = -1, locTileUV = -1, locVoxelsPerTile = -1;
    GLint locCamPos = -1, locFogColor = -1, locFogDensity = -1;
    glm::vec3 cameraPos = glm::vec3(0.0f);

    // The six planes of the camera's view volume, each stored
    // as (normal, distance). A point is inside when
    // dot(normal, point) + distance >= 0 for all six.
    glm::vec4 frustum[6];

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
    }

    // True unless the box is entirely behind one of the planes
    bool boxInFrustum(glm::vec3 lo, glm::vec3 hi) const {
        for (const glm::vec4& p : frustum) {
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
