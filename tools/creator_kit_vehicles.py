# Creator Kit "Dummy Hover" and "Dummy Plane": two demo vehicles for the
# Creator Kit figure, made entirely by code in this file (boxes, lofted rings,
# cylinders - no external asset, no reference picture, no game data). Each
# vehicle is written as its own .glb, without the figure and without the kit
# kart. The kit kart is only MEASURED at run time from the kit file (its
# bounding box = the footprint, the guide SeatPoint = the seat), so the
# vehicles are as long and as wide as the kart and the figure sits where it
# sits in the kart: every seated driver clip of the kit fits. No kart number
# is written into this script, and nothing of the kart (no vertex, colour or
# texture) goes into a vehicle file.
#
#   blender --threads 1 --background --factory-startup --python-exit-code 1 \
#       --python tools/creator_kit_vehicles.py -- --kit-blend <creator-kit-hires.blend> --out <folder>
#
# Options:
#   --kit-blend <file>   the Hi-Res template of the Creator Kit (Reload Studio,
#                        Character > Creator Kit > Generate demo files, way
#                        Hi-Res): <kit>\hires\creator-kit-hires.blend (required)
#   --out <folder>       output folder (required, created if missing)
#   --no-scene           do not write the preview scenes (.blend)
#
# Output in <folder>, per vehicle <v> = dummy-hover, dummy-plane:
#   <v>.glb              the vehicle alone: a root node "kart" with the body,
#                        the moving parts and the morph targets (shape keys).
#                        No figure, no skin, no clips. This is the file to share.
#   <v>-driver.glb       the driver to pack into a character: the kit figure
#                        (puppet without backpack, skeleton, clips) seated in
#                        the vehicle, written by the kit's stored CreatorKit
#                        exporter (morph targets on) with the vehicle in the
#                        collection Kart in place of the kit kart: the vehicle
#                        parts are rigid nodes under the joint "kart" as the
#                        kit kart's were (the plane's own wheel nodes wheel_fl,
#                        wheel_fr, wheel_rl, wheel_rr among them; the hover has
#                        none), the steering part on the joint "kart_steer" as
#                        the kit's steering wheel was, so the clips steer_left,
#                        steer_right, drift_left, drift_right turn it (the joint
#                        stays in the skin). Pack it as a Hi-Res driver with
#                        its own kart (rldpack hires-add --kart-part). It holds
#                        nothing of the game: the figure is made by the kit's
#                        code and the kit kart is removed before (self check:
#                        no kit mark, no kit material or image in the file).
#   <v>-szene.blend      the preview scene: the kit file with the kit kart
#                        removed and the vehicle in its place, the figure (no
#                        backpack) in the seat. It holds the kit's data: keep it
#                        on this computer.
#   LIESMICH.txt         files, shape keys, triangles and the measured checks
#
# Dummy Hover (no wheels): a flat sled - two fat side pontoons, a low floor
# with a seat cushion, a wedge nose with a wide bumper, an engine block behind
# the seat with two round thrusters and a fin, four hover pads underneath.
#   mesh hover_body, shape keys lean_left, lean_right (the body rolls about the
#   kart's long axis through the seat), nose_up (pitch about the cross axis on
#   the floor at the toes, so the floor sinks away from the feet), nose_down
#   (pitch about the cross axis through the seat). Steering: node handlebar -
#   a straight bar through both fists with dark grip sleeves, riser, clamp and a
#   short column; a fixed headset and down tube (part of hover_body) carry it.
# Dummy Plane: a short fat fuselage (a nose cowl with a tunnel for the legs,
# low cockpit walls, a tapering tail cone with a head fairing), stub wings with
# flaps, a tail with fin, rudder, tailplane and elevators, four small wheels on
# struts and a two-blade propeller.
#   mesh plane_body, shape keys elevator_up, elevator_down, rudder_left,
#   rudder_right, flaps_down (each turns its control surfaces about their hinge
#   lines); node spin_prop (spinner + blades, origin on the propeller shaft,
#   the shaft along the node's local X axis - the axis the engine turns a wheel
#   node about - with local +X turned to the kart's front: the node is rotated
#   -90 degrees about the up axis); nodes wheel_fl, wheel_fr, wheel_rl,
#   wheel_rr (origin in the wheel middle, the axle along the node's local X, as
#   the game's wheel rule says). Blender X stays glTF X in the +Y-up export.
#   Steering: node steer_wheel - a yoke: a rim arc over the top through both
#   fists, one spoke up from a yellow hub; a fixed column housing out of the cowl (part of
#   plane_body) carries it.
# THE STEERING PART. The kit's steering wheel hangs on the joint kart_steer and
# the clips turn that joint about its Y axis (the column). The steering parts
# are laid out in that joint's frame as measured from the kit file at run time:
# the pivot (the joint's head), the column, the tilt of the kit's wheel and the
# two fist centres (the mean of each hand's vertices in the rest pose, where the
# kit's rim runs through the fists). As the kit's wheel, the grip runs through
# the fists: the hover's grip axis is the line through both fist centres, the
# plane's yoke an arc of the circle through them (centre on the column, tilted
# by the kit wheel's angle, see RIM_TILT; open at the bottom, see YOKE_ARC); the
# deviation is measured (limit GRIP_DEV_MAX). The pieces the
# hands close around are named grip_*; the check allows contact between them
# and the hands only. The steering node's origin is the pivot, its
# axes are the world's; the column is the node's local -Y (Blender), the
# kart's forward axis. No shape keys: the joint turns it.
# Every shape key holds an absolute shape (Blender's glTF exporter writes each
# key minus the Basis); the keys are independent, the Basis is the neutral pose.
#
# Self check (exit 5 when one fails; the outputs are removed then): every piece
# is closed and consistently wound; in the rest pose and in every frame of the
# seated clips idle, steer_left, steer_right, jump, drift_left, drift_right,
# hit, finish_win, finish_lose the figure and the vehicle (body, wheels and
# propeller as placed, and the volume a spinning wheel or propeller sweeps)
# have no crossing triangle pair and no vertex inside each other (the minimum
# clearance is reported; the shape keys at 1.0 are reported as information);
# the steering part is taken where each frame's joint kart_steer puts it: its
# grip_* pieces touch nothing of the figure but the hands (the hands close
# around them; at least one frame must show that contact), every other piece
# touches nothing of the figure; it touches the body in no frame, nor
# when turned STEER_TURN degrees either way; the driver file keeps the joint
# kart_steer in its skin with the steering part on it and steer_left /
# steer_right turning it;
# the plane's wheels and propeller turned in 15 degree steps do not touch the
# body (wings included); the propeller and wheel pivots lie on their axes
# (radial distance of the mesh centre from the axis) and the propeller is
# symmetric under a half turn about its axis; the .glb has the node and morph
# target names written here, no skin, no animation, no kit part; the triangle
# limits hold.
#
# Exit codes (as templates/make_creator_kit.py): 0 good, 2 wrong call, 3 wrong
# Blender version, 4 input missing or invalid, 5 the self check failed. Same
# Blender and same kit file give the same bytes of each .glb (start Blender with
# --threads 1, as the kit does).
#
# Axes as the kit: Blender X = kart left, -Y = kart front, +Z up, 1 unit = 1 m.

import math
import os
import struct
import json
import sys

import bpy
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

sys.dont_write_bytecode = True

USAGE = "usage: creator_kit_vehicles.py -- --kit-blend <creator-kit-hires.blend> --out <folder> [--no-scene]"
BLENDER_VERSION = (5, 2)
README_NAME = "LIESMICH.txt"
ROOT_NODE = "kart"            # the root node of a vehicle file (the game's name for the kart part)
KART_BONE = "kart"            # the kit joint the kit kart hangs on
STEER_BONE = "kart_steer"     # the kit joint the kit's steering wheel hangs on; the clips turn it
HAND_GROUPS = ("hand.", "fingers.", "thumb.")   # vertex groups of the puppet's hands (name prefixes)
YOKE_ARC = (8.0, 172.0)       # degrees in the rim's plane (0 = towards +side, 90 = up): the plane's yoke arc
YOKE_SEGMENTS = 20
RIM_TILT = -1.0               # the plane's rim leans as the kit's wheel times this: -1 = the kit's angle, but the
                              # top towards the driver (measured: with the kit's own lean, or upright, the arc
                              # touches the thighs in jump; leaning this way it touches nothing but the hands)
GRIP_DEV_MAX = 0.003          # m: the grip axis (hover) or rim circle (plane) runs this close to each fist centre
STEER_TURN = 60               # degrees: the steering part turned this far both ways must not touch the body
PLANE_COWL_BACK = 0.645       # the plane's cowl ends this far in front of the seat (shared by both designs)
SEATED_CLIPS = ("idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit",
                "finish_win", "finish_lose")
RAY_DIRS = ((1.0, 0.013, 0.007), (0.011, 1.0, 0.017), (0.019, 0.009, 1.0))
NEAR = 0.25                   # clearance search radius (m); a larger gap is reported as ">= NEAR"
SPIN_STEP = 15                # degrees between the tested wheel and propeller turns
PROP_ROT = (0.0, 0.0, -0.5 * math.pi)   # the propeller node: local +X (the shaft) to the kart's front (-Y)
PIVOT_TOL = 1e-6              # m: mesh centre off the axis, and the propeller's half-turn symmetry
TRI_LIMIT = 4000              # per vehicle
BUDGET = 64000                # the game's budget for a whole file (figure + vehicle)
# What the export changes in the kit's stored exporters: morph targets on (normals with them, no tangents, dense
# as the kit's export_try_sparse_sk False). The vehicle file has no animation of its own.
EXPORT_MORPH = {"export_morph": True, "export_morph_normal": True, "export_morph_tangent": False}

# ---- colours: a 64 x 64 atlas of flat 16 x 16 fields per vehicle (made up here)
ATLAS = 64
HOVER_RGB = {"hull": (38, 92, 196), "trim": (250, 198, 36), "dark": (46, 48, 56), "glow": (96, 236, 255),
             "metal": (156, 160, 170), "seat": (34, 34, 40), "nose": (232, 80, 40)}
PLANE_RGB = {"body": (206, 48, 40), "wing": (240, 226, 190), "ctrl": (250, 198, 36), "glass": (150, 206, 236),
             "seat": (34, 34, 40), "strut": (120, 124, 132), "tyre": (30, 30, 32), "hub": (190, 192, 198),
             "blade": (58, 46, 40), "spinner": (250, 198, 36)}
# PS1 grey shading as creator_kit_puppet.build_puppet (Hi-Res steps), as the glider
PSX_LIGHT = (0.25, -0.35, 1.0)
PSX_NORMAL_SHARE = 0.7
PSX_DEAD_ZONE = 0.22
PSX_SPREAD = 90.0
PSX_RANGE = (40, 216)
PSX_STEPS = (12, 44, 212)

# ---- shape key angles (degrees)
HOVER_LEAN = 8.0
HOVER_PITCH = 6.0
PLANE_ELEVATOR = 25.0
PLANE_RUDDER = 25.0
PLANE_FLAPS = 35.0


def fail(code, text):
    print("dummy vehicles: " + text)
    sys.exit(code)


def log(text):
    print("dummy vehicles: " + text)
    sys.stdout.flush()


