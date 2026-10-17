#!/usr/bin/env python3
"""Prepare custom-character folders (*.rldchar + chars.txt) for hi-res measuring runs.

Modes:
  budget   one classic character per hi-res glb variant (triangle / texture budgets)
  podium   one character per template 0..7, all carrying the same hi-res glb
  selftest run the tool against a fake packer

Roster order = file names sorted byte-wise (C strcmp / qsort), numbered 1..n.
The game's --dev-char-seats cycle takes roster file (load-1) % n.
Python standard library only.
"""
import argparse
import hashlib
import os
import shlex
import shutil
import subprocess
import sys
import tempfile

# work files below TEMP (the measuring shells point TEMP into the project folder)
WORK_ROOT = os.path.join(tempfile.gettempdir(), "hires_chars_dir")
WORK_DIR = os.path.join(WORK_ROOT, "work")
STANDARD_VARIANTS = ["t10k-x2048", "t20k-x2048", "t40k-x2048", "t80k-x2048",
                     "t160k-x2048", "t320k-x2048", "t40k-x512", "t40k-x1024"]
DEFAULT_ADD = "{exe} hires-add {base} {glb} -o {out}"
DEFAULT_CHECK = "{exe} hires-check {file}"
DEFAULT_VERIFY = "{exe} verify {file}"
DEFAULT_MAKE = ('{exe} make-char --model {model} --name {name} '
                '--template {template} --out {out}')


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def exe_argv(exe):
    # a .py "executable" (used by the selftest fake packer) runs through python
    if exe.lower().endswith(".py"):
        return [sys.executable, exe]
    return [exe]


def build_cmd(tmpl, exe, **fields):
    """Build an argv list from a template; placeholders are replaced per token,
    so paths with spaces stay single arguments."""
    out = []
    for tok in shlex.split(tmpl, posix=False):
        if len(tok) >= 2 and tok[0] == tok[-1] and tok[0] in "\"'":
            tok = tok[1:-1]
        if tok == "{exe}":
            out.extend(exe_argv(exe))
            continue
        for k, v in fields.items():
            tok = tok.replace("{" + k + "}", str(v))
        out.append(tok)
    return out


def run(argv):
    p = subprocess.run(argv, capture_output=True, text=True, errors="replace")
    return p.returncode, (p.stdout or "") + (p.stderr or "")


class Failure(Exception):
    pass


def run_ok(argv):
    code, text = run(argv)
    if code != 0:
        raise Failure("command failed (exit %d): %s\n%s" % (code, " ".join(argv), text))
    return text


def prepare_out(out, clean):
    if os.path.isdir(out):
        names = os.listdir(out)
        if names and not clean:
            print("error: --out %s is not empty (use --clean)" % out, file=sys.stderr)
            return False
        for n in names:
            if n == "chars.txt" or n.lower().endswith(".rldchar"):
                os.remove(os.path.join(out, n))
    elif os.path.exists(out):
        print("error: --out %s exists and is not a folder" % out, file=sys.stderr)
        return False
    else:
        os.makedirs(out)
    return True


def fresh_work():
    if os.path.isdir(WORK_DIR):
        shutil.rmtree(WORK_DIR)
    os.makedirs(WORK_DIR)


