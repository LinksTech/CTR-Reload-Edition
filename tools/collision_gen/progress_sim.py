"""Lap progress of a kart driven over a generated LEV, computed with the game's
integer arithmetic - the check that portals keep the lap count intact.

A synthetic kart drives the middle of the road from the start grid, `laps`
laps, one position every `step` units (about one tick at top speed). Per tick,
in the game's order and with its integer types:

  1. the ground under the kart decides lastValid
     (COLL_FIXED_PlayerSearch_CheckMaskGrabProgress, game/COLL.c): a jump of
     more than a quarter lap forward or back asks for a mask grab - which here
     is an error;
  2. the progress (VehLap_UpdateProgress, game/Vehicle/VehLap.c): restart
     point of lastValid -> its next point (progress point) -> the one after;
     distance = progress.dist * 8 + projection of (kart - progress point) on
     the normalised (progress point - next point), with s16 differences and
     the result modulo the lap (C remainder);
  3. the lap (PlayLevel_UpdateLapStats, game/PlayLevel.c): forward over the
     line when the distance goes from below 1200 to above 32000, the
     checkpoint distance only follows steps of at most a quarter lap.

At a portal the kart drives into the curtain, stands still for the mask grab
(1440 ms = 44 ticks at 30 Hz, VehStuckProc.c) while lastValid stays the last
ground quadblock, and is then put on that quadblock's restart point
(VehStuckProc_MaskGrab_FindDestPos) looking toward its next point.

Checked: every tick the distance moves by at most 512 near a portal (the
last 1024 units before the mouth, the grab, the first 1024 units after the
landing) and at most 1024 elsewhere, except at the finish line; exactly one lap per lap driven; no mask grab asked for by a progress
jump; the kart looks within 10 degrees of the road after every portal.
"""

import math
import struct

STEP_MAX = 512          # around a portal (the chief's criterion)
STEP_MAX_ROAD = 1024    # elsewhere: where a kart passes a restart point in a bend, the game's
                        # projection onto the next stretch makes the distance jump a little
PORTAL_ZONE = 1024.0    # before a portal mouth and after a landing
GRAB_TICKS = 44
NEAR = 1200
FAR = 32000


def _s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def _u32(v):
    return v & 0xFFFFFFFF


def _cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def _crem(a, b):
    return a - _cdiv(a, b) * b


def read_nodes(lev):
    n = struct.unpack_from('<I', lev, 0)[0]
    b = lev[4:4 + n]
    count, table = struct.unpack_from('<II', b, 0x148)
    nodes = []
    for i in range(count):
        x, y, z, dist, fwd, left, back, right = struct.unpack_from('<3hH4B', b, table + 12 * i)
        nodes.append(dict(pos=(x, y, z), dist=dist, fwd=fwd, left=left, back=back, right=right))
    mesh = struct.unpack_from('<I', b, 0)[0]
    nq = struct.unpack_from('<I', b, mesh)[0]
    pq = struct.unpack_from('<I', b, mesh + 0x0C)[0]
    cps = [b[pq + 0x5C * q + 0x3E] for q in range(nq)]
    return nodes, cps


def _normalize(v):
    """MATH_VectorNormalize: (v << 12) / |v| per component, |v| an integer root."""
    length = math.isqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length == 0:
        return (0, 0, 0)
    return tuple(_cdiv(c << 12, length) for c in v)


def progress(nodes, cp, pos):
    """distanceToFinish_curr as VehLap_UpdateProgress computes it (u32)."""
    pn = nodes[nodes[cp]['fwd']]
    nn = nodes[pn['fwd']]
    nd = _normalize(tuple(_s16(pn['pos'][i] - nn['pos'][i]) for i in range(3)))
    d = tuple(_s16(pos[i] - pn['pos'][i]) for i in range(3))
    proj = _s32(d[0] * nd[0] + d[1] * nd[1] + d[2] * nd[2])
    prog = _s32((pn['dist'] << 3) + (proj >> 12))
    length = nodes[0]['dist'] << 3
    return _u32(_crem(prog, length)) if length else _u32(prog)


