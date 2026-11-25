#!/usr/bin/env bash
# Plays the two-controller co-op test on a machine without a graphics card or a screen.
#
# The game (a test build made with -DA51_LOCAL_COOP_QA=ON) runs on a virtual X display with a
# software Vulkan driver.  Its built-in bot drives two virtual gamepads through a timed script
# (walk, look, jump, fire, reload ... on each pad in turn, then on both at once), the game writes
# what happened to its own log, and coopqa.py judges that log.  Nothing but the game's own log is
# used as evidence.
#
# usage: run_linux.sh <mode> <package-dir> <data-dir> <out-dir>
#   mode         coop    two local players asked for (A51_LOCAL_COOP=2), judged as co-op
#                single  control run: co-op not asked for, judged as a one-player game; the same
#                        log judged as co-op has to FAIL (a test that cannot fail proves nothing)
#   package-dir  holds A51 and SHADERS/   (output of cmake/PackageRuntime.cmake)
#   data-dir     game data root: the Asset Pack's Main-Assets with PC-Textures merged in.
#                Never modified; the run uses a hard-linked copy.
#   out-dir      text results only (never game data, never screenshots)
#
# env (all optional):
#   QA_WORK             scratch directory (default: a new temporary one)
#   QA_MAX_SECONDS      wall-clock limit of the whole run                         (default 3300)
#   QA_STARTUP_SECONDS  the bot must be armed (level running) within this time    (default 1200)
#   QA_WINDOW           window size, small so that software rendering keeps up    (default 640x360)
#   QA_CYCLES           how often the script table is repeated                    (default 3)
#   QA_GDB              1 = run under gdb to get a backtrace if the game crashes (default 0)
#
# While the bot plays, a screenshot of the virtual screen is taken every few seconds (needs ImageMagick).
# The images stay in the scratch directory; only numbers made from them go to the out directory.
#
# Prints RESULT: PASS or RESULT: FAIL as the last line and exits 0 only on PASS.
set -uo pipefail

MODE="${1:?mode: coop or single}"
PKG="$(readlink -f "${2:?package dir}")"
DATA="$(readlink -f "${3:?data dir}")"
mkdir -p "${4:?out dir}"
OUT="$(readlink -f "$4")"
HERE="$(cd "$(dirname "$0")" && pwd)"

MAX_SECONDS="${QA_MAX_SECONDS:-3300}"
STARTUP_SECONDS="${QA_STARTUP_SECONDS:-1200}"
WINDOW="${QA_WINDOW:-640x360}"
CYCLES="${QA_CYCLES:-3}"

case "$MODE" in
  coop)   EXPECT=coop ;;
  swap)   EXPECT=coop-swapped ;;   # co-op with the CO-OP-SETTINGS options: controllers swapped, other skins
  single) EXPECT=single ;;
  coop-l2|single-l2) EXPECT=probe ;;   # the second campaign level: cutscene sound report only, never fails
  coop-exit) EXPECT=probe; QA_LEVEL_ID="${QA_LEVEL_ID:-1130}" ;;   # The Last Exit (owner's crash): report only
  *) echo "mode must be coop, swap, single, coop-l2, single-l2 or coop-exit"; exit 2 ;;
esac

say() { echo "$@" | tee -a "$OUT/run.txt"; }
: > "$OUT/run.txt"

fail_early() { say "$1"; say "RESULT: FAIL ($1)"; exit 1; }

[ -x "$PKG/A51" ]     || fail_early "package has no A51 binary: $PKG"
[ -d "$PKG/SHADERS" ] || fail_early "package has no SHADERS directory: $PKG"
[ -d "$DATA/LEVELS" ] || fail_early "data dir does not look like game data (no LEVELS): $DATA"
command -v Xvfb >/dev/null 2>&1 || fail_early "Xvfb is not installed"
command -v python3 >/dev/null 2>&1 || fail_early "python3 is not installed"

WORK="${QA_WORK:-$(mktemp -d)}/$MODE"
INSTALL="$WORK/install"
RUN="$WORK/run"
rm -rf "$WORK"
mkdir -p "$RUN"

say "mode=$MODE expect=$EXPECT window=$WINDOW cycles=$CYCLES max=${MAX_SECONDS}s startup-gate=${STARTUP_SECONDS}s"
say "host: $(uname -srm), $(nproc) cpus"

#------------------------------------------------------------------------------
# An isolated install: hard links to the data (fast, no extra disk), then the game itself.
#------------------------------------------------------------------------------
if ! cp -al "$DATA" "$INSTALL" 2>/dev/null; then
  say "hard links not possible here, copying the data"
  rm -rf "$INSTALL"
  cp -a "$DATA" "$INSTALL" || fail_early "cannot copy the game data"
