# Creator Kit "Dummy Glider": puts a backpack with collapsible wings on the
# Creator Kit puppet of the Hi-Res way and exports the figure with the wings
# as glTF morph targets. Everything added here is made by code in this file
# (a rounded box and telescoping wing segments) - no external asset, no game
# data. The kart is only read from the kit file at run time, to check that the
# folded wings stay clear of it; no kart number is written into this script.
#
#   blender --threads 1 --background --factory-startup --python-exit-code 1 \
#       --python tools/creator_kit_glider.py -- --kit-blend <creator-kit-hires.blend> --out <folder>
#
# Options:
#   --kit-blend <file>   the Hi-Res template of the Creator Kit (Reload Studio,
#                        Character > Creator Kit > Generate demo files, way
#                        Hi-Res): <kit>\hires\creator-kit-hires.blend (required)
#   --out <folder>       output folder (required, created if missing)
#   --no-scene           do not write the scene .blend
#
# Output in <folder>:
#   dummy-glider.glb          the figure alone: puppet + backpack, skeleton and
#                             clips as the kit's figure export, plus the morph
#                             targets of the backpack. No kart - this is the file
#                             to share.
#   dummy-glider-szene.blend  the kit scene with the backpack (it contains the
#                             kit kart from your game files: keep it on this PC)
#   LIESMICH.txt              shape keys and triangle counts
#
# The backpack mesh "glider_pack" hangs 100 % on the bone "chest" (one vertex
# group, Armature modifier like the puppet), so it follows every clip. Its shape
# keys (glTF morph targets, in this order):
#   wings_folded  the Basis: the wing segments nested in the root segment, which
#                 lies across the top of the backpack
#   wings_open    every segment slid out sideways (a pure translation per
#                 segment, so every in-between weight is a rigid telescope)
#   flap          the wing tips raised in a curve   } relative to wings_open:
#   dive          swept back and tips down          } add them on top of
#   pull_up       leading edge up, tips forward     } wings_open = 1
# The wings telescope instead of hinging because a morph target moves every
# vertex on a straight line: a hinge turned by 90 degrees would shrink to 71 %
# halfway, a slide stays exact.
#
# Nothing of the kit is changed: the skeleton (bones, names, rest pose), the
# puppet, the clips and the export settings of the collection Figure stay as
# they are; the export only turns on the morph targets (export_morph).
#
# Exit codes (as templates/make_creator_kit.py): 0 good, 2 wrong call, 3 wrong
# Blender version, 4 input missing or invalid, 5 the self check failed.
# Same Blender and same kit file give the same bytes of the .glb (start Blender
# with --threads 1, as the kit does).
#
# Axes as the kit: Blender X = kart left, -Y = kart front, +Z up, 1 unit = 1 m.

import math
import os
import struct
import json
import sys

import bpy
import bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.dont_write_bytecode = True

USAGE = "usage: creator_kit_glider.py -- --kit-blend <creator-kit-hires.blend> --out <folder> [--no-scene]"
BLENDER_VERSION = (5, 2)

GLB_NAME = "dummy-glider.glb"
SCENE_NAME = "dummy-glider-szene.blend"
README_NAME = "LIESMICH.txt"
LOCAL_NAME = "dummy-glider-demo-local.glb"   # the CreatorKit exporter of the scene (never written here)

OBJ_NAME = "glider_pack"
BONE = "chest"
SHAPE_KEYS = ("wings_folded", "wings_open", "flap", "dive", "pull_up")
ON_OPEN = ("flap", "dive", "pull_up")       # offsets on top of wings_open
# What the export changes in the kit's stored figure exporter: the morph targets on (normals with them,
# no tangents, dense - as the kit's export_try_sparse_sk False). Every other setting stays the kit's.
EXPORT_CHANGES = {"export_morph": True, "export_morph_normal": True, "export_morph_tangent": False}
TRI_LIMIT = 2000          # backpack + wings (the round's done definition)
BUDGET = 64000            # the game's budget for a whole file

