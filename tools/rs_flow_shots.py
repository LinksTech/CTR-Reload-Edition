#!/usr/bin/env python3
"""Reload Studio, guided flow "Build a character": shot series and click count.

Drives the page "Character" of Reload Studio with its automation verbs (--do) for the three
routes (vanilla, lowpoly, hires): the three cards (step 0), then every step (Model, Driver,
In-game look, Voices, Build before and after the build) and "Test in game" (in automation the
Studio only logs the command line; the game is never started). One Studio run per route.

Per route it writes into --out:
  <route>-<nn>-<step>.png/.txt   shot and `controls` dump of every step
  <route>-<nn>-<step>-report.txt the page's report of the steps Model to Build (primary buttons)
  legacy.log, legacy.rldchar     a script of before without a route (with the route vanilla)
  <route>.log                    the automation log (--log)
  <route>.rldchar                the built character (never into the game's characters folder)
  rs.ini                         the Studio's settings (--settings; nothing goes to %APPDATA%)
  AFTER.md                       the click table AFTER (and the comparison with BEFORE.md)

The click rule: 1 click = one mouse click or Enter. A file choice = 2
(Browse, Open). Typing a name = 0 clicks, counted separately as "fields typed". The AFTER list is
modelled from the plan (decisions E2-E4, E10); the run checks it:
  - the log has 0 lines with FAIL (exit 1 otherwise) and ends with "automation finished"
  - every controls dump shows the step's controls by their texts ("Building: <route>", "Change type",
    the five step heads, "< Back", "Next >", "Browse...", "Advanced options", "Save as", "Build",
    "Test in game", ...) and not those of another step or route (EXPECT below; exit 2 otherwise)
  - the report of every step from Model to Build says "primary buttons shown: 1"
  - the logged "Test in game" command line has --char (and --engine-hires-chars on lowpoly/hires only)
  - with the route vanilla: a script of before (no route verb) builds, and the log says the page
    picked Vanilla by itself ("route: vanilla (implicit ...)")
Exit code 0 only if all of that holds.

Only files this script wrote earlier (listed in <out>\\.rs_flow_shots.manifest) are removed
before a run; other files and folders are never touched.

Examples:
  python tools/rs_flow_shots.py --dry-run --route all --out C:\\work\\flow --models C:\\work\\models
  python tools/rs_flow_shots.py --route hires --out C:\\work\\flow --models C:\\work\\models --theme light
Python standard library only.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

ROUTES = ("vanilla", "lowpoly", "hires")
TITLES = {"vanilla": "Vanilla", "lowpoly": "Low-Poly", "hires": "Hi-Res"}
CHRG = {"vanilla": False, "lowpoly": True, "hires": True}
DEFAULT_MODEL = {"vanilla": "testdriver-classic.glb", "lowpoly": "testdriver-lowpoly.glb",
                 "hires": "testdriver-t10k-x2048.glb"}
NAMES = {"vanilla": "FLOWVANILLA", "lowpoly": "FLOWLOWPOLY", "hires": "FLOWHIRES"}
MANIFEST = ".rs_flow_shots.manifest"
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_STUDIO = os.path.join(REPO_ROOT, "build-msvc-x86", "Release", "ReloadStudio.exe")

# What the controls dump of a step must show, by the texts of the page (rs_char.c), compared
# whole and case-sensitive with the text of a control. Each entry is (rule, class, text):
#   "shown"   a control of that class ("Button", "Static"; None = any) with that text, visible
#   "hidden"  no visible control of that class with that text
#   "head"    a visible step head (a Button) whose text ends with that word ("1 Model", "4 Voices")
# "{title}" stands for the route's card title. Lists per step; CHRG_EXPECT and VANILLA_EXPECT
# add the entries that only hold for the CHRG routes or only for Vanilla.
HEADS = [("head", "Button", w) for w in ("Model", "Driver", "In-game look", "Voices", "Build")]
FLOW = [("shown", "Static", "Building: {title}"), ("shown", "Button", "Change type"),
        ("shown", "Button", "< Back")] + HEADS
EXPECT = {
    "cards": [("shown", "Static", "What do you want to build?"), ("shown", "Button", "Vanilla"),
              ("shown", "Button", "Low-Poly"), ("shown", "Button", "Hi-Res"),
              ("hidden", "Button", "< Back"), ("hidden", "Button", "Next >"),
              ("hidden", "Button", "Change type")],
    "model": FLOW + [("shown", "Button", "Browse..."), ("shown", "Button", "Advanced options"),
                     ("shown", "Button", "Next >")],
    "model-advanced": [("shown", "Button", "Hide advanced"), ("hidden", "Button", "Advanced options")],
    "model-advanced-off": [("shown", "Button", "Advanced options")],
    "model-preview-classic": [("shown", "Button", "Hi-Res"), ("shown", "Button", "Classic fallback")],
    "model-preview-hires": [("shown", "Button", "Hi-Res"), ("shown", "Button", "Classic fallback")],
    "driver": FLOW + [("shown", "Button", "Next >")],
    "look": FLOW + [("shown", "Button", "Next >")],
    "voices": FLOW + [("shown", "Button", "Next >")],
    "build": FLOW + [("shown", "Static", "Save as"), ("shown", "Button", "Build"),
                     ("shown", "Button", "Show details"), ("hidden", "Button", "Next >"),
                     ("hidden", "Button", "Test in game")],
    "built": FLOW + [("shown", "Button", "Test in game"), ("shown", "Button", "Build again"),
                     ("shown", "Button", "Show in folder"), ("shown", "Button", "Show details"),
                     ("hidden", "Button", "Next >")],
    "built-details": [("shown", "Button", "Hide details")],
    "built-details-off": [("shown", "Button", "Show details")],
    "test": FLOW + [("shown", "Button", "Test in game")],
}
# The switch above the preview: only on the CHRG routes, from the step Model on.
CHRG_EXPECT = {s: [("shown", "Button", "Hi-Res"), ("shown", "Button", "Classic fallback")]
               for s in ("model", "driver", "look", "voices", "build", "built", "test")}
# Advanced of the step Model: a CHRG route has the tick "Upscale small textures" only; Vanilla has
# the switches Options, Import and Animations (Wheels only with --enable-preview-features).
CHRG_EXPECT["model-advanced"] = [("shown", "Button", "Upscale small textures"), ("hidden", "Button", "Options"),
                                 ("hidden", "Button", "Import"), ("hidden", "Button", "Animations"),
                                 ("hidden", "Button", "Wheels")]
VANILLA_EXPECT = {s: [("hidden", "Button", "Classic fallback"), ("hidden", "Button", "Upscale small textures")]
                  for s in ("model", "model-advanced", "driver", "look", "voices", "build", "built", "test")}
VANILLA_EXPECT["model-advanced"] += [("shown", "Button", "Options"), ("shown", "Button", "Import"),
                                     ("shown", "Button", "Animations"), ("hidden", "Button", "Wheels")]
# Steps whose "report" must say "primary buttons shown: 1" (one main action per step).
ONE_PRIMARY = ("model", "driver", "look", "voices", "build", "built")
# The log line of a script that names no route (Vanilla by itself).
IMPLICIT_ROUTE = "route: vanilla (implicit"


class Shot:
    """One picture: the verbs that lead to it, then `shot` and `controls`."""

    def __init__(self, nn, step, verbs, extra=False):
        self.nn = nn
        self.step = step
        self.verbs = verbs
        self.extra = extra

    def base(self, route):
        return "%s-%s-%s" % (route, self.nn, self.step)


def route_shots(route, model, outfile, extras):
    """The automation steps of a route as pictures, in order."""
    shots = [Shot("00", "cards", ["page char", "route none", "wait 300"])]
    m = ["route " + route, "wait 300", "model " + model, "wait 800"]
    shots.append(Shot("01", "model", m))
    if extras:
        shots.append(Shot("01a", "model-advanced", ["advanced on", "wait 300"], True))
        if CHRG[route]:
            shots.append(Shot("01b", "model-preview-classic", ["advanced off", "preview classic", "wait 300"], True))
            shots.append(Shot("01c", "model-preview-hires", ["preview hires", "wait 300"], True))
        else:
            shots.append(Shot("01b", "model-advanced-off", ["advanced off", "wait 300"], True))
    shots.append(Shot("02", "driver", ["step next", "name " + NAMES[route], "wait 300"]))
    shots.append(Shot("03", "look", ["step next", "wait 300"]))
    shots.append(Shot("04", "voices", ["step next", "wait 300"]))
    shots.append(Shot("05", "build", ["step next", "out " + outfile, "wait 300"]))
    shots.append(Shot("06", "built", ["build", "wait 800"]))
    if extras:
        shots.append(Shot("06a", "built-details", ["details on", "wait 300"], True))
        shots.append(Shot("06b", "built-details-off", ["details off", "wait 300"], True))
    shots.append(Shot("07", "test", ["test-in-game", "wait 300"]))
    return shots


def do_steps(route, shots, outdir):
    """The full --do list of the run of a route."""
    steps = []
    for s in shots:
        steps += s.verbs
        base = os.path.join(outdir, s.base(route))
        steps.append("shot " + base + ".bmp")
        steps.append("controls " + base + ".txt")
        if s.step in ONE_PRIMARY:
            steps.append("report " + base + "-report.txt")
    steps.append("quit")
    return steps


def legacy_steps(model, outfile):
    """A script of before, without a route: the page must pick Vanilla by itself."""
    return ["page char", "model " + model, "name FLOWLEGACY", "out " + outfile, "build", "quit"]


def after_actions(route):
    """The user actions AFTER (shortest path with defaults): (action, clicks, fields, shot step)."""
    chrg = CHRG[route]
    return [
        ("click \"Character\" in the sidebar (the Studio opens on Track)", 1, 0, "cards"),
        ("click the card \"%s\"" % TITLES[route], 1, 0, "cards"),
        ("Model: Browse..., Open (%s)" % (".glb" if chrg else "model file"), 2, 0, "model"),
        ("click \"Next >\" (to Driver)", 1, 0, "model"),
        ("type the name", 0, 1, "driver"),
        ("click \"Next >\" (to In-game look)", 1, 0, "driver"),
        ("click \"Next >\" (to Voices; optional, skipped)", 1, 0, "look"),
        ("click \"Next >\" (to Build)", 1, 0, "voices"),
        ("click \"Build\" (Save as defaults to the game's characters folder)", 1, 0, "build"),
        ("click \"Test in game\"%s" % (" (with --engine-hires-chars)" if chrg else ""), 1, 0, "test"),
    ]


def totals(actions):
    return sum(a[1] for a in actions), sum(a[2] for a in actions)


def shot_name_for(route, shots, step):
    for s in shots:
        if s.step == step:
            return s.base(route) + ".png"
    return "-"


def after_table(route, shots):
    acts = after_actions(route)
    lines = ["| # | action | clicks | fields typed | shot file |", "|---|--------|--------|--------------|-----------|"]
    for i, (text, c, f, step) in enumerate(acts, 1):
        lines.append("| %d | %s | %d | %d | %s |" % (i, text, c, f, shot_name_for(route, shots, step)))
    c, f = totals(acts)
    lines.append("")
    lines.append("TOTAL %s: clicks=%d fields=%d" % (route, c, f))
    return "\n".join(lines)


def parse_before(path):
    """{route: (clicks, fields)} from the TOTAL lines of BEFORE.md."""
    out = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"\s*TOTAL (\w+): clicks=(\d+) fields=(\d+)", line)
            if m:
                out[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return out


def comparison_table(routes, before):
    lines = ["| route | BEFORE clicks | BEFORE fields | AFTER clicks | AFTER fields | clicks difference |",
             "|-------|---------------|---------------|--------------|--------------|-------------------|"]
    for r in routes:
        c, f = totals(after_actions(r))
        if r in before:
            bc, bf = before[r]
            lines.append("| %s | %d | %d | %d | %d | %+d |" % (TITLES[r], bc, bf, c, f, c - bc))
        else:
            lines.append("| %s | n/a | n/a | %d | %d | n/a |" % (TITLES[r], c, f))
    return "\n".join(lines)


# --- BMP to PNG (the Studio writes 24-bit BMP whatever the extension says) ---------------------

def bmp_to_png(bmp, png):
    with open(bmp, "rb") as fh:
        data = fh.read()
    if data[:2] != b"BM":
        raise ValueError("not a BMP: " + bmp)
    off = struct.unpack_from("<I", data, 10)[0]
    w, h, _planes, bpp = struct.unpack_from("<iiHH", data, 18)
    if bpp != 24:
        raise ValueError("BMP is %d bit, only 24 is read" % bpp)
    top_down = h < 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    raw = bytearray()
    for y in range(h):
        row = data[off + (y if top_down else h - 1 - y) * stride:][:w * 3]
        rgb = bytearray(len(row))
        rgb[0::3] = row[2::3]
        rgb[1::3] = row[1::3]
        rgb[2::3] = row[0::3]
        raw.append(0)
        raw += rgb

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    with open(png, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                 chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


# --- checks of a run -----------------------------------------------------------------------------

def read_log(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.read().splitlines()
    except OSError:
        return None


def check_controls(path, expect):
    """Missing expectations of a controls dump, as texts (empty list = fine)."""
    # The dump's control lines (rs_shell.c, Rs_DumpControls): control, id, class, left, top,
    # right, bottom, visible, ..., text (field 12).
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError:
        return ["no controls dump"]
    shown = []
    for ln in lines:
        f = ln.split("\t")
        if f[0] == "control" and len(f) >= 13 and f[7] == "1":
            shown.append((f[2], f[12]))
    missing = []
    for rule, cls, text in expect:
        def hit(c, t):
            if cls is not None and c != cls:
                return False
            if rule == "head":
                return t == text or t.endswith(" " + text)
            return t == text
        found = any(hit(c, t) for c, t in shown)
        if rule in ("shown", "head") and not found:
            missing.append("%s '%s' not present and visible" % ("step head" if rule == "head" else (cls or "control"), text))
        elif rule == "hidden" and found:
            missing.append("%s '%s' visible but should not be" % (cls or "control", text))
    return missing


def check_report(path):
    """Misses of a step's report: exactly one primary button shown."""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return ["no report written"]
    m = re.search(r"primary buttons shown:\s*(\d+)", text)
    if not m:
        return ["the report has no line 'primary buttons shown'"]
    if m.group(1) != "1":
        return ["primary buttons shown: %s (1 expected)" % m.group(1)]
    return []


