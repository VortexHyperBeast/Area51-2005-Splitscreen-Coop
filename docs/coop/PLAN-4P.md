# 4-player split screen: plan (branch `coop/4player`)

Started 2026-11-26 from the 2-player mod at v0.95.55 (`coop/stage1`). The 2-player line, its
releases and its notes are not touched by anything on this branch. Status: **not started**,
requirements below are the owner's (2026-11-26).

## Requirements (owner)

### 1. Inputs

- The game counts the connected controllers. Each controller is one input.
- Keyboard and mouse together count as one input too.
- An option makes keyboard and mouse an **independent** input for one player. In the 2-player mod,
  keyboard and mouse also drove a player who had a controller; this option separates them.

### 2. Before the Splitscreen Co-op menus: players and inputs screen

Shown when Splitscreen Co-op is chosen, before New Campaign / Resume / Level Select.

- The game detects how many controllers are connected.
- A button (top right): **"Use keyboard and mouse for 1 player"**. When on, keyboard and mouse are
  their own input for one player.
- The inputs are shown as icons at the top of the screen; they can be **dragged** onto a player's
  part of the screen. The input dropped on a part controls that player.
- Pressing a button on a controller makes its icon **flash**, so each person sees which one is theirs.
- **How many players?** 2, 3 or 4 (maximum 4):
  - 2: horizontal split, top and bottom, exactly as in the 2-player mod.
  - 3: player 1 on the top half; the bottom half split vertically into player 2 (left) and
    player 3 (right).
  - 4: four quarters.
- Then the usual menus (New Campaign, Resume, Level Select, ...).

### 3. Difficulty and controller swap

- Difficulty scales with the number of players: Easy / Normal / Hard as now (enemy and boss health
  100% / 150% / 200% for 2 players), plus about **+0.25x per extra player**.
  Proposed table (enemy health): 2 players 1.00 / 1.50 / 2.00, 3 players 1.25 / 1.75 / 2.25,
  4 players 1.50 / 2.00 / 2.50 (to confirm with the owner).
- Controller swap: with 2 players it stays as now (normal / swapped). With more than 2 players,
  Co-op Settings opens the same drag-and-drop screen as above to assign inputs to screen parts.

## Releases on this line

- Builds: workflow `.github/workflows/coop-4p-build.yml` (own build numbers), results under
  `runs-4p/` on the branch `qa-results`.
- Test builds: drafts titled "AREA 51 SPLITSCREEN CO-OP 4P v<version> (test build N)", files
  `AREA51_SplitscreenCoop_4P_v<version>...`.
- Official releases: `AREA51_SplitscreenCoop_4P_v<version>_<name>`, description from
  [CHANGELOG-4P.md](CHANGELOG-4P.md) (heading `## 4P_v<version>_<name>`), kept on `milestones`
  in a folder `4P_v<version>_<name>`.
