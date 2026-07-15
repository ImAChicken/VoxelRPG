#include <iostream>
#include <vector>
#include <memory>
#include <functional>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// Interfaces
#include "renderer/IRenderer.h"
#include "renderer/OpenGLRenderer.h"
#include "physics/IPhysics.h"
#include "physics/VoxelPhysics.h"
#include "input/InputAction.h"
#include "input/InputManager.h"
#include "player/Player.h"

// Game systems
#include "render/Camera.h"
#include "render/Mesher.h"
#include "render/AtlasTexture.h"
#include "chunk/Chunk.h"
#include "chunk/ChunkManager.h"
#include "chunk/ChunkLoader.h"
#include "chunk/TerrainGenerator.h"
#include "voxel/VoxelRegistry.h"
#include "render/ResourcePack.h"
#include "ui/UIRenderer.h"
#include "ui/MainMenuScreen.h"
#include "ui/PauseMenuScreen.h"
#include "world/WorldSaveManager.h"

constexpr int WIN_W = 1280;
constexpr int WIN_H = 720;

enum class GameState {
    MainMenu,
    Playing,
    Paused,
};

enum class GameMode {
    Spectator = 0,
    Dev       = 1,
    Survival  = 2,
    Combat    = 3,
};

GameState gState    = GameState::MainMenu;
GameMode  gGameMode = GameMode::Spectator;

std::unique_ptr<IRenderer> gRenderer;
std::unique_ptr<IPhysics>  gPhysics;

MainMenuScreen* gMainMenu = nullptr;

void mouseCallback(GLFWwindow* window,
                   double xpos, double ypos) {
    (void)window; (void)xpos; (void)ypos;
}

void scrollCallback(GLFWwindow* window,
                    double xoffset, double yoffset) {
    (void)window; (void)xoffset;
    gInput.onScroll((float)yoffset);
}

void charCallback(GLFWwindow* window,
                  unsigned int codepoint) {
    (void)window;
    gInput.onChar(codepoint);
    if (gState == GameState::MainMenu && gMainMenu)
        gMainMenu->onChar(codepoint);
}

void keyCallback(GLFWwindow* window, int key,
                 int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (gState == GameState::MainMenu && gMainMenu)
            gMainMenu->onKey(key);
    }
}

void framebufferCallback(GLFWwindow* window,
                         int w, int h) {
    (void)window;
    if (gRenderer) gRenderer->resize(w, h);
}

void buildAllMeshes(ChunkManager& world,
                    std::vector<Mesher>& meshers,
                    AtlasTexture& atlas) {
    for (int cx = 0; cx < WORLD_SIZE_X; cx++)
    for (int cz = 0; cz < WORLD_SIZE_Z; cz++) {
        Chunk* chunk = world.getChunk(cx, cz);
        if (!chunk) continue;

        glm::vec3 origin = world.chunkOrigin(cx, cz);
        auto sampler = [&](int x, int y, int z) -> uint16_t {
            return world.typeAtWorld(cx, cz, x, y, z);
        };

        int idx = world.index(cx, cz);
        meshers[idx].cleanup();
        meshers[idx].buildMesh(*chunk, sampler, origin, atlas);
        world.clearDirty(cx, cz);
    }
}

