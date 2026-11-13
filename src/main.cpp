#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// ── Interfaces ────────────────────────────────────────────
// Game code talks to these, never to OpenGL or a physics
// library directly.
//   swap renderer: change make_unique<OpenGLRenderer>()
//   swap physics:  change make_unique<VoxelPhysics>()
#include "renderer/IRenderer.h"
#include "renderer/OpenGLRenderer.h"
#include "physics/IPhysics.h"
#include "physics/VoxelPhysics.h"
#include "input/InputAction.h"
#include "input/InputManager.h"
#include "entity/Player.h"
#include "entity/PlayerController.h"

// ── Game systems ──────────────────────────────────────────
#include "render/Camera.h"
#include "render/Mesher.h"
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
#include "world/WorldStreamer.h"

constexpr int WIN_W = 1280;
constexpr int WIN_H = 720;

// ── Game state ────────────────────────────────────────────
// MainMenu — menu on screen, cursor visible
// Playing  — in the world, cursor captured
// Paused   — pause menu drawn over the world
enum class GameState {
    MainMenu,
    Playing,
    Paused,
};

// ── Game modes ────────────────────────────────────────────
// Spectator — free fly camera, no physics
// Dev       — test play: a body with gravity and collision
// Survival  — stub
// Combat    — stub
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

// Lets the GLFW text callbacks reach the main menu
MainMenuScreen* gMainMenu = nullptr;

// ── GLFW callbacks ────────────────────────────────────────
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
    // Minimised windows report 0 × 0 — ignore those
    if (gRenderer && w > 0 && h > 0) gRenderer->resize(w, h);
}

