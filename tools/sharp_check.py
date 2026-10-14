#!/usr/bin/env python3
"""sharp_check.py - sharpness of the track surface, engine burst against a reference burst.

Burst files: <dir>/<name>-<vblank>.bmp or .png (24-bit RGB, all the same size). The test and
the reference burst must be taken at the same vblanks.

Luma:    Y = 0.299 R + 0.587 G + 0.114 B  (float64)
Regions: lists of rectangles given as fractions of the frame, written x0,x1,y0,y1
         (x range first, then y range). Pixel bounds: rows int(y0*H):int(y1*H),
         cols int(x0*W):int(x1*W).
Energy of one rectangle r: gx = diff(r, axis=1)[:-1, :], gy = diff(r, axis=0)[:, :-1],
         e = gx*gx + gy*gy (both shape (h-1, w-1)). Energy S of a region is the pooled mean:
         sum of e over all its rectangles / total element count of all those e arrays.
Ratio per frame: r = S_test / S_ref (S_ref == 0: "inf" if S_test > 0, else 1.0).

Defaults (calibrated on 1836x432 frames in round 14; other window sizes may need --near/--mid):
  NEAR = (0.14,0.38,0.66,0.96) and (0.62,0.88,0.66,0.96)
  MID  = (0.14,0.86,0.56,0.64)

Thresholds: D1 per track NEAR median r >= 0.95, D2 pooled NEAR median r >= 0.98,
D3 per track MID median r >= 0.90, FLAG on a frame when NEAR r > 1.6.

Subcommands: measure, compare, verdict. Exit: 0 when the computation ran, 2 on usage/file error.
"""
import argparse
import csv
import os
import re
import sys

import numpy as np
from PIL import Image

D1 = 0.95
D2 = 0.98
D3 = 0.90
FLAG = 1.6

DEFAULT_NEAR = [(0.14, 0.38, 0.66, 0.96), (0.62, 0.88, 0.66, 0.96)]
DEFAULT_MID = [(0.14, 0.86, 0.56, 0.64)]

CSV_HEADER = ["label", "vblank", "near_test", "near_ref", "near_r", "mid_test", "mid_ref", "mid_r", "flag"]


class UsageError(Exception):
    pass


def parse_region(s):
    try:
        v = [float(x) for x in s.split(",")]
        if len(v) != 4:
            raise ValueError
        x0, x1, y0, y1 = v
        if not (0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1):
            raise ValueError
        return tuple(v)
    except ValueError:
        raise argparse.ArgumentTypeError("bad region %r (expected x0,x1,y0,y1 with 0 <= x0 < x1 <= 1, same for y)" % s)


def find_burst(d, name):
    if not os.path.isdir(d):
        raise UsageError("directory not found: %s" % d)
    rx = re.compile(r"^" + re.escape(name) + r"-(\d+)\.(bmp|png)$", re.IGNORECASE)
    found = {}
    for fn in os.listdir(d):
        m = rx.match(fn)
        if m:
            v = int(m.group(1))
            if v in found:
                raise UsageError("both %s and %s exist for vblank %d" % (os.path.basename(found[v]), fn, v))
            found[v] = os.path.join(d, fn)
    if not found:
        raise UsageError("no files %s-<vblank>.bmp/.png in %s" % (name, d))
    return dict(sorted(found.items()))


def load_luma(path):
    try:
        with Image.open(path) as im:
            a = np.asarray(im.convert("RGB"), dtype=np.float64)
    except Exception as e:
        raise UsageError("cannot read %s: %s" % (path, e))
    return 0.299 * a[:, :, 0] + 0.587 * a[:, :, 1] + 0.114 * a[:, :, 2]


def region_energy(y, region):
    h, w = y.shape
    total = 0.0
    count = 0
    for (x0, x1, y0, y1) in region:
        r = y[int(y0 * h):int(y1 * h), int(x0 * w):int(x1 * w)]
        if r.shape[0] < 2 or r.shape[1] < 2:
            raise UsageError("rectangle %s is smaller than 2x2 pixels in a %dx%d frame" % ((x0, x1, y0, y1), w, h))
        gx = np.diff(r, axis=1)[:-1, :]
        gy = np.diff(r, axis=0)[:, :-1]
        e = gx * gx + gy * gy
        total += float(e.sum())
        count += e.size
    return total / count


def ratio(test, ref):
    if ref == 0:
        return float("inf") if test > 0 else 1.0
    return test / ref


def header():
    print("sharp_check " + " ".join(sys.argv[1:]))


def regions(a):
    return (a.near if a.near else DEFAULT_NEAR), (a.mid if a.mid else DEFAULT_MID)


def cmd_measure(a):
    header()
    near, mid = regions(a)
    for v, path in find_burst(a.dir, a.name).items():
        y = load_luma(path)
        print("frame %d: NEAR S=%.3f MID S=%.3f" % (v, region_energy(y, near), region_energy(y, mid)))
    return 0


