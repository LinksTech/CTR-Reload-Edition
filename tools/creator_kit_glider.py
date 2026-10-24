# Creator Kit "Dummy Glider": puts a backpack with folding wings on the Creator
# Kit puppet of the Hi-Res way and exports the figure with the wings as glTF
# morph targets. Everything added here is made by code in this file (a rounded
# box and four hinged wing panels) - no external asset, no game data. The kart
# is only read from the kit file at run time, to check that the wings stay clear
# of it; no kart number is written into this script.
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
#   LIESMICH.txt              shape keys, triangles, the measured checks and the
#                             preview pictures (made by a separate local render
#                             script, not part of this tool)
#
# The backpack mesh "glider_pack" hangs 100 % on the bone "chest" (one vertex
# group, Armature modifier like the puppet), so it follows every clip.
#
# The wings. Each wing has two flat panels and two hinges, both hinge axes
# along the chord (kart front to back): the inner panel hangs on the top edge
# of the backpack's side, the outer panel on the inner panel's tip. Folded, the
# inner panel hangs down along the side of the pack and the outer panel stands
# up against it, a zig-zag of two panels within the pack's height and depth.
# Open, the inner panel lies almost flat (5 degrees up) and the outer one is
# raised (25 degrees), so the tips stay above the rear wheels while the driver
# leans in a drift. A morph target moves every vertex on a straight line, so a
# turn of angle T split into the 3 straight pieces of the key chain keeps
# cos(T / 6) of a panel's length halfway along a piece. Each panel turns at most
# 95 degrees from folded to open (inner 95, outer 65), so no panel ever gets
# shorter than 96 %; the script measures it.
#
# Shape keys (glTF morph targets, in this order; the base mesh is wings_folded):
#   wings_fold_33   one third of the way: every hinge at a third of its angle
#   wings_fold_66   two thirds of the way
#   wings_open      open
#     Play the chain wings_folded -> wings_fold_33 -> wings_fold_66 -> wings_open
#     mixing only the two neighbouring keys at a time.
#   flap            wings beat up (both hinges further up)      } offsets on top
#   dive            swept back, tips lower                      } of wings_open = 1
#   pull_up         leading edge up, tips higher, a little forward }
# Every key holds an absolute shape (Blender's glTF exporter writes each key
# minus the Basis and ignores relative_key); flap, dive and pull_up hold
# Basis + (pose - open), so with wings_open = 1 on top they give the pose.
#
# Self check (exit 5 when one fails): every panel keeps >= 96 % of its length
# along the chain (30 steps per piece); folded, no wing vertex lies more than
# 30 mm outside the pack's bounding box; folded and open have no triangle pair
# crossing and no vertex inside the kart parts or the body, in the rest pose and
# in every frame of every seated clip; the skeleton is unchanged; the morph
# target names read back from the .glb; no kit part in the .glb; at most 2000
# triangles for backpack and wings.
#
# Nothing of the kit is changed: the skeleton (bones, names, rest pose), the
# puppet, the clips and the export settings of the collection Figure stay as
# they are; the export only turns on the morph targets (export_morph).
#
# Exit codes (as templates/make_creator_kit.py): 0 good, 2 wrong call, 3 wrong
# Blender version, 4 input missing or invalid, 5 the self check failed (the
# outputs are removed then). Same Blender and same kit file give the same bytes
# of the .glb (start Blender with --threads 1, as the kit does).
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
# the preview pictures a separate local render script writes next to the files (LIESMICH.txt lists them)
PICTURES = (
    ("front|side|back|top|rear34-wings_folded.png", "the five views with the wings folded"),
    ("front|side|back|top|rear34-wings_open.png", "the same views with the wings open"),
    ("rear34-flap|dive|pull_up.png", "the poses on top of wings_open = 1, from behind and above"),
    ("seq-side|back-0|33|66|100.png", "side and back view along the chain: folded, fold_33, fold_66, open"),
)

OBJ_NAME = "glider_pack"
BONE = "chest"
CHAIN = ("wings_folded", "wings_fold_33", "wings_fold_66", "wings_open")
CHAIN_T = (0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0)
POSES = ("flap", "dive", "pull_up")          # offsets on top of wings_open
SHAPE_KEYS = CHAIN + POSES
# What the export changes in the kit's stored figure exporter: the morph targets on (normals with them,
# no tangents, dense - as the kit's export_try_sparse_sk False). Every other setting stays the kit's.
EXPORT_CHANGES = {"export_morph": True, "export_morph_normal": True, "export_morph_tangent": False}
TRI_LIMIT = 2000          # backpack + wings
BUDGET = 64000            # the game's budget for a whole file
LENGTH_MIN = 0.96         # a panel's length along the key chain, of its real length
CHAIN_STEPS = 30          # measuring steps per piece of the chain
OVERHANG_MAX = 0.030      # folded: how far a wing vertex may lie outside the pack's box
SEATED_CLIPS = ("idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit",
                "finish_win", "finish_lose")
