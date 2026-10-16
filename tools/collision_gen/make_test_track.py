"""Procedural neutral test tracks for the collision generator.

World convention: +Y is up. For a driver heading (dx, dz) the left vector is
(dz, 0, -dx) and the right vector is (-dz, 0, dx).

Usage: python make_test_track.py <out_dir> <kind>   (kind: ring_a, ring_b, ring_c, short)
"""

import argparse
import bisect
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from glb_io import write_glb  # noqa: E402

KINDS = ('ring_a', 'ring_b', 'ring_c', 'short')

POLY_STEP = 8.0        # centre line sample spacing (arc length in XZ)
ROW_EVERY = 4          # cross section every 4th sample (about 32 units)
MAX_PART = 64.0        # maximum lateral cell width
EDGE_WIDTH = 128.0
WALL_HEIGHT = 200.0
SCENERY_DROP = 500.0
SCENERY_MARGIN = 2000.0


# --------------------------------------------------------------------------
# centre line paths
# --------------------------------------------------------------------------

def _make_segment_path(p0x, p0z, d0x, d0z, segs):
    """Path made of ('L', length) lines and ('A', radius, length) left arcs.

    Returns (fn, total_length); fn(s) -> (x, z, dx, dz).
    """
    starts = []
    states = []
    s0 = 0.0
    px, pz, dx, dz = p0x, p0z, d0x, d0z
    for seg in segs:
        starts.append(s0)
        states.append((px, pz, dx, dz))
        length = seg[-1]
        px, pz, dx, dz = _eval_segment(seg, states[-1], length)
        s0 += length
    total = s0

    def fn(s):
        s = min(max(s, 0.0), total)
        i = max(bisect.bisect_right(starts, s) - 1, 0)
        return _eval_segment(segs[i], states[i], s - starts[i])

    return fn, total


def _eval_segment(seg, state, u):
    px, pz, dx, dz = state
    if seg[0] == 'L':
        return px + dx * u, pz + dz * u, dx, dz
    radius = seg[1]
    phi = u / radius
    c, s = math.cos(phi), math.sin(phi)
    lx, lz = dz, -dx
    ndx = dx * c + lx * s
    ndz = dz * c + lz * s
    nlx = lx * c - dx * s
    nlz = lz * c - dz * s
    cx, cz = px + lx * radius, pz + lz * radius
    return cx - nlx * radius, cz - nlz * radius, ndx, ndz


def _make_lemniscate(a, b):
    """Gerono lemniscate sampled uniformly in XZ arc length.

    Returns (fn, total_length, min_radius); fn(s) -> (x, y, z, dx, dz).
    """
    t0 = math.pi / 4.0
    n = 40000
    ts = [t0 + 2.0 * math.pi * i / n for i in range(n + 1)]
    xs = [a * math.sin(t) for t in ts]
    zs = [(b / 2.0) * math.sin(2.0 * t) for t in ts]
    cum = [0.0]
    for i in range(1, n + 1):
        cum.append(cum[-1] + math.hypot(xs[i] - xs[i - 1], zs[i] - zs[i - 1]))
    total = cum[-1]

    max_k = 0.0
    for i in range(n):
        t = ts[i]
        vx, vz = a * math.cos(t), b * math.cos(2.0 * t)
        ax, az = -a * math.sin(t), -2.0 * b * math.sin(2.0 * t)
        k = abs(vx * az - vz * ax) / (vx * vx + vz * vz) ** 1.5
        max_k = max(max_k, k)
    min_radius = 1.0 / max_k

    def fn(s):
        s = min(max(s, 0.0), total)
        i = min(max(bisect.bisect_right(cum, s) - 1, 0), n - 1)
        span = cum[i + 1] - cum[i]
        f = (s - cum[i]) / span if span > 0 else 0.0
        t = ts[i] + (ts[i + 1] - ts[i]) * f
        vx, vz = a * math.cos(t), b * math.cos(2.0 * t)
        norm = math.hypot(vx, vz)
        return (a * math.sin(t), 400.0 * (1.0 - math.cos(t)),
                (b / 2.0) * math.sin(2.0 * t), vx / norm, vz / norm)

    return fn, total, min_radius


# --------------------------------------------------------------------------
# section description
# --------------------------------------------------------------------------

