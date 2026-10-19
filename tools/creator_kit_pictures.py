# Creator Kit guide pictures: draws the 9 SVG pictures of docs/CREATOR_KIT.md.
# Part of CTR Reload's tools (GPL, like the rest of tools/).
#
# The pictures are made from the code-made STAND-IN kit only (a made-up kart,
# four wheels and the grey puppet, all built by templates/creator_kit_*.py).
# No game data is read, ever: there is no way to point this script at a kit
# folder, and --kit (or any other unknown argument) is refused with exit 2.
#
#   blender --background --factory-startup --python-exit-code 1 \
#       --python tools/creator_kit_pictures.py -- --out <dir> --work <dir>
#
#   --out <dir>    where the 9 SVG files are written
#   --work <dir>   work folder: the stand-in kit and the .blend are built there
#
# How it works: the generator templates/make_creator_kit.py is run in memory
# (--stand-in --no-glb); the scene then holds the rig, the puppet, the kart
# parts, the wheels and the 14 actions. For each picture the objects are put
# in a pose, evaluated (armature applied), reduced with a Decimate modifier
# (for the picture only), projected with a fixed camera, culled at the back,
# sorted far to near (painter's order) and written as flat-shaded SVG paths.
# The colour of a face is the texture colour at the face's UV centre of the
# full-resolution mesh, times a fixed light. Diagrams are plain boxes and text.
# Nothing is random: two runs give byte-identical files.
#
# Exit codes: 0 good, 2 wrong call, 5 the kit could not be built or a picture
# could not be drawn.

import math
import os
import runpy
import sys
import traceback
from xml.sax.saxutils import escape

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
GENERATOR = os.path.join(REPO, "templates", "make_creator_kit.py")

USAGE = ("usage: creator_kit_pictures.py -- --out <dir> --work <dir>   "
         "(the pictures are made from the stand-in only)")

FILES = ("kit-overview", "studio-steps", "template-tree", "axes-size", "replace-figure",
         "skeleton", "weights", "clips", "export-path")

MAX_BYTES = 300 * 1000          # hard limit per file
TARGET_BYTES = 288 * 1000       # the budget loop aims below this

BG = "#f2f3f5"
INK = "#1f2933"
GREY = "#5f6b7a"
LINE = "#8a96a5"
COL_L = "#15803d"
COL_R = "#1d4ed8"
COL_M = "#c2410c"
COL_STUDIO = "#dbeafe"
COL_BLENDER = "#ffe8cc"
COL_GAME = "#d3f9d8"

KART_PARTS = ["kart_body", "kart_steer_wheel", "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"]


ERRORS = []


def fail(code, text):
    print(text)
    sys.exit(code)


# ---------------------------------------------------------------- arguments

def parse_args():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    out = work = None
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--out", "--work") and i + 1 < len(args):
            if a == "--out":
                out = args[i + 1]
            else:
                work = args[i + 1]
            i += 2
        elif a == "--kit" or a.startswith("--kit="):
            fail(2, "creator kit pictures: %s is refused: pictures are made from the stand-in only. %s"
                 % (a.split("=")[0], USAGE))
        else:
            fail(2, "creator kit pictures: unknown argument %s: pictures are made from the stand-in only. %s"
                 % (a, USAGE))
    if not out or not work:
        fail(2, "creator kit pictures: --out and --work are required. " + USAGE)
    return os.path.abspath(out), os.path.abspath(work)


# ---------------------------------------------------------------- number / text helpers

def fnum(v, nd=1):
    s = "%.*f" % (nd, v)
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    if s in ("-0", ""):
        s = "0"
    return s


def numjoin(vals):
    out = ""
    for s in vals:
        if out and not s.startswith("-"):
            out += " "
        out += s
    return out


TEXT_W = {"normal": 0.56, "bold": 0.62}


class Svg:
    def __init__(self, w, h, rx=12):
        self.w = w
        self.h = h
        self.parts = ['<rect width="%d" height="%d" rx="%d" fill="%s"/>' % (w, h, rx, BG)]
        self.texts = []

    def add(self, s):
        self.parts.append(s)

    def rect(self, x, y, w, h, fill="#fff", stroke=LINE, sw=1.2, rx=6, dash=None, extra=""):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        st = ' stroke="%s" stroke-width="%s"' % (stroke, fnum(sw)) if stroke else ""
        self.add('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="%s"%s%s%s/>'
                 % (fnum(x), fnum(y), fnum(w), fnum(h), fnum(rx), fill, st, d, extra))

    def line(self, x1, y1, x2, y2, stroke=GREY, sw=1.5, dash=None, cap="round"):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        self.add('<path d="M%s %sL%s %s" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="%s"%s/>'
                 % (fnum(x1), fnum(y1), fnum(x2), fnum(y2), stroke, fnum(sw), cap, d))

    def poly(self, pts, stroke=GREY, sw=1.5, dash=None, fill="none", close=False):
        d = ' stroke-dasharray="%s"' % dash if dash else ""
        p = "M" + numjoin([fnum(pts[0][0]), fnum(pts[0][1])])
        p += "L" + numjoin([fnum(c) for pt in pts[1:] for c in pt])
        if close:
            p += "z"
        self.add('<path d="%s" fill="%s" stroke="%s" stroke-width="%s" stroke-linejoin="round" '
                 'stroke-linecap="round"%s/>' % (p, fill, stroke, fnum(sw), d))

    def head(self, x, y, dx, dy, size=8, fill=GREY):
        """Arrow head with its tip at (x, y), pointing along (dx, dy)."""
        n = math.hypot(dx, dy) or 1.0
        dx, dy = dx / n, dy / n
        bx, by = x - dx * size, y - dy * size
        px, py = -dy * size * 0.42, dx * size * 0.42
        self.add('<path d="M%s %sL%s %sL%s %sz" fill="%s"/>'
                 % (fnum(x), fnum(y), fnum(bx + px), fnum(by + py), fnum(bx - px), fnum(by - py), fill))

    def arrow(self, pts, stroke=GREY, sw=2, size=9, dash=None):
        """Polyline with an arrow head at the end."""
        x1, y1 = pts[-1]
        x0, y0 = pts[-2]
        n = math.hypot(x1 - x0, y1 - y0) or 1.0
        ex, ey = x1 - (x1 - x0) / n * size * 0.8, y1 - (y1 - y0) / n * size * 0.8
        self.poly(pts[:-1] + [(ex, ey)], stroke=stroke, sw=sw, dash=dash)
        self.head(x1, y1, x1 - x0, y1 - y0, size, stroke)

    def circle(self, x, y, r, fill="#fff", stroke=INK, sw=1.2):
        st = ' stroke="%s" stroke-width="%s"' % (stroke, fnum(sw)) if stroke else ""
        self.add('<circle cx="%s" cy="%s" r="%s" fill="%s"%s/>' % (fnum(x), fnum(y), fnum(r), fill, st))

    def text(self, x, y, s, size=13, anchor="start", fill=INK, bold=False, italic=False, halo=False,
             track=True):
        att = ' font-size="%s"' % fnum(size)
        if anchor != "start":
            att += ' text-anchor="%s"' % anchor
        if bold:
            att += ' font-weight="bold"'
        if italic:
            att += ' font-style="italic"'
        if halo:
            att += ' stroke="%s" stroke-width="3" stroke-linejoin="round" paint-order="stroke"' % BG
        self.add('<text x="%s" y="%s"%s fill="%s">%s</text>' % (fnum(x), fnum(y), att, fill, escape(s)))
        if track:
            w = len(s) * size * TEXT_W["bold" if bold else "normal"]
            x0 = x if anchor == "start" else (x - w if anchor == "end" else x - w / 2)
            self.texts.append((x0, y - size * 0.8, x0 + w, y + size * 0.22, s))

    def label(self, x, y, lines, size=13, anchor="middle", fill=INK, bold=False, lead=None):
        lead = lead or size * 1.25
        for k, s in enumerate(lines):
            self.text(x, y + k * lead, s, size, anchor, fill, bold)

    def done(self, name):
        for (x0, y0, x1, y1, s) in self.texts:
            if x0 < 2 or y0 < 2 or x1 > self.w - 2 or y1 > self.h - 2:
                msg = ("%s: text '%s' leaves the picture (%.0f,%.0f,%.0f,%.0f)" % (name, s, x0, y0, x1, y1))
                print("  ERROR: " + msg)
                ERRORS.append(msg)
        for i in range(len(self.texts)):
            a = self.texts[i]
            for j in range(i + 1, len(self.texts)):
                b = self.texts[j]
                if a[0] < b[2] - 1 and b[0] < a[2] - 1 and a[1] < b[3] - 1 and b[1] < a[3] - 1:
                    print("  note: %s: texts '%s' and '%s' may overlap" % (name, a[4], b[4]))
        head = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" '
                'font-family="sans-serif">\n' % (self.w, self.h, self.w, self.h))
        return head + "\n".join(self.parts) + "\n</svg>\n"


