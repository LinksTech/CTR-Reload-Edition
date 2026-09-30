# The three view-setting sets, checked against the C.
#
# A replica of the descriptor table and the setter from game/native_view.c. It
# exists to answer one question with a number instead of an opinion: with the
# sets as they stand, is every expression in the render path the same one it was?
#
#   python tools/viewsets_check.py
#
# Exit code 0 when every case holds.

import re
import sys
import os

SOURCE = os.path.join(os.path.dirname(__file__), "..", "game", "native_view.c")

# The five call sites the settings reach, and what each one computed before.
# Written out here rather than derived, because "the same expression it was" is a
# claim about code that no longer exists and can only be kept by hand.
CALL_SITES = [
    ("PushBuffer.c        one-viewport projection distance", "fov", lambda v: v, 0x100),
    ("PushBuffer.c        frustum vertical half-extent (rect.h 216)", "vertical",
     lambda v: (216 * 0x600) // v // 2, (216 * 0x600) // 0x360 // 2),
    ("PushBuffer.c        frustum corner distance", "far", lambda v: v, 0x100),
    ("226_00              depth clip threshold (d = 0x100)", "nearshift",
     lambda v: (0x100 >> v) + 1, (0x100 >> 1) + 1),
    ("RenderBucket        bucket near reject (d = 0x100)", "nearshift",
     lambda v: 0x100 >> v, 0x100 >> 1),
]

failures = []


def read_source():
    with open(SOURCE, encoding="utf-8") as handle:
        return handle.read()


text = read_source()

print("1. THE DEFINES THE WHOLE THING HANGS ON")
print("   Four numbers, spelled once. If the descriptor table or the set macro")
print("   ever stopped reading them, three sets could drift apart in silence.")
defines = dict(re.findall(r"#define CTR_VIEW_STOCK_(\w+)\s+(0x[0-9a-fA-F]+)", text))
for name in ("FOV", "VERTICAL", "FAR", "NEAR_SHIFT"):
    if name not in defines:
        failures.append("CTR_VIEW_STOCK_%s is not defined" % name)
        print("   CTR_VIEW_STOCK_%-11s MISSING" % name)
    else:
        print("   CTR_VIEW_STOCK_%-11s %s" % (name, defines[name]))
print()

print("2. THE DESCRIPTOR TABLE")
print("   Keys unique, labels short enough for the panel, stock inside range,")
print("   and a step that can actually reach both ends.")
rows = re.findall(r'\{"(\w+)", "([^"]+)", (CTR_VIEW_STOCK_\w+), (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+)\},', text)
if len(rows) != 4:
    failures.append("expected 4 descriptor rows, found %d" % len(rows))

seen_keys = set()
for key, label, stock_name, minimum, maximum, step in rows:
    stock = int(defines.get(stock_name.replace("CTR_VIEW_STOCK_", ""), "0"), 16)
    minimum = int(minimum, 16)
    maximum = int(maximum, 16)
    step = int(step, 16)

    notes = []
    if key in seen_keys:
        notes.append("DUPLICATE KEY")
    seen_keys.add(key)
    if len(label) > 9:
        notes.append("LABEL TOO LONG")
    if not (minimum <= stock <= maximum):
        notes.append("STOCK OUTSIDE RANGE")
    if step <= 0:
        notes.append("STEP NOT POSITIVE")
    if (stock - minimum) % step != 0:
        notes.append("STEP CANNOT REACH STOCK FROM MIN")

    print("   %-10s %-9s stock %5d  range %5d..%-5d step %3d  %s"
          % (key, label, stock, minimum, maximum, step, " ".join(notes) if notes else "OK"))
    failures.extend("%s: %s" % (key, note) for note in notes)
print()

print("3. THREE SETS, AND WHETHER THEY CAN DIFFER")
count = text.count("    CTR_VIEW_STOCK_SET,")
print("   uses of CTR_VIEW_STOCK_SET in the initialiser: %d" % count)
if count != 3:
    failures.append("expected 3 uses of CTR_VIEW_STOCK_SET, found %d" % count)
else:
    print("   Three uses of one macro, so the three sets are identical by")
    print("   construction rather than by inspection.")
print()

print("4. EVERY CALL SITE, AT STOCK")
print("   The number the render path gets now, against the number the literal")
print("   it replaced produced. These must be equal, or the picture moved.")
stock_by_key = {}
for key, _label, stock_name, _mn, _mx, _st in rows:
    stock_by_key[key] = int(defines[stock_name.replace("CTR_VIEW_STOCK_", "")], 16)

for name, key, compute, before in CALL_SITES:
    now = compute(stock_by_key[key])
    ok = now == before
    print("   %-52s %6d vs %6d  %s" % (name, now, before, "OK" if ok else "MOVED"))
    if not ok:
        failures.append("%s: %d != %d" % (name, now, before))
print()

print("5. WHAT THE NEAR-PLANE SHIFT DOES WHEN TURNED")
print("   Printed because it is the one setting whose steps are not linear, and")
print("   a menu row that steps 0,1,2,3,4 is not obviously a distance.")
for shift in range(0, 5):
    print("   shift %d -> near plane at %4d of a %d projection distance" % (shift, 0x100 >> shift, 0x100))
print()

if failures:
    print("FAILURES:")
    for line in failures:
        print("  " + line)
    sys.exit(1)

print("All cases hold: three sets, identical by construction, every call site unmoved.")