def parse_args(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    opt = {"kit_blend": None, "out": None, "scene": True}
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--kit-blend" and i + 1 < len(args):
            opt["kit_blend"] = os.path.abspath(args[i + 1])
            i += 2
        elif a == "--out" and i + 1 < len(args):
            opt["out"] = os.path.abspath(args[i + 1])
            i += 2
        elif a == "--no-scene":
            opt["scene"] = False
            i += 1
        else:
            fail(2, "unknown argument '%s'. %s" % (a, USAGE))
    if opt["kit_blend"] is None or opt["out"] is None:
        fail(2, USAGE)
    return opt


# ------------------------------------------------------------------ the kit file

def open_kit(path):
    if not os.path.isfile(path):
        fail(4, "the kit file %s does not exist." % path)
    bpy.ops.wm.open_mainfile(filepath=path, load_ui=False)
    need = {"creator_rig": "ARMATURE", "puppet": "MESH", "SeatPoint": "EMPTY"}
    for name, kind in need.items():
        o = bpy.data.objects.get(name)
        if o is None or o.type != kind:
            fail(4, "%s has no %s '%s' - is it a Creator Kit template?" % (os.path.basename(path), kind.lower(), name))
    for name in ("CreatorKit", "Figure", "Kart"):
        if bpy.data.collections.get(name) is None:
            fail(4, "%s has no collection '%s'." % (os.path.basename(path), name))
    for name in ("CreatorKit", "Figure"):
        if len(bpy.data.collections[name].exporters) != 1:
            fail(4, "the collection %s has no stored exporter." % name)
    rig = bpy.data.objects["creator_rig"]
    if KART_BONE not in rig.data.bones:
        fail(4, "the skeleton has no bone '%s'." % KART_BONE)
    kart = sorted([o for o in bpy.data.collections["Kart"].all_objects if o.type == "MESH"], key=lambda o: o.name)
    if not kart:
        fail(4, "the collection Kart has no mesh.")
    for c in SEATED_CLIPS:
        if c not in bpy.data.actions:
            fail(4, "the kit file has no clip '%s'." % c)
    return rig, bpy.data.objects["puppet"], kart


def world_coords(obj, dg):
    ev = obj.evaluated_get(dg)
    mesh = ev.to_mesh()
    mw = ev.matrix_world.copy()
    co = [mw @ v.co for v in mesh.vertices]
    polys = [tuple(p.vertices) for p in mesh.polygons]
    ev.to_mesh_clear()
    return co, polys


def measure_kit(rig, kart):
    """The footprint (world bounding box of every mesh of the collection Kart, rest pose) and the seat (the guide
    SeatPoint). Measured from the kit file at every run; nothing of it is stored in this script."""
    stored = rig.data.pose_position
    rig.data.pose_position = "REST"
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    lo = [1e9] * 3
    hi = [-1e9] * 3
    parts = {}
    for o in kart:
        co, _p = world_coords(o, dg)
        plo = [min(v[a] for v in co) for a in range(3)]
        phi = [max(v[a] for v in co) for a in range(3)]
        parts[o.name] = (plo, phi)
        lo = [min(lo[a], plo[a]) for a in range(3)]
        hi = [max(hi[a], phi[a]) for a in range(3)]
    seat = bpy.data.objects["SeatPoint"].matrix_world.translation.copy()
    rig.data.pose_position = stored
    bpy.context.view_layer.update()
    return {"fx0": lo[0], "fx1": hi[0], "fy0": lo[1], "fy1": hi[1], "g": lo[2], "top": hi[2],
            "sx": seat.x, "sy": seat.y, "sz": seat.z, "parts": parts}


def measure_steer(rig, puppet, kart, scene):
    """The kit's steering: the joint it hangs on (its rest frame: origin = the pivot, Y = the column the clips turn
    it about), the tilt of the kit's steering wheel (the normal of its best-fit plane, in the joint's frame), and
    the two fists: per hand the centre (mean) of the puppet's vertices whose strongest weight is a hand, fingers or
    thumb bone, in the rest pose - the seated bind pose, in which the kit's rim runs through both fists. Measured
    from the kit file at every run; nothing of it is stored in this script."""
    wheel = [o for o in kart if o.parent_type == "BONE" and o.parent_bone == STEER_BONE]
    if not wheel:
        fail(4, "the kit has no steering wheel on the joint '%s'." % STEER_BONE)
    if STEER_BONE not in rig.data.bones:
        fail(4, "the skeleton has no bone '%s'." % STEER_BONE)
    stored = rig.data.pose_position
    set_clip(rig, scene, None, 0)
    M = rig.matrix_world @ rig.data.bones[STEER_BONE].matrix_local
    H = M.translation.copy()
    c = M.col[1].xyz.normalized()
    up = (M.col[2].xyz - c * M.col[2].xyz.dot(c)).normalized()
    side = c.cross(up).normalized()

    def sfu(v):
        d = v - H
        return Vector((d.dot(side), d.dot(c), d.dot(up)))
    dg = bpy.context.evaluated_depsgraph_get()
    # the kit wheel's plane (least squares through its vertices): the normal in (s, f, u), turned to point forward
    import numpy as np
    wv = []
    for w in wheel:
        co, _p = world_coords(w, dg)
        wv.extend(tuple(sfu(v)) for v in co)
    W = np.array(wv)
    normal = np.linalg.svd(W - W.mean(0))[2][2]
    if normal[1] < 0.0:
        normal = -normal
    groups = {g.index for g in puppet.vertex_groups if g.name.startswith(HAND_GROUPS)}
    hand = [v.index for v in puppet.data.vertices
            if v.groups and max(v.groups, key=lambda g: g.weight).group in groups]
    co, _p = world_coords(puppet, dg)
    fists, reach = {}, {}
    for sgn in (1, -1):
        pts = [sfu(co[i]) for i in hand if sfu(co[i]).x * sgn > 0.0]
        if not pts:
            fail(4, "the puppet has no hand on one side.")
        fists[sgn] = sum(pts, Vector()) / len(pts)
        reach[sgn] = (min(abs(p.x) for p in pts), max(abs(p.x) for p in pts))
    set_clip(rig, scene, "idle", 0)
    rig.data.pose_position = stored
    bpy.context.view_layer.update()
    return {"H": H, "c": c, "up": up, "side": side, "fists": fists, "reach": reach, "hand": set(hand),
            "normal": Vector((0.0, float(normal[1]), float(normal[2]))).normalized(),
            "normal_s": float(normal[0])}


# ------------------------------------------------------------------ geometry helpers
# A piece is (name, field, verts, tris): a closed, consistently wound triangle mesh in its object's local space.

def circle(n, r, start=0.0):
    """n points (u, v) on a circle; for even n point k + n/2 is exactly the negation of point k, so a half turn
    maps the ring onto itself bit for bit."""
    half = []
    for k in range(n // 2 if n % 2 == 0 else n):
        a = start + 2.0 * math.pi * k / n
        half.append((r * math.cos(a), r * math.sin(a)))
    if n % 2:
        return half
    return half + [(-u, -v) for u, v in half]


def octagon(u0, u1, v0, v1, c):
    """A rectangle with chamfered corners in (u, v), counter-clockwise from the lower edge."""
    c = min(c, 0.49 * (u1 - u0), 0.49 * (v1 - v0))
    return [(u0 + c, v0), (u1 - c, v0), (u1, v0 + c), (u1, v1 - c), (u1 - c, v1), (u0 + c, v1), (u0, v1 - c),
            (u0, v0 + c)]


def ring_y(y, pts2):
    """A ring at y from (x, z) points."""
    return [(u, y, v) for u, v in pts2]


def ring_x(x, pts2):
    """A ring at x from (y, z) points."""
    return [(x, u, v) for u, v in pts2]


def ring_z(z, pts2):
    """A ring at z from (x, y) points."""
    return [(u, v, z) for u, v in pts2]


def loft(rings, cap0=True, cap1=True):
    """Triangles through rings of equal length (quads split on a fixed diagonal), convex caps as fans."""
    n = len(rings[0])
    verts = [p for r in rings for p in r]
    tris = []
    for i in range(len(rings) - 1):
        b0, b1 = i * n, (i + 1) * n
        for k in range(n):
            a, b, c, d = b0 + k, b0 + (k + 1) % n, b1 + (k + 1) % n, b1 + k
            tris.append((a, b, c))
            tris.append((a, c, d))
    if cap0:
        for k in range(1, n - 1):
            tris.append((0, k + 1, k))
    if cap1:
        last = (len(rings) - 1) * n
        for k in range(1, n - 1):
            tris.append((last, last + k, last + k + 1))
    return verts, tris


def cup(outer, inner):
    """A closed solid with a cavity: the outer loft and the inner loft (both from the open end), the inner one turned
    inside out, joined by a rim at the open end (ring 0 of both)."""
    ov, ot = loft(outer, cap0=False, cap1=True)
    iv, it = loft(inner, cap0=False, cap1=True)
    n = len(outer[0])
    off = len(ov)
    verts = ov + iv
    tris = list(ot) + [(off + a, off + c, off + b) for a, b, c in it]
    for k in range(n):
        o0, o1 = k, (k + 1) % n
        i0, i1 = off + k, off + (k + 1) % n
        tris.append((o1, o0, i0))
        tris.append((o1, i0, i1))
    return verts, tris


def volume(verts, tris):
    s = 0.0
    for a, b, c in tris:
        s += Vector(verts[a]).dot(Vector(verts[b]).cross(Vector(verts[c])))
    return s / 6.0


def piece(name, field, vt):
    """Wind the piece outward (positive volume)."""
    verts, tris = vt
    if volume(verts, tris) < 0.0:
        tris = [(a, c, b) for a, b, c in tris]
    return {"name": name, "field": field, "verts": [tuple(float(x) for x in p) for p in verts], "tris": tris}


def box_y(name, field, x0, x1, y0, y1, z0, z1, c, end=None):
    """A chamfered box along y. end = chamfer of the two end faces (default c)."""
    e = c if end is None else end
    rings = [ring_y(y0, octagon(x0 + e, x1 - e, z0 + e, z1 - e, max(c - e, 0.002))),
             ring_y(y0 + e, octagon(x0, x1, z0, z1, c)),
             ring_y(y1 - e, octagon(x0, x1, z0, z1, c)),
             ring_y(y1, octagon(x0 + e, x1 - e, z0 + e, z1 - e, max(c - e, 0.002)))]
    return piece(name, field, loft(rings))


def sections_y(name, field, secs, c):
    """A loft along y through sections (y, x0, x1, z0, z1)."""
    rings = [ring_y(y, octagon(x0, x1, z0, z1, c)) for y, x0, x1, z0, z1 in secs]
    return piece(name, field, loft(rings))


def cylinder_axis(name, field, centre, axis, r_list, n, start=0.0):
    """A cylinder or cone along a main axis ('x', 'y' or 'z') through centre: r_list = [(offset, radius), ...]."""
    cx, cy, cz = centre
    rings = []
    for off, r in r_list:
        pts = circle(n, r, start)
        if axis == "x":
            rings.append([(cx + off, cy + u, cz + v) for u, v in pts])
        elif axis == "y":
            rings.append([(cx + u, cy + off, cz + v) for u, v in pts])
        else:
            rings.append([(cx + u, cy + v, cz + off) for u, v in pts])
    return piece(name, field, loft(rings))


def foil(le, te, zm, t):
    """A six-point wing section in (y, z): leading edge le (front, smaller y), trailing edge te."""
    ch = te - le
    return [(le, zm), (le + 0.18 * ch, zm - 0.5 * t), (te - 0.12 * ch, zm - 0.3 * t), (te, zm),
            (te - 0.12 * ch, zm + 0.3 * t), (le + 0.18 * ch, zm + 0.5 * t)]


def mirror_x(p, cx):
    """The piece mirrored at x = cx (left <-> right), winding kept outward."""
    verts = [(2.0 * cx - x, y, z) for x, y, z in p["verts"]]
    tris = [(a, c, b) for a, b, c in p["tris"]]
    name = p["name"].replace("_l", "_r") if p["name"].endswith("_l") else p["name"] + "_r"
    return {"name": name, "field": p["field"], "verts": verts, "tris": tris}


def rotate_about(verts, origin, axis, deg):
    m = Matrix.Rotation(math.radians(deg), 3, Vector(axis))
    o = Vector(origin)
    return [tuple(m @ (Vector(v) - o) + o) for v in verts]


def perp_frame(d):
    """Two unit vectors perpendicular to d and to each other (a fixed choice, the same every run)."""
    d = Vector(d).normalized()
    ref = Vector((0.0, 0.0, 1.0)) if abs(d.z) < 0.9 else Vector((1.0, 0.0, 0.0))
    e1 = d.cross(ref).normalized()
    return e1, d.cross(e1).normalized()


def tube(name, field, p0, p1, sections, n=10):
    """A tube along the straight line p0 -> p1: sections = [(t 0..1, radius), ...], capped at both ends."""
    p0, p1 = Vector(p0), Vector(p1)
    e1, e2 = perp_frame(p1 - p0)
    rings = []
    for t, r in sections:
        c = p0.lerp(p1, t)
        rings.append([tuple(c + e1 * u + e2 * v) for u, v in circle(n, r, math.pi / n)])
    return piece(name, field, loft(rings))


def torus(name, field, centre, axis, big, small, n_big=24, n_small=8):
    """A closed ring of radius big about axis through centre, tube radius small."""
    centre, axis = Vector(centre), Vector(axis).normalized()
    e1, e2 = perp_frame(axis)
    verts = []
    for i in range(n_big):
        a = 2.0 * math.pi * i / n_big
        radial = e1 * math.cos(a) + e2 * math.sin(a)
        mid = centre + radial * big
        for j in range(n_small):
            b = 2.0 * math.pi * j / n_small
            verts.append(tuple(mid + radial * (small * math.cos(b)) + axis * (small * math.sin(b))))
    tris = []
    for i in range(n_big):
        for j in range(n_small):
            a0, a1 = i * n_small + j, i * n_small + (j + 1) % n_small
            b0, b1 = ((i + 1) % n_big) * n_small + j, ((i + 1) % n_big) * n_small + (j + 1) % n_small
            tris.append((a0, b0, b1))
            tris.append((a0, b1, a1))
    return piece(name, field, (verts, tris))


# ------------------------------------------------------------------ the two designs
# All numbers below are made up for these vehicles. They are offsets from the seat (sx, sy, sz) and from the
# measured footprint (front fy0, rear fy1, half width W, ground g); none is a number of the kit kart.

def footprint(A):
    W = min(A["sx"] - A["fx0"], A["fx1"] - A["sx"])
    return A["sx"], A["sy"], A["sz"], A["fy0"], A["fy1"], W, A["g"]


def design_hover(A):
    sx, sy, sz, fy0, fy1, W, g = footprint(A)
    P = []
    floor_top, floor_bot = sz - 0.075, sz - 0.125
    # floor between the pontoons, under the feet and the seat
    P.append(box_y("floor", "dark", sx - 0.37, sx + 0.37, fy0 + 0.08, sy + 0.30, floor_bot, floor_top, 0.012))
    # seat cushion
    P.append(box_y("seat", "seat", sx - 0.20, sx + 0.20, sy - 0.17, sy + 0.23, sz - 0.085, sz - 0.035, 0.018))
    # side pontoons: fat, nose kicked up and tapered, a little narrower at the back
    xi, xo = 0.36, 0.95 * W
    zb, zt = g + 0.05, sz + 0.21
    secs = [(fy0 + 0.035, xi + 0.05, xo - 0.05, zb + 0.13, zt - 0.03),
            (fy0 + 0.10, xi + 0.01, xo - 0.01, zb + 0.04, zt),
            (fy0 + 0.22, xi, xo, zb, zt),
            (fy1 - 0.12, xi, xo, zb, zt),
            (fy1 - 0.02, xi + 0.03, xo - 0.03, zb + 0.03, zt - 0.04)]
    left = sections_y("pontoon_l", "hull", [(y, sx + a, sx + b, c, d) for y, a, b, c, d in secs], 0.055)
    P.append(left)
    P.append(mirror_x(left, sx))
    # a trim rail on top of each pontoon
    rail = box_y("rail_l", "trim", sx + xi + 0.05, sx + xo - 0.07, fy0 + 0.26, fy1 - 0.20, zt - 0.01, zt + 0.025,
                 0.012)
    P.append(rail)
    P.append(mirror_x(rail, sx))
    # wedge nose between the pontoons, in front of the toes, lower at the front
    P.append(sections_y("nose", "nose", [(fy0 + 0.035, sx - xi - 0.02, sx + xi + 0.02, zb + 0.01, sz + 0.03),
                                         (fy0 + 0.10, sx - xi - 0.02, sx + xi + 0.02, zb, sz + 0.12)], 0.03))
    # wide front bumper
    P.append(box_y("bumper", "dark", sx - xo + 0.04, sx + xo - 0.04, fy0, fy0 + 0.07, g + 0.06, g + 0.17, 0.025))
    # engine block behind the seat (its raked front is the seat back)
    ey0 = sy + 0.26
    P.append(sections_y("engine", "hull", [(ey0, sx - 0.34, sx + 0.34, floor_bot, sz + 0.44),
                                           (ey0 + 0.07, sx - 0.34, sx + 0.34, floor_bot, sz + 0.54),
                                           (fy1 - 0.06, sx - 0.34, sx + 0.34, floor_bot, sz + 0.54),
                                           (fy1 - 0.025, sx - 0.31, sx + 0.31, floor_bot + 0.03, sz + 0.50)], 0.06))
    # two round thrusters at the back, with a glowing core
    for side in (1.0, -1.0):
        cx = sx + side * 0.17
        P.append(cylinder_axis("thruster" + ("_l" if side > 0 else "_r"), "metal", (cx, fy1, sz + 0.20), "y",
                               [(-0.12, 0.12), (-0.03, 0.13), (-0.005, 0.13), (-0.005, 0.105)], 12))
        P.append(cylinder_axis("thrust_glow" + ("_l" if side > 0 else "_r"), "glow", (cx, fy1, sz + 0.20), "y",
                               [(-0.02, 0.095), (0.0, 0.095)], 12))
    # a fin on the engine block, swept back
    fin = [ring_z(sz + 0.52, octagon(sx - 0.03, sx + 0.03, fy1 - 0.20, fy1 - 0.04, 0.012)),
           ring_z(sz + 0.78, octagon(sx - 0.022, sx + 0.022, fy1 - 0.11, fy1 - 0.045, 0.01))]
    P.append(piece("fin", "trim", loft(fin)))
    # four hover pads under the pontoons, each with a glowing ring below
    px = 0.5 * (xi + xo)
    for name, y in (("f", sy - 0.62), ("r", sy + 0.40)):
        for side in (1.0, -1.0):
            c = (sx + side * px, y, g)
            tag = name + ("l" if side > 0 else "r")
            P.append(cylinder_axis("pad_" + tag, "dark", c, "z", [(0.026, 0.12), (0.035, 0.13), (0.08, 0.13)], 12))
            P.append(cylinder_axis("pad_glow_" + tag, "glow", c, "z", [(0.018, 0.085), (0.027, 0.085)], 12))
    return P


def design_plane(A):
    sx, sy, sz, fy0, fy1, W, g = footprint(A)
    body = []
    floor_top, floor_bot = sz - 0.075, sz - 0.125
    body.append(box_y("floor", "seat", sx - 0.33, sx + 0.33, fy0 + 0.08, sy + 0.30, floor_bot, floor_top, 0.012))
    body.append(box_y("seat", "seat", sx - 0.20, sx + 0.20, sy - 0.17, sy + 0.23, sz - 0.085, sz - 0.035, 0.018))
    # nose cowl: a hollow cup over the legs, open towards the seat
    cy_back, cy_front = sy - PLANE_COWL_BACK, fy0 + 0.06
    ox, oz0, oz1 = 0.40, g + 0.02, sz + 0.42
    ix, iz0, iz1 = 0.255, floor_top, sz + 0.34
    outer = [ring_y(cy_back, octagon(sx - ox, sx + ox, oz0, oz1, 0.07)),
             ring_y(cy_front + 0.10, octagon(sx - ox, sx + ox, oz0, oz1, 0.07)),
             ring_y(cy_front + 0.02, octagon(sx - ox + 0.03, sx + ox - 0.03, oz0 + 0.03, oz1 - 0.04, 0.08)),
             ring_y(cy_front, octagon(sx - ox + 0.07, sx + ox - 0.07, oz0 + 0.07, oz1 - 0.09, 0.08))]
    inner = [ring_y(cy_back, octagon(sx - ix, sx + ix, iz0, iz1, 0.03)),
             ring_y(sy - 0.895, octagon(sx - ix, sx + ix, iz0, iz1, 0.03))]
    body.append(piece("cowl", "body", cup(outer, inner)))
    # windscreen on the cowl, raked back
    ws = [ring_z(oz1 - 0.01, octagon(sx - 0.22, sx + 0.22, cy_back - 0.08, cy_back - 0.02, 0.01)),
          ring_z(oz1 + 0.13, octagon(sx - 0.18, sx + 0.18, cy_back - 0.035, cy_back - 0.005, 0.008))]
    body.append(piece("windscreen", "glass", loft(ws)))
    # low cockpit walls from the cowl to the tail cone
    wall = box_y("wall_l", "body", sx + 0.32, sx + 0.40, cy_back - 0.06, sy + 0.30, floor_bot, sz + 0.26, 0.03)
    body.append(wall)
    body.append(mirror_x(wall, sx))
    # tail cone behind the seat (its front is the seat back)
    ty0 = sy + 0.26
    body.append(sections_y("tail_cone", "body", [(ty0, sx - 0.40, sx + 0.40, floor_bot, sz + 0.47),
                                                 (ty0 + 0.08, sx - 0.38, sx + 0.38, floor_bot + 0.02, sz + 0.50),
                                                 (fy1 - 0.20, sx - 0.26, sx + 0.26, sz + 0.02, sz + 0.47),
                                                 (fy1 - 0.02, sx - 0.10, sx + 0.10, sz + 0.20, sz + 0.40)], 0.06))
    # head fairing on the tail cone
    body.append(sections_y("fairing", "body", [(ty0 + 0.06, sx - 0.12, sx + 0.12, sz + 0.44, sz + 0.56),
                                               (ty0 + 0.16, sx - 0.12, sx + 0.12, sz + 0.44, sz + 0.62),
                                               (fy1 - 0.12, sx - 0.06, sx + 0.06, sz + 0.40, sz + 0.52)], 0.04))
    # fin and rudder (hinge: vertical line at y = rudder_y)
    rudder_y = fy1 - 0.055
    fin = [ring_z(sz + 0.42, octagon(sx - 0.028, sx + 0.028, fy1 - 0.24, rudder_y - 0.006, 0.012)),
           ring_z(sz + 0.74, octagon(sx - 0.022, sx + 0.022, rudder_y - 0.09, rudder_y - 0.006, 0.01))]
    body.append(piece("fin", "wing", loft(fin)))
    rudder = [ring_z(sz + 0.44, octagon(sx - 0.022, sx + 0.022, rudder_y, fy1 + 0.015, 0.008)),
              ring_z(sz + 0.74, octagon(sx - 0.018, sx + 0.018, rudder_y, fy1 + 0.005, 0.007))]
    body.append(piece("rudder", "ctrl", loft(rudder)))
    # tailplane and elevators (hinge: line along x at y = elev_y, z = elev_z)
    elev_y, elev_z = fy1 - 0.06, sz + 0.36
    tp = [ring_x(sx + 0.09, foil(fy1 - 0.20, elev_y - 0.006, elev_z, 0.04)),
          ring_x(sx + 0.40, foil(fy1 - 0.15, elev_y - 0.006, elev_z, 0.032))]
    tpl = piece("tailplane_l", "wing", loft(tp))
    body.append(tpl)
    body.append(mirror_x(tpl, sx))
    ev = [ring_x(sx + 0.10, octagon(elev_y, fy1 + 0.01, elev_z - 0.014, elev_z + 0.014, 0.006)),
          ring_x(sx + 0.39, octagon(elev_y, fy1 - 0.005, elev_z - 0.012, elev_z + 0.012, 0.005))]
    evl = piece("elevator_l", "ctrl", loft(ev))
    body.append(evl)
    body.append(mirror_x(evl, sx))
    # stub wings with flaps (hinge: line along x at y = flap_y, z = wing_z)
    wing_z = g + 0.27
    wle, flap_y = sy - 0.36, sy + 0.055
    wing = [ring_x(sx + 0.36, foil(wle, flap_y - 0.006, wing_z, 0.07)),
            ring_x(sx + W - 0.05, foil(wle + 0.04, flap_y - 0.006, wing_z, 0.055)),
            ring_x(sx + W, foil(wle + 0.07, flap_y - 0.03, wing_z, 0.04))]
    wl = piece("wing_l", "wing", loft(wing))
    body.append(wl)
    body.append(mirror_x(wl, sx))
    fl = [ring_x(sx + 0.44, octagon(flap_y, flap_y + 0.085, wing_z - 0.013, wing_z + 0.013, 0.006)),
          ring_x(sx + W - 0.08, octagon(flap_y, flap_y + 0.075, wing_z - 0.011, wing_z + 0.011, 0.005))]
    fll = piece("flap_l", "ctrl", loft(fl))
    body.append(fll)
    body.append(mirror_x(fll, sx))
    # wheels: main wheels under the wings, small nose wheels beside the cowl; struts on the inner side
    wheels = []
    specs = (("wheel_fl", sx + 0.47, fy0 + 0.23, 0.075, 0.032), ("wheel_rl", sx + 0.58, sy - 0.16, 0.10, 0.04))
    for name, x, y, r, hw in specs:
        c = (x, y, g + r)
        for nm, cx in ((name, x), (name[:-1] + "r", 2.0 * sx - x)):
            wheels.append({"name": nm, "centre": (cx, c[1], c[2]), "r": r, "hw": hw})
        strut_top = wing_z - 0.02 if name == "wheel_rl" else sz + 0.10
        sx0 = x - hw - 0.018 - 0.022
        st = box_y("strut_" + name[-2:], "strut", sx0, sx0 + 0.022, y - 0.022, y + 0.022, g + r - 0.02, strut_top,
                   0.006)
        body.append(st)
        body.append(mirror_x(st, sx))
    # propeller: spinner + two blades, in the node's own space (origin on the axis)
    hub = (sx, cy_front - 0.035, 0.5 * (oz0 + oz1))
    prop = []
    n = 12
    tip = fy0 - hub[1]            # the spinner's tip on the footprint's front
    prop.append(cylinder_axis("spinner", "spinner", (0.0, 0.0, 0.0), "y",
                              [(0.027, 0.075), (0.008, 0.072), (-0.008, 0.055), (0.72 * tip, 0.032), (tip, 0.006)],
                              n, start=math.pi / n))
    R = 0.25
    pitch = math.radians(18.0)
    bl_rings = []
    for r, w, t in ((0.04, 0.05, 0.020), (0.10, 0.085, 0.020), (0.19, 0.095, 0.017), (R - 0.02, 0.08, 0.014),
                    (R, 0.045, 0.010)):
        pts = []
        for u, v in octagon(-0.5 * w, 0.5 * w, -0.5 * t, 0.5 * t, 0.25 * t):
            # u across the blade (x), v along the axis (y), turned by the blade pitch about the radial line (z)
            x = u * math.cos(pitch) - v * math.sin(pitch)
            y = u * math.sin(pitch) + v * math.cos(pitch)
            pts.append((x, y, r))
        bl_rings.append(pts)
    blade = piece("blade_a", "blade", loft(bl_rings))
    prop.append(blade)
    # the second blade: the first turned a half turn about the shaft (here y): (x, y, z) -> (-x, y, -z), exact
    prop.append({"name": "blade_b", "field": "blade", "verts": [(-x, y, -z) for x, y, z in blade["verts"]],
                 "tris": list(blade["tris"])})
    # Into the node's own frame with the shaft on the local X axis (as the engine turns a wheel node about its
    # local X): local x' = forward = -y, y' = x, z' = z. The node is turned -90 degrees about Z (PROP_ROT), so
    # local +X points to the kart's front. A half turn about the shaft stays exact: (x', y', z') -> (x', -y', -z').
    prop = [{"name": p["name"], "field": p["field"], "verts": [(-y, x, z) for x, y, z in p["verts"]],
             "tris": p["tris"]} for p in prop]
    hinges = {"rudder": ((sx, rudder_y, 0.0), (0.0, 0.0, 1.0)),
              "elevator": ((sx, elev_y, elev_z), (1.0, 0.0, 0.0)),
              "flap": ((sx, flap_y, wing_z), (1.0, 0.0, 0.0))}
    return body, prop, hub, wheels, hinges


def design_steer(kind, A, S):
    """The steering part (in its node's space: origin on the kit's steering pivot, axes as the world's), the fixed
    pieces that carry it (world space, part of the body) and the grips' deviation from the fists. Laid out in the
    kit joint's frame from the measured pivot S["H"], column S["c"] (forward), S["up"], S["side"]: a point (s, f, u)
    is H + s side + f c + u up. As the kit's wheel, the grip runs through both fists: the hover's grip axis is the
    straight line through the two fist centres, the plane's rim is the circle through them, its centre on the
    column, tilted as the kit's wheel. The pieces the hands close around are named grip_*: the check allows
    contact between them and the hands (only the hands)."""
    H, c, up, side = S["H"], S["c"], S["up"], S["side"]

    def at(v):                  # (s, f, u) -> node space (relative to H)
        return tuple(side * v[0] + c * v[1] + up * v[2])

    def world(v):
        return tuple(H + side * v[0] + c * v[1] + up * v[2])
    fl, fr = S["fists"][1], S["fists"][-1]
    steer, fixed, dev = [], [], {}
    if kind == "hover":
        d = (fr - fl).normalized()
        for sgn, f in ((1, fl), (-1, fr)):
            dev[sgn] = (f - fl - d * (f - fl).dot(d)).length          # distance of the fist centre from the axis
        mid = fl.lerp(fr, 0.5)
        # the sleeves: from inside the hands' inner reach to beyond their outer reach, along the axis
        sleeves = {}
        for sgn, f in ((1, fl), (-1, fr)):
            out = (f - mid).normalized()
            lo, hi = S["reach"][sgn]
            t0 = (lo - 0.015) - abs(f.x)            # along the axis from the fist centre, inward negative
            t1 = (hi + 0.02) - abs(f.x)
            g0, g1 = f + out * t0, f + out * t1
            sleeves[sgn] = g0
            steer.append(tube("grip_" + ("a" if sgn > 0 else "b"), "dark", at(g0), at(g1),
                              [(0.0, 0.021), (0.08, 0.023), (0.9, 0.023), (1.0, 0.019)]))
            steer[-1]["ring"] = 10
        # the bar between the sleeves (5 mm into each), riser and clamp to the column
        a0 = sleeves[1] + (sleeves[-1] - sleeves[1]).normalized() * -0.005
        a1 = sleeves[-1] + (sleeves[1] - sleeves[-1]).normalized() * -0.005
        steer.append(tube("bar", "metal", at(a0), at(a1), [(0.0, 0.016), (1.0, 0.016)]))
        fh = mid.y
        steer.append(tube("riser", "trim", at((0.0, fh, 0.0)), at((0.0, fh, mid.z)), [(0.0, 0.02), (1.0, 0.02)]))
        steer.append(tube("clamp", "trim", at((0.0, fh - 0.025, mid.z)), at((0.0, fh + 0.025, mid.z)),
                          [(0.0, 0.028), (1.0, 0.028)]))
        steer.append(tube("column", "trim", at((0.0, fh - 0.02, 0.0)), at((0.0, fh + 0.10, 0.0)),
                          [(0.0, 0.026), (1.0, 0.026)]))
        # fixed: a headset around the column's end and a down tube to the floor between the feet
        fixed.append(tube("headset", "metal", world((0.0, fh + 0.105, 0.0)), world((0.0, fh + 0.15, 0.0)),
                          [(0.0, 0.036), (1.0, 0.036)]))
        floor = (A["sx"], A["sy"] - 0.70, A["sz"] - 0.07)
        fixed.append(tube("down_tube", "metal", world((0.0, fh + 0.13, 0.0)), floor, [(0.0, 0.024), (1.0, 0.024)]))
        return steer, fixed, dev
    # plane: the rim through the fists, its centre on the column, in a plane tilted about the side axis by
    # RIM_TILT x the kit wheel's measured tilt
    kit_tilt = math.atan2(-S["normal"].z, S["normal"].y)
    tilt = RIM_TILT * kit_tilt
    n = Vector((0.0, math.cos(tilt), -math.sin(tilt)))
    f0 = sum(f.y + (n.z * f.z + n.x * f.x) / n.y for f in (fl, fr)) / 2.0      # plane n.(p - (0, f0, 0)) = 0
    C = Vector((0.0, f0, 0.0))
    big = sum((f - C).length for f in (fl, fr)) / 2.0
    for sgn, f in ((1, fl), (-1, fr)):
        d = f - C
        ax = d.dot(n)
        rad = (d - n * ax).length
        dev[sgn] = math.hypot(rad - big, ax)                    # distance of the fist centre from the rim's circle
    rim_r = 0.012
    es = Vector((1.0, 0.0, 0.0))
    ev = n.cross(es).normalized()
    if ev.z < 0.0:
        ev = -ev
    # a yoke: the rim is an arc over the top, through both fists, open at the bottom (a full ring would reach
    # down to the knees, which rise in jump and hit, and the clips turn it up to 50 degrees); capped ends a little
    # above the hub's height, held by one spoke up from the hub
    a0, a1 = math.radians(YOKE_ARC[0]), math.radians(YOKE_ARC[1])
    rings = []
    for k in range(YOKE_SEGMENTS + 1):
        ang = a0 + (a1 - a0) * k / YOKE_SEGMENTS
        radial = es * math.cos(ang) + ev * math.sin(ang)
        mid = C + radial * big
        rings.append([at(mid + radial * u + n * v) for u, v in circle(8, rim_r, math.pi / 8)])
    steer.append(piece("grip_rim", "seat", loft(rings)))
    steer[-1]["ring"] = 8
    for tag, ang in (("u", 90.0),):
        r = math.radians(ang)
        dirv = es * math.cos(r) + ev * math.sin(r)
        steer.append(tube("spoke_" + tag, "strut", at(C + dirv * 0.03), at(C + dirv * (big - 0.006)),
                          [(0.0, 0.009), (1.0, 0.009)], 8))
    steer.append(tube("hub", "spinner", at((0.0, f0 - 0.025, 0.0)), at((0.0, f0 + 0.09, 0.0)),
                      [(0.0, 0.03), (0.2, 0.036), (1.0, 0.036)]))
    # fixed: the column housing out of the cowl's back face
    cowl = Vector((A["sx"], A["sy"] - PLANE_COWL_BACK, H.z)) - H
    f_cowl = cowl.dot(c)
    fixed.append(tube("column_housing", "body", world((0.0, f0 + 0.095, 0.0)), world((0.0, f_cowl + 0.03, 0.0)),
                      [(0.0, 0.04), (1.0, 0.045)]))
    return steer, fixed, dev


def wheel_piece(r, hw, n=12):
    """A wheel in its own space: chamfered tyre along x, a hub cap through it."""
    ch = 0.25 * hw
    tyre = cylinder_axis("tyre", "tyre", (0.0, 0.0, 0.0), "x",
                         [(-hw, r - ch), (-hw + ch, r), (hw - ch, r), (hw, r - ch)], n, start=math.pi / n)
    hub = cylinder_axis("hub", "hub", (0.0, 0.0, 0.0), "x",
                        [(-hw - 0.008, 0.40 * r), (hw + 0.008, 0.40 * r)], n, start=math.pi / n)
    return [tyre, hub]


# ------------------------------------------------------------------ shape keys

def hover_keys(P, A):
    sx, sy, sz = A["sx"], A["sy"], A["sz"]
    allv = [v for p in P for v in p["verts"]]
    pivot = (sx, sy, sz)
    # nose_up turns about the cross axis on the floor at the toes (sy - 0.90): the floor under the feet then sinks
    # instead of rising into them; nose_down turns about the seat.
    toes = (sx, sy - 0.90, sz - 0.075)
    # rotation about +y by +a lowers +x (kart left); about +x by -a lifts -y (the front)
    return {"lean_left": rotate_about(allv, pivot, (0, 1, 0), HOVER_LEAN),
            "lean_right": rotate_about(allv, pivot, (0, 1, 0), -HOVER_LEAN),
            "nose_up": rotate_about(allv, toes, (1, 0, 0), -HOVER_PITCH),
            "nose_down": rotate_about(allv, pivot, (1, 0, 0), HOVER_PITCH)}


def plane_keys(P, hinges):
    def key(prefix, deg):
        out = []
        origin, axis = hinges[prefix]
        for p in P:
            if p["name"].startswith(prefix):
                out.extend(rotate_about(p["verts"], origin, axis, deg))
            else:
                out.extend(p["verts"])
        return out
    # elevator: +angle about +x lifts the trailing edge (+y); rudder: -angle about +z moves it to +x (kart left)
    return {"elevator_up": key("elevator", PLANE_ELEVATOR), "elevator_down": key("elevator", -PLANE_ELEVATOR),
            "rudder_left": key("rudder", -PLANE_RUDDER), "rudder_right": key("rudder", PLANE_RUDDER),
            "flaps_down": key("flap", -PLANE_FLAPS)}


HOVER_KEYS = ("lean_left", "lean_right", "nose_up", "nose_down")
STEER_NAME = {"hover": "handlebar", "plane": "steer_wheel"}   # the steering part's node
PLANE_KEYS = ("elevator_up", "elevator_down", "rudder_left", "rudder_right", "flaps_down")


# ------------------------------------------------------------------ material, colours, objects

def png_bytes(width, height, rows):
    import zlib
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def atlas_fields(rgb):
    """Field name -> (x0, y0, x1, y1) in a 4 x 4 grid of 16-pixel fields, in the order of the dict."""
    out = {}
    for i, name in enumerate(rgb):
        gx, gy = i % 4, i // 4
        out[name] = (gx * 16, gy * 16, gx * 16 + 16, gy * 16 + 16)
    return out


def make_material(tag, rgb, vcol):
    fields = atlas_fields(rgb)
    first = next(iter(rgb))
    rows = []
    for y in range(ATLAS):
        row = []
        for x in range(ATLAS):
            col = rgb[first]
            for name, (x0, y0, x1, y1) in fields.items():
                if x0 <= x < x1 and y0 <= y < y1:
                    col = rgb[name]
            row.extend(col)
        rows.append(row)
    data = png_bytes(ATLAS, ATLAS, rows)
    img = bpy.data.images.new(tag + "_atlas", ATLAS, ATLAS, alpha=False)
    img.pack(data=data, data_len=len(data))
    img.source = "FILE"
    img.colorspace_settings.name = "sRGB"
    mat = bpy.data.materials.new("%s [vcol %s]" % (tag, vcol))
    if mat.node_tree is None:
        mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Roughness"].default_value = 0.8
    bsdf.inputs["Metallic"].default_value = 0.0
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.interpolation = "Closest"
    tex.location = (-320.0, 260.0)
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    c = rgb[first]
    mat.diffuse_color = (c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, 1.0)
    return mat, fields


def psx_colours(mesh):
    """Grey 128 = neutral, light from above front as the puppet's; the Hi-Res steps (as the glider)."""
    n = len(mesh.vertices)
    nr = [0.0] * (n * 3)
    mesh.vertex_normals.foreach_get("vector", nr)
    light = Vector(PSX_LIGHT).normalized()
    zs = [v.co.z for v in mesh.vertices]
    z0, z1 = min(zs), max(zs)
    shades = []
    for i, v in enumerate(mesh.vertices):
        nv = Vector((nr[3 * i], nr[3 * i + 1], nr[3 * i + 2]))
        h = (v.co.z - z0) / max(z1 - z0, 1e-9)
        shades.append(PSX_NORMAL_SHARE * nv.dot(light) + (1.0 - PSX_NORMAL_SHARE) * (2.0 * h - 1.0))
    srt = sorted(shades)
    median = srt[n // 2] if n % 2 else 0.5 * (srt[n // 2 - 1] + srt[n // 2])
    rgba = []
    q, lo, hi = PSX_STEPS
    for s in shades:
        dd = s - median
        dd = math.copysign(max(abs(dd) - PSX_DEAD_ZONE, 0.0), dd)
        b = int(round(min(max(128.0 + PSX_SPREAD * dd, PSX_RANGE[0]), PSX_RANGE[1])))
        b = min(max(128 + q * int(round((b - 128) / float(q))), lo), hi)
        rgba.extend((b / 255.0, b / 255.0, b / 255.0, 1.0))
    attr = mesh.color_attributes.new(name="Col", type="BYTE_COLOR", domain="POINT")
    attr.data.foreach_set("color_srgb", rgba)
    mesh.color_attributes.active_color = attr
    mesh.color_attributes.render_color_index = mesh.color_attributes.active_color_index


def make_object(name, pieces, mat, fields, vcol, location, keys=None, key_order=(), rotation=(0.0, 0.0, 0.0)):
    """One mesh object from pieces (local coordinates), flat shaded, with shape keys {name: all vertices}."""
    verts, faces, ff = [], [], []
    for p in pieces:
        base = len(verts)
        verts.extend(p["verts"])
        faces.extend(tuple(base + i for i in t) for t in p["tris"])
        ff.extend([p["field"]] * len(p["tris"]))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    mesh.polygons.foreach_set("use_smooth", [False] * len(faces))
    uvl = mesh.uv_layers.new(name="UVMap")
    uv = []
    offs = ((-3.0, -3.0), (3.0, -3.0), (0.0, 3.0))
    for poly, field in zip(mesh.polygons, ff):
        x0, y0, x1, y1 = fields[field]
        cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
        for c in range(len(poly.vertices)):
            ox, oy = offs[c % 3]
            uv.extend(((cx + ox) / ATLAS, 1.0 - (cy + oy) / ATLAS))
    uvl.uv.foreach_set("vector", uv)
    mesh.materials.append(mat)
    if vcol == "psx":
        psx_colours(mesh)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    obj.rotation_euler = rotation
    if keys:
        obj.shape_key_add(name="Basis", from_mix=False)
        for k in key_order:
            sk = obj.shape_key_add(name=k, from_mix=False)
            sk.data.foreach_set("co", [c for p in keys[k] for c in p])
        for kb in mesh.shape_keys.key_blocks:
            kb.value = 0.0
            kb.slider_min, kb.slider_max = 0.0, 1.0
        mesh.shape_keys.use_relative = True
        mesh.update()
    return obj


def tris_of(pieces):
    return sum(len(p["tris"]) for p in pieces)


# ------------------------------------------------------------------ checks

def closed_and_wound(p):
    """Every directed edge exactly once and its reverse exactly once; volume > 0."""
    seen = {}
    for a, b, c in p["tris"]:
        for e in ((a, b), (b, c), (c, a)):
            seen[e] = seen.get(e, 0) + 1
    for (a, b), k in seen.items():
        if k != 1 or seen.get((b, a), 0) != 1:
            return False
    return volume(p["verts"], p["tris"]) > 0.0


class Solid:
    """A closed piece in world space: BVH, bounding box, ray-parity inside test."""

    def __init__(self, name, co, tris):
        self.name = name
        self.co = [Vector(v) for v in co]
        self.tris = tris
        self.bvh = BVHTree.FromPolygons(self.co, tris)
        self.lo = Vector((min(v.x for v in self.co), min(v.y for v in self.co), min(v.z for v in self.co)))
        self.hi = Vector((max(v.x for v in self.co), max(v.y for v in self.co), max(v.z for v in self.co)))

    def inside(self, p):
        if any(p[i] < self.lo[i] or p[i] > self.hi[i] for i in range(3)):
            return False
        odd = 0
        for d in RAY_DIRS:
            d = Vector(d).normalized()
            origin, hits = Vector(p), 0
            while hits < 200:
                h = self.bvh.ray_cast(origin, d)
                if h[0] is None:
                    break
                hits += 1
                origin = h[0] + d * 1e-6
            odd += hits & 1
        return odd >= 2


def world_solids(obj, pieces, verts_local=None):
    """Solids of an object's pieces in world space (verts_local: the object's vertices in a shape key)."""
    mw = obj.matrix_world
    allv = verts_local if verts_local is not None else [v for p in pieces for v in p["verts"]]
    out, base = [], 0
    for p in pieces:
        n = len(p["verts"])
        co = [mw @ Vector(v) for v in allv[base:base + n]]
        out.append(Solid(obj.name + ":" + p["name"], co, p["tris"]))
        base += n
    return out


class Spinner:
    """The volume a part sweeps turning about its axis: a cylinder (radius = the largest distance of a vertex from
    the axis, length = its extent along the axis), in world space."""

    def __init__(self, name, origin, axis, co_world):
        self.name = name
        self.o = Vector(origin)
        self.a = Vector(axis).normalized()
        ts = [(v - self.o).dot(self.a) for v in co_world]
        self.t0, self.t1 = min(ts), max(ts)
        self.r = max(((v - self.o) - self.a * (v - self.o).dot(self.a)).length for v in co_world)

    def distance(self, p):
        d = Vector(p) - self.o
        t = d.dot(self.a)
        rad = (d - self.a * t).length
        dt = max(self.t0 - t, 0.0, t - self.t1)
        dr = max(rad - self.r, 0.0)
        return math.hypot(dt, dr)    # 0 inside


def figure_frames(rig, scene):
    """(clip, frame) for the rest pose and every frame of every seated clip."""
    out = [(None, 0)]
    for c in SEATED_CLIPS:
        f0, f1 = bpy.data.actions[c].frame_range
        out.extend((c, f) for f in range(int(round(f0)), int(round(f1)) + 1))
    return out


def set_clip(rig, scene, clip, frame):
    """The rest pose (clip None) or a frame of a clip. Every pose bone is reset to its rest values first, so a bone
    the clip has no channel for stands in its rest pose - as in the exported clips (the kit's exporter resets the
    pose bones between actions, export_reset_pose_bones). Without the reset such a bone keeps the value the clip
    before left (measured: idle after finish_lose had the hands 22 mm further forward)."""
    rig.data.pose_position = "REST" if clip is None else "POSE"
    for pb in rig.pose.bones:
        pb.location = (0.0, 0.0, 0.0)
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.rotation_euler = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)
    if clip is not None:
        act = bpy.data.actions[clip]
        if rig.animation_data is None:
            rig.animation_data_create()
        if rig.animation_data.action is not act:
            rig.animation_data.action = act
        if getattr(rig.animation_data, "action_slot", 1) is None and len(act.slots):
            rig.animation_data.action_slot = act.slots[0]
    scene.frame_set(frame + 1)
    scene.frame_set(frame)
    bpy.context.view_layer.update()


def figure_sweep(rig, puppet, scene, states, mover=None, mover_skip=("wings",)):
    """states: {state name: (solids, spinners)}. For each state and every figure frame: crossing triangle pairs,
    figure vertices inside a solid or a swept cylinder, vehicle vertices inside the figure, and the clearance
    (smallest distance figure vertex -> vehicle surface or swept cylinder, vehicle vertex -> figure surface).
    mover = (object, pieces): a part on a joint the clips move (the steering part), taken where the frame puts it,
    in every state but those of mover_skip; its own clearance to the figure ("mover_clear") and its pairs with the state's solids
    ("mover_body") are kept too.
    Returns {state: {"pairs", "inside", "clear", "where", "mover_clear", "mover_body"}}, per clip too."""
    res = {s: {"pairs": 0, "inside": 0, "clear": NEAR, "where": "-", "clips": {}, "mover_clear": NEAR,
               "mover_where": "-", "mover_body": 0} for s in states}
    prepared = {}
    for s, (solids, spinners) in states.items():
        allco, alltris, base = [], [], 0
        for so in solids:
            allco.extend(so.co)
            alltris.extend(tuple(base + i for i in t) for t in so.tris)
            base += len(so.co)
        lo = Vector((min(v.x for v in allco), min(v.y for v in allco), min(v.z for v in allco))) - Vector((NEAR,) * 3)
        hi = Vector((max(v.x for v in allco), max(v.y for v in allco), max(v.z for v in allco))) + Vector((NEAR,) * 3)
        probe = [so.co[0] for so in solids]
        prepared[s] = (BVHTree.FromPolygons(allco, alltris), allco, lo, hi, probe)
    for clip, frame in figure_frames(rig, scene):
        set_clip(rig, scene, clip, frame)
        dg = bpy.context.evaluated_depsgraph_get()
        fco, fpolys = world_coords(puppet, dg)
        fsolid = Solid("figure", fco, fpolys)
        name = clip or "rest pose"
        msolids, mbvh, mco = [], None, []
        if mover is not None:
            # the steering part: its grip_* pieces may touch the hands (only the hands: a figure triangle counts as
            # hand when all its corners are hand vertices); every other pair - the other pieces with the whole
            # figure, the grips with the rest of the figure - must stay apart
            hand = mover[2]
            allm = world_solids(mover[0], mover[1])
            msolids = [so for so in allm if not so.name.split(":")[1].startswith("grip")]
            gsolids = [so for so in allm if so.name.split(":")[1].startswith("grip")]
            rest_polys = [p for p in fpolys if not all(i in hand for i in p)]
            hand_polys = [p for p in fpolys if all(i in hand for i in p)]
            rest_bvh = BVHTree.FromPolygons(fco, rest_polys)
            hand_bvh = BVHTree.FromPolygons(fco, hand_polys)

            def joined(sols):
                co, tris, base = [], [], 0
                for so in sols:
                    co.extend(so.co)
                    tris.extend(tuple(base + i for i in t) for t in so.tris)
                    base += len(so.co)
                return co, BVHTree.FromPolygons(co, tris)
            mco, mbvh = joined(msolids)
            gco, gbvh = joined(gsolids)
            pool = mco + gco
            mlo = Vector((min(v.x for v in pool), min(v.y for v in pool), min(v.z for v in pool))) - Vector((NEAR,) * 3)
            mhi = Vector((max(v.x for v in pool), max(v.y for v in pool), max(v.z for v in pool))) + Vector((NEAR,) * 3)
            mnear = [i for i, p in enumerate(fco) if mlo.x <= p.x <= mhi.x and mlo.y <= p.y <= mhi.y and
                     mlo.z <= p.z <= mhi.z]
            mpairs = len(mbvh.overlap(fsolid.bvh)) + len(gbvh.overlap(rest_bvh))
            minside = sum(1 for so in msolids for i in mnear if so.inside(fco[i]))
            # without crossing pairs a piece is wholly inside or wholly outside: one vertex decides, the one
            # farthest from the grips (a piece's end may lie inside a grip, which the hand closes around)
            for so in msolids:
                far = max(so.co, key=lambda v: min((v - g).length for g in gco)) if gco else so.co[0]
                minside += 1 if fsolid.inside(far) else 0
            minside += sum(1 for so in gsolids for i in mnear if i not in hand and so.inside(fco[i]))
            ghand = len(gbvh.overlap(hand_bvh))
            mclear, mwhere = NEAR, None
            for i in mnear:
                h = mbvh.find_nearest(fco[i], NEAR)
                if h[0] is not None and h[3] < mclear:
                    mclear, mwhere = h[3], "figure vertex %s to a steering piece" % _fmt(fco[i])
                if i not in hand:
                    h = gbvh.find_nearest(fco[i], NEAR)
                    if h[0] is not None and h[3] < mclear:
                        mclear, mwhere = h[3], "figure vertex %s (not a hand) to a grip" % _fmt(fco[i])
            for p in mco:
                h = fsolid.bvh.find_nearest(p, NEAR)
                if h[0] is not None and h[3] < mclear:
                    mclear, mwhere = h[3], "steering vertex %s" % _fmt(p)
            for p in gco:
                h = rest_bvh.find_nearest(p, NEAR)
                if h[0] is not None and h[3] < mclear:
                    mclear, mwhere = h[3], "grip vertex %s to the figure (not the hands)" % _fmt(p)
        for s, (solids, spinners) in states.items():
            vbvh, vco, lo, hi, probe = prepared[s]
            r = res[s]
            c = r["clips"].setdefault(name, {"pairs": 0, "inside": 0, "clear": NEAR, "mover_clear": NEAR})
            pairs = len(vbvh.overlap(fsolid.bvh))
            near = [p for p in fco if lo.x <= p.x <= hi.x and lo.y <= p.y <= hi.y and lo.z <= p.z <= hi.z]
            inside = 0
            use_mover = mbvh is not None and s not in mover_skip
            if use_mover:
                pairs += mpairs
                inside += minside
                r["mover_body"] += len(mbvh.overlap(vbvh)) + len(gbvh.overlap(vbvh))
                r["grip_hand"] = max(r.get("grip_hand", 0), ghand)
                c["grip_hand"] = max(c.get("grip_hand", 0), ghand)
                c["mover_clear"] = min(c["mover_clear"], mclear)
                if mclear < r["mover_clear"]:
                    r["mover_clear"], r["mover_where"] = mclear, "%s frame %d, %s" % (name, frame, mwhere)
            for so in solids:
                inside += sum(1 for p in near if so.inside(p))
            for sp in spinners:
                inside += sum(1 for p in near if sp.distance(p) <= 0.0)
            inside += sum(1 for p in probe if fsolid.inside(p))
            clear = NEAR
            where = None
            for p in near:
                h = vbvh.find_nearest(p, NEAR)
                if h[0] is not None and h[3] < clear:
                    clear, where = h[3], "figure vertex %s" % _fmt(p)
                for sp in spinners:
                    dsp = sp.distance(p)
                    if dsp < clear:
                        clear, where = dsp, "figure vertex %s to the swept %s" % (_fmt(p), sp.name)
            for p in vco:
                h = fsolid.bvh.find_nearest(p, NEAR)
                if h[0] is not None and h[3] < clear:
                    clear, where = h[3], "vehicle vertex %s" % _fmt(p)
            if use_mover and mclear < clear:
                clear, where = mclear, mwhere
            c["pairs"] += pairs
            c["inside"] += inside
            c["clear"] = min(c["clear"], clear)
            r["pairs"] += pairs
            r["inside"] += inside
            if clear < r["clear"]:
                r["clear"], r["where"] = clear, "%s frame %d, %s" % (name, frame, where)
    set_clip(rig, scene, "idle", 0)
    rig.data.pose_position = "POSE"
    return res


def _fmt(p):
    return "(%.3f, %.3f, %.3f)" % (p[0], p[1], p[2])


def spin_contacts(part_pieces, mw, others, steps, axis="X", angles=None):
    """A spinning part (pieces in its node's space, turning about the node's local X, or the axis given) turned in
    steps (or to the angles given) against solids: pairs and clearance."""
    pairs, clear = 0, NEAR
    for k in (angles if angles is not None else range(0, 360, steps)):
        m = mw @ Matrix.Rotation(math.radians(k), 4, axis)
        for p in part_pieces:
            co = [m @ Vector(v) for v in p["verts"]]
            so = Solid(p["name"], co, p["tris"])
            for o in others:
                pairs += len(so.bvh.overlap(o.bvh))
                for v in so.co:
                    h = o.bvh.find_nearest(v, NEAR)
                    if h[0] is not None:
                        clear = min(clear, h[3])
                for v in o.co:
                    h = so.bvh.find_nearest(v, NEAR)
                    if h[0] is not None:
                        clear = min(clear, h[3])
    return pairs, clear


def pivot_measure(pieces, axis_index, half_turn):
    """In the node's own space: the radial distance of the vertex centre and of the bounding box centre from the
    axis (local axis_index), the hub ring's centre for the propeller, and the largest distance between the mesh
    and the mesh turned a half turn about the axis (half_turn)."""
    verts = [Vector(v) for p in pieces for v in p["verts"]]
    others = [i for i in range(3) if i != axis_index]
    cen = sum(verts, Vector()) / len(verts)
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    rad_c = math.hypot(cen[others[0]], cen[others[1]])
    rad_b = math.hypot(0.5 * (lo[others[0]] + hi[others[0]]), 0.5 * (lo[others[1]] + hi[others[1]]))
    sym = None
    if half_turn:
        kd = KDTree(len(verts))
        for i, v in enumerate(verts):
            kd.insert(v, i)
        kd.balance()
        sym = 0.0
        for v in verts:
            w = v.copy()
            w[others[0]], w[others[1]] = -v[others[0]], -v[others[1]]
            sym = max(sym, kd.find(w)[2])
    return rad_c, rad_b, sym


def hub_centre(spinner_piece, axis_index):
    """The centre of the spinner's base ring (its first ring of 12), radial distance from the axis."""
    ring = [Vector(v) for v in spinner_piece["verts"][:12]]
    c = sum(ring, Vector()) / len(ring)
    o = [i for i in range(3) if i != axis_index]
    return math.hypot(c[o[0]], c[o[1]])


def read_glb_json(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"glTF":
        return None
    length, _kind = struct.unpack_from("<II", data, 12)
    return json.loads(data[20:20 + length].decode("utf-8"))


def glb_problems(path, root, children, key_names):
    """The vehicle file: root node with the named children, morph target names, no skin, no animation, no kit part."""
    j = read_glb_json(path)
    if j is None:
        return ["%s is no GLB" % os.path.basename(path)]
    out = []
    nodes = j.get("nodes", [])
    roots = [nodes[i].get("name") for i in j["scenes"][j.get("scene", 0)]["nodes"]]
    if roots != [root]:
        out.append("the scene's root nodes are %s, not ['%s']" % (roots, root))
    else:
        r = nodes[j["scenes"][j.get("scene", 0)]["nodes"][0]]
        names = sorted(nodes[i].get("name") for i in r.get("children", []))
        if names != sorted(children):
            out.append("the children of '%s' are %s, not %s" % (root, names, sorted(children)))
    if j.get("skins") or j.get("animations"):
        out.append("the file has a skin or an animation")
    for n in nodes:
        if "ctr_kit" in (n.get("extras") or {}):
            out.append("the file has a kit part (%s)" % n.get("name"))
    for m in j.get("meshes", []):
        if "ctr_kit" in (m.get("extras") or {}):
            out.append("the file has a kit mesh (%s)" % m.get("name"))
    for m in j.get("materials", []):
        if m.get("name", "").startswith("ctrkit_"):
            out.append("the file has a kit material (%s)" % m.get("name"))
    for mname, keys in key_names.items():
        found = None
        for m in j.get("meshes", []):
            if m.get("name") == mname:
                found = (m.get("extras") or {}).get("targetNames")
        if found != list(keys):
            out.append("the morph targets of %s are %s, not %s" % (mname, found, list(keys)))
    return out


def driver_problems(path, parts, key_names, wheels, steer):
    """The figure + vehicle file: the vehicle parts in the subtree of the first node 'kart' (the joint), the wheel
    nodes there when the vehicle has them, one skin, the seated clips, the morph target names, and nothing of the
    kit kart: no kit mark (ctr_kit), no kit material or image (ctrkit_), none of its object names."""
    j = read_glb_json(path)
    if j is None:
        return ["%s is no GLB" % os.path.basename(path)]
    out = []
    nodes = j.get("nodes", [])
    names = [n.get("name") for n in nodes]
    if ROOT_NODE not in names:
        return ["%s has no node '%s'" % (os.path.basename(path), ROOT_NODE)]
    sub, stack = set(), [names.index(ROOT_NODE)]
    while stack:
        i = stack.pop()
        sub.add(names[i])
        stack.extend(nodes[i].get("children", []))
    missing = [p for p in parts if p not in sub]
    if missing:
        out.append("the driver file has %s outside the subtree of '%s'" % (missing, ROOT_NODE))
    if STEER_BONE not in names:
        out.append("the driver file has no joint '%s'" % STEER_BONE)
    else:
        si = names.index(STEER_BONE)
        kids = [names[k] for k in nodes[si].get("children", [])]
        if steer not in kids:
            out.append("the steering part '%s' is not on the joint '%s' (children %s)" % (steer, STEER_BONE, kids))
        skins = j.get("skins", [])
        if skins and si not in skins[0].get("joints", []):
            out.append("the joint '%s' is not a joint of the skin" % STEER_BONE)
        turned = set()
        for an in j.get("animations", []):
            for ch in an.get("channels", []):
                if ch.get("target", {}).get("node") == si and ch.get("target", {}).get("path") == "rotation":
                    turned.add(an.get("name"))
        for clip in ("steer_left", "steer_right"):
            if clip not in turned:
                out.append("the clip %s does not turn the joint '%s'" % (clip, STEER_BONE))
    log("driver file %s: skin joints %d" % (os.path.basename(path),
                                            len(j["skins"][0]["joints"]) if j.get("skins") else 0))
    wheel_names = ("wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr")
    if wheels != all(w in sub for w in wheel_names) or (not wheels and any(w in names for w in wheel_names)):
        out.append("the driver file's wheel nodes are not as the vehicle's")
    if len(j.get("skins", [])) != 1:
        out.append("the driver file has %d skins, not 1" % len(j.get("skins", [])))
    clips = set(a.get("name") for a in j.get("animations", []))
    if any(c not in clips for c in SEATED_CLIPS):
        out.append("the driver file lacks clips: %s" % [c for c in SEATED_CLIPS if c not in clips])
    for mname, keys in key_names.items():
        found = None
        for m in j.get("meshes", []):
            if m.get("name") == mname:
                found = (m.get("extras") or {}).get("targetNames")
        if found != list(keys):
            out.append("the morph targets of %s in the driver file are %s" % (mname, found))
    for kind, items in (("node", nodes), ("mesh", j.get("meshes", []))):
        for it in items:
            if "ctr_kit" in (it.get("extras") or {}):
                out.append("the driver file has a kit %s (%s)" % (kind, it.get("name")))
    for kind, items in (("material", j.get("materials", [])), ("image", j.get("images", []))):
        for it in items:
            if "ctrkit" in str(it.get("name", "")).lower():
                out.append("the driver file has a kit %s (%s)" % (kind, it.get("name")))
    for n in ("kart_body", "kart_steer_wheel"):
        if n in names:
            out.append("the driver file has the kit kart's node %s" % n)
    log("driver file %s: %d nodes, images %s, materials %s" %
        (os.path.basename(path), len(nodes), [i.get("name") for i in j.get("images", [])],
         [m.get("name") for m in j.get("materials", [])]))
    return out


# ------------------------------------------------------------------ export

def exporter_props(exporter):
    """The settings of a stored collection exporter as keyword arguments of export_scene.gltf."""
    pr = exporter.export_properties
    out = {}
    for k, p in pr.bl_rna.properties.items():
        if k in ("rna_type", "filepath", "filter_glob", "check_existing", "collection", "will_save_settings",
                 "ui_tab", "gltf_export_id") or p.is_readonly:
            continue
        out[k] = getattr(pr, k)
    return out


def export_collection(coll, path, settings):
    for o in bpy.data.objects:
        o.select_set(False)
    if os.path.exists(path):
        os.remove(path)
    bpy.ops.export_scene.gltf(filepath=path, collection=coll.name, check_existing=False, **settings)
    return os.path.isfile(path)


# ------------------------------------------------------------------ one vehicle

def build_vehicle(kind, A, vcol):
    """Objects of the vehicle (not linked yet) and what the checks need."""
    S = A["steer"]
    steer_p, fixed, dev = design_steer(kind, A, S)
    loc = tuple(S["H"])
    if kind == "hover":
        P = design_hover(A) + fixed
        keys = hover_keys(P, A)
        mat, fields = make_material("dummy_hover", HOVER_RGB, vcol)
        body = make_object("hover_body", P, mat, fields, vcol, (0.0, 0.0, 0.0), keys, HOVER_KEYS)
        sobj = make_object(STEER_NAME[kind], steer_p, mat, fields, vcol, loc)
        return {"kind": kind, "body": body, "body_pieces": P, "keys": keys, "key_order": HOVER_KEYS,
                "parts": [body, sobj], "prop": None, "wheels": [],
                "steer": {"obj": sobj, "pieces": steer_p, "column": S["c"].copy(), "dev": dev,
                          "hand": S["hand"], "side": S["side"].copy()}}
    P, prop, hub, wheels, hinges = design_plane(A)
    P = P + fixed
    keys = plane_keys(P, hinges)
    mat, fields = make_material("dummy_plane", PLANE_RGB, vcol)
    body = make_object("plane_body", P, mat, fields, vcol, (0.0, 0.0, 0.0), keys, PLANE_KEYS)
    sobj = make_object(STEER_NAME[kind], steer_p, mat, fields, vcol, loc)
    pobj = make_object("spin_prop", prop, mat, fields, vcol, hub, rotation=PROP_ROT)
    wl = []
    for w in wheels:
        wp = wheel_piece(w["r"], w["hw"])
        wobj = make_object(w["name"], wp, mat, fields, vcol, w["centre"])
        wl.append({"obj": wobj, "pieces": wp, "centre": w["centre"], "r": w["r"], "hw": w["hw"]})
    return {"kind": kind, "body": body, "body_pieces": P, "keys": keys, "key_order": PLANE_KEYS,
            "parts": [body, sobj, pobj] + [w["obj"] for w in wl], "prop": {"obj": pobj, "pieces": prop, "hub": hub},
            "wheels": wl, "steer": {"obj": sobj, "pieces": steer_p, "column": S["c"].copy(), "dev": dev,
                                    "hand": S["hand"], "side": S["side"].copy()}}


def grip_deviation(V, rig, puppet, scene):
    """Per hand: the distance of the fist centre (mean of the hand's vertices, rest pose, from the puppet as it is
    now) from the centre line of the grip pieces as built (the centres of their rings, joined), in world space."""
    set_clip(rig, scene, None, 0)
    so = V["steer"]["obj"]
    mw = so.matrix_world
    line = []
    for p in V["steer"]["pieces"]:
        if "ring" not in p:
            continue
        n = p["ring"]
        cen = [sum((mw @ Vector(v) for v in p["verts"][k:k + n]), Vector()) / n
               for k in range(0, len(p["verts"]), n)]
        line.append(cen)
    dg = bpy.context.evaluated_depsgraph_get()
    co, _p = world_coords(puppet, dg)
    origin = mw.translation.copy()
    out = {}
    for sgn in (1, -1):
        pts = [co[i] for i in V["steer"]["hand"] if (co[i] - origin).dot(V["steer"]["side"]) * sgn > 0.0]
        f = sum(pts, Vector()) / len(pts)
        best = 1e9
        for cen in line:
            for a, b in zip(cen, cen[1:]):
                ab = b - a
                t = max(0.0, min(1.0, (f - a).dot(ab) / ab.dot(ab)))
                best = min(best, (a + ab * t - f).length)
        out[sgn] = best
    set_clip(rig, scene, "idle", 0)
    return out


def check_vehicle(V, rig, puppet, scene, problems, report):
    """The geometry checks of one vehicle; appends to problems (gates) and fills report (measurements)."""
    kind = V["kind"]
    allp = list(V["body_pieces"])
    if V["prop"]:
        allp += V["prop"]["pieces"]
    for w in V["wheels"]:
        allp += w["pieces"]
    allp += V["steer"]["pieces"]
    bad = sorted(set(p["name"] for p in allp if not closed_and_wound(p)))
    if bad:
        problems.append("%s: pieces not closed or not wound outward: %s" % (kind, ", ".join(bad)))
    report["pieces"] = len(allp)

    body_solids = world_solids(V["body"], V["body_pieces"])
    spinners = []
    extra = []
    if V["prop"]:
        po = V["prop"]["obj"]
        extra += world_solids(po, V["prop"]["pieces"])
        co = [po.matrix_world @ Vector(v) for p in V["prop"]["pieces"] for v in p["verts"]]
        spinners.append(Spinner("spin_prop", po.matrix_world.translation, po.matrix_world.to_3x3() @ Vector((1, 0, 0)),
                                co))
    for w in V["wheels"]:
        wo = w["obj"]
        extra += world_solids(wo, w["pieces"])
        co = [wo.matrix_world @ Vector(v) for p in w["pieces"] for v in p["verts"]]
        spinners.append(Spinner(wo.name, wo.matrix_world.translation, wo.matrix_world.to_3x3() @ Vector((1, 0, 0)),
                                co))

    # the figure against the vehicle: the Basis (gate) and each shape key at 1.0 (information)
    states = {"basis": (body_solids + extra, spinners)}
    wing_solids = [so for so in body_solids if so.name.split(":")[1].startswith(("wing_", "flap_"))]
    if wing_solids:
        states["wings"] = (wing_solids, [])          # the wings with their flaps alone, for their own clearance
    for k in V["key_order"]:
        states[k] = (world_solids(V["body"], V["body_pieces"], V["keys"][k]) + extra, spinners)
    so = V["steer"]["obj"]
    res = figure_sweep(rig, puppet, scene, states, (so, V["steer"]["pieces"], V["steer"]["hand"]))
    report["sweep"] = res
    b = res["basis"]
    if b["pairs"] or b["inside"]:
        problems.append("%s: the figure touches the vehicle (%d triangle pairs, %d vertices inside over all frames)"
                        % (kind, b["pairs"], b["inside"]))
    log("%s: figure vs vehicle (steering part where each frame turns it), rest pose + %d seated clips: pairs %d, "
        "inside %d, min clearance %.1f mm (%s)" %
        (kind, len(SEATED_CLIPS), b["pairs"], b["inside"], 1000 * b["clear"], b["where"]))
    for clip, c in b["clips"].items():
        log("  %-12s pairs %d inside %d clearance %.1f mm, steering part to the figure %.1f mm; grip-hand contact "
            "pairs (allowed) up to %d" % (clip, c["pairs"], c["inside"], 1000 * c["clear"], 1000 * c["mover_clear"],
                                         c.get("grip_hand", 0)))
    log("%s: %s to the figure (grips to all but the hands, the other pieces to the whole figure), all frames: min "
        "clearance %.1f mm (%s); rest pose %.1f mm" %
        (kind, so.name, 1000 * b["mover_clear"], b["mover_where"], 1000 * b["clips"]["rest pose"]["mover_clear"]))
    dv = grip_deviation(V, rig, puppet, scene)
    report["grip_dev"] = dv
    log("%s: grip through the fist centres, measured on the built part in the rest pose (distance of each fist "
        "centre from the grip's centre line): %.3f mm and %.3f mm (limit %.0f mm; design value %.3f / %.3f mm)" %
        (kind, 1000 * dv[1], 1000 * dv[-1], 1000 * GRIP_DEV_MAX, 1000 * V["steer"]["dev"][1],
         1000 * V["steer"]["dev"][-1]))
    if max(dv.values()) > GRIP_DEV_MAX:
        problems.append("%s: the grip misses a fist centre by %.1f mm" % (kind, 1000 * max(dv.values())))
    if not b.get("grip_hand"):
        problems.append("%s: the hands do not close around the grip in any frame" % kind)
    # the steering part against the body: in every frame (as the clips turn it) and turned to +-STEER_TURN
    log("%s: %s vs the body in every frame of the clips: pairs %d" % (kind, so.name, b["mover_body"]))
    if b["mover_body"]:
        problems.append("%s: the steering part touches the body as the clips turn it" % kind)
    others = body_solids + extra
    axis_local = (so.matrix_world.to_3x3().inverted() @ V["steer"]["column"]).normalized()
    turn = list(range(-STEER_TURN, STEER_TURN + 1, 15))
    pairs, clear = 0, NEAR
    for k in turn:
        m = so.matrix_world @ Matrix.Rotation(math.radians(k), 4, axis_local)
        for pc in V["steer"]["pieces"]:
            sol = Solid(pc["name"], [m @ Vector(v) for v in pc["verts"]], pc["tris"])
            for o in others:
                pairs += len(sol.bvh.overlap(o.bvh))
                for v in sol.co:
                    h = o.bvh.find_nearest(v, NEAR)
                    if h[0] is not None:
                        clear = min(clear, h[3])
    report["steer_turn"] = (pairs, clear)
    log("%s: %s turned %d..%d degrees about the column vs the body and the other parts: pairs %d, min clearance "
        "%.1f mm" % (kind, so.name, -STEER_TURN, STEER_TURN, pairs, 1000 * clear))
    if pairs:
        problems.append("%s: the steering part touches the body when turned" % kind)
    if wing_solids:
        r = res["wings"]
        log("%s: wings + flaps alone vs figure: pairs %d, inside %d, min clearance %.1f mm (%s)" %
            (kind, r["pairs"], r["inside"], 1000 * r["clear"], r["where"]))
        if r["pairs"] or r["inside"]:
            problems.append("%s: the wings touch the figure" % kind)
        wp_, wc_ = 0, NEAR
        for w in V["wheels"]:
            p_, c_ = spin_contacts(w["pieces"], w["obj"].matrix_world, wing_solids, SPIN_STEP)
            wp_ += p_
            wc_ = min(wc_, c_)
        report["wings_wheels"] = (wp_, wc_)
        log("%s: wings + flaps vs the four wheels turned in %d degree steps: pairs %d, min clearance %.1f mm" %
            (kind, SPIN_STEP, wp_, 1000 * wc_))
        if wp_:
            problems.append("%s: the wings touch a wheel" % kind)
    for k in V["key_order"]:
        r = res[k]
        log("%s: shape key %-13s at 1.0 (info): pairs %d, inside %d, min clearance %.1f mm" %
            (kind, k, r["pairs"], r["inside"], 1000 * r["clear"]))

    # spinning parts against the body (and each other)
    if V["wheels"] or V["prop"]:
        spin = {}
        for w in V["wheels"]:
            others = body_solids + ([s for s in extra if s.name.startswith("spin_prop:")])
            spin[w["obj"].name] = spin_contacts(w["pieces"], w["obj"].matrix_world, others, SPIN_STEP)
        if V["prop"]:
            others = body_solids + [s for s in extra if not s.name.startswith("spin_prop:")]
            spin["spin_prop"] = spin_contacts(V["prop"]["pieces"], V["prop"]["obj"].matrix_world, others,
                                              SPIN_STEP)
        report["spin"] = spin
        for name, (pairs, clear) in spin.items():
            log("%s: %s turned in %d degree steps vs body and the other parts: pairs %d, min clearance %.1f mm" %
                (kind, name, SPIN_STEP, pairs, 1000 * clear))
            if pairs:
                problems.append("%s: %s touches the body when it turns (%d pairs)" % (kind, name, pairs))
        # the keys against the wheels (information): flaps_down is the one near the main wheels
        kinfo = {}
        for k in V["key_order"]:
            ks = world_solids(V["body"], V["body_pieces"], V["keys"][k])
            pairs, clear = 0, NEAR
            for w in V["wheels"]:
                p_, c_ = spin_contacts(w["pieces"], w["obj"].matrix_world, ks, 45)
                pairs += p_
                clear = min(clear, c_)
            kinfo[k] = (pairs, clear)
            log("%s: shape key %-13s at 1.0 vs the wheels (info): pairs %d, min clearance %.1f mm" %
                (kind, k, pairs, 1000 * clear))
        report["key_wheels"] = kinfo

    # pivots
    if V["prop"]:
        rc, rb, sym = pivot_measure(V["prop"]["pieces"], 0, True)
        rh = hub_centre(V["prop"]["pieces"][0], 0)
        report["prop_pivot"] = (rc, rb, rh, sym)
        log("%s: spin_prop pivot: vertex centre %.3g m, box centre %.3g m, hub ring centre %.3g m off the axis; "
            "half-turn symmetry max %.3g m" % (kind, rc, rb, rh, sym))
        if max(rc, rb, rh, sym) > PIVOT_TOL:
            problems.append("%s: the propeller's pivot is off its axis or the blades are not symmetric" % kind)
    wp = {}
    for w in V["wheels"]:
        rc, rb, _s = pivot_measure(w["pieces"], 0, False)
        wp[w["obj"].name] = (rc, rb)
        if max(rc, rb) > PIVOT_TOL:
            problems.append("%s: %s's pivot is off its axle (%.3g m)" % (kind, w["obj"].name, max(rc, rb)))
    if wp:
        report["wheel_pivot"] = wp
        log("%s: wheel pivots, largest distance of the mesh centre from the axle: %.3g m" %
            (kind, max(max(v) for v in wp.values())))

    # size against the footprint, ground
    co = [v for sol in body_solids + extra for v in sol.co]
    lo = [min(v[a] for v in co) for a in range(3)]
    hi = [max(v[a] for v in co) for a in range(3)]
    report["bbox"] = (lo, hi)


def make_vehicle_file(V, out_path, settings):
    """The vehicle alone: a collection with the root node 'kart' (an empty at the origin) and the parts."""
    coll = bpy.data.collections.new("Vehicle " + V["kind"])
    bpy.context.scene.collection.children.link(coll)
    root = bpy.data.objects.new(ROOT_NODE, None)
    root.empty_display_type = "PLAIN_AXES"
    root.empty_display_size = 0.2
    coll.objects.link(root)
    for o in V["parts"]:
        coll.objects.link(o)
        o.parent = root
    bpy.context.view_layer.update()
    ok = export_collection(coll, out_path, settings)
    # out of the file again: the parts go to the kit's collection Kart (scene), the empty is removed
    for o in V["parts"]:
        coll.objects.unlink(o)
    bpy.context.scene.collection.children.unlink(coll)
    bpy.data.collections.remove(coll)
    for o in V["parts"]:
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    bpy.data.objects.remove(root)
    return ok


def remove_kit_kart():
    """Remove the kit kart (made from the player's game files) from the open file: objects, meshes, materials and
    images that only it used."""
    kart = bpy.data.collections["Kart"]
    objs = list(kart.all_objects)
    meshes = [o.data for o in objs if o.type == "MESH"]
    mats = set(m for me in meshes for m in me.materials if m is not None)
    imgs = set()
    for m in mats:
        if m.node_tree:
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image is not None:
                    imgs.add(n.image)
    mesh_names = sorted(me.name for me in meshes)
    mat_names = sorted(m.name for m in mats)
    img_names = sorted(i.name for i in imgs)
    for o in objs:
        bpy.data.objects.remove(o)
    # by name: removing an object may already have removed its data
    for coll, names in ((bpy.data.meshes, mesh_names), (bpy.data.materials, mat_names),
                        (bpy.data.images, img_names)):
        for n in names:
            d = coll.get(n)
            if d is not None and d.users == 0:
                coll.remove(d)
    for o in bpy.data.objects:
        if "ctr_kit" in o.keys():
            fail(4, "the kit kart could not be removed (%s)." % o.name)


def seat_in_kit(V, rig):
    """The parts into the collection Kart, hung on the joint 'kart' as the kit kart was, the steering part on the
    joint 'kart_steer' as the kit's steering wheel was (world place kept): the clips turn it."""
    kart = bpy.data.collections["Kart"]
    for o in V["parts"]:
        kart.objects.link(o)
        mw = o.matrix_world.copy()
        o.parent = rig
        o.parent_type = "BONE"
        o.parent_bone = STEER_BONE if o is V["steer"]["obj"] else KART_BONE
        bpy.context.view_layer.update()
        o.matrix_world = mw
    bpy.context.view_layer.update()


# ------------------------------------------------------------------ main

def main():
    opt = parse_args(sys.argv)
    if tuple(bpy.app.version[:2]) != BLENDER_VERSION:
        fail(3, "needs Blender %d.%d, this is %s." % (BLENDER_VERSION + (bpy.app.version_string,)))
    os.makedirs(opt["out"], exist_ok=True)
    names = {"hover": "dummy-hover", "plane": "dummy-plane"}
    outputs = []
    for v in names.values():
        outputs += [os.path.join(opt["out"], v + s) for s in (".glb", "-driver.glb", "-szene.blend",
                                                               "-szene.blend1")]
    outputs.append(os.path.join(opt["out"], README_NAME))
    for o in outputs:
        if os.path.exists(o):
            os.remove(o)

    problems = []
    reports = {}
    A = None
    puppet_tris = 0
    for kind in ("hover", "plane"):
        # every vehicle starts from the kit file as it is
        rig, puppet, kart = open_kit(opt["kit_blend"])
        scene = bpy.context.scene
        A2 = measure_kit(rig, kart)
        if A is None:
            A = A2
            log("kit kart measured: footprint x %.4f .. %.4f, y (front) %.4f .. %.4f (rear), ground z %.4f, top z "
                "%.4f; seat (SeatPoint) %s" % (A["fx0"], A["fx1"], A["fy0"], A["fy1"], A["g"], A["top"],
                                               _fmt((A["sx"], A["sy"], A["sz"]))))
            for n, (lo, hi) in sorted(A["parts"].items()):
                log("  %-18s x %.4f .. %.4f  y %.4f .. %.4f  z %.4f .. %.4f" % (n, lo[0], hi[0], lo[1], hi[1],
                                                                              lo[2], hi[2]))
            A["steer"] = measure_steer(rig, puppet, kart, scene)
            S = A["steer"]
            log("kit steering measured: pivot %s, column %s; fist centres (s, f, u) from the pivot in the joint's "
                "frame (side, along the column, up): %s and %s; the kit wheel's plane normal (s, f, u) (%.4f, %.4f, "
                "%.4f)" % (_fmt(S["H"]), _fmt(S["c"]), _fmt(S["fists"][1]), _fmt(S["fists"][-1]), S["normal_s"],
                           S["normal"].y, S["normal"].z))
        puppet_tris = sum(len(p.vertices) - 2 for p in puppet.data.polygons)
        vcol = "psx"
        for m in puppet.data.materials:
            if m is not None and m.name.endswith("]") and "[vcol " in m.name:
                vcol = m.name[m.name.rindex("[vcol ") + 6:-1]
        remove_kit_kart()
        V = build_vehicle(kind, A, vcol)
        base = os.path.join(opt["out"], names[kind])

        # 1. the vehicle alone, with the kit's figure exporter settings (no animation, morph targets on)
        settings = exporter_props(bpy.data.collections["Figure"].exporters[0])
        settings.update(EXPORT_MORPH)
        settings["export_animations"] = False
        settings["export_skins"] = False
        if not make_vehicle_file(V, base + ".glb", settings):
            fail(5, "self check failed: the exporter wrote no %s.glb." % names[kind])

        # 2. into the kit scene in place of the kit kart: the preview scene and the figure + vehicle file
        seat_in_kit(V, rig)
        set_clip(rig, scene, "idle", 0)
        rep = {"tris": {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in V["parts"]}}
        reports[kind] = rep
        check_vehicle(V, rig, puppet, scene, problems, rep)
        # 3. the driver: figure + vehicle, with the kit's CreatorKit exporter (morph targets on)
        ck = bpy.data.collections["CreatorKit"].exporters[0]
        s2 = exporter_props(ck)
        s2.update(EXPORT_MORPH)
        if not export_collection(bpy.data.collections["CreatorKit"], base + "-driver.glb", s2):
            problems.append("%s: the exporter wrote no -driver.glb" % kind)
        else:
            problems.extend("%s: %s" % (kind, p) for p in
                            driver_problems(base + "-driver.glb", [o.name for o in V["parts"]],
                                            {V["body"].name: V["key_order"]}, len(V["wheels"]) == 4,
                                            V["steer"]["obj"].name))
        if opt["scene"]:
            ck.filepath = "//" + names[kind] + "-driver.glb"
            for k, val in EXPORT_MORPH.items():
                setattr(ck.export_properties, k, val)
            bpy.ops.wm.save_as_mainfile(filepath=base + "-szene.blend", compress=False, copy=True)

        # the file itself
        children = [o.name for o in V["parts"]]
        problems.extend("%s: %s" % (kind, p) for p in
                        glb_problems(base + ".glb", ROOT_NODE, children, {V["body"].name: V["key_order"]}))
        total = sum(rep["tris"].values())
        rep["total"] = total
        if total > TRI_LIMIT or total + puppet_tris > BUDGET:
            problems.append("%s: %d triangles (limit %d), %d with the figure (budget %d)" %
                            (kind, total, TRI_LIMIT, total + puppet_tris, BUDGET))
        log("%s: triangles %s, total %d, with the figure %d" %
            (kind, ", ".join("%s %d" % kv for kv in rep["tris"].items()), total, total + puppet_tris))

    if problems:
        for p in problems:
            log("self check failed: " + p)
        for o in outputs:
            if os.path.exists(o):
                os.remove(o)
        sys.exit(5)
    write_readme(opt, A, reports, puppet_tris)
    log("ok " + ", ".join("%s.glb" % n for n in names.values()))


def write_readme(opt, A, R, puppet_tris):
    def mm(x):
        return ">= %.0f mm" % (1000 * NEAR) if x >= NEAR else "%.1f mm" % (1000 * x)

    t = ["Dummy Hover and Dummy Plane - two demo vehicles for the Creator Kit figure", ""]
    t += ["Files",
          "  dummy-hover.glb, dummy-plane.glb    the vehicle alone (root node 'kart', no figure, no kit kart):",
          "                                      the files to share",
          "  dummy-*-driver.glb                  the kit figure seated in the vehicle, one file: the driver to",
          "                                      pack as a Hi-Res character with its own kart (--kart-part)",
          "  dummy-*-szene.blend                 the preview scene (kit figure without backpack in the seat).",
          "                                      It holds the kit's data: keep it on this computer.",
          "Pictures (preview renders of the scenes, made by a separate local render script, if present)",
          "  hover-|plane-front|side|back|top|rear34.png   the figure seated, all shape keys 0",
          "  plane-prop-000|045|090.png                    spin_prop turned about its shaft (red cross = node origin)",
          "  hover-|plane-key-<key>.png                    each shape key alone at 1.0, vehicle alone",
          "  hover-|plane-alone-<view>.png                 the same views with all keys 0, for comparison",
          ""]
    t += ["Measured from the kit file (the kit kart is not in any of these files)",
          "  footprint  x %.3f .. %.3f m, y %.3f (front) .. %.3f m (rear), ground z %.3f m" %
          (A["fx0"], A["fx1"], A["fy0"], A["fy1"], A["g"]),
          "  seat       %s m (guide SeatPoint)" % _fmt((A["sx"], A["sy"], A["sz"])), ""]
    t += ["Shape keys (glTF morph targets, each alone on the Basis)",
          "  hover_body  lean_left, lean_right    roll %g deg about the long axis through the seat" % HOVER_LEAN,
          "              nose_up, nose_down       pitch %g deg about the cross axis at the toes (up) or" % HOVER_PITCH,
          "                                       through the seat (down)",
          "  plane_body  elevator_up, elevator_down   elevators %g deg" % PLANE_ELEVATOR,
          "              rudder_left, rudder_right    rudder %g deg (trailing edge to that side)" % PLANE_RUDDER,
          "              flaps_down                   flaps %g deg" % PLANE_FLAPS,
          "  Moving parts of the plane: spin_prop (propeller; the shaft is the node's local X axis, local +X = the",
          "  kart's front; origin on the shaft),",
          "  wheel_fl, wheel_fr, wheel_rl, wheel_rr (axle = the node's X axis).",
          "  Steering: handlebar (hover), steer_wheel (plane) - on the joint kart_steer in the driver file, turned by",
          "  the clips like the kit's steering wheel (no shape keys); origin on the kit's steering pivot.", ""]
    t += ["Triangles"]
    for kind in ("hover", "plane"):
        r = R[kind]
        t.append("  %-6s %s; total %d, with the figure (%d) %d (budget %d)" %
                 (kind, ", ".join("%s %d" % kv for kv in r["tris"].items()), r["total"], puppet_tris,
                  r["total"] + puppet_tris, BUDGET))
    t += ["", "Checks (rest pose and every frame of %s)" % ", ".join(SEATED_CLIPS)]
    for kind in ("hover", "plane"):
        r = R[kind]
        b = r["sweep"]["basis"]
        t.append("  %s: figure vs vehicle %d triangle pairs, %d vertices inside, min clearance %s" %
                 (kind, b["pairs"], b["inside"], mm(b["clear"])))
        if "wings" in r["sweep"]:
            w = r["sweep"]["wings"]
            t.append("    wings + flaps alone vs figure: %d pairs, %d inside, min clearance %s" %
                     (w["pairs"], w["inside"], mm(w["clear"])))
            t.append("    wings + flaps vs the wheels (turned in %d deg steps): %d pairs, min clearance %s" %
                     (SPIN_STEP, r["wings_wheels"][0], mm(r["wings_wheels"][1])))
        for k, v in r["sweep"].items():
            if k not in ("basis", "wings"):
                t.append("    key %-13s at 1.0 (info): %d pairs, %d inside, min clearance %s" %
                         (k, v["pairs"], v["inside"], mm(v["clear"])))
        for name, (pairs, clear) in sorted(r.get("spin", {}).items()):
            t.append("    %s turned in %d deg steps vs body: %d pairs, min clearance %s" %
                     (name, SPIN_STEP, pairs, mm(clear)))
        for k, (pairs, clear) in r.get("key_wheels", {}).items():
            t.append("    key %-13s at 1.0 vs wheels (info): %d pairs, min clearance %s" % (k, pairs, mm(clear)))
        if "steer_turn" in r:
            b2 = r["sweep"]["basis"]
            t.append("    grip through the fist centres: deviation %.2f mm and %.2f mm (limit %.0f mm)" %
                     (1000 * r["grip_dev"][1], 1000 * r["grip_dev"][-1], 1000 * GRIP_DEV_MAX))
            t.append("    steering part to the figure (the hands may touch the grip, nothing else): min %s over all "
                     "frames, %s in the rest pose" % (mm(b2["mover_clear"]), mm(b2["clips"]["rest pose"]["mover_clear"])))
            t.append("    steering part vs body: %d pairs in the clips' frames, %d pairs turned %d..%d deg (min %s)" %
                     (b2["mover_body"], r["steer_turn"][0], -STEER_TURN, STEER_TURN, mm(r["steer_turn"][1])))
        if "prop_pivot" in r:
            rc, rb, rh, sym = r["prop_pivot"]
            t.append("    spin_prop pivot off the axis: mesh centre %.2g m, hub ring centre %.2g m; "
                     "half-turn symmetry %.2g m" % (rc, rh, sym))
        if "wheel_pivot" in r:
            t.append("    wheel pivots off the axle: at most %.2g m" % max(max(v) for v in r["wheel_pivot"].values()))
    t += ["", "Made by creator_kit_vehicles.py with Blender %s from %s." %
          (bpy.app.version_string, os.path.basename(opt["kit_blend"])), ""]
    with open(os.path.join(opt["out"], README_NAME), "wb") as f:
        f.write("\r\n".join(t).encode("ascii"))


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(5)
