#!/usr/bin/env python3
"""hires_mask_sharp.py - sharpness of the hi-res driver texture, measured only on driver pixels.

Three screenshots of the same still view (same camera, same pose) are given:
  FULL  driver drawn with its full-resolution texture
  SMALL driver drawn with the texture reduced to 64x64
  KEY   driver drawn with a flat magenta key texture (255,0,255) modulated by grey vertex
        shading 0.45..1.0, so a key pixel is (255*s, 0, 255*s)
The mask comes from KEY. The sharpness ratio S(FULL)/S(SMALL) is computed on the eroded mask
only, so a background that differs between the images (e.g. a waving flag) does not matter.

Images: .bmp or .png, 24/32-bit, read with Pillow, converted to RGB uint8. All three must have
        the same size, else usage error (exit 2).
Luma:   Y = 0.299 R + 0.587 G + 0.114 B  (float64), same as sharp_check.py.
Key pixel (of KEY): G <= 48 and R >= 64 and B >= 64 and abs(R - B) <= max(24, 0.25 * max(R, B)).
Mask:   key pixels, eroded with a square window of radius N (--erode, default 3): a pixel stays
        when every pixel within Chebyshev distance N is a key pixel (pixels outside the frame
        count as non-key). N = 0 means no erosion.
Energy: for pixel (i, j), i < H-1, j < W-1: gx = Y[i,j+1] - Y[i,j], gy = Y[i+1,j] - Y[i,j],
        e = gx*gx + gy*gy. A pixel is valid when mask[i,j] and mask[i,j+1] and mask[i+1,j].
S(image) = mean of e over the valid pixels (the same valid set for FULL and SMALL).
ratio = S(FULL) / S(SMALL)  (S(SMALL) == 0: "inf" if S(FULL) > 0 else 1.0).
Verdict: PASS when ratio >= threshold (--threshold, default 1.5), FAIL otherwise; EMPTY when
        there are 0 valid pixels; SMALL-MASK when valid pixels < 2000 (ratio still printed).
Information only: share of valid pixels where |Y_full - Y_small| > 2 ("differing") and the
        ratio on those pixels alone.

Subcommands: compare, selftest. Exit: 0 when the computation ran, 2 on usage/file error;
selftest exits 0 without failures, else 1.
"""
import argparse
import csv
import os
import sys

import numpy as np
from PIL import Image

KEY_G_MAX = 48
KEY_RB_MIN = 64
KEY_RB_ABS = 24
KEY_RB_REL = 0.25
MIN_VALID = 2000
DIFF_THRESHOLD = 2.0
DEFAULT_ERODE = 3
DEFAULT_THRESHOLD = 1.5
CROP_PAD = 16
CROP_MAX_WIDTH = 2400

CSV_HEADER = ["label", "full", "small", "key", "erode", "key_px", "mask_px", "valid_px",
              "s_full", "s_small", "ratio", "verdict", "differing_share", "ratio_differing"]


class UsageError(Exception):
    pass


def load_rgb(path):
    try:
        with Image.open(path) as im:
            return np.asarray(im.convert("RGB"), dtype=np.uint8)
    except Exception as e:
        raise UsageError("cannot read %s: %s" % (path, e))


def luma(a):
    a = a.astype(np.float64)
    return 0.299 * a[:, :, 0] + 0.587 * a[:, :, 1] + 0.114 * a[:, :, 2]


def key_mask(rgb):
    r = rgb[:, :, 0].astype(np.float64)
    g = rgb[:, :, 1].astype(np.float64)
    b = rgb[:, :, 2].astype(np.float64)
    return (g <= KEY_G_MAX) & (r >= KEY_RB_MIN) & (b >= KEY_RB_MIN) & \
        (np.abs(r - b) <= np.maximum(KEY_RB_ABS, KEY_RB_REL * np.maximum(r, b)))


def erode(m, n):
    """Square erosion of radius n; outside the frame counts as False."""
    if n <= 0:
        return m.copy()
    h, w = m.shape
    p = np.zeros((h + 2 * n, w + 2 * n), dtype=bool)
    p[n:n + h, n:n + w] = m
    # separable: along x, then along y
    t = p[:, 0:w].copy()
    for k in range(1, 2 * n + 1):
        t &= p[:, k:k + w]
    out = t[0:h, :].copy()
    for k in range(1, 2 * n + 1):
        out &= t[k:k + h, :]
    return out