# ---------------------------------------------------------------- 3D soup, camera, drawing

def np_():
    import numpy
    return numpy


class Soup:
    """Triangles in world space (Blender axes, metres) with a base colour (0..1) and a bone number."""

    def __init__(self, P, col, bone):
        self.P, self.col, self.bone = P, col, bone

    @staticmethod
    def join(soups):
        np = np_()
        return Soup(np.concatenate([s.P for s in soups]), np.concatenate([s.col for s in soups]),
                    np.concatenate([s.bone for s in soups]))


class Cam:
    """Orbit camera. az = 0 looks at the kart's front, +90 from the kart's left side; el up."""

    def __init__(self, az, el, target, dist=9.0, persp=True):
        np = np_()
        a, e = math.radians(az), math.radians(el)
        d = np.array([math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e), math.sin(e)])
        self.target = np.array(target, dtype=float)
        self.eye = self.target + dist * d
        self.dist = dist
        self.persp = persp
        self.f = -d
        r = np.cross(self.f, np.array([0.0, 0.0, 1.0]))
        self.r = r / np.linalg.norm(r)
        self.u = np.cross(self.r, self.f)
        light = -0.35 * self.r + 0.55 * self.u + 0.75 * (-self.f)
        self.light = light / np.linalg.norm(light)

    def project(self, P):
        pc = P - self.eye
        z = pc @ self.f
        x = pc @ self.r
        y = pc @ self.u
        if self.persp:
            k = self.dist / z
            return x * k, y * k, z
        return x, y, z


def fit_view(cam, soups, rect, extra_pts=None):
    np = np_()
    pts = np.concatenate([s.P.reshape(-1, 3) for s in soups] + ([np.array(extra_pts)] if extra_pts else []))
    u, v, _ = cam.project(pts)
    x0, y0, w, h = rect
    S = min(w / (u.max() - u.min()), h / (v.max() - v.min()))
    ox = x0 + w / 2 - S * (u.max() + u.min()) / 2
    oy = y0 + h / 2 + S * (v.max() + v.min()) / 2
    return S, ox, oy


def to_screen(cam, view, P):
    S, ox, oy = view
    u, v, z = cam.project(P)
    return ox + S * u, oy - S * v, z


def visible_order(sx, sy, z, sel, B=3):
    """Depth buffer at B times the picture resolution. Keeps the triangles that own at least 2 pixels and
    returns them far to near by the mean depth of their own visible pixels (ties by index)."""
    np = np_()
    if len(sel) == 0:
        return sel
    x, y = sx[sel] * B, sy[sel] * B
    x0, y0 = int(math.floor(x.min())) - 1, int(math.floor(y.min())) - 1
    W, H = int(math.ceil(x.max())) - x0 + 2, int(math.ceil(y.max())) - y0 + 2
    zb = np.full((H, W), np.inf)
    ib = np.full((H, W), -1, dtype=np.int32)
    for k in range(len(sel)):
        xs, ys, zz = x[k] - x0, y[k] - y0, z[sel[k]]
        ix0, ix1 = max(0, int(math.floor(xs.min()))), min(W, int(math.ceil(xs.max())) + 1)
        iy0, iy1 = max(0, int(math.floor(ys.min()))), min(H, int(math.ceil(ys.max())) + 1)
        if ix1 <= ix0 or iy1 <= iy0:
            continue
        d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(d) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(ix0, ix1) + 0.5, np.arange(iy0, iy1) + 0.5)
        a = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
        b = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
        c = 1.0 - a - b
        inside = (a >= 0) & (b >= 0) & (c >= 0)
        depth = a * zz[0] + b * zz[1] + c * zz[2]
        sub, ids = zb[iy0:iy1, ix0:ix1], ib[iy0:iy1, ix0:ix1]
        upd = inside & (depth < sub)
        sub[upd] = depth[upd]
        ids[upd] = k
    ok = ib >= 0
    cnt = np.bincount(ib[ok], minlength=len(sel))
    sm = np.bincount(ib[ok], weights=zb[ok], minlength=len(sel))
    idx = np.nonzero(cnt >= 2)[0]
    mean = sm[idx] / cnt[idx]
    return sel[idx[np.lexsort((idx, -np.round(mean, 5)))]]


