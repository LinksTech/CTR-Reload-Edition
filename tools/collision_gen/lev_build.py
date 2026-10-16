"""Builds the LEV of a collision model from the traced sections of a track.

What the game reads, and where:
  - quadblocks of 0x5C bytes over a 3x3 vertex grid (include/namespace_Level.h,
    struct QuadBlock), vertices of 0x10 bytes (struct LevVertex);
  - a BSP of 0x20-byte nodes whose root is a branch (game/COLL.c,
    COLL_SearchBSP_CallbackPARAM reads the children of the root);
  - restart points of 12 bytes: position, distance to the finish / 8, the
    next point forward, left, backward, right (struct CheckpointNode);
  - nav paths for the bots (include/namespace_Bots.h, NavHeader, NavFrame);
  - one PVS in which everything is visible, VisMem for four players.

Surfaces:
  - road and edge: GROUND | CAMERA_SEARCH (0x1800), terrain 0 (road) or 2 (edge);
  - walls along both sides: COLLISION_SURFACE (0x2000), solid from both sides;
  - the curtain at the end of a section that ends at a portal: 0xE210 (a kill
    plane that catches without stopping);
  - walls, curtains and the bridges over gaps carry no texture: a quadblock
    whose five texture layouts are all zero is not drawn but keeps its
    collision (the zero rule, game/226/226_00_DrawLevelOvr1P.c, and the engine
    track, platform/engine_track.c).

Portals (a section that ends where the next one begins somewhere else): the
mask grab of the game is the portal. Touching the curtain asks for it, and it
puts the kart on the restart point of the last ground quadblock touched
(game/Vehicle/VehStuckProc.c). The ground of the last segment of such a
section, and of its extension under the curtain, names a side point R at the
start of the next section; R looks toward S on that road, and T is placed so
that the progress the game computes for a kart near the portal
(game/Vehicle/VehLap.c) stays continuous - see portal_side_point_t.

Restart points along the lap (the ring, from point 0 on the start line): the
regular points every NODE_SPACING units, and at every portal P (the mouth) and V
(a point past it, which only gives the direction for the progress before the
mouth). The lap position does not advance over a portal: P, V and the first
point of the next section have the same distance to the finish.
"""

import math

import lev_normals
from lev_write import (Body, TILE_COUNT, TILE_EDGE, TILE_ROAD, TILE_START, clamp16, layout_bytes, make_vrm)

FLAG_GROUND = 0x1800        # GROUND | CAMERA_SEARCH, as roads are
FLAG_WALL = 0x2000          # COLLISION_SURFACE
FLAG_CURTAIN = 0xE210       # SKIP_WATER_LIST | NO_CAMERA_RESPAWN_PROBE | COLLISION_SURFACE | KILL_PLANE | NO_COLLISION_RESPONSE
TERRAIN_ROAD = 0
TERRAIN_EDGE = 2
DOUBLE_SIDED = 0x80000000   # draw_order_low bit 31: collision against both sides (COLL.c, MOVED test)
NO_NODE = 0xFF

COLUMN_MAX = 512.0          # widest quadblock across
OUTSET = 24.0               # the ground reaches this far past the traced edge (the tracer feels
                            # the edge in steps of 16): no strip of road without collision
WALL_LEVELS = (-64.0, 96.0, 256.0)
CURTAIN_OVERHANG = 64.0
EXTENSION = 256.0           # the ground goes on this far past the curtain
PORTAL_TAIL = 256.0         # length of the last segment before a portal mouth: the ground that names R
NODE_SPACING = 1024.0       # a regular restart point every 1024 units
LEAF_SEGMENTS = 2
SKY = (96, 150, 220)
LANE = 200.0
LANE_MARGIN = 96.0
NAV_OPCODE = 0x0C00
NAV_MAGIC = -0x1303
SIDE_LENGTH = 512.0
LAP_MIN = 33200.0
FINISH_CLEAR = 1712.0       # no portal closer to the finish (PlayLevel.c: 1200, plus a margin)
COORD_LIMIT = 32700


class BuildError(ValueError):
    pass