def check_sizes(*arrs):
    s = arrs[0].shape
    for a in arrs[1:]:
        if a.shape != s:
            raise UsageError("size mismatch: %dx%d vs %dx%d" % (s[1], s[0], a.shape[1], a.shape[0]))


def ratio_of(test, ref):
    if ref == 0:
        return float("inf") if test > 0 else 1.0
    return test / ref


def energy_map(y):
    gx = y[:-1, 1:] - y[:-1, :-1]
    gy = y[1:, :-1] - y[:-1, :-1]
    return gx * gx + gy * gy


def valid_map(mask):
    return mask[:-1, :-1] & mask[:-1, 1:] & mask[1:, :-1]


def measure(full, small, key, erode_n, use_mask=True):
    """full/small/key: RGB uint8 arrays. Returns a dict of results."""
    check_sizes(full, small, key)
    h, w = key.shape[:2]
    km = key_mask(key)
    if use_mask:
        mask = erode(km, erode_n)
    else:
        mask = np.ones((h, w), dtype=bool)
    yf, ys = luma(full), luma(small)
    valid = valid_map(mask)
    nvalid = int(valid.sum())
    res = {"frame": (w, h), "key_px": int(km.sum()), "mask_px": int(mask.sum()),
           "valid_px": nvalid, "mask": mask, "bbox": None}
    if mask.any():
        ys_, xs_ = np.nonzero(mask)
        res["bbox"] = (int(xs_.min()), int(ys_.min()), int(xs_.max()), int(ys_.max()))
    if nvalid == 0:
        res.update(s_full=float("nan"), s_small=float("nan"), ratio=float("nan"),
                   differing_share=float("nan"), ratio_differing=None)
        return res
    ef, es = energy_map(yf), energy_map(ys)
    sf, ss = float(ef[valid].mean()), float(es[valid].mean())
    res["s_full"], res["s_small"], res["ratio"] = sf, ss, ratio_of(sf, ss)
    diff = valid & (np.abs(yf[:-1, :-1] - ys[:-1, :-1]) > DIFF_THRESHOLD)
    nd = int(diff.sum())
    res["differing_share"] = nd / nvalid
    res["ratio_differing"] = ratio_of(float(ef[diff].mean()), float(es[diff].mean())) if nd else None
    return res


def verdict_of(res, threshold):
    if res["valid_px"] == 0:
        return "EMPTY"
    if res["valid_px"] < MIN_VALID:
        return "SMALL-MASK"
    return "PASS" if res["ratio"] >= threshold else "FAIL"


def fmt(x, nd=4):
    if x is None:
        return "n/a"
    if isinstance(x, float):
        if x != x:
            return "nan"
        if x == float("inf"):
            return "inf"
        return "%.*f" % (nd, x)
    return str(x)


def save_outputs(prefix, full, small, res):
    d = os.path.dirname(os.path.abspath(prefix))
    os.makedirs(d, exist_ok=True)
    mask = res["mask"]
    Image.fromarray(mask.astype(np.uint8) * 255, "L").save(prefix + "-mask.png")
    if res["bbox"] is None:
        return
    h, w = mask.shape
    x0, y0, x1, y1 = res["bbox"]
    x0, y0 = max(0, x0 - CROP_PAD), max(0, y0 - CROP_PAD)
    x1, y1 = min(w - 1, x1 + CROP_PAD), min(h - 1, y1 + CROP_PAD)
    cf = full[y0:y1 + 1, x0:x1 + 1]
    cs = small[y0:y1 + 1, x0:x1 + 1]
    cm = mask[y0:y1 + 1, x0:x1 + 1]
    dark = cf.copy()
    dark[~cm] = (dark[~cm].astype(np.float64) * 0.25).astype(np.uint8)
    im = Image.fromarray(np.concatenate([cf, cs, dark], axis=1), "RGB")
    if im.width > CROP_MAX_WIDTH:
        im = im.resize((CROP_MAX_WIDTH, max(1, round(im.height * CROP_MAX_WIDTH / im.width))), Image.LANCZOS)
    im.save(prefix + "-crop.png")


