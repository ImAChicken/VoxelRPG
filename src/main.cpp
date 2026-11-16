#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
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
#include "render/CharacterRenderer.h"
#include "character/HumanAnim.h"
#include "render/OutlineFx.h"
#include "ui/CharacterScreen.h"
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
#include "ui/InventoryScreen.h"
#include "world/StructureManager.h"
#include "world/Woodcutting.h"
#include "sim/Simulation.h"
#include "game/DebugView.h"
#include "render/PropRenderer.h"
#include "worldgen/WorldType.h"
#include "render/Sky.h"
#include "world/WorldMemory.h"
#include "world/LightBake.h"
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
    Inventory,      // the inventory screen (I)
    Character,      // character creator
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
CharacterScreen* gCharScreen = nullptr;

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
    if (gState == GameState::Character && gCharScreen)
        gCharScreen->onChar(codepoint);
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
        if (gState == GameState::Character && gCharScreen)
            gCharScreen->onKey(key);
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
    gSettings.load(); Human::eyeStand = gSettings.eyeHeight;

    // ── Data — must load BEFORE the renderer ──────────────
    // The renderer needs the resource pack to find its font
    if (!gRegistry.load("data/registry/blocks.toml")) {
        std::cerr << "Registry load failed\n";
        glfwTerminate();
        return -1;
    }

    // Items: every block, plus tools / weapons / dev tools
    gItems.load(gRegistry, "data/registry/items.toml");
    gMovesets.load("data/registry/movesets.toml");
    gHeldModels.loadDir("data/models/held");
    gFactions.load("data/registry/factions.toml");
    gNpcTypes.load("data/registry/npcs.toml", "data/characters/presets.toml");
    gAnims.load("data/animations", "data/animations/retarget.toml");
    gAnimMap.load("data/animations/anim_map.toml");
    gOutlineFx.load("data/registry/outline.toml");
    decor::current() = decor::Loader::load(gRegistry, "data/registry/decorations.toml");
    gItems.addDecor(*decor::current());

    // Multi-block structures: building pieces, doors, trees
    gStructures.load("data/registry/structures.toml"); gStructures.addDecorTrees(*decor::current());

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

    // ── Characters ────────────────────────────────────────
    // The player's body (drawn in third person) and the one
    // shown in the character creator. Freed before the GL
    // context goes, at the end of main.
    // Swatches (a pack may bring its own) and painted faces
    auto loadCharacterArt = [&]() {
        gPalettes.setDefaults();
        gPalettes.load("data/characters/palettes.toml");
        auto packPal = gResourcePacks.findFile("characters/palettes.toml");
        if (std::filesystem::exists(packPal)) gPalettes.load(packPal);
        if (!gFaces.load()) std::cerr << "Character face art missing\n";
    };
    loadCharacterArt();
    auto hairMesh = []() { return (HairMesh)gSettings.charHairMesh; };
    auto edgeStyle = []() { EdgeStyle e; e.mode = gSettings.charEdges; e.color = gSettings.charLineColor; return e; };

    CharacterRenderer charRenderer;
    if (!charRenderer.init()) std::cerr << "Character renderer failed to start\n";
    PropRenderer props;                       // logs, wood, leaves: drawn like the world
    if (!props.init()) std::cerr << "Prop renderer failed to start\n";
    DebugDraw debugDraw;                      // the debug overlay's lines (F3, game/DebugView.h)
    if (!debugDraw.init()) std::cerr << "Debug lines failed to start\n";
    std::vector<std::array<glm::vec3, 3>> axeTrail;   // the last axe swing's blade, for the overlay
    bool wasSwinging = false;
    props.setBlocks(blockTable);
    auto playerBody  = std::make_unique<Character>();
    auto previewBody = std::make_unique<Character>();

    // The look a new world's player starts with: whatever was
    // last chosen in the character creator
    const char* DEFAULT_CHARACTER = "saves/default_character.txt";
    auto loadDefaultCharacter = [&]() {
        Appearance a;
        std::ifstream f(DEFAULT_CHARACTER);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t eq = line.find('=');
            if (eq != std::string::npos) a.read(line.substr(0, eq), line.substr(eq + 1));
        }
        return a;
    };
    auto saveDefaultCharacter = [&](const Appearance& a) {
        std::filesystem::create_directories("saves");
        std::ofstream f(DEFAULT_CHARACTER);
        a.write(f);
    };
    float frameDt = 0.016f;    // last frame's length, for animation

    // ── Light and sky ─────────────────────────────────────
    // The clock (time of day), the sky dome, what the world
    // remembers of chunks out of reach (lowres.dat: their ground
    // and lights), and the glowing points of distant lights
    SkyRenderer   skyRenderer;
    if (!skyRenderer.init()) std::cerr << "Sky shader failed\n";
    WorldMemory   memory;
    DistantLights distantLights;
    distantLights.init();
    uint32_t      memorySeen = 0;
    auto lightTable = [&]() { return light::LightTable::build(gRegistry); };

    // ── Screens ───────────────────────────────────────────
    MainMenuScreen  mainMenu(window);
    PauseMenuScreen pauseMenu(window);
    DevMenuScreen   devMenu(window);
    ControlsScreen  controlsScreen(window);
    SettingsScreen  settingsScreen(window);
    CharacterScreen characterScreen(window);
    BuildMenuScreen buildMenu(window);
    InventoryScreen inventoryScreen(window);
    bool inventoryOpenedFrame = false;
    Hud             hud;
    Console         console;
    gMainMenu = &mainMenu;
    gConsole  = &console;
    gControls = &controlsScreen;
    gCharScreen = &characterScreen;

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
    // The game itself (sim/Simulation.h): every body, the fixed
    // tick, combat. This window shows one of its players.
    Simulation       sim;
    Camera           camera;
    Player&          player = sim.addPlayer();
    PlayerController controller;

    // Left-over frame time not yet turned into physics ticks
    float physicsTime = 0.0f;

    // Put the player's body where the camera is (feet below
    // the eyes) and bring it to rest
    auto bodyToCamera = [&]() {
        player.position = camera.position -
            glm::vec3(0.0f, Human::eyeStand, 0.0f);
        player.resetMotion();
        player.crouching = false;
        player.flying    = false;
        player.height    = Human::STAND_HEIGHT;
        player.eyeHeight = Human::eyeStand;
        controller.reset(player);
        physicsTime = 0.0f;
    };
    std::unique_ptr<TerrainGenerator> gen;
    std::unique_ptr<ChunkManager>     world;
    std::unique_ptr<WorldEditor>      editor;    // all world edits
    std::unique_ptr<StructureManager> structs;   // doors, trees, buildings
    std::unique_ptr<Woodcutting>      wood;      // felled trees, logs, wood on the ground
    // Meshes for woodcutting's logs (by log id), drawn like the
    // world (render/PropRenderer.h)
    std::unordered_map<uint32_t, PropRenderer::Mesh> logMeshes;
    auto dropWoodMeshes = [&]() {
        for (auto& [k, m] : logMeshes) m.release();
        logMeshes.clear();
    };
    // Enemies live in the simulation; their bodies to draw (by id)
    NpcManager& npcs = sim.npcs();
    EntityId talkingTo = 0;           // the NPC this player is in a menu with
    std::unordered_map<uint64_t, std::unique_ptr<Character>> npcBodies;
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
        if (wood) wood->save(gWorldSave.getWorldPath());
        npcs.save(gWorldSave.getWorldPath() + "/npcs.dat");
        memory.save(gWorldSave.getWorldPath() + "/lowres.dat");
        { std::ofstream tf(gWorldSave.getWorldPath() + "/time.txt"); tf << gClock.time << "\n"; }
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
        hands.wood = nullptr;
        wood.reset();
        dropWoodMeshes();
        sim.detachWorld();
        npcBodies.clear();
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
                          int size,
                          int biome = -1,
                          TerrainStyle terrain = TerrainStyle::GuidedRivers,
                          const WorldGenParams& genParams = WorldGenParams()) {
        streamer.reset();
        farView.reset();
        gPhysics.reset();

        if (isNew) {
            std::cout << "Creating world: " << name
                      << " seed=" << seed << "\n";
            gWorldSave.createWorld(name, seed, mode, type, size, biome, terrain, genParams);
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

        // The generator. Island ground is baked with erosion
        // (a few seconds the first time, then read from the world
        // folder), so it's built on a worker thread while a
        // progress screen keeps the window alive
        {
            const bool baking = type != WorldType::Classic && gWorldSave.getTerrainStyle() != TerrainStyle::NoErosion;
            std::string cache = gWorldSave.getWorldPath() + "/terrain.bake";
            int biomeArg = gWorldSave.getWorldBiome();
            TerrainStyle style = gWorldSave.getTerrainStyle();
            WorldGenParams gparams = gWorldSave.getGenParams();
            gBakeProgress = 0.0f; gBakeStage = 0;
            std::unique_ptr<TerrainGenerator> made;
            std::atomic<bool> done{ false };
            std::thread worker([&] {
                made = makeGenerator(type, seed, size, biomeArg, style, cache, gparams);
                done = true;
            });
            while (baking && !done) {
                glfwPollEvents();
                gRenderer->beginFrame();
                UIRenderer* ui = gRenderer->getUIRenderer();
                const auto& theme = gResourcePacks.getTheme();
                float sw = (float)ui->getWidth(), sh = (float)ui->getHeight();
                ui->begin();
                ui->drawRect(0, 0, sw, sh, theme.bg);
                ui->drawTextCentered("Shaping the land", 0, sh * 0.5f - 60, sw, 30, theme.title, 3);
                ui->drawTextCentered(bakeStageName(gBakeStage), 0, sh * 0.5f - 18, sw, 20, theme.textDim, 2);
                float bw = std::min(520.0f, sw - 80.0f), bx = (sw - bw) * 0.5f, by = sh * 0.5f + 20;
                ui->drawRect(bx, by, bw, 14, theme.inputBg);
                ui->drawRect(bx, by, bw * std::clamp(gBakeProgress.load(), 0.0f, 1.0f), 14, theme.accent);
                ui->drawBorder(bx, by, bw, 14, theme.border, 1.0f);
                ui->end();
                gRenderer->endFrame();
                glfwSwapBuffers(window);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            worker.join();
            gen = std::move(made);
        }
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
            *world, chunkSource, blockTable, lightTable());
        memory.load(gWorldSave.getWorldPath() + "/lowres.dat");
        memorySeen = 0;
        {   // the world's clock
            std::ifstream tf(gWorldSave.getWorldPath() + "/time.txt");
            float t; if (tf >> t) gClock.time = std::fmod(std::fabs(t), 1.0f); else gClock.time = 0.35f;
        }

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
        dropWoodMeshes();
        wood = std::make_unique<Woodcutting>();
        wood->load(gWorldSave.getWorldPath());
        hands.wood = wood.get();
        // bodies collide with fallen logs; raycasts hit them
        gPhysics->setLooseSolids([&](int x, int y, int z) { return wood && wood->solidAt(x, y, z); });
        sim.attachWorld(gPhysics.get(), wood.get());
        if (int n = npcs.load(gWorldSave.getWorldPath() + "/npcs.dat")) std::cout << n << " NPCs loaded\n";
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
        player.appearance = loadDefaultCharacter();   // a saved player overrides it below
        playerBody->resetAnimation();
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
        player.inventory.finishLoad();
        player.devInventory.finishLoad();

        if (returning) {
            mode = player.mode;
            camera.position = player.position +
                glm::vec3(0.0f, Human::eyeStand, 0.0f);
            camera.setLook(lookYaw, lookPitch);
            say("welcome back");
        } else {
            float above = rulesFor(mode, false).hasBody
                        ? Human::eyeStand + 3.0f : 105.0f;
            camera.position = glm::vec3(
                (float)spawnX, (float)ground + above, (float)spawnZ);
            // A starter kit in the dev inventory
            const char* kit[] = { "dev_wand", "dev_fill", "dev_brush",
                                  "dev_copy", "dev_paste", "dev_cube",
                                  "stone", "builders_hammer" };
            for (int i = 0; i < 8; i++)
                if (uint16_t id = gItems.find(kit[i]))
                    player.devInventory.setSlot(Inventory::FIRST_MAIN + i, id, 1);
            // Every player starts with a Builder's Hammer
            if (uint16_t h = gItems.find("builders_hammer"))
                player.inventory.setSlot(Inventory::FIRST_MAIN, h, 1);
            // …and a pickaxe
            if (uint16_t pk = gItems.find("stone_pickaxe"))
                player.inventory.setSlot(Inventory::FIRST_MAIN + 1, pk, 1);
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

        // ── This frame's light and haze ───────────────────
        {
            FrameLight& fl = gFrameLight;
            fl.sky = SkyState::at(gClock.time);
            // clear air: only a thin haze, a little more where
            // the voxel chunks hand over to the distant terrain
            float edge = MESH_DISTANCE * (float)VOXEL_DIM;
            float farVox = gSettings.renderAll() ? 0.0f : gSettings.renderDistance * VOXEL_DIM;
            fl.fogDensity = farVox > 0.0f ? std::max(3.0e-5f, 2.6f / farVox) : 3.0e-5f;
            fl.seamStart = std::max(edge * 0.6f, edge - 300.0f);
            fl.seamEnd   = edge - 16.0f;
            fl.seamHaze  = 0.45f;
        }
        // the sky dome behind everything
        {
            glm::mat4 skyProj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect, 0.5f, 10.0f);
            glm::mat4 v = glr->getViewMatrix();
            v[3] = glm::vec4(0, 0, 0, 1);
            skyRenderer.draw(skyProj * v, gFrameLight.sky);
        }
        // remembered ground and lights from the chunks baked
        // since last frame
        if (streamer->columnInfoVersion() != memorySeen) {
            memorySeen = streamer->columnInfoVersion();
            std::vector<int> changed;
            for (const auto& [idx, ci] : streamer->columnInfo()) {
                // the streamer keys by ChunkManager index (cx * Z + cz); memory by cz * X + cx
                int cx = idx / WORLD_SIZE_Z, cz = idx % WORLD_SIZE_Z, key = cz * WORLD_SIZE_X + cx;
                auto it = memory.columns.find(key);
                if (it != memory.columns.end() && it->second.summary == ci.summary) continue;
                if (memory.remember(key, ci.summary, ci.emitters)) changed.push_back(key);
            }
            if (farView && !changed.empty()) {
                auto sums = std::make_shared<FarTerrain::Summaries>();
                for (const auto& [k, c] : memory.columns) if (c.summary) (*sums)[k] = c.summary;
                farView->setSummaries(sums, changed);
            }
        }

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
                          OpenGLRenderer::FOG_COLOR, OpenGLRenderer::fogDensity);
            // lights out beyond the voxel chunks, against the far terrain's depth
            glm::mat4 farProj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                                 FarTerrain::NEAR_PLANE, FarTerrain::FAR_PLANE);
            distantLights.draw(memory, farProj * glr->getViewMatrix(), camera.position,
                               (MESH_DISTANCE - 0.5f) * VOXEL_DIM, gFrameLight.sky.night);
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
                glr->drawMesh(*mesh, origin + glm::vec3(0.0f, (float)(sy * VOXEL_DIM), 0.0f),
                              streamer->getLightTexture(cx, sy, cz), streamer->getCanopyTexture(cx, cz));
            }
        }

        // Culling OFF again so the UI draws normally
        glr->endWorldDraw();

        // The player's body, posed from what it is doing this
        // frame. Animated whenever there is a body (the ragdoll
        // follows the animation even in first person); drawn
        // in third person.
        const GameModeRules rules = player.rules();
        // What any human is doing, for its animation (shared with
        // the simulation's poses: character/HumanAnim.h)
        auto animFor = [](const Human& h, bool combat, float pitch) { return animInputFor(h, combat, pitch); };
        if (rules.hasBody) {
            playerBody->setAppearance(player.appearance, hairMesh());
            const ItemDef* wielded = player.wielded();
            AnimInput in = animFor(player, player.combatStance || wielded, camera.pitch);
            // the axe swing (game/AxeSwing.h): stops if the axe is
            // put away or the body is knocked down
            if (player.swing.active() &&
                (!player.holdingAxe() || player.ragdoll.controlsLocked() || !rules.worldInteraction))
                player.swing.cancel();
            in.swing = player.swing.t;
            in.carryAxe = player.holdingAxe() && !player.combatStance;
            // what the right hand shows (items.toml held_model)
            playerBody->setHeldModel(player.combatStance ? (wielded ? wielded->heldModel : std::string())
                                     : player.activeInventory().heldDef().heldModel);
            // the left hand: the active off-hand slot
            {
                const ItemStack& off = player.activeInventory().offhandItem();
                playerBody->setOffhandModel(off.empty() ? std::string() : gItems.get(off.itemID).heldModel);
            }
            playerBody->animate(in, frameDt);

            // Ragdoll: follows this pose (at the physics position);
            // while it is visible (hit, limp, getting up) it is
            // drawn instead, blended between the last two ticks
            {
                const CharacterModel& model = playerBody->getModel();
                BoneMats target, drawn, out;
                worldBoneMatrices(model, playerBody->pose(), player.position, target);
                player.ragdoll.setModel(model);
                player.ragdoll.setTarget(target);
                worldBoneMatrices(model, playerBody->pose(), controller.renderPosition, drawn);
                if (player.ragdoll.renderBones(physicsTime / PHYSICS_STEP, drawn, out))
                    playerBody->setWorldPose(out);
                else
                    playerBody->clearWorldPose();

                // Axe: trace the blade's edge from last frame to this
                // one; the first solid voxel it passes is the hit
                AxeSwing& sw = player.swing;
                if (sw.active() && editor && structs) {
                    const ItemDef& tool = player.activeInventory().heldDef();
                    const HeldModelDef* hm = gHeldModels.find(tool.heldModel);
                    std::vector<glm::vec3> bp = hm ? hm->edgePoints(model) : std::vector<glm::vec3>();
                    while (bp.size() < 3) bp.push_back(bp.empty() ? heldGrip(model) : bp.back());
                    std::array<glm::vec3, 3> now;
                    for (int k = 0; k < 3; k++) now[k] = glm::vec3(target[B_WRIST_R] * glm::vec4(bp[k], 1.0f));
                    if (sw.striking() && sw.traceValid && sw.lastStep > 0.0f) {
                        axeswing::BladeHit h = axeswing::trace(sw.lastBlade, now, sw.lastStep,
                            [&](glm::ivec3 v) { return gPhysics->solidAt(v.x, v.y, v.z); });   // world and logs
                        // bodies: the first edge point inside someone
                        EntityId who = 0; float whoS = 2.0f; glm::vec3 whoAt(0), whoDir(0);
                        for (int k = 0; k < 3; k++) {
                            glm::vec3 d = now[k] - sw.lastBlade[k];
                            int steps = std::max(1, (int)std::ceil(glm::length(d) / 0.25f));
                            for (int i = 0; i <= steps; i++) {
                                float s = (float)i / steps;
                                if (s >= whoS) break;
                                glm::vec3 p = sw.lastBlade[k] + d * s;
                                if (EntityId id = sim.bodyAt(p, player.uuid)) { who = id; whoS = s; whoAt = p; whoDir = d; break; }
                            }
                        }
                        if (who && (!h.hit || whoS < h.s)) {
                            float speed = glm::length(now[1] - sw.lastBlade[1]) / std::max(sw.lastStep, 1e-4f);
                            float dmg = AxeSwing::damageFor(tool.toolPower, speed);
                            sim.toolHit(player.uuid, who, dmg, tool.poiseDamage * dmg / std::max(tool.toolPower, 1e-3f), whoAt, whoDir);
                            sw.hitDone = true;
                            sw.stop = AxeSwing::HITSTOP;
                        } else if (h.hit) {
                            sw.hitDone = true;
                            sw.stop = AxeSwing::HITSTOP;
                            hands.axeHit(player, *structs, *editor, h,
                                         player.activeInventory().heldDef().toolPower);
                        }
                    }
                    sw.lastBlade = now;
                    sw.traceValid = true;
                }
                sw.advance(frameDt);
            }
        }

        // Enemies: posed the same way, with their ragdolls
        {
            float alpha = std::clamp(physicsTime / PHYSICS_STEP, 0.0f, 1.0f);
            std::unordered_map<uint64_t, bool> alive;
            for (auto& np : npcs.all()) {
                Npc& n = *np;
                alive[n.uuid] = true;
                auto& ch = npcBodies[n.uuid];
                if (!ch) ch = std::make_unique<Character>();
                ch->setAppearance(n.appearance, hairMesh());
                {
                    uint16_t wid = gItems.find(n.weaponItem);
                    ch->setHeldModel(wid ? gItems.get(wid).heldModel : std::string());
                }
                ch->animate(animFor(n, true, 0.0f), frameDt);
                const CharacterModel& model = ch->getModel();
                BoneMats target, drawn, out;
                glm::vec3 at = glm::mix(n.prevPosition, n.position, alpha);
                worldBoneMatrices(model, ch->pose(), n.position, target);
                n.ragdoll.setModel(model);
                n.ragdoll.setTarget(target);
                worldBoneMatrices(model, ch->pose(), at, drawn);
                if (n.ragdoll.renderBones(alpha, drawn, out)) ch->setWorldPose(out);
                else ch->clearWorldPose();
            }
            for (auto it = npcBodies.begin(); it != npcBodies.end();)
                if (!alive.count(it->first)) it = npcBodies.erase(it); else ++it;
        }

        // Characters and loose objects are lit by the baked light
        // where they stand
        auto lightCharacters = [&](glm::vec3 feet) {
            glm::vec3 at = feet + glm::vec3(0.0f, 9.0f, 0.0f);
            uint16_t L = streamer->lightAt(at.x, at.y, at.z);
            float sky = light::skyOf(L) / 15.0f;
            const SkyState& ss = gFrameLight.sky;
            charRenderer.lightDir  = ss.lightDir();
            float shade = streamer->canopyAt(at.x, at.y, at.z);
            glm::vec3 leafTint = glm::mix(glm::vec3(1.0f), glm::vec3(0.30f, 0.50f, 0.24f), shade);
            charRenderer.skyTint   = ss.skyLight * leafTint * (sky * sky) + ss.ambient * 4.0f;
            glm::vec3 b(light::rOf(L) / 15.0f, light::gOf(L) / 15.0f, light::bOf(L) / 15.0f);
            charRenderer.blockTint = b * b * 1.3f;
        };

        // ── Woodcutting: logs, wood on the ground, leaves and
        //    chips (world/Woodcutting.h), drawn like the world ─
        if (wood && glr->hasAtlas()) {
            glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                              Camera::NEAR_PLANE, Camera::FAR_PLANE);
            glm::mat4 view = glr->getViewMatrix();
            float alpha = physicsTime / PHYSICS_STEP;
            // the baked light at a point, as the chunk shader reads it
            PropLightFn lightAt = [&](glm::vec3 p) {
                uint16_t L = streamer->lightAt(p.x, p.y, p.z);
                PropLightSample s;
                s.r = (uint8_t)light::rOf(L); s.g = (uint8_t)light::gOf(L); s.b = (uint8_t)light::bOf(L);
                s.sky = (uint8_t)light::skyOf(L);
                s.shade = std::clamp(streamer->canopyAt(p.x, p.y, p.z), 0.0f, 1.0f);
                return s;
            };
            props.begin(view, proj, camera.position, glr->getAtlas());
            std::unordered_map<uint32_t, bool> alive;
            for (const auto& lg : wood->allLogs()) {
                alive[lg->id] = true;
                PropRenderer::Mesh& m = logMeshes[lg->id];
                if (!m.count) props.buildVoxels(m, lg->body.voxels());
                glm::mat4 M = lg->body.transform(lg->body.settled() ? 1.0f : alpha);
                if (m.litFor != lg->moved) { props.relight(m, lg->body.transform(), lightAt); m.litFor = lg->moved; }
                props.draw(m, M);
            }
            for (auto it = logMeshes.begin(); it != logMeshes.end();)
                if (!alive.count(it->first)) { it->second.release(); it = logMeshes.erase(it); }
                else ++it;
            for (const auto& d : wood->itemDrops()) {         // wood: short lengths of log
                uint16_t blk = gItems.get(d.item).blockID;
                if (!blk) continue;
                float turn = d.resting ? (float)((d.item * 37 + (int)(d.pos.x * 7)) % 360) : d.age * 160.0f;
                glm::mat4 M = glm::translate(glm::mat4(1.0f), d.pos);
                M = glm::rotate(M, glm::radians(turn), glm::vec3(0, 1, 0));
                M = glm::scale(M, glm::vec3(1.8f, 1.2f, 1.2f));
                props.drawBlock(blk, M, lightAt(d.pos + glm::vec3(0, 0.8f, 0)));
            }
            for (const auto& pt : wood->allParticles()) {     // leaves are flat, chips chunky
                float fade = std::min(1.0f, pt.life / (0.3f * pt.maxLife));
                glm::mat4 M = glm::translate(glm::mat4(1.0f), pt.p);
                M = glm::rotate(M, pt.spin * pt.life, glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f)));
                glm::vec3 sc = pt.drag > 0.0f ? glm::vec3(1.0f, 0.18f, 1.0f) : glm::vec3(1.0f);
                M = glm::scale(M, sc * pt.size * fade);
                props.drawBlock(pt.block, M, lightAt(pt.p));
            }
            props.end();
            // "+3 Log" when wood is picked up
        }
        if (rules.hasBody &&
            controller.usingThirdPerson(rules) &&
            controller.bodyVisible()) {
            glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                              Camera::NEAR_PLANE, Camera::FAR_PLANE);
            glm::mat4 view = glr->getViewMatrix();
            lightCharacters(controller.renderPosition);   // from the baked layer where the body stands
            charRenderer.draw(*playerBody, proj * view, controller.renderPosition, camera.position,
                              OpenGLRenderer::FOG_COLOR, OpenGLRenderer::fogDensity);
            // toon edge lines (hidden behind anything in front)
            int fw = gRenderer->getScreenWidth(), fh = gRenderer->getScreenHeight();
            charRenderer.drawEdges({ { playerBody.get(), controller.renderPosition, gOutlineFx.glowFor(player) } }, view, proj,
                                   0, 0, fw, fh, fw, fh, edgeStyle());
        }

        // Enemies
        hud.enemyBars.clear();
        if (!npcs.all().empty()) {
            glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                              Camera::NEAR_PLANE, Camera::FAR_PLANE);
            glm::mat4 view = glr->getViewMatrix();
            float alpha = std::clamp(physicsTime / PHYSICS_STEP, 0.0f, 1.0f);
            std::vector<CharacterRenderer::Placed> placed;
            for (auto& np : npcs.all()) {
                auto it = npcBodies.find(np->uuid);
                if (it == npcBodies.end()) continue;
                glm::vec3 at = glm::mix(np->prevPosition, np->position, alpha);
                if (glm::length(at - camera.position) > 220.0f) continue;
                lightCharacters(at);
                charRenderer.draw(*it->second, proj * view, at, camera.position,
                                  OpenGLRenderer::FOG_COLOR, OpenGLRenderer::fogDensity);
                placed.push_back({ it->second.get(), at, gOutlineFx.glowFor(*np) });
                // a health bar over the head, once hurt (or always, close)
                float dist = glm::length(at - camera.position);
                if (!np->dead() && (np->sinceHurt < 8.0f || dist < 40.0f) && dist < 120.0f) {
                    glm::vec4 c = proj * view * glm::vec4(at + np->ragdoll.headOffset(alpha) + glm::vec3(0.0f, Human::STAND_HEIGHT + 2.5f, 0.0f), 1.0f);
                    if (c.w > 0.1f) {
                        float sx = (c.x / c.w * 0.5f + 0.5f) * gRenderer->getScreenWidth();
                        float sy = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * gRenderer->getScreenHeight();
                        float a = std::clamp(1.0f - (dist - 80.0f) / 40.0f, 0.0f, 1.0f);
                        hud.enemyBars.push_back({ sx, sy, np->health / np->maxHealth, a, np->sinceHurt < 0.3f });
                    }
                }
            }
            int fw = gRenderer->getScreenWidth(), fh = gRenderer->getScreenHeight();
            if (!placed.empty())
                charRenderer.drawEdges(placed, view, proj, 0, 0, fw, fh, fw, fh, edgeStyle());
        }

        // Debug overlay (F3): lines now, labels to the HUD
        hud.debugLabels.clear();
        if (gDebug.on) {
            if (player.swing.active()) {
                if (!wasSwinging) axeTrail.clear();
                if (player.swing.traceValid && axeTrail.size() < 120) axeTrail.push_back(player.swing.lastBlade);
            }
            wasSwinging = player.swing.active();
            glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                              Camera::NEAR_PLANE, Camera::FAR_PLANE);
            glm::mat4 vp = proj * glr->getViewMatrix();
            debugview::build(debugDraw, sim, wood.get(), player, axeTrail);
            debugDraw.flush(vp);
            float sw = (float)gRenderer->getScreenWidth(), shh = (float)gRenderer->getScreenHeight();
            for (auto& l : debugDraw.takeLabels()) {
                glm::vec4 c = vp * glm::vec4(l.at, 1.0f);
                if (c.w < 0.1f || glm::length(l.at - camera.position) > 160.0f) continue;
                float x = (c.x / c.w * 0.5f + 0.5f) * sw, y = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * shh;
                if (x < -200 || x > sw + 200 || y < -50 || y > shh + 50) continue;
                hud.debugLabels.push_back({ x, y, l.text, l.color });
            }
        } else debugDraw.clear();

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
        // Cube tools: what will break (orange) and what the
        // Cube Tool would fill (green)
        if (rules.worldInteraction && gState != GameState::DevMenu) {
            if (hands.cubeBreakShown)
                wire(glm::vec3(hands.cubeBreak.lo) - 0.03f, glm::vec3(hands.cubeBreak.hi) + 1.03f,
                     glm::vec3(1.0f, 0.55f, 0.15f));
            if (hands.cubePlaceShown)
                wire(glm::vec3(hands.cubePlace.lo) - 0.12f, glm::vec3(hands.cubePlace.hi) + 1.12f,
                     glm::vec3(0.3f, 0.95f, 0.4f));
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
        frameDt = dt;
        if (gState == GameState::Playing || gState == GameState::Console) gClock.update(dt);

        // Menus draw block icons from the world atlas (ui/ItemIcon.h)
        {
            const AtlasTexture& at = glr->getAtlas();
            gIconAtlas = { at.id, at.tileU(), at.tileV() };
        }

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

            // F3 — the debug overlay
            if (gInput.isPressed(InputAction::ToggleDebug)) {
                gDebug.on = !gDebug.on;
                say(gDebug.on ? "debug overlay on (console: debug)" : "debug overlay off");
            }

            // I — the inventory; X — swap weapon 1 / 2
            if (rulesFor(player.mode, false).hasBody) {
                if (gInput.isPressed(InputAction::Inventory)) {
                    inventoryScreen.setContext(&player);
                    inventoryScreen.onEnter();
                    inventoryOpenedFrame = true;     // the same I press must not close it
                    gState = GameState::Inventory;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                if (gInput.isPressed(InputAction::SwapWeapon)) {
                    Inventory& inv = player.activeInventory();
                    inv.swapWeapons();
                    const ItemStack& w = inv.weapon();
                    say(std::string(inv.activeWeapon ? "weapon 2: " : "weapon 1: ") +
                        (w.empty() ? "empty" : gItems.get(w.itemID).displayName));
                }
            }

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
                gMovesets.load("data/registry/movesets.toml");
                gHeldModels.loadDir("data/models/held");
                gHeldModels.bump();
                gFactions.load("data/registry/factions.toml");
                gNpcTypes.load("data/registry/npcs.toml", "data/characters/presets.toml");
                gAnims.load("data/animations", "data/animations/retarget.toml");
                gAnimMap.load("data/animations/anim_map.toml");
                gOutlineFx.load("data/registry/outline.toml");
                decor::current() = decor::Loader::load(gRegistry, "data/registry/decorations.toml");
                gItems.addDecor(*decor::current());
                gStructures.load("data/registry/structures.toml"); gStructures.addDecorTrees(*decor::current());
                blockTable = BlockRenderTable::build(
                    gRegistry, gResourcePacks);
                if (streamer)
                    streamer->setRenderTable(blockTable);
                props.setBlocks(blockTable);
                dropWoodMeshes();
                if (streamer) streamer->setLightTable(lightTable());
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
                props.setBlocks(blockTable);
                dropWoodMeshes();
                // Faces and swatches may have changed too
                loadCharacterArt();
                playerBody->setAppearance(player.appearance, hairMesh(), true);
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
            // View distances from the settings (they can change
            // while the game runs: the streamer and the far view
            // simply adjust)
            MESH_DISTANCE = gSettings.chunkDistanceInt();
            Human::eyeStand = gSettings.eyeHeight;
            {
                float farVox = gSettings.renderAll() ? 0.0f : gSettings.renderDistance * VOXEL_DIM;
                if (farView) farView->setMaxDistance(farVox);
                // a short render distance thickens the fog so
                // the land fades out instead of ending in a line
                OpenGLRenderer::fogDensity = farVox > 0.0f
                    ? std::max(OpenGLRenderer::FOG_DENSITY, 2.6f / farVox)
                    : OpenGLRenderer::FOG_DENSITY;
            }
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
                           mainMenu.getNewWorldSize(),
                           mainMenu.getNewWorldBiome(),
                           mainMenu.getNewWorldTerrain(),
                           mainMenu.getNewWorldParams());
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
                    glm::vec3(0.0f, Human::eyeStand, 0.0f);

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

                // 2. The simulation, in fixed 1/60 s ticks. A
                //    fast frame may run none, a slow one several.
                //    This player's intent goes in like any other
                //    player's would over the network.
                physicsTime = std::min(physicsTime + dt, 0.25f);
                while (physicsTime >= PHYSICS_STEP) {
                    controller.beforeTick(player);
                    sim.setIntent(player.uuid, controller.intent);
                    sim.step(PHYSICS_STEP);
                    controller.afterTick(player);
                    physicsTime -= PHYSICS_STEP;
                }

                // 3. What happened, as it concerns this player
                gOutlineFx.update(frameDt);
                for (const SimEvent& e : sim.takeEvents()) {
                    gOutlineFx.onEvent(e);       // flashing outlines (hurt, blocked …)
                    char b[96];
                    switch (e.kind) {
                    case SimEvent::Kind::Killed:
                        if (e.b == player.uuid) say("You died");
                        else if (e.a == player.uuid) say("enemy killed");
                        break;
                    case SimEvent::Kind::Hit:
                        if (e.a == player.uuid) {
                            std::snprintf(b, sizeof(b), e.blocked ? "%s blocked (%.0f)" : "%s hit %.0f",
                                          e.attack == AttackKind::None ? "tool" : combat::kindName(e.attack), e.amount);
                            hands.notice = b; hands.noticeAge = 0.0f;
                        }
                        break;
                    case SimEvent::Kind::Respawned:
                        if (e.b == player.uuid) controller.snap(player);
                        break;
                    case SimEvent::Kind::EncounterStarted:
                    case SimEvent::Kind::EncounterMerged:
                        if (auto* en = sim.encounters().byId(e.encounter); en && en->has(en->players, player.uuid)) {
                            // the opening bookend: who you're up against
                            std::unordered_map<std::string, int> count;
                            for (auto& n : en->enemyNames) count[n]++;
                            std::string who;
                            for (auto& [n, c] : count) who += (who.empty() ? "" : ", ") + n + (c > 1 ? " x" + std::to_string(c) : "");
                            hud.encounterBanner = (e.kind == SimEvent::Kind::EncounterMerged ? "The fight grows: " : "Encounter: ") + who;
                            hud.encounterBannerAge = 0.0f;
                        }
                        break;
                    case SimEvent::Kind::EncounterEnded:
                        if (auto* en = sim.encounters().byId(e.encounter); en && en->has(en->players, player.uuid)) {
                            // the closing bookend: a review of the fight
                            const encounter::PlayerTally& t = en->tally[player.uuid];
                            hud.review.clear();
                            hud.review.push_back(t.died ? "Defeated" : t.escaped ? "You got away" : "Victory");
                            char b2[96];
                            std::snprintf(b2, sizeof(b2), "%.0f s   enemies %zu   killed %d (yours %d)   fled %d",
                                          en->ended - en->started, en->npcs.size(), en->killed, t.kills, en->fled);
                            hud.review.push_back(b2);
                            std::snprintf(b2, sizeof(b2), "damage dealt %.0f   taken %.0f", t.dealt, t.taken);
                            hud.review.push_back(b2);
                            std::snprintf(b2, sizeof(b2), "XP +%.0f%s", t.xp, en->players.size() > 1 ? "  (shared)" : "");
                            hud.review.push_back(b2);
                            hud.review.push_back(t.loot.empty() ? "loot: none yet (drop tables come later)" : "loot");
                            hud.reviewAge = 0.0f;
                        }
                        break;
                    case SimEvent::Kind::MenuClosed:
                        if (e.a == talkingTo) {
                            if (Npc* n = npcs.find(e.a)) say(n->name + " stopped talking");
                            talkingTo = 0;
                        }
                        break;
                    case SimEvent::Kind::Pickup:
                        if (e.b == player.uuid) {
                            std::snprintf(b, sizeof(b), "+%d %s", e.count, gItems.get(e.item).displayName.c_str());
                            hands.notice = b; hands.noticeAge = 0.0f;
                        }
                        break;
                    }
                }

                hud.frameDt = dt;

                // Debug overlay text (F3)
                hud.debugText.clear();
                if (gDebug.on) {
                    char b[160];
                    auto line = [&](const char* fmt, auto... v) { std::snprintf(b, sizeof(b), fmt, v...); hud.debugText.push_back(b); };
                    line("frame %.1f ms  (%.0f fps)  tick %.0f Hz", frameTime * 1000.0f, 1.0f / std::max(frameTime, 1e-4f), 1.0f / PHYSICS_STEP);
                    line("pos %.1f %.1f %.1f   chunk %d %d", player.position.x, player.position.y, player.position.z,
                         (int)std::floor(player.position.x / VOXEL_DIM), (int)std::floor(player.position.z / VOXEL_DIM));
                    line("vel %.1f %.1f %.1f  %s  facing %.0f deg", player.velocity.x, player.velocity.y, player.velocity.z,
                         player.grounded ? "grounded" : "air", glm::degrees(std::atan2(player.facing.x, -player.facing.z)));
                    line("hp %.0f/%.0f  stamina %.0f/%.0f  mass %.0f kg  weapon %.0f", player.health, player.maxHealth,
                         player.stamina, player.maxStamina, player.mass, player.weaponDamage);
                    line("bodies %zu (npcs %zu)  logs %zu  drops %zu", sim.bodies().size(), npcs.all().size(),
                         wood ? wood->allLogs().size() : (size_t)0, wood ? wood->itemDrops().size() : (size_t)0);
                    glm::vec3 eye = camera.position;
                    RaycastResult r = gPhysics->raycast(eye, camera.front, 60.0f);
                    if (r.hit) {
                        glm::vec3 lp = glm::vec3(r.blockPos) + 0.5f + r.normal;
                        uint16_t L = streamer ? streamer->lightAt(lp.x, lp.y, lp.z) : 0;
                        line("aim %d %d %d  %s  light sky %d rgb %d %d %d  canopy %.2f", r.blockPos.x, r.blockPos.y, r.blockPos.z,
                             gRegistry.get((uint16_t)r.blockID).name.c_str(), light::skyOf(L), light::rOf(L), (L >> 8) & 15, (L >> 4) & 15,
                             streamer ? streamer->canopyAt(lp.x, lp.y, lp.z) : 0.0f);
                    }
                    line("layers: %s", gDebug.list().c_str());
                }

                // 4. Camera to the player's eyes — or behind
                //    them in third person — blended between
                //    the last two ticks
                controller.updateCamera(player, camera,
                    physicsTime / PHYSICS_STEP, dt,
                    gPhysics.get());

                // E: talk to the NPC in front of you (it waits in
                // its 'unavailable' state until you leave)
                if (gInput.isPressed(InputAction::Interact)) {
                    if (talkingTo) { sim.closeMenu(talkingTo); }
                    else {
                        Npc* best = nullptr; float bestD = 10.0f;
                        for (auto& n : npcs.all()) {
                            glm::vec3 d = n->position + glm::vec3(0, 9, 0) - camera.position;
                            float dist = glm::length(d);
                            if (dist > 40.0f || glm::dot(d / std::max(dist, 1e-3f), camera.front) < 0.9f) continue;
                            float flat = glm::length(glm::vec2(n->position.x - player.position.x, n->position.z - player.position.z));
                            if (flat < bestD) { bestD = flat; best = n.get(); }
                        }
                        if (best) {
                            if (sim.openMenu(best->uuid, player.uuid)) { talkingTo = best->uuid; say("talking to " + best->name + " (E to leave)"); }
                            else say(best->name + " won't talk now");
                        }
                    }
                }
                hud.talkingTo.clear();
                if (talkingTo) { if (Npc* n = npcs.find(talkingTo)) hud.talkingTo = n->name; else talkingTo = 0; }

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
                    say, &npcs, &sim };
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

        } else if (gState == GameState::Inventory) {

            drawWorld();
            hud.draw(*gRenderer->getUIRenderer(),
                     player, controller, hands, dev);
            if (gInput.isPressed(InputAction::SwapWeapon)) player.activeInventory().swapWeapons();
            ScreenResult r = inventoryScreen.update(*gRenderer->getUIRenderer(), dt);
            bool justOpened = inventoryOpenedFrame;
            inventoryOpenedFrame = false;
            if (!justOpened && (r == ScreenResult::CloseMenu ||
                gInput.isPressed(InputAction::Inventory) ||
                gInput.isPressed(InputAction::Pause))) {
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

        } else if (gState == GameState::Character) {

            drawWorld();
            UIRenderer* ui = gRenderer->getUIRenderer();
            // Esc while typing a favorite's name only leaves the box
            bool typing = characterScreen.escLeftTyping();
            ScreenResult r = characterScreen.update(*ui, dt);
            bool esc = gInput.isPressed(InputAction::Pause) && !typing;
            if (esc) characterScreen.cancel();

            // The turning preview, drawn into the screen's
            // empty right-hand panel
            CharacterScreen::Rect pr = characterScreen.previewRect();
            if (pr.w > 0) {
                previewBody->setAppearance(characterScreen.look(), hairMesh());
                AnimInput idle;
                idle.facingYawDeg = 90.0f;     // model faces +Z, toward the preview camera
                // the Animations tab: a game state or a clip
                CharacterScreen::AnimView av = characterScreen.animPreview();
                if (av.active && av.state >= 0) idle = animpreview::stateInput(av.state, av.time);
                if (av.active && av.clip >= 0) { idle.previewClip = av.clip; idle.previewTime = av.time; }
                // what the body holds for it: the sword for combat, the axe for the swing
                std::string held;
                if (av.active && (idle.combat || idle.attack)) held = "sword";
                if (av.active && idle.swing >= 0.0f) held = "axe";
                if (av.active && av.clip >= 0) {
                    const std::string& n = gAnims.info(av.clip).name;
                    if (n.find("Sword") != std::string::npos) held = "sword";
                    if (n.find("TreeChopping") != std::string::npos) held = "axe";
                }
                previewBody->setHeldModel(held);
                previewBody->animate(idle, dt);
                charRenderer.drawPreview(*previewBody, pr.x, pr.y, pr.w, pr.h,
                                         gRenderer->getScreenWidth(), gRenderer->getScreenHeight(),
                                         characterScreen.previewTurn(), edgeStyle(),
                                         characterScreen.previewFocus());
            }

            if (r == ScreenResult::CloseMenu || esc) {
                if (characterScreen.wasAccepted()) {
                    player.appearance = characterScreen.look();
                    saveDefaultCharacter(player.appearance);
                    say("character saved");
                }
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

            } else if (r == ScreenResult::OpenCharacter) {
                characterScreen.begin(player.appearance);
                previewBody->resetAnimation();
                gState = GameState::Character;

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
    playerBody.reset();
    previewBody.reset();
    charRenderer.cleanup();
    dropWoodMeshes();
    props.cleanup();
    skyRenderer.cleanup();
    distantLights.cleanup();
    editor.reset();
    gPhysics.reset();
    world.reset();
    gen.reset();
    gRenderer->shutdown();
    gRenderer.reset();
    glfwTerminate();
    return 0;
}
