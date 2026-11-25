#!/usr/bin/env python3
"""coopqa - two-controller test for the Area 51 local co-op port (TEST ONLY).

  coopqa.py gen <script-out> [--settle MS] [--cycles N]
        Write the script that the in-game QA bot plays on two virtual gamepads.
  coopqa.py score <a51_qa_bot.log> [--script FILE] [--expect coop|single] [--json FILE]
        Read the game's own log, print one line per check, end with RESULT: PASS or FAIL.
  coopqa.py shots <dir-with-ppm> --log <a51_qa_bot.log> --out <numbers.txt>
        Turn screenshots into numbers (the images themselves are never published);
        score reads them with --shots.
  coopqa.py selftest
        Prove the scorer.  A simulated good run must PASS and every simulated
        fault must be caught.  Exit code 0 only if that holds.

The script generator and the scorer live in one file on purpose: the phase
table below is the single source of truth for what the bot does and for what
the scorer expects to see.

What is judged: only what the game itself wrote to its own log
(a51_qa_bot.log, see Support/NetworkMgr/Game/logic_Campaign.cpp, Apps/GameApp/main.cpp
and xCore/Entropy/Input/backend/sdl/input_sdl.cpp, all under A51_LOCAL_COOP_QA).
Python 3.8+, standard library only.
"""

import argparse
import bisect
import json
import math
import os
import re
import sys

# ----------------------------------------------------------------------------
# Phase table (game time in ms since the game armed the bot)
# ----------------------------------------------------------------------------

DEFAULT_SETTLE_MS = 15000   # nothing is pressed while the level starts up
DEFAULT_CYCLES = 3          # the whole table is repeated; one good cycle is enough
GAP_MS = 500                # idle time between phases

# kind -> (ms the controls are held, ms to keep watching afterwards)
TIMING = {
    "walk0":    (2500, 500),
    "walk1":    (2500, 500),
    "walkboth": (2000, 500),
    "look0":    (1500, 500),
    "look1":    (1500, 500),
    "jump0":    (300, 1200),
    "jump1":    (300, 1200),
    "fire0":    (1500, 500),
    "fire1":    (1500, 500),
    "reload0":  (300, 2800),
    "reload1":  (300, 2800),
}
KINDS = ["walk0", "walk1", "walkboth", "look0", "look1", "jump0", "jump1",
         "fire0", "fire1", "reload0", "reload1"]


def phase_actions(kind, cycle):
    """List of (pad, control, value) held during a phase.  Directions flip on odd
    cycles so that the players do not keep walking into the same wall."""
    s = 1 if cycle % 2 == 0 else -1
    table = {
        "walk0":    [(0, "ly", 100 * s)],
        "walk1":    [(1, "ly", 100 * s)],
        "walkboth": [(0, "lx", 100 * s), (1, "lx", -100 * s)],
        "look0":    [(0, "rx", 100 * s)],
        "look1":    [(1, "rx", -100 * s)],
        "jump0":    [(0, "a", 1)],
        "jump1":    [(1, "a", 1)],
        "fire0":    [(0, "rt", 100)],
        "fire1":    [(1, "rt", 100)],
        "reload0":  [(0, "x", 1)],
        "reload1":  [(1, "x", 1)],
    }
    return table[kind]


class Phase(object):
    def __init__(self, kind, cycle, start, end, check_end, actions):
        self.kind = kind
        self.cycle = cycle
        self.start = start
        self.end = end
        self.check_end = check_end
        self.actions = actions

    def __repr__(self):
        return "Phase(%s c%d %d-%d/%d)" % (self.kind, self.cycle, self.start, self.end, self.check_end)


def build_phases(settle_ms=DEFAULT_SETTLE_MS, cycles=DEFAULT_CYCLES):
    phases = []
    t = settle_ms
    for c in range(cycles):
        for kind in KINDS:
            hold, tail = TIMING[kind]
            phases.append(Phase(kind, c, t, t + hold, t + hold + tail, phase_actions(kind, c)))
            t += hold + tail + GAP_MS
    return phases, t


SWEEP_TAIL_MS = 45000                       # level sweep: play on this long after the moves


CHAIN_FIRST_MS = 12000                      # each level runs this long, then quit to the menu
CHAIN_LEVELS = 13                           # levels started one after another in one session
CHAIN_SECOND_MS = 50000                     # script time the second level runs


def make_chain_script():
    """Owner (build 101): after a few levels in one session (level, menu, level, menu, Invader)
    every further level drops both players through the floor.  Start the levels of
    A51_QA_LEVEL_ID one after another, quitting to the menu after each, and watch the players."""
    lines = ["# start_ms end_ms pad control value   (game time since the bot was armed)",
             "# each level runs, then the test quits to the menu (the script clock pauses there)",
             # the bot needs a stick event or it switches itself off
             "5000 6500 0 rx 40"]
    for k in range(1, CHAIN_LEVELS):
        lines.append("cmd %d tomenu" % (k * CHAIN_FIRST_MS))
    lines.append("quit %d" % ((CHAIN_LEVELS - 1) * CHAIN_FIRST_MS + CHAIN_SECOND_MS))
    return "\n".join(lines) + "\n"


