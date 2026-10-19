#!/usr/bin/env python3
"""Creator Kit measuring tool (criteria K3, K4, K6 of DONE.md).

Pure Python 3 standard library.  Reads creator-kit-demo-local.glb (+ kit-info.json, optional kit-kart.glb),
samples every clip at every 1/30 s frame, computes joint world matrices and skinned corners, and prints
one line per criterion:

    PASS|FAIL|SKIP <id> <measured> (<threshold>) [detail]
    creator-kit-measure: pass P fail F skip S

Exit 0 when no criterion failed, 1 otherwise, 2 for usage / read errors.

Units: the GLB is in metres; everything inside this tool is in model units u (64 u = 1 m, glTF axes:
x = kart left, y = up, z = front).  Left side = ".L" = +x.

Design assumptions are marked "ASSUMPTION" in the comments.

    python tools/creator_kit_measure.py measure <creator-kit-demo-local.glb> --kit-info <kit-info.json>
           [--kart <kit-kart.glb>] [--vcol psx|gltf|off] [--json <out.json>] [--only K3,K4,K6]
    python tools/creator_kit_measure.py selftest
"""
import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
from bisect import bisect_right
from collections import Counter

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hires_glb_check as hgc  # noqa: E402  (decode_png only)

S = 64.0
DEG = 180.0 / math.pi
FIGURE_BONES = ["root", "pelvis", "spine", "chest", "neck", "head"] + \
    [b + s for s in (".L", ".R") for b in ("shoulder", "upper_arm", "forearm", "hand", "fingers", "thumb",
                                           "thigh", "shin", "foot", "toe")]
KART_JOINTS = ("kart", "kart_steer")
FOOT_NAMES = ("foot.L", "foot.R", "toe.L", "toe.R")
CLIP_NAMES = ["idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit",
              "podium_1", "podium_2", "podium_3", "lose", "menu", "finish_win", "finish_lose"]
CLIP_DUR = {"idle": 2.0, "steer_left": 0.5, "steer_right": 0.5, "jump": 0.6, "drift_left": 1.0,
            "drift_right": 1.0, "hit": 1.0, "podium_1": 2.0, "podium_2": 2.0, "podium_3": 2.0,
            "lose": 2.0, "menu": 3.0, "finish_win": 2.0, "finish_lose": 2.0}
LOOPS = ["idle", "drift_left", "drift_right", "podium_1", "podium_2", "podium_3", "lose", "menu",
         "finish_win", "finish_lose"]
STANDING = ["podium_1", "podium_2", "podium_3", "lose", "menu"]
FINISH = ["finish_win", "finish_lose"]  # optional (format 4.3), seated after the finish line
SEATED = ["idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit"] + FINISH
DRIVE = ["idle", "steer_left", "steer_right", "drift_left", "drift_right"]
STD_FLOOR = [[-53.0, 36.3], [-50.0, 33.9], [-46.0, 17.9], [-42.0, 10.9], [-40.0, 10.7], [-30.0, 9.4],
             [-20.0, 8.2], [-10.0, 6.9], [0.0, 5.7], [10.0, 5.9], [20.0, 6.2], [30.0, 6.5], [40.0, 6.8],
             [50.0, 7.2], [55.0, 7.3], [58.0, 7.4]]


# ---------------------------------------------------------------- vector / quaternion / matrix helpers
# matrices: column-major 4x4 lists of 16 floats (glTF), m[col*4 + row]

IDENT = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def mmul(a, b):
    out = []
    a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15 = a
    for c in (0, 4, 8, 12):
        b0, b1, b2, b3 = b[c], b[c + 1], b[c + 2], b[c + 3]
        out.append(a0 * b0 + a4 * b1 + a8 * b2 + a12 * b3)
        out.append(a1 * b0 + a5 * b1 + a9 * b2 + a13 * b3)
        out.append(a2 * b0 + a6 * b1 + a10 * b2 + a14 * b3)
        out.append(a3 * b0 + a7 * b1 + a11 * b2 + a15 * b3)
    return out


def minv_affine(m):
    """Inverse of an affine matrix (last row 0 0 0 1)."""
    a, b, c = m[0], m[4], m[8]
    d, e, f = m[1], m[5], m[9]
    g, h, i = m[2], m[6], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if abs(det) < 1e-18:
        raise ValueError("singular matrix")
    r00, r01, r02 = (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det
    r10, r11, r12 = (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det
    r20, r21, r22 = (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det
    tx, ty, tz = m[12], m[13], m[14]
    return [r00, r10, r20, 0.0, r01, r11, r21, 0.0, r02, r12, r22, 0.0,
            -(r00 * tx + r01 * ty + r02 * tz), -(r10 * tx + r11 * ty + r12 * tz),
            -(r20 * tx + r21 * ty + r22 * tz), 1.0]


def mpoint(m, p):
    return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14])


def mdir(m, p):
    return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2])


def qnorm(q):
    n = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3])
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n) if n > 0 else (0.0, 0.0, 0.0, 1.0)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qangle(a, b):
    """Angle in degrees between two unit quaternions (rotation taking a to b)."""
    r = qmul(qconj(a), b)
    return 2.0 * math.atan2(math.sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]), abs(r[3])) * DEG


def qaxis_angle(axis, deg):
    h = math.radians(deg) / 2.0
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))


def slerp(a, b, t):
    d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
    if d < 0.0:
        b = (-b[0], -b[1], -b[2], -b[3])
        d = -d
    if d > 0.9995:
        return qnorm(tuple(a[i] + t * (b[i] - a[i]) for i in range(4)))
    th = math.acos(d)
    s = math.sin(th)
    wa, wb = math.sin((1.0 - t) * th) / s, math.sin(t * th) / s
    return tuple(wa * a[i] + wb * b[i] for i in range(4))


def trs_m(t, q, s):
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    m = [0.0] * 16
    for c in range(3):
        for rr in range(3):
            m[c * 4 + rr] = r[rr][c] * s[c]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m


def m_to_trs(m):
    """Decompose an affine matrix (no shear) into (t, q, s)."""
    sx = math.sqrt(m[0] ** 2 + m[1] ** 2 + m[2] ** 2) or 1.0
    sy = math.sqrt(m[4] ** 2 + m[5] ** 2 + m[6] ** 2) or 1.0
    sz = math.sqrt(m[8] ** 2 + m[9] ** 2 + m[10] ** 2) or 1.0
    r00, r10, r20 = m[0] / sx, m[1] / sx, m[2] / sx
    r01, r11, r21 = m[4] / sy, m[5] / sy, m[6] / sy
    r02, r12, r22 = m[8] / sz, m[9] / sz, m[10] / sz
    tr = r00 + r11 + r22
    if tr > 0:
        k = math.sqrt(tr + 1.0) * 2
        q = ((r21 - r12) / k, (r02 - r20) / k, (r10 - r01) / k, 0.25 * k)
    elif r00 > r11 and r00 > r22:
        k = math.sqrt(1.0 + r00 - r11 - r22) * 2
        q = (0.25 * k, (r01 + r10) / k, (r02 + r20) / k, (r21 - r12) / k)
    elif r11 > r22:
        k = math.sqrt(1.0 + r11 - r00 - r22) * 2
        q = ((r01 + r10) / k, 0.25 * k, (r12 + r21) / k, (r02 - r20) / k)
    else:
        k = math.sqrt(1.0 + r22 - r00 - r11) * 2
        q = ((r02 + r20) / k, (r12 + r21) / k, 0.25 * k, (r10 - r01) / k)
    return (m[12], m[13], m[14]), qnorm(q), (sx, sy, sz)


