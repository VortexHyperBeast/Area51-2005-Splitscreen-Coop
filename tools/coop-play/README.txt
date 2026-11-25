AREA 51 (2005) - 2 PLAYER SPLIT-SCREEN CO-OP, STAGE 1 (EXPERIMENTAL)
=====================================================================

What this is
------------
The Dreamland-51 PC port of Area 51, with an experiment added: two players in the same
campaign level on one PC, one controller each, top and bottom split screen.
It is Stage 1 of a longer plan, see "Known gaps" below.

Your original Area 51 install is not used and not touched. Everything lives in this folder.


How to start
------------
1. Extract the whole zip (right click, Extract All) into a NEW folder, for example
   C:\Games\Area51-Coop. Do not use your original install folder.
2. Plug in both Xbox controllers.
   (The game draws with Vulkan, not DirectX. Every current NVIDIA, AMD and Intel graphics
   driver includes it. If the game stops right at the start with a Vulkan message, update
   the graphics driver.)
3. Double-click  PLAY-COOP-2-PLAYERS.cmd

   First start only: it asks "Download it now?" before it downloads the game data
   (about 1.5 GB, about 4 GB on disk). That data is the open "Dreamland-51 Asset Pack" of the
   port project. Type Y and press Enter. It checks the file against the fingerprint this build
   was tested with. This needs an internet connection and takes some minutes.

   Windows may show a blue box "Windows protected your PC" because the files are new and
   unsigned. Click "More info", then "Run anyway". The program was built from the source code
   in the GitHub repository this zip came from.

4. The main menu has two campaign entries:
     SINGLE PLAYER       the normal one-player campaign, with your own profiles and saves.
     SPLITSCREEN CO-OP   the two-player campaign. It has its own saves, made the first time:
                         "Splitscreen P1" (the co-op progress and player 1's settings) and
                         "Splitscreen P2" (player 2's settings and body). They are never used
                         by single player, and deleting a single-player profile does not
                         touch them. Profiles named "Player 2" or "Splitscreen" from older
                         test builds are no longer used and can be deleted.
   Both start files open the same menu; you choose the mode there.
   In Splitscreen Co-op the campaign menu also has LEVEL SELECT: it starts any level of the
   campaign directly (for example to try a later part of the story without a save there).
   Both controllers are used. The first controller is player 1 (it also controls the menus);
   the second is player 2. The keyboard and mouse also drive player 1.
   If they come out the wrong way round, unplug both and plug in the controller you want
   as player 1 first.

PLAY-SINGLE-PLAYER.cmd starts the same program as a normal one player game, to compare.

CO-OP SETTINGS IN THE GAME: press Start (pause). Under Options/Settings there are three rows:
  Controllers: normal / swapped   - which controller plays the top and the bottom player
  P1 body / P2 body               - press A to go through the 16 multiplayer characters
                                    (hazmat, spec ops, technician, grey; 4 colours each)
They take effect at once and are saved. With the controllers swapped, the keyboard and mouse
still belong to the first controller's slot, i.e. they move player 2.

Each player has their own settings: Options in the pause menu edits the settings of the player
whose controller pressed Start (sensitivity, invert, vibration, ...). Player 2's settings live in the
profile "Splitscreen P2" that the game creates the first time. Everything is saved when you
change it and again when the game closes.

DOWNED AND REVIVE: when a player loses all health while the other player is still up, he goes
down instead of dying: his body falls and lies on the floor and his view drops to the floor. He can look around but cannot move, shoot, throw grenades, melee or use mutant powers, and enemies leave him alone.
The other player walks up to him and holds USE (X on an Xbox controller) for 4 seconds: he gets
up with 35% health. If nobody helps him within 25 seconds, he dies and the checkpoint restarts.
If both players are down, the checkpoint restarts. To change the rules, add lines to
a51_coop.cfg (in this folder) such as:
  bleedout_seconds=25
  revive_seconds=4
  revive_health_percent=35
  revive_range_cm=200
  downed=0          (turns it off: any death restarts the checkpoint, as before)
  mutant_damage_percent=50   (a mutated player takes this percentage of the damage; 100 = as normal)
  face_brightness_percent=55 (how bright the players' faces are drawn; 100 = as the multiplayer body draws them)

If the game crashes, it writes a51_crash.txt and a51_crash.dmp in this folder. Please send both.


What you should see
-------------------
- The screen split into a top and a bottom view, one HUD each.
- Player 2 starts about a metre beside player 1 in the same world.
- Each player sees the other one as a person in a hazmat suit (the multiplayer body).
- Controller 1 moves, looks, shoots, reloads and uses things for the top player only,
  controller 2 for the bottom player only. Try one at a time, then both together.
- If either player dies, the level restarts from the last checkpoint (temporary rule).


Known gaps (so they are not a surprise)
---------------------------------------
- Enemies may only react to one of the two players. Making them aim at both is the next stage.
- Cutscenes and scenes that take control away are shared: both players get them, the other
  player is moved next to whoever started it, and the screen shows one full-screen view
  while the scene runs.
- Items and pickups are shared: what one player gets (weapons, scanner, ammo, health),
  the other gets too.
- Checkpoints and level changes are not made two-player aware yet.
- There is no menu entry for co-op; that is why the start file sets it up.
- It is the first time this runs on a real Windows PC with real controllers. Expect rough edges.


If something goes wrong
-----------------------
Please send me:
 - what you did and what happened (a phone photo of the screen is fine),
 - the files  a51_crash.txt  and  a51_crash.dmp  if the game crashed,
 - the file  a51_local_coop_probe.log  from this folder, if there is one,
 - the file  TEST-REPORT.txt  is only for reference, you do not need to send it.

The game data and saves are in this folder. To start over, delete the folder.

This is an unofficial, non-commercial test build for personal use. It contains no game data
and has no licence from the owners of Area 51; please do not redistribute it.
