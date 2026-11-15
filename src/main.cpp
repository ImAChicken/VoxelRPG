#include <algorithm>
#include <cmath>
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
#include "game/GameMode.h"
#include "game/ItemRegistry.h"
#include "game/DevTools.h"
#include "game/Commands.h"
#include "game/PlayerInteraction.h"

// ── Game systems ──────────────────────────────────────────
#include "render/Camera.h"
#include "render/Mesher.h"
#include "chunk/Chunk.h"
#include "chunk/ChunkManager.h"
#include "chunk/ChunkLoader.h"
#include "render/FarTerrain.h"
#include "chunk/TerrainGenerator.h"
#include "voxel/VoxelRegistry.h"
#include "render/ResourcePack.h"
#include "ui/UIRenderer.h"
#include "ui/MainMenuScreen.h"
#include "ui/PauseMenuScreen.h"
#include "ui/HudScreen.h"
#include "ui/DevMenuScreen.h"
#include "ui/ConsoleScreen.h"
#include "ui/ControlsScreen.h"
#include "ui/SettingsScreen.h"
#include "game/Settings.h"
#include "game/StructureRegistry.h"
#include "game/BuildTool.h"
#include "ui/BuildMenuScreen.h"
#include "world/StructureManager.h"
#include "worldgen/WorldType.h"
#include "world/WorldSaveManager.h"
#include "world/WorldEditor.h"
#include "world/WorldStreamer.h"

constexpr int WIN_W = 1280;
constexpr int WIN_H = 720;

// ── Game state ────────────────────────────────────────────
// MainMenu — menu on screen, cursor visible
// Playing  — in the world, cursor captured
// Paused   — pause menu drawn over the world
// DevMenu  — dev menu drawn over the world (from Paused)
// Console  — typing a command; the world keeps drawing
// Controls — key bindings screen (from Paused)
// Settings — settings screen (from Paused)
enum class GameState {
    MainMenu,
    Playing,
    Paused,
    DevMenu,
    Console,
    Controls,
    Settings,
    BuildMenu,
};

// Seconds between automatic saves while in a world
constexpr float AUTOSAVE_SECONDS = 180.0f;

// Game modes are per player, not global — see
// game/GameMode.h and Player::mode.

GameState gState    = GameState::MainMenu;

std::unique_ptr<IRenderer> gRenderer;
std::unique_ptr<IPhysics>  gPhysics;

// Lets the GLFW text callbacks reach the main menu and
// the console
MainMenuScreen* gMainMenu = nullptr;
Console*        gConsole  = nullptr;
ControlsScreen* gControls = nullptr;

// Lines typed into the console, waiting to be run by the
// main loop (the key callback only collects them)
std::vector<std::string> gConsoleLines;

// ── GLFW callbacks ────────────────────────────────────────
void scrollCallback(GLFWwindow* window,
                    double xoffset, double yoffset) {
    (void)window; (void)xoffset;
    gInput.onScroll((float)yoffset);
    if (gState == GameState::Controls && gControls)
        gControls->onScroll((float)yoffset);
}

void charCallback(GLFWwindow* window,
                  unsigned int codepoint) {
    (void)window;
    gInput.onChar(codepoint);
    if (gState == GameState::MainMenu && gMainMenu)
        gMainMenu->onChar(codepoint);
    if (gState == GameState::Console && gConsole)
        gConsole->onChar(codepoint);
}

