# Creator Kit stand-in: a code-made kart and wheel that take the place of the kit files
# (kit-kart.glb, kit-kart-gltf.glb, kit-wheel.glb, kit-wheel-gltf.glb, kit-info.json) which normally come from
# the player's own game data. Tests and the guide pictures use it, so they never need game
# data. CC0 (templates/LICENSE). Everything here is our own design made only from code -
# simple lofted boxes, cylinders and a torus; no game data, no model of any game kart. Only
# the outer dimensions follow the retail numbers of tools/rldpack_dummy.inc, so that the
# stand-in fits the same places (outline, floor line, seat, steering wheel, wheel points).
#
#   blender --background --factory-startup --python creator_kit_standin.py -- <out_dir>
#
# or from another Blender script (sys.path = the folder of this file):
#
#   import creator_kit_standin
#   info = creator_kit_standin.write_standin(out_dir)
#
# Units and axes (as include/rldhires.inc): model units "u", 64 u = 1 m; glTF axes x = kart left,
# y = up, z = kart front; Blender X = x / 64, Y = -z / 64, Z = y / 64 (Bv below). Vertex
# colours COLOR_0 are PS1-style baked shading: the sRGB byte of each value IS the PS1 byte,
# 128 = neutral (kit-kart.glb, kit-wheel.glb); kit-kart-gltf.glb has the same kart with each
# byte g = min(255, round(255 * b / 128)) (neutral becomes white, lights clamp); kit-wheel-gltf.glb
# likewise for the wheel.
# Deterministic: no random numbers, no time, a fixed vertex order - two runs give the same
# bytes for all five files (same Blender version).

import hashlib
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

UNIT = 64.0

# ----------------------------------------------------------------- retail numbers (rldpack_dummy.inc)
KART_BOX = (-27.9, 5.6, -54.1, 28.3, 52.1, 58.3)
OUTLINE_RIGHT = ((-54.1, 19.0), (-49.0, 25.8), (-27.5, 28.3), (53.2, 28.3), (58.3, 0.0))      # (z, x) of +x side
OUTLINE_LEFT = ((-54.1, -18.6), (-49.0, -25.4), (-27.5, -27.9), (53.2, -27.9), (58.3, 0.0))   # (z, x) of -x side
BOTTOM = ((-53.0, 36.3), (-50.0, 33.9), (-46.0, 17.9), (-42.0, 10.9), (-40.0, 10.7), (-30.0, 9.4), (-20.0, 8.2),
          (-10.0, 6.9), (0.0, 5.7), (10.0, 5.9), (20.0, 6.2), (30.0, 6.5), (40.0, 6.8), (50.0, 7.2), (55.0, 7.3),
          (58.0, 7.4))
SEAT = (0.0, 10.2, 0.9)
SEAT_Y = 10.2
STEER_CENTER = (0.0, 38.0, 24.0)
STEER_NORMAL = (0.0, -0.316, 0.949)
STEER_RADIUS = 10.5
STEER_TUBE = 1.2
WHEELS = {"wheel_fl": (36.0, 16.0, 49.75), "wheel_fr": (-36.0, 16.0, 49.75),
          "wheel_rl": (36.0, 16.0, -24.0), "wheel_rr": (-36.0, 16.0, -24.0)}
WHEEL_RADIUS = 16.0
WHEEL_HALF_WIDTH = 6.0

# PS1-style shading (as psx_colors() of tools/hires_glb_driver.py; Blender axes: -Y is the front)
PSX_LIGHT = (0.25, -0.35, 1.0)
PSX_NORMAL_SHARE = 0.7
PSX_DEAD_ZONE = 0.22
PSX_SPREAD = 90.0
PSX_RANGE = (40, 216)

PLATE = 0.6          # thickness of the floor plate
CELL = 32            # kart texture: 128 x 128 px in a 4 x 4 grid of 32 px cells


def Bv(p):
    return Vector((p[0] / UNIT, -p[2] / UNIT, p[1] / UNIT))


