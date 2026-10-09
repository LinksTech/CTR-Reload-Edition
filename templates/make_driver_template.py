# Builds templates/driver-template.blend: a made-up placeholder driver with a
# kart, at Crash's size, with the seven shape keys the character importer reads
# (docs/ANIMATIONS.md). Everything in it is made here from simple shapes - no
# game data - and released under CC0 (templates/LICENSE).
#
#   blender --background --factory-startup --python templates/make_driver_template.py
#   blender --background --factory-startup --python templates/make_driver_template.py -- --glb out.glb
#
# Without arguments the .blend is written next to this script. --blend <path>
# writes it elsewhere; --glb <path> also exports the model through the
# collection exporter stored in the file (to that path; the saved file keeps
# //driver-template.glb). Same Blender version, same bytes.
#
# Axes and size. Blender: +Z up, the nose toward -Y, the ground at z = 0, one
# unit = one meter. make-char turns that into game units (+Y up, +Z forward)
# at 64 per meter (RLDMK_UNITS in tools/rldpack_char.inc); --fit crash then
# scales the model so that its kart is as long as Crash's, 112.4 game units.
# The kart here is 112.4 / 64 = 1.75625 m long, so the factor comes out as 1.

import math
import os
import re
import struct
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

UNITS = 64.0  # game units per Blender unit (RLDMK_UNITS)

# Crash with his kart (RLDDUM_CRASH_BOX, tools/rldpack_dummy.inc), game units:
# x0 y0 z0 x1 y1 z1 with +Y up and +Z forward.
CRASH_BOX = (-33.8, 5.6, -54.1, 34.3, 88.3, 58.3)

KART_FRONT = -CRASH_BOX[5] / UNITS  # Blender y of the nose (game +Z = Blender -Y)
KART_BACK = -CRASH_BOX[2] / UNITS
KART_HALF_WIDTH = 28.0 / UNITS      # a retail kart is 56 wide
KART_BOTTOM = 6.0 / UNITS           # the game draws the wheels; the body floats like Crash's
KART_TOP = 16.0 / UNITS             # the seat line (RLDMK_HIP_Y)

MID_Y = 0.5 * (KART_FRONT + KART_BACK)
SEAT_Y = MID_Y + 0.22               # the driver sits behind the middle
WHEEL_CENTER = Vector((0.0, MID_Y - 0.30, 0.62))
WHEEL_RADIUS = 0.15

SHAPE_KEYS = ("steer_left", "steer_right", "win", "lose", "jump", "crash", "reverse")
EMPTY_KEYS = ("jump", "crash", "reverse")  # left at the basis and muted: not exported until shaped

EXPORT_TEXT = """\
DRIVER TEMPLATE - HOW TO EXPORT (CC0, see templates/LICENSE)

1. Model your driver and kart in the collection "Driver".
   Nose toward -Y, +Z up, the ground at z = 0, 1 unit = 1 meter.
   The empty "CrashSize" (collection "Guides") is Crash with his kart.
   The game draws the wheels: leave them out (or use the Wheels card).
2. Shape keys on the driver mesh (Object Data > Shape Keys):
   steer_left, steer_right, win, lose, jump, crash, reverse.
   All are optional. jump, crash and reverse are empty and muted here:
   a muted key is not exported, so the game moves that pose itself.
   Unmute a key once you have shaped it.
   Keep the key values at 0 and do not apply modifiers to a mesh with
   shape keys; no armatures (rigs are not supported yet).
3. Export: Collection properties of "Driver" > Exporters > glTF 2.0 >
   Export (or File > Export All Collections). It writes
   driver-template.glb next to this .blend, with these settings:
   glTF Binary, normals, UVs, materials and textures embedded,
   shape keys (sparse accessors off); no skinning, no animation,
   no cameras, no lights. File > Export > glTF 2.0 works too with the
   same settings.
4. Load the .glb in Reload Studio (Character page, model).

The full guide is docs/ANIMATIONS.md.
"""


def args_after_dashes():
    argv = sys.argv
    return argv[argv.index("--") + 1:] if "--" in argv else []


