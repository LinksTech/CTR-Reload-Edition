"""What one frame costs in fragments, per internal resolution factor.

Not a benchmark - a count. Every pass in the frame writes a known number of
pixels, and which of them grow with the factor is a property of the code rather
than of the machine. That makes the shape of the answer knowable without
driving, and leaves only the constant of proportionality - how long a fragment
takes on this GPU - for the measured run to supply.

The four passes, and where each number comes from:

  scene      the game geometry, drawn into the main target. The target is
             512x216 (SetDefDispEnv, game/MAIN/MainMain.c) times the factor in
             each axis, so this is the only pass that grows with k**2. Times an
             overdraw factor D, which is the one number here that a measurement
             has to supply.

  pack       NativeRenderer_StoreFrameBuffer packs the target back into the
             16-bit VRAM texture. It draws into VRAM, which is never scaled, so
             this is 512x216 whatever the factor is.

  present    NativeRenderer_PresentMainTarget covers the presentation viewport,
             which is the window, times the resolve taps. Constant in k except
             where the box turns on.

  pages      NativeRenderer_FlushPageStore, one draw per stale tile, each
             covering a whole tile of (256 * pageScale)**2. Independent of the
             internal factor entirely - it is the PAGE scale that drives it.

Run: python tools/frame_cost.py
"""

DISPLAY_W = 512
DISPLAY_H = 216

# Measured over 300 frames and reported by NativeRenderer_ReportPageStore. This
# is the count read off a race: the framebuffers and the packed textures
# share pages, so the framebuffer store dirties them every frame.
TILE_FILLS_PER_FRAME = 12

SCREENS = [("1920x1080", 1920, 1080), ("2560x1440", 2560, 1440), ("3440x1440", 3440, 1440)]


def resolve_taps(source, dest):
    return ((source + dest - 1) // dest) if source > dest else 1


def frame(k, page_scale, view_w, view_h, overdraw):
    source_w = DISPLAY_W * k
    source_h = DISPLAY_H * k
    taps = resolve_taps(source_w, view_w) * resolve_taps(source_h, view_h)

    scene = DISPLAY_W * DISPLAY_H * k * k * overdraw
    pack = DISPLAY_W * DISPLAY_H
    present = view_w * view_h * taps
    pages = TILE_FILLS_PER_FRAME * (256 * page_scale) ** 2
    return scene, pack, present, pages


def million(n):
    return "%.2f" % (n / 1e6)


for overdraw in (2, 4):
    print()
    print("=== overdraw D = %d ===" % overdraw)
    for label, view_w, view_h in SCREENS:
        print()
        print("%s, page store x1" % label)
        print("  %-8s %-9s %-8s %-9s %-8s %-9s %s" % ("factor", "scene", "pack", "present", "pages", "total", "vs x1"))
        base = None
        for k in (1, 2, 4, 8):
            scene, pack, present, pages = frame(k, 1, view_w, view_h, overdraw)
            total = scene + pack + present + pages
            if base is None:
                base = total
            print("  x%-7d %-9s %-8s %-9s %-8s %-9s %.2fx"
                  % (k, million(scene), million(pack), million(present), million(pages), million(total), total / base))

print()
print("=== what the page store scale costs, on its own ===")
print("  %-12s %-12s %s" % ("page scale", "tile size", "fills per frame"))
for p in (1, 2, 4):
    print("  x%-11d %-12s %s M fragments" % (p, "%dx%d" % (256 * p, 256 * p), million(TILE_FILLS_PER_FRAME * (256 * p) ** 2)))

print()
print("=== where a fill could be skipped ===")
#
# The two framebuffers, from SetDefDrawEnv in game/MAIN/MainMain.c:
#   db[0] at 0,0    512x216   -> VRAM rows   0..215
#   db[1] at 0,296  512x216   -> VRAM rows 296..511
# A page is 64 VRAM words wide and 256 rows tall, so VRAM is 16 pages across and
# 2 down. The framebuffers therefore live in page row 0 (pages 0..7) and page
# row 1 (pages 16..23), and the free strips a texture can be packed into are the
# rows the framebuffers leave over inside those same pages.
FB = [(0, 0, 216), (1, 296, 216)]
PAGE_ROWS = 256
for page_row, fb_y, fb_h in FB:
    page_top = page_row * PAGE_ROWS
    dirty_lo = max(fb_y, page_top) - page_top
    dirty_hi = min(fb_y + fb_h, page_top + PAGE_ROWS) - page_top
    dirty = dirty_hi - dirty_lo
    print("  page row %d: the framebuffer dirties rows %d..%d of 256 - %d rows, %.0f%% of the tile"
          % (page_row, dirty_lo, dirty_hi - 1, dirty, 100.0 * dirty / PAGE_ROWS))
print("  filling only the dirty rows would save the rest, which is %.0f%% of one fill." % (100.0 * (256 - 216) / 256))
print("  The strip a texture is packed into is exactly those leftover rows, so the")
print("  saving and the texture data are the same 40 rows: the fill has to write")
print("  the 216 rows nobody samples in order to keep the 40 that are.")
