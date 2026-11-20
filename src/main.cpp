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
#include "game/DevKit.h"
#include "server/GameSession.h"
#include "client/ClientWorld.h"
#include "net/EnetTransport.h"
#include "game/LockOn.h"
#include "ui/ItemIcon.h"

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
#include "ui/StationScreen.h"
#include "game/Blueprint.h"
#include "game/Kits.h"
#include "game/Village.h"
#include "game/Villagers.h"
#include "game/Dialogue.h"
#include "ui/InventoryScreen.h"
#include "world/StructureManager.h"
#include "world/Chests.h"
#include "world/Woodcutting.h"
#include "sim/Simulation.h"
#include "game/DebugView.h"
#include "render/PropRenderer.h"
#include "render/RoofRenderer.h"
#include "render/BoxRenderer.h"
#include "worldgen/WorldType.h"
#include "render/Sky.h"
#include "world/WorldMemory.h"
#include "world/LightBake.h"
#include "world/WorldSaveManager.h"
#include "world/WorldEditor.h"
#include "world/WorldStreamer.h"
#include "audio/SoundDirector.h"

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
    Station,        // a crafting station's menu (E on a workbench …)
    Character,      // character creator
};

// Seconds between automatic saves while in a world
constexpr float AUTOSAVE_SECONDS = 180.0f;

// Game modes are per player, not global — see
// game/GameMode.h and Player::mode.

GameState gState    = GameState::MainMenu;

