"""The centre line of each section of a track, traced on the road surface.

A section is driven from its start point in its start direction until it
reaches its end point (an open section, which ends at a portal) or comes back
to its start (a closed loop). Every STEP units the tracer cuts across the road
at the predicted point and takes the middle of the road run it finds there:

  - the run is followed sideways in 16-unit samples, on the layer nearest the
    height of the last sample (so an overpass above or below stays apart);
  - the middle may move at most SHIFT_MAX sideways per step, each half width
    at most GROW_MAX per step (a junction or a wide place does not pull the
    line away);
  - where no road is found (a gap between platforms), the line goes straight
    on for at most gap_max units; the height there is interpolated later.

Afterwards the points are smoothed and resampled to rows ROW units apart.
Every row carries its centre, its direction, the half widths of the road and
of the drivable area (road plus edge) and whether it lies in a gap.
"""

import math

from surface import KIND_EDGE, KIND_ROAD

STEP = 128.0
SIDE = 16.0
SIDE_MAX = 4096.0
SIDE_RISE = 48.0          # a sideways sample higher or lower than this is not the same surface
SHIFT_MAX = 32.0
GROW_MAX = 32.0
WINDOW = 192.0
END_REACH = 256.0
MAX_STEPS = 4096
ROW = 256.0
SMOOTH = 4                # moving average over 2 * SMOOTH + 1 points
EDGE_MAX = 512.0          # the drivable edge reaches at most this far past the road
EDGE_OUTSET = 24.0        # the collision reaches this far past the traced edge (lev_build.OUTSET)


class TraceError(ValueError):
    pass


def _norm(x, z):
    n = math.hypot(x, z)
    if n < 1e-9:
        raise TraceError('direction of length zero')
    return x / n, z / n


def _march(surface, x, z, y, lx, lz, sign, kinds):
    """Walk sideways from (x, z) at height y in steps of SIDE while the surface
    continues; returns (last lateral distance on road, last on any kind)."""
    road = 0.0
    any_kind = 0.0
    yp = y
    d = SIDE
    while d <= SIDE_MAX:
        hit = surface.surface(x + sign * lx * d, z + sign * lz * d, yp, SIDE_RISE, kinds)
        if hit is None:
            break
        yp, kind = hit
        any_kind = d
        if kind == KIND_ROAD:
            road = d
        d += SIDE
    return road, any_kind


def cross_section(surface, x, z, y_ref, hx, hz, search):
    """The road run across (x, z) for heading (hx, hz).

    Returns None (no road within `search` sideways) or a dict with the lateral
    offsets (positive = left) of the road run and the drivable run, and the
    height under the middle of the road run."""
    lx, lz = hz, -hx            # the driver's left
    kinds = (KIND_ROAD, KIND_EDGE)
    found = None
    hit = surface.surface(x, z, y_ref, WINDOW, (KIND_ROAD,))
    if hit is not None:
        found = (0.0, hit[0])
    else:
        d = SIDE
        while d <= search and found is None:
            for sgn in (1, -1):
                h = surface.surface(x + sgn * lx * d, z + sgn * lz * d, y_ref, WINDOW, (KIND_ROAD,))
                if h is not None:
                    found = (sgn * d, h[0])
                    break
            d += SIDE
    if found is None:
        return None
    lat0, y0 = found
    cx, cz = x + lx * lat0, z + lz * lat0
    road_l, drv_l = _march(surface, cx, cz, y0, lx, lz, 1, kinds)
    road_r, drv_r = _march(surface, cx, cz, y0, lx, lz, -1, kinds)
    # the road part of the run: from the last road sample on the right to the last on the left
    left = lat0 + road_l
    right = lat0 - road_r
    mid = (left + right) / 2.0
    hm = surface.surface(x + lx * mid, z + lz * mid, y0, WINDOW, kinds)
    return dict(left=left, right=right, drv_left=lat0 + drv_l, drv_right=lat0 - drv_r,
                y=(hm[0] if hm else y0))


