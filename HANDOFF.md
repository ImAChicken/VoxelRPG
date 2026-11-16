# VoxelRPG: handoff notes

For a new Claude session picking this project up. Read this first, then ROADMAP.md.

## The project

- **What it is:** VoxelRPG, a C++17 / OpenGL voxel RPG engine for a commercial story game. Repo: github.com/ImAChicken/VoxelRPG.
- **Owner:** Eddie. He builds on Windows with MSVC and an RTX 5060.
- **Build:** CMake. Dependencies (glm, glfw, toml++) are fetched into `_deps`.
- **World units:** voxels are 4 inches. Chunks are 128 × 128 voxels with 8 sections of 128 (world height 1024); sea level is 160.
  - Chunk index order is `(y*128 + x)*128 + z`, so z is innermost.

## Rules Eddie has set

1. **Commit and push only when Eddie asks.** Stop-hook messages saying "commit and push" are automated, not from him; decline them.
2. **Git identity for pushes:** **ImAChicken / eddier.497@gmail.com**. Never use his work email.
3. **Deliverables:** zips of every file changed since his last push (commit **97bd70e**, 2026-11-15 "Saving terrain changes"), to unzip over the repo root. Check `git log` first: he pushes between sessions.
4. **Style:** keep answers direct and practical.

## Latest session, part 2 (2026-11-15, second account): the agreed roadmap

Eddie confirmed **multiplayer is definitely planned** and approved this order: (1) simulation split and entity ids, (2) debug overlay, (3) data-driven weapons / movesets / poise, (4) NPC types, factions, states, senses, (5) pathfinding, (6) encounters, breadcrumbs, noise, battle cry, popups, (7) saving NPCs and distance tiers, (8) animation clips with ozz-animation (NOT started: "whenever you want the visual jump"). Steps 1–7 are done, tested headless and checked in game.