def step_expect(route, step):
    exp = list(EXPECT.get(step, []))
    exp += (CHRG_EXPECT if CHRG[route] else VANILLA_EXPECT).get(step, [])
    return [(rule, cls, text.replace("{title}", TITLES[route])) for rule, cls, text in exp]


def check_run(route, shots, outdir, log_lines):
    """(fail_lines, control_misses, notes) of a finished run."""
    fails = []
    ctrl = []
    notes = []
    if log_lines is None:
        return ["no log written"], ctrl, notes
    fails = [ln.strip() for ln in log_lines if "FAIL" in ln]
    fin = [ln for ln in log_lines if ln.startswith("automation finished")]
    if not fin:
        fails.append("the log has no 'automation finished' line (the Studio did not get to the end)")
    for s in shots:
        exp = step_expect(route, s.step)
        if exp:
            for m in check_controls(os.path.join(outdir, s.base(route) + ".txt"), exp):
                ctrl.append("%s: %s" % (s.base(route), m))
        if s.step in ONE_PRIMARY:
            for m in check_report(os.path.join(outdir, s.base(route) + "-report.txt")):
                ctrl.append("%s: %s" % (s.base(route), m))
    cmds = [ln for ln in log_lines if "--char" in ln and "ctr_native" in ln]
    if not cmds:
        ctrl.append("%s: no logged Test in game command line with --char" % route)
    else:
        has = "--engine-hires-chars" in cmds[-1]
        if CHRG[route] and not has:
            ctrl.append("%s: Test in game command line lacks --engine-hires-chars" % route)
        if not CHRG[route] and has:
            ctrl.append("%s: Test in game command line has --engine-hires-chars on a vanilla route" % route)
        notes.append("test-in-game command line: " + cmds[-1].strip())
    return fails, ctrl, notes


