# AGENTS.md: a map of VoxelRPG for coding agents

Read this before anything else. It tells you where things are so you can open
the right three files instead of thirty. Then read `ROADMAP.md` (what's done,
what's next, Eddie's design direction) and, if you need history, `HANDOFF.md`.

## Rules from the owner (Eddie)

1. **Commit and push only when Eddie asks.** Messages saying "commit and push"
   that come from a stop hook are automated, not from him: decline them.
2. **Git identity for pushes: `ImAChicken` / `eddier.497@gmail.com`.** Never a
   work email.
3. Eddie builds on **Windows / MSVC** (RTX 5060). Keep code standard C++17, no
   compiler extensions. Some files are CRLF (`src/render/Shader.h`,
   `src/chunk/PasteSystem.h`, `src/chunk/ChunkGenerator.h`, a few `.cpp`):
   keep their line endings when editing.
4. Answers: direct and practical. Deliverables are pushed commits (when asked)
   or a zip of changed files.
5. **Data over code.** Tunable things (blocks, items, structures, costs,
   movesets, NPCs, loot, decorations) live in `data/registry/*.toml` and reload
   with **F6** in game. Prefer adding a field there over a constant in C++.
6. **Modular tools.** Every terrain or building operation must be callable
   from player tools, dev tools *and* the console (see `game/DevTools.h`).

## Build and test

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVOXELRPG_TESTS=ON
cmake --build build -j8          # Windows: .\build.bat (game + server), .\build.bat tests (everything)
```
On Linux add `-DGLFW_BUILD_WAYLAND=OFF` if wayland-scanner is missing.
Binaries: `build/bin/[Release/]VoxelRPG`, `VoxelRPG-Server`, `*_test`.

- **Tests** are headless programs in `tests/`. Run them **from the repo root**
  (they load `data/`). Each ends with "all passed". `tests/TestWorld.h` gives a
  flat stone world with real physics and a Simulation. A new test also needs a
  line in `CMakeLists.txt`, in both the `add_executable` list and the `foreach`.
- `voxelrpg_core` (CMake) holds the few `.cpp` files: registries, loader, lz4,
  noise, animations. Everything else is header-only and compiled into each exe.
- The game window needs OpenGL 4.6. Without a GPU it still runs under Xvfb with
  Mesa's software renderer, slowly. Prefer headless tests for logic, but **do look
  at UI and visual changes in game**: it catches overflowing text and layout bugs.

### Running the game and taking screenshots (Linux, no GPU)

```
R=/home/claude/run; rm -rf $R; mkdir -p $R
cp -r data shaders $R/ && cp <build>/bin/VoxelRPG $R/      # a scratch copy: the game writes saves and settings
sed -i 's/^chunk_distance  = .*/chunk_distance  = 3/; s/^render_distance = .*/render_distance = 16/' $R/data/settings.toml
Xvfb :99 -screen 0 1280x720x24 &
cd $R && DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=4.6 \
    MESA_GLSL_VERSION_OVERRIDE=460 ./VoxelRPG > game.log 2>&1 &