# ---- the design, in metres (made up here; placed from the puppet and the bone chest at run time)
GAP_BACK = 0.014          # backpack front face behind the puppet's back
PACK_W = 0.27             # width (x)
PACK_D = 0.10             # depth (y)
PACK_TOP = 0.03           # top of the pack below the tail of chest
PACK_BEVEL = 0.018
PACK_BEVEL_SEGS = 2
WING_N = 7                # segments per wing
WING_RATIO = 0.90         # each segment's profile, the size of the one before
WING_INNER = 0.006        # root segment starts this far from the middle
WING_ROOT_LEN = 0.144     # span of the root segment (x)
WING_NEST = 0.003         # each nested segment is this much shorter at each end
WING_OVERLAP = 0.03       # open: each segment stays this far inside the one before
WING_CHORD = 0.26         # root chord (y)
WING_THICK = 0.05         # root thickness (z), times the profile's 0.84
WING_LE_IN = 0.04         # leading edge of the root segment behind the pack's front face
WING_Z_BELOW_TOP = 0.025  # wing centre line below the pack top
TIP_TAPER = 0.75          # the last segment's tip profile
# 6-point profile (u along the chord from the leading edge, v up in thicknesses), convex
PROFILE = ((0.0, 0.0), (0.12, 0.48), (0.5, 0.42), (1.0, 0.0), (0.5, -0.30), (0.12, -0.36))
PROFILE_CENTRE = (0.4, 0.05)
# the poses on top of wings_open (degrees; ramps in metres from the hinge at the root segment's end)
FLAP_UP = 35.0
DIVE_SWEEP, DIVE_DOWN = 40.0, -10.0
PULL_PITCH, PULL_DIHEDRAL, PULL_FORWARD = 20.0, 12.0, -10.0
RAMP = 0.10

# ---- colours: a 64 x 64 atlas of flat fields, as the kit puppet's (creator_kit_puppet._FIELDS)
ATLAS = 64
FIELDS = {"pack": (0, 0, 32, 32), "root": (32, 0, 64, 32), "wing": (0, 32, 32, 64), "tip": (32, 32, 64, 64)}
FIELD_RGB = {"pack": (64, 76, 92), "root": (196, 96, 28), "wing": (236, 128, 36), "tip": (232, 232, 226)}
# PS1 grey shading as creator_kit_puppet.build_puppet (Hi-Res steps)
PSX_LIGHT = (0.25, -0.35, 1.0)
PSX_NORMAL_SHARE = 0.7
PSX_DEAD_ZONE = 0.22
PSX_SPREAD = 90.0
PSX_RANGE = (40, 216)
PSX_STEPS = (12, 44, 212)


def fail(code, text):
    print("dummy glider: " + text)
    sys.exit(code)


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
    need = {"creator_rig": "ARMATURE", "puppet": "MESH"}
    for name, kind in need.items():
        o = bpy.data.objects.get(name)
        if o is None or o.type != kind:
            fail(4, "%s has no %s '%s' - is it a Creator Kit template?" % (os.path.basename(path), kind.lower(), name))
    for name in ("Figure", "Kart"):
        if bpy.data.collections.get(name) is None:
            fail(4, "%s has no collection '%s'." % (os.path.basename(path), name))
    figure = bpy.data.collections["Figure"]
    if len(figure.exporters) != 1:
        fail(4, "the collection Figure has no stored exporter (%d)." % len(figure.exporters))
    rig = bpy.data.objects["creator_rig"]
    if BONE not in rig.data.bones:
        fail(4, "the skeleton has no bone '%s'." % BONE)
    if OBJ_NAME in bpy.data.objects:
        fail(4, "the file already has a '%s' - give the kit template itself." % OBJ_NAME)
    kart = [o for o in bpy.data.collections["Kart"].all_objects if o.type == "MESH"]
    if not kart:
        fail(4, "the collection Kart has no mesh.")
    return rig, bpy.data.objects["puppet"], figure, kart


def rig_state(rig):
    """Bones, parents and rest matrices: what must not change."""
    return [(b.name, b.parent.name if b.parent else None, tuple(round(x, 7) for row in b.matrix_local for x in row),
             b.use_deform) for b in rig.data.bones]


def world_verts(obj, depsgraph):
    ev = obj.evaluated_get(depsgraph)
    mesh = ev.to_mesh()
    mw = ev.matrix_world
    co = [mw @ v.co for v in mesh.vertices]
    polys = [tuple(p.vertices) for p in mesh.polygons]
    ev.to_mesh_clear()
    return co, polys


# ------------------------------------------------------------------ the geometry

