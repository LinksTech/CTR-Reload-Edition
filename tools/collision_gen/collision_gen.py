"""Collision model generator: a track model (glTF binary) and its material
classes in, a LEV and VRM for the game out (packed into a track container by
`rldpack make`).

    python collision_gen.py --glb MODEL.glb --classes CLASSES.tsv --start START.txt
                            --route ROUTE.tsv --out DIR [--name NAME]
                            [--road-class road] [--edge-class edge]

Inputs:
  CLASSES.tsv  material <tab> class, one per line after a header line. The
               triangles of the road class (and the edge class: drivable, slower)
               make the collision; every other class is left out.
  START.txt    one line `x y z dx dz`: the start line and the driving direction.
  ROUTE.tsv    a header line, then one line per section in driving order:
               section start_x start_y start_z dir_x dir_z end_x end_y end_z
               [portal [curtain [guide]]]
               One section whose end is its start is a closed loop. With several
               sections each one ends at a portal to the next (portal 1); curtain
               is the distance of the portal plane past the end point; guide is
               an optional file of `x y z width` rows along the middle of the
               road that the tracer measures along instead of steering itself.
  Positions are game world units and axes (y up); nothing is transformed.

Outputs in DIR: NAME.lev, NAME.vrm, NAME_centreline.tsv (the rows of every
section) and NAME_report.json (the build figures and every check). Nothing is
written when a check fails.
"""

import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import centreline  # noqa: E402
import glb_io  # noqa: E402
import lev_build  # noqa: E402
import lev_verify  # noqa: E402
import progress_sim  # noqa: E402
from surface import KIND_EDGE, KIND_ROAD, Surface  # noqa: E402


class InputError(ValueError):
    pass


def read_classes(path):
    out = {}
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f):
            if n == 0 or not line.strip():
                continue
            cols = line.rstrip('\r\n').split('\t')
            if len(cols) < 2:
                raise InputError('%s line %d: expected material <tab> class' % (path, n + 1))
            out[cols[0]] = cols[1].strip()
    return out


def read_start(path):
    with open(path, encoding='utf-8') as f:
        vals = f.read().split()
    if len(vals) != 5:
        raise InputError('%s: expected one line "x y z dx dz"' % path)
    return tuple(float(v) for v in vals)


def read_route(path):
    secs = []
    base = os.path.dirname(os.path.abspath(path))
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f):
            if n == 0 or not line.strip() or line.startswith('#'):
                continue
            c = line.rstrip('\r\n').split('\t')
            if len(c) < 9:
                raise InputError('%s line %d: expected at least 9 columns' % (path, n + 1))
            v = [float(t) for t in c[1:9]]
            sec = dict(start=tuple(v[0:3]), dir=(v[3], v[4]), end=tuple(v[5:8]))
            sec['portal'] = None if len(c) < 10 or c[9] == '' else bool(int(c[9]))
            sec['curtain'] = 0.0 if len(c) < 11 or c[10] == '' else float(c[10])
            sec['guide'] = None if len(c) < 12 or c[11] == '' else os.path.join(base, c[11])
            secs.append(sec)
    if not secs:
        raise InputError('%s: no section' % path)
    return secs


# Rows along the road and the widest quadblock across: when the collision does
# not follow the road closely enough (the height check), the next, finer level
# is built. Finer than 128 would make triangles shorter than 64, and the game's
# sphere search keeps at most 15 triangles (COLL_MOVED_FindScrub, game/COLL.c).
LEVELS = ((256.0, 512.0), (128.0, 256.0))


