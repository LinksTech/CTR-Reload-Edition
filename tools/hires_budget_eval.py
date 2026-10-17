#!/usr/bin/env python3
"""hires_budget_eval.py - CPU+GPU p99 per hi-res driver variant and a budget proposal (task H5).

One load run measures several hi-res driver files: the game is started with
--perf-dir and --dev-char-seats cycle (platform/native_chars.c, NativeChar_DevSeatsEntry:
roster entry = load count % roster size), so every race load drives the next
file of the roster on all eight seats. This tool cuts the recorded frames into
one segment per race load, names each load by the file of its dev-seats line,
and judges the variants.

INPUTS
  --perf  a perf folder (platform/native_perf.c: frame_times.csv, one row per
          game frame, and gpu_frame_times.csv) or the frame_times.csv itself
          (then gpu_frame_times.csv is looked for next to it).
  --log   the game log (--log <file>).
  --memory  optional, the CSV of the external memory sampler of the perf
          runs (columns private and workingset in bytes, '#' lines skipped).
          The perf recording holds NO memory column and the sampler has a wall
          clock only, so the peak is for the whole run, not per variant.

THE VERDICT METRIC - the rule of the evaluation script of the earlier load runs (perf075: its
header and its perf window function), applied per load segment:
  CPU      = work_ms - swap_window_ms
  GPU      = gpu_ms of gpu_frame_times.csv with renderer_frame_index == frame_index + --gpu-offset
  CPU+GPU  = the sum inside the same frame, only frames that have a GPU row
  window   = the segment's race frames without the first --start (300) and the last --end (5)
  dropped  = frames with framebuffer_readback_ms > 0, and frames whose frameTimer_VsyncCallback is
             in {V-2, V-1, V} for a VBlank V of a log line "[CTR Shot] vblank V: ..." or
             "[CTR Shot] series i of n at vblank V (due ..): ..." (the BMP is written in the
             frame of VBlank V-2, platform/native_platform.c:2098/2125/2155)
  p99      = nearest rank: sorted ascending x[1..n], x[ceil(0.99 n)]; p50 the same; no interpolation
  pass     = CPU+GPU p99 <= --limit-ms (16.7) with at least --min-frames (1800) clean frames
  Reported per load: n, p50/p99/max of CPU+GPU; p99 of GPU, CPU and work_ms as information.

HOW A SEGMENT STARTS
  Preferred key: the log line "[CTR Perf] load end: frame_index <n> level <L> vblank <v>"
  (written by the perf recorder when a load ends). Each
      "[CTR Char] dev seats: <all|cycle>, load <k> on level <L>, <b> of 8 seats = <file> (...)"
  line (platform/native_chars.c, NativeChar_ArmDevSeats) is tied to the next load-end line after it
  (before the next dev-seats line). The segment is every row from that frame_index up to the next
  load-end line of any load, with loading_stage == -1 and the same level_id.
  FALLBACK when the log has no load-end line at all: ORDER proxy - the log has no frame index, so
  the k-th dev-seats line is tied to the next unused run of race rows (game_mode1 without LOADING
  0x40000000 and MAIN_MENU 0x2000, loading_stage -1, no demo, same level id; a run ends at the first
  other row). A WARNING line says so. A skipped log line would shift all later variants by one; the
  level id and the frame counts are the only guards. A load without a segment (or without key, when
  other loads have one) is NODATA and counts as not passed.

DUPLICATE FILES
  A file loaded twice (e.g. a repeated tour entry for contact-sheet shots) is listed as <name> and
  <name>#2. The variant's verdict takes the load with the most clean frames; that row is marked *.

LIMIT RULES (constants below)
  Triangle limit: per texture edge the variants are taken by ascending triangle count; the limit of
  that edge is the largest level whose own and all smaller levels' variants passed (the passing
  prefix); the triangle limit is the largest of these over the edges. Proposed loader constant =
  floor(MARGIN * limit). Texture edge limit: the largest edge that passed at the TRI_REF level (40k).

Usage:
  python tools/hires_budget_eval.py eval --perf <dir|file> --log <log> [--start 300] [--end 5]
         [--gpu-offset 0] [--limit-ms 16.7] [--margin 0.8] [--min-frames 1800] [--memory csv] [--json out.json]
  python tools/hires_budget_eval.py selftest
Exit: 0 ok, 1 selftest failure, 2 unusable input (no perf rows, no load lines).
Last line of eval: "hires_budget_eval: <k> variants, triangle limit <T|none>, texture edge limit <E|none>"
"""

import argparse
import csv
import json
import math
import os
import re
import shutil
import sys
import tempfile
from fractions import Fraction

