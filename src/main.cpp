#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "render/Camera.h"
#include "render/Shader.h"
#include "render/Mesher.h"
#include "chunk/Chunk.h"
#include "chunk/ChunkGenerator.h"

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

    // Load shaders
    Shader shader;
    if (!shader.load("shaders/chunk.vert", "shaders/chunk.frag")) {
        std::cout << "Shader load failed\n";
        glfwTerminate();
        return -1;
    }

    // Generate chunk and build mesh
    ChunkPtr chunk = generateTestChunk();
    std::cout << "Chunk generated\n";
    std::cout.flush();

    Mesher mesher;
    int vertCount = mesher.buildMesh(*chunk);
    std::cout << "Mesh built: " << vertCount << " vertices\n";
    std::cout.flush();

    std::cout << "Entering game loop — WASD to move, ESC to quit\n";
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

        // Draw chunk
        shader.use();
        shader.setMat4("uView",       camera.getViewMatrix());
        shader.setMat4("uProjection", camera.getProjectionMatrix(1280.0f / 720.0f));

        mesher.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    mesher.cleanup();
    glfwTerminate();
    return 0;
}