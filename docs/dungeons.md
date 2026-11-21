# Dimensions, portals and instance dungeons

Design agreed with Eddie, 2026-11-20. Nothing here is built yet; the
build order is at the end. Decisions are marked **decided**; the rest
are proposals to confirm or open questions.

The dungeons are meant to be the main way of getting resources, so
they're the core of the gameplay loop. Drops and loot come later.

## Worlds, dimensions and saves

- **decided:** a save holds a main world plus its dimensions. Several
  saves can be used together: a save can link to a world in another
  save (a library of pregenerated dungeons, say).
- **decided:** dimensions save progress like the main world.
  Instance dungeons don't (see below).
- Proposal: a save becomes a *realm*:
  ```
  saves/<realm>/
    realm.toml          the worlds it holds and links to (by id), which is the main one
    worlds/<id>/        each world as a world save is today (world.vxrp, chunks, …)
    instances/<id>/     running instances (deleted when they close)
  ```
  - Every world gets a permanent id. Portals point at ids, never at
    folders, so a link to another save's world survives moving files.
  - A pregenerated dungeon is a *template*: kept read-only, copied
    into `instances/` when an instance opens.
  - The player's character (inventory, gear, health) belongs to the
    realm, not to one world.
- Existing saves load as a realm with one world.

## Portals

- **decided:** portals are the main way between worlds. A portal hub
  (a dev world with a portal to every dimension and dungeon type) is
  wanted for testing.
- **decided:** walk-through portals that show the other world through
  their frame, with no loading screen.
- **decided:** portal kinds have their own travel rules; in most
  cases followers walk through with you.
- **decided (first version):** one portal at the spawn, which is
  activated (interact) to open. A world generation option turns
  spawning it on or off.
- How it works:
  - **The view:** a second camera at the far portal, moving with
    yours, draws the other world clipped to the portal's frame
    (a stencil), so it's a window, not a picture. Only a small area
    of the far world is loaded and drawn (about 2 chunks round its
    portal).
  - **Crossing:** when a body's centre crosses the portal's plane it
    moves to the far side, its speed and facing turned to match.
    Players, followers, NPCs that chase you (if the kind allows),
    thrown items and projectiles all use the same rule.
  - **Loading first:** the far world starts loading around its portal
    as soon as someone comes near the near one, so it's ready before
    anyone steps through.
- Portal rules, data in `data/registry/portals.toml`, per kind: who
  may pass (players, followers, hostile NPCs, items), one-way or not,
  what it needs to open, whether it makes an instance and its time
  limit, the world (or template) it leads to.
- Built in three steps, each playable:
  1. Step through with a short fade (one world loaded at a time).
  2. The live view through the frame (both worlds loaded).
  3. Seamless crossing for everything, followers included.

## Servers

- **decided:** a server runs several worlds at once. Three players in
  three worlds means three worlds running.
- Each world has its own simulation, chunks, NPCs and saving. A world
  with nobody in it sleeps, then unloads after a while; a portal's
  destination wakes when someone comes near.
- Handing authority to a lone player (asked about): advised against
  for anything that matters (loot, damage, progress), because the
  server is what keeps those honest. If load becomes a problem, spread
  worlds across more server processes or machines the server trusts.
  A server setting could allow player authority on private or trusted
  servers only. Not needed for the first version.
- The code today assumes one world; the first job is a `WorldInstance`
  that owns everything per world (generator, streamer, simulation,
  NPCs, structures, saving), so the game and the server can hold more
  than one.

## Instance dungeons

- **decided:** the server creates the instance when it's opened. It
  lasts an hour, or until a trigger closes it (the boss beaten).
  For now everyone can enter it.
