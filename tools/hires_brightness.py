#!/usr/bin/env python3
"""hires_brightness.py - mean brightness of a driver model's pixels in screenshots.

Answers "is the hi-res driver as bright as its classic model?" and "does Reload Studio's preview show
the brightness the game shows?" with one number per picture: the mean of the model's pixels only, the
background, the floor and the road left out. Two kinds of pictures, each with its own mask:

STUDIO  (--pair LABEL DARK LIGHT) two shots of Reload Studio's preview in the same view, one with the
        dark and one with the light background (automation "view-bg dark" / "view-bg light"). The
        Studio paints background and floor in the background's own colours, the model the same in both
        (its light does not depend on the background), so a model pixel is a pixel that is the same in
        both shots (every channel within --tol). Everything else of the window (the page around the
        preview, labels, buttons) is the same in both too, so the mask is taken only inside the box of
        the pixels that differ (the preview), and parts smaller than --min-part of the mask's largest
        part (the lit button of the preview's bar, a label; default 0.05) are dropped. Edge pixels that
        mix model and background differ and stay out. Why this key and not a shot of the empty stage:
        the Studio shows no stage without a model (the preview then shows a message), the background
        switch exists for users, and the key needs no colour of the stage to be known - it holds for
        any stage whose background and floor change with the switch. If a stage keeps a part the same
        in both colours, give --plate (a shot of the empty stage) and no --shot: the mask is then the
        pixels of DARK that differ from it.
GAME    (--shot LABEL GROUP PICTURE) race shots of the same deterministic run setup at the same frame
        (same level, driver, switches, vblank): the camera behind seat 0 is the same, so the road, the
        sky and the other karts are the same in every shot and only seat 0's model (with its shadow)
        differs. Measured inside the fixed --region around seat 0's kart. The background is not known
        from an empty run, so it is taken from the shots themselves: the plate of a pixel is the value
        at least two shots of different GROUPs agree on (within --tol); a pixel with a plate is a model
        pixel of a shot when the shot differs from the plate, a pixel without a plate (every shot shows
        something of its own there) is a model pixel of every shot. GROUP: shots whose models may have
        the same colour at the same pixel (the same geometry and texture, e.g. one model in two vertex
        colour modes) share a group, so their agreement never makes a plate. The more different models
        at that frame, the better the plate; the table says per shot how much of its mask came from
        "no plate". --plate PICTURE instead: a picture of that frame without the model.

Brightness: Y = 0.299 R + 0.587 G + 0.114 B on the picture's bytes (the display curve, as seen), and
the mean of R, G and B. Pictures: .bmp or .png, read with Pillow as RGB.

Checks (any number):
  --within A B P   PASS when |Y(A) / Y(B) - 1| <= P %   (e.g. hi-res PSX against the classic look)
  --below A B P    PASS when Y(A) <= Y(B) * (1 - P / 100) (e.g. glTF mode clearly darker)
--shadow-cut (GAME): a pixel where the shot is its plate darkened evenly (every channel by the same
factor 0.15..0.95, within 0.04) is the kart's shadow on the road and leaves the mask.
--ref LABEL adds the column "vs ref" (Y / Y(ref) - 1 in %). --mask-dir DIR writes LABEL-mask.png per
picture (model pixels as they are, the rest dark) to check the mask by eye.

Usage:
  hires_brightness.py measure [--pair LABEL DARK LIGHT]... [--shot LABEL GROUP PICTURE]...
                              [--region X0,Y0,X1,Y1] [--plate PICTURE] [--tol N] [--min-part F]
                              [--shadow-cut] [--ref LABEL] [--within A B P]... [--below A B P]...
                              [--mask-dir DIR]
  hires_brightness.py selftest
Exit: 0 measured and every check passed, 1 a check failed or a mask is empty, 2 usage or file error.
"""
import argparse
import os
import sys

import numpy as np

STUDIO_TOL = 2
GAME_TOL = 6
MIN_PART = 0.05
SHADOW_SPREAD = 0.04
LUMA = np.array([0.299, 0.587, 0.114])


class UsageError(Exception):
    pass


def load(path):
    from PIL import Image
    try:
        with Image.open(path) as im:
            return np.asarray(im.convert("RGB")).astype(np.int16)
    except OSError as e:
        raise UsageError("cannot read %s: %s" % (path, e))


