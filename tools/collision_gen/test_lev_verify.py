import math
import os
import struct
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import centreline  # noqa: E402
import collision_gen  # noqa: E402
import glb_io  # noqa: E402
import lev_build  # noqa: E402
import lev_normals  # noqa: E402
import lev_verify  # noqa: E402
import make_test_track as mtt  # noqa: E402

_CACHE = {}


def _route(track):
    return [dict(start=r['start'], dir=r['dir'], end=r['end'], portal=None, curtain=0.0, guide=None) for r in track['route']]


def _generate(kind, route=None, start=None):
    """collision_gen.generate on a test track; returns the result tuple."""
    track = mtt.build_track(kind)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, kind + '.glb')
        glb_io.write_glb(path, track['materials'], track['prims'])
        return collision_gen.generate(path, track['classes'], start or track['start'],
                                      route if route is not None else _route(track), log=lambda *a: None)


def _cached(kind):
    if kind not in _CACHE:
        t0 = time.time()
        _CACHE[kind] = _generate(kind) + (time.time() - t0,)
    return _CACHE[kind]


def _checks(errors):
    return {e.split(':')[0] for e in errors}


def _first(model, kind):
    for qi, q in enumerate(model.quads):
        if model.kind(q) == kind:
            return qi
    raise AssertionError('no %s quad' % kind)


class TrackVerifyTest(unittest.TestCase):
    def _clean(self, kind):
        lev, vrm, report, built, sections, surface = _cached(kind)[:6]
        v = report['verify']
        self.assertEqual(v['errors'], [], kind)
        h = v['height']
        # The criterion: at least 99 % of the raster points over the road hit the
        # collision within 8 units. The largest error is reported, not bounded.
        self.assertGreaterEqual(h['rate'], 0.99, kind)
        self.assertLessEqual(h['p99_error'], 8.0, kind)
        self.assertGreaterEqual(h['delta_p01'], -0.5, kind)
        p = report['progress']
        self.assertEqual(p['errors'], [], kind)
        self.assertEqual(p['laps'], p['laps_driven'], kind)
        return v

    def test_ring_a_clean(self):
        self._clean('ring_a')

    def test_ring_c_clean(self):
        v = self._clean('ring_c')
        self.assertGreater(v['curtain_quads'], 0)
        self.assertGreater(v['nav_jumps'], 0)

    def test_ring_b_clean(self):
        v = self._clean('ring_b')
        elapsed = _cached('ring_b')[6]
        self.assertLess(elapsed, 60.0, 'ring_b took %.1f s' % elapsed)
        self.assertGreater(v['ground_quads'], 0)

    def test_deterministic(self):
        for kind in ('ring_a', 'ring_c', 'ring_b'):
            lev, vrm = _cached(kind)[:2]
            lev2, vrm2 = _generate(kind)[:2]
            self.assertEqual(lev, lev2, kind)
            self.assertEqual(vrm, vrm2, kind)

    def test_short_is_refused(self):
        with self.assertRaises(lev_build.BuildError):
            _generate('short')

    def test_unreachable_end_is_refused(self):
        track = mtt.build_track('ring_a')
        route = _route(track)
        sec = route[0]
        sec['end'] = (sec['start'][0] + 90000.0, sec['start'][1], sec['start'][2] + 90000.0)
        with self.assertRaises(centreline.TraceError):
            _generate('ring_a', route=route)


class MutationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lev, _, _, _, _, cls.surface = _cached('ring_a')[:6]
        cls.model = lev_verify.LevModel(cls.lev)

    def test_unmodified_without_surface_is_clean(self):
        self.assertEqual(lev_verify.verify_lev(self.lev)['errors'], [])

    def _patched(self, edit):
        b = bytearray(self.lev)
        edit(b)
        return bytes(b)

    def test_ground_flag_cleared(self):
        qi = _first(self.model, 'ground')
        o = 4 + self.model.quads[qi]['off']

        def edit(b):
            f = struct.unpack_from('<H', b, o + 0x12)[0]
            struct.pack_into('<H', b, o + 0x12, f & ~lev_verify.FLAG_GROUND)

        v = lev_verify.verify_lev(self._patched(edit))
        self.assertTrue(v['errors'])
        self.assertTrue(_checks(v['errors']) & {'check 6', 'check 5', 'check 4'}, v['errors'])
        # with the surface the height test sees the hole as well
        v = lev_verify.verify_lev(self._patched(edit), surface=self.surface, hit_min=0.9999)
        self.assertIn('check 10', _checks(v['errors']))

    def test_old_divisor_rule(self):
        m = self.model
        found = False
        for qi, q in enumerate(m.quads):
            if m.kind(q) != 'ground':
                continue
            ps = [m.verts[i] for i in q['idx']]
            tris = lev_normals.quad_triangles()
            crosses = [lev_normals.game_cross(ps[a], ps[b], ps[c]) for (a, b, c), _, _ in tris]
            lengths = [math.sqrt(sum(v * v for v in c)) for c in crosses]
            for shift in range(24, -1, -1):
                divs = [int(round(4096 * (1 << shift) / ln)) for ln in lengths]
                if max(divs) <= 0x7FFF:
                    break
            o = 4 + q['off']

            def edit(b, shift=shift, divs=divs):
                b[o + 0x3F] = shift
                struct.pack_into('<10h', b, o + 0x48, *divs)

            v = lev_verify.verify_lev(self._patched(edit))
            if 'check 3' in _checks(v['errors']):
                found = True
                break
        if not found:
            def edit(b):
                b[o + 0x3F] = 0
                struct.pack_into('<h', b, o + 0x48, 0x7FFF)

            v = lev_verify.verify_lev(self._patched(edit))
            self.assertIn('check 3', _checks(v['errors']), v['errors'])
        else:
            self.assertIn('check 3', _checks(v['errors']))

    def test_huge_divisor_always_caught(self):
        qi = _first(self.model, 'ground')
        o = 4 + self.model.quads[qi]['off']

        def edit(b):
            b[o + 0x3F] = 0
            struct.pack_into('<h', b, o + 0x48, 0x7FFF)

        v = lev_verify.verify_lev(self._patched(edit))
        self.assertIn('check 3', _checks(v['errors']), v['errors'])

    def test_visible_wall(self):
        wall = _first(self.model, 'wall')
        ground = _first(self.model, 'ground')
        wo = 4 + self.model.quads[wall]['off']
        tex = self.model.quads[ground]['tex']

        def edit(b):
            for k in range(4):
                struct.pack_into('<I', b, wo + 0x1C + 4 * k, tex[k])
            struct.pack_into('<I', b, wo + 0x40, tex[4])

        v = lev_verify.verify_lev(self._patched(edit))
        self.assertIn('check 8', _checks(v['errors']), v['errors'])

    def test_unreadable_lev(self):
        v = lev_verify.verify_lev(self.lev[:100])
        self.assertTrue(v['errors'])

    def test_pointer_map_order(self):
        n = struct.unpack_from('<I', self.lev, 0)[0]
        mo = 4 + n + 4

        def edit(b):
            a, c = struct.unpack_from('<II', b, mo)
            struct.pack_into('<II', b, mo, c, a)

        v = lev_verify.verify_lev(self._patched(edit))
        self.assertIn('check 1', _checks(v['errors']))

    def test_floor_search_two_ways_agree(self):
        m = self.model
        for qi in range(0, m.nq, 11):
            q = m.quads[qi]
            if m.kind(q) != 'ground':
                continue
            x, y, z = m.verts[q['idx'][6]]
            a = m.ground_hit(x, z, y + 128, y - 256)
            b = m.ground_hit(x, z, y + 128, y - 256, use_bsp=True)
            self.assertIsNotNone(a)
            self.assertEqual(a, b)
            self.assertLess(abs(a[0] - y), 1e-6)


class LayerTest(unittest.TestCase):
    def test_conflict_when_close(self):
        self.assertEqual(lev_verify.conflicts_in([0.0, 100.0]), [(0.0, 100.0)])

    def test_no_conflict_when_far(self):
        self.assertEqual(lev_verify.conflicts_in([0.0, 800.0]), [])

    def test_shared_edge_is_no_conflict(self):
        self.assertEqual(lev_verify.conflicts_in([10.0, 10.4]), [])
        self.assertEqual(lev_verify.conflicts_in([10.0]), [])

    def test_track_has_none(self):
        lev = _cached('ring_b')[0]
        self.assertEqual(lev_verify.layer_conflicts(lev), [])


if __name__ == '__main__':
    unittest.main()
