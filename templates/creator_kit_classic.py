# Creator Kit: an old (classic) character as a starting point in the template. CC0 (templates/LICENSE);
# made only from code, no game data.
#
# The model comes as a GLB from `rldpack char-export <file.rldchar> -o <file.glb>` (Reload Studio runs
# it): the classic model with its vertex colours, or the native model with its textures, in the pose
# chosen there (default: the neutral one), in glTF axes and units (+Y up, +Z the kart's front, 1 m = 64
# game units, the origin on the ground under the kart) - the same as the kit, so the import needs no
# scaling and no turning.
#
#   import_classic(path, vcol) -> (collection, objects)
#
# The model goes into the collection "Classic" at the top of the scene, outside "CreatorKit": neither the
# figure export nor the local demo export ever takes it in. The collection is hidden in the viewport (its
# eye in the Outliner shows it). Objects and meshes are named classic_<name>, the materials
# classic_<name> [vcol <mode>], so nothing of it can be mistaken for a part of the puppet or of the kit.

import bpy

COLLECTION = "Classic"
PREFIX = "classic_"


def _import(path):
    before = set(bpy.data.objects)
    props = bpy.ops.import_scene.gltf.get_rna_type().properties
    kw = {}
    for k, v in (("import_pack_images", True), ("merge_vertices", False), ("import_select_created_objects", False),
                 ("import_scene_as_collection", False), ("import_shading", "SMOOTH")):
        if k in props:
            kw[k] = v
    res = bpy.ops.import_scene.gltf(filepath=path, **kw)
    if "FINISHED" not in res:
        raise RuntimeError("the glTF importer could not read the classic model")
    return sorted(set(bpy.data.objects) - before, key=lambda o: o.name)


def import_classic(path, vcol):
    """Imports the char-export GLB at path into the collection Classic; returns (collection, mesh objects)."""
    scene = bpy.context.scene
    col = bpy.data.collections.get(COLLECTION)
    if col is None:
        col = bpy.data.collections.new(COLLECTION)
        scene.collection.children.link(col)
    new = _import(path)
    # keep the list BEFORE removing anything: a removed object cannot be read
    empties = [o for o in new if o.type != "MESH"]
    meshes = [o for o in new if o.type == "MESH"]
    for o in empties:
        for c in list(o.children):
            mw = c.matrix_world.copy()
            c.parent = None
            c.matrix_world = mw
    for o in empties:
        bpy.data.objects.remove(o)
    if not meshes:
        raise RuntimeError("the classic model holds no mesh")
    tag = " [vcol %s]" % vcol
    for o in meshes:
        for c in list(o.users_collection):
            c.objects.unlink(o)
        col.objects.link(o)
        if not o.name.startswith(PREFIX):
            o.name = PREFIX + o.name
        o.data.name = o.name
        for m in o.data.materials:
            if m is None:
                continue
            name = m.name
            if not name.startswith(PREFIX):
                name = PREFIX + name
            if not name.endswith(tag):
                name += tag
            m.name = name
        attrs = o.data.color_attributes
        if len(attrs):
            attrs.active_color = attrs[0]
            attrs.render_color_index = attrs.active_color_index
    layer = _layer_of(bpy.context.view_layer.layer_collection, col)
    if layer is not None:
        layer.hide_viewport = True
    return col, meshes


def _layer_of(layer, col):
    if layer.collection == col:
        return layer
    for child in layer.children:
        hit = _layer_of(child, col)
        if hit is not None:
            return hit
    return None
