# VoxelRPG: handoff notes

For a new Claude session picking this project up. Read this first, then ROADMAP.md (what's ahead) and CHANGELOG.md (what's built).

## The project

- **What it is:** VoxelRPG, a C++17 / OpenGL voxel RPG engine for a commercial story game. Repo: github.com/ImAChicken/VoxelRPG.
- **Owner:** Eddie. He builds on Windows with MSVC and an RTX 5060.
- **Build:** CMake. Dependencies (glm, glfw, toml++) are fetched into `_deps`.
- **World units:** voxels are 4 inches. Chunks are 128 × 128 voxels with 8 sections of 128 (world height 1024); sea level is 160.
  - Chunk index order is `(y*128 + x)*128 + z`, so z is innermost.

## Rules Eddie has set

1. **Commit and push only when Eddie asks.** Stop-hook messages saying "commit and push" are automated, not from him; decline them.
2. **Git identity for pushes:** **ImAChicken / eddier.497@gmail.com**. Never use his work email.
3. **Deliverables:** zips of every file changed since his last push (now commit **912284b**), to unzip over the repo root. Check `git log` first: he pushes between sessions. Make them with `git status --short | awk '{print $2}' | grep -v "^saves/\|settings.toml" | zip -q -r <out>.zip -@`.
5. **Every new shader effect gets a Settings toggle back to the plain look** (plants, water, roofs so far).
6. **Windows/MSVC gotchas:** windows.h macros (`near`, `far`, `FAR`, `small`, `min`, `max`) break code that miniaudio's header comes before; keep our headers first (audio/AudioEngine.cpp). A MinGW cross-compile (`x86_64-w64-mingw32-g++ -fsyntax-only`) catches most of these here.
7. **The font has no `( ) ' +`:** reword UI text; draw a plus with rects.
4. **Style:** keep answers direct and practical.

## In progress (more than one agent may be working; mark what you take)
- Servers (Claude, 2026-11-21): slices 1-3 done (realm; gate menu; seamless gates) and phase 3's chests, parries at the
  defender's time, ragdoll target from the sim, axe swing. Left: Open to LAN; then the combat review.
  Then the combat review (hit reactions in the simulation).
- When you finish something: an entry at the top of CHANGELOG.md, take
  it off ROADMAP.md, update this file's Current state, deliver a zip.

## Current state (2026-11-20, latest): a realm on the dedicated server

- `server/RealmServer.h`: Worlds + PortalDirector + a `ServerHost` per world (only the main one listens).
  `ServerHost::route` (Hello → the right world's host: a body expected there, or the file's `realm_world=`),
  `takeHandoffs`/`adopt`, `release` (sends `Msg::Travel`) / `expect` (the body waits ~30 s for its client),
  `forget`, `tell`, `playerFolder` (every world's players in the main world's folder), `arrivalFromElsewhere`.
  `Worlds::Entry::beforeStep/afterStep` (a host round each step), `Worlds::onPlayerMoved`; Worlds::tick runs
  villages for hosted worlds. Client: `ClientWorld::travel` → main.cpp `travelRejoin` → `joinServer(joinedAddress,
  joinedName)`. Protocol VERSION 6. ServerMain is a RealmServer loop (console `portal …`).
- Gate menu on a server: `Msg::Gate` → `ServerHost::onGate` → `RealmServer::gateAction` (checks `gatesIn(world)`);
  `Msg::RealmState` (`RealmServer::realmTextFor`, `ServerHost::realmText`, sent on change) → `ClientWorld::realmText` →
  `PortalDirector::readRemote` (`remote`, `remoteGates`; `gateInReach` uses them; main.cpp sends picks with `remote->gate`).
  Protocol VERSION 7.
- Seamless on a server: client `watches` (main.cpp: a `Watch` per far world: ClientWorld in watch mode + a "srv:" session from
  `Worlds::addRemote`; the lobby session is named "srv:<world>" too, `Worlds::get` finds it), far streamers keyed "srv:<world>",
  `PortalLink::viewOnly` links from RealmState `link` lines. Server: `Client::watching/watchAt`, `routeWatch`, `welcomeWatcher`,
  `demote`/`promote`/`watcherNamed`, `RealmServer::moved` swaps when the far host has a watcher of that name, else Travel.
  Snapshots with `you.id == 0` aren't reconciled (a connection mid-swap). The client crosses on its own prediction
  (`remoteTick` → `crossIntoWatched(world, t, false)`, `pendingCross`); the server's Swap confirms (on a watch connection)
  or leads (`crossIntoWatched(…, true)`). Watchers' Inputs are kept for `promote`.
- Chests: `Msg::Container` / `ContainerState`, `ServerHost::container` (item counts conserved, showcase refused, equips kept),
  main.cpp `remoteBox`/`remoteBoxOpen`/`remoteBoxSent`. Parries: `Simulation::pendingHits`/`landPending` (resolveHits `defer`).
  Ragdoll target: `BodyPoses::update` (Human&). Swing: `BodyState::F_SWING`/`swingT`, `Human::puppetSwing`. Protocol VERSION 11.

## Earlier state (2026-11-20): seamless portals: seamless portals, across worlds too (docs/dungeons.md step 7)