def cmd_compare(a):
    header()
    near, mid = regions(a)
    tf = find_burst(a.test_dir, a.test_name)
    rf = find_burst(a.ref_dir, a.ref_name)
    if set(tf) != set(rf):
        only_t = sorted(set(tf) - set(rf))
        only_r = sorted(set(rf) - set(tf))
        raise UsageError("vblank sets differ: missing in ref: %s; missing in test: %s" % (only_t, only_r))
    label = a.label or a.test_name
    rows = []
    for v in tf:
        yt, yr = load_luma(tf[v]), load_luma(rf[v])
        if yt.shape != yr.shape:
            raise UsageError("size mismatch at vblank %d: %s vs %s" % (v, yt.shape, yr.shape))
        nt, nr = region_energy(yt, near), region_energy(yr, near)
        mt, mr = region_energy(yt, mid), region_energy(yr, mid)
        rn, rm = ratio(nt, nr), ratio(mt, mr)
        flag = rn > FLAG
        rows.append((v, nt, nr, rn, mt, mr, rm, flag))
        print("frame %d: NEAR r=%.3f (S %.2f/%.2f) MID r=%.3f (S %.2f/%.2f)%s"
              % (v, rn, nt, nr, rm, mt, mr, " FLAG" if flag else ""))
    rns = [r[3] for r in rows]
    rms = [r[6] for r in rows]
    nmed, mmed = float(np.median(rns)), float(np.median(rms))
    flagged = [str(r[0]) for r in rows if r[7]]
    print("track %s: frames %d, NEAR median r=%.3f (min %.3f max %.3f), MID median r=%.3f; "
          "D1 NEAR>=%.2f %s; D3 MID>=%.2f %s; flagged frames: %s"
          % (label, len(rows), nmed, min(rns), max(rns), mmed, D1, "PASS" if nmed >= D1 else "FAIL",
             D3, "PASS" if mmed >= D3 else "FAIL", ",".join(flagged) if flagged else "none"))
    if a.csv:
        os.makedirs(os.path.dirname(os.path.abspath(a.csv)), exist_ok=True)
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(CSV_HEADER)
            for v, nt, nr, rn, mt, mr, rm, flag in rows:
                w.writerow([label, v, "%.6f" % nt, "%.6f" % nr, "%.6f" % rn,
                            "%.6f" % mt, "%.6f" % mr, "%.6f" % rm, 1 if flag else 0])
    return 0


def read_csv(path):
    if not os.path.isfile(path):
        raise UsageError("csv not found: %s" % path)
    try:
        with open(path, newline="") as f:
            rd = csv.DictReader(f)
            if rd.fieldnames != CSV_HEADER:
                raise UsageError("unexpected csv header in %s: %s" % (path, rd.fieldnames))
            return [(r["label"], int(r["vblank"]), float(r["near_r"]), float(r["mid_r"]), int(r["flag"]))
                    for r in rd]
    except (ValueError, KeyError) as e:
        raise UsageError("bad csv %s: %s" % (path, e))


def cmd_verdict(a):
    header()
    labels = {}
    order = []
    for p in a.csvs:
        for (lab, v, rn, rm, fl) in read_csv(p):
            if lab not in labels:
                labels[lab] = []
                order.append(lab)
            labels[lab].append((v, rn, rm, fl))
    if not order:
        raise UsageError("no rows in the given csv files")
    failed = []
    all_near = []
    flagged = []
    for lab in order:
        rows = labels[lab]
        nmed = float(np.median([r[1] for r in rows]))
        mmed = float(np.median([r[2] for r in rows]))
        d1, d3 = nmed >= D1, mmed >= D3
        if not d1 and "D1" not in failed:
            failed.append("D1")
        if not d3 and "D3" not in failed:
            failed.append("D3")
        print("%s: NEAR median %.3f D1 %s, MID median %.3f D3 %s"
              % (lab, nmed, "PASS" if d1 else "FAIL", mmed, "PASS" if d3 else "FAIL"))
        all_near += [r[1] for r in rows]
        flagged += ["%s@%d" % (lab, r[0]) for r in rows if r[3]]
    pmed = float(np.median(all_near))
    d2 = pmed >= D2
    if not d2:
        failed.append("D2")
    print("pooled NEAR median %.3f over %d frames: D2 >=%.2f %s" % (pmed, len(all_near), D2, "PASS" if d2 else "FAIL"))
    print("flagged frames: %s" % (" ".join(flagged) if flagged else "none"))
    if failed:
        print("VERDICT D1 D2 D3: FAIL (%s)" % ", ".join(sorted(failed)))
    else:
        print("VERDICT D1 D2 D3: PASS")
    return 0


def build_parser():
    p = argparse.ArgumentParser(
        prog="sharp_check.py",
        description="Track-surface sharpness of an engine screenshot burst against a reference burst "
        "(files <dir>/<name>-<vblank>.bmp or .png). Exit: 0 computation ran, 2 usage/file error.")
    sub = p.add_subparsers(dest="cmd", required=True)

    def regs(sp):
        sp.add_argument("--near", type=parse_region, nargs="+", default=None, metavar="x0,x1,y0,y1",
                        help="replace the default NEAR rectangles")
        sp.add_argument("--mid", type=parse_region, nargs="+", default=None, metavar="x0,x1,y0,y1",
                        help="replace the default MID rectangles")

    m = sub.add_parser("measure", help="energy S of one burst")
    m.add_argument("dir")
    m.add_argument("name")
    regs(m)
    c = sub.add_parser("compare", help="ratio of test burst energy to reference burst energy")
    for x in ("test_dir", "test_name", "ref_dir", "ref_name"):
        c.add_argument(x)
    regs(c)
    c.add_argument("--csv", default=None, help="write per-frame rows to this csv")
    c.add_argument("--label", default=None, help="track label (default: test name)")
    v = sub.add_parser("verdict", help="D1/D2/D3 verdict from compare csv files")
    v.add_argument("csvs", nargs="+", metavar="csv")
    return p


def main():
    a = build_parser().parse_args()
    try:
        return {"measure": cmd_measure, "compare": cmd_compare, "verdict": cmd_verdict}[a.cmd](a)
    except UsageError as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
