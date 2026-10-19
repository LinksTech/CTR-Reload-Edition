# Creator Kit puppet: a neutral articulated mannequin and its skeleton, seated
# in the kart in its bind pose. Everything here is made from code (simple
# capsules, rounded boxes and balls) - no game data - and released under CC0
# (templates/LICENSE).
#
# Used by templates/make_creator_kit.py and templates/creator_kit_clips.py:
#
#   grip_points(info)                         where the hands hold the wheel
#   bone_table(info)                          the 28 bones (26 figure + kart + kart_steer)
#   build_rig(collection, bones)              the armature "creator_rig"
#   build_puppet(collection, rig, bones, vcol_mode, style)   the mesh "puppet"
#
# Three styles, one per way of the kit (the same skeleton in each):
#   vanilla   for the classic model (rldpack make-char): 559 triangles of
#             chunky pieces (no piece thinner than the packing step of the
#             classic model), a 64 x 64 colour atlas, PS1 vertex colours in
#             9 steps - at most 45 colours (STYLE_NUMBERS)
#   lowpoly   the puppet of before: 5568 triangles, the same 64 x 64 atlas
#   hires     the same shapes three times as round and twice as fine along
#             (about 33000 triangles), a 1024 x 1024 atlas with a cell per
#             piece and a normal map made here (a quilted surface, ribbed
#             bands)
#
# `info` is the parsed kit-info.json. Only its "seat" and "steer" are read;
# a missing key falls back to the standard value.
#
# Units and axes. Model units "u": 64 u = 1 m. glTF axes: x = kart LEFT,
# y = up, z = kart FRONT, origin on the ground under the kart. Blender:
# X = x / 64, Y = -z / 64, Z = y / 64 (the nose toward -Y). Left = ".L" = +x,
# right = ".R" = -x. Same numbers, same mesh: nothing here is random. The
# textures use only + - * / and the square root (exactly rounded in IEEE
# arithmetic), so they have the same bytes on every PC.

import math
import struct
import zlib

import bpy
import numpy as np
from mathutils import Vector

UNIT = 64.0


