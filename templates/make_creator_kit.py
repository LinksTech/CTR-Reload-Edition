# Creator Kit generator: builds creator-kit.blend (the template) and its two
# exports from the kit files: creator-kit-figure.glb (the figure alone, no
# game data - the file to share) and creator-kit-demo-local.glb (the figure
# with the kit kart and wheels, for a test on the user's own PC; never to
# share: every kit part in it is marked). Everything the
# template shows is made by code in this folder (templates/creator_kit_*.py)
# or comes from the kit files you give it - this script holds no game data. It
# is released under CC0 (templates/LICENSE).
#
#   blender --threads 1 --background --factory-startup --python-exit-code 1 \
#       --python templates/make_creator_kit.py -- --kit <folder> [options]
#
# <folder> must hold kit-info.json, kit-kart.glb and kit-wheel.glb (and
# kit-kart-gltf.glb and kit-wheel-gltf.glb for --vcol gltf). Without game data use --stand-in: it
# writes made-up kit files into <folder> first and then goes the same way.
#
# Options:
#   --kit <folder>            kit folder, also the output folder (required)
#   --stand-in                write the code-made stand-in kit files first
#   --vcol psx|gltf|off       vertex colour mode of the file (default psx)
#   --no-blend                do not write creator-kit.blend
#   --no-glb                  do not write the two .glb files
#   --classic <glb>           an old character as a starting point: the GLB that
#                             rldpack char-export wrote from its .rldchar goes into the
#                             collection Classic (never exported)
#   --test-fake-version X.Y.Z test switch: pretend this Blender version
#
# Output in <folder>: creator-kit.blend, creator-kit-figure.glb,
# creator-kit-demo-local.glb, creator-kit-report.json. Exit codes: 0 good, 2 wrong call, 3 wrong Blender
# version, 4 input missing or invalid, 5 the self check failed or an internal
# error. Needs Blender 5.2; same Blender and same input give the same bytes
# (the .blend only with --threads 1, see THREADS_NOTE).
#
# Units and axes. Kit files use model units "u" (64 u = 1 m) with glTF axes:
# x = kart left, y = up, z = kart front. Blender: X = x / 64, Y = -z / 64,
# Z = y / 64 (the nose toward -Y).

import hashlib
import json
import math
import os
import re
import struct
import sys
import time
import traceback

sys.dont_write_bytecode = True

# The collection exporter stored in the file. The guide and the tests read this.
GLTF_SETTINGS = {
    # on
    "export_format": "GLB",
    "export_yup": True,
    "export_texcoords": True,
    "export_normals": True,
    "export_materials": "EXPORT",
    "export_image_format": "AUTO",      # PNG stays PNG
    "export_skins": True,
    "export_def_bones": True,
    "export_influence_nb": 4,
    "export_animations": True,
    "export_animation_mode": "ACTIONS",
    "export_force_sampling": True,      # every frame; the author's curves are sampled linear
    "export_frame_step": 1,
    "export_anim_single_armature": True,
    "export_reset_pose_bones": True,
    "export_rest_position_armature": True,   # bind pose = the seated rest pose
    "export_vertex_color": "ACTIVE",
    "export_extras": True,              # custom properties -> extras: the kit marker "ctr_kit"
    # off
    "export_apply": False,              # never apply the armature modifier
    "export_tangents": False,
    "export_morph": False,
    "export_try_sparse_sk": False,
    "export_all_influences": False,
    "export_leaf_bone": False,
    "export_optimize_animation_size": False,
    "export_cameras": False,
    "export_lights": False,
    "export_all_vertex_colors": False,
    "export_active_vertex_color_when_no_material": False,
    "export_draco_mesh_compression_enable": False,
    "export_meshopt_compression_enable": False,
    "export_gpu_instances": False,
    "export_unused_images": False,
    "export_unused_textures": False,
    "export_current_frame": False,
    "export_frame_range": False,
    "export_bake_animation": False,
    "export_extra_animations": False,
    "export_hierarchy_flatten_bones": False,
}

