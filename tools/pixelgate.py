#!/usr/bin/env python3
"""Picture gate: compare two window screenshots, with a tolerance of 1/255 per channel.

    python tools/pixelgate.py BEFORE AFTER [--tolerance 1]

The picture of textured semi-transparency in PSX mode 0 is not
bit-identical to the older two-pass form: the factor 0.5 arrives
as an 8-bit fragment output (second blend source) instead of as a floating-point
blend constant, and 0.3 % of the pixels deviate by exactly 1/255 in one
channel. Both are approximations of the console's computation (B + F) / 2, neither
is bit-identical to the PS1 - a deliberate decision.

So that the picture gate still holds, it counts two things separately:
  - pixels that differ AT ALL (number and share), and
  - pixels that differ by MORE than the tolerance - that is the
    finding. Return value 0 if there is none, otherwise 1.

The hard gate (0 pixels) stays reachable: the run with --semi-two-pass
draws the two-pass form, bit-identical to the earlier version; compare
against it with --tolerance 0 (the default is 1). Two screenshots can only be paired if both runs
have the same line 'jumps to ... at vblank N' in the log (measurement anchor).
"""
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError:
    print("pixelgate: Pillow is missing (pip install pillow)")
    sys.exit(2)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    a, b = Path(argv[1]), Path(argv[2])
    tolerance = 1
    if "--tolerance" in argv:
        tolerance = int(argv[argv.index("--tolerance") + 1])
    A = Image.open(a).convert("RGB")
    B = Image.open(b).convert("RGB")
    if A.size != B.size:
        print(f"pixelgate: sizes differ {A.size} against {B.size} - not comparable")
        return 1
    diff = ImageChops.difference(A, B)
    total = A.size[0] * A.size[1]
    differing = 0
    over = 0
    worst = 0
    for px in diff.getdata():
        m = max(px)
        if m:
            differing += 1
            if m > worst:
                worst = m
            if m > tolerance:
                over += 1
    print(f"pixelgate: {a.name} against {b.name}, {A.size[0]}x{A.size[1]}")
    print(f"  different: {differing} of {total} pixels ({100.0 * differing / total:.3f} %), largest deviation {worst}/255")
    print(f"  over tolerance {tolerance}/255: {over} pixels - {'FINDING' if over else 'no finding'}")
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
