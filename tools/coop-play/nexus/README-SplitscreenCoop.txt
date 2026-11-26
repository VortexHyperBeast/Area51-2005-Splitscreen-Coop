AREA 51 (2005) - SPLITSCREEN CO-OP  (mod for the Dreamland-51 PC port)
======================================================================
Version: see the top-left corner of the main menu.

Two players, one PC, one controller each: the whole campaign in top/bottom split screen.


REQUIREMENTS
------------
- The Dreamland-51 PC port of Area 51, installed and working (it runs the game and provides
  the game data). This mod contains no game data.
- Two controllers (Xbox controllers recommended).
- A graphics driver with Vulkan (every current NVIDIA, AMD and Intel driver has it).


INSTALL
-------
1. Back up your Dreamland-51 folder's A51.exe and SHADERS folder if you want to undo later.
2. Drag everything from this zip into your Dreamland-51 game folder (the folder with A51.exe).
   Allow Windows to replace the files.
3. Start A51.exe as usual. Nothing else is needed: no extra launcher, no command window.

To uninstall: put your backed-up A51.exe and SHADERS back, and delete the SCREENS folder,
README-SplitscreenCoop.txt and (if it exists) a51_coop.cfg.


PLAYING
-------
The main menu has two campaign entries:
  SINGLE PLAYER       the normal campaign, with its own profiles and saves.
  SPLITSCREEN CO-OP   the two-player campaign, with its own saves ("Splitscreen P1" and
                      "Splitscreen P2"), separate from single player.
In Splitscreen Co-op, Level Select lets you start any mission of the campaign.

Pause menu > Splitscreen Co-op: swap the controllers, and choose each player's body.

Co-op rules:
- A player whose health runs out goes down instead of dying. The partner revives them by
  standing next to the body and holding USE (X on an Xbox controller) for 4 seconds; the
  downed player gets up with 35% health. If nobody helps within 25 seconds, or both players
  are down, the game goes back to the last checkpoint.
- A downed player cannot shoot, throw grenades or melee.
- Mutation works for both players. A mutated player takes half damage.
- Weapons, ammo, grenades and unlocks are shared; health is separate.
- Enemies and bosses take 1.5 times as many hits as in single player, to balance two players.


SETTINGS (optional)
-------------------
Create a text file named a51_coop.cfg next to A51.exe to change the rules, one per line:
  swap_pads=1                  swap the two controllers (also in the pause menu)
  downed=0                     no downed state: any death restarts the checkpoint
  bleedout_seconds=25          time a downed player can wait for a revive
  revive_seconds=4             how long the revive takes
  revive_health_percent=35     health after a revive
  mutant_damage_percent=50     damage a mutated player takes (100 = normal)
  enemy_health_percent=150     how long enemies and bosses last (100 = as in single player)


IF SOMETHING GOES WRONG
-----------------------
The game writes these files next to A51.exe; please include them in a bug report:
  a51_local_coop_probe.log   (what happened during the session)
  a51_crash.txt, a51_crash.dmp  (only after a crash)


CREDITS
-------
Splitscreen co-op mod by VortexHyperBeast.
Built on the Dreamland-51 PC port of Area 51 (an independent, non-commercial community
project). Area 51 is a game by Inevitable Entertainment, published by Midway Games; the game,
its name and assets belong to their respective rights holders. This mod is free and
unofficial and is not affiliated with or endorsed by them.
