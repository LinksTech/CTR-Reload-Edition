#!/usr/bin/env python3
"""hires_budget_eval.py - p99 frame times per hi-res driver variant and a budget proposal (task H5).

One load run measures several hi-res driver files: the game is started with
--perf-dir and --dev-char-seats cycle (platform/native_chars.c, NativeChar_DevSeatsEntry:
roster entry = load count % roster size), so every race load drives the next
file of the roster on all eight seats. This tool cuts the recorded frames into
one segment per race load, names each segment by the file of its load, and
judges the variants.

INPUTS
  --perf  a perf folder (platform/native_perf.c: frame_times.csv, written one
          row per game frame by NativePerf_EndFrame) or the csv file itself.
  --log   the game log (--log <file>).
  --memory  optional, the CSV of the external memory sampler of the perf
          runs (columns private and workingset in bytes, '#' lines skipped).
          The perf recording itself holds NO memory
          column, and the sampler has a wall clock only, so the peak is for the
          whole run, not per variant.

HOW FRAMES AND LOG LINES ARE TIED (the gap, stated plainly)
  The log has no frame index and no timestamp (Platform_Log), and the perf rows
  have no log line. There is no shared field. The proxy is ORDER:
    - log: one line per bound race load, in order:
        "[CTR Char] dev seats: <all|cycle>, load <n> on level <L>, <b> of 8 seats = <file> (...)"
      (platform/native_chars.c, NativeChar_ArmDevSeats, the Platform_Log at the end)
    - perf: a race segment is a run of rows with game_mode1 without LOADING
      (0x40000000) and without MAIN_MENU (0x2000), loading_stage -1, no demo;
      a new segment starts after every row that is not such a race row, and
      when level_id changes. Each row carries level_id.
  The k-th load line is tied to the next unused race segment with the same
  level id (segments before it that no load line wants, e.g. a race the dev
  seats did not bind, are reported as unmatched). A load line with no segment
  is reported as NODATA and counts as not passed. If the run is cut or a
  race load was refused ("dev seats: not bound") the order still holds,
  because those lines are not counted as loads by the game either. When two
  consecutive loads are on the same level, the order alone separates them
  (the loading rows between them split the segments), so a skipped load line
  would shift every later variant by one - the level id check and the
  per-segment frame count (compare with "[CTR Tour] ... done, N race
  frame(s)") are the guards. This is a proxy, not a key.

METRIC
  Default work_ms (frame time without the vsync wait, NativePerf_EndFrame:
  total_ms minus the vsync_wait bucket). total_ms of a 30 Hz game sits at
  33.3 ms by the wait and says nothing about headroom; --metric total_ms
  selects it anyway.

STATISTICS
  p50/p95/p99 by the NEAREST-RANK method: sorted ascending x[1..n], the
  p-th percentile is x[ceil(p/100 * n)]. No interpolation. max = largest.

RULE (constants below)
  variant passes when p99 <= LIMIT_MS and it has at least MIN_FRAMES frames.
  Triangle limit: per texture edge the variants are taken by ascending
  triangle count; the limit of that edge is the largest level whose own and
  all smaller levels' variants passed (the passing prefix); the triangle
  limit is the largest of these over the edges. Proposed loader constant =
  floor(MARGIN * limit). Texture edge limit: the largest edge that passed at
  the TRI_REF level (40k), if present.

Usage:
  python tools/hires_budget_eval.py eval --perf <dir|file> --log <log> [--warmup-frames 120]
         [--limit-ms 16.7] [--margin 0.8] [--min-frames 1800] [--metric work_ms] [--memory csv] [--json out.json]
  python tools/hires_budget_eval.py selftest
Exit: 0 ok, 1 selftest failure or error, 2 unusable input (no perf rows, no load lines).
Last line of eval: "hires_budget_eval: <k> variants, triangle limit <T|none>, texture edge limit <E|none>"
"""

import argparse
import csv
import json
import math
import os
import re
import sys
import tempfile
from fractions import Fraction

# ---- rule constants -------------------------------------------------------
LIMIT_MS = 16.7          # pass = p99 <= this
MARGIN = "0.8"           # proposed constant = floor(MARGIN * triangle limit)
WARMUP_FRAMES = 120      # race frames skipped after each load
MIN_FRAMES = 1800        # fewer measured frames: p99 not stable, variant not passed
TRI_REF = 40000          # triangle level at which the texture edge limit is read
METRIC = "work_ms"