class _Section:
    def __init__(self, length, closed, pos_fn, width_fn, bank_fn=None, bump_fn=None,
                 edge_fn=None, gap_fn=None):
        self.length = length
        self.closed = closed
        self.pos_fn = pos_fn            # s -> (x, y, z, dx, dz)
        self.width_fn = width_fn        # s -> road width
        self.bank_fn = bank_fn or (lambda s: 0.0)    # degrees, > 0 lowers the right side
        self.bump_fn = bump_fn or (lambda s: 0.0)
        self.edge_fn = edge_fn or (lambda s: False)  # edge strips present at s
        self.gap_fn = gap_fn or (lambda s: False)    # no road / wall at s


def _lift(path_fn, y_fn):
    def fn(s):
        x, z, dx, dz = path_fn(s)
        return x, y_fn(s), z, dx, dz
    return fn


def _sample_rows(sec):
    """Centre line polyline every ~8 units; every 4th sample is a cross section."""
    n = ROW_EVERY * max(1, math.ceil(sec.length / (POLY_STEP * ROW_EVERY)))
    samples = []
    for i in range(n + 1):
        s = sec.length * i / n
        x, y, z, dx, dz = sec.pos_fn(s)
        samples.append({'s': s, 'x': x, 'y': y, 'z': z, 'dx': dx, 'dz': dz,
                        'w': sec.width_fn(s), 'bank': sec.bank_fn(s),
                        'bump': sec.bump_fn(s)})
    return samples, samples[::ROW_EVERY]


def _lat_point(row, lat):
    return (row['x'] + lat * row['dz'],
            row['y'] + lat * math.tan(math.radians(row['bank'])) + row['bump'],
            row['z'] - lat * row['dx'])


class _Mesh:
    def __init__(self):
        self.pos = []
        self.idx = []
        self.keys = {}

    def vertex(self, key, p):
        i = self.keys.get(key)
        if i is None:
            i = len(self.pos) // 3
            self.pos.extend(p)
            self.keys[key] = i
        return i

    def tri(self, a, b, c, ref):
        p = self.pos
        ux, uy, uz = (p[3 * b + k] - p[3 * a + k] for k in range(3))
        vx, vy, vz = (p[3 * c + k] - p[3 * a + k] for k in range(3))
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        if nx * ref[0] + ny * ref[1] + nz * ref[2] < 0.0:
            b, c = c, b
        self.idx.extend((a, b, c))


def _build_section(sec, sec_id, meshes, bounds):
    road, edge, wall = meshes['road'], meshes['edge'], meshes['wall']
    _, rows = _sample_rows(sec)
    m = len(rows) - 1
    wmax = max(r['w'] for r in rows)
    n_lat = max(1, math.ceil(wmax / MAX_PART))
    up = (0.0, 1.0, 0.0)

    def row_index(r):
        return r % m if sec.closed else r

    def track_bounds(p):
        bounds['minx'] = min(bounds['minx'], p[0])
        bounds['maxx'] = max(bounds['maxx'], p[0])
        bounds['minz'] = min(bounds['minz'], p[2])
        bounds['maxz'] = max(bounds['maxz'], p[2])

    for r in range(m):
        ra, rb = rows[r], rows[r + 1]
        ia, ib = row_index(r), row_index(r + 1)
        s_mid = 0.5 * (ra['s'] + rb['s'])
        has_edge = sec.edge_fn(s_mid)
        gap = sec.gap_fn(s_mid)

        # road surface
        if not gap:
            for j in range(n_lat):
                v = []
                for (row, ri, jj) in ((ra, ia, j), (ra, ia, j + 1),
                                      (rb, ib, j + 1), (rb, ib, j)):
                    lat = -row['w'] / 2.0 + row['w'] * jj / n_lat
                    p = _lat_point(row, lat)
                    track_bounds(p)
                    bounds['miny'] = min(bounds['miny'], p[1])
                    v.append(road.vertex((sec_id, ri, jj), p))
                road.tri(v[0], v[1], v[2], up)
                road.tri(v[0], v[2], v[3], up)

        # edge strips on both sides
        if has_edge and not gap:
            for side in (1, -1):
                for j in range(2):
                    v = []
                    for (row, ri, jj) in ((ra, ia, j), (ra, ia, j + 1),
                                          (rb, ib, j + 1), (rb, ib, j)):
                        lat = side * (row['w'] / 2.0 + EDGE_WIDTH * jj / 2.0)
                        p = _lat_point(row, lat)
                        track_bounds(p)
                        bounds['miny'] = min(bounds['miny'], p[1])
                        v.append(edge.vertex((sec_id, side, ri, jj), p))
                    edge.tri(v[0], v[1], v[2], up)
                    edge.tri(v[0], v[2], v[3], up)

        # walls on the outer edge
        if not gap:
            e = EDGE_WIDTH if has_edge else 0.0
            for side in (1, -1):
                inward = (-side * ra['dz'], 0.0, side * ra['dx'])
                v = []
                for (row, ri) in ((ra, ia), (rb, ib)):
                    lat = side * (row['w'] / 2.0 + e)
                    low = _lat_point(row, lat)
                    high = (low[0], low[1] + WALL_HEIGHT, low[2])
                    track_bounds(low)
                    v.append(wall.vertex((sec_id, side, ri, e, 0), low))
                    v.append(wall.vertex((sec_id, side, ri, e, 1), high))
                # v: a_low, a_high, b_low, b_high
                wall.tri(v[0], v[2], v[3], inward)
                wall.tri(v[0], v[3], v[1], inward)