def write_chars(out, rows, extra_cols):
    """rows: list of dict(name, glb_sha, plus extra cols). Writes chars.txt, prints roster."""
    rows = sorted(rows, key=lambda r: r["name"].encode("utf-8"))
    lines = ["# name\tbytes\tsha256\tglb_sha256\troster_no" +
             "".join("\t" + c for c in extra_cols)]
    for i, r in enumerate(rows, 1):
        p = os.path.join(out, r["name"])
        lines.append("\t".join([r["name"], str(os.path.getsize(p)), sha256(p),
                                r["glb_sha"], str(i)] + [str(r[c]) for c in extra_cols]))
    with open(os.path.join(out, "chars.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("roster order (game: --dev-char-seats takes file (load-1) %% %d):" % len(rows))
    for i, r in enumerate(rows, 1):
        print("  %d  %s" % (i, r["name"]))
    return rows


def check_file(args, path):
    run_ok(build_cmd(args.check_template, args.rldpack, file=path))
    run_ok(build_cmd(DEFAULT_VERIFY, args.rldpack, file=path))


def mode_budget(args):
    if args.variants == "standard":
        variants = STANDARD_VARIANTS
    else:
        variants = [v.strip() for v in args.variants.split(",") if v.strip()]
    base = args.base or os.path.join(args.src, "testdriver-classic.rldchar")
    if not prepare_out(args.out, args.clean):
        return 2
    rows = []
    try:
        for v in variants:
            glb = os.path.join(args.src, "testdriver-%s.glb" % v)
            name = "testdriver-%s.rldchar" % v
            out_file = os.path.join(args.out, name)
            run_ok(build_cmd(args.command_template, args.rldpack,
                             base=base, glb=glb, out=out_file))
            check_file(args, out_file)
            rows.append({"name": name, "glb_sha": sha256(glb)})
            print("ok  %s" % name)
    except (Failure, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    write_chars(args.out, rows, [])
    return 0


def mode_podium(args):
    templates = [int(t) for t in args.templates.split(",") if t.strip() != ""]
    glb = os.path.join(args.src, args.glb)
    model = os.path.join(args.src, "testdriver-classic.glb")
    if not prepare_out(args.out, args.clean):
        return 2
    fresh_work()
    rows = []
    seat_names = []
    try:
        glb_sha = sha256(glb)
        for t in templates:
            tmp_char = os.path.join(WORK_DIR, "testdriver-classic-tpl%d.rldchar" % t)
            name = "testdriver-tpl%d.rldchar" % t
            out_file = os.path.join(args.out, name)
            run_ok(build_cmd(DEFAULT_MAKE, args.rldpack, model=model,
                             name='HIRES TEST %d' % t, template=t, out=tmp_char))
            run_ok(build_cmd(args.command_template, args.rldpack,
                             base=tmp_char, glb=glb, out=out_file))
            check_file(args, out_file)
            rows.append({"name": name, "glb_sha": glb_sha, "template": t})
            seat_names.append(name)
            print("ok  %s" % name)
    except (Failure, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    write_chars(args.out, rows, ["template"])
    print("--dev-char-seat-files " + ",".join(seat_names))
    print("note: pass --driver %d (template of seat 0 file %s)" % (templates[0], seat_names[0]))
    return 0


# ---------------------------------------------------------------- selftest

FAKE_SRC = r'''
import os, sys
a = sys.argv[1:]
cmd = a[0] if a else ""
def opt(k):
    return a[a.index(k) + 1]
if cmd == "hires-add":
    if os.environ.get("FAKE_FAIL_ADD"):
        print("fake hires-add failure"); sys.exit(1)
    data = open(a[1], "rb").read() + open(a[2], "rb").read()
    open(opt("-o"), "wb").write(data)
elif cmd == "make-char":
    open(opt("--out"), "wb").write(("template=%s\n" % opt("--template")).encode())
elif cmd in ("hires-check", "verify"):
    if not os.path.exists(a[1]):
        sys.exit(1)
else:
    sys.exit(2)
'''


def selftest():
    fails = []
    n = [0]

    def check(cond, what):
        n[0] += 1
        if not cond:
            fails.append(what)
            print("FAIL: " + what)

    fake_dir = os.path.join(WORK_ROOT, "fake")
    if os.path.isdir(fake_dir):
        shutil.rmtree(fake_dir)
    src = os.path.join(fake_dir, "src")
    os.makedirs(src)
    fake = os.path.join(fake_dir, "fake_packer.py")
    with open(fake, "w") as f:
        f.write(FAKE_SRC)
    for v in STANDARD_VARIANTS:
        with open(os.path.join(src, "testdriver-%s.glb" % v), "wb") as f:
            f.write(b"GLB-" + v.encode())
    with open(os.path.join(src, "testdriver-classic.glb"), "wb") as f:
        f.write(b"GLB-classic")
    with open(os.path.join(src, "testdriver-classic.rldchar"), "wb") as f:
        f.write(b"BASE")
    me = os.path.abspath(__file__)

    def tool(*a, env=None):
        e = dict(os.environ)
        if env:
            e.update(env)
        p = subprocess.run([sys.executable, me] + list(a), capture_output=True,
                           text=True, env=e)
        return p.returncode, p.stdout + p.stderr

    # budget mode
    out = os.path.join(fake_dir, "out budget")
    code, text = tool("budget", "--rldpack", fake, "--src", src, "--out", out)
    check(code == 0, "budget exit 0: " + text)
    files = sorted(x for x in os.listdir(out) if x.endswith(".rldchar"))
    want = sorted("testdriver-%s.rldchar" % v for v in STANDARD_VARIANTS)
    check(files == want, "budget file names")
    p = os.path.join(out, "testdriver-t10k-x2048.rldchar")
    check(os.path.exists(p) and open(p, "rb").read() == b"BASE" + b"GLB-t10k-x2048",
          "budget file content = base + glb")
    chars = open(os.path.join(out, "chars.txt")).read().splitlines()
    rows = [l.split("\t") for l in chars if not l.startswith("#")]
    order = [r[0] for r in rows]
    check(order == want, "roster order is byte-wise sorted")
    check(order[0] == "testdriver-t10k-x2048.rldchar" and
          order.index("testdriver-t160k-x2048.rldchar") <
          order.index("testdriver-t20k-x2048.rldchar") and
          order.index("testdriver-t10k-x2048.rldchar") <
          order.index("testdriver-t160k-x2048.rldchar"),
          "t10k < t160k < t20k")
    check([r[4] for r in rows] == [str(i) for i in range(1, 9)], "roster numbers 1..8")
    check(rows[0][1] == str(os.path.getsize(os.path.join(out, rows[0][0]))) and
          rows[0][2] == sha256(os.path.join(out, rows[0][0])) and
          rows[0][3] == sha256(os.path.join(src, "testdriver-t10k-x2048.glb")),
          "chars.txt bytes and hashes")
    check("roster order" in text, "roster printed")

    # custom variants + command template
    out2 = os.path.join(fake_dir, "out custom")
    tpl = "python \"%s\" hires-add {base} {glb} -o {out}" % fake
    code, text = tool("budget", "--rldpack", fake, "--src", src, "--out", out2,
                      "--variants", "t40k-x1024,t40k-x512", "--command-template", tpl)
    check(code == 0 and sorted(os.listdir(out2)) ==
          ["chars.txt", "testdriver-t40k-x1024.rldchar", "testdriver-t40k-x512.rldchar"],
          "custom variants and command template: " + text)

    # failing hires-add
    out3 = os.path.join(fake_dir, "out fail")
    code, text = tool("budget", "--rldpack", fake, "--src", src, "--out", out3,
                      env={"FAKE_FAIL_ADD": "1"})
    check(code == 1 and "fake hires-add failure" in text, "failing hires-add gives exit 1")

    # non-empty out without --clean
    code, text = tool("budget", "--rldpack", fake, "--src", src, "--out", out)
    check(code == 2, "non-empty out without --clean gives exit 2")

    # --clean deletes only *.rldchar and chars.txt
    with open(os.path.join(out, "keep.txt"), "w") as f:
        f.write("x")
    code, text = tool("budget", "--rldpack", fake, "--src", src, "--out", out,
                      "--variants", "t40k-x512", "--clean")
    check(code == 0 and sorted(os.listdir(out)) ==
          ["chars.txt", "keep.txt", "testdriver-t40k-x512.rldchar"],
          "--clean keeps other files: " + str(sorted(os.listdir(out))))

    # podium mode
    outp = os.path.join(fake_dir, "out podium")
    code, text = tool("podium", "--rldpack", fake, "--src", src, "--out", outp)
    check(code == 0, "podium exit 0: " + text)
    seatline = [l for l in text.splitlines() if l.startswith("--dev-char-seat-files ")]
    want_seats = ",".join("testdriver-tpl%d.rldchar" % t for t in range(8))
    check(seatline == ["--dev-char-seat-files " + want_seats], "podium seat list in seat order")
    check("--driver 0" in text, "driver note")
    p = os.path.join(outp, "testdriver-tpl5.rldchar")
    check(os.path.exists(p) and open(p, "rb").read() ==
          b"template=5\n" + b"GLB-t40k-x1024", "podium file = template + glb")
    chars = open(os.path.join(outp, "chars.txt")).read().splitlines()
    rows = [l.split("\t") for l in chars if not l.startswith("#")]
    check(len(rows) == 8 and chars[0].split("\t")[-1] == "template" and
          all(r[0] == "testdriver-tpl%s.rldchar" % r[5] for r in rows),
          "podium chars.txt template column")
    code, text = tool("podium", "--rldpack", fake, "--src", src, "--out", outp,
                      "--templates", "3,1")
    check(code == 2, "podium non-empty out gives exit 2")
    code, text = tool("podium", "--rldpack", fake, "--src", src, "--out", outp,
                      "--templates", "3,1", "--clean")
    check(code == 0 and "--dev-char-seat-files testdriver-tpl3.rldchar,testdriver-tpl1.rldchar"
          in text and "--driver 3" in text, "podium custom order and driver")

    print("hires_chars_dir selftest: %d checks, %d failures" % (n[0], len(fails)))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode", required=True)
    for m in ("budget", "podium"):
        sp = sub.add_parser(m)
        sp.add_argument("--rldpack", required=True)
        sp.add_argument("--src", required=True)
        sp.add_argument("--out", required=True)
        sp.add_argument("--clean", action="store_true")
        sp.add_argument("--command-template", default=DEFAULT_ADD)
        sp.add_argument("--check-template", default=DEFAULT_CHECK)
        if m == "budget":
            sp.add_argument("--variants", default="standard")
            sp.add_argument("--base", default=None)
        else:
            sp.add_argument("--glb", default="testdriver-t40k-x1024.glb")
            sp.add_argument("--templates", default="0,1,2,3,4,5,6,7")
    sub.add_parser("selftest")
    args = ap.parse_args()
    if args.mode == "selftest":
        return selftest()
    return mode_budget(args) if args.mode == "budget" else mode_podium(args)


if __name__ == "__main__":
    sys.exit(main())
