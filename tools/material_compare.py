#!/usr/bin/env python3
"""material_compare.py - compares the materials of the engine path with the
retail (PS1) path in an oracle log. The retail path only says WHICH texture
page, palette (CLUT), UV window, blend and corner colours belong on a face;
pixels are not compared.

    python tools/material_compare.py LOG [LOG ...] [--out PREFIX]
           [--exceptions FILE] [--expect SPEC ...] [--col-tol-gte N] [--lenient]
    python tools/material_compare.py --selftest

Log format (one entry per line, fields separated by tabs; '#' lines and
empty lines are skipped):
    H  exe-commit  switches...
    F  tag  levelId  container(0|1)  raceFrame  view  [extra]
    R  src  srcId  sub  tpX,tpY,depth  clutX,clutY  u0,v0,u1,v1,u2,v2[,u3,v3]  blend  cell  extra
    E  (same fields as R)
R = what the retail path draws for a source in this frame, E = what the
engine reads for the same source. src: T track face, I instance (srcId
model#index), D driver, X effect, S sky, W water, M menu scene. Untextured:
page, CLUT and UV are '-'. blend: o opaque, 0..3 semi-transparency mode
('-' counts as o). cell: mosaic cell row,col or '-'. extra: key=value pairs
separated by ',', ';' or spaces; read here: col (corner colours r,g,b;...),
box (engine sample box umin,vmin,umax,vmax), lod (model header), stage
(track texture stage), anim, prim (retail draw path), route=retail,
hires, fault (planted fault: clut|cell). F extra: vram=<dump file, relative
to the log>. A tag ending in '-load' is the static load block.

Rules (tolerance 0 on page, depth, CLUT, UV texels, blend, cell, stage,
header; colour see COL_TOL_*):
  Models (I D X M S W), per frame and source: every retail triangle must be
    an engine triangle with the same page, depth, CLUT, blend, UV corners and
    corner colours. Triangles are compared rotated so the smallest (u,v)
    comes first, winding kept; a quad is two triangles of either diagonal.
    A miss is classified by the features in which the nearest engine
    triangle differs. Retail lines with a header (lod) the engine does not
    draw are counted as class lod, not compared. prim other than normal
    (a retail draw path the engine does not rebuild) and anim mark their
    misses (features prim / anim in front). A source the engine does not
    draw at all: missing-source (route=retail: listed, no deviation).
  Track (T), per frame and face (srcId): a retail line with stage or cell
    must equal the engine entry of the same face with the same stage or
    cell. Found under another stage or cell of the face: feature stage or
    cell. In the load block every engine stage and cell needs a retail
    line (else engine-only); in race frames engine entries without a
    retail line are coverage gaps (counted).
  Direction 2 (models, per track and model name): an engine material that
    retail never draws for that model is engine-only-material (lod when
    retail draws the model only with another header). Engine triangles no
    frame uses are counted as unused per model.
  Sampling outside the window: for every engine triangle that draws a
    retail face, the texels of its sample box (box=) outside its UV box are
    read from the frame's VRAM dump: ring0 = word 0x0000 where the nearest
    texel inside is not (a hole), ringf = another colour than that texel.
    ring0 + ringf > 0 is the deviation sample-outside.
  Exceptions (material_compare_exceptions.tsv): a deviation is covered when
    a row matches its source, all its features and its condition. Covered
    deviations are counted per row, never dropped.
  Controls: engine lines with extra fault=<kind> are planted faults. A kind
    has fired when deviations outside the exception list point at its lines
    with the target feature (clut -> clut, cell -> cell) and every faulted
    material on a face retail draws is reported.
  --expect fault:<kind> requires that control; --expect TRACK:SRC:MODEL:FEATURE
    (e.g. L3:I:startbanner:sample-outside) requires a known positive.
  Plausibility: an H line; per track both sides present for every source
    kind it has (T and I always, M instead on the menu scene; route=retail
    sources need no engine side); a readable VRAM dump for every race frame.

Outputs: Markdown summary (stdout, or PREFIX.md), all records in PREFIX.tsv,
candidate classes in PREFIX-classes.tsv.

Exit code: 0 no deviation outside the exception list and the controls, all
controls and expectations fired, plausible; 1 deviations remain; 2 usage,
log or exception file error; 3 a control or expectation did not fire, or
the log is not plausible.
"""
import argparse
import array
import fnmatch
import os
import re
import sys

SOURCES = 'TIDXSWM'
FEATURES = ('prim', 'anim', 'lod', 'page', 'depth', 'clut', 'uv', 'blend', 'col', 'stage', 'cell',
            'missing-source', 'engine-only-material', 'engine-only', 'sample-outside')
BLENDS = ('o', '0', '1', '2', '3')
FAULT_TARGET = {'clut': 'clut', 'cell': 'cell'}
# Colour tolerance per channel (8-bit corner colours). The retail writer
# 'normal' copies the colours of the model or track unchanged into the
# primitive, so the engine (same table, same live frame) must match exactly.
# The other retail writers compute the colour in the GTE in fixed point
# (light: normal x matrix >> 12, then colour x IR >> 12 with saturation;
# depth fade: interpolation >> 12); a float rebuild of these two rounding
# steps can be off by one each, so 2. T1 (normals drawn as colours) is far
# above (>= 32).
COL_TOL_NORMAL = 0
COL_TOL_GTE = 2
BOX_AREA_MAX = 256 * 256
DEFAULT_EXCEPTIONS = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  'material_compare_exceptions.tsv')
MD_ROW_LIMIT = 200


class LogError(Exception):
    pass


# ---------------------------------------------------------------- log parsing

def _int(s):
    s = s.strip()
    if s.lower().startswith(('0x', '-0x')):
        return int(s, 16)
    return int(s, 10)


def _dash(s):
    return s.strip() == '' or set(s.strip()) <= set('-,')


def _ints(s, counts, what):
    try:
        v = [_int(x) for x in s.split(',')]
    except ValueError:
        raise LogError('bad %s %r' % (what, s))
    if len(v) not in counts:
        raise LogError('bad %s %r (want %s numbers)' % (what, s, ' or '.join(map(str, counts))))
    return v


_KEY_TOKEN = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)(?:=(.*))?$')
_VALUE_TOKEN = re.compile(r'^(-?[0-9][0-9A-Fa-fx]*|[0-9A-Fa-f]{6})$')


def parse_extra(s):
    """key=value pairs separated by ',', ';' or spaces. A value may itself
    hold separators (col=1,2,3;4,5,6;7,8,9, box=0,0,3,3): a token that is a
    number (or six hex digits) and has no '=' continues the previous value.
    A key without '=' counts as key=1."""
    out = {}
    if _dash(s):
        return out
    parts = re.split(r'([,; ]+)', s.strip())
    key = None
    for i in range(0, len(parts), 2):
        tok = parts[i]
        if not tok:
            continue
        if key is not None and '=' not in tok and _VALUE_TOKEN.match(tok):
            out[key] += parts[i - 1] + tok
            continue
        m = _KEY_TOKEN.match(tok)
        if not m:
            raise LogError('bad extra token %r' % tok)
        key = m.group(1)
        out[key] = m.group(2) if m.group(2) is not None else '1'
    return out


def parse_col(s):
    """col=r,g,b;r,g,b;... (decimal 0..255) or rrggbb;rrggbb;... -> tuple of (r, g, b)."""
    if s is None:
        return None
    out = []
    for c in s.split(';'):
        c = c.strip()
        if re.match(r'^[0-9A-Fa-f]{6}$', c):
            out.append((int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16)))
            continue
        v = _ints(c, (3,), 'col corner')
        if not all(0 <= x <= 255 for x in v):
            raise LogError('col corner out of 0..255 %r' % c)
        out.append(tuple(v))
    return tuple(out)


def parse_box(s):
    if s is None:
        return None
    v = _ints(s, (4,), 'box')
    if v[0] > v[2] or v[1] > v[3]:
        raise LogError('box min above max %r' % s)
    return tuple(v)


class Line(object):
    __slots__ = ('side', 'src', 'srcId', 'sub', 'page', 'depth', 'clut', 'uv', 'blend', 'cell',
                 'extra', 'lineno', 'fault', 'col', 'box', 'lod', 'stage', 'anim', 'prim', 'route',
                 'key', 'ncorner')

    def mat(self):
        return (self.page, self.depth, self.clut, self.blend)

    def tuple_key(self):
        return (self.mat(), self.uv, self.col, self.box, self.lod, self.key, self.anim, self.prim)


def parse_entry(fields, lineno):
    if len(fields) == 9:
        fields = fields + ['-']
    if len(fields) != 10:
        raise LogError('%s line has %d fields (want 10)' % (fields[0], len(fields)))
    ln = Line()
    ln.side, ln.src, ln.srcId, ln.sub = fields[0], fields[1].strip(), fields[2].strip(), fields[3].strip()
    if len(ln.src) != 1 or ln.src not in SOURCES:
        raise LogError('unknown src %r' % ln.src)
    if ln.srcId == '':
        raise LogError('empty srcId')
    if _dash(fields[4]):
        ln.page, ln.depth = None, None
    else:
        x, y, d = _ints(fields[4], (3,), 'page')
        if d not in (0, 1, 2):
            raise LogError('bad depth %d' % d)
        ln.page, ln.depth = (x, y), d
    ln.clut = None if _dash(fields[5]) else tuple(_ints(fields[5], (2,), 'clut'))
    if _dash(fields[6]):
        ln.uv = None
    else:
        v = _ints(fields[6], (6, 8), 'uv')
        ln.uv = tuple((v[i], v[i + 1]) for i in range(0, len(v), 2))
    b = fields[7].strip()
    ln.blend = 'o' if b in ('-', '') else b
    if ln.blend not in BLENDS:
        raise LogError('bad blend %r' % b)
    if ln.page is not None and ln.depth in (0, 1) and ln.clut is None:
        raise LogError('4/8-bit page without CLUT')
    if ln.page is not None and ln.uv is None and ln.side == 'R':
        raise LogError('textured retail entry without UV')
    ln.extra = parse_extra(fields[9])
    cell = fields[8].strip()
    if _dash(cell) and 'cell' in ln.extra:
        cell = ln.extra['cell']
    ln.cell = '-' if _dash(cell) else cell
    ln.fault = ln.extra.get('fault') if ln.extra.get('fault', '0') not in ('0', '') else None
    ln.anim = ln.extra.get('anim', '0') not in ('0', '')
    ln.col = parse_col(ln.extra.get('col'))
    ln.box = parse_box(ln.extra.get('box'))
    ln.lod = ln.extra.get('lod')
    ln.stage = ln.extra.get('stage')
    ln.prim = ln.extra.get('prim')
    ln.route = ln.extra.get('route')
    ln.ncorner = len(ln.uv) if ln.uv is not None else (len(ln.col) if ln.col and len(ln.col) > 1 else 3)
    if ln.col is not None and len(ln.col) not in (1, ln.ncorner):
        raise LogError('col has %d corners, the face %d' % (len(ln.col), ln.ncorner))
    if ln.src == 'T':
        ln.key = ('cell', ln.cell) if ln.cell != '-' else (('stage', ln.stage) if ln.stage is not None else ('-', '-'))
    else:
        ln.key = None
    ln.lineno = lineno
    return ln