// ══════════════════════════════════════════════════════════
int main() {
    std::cout << "Starting VoxelRPG...\n";
    std::cout.flush();

    // ── Window + OpenGL context ───────────────────────────
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

    glfwSetScrollCallback(window,          scrollCallback);
    glfwSetCharCallback(window,            charCallback);
    glfwSetKeyCallback(window,             keyCallback);
    glfwSetFramebufferSizeCallback(window, framebufferCallback);

    // ── Input bindings ────────────────────────────────────
    // Edit data/controls/keyboard.toml to rebind anything
    gInput.loadKeyboard("data/controls/keyboard.toml");

    // ── Data — must load BEFORE the renderer ──────────────
    // The renderer needs the resource pack to find its font
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

    // ── Renderer ──────────────────────────────────────────
    gRenderer = std::make_unique<OpenGLRenderer>();
    if (!gRenderer->init(WIN_W, WIN_H)) {
        std::cerr << "Renderer init failed\n";
        glfwTerminate();
        return -1;
    }

    // The renderer owns the one and only atlas texture
    if (!gRenderer->loadAtlas(
            gResourcePacks.getAtlasPath().string(),
            gResourcePacks.getTileSize(),
            gResourcePacks.getAtlasCols())) {
        std::cerr << "Atlas load failed\n";
        glfwTerminate();
        return -1;
    }

    // The world is drawn through the concrete OpenGL renderer
    auto* glr = static_cast<OpenGLRenderer*>(gRenderer.get());

    // What the mesher needs to know about each block:
    // which atlas tiles it uses and whether it is see-through
    std::shared_ptr<const BlockRenderTable> blockTable =
        BlockRenderTable::build(gRegistry, gResourcePacks);

    // ── Screens ───────────────────────────────────────────
    MainMenuScreen  mainMenu(window);
    PauseMenuScreen pauseMenu(window);
    gMainMenu = &mainMenu;

    // ── Game objects ──────────────────────────────────────
    // Created when a world is opened, destroyed on Save and
    // Quit. Declaration order matters: the streamer's worker
    // threads use 'gen' and 'world', so it is declared last
    // and therefore destroyed first.
    Camera           camera;
    Player           player;
    PlayerController controller;

    // Left-over frame time not yet turned into physics ticks
    float physicsTime = 0.0f;

    // Put the player's body where the camera is (feet below
    // the eyes) and bring it to rest
    auto bodyToCamera = [&]() {
        player.position = camera.position -
            glm::vec3(0.0f, Human::STAND_EYE, 0.0f);
        player.resetMotion();
        player.crouching = false;
        player.flying    = false;
        player.height    = Human::STAND_HEIGHT;
        player.eyeHeight = Human::STAND_EYE;
        controller.reset(player);
        physicsTime = 0.0f;
    };
    std::unique_ptr<TerrainGenerator> gen;
    std::unique_ptr<ChunkManager>     world;
    std::unique_ptr<WorldStreamer>    streamer;

    // ── Open a world (new or saved) ───────────────────────
    // Returns immediately. Nothing is generated here — the
    // streamer brings chunks in on background threads, so the
    // window never freezes however big the world is.
    auto enterWorld = [&](bool isNew,
                          const std::string& name,
                          uint64_t seed) {
        streamer.reset();
        gPhysics.reset();

        if (isNew) {
            std::cout << "Creating world: " << name
                      << " seed=" << seed << "\n";
            gWorldSave.createWorld(name, seed);
        } else {
            std::cout << "Loading world: " << name << "\n";
            gWorldSave.loadWorld(name);
            seed = gWorldSave.getSeed();
        }

        gen   = std::make_unique<TerrainGenerator>((int)seed);
        world = std::make_unique<ChunkManager>();

        // Where each chunk's voxels come from. Runs on worker
        // threads: a chunk the player edited is read from the
        // save folder, everything else is rebuilt from the seed.
        const TerrainGenerator* genPtr = gen.get();
        std::string worldFolder = gWorldSave.getWorldPath();
        auto chunkSource =
            [genPtr, worldFolder](int cx, int cz) -> ChunkPtr {
                auto path = WorldSaveManager::chunkPath(
                    worldFolder, cx, cz);
                if (std::filesystem::exists(path)) {
                    ChunkPtr saved = makeChunk();
                    if (ChunkLoader::load(path, *saved))
                        return saved;
                }
                return genPtr->generateChunk(cx, cz);
            };

        streamer = std::make_unique<WorldStreamer>(
            *world, chunkSource, blockTable);

        gPhysics = std::make_unique<VoxelPhysics>();
        gPhysics->init(*world);

        // Spawn at the centre of the world, high enough to
        // clear the tallest tree (about 85 voxels)
        int spawnX = WORLD_SIZE_X * VOXEL_DIM / 2;
        int spawnZ = WORLD_SIZE_Z * VOXEL_DIM / 2;
        int ground = gen->getSurfaceY(spawnX, spawnZ);

        player = Player();
        player.spawnPoint = glm::vec3(
            (float)spawnX, (float)ground, (float)spawnZ);
        camera = Camera();
        camera.position = glm::vec3(
            (float)spawnX, (float)ground + 105.0f, (float)spawnZ);
        bodyToCamera();

        gState    = GameState::Playing;
        gGameMode = GameMode::Spectator;

        // Capture the cursor first, then reset the mouse
        // delta, so the camera does not jump on the first frame
        glfwSetInputMode(window, GLFW_CURSOR,
                         GLFW_CURSOR_DISABLED);
        camera.firstMouse = true;
        gInput.resetMouseDelta(window);

        std::cout << "World streaming on "
                  << streamer->workerCount()
                  << " worker thread(s)\n";
    };

    // ── Draw the world ────────────────────────────────────
    // Used while playing and behind the pause menu
    auto drawWorld = [&]() {
        if (!world || !streamer) return;

        float aspect = (float)gRenderer->getScreenWidth() /
                       (float)std::max(1,
                           gRenderer->getScreenHeight());
        gRenderer->setCamera(camera, aspect);

        // World pass: depth test + back-face culling ON
        glr->beginWorldDraw();

        glm::ivec2 pc = world->worldToChunk(camera.position);
        for (int cx = pc.x - MESH_DISTANCE;
                 cx <= pc.x + MESH_DISTANCE; cx++)
        for (int cz = pc.y - MESH_DISTANCE;
                 cz <= pc.y + MESH_DISTANCE; cz++) {
            // Not built yet (or outside the world) → skip
            const ChunkMesh* mesh = streamer->getMesh(cx, cz);
            if (!mesh) continue;
            glr->drawMesh(*mesh, world->chunkOrigin(cx, cz));
        }

        // Culling OFF again so the UI draws normally
        glr->endWorldDraw();
    };

    gState = GameState::MainMenu;
    glfwSetInputMode(window, GLFW_CURSOR,
                     GLFW_CURSOR_NORMAL);

    float lastFrame = (float)glfwGetTime();

    // Frame-rate readout in the window title
    float titleTimer  = 0.0f;
    int   titleFrames = 0;
    float worstFrame  = 0.0f;

    std::cout << "Entering main loop\n";
    std::cout.flush();

    // ══════════════════════════════════════════════════════
    // MAIN LOOP
    // ══════════════════════════════════════════════════════
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        float frameTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        // Movement uses a capped delta so one slow frame
        // cannot fling the camera across the map
        float dt = std::min(frameTime, 0.1f);

        // Events first, then input — poll() must run before
        // any isPressed / isHeld check this frame
        glfwPollEvents();
        gInput.poll(window);

        // ── ESC ───────────────────────────────────────────
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

        // ── Shortcuts while playing ───────────────────────
        if (gState == GameState::Playing) {
            // F4 — switch Spectator ↔ Dev (test play).
            // The position carries over: entering test play
            // drops a body in right where the camera is.
            if (gInput.isPressed(
                    InputAction::GamemodeCycle)) {
                int m = ((int)gGameMode + 1) % 2;
                gGameMode = (GameMode)m;
                if (gGameMode == GameMode::Dev) bodyToCamera();
                std::cout << "GameMode: "
                          << (int)gGameMode << "\n";
            }
            // F7 — hot reload the resource pack
            if (gInput.isPressed(InputAction::ReloadPack)) {
                gResourcePacks.load("data/resourcepacks");
                gRenderer->loadAtlas(
                    gResourcePacks.getAtlasPath().string(),
                    gResourcePacks.getTileSize(),
                    gResourcePacks.getAtlasCols());
                // Blocks may use different tiles now —
                // rebuild every mesh in the background
                blockTable = BlockRenderTable::build(
                    gRegistry, gResourcePacks);
                if (streamer)
                    streamer->setRenderTable(blockTable);
                std::cout << "Pack reloaded\n";
            }
        }

        // ── Keep the world streaming in ───────────────────
        // Runs while paused too, so chunks keep arriving
        // behind the pause menu. Costs well under a
        // millisecond: the heavy lifting is on other threads.
        if (world && streamer &&
            gState != GameState::MainMenu) {
            glm::ivec2 pc =
                world->worldToChunk(camera.position);
            streamer->update(pc.x, pc.y);
        }

        gRenderer->beginFrame();

        // ══════════════════════════════════════════════════
        // STATE MACHINE
        // ══════════════════════════════════════════════════
        if (gState == GameState::MainMenu) {

            UIRenderer* ui = gRenderer->getUIRenderer();
            ScreenResult r = mainMenu.update(*ui, dt);

            if (r == ScreenResult::NewWorld) {
                enterWorld(true,
                           mainMenu.getNewWorldName(),
                           mainMenu.getNewWorldSeed());
            } else if (r == ScreenResult::LoadWorld) {
                enterWorld(false,
                           mainMenu.getLoadWorldName(), 0);
            } else if (r == ScreenResult::Quit) {
                glfwSetWindowShouldClose(window, true);
            }

        } else if (gState == GameState::Playing) {

            glm::vec2 mouseDelta = gInput.getMouseDelta();

            if (gGameMode == GameMode::Spectator) {
                // Free fly camera — no physics, no collision
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

                // Keep the body under the camera, so
                // switching to test play starts from here
                player.position = camera.position -
                    glm::vec3(0.0f, Human::STAND_EYE, 0.0f);

            } else if (gGameMode == GameMode::Dev) {
                // ── Test play: a body in the world ────────
                camera.processMouse(
                    mouseDelta.x, mouseDelta.y);

                // 1. Keyboard → what the player wants to do
                controller.readInput(player, camera, dt);

                // 2. Physics, in fixed 1/60 s ticks. A fast
                //    frame may run none, a slow one several.
                physicsTime = std::min(physicsTime + dt, 0.25f);
                while (physicsTime >= PHYSICS_STEP) {
                    controller.beforeTick(player);
                    gPhysics->moveHuman(
                        player, controller.intent, PHYSICS_STEP);
                    controller.afterTick(player);
                    physicsTime -= PHYSICS_STEP;
                }

                // 3. Fell off the edge of the world → back to
                //    the nearest ground inside it
                if (player.fellOutOfWorld) {
                    float maxX = (float)(WORLD_SIZE_X * VOXEL_DIM) - 8.0f;
                    float maxZ = (float)(WORLD_SIZE_Z * VOXEL_DIM) - 8.0f;
                    float x = std::min(std::max(
                        player.position.x, 8.0f), maxX);
                    float z = std::min(std::max(
                        player.position.z, 8.0f), maxZ);
                    glm::vec3 spot;
                    if (gPhysics->findStandingSpot(
                            x, z, Human::RADIUS,
                            Human::STAND_HEIGHT, spot) ||
                        gPhysics->findStandingSpot(
                            player.spawnPoint.x,
                            player.spawnPoint.z,
                            Human::RADIUS,
                            Human::STAND_HEIGHT, spot)) {
                        player.position = spot;
                        player.resetMotion();
                        controller.snap(player);
                        std::cout << "Respawned\n";
                    }
                }

                // 4. Camera to the player's eyes, blended
                //    between the last two ticks
                controller.updateCamera(player, camera,
                    physicsTime / PHYSICS_STEP, dt);
            }

            drawWorld();

        } else if (gState == GameState::Paused) {

            // World stays visible behind the menu
            drawWorld();

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
                // Streamer first: stops the workers and frees
                // the GPU meshes before the world goes away
                streamer.reset();
                gPhysics.reset();
                world.reset();
                gen.reset();
                gState = GameState::MainMenu;
                glfwSetInputMode(window, GLFW_CURSOR,
                                 GLFW_CURSOR_NORMAL);
                glfwSetWindowTitle(window, "VoxelRPG");
                mainMenu.onEnter();
            }
        }

        gRenderer->endFrame();
        glfwSwapBuffers(window);

        // ── Window title: fps and streaming progress ──────
        titleFrames++;
        titleTimer += frameTime;
        worstFrame  = std::max(worstFrame, frameTime);
        if (titleTimer >= 0.5f) {
            if (streamer && gState != GameState::MainMenu) {
                char title[320];
                int n = std::snprintf(title, sizeof(title),
                    "VoxelRPG  |  %.0f fps  (slowest frame %.0f ms)"
                    "  |  %d chunks drawn%s",
                    (float)titleFrames / titleTimer,
                    worstFrame * 1000.0f,
                    streamer->meshCount(),
                    streamer->isIdle() ? "" : "  |  loading...");

                // Test play: live readout for tuning movement
                if (gGameMode == GameMode::Dev && n > 0 &&
                    n < (int)sizeof(title)) {
                    const char* doing =
                        player.waitingForWorld ? "waiting for chunks"
                      : player.flying          ? "flying"
                      : player.freeze != Human::Freeze::None ? "FREEZE"
                      : player.move == Human::Move::Roll     ? "roll"
                      : player.move == Human::Move::Backstep ? "backstep"
                      : player.move == Human::Move::AirDodge ? "air dodge"
                      : !player.grounded       ? "air"
                      : player.crouching       ? "crouch"
                      : player.sprinting       ? "sprint"
                      :                          "ground";
                    float flat = glm::length(glm::vec2(
                        player.velocity.x, player.velocity.z));
                    std::snprintf(title + n, sizeof(title) - n,
                        "  |  TEST PLAY  pos %.0f %.1f %.0f"
                        "  speed %.1f  vy %.1f  %s",
                        player.position.x, player.position.y,
                        player.position.z, flat,
                        player.velocity.y, doing);
                }
                glfwSetWindowTitle(window, title);
            }
            titleTimer  = 0.0f;
            titleFrames = 0;
            worstFrame  = 0.0f;
        }
    }

    // ── Shutdown ──────────────────────────────────────────
    // Closing the window while in a world still saves it
    if (gState != GameState::MainMenu && world)
        gWorldSave.saveDirtyChunks(*world);

    // Everything that owns OpenGL objects goes before
    // glfwTerminate() destroys the context
    streamer.reset();
    gPhysics.reset();
    world.reset();
    gen.reset();
    gRenderer->shutdown();
    gRenderer.reset();
    glfwTerminate();
    return 0;
}