def same(a, b, tol):
    """Per pixel: every channel of a and b within tol."""
    return np.abs(a - b).max(2) <= tol


def components(mask):
    """4-connected parts of a boolean mask: (label image, sizes). Runs of a row are joined with the
    runs of the row above that they touch (union-find over runs)."""
    h, w = mask.shape
    parent = []

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    runs = []  # (row, x0, x1, id)
    prev = []
    for y in range(h):
        row = mask[y]
        if not row.any():
            prev = []
            continue
        d = np.diff(np.concatenate(([0], row.astype(np.int8), [0])))
        starts, ends = np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]
        cur = []
        k = 0
        for x0, x1 in zip(starts.tolist(), ends.tolist()):
            rid = len(parent)
            parent.append(rid)
            while k < len(prev) and prev[k][1] <= x0:
                k += 1
            j = k
            while j < len(prev) and prev[j][0] < x1:
                a, b = find(prev[j][2]), find(rid)
                if a != b:
                    parent[max(a, b)] = min(a, b)
                j += 1
            cur.append((x0, x1, rid))
            runs.append((y, x0, x1, rid))
        prev = cur
    labels = np.zeros((h, w), np.int32)
    roots = {}
    sizes = [0]
    for y, x0, x1, rid in runs:
        r = find(rid)
        if r not in roots:
            roots[r] = len(sizes)
            sizes.append(0)
        lab = roots[r]
        labels[y, x0:x1] = lab
        sizes[lab] += x1 - x0
    return labels, np.array(sizes)


def drop_small(mask, min_part):
    """The mask without parts smaller than min_part of its largest part."""
    if not mask.any() or min_part <= 0:
        return mask
    labels, sizes = components(mask)
    keep = sizes >= min_part * sizes[1:].max()
    keep[0] = False
    return keep[labels]


def studio_mask(dark, light, tol, min_part):
    if dark.shape != light.shape:
        raise UsageError("the two shots of a pair differ in size: %s and %s" % (dark.shape[:2], light.shape[:2]))
    diff = ~same(dark, light, tol)
    mask = np.zeros(dark.shape[:2], bool)
    if not diff.any():
        return mask, None
    ys, xs = np.nonzero(diff)
    box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    x0, y0, x1, y1 = box
    mask[y0:y1, x0:x1] = ~diff[y0:y1, x0:x1]
    return drop_small(mask, min_part), box


def shadow_pixels(shot, plate, known):
    """Pixels where the shot is the plate darkened evenly (every channel the same factor 0.15..0.95,
    within SHADOW_SPREAD): the kart's shadow on the road, not the model."""
    p = plate.astype(np.float64)
    ok = known & (p.min(2) >= 16)
    k = shot.astype(np.float64) / np.maximum(p, 1.0)
    kmean = k.mean(2)
    return ok & (np.abs(k - kmean[..., None]).max(2) <= SHADOW_SPREAD) & (kmean >= 0.15) & (kmean <= 0.95)


def game_masks(shots, groups, tol, min_part, plate=None, shadow_cut=False):
    """Masks of seat 0's model in shots of the same frame (all cropped to the region already).
    Returns (masks, unknown) - unknown: per shot the mask pixels that had no plate."""
    n = len(shots)
    h, w = shots[0].shape[:2]
    if plate is not None:
        known = np.ones((h, w), bool)
        pl = plate
    else:
        support = np.zeros((n, h, w), np.int16)
        for i in range(n):
            for j in range(i + 1, n):
                if groups[i] == groups[j]:
                    continue
                a = same(shots[i], shots[j], tol)
                support[i] += a
                support[j] += a
        best = support.argmax(0)
        known = support.max(0) >= 1
        stack = np.stack(shots)
        pl = np.take_along_axis(stack, best[None, :, :, None], 0)[0]
    masks, unknown = [], []
    for s in shots:
        m = ~known | ~same(s, pl, tol)
        if shadow_cut:
            m &= ~shadow_pixels(s, pl, known)
        m = drop_small(m, min_part)
        masks.append(m)
        unknown.append(int((m & ~known).sum()))
    return masks, unknown