# glTF importer shading for the kit files. SMOOTH keeps every vertex of the file as one vertex;
# NORMALS (custom normals) makes Blender's exporter split a few vertices again (+4 on the stand-in kart).
IMPORT_SHADING = "SMOOTH"

CRASH_BOX = (-33.8, 5.6, -54.1, 34.3, 88.3, 58.3)   # u, the size a driver may fill
WHEEL_NAMES = ("wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr")
# Two exporters: the figure alone (no game data, the file to share) on the collection Figure, and the
# local demo with the kit kart and wheels (never to share) on the collection CreatorKit.
FIGURE_GLB = "creator-kit-figure.glb"
LOCAL_GLB = "creator-kit-demo-local.glb"
# The kit marker: every node and mesh made from the kit folder gets the custom property ctr_kit (glTF
# extras), every kit material the name prefix ctrkit_, so a check can find game data in a file.
KIT_PROP = "ctr_kit"
# Byte-identical .blend files need Blender started with --threads 1. With more threads the .blend writer
# gives some temporary blocks (the attribute arrays of a mesh) file addresses that depend on which memory
# the allocator hands out while other threads allocate - under CPU load about 2 runs in 100 wrote other
# addresses
# (same data, other bytes). The GLB exports are byte-identical either way.
THREADS_NOTE = ("Blender ran with more than one thread: creator-kit.blend has the same content, but its bytes "
                "may differ between runs. Start Blender with --threads 1 for byte-identical files.")
KIT_MAT_PREFIX = "ctrkit_"
CLASSIC_PREFIX = "classic_"   # the names of an imported old character (creator_kit_classic.py)
BLEND_NAME = "creator-kit.blend"
REPORT_NAME = "creator-kit-report.json"

USAGE = ("usage: make_creator_kit.py -- --kit <folder> [--stand-in] [--vcol psx|gltf|off] "
         "[--no-blend] [--no-glb] [--classic <glb>] [--test-fake-version X.Y.Z]")


def fail(code, text):
    print(text)
    sys.exit(code)


def Bv(p):
    from mathutils import Vector
    return Vector((p[0] / 64.0, -p[2] / 64.0, p[1] / 64.0))


# ---------------------------------------------------------------- arguments

def parse_args(argv):
    if "--" not in argv:
        fail(2, "creator kit: no arguments after '--'. " + USAGE)
    args = argv[argv.index("--") + 1:]
    opt = {"kit": None, "stand_in": False, "vcol": "psx", "blend": True, "glb": True,
           "classic": None, "fake": None}
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--kit", "--vcol", "--classic", "--test-fake-version"):
            if i + 1 >= len(args):
                fail(2, "creator kit: %s needs a value. %s" % (a, USAGE))
            v = args[i + 1]
            i += 2
            if a == "--kit":
                opt["kit"] = v
            elif a == "--vcol":
                if v not in ("psx", "gltf", "off"):
                    fail(2, "creator kit: --vcol must be psx, gltf or off. " + USAGE)
                opt["vcol"] = v
            elif a == "--classic":
                opt["classic"] = v
            else:
                if not re.fullmatch(r"\d+\.\d+\.\d+", v):
                    fail(2, "creator kit: --test-fake-version needs X.Y.Z. " + USAGE)
                opt["fake"] = tuple(int(x) for x in v.split("."))
        elif a == "--stand-in":
            opt["stand_in"] = True
            i += 1
        elif a == "--no-blend":
            opt["blend"] = False
            i += 1
        elif a == "--no-glb":
            opt["glb"] = False
            i += 1
        else:
            fail(2, "creator kit: unknown argument %s. %s" % (a, USAGE))
    if not opt["kit"]:
        fail(2, "creator kit: --kit <folder> is required. " + USAGE)
    return opt


# ---------------------------------------------------------------- kit input

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def num_list(v, n):
    return (isinstance(v, list) and len(v) == n and
            all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v))


