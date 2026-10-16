"""Minimal glTF 2.0 reader/writer (standard library only).

read_glb returns world-space triangles grouped by material name.
write_glb writes a small deterministic .glb used for generated test tracks.
"""

import array
import base64
import json
import math
import os
import struct
import sys
import urllib.parse
from operator import itemgetter

_IDENTITY = (1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             0.0, 0.0, 1.0, 0.0,
             0.0, 0.0, 0.0, 1.0)

_CHUNK_JSON = 0x4E4F534A
_CHUNK_BIN = 0x004E4942
_BIG = sys.byteorder == 'big'

_INDEX_TYPES = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4)}
_REJECTED_EXTENSIONS = ('KHR_draco_mesh_compression', 'EXT_meshopt_compression')


def _mat_mul(a, b):
    """Row-major 4x4 product a * b."""
    out = [0.0] * 16
    for r in range(4):
        for c in range(4):
            out[r * 4 + c] = (a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] +
                              a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c])
    return tuple(out)


def _node_matrix(node):
    """Local node matrix (row-major, applied to column vectors)."""
    if 'matrix' in node:
        m = node['matrix']
        if len(m) != 16:
            raise ValueError('node matrix must have 16 elements')
        # glTF stores column-major.
        return tuple(float(m[c * 4 + r]) for r in range(4) for c in range(4))
    t = node.get('translation', (0.0, 0.0, 0.0))
    q = node.get('rotation', (0.0, 0.0, 0.0, 1.0))
    s = node.get('scale', (1.0, 1.0, 1.0))
    qx, qy, qz, qw = (float(v) for v in q)
    n = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    if n == 0.0:
        raise ValueError('zero-length rotation quaternion')
    qx, qy, qz, qw = qx / n, qy / n, qz / n, qw / n
    r = (1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw),
         2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw),
         2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy))
    sx, sy, sz = (float(v) for v in s)
    return (r[0] * sx, r[1] * sy, r[2] * sz, float(t[0]),
            r[3] * sx, r[4] * sy, r[5] * sz, float(t[1]),
            r[6] * sx, r[7] * sy, r[8] * sz, float(t[2]),
            0.0, 0.0, 0.0, 1.0)


def _load_container(path):
    with open(path, 'rb') as f:
        data = f.read()
    base_dir = os.path.dirname(os.path.abspath(path))
    if data[:4] == b'glTF':
        if len(data) < 12:
            raise ValueError('truncated glb header')
        version = struct.unpack_from('<I', data, 4)[0]
        if version != 2:
            raise ValueError('unsupported glb version %d (need 2)' % version)
        view = memoryview(data)
        pos = 12
        js = None
        bin_chunk = None
        while pos + 8 <= len(data):
            clen, ctype = struct.unpack_from('<II', data, pos)
            body = view[pos + 8:pos + 8 + clen]
            if ctype == _CHUNK_JSON and js is None:
                try:
                    js = json.loads(bytes(body).decode('utf-8'))
                except (UnicodeDecodeError, ValueError) as exc:
                    raise ValueError('invalid JSON chunk: %s' % exc)
            elif ctype == _CHUNK_BIN and bin_chunk is None:
                bin_chunk = body
            pos += 8 + ((clen + 3) & ~3)
        if js is None:
            raise ValueError('glb has no JSON chunk')
        return js, bin_chunk, base_dir
    try:
        js = json.loads(data.decode('utf-8-sig'))
    except (UnicodeDecodeError, ValueError):
        raise ValueError('not a glb/gltf file (bad magic)')
    if not isinstance(js, dict):
        raise ValueError('not a glb/gltf file (bad magic)')
    return js, None, base_dir


def _check_asset(js):
    asset = js.get('asset', {})
    if str(asset.get('version', '2.0')).split('.')[0] != '2':
        raise ValueError('unsupported glTF version %s' % asset.get('version'))
    for ext in js.get('extensionsRequired', ()):
        if ext in _REJECTED_EXTENSIONS:
            raise ValueError('required extension %s is not supported' % ext)


class _Buffers:
    def __init__(self, js, bin_chunk, base_dir):
        self._js = js
        self._bin = bin_chunk
        self._dir = base_dir
        self._cache = {}

    def get(self, index):
        if index in self._cache:
            return self._cache[index]
        buffers = self._js.get('buffers', [])
        if index < 0 or index >= len(buffers):
            raise ValueError('buffer index %d out of range' % index)
        uri = buffers[index].get('uri')
        if uri is None:
            if index != 0 or self._bin is None:
                raise ValueError('buffer %d has no data' % index)
            mv = self._bin
        elif uri.startswith('data:'):
            comma = uri.find(',')
            if comma < 0 or ';base64' not in uri[:comma]:
                raise ValueError('unsupported data URI')
            mv = memoryview(base64.b64decode(uri[comma + 1:]))
        else:
            with open(os.path.join(self._dir, urllib.parse.unquote(uri)), 'rb') as f:
                mv = memoryview(f.read())
        self._cache[index] = mv
        return mv