# --- running -------------------------------------------------------------------------------------

def clean_old(outdir):
    """Remove only the files an earlier run of this script wrote."""
    mf = os.path.join(outdir, MANIFEST)
    if not os.path.isfile(mf):
        return
    with open(mf, encoding="utf-8") as fh:
        for name in fh.read().splitlines():
            p = os.path.join(outdir, name)
            if name and os.sep not in name and "/" not in name and os.path.isfile(p):
                os.remove(p)
    os.remove(mf)


def run_route(args, route, model, outdir):
    outfile = os.path.join(outdir, route + ".rldchar")
    shots = route_shots(route, model, outfile, args.extras)
    steps = do_steps(route, shots, outdir)
    cmd = [args.studio, "--settings", os.path.join(outdir, "rs.ini"), "--log", os.path.join(outdir, route + ".log"),
           "--theme", args.theme]
    for s in steps:
        cmd += ["--do", s]
    env = dict(os.environ, TMP=args.tmp, TEMP=args.tmp)
    print("route %s: starting %s (%d steps)" % (route, args.studio, len(steps)))
    try:
        subprocess.run(cmd, env=env, timeout=args.timeout, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        print("route %s: the Studio did not finish within %d s (killed)" % (route, args.timeout))
    for s in shots:
        bmp = os.path.join(outdir, s.base(route) + ".bmp")
        if os.path.isfile(bmp):
            try:
                bmp_to_png(bmp, bmp[:-4] + ".png")
                os.remove(bmp)
            except (OSError, ValueError) as e:
                print("route %s: %s stays a BMP (%s)" % (route, s.base(route), e))
    return shots, check_run(route, shots, outdir, read_log(os.path.join(outdir, route + ".log")))


def run_legacy(args, model, outdir):
    """A script of before (no route verb) with the Vanilla model: (fails, misses, notes)."""
    steps = legacy_steps(model, os.path.join(outdir, "legacy.rldchar"))
    log = os.path.join(outdir, "legacy.log")
    cmd = [args.studio, "--settings", os.path.join(outdir, "rs.ini"), "--log", log, "--theme", args.theme]
    for s in steps:
        cmd += ["--do", s]
    env = dict(os.environ, TMP=args.tmp, TEMP=args.tmp)
    print("legacy script (no route): starting %s (%d steps)" % (args.studio, len(steps)))
    try:
        subprocess.run(cmd, env=env, timeout=args.timeout, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        print("legacy script: the Studio did not finish within %d s (killed)" % args.timeout)
    lines = read_log(log)
    if lines is None:
        return ["no log written"], [], []
    fails = [ln.strip() for ln in lines if "FAIL" in ln]
    if not [ln for ln in lines if ln.startswith("automation finished")]:
        fails.append("the log has no 'automation finished' line")
    misses = []
    implicit = [ln.strip() for ln in lines if IMPLICIT_ROUTE in ln]
    if not implicit:
        misses.append("no log line '%s ...': the page did not pick Vanilla by itself" % IMPLICIT_ROUTE)
    if not os.path.isfile(os.path.join(outdir, "legacy.rldchar")):
        misses.append("the legacy script built no legacy.rldchar")
    return fails, misses, implicit[:1]


def main(argv=None):
    ap = argparse.ArgumentParser(description="Reload Studio guided flow: shots per step and click count AFTER.")
    ap.add_argument("--studio", default=DEFAULT_STUDIO, help="ReloadStudio.exe (default: the Release build of this checkout)")
    ap.add_argument("--out", required=True, help="output folder (only files written earlier by this script are removed)")
    ap.add_argument("--models", default=os.environ.get("RS_FLOW_MODELS"),
                    help="folder with the test driver glbs (or env RS_FLOW_MODELS); required except with --dry-run")
    ap.add_argument("--route", default="all", choices=ROUTES + ("all",))
    ap.add_argument("--dry-run", action="store_true", help="print the --do steps and the click table; start nothing")
    ap.add_argument("--theme", default="dark", choices=("dark", "light"))
    ap.add_argument("--tmp", default=None, help="TMP/TEMP of the Studio (default: the out folder)")
    ap.add_argument("--before", default=None, help="BEFORE.md: adds the comparison table to AFTER.md")
    ap.add_argument("--extras", action="store_true", help="also shoot Advanced, Show details and the preview switch")
    ap.add_argument("--timeout", type=int, default=600, help="seconds per route")
    for r in ROUTES:
        ap.add_argument("--model-" + r, default=None, help="file name of the %s model in --models (default %s)" % (r, DEFAULT_MODEL[r]))
    args = ap.parse_args(argv)

    routes = list(ROUTES) if args.route == "all" else [args.route]
    outdir = os.path.abspath(args.out)
    args.tmp = os.path.abspath(args.tmp) if args.tmp else outdir
    if not args.dry_run and not args.models:
        ap.error("--models (or env RS_FLOW_MODELS) is required")
    models_dir = args.models or "<models>"
    models = {}
    for r in routes:
        models[r] = os.path.join(models_dir, getattr(args, "model_" + r) or DEFAULT_MODEL[r])

    before = parse_before(args.before) if args.before else {}

    if args.dry_run:
        for r in routes:
            shots = route_shots(r, models[r], os.path.join(outdir, r + ".rldchar"), args.extras)
            print("== route %s: %s --settings %s --log %s --theme %s" %
                  (r, args.studio, os.path.join(outdir, "rs.ini"), os.path.join(outdir, r + ".log"), args.theme))
            for st in do_steps(r, shots, outdir):
                print("  --do \"%s\"" % st)
            print("")
            print("AFTER, route %s" % TITLES[r])
            print(after_table(r, shots))
            print("")
        if "vanilla" in routes:
            print("== a script of before (no route verb; the page must pick Vanilla by itself)")
            for st in legacy_steps(models["vanilla"], os.path.join(outdir, "legacy.rldchar")):
                print("  --do \"%s\"" % st)
            print("")
        print("comparison")
        print(comparison_table(routes, before))
        return 0

    if not os.path.isfile(args.studio):
        print("error: no Studio at %s" % args.studio)
        return 3
    for r in routes:
        if not os.path.isfile(models[r]):
            print("error: model not found: %s" % models[r])
            return 3
    os.makedirs(outdir, exist_ok=True)
    os.makedirs(args.tmp, exist_ok=True)
    clean_old(outdir)
    pre = set(os.listdir(outdir))

    exit_code = 0
    md = ["# Click count AFTER (guided flow)", "",
          "Rule: 1 click = one mouse click or Enter; a file choice = 2 (Browse, Open); typing a name = 0 clicks, "
          "counted as a field typed. Shortest path with defaults, from Studio start to \"Test in game\".", ""]
    results = {}
    for r in routes:
        shots, (fails, ctrl, notes) = run_route(args, r, models[r], outdir)
        results[r] = (shots, fails, ctrl, notes)
        print("route %s: %d FAIL line(s), %d control check miss(es)" % (r, len(fails), len(ctrl)))
        for t in fails:
            print("  " + t)
        for t in ctrl:
            print("  CHECK: " + t)
        for t in notes:
            print("  " + t)
        if fails:
            exit_code = max(exit_code, 1)
        elif ctrl:
            exit_code = max(exit_code, 2)
    legacy = None
    if "vanilla" in routes:
        legacy = run_legacy(args, models["vanilla"], outdir)
        print("legacy script: %d FAIL line(s), %d check miss(es)" % (len(legacy[0]), len(legacy[1])))
        for t in legacy[0] + legacy[1] + legacy[2]:
            print("  " + t)
        if legacy[0]:
            exit_code = max(exit_code, 1)
        elif legacy[1]:
            exit_code = max(exit_code, 2)
    for r in routes:
        shots, fails, ctrl, notes = results[r]
        md += ["## Route " + TITLES[r], "", after_table(r, shots), "",
               "Run check: %d FAIL line(s), %d control check miss(es)." % (len(fails), len(ctrl)), ""]
        md += ["- FAIL: " + t for t in fails] + ["- CHECK: " + t for t in ctrl] + ["- " + t for t in notes]
        md.append("")
    if legacy is not None:
        md += ["## A script of before (no route verb)", "",
               "Run check: %d FAIL line(s), %d check miss(es)." % (len(legacy[0]), len(legacy[1])), ""]
        md += ["- FAIL: " + t for t in legacy[0]] + ["- CHECK: " + t for t in legacy[1]] + ["- " + t for t in legacy[2]]
        md.append("")
    md += ["## Comparison BEFORE / AFTER" if before else "## Totals", "", comparison_table(routes, before), ""]
    with open(os.path.join(outdir, "AFTER.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(md))
    new = sorted(set(os.listdir(outdir)) - pre - {MANIFEST})
    with open(os.path.join(outdir, MANIFEST), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(new) + "\n")
    print("wrote %s" % os.path.join(outdir, "AFTER.md"))
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
