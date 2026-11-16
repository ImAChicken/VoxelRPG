# VoxelRPG Roadmap

Living list of planned work. Sections marked **Tabled** are agreed
ideas parked for later; add to them as new ideas come up.

## Recently done
- World types (Classic, Island, One biome) with the chart preview on the New World screen
- Island chart by wave function collapse (worldgen/BiomeCollapse.h): height levels per biome, neighbours within one level, quadrant themes, pinned peak and swamp, then elevation, rivers that run downhill, lakes, road and settlements on a layer over the land
- Per-biome relief (worldgen/BiomeTable.h): plains and moor mostly flat with scattered hills, forest rolling, mountains ridged, swamp hummocks with standing water; blended across borders
- Biome borders mix in a ragged band sized per biome; beaches follow height; swamp mud by water level; rivers and the road wind instead of following the chunk grid
- Terrain bake with erosion (worldgen/TerrainBake.h): the island's ground on an 8-voxel grid, cut by stream-power erosion (valleys and gullies where water gathers, ridges stay sharp), scree on the steepest faces, hollows drained or kept as lakes, streams traced from the water flow. New World option: Guided rivers (the designed river leads, streams join it) / Natural rivers / No erosion
- Bake runs behind a progress screen and is cached in the world folder (terrain.bake, rebuilt if the world's settings or the generator change)
- Rock formations: boulders, outcrops, spires and overhanging ledges in plain, volcanic and mossy stone, on steep ground, scree and by biome
- Wet banks along streams and lake shores (mud, bare earth, moss on rock)
- Water level with its banks: rivers held flat and stepping down, banks and lake rims brought to one voxel above the water (rock gorges and real cliffs keep their walls)
- Defined waterfalls: steep reaches become one 3 ft+ fall with a rock lip and face and a plunge pool
- Alcoves and overhangs hollowed into steep rock faces (sealed pockets filled back in)
- Decorations: bushes, big bushes, fallen logs, stone piles (plain, mossy, volcanic) by biome
- World Settings on the New World screen (worldgen/WorldGenParams.h): sliders for hilliness, mountain ruggedness, erosion strength and passes, scree angle, biome edge width, flat banks, streams, natural lakes, waterfall height, trees, rocks, bushes and logs, cliff alcoves, plus a weight per biome; defaults are the tuned values, saved in world.vxrp
- Stacked chunk sections: world is 1024 voxels (341 ft) tall
- Island elevation rework: hills, ridged mountain, sea cliffs, river valleys, lake basins, rock and snow by slope and height
- Distant terrain (FarTerrain): generator-driven height map out to the horizon, ocean, haze
- Builder's Hammer, structures (foundations, walls, doors), tree health and chopping
- Dev menu, dev tools, console commands, keybinding and settings screens
- Player model: stylised low-poly body generated from an appearance (3 builds, 3 heights, shoulder/chest/hip/limb/head sliders, 5 face shapes), code-driven animation
- Hair as voxels turned into shards (19 styles, including crew cut, undercut, slicked back, curtains, mohawk, mullet and bald); hats, ears and horns (5 extras) as their own separate shards
- Painted faces from the resource pack (19 eye styles, 5 expressions, blinking); toon shading; toon edge lines (Settings: Off / Lines / Lines + echo, Black / Tinted)
- View settings: Chunk distance (voxel chunks, 2–16) and Render distance (distant terrain, 16 chunks to the whole world)
- Pickaxes break a 6-voxel (2 ft) cube, on a grid unless Shift is held; dev Cube Tool places or breaks any size of cube (Up / Down, `/cube`)
- Character creator (Esc → Character): Body / Face / Hair / Outfit / Favorites tabs, themed by the resource pack; favorites shared by all worlds
- Decorations from a data file (data/registry/decorations.toml, world/Decorations.h): plants (one per 16-inch square), bushes / logs / stones, ground patches (moss, leaf litter, sulphur, lichen) and landmarks, each a recipe + blocks + density per biome
- Sprite plants: flowers, grasses, ferns, reeds and cattails drawn as crossed see-through pictures (render = "sprite" in blocks.toml), as tall as their voxel stack
- Plants are walk-through; breaking any part picks up the whole plant as an item (Survival: player inventory); plant items place the plant, snapped to its square
- Landmarks: a large weeping willow ringed by a flower meadow at every island spawn; weeping willows, standing stones, stone circles and cairns by biome
- River banks settled: a dip beside a river either fills to the river's level (a pond opening off it, a wider bend) or is built up into a sloped embankment, so no river is held up by a thin wall; lake rims slope the same way; rivers end at a lake's edge and start again at its outlet instead of running across it
- New trees from decorations.toml ([[tree]]), mixed in with the usual ones: twisted red oaks, sugar and red maples (autumn colours), willows by water; in the swamp bald cypresses (buttressed, knees, moss) and mangroves (stilt roots) standing in shallow water, and mossy swamp oaks. Choppable like the built-in trees
- The basic oak is replaced by a new-style oak (a calmer twisted oak, same height); the new trees are 1.4–1.7× their first size (scale in decorations.toml)
- Seam haze is a visible band at the edge of the voxel chunks, measured along their square, thinning again over the far terrain
- River bank walls removed: bank flattening blends out instead of stopping at a hard limit; embankments only fill shallow dips that don't touch lower water
- Volcanic quadrant reworked (same terrain, new surfaces): Karst (limestone pavement with grassy cracks, limestone towers, pines and golden birches), Canyons (red sand, terraced slopes in banded sandstone, hoodoos, junipers), Autumn uplands (new: golden birch and aspen woods, maples, leaf litter, bracken), and Ashlands moved to the ground around lava lakes (ash, cinders, jointed basalt, fumaroles, obsidian, pumice, charred snags)
- Canyons (terrain): up to three canyons planned through the volcanic quadrant's uplands, each from high ground to the sea, carrying a river. Cut ~70 voxels below the real ground with a flat floor and stepped walls (a ledge every 14 voxels), in banded limestone (red sandstone in the canyon country); walls kept out of erosion and bank flattening so they stay sheer
- Karst towers and hoodoos switched off (decorations.toml) in favour of the canyons; sandstone only on real cliffs
- Eye height slider (Settings, 4 ft 4 in to 5 ft 10 in; default raised to 5 ft 4 in)
- Volcanic rock redrawn as dark jointed basalt (no glowing streaks)
- Leaves filter the sky instead of blocking it: the light under a canopy is a dim green (per-column canopy texture), characters included
- Dev menu Decorations tab (every plant, decoration and landmark as an item); World Settings: Plants and Landmarks sliders
- Body mass (entity/BodyMass.h): every human's body split into 15 parts weighted from the look (build, height, sliders: 49 kg slim 5'4" to 112 kg sturdy 6'0"), plus worn gear on the parts it covers, the drawn weapon in the right hand (sheathed: on the back) and the held tool. Items have `weight` (kg) and `worn = head|torso|hands|legs|feet|back` in items.toml; nine test pieces of gear (cap to plate cuirass, travel pack), worn slots saved with the inventory, dev menu Gear page
- Active ragdoll (physics/Ragdoll.h) on top of the animation: 20 weighted points with voxel collision, muscles that pull each joint back to its animated angle (force-limited, so weight in the hand slows that arm) and a balance pull on the core. Hits stagger the body and it recovers in under a second; a hit over 35 vox/s (impulse / mass) knocks it down limp, it tumbles and settles, then gets back up. Heavy bodies stagger less and fall less often. Knees and elbows hinge the right way; other joints have cone limits
- Hits are queued on the body (Human::hit) and applied by physics (IPhysics::stepRagdoll); the explosion tool now pushes by impulse / mass and can knock the player down; first-person view follows the ragdoll's head
- Console: shove, knockdown, mass, wear, ragdoll [active on|off]
- tests/ragdoll_test.cpp: headless checks for the ragdoll and body mass (CMake option VOXELRPG_TESTS)
- The axe is a TOOL, not a weapon (items.toml). Held, it forces the third-person camera and is drawn in the right hand, built from voxels like the hair (character/HeldItemModel.h); carried head-down beside the leg
- Valheim-style swing (game/AxeSwing.h): left click (hold to chain), a 1 s wind-up over the right shoulder and a diagonal chop with the torso twisting, a step with the left foot, follow-through to the left hip; walking at a third of the speed meanwhile; looking down bends the chop low (for logs on the ground). The blade's edge is traced through the world each frame: the first solid voxel it passes is hit, damage = tool power × the edge's speed (35–130%), with a short hit-stop
- Felling (world/Woodcutting.h, physics/RigidVoxelBody.h): at 0 health the tree's voxels come out of its structure shape; a 2-voxel stump stays; the leaves break off as falling, fluttering particles; the wood becomes one heavy rigid body (0.6 kg a voxel) that turns over a hinge at the cut, away from the axe, then is released to land and roll; it hits bodies through the ragdoll. It stays a loose entity (no longer written back into the world): bodies, the axe and other logs collide with it (IPhysics::setLooseSolids), and it is saved with its pose (logs.dat, version 2)
- Fallen logs have health (75% of the tree's); each axe hit drops its share of the tree's wood on the ground, the last hit breaks the log up. Wood on the ground falls, settles, slides toward you and is picked up by walking over it. Logs and drops are saved (logs.dat, drops.dat)
- Right-click chopping with other tools also fells trees this way; tree wood now comes only from chopping the log
- tests/woodcut_test.cpp: the axe item and model, swing geometry and speed, blade hits on trunks and (looking down) logs, felling in all four directions, a falling log knocking a body, chopping logs into drops, pickup, save/load
- Loose objects drawn like the world (render/PropRenderer.h, shaders/prop.*): logs, wood drops and chips use the block atlas tiles and the chunk shading (face shade, baked block and sky light, canopy filtering), lit per face from where each face is and relit when a log moves
- Swords and souls-like combat (game/Combat.h, CombatMoves.h, CombatHits.h). The iron sword is drawn in the right hand, built from voxels (blade with fuller and point, crossguard, grip, pommel). Wielded from the weapon slot (Survival, R) or held in the toolbar (any mode with a body): third person, the mouse fights instead of building
  - Left click: light attack, a two-swing combo (a press once the swing is active is buffered and cuts the recovery short). Right click: heavy overhead chop, slow, knocks down, has poise against light hits once wound up
  - In the air: light lurches forward with a wide slash; heavy hangs a moment, plunges straight down and slams the ground, hitting and knocking down everyone around. One air attack per jump; an attack also breaks the double-jump / air-dodge hang
  - Q held: guard. Blocks 90% from the front for stamina; out of stamina the guard breaks (stagger, half damage)
  - Stamina (bar under health): attacks, rolls, backsteps and air dodges cost it; it comes back after a short pause, slower while guarding; infinite in Test Play
  - Committed attacks: a wind-up, an active window, recovery; only a dodge late in the recovery cancels one. Rolls, backsteps and air dodges have i-frames
  - Hits sweep the swing's arc over the active window, in reach and height, need a clear line (no solid voxel between), land once per swing, pause the attacker briefly (hit-stop) and knock the target's ragdoll (stagger or knockdown)
  - Any Human uses the same moveset through HumanIntent (attackLight / attackHeavy / guard): physics runs it for the player and enemies alike, and every attack is checked against every body
- Enemies (game/Npcs.h): NPCs are Humans with a brain filling their intent, random looks (Appearance::random), their own ragdoll and a sword. Swordsman: walks up, circles, attacks (light combos, heavies), guards, rolls out of the way of attacks. Dummy: stands and takes hits (heals after 4 s). Health bars over their heads; the dead go limp, stay down and vanish after 20 s. Bodies push each other apart instead of passing through
- Player death (Survival): "YOU DIED", back at the spawn point after 3 s. Test Play: hits land, health doesn't drop
- Console: spawn, despawn, heal (see below for the newer ones)
- Simulation split (sim/Simulation.h): the game state and fixed tick with no graphics; players' intents go in like network input; entities by id; events out. tests/sim_test.cpp
- Debug overlay (F3 / `debug`): bodies, ragdoll, combat arcs, labels, props, sight lines, chunk borders, NPC brains (state, territory, sight cones, paths, breadcrumbs, encounter slots and attack tokens) and a text panel
- Weapons from data: movesets.toml (attacks per slot), weapon weight scales speed / stamina / poise, held models as voxel files (data/models/held/*.vxm); F6 reloads them
- Poise for every body (flinch while it holds; stagger or knockdown when it breaks; hyper armour); the axe hits bodies too
- Double jump off by default (an ability flag per body)
- NPCs from data: types (npcs.toml), looks (data/characters/presets.toml), factions with per-player reputation (factions.toml); brain states as plug-ins (idle, unavailable, patrol, alert, combat, search, return, flee, dead) driven by events from senses (sight, hearing of footsteps / fights / battle cries, damage, ally deaths, menus); territories and leashes; battle cries that pass on your last breadcrumb; trackers that follow your trail
- Pathfinding (npc/Pathfinder.h): A* over standing spots with steps, jumps and drops; path following with stuck detection
- Encounters (npc/Encounters.h): open on the first NPC to fight a player, merge when they share enemies, close when the enemies are gone or the players are dead / got away; slots around each target (front, rear flank, …) by type preference with a stable front; at most 2 attackers at once (attack tokens); breadcrumbs every second; start banner and end review (XP shared, loot placeholder)
- Resource pack 'jrpg' (the main pack now): new JRPG face sheets on the Dark Medieval textures and UI
- NPC saving (npcs.dat) and distance tiers (full / lite / frozen; frozen over unloaded ground)
- Talking: E on an NPC opens a placeholder menu (its 'unavailable' state); protected or interrupted when attacked
- Console: spawn <type> [count] [distance], smite [radius], rep [faction] [value], debug [layer] [on|off]
- tests/npc_test.cpp: types, sight / alert / battle cry, hearing, factions and reputation, menus, search and leash, pathfinding, encounters (slots, tokens, merge, review, escape, tracking), saving and tiers
- tests/combat_test.cpp: costs, combo, reach and arc, heavy knockdown, poise, guard and guard break, roll i-frames, stamina, air lurch and plunge slam, death, line of sight, the enemy brain using the same moves

## Terrain (planned)
- Rock formations from structure files, so they can be designed in game (now generated from noise)
- Plants that need new block art: reeds, tall grass, flowers, mushrooms, ferns
- Waterfalls with falling-water blocks once water can flow

## Biomes (planned)
- Lily pads as flat pictures on the water instead of one-voxel slabs; more sprite art (mushrooms, heather)
- Kelp and seagrass under the sea (needs plants that keep their water)
- More tree kinds per biome, loaded from structure files
- Move the biome table to a data file (biomes.toml) so it can be tuned without recompiling
- Saved World Settings presets; per-biome sliders for relief and tree density
- Underground: layers and ores by biome, real caves under the cave sites
- The old pass-by-pass chart (IslandChart.h generateIsland) is unused now; remove it once the collapse is settled

## Up next
- Review scale and building in the taller world
- Check lower-end hardware: lower the default chunk / render distance only if it struggles

## Characters (planned)
- Edge lines will most likely settle on solid black; the Off / echo / tinted options stay for testing until then
- Polish the eye styles further (the art is in `data/resourcepacks/<pack>/characters/face_*.png`)
- Hitboxes that follow the body type (collision stays the same 17-voxel capsule for everyone)
- First-person arms holding the current tool
- Hair styles and extras as voxel files, so they can be designed in game or generated
- `.vox` (MagicaVoxel) import for hair, outfits and story characters' heads
- Keyframed animations to replace the code-driven ones where needed
- Ragdoll: a real get-up animation (now the body blends from lying to standing over 0.8 s); hard landings and falls as hits; self-collision between limbs; a ragdoll that tumbles on a plunge slam
- Worn gear models on the body (gear has weight only for now); encumbrance (carried weight slowing movement) if wanted
- Combat next: lock-on (camera on the target, strafing), parry and ripostes, backstabs, more weapons (club, spear: they have no moveset or model yet) with their own movesets, damage types against resistances, hit and block sounds and sparks, archers (a 'ring' slot at range)
- Animation clips (step 8 of the agreed plan): ozz-animation runtime + Quaternius Universal Animation Library 1 and 2 (CC0), retargeted onto our bones; attack timing as clip markers; weapon traces sampled from the animated pose for every weapon (replacing the arc sweep) and running in the simulation (server-side)
- Multiplayer prep still client-side: the axe swing and block editing (PlayerInteraction) should become requests to the Simulation; entity ids should become stable across saves and machines (now a counter)
- NPCs next: real menus (dialogue, trade) behind the 'unavailable' state, loot tables for the review, NPC texture sets (presets' texture_set, e.g. undead) drawn by the renderer, a second skeleton for animals, patrol routes placed with a dev tool, opportunistic NPCs picking on fleeing ones, morale
- Woodcutting: chop sounds and a little tree shake on hits; stamina; stumps that can be dug out; saplings or sticks from the leaves; other tools digging a log voxel by voxel skip its wood count; falling trees only move while a body is in the world (not in Spectator); logs float in water later; axe tiers (stone, bronze) with their own power
- Offer the character creator when a new world starts

## Tools (planned)
- Tune pickaxe cube sizes and dig times per tier once Survival is played more (`cube` and `tool_power` in items.toml)
- Shovels and axes with their own cube or shape behaviour

## Known gaps
- Caves are only pits at cave cells
- Water and lava are static (no flow)
- Villages are flattened plazas with no buildings yet
- Distant terrain shows player edits only for chunks that have been loaded at least once (remembered in lowres.dat)
- No real-time shadows: shading is baked sky light plus sun facing
- Light bake is 50–110 ms per column on one worker; fine for streaming, could be trimmed
- Canyon rivers fill the whole floor in the deepest gorges; a dry gravel strip beside the water would read better
- Side canyons (shorter branches joining the main ones) not done yet
- A few short ramps remain where two rivers' banks meet (each bank follows its nearest river)
- A sprite plant standing across a section boundary (every 128 voxels up) draws only its lower part

## Lighting (Done)
- Baked light layer, separate from chunk data: 2×2×2-voxel cells, RGB at 16 levels per channel plus a sky channel, one 3D texture per section (uniform sections share a 1³ texture)
- Light blocks registry: `light = [r,g,b]` and `light_pass` in blocks.toml. New blocks: lantern, 8 coloured lamps, glow mushroom, candle; torch, fire and lava carry colours
- 20-minute day/night (`/time day|noon|dusk|night|midnight|dawn|0-24|freeze|run|length <min>`), saved with the world
- Sky dome with sun, moon, stars; warm dusk; blue moonlight; caves fall to near black at night
- Thin distance fog that follows the sky colour, plus a haze band at the voxel / far-terrain seam
- Glowing blocks draw fullbright and cut through haze; remembered lights show as points across the island at night
- Low-res chunk summaries (the first item below) feed the far terrain, so edits show in the distance

### Lighting ideas (later)
- Sun shadows (shadow map) for near terrain
- Light-aware water and glass (tinted light through coloured glass)
- Flicker for torches and fire

## Chunk rendering optimizations (Partly done; rest tabled)
Done: per-column low-res summaries (top height + top block every 4 voxels) and remembered lights, kept in `lowres.dat`, drawn by the far terrain. Still tabled: generator far-cache and low-poly tree models.

Ideas for drawing more of the world, faster, and keeping the far view
true to player edits.

- **Saved chunks first, generator as the fallback.** The far view
  draws each chunk from the best data it has, in order: the chunk's
  own stored low-res copy (saved or loaded chunks) → a cached
  generator sample → the generator itself. Anything ungenerated still
  renders; anything generated or edited renders from its real data,
  so all changes show in the distance.
- **Low-res copy stored in saved chunks.** When a chunk is saved,
  also store a pre-packed downsampled copy of it (4× and/or 8×,
  majority-vote voxels, a few KB compressed) in the chunk file's
  reserved `extDataOffset` block, plus a surface summary (height and
  top-block colour every 4 voxels, coarser levels pre-built). The
  far view greedy-meshes the low-res copy, so terrain edits,
  buildings, felled trees and overhangs all show as they really are.
- **Per-world summary index.** One file holding every saved chunk's
  low-res data, read in one go at world load instead of opening each
  chunk file.
- **Generator far-cache.** On first open, worker threads sample the
  whole world at 8-voxel spacing and save it (~10 MB for a medium
  island) so later loads show the whole island instantly. Stamped
  with a generator version so it rebuilds when generation changes.
- **Live updates.** Editing a loaded chunk rebuilds its in-memory
  low-res copy right away and refreshes the far region holding it,
  without waiting for a save.
- **Low-poly tree models from the structure registry — for land
  with no chunk data.** Generated-only land still needs canopies.
  Each tree type in the structure registry (`structures.toml`) gets
  small LOD meshes made by downsampling its voxel shape 4× / 8× and
  greedy-meshing it, built once at load. Natural trees on untouched
  land (positions and types known from the seed via `treeInCell`) are
  drawn as instances of those meshes per far region. Any tree type
  added to the registry gets far models automatically. Replaces the
  far terrain's fake raised canopy. Chunks with stored data don't
  need this; their trees are already in the low-res copy.
- **Sprite impostors (optional).** Camera-facing quads for the
  farthest generator-only trees, if the low-poly tier costs too much.
- **Distance tiers.** Full voxels (≤ MESH_DISTANCE) → low-res chunk
  copies / generator terrain with tree models → canopy colour or
  sprites at the far end.
- **Smaller saved chunks.** Store all-air and single-block sections
  as a marker instead of LZ4 of 4 MB (edited columns are ~300 KB now).
- **Streamer bookkeeping.** Per-frame loops over every column slot are
  fine at 128 × 128 chunks; switch to an active list if worlds grow.
