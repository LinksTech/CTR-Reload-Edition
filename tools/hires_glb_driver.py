#!/usr/bin/env python3
"""Build a synthetic hi-res test driver in Blender and export it as GLB files.

Usage:
  blender --background --factory-startup --python-exit-code 1 --python tools/hires_glb_driver.py -- \
      --out <dir> [--variants standard|t40k-x1024,lowpoly,bare,psx,...] [--no-classic] [--preview <dir>]
      [--blend <file.blend>] [--rldpack <rldpack.exe>]

Writes into <dir>:
  testdriver-t<N>k-x<E>.glb   hi-res driver: one skin (11 deform bones), 12 clips, 4 rigid wheel nodes
  testdriver-x<E>-color.png   RGBA colour atlas (alpha 0 holes in the CUTOUT region)
  testdriver-x<E>-normal.png  RGB tangent space normal map
  testdriver-classic.glb    lowest detail kart + driver, no wheels, no rig, shape keys (steer_left,
                              steer_right, jump, win, lose); a .rldchar is made from it with rldpack
  testdriver-lowpoly.glb     named variant "lowpoly": the same kart and driver at the lowest detail
                              (about 2500 triangles), flat shaded, one 64 x 64 pixel-art colour texture
                              (testdriver-x64-color.png), no normal map; all 12 clips and the 4 wheel nodes
  testdriver-bare.glb        named variant "bare": the body of t40k-x1024 (same skin, same textures) without
                              clips and without wheel nodes - a model as it comes out of a first export
  testdriver-psx.glb         named variant "psx": t10k-x2048 (same skin, clips, wheel nodes, textures) whose
                              faces carry PS1-style shading in COLOR_0: grey, byte 128 (on the display curve)
                              neutral, darker in the shadows, lighter on top, median 128 - the vertex colours
                              of a model made for the PS1 rule "texture x colour / 128" (psx_colors())
  testdriver-psx-classic.rldchar
                              (with "psx" and without --no-classic) the classic character of testdriver-psx.glb
                              as Reload Studio builds the classic fallback of a hi-res driver (make-char
                              --reduce auto --rig bind-pose --wheels off): the PS1 rule, the reference look
  testdriver-variants.txt    manifest

Model space is x = kart left, y = up, z = front, 64 units per metre. Blender location in metres is
(x / 64, -z / 64, y / 64); with the exporter's +Y up the GLB holds model / 64 (x left, y up, z front).
Everything is made here (geometry, textures, rig, clips). Deterministic: no random numbers.
"""
import hashlib
import json
import math
import os
import re
import struct
import subprocess
import sys
import traceback
import zlib

import numpy as np

import bpy
from mathutils import Matrix, Quaternion, Vector

UNIT = 64.0
FPS = 30
STANDARD = ["t10k-x2048", "t20k-x2048", "t40k-x2048", "t80k-x2048", "t160k-x2048",
            "t40k-x512", "t40k-x1024", "t320k-x2048"]
PREVIEW_VARIANT = "t40k-x1024"
# Named variants for the Studio's routes (Low-Poly, and a model without clips and wheels):
#   target    the triangle target of allocate() over body and wheels
#   edge      the edge of the colour texture; "pixel" = the 64 x 64 pixel-art atlas
#   clips     export the 12 clips
#   wheels    export the 4 wheel nodes
#   flat      flat shading (every face its own normal) and no normal map
#   lowwheel  the simple wheel of low_wheel_specs() (a tyre and a rim disc) instead of the detailed one
#   vcol      vertex colours in COLOR_0: None (none, as every variant before) or "psx" (psx_colors())
NAMED = {
    "lowpoly": dict(target=2500, edge=64, pixel=True, clips=True, wheels=True, flat=True, lowwheel=True, vcol=None),
    "bare": dict(target=40000, edge=1024, pixel=False, clips=False, wheels=False, flat=False, lowwheel=False,
                 vcol=None),
    "psx": dict(target=10000, edge=2048, pixel=False, clips=True, wheels=True, flat=False, lowwheel=False,
                vcol="psx"),
}
# The PS1-style shading of the variant "psx" (psx_colors): the light comes from above, a little from the
# front and the left (Blender axes: x left, -y front, z up). The shade of a vertex is mostly how much its
# normal faces that light (PSX_NORMAL_SHARE) and the rest how high it sits. Shades near the median stay
# exactly neutral (a dead zone: most corners of a PS1 model sit at 128), the rest is spread by PSX_SPREAD
# bytes per unit of shade and clamped to PSX_RANGE.
PSX_LIGHT = (0.25, -0.35, 1.0)
PSX_NORMAL_SHARE = 0.7
PSX_DEAD_ZONE = 0.22
PSX_SPREAD = 90.0
PSX_RANGE = (40, 216)
# The make-char switches of Reload Studio's classic fallback of a hi-res driver (RsCharHires_FallbackArgs,
# plus --wheels off for a model with its own four wheel nodes) and the template of its "Test in game".
CLASSIC_FALLBACK_ARGS = ["--reduce", "auto", "--rig", "bind-pose", "--wheels", "off"]
PSX_TEMPLATE = "14"
PIXEL_EDGE = 64
# the packer of this checkout's MSVC build; --rldpack <exe> names another one
RLDPACK_DEFAULT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                               "build-msvc-x86", "Release", "rldpack.exe")

# atlas regions in 1/16 cells (x0, y0, x1, y1), y from the top
CELLS = {
    "PAINT": (0, 0, 8, 8), "SUIT": (8, 0, 16, 8), "HELMET": (0, 8, 8, 16),
    "GREY": (8, 8, 16, 10), "SKIN": (8, 10, 12, 12), "DARK": (12, 10, 16, 12),
    "TREAD": (8, 12, 12, 14), "RIM": (12, 12, 16, 14), "CUTOUT": (8, 14, 16, 16),
}
MARGIN = 1.0 / 128.0  # of the edge (4 px at 512)
BOX_FACES = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
AX = (1.0, 0.0, 0.0)
AY = (0.0, 1.0, 0.0)
AZ = (0.0, 0.0, 1.0)
CLIP_NAMES = ["idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit",
              "podium_1", "podium_2", "podium_3", "lose", "menu"]
LOOPS = ["idle", "drift_left", "drift_right", "podium_1", "podium_2", "podium_3", "lose", "menu"]
WHEELS = [("wheel_fl", 36, 49.75), ("wheel_fr", -36, 49.75), ("wheel_rl", 36, -24), ("wheel_rr", -36, -24)]
JOINTS = ["root", "chassis", "steer", "spine", "head", "arm_upper.L", "arm_lower.L", "hand.L",
          "arm_upper.R", "arm_lower.R", "hand.R"]
KEYED = ["steer", "spine", "head", "arm_upper.L", "arm_lower.L", "hand.L",
         "arm_upper.R", "arm_lower.R", "hand.R"]
GRIP = {".L": (6.0, 29.0, 15.0), ".R": (-6.0, 29.0, 15.0)}  # rest wrist points = grip points on the rim


class Fail(Exception):
    pass


def say(msg):
    print(msg, flush=True)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


# ===================================================================== PNG
def png_bytes(arr):
    """Minimal PNG writer: 8 bit, filter 0 on every row, zlib level 9, one IDAT."""
    h, w, c = arr.shape
    raw = np.zeros((h, 1 + w * c), np.uint8)
    raw[:, 1:] = arr.reshape(h, w * c)
    comp = zlib.compress(raw.tobytes(), 9)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6 if c == 4 else 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", comp) + chunk(b"IEND", b"")


def write_file(path, data):
    with open(path, "wb") as fh:
        fh.write(data)


# ================================================================= textures
FONT = {
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
}


def fill_poly(shape, poly):
    Y, X = np.mgrid[0:shape[0], 0:shape[1]]
    X, Y = X + 0.5, Y + 0.5
    inside = np.zeros(shape, bool)
    for k in range(len(poly)):
        (x1, y1), (x2, y2) = poly[k], poly[(k + 1) % len(poly)]
        if y1 == y2:
            continue
        inside ^= ((y1 > Y) != (y2 > Y)) & (X < (x2 - x1) * (Y - y1) / (y2 - y1) + x1)
    return inside


