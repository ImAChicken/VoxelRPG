VoxelRPG - the game
===================
A test build for friends. Things will break: telling us what broke is the
most useful thing you can do (see "Reporting a problem" at the end).

1. Unzip this folder anywhere (Desktop is fine). Keep the files together.
2. Double-click VoxelRPG.exe. A text window opens beside the game: leave
   it open, it's the game's log.
   Windows may say "Windows protected your PC" the first time, because the
   game isn't signed: click "More info", then "Run anyway".

Playing alone
-------------
Main menu -> New World. Everything works there.

Playing on a server
-------------------
Main menu -> Join Server
  Address  what the host gave you, e.g. 203.0.113.5 or 203.0.113.5:27015
           (leave it empty only if the server runs on this same computer)
  Name     your character on that server. Use the same name next time to
           get your character back. Letters, digits, - and _ only.

If it says "version mismatch", you and the host have different builds:
get the newest zip from the same place the host did.
If it says "no answer", the address is wrong, the server isn't running, or
the host's router isn't letting you in (that's for the host to fix).

On a server you can walk, fight, build with the Builder's Hammer, use
tools, open chests, and go through the dungeon gate together. Not on a
server yet: talking to villagers, searching bodies, and the dev tools.

Things to try
-------------
- The gate near the start: interact with it to open a dungeon (the
  Undead Crypt), walk through, and find the boss. The dungeon closes
  after a while; its clock is on screen.
- The Builder's Hammer: tap the right mouse button to pick a piece,
  left click to place it, hold the right button on a piece to take it
  down. The menu's Palettes tab changes what walls are made of.
- Villages: talk to the villagers (E), and see who's out at night.

Keys
----
W A S D  move          Space  jump        Ctrl  crouch
Shift    hold: sprint, tap: roll          F     grab a ledge, again to climb
Mouse    look          Left   attack / use      Right  second action
R        draw / put away weapon           X     swap weapon set
Q        hold: guard, tap as a hit lands: parry
Tab      lock on       E      interact    I     inventory
1 2      off-hand slots    3-0 / wheel  hotbar
In combat: 1 2 3 use your abilities, 4 5 6 the next one in each
           slot, hold Alt and press 1-8 for quick spells
F5       camera        Esc    menu
Change them in Esc -> Controls.

Controller
----------
Any Xbox / PlayStation / Switch style pad. Buttons are named by where they
sit (the game shows pictures), so A is the bottom face button on any pad.
Always:       Left stick move      Right stick look     A  jump
              LB  hold: sprint, tap: roll                L3 crouch
              R3  lock on          Select  draw / put away weapon
              Select + Start   inventory and the menu (Save, Settings, Quit ...)
              Hold Start   quick spells (D-pad / face buttons); Start + LB: interact
Out of combat: LT / RT  use / break
              D-pad left / right  hotbar, up  left hand
              X  the tool's option (turn the hammer's piece)
              With the hammer: B cancels, hold Y and use the D-pad to move
              the piece (RB / B up and down)
In combat:    RB light  RT heavy  LT guard / off hand
              X Y B  abilities   D-pad  next ability, down: next weapon set
In menus: D-pad / stick moves, A picks (hold A and move to drag), B goes back.
The bottom right of the screen shows what each button does right now
(Settings -> Control hints turns that off). Typing (world and server names)
needs the keyboard. Change buttons in the menu -> Controls -> Controller.

Slow computer?
--------------
Esc -> Settings: lower the chunk and render distance, set Render scale to
Auto or 70%, and turn off the shader options (roofs, glass, plants, water,
terrain shadows) to see if one of them is the problem.

Known problems (no need to report these)
----------------------------------------
- On a server, "Close" in the gate's menu doesn't close the dungeon.
- Turning the camera fast near a gate on a server can show flickering
  bits of the world for a moment.
- F2 doesn't take a screenshot yet: use Windows' own (Win + Shift + S).

Reporting a problem
-------------------
Send the host (or whoever gave you the game):
- what you were doing, and what happened instead
- a screenshot (Win + Shift + S) if it's something you can see
- the last lines of the text window that opened with the game (select
  them and press Enter to copy, or take a screenshot of it)
- the contents of version.txt, which says which build this is