# ----------------------------------------------------------------- small vector helpers (game axes)
def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vadd(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vmul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def vnorm(a):
    n = math.sqrt(vdot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n)


def interp(table, z):
    """Piecewise linear over ((z, value), ...), the ends held."""
    if z <= table[0][0]:
        return table[0][1]
    for i in range(1, len(table)):
        if z <= table[i][0]:
            t = (z - table[i - 1][0]) / (table[i][0] - table[i - 1][0])
            return table[i - 1][1] + t * (table[i][1] - table[i - 1][1])
    return table[-1][1]


def stations(z0, z1, dz, extra=()):
    """Sorted z stations from z0 to z1 at most dz apart, plus the extra ones inside."""
    n = max(1, int(math.ceil((z1 - z0) / dz - 1e-9)))
    zs = [z0 + (z1 - z0) * i / n for i in range(n + 1)]
    zs += [z for z in extra if z0 < z < z1]
    zs = sorted(set(round(z, 6) for z in zs))
    return zs


def bottom_at(z):
    return interp(BOTTOM, z)


def xr_out(z):
    return max(interp(OUTLINE_RIGHT, z), 1.5)


def xl_out(z):
    return min(interp(OUTLINE_LEFT, z), -1.5)


# ----------------------------------------------------------------- parts: lofts of rings
class Part:
    """A solid made of rings of equal point count (game axes); faces between neighbouring rings, flat caps at
    the ends (or a closed loop). cells = (side cell, top cell) of the texture grid, (column, row from the top)."""

    def __init__(self, rings, cells, closed=False, cap_start=True, cap_end=True, smooth=False):
        self.rings = rings
        self.cells = cells
        self.closed = closed
        self.cap_start = cap_start
        self.cap_end = cap_end
        self.smooth = smooth


def box_ring(z, xl, xr, y0, y1):
    return [(xl, y0, z), (xr, y0, z), (xr, y1, z), (xl, y1, z)]


def circle_ring(center, axis, e1, e2, radius, count):
    """count points around center in the plane spanned by e1, e2 (unit, perpendicular to axis)."""
    out = []
    for i in range(count):
        a = 2.0 * math.pi * i / count
        d = vadd(vmul(e1, math.cos(a)), vmul(e2, math.sin(a)))
        out.append(vadd(center, vmul(d, radius)))
    return out


def plane_basis(axis):
    """Two unit vectors perpendicular to the unit axis: e1 'up' in the plane (the part of Y), e2 = e1 x axis."""
    y = (0.0, 1.0, 0.0)
    e1 = vnorm(vsub(y, vmul(axis, vdot(y, axis))))
    return e1, vcross(e1, axis)


def _loft_faces(part):
    """Yield point-index lists [(ring, point), ...] of every face, wound so that the normal points away from the
    middle of its ring pair (outward for the convex rings used here)."""
    rings = part.rings
    n = len(rings[0])
    faces = []
    count = len(rings) if part.closed else len(rings) - 1
    for i in range(count):
        a, b = i, (i + 1) % len(rings)
        c_seg = vmul(tuple(sum(p[k] for p in rings[a] + rings[b]) for k in range(3)), 1.0 / (2 * n))
        for j in range(n):
            j2 = (j + 1) % n
            quad = [(a, j), (a, j2), (b, j2), (b, j)]
            faces.append((quad, c_seg))
    if not part.closed:
        for ring_index, neighbour, want in ((0, 1, part.cap_start), (len(rings) - 1, len(rings) - 2, part.cap_end)):
            if not want:
                continue
            c_seg = vmul(tuple(sum(p[k] for p in rings[ring_index] + rings[neighbour]) for k in range(3)), 1.0 / (2 * n))
            faces.append(([(ring_index, j) for j in range(n)], c_seg))
    return faces


def _face_normal(pts):
    """Newell normal."""
    nx = ny = nz = 0.0
    for i in range(len(pts)):
        p, q = pts[i], pts[(i + 1) % len(pts)]
        nx += (p[1] - q[1]) * (p[2] + q[2])
        ny += (p[2] - q[2]) * (p[0] + q[0])
        nz += (p[0] - q[0]) * (p[1] + q[1])
    return (nx, ny, nz)


def add_part(bm, uv_layer, part, origin, cell_count, cell_size_px):
    """Add a part to the bmesh (vertices converted with Bv relative to origin); returns nothing."""
    rings = part.rings
    allp = [p for r in rings for p in r]
    lo = [min(p[k] for p in allp) for k in range(3)]
    hi = [max(p[k] for p in allp) for k in range(3)]
    verts = [[bm.verts.new(Bv(vsub(p, origin))) for p in r] for r in rings]
    grid = cell_count
    inset = 2.0
    for idx, c_seg in _loft_faces(part):
        pts = [rings[a][b] for a, b in idx]
        nrm = _face_normal(pts)
        mid = vmul(tuple(sum(p[k] for p in pts) for k in range(3)), 1.0 / len(pts))
        if vdot(nrm, vsub(mid, c_seg)) < 0.0:
            idx = idx[::-1]
            pts = pts[::-1]
            nrm = vmul(nrm, -1.0)
        length = math.sqrt(vdot(nrm, nrm)) or 1.0
        nrm = vmul(nrm, 1.0 / length)
        face = bm.faces.new([verts[a][b] for a, b in idx])
        face.smooth = part.smooth
        cx, cy = part.cells[1] if nrm[1] > 0.6 else part.cells[0]
        ax = max(range(3), key=lambda k: abs(nrm[k]))
        pair = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[ax]
        size = cell_size_px * grid
        for loop, p in zip(face.loops, pts):
            fu = (p[pair[0]] - lo[pair[0]]) / max(hi[pair[0]] - lo[pair[0]], 1e-6)
            fv = (p[pair[1]] - lo[pair[1]]) / max(hi[pair[1]] - lo[pair[1]], 1e-6)
            px = cx * cell_size_px + inset + fu * (cell_size_px - 2 * inset)
            py = cy * cell_size_px + inset + (1.0 - fv) * (cell_size_px - 2 * inset)
            loop[uv_layer].uv = (px / size, 1.0 - py / size)


def make_mesh_object(name, parts, origin, material, cell_count, cell_size_px):
    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("UVMap")
    for part in parts:
        add_part(bm, uv_layer, part, origin, cell_count, cell_size_px)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    obj.location = Bv(origin)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def triangle_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


# ----------------------------------------------------------------- the texture (made in code)
def kart_pixels():
    """128 x 128 RGBA floats (bottom row first). 4 x 4 cells of 32 px, cell (column, row from the top):
    (0,0) red paint with panel lines  (1,0) blue paint  (2,0) white stripe on yellow  (3,0) yellow paint
    (0,1) dark seat with stitches     (1,1) grey metal, brushed  (2,1) dark metal  (3,1) black rubber
    (0,2) number-less panel pattern (white / grey squares)  other cells: neutral grey."""
    size = 128
    px = np.zeros((size, size, 3), np.uint8)
    for y in range(size):
        for x in range(size):
            cx, cy = x // CELL, y // CELL
            lx, ly = x % CELL, y % CELL
            if (cx, cy) in ((0, 0), (1, 0), (3, 0)):
                base = {(0, 0): (196, 48, 44), (1, 0): (46, 92, 196), (3, 0): (232, 184, 40)}[(cx, cy)]
                c = tuple(int(v * 0.78) for v in base) if (lx % 16 == 0 or ly % 16 == 0) else base
            elif (cx, cy) == (2, 0):
                c = (240, 200, 40) if 12 <= ly < 20 else (236, 236, 230)
            elif (cx, cy) == (0, 1):
                c = (70, 68, 74) if lx % 8 == 0 else (52, 50, 56)
            elif (cx, cy) == (1, 1):
                c = (152, 154, 160) if ly % 4 == 0 else (140, 142, 148)
            elif (cx, cy) == (2, 1):
                c = (80, 82, 88)
            elif (cx, cy) == (3, 1):
                c = (36, 36, 40)
            elif (cx, cy) == (0, 2):
                c = (232, 232, 232) if ((lx // 8) + (ly // 8)) % 2 == 0 else (150, 150, 156)
            else:
                c = (128, 128, 128)
            px[y, x] = c
    return px


def wheel_pixels():
    """32 x 32, 2 x 2 cells of 16 px: (0,0) rubber with grooves, (1,0) rim grey, (0,1) hub yellow, (1,1) dark."""
    size = 32
    px = np.zeros((size, size, 3), np.uint8)
    for y in range(size):
        for x in range(size):
            cx, cy = x // 16, y // 16
            lx, ly = x % 16, y % 16
            if (cx, cy) == (0, 0):
                c = (52, 52, 56) if lx % 4 == 0 else (38, 38, 42)
            elif (cx, cy) == (1, 0):
                c = (170, 172, 178) if ly % 4 else (150, 152, 158)
            elif (cx, cy) == (0, 1):
                c = (232, 184, 40)
            else:
                c = (70, 70, 76)
            px[y, x] = c
    return px


def make_image(name, px):
    """Packed PNG image from RGB bytes (rows top first)."""
    h, w, _ = px.shape
    img = bpy.data.images.new(name, w, h, alpha=False)
    flat = np.ones((h, w, 4), np.float32)
    flat[:, :, :3] = px[::-1].astype(np.float32) / 255.0   # Blender rows run bottom up
    img.pixels.foreach_set(flat.ravel())
    img.filepath_raw = "//" + name + ".png"
    img.file_format = "PNG"
    img.pack()
    return img


def make_material(name, image):
    mat = bpy.data.materials.new(name)
    if mat.node_tree is None:
        mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Roughness"].default_value = 0.8
    bsdf.inputs["Metallic"].default_value = 0.0
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Closest"
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


# ----------------------------------------------------------------- the kart
# cells (side, top)
C_RED, C_BLUE, C_STRIPE, C_YELLOW = (0, 0), (1, 0), (2, 0), (3, 0)
C_SEAT, C_METAL, C_DARK, C_RUBBER, C_PANEL = (0, 1), (1, 1), (2, 1), (3, 1), (0, 2)


def kart_body_parts():
    parts = []
    floor_z = stations(-42.0, 58.3, 4.0, [b[0] for b in BOTTOM] + [-27.5, 53.2])
    # floor plate: the outline on the bottom line
    rings = [box_ring(z, xl_out(z), xr_out(z), bottom_at(z), bottom_at(z) + PLATE) for z in floor_z]
    parts.append(Part(rings, (C_DARK, C_DARK)))

    # side walls: inner face at |x| 13.5 so the cockpit stays free; low beside the driver, up to the hood in front
    wall_h = ((-12.0, 21.0), (8.0, 21.0), (20.0, 25.0), (31.0, 31.0), (58.3, 31.0))
    wall_z = stations(-12.0, 53.2, 4.0, [8.0, 20.0, 31.0])
    for sign in (1, -1):
        rings = []
        for z in wall_z:
            xs = sorted((13.5 * sign, 24.0 * sign))
            rings.append(box_ring(z, xs[0], xs[1], bottom_at(z) + PLATE, interp(wall_h, z)))
        parts.append(Part(rings, (C_RED, C_RED)))

    # side pods beside the rear half of the cockpit
    pod_h = ((-34.0, 17.0), (-26.0, 26.0), (-14.0, 26.0), (-6.0, 19.0))
    pod_z = stations(-34.0, -6.0, 4.0, [-26.0, -14.0])
    rings = [box_ring(z, 19.0, xr_out(z), bottom_at(z) + PLATE, interp(pod_h, z)) for z in pod_z]
    parts.append(Part(rings, (C_BLUE, C_YELLOW)))
    rings = [box_ring(z, xl_out(z), -19.0, bottom_at(z) + PLATE, interp(pod_h, z)) for z in pod_z]
    parts.append(Part(rings, (C_BLUE, C_YELLOW)))

    # rear deck behind the seat back
    deck_z = stations(-42.0, -12.0, 5.0)
    rings = [box_ring(z, -24.0, 24.0, bottom_at(z) + PLATE, 21.0) for z in deck_z]
    parts.append(Part(rings, (C_RED, C_PANEL)))

    # engine block at the back, z -54.1 .. -38, bottom on the floor line
    eng_top = ((-54.1, 42.0), (-46.0, 42.0), (-42.0, 38.0), (-38.0, 30.0))
    eng_z = stations(-54.1, -38.0, 2.0, [-53.0, -50.0, -49.0, -46.0, -42.0])
    rings = [box_ring(z, max(xl_out(z), -24.0), min(xr_out(z), 24.0), bottom_at(z), interp(eng_top, z)) for z in eng_z]
    parts.append(Part(rings, (C_DARK, C_METAL)))

    # hood, dash and nose: its underside at y 31 keeps the leg channel free below it
    hood_top = ((31.0, 44.0), (38.0, 44.0), (53.2, 36.0), (58.3, 30.0))   # low, so the driver stays in view
    hood_bot = ((31.0, 31.0), (53.2, 31.0), (58.3, 16.0))
    hood_z = stations(31.0, 58.3, 3.0, [38.0, 53.2])
    rings = [box_ring(z, xl_out(z), xr_out(z), interp(hood_bot, z), interp(hood_top, z)) for z in hood_z]
    parts.append(Part(rings, (C_RED, C_STRIPE)))

    # seat: the sitting surface is y 10.2 for z -7.5 .. 0 over x +-7.2 and dips to 8.9 at the front lip (z 9.3)
    seat_y = ((-7.5, SEAT_Y), (0.0, SEAT_Y), (9.3, 8.9))
    rings = [box_ring(z, -7.2, 7.2, bottom_at(z) + PLATE, interp(seat_y, z)) for z in (-7.5, -3.75, 0.0, 9.3)]
    parts.append(Part(rings, (C_SEAT, C_SEAT)))

    # seat back: behind z -7.6, leaning back, up to y 30
    back = []
    for t, y, z0, z1, hw in ((0, 7.0, -13.0, -7.6, 8.5), (1, 14.0, -14.2, -8.8, 8.3), (2, 22.0, -15.6, -10.2, 8.0), (3, 30.0, -17.0, -11.6, 7.6)):
        back.append([(-hw, y, z0), (hw, y, z0), (hw, y, z1), (-hw, y, z1)])
    parts.append(Part(back, (C_SEAT, C_SEAT)))
    return parts


def steer_parts():
    n = vnorm(STEER_NORMAL)
    u, t = plane_basis(n)
    c = STEER_CENTER
    parts = []
    # ring: torus about the centre, tube radius 1.2
    major, minor = 24, 8
    rings = []
    for i in range(major):
        a = 2.0 * math.pi * i / major
        d = vadd(vmul(u, math.cos(a)), vmul(t, math.sin(a)))
        tube_c = vadd(c, vmul(d, STEER_RADIUS))
        ring = []
        for k in range(minor):
            b = 2.0 * math.pi * k / minor
            ring.append(vadd(tube_c, vadd(vmul(d, STEER_TUBE * math.cos(b)), vmul(n, STEER_TUBE * math.sin(b)))))
        rings.append(ring)
    parts.append(Part(rings, (C_DARK, C_DARK), closed=True, smooth=True))
    # three spokes (a T): up-direction angles 90 (left), 210, 330 degrees around
    for deg in (90.0, 210.0, 330.0):
        a = math.radians(deg - 90.0)
        d = vadd(vmul(u, math.cos(a)), vmul(t, math.sin(a)))
        s = vcross(d, n)
        rings = []
        for r in (1.5, STEER_RADIUS - 0.4):
            p = vadd(c, vmul(d, r))
            rings.append([vadd(p, vadd(vmul(s, sx), vmul(n, sn))) for sx, sn in ((-0.9, -0.6), (0.9, -0.6), (0.9, 0.6), (-0.9, 0.6))])
        parts.append(Part(rings, (C_METAL, C_METAL)))
    # hub
    rings = []
    for tt, r in ((-1.4, 2.2), (1.0, 2.4), (1.8, 1.6)):
        rings.append(circle_ring(vadd(c, vmul(n, tt)), n, u, t, r, 12))
    parts.append(Part(rings, (C_YELLOW, C_YELLOW), smooth=True))
    # column from the hub into the dash (the hood's rear face at z 31)
    rings = [circle_ring(vadd(c, vmul(n, tt)), n, u, t, 1.1, 8) for tt in (1.0, 4.0, 8.5)]
    parts.append(Part(rings, (C_METAL, C_METAL), cap_start=False, smooth=True))
    return parts


# ----------------------------------------------------------------- the wheel
def wheel_parts():
    sides = 24
    x_axis = (1.0, 0.0, 0.0)
    e1, e2 = (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
    prof = [(-6.0, 13.0), (-5.4, 14.6), (-4.0, 15.0), (4.0, 15.0), (5.4, 14.6), (6.0, 13.0),
            (5.2, 11.6), (5.0, 10.2), (5.6, 9.6), (5.6, 3.0), (6.6, 2.6), (6.9, 1.2)]
    rings = [circle_ring((x, 0.0, 0.0), x_axis, e1, e2, r, sides) for x, r in prof]
    parts = [Part(rings[0:6], ((0, 0), (0, 0)), cap_end=False, smooth=True),
             Part(rings[5:10], ((1, 0), (1, 0)), cap_start=False, cap_end=False, smooth=True),
             Part(rings[9:12], ((0, 1), (0, 1)), cap_start=False, smooth=True)]
    # 16 tread lugs: boxes standing on the tread, top at the tyre radius 16
    for k in range(16):
        a = 2.0 * math.pi * k / 16
        d = (0.0, math.cos(a), math.sin(a))
        s = (0.0, -math.sin(a), math.cos(a))
        lug = []
        for r in (14.4, WHEEL_RADIUS):
            lug.append([vadd(vmul(d, r), vadd(vmul(x_axis, ax), vmul(s, sv))) for ax, sv in ((-4.6, -1.7), (4.6, -1.7), (4.6, 1.7), (-4.6, 1.7))])
        parts.append(Part(lug, ((1, 1), (1, 1))))
    return parts


# ----------------------------------------------------------------- vertex colours
def srgb_to_linear(c):
    c = np.asarray(c, np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def psx_bytes_for(objs):
    """PS1-style shading bytes (128 neutral, 40..216) per vertex of every object; median taken over the first."""
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
    for shade in shades:
        d = shade - median
        d = np.sign(d) * np.maximum(np.abs(d) - PSX_DEAD_ZONE, 0.0)
        out.append(np.clip(np.round(128.0 + PSX_SPREAD * d), PSX_RANGE[0], PSX_RANGE[1]).astype(np.int64))
    return out


def set_colors(obj, byte_values):
    """Grey colour attribute "Col" (POINT, float): the sRGB byte goes in through the sRGB curve, so the exporter's
    linear float is read back as the same byte."""
    me = obj.data
    while me.color_attributes:
        me.color_attributes.remove(me.color_attributes[0])
    lin = srgb_to_linear(np.asarray(byte_values, np.float64) / 255.0)
    rgba = np.stack([lin, lin, lin, np.ones_like(lin)], 1).astype(np.float32)
    attr = me.color_attributes.new(name="Col", type="FLOAT_COLOR", domain="POINT")
    attr.data.foreach_set("color", rgba.ravel())
    me.color_attributes.active_color = attr
    me.color_attributes.render_color_index = me.color_attributes.active_color_index
    me.update()


# ----------------------------------------------------------------- scene, export, info
def build_kart():
    """The stand-in kart in the current scene: returns (kart_body, kart_steer_wheel, [bytes body, bytes steer]), PS1 shading set."""
    mat = make_material("kart_standin", make_image("kart_standin", kart_pixels()))
    body = make_mesh_object("kart_body", kart_body_parts(), (0.0, 0.0, 0.0), mat, 4, CELL)
    steer = make_mesh_object("kart_steer_wheel", steer_parts(), STEER_CENTER, mat, 4, CELL)
    colours = psx_bytes_for([body, steer])
    set_colors(body, colours[0])
    set_colors(steer, colours[1])
    return body, steer, colours


def build_wheel():
    mat = make_material("wheel_standin", make_image("wheel_standin", wheel_pixels()))
    wheel = make_mesh_object("wheel", wheel_parts(), (0.0, 0.0, 0.0), mat, 2, 16)
    set_colors(wheel, psx_bytes_for([wheel])[0])
    return wheel


def export_glb(path, objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.gltf(
        filepath=path, export_format="GLB", export_yup=True, use_selection=True,
        export_skins=False, export_animations=False, export_morph=False, export_apply=False,
        export_vertex_color="ACTIVE", export_tangents=False, export_try_sparse_sk=False,
        export_cameras=False, export_lights=False, export_extras=False,
        export_image_format="AUTO", export_optimize_animation_size=False,
        export_draco_mesh_compression_enable=False, export_meshopt_compression_enable=False)


def sha256_file(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def write_standin(out_dir):
    """Builds and exports the stand-in kit into out_dir; returns the kit-info dict that was written."""
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)

    body, steer, colours = build_kart()
    kart_tris = triangle_count(body) + triangle_count(steer)
    kart_path = os.path.join(out_dir, "kit-kart.glb")
    export_glb(kart_path, [body, steer])

    # glTF version: each byte g = min(255, round(255 * b / 128))
    for o, bytes_ in zip((body, steer), colours):
        set_colors(o, np.minimum(255, np.floor(255.0 * bytes_ / 128.0 + 0.5)).astype(np.int64))
    gltf_path = os.path.join(out_dir, "kit-kart-gltf.glb")
    export_glb(gltf_path, [body, steer])

    for o in (body, steer):
        bpy.data.objects.remove(o)
    wheel = build_wheel()
    wheel_tris = triangle_count(wheel)
    wheel_path = os.path.join(out_dir, "kit-wheel.glb")
    export_glb(wheel_path, [wheel])
    # glTF version of the wheel, the same conversion as the kart's
    wheel_bytes = psx_bytes_for([wheel])[0]
    set_colors(wheel, np.minimum(255, np.floor(255.0 * wheel_bytes / 128.0 + 0.5)).astype(np.int64))
    wheel_gltf_path = os.path.join(out_dir, "kit-wheel-gltf.glb")
    export_glb(wheel_gltf_path, [wheel])

    normal = vnorm(STEER_NORMAL)
    info = {
        "kit": 1,
        "source": "stand-in",
        "driver": None,
        "units_per_metre": 64,
        "seat": list(SEAT),
        "steer": {"center": list(STEER_CENTER), "axis": [round(v, 6) for v in normal],
                  "normal": [round(v, 6) for v in normal], "radius": STEER_RADIUS},
        "kart_box": list(KART_BOX),
        "floor": [[z, y] for z, y in BOTTOM],
        "wheels": {k: list(v) for k, v in WHEELS.items()},
        "wheel_radius": WHEEL_RADIUS,
        "wheel_half_width": WHEEL_HALF_WIDTH,
        "files": {
            "kit-kart.glb": {"vcol": "psx", "sha256": sha256_file(kart_path), "triangles": kart_tris},
            "kit-kart-gltf.glb": {"vcol": "gltf", "sha256": sha256_file(gltf_path), "triangles": kart_tris},
            "kit-wheel.glb": {"vcol": "psx", "sha256": sha256_file(wheel_path), "triangles": wheel_tris},
            "kit-wheel-gltf.glb": {"vcol": "gltf", "sha256": sha256_file(wheel_gltf_path), "triangles": wheel_tris},
        },
    }
    with open(os.path.join(out_dir, "kit-info.json"), "wb") as fh:
        fh.write((json.dumps(info, sort_keys=True, indent=1) + "\n").encode("utf-8"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return info


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 1:
        print("usage: blender --background --factory-startup --python creator_kit_standin.py -- <out_dir>")
        sys.exit(2)
    info = write_standin(args[0])
    print("stand-in written:", json.dumps({k: v["triangles"] for k, v in info["files"].items()}))


if __name__ == "__main__":
    main()
