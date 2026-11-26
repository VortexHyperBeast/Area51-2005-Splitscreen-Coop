# Saved co-op builds

Each folder is one build of the Splitscreen Co-op mod, kept for good.

| Folder | Version | What it is |
|---|---|---|
| `v0.95.26_Original` | v0.95.26 (build 121) | Last build before the 1.5x enemy health. Owner-verified on a real PC, including the NexusMods package and Single Player. |
| `v0.95.49_Harder_Co-op` | v0.95.49 (build 144) | Enemies and bosses with 1.5x health in co-op (`enemy_health_percent=150`). Bot-tested on all 19 levels; owner-tested (Ascension boss). |
| `v0.95.55_Harder_Co-op` | v0.95.55 (build 150) | Co-op Settings menu (difficulty Easy/Normal/Hard, revive rules), objective marker for both players, keyboard teleport on T. **First official public release** (`AREA51_SplitscreenCoop_v0.95.55_Harder_Co-op` on the Releases page). |

In each folder:
- `Area51-SplitscreenCoop-<name>.zip` / `AREA51_SplitscreenCoop_<name>.zip` (newer): the NexusMods package (drag into the game folder, start A51.exe).
- `Area51-Coop-<name>.zip`: the full play build from the cloud test run.
- `A51-<name>.pdb`: debug symbols, for reading crash reports of that build.
- `ABOUT.txt`, `milestone.json`: which test run and commit the build came from.

Official releases add their folder here automatically (job "official" of `.github/workflows/coop-build.yml`).