def render_soup(soup, cam, view, nd=1, pale=0.0, stroke_w=0.35, overrides=None):
    """Return the SVG text of the soup: <g> with merged same-colour paths in painter's order."""
    np = np_()
    P = soup.P
    e1, e2 = P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]
    n = np.cross(e1, e2)
    ln = np.linalg.norm(n, axis=1)
    valid = ln > 1e-12
    n = n / np.where(valid, ln, 1.0)[:, None]
    cen = P.mean(axis=1)
    vv = (cam.eye - cen) if cam.persp else (-cam.f)[None, :]
    facing = ((n * vv).sum(axis=1) > 0) & valid
    sx, sy, z = to_screen(cam, view, P.reshape(-1, 3))
    sx, sy, z = sx.reshape(-1, 3), sy.reshape(-1, 3), z.reshape(-1, 3)
    depth = z.mean(axis=1)
    sel = np.nonzero(facing)[0]
    order = visible_order(sx, sy, z, sel)
    sh = 0.55 + 0.45 * np.clip(n[order] @ cam.light, 0.0, 1.0)
    col = soup.col[order]
    if pale:
        bg = np.array([0xf2, 0xf3, 0xf5]) / 255.0
        col = col * (1.0 - pale) + bg * pale
        sh = 0.8 + 0.2 * (sh - 0.55) / 0.45
    rgb = np.clip(col * sh[:, None] * 255.0, 0, 255)
    q = np.rint(rgb / 17.0).astype(int)
    runs = []
    cur, buf, start = None, [], None
    for k, idx in enumerate(order):
        px = [round(float(sx[idx, j]), nd) for j in range(3)]
        py = [round(float(sy[idx, j]), nd) for j in range(3)]
        pts = list(zip(px, py))
        if pts[0] == pts[1] or pts[1] == pts[2] or pts[0] == pts[2]:
            continue
        hexc = "#%x%x%x" % (q[k, 0], q[k, 1], q[k, 2])
        if hexc != cur:
            if cur is not None:
                runs.append((cur, "".join(buf)))
            cur, buf, start = hexc, [], None
        steps = [pts[1][0] - pts[0][0], pts[1][1] - pts[0][1], pts[2][0] - pts[1][0], pts[2][1] - pts[1][1]]
        if start is None:
            seg = "M" + numjoin([fnum(pts[0][0], nd), fnum(pts[0][1], nd)]) + "l"
        else:
            seg = "m" + numjoin([fnum(pts[0][0] - start[0], nd), fnum(pts[0][1] - start[1], nd)])
            seg += " "
        seg += numjoin([fnum(v, nd) for v in steps]) + "z"
        buf.append(seg)
        start = pts[0]
    if cur is not None:
        runs.append((cur, "".join(buf)))
    body = "".join('<path fill="%s" stroke="%s" d="%s"/>' % (c, c, d) for c, d in runs)
    return '<g stroke-width="%s" stroke-linejoin="round">%s</g>' % (fnum(stroke_w, 2), body)


def ground_ellipse(cam, view, rx, ry, cx=0.0, cy=0.0, n=28):
    np = np_()
    pts = np.array([[cx + rx * math.cos(2 * math.pi * k / n), cy + ry * math.sin(2 * math.pi * k / n), 0.0]
                    for k in range(n)])
    sx, sy, _ = to_screen(cam, view, pts)
    return [(float(a), float(b)) for a, b in zip(sx, sy)]


# ---------------------------------------------------------------- Blender side

class Kit:
    def __init__(self):
        import bpy
        self.bpy = bpy
        self.scene = bpy.context.scene
        self.rig = bpy.data.objects["creator_rig"]
        self.objs = {n: bpy.data.objects[n] for n in ["puppet"] + KART_PARTS}
        self.mods = {}
        for n, o in self.objs.items():
            m = o.modifiers.new("pic_decimate", "DECIMATE")
            m.decimate_type = "COLLAPSE"
            m.use_collapse_triangulate = True
            m.ratio = 1.0
            self.mods[n] = m
        self.tex = {n: self._texture(o) for n, o in self.objs.items()}
        self.vgroups = [vg.name for vg in self.objs["puppet"].vertex_groups]
        self.actions = {a.name: a for a in bpy.data.actions}

    def _texture(self, obj):
        np = np_()
        mat = obj.data.materials[0]
        img = None
        for nd in mat.node_tree.nodes:
            if nd.type == "TEX_IMAGE" and nd.image:
                img = nd.image
        if img is None:
            return None
        w, h = img.size
        arr = np.empty(w * h * 4, dtype=np.float32)
        img.pixels.foreach_get(arr)
        return arr.reshape(h, w, 4)[:, :, :3].copy()

    # pose: action name or None (= rest pose, which is the seated bind pose)
    def set_pose(self, action=None, frac=0.0):
        bpy = self.bpy
        rig = self.rig
        if action is None:
            rig.data.pose_position = "REST"
        else:
            rig.data.pose_position = "POSE"
            act = self.actions[action]
            if rig.animation_data is None:
                rig.animation_data_create()
            rig.animation_data.action = act
            if hasattr(rig.animation_data, "action_slot") and len(act.slots):
                rig.animation_data.action_slot = act.slots[0]
            self.scene.frame_set(int(round(frac * act.frame_range[1])))
        bpy.context.view_layer.update()

    def _eval(self, name):
        np = np_()
        bpy = self.bpy
        obj = self.objs[name]
        deps = bpy.context.evaluated_depsgraph_get()
        ev = obj.evaluated_get(deps)
        me = ev.to_mesh()
        nv = len(me.vertices)
        co = np.empty(nv * 3)
        me.vertices.foreach_get("co", co)
        co = co.reshape(-1, 3)
        me.calc_loop_triangles()
        nt = len(me.loop_triangles)
        tv = np.empty(nt * 3, dtype=np.int32)
        me.loop_triangles.foreach_get("vertices", tv)
        tv = tv.reshape(-1, 3)
        tl = np.empty(nt * 3, dtype=np.int32)
        me.loop_triangles.foreach_get("loops", tl)
        tl = tl.reshape(-1, 3)
        uvc = None
        if me.uv_layers.active is not None:
            uv = np.empty(len(me.loops) * 2)
            me.uv_layers.active.uv.foreach_get("vector", uv)
            uv = uv.reshape(-1, 2)
            uvc = uv[tl].mean(axis=1)
        vb = np.full(nv, -1, dtype=np.int32)
        if name == "puppet":
            for i, v in enumerate(me.vertices):
                if len(v.groups):
                    vb[i] = max(v.groups, key=lambda g: (g.weight, -g.group)).group
        mw = np.array(ev.matrix_world)
        wco = co @ mw[:3, :3].T + mw[:3, 3]
        ev.to_mesh_clear()
        P = wco[tv]
        b = vb[tv]
        tb = np.where(b[:, 1] == b[:, 2], b[:, 1], b[:, 0])
        return P, uvc, tb

    def _colours(self, name, uvc, count):
        np = np_()
        tex = self.tex[name]
        if tex is None or uvc is None:
            return np.full((count, 3), 0.7)
        h, w = tex.shape[:2]
        ix = np.clip(np.floor(uvc[:, 0] * w).astype(int), 0, w - 1)
        iy = np.clip(np.floor(uvc[:, 1] * h).astype(int), 0, h - 1)
        return tex[iy, ix]

    def soup(self, name, ratio):
        """The object's triangles in the current pose, reduced to `ratio`, coloured from the full mesh."""
        np = np_()
        bpy = self.bpy
        from mathutils import Vector
        from mathutils.bvhtree import BVHTree
        mod = self.mods[name]
        mod.ratio = 1.0
        bpy.context.view_layer.update()
        P, uvc, tb = self._eval(name)
        col = self._colours(name, uvc, len(P))
        if ratio >= 0.999:
            return Soup(P, col, tb)
        mod.ratio = max(0.01, min(1.0, ratio))
        bpy.context.view_layer.update()
        P2, _, _ = self._eval(name)
        mod.ratio = 1.0
        bpy.context.view_layer.update()
        verts = [Vector(v) for v in P.reshape(-1, 3)]
        tris = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(P))]
        tree = BVHTree.FromPolygons(verts, tris, all_triangles=True)
        cen = P2.mean(axis=1)
        idx = np.empty(len(P2), dtype=np.int64)
        for k in range(len(P2)):
            hit = tree.find_nearest(Vector(cen[k]))
            idx[k] = hit[2] if hit[2] is not None else 0
        return Soup(P2, col[idx], tb[idx])

    def bone(self, name):
        np = np_()
        b = self.rig.data.bones[name]
        return np.array(b.head_local), np.array(b.tail_local)

    def world(self, name):
        np = np_()
        return np.array(self.objs[name].matrix_world.translation) if name in self.objs else None