def _accessor_slice(js, buffers, acc_index, want_type, elem_size):
    accessors = js.get('accessors', [])
    if acc_index < 0 or acc_index >= len(accessors):
        raise ValueError('accessor index %d out of range' % acc_index)
    acc = accessors[acc_index]
    if 'sparse' in acc:
        raise ValueError('sparse accessors are not supported')
    if acc.get('type') != want_type:
        raise ValueError('accessor %d: expected type %s' % (acc_index, want_type))
    if 'bufferView' not in acc:
        raise ValueError('accessor %d has no bufferView' % acc_index)
    views = js.get('bufferViews', [])
    bv = views[acc['bufferView']]
    buf = buffers.get(bv['buffer'])
    offset = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    count = acc['count']
    stride = bv.get('byteStride', 0) or elem_size
    if count > 0 and offset + stride * (count - 1) + elem_size > len(buf):
        raise ValueError('accessor %d exceeds its buffer' % acc_index)
    return acc, buf, offset, count, stride


def _read_positions(js, buffers, acc_index):
    acc, buf, offset, count, stride = _accessor_slice(js, buffers, acc_index, 'VEC3', 12)
    if acc.get('componentType') != 5126:
        raise ValueError('POSITION must be float (componentType 5126)')
    out = array.array('f')
    if stride == 12:
        out.frombytes(buf[offset:offset + 12 * count])
        if _BIG:
            out.byteswap()
    else:
        unpack = struct.Struct('<3f').unpack_from
        for i in range(count):
            out.extend(unpack(buf, offset + i * stride))
    return out


def _read_indices(js, buffers, acc_index):
    accessors = js.get('accessors', [])
    if acc_index < 0 or acc_index >= len(accessors):
        raise ValueError('accessor index %d out of range' % acc_index)
    ctype = accessors[acc_index].get('componentType')
    if ctype not in _INDEX_TYPES:
        raise ValueError('unsupported index componentType %s' % ctype)
    code, size = _INDEX_TYPES[ctype]
    acc, buf, offset, count, stride = _accessor_slice(js, buffers, acc_index, 'SCALAR', size)
    if stride != size:
        raise ValueError('strided index buffers are not supported')
    out = array.array(code)
    if out.itemsize != size:
        raise ValueError('unsupported platform integer size')
    out.frombytes(buf[offset:offset + size * count])
    if _BIG:
        out.byteswap()
    return out