def ref_regions():
    """Texture patterns at the 512 reference resolution (the prototype's, regions re-laid)."""
    ref = np.zeros((512, 512, 3), np.uint8)

    def view(name):
        x0, y0, x1, y1 = [c * 32 for c in CELLS[name]]
        return ref[y0:y1, x0:x1]

    # PAINT: orange, diagonal hatch, checker band, pseudo text, white "07"
    R = view("PAINT")
    Y, X = np.mgrid[0:256, 0:256]
    R[:] = (230, 110, 30)
    R[(X + Y) % 4 == 0] = (150, 65, 15)
    band = (Y >= 110) & (Y < 126)
    chk = ((X // 8) + ((Y - 110) // 8)) % 2
    R[band & (chk == 0)] = (0, 0, 0)
    R[band & (chk == 1)] = (255, 255, 255)
    s = 12345
    for line in range(15):
        y, x = 10 + 6 * line, 4
        while x + 3 <= 252:
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            if (s >> 20) & 7 == 0:
                x += 4
                continue
            bits = (s >> 8) & 0x7FFF
            for b in range(15):
                if (bits >> b) & 1:
                    R[y + b // 3, x + b % 3] = (60, 25, 10)
            x += 4
    mask = np.zeros((256, 256), bool)
    for gi, g in enumerate("07"):
        for r, row in enumerate(FONT[g]):
            for c, ch in enumerate(row):
                if ch == "1":
                    y0, x0 = 145 + r * 12, 62 + gi * 72 + c * 12
                    mask[y0:y0 + 12, x0:x0 + 12] = True
    dil = mask.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            dil |= np.roll(mask, (dy, dx), (0, 1))
    R[dil] = (0, 0, 0)
    R[mask] = (255, 255, 255)
    # SUIT: blue, fine 2x2 checker, white stripes every 32 px, white star
    R = view("SUIT")
    R[:] = (30, 60, 185)
    R[((X // 2) + (Y // 2)) % 2 == 0] = (50, 80, 210)
    R[(X % 32) < 2] = (255, 255, 255)
    star = []
    for k in range(10):
        rad = 30 if k % 2 == 0 else 12
        a = math.radians(-90 + 36 * k)
        star.append((128 + rad * math.cos(a), 128 + rad * math.sin(a)))
    R[fill_poly((256, 256), star)] = (255, 255, 255)
    # HELMET: white, red/black vertical stripes, dark visor band
    R = view("HELMET")
    R[:] = (240, 240, 240)
    R[((X % 2 == 0) | (Y % 2 == 0)) & (Y < 128)] = (200, 200, 205)
    for sx, w, col in ((48, 6, (200, 30, 30)), (56, 2, (0, 0, 0)), (60, 4, (200, 30, 30)),
                       (188, 4, (200, 30, 30)), (194, 2, (0, 0, 0)), (198, 6, (200, 30, 30)),
                       (120, 3, (0, 0, 0)), (125, 6, (200, 30, 30))):
        R[:, sx:sx + w] = col
    R[94:124] = (20, 25, 35)
    R[99:100] = (60, 80, 110)
    # GREY: mechanical grid (256 x 64)
    R = view("GREY")
    Y, X = np.mgrid[0:64, 0:256]
    R[:] = (128, 128, 128)
    R[(X % 4 == 0) | (Y % 4 == 0)] = (255, 255, 255)
    # SKIN and DARK (128 x 64)
    Y, X = np.mgrid[0:64, 0:128]
    R = view("SKIN")
    R[:] = (230, 180, 140)
    R[(X % 4 == 0) | (Y % 4 == 0)] = (215, 165, 125)
    R = view("DARK")
    R[:] = (40, 40, 44)
    R[(X % 8 == 0) | (Y % 8 == 0)] = (70, 70, 76)
    # TREAD: dark rubber, brick rows
    R = view("TREAD")
    R[:] = (34, 34, 38)
    R[(Y % 16) < 2] = (62, 62, 68)
    R[((X + 16 * ((Y // 16) % 2)) % 32) < 2] = (62, 62, 68)
    # RIM: silver, vertical bands, darker towards the bottom
    R = view("RIM")
    R[:] = (205, 208, 216)
    R[(X % 16) < 2] = (125, 130, 140)
    R[(Y % 32) < 2] = (150, 155, 165)
    R[:] = np.clip(R.astype(np.int16) - (Y // 2)[..., None], 0, 255).astype(np.uint8)
    # CUTOUT: dark plate with a lighter frame (holes are cut at full resolution)
    R = view("CUTOUT")
    Y, X = np.mgrid[0:64, 0:256]
    R[:] = (70, 72, 80)
    R[(X < 3) | (X >= 253) | (Y < 3) | (Y >= 61)] = (110, 114, 124)
    return ref


def region_px(name, E):
    s = E // 16
    return tuple(c * s for c in CELLS[name])


def build_color(E):
    """RGBA uint8 (E, E, 4): reference patterns repeated up, analytic fine detail at full resolution."""
    k = E // 512
    img = np.repeat(np.repeat(ref_regions(), k, 0), k, 1).astype(np.int16)
    Y, X = np.mgrid[0:E, 0:E]
    # fine 1-pixel grid every 8 pixels: only a larger edge resolves it as a finer structure in uv
    fine = ((X % 8) == 0) | ((Y % 8) == 0)
    cx0, cy0, cx1, cy1 = region_px("CUTOUT", E)
    fine[cy0:cy1, cx0:cx1] = False
    img[fine] = img[fine] * 228 // 256
    # PAINT roundel, analytic (smooth edge at any resolution)
    px0, py0, px1, py1 = region_px("PAINT", E)
    sx, sy = np.mgrid[0:py1 - py0, 0:px1 - px0]
    unit = 512.0 / E  # reference units per pixel
    xr, yr = (sy + 0.5) * unit, (sx + 0.5) * unit
    d = np.hypot(xr - 205.0, yr - 50.0)
    aa = unit
    ring = np.clip((28.0 - d) / aa + 0.5, 0.0, 1.0)
    inner = np.clip((21.0 - d) / aa + 0.5, 0.0, 1.0)
    core = np.clip((9.0 - d) / aa + 0.5, 0.0, 1.0)
    sub = img[py0:py1, px0:px1].astype(np.float32)
    for amount, col in ((ring, (245, 245, 245)), (inner, (20, 30, 95)), (core, (240, 190, 40))):
        sub = sub * (1.0 - amount[..., None]) + np.array(col, np.float32) * amount[..., None]
    img[py0:py1, px0:px1] = np.floor(sub + 0.5).astype(np.int16)
    rgba = np.empty((E, E, 4), np.uint8)
    rgba[..., :3] = np.clip(img, 0, 255).astype(np.uint8)
    rgba[..., 3] = 255
    # CUTOUT: 12 x 3 round holes, alpha 0, one pixel edge ring with alpha 128
    w = cx1 - cx0
    pitch = w / 12.0
    ly, lx = np.mgrid[0:cy1 - cy0, 0:cx1 - cx0]
    lx = lx + 0.5
    ly = ly + 0.5
    ccx = (np.floor(lx / pitch) + 0.5) * pitch
    ccy = (np.floor(ly / pitch) + 0.5) * pitch
    dd = np.hypot(lx - ccx, ly - ccy)
    rad = 0.32 * pitch
    alpha = np.full(dd.shape, 255, np.uint8)
    alpha[dd < rad + 1.0] = 128
    alpha[dd < rad] = 0
    rgba[cy0:cy1, cx0:cx1, 3] = alpha
    return rgba


def build_normal(E):
    """RGB uint8 tangent space normal map from a height field: panel lines and rivets in PAINT."""
    nrm = np.zeros((E, E, 3), np.uint8)
    nrm[...] = (128, 128, 255)
    x0, y0, x1, y1 = region_px("PAINT", E)
    unit = 512.0 / E
    Y, X = np.mgrid[0:y1 - y0, 0:x1 - x0]
    xr = (X + 0.5) * unit
    yr = (Y + 0.5) * unit
    gx = np.abs((xr % 64.0) - 32.0)  # distance to the lines at 32 + 64 k
    gy = np.abs((yr % 64.0) - 32.0)
    h = -np.exp(-(gx / 1.1) ** 2) - np.exp(-(gy / 1.1) ** 2)
    rx = (xr % 32.0) - 16.0
    ry = (yr % 32.0) - 16.0
    h = h + 1.2 * np.exp(-(np.hypot(rx, ry) / 2.2) ** 2)
    dhdrow, dhdx = np.gradient(h.astype(np.float32), unit)
    s = 0.9
    nx, ny, nz = -dhdx * s, dhdrow * s, np.ones_like(dhdx)  # +green = up in the image
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    out = np.stack([nx / ln, ny / ln, nz / ln], -1)
    nrm[y0:y1, x0:x1] = np.floor((out * 0.5 + 0.5) * 255.0 + 0.5).astype(np.uint8)
    return nrm


def build_pixel_color():
    """RGBA uint8 (64, 64, 4) for the variant "lowpoly": the same atlas regions, each one clear flat
    colour with a few pixel-art details, and 2 x 2 pixel holes (alpha 0) in CUTOUT."""
    E = PIXEL_EDGE
    rgba = np.zeros((E, E, 4), np.uint8)
    rgba[..., 3] = 255
    flat = {"PAINT": (210, 45, 40), "SUIT": (40, 90, 200), "HELMET": (245, 200, 40), "GREY": (150, 155, 160),
            "SKIN": (235, 185, 140), "DARK": (40, 40, 48), "TREAD": (25, 25, 28), "RIM": (200, 205, 215),
            "CUTOUT": (120, 125, 130)}
    for name, col in flat.items():
        x0, y0, x1, y1 = region_px(name, E)
        rgba[y0:y1, x0:x1, :3] = col
    # PAINT: a darker one-pixel panel edge and a white "7" at three times the font size
    x0, y0, x1, y1 = region_px("PAINT", E)
    rgba[y0:y1, x0, :3] = rgba[y0:y1, x1 - 1, :3] = (150, 30, 28)
    rgba[y0, x0:x1, :3] = rgba[y1 - 1, x0:x1, :3] = (150, 30, 28)
    gx, gy = x0 + (x1 - x0 - 15) // 2, y0 + (y1 - y0 - 21) // 2
    for r, row in enumerate(FONT["7"]):
        for c, bit in enumerate(row):
            if bit == "1":
                rgba[gy + 3 * r:gy + 3 * r + 3, gx + 3 * c:gx + 3 * c + 3, :3] = (245, 245, 245)
    # SUIT: a white band; HELMET: a dark visor band; TREAD: blocks every 4 pixels
    x0, y0, x1, y1 = region_px("SUIT", E)
    rgba[y0 + 14:y0 + 18, x0:x1, :3] = (245, 245, 245)
    x0, y0, x1, y1 = region_px("HELMET", E)
    rgba[y0 + 12:y0 + 18, x0:x1, :3] = (30, 40, 90)
    x0, y0, x1, y1 = region_px("TREAD", E)
    rgba[y0:y1, x0:x1:4, :3] = (70, 70, 76)
    # CUTOUT: 8 x 2 holes of 2 x 2 pixels
    x0, y0, x1, y1 = region_px("CUTOUT", E)
    for hy in (y0 + 1, y0 + 5):
        for hx in range(x0 + 1, x1, 4):
            rgba[hy:hy + 2, hx:hx + 2, 3] = 0
    return rgba


# =============================================================== geometry
def regmap(region):
    x0, y0, x1, y1 = [c / 16.0 for c in CELLS[region]]
    m = MARGIN

    def f(ta, tb):
        return np.stack([x0 + m + np.asarray(ta) * (x1 - x0 - 2 * m),
                         y0 + m + np.asarray(tb) * (y1 - y0 - 2 * m)], -1)
    return f


def planar_fn(pts, region):
    P = np.asarray(pts, float)
    n = np.zeros(3)
    for i in range(len(P)):  # Newell normal
        a, b = P[i], P[(i + 1) % len(P)]
        n += np.array([(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]),
                       (a[0] - b[0]) * (a[1] + b[1])])
    n /= max(np.linalg.norm(n), 1e-12)
    e1 = P[1] - P[0]
    e1 = e1 / max(np.linalg.norm(e1), 1e-12)
    e2 = np.cross(n, e1)
    a, b = (P - P[0]) @ e1, (P - P[0]) @ e2
    a0, b0 = a.min(), b.min()
    sa, sb = max(a.max() - a0, 1e-9), max(b.max() - b0, 1e-9)
    rm = regmap(region)

    def f(V):
        Q = np.asarray(V, float) - P[0]
        return rm(((Q @ e1) - a0) / sa, 1.0 - ((Q @ e2) - b0) / sb)
    return f


def sheet(Pg, UVg, closed):
    """Quad grid (S+1, J+1) to triangles. closed: the s direction wraps (Pg[S] == Pg[0])."""
    S1, J1, _ = Pg.shape
    ns = S1 - 1 if closed else S1
    V = Pg[:ns].reshape(-1, 3)
    idx = (np.arange(S1) % ns)[:, None] * J1 + np.arange(J1)[None, :]
    a, b, c, d = (idx[:-1, :-1].ravel(), idx[1:, :-1].ravel(), idx[1:, 1:].ravel(), idx[:-1, 1:].ravel())
    ua, ub, uc, ud = (UVg[:-1, :-1].reshape(-1, 2), UVg[1:, :-1].reshape(-1, 2),
                      UVg[1:, 1:].reshape(-1, 2), UVg[:-1, 1:].reshape(-1, 2))
    T = np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)])
    U = np.concatenate([np.stack([ua, ub, uc], 1), np.stack([ua, uc, ud], 1)])
    return V, T, U


def quad_grid(q, n, uvf):
    q = np.asarray(q, float)
    s = np.linspace(0.0, 1.0, n + 1)
    S, T = np.meshgrid(s, s, indexing="ij")
    S, T = S[..., None], T[..., None]
    Pg = (1 - S) * (1 - T) * q[0] + S * (1 - T) * q[1] + S * T * q[2] + (1 - S) * T * q[3]
    UVg = uvf(Pg.reshape(-1, 3)).reshape(n + 1, n + 1, 2)
    return sheet(Pg, UVg, False)


def ppos(c, k, t, rad, ang):
    i, j = (k + 1) % 3, (k + 2) % 3
    t, rad, ang = np.broadcast_arrays(np.asarray(t, float), np.asarray(rad, float), np.asarray(ang, float))
    out = np.empty(t.shape + (3,))
    out[..., k] = c[k] + t
    out[..., i] = c[i] + rad * np.cos(ang)
    out[..., j] = c[j] + rad * np.sin(ang)
    return out


def ref_point(c):
    c = np.asarray(c, float)
    return lambda cen: c


def ref_axis(c, k):
    c = np.asarray(c, float)

    def f(cen):
        p = np.tile(c, (len(cen), 1))
        p[:, k] = cen[:, k]
        return p
    return f


def ref_circle(c, k, rmid):
    c = np.asarray(c, float)

    def f(cen):
        p = cen - c
        p[:, k] = 0.0
        p = p / np.maximum(np.linalg.norm(p, axis=1, keepdims=True), 1e-12)
        r = c + p * rmid
        r[:, k] = c[k]
        return r
    return f


class Acc:
    """Collects the triangles of one part; fixes the winding with a reference function."""

    def __init__(self):
        self.V, self.T, self.U, self.S, self.n = [], [], [], [], 0

    def add(self, geo, smooth, ref=None, sgn=1.0):
        V, T, U = geo
        if len(T) == 0:
            return
        if ref is not None:
            a, b, c = V[T[:, 0]], V[T[:, 1]], V[T[:, 2]]
            nrm = np.cross(b - a, c - a)
            cen = (a + b + c) / 3.0
            flip = sgn * np.einsum("ij,ij->i", nrm, cen - ref(cen)) < 0
            T = T.copy()
            U = U.copy()
            T[flip] = T[flip][:, [0, 2, 1]]
            U[flip] = U[flip][:, [0, 2, 1]]
        self.V.append(V)
        self.T.append(T + self.n)
        self.U.append(U)
        self.S.append(np.full(len(T), bool(smooth)))
        self.n += len(V)

    def result(self):
        return (np.concatenate(self.V), np.concatenate(self.T), np.concatenate(self.U),
                np.concatenate(self.S))


def deff(d, k=1.0):
    return max(1, int(round(d * k)))


def segs_for(base, d):
    return base + int(round(base * 0.5 * (d - 1)))


def box_pts(x0, x1, y0, y1, z0, z1):
    return [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
            (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]


def add_hexa(acc, pts, region, n):
    pts = np.asarray(pts, float)
    cen = pts.mean(0)
    for f in BOX_FACES:
        q = pts[list(f)]
        acc.add(quad_grid(q, n, planar_fn(q, region)), False, ref_point(cen))


def add_box(acc, x0, x1, y0, y1, z0, z1, region, d, k=1.0):
    add_hexa(acc, box_pts(x0, x1, y0, y1, z0, z1), region, deff(d, k))


def add_seg_box(acc, a, b, w, region, d):
    a, b = np.array(a, float), np.array(b, float)
    dd = (b - a) / np.linalg.norm(b - a)
    ref = np.array([0.0, 1.0, 0.0]) if abs(dd[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    u = np.cross(dd, ref)
    u /= np.linalg.norm(u)
    v = np.cross(dd, u)
    h = w / 2.0

    def ring(p):
        return [p - u * h - v * h, p + u * h - v * h, p + u * h + v * h, p - u * h + v * h]
    add_hexa(acc, ring(a) + ring(b), region, deff(d))


def disc_geo(c, k, t, r_in, r_out, segs, m, rm):
    ang = 2 * math.pi * np.arange(segs + 1) / segs
    if r_in > 0:
        rads = np.linspace(r_in, r_out, m + 1)
        Pg = ppos(c, k, t, rads[None, :], ang[:, None])
        UVg = rm(0.5 + 0.5 * rads[None, :] / r_out * np.cos(ang)[:, None],
                 0.5 + 0.5 * rads[None, :] / r_out * np.sin(ang)[:, None])
        return sheet(Pg, UVg, True)
    rads = np.linspace(0.0, r_out, m + 1)[1:]
    rings = ppos(c, k, t, rads[:, None], ang[None, :segs])  # (m, segs, 3)
    center = np.array(c, float)
    center[k] += t
    V = np.concatenate([center[None], rings.reshape(-1, 3)])
    uvr = rm(0.5 + 0.5 * rads[:, None] / r_out * np.cos(ang[None, :segs]),
             0.5 + 0.5 * rads[:, None] / r_out * np.sin(ang[None, :segs]))
    UVv = np.concatenate([rm(np.array([0.5]), np.array([0.5])), uvr.reshape(-1, 2)])
    s = np.arange(segs)
    tris = [np.stack([np.zeros(segs, int), 1 + s, 1 + (s + 1) % segs], 1)]
    for j in range(m - 1):
        a, b = 1 + j * segs + s, 1 + j * segs + (s + 1) % segs
        c2, d2 = 1 + (j + 1) * segs + (s + 1) % segs, 1 + (j + 1) * segs + s
        tris.append(np.stack([a, b, c2], 1))
        tris.append(np.stack([a, c2, d2], 1))
    T = np.concatenate(tris)
    return V, T, UVv[T]


def add_cyl(acc, axis, c, r, length, segs, m, region, r_in=0.0, outer=True):
    """Closed cylinder (or a tube when r_in > 0) along an axis. Smooth walls, flat caps."""
    k = "xyz".index(axis)
    c = np.array(c, float)
    h = length / 2.0
    rm = regmap(region)
    ang = 2 * math.pi * np.arange(segs + 1) / segs
    t = np.linspace(-h, h, m + 1)
    walls = ([(r, 1.0)] if outer else []) + ([(r_in, -1.0)] if r_in > 0 else [])
    for rad, sgn in walls:
        Pg = ppos(c, k, t[None, :], rad, ang[:, None])
        UVg = rm((np.arange(segs + 1) / segs)[:, None] + 0 * t[None, :],
                 (np.arange(m + 1) / m)[None, :] + 0 * ang[:, None])
        acc.add(sheet(Pg, UVg, True), True, ref_axis(c, k), sgn)
    for tt in (-h, h):
        acc.add(disc_geo(c, k, tt, r_in, r, segs, max(1, m // 2 + 1) if r_in == 0 else max(1, m // 2), rm),
                False, ref_point(c))


def add_sphere(acc, c, r, segs, rings, region):
    c = np.array(c, float)
    rm = regmap(region)
    ph = math.pi * np.arange(1, rings) / rings
    th = 2 * math.pi * np.arange(segs + 1) / segs
    Pg = np.stack([c[0] + r * np.sin(ph)[None, :] * np.cos(th)[:, None],
                   c[1] + r * np.cos(ph)[None, :] + 0 * th[:, None],
                   c[2] + r * np.sin(ph)[None, :] * np.sin(th)[:, None]], -1)
    UVg = rm((np.arange(segs + 1) / segs)[:, None] + 0 * ph[None, :],
             (np.arange(1, rings) / rings)[None, :] + 0 * th[:, None])
    V, T, U = sheet(Pg, UVg, True)
    nm = len(V)
    top = np.array([c[0], c[1] + r, c[2]])
    bot = np.array([c[0], c[1] - r, c[2]])
    s = np.arange(segs)
    ring1 = s * (rings - 1)
    ringl = s * (rings - 1) + (rings - 2)
    ring1n = ((s + 1) % segs) * (rings - 1)
    ringln = ((s + 1) % segs) * (rings - 1) + (rings - 2)
    Tt = np.stack([np.full(segs, nm), ring1, ring1n], 1)
    Ut = np.stack([rm((s + 0.5) / segs, np.zeros(segs)), rm(s / segs, np.full(segs, 1.0 / rings)),
                   rm((s + 1.0) / segs, np.full(segs, 1.0 / rings))], 1)
    Tb = np.stack([np.full(segs, nm + 1), ringln, ringl], 1)
    Ub = np.stack([rm((s + 0.5) / segs, np.ones(segs)), rm((s + 1.0) / segs, np.full(segs, (rings - 1.0) / rings)),
                   rm(s / segs, np.full(segs, (rings - 1.0) / rings))], 1)
    V = np.concatenate([V, top[None], bot[None]])
    T = np.concatenate([T, Tt, Tb])
    U = np.concatenate([U, Ut, Ub])
    acc.add((V, T, U), True, ref_point(c))


def add_revolve(acc, c, x_a, r_a, x_b, r_b, segs, m, region, ref):
    """One strip of a surface of revolution about the x axis (profile from (x_a, r_a) to (x_b, r_b))."""
    rm = regmap(region)
    ang = 2 * math.pi * np.arange(segs + 1) / segs
    f = np.linspace(0.0, 1.0, m + 1)
    Pg = ppos(c, 0, (x_a + (x_b - x_a) * f)[None, :], (r_a + (r_b - r_a) * f)[None, :], ang[:, None])
    UVg = rm((np.arange(segs + 1) / segs)[:, None] + 0 * f[None, :], f[None, :] + 0 * ang[:, None])
    acc.add(sheet(Pg, UVg, True), True, ref)


# ------------------------------------------------------------ body parts
class Spec:
    def __init__(self, name, bone, mat, fn, mult=1):
        self.name, self.bone, self.mat, self.fn, self.mult = name, bone, mat, fn, mult
        self.counts = {}

    def build(self, d):
        acc = Acc()
        self.fn(acc, d)
        return acc.result()

    def count(self, d):
        if d not in self.counts:
            self.counts[d] = len(self.build(d)[1])
        return self.counts[d]


def body_specs():
    sp = []

    def S(name, bone, mat, fn):
        sp.append(Spec(name, bone, mat, fn))

    S("floor", "chassis", 0, lambda a, d: add_box(a, -22, 22, 5, 13, -34, 64, "PAINT", d))
    S("nose", "chassis", 0, lambda a, d: add_hexa(a, [(-22, 5, 64), (22, 5, 64), (12, 5, 78), (-12, 5, 78),
                                                      (-22, 13, 64), (22, 13, 64), (12, 8, 78), (-12, 8, 78)],
                                                  "PAINT", deff(d)))
    S("pod_l", "chassis", 0, lambda a, d: add_box(a, 14, 26, 8, 20, -10, 30, "PAINT", d))
    S("pod_r", "chassis", 0, lambda a, d: add_box(a, -26, -14, 8, 20, -10, 30, "PAINT", d))
    S("bumper", "chassis", 0, lambda a, d: add_cyl(a, "x", (0, 9, 74), 2.5, 56, segs_for(12, d), deff(d), "GREY"))
    S("engine", "chassis", 0, lambda a, d: add_box(a, -14, 14, 13, 27, -34, -18, "GREY", d))
    for nm, sx in (("exhaust_l", 8), ("exhaust_r", -8)):
        S(nm, "chassis", 0, lambda a, d, sx=sx: add_cyl(a, "z", (sx, 22, -39), 3, 10, segs_for(12, d), deff(d), "GREY"))
    for nm, sx in (("strut_l", 18), ("strut_r", -18)):
        S(nm, "chassis", 0, lambda a, d, sx=sx: add_box(a, sx - 1, sx + 1, 27, 41, -38, -36, "GREY", d))
    S("wing", "chassis", 0, lambda a, d: add_box(a, -25, 25, 41, 44, -46, -32, "GREY", d))
    S("seat", "chassis", 0, lambda a, d: add_box(a, -12, 12, 13, 38, -20, -16, "DARK", d))
    S("column", "chassis", 0, lambda a, d: add_box(a, -1.5, 1.5, 13, 28, 21, 24, "DARK", d))
    S("leg_l", "chassis", 0, lambda a, d: add_box(a, 3, 9, 13, 19, -12, 20, "SUIT", d))
    S("leg_r", "chassis", 0, lambda a, d: add_box(a, -9, -3, 13, 19, -12, 20, "SUIT", d))
    S("shoe_l", "chassis", 0, lambda a, d: add_box(a, 3, 9, 13, 21, 20, 25, "DARK", d))
    S("shoe_r", "chassis", 0, lambda a, d: add_box(a, -9, -3, 13, 21, 20, 25, "DARK", d))
    # number plate / grille with holes: the MASK part (rear of the engine block)
    S("plate", "chassis", 1, lambda a, d: add_box(a, -12, 12, 18, 24, -35.2, -34, "CUTOUT", d))
    # steering wheel: a ring (grip points lie on it), hub and three spokes
    S("wheel_ring", "steer", 0, lambda a, d: add_cyl(a, "z", (0, 30, 18), 7.0, 1.5, segs_for(24, d), deff(d, 0.5),
                                                      "DARK", r_in=5.4))
    S("wheel_hub", "steer", 0, lambda a, d: add_box(a, -2, 2, 28, 32, 18, 19.5, "PAINT", d))

    def spokes(a, d):
        for ang in (90.0, 210.0, 330.0):
            th = math.radians(ang)
            u = np.array([math.cos(th), math.sin(th), 0.0])
            t = np.array([-math.sin(th), math.cos(th), 0.0])
            c0 = np.array([0.0, 30.0, 18.0])
            def ring(r, c0=c0, u=u, t=t):
                return [c0 + u * r - t * 0.6 + np.array([0, 0, -0.4]), c0 + u * r + t * 0.6 + np.array([0, 0, -0.4]),
                        c0 + u * r + t * 0.6 + np.array([0, 0, 0.4]), c0 + u * r - t * 0.6 + np.array([0, 0, 0.4])]
            add_hexa(a, ring(1.5) + ring(5.6), "DARK", deff(d, 0.5))
    S("wheel_spokes", "steer", 0, spokes)
    S("torso", "spine", 0, lambda a, d: add_box(a, -10, 10, 15, 36, -14, 2, "SUIT", d))
    S("helmet", "head", 0, lambda a, d: add_sphere(a, (0, 47, -5), 9.5, 16 + 4 * (d - 1), 10 + 2 * (d - 1), "HELMET"))
    S("visor", "head", 0, lambda a, d: add_box(a, -7, 7, 44, 49, 3.5, 5.5, "HELMET", d))
    S("neck", "head", 0, lambda a, d: add_box(a, -3, 3, 36, 39, -8, -2, "SKIN", d))
    for side, sx in ((".L", 1.0), (".R", -1.0)):
        sh, el, wr = (10 * sx, 33, -4), (11 * sx, 25, 6), (6 * sx, 29, 15)
        S("upper" + side, "arm_upper" + side, 0, lambda a, d, sh=sh, el=el: add_seg_box(a, sh, el, 4.0, "SUIT", d))
        S("lower" + side, "arm_lower" + side, 0, lambda a, d, el=el, wr=wr: add_seg_box(a, el, wr, 3.5, "SUIT", d))
        S("hand" + side, "hand" + side, 0, lambda a, d, wr=wr: add_box(a, wr[0] - 1.5, wr[0] + 1.5, wr[1] - 1.5,
                                                                         wr[1] + 1.5, 14.5, 17.5, "SKIN", d))
    return sp


# ------------------------------------------------------------ wheel parts (local, outer side = +x)
WHEEL_C = (0.0, 0.0, 0.0)


def wheel_specs():
    sp = []

    def S(name, fn):
        sp.append(Spec(name, None, 0, fn, mult=4))

    prof = [(-6.0, 11.0), (-6.0, 13.6), (-4.6, 15.2), (4.6, 15.2), (6.0, 13.6), (6.0, 11.0)]

    def lvl(d):  # the wheels grow slower than the body, so they stay a fraction of the budget
        return 1.0 + (d - 1) * 0.4

    def tyre(a, d):
        d = lvl(d)
        segs = segs_for(28, d)
        m = deff(d, 0.5)
        ref = ref_circle(WHEEL_C, 0, 13.1)
        for i in range(len(prof) - 1):  # the inner wall (r = 11) is hidden behind the rim
            xa, ra = prof[i]
            xb, rb = prof[i + 1]
            add_revolve(a, WHEEL_C, xa, ra, xb, rb, segs, m, "TREAD", ref)

    def tread(a, d):
        d = lvl(d)
        nb = 20
        pitch = 2 * math.pi / nb
        for i in range(nb):
            a0 = i * pitch
            a1 = a0 + 0.6 * pitch
            pts = []
            for r in (14.8, 16.0):
                pts += [ppos(WHEEL_C, 0, -4.8, r, a0), ppos(WHEEL_C, 0, 4.8, r, a0),
                        ppos(WHEEL_C, 0, 4.8, r, a1), ppos(WHEEL_C, 0, -4.8, r, a1)]
            add_hexa(a, pts, "TREAD", deff(d, 0.3))

    def rim(a, d):
        d = lvl(d)
        add_cyl(a, "x", WHEEL_C, 11.3, 8.8, segs_for(28, d), deff(d, 0.4), "RIM", r_in=9.4, outer=False)

    def backdisc(a, d):
        d = lvl(d)
        add_cyl(a, "x", (-1.0, 0.0, 0.0), 9.5, 1.0, segs_for(20, d), deff(d, 0.4), "DARK")

    def hub(a, d):
        d = lvl(d)
        add_cyl(a, "x", (1.75, 0.0, 0.0), 3.4, 6.5, segs_for(16, d), deff(d, 0.5), "RIM")

    def spokes(a, d):
        d = lvl(d)
        for i in range(5):
            th = math.radians(18.0 + 72.0 * i)
            u = np.array([0.0, math.cos(th), math.sin(th)])
            t = np.array([0.0, -math.sin(th), math.cos(th)])

            def ring(r, u=u, t=t):
                return [u * r - t * 1.3 + np.array([2.0, 0, 0]), u * r - t * 1.3 + np.array([4.2, 0, 0]),
                        u * r + t * 1.3 + np.array([4.2, 0, 0]), u * r + t * 1.3 + np.array([2.0, 0, 0])]
            add_hexa(a, ring(3.0) + ring(9.7), "RIM", deff(d, 0.5))
    S("tyre", tyre)
    S("tread", tread)
    S("rim", rim)
    S("backdisc", backdisc)
    S("hub", hub)
    S("spokes", spokes)
    return sp


def low_wheel_specs():
    """The wheel of the variant "lowpoly": a closed tyre and a rim disc on its outer side, about
    80 triangles at the lowest level (the detailed wheel has about 900)."""
    sp = []

    def S(name, fn):
        sp.append(Spec(name, None, 0, fn, mult=4))
    S("tyre", lambda a, d: add_cyl(a, "x", WHEEL_C, 15.2, 12.0, segs_for(10, d), 1, "TREAD"))
    S("rim", lambda a, d: add_cyl(a, "x", (6.3, 0.0, 0.0), 9.0, 0.6, segs_for(10, d), 1, "RIM"))
    return sp


def allocate(specs, target):
    """Per-spec detail levels so that sum(mult * triangles) is as close to target as possible."""
    def total(ds):
        return sum(s.mult * s.count(dv) for s, dv in zip(specs, ds))
    d = 2
    t2 = total([2] * len(specs))
    d = max(1, int(round(2 * math.sqrt(target / t2))))
    while total([d + 1] * len(specs)) <= target:
        d += 1
    while d > 1 and total([d] * len(specs)) > target:
        d -= 1
    ds = [d] * len(specs)
    cur = total(ds)
    order = sorted(range(len(specs)), key=lambda i: (specs[i].mult * (specs[i].count(d + 1) - specs[i].count(d)), i))
    for i in order:
        delta = specs[i].mult * (specs[i].count(d + 1) - specs[i].count(d))
        if cur + delta <= target:
            ds[i] = d + 1
            cur += delta
    return ds, cur, d


# =================================================================== rig
def Bv(p):
    return Vector((p[0] / UNIT, -p[2] / UNIT, p[1] / UNIT))


def Bdir(p):
    return Vector((p[0], -p[2], p[1]))


BONES = [
    ("root", (0, 0, 0), (0, 0, 10), None),
    ("chassis", (0, 5, 0), (0, 20, 0), "root"),
    ("steer", (0, 30, 18), (0, 30, 22), "chassis"),
    ("spine", (0, 15, -6), (0, 36, -6), "chassis"),
    ("head", (0, 38, -5), (0, 56, -5), "spine"),
]
for _s, _sx in ((".L", 1), (".R", -1)):
    BONES += [
        ("arm_upper" + _s, (10 * _sx, 33, -4), (11 * _sx, 25, 6), "spine"),
        ("arm_lower" + _s, (11 * _sx, 25, 6), (6 * _sx, 29, 15), "arm_upper" + _s),
        ("hand" + _s, (6 * _sx, 29, 15), (6 * _sx, 29, 18), "arm_lower" + _s),
    ]


class Rig:
    def __init__(self):
        data = bpy.data.armatures.new("driver_rig")
        self.obj = bpy.data.objects.new("driver_rig", data)
        bpy.context.scene.collection.objects.link(self.obj)
        bpy.context.view_layer.objects.active = self.obj
        self.obj.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        ebs = {}
        for name, h, t, parent in BONES:
            eb = data.edit_bones.new(name)
            eb.head, eb.tail, eb.roll, eb.use_deform = Bv(h), Bv(t), 0.0, True
            if parent:
                eb.parent = ebs[parent]
            ebs[name] = eb
        bpy.ops.object.mode_set(mode="OBJECT")
        self.order = [b[0] for b in BONES]
        self.parent = {b[0]: b[3] for b in BONES}
        self.rest = {n: Matrix(data.bones[n].matrix_local) for n in self.order}
        self.rel = {n: (self.rest[self.parent[n]].inverted() @ self.rest[n]) if self.parent[n] else self.rest[n]
                    for n in self.order}
        for pb in self.obj.pose.bones:
            pb.rotation_mode = "QUATERNION"
        self.pole = {}
        for side in (".L", ".R"):
            sh = self.rest["arm_upper" + side].translation
            el = self.rest["arm_lower" + side].translation
            wr = self.rest["hand" + side].translation
            dh = (wr - sh).normalized()
            p = (el - sh) - dh * (el - sh).dot(dh)
            self.pole[side] = p.normalized()
        self.L1 = {s: (self.rest["arm_lower" + s].translation - self.rest["arm_upper" + s].translation).length
                   for s in (".L", ".R")}
        self.L2 = {s: (self.rest["hand" + s].translation - self.rest["arm_lower" + s].translation).length
                   for s in (".L", ".R")}

    def pose_mats(self, basis):
        out = {}
        for n in self.order:
            loc, q = basis.get(n, (None, None))
            bm = q.to_matrix().to_4x4() if q is not None else Matrix.Identity(4)
            if loc is not None:
                bm.translation = loc
            p = self.parent[n]
            out[n] = (out[p] @ self.rel[n] @ bm) if p else (self.rest[n] @ bm)
        return out

    def rot_basis(self, bone, rots):
        q = Quaternion((1.0, 0.0, 0.0, 0.0))
        for ax, deg in rots:
            q = Quaternion(Bdir(ax).normalized(), math.radians(deg)) @ q
        R = self.rest[bone].to_quaternion()
        return R.inverted() @ q @ R

    def frame_xf(self, mats, frame):
        if frame == "chassis":
            return Matrix.Identity(4)
        return mats[frame] @ self.rest[frame].inverted()

    def resolve(self, mats, side, sh, spec):
        kind = spec[0]
        L1, L2 = self.L1[side], self.L2[side]
        D = self.frame_xf(mats, spec[1])
        if kind == "dir":
            R = D.to_3x3()
            E = sh + L1 * (R @ Bdir(spec[2])).normalized()
            W = E + L2 * (R @ Bdir(spec[3])).normalized()
            return E, W
        target = D @ Bv(spec[2])
        pole = (D.to_3x3() @ (Bdir(spec[3]) if spec[3] is not None else self.pole[side])).normalized()
        d = target - sh
        dist = min(max(d.length, abs(L1 - L2) + 1e-4), (L1 + L2) * 0.9995)
        dh = d.normalized()
        a = (L1 * L1 - L2 * L2 + dist * dist) / (2.0 * dist)
        h = math.sqrt(max(L1 * L1 - a * a, 0.0))
        perp = pole - dh * pole.dot(dh)
        if perp.length < 1e-6:
            perp = Vector((0, 0, 1)).cross(dh)
        perp.normalize()
        E = sh + dh * a + perp * h
        W = sh + dh * dist
        return E, W

    def solve(self, spec):
        """spec: {'rot': {bone: [(axis, deg)]}, 'bounce': units, 'arms': {side: [(w, mode...)]}} -> basis."""
        basis = {}
        for b, rots in spec.get("rot", {}).items():
            basis[b] = (None, self.rot_basis(b, rots))
        if spec.get("bounce"):
            basis["spine"] = (Vector((0.0, spec["bounce"] / UNIT, 0.0)), basis.get("spine", (None, None))[1])
        mats = self.pose_mats(basis)
        info = {}
        for side in (".L", ".R"):
            up, lo, hd = "arm_upper" + side, "arm_lower" + side, "hand" + side
            M0 = mats[self.parent[up]] @ self.rel[up]
            sh = M0.translation
            E = Vector((0, 0, 0))
            W = Vector((0, 0, 0))
            for item in spec["arms"][side]:
                e, w = self.resolve(mats, side, sh, item[1:])
                E += item[0] * e
                W += item[0] * w
            L1, L2 = self.L1[side], self.L2[side]
            E = sh + L1 * (E - sh).normalized()
            W = E + L2 * (W - E).normalized()
            Q0 = M0.to_quaternion()
            y0 = Q0 @ Vector((0, 1, 0))
            Qd = y0.rotation_difference((E - sh).normalized()) @ Q0
            b_up = Q0.inverted() @ Qd
            Mu = Matrix.Translation(sh) @ Qd.to_matrix().to_4x4()
            M1 = Mu @ self.rest[up].inverted() @ self.rest[lo]
            Q1 = M1.to_quaternion()
            Qd1 = (Q1 @ Vector((0, 1, 0))).rotation_difference((W - E).normalized()) @ Q1
            b_lo = Q1.inverted() @ Qd1
            basis[up] = (None, b_up)
            basis[lo] = (None, b_lo)
            info[side] = (E, W)
        return basis, info


# ================================================================== clips
def sn(t, hz=1.0, ph=0.0):
    return math.sin(2 * math.pi * hz * t + ph)


def arms_grip():
    return {".L": [(1.0, "ik", "steer", GRIP[".L"], None)], ".R": [(1.0, "ik", "steer", GRIP[".R"], None)]}


def arms_rest_dir(side):
    sx = 1.0 if side == ".L" else -1.0
    return ("dir", "spine", (0.08 * sx, -1.0, 0.05), (0.05 * sx, -0.92, 0.38))


def arms_hang():
    return {s: [(1.0,) + arms_rest_dir(s)] for s in (".L", ".R")}


def clip_idle(t):
    return {"rot": {"spine": [(AX, 2.0 * math.sin(math.pi * t))], "head": [(AY, 3.0 * sn(t))]}, "arms": arms_grip()}


def clip_steer(sign):
    def f(t):
        w = t / 0.5  # 0..1, monotonic
        return {"rot": {"steer": [(AZ, -90.0 * sign * w)], "spine": [(AZ, -10.0 * sign * w)],
                        "head": [(AY, 20.0 * sign * w)]}, "arms": arms_grip()}
    return f


def clip_jump(t):
    if t <= 0.15:
        c = t / 0.15
        rots = {"spine": [(AX, 15.0 * c)], "head": [(AX, 20.0 * c)]}
        return {"rot": rots, "arms": arms_grip()}
    v = (t - 0.15) / 0.45
    rots = {"spine": [(AX, 15.0 - 25.0 * v)], "head": [(AX, 20.0 - 25.0 * v)]}
    arms = {}
    for side, sx in ((".L", 1.0), (".R", -1.0)):
        up = ("dir", "spine", (0.15 * sx, 0.17, 0.97), (0.15 * sx, 0.20, 0.97))
        arms[side] = [(1.0 - v, "ik", "steer", GRIP[side], None), (v,) + up]
    return {"rot": rots, "arms": arms}


def clip_drift(sign):
    # sign +1 = drift left: lean out = to kart right = +z rotation of the spine
    def f(t):
        wob = 5.0 * sn(t)
        return {"rot": {"steer": [(AZ, sign * (60.0 + wob))], "spine": [(AZ, 25.0 * sign + 2.0 * sn(t)), (AY, 10.0 * sign)],
                        "head": [(AY, 15.0 * sign)]}, "arms": arms_grip()}
    return f


def clip_hit(t):
    env = math.sin(math.pi * t)
    arms = {}
    for side, sx in ((".L", 1.0), (".R", -1.0)):
        fl = ("dir", "spine", (0.87 * sx, -0.5, 0.0), (0.87 * sx, -0.5, 0.0))
        arms[side] = [(1.0 - env, "ik", "steer", GRIP[side], None), (env,) + fl]
    return {"rot": {"spine": [(AY, 45.0 * sn(t, 2.0)), (AX, -8.0 * env)], "head": [(AY, 30.0 * sn(t, 4.0))]},
            "arms": arms}


def clip_podium1(t):
    arms = {}
    for side, sx in ((".L", 1.0), (".R", -1.0)):
        ang = math.radians(10.0)
        arms[side] = [(1.0, "dir", "spine", (sx * math.sin(ang), math.cos(ang), 0.0), (sx * math.sin(ang), math.cos(ang), 0.0))]
    return {"rot": {"spine": [(AZ, 10.0 * math.sin(math.pi * t))]}, "bounce": 0.6 * (1.0 - math.cos(2 * math.pi * t)),
            "arms": arms}


def clip_podium2(t):
    phi = 35.0 * sn(t)
    pr = math.radians(phi)
    arms = {".L": [(1.0,) + arms_rest_dir(".L")],
            ".R": [(1.0, "dir", "spine", (-0.64, 0.77, 0.0), (-0.3 + math.sin(pr), math.cos(pr), 0.0))]}
    return {"rot": {"spine": [(AZ, 3.0 * math.sin(math.pi * t))]}, "arms": arms}


def clip_podium3(t):
    c = 0.5 - 0.5 * math.cos(2 * math.pi * 2.0 * t)  # closed at 0.25 s and 0.75 s
    arms = {}
    for side, sx in ((".L", 1.0), (".R", -1.0)):
        opn = (9.0 * sx, 31.0, 9.0)
        shut = (0.75 * sx, 31.0, 10.0)
        pt = tuple(opn[i] + (shut[i] - opn[i]) * c for i in range(3))
        arms[side] = [(1.0, "ik", "spine", pt, (0.0, -1.0, 0.0))]
    return {"rot": {"spine": [(AX, 3.0 * c)]}, "arms": arms}


def clip_lose(t):
    sigh = math.sin(math.pi * t)
    return {"rot": {"spine": [(AX, 20.0 + 3.0 * sigh)], "head": [(AX, 35.0 + 3.0 * sigh)]}, "arms": arms_hang()}


def clip_menu(t):
    env = math.sin(math.pi * t / 1.5) ** 2 if t < 1.5 else 0.0
    phi = math.radians(35.0 * sn(t, 2.0)) * env
    raised = ("dir", "spine", (-0.64, 0.77, 0.0), (-0.3 + math.sin(phi), math.cos(phi), 0.0))
    arms = {".L": [(1.0,) + arms_rest_dir(".L")],
            ".R": [(1.0 - env,) + arms_rest_dir(".R"), (env,) + raised]}
    return {"rot": {"head": [(AY, 35.0 * sn(t, 1.0 / 3.0))]}, "arms": arms}


CLIPS = {
    # name: (duration s, function, extra key times)
    "idle": (2.0, clip_idle, []),
    "steer_left": (0.5, clip_steer(1.0), []),
    "steer_right": (0.5, clip_steer(-1.0), []),
    "jump": (0.6, clip_jump, [0.15]),
    "drift_left": (1.0, clip_drift(1.0), []),
    "drift_right": (1.0, clip_drift(-1.0), []),
    "hit": (1.0, clip_hit, []),
    "podium_1": (2.0, clip_podium1, []),
    "podium_2": (2.0, clip_podium2, []),
    "podium_3": (1.0, clip_podium3, [0.25, 0.75]),
    "lose": (2.0, clip_lose, []),
    "menu": (3.0, clip_menu, []),
}


def key_times(name):
    dur, _fn, extra = CLIPS[name]
    n = int(round(dur * FPS))
    ts = [i / FPS for i in range(n + 1)] + list(extra)
    return sorted(ts)


def compute_clips(rig):
    out = {}
    for name in CLIP_NAMES:
        dur, fn, _extra = CLIPS[name]
        times = key_times(name)
        quats = {b: [] for b in KEYED}
        locs = []
        for t in times:
            basis, _info = rig.solve(fn(t))
            for b in KEYED:
                q = basis.get(b, (None, None))[1] or Quaternion((1.0, 0.0, 0.0, 0.0))
                q = Quaternion(q)
                if quats[b] and q.dot(quats[b][-1]) < 0:
                    q.negate()
                quats[b].append(q)
            locs.append(basis.get("spine", (None, None))[0])
        out[name] = (times, quats, locs)
    return out


def make_actions(rig, clips):
    rig.obj.animation_data_create()
    for name in CLIP_NAMES:
        times, quats, locs = clips[name]
        act = bpy.data.actions.new(name)
        slot = act.slots.new("OBJECT", rig.obj.name)
        layer = act.layers.new("Layer")
        strip = layer.strips.new(type="KEYFRAME")
        cb = strip.channelbag(slot, ensure=True)
        frames = np.array([t * FPS for t in times], np.float32)
        for b in KEYED:
            for i in range(4):
                vals = np.array([q[i] for q in quats[b]], np.float32)
                fc = cb.fcurves.new('pose.bones["%s"].rotation_quaternion' % b, index=i, group_name=b)
                fc.keyframe_points.add(len(frames))
                co = np.empty(len(frames) * 2, np.float32)
                co[0::2], co[1::2] = frames, vals
                fc.keyframe_points.foreach_set("co", co)
                for kp in fc.keyframe_points:
                    kp.interpolation = "LINEAR"
                fc.update()
        if any(l is not None for l in locs):
            for i in range(3):
                vals = np.array([(l[i] if l is not None else 0.0) for l in locs], np.float32)
                fc = cb.fcurves.new('pose.bones["spine"].location', index=i, group_name="spine")
                fc.keyframe_points.add(len(frames))
                co = np.empty(len(frames) * 2, np.float32)
                co[0::2], co[1::2] = frames, vals
                fc.keyframe_points.foreach_set("co", co)
                for kp in fc.keyframe_points:
                    kp.interpolation = "LINEAR"
                fc.update()
        act.use_fake_user = True
        act.use_frame_range = True
        act.frame_start, act.frame_end = 0.0, float(round(CLIPS[name][0] * FPS))


# ============================================================ scene objects
def clear_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.cameras, bpy.data.lights):
        for x in list(coll):
            coll.remove(x)


def to_blender(V):
    return np.stack([V[:, 0] / UNIT, -V[:, 2] / UNIT, V[:, 1] / UNIT], 1)


def make_mesh(name, V, T, U, S, mat_index=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata(to_blender(V).astype(np.float32).tolist(), [], T.tolist())
    me.update()
    me.polygons.foreach_set("use_smooth", S.astype(bool))
    uvl = me.uv_layers.new(name="UVMap")
    uv = np.array(U, np.float32).reshape(-1, 2).copy()
    uv[:, 1] = 1.0 - uv[:, 1]
    uvl.uv.foreach_set("vector", uv.ravel())
    if mat_index is not None:
        me.polygons.foreach_set("material_index", mat_index.astype(np.int32))
    me.update()
    return me


def make_materials(color_path, normal_path, with_normal=True):
    for m in list(bpy.data.materials):
        bpy.data.materials.remove(m)
    for im in list(bpy.data.images):
        bpy.data.images.remove(im)
    ci = bpy.data.images.load(color_path, check_existing=False)
    ci.colorspace_settings.name = "sRGB"
    ci.alpha_mode = "STRAIGHT"
    ni = None
    if with_normal:
        ni = bpy.data.images.load(normal_path, check_existing=False)
        ni.colorspace_settings.name = "Non-Color"
    mats = {}
    for name in ("body", "cutout"):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        bsdf = nt.nodes.get("Principled BSDF")
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = ci
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        if name == "body":
            m.use_backface_culling = True
            if ni is not None:
                tn = nt.nodes.new("ShaderNodeTexImage")
                tn.image = ni
                nm = nt.nodes.new("ShaderNodeNormalMap")
                nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
                nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
        else:
            rnd = nt.nodes.new("ShaderNodeMath")
            rnd.operation = "ROUND"
            nt.links.new(tex.outputs["Alpha"], rnd.inputs[0])
            nt.links.new(rnd.outputs[0], bsdf.inputs["Alpha"])
            m.use_backface_culling = False
        mats[name] = m
    return mats


def build_body_object(rig, specs, ds, name, mats):
    parts = [(s, s.build(dv)) for s, dv in zip(specs, ds)]
    off = 0
    Vs, Ts, Us, Ss, Ms, Bs = [], [], [], [], [], []
    for s, (V, T, U, S) in parts:
        Vs.append(V)
        Ts.append(T + off)
        Us.append(U)
        Ss.append(S)
        Ms.append(np.full(len(T), s.mat))
        Bs.append(np.full(len(V), JOINTS.index(s.bone)))
        off += len(V)
    V, T, U, S, M, B = (np.concatenate(x) for x in (Vs, Ts, Us, Ss, Ms, Bs))
    me = make_mesh(name, V, T, U, S, M)
    me.materials.append(mats["body"])
    me.materials.append(mats["cutout"])
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    for j, jn in enumerate(JOINTS):
        vg = obj.vertex_groups.new(name=jn)
        idx = np.nonzero(B == j)[0]
        if len(idx):
            vg.add(idx.tolist(), 1.0, "REPLACE")
    obj.parent = rig.obj
    obj.matrix_parent_inverse = Matrix.Identity(4)
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig.obj
    return obj, len(T)


def build_wheel_objects(wspecs, ds, mats):
    parts = [s.build(dv) for s, dv in zip(wspecs, ds)]
    off = 0
    Vs, Ts, Us, Ss = [], [], [], []
    for V, T, U, S in parts:
        Vs.append(V)
        Ts.append(T + off)
        Us.append(U)
        Ss.append(S)
        off += len(V)
    V, T, U, S = (np.concatenate(x) for x in (Vs, Ts, Us, Ss))
    objs = []
    ntri = 0
    for name, x, z in WHEELS:
        if x > 0:
            Vw, Tw, Uw = V, T, U
        else:  # mirrored copy: x flipped, winding reversed
            Vw = V * np.array([-1.0, 1.0, 1.0])
            Tw = T[:, [0, 2, 1]]
            Uw = U[:, [0, 2, 1]]
        Sw = S if x > 0 else S
        # polygons keep the order, so corner arrays are permuted with the same indices
        me = make_mesh(name, Vw, Tw, Uw, Sw)
        me.materials.append(mats["body"])
        obj = bpy.data.objects.new(name, me)
        obj.location = Bv((x, 16.0, z))
        bpy.context.scene.collection.objects.link(obj)
        objs.append(obj)
        ntri += len(Tw)
    return objs, ntri


def remove_objects(objs):
    for o in objs:
        me = o.data
        bpy.data.objects.remove(o, do_unlink=True)
        if me is not None and me.users == 0:
            bpy.data.meshes.remove(me)


# ============================================================ vertex colours
def srgb_to_linear(c):
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(np.asarray(c, np.float64), 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1.0 / 2.4) - 0.055)


def psx_bytes(shade, median):
    """The grey byte (display curve, 128 = neutral) of each shade: a dead zone of PSX_DEAD_ZONE about the
    median stays exactly 128, beyond it PSX_SPREAD bytes per unit, clamped to PSX_RANGE."""
    d = shade - median
    d = np.sign(d) * np.maximum(np.abs(d) - PSX_DEAD_ZONE, 0.0)
    return np.clip(np.round(128.0 + PSX_SPREAD * d), PSX_RANGE[0], PSX_RANGE[1]).astype(np.int64)


def psx_colors(objs):
    """PS1-style shading as the vertex colours (COLOR_0) of every object (the first is the body): per vertex
    a shade from its normal against PSX_LIGHT and from its height over the whole model, the median taken
    over the body so the body's bytes have median 128. The exporter writes COLOR_0 as linear floats, so the
    byte goes in through the sRGB curve: 128 = linear 0.2158, which a reader turns back into 128.
    Returns the bytes per object."""
    light = np.array(PSX_LIGHT, np.float64)
    light /= np.linalg.norm(light)
    data = []
    for o in objs:
        me = o.data
        n = len(me.vertices)
        co = np.empty(n * 3, np.float32)
        me.vertices.foreach_get("co", co)
        nr = np.empty(n * 3, np.float32)
        me.vertex_normals.foreach_get("vector", nr)
        co = co.reshape(n, 3).astype(np.float64) + np.array(o.location, np.float64)
        data.append((co, nr.reshape(n, 3).astype(np.float64)))
    z = np.concatenate([co[:, 2] for co, _nr in data])
    z0, z1 = float(z.min()), float(z.max())
    shades = []
    for co, nr in data:
        h = (co[:, 2] - z0) / max(z1 - z0, 1e-9)
        shades.append(PSX_NORMAL_SHARE * (nr @ light) + (1.0 - PSX_NORMAL_SHARE) * (2.0 * h - 1.0))
    median = float(np.median(shades[0]))
    out = []
    for o, shade in zip(objs, shades):
        b = psx_bytes(shade, median)
        lin = srgb_to_linear(b / 255.0)
        rgba = np.stack([lin, lin, lin, np.ones_like(lin)], 1).astype(np.float32)
        attr = o.data.color_attributes.new(name="Col", type="FLOAT_COLOR", domain="POINT")
        attr.data.foreach_set("color", rgba.ravel())
        o.data.color_attributes.active_color = attr
        o.data.color_attributes.render_color_index = o.data.color_attributes.active_color_index
        o.data.update()
        out.append(b)
    return out


def glb_vcol_bytes(g):
    """Every COLOR_0 value of a GLB as the byte on the display curve (linear float -> sRGB byte), one row
    of RGB per vertex over all primitives; None when no primitive has COLOR_0."""
    rows = []
    for m in g.j.get("meshes", []):
        for p in m["primitives"]:
            if "COLOR_0" in p["attributes"]:
                c = g.accessor(p["attributes"]["COLOR_0"]).astype(np.float64)[:, :3]
                rows.append(np.round(linear_to_srgb(c) * 255.0).astype(np.int64))
    return np.concatenate(rows) if rows else None


# ================================================================== export
def export_gltf(path, objs, hires, animations=True, vcol=False):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    kw = dict(
        filepath=path, export_format="GLB", use_selection=True, export_yup=True,
        export_image_format="AUTO", export_texcoords=True, export_normals=True, export_tangents=False,
        export_materials="EXPORT", export_cameras=False, export_lights=False, export_extras=False,
        export_apply=False, export_vertex_color="NONE", export_attributes=False,
        export_draco_mesh_compression_enable=False, export_meshopt_compression_enable=False,
        export_unused_images=False, export_unused_textures=False, export_gpu_instances=False,
        export_loglevel=-1,
    )
    if vcol:  # the active colour attribute (psx_colors) as COLOR_0, nothing else
        kw.update(export_vertex_color="ACTIVE", export_all_vertex_colors=False,
                  export_active_vertex_color_when_no_material=True)
    if hires:
        kw.update(export_skins=True, export_def_bones=True, export_leaf_bone=False, export_influence_nb=4,
                  export_all_influences=False, export_morph=False, export_animations=animations,
                  export_animation_mode="ACTIONS", export_force_sampling=False,
                  export_optimize_animation_size=False, export_anim_single_armature=True,
                  export_reset_pose_bones=True, export_current_frame=False, export_frame_range=False,
                  export_bake_animation=False, export_extra_animations=False)
    else:
        kw.update(export_skins=False, export_morph=True, export_morph_normal=False, export_animations=False)
    r = bpy.ops.export_scene.gltf(**kw)
    if "FINISHED" not in r:
        raise Fail("glTF export failed: %s" % (r,))


# ============================================================ GLB reader
class Glb:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.data = fh.read()
        magic, ver, ln = struct.unpack_from("<III", self.data, 0)
        if magic != 0x46546C67 or ver != 2 or ln != len(self.data):
            raise Fail("%s: bad GLB header" % path)
        off = 12
        self.json, self.bin = None, b""
        while off < len(self.data):
            cl, ct = struct.unpack_from("<II", self.data, off)
            chunk = self.data[off + 8:off + 8 + cl]
            if ct == 0x4E4F534A:
                self.json = json.loads(chunk.decode("utf-8"))
            elif ct == 0x004E4942:
                self.bin = chunk
            off += 8 + cl
        self.j = self.json

    def accessor(self, i):
        a = self.j["accessors"][i]
        bv = self.j["bufferViews"][a["bufferView"]]
        comp = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}[a["componentType"]]
        nc = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
        dt = np.dtype("<" + comp[0])
        stride = bv.get("byteStride", nc * comp[1])
        base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        count = a["count"]
        if stride == nc * comp[1]:
            arr = np.frombuffer(self.bin, dt, count * nc, base).reshape(count, nc)
        else:
            rows = [np.frombuffer(self.bin, dt, nc, base + k * stride) for k in range(count)]
            arr = np.array(rows)
        if a.get("normalized"):
            arr = arr.astype(np.float64) / float(np.iinfo(dt).max)
        return arr

    def prim_tris(self, mesh_index):
        n = 0
        for p in self.j["meshes"][mesh_index]["primitives"]:
            n += (self.j["accessors"][p["indices"]]["count"] if "indices" in p
                  else self.j["accessors"][p["attributes"]["POSITION"]]["count"]) // 3
        return n

    def total_tris(self):
        t = 0
        for node in self.j["nodes"]:
            if "mesh" in node:
                t += self.prim_tris(node["mesh"])
        return t

    def node_local(self, i, anim=None):
        n = self.j["nodes"][i]
        if "matrix" in n:
            return np.array(n["matrix"], float).reshape(4, 4).T
        t = np.array(n.get("translation", [0, 0, 0]), float)
        q = np.array(n.get("rotation", [0, 0, 0, 1]), float)
        s = np.array(n.get("scale", [1, 1, 1]), float)
        if anim:
            t = anim.get((i, "translation"), t)
            q = anim.get((i, "rotation"), q)
        return trs_matrix(t, q, s)

    def parents(self):
        par = {}
        for i, n in enumerate(self.j["nodes"]):
            for c in n.get("children", []):
                par[c] = i
        return par

    def world(self, anim=None):
        par = self.parents()
        cache = {}

        def w(i):
            if i not in cache:
                m = self.node_local(i, anim)
                cache[i] = (w(par[i]) @ m) if i in par else m
            return cache[i]
        return [w(i) for i in range(len(self.j["nodes"]))]

    def anim_pose(self, ai, t):
        """Local TRS overrides {(node, path): value} of animation ai at time t."""
        out = {}
        a = self.j["animations"][ai]
        for ch in a["channels"]:
            s = a["samplers"][ch["sampler"]]
            times = self.accessor(s["input"]).ravel()
            vals = self.accessor(s["output"])
            path = ch["target"]["path"]
            k = int(np.searchsorted(times, t, side="right") - 1)
            if k < 0:
                v = vals[0]
            elif k >= len(times) - 1:
                v = vals[-1]
            else:
                f = (t - times[k]) / (times[k + 1] - times[k])
                v = vals[k] * (1 - f) + vals[k + 1] * f
                if path == "rotation":
                    v = v / np.linalg.norm(v)
            out[(ch["target"]["node"], path)] = np.array(v, float)
        return out


def trs_matrix(t, q, s):
    x, y, z, w = q
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m = np.eye(4)
    m[:3, :3] = R * np.array(s)[None, :]
    m[:3, 3] = t
    return m


def check_glb(path, expect_tris, edge, hires, report, clips=True, wheels=True, flat=False, vcol=None):
    g = Glb(path)
    j = g.j
    errs = []
    if vcol == "psx":  # PS1-style shading in COLOR_0: grey, median 128 on the display curve, both sides of it
        vb = glb_vcol_bytes(g)
        nprim = sum(len(m["primitives"]) for m in j.get("meshes", []))
        nvc = sum(1 for m in j.get("meshes", []) for p in m["primitives"] if "COLOR_0" in p["attributes"])
        if vb is None or nvc != nprim:
            errs.append("COLOR_0 on %d of %d primitives, expected all" % (nvc, nprim))
        else:
            med = int(np.median(vb))
            report["vcol"] = (len(vb), med, int(vb.min()), int(vb.max()), float(np.mean(np.abs(vb - 128) <= 8)))
            if np.abs(vb[:, 0] - vb[:, 1]).max() > 0 or np.abs(vb[:, 0] - vb[:, 2]).max() > 0:
                errs.append("COLOR_0 is not grey")
            if med != 128 or vb.min() >= 128 or vb.max() <= 128:
                errs.append("COLOR_0 bytes %d..%d median %d, expected median 128 with both sides"
                            % (vb.min(), vb.max(), med))
    tris = g.total_tris()
    if abs(tris - expect_tris) > 0.10 * expect_tris and hires:
        errs.append("triangles %d not within 10%% of %d" % (tris, expect_tris))
    if "extensionsRequired" in j:
        errs.append("extensionsRequired present: %s" % (j["extensionsRequired"],))
    imgs = j.get("images", [])
    colors = {}
    for im in imgs:
        bv = j["bufferViews"][im["bufferView"]]
        data = g.bin[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
        colors[im.get("name", "")] = data
    report["images"] = [(im.get("name", ""), im.get("mimeType", ""), sha256(colors[im.get("name", "")])) for im in imgs]
    # colour PNG: type 6, some alpha < 255, edge
    mats = j.get("materials", [])
    report["materials"] = [(m.get("name"), m.get("alphaMode", "OPAQUE"), m.get("alphaCutoff"), m.get("doubleSided", False)) for m in mats]
    if flat and len(imgs) != 1:
        errs.append("flat variant holds %d images, expected 1 (colour only, no normal map)" % len(imgs))
    for im in imgs:
        data = colors[im.get("name", "")]
        w, h, bd, ct = struct.unpack(">IIBB", data[16:26])
        if w != edge or h != edge or bd != 8:
            errs.append("image %s is %dx%d depth %d, edge %d expected" % (im.get("name"), w, h, bd, edge))
        src = os.path.join(os.path.dirname(path), im.get("name", "") + ".png")
        if not os.path.isfile(src) or open(src, "rb").read() != data:
            errs.append("embedded image %s is not byte-identical to the written file" % im.get("name"))
        want_type = 6 if im.get("name", "").endswith("-color") else 2
        if ct != want_type:
            errs.append("image %s has colour type %d, expected %d" % (im.get("name"), ct, want_type))
        if want_type == 6:  # decode (filter 0 rows) and look for alpha < 255
            pos, idat = 8, b""
            while pos < len(data):
                ln, tag = struct.unpack(">I4s", data[pos:pos + 8])
                if tag == b"IDAT":
                    idat += data[pos + 8:pos + 8 + ln]
                pos += 12 + ln
            raw = np.frombuffer(zlib.decompress(idat), np.uint8).reshape(h, 1 + w * 4)
            alpha = raw[:, 1:].reshape(h, w, 4)[..., 3]
            report["alpha"] = (int((alpha == 0).sum()), int((alpha == 128).sum()), int((alpha < 255).sum()))
            if not (alpha < 255).any() or not (alpha == 0).any():
                errs.append("colour PNG has no alpha holes")
    if hires:
        names = sorted(a.get("name") for a in j.get("animations", []))
        if clips and names != sorted(CLIP_NAMES):
            errs.append("animations %s differ from the 12 clip names" % (names,))
        if not clips and names:
            errs.append("variant without clips holds animations %s" % (names,))
        skins = j.get("skins", [])
        if len(skins) != 1 or not (8 <= len(skins[0]["joints"]) <= 64):
            errs.append("expected 1 skin with 8..64 joints, found %s" % ([len(s["joints"]) for s in skins],))
        for m in j["meshes"]:
            for p in m["primitives"]:
                at = p["attributes"]
                if "JOINTS_1" in at or "WEIGHTS_1" in at:
                    errs.append("JOINTS_1 present")
        body_nodes = [n for n in j["nodes"] if "skin" in n]
        if not body_nodes:
            errs.append("no skinned mesh node")
        for n in body_nodes:
            for p in j["meshes"][n["mesh"]]["primitives"]:
                if "JOINTS_0" not in p["attributes"] or "WEIGHTS_0" not in p["attributes"]:
                    errs.append("skinned primitive without JOINTS_0/WEIGHTS_0")
        world = g.world()
        par = g.parents()
        if not wheels:
            found = [n.get("name") for n in j["nodes"] if str(n.get("name", "")).startswith("wheel_")]
            if found:
                errs.append("variant without wheels holds wheel nodes %s" % (found,))
        for wn, x, z in (WHEELS if wheels else []):
            idx = [i for i, n in enumerate(j["nodes"]) if n.get("name") == wn]
            if len(idx) != 1:
                errs.append("wheel node %s count %d" % (wn, len(idx)))
                continue
            n = j["nodes"][idx[0]]
            if "mesh" not in n or "skin" in n:
                errs.append("wheel %s needs mesh and no skin" % wn)
            if idx[0] in par:
                errs.append("wheel %s has a parent" % wn)
            want = np.array([x / UNIT, 0.25, z / UNIT])
            got = world[idx[0]][:3, 3]
            if np.abs(got - want).max() > 0.002:
                errs.append("wheel %s at %s, expected %s" % (wn, got, want))
            report.setdefault("wheel_t", {})[wn] = tuple(float(v) for v in got)
        # skinned positions
        skin = skins[0]
        joints = skin["joints"]
        ibm = g.accessor(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
        jn = {j["nodes"][ji].get("name"): ji for ji in joints}
        # collect body vertices with dominant joint
        body = body_nodes[0]
        pos_l, jt_l = [], []
        for p in j["meshes"][body["mesh"]]["primitives"]:
            pos_l.append(g.accessor(p["attributes"]["POSITION"]).astype(float))
            jt = g.accessor(p["attributes"]["JOINTS_0"]).astype(int)
            wt = g.accessor(p["attributes"]["WEIGHTS_0"]).astype(float)
            jt_l.append(jt[np.arange(len(jt)), np.argmax(wt, 1)])
            if not np.allclose(wt.sum(1), 1.0, atol=1e-3):
                errs.append("weights not normalized")
        pos = np.concatenate(pos_l)
        jt = np.concatenate(jt_l)
        wheel_pos = [np.zeros((0, 3))]
        for wn, _x, _z in (WHEELS if wheels else []):
            i = [k for k, n in enumerate(j["nodes"]) if n.get("name") == wn][0]
            P = g.accessor(j["meshes"][j["nodes"][i]["mesh"]]["primitives"][0]["attributes"]["POSITION"]).astype(float)
            wheel_pos.append(P @ world[i][:3, :3].T + world[i][:3, 3])
        wheel_pos = np.concatenate(wheel_pos)
        # rest pose
        jm = np.stack([world[ji] @ ibm[k] for k, ji in enumerate(joints)])
        rest = np.einsum("nij,nj->ni", jm[jt][:, :3, :3], pos) + jm[jt][:, :3, 3]
        allrest = np.concatenate([rest, wheel_pos])
        report["rest_min"] = allrest.min(0)
        report["rest_max"] = allrest.max(0)
        if rest[:, 1].min() < -0.01:
            errs.append("rest pose body min y %.4f < -0.01" % rest[:, 1].min())
        # decimate per joint for the sampled frames
        sel = []
        for k in range(len(joints)):
            ii = np.nonzero(jt == k)[0]
            if len(ii):
                sel.append(ii[::max(1, len(ii) // 3000)])
        sel = np.concatenate(sel)
        spos, sjt = pos[sel], jt[sel]
        worst = np.zeros(3) if clips else (allrest.max(0) - allrest.min(0))
        anim_idx = {a["name"]: i for i, a in enumerate(j.get("animations", []))}
        for cname in CLIP_NAMES:
            ai = anim_idx.get(cname)
            if ai is None:
                continue
            dur = CLIPS[cname][0]
            for f in (0.0, 0.25, 0.5, 0.75, 1.0):
                w = g.world(g.anim_pose(ai, dur * f))
                jmat = np.stack([w[ji] @ ibm[k] for k, ji in enumerate(joints)])
                pp = np.einsum("nij,nj->ni", jmat[sjt][:, :3, :3], spos) + jmat[sjt][:, :3, 3]
                allp = np.concatenate([pp, wheel_pos])
                ext = allp.max(0) - allp.min(0)
                worst = np.maximum(worst, ext)
        report["max_extent"] = worst
        if worst.max() > 4.0:
            errs.append("extent %s > 4 m" % (worst,))
        # clip checks
        mx_t0 = 0.0
        for cname in (CLIP_NAMES if clips else []):
            a = j["animations"][anim_idx[cname]]
            for ch in a["channels"]:
                s = a["samplers"][ch["sampler"]]
                times = g.accessor(s["input"]).ravel()
                if abs(times[0]) > 1e-6:
                    errs.append("clip %s starts at %.4f" % (cname, times[0]))
                if abs(times[-1] - CLIPS[cname][0]) > 1e-3:
                    errs.append("clip %s ends at %.4f, expected %.3f" % (cname, times[-1], CLIPS[cname][0]))
                if cname in LOOPS:
                    v = g.accessor(s["output"])
                    if np.abs(v[0] - v[-1]).max() > 1e-4:
                        errs.append("loop %s first != last on %s" % (cname, ch["target"]["path"]))
                if s.get("interpolation", "LINEAR") != "LINEAR":
                    errs.append("clip %s interpolation %s" % (cname, s.get("interpolation")))
                tn = j["nodes"][ch["target"]["node"]].get("name")
                if tn in ("root", "chassis"):
                    errs.append("clip %s animates %s" % (cname, tn))
        # hand on the rim, clap
        hand = {s: jn["hand" + s] for s in (".L", ".R")}
        steer = jn["steer"]
        w_rest = g.world()
        rim = {}
        for cname in (("steer_left", "steer_right") if clips else ()):
            ai = anim_idx[cname]
            dur = CLIPS[cname][0]
            for tag, tt in (("first", 0.0), ("last", dur)):
                w = g.world(g.anim_pose(ai, tt))
                for s in (".L", ".R"):
                    G0 = np.array(GRIP[s]) / UNIT
                    gp = (w[steer] @ np.linalg.inv(w_rest[steer]) @ np.append(G0, 1.0))[:3]
                    hp = w[hand[s]][:3, 3]
                    rim[(cname, tag, s)] = float(np.linalg.norm(gp - hp) * UNIT)
        report["rim"] = rim
        if rim and max(rim.values()) > 1.5:
            errs.append("hand off the rim by %.3f units" % max(rim.values()))
        clap = []
        for tt in ((0.25, 0.75) if clips else ()):
            ai = anim_idx["podium_3"]
            w = g.world(g.anim_pose(ai, tt))
            clap.append(float(np.linalg.norm(w[hand[".L"]][:3, 3] - w[hand[".R"]][:3, 3]) * UNIT))
        report["clap"] = clap
        if clap and max(clap) > 2.0:
            errs.append("clap distance %.3f > 2 units" % max(clap))
        report["joints"] = len(joints)
        report["clips"] = len(j.get("animations", []))
    else:
        if j.get("skins") or j.get("animations"):
            errs.append("classic glTF holds a skin or animations")
        for m in j["meshes"]:
            for p in m["primitives"]:
                if "JOINTS_0" in p["attributes"] or "WEIGHTS_0" in p["attributes"]:
                    errs.append("classic glTF holds JOINTS/WEIGHTS")
        report["joints"] = 0
        report["clips"] = 0
        report["targets"] = [len(p.get("targets", [])) for m in j["meshes"] for p in m["primitives"]]
        report["target_names"] = [m.get("extras", {}).get("targetNames") for m in j["meshes"]]
    report["tris"] = tris
    if errs:
        raise Fail("%s: " % os.path.basename(path) + "; ".join(errs))
    return report


# ================================================================= variants
def parse_variant(v):
    # t40k-x1024
    a, b = v.split("-")
    return int(a[1:-1]), int(b[1:])


def build_variant(rig, wspecs, bspecs, vname, out, tex_paths, preview, export=True):
    n_k, edge = parse_variant(vname)
    target = n_k * 1000
    specs = bspecs + wspecs
    ds, cur, dbase = allocate(specs, target)
    say("variant %s: target %d, planned %d triangles (base level %d)" % (vname, target, cur, dbase))
    cpath, npath = tex_paths[edge]
    mats = make_materials(cpath, npath)
    bobj, nb = build_body_object(rig, bspecs, ds[:len(bspecs)], "driver_body", mats)
    wobjs, nw = build_wheel_objects(wspecs, ds[len(bspecs):], mats)
    say("  body %d + wheels %d = %d triangles" % (nb, nw, nb + nw))
    path = os.path.join(out, "testdriver-%s.glb" % vname)
    rep = {}
    if export:
        for pb in rig.obj.pose.bones:
            pb.rotation_quaternion = (1, 0, 0, 0)
            pb.location = (0, 0, 0)
        export_gltf(path, [bobj] + wobjs + [rig.obj], True)
        check_glb(path, target, edge, True, rep)
        rep["file"] = path
    if preview:
        render_preview(rig, preview)
    remove_objects([bobj] + wobjs)
    return rep


def build_named(rig, wspecs, bspecs, vname, out, tex_paths):
    """A named variant (NAMED): the same parts, rig and clips as the standard variants, with the
    detail, texture, shading, clips and wheel nodes the entry asks for."""
    spec = NAMED[vname]
    if spec["lowwheel"]:
        wspecs = low_wheel_specs()
    specs = bspecs + wspecs
    ds, cur, dbase = allocate(specs, spec["target"])
    say("variant %s: target %d, planned %d triangles (base level %d)" % (vname, spec["target"], cur, dbase))
    if spec["pixel"]:
        mats = make_materials(tex_paths["pixel"], None, with_normal=False)
    else:
        cpath, npath = tex_paths[spec["edge"]]
        mats = make_materials(cpath, npath)
    bobj, nb = build_body_object(rig, bspecs, ds[:len(bspecs)], "driver_body", mats)
    objs, nw = [bobj], 0
    if spec["wheels"]:
        wobjs, nw = build_wheel_objects(wspecs, ds[len(bspecs):], mats)
        objs += wobjs
    if spec["flat"]:
        for o in objs:
            o.data.polygons.foreach_set("use_smooth", [False] * len(o.data.polygons))
            o.data.update()
    if spec["vcol"] == "psx":
        b = np.concatenate(psx_colors(objs))
        say("  COLOR_0 psx: %d vertices, bytes %d..%d, median %d, %.0f %% at 128 +- 8"
            % (len(b), b.min(), b.max(), int(np.median(b)), 100.0 * np.mean(np.abs(b - 128) <= 8)))
    say("  body %d + wheels %d = %d triangles%s%s" % (nb, nw, nb + nw, "" if spec["clips"] else ", no clips",
                                                    "" if spec["wheels"] else ", no wheel nodes"))
    path = os.path.join(out, "testdriver-%s.glb" % vname)
    for pb in rig.obj.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
    export_gltf(path, objs + [rig.obj], True, animations=spec["clips"], vcol=spec["vcol"] is not None)
    rep = {}
    check_glb(path, nb + nw, spec["edge"], True, rep, clips=spec["clips"], wheels=spec["wheels"], flat=spec["flat"],
              vcol=spec["vcol"])
    rep["file"] = path
    remove_objects(objs)
    return rep


# ================================================================= classic
def build_classic(rig, bspecs, out, tex_paths):
    mats = make_materials(tex_paths[512][0], tex_paths[512][1], with_normal=False)
    cspecs = [s for s in bspecs]
    ds = [1] * len(cspecs)
    obj, ntri = build_body_object(rig, cspecs, ds, "driver_classic", mats)
    say("classic: %d triangles" % ntri)
    if ntri > 2000:
        raise Fail("classic model has %d triangles (> 2000)" % ntri)
    poses = {
        "steer_left": ("steer_left", 0.5), "steer_right": ("steer_right", 0.5), "jump": ("jump", 0.6),
        "win": ("podium_1", 0.5), "lose": ("lose", 0.0),
    }
    coords = {}
    for key in ("steer_left", "steer_right", "jump", "win", "lose"):
        clip, t = poses[key]
        basis, _info = rig.solve(CLIPS[clip][1](t))
        for pb in rig.obj.pose.bones:
            q, loc = basis.get(pb.name, (None, None))[1], basis.get(pb.name, (None, None))[0]
            pb.rotation_quaternion = q if q is not None else (1, 0, 0, 0)
            pb.location = loc if loc is not None else (0, 0, 0)
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        oe = obj.evaluated_get(dg)
        me = oe.to_mesh()
        co = np.empty(len(me.vertices) * 3, np.float32)
        me.vertices.foreach_get("co", co)
        oe.to_mesh_clear()
        coords[key] = co
    for pb in rig.obj.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
    obj.modifiers.remove(obj.modifiers["Armature"])
    for vg in list(obj.vertex_groups):
        obj.vertex_groups.remove(vg)
    obj.shape_key_add(name="Basis", from_mix=False)
    for key in ("steer_left", "steer_right", "jump", "win", "lose"):
        sk = obj.shape_key_add(name=key, from_mix=False)
        sk.data.foreach_set("co", coords[key])
    path = os.path.join(out, "testdriver-classic.glb")
    export_gltf(path, [obj], False)
    rep = {}
    check_glb(path, ntri, 512, False, rep)
    remove_objects([obj])
    rep["file"] = path
    return rep


def run_tool(args):
    say("> " + " ".join(args))
    r = subprocess.run(args, capture_output=True, text=True)
    text = (r.stdout or "") + (r.stderr or "")
    say(text.rstrip())
    return r.returncode, text


# ================================================================= preview
def render_preview(rig, pdir):
    os.makedirs(pdir, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x = sc.render.resolution_y = 320
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.display.render_aa = "8"
    if bpy.data.worlds.get("pv_world") is None:
        w = bpy.data.worlds.new("pv_world")
        w.color = (0.62, 0.66, 0.70)
    sc.world = bpy.data.worlds["pv_world"]
    cam_d = bpy.data.cameras.new("pv_cam")
    cam_d.lens = 42.0
    cam = bpy.data.objects.new("pv_cam", cam_d)
    sc.collection.objects.link(cam)
    sc.camera = cam

    def aim(loc, target):
        cam.location = Vector(loc)
        cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()

    def shot(name):
        tmp = os.path.join(pdir, "_tile.png")
        sc.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        im = bpy.data.images.load(tmp, check_existing=False)
        px = np.empty(im.size[0] * im.size[1] * 4, np.float32)
        im.pixels.foreach_get(px)
        w, h = im.size
        bpy.data.images.remove(im)
        arr = np.floor(px.reshape(h, w, 4)[::-1, :, :3] * 255.0 + 0.5).astype(np.uint8)
        os.remove(tmp)
        return arr

    sc.render.fps, sc.render.fps_base = FPS, 1.0
    rig.obj.animation_data_create()
    for cname in CLIP_NAMES:
        act = bpy.data.actions[cname]
        rig.obj.animation_data.action = act
        rig.obj.animation_data.action_slot = act.slots[0]
        aim((2.1, -2.35, 1.15), (0.0, 0.0, 0.5))
        n = int(round(CLIPS[cname][0] * FPS))
        tiles = []
        for k in range(4):
            sc.frame_set(int(round(n * k / 3.0)))
            tiles.append(shot(cname))
        write_file(os.path.join(pdir, cname + ".png"), png_bytes(np.concatenate(tiles, 1)))
    rig.obj.animation_data.action = None
    for pb in rig.obj.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
    sc.frame_set(0)
    aim((0.0, -2.9, 1.0), (0.0, 0.0, 0.45))
    write_file(os.path.join(pdir, "front.png"), png_bytes(shot("front")))
    aim((0.0, 2.9, 1.0), (0.0, 0.0, 0.45))
    write_file(os.path.join(pdir, "rear.png"), png_bytes(shot("rear")))
    bpy.data.objects.remove(cam, do_unlink=True)
    sc.camera = None


# ==================================================================== main
def parse_args():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    opt = {"out": None, "variants": "standard", "classic": True, "preview": None, "blend": None,
           "rldpack": RLDPACK_DEFAULT}
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--out":
            opt["out"] = args[i + 1]
            i += 2
        elif a == "--variants":
            opt["variants"] = args[i + 1]
            i += 2
        elif a == "--no-classic":
            opt["classic"] = False
            i += 1
        elif a == "--preview":
            opt["preview"] = args[i + 1]
            i += 2
        elif a == "--blend":
            opt["blend"] = args[i + 1]
            i += 2
        elif a == "--rldpack":
            opt["rldpack"] = args[i + 1]
            i += 2
        else:
            raise Fail("unknown argument %s" % a)
    if not opt["out"]:
        raise Fail("usage: blender -b --factory-startup --python hires_glb_driver.py -- --out <dir> "
                   "[--variants standard|t40k-x1024,lowpoly,bare,psx,...] [--no-classic] [--preview <dir>] [--blend f] "
                   "[--rldpack exe]")
    return opt


def main():
    opt = parse_args()
    out = os.path.abspath(opt["out"])
    os.makedirs(out, exist_ok=True)
    variants = STANDARD if opt["variants"] == "standard" else [v for v in opt["variants"].split(",") if v]
    for v in variants:
        if v not in NAMED:
            parse_variant(v)
    clear_scene()
    sc = bpy.context.scene
    sc.render.fps, sc.render.fps_base = FPS, 1.0
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 1.0
    rig = Rig()
    clips = compute_clips(rig)
    make_actions(rig, clips)
    # cross-check the forward kinematics against Blender's evaluated pose
    basis, info = rig.solve(CLIPS["steer_left"][1](0.5))
    for pb in rig.obj.pose.bones:
        loc, q = basis.get(pb.name, (None, None))
        pb.rotation_quaternion = q if q is not None else (1, 0, 0, 0)
    bpy.context.view_layer.update()
    ev = rig.obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mine = rig.pose_mats(basis)
    dev = max((Vector(ev.pose.bones[n].head) - mine[n].translation).length for n in rig.order) * UNIT
    say("FK cross-check against Blender: max head deviation %.5f units" % dev)
    if dev > 0.01:
        raise Fail("forward kinematics differ from Blender by %.4f units" % dev)
    for pb in rig.obj.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
    # textures
    edges = sorted(set([parse_variant(v)[1] for v in variants if v not in NAMED] +
                       [NAMED[v]["edge"] for v in variants if v in NAMED and not NAMED[v]["pixel"]] + [512]))
    tex_paths = {}
    manifest = []
    for e in edges:
        cp = os.path.join(out, "testdriver-x%d-color.png" % e)
        np_ = os.path.join(out, "testdriver-x%d-normal.png" % e)
        write_file(cp, png_bytes(build_color(e)))
        write_file(np_, png_bytes(build_normal(e)))
        tex_paths[e] = (cp, np_)
        say("texture edge %d: color %d bytes, normal %d bytes" % (e, os.path.getsize(cp), os.path.getsize(np_)))
    if any(v in NAMED and NAMED[v]["pixel"] for v in variants):
        tex_paths["pixel"] = os.path.join(out, "testdriver-x%d-color.png" % PIXEL_EDGE)
        write_file(tex_paths["pixel"], png_bytes(build_pixel_color()))
        say("pixel-art texture edge %d: color %d bytes" % (PIXEL_EDGE, os.path.getsize(tex_paths["pixel"])))
    bspecs = body_specs()
    wspecs = wheel_specs()
    reports = []
    for v in variants:
        if v in NAMED:
            reports.append((v, build_named(rig, wspecs, bspecs, v, out, tex_paths)))
            continue
        pv = opt["preview"] if (opt["preview"] and v == PREVIEW_VARIANT) else None
        reports.append((v, build_variant(rig, wspecs, bspecs, v, out, tex_paths, pv)))
    if opt["preview"] and PREVIEW_VARIANT not in variants:
        build_variant(rig, wspecs, bspecs, PREVIEW_VARIANT, out, tex_paths, opt["preview"], export=False)
    classic = None
    if opt["classic"]:
        if not os.path.isfile(opt["rldpack"]):
            raise Fail("rldpack not found: %s (build it, or give --rldpack <exe>, or --no-classic)" % opt["rldpack"])
        classic = build_classic(rig, bspecs, out, tex_paths)
        glb = classic["file"]
        rc, txt = run_tool([opt["rldpack"], "make-char", "--model", glb, "--name", "HIRES TEST", "--template", "0",
                            "--out", os.path.join(out, "testdriver-classic.rldchar")])
        if rc != 0:
            raise Fail("make-char failed (%d)" % rc)
        rl = os.path.join(out, "testdriver-classic.rldchar")
        for sub in ("verify", "info"):
            rc, txt2 = run_tool([opt["rldpack"], sub, rl])
            if rc != 0:
                raise Fail("%s failed (%d)" % (sub, rc))
        if "error" in txt.lower().replace("0 errors", ""):
            say("NOTE: make-char output mentions 'error'; check the text above")
    if opt["classic"] and "psx" in variants:
        # the classic character of the PS1-shaded model, as the Studio's Hi-Res route builds its fallback
        rl = os.path.join(out, "testdriver-psx-classic.rldchar")
        cmd = [opt["rldpack"], "make-char", "--model", os.path.join(out, "testdriver-psx.glb"),
               "--name", "HIRES PSX TEST", "--template", PSX_TEMPLATE] + CLASSIC_FALLBACK_ARGS + ["--out", rl]
        rc, txt = run_tool(cmd)
        m = re.search(r"Size (\d+)% is outside (\d+)\.\.(\d+)%", txt)
        if rc != 0 and m:
            # the Studio's size slider stays inside the range rldpack reports: the largest size it allows
            rc, txt = run_tool(cmd + ["--size", m.group(3)])
        if rc != 0:
            raise Fail("make-char of testdriver-psx.glb failed (%d)" % rc)
        for sub in ("verify", "info"):
            rc, _txt2 = run_tool([opt["rldpack"], sub, rl])
            if rc != 0:
                raise Fail("%s of testdriver-psx-classic.rldchar failed (%d)" % (sub, rc))
    # report details
    for v, rep in reports:
        say("%s: tris %d joints %d clips %d" % (v, rep["tris"], rep["joints"], rep["clips"]))
        say("  wheels %s" % (rep.get("wheel_t", "none"),))
        say("  rest min %s max %s  max extent %s" % (np.round(rep["rest_min"], 3), np.round(rep["rest_max"], 3),
                                                      np.round(rep["max_extent"], 3)))
        say("  materials %s" % (rep["materials"],))
        say("  images %s" % ([(a, b, c[:12]) for a, b, c in rep["images"]],))
        say("  embedded PNGs byte-identical to the written files (checked); colour PNG alpha==0: %d, alpha==128: %d, alpha<255: %d texels" % rep["alpha"])
        if "vcol" in rep:
            say("  COLOR_0 in the GLB: %d vertices, median byte %d, bytes %d..%d, %.3f at 128 +- 8 (checked: grey)"
                % (rep["vcol"][0], rep["vcol"][1], rep["vcol"][2], rep["vcol"][3], rep["vcol"][4]))
    with_rim = [rep for _v, rep in reports if rep.get("rim")]
    if with_rim:
        rep = with_rim[0]
        for k in sorted(rep["rim"]):
            say("  hand-on-rim %s %s %s: %.3f units" % (k + (rep["rim"][k],)))
        say("  clap distances at 0.25 s / 0.75 s: %.3f / %.3f units" % tuple(rep["clap"]))
    if classic:
        say("classic: tris %d shape keys %s names %s" % (classic["tris"], classic["targets"], classic["target_names"]))
    # manifest
    lines = []

    def add(fname, tris, edge, joints, clips_):
        p = os.path.join(out, fname)
        data = open(p, "rb").read()
        lines.append("%s\t%d\t%s\t%s\t%s\t%s\t%s" % (fname, len(data), sha256(data), tris, edge, joints, clips_))
    lines.append("file\tbytes\tsha256\ttriangles\ttexture_edge\tjoints\tclips")
    for e in edges:
        add("testdriver-x%d-color.png" % e, "-", e, "-", "-")
        add("testdriver-x%d-normal.png" % e, "-", e, "-", "-")
    if "pixel" in tex_paths:
        add("testdriver-x%d-color.png" % PIXEL_EDGE, "-", PIXEL_EDGE, "-", "-")
    for v, rep in reports:
        edge = NAMED[v]["edge"] if v in NAMED else parse_variant(v)[1]
        add("testdriver-%s.glb" % v, rep["tris"], edge, rep["joints"], rep["clips"])
    if classic:
        add("testdriver-classic.glb", classic["tris"], 512, 0, 0)
        add("testdriver-classic.rldchar", "-", "-", "-", "-")
    if opt["classic"] and "psx" in variants:
        add("testdriver-psx-classic.rldchar", "-", "-", "-", "-")
    write_file(os.path.join(out, "testdriver-variants.txt"), ("\n".join(lines) + "\n").encode("utf-8"))
    say("manifest written: %d lines" % len(lines))
    if opt["blend"]:
        bpy.ops.wm.save_as_mainfile(filepath=opt["blend"], compress=False)


if __name__ == "__main__":
    try:
        main()
    except Fail as e:
        print("ERROR: %s" % (e,))
        sys.exit(1)
    except Exception:
        traceback.print_exc()
        sys.exit(1)
