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
  with new skeleton textures (bones, empty eye sockets). NPC texture
  sets, drawn by the renderer, are needed for that, and the undead
  can use them too.
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
4. **Ranged combat:** arrows as projectiles, the player's bow, the
   skeleton archer.
5. **The boss:** the Crypt Lord, its arena tile, the boss health bar,
   the completion trigger.
6. **Worlds:** realm saves, `WorldInstance`, the portal at spawn (with
   the world generation option), travel with a fade, instances
   (create, timer, close, delete, respawn at the entrance).
7. **Seamless portals:** the view through the frame, walking through,
   followers; the server running several worlds; the portal hub.

Steps 1–5 are the crypt itself and can be played in a Dungeon world
before portals exist. Step 6 joins it to the main world.

## Open questions

- Whether a big body (the boss) needs its own hit boxes and
  collision, or the human ones scaled.
- Instance options at the portal (later): which ones?
- Loot and resources from dungeons (later): what drops, and what
  only comes from dungeons?