def seated(kit, f, puppet=0.3, body=0.5, wheel=0.25, steer=0.5):
    """Soups of the seated scene: (puppet, kart parts)."""
    p = kit.soup("puppet", puppet * f)
    k = [kit.soup("kart_body", body * f), kit.soup("kart_steer_wheel", steer * f)]
    k += [kit.soup(n, wheel * f) for n in ("wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr")]
    return p, Soup.join(k)


def callout(svg, view_pt, text_xy, text, anchor="start", size=13, bold=False):
    ax, ay = view_pt
    tx, ty = text_xy
    w = len(text) * size * (0.62 if bold else 0.56)
    ex = tx - 5 if anchor == "start" else (tx + 5 if anchor == "end" else tx)
    ey = ty - size * 0.35
    if anchor == "middle":
        ey = ty + 4 if ay > ty else ty - size - 2
    svg.line(ex, ey, ax, ay, INK, 1.1, cap="butt")
    svg.circle(ax, ay, 3, "#fff", INK, 1.3)
    svg.text(tx, ty, text, size, anchor, INK, bold, halo=True)


# ---------------------------------------------------------------- silhouette (picture 5)

def silhouette_paths(sx, sy, rect, eps=1.3):
    """Outline loops of the union of screen triangles, from a 1 px mask. Returns a list of loops."""
    np = np_()
    x0, y0, w, h = rect
    m = np.zeros((h + 2, w + 2), dtype=bool)
    for k in range(len(sx)):
        xs, ys = sx[k] - x0 + 1, sy[k] - y0 + 1
        ix0, ix1 = int(max(0, math.floor(xs.min()))), int(min(w + 1, math.ceil(xs.max())))
        iy0, iy1 = int(max(0, math.floor(ys.min()))), int(min(h + 1, math.ceil(ys.max())))
        if ix1 <= ix0 or iy1 <= iy0:
            continue
        gx, gy = np.meshgrid(np.arange(ix0, ix1) + 0.5, np.arange(iy0, iy1) + 0.5)
        d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(d) < 1e-9:
            continue
        a = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
        b = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
        c = 1.0 - a - b
        t = -0.04
        m[iy0:iy1, ix0:ix1] |= (a >= t) & (b >= t) & (c >= t)
    # close one-pixel cracks
    mm = m.copy()
    mm[1:, :] |= m[:-1, :]
    mm[:-1, :] |= m[1:, :]
    mm[:, 1:] |= m[:, :-1]
    mm[:, :-1] |= m[:, 1:]
    m = mm
    edges = {}
    H, W = m.shape
    for j in range(H):
        for i in range(W):
            if not m[j, i]:
                continue
            if j == 0 or not m[j - 1, i]:
                edges.setdefault((i, j), []).append((i + 1, j))
            if i == W - 1 or not m[j, i + 1]:
                edges.setdefault((i + 1, j), []).append((i + 1, j + 1))
            if j == H - 1 or not m[j + 1, i]:
                edges.setdefault((i + 1, j + 1), []).append((i, j + 1))
            if i == 0 or not m[j, i - 1]:
                edges.setdefault((i, j + 1), []).append((i, j))
    loops = []
    for start in sorted(edges):
        while edges.get(start):
            loop = [start]
            cur = start
            while True:
                nxt = edges[cur].pop(0)
                if not edges[cur]:
                    del edges[cur]
                if nxt == start:
                    break
                loop.append(nxt)
                cur = nxt
                if cur not in edges:
                    break
            if len(loop) > 8:
                loops.append(loop)

    def rdp(pts, e):
        if len(pts) < 3:
            return pts
        (ax, ay), (bx, by) = pts[0], pts[-1]
        dx, dy = bx - ax, by - ay
        nn = math.hypot(dx, dy)
        best, bi = -1.0, 0
        for k in range(1, len(pts) - 1):
            px, py = pts[k]
            dd = abs(dy * (px - ax) - dx * (py - ay)) / nn if nn else math.hypot(px - ax, py - ay)
            if dd > best:
                best, bi = dd, k
        if best > e:
            return rdp(pts[:bi + 1], e)[:-1] + rdp(pts[bi:], e)
        return [pts[0], pts[-1]]

    out = []
    for loop in loops:
        far = max(range(len(loop)), key=lambda k: (loop[k][0] - loop[0][0]) ** 2 + (loop[k][1] - loop[0][1]) ** 2)
        a = rdp(loop[:far + 1], eps)
        b = rdp(loop[far:] + [loop[0]], eps)
        pts = a[:-1] + b[:-1]
        if len(pts) >= 3:
            area = abs(sum(pts[k][0] * pts[(k + 1) % len(pts)][1] - pts[(k + 1) % len(pts)][0] * pts[k][1]
                           for k in range(len(pts)))) / 2
            if area > 40:
                out.append([(p[0] + x0 - 1.0, p[1] + y0 - 1.0) for p in pts])
    return out


# ---------------------------------------------------------------- the pictures

def pic_overview(kit, f):
    svg = Svg(760, 480)
    kit.set_pose(None)
    puppet, kart = seated(kit, f, 1.0, 1.0, 1.0, 1.0)
    cam = Cam(36, 20, (0.0, -0.05, 0.6), 9.0, True)
    view = fit_view(cam, [puppet, kart], (170, 50, 430, 370))
    gx = ground_ellipse(cam, view, 0.85, 1.25, 0.0, -0.05)
    svg.poly(gx, stroke=None, sw=0, fill="#dfe2e7", close=True)
    svg.add(render_soup(Soup.join([kart, puppet]), cam, view))

    def scr(p):
        a, b, _ = to_screen(cam, view, np_().array([p]))
        return float(a[0]), float(b[0])

    head, _ = kit.bone("head")
    steer = kit.world("kart_steer_wheel")
    body = kit.world("kart_body")
    wfl = kit.world("wheel_fl")
    callout(svg, scr(head + np_().array([0.04, 0.0, 0.3])), (205, 62), "Figure (puppet)", "end", 15, True)
    callout(svg, scr(np_().array([0.0, 0.62, 0.38])), (655, 150), "Kart", "start", 15, True)
    callout(svg, scr(wfl + np_().array([0.1, 0.0, 0.0])), (215, 430), "wheels", "end", 15, True)
    callout(svg, scr(steer + np_().array([0.0, 0.0, 0.1])), (160, 262), "steering wheel", "end", 15, True)
    svg.text(380, 465, "Stand-in kit: made by code, no game data", 11, "middle", GREY)
    return svg.done("kit-overview")