def trace_section(surface, start, direction, end, closed, gap_max=1024.0):
    """Raw centre points of one section: a list of dicts x, y, z, half widths
    (road hl/hr, drivable dl/dr, all positive) and gap (bool)."""
    x, y, z = start
    hx, hz = _norm(*direction)
    first = surface.surface(x, z, y, WINDOW, (KIND_ROAD,))
    if first is None:
        raise TraceError('no road under the section start (%.0f, %.0f, %.0f)' % (x, y, z))
    y = first[0]
    pts = []
    prev = None
    travelled = 0.0
    gap = 0.0
    slope = 0.0
    for _ in range(MAX_STEPS):
        search = max(256.0, prev['hl'] + prev['hr']) / 2.0 if prev else 512.0
        cs = cross_section(surface, x, z, y, hx, hz, search)
        lx, lz = hz, -hx
        if cs is None:
            gap += STEP
            if gap > gap_max:
                raise TraceError('the road breaks off for more than %.0f units after (%.0f, %.0f, %.0f)'
                                 % (gap_max, x, y, z))
            if prev is None:
                raise TraceError('no road at the section start')
            p = dict(x=x, y=None, z=z, hl=prev['hl'], hr=prev['hr'], dl=prev['dl'], dr=prev['dr'], gap=True)
        else:
            gap = 0.0
            mid = (cs['left'] + cs['right']) / 2.0
            mid = max(-SHIFT_MAX, min(SHIFT_MAX, mid))
            hl = cs['left'] - mid
            hr = mid - cs['right']
            dl = cs['drv_left'] - mid
            dr = mid - cs['drv_right']
            if prev is not None:
                hl = max(prev['hl'] - GROW_MAX, min(prev['hl'] + GROW_MAX, hl))
                hr = max(prev['hr'] - GROW_MAX, min(prev['hr'] + GROW_MAX, hr))
                dl = max(hl, max(prev['dl'] - GROW_MAX, min(prev['dl'] + GROW_MAX, dl)))
                dr = max(hr, max(prev['dr'] - GROW_MAX, min(prev['dr'] + GROW_MAX, dr)))
            p = dict(x=x + lx * mid, y=cs['y'], z=z + lz * mid, hl=hl, hr=hr, dl=max(dl, hl), dr=max(dr, hr), gap=False)
        if pts:
            travelled += math.hypot(p['x'] - pts[-1]['x'], p['z'] - pts[-1]['z'])
        pts.append(p)
        prev = p
        # the end
        dend = math.hypot(p['x'] - end[0], p['z'] - end[2])
        if closed:
            if travelled > 4 * END_REACH and dend < END_REACH:
                pts.pop()            # the loop closes on the first point
                return pts
        elif dend < END_REACH and travelled > STEP:
            return pts
        # heading from the last points, the next prediction
        if len(pts) >= 3:
            a = pts[-3]
            hx, hz = _norm(p['x'] - a['x'], p['z'] - a['z'])
        known = [q for q in pts[-4:] if q['y'] is not None]
        if len(known) >= 2:
            dist = math.hypot(known[-1]['x'] - known[0]['x'], known[-1]['z'] - known[0]['z'])
            slope = (known[-1]['y'] - known[0]['y']) / dist if dist > 1 else 0.0
        if p['y'] is not None:
            y = p['y'] + slope * STEP
        else:
            y = y + slope * STEP
        x = p['x'] + hx * STEP
        z = p['z'] + hz * STEP
    raise TraceError('the section does not reach its end within %d steps (last at %.0f, %.0f)'
                     % (MAX_STEPS, x, z))


def _resample_guide(guide, closed):
    """Guide points (x, y, z, width) every STEP units of x/z arc length."""
    pts = list(guide)
    if closed and math.hypot(pts[0][0] - pts[-1][0], pts[0][2] - pts[-1][2]) < 1.0:
        pts = pts[:-1]
    seq = pts + ([pts[0]] if closed else [])
    s = [0.0]
    for i in range(1, len(seq)):
        s.append(s[-1] + math.hypot(seq[i][0] - seq[i - 1][0], seq[i][2] - seq[i - 1][2]))
    total = s[-1]
    n = max(2, int(round(total / STEP)))
    out = []
    j = 0
    for k in range(n if closed else n + 1):
        t = total * k / n
        while j < len(seq) - 2 and s[j + 1] < t:
            j += 1
        span = s[j + 1] - s[j]
        f = max(0.0, min(1.0, (t - s[j]) / span if span > 0 else 0.0))
        a, b = seq[j], seq[j + 1]
        out.append(tuple(a[i] + f * (b[i] - a[i]) for i in range(4)))
    return out


