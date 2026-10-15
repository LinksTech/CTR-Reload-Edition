#!/usr/bin/env python3
"""Reader / validator / contact sheet for the animated hi-res driver (anim.hra + anim.hrt).

Usage:
  python tools/hires_anim_preview.py check <dir>
  python tools/hires_anim_preview.py sheet <dir> <out.png> [--yaw 35] [--pitch 20] [--size 320]
  python tools/hires_anim_preview.py selftest
Exit codes: 0 ok, 1 invalid, 2 usage.

HRA1 (anim.hra, little endian, no padding):
  off 0   4   magic "HRA1"
      4   4   u32 vertexCount   1..65535
      8   4   u32 indexCount    > 0, multiple of 3, <= 600000
      12  4   u32 frameCount    1..256
      16  4   u32 clipCount     1..16
      20  24*clipCount  clip table: char name[16] (ASCII, NUL padded, at least one NUL),
                        u32 firstFrame, u32 frameCount (>= 1, firstFrame + frameCount <= frameCount)
      ... 8*vertexCount             texcoords f32 u, f32 v in 0..1 (v = 0 is the TOP row of anim.hrt)
      ... 16*vertexCount*frameCount frames: per vertex f32 x, y, z, u8 r, g, b, a
      ... 2*indexCount              u16 indices (< vertexCount), triangles
  The file size must be exactly the sum. Same topology and texcoords in every frame.
HRT1 (anim.hrt): "HRT1", u32 width, u32 height, RGBA8 rows from the TOP; 512 x 512, alpha 255.
Clips (strict check): steer 0/9, air 9/4, drift_l 13/5, drift_r 18/5, 23 frames.
Vertex colour: r = g = b = round(255 * (0.45 + 0.55 * max(0, n.L))), a = 255.
Model space: x = kart left, y = up, z = front.
"""
import os
import struct
import sys

import numpy as np

STRICT_CLIPS = [("steer", 0, 9), ("air", 9, 4), ("drift_l", 13, 5), ("drift_r", 18, 5)]
WHEELS = [(sx, 16.0, wz) for sx in (36.0, -36.0) for wz in (49.75, -24.0)]
WHEEL_R = 16.0


class HraError(Exception):
    pass


def parse_hra(data, strict=True):
    """Parse and validate HRA1 bytes. Returns a dict; raises HraError."""
    if len(data) < 20:
        raise HraError("short file: %d bytes (header needs 20)" % len(data))
    if data[:4] != b"HRA1":
        raise HraError("bad magic %r" % (data[:4],))
    V, I, F, C = struct.unpack_from("<IIII", data, 4)
    if not 1 <= V <= 65535:
        raise HraError("vertexCount %d out of 1..65535" % V)
    if I <= 0 or I % 3 or I > 600000:
        raise HraError("indexCount %d must be >0, a multiple of 3 and <= 600000" % I)
    if not 1 <= F <= 256:
        raise HraError("frameCount %d out of 1..256" % F)
    if not 1 <= C <= 16:
        raise HraError("clipCount %d out of 1..16" % C)
    want = 20 + 24 * C + 8 * V + 16 * V * F + 2 * I
    if len(data) != want:
        raise HraError("file size %d != expected %d" % (len(data), want))
    clips = []
    off = 20
    for c in range(C):
        raw, first, cnt = struct.unpack_from("<16sII", data, off)
        off += 24
        z = raw.find(b"\0")
        if z < 0:
            raise HraError("clip %d name has no NUL" % c)
        if z == 0 or any(b < 0x20 or b > 0x7E for b in raw[:z]):
            raise HraError("clip %d name empty or not printable ASCII" % c)
        if any(raw[z:]):
            raise HraError("clip %d name not NUL padded" % c)
        if cnt < 1 or first + cnt > F:
            raise HraError("clip %d (%s) frames %d+%d outside 0..%d" % (c, raw[:z].decode(), first, cnt, F))
        clips.append((raw[:z].decode("ascii"), first, cnt))
    uv = np.frombuffer(data, "<f4", V * 2, off).reshape(V, 2)
    off += 8 * V
    if not np.isfinite(uv).all() or uv.min() < 0 or uv.max() > 1:
        raise HraError("texcoords not finite or outside 0..1")
    rec = np.frombuffer(data, np.dtype([("p", "<f4", 3), ("c", "u1", 4)]), V * F, off).reshape(F, V)
    off += 16 * V * F
    pos, col = rec["p"], rec["c"]
    if not np.isfinite(pos).all():
        raise HraError("non-finite vertex position")
    if (col[..., 3] != 255).any():
        raise HraError("vertex alpha is not 255 everywhere")
    if (col[..., 0] != col[..., 1]).any() or (col[..., 0] != col[..., 2]).any():
        raise HraError("vertex colour is not grey (r = g = b expected)")
    idx = np.frombuffer(data, "<u2", I, off)
    if int(idx.max()) >= V:
        raise HraError("index %d >= vertexCount %d" % (int(idx.max()), V))
    if strict:
        if [c[0] for c in clips] != [c[0] for c in STRICT_CLIPS]:
            raise HraError("clip names/order %r != %r" % ([c[0] for c in clips], [c[0] for c in STRICT_CLIPS]))
        if clips != STRICT_CLIPS:
            raise HraError("clip table %r != %r" % (clips, STRICT_CLIPS))
        if F != 23:
            raise HraError("frameCount %d != 23" % F)
    return {"V": V, "I": I, "F": F, "clips": clips, "uv": uv, "pos": pos, "col": col, "idx": idx}


