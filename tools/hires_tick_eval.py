#!/usr/bin/env python3
"""Evaluator for the per-tick log lines of the hi-res driver (done criteria H2 b-d).

Usage:
  python tools/hires_tick_eval.py eval <game log> [--rate 30|60] [--seat N|all]
         [--allow-missing hit] [--expected state=clip] [--json out.json]
  python tools/hires_tick_eval.py selftest

Python standard library only.  The log words are NOT fixed yet: everything that
depends on them lives in LINE_PREFIX_RE and FIELD_TABLE below.

Exit codes: 0 PASS, 1 FAIL, 2 usage error or no hires line found.
"""

import contextlib
import io
import json
import re
import statistics
import sys

# --------------------------------------------------------------------------
# Log words (PROVISIONAL, map to the engine's real words here)
# --------------------------------------------------------------------------
# Finds the line; everything after the match is "key value ..." tokens.
LINE_PREFIX_RE = re.compile(r"\[CTR Engine\]\s*hires char:\s*(?P<rest>.*)$")

# our field name -> (word in the log, kind, number of values)
FIELD_TABLE = {
    "vb":      ("vb",      "int",   1),
    "seat":    ("seat",    "int",   1),
    "state":   ("state",   "str",   1),
    "clip":    ("clip",    "str",   1),
    "blend":   ("blend",   "float", 1),
    "prev":    ("prev",    "str",   1),
    "timer":   ("timer",   "int",   1),
    "want":    ("want",    "str",   1),
    "t":       ("t",       "float", 1),
    "steerclip": ("steerclip", "str", 1),
    "steerRaw":  ("steerRaw",  "int", 1),
    "air":     ("air",     "int",   1),
    "kart":    ("kart",    "int",   1),
    "drift":   ("drift",   "int",   1),
    "rank":    ("rank",    "int",   1),
    "roll":    ("roll",    "float", 1),
    "pos":     ("pos",     "float", 3),   # x y z
    "wheels":  ("wheels",  "str",   1),   # own | standard
    "steer":   ("steer",   "float", 1),
    "yaw":     ("yaw",     "float", 1),
    "yawgame": ("yawgame", "float", 1),
    "omega":   ("omega",   "float", 1),
    "radius":  ("radius",  "float", 1),
    "v":       ("v",       "float", 1),
    "box":     ("box",     "float", 4),   # x0 y0 x1 y1 (screen fractions)
}
REQUIRED_FIELDS = ("vb", "seat", "state", "clip")
NONE_WORDS = {"prev": "none", "steerclip": "none"}   # this word means "no value" -> None

# --------------------------------------------------------------------------
# Thresholds and tables
# --------------------------------------------------------------------------
CLIP_SHARE_MIN = 0.99            # criterion 1: share of lines with the expected clip
REQUIRED_STATES = ("steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit")
EXPECTED_CLIP = {                # state -> clip; default identity
    s: s for s in ("idle", "steer_left", "steer_right", "jump", "drift_left",
                   "drift_right", "hit", "podium_1", "podium_2", "podium_3",
                   "lose", "menu")
}
RAMP_MIN_FRAMES_60 = 4           # criterion 2: ramp covers >= 4 frames at 60 Hz
BLEND_DECREASE_EPS = 1e-6
WHEEL_SLIP_MAX = 0.05            # criterion 3: mean |omega*radius - v| / v
STEER_DEADZONE = 0.05            # |steer| above it in state base must draw a steer clip
STEER_DIFF_MAX = 0.01            # rad, max |yaw - yawgame|

# --------------------------------------------------------------------------
# Parser
# --------------------------------------------------------------------------
_WORD_TO_FIELD = {w: (f, kind, n) for f, (w, kind, n) in FIELD_TABLE.items()}


def _conv(kind, tok):
    try:
        if kind == "int":
            return int(tok)
        if kind == "float":
            return float(tok)
    except ValueError:
        return None
    return tok


def _derive(rec):
    """Add eval_state / eval_clip / eval_expected / eval_ok to a record.

    The engine's states (base, jump, ..., forced, finished) are not clip names.
    A record without "want" is the provisional format: state and clip as logged.
    """
    state, clip = rec["state"], rec["clip"]
    exp = None
    ok = True
    if rec.get("want") is None:
        es, ec = state, clip
    elif state == "base":
        sc = rec.get("steerclip")
        if sc in ("steer_left", "steer_right"):
            es, ec = sc, sc
            steer = rec.get("steer")
            if steer is not None and ((sc == "steer_left" and steer < 0)
                                      or (sc == "steer_right" and steer > 0)):
                ok = False      # sign mismatch: + = left
        else:
            steer = rec.get("steer")
            if steer is not None and abs(steer) > STEER_DEADZONE:
                # steering, but no steer clip drawn: a wrong line of that steer state
                es, ec = ("steer_left" if steer > 0 else "steer_right"), "none"
            else:
                es, ec = "idle", clip
    elif state == "finished":
        rank = rec.get("rank")
        if rank is None:
            es = "finished"
        else:
            es = "podium_%d" % (rank + 1) if 0 <= rank <= 2 else "lose"
        ec = clip
    elif state == "forced":
        es, ec, exp = "forced:" + rec["want"], clip, rec["want"]
    else:
        es, ec = state, clip
    rec["eval_state"], rec["eval_clip"] = es, ec
    rec["eval_expected"], rec["eval_ok"] = exp, ok
    return rec


