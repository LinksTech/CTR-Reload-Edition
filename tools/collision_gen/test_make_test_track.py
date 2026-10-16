import math
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from glb_io import read_glb, write_glb  # noqa: E402
import make_test_track as mtt  # noqa: E402


def _tris(flat):
    for i in range(0, len(flat), 9):
        yield flat[i:i + 9]


def _height_at(tri, px, pz):
    """Interpolated height of a triangle at (px, pz) in XZ, or None if outside."""
    x0, y0, z0, x1, y1, z1, x2, y2, z2 = tri
    det = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
    if abs(det) < 1e-9:
        return None
    a = ((z1 - z2) * (px - x2) + (x2 - x1) * (pz - z2)) / det
    b = ((z2 - z0) * (px - x2) + (x0 - x2) * (pz - z2)) / det
    c = 1.0 - a - b
    if min(a, b, c) < -1e-9:
        return None
    return a * y0 + b * y1 + c * y2


class MakeTestTrackTest(unittest.TestCase):
    def test_deterministic_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            for kind in mtt.KINDS:
                blobs = []
                for n in range(2):
                    track = mtt.build_track(kind)
                    path = os.path.join(tmp, '%s_%d.glb' % (kind, n))
                    write_glb(path, track['materials'], track['prims'])
                    with open(path, 'rb') as f:
                        blobs.append(f.read())
                self.assertEqual(blobs[0], blobs[1], kind)

    def test_ring_a(self):
        info = mtt.build_track('ring_a')['info']
        self.assertTrue(36000 < info['lap'] < 37500)
        self.assertEqual(info['sections'], 1)

    def test_short(self):
        info = mtt.build_track('short')['info']
        self.assertLess(info['lap'], 32000)

    def test_ring_c_layout(self):
        track = mtt.build_track('ring_c')
        self.assertEqual(track['info']['sections'], 3)
        self.assertGreater(track['info']['lap'], 33200)
        route = track['route']
        self.assertEqual(len(route), 3)
        self.assertGreater(abs(route[0]['start'][0] - route[1]['start'][0]), 33000)
        for prim in track['prims']:
            pos = prim[1]
            self.assertLessEqual(max(abs(v) for v in pos[0::3]), 31000)
            self.assertLessEqual(max(abs(v) for v in pos[2::3]), 31000)

    def test_ring_c_start_on_first_straight(self):
        x, y, z, dx, dz = mtt.build_track('ring_c')['start']
        self.assertEqual((x, y, dx, dz), (-12000, 0, 1, 0))

    def test_ring_c_gap(self):
        track = mtt.build_track('ring_c')
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'c.glb')
            write_glb(path, track['materials'], track['prims'])
            tris = read_glb(path, want={'road_c'})['triangles']['road_c']
        # section 1 starts at (20000, 200, -2000) heading +x: straight of 4000,
        # then a left arc of radius 2000 around (24000, -4000)
        phi = (5150.0 - 4000.0) / 2000.0
        px = 24000.0 + 2000.0 * math.sin(phi)
        pz = -2000.0 - 2000.0 * (1.0 - math.cos(phi))
        hits = [t for t in _tris(tris) if _height_at(t, px, pz) is not None]
        self.assertEqual(hits, [])
        # sanity: the road before the gap is present
        phi = (4800.0 - 4000.0) / 2000.0
        px = 24000.0 + 2000.0 * math.sin(phi)
        pz = -2000.0 - 2000.0 * (1.0 - math.cos(phi))
        self.assertTrue(any(_height_at(t, px, pz) is not None for t in _tris(tris)))

    def test_ring_b_two_layers_over_crossing(self):
        track = mtt.build_track('ring_b')
        self.assertGreaterEqual(track['info']['min_radius'], 1200)
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'b.glb')
            write_glb(path, track['materials'], track['prims'])
            tris = read_glb(path, want={'road_b'})['triangles']['road_b']
        heights = sorted(h for h in (_height_at(t, 0.0, 0.0) for t in _tris(tris))
                         if h is not None)
        self.assertTrue(heights)
        self.assertLess(abs(heights[0] - 0.0), 15.0)
        self.assertLess(abs(heights[-1] - 800.0), 15.0)

    def test_file_formats(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = mtt.write_track(tmp, 'ring_c')
            with open(paths['classes']) as f:
                lines = f.read().splitlines()
            self.assertEqual(lines[0], 'material\tclass')
            names = [ln.split('\t')[0] for ln in lines[1:]]
            self.assertEqual(names, sorted(names))
            classes = dict(ln.split('\t') for ln in lines[1:])
            self.assertEqual(classes['road_c'], 'road')
            self.assertEqual(classes['wall_c'], 'wall')
            self.assertEqual(classes['scenery_c'], 'scenery')
            with open(paths['start']) as f:
                self.assertEqual(f.read(), '-12000 0 3000 1 0\n')
            with open(paths['route']) as f:
                rows = f.read().splitlines()
            self.assertEqual(rows[0], 'section\tstart_x\tstart_y\tstart_z\t'
                                      'dir_x\tdir_z\tend_x\tend_y\tend_z')
            self.assertEqual(len(rows), 4)
            self.assertEqual(rows[1].split('\t')[:7], ['0', '-14000', '0', '3000', '1', '0', '-14000'])
            self.assertEqual(rows[3].split('\t')[4:6], ['0', '-1'])
            self.assertTrue(os.path.exists(paths['glb']))

    def test_ring_a_start_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = mtt.write_track(tmp, 'ring_a')
            with open(paths['start']) as f:
                self.assertEqual(f.read(), '0 0 0 -1 0\n')


if __name__ == '__main__':
    unittest.main()