def _world_triangles(pos, idx, matrix):
    """Return array('d') with 9 values per triangle in world space."""
    vcount = len(pos) // 3
    if matrix == _IDENTITY:
        verts = pos
    else:
        m = matrix
        verts = array.array('d', bytes(8 * len(pos)))
        for i in range(0, len(pos), 3):
            x, y, z = pos[i], pos[i + 1], pos[i + 2]
            verts[i] = m[0] * x + m[1] * y + m[2] * z + m[3]
            verts[i + 1] = m[4] * x + m[5] * y + m[6] * z + m[7]
            verts[i + 2] = m[8] * x + m[9] * y + m[10] * z + m[11]
    if idx is None:
        return array.array('d', verts[:vcount // 3 * 9])
    n = len(idx) // 3 * 3
    if n and max(idx[:n]) >= vcount:
        raise ValueError('index out of range of POSITION accessor')
    out = array.array('d')
    step = 30000
    for a in range(0, n, step):
        ix = [k for i in idx[a:min(a + step, n)] for k in (3 * i, 3 * i + 1, 3 * i + 2)]
        out.extend(itemgetter(*ix)(verts))
    return out


def read_glb(path, want=None):
    """Read triangles in world coordinates, grouped by material name."""
    js, bin_chunk, base_dir = _load_container(path)
    _check_asset(js)
    buffers = _Buffers(js, bin_chunk, base_dir)
    materials = []
    for i, mat in enumerate(js.get('materials', [])):
        materials.append(mat.get('name') or ('material_%d' % i))
    nodes = js.get('nodes', [])
    scenes = js.get('scenes', [])
    if scenes:
        scene_index = js.get('scene', 0)
        roots = list(scenes[scene_index].get('nodes', []))
    else:
        children = set()
        for node in nodes:
            children.update(node.get('children', ()))
        roots = [i for i in range(len(nodes)) if i not in children]
    result = {'materials': materials, 'triangles': {}, 'skipped': {'modes': 0}}
    triangles = result['triangles']
    decoded = {}

    stack = [(r, _IDENTITY, 0) for r in reversed(roots)]
    while stack:
        index, parent, depth = stack.pop()
        if depth > 1000:
            raise ValueError('node hierarchy too deep or cyclic')
        node = nodes[index]
        world = _mat_mul(parent, _node_matrix(node))
        if 'mesh' in node:
            mesh_index = node['mesh']
            for pi, prim in enumerate(js['meshes'][mesh_index].get('primitives', [])):
                if prim.get('mode', 4) != 4:
                    result['skipped']['modes'] += 1
                    continue
                if 'material' in prim:
                    mi = prim['material']
                    name = materials[mi] if 0 <= mi < len(materials) else 'material_%d' % mi
                else:
                    name = ''
                if want is not None and name not in want:
                    continue
                key = (mesh_index, pi)
                if key not in decoded:
                    attrs = prim.get('attributes', {})
                    if 'POSITION' not in attrs:
                        raise ValueError('primitive without POSITION')
                    pos = _read_positions(js, buffers, attrs['POSITION'])
                    idx = _read_indices(js, buffers, prim['indices']) if 'indices' in prim else None
                    decoded[key] = (pos, idx)
                pos, idx = decoded[key]
                tris = _world_triangles(pos, idx, world)
                triangles.setdefault(name, array.array('d')).extend(tris)
        for child in reversed(node.get('children', ())):
            stack.append((child, world, depth + 1))
    return result


def write_glb(path, materials, prims):
    """Write a minimal deterministic glTF 2.0 binary file.

    prims: list of (material_index, positions, indices); positions is a flat
    sequence of floats (x, y, z per vertex), indices a sequence of ints.
    """
    bin_data = bytearray()
    views = []
    accessors = []
    meshes = []
    nodes = []
    for mat_index, positions, indices in prims:
        if not 0 <= mat_index < len(materials):
            raise ValueError('material index out of range')
        if len(positions) == 0 or len(positions) % 3 or len(indices) % 3:
            raise ValueError('bad primitive data')
        pa = array.array('f', positions)
        mins = [min(pa[k::3]) for k in range(3)]
        maxs = [max(pa[k::3]) for k in range(3)]
        if _BIG:
            pa.byteswap()
        raw_pos = pa.tobytes()
        raw_idx = struct.pack('<%dI' % len(indices), *indices)

        views.append({'buffer': 0, 'byteOffset': len(bin_data),
                      'byteLength': len(raw_pos), 'target': 34962})
        bin_data += raw_pos
        accessors.append({'bufferView': len(views) - 1, 'componentType': 5126,
                          'count': len(raw_pos) // 12, 'type': 'VEC3',
                          'min': mins, 'max': maxs})
        pos_acc = len(accessors) - 1

        views.append({'buffer': 0, 'byteOffset': len(bin_data),
                      'byteLength': len(raw_idx), 'target': 34963})
        bin_data += raw_idx
        accessors.append({'bufferView': len(views) - 1, 'componentType': 5125,
                          'count': len(indices), 'type': 'SCALAR'})
        idx_acc = len(accessors) - 1

        meshes.append({'primitives': [{'attributes': {'POSITION': pos_acc},
                                       'indices': idx_acc, 'mode': 4,
                                       'material': mat_index}]})
        nodes.append({'mesh': len(meshes) - 1})

    doc = {'asset': {'version': '2.0', 'generator': 'collision_gen test track'},
           'scene': 0,
           'scenes': [{'nodes': list(range(len(nodes)))}],
           'materials': [{'name': m} for m in materials]}
    if prims:
        doc['nodes'] = nodes
        doc['meshes'] = meshes
        doc['accessors'] = accessors
        doc['bufferViews'] = views
        doc['buffers'] = [{'byteLength': len(bin_data)}]
    json_bytes = json.dumps(doc, sort_keys=True, separators=(',', ':')).encode('utf-8')
    json_bytes += b' ' * (-len(json_bytes) % 4)
    chunks = struct.pack('<II', len(json_bytes), _CHUNK_JSON) + json_bytes
    if prims:
        bin_bytes = bytes(bin_data) + b'\0' * (-len(bin_data) % 4)
        chunks += struct.pack('<II', len(bin_bytes), _CHUNK_BIN) + bin_bytes
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, 12 + len(chunks)))
        f.write(chunks)
