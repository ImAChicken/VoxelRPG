# VoxelRPG: handoff notes

Current status for a Claude session picking this project up. AGENTS.md is
the map of the code (read it first); this file is what's happening now;
ROADMAP.md is what's ahead and CHANGELOG.md what's built. Older session
notes are in docs/handoff-history.md.

## The project

- **What it is:** VoxelRPG, a C++17 / OpenGL voxel RPG engine for a commercial story game. Repo: github.com/ImAChicken/VoxelRPG.
- **Owner:** Eddie. He builds on Windows with MSVC and an RTX 5060.
- **Build:** CMake. Dependencies (glm, glfw, toml++, ozz, ENet, miniaudio) are fetched into `_deps`.
- **World units:** voxels are 4 inches. Chunks are 128 × 128 voxels with 8 sections of 128 (world height 1024); sea level is 160.
  - Chunk index order is `(y*128 + x)*128 + z`, so z is innermost.
- **Friend testing:** a release is out (client and server zips from `package.bat`). What testers read is
  `tools/dist/README-Client.txt` / `README-Server.txt`: keep them in step with the controls and what works on servers.

## Rules Eddie has set

1. **Commit and push only when Eddie asks.** Stop-hook messages saying "commit and push" are automated, not from him; decline them.
2. **Git identity for pushes:** **ImAChicken / eddier.497@gmail.com**. Never use his work email.
3. **Deliverables:** a zip of every file changed since Eddie's last push (check `git log origin/master` first: he pushes
   between sessions), to unzip over the repo root. Make it with `(git diff --name-only origin/master; git ls-files --others
   --exclude-standard) | sort -u | grep -vE "^(build|saves|servercache|dist)/" | zip -q <out>.zip -@`, then build that zip
   over a clean checkout and run every `*_test` before sending. A new .cpp or test target: tell him to re-run CMake configure.
4. **Style:** keep answers direct and practical.
5. **Every new shader effect gets a Settings toggle back to the plain look** (plants, water, glass, roofs, sun shading,
   terrain and cloud shadows so far).
6. **Windows/MSVC gotchas:** windows.h macros (`near`, `far`, `FAR`, `small`, `min`, `max`) break code that comes after
   them; never include `<windows.h>` from a header, keep Windows calls in their own .cpp (audio/AudioEngine.cpp,
   input/Rumble.cpp). A MinGW cross-compile (`x86_64-w64-mingw32-g++ -fsyntax-only`) catches most of these here.
