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
4 hires-check of creator-kit-demo-local.glb (0 warnings), 5 hires-check of creator-kit-figure.glb (no wheel
nodes: only HRG-20, the standard wheels), 6 the kit marker (ctr_kit extras, ctrkit_ materials) on every kit
part of the local demo and on nothing of the figure, 7 make-char --check, 8 creator_kit_measure, 9 --vcol
gltf, 10 wrong-version exit 3, 11 no path in the .blend.
Last line: "creator-kit-test: <n> steps, <f> failed"; exit 0 when f = 0, else 1.

Every child process gets the Blender user folders, TEMP and TMP below <work>. Only the driver's own
subfolders of <work> (r1, r2, gltf, fake, temp) are emptied. Standard library only.
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
MEASURE = os.path.join(REPO, "tools", "creator_kit_measure.py")
DEFAULT_BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
TIMEOUT = 300
SKIP_CODE = 77
OWN_DIRS = ("r1", "r2", "gltf", "fake", "temp")


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
    for f in ("reopen.py", "reexport-figure.glb", "reexport-local.glb", "settings.json"):
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
if len(bpy.data.actions) != 12:
    problems.append("actions: %d (want 12)" % len(bpy.data.actions))
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


def figure_warnings_ok(out):
    # the figure alone has no wheel nodes: hires-check warns HRG-20 (the standard wheels); nothing else
    warns = [l.strip() for l in out.splitlines() if l.strip().startswith("warning ")]
    return "0 errors" in out and all(" HRG-20 " in (w + " ") for w in warns) and "clips 12/12" in out


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

    def hires(glb, mode):
        return run(packer + ["hires-check", glb, "--vertex-colors", mode], env)

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
            rep.check(same, "3 reopen", "2 exporters x %d settings, 4 collections, 12 actions, re-exports same sha256"
                      % len(settings), "a re-export differs from the generated file")
    except Exception as e:
        rep.fail("3 reopen", repr(e))

    # 4 hires-check of the local demo (with the kit kart)
    code, out = hires(loc1, "psx")
    rep.check(code == 0 and "0 errors, 0 warnings" in out and "clips 12/12" in out and "wheels 4/4" in out,
              "4 hires-check local", "0 errors, 0 warnings, clips 12/12, wheels 4/4",
              "exit %s: %s" % (code, tail(out)))

    # 5 hires-check of the figure alone: no wheel nodes, so only HRG-20 (standard wheels) may warn
    code, out = hires(fig1, "psx")
    rep.check(code == 0 and figure_warnings_ok(out) and "wheels 0/4" in out,
              "5 hires-check figure", "0 errors, clips 12/12, wheels 0/4, only HRG-20: %s" % tail(out, 1),
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
        code, out = hires(os.path.join(gl, LOCAL_GLB), "gltf")
        code2, out2 = hires(os.path.join(gl, FIGURE_GLB), "gltf")
        rep.check(code == 0 and "0 errors, 0 warnings" in out and code2 == 0 and figure_warnings_ok(out2),
                  "9 gltf", "local 0 errors, 0 warnings; figure only HRG-20",
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
    needles = path_needles(r"C:\Work\Dir", r"D:\Home\Zed")
    chk(find_paths_in_bytes(b"xx c:/work/dir/foo", needles) != [], "path search forward slash, case")
    chk(find_paths_in_bytes(b"xx C:\\WORK\\DIR\\foo", needles) != [], "path search backslash, case")
    chk(find_paths_in_bytes(b"a\x00p\x00p\x00d\x00a\x00t\x00a\x00".replace(b"a\x00p\x00p\x00d\x00a\x00t\x00a\x00",
                                                                           "AppData".encode("utf-16-le")), needles) != [],
        "path search utf-16")
    chk(find_paths_in_bytes(b"D:/Home/Zed/x", needles) != [], "path search home")
    chk(find_paths_in_bytes(b"nothing here /tmp/blender", needles) == [], "path search clean")

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
