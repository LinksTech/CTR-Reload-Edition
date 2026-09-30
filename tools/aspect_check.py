# Which of the three shapes a display is, checked against the C.
#
# A replica of CTR_View_RatioMilli / CTR_View_NearestMode from
# game/native_view.c - integer only, the same truncation, the same tie rule.
# It exists so the boundaries can be read off rather than argued about: the
# answer for a 16:10 or a 2:1 panel is a consequence of "nearest", and a
# consequence nobody has printed is a guess.
#
#   python tools/aspect_check.py
#
# Exit code 0 when every case holds.

import sys

# Row 0 is the reference aspect. Same order as the C.
MODES = [("4:3", 4, 3), ("16:9", 16, 9), ("43:18", 43, 18)]


def ratio_milli(width, height):
    if width <= 0 or height <= 0:
        return 0
    return (width * 1000) // height


def nearest_mode(width, height):
    ratio = ratio_milli(width, height)
    if ratio <= 0:
        return -1

    best = -1
    best_distance = 0
    for index, (_, aspect_w, aspect_h) in enumerate(MODES):
        distance = abs(ratio - ratio_milli(aspect_w, aspect_h))
        if best < 0 or distance < best_distance:
            best = index
            best_distance = distance
    return best


failures = []


def expect(width, height, wanted, note=""):
    got = MODES[nearest_mode(width, height)][0] if nearest_mode(width, height) >= 0 else "none"
    ratio = ratio_milli(width, height)
    ok = got == wanted
    if not ok:
        failures.append("%dx%d -> %s, wanted %s" % (width, height, got, wanted))
    print("  %-11s ratio %d.%03d  -> %-6s %s%s"
          % ("%dx%d" % (width, height), ratio // 1000, ratio % 1000, got,
             "OK " if ok else "FAIL ", note))


print("1. THE THREE THEMSELVES")
print("   Each mode's own ratio has to pick that mode, or the table and the")
print("   arithmetic disagree about what they mean.")
for name, aspect_w, aspect_h in MODES:
    print("   %-6s = %d.%03d" % (name, ratio_milli(aspect_w, aspect_h) // 1000,
                                 ratio_milli(aspect_w, aspect_h) % 1000))
print()

print("2. REAL PANELS")
expect(640, 480, "4:3", "the authored shape")
expect(1024, 768, "4:3")
expect(1280, 1024, "4:3", "5:4, 1.250 - nearer 1.333 than 1.777")
expect(1920, 1080, "16:9")
expect(2560, 1440, "16:9")
expect(3840, 2160, "16:9")
expect(1920, 1200, "16:9", "16:10, 1.600 - a reported case")
expect(3440, 1440, "43:18", "2.388 exactly, the reason the row exists")
expect(2560, 1080, "43:18", "sold as 21:9, actually 2.370")
expect(3840, 1600, "43:18", "2.400")
expect(3840, 1080, "43:18", "32:9, 3.555 - off the end, still nearest")
expect(2048, 1024, "16:9", "2:1, 2.000 - the other reported case")
print()

print("3. WHERE THE ANSWER TURNS OVER")
print("   Nearest puts the boundary at the midpoint. Printed rather than")
print("   assumed: a boundary nobody measured is a threshold somebody guessed.")
for low, high, name_low, name_high in ((1333, 1777, "4:3", "16:9"),
                                       (1777, 2388, "16:9", "43:18")):
    midpoint = (low + high) // 2
    # Search the turnover on a 1000-high panel, so width in pixels is the ratio.
    turn = None
    for width in range(low, high + 1):
        if MODES[nearest_mode(width, 1000)][0] == name_high:
            turn = width
            break
    print("   %-6s / %-6s  midpoint %d.%03d  turns over at %d.%03d"
          % (name_low, name_high, midpoint // 1000, midpoint % 1000,
             turn // 1000, turn % 1000))
    if abs(turn - midpoint) > 1:
        failures.append("turnover %d is not the midpoint %d" % (turn, midpoint))
print()

print("4. NOTHING, AND NONSENSE")
print("   A display SDL cannot size must not silently become 4:3.")
for width, height in ((0, 0), (1920, 0), (-1, 100), (0, 1080)):
    mode = nearest_mode(width, height)
    print("   %5dx%-5d -> %s %s" % (width, height, mode, "OK" if mode == -1 else "FAIL"))
    if mode != -1:
        failures.append("%dx%d answered %d instead of -1" % (width, height, mode))
print()

print("5. TRUNCATION")
print("   The ratio is truncated to thousandths. The question is whether that")
print("   can ever move an answer: it can only do so within 1/1000 of a")
print("   boundary, and the boundaries are 222 and 305 thousandths from the")
print("   nearest mode.")
worst = min(min(abs(ratio_milli(1555, 1000) - ratio_milli(w, h)),
                abs(ratio_milli(2082, 1000) - ratio_milli(w, h)))
            for _, w, h in MODES)
print("   nearest mode to either boundary: %d thousandths away" % worst)
if worst < 2:
    failures.append("truncation is within reach of a boundary")
print()

if failures:
    print("FAILURES:")
    for line in failures:
        print("  " + line)
    sys.exit(1)

print("All cases hold.")
