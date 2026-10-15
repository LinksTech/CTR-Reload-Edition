#!/usr/bin/env python3
"""Generate a synthetic hi-res test kart: body.hrm, wheel.hrm, skin.hrt.

Usage: python tools/hires_testdriver.py <out dir> [--preview <dir>]

Model space: +x = kart left, y = up, z = forward. Everything is procedural
(numpy + struct); Pillow is only used for the optional --preview output.
Output is deterministic (no random numbers).
"""
import argparse
import math
import os
import struct
import sys

import numpy as np

TEX = 1024
MARGIN = 4
REG = {  # name -> (x0, y0, x1, y1) in pixels, end exclusive
    "PAINT": (0, 0, 512, 512),
    "SUIT": (512, 0, 1024, 512),
    "HELMET": (0, 512, 512, 1024),
    "TREAD": (512, 512, 1024, 768),
    "RIM": (512, 768, 768, 1024),
    "GREY": (768, 768, 1024, 1024),
}
LIGHT = np.array([0.4, 0.8, 0.45])
LIGHT = LIGHT / np.linalg.norm(LIGHT)
BOX_FACES = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]


def reg_uv(name, ta, tb):
    """Map (ta, tb) in 0..1 to a UV inside a region, keeping a pixel margin."""
    x0, y0, x1, y1 = REG[name]
    px = x0 + MARGIN + ta * (x1 - x0 - 2 * MARGIN)
    py = y0 + MARGIN + tb * (y1 - y0 - 2 * MARGIN)
    return (px / TEX, py / TEX)


def planar(pts, region):
    """Planar projection of a (convex, planar) point set onto its region."""
    P = np.array(pts, float)
    n = np.zeros(3)
    for i in range(len(P)):  # Newell normal
        a, b = P[i], P[(i + 1) % len(P)]
        n += np.array([(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]),
                       (a[0] - b[0]) * (a[1] + b[1])])
    n /= max(np.linalg.norm(n), 1e-12)
    e1 = P[1] - P[0]
    e1 /= max(np.linalg.norm(e1), 1e-12)
    e2 = np.cross(n, e1)
    a, b = (P - P[0]) @ e1, (P - P[0]) @ e2
    a0, b0 = a.min(), b.min()
    sa, sb = max(a.max() - a0, 1e-9), max(b.max() - b0, 1e-9)
    return lambda p: reg_uv(region, ((p - P[0]) @ e1 - a0) / sa, 1.0 - ((p - P[0]) @ e2 - b0) / sb)