void keyCallback(GLFWwindow* window, int key,
                 int scancode, int action, int mods) {
    (void)window; (void)scancode;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (gState == GameState::MainMenu && gMainMenu)
            gMainMenu->onKey(key);
        if (gState == GameState::Controls && gControls &&
            action == GLFW_PRESS)
            gControls->onKey(key);
        if (gState == GameState::Console && gConsole) {
            std::string line;
            if (gConsole->onKey(key, action == GLFW_REPEAT, mods, line))
                gConsoleLines.push_back(line);
        }
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

    // Player preferences (Esc -> Settings)
    gSettings.load();

    // ── Data — must load BEFORE the renderer ──────────────
    // The renderer needs the resource pack to find its font
    if (!gRegistry.load("data/registry/blocks.toml")) {
        std::cerr << "Registry load failed\n";
        glfwTerminate();
        return -1;
    }

    // Items: every block, plus tools / weapons / dev tools
    gItems.load(gRegistry, "data/registry/items.toml");

    // Multi-block structures: building pieces, doors, trees
    gStructures.load("data/registry/structures.toml");

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
    DevMenuScreen   devMenu(window);
    ControlsScreen  controlsScreen(window);
    SettingsScreen  settingsScreen(window);
    BuildMenuScreen buildMenu(window);
    Hud             hud;
    Console         console;
    gMainMenu = &mainMenu;
    gConsole  = &console;
    gControls = &controlsScreen;

    // Messages from commands, tools and saving go to the
    // console (and the terminal)
    auto say = [&console](const std::string& msg) {
        console.print(msg);
        std::cout << msg << "\n";
    };

    // ── Building and commands ─────────────────────────────
    // DevTools holds the dev operations shared by the tool
    // items and the console commands; PlayerInteraction is
    // the player's hands (break / place / use tool).
    DevTools          dev;
    PlayerInteraction hands;
    BuildTool         build;       // the Builder's Hammer
    hud.build = &build;
    CommandRegistry   commands;
    registerDefaultCommands(commands);
    dev.log   = say;
    hands.log = say;

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
    std::unique_ptr<WorldEditor>      editor;    // all world edits
    std::unique_ptr<StructureManager> structs;   // doors, trees, buildings
    std::unique_ptr<WorldStreamer>    streamer;
    std::unique_ptr<FarTerrain>       farView;   // distant terrain
    float autosaveTimer = 0.0f;

    // ── Save everything ───────────────────────────────────
    // Edited chunks (only those — untouched terrain is
    // rebuilt from the seed and comes out identical) and the
    // player file: position, view, mode, health, both
    // inventories, dev tool settings.
    auto saveWorld = [&]() {
        if (!world) return;
        if (editor) editor->flush();     // finish queued edits first
        gWorldSave.saveDirtyChunks(*world);
        if (structs) structs->save(gWorldSave.getWorldPath());
        gWorldSave.savePlayer([&](std::ostream& out) {
            player.write(out);
            build.write(out);
            out << "look=" << camera.yaw << " " << camera.pitch << "\n";
            out << "thirdperson=" << (controller.thirdPersonWanted ? 1 : 0) << "\n";
            dev.write(out);
        });
        autosaveTimer = 0.0f;
        say("world saved");
    };

    // Leave the world: save, then tear everything down.
    // Streamer first: it stops the worker threads and frees
    // the GPU meshes before the world goes away.
    auto leaveWorld = [&]() {
        saveWorld();
        streamer.reset();
        farView.reset();
        dev.attach(nullptr);
        structs.reset();
        editor.reset();
        gPhysics.reset();
        world.reset();
        gen.reset();
    };

    // Switch the player to another game mode. The body is
    // put wherever the camera is, so you can fly somewhere
    // as a spectator and drop in to play from there.
    auto changeMode = [&](GameMode mode) {
        player.setMode(mode);
        bodyToCamera();
        std::cout << "Game mode: "
                  << player.rules().name << "\n";
    };

    // ── Open a world (new or saved) ───────────────────────
    //   mode — default mode for a new world; a saved world
    //          uses the one stored in its file
    // Returns immediately. Nothing is generated here — the
    // streamer brings chunks in on background threads, so the
    // window never freezes however big the world is.
    auto enterWorld = [&](bool isNew,
                          const std::string& name,
                          uint64_t seed,
                          GameMode mode,
                          WorldType type,
                          int size) {
        streamer.reset();
        farView.reset();
        gPhysics.reset();

        if (isNew) {
            std::cout << "Creating world: " << name
                      << " seed=" << seed << "\n";
            gWorldSave.createWorld(name, seed, mode, type, size);
        } else {
            std::cout << "Loading world: " << name << "\n";
            gWorldSave.loadWorld(name);
            seed = gWorldSave.getSeed();
            mode = gWorldSave.getDefaultMode();
            type = gWorldSave.getWorldType();
        }

        // World size first: the chunk grid and streamer are
        // built for it
        size = gWorldSave.getWorldSize();
        WORLD_SIZE_X = WORLD_SIZE_Z = size;
        std::cout << worldTypeName(type) << " world, " << size << " x "
                  << size << " chunks\n";

        gen   = makeGenerator(type, seed, size);
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

        // The distant view, coloured with each block's
        // average atlas colour
        {
            auto colors = std::make_shared<FarTerrain::ColorTable>(blockTable->blocks.size());
            for (size_t id = 0; id < blockTable->blocks.size(); id++) {
                const BlockRenderInfo& info = blockTable->get((uint16_t)id);
                (*colors)[id] = glr->tileAverage(info.tileX[FACE_TOP], info.tileY[FACE_TOP]);
            }
            farView = std::make_unique<FarTerrain>(*gen, WORLD_SIZE_X, WORLD_SIZE_Z, colors);
            if (!farView->initGL()) {
                std::cerr << "Distant terrain shaders failed to load\n";
                farView.reset();
            }
        }

        gPhysics = std::make_unique<VoxelPhysics>();
        gPhysics->init(*world);

        editor = std::make_unique<WorldEditor>(*world);
        structs = std::make_unique<StructureManager>(*world, *editor, *gen);
        structs->log = say;
        structs->load(gWorldSave.getWorldPath());
        build.reset();
        dev.resetForNewWorld();
        dev.attach(editor.get());
        hands.clearTarget();
        autosaveTimer = 0.0f;

        // Spawn point. A spectator
        // starts high enough to clear the tallest tree
        // (about 85 voxels); a body starts just above the
        // ground and drops onto it once the chunk arrives.
        // The world type decides where players appear (the
        // island's spawn marker; the classic world's centre)
        glm::ivec3 sp = gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z);
        int spawnX = sp.x, spawnZ = sp.z, ground = sp.y;

        player.resetForNewWorld();
        player.spawnPoint = glm::vec3(
            (float)spawnX, (float)ground, (float)spawnZ);
        camera = Camera();
        controller.thirdPersonWanted = false;

        // A returning player picks up exactly where they
        // left off: position, view, mode and inventories.
        // A new player starts at the spawn in the world's
        // default mode.
        float lookYaw = camera.yaw, lookPitch = camera.pitch;
        bool returning = !isNew && gWorldSave.loadPlayer(
            [&](const std::string& key, const std::string& value) {
                if (key == "look")
                    std::sscanf(value.c_str(), "%f %f", &lookYaw, &lookPitch);
                else if (key == "thirdperson")
                    controller.thirdPersonWanted = (value == "1");
                else if (!player.read(key, value) && !build.read(key, value))
                    dev.read(key, value);
            });

        if (returning) {
            mode = player.mode;
            camera.position = player.position +
                glm::vec3(0.0f, Human::STAND_EYE, 0.0f);
            camera.setLook(lookYaw, lookPitch);
            say("welcome back");
        } else {
            float above = rulesFor(mode, false).hasBody
                        ? Human::STAND_EYE + 3.0f : 105.0f;
            camera.position = glm::vec3(
                (float)spawnX, (float)ground + above, (float)spawnZ);
            // A starter kit in the dev inventory
            const char* kit[] = { "dev_wand", "dev_fill", "dev_brush",
                                  "dev_copy", "dev_paste", "dev_breaker",
                                  "stone", "builders_hammer" };
            for (int i = 0; i < Inventory::TOOLBAR_SLOTS; i++)
                if (uint16_t id = gItems.find(kit[i]))
                    player.devInventory.setSlot(i, id, 1);
            // Every player starts with a Builder's Hammer
            if (uint16_t h = gItems.find("builders_hammer"))
                player.inventory.setSlot(0, h, 1);
        }
        changeMode(mode);

        gState = GameState::Playing;

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

        glm::ivec2 pc = world->worldToChunk(camera.position);

        // Distant terrain first, with its own clip planes,
        // stepping aside wherever voxel chunks are drawn.
        // Then the depth buffer is cleared, so the voxels
        // always draw over it.
        if (farView) {
            static std::vector<uint8_t> nearMask;
            nearMask.assign((size_t)WORLD_SIZE_X * WORLD_SIZE_Z, 0);
            for (int cx = pc.x - MESH_DISTANCE; cx <= pc.x + MESH_DISTANCE; cx++)
            for (int cz = pc.y - MESH_DISTANCE; cz <= pc.y + MESH_DISTANCE; cz++)
                if (streamer->hasMesh(cx, cz))
                    nearMask[(size_t)cz * WORLD_SIZE_X + cx] = 1;
            farView->setNearMask(nearMask);
            farView->draw(glr->getViewMatrix(), Camera::FOV_DEG, aspect, camera.position,
                          OpenGLRenderer::FOG_COLOR, OpenGLRenderer::FOG_DENSITY);
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        // World pass: depth test + back-face culling ON
        glr->beginWorldDraw();

        for (int cx = pc.x - MESH_DISTANCE;
                 cx <= pc.x + MESH_DISTANCE; cx++)
        for (int cz = pc.y - MESH_DISTANCE;
                 cz <= pc.y + MESH_DISTANCE; cz++) {
            // Not built yet (or outside the world) → skip
            if (!streamer->hasMesh(cx, cz)) continue;
            glm::vec3 origin = world->chunkOrigin(cx, cz);
            for (int sy = 0; sy < SECTIONS; sy++) {
                const ChunkMesh* mesh = streamer->getMesh(cx, sy, cz);
                if (!mesh || mesh->isEmpty()) continue;
                glr->drawMesh(*mesh, origin + glm::vec3(0.0f, (float)(sy * VOXEL_DIM), 0.0f));
            }
        }

        // Culling OFF again so the UI draws normally
        glr->endWorldDraw();

        // Third person: there is no player model yet, so a
        // plain box stands in for the body — torso in the
        // stance colour, with a lighter block for the face
        // so you can tell which way it is looking.
        const GameModeRules rules = player.rules();
        if (rules.hasBody &&
            controller.usingThirdPerson(rules) &&
            controller.bodyVisible()) {
            float yaw = glm::degrees(std::atan2(
                player.facing.z, player.facing.x));
            glm::vec3 feet = controller.renderPosition;
            glm::vec3 tint = player.combatStance
                ? glm::vec3(0.80f, 0.30f, 0.25f)
                : glm::vec3(0.25f, 0.45f, 0.80f);
            glr->drawBox(feet,
                glm::vec3(Human::RADIUS * 2.0f, player.height,
                          Human::RADIUS * 2.0f),
                yaw, tint);
            glm::vec3 ahead = glm::normalize(player.facing);
            glr->drawBox(
                feet + glm::vec3(0.0f, player.eyeHeight - 1.0f, 0.0f)
                     + ahead * Human::RADIUS,
                glm::vec3(1.0f, 2.0f, 2.0f), yaw,
                glm::vec3(0.95f, 0.85f, 0.70f));
        }

        // ── Outlines: target block, selection, paste ──────
        // Twelve thin boxes along the edges of [lo, hi).
        // Thickness grows with distance so far edges stay
        // visible.
        auto wire = [&](glm::vec3 lo, glm::vec3 hi, glm::vec3 col) {
            glm::vec3 mid = (lo + hi) * 0.5f;
            float t = std::max(0.06f,
                glm::length(mid - camera.position) * 0.004f);
            glm::vec3 s = hi - lo;
            for (int a = 0; a < 3; a++) {          // edge direction
                int b = (a + 1) % 3, c = (a + 2) % 3;
                for (int i = 0; i < 4; i++) {
                    glm::vec3 base = lo;
                    base[b] += (i & 1) ? s[b] : 0.0f;
                    base[c] += (i & 2) ? s[c] : 0.0f;
                    glm::vec3 size(t);
                    size[a] = s[a] + t;
                    // drawBox places boxes by the centre of
                    // their bottom face
                    glm::vec3 centre = base;
                    centre[a] += s[a] * 0.5f;
                    glm::vec3 bottom = centre - glm::vec3(0.0f, size.y * 0.5f, 0.0f);
                    glr->drawBox(bottom, size, 0.0f, col);
                }
            }
        };

        // Builder's Hammer: outline of the chosen structure
        // where it would go — green if it can be placed, red
        // if not — plus its anchor voxel
        if (hands.hammerActive && hands.previewHas && rules.worldInteraction) {
            if (const StructureType* t = build.type()) {
                const VoxelVolume& v = t->shape(0, build.rot);
                glm::vec3 lo(hands.previewSpot - v.anchor);
                wire(lo, lo + glm::vec3(v.size),
                     hands.previewWhy.empty() ? glm::vec3(0.3f, 0.95f, 0.4f)
                                              : glm::vec3(1.0f, 0.3f, 0.25f));
                wire(glm::vec3(hands.previewSpot), glm::vec3(hands.previewSpot) + 1.0f,
                     glm::vec3(1.0f));
            }
        }

        if (rules.worldInteraction && hands.target.hit &&
            gState != GameState::DevMenu) {
            glm::vec3 b(hands.target.blockPos);
            wire(b - 0.01f, b + 1.01f, glm::vec3(0.05f));
        }
        if (player.mode == GameMode::Dev) {
            if (dev.hasSelection())
                wire(glm::vec3(dev.selMin()) - 0.02f,
                     glm::vec3(dev.selMax()) + 1.02f,
                     glm::vec3(0.95f, 0.80f, 0.20f));
            else if (dev.hasPos1 || dev.hasPos2) {
                glm::vec3 p(dev.hasPos1 ? dev.pos1 : dev.pos2);
                wire(p, p + 1.0f, glm::vec3(0.95f, 0.80f, 0.20f));
            }

            // Where the paste tool would put the clipboard:
            // following the crosshair, or locked (brighter)
            const ItemDef& held = player.devInventory.heldDef();
            if (held.devTool == DevToolKind::Paste && dev.clipboard &&
                dev.pasteHasTarget(hands.target)) {
                glm::ivec3 at = dev.pasteTarget(hands.target);
                glm::vec3 lo(at - dev.clipboard->anchor);
                wire(lo, lo + glm::vec3(dev.clipboard->size),
                     dev.pasteLocked ? glm::vec3(1.0f, 0.4f, 1.0f)
                                     : glm::vec3(0.75f, 0.5f, 0.75f));
                // The anchor voxel itself
                wire(glm::vec3(at), glm::vec3(at) + 1.0f,
                     glm::vec3(1.0f, 1.0f, 1.0f));
            }

            // Brush and explosion: three rings showing the
            // size of the sphere they will make
            if ((held.devTool == DevToolKind::Explode ||
                 held.devTool == DevToolKind::Brush) && hands.target.hit) {
                bool blast = held.devTool == DevToolKind::Explode;
                glm::vec3 c = glm::vec3(blast ? hands.target.blockPos
                    : hands.target.blockPos + glm::ivec3(hands.target.normal)) + 0.5f;
                float r = dev.brushRadius;
                glm::vec3 col = blast ? glm::vec3(1.0f, 0.3f, 0.15f)
                                      : glm::vec3(0.3f, 0.9f, 0.45f);
                float t = std::max(0.08f,
                    glm::length(c - camera.position) * 0.004f);
                // Enough small cubes along each ring that they
                // touch and read as a solid line
                const int seg = std::max(48, std::min(720,
                    (int)(6.2831853f * r / (t * 1.6f))));
                for (int ring = 0; ring < 3; ring++)
                for (int i = 0; i < seg; i++) {
                    float ang = 6.2831853f * i / seg;
                    glm::vec3 p;
                    if (ring == 0)      p = { std::cos(ang), 0, std::sin(ang) };
                    else if (ring == 1) p = { std::cos(ang), std::sin(ang), 0 };
                    else                p = { 0, std::sin(ang), std::cos(ang) };
                    glm::vec3 at = c + p * r;
                    glr->drawBox(at - glm::vec3(0, t, 0), glm::vec3(t * 2.0f),
                                 0.0f, col);
                }
            }
        }
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
            // F4 — next game mode:
            // Spectator → Test Play → Survival → Spectator.
            // (Combat is not in the cycle: it is a stance
            // inside Survival, toggled with R.)
            if (gInput.isPressed(InputAction::GamemodeCycle))
                changeMode(nextGameMode(player.mode));

            // ` — open the console
            if (gInput.isPressed(InputAction::ToggleConsole)) {
                console.toggle();
                gState = GameState::Console;
                gConsoleLines.clear();
            }

            // Test Play building keys
            if (player.mode == GameMode::Dev) {
                if (gInput.isPressed(InputAction::Undo)) dev.undo();
                if (gInput.isPressed(InputAction::Redo)) dev.redo();
                if (gInput.isPressed(InputAction::BrushBigger))
                    dev.setBrush(dev.brushRadius + 1.0f);
                if (gInput.isPressed(InputAction::BrushSmaller))
                    dev.setBrush(dev.brushRadius - 1.0f);
            }

            // F6 — hot reload blocks.toml and items.toml
            if (gInput.isPressed(InputAction::ReloadRegistry)) {
                gRegistry.load("data/registry/blocks.toml");
                gItems.load(gRegistry, "data/registry/items.toml");
                gStructures.load("data/registry/structures.toml");
                blockTable = BlockRenderTable::build(
                    gRegistry, gResourcePacks);
                if (streamer)
                    streamer->setRenderTable(blockTable);
                if (gPhysics && world) gPhysics->init(*world);
                say("blocks and items reloaded");
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
            if (farView) farView->update(camera.position);

            // Big edits are spread over frames: a few
            // milliseconds of them per frame
            if (editor) editor->update(4.0f);

            // Autosave every few minutes
            autosaveTimer += frameTime;
            if (autosaveTimer >= AUTOSAVE_SECONDS) saveWorld();
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
                           mainMenu.getNewWorldSeed(),
                           mainMenu.getNewWorldMode(),
                           mainMenu.getNewWorldType(),
                           mainMenu.getNewWorldSize());
            } else if (r == ScreenResult::LoadWorld) {
                // Seed and mode come from the save file
                enterWorld(false,
                           mainMenu.getLoadWorldName(), 0,
                           GameMode::Survival,
                           WorldType::Classic, 0);
            } else if (r == ScreenResult::Quit) {
                glfwSetWindowShouldClose(window, true);
            }

        } else if (gState == GameState::Playing) {

            camera.sensitivity = gSettings.mouseSensitivity;
            glm::vec2 mouseDelta = gInput.getMouseDelta();

            // What this player's mode allows. Every mode
            // with a body shares the same physics path;
            // only the free camera is different.
            const GameModeRules rules = player.rules();

            if (!rules.hasBody) {
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

            } else {
                // ── A body in the world (Test Play,
                //    Survival, and Survival's combat stance) ─
                camera.processMouse(
                    mouseDelta.x, mouseDelta.y);

                // 1. Keyboard → what the player wants to do.
                //    Which buttons do what depends on the
                //    mode and stance (see PlayerController).
                controller.slotKeysBlocked =
                    PlayerInteraction::blocksToolbarKeys(dev);
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

                // 4. Camera to the player's eyes — or behind
                //    them in third person — blended between
                //    the last two ticks
                controller.updateCamera(player, camera,
                    physicsTime / PHYSICS_STEP, dt,
                    gPhysics.get());

                // 5. Hands: what is under the crosshair, and
                //    break / place / use the held tool
                glm::vec3 eyes = controller.renderPosition +
                    glm::vec3(0.0f, player.eyeHeight, 0.0f);
                if (rules.worldInteraction) {
                    hands.aim(camera, eyes, *gPhysics,
                              player.mode == GameMode::Dev);
                    hands.update(player, *editor, dev, *structs, build, dt);

                    // Hammer right click → build menu
                    if (hands.wantBuildMenu) {
                        hands.wantBuildMenu = false;
                        buildMenu.setContext(&player, &build);
                        buildMenu.onEnter();
                        gState = GameState::BuildMenu;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    }
                } else {
                    hands.clearTarget();
                }
            }
            if (!rules.hasBody) hands.clearTarget();

            drawWorld();
            hud.draw(*gRenderer->getUIRenderer(),
                     player, controller, hands, dev);
            console.update(dt);
            console.draw(*gRenderer->getUIRenderer());

        } else if (gState == GameState::Console) {

            // The world keeps drawing; the player stands still
            // while you type
            drawWorld();
            hud.draw(*gRenderer->getUIRenderer(),
                     player, controller, hands, dev);

            // Run whatever was typed this frame
            for (const std::string& line : gConsoleLines) {
                CommandContext ctx {
                    player, dev, hands.target, structs.get(),
                    [&](GameMode m) { changeMode(m); },
                    [&]() { saveWorld(); },
                    say };
                commands.run(line, ctx);
            }
            gConsoleLines.clear();

            console.update(dt);
            console.draw(*gRenderer->getUIRenderer());

            // Closed with ` or Esc → back to the game
            if (!console.open) {
                gState = GameState::Playing;
                camera.firstMouse = true;
                gInput.resetMouseDelta(window);
            }

        } else if (gState == GameState::BuildMenu) {

            drawWorld();
            hud.draw(*gRenderer->getUIRenderer(),
                     player, controller, hands, dev);
            ScreenResult r = buildMenu.update(*gRenderer->getUIRenderer(), dt);
            if (r == ScreenResult::CloseMenu ||
                gInput.isPressed(InputAction::Pause)) {
                gState = GameState::Playing;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                camera.firstMouse = true;
                gInput.resetMouseDelta(window);
            }

        } else if (gState == GameState::Controls) {

            drawWorld();
            ScreenResult r = controlsScreen.update(
                *gRenderer->getUIRenderer(), dt);
            if (r == ScreenResult::CloseMenu) {   // Back or Esc
                gState = GameState::Paused;
                pauseMenu.onEnter();
            }

        } else if (gState == GameState::Settings) {

            // The HUD stays visible so toolbar changes show
            // as you drag the sliders
            drawWorld();
            hud.draw(*gRenderer->getUIRenderer(),
                     player, controller, hands, dev);
            ScreenResult r = settingsScreen.update(
                *gRenderer->getUIRenderer(), dt);
            if (r == ScreenResult::CloseMenu ||
                gInput.isPressed(InputAction::Pause)) {
                gSettings.save();
                gState = GameState::Paused;
                pauseMenu.onEnter();
            }

        } else if (gState == GameState::DevMenu) {

            drawWorld();
            UIRenderer* ui = gRenderer->getUIRenderer();
            ScreenResult r = devMenu.update(*ui, dt);
            if (r == ScreenResult::CloseMenu ||
                gInput.isPressed(InputAction::Pause)) {
                gState = GameState::Paused;
                pauseMenu.onEnter();
            }

        } else if (gState == GameState::Paused) {

            // World stays visible behind the menu
            drawWorld();

            UIRenderer* ui = gRenderer->getUIRenderer();
            pauseMenu.setDevMenuAllowed(player.mode != GameMode::Survival);
            ScreenResult r = pauseMenu.update(*ui, dt);

            if (r == ScreenResult::OpenDevMenu) {
                devMenu.setContext(&player, &dev, &build);
                devMenu.onEnter();
                gState = GameState::DevMenu;

            } else if (r == ScreenResult::OpenControls) {
                controlsScreen.onEnter();
                gState = GameState::Controls;

            } else if (r == ScreenResult::OpenSettings) {
                settingsScreen.onEnter();
                gState = GameState::Settings;

            } else if (r == ScreenResult::ResumeGame) {
                gState = GameState::Playing;
                glfwSetInputMode(window, GLFW_CURSOR,
                                 GLFW_CURSOR_DISABLED);
                camera.firstMouse = true;
                gInput.resetMouseDelta(window);

            } else if (r == ScreenResult::SaveAndQuit) {
                leaveWorld();
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

                // A body: live readout for tuning movement
                const GameModeRules rules = player.rules();
                if (rules.hasBody && n > 0 &&
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
                        "  |  %s  pos %.0f %.1f %.0f"
                        "  speed %.1f  vy %.1f  %s",
                        rules.name,
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
        leaveWorld();

    // Everything that owns OpenGL objects goes before
    // glfwTerminate() destroys the context
    streamer.reset();
    farView.reset();
    editor.reset();
    gPhysics.reset();
    world.reset();
    gen.reset();
    gRenderer->shutdown();
    gRenderer.reset();
    glfwTerminate();
    return 0;
}