def cmd_compare(a):
    if a.erode < 0:
        raise UsageError("--erode must be >= 0")
    full, small, key = load_rgb(a.full), load_rgb(a.small), load_rgb(a.key)
    check_sizes(full, small, key)
    res = measure(full, small, key, a.erode)
    res["verdict"] = verdict_of(res, a.threshold)
    w, h = res["frame"]
    label = a.label or os.path.splitext(os.path.basename(a.full))[0]
    bbox = "none" if res["bbox"] is None else "%d,%d,%d,%d" % res["bbox"]
    share = 100.0 * res["valid_px"] / (w * h)
    dshare = res["differing_share"] * 100.0
    print("label=%s frame=%dx%d key=%d mask=%d valid=%d share=%s%% bbox=%s S_full=%s S_small=%s "
          "ratio=%s verdict=%s differing=%s%% ratio_differing=%s"
          % (label, w, h, res["key_px"], res["mask_px"], res["valid_px"], fmt(share, 2), bbox,
             fmt(res["s_full"]), fmt(res["s_small"]), fmt(res["ratio"]), res["verdict"],
             fmt(dshare, 2), fmt(res["ratio_differing"])))
    if a.csv:
        new = not os.path.isfile(a.csv)
        os.makedirs(os.path.dirname(os.path.abspath(a.csv)), exist_ok=True)
        with open(a.csv, "a", newline="") as f:
            wr = csv.writer(f)
            if new:
                wr.writerow(CSV_HEADER)
            wr.writerow([label, a.full, a.small, a.key, a.erode, res["key_px"], res["mask_px"],
                         res["valid_px"], fmt(res["s_full"], 6), fmt(res["s_small"], 6),
                         fmt(res["ratio"], 6), res["verdict"], fmt(res["differing_share"], 6),
                         fmt(res["ratio_differing"], 6)])
    if a.out_prefix:
        save_outputs(a.out_prefix, full, small, res)
    return 0


# ---------------------------------------------------------------- selftest

def _box_blur(a, k):
    """Box blur of an (h, w, 3) float array with a k x k box, edge replicated."""
    lo = (k - 1) // 2
    hi = k - 1 - lo
    p = np.pad(a, ((lo, hi), (lo, hi), (0, 0)), mode="edge")
    c = np.cumsum(np.cumsum(p, axis=0), axis=1)
    c = np.pad(c, ((1, 0), (1, 0), (0, 0)))
    h, w = a.shape[:2]
    s = c[k:k + h, k:k + w] - c[0:h, k:k + w] - c[k:k + h, 0:w] + c[0:h, 0:w]
    return s / (k * k)


def _noise(rng, h, w):
    n = rng.integers(0, 256, size=(h, w, 3)).astype(np.uint8)
    n[:, :, 1] = 100 + rng.integers(0, 156, size=(h, w))  # never key
    return n


