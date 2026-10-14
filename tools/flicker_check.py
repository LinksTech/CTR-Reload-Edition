#!/usr/bin/env python3
"""flicker_check.py - flicker / z-fighting and change-map measurement for screenshot bursts.

Burst files: <dir>/<name>-<vblank>.bmp (24-bit BMP, all same size).
Oscillating pixel (triple i): d(f0,f1) > T and d(f1,f2) > T and d(f0,f2) <= T/2,
where d = max over R,G,B of the absolute difference.
"""
import argparse
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw


class UsageError(Exception):
    pass


def parse_rect(s):
    try:
        v = [int(x) for x in s.split(",")]
        if len(v) != 4 or v[2] <= 0 or v[3] <= 0:
            raise ValueError
        return tuple(v)
    except ValueError:
        raise argparse.ArgumentTypeError("bad rect %r (expected x,y,w,h)" % s)


def find_burst(d, name):
    if not os.path.isdir(d):
        raise UsageError("directory not found: %s" % d)
    rx = re.compile(r"^" + re.escape(name) + r"-(\d+)\.bmp$", re.IGNORECASE)
    found = {}
    for fn in os.listdir(d):
        m = rx.match(fn)
        if m:
            found[int(m.group(1))] = os.path.join(d, fn)
    if not found:
        raise UsageError("no files %s-<vblank>.bmp in %s" % (name, d))
    return dict(sorted(found.items()))


def load(path):
    try:
        with Image.open(path) as im:
            return np.asarray(im.convert("RGB"), dtype=np.int16).copy()
    except Exception as e:
        raise UsageError("cannot read %s: %s" % (path, e))


def load_burst(files, vblanks=None):
    keys = list(files.keys()) if vblanks is None else list(vblanks)
    imgs = [load(files[v]) for v in keys]
    for v, im in zip(keys, imgs):
        if im.shape != imgs[0].shape:
            raise UsageError("size mismatch at vblank %d: %s vs %s" % (v, im.shape, imgs[0].shape))
    return keys, imgs


def crop_box(shape, crop):
    h, w = shape[:2]
    if crop:
        x, y, cw, ch = crop
        x0, y0, x1, y1 = max(0, x), max(0, y), min(w, x + cw), min(h, y + ch)
        if x1 <= x0 or y1 <= y0:
            raise UsageError("crop outside image (%dx%d)" % (w, h))
        return x0, y0, x1, y1
    return 0, 0, w, h


def make_mask(shape, crop, excludes):
    """Boolean HxW mask of counted pixels (crop minus excludes)."""
    h, w = shape[:2]
    x0, y0, x1, y1 = crop_box(shape, crop)
    mask = np.zeros((h, w), dtype=bool)
    mask[y0:y1, x0:x1] = True
    for (ex, ey, ew, eh) in excludes or []:
        mask[max(0, ey):max(0, ey + eh), max(0, ex):max(0, ex + ew)] = False
    if not mask.any():
        raise UsageError("no counted pixels left after crop/exclude")
    return mask


def dmax(a, b):
    return np.abs(a - b).max(axis=2)


def analyze(keys, imgs, mask, T):
    """Returns list of (vblank_mid, vblanks tuple, count, ratio_percent, osc_mask)."""
    res = []
    n = int(mask.sum())
    half = T / 2.0
    for i in range(len(imgs) - 2):
        d01 = dmax(imgs[i], imgs[i + 1])
        d12 = dmax(imgs[i + 1], imgs[i + 2])
        d02 = dmax(imgs[i], imgs[i + 2])
        osc = (d01 > T) & (d12 > T) & (d02 <= half) & mask
        c = int(osc.sum())
        res.append((keys[i + 1], (keys[i], keys[i + 1], keys[i + 2]), c, 100.0 * c / n, osc))
    return res


def summarize(label, res):
    print("%s: %d triple(s)" % (label, len(res)))
    for mid, vb, c, r, _ in res:
        print("  triple %d,%d,%d: oscillating %d ratio %.4f %%" % (vb[0], vb[1], vb[2], c, r))
    mx = max([r[3] for r in res]) if res else 0.0
    mean = sum(r[3] for r in res) / len(res) if res else 0.0
    print("  max ratio %.4f %%  mean ratio %.4f %%" % (mx, mean))
    return mx, mean


def write_osc_pngs(res, imgs, keys, outdir):
    if not outdir:
        return
    os.makedirs(outdir, exist_ok=True)
    top = sorted([r for r in res if r[3] > 0], key=lambda r: -r[3])[:3]
    for mid, vb, c, r, osc in top:
        mi = keys.index(mid)
        img = (imgs[mi].astype(np.float32) * 0.3).astype(np.uint8)
        img[osc] = (255, 0, 255)
        p = os.path.join(outdir, "osc-%d.png" % mid)
        Image.fromarray(img, "RGB").save(p)
        print("  wrote %s" % p)


def header():
    print("flicker_check " + " ".join(sys.argv[1:]))


def cmd_burst(a):
    header()
    files = find_burst(a.dir, a.name)
    keys, imgs = load_burst(files)
    mask = make_mask(imgs[0].shape, a.crop, a.exclude)
    res = analyze(keys, imgs, mask, a.T)
    print("burst %s/%s: %d images, counted pixels %d, T=%g" % (a.dir, a.name, len(keys), int(mask.sum()), a.T))
    summarize("result", res)
    write_osc_pngs(res, imgs, keys, a.out)
    return 0