class Mesh:
    def __init__(self):
        self.v = []  # (px, py, pz, u, v, r, g, b, a)
        self.idx = []
        self.c = np.zeros(3)  # centroid of the current convex part (for winding)

    def part(self, pts):
        self.c = np.mean(np.array(pts, float), axis=0)

    def _vert(self, p, uv, n):
        s = int(math.floor(255 * (0.45 + 0.55 * max(0.0, float(n @ LIGHT))) + 0.5))
        self.v.append((p[0], p[1], p[2], uv[0], uv[1], s, s, s, 255))

    def emit(self, P, UV):
        """Add a flat triangle or quad; winding is made counter-clockwise from outside."""
        P = [np.asarray(q, float) for q in P]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        ln = np.linalg.norm(n)
        if ln < 1e-9:
            return
        n /= ln
        if float(n @ (np.mean(P, axis=0) - self.c)) < 0:
            P, UV, n = P[::-1], UV[::-1], -n
        b = len(self.v)
        for p, uv in zip(P, UV):
            self._vert(p, uv, n)
        self.idx += [b, b + 1, b + 2] + ([b, b + 2, b + 3] if len(P) == 4 else [])

    def hexa(self, v, region):
        """Hexahedron from 8 corners: v0..v3 ring at the bottom, v4..v7 the ring above."""
        v = [np.array(p, float) for p in v]
        self.part(v)
        for f in BOX_FACES:
            pts = [v[i] for i in f]
            fn = planar(pts, region)
            self.emit(pts, [fn(p) for p in pts])

    def box(self, x0, x1, y0, y1, z0, z1, region):
        self.hexa([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
                   (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], region)

    def cyl(self, axis, c, r, length, segs, region):
        k = "xyz".index(axis)
        i, j = (k + 1) % 3, (k + 2) % 3

        def ring(t, a):
            p = np.array(c, float)
            p[k] += t
            p[i] += r * math.cos(a)
            p[j] += r * math.sin(a)
            return p

        h = length / 2.0
        self.part([c])
        ang = [2 * math.pi * s / segs for s in range(segs + 1)]
        for s in range(segs):
            pts = [ring(-h, ang[s]), ring(-h, ang[s + 1]), ring(h, ang[s + 1]), ring(h, ang[s])]
            fn = planar(pts, region)
            self.emit(pts, [fn(p) for p in pts])
        for t in (-h, h):
            cap = [ring(t, a) for a in ang[:-1]]
            ctr = ring(t, 0.0)
            ctr[i], ctr[j] = c[i], c[j]
            fn = planar(cap, region)
            for s in range(segs):
                pts = [ctr, cap[s], cap[(s + 1) % segs]]
                self.emit(pts, [fn(p) for p in pts])

    def sphere(self, c, r, segs, rings, region):
        self.part([c])

        def pt(s, j):
            ph, th = math.pi * j / rings, 2 * math.pi * s / segs
            return (c[0] + r * math.sin(ph) * math.cos(th), c[1] + r * math.cos(ph),
                    c[2] + r * math.sin(ph) * math.sin(th))

        def uv(s, j):
            return reg_uv(region, s / segs, j / rings)

        for j in range(rings):
            for s in range(segs):
                if j == 0:
                    self.emit([pt(s, 0), pt(s + 1, 1), pt(s, 1)], [uv(s + 0.5, 0), uv(s + 1, 1), uv(s, 1)])
                elif j == rings - 1:
                    self.emit([pt(s, j), pt(s + 1, j), pt(s, rings)], [uv(s, j), uv(s + 1, j), uv(s + 0.5, rings)])
                else:
                    self.emit([pt(s, j), pt(s + 1, j), pt(s + 1, j + 1), pt(s, j + 1)],
                              [uv(s, j), uv(s + 1, j), uv(s + 1, j + 1), uv(s, j + 1)])


def build_body():
    m = Mesh()
    m.box(-22, 22, 5, 13, -34, 64, "PAINT")  # chassis floor
    m.hexa([(-22, 5, 64), (22, 5, 64), (12, 5, 78), (-12, 5, 78),
            (-22, 13, 64), (22, 13, 64), (12, 8, 78), (-12, 8, 78)], "PAINT")  # nose wedge
    m.box(14, 26, 8, 20, -10, 30, "PAINT")  # side pods
    m.box(-26, -14, 8, 20, -10, 30, "PAINT")
    m.cyl("x", (0, 9, 74), 2.5, 60, 16, "GREY")  # front bumper bar
    m.box(-14, 14, 13, 27, -34, -18, "GREY")  # engine block
    for sx in (8, -8):
        m.cyl("z", (sx, 22, -39), 3, 10, 16, "GREY")  # exhaust pipes
    for sx in (18, -18):
        m.box(sx - 1, sx + 1, 27, 41, -38, -36, "GREY")  # wing struts
    m.box(-30, 30, 41, 44, -46, -32, "GREY")  # rear wing plate
    m.box(-12, 12, 13, 38, -20, -16, "GREY")  # seat back
    m.box(-1.5, 1.5, 13, 28, 22, 25, "GREY")  # steering column
    m.cyl("z", (0, 30, 18), 7, 1.5, 24, "GREY")  # steering wheel
    m.box(-10, 10, 13, 36, -14, 4, "SUIT")  # driver torso
    m.box(9, 13, 28, 32, -6, 18, "SUIT")  # driver arms
    m.box(-13, -9, 28, 32, -6, 18, "SUIT")
    m.sphere((0, 47, -5), 10, 24, 16, "HELMET")  # driver head
    m.box(-7, 7, 44, 49, 4, 6, "HELMET")  # helmet visor
    return m


def build_wheel():
    m = Mesh()
    segs, r, hw = 48, 16.0, 7.0
    ang = [2 * math.pi * s / segs for s in range(segs + 1)]
    for s in range(segs):
        y0, z0 = r * math.cos(ang[s]), r * math.sin(ang[s])
        y1, z1 = r * math.cos(ang[s + 1]), r * math.sin(ang[s + 1])
        v0, v1 = s / segs, (s + 1) / segs
        m.emit([(-hw, y0, z0), (-hw, y1, z1), (hw, y1, z1), (hw, y0, z0)],
               [reg_uv("TREAD", 0, v0), reg_uv("TREAD", 0, v1), reg_uv("TREAD", 1, v1), reg_uv("TREAD", 1, v0)])
    for sgn in (1, -1):  # side discs: triangle fans from a centre vertex
        n = np.array([float(sgn), 0, 0])
        b = len(m.v)
        m._vert((sgn * hw, 0.0, 0.0), reg_uv("RIM", 0.5, 0.5), n)
        for s in range(segs):
            ca, sa = math.cos(ang[s]), math.sin(ang[s])
            m._vert((sgn * hw, r * ca, r * sa), reg_uv("RIM", 0.5 + 0.5 * sgn * ca, 0.5 + 0.5 * sa), n)
        for s in range(segs):
            a, bb = b + 1 + s, b + 1 + (s + 1) % segs
            m.idx += [b, a, bb] if sgn > 0 else [b, bb, a]
    return m


def fill_poly(shape, poly):
    """Even-odd polygon fill (vertices in x, y pixel coordinates)."""
    Y, X = np.mgrid[0:shape[0], 0:shape[1]]
    X, Y = X + 0.5, Y + 0.5
    inside = np.zeros(shape, bool)
    for k in range(len(poly)):
        (x1, y1), (x2, y2) = poly[k], poly[(k + 1) % len(poly)]
        if y1 == y2:
            continue
        inside ^= ((y1 > Y) != (y2 > Y)) & (X < (x2 - x1) * (Y - y1) / (y2 - y1) + x1)
    return inside


FONT = {
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
}


def build_texture():
    tex = np.zeros((TEX, TEX, 3), np.uint8)
    # --- PAINT ---
    R = tex[0:512, 0:512]
    Y, X = np.mgrid[0:512, 0:512]
    R[:] = (230, 110, 30)
    R[(X + Y) % 8 == 0] = (150, 65, 15)
    band = (Y >= 220) & (Y < 252)
    R[band & (((X // 16) + ((Y - 220) // 16)) % 2 == 0)] = (0, 0, 0)
    R[band & (((X // 16) + ((Y - 220) // 16)) % 2 == 1)] = (255, 255, 255)
    s = 12345  # pseudo text: 3x5 glyph blocks from a fixed LCG
    for line in range(15):
        y, x = 20 + 12 * line, 8
        while x + 3 <= 504:
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            if (s >> 20) & 7 == 0:
                x += 4
                continue
            bits = (s >> 8) & 0x7FFF
            for b in range(15):
                if (bits >> b) & 1:
                    R[y + b // 3, x + b % 3] = (60, 25, 10)
            x += 4
    mask = np.zeros((512, 512), bool)
    for gi, g in enumerate("07"):
        for r, row in enumerate(FONT[g]):
            for c, ch in enumerate(row):
                if ch == "1":
                    y0, x0 = 291 + r * 24, 124 + gi * 144 + c * 24
                    mask[y0:y0 + 24, x0:x0 + 24] = True
    dil = mask.copy()
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            dil |= np.roll(mask, (dy, dx), (0, 1))
    R[dil] = (0, 0, 0)
    R[mask] = (255, 255, 255)
    # --- SUIT ---
    R = tex[0:512, 512:1024]
    Y, X = np.mgrid[0:512, 0:512]
    R[:] = (30, 60, 185)
    R[((X // 2) + (Y // 2)) % 2 == 0] = (50, 80, 210)
    R[(X % 64) < 3] = (255, 255, 255)
    star = []
    for k in range(10):
        rad = 60 if k % 2 == 0 else 24
        a = math.radians(-90 + 36 * k)
        star.append((256 + rad * math.cos(a), 256 + rad * math.sin(a)))
    R[fill_poly((512, 512), star)] = (255, 255, 255)
    # --- HELMET ---
    R = tex[512:1024, 0:512]
    Y, X = np.mgrid[0:512, 0:512]
    R[:] = (240, 240, 240)
    R[((X % 4 == 0) | (Y % 4 == 0)) & (Y < 256)] = (200, 200, 205)
    for sx, w, col in ((96, 12, (200, 30, 30)), (112, 4, (0, 0, 0)), (120, 8, (200, 30, 30)),
                       (376, 8, (200, 30, 30)), (388, 4, (0, 0, 0)), (396, 12, (200, 30, 30)),
                       (240, 6, (0, 0, 0)), (250, 12, (200, 30, 30))):
        R[:, sx:sx + w] = col
    R[188:248] = (20, 25, 35)  # visor band: atlas rows 700..760
    R[198:200] = (60, 80, 110)
    # --- TREAD ---
    R = tex[512:768, 512:1024]
    Y, X = np.mgrid[0:256, 0:512]
    R[:] = (35, 35, 38)
    R[((Y + np.abs(X - 256)) % 16) < 3] = (15, 15, 17)
    dash = ((X % 4) < 2) & ((Y % 12) < 6) & (((X >= 12) & (X < 40)) | ((X >= 472) & (X < 500)))
    R[dash] = (150, 150, 155)
    # --- RIM ---
    R = tex[768:1024, 512:768]
    Y, X = np.mgrid[0:256, 0:256]
    dx, dy = X + 0.5 - 128, Y + 0.5 - 128
    rad = np.hypot(dx, dy)
    R[:] = (30, 30, 32)
    R[(rad >= 70) & (rad <= 120)] = (170, 170, 180)
    R[rad < 70] = (0, 0, 0)
    spoke = np.zeros((256, 256), bool)
    for k in range(5):
        a = math.radians(-90 + 72 * k)
        al = dx * math.cos(a) + dy * math.sin(a)
        pe = -dx * math.sin(a) + dy * math.cos(a)
        spoke |= (al >= 20) & (al <= 120) & (np.abs(pe) <= 9)
    R[spoke] = (140, 140, 150)
    R[rad < 20] = (140, 140, 150)
    R[np.hypot(dx, dy + 95) <= 10] = (210, 40, 40)
    # --- GREY ---
    R = tex[768:1024, 768:1024]
    Y, X = np.mgrid[0:256, 0:256]
    R[:] = (128, 128, 128)
    R[(X % 8 == 0) | (Y % 8 == 0)] = (255, 255, 255)
    return tex


def check(name, m):
    n, ni = len(m.v), len(m.idx)
    arr = np.array(m.v, np.float32)
    errs = []
    if n > 65535:
        errs.append("vertexCount > 65535")
    if ni % 3:
        errs.append("indexCount not a multiple of 3")
    if ni and (min(m.idx) < 0 or max(m.idx) >= n):
        errs.append("index out of range")
    if not np.isfinite(arr).all():
        errs.append("NaN/inf in vertices")
    if arr[:, 3:5].min() < 0 or arr[:, 3:5].max() > 1:
        errs.append("UV outside 0..1")
    if errs:
        sys.exit("self-check failed for %s: %s" % (name, "; ".join(errs)))
    return arr


def mesh_bytes(m):
    out = [struct.pack("<4sII", b"HRM1", len(m.v), len(m.idx))]
    for v in m.v:
        out.append(struct.pack("<5f4B", *v))
    out.append(struct.pack("<%dH" % len(m.idx), *m.idx))
    return b"".join(out)


def tex_bytes(tex):
    rgba = np.empty((TEX, TEX, 4), np.uint8)
    rgba[..., :3] = tex
    rgba[..., 3] = 255
    return struct.pack("<4sII", b"HRT1", TEX, TEX) + rgba.tobytes()


def preview(outdir, body, wheel, tex):
    from PIL import Image, ImageDraw
    os.makedirs(outdir, exist_ok=True)
    Image.fromarray(tex, "RGB").save(os.path.join(outdir, "skin.png"))
    tris = []  # (pos (3x3), colour)
    def collect(m, off):
        for t in range(0, len(m.idx), 3):
            vs = [m.v[i] for i in m.idx[t:t + 3]]
            tris.append((np.array([[v[0] + off[0], v[1] + off[1], v[2] + off[2]] for v in vs]), vs[0][5:8]))
    collect(body, (0, 0, 0))
    for wx in (36, -36):
        for wz in (49.75, -24):
            collect(wheel, (wx, 16, wz))
    S = 5
    views = {"side.png": (800, 300, lambda p: ((p[2] + 60) * S, (60 - p[1]) * S), 0, -1),
             "top.png": (500, 800, lambda p: ((50 - p[0]) * S, (100 - p[2]) * S), 1, 1)}
    for fname, (w, h, proj, dax, sgn) in views.items():
        img = Image.new("RGB", (w, h), (24, 26, 30))
        d = ImageDraw.Draw(img)
        # side view looks from -x (draw larger x first); top view looks down (smaller y first)
        for pos, col in sorted(tris, key=lambda t: sgn * t[0][:, dax].mean()):
            d.polygon([proj(p) for p in pos], fill=tuple(int(c) for c in col), outline=(10, 10, 12))
        img.save(os.path.join(outdir, fname))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--preview")
    a = ap.parse_args()
    body, wheel, tex = build_body(), build_wheel(), build_texture()
    arrs = {"body": check("body", body), "wheel": check("wheel", wheel)}
    os.makedirs(a.outdir, exist_ok=True)
    for name, m in (("body", body), ("wheel", wheel)):
        data = mesh_bytes(m)
        with open(os.path.join(a.outdir, name + ".hrm"), "wb") as f:
            f.write(data)
        pos = arrs[name][:, :3]
        print("%s.hrm: vertices=%d indices=%d bytes=%d" % (name, len(m.v), len(m.idx), len(data)))
        print("  hull min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f)" % (*pos.min(0), *pos.max(0)))
    data = tex_bytes(tex)
    with open(os.path.join(a.outdir, "skin.hrt"), "wb") as f:
        f.write(data)
    print("skin.hrt: %dx%d bytes=%d" % (TEX, TEX, len(data)))
    if a.preview:
        preview(a.preview, body, wheel, tex)
    return 0


if __name__ == "__main__":
    sys.exit(main())
