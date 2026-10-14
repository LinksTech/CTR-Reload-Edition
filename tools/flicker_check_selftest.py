#!/usr/bin/env python3
"""Self-test for flicker_check.py (synthetic 64x48 bursts).

Usage: flicker_check_selftest.py <work dir>  (emptied and refilled by the test)
"""
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "flicker_check.py")
if len(sys.argv) != 2:
    sys.exit("usage: flicker_check_selftest.py <work dir>")
ROOT = os.path.abspath(sys.argv[1])
W, H = 64, 48
checks = 0
fails = []


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        fails.append(msg)


def make(d, name, toggle, n=6):
    os.makedirs(d, exist_ok=True)
    rng = np.random.RandomState(1)
    base = rng.randint(60, 100, (H, W, 3)).astype(np.uint8)
    for i in range(n):
        im = base.copy()
        x = 4 + 2 * i
        im[30:36, x:x + 6] = (250, 250, 250)  # moving square, 2 px per frame
        if toggle:
            im[5:9, 50:54] = (200, 20, 20) if i % 2 == 0 else (20, 200, 20)
        Image.fromarray(im, "RGB").save(os.path.join(d, "%s-%d.bmp" % (name, 100 + 2 * i)))


def run(*args):
    p = subprocess.run([sys.executable, TOOL] + [str(a) for a in args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


if os.path.isdir(ROOT):
    shutil.rmtree(ROOT)
ref, tst, same = os.path.join(ROOT, "ref"), os.path.join(ROOT, "tog"), os.path.join(ROOT, "same")
make(ref, "b", False)
make(tst, "b", True)
make(same, "b", False)

rc, out = run("burst", ref, "b")
check(rc == 0 and "max ratio 0.0000 %" in out, "static+moving square should be 0: " + out)

rc, out = run("burst", tst, "b", "--out", os.path.join(ROOT, "o1"))
exp = "%.4f" % (100.0 * 16 / (W * H))
check(rc == 0 and out.count("oscillating 16 ") == 4 and ("ratio %s %%" % exp) in out,
      "toggle block should be 16 px: " + out)
check(os.path.exists(os.path.join(ROOT, "o1", "osc-104.png")), "osc png missing")

rc, out = run("burst", tst, "b", "--crop", "0,20,64,28")
check(rc == 0 and "max ratio 0.0000 %" in out, "crop excluding block should be 0: " + out)
rc, out = run("burst", tst, "b", "--exclude", "48,3,8,8")
check(rc == 0 and "max ratio 0.0000 %" in out, "exclude over block should be 0: " + out)

rc, out = run("compare", ref, "b", tst, "b", "--out", os.path.join(ROOT, "o2"))
check(rc == 1 and "FLICKER: TOO HIGH" in out, "compare toggle should be TOO HIGH: " + out)
check(os.path.exists(os.path.join(ROOT, "o2", "diff-100.png")), "diff png missing")
check(Image.open(os.path.join(ROOT, "o2", "diff-100.png")).size == (2 * W, H), "diff png size")

rc, out = run("compare", ref, "b", same, "b", "--out", os.path.join(ROOT, "o3"))
check(rc == 0 and "FLICKER: OK" in out and "diff 100: 0.0000 %" in out, "identical should be OK: " + out)

rc, out = run("burst", ref, "nonexistent")
check(rc == 2, "missing files should exit 2")
rc, out = run("compare", ref, "b", ref, "zzz")
check(rc == 2, "compare missing exit 2")

sheet = os.path.join(ROOT, "sheet.png")
rc, out = run("sheet", ref, "b", sheet, "--cols", "4", "--scale", "0.5")
check(rc == 0 and os.path.exists(sheet) and Image.open(sheet).size == (4 * 32, 2 * 24), "sheet size: " + out)

if fails:
    print("flicker_check selftest FAILED:")
    for f in fails:
        print(" - " + f)
    sys.exit(1)
print("flicker_check selftest passed: %d checks" % checks)