def wrap16(v):
    """v as the game sees a difference of two s16 coordinates: wrapped to s16."""
    v = int(v) & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def portal_side_point_t(mouth, mouth_dir, s):
    """T of a portal: unit(S - T) is perpendicular to the difference between the
    portal mouth and S as the game computes it (s16, wrapped, VehLap.c), and
    otherwise as close as possible to the direction back along the road before
    the mouth. For a kart at the mouth the game's projection onto unit(S - T)
    then is the distance it still has to the mouth."""
    d = [wrap16(round(mouth[i]) - round(s[i])) for i in range(3)]
    dn = math.sqrt(sum(v * v for v in d))
    back = (-mouth_dir[0], 0.0, -mouth_dir[1])
    if dn < 1.0:
        u = list(back)
    else:
        dh = [v / dn for v in d]
        k = sum(back[i] * dh[i] for i in range(3))
        u = [back[i] - k * dh[i] for i in range(3)]
        un = math.sqrt(sum(v * v for v in u))
        if un < 1e-3:
            raise BuildError('a portal mouth points straight at its landing - no side direction for T')
        u = [v / un for v in u]
    return tuple(s[i] - SIDE_LENGTH * u[i] for i in range(3))


class Mesh:
    """Vertices (shared by position) and quadblocks."""

    def __init__(self):
        self.verts = []
        self.vindex = {}
        self.quads = []

    def vert(self, p):
        # x and z to the nearest unit, y up: rounding never puts the ground below the model
        key = (clamp16(p[0], 'vertex coordinate'), clamp16(math.ceil(p[1] - 1e-6), 'vertex coordinate'),
               clamp16(p[2], 'vertex coordinate'))
        if max(abs(v) for v in key) > COORD_LIMIT:
            raise BuildError('vertex %r lies outside +-%d' % (key, COORD_LIMIT))
        i = self.vindex.get(key)
        if i is None:
            i = len(self.verts)
            self.vindex[key] = i
            self.verts.append(key)
        return i

    def quad(self, grid, **kw):
        """grid[r][c]: vertex indices, r forward (0..2), c across (0..2). The
        index pattern puts index 1 forward of index 0 and index 2 toward lower
        c, so the game's normal is (toward lower c) x (forward)."""
        g = grid
        q = dict(idx=[g[0][2], g[2][2], g[0][0], g[2][0], g[1][2], g[0][1], g[1][1], g[2][1], g[1][0]])
        q.update(kw)
        self.quads.append(q)
        return q


def _row_points(surface, row, ncol, kinds):
    """The 2 * ncol + 1 points across a row, right edge first. Heights from the
    surface, followed outward from the middle; a point without surface takes
    its inner neighbour's height. Returns (points, lateral offsets, misses)."""
    lx, lz = row['dz'], -row['dx']
    right = -row['dr'] - OUTSET
    left = row['dl'] + OUTSET
    lats = [right + (left - right) * j / (2.0 * ncol) for j in range(2 * ncol + 1)]
    ys = [row['y']] * len(lats)
    misses = 0
    if surface is not None and not row['gap']:
        mid = min(range(len(lats)), key=lambda j: (abs(lats[j]), j))
        order = [mid]
        for k in range(1, len(lats)):
            for j in (mid - k, mid + k):
                if 0 <= j < len(lats):
                    order.append(j)
        done = set()
        for j in order:
            nb = j + 1 if j < mid else (j - 1 if j > mid else None)
            y_ref = row['y'] if nb is None else ys[nb]
            hit = surface.surface(row['x'] + lx * lats[j], row['z'] + lz * lats[j], y_ref, 192.0, kinds)
            if hit is None:
                misses += 1
                ys[j] = y_ref
            else:
                ys[j] = hit[0]
            done.add(j)
    pts = [(row['x'] + lx * lats[j], ys[j], row['z'] + lz * lats[j]) for j in range(len(lats))]
    return pts, lats, misses


# The ground lies on the model's road or a little above it, never below: a kart
# shadow or a skid mark drawn on the collision ground would vanish into the
# model's road otherwise (the engine draws them with a depth bias of 1/256).
LIFT_START = 1.0            # every vertex starts this far above the model's road
LIFT_MIN = 0.5              # inside a triangle the ground stays at least this far above
LIFT_PASSES = 6
LIFT_MAX = 256.0            # a single vertex is never raised further than this
LIFT_SAMPLES = 4            # samples along each triangle edge (barycentric grid)
LIFT_REPORT = 200           # the largest raises are reported


