import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import collision_gen  # noqa: E402
import make_test_track  # noqa: E402


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