std::unique_ptr<IRenderer> gRenderer;

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
    gInput.loadPad("data/controls/pad.toml");          // the controller (written by the Controls screen)

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
    gMovesets.load("data/registry/movesets.toml");
    gHeldModels.loadDir("data/models/held");
    gFactions.load("data/registry/factions.toml");

    // Sound (audio/): how materials sound, then the device. No device:
    // the game runs silent
    sound::gMaterials.load("data/audio/materials.toml");
    sound::gMaterials.bind();
    sound::gAudio.init();
    gLoot.load("data/registry/loot.toml");
    gKits.load("data/registry/kits.toml");
    gNpcTypes.load("data/registry/npcs.toml", "data/characters/presets.toml"); gVillagers.load(); dialogue::gDialogue.load();
    gAnims.load("data/animations", "data/animations/retarget.toml");
    gAnimMap.load("data/animations/anim_map.toml");
    gOutlineFx.load("data/registry/outline.toml");
    decor::current() = decor::Loader::load(gRegistry, "data/registry/decorations.toml");
    gItems.addDecor(*decor::current());

    // Multi-block structures: building pieces, doors, trees
    gStructures.load("data/registry/structures.toml"); gStructures.addDecorTrees(*decor::current()); gBlueprints.load(); gBuildPalettes.load();
    buildItemIcons();   // tools, weapons and gear drawn from their models (ui/ItemIcon.h)

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
        BlockRenderTable::build(gRegistry, gResourcePacks, gSettings.glassStyle == 1);

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
    BoxRenderer blobs;                        // bodies with no skeleton (slimes): stacked boxes
    if (!blobs.init()) std::cerr << "Box renderer failed to start\n";
    PropRenderer props;                       // logs, wood, leaves: drawn like the world
    if (!props.init()) std::cerr << "Prop renderer failed to start\n";
    RoofRenderer roofs;                       // roofs drawn smooth, eaves overhanging (Settings → Roofs)
    if (!roofs.init()) std::cerr << "Roof renderer failed to start\n";
    DebugDraw debugDraw;                      // the debug overlay's lines (F3, game/DebugView.h)
    if (!debugDraw.init()) std::cerr << "Debug lines failed to start\n";
    std::vector<std::array<glm::vec3, 3>> axeTrail;   // the last axe swing's blade, for the overlay
    bool wasSwinging = false;
    props.setBlocks(blockTable); roofs.setBlocks(blockTable);
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
    StationScreen   stationScreen(window);
    InventoryScreen inventoryScreen(window);
    bool inventoryOpenedFrame = false;
    Hud             hud;
    Console         console;
    gMainMenu = &mainMenu;
    gConsole  = &console;
    gControls = &controlsScreen;
    gCharScreen = &characterScreen;
    // the Voice tab's test line: spoken from the preview (its mouth moves)
    characterScreen.speakTest = [](const Voice& v, const std::string& line) { sound::gSounds.sayPreview(v, line); };

    // Messages from commands, tools and saving go to the
    // console (and the terminal)
    auto say = [&console](const std::string& msg) {
        std::string m = keytext::plain(msg);     // {interact} → the key bound to it (ui/KeyText.h)
        console.print(m);
        std::cout << m << "\n";
    };

    // ── Building and commands ─────────────────────────────
    // DevTools holds the dev operations shared by the tool
    // items and the console commands; PlayerInteraction is
    // the player's hands (break / place / use tool).
    DevTools          dev;
    PlayerInteraction hands;
    // its edits make their sounds (breaking, placing, doors …: audio/SoundDirector.h)
    hands.onRequest = [](const WorldRequest& q, uint16_t before, uint16_t placed) { sound::gSounds.onRequest(q, before, placed); };
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
    // The open world: the same GameSession the headless server
    // runs (server/GameSession.h). The names below are its parts.
    GameSession session(sim);
    session.log = say;
    auto& gen      = session.gen;
    auto& world    = session.world;
    auto& editor   = session.editor;    // all world edits
    auto& structs  = session.structs;   // doors, trees, buildings
    ChestStore& chests = session.chests;   // what each chest holds (world/Chests.h)
    auto& wood     = session.wood;      // felled trees, logs, wood on the ground
    auto& gPhysics = session.physics;
    // Meshes for woodcutting's logs (by log id), drawn like the
    // world (render/PropRenderer.h)
    std::unordered_map<uint32_t, PropRenderer::Mesh> logMeshes;
    PropRenderer::Mesh ghost;                 // the hammer's piece as it will be (a ghost)
    std::string ghostKey;
    glm::ivec3 ghostLitAt(1 << 30);
    auto dropWoodMeshes = [&]() {
        for (auto& [k, m] : logMeshes) m.release();
        logMeshes.clear();
    };
    // Enemies live in the simulation; their bodies to draw (by id)
    NpcManager& npcs = sim.npcs();
    EntityId talkingTo = 0;           // the NPC this player is in a menu with
    std::string talkLine;             // what it last said (game/Dialogue.h)
    float talkCloseIn = -1.0f;        // after goodbye: the menu closes in this many s
    uint32_t talkRng = 0x2f6b1d3u;
    bool pendingVillage = false;      // a new flat world with a village: built once you stand on the ground
    glm::ivec3 villageAt{ 0 };        // where its square goes (picked when you land)
    bool villageAtSet = false;
    int  villageWait = 0;             // frames waited for its ground to load
    bool pendingKit = false;          // a new dev world: lay out the showcase chests (game/DevKit.h)
    LockOn lockOn;                    // the camera held on an enemy (TAB: game/LockOn.h)
    std::unordered_map<uint64_t, std::unique_ptr<Character>> npcBodies;
    std::unique_ptr<WorldStreamer>    streamer;
    std::unique_ptr<FarTerrain>       farView;   // distant terrain
    float autosaveTimer = 0.0f;

    // ── Playing on a server (client/ClientWorld.h) ────────
    // 'remote' is the connection and the mirror of the server's
    // world, built into 'session' so everything below draws and
    // predicts on it like a local world. The local simulation
    // then holds only this player (its prediction) and PUPPETS:
    // the other players and NPCs, as Npcs that never think, moved
    // from the server's snapshots.
    std::unique_ptr<ClientWorld> remote;
    uint64_t localUuid = player.uuid;         // this player's id off line
    float remoteTime = 0.0f;                  // ticks owed while a menu is open
    std::unordered_map<uint64_t, bool> puppetDown;
    std::unordered_map<std::string, std::string> itemForModel;   // held model → an item that shows it
    auto syncPuppets = [&](float dtFrame) {
        if (!remote) return;
        if (itemForModel.empty())
            for (uint16_t id : gItems.inCategory(IC_TOOL | IC_WEAPON | IC_SPELLBOOK | IC_SHIELD | IC_DEV)) {
                const ItemDef& d = gItems.get(id);
                if (!d.heldModel.empty() && !itemForModel.count(d.heldModel)) itemForModel[d.heldModel] = d.name;
            }
        auto& list = npcs.all();
        list.erase(std::remove_if(list.begin(), list.end(), [&](auto& n) { return n->puppet && !remote->bodies.count(n->uuid); }), list.end());
        for (auto& [id, body] : remote->bodies) {
            Npc* n = npcs.find(id);
            if (!n) {
                auto made = std::make_unique<Npc>();
                made->uuid = id;
                made->puppet = made->untouchable = true;
                made->typeId = body.info.type;
                made->type = gNpcTypes.find(body.info.type);
                made->name = body.info.name;
                made->appearance = body.look;
                if (made->type) { made->faction = gFactions.find(made->type->faction); NpcManager::applyBody(*made, *made->type); }
                n = made.get();
                list.push_back(std::move(made));
                puppetDown[id] = false;
            }
            if (!n->type) {                      // a player: what it wears can change
                n->wearing.clear();
                std::istringstream gs(body.info.gear);
                for (std::string w; gs >> w;) n->wearing.push_back(w);
            }
            proto::BodyState st;
            if (!remote->sample(id, st)) continue;
            using B = proto::BodyState;
            n->position = n->prevPosition = st.pos;
            n->velocity = st.vel;
            n->facing = st.facing;
            if (st.health < n->health) n->sinceHurt = 0.0f; else n->sinceHurt += dtFrame;
            n->health = st.health; n->maxHealth = st.maxHealth;
            n->grounded = st.flags & B::F_GROUNDED; n->crouching = st.flags & B::F_CROUCH;
            n->sprinting = st.flags & B::F_SPRINT;  n->flying = st.flags & B::F_FLY;
            n->guarding = st.flags & B::F_GUARD;
            n->move = (Human::Move)st.move; n->moveTimer = st.moveTimer;
            auto im = itemForModel.find(st.held);
            n->weaponItem = im == itemForModel.end() ? std::string() : im->second;
            n->offModel = st.off;                // the left hand (a shield: its guard pose)
            {
                auto om = itemForModel.find(st.off);
                n->guardStyle.shield = om != itemForModel.end() && gItems.get(gItems.find(om->second)).shield;
            }
            const AttackDef* ad = st.attack.empty() ? nullptr : gMovesets.attack(st.attack);
            n->attack = AttackState();
            if (ad) { n->attack.kind = AttackKind::Light; n->attack.d = ad; n->attack.t = st.attackT; n->attack.dir = st.attackDir; }
            // knocked down or dead: it falls here too (for looks; the server decides what's hit)
            bool down = st.flags & (B::F_DEAD | B::F_LIMP);
            if (down && !puppetDown[id]) {
                n->ragdoll.stayDown = st.flags & B::F_DEAD;
                n->hit(n->bodyPoint(0.6f), -n->facing * 46.0f * n->mass);
            } else if (!down && puppetDown[id]) {
                n->ragdoll.stayDown = false;
                if (n->ragdoll.isLimp()) n->ragdoll.reset();
            }
            puppetDown[id] = down;
        }
    };
    // One tick on a server: this tick's input goes out, the local
    // simulation predicts it, the fallen bodies' ragdolls move
    auto remoteTick = [&](const HumanIntent& in) {
        if (!remote->groundReady()) return;      // wait with the server until the ground is here
        remote->sendInput(in, player.facing, camera.pitch);
        sim.setIntent(player.uuid, in);
        sim.step(PHYSICS_STEP);
        remote->afterPrediction();
        for (auto& n : npcs.all()) if (n->puppet && gPhysics) gPhysics->stepRagdoll(*n, PHYSICS_STEP);
    };

    // ── Save everything ───────────────────────────────────
    // Edited chunks (only those — untouched terrain is
    // rebuilt from the seed and comes out identical) and the
    // player file: position, view, mode, health, both
    // inventories, dev tool settings.
    //   Single player trusts itself: every save is committed
    //   (server/SaveStaging.h), the player file and the far
    //   view's memory going in with the rest.
    auto saveWorld = [&]() {
        if (!world || remote) return;        // a server's world is the server's to save
        session.save(true, [&](const std::string& stage) {
            memory.save(stage + "/lowres.dat");
            std::ofstream out(stage + "/player.dat");
            player.write(out);
            build.write(out);
            out << "look=" << camera.yaw << " " << camera.pitch << "\n";
            out << "thirdperson=" << (controller.thirdPersonWanted ? 1 : 0) << "\n";
            dev.write(out);
        });
        autosaveTimer = 0.0f;
        say("world saved");
    };

    // Leave the world: save (unless told not to: a dev world
    // left without saving), then tear everything down.
    // Streamer first: it stops the worker threads and frees
    // the GPU meshes before the world goes away.
    auto leaveWorld = [&](bool save) {
        pendingKit = false;
        pendingVillage = false;
        lockOn.clear();
        if (save) saveWorld();
        if (remote) remote->leave();
        streamer.reset();
        farView.reset();
        dev.attach(nullptr);
        hands.wood = nullptr;
        hands.remote = nullptr;
        dropWoodMeshes();
        npcBodies.clear();
        if (remote) {
            remote->receive();               // lets the goodbye out
            remote.reset();
            puppetDown.clear();
            player.uuid = localUuid;
            pauseMenu.setRemote(false);
        }
        session.close();
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
                          const WorldGenParams& genParams = WorldGenParams(),
                          const FlatOptions& flat = FlatOptions(),
                          bool devWorld = false) {     // in devworlds/
        streamer.reset();
        farView.reset();
        session.close();

        if (isNew) {
            std::cout << "Creating world: " << name
                      << " seed=" << seed << "\n";
            session.beginNew(name, seed, mode, type, size, biome, terrain, genParams, flat, devWorld);
        } else {
            std::cout << "Loading world: " << name << (devWorld ? " (dev world)" : "") << "\n";
            session.begin(name, devWorld);
            if (!session.recovered.empty()) say(session.recovered);
            seed = gWorldSave.getSeed();
            mode = gWorldSave.getDefaultMode();
            type = gWorldSave.getWorldType();
        }

        hud.devWorld = gWorldSave.isDevWorld();

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
            const bool baking = worldTypeShaped(type) && gWorldSave.getTerrainStyle() != TerrainStyle::NoErosion;
            gBakeProgress = 0.0f; gBakeStage = 0;
            std::atomic<bool> done{ false };
            std::thread worker([&] {
                session.buildGenerator();
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
        }
        // The rest of the world from its folder: voxels' home,
        // physics, structures, containers, logs, NPCs, the clock
        session.attach();

        // Chunks come in around the camera (a chunk the player
        // edited is read from the save folder, everything else is
        // rebuilt from the seed: GameSession::chunkSource)
        streamer = std::make_unique<WorldStreamer>(
            *world, session.chunkSource(), blockTable, lightTable());
        {   // where rivers run (the generator knows; the water moves that way)
            const TerrainGenerator* g = session.gen.get();
            streamer->flowAt = [g](int x, int z) { return g->flowAt(x, z); };
        }
        memory.load(gWorldSave.getWorldPath() + "/lowres.dat");
        memorySeen = 0;

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

        dropWoodMeshes();
        hands.wood = wood.get();
        hands.chests = &chests;
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
            // The starting kits (data/registry/kits.toml): the
            // player's own, and the dev tools for Test Play
            Kits::give(player.inventory, gKits.player, true);
            Kits::give(player.devInventory, gKits.dev, false);
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

    // ── Join a server ─────────────────────────────────────
    // Connects (a screen shows how it's going; Esc gives up),
    // builds the same world from the settings the server sends,
    // and starts playing on the mirror. Returns "" or what went
    // wrong.
    auto joinServer = [&](const std::string& address, const std::string& name) -> std::string {
        streamer.reset();
        farView.reset();
        session.close();
        std::string host; int port;
        net::splitAddress(address, host, port);
        auto conn = net::enetConnect(host, port);
        if (!conn) return "couldn't look up " + host;
        player.resetForNewWorld();
        player.inventory = Inventory();
        player.appearance = loadDefaultCharacter();
        localUuid = player.uuid;
        remote = std::make_unique<ClientWorld>(conn, "servercache", &player, &session);
        remote->join(name, player.appearance);
        hands.remote = [&remote](const WorldRequest& q) { if (remote) remote->request(q); };
        auto screen = [&](const std::string& title, const std::string& line, float bar) {
            glfwPollEvents();
            gRenderer->beginFrame();
            UIRenderer* ui = gRenderer->getUIRenderer();
            const auto& theme = gResourcePacks.getTheme();
            float sw = (float)ui->getWidth(), sh = (float)ui->getHeight();
            ui->begin();
            ui->drawRect(0, 0, sw, sh, theme.bg);
            ui->drawTextCentered(title, 0, sh * 0.5f - 60, sw, 30, theme.title, 3);
            ui->drawTextCentered(line, 0, sh * 0.5f - 18, sw, 20, theme.textDim, 2);
            if (bar >= 0.0f) {
                float bw = std::min(520.0f, sw - 80.0f), bx = (sw - bw) * 0.5f, by = sh * 0.5f + 20;
                ui->drawRect(bx, by, bw, 14, theme.inputBg);
                ui->drawRect(bx, by, bw * std::clamp(bar, 0.0f, 1.0f), 14, theme.accent);
                ui->drawBorder(bx, by, bw, 14, theme.border, 1.0f);
            }
            ui->end();
            gRenderer->endFrame();
            glfwSwapBuffers(window);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        };
        auto fail = [&](const std::string& why) { remote.reset(); player.uuid = localUuid; session.close(); return why; };
        // 1. the answer
        while (remote->state == ClientWorld::State::Connecting) {
            remote->receive();
            screen("Joining " + address, "Esc: cancel", -1.0f);
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS || glfwWindowShouldClose(window)) return fail("cancelled");
        }
        if (remote->state == ClientWorld::State::Refused) return fail(remote->refusedWhy);
        if (remote->state != ClientWorld::State::Joined) return fail("no answer from " + address);
        // 2. the same world, from its settings (island worlds bake:
        //    on a worker, the connection kept alive meanwhile)
        {
            gBakeProgress = 0.0f; gBakeStage = 0;
            std::atomic<bool> done{ false };
            std::thread worker([&] { remote->buildGenerator(); done = true; });
            while (!done) {
                remote->keepAlive();
                screen("Shaping the land", bakeStageName(gBakeStage), gBakeProgress.load());
            }
            worker.join();
        }
        std::cout << "Joined " << address << " as " << name << ": " << worldTypeName(gWorldSave.getWorldType()) << " world, "
                  << WORLD_SIZE_X << " x " << WORLD_SIZE_Z << " chunks\n";
        hud.devWorld = false;
        pauseMenu.setRemote(true);
        streamer = std::make_unique<WorldStreamer>(*world, remote->chunkSource(), blockTable, lightTable());
        if (const TerrainGenerator* g = remote->gen.get()) streamer->flowAt = [g](int x, int z) { return g->flowAt(x, z); };
        // columns the server has changed come from the server, not the seed
        streamer->skipLoad = [&](int cx, int cz) { return remote && remote->isModified(cx, cz); };
        memory.load(remote->cacheFolder() + "/lowres.dat");
        memorySeen = 0;
        {
            auto colors = std::make_shared<FarTerrain::ColorTable>(blockTable->blocks.size());
            for (size_t id = 0; id < blockTable->blocks.size(); id++) {
                const BlockRenderInfo& info = blockTable->get((uint16_t)id);
                (*colors)[id] = glr->tileAverage(info.tileX[FACE_TOP], info.tileY[FACE_TOP]);
            }
            farView = std::make_unique<FarTerrain>(*gen, WORLD_SIZE_X, WORLD_SIZE_Z, colors);
            if (!farView->initGL()) farView.reset();
        }
        dropWoodMeshes();
        hands.wood = wood.get();
        hands.chests = &chests;
        build.reset();
        dev.resetForNewWorld();
        dev.attach(nullptr);
        hands.clearTarget();
        playerBody->resetAnimation();
        player.spawnPoint = glm::vec3(gen->spawnPoint(WORLD_SIZE_X, WORLD_SIZE_Z));
        camera = Camera();
        controller.thirdPersonWanted = false;
        camera.position = player.position + glm::vec3(0.0f, Human::eyeStand, 0.0f);
        controller.reset(player);
        physicsTime = 0.0f;
        remoteTime = 0.0f;
        say("joined " + address + " as " + name);
        gState = GameState::Playing;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        camera.firstMouse = true;
        gInput.resetMouseDelta(window);
        return "";
    };
    // Back to the main menu's Join page, saying why
    auto dropToMenu = [&](const std::string& why) {
        leaveWorld(false);
        gState = GameState::MainMenu;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetWindowTitle(window, "VoxelRPG");
        mainMenu.onEnter();
        mainMenu.showJoinError(why);
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
                              streamer->getLightTexture(cx, sy, cz), streamer->getCanopyTexture(cx, cz),
                              streamer->getFlowTexture(cx, cz), streamer->getTintTexture(cx, sy, cz));
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
            playerBody->setAppearance(player.appearance, hairMesh(), false, gearLooksOf(player.activeInventory()));
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
            {
                // what the simulation says is in hand (a spellbook while it casts)
                std::string held; bool fighting = false;
                Simulation::handsOf(player, held, fighting);
                playerBody->setHeldModel(held);
            }
            // the left hand: the active off-hand slot
            // (in combat: the loadout's off hand, a shield …)
            playerBody->setOffhandModel(Simulation::offHandOf(player));
            playerBody->setTalk(sound::gAudio.mouthOf(player.uuid));
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
                if (sw.active() && editor && structs) {    // (on a server, what it hits is a request: hands.axeHit)
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
                if (n.type && !n.type->person()) continue;         // a slime: no skeleton (drawn below)
                alive[n.uuid] = true;
                auto& ch = npcBodies[n.uuid];
                if (!ch) ch = std::make_unique<Character>();
                {
                    std::vector<GearLook> gear;
                    for (const std::string& w : n.type ? n.type->wear : n.wearing)
                        if (uint16_t id = gItems.find(w); id && !gItems.get(id).gear.shape.empty()) gear.push_back(gItems.get(id).gear);
                    ch->setAppearance(n.appearance, hairMesh(), false, gear);
                }
                {
                    uint16_t wid = gItems.find(n.weaponItem);
                    ch->setHeldModel(wid ? gItems.get(wid).heldModel : std::string());
                    ch->setOffhandModel(Simulation::offHandOf(n));
                }
                ch->setTalk(sound::gAudio.mouthOf(n.uuid));        // speaking: its mouth moves (audio)
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

        // A body with no skeleton (npcs.toml body: a slime): three
        // stacked boxes for a rounded blob, two eyes on the side it
        // faces. Squashed on the ground (harder just after landing
        // and just before a hop), stretched in the air, flat when
        // dead; lit by the baked light where it is
        auto drawBlob = [&](const Npc& n, glm::vec3 at, const glm::mat4& vp) {
            const NpcType& t = *n.type;
            float w = t.bodyWidth > 0.0f ? t.bodyWidth : n.hitRadius * 2.0f, h = n.standHeight;
            float sy = 1.0f;
            if (n.dead()) sy = 0.35f;
            else if (!n.grounded) sy = 1.0f + std::clamp(std::fabs(n.velocity.y) / 45.0f, 0.0f, 0.35f);
            else if (n.hop.on && n.hop.every > 0.0f && !n.puppet) {     // (a server's slime: its hop timer isn't sent)
                float f = std::clamp(n.hop.wait / n.hop.every, 0.0f, 1.0f);    // 1 just landed .. 0 about to hop
                sy = 1.0f - 0.24f * f * f * f * f - 0.18f * (1.0f - f) * (1.0f - f) * (1.0f - f) * (1.0f - f);
            }
            float sx = 1.0f / std::sqrt(sy);                                  // the same volume
            uint16_t L = streamer ? streamer->lightAt(at.x, at.y + h * 0.5f, at.z) : (uint16_t)0xFFFF;
            float sky = light::skyOf(L) / 15.0f, blk = std::max({ light::rOf(L), light::gOf(L), light::bOf(L) }) / 15.0f;
            float lit = std::clamp(std::max(sky * gFrameLight.sky.skyLight.y * 1.1f + 0.12f, blk), 0.12f, 1.0f);
            glm::vec3 col = t.color * lit * (n.dead() ? 0.55f : 1.0f);
            if (n.sinceHurt < 0.15f && !n.dead()) col = glm::mix(col, glm::vec3(1.0f), 0.6f);   // a flash when hit
            glm::vec3 f(n.facing.x, 0.0f, n.facing.z);
            f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(1, 0, 0);
            float yaw = glm::degrees(std::atan2(f.z, f.x));
            float H = h * sy, W = w * sx;
            // the outline first (the toon edge lines, like the characters')
            const glm::vec3 LINE(0.03f, 0.04f, 0.03f);
            const float T = 0.28f;
            blobs.drawOutline(vp, at, { W, H * 0.55f, W }, yaw, LINE, T);
            blobs.drawOutline(vp, at + glm::vec3(0, H * 0.55f, 0), { W * 0.78f, H * 0.3f, W * 0.78f }, yaw, LINE, T);
            blobs.drawOutline(vp, at + glm::vec3(0, H * 0.85f, 0), { W * 0.45f, H * 0.15f, W * 0.45f }, yaw, LINE, T);
            blobs.draw(vp, at, { W, H * 0.55f, W }, yaw, col);
            blobs.draw(vp, at + glm::vec3(0, H * 0.55f, 0), { W * 0.78f, H * 0.3f, W * 0.78f }, yaw, col * 1.06f);
            blobs.draw(vp, at + glm::vec3(0, H * 0.85f, 0), { W * 0.45f, H * 0.15f, W * 0.45f }, yaw, col * 1.12f);
            if (n.dead()) return;
            glm::vec3 right(-f.z, 0.0f, f.x);
            for (float side : { -1.0f, 1.0f })
                blobs.draw(vp, at + f * (W * 0.5f) + right * (side * W * 0.2f) + glm::vec3(0, H * 0.42f, 0),
                           { 0.5f, std::max(0.6f, H * 0.2f), 0.6f }, yaw, glm::vec3(0.05f, 0.07f, 0.05f));
        };

        // ── Roofs: one sloped surface over each roof piece's
        //    steps, eaves overhanging (render/RoofRenderer.h) ─
        if (gSettings.roofStyle == 1 && structs && streamer && glr->hasAtlas()) {
            glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                              Camera::NEAR_PLANE, Camera::FAR_PLANE);
            PropLightFn lightAt = [&](glm::vec3 p) {
                uint16_t L = streamer->lightAt(p.x, p.y, p.z);
                PropLightSample s;
                s.r = (uint8_t)light::rOf(L); s.g = (uint8_t)light::gOf(L); s.b = (uint8_t)light::bOf(L);
                s.sky = (uint8_t)light::skyOf(L);
                s.shade = std::clamp(streamer->canopyAt(p.x, p.y, p.z), 0.0f, 1.0f);
                return s;
            };
            roofs.draw(*structs, glr->getViewMatrix(), proj, camera.position, glr->getAtlas(), lightAt);
        }

        // ── The hammer's piece, as it will be: a ghost of its voxels,
        //    so the outside (timber, inlay) and the inside (paint,
        //    panels) can be told apart before it's placed ─
        if (hands.hammerActive && hands.previewHas && rules.worldInteraction && build.mode == HammerMode::Piece &&
            streamer && glr->hasAtlas()) {
            if (const StructureType* t = build.type()) {
                std::string key = std::to_string(t->id) + "|" + build.shownSpec + "|" + std::to_string(build.shownRot);
                const VoxelVolume& v = gStructures.shape(t->id, build.shownSpec, 0, build.shownRot);
                if (key != ghostKey) {
                    std::vector<VoxelPiece> pieces;
                    for (int y = 0; y < v.size.y; y++) for (int z = 0; z < v.size.z; z++) for (int x = 0; x < v.size.x; x++)
                        if (uint16_t b = v.at(x, y, z)) pieces.push_back({ { x, y, z }, b });
                    props.buildVoxels(ghost, pieces);
                    ghostKey = key;
                    ghostLitAt = glm::ivec3(1 << 30);
                }
                glm::ivec3 lo = hands.previewSpot - v.anchor;
                glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(lo));
                glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                                  Camera::NEAR_PLANE, Camera::FAR_PLANE);
                props.begin(glr->getViewMatrix(), proj, camera.position, glr->getAtlas());
                if (lo != ghostLitAt) {
                    PropLightFn lightAt = [&](glm::vec3 p) {
                        uint16_t L = streamer->lightAt(p.x, p.y, p.z);
                        PropLightSample s;
                        s.r = (uint8_t)light::rOf(L); s.g = (uint8_t)light::gOf(L); s.b = (uint8_t)light::bOf(L);
                        s.sky = (uint8_t)std::max(8, (int)light::skyOf(L));   // (inside the ghost the world is solid)
                        s.shade = 0.0f;
                        return s;
                    };
                    props.relight(ghost, M, lightAt);
                    ghostLitAt = lo;
                }
                props.drawGhost(ghost, M, hands.previewWhy.empty() ? 0.72f : 0.4f);
                props.end();
            }
        }

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
        hud.lockMarker.on = false;
        if (!npcs.all().empty()) {
            glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                              Camera::NEAR_PLANE, Camera::FAR_PLANE);
            glm::mat4 view = glr->getViewMatrix();
            float alpha = std::clamp(physicsTime / PHYSICS_STEP, 0.0f, 1.0f);
            std::vector<CharacterRenderer::Placed> placed;
            for (auto& np : npcs.all()) {
                const bool blob = np->type && !np->type->person();
                auto it = npcBodies.find(np->uuid);
                if (it == npcBodies.end() && !blob) continue;
                glm::vec3 at = glm::mix(np->prevPosition, np->position, alpha);
                if (glm::length(at - camera.position) > 220.0f) continue;
                if (blob) drawBlob(*np, at, proj * view);
                else {
                    lightCharacters(at);
                    charRenderer.draw(*it->second, proj * view, at, camera.position,
                                      OpenGLRenderer::FOG_COLOR, OpenGLRenderer::fogDensity);
                    placed.push_back({ it->second.get(), at, gOutlineFx.glowFor(*np) });
                }
                const float top = blob ? np->height : Human::STAND_HEIGHT;     // for the bar and the marker
                const glm::vec3 head = blob ? glm::vec3(0.0f) : np->ragdoll.headOffset(alpha);
                // a health bar over the head, once hurt (or always, close)
                float dist = glm::length(at - camera.position);
                if (!np->dead() && (np->sinceHurt < 8.0f || dist < 40.0f) && dist < 120.0f) {
                    glm::vec4 c = proj * view * glm::vec4(at + head + glm::vec3(0.0f, top + 2.5f, 0.0f), 1.0f);
                    if (c.w > 0.1f) {
                        float sx = (c.x / c.w * 0.5f + 0.5f) * gRenderer->getScreenWidth();
                        float sy = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * gRenderer->getScreenHeight();
                        float a = std::clamp(1.0f - (dist - 80.0f) / 40.0f, 0.0f, 1.0f);
                        hud.enemyBars.push_back({ sx, sy, np->health / np->maxHealth, a, np->sinceHurt < 0.3f });
                    }
                }
                if (np->uuid == lockOn.target && !np->dead()) {     // the lock-on marker, on its chest
                    glm::vec4 c = proj * view * glm::vec4(at + head + glm::vec3(0.0f, top * 0.6f, 0.0f), 1.0f);
                    if (c.w > 0.1f)
                        hud.lockMarker = { true, (c.x / c.w * 0.5f + 0.5f) * gRenderer->getScreenWidth(),
                                           (1.0f - (c.y / c.w * 0.5f + 0.5f)) * gRenderer->getScreenHeight() };
                }
            }
            int fw = gRenderer->getScreenWidth(), fh = gRenderer->getScreenHeight();
            if (!placed.empty())
                charRenderer.drawEdges(placed, view, proj, 0, 0, fw, fh, fw, fh, edgeStyle());
        }

        // Spells in flight (Simulation::projectiles): an ice spike is
        // a pale shard along its way, a thinner point ahead
        if (!sim.projectiles().empty()) {
            glm::mat4 proj = glm::perspective(glm::radians(Camera::FOV_DEG), aspect,
                                              Camera::NEAR_PLANE, Camera::FAR_PLANE);
            glm::mat4 vp = proj * glr->getViewMatrix();
            float alpha = std::clamp(physicsTime / PHYSICS_STEP, 0.0f, 1.0f);
            for (const Simulation::Projectile& p : sim.projectiles()) {
                glm::vec3 at = p.pos + p.vel * (alpha * PHYSICS_STEP);
                glm::vec3 f(p.vel.x, 0.0f, p.vel.z);
                f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(1, 0, 0);
                float yaw = glm::degrees(std::atan2(f.z, f.x));
                glm::vec3 col = p.kind == "ice_spike" ? glm::vec3(0.72f, 0.90f, 1.0f) : glm::vec3(1.0f);
                blobs.draw(vp, at - glm::vec3(0.0f, 0.5f, 0.0f), { 3.0f, 1.0f, 1.0f }, yaw, col);
                blobs.draw(vp, at + f * 2.0f - glm::vec3(0.0f, 0.25f, 0.0f), { 1.2f, 0.5f, 0.5f }, yaw, col * 1.1f);
            }
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
        // Window glass, after everything solid (chunks, roofs, bodies, props), far sections first
        // (Mesher.h panes, OpenGLRenderer::beginPanes)
        {
            struct PaneSec { float d; int cx, sy, cz; };
            std::vector<PaneSec> paneSecs;
            for (int cx = pc.x - MESH_DISTANCE; cx <= pc.x + MESH_DISTANCE; cx++)
            for (int cz = pc.y - MESH_DISTANCE; cz <= pc.y + MESH_DISTANCE; cz++) {
                if (!streamer->hasMesh(cx, cz)) continue;
                for (int sy = 0; sy < SECTIONS; sy++) {
                    const ChunkMesh* mesh = streamer->getMesh(cx, sy, cz);
                    if (!mesh || !mesh->hasPanes()) continue;
                    glm::vec3 c = world->chunkOrigin(cx, cz) + glm::vec3(VOXEL_DIM * 0.5f, (sy + 0.5f) * VOXEL_DIM, VOXEL_DIM * 0.5f);
                    paneSecs.push_back({ glm::length(c - camera.position), cx, sy, cz });
                }
            }
            if (!paneSecs.empty()) {
                std::sort(paneSecs.begin(), paneSecs.end(), [](const PaneSec& a, const PaneSec& b) { return a.d > b.d; });
                glr->beginPanes();
                for (const PaneSec& p : paneSecs)
                    glr->drawPanes(*streamer->getMesh(p.cx, p.sy, p.cz),
                                   world->chunkOrigin(p.cx, p.cz) + glm::vec3(0.0f, (float)(p.sy * VOXEL_DIM), 0.0f),
                                   streamer->getLightTexture(p.cx, p.sy, p.cz), streamer->getCanopyTexture(p.cx, p.cz),
                                   streamer->getFlowTexture(p.cx, p.cz), streamer->getTintTexture(p.cx, p.sy, p.cz));
                glr->endPanes();
            }
        }
        if (hands.hammerActive && !hands.previewBoxes.empty() && rules.worldInteraction) {
            // a blueprint or a building to copy: every piece, and the whole
            glm::vec3 col = build.mode == HammerMode::Copy ? glm::vec3(1.0f, 0.85f, 0.3f)
                          : hands.previewWhy.empty() ? glm::vec3(0.3f, 0.95f, 0.4f) : glm::vec3(1.0f, 0.3f, 0.25f);
            glm::ivec3 lo(1 << 30), hi(-(1 << 30));
            for (auto& [a, b] : hands.previewBoxes) {
                wire(glm::vec3(a), glm::vec3(b) + 1.0f, col * 0.7f);
                lo = glm::min(lo, a); hi = glm::max(hi, b);
            }
            wire(glm::vec3(lo) - 0.05f, glm::vec3(hi) + 1.05f, col);
        } else if (hands.hammerActive && hands.previewHas && rules.worldInteraction && build.mode == HammerMode::Piece) {
            if (const StructureType* t = build.type()) {
                const VoxelVolume& v = gStructures.shape(t->id, build.shownSpec, 0, build.shownRot);
                glm::vec3 lo(hands.previewSpot - v.anchor);
                wire(lo, lo + glm::vec3(v.size),
                     hands.previewWhy.empty() ? glm::vec3(0.3f, 0.95f, 0.4f)
                                              : glm::vec3(1.0f, 0.3f, 0.25f));
                // a wall with an outside: an arrow out of its outside face
                // (the ghost below shows the faces themselves)
                if (gStructures.faced(*t, build.shownSpec)) {
                    glm::ivec2 o = StructureManager::outsideDir(build.shownRot);
                    glm::vec3 c = lo + glm::vec3(v.size) * 0.5f;
                    glm::vec3 out(o.x, 0, o.y);
                    glm::vec3 face = c + out * (glm::dot(glm::abs(out), glm::vec3(v.size)) * 0.5f);
                    glm::vec3 side(-out.z, 0, out.x);
                    glm::vec3 arrowCol(1.0f, 0.85f, 0.25f);
                    for (int k = 0; k < 6; k++)                    // the shaft: a run of small boxes
                        wire(face + out * (k + 0.25f) - 0.25f, face + out * (k + 0.25f) + 0.25f, arrowCol);
                    for (int k = 1; k <= 3; k++)                   // the head
                        for (float sd : { -1.0f, 1.0f }) {
                            glm::vec3 q = face + out * (6.0f - k) + side * (sd * k);
                            wire(q - 0.25f, q + 0.25f, arrowCol);
                        }
                }
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

    // ── The pause menu's buttons, from either place ─────────
    // Esc's pause menu and the inventory's left panel (the pad's
    // way there: Select + Start) offer the same buttons
    // (ui/PauseMenuScreen.h pauseButtons). Controls, Settings,
    // Character and the Dev Menu go back to where they came from.
    GameState menuReturn = GameState::Paused;
    auto backToMenu = [&]() {
        if (menuReturn == GameState::Inventory) {
            inventoryScreen.setContext(&player);
            inventoryScreen.onEnter();
            inventoryOpenedFrame = true;
            gState = GameState::Inventory;
        } else {
            gState = GameState::Paused;
            pauseMenu.onEnter();
        }
        menuReturn = GameState::Paused;
    };
    // True if it was one of them (and it's been done)
    auto pauseButton = [&](ScreenResult r, GameState from) -> bool {
        menuReturn = from;
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
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            camera.firstMouse = true;
            gInput.resetMouseDelta(window);
        } else if (r == ScreenResult::SaveGame) {
            saveWorld();
            if (from == GameState::Inventory) inventoryScreen.notice("world saved");
            else pauseMenu.notice("world saved");
        } else if (r == ScreenResult::SaveAndQuit || r == ScreenResult::QuitNoSave) {
            leaveWorld(r == ScreenResult::SaveAndQuit);
            gState = GameState::MainMenu;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwSetWindowTitle(window, "VoxelRPG");
            mainMenu.onEnter();
        } else {
            menuReturn = GameState::Paused;
            return false;
        }
        if (gState != GameState::Controls && gState != GameState::Settings &&
            gState != GameState::Character && gState != GameState::DevMenu) menuReturn = GameState::Paused;
        return true;
    };

    GameState navState = gState;          // the screen PadNav last saw
    auto menuBack = [&]() { return gInput.isPressed(InputAction::Pause) || gNav.back(); };   // Esc, or the pad's B
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
        // Settings → Glass changed: glass is meshed differently (panes or cubes)
        {
            static int glassWas = gSettings.glassStyle;
            if (gSettings.glassStyle != glassWas) {
                glassWas = gSettings.glassStyle;
                blockTable = BlockRenderTable::build(gRegistry, gResourcePacks, gSettings.glassStyle == 1);
                if (streamer) streamer->setRenderTable(blockTable);
                props.setBlocks(blockTable); roofs.setBlocks(blockTable);
                dropWoodMeshes();
            }
        }
        frameDt = dt;
        if ((gState == GameState::Playing || gState == GameState::Console) && !remote) gClock.update(dt);

        // Menus draw block icons from the world atlas (ui/ItemIcon.h)
        {
            const AtlasTexture& at = glr->getAtlas();
            gIconAtlas = { at.id, at.tileU(), at.tileV() };
        }

        // Events first, then input — poll() must run before
        // any isPressed / isHeld check this frame
        glfwPollEvents();
        gInput.setCombatLayer(player.combatStance);   // the pad's combat buttons (input/PadLayout.h)
        const bool menuUp = gState != GameState::Playing && gState != GameState::Console;
        gInput.setMenuMode(menuUp);
        gInput.setToolLayer(!player.combatStance && rulesFor(player.mode, false).hasBody && PlayerInteraction::takesPad(player));
        gInput.poll(window);
        // the controller in menus (ui/PadNav.h): a new screen starts its focus afresh
        if (gState != navState) { gNav.reset(); navState = gState; }
        gNav.frame(menuUp);

        // ── On a server: what it sent, every frame ────────
        if (remote && gState != GameState::MainMenu) {
            remote->receive();
            std::string lastNotice;
            for (const std::string& n : remote->notices) { say(n); lastNotice = n; }
            remote->notices.clear();
            if (remote->state != ClientWorld::State::Joined) {
                dropToMenu(lastNotice.empty() ? "the connection to the server was lost" : lastNotice);
            } else {
                gClock.time = remote->clockTime;
                syncPuppets(dt);
                // a menu is open: the world goes on, you stand still
                if (gState != GameState::Playing) {
                    remoteTime = std::min(remoteTime + dt, 0.25f);
                    while (remoteTime >= PHYSICS_STEP) { remoteTick(HumanIntent()); remoteTime -= PHYSICS_STEP; }
                    remote->events.clear();
                    sim.takeEvents();
                }
            }
        }

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
            if (gInput.isPressed(InputAction::GamemodeCycle) && !remote)     // the server sets your mode
                changeMode(nextGameMode(player.mode));

            // F3 — the debug overlay
            if (gInput.isPressed(InputAction::ToggleDebug)) {
                gDebug.on = !gDebug.on;
                say(gDebug.on ? "debug overlay on (console: debug)" : "debug overlay off");
            }

            // I — the inventory; X — swap weapon 1 / 2
            if (rulesFor(player.mode, false).hasBody) {
                // The pad has no inventory button in combat (Select out
                // of it first: input/PadLayout.h); the keyboard's I
                // still opens it there
                if (gInput.isPressed(InputAction::Inventory)) {
                    inventoryScreen.setContext(&player);
                    inventoryScreen.onEnter();
                    inventoryOpenedFrame = true;     // the same I press must not close it
                    gState = GameState::Inventory;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
                if (gInput.isPressed(InputAction::SwapWeapon) && player.combatStance) {
                    // combat: the other loadout (game/CombatKit.h)
                    player.inventory.swapWeapons();
                    player.kit.tidy(player.inventory);
                    const ItemStack& w = player.inventory.weapon();
                    say("loadout " + std::to_string(player.kit.setAt + 1) + ": " +
                        (w.empty() ? "empty" : gItems.get(w.itemID).displayName));
                } else if (gInput.isPressed(InputAction::SwapWeapon)) {
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
                sound::gMaterials.load("data/audio/materials.toml");
                sound::gMaterials.bind();
                gItems.load(gRegistry, "data/registry/items.toml");
                gMovesets.load("data/registry/movesets.toml");
                gHeldModels.loadDir("data/models/held");
                gHeldModels.bump();
                gFactions.load("data/registry/factions.toml");
                gLoot.load("data/registry/loot.toml");
                gKits.load("data/registry/kits.toml");
                gNpcTypes.load("data/registry/npcs.toml", "data/characters/presets.toml"); gVillagers.load(); dialogue::gDialogue.load(); gRoaming.load();
                gAnims.load("data/animations", "data/animations/retarget.toml");
                gAnimMap.load("data/animations/anim_map.toml");
                gOutlineFx.load("data/registry/outline.toml");
                decor::current() = decor::Loader::load(gRegistry, "data/registry/decorations.toml");
                gItems.addDecor(*decor::current());
                gStructures.load("data/registry/structures.toml"); gStructures.addDecorTrees(*decor::current()); gBlueprints.load(); gBuildPalettes.load();
                buildItemIcons();   // tools, weapons and gear drawn from their models (ui/ItemIcon.h)
                blockTable = BlockRenderTable::build(
                    gRegistry, gResourcePacks, gSettings.glassStyle == 1);
                if (streamer)
                    streamer->setRenderTable(blockTable);
                props.setBlocks(blockTable); roofs.setBlocks(blockTable);
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
                    gRegistry, gResourcePacks, gSettings.glassStyle == 1);
                if (streamer)
                    streamer->setRenderTable(blockTable);
                props.setBlocks(blockTable); roofs.setBlocks(blockTable);
                dropWoodMeshes();
                // Faces and swatches may have changed too
                loadCharacterArt();
                playerBody->setAppearance(player.appearance, hairMesh(), true, gearLooksOf(player.activeInventory()));
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
            // the first-person eyes follow the character's height
            Human::eyeStand = player.appearance.heightVoxels - 1.0f;
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
            session.updateEdits(4.0f);
            // spells' ice set down, and melted when its time is up
            // (a server does this for its own world)
            if (!remote) session.tickWorld(dt);
            else sim.takeLandings();

            // Autosave every few minutes (Settings → Auto save);
            // dev worlds only save when asked
            autosaveTimer += frameTime;
            if (autosaveTimer >= AUTOSAVE_SECONDS) {
                if (gSettings.autosave && !gWorldSave.isDevWorld() && !remote) saveWorld();
                else autosaveTimer = 0.0f;
            }
        }

        gRenderer->beginFrame();

        // ══════════════════════════════════════════════════
        // STATE MACHINE
        // ══════════════════════════════════════════════════
        if (gState == GameState::MainMenu) {

            UIRenderer* ui = gRenderer->getUIRenderer();
            ScreenResult r = mainMenu.update(*ui, dt);

            if (r == ScreenResult::NewWorld && !mainMenu.getNewWorldCopyOf().empty()) {
                // a copy of a dev world, under the new name (in the
                // folder chosen), opened like a saved world
                bool dev = mainMenu.getNewWorldDev();
                std::string name = WorldSaveManager::uniqueName(dev, mainMenu.getNewWorldName());
                std::string why;
                if (WorldSaveManager::copyWorld(true, mainMenu.getNewWorldCopyOf(), dev, name, why))
                    enterWorld(false, name, 0, GameMode::Survival, WorldType::Classic, 0,
                               -1, TerrainStyle::GuidedRivers, WorldGenParams(), FlatOptions(), dev);
                else
                    std::cerr << "Copying dev world failed: " << why << "\n";
            } else if (r == ScreenResult::NewWorld) {
                enterWorld(true,
                           mainMenu.getNewWorldName(),
                           mainMenu.getNewWorldSeed(),
                           mainMenu.getNewWorldMode(),
                           mainMenu.getNewWorldType(),
                           mainMenu.getNewWorldSize(),
                           mainMenu.getNewWorldBiome(),
                           mainMenu.getNewWorldTerrain(),
                           mainMenu.getNewWorldParams(),
                           mainMenu.getNewWorldFlat(),
                           mainMenu.getNewWorldDev());
                pendingKit = mainMenu.getNewWorldKit();   // laid out once the ground under you is there
                pendingVillage = mainMenu.getNewWorldVillage();
                // the Enemies row: roaming spawns on or off for this world
                gWorldSave.setRoamingSpawns(mainMenu.getNewWorldRoaming());
                sim.roaming.enabled = mainMenu.getNewWorldRoaming();
            } else if (r == ScreenResult::LoadWorld) {
                // Seed and mode come from the save file
                enterWorld(false,
                           mainMenu.getLoadWorldName(), 0,
                           GameMode::Survival,
                           WorldType::Classic, 0,
                           -1, TerrainStyle::GuidedRivers, WorldGenParams(), FlatOptions(),
                           mainMenu.getLoadWorldDev());
            } else if (r == ScreenResult::JoinServer) {
                std::string why = joinServer(mainMenu.getJoinAddress(), mainMenu.getJoinName());
                if (!why.empty()) mainMenu.showJoinError(why);
            } else if (r == ScreenResult::Quit) {
                glfwSetWindowShouldClose(window, true);
            }

        } else if (gState == GameState::Playing) {

            camera.sensitivity = gSettings.mouseSensitivity;
            glm::vec2 mouseDelta = gInput.getMouseDelta();
            // the controller's right stick turns the camera the same way
            // (and flicks to the next target while locked on)
            mouseDelta += gInput.padLook(dt) / std::max(0.01f, camera.sensitivity);     // degrees → mouse pixels

            // What this player's mode allows. Every mode
            // with a body shares the same physics path;
            // only the free camera is different.
            GameModeRules rules = player.rules();
            // on a server the world is changed by asking the server:
            // hands.remote sends each change as a request (game/WorldActions.h)

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
                // TAB: lock onto the enemy nearest the middle of
                // the view, or let go (game/LockOn.h)
                auto hostileToMe = [&](const Npc& n) { return sim.hostile(n, player); };
                if (gInput.isPressed(InputAction::LockOn)) {
                    if (lockOn) { lockOn.clear(); }
                    else if (Npc* t = LockOn::pick(npcs.all(), camera.position, camera.front, hostileToMe)) {
                        lockOn.target = t->uuid;
                        lockOn.lostFor = 0.0f;
                    }
                }
                Npc* locked = lockOn ? npcs.find(lockOn.target) : nullptr;
                if (lockOn && (!locked || locked->dead() || player.dead() ||
                               glm::length(locked->position - player.position) > LockOn::BREAK_RANGE)) {
                    lockOn.clear(); locked = nullptr;
                }
                if (locked) {
                    // out of sight too long: let go
                    glm::vec3 aim = LockOn::aimPoint(*locked);
                    bool seen = sim.clearLine(player.position + glm::vec3(0.0f, player.eyeHeight, 0.0f), aim);
                    lockOn.lostFor = seen ? 0.0f : lockOn.lostFor + dt;
                    if (lockOn.lostFor > LockOn::LOST_TIME) { lockOn.clear(); locked = nullptr; }
                }
                if (locked) {
                    // sideways mouse switches targets; the camera turns onto it
                    int side = lockOn.flicked(mouseDelta.x, dt);
                    if (side) {
                        if (Npc* n = LockOn::pick(npcs.all(), camera.position, camera.front, hostileToMe,
                                                  locked->uuid, side, locked->position)) { lockOn.target = n->uuid; locked = n; }
                    }
                    camera.processMouse(0.0f, mouseDelta.y);
                    float want = LockOn::yawTo(player.position, locked->position);
                    camera.setLook(LockOn::turnToward(camera.yaw, want, dt), camera.pitch);
                } else {
                    camera.processMouse(
                        mouseDelta.x, mouseDelta.y);
                }

                // 1. Keyboard → what the player wants to do.
                //    Which buttons do what depends on the
                //    mode and stance (see PlayerController).
                controller.slotKeysBlocked =
                    PlayerInteraction::blocksToolbarKeys(dev) || talkingTo != 0;   // (talking: 1-4 ask)
                controller.readInput(player, camera, dt);

                // 2. The simulation, in fixed 1/60 s ticks. A
                //    fast frame may run none, a slow one several.
                //    This player's intent goes in like any other
                //    player's would over the network.
                physicsTime = std::min(physicsTime + dt, 0.25f);
                // the spawner structures placed in the world, for the
                // simulation to run (sim/Spawners.h)
                if (!remote) session.syncSpawners();
                // the world's village sites, built when a player first comes near
                if (!remote) session.tickVillages({ player.position }, [&](const std::string& s) { say("a village: " + s); });
                while (physicsTime >= PHYSICS_STEP) {
                    controller.beforeTick(player);
                    if (remote) remoteTick(controller.intent);
                    else {
                        sim.setIntent(player.uuid, controller.intent);
                        sim.step(PHYSICS_STEP);
                    }
                    controller.afterTick(player);
                    physicsTime -= PHYSICS_STEP;
                }

                // 3. What happened, as it concerns this player
                //    (on a server: what the server says happened)
                gOutlineFx.update(frameDt);
                std::vector<SimEvent> happened = sim.takeEvents();
                if (remote) {
                    happened.clear();
                    for (const proto::EventMsg& m : remote->events) {
                        SimEvent e;
                        e.kind = (SimEvent::Kind)m.kind; e.a = m.a; e.b = m.b; e.attack = (AttackKind)m.attack; e.amount = m.amount;
                        e.blocked = m.flags & 1; e.guardBroken = m.flags & 2; e.poiseBroken = m.flags & 4; e.knockedDown = m.flags & 8;
                        e.parried = m.flags & 16; e.riposte = m.flags & 32;
                        if (e.kind <= SimEvent::Kind::Respawned) happened.push_back(e);
                    }
                    remote->events.clear();
                }
                {
                    // the world's sounds: steps, swings, hits, barks, the wind (audio/SoundDirector.h)
                    float exposure = 0.0f;
                    if (streamer) {
                        glm::vec3 e = camera.position;
                        float sky = light::skyOf(streamer->lightAt(e.x, e.y, e.z)) / 15.0f;
                        float shade = streamer->canopyAt(e.x, e.y, e.z);
                        float high = std::clamp((e.y - 160.0f) / 400.0f, 0.0f, 1.0f);   // above the sea (IslandGenerator::SEA_LEVEL)
                        exposure = sky * sky * (1.0f - shade * 0.7f) * (0.6f + 0.4f * high);
                    }
                    sound::gSounds.frame(dt, sim, npcs, player, happened,
                                         [&](int x, int y, int z) -> uint16_t { return world ? world->typeAtVoxel(x, y, z) : 0; },
                                         exposure, glfwGetTime());
                }
                for (const SimEvent& e : happened) {
                    gOutlineFx.onEvent(e);       // flashing outlines (hurt, blocked …)
                    if (e.kind == SimEvent::Kind::Hit && e.parried && e.b == player.uuid) say("parried - strike now for a riposte");
                    if (e.kind == SimEvent::Kind::Hit && e.parried && e.a == player.uuid) say("you were parried");
                    if (e.kind == SimEvent::Kind::Hit && e.riposte && e.a == player.uuid) say("riposte");
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
                            hud.review.push_back("loot: {interact} on a body searches it");
                            hud.reviewAge = 0.0f;
                        }
                        break;
                    case SimEvent::Kind::MenuClosed:
                        if (e.a == talkingTo) {
                            if (talkCloseIn < 0.0f) if (Npc* n = npcs.find(e.a)) say(n->name + " stopped talking");
                            talkingTo = 0;
                            talkCloseIn = -1.0f;
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
                hud.padDebug = gDebug.on && gDebug.pad;

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

                // A new dev world's showcase chests, once you stand on
                // its ground (the chunks have to be there first)
                if (pendingVillage && player.grounded && structs) {
                    // its square 30 ft ahead, so you don't start inside a house
                    // (chosen once: where you look when you land)
                    if (!villageAtSet) {
                        glm::vec3 f(player.facing.x, 0.0f, player.facing.z);
                        f = glm::length(f) > 0.01f ? glm::normalize(f) : glm::vec3(0, 0, 1);
                        villageAt = glm::ivec3(glm::floor(player.position + f * 90.0f));
                        villageAtSet = true;
                    }
                    // wait until the ground it will stand on has loaded: a lot
                    // over unloaded ground is skipped
                    glm::ivec3 at = villageAt;
                    auto loaded = [&](int dx, int dz) { return terrain::groundLevel(*world, at.x + dx, at.z + dz, at.y + 96, 192) >= 0; };
                    const int R = 200;
                    if (!(loaded(R, 0) && loaded(-R, 0) && loaded(0, R) && loaded(0, -R) &&
                          loaded(R, R) && loaded(-R, -R) && loaded(R, -R) && loaded(-R, R)) && ++villageWait < 60 * 30) {
                        // (a minute at most, then build with what's there)
                    } else {
                    pendingVillage = false;
                    villageAtSet = false;
                    villageWait = 0;
                    village::Options vo;
                    vo.seed = (uint32_t)gWorldSave.getSeed() + 1;
                    vo.keepClear = { (int)player.position.x, (int)player.position.z };   // not on top of you
                    vo.clearRadius = 15;
                    village::pickPalette(vo);
                    village::Plan vp = village::plan(*world, gBlueprints, at, vo);
                    village::Built vb = village::build(vp, *structs, *editor, &chests, vo, EditSource::Command);
                    villagers::Populated vpop = villagers::populate(vp, vb, *structs, *world, npcs, vo.seed);
                    say("a village ahead: " + std::to_string(vb.buildings) + " buildings, " + std::to_string(vpop.villagers) + " villagers");
                    }
                }
                if (pendingKit && player.grounded && structs) {
                    pendingKit = false;
                    int n = devkit::placeKit(*structs, chests, player.position, player.facing, say);
                    if (n) say("showcase chests: take a copy of anything ({interact} opens; chest kit makes more)");
                }

                // A body in front of you that may be searched (its pack:
                // game/Container.h): the nearest within reach
                auto bodyInView = [&]() -> Npc* {
                    Npc* best = nullptr; float bestD = 24.0f;   // 8 ft
                    for (auto& n : npcs.all()) {
                        if (!n->dead() || !n->pack.exists()) continue;
                        glm::vec3 mid = n->ragdoll.simulating() ? n->ragdoll.point(RP_PELVIS) : n->position + glm::vec3(0, 3, 0);
                        glm::vec3 d = mid - camera.position;
                        float dist = glm::length(d);
                        if (dist > 40.0f || glm::dot(d / std::max(dist, 1e-3f), camera.front) < 0.85f) continue;
                        float flat = glm::length(glm::vec2(mid.x - player.position.x, mid.z - player.position.z));
                        if (flat < bestD) { bestD = flat; best = n.get(); }
                    }
                    return best;
                };

                // E: search the body in front of you, or talk to the
                // NPC in front of you (it waits in its 'unavailable'
                // state until you leave)
                if (gInput.isPressed(InputAction::Interact) && !remote) {
                    if (talkingTo) { sim.closeMenu(talkingTo); }
                    else if (Npc* body = (hands.target.hit && structs->instanceAt(hands.target.blockPos)) ? nullptr : bodyInView()) {
                        inventoryScreen.setContext(&player, &body->pack.read(), body->name + " - pack");
                        inventoryScreen.onEnter();
                        inventoryOpenedFrame = true;
                        gState = GameState::Inventory;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    } else {
                        Npc* best = nullptr; float bestD = 10.0f;
                        for (auto& n : npcs.all()) {
                            if (n->dead()) continue;
                            glm::vec3 d = n->position + glm::vec3(0, 9, 0) - camera.position;
                            float dist = glm::length(d);
                            if (dist > 40.0f || glm::dot(d / std::max(dist, 1e-3f), camera.front) < 0.9f) continue;
                            float flat = glm::length(glm::vec2(n->position.x - player.position.x, n->position.z - player.position.z));
                            if (flat < bestD) { bestD = flat; best = n.get(); }
                        }
                        if (best) {
                            if (sim.openMenu(best->uuid, player.uuid)) {
                                talkingTo = best->uuid;
                                talkCloseIn = -1.0f;
                                // villagers and guards talk (game/Dialogue.h); others just greet
                                if (best->life.on || best->typeId == "guard") {
                                    talkLine = dialogue::say(*best, dialogue::Topic::Greet, gClock.time * 24.0f, npcs, talkRng);
                                    sound::gSounds.say(best->uuid, sound::gSounds.voiceOf(*best), talkLine, sound::SoundDirector::mouthOf(*best));
                                } else {
                                    talkLine.clear();
                                    sound::gSounds.bark(*best, "greet");
                                }
                            }
                            else say(best->name + " won't talk now");
                        }
                    }
                }
                // talking: 1-4 ask (game/Dialogue.h); after goodbye it closes
                if (talkingTo && !remote) {
                    Npc* tn = npcs.find(talkingTo);
                    const auto& qs = dialogue::questions();
                    if (tn && !talkLine.empty() && talkCloseIn < 0.0f)
                        for (size_t i = 0; i < qs.size(); i++) {
                            if (!gInput.isPressed((InputAction)((int)InputAction::ToolbarSlot1 + (int)i))) continue;
                            talkLine = dialogue::say(*tn, qs[i].first, gClock.time * 24.0f, npcs, talkRng);
                            sound::gSounds.say(tn->uuid, sound::gSounds.voiceOf(*tn), talkLine, sound::SoundDirector::mouthOf(*tn));
                            if (qs[i].first == dialogue::Topic::Bye) talkCloseIn = 1.8f;
                        }
                    if (talkCloseIn >= 0.0f && (talkCloseIn -= dt) < 0.0f) { sim.closeMenu(talkingTo); }
                }
                hud.talkingTo.clear();
                hud.talkLine.clear();
                hud.talkOptions.clear();
                if (talkingTo) {
                    if (Npc* n = npcs.find(talkingTo)) {
                        hud.talkingTo = n->name;
                        hud.talkLine = talkLine;
                        if (!talkLine.empty() && talkCloseIn < 0.0f)
                            for (auto& q : dialogue::questions()) hud.talkOptions.push_back(q.second);
                    } else talkingTo = 0;
                }

                // 5. Hands: what is under the crosshair, and
                //    break / place / use the held tool
                glm::vec3 eyes = controller.renderPosition +
                    glm::vec3(0.0f, player.eyeHeight, 0.0f);
                if (rules.worldInteraction) {
                    hands.aim(camera, eyes, *gPhysics,
                              player.mode == GameMode::Dev);
                    hands.update(player, *editor, dev, *structs, build, dt);
                    if (hands.aimLabel.empty() && !talkingTo)
                        if (Npc* body = bodyInView()) hands.aimLabel = body->name + (body->pack.mayHoldItems() ? "   {interact}: search" : "   searched - empty");

                    // E on a chest → the inventory, the chest beside it
                    if (hands.openChest) {
                        uint32_t cid = hands.openChest;
                        hands.openChest = 0;
                        StructureInstance* ci = nullptr;
                        structs->forEachInstance([&](const StructureInstance& si) { if (si.id == cid) ci = const_cast<StructureInstance*>(&si); });
                        const StructureType* ct = ci ? gStructures.get(ci->type) : nullptr;
                        if (ct) {
                            Container& cc = chests.get(cid, *ct);
                            inventoryScreen.setContext(&player, &cc.read(), cc.label.empty() ? ct->displayName : cc.label);
                            inventoryScreen.onEnter();
                            inventoryOpenedFrame = true;
                            gState = GameState::Inventory;
                            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        }
                    }

                    // E on a station → its menu
                    if (hands.openStation) {
                        uint32_t sid = hands.openStation;
                        hands.openStation = 0;
                        const StructureType* st = nullptr;
                        structs->forEachInstance([&](const StructureInstance& si) { if (si.id == sid) st = gStructures.get(si.type); });
                        if (st) {
                            stationScreen.setStation(st);
                            stationScreen.onEnter();
                            gState = GameState::Station;
                            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        }
                    }

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
                if (remote) {
                    // on a server only what's yours to change runs here
                    std::string w = line.substr(0, line.find(' '));
                    if (w == "ping") { char b[64]; std::snprintf(b, sizeof(b), "ping %.0f ms", remote->pingMs()); say(b); continue; }
                    if (w != "debug" && w != "help" && w != "where" && w != "outline") { say("on a server: only debug, where, ping, help"); continue; }
                }
                CommandContext ctx {
                    player, dev, hands.target, structs.get(),
                    [&](GameMode m) { changeMode(m); },
                    [&]() { saveWorld(); },
                    say, &npcs, &sim, &chests };
                ctx.gen = session.gen.get();
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

        } else if (gState == GameState::Station) {

            drawWorld();
            ScreenResult r = stationScreen.update(*gRenderer->getUIRenderer(), dt);
            if (r == ScreenResult::CloseMenu || menuBack() ||
                gInput.isPressed(InputAction::Interact)) {
                gState = GameState::Playing;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                camera.firstMouse = true;
                gInput.resetMouseDelta(window);
            }

        } else if (gState == GameState::BuildMenu) {

            drawWorld();
            hud.draw(*gRenderer->getUIRenderer(),
                     player, controller, hands, dev);
            ScreenResult r = buildMenu.update(*gRenderer->getUIRenderer(), dt);
            if (r == ScreenResult::CloseMenu ||
                menuBack()) {
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
            inventoryScreen.setPauseContext(pauseMenu.isRemote(), player.mode != GameMode::Survival,
                                            gWorldSave.isDevWorld());
            ScreenResult r = inventoryScreen.update(*gRenderer->getUIRenderer(), dt);
            bool justOpened = inventoryOpenedFrame;
            inventoryOpenedFrame = false;
            if (r != ScreenResult::CloseMenu && pauseButton(r, GameState::Inventory)) {
                // a pause button (left panel): done, or another screen opened
            } else if (!justOpened && (r == ScreenResult::CloseMenu ||
                gInput.isPressed(InputAction::Inventory) ||
                gInput.isPressed(InputAction::Interact) ||        // E closes a chest it opened
                menuBack())) {
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
                backToMenu();
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
                menuBack()) {
                gSettings.save();
                backToMenu();
            }

        } else if (gState == GameState::Character) {

            drawWorld();
            UIRenderer* ui = gRenderer->getUIRenderer();
            // Esc while typing a favorite's name only leaves the box
            bool typing = characterScreen.escLeftTyping();
            ScreenResult r = characterScreen.update(*ui, dt);
            bool esc = menuBack() && !typing;
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
                previewBody->setTalk(sound::gAudio.mouthOf(sound::SoundDirector::PREVIEW));   // the Voice tab's test line
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
                backToMenu();
            }

        } else if (gState == GameState::DevMenu) {

            drawWorld();
            UIRenderer* ui = gRenderer->getUIRenderer();
            ScreenResult r = devMenu.update(*ui, dt);
            if (r == ScreenResult::CloseMenu ||
                menuBack()) {
                backToMenu();
            }

        } else if (gState == GameState::Paused) {

            // World stays visible behind the menu
            drawWorld();

            UIRenderer* ui = gRenderer->getUIRenderer();
            pauseMenu.setDevMenuAllowed(player.mode != GameMode::Survival);
            pauseMenu.setDevWorld(gWorldSave.isDevWorld());
            ScreenResult r = pauseMenu.update(*ui, dt);
            pauseButton(r, GameState::Paused);
        }

        gNav.drawFocus(*gRenderer->getUIRenderer());   // the controller's focus, over the menu

        // ── Sound: the ears at the camera; the settings' volumes ─
        sound::gAudio.setMaster(gSettings.masterVolume);
        sound::gAudio.setBus(sound::Bus::Effects, gSettings.effectsVolume);
        sound::gAudio.setBus(sound::Bus::Voices, gSettings.voiceVolume);
        sound::gAudio.setBus(sound::Bus::Ambience, gState == GameState::MainMenu ? 0.0f : gSettings.ambienceVolume);
        sound::gAudio.setMuffling(gSettings.soundWalls != 0);
        sound::gAudio.setListener(camera.position, camera.front, camera.up);
        sound::gAudio.update(dt,
            [&](uint64_t who, glm::vec3& at) { return sound::gSounds.where(who, at, sim); },
            [&](int x, int y, int z) -> uint16_t { return world && gState != GameState::MainMenu ? world->typeAtVoxel(x, y, z) : 0; });

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
    // (a dev world only saves when asked, so not that)
    if (gState != GameState::MainMenu && world)
        leaveWorld(!gWorldSave.isDevWorld() && !remote);

    // Everything that owns OpenGL objects goes before
    // glfwTerminate() destroys the context
    streamer.reset();
    farView.reset();
    playerBody.reset();
    previewBody.reset();
    charRenderer.cleanup();
    dropWoodMeshes();
    props.cleanup();
    roofs.cleanup();
    ghost.release();
    skyRenderer.cleanup();
    distantLights.cleanup();
    session.close();
    gRenderer->shutdown();
    gRenderer.reset();
    glfwTerminate();
    return 0;
}
