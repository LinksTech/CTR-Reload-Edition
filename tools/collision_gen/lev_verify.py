"""Checks a generated LEV the way the game reads it.

verify_lev() reads only the LEV bytes (body, pointer map, level head, mesh,
quadblocks, BSP, restart points, nav paths), plus optionally the height
surface the LEV was built from for a height test. It returns a dict: 'errors'
(a list of strings, empty = everything fine) and the figures of each check.

Checks (numbers appear in the error strings):
  1  pointer map: ascending, no duplicates, slots inside the body, targets <= body size
  2  quad vertex indices below numVert; stored quad box = min/max of its 9 corners
  3  collision normals, in the game's 32-bit arithmetic (lev_normals): no overflow,
     length 4096 (fine) / 1024 (coarse) +-5 %, ground faces up, walls and curtains upright
  4  seams: no half-edge shared by more than two ground quads, no two ground
     vertices at one position; the number of open edges is reported
  5  BSP: root is a branch, every node reachable exactly once, depth <= 40,
     every quad in exactly one leaf, boxes of leaves and branches contain their contents
  6  restart points: count, link indices, the ring from point 0, distance order,
     ground quads name a restart point, the floor search finds ground at each named point
  7  nav paths: three, magic, point counts, distances, go-back counts; 'nav_jumps'
  8  invisible quads: walls and curtains must be invisible; visible/invisible ground counted
  9  the game's floor search: ground_hit()
  10 height test against the surface (only with a surface)
  11 layers: two ground hits 1..160 apart at one point are a conflict

Which query takes which way: checks 5 and 6 walk the real BSP (the same walk as
the game's search, LevModel.bsp_candidates). The mass queries of checks 10 and 11
use a grid over the ground quads in x/z (cells of 512), which is much faster and
gives the same triangles. ground_hit() takes either way (use_bsp).

At most 5 single messages per check are reported, followed by one summary line
when there were more.
"""

import math
import struct

import lev_normals
from lev_normals import HI_TRIS, game_cross, game_normal, quad_triangles, s32

FLAG_GROUND = 0x1000
FLAG_COLLISION_SURFACE = 0x2000
FLAG_KILL_PLANE = 0x0200
FLAG_NO_COLLISION_RESPONSE = 0x0010
FLAG_TRIGGER = 0x0040

NO_NODE = 0xFF
MAX_DEPTH = 40
LAP_MIN = 33200
GRID_CELL = 512
MAX_MESSAGES = 5
NAV_MAGIC = -0x1303
CONFLICT_MIN = 1.0
CONFLICT_MAX = 160.0
CONFLICT_WINDOW = 2000.0
EPS = 1e-9
DECAL = 12.0                # road layers this close count as one (decals lie up to ~12 over the road)


class _Unreadable(ValueError):
    """The LEV is too damaged to read the structure a check needs."""


class _Errors:
    def __init__(self):
        self.items = []
        self.counts = {}

    def add(self, check, msg):
        n = self.counts.get(check, 0) + 1
        self.counts[check] = n
        if n <= MAX_MESSAGES:
            self.items.append('check %d: %s' % (check, msg))

    def result(self):
        out = list(self.items)
        for check in sorted(self.counts):
            if self.counts[check] > MAX_MESSAGES:
                out.append('check %d: %d problems in total' % (check, self.counts[check]))
        return out


def _u32(b, o):
    if o < 0 or o + 4 > len(b):
        raise _Unreadable('read outside the body at %d' % o)
    return struct.unpack_from('<I', b, o)[0]


def _unpack(fmt, b, o):
    n = struct.calcsize('<' + fmt)
    if o < 0 or o + n > len(b):
        raise _Unreadable('read outside the body at %d' % o)
    return struct.unpack_from('<' + fmt, b, o)