def box(svg, x, y, w, h, lines, fill, sub=None, size=14, bold_last=True, num=None):
    svg.rect(x, y, w, h, fill, LINE, 1.4, 8)
    n = len(lines)
    ty = y + h / 2 + size * 0.35 - (n - 1) * size * 0.6
    for k, s in enumerate(lines):
        last = k == n - 1
        svg.text(x + w / 2, ty + k * size * 1.2, s, size if last else size - 2, "middle",
                 INK if last else GREY, bold=last and bold_last)
    if num is not None:
        svg.circle(x + 2, y + 2, 11, "#fff", INK, 1.4)
        svg.text(x + 2, y + 6.5, str(num), 13, "middle", INK, True)


def pic_studio_steps():
    svg = Svg(760, 330)
    w, h, gap = 200, 62, 56
    x0 = (760 - (3 * w + 2 * gap)) / 2
    y1, y2 = 62, 196
    xs = [x0 + k * (w + gap) for k in range(3)]
    specs1 = [(["Reload Studio:", "Creator Kit"], COL_STUDIO), (["Generate demo files"], COL_STUDIO),
              (["Blender:", "creator-kit.blend"], COL_BLENDER)]
    specs2 = [(["Export (test on your PC):", "creator-kit-demo-local.glb"], COL_BLENDER), (["Reload Studio:", "Character"], COL_STUDIO),
              (["Test in game"], COL_GAME)]
    for k, (ln, fl) in enumerate(specs1):
        box(svg, xs[k], y1, w, h, ln, fl, num=k + 1)
    for k, (ln, fl) in enumerate(specs2):
        box(svg, xs[k], y2, w, h, ln, fl, size=12 if k == 0 else 14, num=k + 4)
    for k in range(2):
        svg.arrow([(xs[k] + w + 4, y1 + h / 2), (xs[k + 1] - 4, y1 + h / 2)])
        svg.arrow([(xs[k] + w + 4, y2 + h / 2), (xs[k + 1] - 4, y2 + h / 2)])
    ym = (y1 + h + y2) / 2
    svg.arrow([(xs[2] + w / 2, y1 + h + 4), (xs[2] + w / 2, ym), (xs[0] + w / 2, ym), (xs[0] + w / 2, y2 - 4)])
    svg.text(380, 34, "From the kit files to a test in the game", 16, "middle", INK, True)
    # legend
    lx = 150
    for fill, name in ((COL_STUDIO, "Reload Studio"), (COL_BLENDER, "Blender"), (COL_GAME, "the game")):
        svg.rect(lx, 292, 16, 16, fill, LINE, 1.2, 3)
        svg.text(lx + 24, 305, name, 13, "start", INK)
        lx += 170
    return svg.done("studio-steps")


def tree_icon(svg, x, y, kind):
    c = {"col": ("#e8e8e8", "#9a9a9a"), "arm": ("#7ed99b", "#3b8f57"), "mesh": ("#7fb2f0", "#3d6fb0"),
         "empty": ("#d0d0d0", "#8a8a8a")}[kind]
    if kind == "col":
        svg.rect(x, y - 10, 13, 11, "none", c[0], 1.6, 2)
    elif kind == "arm":
        svg.poly([(x + 6.5, y - 11), (x + 13, y + 1), (x, y + 1)], stroke=c[1], sw=1, fill=c[0], close=True)
    elif kind == "mesh":
        svg.rect(x, y - 10, 13, 11, c[0], c[1], 1, 2)
    else:
        svg.line(x + 6.5, y - 10, x + 6.5, y + 1, c[0], 1.8)
        svg.line(x, y - 4.5, x + 13, y - 4.5, c[0], 1.8)


def pic_template_tree():
    rows = [
        (0, "col", "CreatorKit", "second exporter: demo-local"), (1, "col", "Figure", "exporter: figure"),
        (2, "arm", "creator_rig", ""), (2, "mesh", "puppet", ""),
        (1, "col", "Kart", ""), (2, "mesh", "kart_body", "kit"),
        (2, "mesh", "kart_steer_wheel", "kit"), (2, "mesh", "wheel_fl", "kit"), (2, "mesh", "wheel_fr", "kit"),
        (2, "mesh", "wheel_rl", "kit"), (2, "mesh", "wheel_rr", "kit"),
        (0, "col", "Guides", ""), (1, "empty", "CrashSize", ""), (1, "empty", "SeatPoint", ""),
        (1, "empty", "StandSpot", ""), (1, "empty", "Front", ""),
        (0, "col", "Classic", "only with an old character"),
    ]
    rh = 25
    top = 64
    H = top + len(rows) * rh + 16
    svg = Svg(760, H)
    svg.rect(20, 18, 720, H - 36, "#2b2f36", "#14171a", 1.5, 8)
    svg.rect(20, 18, 720, 30, "#3a3f47", "#14171a", 1.5, 8)
    svg.text(36, 38, "Outliner", 14, "start", "#e6e8eb", True)
    for k, (ind, kind, name, note) in enumerate(rows):
        y = top + k * rh
        if k % 2 == 0:
            svg.add('<rect x="21" y="%s" width="718" height="%s" fill="#303540"/>' % (fnum(y - 17), fnum(rh)))
        x = 40 + ind * 24
        tree_icon(svg, x, y, kind)
        svg.text(x + 22, y, name, 14, "start", "#e6e8eb")
        if note == "kit":
            tx = x + 22 + len(name) * 8.2 + 10
            svg.circle(tx + 4, y - 4, 4, "#f08c00", "#f08c00", 1)
            svg.text(tx + 12, y, "kit", 12, "start", "#ffa94d", True)
        elif note:
            svg.text(x + 22 + len(name) * 8.2 + 12, y, note, 12, "start", "#9aa3ad", italic=True)
    # brackets: what each part is exported into
    bx = 400

    def bracket(r0, r1, col, tcol, lines):
        y0, y1 = top + r0 * rh - 14, top + r1 * rh + 6
        svg.poly([(bx, y0), (bx + 8, y0), (bx + 8, y1), (bx, y1)], stroke=col, sw=2)
        ym = (y0 + y1) / 2 + 4 - (len(lines) - 1) * 8
        for q, t in enumerate(lines):
            svg.text(bx + 18, ym + q * 17, t, 13, "start", tcol, q == 0)

    bracket(1, 3, "#6fd08c", "#6fd08c", ["creator-kit-figure.glb", "no game data (share)"])
    bracket(4, 10, "#ffa94d", "#ffa94d", ["only in creator-kit-demo-local.glb",
                                          "your game data (keep local)"])
    bracket(11, 16, "#9aa3ad", "#c3c9d0", ["never exported"])
    return svg, H


def pic_template_tree_done():
    svg, H = pic_template_tree()
    return svg.done("template-tree")