def parse_hrt(data):
    if len(data) < 12:
        raise HraError("hrt short file")
    if data[:4] != b"HRT1":
        raise HraError("hrt bad magic %r" % (data[:4],))
    w, h = struct.unpack_from("<II", data, 4)
    if (w, h) != (512, 512):
        raise HraError("hrt size %dx%d != 512x512" % (w, h))
    if len(data) != 12 + w * h * 4:
        raise HraError("hrt file size %d != %d" % (len(data), 12 + w * h * 4))
    img = np.frombuffer(data, np.uint8, w * h * 4, 12).reshape(h, w, 4)
    if (img[..., 3] != 255).any():
        raise HraError("hrt alpha is not 255 everywhere")
    return img


def load(d, need_tex=True):
    with open(os.path.join(d, "anim.hra"), "rb") as f:
        a = parse_hra(f.read())
    t = None
    if need_tex:
        with open(os.path.join(d, "anim.hrt"), "rb") as f:
            t = parse_hrt(f.read())
    return a, t


# ------------------------------------------------------------------ check
def cmd_check(d):
    try:
        a, t = load(d)
    except (HraError, OSError) as e:
        print("INVALID: %s" % (e,))
        return 1
    V, I, F = a["V"], a["I"], a["F"]
    print("vertices=%d indices=%d triangles=%d frames=%d clips=%d" % (V, I, I // 3, F, len(a["clips"])))
    print("texture 512x512 alpha 255 ok")
    pos = a["pos"].astype(np.float64)
    tri = pos[:, a["idx"].reshape(-1, 3)]
    area = np.linalg.norm(np.cross(tri[0, :, 1] - tri[0, :, 0], tri[0, :, 2] - tri[0, :, 0]), axis=1)
    print("degenerate triangles in frame 0: %d" % int((area < 1e-9).sum()))
    ref = int(a["clips"][0][1]) + a["clips"][0][2] // 2  # steer centre frame
    for name, first, cnt in a["clips"]:
        p = pos[first:first + cnt].reshape(-1, 3)
        print("clip %-8s first=%2d count=%d hull min=(%7.2f %7.2f %7.2f) max=(%7.2f %7.2f %7.2f)" %
              (name, first, cnt, *p.min(0), *p.max(0)))
    print("per-frame max displacement vs steer centre frame %d:" % ref)
    for name, first, cnt in a["clips"]:
        row = []
        for i in range(cnt):
            dd = np.linalg.norm(pos[first + i] - pos[ref], axis=1).max()
            row.append("%.2f" % dd)
        print("  %-8s %s" % (name, " ".join(row)))
    print("OK")
    return 0


# ------------------------------------------------------------------ sheet
def view_basis(yaw, pitch):
    y, p = np.radians(yaw), np.radians(pitch)
    campos = np.array([np.cos(p) * np.sin(y), np.sin(p), np.cos(p) * np.cos(y)])
    f = -campos
    r = np.cross(f, [0.0, 1.0, 0.0])
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    return r, u, f


def cmd_sheet(d, out, yaw, pitch, size):
    from PIL import Image, ImageDraw
    try:
        a, t = load(d)
    except (HraError, OSError) as e:
        print("INVALID: %s" % (e,))
        return 1
    r, u, f = view_basis(yaw, pitch)
    pos = a["pos"].astype(np.float64)
    allp = pos.reshape(-1, 3)
    wc = []  # wheel circle points (yz plane at x = +-36)
    for (wx, wy, wz) in WHEELS:
        for k in range(24):
            ang = 2 * np.pi * k / 24
            wc.append((wx, wy + WHEEL_R * np.cos(ang), wz + WHEEL_R * np.sin(ang)))
    ext = np.concatenate([allp, np.array(wc)])
    sx, sy = ext @ r, -(ext @ u)
    x0, x1, y0, y1 = sx.min(), sx.max(), sy.min(), sy.max()
    SS = 2
    lab = 16
    inner = size - 8
    scale = inner * SS / max(x1 - x0, y1 - y0)
    ox = (size * SS - (x1 - x0) * scale) / 2 - x0 * scale
    oy = (size * SS - lab * SS - (y1 - y0) * scale) / 2 - y0 * scale + lab * SS

    def proj(p):
        p = np.asarray(p, float)
        return np.stack([(p @ r) * scale + ox, -(p @ u) * scale + oy], axis=-1)

    img_tex = t
    uv = a["uv"]
    idx = a["idx"].reshape(-1, 3).astype(np.int64)
    tcen = uv[idx].mean(axis=1)
    tc = img_tex[np.minimum((tcen[:, 1] * 512).astype(int), 511), np.minimum((tcen[:, 0] * 512).astype(int), 511), :3]
    rows = len(a["clips"])
    cols = max(c[2] for c in a["clips"])
    sheet = Image.new("RGB", (cols * size, rows * size), (22, 24, 28))
    for ri, (name, first, cnt) in enumerate(a["clips"]):
        for i in range(cnt):
            fr = first + i
            P = pos[fr]
            tp = P[idx]
            n = np.cross(tp[:, 1] - tp[:, 0], tp[:, 2] - tp[:, 0])
            vis = (n @ f) < 0  # counter-clockwise from outside faces the viewer
            shade = a["col"][fr][idx[:, 0], 0].astype(float) / 255.0
            depth = tp.mean(axis=1) @ f
            items = []  # (depth, polygon, fill, outline)
            for k in range(len(WHEELS)):
                pts = np.array(wc[k * 24:(k + 1) * 24])
                items.append((float(pts.mean(axis=0) @ f), [tuple(q) for q in proj(pts)], (44, 44, 50), (90, 90, 100)))
            sel = np.where(vis)[0]
            p2 = proj(tp[sel].reshape(-1, 3)).reshape(-1, 3, 2)
            for j, k in enumerate(sel):
                fill = tuple(int(c) for c in np.clip(tc[k] * shade[k], 0, 255))
                items.append((float(depth[k]), [tuple(q) for q in p2[j]], fill, fill))
            items.sort(key=lambda it: -it[0])  # far to near
            panel = Image.new("RGB", (size * SS, size * SS), (22, 24, 28))
            dr = ImageDraw.Draw(panel)
            for _dp, poly, fill, line in items:
                dr.polygon(poly, fill=fill, outline=line)
            panel = panel.resize((size, size), Image.LANCZOS)
            ImageDraw.Draw(panel).text((4, 2), "%s %d" % (name, i), fill=(255, 255, 255))
            sheet.paste(panel, (i * size, ri * size))
    sheet.save(out)
    print("wrote %s (%dx%d, %d panels)" % (out, sheet.width, sheet.height, a["F"]))
    return 0


# --------------------------------------------------------------- selftest
def build_hra(V=3, idx=(0, 1, 2), clips=(("a", 0, 1), ("b", 1, 1)), F=2, uv=None, magic=b"HRA1"):
    out = [struct.pack("<4sIIII", magic, V, len(idx), F, len(clips))]
    for n, fi, c in clips:
        out.append(struct.pack("<16sII", n.encode(), fi, c))
    uvs = uv if uv is not None else [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)] + [(0.0, 0.0)] * (V - 3)
    for u, v in uvs[:V]:
        out.append(struct.pack("<ff", u, v))
    for fr in range(F):
        for k in range(V):
            out.append(struct.pack("<fff4B", float(k), float(fr), 0.0, 200, 200, 200, 255))
    out.append(struct.pack("<%dH" % len(idx), *idx))
    return b"".join(out)


def cmd_selftest():
    fails = 0

    def expect_ok(label, fn):
        nonlocal fails
        try:
            fn()
            print("ok   %s" % label)
        except Exception as e:
            fails += 1
            print("FAIL %s: unexpected %r" % (label, e))

    def expect_bad(label, fn):
        nonlocal fails
        try:
            fn()
            fails += 1
            print("FAIL %s: accepted" % label)
        except HraError as e:
            print("ok   %s (%s)" % (label, e))
        except Exception as e:
            fails += 1
            print("FAIL %s: wrong exception %r" % (label, e))

    good = build_hra()
    expect_ok("valid file accepted", lambda: parse_hra(good, strict=False))
    expect_bad("strict clip table rejects the 2-clip file", lambda: parse_hra(good, strict=True))
    expect_bad("bad magic", lambda: parse_hra(build_hra(magic=b"HRAX"), strict=False))
    expect_bad("short file", lambda: parse_hra(good[:40], strict=False))
    expect_bad("short header", lambda: parse_hra(good[:10], strict=False))
    expect_bad("trailing byte", lambda: parse_hra(good + b"\0", strict=False))
    expect_bad("index out of range", lambda: parse_hra(build_hra(idx=(0, 1, 3)), strict=False))
    expect_bad("clip outside the frames", lambda: parse_hra(build_hra(clips=(("a", 0, 1), ("b", 1, 2))), strict=False))
    expect_bad("uv out of range", lambda: parse_hra(build_hra(uv=[(0.0, 0.0), (1.5, 0.0), (0.5, 1.0)]), strict=False))
    expect_bad("index count not multiple of 3", lambda: parse_hra(build_hra(idx=(0, 1)), strict=False))
    nan = bytearray(good)
    off = 20 + 24 * 2 + 8 * 3
    nan[off:off + 4] = struct.pack("<f", float("nan"))
    expect_bad("NaN position", lambda: parse_hra(bytes(nan), strict=False))
    print("selftest failures: %d" % fails)
    return 0 if fails == 0 else 1


def main(argv):
    if len(argv) >= 2 and argv[0] == "check" and len(argv) == 2:
        return cmd_check(argv[1])
    if len(argv) >= 3 and argv[0] == "sheet":
        yaw, pitch, size = 35.0, 20.0, 320
        rest = argv[3:]
        try:
            while rest:
                if rest[0] == "--yaw":
                    yaw = float(rest[1])
                elif rest[0] == "--pitch":
                    pitch = float(rest[1])
                elif rest[0] == "--size":
                    size = int(rest[1])
                else:
                    raise ValueError(rest[0])
                rest = rest[2:]
        except (ValueError, IndexError):
            print(__doc__.split("\n\n")[0])
            return 2
        return cmd_sheet(argv[1], argv[2], yaw, pitch, size)
    if argv == ["selftest"]:
        return cmd_selftest()
    print("usage: hires_anim_preview.py check <dir> | sheet <dir> <out.png> [--yaw N] [--pitch N] [--size N] | selftest")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
