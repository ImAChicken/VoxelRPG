# Dev worlds

Stages for showcases and tests: enemies, spawners, items, builds.
Each folder here is a world, like those in `saves/`, with two
differences:

- **It never saves by itself.** Esc → **Save** keeps what's there;
  **Quit without saving** (or closing the window) throws the visit
  away. So you can stage a scene once and come back to it as it was.
- **It can be copied.** New World → Start: **Copy a dev world** makes
  a new world from one (into `saves/`, or into here with Save in:
  Dev worlds), with everything in it: ground, structures, spawners,
  NPCs, your inventory and where you stand.

Make one with New World → Save in: **Dev worlds** (a Flat world is a
good base). Open one from Load World → Dev worlds.

## Setting out items

New World → **Kit: Showcase chests** (on by default for a new dev
world) puts a row of labelled chests 8 ft in front of where you start:
Weapons, Armour, Tools, Blocks, Decor, one of everything. They're
**showcases**: taking from one gives you a copy, so they never run out
(Delete over a slot takes it off the shelf; putting things in works as
usual; broken, they spill nothing). Every chest panel also has
**Take all** and **Store all** (your pack, not the hotbar or what you
wear).

Console (`), aiming at a chest:

    chest                         what's in it
    chest fill weapons armour     one of each (also tools blocks decor dev all, or item names)
    chest clear
    chest name Swords             its title, shown when you aim at it
    chest showcase [on|off]       take a copy, or take the thing
    chest loot bandit_stash       roll a loot table when next opened (loot lists them)
    chest kit                     another row of showcase chests in front of you
    place bandit_stash            a chest that rolls its loot table

NPCs carry packs too (npcs.toml `pack` / `loot`): `spawn bandit`, kill it
(`smite`), and E on the body searches it.

Commit the ones worth sharing; they're small (only edited chunks are
stored).