def trace_guided(surface, guide, closed):
    """Raw centre points along a guide line (an outside estimate of the middle
    of the road with its width): at every guide point the road run across is
    measured on the layer nearest the guide's height; the middle may differ
    from the guide by at most a quarter of the width."""
    g = _resample_guide(guide, closed)
    n = len(g)
    pts = []
    prev = None
    for k in range(n):
        a = g[(k - 1) % n] if closed or k > 0 else g[k]
        b = g[(k + 1) % n] if closed or k < n - 1 else g[k]
        hx, hz = _norm(b[0] - a[0], b[2] - a[2])
        lx, lz = hz, -hx
        x, y, z, w = g[k]
        cs = cross_section(surface, x, z, y, hx, hz, max(256.0, w / 2.0))
        if cs is None:
            if prev is None:
                hl = hr = w / 2.0
                dl, dr = hl, hr
            else:
                hl, hr, dl, dr = prev['hl'], prev['hr'], prev['dl'], prev['dr']
            p = dict(x=x, y=None, z=z, hl=hl, hr=hr, dl=dl, dr=dr, gap=True)
        else:
            lim = max(64.0, w / 4.0)
            mid = max(-lim, min(lim, (cs['left'] + cs['right']) / 2.0))
            hl = max(16.0, cs['left'] - mid)
            hr = max(16.0, mid - cs['right'])
            p = dict(x=x + lx * mid, y=cs['y'], z=z + lz * mid, hl=hl, hr=hr,
                     dl=max(hl, cs['drv_left'] - mid), dr=max(hr, mid - cs['drv_right']), gap=False)
        pts.append(p)
        prev = p
    return pts


def classify_gaps(rows, closed, bridge_slope=math.tan(math.radians(30.0)), drop_max=384.0):
    """Every run of gap rows becomes a bridge (ground without texture, the height
    interpolated) when the slope between its two ends is at most bridge_slope,
    or a drop (no ground, the kart falls onto the lower side) when it goes down
    more steeply over at most drop_max units. Anything else is refused.
    Returns a list of (kind, first row, last row, length, height change)."""
    n = len(rows)
    out = []
    i = 0
    while i < n:
        if not rows[i]['gap']:
            i += 1
            continue
        j = i
        while j + 1 < n and rows[j + 1]['gap']:
            j += 1
        if (i == 0 or j == n - 1) and not closed:
            raise TraceError('a section starts or ends in a gap (rows %d..%d)' % (i, j))
        a, b = rows[(i - 1) % n], rows[(j + 1) % n]
        length = math.hypot(b['x'] - a['x'], b['z'] - a['z'])
        dy = b['y'] - a['y']
        if abs(dy) <= bridge_slope * length:
            kind = 'bridge'
        elif dy < 0 and length <= drop_max:
            kind = 'drop'
        else:
            raise TraceError('a gap of %.0f units with a height change of %.0f (rows %d..%d at %.0f, %.0f) is '
                             'neither a bridge nor a drop' % (length, dy, i, j, a['x'], a['z']))
        for k in range(i, j + 1):
            rows[k]['drop'] = kind == 'drop'
        out.append((kind, i, j, length, dy))
        i = j + 1
    return out