def _unit(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return [v[0] / n, v[1] / n, v[2] / n]


SEAT_STD = (0.0, 10.2, 0.9)
_N_STD = _unit((0.0, -0.316, 0.949))
STEER_STD = {"center": [0.0, 38.0, 24.0], "axis": list(_N_STD), "normal": list(_N_STD), "radius": 10.5}

_ARMS = ("shoulder", "upper_arm", "forearm", "hand", "fingers", "thumb")
_LEGS = ("thigh", "shin", "foot", "toe")
FIGURE_BONES = (
    ["root", "pelvis", "spine", "chest", "neck", "head"]
    + [n + s for s in (".L", ".R") for n in _ARMS + _LEGS]
)

_ATLAS = 64
_FIELDS = {  # name: (x0, y0, x1, y1) in pixels, row 0 on top
    "body": (0, 0, 32, 32),
    "joint": (32, 0, 64, 32),
    "eyes": (0, 32, 32, 64),
    "bandL": (32, 32, 64, 48),
    "bandR": (32, 48, 64, 64),
}
_FIELD_RGB = {
    "body": (200, 190, 170),
    "joint": (70, 70, 70),
    "eyes": (20, 20, 24),
    "bandL": (60, 170, 80),
    "bandR": (60, 110, 200),
}

# PS1 grey shading, as psx_colors() in tools/hires_glb_driver.py
_PSX_LIGHT = (0.25, -0.35, 1.0)
_PSX_NORMAL_SHARE = 0.7
_PSX_DEAD_ZONE = 0.22
_PSX_SPREAD = 90.0
_PSX_RANGE = (40, 216)


def Bv(p):
    return Vector((p[0] / 64.0, -p[2] / 64.0, p[1] / 64.0))


# ------------------------------------------------------------------ info


def _seat(info):
    s = (info or {}).get("seat")
    return Vector(s if s is not None else SEAT_STD)


def _steer(info):
    s = (info or {}).get("steer") or {}
    center = s.get("center", STEER_STD["center"])
    normal = _unit(s.get("normal", STEER_STD["normal"]))
    axis = _unit(s["axis"]) if "axis" in s else (normal if "normal" in s else list(STEER_STD["axis"]))
    return Vector(center), Vector(axis), Vector(normal), float(s.get("radius", STEER_STD["radius"]))


def grip_points(info):
    # The hand grips on the ring at rest: centre + r * (sin60 * xhat + cos60 * uhat),
    # uhat = the in-plane "up". Standard: L = (9.09, 42.98, 25.66).
    c, _axis, n, r = _steer(info)
    up = Vector((0.0, 1.0, 0.0))
    uhat = (up - n * up.dot(n)).normalized()
    out = {}
    for suffix, sx in ((".L", 1.0), (".R", -1.0)):
        g = c + r * (math.sin(math.radians(60.0)) * Vector((sx, 0.0, 0.0)) + math.cos(math.radians(60.0)) * uhat)
        out[suffix] = (g.x, g.y, g.z)
    return out


def _two_bone(shoulder, wrist, l1, l2, pole):
    d = wrist - shoulder
    dist = min(max(d.length, abs(l1 - l2) + 1e-3), l1 + l2 - 1e-3)
    direction = d.normalized()
    a = (dist * dist + l1 * l1 - l2 * l2) / (2.0 * dist)
    h = math.sqrt(max(l1 * l1 - a * a, 0.0))
    perp = (pole - direction * pole.dot(direction)).normalized()
    return shoulder + direction * a + perp * h


def _t(v):
    return (float(v[0]), float(v[1]), float(v[2]))


def bone_table(info):
    # [(name, head_u, tail_u, parent_or_None, deform)] in the order of INTERFACE.md.
    # Everything but root, kart and kart_steer is shifted by (seat - SEAT_STD)
    # before the arm IK, so the arms are solved to the kit's grips.
    d = _seat(info) - Vector(SEAT_STD)
    c, axis, _n, _r = _steer(info)
    grips = grip_points(info)

    def P(x, y, z):
        return Vector((x, y, z)) + d

    rows = [
        ("root", (0.0, 0.0, 0.0), (0.0, 8.0, 0.0), None, True),
        ("kart", (0.0, 6.0, 2.1), (0.0, 6.0, 22.1), "root", False),
        ("kart_steer", _t(c), _t(c + 6.0 * axis), "kart", False),
        ("pelvis", _t(P(0, 16, 1)), _t(P(0, 22, 0)), "root", True),
        ("spine", _t(P(0, 22, 0)), _t(P(0, 33, -1)), "pelvis", True),
        ("chest", _t(P(0, 33, -1)), _t(P(0, 51, -1)), "spine", True),
        ("neck", _t(P(0, 51, -1)), _t(P(0, 55, 0)), "chest", True),
        ("head", _t(P(0, 55, 0)), _t(P(0, 84, 1)), "neck", True),
    ]
    for suffix, sx in ((".L", 1.0), (".R", -1.0)):
        S = P(12 * sx, 49, -1)
        G = Vector(grips[suffix])
        W = G - 6.0 * Vector((-0.2 * sx, 0.15, 0.97)).normalized()
        E = _two_bone(S, W, 14.0, 13.0, Vector((sx, -1.0, 0.0)).normalized())
        dv = (G - W).normalized()
        T0 = W + 0.4 * (G - W) + Vector((-1.5 * sx, 1.5, 0.0))
        K = P(8 * sx, 26, 26)
        A = P(8 * sx, 11, 42)
        rows += [
            ("shoulder" + suffix, _t(P(3 * sx, 49, -1)), _t(S), "chest", True),
            ("upper_arm" + suffix, _t(S), _t(E), "shoulder" + suffix, True),
            ("forearm" + suffix, _t(E), _t(W), "upper_arm" + suffix, True),
            ("hand" + suffix, _t(W), _t(G), "forearm" + suffix, True),
            ("fingers" + suffix, _t(G), _t(G + 4.0 * dv), "hand" + suffix, True),
            ("thumb" + suffix, _t(T0), _t(T0 + 3.0 * dv), "hand" + suffix, True),
            ("thigh" + suffix, _t(P(6.5 * sx, 16, 1)), _t(K), "pelvis", True),
            ("shin" + suffix, _t(K), _t(A), "thigh" + suffix, True),
            ("foot" + suffix, _t(A), _t(P(8 * sx, 8, 50)), "shin" + suffix, True),
            ("toe" + suffix, _t(P(8 * sx, 8, 50)), _t(P(8 * sx, 8, 54)), "foot" + suffix, True),
        ]
    return rows


# ------------------------------------------------------------------ rig


def build_rig(collection, bones):
    arm = bpy.data.armatures.new("creator_rig")
    obj = bpy.data.objects.new("creator_rig", arm)
    collection.objects.link(obj)
    layer = bpy.context.view_layer
    previous = layer.objects.active
    layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    edit = {}
    for name, head, tail, parent, deform in bones:
        eb = arm.edit_bones.new(name)
        eb.head = Bv(head)
        eb.tail = Bv(tail)
        eb.roll = 0.0
        eb.use_deform = bool(deform)
        if parent is not None:
            eb.parent = edit[parent]
        edit[name] = eb
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in obj.pose.bones:
        pb.rotation_mode = "QUATERNION"
    if previous is not None and previous.name in layer.objects:
        layer.objects.active = previous
    return obj


# ------------------------------------------------------------------ the mesh builder


class _Mesh:
    def __init__(self):
        self.co = []
        self.bone = []
        self.tris = []    # [a, b, c, field, smooth, piece, uvs]: uvs = 3 (u, v) in the piece's own 0..1 square
        self.pieces = []  # the _Seg of every piece, in the order they are made (piece = the index)

    def vert(self, p, bone):
        self.co.append(Vector(p))
        self.bone.append(bone)
        return len(self.co) - 1

    def tri(self, a, b, c, field="body", smooth=True, piece=-1, uvs=None):
        self.tris.append([a, b, c, field, smooth, piece, uvs or [(0.5, 0.5)] * 3])
        return len(self.tris) - 1


class _Seg:
    def __init__(self, bone, mid, piece):
        self.bone = bone
        self.verts = []
        self.tris = []
        self.mid = Vector(mid)
        self.piece = piece
        self.ring_ids = []
        self.v = []            # v of each ring: the length of the profile up to it, 0..1
        self.span_fields = []  # the field of each span between two rings


class _Style:
    """How much finer than written a piece is made: segs x seg_mul around, each span between two rings cut into
    ring_sub (the radius of a new ring on a Catmull-Rom curve through the rings, a straight line next to a pole)."""

    def __init__(self, seg_mul, ring_sub):
        self.seg_mul = seg_mul
        self.ring_sub = ring_sub

    def segs(self, n):
        return n * self.seg_mul

    def refine(self, rings, fields, default):
        n = len(rings)
        out = [rings[0]]
        nf = {}
        for i in range(n - 1):
            t0, r0 = rings[i][0], rings[i][1]
            t1, r1 = rings[i + 1][0], rings[i + 1][1]
            pole = r0 <= 1e-9 or r1 <= 1e-9
            pm = rings[i - 1][1] if i > 0 else r0
            pp = rings[i + 2][1] if i + 2 < n else r1
            for j in range(1, self.ring_sub):
                x = j / float(self.ring_sub)
                t = t0 + (t1 - t0) * x
                if pole:
                    r = r0 + (r1 - r0) * x
                else:
                    r = 0.5 * (2.0 * r0 + (r1 - pm) * x + (2.0 * pm - 5.0 * r0 + 4.0 * r1 - pp) * x * x
                               + (3.0 * r0 - pm - 3.0 * r1 + pp) * x * x * x)
                    r = max(r, 0.25 * min(r0, r1))
                out.append((t, r))
            out.append(rings[i + 1])
            for j in range(self.ring_sub):
                nf[i * self.ring_sub + j] = (fields or {}).get(i, default)
        return out, nf


_HIRES = _Style(3, 2)


def _lathe(m, bone, a, b, rings, segs, sx=1.0, sz=1.0, power=2.0, fields=None, default="body", smooth=True,
           style=None, refine=True):
    # A closed surface along the axis a -> b. rings = [(t, r)] or [(t, r, kx)]: t along the axis
    # from a, r the radius (0 = a pole), kx a factor on the side scale of that ring. The cross-section is a
    # superellipse (power 2 = ellipse) with the scales sx (side) and sz (the other way). style (a _Style)
    # makes it finer; refine=False keeps its rings (a flat disc).
    if style is not None:
        segs = style.segs(segs)
        if refine:
            rings, fields = style.refine(rings, fields, default)
    a = Vector(a)
    w = (Vector(b) - a).normalized()
    up = Vector((0.0, 1.0, 0.0)) if abs(w.y) < 0.9 else Vector((0.0, 0.0, 1.0))
    s = up.cross(w).normalized()
    f = w.cross(s)
    e = 2.0 / power
    cs = []
    for k in range(segs):
        th = 2.0 * math.pi * k / segs
        c, sn = math.cos(th), math.sin(th)
        c = 0.0 if abs(c) < 1e-12 else math.copysign(abs(c) ** e, c)
        sn = 0.0 if abs(sn) < 1e-12 else math.copysign(abs(sn) ** e, sn)
        cs.append((c, sn))
    seg = _Seg(bone, a + w * (0.5 * (Vector(b) - a).length), len(m.pieces))
    m.pieces.append(seg)
    ring_ids = []
    for ring in rings:
        t, r = ring[0], ring[1]
        kx = ring[2] if len(ring) > 2 else 1.0
        if r <= 1e-9:
            i = m.vert(a + w * t, bone)
            seg.verts.append(i)
            ring_ids.append([i])
        else:
            ids = []
            for c, sn in cs:
                ids.append(m.vert(a + w * t + s * (c * r * sx * kx) + f * (sn * r * sz), bone))
            seg.verts.extend(ids)
            ring_ids.append(ids)
    seg.ring_ids = ring_ids
    acc = [0.0]
    for i in range(len(rings) - 1):
        acc.append(acc[-1] + math.sqrt((rings[i + 1][0] - rings[i][0]) ** 2 + (rings[i + 1][1] - rings[i][1]) ** 2))
    seg.v = [x / max(acc[-1], 1e-9) for x in acc]
    fields = fields or {}
    seg.span_fields = [fields.get(i, default) for i in range(len(rings) - 1)]
    first = len(m.tris)
    for i in range(len(rings) - 1):
        R0, R1 = ring_ids[i], ring_ids[i + 1]
        v0, v1 = seg.v[i], seg.v[i + 1]
        fld = fields.get(i, default)
        if len(R0) == 1 and len(R1) == 1:
            continue
        for k in range(segs):
            k1 = (k + 1) % segs
            u0, u1, um = k / float(segs), (k + 1) / float(segs), (k + 0.5) / float(segs)
            if len(R0) == 1:
                m.tri(R0[0], R1[k1], R1[k], fld, smooth, seg.piece, [(um, v0), (u1, v1), (u0, v1)])
            elif len(R1) == 1:
                m.tri(R0[k], R0[k1], R1[0], fld, smooth, seg.piece, [(u0, v0), (u1, v0), (um, v1)])
            else:
                m.tri(R0[k], R0[k1], R1[k1], fld, smooth, seg.piece, [(u0, v0), (u1, v0), (u1, v1)])
                m.tri(R0[k], R1[k1], R1[k], fld, smooth, seg.piece, [(u0, v0), (u1, v1), (u0, v1)])
    seg.tris = list(range(first, len(m.tris)))
    # outward winding: flip the whole piece when its volume comes out negative
    vol = 0.0
    for ti in seg.tris:
        ta, tb, tc = m.tris[ti][:3]
        vol += m.co[ta].dot(m.co[tb].cross(m.co[tc]))
    if vol < 0.0:
        for ti in seg.tris:
            t = m.tris[ti]
            t[1], t[2] = t[2], t[1]
            t[6] = [t[6][0], t[6][2], t[6][1]]
    return seg


def _capsule_rings(length, r0, r1, band=None, caps=(30.0, 60.0)):
    # Hemispherical ends, a straight (tapering) run between. band = (f0, f1)
    # fractions of the run that get a slightly raised ring (a colour band).
    rings = [(0.0, 0.0)]
    for deg in caps:
        p = math.radians(deg)
        rings.append((r0 * (1.0 - math.cos(p)), r0 * math.sin(p)))
    t0, t1 = r0, length - r1
    ts = [t0, 0.5 * (t0 + t1), t1]
    if band is not None:
        ts = [t0, t0 + band[0] * (t1 - t0), t0 + band[1] * (t1 - t0), t1]
    run = []
    for t in ts:
        u = (t - t0) / max(t1 - t0, 1e-9)
        run.append((t, r0 + (r1 - r0) * u))
    band_idx = None
    if band is not None:
        run[1] = (run[1][0], run[1][1] * 1.08)
        run[2] = (run[2][0], run[2][1] * 1.08)
    rings += run
    if band is not None:
        band_idx = len(rings) - 3  # span from run[1] to run[2]
    for deg in reversed(caps):
        p = math.radians(deg)
        rings.append((length - r1 * (1.0 - math.cos(p)), r1 * math.sin(p)))
    rings.append((length, 0.0))
    return rings, band_idx


def _ball_rings(radius):
    rings = []
    for deg in (0.0, 36.0, 72.0, 108.0, 144.0, 180.0):
        p = math.radians(deg)
        rings.append((radius * (1.0 - math.cos(p)), radius * math.sin(p) if 0.0 < deg < 180.0 else 0.0))
    return rings


def _ellipsoid_rings(length, radius, steps=8):
    rings = []
    for i in range(steps + 1):
        p = math.pi * i / steps
        rings.append((0.5 * length * (1.0 - math.cos(p)), radius * math.sin(p) if 0 < i < steps else 0.0))
    return rings


def _weld(m, point, bone, seg_a, seg_b):
    # One shared corner hidden inside both pieces: it joins to the two
    # nearest corners of each piece by a hidden triangle, so the mesh is one piece.
    point = Vector(point)
    c = m.vert(point, bone)
    for seg in (seg_a, seg_b):
        cand = sorted((i for i in seg.verts), key=lambda i: ((m.co[i] - point).length_squared, i))
        p0 = cand[0]
        q = cand[1]
        for q in cand[1:]:
            if (m.co[p0] - point).cross(m.co[q] - point).length > 1e-4:
                break
        m.tri(c, p0, q, "body", False)


def _toward(frm, to, dist):
    return Vector(frm) + (Vector(to) - Vector(frm)).normalized() * dist


def _weld_pole(m, pole, host):
    # The vanilla puppet: the pole of one piece lies inside its neighbour; one hidden triangle from it to the
    # two nearest corners of the neighbour (at least 0.5 u^2 large, so the packing of the classic model keeps
    # it) makes the two one piece.
    point = m.co[pole]
    cand = sorted(host.verts, key=lambda i: ((m.co[i] - point).length_squared, i))
    p0 = cand[0]
    q = cand[1]
    for q in cand[1:]:
        if (m.co[p0] - point).cross(m.co[q] - point).length > 1.0:
            break
    m.tri(pole, p0, q, "body", False)


# ------------------------------------------------------------------ atlas


def _png_bytes(width, height, rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def _atlas_png():
    rows = []
    for y in range(_ATLAS):
        row = []
        for x in range(_ATLAS):
            rgb = _FIELD_RGB["body"]
            for name, (x0, y0, x1, y1) in _FIELDS.items():
                if x0 <= x < x1 and y0 <= y < y1:
                    rgb = _FIELD_RGB[name]
            row.extend(rgb)
        rows.append(row)
    return _png_bytes(_ATLAS, _ATLAS, rows)


def _packed_image(name, width, height, data, colorspace):
    old = bpy.data.images.get(name)
    if old is not None:
        bpy.data.images.remove(old)
    img = bpy.data.images.new(name, width, height, alpha=False)
    img.pack(data=data, data_len=len(data))
    img.source = "FILE"  # read the packed PNG bytes, not the black generated buffer
    img.colorspace_settings.name = colorspace
    return img


def _make_atlas_image():
    return _packed_image("puppet_atlas", _ATLAS, _ATLAS, _atlas_png(), "sRGB")


# The Hi-Res atlas: a cell of _HI_CELL px per piece (8 x 8 cells); the _HI_PAD px at each cell's edge repeat
# its border (no colour of a neighbour bleeds in when the texture is filtered). The hidden weld triangles use
# the last cell.
_HI_ATLAS = 1024
_HI_CELL = 128
_HI_PAD = 4
_HI_COLS = _HI_ATLAS // _HI_CELL
_HI_WELD_CELL = _HI_COLS * _HI_COLS - 1
_HI_TILT = 0.45          # the steepest slope of the normal map's bumps (about 24 degrees)
_HI_QUILT = 8            # bumps of the quilted surface along u and along v of a piece
_HI_RIBS = 12            # ribs of a colour band along v


def _hires_cell(piece):
    cell = piece if 0 <= piece < _HI_WELD_CELL else _HI_WELD_CELL
    return (cell % _HI_COLS) * _HI_CELL, (cell // _HI_COLS) * _HI_CELL


def _hires_uv(piece, uv):
    x0, y0 = _hires_cell(piece)
    inner = float(_HI_CELL - 2 * _HI_PAD)
    return ((x0 + _HI_PAD + uv[0] * inner) / _HI_ATLAS, 1.0 - (y0 + _HI_PAD + (1.0 - uv[1]) * inner) / _HI_ATLAS)


def _hires_pngs(m):
    """(base colour PNG, normal map PNG) of the Hi-Res atlas, both _HI_ATLAS x _HI_ATLAS RGB. The normal map is
    in tangent space (+X along u, +Y along v, as glTF reads it); a body piece is quilted (h = s(u) s(v) with
    s(x) = 4x(1 - x) over each bump), a colour band ribbed (h = s(v)), joints and eyes are smooth."""
    if len(m.pieces) > _HI_WELD_CELL:
        raise RuntimeError("the Hi-Res atlas has %d cells for %d pieces" % (_HI_WELD_CELL, len(m.pieces)))
    base = np.zeros((_HI_ATLAS, _HI_ATLAS, 3), np.float64)
    nrm = np.zeros((_HI_ATLAS, _HI_ATLAS, 3), np.float64)
    q = np.clip((np.arange(_HI_CELL, dtype=np.float64) + 0.5 - _HI_PAD) / (_HI_CELL - 2 * _HI_PAD), 0.0, 1.0)
    U = np.broadcast_to(q[None, :], (_HI_CELL, _HI_CELL))          # columns: u from 0 to 1
    V = np.broadcast_to(1.0 - q[:, None], (_HI_CELL, _HI_CELL))    # rows from the top: v from 1 to 0

    def bump(x):    # s(x) = 4x(1 - x) of the fraction of x, and s'(x) / 4
        fx = x - np.floor(x)
        return 4.0 * fx * (1.0 - fx), 1.0 - 2.0 * fx

    su, du = bump(U * _HI_QUILT)
    sv, dv = bump(V * _HI_QUILT)
    rv, rd = bump(V * _HI_RIBS)

    def cell(piece, spans, vs):
        x0, y0 = _hires_cell(piece)
        rgb = np.zeros((_HI_CELL, _HI_CELL, 3), np.float64)
        nx = np.zeros((_HI_CELL, _HI_CELL), np.float64)
        ny = np.zeros((_HI_CELL, _HI_CELL), np.float64)
        span = np.clip(np.searchsorted(np.asarray(vs, np.float64), V[:, 0], side="right") - 1, 0, len(spans) - 1)
        for row in range(_HI_CELL):
            field = spans[int(span[row])]
            colour = np.asarray(_FIELD_RGB[field], np.float64) / 255.0
            if field == "body":
                h = su[row] * sv[row]
                nx[row] = -_HI_TILT * du[row] * sv[row]
                ny[row] = -_HI_TILT * su[row] * dv[row]
                rgb[row] = colour[None, :] * (0.88 + 0.12 * h)[:, None]
            elif field in ("bandL", "bandR"):
                h = rv[row]
                ny[row] = -_HI_TILT * rd[row]
                rgb[row] = colour[None, :] * (0.85 + 0.15 * h)[:, None]
            else:
                rgb[row] = colour[None, :]
        length = np.sqrt(nx * nx + ny * ny + 1.0)
        base[y0:y0 + _HI_CELL, x0:x0 + _HI_CELL] = rgb
        nrm[y0:y0 + _HI_CELL, x0:x0 + _HI_CELL, 0] = nx / length
        nrm[y0:y0 + _HI_CELL, x0:x0 + _HI_CELL, 1] = ny / length
        nrm[y0:y0 + _HI_CELL, x0:x0 + _HI_CELL, 2] = 1.0 / length

    base[:, :] = np.asarray(_FIELD_RGB["body"], np.float64) / 255.0
    nrm[:, :, 2] = 1.0
    for seg in m.pieces:
        cell(seg.piece, seg.span_fields, seg.v)

    def png(arr):
        b = np.floor(np.clip(arr, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        return _png_bytes(_HI_ATLAS, _HI_ATLAS, [b[y].tobytes() for y in range(_HI_ATLAS)])

    return png(base), png(nrm * 0.5 + 0.5)


def _make_material(name, image, normal_image=None):
    old = bpy.data.materials.get(name)
    if old is not None:
        bpy.data.materials.remove(old)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Roughness"].default_value = 0.8
    bsdf.inputs["Metallic"].default_value = 0.0
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Closest" if normal_image is None else "Linear"
    tex.location = (-320.0, 260.0)
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if normal_image is not None:
        ntex = nodes.new("ShaderNodeTexImage")
        ntex.image = normal_image
        ntex.interpolation = "Linear"
        ntex.location = (-520.0, -120.0)
        nmap = nodes.new("ShaderNodeNormalMap")
        nmap.space = "TANGENT"
        nmap.uv_map = "UVMap"
        nmap.location = (-240.0, -120.0)
        mat.node_tree.links.new(ntex.outputs["Color"], nmap.inputs["Color"])
        mat.node_tree.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    mat.diffuse_color = (0.78, 0.745, 0.667, 1.0)
    return mat


# ------------------------------------------------------------------ the puppet geometry


def _build_geometry(bones, style=None):
    # The puppet of the ways Low-Poly (style None: the shapes as written) and Hi-Res (_HIRES).
    T = {n: (Vector(h), Vector(t)) for n, h, t, _p, _d in bones}
    m = _Mesh()
    main = {}
    # the offset of the seat from the standard, for the shapes that are not drawn along a bone
    d = T["pelvis"][0] - Vector((0.0, 16.0, 1.0))

    def P(x, y, z):
        return Vector((x, y, z)) + d

    # ---- the middle line
    main["pelvis"] = _lathe(
        m, "pelvis", P(0, 10.2, 0.9), P(0, 24.4, 0.9),
        [(0.0, 0.0), (0.0, 6.5), (1.0, 8.5), (2.8, 9.4), (9.5, 9.4), (12.0, 7.6), (13.6, 4.5), (14.2, 0.0)],
        14, 1.0, 0.8, 2.4, style=style)
    main["spine"] = _lathe(
        m, "spine", P(0, 21.0, 0.1), P(0, 34.5, -1.1),
        _capsule_rings(13.5, 6.4, 6.4)[0], 14, 1.12, 0.88, style=style)
    main["chest"] = _lathe(
        m, "chest", P(0, 32.5, -1.0), P(0, 52.5, -1.0),
        [(0.0, 0.0), (0.3, 6.5), (3.0, 9.2), (9.0, 10.4), (14.5, 9.6), (17.5, 7.0), (19.2, 4.6), (20.0, 0.0)],
        14, 1.0, 0.74, 2.4, style=style)
    main["neck"] = _lathe(
        m, "neck", P(0, 51.0, -1.0), P(0, 62.4, 0.3),
        _capsule_rings(11.5, 3.5, 3.6)[0], 10, style=style)
    main["head"] = _lathe(
        m, "head", P(0, 59.9, 0.3), P(0, 84.2, 0.3),
        _ellipsoid_rings(24.3, 11.2, 8), 14, 1.0, 1.03, style=style)

    # ---- arms and legs, both sides
    fingers_dir = {}
    for suffix, sx in ((".L", 1.0), (".R", -1.0)):
        sh_h, sh_t = T["shoulder" + suffix]
        ua_h, ua_t = T["upper_arm" + suffix]
        fa_h, fa_t = T["forearm" + suffix]
        ha_h, ha_t = T["hand" + suffix]
        fi_h, fi_t = T["fingers" + suffix]
        th_h, th_t = T["thumb" + suffix]
        dv = (ha_t - ha_h).normalized()
        fingers_dir[suffix] = dv
        band = "bandL" if sx > 0 else "bandR"

        def length(a, b):
            return (b - a).length

        main["shoulder" + suffix] = _lathe(
            m, "shoulder" + suffix, sh_h - Vector((sx, 0, 0)), sh_t + Vector((0.5 * sx, 0, 0)),
            _capsule_rings(length(sh_h, sh_t) + 1.5, 3.9, 3.9)[0], 10, style=style)
        main["upper_arm" + suffix] = _lathe(
            m, "upper_arm" + suffix, ua_h, ua_t, _capsule_rings(length(ua_h, ua_t), 3.5, 3.1)[0], 10, style=style)
        rings, bi = _capsule_rings(length(fa_h, fa_t), 3.1, 2.7, band=(0.55, 0.78))
        main["forearm" + suffix] = _lathe(m, "forearm" + suffix, fa_h, fa_t, rings, 10, fields={bi: band}, style=style)
        main["hand" + suffix] = _lathe(
            m, "hand" + suffix, ha_h, ha_t + dv * 2.3,
            [(0.0, 0.0), (0.4, 2.5), (2.2, 3.1), (4.6, 3.4), (6.5, 3.2), (8.0, 2.4), (8.7, 0.0)],
            10, 1.0, 0.82, style=style)
        main["fingers" + suffix] = _lathe(
            m, "fingers" + suffix, fi_h - dv * 0.5, fi_h + dv * 5.0,
            _capsule_rings(5.5, 2.5, 2.3)[0], 8, 1.25, 0.9, style=style)
        main["thumb" + suffix] = _lathe(
            m, "thumb" + suffix, th_h, th_t, _capsule_rings(3.4, 1.5, 1.3)[0], 8, style=style)

        tg_h, tg_t = T["thigh" + suffix]
        sn_h, sn_t = T["shin" + suffix]
        ft_h, _ft_t = T["foot" + suffix]
        main["thigh" + suffix] = _lathe(
            m, "thigh" + suffix, tg_h, tg_t, _capsule_rings(length(tg_h, tg_t), 5.4, 4.0)[0], 12, style=style)
        rings, bi = _capsule_rings(length(sn_h, sn_t), 4.3, 3.4, band=(0.55, 0.78))
        main["shin" + suffix] = _lathe(m, "shin" + suffix, sn_h, sn_t, rings, 12, fields={bi: band}, style=style)
        main["foot" + suffix] = _lathe(
            m, "foot" + suffix, P(8 * sx, 10.2, 37.8), P(8 * sx, 10.2, 50.6),
            [(0.0, 0.0), (0.9, 0.62), (2.2, 0.92), (4.0, 1.0), (9.4, 1.0), (11.4, 0.9), (12.5, 0.55), (12.8, 0.0)],
            12, 4.2, 2.7, 3.2, style=style)
        main["toe" + suffix] = _lathe(
            m, "toe" + suffix, P(8 * sx, 10.0, 49.4), P(8 * sx, 10.0, 55.4),
            [(0.0, 0.0), (0.5, 0.6), (1.4, 0.95), (4.4, 1.0), (5.4, 0.85), (6.0, 0.0)],
            12, 3.9, 2.5, 3.2, style=style)

    # ---- eyes: two dark discs on the front of the head, welded into the head
    for sx in (1.0, -1.0):
        eye = _lathe(
            m, "head", P(4.0 * sx, 72.5, 9.2), P(4.0 * sx, 72.5, 11.7),
            [(0.0, 0.0), (0.0, 1.9), (2.5, 1.9), (2.5, 0.0)], 10, default="eyes", smooth=False, style=style, refine=False)
        _weld(m, P(4.0 * sx, 72.5, 9.7), "head", main["head"], eye)

    # ---- joints: a ball on the parent bone at the child's head, welded to both
    def joint(child, parent, J, radius, segs=10):
        J = Vector(J)
        ball = _lathe(m, parent, J - Vector((0, radius, 0)), J + Vector((0, radius, 0)),
                      _ball_rings(radius), segs, default="joint", style=style)
        _weld(m, _toward(J, main[parent].mid, 1.0), parent, main[parent], ball)
        _weld(m, _toward(J, main[child].mid, 1.0), parent, ball, main[child])

    joint("spine", "pelvis", T["spine"][0], 5.6)
    joint("chest", "spine", T["chest"][0], 5.8)
    joint("neck", "chest", T["neck"][0], 4.4)
    joint("head", "neck", P(0, 61.2, 0.3), 4.6)
    for suffix, sx in ((".L", 1.0), (".R", -1.0)):
        joint("shoulder" + suffix, "chest", T["shoulder" + suffix][0], 4.4)
        joint("upper_arm" + suffix, "shoulder" + suffix, T["upper_arm" + suffix][0], 4.6)
        joint("forearm" + suffix, "upper_arm" + suffix, T["forearm" + suffix][0], 4.0)
        joint("hand" + suffix, "forearm" + suffix, T["hand" + suffix][0], 3.3, 8)
        joint("fingers" + suffix, "hand" + suffix, T["fingers" + suffix][0], 2.6, 8)
        joint("thumb" + suffix, "hand" + suffix, T["thumb" + suffix][0], 1.9, 8)
        joint("thigh" + suffix, "pelvis", T["thigh" + suffix][0], 5.8)
        joint("shin" + suffix, "thigh" + suffix, T["shin" + suffix][0], 5.0)
        joint("foot" + suffix, "shin" + suffix, T["foot" + suffix][0], 3.6)
        joint("toe" + suffix, "foot" + suffix, P(8 * sx, 10.0, 50.0), 2.4, 8)
    return m


def _build_vanilla(bones):
    # The puppet of the way Vanilla (the classic model of make-char): 16 chunky pieces in one connected mesh
    # (each piece's start pole lies inside the piece before it: _weld_pole), 559 triangles. No piece is
    # thinner than about 2.5 u and no ring closer than 1.5 u to the next, so the packing of the classic model
    # (8 bits per axis over the model, the up axis 7) keeps every triangle. Mittens in the band colours (left
    # green, right blue), dark shoes, a dark visor for the eyes.
    T = {n: (Vector(h), Vector(t)) for n, h, t, _p, _d in bones}
    m = _Mesh()
    d = T["pelvis"][0] - Vector((0.0, 16.0, 1.0))

    def P(x, y, z):
        return Vector((x, y, z)) + d

    def regroup(seg, pick):
        for i in seg.verts:
            m.bone[i] = pick(m.co[i] - d)

    def start(seg):
        return seg.ring_ids[0][0]

    def end(seg):
        return seg.ring_ids[-1][0]

    def limb(bone, h, t, r0, r1, ext=1.8):
        dv = (t - h).normalized()
        length = (t - h).length + 2.0 * ext
        cap = min(1.8, 0.4 * r0)
        return _lathe(m, bone, h - dv * ext, t + dv * ext,
                      [(0.0, 0.0), (cap, r0), (length - cap, r1), (length, 0.0)], 6)

    torso = _lathe(
        m, "chest", P(0, 10.2, 0.6), P(0, 52.6, -1.0),
        [(0.0, 0.0), (0.0, 6.0), (2.6, 9.0), (9.0, 9.2), (14.5, 7.4), (30.0, 9.6, 1.1), (38.6, 7.0, 1.6),
         (41.6, 3.6, 1.3), (42.4, 0.0)], 8, 1.0, 0.8, 2.4)
    regroup(torso, lambda p: "pelvis" if p.y < 22.0 else ("spine" if p.y < 33.0 else "chest"))
    head = _lathe(
        m, "head", P(0, 51.0, -1.0), P(0, 84.2, 0.3),
        [(0.0, 0.0), (7.5, 3.6), (13.5, 10.5), (19.5, 11.2), (23.5, 11.0), (28.5, 8.0), (33.2, 0.0)], 8, 1.0, 1.03)
    regroup(head, lambda p: "neck" if p.y < 58.0 else "head")
    for ti in head.tris:
        a, b, c = (m.co[i] - d for i in m.tris[ti][:3])
        cy, cz = (a.y + b.y + c.y) / 3.0, (a.z + b.z + c.z) / 3.0
        if 70.0 < cy < 75.0 and cz > 4.0:
            m.tris[ti][3] = "eyes"
    _weld_pole(m, start(head), torso)

    for suffix, sx in ((".L", 1.0), (".R", -1.0)):
        S = T["upper_arm" + suffix][0]
        E = T["forearm" + suffix][0]
        W = T["hand" + suffix][0]
        G = T["fingers" + suffix][0]
        dh = (G - W).normalized()
        band = "bandL" if sx > 0 else "bandR"
        sh0 = P(4.5 * sx, 49.0, -1.0)
        sh1 = Vector((S.x + 2.0 * sx, S.y, S.z))
        sl = (sh1 - sh0).length
        shoulder = _lathe(m, "shoulder" + suffix, sh0, sh1, [(0.0, 0.0), (1.6, 3.8), (sl - 1.6, 3.8), (sl, 0.0)], 6)
        _weld_pole(m, start(shoulder), torso)
        upper = limb("upper_arm" + suffix, S, E, 3.5, 3.1)
        _weld_pole(m, start(upper), shoulder)
        fore = limb("forearm" + suffix, E, W, 3.1, 2.7)
        _weld_pole(m, start(fore), upper)
        hl = (G - W).length + 6.0
        hand = _lathe(m, "hand" + suffix, W - dh * 1.5, W - dh * 1.5 + dh * hl,
                      [(0.0, 0.0), (1.5, 2.9), (hl - 2.2, 3.1), (hl, 0.0)], 6, 1.15, 0.85, default=band)
        regroup(hand, lambda p, g=G - d, dv=dh, s=suffix: ("fingers" + s) if (p - g).dot(dv) > 0.0 else ("hand" + s))
        _weld_pole(m, start(hand), fore)

        TH = T["thigh" + suffix][0]
        K = T["shin" + suffix][0]
        A = T["foot" + suffix][0]
        thigh = limb("thigh" + suffix, TH, K, 5.4, 4.0)
        _weld_pole(m, start(thigh), torso)
        shin = limb("shin" + suffix, K, A, 4.3, 3.4)
        _weld_pole(m, start(shin), thigh)
        shoe = _lathe(m, "foot" + suffix, P(8 * sx, 10.2, 37.8), P(8 * sx, 10.2, 55.4),
                      [(0.0, 0.0), (1.8, 0.9), (15.6, 0.95), (17.6, 0.0)], 6, 4.2, 2.7, 3.2, default="joint")
        regroup(shoe, lambda p, s=suffix: ("toe" + s) if p.z > 50.0 else ("foot" + s))
        _weld_pole(m, end(shin), shoe)
    return m


# ------------------------------------------------------------------ build_puppet

STYLES = ("vanilla", "lowpoly", "hires")
_VANILLA_STEP = 16       # the vanilla puppet's PS1 shading in steps of 16: 64, 80, .. 192 (9 values)
_VANILLA_RANGE = (64, 192)


def build_puppet(collection, rig, bones, vcol_mode, style="lowpoly"):
    if style not in STYLES:
        raise ValueError(style)
    if style == "vanilla":
        m = _build_vanilla(bones)
    else:
        m = _build_geometry(bones, _HIRES if style == "hires" else None)
    verts = [tuple(Bv(c)) for c in m.co]
    faces = [(t[0], t[1], t[2]) for t in m.tris]
    mesh = bpy.data.meshes.new("puppet")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    mesh.polygons.foreach_set("use_smooth", [bool(t[4]) for t in m.tris])

    uvl = mesh.uv_layers.new(name="UVMap")
    uv = []
    if style == "hires":
        # a cell of the atlas per piece, the piece unrolled in it (u around, v along)
        for t in m.tris:
            for c in t[6]:
                uv.extend(_hires_uv(t[5], c))
        base_png, normal_png = _hires_pngs(m)
        image = _packed_image("puppet_atlas", _HI_ATLAS, _HI_ATLAS, base_png, "sRGB")
        normal = _packed_image("puppet_normal", _HI_ATLAS, _HI_ATLAS, normal_png, "Non-Color")
    else:
        # flat colour fields of the atlas
        offs = ((-3.0, -3.0), (3.0, -3.0), (0.0, 3.0))
        for t in m.tris:
            x0, y0, x1, y1 = _FIELDS[t[3]]
            cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
            for ox, oy in offs:
                uv.extend(((cx + ox) / _ATLAS, 1.0 - (cy + oy) / _ATLAS))
        image = _make_atlas_image()
        normal = None
    uvl.uv.foreach_set("vector", uv)

    mat = _make_material("puppet [vcol %s]" % vcol_mode, image, normal)
    mesh.materials.append(mat)

    obj = bpy.data.objects.new("puppet", mesh)
    collection.objects.link(obj)

    # one vertex group per figure bone, every vertex 100 % one bone
    members = {}
    for i, b in enumerate(m.bone):
        members.setdefault(b, []).append(i)
    names = [n for n in FIGURE_BONES]
    for n in names:
        vg = obj.vertex_groups.new(name=n)
        if n in members:
            vg.add(members[n], 1.0, "REPLACE")

    if vcol_mode == "psx":
        n = len(mesh.vertices)
        nr = [0.0] * (n * 3)
        mesh.vertex_normals.foreach_get("vector", nr)
        light = Vector(_PSX_LIGHT).normalized()
        zs = [v[2] for v in verts]
        z0, z1 = min(zs), max(zs)
        shades = []
        for i in range(n):
            nv = Vector((nr[3 * i], nr[3 * i + 1], nr[3 * i + 2]))
            h = (verts[i][2] - z0) / max(z1 - z0, 1e-9)
            shades.append(_PSX_NORMAL_SHARE * nv.dot(light) + (1.0 - _PSX_NORMAL_SHARE) * (2.0 * h - 1.0))
        median = sorted(shades)[len(shades) // 2] if len(shades) % 2 else \
            0.5 * (sorted(shades)[len(shades) // 2 - 1] + sorted(shades)[len(shades) // 2])
        data = []
        for s in shades:
            dd = s - median
            dd = math.copysign(max(abs(dd) - _PSX_DEAD_ZONE, 0.0), dd)
            b = int(round(min(max(128.0 + _PSX_SPREAD * dd, _PSX_RANGE[0]), _PSX_RANGE[1])))
            if style == "vanilla":
                b = 128 + _VANILLA_STEP * int(round((b - 128) / float(_VANILLA_STEP)))
                b = min(max(b, _VANILLA_RANGE[0]), _VANILLA_RANGE[1])
            data.append(b)
        attr = mesh.color_attributes.new(name="Col", type="BYTE_COLOR", domain="POINT")
        rgba = []
        for b in data:
            v = b / 255.0
            rgba.extend((v, v, v, 1.0))
        attr.data.foreach_set("color_srgb", rgba)
        mesh.color_attributes.active_color = attr
        mesh.color_attributes.render_color_index = mesh.color_attributes.active_color_index

    mesh.update()

    # the armature
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    obj.parent = rig
    obj.parent_type = "OBJECT"
    return obj


def style_numbers(obj):
    """Triangles, vertices and the textures [name, width, height] of a built puppet (for the report)."""
    mesh = obj.data
    images = []
    for mat in mesh.materials:
        if mat is not None and mat.node_tree is not None:
            for node in mat.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image is not None:
                    images.append([node.image.name, node.image.size[0], node.image.size[1]])
    return {"triangles": sum(len(p.vertices) - 2 for p in mesh.polygons), "vertices": len(mesh.vertices),
            "textures": sorted(images)}