def dist(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def vnorm(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return (v[0] / n, v[1] / n, v[2] / n)


def lin_to_srgb_byte(c):
    c = min(1.0, max(0.0, c))
    v = 12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055
    return int(math.floor(v * 255.0 + 0.5))


# ---------------------------------------------------------------- kit-info based helpers

def grip_points(info):
    """Hand grips on the ring at rest (u), as INTERFACE.md grip_points: centre + r*(sin60*xhat + cos60*uhat)
    with uhat = normalise(Y - (Y.n) n); L = +x."""
    st = info["steer"]
    n = vnorm(st["normal"])
    c = st["center"]
    r = st["radius"]
    yn = n[1]
    u = vnorm((0.0 - yn * n[0], 1.0 - yn * n[1], 0.0 - yn * n[2]))
    s60, c60 = math.sin(math.radians(60)), math.cos(math.radians(60))
    out = {}
    for side, sx in ((".L", 1.0), (".R", -1.0)):
        out[side] = (c[0] + r * (s60 * sx + c60 * u[0]), c[1] + r * c60 * u[1], c[2] + r * c60 * u[2])
    return out


def ring_top(info):
    st = info["steer"]
    n = vnorm(st["normal"])
    c = st["center"]
    yn = n[1]
    u = vnorm((0.0 - yn * n[0], 1.0 - yn * n[1], 0.0 - yn * n[2]))
    return (c[0] + st["radius"] * u[0], c[1] + st["radius"] * u[1], c[2] + st["radius"] * u[2])


def floor_at(fl, z):
    if not fl:
        return -1e9
    if z <= fl[0][0]:
        return fl[0][1]
    if z >= fl[-1][0]:
        return fl[-1][1]
    zs = [p[0] for p in fl]
    k = bisect_right(zs, z) - 1
    z0, y0 = fl[k]
    z1, y1 = fl[k + 1]
    return y0 + (z - z0) / (z1 - z0) * (y1 - y0) if z1 != z0 else y0


def mirror_name(n):
    if n.endswith(".L"):
        return n[:-2] + ".R"
    if n.endswith(".R"):
        return n[:-2] + ".L"
    return n


# ---------------------------------------------------------------- glTF model

class MeasureError(Exception):
    pass


def parse_glb(data):
    if len(data) < 20 or data[:4] != b"glTF":
        raise MeasureError("not a GLB file")
    pos = 12
    gltf = None
    binb = b""
    while pos + 8 <= len(data):
        ln, ty = struct.unpack_from("<II", data, pos)
        body = data[pos + 8:pos + 8 + ln]
        if ty == 0x4E4F534A and gltf is None:
            gltf = json.loads(body.decode("utf-8").rstrip(" \x00"))
        elif ty == 0x004E4942 and not binb:
            binb = body
        pos += 8 + ln
    if gltf is None:
        raise MeasureError("GLB has no JSON chunk")
    return gltf, binb


CT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NORMDIV = {"b": 127.0, "B": 255.0, "h": 32767.0, "H": 65535.0}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}


class Clip:
    def __init__(self, name, chans):
        self.name = name
        self.chans = chans  # (node, path, times, vals, interp)
        self.dur = max([c[2][-1] for c in chans if c[2]] or [0.0])
        self.nframes = int(round(self.dur * 30.0))


def sample_track(times, vals, interp, path, t):
    if interp == "CUBICSPLINE":
        vals = [vals[3 * k + 1] for k in range(len(times))]
    if t <= times[0]:
        return vals[0]
    if t >= times[-1]:
        return vals[-1]
    k = bisect_right(times, t) - 1
    if interp == "STEP":
        return vals[k]
    f = (t - times[k]) / (times[k + 1] - times[k])
    a, b = vals[k], vals[k + 1]
    if path == "rotation":
        return slerp(a, b, f)
    return tuple(a[i] + f * (b[i] - a[i]) for i in range(3))


class Model:
    """A parsed GLB: nodes, hierarchy, accessors, animations.  Lengths in u (metres * 64)."""

    def __init__(self, data):
        self.g, self.bin = parse_glb(data)
        g = self.g
        self.nodes = g.get("nodes", [])
        n = len(self.nodes)
        self.parent = [-1] * n
        for i, nd in enumerate(self.nodes):
            for c in nd.get("children", []):
                self.parent[c] = i
        scenes = g.get("scenes")
        if scenes:
            self.roots = list(scenes[g.get("scene", 0)].get("nodes", []))
        else:
            self.roots = [i for i in range(n) if self.parent[i] < 0]
        self.order = []
        stack = list(reversed(self.roots))
        while stack:
            i = stack.pop()
            self.order.append(i)
            stack.extend(reversed(self.nodes[i].get("children", [])))
        self.names = {}
        for i, nd in enumerate(self.nodes):
            self.names.setdefault(nd.get("name", ""), i)
        self.rest_local = [self._local(nd) for nd in self.nodes]
        self._acc = {}
        self._mesh = {}
        self._anims = None
        self._rest_world = None

    @staticmethod
    def _local(nd):
        if "matrix" in nd and len(nd["matrix"]) == 16:
            m = [float(v) for v in nd["matrix"]]
            t, q, s = m_to_trs(m)
        else:
            t = nd.get("translation", [0, 0, 0])
            q = qnorm(tuple(nd.get("rotation", [0, 0, 0, 1])))
            s = nd.get("scale", [1, 1, 1])
        return [tuple(x * S for x in t), tuple(q), tuple(s)]

    def world(self, loc):
        w = [None] * len(self.nodes)
        for ni in self.order:
            t, q, s = loc[ni]
            m = trs_m(t, q, s)
            p = self.parent[ni]
            w[ni] = m if p < 0 else mmul(w[p], m)
        return w

    @property
    def rest_world(self):
        if self._rest_world is None:
            self._rest_world = self.world(self.rest_local)
        return self._rest_world

    def subtree(self, ni):
        out, st = [], [ni]
        while st:
            i = st.pop()
            out.append(i)
            st.extend(self.nodes[i].get("children", []))
        return out

    def reachable(self):
        return list(self.order)

    def acc(self, idx):
        """Accessor -> list of tuples (floats for float / normalized, ints otherwise)."""
        if idx in self._acc:
            return self._acc[idx]
        a = self.g["accessors"][idx]
        code, size = CT[a["componentType"]]
        nc = NC[a["type"]]
        count = a["count"]
        if "bufferView" not in a:
            res = [tuple([0] * nc)] * count
        else:
            bv = self.g["bufferViews"][a["bufferView"]]
            base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
            stride = bv.get("byteStride") or size * nc
            norm = a.get("normalized", False) and code in NORMDIV
            res = []
            if stride == size * nc:
                flat = struct.unpack_from("<%d%s" % (count * nc, code), self.bin, base)
                rows = [flat[k * nc:(k + 1) * nc] for k in range(count)]
            else:
                rows = [struct.unpack_from("<%d%s" % (nc, code), self.bin, base + k * stride) for k in range(count)]
            if norm:
                div = NORMDIV[code]
                rows = [tuple(max(x / div, -1.0) for x in r) for r in rows]
            res = [tuple(r) for r in rows]
        self._acc[idx] = res
        return res

    def mesh(self, mi):
        """Concatenated primitives of mesh mi: pos (u), idx (flat ints), joints, weights, color, mats."""
        if mi in self._mesh:
            return self._mesh[mi]
        pos, idx, joints, weights, color = [], [], [], [], []
        has_c = has_j = False
        extra_joints = False
        for pr in self.g["meshes"][mi]["primitives"]:
            at = pr["attributes"]
            base = len(pos)
            p = self.acc(at["POSITION"])
            pos.extend((x * S, y * S, z * S) for x, y, z in p)
            if "indices" in pr:
                idx.extend(base + i[0] for i in self.acc(pr["indices"]))
            else:
                idx.extend(range(base, base + len(p)))
            if "JOINTS_0" in at and "WEIGHTS_0" in at:
                has_j = True
                joints.extend(self.acc(at["JOINTS_0"]))
                weights.extend(self.acc(at["WEIGHTS_0"]))
            else:
                joints.extend([(0, 0, 0, 0)] * len(p))
                weights.extend([(0.0, 0.0, 0.0, 0.0)] * len(p))
            if "JOINTS_1" in at:
                extra_joints = True
            if "COLOR_0" in at:
                has_c = True
                color.extend(self.acc(at["COLOR_0"]))
            else:
                color.extend([None] * len(p))
        res = {"pos": pos, "idx": idx, "joints": joints if has_j else None, "weights": weights if has_j else None,
               "color": color if has_c else None, "ntri": len(idx) // 3, "extra_joints": extra_joints}
        self._mesh[mi] = res
        return res

    def anims(self):
        if self._anims is not None:
            return self._anims
        out = {}
        for ai, a in enumerate(self.g.get("animations", [])):
            name = a.get("name", "anim%d" % ai)
            chans = []
            for ch in a.get("channels", []):
                tg = ch.get("target", {})
                path = tg.get("path")
                if path not in ("translation", "rotation", "scale") or "node" not in tg:
                    continue
                sm = a["samplers"][ch["sampler"]]
                times = [x[0] for x in self.acc(sm["input"])]
                vals = list(self.acc(sm["output"]))
                if path == "translation":
                    vals = [(v[0] * S, v[1] * S, v[2] * S) for v in vals]
                elif path == "rotation":
                    vals = [qnorm(v) for v in vals]
                if times:
                    chans.append((tg["node"], path, times, vals, sm.get("interpolation", "LINEAR")))
            out[name] = Clip(name, chans)
        self._anims = out
        return out

    def pose_local(self, clip, t):
        loc = [list(x) for x in self.rest_local]
        slot = {"translation": 0, "rotation": 1, "scale": 2}
        for node, path, times, vals, interp in clip.chans:
            loc[node][slot[path]] = sample_track(times, vals, interp, path, t)
        return loc

    def images(self):
        """sha256 of (w, h, RGBA bytes) for each image (PNG decoded; others hashed raw)."""
        out = []
        for im in self.g.get("images", []):
            raw = b""
            if "bufferView" in im:
                bv = self.g["bufferViews"][im["bufferView"]]
                o = bv.get("byteOffset", 0)
                raw = bytes(self.bin[o:o + bv["byteLength"]])
            info = hgc.decode_png(raw, keep_raw=True) if raw else {"decoded": False}
            h = hashlib.sha256()
            if info.get("decoded") and "raw" in info:
                px = info["raw"]
                w, hh = info["w"], info["h"]
                if info["ctype"] == 2:
                    rgba = bytearray(w * hh * 4)
                    rgba[0::4], rgba[1::4], rgba[2::4] = px[0::3], px[1::3], px[2::3]
                    rgba[3::4] = b"\xff" * (w * hh)
                    px = bytes(rgba)
                h.update(struct.pack("<II", w, hh))
                h.update(px)
            else:
                h.update(b"raw:")
                h.update(raw)
            out.append(h.hexdigest())
        return out

    def mesh_nodes(self, node_ids):
        return [i for i in node_ids if "mesh" in self.nodes[i]]


# ---------------------------------------------------------------- the kit being measured

class Kit:
    def __init__(self, data, info):
        self.m = Model(data)
        self.info = info
        m = self.m
        # the skinned puppet mesh node: named "puppet" (ASSUMPTION: else the first node with mesh + skin)
        pn = m.names.get("puppet")
        if pn is None or "skin" not in m.nodes[pn] or "mesh" not in m.nodes[pn]:
            pn = next((i for i in m.order if "skin" in m.nodes[i] and "mesh" in m.nodes[i]), None)
        self.pn = pn
        self.has_puppet = pn is not None
        if self.has_puppet:
            nd = m.nodes[pn]
            skin = m.g["skins"][nd["skin"]]
            self.joints = list(skin["joints"])
            self.jname = [m.nodes[j].get("name", "") for j in self.joints]
            self.jindex = {n: k for k, n in enumerate(self.jname)}
            if "inverseBindMatrices" in skin:
                self.ibm = []
                for t in m.acc(skin["inverseBindMatrices"]):
                    t = list(t)
                    t[12], t[13], t[14] = t[12] * S, t[13] * S, t[14] * S
                    self.ibm.append(t)
            else:
                self.ibm = [IDENT] * len(self.joints)
            self.mesh = m.mesh(nd["mesh"])
            self.pos = self.mesh["pos"]
            nv = len(self.pos)
            self.infl = []
            self.dom = []
            if self.mesh["joints"]:
                for v in range(nv):
                    inf = [(j, w) for j, w in zip(self.mesh["joints"][v], self.mesh["weights"][v]) if w > 0.0]
                    self.infl.append(inf)
                    self.dom.append(max(inf, key=lambda e: e[1])[0] if inf else -1)
            else:
                self.infl = [[] for _ in range(nv)]
                self.dom = [-1] * nv
        self._frames = {}
        self._rest_all = None
        self._bboxes = None

    # ----- clips
    @property
    def clips(self):
        return self.m.anims()

    def frames(self, name):
        """List over frames 0..round(dur*30) of (local, world) at t = i/30 (clamped by the sampler)."""
        if name not in self._frames:
            c = self.clips[name]
            fr = []
            for i in range(c.nframes + 1):
                loc = self.m.pose_local(c, i / 30.0)
                fr.append((loc, self.m.world(loc)))
            self._frames[name] = fr
        return self._frames[name]

    def rest_frame(self):
        return (self.m.rest_local, self.m.rest_world)

    # ----- skinning
    def dom_set(self, names):
        ids = {self.jindex[n] for n in names if n in self.jindex}
        return [v for v in range(len(self.pos)) if self.dom[v] in ids]

    def jmats(self, world, jset):
        return {j: mmul(world[self.joints[j]], self.ibm[j]) for j in jset}

    def skin_pts(self, vs, world, jm=None):
        """Skin vertices vs: sum w * (world[joint] @ IBM @ v)."""
        if jm is None:
            need = {j for v in vs for j, _ in self.infl[v]}
            jm = self.jmats(world, need)
        out = []
        pos = self.pos
        for v in vs:
            px, py, pz = pos[v]
            x = y = z = 0.0
            for j, w in self.infl[v]:
                mm = jm[j]
                x += w * (mm[0] * px + mm[4] * py + mm[8] * pz + mm[12])
                y += w * (mm[1] * px + mm[5] * py + mm[9] * pz + mm[13])
                z += w * (mm[2] * px + mm[6] * py + mm[10] * pz + mm[14])
            out.append((x, y, z))
        return out

    def rest_all(self):
        if self._rest_all is None:
            self._rest_all = self.skin_pts(range(len(self.pos)), self.m.rest_world)
        return self._rest_all

    def joint_pos(self, name, world):
        ni = self.m.names.get(name)
        if ni is None:
            raise MeasureError("node %s missing" % name)
        return (world[ni][12], world[ni][13], world[ni][14])

    # ----- kart parts
    def kart_part_nodes(self, include_wheels=True):
        m = self.m
        kn = m.names.get("kart")
        ids = []
        if kn is not None:
            ids = m.mesh_nodes(m.subtree(kn))
        if include_wheels:
            for n, i in m.names.items():
                if n.startswith("wheel_") and "mesh" in m.nodes[i] and i not in ids:
                    ids.append(i)
        else:
            ids = [i for i in ids if not m.nodes[i].get("name", "").startswith("wheel_")]
        return [i for i in ids if "skin" not in m.nodes[i]]

    def part_bbox(self, ni):
        pos = self.m.mesh(self.m.nodes[ni]["mesh"])["pos"]
        if not pos:
            return None
        mn = [min(p[a] for p in pos) for a in range(3)]
        mx = [max(p[a] for p in pos) for a in range(3)]
        return mn, mx

    def joint_boxes(self):
        """Per joint the bounding box of its dominant-joint vertices in bind (mesh) space."""
        if self._bboxes is None:
            bb = {}
            for v, j in enumerate(self.dom):
                if j < 0:
                    continue
                p = self.pos[v]
                if j not in bb:
                    bb[j] = [list(p), list(p)]
                else:
                    for a in range(3):
                        if p[a] < bb[j][0][a]:
                            bb[j][0][a] = p[a]
                        if p[a] > bb[j][1][a]:
                            bb[j][1][a] = p[a]
            self._bboxes = bb
        return self._bboxes

    def hull(self, world):
        lo = [1e18] * 3
        hi = [-1e18] * 3
        jm = self.jmats(world, self.joint_boxes().keys()) if self.has_puppet else {}
        for j, (mn, mx) in (self.joint_boxes().items() if self.has_puppet else []):
            for cx in (mn[0], mx[0]):
                for cy in (mn[1], mx[1]):
                    for cz in (mn[2], mx[2]):
                        p = mpoint(jm[j], (cx, cy, cz))
                        for a in range(3):
                            lo[a] = min(lo[a], p[a])
                            hi[a] = max(hi[a], p[a])
        for ni in self.kart_part_nodes(True):
            bb = self.part_bbox(ni)
            if bb is None:
                continue
            for cx in (bb[0][0], bb[1][0]):
                for cy in (bb[0][1], bb[1][1]):
                    for cz in (bb[0][2], bb[1][2]):
                        p = mpoint(world[ni], (cx, cy, cz))
                        for a in range(3):
                            lo[a] = min(lo[a], p[a])
                            hi[a] = max(hi[a], p[a])
        return lo, hi

    # ----- kit-info access (standard values when a key is missing)
    @property
    def seat(self):
        return self.info.get("seat", [0.0, 10.2, 0.9])

    @property
    def kart_box(self):
        return self.info.get("kart_box", [-27.9, 5.6, -54.1, 28.3, 52.1, 58.3])

    @property
    def floor(self):
        return self.info.get("floor", STD_FLOOR)

    @property
    def steer(self):
        return self.info.get("steer", {"center": [0, 38.0, 24.0], "axis": [0, -0.316, 0.949],
                                       "normal": [0, -0.316, 0.949], "radius": 10.5})

    def foot_vs(self):
        return self.dom_set(FOOT_NAMES)

    def have(self, names):
        return [n for n in names if n in self.clips]


# ---------------------------------------------------------------- results

def fnum(x):
    return "%.3f" % x


class Res:
    def __init__(self, cid, status, measured, thr, detail="", value=None):
        self.cid, self.status, self.measured, self.thr, self.detail, self.value = cid, status, measured, thr, detail, value

    def line(self):
        s = "%s %s %s (%s)" % (self.status, self.cid, self.measured, self.thr)
        return s + (" " + self.detail if self.detail else "")

    def as_dict(self):
        return {"detail": self.detail, "id": self.cid, "measured": self.measured, "status": self.status,
                "threshold": self.thr, "value": self.value}


def ck(cond, measured, thr, detail="", value=None):
    return ("PASS" if cond else "FAIL", measured, thr, detail, value)


def skip(why):
    return ("SKIP", "-", "-", why, None)


NO_PUPPET = ("FAIL", "-", "-", "no skinned puppet mesh found", None)


# ---------------------------------------------------------------- K3 criteria

def c3_1(K):
    ys = [p[1] for p in K.rest_all()]
    y = max(ys)
    return ck(82 <= y <= 90, fnum(y) + " u", "max y 82..90 u", value=y)


def c3_2(K):
    xs = [p[0] for p in K.rest_all()]
    lo, hi = min(xs), max(xs)
    return ck(lo >= -33.8 and hi <= 34.3, "x %s..%s u" % (fnum(lo), fnum(hi)), "inside -33.8..34.3 u", value=[lo, hi])


def c3_3(K):
    vs = K.dom_set(["pelvis"])
    if not vs:
        return ("FAIL", "no pelvis vertices", "y = seat y +-1.0, xz <= 2.0", "", None)
    rest = K.rest_all()
    pts = [rest[v] for v in vs]
    ymin = min(p[1] for p in pts)
    cx = sum(p[0] for p in pts) / len(pts)
    cz = sum(p[2] for p in pts) / len(pts)
    seat = K.seat
    dy = ymin - seat[1]
    dxz = math.hypot(cx - seat[0], cz - seat[2])
    # ASSUMPTION: the centroid is taken over ALL pelvis-dominant vertices (not only the lowest ones)
    return ck(abs(dy) <= 1.0 and dxz <= 2.0, "ymin %s (seat %s), xz off %s u" % (fnum(ymin), fnum(seat[1]), fnum(dxz)),
              "|dy| <= 1.0 and xz <= 2.0 u", "%d pelvis vertices" % len(vs), value=[dy, dxz])


def c3_4(K):
    gp = grip_points(K.info if "steer" in K.info else {"steer": K.steer})
    worst = 0.0
    for side in (".L", ".R"):
        p = K.joint_pos("fingers" + side, K.m.rest_world)
        worst = max(worst, dist(p, gp[side]))
    return ck(worst <= 1.0, fnum(worst) + " u", "<= 1.0 u", value=worst)


def foot_violation(K, pts, tol):
    """Largest violation (u) of: inside kart_box x/z and y >= floor(z) - tol."""
    b = K.kart_box
    worst = 0.0
    fl = K.floor
    for x, y, z in pts:
        v = max(b[0] - x, x - b[3], b[2] - z, z - b[5], floor_at(fl, z) - tol - y)
        if v > worst:
            worst = v
    return worst


def c3_5(K):
    vs = K.foot_vs()
    if not vs:
        return ("FAIL", "no foot/toe vertices", "inside kart_box", "", None)
    pts = K.skin_pts(vs, K.m.rest_world)
    w = foot_violation(K, pts, 0.5)
    return ck(w <= 0.0, "violation %s u" % fnum(w), "0 (box x/z, y >= floor - 0.5)", "%d vertices" % len(vs), value=w)


def c3_6(K):
    # the puppet of the local demo is the Hi-Res puppet (the kit's way hires): within the Hi-Res budget
    # (include/rldhires.inc RLDHIRES_BUDGET_TRIANGLES); the Low-Poly target (8000) is checked on its own figure
    n = K.mesh["ntri"]
    return ck(n <= 64000, str(n), "<= 64000 (Hi-Res budget)", value=n)


def c3_7(K):
    fig = [n for n in K.jname if n not in KART_JOINTS]
    miss = [n for n in FIGURE_BONES if n not in K.jname]
    ok = len(fig) == 26 and len(K.jname) <= 32
    return ck(ok, "figure %d, total %d" % (len(fig), len(K.jname)), "figure 26, total <= 32",
              ("missing: " + ",".join(miss)) if miss else "", value=[len(fig), len(K.jname)])


def c3_8(K):
    if not K.mesh["joints"]:
        return ("FAIL", "no JOINTS_0/WEIGHTS_0", "1..4 weights, sum 1 +-1e-3", "", None)
    nj = len(K.joints)
    bad = 0
    for js, ws in zip(K.mesh["joints"], K.mesh["weights"]):
        nz = sum(1 for w in ws if w > 0.0)
        if not (1 <= nz <= 4) or abs(sum(ws) - 1.0) > 1e-3 or any(j >= nj for j in js):
            bad += 1
    if K.mesh["extra_joints"]:
        bad += 1
    return ck(bad == 0, "%d bad vertices" % bad, "0 (1..4 weights, sum 1 +-1e-3, valid joints)",
              "JOINTS_1 present" if K.mesh["extra_joints"] else "", value=bad)


def c3_9(K):
    keyid = {}
    parent = []

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    vid = []
    for x, y, z in K.pos:
        key = (round(x / S * 1e6), round(y / S * 1e6), round(z / S * 1e6))
        if key not in keyid:
            keyid[key] = len(parent)
            parent.append(len(parent))
        vid.append(keyid[key])
    idx = K.mesh["idx"]
    for t in range(0, len(idx) - 2, 3):
        a, b, c = find(vid[idx[t]]), find(vid[idx[t + 1]]), find(vid[idx[t + 2]])
        parent[b] = a
        parent[find(c)] = find(a)
    n = len({find(i) for i in range(len(parent))})
    return ck(n == 1, str(n), "exactly 1", value=n)


def c3_10(K):
    pts = K.rest_all()
    cell = 0.05
    grid = {}
    for p in pts:
        grid.setdefault((math.floor(p[0] / cell), math.floor(p[1] / cell), math.floor(p[2] / cell)), []).append(p)
    ok = 0
    for x, y, z in pts:
        mx = -x
        cx, cy, cz = math.floor(mx / cell), math.floor(y / cell), math.floor(z / cell)
        found = False
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q in grid.get((cx + dx, cy + dy, cz + dz), ()):
                        if dist(q, (mx, y, z)) <= 0.01:
                            found = True
                            break
                    if found:
                        break
                if found:
                    break
            if found:
                break
        ok += found
    pct = 100.0 * ok / max(1, len(pts))
    return ck(pct >= 99.0, "%.2f %%" % pct, ">= 99 %", value=pct)


def c3_11(K):
    if "podium_2" not in K.clips:
        return ("FAIL", "no podium_2", "max y 112..128 u", "", None)
    world = K.frames("podium_2")[0][1]
    y = max(p[1] for p in K.skin_pts(range(len(K.pos)), world))
    return ck(112 <= y <= 128, fnum(y) + " u", "max y 112..128 u", value=y)


# ---------------------------------------------------------------- K4 criteria

def need_clips(K, names):
    return K.have(names)


def c4_2(K):
    miss = [n for n in CLIP_NAMES if n not in K.clips]
    worst = 0.0
    for n in CLIP_NAMES:
        if n in K.clips:
            worst = max(worst, abs(K.clips[n].dur - CLIP_DUR[n]))
    ok = not miss and worst <= 1.0 / 30.0 + 1e-6
    nc = len(CLIP_NAMES)
    return ck(ok, "%d/%d present, max dev %s s" % (nc - len(miss), nc, fnum(worst)), "all %d, dev <= 1/30 s" % nc,
              ("missing: " + ",".join(miss)) if miss else "", value=worst)


def joints_all(K):
    return list(K.joints)


def c4_3(K):
    cl = need_clips(K, LOOPS)
    if not cl:
        return skip("no loop clips")
    wa = wt = 0.0
    at = ""
    for n in cl:
        fr = K.frames(n)
        l0, ln = fr[0][0], fr[-1][0]
        for j in joints_all(K):
            a = qangle(l0[j][1], ln[j][1])
            t = dist(l0[j][0], ln[j][0])
            if a > wa:
                wa, at = a, "%s/%s" % (n, K.m.nodes[j].get("name"))
            wt = max(wt, t)
    return ck(wa <= 0.5 and wt <= 0.1, "angle %s deg, move %s u" % (fnum(wa), fnum(wt)), "<= 0.5 deg, <= 0.1 u",
              "worst %s" % at if wa > 0.5 else "", value=[wa, wt])


def signed_steer_angle(K, q):
    ks = K.m.names["kart_steer"]
    rest = K.m.rest_local[ks][1]
    r = qmul(q, qconj(rest))
    if r[3] < 0:
        r = (-r[0], -r[1], -r[2], -r[3])
    ax = vnorm(K.steer.get("axis", K.steer["normal"]))
    return 2.0 * math.atan2(r[0] * ax[0] + r[1] * ax[1] + r[2] * ax[2], r[3]) * DEG


def c4_4(K):
    cl = need_clips(K, ["steer_left", "steer_right"])
    if not cl:
        return skip("no steer clips")
    ks = K.m.names["kart_steer"]
    rest = K.m.rest_local[ks][1]
    worst_back = 0.0
    for n in cl:
        prev = 0.0
        for loc, _ in K.frames(n):
            a = qangle(rest, loc[ks][1])
            worst_back = max(worst_back, prev - a)
            prev = a
    dev = 0.0
    if "idle" in K.clips:
        i0 = K.frames("idle")[0][0]
        for n in cl:
            l0 = K.frames(n)[0][0]
            for j in joints_all(K):
                dev = max(dev, qangle(l0[j][1], i0[j][1]))
    else:
        return ("FAIL", "no idle clip", "monotone, frame 0 vs idle <= 2 deg", "", None)
    return ck(worst_back <= 0.01 and dev <= 2.0, "backstep %s deg, frame0 dev %s deg" % (fnum(worst_back), fnum(dev)),
              "non-decreasing (0.01), frame 0 <= 2 deg", value=[worst_back, dev])


def c4_5(K):
    cl = need_clips(K, ["steer_left", "steer_right"])
    if not cl:
        return skip("no steer clips")
    ks = K.m.names["kart_steer"]
    rest_w = K.m.rest_world[ks]
    inv = minv_affine(rest_w)
    top = ring_top({"steer": K.steer})
    msgs = []
    ok = True
    for n in cl:
        loc, w = K.frames(n)[-1]
        ang = qangle(K.m.rest_local[ks][1], loc[ks][1])
        px = mpoint(mmul(w[ks], inv), top)[0]
        want_pos = n == "steer_left"
        sign_ok = px > 0 if want_pos else px < 0
        ok = ok and 40.0 <= ang <= 50.0 and sign_ok
        msgs.append("%s %s deg x=%s" % (n, fnum(ang), fnum(px)))
    return ck(ok, "; ".join(msgs), "40..50 deg, top x>0 left / <0 right", value=None)


def c4_6(K):
    msgs = []
    ok = True
    rest = K.joint_pos("pelvis", K.m.rest_world)
    any_ = False
    for n in K.have(DRIVE + ["jump", "hit"] + FINISH):
        any_ = True
        up = dn = hz = dd = 0.0
        for _, w in K.frames(n):
            p = K.joint_pos("pelvis", w)
            dy = p[1] - rest[1]
            up, dn = max(up, dy), max(dn, -dy)
            hz = max(hz, math.hypot(p[0] - rest[0], p[2] - rest[2]))
            dd = max(dd, dist(p, rest))
        if n in ("jump", "hit"):
            good = up <= 6.0 and hz <= 2.0 and dn <= 2.0
            msgs.append("%s up %s hz %s dn %s" % (n, fnum(up), fnum(hz), fnum(dn)))
        else:
            good = dd <= 2.0
            msgs.append("%s %s" % (n, fnum(dd)))
        ok = ok and good
    if not any_:
        return skip("no seated clips")
    return ck(ok, "; ".join(msgs), "drive and finish <= 2.0 u; jump/hit up <= 6, xz <= 2, down <= 2")


def c4_7(K):
    cl = K.have(DRIVE)
    if not cl:
        return skip("no drive clips")
    ks = K.m.names["kart_steer"]
    inv = minv_affine(K.m.rest_world[ks])
    gp = grip_points({"steer": K.steer})
    worst = 0.0
    at = ""
    for n in cl:
        for i, (_, w) in enumerate(K.frames(n)):
            mm = mmul(w[ks], inv)
            for side in (".L", ".R"):
                d = dist(K.joint_pos("fingers" + side, w), mpoint(mm, gp[side]))
                if d > worst:
                    worst, at = d, "%s f%d fingers%s" % (n, i, side)
    return ck(worst <= 1.5, fnum(worst) + " u", "<= 1.5 u", "worst " + at if worst > 1.5 else "", value=worst)


def c4_8(K):
    cl = K.have(SEATED)
    if not cl:
        return skip("no seated clips")
    vs = K.foot_vs()
    if not vs:
        return ("FAIL", "no foot/toe vertices", "inside kart_box, y >= floor - 1.0", "", None)
    need = {j for v in vs for j, _ in K.infl[v]}
    worst = 0.0
    at = ""
    for n in cl:
        for i, (_, w) in enumerate(K.frames(n)):
            v = foot_violation(K, K.skin_pts(vs, w, K.jmats(w, need)), 1.0)
            if v > worst:
                worst, at = v, "%s f%d" % (n, i)
    return ck(worst <= 0.0, "violation %s u" % fnum(worst), "0 (box x/z, y >= floor - 1.0)",
              "worst " + at if worst > 0 else "", value=worst)


def foot_miny_series(K, name, vs, need):
    out = []
    for _, w in K.frames(name):
        pts = K.skin_pts(vs, w, K.jmats(w, need))
        out.append(min(p[1] for p in pts))
    return out


def c4_9(K):
    cl = K.have(["podium_2", "podium_3", "lose", "menu"])
    if not cl:
        return skip("no standing clips")
    vs = K.foot_vs()
    if not vs:
        return ("FAIL", "no foot/toe vertices", "min y within +-0.64", "", None)
    need = {j for v in vs for j, _ in K.infl[v]}
    lo, hi = 1e9, -1e9
    for n in cl:
        s = foot_miny_series(K, n, vs, need)
        lo, hi = min(lo, min(s)), max(hi, max(s))
    return ck(lo >= -0.64 and hi <= 0.64, "min y %s..%s u" % (fnum(lo), fnum(hi)), "-0.64..+0.64 u", value=[lo, hi])


def c4_10(K):
    if "podium_1" not in K.clips:
        return skip("no podium_1")
    vs = K.foot_vs()
    if not vs:
        return ("FAIL", "no foot/toe vertices", "-0.64..9.6", "", None)
    need = {j for v in vs for j, _ in K.infl[v]}
    s = foot_miny_series(K, "podium_1", vs, need)
    ok = min(s) >= -0.64 and max(s) <= 9.6 and abs(s[0]) <= 0.64
    return ck(ok, "min y %s..%s, frame 0 %s" % (fnum(min(s)), fnum(max(s)), fnum(s[0])),
              ">= -0.64, <= 9.6, frame 0 +-0.64", value=[min(s), max(s), s[0]])


def c4_11(K):
    cl = list(K.clips)
    if not cl:
        return skip("no clips")
    nodes = [K.m.names[n] for n in ("root", "kart") if n in K.m.names]
    worst = 0.0
    for n in cl:
        for loc, _ in K.frames(n):
            for ni in nodes:
                r = K.m.rest_local[ni]
                q, qr = loc[ni][1], r[1]
                if q[0] * qr[0] + q[1] * qr[1] + q[2] * qr[2] + q[3] * qr[3] < 0:
                    q = tuple(-x for x in q)
                d = max([abs(loc[ni][0][a] - r[0][a]) for a in range(3)] +
                        [abs(loc[ni][2][a] - r[2][a]) for a in range(3)] + [abs(q[a] - qr[a]) for a in range(4)])
                worst = max(worst, d)
    hz = 0.0
    for n in K.have(STANDING):
        for _, w in K.frames(n):
            p = K.joint_pos("pelvis", w)
            hz = max(hz, math.hypot(p[0], p[2]))
    # root/kart tolerance 1e-4 is applied to metres for translation, plain for scale / quaternion components
    return ck(worst <= 1e-4 and hz <= 6.4, "root/kart dev %.2e, pelvis xz %s u" % (worst, fnum(hz)),
              "<= 1e-4, <= 6.4 u", value=[worst, hz])


def c4_12(K):
    cl = K.have(STANDING)
    if not cl:
        return skip("no standing clips")
    hd = K.m.names["head"]
    inv = minv_affine(K.m.rest_world[hd])
    low = 9.0
    for n in cl:
        w = K.frames(n)[0][1]
        v = vnorm(mdir(mmul(w[hd], inv), (0.0, 0.0, 1.0)))
        low = min(low, v[2])
    return ck(low > 0.9, "min z %.3f" % low, "> 0.9", value=low)


def c4_13(K):
    if "idle" not in K.clips:
        return ("FAIL", "no idle clip", ">= 10 deg", "", None)
    idle = K.clips["idle"]
    worst_clip, worst_val = "", 1e9
    for n, c in sorted(K.clips.items()):
        if n == "idle" or n not in CLIP_NAMES:
            continue
        best = 0.0
        for k in range(11):
            f = k / 10.0
            la = K.m.pose_local(c, f * c.dur)
            li = K.m.pose_local(idle, f * idle.dur)
            for j in joints_all(K):
                best = max(best, qangle(la[j][1], li[j][1]))
        if best < worst_val:
            worst_clip, worst_val = n, best
    if not worst_clip:
        return skip("no clips besides idle")
    return ck(worst_val >= 10.0, "min over clips %s deg (%s)" % (fnum(worst_val), worst_clip), ">= 10 deg", value=worst_val)


def c4_14(K):
    worst_p, worst_a = 0.0, 0.0
    done = 0
    ks = K.m.names["kart_steer"]
    names = [K.m.nodes[j].get("name", "") for j in K.joints]
    for a, b in (("steer_left", "steer_right"), ("drift_left", "drift_right")):
        if a not in K.clips or b not in K.clips:
            continue
        fa, fb = K.frames(a), K.frames(b)
        if len(fa) != len(fb):
            return ("FAIL", "frame counts differ %s/%s" % (len(fa), len(fb)), "<= 0.1 u, <= 1 deg", "", None)
        done += 1
        for (la, wa), (lb, wb) in zip(fa, fb):
            for j, nm in zip(K.joints, names):
                pa = (wa[j][12], wa[j][13], wa[j][14])
                other = K.m.names.get(mirror_name(nm))
                if other is None:
                    return ("FAIL", "no mirror joint for " + nm, "<= 0.1 u", "", None)
                pb = (-wb[other][12], wb[other][13], wb[other][14])
                worst_p = max(worst_p, dist(pa, pb))
            sa = signed_steer_angle(K, la[ks][1])
            sb = signed_steer_angle(K, lb[ks][1])
            worst_a = max(worst_a, abs(sa + sb))
    if not done:
        return skip("no left/right clip pair")
    return ck(worst_p <= 0.1 and worst_a <= 1.0, "pos %s u, steer %s deg" % (fnum(worst_p), fnum(worst_a)),
              "<= 0.1 u, <= 1 deg", value=[worst_p, worst_a])


def c4_15(K):
    lo, hi = K.hull(K.m.rest_world)
    ext = [hi[a] - lo[a] for a in range(3)]
    for n in K.clips:
        for _, w in K.frames(n):
            lo, hi = K.hull(w)
            ext = [max(ext[a], hi[a] - lo[a]) for a in range(3)]
    return ck(max(ext) <= 160.0, "extent %s / %s / %s u" % tuple(fnum(e) for e in ext), "<= 160 u per axis", value=ext)


def head_turn(K, w):
    """(pitch down, yaw toward +x) in degrees of the head's forward direction against its rest pose."""
    hd = K.m.names["head"]
    f = vnorm(mdir(mmul(w[hd], minv_affine(K.m.rest_world[hd])), (0.0, 0.0, 1.0)))
    return math.asin(max(-1.0, min(1.0, -f[1]))) * DEG, math.atan2(f[0], f[2]) * DEG


def c4_16(K):
    # the optional finish clips: finish_win cheers with both hands up (above the head joint) in every frame;
    # finish_lose keeps the hands on the wheel, the head down and shaking
    cl = K.have(FINISH)
    if not cl:
        return skip("no finish clips")
    msgs = []
    ok = True
    if "finish_win" in cl:
        low = 1e9
        for _, w in K.frames("finish_win"):
            hy = K.joint_pos("head", w)[1]
            for side in (".L", ".R"):
                low = min(low, K.joint_pos("fingers" + side, w)[1] - hy)
        ok = ok and low >= 0.0
        msgs.append("win: fingers - head y min %s u" % fnum(low))
    if "finish_lose" in cl:
        ks = K.m.names["kart_steer"]
        inv = minv_affine(K.m.rest_world[ks])
        gp = grip_points({"steer": K.steer})
        grip = 0.0
        pmin, ylo, yhi = 1e9, 1e9, -1e9
        for _, w in K.frames("finish_lose"):
            mm = mmul(w[ks], inv)
            for side in (".L", ".R"):
                grip = max(grip, dist(K.joint_pos("fingers" + side, w), mpoint(mm, gp[side])))
            pd, yw = head_turn(K, w)
            pmin, ylo, yhi = min(pmin, pd), min(ylo, yw), max(yhi, yw)
        ok = ok and grip <= 1.5 and pmin >= 10.0 and (yhi - ylo) >= 20.0
        msgs.append("lose: grip %s u, head down min %s deg, shake %s deg" % (fnum(grip), fnum(pmin), fnum(yhi - ylo)))
    return ck(ok, "; ".join(msgs), "win: hands >= head joint; lose: grip <= 1.5 u, head down >= 10 deg, shake >= 20 deg")


# ---------------------------------------------------------------- K6 criteria

def tri_count(K_or_model, nodes):
    m = K_or_model
    n = 0
    for ni in nodes:
        n += m.mesh(m.nodes[ni]["mesh"])["ntri"]
    return n


def part_data(m, nodes):
    """World vertex positions (u), colour bytes, triangles of mesh nodes."""
    pos, col = [], []
    tri = 0
    for ni in nodes:
        md = m.mesh(m.nodes[ni]["mesh"])
        w = m.rest_world[ni]
        pos.extend(mpoint(w, p) for p in md["pos"])
        if md["color"]:
            for c in md["color"]:
                if c is None:
                    continue
                # RGB as sRGB bytes and alpha as a byte; COLOR_0 as VEC3 has alpha 1 (glTF), so a VEC3 file and
                # its VEC4 re-export (Blender writes VEC4) compare equal
                col.append(tuple(lin_to_srgb_byte(x) for x in c[:3]) + (int(round((c[3] if len(c) > 3 else 1.0) * 255)),))
        tri += md["ntri"]
    return pos, col, tri


def c6_6(K, kart_model):
    if kart_model is None:
        return skip("no --kart")
    parts = K.kart_part_nodes(False)
    km = kart_model
    kparts = [i for i in km.order if "mesh" in km.nodes[i]]
    dp, dc, dt = part_data(K.m, parts)
    kp, kc, kt = part_data(km, kparts)
    msgs = []
    ok = dt == kt
    msgs.append("tri %d/%d" % (kt, dt))
    maxd = 1e9
    if len(dp) == len(kp):
        key = lambda p: (round(p[0], 3), round(p[1], 3), round(p[2], 3))  # noqa: E731
        a, b = sorted(dp, key=key), sorted(kp, key=key)
        maxd = max([dist(p, q) for p, q in zip(a, b)] or [0.0]) / S
        ok = ok and maxd <= 1e-5
    else:
        ok = False
    msgs.append("pos %s m (%d/%d)" % ("%.2e" % maxd if maxd < 1e8 else "count differs", len(kp), len(dp)))
    cok = Counter(dc) == Counter(kc)
    ok = ok and cok
    msgs.append("colors %s" % ("equal" if cok else "DIFFER"))
    di, ki = Counter(K.m.images()), Counter(km.images())
    miss = sum(1 for h in ki if h not in di)
    ok = ok and miss == 0
    msgs.append("images %d/%d matched" % (len(ki) - miss, len(ki)))
    return ck(ok, "; ".join(msgs), "tri equal, pos <= 1e-5 m, colour bytes equal, pixels equal")


def c6_7(K):
    wheels = K.info.get("wheels")
    if not wheels:
        return skip("kit-info has no wheels")
    radius = K.info.get("wheel_radius")
    worst, worst_r = 0.0, 0.0
    missing = []
    for name, want in sorted(wheels.items()):
        ni = K.m.names.get(name)
        if ni is None:
            missing.append(name)
            continue
        w = K.m.rest_world[ni]
        worst = max(worst, dist((w[12], w[13], w[14]), want))
        if radius and "mesh" in K.m.nodes[ni]:
            sy = math.sqrt(w[4] ** 2 + w[5] ** 2 + w[6] ** 2)
            sz = math.sqrt(w[8] ** 2 + w[9] ** 2 + w[10] ** 2)
            rr = max(math.hypot(sy * p[1], sz * p[2]) for p in K.m.mesh(K.m.nodes[ni]["mesh"])["pos"])
            worst_r = max(worst_r, abs(rr - radius) / radius)
    ok = not missing and worst <= 0.1 and worst_r <= 0.01
    return ck(ok, "pos %s u, radius dev %.2f %%" % (fnum(worst), worst_r * 100), "<= 0.1 u, radius +-1 %",
              ("missing: " + ",".join(missing)) if missing else "", value=[worst, worst_r])


def c6_8(K):
    total = tri_count(K.m, K.m.mesh_nodes(K.m.order))
    return ck(total <= 64000, str(total), "<= 64000", "target <= 40000: %s" % ("yes" if total <= 40000 else "no"),
              value=total)


def c6_9(K, vcol=None):
    mats = [mt.get("name", "") for mt in K.m.g.get("materials", [])]
    if not mats:
        return ("FAIL", "no materials", "name ends with ' [vcol <m>]'", "", None)
    modes = set()
    bad = []
    for n in mats:
        mm = re.search(r" \[vcol ([A-Za-z]+)\]$", n)
        if mm:
            modes.add(mm.group(1))
        else:
            bad.append(n)
    ok = not bad and len(modes) == 1
    if ok and vcol and vcol not in modes:
        ok = False
    return ck(ok, "%d materials, mode %s" % (len(mats), ",".join(sorted(modes)) or "-"), "all end ' [vcol <m>]', one m" +
              (" == " + vcol if vcol else ""), ("no suffix: " + "; ".join(bad)) if bad else "")


# ---------------------------------------------------------------- runner

ALL = [("3.1", c3_1), ("3.2", c3_2), ("3.3", c3_3), ("3.4", c3_4), ("3.5", c3_5), ("3.6", c3_6), ("3.7", c3_7),
       ("3.8", c3_8), ("3.9", c3_9), ("3.10", c3_10), ("3.11", c3_11),
       ("4.2", c4_2), ("4.3", c4_3), ("4.4", c4_4), ("4.5", c4_5), ("4.6", c4_6), ("4.7", c4_7), ("4.8", c4_8),
       ("4.9", c4_9), ("4.10", c4_10), ("4.11", c4_11), ("4.12", c4_12), ("4.13", c4_13), ("4.14", c4_14),
       ("4.15", c4_15), ("4.16", c4_16), ("6.6", c6_6), ("6.7", c6_7), ("6.8", c6_8), ("6.9", c6_9)]
NEEDS_PUPPET = ({c for c, _ in ALL if c.startswith("3.") or c.startswith("4.")})


def run_measure(K, kart_model=None, vcol=None, only=None):
    out = []
    for cid, fn in ALL:
        if only and cid.split(".")[0] not in only:
            continue
        try:
            if cid in NEEDS_PUPPET and not K.has_puppet:
                st = NO_PUPPET
            elif cid == "6.6":
                st = fn(K, kart_model)
            elif cid == "6.9":
                st = fn(K, vcol)
            else:
                st = fn(K)
        except Exception as e:  # a measuring error is a failed criterion, never a crash
            st = ("FAIL", "-", "-", "error: %s: %s" % (type(e).__name__, e), None)
        out.append(Res(cid, st[0], st[1], st[2], st[3], st[4]))
    return out


def summarize(results):
    p = sum(1 for r in results if r.status == "PASS")
    f = sum(1 for r in results if r.status == "FAIL")
    s = sum(1 for r in results if r.status == "SKIP")
    return p, f, s


def cmd_measure(args):
    try:
        with open(args.glb, "rb") as fh:
            data = fh.read()
        with open(args.kit_info, "r", encoding="utf-8") as fh:
            info = json.load(fh)
        kart = None
        if args.kart:
            with open(args.kart, "rb") as fh:
                kart = Model(fh.read())
        K = Kit(data, info)
    except (OSError, ValueError, MeasureError, KeyError) as e:
        print("creator-kit-measure: cannot read input: %s" % e, file=sys.stderr)
        return 2
    only = None
    if args.only:
        only = set()
        for tok in args.only.split(","):
            tok = tok.strip().upper()
            if tok not in ("K3", "K4", "K6"):
                print("creator-kit-measure: --only takes K3,K4,K6", file=sys.stderr)
                return 2
            only.add(tok[1:])
    res = run_measure(K, kart, args.vcol, only)
    for r in res:
        print(r.line())
    p, f, s = summarize(res)
    print("creator-kit-measure: pass %d fail %d skip %d" % (p, f, s))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump({"results": [r.as_dict() for r in res], "summary": {"fail": f, "pass": p, "skip": s}}, fh,
                      sort_keys=True, indent=1)
            fh.write("\n")
    return 0 if f == 0 else 1


# ================================================================ selftest (GLBs built in memory)

class GB:
    """Minimal glTF builder: buffer, accessors, nodes, meshes, skins, animations."""

    def __init__(self):
        self.bin = bytearray()
        self.views, self.accs = [], []
        self.g = {"asset": {"version": "2.0"}, "nodes": [], "meshes": [], "materials": [], "skins": [],
                  "animations": [], "scenes": [{"nodes": []}], "scene": 0}

    def acc(self, data, ctype, typ, normalized=False):
        code, size = CT[ctype]
        nc = NC[typ]
        flat = []
        for d in data:
            flat.extend(d if isinstance(d, (tuple, list)) else [d])
        while len(self.bin) % 4:
            self.bin.append(0)
        off = len(self.bin)
        self.bin += struct.pack("<%d%s" % (len(flat), code), *flat)
        self.views.append({"buffer": 0, "byteOffset": off, "byteLength": len(self.bin) - off})
        a = {"bufferView": len(self.views) - 1, "componentType": ctype, "count": len(data), "type": typ}
        if normalized:
            a["normalized"] = True
        self.accs.append(a)
        return len(self.accs) - 1

    def node(self, name, t=(0, 0, 0), q=(0, 0, 0, 1), s=(1, 1, 1), **kw):
        nd = {"name": name}
        if tuple(t) != (0, 0, 0):
            nd["translation"] = list(t)
        if tuple(q) != (0, 0, 0, 1):
            nd["rotation"] = list(q)
        if tuple(s) != (1, 1, 1):
            nd["scale"] = list(s)
        nd.update(kw)
        self.g["nodes"].append(nd)
        return len(self.g["nodes"]) - 1

    def child(self, parent, child):
        self.g["nodes"][parent].setdefault("children", []).append(child)

    def mesh(self, name, pos, idx, mat=None, joints=None, weights=None, color=None):
        at = {"POSITION": self.acc(pos, 5126, "VEC3")}
        if joints is not None:
            at["JOINTS_0"] = self.acc(joints, 5121, "VEC4")
            at["WEIGHTS_0"] = self.acc(weights, 5126, "VEC4")
        if color is not None:
            at["COLOR_0"] = self.acc(color, 5121, "VEC4", normalized=True)
        pr = {"attributes": at, "indices": self.acc([(i,) for i in idx], 5125, "SCALAR")}
        if mat is not None:
            pr["material"] = mat
        self.g["meshes"].append({"name": name, "primitives": [pr]})
        return len(self.g["meshes"]) - 1

    def material(self, name):
        self.g["materials"].append({"name": name})
        return len(self.g["materials"]) - 1

    def anim(self, name, tracks):
        samplers, chans = [], []
        for node, path, times, vals, interp in tracks:
            si = len(samplers)
            samplers.append({"input": self.acc([(t,) for t in times], 5126, "SCALAR"),
                             "output": self.acc(vals, 5126, "VEC4" if path == "rotation" else "VEC3"),
                             "interpolation": interp})
            chans.append({"sampler": si, "target": {"node": node, "path": path}})
        self.g["animations"].append({"name": name, "samplers": samplers, "channels": chans})

    def glb(self):
        g = dict(self.g)
        g["buffers"] = [{"byteLength": len(self.bin)}]
        g["bufferViews"], g["accessors"] = self.views, self.accs
        for k in ("skins", "animations", "materials", "meshes"):
            if not g[k]:
                del g[k]
        js = json.dumps(g, sort_keys=True).encode("utf-8")
        js += b" " * (-len(js) % 4)
        b = bytes(self.bin) + b"\0" * (-len(self.bin) % 4)
        total = 12 + 8 + len(js) + 8 + len(b)
        return (struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<II", len(js), 0x4E4F534A) + js +
                struct.pack("<II", len(b), 0x004E4942) + b)


STD_INFO = {"kit": 1, "source": "stand-in", "units_per_metre": 64, "seat": [0, 10.2, 0.9],
            "steer": {"center": [0, 38.0, 24.0], "axis": [0, -0.316, 0.949], "normal": [0, -0.316, 0.949],
                      "radius": 10.5},
            "kart_box": [-27.9, 5.6, -54.1, 28.3, 52.1, 58.3], "floor": STD_FLOOR,
            "wheels": {"wheel_fl": [36, 16, 49.75], "wheel_fr": [-36, 16, 49.75], "wheel_rl": [36, 16, -24],
                       "wheel_rr": [-36, 16, -24]}, "wheel_radius": 16, "wheel_half_width": 6}

# test kit: joint heads (u) of the left side; the right side is mirrored
_L = {"shoulder": (3, 49, -1), "upper_arm": (12, 49, -1), "forearm": (16, 44, 10), "hand": (10, 42, 22),
      "fingers": None, "thumb": (9.5, 45, 24), "thigh": (6.5, 16, 1), "shin": (8, 26, 26), "foot": (8, 11, 42),
      "toe": (8, 8, 50)}
# box centre + half size per figure bone (left side, central bones x = 0)
_BOX = {"pelvis": (0, 13.2, 0.9, 3, 3, 3), "spine": (0, 27, 0, 3, 5, 3), "chest": (0, 42, -1, 5, 8, 3),
        "neck": (0, 53, -1, 1.5, 2, 1.5), "head": (0, 75, 0, 9, 10, 9),
        "shoulder": (7.5, 49, -1, 3, 1.5, 1.5), "upper_arm": (14, 46, 2, 2, 2, 2), "forearm": (15, 44, 10, 2, 2, 2),
        "hand": (11, 42, 22, 2, 2, 2), "fingers": (9, 43, 25.5, 0.5, 0.5, 0.5), "thumb": (9.5, 45, 24, 0.5, 0.5, 0.5),
        "thigh": (7, 20, 13, 2, 2, 2), "shin": (8, 19, 34, 2, 2, 2), "foot": (8, 9.5, 46, 2, 1.5, 4),
        "toe": (8, 9.5, 52.5, 2, 1.5, 2)}
_PARENT = {"pelvis": "root", "spine": "pelvis", "chest": "spine", "neck": "chest", "head": "neck",
           "shoulder": "chest", "upper_arm": "shoulder", "forearm": "upper_arm", "hand": "forearm",
           "fingers": "hand", "thumb": "hand", "thigh": "pelvis", "shin": "thigh", "foot": "shin", "toe": "foot"}


def _heads(info):
    h = {"root": (0, 0, 0), "kart": (0, 6, 2.1), "kart_steer": tuple(info["steer"]["center"]), "pelvis": (0, 16, 1),
         "spine": (0, 22, 0), "chest": (0, 33, -1), "neck": (0, 51, -1), "head": (0, 55, 0)}
    gp = grip_points(info)
    for side, sx in ((".L", 1), (".R", -1)):
        for b, p in _L.items():
            h[b + side] = gp[side] if b == "fingers" else (p[0] * sx, p[1], p[2])
    return h


def _box(c, flip):
    cx, cy, cz, hx, hy, hz = c
    cx = -cx if flip else cx
    return [(cx + sx * hx, cy + sy * hy, cz + sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]


BOX_TRIS = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6),
            (0, 2, 6), (0, 6, 4), (1, 5, 7), (1, 7, 3)]


def build_kit(o=None):
    """A small valid creator-kit-demo-local.glb (+ optional kart GLB).  o = option dict for mutations."""
    o = dict(o or {})
    info = o.get("info", STD_INFO)
    gb = GB()
    mat_p = gb.material(o.get("mat_puppet", "puppet [vcol psx]"))
    mat_k = gb.material(o.get("mat_kart", "kart [vcol psx]"))
    heads = _heads(info)
    names = ["root", "kart", "kart_steer", "pelvis", "spine", "chest", "neck", "head"] + \
        [b + s for s in (".L", ".R") for b in _L]
    par = {"kart": "root", "kart_steer": "kart"}
    for n in names:
        b = n[:-2] if n.endswith((".L", ".R")) else n
        if b in _PARENT:
            par[n] = _PARENT[b] + (n[-2:] if n.endswith((".L", ".R")) and _PARENT[b] not in ("root", "chest", "pelvis", "spine", "neck") else "")
    for n in names:  # shoulders hang from chest, thighs from pelvis, no side suffix on central parents
        pass
    nid = {}
    for n in names:
        p = par.get(n)
        hp = heads[p] if p else (0, 0, 0)
        loc = tuple((heads[n][a] - hp[a]) / S for a in range(3))
        nid[n] = gb.node(n, t=loc)
        if p:
            gb.child(nid[p], nid[n])
    # kart meshes (children of kart; world = u positions)
    kb = info["kart_box"]
    kv = [(kb[0] if i & 4 == 0 else kb[3], kb[1] if i & 2 == 0 else kb[4], kb[2] if i & 1 == 0 else kb[5]) for i in range(8)]
    kcol = [(0.2, 0.3, 0.4, 1.0)] * 8
    if o.get("kart_color_diff"):
        kcol = [(0.2, 0.3, 0.45, 1.0)] + kcol[1:]
    kmesh = gb.mesh("kart_body", [tuple(c / S for c in v) for v in kv], [i for t in BOX_TRIS for i in t], mat_k,
                    color=[tuple(int(round(x * 255)) for x in c) for c in kcol])
    kn = gb.node("kart_body", t=(0, -6 / S, -2.1 / S), mesh=kmesh)
    gb.child(nid["kart"], kn)
    ring = [(0.0, 0.0, 0.0), (0.02, 0.0, 0.0), (0.0, 0.02, 0.0), (0.0, 0.0, 0.02)]
    rmesh = gb.mesh("kart_steer_wheel", ring, [0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2], mat_k)
    rn = gb.node("kart_steer_wheel", mesh=rmesh)
    gb.child(nid["kart_steer"], rn)
    wr = info["wheel_radius"]
    wmesh = None
    pts, tris = [], []
    for k in range(8):
        a = 2 * math.pi * k / 8
        for sx in (-0.5, 0.5):
            pts.append((sx * 12 / S, wr * math.cos(a) / S, wr * math.sin(a) / S))
    for k in range(8):
        a0, a1 = 2 * k, 2 * ((k + 1) % 8)
        tris += [a0, a0 + 1, a1, a1, a0 + 1, a1 + 1]
    wmesh = gb.mesh("wheel", pts, tris, mat_k)
    for wn, wp in sorted(info["wheels"].items()):
        p = list(wp)
        if o.get("wheel_off") and wn == "wheel_fl":
            p[0] += 1.0
        wnode = gb.node(wn, t=tuple((p[a] - heads["kart"][a]) / S for a in range(3)), mesh=wmesh)
        gb.child(nid["kart"], wnode)
    # puppet
    pos, joints, weights, idx = [], [], [], []
    base_of = {}
    jidx = {n: k for k, n in enumerate(names)}
    for n in names:
        b = n[:-2] if n.endswith((".L", ".R")) else n
        if b not in _BOX:
            continue
        flip = n.endswith(".R")
        for rep in range(o.get("rep", 1)):  # rep > 1: identical copies of the box (stress / speed test)
            if rep == 0:
                base_of[n] = len(pos)
            pos.extend(_box(_BOX[b], flip))
            for _ in range(8):
                joints.append((jidx[n], 0, 0, 0))
                weights.append((1.0, 0.0, 0.0, 0.0))
            idx.extend(len(pos) - 8 + i for t in BOX_TRIS for i in t)
    for n in names:
        if n in base_of and n in par and par[n] in base_of:
            if o.get("cut") == n:
                continue
            idx.extend([base_of[par[n]], base_of[n], base_of[n] + 1])
    for k in o.get("asym", []):
        pos[k] = (pos[k][0] + 3.0, pos[k][1], pos[k][2])
    if o.get("bad_weight"):
        weights[0] = (0.9, 0.0, 0.0, 0.0)
    pm = gb.mesh("puppet", [tuple(c / S for c in p) for p in pos], idx, mat_p, joints=joints, weights=weights)
    ibm = []
    for n in names:
        h = heads[n]
        ibm.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -h[0] / S, -h[1] / S, -h[2] / S, 1))
    gb.g["skins"].append({"joints": [nid[n] for n in names], "inverseBindMatrices": gb.acc(ibm, 5126, "MAT4")})
    pn = gb.node("puppet", mesh=pm, skin=0)
    gb.g["scenes"][0]["nodes"] = [nid["root"], pn]
    # clips
    custom = o.get("clips", {})
    durs = dict(CLIP_DUR)
    durs.update(o.get("durs", {}))
    steer_deg = o.get("steer_deg", 45.0)
    ax = vnorm(info["steer"]["axis"])
    for cn in CLIP_NAMES:
        if cn in o.get("skip_clips", ()):
            continue
        dur = durs[cn]
        n = int(round(dur * 30))
        times = [i / 30.0 for i in range(n + 1)]
        amp = 0.0 if o.get("flat") else (3.0 if cn == "idle" else 20.0)
        ph = 2 * math.pi
        spine = [qaxis_angle((1, 0, 0), amp * math.sin(ph * i / n)) for i in range(n + 1)]
        tracks = [(nid["spine"], "rotation", times, spine, "LINEAR")]
        if cn in ("steer_left", "steer_right"):
            sgn = -1.0 if cn == "steer_left" else 1.0
            if o.get("wrong_sign"):
                sgn = -sgn
            ang = [sgn * steer_deg * i / n for i in range(n + 1)]
            if o.get("nonmono") and cn == "steer_left":
                ang = [sgn * steer_deg * (1.0 - abs(2.0 * i / n - 1.0)) for i in range(n + 1)]
            tracks.append((nid["kart_steer"], "rotation", times, [qaxis_angle(ax, a) for a in ang], "LINEAR"))
        if cn in STANDING:
            dy = -8.0 - (o.get("below", 0.0) if cn == "podium_2" else 0.0)
            tracks.append((nid["pelvis"], "translation", times[:1] + times[-1:], [(0.0, (16 + dy) / S, 1.0 / S)] * 2,
                           "LINEAR"))
        if cn in o.get("last_diff", ()):
            q = spine[:-1] + [qaxis_angle((1, 0, 0), 15.0)]
            tracks[0] = (nid["spine"], "rotation", times, q, "LINEAR")
        gb.anim(cn, [(a, b, c, d, e) for a, b, c, d, e in tracks])
    data = gb.glb()
    kart = None
    if o.get("with_kart"):
        kb2 = GB()
        m2 = kb2.material("kart [vcol psx]")
        mesh2 = kb2.mesh("kart_body", [tuple(c / S for c in v) for v in kv], [i for t in BOX_TRIS for i in t], m2,
                         color=[tuple(int(round(x * 255)) for x in c) for c in kcol]
                         if not o.get("kart_glb_color_diff") else [(0, 0, 0, 255)] * 8)
        a = kb2.node("kart_body", mesh=mesh2)
        mesh3 = kb2.mesh("kart_steer_wheel", ring, [0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2], m2)
        b2 = kb2.node("kart_steer_wheel", t=tuple(c / S for c in info["steer"]["center"]), mesh=mesh3)
        kb2.g["scenes"][0]["nodes"] = [a, b2]
        kart = kb2.glb()
    return data, kart


