#!/usr/bin/env python3
"""Contact sheets for the hi-res driver review (snapshot series -> review sheets).

Usage:
  python tools/hires_sheet.py race --shots <dir> --name <shot-name>
         --series <first>,<count>,<step> --log <game log> --out <dir>
         [--seat 0] [--crop 2.0]
  python tools/hires_sheet.py files --out <file.png> --title <text> <label>=<image> ...
  python tools/hires_sheet.py podium --shots <dir> --name <shot-name> --out <dir>
  python tools/hires_sheet.py selftest

Exit codes: 0 at least one sheet written, 1 nothing written, 2 usage error.
Needs Pillow; log parsing comes from hires_tick_eval.py (same folder).
"""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

SHEET_W = 918
COLS = 3
TILE_W = SHEET_W // COLS      # 306
TILE_H = 300
LABEL_H = 28
STATES = ("steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit", "idle")

WARNINGS = []


def warn(msg):
    WARNINGS.append(msg)
    print("warning: " + msg, file=sys.stderr)


def load_eval_module():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hires_tick_eval.py")
    spec = importlib.util.spec_from_file_location("hires_tick_eval", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --------------------------------------------------------------------------
# Drawing
# --------------------------------------------------------------------------
def _font():
    try:
        return ImageFont.load_default(size=13)
    except TypeError:
        return ImageFont.load_default()


def make_tile(img, label):
    """One tile TILE_W x (LABEL_H + TILE_H); img None -> grey 'missing' tile."""
    tile = Image.new("RGB", (TILE_W, LABEL_H + TILE_H), (40, 40, 44))
    d = ImageDraw.Draw(tile)
    d.rectangle([0, 0, TILE_W, LABEL_H - 1], fill=(24, 24, 28))
    d.text((6, 7), label, fill=(235, 235, 235), font=_font())
    if img is None:
        d.rectangle([0, LABEL_H, TILE_W, LABEL_H + TILE_H], fill=(110, 110, 110))
        d.text((TILE_W // 2 - 25, LABEL_H + TILE_H // 2 - 6), "missing", fill=(255, 255, 255), font=_font())
        return tile
    img = img.convert("RGB")
    s = min(TILE_W / img.width, TILE_H / img.height)
    w, h = max(1, round(img.width * s)), max(1, round(img.height * s))
    small = img.resize((w, h), Image.LANCZOS)
    tile.paste(small, ((TILE_W - w) // 2, LABEL_H + (TILE_H - h) // 2))
    return tile


def make_sheet(tiles, title=None):
    """tiles: list of (img or None, label). Width 918, 3 columns."""
    rows = max(1, (len(tiles) + COLS - 1) // COLS)
    top = 30 if title else 0
    sheet = Image.new("RGB", (SHEET_W, top + rows * (LABEL_H + TILE_H)), (16, 16, 18))
    if title:
        ImageDraw.Draw(sheet).text((6, 8), title, fill=(255, 255, 255), font=_font())
    for i, (img, label) in enumerate(tiles):
        sheet.paste(make_tile(img, label), ((i % COLS) * TILE_W, top + (i // COLS) * (LABEL_H + TILE_H)))
    return sheet


def load_image(path):
    try:
        with Image.open(path) as im:
            return im.convert("RGB")
    except (OSError, ValueError):
        return None


def crop_box(img, box, factor):
    """Box (fractions) enlarged by factor around its centre, clamped to the image."""
    if not box:
        return img
    w, h = img.size
    cx, cy = (box[0] + box[2]) / 2 * w, (box[1] + box[3]) / 2 * h
    hw, hh = abs(box[2] - box[0]) * w * factor / 2, abs(box[3] - box[1]) * h * factor / 2
    x0, y0 = max(0, int(round(cx - hw))), max(0, int(round(cy - hh)))
    x1, y1 = min(w, int(round(cx + hw))), min(h, int(round(cy + hh)))
    if x1 - x0 < 2 or y1 - y0 < 2:
        return img
    return img.crop((x0, y0, x1, y1))


# --------------------------------------------------------------------------
# race
# --------------------------------------------------------------------------
def longest_run_middle(recs, state):
    """Middle record of the longest run of consecutive lines with state == clip == state."""
    best, cur = [], []
    for r in recs:
        ok = r["state"] == state and r["clip"] == state and r["blend"] in (None, 1.0)
        if ok:
            cur.append(r)
            if len(cur) > len(best):
                best = list(cur)
        else:
            cur = []
    return best[len(best) // 2] if best else None


def parse_series(text):
    parts = text.split(",")
    if len(parts) != 3:
        raise ValueError("--series needs <first>,<count>,<step>")
    first, count, step = (int(p) for p in parts)
    if count < 1 or step < 1:
        raise ValueError("--series count and step must be >= 1")
    return first, count, step


def frame_index(vb, first, step):
    d = (vb - first) / step
    return int(d + 0.5) if d >= 0 else -int(-d + 0.5)


def do_race(shots, name, series, log, out, seat=0, crop=2.0):
    ev = load_eval_module()
    first, count, step = series
    with open(log, "r", errors="replace") as f:
        recs = [r for r in ev.parse_hires_lines(f.readlines()) if r["seat"] == seat]
    os.makedirs(out, exist_ok=True)
    tiles = []
    for state in STATES:
        rec = longest_run_middle(recs, state)
        if rec is None:
            warn("state %s: no run with clip == state for seat %d" % (state, seat))
            continue
        idx = frame_index(rec["vb"], first, step)
        if not 0 <= idx < count:
            warn("state %s: vb %d maps to index %d outside 0..%d" % (state, rec["vb"], idx, count - 1))
            continue
        path = os.path.join(shots, "%s-f%05d.bmp" % (name, idx))
        img = load_image(path) if os.path.isfile(path) else None
        if img is None:
            warn("state %s: snapshot missing: %s" % (state, path))
            continue
        c = crop_box(img, rec.get("box"), crop)
        c.save(os.path.join(out, "race-%s.png" % state))
        tiles.append((c, "%s | clip %s | vb %d" % (state, rec["clip"], rec["vb"])))
    if not tiles:
        return 0
    make_sheet(tiles).save(os.path.join(out, "race-sheet.png"))
    return len(tiles)


# --------------------------------------------------------------------------
# files / podium
# --------------------------------------------------------------------------
def do_files(out, title, pairs):
    tiles = []
    for label, path in pairs:
        img = load_image(path) if os.path.isfile(path) else None
        if img is None:
            warn("image missing: %s" % path)
        tiles.append((img, label))
    if not tiles:
        return 0
    d = os.path.dirname(out)
    if d:
        os.makedirs(d, exist_ok=True)
    make_sheet(tiles, title).save(out)
    return len(tiles)


def find_podium(shots, name):
    rx = re.compile(r"^" + re.escape(name) + r"-cup-podium-f(\d+)\.bmp$")
    found = []
    for fn in os.listdir(shots):
        m = rx.match(fn)
        if m:
            found.append((int(m.group(1)), os.path.join(shots, fn)))
    found.sort()
    if len(found) > 12:
        found = [found[round(i * (len(found) - 1) / 11)] for i in range(12)]
    return found


def do_podium(shots, name, out):
    found = find_podium(shots, name)
    if not found:
        warn("no podium shots for %s in %s" % (name, shots))
        return 0
    os.makedirs(out, exist_ok=True)
    tiles = [(load_image(p), "podium f%d" % fr) for fr, p in found]
    make_sheet(tiles).save(os.path.join(out, "podium-sheet.png"))
    return len(tiles)


# --------------------------------------------------------------------------
# selftest
# --------------------------------------------------------------------------
def selftest():
    base = os.path.join(tempfile.gettempdir(), "hires_sheet_selftest")
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(base)
    n = [0, 0]

    def check(cond, text):
        n[0] += 1
        if not cond:
            n[1] += 1
            print("FAIL: " + text)

    first, step, count = 1000, 10, 60
    colours = {"steer_left": (255, 0, 0), "steer_right": (0, 255, 0), "jump": (0, 0, 255),
               "drift_left": (255, 255, 0), "drift_right": (255, 0, 255), "hit": (0, 255, 255),
               "idle": (255, 128, 0)}
    fmt = "[CTR Engine] hires char: vb %d seat 0 state %s clip %s blend 1.000 box %s"
    log = []
    plan = {}
    vb = first
    for s in STATES:
        # decoy run of length 1 with a different box, a break, then the real run of 3
        log.append(fmt % (vb, s, s, "0.05 0.05 0.1 0.1"))
        vb += 10
        log.append("[CTR Engine] hires char: vb %d seat 0 state other clip other blend 1.000" % vb)
        vb += 10
        start = vb
        for _ in range(3):
            log.append(fmt % (vb, s, s, "0.41 0.52 0.58 0.71"))
            vb += 10
        plan[s] = start + 10
    log.append("[CTR Engine] hires char: vb 1020 seat 1 state hit clip hit blend 1.000 box 0.0 0.0 0.1 0.1")
    W, H = 640, 360
    frame_of = {s: frame_index(v, first, step) for s, v in plan.items()}
    check(max(frame_of.values()) < count, "plan fits the series")
    for i in range(count):
        im = Image.new("RGB", (W, H), (60, 60, 60))
        for s, fi in frame_of.items():
            if fi == i:
                ImageDraw.Draw(im).rectangle([int(.41 * W), int(.52 * H), int(.58 * W), int(.71 * H)], fill=colours[s])
        im.save(os.path.join(base, "t-f%05d.bmp" % i), format="BMP")
    os.remove(os.path.join(base, "t-f%05d.bmp" % frame_of["jump"]))
    lp = os.path.join(base, "game.log")
    with open(lp, "w") as f:
        f.write("\n".join(log) + "\nsome unrelated line\n")
    out = os.path.join(base, "out")
    del WARNINGS[:]
    got = do_race(base, "t", (first, count, step), lp, out, 0, 2.0)
    check(got == len(STATES) - 1, "race tiles written (all but missing jump): %d" % got)
    check(any("jump" in w and "missing" in w for w in WARNINGS), "missing frame gives a warning")
    sizes = set()
    for s in STATES:
        p = os.path.join(out, "race-%s.png" % s)
        if s == "jump":
            check(not os.path.exists(p), "no crop for the missing jump frame")
            continue
        check(os.path.exists(p), "race-%s.png exists" % s)
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGB")
        c = im.getpixel((im.width // 2, im.height // 2))
        check(c == colours[s], "%s: centre colour %s expected %s (right frame + crop)" % (s, c, colours[s]))
        check(im.width < W and im.height < H, "%s: crop smaller than frame" % s)
        sizes.add(im.size)
    check(len(sizes) == 1, "all crops same size")
    sp = os.path.join(out, "race-sheet.png")
    check(os.path.exists(sp) and Image.open(sp).width == SHEET_W, "race-sheet width 918")
    check(Image.open(sp).height == 2 * (LABEL_H + TILE_H), "race-sheet 6 tiles -> 2 rows")
    im0 = Image.new("RGB", (100, 50))
    check(crop_box(im0, None, 2.0).size == (100, 50), "no box -> whole frame")
    check(crop_box(im0, (0.0, 0.0, 0.2, 0.2), 4.0).size[0] <= 100, "crop clamped to image")
    check(frame_index(1005, 1000, 10) == 1 and frame_index(1004, 1000, 10) == 0, "frame_index rounding")
    del WARNINGS[:]
    do_race(base, "t", (first, 3, step), lp, os.path.join(base, "out2"), 0, 2.0)
    check(any("outside" in w for w in WARNINGS), "out-of-range index gives a warning")
    fp = os.path.join(base, "files.png")
    del WARNINGS[:]
    cnt = do_files(fp, "title", [("a", os.path.join(base, "t-f00000.bmp")),
                                 ("b", os.path.join(base, "nope.bmp")),
                                 ("c", os.path.join(base, "t-f00001.bmp"))])
    check(cnt == 3 and os.path.exists(fp) and Image.open(fp).width == SHEET_W, "files sheet written, width 918")
    check(any("nope.bmp" in w for w in WARNINGS), "files mode: missing image warns")
    ft = Image.open(fp).convert("RGB")
    check(ft.getpixel((TILE_W + 20, 30 + LABEL_H + 20)) == (110, 110, 110), "missing tile is grey")
    pd = os.path.join(base, "pod")
    os.makedirs(pd)
    for fr in (300, 20, 100):
        Image.new("RGB", (64, 36), (fr % 256, 0, 0)).save(os.path.join(pd, "z-cup-podium-f%d.bmp" % fr), format="BMP")
    Image.new("RGB", (64, 36)).save(os.path.join(pd, "other-cup-podium-f5.bmp"), format="BMP")
    check([f for f, _ in find_podium(pd, "z")] == [20, 100, 300], "podium sorted by frame numerically")
    check(do_podium(pd, "z", os.path.join(base, "podout")) == 3, "podium 3 tiles")
    pp = os.path.join(base, "podout", "podium-sheet.png")
    check(os.path.exists(pp) and Image.open(pp).width == SHEET_W, "podium-sheet width 918")
    for fr in range(30):
        Image.new("RGB", (8, 8)).save(os.path.join(pd, "q-cup-podium-f%d.bmp" % fr), format="BMP")
    cap = find_podium(pd, "q")
    check(len(cap) == 12 and cap[0][0] == 0 and cap[-1][0] == 29, "podium capped to 12 evenly")
    check(do_podium(pd, "nothing", os.path.join(base, "podout2")) == 0, "no podium shots -> 0, no crash")
    print("hires_sheet selftest: %d checks, %d failures" % (n[0], n[1]))
    return 1 if n[1] else 0


# --------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    r = sub.add_parser("race")
    r.add_argument("--shots", required=True)
    r.add_argument("--name", required=True)
    r.add_argument("--series", required=True)
    r.add_argument("--log", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--seat", type=int, default=0)
    r.add_argument("--crop", type=float, default=2.0)
    f = sub.add_parser("files")
    f.add_argument("--out", required=True)
    f.add_argument("--title", default="")
    f.add_argument("items", nargs="+")
    p = sub.add_parser("podium")
    p.add_argument("--shots", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--out", required=True)
    sub.add_parser("selftest")
    try:
        a = ap.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    if a.cmd is None:
        ap.print_usage(sys.stderr)
        return 2
    try:
        if a.cmd == "selftest":
            return selftest()
        if a.cmd == "race":
            if a.crop <= 0:
                print("--crop must be > 0", file=sys.stderr)
                return 2
            n = do_race(a.shots, a.name, parse_series(a.series), a.log, a.out, a.seat, a.crop)
        elif a.cmd == "files":
            pairs = []
            for it in a.items:
                if "=" not in it:
                    print("item needs <label>=<image>: " + it, file=sys.stderr)
                    return 2
                lab, path = it.split("=", 1)
                pairs.append((lab, path))
            n = do_files(a.out, a.title, pairs)
        else:
            n = do_podium(a.shots, a.name, a.out)
    except ValueError as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    except OSError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    print("%d tile(s) written" % n)
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