def eval_view(rec):
    """(eval_state, eval_clip) of a record: what the criteria compare."""
    if "eval_state" not in rec:
        _derive(rec)
    return rec["eval_state"], rec["eval_clip"]


def parse_hires_lines(lines):
    """Parse game log lines; return one dict per hires line (see FIELD_TABLE)."""
    out = []
    for raw in lines:
        m = LINE_PREFIX_RE.search(raw)
        if not m:
            continue
        toks = m.group("rest").split()
        rec = {f: None for f in FIELD_TABLE}
        extra = {}
        i = 0
        while i < len(toks):
            word = toks[i]
            info = _WORD_TO_FIELD.get(word)
            if info is None:
                if i + 1 < len(toks):
                    extra[word] = toks[i + 1]
                    i += 2
                else:
                    extra[word] = ""
                    i += 1
                continue
            field, kind, n = info
            vals = toks[i + 1:i + 1 + n]
            i += 1 + n
            if len(vals) < n:
                continue
            if n == 1:
                rec[field] = _conv(kind, vals[0])
            else:
                conv = [_conv(kind, t) for t in vals]
                rec[field] = tuple(conv) if None not in conv else None
        if any(rec[k] is None for k in REQUIRED_FIELDS):
            continue
        rec["raw"] = raw.rstrip("\r\n")
        rec["extra"] = extra
        for f, w in NONE_WORDS.items():
            if rec[f] == w:
                rec[f] = None
        _derive(rec)
        out.append(rec)
    return out


# --------------------------------------------------------------------------
# Evaluation
# --------------------------------------------------------------------------
def _analyse_ramps(records, rate_hz):
    """Per seat: follow every cross-fade until blend reaches 1.0."""
    min_lines = RAMP_MIN_FRAMES_60 * rate_hz / 60.0
    by_seat = {}
    for r in records:
        by_seat.setdefault(r["seat"], []).append(r)
    ramps = []          # dicts: seat, lines (blend<1), steps, monotonic, status
    for seat, recs in sorted(by_seat.items()):
        cur = None      # open ramp
        prev_clip = None
        prev_blend = None
        for r in recs:
            b = r["blend"]
            if b is None:
                continue
            changed = prev_clip is not None and r["clip"] != prev_clip
            if cur is not None:
                if changed:
                    cur["status"] = "interrupted"
                    ramps.append(cur)
                    cur = None
                else:
                    if b < prev_blend - BLEND_DECREASE_EPS:
                        cur["monotonic"] = False
                    cur["steps"].append(abs(b - prev_blend))
                    if b >= 1.0:
                        cur["status"] = "done"
                        ramps.append(cur)
                        cur = None
                    else:
                        cur["lines"] += 1
            if cur is None and (changed or b < 1.0):
                if b >= 1.0:       # clip changed, no cross-fade at all
                    ramps.append({"seat": seat, "lines": 0, "steps": [],
                                  "monotonic": True, "status": "done"})
                else:
                    cur = {"seat": seat, "lines": 1, "steps": [], "monotonic": True,
                           "status": "open"}
            prev_clip, prev_blend = r["clip"], b
        if cur is not None:
            cur["status"] = "interrupted"
            ramps.append(cur)
    for rp in ramps:
        rp["frames"] = rp["lines"] * 60.0 / rate_hz
        rp["ok"] = (rp["status"] == "interrupted"
                    or (rp["monotonic"] and rp["lines"] >= min_lines - 1e-9))
    return ramps