def read_kit_info(out, vcol):
    path = os.path.join(out, "kit-info.json")
    if not os.path.isfile(path):
        fail(4, "creator kit: kit-info.json is missing in %s. How to fix: generate the kit files again "
                "(or use --stand-in)." % out)
    try:
        with open(path, "r", encoding="utf-8") as f:
            info = json.load(f)
    except (OSError, ValueError) as e:
        fail(4, "creator kit: kit-info.json cannot be read (%s). How to fix: generate the kit files again." % e)

    def bad(key):
        fail(4, "creator kit: kit-info.json: '%s' is missing or invalid. "
                "How to fix: generate the kit files again." % key)

    if not isinstance(info, dict):
        bad("(top level)")
    if info.get("kit") != 1:
        bad("kit")
    if not num_list(info.get("seat"), 3):
        bad("seat")
    st = info.get("steer")
    if not isinstance(st, dict):
        bad("steer")
    for k in ("center", "axis", "normal"):
        if not num_list(st.get(k), 3):
            bad("steer." + k)
    if not isinstance(st.get("radius"), (int, float)):
        bad("steer.radius")
    if not num_list(info.get("kart_box"), 6):
        bad("kart_box")
    fl = info.get("floor")
    if not (isinstance(fl, list) and fl and all(num_list(x, 2) for x in fl)):
        bad("floor")
    wh = info.get("wheels")
    if not isinstance(wh, dict):
        bad("wheels")
    for n in WHEEL_NAMES:
        if not num_list(wh.get(n), 3):
            bad("wheels." + n)
    if not isinstance(info.get("wheel_radius"), (int, float)):
        bad("wheel_radius")
    files = info.get("files")
    if not isinstance(files, dict):
        bad("files")
    # The kart and the wheel by the mode: a file whose "vcol" is the mode (psx also for off); a wheel
    # file without "vcol" has no vertex colours and fits every mode.
    want = "gltf" if vcol == "gltf" else "psx"

    def pick(prefix):
        for name in sorted(files):
            ent = files[name]
            if name.startswith(prefix) and isinstance(ent, dict) and ent.get("vcol") == want:
                return name
        for name in sorted(files):
            ent = files[name]
            if name.startswith(prefix) and isinstance(ent, dict) and "vcol" not in ent and prefix == "kit-wheel":
                return name
        return None

    kart = pick("kit-kart")
    wheel = pick("kit-wheel")
    for part, name in (("kart", kart), ("wheel", wheel)):
        if name is None:
            if vcol == "gltf":
                fail(4, "creator kit: This kit has no %s for the vertex colour mode glTF. "
                        "Generate the kit files again." % part)
            bad("files (a %s with vcol psx)" % part)
    for name in (kart, wheel):
        p = os.path.join(out, name)
        if not os.path.isfile(p):
            fail(4, "creator kit: %s is missing in %s. How to fix: generate the kit files again." % (name, out))
        want_sha = files[name].get("sha256")
        if not isinstance(want_sha, str):
            bad("files.%s.sha256" % name)
        if sha256_file(p).lower() != want_sha.lower():
            fail(4, "creator kit: %s does not match the sha256 in kit-info.json. "
                    "How to fix: generate the kit files again." % name)
    return info, kart, wheel


# ---------------------------------------------------------------- scene helpers

def import_glb(path):
    """Import one GLB; returns the new objects (sorted by name)."""
    import bpy
    before = set(bpy.data.objects)
    props = bpy.ops.import_scene.gltf.get_rna_type().properties
    wanted = {"import_pack_images": True, "merge_vertices": False,
              "import_select_created_objects": False, "import_scene_as_collection": False,
              "import_shading": IMPORT_SHADING}
    kw = {}
    for k, v in wanted.items():
        if k in props:
            kw[k] = v
        else:
            print("creator kit: glTF importer option missing: " + k +
                  " (options: %s)" % ", ".join(sorted(props.keys())))
            sys.exit(5)
    res = bpy.ops.import_scene.gltf(filepath=path, **kw)
    if "FINISHED" not in res:
        fail(4, "creator kit: the glTF importer could not read %s." % os.path.basename(path))
    return sorted(set(bpy.data.objects) - before, key=lambda o: o.name)


def relink(obj, collection):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    collection.objects.link(obj)