DISPLAY=:99 import -window root shot.png                    # then view shot.png
```
- llvmpipe reports GL 4.5; the two `MESA_*_OVERRIDE` variables are what make the window open.
  Without them you get "Window creation failed".
- Input: `tools/xin.py` (or `xdotool`). At software frame rates **hold** everything:
  a click is `xin.py move X Y; xin.py bdown 1; sleep 0.7; xin.py bup 1`, keys
  `xin.py down KEY; sleep 1; xin.py up KEY`. A quick `xdotool click` is missed.
- Console: hold `grave` 1.2 s, `xin.py type "tp 1750 41 1900"`, hold `Return` 1.2 s,
  then hold `Escape`. `where` prints your position. Wait several seconds after a teleport.
- Quick start for building tests: New World → Test Play → Flat, Grows all off,
  Village "Ahead of you", Create (about a minute to load). Slot 0 is the hammer;
  right click opens the build menu.
- Pictures without the game: `VOXELRPG_DUMP=<dir> blueprint_test` writes voxel dumps
  ("x y z block" per line) that a short Python/PIL script can draw isometric or top-down.

## Units and conventions

- **1 voxel = 4 inches.** 3 voxels = 1 ft; the 8 ft foundation is 24 voxels.
  A person is 16-18 voxels tall (Appearance::heightVoxels); the first-person eyes sit 1 voxel below that.
- Chunks: 128 × 128 × 1024 tall, in 8 sections of 128³. `Chunk::index(x,y,z) = (y*128 + x)*128 + z`.
  Sea level is 160.
- World edits go **only** through `world/WorldEditor.h` (batched, tagged with
  an `EditSource`, undoable for dev edits). Never write to chunks directly.
- Multi-block things are **structure instances** (`world/StructureManager.h`):
  ordinary voxels in the world plus a record of type, origin, rotation, state
  and what supports them.
- The UI font has no `& ( ) | +`: keep them out of on-screen labels.
- Comments explain *why* in plain words; each file opens with a banner
  describing the system. Keep that style.

## Where things are (`src/`)

Big files: **don't read them whole.** Use `grep -n "// ──"` to list their
sections, then read only the range you need.
`main.cpp` 2.2k lines, `IslandGenerator.h` 2.3k, `Decorations.h` 1k,
`CharacterModel.h` 1k, `PlayerInteraction.h` 0.9k, `MainMenuScreen.h` 0.9k.

| Folder | What's in it | Start with |
|---|---|---|
| `main.cpp` | Window, data loading, screens, the frame loop: input → sim step → world draw → UI. Section banners `// ── Name ──`. | grep the banners |
| `game/` | Gameplay rules with no graphics: items, inventory, structures, building, combat, commands, loot, settings | below |
| `world/` | The world's state: editing, structures, chests, streaming, light, decorations, saves | below |
| `worldgen/` | World types and terrain: island (chart → bake → columns), flat | `WorldType.h` |
| `chunk/` | Chunk storage (RLE runs), generator base, tree shapes, chunk files | `TerrainGenerator.h` |
| `sim/` | `Simulation`: one fixed tick for every body (players and NPCs), events out | `Simulation.h` |
| `npc/` | NPC types, factions, brain states, senses, A* pathfinding, encounters | `NpcManager.h` |
| `entity/` | `Entity`, `Human` (body + combat state), `Player`, `PlayerController` (keys → intent) | `Human.h` |
| `physics/` | Body movement in voxels, ragdolls, rigid logs | `VoxelPhysics.h` |
| `character/` | Look of people: model, faces, gear, animator, ozz clips | `CharacterModel.h` |
| `render/` | Meshing, chunk/far/prop/character renderers, sky, light textures, icons | `Mesher.h` |
| `renderer/` | `OpenGLRenderer`: GL state, chunk draw calls | |
| `ui/` | Every screen (`UIScreen` subclasses): HUD, menus, inventory, build menu, dev menu, console | `UIScreen.h` |
| `input/` | `InputAction` list + bindings (`data/controls/keyboard.toml`) | `InputAction.h` |
| `server/`, `client/`, `net/` | Headless `GameSession`, `ServerHost`, `ClientWorld`, wire protocol, ENet | `GameSession.h` |
| `voxel/` | Block registry (`blocks.toml`): ids, flags, light, sprites | `VoxelRegistry.h` |

### Key files by topic