def _section_specs(kind):
    """Return (sections, suffix, min_radius, extra_info)."""
    two_pi = 2.0 * math.pi
    if kind in ('ring_a', 'short'):
        if kind == 'ring_a':
            straight, radius = 9000.0, 3000.0
        else:
            straight, radius = 3000.0, 2000.0
        arc = math.pi * radius
        segs = [('L', straight), ('A', radius, arc), ('L', straight), ('A', radius, arc)]
        path, lap = _make_segment_path(0.0, 0.0, -1.0, 0.0, segs)
        if kind == 'ring_a':
            second_a = straight + arc + straight
            second_b = lap

            def blend(s):
                if second_a <= s <= second_b:
                    return 1.0
                d = min(abs(s - second_a), abs(s - second_b),
                        abs(s + lap - second_b), abs(s - lap - second_a))
                return max(0.0, 1.0 - d / 1000.0)

            sec = _Section(
                lap, True,
                _lift(path, lambda s: 300.0 * (1.0 - math.cos(two_pi * s / lap))),
                lambda s: 1250.0 + 350.0 * math.sin(two_pi * 3.0 * s / lap),
                # inside of the turn is the left side, which is lowered
                bank_fn=lambda s: -6.0 * blend(s),
                # gentle waves of 6 units over 1024: the collision follows them within the tolerance
                bump_fn=lambda s: 6.0 * math.sin(two_pi * s / 1024.0),
                edge_fn=lambda s: 1000.0 <= s <= 8000.0)
        else:
            sec = _Section(lap, True, _lift(path, lambda s: 0.0), lambda s: 1000.0)
        return [sec], 'a', radius, {}

    if kind == 'ring_b':
        a, b = 10000.0, 16000.0
        scale = 1.0
        while True:
            fn, lap, min_radius = _make_lemniscate(a * scale, b * scale)
            if min_radius >= 1200.0:
                break
            scale *= 1200.0 / min_radius * 1.001
        sec = _Section(lap, True, fn, lambda s: 1000.0)
        return [sec], 'b', min_radius, {'scale': scale}

    if kind == 'ring_c':
        u_segs = [('L', 4000.0), ('A', 2000.0, math.pi * 2000.0), ('L', 4000.0)]
        starts = ((-14000.0, 0.0, 3000.0, 1.0, 0.0),
                  (20000.0, 200.0, -2000.0, 1.0, 0.0),
                  (-3000.0, -100.0, 22000.0, 0.0, -1.0))
        sections = []
        for i, (x, y, z, dx, dz) in enumerate(starts):
            path, length = _make_segment_path(x, z, dx, dz, u_segs)
            gap = (lambda s: 5000.0 < s < 5300.0) if i == 1 else None
            sections.append(_Section(length, False, _lift(path, lambda s, y=y: y),
                                     lambda s: 1100.0, gap_fn=gap))
        return sections, 'c', 2000.0, {}

    raise ValueError('unknown track kind: %s' % kind)


# --------------------------------------------------------------------------
# public API
# --------------------------------------------------------------------------

