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

Steps: 1 generate, 2 byte-identical second run, 3 reopen + re-export of the stored exporter, 4 hires-check,
5 make-char --check, 6 creator_kit_measure, 7 --vcol gltf, 8 wrong-version exit 3, 9 no path in the .blend.
Last line: "creator-kit-test: <n> steps, <f> failed"; exit 0 when f = 0, else 1.

Every child process gets the Blender user folders, TEMP and TMP below <work>. Only the driver's own
subfolders of <work> (r1, r2, gltf, fake, temp) are emptied. Standard library only.
"""
import ast
import hashlib
import json
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
    for f in ("reopen.py", "reexport.glb", "settings.json"):
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
blend, settings_json, reexport = sys.argv[i + 1], sys.argv[i + 2], sys.argv[i + 3]
want = json.load(open(settings_json, encoding="utf-8"))
bpy.ops.wm.open_mainfile(filepath=blend)
problems = []
names = {c.name for c in bpy.data.collections}
for n in ("CreatorKit", "Figure", "Kart", "Guides"):
    if n not in names:
        problems.append("collection %s missing" % n)
if len(bpy.data.actions) != 12:
    problems.append("actions: %d (want 12)" % len(bpy.data.actions))
kc = bpy.data.collections.get("CreatorKit")
if kc is None or len(kc.exporters) < 1:
    problems.append("CreatorKit has no exporter")
else:
    ex = kc.exporters[0]
    p = ex.export_properties
    for k, v in want.items():
        if not hasattr(p, k):
            problems.append("setting %s missing" % k)
        elif getattr(p, k) != v:
            problems.append("setting %s = %r (want %r)" % (k, getattr(p, k), v))
    print("CHK settings %d" % len(want))
    if not problems:
        ex.filepath = reexport
        if os.path.exists(reexport):
            os.remove(reexport)
        with bpy.context.temp_override(collection=kc):
            bpy.ops.collection.exporter_export(index=0)
        print("CHK sha " + hashlib.sha256(open(reexport, "rb").read()).hexdigest())
for p_ in problems:
    print("CHK PROBLEM " + p_)
print("CHK actions %d" % len(bpy.data.actions))
sys.exit(0 if not problems else 1)
'''


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
    glb1 = os.path.join(r1, "creator-kit-demo.glb")
    blend1 = os.path.join(r1, "creator-kit.blend")

    # 2 again
    code, out = gen(r2, "--stand-in")
    if code != 0:
        rep.fail("2 repeat", "exit %s: %s" % (code, tail(out)))
    else:
        diffs = []
        names = sorted(set(os.listdir(r1)) | set(os.listdir(r2)))
        cmp_names = [n for n in names
                     if n in ("creator-kit.blend", "creator-kit-demo.glb") or n.startswith("kit-")]
        for n in cmp_names:
            a, b = os.path.join(r1, n), os.path.join(r2, n)
            if not (os.path.isfile(a) and os.path.isfile(b)) or sha256(a) != sha256(b):
                diffs.append(n)
        rep.check(not diffs and "creator-kit.blend" in cmp_names and "creator-kit-demo.glb" in cmp_names,
                  "2 repeat", "r1 = r2 byte-identical (%d files)" % len(cmp_names),
                  "differ: %s" % ", ".join(diffs or ["blend/glb missing"]))

    # 3 reopen
    try:
        src = open(GENERATOR, encoding="utf-8").read()
        settings = parse_gltf_settings(src)
        sj = os.path.join(work, "settings.json")
        with open(sj, "w", encoding="utf-8") as f:
            json.dump(settings, f)
        rs = os.path.join(work, "reopen.py")
        with open(rs, "w", encoding="utf-8") as f:
            f.write(REOPEN_SCRIPT)
        reexp = os.path.join(work, "reexport.glb")
        code, out = run([blender, "--background", "--factory-startup", "--python-exit-code", "1",
                         "--python", rs, "--", blend1, sj, reexp], env)
        m = re.search(r"CHK sha ([0-9a-f]{64})", out)
        probs = [l for l in out.splitlines() if "CHK PROBLEM" in l]
        if code != 0 or probs or not m:
            rep.fail("3 reopen", "exit %s %s" % (code, "; ".join(probs) or tail(out)))
        else:
            same = os.path.isfile(glb1) and m.group(1) == sha256(glb1)
            rep.check(same, "3 reopen", "%d settings, 4 collections, 12 actions, re-export same sha256"
                      % len(settings), "re-export differs from creator-kit-demo.glb")
    except Exception as e:
        rep.fail("3 reopen", repr(e))

    # 4 hires-check
    code, out = hires(glb1, "psx")
    rep.check(code == 0 and "0 errors, 0 warnings" in out and "clips 12/12" in out and "wheels 4/4" in out,
              "4 hires-check", "0 errors, 0 warnings, clips 12/12, wheels 4/4",
              "exit %s: %s" % (code, tail(out)))

    # 5 make-char --check
    code, out = run(packer + ["make-char", "--check", "--model", glb1, "--reduce", "auto", "--rig", "bind-pose",
                              "--wheels", "off", "--name", "TEST", "--template", "0"], env)
    errs = [l for l in out.splitlines() if l.startswith("ERROR")]
    rep.check(code == 0 and not errs, "5 make-char", "exit 0, no ERROR",
              "exit %s: %s" % (code, "; ".join(errs[:3]) or tail(out)))

    # 6 measure
    code, out = run([sys.executable, "-I", MEASURE, "measure", glb1, "--kit-info",
                     os.path.join(r1, "kit-info.json"), "--kart", os.path.join(r1, "kit-kart.glb"),
                     "--vcol", "psx"], env)
    rep.check(code == 0 and re.search(r"fail\s+0\b", out) is not None, "6 measure", "fail 0",
              "exit %s: %s" % (code, tail(out)))

    # 7 gltf
    code, out = gen(gl, "--stand-in", "--vcol", "gltf")
    if code != 0:
        rep.fail("7 gltf", "generate exit %s: %s" % (code, tail(out)))
    else:
        code, out = hires(os.path.join(gl, "creator-kit-demo.glb"), "gltf")
        rep.check(code == 0 and "0 errors, 0 warnings" in out, "7 gltf", "0 errors, 0 warnings",
                  "hires-check exit %s: %s" % (code, tail(out)))

    # 8 version
    code, out = gen(fk, "--stand-in", "--test-fake-version", "4.5.0")
    rep.check(code == 3 and "How to fix: install Blender 5.2" in out, "8 version",
              "exit 3, 'How to fix: install Blender 5.2'", "exit %s: %s" % (code, tail(out)))

    # 9 no path
    try:
        data = open(blend1, "rb").read()
        found = find_paths_in_bytes(data, path_needles(work, os.path.expanduser("~")))
        rep.check(not found, "9 no path", "no work/AppData/home path in creator-kit.blend",
                  "found: %s" % ", ".join(found))
    except Exception as e:
        rep.fail("9 no path", repr(e))

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
