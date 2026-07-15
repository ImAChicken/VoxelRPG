#pragma once
#include "IRenderer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <iostream>
#include "../render/Shader.h"
#include "../render/Mesher.h"
#include "../render/AtlasTexture.h"
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

        if (!uiRenderer.init(screenW, screenH)) {
            std::cerr << "OpenGLRenderer: UI init failed\n";
            return false;
        }

        if (!uiRenderer.loadFont()) {
            std::cerr << "OpenGLRenderer: font load failed\n";
            return false;
        }

        std::cout << "OpenGLRenderer initialized "
                  << screenW << "x" << screenH << "\n";
        return true;
    }

    void shutdown() override {
        atlas.cleanup();
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

        glClearColor(0.4f, 0.6f, 0.9f, 1.0f);
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
    }

    void drawChunk(const Chunk& chunk,
                   glm::vec3 worldPos) override {
        // Mesher lookup handled externally via
        // getMesherForChunk — renderer draws
        // pre-built meshes passed to it
        // This signature kept for interface compliance
        // See drawMesh below for actual draw path
        (void)chunk; (void)worldPos;
    }

    // Draw a pre-built mesher directly
    // This is the actual draw path used by main loop
    void drawMesh(Mesher& mesher) {
        if (!atlasLoaded) return;

        shader.use();
        shader.setMat4("uView",       viewMatrix);
        shader.setMat4("uProjection", projMatrix);
        shader.setInt("uAtlas",       0);
        shader.setInt("uUseTexture",  1);
        atlas.bind(0);

        mesher.draw();
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

private:
    Shader      shader;
    AtlasTexture atlas;
    UIRenderer  uiRenderer;

    glm::mat4   viewMatrix;
    glm::mat4   projMatrix;

    int  width         = 1280;
    int  height        = 720;
    int  drawCallCount = 0;
    int  vertexCount   = 0;
    bool atlasLoaded   = false;
    bool wireframe     = false;
    bool fogEnabled    = false;
    float fogDistance  = 256.0f;
};
