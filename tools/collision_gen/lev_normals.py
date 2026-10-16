"""Collision normals of a quadblock, computed the way the game computes them.

The game builds the normal of each of a quadblock's ten triangles in
COLL_FIXED_TRIANGL_GetNormVec (game/COLL.c) with 32-bit integers:

    c = (v3 - v1) x (v2 - v1)                        (GTE OP, no shift)
    n = ((c >> lodShift) * dividend) >> bitShift     (lodShift 0 for the eight
                                                      fine triangles, 2 for the
                                                      two coarse ones)

The product must fit an s32. If it wraps, the normal flips and the floor
points down - a kart driven by a player falls through it (bots do not use this
collision, so autopilot does not show it). Every product therefore stays below
2^30, and the fine normals get the length 4096, the coarse ones 1024.

Triangle tables: game/COLL.c, COLL_FIXED_QUADBLK_TestTriangles (high LOD: the
eight triangles of the 3x3 vertex grid, low LOD: two over the corners).
"""

import math

HI_TRIS = ((0, 4, 5), (4, 6, 5), (6, 4, 1), (5, 6, 2), (8, 6, 7), (7, 3, 8), (1, 7, 6), (2, 6, 8))
LO_TRIS = ((0, 1, 2), (1, 3, 2))
PRODUCT_MAX = 1 << 30
DIVIDEND_MAX = 0x7FFF


def s32(v):
    """The value wrapped to a signed 32-bit integer."""
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def game_cross(p1, p2, p3):
    """(p3 - p1) x (p2 - p1), the cross product the game takes for (v1, v2, v3)."""
    ax, ay, az = (p3[i] - p1[i] for i in range(3))
    bx, by, bz = (p2[i] - p1[i] for i in range(3))
    return (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)


def game_normal(p1, p2, p3, lod_shift, dividend, bit_shift):
    """The normal exactly as the game computes it, with the 32-bit wrap."""
    return [s32((s32(c) >> lod_shift) * dividend) >> bit_shift for c in game_cross(p1, p2, p3)]


def quad_triangles():
    """(vertex triple, lod shift, dividend index) of the ten triangles of a quadblock."""
    return [(HI_TRIS[k], 0, k) for k in range(8)] + [(LO_TRIS[k], 2, 8 + k) for k in range(2)]


def normal_dividends(ps):
    """bitShift and the ten dividends of a quadblock (ps = its 9 vertices as
    integers). Raises ValueError when no shift keeps every product in range."""
    tris = quad_triangles()
    crosses = [game_cross(ps[a], ps[b], ps[c]) for (a, b, c), _, _ in tris]
    lengths = [math.sqrt(sum(v * v for v in c)) for c in crosses]
    for shift in range(24, -1, -1):
        divs = []
        ok = True
        for (_, lod, _), c, length in zip(tris, crosses, lengths):
            d = int(round(4096 * (1 << shift) / length)) if length > 0 else 0
            if d > DIVIDEND_MAX or any(abs(v >> lod) * d >= PRODUCT_MAX for v in c):
                ok = False
                break
            divs.append(d)
        if ok:
            return shift, divs
    raise ValueError('no collision normal of this quadblock fits 32 bits: %r' % (ps,))
