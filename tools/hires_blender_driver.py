#!/usr/bin/env python3
"""Build the animated synthetic hi-res test driver in Blender and export it.

Usage:
  blender --background --factory-startup --python tools/hires_blender_driver.py \
      -- <out dir> [--blend <file.blend>]

Writes <out dir>/anim.hra and <out dir>/anim.hrt (and the .blend only if asked).

anim.hra (HRA1, little endian, no padding):
  4   magic "HRA1"
  4   u32 vertexCount (1..65535)
  4   u32 indexCount (>0, multiple of 3, <= 600000)
  4   u32 frameCount (1..256)
  4   u32 clipCount (1..16)
  24*clipCount  char name[16] (NUL padded), u32 firstFrame, u32 frameCount
  8*vertexCount            f32 u, f32 v (v = 0 is the TOP row of anim.hrt)
  16*vertexCount*frameCount  per frame, per vertex f32 x, y, z, u8 r, g, b, a
  2*indexCount             u16 indices
anim.hrt (HRT1): "HRT1", u32 512, u32 512, RGBA8 rows from the top.

Model space: x = kart left, y = up, z = front. Blender (X, Y, Z) = (-x, z, y).
Deterministic: no random numbers (fixed LCG in the texture only).
"""
import math
import os
import struct
import sys
import traceback

import numpy as np

import bpy
import bmesh
from mathutils import Vector, Quaternion