def drop_import_collections(keep):
    import bpy
    for c in list(bpy.data.collections):
        if c.name not in keep and not c.objects and not c.children and c.users == 0:
            bpy.data.collections.remove(c)


def bone_parent(obj, rig, bone):
    import bpy
    mw = obj.matrix_world.copy()
    obj.parent = rig
    obj.parent_type = "BONE"
    obj.parent_bone = bone
    bpy.context.view_layer.update()
    obj.matrix_world = mw


def suffix_materials(mesh, vcol, kit=False):
    tag = " [vcol %s]" % vcol
    for m in mesh.materials:
        if m is None:
            continue
        name = m.name
        if kit and not name.startswith(KIT_MAT_PREFIX):
            name = KIT_MAT_PREFIX + name
        if not name.endswith(tag):
            name = name + tag
        m.name = name


def mark_kit(obj, kind):
    """The kit marker on the object (glTF node extras) and its mesh (mesh extras)."""
    obj[KIT_PROP] = kind
    obj.data[KIT_PROP] = kind


def tri_count(mesh):
    return sum(len(p.vertices) - 2 for p in mesh.polygons)


def make_empty(name, collection, kind, size, location, rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0)):
    import bpy
    e = bpy.data.objects.new(name, None)
    e.empty_display_type = kind
    e.empty_display_size = size
    e.location = location
    e.rotation_euler = rotation
    e.scale = scale
    collection.objects.link(e)
    return e


def import_kart(kart_path, collection, rig, vcol):
    import bpy
    new = import_glb(kart_path)
    # an empty root node (an earlier kit-extract wrote one, "kart"): re-parent its children, then remove it.
    # The list of the objects to keep is made BEFORE the removal: a removed object cannot be read at all.
    empties = [o for o in new if o.type == "EMPTY"]
    new = [o for o in new if o.type != "EMPTY"]
    for o in empties:
        for c in list(o.children):
            mw = c.matrix_world.copy()
            c.parent = None
            c.matrix_world = mw
    for o in empties:
        bpy.data.objects.remove(o)
    names = [o.name for o in new]
    for o in new:
        if o.type != "MESH" or not (o.name.startswith("kart_") or o.name == "kart_steer_wheel"):
            fail(4, "creator kit: %s holds the node '%s', which is not a kart part. The kart file must "
                    "hold only nodes named kart_* and kart_steer_wheel." % (os.path.basename(kart_path), o.name))
    if names.count("kart_steer_wheel") != 1:
        fail(4, "creator kit: %s must hold exactly one node 'kart_steer_wheel'." % os.path.basename(kart_path))
    if not any(n.startswith("kart_") and n != "kart_steer_wheel" for n in names):
        fail(4, "creator kit: %s holds no kart body (a node named kart_*)." % os.path.basename(kart_path))
    for o in new:
        o.parent = None
        relink(o, collection)
        o.data.name = o.name
        suffix_materials(o.data, vcol, kit=True)
        mark_kit(o, "steer_wheel" if o.name == "kart_steer_wheel" else "kart")
        attrs = o.data.color_attributes
        if vcol == "off":
            for a in list(attrs):
                attrs.remove(a)
        elif len(attrs):
            attrs.active_color = attrs[0]
            attrs.render_color_index = attrs.active_color_index
    bpy.context.view_layer.update()
    for o in sorted(new, key=lambda o: o.name):
        bone_parent(o, rig, "kart_steer" if o.name == "kart_steer_wheel" else "kart")
    return new


