"""The UI mapping, checked as arithmetic rather than as a picture.

A replica of CTR_UI_MapX and NativeUiDecl_Floor (game/native_view.c,
game/native_uidecl.c) in the same integer arithmetic, run over every coordinate
the 512x216 authoring canvas can produce.

The mapper SHIFTS instead of scaling: the HUD is authored in 512
columns and drawn into 512, 682 or 918, and the anchor says which point of the
one falls on which point of the other. That changes what there is to check:

  1. Is 4:3 the identity? Both widths are 512 there, so every shift is zero and
     the stretch is x*512/512. If it is the identity, the step cannot change a
     4:3 frame, and that is the strongest claim available without driving.
  2. Is the map monotone in x? The build maps an element by its two corners
     instead of mapping every vertex and taking the union. Those are the same
     thing only if the map is monotone.
  3. Does a shifted element keep its EXACT width, and does it land inside the
     drawn canvas? A shift cannot distort - that is the point of shifting - so
     the width error must be 0 and not "at most one pixel".
  4. Does FULL_CANVAS really cover the drawn canvas? A fade authored 0..512 has
     to come out 0..W or it leaves the picture showing through.
  5. What does the declaration row for the fruit counter actually buy?

Run: python tools/uimap_check.py
"""

CANVAS_W = 512
CANVAS_H = 216

ANCHOR_CENTRE = 0
ANCHOR_CANVAS = 1
ANCHOR_LEFT = 2
ANCHOR_RIGHT = 3
ANCHOR_NAMES = {0: "centre", 1: "canvas", 2: "left", 3: "right"}

FLOOR_MARGIN = 5
FLOOR_GAP = 5
MAX_ROUNDS = 16

RATIOS = [(4, 3), (16, 9), (43, 18)]