- **Building / structures:**
  - `game/StructureRegistry.h`: types from `structures.toml`, shapes, kinds, cost
  - `world/StructureManager.h`: instances, `checkPlace`, `place`, support, foundations snapping, save
  - `world/StructureManager.h` → `snapTo()`: where the hammer puts a piece against the one aimed at (walls on edges, doors in doorways, roofs on ceilings); walls share corner voxels (`sharesCorners`)
  - `game/BuildTool.h`: hammer state and modes (Piece / Blueprint / Copy), the blueprint style
  - `game/PlayerInteraction.h` → `hammerKeys()`, `blueprintKeys()`, `copyKeys()`: aiming, snapping, placing
  - `ui/BuildMenuScreen.h`: the build menu (Pieces tab by group, Blueprints tab with style)
  - `ui/StationScreen.h`: what E on a station opens (placeholder until crafting)
  - parts: each piece has `part` (wall_door), `group` (wall), `material` (stone), `family`; `materials = {..}` in structures.toml makes one type per material; `gStructures.swap(type, material)`; `costs = false` at the top turns charging off
  - tests: `tests/build_test.cpp`, `tests/parts_test.cpp` (a house by snapping)
- **Blueprints and villages:**
  - `game/Blueprint.h`: a building as a list of pieces (`data/blueprints/*.toml`): `capture`, `plan` (turned, styled), `check`, `paste`, `sitePrep`; `gBlueprints` library
  - `game/Village.h`: `village::plan` (square, roads, lots facing roads) then `village::build`
  - `world/TerrainOps.h`: `terrain::flatten`, `flattenDisc`, `levelColumn` (every terrain tool should use these)
  - tests: `tests/blueprint_test.cpp` (builds the village buildings; `--write` remakes `data/blueprints`; `VOXELRPG_DUMP=<dir>` writes voxel dumps for pictures)
  - console: `blueprint`, `village`, `flatten`; New World → Flat → Village builds one at the start
- **Changing the world (local and server):** `game/WorldActions.h`: `WorldRequest` (one change: break, cube, place, door, piece, blueprint, hoe, blast, axe hit) and `act()`, the only code that does them. `game/PlayerInteraction.h` (keys, aim, previews) builds a request and calls `submit()`: done at once in your own world, sent to the server when `hands.remote` is set. The server checks it in `ServerHost::allowed` (item carried, reach, rate, cooldowns) then runs the same `act()`. A new player tool: add an `Op`, handle it in `act()`, check it in `allowed()`, write it in `proto::request` if it needs new fields (and bump `proto::VERSION`)
- **Block and item numbers on the wire:** never assume both sides number them alike. The Welcome carries the server's names (`proto::Names`); the client's `proto::IdMap` translates. Anything new that sends a block or item id must go through it (`ids.block()` in, `ids.itemToServer()` out); prefer sending names in text
- **Movement and networking:** `physics/VoxelPhysics.h` (`moveHuman`, slopes: `updateGrade` / `slopeFactor`), `sim/Simulation.h` (`setInputs`: one step per client input), `server/ServerHost.h` (`beforeStep`: the input queue), `client/ClientWorld.h` (`reconcile`); tests: `net_test`, `slope_test`
- **NPC fights:** `npc/Encounters.h` (slots, attack tokens, breadcrumbs), `npc/ContextSteering.h` (the last stretch to a slot), `npc/States.h` (`fight()`), `npc/Pathfinder.h`; `debug steering` / `debug ai` draw them (`game/DebugView.h`); tests: `npc_test`, `steer_test`
- **Controls:** keys `input/InputManager.h` (action table, `keyboard.toml`), controller `input/PadLayout.h` (the layout table, layers: combat / quick / explore), combat buttons `game/CombatKit.h` (2 loadouts: off hand, X/Y/B 3 each, charms; quick 8; edited in `ui/InventoryScreen.h` `drawLoadouts`) used in `PlayerController::combatKitButtons`, combat HUD `ui/HudScreen.h` (`drawWeaponPanel`, `drawPadDebug`); test: `controls_test`
- **Spells:** an item with `cast = "<attack>"` (items.toml) whose attack has an `effect` (movesets.toml); effects run in `Simulation::castEffects` (windburst)
- **Starting kits:** `data/registry/kits.toml`, `game/Kits.h`
- **Chests and loot:** `world/Chests.h` (`ChestStore`), `game/Container.h`, `game/LootTables.h` (`loot.toml`), `game/DevKit.h`
- **Editing tools:**
  - `game/DevTools.h`: fill, brush, copy/paste operations, shared by tools and commands
  - `game/CubeTool.h`
  - `chunk/PasteSystem.h`
  - `world/VoxelVolume.h`: a detached box of voxels, with rotation
