#!/usr/bin/env python3
"""ctest driver for the Creator Kit generator (templates/make_creator_kit.py), Blender 5.2.

  python tools/creator_kit_test.py --rldpack <rldpack.exe> --work <dir> [--blender <blender.exe>]
  python tools/creator_kit_test.py --rldpack-studio <ReloadStudio.exe> --work <dir> [--blender ...]
  python tools/creator_kit_test.py selftest

--rldpack          path of rldpack.exe
--rldpack-studio   ReloadStudio.exe; the packer is called as "<exe> --rldpack <verb> ..."
Blender: --blender, else env CTR_BLENDER, else (Windows) C:\\Program Files\\Blender Foundation\\Blender 5.2\\blender.exe.
Without Blender 5.2 the test prints "creator-kit-test: SKIP (Blender 5.2 not found)" and exits 77
(ctest SKIP_RETURN_CODE 77).

Steps: 1 generate, 2 byte-identical second run, 3 reopen + re-export through both stored exporters,
4 hires-check of creator-kit-demo-local.glb (--kart-part: only HRG-41), 5 hires-check of creator-kit-figure.glb (no wheel
nodes: only HRG-20, the standard wheels), 6 the kit marker (ctr_kit extras, ctrkit_ materials) on every kit
part of the local demo and on nothing of the figure, 7 make-char --check, 8 creator_kit_measure, 9 --vcol
gltf, 10 wrong-version exit 3, 11 no path in the .blend, 12 a kit kart with an empty root node, 13 an old
character (classic and native model of a .rldchar made here from the old template, char-export, --classic):
in the collection Classic, size and box as the export, same triangles, colours and images, in no export.
The local demo is checked as it is packed (--kart-part): only HRG-41 (the kit mark) may warn.
Last line: "creator-kit-test: <n> steps, <f> failed"; exit 0 when f = 0, else 1.

Every child process gets the Blender user folders, TEMP and TMP below <work>. Only the driver's own
subfolders of <work> (r1, r2, gltf, fake, root, classic, temp) are emptied. Standard library only.
"""
import ast
import hashlib
import json
import struct
import os
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GENERATOR = os.path.join(REPO, "templates", "make_creator_kit.py")
OLD_TEMPLATE = os.path.join(REPO, "templates", "make_driver_template.py")
MEASURE = os.path.join(REPO, "tools", "creator_kit_measure.py")
# Blender 5.2 where its installer puts it (the 64-bit Program Files folder).
DEFAULT_BLENDER = os.path.join(os.environ.get("ProgramW6432") or os.environ.get("ProgramFiles") or "",
                               "Blender Foundation", "Blender 5.2", "blender.exe")
TIMEOUT = 300
SKIP_CODE = 77
OWN_DIRS = ("r1", "r2", "gltf", "fake", "root", "classic", "temp")


# ------------------------------------------------------------------ helpers

def parse_gltf_settings(source):
    """The GLTF_SETTINGS dict literal of make_creator_kit.py (comments allowed inside)."""
    a = source.index("GLTF_SETTINGS = {")
    start = source.index("{", a)
    end = source.index("\n}", start) + 2
    return ast.literal_eval(source[start:end])


def parse_blender_version(text):
    """(major, minor, patch) from the first line of `blender --version` ("Blender 5.2.2 LTS"), else None."""
    for line in text.splitlines():
        m = re.match(r"\s*Blender\s+(\d+)\.(\d+)(?:\.(\d+))?", line)
        if m:
            return int(m.group(1)), int(m.group(2)), int(m.group(3) or 0)
        if line.strip():
            break
    return None


def find_blender(explicit):
    """Existing blender path or None. explicit (an option) wins, then CTR_BLENDER, then the Windows default."""
    for cand in (explicit, os.environ.get("CTR_BLENDER"),
                 DEFAULT_BLENDER if os.name == "nt" else None):
        if cand and os.path.isfile(cand):
            return cand
    return None


def make_env(work):
    ub = os.path.join(work, "blender-user")
    env = dict(os.environ)
    env.update({
        "BLENDER_USER_RESOURCES": ub,
        "BLENDER_USER_CONFIG": os.path.join(ub, "config"),
        "BLENDER_USER_SCRIPTS": os.path.join(ub, "scripts"),
        "BLENDER_USER_DATAFILES": os.path.join(ub, "datafiles"),
        "BLENDER_USER_EXTENSIONS": os.path.join(ub, "extensions"),
        "TEMP": os.path.join(work, "temp"),
        "TMP": os.path.join(work, "temp"),
        "PYTHONDONTWRITEBYTECODE": "1",
    })
    return env


