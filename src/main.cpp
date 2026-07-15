#include <iostream>
#include <vector>
#include <functional>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "render/Camera.h"
#include "render/Shader.h"
#include "render/Mesher.h"
#include "chunk/Chunk.h"
#include "chunk/ChunkManager.h"
#include "chunk/ChunkLoader.h"
#include "chunk/StructureGenerator.h"
#include "chunk/PasteSystem.h"
#include "voxel/VoxelRegistry.h"

Camera* gCamera = nullptr;
float lastFrame = 0.0f;

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    (void)window;
    if (gCamera) gCamera->processMouse((float)xpos, (float)ypos);
}

// Rebuild mesh for a single chunk
void rebuildMesh(ChunkManager& world,
                 std::vector<Mesher>& meshers,
                 int cx, int cz) {
    Chunk* chunk = world.getChunk(cx, cz);
    if (!chunk) return;

    glm::vec3 origin = world.chunkOrigin(cx, cz);
    auto sampler = [&](int x, int y, int z) -> uint16_t {
        return world.typeAtWorld(cx, cz, x, y, z);
    };

    meshers[world.index(cx, cz)].cleanup();
    meshers[world.index(cx, cz)].buildMesh(*chunk, sampler, origin);
    world.clearDirty(cx, cz);
}

int main() {
    std::cout << "Starting VoxelRPG...\n";
    std::cout.flush();

    Camera camera;
    gCamera = &camera;

    camera.position = glm::vec3(
        WORLD_SIZE_X * VOXEL_DIM * 0.5f,
        80.0f,
        WORLD_SIZE_Z * VOXEL_DIM * 0.5f
    );

    if (!glfwInit()) {
        std::cout << "GLFW init failed\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        1280, 720, "VoxelRPG", nullptr, nullptr);

    if (!window) {
        std::cout << "Window failed\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "GLAD failed\n";
        glfwTerminate();
        return -1;
    }

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouseCallback);

    glViewport(0, 0, 1280, 720);
    glEnable(GL_DEPTH_TEST);

    // Load registry
    if (!gRegistry.load("data/registry/blocks.toml")) {
        std::cout << "Registry load failed\n";
        glfwTerminate();
        return -1;
    }

    // Load shaders
    Shader shader;
    if (!shader.load("shaders/chunk.vert", "shaders/chunk.frag")) {
        std::cout << "Shader load failed\n";
        glfwTerminate();
        return -1;
    }

    // Generate world
    std::cout << "Generating world...\n";
    std::cout.flush();

    ChunkManager world;

    // Generate and save tree structure files
    std::cout << "Generating tree structures...\n";
    std::cout.flush();

    ChunkPtr oakTree  = generateOakTree();
    ChunkPtr pineTree = generatePineTree();

    ChunkLoader::save(*oakTree,
        "data/structures/oak_tree.vxrp",
        0, 0, 0, ChunkRole::Structure);
    ChunkLoader::save(*pineTree,
        "data/structures/pine_tree.vxrp",
        0, 0, 0, ChunkRole::Structure);

    std::cout << "Tree structures saved\n";

    // Scatter trees across the world
    // Place on top of grass layer (y=32 is first air above grass at y=31)
    struct TreePlacement {
        int cx, cz;     // which chunk
        int lx, lz;     // local position within chunk
        bool isPine;
    };

    TreePlacement trees[] = {
        { 1, 1,  10, 10, false },
        { 1, 1,  40, 40, true  },
        { 2, 2,  15, 20, false },
        { 2, 3,  50, 15, true  },
        { 3, 1,  25, 45, false },
        { 4, 4,  10, 50, false },
        { 4, 2,  55, 30, true  },
        { 5, 5,  20, 20, false },
        { 6, 3,  35, 10, true  },
        { 3, 6,  45, 35, false },
    };

    for (auto& t : trees) {
        const Chunk& src = t.isPine ? *pineTree : *oakTree;
        PasteSystem::paste(
            world, src,
            t.cx, t.cz,
            t.lx, 32, t.lz,  // y=32 = on top of grass
            32, 32,           // source origin = center of structure chunk
            PasteMode::AirIsTransparent
        );

        // Mark affected chunks dirty for mesh rebuild
        world.markDirty(t.cx,     t.cz);
        world.markDirty(t.cx - 1, t.cz);
        world.markDirty(t.cx + 1, t.cz);
        world.markDirty(t.cx,     t.cz - 1);
        world.markDirty(t.cx,     t.cz + 1);
    }

    std::cout << "Trees pasted into world\n";
    std::cout.flush();

    // Build all meshes
    std::vector<Mesher> meshers(WORLD_SIZE_X * WORLD_SIZE_Z);

    int totalVerts = 0;
    for (int cx = 0; cx < WORLD_SIZE_X; cx++)
    for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
        Chunk* chunk = world.getChunk(cx, cz);
        glm::vec3 origin = world.chunkOrigin(cx, cz);

        auto sampler = [&](int x, int y, int z) -> uint16_t {
            return world.typeAtWorld(cx, cz, x, y, z);
        };

        int verts = meshers[world.index(cx, cz)]
            .buildMesh(*chunk, sampler, origin);
        totalVerts += verts;
        world.clearDirty(cx, cz);
    }

    std::cout << "World built: " << totalVerts
              << " vertices across "
              << WORLD_SIZE_X * WORLD_SIZE_Z << " chunks\n";
    std::cout << "WASD to move, Space/Ctrl up/down, ESC to quit\n";
    std::cout.flush();

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        camera.processKeyboard(
            glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS,
            glfwGetKey(window, GLFW_KEY_S)            == GLFW_PRESS,
            glfwGetKey(window, GLFW_KEY_A)            == GLFW_PRESS,
            glfwGetKey(window, GLFW_KEY_D)            == GLFW_PRESS,
            glfwGetKey(window, GLFW_KEY_SPACE)        == GLFW_PRESS,
            glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS,
            deltaTime
        );

        glClearColor(0.4f, 0.6f, 0.9f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("uView",
            camera.getViewMatrix());
        shader.setMat4("uProjection",
            camera.getProjectionMatrix(1280.0f / 720.0f));

        for (int cx = 0; cx < WORLD_SIZE_X; cx++)
        for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
            meshers[world.index(cx, cz)].draw();
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    for (auto& m : meshers) m.cleanup();
    glfwTerminate();
    return 0;
}