def _fill_gaps(pts, closed):
    """Heights of gap points: linear between the known neighbours."""
    n = len(pts)
    known = [i for i in range(n) if pts[i]['y'] is not None]
    if not known:
        raise TraceError('a section without any road height')
    for i in range(n):
        if pts[i]['y'] is not None:
            continue
        before = [k for k in known if k < i]
        after = [k for k in known if k > i]
        if closed:
            a = before[-1] if before else known[-1] - n
            b = after[0] if after else known[0] + n
        else:
            if not before or not after:
                raise TraceError('a section starts or ends in a gap')
            a, b = before[-1], after[0]
        t = (i - a) / float(b - a)
        pts[i]['y'] = pts[a % n]['y'] + t * (pts[b % n]['y'] - pts[a % n]['y'])


def _smooth(pts, closed):
    n = len(pts)
    keys = ('x', 'y', 'z', 'hl', 'hr', 'dl', 'dr')
    out = []
    for i in range(n):
        acc = dict.fromkeys(keys, 0.0)
        cnt = 0
        for k in range(i - SMOOTH, i + SMOOTH + 1):
            if closed:
                q = pts[k % n]
            elif 0 <= k < n:
                q = pts[k]
            else:
                continue
            for key in keys:
                acc[key] += q[key]
            cnt += 1
        p = {key: acc[key] / cnt for key in keys}
        p['gap'] = pts[i]['gap']
        out.append(p)
    if not closed:
        for i in (0, n - 1):            # the ends stay where they are
            for key in ('x', 'y', 'z'):
                out[i][key] = pts[i][key]
    return out


def resample(pts, closed, tail=0.0, row=ROW):
    """Rows ROW apart along the x/z arc length (an even number of intervals).
    With `tail` > 0 (an open section that ends at a portal) the last `tail`
    units form one extra short segment: rows at L - tail, L - tail/2 and L.
    Returns (rows, length). `row` is the spacing of the rows."""
    n = len(pts)
    m = n + 1 if closed else n
    s = [0.0]
    for i in range(1, m):
        a, b = pts[(i - 1) % n], pts[i % n]
        s.append(s[-1] + math.hypot(b['x'] - a['x'], b['z'] - a['z']))
    length = s[-1]
    main = length - tail
    nseg = max(1, int(round(main / (2 * row))))
    targets = [main * k / (2.0 * nseg) for k in range(2 * nseg + (0 if closed else 1))]
    if tail > 0:
        targets += [length - tail / 2.0, length]
    rows = []
    j = 0
    keys = ('x', 'y', 'z', 'hl', 'hr', 'dl', 'dr')
    for t in targets:
        while j < m - 2 and s[j + 1] < t:
            j += 1
        a, b = pts[j % n], pts[(j + 1) % n]
        span = s[j + 1] - s[j]
        f = (t - s[j]) / span if span > 0 else 0.0
        f = max(0.0, min(1.0, f))
        r = {key: a[key] + f * (b[key] - a[key]) for key in keys}
        r['gap'] = a['gap'] or b['gap']
        r['s'] = t
        rows.append(r)
    count = len(rows)
    for i, r in enumerate(rows):
        if closed:
            a, b = rows[(i - 1) % count], rows[(i + 1) % count]
        else:
            a, b = rows[max(0, i - 1)], rows[min(count - 1, i + 1)]
        r['dx'], r['dz'] = _norm(b['x'] - a['x'], b['z'] - a['z'])
    return rows, length


def limit_inner(rows, closed, share=0.8, margin=32.0):
    """Where the road bends tighter than its width allows, the inner side would
    fold over itself (quadblocks upside down, two layers): the inner half widths
    are cut to share * the smallest radius over five rows, minus margin.
    Returns the number of rows cut."""
    n = len(rows)
    radius = [None] * n
    left = [False] * n
    for i in range(n):
        if not closed and (i == 0 or i == n - 1):
            continue
        a, b, c = rows[(i - 1) % n], rows[i], rows[(i + 1) % n]
        ax, az = b['x'] - a['x'], b['z'] - a['z']
        bx, bz = c['x'] - b['x'], c['z'] - b['z']
        la, lb = math.hypot(ax, az), math.hypot(bx, bz)
        ang = math.atan2(abs(ax * bz - az * bx), ax * bx + az * bz)
        radius[i] = 0.5 * (la + lb) / ang if ang > 1e-6 else None
        # The left of a heading (hx, hz) is (hz, -hx): the road turns left when
        # the second step leans toward the left of the first.
        left[i] = (bx * az - bz * ax) > 0
    cut = 0
    for i in range(n):
        for side_left in (True, False):
            near = []
            for k in range(i - 2, i + 3):
                if not closed and not 0 <= k < n:
                    continue
                k %= n
                if radius[k] is not None and left[k] == side_left:
                    near.append(radius[k])
            if not near:
                continue
            limit = max(16.0, share * min(near) - margin - EDGE_OUTSET)
            keys = ('hl', 'dl') if side_left else ('hr', 'dr')
            hit = False
            for key in keys:
                if rows[i][key] > limit:
                    rows[i].setdefault(key + '0', rows[i][key])     # the width before the cut
                    rows[i][key] = limit
                    hit = True
            cut += hit
    return cut