# ---- rule constants -------------------------------------------------------
LIMIT_MS = 16.7          # pass = CPU+GPU p99 <= this
MARGIN = "0.8"           # proposed constant = floor(MARGIN * triangle limit)
START_FRAMES = 300       # race frames dropped at the start of each segment
END_FRAMES = 5           # race frames dropped at the end of each segment
MIN_FRAMES = 1800        # fewer clean frames: p99 not stable, variant not passed
TRI_REF = 40000          # triangle level at which the texture edge limit is read

# ---- game constants (include/namespace_Main.h) -----------------------------
GM_LOADING = 0x40000000
GM_MAIN_MENU = 0x2000

LOAD_RE = re.compile(
    r"\[CTR Char\] dev seats: (all|cycle), load (\d+) on level (\d+), (\d+) of (\d+) seats = (.+?) \((\d+) draw bytes\)")
LOAD_END_RE = re.compile(r"\[CTR Perf\] load end: frame_index (\d+) level (\d+) vblank (\d+)")
SHOT_RES = (re.compile(r"\[CTR Shot\] vblank (\d+):"),
            re.compile(r"\[CTR Shot\] series \d+ of \d+ at vblank (\d+) \(due"))
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
    # the copy of hires_chars_dir.py --shots-first is a second load of the same variant
    stem = re.sub(r"-0shots-", "-", stem, flags=re.IGNORECASE)
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


def read_rows(csv_path):
    """All perf rows as dicts of numbers (rows that do not parse are skipped)."""
    rows = []
    with open(csv_path, newline="", encoding="ascii", errors="replace") as f:
        reader = csv.DictReader(f)
        need = ["frame_index", "work_ms", "swap_window_ms", "framebuffer_readback_ms", "frameTimer_VsyncCallback",
                "level_id", "game_mode1", "loading_stage", "bool_demo_mode"]
        for c in need:
            if reader.fieldnames is None or c not in reader.fieldnames:
                raise InputError("column %s missing in %s" % (c, csv_path))
        for r in reader:
            try:
                mode = int(r["game_mode1"]) & 0xFFFFFFFF
                rows.append({"idx": int(r["frame_index"]), "work": float(r["work_ms"]), "swap": float(r["swap_window_ms"]),
                             "readback": float(r["framebuffer_readback_ms"]), "cb": int(r["frameTimer_VsyncCallback"]),
                             "level": int(r["level_id"]), "stage": int(r["loading_stage"]),
                             "race": ((mode & (GM_LOADING | GM_MAIN_MENU)) == 0 and int(r["loading_stage"]) == -1
                                      and int(r["bool_demo_mode"]) == 0)})
            except (ValueError, TypeError):
                continue
    return rows


def read_gpu(csv_path):
    gpu = {}
    p = os.path.join(os.path.dirname(csv_path), "gpu_frame_times.csv")
    if not os.path.isfile(p):
        return None
    with open(p, newline="", encoding="ascii", errors="replace") as f:
        for r in csv.DictReader(f):
            try:
                gpu[int(r["renderer_frame_index"])] = float(r["gpu_ms"])
            except (ValueError, KeyError, TypeError):
                continue
    return gpu


def read_log(log_path):
    """-> loads (dev-seats lines, each with 'key' = tied load-end frame_index or None), load_ends, shot vblanks."""
    loads = []
    ends = []
    shots = []
    open_load = None
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LOAD_RE.search(line)
            if m:
                open_load = {"mode": m.group(1), "n": int(m.group(2)), "level": int(m.group(3)), "bound": int(m.group(4)),
                             "file": m.group(6), "key": None}
                loads.append(open_load)
                continue
            m = LOAD_END_RE.search(line)
            if m:
                ends.append({"idx": int(m.group(1)), "level": int(m.group(2)), "vblank": int(m.group(3))})
                if open_load is not None and open_load["key"] is None:
                    open_load["key"] = int(m.group(1))
                    open_load["key_level"] = int(m.group(2))
                    open_load = None
                continue
            for rx in SHOT_RES:
                m = rx.search(line)
                if m:
                    shots.append(int(m.group(1)))
                    break
    return loads, ends, shots


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


# ---- segments -----------------------------------------------------------------
def order_segments(rows):
    """Runs of race rows (same level id), in order. The proxy when the log has no load-end line."""
    segs = []
    cur = None
    for r in rows:
        if not r["race"]:
            cur = None
            continue
        if cur is None or cur["level"] != r["level"]:
            cur = {"level": r["level"], "rows": []}
            segs.append(cur)
        cur["rows"].append(r)
    return segs


def tie_order(loads, segs):
    """k-th load takes the next unused segment with its level id. -> (list of segment|None, unmatched count)"""
    pos = 0
    out = []
    unmatched = 0
    for ld in loads:
        found = None
        for i in range(pos, len(segs)):
            if segs[i]["level"] == ld["level"]:
                found = i
                break
        if found is None:
            out.append(None)
            continue
        unmatched += found - pos
        out.append(segs[found])
        pos = found + 1
    unmatched += len(segs) - pos
    return out, unmatched


