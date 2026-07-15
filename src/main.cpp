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
#include "voxel/VoxelRegistry.h"
#include "lz4/lz4.h"

Camera* gCamera = nullptr;
float lastFrame = 0.0f;

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    (void)window;
    if (gCamera) gCamera->processMouse((float)xpos, (float)ypos);
}

int main() {
    std::cout << "Starting VoxelRPG...\n";
    std::cout.flush();

    Camera camera;
    gCamera = &camera;

    // Start above center of 8x8 grid
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

    // Load block registry
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

    // Build one mesh per chunk
    std::vector<Mesher> meshers(WORLD_SIZE_X * WORLD_SIZE_Z);

    int totalVerts = 0;
    for (int cx = 0; cx < WORLD_SIZE_X; cx++)
    for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
        Chunk* chunk = world.getChunk(cx, cz);
        glm::vec3 origin = world.chunkOrigin(cx, cz);

        // Neighbour sampler for seam stitching
        auto sampler = [&](int x, int y, int z) -> uint16_t {
            return world.typeAtWorld(cx, cz, x, y, z);
        };

        int verts = meshers[world.index(cx, cz)]
            .buildMesh(*chunk, sampler, origin);
        totalVerts += verts;
    }

    std::cout << "World built: " << totalVerts
              << " total vertices across "
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

        // Draw all chunk meshes
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