STANDING_CLIPS = ("podium_1", "podium_2", "podium_3", "lose", "menu")   # no kart: body only
RAY_DIRS = ((1.0, 0.013, 0.007), (0.011, 1.0, 0.017), (0.019, 0.009, 1.0))

# ---- the design, in metres (made up here; placed from the puppet and the bone chest at run time)
GAP_BACK = 0.014          # backpack front face behind the puppet's back
PACK_W = 0.27             # width (x)
PACK_D = 0.16             # depth (y)
PACK_H = 0.36             # height (z); its top is the tail of chest
PACK_BEVEL = 0.018
PACK_BEVEL_SEGS = 2
PANEL_T = 0.011           # panel thickness
PANEL_GAP = 0.002         # between the two panels of a wing
SIDE_GAP = 0.001          # between the folded inner panel and the pack
PANEL_L = (0.38, 0.37)    # inner, outer panel length (the outer one is the shorter: it folds up inside)
CHORD = PACK_D            # the panels are as deep as the pack
TIP_CHORD = 0.75          # the outer panel's tip, of its chord (leading edge straight)
# 6-point profile of a panel (u along the chord from the leading edge, v across the thickness), convex
PROFILE = ((0.0, 0.5), (0.08, 1.0), (0.9, 1.0), (1.0, 0.5), (0.9, 0.0), (0.08, 0.0))
# hinge angles (degrees from the horizontal, outward, + up): inner panel, outer panel - both absolute
FOLDED = (-90.0, 90.0)
OPEN = (5.0, 25.0)
# the poses on top of open: hinge angles, pitch (leading edge up), sweep (+ tips back)
POSE_SET = {
    "flap": {"angles": (30.0, 45.0), "pitch": 0.0, "sweep": 0.0},
    "dive": {"angles": (-3.0, 5.0), "pitch": 0.0, "sweep": 30.0},
    "pull_up": {"angles": (10.0, 35.0), "pitch": 15.0, "sweep": -8.0},
}

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
    kart = sorted([o for o in bpy.data.collections["Kart"].all_objects if o.type == "MESH"], key=lambda o: o.name)
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
    z1 = tail.z
    z0 = z1 - PACK_H
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


