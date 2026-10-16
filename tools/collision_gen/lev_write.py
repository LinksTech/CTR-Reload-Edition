"""Byte-level helpers for a LEV file: the body with its pointer map, s16
coordinates, and the small texture set (VRM) a generated collision model
carries.

A LEV file is: u32 body size, the body (the level as the game sees it in
memory, pointers as offsets into the body), u32 size of the pointer map in
bytes, then the pointer map (the offset of every pointer slot). The loader
relocates every listed slot (LOAD_RunPtrMap, game/LOAD/LOAD_Assets.c); the
container check refuses a slot listed twice or pointing outside the body
(Rld_CheckLev, include/rldtrack.inc).
"""

import struct


class Body:
    """The LEV body: bytes plus the list of pointer slots (the pointer map)."""

    def __init__(self):
        self.b = bytearray()
        self.ptrs = []

    def alloc(self, size, align=4):
        while len(self.b) % align:
            self.b.append(0)
        off = len(self.b)
        self.b.extend(bytes(size))
        return off

    def put(self, fmt, off, *vals):
        struct.pack_into('<' + fmt, self.b, off, *vals)

    def ptr(self, slot, target):
        """Write a pointer at slot; target None = NULL (not mapped)."""
        if target is None:
            self.put('I', slot, 0)
            return
        if not 0 <= target <= len(self.b):
            raise ValueError('pointer target %d outside the body (%d bytes)' % (target, len(self.b)))
        if slot in self.ptrs:
            raise ValueError('pointer slot %d written twice' % slot)
        self.put('I', slot, target)
        self.ptrs.append(slot)

    def lev_bytes(self):
        """The finished LEV file: body size, body, pointer map (sorted)."""
        body = bytes(self.b)
        pmap = sorted(self.ptrs)
        return (struct.pack('<I', len(body)) + body + struct.pack('<I', 4 * len(pmap)) +
                struct.pack('<%dI' % len(pmap), *pmap))


def clamp16(v, what='coordinate'):
    """v rounded to an integer that must fit an s16 (LevVertex.pos is SVec3)."""
    r = int(round(v))
    if not -32768 <= r <= 32767:
        raise ValueError('%s %r does not fit 16 bits (-32768..32767)' % (what, v))
    return r


# ------------------------------------------------------------------ texture
#
# One 4-bit texture page of our own (no game data): a road tile, an edge
# (grass) tile and a start line tile, each 64x64, with 16-colour CLUTs.

TILE_ROAD = 0
TILE_EDGE = 1
TILE_START = 2
TILE_COUNT = 3

VRAM_X, VRAM_Y = 512, 0     # texel page (4-bit: 64 halfwords = 256 texels wide)
CLUT_X, CLUT_Y = 576, 0     # CLUT rows, right of the texels


def _rgb15(r, g, b):
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000  # STP set: opaque even for black


def texture_page():
    """Texels of the 4-bit page (192 texels used of 256, 64 rows) and the CLUTs."""
    road = [_rgb15(70 + 6 * i, 70 + 6 * i, 78 + 6 * i) for i in range(16)]
    edge = [_rgb15(40 + 4 * i, 110 + 7 * i, 40 + 3 * i) for i in range(16)]
    line = [_rgb15(20, 20, 20)] * 8 + [_rgb15(235, 235, 235)] * 8
    tex = bytearray(128 * 64)  # 256 texels across, 2 per byte
    seed = 12345
    for y in range(64):
        for x in range(256):
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            n = (seed >> 16) & 7
            tile = x // 64
            u = x % 64
            if tile == TILE_ROAD:       # asphalt noise with a checker, to see the collision surface
                c = 4 + (n >> 1) + (3 if ((u // 16) + (y // 16)) % 2 else 0)
            elif tile == TILE_EDGE:     # grass noise
                c = 3 + n + (2 if (y // 8 + u // 8) % 2 else 0)
            elif tile == TILE_START:    # 8x8 checker
                c = 0 if ((u // 8) + (y // 8)) % 2 else 12
            else:
                c = 0
            if x & 1:
                tex[y * 128 + x // 2] |= c << 4
            else:
                tex[y * 128 + x // 2] |= c
    return bytes(tex), [road, edge, line]


def tim_block(x, y, w, h, data):
    """A TIM with only a pixel block (flags 2 = 15-bit), as a VRM chain link."""
    blen = 12 + len(data)
    return struct.pack('<IIIHHHH', 0x10, 2, blen, x, y, w, h) + data


def make_vrm():
    """The VRM: a chain (magic 0x20) of the texel block and the CLUT block."""
    tex, cluts = texture_page()
    clut_bytes = b''.join(struct.pack('<16H', *c) for c in cluts)
    links = [tim_block(VRAM_X, VRAM_Y, 64, 64, tex), tim_block(CLUT_X, CLUT_Y, 16, len(cluts), clut_bytes)]
    out = struct.pack('<I', 0x20)
    for t in links:
        out += struct.pack('<I', len(t)) + t
    return out + struct.pack('<I', 0)


def layout_bytes(tile):
    """TextureLayout (12 bytes) for a whole 64x64 tile: corners TL, TR, BL, BR."""
    u0 = tile * 64
    u1 = u0 + 63
    tpage = (VRAM_X // 64) | ((VRAM_Y // 256) << 4)
    clut = (CLUT_X // 16) | ((CLUT_Y + tile) << 6)
    return struct.pack('<BBHBBHBBBB', u0, 0, clut, u1, 0, tpage, u0, 63, u1, 63)
