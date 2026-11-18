# VoxelRPG Roadmap

Living list of planned work. Sections marked **Tabled** are agreed
ideas parked for later; add to them as new ideas come up.

## Recently done
- Spellbooks and the ice spike (2026-11-18): spellbook is a new weapon type (items.toml category "spellbook"): it goes in the X / Y / B slots or the off hand, casts its spell and is held while it does (a book model per spell, made by tools/gen_held_models.py; an icon from it). Windburst is now a spellbook. Ice Spike Spellbook: a shard from the chest along your facing (turning onto an enemy within 12 degrees: there's no aiming up or down), falling a little as it flies; the first body it meets takes 18 damage, poise and a knock; where it meets the ground it leaves ice (a new block) as a structure in the same registry as trees (structures.toml ice_spike, kind "effect", lifetime = 15) that melts after 15 s, never set down over a body. Any structure can have a lifetime now. Both books are in the starting kit (X: 4 steps between them). tests/spell_test.cpp
- Slime (2026-11-18), the second enemy type: in npcs.toml like the people (faction "slimes", hostile to people and players), with a body that isn't a person: 5 voxels tall, no skeleton or ragdoll (hits push it; its hit box is taller than it, so swings at a person's height still land). It gets about by hopping: still on the ground between hops, moving only in the air; a higher hop up a ledge its path needs. Same brain, paths and fight slots as the others; its attack is a lunge (a long hop at the target) and its range comes from how far that carries. It hurts whatever it touches that isn't on its side (8 damage, a shove, once every 0.8 s); a guard raised toward it blocks. Drawn as a stacked-box blob with eyes that squashes and stretches. The pathfinder can now jump up a ledge whose foot is no place to stand (it reaches over that cell): people benefit too. tests/slime_test.cpp
- Building and tools on servers (2026-11-18): every change a player's hands make is a WorldRequest (game/WorldActions.h: break, pickaxe cube, place a block or plant, door, hammer piece, blueprint, hoe, blast, axe hit). In your own world it's done at once; on a server it's sent (Request message) and the server checks it (the item is carried and right for the job, the spot within reach of the eyes, at most 20 a second, the hoe's and blast's cooldowns) and does the same code for that player. Costs are paid from the server's inventory; a blueprint is sent as its text, and in Survival its chests come empty (no items from nothing). What the server says ("too far away", "needs 6 Stone") comes back as a notice. Still local only: dev tools (Test Play's own worlds), chests (opening one on a server says not yet), the logs of a felled tree (on a server its wood goes straight into the pack)
- Data versions between client and server (protocol VERSION 2): the Welcome carries the server's names for its block and item numbers, and the client turns every number it gets or sends into the other side's (proto::IdMap: chunks, edits, the loadout, the item used, requests). Adding or reordering blocks or items no longer breaks a game against an older or newer server. A block the client has never heard of shows as stone if solid, else air; missing names are listed when you join. tests/net_test.cpp
- Controller layout (2026-11-18, Eddie's design; input/PadLayout.h is the whole table): any GLFW gamepad. Combat: LB sprint/dodge, RB light, RT heavy, LT special, X/Y/B use the slot's item, D-pad left/up/right cycle them, down the next weapon set, Start held + D-pad/face buttons the 8 quick options, A tap jump / hold interact, L3 sneak (toggle), R3 lock on (right stick swaps targets), Select out of combat (no menus in combat). Out of combat: LB/RB hotbar, LT/RT left/right click, B sprint/roll, Y inventory, X turn piece, D-pad nudge, Start pause. Keyboard stands in: 1/2/3 use, 4/5/6 cycle, X next set, Q special, hold Left Alt + 1-8 quick. Keyboard and mouse checked in game (2026-11-18): left / right click light / heavy, Q special, Shift sprint / dodge, Tab lock on, R combat on / off, I still opens the inventory in combat (only the pad has no menu button there); a weapon held in the toolbar (not drawn) keeps the number keys for the hotbar and Q as guard. The HUD and the inventory's loadout panel name the keys when the keyboard was used last ("4 5 6: next X Y B, hold LEFT ALT: quick"); keyboard.toml lists the new keys
- Loadouts (game/CombatKit.h, saved in player.dat), set up on the right of the inventory screen (Survival): armour and amulet at the top (other worn gear small below), then Loadout 1 and 2 side by side — primary weapon (the Weapon 1 / 2 equipment slot), off hand (empty: the primary blocks and parries), 3 items each for X, Y and B, 2 charms — and the 8 quick options (Start held) at the bottom. Drag items in; drag out, right click or shift click to clear. Nothing leaves the inventory: one item can be in several places. Slots highlight where the dragged item fits. Console `kit` does the same. LT: a spell in the off hand casts, otherwise guard (block, timed = parry), or `special = "heavy"` on an item. New item category `charm` (no charms yet)
- Windburst (items.toml spell, movesets.toml windburst_cast with effect = "windburst"): a cast animation, then a cone that throws bodies back, harder the closer, with poise damage so a body low on poise is knocked down. Spells are items in the X/Y/B lists; in the starting kit
- Combat HUD: the face buttons with their items, the shoulders with what they do now, the weapon set; the quick layer while Start (Alt) is held. Button names follow the device used last. `debug pad`: a controller drawing, lit by the pad or by the keys standing in for it
- Second enemy type: done (the slime, see above). Next for it: bigger slimes that split, a slime spawner, a proper model
- Server movement in step with the client (2026-11-18): the server moves a player one step per input it sent, never one it didn't (Simulation::setInputs): late frames wait, a burst after a hitch catches up two a tick, only a stall over a second drops inputs. The client waits for its first placement and its ground before predicting, corrects position and velocity together, and eases the camera over a correction (Human::renderOffset). Sprint-jumping with hitches: 22 corrections before, 0 now (tests/net_test.cpp)
- Starting kit in one file (data/registry/kits.toml) for the game, the server (unless server.toml lists its own) and the dev inventory: hammer, hoe, Blast Wand, sword, axe, spear, staff, club, pickaxe, shovel, jerkin, torches
- The hoe, Valheim style (T: level to your feet, raise, lower, gravel path), on world/TerrainOps.h; buildings are never touched; raise and lower stop 2 ft from your feet
- Blast Wand: a player tool (left: blast, right: big blast) for testing destruction in Survival
- Popups: one lifetime and fade for notices, the encounter banner and the review (game/HudScreen.h); the review goes as soon as you attack or a new fight starts
- Eye height: the first-person camera follows the character's height; the Face tab has an Eye height slider that moves the painted eyes and brows on the face (Appearance::eyeHeight). The Settings slider is gone
- Slopes cost time for every body (VoxelPhysics::slopeFactor): about half speed up 45 degrees, a little quicker downhill; npcs.toml `climb` per type; paths weigh climbing the same way, and the pathfinder no longer treats a 1-2 voxel terrace as a wall (tests/slope_test.cpp)
- Context steering for NPCs near their spot in a fight (npc/ContextSteering.h): 16 directions of interest (the straight line first, circling) and danger (other NPCs in the fight, walls and drops, the target's swing, closer than striking distance); every 3 ticks, staggered. `debug steering` draws the rays (green interest, red danger, yellow the choice, white the spot). tests/steer_test.cpp
- Building parts (2026-11-18): foundations 2, 4, 6, 8 and 10 ft square, all 4 ft deep, in stone or slate; 10 ft walls (2, 4, 8 ft long) with doorways (8 ft tall) and windows, in plank, stone, brick and plaster; gables, posts, beams, floors, 45 degree roofs (thatch, tile, slate), stairs, a door that fills the doorway. Each piece has a part, group and material (structures.toml `materials = {...}` makes one piece per material), so a piece swaps to another material in one call (StructureRegistry::swap)
- Stations, E opens a placeholder menu (ui/StationScreen.h): workbench, campfire (glowing embers), hearth (stone bricks round a fire), forge, anvil; furniture: table, bench, bed, lantern post. New blocks: slate, stone bricks, roof tiles, plaster, workbench top
- Hammer snapping (StructureManager::snapTo): walls go on the nearest edge of a foundation or floor and turn themselves, posts go in corners, doors into doorways, ceilings on wall tops, roofs on a ceiling's edge rising inward (or on a wall top), furniture centred where aimed, anything else flush beside the piece aimed at. Walls share corner voxels; taking one down hands the corner back
- Costs off for now: `costs = false` at the top of structures.toml (the costs stay written per piece; flip it to charge them)
- Blueprints (game/Blueprint.h, data/blueprints/*.toml): whole buildings as lists of pieces. The hammer's build menu has a Blueprints tab: "Copy a building" (click any piece, the whole connected building is saved, chests with their items), then place it anywhere: T turns it, the ground under it is levelled, and a style restyles it as it goes down (walls, roofs, foundations, floors, gables, posts by material; chests keep their items or roll a loot table; foundations sunk 0 to 3 ft). Each blueprint adds up its pieces' costs, ready for players placing huts or tents for a price
- Six village buildings, each built piece by piece with the hammer's snapping in tests/blueprint_test.cpp (run it with --write to remake them): small cottage, longhouse, smithy, storehouse, workshop hut, village square
- Villages from blueprints (game/Village.h): a square in the middle, 1 to 4 roads out of it, buildings along both sides facing the road, picked by tag weight, one style for the whole village, roads levelled along the ground. Console: `village [seed] [roads] [length]`
- Terrain operations (world/TerrainOps.h): flatten a box (with a feathered edge back to the old ground), a flatten disc (brush), level one column; shared by blueprints, villages and the console `flatten [radius] [level]`
- AGENTS.md: a map of the code for coding agents
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
- Animation clips (step 8): ozz-animation 0.16 + Quaternius Universal Animation Library 1 and 2 (CC0, 85 clips in data/animations), retargeted onto our 15 bones at load (retarget.toml). anim_map.toml picks the clips the game uses: idle, walk/run/sprint blended by speed (clip speeds measured from the packs' root motion, so feet don't slide), crouch, jump/fall, roll, guard, combat stance and sword attacks. Backstep, air dodge, flying and the axe swing stay code-driven. `use_clips = false` turns them all off
- Dev worlds: worlds kept in devworlds/ (README there) that never save by themselves: stages for enemy and item showcases. Esc → Save (save and stay, in every world), Save and Quit, and in dev worlds Quit without saving; closing the window doesn't save one either. New World: Save in Worlds / Dev worlds, and Start: New terrain / Copy a dev world (everything in it comes along; the preview shows its type, size, structures and NPCs). Load World lists Worlds and Dev worlds side by side. A "dev world" tag by the mode name. Settings → Auto save (on / off) for ordinary worlds
- Chests: a Wooden Chest (3 x 2 x 2 ft, wood with iron bands and a lock) is a structure in structures.toml (kind = "chest", slots = 50), placed with the Builder's Hammer, the dev menu or `place wooden_chest`. E opens it beside your inventory (left window): drag, shift click to send a stack across, right drag for half, 1-0 over a chest slot to move it to the hotbar; E or I closes. Broken with an axe like a tree (30 health, 1 per punch, full in Test Play); it drops 8 planks and everything in it. Contents save in chests.dat. New blocks chest_wood / chest_iron / chest_lock, drawn in all three packs
- Containers and loot tables: any entity can carry an inventory like a chest (game/Container.h: a capacity and an optional loot table). Until something reads it, it's only the table and a seed: nothing is rolled or saved. The first open / search / break rolls the table (data/registry/loot.toml: rolls, weighted pick, always + chance, nested tables) into ordinary items. NPC types take `pack` and `loot` (bandits, guards, villagers, the dead have tables); E on a body searches it ("Bandit  E: search"); a body that may hold something stays 5 minutes. Structures take `slots` and `loot` on any kind; a Bandit Stash chest rolls bandit_stash. A chest built with the hammer starts empty (building is no way to make loot); one placed by `place` or a world rolls. Console: `loot` lists tables, `loot roll <table> [n]` tries one, `loot <table|none>` re-arms the chest you aim at. Pause menu buttons now really widen for long labels
- Dev world chests: New World → Kit: Showcase chests (new dev worlds; on by default) lays out a labelled row (Weapons, Armour, Tools, Blocks, Decor) 8 ft ahead when you arrive. Showcase chests give a copy of what you take and never run out (Delete removes from the shelf; broken, they spill nothing). Chests can have a label (aim text and panel title). Take all / Store all buttons on every chest panel. Console `chest`: info, fill <kinds or items>, clear, name, showcase, loot, kit. devworlds/README.md covers it
- Item icons from models: tools, weapons, dev tools and worn gear show their real model in every menu (hotbar, inventory, equipment, chests, dev menu), drawn once at load on the CPU (render/IconRaster.h: lit, smoothed, outlined) into one sheet (ui/ItemIcon.h buildItemIcons; F6 redraws). Held items lie on the diagonal, gear is shown as worn. New held models for every tool and weapon that had none, made by tools/gen_held_models.py: three pickaxes, two shovels, hoe, builder's hammer, club, spear, a new quarterstaff item (no moveset yet), and a gem-tipped wand per dev tool. Held in the hand too
- Worn gear on the body: items.toml `gear = { shape, color, trim }` (character/GearLook.h); CharacterModel builds each piece as lofts just outside the body part it covers (from the body's measurements, `CharacterModel::Fit`), so it fits every build, height and slider and moves with the ragdoll. Shapes: cap and helmet (hide hair and hats), jerkin, mail, cuirass (pauldrons), gauntlets, greaves (knee cops), boots, pack (bedroll, straps). NPC types can wear gear (npcs.toml `wear`): guards in helmet, mail, greaves and boots, bandits in jerkins, swordsmen in plate
- New weapons with movesets (movesets.toml): club (slow blunt blows, heavy poise damage, an overhead knockdown), spear (long narrow thrusts and a lunging heavy; light reach 25 voxels against the sword's 19) and quarterstaff (fast three-hit chain, 22 reach, a low wide knockdown sweep). Attacks can set grip = "thrust": the held item turns about the fist to point along the thrust (its model's `thrust` direction), in drawing and in hits alike (HeldModelDef::turn, Pose::heldTurn). Spear thrusts play Punch_Cross
- Parry: a guard raised within 0.18 s of a hit parries it: no damage, no stamina, the attacker's swing is knocked aside and it is exposed for 1.1 s (can't attack or guard, orange outline). A hit on an exposed body is a riposte: 2.5× damage and a knockdown. A guard held up longer blocks as before. The plunge's slam can't be parried. Outline effects parry / exposed / riposte
- Lock-on (TAB, game/LockOn.h): holds the camera on the body nearest the middle of the view (hostile ones first, within 70 voxels); you face it, A / D circle it; a quick sideways mouse flick switches to the next body that way; lets go on death, past 100 voxels, or out of sight for 2 s. A gold diamond marks the target
- Foundations remade: the Stone Foundation is 8 x 8 ft and 4 ft tall (24 x 12 x 24 voxels, 96 stone). The first goes anywhere; one within 4 ft of another must line up with it (side by side, edges flush, corner to corner, or stacked), and the hammer snaps to the nearest one's grid within 8 ft: aim at a foundation's side for the next slot that way, at its top to stack, at the ground nearby for the nearest slot (StructureManager::foundationFit / alignFoundation). Snapping pieces (plank floors) line up with the foundation they're aimed at. The old 4 ft foundation still loads (out of the menu); a saved hammer selection of it switches to the new one. tests/build_test.cpp
- Server split, phase 1: a core library (voxelrpg_core: the world and simulation, no graphics) shared by the game, a new headless VoxelRPG-Server and the tests. server/GameSession.h is one open world (generator, voxels, physics, editor, structures, containers, woodcutting, the Simulation); the game's single player runs on it too. Staged saves (server/SaveStaging.h): a save goes into <world>/.staging and only a commit replaces the world's files (the replaced ones kept in .backup-1..3); a crash mid-commit is finished on the next open, an uncommitted set (a crash, a hostile session) is set aside as .staging-abandoned-<time>. Single player commits every save; the server checkpoints every 5 minutes and commits on stop or 'save'. server/ChunkKeeper.h keeps voxel columns loaded around every player with no meshing. VoxelRPG-Server reads server.toml (made with defaults: world, dev_world, port, max_players, checkpoint_seconds, default_trust), takes status / save / stop / quit-nosave, saves on Ctrl+C. NPC ids are saved and kept across reloads. tests/server_test.cpp
- Server split, phase 2 (networking core): net/Wire.h (bytes), net/Protocol.h (Hello, Input with the last 4 ticks for loss, Welcome with the world's settings and your player file, Modified columns, ChunkData, Edits, Structures, Spawn / Despawn, 20 Hz Snapshot, Event, Notice, Refused; VERSION checked at Hello), net/Transport.h (reliable and unreliable channels; an in-process loopback with adjustable lag, jitter and loss). server/ServerHost.h serves a GameSession: joins (refuses a wrong version, a name in use, a full server), tick-stamped input queues, events, voxel edits (WorldEditor::onVoxel), modified columns sent whole as players come near, structures resent on change, snapshots of bodies within 220 voxels, players saved by name in <world>/players/. client/ClientWorld.h is a player's copy: builds the same generator from the settings, generates every column the seed makes and waits for the modified ones, applies edits, mirrors structures, interpolates other bodies 100 ms behind (smooth through 25% loss), predicts your own movement and corrects it from the server's word. Lag compensation: BodyPoses keeps 0.5 s of skeletons; a remote player's swing is checked against bodies where they were on its screen (its view tick, sent with each input). tests/net_test.cpp (a server and two clients, lag and loss), hit_test (lag compensation)
- Server split, phase 3 (playing together, first part): ENet (UDP) under the same transport (net/EnetTransport.h / .cpp; fetched by CMake). VoxelRPG-Server listens on its port and serves players; server.toml gets starter_kit (what a new player starts with). The game has Join Server on the main menu (address or address:port, and a name: your character on that server; remembered): it builds the same world from the server's settings and plays on a mirror of it. On a server you walk, see the other players (name, look, worn gear, what they hold, their attacks) and NPCs, fight (hits are the server's; lag compensated), and arrange your inventory and equipment (the server accepts an arrangement only if it holds exactly the items it gave you; pickups come back). The pause menu becomes Resume / Controls / Settings / Disconnect; the world keeps running behind menus; losing the server drops you to the Join page with the reason. Not on a server yet: chests, console commands (only debug, where, ping, help), the character creator. `ping` in the console. tests/net_bot (a screenless player that walks and swings: `net_bot 127.0.0.1 bot1 600`) for trying a server alone
- Spawners: a structure kind ("spawner" in structures.toml), so a spawner is a voxel model placed like any structure (dev menu → Structures → Builder's Hammer, or `place <name>`), saved with the world's structures and removed with the hammer. Its entry's spawn = { npcs = { type = weight }, max_alive, total, radius, activate, respawn, min_player_distance, home }. It sleeps until a player comes within 'activate', fills up at once, then replaces the fallen one at a time; its NPCs live around it and remember it (npcs.dat), so a reload doesn't double up. Three to start: Bandit Camp (fire ring and banner, 3 bandits), Training Post (dummies, 4 s respawn: for testing), Grave (undead). Console `spawners`; the ai overlay shows each one's radius, wake range and count
- Structures can be drawn in structures.toml (legend + layers of rows, with repeat), for any kind but trees and doors; placing a structure clears grass and flowers out of the way instead of being blocked by them
- Flat world type (worldgen/FlatGenerator.h): a level field at the Classic size (32 × 32 chunks) with no hills, water, villages or landmarks. New World options: ground block (grass, dirt, sand, red sand, gravel, stone, cobblestone, snow, moss, planks), trees, plants (grass and flowers), decor (bushes, logs, stones), each on or off, and which biome's trees, plants and decor grow. Saved as flat.* lines in world.vxrp. The base for a test world
- New World screen reworked: label beside each control, one section with only the chosen type's options, a line saying what the type is, and a preview for flat worlds (the ground block and what grows)
- Outline effects (render/OutlineFx.h, data/registry/outline.toml): a character's outline can glow or flash. Its colour goes into the edge lines, a soft halo around the figure (shown even with edge lines off), and both tones of the echo in "Lines + echo" mode. Damage taken flashes red, a blocked hit white, a guard break a longer white; an attack's wind-up glows yellow, building until its hit window opens (attacks with at least 0.3 s of wind-up: the heavies, not the quick lights). Looks are data (colour, time, strength, blinks, priority); console `outline <effect> [me|npc]`
- Weapon hits from poses: the simulation poses every nearby body each tick with the same animator and input as what is drawn (sim/BodyPoses.h, character/HumanAnim.h). A weapon hits with its voxel map: the voxels merge into a few boxes when the model loads (9 for the sword, 15 for the axe), grown by hit_margin (default 0.75 in, per .vxm), and during the attack's hit window they are swept from last tick's wrist to this tick's in quarter-voxel steps against every other body's limb boxes (one per bone, from the mesh). First contact lands, at that point, pushed the way the blade moves; walls between the chest and the contact block it. Knocked-down bodies are hit where their ragdoll lies. The axe's chops on bodies use the limb boxes too. Old reach / arc / band rule stays only for the plunge's slam, unarmed and far bodies. About 0.3 ms a tick with 10 bandits and a player
- Attack clips timed by strike marks: anim_map.toml marks where each clip's blade swings (strike = [from, to], fractions of the clip; measured from blade speed), and the clip plays so that part fills the moveset's hit window, the wind-up and recovery around it. Joint blending goes through quaternions (no limb swinging the long way round at a gimbal point)
- Debug overlay, combat layer: every posed body's limb boxes and the weapon's boxes (red while live)
- Inventory rework: 50 slots (game/Inventory.h). Hotbar of 10: slots 1 and 2 are the off-hand (keys 1 / 2 put that item in the left hand or take it out, drawn mirrored on the left wrist), slots 3–10 the right hand (keys 3–0 or the wheel); 40 pack slots. Equipment points at inventory slots: an equipped item keeps its slot and is marked E, so an axe can be in the hotbar and Weapon 1 at once. Equipment: Weapon 1 / Weapon 2 (X swaps; R draws the active one), head, torso, hands, legs, feet, back, two rings and an amulet (no ring or amulet items yet). Tools with a moveset can be equipped as weapons (the iron axe: new `axe` moveset). Old saves load: the 8-slot toolbar moves to slots 3–10, the old weapon and worn gear are equipped from pack slots
- Inventory screen (I, ui/InventoryScreen.h): placeholder window on the left for another menu, the 50 slots in the middle, equipment on the right. Drag to move or swap, right-drag for half a stack, drag onto an equipment slot to equip (or away to unequip), shift-click to equip / unequip, 1–0 over a slot to swap it into the hotbar, carried weight
- Block icons: every menu (hotbar, inventory, dev menu, build menu) draws blocks as little cubes in their own textures from the world atlas, and sprite blocks (flowers, torches, fire) as their flat picture (ui/ItemIcon.h); other items keep the coloured square with a letter until they get icons
- Animation viewer (Character → Animations): every game state as the game shows it (walk, sprint, roll, each sword attack, the axe swing …) and every clip by category, on the character being edited, with play / pause, restart, speed (0.1×–2×), loop and a time bar to scrub
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

## Server split (planned, agreed 2026-11-16; after icons / gear and combat)
Target: at least 20 players per server for now.

- **Programs.** `voxelrpg_core` (world, simulation, NPCs, registries, physics; no GL), `VoxelRPG-Server` (headless: `VoxelRPG-Server <world> --port N`, server.toml), and the game, which runs the same server in-process on loopback when it loads a world (single player is a local server; opening to LAN opens the port). Transport: ENet.
- **The server owns** block edits and structures, inventories / containers / loot rolls, applied damage and health, NPC brains (unless handed to a client), spawners, reputation, saves.
- **Clients do the heavy lifting** wherever the result can be checked: rendering, meshing, render lighting, far terrain, animation, ragdoll visuals (client only); chunk generation (same from the seed: the server sends only edits); the terrain bake, low-res summaries and light levels (made by clients, ideally on their GPUs, and sent up); their own movement and pose (predicted); their own weapon hits (reported, checked for plausibility: distance, hit window, stamina, line of sight); NPCs near only one player (handed to that client, taken back when someone else comes near).
- **Trust tags per connection.**
  - full (host, LAN friends): their movement and hits stand; can run handed-over NPCs; uploads accepted.
  - standard: predicted movement, hits checked. Uploaded files (bakes) all get format checks (header, size, value ranges, hash); on top of that the server re-does a RANDOM batch every few batches and compares. The client can't tell which batches are audited, so slipping bad data in on purpose is a gamble; the server does a fraction of the work. A failed audit demotes the connection to thin and drops (or re-checks) everything it sent since its last good audit, so each file keeps where it came from. The audit rate can start high for a new connection and fall as it builds a record; occasionally giving the same batch to two clients is a free check.
  - thin / insecure: the server does the maths for them (movement, hits) and can send chunk data instead of having them generate it. Set by config, chosen by a weak client, or forced on a connection that fails checks.
- **GPU bakes.** The erosion and light bakes are CPU code today: they need compute-shader ports, with results rounded (quantized) so different GPUs agree. A server with no GPU keeps the CPU path as its fallback (slow, cached).
- **Saving safely.** A running server never overwrites the world's files: changes go to modified copies (a staging set beside the committed files). They replace the real files only on an intentional shutdown with save, or a forced save, and then atomically (write all, mark the set complete, swap; a crash mid-swap finishes or rolls back on the next start). A hostile session or a crash leaves the last committed world untouched. Keep a few committed generations as backups. Autosave writes checkpoints into the staging set, not the committed files.
- **Phases.** (1, done) core and server targets, the server loading a world headless; (2, done) the networking core; (3, first part done: ENet, the server serving players, Join Server; still to do: Open to LAN from a single-player world, chests (building, tools and the axe are requests now), parries judged at the defender's time, the ragdoll's target set by the simulation, what the axe swing looks like to others) the game on its in-process server (intents in, snapshots and events out); (3) the axe swing and block editing as requests; (4) ENet: joining, chunk edits, entity snapshots, staged saves; (5) trust tags, NPC hand-over, uploads with random audits, GPU bake ports.
- **Under lag** (combat review, 2026-11-16): keep about half a second of pose history (BodyPoses as a ring buffer) and check a hit against where the target was when the attacker saw it (lag compensation; it also makes checking a client's reported hit cheap). Inputs carry the client's tick, and a parry is judged at the time the defender pressed guard. The server owns ragdolls (the ragdoll's target pose moves out of the renderer). Hit-stop becomes a client prediction. The in-process server gets an artificial lag setting (0-300 ms) to tune parries and hits on one machine. These go in phase 2.
- **Downloads** (Eddie): one build makes VoxelRPG (with its single-player server) and VoxelRPG-Server; a packaging step makes zips friends can download and run. Phase 5.

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

- Chests and containers next: other sizes (small crate, iron chest), a lid that opens, locks and keys, stashes placed in camps and villages, a pack on the player's back (travel_pack: more slots), NPCs using what's in their pack (equip the armour they carry; drop the weapon in hand into the pack when they die), item stands and dropped-item containers for dev world showcases, a `model = "wooden_chest"` key so variants don't copy the layers, and keeping bodies (with what's on them) across saves
- Inventory next: dropping items into the world, ring and amulet items, what off-hand items do (shield blocks, torch light), sorting, a real menu in the left window (crafting, a chest, trade); the world doesn't run while the inventory is open (like the build menu), which multiplayer will need to change
- Dev worlds next: item stands that show the item itself, NPC showcases (a spawner or command that stands one of each type in a row, frozen until poked); a starter Arena dev world; Save as dev world from a running world; delete / rename worlds from Load World
- Spawners next: conditions (night only, a reputation, a quest), waves, spawners that can be destroyed (health like trees), a test-arena preset on flat worlds; and other uses for spawner-like structures (Eddie: "we may use them for something else")
- Easier ways to start fights for testing, on a flat world (Eddie: "worth keeping in mind"; spawners are done): placed NPC spawners (a block or structure that spawns a type, with a count, radius and respawn time), a dev test-arena world type with spawners and dummies ready, and/or command blocks that run console commands (spawn, despawn, smite) when used or powered
- Review scale and building in the taller world
- Check lower-end hardware: lower the default chunk / render distance only if it struggles

## Eddie's list (2026-11-18)
Ordered roughly by when; see the notes under each.

**Fix soon (small, felt every session)**
- (done 2026-11-18, see Recently done) Rubber banding when sprint-jumping on a server
- (done 2026-11-18, see Recently done) Eye height
- (done 2026-11-18, see Recently done) Combat popups stay on screen too long
- (done 2026-11-18, see Recently done) Starting kit for testing

**Movement**
- (done 2026-11-18, see Recently done) Slopes should cost time

**Combat**
- (done 2026-11-18, see Recently done) Context steering for NPCs in a fight
- (done 2026-11-18, see Recently done) Windburst spell; spellbooks and the ice spike
- Spells next: projectiles seen by other players on a server (now only the caster's own and the ice they leave), aiming up and down (the camera's pitch in the intent), a cooldown or mana instead of stamina, more spells (a fire bolt that lights things, a ward), spellbooks NPCs can use
- (done 2026-11-18, see Recently done) Another enemy type with a different model: the slime
- Combat layout, next steps: a shield item and model (LT guards with it; a second sword's own off-hand attack), what charms do (and the first charms), pad button pictures instead of names, rebinding the pad, more spells
- (done 2026-11-18, see Recently done) On-screen controller debug (debug pad)
- Pickaxe and block breaking feel slow to respond: revisit with destructive weapons (pickaxe is being phased out)

**Talk**
- Dialogue boxes with mumble speech and mouth sync, also for typed chat; dialogue imported from text files. Synth stays useful even if voices come later

**Rendering**
- Grass and foliage: billboards with bent normals (lit like a rounded clump), sway from a scrolling noise map; leaves the same way instead of cubes; water with the same noise for its surface

**Outside the game**
- GitHub repo: a README (what the game is, screenshots, how to build and run, controls, the layout, state of the project) and the About box (description, topics). No README exists yet
- Hosting: immortal2022.com for now, for servers first (a DNS name for the server address), a dev site later; the repo covers updates until then
- Browser client (low priority): a WebGL/WebGPU build of the least-permission client, lower rendering cost, same protocol and version, joining the same servers

## Characters (planned)
- Edge lines will most likely settle on solid black; the Off / echo / tinted options stay for testing until then
- Polish the eye styles further (the art is in `data/resourcepacks/<pack>/characters/face_*.png`)
- Hitboxes that follow the body type (collision stays the same 17-voxel capsule for everyone)
- First-person arms holding the current tool
- Hair styles and extras as voxel files, so they can be designed in game or generated
- `.vox` (MagicaVoxel) import for hair, outfits and story characters' heads
- Keyframed animations to replace the code-driven ones where needed
- Ragdoll: a real get-up animation (now the body blends from lying to standing over 0.8 s); hard landings and falls as hits; self-collision between limbs; a ragdoll that tumbles on a plunge slam
- Worn gear next: more shapes (hoods, robes, shields on the back), gear on the character creator preview, worn gear on limb hit boxes and as armour against damage, NPCs putting on gear they carry; encumbrance (carried weight slowing movement) if wanted
- Combat next: NPCs that parry (and use the new weapons: a spearman type), backstabs, two-handed grips for the spear and staff (the off hand on the shaft, carried pointing ahead in the stance), damage types against resistances, hit and block sounds and sparks, archers (a 'ring' slot at range)
- Animation clips, next: clips for backstep and air dodge; check hand and weapon angles across the sword clips; a shield model for the shield clips; a longer blend into attacks (the 0.06 s blend whips the blade across before the wind-up)
- Outline effects next: a real hold-to-charge heavy (the 'charge' glow now shows the wind-up), more triggers (poise break, i-frames, low health, a status like poison, the lock-on target, an NPC about to notice you), per-faction or per-NPC glow colours, a pulse in the echo's offset while glowing
- Hits next: blade against blade (parries, a guard that blocks with the weapon instead of a facing cone), damage by limb (head, arms, legs), NPCs choosing to swing from their real blade reach (moveset reach 10 vs ~16 measured for light 1), the axe swing moved into the simulation, the ragdoll's target set by the simulation (now the renderer sets it)
- Multiplayer next: chests on a server (open, take, put: the container as server state), falling logs and dropped items seen by clients, the dig timer checked by the server (now the client says when a block is dug), dev tools on a server for trusted players; entity ids stable across saves and machines (now a counter)
- NPCs next: real menus (dialogue, trade) behind the 'unavailable' state, loot tables for the review, NPC texture sets (presets' texture_set, e.g. undead) drawn by the renderer, a second skeleton for animals, patrol routes placed with a dev tool, opportunistic NPCs picking on fleeing ones, morale
- Woodcutting: chop sounds and a little tree shake on hits; stamina; stumps that can be dug out; saplings or sticks from the leaves; other tools digging a log voxel by voxel skip its wood count; falling trees only move while a body is in the world (not in Spectator); logs float in water later; axe tiers (stone, bronze) with their own power
- Offer the character creator when a new world starts

## Resources and building direction (Eddie, 2026-11-16)
- Leaning away from players mining and placing single voxels. Building goes through the Builder's Hammer and structures (foundations first)
- Pickaxes extract ore from tagged resource veins: a vein is a structure spawned in the world like a tree (structures.toml kind "vein", a voxel model with a resource, health and yield), worn down by pickaxe hits and dropping ore, as trees drop wood. Pickaxe cube-breaking becomes less important (may go entirely, or stay a dev / Test Play tool)
- Open: whether Survival keeps any voxel breaking or placing (dirt and stone for terraforming, or not at all), and where stone for foundations comes from (stone veins / quarry outcrops)
- Building next (2026-11-18 list): a flatten brush as a dev tool item (the hoe is done); real station menus once crafting exists; hip roofs and roof corners; a 10 ft roof span; blueprints placeable by players for a cost (huts, tents); chest loot-table swap per blueprint in the style menu is done, per-chest choice later; a blueprint editor (rename, tags, front) in game
- Villages next: smart road tools (curves, junctions, a road the player draws, paths to doors), villages placed by the island generator at its settlement sites, lots that follow slopes (terraces, stilts) instead of skipping them, wells, fences, fields, NPC villagers living in the houses
- A simpler start world: partly done (New World → Flat → Village builds one ahead of you); still to do: a gentle-hills world type, and making it the default for testing

## Tools (planned)
- (Pickaxe cube sizes and dig times: on hold, see Resources and building direction above)
- Shovels and axes with their own cube or shape behaviour

## Known gaps
- Caves are only pits at cave cells
- Water and lava are static (no flow)
- The island generator's settlements are still flattened plazas: the village generator (game/Village.h) isn't hooked into world generation yet; use the `village` command
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

## Packaging and downloads (added with server phase 3)

- `build.bat` builds game + server (Release); `build.bat tests` builds the tests too. `package.bat` builds, then runs
  `cmake -P tools/package.cmake`, which writes `dist/VoxelRPG-Client.zip` (game, data/, shaders/,
  README) and `dist/VoxelRPG-Server.zip` (server, data/ without controls and resource packs,
  Start-Server.bat, README). Both carry `version.txt` (build time + `net::VERSION`).
  The texts and start scripts packed in are in `tools/dist/`. `dist/` and `server.toml` are ignored by git.
- The zips go on GitHub as a Release (steps in `BUILDING.txt`, which is Eddie's command reference).
- The server makes its world when it's missing: `server.toml` `new_world = flat | classic | island | none`
  and `new_world_seed`. So the server zip runs on a machine with no game.
- Not done: a GitHub Actions workflow that builds on Windows and attaches the zips to a release
  (offered; nothing to test it with here), a real installer, code signing (friends see SmartScreen
  once: More info -> Run anyway).