7. **The UI font:** `' ( ) + & ? ;` were added by `tools/gen_font_glyphs.py`; `\` and `^` are still blank. Check any
   other symbol on screen before using it in a label.
8. **Say who changed what:** a session's work gets a CHANGELOG.md entry naming it (e.g. "docs pass (Claude, date)").
9. **Logs are pushed:** `logs/` is tracked by git so agents can read Eddie's logs and crash reports; don't
   ignore it. Only `logs/*.dmp` (big binary dumps) and `screenshots/` stay out.

## In progress (more than one agent may be working; mark what you take)
- **Claude (2026-11-25): Build mode (ROADMAP "Build mode")**: steps 1-4 and 7-9 done (plans, the Build tab, the
  build camera, foundations, walls and rooms, paint, heights, pillars, snapping, frames with doors and windows,
  roofs, storeys with per-level wall height, straight / L / U / spiral stairs, fences, automatic railings,
  walls up / down in the build camera). Next: curves and round towers with cone roofs (5), smooth angled walls (6),
  arched frames, the build camera on a pad.
  Touches game/BuildPlan.h, world/PlanManager.h, BuildTool, PlayerInteraction, BuildMenuScreen, main.cpp.
- Done 2026-11-25 (Claude): spell groundwork (CHANGELOG): `data/registry/spells.toml`, `game/Spells.h`,
  `sim/SpellSystem.h`, anim_map `[casts]`, 17 spells with books. Next: the village island (phase 4) built with
  spell progression in mind; mana, staves, enchanted weapons and the skill tree on the same data.
- Done 2026-11-25 (Claude): the grapple and the Hywirl flier, Mushroom Islands phases 2-3 (CHANGELOG): `game/Grapple.h`,
  `VoxelPhysics::grappleTick / flierTick`, `Simulation::grappleOk`, `Combat::flyerFight`. Next: others' ropes over the
  network, grapple points along the rim road, the village island (phase 4).
- Done 2026-11-26 (Claude): the grapple is equipped like a spellbook (ability slot / quick option / off hand; no G key)
  and aims itself at the point or enemy nearest the view in reach (`grapple::aim`, `scanPoints`, `CombatKit::grappleReady`).
- Done 2026-11-25 (Claude): Mushroom Islands phase 1 (CHANGELOG): the sky world type (`worldgen/SkyGenerator.h`,
  `data/worlds/mushroom_islands.toml`), the coarse far view (`render/SkyFarView.h`), stepping stones, the bowl,
  glowing mushrooms, bonfire and spawn points, the King at the boarded mine (`GameSession::tickSky`). Next: a portal
  from the main world, phase 2 (grapple) per ROADMAP, rest/spawn for server clients.
- Done 2026-11-22 (Claude): mushroom monsters (Mushnub, Spiny Mushnub, Mushroom King) as monster models with
  their own clips, drawn in the characters' toon style; the Hit physics setting; the second quit crash (CHANGELOG).
  Next on them: the King's Wave / Yes / No clips as spells, placing him (he only comes from `spawn` now), his
  swings shown on server clients.
- Done 2026-11-21 (Claude): combat review part 1, hit reactions (lean spring, reaction clips, smaller ragdoll
  wobble, get-up off the back; CHANGELOG). Next in the combat review: ROADMAP "Next" 1. Minor visual issues can
  wait (Eddie). ROADMAP "Open issues" has the server portal bugs.
- **Eddie's logs are in the repo:** `logs/game.txt`, `logs/game-previous.txt`, `logs/server.txt`, `logs/crash-*.txt`
  are pushed with his commits. Read them when he reports a problem (newest crash report first).
- Done 2026-11-21 (Claude): logs to `logs/`, crash reports, F2 screenshots, the crash on quit fixed (CHANGELOG).
- Last sessions (2026-11-21):
  - Claude, docs-only pass: this file trimmed (history moved to docs/handoff-history.md), the friends' readme
    rewritten for the current controls and what works on servers, stale lines fixed in AGENTS.md, ROADMAP.md and
    BUILDING.txt. No code changed. CHANGELOG.md has the entry.
  - Before that (Claude): servers slices 1-3 and phase 3 (realm, gate menu, seamless gates, chests, parries at the
    defender's time); pad layout; rendering second pass (occlusion queries, render scale, far foliage, unseen worlds at
    half rate); light from the land (`render/TerrainLight.h`, `shaders/terrainlight.glsl`); portal views in the far
    world's light. CHANGELOG.md has each.
- When you finish something: an entry at the top of CHANGELOG.md (say who did it), take it off ROADMAP.md, update
  this file's Current state, deliver a zip.

## Current state (2026-11-20): a realm on the dedicated server

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

## Testing here (no screen)

- **Preferred: scripts.** `VOXELRPG_SCRIPT=<file>` with `world <name>` / `newworld <name> <seed>`, `pos`, `look`, `wait`,
  `walk`, `shot <file>`, `dbg` (draws, hidden sections, combat, gate in reach / menu), `cmd <console command>` (e.g.
  `cmd time 17`, `cmd mode survival`, `cmd portal open crypt`, `cmd dungeon tp portal`), `atportal dx dy dz`,
  `pad <buttons>` with `VOXELRPG_FAKE_PAD=<file>` (a pretend controller), `join <address> <name>`. Run under
  `MESA_GL_VERSION_OVERRIDE=4.6 MESA_GLSL_VERSION_OVERRIDE=460 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x720x24"`
  (about 1 fps). Keep `chunk_distance` low in the scratch copy's data/settings.toml for shots from high up.
- Driving the game by hand (tools/xin.py) is slower and flakier; notes below are for when a script can't do it.

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