def clip_edges(surface, rows):
    """The drivable edge beyond the road (dl, dr past hl, hr) only as far as the
    model has a surface there: smoothing the widths along the road can carry an
    edge out over open ground, where it would lie under another part of the
    track (a ramp crossing above). Each side is walked outward from the road
    edge on the model's surface; the edge stops where that surface ends."""
    cut = 0
    for r in rows:
        if r['gap']:
            continue
        lx, lz = r['dz'], -r['dx']
        for sign, road_key, edge_key in ((1, 'hl', 'dl'), (-1, 'hr', 'dr')):
            if r[edge_key] <= r[road_key] + SIDE:
                continue
            ex = r['x'] + sign * lx * r[road_key]
            ez = r['z'] + sign * lz * r[road_key]
            start = surface.surface(ex, ez, r['y'], WINDOW, (KIND_ROAD, KIND_EDGE))
            if start is None:
                reach = 0.0
            else:
                _, reach = _march(surface, ex, ez, start[0], lx, lz, sign, (KIND_ROAD, KIND_EDGE))
            if r[road_key] + reach < r[edge_key]:
                r[edge_key] = r[road_key] + reach
                cut += 1
    return cut


def build_centreline(surface, start, direction, end, closed, gap_max=1024.0, guide=None, tail=0.0, row=ROW):
    """The rows of one section, ready for the quadblocks. With a guide (list of
    (x, y, z, width)) the tracer measures along it instead of steering itself.
    tail > 0: the last segment is that long (a section that ends at a portal)."""
    if guide:
        pts = trace_guided(surface, guide, closed)
        run = 0.0
        for k, p in enumerate(pts):
            run = run + STEP if p['gap'] else 0.0
            if run > gap_max:
                raise TraceError('the road breaks off for more than %.0f units near (%.0f, %.0f)' % (gap_max, p['x'], p['z']))
    else:
        pts = trace_section(surface, start, direction, end, closed, gap_max)
    _fill_gaps(pts, closed)
    pts = _smooth(pts, closed)
    rows, length = resample(pts, closed, tail, row)
    for r in rows:
        r['drop'] = False
        r['dl'] = min(r['dl'], r['hl'] + EDGE_MAX)
        r['dr'] = min(r['dr'], r['hr'] + EDGE_MAX)
    clip_edges(surface, rows)
    gaps = classify_gaps(rows, closed)
    cut = limit_inner(rows, closed)
    return dict(rows=rows, length=length, closed=closed, raw_points=len(pts), inner_cut=cut, gaps=gaps, row=row)


def read_guide(path):
    """A guide line: rows `x y z width`, one per line ('#' starts a comment, a
    first line that does not start with a number is a header)."""
    pts = []
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.split('#')[0].strip()
            if not line:
                continue
            v = line.split()
            try:
                vals = [float(t) for t in v[:4]]
            except ValueError:
                if not pts:
                    continue
                raise TraceError('%s line %d: expected "x y z width"' % (path, n))
            if len(vals) != 4:
                raise TraceError('%s line %d: expected "x y z width"' % (path, n))
            pts.append(tuple(vals))
    if len(pts) < 3:
        raise TraceError('%s: fewer than 3 points' % path)
    return pts