# ---- game constants (include/namespace_Main.h) -----------------------------
GM_LOADING = 0x40000000
GM_MAIN_MENU = 0x2000

LOAD_RE = re.compile(
    r"\[CTR Char\] dev seats: (all|cycle), load (\d+) on level (\d+), (\d+) of (\d+) seats = (.+?) \((\d+) draw bytes\)")
VARIANT_RE = re.compile(r"-t(\d+)k-x(\d+)", re.IGNORECASE)


class InputError(Exception):
    pass


def percentile(sorted_vals, p):
    """Nearest rank: x[ceil(p/100*n)] of the ascending list (1-based)."""
    n = len(sorted_vals)
    if n == 0:
        return None
    rank = max(1, math.ceil(p * n / 100.0 - 1e-9))
    return sorted_vals[min(rank, n) - 1]


def variant_info(filename):
    stem = os.path.basename(filename.replace("\\", "/"))
    stem = re.sub(r"\.(rldchar|glb)$", "", stem, flags=re.IGNORECASE)
    m = VARIANT_RE.search(stem)
    if m:
        return stem, int(m.group(1)) * 1000, int(m.group(2))
    return stem, None, None


# ---- reading ----------------------------------------------------------------
def find_csv(perf):
    if os.path.isdir(perf):
        p = os.path.join(perf, "frame_times.csv")
        if not os.path.isfile(p):
            raise InputError("no frame_times.csv in %s" % perf)
        return p
    if os.path.isfile(perf):
        return perf
    raise InputError("perf path not found: %s" % perf)


def read_race_segments(csv_path, metric):
    """-> list of segments {level, rows:[(frame_index, value)]} of race frames in order."""
    segments = []
    cur = None
    with open(csv_path, newline="", encoding="ascii", errors="replace") as f:
        reader = csv.DictReader(f)
        need = ["frame_index", metric, "level_id", "game_mode1", "loading_stage", "bool_demo_mode"]
        for c in need:
            if reader.fieldnames is None or c not in reader.fieldnames:
                raise InputError("column %s missing in %s" % (c, csv_path))
        for row in reader:
            try:
                mode = int(row["game_mode1"]) & 0xFFFFFFFF
                is_race = ((mode & (GM_LOADING | GM_MAIN_MENU)) == 0 and int(row["loading_stage"]) == -1
                           and int(row["bool_demo_mode"]) == 0)
                level = int(row["level_id"])
                idx = int(row["frame_index"])
                val = float(row[metric])
            except (ValueError, TypeError):
                is_race = False
                level = idx = val = None
            if not is_race:
                cur = None
                continue
            if cur is None or cur["level"] != level:
                cur = {"level": level, "rows": []}
                segments.append(cur)
            cur["rows"].append((idx, val))
    return segments


def read_loads(log_path):
    loads = []
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LOAD_RE.search(line)
            if m:
                loads.append({"mode": m.group(1), "n": int(m.group(2)), "level": int(m.group(3)), "bound": int(m.group(4)),
                              "file": m.group(6)})
    return loads


def read_memory(paths):
    for p in paths:
        if p and os.path.isfile(p):
            peak = {"private": 0, "workingset": 0}
            with open(p, encoding="ascii", errors="replace") as f:
                lines = [x for x in f if not x.startswith("#")]
            for r in csv.DictReader(lines):
                try:
                    peak["private"] = max(peak["private"], int(r["private"]))
                    peak["workingset"] = max(peak["workingset"], int(r["workingset"]))
                except (ValueError, KeyError, TypeError):
                    continue
            return {"file": p, "private_peak_mib": round(peak["private"] / 1048576.0, 1),
                    "workingset_peak_mib": round(peak["workingset"] / 1048576.0, 1)}
    return None


# ---- evaluation ---------------------------------------------------------------
def tie(loads, segments):
    """Order proxy: k-th load takes the next unused segment with its level id."""
    pos = 0
    pairs = []
    unmatched = []
    for ld in loads:
        found = None
        for i in range(pos, len(segments)):
            if segments[i]["level"] == ld["level"]:
                found = i
                break
        if found is None:
            pairs.append((ld, None))
            continue
        unmatched.extend(range(pos, found))
        pairs.append((ld, segments[found]))
        pos = found + 1
    unmatched.extend(range(pos, len(segments)))
    return pairs, unmatched


