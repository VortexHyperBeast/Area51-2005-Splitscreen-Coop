# Splitscreen Co-op mod: where everything is

Two-player local split-screen co-op for the Area 51 (2005) Dreamland-51 PC port, by
VortexHyperBeast. Target: Windows 11 PC. The upstream port lives on `master` (kept untouched);
all mod work is on the branch `coop/stage1`.

## Branches

| Branch | What it holds |
|---|---|
| `master` | The upstream port (Source2Spy/area51-pc) as forked. Never changed here. |
| `coop/stage1` | The mod: game code changes, build/test workflows, tools, these docs. |
| `milestones` | Saved builds kept for good (NexusMods zip, play build, debug symbols), one folder each. |
| `qa-results` | Text logs and test results of the newest cloud builds. |

## Folders on `coop/stage1`

| Path | What it is |
|---|---|
| `docs/coop/STATUS.md` | Feature status, verified fixes, level compatibility, on-hold ideas, risks. |
| `Support/`, `xCore/`, `Apps/` | Game code. Co-op changes are under `#ifdef A51_LOCAL_COOP`; test-only code under `A51_LOCAL_COOP_QA`. Main co-op files: `Support/NetworkMgr/Game/LocalCoop.hpp`, `logic_Campaign.cpp`. |
| `tools/coop-play/` | Player-facing files: `README.txt` (settings in `a51_coop.cfg`), the start screen (`SCREENS/`), the asset setup script and the test launchers of the cloud play build. |
| `tools/coop-play/nexus/` | The readme shipped in the NexusMods package. |
| `tools/coop-qa/` | The automated test: bot scripts and log checks (`coopqa.py`), Linux test runner, result publishing. |
| `.github/workflows/coop-build.yml` | Builds every push to `coop/**`: Windows build, Linux bot tests, NexusMods package and checks, GitHub release (newest 3 kept). Started by hand with "level sweep" it also starts every campaign level. |
| `.github/workflows/milestone.yml` | Saves a build to `milestones`: push a branch `milestone/<name>` with `.github/milestone.json` (run id, commit, tag, `nexus: true`, version). The branch can be deleted afterwards. |

## Versions

The version shown in the menu is the build number: build 87 = v0.87, up to v0.95; after that
v0.95.N with N = build - 95 (build 144 = v0.95.49).

| Version | Build | Notes |
|---|---|---|
| v0.95.26 | 121 | Before the enemy-health change. Owner-verified on a real PC, NexusMods package included. |
| v0.95.49 | 144 | Enemies and bosses 1.5x health in co-op. Bot-tested on all 19 levels; owner test pending. |
