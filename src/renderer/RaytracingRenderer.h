#pragma once
#include "IRenderer.h"
#include "../ui/UIRenderer.h"
#include <iostream>

// Stub for future raytracing renderer
// Teardown-style voxel raytracing
// Implements same IRenderer interface as OpenGLRenderer
// Swap to this renderer by changing one line in main.cpp:
//
//   renderer = std::make_unique<RaytracingRenderer>();
//
// Zero other changes needed anywhere in the codebase

class RaytracingRenderer : public IRenderer {
public:
    bool init(int screenW, int screenH) override {
        width  = screenW;
        height = screenH;
        std::cout << "RaytracingRenderer: STUB — not implemented\n";
        std::cout << "Falling back to OpenGLRenderer\n";
        // TODO Phase A future:
        //   - Initialize compute shaders
        //   - Build BVH acceleration structure
        //   - Set up GBuffer
        //   - Initialize light chunk sampler
        return false; // returns false until implemented
    }

    void shutdown() override {}
    void resize(int, int) override {}
    void beginFrame() override {}
    void endFrame() override {}
    void setCamera(const Camera&, float) override {}
    void drawChunk(const Chunk&, glm::vec3) override {}

    UIRenderer* getUIRenderer() override {
        return &uiRenderer;
    }

    bool loadAtlas(const std::string&, int, int) override {
        return false;
    }

    void setWireframe(bool) override {}
    void setFogEnabled(bool) override {}
    void setFogDistance(float) override {}

    int getDrawCalls()    const override { return 0; }
    int getVertexCount()  const override { return 0; }
    int getScreenWidth()  const override { return width; }
    int getScreenHeight() const override { return height; }

private:
    UIRenderer uiRenderer;
    int width  = 1280;
    int height = 720;
};