def keyed_segment(rows, ld, boundary_after):
    if ld["key"] is None:
        return None
    sel = [r for r in rows if ld["key"] <= r["idx"] < boundary_after and r["stage"] == -1 and r["level"] == ld["level"]]
    return {"level": ld["level"], "rows": sel} if sel else None


# ---- statistics ---------------------------------------------------------------
def load_stats(seg, gpu, gpu_offset, start, end, shot_set):
    """The rule of the earlier load runs' evaluation (see the head) on one segment."""
    res = {"race_frames": 0, "window": 0, "dropped": 0, "n": 0, "no_gpu_row": 0, "p50": None, "p99": None, "max": None,
           "gpu_p99": None, "cpu_p99": None, "work_p99": None}
    if seg is None:
        return res
    rows = seg["rows"]
    res["race_frames"] = len(rows)
    if len(rows) > start + end and end > 0:
        window = rows[start:-end]
    elif len(rows) > start:
        window = rows[start:] if end <= 0 else []
    else:
        window = []
    clean = [r for r in window if r["readback"] <= 0.0 and r["cb"] not in shot_set]
    res["window"] = len(window)
    res["dropped"] = len(window) - len(clean)
    cpu, work, gpus, both = [], [], [], []
    for r in clean:
        c = r["work"] - r["swap"]
        cpu.append(c)
        work.append(r["work"])
        g = gpu.get(r["idx"] + gpu_offset) if gpu else None
        if g is not None:
            gpus.append(g)
            both.append(c + g)
    res["n"] = len(both)
    res["no_gpu_row"] = len(clean) - len(both)
    sb = sorted(both)
    res["p50"] = percentile(sb, 50)
    res["p99"] = percentile(sb, 99)
    res["max"] = sb[-1] if sb else None
    res["gpu_p99"] = percentile(sorted(gpus), 99)
    res["cpu_p99"] = percentile(sorted(cpu), 99)
    res["work_p99"] = percentile(sorted(work), 99)
    return res


def status_of(st, limit_ms, min_frames):
    if st["n"] == 0:
        return "NODATA"
    if st["n"] < min_frames:
        return "FEW"
    return "PASS" if st["p99"] <= limit_ms else "FAIL"