def anchors(rig, puppet):
    """Where the backpack goes, from the bone chest and the puppet's back (rest pose)."""
    mw = rig.matrix_world
    b = rig.data.bones[BONE]
    head, tail = mw @ b.head_local, mw @ b.tail_local
    z0, z1 = head.z, tail.z - PACK_TOP
    pv = [puppet.matrix_world @ v.co for v in puppet.data.vertices]
    band = [v.y for v in pv if z0 - 0.02 <= v.z <= z1 + 0.02 and abs(v.x - head.x) <= PACK_W / 2 + 0.03]
    if not band:
        fail(4, "the puppet has no back at the bone chest.")
    front = max(band) + GAP_BACK
    return {"x": head.x, "front": front, "z0": z0, "z1": z1}


def build_pack(a):
    """Rounded box: (verts, faces) in world coordinates, triangles."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    sx, sy, sz = PACK_W, PACK_D, a["z1"] - a["z0"]
    cx, cy, cz = a["x"], a["front"] + PACK_D / 2, (a["z0"] + a["z1"]) / 2
    for v in bm.verts:
        v.co = Vector((cx + v.co.x * sx, cy + v.co.y * sy, cz + v.co.z * sz))
    bmesh.ops.bevel(bm, geom=list(bm.verts) + list(bm.edges), offset=PACK_BEVEL, offset_type="OFFSET",
                    segments=PACK_BEVEL_SEGS, profile=0.5, affect="EDGES", clamp_overlap=True)
    bmesh.ops.triangulate(bm, faces=list(bm.faces), quad_method="FIXED", ngon_method="EAR_CLIP")
    bm.verts.index_update()
    verts = [tuple(v.co) for v in bm.verts]
    faces = [tuple(v.index for v in f.verts) for f in bm.faces]
    bm.free()
    # The bevel hands out a few faces in an order that differs between runs (same triangles, other order,
    # so other index bytes in the .glb). Give vertices and triangles an order of their own: vertices by
    # position, each triangle starting at its lowest vertex (winding kept), triangles by their vertices.
    order = sorted(range(len(verts)), key=lambda i: tuple(round(c, 6) for c in verts[i]))
    new_index = {old: new for new, old in enumerate(order)}
    verts = [verts[i] for i in order]
    canon = []
    for f in faces:
        f = [new_index[i] for i in f]
        m = f.index(min(f))
        canon.append(tuple(f[m:] + f[:m]))
    return verts, sorted(canon)


def segment_rings(k, a):
    """Segment k of the left wing (+x): the folded and the open x range and its two profile rings (y, z)."""
    s = WING_RATIO ** k
    length = WING_ROOT_LEN - 2 * k * WING_NEST
    fold_x0 = WING_INNER + k * WING_NEST
    open_x0 = WING_INNER
    for j in range(1, k + 1):
        open_x0 += (WING_ROOT_LEN - 2 * (j - 1) * WING_NEST) - WING_OVERLAP
    le = a["front"] + WING_LE_IN
    cy = le + PROFILE_CENTRE[0] * WING_CHORD
    cz = a["z1"] - WING_Z_BELOW_TOP + PROFILE_CENTRE[1] * WING_THICK

    def ring(scale):
        return [(cy + scale * (u - PROFILE_CENTRE[0]) * WING_CHORD, cz + scale * (v - PROFILE_CENTRE[1]) * WING_THICK)
                for u, v in PROFILE]
    tip = s * (TIP_TAPER if k == WING_N - 1 else 1.0)
    return fold_x0, open_x0, length, ring(s), ring(tip)


def build_wings(a):
    """Both wings: verts (folded), open positions, faces, field per face, and per vertex its segment."""
    verts, opened, faces, fields, seg = [], [], [], [], []
    for side in (1.0, -1.0):
        for k in range(WING_N):
            fx0, ox0, length, r0, r1 = segment_rings(k, a)
            base = len(verts)
            for x, ring in ((0.0, r0), (length, r1)):
                for y, z in ring:
                    verts.append((a["x"] + side * (fx0 + x), y, z))
                    opened.append((a["x"] + side * (ox0 + x), y, z))
                    seg.append(k)
            n = len(PROFILE)
            field = "root" if k == 0 else ("tip" if k == WING_N - 1 else "wing")
            quads = [(base + i, base + (i + 1) % n, base + n + (i + 1) % n, base + n + i) for i in range(n)]
            caps = [tuple(base + i for i in reversed(range(n))), tuple(base + n + i for i in range(n))]
            for f in quads + caps:
                if side < 0:
                    f = tuple(reversed(f))
                # triangulate as a fan from the first corner (the faces are convex)
                for t in range(1, len(f) - 1):
                    faces.append((f[0], f[t], f[t + 1]))
                    fields.append(field)
    return verts, opened, faces, fields, seg


def ramp(d, length):
    t = min(max(d / length, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def pose(p, a, kind):
    """A vertex of the open wing in one of the poses on top of wings_open (left/right by the sign of x)."""
    side = 1.0 if p[0] >= a["x"] else -1.0
    hx = WING_INNER + WING_ROOT_LEN          # the hinge: the root segment's outer end
    x, y, z = side * (p[0] - a["x"]), p[1], p[2]
    d = x - hx
    if d <= 0.0:
        return p
    _x0, _o, _l, ring, _r = segment_rings(0, a)
    hy = sum(q[0] for q in ring) / len(ring)
    hz = sum(q[1] for q in ring) / len(ring)
    w = ramp(d, RAMP)

    def roll(x, z, deg):          # about the hinge line along y (dihedral), +deg lifts the tip
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        dx, dz = x - hx, z - hz
        return hx + dx * c - dz * s, hz + dx * s + dz * c

    def sweep(x, y, deg):         # about the vertical through the hinge, +deg moves the tip back (+y)
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        dx, dy = x - hx, y - hy
        return hx + dx * c - dy * s, hy + dx * s + dy * c

    def pitch(y, z, deg):         # about the span line, +deg lifts the leading edge (-y)
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        dy, dz = y - hy, z - hz
        return hy + dy * c + dz * s, hz - dy * s + dz * c

    if kind == "flap":
        span = (segment_rings(WING_N - 1, a)[1] + segment_rings(WING_N - 1, a)[2]) - hx
        x, z = roll(x, z, FLAP_UP * min(d / span, 1.0) * w)
    elif kind == "dive":
        x, z = roll(x, z, DIVE_DOWN * w)
        x, y = sweep(x, y, DIVE_SWEEP * w)
    elif kind == "pull_up":
        y, z = pitch(y, z, PULL_PITCH * w)
        x, z = roll(x, z, PULL_DIHEDRAL * w)
        x, y = sweep(x, y, PULL_FORWARD * w)
    return (a["x"] + side * x, y, z)


# ------------------------------------------------------------------ material and colours

def png_bytes(width, height, rows):
    import zlib
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def make_material(vcol):
    rows = []
    for y in range(ATLAS):
        row = []
        for x in range(ATLAS):
            rgb = FIELD_RGB["pack"]
            for name, (x0, y0, x1, y1) in FIELDS.items():
                if x0 <= x < x1 and y0 <= y < y1:
                    rgb = FIELD_RGB[name]
            row.extend(rgb)
        rows.append(row)
    data = png_bytes(ATLAS, ATLAS, rows)
    img = bpy.data.images.new("glider_atlas", ATLAS, ATLAS, alpha=False)
    img.pack(data=data, data_len=len(data))
    img.source = "FILE"
    img.colorspace_settings.name = "sRGB"
    mat = bpy.data.materials.new("glider [vcol %s]" % vcol)
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
    mat.diffuse_color = (0.92, 0.5, 0.14, 1.0)
    return mat


def psx_colours(mesh):
    """Grey 128 = neutral, light from above front as the puppet's; the Hi-Res steps."""
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