def rot2(x, z, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return x * c - z * s, x * s + z * c


def panel_local():
    """The two panels in their own frame: per panel a list of (s, u_y, n) - s along the span from the hinge end,
    y across the chord (metres from the leading edge), n across the thickness (0 = the face the inner hinge is on)
    - as two rings of the profile (root, tip)."""
    panels = []
    for k, length in enumerate(PANEL_L):
        pts = []
        for s, scale in ((0.0, 1.0), (length, TIP_CHORD if k == 1 else 1.0)):
            for u, v in PROFILE:
                pts.append((s, u * CHORD * scale, v * PANEL_T))
        panels.append(pts)
    return panels


def hinges(a, angles):
    """The two hinge lines (x outward from the pack's middle, z) of the left wing for (inner, outer) angles."""
    h0 = (PACK_W / 2 + SIDE_GAP, a["z1"])
    dx, dz = rot2(PANEL_L[0], PANEL_T + PANEL_GAP, angles[0])
    return h0, (h0[0] + dx, h0[1] + dz)


def place(a, side, k, p, angles, pitch=0.0, sweep=0.0):
    """A point p = (s, y, n) of panel k of the wing on `side` (+1 left, -1 right) for the hinge angles, then the
    pose's pitch (about the span line through the inner hinge, + lifts the leading edge) and sweep (about the
    vertical through the inner hinge, + moves the tip back; the axis at the trailing edge for a sweep back, at the
    leading edge for a sweep forward, so the panel's root never swings into the pack)."""
    s, y, n = p
    h0, h1 = hinges(a, angles)
    if k == 0:
        dx, dz = rot2(s, n, angles[0])
        x, z = h0[0] + dx, h0[1] + dz
    else:
        dx, dz = rot2(s, n - PANEL_T, angles[1])
        x, z = h1[0] + dx, h1[1] + dz
    y = a["front"] + y
    if pitch:
        # in (y, z) a turn by +angle lifts +y (the trailing edge); by -pitch the leading edge goes up
        ym = a["front"] + CHORD / 2
        dy, dz = rot2(y - ym, z - h0[1], -pitch)
        y, z = ym + dy, h0[1] + dz
    if sweep:
        ya = a["front"] + (CHORD if sweep > 0 else 0.0)
        dx, dy = rot2(x - h0[0], y - ya, sweep)
        x, y = h0[0] + dx, ya + dy
    return (a["x"] + side * x, y, z)


def place_hinge(a, side, which, angles, pitch=0.0, sweep=0.0):
    """Hinge `which` (0 inner, 1 outer) at the chord's middle, moved as the panels are."""
    if which == 0:
        return place(a, side, 0, (0.0, CHORD / 2, 0.0), angles, pitch, sweep)
    return place(a, side, 1, (0.0, CHORD / 2, PANEL_T), angles, pitch, sweep)


def lerp_angles(t):
    return tuple(f + (o - f) * t for f, o in zip(FOLDED, OPEN))


def wing_layout():
    """Both wings: vertex records (side, panel, local point), triangles (indices into the records), the colour
    field of each triangle and the first record of each panel."""
    recs, faces, fields, starts = [], [], [], []
    loc = panel_local()
    n = len(PROFILE)
    for side in (1.0, -1.0):
        for k in range(len(PANEL_L)):
            base = len(recs)
            starts.append(base)
            recs.extend((side, k, p) for p in loc[k])
            quads = [(base + i, base + (i + 1) % n, base + n + (i + 1) % n, base + n + i) for i in range(n)]
            caps = [tuple(base + i for i in range(n)), tuple(base + n + i for i in range(n))]
            for qi, f in enumerate(quads + caps):
                field = "root" if k == 0 else ("tip" if qi == n + 1 else "wing")
                for t in range(1, len(f) - 1):        # a fan: the faces are convex
                    faces.append((f[0], f[t], f[t + 1]))
                    fields.append(field)
    return recs, faces, fields, starts


def wing_shape(a, recs, angles, pitch=0.0, sweep=0.0):
    return [place(a, side, k, p, angles, pitch, sweep) for side, k, p in recs]


def outward(faces, pos, starts, count):
    """Turn every triangle of a panel so that its normal points away from the panel's centre."""
    out = []
    bounds = list(starts) + [count]
    centre = []
    for i in range(len(starts)):
        pts = [Vector(pos[j]) for j in range(bounds[i], bounds[i + 1])]
        centre.append(sum(pts, Vector()) / len(pts))
    for f in faces:
        piece = max(i for i in range(len(starts)) if starts[i] <= f[0])
        p0, p1, p2 = (Vector(pos[j]) for j in f)
        nrm = (p1 - p0).cross(p2 - p0)
        out.append(f if nrm.dot((p0 + p1 + p2) / 3.0 - centre[piece]) >= 0.0 else (f[0], f[2], f[1]))
    return out


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
    """The mesh with its shape keys. Returns the object and what the checks need."""
    pv, pf = build_pack(a)
    recs, wf, wfields, starts = wing_layout()
    nb = len(pv)
    shapes = {}
    for name, t in zip(CHAIN, CHAIN_T):
        shapes[name] = pv + wing_shape(a, recs, lerp_angles(t))
    for name in POSES:
        ps = POSE_SET[name]
        shapes[name] = pv + wing_shape(a, recs, ps["angles"], ps["pitch"], ps["sweep"])
    basis = shapes[CHAIN[0]]
    wf = outward(wf, basis[nb:], starts, len(recs))
    faces = pf + [tuple(nb + i for i in f) for f in wf]
    fields = ["pack"] * len(pf) + wfields

    mesh = bpy.data.meshes.new(OBJ_NAME)
    mesh.from_pydata(basis, [], faces)
    mesh.update()
    # flat panels, the rounded pack smooth
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
    vg.add(list(range(len(basis))), 1.0, "REPLACE")
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig

    obj.shape_key_add(name=CHAIN[0], from_mix=False)
    for name in CHAIN[1:]:
        k = obj.shape_key_add(name=name, from_mix=False)
        k.data.foreach_set("co", [c for p in shapes[name] for c in p])
    # glTF morph targets are offsets from the base mesh, and Blender's exporter takes each key minus the Basis
    # (it ignores relative_key). So these keys hold Basis + (pose - open): with wings_open = 1 on top, the pose.
    opened = shapes["wings_open"]
    for name in POSES:
        k = obj.shape_key_add(name=name, from_mix=False)
        k.data.foreach_set("co", [c for b, q, o in zip(basis, shapes[name], opened) for c in
                                  (Vector(b) + Vector(q) - Vector(o))])
    for k in mesh.shape_keys.key_blocks:
        k.value = 0.0
        k.slider_min, k.slider_max = 0.0, 1.0
    mesh.shape_keys.use_relative = True
    mesh.update()

    # the pieces (pack, then each panel) as vertex ranges, for the checks
    pieces = [("pack", 0, nb)]
    bounds = list(starts) + [len(recs)]
    for i, s in enumerate(starts):
        side = "left" if recs[s][0] > 0 else "right"
        pieces.append(("%s %s" % (side, "inner" if recs[s][1] == 0 else "outer"), nb + s, nb + bounds[i + 1]))
    info = {"recs": recs, "nb": nb, "pieces": pieces, "shapes": shapes, "a": a,
            "pack_tris": len(pf), "wing_tris": len(wf)}
    return obj, info


# ------------------------------------------------------------------ checks

def chain_lengths(info):
    """Per panel the smallest hinge-to-tip length along the chain (each vertex on a straight line between
    neighbouring keys), of its real length. The hinges move like vertices of their panels."""
    a, recs, nb = info["a"], info["recs"], info["nb"]
    keys = [info["shapes"][n] for n in CHAIN]
    result = {}
    for name, lo, hi in info["pieces"][1:]:
        side, k = recs[lo - nb][0], recs[lo - nb][1]
        hinge = [Vector(place_hinge(a, side, k, lerp_angles(t))) for t in CHAIN_T]
        tips = [i for i in range(lo, hi) if recs[i - nb][2][0] > 0.0]      # the tip ring
        worst = 1.0
        for i in tips:
            real = (Vector(keys[0][i]) - hinge[0]).length
            for seg in range(len(CHAIN) - 1):
                for step in range(CHAIN_STEPS + 1):
                    s = step / CHAIN_STEPS
                    p = Vector(keys[seg][i]).lerp(Vector(keys[seg + 1][i]), s)
                    h = hinge[seg].lerp(hinge[seg + 1], s)
                    worst = min(worst, (p - h).length / real)
        result[name] = worst
    return result


def overhang(info):
    """Folded: how far wing vertices lie outside the pack's bounding box, per direction (metres). The pack is
    axis-aligned in its own frame (the Basis coordinates)."""
    nb = info["nb"]
    folded = info["shapes"][CHAIN[0]]
    pack, wing = folded[:nb], folded[nb:]
    res = {}
    for ax, name in enumerate("xyz"):
        pmin, pmax = min(p[ax] for p in pack), max(p[ax] for p in pack)
        res["-" + name] = max(0.0, pmin - min(p[ax] for p in wing))
        res["+" + name] = max(0.0, max(p[ax] for p in wing) - pmax)
    return res


def set_keys(obj, state):
    """folded, a chain key, open, or a pose (on top of open)."""
    kb = obj.data.shape_keys.key_blocks
    for k in kb:
        k.value = 0.0
    if state in POSES:
        kb["wings_open"].value = 1.0
        kb[state].value = 1.0
    elif state != CHAIN[0]:
        kb[state].value = 1.0


def set_clip(rig, scene, clip, frame):
    if clip is None:
        rig.data.pose_position = "REST"
    else:
        rig.data.pose_position = "POSE"
        act = bpy.data.actions[clip]
        if rig.animation_data is None:
            rig.animation_data_create()
        rig.animation_data.action = act
        if getattr(rig.animation_data, "action_slot", 1) is None and len(act.slots):
            rig.animation_data.action_slot = act.slots[0]
    scene.frame_set(frame)
    bpy.context.view_layer.update()


class Surface:
    def __init__(self, obj, dg):
        co, polys = world_verts(obj, dg)
        self.name = obj.name
        self.bvh = BVHTree.FromPolygons(co, polys)
        self.lo = Vector((min(v.x for v in co), min(v.y for v in co), min(v.z for v in co)))
        self.hi = Vector((max(v.x for v in co), max(v.y for v in co), max(v.z for v in co)))

    def inside(self, p):
        """Ray parity in 3 directions, inside when 2 of 3 rays cross the surface an odd number of times (the kit
        kart is not watertight everywhere)."""
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
                origin = h[0] + d * 1e-5
            odd += hits & 1
        return odd >= 2


def contacts(obj, others, pieces, every_vertex):
    """Triangle pairs crossing and vertices inside, against each surface. Without crossings a closed piece is
    wholly inside or wholly outside, so one vertex per piece decides (every_vertex tests them all)."""
    dg = bpy.context.evaluated_depsgraph_get()
    co, polys = world_verts(obj, dg)
    tree = BVHTree.FromPolygons(co, polys)
    probe = range(len(co)) if every_vertex else [lo for _n, lo, _h in pieces]
    pairs = inside = 0
    for s in others:
        pairs += len(tree.overlap(s.bvh))
        inside += sum(1 for i in probe if s.inside(co[i]))
    return pairs, inside


def sweep(obj, rig, scene, kart, puppet, pieces, log):
    """Rest pose and every frame of every clip: folded and open are gates (seated clips against the kart parts
    and the body, standing clips against the body), the chain keys and the poses are information.
    Returns (gate failures, rows)."""
    states = CHAIN + POSES
    rows, failures = [], []
    clips = [(None, (0, 0), True)]
    for c in SEATED_CLIPS + STANDING_CLIPS:
        if c not in bpy.data.actions:
            failures.append("clip %s is missing" % c)
            continue
        f0, f1 = bpy.data.actions[c].frame_range
        clips.append((c, (int(round(f0)), int(round(f1))), c in SEATED_CLIPS))
    for clip, (f0, f1), seated in clips:
        worst = {s: [0, 0] for s in states}
        for frame in range(f0, f1 + 1):
            set_clip(rig, scene, clip, frame)
            dg = bpy.context.evaluated_depsgraph_get()
            others = ([Surface(o, dg) for o in kart] if seated else []) + [Surface(puppet, dg)]
            for state in states:
                set_keys(obj, state)
                bpy.context.view_layer.update()
                p, i = contacts(obj, others, pieces, clip is None)
                worst[state][0] = max(worst[state][0], p)
                worst[state][1] = max(worst[state][1], i)
        set_keys(obj, CHAIN[0])
        name = clip or "rest pose"
        rows.append((name, f0, f1, seated, worst))
        for state in ("wings_folded", "wings_open"):
            if worst[state] != [0, 0]:
                failures.append("%s %s: %d triangle pairs, %d vertices inside" %
                                (name, state, worst[state][0], worst[state][1]))
        log("clip %-12s frames %3d-%-3d %s  %s" % (name, f0, f1, "kart+body" if seated else "body     ",
            "  ".join("%s %d/%d" % (s, worst[s][0], worst[s][1]) for s in states)))
    set_clip(rig, scene, "idle" if "idle" in bpy.data.actions else None, 0)
    return failures, rows


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
    scene = bpy.context.scene
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
    obj, info = build_object(rig, figure, a, vcol)
    rig.data.pose_position = stored_pose
    bpy.context.view_layer.update()
    if rig_state(rig) != before:
        fail(5, "self check failed: the skeleton changed.")
    pack_tris, wing_tris = info["pack_tris"], info["wing_tris"]
    total = puppet_tris + pack_tris + wing_tris

    # the exporter of Figure as the kit stored it, plus the morph targets
    os.makedirs(opt["out"], exist_ok=True)
    exporter = figure.exporters[0]
    props = exporter.export_properties
    for key, value in EXPORT_CHANGES.items():
        if not hasattr(props, key):
            fail(5, "the glTF exporter has no setting '%s' (this Blender is not as tested)." % key)
        setattr(props, key, value)
    glb = os.path.join(opt["out"], GLB_NAME)
    scene_path = os.path.join(opt["out"], SCENE_NAME)
    readme = os.path.join(opt["out"], README_NAME)
    for old in (glb, scene_path, scene_path + "1", readme):
        if os.path.exists(old):
            os.remove(old)
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
    if opt["scene"]:
        bpy.ops.wm.save_as_mainfile(filepath=scene_path, compress=False, copy=True)

    # ---- the checks (after the export, so they cannot change it); a failure removes the outputs
    problems = []

    def log(text):
        print("dummy glider: " + text)

    j = read_glb_json(glb)
    names = None
    for m in j.get("meshes", []):
        if m.get("name") == OBJ_NAME:
            names = (m.get("extras") or {}).get("targetNames")
    if names != list(SHAPE_KEYS[1:]):
        problems.append("the morph targets in the .glb are %s, not %s" % (names, list(SHAPE_KEYS[1:])))
    for node in j.get("nodes", []):
        if "ctr_kit" in (node.get("extras") or {}):
            problems.append("the .glb has a kit part (%s)" % node.get("name"))
    for mat in j.get("materials", []):
        if mat.get("name", "").startswith("ctrkit_"):
            problems.append("the .glb has a kit material (%s)" % mat.get("name"))
    if pack_tris + wing_tris > TRI_LIMIT or total > BUDGET:
        problems.append("%d triangles for backpack and wings, %d in all" % (pack_tris + wing_tris, total))

    lengths = chain_lengths(info)
    for name in sorted(lengths):
        log("length along the chain, %-12s min %.2f %%" % (name, 100.0 * lengths[name]))
    if min(lengths.values()) < LENGTH_MIN:
        problems.append("a panel gets shorter than %.0f %% along the chain (%.2f %%)" %
                        (100 * LENGTH_MIN, 100.0 * min(lengths.values())))
    over = overhang(info)
    log("folded, outside the pack's box: " + ", ".join("%s %.1f mm" % (k, 1000 * over[k]) for k in sorted(over)))
    if max(over.values()) > OVERHANG_MAX + 1e-9:
        problems.append("the folded wings stick out %.1f mm beyond the pack (limit %.0f)" %
                        (1000 * max(over.values()), 1000 * OVERHANG_MAX))

    sweep_fail, rows = sweep(obj, rig, scene, kart, puppet, info["pieces"], log)
    problems.extend(sweep_fail)
    if rig_state(rig) != before:
        problems.append("the skeleton changed")

    if problems:
        for p in problems:
            log("self check failed: " + p)
        for path in (glb, scene_path):
            if os.path.exists(path):
                os.remove(path)
        sys.exit(5)

    poses_info = []
    for state in POSES + CHAIN[1:3]:
        bad = ["%s %d/%d" % (r[0], r[4][state][0], r[4][state][1]) for r in rows if r[4][state] != [0, 0]]
        poses_info.append("  %-14s %s" % (state, "clear in every clip" if not bad else "touches in " + ", ".join(bad)))
    pic = ["  %-46s %s" % p for p in PICTURES]
    text = [
        "Dummy Glider - the Creator Kit puppet (Hi-Res) with a backpack and folding wings",
        "",
        "Files",
        "  %s  the figure alone (puppet + backpack, skeleton, clips, morph targets). No kart: share this one." % GLB_NAME,
        "  %s  the kit scene with the backpack. It contains the kart from your game files:" % SCENE_NAME,
        "      keep it on this computer, never share it.",
        "Pictures (preview renders of the scene, with the kit kart)",
    ] + pic + [
        "",
        "Shape keys of %s (glTF morph targets; the base mesh is %s)" % (OBJ_NAME, CHAIN[0]),
        "  %-14s the Basis: each wing a zig-zag of two panels along the side of the pack" % CHAIN[0],
        "  %-14s every hinge at a third of its angle" % CHAIN[1],
        "  %-14s every hinge at two thirds" % CHAIN[2],
        "  %-14s open: inner panel %g deg up, outer panel %g deg up" % (CHAIN[3], OPEN[0], OPEN[1]),
        "  %-14s wings beat up                       (on top of wings_open = 1)" % "flap",
        "  %-14s swept back, tips lower              (on top of wings_open = 1)" % "dive",
        "  %-14s leading edge up, tips higher         (on top of wings_open = 1)" % "pull_up",
        "  Play the chain %s mixing only two neighbouring keys at a time." % " -> ".join(CHAIN),
        "",
        "Checks",
        "  shortest panel along the chain   %.2f %% of its length (limit %.0f %%)" %
        (100.0 * min(lengths.values()), 100 * LENGTH_MIN),
        "  folded, beyond the pack's box    %s (limit %.0f mm)" %
        (", ".join("%s %.1f mm" % (k, 1000 * over[k]) for k in sorted(over)), 1000 * OVERHANG_MAX),
        "  folded and open: no contact with the kart parts and the body in the rest pose and in every frame of",
        "  " + ", ".join(SEATED_CLIPS) + ";",
        "  with the body in every frame of " + ", ".join(STANDING_CLIPS) + ".",
        "  The other keys (information):",
    ] + poses_info + [
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
    with open(readme, "wb") as f:
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