def make_script(settle_ms=DEFAULT_SETTLE_MS, cycles=DEFAULT_CYCLES, sweep=False):
    phases, end = build_phases(settle_ms, cycles)
    lines = [
        "# coopqa script v1 settle=%d cycles=%d" % (settle_ms, cycles),
        "# start_ms end_ms pad control value   (game time since the bot was armed)",
    ]
    for p in phases:
        lines.append("# %s cycle %d" % (p.kind, p.cycle))
        for (pad, ctrl, val) in p.actions:
            lines.append("%d %d %d %s %d" % (p.start, p.end, pad, ctrl, val))
    if sweep:
        # all-levels sweep: the moves once, then both stand and look around while the level runs
        t = end
        for k in range(SWEEP_TAIL_MS // 6000):
            for pad in (0, 1):
                lines.append("%d %d %d rx %d" % (t + 6000 * k, t + 6000 * k + 1500, pad, 40 if k % 2 else -40))
        lines.append("quit %d" % (t + SWEEP_TAIL_MS))
        return "\n".join(lines) + "\n"
    # the scanner: the owner's game crashed when R1 was pressed with it (2026-11-24).  The game
    # gives it the way the level script does and switches both players to it; then each pad holds
    # the trigger (rt) and the right bumper (rb, "R1") while turning a little.
    t = end
    lines.append("# the pause menu's controller swap, there and back")
    lines.append("cmd %d swaptest" % t)
    lines.append("# scanner")
    lines.append("cmd %d scanner" % t)
    t += SCANNER_SWITCH_MS
    for pad in (0, 1):
        for ctrl, hold in (("rt", 3000), ("rb", 1500)):
            lines.append("%d %d %d %s 100" % (t, t + hold, pad, ctrl))
            lines.append("%d %d %d rx 25" % (t, t + hold, pad))
            t += hold + 500
    lines.append("cmd %d geominfo" % t)       # player and NPC body geometry (owner: brighter face, no visor)
    lines.append("# a level change's inventory store and restore, after the scanner was given")
    lines.append("cmd %d carryover" % t)
    t += 2000
    # the "join partner" button: player 2 walks away, then presses Back
    lines.append("# player 2 walks away and presses Back (join player 1)")
    lines.append("%d %d 1 ly 100" % (t, t + 3000))
    lines.append("%d %d 1 back 100" % (t + JOIN_PRESS_MS, t + JOIN_PRESS_MS + 300))
    t += JOIN_MS
    # mutation: player 2 alone turns into the mutant and back; player 1 stays human and the game
    # does not slow down (the single-player mutation slows time for everyone)
    lines.append("# player 2 mutates and turns back")
    lines.append("cmd %d mutate1" % t)
    lines.append("cmd %d mutstat" % (t + MUT_STAT1_MS))
    lines.append("cmd %d unmutate1" % (t + MUT_BACK_MS))
    lines.append("cmd %d mutstat" % (t + MUT_STAT2_MS))
    t += MUT_MS
    # downed and revive: player 2 takes lethal damage and goes down; player 1 (beside him after
    # the join) holds Use (X).  Both pads hold it, the downed player's Use does nothing, and so
    # the swapped-pads run works too.  Then player 2 goes down again and stays down.
    lines.append("# player 2 goes down, player 1 revives him")
    lines.append("cmd %d down1" % t)
    for pad in (0, 1):
        lines.append("%d %d %d x 100" % (t + DOWN_HOLD_START_MS, t + DOWN_HOLD_START_MS + DOWN_HOLD_MS, pad))
    # player 2 takes the pistol out, goes down again, and both pads pull the trigger and press
    # the grenade button (B): the downed player must not shoot or throw (player 1 may)
    lines.append("cmd %d pistol1" % (t + DOWN_PISTOL_MS))
    lines.append("cmd %d down1" % (t + DOWN_AGAIN_MS))
    lines.append("cmd %d downstat" % (t + DOWN_STAT1_MS))
    for pad in (0, 1):
        lines.append("%d %d %d rt 100" % (t + DOWN_ATTACK_MS, t + DOWN_ATTACK_MS + 800, pad))
        lines.append("%d %d %d b 100" % (t + DOWN_ATTACK_MS, t + DOWN_ATTACK_MS + 300, pad))
    lines.append("cmd %d downstat" % (t + DOWN_STAT2_MS))
    t += DOWN_MS
    # player 1 dies: the level must restart from the checkpoint without a crash (with player 2
    # down too, both players are down: the checkpoint restarts)
    lines.append("# player 1 dies, the level restarts")
    lines.append("cmd %d kill0" % t)
    t += DEATH_MS
    # the pause menu: both views must stay drawn behind it (the top view went black, owner
    # 2026-11-25).  The game resumes by itself after 15 s of wall-clock time; the script clock
    # stands still while it is paused.
    lines.append("# pause; the game resumes by itself")
    lines.append("cmd %d pause" % t)
    t += 1000
    # the blue gun whose shot warps the screen: both views must keep drawing after the shot
    lines.append("# player 1 fires the screen-warping blue gun")
    lines.append("cmd %d bbg0" % t)
    for pad in (0, 1):
        lines.append("%d %d %d rt 100" % (t + WARP_FIRE_MS, t + WARP_FIRE_MS + 600, pad))
    lines.append("cmd %d warpmark" % (t + WARP_FIRE_MS))
    # then the grav charge's screen warp right at player 1's feet (the owner's views froze only
    # for a close shot): first the old way, then as the game now draws it, 6 s each
    lines.append("cmd %d warpraw" % (t + WARP_RAW_MS))
    lines.append("cmd %d warpfeet" % (t + WARP_FEET_MS))
    t += WARP_MS
    lines.append("quit %d" % (t + 1000))
    return "\n".join(lines) + "\n"


SCANNER_SWITCH_MS = 4000
SCANNER_MS = SCANNER_SWITCH_MS + 2 * (3500 + 2000) + 2000
JOIN_PRESS_MS = 3500                        # after the start of the join section
JOIN_MS = 5500
WARP_FIRE_MS = 3000                         # after "bbg0" (the weapon switch takes a moment)
WARP_RAW_MS = 8000                          # warp at the feet, drawn the old way
WARP_FEET_MS = 16000                        # warp at the feet, as now drawn
WARP_MS = 24000
MUT_STAT1_MS = 3000                         # after "mutate1"
MUT_BACK_MS = 3500
MUT_STAT2_MS = 6500
MUT_MS = 7000
DOWN_HOLD_START_MS = 1000                   # after the "down1" command
DOWN_HOLD_MS = 5500                         # Use held; the revive takes 4 s
DOWN_PISTOL_MS = 6800                       # player 2 takes the pistol out
DOWN_AGAIN_MS = 8000                        # second "down1"
DOWN_STAT1_MS = 8200                        # what player 2 holds, before ...
DOWN_ATTACK_MS = 8400                       # ... both pads fire and throw ...
DOWN_STAT2_MS = 9400                        # ... and after
DOWN_MS = 9600
DEATH_MS = 20000


HEADER_RE = re.compile(r"^#\s*coopqa script v1 settle=(\d+) cycles=(\d+)", re.M)


def phases_from_script_text(text):
    m = HEADER_RE.search(text)
    if not m:
        return None
    return build_phases(int(m.group(1)), int(m.group(2)))[0]


# ----------------------------------------------------------------------------
# Raw values the bot sends to SDL, used to check that it really pressed things
# ----------------------------------------------------------------------------

def rest_value(ctrl):
    return -32768 if ctrl in ("lt", "rt") else 0


def raw_value(ctrl, val):
    """Same conversion as qa::ToSDL in input_sdl.cpp."""
    if ctrl in ("lx", "ly", "rx", "ry"):
        v = max(-100, min(100, val))
        raw = int(v * 32767 / 100)          # C division truncates toward zero
        return -raw if ctrl in ("ly", "ry") else raw
    if ctrl in ("lt", "rt"):
        v = max(0, min(100, val))
        return -32768 + int(v * 65535 / 100)
    return 1 if val != 0 else 0


# ----------------------------------------------------------------------------
# Log parsing
# ----------------------------------------------------------------------------

LINE_RE = re.compile(r"^wall=(\d+) qt=(-?\d+) (.*)$")
PLAYER_RE = re.compile(
    r"^player (\d+) guid=([0-9A-Fa-f]+) slot=(-?\d+) pad=(-?\d+) pos=(-?\d+),(-?\d+),(-?\d+) "
    r"yaw=(-?\d+) pitch=(-?\d+) hp=(-?\d+) weapon=(.*?) ammo=(-?\d+) inclip=(-?\d+) perclip=(-?\d+)(?: cinema=(\d))?$")
VIEW_RE = re.compile(
    r"^view (\d+) viewport=(-?\d+),(-?\d+),(-?\d+),(-?\d+) eye=(-?\d+),(-?\d+),(-?\d+) "
    r"frames=(\d+) views=(\d+)$")
CTRL_RE = re.compile(r"^pad (\d+) (\w+) = (-?\d+)$")
BODY_RE = re.compile(r"^bodies view=(\d+) drawn=(\d+) frames=(\d+) others=(\d+) with_body=(\d+)$")
STATE_RE = re.compile(r"^state (\S+) -> (\S+)$")


class Log(object):
    def __init__(self):
        self.players = {}       # index -> list of sample dicts (sorted by qt)
        self.views = {}         # index -> list of view dicts
        self.profiles = {}      # local player -> (profile slot, name, avatar)
        self.carryover = {}     # player -> (scanner, pistol) after a stored/restored inventory
        self.swaptest = {}      # "flipped"/"restored" -> (player 0 pad, player 1 pad)
        self.kill_qt = None
        self.down_cmds = []     # script times of the "down1" command
        self.loadout = {}        # player -> scanner count at the level start
        self.loadout_given = False  # the level-select loadout ran
        self.warp_mark = None    # wall ms when the screen-warping gun was fired
        self.warp_raw = None     # wall ms when the warp at the feet began, drawn the old way
        self.warp_feet = None    # wall ms when the warp at the feet began, as now drawn
        self.pause_on = None     # (wall ms, paused) when the test pause began
        self.pause_off = None    # wall ms when the game resumed
        self.mutstats = []      # (qt, player 1 mutated, player 2 mutated, global time x100)
        self.downstats = []     # (qt, downed, grenades, rounds) of player 2 while downed
        self.downs = []         # (qt, downed/revive_start/revived/bledout, local slot)
        self.finished_wall = None
        self.begins = []        # script times of "campaign BeginGame done"
        self.begin_levels = []  # the level of each of them
        self.scanner_cmd_qt = None
        self.scanner_has = {}
        self.shares = []        # (p0 before, p0 after, p1 before, p1 after) of the shared-item probe
        self.bodies = {}        # view index -> list of (drawn frames, frames, others, others with a body)
        self.controls = []      # (qt, pad, name, raw)
        self.states = []        # (wall, from, to)
        self.armed_wall = None
        self.finished = False
        self.wall_limit = False
        self.errors = []
        self.attached = {}      # pad -> is_gamepad
        self.missing = []       # "player N missing" lines
        self.asserts = []       # assertions the game hit (logged and skipped by the bot)
        self.exit_asserts = []  # assertions while the game shuts down after the script
        self.lines = 0
        self.unparsed = 0
        self.perf = []          # (wall ms, frames, window ms) from the bot's frame counter
        self.level = None       # level id the campaign started
        self.declared_players = None
        self.npc = []           # (qt, what, self guid, type, target guid, target local slot) enemy target changes


def parse_log(text):
    log = Log()
    for line in text.splitlines():
        line = line.rstrip("\r")
        if not line:
            continue
        log.lines += 1
        m = LINE_RE.match(line)
        if not m:
            log.unparsed += 1
            continue
        wall, qt, msg = int(m.group(1)), int(m.group(2)), m.group(3)

        pm = PLAYER_RE.match(msg)
        if pm:
            g = pm.groups()
            log.players.setdefault(int(g[0]), []).append({
                "qt": qt, "guid": g[1].upper(), "slot": int(g[2]), "pad": int(g[3]),
                "x": int(g[4]), "y": int(g[5]), "z": int(g[6]),
                "yaw": int(g[7]), "pitch": int(g[8]), "hp": int(g[9]),
                "weapon": g[10], "ammo": int(g[11]), "inclip": int(g[12]), "perclip": int(g[13]),
                "cinema": int(g[14]) if g[14] is not None else 0,
            })
            continue
        vm = VIEW_RE.match(msg)
        if vm:
            g = [int(v) for v in vm.groups()]
            log.views.setdefault(g[0], []).append({
                "qt": qt, "wall": wall, "vp": (g[1], g[2], g[3], g[4]),
                "eye": (g[5], g[6], g[7]), "frames": g[8], "views": g[9],
            })
            continue
        nm = re.match(r"^npc (\w+) self=([0-9a-fA-F]+) type=(.+?) tgt=([0-9a-fA-F]+) tslot=(-?\d+)$", msg)
        if nm:
            g = nm.groups()
            log.npc.append((qt, g[0], g[1].upper(), g[2], g[3].upper(), int(g[4])))
            continue
        sh = re.match(r"^share item=\S+ p0 (-?\d+)->(-?\d+) p1 (-?\d+)->(-?\d+)$", msg)
        if sh:
            log.shares.append(tuple(int(v) for v in sh.groups()))
            continue
        bm_ = BODY_RE.match(msg)
        if bm_:
            g = [int(v) for v in bm_.groups()]
            log.bodies.setdefault(g[0], []).append((g[1], g[2], g[3], g[4]))
            continue
        cm = CTRL_RE.match(msg)
        if cm:
            log.controls.append((qt, int(cm.group(1)), cm.group(2), int(cm.group(3))))
            continue
        sm = STATE_RE.match(msg)
        if sm:
            log.states.append((wall, sm.group(1), sm.group(2)))
            continue
        pfm = re.match(r"^perf frames=(\d+) in (\d+) ms", msg)
        bm = re.match(r"^campaign BeginGame done: players=(\d+) level=(-?\d+)", msg)
        if pfm:
            log.perf.append((wall, int(pfm.group(1)), int(pfm.group(2))))
        elif bm:
            log.declared_players = int(bm.group(1))
            log.level = int(bm.group(2))
            log.begins.append(qt)
            log.begin_levels.append(int(bm.group(2)))
        elif msg.startswith("armed:"):
            if log.armed_wall is None:
                log.armed_wall = wall
        elif msg.startswith("script finished"):
            log.finished = True
            log.finished_wall = wall
        elif msg.startswith("profile player "):
            mm = re.match(r"profile player (\d+): index=(-?\d+) name=(.*) avatar=(-?\d+)$", msg)
            if mm:
                log.profiles[int(mm.group(1))] = (int(mm.group(2)), mm.group(3), int(mm.group(4)))
        elif msg.startswith("carryover player "):
            mm = re.match(r"carryover player (\d+): scanner=(-?\d+) pistol=(-?\d+)$", msg)
            if mm:
                log.carryover[int(mm.group(1))] = (int(mm.group(2)), int(mm.group(3)))
        elif msg.startswith("swaptest "):
            mm = re.match(r"swaptest (\w+): player 0 pad=(-?\d+) player 1 pad=(-?\d+)$", msg)
            if mm:
                log.swaptest[mm.group(1)] = (int(mm.group(2)), int(mm.group(3)))
        elif msg.startswith("downstat player 1:"):
            dm = re.match(r"^downstat player 1: downed=(\d) grenades=(-?\d+) rounds=(-?\d+)$", msg)
            if dm:
                log.downstats.append((qt, int(dm.group(1)), int(dm.group(2)), int(dm.group(3))))
        elif msg.startswith("loadout player "):
            lm = re.match(r"^loadout player (\d+): scanner=(-?\d+)", msg)
            if lm:
                log.loadout[int(lm.group(1))] = int(lm.group(2))
                if msg.endswith("given=1"):
                    log.loadout_given = True
        elif msg.startswith("warp mark"):
            log.warp_mark = wall
        elif msg.startswith("warp feet: on"):
            if msg.endswith("raw=1"):
                log.warp_raw = wall
            else:
                log.warp_feet = wall
        elif msg.startswith("pause: on"):
            log.pause_on = (wall, msg.endswith("paused=1"))
        elif msg.startswith("pause: off"):
            log.pause_off = wall
        elif msg.startswith("mutstat "):
            mm = re.match(r"^mutstat p0=(-?\d+) p1=(-?\d+) speed=(-?\d+)(?: abil0=(-?\d+) abil1=(-?\d+))?(?: body1=([0-9a-fA-F]+))?$", msg)
            if mm:
                g = mm.groups()
                log.mutstats.append((qt, int(g[0]), int(g[1]), int(g[2]),
                                     int(g[3]) if g[3] is not None else None, int(g[4]) if g[4] is not None else None,
                                     int(g[5], 16) if g[5] is not None else None))
        elif msg.startswith("down player 1"):
            log.down_cmds.append(qt)
        elif msg.startswith("coop downed ") or msg.startswith("coop revived ") or \
                msg.startswith("coop revive_start ") or msg.startswith("coop bledout ") or \
                msg.startswith("coop down_pose "):
            dm = re.match(r"^coop (\w+) slot=(-?\d+)", msg)
            if dm:
                what = dm.group(1)
                if what == "down_pose":
                    what = "pose_ok" if msg.endswith("played=1") else "pose_missing"
                log.downs.append((qt, what, int(dm.group(2))))
        elif msg.startswith("kill player 0"):
            log.kill_qt = qt
        elif msg.startswith("command scanner"):
            log.scanner_cmd_qt = qt
        elif msg.startswith("scanner player "):
            mm = re.match(r"scanner player (\d+): in inventory=(-?\d+)", msg)
            if mm:
                log.scanner_has[int(mm.group(1))] = int(mm.group(2))
        elif msg.startswith("wall-clock limit reached"):
            log.wall_limit = True
        elif msg.startswith("ERROR"):
            log.errors.append(msg)
        elif msg.startswith("ASSERT #"):
            # after "script finished" the game is shutting down (static destructors)
            if log.finished_wall is None:
                log.asserts.append(msg)
            else:
                log.exit_asserts.append(msg)
        elif re.match(r"^player \d+ missing", msg):
            log.missing.append(msg)
        else:
            am = re.match(r"^pad (\d+) attached: .*is_gamepad=(\d)", msg)
            if am:
                log.attached[int(am.group(1))] = int(am.group(2))

    for lst in log.players.values():
        lst.sort(key=lambda s: s["qt"])
    for lst in log.views.values():
        lst.sort(key=lambda s: (s["wall"]))
    return log


# ----------------------------------------------------------------------------
# Helpers on sample series
# ----------------------------------------------------------------------------

def at(samples, t):
    """Last sample with qt <= t, or None."""
    keys = [s["qt"] for s in samples]
    i = bisect.bisect_right(keys, t) - 1
    return samples[i] if i >= 0 else None


def at_lerp(samples, t):
    """Position at time t, interpolated between the samples around it."""
    keys = [s["qt"] for s in samples]
    i = bisect.bisect_right(keys, t) - 1
    if i < 0:
        return None
    a = samples[i]
    if i + 1 >= len(samples) or samples[i + 1]["qt"] == a["qt"]:
        return a
    b = samples[i + 1]
    f = float(t - a["qt"]) / (b["qt"] - a["qt"])
    return {"qt": t, "x": a["x"] + f * (b["x"] - a["x"]), "y": a["y"] + f * (b["y"] - a["y"]),
            "z": a["z"] + f * (b["z"] - a["z"])}


def between(samples, t0, t1):
    return [s for s in samples if t0 <= s["qt"] <= t1]


def hdist(a, b):
    return math.hypot(a["x"] - b["x"], a["z"] - b["z"])


def yaw_delta(a, b):
    """Signed difference in tenths of a degree, wrapped to -1800..1800."""
    return ((b["yaw"] - a["yaw"] + 1800) % 3600) - 1800


def rounds(s):
    """Rounds the player can fire right now: the clip, else the pool; None if no weapon."""
    if s["inclip"] >= 0:
        return s["inclip"]
    if s["ammo"] >= 0:
        return s["ammo"]
    return None


# thresholds (world units are centimetres, yaw in tenths of a degree)
MOVE_MIN = 100      # the player that was driven moved at least this far
STILL_MAX = 40      # a bystander moved less than this
YAW_MIN = 150       # 15 degrees
YAW_STILL = 30      # 3 degrees
JUMP_MIN = 15
JUMP_STILL = 10
NO_FALL = 1500      # a player that sinks further than 15 m below its first position fell out of the world

PASS, FAIL, NOEV, INFO = "PASS", "FAIL", "NO-EVIDENCE", "INFO"

REQUIRED_COOP = ["startup", "two_players", "separate_pads", "no_fall", "alive", "bot_drove",
                 "completed", "walk0", "walk1", "walkboth", "look0", "look1",
                 "fire0", "fire1", "reload0", "reload1", "pad_mapping", "split_view", "split_pixels",
                 "partner_body", "shared_cinema", "shared_items", "scanner", "own_profiles", "live_swap", "asserts", "carry_over", "join_button", "pause_view", "mutation_independent", "downed_revive", "downed_no_attack", "death_restart"]
INFO_COOP = ["jump0", "jump1"]
REQUIRED_SINGLE = ["startup", "one_player", "bot_drove", "completed", "walk_single"]


class Result(object):
    def __init__(self, name, status, detail):
        self.name = name
        self.status = status
        self.detail = detail

    def as_dict(self):
        return {"name": self.name, "status": self.status, "detail": self.detail}


# ----------------------------------------------------------------------------
# Per-phase evidence.  Each returns (positive_ok, negative_ok, text, actor)
#   positive_ok: the driven player did what was asked
#   negative_ok: the other player did not react
#   actor:       index of the player that reacted most (None if nothing did)
# ----------------------------------------------------------------------------

def _pair(log, ph):
    a = [at(log.players.get(i, []), ph.start) for i in (0, 1)]
    b = [at(log.players.get(i, []), ph.check_end) for i in (0, 1)]
    if None in a or None in b:
        return None
    return a, b


CONTACT = 130       # the players' bodies touch (collision radius 30 cm each, plus margin for the 0.5 s samples)


def pushed(log, ph, actor):
    """The bystander only moved after the walking player's body reached it (since the players
    have bodies, one can push the other).  True when the bystander had not moved before the
    first sample at which the two were in contact, and moved less than the walker."""
    other = 1 - actor
    w0 = between(log.players.get(0, []), ph.start, ph.check_end)
    for s0 in w0:
        s1 = at(log.players.get(1, []), s0["qt"])
        if s1 is None or hdist(s0, s1) > CONTACT:
            continue
        a = at(log.players[other], ph.start)
        c = at(log.players[other], s0["qt"])
        return a is not None and c is not None and hdist(a, c) <= STILL_MAX
    return False


def ev_walk(log, ph):
    pr = _pair(log, ph)
    if pr is None:
        return None
    a, b = pr
    m = [hdist(a[i], b[i]) for i in (0, 1)]
    actor = 0 if m[0] >= m[1] else 1
    still = m[1 - actor] <= STILL_MAX
    note = ""
    if not still and m[actor] >= MOVE_MIN and m[1 - actor] < m[actor] and pushed(log, ph, actor):
        still, note = True, " (pushed by the walker's body)"
    return (m[actor] >= MOVE_MIN, still,
            "P0 %dcm P1 %dcm%s" % (m[0], m[1], note), actor if m[actor] >= MOVE_MIN else None)


def ev_look(log, ph):
    pr = _pair(log, ph)
    if pr is None:
        return None
    a, b = pr
    m = [abs(yaw_delta(a[i], b[i])) for i in (0, 1)]
    actor = 0 if m[0] >= m[1] else 1
    return (m[actor] >= YAW_MIN, m[1 - actor] <= YAW_STILL,
            "P0 %.1fdeg P1 %.1fdeg" % (m[0] / 10.0, m[1] / 10.0), actor if m[actor] >= YAW_MIN else None)


def ev_jump(log, ph):
    pr = _pair(log, ph)
    if pr is None:
        return None
    a, b = pr
    gain = []
    for i in (0, 1):
        win = between(log.players[i], ph.start, ph.check_end)
        gain.append(max([s["y"] - a[i]["y"] for s in win] + [0]))
    actor = 0 if gain[0] >= gain[1] else 1
    return (gain[actor] >= JUMP_MIN, gain[1 - actor] <= JUMP_STILL,
            "P0 +%dcm P1 +%dcm" % (gain[0], gain[1]), actor if gain[actor] >= JUMP_MIN else None)


def ev_fire(log, ph):
    pr = _pair(log, ph)
    if pr is None:
        return None
    a, b = pr
    spent = []
    for i in (0, 1):
        r0 = rounds(a[i])
        if r0 is None or r0 <= 0:
            return None                      # no weapon or an empty one: cannot judge
        win = [rounds(s) for s in between(log.players[i], ph.start, ph.check_end)]
        win = [w for w in win if w is not None]
        spent.append(r0 - min(win) if win else 0)
    actor = 0 if spent[0] >= spent[1] else 1
    return (spent[actor] >= 1, spent[1 - actor] == 0,
            "P0 spent %d P1 spent %d" % (spent[0], spent[1]), actor if spent[actor] >= 1 else None)


def ev_reload(log, ph):
    pr = _pair(log, ph)
    if pr is None:
        return None
    a, b = pr
    gain = []
    for i in (0, 1):
        r0, r1 = rounds(a[i]), rounds(b[i])
        if r0 is None or r1 is None or a[i]["inclip"] < 0:
            return None
        gain.append(r1 - r0)
    # a reload needs a clip that is not full at the start; if neither was short, nothing to judge
    if all(a[i]["perclip"] > 0 and a[i]["inclip"] >= a[i]["perclip"] for i in (0, 1)):
        return None
    actor = 0 if gain[0] >= gain[1] else 1
    return (gain[actor] >= 1, gain[1 - actor] <= 0,
            "P0 clip%+d P1 clip%+d" % (gain[0], gain[1]), actor if gain[actor] >= 1 else None)


EVIDENCE = {"walk0": ev_walk, "walk1": ev_walk, "look0": ev_look, "look1": ev_look,
            "jump0": ev_jump, "jump1": ev_jump, "fire0": ev_fire, "fire1": ev_fire,
            "reload0": ev_reload, "reload1": ev_reload}


def ev_walkboth(log, ph):
    pr = _pair(log, ph)
    if pr is None:
        return None
    a, b = pr
    d = []
    for i in (0, 1):
        d.append((b[i]["x"] - a[i]["x"], b[i]["z"] - a[i]["z"]))
    m = [math.hypot(*d[i]) for i in (0, 1)]
    apart = math.hypot(d[0][0] - d[1][0], d[0][1] - d[1][1])
    ok = m[0] >= MOVE_MIN and m[1] >= MOVE_MIN and apart >= MOVE_MIN
    return (ok, True, "P0 %dcm P1 %dcm, displacement difference %dcm" % (m[0], m[1], apart), None)


def aggregate(name, per_cycle):
    """per_cycle: list of (cycle, evidence tuple or None)."""
    have = [(c, e) for (c, e) in per_cycle if e is not None]
    text = "; ".join("c%d: %s" % (c, e[2]) for (c, e) in have) or "no samples in any window"
    cross = [c for (c, e) in have if e[0] and not e[1]]
    good = [c for (c, e) in have if e[0] and e[1]]
    if cross:
        return Result(name, FAIL, "the other player reacted too (cycle %s). %s" % (cross, text))
    if good:
        return Result(name, PASS, text)
    return Result(name, NOEV, "the driven player never did it. %s" % text)


# ----------------------------------------------------------------------------
# Whole-run checks
# ----------------------------------------------------------------------------

def check_startup(log, expect):
    need = [0, 1] if expect == "coop" else [0]
    counts = [len(log.players.get(i, [])) for i in need]
    if log.armed_wall is None:
        last = ", ".join("%s" % s[2] for s in log.states[-4:]) or "no state change seen"
        return Result("startup", FAIL, "the game never started a campaign level (last states: %s)" % last)
    if min(counts) < 20:
        return Result("startup", FAIL, "armed, but too few player samples: %s" % counts)
    return Result("startup", PASS, "level %s started, bot armed at wall=%dms, samples per player %s" % (
        log.level if log.level is not None else "?", log.armed_wall, counts))


def before_kill(log, samples):
    """Samples before the scripted death of player 1 (the level restarts after it)."""
    if log.kill_qt is None:
        return samples
    return [s for s in samples if s["qt"] < log.kill_qt]


def check_two_players(log):
    p0, p1 = before_kill(log, log.players.get(0, [])), before_kill(log, log.players.get(1, []))
    if not p0 or not p1:
        return Result("two_players", FAIL, "players seen: %s" % sorted(log.players))
    g0 = set(s["guid"] for s in p0)
    g1 = set(s["guid"] for s in p1)
    if len(g0) != 1 or len(g1) != 1 or g0 == g1:
        return Result("two_players", FAIL, "guids P0=%s P1=%s" % (sorted(g0), sorted(g1)))
    near = hdist(p0[0], p1[0])
    return Result("two_players", PASS, "two player objects %s and %s, first seen %dcm apart" % (
        list(g0)[0], list(g1)[0], near))


def check_separate_pads(log):
    p0, p1 = log.players.get(0, []), log.players.get(1, [])
    if not p0 or not p1:
        return Result("separate_pads", FAIL, "second player missing")
    pads0 = set(s["pad"] for s in p0)
    pads1 = set(s["pad"] for s in p1)
    slots0 = set(s["slot"] for s in p0)
    slots1 = set(s["slot"] for s in p1)
    ok = (len(pads0) == 1 and len(pads1) == 1 and pads0 != pads1 and min(pads0 | pads1) >= 0
          and slots0 == {0} and slots1 == {1})
    return Result("separate_pads", PASS if ok else FAIL,
                  "game-side pad P0=%s P1=%s, local slots P0=%s P1=%s" % (
                      sorted(pads0), sorted(pads1), sorted(slots0), sorted(slots1)))


def check_no_fall(log):
    bad = []
    for i, lst in sorted(log.players.items()):
        if lst and min(s["y"] for s in lst) < lst[0]["y"] - NO_FALL:
            bad.append(i)
    return Result("no_fall", FAIL if bad else PASS,
                  ("player %s fell out of the world" % bad) if bad else "no player sank below its start")


def check_alive(log):
    dead = []
    for i, lst in sorted(log.players.items()):
        for s in before_kill(log, lst):
            if s["hp"] <= 0:
                dead.append((i, s["qt"]))
                break
    return Result("alive", FAIL if dead else PASS,
                  ("dead (player, qt ms): %s" % dead) if dead else "every sample has hp > 0")


def check_bot_drove(log, phases):
    """Hollow-pass guard: the bot must have really pressed what the table says."""
    problems = []
    if sorted(log.attached) != [0, 1] or not all(log.attached.values()):
        problems.append("virtual pads attached: %s" % log.attached)
    if log.errors:
        problems.append("bot errors: %s" % log.errors[:2])
    seen = {}
    for (qt, pad, name, raw) in log.controls:
        seen.setdefault((pad, name), []).append((qt, raw))
    total = hit = 0
    for ph in phases:
        for (pad, ctrl, val) in ph.actions:
            total += 1
            evs = seen.get((pad, ctrl), [])
            want = raw_value(ctrl, val)
            pressed = [q for (q, r) in evs if r == want and ph.start <= q <= ph.start + 1000]
            released = [q for (q, r) in evs if r == rest_value(ctrl) and ph.end <= q <= ph.end + 1000]
            if pressed and released:
                hit += 1
    if total == 0:
        problems.append("no scheduled controls")
    elif hit < 0.95 * total:
        problems.append("only %d of %d scheduled control changes appear in the log" % (hit, total))
    return Result("bot_drove", FAIL if problems else PASS,
                  "; ".join(problems) if problems else "%d of %d scheduled control changes seen, both pads attached" % (hit, total))


def check_completed(log, phases):
    if log.wall_limit:
        return Result("completed", FAIL, "wall-clock limit reached before the script finished")
    if not log.finished:
        return Result("completed", FAIL, "no 'script finished' line (crash or hang?)")
    need = phases[-1].check_end - 1500 if phases else 0
    last = max([s["qt"] for l in log.players.values() for s in l] + [-1])
    if last < need:
        return Result("completed", FAIL, "last player sample at %dms, script needs %dms" % (last, need))
    return Result("completed", PASS, "script played to the end, last sample at %dms" % last)


def check_split_view(log):
    # only while the script runs: during a scene before it, both players share one full-screen view
    # only the walk/look/fire phases: later sections teleport a player (join button)
    last = max([s["qt"] for s in log.players.get(0, [])] or [0])
    if log.scanner_cmd_qt is not None:
        last = log.scanner_cmd_qt
    v0 = [v for v in log.views.get(0, []) if 0 <= v["qt"] < last]
    v1 = [v for v in log.views.get(1, []) if 0 <= v["qt"] < last]
    if len(v0) < 3 or len(v1) < 3:
        return Result("split_view", NOEV, "view lines: view0=%d view1=%d" % (len(v0), len(v1)))
    vp0, vp1 = set(v["vp"] for v in v0), set(v["vp"] for v in v1)
    if len(vp0) != 1 or len(vp1) != 1:
        return Result("split_view", FAIL, "viewports change during the run: %s %s" % (sorted(vp0), sorted(vp1)))
    a, b = list(vp0)[0], list(vp1)[0]
    total_h = b[3] - a[1]
    h0, h1 = a[3] - a[1], b[3] - b[1]
    problems = []
    if a[0] != b[0] or a[2] != b[2]:
        problems.append("the two views have different widths")
    if not (a[3] <= b[1]):
        problems.append("view 0 is not above view 1 (or they overlap)")
    if total_h <= 0 or not (0.35 * total_h <= h0 <= 0.5 * total_h and 0.35 * total_h <= h1 <= 0.5 * total_h):
        problems.append("the views are not two halves of the screen (heights %d, %d of %d)" % (h0, h1, total_h))
    if any(v["views"] != 2 for v in v0 + v1):
        problems.append("the renderer drew %s views" % sorted(set(v["views"] for v in v0 + v1)))

    # each eye must follow its own player once the two have walked apart
    checked = 0
    wrong = 0
    for i, views in ((0, v0), (1, v1)):
        for v in views:
            if v["qt"] < 0:
                continue
            # players are sampled every 0.5 s and views once a second: interpolate the player
            # position to the time of the view sample, or a walking player's eye looks misplaced
            p = [at_lerp(log.players.get(k, []), v["qt"]) for k in (0, 1)]
            if None in p:
                continue
            if hdist(p[0], p[1]) < 300:
                continue                      # still too close together to tell them apart
            ex, ez = v["eye"][0], v["eye"][2]
            d = [math.hypot(ex - q["x"], ez - q["z"]) for q in p]
            checked += 1
            if d[i] > d[1 - i]:
                wrong += 1
    if checked == 0:
        problems.append("no view sample while the players were far enough apart to tell them apart")
    elif wrong:
        problems.append("%d of %d view samples sat nearer to the other player" % (wrong, checked))
    eyes0 = set(v["eye"] for v in v0)
    eyes1 = set(v["eye"] for v in v1)
    if len(eyes0) < 2 or len(eyes1) < 2:
        problems.append("a camera never moved")
    return Result("split_view", FAIL if problems else PASS,
                  "; ".join(problems) if problems else
                  "view 0 %s above view 1 %s, %d eye samples near their own player, both cameras moved" % (a, b, checked))


def check_pad_mapping(results_meta):
    """The same pad must drive the same player in every kind of action."""
    mapping = {}
    for (pad, actor, tag) in results_meta:
        mapping.setdefault(pad, {}).setdefault(actor, []).append(tag)
    if sorted(mapping) != [0, 1]:
        return Result("pad_mapping", NOEV, "pads with evidence: %s" % sorted(mapping))
    bad = [p for p in mapping if len(mapping[p]) != 1]
    drivers = [list(mapping[p])[0] for p in (0, 1) if len(mapping[p]) == 1]
    if bad:
        return Result("pad_mapping", FAIL, "pad %s drove different players in different actions: %s" % (
            bad, {p: {a: len(t) for a, t in mapping[p].items()} for p in bad}))
    if drivers[0] == drivers[1]:
        return Result("pad_mapping", FAIL, "both pads drive player %d" % drivers[0])
    return Result("pad_mapping", PASS, "pad 0 -> player %d, pad 1 -> player %d, in all %d judged actions" % (
        drivers[0], drivers[1], sum(len(t) for p in mapping.values() for t in p.values())))


def check_join_button(log, script_end):
    """Player 2 presses Back (View) far from player 1 and must be next to him right after."""
    press = script_end + SCANNER_MS + JOIN_PRESS_MS
    before = [s for s in between(log.players.get(1, []), press - 1200, press - 1)]
    after = [s for s in between(log.players.get(1, []), press + 300, press + 1600)]
    if not before or not after:
        return Result("join_button", NOEV, "no samples around the Back press")
    def dist(s1):
        s0 = at(log.players.get(0, []), s1["qt"])
        return hdist(s0, s1) if s0 else 1e9
    d0 = min(dist(s) for s in before)
    d1 = min(dist(s) for s in after)
    if d0 >= 300 and d1 <= 250:
        return Result("join_button", PASS, "player 2 was %d cm away, %d cm after pressing Back" % (d0, d1))
    return Result("join_button", FAIL, "player 2 was %d cm away before Back and %d cm after" % (d0, d1))


def check_downed_revive(log):
    """Player 2 takes lethal damage while player 1 is up: he must go down, not die; player 1
    holds Use beside him and he gets up; a second lethal hit puts him down again."""
    if not log.down_cmds:
        return Result("downed_revive", NOEV, "the down command never ran")
    first = log.down_cmds[0]
    downed = [q for (q, w, sl) in log.downs if w == "downed" and sl == 1 and q >= first]
    revived = [q for (q, w, sl) in log.downs if w == "revived" and sl == 1 and q >= first]
    if not downed:
        return Result("downed_revive", FAIL, "player 2 did not go down after lethal damage")
    if not revived:
        return Result("downed_revive", FAIL, "player 2 went down but was not revived while player 1 held Use")
    took = revived[0] - downed[0]
    again = [q for q in downed if q > revived[0]]
    if not again:
        return Result("downed_revive", FAIL, "player 2 was revived after %d ms but did not go down a second time" % took)
    poses = [w for (q, w, sl) in log.downs if w in ("pose_ok", "pose_missing") and sl == 1]
    pose = (" The lying-down body animation played %d of %d times." % (poses.count("pose_ok"), len(poses))) if poses else ""
    if poses and "pose_missing" in poses:
        return Result("downed_revive", FAIL, "player 2 went down and was revived, but the lying-down body animation did not play." + pose)
    return Result("downed_revive", PASS, "player 2 went down, was revived after %d ms of play, and went down again.%s" % (took, pose))


def check_downed_no_attack(log):
    """While downed, player 2 must not shoot or throw a grenade: both pads pull the trigger and
    press the grenade button between two snapshots of what he holds."""
    if len(log.downstats) < 2:
        return Result("downed_no_attack", NOEV, "the two snapshots of the downed player were not logged")
    a, b = log.downstats[0], log.downstats[1]
    if not (a[1] and b[1]):
        return Result("downed_no_attack", FAIL, "player 2 was not down during the attack test (downed %d then %d)" % (a[1], b[1]))
    spent = []
    if b[2] != a[2]:
        spent.append("grenades %d -> %d" % (a[2], b[2]))
    if b[3] != a[3]:
        spent.append("rounds %d -> %d" % (a[3], b[3]))
    samples = [smp for smp in log.players.get(1, []) if a[0] <= smp["qt"] <= b[0]]
    clips = [smp["inclip"] for smp in samples if smp["inclip"] >= 0]
    if clips and max(clips) != min(clips):
        spent.append("clip %d -> %d" % (max(clips), min(clips)))
    if spent:
        return Result("downed_no_attack", FAIL, "the downed player attacked: " + ", ".join(spent))
    return Result("downed_no_attack", PASS, "the downed player fired nothing and threw nothing (grenades %d, rounds %d)" % (b[2], b[3]))


def check_mutation_independent(log):
    """Player 2 turns into the mutant on his own: player 1 must stay human and the world must
    not slow down; then player 2 turns back."""
    if len(log.mutstats) < 2:
        return Result("mutation_independent", NOEV, "the two mutation snapshots were not logged")
    a, b = log.mutstats[0], log.mutstats[1]
    if a[2] != 1:
        return Result("mutation_independent", FAIL, "player 2 did not turn into the mutant (p2 mutated=%d)" % a[2])
    if a[1] != 0:
        return Result("mutation_independent", FAIL, "player 1 turned into the mutant too when only player 2 did")
    if a[3] < 95:
        return Result("mutation_independent", FAIL, "the world slowed down (%d%%) while player 2 was a mutant" % a[3])
    if b[2] != 0:
        return Result("mutation_independent", FAIL, "player 2 did not turn back")
    if a[4] is not None and a[5] is not None and a[4] != a[5]:
        return Result("mutation_independent", FAIL, "player 2's mutant attacks differ from player 1's "
                      "(abilities %d vs %d: melee 1, primary 2, secondary 4, toggle 8)" % (a[5], a[4]))
    ODD = 2 | 8 | 32 | 128
    if a[6] is not None and b[6] is not None:
        if not (a[6] & ODD):
            return Result("mutation_independent", FAIL, "player 2 mutated but his body stayed human (mesh mask %x)" % a[6])
        if b[6] & ODD:
            return Result("mutation_independent", FAIL, "player 2 turned back but his body stayed a mutant (mesh mask %x)" % b[6])
    body = " His body showed the mutant form and went back." if a[6] is not None else ""
    return Result("mutation_independent", PASS, "player 2 mutated alone and turned back; player 1 stayed human, the world ran at %d%%.%s" % (a[3], body))


def check_death_restart(log):
    """Player 1 dies; with shared fate the level restarts from the checkpoint.  The game must
    get there without crashing and run on (the owner's game crashed at such a restart)."""
    if log.kill_qt is None:
        return Result("death_restart", NOEV, "the kill command never ran")
    again = [q for q in log.begins if q is not None and q > log.kill_qt]
    if not again:
        return Result("death_restart", FAIL, "no restart after player 1 died%s" % ("" if log.finished else " (and the game did not finish)"))
    if not log.finished:
        return Result("death_restart", FAIL, "the level restarted %d ms after the death, then the game did not finish" % (again[0] - log.kill_qt))
    return Result("death_restart", PASS, "the level restarted %d ms after player 1 died, and the game ran on" % (again[0] - log.kill_qt))


def check_carry_over(log, players=2):
    """A level change stores the inventory and restores it in the new level.  Both players must
    come out with the scanner and the pistol they had (player 1 lost his on the owner's PC)."""
    if not log.carryover:
        return Result("carry_over", NOEV, "no carry-over lines")
    bad = [i for i in range(players) if log.carryover.get(i, (0, 0)) < (1, 1) or min(log.carryover.get(i, (0, 0))) < 1]
    detail = "; ".join("player %d: scanner %d, pistol %d" % ((i,) + log.carryover.get(i, (-1, -1))) for i in range(players))
    return Result("carry_over", FAIL if bad else PASS, detail)


def check_live_swap(log):
    """The pause menu's controller swap re-binds the pads at once, and back again."""
    f, r = log.swaptest.get("flipped"), log.swaptest.get("restored")
    if f is None or r is None:
        return Result("live_swap", NOEV, "no swap test lines")
    if f[0] != f[1] and f == (r[1], r[0]) and -1 not in f:
        return Result("live_swap", PASS, "swapped: player 0 pad %d, player 1 pad %d; back: %d, %d" % (f + r))
    return Result("live_swap", FAIL, "swapped: %s, back: %s" % (f, r))


def check_own_profiles(log):
    """Player 2 has its own profile (own settings and body, saved with it), named "Splitscreen P2",
    in a different profile slot from player 1's."""
    if 0 not in log.profiles or 1 not in log.profiles:
        return Result("own_profiles", NOEV, "no profile lines for both players")
    (i0, n0, a0), (i1, n1, a1) = log.profiles[0], log.profiles[1]
    if i0 >= 0 and i1 >= 0 and i0 != i1 and n1 == "Splitscreen P2":
        return Result("own_profiles", PASS, "player 1: slot %d '%s' body %d; player 2: slot %d '%s' body %d" % (i0, n0, a0, i1, n1, a1))
    return Result("own_profiles", FAIL, "player 1: slot %d '%s'; player 2: slot %d '%s'" % (i0, n0, i1, n1))


def check_scanner(log):
    """Both players get the scanner (given to player 0 the way the level script does) and fire it
    with RT and R1 without the game crashing (the run must still finish: see 'completed')."""
    if log.scanner_cmd_qt is None:
        return Result("scanner", NOEV, "the scanner command never ran")
    bad = []
    for i in (0, 1):
        if log.scanner_has.get(i, 0) < 1:
            bad.append("player %d did not get it" % i)
        after = [s for s in log.players.get(i, []) if s["qt"] >= log.scanner_cmd_qt + SCANNER_SWITCH_MS]
        if not any(s["weapon"] == "SCN" for s in after):
            bad.append("player %d never held it" % i)
    if not log.finished:
        bad.append("the game did not reach the end of the script after firing it")
    if bad:
        return Result("scanner", FAIL, "; ".join(bad))
    return Result("scanner", PASS, "both players got the scanner, held it and fired it with RT and R1; the game ran on")


def check_shared_items(log):
    """An item a script gives the first player reaches the other one too (on the owner's PC the
    pistol and the scanner of the first level went to player 1 only).  The test build gives
    player 0 one grenade the way the trigger action does and logs both players' counts."""
    if not log.shares:
        return Result("shared_items", NOEV, "no shared-item probe in the log")
    b0, a0, b1, a1 = log.shares[0]
    if a0 == b0 + 1 and a1 == b1 + 1:
        return Result("shared_items", PASS, "grenade given to player 0: player 0 %d->%d, player 1 %d->%d" % (b0, a0, b1, a1))
    return Result("shared_items", FAIL, "grenade given to player 0: player 0 %d->%d, player 1 %d->%d" % (b0, a0, b1, a1))


def check_shared_cinema(log):
    """A scripted scene (cutscene, or a moment that takes control away) must reach both players.
    Triggers address the first player; the co-op code copies the scene to the other one.  On
    the owner's PC only player 1 got it (2026-11-24).  Level 1000 opens with such a scene."""
    p0 = [s for s in log.players.get(0, []) if s["cinema"]]
    p1 = [s for s in log.players.get(1, []) if s["cinema"]]
    if not p0 and not p1:
        return Result("shared_cinema", INFO, "no scripted scene ran while the log was written")
    if p0 and p1:
        return Result("shared_cinema", PASS, "scene seen by both players (%d and %d samples)" % (len(p0), len(p1)))
    who = 0 if p0 else 1
    return Result("shared_cinema", FAIL, "a scene ran for player %d only (%d samples)" % (who, len(p0 or p1)))


def check_partner_body(log):
    """Each view draws the other player's body.  The game counts, per view and second, the frames
    in which a player body was submitted for drawing, and whether every other player has a body
    (skin) at all.  A player with no body is invisible to the other one: the bug seen on the
    owner's PC on 2026-11-24, before the co-op players got the avatar body."""
    if not log.bodies:
        return Result("partner_body", NOEV, "no body lines in the log (older build?)")
    parts, bad = [], []
    for v in (0, 1):
        lst = log.bodies.get(v, [])
        if not lst:
            bad.append("view %d: no body lines" % v)
            continue
        drawn = sum(d for (d, f, o, w) in lst)
        frames = sum(f for (d, f, o, w) in lst)
        lst = [b for b in lst if b[2] >= 1]      # lines with no other view (full-screen scene) say nothing
        missing = len([1 for (d, f, o, w) in lst if w < o])
        if missing:
            bad.append("view %d: the other player had no body in %d of %d seconds" % (v, missing, len(lst)))
        if drawn == 0:
            bad.append("view %d: the other player's body was never drawn (%d frames)" % (v, frames))
        parts.append("view %d: other player's body drawn in %d of %d frames" % (v, drawn, frames))
    if bad:
        return Result("partner_body", FAIL, "; ".join(bad))
    return Result("partner_body", PASS, "; ".join(parts))


def check_perf(log):
    """Information only: how fast the game ran (a software renderer can be slow)."""
    if not log.perf:
        return Result("perf", INFO, "no frame-rate lines in the log")
    rates = [1000.0 * f / max(1, ms) for (_, f, ms) in log.perf]
    return Result("perf", INFO, "%d samples, frame rate min %.1f, mean %.1f, max %.1f per second" % (
        len(rates), min(rates), sum(rates) / len(rates), max(rates)))


def check_enemy_targets(log):
    """Report only (Stage 2): which player the enemies pick. Becomes a requirement once a bot
    route reliably meets enemies."""
    if not log.npc:
        return Result("enemy_targets", INFO, "no enemy picked a target while the log was written")
    sets = [n for n in log.npc if n[1] == "set"]
    hits = [n for n in log.npc if n[1] == "hit" and n[5] in (0, 1)]
    by_slot = {}
    for n in sets:
        by_slot[n[5]] = by_slot.get(n[5], 0) + 1
    enemies = sorted(set(n[2] for n in log.npc))
    types = sorted(set(n[3] for n in log.npc))
    on_p2 = sorted(set(n[2] for n in sets if n[5] == 1))
    return Result("enemy_targets", INFO,
                  "%d enemies (%s); target picks: player 1 %d, player 2 %d, other %d; "
                  "%d enemies picked player 2; %d turned on the player who hit them" % (
                      len(enemies), ",".join(types), by_slot.get(0, 0), by_slot.get(1, 0),
                      sum(v for k, v in by_slot.items() if k not in (0, 1)), len(on_p2), len(hits)))


def check_cinema(log):
    """Information only: a cutscene during the script explains missing evidence."""
    during = []
    for i, lst in sorted(log.players.items()):
        n = len([s for s in lst if s["qt"] >= 0 and s["cinema"]])
        if n:
            during.append("player %d: %d samples" % (i, n))
    return Result("cinema", INFO, ("a cinematic ran during the script (%s)" % "; ".join(during)) if during
                  else "no cinematic during the script")


# Assertions known to be harmless and not caused by co-op (seen in every run, also single player).
BENIGN_ASSERTS = ["x_threads_posix.cpp"]


def check_asserts(log, required=False):
    """The test build logs assertions and carries on.  A retail build compiles ASSERT(x) into
    __assume(x), so an assertion that fails here can be a crash there: the owner's scanner crash
    (2026-11-24) was 'Weapon not supported in mp!' asserts that runs 13 to 15 logged and ignored.
    In co-op runs every assertion fails the run, except the known harmless ones."""
    real = [a for a in log.asserts if not any(b in a for b in BENIGN_ASSERTS)]
    if not log.asserts:
        return Result("asserts", PASS if required else INFO, "the game hit no assertion")
    first = "; ".join(a[len("ASSERT "):][:160] for a in (real or log.asserts)[:3])
    if real and required:
        return Result("asserts", FAIL, "%d assertion(s) (a retail build may crash there). First: %s" % (len(real), first))
    return Result("asserts", PASS if required else INFO,
                  "%d assertion(s), %d of them not known harmless. First: %s" % (len(log.asserts), len(real), first))


# ----------------------------------------------------------------------------
# Screenshots.  The launcher grabs the virtual screen a few times while the bot plays.  The
# images show game content and stay on the test machine; this code turns each one into a few
# numbers (the only thing that is published) and the check below judges the numbers.
#
# A real split screen is two complete views with a thin gap between them that neither view
# draws into.  The gap shows the clear colour, so it is a uniform line across the whole width
# while the rows on both sides of it show the game.  One view drawn full screen has no such line.
# ----------------------------------------------------------------------------

SHOT_BAND = 8            # rows on each side of the gap that are looked at
SHOT_CONTENT_STD = 6.0   # luminance spread that counts as "the game is drawn here"
SHOT_BAR_STD = 4.0       # luminance spread of a uniform gap
SHOT_MIN_USABLE = 3      # screenshots needed before the check can say anything
SHOT_DIFF_MIN = 3.0      # the two halves are not the same picture if they differ by this much on average

SHOT_KEYS = ("bar_mean", "bar_std", "above_mean", "above_std", "below_mean", "below_std",
             "top_std", "bot_std", "diff")


def read_ppm(path):
    """Binary PPM (P6, 8 bit) -> (width, height, pixel bytes)."""
    with open(path, "rb") as f:
        data = f.read()
    n = len(data)
    pos = 0
    tokens = []
    while len(tokens) < 4 and pos < n:
        c = data[pos:pos + 1]
        if c in (b" ", b"\t", b"\r", b"\n"):
            pos += 1
        elif c == b"#":
            while pos < n and data[pos:pos + 1] != b"\n":
                pos += 1
        else:
            start = pos
            while pos < n and data[pos:pos + 1] not in (b" ", b"\t", b"\r", b"\n"):
                pos += 1
            tokens.append(data[start:pos])
    if len(tokens) < 4 or tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError("not an 8 bit binary PPM")
    pos += 1                                  # the single whitespace after the maximum value
    w, h = int(tokens[1]), int(tokens[2])
    body = data[pos:pos + w * h * 3]
    if len(body) != w * h * 3:
        raise ValueError("truncated PPM")
    return w, h, body


def _lum_row(body, w, y):
    row = body[y * w * 3:(y + 1) * w * 3]
    return [(r * 299 + g * 587 + b * 114) // 1000 for r, g, b in zip(row[0::3], row[1::3], row[2::3])]


def _mean_std(values):
    if not values:
        return 0.0, 0.0
    m = sum(values) / float(len(values))
    return m, math.sqrt(sum((v - m) ** 2 for v in values) / float(len(values)))


def shot_stats(path, vp0, vp1):
    """Numbers for one screenshot.  vp0 / vp1 are the two viewports (x0, y0, x1, y1) from the game's log."""
    w, h, body = read_ppm(path)
    full_w, full_h = max(vp0[2], vp1[2]), max(vp0[3], vp1[3])
    sy = h / float(full_h)
    top_end = int(round(vp0[3] * sy))         # first row below the top view
    bot_start = int(round(vp1[1] * sy))       # first row of the bottom view
    if top_end >= bot_start:
        # co-op layout: the views share an edge and each draws its part of a 4-row divider
        # (rows edge-2 .. edge+1) inside itself
        edge = bot_start
        top_end, bot_start = edge - int(round(2 * sy)), edge + int(round(2 * sy))
    bar_rows = list(range(top_end, bot_start))
    out = {"w": w, "h": h, "bar_y0": top_end, "bar_y1": bot_start}

    def band(rows):
        vals = []
        for y in rows:
            if 0 <= y < h:
                vals.extend(_lum_row(body, w, y))
        return _mean_std(vals)

    out["bar_mean"], out["bar_std"] = band(bar_rows)
    out["above_mean"], out["above_std"] = band(range(top_end - SHOT_BAND, top_end))
    out["below_mean"], out["below_std"] = band(range(bot_start, bot_start + SHOT_BAND))

    # whole views on a coarse grid (every 4th row, every 4th pixel), and the two compared point by point
    top_h = top_end
    bot_h = h - bot_start
    n = max(0, min(top_h, bot_h))
    tops, bots, diffs = [], [], []
    for k in range(0, n, 4):
        a = _lum_row(body, w, k)[::4]
        b = _lum_row(body, w, bot_start + k)[::4]
        tops.extend(a)
        bots.extend(b)
        diffs.extend(abs(p - q) for p, q in zip(a, b))
    # the left edge of each view (the pause menu and its bars stay clear of it)
    edge_w = max(1, w // 12)
    lt, lb = [], []
    for k in range(0, n, 4):
        lt.extend(_lum_row(body, w, k)[:edge_w])
        lb.extend(_lum_row(body, w, bot_start + k)[:edge_w])
    out["top_left_std"] = _mean_std(lt)[1]
    out["bot_left_std"] = _mean_std(lb)[1]
    out["top_left_mean"] = _mean_std(lt)[0]
    out["bot_left_mean"] = _mean_std(lb)[0]
    out["top_std"] = _mean_std(tops)[1]
    out["bot_std"] = _mean_std(bots)[1]
    out["diff"] = (sum(diffs) / float(len(diffs))) if diffs else 0.0
    return out


def shots_from_dir(directory, log):
    """Statistics for every *.ppm in the directory, as text lines the score command can read back."""
    import os
    v0 = log.views.get(0, [])
    v1 = log.views.get(1, [])
    lines = []
    if v0 and not v1:
        # a one-view run: look at the middle of the screen as if it were split there
        x0, y0, x1, y1 = v0[-1]["vp"]
        mid = (y1 - y0) // 2
        vp0, vp1 = (x0, y0, x1, y0 + mid - 1), (x0, y0 + mid + 1, x1, y1)
    elif not v0:
        return ["# no view lines in the game's log, so the screenshots cannot be located"]
    else:
        vp0, vp1 = v0[-1]["vp"], v1[-1]["vp"]
    names = sorted(f for f in os.listdir(directory) if f.endswith(".ppm"))
    walls = {}
    try:
        for tl in open(os.path.join(directory, "times.txt")):
            parts = tl.split()
            if len(parts) == 2 and parts[1].isdigit():
                walls[parts[0] + ".ppm"] = int(parts[1])
    except IOError:
        pass
    for i, name in enumerate(names):
        try:
            s = shot_stats(os.path.join(directory, name), vp0, vp1)
        except (ValueError, IOError) as e:
            lines.append("shot %d unreadable: %s" % (i, e))
            continue
        extra = " top_left_std=%.1f bot_left_std=%.1f top_left_mean=%.1f bot_left_mean=%.1f" % (
            s["top_left_std"], s["bot_left_std"], s["top_left_mean"], s["bot_left_mean"])
        if name in walls:
            extra += " wall=%d" % walls[name]
        lines.append("shot %d size=%dx%d gap_rows=%d-%d %s%s" % (
            i, s["w"], s["h"], s["bar_y0"], s["bar_y1"],
            " ".join("%s=%.1f" % (k, s[k]) for k in SHOT_KEYS), extra))
    return lines


SHOT_RE = re.compile(r"^shot (\d+) size=(\d+)x(\d+) gap_rows=(-?\d+)-(-?\d+) (.*)$")


def parse_shots(text):
    shots = []
    for line in text.splitlines():
        m = SHOT_RE.match(line.strip())
        if not m:
            continue
        d = {"n": int(m.group(1)), "gap": int(m.group(5)) - int(m.group(4))}
        for kv in m.group(6).split():
            k, _, v = kv.partition("=")
            try:
                d[k] = float(v)
            except ValueError:
                pass
        if all(k in d for k in SHOT_KEYS):
            shots.append(d)
    return shots


def _split_counts(shots):
    usable = [s for s in shots if s["gap"] > 0 and s["above_std"] >= SHOT_CONTENT_STD and s["below_std"] >= SHOT_CONTENT_STD]
    bar = [s for s in usable if s["bar_std"] < SHOT_BAR_STD]
    return usable, bar


def check_pause_view(log, shots):
    """While the pause menu is open, both views must still show the game: the left edge of each
    view (clear of the menu) has picture detail, not a black or smeared area."""
    if log.pause_on is None:
        return Result("pause_view", NOEV, "the pause command never ran")
    if not log.pause_on[1]:
        return Result("pause_view", FAIL, "the game did not pause")
    if shots is None:
        return Result("pause_view", NOEV, "no screenshot statistics were given")
    on = log.pause_on[0]
    off = log.pause_off if log.pause_off is not None else on + 15000
    during = [s for s in shots if "wall" in s and on + 1500 <= s["wall"] < off - 300 and "top_left_std" in s]
    if not during:
        return Result("pause_view", NOEV, "no screenshot was taken while the game was paused")
    # Compare with the last screenshot before the pause (same spot, same light): a view that is
    # still drawn keeps most of its brightness and detail; the bug left it black (mean ~0).
    before = [s for s in shots if "wall" in s and s["wall"] < on and "top_left_std" in s and
              s["top_left_mean"] > 2.0 and s["bot_left_mean"] > 2.0]
    ref = before[-1] if before else None

    def empty(s, half):
        mean, std = s[half + "_left_mean"], s[half + "_left_std"]
        if ref is None:
            return std < SHOT_CONTENT_STD
        return mean < 0.5 * ref[half + "_left_mean"] or std < 0.4 * ref[half + "_left_std"] or mean < 2.0

    bad = [s for s in during if empty(s, "top") or empty(s, "bot")]
    detail = "; ".join("top %.1f/%.1f bottom %.1f/%.1f" % (s["top_left_mean"], s["top_left_std"],
                                                           s["bot_left_mean"], s["bot_left_std"]) for s in during)
    if ref is not None:
        detail += " (before the pause: top %.1f/%.1f bottom %.1f/%.1f)" % (
            ref["top_left_mean"], ref["top_left_std"], ref["bot_left_mean"], ref["bot_left_std"])
    if bad:
        return Result("pause_view", FAIL, "%d of %d paused screenshots have an empty view (edge mean/spread: %s)" % (
            len(bad), len(during), detail))
    if log.pause_off is None:
        return Result("pause_view", FAIL, "both views stayed drawn, but the game never resumed (%s)" % detail)
    return Result("pause_view", PASS, "%d paused screenshots: both views drawn (edge mean/spread: %s); the game resumed" % (
        len(during), detail))


def check_loadout(log):
    """Report: at the level start (level select gives the level's starting weapons), both
    players must have the scanner (the owner had none in later levels, 2026-11-25)."""
    if not log.loadout:
        return Result("loadout", INFO, "the start inventory was not logged")
    if not log.loadout_given:
        return Result("loadout", INFO, "not a level-select start (the first level starts unarmed by design)")
    missing = [p for p, n in sorted(log.loadout.items()) if n <= 0]
    text = ", ".join("player %d scanner=%d" % (p + 1, n) for p, n in sorted(log.loadout.items()))
    if missing:
        return Result("loadout", FAIL, "a player started without the scanner (%s)" % text)
    return Result("loadout", PASS, text)


def check_warp_view(log, shots):
    """After the screen-warping blue gun is fired, both views must keep showing the game (the
    owner's views froze and the top one went black, 2026-11-25)."""
    if log.warp_mark is None:
        return Result("warp_view", NOEV, "the blue gun was never fired")
    if shots is None:
        return Result("warp_view", NOEV, "no screenshot statistics were given")
    held = [smp for smp in log.players.get(0, []) if smp.get("weapon") == "BBG"]
    if not held:
        return Result("warp_view", NOEV, "player 1 never had the blue gun in his hands, so nothing was fired")
    after = [s for s in shots if "wall" in s and log.warp_mark + 1500 <= s["wall"] <= log.warp_mark + 9000
             and "top_left_std" in s]
    if not after:
        return Result("warp_view", NOEV, "no screenshot was taken after the shot")
    detail = "; ".join("top %.1f/%.1f bottom %.1f/%.1f" % (s["top_left_mean"], s["top_left_std"],
                                                           s["bot_left_mean"], s["bot_left_std"]) for s in after)
    bad = [s for s in after if s["top_left_std"] < SHOT_CONTENT_STD or s["bot_left_std"] < SHOT_CONTENT_STD]
    if bad:
        return Result("warp_view", FAIL, "%d of %d screenshots after the shot have an empty view (edge mean/spread: %s)" % (
            len(bad), len(after), detail))
    return Result("warp_view", PASS, "%d screenshots after the shot: both views drawn (%s)" % (len(after), detail))


def _warp_window(log, shots, start):
    """Screenshots and frame rate in the 6 s a warp at the feet is drawn."""
    after = [x for x in (shots or []) if "wall" in x and start + 1000 <= x["wall"] <= start + 6000
             and "top_left_std" in x]
    rates = [1000.0 * f / max(1, ms) for (w, f, ms) in log.perf if start + 1000 <= w <= start + 7500]
    base = [1000.0 * f / max(1, ms) for (w, f, ms) in log.perf if start - 12000 <= w < start - 500]
    return after, rates, base


def check_warp_feet(log, shots):
    """A grav-charge screen warp right at player 1's feet (the owner's close blue-gun shot): both
    views must keep being drawn and the game must not slow to a crawl.  The same warp drawn the
    old way is reported next to it for comparison."""
    if log.warp_feet is None:
        return Result("warp_feet", NOEV, "the warp at the feet was never drawn")
    after, rates, base = _warp_window(log, shots, log.warp_feet)
    if not after:
        return Result("warp_feet", NOEV, "no screenshot was taken during the warp")
    def desc(a, r, b):
        return "views %s; fps %s (before %s)" % (
            "; ".join("top %.1f/%.1f bottom %.1f/%.1f" % (x["top_left_mean"], x["top_left_std"],
                                                         x["bot_left_mean"], x["bot_left_std"]) for x in a) or "-",
            "/".join("%.1f" % v for v in r) or "-", "/".join("%.1f" % v for v in b) or "-")
    detail = desc(after, rates, base)
    if log.warp_raw is not None:
        detail += " | the old way: " + desc(*_warp_window(log, shots, log.warp_raw))
    bad = [x for x in after if x["top_left_std"] < SHOT_CONTENT_STD or x["bot_left_std"] < SHOT_CONTENT_STD]
    if bad:
        return Result("warp_feet", FAIL, "%d of %d screenshots during the warp have an empty view: %s" % (
            len(bad), len(after), detail))
    if rates and base and min(rates) < 0.5 * (sum(base) / len(base)):
        return Result("warp_feet", FAIL, "the game slowed to under half speed during the warp: " + detail)
    return Result("warp_feet", PASS, "both views drawn at normal speed: " + detail)


def sweep_report(log, level, probe_text="", shots=None):
    """One line for the all-levels sweep: did the level start, run and keep both players alive?"""
    problems, notes = [], []
    begins = [q for q in log.begins if q is not None] + ([None] if None in log.begins else [])
    if not log.begins:
        problems.append("the level never started")
    lv = log.begin_levels
    restarts = sum(1 for k in range(1, len(lv)) if lv[k] == lv[k - 1])
    changes = ["%d -> %d" % (lv[k - 1], lv[k]) for k in range(1, len(lv)) if lv[k] != lv[k - 1]]
    if changes:
        notes.append("level change: " + ", ".join(changes))
    downs = [d for d in log.downs if d[1] == "downed"]
    bled = [d for d in log.downs if d[1] == "bledout"]
    if restarts:
        # The bot stands still and never revives, so a downed player bleeds out: that is not
        # a level problem by itself, a restart without anyone going down is.
        why = "%d downed, %d bled out (the bot does not fight or revive)" % (len(downs), len(bled)) if downs else "nobody went down first"
        (notes if downs else problems).append("%d checkpoint restart(s): %s" % (restarts, why))
    if not log.finished:
        problems.append("the game did not finish the run (crash or hang)")
    if log.asserts:
        problems.append("%d assertion(s): %s" % (len(log.asserts), "; ".join(sorted(set(log.asserts)))[:300]))
    fails = sorted(set(l.split("render failure:", 1)[1].strip() for l in probe_text.splitlines() if "render failure:" in l))
    if fails:
        problems.append("render failure: " + " | ".join(fails)[:300])
    for i in sorted(log.players):
        smp = [x for x in log.players[i] if x["qt"] is not None and x["qt"] >= 0]
        if not smp:
            continue
        # a fall: a drop of more than 15 m that ends in death (hp 0) - lifts go down too
        for k in range(1, len(smp)):
            if smp[k]["hp"] > 0:
                continue
            recent = [x for x in smp[:k] if smp[k]["qt"] - x["qt"] <= 5000]
            if recent and max(x["y"] for x in recent) - smp[k]["y"] > 1500:
                problems.append("player %d fell %d m to his death" % (i + 1, (max(x["y"] for x in recent) - smp[k]["y"]) // 100))
                break
        else:
            low = min(x["y"] for x in smp)
            if smp[0]["y"] - low > 1500:
                notes.append("player %d went %d m down (lift?) and lived" % (i + 1, (smp[0]["y"] - low) // 100))
        notes.append("player %d hp %d" % (i + 1, smp[-1]["hp"]))
    if len(log.players) < 2:
        problems.append("only %d player(s) in the level" % len(log.players))
    et = check_enemy_targets(log)
    notes.append("enemies: " + et.detail.split(";")[0])
    if log.npc:
        on = set(n[5] for n in log.npc if n[1] == "set")
        notes.append("targeted: %s" % ",".join("player %d" % (t + 1) for t in sorted(on) if t in (0, 1)) or "no player")
    cines = [l for l in check_cinema(log).detail.split(";")]
    notes.append(check_cinema(log).detail[:80])
    if shots:
        dark = [x for x in shots if x.get("top_left_mean", 99) < 2 and x.get("bot_left_mean", 99) < 2]
        if len(dark) > len(shots) // 2:
            problems.append("most screenshots are black")
    verdict = "OK" if not problems else "PROBLEM"
    return "level %d: %s - %s%s" % (level, verdict, "; ".join(problems) + (" | " if problems else ""), "; ".join(notes))


def check_split_pixels(shots):
    if shots is None:
        return Result("split_pixels", NOEV, "no screenshot statistics were given")
    if not shots:
        return Result("split_pixels", NOEV, "no screenshot could be read")
    if all(s["gap"] <= 0 for s in shots):
        return Result("split_pixels", FAIL, "the two viewports in the game's log leave no gap, so no line can separate the views")
    usable, bar = _split_counts(shots)
    if len(usable) < SHOT_MIN_USABLE:
        return Result("split_pixels", NOEV, "only %d of %d screenshots show the game on both sides of the gap (need %d)" % (
            len(usable), len(shots), SHOT_MIN_USABLE))
    problems = []
    if len(bar) < max(SHOT_MIN_USABLE, int(0.6 * len(usable) + 0.999)):
        problems.append("only %d of %d screenshots show a uniform line between two drawn views" % (len(bar), len(usable)))
    if max(s["diff"] for s in usable) < SHOT_DIFF_MIN:
        problems.append("the two halves are the same picture (largest difference %.1f)" % max(s["diff"] for s in usable))
    if problems:
        return Result("split_pixels", FAIL, "; ".join(problems))
    return Result("split_pixels", PASS, "%d of %d screenshots: a uniform line between two drawn halves, halves differ by up to %.0f" % (
        len(bar), len(usable), max(s["diff"] for s in usable)))


def score(log, phases, expect="coop", shots=None):
    # coop-swapped: a co-op run with the controller-swap setting on; pad 0 must drive player 1
    swapped = (expect == "coop-swapped")
    if swapped:
        expect = "coop"
    results = []
    results.append(check_startup(log, expect))
    results.append(check_asserts(log, required=(expect == "coop")))
    results.append(check_cinema(log))
    results.append(check_enemy_targets(log))
    results.append(check_perf(log))

    if expect == "single":
        extra = set(log.players) - {0}
        views = [v["views"] for l in log.views.values() for v in l]
        ok = not extra and all(n == 1 for n in views)
        results.append(Result("one_player", PASS if ok else FAIL,
                              "only player 0 exists, renderer drew 1 view" if ok else
                              "players seen %s, view counts %s" % (sorted(log.players), sorted(set(views)))))
        results.append(check_bot_drove(log, phases))
        results.append(check_completed(log, phases))
        best = 0
        for ph in phases:
            if ph.kind in ("walk0", "walk1"):
                lst = log.players.get(0, [])
                a, b = at(lst, ph.start), at(lst, ph.check_end)
                if a and b:
                    best = max(best, hdist(a, b))
        results.append(Result("walk_single", PASS if best >= MOVE_MIN else NOEV,
                              "the single player walked %dcm in the best walk phase" % best))
        if shots:
            usable, bar = _split_counts(shots)
            results.append(Result("no_split_line", INFO,
                                  "%d of %d screenshots show a uniform line across the middle (a one-view run should show none)" % (
                                      len(bar), len(usable)), ))
        return results, REQUIRED_SINGLE

    results.append(check_two_players(log))
    results.append(check_separate_pads(log))
    results.append(check_no_fall(log))
    results.append(check_alive(log))
    results.append(check_bot_drove(log, phases))
    results.append(check_completed(log, phases))

    meta = []
    for kind in KINDS:
        per = []
        for ph in [p for p in phases if p.kind == kind]:
            if len(log.players.get(0, [])) == 0 or len(log.players.get(1, [])) == 0:
                per.append((ph.cycle, None))
                continue
            fn = ev_walkboth if kind == "walkboth" else EVIDENCE[kind]
            e = fn(log, ph)
            per.append((ph.cycle, e))
            if e is not None and kind != "walkboth" and e[0] and e[1] and e[3] is not None:
                pad = int(kind[-1])
                meta.append((pad, e[3], "%s c%d" % (kind, ph.cycle)))
        results.append(aggregate(kind, per))
    results.append(check_pad_mapping(meta))
    if swapped:
        r = results[-1]
        if r.status == PASS and not r.detail.startswith("pad 0 -> player 1,"):
            results[-1] = Result("pad_mapping", FAIL, "controller swap is on, but " + r.detail)
        elif r.status == PASS:
            r.detail = "controller swap on: " + r.detail
    results.append(check_split_view(log))
    results.append(check_split_pixels(shots))
    if expect != "single":
        results.append(check_pause_view(log, shots))
        results.append(check_warp_view(log, shots))
        results.append(check_warp_feet(log, shots))
        results.append(check_loadout(log))
    results.append(check_partner_body(log))
    results.append(check_shared_cinema(log))
    results.append(check_shared_items(log))
    results.append(check_scanner(log))
    results.append(check_own_profiles(log))
    results.append(check_live_swap(log))
    results.append(check_carry_over(log))
    results.append(check_mutation_independent(log))
    results.append(check_downed_revive(log))
    results.append(check_downed_no_attack(log))
    results.append(check_death_restart(log))
    if phases:
        results.append(check_join_button(log, max(p.check_end for p in phases) + GAP_MS))
    return results, REQUIRED_COOP


def verdict(results, required):
    by = {r.name: r for r in results}
    failed = [n for n in required if by.get(n) is None or by[n].status == FAIL]
    noev = [n for n in required if by.get(n) is not None and by[n].status == NOEV]
    ok = not failed and not noev
    return ok, failed, noev


def render(results, required, ok, failed, noev):
    out = []
    width = max(len(r.name) for r in results)
    for r in results:
        tag = "" if r.name in required else "  (info)"
        out.append(" %-11s %-*s  %s%s" % (r.status, width, r.name, r.detail, tag))
    if ok:
        out.append("RESULT: PASS")
    else:
        out.append("RESULT: FAIL (%d failed: %s; %d without evidence: %s)" % (
            len(failed), ",".join(failed) or "-", len(noev), ",".join(noev) or "-"))
    return "\n".join(out)


# ----------------------------------------------------------------------------
# Simulator for the self-test: writes a log the way the game would
# ----------------------------------------------------------------------------

SIM_W, SIM_H = 640, 360          # window of the simulated run
SIM_GAP0, SIM_GAP1 = 179, 181    # the two rows between the views

FAULTS = [
    "none",
    "p1_clone",            # player 1 just copies player 0
    "p1_ignores_pad",      # pad 1 does nothing
    "crosstalk_move",      # pad 1 walks both players
    "shared_aim",          # right stick turns both players
    "shared_fire",         # firing spends both players' ammo
    "shared_reload",       # reloading refills both players
    "same_viewport",       # both views drawn full screen
    "views_swapped",       # camera of view 0 sits on player 1
    "static_cameras",      # cameras never follow the players
    "no_bot_lines",        # the bot never pressed anything (and nothing moved)
    "single_player",       # only one player exists
    "never_armed",         # the level never started
    "timeout",             # the script did not finish
    "no_weapon",           # nobody holds a weapon
    "pads_swapped",        # pad 0 drives player 1 and the reverse (still a valid co-op)
    "same_pad",            # both players bound to one game-side pad
    "p1_falls",            # player 1 spawned outside the world
    "no_gap_line",         # the screenshots show one continuous picture (no line between two views)
    "bottom_blank",        # the bottom half of the screen stays empty
    "identical_halves",    # both halves show the same picture
    "partner_invisible",   # the players have no 3rd person body, so neither sees the other
    "cinema_p0_only",      # a scripted scene reached player 0 only
    "items_not_shared",    # an item given to player 0 does not reach player 1
    "scanner_crash",       # the game dies when the scanner is fired
    "shared_profile",      # player 2 uses player 1's profile
    "swap_noop",           # the in-game controller swap changes nothing
    "assert_hit",          # the game hit a real assertion (a retail build may crash there)
    "carry_lost_p1",       # player 1 loses his weapons at a level change
    "join_ignored",        # the Back button does not bring player 2 to player 1
    "death_crash",         # the game dies when the level restarts after a death
    "not_downed",          # lethal damage kills player 2 instead of putting him down
    "no_revive",           # holding Use beside the downed partner does nothing
    "downed_attacks",      # the downed player can still shoot and throw grenades
    "mutation_shared",     # player 2's mutation also mutates player 1 and slows the world
    "mutation_failed",     # player 2 cannot turn into the mutant
    "mutant_no_attack",    # player 2 mutates but lacks player 1's mutant attacks
    "mutant_body_missing", # player 2 mutates but his body stays human
    "pause_black",         # a view goes black behind the pause menu
]

# fault -> names that must NOT be PASS in coop mode ("*ok*" = the run is a good one)
EXPECTED = {
    "none": [],
    "p1_clone": ["walk0", "walk1"],
    "p1_ignores_pad": ["walk1", "look1"],
    "crosstalk_move": ["walk1"],
    "shared_aim": ["look0", "look1"],
    "shared_fire": ["fire0", "fire1"],
    "shared_reload": ["reload0", "reload1"],
    "same_viewport": ["split_view"],
    "views_swapped": ["split_view"],
    "static_cameras": ["split_view"],
    "no_bot_lines": ["bot_drove", "walk0"],
    "single_player": ["two_players", "startup"],
    "never_armed": ["startup"],
    "timeout": ["completed"],
    "no_weapon": ["fire0", "fire1", "reload0", "reload1"],
    "pads_swapped": [],
    "same_pad": ["separate_pads"],
    "p1_falls": ["no_fall"],
    "no_gap_line": ["split_pixels"],
    "bottom_blank": ["split_pixels"],
    "identical_halves": ["split_pixels"],
    "partner_invisible": ["partner_body"],
    "cinema_p0_only": ["shared_cinema"],
    "items_not_shared": ["shared_items"],
    "scanner_crash": ["scanner", "completed"],
    "shared_profile": ["own_profiles"],
    "swap_noop": ["live_swap"],
    "assert_hit": ["asserts"],
    "carry_lost_p1": ["carry_over"],
    "join_ignored": ["join_button"],
    "death_crash": ["death_restart", "completed"],
    "not_downed": ["downed_revive"],
    "no_revive": ["downed_revive"],
    "downed_attacks": ["downed_no_attack"],
    "mutation_shared": ["mutation_independent"],
    "mutation_failed": ["mutation_independent"],
    "mutant_no_attack": ["mutation_independent"],
    "mutant_body_missing": ["mutation_independent"],
    "pause_black": ["pause_view"],
}


def simulate(faults=(), settle_ms=DEFAULT_SETTLE_MS, cycles=DEFAULT_CYCLES, players=2):
    faults = set(faults)
    phases, end = build_phases(settle_ms, cycles)
    events = []
    for ph in phases:
        for (pad, ctrl, val) in ph.actions:
            events.append((ph.start, ph.end, pad, ctrl, val))
    quit_ms = end + SCANNER_MS + JOIN_MS + MUT_MS + DOWN_MS + DEATH_MS + 1000 + WARP_MS + 1000
    lines = []

    def emit(wall, qt, msg):
        lines.append("wall=%d qt=%d %s" % (wall, qt, msg))

    emit(100, -1, "QA bot starting (wall limit 1500 s, menu pulses 1)")
    emit(110, -1, "script 'x' loaded: %d events, quit at %d ms" % (len(events), quit_ms))
    emit(120, -1, "pad 0 attached: instance id 1, is_gamepad=1")
    emit(121, -1, "pad 1 attached: instance id 2, is_gamepad=1")
    emit(5000, -1, "state SM_PRESS_START_SCREEN -> SM_PROFILE_SELECT")
    emit(9000, -1, "state SM_LEVEL_SELECT -> SM_START_GAME")
    if "never_armed" in faults:
        emit(20000, -1, "wall-clock limit reached (armed=0), asking the game to exit")
        return "\n".join(lines) + "\n"
    emit(25000, -1, "campaign BeginGame done: players=%d level=1000" % players)
    # the opening scene of the level, before the bot is armed (full-screen view while it runs)
    for k in range(4):
        for i in range(players):
            if "single_player" in faults and i == 1:
                continue
            on = 0 if ("cinema_p0_only" in faults and i == 1) else 1
            emit(26000 + 500 * k, -1,
                 "player %d guid=%08X slot=%d pad=%d pos=%d,200,500 yaw=0 pitch=0 hp=100 weapon=none ammo=-1 inclip=-1 perclip=-1 cinema=%d" % (
                     i, 0xA0000 + i, i, i, 1000 + 100 * i, on))
        emit(26000 + 500 * k, -1, "view 0 viewport=0,0,%d,%d eye=1000,372,500 frames=%d views=1" % (SIM_W, SIM_H, 10 + k))
    emit(30000, 0, "armed: level running, no cinematic on any local player")
    if players > 1 and "single_player" not in faults:
        emit(30000, 0, "profile player 0: index=0 name=QA avatar=0")
        emit(30000, 0, "profile player 1: index=%s avatar=1" % ("0 name=QA" if "shared_profile" in faults else "1 name=Splitscreen P2"))
        emit(30000, 0, "share item=grenade p0 0->1 p1 0->%d" % (0 if "items_not_shared" in faults else 1))

    driver = {0: 0, 1: 1}
    if "pads_swapped" in faults:
        driver = {0: 1, 1: 0}

    class P(object):
        pass

    pl = []
    for i in range(players):
        p = P()
        p.x, p.y, p.z = 1000.0 + 100.0 * i, 200.0, 500.0
        p.ground = 200.0
        p.vy = 0.0
        p.yaw = 0.0                       # degrees
        p.hp = 100
        p.weapon = "none" if "no_weapon" in faults else "Weapon SMP"
        p.perclip = -1 if "no_weapon" in faults else 30
        p.inclip = -1 if "no_weapon" in faults else 30
        p.ammo = -1 if "no_weapon" in faults else 150
        p.fire_acc = 0.0
        p.reload_left = 0.0
        p.jump_prev = 0
        pl.append(p)
    def ctrl_state(t):
        st = {}
        for (s, e, pad, ctrl, val) in events:
            if s <= t < e:
                st[(pad, ctrl)] = val
        return st

    last_ctl = {}
    step = 50
    sample_every = 500
    view_every = 1000
    frames = 0
    t = 0
    while t <= quit_ms + 1000:
        st = ctrl_state(t)
        if "no_bot_lines" in faults:
            st = {}
        # bot log: report changes of the pad state like the real bot does
        keys = set(list(st.keys()) + list(last_ctl.keys()))
        if "no_bot_lines" not in faults:
            for (pad, ctrl) in sorted(keys):
                new = raw_value(ctrl, st[(pad, ctrl)]) if (pad, ctrl) in st else rest_value(ctrl)
                old = raw_value(ctrl, last_ctl[(pad, ctrl)]) if (pad, ctrl) in last_ctl else rest_value(ctrl)
                if new != old:
                    emit(30000 + t, t, "pad %d %s = %d" % (pad, ctrl, new))
        last_ctl = dict(st)

        dt = step / 1000.0
        for i, p in enumerate(pl):
            # which pad's controls act on this player
            pads = [pad for pad in (0, 1) if driver[pad] == i]
            if "p1_ignores_pad" in faults and i == 1:
                pads = []
            if "crosstalk_move" in faults and i == 0:
                pads = [0, 1]
            if "shared_aim" in faults:
                aim_pads = [0, 1]
            else:
                aim_pads = pads
            fwd = sum(st.get((pad, "ly"), 0) for pad in pads) / 100.0
            strafe = sum(st.get((pad, "lx"), 0) for pad in pads) / 100.0
            look = sum(st.get((pad, "rx"), 0) for pad in aim_pads) / 100.0
            jump = max([st.get((pad, "a"), 0) for pad in pads] + [0])
            fire_pads = [0, 1] if "shared_fire" in faults else pads
            fire = max([st.get((pad, "rt"), 0) for pad in fire_pads] + [0])
            rel_pads = [0, 1] if "shared_reload" in faults else pads
            rel = max([st.get((pad, "x"), 0) for pad in rel_pads] + [0])
            if "single_player" in faults and i == 1:
                continue
            yaw_r = math.radians(p.yaw)
            p.x += (math.sin(yaw_r) * fwd + math.cos(yaw_r) * strafe) * 300.0 * dt
            p.z += (math.cos(yaw_r) * fwd - math.sin(yaw_r) * strafe) * 300.0 * dt
            p.yaw = (p.yaw + look * 120.0 * dt) % 360.0
            if jump and not p.jump_prev and p.y <= p.ground + 0.1:
                p.vy = 350.0
            p.jump_prev = jump
            if "p1_falls" in faults and i == 1:
                p.y -= 3000.0 * dt              # spawned outside the world: no floor, keeps falling
            else:
                p.vy -= 980.0 * dt
                p.y = max(p.ground, p.y + p.vy * dt)
                if p.y <= p.ground:
                    p.vy = 0.0
            if p.inclip >= 0:
                if fire and p.inclip > 0:
                    p.fire_acc += dt
                    while p.fire_acc >= 0.1 and p.inclip > 0:
                        p.fire_acc -= 0.1
                        p.inclip -= 1
                        p.ammo -= 1
                if rel and p.reload_left <= 0 and p.inclip < p.perclip:
                    p.reload_left = 1.5
                if p.reload_left > 0:
                    p.reload_left -= dt
                    if p.reload_left <= 0:
                        take = p.perclip - p.inclip
                        p.inclip += take
        if "p1_clone" in faults and players > 1:
            c = pl[0]
            q = pl[1]
            q.x, q.y, q.z, q.yaw = c.x + 100.0, c.y, c.z, c.yaw
            q.inclip, q.ammo, q.reload_left = c.inclip, c.ammo, c.reload_left

        if t % sample_every == 0:
            for i, p in enumerate(pl):
                if "single_player" in faults and i == 1:
                    continue
                pad_id = i if "same_pad" not in faults else 0
                emit(30000 + t, t,
                     "player %d guid=%08X slot=%d pad=%d pos=%d,%d,%d yaw=%d pitch=0 hp=%d weapon=%s ammo=%d inclip=%d perclip=%d cinema=0" % (
                         i, 0xA0000 + i, i, pad_id, int(p.x), int(p.y), int(p.z),
                         int(p.yaw * 10), p.hp, p.weapon, p.ammo, p.inclip, p.perclip))
        if t % view_every == 0:
            frames += 30
            n_views = 1 if "single_player" in faults else 2
            for i in range(n_views):
                src = pl[i]
                if "views_swapped" in faults and n_views == 2:
                    src = pl[1 - i]
                if "static_cameras" in faults:
                    ex, ey, ez = 1000, 372, 500
                else:
                    ex, ey, ez = int(src.x), int(src.y + 172), int(src.z)
                if n_views == 1:
                    vp = (0, 0, SIM_W, SIM_H)
                elif "same_viewport" in faults:
                    vp = (0, 0, SIM_W, SIM_H)
                else:
                    vp = (0, 0, SIM_W, SIM_GAP0) if i == 0 else (0, SIM_GAP1, SIM_W, SIM_H)
                emit(30000 + t, t, "view %d viewport=%d,%d,%d,%d eye=%d,%d,%d frames=%d views=%d" % (
                    i, vp[0], vp[1], vp[2], vp[3], ex, ey, ez, frames, n_views))
                others = n_views - 1
                body = 0 if "partner_invisible" in faults else others
                drawn = 0 if (body == 0 or (t // view_every) % 3 == 0) else 20
                emit(30000 + t, t, "bodies view=%d drawn=%d frames=30 others=%d with_body=%d" % (
                    i, drawn, others, body))
        t += step

    if players > 1 and "single_player" not in faults:
        emit(30000 + end, end, "swaptest flipped: player 0 pad=%s" % ("0 player 1 pad=1" if "swap_noop" in faults else "1 player 1 pad=0"))
        emit(30000 + end, end, "swaptest restored: player 0 pad=0 player 1 pad=1")
        if "assert_hit" in faults:
            emit(30000 + end, end, "ASSERT #1 PlayerLoco.cpp:198 expr=[0] msg=[Weapon not supported in mp!]")
        emit(30000 + end, end, "ASSERT #2 x_threads_posix.cpp:425 expr=[Joined] msg=[join timeout]")
        emit(30000 + end, end, "command scanner (hook set)")
        j0 = end + SCANNER_MS
        for k, x1 in ((0, 5100), (4, 6200), (6, 7000), (7, 7000), (8, 7000 if "join_ignored" in faults else 5090),
                      (9, 7000 if "join_ignored" in faults else 5090)):
            tt = j0 + 500 * k
            for i, x in ((0, 5000), (1, x1)):
                emit(30000 + tt, tt, "player %d guid=%08X slot=%d pad=%d pos=%d,200,500 yaw=0 pitch=0 hp=100 weapon=SCN ammo=0 inclip=0 perclip=0 cinema=0" % (
                    i, 0xA0000 + i, i, i, x))
        emit(30000 + end + SCANNER_MS - 1500, end + SCANNER_MS - 1500,
             "carryover player 0: scanner=%d pistol=%d" % ((0, 0) if "carry_lost_p1" in faults else (1, 1)))
        emit(30000 + end + SCANNER_MS - 1500, end + SCANNER_MS - 1500, "carryover player 1: scanner=1 pistol=1")
        for i in range(players):
            emit(30000 + end, end, "scanner player %d: in inventory=1" % i)
        for k in range(1, (SCANNER_MS // 500)):
            tt = end + 500 * k
            for i in range(players):
                emit(30000 + tt, tt, "player %d guid=%08X slot=%d pad=%d pos=%d,200,500 yaw=0 pitch=0 hp=100 weapon=%s ammo=0 inclip=0 perclip=0 cinema=0" % (
                    i, 0xA0000 + i, i, i, 5000 + 100 * i, "SCN" if tt >= end + SCANNER_SWITCH_MS else "EGL"))
    if players > 1 and "single_player" not in faults:
        mt = end + SCANNER_MS + JOIN_MS
        emit(30000 + mt, mt, "mutate1 player 1: changing")
        if "mutation_shared" in faults:
            emit(30000 + mt + MUT_STAT1_MS, mt + MUT_STAT1_MS, "mutstat p0=1 p1=1 speed=65 abil0=15 abil1=15")
        elif "mutation_failed" in faults:
            emit(30000 + mt + MUT_STAT1_MS, mt + MUT_STAT1_MS, "mutstat p0=0 p1=0 speed=100 abil0=15 abil1=15")
        elif "mutant_no_attack" in faults:
            emit(30000 + mt + MUT_STAT1_MS, mt + MUT_STAT1_MS, "mutstat p0=0 p1=1 speed=100 abil0=15 abil1=8")
        else:
            emit(30000 + mt + MUT_STAT1_MS, mt + MUT_STAT1_MS, "mutstat p0=0 p1=1 speed=100 abil0=15 abil1=15 body1=%x" % (1 if "mutant_body_missing" in faults else 2))
        emit(30000 + mt + MUT_STAT2_MS, mt + MUT_STAT2_MS, "mutstat p0=0 p1=0 speed=100 abil0=15 abil1=15 body1=1")
        d = end + SCANNER_MS + JOIN_MS + MUT_MS
        emit(30000 + d, d, "down player 1: lethal damage")
        if "not_downed" not in faults:
            emit(30000 + d, d, "coop downed slot=1 bleedout=25.0")
            if "no_revive" not in faults:
                emit(30000 + d + DOWN_HOLD_START_MS, d + DOWN_HOLD_START_MS, "coop revive_start slot=1 left=24.0")
                emit(30000 + d + DOWN_HOLD_START_MS + 4000, d + DOWN_HOLD_START_MS + 4000, "coop revived slot=1 health=35")
        emit(30000 + d + DOWN_AGAIN_MS, d + DOWN_AGAIN_MS, "down player 1: lethal damage")
        if "not_downed" not in faults:
            emit(30000 + d + DOWN_AGAIN_MS, d + DOWN_AGAIN_MS, "coop downed slot=1 bleedout=25.0")
        dn = 0 if "not_downed" in faults else 1
        emit(30000 + d + DOWN_STAT1_MS, d + DOWN_STAT1_MS, "downstat player 1: downed=%d grenades=2 rounds=7" % dn)
        emit(30000 + d + DOWN_STAT2_MS, d + DOWN_STAT2_MS, "downstat player 1: downed=%d grenades=%d rounds=%d" % (
            dn, 1 if "downed_attacks" in faults else 2, 6 if "downed_attacks" in faults else 7))
        k = end + SCANNER_MS + JOIN_MS + MUT_MS + DOWN_MS
        emit(30000 + k, k, "kill player 0: dying")
        if "death_crash" not in faults:
            emit(30000 + k + 9000, k + 9000, "campaign BeginGame done: players=%d level=1000" % players)
    if "timeout" in faults:
        emit(30000 + t, t, "wall-clock limit reached (armed=1), asking the game to exit")
    elif "scanner_crash" in faults or "death_crash" in faults:
        pass
    else:
        if players > 1 and "single_player" not in faults:
            pq = end + SCANNER_MS + JOIN_MS + MUT_MS + DOWN_MS + DEATH_MS
            emit(30000 + pq, pq, "pause: on paused=1")
            emit(30000 + pq + 15000, pq, "pause: off paused=0")
        emit(30000 + quit_ms + 15000, quit_ms, "script finished, asking the game to exit")
    return "\n".join(lines) + "\n"


def synth_shots(directory, faults=(), n=8, w=SIM_W, h=SIM_H, pause_wall=None):
    """Screenshots the way a correct split screen looks (two noisy halves, a black gap), or broken."""
    import os
    import random
    faults = set(faults)
    rows_top, rows_bot, rows_gap = SIM_GAP0, h - SIM_GAP1, SIM_GAP1 - SIM_GAP0

    def noise(rnd, rows):
        size = rows * w * 3
        return rnd.getrandbits(8 * size).to_bytes(size, "little")

    for i in range(n):
        rnd = random.Random(1000 + i)
        top = noise(rnd, rows_top)
        bot = noise(rnd, rows_bot)
        if "identical_halves" in faults:
            bot = top[:rows_bot * w * 3]
        if "bottom_blank" in faults:
            bot = bytes(len(bot))
        gap = bytes(rows_gap * w * 3)
        if "no_gap_line" in faults:
            gap = noise(rnd, rows_gap)
        paused_shot = pause_wall is not None and i >= n - 2
        if paused_shot and "pause_black" in faults:
            top = bytes(len(top))
        if pause_wall is not None:
            wall = (pause_wall + 3000 + 5000 * (i - (n - 2))) if paused_shot else (pause_wall - 60000 + 5000 * i)
            with open(os.path.join(directory, "times.txt"), "a") as tf:
                tf.write("shot%02d %d\n" % (i, wall))
        with open(os.path.join(directory, "shot%02d.ppm" % i), "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            f.write(top + gap + bot)


def sim_shots(log, faults=()):
    import shutil
    import tempfile
    d = tempfile.mkdtemp(prefix="coopqa-shots-")
    try:
        synth_shots(d, faults, pause_wall=(log.pause_on[0] if log.pause_on else None))
        return parse_shots("\n".join(shots_from_dir(d, log)))
    finally:
        shutil.rmtree(d, ignore_errors=True)


def selftest():
    ok_all = True
    phases, _ = build_phases()

    # the screenshot reader: a tiny picture whose numbers are known
    import os
    import tempfile
    tmp = tempfile.mkdtemp(prefix="coopqa-ppm-")
    try:
        path = os.path.join(tmp, "t.ppm")
        with open(path, "wb") as f:
            f.write(b"P6\n# comment\n4 2\n255\n" + bytes([10, 20, 30] * 4 + [255, 255, 255] * 4))
        w, h, body = read_ppm(path)
        lum0 = _lum_row(body, w, 0)
        lum1 = _lum_row(body, w, 1)
        rd = (w, h, len(body)) == (4, 2, 24) and lum0 == [(10 * 299 + 20 * 587 + 30 * 114) // 1000] * 4 == [18] * 4 and lum1 == [255] * 4
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    print(" %-4s screenshot reader" % ("ok" if rd else "BAD"))
    ok_all &= rd

    # the script generator and the parser agree
    txt = make_script()
    back = phases_from_script_text(txt)
    same = back is not None and [(p.kind, p.cycle, p.start, p.end) for p in back] == [(p.kind, p.cycle, p.start, p.end) for p in phases]
    print(" %-4s script header round trip" % ("ok" if same else "BAD"))
    ok_all &= same

    # the raw-value conversion matches input_sdl.cpp (+100 forward is negative on Y; triggers rest at -32768)
    conv = (raw_value("ly", 100) == -32767 and raw_value("lx", -100) == -32767 and
            raw_value("rt", 100) == 32767 and rest_value("rt") == -32768 and raw_value("a", 1) == 1)
    print(" %-4s raw value conversion" % ("ok" if conv else "BAD"))
    ok_all &= conv

    for fault in FAULTS:
        log = parse_log(simulate([fault] if fault != "none" else []))
        results, required = score(log, phases, "coop", sim_shots(log, [fault]))
        ok, failed, noev = verdict(results, required)
        by = {r.name: r.status for r in results}
        expected = EXPECTED[fault]
        if fault in ("none", "pads_swapped"):
            good = ok
            why = "must PASS" if good else "must PASS but got FAIL: %s %s" % (failed, noev)
        else:
            not_pass = [n for n in expected if by.get(n) != PASS]
            good = (not ok) and len(not_pass) == len(expected)
            why = "must FAIL on %s" % expected if good else "expected non-PASS on %s but statuses are %s (verdict ok=%s)" % (
                expected, dict((n, by.get(n)) for n in expected), ok)
        print(" %-4s fault %-15s %s" % ("ok" if good else "BAD", fault, why))
        ok_all &= good

    # bodies: a bystander pushed by the walker's body is not a control leak; one that moves on its
    # own while far away still is
    ph = [p for p in phases if p.kind == "walk1"][0]
    def mk(push):
        lg = Log()
        lg.players = {0: [], 1: []}
        for k in range(0, 9):
            t = ph.start + k * (ph.check_end - ph.start) // 8
            x1 = 1000 - 700 + 100 * k if push else 300 + 100 * k
            x0 = 1000 if (not push or x1 < 900) else x1 + 100
            if not push and k > 4:
                x0 = 1000 + 20 * k
            lg.players[0].append({"qt": t, "x": x0, "y": 0, "z": 0})
            lg.players[1].append({"qt": t, "x": x1, "y": 0, "z": 0})
        return lg
    e_push, e_far = ev_walk(mk(True), ph), ev_walk(mk(False), ph)
    good = e_push[1] and "pushed" in e_push[2] and not e_far[1]
    print(" %-4s body push excused, independent drift not: %s / %s" % ("ok" if good else "BAD", e_push[2], e_far[2]))
    ok_all &= good

    # the controller-swap setting: a swapped run passes as coop-swapped, an unswapped one fails it
    log = parse_log(simulate(["pads_swapped"]))
    ok, failed, noev = verdict(*score(log, phases, "coop-swapped", sim_shots(log, [])))
    print(" %-4s swapped pads judged as coop-swapped must PASS: %s" % ("ok" if ok else "BAD", "PASS" if ok else (failed, noev)))
    ok_all &= ok
    log = parse_log(simulate([]))
    ok, failed, noev = verdict(*score(log, phases, "coop-swapped", sim_shots(log, [])))
    good = (not ok) and failed == ["pad_mapping"]
    print(" %-4s unswapped pads judged as coop-swapped must FAIL on pad_mapping: %s" % ("ok" if good else "BAD", failed or "PASS"))
    ok_all &= good

    # the single-player control
    log = parse_log(simulate(["single_player"], players=1))
    one_shots = sim_shots(log, ["no_gap_line"])
    results, required = score(log, phases, "single", one_shots)
    ok, failed, noev = verdict(results, required)
    print(" %-4s control run, one player, expect=single: %s" % ("ok" if ok else "BAD", "PASS" if ok else (failed, noev)))
    ok_all &= ok
    results, required = score(log, phases, "coop", one_shots)
    ok, failed, noev = verdict(results, required)
    print(" %-4s control run judged as co-op must FAIL: %s" % ("ok" if not ok else "BAD", "FAIL" if not ok else "PASS"))
    ok_all &= (not ok)
    log = parse_log(simulate([]))
    results, required = score(log, phases, "single", sim_shots(log, []))
    ok, failed, noev = verdict(results, required)
    print(" %-4s a two-player run judged as single must FAIL: %s" % ("ok" if not ok else "BAD", "FAIL" if not ok else "PASS"))
    ok_all &= (not ok)

    print("SELFTEST: %s" % ("PASS" if ok_all else "FAIL"))
    return 0 if ok_all else 1


# ----------------------------------------------------------------------------
# Command line
# ----------------------------------------------------------------------------

def audio_report(text):
    """Cinema sound tracks: how long each one played against its length, and the voices in use.
    A track that stops well before its length was cut off (the owner's co-op cutscene)."""
    out, starts = [], {}
    voices = []
    dialog = []
    levels = {}
    for line in text.splitlines():
        m = re.match(r"^wall=(\d+) qt=(-?\d+) (.*)$", line)
        if not m:
            continue
        wall, msg = int(m.group(1)), m.group(3)
        a = re.match(r"cinema audio start (\S+) length=(-?\d+) ms", msg)
        if a:
            starts[a.group(1)] = (wall, int(a.group(2)))
            continue
        e = re.match(r"cinema audio end (\S+)", msg)
        if e and e.group(1) in starts:
            w0, length = starts.pop(e.group(1))
            played = wall - w0
            verdict = "CUT" if (length > 0 and played < 0.9 * length - 300) else "ok"
            out.append("%-4s %s: played %d ms of %d ms" % (verdict, e.group(1), played, length))
            continue
        f = re.match(r"cinema audio (failed to start|lost while warming up) (\S+)", msg)
        if f:
            out.append("FAIL %s: %s" % (f.group(2), f.group(1)))
            continue
        lv = re.match(r"cinema audio level (\S+) vol=(-?[\d.]+) pos3d=(-?[\d.]+) zone=(-?\d+) marker=(-?\d+) at=(\S+) 2d=(\d)", msg)
        if lv:
            levels.setdefault(lv.group(1), []).append((float(lv.group(2)), float(lv.group(3)), lv.group(4), lv.group(5), lv.group(7)))
            continue
        d = re.match(r"dialog (stream|voice|stop) (\S+) id=([0-9a-f]+)", msg)
        if d:
            dialog.append((wall, d.group(1), d.group(2), d.group(3)))
            continue
        v = re.match(r"audio voices=(\d+) top=(\S+) x(\d+)", msg)
        if v:
            voices.append((int(v.group(1)), v.group(2), int(v.group(3))))
    for name, (w0, length) in starts.items():
        out.append("OPEN %s: started, no end logged (length %d ms)" % (name, length))
    for name, lst in levels.items():
        seq = " ".join("%.1f" % v for (v, p3, z, mk, d2) in lst)
        marks = " ".join(sorted(set("m%s/z%s/2d%s" % (mk, z, d2) for (v, p3, z, mk, d2) in lst)))
        out.append("track %s volume every 0.5 s: %s" % (name, seq))
        out.append("track %s markers/zones: %s" % (name, marks))
    if dialog:
        from collections import Counter
        plays = Counter((k, n) for (_, k, n, i) in dialog if k != "stop")
        failed = Counter((k, n) for (_, k, n, i) in dialog if k != "stop" and int(i, 16) == 0)
        stops = sum(1 for d in dialog if d[1] == "stop")
        out.append("dialog lines started: %d (%d could not start), stop calls: %d" % (
            sum(plays.values()), sum(failed.values()), stops))
        for (k, n), c in sorted(plays.items()):
            out.append("  %-6s %-40s x%d%s" % (k, n, c, ("  (%d failed)" % failed[(k, n)]) if failed[(k, n)] else ""))
    if voices:
        mx = max(voices)
        out.append("voices in use: max %d (top %s x%d), last %d" % (mx[0], mx[1], mx[2], voices[-1][0]))
    if not out:
        out.append("no cinema sound and no voice lines in the log")
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")

    g = sub.add_parser("gen")
    g.add_argument("out")
    g.add_argument("--settle", type=int, default=DEFAULT_SETTLE_MS)
    g.add_argument("--cycles", type=int, default=DEFAULT_CYCLES)
    g.add_argument("--sweep", action="store_true", help="the short script of the all-levels sweep")
    g.add_argument("--chain", action="store_true", help="a level, the main menu, then the next level")
    sw = sub.add_parser("sweep")
    sw.add_argument("log")
    sw.add_argument("--level", type=int, required=True)
    sw.add_argument("--probe")
    sw.add_argument("--shots")

    s = sub.add_parser("score")
    s.add_argument("log")
    s.add_argument("--script")
    s.add_argument("--expect", choices=["coop", "coop-swapped", "single"], default="coop")
    s.add_argument("--json")
    s.add_argument("--shots", help="statistics written by the shots command")

    h = sub.add_parser("shots")
    h.add_argument("dir", help="directory with the *.ppm screenshots (they never leave the test machine)")
    h.add_argument("--log", required=True, help="the game's a51_qa_bot.log, for the viewport rectangles")
    h.add_argument("--out", required=True, help="text file with numbers only")

    m = sub.add_parser("sim")
    m.add_argument("out")
    m.add_argument("--fault", action="append", default=[], choices=FAULTS)
    m.add_argument("--players", type=int, default=2)

    sub.add_parser("selftest")
    au = sub.add_parser("audio")
    au.add_argument("log")

    a = ap.parse_args(argv)
    if a.cmd == "gen" and a.chain:
        with open(a.out, "w") as f:
            f.write(make_chain_script())
        print("wrote %s: level, main menu, next level" % a.out)
        return 0
    if a.cmd == "gen":
        with open(a.out, "w") as f:
            f.write(make_script(a.settle, a.cycles, sweep=getattr(a, "sweep", False)))
        phases, end = build_phases(a.settle, a.cycles)
        print("wrote %s: %d phases, %.0f s of game time" % (a.out, len(phases), end / 1000.0))
        return 0
    if a.cmd == "sweep":
        text = open(a.log, errors="replace").read() if os.path.exists(a.log) else ""
        probe = open(a.probe, errors="replace").read() if a.probe and os.path.exists(a.probe) else ""
        shots = parse_shots(open(a.shots).read()) if a.shots and os.path.exists(a.shots) else None
        print(sweep_report(parse_log(text), a.level, probe, shots))
        return 0
    if a.cmd == "audio":
        sys.stdout.write(audio_report(open(a.log, encoding="utf-8", errors="replace").read()))
        return 0
    if a.cmd == "selftest":
        return selftest()
    if a.cmd == "sim":
        faults = [f for f in a.fault if f != "none"]
        with open(a.out, "w") as f:
            f.write(simulate(faults, players=a.players))
        print("wrote simulated log %s (faults: %s)" % (a.out, faults or "none"))
        return 0
    if a.cmd == "shots":
        try:
            with open(a.log, "r", errors="replace") as f:
                log = parse_log(f.read())
        except IOError as e:
            print("cannot read %s: %s" % (a.log, e))
            return 1
        lines = shots_from_dir(a.dir, log)
        with open(a.out, "w") as f:
            f.write("\n".join(lines) + "\n")
        print("coopqa shots: %d lines written to %s" % (len(lines), a.out))
        return 0
    if a.cmd == "score":
        try:
            with open(a.log, "r", errors="replace") as f:
                text = f.read()
        except IOError as e:
            print("cannot read %s: %s" % (a.log, e))
            print("RESULT: FAIL (no log: the game never wrote one)")
            return 1
        phases = None
        if a.script:
            with open(a.script) as f:
                phases = phases_from_script_text(f.read())
        if phases is None:
            phases = build_phases()[0]
        log = parse_log(text)
        shots = None
        if a.shots:
            try:
                with open(a.shots, "r", errors="replace") as f:
                    shots = parse_shots(f.read())
            except IOError:
                shots = None
        results, required = score(log, phases, a.expect, shots)
        ok, failed, noev = verdict(results, required)
        print("coopqa score: expect=%s log=%s (%d lines, %d not understood)" % (a.expect, a.log, log.lines, log.unparsed))
        print(render(results, required, ok, failed, noev))
        if a.json:
            with open(a.json, "w") as f:
                json.dump({"expect": a.expect, "ok": ok, "failed": failed, "no_evidence": noev,
                           "results": [r.as_dict() for r in results]}, f, indent=1)
        return 0 if ok else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
