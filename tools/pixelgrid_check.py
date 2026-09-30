"""The pixel grid, checked as arithmetic.

A replica of four things in platform/native_renderer.c:
NativeRenderer_UpdatePresentationViewport, NativeRenderer_MapAxis,
NativeRenderer_BlockPattern and the one-tap case of NativeRenderer_BuildResolveBox.

It answers four questions:

  1. Does the factor path still produce exactly the rectangles it produced when
     it multiplied by an integer? The clip box and the clear box stopped
     multiplying by a factor and started mapping against the target's own size.
     If those two are not the same number for every edge at every factor, a
     picture that worked yesterday moves today.

  2. At NATIVE, is the last step really one texel per pixel? Source equals
     destination has to give one tap and a zero offset, or the acceptance line
     in the present report is saying something it cannot back up.

  3. How does a game pixel land on the window, per axis? Where that is not a
     whole number the blocks cannot all be the same size, and no filter changes
     that - a filter only decides whether the difference is an edge or a blur.
     This prints the pattern and its period for the three shapes.

  4. What the scene pass costs, in fragments, per shape.

Run: python tools/pixelgrid_check.py
"""

DISPLAY_W = 512  # dispEnv.disp.w, game/MAIN/MainMain.c
DISPLAY_H = 216  # dispEnv.disp.h
RES_SCALE_MAX = 8  # NATIVE_RES_SCALE_MAX in platform/native_renderer.c

SHAPES = (("4:3", 4, 3), ("16:9", 16, 9), ("43:18", 43, 18))

failures = []


def require(ok, message):
    if not ok:
        failures.append(message)


def viewport(window_w, window_h, aspect_w, aspect_h):
    """NativeRenderer_UpdatePresentationViewport, integer division and all."""
    w = window_w
    h = (w * aspect_h) // aspect_w
    if h > window_h:
        h = window_h
        w = (h * aspect_w) // aspect_h
    return max(w, 1), max(h, 1)


def map_axis(value, from_size, to_size):
    """NativeRenderer_MapAxis."""
    if from_size <= 0:
        return value
    return (value * to_size) // from_size


def block_pattern(src_size, dst_size):
    """NativeRenderer_BlockPattern. Returns (narrow, wide, wide_count, period)."""
    if src_size <= 0 or dst_size <= 0:
        return 0, 0, 0, 0
    divisor = _gcd(src_size, dst_size)
    period = src_size // divisor
    per_period = dst_size // divisor
    return per_period // period, per_period // period + 1, per_period % period, period


def _gcd(a, b):
    while b:
        a, b = b, a % b
    return a