def evaluate(records, rate_hz=30, expected=None, require_states=None,
             allow_missing=(), v_min=0.5):
    """Evaluate parsed records; returns a dict with all numbers and verdicts."""
    exp = dict(EXPECTED_CLIP)
    if expected:
        exp.update(expected)
    req = tuple(REQUIRED_STATES if require_states is None else require_states)
    allow = set(allow_missing or ())

    # criterion 1
    per_state = {}
    for r in records:
        es, ec = eval_view(r)
        want = exp[es] if es in exp else (r["eval_expected"] or es)
        s = per_state.setdefault(es, {"lines": 0, "excluded": 0,
                                      "counted": 0, "matching": 0, "expected_clip": want})
        s["lines"] += 1
        if r["blend"] is not None and r["blend"] < 1.0:
            s["excluded"] += 1
            continue
        s["counted"] += 1
        if ec == want and r["eval_ok"]:
            s["matching"] += 1
    for st, s in per_state.items():
        s["share"] = (s["matching"] / s["counted"]) if s["counted"] else None
        s["ok"] = s["share"] is None or s["share"] >= CLIP_SHARE_MIN
    missing = {}
    for st in req:
        if st not in per_state:
            missing[st] = "missing, allowed" if st in allow else "missing"
    c_clip = all(s["ok"] for s in per_state.values())
    c_req = all(v == "missing, allowed" for v in missing.values())

    # criterion 2
    ramps = _analyse_ramps(records, rate_hz)
    done = [r for r in ramps if r["status"] == "done"]
    interrupted = [r for r in ramps if r["status"] == "interrupted"]
    frames = [r["frames"] for r in done]
    steps = [x for r in done for x in r["steps"]]
    ramp_info = {
        "count": len(ramps), "completed": len(done), "interrupted": len(interrupted),
        "min_frames": min(frames) if frames else None,
        "median_frames": statistics.median(frames) if frames else None,
        "max_step": max(steps) if steps else None,
        "failed": sum(1 for r in ramps if not r["ok"]),
    }
    c_ramp = ramp_info["failed"] == 0

    # criterion 3
    slips = []
    for r in records:
        if (r["v"] is not None and r["v"] > v_min and r["omega"] is not None
                and r["radius"] is not None):
            slips.append(abs(r["omega"] * r["radius"] - r["v"]) / r["v"])
    mean_slip = (sum(slips) / len(slips)) if slips else None
    c_wheel = mean_slip is None or mean_slip <= WHEEL_SLIP_MAX
    diffs = [abs(r["yaw"] - r["yawgame"]) for r in records
             if r["yaw"] is not None and r["yawgame"] is not None]
    max_diff = max(diffs) if diffs else None
    c_steer = max_diff is None or max_diff <= STEER_DIFF_MAX

    criteria = {
        "clip_per_state": c_clip, "required_states": c_req, "blend_ramps": c_ramp,
        "wheel_slip": c_wheel, "front_steer": c_steer,
    }
    return {
        "lines": len(records),
        "seats": len({r["seat"] for r in records}),
        "rate_hz": rate_hz,
        "states": per_state,
        "missing": missing,
        "ramps": ramp_info,
        "wheels": {"lines": len(slips), "mean_slip": mean_slip},
        "steer": {"lines": len(diffs), "max_diff": max_diff},
        "criteria": criteria,
        "verdict": "PASS" if all(criteria.values()) else "FAIL",
    }


def _fmt(x, f="%.4f"):
    return "n/a" if x is None else f % x


def format_report(res):
    out = []
    out.append("%-12s %6s %6s %6s %8s  %s" % ("state", "lines", "excl", "match", "share", "verdict"))
    for st in sorted(res["states"]):
        s = res["states"][st]
        out.append("%-12s %6d %6d %6d %8s  %s (expects clip %s)" % (
            st, s["lines"], s["excluded"], s["matching"], _fmt(s["share"], "%.4f"),
            "PASS" if s["ok"] else "FAIL", s["expected_clip"]))
    for st, v in sorted(res["missing"].items()):
        out.append("%-12s %s" % (st, v))
    r = res["ramps"]
    out.append("ramps: %d total, %d completed, %d interrupted, %d failed; frames@60Hz min %s median %s; "
               "max blend step %s" % (r["count"], r["completed"], r["interrupted"], r["failed"],
                                      _fmt(r["min_frames"], "%.1f"), _fmt(r["median_frames"], "%.1f"),
                                      _fmt(r["max_step"], "%.3f")))
    w = res["wheels"]
    out.append("wheels: %d lines, mean |omega*radius-v|/v = %s (max %.2f)" % (
        w["lines"], _fmt(w["mean_slip"], "%.4f"), WHEEL_SLIP_MAX))
    s = res["steer"]
    out.append("front steer: %d lines, max |yaw-yawgame| = %s rad (max %.2f)" % (
        s["lines"], _fmt(s["max_diff"], "%.4f"), STEER_DIFF_MAX))
    c = res["criteria"]
    miss_bad = [k for k, v in res["missing"].items() if v == "missing"]
    out.append("%s clip per state (share >= %.2f)" % ("PASS" if c["clip_per_state"] else "FAIL", CLIP_SHARE_MIN))
    out.append("%s required states seen%s" % (
        "PASS" if c["required_states"] else "FAIL",
        (" (missing: %s)" % ", ".join(miss_bad)) if miss_bad else ""))
    out.append("%s blend ramps (monotonic, >= %d frames at 60 Hz)" % (
        "PASS" if c["blend_ramps"] else "FAIL", RAMP_MIN_FRAMES_60))
    out.append("%s wheel slip" % ("PASS" if c["wheel_slip"] else "FAIL"))
    out.append("%s front steer" % ("PASS" if c["front_steer"] else "FAIL"))
    out.append("hires_tick_eval: %d lines, %d seats, verdict %s" % (res["lines"], res["seats"], res["verdict"]))
    return "\n".join(out)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