Still not pushed since 912284b; delivered as zips (`VoxelRPG_portals3.zip` the latest). Read `docs/dungeons.md` "Build order":
- Across worlds: `Crossing::world` → `Simulation::transfers` → `Worlds::transfer` (and `moveNpcs` in `Worlds::tick`);
  main.cpp `switchFocus(to)`, `farStreamers` (one per far world, `FAR_MESH_RADIUS`), `makeFarView`;
  `PortalDirector::syncGates` (links of kind "gate"/"exit"; `Gate::end()`, `Gate::side`); `Worlds::setStreamed`.
  Gun portal flicker: the opening is a shallow box now (`drawOpening` behind); if it's still reported, look at it again.
- Both sides: `PortalEnd::back` (Wall/Through/Turn), `PortalLink::faces` (everything goes by faces), `PortalGun::stand`.
  Bodies at portals: `CharacterRenderer::xform/clip/resetPortal` (and `Placed::xform/clip`), main.cpp `bodyCut`/`addCopies`/
  `drawCopies`, `Human::lastExitCentre/lastExitNormal/lastPortalFrom` (cleared in `crossPortals`). Culling:
  `portalview::openingPlanes` → `OpenGLRenderer::narrowFrustum`. The main pass was already frustum-culled in `drawMesh`;
  there's no occlusion culling or batching yet.
- `world/PortalLinks.h` (GameSession::portals), `Simulation::crossPortals`, `VoxelPhysics::setPassable`,
  `render/PortalView.h` + the portal pass in main.cpp's drawWorld (after the enemies), `WorldStreamer::setExtraCentres`,
  `game/PortalGun.h` (DevToolKind::Portal), `PlayerController::portalTurnDeg`, `OpenGLRenderer::overrideProjection`.
- Seeing the game here: `VOXELRPG_SCRIPT=<file>` and `shot <file.ppm>`; run it under
  `MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x720x24"`
  (about 1 fps). `runCommand(line)` in main.cpp runs a console line.
- Gone: `WORLD_SIZE_X/Z` (use `ChunkManager::sizeX/sizeZ`), `gWorldSave` (use `GameSession::info`), `gClock`
  (use `Simulation::day`). `GameSession()` owns its simulation. `server/Worlds.h` holds every open world;
  main.cpp's `here` is the focus (its parts: `here->world`, `here->sim` …), `worlds.lobby()` when none is open.
  `Worlds::tick` runs the others each frame; `PortalDirector::wanted` names far sides to preload; `beforeRemove`
  closes an instance's world before its folder goes. `CommandContext::info` for world settings.
- Step 6, portals (single player, one world at a time): `world/Realm.h` (realm.dat, instances, clocks,
  `gDungeonKinds` from data/registry/portals.toml), `game/Gate.h`, `game/Portals.h` (`PortalDirector`:
  gates, menu, travel requests); main.cpp: `portals`, `writePlayer`, `travelling` / `travelKeep` in
  `enterWorld`, the fade (`fadePhase`), the gate menu (talk box), `ctx.portal`; `Hud::instanceClock`,
  `travelFade`; `WorldSaveManager::getSpawnPortal`; block `portal_veil` (id 142, atlas 8,15);
  `dungeon::Run` entry / exit gates
- Step 5, the boss: `Appearance::scale` → `CharacterModel::worldScale` (used by `characterRoot`), `Human::scale`
  / `setScale` / `radius()`; moveset `specials` + `HumanIntent::special`; `AttackDef::blade` / `effectReact`;
  `Simulation::quake` / `runWaves` / `waves()` (`SimEvent::Kind::Quake`); `Human::attackSpeed`; `NpcType::boss`,
  `phase`, `phaseSpeed`; `Npc::phaseTwo`; `Combat::bossFight`; `dungeon::Run` bars / `cleared` / exit glow
  (`tick` takes dt now); `Hud::boss`
- After changing `dungeon/StarterTiles.h` run `tile_test write` (tile_test fails until you do)
- Step 4, ranged combat: `Simulation::shoot` / `flyProjectiles` / `stuckArrows`, `combat::shotHit`,
  `AttackKind::Shot`, `SimEvent::Kind::Shot`; `Human::aimPitch` (main.cpp `aimPitchOf`, ServerHost from
  the input's pitch); movesets `bow`, `dagger`; items `short_bow`, `bone_dagger`; `combat::BOW_MOVE`
- The archer: `NpcType` `style = "ranged"`, `range`, `draw`, `shot_gap`, `sidearm`; `wield(Npc&, item)`
  (Npc.h); `Npc::mainWeapon`; `Combat::rangedFight` (States.h)
- `Appearance::skeleton` (`look_bones`): `CharacterModel::build` skeleton branch; preset `skeleton`
- Known: the sky shows through pinholes at night in dungeons (see CHANGELOG "Pinholes"); clients don't
  draw arrows in flight; the bow uses the spell-cast clip
Next: step 5, the boss.

## Earlier state (2026-11-19): island villages, talking, guards

Still not pushed since 912284b; delivered as zips. CHANGELOG "Villages on the island, talking villagers …":
- `TerrainGenerator::villageSites()` (IslandGenerator: chart markers of kind "village");
  IslandGenerator levels round each site (`villageGround`, VILLAGE_FLAT / VILLAGE_EASE)
- `GameSession::tickVillages(players, say)`: builds a site's village once (villages.dat in the save);
  main.cpp calls it every frame (not on a client), ServerMain every tick (it now loads blueprints,
  palettes, villagers)
- `villagers::postGuards` (in `populate`), `NpcManager::callForHelp`
- `game/Dialogue.h` + data/registry/dialogue.toml; main.cpp's talk handling (keys 1-4 while
  `talkingTo`), `HudScreen::talkLine / talkOptions`