def evaluate(loads, segments, warmup, limit_ms, margin, min_frames):
    pairs, unmatched = tie(loads, segments)
    variants = {}
    order = []
    for ld, seg in pairs:
        name, tris, edge = variant_info(ld["file"])
        v = variants.get(name)
        if v is None:
            v = variants[name] = {"name": name, "tris": tris, "edge": edge, "loads": 0, "vals": []}
            order.append(name)
        v["loads"] += 1
        if seg is not None:
            v["vals"].extend(val for _, val in seg["rows"][warmup:])
    out = []
    for name in order:
        v = variants[name]
        s = sorted(v["vals"])
        n = len(s)
        rec = {"name": name, "triangles": v["tris"], "edge": v["edge"], "loads": v["loads"], "frames": n,
               "p50": percentile(s, 50), "p95": percentile(s, 95), "p99": percentile(s, 99), "max": s[-1] if s else None}
        if n == 0:
            rec["status"] = "NODATA"
        elif n < min_frames:
            rec["status"] = "FEW"
        elif rec["p99"] <= limit_ms:
            rec["status"] = "PASS"
        else:
            rec["status"] = "FAIL"
        out.append(rec)

    # triangle limit: passing prefix per edge, largest over the edges
    tri_limit = None
    edges = sorted({r["edge"] for r in out if r["edge"] is not None and r["triangles"] is not None})
    for e in edges:
        ladder = sorted((r for r in out if r["edge"] == e and r["triangles"] is not None), key=lambda r: r["triangles"])
        best = None
        for r in ladder:
            if r["status"] != "PASS":
                break
            best = r["triangles"]
        if best is not None and (tri_limit is None or best > tri_limit):
            tri_limit = best
    constant = None
    if tri_limit is not None:
        constant = math.floor(Fraction(str(margin)) * tri_limit)

    ref = [r for r in out if r["triangles"] == TRI_REF and r["edge"] is not None]
    passed = [r["edge"] for r in ref if r["status"] == "PASS"]
    edge_limit = max(passed) if passed else None
    edge_note = None
    if passed and any(r["status"] != "PASS" and r["edge"] < edge_limit for r in ref):
        edge_note = "a smaller edge failed at %dk while %d passed - not monotonic" % (TRI_REF // 1000, edge_limit)
    return {"variants": out, "triangle_limit": tri_limit, "loader_constant": constant, "edge_limit": edge_limit,
            "edge_note": edge_note, "unmatched_segments": len(unmatched), "loads": len(loads), "segments": len(segments)}


def fmt(x):
    return "-" if x is None else "%.2f" % x


def print_report(res, args, mem):
    print("hires_budget_eval: metric %s, warm-up %d frames, limit p99 <= %.1f ms, min %d frames, margin %s" %
          (args.metric, args.warmup_frames, args.limit_ms, args.min_frames, args.margin))
    print("loads in log %d, race segments in perf %d, unmatched segments %d" %
          (res["loads"], res["segments"], res["unmatched_segments"]))
    print("%-28s %8s %6s %6s %7s %7s %7s %7s %7s  %s" % ("variant", "tris", "edge", "loads", "frames", "p50", "p95", "p99", "max", "status"))
    rows = sorted(res["variants"], key=lambda r: (r["edge"] or 0, r["triangles"] or 0, r["name"]))
    for r in rows:
        print("%-28s %8s %6s %6d %7d %7s %7s %7s %7s  %s" %
              (r["name"], r["triangles"] if r["triangles"] is not None else "-", r["edge"] if r["edge"] is not None else "-",
               r["loads"], r["frames"], fmt(r["p50"]), fmt(r["p95"]), fmt(r["p99"]), fmt(r["max"]), r["status"]))
    if mem:
        print("host memory peak (whole run, not per variant): private %.1f MiB, working set %.1f MiB (%s)" %
              (mem["private_peak_mib"], mem["workingset_peak_mib"], mem["file"]))
    else:
        print("host memory peak: not in the perf recording (pass --memory <memory sampler csv>)")
    if res["edge_note"]:
        print("note: " + res["edge_note"])
    if res["triangle_limit"] is None:
        print("proposal: no triangle level passed - no loader constant")
    else:
        print("proposal: triangle limit %d, loader constant = floor(%s * %d) = %d" %
              (res["triangle_limit"], args.margin, res["triangle_limit"], res["loader_constant"]))
    print("proposal: texture edge limit at %dk: %s" % (TRI_REF // 1000, res["edge_limit"] if res["edge_limit"] is not None else "none"))
    print("hires_budget_eval: %d variants, triangle limit %s, texture edge limit %s" %
          (len(res["variants"]), res["triangle_limit"] if res["triangle_limit"] is not None else "none",
           res["edge_limit"] if res["edge_limit"] is not None else "none"))


def cmd_eval(args):
    try:
        csv_path = find_csv(args.perf)
        segments = read_race_segments(csv_path, args.metric)
        if not segments:
            raise InputError("no race frames in %s" % csv_path)
        if not os.path.isfile(args.log):
            raise InputError("log not found: %s" % args.log)
        loads = read_loads(args.log)
        if not loads:
            raise InputError("no '[CTR Char] dev seats: ... load N on level L ... = <file>' line in %s (run needs --dev-char-seats)" % args.log)
    except InputError as e:
        print("hires_budget_eval: error: %s" % e)
        return 2
    res = evaluate(loads, segments, args.warmup_frames, args.limit_ms, args.margin, args.min_frames)
    base = args.perf if os.path.isdir(args.perf) else os.path.dirname(args.perf)
    mem = read_memory([args.memory])
    print_report(res, args, mem)
    if args.json:
        res["memory"] = mem
        res["settings"] = {"metric": args.metric, "warmup_frames": args.warmup_frames, "limit_ms": args.limit_ms,
                           "margin": args.margin, "min_frames": args.min_frames}
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1)
    return 0


# ---- selftest -------------------------------------------------------------------
REAL_HEADER = (
    "frame_index,total_ms,work_ms,over_budget_ms,work_over_budget_ms,dominant_bucket,dominant_ms,frameCounter,timer,level_id,"
    "game_mode1,loading_stage,bool_demo_mode,num_plyr,elapsed_time_ms,vsync_till_flip,vSync_between_drawSync,"
    "frameTimer_VsyncCallback,audio_underrun_delta,audio_overflow_delta,audio_queued_frames,game_logic_ms,render_frame_ms,"
    "mainframe_setup_ms,mainframe_effects_ms,mainframe_hud_ms,mainframe_queue_instances_ms,mainframe_execute_instances_ms,"
    "mainframe_level_geometry_ms,mainframe_post_level_ms,mainframe_refreshcard_ms,mainframe_clear_screen_ms,mainframe_ui_ms,"
    "mainframe_render_vsync_ms,platform_end_frame_ms,platform_end_scene_ms,render_submit_ms,platform_begin_scene_ms,draw_otag_ms,"
    "draw_otag_parse_ms,draw_all_splits_ms,renderer_begin_scene_ms,renderer_update_vram_ms,renderer_vertex_upload_ms,"
    "renderer_draw_triangles_ms,framebuffer_store_ms,framebuffer_readback_ms,swap_window_ms,vsync_wait_ms,audio_vblank_ms,"
    "native_char_pull_ms,native_char_wheels_ms,native_char_upload_ms,native_char_bind_ms,native_char_draw_ms,native_char_views_ms,"
    "vk_draw_setup_ms,draw_calls,char_draw_calls,char_views,upload_bytes,char_upload_bytes,char_vertices,char_items,char_mirror_items")


class SynthRun:
    """Writes frame_times.csv in the real column layout and a log in the real line format."""

    def __init__(self, folder):
        self.folder = folder
        self.rows = []
        self.log = ["[CTR Native] Initialising platform"]
        self.idx = 0
        self.cols = REAL_HEADER.split(",")

    def _row(self, work, total, level, mode, stage):
        d = dict.fromkeys(self.cols, "0")
        d.update(frame_index=str(self.idx), total_ms="%.3f" % total, work_ms="%.3f" % work, level_id=str(level),
                 game_mode1=str(mode), loading_stage=str(stage), num_plyr="1", frameCounter=str(self.idx + 1))
        self.idx += 1
        self.rows.append(",".join(d[c] for c in self.cols))

    def menu(self, n=3):
        for _ in range(n):
            self._row(2.0, 33.3, 39, GM_LOADING | GM_MAIN_MENU, 4)
            self._row(2.0, 33.3, 39, GM_MAIN_MENU, -1)

    def load(self, level, filename, work_values, load_rows=6, warm_value=99.0, warmup=120, bound=True):
        for s in range(load_rows):
            self._row(3.0, 33.0, level, GM_LOADING | 0x400000, s + 1)
        if bound:
            self.log.append("[CTR Char] dev seats: cycle, load %d on level %d, 8 of 8 seats = %s (123456 draw bytes), bots on template 14" %
                            (sum(1 for x in self.log if "dev seats: cycle, load" in x) + 1, level, filename))
        for i in range(warmup):
            self._row(warm_value, warm_value, level, 0x400000, -1)
        for v in work_values:
            self._row(v, 33.3, level, 0x400000, -1)

    def write(self):
        os.makedirs(os.path.join(self.folder, "perf"), exist_ok=True)
        with open(os.path.join(self.folder, "perf", "frame_times.csv"), "w", newline="", encoding="ascii") as f:
            f.write(REAL_HEADER + "\n" + "\n".join(self.rows) + "\n")
        lp = os.path.join(self.folder, "run.log")
        with open(lp, "w", encoding="ascii") as f:
            f.write("\n".join(self.log) + "\n")
        return os.path.join(self.folder, "perf"), lp


def dist(n, base, tail_value, tail_count):
    """n values: base for most, tail_count values of tail_value at the end of the sorted order.
    Interleaved so that the order in time carries no meaning for the percentile."""
    vals = [base + (i % 10) * 0.01 for i in range(n)]
    for k in range(tail_count):
        vals[(k * 37 + 5) % n] = tail_value
    return vals


def build_ladder(folder, n=2000, warmup=120, fail_from=160, with_t320=True, skip=None, unbound_extra=False):
    r = SynthRun(folder)
    r.menu()
    tris = [10, 20, 40, 80, 160, 320]
    for t in tris:
        if skip == t:
            continue
        # passing: p99 value 10.xx; failing: 1.5 % of frames at 30 ms -> p99 = 30
        base = 4.0 + t / 40.0
        values = dist(n, base, 30.0 if t >= fail_from else 12.0, 25 if t >= fail_from else 10)
        r.load(7, "testdriver-t%dk-x2048.rldchar" % t, values, warmup=warmup)
        r.menu()
    for e in (512, 1024):
        r.load(7, "testdriver-t40k-x%d.rldchar" % e, dist(n, 5.0 + e / 1024.0, 12.0, 10), warmup=warmup)
        r.menu()
    r.load(7, "other model.rldchar", dist(n, 3.0, 5.0, 3), warmup=warmup)
    return r


class Checker:
    def __init__(self):
        self.n = 0
        self.fail = 0

    def check(self, cond, what):
        self.n += 1
        if not cond:
            self.fail += 1
            print("FAIL: " + what)


def selftest():
    c = Checker()
    # statistics: nearest rank
    xs = list(range(1, 101))
    c.check(percentile(xs, 99) == 99 and percentile(xs, 50) == 50 and percentile(xs, 95) == 95, "nearest rank on 1..100")
    c.check(percentile([1, 2, 3, 4], 50) == 2 and percentile([1, 2, 3, 4], 99) == 4, "nearest rank on 4 values")
    c.check(percentile([], 99) is None and percentile([7], 99) == 7, "nearest rank on empty / single")
    c.check(variant_info("C:\\x\\testdriver-t40k-x1024.rldchar") == ("testdriver-t40k-x1024", 40000, 1024), "variant name .rldchar")
    c.check(variant_info("testdriver-t320k-x2048.glb") == ("testdriver-t320k-x2048", 320000, 2048), "variant name .glb")
    c.check(variant_info("Crash JP.rldchar") == ("Crash JP", None, None), "unknown name kept")

    class A:  # args
        metric = "work_ms"
        warmup_frames = 120
        limit_ms = 16.7
        margin = "0.8"
        min_frames = 1800
        memory = None
        json = None
        perf = ""
        log = ""

    root = tempfile.mkdtemp(prefix="hbe-")
    # ---- run 1: the full ladder, t160k and t320k fail
    f1 = os.path.join(root, "r1")
    run = build_ladder(f1)
    a = A()
    a.perf, a.log = run.write()
    segs = read_race_segments(find_csv(a.perf), "work_ms")
    loads = read_loads(a.log)
    c.check(len(loads) == 9 and len(segs) == 9, "9 loads, 9 race segments (got %d / %d)" % (len(loads), len(segs)))
    res = evaluate(loads, segs, 120, 16.7, "0.8", 1800)
    byname = {v["name"]: v for v in res["variants"]}
    c.check(len(res["variants"]) == 9, "9 variants")
    v = byname["testdriver-t10k-x2048"]
    c.check(v["frames"] == 2000, "warm-up skipped: 2000 frames after 120 warm-up rows (got %d)" % v["frames"])
    c.check(v["max"] is not None and v["max"] < 50, "warm-up values (99 ms) are not in max (got %s)" % v["max"])
    c.check(abs(v["p99"] - 12.0) < 1e-9 or v["p99"] <= 16.7, "t10k p99 within limit")
    c.check(abs(byname["testdriver-t160k-x2048"]["p99"] - 30.0) < 1e-9, "t160k p99 = 30 (got %s)" % byname["testdriver-t160k-x2048"]["p99"])
    c.check(byname["testdriver-t160k-x2048"]["status"] == "FAIL" and byname["testdriver-t80k-x2048"]["status"] == "PASS", "t160k FAIL, t80k PASS")
    c.check(res["triangle_limit"] == 80000, "triangle limit 80000 (got %s)" % res["triangle_limit"])
    c.check(res["loader_constant"] == 64000, "loader constant 64000 (got %s)" % res["loader_constant"])
    c.check(byname["testdriver-t320k-x2048"]["status"] == "FAIL", "t320k FAIL")
    c.check(res["edge_limit"] == 2048, "edge limit 2048 (got %s)" % res["edge_limit"])
    c.check(byname["other model"]["triangles"] is None and byname["other model"]["status"] == "PASS", "unknown name kept and judged")
    c.check(res["unmatched_segments"] == 0, "no unmatched segments")
    # loads with level mismatch order: level ids tie
    # ---- non-prefix pass: t320k passing does not lift the limit above a failing t160k
    f2 = os.path.join(root, "r2")
    r2 = SynthRun(f2)
    r2.menu()
    for t, bad in ((10, False), (20, False), (40, False), (80, True), (160, False)):
        r2.load(7, "testdriver-t%dk-x2048.rldchar" % t, dist(2000, 5.0, 30.0 if bad else 12.0, 25 if bad else 10))
        r2.menu()
    p, lg = r2.write()
    res2 = evaluate(read_loads(lg), read_race_segments(find_csv(p), "work_ms"), 120, 16.7, "0.8", 1800)
    c.check(res2["triangle_limit"] == 40000 and res2["loader_constant"] == 32000, "limit stops at the first failing level (got %s)" % res2["triangle_limit"])
    # ---- everything passes, edge ladder at 40k: 512 fails, 1024 passes
    f3 = os.path.join(root, "r3")
    r3 = SynthRun(f3)
    r3.menu()
    for t in (10, 20, 40):
        r3.load(7, "testdriver-t%dk-x2048.rldchar" % t, dist(2000, 5.0, 12.0, 10))
        r3.menu()
    r3.load(7, "testdriver-t40k-x512.rldchar", dist(2000, 5.0, 30.0, 40))
    r3.menu()
    r3.load(7, "testdriver-t40k-x1024.rldchar", dist(2000, 5.0, 12.0, 10))
    p, lg = r3.write()
    res3 = evaluate(read_loads(lg), read_race_segments(find_csv(p), "work_ms"), 120, 16.7, "0.8", 1800)
    c.check(res3["triangle_limit"] == 40000 and res3["edge_limit"] == 2048, "edge ladder: limit 2048 (got %s)" % res3["edge_limit"])
    c.check(res3["edge_note"] is not None, "non-monotonic edge noted")
    # ---- a missing variant: a load line without race frames
    f4 = os.path.join(root, "r4")
    r4 = SynthRun(f4)
    r4.menu()
    r4.load(7, "testdriver-t10k-x2048.rldchar", dist(2000, 5.0, 12.0, 10))
    r4.menu()
    r4.log.append("[CTR Char] dev seats: cycle, load 2 on level 9, 8 of 8 seats = testdriver-t20k-x2048.rldchar (1 draw bytes), bots on template 14")
    p, lg = r4.write()
    res4 = evaluate(read_loads(lg), read_race_segments(find_csv(p), "work_ms"), 120, 16.7, "0.8", 1800)
    b4 = {v["name"]: v for v in res4["variants"]}
    c.check(b4["testdriver-t20k-x2048"]["status"] == "NODATA" and b4["testdriver-t20k-x2048"]["frames"] == 0, "missing variant is NODATA")
    c.check(res4["triangle_limit"] == 10000, "missing variant does not lift the limit (got %s)" % res4["triangle_limit"])
    # ---- too few frames
    f5 = os.path.join(root, "r5")
    r5 = SynthRun(f5)
    r5.menu()
    r5.load(7, "testdriver-t10k-x2048.rldchar", dist(500, 5.0, 6.0, 1))
    p, lg = r5.write()
    res5 = evaluate(read_loads(lg), read_race_segments(find_csv(p), "work_ms"), 120, 16.7, "0.8", 1800)
    c.check(res5["variants"][0]["status"] == "FEW" and res5["triangle_limit"] is None, "too few frames: FEW, no limit")
    # ---- unmatched segment (a race the dev seats did not bind) before the bound one
    f6 = os.path.join(root, "r6")
    r6 = SynthRun(f6)
    r6.menu()
    r6.load(3, "unbound.rldchar", dist(300, 5.0, 6.0, 1), bound=False)
    r6.menu()
    r6.load(7, "testdriver-t10k-x2048.rldchar", dist(2000, 5.0, 12.0, 10))
    p, lg = r6.write()
    res6 = evaluate(read_loads(lg), read_race_segments(find_csv(p), "work_ms"), 120, 16.7, "0.8", 1800)
    c.check(res6["unmatched_segments"] == 1 and res6["variants"][0]["frames"] == 2000 - 0, "unbound race segment skipped by level id")
    # ---- eval command: output, last line, JSON, exit codes
    import io
    import contextlib
    a = A()
    a.perf, a.log = run.write()
    a.json = os.path.join(root, "out.json")
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cmd_eval(a)
    lines = buf.getvalue().strip().splitlines()
    c.check(rc == 0, "eval exit 0")
    c.check(lines[-1] == "hires_budget_eval: 9 variants, triangle limit 80000, texture edge limit 2048", "eval last line (got %r)" % lines[-1])
    c.check(os.path.isfile(a.json) and json.load(open(a.json))["loader_constant"] == 64000, "json written")
    # log without load lines -> exit 2
    nolog = os.path.join(root, "empty.log")
    with open(nolog, "w") as fh:
        fh.write("[CTR Native] Initialising platform\n[CTR Char] dev seats: not bound (demo)\n")
    a2 = A()
    a2.perf, a2.log = a.perf, nolog
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cmd_eval(a2)
    c.check(rc == 2 and "no '[CTR Char] dev seats" in buf.getvalue(), "log without load lines -> exit 2")
    a3 = A()
    a3.perf, a3.log = os.path.join(root, "nonexistent"), a.log
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cmd_eval(a3)
    c.check(rc == 2, "missing perf folder -> exit 2")
    # memory csv
    mem = os.path.join(root, "memory.csv")
    with open(mem, "w") as fh:
        fh.write("# comment\ntime_ms,pid,private,workingset,peak_workingset,virtual,peak_virtual,gpu_dedicated,gpu_shared\n"
                 "1,1,104857600,209715200,209715200,0,0,0,0\n2,1,314572800,209715200,209715200,0,0,0,0\n")
    m = read_memory([mem])
    c.check(m is not None and m["private_peak_mib"] == 300.0 and m["workingset_peak_mib"] == 200.0, "memory peak read")

    print("hires_budget_eval selftest: %d checks, %d failures" % (c.n, c.fail))
    return 0 if c.fail == 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="p99 frame times per hi-res driver variant and a budget proposal")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("eval")
    e.add_argument("--perf", required=True)
    e.add_argument("--log", required=True)
    e.add_argument("--warmup-frames", type=int, default=WARMUP_FRAMES)
    e.add_argument("--limit-ms", type=float, default=LIMIT_MS)
    e.add_argument("--margin", default=MARGIN)
    e.add_argument("--min-frames", type=int, default=MIN_FRAMES)
    e.add_argument("--metric", default=METRIC, choices=["work_ms", "total_ms"])
    e.add_argument("--memory", default=None)
    e.add_argument("--json", default=None)
    sub.add_parser("selftest")
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return selftest()
    return cmd_eval(args)


if __name__ == "__main__":
    sys.exit(main())
