#pragma once
#include <glm/glm.hpp>
#include <string>

// Forward declarations
class Camera;
class Chunk;
class UIRenderer;

// Pure interface for all renderer implementations
// OpenGLRenderer implements this for mesh-based rendering
// RaytracingRenderer will implement this for Teardown-style rendering
// Game code never calls OpenGL directly — only this interface

class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Lifecycle
    virtual bool init(int screenW, int screenH) = 0;
    virtual void shutdown() = 0;
    virtual void resize(int screenW, int screenH) = 0;

    // Frame
    virtual void beginFrame() = 0;
    virtual void endFrame()   = 0;

    // Camera
    virtual void setCamera(const Camera& camera,
                           float aspectRatio) = 0;

    // World rendering
    virtual void drawChunk(const Chunk& chunk,
                           glm::vec3 worldPos) = 0;

    // UI — renderer exposes UI drawing surface
    virtual UIRenderer* getUIRenderer() = 0;

    // Resource management
    virtual bool loadAtlas(const std::string& path,
                           int tileSize,
                           int atlasCols) = 0;

    // Settings
    virtual void setWireframe(bool enabled) = 0;
    virtual void setFogEnabled(bool enabled) = 0;
    virtual void setFogDistance(float dist)  = 0;

    // Info
    virtual int  getDrawCalls()    const = 0;
    virtual int  getVertexCount()  const = 0;
    virtual int  getScreenWidth()  const = 0;
    virtual int  getScreenHeight() const = 0;
};
