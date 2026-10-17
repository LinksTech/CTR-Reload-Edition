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
#   build_puppet(collection, rig, bones, vcol_mode)   the mesh "puppet"
#
# `info` is the parsed kit-info.json. Only its "seat" and "steer" are read;
# a missing key falls back to the standard value.
#
# Units and axes. Model units "u": 64 u = 1 m. glTF axes: x = kart LEFT,
# y = up, z = kart FRONT, origin on the ground under the kart. Blender:
# X = x / 64, Y = -z / 64, Z = y / 64 (the nose toward -Y). Left = ".L" = +x,
# right = ".R" = -x. Same numbers, same mesh: nothing here is random.

import math
import struct
import zlib

import bpy
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
        self.tris = []  # [a, b, c, field, smooth]

    def vert(self, p, bone):
        self.co.append(Vector(p))
        self.bone.append(bone)
        return len(self.co) - 1

    def tri(self, a, b, c, field="body", smooth=True):
        self.tris.append([a, b, c, field, smooth])
        return len(self.tris) - 1


class _Seg:
    def __init__(self, bone, mid):
        self.bone = bone
        self.verts = []
        self.tris = []
        self.mid = Vector(mid)


def _lathe(m, bone, a, b, rings, segs, sx=1.0, sz=1.0, power=2.0, fields=None, default="body", smooth=True):
    # A closed surface along the axis a -> b. rings = [(t, r)]: t along the axis
    # from a, r the radius (0 = a pole). The cross-section is a superellipse
    # (power 2 = ellipse) with the scales sx (side) and sz (the other way).
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
    seg = _Seg(bone, a + w * (0.5 * (Vector(b) - a).length))
    ring_ids = []
    for t, r in rings:
        if r <= 1e-9:
            i = m.vert(a + w * t, bone)
            seg.verts.append(i)
            ring_ids.append([i])
        else:
            ids = []
            for c, sn in cs:
                ids.append(m.vert(a + w * t + s * (c * r * sx) + f * (sn * r * sz), bone))
            seg.verts.extend(ids)
            ring_ids.append(ids)
    fields = fields or {}
    first = len(m.tris)
    for i in range(len(rings) - 1):
        R0, R1 = ring_ids[i], ring_ids[i + 1]
        fld = fields.get(i, default)
        if len(R0) == 1 and len(R1) == 1:
            continue
        for k in range(segs):
            k1 = (k + 1) % segs
            if len(R0) == 1:
                m.tri(R0[0], R1[k1], R1[k], fld, smooth)
            elif len(R1) == 1:
                m.tri(R0[k], R0[k1], R1[0], fld, smooth)
            else:
                m.tri(R0[k], R0[k1], R1[k1], fld, smooth)
                m.tri(R0[k], R1[k1], R1[k], fld, smooth)
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


def _make_atlas_image():
    old = bpy.data.images.get("puppet_atlas")
    if old is not None:
        bpy.data.images.remove(old)
    data = _atlas_png()
    img = bpy.data.images.new("puppet_atlas", _ATLAS, _ATLAS, alpha=False)
    img.pack(data=data, data_len=len(data))
    img.source = "FILE"  # read the packed PNG bytes, not the black generated buffer
    img.colorspace_settings.name = "sRGB"
    return img


def _make_material(name, image):
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
    tex.interpolation = "Closest"
    tex.location = (-320.0, 260.0)
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    mat.diffuse_color = (0.78, 0.745, 0.667, 1.0)
    return mat


# ------------------------------------------------------------------ the puppet geometry


