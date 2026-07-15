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
#include "chunk/TerrainGenerator.h"
#include "voxel/VoxelRegistry.h"

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

    camera.position = glm::vec3(
        WORLD_SIZE_X * VOXEL_DIM * 0.5f,
        60.0f,
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

    // Generate world with seed
    const int SEED = 42;
    std::cout << "Generating world (seed " << SEED << ")...\n";
    std::cout.flush();

    ChunkManager world(SEED);
    TerrainGenerator gen(SEED);

    // Generate tree structure files
    ChunkPtr oakTree  = generateOakTree();
    ChunkPtr pineTree = generatePineTree();

    ChunkLoader::save(*oakTree,
        "data/structures/oak_tree.vxrp",
        0, 0, 0, ChunkRole::Structure);
    ChunkLoader::save(*pineTree,
        "data/structures/pine_tree.vxrp",
        0, 0, 0, ChunkRole::Structure);

    // Scatter trees based on noise
    std::cout << "Placing trees...\n";
    std::cout.flush();

    int treeCount = 0;
    for (int cx = 0; cx < WORLD_SIZE_X; cx++)
    for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
        // Sample every 8 voxels within the chunk
        for (int lx = 4; lx < VOXEL_DIM; lx += 8)
        for (int lz = 4; lz < VOXEL_DIM; lz += 8) {
            int wx = cx * VOXEL_DIM + lx;
            int wz = cz * VOXEL_DIM + lz;

            if (gen.shouldPlaceTree(wx, wz)) {
                int surfY = gen.getSurfaceY(wx, wz);

                // Pick oak or pine based on position
                bool isPine = ((wx + wz) % 3 == 0);
                const Chunk& src = isPine ? *pineTree : *oakTree;

                PasteSystem::paste(
                    world, src,
                    cx, cz,
                    lx, surfY, lz,
                    32, 32,
                    PasteMode::AirIsTransparent
                );

                // Mark this chunk and neighbours dirty
                world.markDirty(cx,     cz);
                world.markDirty(cx - 1, cz);
                world.markDirty(cx + 1, cz);
                world.markDirty(cx,     cz - 1);
                world.markDirty(cx,     cz + 1);

                treeCount++;
            }
        }
    }

    std::cout << "Placed " << treeCount << " trees\n";
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