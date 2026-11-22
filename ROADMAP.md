# VoxelRPG Roadmap

What's ahead. Finished work goes in CHANGELOG.md (newest first), not
here. Sections marked **Tabled** are agreed ideas parked for later; add
to them as new ideas come up. More than one agent may be working at
once: before starting an item, check HANDOFF.md "In progress" and
mark yours there.

## Open issues (tabled by Eddie, 2026-11-21: look at these first)
- **Can't close the portal on a server.** The gate menu opens there
  (also in combat now), but "Close" doesn't close the instance.
  Start at main.cpp's gate menu pick (`remote->gate(a)`, `Msg::Gate`)
  → `ServerHost::onGate` → `RealmServer::gateAction` "close"; with
  someone inside it only starts the closing countdown (by design),
  so check whether that's what Eddie saw, and that the countdown and
  the closed state reach the client (`Msg::RealmState`).
- **Artifacts spinning the camera near a gate on a server** (Eddie,
  2026-11-21; walking through is fine now). Not looked at. Suspects:
  hidden chunk culling (its answers are a frame late, and a fast turn
  shows sections that were behind you: try Settings > Hidden chunk
  culling Off first), the portal view's opening box / tunnel at the
  camera (`portalview::begin`, the `tunnel` / `justOut` cases), and on
  a server the watched world streaming round the far end.
- **The canyon village** (island generator, older): check its look in
  game (seed 42, world 5056, 9280); its plaza was a grey disc with a
  river through it. See docs/handoff-history.md "In progress when this
  session stopped".
- **F2 (Screenshot) does nothing:** the key is bound (keyboard.toml, `InputAction::Screenshot`) but no code handles
  it. The friends' readme says to use Windows' own screenshot keys for now; wire it up (save a PNG next to the
  game) or drop the binding.
- Interacting with a gate in combat on a server: Eddie saw it fail
  once, then work. Couldn't reproduce here (single player, pad and
  keys, main gate and crypt gate). Watch for it.
- Untested in game: portal views lit by the far world's time of day;
  light from the land on a real GPU (shadow edges, the bias in
  shaders/terrainlight.glsl); render scale Auto's steering; hidden
  chunk culling's one-frame pop-in coming round a hill.

## Eddie's list (2026-11-21): the look of the land, villages, the sky expanse
Talked through at the end of the 2026-11-21 session; not started.
Every visual piece gets a setting back to the plain look. Build order
inside each part as listed.

**Shared world maps and wind** (builds on `render/TerrainLight.h`)
1. One "world maps" shader include (grown from `shaders/terrainlight.glsl`):
   height, sun shadow, wind (later wetness, snow), on fixed texture
   units, bound once a frame. The CPU side split from the GL side so
   the headless server can build the same maps.
2. Terrain wind field: one sweep over the height map toward the wind
   (like the shadow sweep): lee behind ridges, speed-up on crests,
   turned along valleys; gusts as noise moving through it with time.
   Direction and gusts from the seed and the world clock, so every
   client and the server agree. Remade on a thread when the
   prevailing wind turns. (Replaces the roadmap's "lee behind hills".)
3. Everything reads it: plants and grass, leaves, water ripples,
   clouds and cloud shadows (and clouds drawn in the sky), particles;
   physics items (banners, cloth, arrows, loose leaves) from the CPU
   copy, server-side where it affects play.
4. Far-land shaders: grass waves moving with the gusts, darker lee
   side on the canopy bump, morning mist in low ground.

**Smooth terrain and foliage** (one code path: a density field + Surface Nets)
1. Rounded voxel edges in the shader (normals bent near face edges,
   a little contact darkening): cheap, try first.
2. Density field + Surface Nets meshing, shared by the next two.
3. Terrain skin: blocks tagged `smooth` in blocks.toml (grass, dirt,
   sand, stone, snow) meshed as a smooth surface; buildings and
   placed pieces stay crisp voxels (the skin tucks under them).
   Material per vertex, triplanar blending between blocks. The
   character controller walks on the skin's height where the ground
   is smooth; edits stay voxel and remesh the skin. Makes the seam
   to the far terrain nearly vanish.
4. Leaves: the crown's leaf density blurred into a hull (Surface
   Nets), drawn as a dark inner shell; leaf sprites scattered over it
   (hash-placed, tilted along the hull normal, the leaf block's own
   colour), lit as soft clumps, backlit at the edges, swaying with
   the wind map (outer more). Collision, chopping and light keep the
   real leaf voxels. Felled trees (woodcutting props) the same.
   Estimated: about the same triangles, 1.5-3x the pixel cost up
   close (overdraw: alpha-to-coverage, fewer bigger sprites far).
   Measure with F3 against plain cubes on a test tree.