def _build_geometry(bones):
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
        14, 1.0, 0.8, 2.4)
    main["spine"] = _lathe(
        m, "spine", P(0, 21.0, 0.1), P(0, 34.5, -1.1),
        _capsule_rings(13.5, 6.4, 6.4)[0], 14, 1.12, 0.88)
    main["chest"] = _lathe(
        m, "chest", P(0, 32.5, -1.0), P(0, 52.5, -1.0),
        [(0.0, 0.0), (0.3, 6.5), (3.0, 9.2), (9.0, 10.4), (14.5, 9.6), (17.5, 7.0), (19.2, 4.6), (20.0, 0.0)],
        14, 1.0, 0.74, 2.4)
    main["neck"] = _lathe(
        m, "neck", P(0, 51.0, -1.0), P(0, 62.4, 0.3),
        _capsule_rings(11.5, 3.5, 3.6)[0], 10)
    main["head"] = _lathe(
        m, "head", P(0, 59.9, 0.3), P(0, 84.2, 0.3),
        _ellipsoid_rings(24.3, 11.2, 8), 14, 1.0, 1.03)

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
            _capsule_rings(length(sh_h, sh_t) + 1.5, 3.9, 3.9)[0], 10)
        main["upper_arm" + suffix] = _lathe(
            m, "upper_arm" + suffix, ua_h, ua_t, _capsule_rings(length(ua_h, ua_t), 3.5, 3.1)[0], 10)
        rings, bi = _capsule_rings(length(fa_h, fa_t), 3.1, 2.7, band=(0.55, 0.78))
        main["forearm" + suffix] = _lathe(m, "forearm" + suffix, fa_h, fa_t, rings, 10, fields={bi: band})
        main["hand" + suffix] = _lathe(
            m, "hand" + suffix, ha_h, ha_t + dv * 2.3,
            [(0.0, 0.0), (0.4, 2.5), (2.2, 3.1), (4.6, 3.4), (6.5, 3.2), (8.0, 2.4), (8.7, 0.0)],
            10, 1.0, 0.82)
        main["fingers" + suffix] = _lathe(
            m, "fingers" + suffix, fi_h - dv * 0.5, fi_h + dv * 5.0,
            _capsule_rings(5.5, 2.5, 2.3)[0], 8, 1.25, 0.9)
        main["thumb" + suffix] = _lathe(
            m, "thumb" + suffix, th_h, th_t, _capsule_rings(3.4, 1.5, 1.3)[0], 8)

        tg_h, tg_t = T["thigh" + suffix]
        sn_h, sn_t = T["shin" + suffix]
        ft_h, _ft_t = T["foot" + suffix]
        main["thigh" + suffix] = _lathe(
            m, "thigh" + suffix, tg_h, tg_t, _capsule_rings(length(tg_h, tg_t), 5.4, 4.0)[0], 12)
        rings, bi = _capsule_rings(length(sn_h, sn_t), 4.3, 3.4, band=(0.55, 0.78))
        main["shin" + suffix] = _lathe(m, "shin" + suffix, sn_h, sn_t, rings, 12, fields={bi: band})
        main["foot" + suffix] = _lathe(
            m, "foot" + suffix, P(8 * sx, 10.2, 37.8), P(8 * sx, 10.2, 50.6),
            [(0.0, 0.0), (0.9, 0.62), (2.2, 0.92), (4.0, 1.0), (9.4, 1.0), (11.4, 0.9), (12.5, 0.55), (12.8, 0.0)],
            12, 4.2, 2.7, 3.2)
        main["toe" + suffix] = _lathe(
            m, "toe" + suffix, P(8 * sx, 10.0, 49.4), P(8 * sx, 10.0, 55.4),
            [(0.0, 0.0), (0.5, 0.6), (1.4, 0.95), (4.4, 1.0), (5.4, 0.85), (6.0, 0.0)],
            12, 3.9, 2.5, 3.2)

    # ---- eyes: two dark discs on the front of the head, welded into the head
    for sx in (1.0, -1.0):
        eye = _lathe(
            m, "head", P(4.0 * sx, 72.5, 9.2), P(4.0 * sx, 72.5, 11.7),
            [(0.0, 0.0), (0.0, 1.9), (2.5, 1.9), (2.5, 0.0)], 10, default="eyes", smooth=False)
        _weld(m, P(4.0 * sx, 72.5, 9.7), "head", main["head"], eye)

    # ---- joints: a ball on the parent bone at the child's head, welded to both
    def joint(child, parent, J, radius, segs=10):
        J = Vector(J)
        ball = _lathe(m, parent, J - Vector((0, radius, 0)), J + Vector((0, radius, 0)),
                      _ball_rings(radius), segs, default="joint")
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


# ------------------------------------------------------------------ build_puppet


def build_puppet(collection, rig, bones, vcol_mode):
    m = _build_geometry(bones)
    verts = [tuple(Bv(c)) for c in m.co]
    faces = [(t[0], t[1], t[2]) for t in m.tris]
    mesh = bpy.data.meshes.new("puppet")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    mesh.polygons.foreach_set("use_smooth", [bool(t[4]) for t in m.tris])

    # UVs: flat colour fields of the atlas
    uvl = mesh.uv_layers.new(name="UVMap")
    offs = ((-3.0, -3.0), (3.0, -3.0), (0.0, 3.0))
    uv = []
    for t in m.tris:
        x0, y0, x1, y1 = _FIELDS[t[3]]
        cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
        for ox, oy in offs:
            uv.extend(((cx + ox) / _ATLAS, 1.0 - (cy + oy) / _ATLAS))
    uvl.uv.foreach_set("vector", uv)

    image = _make_atlas_image()
    mat = _make_material("puppet [vcol %s]" % vcol_mode, image)
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