def import_wheels(path, info, collection, rig, vcol):
    import bpy
    from mathutils import Matrix
    new = import_glb(path)
    meshes = [o for o in new if o.type == "MESH"]
    if len(meshes) != 1:
        fail(4, "creator kit: kit-wheel.glb must hold exactly one mesh node (it holds %d)." % len(meshes))
    src = meshes[0]
    ident = Matrix.Identity(4)
    if any(abs(a - b) > 1e-5 for ra, rb in zip(src.matrix_world, ident) for a, b in zip(ra, rb)):
        fail(4, "creator kit: the wheel node in kit-wheel.glb must have an identity transform.")
    mesh = src.data
    mesh.name = "wheel"
    suffix_materials(mesh, vcol, kit=True)
    if vcol == "off":
        for a in list(mesh.color_attributes):
            mesh.color_attributes.remove(a)
    for o in new:
        bpy.data.objects.remove(o)
    objs = []
    for name in WHEEL_NAMES:
        p = info["wheels"][name]
        o = bpy.data.objects.new(name, mesh)
        mark_kit(o, "wheel")
        o.location = Bv(p)
        if p[0] < 0:
            o.rotation_euler = (0.0, 0.0, math.pi)     # right side: turned, not mirrored
        collection.objects.link(o)
        objs.append(o)
    bpy.context.view_layer.update()
    for o in objs:
        bone_parent(o, rig, "kart")
    return objs


HOW_TO_EXPORT = [
    "Creator Kit - how to export",
    "",
    "Figure: your character and creator_rig. The grey puppet is only an example; replace it and bind it to creator_rig.",
    "Kart: the kart parts and the four wheels from YOUR game data. They are fixed to the bones kart and kart_steer.",
    "Guides: size box, seat point, stand spot and front arrow. They are never exported.",
    "Keep the rest pose of creator_rig as it is: the figure sits in the kart.",
    "There are {count} actions: {clips}.",
    "finish_win and finish_lose are optional (seated, after the finish line). Without them the game plays its other clips there.",
    "Every action has a fake user, so none is lost. Do not rename them.",
    "Standing actions (podium_1, podium_2, podium_3, lose, menu) have a marker: hide the collection Kart for them.",
    "",
    "Two exporters, both under Collection properties > Exporters > glTF 2.0 > Export:",
    "- on the collection Figure: creator-kit-figure.glb, the figure alone. It holds no game data.",
    "- on the collection CreatorKit: creator-kit-demo-local.glb, the figure WITH the kart and wheels of",
    "  your game data, for a test on your own computer. Never share it.",
    "File > Export All Collections writes both, next to this .blend.",
    "Kit parts are marked (custom property ctr_kit, materials named ctrkit_...); keep them in the collection Kart.",
    "Do not change the export settings of the exporters.",
    "Do not apply the Armature modifier and do not use scaled or mirrored objects.",
    "Keep the model under 64000 triangles.",
    "The full guide is docs/CREATOR_KIT.md (in Reload Studio: page Creator Kit, button Guide).",
]


def add_exporter(collection, filepath):
    import bpy
    def find(layer):
        if layer.collection == collection:
            return layer
        for child in layer.children:
            hit = find(child)
            if hit is not None:
                return hit
        return None

    bpy.context.view_layer.active_layer_collection = find(bpy.context.view_layer.layer_collection)
    with bpy.context.temp_override(collection=collection):
        bpy.ops.collection.exporter_add(name="IO_FH_gltf2")
    exporter = collection.exporters[0]
    props = exporter.export_properties
    for key in GLTF_SETTINGS:
        if not hasattr(props, key):
            print("creator kit: the glTF exporter has no setting '%s' (this Blender is not as tested)." % key)
            sys.exit(5)
    for key, value in GLTF_SETTINGS.items():
        setattr(props, key, value)
    exporter.filepath = filepath
    return exporter


# ---------------------------------------------------------------- stable bytes

def stabilize(path, out):
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
    # Afterwards neither a home folder nor the output folder may be left in the file.
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
        out_ = d[p:].split(b"\0")[:count]
        return [x.decode() for x in out_], (p + sum(len(x) + 1 for x in out_) + 3) & ~3

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
    lower = bytes(data).lower()
    needles = set()
    for folder in (out, os.path.expanduser("~")):
        if not folder:
            continue
        for form in (folder, folder.replace("\\", "/"), folder.replace("/", "\\")):
            needles.add(form.rstrip("\\/").lower().encode("utf-8"))
    for needle in sorted(needles):
        if len(needle) > 3 and needle in lower:
            raise RuntimeError("the output folder or the home folder is left in the file at byte %d"
                               % lower.index(needle))
    with open(path, "wb") as f:
        f.write(data)