def prepare_work(work):
    work = os.path.abspath(work)
    os.makedirs(work, exist_ok=True)
    for d in OWN_DIRS:
        shutil.rmtree(os.path.join(work, d), ignore_errors=True)
    for f in ("reopen.py", "standin_root.py", "classic_check.py", "reexport-figure.glb", "reexport-local.glb",
              "settings.json"):
        p = os.path.join(work, f)
        if os.path.isfile(p):
            os.remove(p)
    for d in ("temp", "blender-user", "blender-user/config", "blender-user/scripts",
              "blender-user/datafiles", "blender-user/extensions"):
        os.makedirs(os.path.join(work, d), exist_ok=True)
    return work


def run(cmd, env, timeout=TIMEOUT):
    """(exit code or None on timeout, output text). A timeout kills the whole process tree."""
    p = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        out, _ = p.communicate(timeout=timeout)
        return p.returncode, out.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(p.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            p.kill()
        try:
            out, _ = p.communicate(timeout=30)
        except Exception:
            out = b""
        return None, out.decode("utf-8", "replace")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_paths_in_bytes(data, needles):
    """The needles (str) found in data, case-insensitive, as ASCII and UTF-16LE."""
    low = data.lower()
    found = []
    for n in needles:
        if not n:
            continue
        nl = n.lower()
        if nl.encode("utf-8") in low or nl.encode("utf-16-le") in low:
            found.append(n)
    return found


def path_needles(work, home):
    w = os.path.abspath(work)
    out = [w, w.replace("\\", "/"), w.replace("/", "\\"), "AppData"]
    if home:
        out += [home, home.replace("\\", "/")]
    seen = []
    for n in out:
        if n not in seen:
            seen.append(n)
    return seen


REOPEN_SCRIPT = r'''
import bpy, sys, os, json, hashlib
i = sys.argv.index("--")
blend, settings_json, work = sys.argv[i + 1], sys.argv[i + 2], sys.argv[i + 3]
want = json.load(open(settings_json, encoding="utf-8"))
bpy.ops.wm.open_mainfile(filepath=blend)
problems = []
names = {c.name for c in bpy.data.collections}
for n in ("CreatorKit", "Figure", "Kart", "Guides"):
    if n not in names:
        problems.append("collection %s missing" % n)
if len(bpy.data.actions) != 14:
    problems.append("actions: %d (want 14)" % len(bpy.data.actions))
# the two stored exporters: Figure (the figure alone) and CreatorKit (the local demo with the kit kart)
for tag, cname in (("figure", "Figure"), ("local", "CreatorKit")):
    col = bpy.data.collections.get(cname)
    if col is None or len(col.exporters) != 1:
        problems.append("%s has %s exporters (want 1)" % (cname, "no" if col is None else len(col.exporters)))
        continue
    ex = col.exporters[0]
    p = ex.export_properties
    for k, v in want.items():
        if not hasattr(p, k):
            problems.append("%s: setting %s missing" % (cname, k))
        elif getattr(p, k) != v:
            problems.append("%s: setting %s = %r (want %r)" % (cname, k, getattr(p, k), v))
    print("CHK settings %s %d" % (tag, len(want)))
    if not problems:
        out = os.path.join(work, "reexport-%s.glb" % tag)
        ex.filepath = out
        if os.path.exists(out):
            os.remove(out)
        with bpy.context.temp_override(collection=col):
            bpy.ops.collection.exporter_export(index=0)
        print("CHK sha %s %s" % (tag, hashlib.sha256(open(out, "rb").read()).hexdigest()))
for p_ in problems:
    print("CHK PROBLEM " + p_)
print("CHK actions %d" % len(bpy.data.actions))
sys.exit(0 if not problems else 1)
'''

FIGURE_GLB = "creator-kit-figure.glb"
LOCAL_GLB = "creator-kit-demo-local.glb"
KIT_PROP = "ctr_kit"
KIT_MAT_PREFIX = "ctrkit_"
WHEEL_NAMES = ("wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr")


def glb_json(path):
    with open(path, "rb") as f:
        head = f.read(20)
        if head[:4] != b"glTF":
            raise ValueError("not a GLB: " + path)
        return json.loads(f.read(struct.unpack_from("<I", head, 12)[0]).decode("utf-8"))


def kit_findings(j, local):
    # local: every kit part (mesh node kart_* or wheel_*) and its mesh carry ctr_kit, its materials ctrkit_;
    # figure: no kit part, no ctr_kit extras, no ctrkit_ material. Returns the problems.
    nodes, meshes, mats = j.get("nodes", []), j.get("meshes", []), j.get("materials", [])
    kit = [n for n in nodes if "mesh" in n and (n.get("name") in WHEEL_NAMES or
                                                 (n.get("name") or "").startswith("kart_"))]
    probs = []
    if local:
        if len([n for n in kit if n.get("name") in WHEEL_NAMES]) != 4 or len(kit) < 6:
            probs.append("kit parts: %d (want 4 wheels + kart + steering wheel)" % len(kit))
        for n in kit:
            if KIT_PROP not in (n.get("extras") or {}):
                probs.append("node %s unmarked" % n.get("name"))
            m = meshes[n["mesh"]]
            if KIT_PROP not in (m.get("extras") or {}):
                probs.append("mesh of %s unmarked" % n.get("name"))
            for prim in m.get("primitives", []):
                if "material" in prim and not mats[prim["material"]].get("name", "").startswith(KIT_MAT_PREFIX):
                    probs.append("material of %s unmarked" % n.get("name"))
    else:
        probs += ["kit part %s" % n.get("name") for n in kit]
        probs += ["extras on %s" % x.get("name") for x in nodes + meshes if KIT_PROP in (x.get("extras") or {})]
        probs += ["material %s" % m.get("name") for m in mats if m.get("name", "").startswith(KIT_MAT_PREFIX)]
    return probs


def clips_ok(out):
    # every clip found: the 12 clips ("clips 12/12") and the two optional finish clips ("finish clips 2/2")
    return (re.search(r"\bclips 12/12\b", out) is not None
            and re.search(r"\bfinish clips 2/2\b", out) is not None)


def local_check_ok(out):
    # the local demo, checked as it is packed (--kart-part): 0 errors, exactly one warning - HRG-41, the kit
    # mark (by design: the file holds the kart of the player's game data) - and no note
    warns = [l.strip() for l in out.splitlines() if l.strip().startswith("warning ")]
    return ("0 errors, 1 warnings, 0 notes" in out and len(warns) == 1 and " HRG-41 " in warns[0] + " "
            and clips_ok(out) and "wheels 4/4" in out)


# Writes the stand-in kit with an empty root node "kart" over the kart parts (as an earlier kit-extract did).
STANDIN_ROOT_SCRIPT = r'''
import sys
i = sys.argv.index("--")
templates, out = sys.argv[i + 1], sys.argv[i + 2]
sys.path.insert(0, templates)
import creator_kit_standin
creator_kit_standin.write_standin(out, empty_root=True)
print("CHK standin root ok")
'''


# Reads a template with an imported old character: where the collection Classic hangs, its objects, their
# box in game units (glTF axes), triangles, the distinct vertex colours as sRGB bytes and the packed images.
CLASSIC_CHECK_SCRIPT = r'''
import bpy, sys, json, hashlib
i = sys.argv.index("--")
blend, out = sys.argv[i + 1], sys.argv[i + 2]
bpy.ops.wm.open_mainfile(filepath=blend)
res = {"problems": []}
col = bpy.data.collections.get("Classic")
if col is None:
    res["problems"].append("no collection Classic")
else:
    parents = [c.name for c in bpy.data.collections if col.name in c.children]
    res["parents"] = parents
    res["in_scene_root"] = col.name in bpy.context.scene.collection.children
    lo, hi, tris, colours, images = [1e9] * 3, [-1e9] * 3, 0, set(), []
    objs = sorted(col.objects, key=lambda o: o.name)
    res["objects"] = [o.name for o in objs]
    for o in objs:
        me = o.data
        for v in me.vertices:
            w = o.matrix_world @ v.co
            g = (w.x * 64.0, w.z * 64.0, -w.y * 64.0)
            for k in range(3):
                lo[k] = min(lo[k], g[k]); hi[k] = max(hi[k], g[k])
        tris += sum(len(p.vertices) - 2 for p in me.polygons)
        if len(me.color_attributes):
            a = me.color_attributes[0]
            for d in a.data:
                c = d.color_srgb
                colours.add(tuple(int(round(min(max(x, 0.0), 1.0) * 255)) for x in c[:3]))
        for m in me.materials:
            if m and m.node_tree:
                for n in m.node_tree.nodes:
                    if n.type == "TEX_IMAGE" and n.image and n.image.packed_file:
                        images.append(hashlib.sha256(bytes(n.image.packed_file.data)).hexdigest())
    res.update(box=[lo, hi], triangles=tris, colours=sorted(colours), images=sorted(set(images)),
               materials=sorted({m.name for o in objs for m in o.data.materials if m}))
json.dump(res, open(out, "w"))
print("CHK classic ok")
'''


def glb_box_tris_colours_images(path):
    # box (game units), triangles, distinct sRGB colour bytes, sha256 of the embedded images of a GLB
    with open(path, "rb") as f:
        data = f.read()
    n = struct.unpack_from("<I", data, 12)[0]
    j = json.loads(data[20:20 + n].decode("utf-8"))
    binary = data[20 + n + 8:]
    lo, hi, tris, colours, images = [1e9] * 3, [-1e9] * 3, 0, set(), []

    def acc(i):
        a = j["accessors"][i]
        bv = j["bufferViews"][a["bufferView"]]
        comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        fmt = {5126: "f", 5121: "B", 5123: "H", 5125: "I"}[a["componentType"]]
        size = struct.calcsize(fmt)
        stride = bv.get("byteStride", size * comps)
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        norm = {5121: 255.0, 5123: 65535.0}.get(a["componentType"]) if a.get("normalized") else None
        out = []
        for k in range(a["count"]):
            v = struct.unpack_from("<" + fmt * comps, binary, off + k * stride)
            out.append(tuple(x / norm for x in v) if norm else v)
        return out

    def srgb(c):
        c = min(max(c, 0.0), 1.0)
        return int(round(255.0 * (12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055)))

    for m in j["meshes"]:
        for prim in m["primitives"]:
            for p in acc(prim["attributes"]["POSITION"]):
                for k in range(3):
                    lo[k] = min(lo[k], p[k] * 64.0); hi[k] = max(hi[k], p[k] * 64.0)
            tris += (j["accessors"][prim["indices"]]["count"] if "indices" in prim
                     else j["accessors"][prim["attributes"]["POSITION"]]["count"]) // 3
            if "COLOR_0" in prim["attributes"]:
                for c in acc(prim["attributes"]["COLOR_0"]):
                    colours.add(tuple(srgb(x) for x in c[:3]))
    for im in j.get("images", []):
        bv = j["bufferViews"][im["bufferView"]]
        images.append(hashlib.sha256(binary[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]).hexdigest())
    return (lo, hi), tris, sorted(colours), sorted(set(images))


def info_size(text):
    # "size  112.4 long, 78.2 tall" of rldpack info
    m = re.search(r"size\s+([0-9.]+) long, ([0-9.]+) tall", text)
    return (float(m.group(1)), float(m.group(2))) if m else None


def clips_seen(out):
    m = re.search(r", clips (\d+/\d+)", out)
    f = re.search(r"\bfinish clips (\d+/\d+)", out)
    return "%s + finish %s" % (m.group(1) if m else "?", f.group(1) if f else "?")


def figure_warnings_ok(out):
    # the figure alone has no wheel nodes: hires-check warns HRG-20 (the standard wheels); nothing else
    warns = [l.strip() for l in out.splitlines() if l.strip().startswith("warning ")]
    return "0 errors" in out and all(" HRG-20 " in (w + " ") for w in warns) and clips_ok(out)


# ------------------------------------------------------------------ steps

class Report:
    def __init__(self):
        self.steps = 0
        self.failed = 0

    def ok(self, step, detail=""):
        self.steps += 1
        print("ok   %s %s" % (step, detail), flush=True)

    def fail(self, step, detail=""):
        self.steps += 1
        self.failed += 1
        print("FAIL %s %s" % (step, detail), flush=True)

    def check(self, cond, step, detail_ok="", detail_fail=""):
        (self.ok if cond else self.fail)(step, detail_ok if cond else detail_fail)
        return cond


def tail(text, n=6):
    lines = [l for l in text.splitlines() if l.strip()]
    return " | ".join(lines[-n:])


def run_all(blender, packer, work):
    rep = Report()
    t0 = time.time()
    env = make_env(work)
    r1, r2 = os.path.join(work, "r1"), os.path.join(work, "r2")
    gl, fk = os.path.join(work, "gltf"), os.path.join(work, "fake")

    def gen(out, *extra):
        # --threads 1: the .blend is byte-identical only single-threaded (make_creator_kit.THREADS_NOTE)
        return run([blender, "--threads", "1", "--background", "--factory-startup", "--python-exit-code", "1",
                    "--python", GENERATOR, "--", "--kit", out] + list(extra), env)

    def hires(glb, mode, *extra):
        return run(packer + ["hires-check", glb, "--vertex-colors", mode] + list(extra), env)

    # 1 generate
    code, out = gen(r1, "--stand-in")
    rep.check(code == 0 and "creator kit: ok" in out, "1 generate",
              "exit 0, 'creator kit: ok'", "exit %s: %s" % (code, tail(out)))
    fig1 = os.path.join(r1, FIGURE_GLB)
    loc1 = os.path.join(r1, LOCAL_GLB)
    blend1 = os.path.join(r1, "creator-kit.blend")

    # 2 again
    code, out = gen(r2, "--stand-in")
    if code != 0:
        rep.fail("2 repeat", "exit %s: %s" % (code, tail(out)))
    else:
        diffs = []
        names = sorted(set(os.listdir(r1)) | set(os.listdir(r2)))
        cmp_names = [n for n in names
                     if n in ("creator-kit.blend", FIGURE_GLB, LOCAL_GLB) or n.startswith("kit-")]
        for n in cmp_names:
            a, b = os.path.join(r1, n), os.path.join(r2, n)
            if not (os.path.isfile(a) and os.path.isfile(b)) or sha256(a) != sha256(b):
                diffs.append(n)
        need = ("creator-kit.blend", FIGURE_GLB, LOCAL_GLB)
        rep.check(not diffs and all(n in cmp_names for n in need),
                  "2 repeat", "r1 = r2 byte-identical (%d files)" % len(cmp_names),
                  "differ: %s" % ", ".join(diffs or ["blend/glb missing"]))

    # 3 reopen: both stored exporters give the same files again
    try:
        src = open(GENERATOR, encoding="utf-8").read()
        settings = parse_gltf_settings(src)
        sj = os.path.join(work, "settings.json")
        with open(sj, "w", encoding="utf-8") as f:
            json.dump(settings, f)
        rs = os.path.join(work, "reopen.py")
        with open(rs, "w", encoding="utf-8") as f:
            f.write(REOPEN_SCRIPT)
        code, out = run([blender, "--background", "--factory-startup", "--python-exit-code", "1",
                         "--python", rs, "--", blend1, sj, work], env)
        shas = dict(re.findall(r"CHK sha (figure|local) ([0-9a-f]{64})", out))
        probs = [l for l in out.splitlines() if "CHK PROBLEM" in l]
        if code != 0 or probs or len(shas) != 2:
            rep.fail("3 reopen", "exit %s %s" % (code, "; ".join(probs) or tail(out)))
        else:
            same = (os.path.isfile(fig1) and shas["figure"] == sha256(fig1) and
                    os.path.isfile(loc1) and shas["local"] == sha256(loc1))
            rep.check(same, "3 reopen", "2 exporters x %d settings, 4 collections, 14 actions, re-exports same sha256"
                      % len(settings), "a re-export differs from the generated file")
    except Exception as e:
        rep.fail("3 reopen", repr(e))

    # 4 hires-check of the local demo (with the kit kart), as it is packed: --kart-part
    code, out = hires(loc1, "psx", "--kart-part")
    rep.check(code == 0 and local_check_ok(out),
              "4 hires-check local", "--kart-part: 0 errors, only HRG-41 (the kit mark), 0 notes, clips %s, wheels 4/4" % clips_seen(out),
              "exit %s: %s" % (code, tail(out)))

    # 5 hires-check of the figure alone: no wheel nodes, so only HRG-20 (standard wheels) may warn
    code, out = hires(fig1, "psx")
    rep.check(code == 0 and figure_warnings_ok(out) and "wheels 0/4" in out,
              "5 hires-check figure", "0 errors, clips %s, wheels 0/4, only HRG-20: %s" % (clips_seen(out), tail(out, 1)),
              "exit %s: %s" % (code, tail(out)))

    # 6 the kit marker: everything from the kit folder marked in the local demo, nothing of it in the figure
    try:
        p_loc = kit_findings(glb_json(loc1), True)
        p_fig = kit_findings(glb_json(fig1), False)
        rep.check(not p_loc and not p_fig, "6 kit marker",
                  "local: every kit node, mesh, material marked; figure: no kit part, no marker",
                  "; ".join(["local " + x for x in p_loc] + ["figure " + x for x in p_fig])[:400])
    except Exception as e:
        rep.fail("6 kit marker", repr(e))

    # 7 make-char --check (the classic fallback) of the local demo
    code, out = run(packer + ["make-char", "--check", "--model", loc1, "--reduce", "auto", "--rig", "bind-pose",
                              "--wheels", "off", "--name", "TEST", "--template", "0"], env)
    errs = [l for l in out.splitlines() if l.startswith("ERROR")]
    rep.check(code == 0 and not errs, "7 make-char", "exit 0, no ERROR",
              "exit %s: %s" % (code, "; ".join(errs[:3]) or tail(out)))

    # 8 measure (the local demo: kart, wheels, hands on the steering wheel)
    code, out = run([sys.executable, "-I", MEASURE, "measure", loc1, "--kit-info",
                     os.path.join(r1, "kit-info.json"), "--kart", os.path.join(r1, "kit-kart.glb"),
                     "--vcol", "psx"], env)
    rep.check(code == 0 and re.search(r"fail\s+0\b", out) is not None, "8 measure", "fail 0",
              "exit %s: %s" % (code, tail(out)))

    # 9 gltf
    code, out = gen(gl, "--stand-in", "--vcol", "gltf")
    if code != 0:
        rep.fail("9 gltf", "generate exit %s: %s" % (code, tail(out)))
    else:
        code, out = hires(os.path.join(gl, LOCAL_GLB), "gltf", "--kart-part")
        code2, out2 = hires(os.path.join(gl, FIGURE_GLB), "gltf")
        rep.check(code == 0 and local_check_ok(out) and code2 == 0 and figure_warnings_ok(out2),
                  "9 gltf", "local (--kart-part) only HRG-41, 0 notes; figure only HRG-20",
                  "hires-check exit %s/%s: %s | %s" % (code, code2, tail(out, 2), tail(out2, 2)))

    # 10 version
    code, out = gen(fk, "--stand-in", "--test-fake-version", "4.5.0")
    rep.check(code == 3 and "How to fix: install Blender 5.2" in out, "10 version",
              "exit 3, 'How to fix: install Blender 5.2'", "exit %s: %s" % (code, tail(out)))

    # 11 no path
    try:
        data = open(blend1, "rb").read()
        found = find_paths_in_bytes(data, path_needles(work, os.path.expanduser("~")))
        rep.check(not found, "11 no path", "no work/AppData/home path in creator-kit.blend",
                  "found: %s" % ", ".join(found))
    except Exception as e:
        rep.fail("11 no path", repr(e))

    # 12 a kit whose kart parts hang under an empty root node "kart": the generator takes it
    try:
        rt = os.path.join(work, "root")
        rs = os.path.join(work, "standin_root.py")
        with open(rs, "w", encoding="utf-8") as f:
            f.write(STANDIN_ROOT_SCRIPT)
        code, out = run([blender, "--threads", "1", "--background", "--factory-startup", "--python-exit-code", "1",
                         "--python", rs, "--", os.path.dirname(GENERATOR), rt], env)
        if code != 0 or "CHK standin root ok" not in out:
            rep.fail("12 empty root", "stand-in exit %s: %s" % (code, tail(out)))
        else:
            code, out = gen(rt)
            loc = os.path.join(rt, LOCAL_GLB)
            ok = code == 0 and "creator kit: ok" in out and os.path.isfile(loc)
            probs = kit_findings(glb_json(loc), True) if ok else []
            code2, out2 = hires(loc, "psx", "--kart-part") if ok else (1, "")
            rep.check(ok and not probs and code2 == 0 and local_check_ok(out2), "12 empty root",
                      "kit-kart.glb with an empty root 'kart': exit 0, kit parts marked, --kart-part only HRG-41",
                      "exit %s: %s %s %s" % (code, tail(out, 3), "; ".join(probs)[:200], tail(out2, 2)))
    except Exception as e:
        rep.fail("12 empty root", repr(e))

    # 13 an old character as a starting point (classic and native model): a .rldchar made here from the
    # old template (code-made, CC0) by make-char, exported by char-export, imported by --classic
    try:
        ck = os.path.join(work, "classic")
        os.makedirs(ck, exist_ok=True)
        old_glb = os.path.join(ck, "old.glb")
        code, out = run([blender, "--threads", "1", "--background", "--factory-startup", "--python-exit-code", "1",
                         "--python", OLD_TEMPLATE, "--", "--blend", os.path.join(ck, "old.blend"), "--glb", old_glb], env)
        if code != 0 or not os.path.isfile(old_glb):
            raise RuntimeError("old template exit %s: %s" % (code, tail(out, 2)))
        cs = os.path.join(work, "classic_check.py")
        with open(cs, "w", encoding="utf-8") as f:
            f.write(CLASSIC_CHECK_SCRIPT)
        for kind, extra in (("classic", []), ("native", ["--native-model", "on", "--wheels", "off"])):
            rc = os.path.join(ck, "old-%s.rldchar" % kind)
            code, out = run(packer + ["make-char", "--model", old_glb, "--name", "TEST", "--template", "0",
                                      "--out", rc] + extra, env)
            if code != 0 or not os.path.isfile(rc):
                raise RuntimeError("make-char %s exit %s: %s" % (kind, code, tail(out, 2)))
            exp = os.path.join(ck, "old-%s.glb" % kind)
            code, out = run(packer + ["char-export", rc, "-o", exp, "--source", kind], env)
            if code != 0 or not os.path.isfile(exp):
                raise RuntimeError("char-export %s exit %s: %s" % (kind, code, tail(out, 2)))
            code, out = run(packer + ["info", rc], env)
            size = info_size(out)
            kit = os.path.join(ck, "kit-" + kind)
            code, out = gen(kit, "--stand-in", "--classic", exp)
            if code != 0:
                raise RuntimeError("generate --classic (%s) exit %s: %s" % (kind, code, tail(out, 3)))
            res_path = os.path.join(ck, "check-%s.json" % kind)
            code, out = run([blender, "--background", "--factory-startup", "--python-exit-code", "1",
                             "--python", cs, "--", os.path.join(kit, "creator-kit.blend"), res_path], env)
            if code != 0 or not os.path.isfile(res_path):
                raise RuntimeError("check script (%s) exit %s: %s" % (kind, code, tail(out, 3)))
            got = json.load(open(res_path, encoding="utf-8"))
            (glo, ghi), gtris, gcols, gimgs = glb_box_tris_colours_images(exp)
            blo, bhi = got["box"]
            box_dev = max(abs(a - b) for a, b in zip(list(blo) + list(bhi), list(glo) + list(ghi)))
            length, tall = ghi[2] - glo[2], ghi[1] - glo[1]
            probs = list(got["problems"])
            if not got.get("in_scene_root") or got.get("parents"):
                probs.append("Classic not at the top of the scene (parents %s)" % got.get("parents"))
            if not got["objects"] or not all(o.startswith("classic_") for o in got["objects"]):
                probs.append("objects %s" % got["objects"])
            if box_dev > 0.5:
                probs.append("box differs from the GLB by %.3f u" % box_dev)
            if size is None or abs(length - size[0]) > max(0.5, 0.01 * size[0]) or abs(tall - size[1]) > max(0.5, 0.01 * size[1]):
                probs.append("size %.2f long %.2f tall, rldpack info says %s" % (length, tall, size))
            if not (ghi[2] > -glo[2] and abs(ghi[2] - 58.3) < 1.5):
                probs.append("the nose is not at +z (z %.2f..%.2f)" % (glo[2], ghi[2]))
            if got["triangles"] != gtris:
                probs.append("triangles %d, the GLB %d" % (got["triangles"], gtris))
            if [tuple(c) for c in got["colours"]] != [tuple(c) for c in gcols]:
                probs.append("vertex colours differ (%d against %d distinct)" % (len(got["colours"]), len(gcols)))
            if got["images"] != gimgs or (kind == "native" and not gimgs):
                probs.append("images differ or missing (%d against %d)" % (len(got["images"]), len(gimgs)))
            if not all(m.startswith("classic_") and m.endswith(" [vcol psx]") for m in got["materials"]):
                probs.append("materials %s" % got["materials"])
            same = all(sha256(os.path.join(kit, f)) == sha256(os.path.join(r1, f)) for f in (FIGURE_GLB, LOCAL_GLB))
            if not same:
                probs.append("an export differs from the run without --classic")
            rep.check(not probs, "13 classic %s" % kind,
                      "%d triangles, %d colours, %d images, size %.1f x %.1f (info %s), box = GLB, not exported"
                      % (gtris, len(gcols), len(gimgs), length, tall, size), "; ".join(probs)[:400])
    except Exception as e:
        rep.fail("13 classic", repr(e)[:400])

    print("creator-kit-test: runtime %.0f s" % (time.time() - t0))
    print("creator-kit-test: %d steps, %d failed" % (rep.steps, rep.failed))
    return 0 if rep.failed == 0 else 1


# ------------------------------------------------------------------ selftest

def selftest():
    n = [0, 0]

    def chk(cond, what):
        n[0] += 1
        if not cond:
            n[1] += 1
            print("selftest FAIL: " + what)

    # GLTF_SETTINGS parsing, on the real file and on a synthetic text with comments
    try:
        s = parse_gltf_settings(open(GENERATOR, encoding="utf-8").read())
        chk(isinstance(s, dict) and s.get("export_format") == "GLB" and len(s) > 30, "real GLTF_SETTINGS")
        chk(s.get("export_apply") is False and s.get("export_influence_nb") == 4, "real GLTF_SETTINGS values")
    except Exception as e:
        chk(False, "real GLTF_SETTINGS: %r" % e)
    s = parse_gltf_settings('X = 1\nGLTF_SETTINGS = {\n    # c\n    "a": True,  # x\n    "b": "Q",\n}\nY = {}\n')
    chk(s == {"a": True, "b": "Q"}, "synthetic GLTF_SETTINGS")

    # version strings
    chk(parse_blender_version("Blender 5.2.2 LTS\n\tbuild date: x") == (5, 2, 2), "version 5.2.2 LTS")
    chk(parse_blender_version("Blender 5.2\nfoo") == (5, 2, 0), "version 5.2")
    chk(parse_blender_version("Blender 4.5.1 LTS") == (4, 5, 1), "version 4.5.1")
    chk(parse_blender_version("") is None and parse_blender_version("hello 5.2") is None, "version garbage")

    # SKIP path
    chk(find_blender(os.path.join(os.getcwd(), "no-such-dir", "blender.exe")) in (None, os.environ.get("CTR_BLENDER"))
        or os.path.isfile(find_blender("no-such") or ""), "find_blender missing")
    old = os.environ.pop("CTR_BLENDER", None)
    try:
        if not os.path.isfile(DEFAULT_BLENDER):
            chk(find_blender("no-such-blender.exe") is None, "find_blender none")
    finally:
        if old is not None:
            os.environ["CTR_BLENDER"] = old
    code = main(["--rldpack", "x", "--work", os.path.join(REPO, "no-such-work-dir-selftest"),
                 "--blender", os.path.join(REPO, "no-such-blender.exe"), "--_selftest-skip"])
    chk(code == SKIP_CODE, "main skip exit 77 (got %s)" % code)

    # path search
    # Made-up folders, the drive letters joined at run time.
    needles = path_needles("C" + ":" + r"\Work\Dir", "D" + ":" + r"\Home\Zed")
    chk(find_paths_in_bytes(b"xx c" + b":/work/dir/foo", needles) != [], "path search forward slash, case")
    chk(find_paths_in_bytes(b"xx C:\\WORK\\DIR\\foo", needles) != [], "path search backslash, case")
    chk(find_paths_in_bytes(b"a\x00p\x00p\x00d\x00a\x00t\x00a\x00".replace(b"a\x00p\x00p\x00d\x00a\x00t\x00a\x00",
                                                                           "AppData".encode("utf-16-le")), needles) != [],
        "path search utf-16")
    chk(find_paths_in_bytes(b"D" + b":/Home/Zed/x", needles) != [], "path search home")
    chk(find_paths_in_bytes(b"nothing here /tmp/blender", needles) == [], "path search clean")

    # hires-check summaries: the 12 clips and the two optional finish clips
    full = ("x.glb: 0 errors, 1 warnings, 0 notes, clips 12/12, wheels 4/4, skin 28 joints\n"
            "x.glb: finish clips 2/2 (optional: finish_win yes, finish_lose yes; a missing one plays ...)\n")
    chk(clips_ok(full) and clips_seen(full) == "12/12 + finish 2/2", "clips 12/12 + finish clips 2/2")
    chk(not clips_ok(full.replace("finish clips 2/2", "finish clips 1/2")), "a finish clip missing")
    chk(not clips_ok(full.replace("clips 12/12", "clips 11/12")), "a clip missing")
    chk(not clips_ok("x.glb: 0 errors, 0 warnings, 0 notes, clips 12/12, wheels 4/4\n"), "no finish line")

    print("creator-kit-test selftest: %d checks, %d failures" % tuple(n))
    return 0 if n[1] == 0 else 1


# ------------------------------------------------------------------ main

def main(argv):
    if argv[:1] == ["selftest"]:
        return selftest()
    opt = {"rldpack": None, "studio": None, "work": None, "blender": None}
    i = 0
    skip_only = False
    while i < len(argv):
        a = argv[i]
        if a == "--_selftest-skip":
            skip_only = True
            i += 1
            continue
        key = {"--rldpack": "rldpack", "--rldpack-studio": "studio", "--work": "work", "--blender": "blender"}.get(a)
        if key is None or i + 1 >= len(argv):
            print("usage: creator_kit_test.py --rldpack <rldpack.exe> | --rldpack-studio <ReloadStudio.exe> "
                  "--work <dir> [--blender <blender.exe>]  |  selftest")
            return 2
        opt[key] = argv[i + 1]
        i += 2
    if not opt["work"]:
        print("creator-kit-test: --work is required")
        return 2
    work = os.path.abspath(opt["work"])

    blender = find_blender(opt["blender"]) if not (opt["blender"] and not os.path.isfile(opt["blender"])) else None
    if blender:
        os.makedirs(os.path.join(work, "temp"), exist_ok=True)
        code, out = run([blender, "--version"], make_env(work), timeout=60)
        ver = parse_blender_version(out) if code == 0 else None
        if not ver or ver[:2] != (5, 2):
            blender = None
    if not blender:
        print("creator-kit-test: SKIP (Blender 5.2 not found)")
        return SKIP_CODE
    if skip_only:
        return 0

    if opt["studio"]:
        packer = [opt["studio"], "--rldpack"]
    elif opt["rldpack"]:
        packer = [opt["rldpack"]]
    else:
        print("creator-kit-test: --rldpack or --rldpack-studio is required")
        return 2
    work = prepare_work(work)
    return run_all(blender, packer, work)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