int main() {
    std::cout << "Starting VoxelRPG...\n";
    std::cout.flush();

    // ── GLFW ─────────────────────────────────────────────
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE,
                   GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        WIN_W, WIN_H, "VoxelRPG", nullptr, nullptr);
    if (!window) {
        std::cerr << "Window creation failed\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader(
            (GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD init failed\n";
        glfwTerminate();
        return -1;
    }

    glfwSetCursorPosCallback(window,       mouseCallback);
    glfwSetScrollCallback(window,          scrollCallback);
    glfwSetCharCallback(window,            charCallback);
    glfwSetKeyCallback(window,             keyCallback);
    glfwSetFramebufferSizeCallback(window, framebufferCallback);

    // ── Input ─────────────────────────────────────────────
    gInput.loadKeyboard("data/controls/keyboard.toml");

    // ── Data — load BEFORE renderer so font path is ready
    if (!gRegistry.load("data/registry/blocks.toml")) {
        std::cerr << "Registry load failed\n";
        glfwTerminate();
        return -1;
    }

    if (!gResourcePacks.load("data/resourcepacks")) {
        std::cerr << "Resource pack load failed\n";
        glfwTerminate();
        return -1;
    }

    // ── Renderer — init AFTER resource packs loaded
    gRenderer = std::make_unique<OpenGLRenderer>();
    if (!gRenderer->init(WIN_W, WIN_H)) {
        std::cerr << "Renderer init failed\n";
        glfwTerminate();
        return -1;
    }

    // ── Atlas
    AtlasTexture atlas;
    if (!atlas.load(
            gResourcePacks.getAtlasPath().string(),
            gResourcePacks.getTileSize(),
            gResourcePacks.getAtlasCols())) {
        std::cerr << "Atlas load failed\n";
        glfwTerminate();
        return -1;
    }

    // Also register atlas with renderer
    gRenderer->loadAtlas(
        gResourcePacks.getAtlasPath().string(),
        gResourcePacks.getTileSize(),
        gResourcePacks.getAtlasCols());

    // ── Screens ───────────────────────────────────────────
    MainMenuScreen  mainMenu(window);
    PauseMenuScreen pauseMenu(window);
    gMainMenu = &mainMenu;

    // ── Game objects ──────────────────────────────────────
    Camera camera;
    std::unique_ptr<ChunkManager>     world;
    std::unique_ptr<TerrainGenerator> gen;
    std::vector<Mesher>               meshers;
    Player                            player;

    gState = GameState::MainMenu;
    glfwSetInputMode(window, GLFW_CURSOR,
                     GLFW_CURSOR_NORMAL);

    float lastFrame = 0.0f;
    std::cout << "Entering main loop\n";

    // ── Main loop ─────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float dt           = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        glfwPollEvents();
        gInput.poll(window);

        // ESC
        if (gInput.isPressed(InputAction::Pause)) {
            if (gState == GameState::Playing) {
                gState = GameState::Paused;
                glfwSetInputMode(window, GLFW_CURSOR,
                                 GLFW_CURSOR_NORMAL);
                pauseMenu.onEnter();
            } else if (gState == GameState::MainMenu) {
                glfwSetWindowShouldClose(window, true);
            }
        }

        // Playing shortcuts
        if (gState == GameState::Playing) {
            if (gInput.isPressed(
                    InputAction::GamemodeCycle)) {
                int m = ((int)gGameMode + 1) % 2;
                gGameMode = (GameMode)m;
                std::cout << "GameMode: "
                          << (int)gGameMode << "\n";
            }
            if (gInput.isPressed(InputAction::ToggleFly) &&
                gGameMode == GameMode::Dev) {
                player.isFlying = !player.isFlying;
                std::cout << "Fly: "
                          << (player.isFlying
                              ? "ON" : "OFF") << "\n";
            }
            if (gInput.isPressed(InputAction::ReloadPack)) {
                gResourcePacks.load("data/resourcepacks");
                atlas.load(
                    gResourcePacks.getAtlasPath().string(),
                    gResourcePacks.getTileSize(),
                    gResourcePacks.getAtlasCols());
                gRenderer->loadAtlas(
                    gResourcePacks.getAtlasPath().string(),
                    gResourcePacks.getTileSize(),
                    gResourcePacks.getAtlasCols());
                std::cout << "Pack reloaded\n";
            }
        }

        gRenderer->beginFrame();

        // ── State machine ─────────────────────────────────
        if (gState == GameState::MainMenu) {

            UIRenderer* ui = gRenderer->getUIRenderer();
            ScreenResult r = mainMenu.update(*ui, dt);

            if (r == ScreenResult::NewWorld) {
                std::string name = mainMenu.getNewWorldName();
                uint64_t    seed = mainMenu.getNewWorldSeed();

                std::cout << "Creating world: "
                          << name << " seed=" << seed << "\n";

                gWorldSave.createWorld(name, seed);
                gen   = std::make_unique<TerrainGenerator>(
                            (int)seed);
                world = std::make_unique<ChunkManager>(
                            (int)seed);

                gPhysics = std::make_unique<VoxelPhysics>();
                gPhysics->init(*world);

                // Mark all dirty so trees save
                for (int cx = 0; cx < WORLD_SIZE_X; cx++)
                for (int cz = 0; cz < WORLD_SIZE_Z; cz++)
                    world->markDirty(cx, cz);

                meshers.clear();
                meshers.resize(
                    WORLD_SIZE_X * WORLD_SIZE_Z);
                buildAllMeshes(*world, meshers, atlas);

                player = Player();
                player.position = glm::vec3(
                    WORLD_SIZE_X * VOXEL_DIM * 0.5f,
                    80.0f,
                    WORLD_SIZE_Z * VOXEL_DIM * 0.5f);

                camera.position   = player.getEyePosition();
                gState    = GameState::Playing;
                gGameMode = GameMode::Spectator;
                glfwSetInputMode(window, GLFW_CURSOR,
                                GLFW_CURSOR_DISABLED);
                camera.firstMouse = true;
                gInput.resetMouseDelta(window);

            } else if (r == ScreenResult::LoadWorld) {
                std::string name = mainMenu.getLoadWorldName();
                std::cout << "Loading world: " << name << "\n";

                gWorldSave.loadWorld(name);
                uint64_t seed = gWorldSave.getSeed();

                gen   = std::make_unique<TerrainGenerator>(
                            (int)seed);
                world = std::make_unique<ChunkManager>(
                            (int)seed);
                gWorldSave.initChunks(*world, *gen);

                gPhysics = std::make_unique<VoxelPhysics>();
                gPhysics->init(*world);

                meshers.clear();
                meshers.resize(
                    WORLD_SIZE_X * WORLD_SIZE_Z);
                buildAllMeshes(*world, meshers, atlas);

                player = Player();
                player.position = glm::vec3(
                    WORLD_SIZE_X * VOXEL_DIM * 0.5f,
                    80.0f,
                    WORLD_SIZE_Z * VOXEL_DIM * 0.5f);

                camera.position   = player.getEyePosition();
                gState    = GameState::Playing;
                gGameMode = GameMode::Spectator;
                glfwSetInputMode(window, GLFW_CURSOR,
                                GLFW_CURSOR_DISABLED);
                camera.firstMouse = true;
                gInput.resetMouseDelta(window);

            } else if (r == ScreenResult::Quit) {
                glfwSetWindowShouldClose(window, true);
            }

        } else if (gState == GameState::Playing) {

            glm::vec2 mouseDelta = gInput.getMouseDelta();

            if (gGameMode == GameMode::Spectator) {
                camera.processMouse(
                    mouseDelta.x, mouseDelta.y);

                float speed = 30.0f;
                if (gInput.isHeld(InputAction::Sprint))
                    speed = 80.0f;

                glm::vec3 right = glm::normalize(
                    glm::cross(camera.front, camera.up));

                if (gInput.isHeld(InputAction::MoveForward))
                    camera.position +=
                        camera.front * speed * dt;
                if (gInput.isHeld(InputAction::MoveBackward))
                    camera.position -=
                        camera.front * speed * dt;
                if (gInput.isHeld(InputAction::StrafeLeft))
                    camera.position -= right * speed * dt;
                if (gInput.isHeld(InputAction::StrafeRight))
                    camera.position += right * speed * dt;
                if (gInput.isHeld(InputAction::Jump))
                    camera.position +=
                        camera.up * speed * dt;
                if (gInput.isHeld(InputAction::Crouch))
                    camera.position -=
                        camera.up * speed * dt;

            } else if (gGameMode == GameMode::Dev) {
                camera.processMouse(
                    mouseDelta.x, mouseDelta.y);
                // TODO D2 — gPhysics->movePlayer(player, dt)
                camera.position = player.getEyePosition();
            }

            gRenderer->setCamera(
                camera, (float)WIN_W / (float)WIN_H);

            auto* glr = static_cast<OpenGLRenderer*>(
                gRenderer.get());
            for (int cx = 0; cx < WORLD_SIZE_X; cx++)
            for (int cz = 0; cz < WORLD_SIZE_Z; cz++)
                glr->drawMesh(
                    meshers[world->index(cx, cz)]);

        } else if (gState == GameState::Paused) {

            gRenderer->setCamera(
                camera, (float)WIN_W / (float)WIN_H);

            auto* glr = static_cast<OpenGLRenderer*>(
                gRenderer.get());
            for (int cx = 0; cx < WORLD_SIZE_X; cx++)
            for (int cz = 0; cz < WORLD_SIZE_Z; cz++)
                glr->drawMesh(
                    meshers[world->index(cx, cz)]);

            UIRenderer* ui = gRenderer->getUIRenderer();
            ScreenResult r = pauseMenu.update(*ui, dt);

            if (r == ScreenResult::ResumeGame) {
                gState = GameState::Playing;
                glfwSetInputMode(window, GLFW_CURSOR,
                                GLFW_CURSOR_DISABLED);
                camera.firstMouse = true;
                gInput.resetMouseDelta(window);

            } else if (r == ScreenResult::SaveAndQuit) {
                if (world)
                    gWorldSave.saveDirtyChunks(*world);
                world.reset();
                meshers.clear();
                gPhysics.reset();
                gState = GameState::MainMenu;
                glfwSetInputMode(window, GLFW_CURSOR,
                                 GLFW_CURSOR_NORMAL);
                mainMenu.onEnter();
            }
        }

        gRenderer->endFrame();
        glfwSwapBuffers(window);
    }

    // Cleanup
    if (gState == GameState::Playing && world)
        gWorldSave.saveDirtyChunks(*world);

    for (auto& m : meshers) m.cleanup();
    atlas.cleanup();
    gRenderer->shutdown();
    glfwTerminate();
    return 0;
}