class Frame(object):
    def __init__(self, tag, level, container, race_frame, view, lineno, origin, extra=None):
        self.tag, self.level, self.container = tag, level, container
        self.race_frame, self.view, self.lineno, self.origin = race_frame, view, lineno, origin
        self.extra = extra or {}
        self.vram = self.extra.get('vram')   # VRAM dump of this frame (path relative to the log)
        self.base_dir = ''
        self.load = tag.endswith('-load')
        self.sources = {}   # (src, srcId) -> {'R': [...], 'E': [...]}

    def track(self):
        return 'L%s%s' % (self.level, 'c' if self.container not in ('0', '') else '')


class Log(object):
    def __init__(self):
        self.headers, self.frames, self.errors = [], [], []


def parse_log(text, origin='log', log=None):
    """Adds the frames of one log text to log (a new Log when None)."""
    log = log or Log()
    cur = None
    for i, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip('\r\n')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        fields = line.split('\t')
        try:
            kind = fields[0].strip()
            if kind == 'H':
                if len(fields) < 2 or not fields[1].strip():
                    raise LogError('H line without exe commit')
                log.headers.append(' '.join(f.strip() for f in fields[1:]))
            elif kind == 'F':
                if len(fields) < 6:
                    raise LogError('F line has %d fields (want 6)' % len(fields))
                lvl = fields[2].strip()
                _int(lvl)
                cont = fields[3].strip()
                if cont not in ('0', '1'):
                    raise LogError('bad container flag %r' % cont)
                fx = parse_extra(fields[6]) if len(fields) > 6 else {}
                cur = Frame(fields[1].strip(), lvl, cont, fields[4].strip(), fields[5].strip(), i, origin, fx)
                log.frames.append(cur)
            elif kind in ('R', 'E'):
                if cur is None:
                    raise LogError('%s line before the first F line' % kind)
                fields[0] = kind
                ln = parse_entry(fields, i)
                cur.sources.setdefault((ln.src, ln.srcId), {'R': [], 'E': []})[kind].append(ln)
            else:
                raise LogError('unknown line kind %r' % kind)
        except (LogError, ValueError) as e:
            log.errors.append('%s:%d: %s' % (origin, i, e))
    return log


# ------------------------------------------------------------ triangles

def bbox(points):
    us = [p[0] for p in points]
    vs = [p[1] for p in points]
    return (min(us), min(vs), max(us), max(vs))


def _corners(ln):
    n = ln.ncorner
    uv = ln.uv if ln.uv is not None else (None,) * n
    if ln.col is None:
        col = (None,) * n
    elif len(ln.col) == 1:
        col = ln.col * n
    else:
        col = ln.col
    return list(zip(uv, col))


def _rot(corners):
    """Rotation of a triangle with the smallest (u,v) first (untextured: the
    smallest colour); the winding is kept."""
    best = None
    for i in range(len(corners)):
        c = corners[i:] + corners[:i]
        k = tuple((x[0] or (-1, -1), x[1] or ()) for x in c)
        if best is None or k < best[0]:
            best = (k, c)
    return best[1]


def line_options(ln):
    """Triangle sets that draw the face: a triangle as itself, a quad (PS1
    corner order 0 1 2 3) as the two triangles of either diagonal."""
    c = _corners(ln)
    if len(c) == 3:
        return [[c]]
    return [[[c[0], c[1], c[2]], [c[1], c[3], c[2]]], [[c[0], c[1], c[3]], [c[0], c[3], c[2]]]]


class Tri(object):
    __slots__ = ('e', 'mat', 'uvkey', 'cols', 'lod', 'key', 'fault')


def _cols_ok(a, b, ctol):
    if any(x is None for x in a) or any(x is None for x in b):
        return True
    return all(max(abs(p - q) for p, q in zip(x, y)) <= ctol for x, y in zip(a, b))


class EngineSet(object):
    """The engine triangles of one source in one frame."""

    def __init__(self, el):
        uniq = {}
        for e in el:
            k = e.tuple_key()
            if k not in uniq or (e.fault and not uniq[k].fault):
                uniq[k] = e
        self.lines = list(uniq.values())
        self.tris, self.by_uv, self.unknown = [], {}, {}
        self.lods = set((e.lod or '0') for e in self.lines)
        for e in self.lines:
            if e.page is not None and e.uv is None:
                self.unknown.setdefault((e.mat(), e.lod or '0', e.key), []).append(e)
                continue
            for corners in line_options(e)[0]:
                t = Tri()
                rc = _rot(corners)
                t.e, t.mat, t.lod, t.key, t.fault = e, e.mat(), e.lod or '0', e.key, e.fault
                t.uvkey = tuple(x[0] for x in rc)
                t.cols = tuple(x[1] for x in rc)
                self.tris.append(t)
                self.by_uv.setdefault((t.mat, t.uvkey, t.lod, t.key), []).append(t)

    def match(self, corners, mat, lod, key, ctol):
        rc = _rot(corners)
        uvkey = tuple(x[0] for x in rc)
        cols = tuple(x[1] for x in rc)
        for t in self.by_uv.get((mat, uvkey, lod, key), ()):
            if _cols_ok(cols, t.cols, ctol):
                return t
        u = self.unknown.get((mat, lod, key))
        return u[0] if u else None

    def keys(self):
        return set(e.key for e in self.lines)


def _tri_diffs(mat, rc, t, ctol):
    d = []
    for name, i in (('page', 0), ('depth', 1), ('clut', 2)):
        if mat[i] != t.mat[i]:
            d.append(name)
    uvkey = tuple(x[0] for x in rc)
    if uvkey != t.uvkey:
        d.append('uv')
    if mat[3] != t.mat[3]:
        d.append('blend')
    if uvkey == t.uvkey and not _cols_ok(tuple(x[1] for x in rc), t.cols, ctol):
        d.append('col')
    return d


def _uv_dist(rc, t):
    a = [x[0] for x in rc]
    if any(p is None for p in a) or any(p is None for p in t.uvkey):
        return 0 if all(p is None for p in a) and all(p is None for p in t.uvkey) else 100000
    return sum(abs(p[0] - q[0]) + abs(p[1] - q[1]) for p, q in zip(a, t.uvkey))


def nearest_tri(corners, mat, pool, ctol):
    rc = _rot(corners)
    best = None
    for t in pool:
        d = _tri_diffs(mat, rc, t, ctol)
        k = (len(d), _uv_dist(rc, t), 0 if t.fault else 1, t.e.lineno)
        if best is None or k < best[0]:
            best = (k, d, t)
    return (best[1], best[2]) if best else (None, None)


def match_line(r, es, lod, key, ctol, pool):
    """Matches a retail line against the engine set. Returns (used engine
    lines, None) when drawn, else (used, (features, engine line, fault, winding))."""
    used, best = [], None
    mat = r.mat()
    for opt in line_options(r):
        fails, got = [], []
        for corners in opt:
            t = es.match(corners, mat, lod, key, ctol)
            if t is not None:
                got.append(t.e if isinstance(t, Tri) else t)
            else:
                fails.append(corners)
        used += got
        if not fails:
            return used, None
        feats, eng, fault, wind = set(), None, None, False
        cost = 0
        for corners in fails:
            rev = [corners[0], corners[2], corners[1]]
            t = es.match(rev, mat, lod, key, ctol)
            if isinstance(t, Tri):
                feats.add('uv')
                wind = True
                cost += 1
                cand = t
            else:
                d, cand = nearest_tri(corners, mat, pool, ctol)
                if cand is None:
                    continue
                d = d or ['uv']
                feats.update(d)
                cost += len(d)
            if eng is None or (cand.fault and not fault):
                eng, fault = cand.e, cand.fault
        if best is None or cost < best[0]:
            best = (cost, [f for f in FEATURES if f in feats], eng, fault, wind)
    return used, best[1:]


# ------------------------------------------------------------------ colours

def col_gap(r, e):
    """Largest channel difference between the two colour sets (for the class pattern)."""
    if r is None or e is None or r.col is None or e.col is None:
        return None
    return max(min(max(abs(x - y) for x, y in zip(a, b)) for b in r.col) for a in e.col)


def col_tol(r, gte):
    return COL_TOL_NORMAL if r.prim in (None, 'normal') else gte


# --------------------------------------------------------------------- VRAM

class Vram(object):
    """PS1 VRAM, 1024x512 16-bit words, row 0 at the top."""

    def __init__(self, words):
        if len(words) != 1024 * 512:
            raise LogError('VRAM has %d words (want 1024x512)' % len(words))
        self.words = words

    def word(self, x, y):
        return self.words[(y & 511) * 1024 + (x & 1023)]

    def texel(self, page, depth, clut, u, v):
        """Colour word of page texel (u, v); the page wraps at 256 like the GPU."""
        u &= 255
        v &= 255
        px, py = page
        if depth == 0:
            w = self.word(px + (u >> 2), py + v)
            return self.word(clut[0] + ((w >> ((u & 3) * 4)) & 15), clut[1])
        if depth == 1:
            w = self.word(px + (u >> 1), py + v)
            return self.word(clut[0] + ((w >> ((u & 1) * 8)) & 255), clut[1])
        return self.word(px + u, py + v)