def stats(img, mask):
    n = int(mask.sum())
    if n == 0:
        return dict(n=0, Y=float("nan"), R=float("nan"), G=float("nan"), B=float("nan"))
    px = img[mask].astype(np.float64)
    m = px.mean(0)
    return dict(n=n, Y=float((px @ LUMA).mean()), R=float(m[0]), G=float(m[1]), B=float(m[2]))


def parse_region(text):
    try:
        v = [int(t) for t in text.split(",")]
    except ValueError:
        v = []
    if len(v) != 4 or v[0] >= v[2] or v[1] >= v[3] or min(v) < 0:
        raise UsageError("--region wants X0,Y0,X1,Y1 with X0 < X1 and Y0 < Y1, not %s" % text)
    return v


def write_mask(path, img, mask):
    from PIL import Image
    out = (img.astype(np.float64) * 0.15).astype(np.uint8)
    out[mask] = img[mask].astype(np.uint8)
    Image.fromarray(out).save(path)


def measure(args, out=print):
    if not args.pair and not args.shot:
        raise UsageError("give at least one --pair or --shot")
    rows = []  # (label, kind, stats, note)
    images = {}
    for label, dark_p, light_p in args.pair or []:
        dark, light = load(dark_p), load(light_p)
        tol = STUDIO_TOL if args.tol is None else args.tol
        if args.plate and not args.shot:
            plate = load(args.plate)
            if plate.shape != dark.shape:
                raise UsageError("--plate %s differs in size from %s" % (args.plate, dark_p))
            mask = drop_small(~same(dark, plate, tol), args.min_part)
            note = "plate"
        else:
            mask, box = studio_mask(dark, light, tol, args.min_part)
            note = "key dark/light, preview box %s" % (box,)
        rows.append((label, "studio", stats(dark, mask), note))
        images[label] = (dark, mask)
    if args.shot:
        region = parse_region(args.region) if args.region else None
        if region is None:
            raise UsageError("--shot needs --region X0,Y0,X1,Y1 (the box around seat 0's kart)")
        x0, y0, x1, y1 = region
        full = [load(p) for _l, _g, p in args.shot]
        for (label, _g, p), im in zip(args.shot, full):
            if im.shape != full[0].shape:
                raise UsageError("%s is %dx%d, the first shot %dx%d" % (p, im.shape[1], im.shape[0],
                                                                        full[0].shape[1], full[0].shape[0]))
            if x1 > im.shape[1] or y1 > im.shape[0]:
                raise UsageError("--region %s leaves %s (%dx%d)" % (args.region, p, im.shape[1], im.shape[0]))
        crops = [im[y0:y1, x0:x1] for im in full]
        plate = None
        if args.plate:
            pim = load(args.plate)
            if pim.shape != full[0].shape:
                raise UsageError("--plate %s differs in size from the shots" % args.plate)
            plate = pim[y0:y1, x0:x1]
        groups = [g for _l, g, _p in args.shot]
        if plate is None and len(set(groups)) < 2:
            raise UsageError("the plate needs shots of at least two groups (or --plate)")
        tol = GAME_TOL if args.tol is None else args.tol
        masks, unknown = game_masks(crops, groups, tol, args.min_part, plate, args.shadow_cut)
        for (label, g, _p), c, m, u in zip(args.shot, crops, masks, unknown):
            note = ("plate picture" if plate is not None else
                    "group %s, region %d,%d,%d,%d, %d mask pixels without plate" % (g, x0, y0, x1, y1, u))
            rows.append((label, "game", stats(c, m), note))
            images[label] = (c, m)
    labels = [r[0] for r in rows]
    if len(set(labels)) != len(labels):
        raise UsageError("labels must differ: %s" % labels)
    by = {r[0]: r[2] for r in rows}
    for a, b, _p in (args.within or []) + (args.below or []):
        for x in (a, b):
            if x not in by:
                raise UsageError("check names %s, not a label (%s)" % (x, ", ".join(labels)))
    if args.ref and args.ref not in by:
        raise UsageError("--ref %s is not a label" % args.ref)
    out("label\tkind\tpixels\tY\tR\tG\tB" + ("\tvs %s" % args.ref if args.ref else "") + "\tmask")
    bad = 0
    for label, kind, s, note in rows:
        line = "%s\t%s\t%d\t%.1f\t%.1f\t%.1f\t%.1f" % (label, kind, s["n"], s["Y"], s["R"], s["G"], s["B"])
        if args.ref and s["n"] and by[args.ref]["n"]:
            line += "\t%+.1f %%" % (100.0 * (s["Y"] / by[args.ref]["Y"] - 1.0))
        elif args.ref:
            line += "\t-"
        out(line + "\t" + note)
        if s["n"] == 0:
            out("EMPTY %s: no model pixel in the mask" % label)
            bad += 1
    for a, b, p in args.within or []:
        p = float(p)
        r = by[a]["Y"] / by[b]["Y"] - 1.0 if by[b]["n"] and by[a]["n"] else float("nan")
        ok = bool(abs(r) * 100.0 <= p)
        out("%s within %s %s %%: %+.1f %% -> %s" % (a, b, p, 100.0 * r, "PASS" if ok else "FAIL"))
        bad += not ok
    for a, b, p in args.below or []:
        p = float(p)
        r = by[a]["Y"] / by[b]["Y"] - 1.0 if by[b]["n"] and by[a]["n"] else float("nan")
        ok = bool(r * 100.0 <= -p)
        out("%s at least %s %% below %s: %+.1f %% -> %s" % (a, p, b, 100.0 * r, "PASS" if ok else "FAIL"))
        bad += not ok
    if args.mask_dir:
        for label, (img, m) in images.items():
            write_mask(os.path.join(args.mask_dir, label + "-mask.png"), img, m)
    return 1 if bad else 0