def pic_axes(kit, f):
    svg = Svg(760, 440)
    kit.set_pose(None)
    puppet, kart = seated(kit, f, 1.0, 1.0, 1.0, 1.0)
    cam = Cam(90, 0, (0.0, 0.0, 0.7), 10.0, False)
    box_pts = [(-0.528, -0.911, 0.0875), (0.535, 0.845, 1.38)]
    corners = [(x, y, z) for x in (box_pts[0][0], box_pts[1][0]) for y in (box_pts[0][1], box_pts[1][1])
               for z in (box_pts[0][2], box_pts[1][2])]
    view = fit_view(cam, [puppet, kart], (40, 70, 560, 270), corners + [(0.0, 0.0, 0.0)])
    np = np_()

    def scr(p):
        a, b, _ = to_screen(cam, view, np.array([p], dtype=float))
        return float(a[0]), float(b[0])

    gy = scr((0, 0, 0))[1]
    svg.line(20, gy, 590, gy, GREY, 2)
    for k in range(0, 570, 14):
        svg.line(24 + k, gy, 14 + k, gy + 9, LINE, 1)
    svg.text(26, gy + 26, "ground (z = 0)", 12, "start", GREY)
    svg.add(render_soup(Soup.join([kart, puppet]), cam, view))
    # size box
    (bx0, by1), (bx1, by0) = scr(box_pts[0]), scr(box_pts[1])
    svg.rect(min(bx0, bx1), min(by0, by1), abs(bx1 - bx0), abs(by1 - by0), "none", "#d9480f", 2, 2, dash="7 5")
    svg.text(min(bx0, bx1) + 4, min(by0, by1) - 8, "size box", 14, "start", "#d9480f", True)
    # dimension line along the kart (front -0.911 .. rear 0.845)
    xa, xb = scr((0, -0.911, 0))[0], scr((0, 0.845, 0))[0]
    dy = gy + 56
    svg.line(xa, gy + 4, xa, dy + 6, LINE, 1)
    svg.line(xb, gy + 4, xb, dy + 6, LINE, 1)
    svg.line(xa, dy, xb, dy, INK, 1.6)
    svg.head(xa, dy, -1, 0, 9, INK)
    svg.head(xb, dy, 1, 0, 9, INK)
    svg.text((xa + xb) / 2, dy - 8, "about 1.76 m", 15, "middle", INK, True)
    # seat point
    sp = scr((0.0, -0.014, 0.16))
    svg.circle(sp[0], sp[1], 5.5, "#ffd43b", INK, 1.6)
    svg.line(sp[0] + 5, sp[1] + 3, sp[0] + 40, sp[1] + 46, INK, 1.1)
    svg.text(sp[0] + 44, sp[1] + 58, "seat point", 14, "start", INK, True, halo=True)
    # axes gizmo
    ox, oy = 640, 250
    svg.arrow([(ox, oy), (ox - 74, oy)], INK, 2.2, 10)
    svg.arrow([(ox, oy), (ox, oy - 74)], INK, 2.2, 10)
    svg.circle(ox, oy, 4, INK, INK, 1)
    svg.text(ox - 74, oy + 22, "-Y = front", 14, "start", INK, True)
    svg.text(ox + 10, oy - 62, "+Z = up", 14, "start", INK, True)
    svg.text(380, 34, "Seen from the kart's left side", 16, "middle", INK, True)
    return svg.done("axes-size")


def pic_replace(kit, f):
    svg = Svg(760, 420)
    kit.set_pose(None)
    puppet, kart = seated(kit, f, 1.0, 1.0, 1.0, 1.0)
    cam = Cam(90, 0, (0.0, 0.0, 0.7), 10.0, False)
    pw = 350
    view = fit_view(cam, [puppet, kart], (20, 70, pw - 40, 280))
    S, ox, oy = view
    np = np_()
    for k in range(2):
        px = 15 + k * (pw + 20)
        svg.rect(px, 14, pw, 360, "#fff", "#d0d5db", 1.2, 10)
    sh = 380
    # left panel
    vl = (S, ox, oy)
    svg.add(render_soup(Soup.join([kart, puppet]), cam, vl))
    svg.text(15 + pw / 2, 48, "puppet", 18, "middle", INK, True)
    # right panel: same kart, the puppet as a silhouette
    vr = (S, ox + sh, oy)
    svg.add(render_soup(kart, cam, vr))
    sx, sy, _ = to_screen(cam, vr, puppet.P.reshape(-1, 3))
    sx, sy = sx.reshape(-1, 3), sy.reshape(-1, 3)
    rect = (int(sh + 15), 60, pw, 310)
    loops = silhouette_paths(sx, sy, rect)
    for lp in loops:
        d = "M" + numjoin([fnum(c) for pt in lp[:1] for c in pt]) + "L" + numjoin([fnum(c) for pt in lp[1:] for c in pt]) + "z"
        svg.add('<path d="%s" fill="#eceff2" stroke="#e8590c" stroke-width="2" '
                'stroke-dasharray="7 5" stroke-linejoin="round"/>' % d)
    svg.text(15 + pw + 20 + pw / 2, 48, "your figure", 18, "middle", INK, True)
    head, _ = kit.bone("head")
    hx, hy, _ = to_screen(cam, vr, np.array([[0.0, head[1] - 0.05, head[2] + 0.3]]))
    svg.text(float(hx[0]) + 50, float(hy[0]) - 12, "your figure here", 14, "start", "#c2410c", True, True)
    svg.line(float(hx[0]) + 46, float(hy[0]) - 16, float(hx[0]) + 6, float(hy[0]) + 4, "#c2410c", 1.2)
    # same seat marker in both panels
    sp = np.array([[0.0, -0.014, 0.16]])
    for v, name in ((vl, 0), (vr, 1)):
        a, b, _ = to_screen(cam, v, sp)
        svg.circle(float(a[0]), float(b[0]), 5, "#ffd43b", INK, 1.5)
    svg.text(380, 400, "same kart, same seat (yellow dot), same skeleton", 13, "middle", GREY)
    return svg.done("replace-figure")


def bone_color(name):
    if name.endswith(".L"):
        return COL_L
    if name.endswith(".R"):
        return COL_R
    return COL_M


def spread(ys, gap, lo, hi):
    """Move label heights apart (keeps the order, minimum distance `gap`, inside lo..hi)."""
    order = sorted(range(len(ys)), key=lambda k: ys[k])
    out = list(ys)
    prev = lo - gap
    for k in order:
        out[k] = max(ys[k], prev + gap)
        prev = out[k]
    over = out[order[-1]] - hi
    if over > 0:
        for k in order:
            out[k] -= over
    return out


def stack(svg, pos, names, x, y0, dy, anchor="end"):
    """Labels in the given order, evenly spaced, each with a leader to its bone."""
    for k, n in enumerate(names):
        callout(svg, pos[n], (x, y0 + k * dy), n, anchor, 14, True)


