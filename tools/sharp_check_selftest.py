#!/usr/bin/env python3
"""Self-test for sharp_check.py (synthetic 200x100 bursts).

Usage: sharp_check_selftest.py <work dir>  (emptied and refilled by the test)
"""
import csv
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "sharp_check.py")
if len(sys.argv) != 2:
    sys.exit("usage: sharp_check_selftest.py <work dir>")
ROOT = os.path.abspath(sys.argv[1])
W, H = 200, 100
VBLANKS = [100, 102, 104]
checks = 0
fails = []


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        fails.append(msg)


def run(*args):
    p = subprocess.run([sys.executable, TOOL] + [str(a) for a in args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def checker(block):
    yy, xx = np.mgrid[0:H, 0:W]
    on = ((yy // block) + (xx // block)) % 2 == 0
    g = np.where(on, 200, 100).astype(np.uint8)
    return np.stack([g, g, g], axis=2)


def flat(level=128):
    return np.full((H, W, 3), level, dtype=np.uint8)


def write(d, name, img, vblanks=VBLANKS, ext="bmp"):
    os.makedirs(d, exist_ok=True)
    for v in vblanks:
        Image.fromarray(img, "RGB").save(os.path.join(d, "%s-%d.%s" % (name, v, ext)))


def rows_of(path):
    with open(path, newline="") as f:
        return list(csv.reader(f))


if os.path.isdir(ROOT):
    shutil.rmtree(ROOT)
os.makedirs(ROOT)
P = lambda *x: os.path.join(ROOT, *x)  # noqa: E731

# a) identical bursts (test as bmp, ref as png)
write(P("a_t"), "t", checker(1))
write(P("a_r"), "r", checker(1), ext="png")
rc, out = run("compare", P("a_t"), "t", P("a_r"), "r", "--csv", P("a.csv"), "--label", "same")
check(rc == 0, "a: exit code: " + out)
check(out.count("NEAR r=1.000") == 3 and out.count("MID r=1.000") == 3, "a: all ratios must be 1.000: " + out)
check("D1 NEAR>=0.95 PASS" in out and "D3 MID>=0.90 PASS" in out and "flagged frames: none" in out, "a: verdicts: " + out)

# b) test 8x8 blocks, ref 1x1 checker -> blurred test
write(P("b_t"), "t", checker(8))
write(P("b_r"), "r", checker(1))
rc, out = run("compare", P("b_t"), "t", P("b_r"), "r", "--csv", P("b.csv"), "--label", "blur")
rs = [float(x.split("NEAR r=")[1].split()[0]) for x in out.splitlines() if x.startswith("frame ")]
check(rc == 0 and len(rs) == 3 and all(r < 0.5 for r in rs), "b: NEAR r < 0.5: " + out)
check("D1 NEAR>=0.95 FAIL" in out, "b: D1 must FAIL: " + out)

# c) reverse -> over-sharp, flagged
rc, out = run("compare", P("b_r"), "r", P("b_t"), "t", "--label", "sharp")
rs = [float(x.split("NEAR r=")[1].split()[0]) for x in out.splitlines() if x.startswith("frame ")]
check(rc == 0 and len(rs) == 3 and all(r > 1.6 for r in rs), "c: NEAR r > 1.6: " + out)
check(out.count(" FLAG") == 3 and "flagged frames: 100,102,104" in out, "c: frames must be FLAGged: " + out)

# d) region mapping: noisy patch exactly inside the first NEAR rectangle (rows 70..90, cols 30..70)
img = flat()
rng = np.random.RandomState(3)
img[70:90, 30:70] = rng.randint(0, 256, (20, 40, 3)).astype(np.uint8)
write(P("d"), "n", img, vblanks=[100])
rc, out = run("measure", P("d"), "n")
line = [x for x in out.splitlines() if x.startswith("frame ")][0]
near_s = float(line.split("NEAR S=")[1].split()[0])
mid_s = float(line.split("MID S=")[1].split()[0])
check(rc == 0 and near_s > 0 and mid_s == 0.0, "d: NEAR S > 0 and MID S == 0: " + out)

# e) vblank set mismatch
write(P("e_t"), "t", checker(1), vblanks=[100, 102, 104])
write(P("e_r"), "r", checker(1), vblanks=[100, 104])
rc, out = run("compare", P("e_t"), "t", P("e_r"), "r")
check(rc == 2 and "102" in out and "missing in ref: [102]" in out, "e: mismatch must exit 2 naming 102: " + out)

# f) missing directory / no files
rc, out = run("measure", P("nonexistent"), "t")
check(rc == 2, "f: missing dir exit 2")
rc, out = run("measure", P("a_t"), "zzz")
check(rc == 2, "f: no files exit 2")

# g) bad region
rc, out = run("measure", P("a_t"), "t", "--near", "0.5,0.2,0.1,0.9")
check(rc == 2, "g: x0 >= x1 exit 2: " + out)
rc, out = run("measure", P("a_t"), "t", "--mid", "0.1,0.2,0.1")
check(rc == 2, "g: wrong field count exit 2")

# h) bmp and png for one vblank
write(P("h"), "t", checker(1), vblanks=[100])
write(P("h"), "t", checker(1), vblanks=[100], ext="png")
rc, out = run("measure", P("h"), "t")
check(rc == 2 and "both" in out, "h: bmp+png exit 2: " + out)

# i) CSV content and verdict
r = rows_of(P("a.csv"))
check(r[0] == ["label", "vblank", "near_test", "near_ref", "near_r", "mid_test", "mid_ref", "mid_r", "flag"]
      and len(r) == 4, "i: csv header and 3 rows: %s" % r)
check(r[1][0] == "same" and r[1][1] == "100" and r[1][4] == "1.000000" and r[1][8] == "0", "i: csv row values: %s" % r[1])
rc, out = run("verdict", P("a.csv"), P("b.csv"))
check(rc == 0 and "VERDICT D1 D2 D3: FAIL" in out and "D1" in out.splitlines()[-1], "i: verdict FAIL naming D1: " + out)
check("blur: NEAR median" in out and "same: NEAR median" in out, "i: per label lines: " + out)
write(P("i2_t"), "t", checker(2))
write(P("i2_r"), "r", checker(2))
rc, out = run("compare", P("i2_t"), "t", P("i2_r"), "r", "--csv", P("i2.csv"), "--label", "same2")
rc, out = run("verdict", P("a.csv"), P("i2.csv"))
check(rc == 0 and out.splitlines()[-1] == "VERDICT D1 D2 D3: PASS" and "flagged frames: none" in out,
      "i: verdict PASS: " + out)

# j) D2: every track passes D1 but the pooled median is below 0.98
hdr = "label,vblank,near_test,near_ref,near_r,mid_test,mid_ref,mid_r,flag\n"
for lab in ("ta", "tb"):
    with open(P("j_%s.csv" % lab), "w") as f:
        f.write(hdr)
        for v in (100, 102, 104):
            f.write("%s,%d,0.960000,1.000000,0.960000,1.000000,1.000000,1.000000,0\n" % (lab, v))
rc, out = run("verdict", P("j_ta.csv"), P("j_tb.csv"))
check(rc == 0 and "D1 PASS" in out and out.splitlines()[-1] == "VERDICT D1 D2 D3: FAIL (D2)", "j: D2 only: " + out)
check("over 6 frames" in out, "j: pooled frame count: " + out)

# k) S_ref == 0
write(P("k_t"), "t", flat())
write(P("k_r"), "r", flat())
rc, out = run("compare", P("k_t"), "t", P("k_r"), "r")
check(rc == 0 and out.count("NEAR r=1.000") == 3 and "MID r=1.000" in out, "k: flat/flat r 1.000: " + out)
write(P("k2_t"), "t", checker(1))
rc, out = run("compare", P("k2_t"), "t", P("k_r"), "r")
check(rc == 0 and "NEAR r=inf" in out and " FLAG" in out, "k: flat ref, busy test -> inf + FLAG: " + out)

# l) tiny rectangle
rc, out = run("measure", P("a_t"), "t", "--near", "0.1,0.101,0.1,0.101")
check(rc == 2, "l: rectangle < 2x2 exit 2: " + out)

if fails:
    print("sharp_check selftest FAILED:")
    for f in fails:
        print(" - " + f)
    print("sharp_check selftest: %d checks, %d failures" % (checks, len(fails)))
    sys.exit(1)
print("sharp_check selftest: %d checks, 0 failures" % checks)