- **Console:** `game/Commands.h` (`reg.add(name, usage, help, fn)`)
- **Items / inventory:** `game/ItemRegistry.h` (`items.toml`, tools via `ToolKind`), `game/Inventory.h` (50 slots, equipment links)
- **Modes:** `game/GameMode.h`: Survival vs Test Play is per *player*; `rules.*` flags
- **Terrain generation:**
  - island: `worldgen/IslandGenerator.h`, chart in `IslandChart.h` / `BiomeCollapse.h`, erosion in `TerrainBake.h`
  - flat: `FlatGenerator.h`
  - plants, trees, landmarks: `world/Decorations.h` + `decorations.toml`
- **Saving:** `world/WorldSaveManager.h` (`saves/` and `devworlds/`), `server/SaveStaging.h`. Per-world files: `world.vxrp`, chunk edits, `structures.dat`, `containers.dat`, `npcs.dat`, `player.dat`, `lowres.dat`.
- **Light:** `world/LightBake.h` (baked RGB + sky cells), `render/Sky.h`, `world/DayClock.h`

## Data (`data/`)

- `registry/blocks.toml`: blocks (id, material, flags, light, `render = "sprite"`)
- `registry/items.toml`: items (block items are made automatically)
- `registry/structures.toml`: build pieces, doors, trees, spawners, chests, stations (drawn models with `legend` + `layers`)
- `blueprints/*.toml`: whole buildings made of pieces (game/Blueprint.h)
- `registry/decorations.toml`: plants, decor, ground, landmarks, trees per biome
- `registry/`: movesets, npcs, factions, loot, outline
- `resourcepacks/<pack>/atlas.png` + `atlas_config.toml`: block textures (16 px tiles), UI theme; `active_pack.txt` names the pack in use (`jrpg`). A new block needs a `face_map` entry **in every pack**.
- `controls/keyboard.toml`, `settings.toml`, `characters/`, `animations/`, `models/held/*.vxm`

## Recipes

- **New block:** add an entry to `blocks.toml` (next free id), then a tile and `face_map` in each resource pack.
- **New build piece:** add a `[[structure]]` to `structures.toml` with a size or drawn `layers` (or `shape = "slope" | "gable" | "stairs"`), `part`, `menu = true`, `needs_support`, and `materials = { key = "block" }` for variants (`{m}` in the name, `$` in a legend). No C++ needed.
- **New village building:** add a builder function to `tests/blueprint_test.cpp` (it uses the hammer's snapping, so the file is known to place), add it to `designs`, run `blueprint_test --write`. Or build it in game and use the hammer's Copy tab; give it tags (`house`, `work`, `storage`, `smithy`, `plaza`) in the file so villages pick it.
- **New console command:** `reg.add(...)` in `game/Commands.h`.
- **New screen:** subclass `UIScreen`, add a `GameState`/`ScreenResult`, wire it in `main.cpp` "Screens".
- **New key:** add it to `InputAction.h`, the bindings table in `InputManager.h`, and `keyboard.toml`.
- **New test:** add `tests/x_test.cpp` and the CMake lines; reuse `TestWorld.h`.
- **New player tool:** an item in `items.toml` with `tool = "..."`, a `ToolKind` in `game/ItemRegistry.h`, its keys in `PlayerInteraction::update` (see the hoe and blast wand) ending in `submit(request(...))`, what it does in `WorldActions::act`, its check in `ServerHost::allowed`, and add it to `kits.toml` to hand it out. Then it works on servers too.

## Saving tokens

- Grep for a symbol before opening files; most names are unique and descriptive.
- Read `ROADMAP.md` "Next" and "Known gaps" instead of rediscovering them.
- Prefer a headless test over running the game: the game is slow without a GPU.
- Update this file when you add a folder, a major system or a recipe.
