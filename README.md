# Co-op test results

Text logs and test results written by the cloud builds of `coop/stage1`
(`.github/workflows/coop-build.yml`, script `tools/coop-qa/publish_results.sh`). No game data.

- `runs/<build>/<job>/`: one folder per job of a build. Most useful files:
  `score.txt` (pass/fail per check), `sweep.txt` / `sweep-report/` (all-levels sweep),
  `a51_qa_bot.log` (what the test bot did and saw), `smoke.txt` (Windows start-up check).
- `runs/milestone-*`: the package check of a saved build (see the branch `milestones`).

Kept: the newest 20 builds, build 121 (v0.95.26, owner-verified) and the latest check of each
saved build. Older folders were removed on 2026-11-26 and are still in this branch's history.