TEX = 512
MARGIN = 4
REG = {  # name -> (x0, y0, x1, y1) in pixels, end exclusive
    "PAINT": (0, 0, 256, 256),
    "SUIT": (256, 0, 512, 256),
    "HELMET": (0, 256, 256, 512),
    "GREY": (256, 256, 512, 384),
    "SKIN": (256, 384, 384, 512),
    "DARK": (384, 384, 512, 512),
}
LIGHT = np.array([0.4, 0.8, 0.45])
LIGHT = LIGHT / np.linalg.norm(LIGHT)
BOX_FACES = [(0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
CLIPS = [("steer", 1, 9), ("air", 11, 4), ("drift_l", 21, 5), ("drift_r", 31, 5)]
GROUPS = ["chassis", "steer", "spine", "head", "arm_upper.L", "arm_lower.L",
          "arm_upper.R", "arm_lower.R"]
AX = (1.0, 0.0, 0.0)
AY = (0.0, 1.0, 0.0)
AZ = (0.0, 0.0, 1.0)


class Fail(Exception):
    pass


def B(p):
    """Model (x, y, z) -> Blender (X, Y, Z)."""
    return (-float(p[0]), float(p[2]), float(p[1]))


# ---------------------------------------------------------------- texture
def reg_uv(name, ta, tb):
    x0, y0, x1, y1 = REG[name]
    px = x0 + MARGIN + ta * (x1 - x0 - 2 * MARGIN)
    py = y0 + MARGIN + tb * (y1 - y0 - 2 * MARGIN)
    return (px / TEX, py / TEX)


def planar(pts, region):
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


FONT = {
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
}


def build_texture():
    tex = np.zeros((TEX, TEX, 3), np.uint8)
    # PAINT: orange, diagonal hatch, checker band, pseudo text, white "07"
    R = tex[0:256, 0:256]
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
    R = tex[0:256, 256:512]
    Y, X = np.mgrid[0:256, 0:256]
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
    R = tex[256:512, 0:256]
    Y, X = np.mgrid[0:256, 0:256]
    R[:] = (240, 240, 240)
    R[((X % 2 == 0) | (Y % 2 == 0)) & (Y < 128)] = (200, 200, 205)
    for sx, w, col in ((48, 6, (200, 30, 30)), (56, 2, (0, 0, 0)), (60, 4, (200, 30, 30)),
                       (188, 4, (200, 30, 30)), (194, 2, (0, 0, 0)), (198, 6, (200, 30, 30)),
                       (120, 3, (0, 0, 0)), (125, 6, (200, 30, 30))):
        R[:, sx:sx + w] = col
    R[94:124] = (20, 25, 35)
    R[99:100] = (60, 80, 110)
    # GREY: mechanical grid
    R = tex[256:384, 256:512]
    Y, X = np.mgrid[0:128, 0:256]
    R[:] = (128, 128, 128)
    R[(X % 4 == 0) | (Y % 4 == 0)] = (255, 255, 255)
    # SKIN
    R = tex[384:512, 256:384]
    Y, X = np.mgrid[0:128, 0:128]
    R[:] = (230, 180, 140)
    R[(X % 4 == 0) | (Y % 4 == 0)] = (215, 165, 125)
    # DARK
    R = tex[384:512, 384:512]
    Y, X = np.mgrid[0:128, 0:128]
    R[:] = (40, 40, 44)
    R[(X % 8 == 0) | (Y % 8 == 0)] = (70, 70, 76)
    return tex


# ------------------------------------------------------------------- mesh
class Mesh:
    """Flat-face part builder (model coordinates)."""

    def __init__(self):
        self.faces = []  # (bone, [pts], [uv], smooth)
        self.bone = None
        self.smooth = False
        self.c = np.zeros(3)

    def part(self, pts):
        self.c = np.mean(np.array(pts, float), axis=0)

    def emit(self, P, UV):
        P = [np.asarray(q, float) for q in P]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        ln = np.linalg.norm(n)
        if ln < 1e-9:
            return
        n /= ln
        if float(n @ (np.mean(P, axis=0) - self.c)) < 0:
            P, UV = P[::-1], UV[::-1]
        self.faces.append((self.bone, P, list(UV), self.smooth))

    def hexa(self, v, region):
        v = [np.array(p, float) for p in v]
        self.part(v)
        for f in BOX_FACES:
            pts = [v[i] for i in f]
            fn = planar(pts, region)
            self.emit(pts, [fn(p) for p in pts])

    def box(self, x0, x1, y0, y1, z0, z1, region):
        self.hexa([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
                   (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], region)

    def seg_box(self, a, b, w, region):
        a, b = np.array(a, float), np.array(b, float)
        d = (b - a) / np.linalg.norm(b - a)
        ref = np.array([0.0, 1.0, 0.0]) if abs(d[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
        u = np.cross(d, ref)
        u /= np.linalg.norm(u)
        v = np.cross(d, u)
        h = w / 2.0

        def ring(p):
            return [p - u * h - v * h, p + u * h - v * h, p + u * h + v * h, p - u * h + v * h]

        self.hexa(ring(a) + ring(b), region)

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


def build_parts():
    m = Mesh()
    m.bone = "chassis"
    m.box(-22, 22, 5, 13, -34, 64, "PAINT")  # floor
    m.hexa([(-22, 5, 64), (22, 5, 64), (12, 5, 78), (-12, 5, 78),
            (-22, 13, 64), (22, 13, 64), (12, 8, 78), (-12, 8, 78)], "PAINT")  # nose wedge
    m.box(14, 26, 8, 20, -10, 30, "PAINT")  # side pods
    m.box(-26, -14, 8, 20, -10, 30, "PAINT")
    m.cyl("x", (0, 9, 74), 2.5, 56, 12, "GREY")  # front bumper
    m.box(-14, 14, 13, 27, -34, -18, "GREY")  # engine block
    for sx in (8, -8):
        m.cyl("z", (sx, 22, -39), 3, 10, 12, "GREY")  # exhaust pipes
    for sx in (18, -18):
        m.box(sx - 1, sx + 1, 27, 41, -38, -36, "GREY")  # wing struts
    m.box(-25, 25, 41, 44, -46, -32, "GREY")  # rear wing plate (narrowed so a drift roll stays in x +-30)
    m.box(-12, 12, 13, 38, -20, -16, "DARK")  # seat back
    m.box(-1.5, 1.5, 13, 28, 21, 24, "DARK")  # steering column
    m.box(3, 9, 13, 19, -12, 20, "SUIT")  # legs
    m.box(-9, -3, 13, 19, -12, 20, "SUIT")
    m.box(3, 9, 13, 21, 20, 25, "DARK")  # shoes
    m.box(-9, -3, 13, 21, 20, 25, "DARK")
    m.bone = "steer"
    m.cyl("z", (0, 30, 18), 7, 1.5, 16, "DARK")  # steering wheel
    m.box(-2, 2, 28, 32, 18, 19.5, "PAINT")  # hub
    m.bone = "spine"
    m.box(-10, 10, 15, 36, -14, 2, "SUIT")  # torso
    m.bone = "head"
    m.smooth = True
    m.sphere((0, 47, -5), 9.5, 16, 10, "HELMET")
    m.smooth = False
    m.box(-7, 7, 44, 49, 3.5, 5.5, "HELMET")  # visor
    m.box(-3, 3, 36, 39, -8, -2, "SKIN")  # neck
    for side, sx in ((".L", 1.0), (".R", -1.0)):
        sh, el, wr = (10 * sx, 33, -4), (11 * sx, 25, 6), (6 * sx, 29, 15)
        m.bone = "arm_upper" + side
        m.seg_box(sh, el, 4.0, "SUIT")
        m.bone = "arm_lower" + side
        m.seg_box(el, wr, 3.5, "SUIT")
        m.box(wr[0] - 1.5, wr[0] + 1.5, wr[1] - 1.5, wr[1] + 1.5, 14.5, 17.5, "SKIN")  # hand
    return m


# --------------------------------------------------------------- scene
def clear_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)


def build_body_object(m, collection):
    bm = bmesh.new()
    uvl = bm.loops.layers.uv.new("UVMap")
    dl = bm.verts.layers.deform.verify()
    gidx = {n: i for i, n in enumerate(GROUPS)}
    head_verts = []
    for bone, P, UV, smooth in m.faces:
        vs = []
        for p in P:
            v = bm.verts.new(B(p))
            v[dl][gidx[bone]] = 1.0
            vs.append(v)
            if smooth:
                head_verts.append(v)
        f = bm.faces.new(vs)
        f.smooth = smooth
        for lp, uv in zip(f.loops, UV):
            lp[uvl].uv = (uv[0], 1.0 - uv[1])
    bmesh.ops.remove_doubles(bm, verts=head_verts, dist=1e-4)
    me = bpy.data.meshes.new("driver_body")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("driver_body", me)
    collection.objects.link(obj)
    for n in GROUPS:
        obj.vertex_groups.new(name=n)
    return obj


def make_material(tex, obj):
    try:
        img = bpy.data.images.new("driver_tex", TEX, TEX, alpha=False)
        rgba = np.ones((TEX, TEX, 4), np.float32)
        rgba[..., :3] = tex[::-1].astype(np.float32) / 255.0  # Blender rows start at the bottom
        img.pixels.foreach_set(rgba.ravel())
        img.update()
        mat = bpy.data.materials.new("driver_mat")
        try:
            mat.use_nodes = True
        except Exception:
            pass
        nt = mat.node_tree
        if nt is not None:
            bsdf = nt.nodes.get("Principled BSDF")
            node = nt.nodes.new("ShaderNodeTexImage")
            node.image = img
            if bsdf is not None:
                nt.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        obj.data.materials.append(mat)
    except Exception as e:
        print("WARNING: material/image setup failed: %r" % (e,))


BONES = [
    # name, head, tail (model), parent, deform
    ("root", (0, 0, 0), (0, 0, 10), None, True),
    ("chassis", (0, 5, 0), (0, 20, 0), "root", True),
    ("steer", (0, 30, 18), (0, 30, 22), "chassis", True),
    ("spine", (0, 15, -6), (0, 36, -6), "chassis", True),
    ("head", (0, 38, -5), (0, 56, -5), "spine", True),
]
for _s, _sx in ((".L", 1), (".R", -1)):
    BONES += [
        ("arm_upper" + _s, (10 * _sx, 33, -4), (11 * _sx, 25, 6), "spine", True),
        ("arm_lower" + _s, (11 * _sx, 25, 6), (6 * _sx, 29, 15), "arm_upper" + _s, True),
        ("hand_target" + _s, (6 * _sx, 29, 15), (6 * _sx, 29, 18), "steer", False),
        ("arm_pole" + _s, (16 * _sx, 27, -4), (16 * _sx, 27, -1), "spine", False),
    ]


def build_armature(collection):
    data = bpy.data.armatures.new("driver_rig")
    arm = bpy.data.objects.new("driver_rig", data)
    collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    ebs = {}
    for name, h, t, parent, deform in BONES:
        eb = data.edit_bones.new(name)
        eb.head = B(h)
        eb.tail = B(t)
        eb.roll = 0.0
        eb.use_deform = deform
        if parent:
            eb.parent = ebs[parent]
        ebs[name] = eb
    bpy.ops.object.mode_set(mode='OBJECT')
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    return arm


def eval_bones(arm, *names):
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ae = arm.evaluated_get(dg)
    return [(Vector(ae.pose.bones[n].head), Vector(ae.pose.bones[n].tail)) for n in names]


def add_ik(arm):
    report = []
    for side, sx in ((".L", 1), (".R", -1)):
        pb = arm.pose.bones["arm_lower" + side]
        c = pb.constraints.new('IK')
        c.name = "IK"
        c.target = arm
        c.subtarget = "hand_target" + side
        c.pole_target = arm
        c.pole_subtarget = "arm_pole" + side
        c.chain_count = 2
        want_elbow = Vector(B((11 * sx, 25, 6)))
        want_wrist = Vector(B((6 * sx, 29, 15)))

        def err(deg):
            c.pole_angle = math.radians(deg)
            (_, elbow), (_, wrist) = eval_bones(arm, "arm_upper" + side, "arm_lower" + side)
            return (elbow - want_elbow).length, (wrist - want_wrist).length

        best = min(range(-180, 181, 10), key=lambda a: err(a)[0])
        best = min([best + d * 0.5 for d in range(-20, 21)], key=lambda a: err(a)[0])
        e_el, e_wr = err(best)
        report.append((side, best, e_el, e_wr))
        if e_wr > 0.5:
            raise Fail("IK%s: wrist error %.3f > 0.5 in the rest pose" % (side, e_wr))
        if e_el > 0.5:
            print("WARNING: IK%s elbow error %.3f in the rest pose (pole angle %.1f deg)" % (side, e_el, best))
    for r in report:
        print("IK%s pole angle %.1f deg, rest elbow error %.4f, wrist error %.4f" % r)


def rot(pb, rots):
    """Rotate about MODEL axes (applied in list order), expressed in the bone rest frame."""
    q = Quaternion((1.0, 0.0, 0.0, 0.0))
    for ax, deg in rots:
        a_b = Vector((-ax[0], ax[2], ax[1]))
        q = Quaternion(a_b.normalized(), math.radians(deg)) @ q
    R = pb.bone.matrix_local.to_quaternion()
    pb.rotation_quaternion = R.inverted() @ q @ R


def poses():
    """Yield (clip, scene frame, {bone: rotations}, ik influence)."""
    out = []
    for i in range(9):
        s = -1 + 2 * i / 8
        out.append((1 + i, {"steer": [(AZ, -90 * s)], "spine": [(AZ, -8 * s)], "head": [(AY, 20 * s)]}, 1.0))
    for i in range(4):
        t = (i + 1) / 4
        out.append((11 + i, {"spine": [(AX, -15 * t)], "head": [(AX, -10 * t)],
                             "arm_upper.L": [(AX, -100 * t), (AZ, -20 * t)],
                             "arm_upper.R": [(AX, -100 * t), (AZ, 20 * t)]}, 1.0 - t))
    for base, sg in ((21, 1.0), (31, -1.0)):
        for i in range(5):
            t = (i + 1) / 5
            out.append((base + i, {"chassis": [(AZ, 6 * t * sg)],
                                   "spine": [(AZ, -25 * t * sg), (AY, 15 * t * sg)],
                                   "head": [(AY, 30 * t * sg)],
                                   "steer": [(AZ, -120 * t * sg)]}, 1.0))
    return out


def author_action(arm):
    try:
        bpy.context.preferences.edit.keyframe_new_interpolation_type = 'CONSTANT'
    except Exception as e:
        print("WARNING: cannot set default interpolation: %r" % (e,))
    scene = bpy.context.scene
    for frame, rots, infl in poses():
        for pb in arm.pose.bones:
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        for bone, r in rots.items():
            rot(arm.pose.bones[bone], r)
        for side in (".L", ".R"):
            arm.pose.bones["arm_lower" + side].constraints["IK"].influence = infl
        for pb in arm.pose.bones:
            pb.keyframe_insert("rotation_quaternion", frame=frame, group=pb.name)
        for side in (".L", ".R"):
            arm.pose.bones["arm_lower" + side].constraints["IK"].keyframe_insert(
                "influence", frame=frame, group="arm_lower" + side)
    for name, f0, _n in CLIPS:
        scene.timeline_markers.new(name, frame=f0)
    try:
        act = arm.animation_data.action
        cnt = 0
        for layer in act.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    for fc in cb.fcurves:
                        for kp in fc.keyframe_points:
                            kp.interpolation = 'CONSTANT'
                            cnt += 1
        print("action %s: %d keyframe points set CONSTANT" % (act.name, cnt))
    except Exception as e:
        print("WARNING: could not post-set CONSTANT interpolation: %r" % (e,))


# --------------------------------------------------------------- export
def read_eval(obj, frame):
    bpy.context.scene.frame_set(frame)
    dg = bpy.context.evaluated_depsgraph_get()
    oe = obj.evaluated_get(dg)
    me = oe.to_mesh()
    try:
        nv, nl = len(me.vertices), len(me.loops)
        co = np.empty(nv * 3, np.float32)
        me.vertices.foreach_get("co", co)
        cn = np.empty(nl * 3, np.float32)
        me.corner_normals.foreach_get("vector", cn)
        info = None
        if frame == 1:
            nt = len(me.loop_triangles)
            tl = np.empty(nt * 3, np.int32)
            me.loop_triangles.foreach_get("loops", tl)
            tp = np.empty(nt, np.int32)
            me.loop_triangles.foreach_get("polygon_index", tp)
            lv = np.empty(nl, np.int32)
            me.loops.foreach_get("vertex_index", lv)
            uv = np.empty(nl * 2, np.float32)
            layer = me.uv_layers[0]
            try:
                layer.uv.foreach_get("vector", uv)
            except Exception:
                layer.data.foreach_get("uv", uv)
            try:
                smooth = np.empty(len(me.polygons), bool)
                me.polygons.foreach_get("use_smooth", smooth)
            except Exception:
                sharp = np.zeros(len(me.polygons), bool)
                me.attributes["sharp_face"].data.foreach_get("value", sharp)
                smooth = ~sharp
            info = (tl.reshape(-1, 3), tp, lv, uv.reshape(-1, 2), smooth)
        return co.reshape(-1, 3), cn.reshape(-1, 3), info
    finally:
        oe.to_mesh_clear()


def to_model(a):
    return np.stack([-a[:, 0], a[:, 2], a[:, 1]], axis=1)


def export(obj, tex, outdir, blend):
    frames = []
    for name, f0, n in CLIPS:
        frames += list(range(f0, f0 + n))
    co1, cn1, info = read_eval(obj, 1)
    tl, tp, lv, uvs, smooth = info
    keymap, out_vi, out_loop, out_uv, idx = {}, [], [], [], []
    for t in range(len(tl)):
        for l in tl[t]:
            vi = int(lv[l])
            u, v = round(float(uvs[l, 0]), 6), round(float(uvs[l, 1]), 6)
            key = (vi, u, v, -1 if smooth[tp[t]] else int(tp[t]))
            o = keymap.get(key)
            if o is None:
                o = len(out_vi)
                keymap[key] = o
                out_vi.append(vi)
                out_loop.append(int(l))
                out_uv.append((u, 1.0 - v))
            idx.append(o)
    V, I, F = len(out_vi), len(idx), len(frames)
    out_vi = np.array(out_vi, np.int64)
    out_loop = np.array(out_loop, np.int64)
    out_uv = np.array(out_uv, np.float32)
    idx = np.array(idx, np.int64)

    # texture orientation: floor box triangles (polygons 0..5) must sample orange
    sel = np.where(tp < 6)[0]
    samp = []
    for t in sel:
        pts = [out_uv[keymap[(int(lv[l]), round(float(uvs[l, 0]), 6), round(float(uvs[l, 1]), 6), int(tp[t]))]]
               for l in tl[t]]
        c = np.mean(pts, axis=0)
        px = min(TEX - 1, int(c[0] * TEX))
        py = min(TEX - 1, int(c[1] * TEX))
        samp.append(tex[py, px].astype(float))
    mean = np.mean(samp, axis=0)
    print("floor texture sample mean RGB = (%.0f, %.0f, %.0f) over %d triangles" % (*mean, len(samp)))
    if not (mean[0] > mean[2] + 60):
        raise Fail("texture orientation check failed: floor does not sample orange")

    pos_all, col_all = [], []
    for f in frames:
        co, cn, _ = read_eval(obj, f)
        p = to_model(co)[out_vi]
        n = to_model(cn)[out_loop]
        n = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        s = 0.45 + 0.55 * np.maximum(0.0, n @ LIGHT)
        c = np.floor(255.0 * s + 0.5).astype(np.uint8)
        col = np.empty((V, 4), np.uint8)
        col[:, 0] = col[:, 1] = col[:, 2] = c
        col[:, 3] = 255
        pos_all.append(p.astype(np.float32))
        col_all.append(col)
    pos_all = np.stack(pos_all)  # (F, V, 3)

    # self-checks
    errs = []
    if not (1 <= V <= 65535):
        errs.append("vertexCount %d out of range" % V)
    if V > 2600:
        errs.append("vertexCount %d > 2600 budget" % V)
    if I <= 0 or I % 3 or I > 600000:
        errs.append("indexCount %d invalid" % I)
    if not (1 <= F <= 256):
        errs.append("frameCount invalid")
    if idx.max() >= V or idx.min() < 0:
        errs.append("index out of range")
    if out_uv.min() < 0 or out_uv.max() > 1:
        errs.append("uv outside 0..1")
    if not np.isfinite(pos_all).all():
        errs.append("non-finite positions")
    hmin, hmax = pos_all.reshape(-1, 3).min(0), pos_all.reshape(-1, 3).max(0)
    if hmin[0] < -30 or hmax[0] > 30:
        errs.append("hull x %.3f..%.3f outside -30..30" % (hmin[0], hmax[0]))
    if hmin[1] < 0:
        errs.append("hull y min %.3f < 0" % hmin[1])
    p0 = pos_all[0]
    tri = p0[idx.reshape(-1, 3)]
    vol = float(np.sum(np.einsum("ij,ij->i", tri[:, 0], np.cross(tri[:, 1], tri[:, 2]))) / 6.0)
    if vol <= 0:
        errs.append("signed volume %.1f <= 0 (winding inside out?)" % vol)
    ntot = 20 + 24 * len(CLIPS) + 8 * V + 16 * V * F + 2 * I
    if ntot >= 1200000:
        errs.append("file size %d >= 1.2 MB" % ntot)
    if errs:
        raise Fail("self-check failed: " + "; ".join(errs))

    parts = [struct.pack("<4sIIII", b"HRA1", V, I, F, len(CLIPS))]
    first = 0
    for name, f0, n in CLIPS:
        parts.append(struct.pack("<16sII", name.encode("ascii"), first, n))
        first += n
    parts.append(out_uv.astype("<f4").tobytes())
    rec = np.empty((F, V), dtype=[("p", "<f4", 3), ("c", "u1", 4)])
    rec["p"] = pos_all
    rec["c"] = np.stack(col_all)
    parts.append(rec.tobytes())
    parts.append(idx.astype("<u2").tobytes())
    hra = b"".join(parts)
    if len(hra) != ntot:
        raise Fail("size mismatch: %d vs formula %d" % (len(hra), ntot))
    rgba = np.empty((TEX, TEX, 4), np.uint8)
    rgba[..., :3] = tex
    rgba[..., 3] = 255
    hrt = struct.pack("<4sII", b"HRT1", TEX, TEX) + rgba.tobytes()
    if len(hrt) != 12 + TEX * TEX * 4:
        raise Fail("hrt size mismatch")

    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "anim.hra"), "wb") as fh:
        fh.write(hra)
    with open(os.path.join(outdir, "anim.hrt"), "wb") as fh:
        fh.write(hrt)
    if blend:
        bpy.ops.wm.save_as_mainfile(filepath=blend, compress=False)
    print("anim.hra: vertices=%d indices=%d (triangles=%d) frames=%d clips=%d bytes=%d" %
          (V, I, I // 3, F, len(CLIPS), len(hra)))
    print("anim.hrt: %dx%d bytes=%d" % (TEX, TEX, len(hrt)))
    print("hull (all frames, model) min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f)" % (*hmin, *hmax))
    print("frame-1 hull min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f), signed volume %.0f" %
          (*p0.min(0), *p0.max(0), vol))


def main():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    blend = None
    if "--blend" in args:
        k = args.index("--blend")
        blend = args[k + 1]
        del args[k:k + 2]
    if len(args) != 1:
        raise Fail("usage: blender -b --factory-startup --python hires_blender_driver.py -- <out dir> [--blend f]")
    outdir = args[0]
    clear_scene()
    coll = bpy.context.scene.collection
    tex = build_texture()
    m = build_parts()
    body = build_body_object(m, coll)
    make_material(tex, body)
    arm = build_armature(coll)
    mod = body.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm
    add_ik(arm)
    for pb in arm.pose.bones:
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
    author_action(arm)
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 35
    export(body, tex, outdir, blend)


if __name__ == "__main__":
    try:
        main()
    except Fail as e:
        print("ERROR: %s" % (e,))
        sys.exit(1)
    except Exception:
        traceback.print_exc()
        sys.exit(1)