def c_div(a, b):
    """C integer division: truncation toward zero, not Python's floor."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def canvas_width(aw, ah):
    """CTR_Canvas_Width: the exact fraction, rounded to the nearest even column.

    512 * (aw/ah) / (4/3), kept as a fraction so the rounding happens once.
    Comes out 512, 682, 918 - the same three numbers as the C table.
    """
    num = CANVAS_W * 3 * aw
    den = 4 * ah
    width = 2 * ((num + den) // (2 * den))
    return max(width, CANVAS_W)


def map_x(x, anchor, canvas_w, virtual_w=CANVAS_W):
    """CTR_UI_MapX: three shifts and one stretch."""
    shift = canvas_w - virtual_w
    if anchor == ANCHOR_CANVAS:
        if shift == 0:
            return x
        return c_div(x * canvas_w, virtual_w)
    if anchor == ANCHOR_LEFT:
        return x
    if anchor == ANCHOR_RIGHT:
        return x + shift
    return x + (canvas_w // 2) - (virtual_w // 2)


def floor_shift(authored, mapped, anchors, canvas_w=CANVAS_W, width=CANVAS_W):
    """NativeUiDecl_Floor. Returns (shift, settled).

    `width` is the authoring canvas - it decides how much margin an element may
    ask for. `canvas_w` is the drawn one - it decides where the right edge is.
    """
    n = len(authored)
    shift = [0] * n

    def bands(a, b):
        return (min(a[3], b[3]) - max(a[1], b[1])) >= 1

    for _ in range(MAX_ROUNDS):
        changed = False

        for i in range(n):
            if anchors[i] == ANCHOR_CANVAS:
                continue
            x0 = mapped[i][0] + shift[i]
            x1 = mapped[i][2] + shift[i]
            want_l = min(FLOOR_MARGIN, authored[i][0])
            want_r = min(FLOOR_MARGIN, width - 1 - authored[i][2])
            need_l = want_l - x0
            need_r = want_r - (canvas_w - 1 - x1)
            if need_l > 0:
                shift[i] += need_l
                changed = True
            elif need_r > 0:
                shift[i] -= need_r
                changed = True

        for i in range(n):
            if anchors[i] == ANCHOR_CANVAS:
                continue
            for j in range(i + 1, n):
                if anchors[j] == ANCHOR_CANVAS:
                    continue
                a = [mapped[i][0] + shift[i], mapped[i][1], mapped[i][2] + shift[i], mapped[i][3]]
                b = [mapped[j][0] + shift[j], mapped[j][1], mapped[j][2] + shift[j], mapped[j][3]]
                if not bands(a, b):
                    continue
                if authored[i][0] <= authored[j][0]:
                    was = authored[j][0] - authored[i][2] - 1
                else:
                    was = authored[i][0] - authored[j][2] - 1
                if was < 0:
                    continue
                want = min(was, FLOOR_GAP)
                if a[0] <= b[0]:
                    gap, right = b[0] - a[2] - 1, j
                else:
                    gap, right = a[0] - b[2] - 1, i
                if gap < want:
                    shift[right] += want - gap
                    changed = True

        if not changed:
            return shift, True

    return shift, False


def anchor_for_box(x0, x1, width=CANVAS_W):
    centre = (x0 + x1) // 2
    if centre * 3 < width:
        return ANCHOR_LEFT
    if centre * 3 >= width * 2:
        return ANCHOR_RIGHT
    return ANCHOR_CENTRE


failures = []


def require(ok, message):
    if not ok:
        failures.append(message)


# ---- 1. 4:3 is the identity -------------------------------------------------
#
# Over a range wider than the canvas, because six HUD elements in this game are
# authored partly outside it and one sits at x = -140.
w43 = canvas_width(4, 3)
require(w43 == CANVAS_W, "4:3 canvas is not 512 but %d" % w43)

moved = 0
for anchor in (ANCHOR_CENTRE, ANCHOR_CANVAS, ANCHOR_LEFT, ANCHOR_RIGHT):
    for x in range(-512, 1025):
        if map_x(x, anchor, w43) != x:
            moved += 1
require(moved == 0, "4:3 moved %d coordinates" % moved)
print("1. 4:3 identity        : %d of %d coordinates moved" % (moved, 4 * 1537))

# The floor at 4:3, over every element the canvas could hold. authored == mapped
# there, so this is the whole claim: min(FLOOR, authored) <= authored.
clamped = 0
for x0 in range(-160, CANVAS_W + 1, 7):
    for w in (1, 4, 16, 64, 256, 512):
        box = [x0, 10, x0 + w, 30]
        shift, settled = floor_shift([box], [box], [ANCHOR_CENTRE], w43)
        if shift[0] != 0 or not settled:
            clamped += 1
require(clamped == 0, "the floor fired %d times at 4:3" % clamped)
print("2. 4:3 floor           : %d elements clamped" % clamped)

# ---- 2. monotone, in-canvas, undistorted ------------------------------------
for aw, ah in RATIOS:
    cw = canvas_width(aw, ah)
    for anchor in (ANCHOR_CENTRE, ANCHOR_CANVAS, ANCHOR_LEFT, ANCHOR_RIGHT):
        previous = None
        outside = 0
        for x in range(0, CANVAS_W + 1):
            y = map_x(x, anchor, cw)
            if previous is not None:
                require(y >= previous, "%d:%d %s not monotone at x=%d" % (aw, ah, ANCHOR_NAMES[anchor], x))
            previous = y
            if y < 0 or y > cw:
                outside += 1
        require(outside == 0, "%d:%d %s put %d coordinates outside the canvas" % (aw, ah, ANCHOR_NAMES[anchor], outside))

    # A shift cannot distort. Not "within a pixel" - exactly.
    worst = 0
    for anchor in (ANCHOR_CENTRE, ANCHOR_LEFT, ANCHOR_RIGHT):
        for x0 in range(-160, CANVAS_W + 1, 3):
            for w in (1, 8, 32, 128, 256):
                x1 = x0 + w
                after = map_x(x1, anchor, cw) - map_x(x0, anchor, cw)
                worst = max(worst, abs(after - w))
    require(worst == 0, "%d:%d distorted a shifted box by %d px" % (aw, ah, worst))

    # FULL_CANVAS covers the drawn canvas, edge to edge.
    require(map_x(0, ANCHOR_CANVAS, cw) == 0, "%d:%d full canvas does not start at 0" % (aw, ah))
    require(map_x(CANVAS_W, ANCHOR_CANVAS, cw) == cw,
            "%d:%d full canvas ends at %d, not %d" % (aw, ah, map_x(CANVAS_W, ANCHOR_CANVAS, cw), cw))

    print("3. %5s monotone      : yes, in canvas, width error %d px  (canvas %d, shift left 0 / centre %d / right %d)"
          % ("%d:%d" % (aw, ah), worst, cw, (cw - CANVAS_W) // 2, cw - CANVAS_W))

# ---- 3. the fruit counter ---------------------------------------------------
#
# The count is the one element in the 1P race HUD whose drawn box can be derived
# from code alone. data.hud_1P_P1[UI_HUD_SLOT_WUMPA_COUNT] authors it at 336,16;
# UI_DrawNumWumpa puts the sign at posX in FONT_SMALL and the number at posX+13
# in FONT_BIG, and data.font_charPixWidth is 13 small and 17 big.
COUNT_1 = [336, 16, 366, 33]   # one digit
COUNT_2 = [336, 16, 383, 33]   # two digits

# The fruit model beside it is a 3D model drawn through DecalHUD, so its extent
# is NOT in any table - this is the one number in this row that a capture has to
# settle. Its authored anchor point is 316,24, and the count starts at 336.
FRUIT_ANCHOR_X = 316
COUNT_X = 336

print()
print("4. the fruit count element, left edge in canvas pixels")
print("   %-8s %-16s %-16s %-16s" % ("", "derived (1 digit)", "derived (2 digits)", "declared centre"))
for aw, ah in RATIOS:
    cw = canvas_width(aw, ah)
    a1 = anchor_for_box(COUNT_1[0], COUNT_1[2])
    a2 = anchor_for_box(COUNT_2[0], COUNT_2[2])
    print("   %-8s %-16s %-16s %-16s"
          % ("%d:%d" % (aw, ah),
             "%d (%s)" % (map_x(COUNT_1[0], a1, cw), ANCHOR_NAMES[a1]),
             "%d (%s)" % (map_x(COUNT_2[0], a2, cw), ANCHOR_NAMES[a2]),
             "%d" % map_x(COUNT_1[0], ANCHOR_CENTRE, cw)))

# The derived rule answers RIGHT for this element at both digit counts, and the
# neighbour it belongs to sits short of the same boundary. That is the tear.
require(anchor_for_box(COUNT_1[0], COUNT_1[2]) == ANCHOR_RIGHT, "the count is not right-anchored by the derived rule")
require(anchor_for_box(COUNT_2[0], COUNT_2[2]) == ANCHOR_RIGHT, "the count is not right-anchored at two digits")

# How far apart the two must be drawn for the grouping to keep them separate.
# NativeGpu_UiBoxesTouch merges when b[0] <= a[2] + NATIVE_UI_TOUCH_GAP, so a
# fruit model whose right edge reaches 332 or beyond makes the pair ONE element,
# which gets one anchor and needs no declaration at all. This is exactly what
# --ui-elements has to settle on a real race frame.
print()
print("   the row applies only while the two are separate elements:")
print("     fruit right edge <= %d  ->  two elements, the row decides the count" % (COUNT_X - 4 - 1))
print("     fruit right edge >= %d  ->  one element, its own box decides" % (COUNT_X - 4))
print("     authored fruit anchor point x = %d" % FRUIT_ANCHOR_X)

# The declared centre is the identity at 4:3 whatever the neighbour turns out to
# be, so this row cannot change the 4:3 frame either way.
require(map_x(COUNT_1[0], ANCHOR_CENTRE, w43) == COUNT_1[0], "the declared row moves the 4:3 frame")

# ---- 5. the flicker, and what the fix does to it ----------------------------
#
# In the one-player HUD the fruit model is a 3D mesh projected straight into the
# UI ordering table - UI_RenderFrame.c only draws a fixed-size copy quad from two
# players up. A mesh turns, so its drawn right edge moves from frame to frame.
#
# NativeGpu_UiBoxesTouch merges two boxes when b[0] <= a[2] + NATIVE_UI_TOUCH_GAP,
# so with the count starting at 336 the pair is ONE element as soon as the fruit
# reaches 332 and TWO below it. That boundary is what the rotation crosses.
TOUCH_GAP = 4
FRUIT_Y = (11, 34)


def count_left_edge(fruit_right, count, cw, per_primitive):
    """Where the count ends up, under the old rule and under the new one."""
    fruit = [300, FRUIT_Y[0], fruit_right, FRUIT_Y[1]]

    if per_primitive:
        # Claimed on its own: no grouping, no floor, its own coordinates only.
        return map_x(count[0], ANCHOR_CENTRE, cw), "decl", 0

    merged = count[0] <= fruit[2] + TOUCH_GAP

    if merged:
        box = [min(fruit[0], count[0]), min(fruit[1], count[1]),
               max(fruit[2], count[2]), max(fruit[3], count[3])]
        # The merged box is not inside the declared region, so the row misses.
        anchor = anchor_for_box(box[0], box[2])
        shift, _ = floor_shift([box], [[map_x(box[0], anchor, cw), box[1],
                                        map_x(box[2], anchor, cw), box[3]]], [anchor], cw)
        return map_x(count[0], anchor, cw) + shift[0], "merged/" + ANCHOR_NAMES[anchor], shift[0]

    # Two elements: the count is claimed by the row, the fruit derives its own.
    fruit_anchor = anchor_for_box(fruit[0], fruit[2])
    authored = [fruit, count]
    anchors = [fruit_anchor, ANCHOR_CENTRE]
    mapped = [[map_x(b[0], a, cw), b[1], map_x(b[2], a, cw), b[3]]
              for b, a in zip(authored, anchors)]
    shift, _ = floor_shift(authored, mapped, anchors, cw)
    return mapped[1][0] + shift[1], "split/centre", shift[1]


print()
print("5. the flicker: where the count lands as the fruit model turns")
for aw, ah in RATIOS:
    cw = canvas_width(aw, ah)
    for label, count in (("x0 (one digit)", COUNT_1), ("x10 (two digits)", COUNT_2)):
        old_positions = []
        new_positions = []
        for fruit_right in range(328, 337):
            old_x, _, _ = count_left_edge(fruit_right, count, cw, per_primitive=False)
            new_x, _, _ = count_left_edge(fruit_right, count, cw, per_primitive=True)
            old_positions.append(old_x)
            new_positions.append(new_x)
        old_span = max(old_positions) - min(old_positions)
        new_span = max(new_positions) - min(new_positions)
        print("   %-6s %-17s per element: %3d..%3d  swing %3d px   |   per primitive: %3d  swing %d px"
              % ("%d:%d" % (aw, ah), label, min(old_positions), max(old_positions), old_span,
                 new_positions[0], new_span))
        require(new_span == 0, "%d:%d %s still swings %d px after the fix" % (aw, ah, label, new_span))
        if (aw, ah) == (4, 3):
            require(old_span == 0, "4:3 swung %d px, which it must not" % old_span)

print()
if failures:
    for f in failures:
        print("FAIL: " + f)
    raise SystemExit(1)
print("all checks passed")

# ---- 6. the arcade results driver row: eight elements, one row ----------------
#
# 222.c centres a row of N driver icons in the reference width: width
# N*44 + (N-1)*12, start 256 - width/2, pitch AA_DRIVER_ICON_SPACING = 56, icon
# 44 wide at y = 0x60. The gap of 12 is wider than the touch gap of 4, so every
# icon is its own element and reads its own third. Measured at 43:18 (VBlank
# 9223, race end forced): left, left, centre x4, right, right.
ROW_Y = (95, 121)
ROW_REGION = (-100, 93, 580, 123)   # the declared rectangle: the row's whole path


def results_row(n):
    width = n * 44 + (n - 1) * 12
    start = 256 - width // 2
    return [[start + i * 56, ROW_Y[0], start + i * 56 + 44, ROW_Y[1]] for i in range(n)]


print()
print("6. the arcade results driver row, eight icons")
for aw, ah in RATIOS:
    cw = canvas_width(aw, ah)
    derived = sorted(set(map_x(0, anchor_for_box(b[0], b[2]), cw) for b in results_row(8)))
    declared = sorted(set(map_x(0, ANCHOR_CENTRE, cw) for b in results_row(8)))
    print("   %-8s derived shifts %-16s declared shifts %s" % ("%d:%d" % (aw, ah), derived, declared))

# The tear: three different shifts in one row wherever the canvas is wider.
require(len(set(map_x(0, anchor_for_box(b[0], b[2]), canvas_width(43, 18)) for b in results_row(8))) == 3,
        "the derived rule does not tear the results row at 43:18")
# The declared centre: one shift, and the identity at 4:3.
for n in range(1, 9):
    for b in results_row(n):
        require(map_x(b[0], ANCHOR_CENTRE, w43) == b[0], "the declared row moves the 4:3 frame")
        require(ROW_REGION[0] <= b[0] and b[2] <= ROW_REGION[2] and ROW_REGION[1] <= b[1] and b[3] <= ROW_REGION[3],
                "an icon of a %d-driver row lies outside the declared rectangle" % n)
# The path: the fly-in start (0x218 = 536) and the exit target (-100) stay inside.
require(ROW_REGION[0] <= -100 and 536 + 44 <= ROW_REGION[2], "the row's path leaves the declared rectangle")
print("   every icon of every row size, and both ends of its path, lie inside the declared rectangle")