def read_tga_vram(data):
    """VRAM dump as written by the port: TGA type 2, 1024x512, 16 bit, raw PS1
    words (not converted to colours), rows bottom-up unless bit 5 of the
    descriptor says top-down."""
    if len(data) < 18:
        raise LogError('VRAM dump too short')
    idlen, cmtype, itype = data[0], data[1], data[2]
    cmlen, cmbits = data[5] | (data[6] << 8), data[7]
    w, h = data[12] | (data[13] << 8), data[14] | (data[15] << 8)
    bpp, desc = data[16], data[17]
    if itype != 2 or w != 1024 or h != 512 or bpp != 16:
        raise LogError('VRAM dump is not a 1024x512 16-bit TGA (type %d, %dx%d, %d bit)' % (itype, w, h, bpp))
    start = 18 + idlen + (cmlen * ((cmbits + 7) // 8) if cmtype else 0)
    body = data[start:start + 1024 * 512 * 2]
    if len(body) != 1024 * 512 * 2:
        raise LogError('VRAM dump truncated')
    rows = array.array('H')
    rows.frombytes(body)
    if sys.byteorder != 'little':
        rows.byteswap()
    if desc & 0x20:
        return Vram(rows)
    out = array.array('H')
    for y in range(511, -1, -1):
        out.extend(rows[y * 1024:(y + 1) * 1024])
    return Vram(out)


def box_outside(e, vram):
    """Texels of the engine sample box (box=) outside the UV box of the face
    (the integer box of its corners, which the retail path never leaves).
    Returns (ring0, ringf, reach, wrapped): ring0 = word 0x0000 where the
    nearest texel inside is not (the engine discards it, the background
    shows), ringf = another colour than that nearest texel, reach = all
    texels outside, wrapped = the box leaves the page (0..255)."""
    if e.box is None or e.uv is None or e.page is None:
        return 0, 0, 0, False
    wu0, wv0, wu1, wv1 = bbox(e.uv)
    bu0, bv0, bu1, bv1 = e.box
    if wu0 <= bu0 and wv0 <= bv0 and bu1 <= wu1 and bv1 <= wv1:
        return 0, 0, 0, False
    if (bu1 - bu0 + 1) * (bv1 - bv0 + 1) > BOX_AREA_MAX:
        bu0, bv0 = max(bu0, wu0 - 256), max(bv0, wv0 - 256)
        bu1, bv1 = min(bu1, wu1 + 256), min(bv1, wv1 + 256)
    ring0 = ringf = reach = 0
    wrapped = False
    for v in range(bv0, bv1 + 1):
        for u in range(bu0, bu1 + 1):
            if wu0 <= u <= wu1 and wv0 <= v <= wv1:
                continue
            reach += 1
            if not (0 <= u <= 255 and 0 <= v <= 255):
                wrapped = True
            w = vram.texel(e.page, e.depth, e.clut, u, v)
            inside = vram.texel(e.page, e.depth, e.clut, min(max(u, wu0), wu1), min(max(v, wv0), wv1))
            if w == inside:
                continue
            if w == 0:
                ring0 += 1
            else:
                ringf += 1
    return ring0, ringf, reach, wrapped


# --------------------------------------------------------------- formatting

def f_page(ln):
    return '-' if ln is None or ln.page is None else '%d,%d' % ln.page


def f_depth(ln):
    return '-' if ln is None or ln.depth is None else str(ln.depth)


def f_clut(ln):
    return '-' if ln is None or ln.clut is None else '%d,%d' % ln.clut


def f_uv(ln):
    return '-' if ln is None or ln.uv is None else ','.join('%d,%d' % p for p in ln.uv)


def f_extra(ln):
    if ln is None or not ln.extra:
        return '-'
    return ','.join('%s=%s' % kv for kv in sorted(ln.extra.items()))


def f_mat(ln):
    return 'page %s d%s clut %s blend %s' % (f_page(ln), f_depth(ln), f_clut(ln), ln.blend)


def f_matkey(m):
    return 'page %s d%s clut %s blend %s' % ('-' if m[0] is None else '%d,%d' % m[0],
                                             '-' if m[1] is None else m[1],
                                             '-' if m[2] is None else '%d,%d' % m[2], m[3])


def _delta(a, b):
    return ','.join('%+d' % (y - x) for x, y in zip(a, b))


def _gap_bucket(g):
    if g is None:
        return '?'
    return '<=8' if g <= 8 else ('9-32' if g <= 32 else ('33-128' if g <= 128 else '>128'))


def pattern(features, r, e, wind=False):
    """Short description of a direction-1 deviation, the same for the same
    kind of fault on any track (the key of the candidate classes)."""
    if r is None or e is None:
        return '-'
    parts = []
    for f in features:
        if f == 'prim':
            parts.append('prim=%s' % r.prim)
        elif f == 'anim':
            parts.append('anim %d>%d' % (r.anim, e.anim))
        elif f == 'page':
            parts.append('page d=%s' % (_delta(r.page, e.page) if r.page and e.page
                                        else '%s>%s' % (f_page(r), f_page(e))))
        elif f == 'depth':
            parts.append('depth %s>%s' % (f_depth(r), f_depth(e)))
        elif f == 'clut':
            parts.append('clut d=%s' % (_delta(r.clut, e.clut) if r.clut and e.clut
                                        else '%s>%s' % (f_clut(r), f_clut(e))))
        elif f == 'uv':
            if wind:
                parts.append('uv winding reversed')
            elif r.uv is not None and e.uv is not None:
                d = _delta(bbox(r.uv), bbox(e.uv))
                parts.append('uv box d=%s' % d if d != '+0,+0,+0,+0' else 'uv corners')
            else:
                parts.append('uv %s>%s' % ('-' if r.uv is None else 'set', '-' if e.uv is None else 'set'))
        elif f == 'blend':
            parts.append('blend %s>%s' % (r.blend, e.blend))
        elif f == 'col':
            parts.append('col gap %s' % _gap_bucket(col_gap(r, e)))
    return ' '.join(parts) or '-'


def object_key(src, src_id):
    if src == 'I' and '#' in src_id:
        return src_id.rsplit('#', 1)[0]
    return src_id


# --------------------------------------------------------------- comparison

class Record(object):
    def __init__(self, **kw):
        self.kind = kw['kind']                  # 'd1', 'd2', 'info'
        self.frame = kw.get('frame')
        self.track = kw['track']
        self.frames = kw.get('frames', 1)
        self.src, self.srcId = kw['src'], kw['srcId']
        self.object = '%s:%s' % (self.src, object_key(self.src, self.srcId))
        self.features = kw['features']
        self.r, self.e = kw.get('r'), kw.get('e')
        self.r_count = kw.get('r_count', 0)
        self.fault = kw.get('fault')            # fault kind of the engine line it points at
        if 'pattern' in kw:
            self.pattern = kw['pattern']
        elif self.kind == 'd1':
            self.pattern = pattern(self.features, self.r, self.e, kw.get('wind'))
        else:
            self.pattern = '-'
        self.detail = kw.get('detail', '-')
        self.excepted = None

    def feature_label(self):
        return '+'.join(self.features)


def _new_stats():
    return {'frames': 0, 'load_frames': 0, 'R': 0, 'E': 0, 'compared': 0, 'gap': 0,
            'fault_lines': 0, 'tracks': set(), 'fault_mats': [], 'engine_no_uv': 0,
            'r_no_col': 0, 'e_no_col': 0, 'box_checked': 0, 'box_unchecked': 0, 'e_no_box': 0,
            'vram_frames': 0, 'vram_missing': [], 'route_retail': {}, 'lod_lines': {},
            'per': {}, 'unused': {}, 'coverage': {}, 'prim_equal': 0}


def _per(stats, tr, src):
    return stats['per'].setdefault((tr, src), {'R': 0, 'E': 0, 'compared': 0})


def compare(frames, gte_tol=COL_TOL_GTE, vram_for=None):
    """Returns (records, stats). vram_for(frame) gives the frame's Vram or None."""
    records, stats = [], _new_stats()
    stats['frames'] = len(frames)
    per_track = {}            # track -> (src, model) -> object data (models)
    tcov = {}                 # track -> coverage sets (track faces)
    visible = set()           # (track, object, material) of fault lines on faces retail draws
    for fr in frames:
        tr = fr.track()
        stats['tracks'].add(tr)
        if fr.load:
            stats['load_frames'] += 1
        objs = per_track.setdefault(tr, {})
        cov = tcov.setdefault(tr, {'load_faces': set(), 'load_cells': set(), 'seen_faces': set(),
                                   'seen_cells': set()})
        vram = vram_for(fr) if (vram_for is not None and fr.vram) else None
        if vram is not None:
            stats['vram_frames'] += 1
        elif not fr.load:
            stats['vram_missing'].append(fr.tag)
        for (src, sid), sides in sorted(fr.sources.items()):
            rl, el = sides['R'], sides['E']
            p = _per(stats, tr, src)
            p['R'] += len(rl)
            p['E'] += len(el)
            stats['R'] += len(rl)
            stats['E'] += len(el)
            stats['fault_lines'] += sum(1 for e in el if e.fault)
            stats['engine_no_uv'] += sum(1 for e in el if e.page is not None and e.uv is None)
            stats['r_no_col'] += sum(1 for r in rl if r.col is None)
            stats['e_no_col'] += sum(1 for e in el if e.col is None)
            stats['e_no_box'] += sum(1 for e in el if e.box is None and e.page is not None)
            obj = '%s:%s' % (src, object_key(src, sid))
            if src == 'T':
                if fr.load:
                    cov['load_faces'].update(sid for e in el)
                    cov['load_cells'].update((sid, e.cell) for e in el if e.cell != '-')
                else:
                    cov['seen_faces'].update(sid for r in rl)
                    cov['seen_cells'].update((sid, r.cell) for r in rl if r.cell != '-')
            if any(r.route == 'retail' for r in rl):
                stats['route_retail'].setdefault(tr, set()).add(obj)
                continue
            es = EngineSet(el)
            ob = None
            if src != 'T':
                ob = objs.setdefault((src, object_key(src, sid)),
                                     {'R': set(), 'E': {}, 'srcId': sid, 'rlod': set(), 'seen': set(),
                                      'used': set()})
                for e in es.lines:
                    ent = ob['E'].setdefault(e.mat(), {'e': e, 'frames': set(), 'fault': None,
                                                       'lod': set()})
                    ent['frames'].add(id(fr))
                    ent['lod'].add(e.lod or '0')
                    if e.fault:
                        ent['fault'], ent['e'] = e.fault, e
                    ob['seen'].add(e.tuple_key())
            if not rl:
                continue
            if not es.lines:
                records.append(Record(kind='d1', frame=fr, track=tr, src=src, srcId=sid,
                                      features=['missing-source'], r=rl[0], r_count=len(rl)))
                continue
            # retail lines, one per distinct tuple
            ret = {}
            for r in rl:
                k = r.tuple_key()
                if k in ret:
                    ret[k][1] += 1
                else:
                    ret[k] = [r, 1]
            # fault lines on faces retail draws (for the control rule)
            rkeys = set()
            for r, _ in ret.values():
                for opt in line_options(r):
                    for c in opt:
                        rc = _rot(c)
                        rkeys.add((r.page, r.depth, r.blend, tuple(x[0] for x in rc), r.key))
            for t in es.tris:
                if t.fault and (t.mat[0], t.mat[1], t.mat[3], t.uvkey, t.key) in rkeys:
                    visible.add((tr, obj, t.mat))
            used_by = {}
            lod_count = {}
            for r, count in ret.values():
                rlod = r.lod or '0'
                if ob is not None:
                    ob['R'].add(r.mat())
                    ob['rlod'].add(rlod)
                if src != 'T' and rlod not in es.lods:
                    lod_count[rlod] = lod_count.get(rlod, 0) + count
                    continue
                p['compared'] += count
                stats['compared'] += count
                ctol = col_tol(r, gte_tol)
                if src == 'T':
                    same = [t for t in es.tris if t.key == r.key]
                    used, miss = match_line(r, es, rlod, r.key, ctol, same or es.tris)
                    if miss is not None and r.key[0] != '-':
                        for k2 in sorted(es.keys() - set([r.key]), key=str):
                            u2, m2 = match_line(r, es, rlod, k2, ctol, [])
                            if m2 is None:
                                e2 = u2[0]
                                records.append(Record(
                                    kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=[r.key[0]],
                                    r=r, e=e2, r_count=count, fault=e2.fault,
                                    pattern='%s %s>%s' % (r.key[0], r.key[1], k2[1])))
                                used, miss = u2, False
                                break
                        if miss is not False and not same:
                            d, cand = nearest_tri(_corners(r)[:3], r.mat(), es.tris, ctol)
                            records.append(Record(
                                kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=[r.key[0]],
                                r=r, e=cand.e if cand else None, r_count=count,
                                fault=cand.fault if cand else None,
                                pattern='%s %s absent' % r.key))
                            miss = False
                else:
                    pool = [t for t in es.tris if t.lod == rlod]
                    used, miss = match_line(r, es, rlod, None, ctol, pool)
                for e in used:
                    used_by.setdefault(id(e), (e, r))
                if not miss:
                    if r.prim not in (None, 'normal'):
                        stats['prim_equal'] += count
                    continue
                feats, eng, fault, wind = miss
                if not feats:
                    # no engine triangle to measure against (only lines without UV)
                    other = es.lines[0]
                    feats = [n for n, i in (('page', 0), ('depth', 1), ('clut', 2), ('blend', 3))
                             if r.mat()[i] != other.mat()[i]] or ['uv']
                    eng, fault = other, other.fault
                pre = []
                if r.prim not in (None, 'normal'):
                    pre.append('prim')
                if eng is not None and (r.anim or eng.anim):
                    pre.append('anim')
                records.append(Record(kind='d1', frame=fr, track=tr, src=src, srcId=sid,
                                      features=pre + feats, r=r, e=eng, r_count=count, fault=fault,
                                      wind=wind))
            for lv, n in sorted(lod_count.items()):
                rr = next(r for r, _ in ret.values() if (r.lod or '0') == lv)
                stats['lod_lines'][(tr, obj)] = stats['lod_lines'].get((tr, obj), 0) + n
                records.append(Record(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=['lod'],
                                      r=rr, r_count=n,
                                      pattern='lod %s>%s' % (lv, '|'.join(sorted(es.lods)))))
            if ob is not None:
                ob['used'].update(e.tuple_key() for e, _ in used_by.values())
            # track: engine entries without a retail line
            if src == 'T':
                rk = set(r.key for r, _ in ret.values())
                for e in es.lines:
                    if e.key in rk:
                        continue
                    if fr.load:
                        records.append(Record(kind='d2', frame=fr, track=tr, src=src, srcId=sid,
                                              features=['engine-only'], e=e, fault=e.fault,
                                              pattern='%s %s' % e.key))
                    else:
                        stats['gap'] += 1
            # sampling outside the window: engine entries that draw a face retail draws
            for e, r in used_by.values():
                if e.box is None or e.uv is None or e.page is None:
                    continue
                if vram is None:
                    stats['box_unchecked'] += 1
                    continue
                stats['box_checked'] += 1
                ring0, ringf, reach, wrapped = box_outside(e, vram)
                if ring0 or ringf:
                    records.append(Record(
                        kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=['sample-outside'],
                        r=r, e=e, r_count=1, fault=e.fault,
                        pattern=('ring0' if ring0 else 'ringf') + (' wrap' if wrapped else ''),
                        detail='box %s uv box %s ring0 %d ringf %d reach %d' % (
                            ','.join(map(str, e.box)), ','.join(map(str, bbox(e.uv))), ring0, ringf, reach)))
    # direction 2, models
    reported = set((r.track, r.object, r.e.mat()) for r in records if r.kind == 'd1' and r.e is not None)
    for tr in sorted(per_track):
        for (src, ok), ob in sorted(per_track[tr].items()):
            obj = '%s:%s' % (src, ok)
            stats['unused'][(tr, obj)] = (len(ob['seen']), len(ob['seen'] & ob['used']))
            if not ob['R']:
                stats['fault_mats'] += [(tr, obj, m, v['fault'], False) for m, v in ob['E'].items() if v['fault']]
                if ob['E']:
                    any_e = next(iter(ob['E'].values()))
                    records.append(Record(kind='info', track=tr, src=src, srcId=ob['srcId'],
                                          features=['engine-only-source'], e=any_e['e'],
                                          frames=len(set().union(*[v['frames'] for v in ob['E'].values()]))))
                continue
            for mat, ent in sorted(ob['E'].items(), key=lambda kv: kv[1]['e'].lineno):
                if ent['fault']:
                    stats['fault_mats'].append((tr, obj, mat, ent['fault'], (tr, obj, mat) in visible))
                if mat in ob['R'] or (tr, obj, mat) in reported:
                    continue
                if not (ent['lod'] & ob['rlod']):
                    records.append(Record(kind='d2', track=tr, src=src, srcId=ent['e'].srcId,
                                          features=['lod'], e=ent['e'], frames=len(ent['frames']),
                                          fault=ent['fault'],
                                          pattern='lod %s>%s (engine-only-material)' % (
                                              '|'.join(sorted(ob['rlod'])), '|'.join(sorted(ent['lod'])))))
                    continue
                records.append(Record(kind='d2', track=tr, src=src, srcId=ent['e'].srcId,
                                      features=['engine-only-material'], e=ent['e'],
                                      frames=len(ent['frames']), fault=ent['fault']))
    # faulted track lines (cell faults): checkable when retail draws that face in a frame
    for fr in frames:
        for (src, sid), sides in fr.sources.items():
            if src != 'T':
                continue
            for e in sides['E']:
                if e.fault:
                    stats['fault_mats'].append((fr.track(), 'T:%s' % sid, (e.mat(), e.key), e.fault,
                                                bool(sides['R'])))
    for tr, cov in tcov.items():
        if cov['load_faces']:
            stats['coverage'][tr] = (len(cov['load_faces']), len(cov['load_faces'] & cov['seen_faces']),
                                     len(cov['load_cells']), len(cov['load_cells'] & cov['seen_cells']))
    stats['tracks'] = sorted(stats['tracks'])
    return records, stats


# --------------------------------------------------------------- exceptions

TERM = re.compile(r'^(!?)([fre])\.([A-Za-z0-9_]+)\s*(?:(!=|!~|=|~)\s*(.*))?$')
FRAME_KEYS = ('tag', 'level', 'container', 'view', 'track', 'raceFrame')


class ExceptionRow(object):
    def __init__(self, xid, srcs, features, terms, reason, lineno):
        self.id, self.srcs, self.features = xid, srcs, features
        self.terms, self.reason, self.lineno = terms, reason, lineno
        self.hits = 0
        self.doc = features == 'doc'


def parse_exceptions(text, origin='exceptions'):
    """Returns (rows, errors)."""
    rows, errors, header = [], [], False
    seen = set()
    for i, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip('\r\n')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        f = [x.strip() for x in line.split('\t')]
        if not header:
            if f != ['id', 'src', 'feature', 'condition', 'reason']:
                errors.append('%s:%d: header must be: id src feature condition reason (tabs)' % (origin, i))
                return rows, errors
            header = True
            continue
        if len(f) != 5:
            errors.append('%s:%d: %d fields (want 5)' % (origin, i, len(f)))
            continue
        xid, src, feat, cond, reason = f
        try:
            if not re.match(r'^[a-z0-9][a-z0-9-]*$', xid) or xid in seen:
                raise LogError('bad or repeated id %r' % xid)
            seen.add(xid)
            if len(reason) < 20:
                raise LogError('reason missing or too short')
            if src == '*':
                srcs = None
            else:
                srcs = set(src.split('|'))
                if not srcs <= set(SOURCES):
                    raise LogError('bad src %r' % src)
            if feat == '-':
                feats = 'doc'
                if cond != '-':
                    raise LogError('a documentation row (feature -) has condition -')
            elif feat == '*':
                feats = None
            else:
                feats = set(feat.split('|'))
                if not feats <= set(FEATURES):
                    raise LogError('bad feature %r (want * or %s)' % (feat, '|'.join(FEATURES)))
            terms = []
            if cond != '-':
                for t in cond.split(';'):
                    m = TERM.match(t.strip())
                    if not m or (m.group(1) and m.group(4)):
                        raise LogError('bad condition term %r' % t.strip())
                    neg, side, key, op, val = m.groups()
                    if side == 'f' and key not in FRAME_KEYS:
                        raise LogError('unknown frame key %r' % key)
                    terms.append((neg == '!', side, key, op, (val or '').strip()))
            rows.append(ExceptionRow(xid, srcs, feats, terms, reason, i))
        except LogError as e:
            errors.append('%s:%d: %s' % (origin, i, e))
    if not header:
        errors.append('%s: no header line' % origin)
    return rows, errors


def _value(rec, side, key):
    if side == 'f':
        fr = rec.frame
        if key == 'track':
            return rec.track
        if fr is None:
            return None
        return {'tag': fr.tag, 'level': fr.level, 'container': fr.container, 'view': fr.view,
                'raceFrame': fr.race_frame}[key]
    ln = rec.r if side == 'r' else rec.e
    if ln is None:
        return None
    if key == 'page':
        return f_page(ln)
    if key == 'depth':
        return f_depth(ln)
    if key == 'clut':
        return f_clut(ln)
    if key == 'uv':
        return f_uv(ln)
    if key in ('blend', 'cell', 'srcId', 'sub', 'src'):
        return getattr(ln, key)
    return ln.extra.get(key)


def _term(rec, term):
    neg, side, key, op, val = term
    v = _value(rec, side, key)
    if op is None:
        return (v is None) if neg else (v is not None)
    if op == '=':
        return v is not None and v == val
    if op == '!=':
        return v is None or v != val
    if op == '~':
        return v is not None and fnmatch.fnmatchcase(v, val)
    return v is None or not fnmatch.fnmatchcase(v, val)


def apply_exceptions(records, rows):
    for rec in records:
        if rec.kind == 'info':
            continue
        for x in rows:
            if x.doc:
                continue
            if x.srcs is not None and rec.src not in x.srcs:
                continue
            if x.features is not None and not set(rec.features) <= x.features:
                continue
            if all(_term(rec, t) for t in x.terms):
                rec.excepted = x.id
                x.hits += 1
                break


# ------------------------------------------------------------------ results

class Result(object):
    pass


def parse_expect(specs):
    """--expect values: fault:<kind> or TRACK:SRC:MODEL:FEATURE."""
    faults, positives, errors = [], [], []
    for s in specs or []:
        p = s.split(':')
        if len(p) == 2 and p[0] == 'fault' and p[1]:
            faults.append(p[1])
        elif len(p) == 4 and p[1] in SOURCES and len(p[1]) == 1 and p[3] in FEATURES:
            positives.append((p[0], '%s:%s' % (p[1], p[2]), p[3]))
        else:
            errors.append('bad --expect %r (want fault:<kind> or TRACK:SRC:MODEL:FEATURE)' % s)
    return faults, positives, errors


def plausibility(log, stats):
    """Problems that make a zero worthless (dead hooks, missing dumps)."""
    out = []
    if not log.headers:
        out.append('no H line (exe commit and switches)')
    for tr in stats['tracks']:
        kinds = set(src for (t, src) in stats['per'] if t == tr)
        need = set(['M']) if 'M' in kinds else set(['T', 'I'])
        routed = set(o.split(':')[0] for o in stats['route_retail'].get(tr, ()))
        for src in sorted(kinds | need):
            p = stats['per'].get((tr, src), {'R': 0, 'E': 0})
            if p['R'] == 0:
                out.append('%s %s: no retail line' % (tr, src))
            if p['E'] == 0 and src not in routed:
                out.append('%s %s: no engine line' % (tr, src))
    if stats['vram_missing']:
        out.append('%d race frame(s) without a readable VRAM dump: %s' % (
            len(stats['vram_missing']), ' '.join(stats['vram_missing'][:10])))
    return out


def analyse(log, rows, gte_tol=COL_TOL_GTE, expect=None, vram_for=None, check_plausible=True):
    records, stats = compare(log.frames, gte_tol, vram_for)
    apply_exceptions(records, rows)
    faults, positives, _ = parse_expect(expect)
    res = Result()
    res.log, res.records, res.stats, res.rows = log, records, stats, rows
    devs = [r for r in records if r.kind in ('d1', 'd2')]
    res.open = [r for r in devs if r.excepted is None]
    res.covered = [r for r in devs if r.excepted is not None]
    res.control_open = [r for r in res.open if r.fault]
    res.control_covered = [r for r in res.covered if r.fault]
    res.others = [r for r in res.open if not r.fault]
    res.info = [r for r in records if r.kind == 'info']
    res.control_missing = []
    failed = False
    # controls, per fault kind
    kinds = sorted(set(k for _, _, _, k, _ in stats['fault_mats']) | set(faults))
    parts = []
    for kind in kinds:
        target = FAULT_TARGET.get(kind)
        mats = sorted(set((t, o, m) for t, o, m, k, vis in stats['fault_mats'] if k == kind and vis), key=repr)
        blind = len(set((t, o, m) for t, o, m, k, vis in stats['fault_mats'] if k == kind and not vis) -
                    set(mats))
        mine = [r for r in res.control_open if r.fault == kind]
        hits = [r for r in mine if target is None or target in r.features]

        def mkey(r):
            return (r.track, r.object, (r.e.mat(), r.e.key) if r.src == 'T' else r.e.mat())
        reported = set(mkey(r) for r in mine if r.e is not None)
        missing = [m for m in mats if m not in reported]
        res.control_missing += [(kind,) + m for m in missing]
        if not any(k == kind for _, _, _, k, _ in stats['fault_mats']):
            parts.append('%s no - FAILED: expected, no fault=%s line in the log' % (kind, kind))
            failed = True
        elif hits and not missing:
            parts.append('%s yes (%d deviation(s), feature %s on %s; %d faulted material(s) on drawn faces%s)' % (
                kind, len(mine), target or 'any', ' '.join(sorted(set(r.track for r in hits))), len(mats),
                ', %d not drawn by retail' % blind if blind else ''))
        else:
            swallowed = sorted(set(r.excepted for r in res.control_covered if r.fault == kind))
            why = ('covered by exception(s) %s' % ', '.join(swallowed)) if swallowed and not hits else (
                '%d of %d faulted material(s) not reported' % (len(missing), len(mats)) if missing
                else 'no deviation with feature %s' % target)
            parts.append('%s no - FAILED: %s' % (kind, why))
            failed = True
    if not kinds:
        res.control = 'no (no fault= line in the log; a zero without the control does not count)'
    else:
        res.control = ('yes' if not failed else 'no') + ' - ' + '; '.join(parts)
    # known positives
    res.positives = []
    for tr, obj, feat in positives:
        n = sum(1 for r in res.open if r.track == tr and r.object == obj and feat in r.features)
        res.positives.append((tr, obj, feat, n))
        if not n:
            failed = True
    res.plausible = plausibility(log, stats) if check_plausible else []
    if res.plausible:
        failed = True
    res.exit = 3 if failed else (1 if res.others else 0)
    return res


def classes(res):
    out = {}
    for r in res.open:
        k = (r.src, r.feature_label(), r.pattern)
        c = out.setdefault(k, {'n': 0, 'tracks': set(), 'objects': set(), 'fault': 0})
        c['n'] += 1
        c['tracks'].add(r.track)
        c['objects'].add(r.object)
        c['fault'] += 1 if r.fault else 0
    return sorted(out.items(), key=lambda kv: (-kv[1]['n'], kv[0]))


TSV_COLUMNS = ('status', 'kind', 'exception', 'control', 'track', 'tag', 'raceFrame', 'view',
               'frames', 'src', 'srcId', 'object', 'features', 'pattern', 'detail', 'r_count',
               'r_line', 'r_page', 'r_depth', 'r_clut', 'r_uv', 'r_blend', 'r_cell', 'r_extra',
               'e_line', 'e_page', 'e_depth', 'e_clut', 'e_uv', 'e_blend', 'e_cell', 'e_extra')


def _clean(s):
    return str(s).replace('\t', ' ').replace('\n', ' ')


def render_tsv(res):
    lines = ['\t'.join(TSV_COLUMNS)]
    for rec in res.records:
        status = 'info' if rec.kind == 'info' else ('covered' if rec.excepted else 'deviation')
        fr = rec.frame
        row = [status, rec.kind, rec.excepted or '-', rec.fault or '-', rec.track,
               fr.tag if fr else '-', fr.race_frame if fr else '-', fr.view if fr else '-',
               rec.frames, rec.src, rec.srcId, rec.object, rec.feature_label(), rec.pattern,
               rec.detail, rec.r_count]
        for ln in (rec.r, rec.e):
            if ln is None:
                row += ['-'] * 8
            else:
                row += [ln.lineno, f_page(ln), f_depth(ln), f_clut(ln), f_uv(ln), ln.blend,
                        ln.cell, f_extra(ln)]
        lines.append('\t'.join(_clean(x) for x in row))
    return '\n'.join(lines) + '\n'


def render_classes_tsv(res):
    lines = ['\t'.join(('class', 'src', 'features', 'pattern', 'count', 'control', 'tracks',
                        'objects'))]
    for i, ((src, feats, pat), c) in enumerate(classes(res), 1):
        lines.append('\t'.join(_clean(x) for x in (
            'K%d' % i, src, feats, pat, c['n'], c['fault'], ' '.join(sorted(c['tracks'])),
            ' '.join(sorted(c['objects'])))))
    return '\n'.join(lines) + '\n'


def _table(head, rows, limit=MD_ROW_LIMIT):
    out = ['| ' + ' | '.join(head) + ' |', '|' + '---|' * len(head)]
    for r in rows[:limit]:
        out.append('| ' + ' | '.join(_clean(x).replace('|', '/') for x in r) + ' |')
    if len(rows) > limit:
        out.append('')
        out.append('(%d more rows in the TSV)' % (len(rows) - limit))
    return out


def _count(items, key):
    out = {}
    for it in items:
        k = key(it)
        out[k] = out.get(k, 0) + 1
    return out


def render_md(res, inputs, exc_origin):
    s = res.stats
    md = ['# Material comparison', '']
    md.append('Control fired: %s' % res.control)
    for m in res.control_missing[:20]:
        kind, t, o, mat = m
        md.append('- not reported (%s): %s %s %s' % (kind, t, o, f_matkey(mat if len(mat) == 4 else mat[0])))
    if res.positives:
        md.append('Known positives: %s' % '; '.join('%s %s %s %s' % (t, o, f, ('yes (%d)' % n) if n else 'no - FAILED')
                                                for t, o, f, n in res.positives))
    md.append('Plausibility: %s' % ('ok' if not res.plausible else 'FAILED - ' + '; '.join(res.plausible[:12])))
    md.append('')
    md.append('Deviations outside the exception list and the controls: %d' % len(res.others))
    md.append('Control deviations: %d; covered by the exception list: %d' % (len(res.control_open), len(res.covered)))
    md.append('Input: %s - %s; %d frame(s) (%d load block), track(s) %s, %d retail line(s), %d engine line(s), '
              '%d retail tuple(s) compared, %d fault= line(s)' % (
                  ', '.join(inputs), ' / '.join(res.log.headers) or 'no H line', s['frames'], s['load_frames'],
                  ' '.join(s['tracks']) or '-', s['R'], s['E'], s['compared'], s['fault_lines']))
    md.append('VRAM: %d frame(s) with a dump, %d race frame(s) without; %d engine box(es) checked, '
              '%d not checked (no dump), %d textured engine line(s) without box' % (
                  s['vram_frames'], len(s['vram_missing']), s['box_checked'], s['box_unchecked'], s['e_no_box']))
    md.append('Colour: tolerance %d per channel for the normal writer, %d for GTE writers; '
              '%d retail / %d engine line(s) without col (not compared)' % (
                  COL_TOL_NORMAL, res.gte_tol if hasattr(res, 'gte_tol') else COL_TOL_GTE,
                  s['r_no_col'], s['e_no_col']))
    md.append('Exceptions: %s' % exc_origin)
    md.append('')
    md.append('## Exception list')
    md.append('')
    md += _table(('id', 'src', 'feature', 'condition', 'covered', 'reason'), [
        (x.id, '*' if x.srcs is None else '|'.join(sorted(x.srcs)),
         '- (not compared)' if x.doc else ('*' if x.features is None else '|'.join(sorted(x.features))),
         '; '.join(('!' if t[0] else '') + '%s.%s%s%s' % (t[1], t[2], t[3] or '', t[4]) for t in x.terms) or '-',
         '-' if x.doc else x.hits, x.reason) for x in res.rows], limit=1000)
    md.append('')
    md.append('## Counters per track and source kind')
    md.append('')
    rows = []
    for (tr, src), p in sorted(s['per'].items()):
        rows.append((tr, src, p['R'], p['E'], p['compared'],
                     sum(1 for r in res.others if r.track == tr and r.src == src),
                     sum(1 for r in res.control_open if r.track == tr and r.src == src),
                     sum(1 for r in res.covered if r.track == tr and r.src == src),
                     sum(1 for o in s['route_retail'].get(tr, ()) if o.startswith(src + ':')),
                     sum(n for (t, o), n in s['lod_lines'].items() if t == tr and o.startswith(src + ':'))))
    md += _table(('track', 'src', 'R lines', 'E lines', 'compared', 'deviations', 'control', 'covered',
                  'route=retail', 'lod>0 lines'), rows, limit=1000)
    md.append('')
    md.append('## Per feature (outside the exception list and the controls)')
    md.append('')
    byf = _count(res.others, lambda r: r.feature_label())
    md += _table(('features', 'count'), sorted(byf.items(), key=lambda kv: (-kv[1], kv[0])))
    md.append('')
    md.append('## Per object (outside the exception list and the controls)')
    md.append('')
    byo = _count(res.others, lambda r: (r.track, r.object, r.feature_label()))
    md += _table(('track', 'object', 'features', 'count'),
                 [k + (v,) for k, v in sorted(byo.items(), key=lambda kv: (-kv[1], kv[0]))])
    md.append('')
    md.append('## Candidate classes (same source kind, features and pattern across tracks)')
    md.append('')
    md += _table(('class', 'src', 'features', 'pattern', 'count', 'control', 'tracks', 'objects'), [
        ('K%d' % i, src, feats, pat, c['n'], c['fault'], ' '.join(sorted(c['tracks'])),
         ' '.join(sorted(c['objects'])[:12]) + (' ...' if len(c['objects']) > 12 else ''))
        for i, ((src, feats, pat), c) in enumerate(classes(res), 1)])
    md.append('')
    md.append('## Coverage')
    md.append('')
    md.append('- Track: engine entries in race frames without a retail line (coverage gaps): %d' % s['gap'])
    md.append('- Retail lines of a draw path other than normal that matched: %d' % s['prim_equal'])
    md.append('- Textured engine lines without UV (external meshes; UV not checked): %d' % s['engine_no_uv'])
    if s['coverage']:
        md.append('')
        md += _table(('track', 'faces (load block)', 'seen in race frames', 'cells (load block)', 'seen'),
                     [(tr,) + v for tr, v in sorted(s['coverage'].items())], limit=1000)
    md.append('')
    md += _table(('track', 'model', 'engine tuples', 'used', 'unused'),
                 [(t, o, a, b, a - b) for (t, o), (a, b) in sorted(s['unused'].items())])
    md.append('')
    md.append('## Information (not deviations)')
    md.append('')
    md.append('- Models the engine draws but retail never draws on the track: %d' % len(res.info))
    for tr in sorted(s['route_retail']):
        md.append('- %s route=retail: %s' % (tr, ' '.join(sorted(s['route_retail'][tr]))))
    if res.info:
        md.append('')
        md += _table(('track', 'model', 'frames'), sorted((r.track, r.object, r.frames) for r in res.info))
    md.append('')
    md.append('## Samples (first deviation of each class)')
    md.append('')
    seen, samples = set(), []
    for r in res.open:
        k = (r.src, r.feature_label(), r.pattern)
        if k in seen:
            continue
        seen.add(k)
        samples.append((r.track, r.frame.tag if r.frame else '-', r.frame.race_frame if r.frame else '-',
                        r.srcId, r.feature_label(),
                        ('%s uv %s %s (line %d)' % (f_mat(r.r), f_uv(r.r), f_extra(r.r), r.r.lineno)) if r.r else '-',
                        ('%s uv %s %s (line %d)' % (f_mat(r.e), f_uv(r.e), f_extra(r.e), r.e.lineno)) if r.e else '-'))
    md += _table(('track', 'tag', 'frame', 'srcId', 'features', 'retail', 'engine (nearest)'), samples)
    return '\n'.join(md) + '\n'


# ---------------------------------------------------------------------- CLI

def load_exceptions(path):
    try:
        with open(path, encoding='utf-8') as fh:
            text = fh.read()
    except OSError as e:
        return [], ['cannot read %s: %s' % (path, e)]
    return parse_exceptions(text, path)


def file_vram_loader():
    cache = {}

    def load(fr):
        path = os.path.join(fr.base_dir, fr.vram)
        if path not in cache:
            if len(cache) > 4:
                cache.clear()
            try:
                with open(path, 'rb') as fh:
                    cache[path] = read_tga_vram(fh.read())
            except (OSError, LogError) as e:
                print('material_compare: VRAM dump %s: %s' % (path, e))
                cache[path] = None
        return cache[path]
    return load


def main(argv):
    if argv == ['--selftest']:
        return selftest()
    ap = argparse.ArgumentParser(description='Material comparison engine against retail (oracle log).')
    ap.add_argument('logs', nargs='+', help='oracle log file(s)')
    ap.add_argument('--out', help='write PREFIX.md, PREFIX.tsv, PREFIX-classes.tsv')
    ap.add_argument('--exceptions', default=DEFAULT_EXCEPTIONS)
    ap.add_argument('--expect', action='append', default=[],
                    help='fault:<kind> or TRACK:SRC:MODEL:FEATURE that must fire (repeatable)')
    ap.add_argument('--col-tol-gte', type=int, default=COL_TOL_GTE,
                    help='colour tolerance for GTE retail writers (default %d)' % COL_TOL_GTE)
    ap.add_argument('--lenient', action='store_true', help='skip malformed log lines (counted)')
    a = ap.parse_args(argv)
    _, _, xerr = parse_expect(a.expect)
    rows, errs = load_exceptions(a.exceptions)
    for e in xerr + errs:
        print('material_compare: %s' % e)
    if xerr or errs:
        return 2
    log = Log()
    for path in a.logs:
        try:
            with open(path, encoding='utf-8', errors='replace') as fh:
                text = fh.read()
        except OSError as e:
            print('material_compare: cannot read %s: %s' % (path, e))
            return 2
        n = len(log.frames)
        parse_log(text, os.path.basename(path), log)
        for fr in log.frames[n:]:
            fr.base_dir = os.path.dirname(os.path.abspath(path))
    bad = log.errors
    if bad:
        for e in bad[:20]:
            print('material_compare: %s' % e)
        if len(bad) > 20:
            print('material_compare: ... %d more' % (len(bad) - 20))
        if not a.lenient:
            print('material_compare: %d malformed line(s) - fix the log or use --lenient' % len(bad))
            return 2
    if not log.frames:
        print('material_compare: no F line in the log')
        return 2
    res = analyse(log, rows, a.col_tol_gte, a.expect, file_vram_loader())
    res.gte_tol = a.col_tol_gte
    md = render_md(res, [os.path.basename(p) for p in a.logs], os.path.basename(a.exceptions))
    if bad:
        md = md.replace('\nExceptions: ', '\nMalformed lines skipped (--lenient): %d\nExceptions: ' % len(bad), 1)
    if a.out:
        for suffix, text in (('.md', md), ('.tsv', render_tsv(res)),
                             ('-classes.tsv', render_classes_tsv(res))):
            with open(a.out + suffix, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write(text)
        print('material_compare: control fired: %s' % res.control)
        print('material_compare: %d deviation(s) outside the exception list and the controls, '
              '%d control, %d covered; exit %d; written %s.md/.tsv/-classes.tsv' % (
                  len(res.others), len(res.control_open), len(res.covered), res.exit, a.out))
    else:
        sys.stdout.write(md)
    return res.exit


# ----------------------------------------------------------------- selftest

def selftest():
    checks = [0]
    fails = []

    def check(name, cond):
        checks[0] += 1
        if not cond:
            fails.append(name)
            print('FAIL: %s' % name)

    def F(tag='t', level=3, cont=0, frame=180, view=1, extra='-'):
        return '\t'.join(('F', tag, str(level), str(cont), str(frame), str(view), extra))

    def L(side, src, sid, page='512,0,0', clut='0,480', uv='0,0,31,0,0,31,31,31', blend='o',
          cell='-', extra='-', sub='0'):
        return '\t'.join((side, src, sid, sub, page, clut, uv, blend, cell, extra))

    QUAD = '0,0,31,0,0,31,31,31'
    TRI_A, TRI_B = '0,0,31,0,0,31', '31,0,31,31,0,31'      # diagonal 1-2 (PS1 order 0 1 2, 1 3 2)
    TRI_C, TRI_D = '0,0,31,0,31,31', '0,0,31,31,0,31'      # diagonal 0-3 (0 1 3, 0 3 2)
    TRI_A_REV = '0,0,0,31,31,0'

    exc_text = '\n'.join((
        '# synthetic exception list',
        'id\tsrc\tfeature\tcondition\treason',
        'sky\tS|W\t*\t-\tthe engine draws its own sky and water look (accepted by screenshot)',
        'hires\tD\t*\te.hires=1\tthe engine reads a hi-res driver texture instead of VRAM',
        'only-uv\tI\tuv\tr.marker=1\tsynthetic: uv deviations of marked retail faces only',
        'doc\tT\t-\t-\tdocumentation row: a field the tool does not compare',
    ))
    rows, errs = parse_exceptions(exc_text, 'synthetic')
    check('synthetic exception list parses', not errs and len(rows) == 4)

    def run(lines, expect=None, rws=None, vram=None, plaus=False, gte=COL_TOL_GTE):
        log = parse_log('\n'.join(['H\tabc1234\t--engine --engine-oracle x'] + lines) + '\n', 'synthetic')
        if log.errors:
            print('parse errors: %s' % log.errors)
        rr = rws if rws is not None else rows
        for x in rr:
            x.hits = 0
        res = analyse(log, rr, gte, expect, (lambda fr: vram) if vram is not None else None, plaus)
        return res, log.errors

    def feats(res, kind='d1'):
        return sorted(r.feature_label() for r in res.others + res.control_open if r.kind == kind)

    base_r = [F(), L('R', 'I', 'tikka#0', uv=QUAD)]
    eng_ok = [L('E', 'I', 'tikka#0', uv=TRI_A, sub='0'), L('E', 'I', 'tikka#0', uv=TRI_B, sub='1')]

    # (a) same
    res, er = run(base_r + eng_ok)
    check('(a) same: no parse error', not er)
    check('(a) same: 0 deviations, exit 0', len(res.open) == 0 and res.exit == 0)
    check('(a) same: no control in the log', res.control.startswith('no (no fault'))
    res, _ = run(base_r + [L('E', 'I', 'tikka#0', uv=TRI_C), L('E', 'I', 'tikka#0', uv=TRI_D)])
    check('(a) same: other diagonal', len(res.open) == 0)
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv='0,31,0,0,31,0')])
    check('(a) same: triangle rotated, winding kept', len(res.open) == 0)
    res, _ = run([F(), L('R', 'I', 'b#0', uv=TRI_A, extra='col=10,20,30;40,50,60;70,80,90'),
                  L('E', 'I', 'b#0', uv='31,0,0,31,0,0', extra='col=40,50,60;70,80,90;10,20,30')])
    check('(a) same: colours follow the rotated corners', len(res.open) == 0)

    # (b) one field each
    def one(engine, expect_feat, name, extra_r='-'):
        res, _ = run([F(), L('R', 'I', 'tikka#0', uv=QUAD, extra=extra_r)] + engine)
        d1 = [r for r in res.open if r.kind == 'd1']
        check('(b) %s: one deviation, feature %s' % (name, expect_feat),
              len(d1) == 1 and d1[0].feature_label() == expect_feat and len(res.open) == 1)
        return res
    res = one([L('E', 'I', 'tikka#0', page='576,0,0', uv=TRI_A), L('E', 'I', 'tikka#0', page='576,0,0', uv=TRI_B)],
              'page', 'page')
    check('(b) page: pattern', res.open[0].pattern == 'page d=+64,+0')
    one([L('E', 'I', 'tikka#0', page='512,0,1', uv=TRI_A), L('E', 'I', 'tikka#0', page='512,0,1', uv=TRI_B)],
        'depth', 'depth')
    res = one([L('E', 'I', 'tikka#0', clut='16,480', uv=TRI_A), L('E', 'I', 'tikka#0', clut='16,480', uv=TRI_B)],
              'clut', 'clut')
    check('(b) clut: pattern and nearest candidate', res.open[0].pattern == 'clut d=+16,+0' and res.open[0].e)
    res = one([L('E', 'I', 'tikka#0', uv='0,1,31,1,0,32'), L('E', 'I', 'tikka#0', uv='31,1,31,32,0,32')],
              'uv', 'uv +1 texel')
    check('(b) uv: pattern box delta', res.open[0].pattern == 'uv box d=+0,+1,+0,+1')
    one([L('E', 'I', 'tikka#0', uv=TRI_A, blend='1'), L('E', 'I', 'tikka#0', uv=TRI_B, blend='1')],
        'blend', 'blend o>1')
    one([L('E', 'I', 'tikka#0', uv=TRI_A, extra='col=128,128,128'), L('E', 'I', 'tikka#0', uv=TRI_B, extra='col=128,128,128')],
        'col', 'colour 1 above tolerance 0 (normal writer)', 'col=129,128,128')
    res, _ = run([F(), L('R', 'I', 'f#0', uv=TRI_A, extra='prim=lit col=130,128,128'),
                  L('E', 'I', 'f#0', uv=TRI_A, extra='col=128,128,128')])
    check('(b) colour within the GTE tolerance 2: 0', len(res.open) == 0)
    one([L('E', 'I', 'tikka#0', uv=TRI_A)], 'uv', 'half of the quad')
    one([L('E', 'I', 'tikka#0', uv=TRI_A_REV), L('E', 'I', 'tikka#0', uv=TRI_B)], 'uv', 'winding reversed')
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv=TRI_A_REV)])
    check('(b) winding reversed: pattern', [r.pattern for r in res.open] == ['uv winding reversed'])
    # half the quad with a wrong CLUT says clut
    res, _ = run(base_r + [L('E', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', clut='16,480', uv=TRI_B)])
    check('(b) half a quad with another CLUT: clut', feats(res) == ['clut'])
    # untextured faces and blend '-' as opaque
    res, _ = run([F(), L('R', 'X', 'spark', page='-', clut='-,-', uv='-', blend='-', extra='col=1,2,3'),
                  L('E', 'X', 'spark', page='-,-,-', clut='-', uv='-', blend='o', extra='col=1,2,3')])
    check('(b) untextured: dashes match', len(res.open) == 0)
    res, _ = run([F(), L('R', 'X', 'spark', page='-', clut='-', uv='-', blend='1'),
                  L('E', 'X', 'spark', page='-', clut='-', uv='-', blend='o')])
    check('(b) untextured: blend still compared', feats(res) == ['blend'])
    res, _ = run([F(), L('R', 'M', 'logo', page='640,0,2', clut='-,-', uv=TRI_A),
                  L('E', 'M', 'logo', page='640,0,2', clut='-,-', uv=TRI_A)])
    check('(b) 15-bit page without CLUT: same', len(res.open) == 0)
    res, _ = run([F(), L('R', 'D', '1', uv=QUAD), L('E', 'D', '1', uv='-', extra='mesh=external')])
    check('(b) engine UV unknown: covers, counted', len(res.open) == 0 and res.stats['engine_no_uv'] == 1)

    # track faces: stage and cell
    def T(side, sid, uv, key, clut='0,480', extra=''):
        cell = key[5:] if key.startswith('cell=') else '-'
        ex = ','.join(x for x in (key if key.startswith('stage=') else '', extra) if x) or '-'
        return L(side, 'T', sid, uv=uv, cell=cell, clut=clut, extra=ex)
    C00, C01, C10, C11 = '0,0,7,0,0,7,7,7', '8,0,15,0,8,7,15,7', '0,8,7,8,0,15,7,15', '8,8,15,8,8,15,15,15'
    eng_cells = [T('E', '5', C00, 'cell=0,0'), T('E', '5', C01, 'cell=0,1'), T('E', '5', C10, 'cell=1,0'),
                 T('E', '5', C11, 'cell=1,1'), T('E', '5', '0,0,15,0,0,15,15,15', 'stage=2')]
    res, _ = run([F(), T('R', '5', '0,0,15,0,0,15,15,15', 'stage=2')] + eng_cells)
    check('(d) track: retail stage 2 against engine cells + stage 2: 0', len(res.open) == 0)
    check('(d) track: engine cells without retail line are coverage gaps', res.stats['gap'] == 4)
    res, _ = run([F(), T('R', '5', C01, 'cell=0,1'), T('R', '5', C10, 'cell=1,0')] + eng_cells)
    check('(a) track: cells equal', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', C01, 'cell=0,1'), T('R', '5', C10, 'cell=1,0')] + [
        T('E', '5', C10, 'cell=0,1'), T('E', '5', C01, 'cell=1,0')])
    check('(c) track: two cells swapped: cell deviations',
          feats(res) == ['cell', 'cell'] and sorted(r.pattern for r in res.open) == ['cell 0,1>1,0', 'cell 1,0>0,1'])
    res, _ = run([F(), T('R', '5', '0,0,15,0,0,15,15,15', 'stage=2'),
                  T('E', '5', '0,0,15,0,0,15,15,15', 'stage=1')])
    check('(b) track: stage', feats(res) == ['stage'] and res.open[0].pattern == 'stage 2>1')
    res, _ = run([F(), T('R', '5', C01, 'cell=0,1'), T('E', '5', C01, 'cell=0,1', clut='0,481')])
    check('(b) track: clut of a cell', feats(res) == ['clut'])
    res, _ = run([F(), T('R', '5', C01, 'cell=0,1'), T('E', '5', '8,0,15,0,8,8,15,8', 'cell=0,1')])
    check('(b) track: uv of a cell one texel off', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '5', C01, 'cell=0,1'), T('E', '5', '0,0,15,0,0,15,15,15', 'stage=2')])
    check('(b) track: cell absent in the engine', feats(res) == ['cell'] and res.open[0].pattern == 'cell 0,1 absent')
    res, _ = run([F('t-load'), T('R', '5', C00, 'cell=0,0'), T('R', '5', '0,0,15,0,0,15,15,15', 'stage=2')]
                 + eng_cells)
    check('track load block: engine cells without retail line are engine-only',
          feats(res, 'd2') == ['engine-only'] * 3 and res.stats['gap'] == 0)
    res, _ = run([F('t-load'), T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0'),
                  F('t1'), T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0')])
    check('track coverage: faces and cells', res.stats['coverage'].get('L3') == (1, 1, 1, 1))

    # (e) missing source, route=retail
    res, _ = run(base_r + [L('E', 'I', 'tikka#1', uv=TRI_A)])
    check('(e) missing: one missing-source record', feats(res) == ['missing-source'] and len(res.open) == 1)
    res, _ = run([F(), L('R', 'I', 'crate#0', uv=QUAD, extra='route=retail')])
    check('(e) route=retail: no deviation, listed',
          len(res.open) == 0 and res.stats['route_retail'] == {'L3': set(['I:crate'])})

    # (f) direction 2
    res, _ = run(base_r + eng_ok + [L('E', 'I', 'tikka#0', clut='0,500', uv='64,0,95,0,64,31')])
    check('(f) engine-only-material', feats(res, 'd2') == ['engine-only-material'] and not feats(res))
    res, _ = run(base_r + eng_ok + [L('E', 'I', 'crate#1', uv=TRI_A)])
    check('(f) model only the engine draws: information',
          len(res.open) == 0 and [r.object for r in res.info] == ['I:crate'])
    res, _ = run(base_r + eng_ok + [F('t2', frame=300), L('R', 'I', 'tikka#0', uv=TRI_C),
                                    L('E', 'I', 'tikka#0', uv=TRI_C)] + eng_ok)
    check('(f) engine tuples never used are counted per model',
          len(res.open) == 0 and res.stats['unused'][('L3', 'I:tikka')] == (3, 3))
    res, _ = run(base_r + eng_ok + [L('E', 'I', 'tikka#0', uv='64,0,95,0,64,31')])
    check('(f) unused engine triangle counted', res.stats['unused'][('L3', 'I:tikka')] == (3, 2))

    # (g) exceptions
    res, _ = run([F(), L('R', 'S', 'sky', uv=TRI_A), L('E', 'S', 'sky', clut='0,481', uv=TRI_A)])
    check('(g) exception covers sky and is counted',
          len(res.open) == 0 and len(res.covered) == 1 and [x.hits for x in rows if x.id == 'sky'] == [1])
    res, _ = run([F(), L('R', 'D', '0', uv=TRI_A), L('E', 'D', '0', page='-', clut='-', uv='-', extra='hires=1')])
    check('(g) hi-res driver covered', len(res.open) == 0 and len(res.covered) >= 1)
    res, _ = run([F(), L('R', 'D', '0', uv=TRI_A), L('E', 'D', '0', clut='0,481', uv=TRI_A)])
    check('(g) driver without hires=1 not covered', feats(res) == ['clut'])
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=TRI_A, extra='marker=1'), L('E', 'I', 'tikka#0', uv='0,1,31,1,0,32')])
    check('(g) row with feature uv and condition covers', len(res.open) == 0 and len(res.covered) == 1)
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=TRI_A, extra='marker=1'),
                  L('E', 'I', 'tikka#0', clut='16,480', uv='0,1,31,1,0,32')])
    check('(g) row does not cover clut+uv', feats(res) == ['clut+uv'])
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv='0,1,31,1,0,32')])
    check('(g) row does not cover when its condition is false', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '7', C00, 'cell=0,0'), T('E', '7', C00, 'cell=0,0', clut='0,481')])
    check('(g) documentation row covers nothing', feats(res) == ['clut'] and
          [x.hits for x in rows if x.id == 'doc'] == [0])

    # (h) controls and exit codes
    fault2 = [L('E', 'I', 'tikka#0', clut='0,490', uv=TRI_A, extra='fault=clut'),
              L('E', 'I', 'tikka#0', clut='0,490', uv=TRI_B, extra='fault=clut')]
    res, _ = run(base_r + fault2, expect=['fault:clut'])
    check('(h) control clut fired, exit 0', res.control.startswith('yes') and res.exit == 0)
    check('(h) control deviations counted apart', len(res.control_open) == 1 and not res.others)
    res, _ = run(base_r + [L('E', 'I', 'tikka#0', uv=TRI_A),
                           L('E', 'I', 'tikka#0', clut='0,490', uv=TRI_B, extra='fault=clut')])
    check('(h) one faulted triangle of a quad: clut, control', feats(res) == ['clut'] and not res.others)
    res, _ = run(base_r + fault2 + [F('t2', level=4), L('R', 'I', 'bush#0', uv=TRI_A),
                                    L('E', 'I', 'bush#0', uv=TRI_A, clut='0,481')])
    check('(h) other deviations: exit 1', res.exit == 1 and res.control.startswith('yes'))
    res, _ = run(base_r + eng_ok + [L('E', 'I', 'crate#0', clut='0,490', uv=TRI_A, extra='fault=clut')])
    check('(h) control on a model retail does not draw: exit 3', res.exit == 3 and 'FAILED' in res.control)
    res, _ = run([F(), L('R', 'S', 'sky', uv=TRI_A), L('E', 'S', 'sky', clut='0,490', uv=TRI_A, extra='fault=clut')])
    check('(h) control swallowed by an exception: exit 3', res.exit == 3 and 'covered by exception' in res.control)
    res, _ = run(base_r + eng_ok, expect=['fault:clut'])
    check('(h) expected control absent: exit 3', res.exit == 3)
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=QUAD), L('R', 'I', 'tikka#0', clut='0,481', uv='32,0,63,0,32,31'),
                  L('E', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv=TRI_B),
                  L('E', 'I', 'tikka#0', clut='0,481', uv='32,0,63,0,32,31'),
                  L('E', 'I', 'tikka#0', clut='0,481', uv=TRI_A, extra='fault=clut')])
    check('(h) faulted material on a drawn face that nothing reports: exit 3',
          res.exit == 3 and 'not reported' in res.control and len(res.control_missing) == 1)
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=QUAD), L('R', 'I', 'tikka#0', clut='0,481', uv='32,0,63,0,32,31'),
                  L('E', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv=TRI_B),
                  L('E', 'I', 'tikka#0', clut='0,481', uv='32,0,63,0,32,31'),
                  L('E', 'I', 'tikka#0', clut='0,481', uv=TRI_A, extra='fault=clut'),
                  L('R', 'I', 'bush#0', uv=TRI_A), L('E', 'I', 'bush#0', clut='0,490', uv=TRI_A, extra='fault=clut')])
    check('(h) one faulted material reported, another not: exit 3',
          res.exit == 3 and '1 of 2 faulted' in res.control and len(res.control_open) == 1)
    res, _ = run([F(), T('R', '5', C01, 'cell=0,1'), T('R', '5', C10, 'cell=1,0')] + [
        T('E', '5', C10, 'cell=0,1', extra='fault=cell'), T('E', '5', C01, 'cell=1,0', extra='fault=cell')],
        expect=['fault:cell'])
    check('(h) control cell fired', res.control.startswith('yes') and res.exit == 0 and len(res.control_open) == 2)

    # (i) animated texture: retail changes between frames, engine static
    res, _ = run([F('a', frame=10), L('R', 'I', 'tikka#0', uv=TRI_A, extra='anim=1'),
                  L('E', 'I', 'tikka#0', uv=TRI_A, extra='anim=1'),
                  F('b', frame=12), L('R', 'I', 'tikka#0', uv=TRI_A, clut='0,481', extra='anim=1'),
                  L('E', 'I', 'tikka#0', uv=TRI_A, extra='anim=1')])
    check('(i) anim: deviation with anim in front', feats(res) == ['anim+clut'])

    # (j) draw path and LOD header
    res, _ = run([F(level=4), L('R', 'I', 'flamejet#2', uv=TRI_A, extra='prim=lit col=160,140,40'),
                  L('E', 'I', 'flamejet#2', uv=TRI_A, extra='col=15,0,125')],
                 expect=['L4:I:flamejet:col'])
    check('(j) prim=lit: class draw path with the colour', feats(res) == ['prim+col'] and
          res.open[0].pattern == 'prim=lit col gap >128')
    check('(j) known positive fired', res.positives == [('L4', 'I:flamejet', 'col', 1)] and res.exit == 1)
    res, _ = run([F(), L('R', 'I', 'fruit#0', uv='0,0,7,0,0,7', extra='lod=1'),
                  L('R', 'I', 'fruit#0', uv='8,0,15,0,8,7', extra='lod=1'),
                  L('E', 'I', 'fruit#0', page='576,0,0', uv='0,0,31,0,0,31', extra='lod=0')])
    check('(j) lod=1: one LOD-header record, not compared',
          [(r.feature_label(), r.r_count, r.pattern) for r in res.open if r.kind == 'd1'] == [('lod', 2, 'lod 1>0')])
    check('(j) lod=1: engine material of header 0 not drawn at header 0 is lod too',
          feats(res, 'd2') == ['lod'])
    res, _ = run([F(level=4), L('R', 'I', 'flamejet#2', uv=TRI_A, extra='col=1,1,1'),
                  L('E', 'I', 'flamejet#2', uv=TRI_A, extra='col=1,1,1')], expect=['L4:I:flamejet:col'])
    check('(j) known positive missing: exit 3', res.exit == 3 and res.positives[0][3] == 0)

    # (k) sampling outside the window against a synthetic VRAM
    words = array.array('H', [0] * (1024 * 512))
    vr = Vram(words)

    def set_index(u, v, idx):   # 4-bit page at 896,256
        x, y = 896 + (u >> 2), 256 + v
        w = words[y * 1024 + x] & ~(15 << ((u & 3) * 4))
        words[y * 1024 + x] = w | (idx << ((u & 3) * 4))
    words[286 * 1024 + 801] = 0x7fff   # CLUT 800,286: entry 1 white, entry 2 red, entry 0 transparent
    words[286 * 1024 + 802] = 0x001f
    for v in range(228, 232):
        for u in range(252, 256):
            set_index(u, v, 1)
    BUV = '252,228,255,228,252,231,255,231'

    def banner(box, fault=''):
        ex = 'box=%s' % box + (',' + fault if fault else '')
        return [F(extra='vram=v.tga'), L('R', 'I', 'startbanner#0', page='896,256,0', clut='800,286', uv=BUV),
                L('E', 'I', 'startbanner#0', page='896,256,0', clut='800,286', uv='252,228,255,228,252,231', extra=ex),
                L('E', 'I', 'startbanner#0', page='896,256,0', clut='800,286', uv='255,228,255,231,252,231', extra=ex)]
    res, _ = run(banner('251,227,256,232'), vram=vr, expect=['L3:I:startbanner:sample-outside'])
    so = [r for r in res.open if r.features == ['sample-outside']]
    check('(k) box with a transparent ring: ring0 = 20 per triangle, wraps',
          len(so) == 2 and all('ring0 20 ringf 0 reach 20' in r.detail and r.pattern == 'ring0 wrap' for r in so))
    check('(k) known positive T2 fired', res.positives[0][3] == 2)
    res, _ = run(banner('252,228,255,231'), vram=vr)
    check('(k) box = triangle box: 0', len(res.open) == 0 and res.stats['box_checked'] == 2)
    for u in range(252, 256):
        set_index(u, 227, 2)
    res, _ = run(banner('252,227,255,231'), vram=vr)
    check('(k) foreign ring texels: ringf', [r.pattern for r in res.open] == ['ringf', 'ringf'])
    for u in range(252, 256):
        set_index(u, 227, 1)
    res, _ = run(banner('252,227,255,231'), vram=vr)
    check('(k) ring texels equal to the edge are harmless', len(res.open) == 0)
    res, _ = run(banner('251,227,256,232'))
    check('(k) no dump: box not checked, counted', len(res.open) == 0 and res.stats['box_unchecked'] == 2)
    # TGA reader: bottom-up rows, raw words
    body = array.array('H')
    for y in range(511, -1, -1):
        body.extend(words[y * 1024:(y + 1) * 1024])
    if sys.byteorder != 'little':
        body.byteswap()
    hdr = bytes([0, 0, 2] + [0] * 9 + [0, 4, 0, 2, 16, 0])
    vt = read_tga_vram(hdr + body.tobytes())
    check('(k) TGA reader: texel round trip', vt.texel((896, 256), 0, (800, 286), 253, 229) == 0x7fff and
          vt.texel((896, 256), 0, (800, 286), 250, 229) == 0)

    # plausibility
    res, _ = run(base_r + eng_ok, plaus=True)
    check('plausibility: no T lines and no dump fail', res.exit == 3 and any('T: no retail' in p for p in res.plausible)
          and any('VRAM' in p for p in res.plausible))
    res, _ = run([F(extra='vram=v.tga'), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv=TRI_A),
                  T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0')], plaus=True, vram=vr)
    check('plausibility: both sides of T and I with a dump: ok', res.plausible == [] and res.exit == 0)
    log = parse_log('\n'.join([F(), L('R', 'I', 'x#0', uv=TRI_A), L('E', 'I', 'x#0', uv=TRI_A)]) + '\n', 's')
    check('plausibility: missing H line', 'no H line' in ' '.join(plausibility(log, compare(log.frames)[1])))

    # classes across tracks
    res, _ = run([F('a', level=3), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', clut='16,480', uv=TRI_A),
                  F('b', level=4), L('R', 'I', 'tikka#2', uv=TRI_A), L('E', 'I', 'tikka#2', clut='16,480', uv=TRI_A),
                  F('c', level=5, cont=1), L('R', 'I', 'bush#0', uv=TRI_A), L('E', 'I', 'bush#0', clut='16,480', uv=TRI_A)])
    cl = [c for c in classes(res) if c[0][1] == 'clut']
    check('classes: one clut class over three tracks',
          len(cl) == 1 and cl[0][1]['n'] == 3 and sorted(cl[0][1]['tracks']) == ['L3', 'L4', 'L5c'])

    # log parsing
    for bad, why in (([F(), L('R', 'I', 'x#0', page='512,0')], 'bad page'),
                     ([L('R', 'I', 'x#0')], 'R before F'),
                     ([F(), L('R', 'Q', 'x#0')], 'unknown src'),
                     ([F(), L('R', 'I', 'x#0', blend='4')], 'bad blend'),
                     ([F(), L('R', 'I', 'x#0', clut='-')], '4-bit page without CLUT'),
                     ([F(), L('R', 'I', 'x#0', uv='-')], 'textured retail line without UV'),
                     ([F(), L('R', 'I', 'x#0', uv=TRI_A, extra='col=1,2,3;4,5,6')], 'col corners'),
                     ([F(), L('R', 'I', 'x#0', extra='box=5,5,4,4')], 'box')):
        check('parse: %s is an error' % why, len(parse_log('\n'.join(bad) + '\n', 's').errors) == 1)
    log = parse_log('\n'.join(['H\tabc\t--engine', '# comment', '', F(extra='vram=a-1.tga'),
                               L('R', 'I', 'x#0', extra='anim=1;lod=2 hdr col=1,2,3;4,5,6;7,8,9;1,1,1 tp=0x7e,box=1,2,3,4'),
                               L('E', 'I', 'x#0', uv=TRI_A).rsplit('\t', 1)[0]]) + '\r\n', 's')
    ln = log.frames[0].sources[('I', 'x#0')]['R'][0] if log.frames else None
    check('parse: H, comments, CRLF, extra values with separators, 9-field line',
          not log.errors and log.headers == ['abc --engine'] and log.frames[0].vram == 'a-1.tga' and ln is not None
          and ln.extra == {'anim': '1', 'lod': '2', 'hdr': '1', 'col': '1,2,3;4,5,6;7,8,9;1,1,1', 'tp': '0x7e',
                           'box': '1,2,3,4'} and ln.col[3] == (1, 1, 1) and ln.box == (1, 2, 3, 4))

    # exception lists
    with open(DEFAULT_EXCEPTIONS, encoding='utf-8') as fh:
        repo_rows, repo_errs = parse_exceptions(fh.read(), DEFAULT_EXCEPTIONS)
    ids = set(x.id for x in repo_rows)
    check('repo exception list parses with exactly the agreed rows',
          not repo_errs and ids == set(['sky-look', 'water-look', 'hires-driver', 'depth-offset', 'lod-header']) and
          [x.id for x in repo_rows if x.doc] == ['depth-offset'])
    res, _ = run([F(), L('R', 'D', '0', uv=TRI_A), L('E', 'D', '0', page='-', clut='-', uv='-', extra='hires=1'),
                  L('R', 'W', 'water', uv=TRI_A), L('E', 'W', 'water', clut='0,481', uv=TRI_A),
                  L('R', 'S', 'sky', page='-', clut='-', uv='-', extra='col=1,2,3'),
                  L('E', 'S', 'sky', page='-', clut='-', uv='-', extra='col=9,9,9'),
                  L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', clut='0,481', uv=TRI_A)], rws=repo_rows)
    check('repo exception list: hi-res, water and sky covered, instance not',
          feats(res) == ['clut'] and sorted(set(r.excepted for r in res.covered)) ==
          ['hires-driver', 'sky-look', 'water-look'])
    for bad, why in (('id\tsrc\tfeature\tcondition\treason\nx\tI\tcolour\t-\tsome reason that is long enough', 'feature'),
                     ('id\tsrc\tfeature\tcondition\treason\nx\tI\tclut\t-\tshort', 'reason'),
                     ('id\tsrc\tfeature\tcondition\treason\nx\tI\tclut\tr.anim>1\tsome reason that is long enough', 'term'),
                     ('id\tsrc\tfeature\tcondition\treason\nx\tQ\tclut\t-\tsome reason that is long enough', 'src'),
                     ('src\tfeature\tcondition\treason', 'header')):
        _, er = parse_exceptions(bad, 'bad')
        check('bad exception list rejected (%s)' % why, len(er) >= 1)
    check('--expect syntax', parse_expect(['fault:clut', 'L3:I:startbanner:sample-outside', 'L3:startbanner'])[2] != []
          and parse_expect(['fault:clut', 'L3:I:startbanner:sample-outside'])[1] == [('L3', 'I:startbanner', 'sample-outside')])

    # output
    res, _ = run(base_r + fault2 + [L('E', 'I', 'crate#0', uv=TRI_A)], expect=['fault:clut'])
    md = render_md(res, ['synthetic'], 'synthetic')
    body_lines = [l for l in md.splitlines() if l.strip()]
    check('markdown: control line right after the title', body_lines[0] == '# Material comparison' and
          body_lines[1].startswith('Control fired: yes'))
    check('markdown: total line', 'Deviations outside the exception list and the controls: 0' in md)
    tsv = render_tsv(res).splitlines()
    check('tsv: header and one row per record',
          tsv[0].split('\t') == list(TSV_COLUMNS) and len(tsv) == 1 + len(res.records) and
          all(len(l.split('\t')) == len(TSV_COLUMNS) for l in tsv))
    check('tsv: control marked', sum(1 for l in tsv[1:] if l.split('\t')[3] == 'clut') == 1)
    check('classes tsv: header plus classes', len(render_classes_tsv(res).splitlines()) == 1 + len(classes(res)))

    if fails:
        print('material_compare selftest FAILED: %d checks, %d failures' % (checks[0], len(fails)))
        return 1
    print('material_compare selftest passed: %d checks, 0 failures' % checks[0])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
