# Area 51 (2005) local co-op: status and TODO

Branch `coop/stage1` of `VortexHyperBeast/area51-pc`. Last updated 2026-11-24 (after build 27; shared-inventory decision; AI audit).

Every status below is backed by one of three kinds of evidence, and says which:

- **Bot**: the automated cloud test. A Linux test build with a bot that drives two virtual
  controllers, judged from the game's own log by `tools/coop-qa/coopqa.py`. Software rendering,
  silent audio driver, mostly level 1 (`dreamlnd`), plus report-only runs of the start of level 2.
- **Owner**: played by the owner on Windows 11, RTX 4070, two Xbox controllers.
- **Code**: read in the source only.

A passing bot test is not a played game. Statuses:
**DONE+VERIFIED** (bot and/or owner),
**IMPLEMENTED, NOT FULLY TESTED**,
**PARTIAL**,
**NOT IMPLEMENTED**,
**BROKEN / INVESTIGATE**.

## 1. Feature audit

### Core co-op (Stage 1)

| Feature | Status | Evidence | Files |
|---|---|---|---|
| Second local player in the campaign, created beside player 1 | DONE+VERIFIED | Bot every run; owner | `logic_Campaign.cpp` (`BeginGame`, `CreateExtraPlayer`), `GameMgr.cpp`, `NetLimits.hpp`, `LocalCoop.hpp` |
| One controller per player, independent input | DONE+VERIFIED | Bot (walk/look/jump/fire/reload per pad); owner | `GameServerStates.cpp`, `PlayerObject.cpp` (`SetLocalPlayer`) |
| Horizontal split screen, one camera and HUD each | DONE+VERIFIED | Bot (viewports, eye positions, screenshot divider); owner | `Apps/GameApp/main.cpp` (render loop) |
| Players see each other (multiplayer avatar body) | DONE+VERIFIED | Bot (`partner_body`); owner | `PlayerObject.cpp` (`PrepLocalCoopAvatar`), `PlayerMovement.cpp` (`UsingLoco`) |
| Body animation for campaign-only weapons (scanner crash) | DONE+VERIFIED | Owner crash report fixed; bot fires the scanner; asserts now fail the test | `PlayerLoco.cpp` (`SetWeapon`) |
| Bloom streaks in the bottom view | IMPLEMENTED, NOT FULLY TESTED | Owner saw no streaks after the fix, but did not confirm explicitly | `BloomEffect.cpp` |
| Bodies collide (players can push each other) | IMPLEMENTED, NOT FULLY TESTED | Bot (excused in the walk check) | follows from the avatar |

### Settings and profiles

