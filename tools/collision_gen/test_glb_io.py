import json
import math
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from glb_io import read_glb, write_glb  # noqa: E402


def _hand_glb(doc, bin_data):
    js = json.dumps(doc).encode('utf-8')
    js += b' ' * (-len(js) % 4)
    bin_data = bin_data + b'\0' * (-len(bin_data) % 4)
    body = (struct.pack('<II', len(js), 0x4E4F534A) + js +
            struct.pack('<II', len(bin_data), 0x004E4942) + bin_data)
    return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body


TRI = (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)


def _simple_doc(nodes, scene_nodes, sparse=False):
    acc = {'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3'}
    if sparse:
        acc['sparse'] = {'count': 1}
    return {'asset': {'version': '2.0'},
            'scene': 0, 'scenes': [{'nodes': scene_nodes}],
            'nodes': nodes,
            'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'material': 0}]}],
            'materials': [{'name': 'mat'}],
            'accessors': [acc],
            'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': 36}],
            'buffers': [{'byteLength': 36}]}


class GlbIoTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = self._tmp.name

    def path(self, name):
        return os.path.join(self.dir, name)

    def test_round_trip(self):
        pos = [0, 0, 0, 10.5, 0, 0, 0, 2.25, 7, 3, 3, 3]
        write_glb(self.path('a.glb'), ['one', 'two'],
                  [(0, pos, [0, 1, 2]), (1, pos, [1, 2, 3, 0, 1, 3])])
        res = read_glb(self.path('a.glb'))
        self.assertEqual(res['materials'], ['one', 'two'])
        self.assertEqual(len(res['triangles']['one']), 9)
        self.assertEqual(len(res['triangles']['two']), 18)
        expect = [0, 0, 0, 10.5, 0, 0, 0, 2.25, 7]
        for got, exp in zip(res['triangles']['one'], expect):
            self.assertAlmostEqual(got, exp, places=5)
        self.assertEqual(res['skipped'], {'modes': 0})

    def test_deterministic_bytes(self):
        pos = [0.1, 0.2, 0.3, 1, 2, 3, 4, 5, 6]
        write_glb(self.path('a.glb'), ['m'], [(0, pos, [0, 1, 2])])
        write_glb(self.path('b.glb'), ['m'], [(0, pos, [0, 1, 2])])
        with open(self.path('a.glb'), 'rb') as fa, open(self.path('b.glb'), 'rb') as fb:
            data = fa.read()
            self.assertEqual(data, fb.read())
        self.assertEqual(len(data) % 4, 0)

    def test_node_transform(self):
        doc = _simple_doc(
            [{'translation': [100, 0, 0], 'children': [1]},
             {'rotation': [0, math.sin(math.pi / 4), 0, math.cos(math.pi / 4)],
              'scale': [2, 2, 2], 'mesh': 0}], [0])
        raw = struct.pack('<9f', *TRI)
        with open(self.path('t.glb'), 'wb') as f:
            f.write(_hand_glb(doc, raw))
        tri = read_glb(self.path('t.glb'))['triangles']['mat']
        # rotation of +90 degrees about Y maps (x, y, z) -> (z, y, -x)
        expect = [100, 0, 0, 100, 0, -2, 102, 0, 0]
        self.assertEqual(len(tri), 9)
        for got, exp in zip(tri, expect):
            self.assertAlmostEqual(got, exp, places=4)

    def test_matrix_node(self):
        doc = _simple_doc([{'matrix': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1],
                            'mesh': 0}], [0])
        with open(self.path('m.glb'), 'wb') as f:
            f.write(_hand_glb(doc, struct.pack('<9f', *TRI)))
        tri = read_glb(self.path('m.glb'))['triangles']['mat']
        self.assertEqual(list(tri[:3]), [5.0, 6.0, 7.0])

    def test_want_filter(self):
        pos = [0, 0, 0, 1, 0, 0, 0, 0, 1]
        write_glb(self.path('w.glb'), ['keep', 'drop'],
                  [(0, pos, [0, 1, 2]), (1, pos, [0, 1, 2])])
        res = read_glb(self.path('w.glb'), want={'keep'})
        self.assertEqual(list(res['triangles']), ['keep'])
        self.assertEqual(res['materials'], ['keep', 'drop'])

    def test_bad_magic(self):
        with open(self.path('bad.glb'), 'wb') as f:
            f.write(b'XXXX' + bytes(40))
        with self.assertRaises(ValueError):
            read_glb(self.path('bad.glb'))

    def test_bad_version(self):
        with open(self.path('v1.glb'), 'wb') as f:
            f.write(struct.pack('<4sII', b'glTF', 1, 12))
        with self.assertRaises(ValueError):
            read_glb(self.path('v1.glb'))

    def test_sparse_accessor_rejected(self):
        doc = _simple_doc([{'mesh': 0}], [0], sparse=True)
        with open(self.path('s.glb'), 'wb') as f:
            f.write(_hand_glb(doc, struct.pack('<9f', *TRI)))
        with self.assertRaises(ValueError):
            read_glb(self.path('s.glb'))

    def test_compression_rejected(self):
        doc = _simple_doc([{'mesh': 0}], [0])
        doc['extensionsRequired'] = ['KHR_draco_mesh_compression']
        with open(self.path('d.glb'), 'wb') as f:
            f.write(_hand_glb(doc, struct.pack('<9f', *TRI)))
        with self.assertRaises(ValueError):
            read_glb(self.path('d.glb'))


if __name__ == '__main__':
    unittest.main()