class Kart:
    def __init__(self, nodes):
        self.nodes = nodes
        self.behind = True
        self.checkpoint = nodes[0]['dist'] << 3
        self.curr = 0
        self.last_valid = None
        self.laps = 0
        self.grabbed = False
        self.errors = []
        self.max_step = 0
        self.max_step_portal = 0
        self.ticks = 0

    def touch(self, quad, cp, where):
        """CheckMaskGrabProgress for a ground quadblock with restart point cp."""
        nodes = self.nodes
        if self.grabbed or cp == 0xFF:
            return
        node = nodes[cp]
        quarter = (nodes[0]['dist'] >> 2) << 3
        if (not self.behind) and node['fwd'] > 1 and quarter < _s32(self.checkpoint - node['dist'] * 8):
            self.errors.append('a progress jump asks for a mask grab at %s (restart point %d)' % (where, cp))
            return
        if self.last_valid is None:
            self.last_valid = (quad, cp)
            return
        length = nodes[0]['dist']
        lv = self.last_valid[1]
        if node['dist'] < ((length * 15) >> 4) and lv != 0xFF and nodes[lv]['dist'] + (length >> 2) < node['dist']:
            self.errors.append('a backward jump asks for a mask grab at %s (restart point %d)' % (where, cp))
            return
        self.last_valid = (quad, cp)

    def tick(self, pos, where, portal=False):
        self.ticks += 1
        if self.last_valid is None:
            return
        prev = _s32(self.curr)
        self.curr = progress(self.nodes, self.last_valid[1], pos)
        curr = _s32(self.curr)
        crossed = prev < NEAR and curr > FAR
        if not crossed and self.ticks > 1:
            step = abs(curr - prev)
            if portal:
                self.max_step_portal = max(self.max_step_portal, step)
            else:
                self.max_step = max(self.max_step, step)
            if step > (STEP_MAX if portal else STEP_MAX_ROAD):
                self.errors.append('the distance to the finish jumps by %d at %s (%d -> %d)' % (step, where, prev, curr))
        if crossed:
            if self.behind:
                self.behind = False
            else:
                self.checkpoint = self.curr
                self.laps += 1
        elif curr < NEAR and prev > FAR:
            self.behind = True
            self.errors.append('the kart crosses the finish line backward at %s' % where)
        else:
            track = self.nodes[0]['dist']
            if not self.behind and _u32(self.checkpoint - self.curr) <= ((track >> 2) << 3):
                self.checkpoint = self.curr


def _path_point(rows, closed, s):
    """Position and direction on the rows at arc length s."""
    n = len(rows)
    if closed:
        total = rows[-1]['s'] + math.hypot(rows[0]['x'] - rows[-1]['x'], rows[0]['z'] - rows[-1]['z'])
        s %= total
    for i in range(n - 1 if not closed else n):
        a, b = rows[i], rows[(i + 1) % n]
        sa = a['s']
        sb = b['s'] if i + 1 < n else total
        if sa <= s <= sb or (i == (n - 2 if not closed else n - 1)):
            f = (s - sa) / (sb - sa) if sb > sa else 0.0
            return (a['x'] + f * (b['x'] - a['x']), a['y'] + f * (b['y'] - a['y']), a['z'] + f * (b['z'] - a['z'])), i, f
    raise ValueError('path position %.1f outside the rows' % s)