# ------------------------------------------------------------------ the object

def build_object(rig, figure, a, vcol):
    pv, pf = build_pack(a)
    wv, wo, wf, wfields, _seg = build_wings(a)
    nb = len(pv)
    verts = pv + wv
    faces = pf + [tuple(nb + i for i in f) for f in wf]
    fields = ["pack"] * len(pf) + wfields
    opened = pv + wo
    mesh = bpy.data.meshes.new(OBJ_NAME)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    # flat wings, the rounded pack smooth
    mesh.polygons.foreach_set("use_smooth", [f == "pack" for f in fields])
    uvl = mesh.uv_layers.new(name="UVMap")
    uv = []
    offs = ((-3.0, -3.0), (3.0, -3.0), (0.0, 3.0))
    for poly, field in zip(mesh.polygons, fields):
        x0, y0, x1, y1 = FIELDS[field]
        cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
        for c in range(len(poly.vertices)):
            ox, oy = offs[c % 3]
            uv.extend(((cx + ox) / ATLAS, 1.0 - (cy + oy) / ATLAS))
    uvl.uv.foreach_set("vector", uv)
    mesh.materials.append(make_material(vcol))
    if vcol == "psx":
        psx_colours(mesh)
    mesh.update()

    obj = bpy.data.objects.new(OBJ_NAME, mesh)
    figure.objects.link(obj)
    obj.parent = rig
    obj.parent_type = "OBJECT"
    vg = obj.vertex_groups.new(name=BONE)
    vg.add(list(range(len(verts))), 1.0, "REPLACE")
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig

    basis = obj.shape_key_add(name=SHAPE_KEYS[0], from_mix=False)
    keys = {SHAPE_KEYS[0]: basis}
    k_open = obj.shape_key_add(name="wings_open", from_mix=False)
    k_open.data.foreach_set("co", [c for p in opened for c in p])
    keys["wings_open"] = k_open
    # glTF morph targets are offsets from the base mesh, and Blender's exporter takes each key minus the Basis
    # (it ignores relative_key). So these keys hold Basis + (pose - open): with wings_open = 1 on top, the pose.
    for kind in ON_OPEN:
        k = obj.shape_key_add(name=kind, from_mix=False)
        k.data.foreach_set("co", [c for b, p in zip(verts, opened) for c in
                                  (Vector(b) + Vector(pose(p, a, kind)) - Vector(p))])
        keys[kind] = k
    for k in mesh.shape_keys.key_blocks:
        k.value = 0.0
        k.slider_min, k.slider_max = 0.0, 1.0
    mesh.shape_keys.use_relative = True
    mesh.update()
    return obj, nb, len(pf), len(wf)


