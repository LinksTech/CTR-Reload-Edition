import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import collision_gen  # noqa: E402
import glb_io  # noqa: E402
import lev_verify  # noqa: E402
import make_test_track  # noqa: E402


def _generate(kind, region):
    track = make_test_track.build_track(kind)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, kind + '.glb')
        glb_io.write_glb(path, track['materials'], track['prims'])
        route = [dict(start=r['start'], dir=r['dir'], end=r['end'], portal=None, curtain=0.0, guide=None) for r in track['route']]
        return collision_gen.generate(path, track['classes'], track['start'], route, log=lambda *a: None, region=region)


class UncoveredRoadTest(unittest.TestCase):
    """The measurement of road without collision ground (uncovered_points, the
    band of the measured widths) reads only: the LEV is the same bytes with and
    without it, and it finds a hole that is made on purpose."""

    @classmethod
    def setUpClass(cls):
        cls.plain = _generate('ring_c', None)
        cls.measured = _generate('ring_c', 'route')

    def test_measurement_keeps_the_bytes(self):
        self.assertEqual(self.plain[0], self.measured[0])
        self.assertEqual(self.plain[1], self.measured[1])
        self.assertNotIn('uncovered_road', self.plain[2])
        self.assertIsInstance(self.measured[2]['uncovered_road']['points'], int)

    def test_a_missing_ground_quad_is_found(self):
        lev, surface = self.measured[0], self.measured[5]
        model = lev_verify.LevModel(lev)
        before = len(lev_verify.uncovered_points(model, surface))
        qi = next(i for i, q in enumerate(model.quads) if model.kind(q) == 'ground' and not model.is_invisible(q))
        b = bytearray(lev)
        o = 4 + model.quads[qi]['off'] + 0x12
        b[o:o + 2] = (model.quads[qi]['flags'] & ~lev_verify.FLAG_GROUND).to_bytes(2, 'little')
        after = len(lev_verify.uncovered_points(bytes(b), surface))
        self.assertGreater(after, before)


class WarnSwitchTest(unittest.TestCase):
    REPORT = dict(verify=dict(errors=['check 3: a normal', 'check 10: heights', 'check 11: two ground layers']),
                  progress=dict(errors=['a lap']))

    def test_default_stops_on_everything(self):
        self.assertEqual(len(collision_gen.failures(self.REPORT)), 4)

    def test_height_warn_only_softens_check_10(self):
        out = collision_gen.failures(self.REPORT, height_warn=True)
        self.assertNotIn('check 10: heights', out)
        self.assertIn('check 11: two ground layers', out)
        self.assertIn('check 3: a normal', out)
        self.assertIn('a lap', out)

    def test_conflict_warn_only_softens_check_11(self):
        out = collision_gen.failures(self.REPORT, conflict_warn=True)
        self.assertNotIn('check 11: two ground layers', out)
        self.assertIn('check 10: heights', out)
        self.assertIn('check 3: a normal', out)
        self.assertIn('a lap', out)

    def test_switches_do_not_change_the_bytes(self):
        """The warn switches decide only whether a failed check stops the
        writing; the LEV and VRM written are the same bytes."""
        with tempfile.TemporaryDirectory() as tmp:
            paths = make_test_track.write_track(os.path.join(tmp, 'model'), 'ring_c')
            outs = []
            for extra in ([], ['--height-warn', '--conflict-warn']):
                out = os.path.join(tmp, 'out%d' % len(outs))
                args = ['--glb', paths['glb'], '--classes', paths['classes'], '--start', paths['start'],
                        '--route', paths['route'], '--out', out, '--name', 'collgen'] + extra
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(collision_gen.main(args), 0)
                with open(os.path.join(out, 'collgen.lev'), 'rb') as f:
                    lev = f.read()
                with open(os.path.join(out, 'collgen.vrm'), 'rb') as f:
                    vrm = f.read()
                outs.append((lev, vrm))
            self.assertEqual(outs[0], outs[1])


if __name__ == '__main__':
    unittest.main()