| Feature | Status | Evidence | Files |
|---|---|---|---|
| Player 2's own profile ("Splitscreen P2"), own options (sensitivity, invert, vibration...) | IMPLEMENTED, NOT FULLY TESTED | Bot (`own_profiles`); owner asked for it, has not confirmed | `StateMgr.cpp` (`CoopPrepareSecondProfile`, `SetupProfileListIndex`), `SaveDataMgr.cpp` |
| Pause > Options edits the profile of the player who paused | IMPLEMENTED, NOT FULLY TESTED | Code | `StateMgr.cpp`, `dlg_PauseMain.cpp` |
| Profiles saved on change, on leaving the campaign, at shutdown | IMPLEMENTED, NOT FULLY TESTED | Code (create/load bot-tested, the save at shutdown is not) | `StateMgr.cpp` (`CoopSaveProfilesNow`), `main.cpp` (`DoShutdown`) |
| Controller swap in the pause menu, kept in `a51_coop.cfg` | DONE+VERIFIED | Bot (`live_swap`, swap run); owner used it | `StateMgr.cpp` (`CoopSetSwapPads`), `dlg_PauseMain.cpp`, `LocalCoop.hpp` |
| Character (16 multiplayer skins) per player in the pause menu, with picture | IMPLEMENTED, NOT FULLY TESTED | Owner: "working well" for skins; picture not confirmed | `dlg_PauseMain.cpp`, `PlayerObject.cpp` (`CoopApplySkin`) |
| Pause menu layout: highlight bar follows the co-op rows, rows fit inside the frame, no build number/date | DONE+VERIFIED | Owner (build 41): the highlight follows the controller and body rows | `dlg_PauseMain.cpp` |
| Start screen says "Splitscreen co-op" (the owner's picture, `SCREENS/UI_COOP_WARNINGSCREEN.XBMP` in the zip; the original screen if the file is missing) | DONE+VERIFIED | Owner (build 48): the picture shows. Builds 41-47: the picture never loaded (black, then the original screen). Cause found with the probe: the play build's file system reads only the packed archives, loose files are not passed through. Now read with stdio and made into a texture directly | `StateMgr.cpp` (`EnterStartupInfo`), workflow, `tools/coop-play/SCREENS` |

### Shared world, scenes and items

| Feature | Status | Evidence | Files |
|---|---|---|---|
| Cutscenes and scripted no-control scenes for both players | DONE+VERIFIED | Bot (`shared_cinema`); owner | `PlayerProperties.cpp` (mirror), `logic_Campaign.cpp` (`SyncCoopCinema`) |
| Full-screen view during scenes | DONE+VERIFIED | Bot; owner | `main.cpp` |
| The other player joins after a scene (checked free spot, after the end marker) | IMPLEMENTED, NOT FULLY TESTED | Code; owner's fall-off case not re-tested | `PlayerProperties.cpp` (`CoopMoveNextTo`, `CoopUpdate`) |
| Teleport / regroup button (Back), fall rescue | DONE (bot) | Bot (`join_button`). The brief says not to work on these further | `PlayerProperties.cpp` (`CoopUpdate`) |
| Look-and-use prompts (focus objects) for either player | DONE+VERIFIED | Owner (scanner pickup) | `FocusObject.cpp`, `condition_player_button_state.cpp` |
| Items: scripted gives and all pickups shared (weapons, ammo, grenades, equipment unlocks) | IMPLEMENTED, NOT FULLY TESTED. **Owner's decision (2026-11-24): keep it shared, do not change** | Bot (`shared_items`) | `PlayerInventory.cpp` |
| Inventory carried across level changes and checkpoints for both | IMPLEMENTED, NOT FULLY TESTED | Bot (store/restore only, `carry_over`); real level change not re-tested | `StateMgr.cpp` (`Backup/RestorePlayerInventory`), `CheckPointMgr.cpp` |
| "The player" of scripts is always player 1 | IMPLEMENTED, NOT FULLY TESTED | Code (cloud object order already put player 1 first) | `SimpleUtils.cpp` (`SMP_UTIL_GetActivePlayer`) |
| Elevator cutscene voices (level 2 opening) | DONE+VERIFIED | Owner (build 28): sound in the cutscene works. Cause: the track became 3D when "the player" was player 2 | `SimpleUtils.cpp`, `Cinema.cpp` |
| Crash on level restart while a cinema runs | DONE+VERIFIED | Owner (build 28): no crash when either player dies; bot kills player 1 and the restart passes | `Global_Variables_Manager.cpp` |

### Gameplay systems in the brief

| Feature | Status | Evidence / notes | Files |
|---|---|---|---|
| Independent health, damage, death | IMPLEMENTED, NOT FULLY TESTED | Separate health per player. Lethal damage puts a player down while the partner is up (see the downed row). The checkpoint restarts when both are down, or when a downed player bleeds out | `Actor.cpp` (`TakeDamage`), `PlayerProperties.cpp`, `logic_Campaign.cpp` (`PlayerDied`) |
| Independent weapon use: switching, firing, reloading, current clip | DONE+VERIFIED | Bot: fire/reload per player | - |
| Inventory rules (owner's decision 2026-11-24) | DECIDED | Weapons, ammunition, grenades and equipment are **shared**: what one player gets, the other gets too, and this carries across levels. Health and damage stay **individual**. No change planned | `PlayerInventory.cpp` |
| Enemies perceive and target both players | IMPLEMENTED, NOT FULLY TESTED | Bot, level 1010 (build 31): 20 enemies, 8 of them picked player 2; target picks player 1: 42, player 2: 38. Changes in build 31: turn on the player who hits, threat memory, 2 melee slots per player, Theta and globs use the nearest player. Owner has not played it; bosses not reached by the bot | `Character.cpp`, `God.cpp`, `MutantTank_Attack_State.cpp`, `Mutant_Tank.cpp`, `AlienGlob.cpp`, `SimpleUtils.cpp` |
| Downed state, revive, bleedout | IMPLEMENTED, PARTLY VERIFIED (owner, build 41: lying body and revive work; bleedout and enemies ignoring a downed player not confirmed) | Build 35: lethal damage while the partner is up puts the player down: look only, no damage, enemies ignore him. Build 41: every button is dropped while down (no shooting, grenades, melee, mutation or Use; a shot or throw in progress is cancelled), bot check `downed_no_attack`. Partner holds Use (X) within 2 m for 4 s: up with 35% health. 25 s bleedout, then death and checkpoint. Both down: checkpoint. Rules in `a51_coop.cfg` (`downed`, `bleedout_seconds`, `revive_seconds`, `revive_health_percent`, `revive_range_cm`). Bot (build 35, normal and swapped pads): player 2 went down from lethal damage, was revived by player 1 holding X, went down again; then player 1 died and the checkpoint restarted. Bleedout timer not bot-tested; owner has not played it. Body: the limp body (ragdoll) a killed multiplayer player leaves lies on the floor while the live body is hidden (the avatar has no death animation; build 39 showed it). Bot, build 41: the lying body was made 2 of 2 times, no assertions; the view drops to the floor (owner request, existing animation reused, nothing new made). Not seen by the owner yet | `PlayerProperties.cpp`, `PlayerInput.cpp`, `PlayerStatus.cpp`, `Actor.cpp`, `Character.cpp` (`IsValidTarget`), `LocalCoop.hpp`, `CheckPointMgr.cpp` |
| Mutant abilities for both players | IMPLEMENTED, PARTLY VERIFIED (owner, build 54: no issues with mutations) | Per-player mutation, mutagen and mutant vision (player members). The global slow-motion of the single-player mutation is off with two local players (the engine's own split-screen rule), so one player's mutation never slows the other. Scripted mutagen refills reach both players; scripts asking "is the player mutated" count either player. Bot (build 52, normal and swapped pads): player 2 mutated alone and turned back, player 1 stayed human, world at 100%, no assertions. Owner has not played a mutation level | `PlayerMutation.cpp`, `PlayerProperties.cpp`, `PerceptionMgr.cpp:57` |
| Mutated players take 50% damage in co-op (owner request; `mutant_damage_percent` in a51_coop.cfg) | DONE+VERIFIED | Owner (build 55): a mutated player takes less damage. Applied after the game's own difficulty and mutant pain settings, players only, co-op only | `Actor.cpp` (`TakeDamage`), `LocalCoop.hpp` |
| Player 2's mutant attacks (melee, primary, secondary, toggle) follow player 1's | IMPLEMENTED, NOT FULLY TESTED | Owner (build 57): player 2 could mutate but not attack until a death; the checkpoint restore gave both players the abilities, the level start only player 1. Now copied every frame; bot `mutation_independent` compares them | `PlayerProperties.cpp` (`CoopUpdate`) |
| Mutated players show the mutant body (the multiplayer avatar's own mutant meshes) | IMPLEMENTED, NOT FULLY TESTED | Owner request. The engine swaps the meshes only in multiplayer; co-op players now too. Bot `mutation_independent` checks the mesh mask | `Actor.cpp` (`SetSkinVMesh`) |
| Revive at the body, not where the player fell | IMPLEMENTED, NOT FULLY TESTED | Owner (build 57): an explosion threw the body away from the revive spot. Revive range, HUD distance and the stand-up spot now use the body | `PlayerProperties.cpp` (`CoopDownSpot`, `CoopRevive`), `HudObject.cpp` |
| Boss fights with two players | PARTLY VERIFIED | Owner (build 55): the Theta fight works, but his close melee did no damage (his leap did). Fix in build 57+: close melee prefers a player among the hits and also hits a co-op player standing in the swept sphere; not yet confirmed | `Character.cpp` (`EmitMeleePain`), `MutantTank_*` |
| Checkpoint restore of both players | PARTIAL | Restore gives both players the inventory; positions after respawn not checked | `CheckPointMgr.cpp` |
| Saving/loading a campaign with two players | NOT TESTED | Campaign progress lives in player 1's profile (slot 0) by design | `CheckPointMgr.cpp`, `StateMgr.cpp` |
| HUD: per-player health/ammo/crosshair | DONE+VERIFIED | The engine's split-screen HUD; owner | `HudObject.cpp` |
| HUD: downed and revive indicators | IMPLEMENTED, NOT FULLY TESTED | Text in each view: "YOU ARE DOWN" with the seconds left or the revive percentage; "YOUR PARTNER IS DOWN" with the distance, seconds left and the revive percentage | `HudObject.cpp` (`CoopRenderDowned`) |
| Main menu: "Single Player" and "Splitscreen Co-op" (own saves: profiles "Splitscreen P1" with the progress, "Splitscreen P2") | DONE+VERIFIED | Owner (build 49): menu entries and the separate Splitscreen P1/P2 saves work. The bot starts the campaign without the menu | `dlg_MainMenu.cpp`, `StateMgr.cpp` (`CoopPrepareSplitscreenProfile`), `LocalCoop.hpp` (`LocalCoop_SetPlayerCount`) |
| Level Select in the Splitscreen Co-op campaign menu (the developers' level select, compiled into the play build for co-op only) | IMPLEMENTED, NOT FULLY TESTED | Owner request: start later parts of the story without a save. The bot already starts level 1010 through the same level select path | `dlg_CampaignMenu.cpp`, `dlg_LevelSelect.cpp`, `StateMgr.cpp` |
| Performance with two views | NOT MEASURED on a real GPU | Owner reports nothing bad | - |

### Delivery and diagnostics

| Feature | Status | Files |
|---|---|---|
| Windows build, package, setup script, launchers, GitHub release per green run | DONE+VERIFIED | `.github/workflows/coop-build.yml`, `tools/coop-play/*` |
| Crash report (`a51_crash.txt`/`.dmp`) and the PDB with each release | DONE+VERIFIED (two crashes solved with it) | `GameAppPlatform_Windows.cpp`, workflow |
| Co-op probe log: players, cinema sound, dialog lines, voices | DONE | `logic_Campaign.cpp`, `Cinema.cpp`, `ConversationMgr.cpp` |
| Bot test: 30+ checks, a self-test with 30+ injected faults, a single-player control twin, a swapped-pads run, a death and restart, second-level sound reports | DONE | `tools/coop-qa/*` |

## 2. Campaign compatibility (the 19 campaign maps of the level list; names from `s_MapToWeaponTable` in the same order, an assumption)

"Bot" covers about 2.5 minutes of scripted play from the level start. Nothing below is marked done just because the level loads.

| ID | Map | Two players | Enemies | Objectives | Cutscenes | Dialogue | Pickups | Checkpoints | Transition | Known bugs | Manual test |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1000 | dreamlnd | bot+owner | n/a (none early) | owner: progressed to the elevator | bot+owner | owner | owner (scanner) | bot (death restart) | owner: elevator to 1010 | - | owner, whole level |
| 1010 | undrgrnd | owner (part) | owner (part), not verified for player 2 | ? | owner: elevator voices OK (build 28) | bot: same lines as single | ? | owner: death restart OK for both players (build 28) | ? | fall after cutscene (fix pending) | owner, part |
| 1020-1130 | hotzone ... exit (17 maps) | not tested | not tested | not tested | not tested | not tested | not tested | not tested | not tested | unknown | no |

## 3. Next milestone: Stage 2, co-op enemy AI

Audit result: target selection (`character::ChoseBestTarget`, `Character.cpp`) already scores every
hostile actor, so player 2 can be chosen. The gaps are specific:

| Spot | Problem | Planned change (co-op only) |
|---|---|---|
| `Character.cpp` damage handling | Switches to the attacker only if it is closer than the current target | Remember the last attacker for a few seconds and weight it in `ChoseBestTarget` |
| `God.cpp` melee slots | Two melee slots for all players together | Two slots per player |
| `MutantTank_Attack_State.cpp` stage rage | Aims at "the player" (player 1) | Nearest player |
| `Mutant_Tank.cpp` `ScanForPlayerProjectile` | Watches player 1's projectiles only | Both players |
| `AlienGlob.cpp` default target | First player object | Nearest player |
| `Character.cpp`, `Character_Cover_State.cpp` | Possible null target reads | Null checks |

Helpers `SMP_UTIL_GetNearestPlayer(pos)` and `SMP_UTIL_IsPlayerGuid(guid)` in `SimpleUtils.cpp`.

1. Log per enemy (test build): guid, type, target and which player, state, distance to each player.
   Bot run on level 1010 (enemies at the start).
2. Make the changes above, all behind `LocalCoop_IsActive()`, so single player is unchanged.
3. Bot checks: an enemy targets player 2; an enemy hit by the other player turns to it; both players
   get targeted during the run.
4. Downed/revive: implemented (see section 1). Mutants: first pass (see section 1). Next: bosses.

## 4. Biggest risks

- About 85 "the player" lookups across the game: triggers, AI, cinemas, lore, alien platforms.
  Each is a possible co-op progression bug that only shows in that level.
- The bot plays only the opening minutes of a level. Most of the campaign needs the owner's play
  sessions, or bot routes that reach later sections.
- Retail builds turn `ASSERT(x)` into `__assume(x)`: an assertion that is harmless in the test build
  can crash the owner's build. Co-op runs now fail on any assertion during play.
- No licence on the original code: the fork is for personal, non-commercial testing.