def cmd_compare(a):
    header()
    rf = find_burst(a.ref_dir, a.ref_name)
    tf = find_burst(a.test_dir, a.test_name)
    common = sorted(set(rf) & set(tf))
    if not common:
        raise UsageError("no common vblanks (ref %s, test %s)" % (list(rf)[:6], list(tf)[:6]))
    _, ri = load_burst(rf, common)
    _, ti = load_burst(tf, common)
    if ri[0].shape != ti[0].shape:
        raise UsageError("size mismatch ref %s vs test %s" % (ri[0].shape, ti[0].shape))
    mask = make_mask(ri[0].shape, a.crop, a.exclude)
    n = int(mask.sum())
    print("common vblanks: %d, counted pixels %d, T=%g" % (len(common), n, a.T))
    rres = analyze(common, ri, mask, a.T)
    tres = analyze(common, ti, mask, a.T)
    rmax, _ = summarize("ref", rres)
    tmax, _ = summarize("test", tres)
    write_osc_pngs(tres, ti, common, a.out)
    if a.out:
        os.makedirs(a.out, exist_ok=True)
    for v, r, t in zip(common, ri, ti):
        diff = (dmax(r, t) > a.T) & mask
        pct = 100.0 * int(diff.sum()) / n
        print("diff %d: %.4f %%" % (v, pct))
        if a.out:
            tint = t.astype(np.float32)
            tint[diff] = tint[diff] * 0.5 + np.array([255, 0, 0], np.float32) * 0.5
            both = np.concatenate([r.astype(np.uint8), np.clip(tint, 0, 255).astype(np.uint8)], axis=1)
            Image.fromarray(both, "RGB").save(os.path.join(a.out, "diff-%d.png" % v))
    limit = rmax * a.factor + a.margin
    if tmax <= limit:
        print("FLICKER: OK (test max %.4f %% <= ref max %.4f %% * %g + %g)" % (tmax, rmax, a.factor, a.margin))
        return 0
    print("FLICKER: TOO HIGH (test max %.4f %% > ref max %.4f %% * %g + %g)" % (tmax, rmax, a.factor, a.margin))
    return 1


def cmd_sheet(a):
    header()
    files = find_burst(a.dir, a.name)
    keys, imgs = load_burst(files)
    x0, y0, x1, y1 = crop_box(imgs[0].shape, a.crop)
    tw = max(1, int((x1 - x0) * a.scale))
    th = max(1, int((y1 - y0) * a.scale))
    cols = max(1, a.cols)
    rows = (len(keys) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows * th), (0, 0, 0))
    for i, (v, im) in enumerate(zip(keys, imgs)):
        tile = Image.fromarray(im[y0:y1, x0:x1].astype(np.uint8), "RGB").resize((tw, th), Image.LANCZOS)
        dr = ImageDraw.Draw(tile)
        txt = str(v)
        dr.rectangle(dr.textbbox((2, 2), txt), fill=(0, 0, 0))
        dr.text((2, 2), txt, fill=(255, 255, 0))
        sheet.paste(tile, ((i % cols) * tw, (i // cols) * th))
    os.makedirs(os.path.dirname(os.path.abspath(a.out_png)), exist_ok=True)
    sheet.save(a.out_png)
    print("wrote %s (%dx%d, %d tiles)" % (a.out_png, sheet.width, sheet.height, len(keys)))
    return 0


def build_parser():
    p = argparse.ArgumentParser(
        prog="flicker_check.py",
        description="Flicker / z-fighting metric and change map for screenshot bursts "
        "(files <dir>/<name>-<vblank>.bmp). Exit: burst/sheet 0; compare 0=OK, 1=TOO HIGH; 2=usage/file error.")
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp, out=True, full=True):
        sp.add_argument("--crop", type=parse_rect, default=None, metavar="x,y,w,h")
        if full:
            sp.add_argument("--exclude", type=parse_rect, action="append", default=[], metavar="x,y,w,h",
                            help="HUD rect to ignore (repeatable)")
            sp.add_argument("--T", type=float, default=24, help="threshold on max channel abs diff (default 24)")
        if out:
            sp.add_argument("--out", default=None, help="output dir for PNGs")

    b = sub.add_parser("burst", help="metric of one burst")
    b.add_argument("dir")
    b.add_argument("name")
    common(b)
    c = sub.add_parser("compare", help="compare test burst against reference burst")
    for x in ("ref_dir", "ref_name", "test_dir", "test_name"):
        c.add_argument(x)
    common(c)
    c.add_argument("--factor", type=float, default=1.5)
    c.add_argument("--margin", type=float, default=0.05, help="percent points (default 0.05)")
    s = sub.add_parser("sheet", help="contact sheet of a burst")
    s.add_argument("dir")
    s.add_argument("name")
    s.add_argument("out_png")
    common(s, out=False, full=False)
    s.add_argument("--cols", type=int, default=4)
    s.add_argument("--scale", type=float, default=0.5)
    return p


def main():
    a = build_parser().parse_args()
    try:
        return {"burst": cmd_burst, "compare": cmd_compare, "sheet": cmd_sheet}[a.cmd](a)
    except UsageError as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