def smoothstep(a, b, x):
    if x <= a:
        return 0.0
    if x >= b:
        return 1.0
    t = (x - a) / (b - a)
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------- texture

def suit_pixels(size):
    # A small suit texture, made here: orange suit, a white stripe down the
    # front, skin for the face, a blue cap. u runs around the body (0.5 =
    # front), v from the bottom (0) to the top (1).
    orange = (0.93, 0.42, 0.08, 1.0)
    white = (0.95, 0.95, 0.92, 1.0)
    skin = (0.96, 0.76, 0.58, 1.0)
    blue = (0.10, 0.28, 0.75, 1.0)
    dark = (0.55, 0.22, 0.04, 1.0)
    pixels = []
    for y in range(size):
        v = (y + 0.5) / size
        for x in range(size):
            u = (x + 0.5) / size
            if v > 0.84:
                c = blue
            elif v > 0.57:
                c = skin
            elif abs(u - 0.5) < 0.06:
                c = white
            elif ((x // 4) + (y // 4)) % 2 == 0 and v < 0.12:
                c = dark
            else:
                c = orange
            pixels.extend(c)
    return pixels


def make_suit_image():
    size = 64
    image = bpy.data.images.new("driver_suit", size, size, alpha=False)
    image.pixels.foreach_set(suit_pixels(size))
    image.filepath_raw = "//driver_suit.png"
    image.file_format = "PNG"
    image.pack()
    return image


# ---------------------------------------------------------------- materials

def principled(name, color, roughness, image=None, viewport=None):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = viewport if viewport is not None else color  # Solid view
    if mat.node_tree is None:
        mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = color
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = 0.0
    if image is not None:
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = image
        tex.interpolation = "Closest"
        tex.location = (-320.0, 260.0)
        mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


# ---------------------------------------------------------------- shapes

def revolve(bm, uv, center, profile, segments, mat_index, v_range=(0.0, 1.0)):
    # A closed surface of revolution about the vertical axis through `center`:
    # profile = [(z, radius), ...] from the bottom pole to the top pole.
    # u = 0 at the back (+Y), 0.5 at the front (-Y).
    rings = []
    for z, r in profile:
        if r == 0.0:
            rings.append([bm.verts.new((center.x, center.y, z))] * segments)  # a pole
            continue
        ring = []
        for s in range(segments):
            a = 2.0 * math.pi * s / segments
            # a = 0 at the back (+Y); +a turns toward +X
            ring.append(bm.verts.new((center.x + r * math.sin(a), center.y + r * math.cos(a), z)))
        rings.append(ring)
    faces = []
    n = len(profile)
    for i in range(n - 1):
        v0 = v_range[0] + (v_range[1] - v_range[0]) * i / (n - 1)
        v1 = v_range[0] + (v_range[1] - v_range[0]) * (i + 1) / (n - 1)
        for s in range(segments):
            t = (s + 1) % segments
            a, b = rings[i][s], rings[i][t]
            c, d = rings[i + 1][t], rings[i + 1][s]
            if profile[i][1] == 0.0:
                f = bm.faces.new((a, c, d))
                uvs = ((s + 0.5) / segments, v0), ((s + 1) / segments, v1), (s / segments, v1)
            elif profile[i + 1][1] == 0.0:
                f = bm.faces.new((a, b, d))
                uvs = (s / segments, v0), ((s + 1) / segments, v0), ((s + 0.5) / segments, v1)
            else:
                f = bm.faces.new((a, b, c, d))
                uvs = (s / segments, v0), ((s + 1) / segments, v0), ((s + 1) / segments, v1), (s / segments, v1)
            f.material_index = mat_index
            f.smooth = True
            for loop, co in zip(f.loops, uvs):
                loop[uv].uv = co
            faces.append(f)
    return faces


def tube(bm, uv, start, end, radius, segments, mat_index, v_at):
    # A capped cylinder from `start` to `end`.
    axis = (end - start).normalized()
    rot = axis.to_track_quat("Z", "Y").to_matrix()
    ends = []
    for p in (start, end):
        ring = []
        for s in range(segments):
            a = 2.0 * math.pi * s / segments
            ring.append(bm.verts.new(p + rot @ Vector((radius * math.sin(a), radius * math.cos(a), 0.0))))
        ends.append(ring)
    for s in range(segments):
        t = (s + 1) % segments
        f = bm.faces.new((ends[0][s], ends[0][t], ends[1][t], ends[1][s]))
        f.material_index = mat_index
        f.smooth = True
        for loop, co in zip(f.loops, ((s / segments, v_at), ((s + 1) / segments, v_at), ((s + 1) / segments, v_at + 0.02), (s / segments, v_at + 0.02))):
            loop[uv].uv = co
    for ring, flip in ((ends[0], True), (ends[1], False)):
        f = bm.faces.new(list(reversed(ring)) if flip else ring)
        f.material_index = mat_index
        for loop in f.loops:
            loop[uv].uv = (0.25, v_at)


def ball(bm, uv, center, radius, mat_index, v_at, scale=(1.0, 1.0, 1.0)):
    profile = []
    rings = 6
    for i in range(rings + 1):
        a = math.pi * i / rings
        profile.append((-math.cos(a), math.sin(a) if 0 < i < rings else 0.0))
    first = len(bm.verts)
    revolve(bm, uv, Vector((0.0, 0.0, 0.0)), profile, 8, mat_index, (v_at, v_at + 0.02))
    bm.verts.ensure_lookup_table()
    for v in bm.verts[first:]:
        v.co = Vector((center.x + v.co.x * radius * scale[0], center.y + v.co.y * radius * scale[1], center.z + v.co.z * radius * scale[2]))


def box(bm, lo, hi, mat_index):
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    v = [bm.verts.new(c) for c in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                                   (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
    for idx in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        bm.faces.new([v[i] for i in idx]).material_index = mat_index


def new_object(name, bm, materials, collection):
    mesh = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    for m in materials:
        mesh.materials.append(m)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def make_kart(collection, paint, seat_mat):
    bm = bmesh.new()
    # The body: one closed piece, the nose lower and narrower.
    sections = ((KART_FRONT, 0.30, KART_TOP - 0.06), (MID_Y - 0.40, KART_HALF_WIDTH, KART_TOP),
                (KART_BACK, KART_HALF_WIDTH, KART_TOP))
    rings = []
    for y, hw, top in sections:
        rings.append([bm.verts.new(c) for c in ((-hw, y, KART_BOTTOM), (hw, y, KART_BOTTOM), (hw, y, top), (-hw, y, top))])
    for i in range(len(rings) - 1):
        a, b = rings[i], rings[i + 1]
        for k in range(4):
            n = (k + 1) % 4
            bm.faces.new((a[k], a[n], b[n], b[k])).material_index = 0
    bm.faces.new(list(reversed(rings[0]))).material_index = 0
    bm.faces.new(rings[-1]).material_index = 0
    # The seat and its back rest on the body (they stay still with the kart).
    box(bm, (-0.22, SEAT_Y - 0.20, KART_TOP), (0.22, SEAT_Y + 0.20, KART_TOP + 0.05), 1)
    box(bm, (-0.22, SEAT_Y + 0.20, KART_TOP), (0.22, SEAT_Y + 0.28, 0.62), 1)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return new_object("Kart", bm, (paint, seat_mat), collection)


def make_steering_wheel(collection, mat):
    bm = bmesh.new()
    major, minor, seg_major, seg_minor = WHEEL_RADIUS, 0.022, 16, 6
    tilt = Matrix.Rotation(math.radians(-60.0), 3, "X")  # the wheel faces the driver
    rings = []
    for i in range(seg_major):
        a = 2.0 * math.pi * i / seg_major
        ring = []
        for j in range(seg_minor):
            b = 2.0 * math.pi * j / seg_minor
            r = major + minor * math.cos(b)
            ring.append(bm.verts.new(WHEEL_CENTER + tilt @ Vector((r * math.cos(a), r * math.sin(a), minor * math.sin(b)))))
        rings.append(ring)
    for i in range(seg_major):
        a, b = rings[i], rings[(i + 1) % seg_major]
        for j in range(seg_minor):
            k = (j + 1) % seg_minor
            f = bm.faces.new((a[j], b[j], b[k], a[k]))
            f.smooth = True
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return new_object("SteeringWheel", bm, (mat,), collection)


# The driver: body and head as one piece, arms, hands, legs, shoes and eyes
# as pieces of their own in the same mesh. Left and right are the driver's:
# with the nose toward -Y his left is +X.
BODY_CENTER = Vector((0.0, SEAT_Y, 0.0))
BODY_PROFILE = (
    (0.30, 0.0), (0.31, 0.10), (0.34, 0.15), (0.42, 0.17), (0.55, 0.165), (0.68, 0.17),
    (0.76, 0.185), (0.82, 0.17), (0.86, 0.11), (0.89, 0.065), (0.93, 0.07), (0.96, 0.13),
    (1.01, 0.175), (1.08, 0.195), (1.15, 0.19), (1.22, 0.16), (1.28, 0.10), (1.31, 0.04), (1.32, 0.0))
NECK = Vector((0.0, SEAT_Y, 0.89))
HIP = Vector((0.0, SEAT_Y, 0.30))


def shoulder(side):
    return Vector((side * 0.19, SEAT_Y, 0.78))


def hand(side):
    return Vector((side * WHEEL_RADIUS, WHEEL_CENTER.y, WHEEL_CENTER.z))


def make_driver(collection, suit, eyes_mat):
    # make-char takes the largest connected piece as the driver and small
    # pieces in front of him as the steering wheel, so every limb is joined
    # to the body: at one corner, welded onto the nearest corner of the piece
    # it hangs on.
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    layer = bm.verts.layers.int.new("group")
    names = []   # vertex groups in the order they come
    pieces = []  # (corners, index of the piece it hangs on or -1)

    def piece(group, parent, make):
        first = len(bm.verts)
        make()
        bm.verts.ensure_lookup_table()
        if group not in names:
            names.append(group)
        for v in bm.verts[first:]:
            v[layer] = names.index(group)
        pieces.append((list(bm.verts[first:]), parent))
        return len(pieces) - 1

    body = piece("body", -1, lambda: revolve(bm, uv, BODY_CENTER, BODY_PROFILE, 20, 0))
    for side, name in ((1.0, "left"), (-1.0, "right")):
        hip = Vector((side * 0.09, SEAT_Y - 0.05, 0.36))
        foot = Vector((side * 0.12, SEAT_Y - 0.75, 0.33))
        arm = piece("arm_" + name, body, lambda: tube(bm, uv, shoulder(side), hand(side), 0.045, 8, 0, 0.40))
        piece("arm_" + name, arm, lambda: ball(bm, uv, hand(side), 0.055, 0, 0.60))
        leg = piece("leg_" + name, body, lambda: tube(bm, uv, hip, foot, 0.06, 8, 0, 0.20))
        piece("leg_" + name, leg, lambda: ball(bm, uv, foot + Vector((0.0, -0.04, 0.0)), 0.07, 0, 0.02, (0.9, 1.4, 0.8)))
        piece("eye_" + name, body, lambda: ball(bm, uv, Vector((side * 0.07, SEAT_Y - 0.17, 1.11)), 0.035, 1, 0.70))

    weld = {}
    for verts, parent in pieces:
        if parent >= 0:
            # the nearest pair; on a tie the first in creation order
            pair = min(((v, t) for v in verts for t in pieces[parent][0]), key=lambda vt: (vt[0].co - vt[1].co).length)
            weld[pair[0]] = pair[1]
    bmesh.ops.weld_verts(bm, targetmap=weld)
    bm.verts.index_update()
    groups = {name: [] for name in names}
    for v in bm.verts:
        groups[names[v[layer]]].append(v.index)
    bm.verts.layers.int.remove(layer)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    obj = new_object("Driver", bm, (suit, eyes_mat), collection)
    for name in names:
        obj.vertex_groups.new(name=name).add(groups[name], 1.0, "REPLACE")
    return obj, groups


def in_groups(groups, names):
    out = set()
    for name in names:
        out.update(groups.get(name, ()))
    return out


def rotate_about(co, pivot, rot, weight):
    moved = pivot + rot @ (co - pivot)
    return co.lerp(moved, weight)


def key_steer(base, groups, side):
    # The upper body leans toward the driver's left (side = +1, +X) or right;
    # the hips stay on the seat and the hands on the steering wheel.
    rot = Matrix.Rotation(math.radians(12.0 * side), 3, "Y")
    out = list(base)
    for i in in_groups(groups, ("body", "eye_left", "eye_right")):
        out[i] = rotate_about(base[i], HIP, rot, smoothstep(0.32, 0.70, base[i].z))
    for s, name in ((1.0, "left"), (-1.0, "right")):
        sh, ha = shoulder(s), hand(s)
        length = (ha - sh).length
        for i in in_groups(groups, ("arm_" + name,)):
            t = max(0.0, min(1.0, (base[i] - sh).dot((ha - sh).normalized()) / length))
            out[i] = rotate_about(base[i], HIP, rot, 1.0 - t)
    return out


def key_win(base, groups):
    # Both arms up, the head a little back.
    out = list(base)
    for s, name in ((1.0, "left"), (-1.0, "right")):
        sh = shoulder(s)
        q = (hand(s) - sh).normalized().rotation_difference(Vector((s * 0.35, 0.05, 1.0)).normalized()).to_matrix()
        for i in in_groups(groups, ("arm_" + name,)):
            out[i] = rotate_about(base[i], sh, q, 1.0)
    back = Matrix.Rotation(math.radians(-10.0), 3, "X")
    for i in in_groups(groups, ("body", "eye_left", "eye_right")):
        out[i] = rotate_about(base[i], NECK, back, smoothstep(0.86, 0.95, base[i].z))
    return out


def key_lose(base, groups):
    # The head hangs forward and down, the shoulders slump a little.
    out = list(base)
    slump = Matrix.Rotation(math.radians(6.0), 3, "X")
    hang = Matrix.Rotation(math.radians(38.0), 3, "X")  # + moves the top toward -Y (forward)
    for i in in_groups(groups, ("body", "eye_left", "eye_right")):
        co = rotate_about(base[i], NECK, hang, smoothstep(0.86, 0.95, base[i].z))
        out[i] = rotate_about(co, HIP, slump, smoothstep(0.45, 0.85, base[i].z))
    return out


def add_shape_keys(obj, groups):
    base = [v.co.copy() for v in obj.data.vertices]
    obj.shape_key_add(name="Basis", from_mix=False)
    shapes = {
        "steer_left": key_steer(base, groups, 1.0),
        "steer_right": key_steer(base, groups, -1.0),
        "win": key_win(base, groups),
        "lose": key_lose(base, groups),
    }
    for name in SHAPE_KEYS:
        kb = obj.shape_key_add(name=name, from_mix=False)
        kb.value = 0.0
        if name in shapes:
            flat = [c for co in shapes[name] for c in co]
            kb.data.foreach_set("co", flat)
        if name in EMPTY_KEYS:
            kb.mute = True


def make_guide(collection):
    x0, y0, z0, x1, y1, z1 = CRASH_BOX
    guide = bpy.data.objects.new("CrashSize", None)
    guide.empty_display_type = "CUBE"
    guide.empty_display_size = 1.0
    # game (x, y up, z forward) -> Blender (x, -z, y)
    guide.location = ((x0 + x1) / (2.0 * UNITS), -(z0 + z1) / (2.0 * UNITS), (y0 + y1) / (2.0 * UNITS))
    guide.scale = ((x1 - x0) / (2.0 * UNITS), (z1 - z0) / (2.0 * UNITS), (y1 - y0) / (2.0 * UNITS))
    guide.hide_select = True
    collection.objects.link(guide)


GLTF_SETTINGS = {
    "export_format": "GLB",
    "export_yup": True,
    "export_apply": False,          # modifiers and shape keys do not mix
    "export_texcoords": True,
    "export_normals": True,
    "export_tangents": False,
    "export_materials": "EXPORT",
    "export_image_format": "AUTO",  # PNG stays PNG, embedded in the .glb
    "export_vertex_color": "MATERIAL",
    "export_attributes": False,
    "export_morph": True,
    "export_morph_normal": True,
    "export_morph_tangent": False,
    "export_morph_animation": False,
    "export_try_sparse_sk": False,  # plain accessors for the shape keys
    "export_skins": False,
    "export_animations": False,     # no animation sampling
    "export_force_sampling": False,
    "export_cameras": False,
    "export_lights": False,
    "export_extras": False,
}


def add_exporter(collection, filepath):
    layer = bpy.context.view_layer.layer_collection.children[collection.name]
    bpy.context.view_layer.active_layer_collection = layer
    with bpy.context.temp_override(collection=collection):
        bpy.ops.collection.exporter_add(name="IO_FH_gltf2")
    exporter = collection.exporters[0]
    props = exporter.export_properties
    for key, value in GLTF_SETTINGS.items():
        setattr(props, key, value)
    exporter.filepath = filepath
    return exporter


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.name = "DriverTemplate"
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0

    driver_col = bpy.data.collections.new("Driver")
    scene.collection.children.link(driver_col)
    guide_col = bpy.data.collections.new("Guides")
    scene.collection.children.link(guide_col)

    suit = principled("DriverSuit", (1.0, 1.0, 1.0, 1.0), 0.6, make_suit_image(), (0.93, 0.42, 0.08, 1.0))
    eyes = principled("Eyes", (0.03, 0.03, 0.04, 1.0), 0.3)
    paint = principled("KartPaint", (0.12, 0.55, 0.20, 1.0), 0.4)
    seat = principled("Seat", (0.10, 0.10, 0.12, 1.0), 0.8)
    grip = principled("SteeringWheel", (0.15, 0.15, 0.15, 1.0), 0.7)

    make_kart(driver_col, paint, seat)
    make_steering_wheel(driver_col, grip)
    driver, groups = make_driver(driver_col, suit, eyes)
    add_shape_keys(driver, groups)
    make_guide(guide_col)

    text = bpy.data.texts.new("HOW_TO_EXPORT")
    text.from_string(EXPORT_TEXT)

    add_exporter(driver_col, "//driver-template.glb")
    for obj in bpy.data.objects:
        obj.select_set(False)
    bpy.context.view_layer.objects.active = driver
    return driver_col


# ---------------------------------------------------------------- stable bytes

def stabilize(path):
    # The .blend is written uncompressed and then made the same on every run
    # of the same Blender, in place:
    #   - each new mesh gets a random face set color seed: 0
    #   - each shader node a random identifier: 1, 2, 3 ... in file order
    #     (unique in the file, so in each node tree; nothing here refers to them)
    #   - the screen edges keep their two corners in the order of their memory
    #     addresses: sorted by the pointer values in the file instead (Blender
    #     sorts them again when it reads the file)
    #   - the file browsers of the default layout hold the folder of whoever
    #     ran this (and a file name): cleared
    # Afterwards no home folder may be left in the file.
    data = bytearray(open(path, "rb").read())
    if data[:9] != b"BLENDER17":
        raise RuntimeError("unexpected .blend header")
    blocks = []
    pos = 17
    while True:
        code = bytes(data[pos:pos + 4])
        sdna, length = struct.unpack_from("<i8xq", data, pos + 4)
        blocks.append((pos + 32, code, sdna, length))
        if code == b"ENDB":
            break
        pos += 32 + length
    dna = next(b for b in blocks if b[1] == b"DNA1")
    d = bytes(data[dna[0]:dna[0] + dna[3]])

    def strings(p, count):
        out = d[p:].split(b"\0")[:count]
        return [x.decode() for x in out], (p + sum(len(x) + 1 for x in out) + 3) & ~3

    names, p = strings(12, struct.unpack_from("<i", d, 8)[0])
    count = struct.unpack_from("<i", d, p + 4)[0]
    types, p = strings(p + 8, count)
    sizes = struct.unpack_from("<%dh" % count, d, p + 4)
    p = (p + 4 + 2 * count + 3) & ~3
    layout = []
    for _ in range(struct.unpack_from("<i", d, p + 4)[0]):
        t, fields = struct.unpack_from("<hh", d, p + 8)
        p += 4
        layout.append((types[t], struct.unpack_from("<%dh" % (2 * fields), d, p + 8)))
        p += 4 * fields

    def offset(struct_name, field):
        index = next(i for i, (n, _) in enumerate(layout) if n == struct_name)
        fields = layout[index][1]
        at = 0
        for k in range(0, len(fields), 2):
            ftype, fname = types[fields[k]], names[fields[k + 1]]
            if fname == field:
                return index, at
            count = 1
            for n in re.findall(r"\[(\d+)\]", fname):
                count *= int(n)
            at += count * (8 if fname.startswith(("*", "(*")) else sizes[types.index(ftype)])
        raise RuntimeError("no field " + field)

    mesh, seed_at = offset("Mesh", "face_sets_color_seed")
    node, ident_at = offset("bNode", "identifier")
    edge, v1_at = offset("ScrEdge", "*v1")
    _, v2_at = offset("ScrEdge", "*v2")
    params, dir_at = offset("FileSelectParams", "dir[1282]")
    _, file_at = offset("FileSelectParams", "file[256]")
    _, rename_at = offset("FileSelectParams", "renamefile[256]")
    next_id = 1
    for start, code, sdna, length in blocks:
        if sdna == mesh and code == b"ME\0\0":
            struct.pack_into("<i", data, start + seed_at, 0)
        elif sdna == node:
            struct.pack_into("<i", data, start + ident_at, next_id)
            next_id += 1
        elif sdna == edge:
            v1, v2 = struct.unpack_from("<QQ", data, start + v1_at)
            if v2_at == v1_at + 8 and v1 > v2:
                struct.pack_into("<QQ", data, start + v1_at, v2, v1)
        elif sdna == params:
            data[start + dir_at:start + dir_at + 1282] = bytes(1282)
            data[start + file_at:start + file_at + 256] = bytes(256)
            data[start + rename_at:start + rename_at + 256] = bytes(256)
    found = re.search(rb"[A-Za-z]:[\\/]Users[\\/]|/Users/|/home/|AppData", data)
    if found:
        raise RuntimeError("a local path is left in the file at byte %d" % found.start())
    with open(path, "wb") as f:
        f.write(data)


def main():
    args = args_after_dashes()
    here = os.path.dirname(os.path.abspath(__file__))
    blend = os.path.join(here, "driver-template.blend")
    glb = None
    i = 0
    while i < len(args):
        if args[i] == "--blend" and i + 1 < len(args):
            blend = os.path.abspath(args[i + 1])
            i += 2
        elif args[i] == "--glb" and i + 1 < len(args):
            glb = os.path.abspath(args[i + 1])
            i += 2
        else:
            print("unknown argument: " + args[i])
            sys.exit(2)

    build()
    # Saved as a copy (the session keeps no file name), uncompressed, so that
    # stabilize() can make the bytes the same on every run.
    os.makedirs(os.path.dirname(blend), exist_ok=True)
    if os.path.exists(blend):
        os.remove(blend)  # else Blender keeps the old one as .blend1
    bpy.ops.wm.save_as_mainfile(filepath=blend, compress=False, copy=True)
    stabilize(blend)
    print("written " + blend)

    if glb is not None:
        driver_col = bpy.data.collections["Driver"]
        driver_col.exporters[0].filepath = glb
        with bpy.context.temp_override(collection=driver_col):
            bpy.ops.collection.exporter_export(index=0)
        print("exported " + glb)


main()