def build_track(kind):
    if kind not in KINDS:
        raise ValueError('unknown track kind: %s' % kind)
    sections, suffix, min_radius, extra = _section_specs(kind)
    meshes = {'road': _Mesh(), 'edge': _Mesh(), 'wall': _Mesh(), 'scenery': _Mesh()}
    inf = float('inf')
    bounds = {'minx': inf, 'maxx': -inf, 'minz': inf, 'maxz': -inf, 'miny': inf}
    for i, sec in enumerate(sections):
        _build_section(sec, i, meshes, bounds)

    scen = meshes['scenery']
    y = bounds['miny'] - SCENERY_DROP
    x0, x1 = bounds['minx'] - SCENERY_MARGIN, bounds['maxx'] + SCENERY_MARGIN
    z0, z1 = bounds['minz'] - SCENERY_MARGIN, bounds['maxz'] + SCENERY_MARGIN
    corners = [scen.vertex(k, p) for k, p in enumerate(
        ((x0, y, z0), (x0, y, z1), (x1, y, z1), (x1, y, z0)))]
    scen.tri(corners[0], corners[1], corners[2], (0.0, 1.0, 0.0))
    scen.tri(corners[0], corners[2], corners[3], (0.0, 1.0, 0.0))

    names = [('road', 'road_' + suffix, 'road'), ('edge', 'edge_' + suffix, 'edge'),
             ('wall', 'wall_' + suffix, 'wall'), ('scenery', 'scenery_' + suffix, 'scenery')]
    materials, prims, classes, tri_counts = [], [], {}, {}
    for key, name, cls in names:
        mesh = meshes[key]
        if not mesh.idx:
            continue
        materials.append(name)
        classes[name] = cls
        tri_counts[name] = len(mesh.idx) // 3
        prims.append((len(materials) - 1, mesh.pos, mesh.idx))

    route = []
    for sec in sections:
        x, yy, z, dx, dz = sec.pos_fn(0.0)
        ex, ey, ez, _, _ = sec.pos_fn(sec.length)
        end = (x, yy, z) if sec.closed else (ex, ey, ez)
        route.append({'start': (x, yy, z), 'dir': (dx, dz), 'end': end})

    if kind == 'ring_c':
        x, yy, z, dx, dz = sections[0].pos_fn(2000.0)
    else:
        x, yy, z, dx, dz = sections[0].pos_fn(0.0)
    info = {'lap': sum(sec.length for sec in sections), 'sections': len(sections),
            'min_radius': min_radius, 'triangles': tri_counts}
    info.update(extra)
    return {'materials': materials, 'prims': prims, 'classes': classes,
            'start': (x, yy, z, dx, dz), 'route': route, 'info': info}


def _fmt(v):
    r = round(v, 3)
    if r == int(r):
        return str(int(r))
    return '%.3f' % r


def write_track(out_dir, kind):
    track = build_track(kind)
    os.makedirs(out_dir, exist_ok=True)
    paths = {'glb': os.path.join(out_dir, kind + '.glb'),
             'classes': os.path.join(out_dir, kind + '_classes.tsv'),
             'start': os.path.join(out_dir, kind + '_start.txt'),
             'route': os.path.join(out_dir, kind + '_route.tsv')}
    write_glb(paths['glb'], track['materials'], track['prims'])
    with open(paths['classes'], 'w', newline='\n') as f:
        f.write('material\tclass\n')
        for name in sorted(track['classes']):
            f.write('%s\t%s\n' % (name, track['classes'][name]))
    with open(paths['start'], 'w', newline='\n') as f:
        f.write(' '.join(_fmt(v) for v in track['start']) + '\n')
    with open(paths['route'], 'w', newline='\n') as f:
        f.write('section\tstart_x\tstart_y\tstart_z\tdir_x\tdir_z\tend_x\tend_y\tend_z\n')
        for i, r in enumerate(track['route']):
            vals = (list(r['start']) + list(r['dir']) + list(r['end']))
            f.write('%d\t%s\n' % (i, '\t'.join(_fmt(v) for v in vals)))
    return paths


def main(argv=None):
    parser = argparse.ArgumentParser(description='Write a procedural test track.')
    parser.add_argument('out_dir')
    parser.add_argument('kind', choices=KINDS)
    args = parser.parse_args(argv)
    paths = write_track(args.out_dir, args.kind)
    for key in sorted(paths):
        print('%s: %s' % (key, paths[key]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