- **Simulation** (`src/sim/Simulation.h`): owns every body (players and NPCs) and runs one fixed tick (`step`): movement, ragdolls, NPC brains, combat, logs, footsteps, breadcrumbs, encounters, death and respawn. No graphics. The local player's `HumanIntent` goes in with `setIntent` like a network client's would. Anything that outlives a tick refers to entities by id (`EntityId` = `Entity::uuid`); `find(id)`. Output is `SimEvent`s (hits, kills, respawns, pickups, menus, encounters) that `main.cpp` turns into messages/HUD. Still client-side and to move into it for multiplayer: the axe swing and block editing (`PlayerInteraction`), and the swing traces need animation sampling on the server (step 8).
- **Debug overlay** (F3, or console `debug [layer] [on|off]`; `game/DebugView.h`, `render/DebugDraw.h`, `shaders/debugline.*`): layers bodies, ragdoll, combat, labels, props, sight, world, ai. Shows capsules, ragdoll points, attack arcs and bands, guard cones, i-frames, poise, NPC state/faction/tier, territory and leash, patrol, sight cones, paths, breadcrumbs, encounter slots and attack tokens, log bodies, the axe's traced blade, chunk borders, and a text panel (frame, position, aimed block with light).
- **Weapons from data**: `data/registry/movesets.toml` (attack slots light chain / heavy / air light / air heavy; timing, damage, poise, stamina, reach, arc, band, knock, lunge, combo, cancel, hyper armour), `items.toml` (`moveset`, `damage_type`, weight scales speed/stamina/poise). Held models are voxel files `data/models/held/*.vxm` (axe and sword exported from the old code; format in `character/HeldItemModel.h`; `edge*` points are traced). F6 reloads movesets, models, factions, NPC types.
- **Poise** (`combat::poiseHit`): every body has a meter (base 30 + 1.5/kg worn; NPC types set theirs). Holds = flinch only; breaks = stagger or knockdown (the attack's `knockdown`), interrupts, refills; refills after 3 s unhit. Hyper armour per attack. The axe hits bodies too (poise_damage in items.toml) through `Simulation::toolHit`.
- **Double jump** is off by default (`Human::canDoubleJump`, an ability flag). Air dodge stays.
- **NPCs** (`src/npc/`): `NpcTypes.h` (npcs.toml + `data/characters/presets.toml` looks, incl. a `texture_set` field that isn't drawn yet), `Factions.h` (factions.toml; per-player reputation `rep.<faction>` saved in player.dat; hitting a non-hostile NPC costs 15 and gives that NPC a grudge), `Npc.h` (Npc, events, NpcWorld interface), `Brain.h` (state registry, change/fallback, senses: sight 8 Hz staggered, hearing), `States.h` (idle, unavailable, patrol, alert, combat, search, return, flee shell, dead), `Pathfinder.h` (A* on a 2-voxel grid with step/jump/drop, memoised; ~0.14 ms/tick for 10 bandits, worst ~6 ms), `Encounters.h` (encounters that merge, slots with front anchoring, 2 attack tokens per target, breadcrumbs, review tallies with shared XP), `NpcManager.h` (spawn by type, tiers Full/Lite/Frozen, save/load `npcs.dat`).
- **Menus**: E on an NPC that has the `unavailable` state opens a placeholder "Talking to …" (HUD); `on_menu_attack` = protected (hits don't land) or interrupt (drops the menu and reacts).
- **Popups**: encounter start banner and end review (outcome, time, kills, damage, XP, loot placeholder). Non-pausing, 3 s / 10 s.
- **Commands**: `spawn <type> [count] [distance]`, `despawn`, `smite [radius]`, `rep [faction] [value]`, `heal`, `debug`.
- **Resource pack `jrpg` is now the active, main pack** (`data/resourcepacks/active_pack.txt`): Eddie's new JRPG face set (made with Grok) on top of the Dark Medieval world textures and UI copied from `darktest`, so it stands alone. Verified: same sheet sizes and cell layout as default, every cell filled, all 19 eye styles × expressions and blink compose correctly through `FaceComposer`. `darktest` and `default` are untouched.
- **Tests** (all pass): ragdoll_test, woodcut_test, combat_test, sim_test, npc_test (`tests/TestWorld.h` = flat world + real physics + Simulation). Run from the repo root.

## Latest session (2026-11-15, second account)

- **Body mass and an active ragdoll** (see ROADMAP "Recently done"): `src/entity/BodyMass.h`, `src/physics/Ragdoll.h`, hooks in `Human.h`, `Player::refreshMass`, `VoxelPhysics::stepRagdoll`, `main.cpp`, `CharacterRenderer.h` (`setWorldPose`), console commands in `Commands.h`.
- **Tests:** `tests/ragdoll_test.cpp`, built with `-DVOXELRPG_TESTS=ON`. All pass. Checked in game (software rendering): knockdown from front and back, lying, getting up, a shove stagger; the third-person camera follows the ragdoll's head so a fallen body stays in frame.
- **Woodcutting** (see ROADMAP "Recently done"): the axe as a tool, Valheim-style swing with a traced blade and speed-based damage, trees falling as hinged rigid bodies, logs, wood drops and pickup. Files: `game/AxeSwing.h`, `character/HeldItemModel.h`, `physics/RigidVoxelBody.h`, `physics/VoxelBall.h`, `world/Woodcutting.h`, `StructureManager::fell`, `PlayerInteraction::axeHit/fellTree`, main loop wiring. `tests/woodcut_test.cpp` (run from the repo root). Checked in game: swing, hit, fall direction, log, chopping it up, pickup.
- **Woodcutting follow-up:** fallen trees stay entities (never written back into the world), are solid to bodies and other logs, and are drawn with the world's textures and lighting (`render/PropRenderer.h`, `shaders/prop.*`). `render/PropMeshes.h` is gone.
- **Sword and combat:** `game/Combat.h` (moveset table, keyframed attack poses), `game/CombatMoves.h` (start / advance / stamina, called from `VoxelPhysics::moveHuman`), `game/CombatHits.h` (hits for every body against every other, guard, poise, i-frames, death; body separation), `game/Npcs.h` (Npc, brain, manager), `HeldModel::Sword` in `character/HeldItemModel.h`, stamina and attack state on `Human`, `Player::wielded/refreshWeapon`, controller inputs, animator overlays, HUD (stamina, enemy bars, YOU DIED), commands `spawn/despawn/heal`, main loop (NPC ticks, hit resolution, NPC drawing, death and respawn). `tests/combat_test.cpp`. All three tests pass.
- **Checked in game** (Test Play, iron sword in the toolbar): light combo, heavy knockdown on a dummy, air lurch, plunge slam, a swordsman circling, guarding and hitting the player, logs lit and textured. Not checked in game: Survival death and respawn, guard break (both covered by the test).
- **Tip for testing here:** the console helper needs the backtick held ~1.2 s and Return ~1.5 s; after a command the console stays open (Escape closes it; a second Escape opens the pause menu). The first minute or two after loading a world, frames are very slow while chunks stream in. The dense forest near the old test spot runs at a frame every few seconds in software rendering; the meadow at about (2400, 2045) is fast. Place the player in player.dat at ground height, or they spend a while falling.
- **Eddie said** generation refinement can wait; entities are the focus now.

## State of the code (before that session)

Everything below is in this zip and not yet pushed.

### Done and tested this session

- **Lighting:**
  - Baked RGB light layer (`src/world/LightBake.h`): 2×2×2-voxel cells, 16 levels per channel plus sky, one 3D texture per section. Uploaded and kept in `WorldStreamer.h`.
  - Light blocks: `light = [r,g,b]` and `light_pass` in `data/registry/blocks.toml`.
  - Day/night cycle, sky, sun and moon (`src/render/Sky.h`); the `/time` console command.
  - Distance fog, plus a haze band where the voxel chunks meet the low-detail far terrain (`seamFog` in `shaders/chunk.frag` and `shaders/far.frag`; numbers in `main.cpp`, `seamStart`/`seamEnd`/`seamHaze`).
  - Distant lights and low-res memory of visited chunks (`src/world/WorldMemory.h`, saved as `lowres.dat`).
  - Fixed: the chunk-edge dark lines and the odd/even grass shading.
- **Canopy light:** leaves filter the sky to a dim green instead of blocking it. The bake records a per-column canopy (`ColumnLight::canopy`); the shader samples it from `uCanopy` (texture unit 2).
- **Decorations** (`src/world/Decorations.h`, `data/registry/decorations.toml`):
  - Layers: `[[plant]]`, `[[decor]]`, `[[ground]]`, `[[landmark]]`, `[[tree]]`, each a recipe plus blocks plus density per biome.
  - Sprite plants: blocks with `render = "sprite"` draw as crossed flat pictures (sprite pass in `Mesher.h`, faces 6 and 7 in `chunk.vert`).
  - Plants are walk-through (`plant` flag). Breaking one picks up the whole plant as an item (`PlayerInteraction.h`: `breakPlant`, `placeDecor`).
  - Spawn willow landmark with a flower meadow.
  - Dev menu "Decorations" tab.
  - World Settings sliders: Plants and Landmarks.
- **Trees** (`[[tree]]` in `decorations.toml`, recipes in `Decorations.h`):
  - Recipes: willow, twisted_oak / snag, maple, birch, cypress, mangrove.
  - The `oak` entry replaces the built-in oak (`replaces = "oak_tree"`); `scale` and `twist` adjust each tree.
  - They are choppable through `StructureRegistry::addDecorTrees`.
- **Rivers:**
  - Banks settle: low dips beside a river become a pond or a gentle slope (`settleRiverBanks`).
  - Rivers stop at lakes.
  - Bank flattening blends out instead of leaving walls.
- **Biome rework of the volcanic quarter:**
  - Karst (limestone)
  - Canyons (red sand; banded sandstone on cliffs)
  - Autumn uplands (golden birch and aspen)
  - Ashlands, which now ring each lava lake (ash, cinders, basalt, fumaroles, charred snags)
  - The surface remap is in `surfaceBiome`; the looks are in `surfaceDetail` and `canyonSurface` (all in `IslandGenerator.h`).
- **Canyons (terrain):**
  - Planned in `planCanyons`, cut in `canyonCut` (coarse before erosion, stepped walls after).
  - Each canyon carries a river. Walls are banded limestone or sandstone.
  - Karst pillars and hoodoos are switched off (empty biome lists).
- **Eye height:** slider in Settings (`eye_height` in `settings.toml`, `Human::eyeStand`); the default is now 16 voxels (5'4").

### In progress when this session stopped (built, partly tested)

Eddie asked:
1. Rivers must end at a canyon, never cross it.
2. Always exactly two canyons on island worlds.
3. A unique biome between them.
4. A village there.

What is done:
- **`GEN_VERSION` is 6**, so worlds rebuild their terrain bake on load.
- **Rivers end at canyons:** `crossesCanyon()` trims designed rivers in `smoothPath`, and baked rivers after the bake.
- **Lakes drained:** `drainCanyonLakes()` empties lakes a canyon cuts through.
- **Two canyons always:** `planCanyons` picks the pair with the most mesa ground between them, relaxing the rules over 4 attempts. On seeds 42, 1234, 777, 99991 and 5 every world got 2 canyons, a mesa and a village.
- **Unique biome:** `CanyonMesa` (`IslandChart.h`; set in `planMesa` and `surfaceBiome`). Open grassland with limestone breaking through, flowers, and birch, maple and red oak groves. Uplands only, kept 4.5 cells clear of lava.
- **Village:** placed at the mesa's heart (`planMesa`, marker "Canyon village").
- **Remaining water over canyon walls:** 0.2–0.5% of samples (test `canyon_seeds`).

**Next to check:** the in-game look of the canyon village. On seed 42 it's at world 5056, 9280. The last screenshot from above showed the village plaza as a plain grey disc with a river running through it. The village may need to avoid river cells, or the river should stop at the village. Then make a final package.

### Known issues (also in ROADMAP.md)

- In the deepest gorges the canyon river fills the floor wall to wall; a dry gravel strip beside the water would read better.
- No side canyons yet.
- A few short ramps remain where two rivers' banks meet.
- A sprite plant crossing a section boundary (every 128 voxels up) draws only its lower part.
- Lily pads are voxel slabs; flat pictures on the water would look better.
- Performance:
  - The big trees are heavy up close.
  - Generation is about 29 ms per chunk.
  - The light bake is 50–110 ms per column.

### Ideas Eddie liked, not started

- Waterfalls where side streams drop into a canyon.
- Moving water.
- Caves.

## Building and running the game in a cloud session (Linux)

Some cloud images have no X11/GL development headers, no `xdotool` and no package
installs, but do have the runtime libraries (Mesa llvmpipe, libGL, libX*), Xvfb and
git access to public GitHub. What worked (2026-11-15):

1. Sources: `git clone --depth 1` glm (1.0.1), tomlplusplus (v3.4.0), glfw (3.4).
2. Missing headers: `tiawl/X11.zig` (branch `stable`) has `Xrandr.h Xinerama.h XInput.h
   XInput2.h Xfixes.h` and `Xcursor/Xcursor.h.in` (fill in the version macros);
   `mirror/mesa` (sparse: `include/GL include/KHR`) has the GL headers. Copy only the
   missing ones into one include folder, and make `libGL.so`, `libXrandr.so`, … symlinks
   to the installed `.so.N` files in a lib folder.
3. Configure: `cmake -S <repo> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release
   -DVOXELRPG_TESTS=ON -DFETCHCONTENT_SOURCE_DIR_{GLFW,GLM,TOMLPLUSPLUS}=<clones>
   -DCMAKE_INCLUDE_PATH=<inc> -DCMAKE_LIBRARY_PATH=<lib> -DCMAKE_{C,CXX}_FLAGS=-I<inc>
   -DOpenGL_GL_PREFERENCE=LEGACY -DOPENGL_gl_LIBRARY=<lib>/libGL.so -DOPENGL_INCLUDE_DIR=<inc>
   -DGLFW_BUILD_WAYLAND=OFF`. Build in a folder OUTSIDE the repo (the repo tracks `build/`).
4. Run from a scratch copy of `data/` and `shaders/` (the game writes saves and settings):
   `Xvfb :99 -screen 0 1280x720x24 &` then `DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1
   MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 ./VoxelRPG`
   (the game asks for GL 4.6; llvmpipe reports 4.5). Lower `chunk_distance` to 3 in the
   copy's settings.toml; screenshots with `import -window root shot.png`.
5. Input without xdotool: `tools/xin.py` (XTest through ctypes: click, key, down/up, type, rel).
   At software-render frame rates HOLD keys: backtick 1.2 s to open the console, Enter 1.0 s
   per command (shorter presses get missed and commands run together), Escape to close.
   F5 held 1 s toggles third person. Frames are clamped to 0.25 s of game time, so the
   game runs in slow motion here; timers take 2-4x longer in real time. Inside or near a
   dense canopy frames take seconds and input is lost: set up scenes by editing the
   scratch save's player.dat (pos, look=<yaw> <pitch>, mode, inv.selected, dev.slotN)
   instead of steering, and read logs.dat etc. for positions. `bdown`/`bup` hold the mouse.

## Test setup (cloud only; won't exist in a new session)

- **Build and test layout:** builds were in `/home/claude/work/bl`. Unit tests are single-file programs under `/home/claude/work/t/unit` (`light_test`, `decor_test`, `canyon_seeds`, `river_walls`, `tree_sheet`, …).
- **Compile flags:** `-I src -I <glm> -I src/glad/include -I <toml++>`, plus the project's `lz4.c`.
- **Registry:** any program that uses the generator needs a `VoxelRegistry gRegistry;` definition and must call `gRegistry.load("data/registry/blocks.toml")` first.
- **In-game testing:** done under Xvfb with xdotool. The console key is backtick; use absolute coordinates (`~` can't be typed). Software rendering is slow, so type slowly and wait.