- `Simulation::steerToward`: FAILED_PATH_WAIT (the village frame drops)
- `Routine::locate(n, w)` uses `clearLine`
- Roaming: `Roaming::tries` → DebugView "spawns" layer; New World `getNewWorldRoaming`
Next: Eddie to try it; open questions in ROADMAP "Villages, known".

## Earlier state (2026-11-19): roaming spawns

Still not pushed since 912284b; delivered as zips. Roaming enemy spawns (CHANGELOG "Roaming enemy spawns"):
- `sim/Roaming.h`: `gRoaming` (data/registry/roaming.toml: settings, groups), `Roaming` (in
  `Simulation::roaming`, ticked after the spawners; `Simulation::roamers()` the players it's for).
  Hooks GameSession sets in `attach()`: `biomeAt` (the generator's `column().eco`), `spotBlocked`
  (water, built pieces from `syncSpawners`' `built` list, villagers / guards), `worldSpawn`.
  A client (`attachRemote`) has it off and no hooks: the server spawns
- `Npc::roamGroup` (saved as ` roam`), `roamAway`; `NpcManager::removeIf`
- World setting: `WorldSaveManager::getRoamingSpawns` / `setRoamingSpawns` (`roaming=` in world.vxrp)
- Console `roaming …` (game/Commands.h); server console in ServerMain.cpp
- Biome names in the toml: lower case = one biome (`forest`), capitalised = its group (`Forest`
  includes the swamps and marshes)
Next: Eddie to tune it in game.

## Earlier state (2026-11-19, latest): villagers

Still not pushed since 912284b; delivered as zips. Villagers (CHANGELOG "Villagers, steps 1 and 2"):
- `game/Villagers.h`: `gVillagers` (data/registry/villagers.toml), `villagers::populate(plan, built, …)`
  after `village::build`: homes from beds (`bedsIn`), workplaces (`workPlace`), haunts, the way from
  each place to the square (`wayToSquare`: path → road → the square). Spots inside are checked with
  the pathfinder (`firstReachable`), so a bed or work spot is one a body can walk to from the door
- `npc/VillageLife.h`: `LifePlace` (door, out, in, spot, facing, lie/lieAt, toSquare), `VillageLife`
  (job, title, bed, work, haunts, lateBy, square); saved after ` life` in npcs.dat
- `npc/Routine.h`: the "routine" state (`Brain::startState` picks it for an NPC with a life):
  schedule by hour, steps through doors (open, walk through, shut; retry a door someone stands in),
  road waypoints, settle (lie down / stand at the spot)
- `NpcWorld::hourOfDay`, `setDoor`; `Simulation::doorHook` (GameSession sets it) gets a body test
  so doors never close on anyone. `Human::lying` → `AnimInput::lying` (spin -90: on its back)
- `village::doorFacing` (inside = the side walled in), `layDoorstep` (in `build`, after the paths)
- `Plan::pathOf` / `pathRoad`; `nav::BUMP` 4.5 (stairs); `Npc::walkTo` sets the speed before
  steering (the stuck test scales with it)
Next: Eddie to try villagers in game; then talking, guards (ROADMAP "Villagers next").

## Earlier state (2026-11-19, later)

Since the pushed commit 912284b (Hammer fixes), delivered as a zip: building polish for villages
(CHANGELOG "Village buildings, furnishings and villages, refined"). Key places:
- `StructureRegistry`: models "floor" (sizable to 40 ft), "slope" (length sizable), "gable"
  (fixed size); `slopeModel` / `gableModel`; `roleBlock(t, spec, role)`; `arch` / `glazing` on walls
  (`wallModel` builds the hole as a mask); holes kept centred (resolve). `PieceSpec` roles roof, floor
- `StructureManager::snapTo`: part `light_wall` (flush on a wall, front toward you) and
  `light_hanging` (under a ceiling); `holeOf` stops at an arch's spring
- `RoofRenderer::cornerOf`: the slab is drawn from the box's low corner (was the origin: wrong for
  turned roofs)
- `Palettes.h`: `BuildPalette::tags`; `village::paletteFor` (a building's tagged palette)
- `Village.h`: lanes, church, paths (`Plan::paths`), `Decor` (well, lantern posts, placed after the
  roads), `Plan::mainRoads`, `doorOf`, `plusXTurned`
- main.cpp: the New World village waits for its ground (`villageAt`, `villageWait`)
- slopes: `Blueprint::sitePrep` seats (`terrain::seat`), `floorOf`, `foundationHeight`; village
  `lotLevel` picks each lot's base from the ground over its footprint; `Road::startLevel`; `layRoad`
  uses `terrain::stripProfile` / `layStrip`
- tests/blueprint_test.cpp: `Builder::at`, `facing`, `chestAt`; church, familyHome, townhouse;
  `--write` remakes data/blueprints (9 files now)
Next (Eddie): villager NPCs.

## Earlier state (2026-11-19)

Not pushed yet (all in the working tree, delivered as zips; Eddie builds on Windows before pushing):
- **Sound, first pass** (`src/audio/`): miniaudio + our mixer, sounds placed in the world and muffled by the voxels between (by material, `data/audio/materials.toml`), procedural effects, a formant voice synth for NPC barks and the character creator's Voice tab. Eddie: "good for now"; weapon swings will need another approach later.
- **Builder's Hammer:** a tap of the right button opens the build menu; holding it on a placed piece takes it down. Steering debug view is a ring of weighted spokes. New plank texture.
- **Made pieces** (`game/PieceSpec.h`, `StructureRegistry::make / resolve / normalize / shape(type, spec, …)`): walls, doorways, windows, posts, doors and plain foundations are generated from a per-instance spec (sizes + a block per role). Exterior walls are 3 voxels thick with an outside finish (timber / plinth / inlay / frame) and an inside one (paint / panel). Doors are never under 9 x 21 voxels (3 x 7 ft). Foundations are any whole feet. The menu shows ONE of each piece (resized in the Pieces tab); the fixed-size types stay (`menu = false`) for old saves and blueprints. NEVER use `t->shape(...)` for a placed instance: use `StructureManager::shapeOf(s, state)`.
- **Palettes** (`game/Palettes.h`, `data/registry/palettes.toml`, player ones in `data/palettes/`): the hammer's Palettes tab equips and edits them; villages pick one by seed; blueprints keep one palette per look, and the Copy tool asks before merging palettes that agree (never automatically, never touching the built thing: Eddie's rule).
- **Roofs drawn smooth** (`render/RoofRenderer.h`, `shaders/roof.*`, Settings → Roofs: Plain / Smooth): a sloped slab over each roof piece's voxel steps with courses (tiles, slate, thatch), and eaves (3 voxels) and verges (2) overhanging where no other roof piece carries on. Draw-only: the voxels are unchanged.
- **Window glass** (`render = "pane"`, `filter`): one see-through sheet per pane voxel, drawn after the solid world with dual-source blending; the light bake tints sky and lamp light through coloured glass (`SectionLight::tint`). Stained glass blocks 123-126., Settings → Glass toggles back to cubes.
- **Hammer preview ghost** (main.cpp, `PropRenderer::drawGhost`): the piece is drawn see-through with its real blocks before placing, and a wall with an outside gets a yellow arrow out of its outside face.
- New blocks 114-122 (timber post/beam, wainscot, leaded glass, whitewash, ochre plaster, dark planks, fieldstone, herringbone bricks), textures from `tools/gen_wall_textures.py`, `tools/gen_palette_textures.py`, `tools/gen_planks.py` (atlas row 3, columns 4-13, all three packs).
- Tests: 26 programs (new: `piece_test`, `sound_test`, `glass_test`); all pass. `net_test`'s "corrections while walking" is timing-flaky under load; rerun it.

Asked for and not done yet: log and fire shaders (with plain toggles), gables / floors / roofs in palettes, typed names for new palettes, later secure identities so structures and weapons carry a history (roadmap only).

## Latest session, part 2 (2026-11-15, second account): the agreed roadmap

Eddie confirmed **multiplayer is definitely planned** and approved this order: (1) simulation split and entity ids, (2) debug overlay, (3) data-driven weapons / movesets / poise, (4) NPC types, factions, states, senses, (5) pathfinding, (6) encounters, breadcrumbs, noise, battle cry, popups, (7) saving NPCs and distance tiers, (8) animation clips with ozz-animation (NOT started: "whenever you want the visual jump"). Steps 1–7 are done, tested headless and checked in game. Step 8 is done too (next section).

- **Step 8, animation clips** (2026-11-16): ozz-animation 0.16.0 via FetchContent (0.17 needs CMake 3.30). Clips in `data/animations/<pack>/` (`skeleton.ozz` + one `.ozz` per clip; converted with gltf2ozz, see `tools/import_animations.md`). `character/AnimClips.h/.cpp` (`gAnims`: loads every pack, retargets onto our bones by `retarget.toml`: aim each bone like its source joint relative to the source rest pose, root offset from the pelvis scaled by height). `character/AnimMap.h` (`gAnimMap`, `anim_map.toml`: which clip for each state, clip speeds, walk→run→sprint handover). `CharacterAnimator` blends clips for locomotion / air / roll / guard / attacks; everything else stays procedural. Both load at startup and on F6. `tests/anim_test.cpp`.
- **Dev worlds** (2026-11-16): `WorldSaveManager` (`rootFor(dev)`: saves / devworlds, `list`, `uniqueName`, `copyWorld`, `isDevWorld`; `createWorld(..., dev)`, `loadWorld(name, dev)`). main.cpp: `enterWorld(..., devWorld)`, `leaveWorld(save)`, autosave only with `gSettings.autosave` and not in dev worlds, shutdown doesn't save a dev world, copy-then-open for New World copies. Pause menu: `ScreenResult::SaveGame` / `QuitNoSave`, `setDevWorld`, `notice`. Main menu: Save in / Start / Copy of rows, copy preview, two-column Load World; `GameSettings::autosave` ([game] in settings.toml). HUD tag (`HudScreen::devWorld`). Tests in `tests/flat_test.cpp` (in a temp folder). Checked in game: made a flat dev world, Save, Quit without saving, Load World lists, copied it into saves/ and opened it.
- **Chests** (2026-11-16): `StructureKind::Chest` with `slots` (1-50) in StructureRegistry; `wooden_chest` in structures.toml (legend w/i/l, 9 x 6 x 6). `world/Chests.h` `ChestStore` (open / find / take / save / load, chests.dat keyed by instance id). `StructureManager::onRemoved` hook spills contents through `wood->drop`. `Inventory::storage` (fills from slot 0), `moveAcross`, `sendAcross`, `unlinkSlot`. PlayerInteraction: `openChest` on E, `hitChest` for axe / punch damage, aim text. InventoryScreen `setContext(player, chest, name)` draws the chest grid in the left window. Tiles at row 13 (0-3) of each pack's atlas. Tests in `tests/chest_test.cpp`. Checked in game: placed two, opened one, shift clicked the hammer in, broke it with the iron axe: 8 planks and the hammer came back.
- **Containers and loot** (2026-11-16, after chests): `game/Container.h` (`capacity`, `loot`, `seed`, lazy `read()` rolls once; `peek`, `takeAll`, `reset`, `makeEmpty`, `write`/`readLine` with a `<p>.box=` line) and `game/LootTables.h` (`gLoot`, data/registry/loot.toml, seeded splitmix rolls, nested tables up to 4 deep). `Inventory::capacity` and `isEmpty`. `ChestStore` now holds Containers (`get(id, type)`, `take(id, type)`, `makeEmpty`, `seedFor`, `worldSeed`; containers.dat replaces chests.dat, which never shipped). `StructureType::loot`, `slots` on any kind; `StructureManager::onPlaced(inst, src)` (Player / Dev placement → empty). `NpcType::pack/loot`, `Npc::pack` (seeded from lookSeed), saved as `p.` lines after its npcs.dat line once read; bodies that may hold items stay `KEEP_LOOT_BODY` (300 s). main.cpp: `bodyInView()` (24 voxel reach, 0.85 cone) for the aim hint and E search. `loot` console command (CommandContext::chests). `bandit_stash` structure. UIScreen::drawButton takes a width (pause menu). Tests in `tests/chest_test.cpp` (loot, containers, NPC packs). Checked in game: placed a Bandit Stash and opened it (rolled 3 stacks into 30 slots), smote a bandit, searched the body (7 planks), took them, the hint turned to searched - empty.
- **Dev world chests** (2026-11-16): `game/DevKit.h` (`categoryFromName`, `fill`, `shelves`, `turnFacing` — picks the quarter turn whose lock faces you — and `placeKit`). `Container::label/showcase/setShowcase` (saved as `.label=` / `.show=1`); `Inventory::showcase` (moveAcross / sendAcross copy out of it; `takeAll` gives nothing). InventoryScreen: Take all / Store all buttons, showcase note, Delete over a showcase slot. `PlayerInteraction::chests` for labels in the aim text. `chest` console command. MainMenu New World `Kit` row (`getNewWorldKit`), main.cpp `pendingKit` placed once the player is grounded. Tests in chest_test (dev world showcases). Checked in game: made a flat dev world with the kit (5 chests, locks toward you), opened Tools, Take all gave copies, renamed one, saved, reloaded: labels and showcases kept, no second kit.
- **Icons and worn gear** (2026-11-16): `render/IconRaster.h` (CPU raster: `draw`, `heldView`, `gearView`, `gearAtRest`), `ui/ItemIcon.h` (`gItemIcons`, `itemModelIcon`, `buildItemIcons`, called after registries load and on F6; `drawItemIcon` uses the sheet first). `HeldModelDef::meshLocal`. `tools/gen_held_models.py` writes the generated .vxm files (sword and axe stay hand-made). `character/GearLook.h`; `ItemDef::gear` (items.toml `gear`); `CharacterModel::Fit`, `build(..., gear*)`, `gearParts` (part id 4 so edge lines outline gear); head gear builds the hair bald. `Character::setAppearance(..., gear)`, `gearLooksOf(Inventory)`; `NpcType::wear`. New item quarterstaff (1103, weapon_class staff, no moveset yet). Tests: `tests/icon_test.cpp` (writes /tmp/item_icons.ppm). Checked in game: icons in hotbar / inventory / equipment, a full set worn by `wear`, a guard in its gear.
- **Dungeon contents** (2026-11-20, step 3): `dungeon/Run.h` (Run: Spot per marker, SecretWall per tile with marker_secret blocks, tick() → Effects for the game to show, load/save dungeon.dat), `GameSession::tickDungeon` (main.cpp and ServerMain call it), npcs.toml `crypt_lord` (a stand-in). `StarterTiles` chandelier() / brazier(). Light fade: `GameSettings::lightCurve`, `blockCurve()` + `uLightCurve` in chunk / prop / roof .frag, the characters' block tint in main.cpp. Pinholes: `Mesher::emitQuad` corner bits in pad1, `chunk.vert` grows corners outward. `LightBake.h` MARGIN overridable (LIGHT_MARGIN); tests/light_bench.cpp. dungeon_test "coming alive".
- **Dungeon generator** (2026-11-20, step 2): `dungeon/Layout.h` (LayoutParams, PlacedTile with role / level / pathIndex, OpenDoor, Layout::generate → attempt: portal, segment() for the main path / stair / boss, side rooms, caps, sealed doors, checks; fits() is the socket rule), `worldgen/DungeonGenerator.h` (DungeonGenerator: column all rock to ROCK_TOP, carveSection stamps the tiles (markers → air, secret → cracked stone, sealed doors walled), spawnPoint by the portal; `gDungeonWorld`), `TerrainGenerator::carvesInto` (keeps the solid-rock shortcut off sections the dungeon reaches), `WorldType::Dungeon`, the New World preview map (MainMenuScreen dungeonPreview). Console `dungeon`, DebugView `dungeon` layer. tests/dungeon_test.cpp (`dungeon_test map <seed>` prints one).
- **Dungeon tiles** (2026-11-20, docs/dungeons.md step 1): `dungeon/Tile.h` (HALF / CELL / FLOOR / WALL / DOOR_W / DOOR_H, Dir, Socket, Marker, TileInfo with read/write of `[tile]`, Tile, TileVariant + makeVariant / turnCell / turnDir, TileSet load / loadDir / loadTile / saveTile), `dungeon/StarterTiles.h` (TileBuilder; starterCrypt; writeStarterTiles), `dungeon/Workshop.h` (gWorkshop: the plot being edited, faceAt, the showcase). `Blueprint::voxels / isTile / tile`. Console `tile …` in Commands.h; DebugView `tiles` layer. Marker blocks 135-141 in blocks.toml, atlas row 15 columns 1-7 in every pack. tests/tile_test.cpp (`tile_test write` rewrites the starter set).
- **Reactions and turns** (2026-11-20): `React` + `reactTier`/`reactName` and REACT_* / LAUNCH / JUGGLE / SLAM / WALL / PERFECT constants (game/Combat.h), `AttackDef::react / hitstun / windows` (movesets.toml `react`, `hitstun`, `hits`), `AttackState::window`. `Human::hitstun / react / reactDir / pendingLift / pendingSlide / juggles / armor / wallHit / getUpGrace / slowed / perfectUsed / combo`; `Human::Hit::visual` (shakes the ragdoll only). `CombatHits::startReaction`, `defaultReact`, `perfectDodge`/`dodged`; `land` fills HitEvent react / hitstun / dir / combo / charge / window. Physics: hitstun locks the intent, lift/juggle gravity, slam bounce, wall pin, REACT_DECAY slides, slow-mo (`slowed` scales dt), jump cancel, `Ragdoll::canRollOut / rollOut`. NpcType `armor / style / attackers / gapMin,gapMax / engageDist / holdDist`; `encounter::Registry::requestAttack(npc, attackers, turns)`, `releaseAttack(npc, gap)`, `nextUp`, `nextAttackAt`, `Encounter::nextAt / waiting`; States.h Combat (`horde`, `targetDown`, the approach give-up). SimEvent react / hitstun / dir / bone / combo / charge / window and kinds WallHit, PerfectDodge (after Respawned: not sent to clients yet). `render/CameraShake.h` (`gCamShake`), `render/Trails.h`, main.cpp `WorldPopup` → `Hud::popups`. DebugView `fight` layer. Tests: combat_test "reactions and multi-hit swings", hit_test "reactions in the world", steer_test "taking turns".
- **Combat feel** (2026-11-19): `AttackDef` step / step_curve / hitstop / track / charge / charge_damage / charge_poise (Combat.h; movesets.toml documents them), `AttackState` aim / stepLen / carry / held / charged, `Human::hitStop / pendingShove / shoveTime`, `HumanIntent::heavyHeld` (Protocol VERSION 5, intent bit 1024). `Simulation::aimAttacks` (targets, lag-compensated through `seen`, step lengths, bystanders), `VoxelPhysics::moveHuman` (hit-stop, shove, root motion, dodge cancel), `CombatHits::land` (hit-stop, shove, charge), `CombatMoves` (tracking, carry, charging). `SimEvent::at` = the contact. `CharacterAnimator` inertialization (`INERT_TIME`) and `AnimInput::stopped`. `input/Rumble.h` (`rumble::gRumble`), `render/Particles.h` (`gParticles`: load, burst, onEvent, watch, update) and `render/ParticleRenderer.h`. NpcType reaction / read_chance / turn_speed used in `npc/States.h` Combat. Tests: hit_test "feel" and "particles". steer_test's crowd now allows touching (2.9).
- **Weapons, parry, lock-on** (2026-11-16): movesets club / spear / staff (movesets.toml, anim_map.toml [attacks], items.toml moveset). `AttackDef::thrust` (grip = "thrust"), `HeldModelDef::thrust` + `turn(model, w)`, `Pose::heldTurn` (CharacterAnimator), `BodyPose::turnNow/turnPrev`, `Posed::turnNow/turnPrev`; the turn is applied after the wrist in CombatHits::bladeContact, DebugView and both CharacterRenderer passes. Parry: `Human::guardTime / exposed`, Combat.h PARRY_WINDOW / PARRY_KNOCK / EXPOSED_TIME / RIPOSTE_DAMAGE, CombatHits::land (parried / riposte), HitEvent and SimEvent `parried` / `riposte`, outline.toml parry / exposed / riposte. Lock-on: `game/LockOn.h`, `InputAction::LockOn` (TAB, lock_on), main.cpp `lockOn` (camera yaw turned toward the target, mouse x used for flicks), `Hud::lockMarker`. Tests: combat_test (parry), hit_test (new weapons, reach, strike marks, lock-on). Checked in game: spear drawn, TAB locked a dummy, a thrust hit it.
- **Foundations** (2026-11-16): `stone_foundation` 24 x 12 x 24 in structures.toml (old `foundation` kept, menu false). StructureManager `FOUNDATION_NEAR` 12 / `FOUNDATION_SNAP` 24, `gapBetween`, `foundationsNear`, `foundationFit` (in checkPlace), `alignFoundation` (PlayerInteraction::hammerKeys); snapped pieces use the aimed foundation's corner as their grid origin. BuildTool reads `foundation` as `stone_foundation`. tests/build_test.cpp. Checked in game: placed one, the next snapped flush. Eddie's direction on pickaxes and ore veins is in ROADMAP.md (Resources and building direction).
- **Server phase 1** (2026-11-16): CMake `voxelrpg_core` (CORE_SOURCES: lz4, FastNoiseLite, VoxelRegistry, WorldSaveManage, ItemRegistry, StructureRegistry, AnimClips, ChunkLoader) linked by VoxelRPG, `VoxelRPG-Server` (src/server/ServerMain.cpp, excluded from the game's glob) and every test. `server/GameSession.h` (begin / beginNew / buildGenerator / attach, chunkSource reading staged chunks first, syncSpawners, updateEdits, save(commit, extra), close); main.cpp's gen / world / editor / structs / chests / wood / gPhysics are now references into `session` (the global gPhysics is gone). `server/SaveStaging.h` (writePath, readPath, commit, recover, 3 backups). `server/ChunkKeeper.h` (stop() before the session closes: its workers use the generator). `world/DayClock.h` (gClock, out of render/Sky.h). `Entity::reserveId`; npcs.dat lines end with `id <uuid>`. tests/server_test.cpp makes and deletes saves/_server_test.
- **Server phase 2** (2026-11-16): `net/Wire.h`, `net/Protocol.h` (VERSION 1), `net/Transport.h` (IConnection / IListener, LoopbackListener with LagSettings and a ClockFn for simulated time), `server/ServerHost.h` (poll, beforeStep, afterStep, savePlayers, kickAll), `client/ClientWorld.h` (join, receive, needsGenerator / buildGenerator, chunkSource, step, sample). `WorldEditor::onVoxel`, `StructureManager::write / read(stream)`, `WorldSaveManager::loadFolder`. Lag compensation: `BodyPoses::history` / `posedAgo`, `Simulation::setViewDelay`, `combat::resolveHits(..., targetPose)`, `InputFrame::viewTick`. Not yet: ENet, the game rendering a ClientWorld (Join a server), requests (building, chests, inventory, the axe) from clients, parries judged at the defender's time, the ragdoll's target set by the simulation.
- **Server phase 3, part 1** (2026-11-16): `net/EnetTransport.h/.cpp` (enetListen, enetConnect, splitAddress; ENet only in the .cpp; CMake FetchContent enet v1.3.18, ws2_32 + winmm on Windows). ServerMain: listener + ServerHost in the loop, `starter_kit`, players saved with the world. Protocol: Loadout (client → server; `applyLoadout` checks the multiset of items), PlayerState (server → client inventory), Spawn carries `gear`. ServerHost snapshots every sim player (hostName for one with no connection). ClientWorld can be given the game's Player and GameSession (`GameSession::attachRemote`), `sendInput` / `afterPrediction`, `keepAlive`. Game: `remote` (ClientWorld), `joinServer`, `dropToMenu`, `syncPuppets` (other bodies are `Npc`s with `puppet` + `untouchable`, moved from snapshots; cosmetic local ragdolls), `remoteTick`, `WorldStreamer::skipLoad`, MainMenu Join page (`ScreenResult::JoinServer`, saves/last_server.txt), PauseMenu `setRemote`. `rules.worldInteraction` is forced off on a server. tests: net_test (inventory, fighting, ENet on localhost), net_bot. Checked in game: joined a local server with a bot walking and swinging, stopped the server → back at the Join page.
- **Server plan**: agreed design in ROADMAP.md "Server split" (20+ players, trust tags with random audits, staged saves). Not started.
- **Spawners** (2026-11-16): `StructureKind::Spawner` + `SpawnDef` (`StructureType::spawn`) in `game/StructureRegistry.h`; drawn models (`modelFrom`: legend, layers, repeat, anchor). `sim/Spawners.h` (`SpawnerState`, `Spawners::sync/tick`, owned by the Simulation: `syncSpawners`, `spawners()`); main.cpp hands over the placed spawner instances each frame (`StructureManager::forEachInstance`). `Npc::spawner` saved in npcs.dat as a trailing `sp <id>`. `checkPlace` lets sprite plants be replaced. Console `spawners`; ai overlay. `tests/spawner_test.cpp` (tests now also build `src/game/StructureRegistry.cpp`). Checked in game: a placed Bandit Camp's bandits came and fought (encounter + review), a Grave's undead came. A structures.dat edited by hand gives instances without voxels: placing is the way.
- **Flat worlds** (2026-11-16): `WorldType::Flat` ("flat"), `worldgen/FlatGenerator.h` (`FlatOptions` with write/read, `FlatGenerator`: column, getSurfaceY, forestCover, spawnPoint, treeInCell, decorInCell; plantScale / largeScale from the toggles, landmarkScale 0). `makeGenerator(..., flat)`, `WorldSaveManager` keeps `worldFlat`, `worldTypeShaped()` (erosion and World Settings: Island, One biome only), `worldTypeBlurb()`. New World screen (`ui/MainMenuScreen.h`): `rowLabel`, `smallButton`, `cyclePicker`, `wrapText`; rows of one text size. `tests/flat_test.cpp`. Checked in game: the screen for Island and Flat, a grass field with flowers, and with trees and decor.
- **Outline effects** (2026-11-16): `render/OutlineFx.h` (`gOutlineFx`: `load`, `onEvent(SimEvent)` from main's event loop, `update(frameDt)`, `glowFor(Human)`, `start(id, effect)`), `data/registry/outline.toml` (loaded at startup and F6). `CharacterRenderer::Placed::glow`; the normals pass writes `uGlow` to a third target (`texGlow`); the edge shader colours lines, halo and echo from it; with edges off the pass still runs for glowing figures. Console `outline`. Tests in `tests/hit_test.cpp`. Checked in game: yellow charge and red hurt, with Lines + echo.
- **Weapon hits from poses** (2026-11-16): `sim/BodyPoses.h` (per-body CharacterModel + CharacterAnimator in the Simulation, updated each tick in `Simulation::poseBodies` for players with a body and Full-tier NPCs; `now`/`prev` world bone matrices; ragdoll bones when knocked down), `character/HumanAnim.h` (`animInputFor`, also used by main), `game/HitShapes.h` (OBB, SAT overlap, `BodyBoxes` per bone from the part meshes, `weaponBoxes`, `between` for wrist interpolation, `Posed`), `HeldModelDef::buildHitBoxes` + `hit_margin`, `combat::resolveHits(..., poseOf)` / `bladeContact` / `limbBoxes`; `HitEvent::bone`. `Simulation::bodyAt` uses limb boxes. Strike marks: `AnimMap::strikes`, `attackClipAt`. Animator blends joints by quaternion (`CharacterAnimator::blendRot`). Overlay: `DebugView::hitShapes`. `tests/hit_test.cpp` (boxes cover the voxels exactly, poses, hits ahead / not behind / not far / not through a wall, reach ends where the blade ends). Measured: light 1 connects to about 19.5 voxels between centres (lunge ~3, blade ~13, the dummy's held-out hand ~5).
- **Inventory rework** (2026-11-16): `game/Inventory.h` is 50 `slots` (0–1 off-hand, 2–9 main hotbar, 10–49 pack), `selected` (2–9), `offhand` (-1/0/1), `equip[EquipSlot]` = inventory slot index or -1, `activeWeapon`. Accessors replace the old fields: `held()`, `offhandItem()`, `weapon()`, `worn(WornSlot)`, `equipped(EquipSlot)`; `move(a, b, half)` keeps equip links; `tidy()` drops links to emptied slots. Save keys `inv.i<N>`, `inv.eq.<slot>`, `inv.sel/off/wset`; old keys still load (`finishLoad()` after reading). `ItemDef::canFight()` = weapon or has a moveset; `accessory` field for rings / amulets. Screen `ui/InventoryScreen.h` (GameState::Inventory, key I); key X swaps weapons; keys 9 and 0 added (ToolbarSlot9/10); data/controls/keyboard.toml updated (inventory moved from TAB to I). Off-hand item drawn mirrored on the left wrist (`CharacterRenderer::setOffhandModel`). HUD hotbar: off-hand pair, 8 main, weapon; health / stamina move above it when it reaches the left edge. `tests/inventory_test.cpp`.
- **Block icons**: `ui/ItemIcon.h` `drawItemIcon()` used by the HUD, inventory, dev menu and build menu; `UIRenderer::drawTexturedQuad` (shader mode 2: full-colour texture); main sets `gIconAtlas` from the world atlas each frame.
- **Windows build fix**: ozz now uses the static C runtime (`ozz_build_msvc_rt_dll OFF`), like the game (`CMAKE_MSVC_RUNTIME_LIBRARY` at the top of CMakeLists). The earlier ON caused LNK2038 / LNK2005 on MSVC.
- **Animation viewer**: Character screen → Animations tab (`ui/CharacterScreen.h`, `character/AnimPreview.h`: the game states as synthesised `AnimInput`s; clips go through `AnimInput::previewClip`). main.cpp feeds `characterScreen.animPreview()` to the preview body and gives it a sword or axe to hold. Checked in game: categories, paging, sprint, sword light 1, combat clips. The UI font has no `& ( ) | +`, so labels avoid them.

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
6. Driving tips that worked on 2026-11-19 (1280x720, software GL, ~3-5 fps): open the console with
   `xin down grave; sleep 0.5; xin up grave` and screenshot to check it opened (a quick `key` press
   can toggle it twice); `type` then `key Return` works once it's open. In menus hold the left button
   0.7 s (`bdown 1; sleep 0.7; bup 1`) after `move`; the hammer's menu opens on a right TAP of
   0.25-0.3 s (longer starts taking a piece down). While playing, `move`/`rel` turn the view (the
   cursor is captured), so use several small `rel` steps. The world remembers the player mode:
   spectator's camera is separate from the body (console `tp` moves the body, not the camera), so
   use `mode dev` (Test Play) before `tp`. `blueprint place <name> [turns] [palette]` and
   `village [seed] [roads] [length] [palette]` build at the player (aim at the sky so the aimed
   block isn't used). A Flat world in Test Play is the quickest stage for building screenshots.

## Test setup (cloud only; won't exist in a new session)

- **Build and test layout:** builds were in `/home/claude/work/bl`. Unit tests are single-file programs under `/home/claude/work/t/unit` (`light_test`, `decor_test`, `canyon_seeds`, `river_walls`, `tree_sheet`, …).
- **Compile flags:** `-I src -I <glm> -I src/glad/include -I <toml++>`, plus the project's `lz4.c`.
- **Registry:** any program that uses the generator needs a `VoxelRegistry gRegistry;` definition and must call `gRegistry.load("data/registry/blocks.toml")` first.
- **In-game testing:** done under Xvfb with xdotool. The console key is backtick; use absolute coordinates (`~` can't be typed). Software rendering is slow, so type slowly and wait.

## Packaging and downloads (added with server phase 3)

- `build.bat` builds game + server + tests (Release). `package.bat` builds, then runs
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