def pic_skeleton(kit, f):
    svg = Svg(840, 560)
    kit.set_pose(None)
    np = np_()
    puppet = kit.soup("puppet", 1.0 * f)
    figure = [n for n in kit.vgroups if n not in ("kart", "kart_steer")]
    draw_order = sorted(figure, key=lambda n: (0 if n.endswith(".R") else (2 if n.endswith(".L") else 1), n))
    svg.rect(10, 10, 430, 480, "#fff", "#d0d5db", 1.2, 10)
    svg.rect(450, 10, 380, 480, "#fff", "#d0d5db", 1.2, 10)
    views = [("front view", Cam(0, 4, (0.0, 0.0, 0.7), 10.0, False), (125, 70, 190, 400)),
             ("side view (kart front on the left)", Cam(90, 0, (0.0, 0.0, 0.7), 10.0, False), (565, 80, 255, 390))]
    scr = []
    for title, cam, rect in views:
        view = fit_view(cam, [puppet], rect)
        svg.add(render_soup(puppet, cam, view, 1, 0.55))
        pos = {}
        for nme in draw_order:
            h, t = kit.bone(nme)
            ha, hb, _ = to_screen(cam, view, np.array([h]))
            ta, tb, _ = to_screen(cam, view, np.array([t]))
            ma, mb, _ = to_screen(cam, view, np.array([(h + t) / 2]))
            svg.line(float(ha[0]), float(hb[0]), float(ta[0]), float(tb[0]), bone_color(nme), 2.4)
            pos[nme] = (float(ma[0]), float(mb[0]))
        for nme in draw_order:
            h, t = kit.bone(nme)
            ha, hb, _ = to_screen(cam, view, np.array([h]))
            svg.circle(float(ha[0]), float(hb[0]), 2.8, "#fff", bone_color(nme), 1.4)
        scr.append(pos)
        svg.text(rect[0] + rect[2] / 2 + (0 if title == "front view" else -40), 40, title, 16, "middle", INK, True)

    def column(pos, names, x, anchor, lo, hi, gap=26):
        ys = spread([pos[n][1] for n in names], gap, lo, hi)
        for n, y in zip(names, ys):
            callout(svg, pos[n], (x, y + 4), n, anchor, 14, True)

    column(scr[0], ["head", "neck", "chest", "spine", "pelvis"], 105, "end", 80, 440)
    callout(svg, scr[0]["shoulder.L"], (342, 130), "shoulder.L", "start", 14, True)
    stack(svg, scr[1], ["upper_arm.L", "forearm.L", "hand.L", "fingers.L"], 550, 120, 34)
    stack(svg, scr[1], ["thigh.L", "shin.L", "foot.L", "toe.L"], 550, 345, 34)
    callout(svg, scr[1]["root"], (782, 466), "root", "start", 14, True)
    svg.rect(10, 500, 820, 50, "#f8f9fa", LINE, 1, 8)
    svg.text(30, 530, ".L = left (+X), .R = right (-X)", 14, "start", INK, True)
    svg.line(330, 525, 358, 525, COL_L, 3.5)
    svg.text(366, 530, ".L", 13, "start", INK)
    svg.line(400, 525, 428, 525, COL_R, 3.5)
    svg.text(436, 530, ".R", 13, "start", INK)
    svg.line(470, 525, 498, 525, COL_M, 3.5)
    svg.text(506, 530, "middle bones (no side)", 13, "start", INK)
    svg.text(820, 530, "joints = dots", 13, "end", GREY)
    return svg.done("skeleton")


def hsl(h, s, l):
    import colorsys
    r, g, b = colorsys.hls_to_rgb(h, l, s)
    return (r, g, b)


def bone_palette(names):
    pal = {}
    mids = [n for n in names if not (n.endswith(".L") or n.endswith(".R"))]
    warm = [(0.08, 0.12, 0.50), (0.04, 0.85, 0.48), (0.09, 0.90, 0.55), (0.12, 0.45, 0.62),
            (0.07, 0.30, 0.42), (0.02, 0.65, 0.35)]
    for k, n in enumerate(mids):
        h, s, l = warm[k % len(warm)]
        pal[n] = hsl(h, s, l)
    for side, base in ((".L", 0.30), (".R", 0.64)):
        sided = [n for n in names if n.endswith(side)]
        for k, n in enumerate(sided):
            hue = base + (k % 5) * 0.045 - 0.09
            light = 0.30 if k % 2 == 0 else 0.55
            pal[n] = hsl(hue, 0.75, light)
    return pal