def build_resolve(source_w, source_h, dest_w, dest_h):
    """The taps half of NativeRenderer_BuildResolveBox."""
    taps_x = ((source_w + dest_w - 1) // dest_w) if source_w > dest_w else 1
    taps_y = ((source_h + dest_h - 1) // dest_h) if source_h > dest_h else 1
    step_x = 0.0 if taps_x == 1 else 1.0
    step_y = 0.0 if taps_y == 1 else 1.0
    return taps_x, taps_y, step_x, step_y


# --- 1. the factor path does not move a pixel --------------------------------
#
# Exhaustive, not sampled. Every edge and every width a rectangle in a 512x216
# frame can have, at every factor the setting reaches.

print("1. THE FACTOR PATH, EVERY EDGE AT EVERY FACTOR")
for scale in range(1, RES_SCALE_MAX + 1):
    for size in (DISPLAY_W, DISPLAY_H):
        target = size * scale
        for x in range(size + 1):
            require(
                map_axis(x, size, target) == x * scale,
                "edge %d of %d at x%d: mapped %d, multiplied %d" % (x, size, scale, map_axis(x, size, target), x * scale),
            )
        for x in range(size + 1):
            for w in range(size + 1 - x):
                mapped = map_axis(x + w, size, target) - map_axis(x, size, target)
                require(
                    mapped == w * scale,
                    "width %d at %d of %d at x%d: mapped %d, multiplied %d" % (w, x, size, scale, mapped, w * scale),
                )
    print("   x%d  %d edges and every width in both axes: identical" % (scale, DISPLAY_W + DISPLAY_H + 2))

# --- 2. one texel per pixel when the sizes match -----------------------------

print()
print("2. THE LAST STEP WHEN SOURCE EQUALS DESTINATION")
for window in ((3440, 1440), (1920, 1080), (2560, 1440), (3840, 2160), (800, 600), (640, 480)):
    for name, aw, ah in SHAPES:
        vw, vh = viewport(window[0], window[1], aw, ah)
        taps_x, taps_y, step_x, step_y = build_resolve(vw, vh, vw, vh)
        require(
            (taps_x, taps_y, step_x, step_y) == (1, 1, 0.0, 0.0),
            "%dx%d %s: %d x %d taps, step %s %s" % (window[0], window[1], name, taps_x, taps_y, step_x, step_y),
        )
print("   6 windows x 3 shapes: 1 tap, offset 0, every one of them")

# --- 3. how a game pixel lands on the window ---------------------------------

print()
print("3. THE BLOCK PATTERN AT NATIVE")
for window in ((3440, 1440), (2560, 1440), (1920, 1080), (3840, 2160)):
    print("   window %dx%d" % window)
    for name, aw, ah in SHAPES:
        vw, vh = viewport(window[0], window[1], aw, ah)
        px = vw / DISPLAY_W
        py = vh / DISPLAY_H
        nx, wx, cx, px_period = block_pattern(DISPLAY_W, vw)
        ny, wy, cy, py_period = block_pattern(DISPLAY_H, vh)
        shape = px / py
        even_x = "even, every one %d" % nx if cx == 0 else "%d of %d are %d, %d are %d" % (cx, px_period, wx, px_period - cx, nx)
        even_y = "even, every one %d" % ny if cy == 0 else "%d of %d are %d, %d are %d" % (cy, py_period, wy, py_period - cy, ny)
        print(
            "     %-6s viewport %4dx%-4d  game px %6.3f x %6.3f  shape %.4f\n"
            "            x: %-28s y: %s" % (name, vw, vh, px, py, shape, even_x, even_y)
        )

# --- 4. what the scene pass costs --------------------------------------------

print()
print("4. SCENE FRAGMENTS PER PASS, 3440x1440")
base = DISPLAY_W * DISPLAY_H
print("   x1      %9d   1.00x   (%dx%d)" % (base, DISPLAY_W, DISPLAY_H))
for scale in (2, 4, 8):
    px = base * scale * scale
    print("   x%-6d %9d   %.2fx   (%dx%d)" % (scale, px, px / base, DISPLAY_W * scale, DISPLAY_H * scale))
for name, aw, ah in SHAPES:
    vw, vh = viewport(3440, 1440, aw, ah)
    px = vw * vh
    print("   NATIVE %-4s %8d   %.2fx   (%dx%d)   %.2fx of x4" % (name, px, px / base, vw, vh, px / (base * 16)))

# --- 5. the 216-of-240-lines question, as a number ---------------------------
#
# dispEnv.screen says the picture sits on 216 of a 240-line frame at an offset of
# 12 - so on a console it fills nine tenths of the height, and the presentation
# here stretches it over the whole of it instead. The open question is whether
# THAT is what makes the pixels non-square. It is not, and this says by how much:
# closing it moves the shape number, it does not move it to one.
#
# Square pixels need the viewport's aspect to equal the buffer's own 512:216.
# Nothing but a shape near 2.370 gets there, whatever is done about the lines.

print()
print("5. WHAT (d) WOULD CHANGE - 216 OF 240 LINES, ON 3440x1440")
print("   buffer 512x216 is %.4f wide to tall; square pixels need the viewport at that" % (DISPLAY_W / DISPLAY_H))
for name, aw, ah in SHAPES:
    vw, vh = viewport(3440, 1440, aw, ah)
    now = (vw / DISPLAY_W) / (vh / DISPLAY_H)
    # The same shape with the height cut to the 216 of 240 lines the game asks for.
    lines_w, lines_h = aw * 240, ah * 216
    dw, dh = viewport(3440, 1440, lines_w, lines_h)
    then = (dw / DISPLAY_W) / (dh / DISPLAY_H)
    print(
        "   %-6s today viewport %4dx%-4d shape %.4f  ->  with the bars %4dx%-4d shape %.4f"
        % (name, vw, vh, now, dw, dh, then)
    )

print()
if failures:
    print("FAILED: %d" % len(failures))
    for message in failures[:20]:
        print("  " + message)
    raise SystemExit(1)

print("All checks passed.")