- **decided:** players can leave early and come back while it's open.
- **decided:** dying inside keeps the timer running (respawn at the
  instance's entrance).
- **decided:** difficulty is fixed per dungeon for now. Later, players
  set an instance's options at the portal (difficulty, size, theme).
- **decided:** a dungeon selector at the portal before it opens: pick
  which dungeon (later, its options too), then the instance is made
  and the portal opens onto its portal room.
- **decided:** a way to close an instance early.
  - Proposal: "Close instance" in the portal's menu, from either
    side. Everyone inside gets a 30 s warning and is moved out. For
    now anyone may close it; later the one who opened it, or a vote.
- Proposals:
  - The countdown on screen, with warnings at 10 minutes and 1 minute.
  - When the boss dies an exit portal opens in its room, and the
    instance closes about 5 minutes later.
  - When it closes, everyone inside is moved out through the portal
    they came in by and the instance is deleted. What they carry
    stays with them; anything left inside (dropped, in chests) is
    gone.
  - If the server stops while an instance is open, its timer carries
    on from where it was when the server starts again.

## The first dungeon: an undead crypt

- **decided:** a main path with side rooms. No keys or locked doors
  yet (later, maybe). About 20 minutes to clear.
- What 20 minutes means:
  - About 10–14 rooms in total.
    - 7–9 on the main path.
    - 3–5 side rooms off it: a tougher fight, a chest, a shortcut
      back.
  - Two levels, joined by a stair down.
  - 25–40 undead in groups of 2–6 (the horde style), a few skeleton
    archers, and a boss at the end.
- **decided:** no traps for now (wanted later).
- **decided:** secret walls (below).

### Tiles and generation

- **decided:** the dungeon is generated from hand-built tiles, using
  wave function collapse (WFC) tools to grow a sprawling layout from
  them.
- **decided:** the grid. One cell is 32 × 32 voxels across (4 per
  chunk side) and 32 tall: the size for most things. A tile covers
  one or more cells (a corridor 1×1, a hall 2×3, a boss arena 4×4, a
  stair 1×2 and two cells tall).
- **decided:** smaller pieces too, to link small rooms.
  - Proposal: the grid underneath is half cells, 16 voxels, so a
    tile can be 16 wide (a narrow passage, a closet, a crawlway
    linking two rooms) and a 32 cell is 2 × 2 of them. Sockets are
    per half-cell edge, so a big tile's doorway lines up with a small
    one's. 16 voxels fits a 13-wide doorway with its walls.
  - Tile edges don't have to fall on chunk edges. A tile stamped
    across a chunk boundary is written into both chunks, as trees
    are now, and stored in each chunk's own file.
- Sockets: each cell edge of a tile is one of:
  - `wall`
  - `door` (a doorway 13 wide and 24 tall, centred)
  - `open` (no wall at all: one big room across tiles)
  - `stair_up` / `stair_down`

  Edges join where the sockets match.
- Rotations are made automatically (and mirror images where the tile
  allows them), so one tile gives up to 8.
- **decided:** tiles are blueprints (`data/blueprints`' format, the
  one the hammer already captures and the villages stamp), with extra
  fields. Blueprints are meant to hold all kinds of structures this
  way. Tiles live in `data/dungeons/<theme>/tiles/`.
- **decided:** tiles have variable height: 1 to several cells tall
  (32 voxels a cell; half cells, 16, for low passages), with stairs
  linking levels. Sockets are per cell face, sides and top/bottom.
- **decided:** a generated starter set for the crypt (rough but
  complete: corridors straight / corner / T / cross / dead end, small
  passages, rooms, side rooms, the stair down, the portal room, the
  boss arena), so the generator runs from the start; hand-built tiles
  replace them over time.
- What a tile knows, besides its voxels:
  - its size in cells and its sockets;
  - its theme and kind: `portal_room`, `corridor`, `room`, `side`,
    `stair`, `boss`, `filler`;
  - its weight (how often it's picked);
  - at most / at least how many per dungeon;
  - which depth it can be at;
  - its markers (below).
- Generating, seeded:
  1. **The spine:** a walk on the cell grid places the portal room
     (**decided:** every dungeon is entered from a portal room tile:
     you arrive there, and it's how you leave), the
     main path's rooms, the stair down and the boss arena, spaced to
     the target length.
  2. **WFC fill:** corridors and side rooms grow between and off the
     spine, constrained so every socket meets a match. When it gets
     stuck it backtracks; when it can't finish, it retries with the
     next seed.
  3. **Checks:** every room can be reached, the path from entrance to
     boss is in range, the side rooms count is in range.
  4. **Into chunks:** the "Dungeon" world type is solid rock with the
     dungeon in the middle of its height and no other terrain (no
     surface: you arrive in the portal room), so players are boxed in.
     Tiles are stamped into the chunks they reach, the way trees and
     decorations are now. (A dungeon in an open area isn't ruled out
     for later.)
- Dark, lit only by what the tiles hold (torches, braziers, glowing
  stone).

### Building tiles

- **decided:** tools to build tiles by hand.
- Proposal:
  - **A "Tile Workshop" dev world:** a row of build plots, each
    snapped to the cell grid, with the cell edges drawn and each
    edge's socket shown.
  - **A Tile mode on the Builder's Hammer:**
    - pick a plot's size in cells;
    - click an edge to cycle its socket;
    - place markers;
    - set the theme, kind and weight;
    - save.
  - Saved under `data/dungeons/<theme>/tiles/` (voxels plus a
    `.toml` with the rest), so tiles can be shared and edited.
  - A debug layer for a generated dungeon showing:
    - the cells and the tile in each;
    - the sockets;
    - the spine;
    - each room's role;
    - where WFC had to backtrack.

### Spaces that don't fit (later)

- **decided:** the portals should allow non-Euclidean spaces: a door
  that leads somewhere that couldn't fit there, a corridor longer
  inside than out, a room that loops back on itself. Portals inside a
  dungeon use the same seamless portals as between worlds (step 7).
- **decided:** the first dungeon has none of these besides its entry
  portal. Small rooms are linked with half cells, all on one grid.

### Skybox blocks

- **decided (idea to build):** a room can seem to be a floating island
  with sky all round it, using a *skybox block*: a block whose faces
  draw the sky (the same sky as the world's, by the direction you look
  through them, with no depth), so a wall of them looks like open sky
  stretching away.
- **decided:** skies differ by world, and a skybox block can show an
  otherworldly one. Proposal:
  - Skies as data, `data/registry/skies.toml`: colours by time of day,
    sun and moon (or none, or two), stars, clouds, a fixed time or the
    day cycle, and the light it gives (strength and colour: a violet
    sky lights the room violet).
  - Each world (and dimension) names its sky; the main world keeps
    today's.
  - A skybox block shows its world's sky by default; a tile (or the
    block's variant) can name another one, so a crypt room can open
    onto a strange sky while the world outside has a normal one.
  - The light the faces give follows the sky they show.
- A setting (and the plain look for building, like the other shader
  effects) draws them as plain blocks, so a tile is easy to plan.
- **decided:** they spread light as if open to the sky: each face
  is a sky light source at full strength (following the day, so the
  room darkens at night), and the light carries into the room as it
  does under an open sky.
- Proposal: they're solid (you can't walk off into them) unless a tile
  marks a fall that drops you somewhere (later).

### Markers in tiles

- **decided:** markers are blocks you place with the hammer like any
  other, labelled in the workshop, turned into the real thing (or
  nothing) when the tile is stamped into a dungeon. Settings a block
  can't hold (what a spawner spawns) are set by pointing at the marker
  in the hammer's Tile mode.

- **decided:** entity spawners placed in tiles, with a rule to spawn
  the enemies when the tile is entered.
  - A spawner marker says what spawns (an NPC type or a group from a
    table), how many, and when:
    - `enter`: a player steps into the tile;
    - `sight`: a player can see the spot;
    - `boss`: with the boss fight;
    - `wave`: after the room's last group falls.
  - Builds on the spawners there are now (`sim/Spawners.h`).
- Other markers:
  - `light`: a torch or brazier.
  - `chest`: empty for now; loot later.
  - `portal`: in the portal room, where the portal stands (you
    arrive and leave through it).
  - `exit`: where the exit portal opens after the boss.
  - `boss`: the arena's centre.
  - `secret` (next section).
  - `coffin`: an undead bursts out as you pass (ties to the
    spawner's `enter` rule).
- **decided: secret walls.** A stretch of wall marked secret is a
  breakable structure with health, like a tree. Hitting it wears it
  down, with cracks showing as it weakens, and then it crumbles
  (dust, rubble), opening the way. A faint tell (a slightly
  different stone) so they can be found.

### Enemies

- **The undead** (exist, horde style): the crypt's crowd.
- **decided: a skeleton archer.** The same human body as everyone,
  built as bones (`Appearance::skeleton`: thin limbs, ribs, a skull
  with eye sockets) instead of with textures.
  - Keeps its distance: a ring at range around its target. It backs
    off when you close in, and swings a dagger if cornered.
  - Draws with a clear wind-up (the telegraph), aims where you're
    going, and the arrow flies with gravity and can be dodged. A
    perfect dodge works on arrows too.
  - Arrows hit through the same hit system as swings: flinch on a
    normal shot, stumble on a full draw.
- **decided: the first boss:** a large human (the same body scaled
  up, about 1.6×, with its reach, mass and poise to match). Spawned by
  a boss marker in the arena tile for now; a boss spawner may replace
  that later. Proposal, "the Crypt Lord":
  - A big armoured undead. Its armour shrugs off flinches and
    stumbles, so only its poise breaking staggers it.
  - Attacks:
    - wide heavy sweeps;
    - an overhead slam that sends a shockwave along the ground;
    - a grab or charge to close distance.
  - At half health it calls the coffins in the arena open (more
    undead), and its attacks get faster.
  - A health bar at the top of the screen while you fight it.
  - When it dies: the exit portal opens and the instance starts its
    last 5 minutes.

### The bow (players)

- **decided:** a bow, with abilities in the same style as the melee.
  Proposal:
  - **Light:** a quick shot (partial draw).
  - **Heavy:** hold to draw. A full draw shoots harder and further,
    and makes the target stumble (the same charge as heavy melee
    attacks).
  - **Aiming:** where you look. With lock-on, at the target with a
    little lead; on a pad, a gentle pull onto the nearest foe.
  - Dodging or rolling cancels a draw. You move slowly while drawn.
  - Arrows are real projectiles (they fall, can be dodged, stick in
    walls).
  - Ammo: unlimited for now; arrows as items later.
  - The same arrow and projectile code serves the skeleton archer.

## Build order

1. **Tiles:** the tile format (cells, sockets, rotations, markers),
   the Tile Workshop dev world, the hammer's Tile mode, and saving
   and loading tiles.
   - **Done (2026-11-20):**
     - The format: `dungeon/Tile.h`, a blueprint with a `[tile]` table
       and a `.vxvol` voxel layer; half cells of 16; sockets wall /
       door / open / shaft; turns and mirrors.
     - Marker blocks `marker_spawner`, `chest`, `portal`, `exit`,
       `boss`, `coffin`, `secret`, painted by
       `tools/gen_marker_textures.py`.
     - The crypt's starter set: 17 tiles, 68 ways to place them
       (`dungeon/StarterTiles.h`, written to
       `data/dungeons/crypt/tiles`).
     - Building tiles from the console in any world (`tile new`,
       `edit`, `socket`, `marker`, `save`, `showcase`, `starter`,
       `list`; `dungeon/Workshop.h`), with a `tiles` debug layer.
     - `tile_test`.
   - **Not yet:**
     - The hammer's Tile mode: the console does it for now.
     - A ready-made Tile Workshop dev world: a flat stone world works.
2. **Generator:** spine, WFC fill, checks, stamping into chunks; a
   "Dungeon" world type on the New World screen (seed, theme) to walk
   one straight away; the debug layer; tests (always connected, path
   length, room counts, same seed same dungeon).
   - **Done (2026-11-20):**
     - `dungeon/Layout.h` grows the layout on the half-cell grid:
       1. the portal room;
       2. the main path (1-3 corridors, then a room, 7-9 times,
          down the stair halfway, the boss's arena last);
       3. side rooms off its spare doors;
       4. a dead end on every door left open (one with no room even
          for that is walled up);
       5. the checks.

       Every tile must agree with all its neighbours' sockets (the WFC
       rule); a stretch that can't be finished is undone and tried
       again, an attempt that can't be finished starts over. 60 seeds
       out of 60 grow, in 5 attempts at most.
     - The Dungeon world type (`worldgen/DungeonGenerator.h`): rock,
       the tiles stamped through it, you arrive by the portal. It's
       on the New World screen with a map of the layout.
     - The `dungeon` debug layer and the `dungeon` console command
       (info, map, tp).
     - `dungeon_test`, including walking the main path end to end with
       the NPC pathfinder.
3. **Contents:** spawner markers with their rules, secret walls,
   lights, chests (empty).
   - **Done (2026-11-20):** `dungeon/Run.h`, run by the game or the
     server in a Dungeon world:
     - Spawners fire once, by their rule (`enter`: a player steps into
       their tile; `sight`: within 56 voxels on their level; `boss`:
       with the boss; `wave`: as enter for now).
     - Coffins burst as you pass within 22 voxels.
     - The boss marker raises the boss when its arena is entered (a
       stand-in `crypt_lord` in npcs.toml until step 5).
     - Chests are put down, empty, facing into the room.
     - Secret walls break after 4 light hits (a heavy counts double),
       knocking chunks out as they go.
     - What happened is kept in `dungeon.dat`.
     - Lights: chandeliers and braziers in the big starter rooms, and
       a gentler light fade (Settings > Graphics > Light fade:
       Classic / Gentle).
   - Not yet: `wave` as its own rule (after the room's last group
     falls); the portal and exit markers (steps 5 and 6).
4. **Ranged combat:** arrows as projectiles, the player's bow, the
   skeleton archer.
   - **Done (2026-11-20):**
     - Arrows (`Simulation::shoot`, attacks with `effect = "arrow"`):
       real projectiles that fall, hit through `combat::shotHit` (a
       reaction like a swing's: flinch, a full draw stumbles; a guard
       facing it stops it; a roll passes through, a perfect dodge
       counts), and stick in walls for 30 s. The player aims along
       the crosshair (`Human::aimPitch`, sent up with each input), with
       a 6° pull toward an enemy near the line; an NPC leads its target.
       The draw sets speed, reach, damage and poise.
     - The bow (`short_bow`, moveset `bow`): light a quick shot, heavy
       held to draw (charge), a dodge cancels it, you walk at 40%
       while drawing. The `bone_dagger` (moveset `dagger`). Held
       models from `tools/gen_held_models.py`. Get them with
       `give short_bow`.
     - The skeleton archer (`skeleton_archer`, style `ranged`,
       `npc/States.h` `Combat::rangedFight`): keeps 24-75 voxels off
       with a clear line, backs off when you close in, holds its
       draw 0.9-1.5 s (the outline's charge glow is the telegraph),
       and pulls its dagger when caught close or backed into a wall.
       Spawners in `room_hall` and `room_pillars`.
     - Its look: `Appearance::skeleton` (`look_bones`) builds bones on
       the same frame instead of a body, so no texture sets were
       needed; preset `skeleton`.
     - `bow_test`.
   - **Not yet:** a real bow animation (the spell-cast clip stands in),
     arrows as items, sounds for the bow, lock-on lead for the
     player. In multiplayer only the host's own simulation draws
     arrows; clients don't see them in flight yet.
5. **The boss:** the Crypt Lord, its arena tile, the boss health bar,
   the completion trigger.
   - **Done (2026-11-20):**
     - Giants: `look_scale` (`Appearance::scale`) sets
       `CharacterModel::worldScale`, so the drawn body, its posed hit
       boxes, its weapon and its ragdoll all grow; `Human::scale` /
       `setScale` / `radius()` grow its collision, eyes and steps. Mass
       goes by the cube.
     - The Crypt Lord (`crypt_lord`, look `crypt_lord`: 1.6x, 900
       health, armour 2, `boss = true`) with the `crypt_greatsword`
       (moveset `greatsword`): two wide sweeps, an overhead slam whose
       blade hits and then sends a shockwave along the ground
       (`effect = "shockwave"`, `Simulation::quake`: jump it, roll it or
       guard it), and a charge from afar (`specials`, started by name
       through `HumanIntent::special`).
     - Its brain (`style = "boss"`, `Combat::bossFight`): moves picked by
       distance, slow walking in between, never guards or rolls. Below
       half health (`phase`): a roar, faster attacks (`phase_speed`,
       `Human::attackSpeed`), shorter pauses, more combos.
     - The arena (`dungeon/Run.h`): iron bars drop across its doorway
       while the boss lives and someone is inside (they lift if
       everyone's out for 2 s, so a death never locks you out); two
       coffins open with the boss and four more in its second phase
       (marker rule `phase`); when it dies the dungeon is cleared
       (saved), the bars lift for good and the exit glows.
     - The boss health bar across the top of the screen (with its
       phase mark and the health just lost).
     - `boss_test`, and the arena in `dungeon_test`. `tile_test` now
       fails if `data/dungeons/crypt/tiles` is older than the starter
       set (`tile_test write` updates it).
   - **Not yet:** its own animations (the sword clips stand in); a
     grab; the exit portal itself and the instance's last 5 minutes
     (step 6).
6. **Worlds:** realm saves, `WorldInstance`, the portal at spawn (with
   the world generation option), travel with a fade, instances
   (create, timer, close, delete, respawn at the entrance).
   - **Done (2026-11-20), single player, one world loaded at a time:**
     - The realm (`world/Realm.h`): the main world's folder is the
       realm; `realm.dat` lists its open instances (with their clocks),
       where the player is and the spawn gate; an instance is a world
       save of its own in `instances/<id>/`, opened by the name
       `<world>/instances/<id>`. Old saves are realms already. The
       player's file stays in the main world's folder (`away=1` while
       they're in an instance: opened from the menu, they come out at
       the gate).
     - Gates (`game/Gate.h`): an obsidian arch, a 10 x 22 opening, the
       `portal_veil` block filling it while open. The main world's is
       placed a few steps from the spawn facing it (New World >
       Portal: At spawn / None; `portal=` in world.vxrp; on by default
       except dungeons and dev worlds) and built once its ground is
       loaded. A dungeon's portal room has one (always open: home), and
       its exit gets one when the boss falls.
     - The game (`game/Portals.h`, main.cpp): Interact at a gate opens
       its menu (the dungeon selector: `portals.toml`; close it early).
       Walk into an open veil: a fade, the far world opened with the
       same player, out in front of the far gate.
     - Instances: an hour (counting while you play, saved), 5 minutes
       once the boss falls, 30 s once closed at a gate; warnings at 10
       and 1 minute; the clock on screen inside; at zero you're moved
       out and it's deleted. Dying inside respawns you in its portal
       room.
     - Console `portal` (open, close, go, time, tp); `portal_test`.
   - **Not yet:** `WorldInstance` and more than one world loaded (needed
     for the live view and for servers: step 7); portals on a server;
     persistent dimensions and dungeon templates (only instances so
     far); a nicer gate model and its sound.
7. **Seamless portals:** the view through the frame, walking through,
   followers; the server running several worlds; the portal hub.
   Eddie's order (2026-11-20): several worlds running at once first,
   each with its own settings (a hub world to load into others is
   wanted, maybe for servers too), then the view and the crossing;
   doors and windows as portals later.
   - **Done (2026-11-20): several worlds at once (game side).**
     - No more one-world globals: a `ChunkManager` knows its own size
       (`sizeX/sizeZ`; `WORLD_SIZE_X/Z` are gone), a `GameSession` has
       its own settings (`info`, its world.vxrp; `gWorldSave` is gone)
       and can own its simulation, and every simulation has its own
       clock (`Simulation::day`; `gClock` is gone).
     - `server/Worlds.h`: every world open at once, by name. The game
       draws one (the focus, streamed and meshed); the others run
       headless at the same time (their simulations, dungeons, edits,
       clocks), with ground kept loaded round their players and round
       watched points (a ChunkKeeper each). A world nobody is in sleeps
       and closes after a minute (saved); the realm's main world is
       pinned open. Players move between simulations
       (`Simulation::takePlayer / adoptPlayer`, the same body).
     - main.cpp works on `here` (the focus); the lobby holds the player
       with no world open. A trip through a gate moves the player into
       a world that's already running: walking toward a gate opens its
       far side in the background and loads the ground there, so the
       fade is only a blink. The world left keeps running.
     - `worlds_test`.
   - **Done (2026-11-20): seamless portals in one world, and the portal
     gun.**
     - `world/PortalLinks.h`: a link joins two ends (flat openings on
       walls; `through()` the way from one to the other: a half turn
       between their frames). One way (only end a opens and shows the
       far side; b is a plain wall) or two way. Each world has its own
       (`GameSession::portals`).
     - Crossing (`Simulation::crossPortals`): a body whose middle goes
       into an opening comes out of the other end, still moving, turned;
       the game turns the camera with it and carries the view's
       smoothing through (`PlayerController` `portalTurnDeg`). Bodies
       walk into the wall behind an opening (`passable`: the voxels stay,
       physics lets bodies through).
     - The view (`render/PortalView.h`, main.cpp): the far side drawn
       from a second camera into a stencil of the opening, its near plane
       leaned onto the other end; sky, chunks, you and the NPCs over
       there; the chunks round a far end kept meshed
       (`WorldStreamer::setExtraCentres`). One level deep. A coloured rim.
     - The portal gun (`dev_portal_gun`, in the dev kit; `game/PortalGun.h`):
       left / right click put the blue / orange end on a wall (standing on
       the floor, slid to fit), middle click takes them away; console
       `portalgun oneway|twoway|clear`.
     - A test script (`VOXELRPG_SCRIPT=<file>`: world, pos, look, wait,
       walk, gun, shot, dbg, /console, quit) to check what's drawn without anyone at
       the keys; `seamless_test`.
   - **Done (2026-11-20): the dungeon gates, seamless across worlds.**
     - A link's far end can be in another open world (`PortalEnd`'s
       world; `Crossing::world`). Walking through queues a
       `Simulation::Transfer`; `Worlds::transfer` moves the body (player
       or NPC, turned) into the other world's simulation; main.cpp's
       `switchFocus` swaps the focus and streamers, no fade, no load.
     - `PortalDirector::syncGates` links the main gate and the crypt's
       entry gate (and its exit once the boss falls) once both are built
       and loaded; the veils go. Until then the old fade is the fallback.
     - The far world is kept loaded around its end only
       (`farStreamers` in main, `WorldStreamer::meshRadius`,
       `Worlds::setStreamed`); its NPCs are drawn through the arch and
       can follow you through.
     - The opening is drawn as a shallow box (`PortalView.h`
       drawOpening `behind`), so the view doesn't blink while the
       camera passes through the surface; polygon offset against the
       wall it's on.
     - The arch is one sided: from behind it's an arch.
     - `seamless_test` "the gate into the crypt" section.
   - **Done (2026-11-20): both sides, bodies part way through, culling.**
     - An end's back (`PortalEnd::back`): Wall (one sided), Through
       (back of A out of back of B, a doorway in the open), Turn (either
       side of A out of B's front). A back that goes somewhere else is a
       second link on the same opening turned round. `PortalLink::faces`
       lists the ways in; crossing, holes and drawing go by faces. Gates
       are Turn.
     - The portal gun stands an end on the ground (both sides open;
       Through when both stand, Turn when the other's on a wall); on a
       wall as before.
     - A body part way through is cut at the surface
       (`CharacterRenderer::clip`, gl_ClipDistance) and a copy comes out
       of the far face (`xform`), both ways, across worlds too
       (`Human::lastExit…` for the half still going in). main.cpp
       `bodyCut`, `addCopies`, `drawCopies`.
     - A portal view only draws sections seen through its opening
       (`portalview::openingPlanes`, `OpenGLRenderer::narrowFrustum`).
       Script `dbg` prints draw counts.
   - **Next:** floors and ceilings; projectiles; portals seen through portals;
     occlusion culling and batching (world and portal views);
     the server running several worlds; the hub world; doors and windows
     as portals.

Steps 1–5 are the crypt itself and can be played in a Dungeon world
before portals exist. Step 6 joins it to the main world.

## Open questions

- Whether a big body (the boss) needs its own hit boxes and
  collision, or the human ones scaled.
- Instance options at the portal (later): which ones?
- Loot and resources from dungeons (later): what drops, and what
  only comes from dungeons?
