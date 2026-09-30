"""The present resolve, checked as arithmetic.

A replica of NativeRenderer_BuildResolve and the tap loop in
ctr_present_target_shader. It answers three questions:

  1. Where the window is at least as large as the internal picture, is the pass
     still the single tap it was? If it is, nothing that ran at factor 1 or on a
     large monitor can have changed.
  2. At an integer reduction, do the taps land on the source texel centres - all
     of them, each once? That is what makes the result the exact box average
     rather than an approximation.
  3. Is the box centred on the output pixel? Anything else is a half-texel
     drift, which reads as the picture having moved.

Run: python tools/resolve_check.py
"""

TAPS_MAX = 4  # CTR_RESOLVE_TAPS_MAX in platform/native_shaders.inc

failures = []


def require(ok, message):
    if not ok:
        failures.append(message)


def build_resolve(source_w, source_h, dest_w, dest_h):
    """NativeRenderer_BuildResolve. Returns (tapsX, tapsY, stepX_uv, stepY_uv)."""
    ratio_x = source_w / dest_w
    ratio_y = source_h / dest_h
    taps_x = ((source_w + dest_w - 1) // dest_w) if source_w > dest_w else 1
    taps_y = ((source_h + dest_h - 1) // dest_h) if source_h > dest_h else 1
    taps_x = min(taps_x, TAPS_MAX)
    taps_y = min(taps_y, TAPS_MAX)
    step_x = (ratio_x / taps_x) / source_w if taps_x > 1 else 0.0
    step_y = (ratio_y / taps_y) / source_h if taps_y > 1 else 0.0
    return taps_x, taps_y, step_x, step_y


def sample_texels(source_w, dest_w, dest_pixel):
    """Where the shader taps land, in source texels, for one output pixel."""
    taps_x, _, step_x, _ = build_resolve(source_w, 1, dest_w, 1)
    uv = (dest_pixel + 0.5) / dest_w
    return [(uv + (i + 0.5 - taps_x * 0.5) * step_x) * source_w for i in range(taps_x)]


# ---- 1. no reduction, no change ---------------------------------------------
#
# Every case where the presentation viewport is at least as wide and tall as the
# internal target. The single tap has to be at the output pixel centre exactly.
print("1. window at least as large as the internal picture")
for source, dest in [((512, 216), (1920, 1080)), ((1024, 432), (1920, 1080)),
                     ((2048, 864), (3440, 1440)), ((512, 216), (512, 216))]:
    taps_x, taps_y, step_x, step_y = build_resolve(source[0], source[1], dest[0], dest[1])
    require((taps_x, taps_y, step_x, step_y) == (1, 1, 0.0, 0.0),
            "%s into %s is not a single tap: %s" % (source, dest, (taps_x, taps_y, step_x, step_y)))
    print("   %-12s -> %-12s  taps %dx%d, step %.1f/%.1f" % (source, dest, taps_x, taps_y, step_x, step_y))

# ---- 2. integer reduction is the exact box ----------------------------------
print()
print("2. integer reduction: do the taps land on the source texel centres")
for factor in (2, 3, 4):
    dest_w = 480
    source_w = dest_w * factor
    taps_x, _, _, _ = build_resolve(source_w, 1, dest_w, 1)
    require(taps_x == factor, "factor %d asked for %d taps" % (factor, taps_x))

    covered = []
    for pixel in range(dest_w):
        covered.extend(sample_texels(source_w, dest_w, pixel))

    # Every source texel centre, each hit exactly once, and nothing else.
    wanted = sorted(k + 0.5 for k in range(source_w))
    got = sorted(covered)
    worst = max(abs(a - b) for a, b in zip(wanted, got))
    require(len(got) == source_w, "factor %d covered %d of %d texels" % (factor, len(got), source_w))
    require(worst < 1e-3, "factor %d missed a texel centre by %.4f" % (factor, worst))
    print("   x%d: %d taps, all %d source texel centres hit once, worst miss %.6f texel"
          % (factor, taps_x, source_w, worst))

# ---- 3. the box is centred --------------------------------------------------
print()
print("3. the box is centred on the output pixel, so nothing drifts")
worst_drift = 0.0
for source_w, dest_w in [(2048, 1920), (2048, 960), (1536, 640), (4096, 1280), (2048, 1000)]:
    for pixel in (0, dest_w // 3, dest_w // 2, dest_w - 1):
        taps = sample_texels(source_w, dest_w, pixel)
        centre = (taps[0] + taps[-1]) / 2.0
        want = ((pixel + 0.5) / dest_w) * source_w
        worst_drift = max(worst_drift, abs(centre - want))
print("   worst drift over five source/window pairs: %.6f texel" % worst_drift)
require(worst_drift < 1e-3, "the box drifted by %.4f texel" % worst_drift)

# ---- 4. what the cap costs --------------------------------------------------
print()
print("4. where the cap of %d taps bites" % TAPS_MAX)
for source_w, dest_w in [(2048, 512), (4096, 640), (4096, 512)]:
    want = (source_w + dest_w - 1) // dest_w
    taps_x, _, _, _ = build_resolve(source_w, 1, dest_w, 1)
    note = "capped" if want > TAPS_MAX else "exact"
    print("   %5d source -> %4d window: wants %d taps, uses %d (%s)" % (source_w, dest_w, want, taps_x, note))

# ---- 5. what actually happens on a real screen ------------------------------
#
# The PSX display is 512x216 here - SetDefDispEnv in game/MAIN/MainMain.c, 0x200
# by 0xd8 - so the internal target is 512*k by 216*k. The presentation viewport is
# the window with the aspect letterboxed into it.
print()
print("5. real configurations: 512x216 internal, viewport as the aspect gives it")
print("   %-8s %-6s %-12s %-12s %-10s" % ("window", "factor", "internal", "viewport", "resolve"))
for label, view_w, view_h in [("1080p 16:9", 1920, 1080), ("1440p 16:9", 2560, 1440), ("3440x1440", 3440, 1440)]:
    for factor in (1, 2, 4):
        source = (512 * factor, 216 * factor)
        taps_x, taps_y, _, _ = build_resolve(source[0], source[1], view_w, view_h)
        kind = "1 tap" if (taps_x, taps_y) == (1, 1) else "%dx%d box" % (taps_x, taps_y)
        print("   %-8s x%-5d %-12s %-12s %-10s"
              % (label if factor == 1 else "", factor, "%dx%d" % source, "%dx%d" % (view_w, view_h), kind))

print()
if failures:
    for f in failures:
        print("FAIL: " + f)
    raise SystemExit(1)
print("all checks passed")