def lift_ground(surface, kinds, pts, allrows, segs, ncol):
    """Raise ground vertices until every visible ground triangle lies at least
    LIFT_MIN above the model's road at a grid of points inside it. Raising the
    three corners of a triangle by d raises every point of it by d. Triangles of
    bridges and drops (no model road under them) are left alone. Returns the
    number of vertices raised and the largest raises as (raise, x, z)."""
    base = {}
    tris = []
    for rr in segs:
        seg_rows = [allrows[i] for i in rr]
        if any(r['gap'] or r.get('drop') for r in seg_rows):
            continue
        for c in range(ncol):
            g = [[(rr[k], 2 * c + j) for j in range(3)] for k in range(3)]
            idx = [g[0][2], g[2][2], g[0][0], g[2][0], g[1][2], g[0][1], g[1][1], g[2][1], g[1][0]]
            for a, b, cc in lev_normals.HI_TRIS:
                tris.append((idx[a], idx[b], idx[cc]))
    for tri in tris:
        for v in tri:
            if v not in base:
                base[v] = pts[v[0]][v[1]][1]
    s = LIFT_SAMPLES
    bary = [(i / s, j / s) for i in range(s + 1) for j in range(s + 1 - i)]
    for _ in range(LIFT_PASSES):
        need = {}
        for tri in tris:
            p = [pts[v[0]][v[1]] for v in tri]
            worst = 0.0
            for u, w in bary:
                x = p[0][0] + u * (p[1][0] - p[0][0]) + w * (p[2][0] - p[0][0])
                y = p[0][1] + u * (p[1][1] - p[0][1]) + w * (p[2][1] - p[0][1])
                z = p[0][2] + u * (p[1][2] - p[0][2]) + w * (p[2][2] - p[0][2])
                hit = surface.surface(x, z, y, 96.0, kinds)
                if hit is None:
                    continue
                d = hit[0] + LIFT_MIN - y
                if d > worst:
                    worst = d
            if worst > 1e-6:
                for v in tri:
                    if worst > need.get(v, 0.0):
                        need[v] = worst
        if not need:
            break
        for v in sorted(need):
            q = pts[v[0]][v[1]]
            q[1] = min(q[1] + need[v], base[v] + LIFT_MAX)
    raised = sorted(((pts[v[0]][v[1]][1] - base[v], v) for v in base if pts[v[0]][v[1]][1] > base[v] + 1e-6), reverse=True)
    top = [(round(d, 1), round(pts[v[0]][v[1]][0]), round(pts[v[0]][v[1]][2])) for d, v in raised[:LIFT_REPORT]]
    return len(raised), top


LAYER_CLEAR = 160.0         # two ground layers closer than this confuse the game's floor search
                            # (the ray reaches 128 above the kart, COLL.c, and the topmost hit wins)