fi
cp -f "$PKG/A51" "$INSTALL/A51" || fail_early "cannot install the binary"
chmod +x "$INSTALL/A51"
mkdir -p "$INSTALL/SHADERS"
cp -a "$PKG/SHADERS/." "$INSTALL/SHADERS/" || fail_early "cannot install the shaders"
# the co-op mod's start screen, as in the play zip (the probe log reports whether it loads)
SCREENS_SRC="$(cd "$(dirname "$0")/../coop-play/SCREENS" 2>/dev/null && pwd)"
if [ -n "$SCREENS_SRC" ]; then mkdir -p "$INSTALL/SCREENS"; cp -f "$SCREENS_SRC"/*.XBMP "$INSTALL/SCREENS/" 2>/dev/null || true; fi
printf 'AutoCampaign=1\n' > "$INSTALL/Config.ini"
say "install ready ($(find "$INSTALL/SHADERS" -name '*.ecs' | wc -l) shader packages)"

python3 -I "$HERE/coopqa.py" gen "$WORK/script.txt" --cycles "$CYCLES" | tee -a "$OUT/run.txt"
cp "$WORK/script.txt" "$OUT/script.txt"

#------------------------------------------------------------------------------
# Environment of the run
#------------------------------------------------------------------------------
export A51_DATA_ROOT="$INSTALL"
export A51_QA_SCRIPT="$WORK/script.txt"
export A51_QA_MAX_SECONDS="$MAX_SECONDS"
export A51_QA_WINDOW="$WINDOW"
export SDL_AUDIODRIVER=dummy
export SDL_VIDEODRIVER=x11
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
if [ "$MODE" = coop ] || [ "$MODE" = swap ] || [ "$MODE" = coop-l2 ] || [ "$MODE" = coop-exit ]; then export A51_LOCAL_COOP=2; else unset A51_LOCAL_COOP; fi
if [ "$EXPECT" = probe ]; then export A51_QA_LEVEL_ID="${QA_LEVEL_ID:-1010}"; else unset A51_QA_LEVEL_ID; fi
if [ "$MODE" = swap ]; then
  export A51_COOP_SWAP_PADS=1 A51_COOP_SKIN_P1=6 A51_COOP_SKIN_P2=9
else
  unset A51_COOP_SWAP_PADS A51_COOP_SKIN_P1 A51_COOP_SKIN_P2
fi

# Diagnostics that help when the very first start fails.
{
  echo "--- missing shared libraries:"
  ldd "$INSTALL/A51" 2>&1 | grep -i "not found" || echo "(none)"
  echo "--- vulkan drivers:"
  ls /usr/share/vulkan/icd.d 2>&1
  if command -v vulkaninfo >/dev/null 2>&1; then
    echo "--- vulkaninfo --summary:"
    vulkaninfo --summary 2>&1 | head -60
  fi
} > "$OUT/environment.txt" 2>&1

# Virtual display.
N=99
while [ -e "/tmp/.X11-unix/X$N" ] || [ -e "/tmp/.X$N-lock" ]; do N=$((N + 1)); done
# The virtual screen is exactly as big as the game window, so a screenshot of the whole screen is the game.
Xvfb ":$N" -screen 0 "${WINDOW}x24" -nolisten tcp > "$OUT/xvfb.txt" 2>&1 &
XPID=$!
trap 'kill "$XPID" 2>/dev/null || true' EXIT
for _ in $(seq 1 100); do
  [ -S "/tmp/.X11-unix/X$N" ] && break
  sleep 0.1
done
[ -S "/tmp/.X11-unix/X$N" ] || fail_early "the virtual display did not start (see xvfb.txt)"
export DISPLAY=":$N"

#------------------------------------------------------------------------------
# Run the game, with a watchdog on the startup gate
#------------------------------------------------------------------------------
CMD=("$INSTALL/A51")
if [ "${QA_GDB:-0}" = 1 ] && command -v gdb >/dev/null 2>&1; then
  CMD=(gdb -q -batch -ex "set pagination off" -ex "handle SIGPIPE nostop noprint pass" -ex run -ex "bt 40" --args "$INSTALL/A51")
  say "running under gdb"
fi

ulimit -f 4000000 2>/dev/null || true
cd "$RUN"
START="$(date +%s)"
timeout --signal=TERM --kill-after=30 "$((MAX_SECONDS + 300))" "${CMD[@]}" > "$WORK/game-output.txt" 2>&1 &
GPID=$!
say "game started, pid $GPID"

# Screenshots stay in $WORK/shots on this machine (they show game content); only numbers made from
# them are published.
SHOTS="$WORK/shots"
mkdir -p "$SHOTS"
NSHOTS=0
if ! command -v import >/dev/null 2>&1; then say "ImageMagick 'import' is not installed: no screenshots"; fi

REASON=""
while kill -0 "$GPID" 2>/dev/null; do
  sleep 5
  ELAPSED=$(( $(date +%s) - START ))
  if [ "$NSHOTS" -lt 150 ] && grep -q ' armed: ' "$RUN/a51_qa_bot.log" 2>/dev/null && command -v import >/dev/null 2>&1; then
    if timeout 20 import -window root -depth 8 "ppm:$SHOTS/$(printf '%03d' "$NSHOTS").ppm" 2>/dev/null; then
      # the game's clock at about the time of the shot (its last log line), to place the shot
      printf '%03d %s\n' "$NSHOTS" "$(grep -a -o 'wall=[0-9]*' "$RUN/a51_qa_bot.log" 2>/dev/null | tail -n 1 | cut -d= -f2)" >> "$SHOTS/times.txt"
      NSHOTS=$((NSHOTS + 1))
    fi
  fi
  if ! grep -q ' armed: ' "$RUN/a51_qa_bot.log" 2>/dev/null && [ "$ELAPSED" -gt "$STARTUP_SECONDS" ]; then
    REASON="startup gate: the level was not running after ${STARTUP_SECONDS}s"
    break
  fi
done
if [ -n "$REASON" ]; then
  say "$REASON; stopping the game"
  kill -TERM "$GPID" 2>/dev/null || true
  sleep 5
  kill -KILL "$GPID" 2>/dev/null || true
fi
wait "$GPID"
RC=$?
END="$(date +%s)"
say "game ended: exit code $RC after $((END - START))s (124 = stopped by timeout, 137/143 = killed, 139 = crash)"

kill "$XPID" 2>/dev/null || true

#------------------------------------------------------------------------------
# Collect text only
#------------------------------------------------------------------------------
if [ -f "$RUN/a51_qa_bot.log" ]; then head -c 3000000 "$RUN/a51_qa_bot.log" > "$OUT/a51_qa_bot.log"; else echo "(the game wrote no a51_qa_bot.log)" > "$OUT/a51_qa_bot.log"; fi
if [ -f "$RUN/a51_local_coop_probe.log" ]; then tail -n 120 "$RUN/a51_local_coop_probe.log" > "$OUT/probe-tail.txt"; fi
{
  echo "=== first 150 lines"
  head -n 150 "$WORK/game-output.txt"
  echo "=== last 300 lines"
  tail -n 300 "$WORK/game-output.txt"
} > "$OUT/game-output.txt" 2>&1
grep -n -i -E "error|assert|fail|fatal|vulkan|segmentation|abort|cannot|unable" "$WORK/game-output.txt" | head -150 > "$OUT/game-output-notable.txt" || true
{
  echo "--- run directory:"
  ls -la "$RUN"
  echo "--- files the game created or changed in the install (names only):"
  find "$INSTALL" -type f -newer "$WORK/script.txt" 2>/dev/null | sed "s#^$INSTALL/##" | head -80
} > "$OUT/files.txt" 2>&1
say "game output: $(wc -l < "$WORK/game-output.txt") lines, a51_qa_bot.log: $(wc -l < "$OUT/a51_qa_bot.log") lines"

#------------------------------------------------------------------------------
# Score
#------------------------------------------------------------------------------
say "screenshots taken: $NSHOTS"
python3 -I "$HERE/coopqa.py" shots "$SHOTS" --log "$OUT/a51_qa_bot.log" --out "$OUT/shots.txt" 2>&1 | tee -a "$OUT/run.txt"
python3 -I "$HERE/coopqa.py" audio "$OUT/a51_qa_bot.log" > "$OUT/audio.txt" 2>&1
say "cutscene sound: $(head -c 2000 "$OUT/audio.txt")"
if [ "$EXPECT" = probe ]; then
  grep "level list\|campaign BeginGame" "$OUT/a51_qa_bot.log" | head -30 >> "$OUT/run.txt"
  echo "RESULT: PASS (probe run, report only)" | tee -a "$OUT/run.txt"
  exit 0
fi
python3 -I "$HERE/coopqa.py" score "$OUT/a51_qa_bot.log" --script "$WORK/script.txt" --expect "$EXPECT" --shots "$OUT/shots.txt" --json "$OUT/score.json" > "$OUT/score.txt" 2>&1
SCORE_RC=$?
cat "$OUT/score.txt"

FINAL=$SCORE_RC
if [ "$MODE" = single ]; then
  # The control twin: the very same log judged as a co-op run must fail.
  python3 -I "$HERE/coopqa.py" score "$OUT/a51_qa_bot.log" --script "$WORK/script.txt" --expect coop --shots "$OUT/shots.txt" > "$OUT/score-twin-as-coop.txt" 2>&1
  TWIN_RC=$?
  say "control twin (single-player log judged as co-op): $(tail -n 1 "$OUT/score-twin-as-coop.txt")"
  if [ "$TWIN_RC" -eq 0 ]; then
    say "the control run passed as co-op, so the co-op test cannot tell the two apart"
    FINAL=1
  fi
fi

if [ "$FINAL" -eq 0 ]; then
  echo "RESULT: PASS" | tee -a "$OUT/run.txt"
else
  echo "RESULT: FAIL" | tee -a "$OUT/run.txt"
fi
exit "$FINAL"