def evaluate(loads, rows, ends, shots, gpu, a):
    """a: object with start, end, gpu_offset, limit_ms, margin, min_frames."""
    shot_set = set()
    for v in shots:
        shot_set.update((v - 2, v - 1, v))
    warnings = []
    unmatched = 0
    keyed = len(ends) > 0
    if keyed:
        bounds = sorted(e["idx"] for e in ends)
        segs = []
        for ld in loads:
            after = next((b for b in bounds if ld["key"] is not None and b > ld["key"]), 1 << 62)
            segs.append(keyed_segment(rows, ld, after))
        for ld in loads:
            if ld["key"] is None:
                warnings.append("load %d (%s) has no '[CTR Perf] load end' line after it - NODATA" % (ld["n"], ld["file"]))
    else:
        warnings.append("WARNING: the log has no '[CTR Perf] load end' lines - segments tied to the log lines by ORDER and level id "
                        "(proxy, not a key); a skipped dev-seats line would shift the later variants")
        segs, unmatched = tie_order(loads, order_segments(rows))

    seen = {}
    loadrecs = []
    for ld, seg in zip(loads, segs):
        base, tris, edge = variant_info(ld["file"])
        seen[base] = seen.get(base, 0) + 1
        label = base if seen[base] == 1 else "%s#%d" % (base, seen[base])
        st = load_stats(seg, gpu, a.gpu_offset, a.start, a.end, shot_set)
        st.update(label=label, name=base, triangles=tris, edge=edge, load=ld["n"], level=ld["level"])
        st["status"] = status_of(st, a.limit_ms, a.min_frames)
        loadrecs.append(st)

    variants = []
    for base in dict.fromkeys(r["name"] for r in loadrecs):
        mine = [r for r in loadrecs if r["name"] == base]
        best = max(mine, key=lambda r: r["n"])  # first of equals
        for r in mine:
            r["used"] = r is best
        variants.append({"name": base, "triangles": best["triangles"], "edge": best["edge"], "status": best["status"],
                         "p99": best["p99"], "n": best["n"], "loads": [r["label"] for r in mine], "used": best["label"]})

    tri_limit = None
    for e in sorted({v["edge"] for v in variants if v["edge"] is not None and v["triangles"] is not None}):
        ladder = sorted((v for v in variants if v["edge"] == e and v["triangles"] is not None), key=lambda v: v["triangles"])
        best_t = None
        for v in ladder:
            if v["status"] != "PASS":
                break
            best_t = v["triangles"]
        if best_t is not None and (tri_limit is None or best_t > tri_limit):
            tri_limit = best_t
    constant = math.floor(Fraction(str(a.margin)) * tri_limit) if tri_limit is not None else None

    ref = [v for v in variants if v["triangles"] == TRI_REF and v["edge"] is not None]
    passed = [v["edge"] for v in ref if v["status"] == "PASS"]
    edge_limit = max(passed) if passed else None
    edge_note = None
    if passed and any(v["status"] != "PASS" and v["edge"] < edge_limit for v in ref):
        edge_note = "a smaller edge failed at %dk while %d passed - not monotonic" % (TRI_REF // 1000, edge_limit)
    return {"loads": loadrecs, "variants": variants, "triangle_limit": tri_limit, "loader_constant": constant,
            "edge_limit": edge_limit, "edge_note": edge_note, "unmatched_segments": unmatched, "keyed": keyed,
            "warnings": warnings, "shot_vblanks": len(shots), "load_lines": len(loads), "load_end_lines": len(ends)}


# ---- output ---------------------------------------------------------------------
def fmt(x):
    return "-" if x is None else "%.2f" % x


def print_report(res, args, mem):
    print("hires_budget_eval: verdict CPU+GPU p99 <= %.1f ms, min %d clean frames, window = drop first %d / last %d race frames, "
          "gpu offset %d, margin %s" % (args.limit_ms, args.min_frames, args.start, args.end, args.gpu_offset, args.margin))
    print("dev-seats lines %d, load-end lines %d, shot vblanks %d, unmatched segments %d (key: %s)" %
          (res["load_lines"], res["load_end_lines"], res["shot_vblanks"], res["unmatched_segments"],
           "load end" if res["keyed"] else "ORDER proxy"))
    for w in res["warnings"]:
        print(w)
    print("%-30s %8s %6s %7s %7s %7s %7s | %7s %7s %7s %5s  %s" %
          ("load", "tris", "edge", "n", "p50", "p99", "max", "gpu p99", "cpu p99", "wrk p99", "drop", "status"))
    rows = sorted(res["loads"], key=lambda r: (r["edge"] or 0, r["triangles"] or 0, r["label"]))
    for r in rows:
        multi = sum(1 for x in res["loads"] if x["name"] == r["name"]) > 1
        print("%-30s %8s %6s %7d %7s %7s %7s | %7s %7s %7s %5d  %s%s" %
              (r["label"], r["triangles"] if r["triangles"] is not None else "-", r["edge"] if r["edge"] is not None else "-",
               r["n"], fmt(r.get("p50")), fmt(r.get("p99")), fmt(r.get("max")), fmt(r.get("gpu_p99")), fmt(r.get("cpu_p99")),
               fmt(r.get("work_p99")), r["dropped"], r["status"], (" *" if (multi and r["used"]) else "")))
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
        rows = read_rows(csv_path)
        if not any(r["race"] for r in rows):
            raise InputError("no race frames in %s" % csv_path)
        if not os.path.isfile(args.log):
            raise InputError("log not found: %s" % args.log)
        loads, ends, shots = read_log(args.log)
        if not loads:
            raise InputError("no '[CTR Char] dev seats: ... load N on level L ... = <file>' line in %s (run needs --dev-char-seats)" % args.log)
        gpu = read_gpu(csv_path)
    except InputError as e:
        print("hires_budget_eval: error: %s" % e)
        return 2
    res = evaluate(loads, rows, ends, shots, gpu, args)
    if gpu is None:
        res["warnings"].append("WARNING: no gpu_frame_times.csv next to the perf csv - no frame has a GPU row, every load is NODATA")
    mem = read_memory([args.memory])
    print_report(res, args, mem)
    if args.json:
        res["memory"] = mem
        res["settings"] = {"start": args.start, "end": args.end, "gpu_offset": args.gpu_offset, "limit_ms": args.limit_ms,
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

SYNTH_SWAP = 2.0


class SynthRun:
    """Writes frame_times.csv + gpu_frame_times.csv in the real layout and a log in the real line format."""

    def __init__(self, folder, keyed=True, gpu_offset=0):
        self.folder = folder
        self.keyed = keyed
        self.gpu_offset = gpu_offset
        self.rows = []
        self.gpu = []
        self.log = ["[CTR Native] Initialising platform"]
        self.idx = 0
        self.nload = 0
        self.cols = REAL_HEADER.split(",")

    def _row(self, cpu, level, mode, stage, readback=0.0):
        d = dict.fromkeys(self.cols, "0")
        d.update(frame_index=str(self.idx), total_ms="33.300", work_ms="%.4f" % (cpu + SYNTH_SWAP), swap_window_ms="%.3f" % SYNTH_SWAP,
                 framebuffer_readback_ms="%.3f" % readback, frameTimer_VsyncCallback=str(self.vb(self.idx)), level_id=str(level),
                 game_mode1=str(mode), loading_stage=str(stage), num_plyr="1", frameCounter=str(self.idx + 1))
        self.idx += 1
        self.rows.append(",".join(d[c] for c in self.cols))

    @staticmethod
    def vb(idx):
        return 2 * idx + 100  # a 30 Hz game: two VBlanks per frame

    def menu(self, n=3):
        for _ in range(n):
            self._row(2.0, 39, GM_LOADING | GM_MAIN_MENU, 4)
            self._row(2.0, 39, GM_MAIN_MENU, -1)

    def load(self, level, filename, values, lead=START_FRAMES, tail=END_FRAMES, bound=True, readback_at=(), gpu_missing_every=0,
             gpu_linear=False, filler=99.0):
        """values = CPU+GPU sums of the clean window (split 0.6 CPU / 0.4 GPU), or with gpu_linear CPU 5.0 and GPU 0.001*frame_index."""
        for s in range(6):
            self._row(3.0, level, GM_LOADING | 0x400000, s + 1)
        if bound:
            self.nload += 1
            self.log.append("[CTR Char] dev seats: cycle, load %d on level %d, 8 of 8 seats = %s (123456 draw bytes), bots on template 14" %
                            (self.nload, level, filename))
        if self.keyed:
            self.log.append("[CTR Perf] load end: frame_index %d level %d vblank %d" % (self.idx, level, self.vb(self.idx)))
        seq = [("f", filler)] * lead + [("v", v) for v in values] + [("f", filler)] * tail
        for k, (kind, v) in enumerate(seq):
            i = self.idx
            if gpu_linear and kind == "v":
                cpu, gpu = 5.0, 0.001 * i
            else:
                cpu, gpu = (v * 0.6, v * 0.4) if kind == "v" else (v, 0.0)
            rb = 5.0 if (k - lead) in readback_at else 0.0
            self._row(cpu, level, 0x400000, -1, readback=rb)
            if not (gpu_missing_every and kind == "v" and (k % gpu_missing_every) == 0):
                self.gpu.append("%d,%.4f" % (i + self.gpu_offset, gpu))

    def write(self):
        os.makedirs(os.path.join(self.folder, "perf"), exist_ok=True)
        with open(os.path.join(self.folder, "perf", "frame_times.csv"), "w", newline="", encoding="ascii") as f:
            f.write(REAL_HEADER + "\n" + "\n".join(self.rows) + "\n")
        with open(os.path.join(self.folder, "perf", "gpu_frame_times.csv"), "w", newline="", encoding="ascii") as f:
            f.write("renderer_frame_index,gpu_ms\n" + "\n".join(self.gpu) + "\n")
        lp = os.path.join(self.folder, "run.log")
        with open(lp, "w", encoding="ascii") as f:
            f.write("\n".join(self.log) + "\n")
        return os.path.join(self.folder, "perf"), lp


def dist(n, base, tail_value, tail_count):
    """n values: base for most, tail_count values of tail_value; spread so the time order carries no meaning."""
    vals = [base + (i % 10) * 0.01 for i in range(n)]
    for k in range(tail_count):
        vals[(k * 37 + 5) % n] = tail_value
    return vals


def build_ladder(folder, keyed=True, n=2000, fail_from=160):
    r = SynthRun(folder, keyed=keyed)
    r.menu()
    for t in (10, 20, 40, 80, 160, 320):
        r.load(7, "testdriver-t%dk-x2048.rldchar" % t, dist(n, 4.0 + t / 40.0, 30.0 if t >= fail_from else 12.0, 25 if t >= fail_from else 10))
        r.menu()
    for e in (512, 1024):
        r.load(7, "testdriver-t40k-x%d.rldchar" % e, dist(n, 5.0 + e / 1024.0, 12.0, 10))
        r.menu()
    r.load(7, "other model.rldchar", dist(n, 3.0, 5.0, 3))
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


class Args:
    start = START_FRAMES
    end = END_FRAMES
    gpu_offset = 0
    limit_ms = LIMIT_MS
    margin = MARGIN
    min_frames = MIN_FRAMES
    memory = None
    json = None
    perf = ""
    log = ""


def run_eval(run, a=None, shots_in_log=()):
    """Write the synthetic run and evaluate it with the library functions."""
    a = a or Args()
    p, lg = run.write()
    rows = read_rows(find_csv(p))
    loads, ends, shots = read_log(lg)
    gpu = read_gpu(find_csv(p))
    return evaluate(loads, rows, ends, shots, gpu, a)


def selftest():
    """One work folder per run (<TEMP>/hires_budget_eval-selftest-<pid>-<random>),
    removed at the end, also when a check raises: two runs at the same time
    never share a file and nothing is left behind."""
    root = tempfile.mkdtemp(prefix="hires_budget_eval-selftest-%d-" % os.getpid(), dir=tempfile.gettempdir())
    try:
        return _selftest(root)
    finally:
        shutil.rmtree(root)


def _selftest(root):
    import contextlib
    import io
    c = Checker()
    ok = lambda x, y, eps=1e-6: x is not None and abs(x - y) < eps  # noqa: E731
    xs = list(range(1, 101))
    c.check(percentile(xs, 99) == 99 and percentile(xs, 50) == 50 and percentile(xs, 95) == 95, "nearest rank on 1..100")
    c.check(percentile([1, 2, 3, 4], 50) == 2 and percentile([1, 2, 3, 4], 99) == 4, "nearest rank on 4 values")
    c.check(percentile([], 99) is None and percentile([7], 99) == 7, "nearest rank on empty / single")
    c.check(variant_info("C:\\x\\testdriver-t40k-x1024.rldchar") == ("testdriver-t40k-x1024", 40000, 1024), "variant name .rldchar")
    c.check(variant_info("testdriver-t320k-x2048.glb") == ("testdriver-t320k-x2048", 320000, 2048), "variant name .glb")
    c.check(variant_info("testdriver-0shots-t40k-x1024.rldchar") == ("testdriver-t40k-x1024", 40000, 1024), "shots copy = same variant")
    c.check(variant_info("Crash JP.rldchar") == ("Crash JP", None, None), "unknown name kept")

    # ---- 1. the ladder, keyed by load-end lines: t160k and t320k fail
    res = run_eval(build_ladder(os.path.join(root, "r1")))
    by = {v["name"]: v for v in res["variants"]}
    L = {r["label"]: r for r in res["loads"]}
    c.check(res["keyed"] and not res["warnings"], "keyed path: no warning")
    c.check(res["load_lines"] == 9 and res["load_end_lines"] == 9 and len(res["variants"]) == 9, "9 loads, 9 load-end lines, 9 variants")
    t10 = L["testdriver-t10k-x2048"]
    c.check(t10["race_frames"] == 2305 and t10["window"] == 2000 and t10["n"] == 2000, "start 300 / end 5 trimmed: 2305 -> 2000 (got %s/%s/%s)" %
            (t10["race_frames"], t10["window"], t10["n"]))
    c.check(t10["max"] < 50, "filler rows (99 ms) are not in max")
    c.check(ok(L["testdriver-t160k-x2048"]["p99"], 30.0), "t160k CPU+GPU p99 = 30 (got %s)" % L["testdriver-t160k-x2048"]["p99"])
    c.check(by["testdriver-t160k-x2048"]["status"] == "FAIL" and by["testdriver-t80k-x2048"]["status"] == "PASS", "t160k FAIL, t80k PASS")
    c.check(res["triangle_limit"] == 80000 and res["loader_constant"] == 64000, "limit 80000 -> constant 64000 (got %s/%s)" %
            (res["triangle_limit"], res["loader_constant"]))
    c.check(by["testdriver-t320k-x2048"]["status"] == "FAIL", "t320k FAIL")
    c.check(res["edge_limit"] == 2048, "edge limit 2048 (got %s)" % res["edge_limit"])
    c.check(by["other model"]["triangles"] is None and by["other model"]["status"] == "PASS", "unknown name kept and judged")
    c.check(ok(t10["gpu_p99"], 0.4 * t10["p99"]) and ok(t10["cpu_p99"], 0.6 * t10["p99"]) and ok(t10["work_p99"], 0.6 * t10["p99"] + SYNTH_SWAP),
            "gpu / cpu / work p99 reported as information (cpu = work - swap)")

    # ---- 2. the same run without load-end lines: order proxy and its warning, same verdict
    res2 = run_eval(build_ladder(os.path.join(root, "r2"), keyed=False))
    c.check((not res2["keyed"]) and any("ORDER" in w for w in res2["warnings"]), "fallback path warns about the order proxy")
    c.check(res2["triangle_limit"] == 80000 and res2["loader_constant"] == 64000 and len(res2["variants"]) == 9, "fallback gives the same verdict")

    # ---- 3. GPU offset: frame_index + offset
    r3 = SynthRun(os.path.join(root, "r3"), gpu_offset=2)
    r3.menu()
    r3.load(7, "testdriver-t10k-x2048.rldchar", [0.0] * 2000, gpu_linear=True)
    a = Args()
    a.gpu_offset = 2
    res3 = run_eval(r3, a)
    t = res3["loads"][0]
    last = 300 + 2000 - 1  # window rows are race rows 300..2299; the first race row has index first
    first_idx = r3.idx - (300 + 2000 + 5)
    c.check(t["n"] == 2000 and ok(t["max"], 5.0 + 0.001 * (first_idx + last)), "gpu offset 2 matches the right GPU rows (max %s)" % t["max"])
    res3b = run_eval(r3, Args())  # offset 0 on the same files
    c.check(not ok(res3b["loads"][0]["max"], t["max"]), "a wrong offset gives other sums")
    c.check(res3b["loads"][0]["n"] == 2000 - 0 or res3b["loads"][0]["n"] < 2000, "wrong offset still sums only frames with a GPU row")

    # ---- 4. frames without a GPU row are not summed
    r4 = SynthRun(os.path.join(root, "r4"))
    r4.menu()
    r4.load(7, "testdriver-t10k-x2048.rldchar", dist(2000, 5.0, 6.0, 1), gpu_missing_every=10)
    res4 = run_eval(r4)
    c.check(res4["loads"][0]["n"] == 1800 and res4["loads"][0]["no_gpu_row"] == 200, "missing GPU rows: 1800 summed, 200 without (got %s/%s)" %
            (res4["loads"][0]["n"], res4["loads"][0].get("no_gpu_row")))

    # ---- 5. readback frames and shot frames (V-2, V-1, V) are dropped
    r5 = SynthRun(os.path.join(root, "r5"))
    r5.menu()
    r5.load(7, "testdriver-t10k-x2048.rldchar", dist(2000, 5.0, 6.0, 1), readback_at=(10, 500))
    p5, lg5 = r5.write()
    first = r5.idx - (300 + 2000 + 5)
    v1 = r5.vb(first + 300 + 100)       # a shot at the VBlank of window row 100: rows with cb V-2 and V (30 Hz) fall away
    v2 = r5.vb(first + 300 + 900)
    with open(lg5, "a") as fh:
        fh.write("[CTR Shot] vblank %d: SCREENSHOT.BMP (asked for 1)\n" % v1)
        fh.write("[CTR Shot] series 3 of 10 at vblank %d (due %d): a-f00002.bmp\n" % (v2, v2))
    rows5 = read_rows(find_csv(p5))
    loads5, ends5, shots5 = read_log(lg5)
    res5 = evaluate(loads5, rows5, ends5, shots5, read_gpu(find_csv(p5)), Args())
    d5 = res5["loads"][0]
    c.check(shots5 == [v1, v2], "both [CTR Shot] line forms parsed (got %s)" % shots5)
    c.check(d5["dropped"] == 2 + 2 * 2 and d5["n"] == 2000 - 6, "2 readback + 2x2 shot rows dropped (got dropped %s, n %s)" % (d5["dropped"], d5["n"]))

    # ---- 6. --start / --end change the window
    a6 = Args()
    a6.start, a6.end = 310, 0
    res6 = run_eval(build_ladder(os.path.join(root, "r6")), a6)
    t6 = {r["label"]: r for r in res6["loads"]}["testdriver-t10k-x2048"]
    c.check(t6["window"] == 2305 - 310 and t6["max"] >= 99.0, "--start 310 --end 0: 1995 rows, tail filler kept (window %s, max %s)" % (t6["window"], t6["max"]))

    # ---- 7. too few clean frames, and a load without any key / segment
    r7 = SynthRun(os.path.join(root, "r7"))
    r7.menu()
    r7.load(7, "testdriver-t10k-x2048.rldchar", dist(500, 5.0, 6.0, 1))
    r7.menu()
    r7.log.append("[CTR Char] dev seats: cycle, load 2 on level 9, 8 of 8 seats = testdriver-t20k-x2048.rldchar (1 draw bytes), bots on template 14")
    res7 = run_eval(r7)
    b7 = {v["name"]: v for v in res7["variants"]}
    c.check(b7["testdriver-t10k-x2048"]["status"] == "FEW" and res7["triangle_limit"] is None, "too few clean frames: FEW, no limit")
    c.check(b7["testdriver-t20k-x2048"]["status"] == "NODATA" and any("no '[CTR Perf] load end'" in w for w in res7["warnings"]), "load without key: NODATA + warning")

    # ---- 8. a missing variant in the fallback path, and an unbound race before a bound one
    r8 = SynthRun(os.path.join(root, "r8"), keyed=False)
    r8.menu()
    r8.load(3, "unbound.rldchar", dist(600, 5.0, 6.0, 1), bound=False)
    r8.menu()
    r8.load(7, "testdriver-t10k-x2048.rldchar", dist(2000, 5.0, 12.0, 10))
    r8.menu()
    r8.log.append("[CTR Char] dev seats: cycle, load 2 on level 9, 8 of 8 seats = testdriver-t20k-x2048.rldchar (1 draw bytes), bots on template 14")
    res8 = run_eval(r8)
    b8 = {v["name"]: v for v in res8["variants"]}
    c.check(res8["unmatched_segments"] == 1 and b8["testdriver-t10k-x2048"]["n"] == 2000, "fallback skips the unbound race by level id")
    c.check(b8["testdriver-t20k-x2048"]["status"] == "NODATA" and res8["triangle_limit"] == 10000, "fallback: missing variant NODATA, limit stays 10000")

    # ---- 9. a file loaded twice: <name>#2, the load with more clean frames decides
    r9 = SynthRun(os.path.join(root, "r9"))
    r9.menu()
    r9.load(7, "testdriver-t10k-x2048.rldchar", dist(2000, 5.0, 6.0, 1))
    r9.menu()
    r9.load(7, "testdriver-t20k-x2048.rldchar", dist(2000, 6.0, 7.0, 1))
    r9.menu()
    r9.load(7, "testdriver-t10k-x2048.rldchar", dist(1900, 5.0, 30.0, 40))   # fewer frames and bad: must not decide
    res9 = run_eval(r9)
    l9 = {r["label"]: r for r in res9["loads"]}
    c.check(len(res9["loads"]) == 3 and len(res9["variants"]) == 2 and "testdriver-t10k-x2048#2" in l9, "duplicate listed as <name>#2, 2 variants")
    c.check(l9["testdriver-t10k-x2048"]["used"] and not l9["testdriver-t10k-x2048#2"]["used"], "the load with the most clean frames is used")
    c.check({v["name"]: v for v in res9["variants"]}["testdriver-t10k-x2048"]["status"] == "PASS", "verdict from the larger load: PASS")

    # ---- 10. edge ladder at 40k: 512 fails, 1024 passes -> edge limit 2048 with a note
    r10 = SynthRun(os.path.join(root, "r10"))
    r10.menu()
    for t in (10, 20, 40):
        r10.load(7, "testdriver-t%dk-x2048.rldchar" % t, dist(2000, 5.0, 12.0, 10))
        r10.menu()
    r10.load(7, "testdriver-t40k-x512.rldchar", dist(2000, 5.0, 30.0, 40))
    r10.menu()
    r10.load(7, "testdriver-t40k-x1024.rldchar", dist(2000, 5.0, 12.0, 10))
    res10 = run_eval(r10)
    c.check(res10["triangle_limit"] == 40000 and res10["edge_limit"] == 2048 and res10["edge_note"] is not None, "edge limit 2048, non-monotonic noted")

    # ---- 11. the eval command: output, last line, JSON, exit codes, memory
    a = Args()
    run = build_ladder(os.path.join(root, "r11"))
    a.perf, a.log = run.write()
    a.json = os.path.join(root, "out.json")
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cmd_eval(a)
    lines = buf.getvalue().strip().splitlines()
    c.check(rc == 0 and lines[-1] == "hires_budget_eval: 9 variants, triangle limit 80000, texture edge limit 2048", "eval last line (got %r)" % lines[-1])
    c.check(os.path.isfile(a.json) and json.load(open(a.json))["loader_constant"] == 64000, "json written")
    a2 = Args()
    a2.perf = a.perf
    a2.log = os.path.join(root, "empty.log")
    with open(a2.log, "w") as fh:
        fh.write("[CTR Native] Initialising platform\n[CTR Char] dev seats: not bound (demo)\n")
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cmd_eval(a2)
    c.check(rc == 2 and "no '[CTR Char] dev seats" in buf.getvalue(), "log without load lines -> exit 2")
    a3 = Args()
    a3.perf, a3.log = os.path.join(root, "nonexistent"), a.log
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = cmd_eval(a3)
    c.check(rc == 2, "missing perf folder -> exit 2")
    mem = os.path.join(root, "memory.csv")
    with open(mem, "w") as fh:
        fh.write("# comment\nzeit_ms,pid,private,workingset,peak_workingset,virtual,peak_virtual,gpu_dedicated,gpu_shared\n"
                 "1,1,104857600,209715200,209715200,0,0,0,0\n2,1,314572800,209715200,209715200,0,0,0,0\n")
    m = read_memory([mem])
    c.check(m is not None and m["private_peak_mib"] == 300.0 and m["workingset_peak_mib"] == 200.0, "memory peak read")

    print("hires_budget_eval selftest: %d checks, %d failures" % (c.n, c.fail))
    return 0 if c.fail == 0 else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="CPU+GPU p99 per hi-res driver variant and a budget proposal")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("eval")
    e.add_argument("--perf", required=True)
    e.add_argument("--log", required=True)
    e.add_argument("--start", type=int, default=START_FRAMES)
    e.add_argument("--end", type=int, default=END_FRAMES)
    e.add_argument("--gpu-offset", type=int, default=0)
    e.add_argument("--limit-ms", type=float, default=LIMIT_MS)
    e.add_argument("--margin", default=MARGIN)
    e.add_argument("--min-frames", type=int, default=MIN_FRAMES)
    e.add_argument("--memory", default=None)
    e.add_argument("--json", default=None)
    sub.add_parser("selftest")
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return selftest()
    return cmd_eval(args)


if __name__ == "__main__":
    sys.exit(main())
