VoxelRPG - the server
=====================

Runs a world for others to join. No graphics; it's a text window.

Start
-----
Windows:  double-click Start-Server.bat
Linux:    ./start-server.sh

The first start makes server.toml (the settings) and, if there's no world
yet, a new flat one in saves/server/. Then it says "Ready: port 27015".
Windows Firewall will ask once: allow it on private networks (and public,
if friends join over the internet).

While it runs, type into its window:
  status        who's on, and their ping
  save          save now
  stop          save and quit          (closing with Ctrl+C does the same)
  quit-nosave   quit, keeping the world as it was at the last save

Letting friends in
------------------
Same house / same Wi-Fi: they type this computer's local address, e.g.
  192.168.1.23        (find it with:  ipconfig   -> "IPv4 Address")
Over the internet: in your router, forward UDP port 27015 to this
computer, and give them your public address (search "what is my ip").
Everyone needs the client from the same build as this server (version.txt).

Settings (server.toml)
----------------------
  world = "server"          the folder in saves/ to run
  dev_world = false         true: look in devworlds/ instead
  port = 27015
  max_players = 20
  checkpoint_seconds = 300  autosave into a staging folder; the world's
                            files are only replaced on stop / save
  starter_kit = [...]       items a new player starts with
  new_world = "flat"        made if 'world' isn't there: flat, classic,
                            island (takes a minute to shape), or none
  new_world_seed = 0        0: a new seed

Using a world you made in the game: copy its folder from the game's
saves/ (or devworlds/) into the same place here, and set 'world' to its name.
Or run the server from the game's own folder.

Players are saved by name in saves/<world>/players/. The last three saves
are kept beside the world as .backup-1 to .backup-3.
