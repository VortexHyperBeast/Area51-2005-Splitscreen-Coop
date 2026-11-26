# Saved co-op builds

Each folder is one build of the Splitscreen Co-op mod, kept for good.

| Folder | Version | What it is |
|---|---|---|
| `milestone-v0.95.26-before-enemy-health` | v0.95.26 (build 121) | Last build before the 1.5x enemy health. Owner-verified on a real PC, including the NexusMods package and Single Player. |
| `milestone-v0.95.49-enemy-health` | v0.95.49 (build 144) | Enemies and bosses with 1.5x health in co-op (`enemy_health_percent=150`). Bot-tested on all 19 levels. |

In each folder:
- `Area51-SplitscreenCoop-v*.zip`: the NexusMods package (drag into the game folder, start A51.exe).
- `Area51-Coop-milestone-*.zip`: the full play build from the cloud test run.
- `A51-milestone-*.pdb`: debug symbols, for reading crash reports of that build.
- `ABOUT.txt`, `milestone.json`: which test run and commit the build came from.

New entries are added by the workflow "Save a milestone build" (`.github/workflows/milestone.yml` on `coop/stage1`).
