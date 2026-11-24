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


def make_script(settle_ms=DEFAULT_SETTLE_MS, cycles=DEFAULT_CYCLES):
    phases, end = build_phases(settle_ms, cycles)
    lines = [
        "# coopqa script v1 settle=%d cycles=%d" % (settle_ms, cycles),
        "# start_ms end_ms pad control value   (game time since the bot was armed)",
    ]
    for p in phases:
        lines.append("# %s cycle %d" % (p.kind, p.cycle))
        for (pad, ctrl, val) in p.actions:
            lines.append("%d %d %d %s %d" % (p.start, p.end, pad, ctrl, val))
    lines.append("quit %d" % (end + 1000))
    return "\n".join(lines) + "\n"


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
STATE_RE = re.compile(r"^state (\S+) -> (\S+)$")


class Log(object):
    def __init__(self):
        self.players = {}       # index -> list of sample dicts (sorted by qt)
        self.views = {}         # index -> list of view dicts
        self.controls = []      # (qt, pad, name, raw)
        self.states = []        # (wall, from, to)
        self.armed_wall = None
        self.finished = False
        self.wall_limit = False
        self.errors = []
        self.attached = {}      # pad -> is_gamepad
        self.missing = []       # "player N missing" lines
        self.asserts = []       # assertions the game hit (logged and skipped by the bot)
        self.lines = 0
        self.unparsed = 0
        self.perf = []          # (wall ms, frames, window ms) from the bot's frame counter
        self.level = None       # level id the campaign started
        self.declared_players = None


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
        elif msg.startswith("armed:"):
            if log.armed_wall is None:
                log.armed_wall = wall
        elif msg.startswith("script finished"):
            log.finished = True
        elif msg.startswith("wall-clock limit reached"):
            log.wall_limit = True
        elif msg.startswith("ERROR"):
            log.errors.append(msg)
        elif msg.startswith("ASSERT #"):
            log.asserts.append(msg)
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
                 "fire0", "fire1", "reload0", "reload1", "pad_mapping", "split_view", "split_pixels"]
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


def ev_walk(log, ph):
    pr = _pair(log, ph)
    if pr is None:
        return None
    a, b = pr
    m = [hdist(a[i], b[i]) for i in (0, 1)]
    actor = 0 if m[0] >= m[1] else 1
    return (m[actor] >= MOVE_MIN, m[1 - actor] <= STILL_MAX,
            "P0 %dcm P1 %dcm" % (m[0], m[1]), actor if m[actor] >= MOVE_MIN else None)


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


def check_two_players(log):
    p0, p1 = log.players.get(0, []), log.players.get(1, [])
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
        for s in lst:
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
    v0, v1 = log.views.get(0, []), log.views.get(1, [])
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
            p = [at(log.players.get(k, []), v["qt"]) for k in (0, 1)]
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


def check_perf(log):
    """Information only: how fast the game ran (a software renderer can be slow)."""
    if not log.perf:
        return Result("perf", INFO, "no frame-rate lines in the log")
    rates = [1000.0 * f / max(1, ms) for (_, f, ms) in log.perf]
    return Result("perf", INFO, "%d samples, frame rate min %.1f, mean %.1f, max %.1f per second" % (
        len(rates), min(rates), sum(rates) / len(rates), max(rates)))


def check_cinema(log):
    """Information only: a cutscene during the script explains missing evidence."""
    during = []
    for i, lst in sorted(log.players.items()):
        n = len([s for s in lst if s["qt"] >= 0 and s["cinema"]])
        if n:
            during.append("player %d: %d samples" % (i, n))
    return Result("cinema", INFO, ("a cinematic ran during the script (%s)" % "; ".join(during)) if during
                  else "no cinematic during the script")


def check_asserts(log):
    if not log.asserts:
        return Result("asserts", INFO, "the game hit no assertion")
    first = "; ".join(a[len("ASSERT "):][:160] for a in log.asserts[:3])
    return Result("asserts", INFO, "%d assertion(s) were logged and skipped. First: %s" % (len(log.asserts), first))


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
    for i, name in enumerate(names):
        try:
            s = shot_stats(os.path.join(directory, name), vp0, vp1)
        except (ValueError, IOError) as e:
            lines.append("shot %d unreadable: %s" % (i, e))
            continue
        lines.append("shot %d size=%dx%d gap_rows=%d-%d %s" % (
            i, s["w"], s["h"], s["bar_y0"], s["bar_y1"],
            " ".join("%s=%.1f" % (k, s[k]) for k in SHOT_KEYS)))
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
    results = []
    results.append(check_startup(log, expect))
    results.append(check_asserts(log))
    results.append(check_cinema(log))
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
    results.append(check_split_view(log))
    results.append(check_split_pixels(shots))
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
}


def simulate(faults=(), settle_ms=DEFAULT_SETTLE_MS, cycles=DEFAULT_CYCLES, players=2):
    faults = set(faults)
    phases, end = build_phases(settle_ms, cycles)
    events = []
    for ph in phases:
        for (pad, ctrl, val) in ph.actions:
            events.append((ph.start, ph.end, pad, ctrl, val))
    quit_ms = end + 1000
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
    emit(30000, 0, "armed: level running, no cinematic on any local player")

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
        t += step

    if "timeout" in faults:
        emit(30000 + t, t, "wall-clock limit reached (armed=1), asking the game to exit")
    else:
        emit(30000 + quit_ms, quit_ms, "script finished, asking the game to exit")
    return "\n".join(lines) + "\n"


def synth_shots(directory, faults=(), n=8, w=SIM_W, h=SIM_H):
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
        with open(os.path.join(directory, "shot%02d.ppm" % i), "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            f.write(top + gap + bot)


def sim_shots(log, faults=()):
    import shutil
    import tempfile
    d = tempfile.mkdtemp(prefix="coopqa-shots-")
    try:
        synth_shots(d, faults)
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

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")

    g = sub.add_parser("gen")
    g.add_argument("out")
    g.add_argument("--settle", type=int, default=DEFAULT_SETTLE_MS)
    g.add_argument("--cycles", type=int, default=DEFAULT_CYCLES)

    s = sub.add_parser("score")
    s.add_argument("log")
    s.add_argument("--script")
    s.add_argument("--expect", choices=["coop", "single"], default="coop")
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

    a = ap.parse_args(argv)
    if a.cmd == "gen":
        with open(a.out, "w") as f:
            f.write(make_script(a.settle, a.cycles))
        phases, end = build_phases(a.settle, a.cycles)
        print("wrote %s: %d phases, %.0f s of game time" % (a.out, len(phases), end / 1000.0))
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