5. Better crowns in the recipes (Decorations.h `clump`): more,
   smaller clumps with gaps, lacy edges. Later: recipes record their
   clumps for exact normals and per-clump colour.
6. Parallax occlusion (with silhouettes) on chosen building materials
   (`parallax = true`, height maps in the resource pack), fading by
   distance.

**Impostors baked from the registry**
- On loading decorations (and F6), render every tree, rock and
  landmark variant offscreen from ~8 sides and above (colour, normal,
  depth), like `buildItemIcons`; cached on disk by the data's hash.
  Lit live, swaying with the wind map; a dither fade at the swap.
- Mid range (~150 vox out) trees become one quad each.
- The far terrain scatters them where the generator puts its trees
  (`treeInCell`): real maples and birches in the distance. Visited
  chunks remember which trees still stand (lowres.dat). Later
  buildings from blueprints, so villages show from afar.

**Villages: more to them** (Eddie: "roles and jobs need work")
- Roles with real jobs (smith at the anvil, farmer in the fields,
  storekeeper trading, guards on rounds), dialogue that does things
  (quests, trade), villages placed by the island generator at its
  sites, lots on slopes, fences, fields, markets, more buildings. See
  "Villagers next" / "Villages next" below.

**A new world type: the sky expanse**
- A floating expanse of fantasy mushroom-covered islands in the sky,
  reached through a portal. Alien, fungal biomes; their resources
  feed magic (new blocks, items, spells).