class LevModel:
    """The LEV parsed the way the game reads it."""

    def __init__(self, lev):
        self.lev = bytes(lev)
        lev = self.lev
        if len(lev) < 12:
            raise _Unreadable('file shorter than 12 bytes')
        self.n = struct.unpack_from('<I', lev, 0)[0]
        if 4 + self.n + 4 > len(lev):
            raise _Unreadable('body size %d does not fit the file (%d bytes)' % (self.n, len(lev)))
        self.b = lev[4:4 + self.n]
        b = self.b
        self.map_bytes = struct.unpack_from('<I', lev, 4 + self.n)[0]
        self.map_slots = []
        if 4 + self.n + 4 + self.map_bytes > len(lev):
            raise _Unreadable('pointer map of %d bytes does not fit the file' % self.map_bytes)
        cnt = self.map_bytes // 4
        self.map_slots = list(struct.unpack_from('<%dI' % cnt, lev, 4 + self.n + 4))
        # level head
        self.mesh_off = _u32(b, 0x00)
        self.num_cp = _u32(b, 0x148)
        self.cp_off = _u32(b, 0x14C)
        self.nav_tab = _u32(b, 0x188)
        self.vis_off = _u32(b, 0x190)
        if not 0 < self.mesh_off <= self.n - 0x20:
            raise _Unreadable('mesh_info pointer %d outside the body' % self.mesh_off)
        mo = self.mesh_off
        self.nq = _u32(b, mo + 0x00)
        self.nv = _u32(b, mo + 0x04)
        self.quad_off = _u32(b, mo + 0x0C)
        self.vert_off = _u32(b, mo + 0x10)
        self.bsp_off = _u32(b, mo + 0x18)
        self.nbsp = _u32(b, mo + 0x1C)
        if self.vert_off + 16 * self.nv > self.n or self.quad_off + 0x5C * self.nq > self.n:
            raise _Unreadable('vertex or quad array outside the body')
        if self.bsp_off + 0x20 * self.nbsp > self.n:
            raise _Unreadable('BSP array outside the body')
        self.verts = [_unpack('3h', b, self.vert_off + 16 * i) for i in range(self.nv)]
        self.quads = []
        for qi in range(self.nq):
            o = self.quad_off + 0x5C * qi
            q = dict(off=o)
            q['idx'] = _unpack('9H', b, o)
            q['flags'] = _unpack('H', b, o + 0x12)[0]
            q['draw'] = _u32(b, o + 0x14)
            q['tex'] = _unpack('5I', b, o + 0x1C)[0:4] + (_u32(b, o + 0x40),)
            q['box'] = _unpack('6h', b, o + 0x2C)
            q['terrain'] = b[o + 0x38]
            q['cp'] = b[o + 0x3E]
            q['shift'] = b[o + 0x3F]
            q['divs'] = _unpack('10h', b, o + 0x48)
            self.quads.append(q)
        self.nodes = []
        for ni in range(self.nbsp):
            o = self.bsp_off + 0x20 * ni
            flag, nid = _unpack('Hh', b, o)
            box = _unpack('6h', b, o + 4)
            nd = dict(leaf=bool(flag & 1), id=nid, box=box)
            if nd['leaf']:
                nd['count'] = _u32(b, o + 0x18)
                nd['qptr'] = _u32(b, o + 0x1C)
            else:
                nd['kids'] = _unpack('4H', b, o + 0x18)
            self.nodes.append(nd)
        self._tris = {}
        self._grid = None

    # ------------------------------------------------------------ classification
    @staticmethod
    def kind(q):
        f = q['flags']
        if f & FLAG_GROUND:
            return 'ground'
        if f & FLAG_KILL_PLANE:
            return 'curtain'
        if f & FLAG_COLLISION_SURFACE:
            return 'wall'
        return 'other'

    def is_invisible(self, q):
        for p in q['tex']:
            if p + 12 > self.n or self.b[p:p + 12] != bytes(12):
                return False
        return True

    # ------------------------------------------------------------ floor search
    def _floor_tris(self, qi):
        """The eight fine triangles of a quad as (x0, y0, z0, e1, e2, inv_det)
        tuples, vertical ones left out."""
        t = self._tris.get(qi)
        if t is not None:
            return t
        q = self.quads[qi]
        ps = [self.verts[i] if i < self.nv else (0, 0, 0) for i in q['idx']]
        t = []
        for a, bb, c in HI_TRIS:
            x0, y0, z0 = ps[a]
            x1, y1, z1 = ps[bb]
            x2, y2, z2 = ps[c]
            e1x, e1y, e1z = x1 - x0, y1 - y0, z1 - z0
            e2x, e2y, e2z = x2 - x0, y2 - y0, z2 - z0
            det = e1x * e2z - e2x * e1z
            if det == 0:
                continue
            t.append((x0, y0, z0, e1x, e1y, e1z, e2x, e2y, e2z, 1.0 / det))
        self._tris[qi] = t
        return t

    def quad_hits(self, qi, x, z):
        """Heights of the quad's fine triangles at (x, z); the border counts."""
        q = self.quads[qi]
        bx = q['box']
        if x < bx[0] or x > bx[3] or z < bx[2] or z > bx[5]:
            return []
        out = []
        for x0, y0, z0, e1x, e1y, e1z, e2x, e2y, e2z, inv in self._floor_tris(qi):
            px = x - x0
            pz = z - z0
            u = (px * e2z - pz * e2x) * inv
            if u < -EPS or u > 1 + EPS:
                continue
            v = (e1x * pz - e1z * px) * inv
            if v < -EPS or u + v > 1 + EPS:
                continue
            out.append(y0 + u * e1y + v * e2y)
        return out

    def _is_floor(self, q):
        return bool(q['flags'] & FLAG_GROUND) and not q['flags'] & FLAG_NO_COLLISION_RESPONSE

    def bsp_candidates(self, x, z, top, bottom):
        """Quad indices of the leaves whose boxes hold (x, z) in x/z and
        overlap [bottom, top] in y: the walk of the game's BSP search."""
        out = []
        if not self.nodes:
            return out
        stack = [0]
        seen = set()
        while stack:
            ni = stack.pop()
            if ni in seen or not 0 <= ni < len(self.nodes):
                continue
            seen.add(ni)
            nd = self.nodes[ni]
            bx = nd['box']
            if x < bx[0] or x > bx[3] or z < bx[2] or z > bx[5] or bx[4] < bottom or bx[1] > top:
                continue
            if nd['leaf']:
                first = (nd['qptr'] - self.quad_off) // 0x5C
                for qi in range(first, first + nd['count']):
                    if 0 <= qi < self.nq:
                        out.append(qi)
            else:
                for k in nd['kids'][:2]:
                    if k != 0xFFFF:
                        stack.append(k & 0x3FFF)
        return out

    def _ground_grid(self):
        if self._grid is None:
            grid = {}
            for qi, q in enumerate(self.quads):
                if not self._is_floor(q):
                    continue
                bx = q['box']
                for ix in range(int(bx[0] // GRID_CELL), int(bx[3] // GRID_CELL) + 1):
                    for iz in range(int(bx[2] // GRID_CELL), int(bx[5] // GRID_CELL) + 1):
                        grid.setdefault((ix, iz), []).append(qi)
            self._grid = grid
        return self._grid

    def ground_hits_all(self, x, z):
        """Every ground hit over (x, z) by the grid: a list of (y, quad index)."""
        out = []
        for qi in self._ground_grid().get((int(x // GRID_CELL), int(z // GRID_CELL)), ()):
            for y in self.quad_hits(qi, x, z):
                out.append((y, qi))
        return out

    def ground_hit(self, x, z, top, bottom, use_bsp=False):
        """The floor search of the game: the highest ground triangle hit over
        (x, z) with bottom <= y <= top (the game shortens its ray to each hit,
        COLL_FIXED_TRIANGL_TestPoint_Body, so the topmost wins). Returns
        (y, quad index) or None."""
        if use_bsp:
            hits = []
            for qi in self.bsp_candidates(x, z, top, bottom):
                if self._is_floor(self.quads[qi]):
                    hits.extend((y, qi) for y in self.quad_hits(qi, x, z))
        else:
            hits = self.ground_hits_all(x, z)
        best = None
        for y, qi in hits:
            if bottom <= y <= top and (best is None or (y, -qi) > (best[0], -best[1])):
                best = (y, qi)
        return best


def ground_hit(model, x, z, top, bottom, use_bsp=False):
    return model.ground_hit(x, z, top, bottom, use_bsp)


# ---------------------------------------------------------------- layers

def conflicts_in(ys):
    """Pairs (low, high) of heights from one point whose distance is in
    (1, 160): the game's fixed ray reaches 128 up, so it cannot tell these
    layers apart."""
    s = sorted(ys)
    out = []
    for i in range(len(s)):
        for j in range(i + 1, len(s)):
            d = s[j] - s[i]
            if CONFLICT_MIN < d < CONFLICT_MAX:
                out.append((s[i], s[j]))
    return out


def _road_layers(surface, x, z):
    """ROAD layers over (x, z): heights; a layer at most DECAL above the one
    below it (a decal on the road) counts as one layer at the top, which is
    the visible surface the collision must not lie under."""
    layers = []
    for y, kind in surface.hits(x, z):
        if kind != 1:
            continue
        if layers and y - layers[-1] <= DECAL:
            layers[-1] = y
            continue
        layers.append(y)
    return layers


def _raster_points(surface, raster):
    """Multiples of raster inside the x/z box of the ROAD triangles that lie
    in a surface cell holding one; points elsewhere have no ROAD layer."""
    pts = set()
    cell = surface.cell
    for (ix, iz), lst in surface.grid.items():
        if not any(surface.tri[k][10] == 1 for k in lst):
            continue
        x_lo, x_hi = ix * cell, (ix + 1) * cell
        z_lo, z_hi = iz * cell, (iz + 1) * cell
        for gx in range(int(math.ceil(x_lo / raster)), int(math.ceil(x_hi / raster))):
            for gz in range(int(math.ceil(z_lo / raster)), int(math.ceil(z_hi / raster))):
                pts.add((gx * raster, gz * raster))
    return sorted(pts)


def _scan_points(model, surface, raster, region):
    """For each raster point and ROAD layer: the layer height, the floor-search
    result of the game (or None), and the wide list of all ground hits."""
    rows = []
    for x, z in _raster_points(surface, raster):
        layers = _road_layers(surface, x, z)
        if region is not None:
            layers = [y for y in layers if region(x, z, y)]
        if not layers:
            continue
        hits = model.ground_hits_all(x, z)
        rows.append((x, z, layers, hits))
    return rows


def _row_conflicts(layers, hits):
    lo = min(layers) - CONFLICT_WINDOW
    hi = max(layers) + CONFLICT_WINDOW
    return conflicts_in([y for y, _ in hits if lo <= y <= hi])


def layer_conflicts(lev, surface=None, raster=64, region=None):
    """List of (x, z, y_low, y_high) where two ground hits of one point are
    1..160 apart. At the raster points of the surface, or without a surface at
    the centre of every ground quad."""
    model = lev if isinstance(lev, LevModel) else LevModel(lev)
    out = []
    if surface is not None:
        for x, z, layers, hits in _scan_points(model, surface, raster, region):
            for a, bb in _row_conflicts(layers, hits):
                out.append((x, z, a, bb))
        return out
    for qi, q in enumerate(model.quads):
        if not model._is_floor(q) or q['idx'][6] >= model.nv:
            continue
        x, y, z = model.verts[q['idx'][6]]
        hits = model.ground_hits_all(x, z)
        for a, bb in _row_conflicts([y], hits):
            out.append((x, z, a, bb))
    return out


# ---------------------------------------------------------------- the checks

def _check_pointer_map(m, err):
    prev = -1
    for k, slot in enumerate(m.map_slots):
        if slot <= prev:
            err.add(1, 'pointer map entry %d (%d) is %s' % (k, slot, 'a duplicate' if slot == prev else 'out of order'))
        prev = max(prev, slot)
        if slot % 4 or slot >= m.n - 3:
            err.add(1, 'pointer map entry %d (%d) is not a word inside the body' % (k, slot))
            continue
        val = struct.unpack_from('<I', m.b, slot)[0]
        if val > m.n:
            err.add(1, 'pointer at %d points to %d, past the body end %d' % (slot, val, m.n))
    if m.map_bytes % 4:
        err.add(1, 'pointer map size %d is not a multiple of 4' % m.map_bytes)


def _check_quads(m, err):
    """Check 2; returns the set of quad indices with a bad vertex index."""
    bad = set()
    for qi, q in enumerate(m.quads):
        if any(i >= m.nv for i in q['idx']):
            err.add(2, 'quad %d has a vertex index >= numVert %d: %r' % (qi, m.nv, q['idx']))
            bad.add(qi)
            continue
        ps = [m.verts[i] for i in q['idx']]
        box = (min(p[0] for p in ps), min(p[1] for p in ps), min(p[2] for p in ps),
               max(p[0] for p in ps), max(p[1] for p in ps), max(p[2] for p in ps))
        if tuple(q['box']) != box:
            err.add(2, 'quad %d box %r differs from its corners %r' % (qi, tuple(q['box']), box))
    return bad


def _check_normals(m, err, bad, stats):
    tris = quad_triangles()
    for qi, q in enumerate(m.quads):
        kind = m.kind(q)
        if kind == 'other' or qi in bad:
            continue
        stats[kind] += 1
        ps = [m.verts[i] for i in q['idx']]
        for tri, lod, di in tris:
            p1, p2, p3 = (ps[i] for i in tri)
            d = q['divs'][di]
            cr = game_cross(p1, p2, p3)
            if any(c != s32(c) or abs((s32(c) >> lod) * d) >= (1 << 31) for c in cr):
                err.add(3, 'quad %d (%s) triangle %d: 32-bit overflow (cross %r, dividend %d)' % (qi, kind, di, cr, d))
                continue
            n = game_normal(p1, p2, p3, lod, d, q['shift'])
            length = math.sqrt(sum(v * v for v in n))
            want = 4096.0 if lod == 0 else 1024.0
            if abs(length - want) > 0.05 * want:
                err.add(3, 'quad %d (%s) triangle %d: normal length %.0f, want %.0f' % (qi, kind, di, length, want))
                continue
            if kind == 'ground':
                if n[1] <= 0:
                    err.add(3, 'quad %d (ground) triangle %d: normal %r does not point up' % (qi, di, tuple(n)))
            elif abs(n[1]) > 0.10 * length:
                err.add(3, 'quad %d (%s) triangle %d: normal %r is not upright' % (qi, kind, di, tuple(n)))


def _outer_edges(idx):
    for a, b in ((0, 4), (4, 1), (1, 7), (7, 3), (3, 8), (8, 2), (2, 5), (5, 0)):
        yield (idx[a], idx[b]) if idx[a] < idx[b] else (idx[b], idx[a])


def _check_seams(m, err, bad):
    edges = {}
    used = set()
    for qi, q in enumerate(m.quads):
        if m.kind(q) != 'ground' or qi in bad:
            continue
        used.update(q['idx'])
        for e in _outer_edges(q['idx']):
            edges[e] = edges.get(e, 0) + 1
    for e in sorted(edges):
        if edges[e] > 2:
            err.add(4, 'edge %r belongs to %d ground quads' % (e, edges[e]))
    where = {}
    for i in sorted(used):
        where.setdefault(m.verts[i], []).append(i)
    for pos in sorted(where):
        if len(where[pos]) > 1:
            err.add(4, 'seam: vertices %r share the position %r' % (where[pos], pos))
    return sum(1 for e in edges if edges[e] == 1)


def _check_bsp(m, err, bad):
    """Check 5; returns the maximum depth."""
    nodes = m.nodes
    if not nodes:
        err.add(5, 'no BSP nodes')
        return 0
    if nodes[0]['leaf']:
        err.add(5, 'the root is a leaf')
    visits = [0] * len(nodes)
    leaf_of = {}
    depth_max = 0
    stack = [(0, 1)]
    while stack:
        ni, depth = stack.pop()
        visits[ni] += 1
        if visits[ni] > 1:
            err.add(5, 'node %d is reached more than once' % ni)
            continue
        depth_max = max(depth_max, depth)
        if depth > MAX_DEPTH:
            err.add(5, 'node %d lies at depth %d (> %d)' % (ni, depth, MAX_DEPTH))
            continue
        nd = nodes[ni]
        if nd['leaf']:
            cnt = nd['count']
            rel = nd['qptr'] - m.quad_off
            if cnt < 1:
                err.add(5, 'leaf %d holds no quad' % ni)
                continue
            if rel < 0 or rel % 0x5C or rel // 0x5C + cnt > m.nq:
                err.add(5, 'leaf %d quad range (offset %d, count %d) is outside the quad array' % (ni, nd['qptr'], cnt))
                continue
            first = rel // 0x5C
            for qi in range(first, first + cnt):
                leaf_of.setdefault(qi, []).append(ni)
                if qi in bad:
                    continue
                qb = m.quads[qi]['box']
                nb = nd['box']
                if any(qb[k] < nb[k] for k in range(3)) or any(qb[k] > nb[k] for k in range(3, 6)):
                    err.add(5, 'leaf %d box %r does not hold quad %d box %r' % (ni, tuple(nb), qi, tuple(qb)))
            continue
        for k in nd['kids'][:2]:
            if k == 0xFFFF:
                err.add(5, 'branch %d has a missing child' % ni)
                continue
            ci = k & 0x3FFF
            if ci >= len(nodes):
                err.add(5, 'branch %d child %d does not exist' % (ni, ci))
                continue
            child = nodes[ci]
            if bool(k & 0x4000) != child['leaf']:
                err.add(5, 'branch %d child %d: leaf bit disagrees with the node' % (ni, ci))
            nb, cb = nd['box'], child['box']
            if any(cb[j] < nb[j] for j in range(3)) or any(cb[j] > nb[j] for j in range(3, 6)):
                err.add(5, 'branch %d box %r does not hold child %d box %r' % (ni, tuple(nb), ci, tuple(cb)))
            stack.append((ci, depth + 1))
    for ni, v in enumerate(visits):
        if v == 0:
            err.add(5, 'node %d is not reachable from the root' % ni)
    for qi in range(m.nq):
        n = len(leaf_of.get(qi, ()))
        if n != 1:
            err.add(5, 'quad %d is in %d leaves, want exactly 1' % (qi, n))
    return depth_max


def _check_restart(m, err, bad):
    cnt = m.num_cp
    if not 3 <= cnt <= 255:
        err.add(6, 'restart point count %d outside 3..255' % cnt)
        return []
    if m.cp_off + 12 * cnt > m.n:
        err.add(6, 'restart table outside the body')
        return []
    cps = []
    for i in range(cnt):
        x, y, z, dist, fwd, left, back, right = _unpack('3hH4B', m.b, m.cp_off + 12 * i)
        cps.append(dict(pos=(x, y, z), dist=dist, fwd=fwd, left=left, back=back, right=right))
    for i, c in enumerate(cps):
        if c['fwd'] >= cnt:
            err.add(6, 'restart point %d: forward link %d >= count %d' % (i, c['fwd'], cnt))
        for nm in ('left', 'back', 'right'):
            if c[nm] >= cnt and c[nm] != NO_NODE:
                err.add(6, 'restart point %d: %s link %d >= count %d' % (i, nm, c[nm], cnt))
    ring = []
    i = 0
    while i < cnt and i not in ring:
        ring.append(i)
        i = cps[i]['fwd']
    if i != 0:
        err.add(6, 'the ring from point 0 does not come back to 0 (stopped at %d)' % i)
    elif len(ring) < 3:
        err.add(6, 'the ring has %d points, want at least 3' % len(ring))
    for a, b in zip(ring, ring[1:]):
        if cps[b]['dist'] > cps[a]['dist']:
            err.add(6, 'distance rises along the ring: point %d has %d, next point %d has %d' % (a, cps[a]['dist'], b, cps[b]['dist']))
    if cps[0]['dist'] * 8 <= LAP_MIN:
        err.add(6, 'lap length %d (point 0 dist %d * 8) is not above %d' % (cps[0]['dist'] * 8, cps[0]['dist'], LAP_MIN))
    named = set()
    for qi, q in enumerate(m.quads):
        if qi in bad:
            continue
        if m.kind(q) == 'ground':
            if q['cp'] >= cnt:
                err.add(6, 'ground quad %d names restart point %d (count %d)' % (qi, q['cp'], cnt))
            else:
                named.add(q['cp'])
        elif q['cp'] != NO_NODE:
            err.add(6, '%s quad %d names restart point %d, want 0xFF' % (m.kind(q), qi, q['cp']))
    for i in sorted(named):
        x, y, z = cps[i]['pos']
        hit = m.ground_hit(x, z, y + 128, y - 256, use_bsp=True)
        if hit is None:
            err.add(6, 'restart point %d at %r finds no ground (floor search %d above to %d below)' % (i, cps[i]['pos'], 128, 256))
    return cps


def _check_nav(m, err, cps):
    """Check 7; returns nav_jumps (frames with distXZ == 1 in path 0)."""
    jumps = 0
    if m.nav_tab == 0:
        return 0            # no nav paths: the game makes no bots (BOTS_Driver_Init)
    if m.nav_tab + 12 > m.n:
        err.add(7, 'nav table pointer %d is invalid' % m.nav_tab)
        return 0
    ptrs = _unpack('3I', m.b, m.nav_tab)
    for k, p in enumerate(ptrs):
        if p == 0 or p + 0x4C > m.n:
            err.add(7, 'nav path %d pointer %d is invalid' % (k, p))
            continue
        magic, npts = _unpack('hh', m.b, p)
        if magic != NAV_MAGIC:
            err.add(7, 'nav path %d magic %d, want %d' % (k, magic, NAV_MAGIC))
        if npts < 2:
            err.add(7, 'nav path %d has %d points, want at least 2' % (k, npts))
            continue
        if p + 0x4C + 0x14 * npts > m.n:
            err.add(7, 'nav path %d frames run past the body' % k)
            continue
        for f in range(npts):
            o = p + 0x4C + 0x14 * f
            dxyz, dxz = _unpack('hh', m.b, o + 10)
            back = m.b[o + 18]
            if dxz < 1 or dxyz < 1:
                err.add(7, 'nav path %d frame %d: distXYZ %d distXZ %d, want >= 1' % (k, f, dxyz, dxz))
            if cps and back >= len(cps):
                err.add(7, 'nav path %d frame %d: goBackCount %d >= restart points %d' % (k, f, back, len(cps)))
            if k == 0 and dxz == 1:
                jumps += 1
    return jumps


def _check_visibility(m, err, bad, stats):
    for qi, q in enumerate(m.quads):
        kind = m.kind(q)
        if kind == 'other' or qi in bad:
            continue
        for p in q['tex']:
            if p + 12 > m.n:
                err.add(8, 'quad %d has a texture pointer %d outside the body' % (qi, p))
                break
        invisible = m.is_invisible(q)
        if kind == 'ground':
            stats['invisible_ground' if invisible else 'visible_ground'] += 1
        elif not invisible:
            err.add(8, '%s quad %d is visible' % (kind, qi))


def _check_heights(m, surface, raster, tol, hit_min, region, err, tol_below=0.5):
    """Checks 10 and 11 in one pass over the raster points. A point is good when
    the collision ground lies at most tol above the model's road and at most
    tol_below under it (the rounding of a coordinate): shadows and skid marks
    are drawn on the collision ground and must not sink into the model."""
    rows = _scan_points(m, surface, raster, region)
    points = good = off = missed = below = target = 0
    errors = []
    deltas = []
    worst = []
    conflicts = []
    for x, z, layers, hits in rows:
        for ys in layers:
            points += 1
            cands = [y for y, _ in hits if ys - 256 <= y <= ys + 128]
            if not cands:
                missed += 1
                worst.append((float('inf'), x, z, ys, None))
                continue
            h = max(cands)
            d = h - ys
            e = abs(d)
            errors.append(e)
            deltas.append(d)
            worst.append((e, x, z, ys, h))
            if d < -tol_below:
                below += 1
            if 0.0 <= d <= 3.0:
                target += 1
            if -tol_below <= d <= tol:
                good += 1
            else:
                off += 1
        for a, bb in _row_conflicts(layers, hits):
            conflicts.append((x, z, a, bb))
    errors.sort()
    worst.sort(key=lambda t: (-t[0], t[1], t[2], t[3]))
    p99 = errors[min(len(errors) - 1, int(math.ceil(0.99 * len(errors))) - 1)] if errors else 0.0
    deltas.sort()
    height = dict(points=points, good=good, hit_but_off=off, missed=missed, below=below,
                  in_target=(target / points) if points else 0.0,
                  delta_min=deltas[0] if deltas else 0.0, delta_max=deltas[-1] if deltas else 0.0,
                  delta_p01=deltas[int(0.01 * len(deltas))] if deltas else 0.0,
                  delta_p99=deltas[min(len(deltas) - 1, int(0.99 * len(deltas)))] if deltas else 0.0,
                  rate=(good / points) if points else 0.0,
                  max_error=errors[-1] if errors else 0.0, p99_error=p99,
                  worst=[dict(x=x, z=z, y=ys, hit=h, error=None if e == float('inf') else round(e, 3))
                         for e, x, z, ys, h in worst[:10]])
    if points == 0:
        err.add(10, 'no raster point has a road layer')
    elif height['rate'] < hit_min:
        err.add(10, 'floor search finds the road within -%.1f..+%.1f at %.4f of %d points (want >= %.4f): %d off '
                '(%d of them below), %d missed; worst %r' % (tol_below, tol, height['rate'], points, hit_min, off, below, missed,
                                                          height['worst'][:3]))
    for x, z, a, bb in conflicts:
        err.add(11, 'two ground layers %.1f and %.1f over (%d, %d)' % (a, bb, x, z))
    return height, len(conflicts)


def uncovered_points(lev, surface, raster=64, region=None):
    """Raster points of the model's road (each road layer, region applied) under
    which the game's floor search finds no ground at all (window y - 256 ..
    y + 128): road the kart can see but cannot drive on. A list of (x, z, y)."""
    model = lev if isinstance(lev, LevModel) else LevModel(lev)
    out = []
    for x, z in _raster_points(surface, raster):
        for y in _road_layers(surface, x, z):
            if region is not None and not region(x, z, y):
                continue
            if model.ground_hit(x, z, y + 128, y - 256) is None:
                out.append((x, z, y))
    return out


def verify_lev(lev, surface=None, raster=64, tol=8.0, hit_min=0.99, region=None):
    """Check a LEV (see the module doc); returns a dict with 'errors' and figures."""
    err = _Errors()
    out = dict(errors=[])
    try:
        m = LevModel(lev)
    except (_Unreadable, struct.error) as e:
        out['errors'] = ['unreadable LEV: %s' % e]
        return out
    stats = dict(ground=0, wall=0, curtain=0, visible_ground=0, invisible_ground=0)
    bad = set()
    cps = []
    steps = (
        (1, lambda: _check_pointer_map(m, err)),
        (2, lambda: bad.update(_check_quads(m, err))),
        (3, lambda: _check_normals(m, err, bad, stats)),
        (4, lambda: out.__setitem__('open_edges', _check_seams(m, err, bad))),
        (5, lambda: out.__setitem__('bsp_depth', _check_bsp(m, err, bad))),
        (6, lambda: cps.extend(_check_restart(m, err, bad))),
        (7, lambda: out.__setitem__('nav_jumps', _check_nav(m, err, cps))),
        (8, lambda: _check_visibility(m, err, bad, stats)),
    )
    for number, step in steps:
        try:
            step()
        except (_Unreadable, struct.error, IndexError) as e:
            err.add(number, 'unreadable: %s' % e)
    out['height'] = None
    out['layer_conflicts'] = None
    if surface is not None:
        try:
            out['height'], out['layer_conflicts'] = _check_heights(m, surface, raster, tol, hit_min, region, err)
        except (_Unreadable, struct.error, IndexError) as e:
            err.add(10, 'unreadable: %s' % e)
    else:
        try:
            found = layer_conflicts(m)
            out['layer_conflicts'] = len(found)
            for x, z, a, bb in found:
                err.add(11, 'two ground layers %.1f and %.1f over (%d, %d)' % (a, bb, x, z))
        except (_Unreadable, struct.error, IndexError) as e:
            err.add(11, 'unreadable: %s' % e)
    out.update(quads=m.nq, vertices=m.nv, bsp_nodes=m.nbsp, restart_points=m.num_cp,
               ground_quads=stats['ground'], wall_quads=stats['wall'], curtain_quads=stats['curtain'],
               visible_ground=stats['visible_ground'], invisible_ground=stats['invisible_ground'])
    out.setdefault('open_edges', None)
    out.setdefault('bsp_depth', None)
    out.setdefault('nav_jumps', None)
    out['errors'] = err.result()
    return out