USAGE = ("usage: hires_tick_eval.py eval <log> [--rate 30|60] [--seat N|all] "
         "[--allow-missing STATE] [--expected state=clip] [--json out.json]\n"
         "       hires_tick_eval.py selftest")


def _eval_lines(lines, argv):
    rate, seat, allow, expected, json_out = 30, "all", [], {}, None
    i = 0
    try:
        while i < len(argv):
            a = argv[i]
            if a == "--rate":
                rate = int(argv[i + 1]); i += 2
                if rate not in (30, 60):
                    raise ValueError
            elif a == "--seat":
                seat = argv[i + 1]; i += 2
                if seat != "all":
                    seat = int(seat)
            elif a == "--allow-missing":
                allow.append(argv[i + 1]); i += 2
            elif a == "--expected":
                k, v = argv[i + 1].split("=", 1)
                expected[k] = v; i += 2
            elif a == "--json":
                json_out = argv[i + 1]; i += 2
            else:
                raise ValueError
    except (ValueError, IndexError):
        print(USAGE)
        return 2
    recs = parse_hires_lines(lines)
    if seat != "all":
        recs = [r for r in recs if r["seat"] == seat]
    if not recs:
        print("hires_tick_eval: no hires lines found")
        return 2
    res = evaluate(recs, rate_hz=rate, expected=expected, allow_missing=allow)
    print(format_report(res))
    if json_out:
        with open(json_out, "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1, default=str)
    return 0 if res["verdict"] == "PASS" else 1


def main(argv):
    if argv and argv[0] == "selftest":
        return selftest()
    if len(argv) >= 2 and argv[0] == "eval":
        try:
            with open(argv[1], "r", encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
        except OSError as e:
            print("cannot read log: %s" % e)
            return 2
        return _eval_lines(lines, argv[2:])
    print(USAGE)
    return 2


# --------------------------------------------------------------------------
# Selftest
# --------------------------------------------------------------------------
def _line(vb, seat, state, clip, blend=1.0, prev="idle", steer=None, yaw=0.2,
          yawgame=0.2, v=3.125, radius=0.25, omega=None, prefix="[CTR Engine] ",
          tail="", fmt="real", rank=None, want=None):
    """One log line. `state` is the eval-level state (idle, steer_left, jump,
    podium_2, lose, forced:menu, ...); fmt "real" maps it to the engine's words,
    fmt "prov" writes the provisional format (state and clip as given)."""
    if omega is None:
        omega = v / radius
    if fmt == "prov":
        parts = ["%shires char:" % prefix, "vb %d" % vb, "seat %d" % seat, "state %s" % state,
                 "clip %s" % clip]
        if blend is not None:
            parts.append("blend %.3f" % blend)
        parts += ["prev %s" % prev, "steer %.3f" % (0.5 if steer is None else steer),
                  "yaw %.4f" % yaw, "yawgame %.4f" % yawgame, "omega %.3f" % omega,
                  "radius %.4f" % radius, "v %.3f" % v, "box 0.41 0.52 0.58 0.71"]
        return " ".join(parts) + tail
    steerclip = "none"
    drift = 0
    if state == "idle":
        eng, want_ = "base", clip
        steer = 0.0 if steer is None else steer
    elif state in ("steer_left", "steer_right"):
        eng, want_, steerclip = "base", clip, clip
        steer = (0.5 if state == "steer_left" else -0.5) if steer is None else steer
    elif state.startswith("podium_") or state == "lose":
        eng, want_ = "finished", clip
        rank = (int(state[-1]) - 1 if state != "lose" else 5) if rank is None else rank
    elif state.startswith("forced:"):
        eng, want_ = "forced", state[7:]
    else:
        eng, want_ = state, clip
        drift = {"drift_left": 1, "drift_right": -1}.get(state, 0)
    if want is not None:
        want_ = want
    steer = 0.0 if steer is None else steer
    parts = ["%shires char:" % prefix, "vb %d" % vb, "seat %d" % seat, "timer %d" % (vb * 2),
             "state %s" % eng, "want %s" % want_, "clip %s" % clip, "t %.3f" % 0.5]
    if blend is not None:
        parts.append("blend %.3f" % blend)
    parts += ["prev %s" % (prev or "none"), "steer %.3f" % steer, "steerclip %s" % steerclip,
              "steerRaw %d" % int(steer * 100), "air 0", "kart 0", "drift %d" % drift,
              "rank %d" % (0 if rank is None else rank), "yaw %.4f" % yaw,
              "yawgame %.4f" % yawgame, "roll 0.1000", "omega %.3f" % omega,
              "radius %.4f" % radius, "v %.3f" % v, "pos 1.0 2.0 3.0", "wheels own",
              "box 0.41 0.52 0.58 0.71"]
    return " ".join(parts) + tail


DEFAULT_SEGMENTS = [("idle", 20), ("steer_left", 20), ("steer_right", 20), ("jump", 20),
                    ("drift_left", 20), ("drift_right", 20), ("hit", 20), ("idle", 20)]


def make_seat(seat, segments=None, ramp_n=5, cut=None, clip_of=None, mutate=None):
    """Lines for one seat. Each segment after the first starts with a ramp of
    ramp_n lines (blend k/ramp_n); `cut` truncates the ramp of every segment
    (interrupted ramps); clip_of maps state -> clip; mutate(idx, kwargs) edits."""
    segments = segments or DEFAULT_SEGMENTS
    clip_of = clip_of or {}
    out = []
    prev_state = "idle"
    idx = 0
    for n_seg, (state, steady) in enumerate(segments):
        clip = clip_of.get(state, state)
        ramp = []
        if n_seg > 0:
            k = ramp_n if cut is None else cut
            ramp = [j / float(ramp_n) for j in range(k)]
        for b in ramp + [1.0] * steady:
            kw = dict(vb=100 + seat, seat=seat, state=state, clip=clip, blend=b, prev=prev_state)
            if mutate:
                mutate(idx, kw)
            out.append(_line(**kw))
            idx += 1
        prev_state = state
    return out


def make_log(seats=(0, 1), **kw):
    lines = ["engine starting up", "random junk line"]
    for s in seats:
        lines += make_seat(s, **kw)
    return lines


def selftest():
    checks = [0, 0]   # total, failures

    def check(name, cond, detail=""):
        checks[0] += 1
        if not cond:
            checks[1] += 1
            print("FAIL: %s %s" % (name, detail))

    def run(lines, **kw):
        return evaluate(parse_hires_lines(lines), **kw)

    def crit(res):
        return res["criteria"]

    # --- parser -----------------------------------------------------------
    ln = _line(1234, 0, "steer_left", "steer_left", blend=0.75, prev="idle", steer=0.62,
               yaw=0.21, yawgame=0.21, omega=12.5, radius=0.25, v=3.125,
               prefix="12:03:44.512 INFO game: [CTR Engine] ", tail=" newkey somevalue")
    recs = parse_hires_lines(["noise", ln, "[CTR Engine] other: vb 1"])
    check("parse count", len(recs) == 1)
    r = recs[0] if recs else {}
    check("parse ints", r.get("vb") == 1234 and r.get("seat") == 0)
    check("parse strs", r.get("state") == "base" and r.get("prev") == "idle"
          and r.get("want") == "steer_left" and r.get("wheels") == "own")
    check("parse eval view", eval_view(r) == ("steer_left", "steer_left") and r.get("eval_ok"))
    check("parse real extras", r.get("timer") == 2468 and r.get("steerclip") == "steer_left"
          and r.get("pos") == (1.0, 2.0, 3.0) and r.get("steerRaw") == 62 and r.get("rank") == 0
          and r.get("t") == 0.5 and r.get("drift") == 0 and r.get("roll") == 0.1)
    idl = parse_hires_lines([_line(1, 0, "idle", "idle", prev=None)])[0]
    check("parse none words", idl["prev"] is None and idl["steerclip"] is None
          and eval_view(idl) == ("idle", "idle"))
    prv = parse_hires_lines([_line(1, 0, "steer_left", "steer_left", fmt="prov")])[0]
    check("parse provisional", prv["state"] == "steer_left" and prv["want"] is None
          and eval_view(prv) == ("steer_left", "steer_left"))
    check("parse floats", r.get("blend") == 0.75 and r.get("omega") == 12.5 and r.get("v") == 3.125)
    check("parse box", r.get("box") == (0.41, 0.52, 0.58, 0.71))
    check("parse unknown key", r.get("extra") == {"newkey": "somevalue"})
    check("parse raw", r.get("raw") == ln)
    minimal = parse_hires_lines(["[CTR Engine] hires char: vb 1 seat 2 state idle clip idle"])
    check("parse optional None", len(minimal) == 1 and minimal[0]["blend"] is None
          and minimal[0]["box"] is None and minimal[0]["prev"] is None)
    check("parse missing required", parse_hires_lines(["[CTR Engine] hires char: vb 1 seat 2 state idle"]) == [])

    # --- good logs ----------------------------------------------------------
    res = run(make_log(), rate_hz=30)
    check("good 30Hz verdict", res["verdict"] == "PASS", str(res["criteria"]))
    check("good seats/lines", res["seats"] == 2 and res["lines"] == 2 * (8 * 20 + 7 * 5))
    check("good ramps counted", res["ramps"]["completed"] == 14 and res["ramps"]["interrupted"] == 0
          and res["ramps"]["failed"] == 0)
    check("good ramp frames", res["ramps"]["min_frames"] == 10.0)
    check("good max step", abs(res["ramps"]["max_step"] - 0.2) < 1e-6, str(res["ramps"]["max_step"]))
    check("good no missing", res["missing"] == {})
    check("good state shares", all(s["share"] == 1.0 for s in res["states"].values()))
    check("good excluded counted", res["states"]["steer_left"]["excluded"] == 2 * 5)

    res = run(make_log(ramp_n=9), rate_hz=60)
    check("good 60Hz verdict", res["verdict"] == "PASS" and res["ramps"]["min_frames"] == 9.0)
    res = run(make_log(ramp_n=3), rate_hz=60)
    check("60Hz 3-line ramp fails", not crit(res)["blend_ramps"] and res["verdict"] == "FAIL")
    res = run(make_log(ramp_n=2), rate_hz=30)
    check("30Hz 2-line ramp (4 frames) passes", crit(res)["blend_ramps"])
    res = run(make_log(ramp_n=1), rate_hz=30)
    check("30Hz 1-line ramp fails", not crit(res)["blend_ramps"])

    # --- clip share ---------------------------------------------------------
    log = make_log(seats=(0,), segments=[("idle", 20), ("jump", 100), ("hit", 20)],
                   mutate=lambda i, kw: (kw.update(clip="idle", blend=None)
                                         if kw["state"] == "jump" and i in (30, 60) else None))
    res = run(log, allow_missing=("steer_left", "steer_right", "drift_left", "drift_right"))
    st = res["states"]["jump"]
    check("2% wrong clip share", abs(st["share"] - 0.98) < 1e-9 and not st["ok"], str(st))
    check("2% wrong clip FAIL", not crit(res)["clip_per_state"] and res["verdict"] == "FAIL")
    log = make_log(seats=(0,), segments=[("idle", 20), ("jump", 100), ("hit", 20)],
                   mutate=lambda i, kw: (kw.update(clip="idle", blend=None)
                                         if kw["state"] == "jump" and i == 30 else None))
    res = run(log, allow_missing=("steer_left", "steer_right", "drift_left", "drift_right"))
    check("1% wrong clip PASS", crit(res)["clip_per_state"] and res["verdict"] == "PASS", str(res["states"]["jump"]))

    # --- ramps --------------------------------------------------------------
    # 0 -> 1 in one line
    log = ["x"] + [_line(1, 0, "idle", "idle")] * 3 + [_line(1, 0, "steer_left", "steer_left", 0.0)] \
        + [_line(1, 0, "steer_left", "steer_left", 1.0)] * 5
    res = run(log, allow_missing=REQUIRED_STATES)
    check("jump 0->1 FAIL", not crit(res)["blend_ramps"] and res["ramps"]["max_step"] == 1.0
          and res["verdict"] == "FAIL", str(res["ramps"]))
    # clip change with blend 1 immediately
    log = [_line(1, 0, "idle", "idle")] * 3 + [_line(1, 0, "steer_left", "steer_left", 1.0)] * 5
    res = run(log, allow_missing=REQUIRED_STATES)
    check("clip change without ramp FAIL", not crit(res)["blend_ramps"])
    # decreasing ramp
    bl = [0.0, 0.4, 0.2, 0.6, 0.8, 1.0]
    log = [_line(1, 0, "idle", "idle")] * 3 + [_line(1, 0, "steer_left", "steer_left", b) for b in bl] \
        + [_line(1, 0, "steer_left", "steer_left", 1.0)] * 3
    res = run(log, allow_missing=REQUIRED_STATES)
    check("decreasing ramp FAIL", not crit(res)["blend_ramps"] and res["verdict"] == "FAIL")
    check("decreasing ramp others fine", crit(res)["clip_per_state"] and crit(res)["wheel_slip"])
    # interrupted
    cutsegs = [("idle", 10)] + [(s_, 0) for s_ in ("steer_left", "steer_right", "jump", "drift_left",
                                                   "drift_right", "hit")] + [("idle", 10)]
    res = run(make_log(seats=(0,), segments=cutsegs, cut=3), rate_hz=30, allow_missing=REQUIRED_STATES)
    check("interrupted ramps not a failure", res["ramps"]["interrupted"] == 6 and res["ramps"]["completed"] == 1
          and crit(res)["blend_ramps"] and res["verdict"] == "PASS", str(res["ramps"]))
    # ramps tracked per seat (interleaved lines)
    a, b = make_seat(0), make_seat(1)
    inter = [x for pair in zip(a, b) for x in pair]
    res = run(inter, rate_hz=30)
    check("interleaved seats PASS", res["verdict"] == "PASS" and res["ramps"]["completed"] == 14)

    # --- wheels / steer -----------------------------------------------------
    res = run(make_log(mutate=lambda i, kw: kw.update(omega=kw.get("v", 3.125) / 0.25 * 1.10)), rate_hz=30)
    check("wheel slip 10% FAIL", not crit(res)["wheel_slip"] and res["verdict"] == "FAIL"
          and abs(res["wheels"]["mean_slip"] - 0.10) < 1e-6, str(res["wheels"]))
    res = run(make_log(mutate=lambda i, kw: kw.update(omega=3.125 / 0.25 * 1.03)), rate_hz=30)
    check("wheel slip 3% PASS", crit(res)["wheel_slip"] and res["verdict"] == "PASS")
    res = run(make_log(mutate=lambda i, kw: kw.update(v=0.3, omega=9.0)), rate_hz=30)
    check("slow lines ignored (v <= v_min)", crit(res)["wheel_slip"] and res["wheels"]["lines"] == 0)
    res = run(make_log(mutate=lambda i, kw: kw.update(yawgame=0.2 + 0.02)), rate_hz=30)
    check("steer offset 0.02 FAIL", not crit(res)["front_steer"] and res["verdict"] == "FAIL"
          and abs(res["steer"]["max_diff"] - 0.02) < 1e-9, str(res["steer"]))
    res = run(make_log(mutate=lambda i, kw: kw.update(yawgame=0.2 + 0.005)), rate_hz=30)
    check("steer offset 0.005 PASS", crit(res)["front_steer"])

    # --- missing states -----------------------------------------------------
    segs_nohit = [s for s in DEFAULT_SEGMENTS if s[0] != "hit"]
    res = run(make_log(segments=segs_nohit), rate_hz=30)
    check("missing hit FAIL", res["missing"] == {"hit": "missing"} and not crit(res)["required_states"]
          and res["verdict"] == "FAIL")
    res = run(make_log(segments=segs_nohit), rate_hz=30, allow_missing=("hit",))
    check("missing hit allowed PASS", res["missing"] == {"hit": "missing, allowed"}
          and res["verdict"] == "PASS")
    res = run(make_log(segments=segs_nohit), rate_hz=30, require_states=("jump",))
    check("require_states override", res["verdict"] == "PASS")

    # --- expected override (fallback) --------------------------------------
    fb = {"drift_left": "steer_left", "drift_right": "steer_right"}
    res = run(make_log(clip_of=fb), rate_hz=30)
    check("fallback without override FAIL", not crit(res)["clip_per_state"]
          and not res["states"]["drift_left"]["ok"])
    res = run(make_log(clip_of=fb), rate_hz=30, expected=fb)
    check("fallback with override PASS", res["verdict"] == "PASS", str(res["criteria"]))

    # --- derived states (real format) --------------------------------------
    allow_all = REQUIRED_STATES
    # steer sign mismatch: steer_left clip drawn while steer < 0
    log = make_log(seats=(0,), segments=[("idle", 10), ("steer_left", 50), ("steer_right", 20)],
                   mutate=lambda i, kw: kw.update(steer=-0.4)
                   if kw["state"] == "steer_left" and kw["blend"] == 1.0 else None)
    res = run(log, allow_missing=allow_all)
    check("steer sign mismatch FAIL", not crit(res)["clip_per_state"]
          and res["states"]["steer_left"]["matching"] == 0 and res["verdict"] == "FAIL",
          str(res["states"]["steer_left"]))
    check("steer sign other state intact", res["states"]["steer_right"]["share"] == 1.0)
    log = make_log(seats=(0,), segments=[("idle", 10), ("steer_right", 20)],
                   mutate=lambda i, kw: kw.update(steer=0.3) if kw["state"] == "steer_right" else None)
    res = run(log, allow_missing=allow_all)
    check("steer sign mismatch right FAIL", not res["states"]["steer_right"]["ok"])
    # steer clip names a state even when the logged state is "base"
    res = run(make_log(), rate_hz=30)
    check("base+steerclip -> steer_left/right", "steer_left" in res["states"]
          and "steer_right" in res["states"] and "base" not in res["states"])
    # base with steerclip none -> idle
    one = parse_hires_lines([_line(1, 0, "idle", "idle")])[0]
    check("base steerclip none -> idle", one["state"] == "base" and one["steerclip"] is None
          and eval_view(one) == ("idle", "idle"))
    # base, steering beyond the dead zone, but no steer clip drawn: a wrong steer line
    two = parse_hires_lines([_line(1, 0, "idle", "idle", steer=0.4), _line(2, 0, "idle", "idle", steer=0.02)])
    check("base steer 0.4 without steer clip -> steer_left, wrong", eval_view(two[0]) == ("steer_left", "none"))
    check("base steer 0.02 (dead zone) -> idle", eval_view(two[1]) == ("idle", "idle"))
    # finished: rank 1 -> podium_2, rank 5 -> lose
    fin = [("idle", 5), ("podium_2", 30)]
    res = run(make_log(seats=(0,), segments=fin), allow_missing=allow_all)
    check("finished rank 1 -> podium_2 PASS", "podium_2" in res["states"]
          and res["states"]["podium_2"]["share"] == 1.0 and res["verdict"] == "PASS", str(res["states"]))
    res = run(make_log(seats=(0,), segments=[("idle", 5), ("lose", 30)]), allow_missing=allow_all)
    check("finished rank 5 -> lose PASS", res["states"]["lose"]["share"] == 1.0 and res["verdict"] == "PASS")
    res = run(make_log(seats=(0,), segments=[("idle", 5), ("lose", 30)], clip_of={"lose": "podium_1"}),
              allow_missing=allow_all)
    check("rank 5 drawing podium_1 FAIL", not crit(res)["clip_per_state"] and res["verdict"] == "FAIL",
          str(res["states"].get("lose")))
    res = run(make_log(seats=(0,), segments=[("idle", 5), ("podium_3", 30)], clip_of={"podium_3": "podium_1"}),
              allow_missing=allow_all)
    check("podium_3 drawing podium_1 FAIL", not res["states"]["podium_3"]["ok"])
    res = run(make_log(seats=(0,), segments=[("idle", 5), ("podium_3", 30)], clip_of={"podium_3": "podium_1"}),
              allow_missing=allow_all, expected={"podium_3": "podium_1"})
    check("podium fallback via expected PASS", res["verdict"] == "PASS")
    # forced: expected clip is the wanted one
    res = run(make_log(seats=(0,), segments=[("idle", 5), ("forced:menu", 30)], clip_of={"forced:menu": "menu"}),
              allow_missing=allow_all)
    check("forced want menu drawing menu PASS", res["states"]["forced:menu"]["share"] == 1.0
          and res["verdict"] == "PASS", str(res["states"]))
    res = run(make_log(seats=(0,), segments=[("idle", 5), ("forced:menu", 30)], clip_of={"forced:menu": "idle"}),
              allow_missing=allow_all)
    check("forced want menu drawing idle FAIL", not crit(res)["clip_per_state"])
    # provisional format still works (including a mixed log)
    prov = [_line(1, 0, "idle", "idle", fmt="prov")] * 5 + [_line(1, 0, "jump", "jump", fmt="prov")] * 5
    prov = [x for x in prov]   # (no cross-fade here: only the clip share is judged)
    res = run(prov, allow_missing=allow_all)
    check("provisional lines PASS", crit(res)["clip_per_state"] and set(res["states"]) == {"idle", "jump"})
    res = run(prov + make_seat(3, segments=[("idle", 5), ("jump", 5)]), allow_missing=allow_all)
    check("mixed formats PASS", crit(res)["clip_per_state"] and res["states"]["jump"]["lines"] == 15)

    # --- CLI paths ----------------------------------------------------------
    def cli(lines, argv):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = _eval_lines(lines, argv)
        return rc, buf.getvalue().strip().splitlines()

    rc, outl = cli(make_log(), [])
    check("cli PASS exit 0", rc == 0 and outl[-1] == "hires_tick_eval: 390 lines, 2 seats, verdict PASS",
          outl[-1] if outl else "")
    rc, outl = cli(make_log(segments=segs_nohit), [])
    check("cli FAIL exit 1", rc == 1 and outl[-1].endswith("verdict FAIL"))
    check("cli FAIL line printed", any(x.startswith("FAIL required states") for x in outl))
    rc, outl = cli(make_log(segments=segs_nohit), ["--allow-missing", "hit"])
    check("cli allow-missing exit 0", rc == 0)
    rc, outl = cli(make_log(clip_of=fb), ["--expected", "drift_left=steer_left",
                                          "--expected", "drift_right=steer_right"])
    check("cli --expected exit 0", rc == 0)
    rc, outl = cli(make_log(ramp_n=9), ["--rate", "60"])
    check("cli --rate 60 exit 0", rc == 0)
    rc, outl = cli(make_log(), ["--seat", "1", "--allow-missing", "hit"])
    check("cli --seat 1", rc == 0 and outl[-1].startswith("hires_tick_eval: 195 lines, 1 seats"), outl[-1])
    rc, outl = cli(make_log(), ["--seat", "7"])
    check("cli unknown seat exit 2", rc == 2)
    rc, outl = cli(["nothing here", "[CTR Engine] some other line"], [])
    check("cli no hires lines exit 2", rc == 2)
    rc, outl = cli(make_log(), ["--rate", "45"])
    check("cli bad option exit 2", rc == 2)

    print("hires_tick_eval selftest: %d checks, %d failures" % (checks[0], checks[1]))
    return 1 if checks[1] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