- A network of portals between the islands, unlocked as the player
  finds them; gate sites placed by the generation rules (like the
  chart's sites; `PortalDirector` gates, `Gate.h`).
- Falling far enough: a seamless teleport back to the top (a
  horizontal portal plane under the world linked to one above it:
  needs "portals on floors and ceilings").
- Notes for building it: the generator needs 3D shapes (an island
  has an underside; `TerrainColumn` is one ground per column), so it
  fills sections from a density (shared with the Surface Nets work);
  the far view needs per-island meshes or impostors instead of the
  height-map `FarTerrain`; the shadow map's top heights already shade
  islands under islands; glowing mushrooms use emissive blocks and
  the distant lights; a sea of cloud below. A persistent world (like
  the main one), not a timed instance: `Worlds` keeps several open.
- The height-map far terrain stays the far view for worlds that
  aren't floating, and a lower-detail option anywhere (Eddie): on the
  sky expanse it draws islands by their tops only (no undersides),
  cheap, for weak machines or the far ring past the island impostors.
- Its own sky (Eddie: "we may have a very different sky for our
  mushroom world"): skies as data, `data/registry/skies.toml`, as
  proposed in docs/dungeons.md "Skybox blocks" (colours by time of
  day, suns and moons or none, stars, clouds, fixed time or the day
  cycle, the light it gives); each world names its sky. Everything
  that shows another world draws that world's sky, live (its clock,
  its clouds): portal views (they already take the far world's time
  of day, 2026-11-21) and skybox blocks, so a room in the main world
  or a crypt can open onto the sky expanse's sky and be lit by it.
  Later: a skybox block showing that world's distant islands too
  (their impostors against its sky), a window onto another world.


## Spells: a big selection, built from parts (Eddie, 2026-11-21)
Now: two spells (windburst, ice spike), each a hand-written effect
in `Simulation` picked by name (Simulation.h, `d.effect == ...`);
a spellbook item names a 'cast' attack in movesets.toml. That
doesn't scale to many spells. Plan:
- **Spells as data,** `data/registry/spells.toml`. A spell =
  - delivery: self, touch, cone, projectile (speed, gravity, homing,
    bounces, pierce), beam, area at a point, wall or line, rune or
    trap left on the ground, summon;
  - shape: range, radius, width, duration, count (a fan of three);
  - effects, a list: damage (with a type: fire, frost, shock,
    arcane, against resistances), push and pull, status (burn,
    slow, freeze, poison, stagger, silence), heal, ward or shield,
    light, terrain (freeze water, place ice or stone, burn grass,
    grow plants, break weak blocks), blink or a short portal;
  - triggers: on cast, on hit, on expire, every N seconds; a trigger
    can start another spell, so spells chain (a fireball that
    bursts into sparks; an ice spike that leaves a frozen patch);
  - look and sound: particles (particles.toml), trail, light colour,
    sounds, the cast animation (movesets.toml), the book's model;
  - cost: mana or stamina, cooldown, cast time, channelled or not.
- **The effects are C++ primitives written once** (a damage, a push,
  a status, a block change); spells only combine them. New spells
  need no code. The two spells today become the first entries.
- Spellbooks (items.toml) name a spell id; NPCs use the same spells
  (spellbooks NPCs can use is already on the list); icons drawn
  from the spell's colour and element.
- Simulated in `Simulation` (deterministic), so servers run them and
  other players see the projectiles (already on the list).
- Later: players compose spells from parts they've found (the sky
  expanse's fungal resources as reagents), as a crafting system on
  the same data. A test command to cast any spell by id, and a dev
  showcase of every spell.
- Later (Eddie): spells unlocked through a skill tree instead of
  found as spellbooks; memorized at a table (a station) to equip
  them in the combat slots. Not until combat and skills exist to
  balance the tree against. Keep the slot / equip code independent
  of the book item so the switch is small: a slot holds a spell id,
  whether it came from a book or from memory.

## Vehicles (Eddie, 2026-11-21; later down the line)
- Horse-drawn carriages and cars, with physics. Tyre grip by the
  ground's material (grass, gravel road, sand, mud, ice, stone):
  a friction / rolling-resistance table per material (beside
  data/audio/materials.toml, or in blocks.toml).
- Modular, interchangeable parts: chassis, wheels and axles,
  engine or harness, seats, body panels, storage; each a part with
  stats (mass, grip, power, durability), snapped together on a grid
  like the hammer's pieces (game/PieceSpec.h, Blueprint.h). Saved
  as vehicle blueprints.
- Its own builder wand (a vehicle bench or tool, like the Builder's
  Hammer), with the parts' stats shown as you build.
- Physics: a rigid body (the felled tree's shape-matched body,
  physics/RigidVoxelBody.h, is a starting point) with per-wheel ray
  or sphere contacts: suspension, steering, grip from the ground
  under each wheel; collisions with the voxel world; damage to
  parts. Horses as animals pulling through a harness joint (needs
  the second, animal skeleton already planned).
- On a server: the server simulates vehicles; the driver predicts
  their own like their movement; passengers ride on it (positions
  relative to the vehicle). After Open to LAN and the combat review.

## Next, in order (Eddie, 2026-11-20)
1. **Combat review.** Eddie: "combat still feels a bit off, probably
   because of ragdoll jank". A hit-reaction animation set in place of
   the ragdoll wobble, then the open items under the 2026-11-20
   character-action entry in CHANGELOG.md (air combos, feints, an
   aggression budget, recorded hit sounds, reactions over the network).
   The boss uses player animations for now (fine).
   Do it in the simulation, not the renderer (the ragdoll's target and
   reactions decided by the sim, "Server split" > "Under lag"), so the
   server work after it doesn't redo it.
2. **Servers.** Slices 1-3 done (CHANGELOG: a realm on the dedicated
   server; the gate from the game; seamless gates). Next:
   - Seamless on a server: done (CHANGELOG 2026-11-21; pop-in fixed).
     Near a gate the client meshes two worlds in full (memory, CPU):
     if that drops frames, mesh only what's seen through the opening.
   - The game hosting a realm (single player is a local server: Open
     to LAN uses RealmServer too). Phase 3's chests, parries at the
     defender's time, the ragdoll's target from the simulation and the
     axe swing are done (CHANGELOG 2026-11-21). See "Server split".
   With it, keeping everyone (server and clients) as light as
   possible (Eddie, 2026-11-20):
   - Batching (multi-draw instead of a call per section; 4 texture
     binds per section now). Occlusion culling, render scale, far
     foliage colours, unseen worlds at half rate: done (CHANGELOG
     2026-11-21, second pass).
   - (Done, CHANGELOG 2026-11-21: GPU times on F3, per-direction
     faces, near to far, one draw per body, far bodies without
     outlines, cached uniform locations.)
   - Cave culling: when a section is meshed, record which of its 6
     faces connect through open space; each frame flood out from the
     camera's section through connected faces (Minecraft's approach,
     no GPU queries). Our sections are 128 cubed, too coarse for it to
     drop much: it wants sub-sections (32 cubed), which go with
     batching (one buffer, many small ranges).
   - Portal views of far-off openings at lower resolution, refreshed
     every few frames: needs each view drawn offscreen and pasted in
     (the views are drawn straight into the frame through the
     stencil now); render scale covers them meanwhile.
   (Chunk vertices are already 8 bytes; meshing is greedy.)
   - Light from the land: done (CHANGELOG 2026-11-21). Next for it:
     clouds drawn in the sky to go with the cloud shadows; edits and
     buildings in the height map (from the chunk summaries); a
     world looked into before you've been there has no map yet (lit
     plain).