def edge_conflicts(quads, verts):
    """(section, segment, column) of every edge quadblock (drivable edge, not
    road) that lies under or over another ground quadblock closer than
    LAYER_CLEAR: an edge running under a ramp of another part of the track.
    Leaving these out is the cure; the road is never left out."""
    cell = 256
    grid = {}
    info = []
    for qi, q in enumerate(quads):
        if q['kind'] != 'ground':
            info.append(None)
            continue
        ps = [verts[i] for i in q['idx']]
        box = (min(p[0] for p in ps), min(p[1] for p in ps), min(p[2] for p in ps),
               max(p[0] for p in ps), max(p[1] for p in ps), max(p[2] for p in ps))
        info.append(box)
        for ix in range(box[0] // cell, box[3] // cell + 1):
            for iz in range(box[2] // cell, box[5] // cell + 1):
                grid.setdefault((ix, iz), []).append(qi)
    out = set()
    for qi, q in enumerate(quads):
        if info[qi] is None or not q.get('edge'):
            continue
        a = info[qi]
        near = set()
        for ix in range(a[0] // cell, a[3] // cell + 1):
            for iz in range(a[2] // cell, a[5] // cell + 1):
                near.update(grid.get((ix, iz), ()))
        for oj in sorted(near):
            if oj == qi:
                continue
            o = quads[oj]
            if o['sec'] == q['sec'] and abs(o['seg'] - q['seg']) <= 1:
                continue            # neighbours share edges
            b = info[oj]
            if a[3] < b[0] or b[3] < a[0] or a[5] < b[2] or b[5] < a[2]:
                continue
            gap = max(b[1] - a[4], a[1] - b[4])
            if gap < LAYER_CLEAR:
                out.add((q['sec'], q['seg'], q['col']))
                break
    return out


def build_lev(sections, start, surface=None, kinds=(1, 2), column_max=COLUMN_MAX, nav=True, skip=frozenset()):
    """sections: list in driving order; each a dict with 'rows' and 'length'
    (from centreline.build_centreline), 'closed', 'portal' (the section ends at
    a portal to the next) and 'curtain' (the curtain's distance past the last
    row). start = (x, y, z) of the start line, which lies in section 0.
    column_max: the widest quadblock across. nav=False leaves the nav paths out:
    then the game makes no bots (BOTS_Driver_Init, game/BOTS.c, finds no path),
    a race with the player alone; pack it with rldpack make --keep-nav.
    skip: (section, segment, column) of edge quadblocks left out (see edge_conflicts).
    Returns (lev bytes, vrm bytes, info, model); model carries what the
    progress simulation needs."""
    nsec = len(sections)
    if nsec == 0:
        raise BuildError('no section')
    if nsec == 1 and not sections[0]['closed']:
        raise BuildError('a single section must be a closed loop')
    if nsec > 1 and any(s['closed'] for s in sections):
        raise BuildError('a closed loop cannot be one of several sections')
    for si, sec in enumerate(sections):
        if nsec > 1 and not sec['portal']:
            raise BuildError('section %d does not end at a portal; join sections that meet into one' % si)
    mesh = Mesh()

    lengths = [sec['length'] for sec in sections]
    lap = sum(lengths)
    if lap <= LAP_MIN:
        raise BuildError('lap %.0f units is too short: the game counts a lap only above 32000 (PlayLevel.c), '
                         'the generator wants more than %.0f' % (lap, LAP_MIN))
    rows0 = sections[0]['rows']
    start_row = min(range(len(rows0)), key=lambda i: ((rows0[i]['x'] - start[0]) ** 2 + (rows0[i]['z'] - start[2]) ** 2, i))
    sd = math.hypot(rows0[start_row]['x'] - start[0], rows0[start_row]['z'] - start[2])
    if sd > 384:
        raise BuildError('the start line (%.0f, %.0f) lies %.0f units from the traced road' % (start[0], start[2], sd))
    s_start = rows0[start_row]['s']
    offset = [0.0]
    for ln in lengths[:-1]:
        offset.append(offset[-1] + ln)

    def gs(si, r):
        """position on the lap from the start line, 0 .. lap"""
        return (offset[si] + sections[si]['rows'][r]['s'] - s_start) % lap

    # ---------------------------------------------------------- restart points
    node_rows = []
    for si, sec in enumerate(sections):
        n = len(sec['rows'])
        anchor = start_row if si == 0 else 0
        node_rows_step = max(2, int(round(NODE_SPACING / sec.get('row', 256.0))))
        last_regular = n - 1 if sec['closed'] else n - 4      # the last segment belongs to R
        rs = {r for r in range(0, last_regular + 1) if (r - anchor) % node_rows_step == 0}
        if not sec['closed']:
            rs.add(0)
        node_rows.append(sorted(rs))
    entries = []
    for si, sec in enumerate(sections):
        for r in node_rows[si]:
            entries.append((round(gs(si, r), 3), 2, si, r, 'row'))
        if sec['portal']:
            last = len(sec['rows']) - 1
            g = round(gs(si, last), 3)
            entries.append((g, 0, si, last, 'P'))
            entries.append((g, 1, si, last, 'V'))
    entries.sort(key=lambda e: (e[0] if not (e[4] == 'row' and e[2] == 0 and e[3] == start_row) else -1.0, e[1], e[2], e[3]))
    if entries[0][4] != 'row' or entries[0][2] != 0 or entries[0][3] != start_row:
        raise BuildError('internal: restart point 0 is not on the start line')
    nodes = []
    node_of = {}
    for g, _, si, r, kind in entries:
        row = sections[si]['rows'][r]
        if kind == 'V':
            pos = (row['x'] + row['dx'] * SIDE_LENGTH, row['y'], row['z'] + row['dz'] * SIDE_LENGTH)
        else:
            pos = (row['x'], row['y'], row['z'])
        node_of[(kind, si, r)] = len(nodes)
        nodes.append(dict(pos=pos, g=0.0 if not nodes else g, kind=kind, sec=si, row=r))
    nring = len(nodes)
    for i, nd in enumerate(nodes):
        nd['fwd'] = (i + 1) % nring
        nd['back'] = (i - 1) % nring
        nd['dist'] = int(round((lap - nd['g']) / 8.0))
        if nd['kind'] == 'P' and lap - nd['g'] < FINISH_CLEAR:
            raise BuildError('the portal of section %d lies %.0f units before the finish, at least %.0f are needed'
                             % (nd['sec'], lap - nd['g'], FINISH_CLEAR))
    side = {}
    for si, sec in enumerate(sections):
        if not sec['portal']:
            continue
        nxt = (si + 1) % nsec
        last = sec['rows'][-1]
        landing = sections[nxt]['rows'][0]
        ahead = sections[nxt]['rows'][min(2, len(sections[nxt]['rows']) - 1)]
        p = node_of[('P', si, len(sec['rows']) - 1)]
        dist = nodes[p]['dist']
        s_pos = (ahead['x'], ahead['y'], ahead['z'])
        t_pos = portal_side_point_t((last['x'], last['y'], last['z']), (last['dx'], last['dz']), s_pos)
        b0 = node_of[('row', nxt, 0)]
        r_i = len(nodes)
        nodes.append(dict(pos=(landing['x'], landing['y'], landing['z']), kind='R', sec=si, fwd=r_i + 1, back=NO_NODE, dist=dist))
        nodes.append(dict(pos=s_pos, kind='S', sec=si, fwd=r_i + 2, back=NO_NODE, dist=dist))
        nodes.append(dict(pos=t_pos, kind='T', sec=si, fwd=nodes[b0]['fwd'], back=NO_NODE, dist=dist))
        side[si] = r_i
    if len(nodes) > 255:
        raise BuildError('%d restart points, the game takes at most 255' % len(nodes))
    for nd in nodes:
        for v in nd['pos']:
            if abs(clamp16(v, 'restart point coordinate')) > COORD_LIMIT:
                raise BuildError('restart point %r lies outside +-%d' % (nd['pos'], COORD_LIMIT))

    def regular_cp(si, mid_row):
        rs = node_rows[si]
        best = None
        for r in rs:
            if r <= mid_row:
                best = r
        if best is None:
            best = rs[-1]          # a closed loop: before the first point comes the last
        return node_of[('row', si, best)]

    # ---------------------------------------------------------- quadblocks
    misses = 0
    lifted = 0
    lift_top = []
    leaves = []
    sec_info = []
    for si, sec in enumerate(sections):
        rows = sec['rows']
        n = len(rows)
        closed = sec['closed']
        allrows = list(rows)
        if sec['portal']:
            last = rows[-1]
            far = sec.get('curtain', 0.0) + EXTENSION
            for d in (far / 2.0, far):
                r = dict(last)
                r['x'] = last['x'] + last['dx'] * d
                r['z'] = last['z'] + last['dz'] * d
                r['gap'] = True
                r['drop'] = False
                allrows.append(r)
        width = max(r['dl'] + r['dr'] for r in rows)
        ncol = max(1, int(math.ceil((width + 2 * OUTSET) / column_max)))
        pts = []
        for r in allrows:
            p, _, miss = _row_points(surface, r, ncol, kinds)
            misses += miss
            pts.append([[q[0], q[1] + LIFT_START, q[2]] for q in p])
        if closed:
            segs = [(2 * s, 2 * s + 1, (2 * s + 2) % n) for s in range(n // 2)]
        else:
            segs = [(2 * s, 2 * s + 1, 2 * s + 2) for s in range((n - 1) // 2)]
        if sec['portal']:
            segs.append((n - 1, n, n + 1))
        if surface is not None:
            count, top = lift_ground(surface, kinds, pts, allrows, segs, ncol)
            lifted += count
            lift_top += [(d, x, z, si) for d, x, z in top]
        ids = [[mesh.vert(q) for q in row_pts] for row_pts in pts]
        first = len(mesh.quads)
        for k, rr in enumerate(segs):
            seg_rows = [allrows[i] for i in rr]
            extension = sec['portal'] and k == len(segs) - 1
            last_main = sec['portal'] and k == len(segs) - 2
            if extension or last_main:
                cp = side[si]
            else:
                cp = regular_cp(si, rr[1])
            drop = any(r.get('drop') for r in seg_rows)
            invisible = any(r['gap'] for r in seg_rows)
            mid = seg_rows[1]
            right = -mid['dr'] - OUTSET
            left = mid['dl'] + OUTSET
            if not drop:
                for c in range(ncol):
                    lat = right + (left - right) * (2 * c + 1) / (2.0 * ncol)
                    edge = lat > mid['hl'] or lat < -mid['hr']
                    if edge and (si, k, c) in skip:
                        continue
                    tile = TILE_EDGE if edge else TILE_ROAD
                    if si == 0 and start_row in rr[:2] and not edge:
                        tile = TILE_START
                    grid = [[ids[i][2 * c + j] for j in range(3)] for i in rr]
                    mesh.quad(grid, kind='ground', flags=FLAG_GROUND, terrain=TERRAIN_EDGE if edge else TERRAIN_ROAD,
                              tile=None if invisible else tile, cp=cp, sec=si, seg=k, col=c, edge=edge, double=False)
            for col, levels in ((2 * ncol, WALL_LEVELS), (0, tuple(reversed(WALL_LEVELS)))):
                grid = [[mesh.vert((pts[i][col][0], pts[i][col][1] + levels[j], pts[i][col][2])) for j in range(3)] for i in rr]
                mesh.quad(grid, kind='wall', flags=FLAG_WALL, terrain=0, tile=None, cp=NO_NODE, sec=si, seg=k, double=True)
            if extension:
                last = allrows[n - 1]
                lx, lz = last['dz'], -last['dx']
                cd = sec.get('curtain', 0.0)
                cx, cz = last['x'] + last['dx'] * cd, last['z'] + last['dz'] * cd
                y = last['y']
                hw_r = last['dr'] + CURTAIN_OVERHANG
                hw_l = last['dl'] + CURTAIN_OVERHANG
                across = [(cx - lx * hw_r, cz - lz * hw_r), (cx + lx * (hw_l - hw_r) / 2.0, cz + lz * (hw_l - hw_r) / 2.0),
                          (cx + lx * hw_l, cz + lz * hw_l)]
                levels = tuple(reversed(WALL_LEVELS))
                grid = [[mesh.vert((a[0], y + levels[j], a[1])) for j in range(3)] for a in across]
                mesh.quad(grid, kind='curtain', flags=FLAG_CURTAIN, terrain=0, tile=None, cp=NO_NODE, sec=si, seg=k, double=True)
            if (k + 1) % LEAF_SEGMENTS == 0 or k == len(segs) - 1:
                if len(mesh.quads) > first:
                    leaves.append((first, len(mesh.quads) - first))
                first = len(mesh.quads)
        sec_info.append(dict(rows=n, columns=ncol, segments=len(segs), length=sec['length'],
                             gap_rows=sum(1 for r in rows if r['gap']), drop_rows=sum(1 for r in rows if r.get('drop'))))
    if len(mesh.verts) >= 65536:
        raise BuildError('%d vertices, a quadblock index is 16 bits' % len(mesh.verts))
    if len(leaves) < 2:
        raise BuildError('fewer than two BSP leaves: the root must be a branch')

    # ---------------------------------------------------------- nav paths
    order = []
    for si, sec in enumerate(sections):
        for r in range(len(sec['rows'])):
            order.append((round(gs(si, r), 3), 0 if (sec['portal'] and r == len(sec['rows']) - 1) else 1, si, r))
    order.sort(key=lambda e: (e[0] if not (e[2] == 0 and e[3] == start_row) else -1.0, e[1], e[2], e[3]))
    order = order[1:] + order[:1]          # frame 0 lies just past the start line, ahead of the grid
    lanes = []
    for lane in (-1, 0, 1):
        frames = []
        for _, _, si, r in order:
            sec = sections[si]
            row = sec['rows'][r]
            off = 0.0
            if lane:
                room = (row['hl'] if lane > 0 else row['hr']) - LANE_MARGIN
                off = lane * max(0.0, min(LANE, room))
            lx, lz = row['dz'], -row['dx']
            x, z = row['x'] + lx * off, row['z'] + lz * off
            y = row['y']
            if surface is not None and not row['gap']:
                hit = surface.surface(x, z, row['y'], 192.0, kinds)
                if hit:
                    y = hit[0]
            jump = sec['portal'] and r == len(sec['rows']) - 1
            frames.append(dict(x=x, y=y, z=z, dx=row['dx'], dz=row['dz'], jump=jump, cp=regular_cp(si, r)))
        lanes.append(frames)

    # ---------------------------------------------------------- BSP tree
    def qbox(q):
        ps = [mesh.verts[i] for i in q['idx']]
        return [min(p[0] for p in ps), min(p[1] for p in ps), min(p[2] for p in ps),
                max(p[0] for p in ps), max(p[1] for p in ps), max(p[2] for p in ps)]

    def union(boxes):
        return [min(b[0] for b in boxes), min(b[1] for b in boxes), min(b[2] for b in boxes),
                max(b[3] for b in boxes), max(b[4] for b in boxes), max(b[5] for b in boxes)]

    for q in mesh.quads:
        q['box'] = qbox(q)
    leaf_boxes = [union([mesh.quads[k]['box'] for k in range(f, f + c)]) for f, c in leaves]
    bsp = []

    def make(lo, hi):
        if hi - lo == 1:
            bsp.append(dict(leaf=lo, box=leaf_boxes[lo]))
            return len(bsp) - 1
        me = len(bsp)
        bsp.append(None)
        mid = (lo + hi) // 2
        a = make(lo, mid)
        b = make(mid, hi)
        bsp[me] = dict(children=(a, b), box=union([bsp[a]['box'], bsp[b]['box']]))
        return me

    make(0, len(leaves))
    if len(bsp) > 0x3FFF:
        raise BuildError('%d BSP nodes, a child id holds 14 bits' % len(bsp))

    # ---------------------------------------------------------- layout
    nq = len(mesh.quads)
    L = Body()
    lvl = L.alloc(0x1F4)
    mesh_off = L.alloc(0x20)
    vert_off = L.alloc(16 * len(mesh.verts))
    quad_off = L.alloc(0x5C * nq)
    bsp_off = L.alloc(0x20 * len(bsp))
    icon_off = L.alloc(0x30 * TILE_COUNT)
    blank_off = L.alloc(0x30)                          # a zeroed IconGroup4: not drawn
    nb32 = ((len(bsp) + 31) >> 5) << 2
    nq32 = ((nq + 31) >> 5) << 2
    pvs_off = L.alloc(16)
    pvs_leaf = L.alloc(nb32)
    pvs_face = L.alloc(nq32)
    vis_off = L.alloc(0x90)
    vis_leaf = [L.alloc(nb32) for _ in range(4)]
    vis_face = [L.alloc(nq32) for _ in range(4)]
    vis_bsp = [L.alloc(8 * len(bsp)) for _ in range(4)]
    anim_off = L.alloc(4)
    spawn1_off = L.alloc(4)
    cp_off = L.alloc(12 * len(nodes))
    if not nav:
        lanes = []
    nav_tab = L.alloc(12) if lanes else None
    nav_off = [L.alloc(0x4C + 0x14 * len(fr)) for fr in lanes]

    L.ptr(lvl + 0x00, mesh_off)
    L.ptr(lvl + 0x08, anim_off)
    L.ptr(anim_off, anim_off)                          # an empty AnimTex list: a word pointing at itself
    L.put('I', lvl + 0xDC, 0)
    L.put('4B', lvl + 0xD8, SKY[0], SKY[1], SKY[2], 0)
    for k in range(3):
        L.put('4B', lvl + 0x160 + 4 * k, SKY[0], SKY[1], SKY[2], 1)
    L.ptr(lvl + 0x134, spawn1_off)                     # SpawnType1 {count 0}
    L.put('I', lvl + 0x148, len(nodes))
    L.ptr(lvl + 0x14C, cp_off)
    L.ptr(lvl + 0x188, nav_tab)
    L.ptr(lvl + 0x190, vis_off)
    # DriverSpawn (0x6C) stays zero: rldpack make places the grid behind restart point 0

    L.put('III', mesh_off, nq, len(mesh.verts), 0)
    L.ptr(mesh_off + 0x0C, quad_off)
    L.ptr(mesh_off + 0x10, vert_off)
    L.put('I', mesh_off + 0x14, 0)
    L.ptr(mesh_off + 0x18, bsp_off)
    L.put('I', mesh_off + 0x1C, len(bsp))

    for i, (x, y, z) in enumerate(mesh.verts):
        L.put('3hH8B', vert_off + 16 * i, x, y, z, 0, 0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 0)
    for t in range(TILE_COUNT):
        lay = layout_bytes(t)
        for k in range(4):
            L.b[icon_off + 0x30 * t + 12 * k:icon_off + 0x30 * t + 12 * k + 12] = lay

    for qi, q in enumerate(mesh.quads):
        qo = quad_off + 0x5C * qi
        L.put('9H', qo, *q['idx'])
        L.put('H', qo + 0x12, q['flags'])
        L.put('II', qo + 0x14, DOUBLE_SIDED if q['double'] else 0, 0)
        ig = blank_off if q['tile'] is None else icon_off + 0x30 * q['tile']
        for k in range(4):
            L.ptr(qo + 0x1C + 4 * k, ig)
        L.put('6h', qo + 0x2C, *q['box'])
        L.put('B', qo + 0x38, q['terrain'])
        L.put('h', qo + 0x3C, qi ^ 31)
        L.put('B', qo + 0x3E, q['cp'])
        L.ptr(qo + 0x40, ig)
        L.ptr(qo + 0x44, pvs_off)
        try:
            shift, dividends = lev_normals.normal_dividends([mesh.verts[i] for i in q['idx']])
        except ValueError as e:
            raise BuildError('quadblock %d (%s, section %d, segment %d): %s' % (qi, q['kind'], q['sec'], q['seg'], e))
        L.put('B', qo + 0x3F, shift)
        L.put('10h', qo + 0x48, *dividends)

    for ni, nd in enumerate(bsp):
        no = bsp_off + 0x20 * ni
        if 'leaf' in nd:
            first, count = leaves[nd['leaf']]
            L.put('Hh6h', no, 0x0001, ni, *nd['box'])
            L.put('I', no + 0x10, 0)
            L.ptr(no + 0x14, None)
            L.put('I', no + 0x18, count)
            L.ptr(no + 0x1C, quad_off + 0x5C * first)
        else:
            a, b = nd['children']
            L.put('Hh6h', no, 0x0000, ni, *nd['box'])
            L.put('4h', no + 0x10, 4096, 0, 0, clamp16((nd['box'][0] + nd['box'][3]) / 2))
            L.put('4H', no + 0x18, a | (0x4000 if 'leaf' in bsp[a] else 0), b | (0x4000 if 'leaf' in bsp[b] else 0),
                  0xFFFF, 0xFFFF)

    L.ptr(pvs_off + 0, pvs_leaf)
    L.ptr(pvs_off + 4, pvs_face)
    L.ptr(pvs_off + 8, None)
    L.ptr(pvs_off + 12, None)
    for o, c in ((pvs_leaf, nb32), (pvs_face, nq32)):
        L.b[o:o + c] = b'\xff' * c
    for p in range(4):
        L.ptr(vis_off + 0x00 + 4 * p, vis_leaf[p])
        L.ptr(vis_off + 0x10 + 4 * p, vis_face[p])
        L.ptr(vis_off + 0x80 + 4 * p, vis_bsp[p])
        L.b[vis_leaf[p]:vis_leaf[p] + nb32] = b'\xff' * nb32
        L.b[vis_face[p]:vis_face[p] + nq32] = b'\xff' * nq32

    for i, nd in enumerate(nodes):
        x, y, z = (clamp16(v) for v in nd['pos'])
        if not 0 < nd['dist'] <= 0xFFFF:
            raise BuildError('restart point %d: distance %d does not fit 16 bits' % (i, nd['dist']))
        L.put('3hH4B', cp_off + 12 * i, x, y, z, nd['dist'], nd['fwd'], NO_NODE, nd['back'], NO_NODE)

    for k, frames in enumerate(lanes):
        h = nav_off[k]
        L.ptr(nav_tab + 4 * k, h)
        L.put('hhi', h, NAV_MAGIC, len(frames), 0)
        L.ptr(h + 8, None)
        for f, a in enumerate(frames):
            b = frames[(f + 1) % len(frames)]
            if a['jump']:
                dxyz = dxz = 1                 # the bot passes the portal within one tick (BOTS.c)
                yaw = math.atan2(a['dx'], a['dz'])
            else:
                dxz = math.hypot(b['x'] - a['x'], b['z'] - a['z'])
                dxyz = math.sqrt(dxz * dxz + (b['y'] - a['y']) ** 2)
                yaw = math.atan2(b['x'] - a['x'], b['z'] - a['z'])
            yaw = (yaw * 2048 / math.pi) % 4096
            fo = h + 0x4C + 0x14 * f
            L.put('3h4BhhhhBB', fo, clamp16(a['x']), clamp16(a['y']), clamp16(a['z']), 0, int(yaw) >> 4 & 0xFF, 0, 0,
                  max(1, clamp16(dxyz)), max(1, clamp16(dxz)), 0, NAV_OPCODE, a['cp'], 0)

    lev_bytes = L.lev_bytes()
    info = dict(sections=nsec, lap=round(lap, 1), quads=nq, vertices=len(mesh.verts), bsp_nodes=len(bsp), leaves=len(leaves),
                restart_points=len(nodes), ring_points=nring, portals=len(side), nav_frames=len(lanes[0]) if lanes else 0,
                vertex_misses=misses, vertices_lifted=lifted,
                lift_top=[dict(raise_y=d, x=x, z=z, section=s) for d, x, z, s in sorted(lift_top, reverse=True)[:LIFT_REPORT]],
                lev_bytes=len(lev_bytes), pointers=len(L.ptrs), start_row=start_row,
                per_section=sec_info)
    model = dict(nodes=nodes, ring=nring, side=side, lap=lap, start_row=start_row, node_rows=node_rows,
                 offset=offset, s_start=s_start, quads=mesh.quads, verts=mesh.verts)
    return lev_bytes, make_vrm(), info, model