# ------------------------------------------------------------------ self check

def overlaps(obj, others, depsgraph):
    co, polys = world_verts(obj, depsgraph)
    tree = BVHTree.FromPolygons(co, polys)
    found = {}
    for o in others:
        oc, op = world_verts(o, depsgraph)
        n = len(tree.overlap(BVHTree.FromPolygons(oc, op)))
        if n:
            found[o.name] = n
    return found


def read_glb_json(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"glTF":
        fail(5, "self check failed: %s is no GLB." % os.path.basename(path))
    length, kind = struct.unpack_from("<II", data, 12)
    return json.loads(data[20:20 + length].decode("utf-8"))


def tris(mesh):
    return sum(len(p.vertices) - 2 for p in mesh.polygons)


def main():
    opt = parse_args(sys.argv)
    if tuple(bpy.app.version[:2]) != BLENDER_VERSION:
        fail(3, "needs Blender %d.%d, this is %s." % (BLENDER_VERSION + (bpy.app.version_string,)))
    rig, puppet, figure, kart = open_kit(opt["kit_blend"])
    before = rig_state(rig)
    puppet_tris = tris(puppet.data)
    vcol = "psx"
    for m in puppet.data.materials:
        if m is not None and m.name.endswith("]") and "[vcol " in m.name:
            vcol = m.name[m.name.rindex("[vcol ") + 6:-1]

    # build in the rest pose (the seated bind pose), then back to the stored pose
    stored_pose = rig.data.pose_position
    rig.data.pose_position = "REST"
    bpy.context.view_layer.update()
    a = anchors(rig, puppet)
    obj, _nb, pack_tris, wing_tris = build_object(rig, figure, a, vcol)

    # self check: the folded backpack against the kart and the body, rest pose and stored pose
    for label in ("REST", "POSE"):
        rig.data.pose_position = label
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        hit = overlaps(obj, kart + [puppet], dg)
        if hit:
            fail(5, "self check failed: the folded backpack touches %s (%s pose)." %
                 (", ".join("%s %d pairs" % kv for kv in sorted(hit.items())), label.lower()))
    rig.data.pose_position = stored_pose
    bpy.context.view_layer.update()
    if rig_state(rig) != before:
        fail(5, "self check failed: the skeleton changed.")

    # the exporter of Figure as the kit stored it, plus the morph targets
    os.makedirs(opt["out"], exist_ok=True)
    exporter = figure.exporters[0]
    props = exporter.export_properties
    for key, value in EXPORT_CHANGES.items():
        if not hasattr(props, key):
            fail(5, "the glTF exporter has no setting '%s' (this Blender is not as tested)." % key)
        setattr(props, key, value)
    glb = os.path.join(opt["out"], GLB_NAME)
    if os.path.exists(glb):
        os.remove(glb)
    exporter.filepath = glb
    for o in bpy.data.objects:
        o.select_set(False)
    bpy.context.view_layer.objects.active = rig
    with bpy.context.temp_override(collection=figure):
        bpy.ops.collection.exporter_export(index=0)
    if not os.path.isfile(glb):
        fail(5, "self check failed: the exporter wrote no %s." % GLB_NAME)
    exporter.filepath = "//" + GLB_NAME
    kit_col = bpy.data.collections.get("CreatorKit")
    if kit_col is not None:
        for e in kit_col.exporters:
            e.filepath = "//" + LOCAL_NAME

    # read it back: the morph target names, no kit part
    j = read_glb_json(glb)
    names = None
    for m in j.get("meshes", []):
        if m.get("name") == OBJ_NAME or (m.get("extras") or {}).get("targetNames"):
            names = (m.get("extras") or {}).get("targetNames")
    if names != list(SHAPE_KEYS[1:]):
        fail(5, "self check failed: the morph targets in the .glb are %s, not %s." % (names, list(SHAPE_KEYS[1:])))
    for node in j.get("nodes", []):
        if "ctr_kit" in (node.get("extras") or {}):
            fail(5, "self check failed: the .glb has a kit part (%s)." % node.get("name"))
    for mat in j.get("materials", []):
        if mat.get("name", "").startswith("ctrkit_"):
            fail(5, "self check failed: the .glb has a kit material (%s)." % mat.get("name"))

    total = puppet_tris + pack_tris + wing_tris
    if pack_tris + wing_tris > TRI_LIMIT or total > BUDGET:
        fail(5, "self check failed: %d triangles for backpack and wings, %d in all." % (pack_tris + wing_tris, total))

    if opt["scene"]:
        scene_path = os.path.join(opt["out"], SCENE_NAME)
        for old in (scene_path, scene_path + "1"):
            if os.path.exists(old):
                os.remove(old)
        bpy.ops.wm.save_as_mainfile(filepath=scene_path, compress=False, copy=True)

    text = [
        "Dummy Glider - the Creator Kit puppet (Hi-Res) with a backpack and collapsible wings",
        "",
        "Files",
        "  %s  the figure alone (puppet + backpack, skeleton, clips, morph targets). No kart: share this one." % GLB_NAME,
        "  %s  the kit scene with the backpack. It contains the kart from your game files:" % SCENE_NAME,
        "      keep it on this computer, never share it.",
        "  front/side/back x folded/open .png  pictures of the scene",
        "",
        "Shape keys of %s (glTF morph targets; the base mesh is %s)" % (OBJ_NAME, SHAPE_KEYS[0]),
        "  %-13s the Basis: wing segments nested in the root segment on the backpack" % SHAPE_KEYS[0],
        "  %-13s every segment slid out sideways" % "wings_open",
        "  %-13s wing tips raised in a curve        (on top of wings_open = 1)" % "flap",
        "  %-13s swept back, tips down              (on top of wings_open = 1)" % "dive",
        "  %-13s leading edge up, tips forward      (on top of wings_open = 1)" % "pull_up",
        "",
        "Triangles",
        "  figure (puppet)        %6d" % puppet_tris,
        "  backpack               %6d" % pack_tris,
        "  wings                  %6d" % wing_tris,
        "  backpack + wings       %6d   (limit %d)" % (pack_tris + wing_tris, TRI_LIMIT),
        "  total                  %6d   (budget %d)" % (total, BUDGET),
        "",
        "The backpack hangs on the bone '%s'; skeleton, rest pose and clips are the kit's." % BONE,
        "Made by creator_kit_glider.py with Blender %s from %s." % (bpy.app.version_string,
                                                                  os.path.basename(opt["kit_blend"])),
        "",
    ]
    with open(os.path.join(opt["out"], README_NAME), "wb") as f:
        f.write("\r\n".join(text).encode("ascii"))
    print("dummy glider: ok glb=%s morph=%s triangles figure=%d pack=%d wings=%d total=%d" %
          (GLB_NAME, ",".join(names), puppet_tris, pack_tris, wing_tris, total))


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(5)