## Portals, still open (docs/dungeons.md step 7)
- **Bug, fix untested in game:** at a grazing angle the far side
  showed in a sliver beside a gate's arch (Eddie's screenshot from the
  crypt, third-person camera). The deep stencil box behind the opening
  now only draws while the camera is passing through the opening
  (`portalview::passingThrough`); couldn't reproduce the sliver here,
  so confirm in game.
- Gun portal flicker: the shallow-box fix may have cured it; unconfirmed.
- Arrows and spells through portals; portals on floors and ceilings;
  portals seen through portals (one level); occlusion culling in
  portal views and batching for chunk draws.
- Far worlds you look into still tick in full (unseen ones step at
  half rate): tick them at 10-20 Hz if frames drop near gates.

## Eddie's list (2026-11-20): worlds, portals and what they open up
Wanted down the road (not started unless marked). Portals first: the
rest builds on seamless portals and several worlds at once.
(Done and in CHANGELOG.md: the portal gun, seamless portals between
worlds, two-sided portals, NPCs crossing. What's left of portals
themselves is under "Portals, still open".)
- **Join portals:** a home island with a portal to every world. Join
  portals may be one way, showing the far side from one wall only; a
  call to make a portal two way.
- **Non-Euclidean dungeons and puzzles:** rooms stitched seamlessly
  between connected layers (same-world portals; tiles whose doors lead
  elsewhere).
- **Level editors in game** for designing those non-Euclidean tiles.
- **Builder's tool marks doors and windows as portals:** a built door
  or window joined to another place, seamless.
- **Interior facades:** a building's windows and doors show (and lead
  into) an interior kept elsewhere, for buildings we don't design
  inside.
- **Corruption events:** a new biome ("hell") takes over part of the
  main world for a while, its chunks swapped for temporary ones; a time
  limit or beating its boss restores the world. Built like an instance
  dungeon laid over the world.
- **Terrain that feels less blocky:** shaders and terrain smoothing.
- **Foundations off the grid:** foundation pieces placed at an angle.

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
- **Phases.** (1, done) core and server targets, the server loading a world headless; (2, done) the networking core; (3, done but Open to LAN from a single-player world: ENet, the server serving players, Join Server, chests, parries at the defender's time, the ragdoll's target from the simulation, the axe swing seen by others) the game on its in-process server (intents in, snapshots and events out); (3) the axe swing and block editing as requests; (4) ENet: joining, chunk edits, entity snapshots, staged saves; (5) trust tags, NPC hand-over, uploads with random audits, GPU bake ports.
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

**Movement**

**Combat**
- Spells next: projectiles seen by other players on a server (now only the caster's own and the ice they leave), aiming up and down (the camera's pitch in the intent), a cooldown or mana instead of stamina, more spells (a fire bolt that lights things, a ward), spellbooks NPCs can use
- Combat layout, next steps: a second sword's own off-hand attack (the shield is done), what charms do (and the first charms), more spells
- Pickaxe and block breaking feel slow to respond: revisit with destructive weapons (pickaxe is being phased out)

**Talk**
- Audio is good for now (Eddie, 2026-11-19): a base that makes sound for anything at once; more gameplay first, then judge it. Weapon swings (and likely hits) need a different solution than the synthesized noise sweep: recorded or layered samples per weapon, keyed to the swing's speed and the moveset.
- Talk and audio next (the first pass is done, see Recently done): sound around corners (a flood of the open air from the sound, so a voice in the next room comes from the doorway), the room's echo from what it's made of (materials.toml echo), wind carrying sound downwind, NPC hearing on the same paths (a fight two rooms away draws them; sneaking is quieter), real recordings in place of any synthesized effect, dialogue boxes that speak their text with the voice and mouth, typed chat spoken the same way, music
- Dialogue boxes with mumble speech and mouth sync, also for typed chat; dialogue imported from text files. Synth stays useful even if voices come later