# ---- selftest cases

def _kit(o=None, with_info=None):
    data, kart = build_kit(o)
    info = (o or {}).get("info", STD_INFO)
    return Kit(data, info), kart


def selftest():
    results = []

    def case(name, fn):
        try:
            ok, detail = fn()
        except Exception as e:  # noqa: BLE001
            ok, detail = False, "exception %s: %s" % (type(e).__name__, e)
        results.append(ok)
        print(("ok " if ok else "FAIL ") + name + ("" if ok else "  [" + detail + "]"))

    def near(a, b, tol=1e-6):
        return abs(a - b) <= tol

    # 1 slerp mid of a 90 degree key pair = 45 degrees
    def t_slerp():
        a = qaxis_angle((0, 0, 1), 0.0)
        b = qaxis_angle((0, 0, 1), 90.0)
        m = sample_track([0.0, 1.0], [a, b], "LINEAR", "rotation", 0.5)
        ang = qangle(a, m)
        return near(ang, 45.0, 1e-6), "angle %.9f" % ang
    case("slerp mid of 90 deg keys = 45 deg", t_slerp)

    # 2 shortest path (negated key)
    def t_short():
        a = qaxis_angle((0, 0, 1), 0.0)
        b = tuple(-x for x in qaxis_angle((0, 0, 1), 90.0))
        m = sample_track([0.0, 1.0], [a, b], "LINEAR", "rotation", 0.5)
        return near(qangle(a, m), 45.0, 1e-6), "angle %.6f" % qangle(a, m)
    case("slerp takes the shortest path", t_short)

    # 3 STEP + clamp + cubicspline value element
    def t_step():
        ts = [0.0, 1.0, 2.0]
        vs = [(0, 0, 0), (1, 1, 1), (2, 2, 2)]
        a = sample_track(ts, vs, "STEP", "translation", 0.99)
        b = sample_track(ts, vs, "STEP", "translation", 1.0)
        c = sample_track(ts, vs, "LINEAR", "translation", 5.0)
        cub = [(9, 9, 9), (0, 0, 0), (9, 9, 9), (9, 9, 9), (4, 4, 4), (9, 9, 9), (9, 9, 9), (8, 8, 8), (9, 9, 9)]
        d = sample_track(ts, cub, "CUBICSPLINE", "translation", 1.5)
        return a == (0, 0, 0) and b == (1, 1, 1) and c == (2, 2, 2) and near(d[0], 6.0), repr((a, b, c, d))
    case("STEP holds, clamps at the end, CUBICSPLINE value element", t_step)

    # 4 world matrix of a 3-node chain
    def t_chain():
        g = GB()
        h = math.sqrt(0.5)
        a = g.node("A", t=(1, 0, 0))
        b = g.node("B", t=(1, 0, 0), q=(0, 0, h, h))
        c = g.node("C", t=(1, 0, 0))
        g.child(a, b)
        g.child(b, c)
        g.g["scenes"][0]["nodes"] = [a]
        m = Model(g.glb())
        w = m.rest_world[c]
        return near(w[12], 128, 1e-3) and near(w[13], 64, 1e-3) and near(w[14], 0, 1e-3), repr(w[12:15])
    case("world matrix of a 3-node chain", t_chain)

    # 5 skinning of one vertex by 2 joints, 0.5/0.5
    def t_skin():
        g = GB()
        j0 = g.node("j0", t=(2, 0, 0))
        j1 = g.node("j1", t=(0, 4, 0))
        pm = g.mesh("puppet", [(0, 0, 0), (0, 0, 0), (0, 0, 0)], [0, 1, 2], None,
                    joints=[(0, 1, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0)],
                    weights=[(0.5, 0.5, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)])
        g.g["skins"].append({"joints": [j0, j1]})
        pn = g.node("puppet", mesh=pm, skin=0)
        g.g["scenes"][0]["nodes"] = [j0, j1, pn]
        K = Kit(g.glb(), STD_INFO)
        p = K.skin_pts([0, 1, 2], K.m.rest_world)
        return near(p[0][0], 64, 1e-3) and near(p[0][1], 128, 1e-3) and near(p[1][0], 128, 1e-3) and near(p[2][1], 256, 1e-3), repr(p)
    case("skinning one vertex by 2 joints 0.5/0.5 = midpoint", t_skin)

    # 6 grip formula
    def t_grip():
        gp = grip_points(STD_INFO)
        ok = near(gp[".L"][0], 9.09, 0.01) and near(gp[".L"][1], 42.98, 0.01) and near(gp[".L"][2], 25.66, 0.01)
        return ok and near(gp[".R"][0], -9.09, 0.01), repr(gp)
    case("grip point standard L = (9.09, 42.98, 25.66)", t_grip)

    # 7 baseline kit passes the criteria it is built to pass
    base_pass = ["3.1", "3.2", "3.3", "3.4", "3.5", "3.6", "3.7", "3.8", "3.9", "3.10", "4.2", "4.3", "4.4", "4.5",
                 "4.8", "4.9", "4.11", "4.12", "4.13", "4.14", "4.15", "6.7", "6.8", "6.9"]
    cache = {}

    def run(o, key):
        if key not in cache:
            K, kart = _kit(o)
            cache[key] = ({r.cid: r for r in run_measure(K, Model(kart) if kart else None, "psx")}, K)
        return cache[key]

    def t_base():
        res, _ = run({}, "base")
        bad = [c for c in base_pass if res[c].status != "PASS"]
        return not bad, "; ".join(res[c].line() for c in bad)
    case("baseline kit: %d criteria PASS" % len(base_pass), t_base)

    def expect(key, o, cid, status):
        def f():
            res, _ = run(o, key)
            return res[cid].status == status, res[cid].line()
        return f

    case("last frame differs -> 4.3 FAIL", expect("last", {"last_diff": ["idle"]}, "4.3", "FAIL"))
    case("non-monotone steer -> 4.4 FAIL", expect("nonmono", {"nonmono": True}, "4.4", "FAIL"))
    case("steer sign wrong -> 4.5 FAIL", expect("sign", {"wrong_sign": True}, "4.5", "FAIL"))
    case("material name without suffix -> 6.9 FAIL", expect("mat", {"mat_kart": "kart"}, "6.9", "FAIL"))
    case("mixed vcol modes -> 6.9 FAIL", expect("mat2", {"mat_kart": "kart [vcol gltf]"}, "6.9", "FAIL"))
    case("mirror symmetry broken -> 3.10 FAIL", expect("asym", {"asym": [0, 8, 16, 24, 32, 40, 48, 56]}, "3.10", "FAIL"))
    case("mirror symmetry intact -> 3.10 PASS", expect("base", {}, "3.10", "PASS"))
    case("two components -> 3.9 FAIL", expect("cut", {"cut": "head"}, "3.9", "FAIL"))
    case("duration off by 0.2 s -> 4.2 FAIL", expect("dur", {"durs": {"steer_left": 0.7}}, "4.2", "FAIL"))
    case("missing clip -> 4.2 FAIL", expect("miss", {"skip_clips": ["menu"]}, "4.2", "FAIL"))
    case("finish clips with the arms down and no head shake -> 4.16 FAIL", expect("base", {}, "4.16", "FAIL"))
    case("no finish clips -> 4.16 SKIP", expect("nofin", {"skip_clips": ["finish_win", "finish_lose"]}, "4.16", "SKIP"))
    case("podium foot 1 u below ground -> 4.9 FAIL", expect("below", {"below": 1.0}, "4.9", "FAIL"))
    case("weights not summing to 1 -> 3.8 FAIL", expect("bw", {"bad_weight": True}, "3.8", "FAIL"))
    case("wheel 1 u off -> 6.7 FAIL", expect("wh", {"wheel_off": True}, "6.7", "FAIL"))
    case("hands do not follow the turning ring -> 4.7 FAIL", expect("base", {}, "4.7", "FAIL"))
    case("hands rest on the ring, nothing moves -> 4.7 PASS", expect("nosteer", {"steer_deg": 0.0, "flat": True}, "4.7", "PASS"))

    def t_311():
        res, _ = run({}, "base")
        v = res["3.11"].value
        return res["3.11"].status == "FAIL" and v is not None and near(v, 77.0, 0.01), res["3.11"].line()
    case("podium_2 frame 0 height = 77 -> 3.11 FAIL (known value)", t_311)

    def t_66():
        o = {"with_kart": True}
        res, _ = run(o, "k66")
        res2, _ = run({"with_kart": True, "kart_glb_color_diff": True}, "k66b")
        return res["6.6"].status == "PASS" and res2["6.6"].status == "FAIL", res["6.6"].line() + " | " + res2["6.6"].line()
    case("kart 1:1: equal PASS, colour bytes changed FAIL (6.6)", t_66)

    def t_skip():
        res, _ = run({}, "base")
        return res["6.6"].status == "SKIP", res["6.6"].line()
    case("6.6 without --kart is SKIP", t_skip)

    def t_det():
        a, _ = run({}, "base")
        K, _k = _kit({})
        b = {r.cid: r for r in run_measure(K, None, "psx")}
        return all(a[c].line() == b[c].line() for c in a), "lines differ between two runs"
    case("deterministic output", t_det)

    def t_json():
        K, _k = _kit({})
        r = run_measure(K, None, "psx")
        s1 = json.dumps([x.as_dict() for x in r], sort_keys=True)
        return len(r) == 30 and "3.1" in s1, "len %d" % len(r)
    case("30 criteria, JSON serialisable", t_json)

    bad = results.count(False)
    print("creator-kit-measure selftest: %d ok, %d FAIL" % (len(results) - bad, bad))
    return 0 if bad == 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(prog="creator_kit_measure.py", description="Creator Kit measuring tool (K3, K4, K6)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure", help="measure a creator-kit-demo-local.glb")
    m.add_argument("glb")
    m.add_argument("--kit-info", required=True)
    m.add_argument("--kart", default=None)
    m.add_argument("--vcol", choices=["psx", "gltf", "off"], default=None)
    m.add_argument("--json", default=None)
    m.add_argument("--only", default=None, help="comma list of K3,K4,K6")
    sub.add_parser("selftest", help="run the built-in checks")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    if args.cmd == "selftest":
        return selftest()
    return cmd_measure(args)


if __name__ == "__main__":
    sys.exit(main())