def pic_weights(kit, f):
    svg = Svg(760, 560)
    kit.set_pose(None)
    np = np_()
    puppet = kit.soup("puppet", 1.0 * f)
    figure = [n for n in kit.vgroups if n not in ("kart", "kart_steer")]
    pal = bone_palette(figure)
    cols = np.zeros((len(puppet.P), 3))
    for k, b in enumerate(puppet.bone):
        nm = kit.vgroups[b] if b >= 0 else "root"
        cols[k] = pal.get(nm, (0.5, 0.5, 0.5))
    colored = Soup(puppet.P, cols, puppet.bone)
    cam = Cam(18, 6, (0.0, 0.0, 0.7), 10.0, False)
    view = fit_view(cam, [colored], (30, 70, 320, 420))
    svg.add(render_soup(colored, cam, view))
    svg.text(190, 40, "one colour per bone", 20, "middle", INK, True)
    # legend, three groups
    x0, y = 420, 90
    groups = [("middle bones", [n for n in figure if not (n.endswith(".L") or n.endswith(".R"))]),
              (".L bones (left, +X)", [n for n in figure if n.endswith(".L")]),
              (".R bones (right, -X)", [n for n in figure if n.endswith(".R")])]
    cx = [x0, x0 + 170]
    for title, names in groups:
        svg.text(x0, y, title, 14, "start", INK, True)
        y += 10
        for k, n in enumerate(names):
            c = pal[n]
            hexc = "#%02x%02x%02x" % tuple(int(round(v * 255)) for v in c)
            col = k % 2
            row = k // 2
            svg.rect(cx[col], y + row * 24 + 4, 16, 16, hexc, "#555", 0.8, 3)
            svg.text(cx[col] + 22, y + row * 24 + 17, n, 12.5, "start", INK)
        y += ((len(names) + 1) // 2) * 24 + 26
    svg.text(380, 538, "Every point of the puppet follows exactly one bone.", 13, "middle", GREY)
    return svg.done("weights")


def pic_clips(kit, f):
    from_names = ["idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit",
                  "podium_1", "podium_2", "podium_3", "lose", "menu", "finish_win", "finish_lose"]
    frac = {"idle": 0.25, "steer_left": 1.0, "steer_right": 1.0, "jump": 1.0, "drift_left": 0.25,
            "drift_right": 0.25, "hit": 0.3, "podium_1": 0.25, "podium_2": 0.3, "podium_3": 0.25,
            "lose": 0.6, "menu": 0.3, "finish_win": 0.25, "finish_lose": 0.25}
    standing = {"podium_1", "podium_2", "podium_3", "lose", "menu"}
    optional = {"finish_win", "finish_lose"}
    np = np_()
    TW, TH, GAP, MX = 185, 200, 12, 12
    W = MX * 2 + 4 * TW + 3 * GAP
    rows = (len(from_names) + 3) // 4
    H = MX * 2 + rows * TH + (rows - 1) * GAP
    svg = Svg(W, H)
    cam = Cam(32, 12, (0.0, 0.0, 0.7), 9.0, True)
    soups = {}
    for name in from_names:
        kit.set_pose(name, frac[name])
        p = kit.soup("puppet", 0.4 * f)
        if name in standing:
            soups[name] = [p]
        else:
            k = [kit.soup("kart_body", 0.5 * f), kit.soup("kart_steer_wheel", 0.4 * f)]
            k += [kit.soup(n, 0.15 * f) for n in ("wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr")]
            soups[name] = [Soup.join(k + [p])]
    ring = lambda r: np.array([[r * math.cos(t), r * math.sin(t) - 0.05 * (r > 0.5), 0.0] for t in (0, 1.57, 3.14, 4.71)])
    ext = {}
    for grp in (False, True):
        pts = [s.P.reshape(-1, 3) for n, v in soups.items() if (n in standing) == grp for s in v] + [ring(0.45 if grp else 1.25)]
        u, v, _ = cam.project(np.concatenate(pts))
        ext[grp] = (u.min(), u.max(), v.max(), min((TW - 24) / (u.max() - u.min()), (TH - 62) / (v.max() - v.min())))
    for k, name in enumerate(from_names):
        col, row = k % 4, k // 4
        tx = MX + col * (TW + GAP)
        ty = MX + row * (TH + GAP)
        svg.rect(tx, ty, TW, TH, "#fff", "#d0d5db", 1.2, 8)
        umin, umax, vmax, S = ext[name in standing]
        ox = tx + TW / 2 - S * (umax + umin) / 2
        oy = ty + 12 + S * vmax
        view = (S, ox, oy)
        rx, ry = (0.85, 1.25) if name not in standing else (0.45, 0.45)
        svg.poly(ground_ellipse(cam, view, rx, ry, 0.0, -0.05 if name not in standing else 0.0),
                 stroke=None, sw=0, fill="#e6e8ec", close=True)
        for s in soups[name]:
            svg.add(render_soup(s, cam, view, 0, 0.0, 0.3))
        svg.text(tx + TW / 2, ty + TH - 30, name, 15, "middle", INK, True)
        svg.text(tx + TW / 2, ty + TH - 12, "standing" if name in standing else
                 "seated, optional" if name in optional else "seated", 12, "middle",
                 "#c2410c" if name in standing else GREY)
    return svg.done("clips")


def pic_export_path():
    svg = Svg(800, 396)
    ws = [140, 160, 160, 230]
    gap = 28
    x0 = (800 - (sum(ws) + 3 * gap)) / 2
    xs = [x0]
    for k in range(3):
        xs.append(xs[-1] + ws[k] + gap)
    h = 62
    svg.text(400, 34, "Where to click in Blender", 16, "middle", INK, True)
    lanes = [
        (52, "#e6f7ea", "#2f9e44", "#15803d", "(a) the file to share: the figure alone", "select Figure",
         "creator-kit-figure.glb", "figure alone, no game data", "#b2f2bb"),
        (196, "#fff0e0", "#e8590c", "#c2410c", "(b) for a test on your own PC", "select CreatorKit",
         "creator-kit-demo-local.glb", "with your kart, keep it local", "#ffc078"),
    ]
    for (T, bg, edge, tcol, title, sel, res, cap, rfill) in lanes:
        svg.rect(6, T, 788, 128, bg, edge, 1.6, 10)
        svg.text(20, T + 22, title, 14, "start", tcol, True)
        by = T + 34
        specs = [(["Outliner:", sel], 14), (["Properties:", "Collection tab"], 14),
                 (["Exporters:", "glTF 2.0 > Export"], 14)]
        for k, (ln, sz) in enumerate(specs):
            box(svg, xs[k], by, ws[k], h, ln, "#fff", size=sz, num=k + 1)
        box(svg, xs[3], by, ws[3], h, ["Result:", res], rfill, size=13)
        for k in range(3):
            svg.arrow([(xs[k] + ws[k] + 4, by + h / 2), (xs[k + 1] - 4, by + h / 2)])
        svg.text(xs[3] + ws[3] / 2, by + h + 18, cap, 12.5, "middle", tcol)
    svg.rect(200, 342, 400, 40, "#fff", LINE, 1.4, 8, dash="6 4")
    svg.text(400, 367, "or  File > Export All Collections (writes both)", 14, "middle", INK, True)
    return svg.done("export-path")


# ---------------------------------------------------------------- main

def draw_all(kit, out):
    jobs = [
        ("kit-overview", lambda f: pic_overview(kit, f)),
        ("studio-steps", lambda f: pic_studio_steps()),
        ("template-tree", lambda f: pic_template_tree_done()),
        ("axes-size", lambda f: pic_axes(kit, f)),
        ("replace-figure", lambda f: pic_replace(kit, f)),
        ("skeleton", lambda f: pic_skeleton(kit, f)),
        ("weights", lambda f: pic_weights(kit, f)),
        ("clips", lambda f: pic_clips(kit, f)),
        ("export-path", lambda f: pic_export_path()),
    ]
    sizes = {}
    for name, fn in jobs:
        f = 1.0
        for _ in range(24):
            text = fn(f)
            data = text.encode("utf-8")
            if len(data) <= TARGET_BYTES:
                break
            f *= 0.88
        if len(data) > MAX_BYTES:
            fail(5, "creator kit pictures: %s is %d bytes, over the limit" % (name, len(data)))
        with open(os.path.join(out, "kit-guide-" + name + ".svg"), "wb") as fh:
            fh.write(data)
        sizes[name] = (len(data), f)
        print("creator kit pictures: %s.svg %d bytes (detail factor %.2f)" % (name, len(data), f))
    return sizes


def build_kit(work):
    """Run the generator in memory: stand-in kit, no GLB. The scene then holds everything."""
    saved = list(sys.argv)
    sys.argv = [saved[0], "--", "--kit", os.path.join(work, "kit"), "--stand-in", "--no-glb"]
    try:
        runpy.run_path(GENERATOR, run_name="__main__")
    except SystemExit as e:
        if e.code not in (0, None):
            fail(5, "creator kit pictures: the stand-in kit could not be built (exit %s)" % e.code)
    finally:
        sys.argv = saved


def main():
    out, work = parse_args()
    try:
        import bpy  # noqa: F401
    except ImportError:
        fail(2, "creator kit pictures: this script runs inside Blender. " + USAGE)
    os.makedirs(out, exist_ok=True)
    os.makedirs(work, exist_ok=True)
    build_kit(work)
    kit = Kit()
    draw_all(kit, out)
    if ERRORS:
        fail(5, "creator kit pictures: %d layout errors" % len(ERRORS))
    print("creator kit pictures: ok")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:  # noqa: B902
        print("creator kit pictures: internal error: %s" % e)
        traceback.print_exc()
        sys.exit(5)