**Rendering** (Eddie, 2026-11-18)
- Every new shader effect gets a toggle back to the plain look: voxels, or for grass simple billboards. For performance, and because building is easier against plain voxels
- Grass and foliage (done for plants: offsets and bent normals). Next: leaves the same way instead of cubes, with the cubes kept as the plain option
- Wind (first pass done, see Recently done: sky light, canopy and height decide where it's felt). Next: leaves swaying with it, a lee behind hills measured from the terrain (upwind height) instead of only the sky light, weather (calm days, storms)
- Water (first pass done: rivers flow along the generator's rivers). Next: slopes in the surface itself (the stepped river surfaces drawn as one sloping sheet), foam at falls and banks, flow for water a player pours (no flow yet)

**Later: identity and history** (Eddie, 2026-11-19; not until there are secure identities)
- Identity tags on what players make: who placed each structure, who crafted each weapon. Weapons carrying a history of their improvements and the people who crafted and improved them (fun to find an old blade with a story). Needs accounts that can't be faked first (the server's names are only typed in today)

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
- Building next (2026-11-18 list): a flatten brush as a dev tool item (the hoe is done); real station menus once crafting exists; hip roofs, ridge caps and roof corners; a bell tower for the church; stairs any length; furniture woods from the palette; inner walls snapping across a floor (not only its edges); blueprints placeable by players for a cost (huts, tents); chest loot-table swap per blueprint in the style menu is done, per-chest choice later; a blueprint editor (rename, tags, front) in game
- Spawning next (Eddie, 2026-11-19, roaming first): fixed places from world generation (bandit camps, graves, slime pools at the island's sites, cleared and coming back after a while); raids on villages now there are guards; difficulty that follows the player (gear, level) as well as distance; new creatures (wolves, slimes that split)
- Villages, known: about one building in twelve doesn't go up ("Stone Foundation: blocked" in the log); a door that opens or shuts relights the chunks round it (a hitch on slow machines when villagers come and go: doors may need to be props, not voxels); building a site's village takes a moment the first time you come near
- Villagers next: real dialogue (choices that do something: quests, trade), a spearman type for the guards; a villager in a fight it can't run from; work that does something (the smith at the anvil with a hammer swing, the storekeeper trading); villagers carrying a lantern at night; the network doesn't send `lying` yet (other players see a sleeper standing in the bed)
- Villages next: smart road tools (curves, junctions, a road the player draws); villages placed by the island generator at its settlement sites; lots that follow slopes (terraces, stilts) instead of skipping them; fences and gardens round homes, fields, a market; more buildings (tavern, farmhouse, mill)
- A simpler start world: partly done (New World → Flat → Village builds one ahead of you); still to do: a gentle-hills world type, and making it the default for testing

## Tools (planned)
- (Pickaxe cube sizes and dig times: on hold, see Resources and building direction above)
- Shovels and axes with their own cube or shape behaviour

## Known gaps
- Caves are only pits at cave cells
- Water and lava don't spread: rivers only look like they run (the chunk shader moves the surface along the generator's flow); water or lava a player places stays where it's put
- Island villages are built at the generator's sites (`GameSession::tickVillages`), but the canyon village's plaza still needs a look (see Open issues)
- Distant terrain shows player edits only for chunks that have been loaded at least once (remembered in lowres.dat)
- No real-time shadows from buildings, trees or bodies: shading is baked sky light, sun facing and the coarse terrain shadows (hills shade, houses don't)
- Light bake is 50–110 ms per column on one worker; fine for streaming, could be trimmed
- Canyon rivers fill the whole floor in the deepest gorges; a dry gravel strip beside the water would read better
- Side canyons (shorter branches joining the main ones) not done yet
- A few short ramps remain where two rivers' banks meet (each bank follows its nearest river)
- A sprite plant standing across a section boundary (every 128 voxels up) draws only its lower part

## Lighting ideas (later)
- Sun light from the terrain map by time of day (Eddie, 2026-11-20;
  client only, each with a Settings toggle back to plain):
  - Smooth normals from the height map: shading by the sun's angle
    across rolling ground instead of per block face (near chunks
    blended with the face normal; far terrain fully). Goes with
    "terrain that feels less blocky".
  - Horizon map: per column (in the lowres summaries), how high the
    land rises toward 8-16 compass directions; the sun is hidden when
    it's below that. Mountains shade valleys at dawn and dusk, any
    time of day, one texture read; re-made only where edits change
    heights.
  - Ray march a low-res height texture toward the sun in the shader:
    the same without the bake, more cost per pixel.
  - Shadow map (near, cascades) for buildings, trees and characters:
    the real thing up close, the costly one; the horizon map covers
    the distance.
  - Cloud shadows drifting with the wind (render/Wind.h): cheap mood.
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