# ---------------------------------------------------------------- GLB self check

def read_glb_json(path):
    with open(path, "rb") as f:
        head = f.read(20)
        if head[:4] != b"glTF":
            raise RuntimeError("not a GLB")
        n = struct.unpack_from("<I", head, 12)[0]
        return json.loads(f.read(n).decode("utf-8"))


def kit_marks(j):
    """Everything in the GLB that carries the kit marker: node and mesh extras ctr_kit, material prefix."""
    out = []
    for kind in ("nodes", "meshes"):
        for x in j.get(kind, []):
            if KIT_PROP in (x.get("extras") or {}):
                out.append("%s %s" % (kind[:-1], x.get("name", "?")))
    for m in j.get("materials", []):
        if m.get("name", "").startswith(KIT_MAT_PREFIX):
            out.append("material " + m.get("name", ""))
    return out


def check_glb(path, clip_names, vcol, local):
    """local: the demo with the kit kart (wheels, kart nodes, every kit part marked); else the figure alone
    (no wheel node, no kart node, no kit marker)."""
    j = read_glb_json(path)
    what = os.path.basename(path)
    anims = sorted(a.get("name", "") for a in j.get("animations", []))
    if anims != sorted(clip_names):
        fail(5, "creator kit: self check failed: %s holds the clips %s, expected %s."
                % (what, anims, sorted(clip_names)))
    nodes = j.get("nodes", [])
    names = [n.get("name") for n in nodes]
    # kit parts: wheel nodes and kart_* nodes WITH a mesh (the bones kart and kart_steer are joints, no mesh;
    # the figure file keeps them, they hold no game data)
    kit_names = [n.get("name") for n in nodes
                 if "mesh" in n and (n.get("name") in WHEEL_NAMES or (n.get("name") or "").startswith("kart_"))]
    if local:
        for w in WHEEL_NAMES:
            if names.count(w) != 1:
                fail(5, "creator kit: self check failed: %s has %d nodes named %s (expected 1)."
                        % (what, names.count(w), w))
        unmarked = []
        for n in nodes:
            if n.get("name") not in kit_names or "mesh" not in n:
                continue
            if KIT_PROP not in (n.get("extras") or {}):
                unmarked.append("node " + n.get("name"))
            if KIT_PROP not in (j["meshes"][n["mesh"]].get("extras") or {}):
                unmarked.append("mesh of " + n.get("name"))
        kit_mats = set()
        for n in nodes:
            if n.get("name") in kit_names and "mesh" in n:
                for prim in j["meshes"][n["mesh"]].get("primitives", []):
                    if "material" in prim:
                        kit_mats.add(j["materials"][prim["material"]].get("name", ""))
        unmarked += ["material " + m for m in sorted(kit_mats) if not m.startswith(KIT_MAT_PREFIX)]
        if unmarked:
            fail(5, "creator kit: self check failed: kit parts without the marker in %s: %s" % (what, unmarked))
    else:
        marks = kit_marks(j)
        if kit_names or marks:
            fail(5, "creator kit: self check failed: %s must hold the figure alone, but holds %s."
                    % (what, sorted(set(kit_names)) + marks))
    classic = [n for n in names if (n or "").startswith(CLASSIC_PREFIX)]
    if classic:
        fail(5, "creator kit: self check failed: %s holds the classic model (%s) - it must stay out of every "
                "export." % (what, ", ".join(sorted(classic))))
    if len(j.get("skins", [])) != 1:
        fail(5, "creator kit: self check failed: %s has %d skins (expected 1)." % (what, len(j.get("skins", []))))
    tag = " [vcol %s]" % vcol
    bad = [m.get("name", "") for m in j.get("materials", []) if not m.get("name", "").endswith(tag)]
    if bad or not j.get("materials"):
        fail(5, "creator kit: self check failed: materials without the ending '%s' in %s: %s" % (tag, what, bad))
    return j


# ---------------------------------------------------------------- main