# ------------------------------------------------------------------ selftest
def selftest():
    import tempfile
    from PIL import Image
    fails = []
    checks = [0]

    def check(name, cond):
        checks[0] += 1
        if not cond:
            fails.append(name)
            print("FAIL " + name)

    # STUDIO: page grey around a preview; background and floor in dark and light colours; a model of
    # two colours; a button of the bar the same in both; a mixed edge column.
    def studio_pair():
        h, w = 120, 200
        page = np.full((h, w, 3), 50, np.int16)
        pics = []
        for bg, fl in (((30, 32, 35), (56, 60, 66)), ((228, 230, 233), (196, 200, 206))):
            im = page.copy()
            im[10:110, 60:190] = bg
            im[70:110, 60:190] = fl
            im[40:80, 100:140] = (200, 100, 50)      # model, 1600 px
            im[40:60, 100:140] = (100, 200, 50)      # its top half another colour
            im[40:80, 140] = (np.array(bg) + np.array((200, 100, 50))) // 2  # edge column, mixes the bg
            im[100:104, 62:66] = (90, 90, 90)        # a button of the bar, 16 px, the same in both
            pics.append(im)
        return pics

    dark, light = studio_pair()
    m, box = studio_mask(dark, light, STUDIO_TOL, MIN_PART)
    truth = np.zeros(dark.shape[:2], bool)
    truth[40:80, 100:140] = True
    check("studio mask is the model only", bool((m == truth).all()))
    check("studio preview box", box == (60, 10, 190, 110))
    s = stats(dark, m)
    yt = (800 * (LUMA @ np.array((200, 100, 50))) + 800 * (LUMA @ np.array((100, 200, 50)))) / 1600
    check("studio mean Y exact", abs(s["Y"] - yt) < 1e-9 and s["n"] == 1600)
    check("studio mean R exact", abs(s["R"] - 150.0) < 1e-9)
    m2, _b = studio_mask(dark, light, STUDIO_TOL, 0.0)
    check("studio without the part filter keeps the button", int(m2.sum()) == 1616)

    # GAME: a road texture without randomness; five shots: A1/A2 one model in two colour modes (same
    # shape, partly the same colours), B a wider model, C a smaller one, D a small one elsewhere (so
    # that two shots show the road wherever a pixel is road in any of A, B, C).
    h, w = 90, 160
    yy, xx = np.mgrid[0:h, 0:w]
    road = np.stack([(60 + (xx * 7 + yy * 13) % 40), (62 + (xx * 5 + yy * 11) % 40), (70 + (xx * 3 + yy * 7) % 40)],
                    2).astype(np.int16)

    def put(base, y0, y1, x0, x1, col):
        im = base.copy()
        im[y0:y1, x0:x1] = col
        return im

    a1 = put(road, 30, 70, 50, 110, (180, 120, 60))
    a1[30:50, 50:110] = (128, 128, 128)          # the half that is the same in A1 and A2
    a2 = put(road, 30, 70, 50, 110, (90, 60, 30))
    a2[30:50, 50:110] = (128, 128, 128)
    bb = put(road, 35, 75, 30, 130, (40, 160, 200))
    cc = put(road, 40, 66, 60, 100, (220, 220, 40))
    dd = put(road, 5, 15, 5, 20, (250, 250, 250))
    shots = [a1, a2, bb, cc, dd]
    masks, unknown = game_masks(shots, ["a", "a", "b", "c", "d"], GAME_TOL, 0.0)
    ta = np.zeros((h, w), bool)
    ta[30:70, 50:110] = True
    tb = np.zeros((h, w), bool)
    tb[35:75, 30:130] = True
    tc = np.zeros((h, w), bool)
    tc[40:66, 60:100] = True
    check("game mask A1 (same colours as A2 kept)", bool((masks[0] == ta).all()))
    check("game mask A2", bool((masks[1] == ta).all()))
    check("game mask B", bool((masks[2] == tb).all()))
    check("game mask C", bool((masks[3] == tc).all()))
    s1, s2 = stats(a1, masks[0]), stats(a2, masks[1])
    y1 = (1200 * (LUMA @ np.array((128, 128, 128))) + 1200 * (LUMA @ np.array((180, 120, 60)))) / 2400
    check("game mean Y of A1 exact", abs(s1["Y"] - y1) < 1e-9)
    check("game A2 darker than A1", s2["Y"] < s1["Y"])
    # where every shot covers the pixel (no plate) the mask is the model in every shot
    check("game pixels without a plate counted", unknown[3] == int(tc.sum()))
    # the limit of the plate: where only D shows the road, D gets those pixels too (and says so)
    check("game: one road shot alone makes no plate", int(masks[4].sum()) == 150 + int(tc.sum()) and
          unknown[4] == int(tc.sum()))
    # the same groups for all: no plate anywhere, every pixel is a model pixel
    masks_g, _u = game_masks(shots, ["a"] * 5, GAME_TOL, 0.0)
    check("one group gives no plate", bool(masks_g[0].all()))
    # with a plate picture
    masks_p, unk_p = game_masks([a1, bb], ["x", "y"], GAME_TOL, 0.0, plate=road)
    check("game mask with --plate", bool((masks_p[0] == ta).all()) and bool((masks_p[1] == tb).all()))
    check("plate picture leaves nothing unknown", unk_p == [0, 0])
    # A1 and A2 in different groups: their neutral half agrees and becomes "plate" - the reason for groups
    masks_w, _u = game_masks(shots, ["a", "x", "b", "c", "d"], GAME_TOL, 0.0)
    check("without groups the neutral half is lost", int(masks_w[0].sum()) < int(ta.sum()))

    # the shadow cut: a shadow (road x 0.5) beside B leaves B's mask only with --shadow-cut
    bs = bb.copy()
    bs[75:80, 30:130] = road[75:80, 30:130] // 2
    sh = [a1, a2, bs, cc, dd]
    m_ns, _u = game_masks(sh, ["a", "a", "b", "c", "d"], GAME_TOL, 0.0)
    m_sc, _u = game_masks(sh, ["a", "a", "b", "c", "d"], GAME_TOL, 0.0, shadow_cut=True)
    check("shadow counts as model without the cut", int(m_ns[2].sum()) == int(tb.sum()) + 500)
    check("shadow cut leaves the model only", bool((m_sc[2] == tb).all()) and bool((m_sc[0] == ta).all()))

    # components and the part filter
    cm = np.zeros((20, 20), bool)
    cm[2:10, 2:10] = True
    cm[12:14, 12:14] = True
    cm[2:4, 10:12] = True                         # touches the first part at x = 10
    lab, sizes = components(cm)
    check("components: two parts", len(sizes) == 3 and sorted(sizes[1:].tolist()) == [4, 68])
    check("part filter drops the small part", int(drop_small(cm, 0.1).sum()) == 68)
    um = np.zeros((6, 8), bool)
    um[0:6, 0] = um[5, 0:8] = um[0:6, 7] = True    # a U: joined only through the bottom row
    check("components: a U is one part", len(components(um)[1]) == 2)

    # the command line, through files: table, ref, within, below, exit codes, mask pictures
    with tempfile.TemporaryDirectory(dir=os.environ.get("TMP") or None) as td:
        def save(name, arr):
            p = os.path.join(td, name)
            Image.fromarray(arr.astype(np.uint8)).save(p)
            return p
        pd, pl = save("d.png", dark), save("l.png", light)
        pa1, pa2, pb, pc = save("a1.png", a1), save("a2.png", a2), save("b.bmp", bb), save("c.png", cc)
        pdd = save("dd.png", dd)
        lines = []
        rc = main(["measure", "--pair", "studio", pd, pl, "--shot", "classic", "a", pa1, "--shot", "psx", "a", pa2,
                   "--shot", "b", "b", pb, "--shot", "c", "c", pc, "--shot", "d", "d", pdd,
                   "--region", "0,0,160,90", "--ref", "classic",
                   "--below", "psx", "classic", "20", "--mask-dir", td], out=lines.append)
        check("measure exit 0", rc == 0)
        check("table has a row per picture", sum(1 for x in lines if x.split("\t")[0] in
                                                  ("studio", "classic", "psx", "b", "c")) == 5)
        check("below check passes", any(x.startswith("psx at least 20.0 % below classic") and x.endswith("PASS")
                                        for x in lines))
        check("mask pictures written", os.path.isfile(os.path.join(td, "psx-mask.png")))
        lines = []
        rc = main(["measure", "--shot", "classic", "a", pa1, "--shot", "psx", "a", pa2, "--shot", "b", "b", pb,
                   "--region", "0,0,160,90", "--within", "psx", "classic", "10"], out=lines.append)
        check("within check fails, exit 1", rc == 1 and any(x.endswith("FAIL") for x in lines))
        rc = main(["measure", "--shot", "x", "a", pa1, "--region", "0,0,160,90"], out=lines.append)
        check("one group without --plate is a usage error", rc == 2)
        small = save("s.png", a1[:40])
        rc = main(["measure", "--shot", "x", "a", pa1, "--shot", "y", "b", small, "--region", "0,0,160,90"],
                  out=lines.append)
        check("size mismatch is a usage error", rc == 2)
        rc = main(["measure", "--shot", "x", "a", pa1, "--shot", "y", "b", pb, "--region", "0,0,999,90"],
                  out=lines.append)
        check("region outside the picture is a usage error", rc == 2)
        lines = []
        rc = main(["measure", "--pair", "empty", pd, pd], out=lines.append)
        check("a pair without differences is EMPTY, exit 1", rc == 1 and any(x.startswith("EMPTY") for x in lines))
    print("hires_brightness selftest: %d checks, %d failures" % (checks[0], len(fails)))
    return 1 if fails else 0


def main(argv=None, out=print):
    ap = argparse.ArgumentParser(prog="hires_brightness.py", description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    m = sub.add_parser("measure")
    m.add_argument("--pair", nargs=3, action="append", metavar=("LABEL", "DARK", "LIGHT"))
    m.add_argument("--shot", nargs=3, action="append", metavar=("LABEL", "GROUP", "PICTURE"))
    m.add_argument("--region")
    m.add_argument("--plate")
    m.add_argument("--tol", type=int)
    m.add_argument("--min-part", type=float, default=MIN_PART)
    m.add_argument("--shadow-cut", action="store_true")
    m.add_argument("--ref")
    m.add_argument("--within", nargs=3, action="append", metavar=("A", "B", "PERCENT"))
    m.add_argument("--below", nargs=3, action="append", metavar=("A", "B", "PERCENT"))
    m.add_argument("--mask-dir")
    sub.add_parser("selftest")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    if args.cmd == "selftest":
        return selftest()
    if args.cmd != "measure":
        ap.print_help()
        return 2
    try:
        for _a, _b, p in (args.within or []) + (args.below or []):
            float(p)
        return measure(args, out)
    except (UsageError, ValueError) as e:
        out("error: %s" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main())