def _fine_pattern(h, w):
    yy, xx = np.mgrid[0:h, 0:w]
    chk = (((yy // 2) + (xx // 2)) % 2).astype(bool)
    p = np.where(chk[:, :, None], np.array([220.0, 40.0, 40.0]), np.array([40.0, 40.0, 220.0]))
    p[xx % 4 == 0] = np.array([255.0, 255.0, 255.0])
    return p


def _mid_pattern(h, w):
    yy, xx = np.mgrid[0:h, 0:w]
    v = 128 + 60 * np.sin(xx / 5.0) + 40 * np.cos(yy / 7.0)
    return np.stack([v, v * 0.8, 255 - v], axis=2)


def _key_img(base, x0, y0, rw, rh):
    k = base.copy()
    row = np.round(255 * np.linspace(0.45, 1.0, rw)).astype(np.uint8)
    blk = np.zeros((rh, rw, 3), dtype=np.uint8)
    blk[:, :, 0] = row[None, :]
    blk[:, :, 2] = row[None, :]
    k[y0:y0 + rh, x0:x0 + rw] = blk
    return k


def selftest():
    cnt = [0, 0]

    def check(name, cond):
        cnt[0] += 1
        if not cond:
            cnt[1] += 1
            print("FAIL: %s" % name)

    # a. key rule
    def px(r, g, b):
        return np.array([[[r, g, b]]], dtype=np.uint8)
    for c in ((255, 0, 255), (115, 0, 115), (200, 10, 190)):
        check("key %s" % (c,), bool(key_mask(px(*c))[0, 0]))
    for c in ((128, 128, 128), (255, 40, 40), (255, 255, 255), (0, 0, 0), (40, 0, 40)):
        check("non-key %s" % (c,), not bool(key_mask(px(*c))[0, 0]))

    H, W = 480, 640
    x0, y0, rw, rh = 100, 80, 200, 150
    rng = np.random.default_rng(12345)
    bg = _noise(rng, H, W)
    key = _key_img(bg, x0, y0, rw, rh)

    # b. sharp vs blurred
    pat = _fine_pattern(rh, rw)
    full = bg.copy()
    full[y0:y0 + rh, x0:x0 + rw] = pat.astype(np.uint8)
    small = bg.copy()
    small[y0:y0 + rh, x0:x0 + rw] = np.round(_box_blur(pat, 8)).astype(np.uint8)
    r = measure(full, small, key, 3)
    check("b mask size", r["mask_px"] == (rw - 6) * (rh - 6))
    check("b ratio > 1.5 (%s)" % fmt(r["ratio"]), r["ratio"] > 1.5)
    check("b verdict PASS", verdict_of(r, 1.5) == "PASS")

    # c. background and silhouette excluded
    mid = _mid_pattern(rh, rw).astype(np.uint8)
    full = bg.copy()
    full[y0:y0 + rh, x0:x0 + rw] = mid
    small = np.full((H, W, 3), 128, dtype=np.uint8)
    small[y0:y0 + rh, x0:x0 + rw] = mid
    r = measure(full, small, key, 3)
    check("c ratio == 1 (%r)" % r["ratio"], abs(r["ratio"] - 1.0) < 1e-12)
    rwf = measure(full, small, key, 3, use_mask=False)
    check("c whole-frame ratio > 1.5 (%s)" % fmt(rwf["ratio"]), rwf["ratio"] > 1.5)

    # d. erosion matters
    full_d, small_d = full.copy(), small.copy()
    for arr, val in ((full_d, 255), (small_d, 0)):
        arr[y0, x0:x0 + rw] = val
        arr[y0 + rh - 1, x0:x0 + rw] = val
        arr[y0:y0 + rh, x0] = val
        arr[y0:y0 + rh, x0 + rw - 1] = val
    r0 = measure(full_d, small_d, key, 0)
    r3 = measure(full_d, small_d, key, 3)
    check("d erode 0 ratio differs (%s)" % fmt(r0["ratio"]), abs(r0["ratio"] - 1.0) > 1e-3)
    check("d erode 3 ratio == 1", abs(r3["ratio"] - 1.0) < 1e-12)

    # e. size mismatch
    try:
        measure(full, small[:-1], key, 3)
        check("e size mismatch raises", False)
    except UsageError:
        check("e size mismatch raises", True)

    # f. all-grey key
    grey = np.full((H, W, 3), 128, dtype=np.uint8)
    try:
        r = measure(full, small, grey, 3)
        check("f verdict EMPTY", verdict_of(r, 1.5) == "EMPTY")
    except Exception as e:
        check("f no exception (%s)" % e, False)

    # g. erosion function
    m = np.zeros((20, 20), dtype=bool)
    m[5:15, 5:15] = True
    check("g 10x10 erode 2 -> 36", int(erode(m, 2).sum()) == 36)
    m = np.zeros((20, 20), dtype=bool)
    m[0:5, 0:5] = True
    e1 = erode(m, 1)
    check("g corner 5x5 erode 1 -> 9", int(e1.sum()) == 9 and bool(e1[1:4, 1:4].all()))
    check("g erode 0 unchanged", bool((erode(m, 0) == m).all()))

    print("selftest: %d checks, %d failures" % (cnt[0], cnt[1]))
    return 0 if cnt[1] == 0 else 1


def build_parser():
    p = argparse.ArgumentParser(
        prog="hires_mask_sharp.py",
        description="Sharpness ratio FULL/SMALL of the hi-res driver texture on the eroded KEY mask only. "
        "Exit: 0 computation ran, 2 usage/file error.")
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compare", help="ratio S(FULL)/S(SMALL) on the eroded KEY mask")
    for x in ("full", "small", "key"):
        c.add_argument(x)
    c.add_argument("--erode", type=int, default=DEFAULT_ERODE, help="erosion radius (default 3)")
    c.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD, help="PASS ratio (default 1.5)")
    c.add_argument("--label", default=None, help="label (default: FULL file name)")
    c.add_argument("--csv", default=None, help="append a row to this csv")
    c.add_argument("--out-prefix", default=None, help="write <P>-mask.png and <P>-crop.png")
    sub.add_parser("selftest", help="synthetic self test")
    return p


def main():
    a = build_parser().parse_args()
    if a.cmd == "selftest":
        return selftest()
    try:
        return cmd_compare(a)
    except UsageError as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