def simulate(lev, built, sections, laps=2, step=96.0):
    nodes, cps = read_nodes(lev)
    quads = built['quads']
    seg_quad = {}
    for qi, q in enumerate(quads):
        if q['kind'] == 'ground':
            seg_quad.setdefault((q['sec'], q['seg']), qi)
    kart = Kart(nodes)
    nsec = len(sections)
    yaw_errors = []
    s0 = sections[0]['rows'][built['start_row']]['s']

    def ground(si, row_index, extension=False):
        sec = sections[si]
        n = len(sec['rows'])
        if extension:
            k = (n - 1) // 2 if not sec['closed'] else None
        elif sec['closed']:
            k = (row_index // 2) % (n // 2)
        else:
            k = min(row_index // 2, (n - 1) // 2 - 1)
        return seg_quad.get((si, k))

    def drive(si, s_from, s_to):
        sec = sections[si]
        rows = sec['rows']
        s = s_from
        while s < s_to - 1e-6:
            s = min(s + step, s_to)
            pos, i, f = _path_point(rows, sec['closed'], s)
            q = ground(si, i)
            where = 'section %d, %.0f units' % (si, s)
            if q is not None:
                kart.touch(q, cps[q], where)
            near = nsec > 1 and (s > sec['length'] - PORTAL_ZONE or s < PORTAL_ZONE)
            kart.tick(tuple(int(math.floor(v)) for v in pos), where, near)

    if nsec == 1:
        total = sections[0]['length']
        drive(0, s0 - 224.0, s0 - 224.0 + step)        # the grid
        drive(0, s0 - 224.0 + step, s0 + laps * total + 300.0)
        return _result(kart, yaw_errors, laps, 0)

    si = 0
    s = s0 - 224.0
    drive(0, s, s + step)
    s += step
    passes = 0
    while passes < laps * nsec:
        sec = sections[si]
        rows = sec['rows']
        drive(si, s, sec['length'])
        # past the mouth on the extension, into the curtain
        last = rows[-1]
        d = 0.0
        q = ground(si, 0, extension=True)
        while d < sec.get('curtain', 0.0) - 25.0:
            d = min(d + step, sec.get('curtain', 0.0) - 25.0)
            pos = (last['x'] + last['dx'] * d, last['y'], last['z'] + last['dz'] * d)
            where = 'section %d, %.0f past the mouth' % (si, d)
            if q is not None:
                kart.touch(q, cps[q], where)
            kart.tick(tuple(int(math.floor(v)) for v in pos), where, True)
        # the mask grab: lastValid stays, the kart waits
        kart.grabbed = True
        hold = tuple(int(math.floor(v)) for v in (last['x'] + last['dx'] * d, last['y'], last['z'] + last['dz'] * d))
        for _ in range(GRAB_TICKS):
            kart.tick(hold, 'section %d, mask grab' % si, True)
        kart.grabbed = False
        dest = nodes[kart.last_valid[1]]
        look = nodes[dest['fwd']]
        nxt = (si + 1) % nsec
        r0 = sections[nxt]['rows'][0]
        yaw = math.atan2(look['pos'][0] - dest['pos'][0], look['pos'][2] - dest['pos'][2])
        want = math.atan2(r0['dx'], r0['dz'])
        diff = abs((yaw - want + math.pi) % (2 * math.pi) - math.pi)
        if diff > math.radians(10.0):
            yaw_errors.append('after the portal of section %d the kart looks %.1f degrees off the road' % (si, math.degrees(diff)))
        if math.hypot(dest['pos'][0] - r0['x'], dest['pos'][2] - r0['z']) > 64:
            yaw_errors.append('the portal of section %d does not land on the start of section %d' % (si, nxt))
        # set down: lastValid = the ground under the restart point
        q0 = ground(nxt, 0)
        kart.last_valid = (q0, cps[q0])
        kart.tick(tuple(int(math.floor(v)) for v in dest['pos']), 'section %d, landing' % nxt, True)
        si = nxt
        s = 0.0
        passes += 1
    # on to the line once more
    drive(0, 0.0, s0 + 300.0)
    return _result(kart, yaw_errors, laps, passes)


def _result(kart, yaw_errors, laps, passes):
    errors = kart.errors + yaw_errors
    if kart.laps != laps:
        errors.append('%d laps counted for %d driven' % (kart.laps, laps))
    return dict(errors=errors, laps=kart.laps, laps_driven=laps, ticks=kart.ticks, max_step=kart.max_step,
                max_step_portal=kart.max_step_portal, portals_passed=passes)