def build(opt, started):
    import bpy

    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    import creator_kit_puppet
    import creator_kit_standin
    import creator_kit_clips

    out = os.path.abspath(opt["kit"])
    vcol = opt["vcol"]
    os.makedirs(out, exist_ok=True)
    if opt["stand_in"]:
        creator_kit_standin.write_standin(out)
    info, kart_file, wheel_file = read_kit_info(out, vcol)
    classic_path = None
    if opt["classic"] is not None:
        classic_path = os.path.abspath(opt["classic"])
        try:
            with open(classic_path, "rb") as f:
                head = f.read(4)
        except OSError:
            fail(4, "creator kit: the classic model %s cannot be read. How to fix: export the old character "
                    "again (rldpack char-export <file.rldchar> -o <file.glb>)." % classic_path)
        if head != b"glTF":
            fail(4, "creator kit: the classic model %s is no glTF binary (.glb). How to fix: export the old "
                    "character with rldpack char-export <file.rldchar> -o <file.glb>." % classic_path)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.name = "CreatorKit"
    scene.render.fps = 30
    scene.render.fps_base = 1.0
    scene.frame_start = 0
    scene.frame_end = 60
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0

    kit_col = bpy.data.collections.new("CreatorKit")
    scene.collection.children.link(kit_col)
    figure_col = bpy.data.collections.new("Figure")
    kit_col.children.link(figure_col)
    kart_col = bpy.data.collections.new("Kart")
    kit_col.children.link(kart_col)
    guide_col = bpy.data.collections.new("Guides")
    scene.collection.children.link(guide_col)

    bones = creator_kit_puppet.bone_table(info)
    rig = creator_kit_puppet.build_rig(figure_col, bones)
    puppet = creator_kit_puppet.build_puppet(figure_col, rig, bones, vcol)

    kart_objs = import_kart(os.path.join(out, kart_file), kart_col, rig, vcol)
    wheel_objs = import_wheels(os.path.join(out, wheel_file), info, kart_col, rig, vcol)
    drop_import_collections({"CreatorKit", "Figure", "Kart", "Guides", "Classic"})

    creator_kit_clips.build_actions(rig, bones, info)

    classic_objs = []
    if classic_path is not None:
        import creator_kit_classic
        try:
            _col, classic_objs = creator_kit_classic.import_classic(classic_path, vcol)
        except RuntimeError as e:
            fail(4, "creator kit: the classic model %s cannot be used: %s." % (os.path.basename(classic_path), e))
    clip_names = list(creator_kit_clips.CLIP_NAMES)

    # guides (not exported)
    box = CRASH_BOX
    centre = ((box[0] + box[3]) / 2, (box[1] + box[4]) / 2, (box[2] + box[5]) / 2)
    half = ((box[3] - box[0]) / 128.0, (box[5] - box[2]) / 128.0, (box[4] - box[1]) / 128.0)
    make_empty("CrashSize", guide_col, "CUBE", 1.0, Bv(centre), scale=half)
    make_empty("SeatPoint", guide_col, "SPHERE", 0.03, Bv(info["seat"]))
    make_empty("StandSpot", guide_col, "CIRCLE", 0.15, (0.0, 0.0, 0.0), rotation=(math.pi / 2, 0.0, 0.0))
    nose = info["kart_box"][5]
    make_empty("Front", guide_col, "SINGLE_ARROW", 0.3, Bv((0.0, 0.0, nose)), rotation=(math.pi / 2, 0.0, 0.0))

    text = bpy.data.texts.new("HOW_TO_EXPORT")
    text.from_string("\n".join(HOW_TO_EXPORT).replace("{count}", str(len(clip_names)))
                     .replace("{clips}", ", ".join(clip_names)) + "\n")

    figure_exporter = add_exporter(figure_col, "//" + FIGURE_GLB)
    local_exporter = add_exporter(kit_col, "//" + LOCAL_GLB)

    for obj in bpy.data.objects:
        obj.select_set(False)
    bpy.context.view_layer.objects.active = rig
    if "idle" in bpy.data.actions:
        if rig.animation_data is None:
            rig.animation_data_create()
        if rig.animation_data.action != bpy.data.actions["idle"]:
            rig.animation_data.action = bpy.data.actions["idle"]
    scene.frame_set(0)

    blend_path = os.path.join(out, BLEND_NAME)
    figure_path = os.path.join(out, FIGURE_GLB)
    local_path = os.path.join(out, LOCAL_GLB)

    if opt["blend"]:
        for old in (blend_path, blend_path + "1"):
            if os.path.exists(old):
                os.remove(old)
        bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=False, copy=True)
        stabilize(blend_path, out)

    figure_json = local_json = None
    if opt["glb"]:
        for exporter, collection, path in ((figure_exporter, figure_col, figure_path),
                                           (local_exporter, kit_col, local_path)):
            if os.path.exists(path):
                os.remove(path)
            exporter.filepath = path
            with bpy.context.temp_override(collection=collection):
                bpy.ops.collection.exporter_export(index=0)
            if not os.path.isfile(path):
                fail(5, "creator kit: self check failed: the exporter wrote no %s." % os.path.basename(path))
        figure_json = check_glb(figure_path, clip_names, vcol, local=False)
        local_json = check_glb(local_path, clip_names, vcol, local=True)

    puppet_tris = tri_count(puppet.data)
    kart_tris = sum(tri_count(o.data) for o in kart_objs)
    wheel_tris = sum(tri_count(o.data) for o in wheel_objs)
    report = {
        "blender": bpy.app.version_string,
        "vcol": vcol,
        "source": info.get("source"),
        "triangles": {"puppet": puppet_tris, "kart": kart_tris, "wheels": wheel_tris,
                      "total": puppet_tris + kart_tris + wheel_tris},
        "skin_joints": {FIGURE_GLB: len(figure_json["skins"][0]["joints"]) if figure_json else None,
                        LOCAL_GLB: len(local_json["skins"][0]["joints"]) if local_json else None},
        "clips": sorted(a.get("name", "") for a in figure_json.get("animations", [])) if figure_json else None,
        "sha256": {BLEND_NAME: sha256_file(blend_path) if opt["blend"] else None,
                   FIGURE_GLB: sha256_file(figure_path) if opt["glb"] else None,
                   LOCAL_GLB: sha256_file(local_path) if opt["glb"] else None},
        "seconds": round(time.time() - started, 1),
        "messages": [] if single_thread(sys.argv) else [THREADS_NOTE],
        "classic": ({"file": os.path.basename(classic_path), "objects": len(classic_objs),
                     "triangles": sum(tri_count(o.data) for o in classic_objs)} if classic_path else None),
    }
    with open(os.path.join(out, REPORT_NAME), "w", encoding="utf-8", newline="\n") as f:
        json.dump(report, f, indent=1, sort_keys=True)
        f.write("\n")
    print("creator kit: ok vcol=%s source=%s triangles=%d joints=%s clips=%s blend=%s glb=%s %.1fs"
          % (vcol, report["source"], report["triangles"]["total"],
             "/".join(str(report["skin_joints"][k]) for k in (FIGURE_GLB, LOCAL_GLB)),
             len(report["clips"]) if report["clips"] is not None else "-",
             "yes" if opt["blend"] else "no", "yes" if opt["glb"] else "no", report["seconds"]))


def single_thread(argv):
    """True when Blender was started with --threads 1 (or -t 1)."""
    before = argv[:argv.index("--")] if "--" in argv else argv
    for i, a in enumerate(before[:-1]):
        if a in ("--threads", "-t") and before[i + 1] == "1":
            return True
    return False


def main():
    started = time.time()
    opt = parse_args(sys.argv)
    import bpy
    version = opt["fake"] if opt["fake"] else tuple(bpy.app.version)
    if tuple(version[:2]) != (5, 2):
        fail(3, "Creator Kit needs Blender 5.2 (this is %s). How to fix: install Blender 5.2 from blender.org."
             % ".".join(str(x) for x in version))
    build(opt, started)


try:
    main()
except SystemExit:
    raise
except BaseException as e:  # noqa: B902
    print("creator kit: internal error: %s" % e)
    traceback.print_exc()
    sys.exit(5)