def route_band(sections, margin=32.0, rise=60.0):
    """A predicate for the height check: is (x, z, y) on the road band the
    sections trace (inside the traced width plus margin, within rise of the
    row height)? Road triangles of the class that lie off the route (a side
    corridor, a pipe) are then left out of the check."""
    cell = 512.0
    grid = {}
    for sec in sections:
        rows = sec['rows']
        for i, r in enumerate(rows):
            key = (int(r['x'] // cell), int(r['z'] // cell))
            grid.setdefault(key, []).append(r)
    reach = 1

    def inside(x, z, y):
        cx, cz = int(x // cell), int(z // cell)
        for ix in range(cx - reach, cx + reach + 1):
            for iz in range(cz - reach, cz + reach + 1):
                for r in grid.get((ix, iz), ()):
                    px, pz = x - r['x'], z - r['z']
                    along = px * r['dx'] + pz * r['dz']
                    if abs(along) > 160.0 or abs(y - r['y']) > rise:
                        continue
                    lat = px * r['dz'] - pz * r['dx']
                    if -r['dr'] - margin <= lat <= r['dl'] + margin:
                        return True
        return False
    return inside


def generate(glb, classes, start, route, road_class='road', edge_class='edge', log=print, region=None, nav=True):
    """region: None (the height check covers every road triangle), 'route' (only
    the band the sections trace) or a predicate (x, z, y) -> bool."""
    """Everything in memory; returns (lev, vrm, report, model, sections, surface)."""
    road = sorted(m for m, c in classes.items() if c == road_class)
    edge = sorted(m for m, c in classes.items() if c == edge_class)
    if not road:
        raise InputError('no material has the class %r' % road_class)
    model = glb_io.read_glb(glb, want=set(road) | set(edge))
    surface = Surface()
    for name in road:
        if name in model['triangles']:
            surface.add(model['triangles'][name], KIND_ROAD)
    for name in edge:
        if name in model['triangles']:
            surface.add(model['triangles'][name], KIND_EDGE)
    log('surface: %d road, %d edge triangles' % (surface.count[KIND_ROAD], surface.count.get(KIND_EDGE, 0)))
    guides = [centreline.read_guide(sec['guide']) if sec['guide'] else None for sec in route]
    tried = []
    for level, (row, column) in enumerate(LEVELS):
        sections = []
        for i, sec in enumerate(route):
            closed = len(route) == 1 and math.dist(sec['start'], sec['end']) < 1.0
            portal = sec['portal'] if sec['portal'] is not None else len(route) > 1
            cl = centreline.build_centreline(surface, sec['start'], sec['dir'], sec['end'], closed, guide=guides[i],
                                             tail=lev_build.PORTAL_TAIL if portal else 0.0, row=row)
            cl['portal'] = portal
            cl['curtain'] = sec['curtain']
            log('section %d: %d rows of %.0f, length %.0f, gaps %s, inner cut %d'
                % (i, len(cl['rows']), row, cl['length'], [(g[0], round(g[3]), round(g[4])) for g in cl['gaps']], cl['inner_cut']))
            sections.append(cl)
        lev, vrm, info, built = lev_build.build_lev(sections, start[:3], surface=surface, column_max=column, nav=nav)
        skip = lev_build.edge_conflicts(built['quads'], built['verts'])
        if skip:
            log('%d edge quadblocks lie under or over another part of the track: left out' % len(skip))
            lev, vrm, info, built = lev_build.build_lev(sections, start[:3], surface=surface, column_max=column, nav=nav,
                                                        skip=frozenset(skip))
            info['edge_left_out'] = len(skip)
        band = route_band(sections) if region == 'route' else region
        verify = lev_verify.verify_lev(lev, surface=surface, region=band)
        height = verify.get('height', {})
        tried.append(dict(row=row, column=column, quads=info['quads'], rate=height.get('rate'), max_error=height.get('max_error')))
        log('level %d (rows %.0f, columns %.0f): %d quadblocks, height rate %s, max error %s'
            % (level, row, column, info['quads'], height.get('rate'), height.get('max_error')))
        if not verify['errors'] or level == len(LEVELS) - 1:
            break
    report = dict(build=info, levels=tried)
    report['verify'] = verify
    if region == 'route' and len(sections) > 1:
        per = []
        for sec in sections:
            h = lev_verify.verify_lev(lev, surface=surface, region=route_band([sec])).get('height') or {}
            per.append({k: h.get(k) for k in ('points', 'good', 'rate', 'in_target', 'below', 'missed', 'delta_p01', 'delta_p99',
                                               'delta_max')})
        report['height_by_section'] = per
    report['progress'] = progress_sim.simulate(lev, built, sections)
    return lev, vrm, report, built, sections, surface


def failures(report, height_warn=False, conflict_warn=False):
    """The errors that stop the generator. With height_warn the height check
    (check 10), with conflict_warn the layer check (check 11) only warns: for a
    model the collision cannot follow everywhere, the figures are in the report
    and the track is written, to be judged by driving it. Neither switch
    changes a byte of what is built."""
    soft = tuple(c for c, on in (('check 10', height_warn), ('check 11', conflict_warn)) if on)
    out = [e for e in report['verify'].get('errors', []) if not (soft and e.startswith(soft))]
    out += report['progress'].get('errors', [])
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--glb', required=True)
    ap.add_argument('--classes', required=True)
    ap.add_argument('--start', required=True)
    ap.add_argument('--route', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--name', default='track')
    ap.add_argument('--road-class', default='road')
    ap.add_argument('--edge-class', default='edge')
    ap.add_argument('--height-warn', action='store_true',
                    help='the height check only warns (the figures stay in the report)')
    ap.add_argument('--conflict-warn', action='store_true',
                    help='the layer check (two ground layers too close) only warns')
    ap.add_argument('--no-nav', action='store_true',
                    help='no nav paths: the game makes no bots (pack with rldpack make --keep-nav)')
    ap.add_argument('--height-region', choices=('all', 'route'), default='all',
                    help='route: the height check covers only the road band of the route')
    a = ap.parse_args(argv)
    try:
        lev, vrm, report, built, sections, _ = generate(a.glb, read_classes(a.classes), read_start(a.start), read_route(a.route),
                                                        a.road_class, a.edge_class,
                                                        region='route' if a.height_region == 'route' else None,
                                                        nav=not a.no_nav)
    except (InputError, centreline.TraceError, lev_build.BuildError, ValueError) as e:
        print('collision_gen: %s' % e, file=sys.stderr)
        return 1
    errs = failures(report, a.height_warn, a.conflict_warn)
    for e in report['verify'].get('errors', []):
        if (a.height_warn and e.startswith('check 10')) or (a.conflict_warn and e.startswith('check 11')):
            print('collision_gen: WARNING %s' % e, file=sys.stderr)
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, a.name + '_report.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1, sort_keys=True)
    if errs:
        print('collision_gen: CHECK FAILED - no track written:\n  ' + '\n  '.join(errs[:20]), file=sys.stderr)
        return 1
    with open(os.path.join(a.out, a.name + '.lev'), 'wb') as f:
        f.write(lev)
    with open(os.path.join(a.out, a.name + '.vrm'), 'wb') as f:
        f.write(vrm)
    with open(os.path.join(a.out, a.name + '_centreline.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('section\trow\tx\ty\tz\tleft\tright\tgap\tdrop\n')
        for si, sec in enumerate(sections):
            for r, row in enumerate(sec['rows']):
                f.write('%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%d\t%d\n' % (si, r, row['x'], row['y'], row['z'], row['dl'], row['dr'],
                                                                         row['gap'], row.get('drop', False)))
    b = report['build']
    print('collision_gen: %d quadblocks, %d restart points, %d portals, lap %.0f, %d bytes; height check %s'
          % (b['quads'], b['restart_points'], b['portals'], b['lap'], b['lev_bytes'], report['verify'].get('height')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
