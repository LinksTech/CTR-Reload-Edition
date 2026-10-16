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
    corner colours. A triangle is a set of (UV, colour) corners; one found
    only with the other winding is counted apart (the import lists one-sided
    triangles in the engine's cull order; --strict-winding compares the
    winding). A quad is two triangles of either diagonal. Pieces of
    func=split|reflection|special (cut at the water or mirror line): material
    exact, UV corners inside the corner box of an engine triangle, their
    colour outside the colour range of every such triangle (the engine draws
    a triangle once per side tint) is split-col / mirror-col. World
    particles (X particle-...): a quad like a model face (two engine
    triangles), a line (prim=line) untextured by blend and its colour set.
    A miss is classified by the features in which the nearest engine
    triangle differs. Retail lines with a header (lod) the engine does not
    draw are counted as class lod, not compared. prim other than normal
    (a retail draw path the engine does not rebuild) and anim mark their
    misses (features prim / anim in front). A source the engine does not
    draw at all: missing-source (route=retail: listed, no deviation).
  Track (T), per frame and face (srcId): a retail line with stage or cell
    must equal the engine entry of the same face with the same stage or
    cell: material exact, corner k of the retail line on slot k of the
    engine line (extra slots=<s>,... or ord=slot; a GT3 fills 3 of the 4
    slots; an engine line with four corners is in slot order), colours per
    slot. Retail lines without a slot marker fall back to a corner set (its
    corners a subset of the engine corners, colours paired by UV point; the
    orientation inside the face is then not provable) and are counted.
    Found under another stage or cell of the face: feature stage or
    cell. In the load block every engine stage and cell needs a retail
    line (else engine-only); in race frames engine entries without a
    retail line are coverage gaps (counted). Each set (extra set=: none =
    drawn this frame, load, anim = all AnimTex faces) is compared on its
    own; engine entries of set=anim without a retail line are counted.
    Markers: part=1 (a piece: clip or subdivision) - page, depth, CLUT and
    blend exact against its key, every UV corner inside the integer corner
    box of the key's engine line; stage=none - own class stage-none;
    lodonly=1 / noleaf=1 without an engine line of the same key -
    information, with one compared; nogeo=1 - counted only; clip=1 -
    counted and compared.
  Direction 2 (models, per track and model name): an engine material that
    retail never draws for that model is engine-only-material when retail
    drew one of its triangles (UV set) with another material; on triangles
    retail never drew it is coverage, and so is a material of back-side
    copies only (engine lines side=back: a two-sided triangle drawn as two
    one-sided ones) while retail never showed that model from the back
    (retail lines side=back). When retail
    drew the model only with another header, the material is listed as
    "header 0 never compared" (information). Engine triangles no frame uses
    are counted as unused per model.
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
    with the target feature (clut -> clut, cell -> cell; other deviations
    on fault lines stay deviations, listed as strays) and every faulted
    unit retail draws (a model material, a track face) is reported.
  --expect fault:<kind> requires that control; --expect TRACK:SRC:MODEL:FEATURE
    (e.g. L3:I:startbanner:sample-outside) requires a known positive.
  Plausibility: an H line; per track both sides of T and I in its race
    frames (load block and set=anim lines do not count; route=retail
    sources need no engine side; the menu scene, a frame tag with 'menu',
    needs none); a readable VRAM dump for every race frame. Source kinds
    with one side only (effects, native drivers, sky, water) are listed.

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
            'stage-none', 'split-col', 'mirror-col', 'missing-source', 'engine-only-material', 'engine-only',
            'sample-outside')
# Retail draw functions that cut model triangles at the water or mirror line
# and write pieces with interpolated UV and the colour of their side.
PIECE_FUNCS = {'split': 'split-col', 'reflection': 'mirror-col', 'special': 'mirror-col'}
# Markers of the track lines (track oracle): retail part (a piece), clip (from a clip
# record), nogeo (a mosaic piece without a place), lodonly (a level retail does
# not draw by default), noleaf (a block the walk never reaches), sieve; engine
# frozen (an AnimTex face that does not follow its cell), tablefail.
FLAG_KEYS = ('part', 'clip', 'nogeo', 'lodonly', 'noleaf', 'sieve', 'frozen', 'tablefail', 'degenerate',
             'nolayout', 'dropped', 'blank', 'zeroarea', 'wide', 'foreign')
# Retail track lines without an engine line of their key that are information:
# in every set a level retail does not draw by default (lodonly), a block in
# no leaf (noleaf), a table of null entries (blank), a packet without area
# (zeroarea); in the load block also a degenerate face, a face without layout
# and a dropped layout.
INFO_FLAGS_ANY = ('lodonly', 'noleaf', 'blank', 'zeroarea')
INFO_FLAGS_LOAD = ('degenerate', 'nolayout', 'dropped')
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
# Corner colours of track tile packets (cell lines, slots on both sides) are
# computed by the oracle's model of the walk's colour averaging; a difference
# of at most this is information (col-model, counted), above it stays col.
# Stage lines and the colours of the plain model writer keep COL_TOL_NORMAL.
COL_MODEL_GAP = 2
# Colours of pieces cut at the water or mirror line are interpolated by the
# retail cut; outside the triangle's colour range by at most this they are
# information (counted), above it split-col / mirror-col.
COL_CUT_GAP = 2
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
                 'key', 'ncorner', 'set', 'flags', 'colbad', 'slots', 'rawcol', 'colw')

    def mat(self):
        return (self.page, self.depth, self.clut, self.blend)

    def tuple_key(self):
        return (self.mat(), self.uv, self.col, self.box, self.lod, self.key, self.anim, self.prim,
                self.flags)


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
    # raw texture (raw=1, the raw bit of the packet): the PS1 GPU does not
    # modulate the texture, the colour bytes do not count - not compared
    ln.rawcol = ln.side == 'R' and ln.col is not None and ln.extra.get('raw', '0') not in ('0', '')
    if ln.rawcol:
        ln.col = None
    ln.box = parse_box(ln.extra.get('box'))
    ln.lod = ln.extra.get('lod')
    ln.stage = ln.extra.get('stage')
    ln.prim = ln.extra.get('prim')
    ln.route = ln.extra.get('route')
    ln.set = ln.extra.get('set', '-')
    ln.flags = frozenset(k for k in FLAG_KEYS if ln.extra.get(k, '0') not in ('0', ''))
    # track corners per slot of the face (0 (0,0), 1 (1,0), 2 (0,1), 3 (1,1)):
    # slots=<s>,<s>,... names the slot of every listed corner (a GT3 three of
    # them), ord=slot says the corners are listed in slot order; an engine
    # track line with four corners and no marker is in slot order
    ln.slots = None
    if ln.uv is not None and ln.src == 'T':
        if 'slots' in ln.extra:
            sl = _ints(ln.extra['slots'], (len(ln.uv),), 'slots')
            if len(set(sl)) != len(sl) or not all(0 <= x <= 3 for x in sl):
                raise LogError('bad slots %r' % ln.extra['slots'])
            ln.slots = tuple(sl)
        elif ln.extra.get('ord') == 'slot' or (ln.side == 'E' and len(ln.uv) == 4):
            ln.slots = tuple(range(len(ln.uv)))
    if ln.side == 'R' and 'foreign' in ln.extra:
        # a stage line laid on its owner face from another mid face (foreign=<place>):
        # its corners are compared as a set
        ln.slots = None
    ln.ncorner = len(ln.uv) if ln.uv is not None else (len(ln.col) if ln.col and len(ln.col) in (3, 4) else 3)
    # colw= (engine cell lines): the colours of the cell under the wide frame rule
    ln.colw = parse_col(ln.extra.get('colw'))
    if ln.colw is not None and len(ln.colw) not in (1, ln.ncorner):
        ln.colw = None
    ln.colbad = ln.col is not None and len(ln.col) not in (1, ln.ncorner)
    if ln.colbad:
        # colours that cannot be paired with the corners (e.g. three colours on
        # a quad): not compared, counted
        ln.col = None
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
        self.section = None
        self.sources = {}   # (src, srcId) -> {'R': [...], 'E': [...]}

    def slot(self):
        return 'L%s%s' % (self.level, 'c' if self.container not in ('0', '') else '')

    def track(self):
        return self.section or self.slot()


_SECTION_TAG = re.compile(r'^(t\d+-L\d+)(?:-|$)')


def assign_sections(frames):
    """The track of a frame is its section of the log: a load block (tag
    ...-load) starts a new section, the race frames after it belong to it.
    Containers play in the slot of their donor level, so the level id alone
    does not name a track. Name = tNN-L<id> of the first race frame tag of
    the section (the tour entry), else the level slot (L<id>, c for a
    container). A race frame with another tNN-L<id> prefix than its section,
    or without one after a named section, starts a new section too."""
    cur, name, members = None, None, []

    def close():
        for f in members:
            f.section = name or members[0].slot()
    for fr in frames:
        m = _SECTION_TAG.match(fr.tag)
        pre = m.group(1) if m else None
        races = [f for f in members if not f.load]
        if fr.load or cur is None or (name is not None and pre != name) or \
                (name is None and races and (pre is not None or fr.slot() != races[-1].slot())):
            if members:
                close()
            cur, name, members = fr, None, []
        if not fr.load and name is None and pre is not None:
            name = pre
        members.append(fr)
    if members:
        close()


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
    __slots__ = ('e', 'mat', 'uvkey', 'cols', 'lod', 'key', 'fault', 'corners', 'skey')


def _uvs(p):
    return p if p is not None else (-1, -1)


def _pair_cols_ok(rc, tcorners, ctol):
    """Colours paired by UV point (a corner keeps its UV and colour whatever
    the order of the corners)."""
    for uv, c in rc:
        if c is None:
            continue
        cands = [tc for tu, tc in tcorners if tu == uv]
        if not cands or any(x is None for x in cands):
            continue
        if not any(max(abs(p - q) for p, q in zip(c, x)) <= ctol for x in cands):
            return False
    return True


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
        self.tris, self.by_uv, self.by_set, self.unknown = [], {}, {}, {}
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
                t.corners = rc
                t.skey = tuple(sorted(t.uvkey, key=_uvs))
                self.tris.append(t)
                self.by_uv.setdefault((t.mat, t.uvkey, t.lod, t.key), []).append(t)
                self.by_set.setdefault((t.mat, t.skey, t.lod, t.key), []).append(t)

    def match(self, corners, mat, lod, key, ctol):
        rc = _rot(corners)
        uvkey = tuple(x[0] for x in rc)
        cols = tuple(x[1] for x in rc)
        for t in self.by_uv.get((mat, uvkey, lod, key), ()):
            if _cols_ok(cols, t.cols, ctol):
                return t
        u = self.unknown.get((mat, lod, key))
        return u[0] if u else None

    def match_set(self, corners, mat, lod, key, ctol):
        """The triangle as a set of (UV, colour) corners: the winding is not
        compared (the import lists one-sided triangles in the engine's cull
        order, every corner keeps its position, UV and colour)."""
        skey = tuple(sorted((x[0] for x in corners), key=_uvs))
        for t in self.by_set.get((mat, skey, lod, key), ()):
            if _pair_cols_ok(corners, t.corners, ctol):
                return t
        return None

    def keys(self):
        return set(e.key for e in self.lines)


def _tri_diffs(mat, rc, t, ctol, strict=False):
    d = []
    for name, i in (('page', 0), ('depth', 1), ('clut', 2)):
        if mat[i] != t.mat[i]:
            d.append(name)
    uvkey = tuple(x[0] for x in rc)
    same = (uvkey == t.uvkey) if strict else (tuple(sorted(uvkey, key=_uvs)) == t.skey)
    if not same:
        d.append('uv')
    if mat[3] != t.mat[3]:
        d.append('blend')
    if same and not (_cols_ok(tuple(x[1] for x in rc), t.cols, ctol) if strict
                     else _pair_cols_ok(rc, t.corners, ctol)):
        d.append('col')
    return d


def _uv_dist(rc, t):
    a = [x[0] for x in rc]
    if any(p is None for p in a) or any(p is None for p in t.uvkey):
        return 0 if all(p is None for p in a) and all(p is None for p in t.uvkey) else 100000
    return sum(abs(p[0] - q[0]) + abs(p[1] - q[1]) for p, q in zip(a, t.uvkey))


def nearest_tri(corners, mat, pool, ctol, strict=False):
    rc = _rot(corners)
    best = None
    for t in pool:
        d = _tri_diffs(mat, rc, t, ctol, strict)
        k = (len(d), _uv_dist(rc, t), 0 if t.fault else 1, t.e.lineno)
        if best is None or k < best[0]:
            best = (k, d, t)
    return (best[1], best[2]) if best else (None, None)


def match_line(r, es, lod, key, ctol, pool, strict=False):
    """Matches a retail line against the engine set. Returns (used engine
    lines, None, n) when drawn - n = triangles found only with the other
    winding - else (used, (features, engine line, fault, winding), 0).
    strict: the winding is compared (a reversed one is uv)."""
    used, best = [], None
    mat = r.mat()
    for opt in line_options(r):
        fails, got, nrev = [], [], 0
        for corners in opt:
            t = es.match(corners, mat, lod, key, ctol)
            if t is None and not strict:
                t = es.match_set(corners, mat, lod, key, ctol)
                nrev += 1 if t is not None else 0
            if t is not None:
                got.append(t.e if isinstance(t, Tri) else t)
            else:
                fails.append(corners)
        used += got
        if not fails:
            return used, None, nrev
        feats, eng, fault, wind = set(), None, None, False
        cost = 0
        for corners in fails:
            rev = [corners[0], corners[2], corners[1]]
            t = es.match(rev, mat, lod, key, ctol) if strict else None
            if isinstance(t, Tri):
                feats.add('uv')
                wind = True
                cost += 1
                cand = t
            else:
                d, cand = nearest_tri(corners, mat, pool, ctol, strict)
                if cand is None:
                    continue
                d = d or ['uv']
                feats.update(d)
                cost += len(d) + (2 if 'uv' in d else 0)   # a wrong diagonal weighs more
            if eng is None or (cand.fault and not fault):
                eng, fault = cand.e, cand.fault
        if best is None or cost < best[0]:
            best = (cost, [f for f in FEATURES if f in feats], eng, fault, wind)
    return used, best[1:], 0


# ------------------------------------------------------------------ colours

def col_gap(r, e):
    """Largest channel difference between the two colour sets (for the class pattern)."""
    if r is None or e is None or r.col is None or e.col is None:
        return None
    return max(min(max(abs(x - y) for x, y in zip(a, b)) for b in r.col) for a in e.col)


# Retail writers that copy their colour unchanged (no GTE step): the plain
# model writer and the world particles (quad and line).
PLAIN_PRIMS = (None, 'normal', 'particle', 'line')


def col_tol(r, gte):
    return COL_TOL_NORMAL if r.prim in PLAIN_PRIMS else gte


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


def _flags(r, e):
    """Track markers that split a class (retail piece kinds, engine frozen faces)."""
    out = []
    if r is not None:
        out += [k for k in ('part', 'clip', 'sieve', 'lodonly', 'noleaf') if k in r.flags]
    if e is not None:
        out += [k for k in ('frozen', 'tablefail') if k in e.flags]
    return (' ' + ' '.join(out)) if out else ''


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
        elif f in ('col', 'split-col', 'mirror-col'):
            parts.append('%s gap %s' % (f, _gap_bucket(col_gap(r, e))))
    return (' '.join(parts) or '-') + _flags(r, e)


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
    return {'frames': 0, 'load_frames': 0, 'R': 0, 'E': 0, 'compared': 0, 'gap': 0, 'anim_gap': 0,
            'fault_lines': 0, 'tracks': set(), 'fault_units': [], 'engine_no_uv': 0,
            'r_no_col': 0, 'e_no_col': 0, 'box_checked': 0, 'box_unchecked': 0, 'e_no_box': 0,
            'vram_frames': 0, 'vram_missing': [], 'route_retail': {}, 'lod_lines': {},
            'per': {}, 'unused': {}, 'coverage': {}, 'prim_equal': 0, 'menu_tracks': set(),
            'part': {}, 'clip': {}, 'nogeo': {}, 'info_lines': {}, 'stage_none': {}, 'lod_never': set(),
            'col_unpaired': 0, 'winding': 0, 'piece': {}, 'eom_coverage': [], 'fault_orient': set(),
            't_set_rule': {}, 'back_coverage': [], 'src_cell': {}, 'col_model': {}, 'raw_col': 0,
            'tablefail': {}, 'anim_degenerate': {}, 'foreign': {}, 'stage_hit': {}, 'cut_col': {}}


def _per(stats, tr, src):
    return stats['per'].setdefault((tr, src), {'R': 0, 'E': 0, 'Rrace': 0, 'Erace': 0, 'compared': 0})


def _bump(d, k, n=1):
    d[k] = d.get(k, 0) + n


def _inside(r, e):
    """The UV corners of the retail piece lie in the integer corner box of
    the engine line (inclusive, tolerance 0); of a four-corner piece one
    corner may lie outside (the free fourth corner of a layout)."""
    if r.uv is None:
        return e.uv is None
    if e.uv is None:
        return False
    u0, v0, u1, v1 = bbox(e.uv)
    n = sum(1 for u, v in r.uv if u0 <= u <= u1 and v0 <= v <= v1)
    return n == len(r.uv) or (len(r.uv) == 4 and n >= 3)


def _in_box(points, corners):
    u0, v0, u1, v1 = bbox(corners)
    return all(u0 <= u <= u1 and v0 <= v <= v1 for u, v in points)


def _range_gap(cols, ref):
    """How far (largest channel step) colours lie outside the channel range
    of ref (0 inside); for naming the nearest engine triangle."""
    ref = [c for c in ref if c is not None]
    if not cols or not ref:
        return 0
    lo = [min(c[i] for c in ref) for i in range(3)]
    hi = [max(c[i] for c in ref) for i in range(3)]
    return max(max(lo[i] - c[i], c[i] - hi[i], 0) for c in cols for i in range(3))


def _cols_in_range(cols, ref, ctol):
    lo = [min(c[i] for c in ref) - ctol for i in range(3)]
    hi = [max(c[i] for c in ref) + ctol for i in range(3)]
    return all(lo[i] <= c[i] <= hi[i] for c in cols for i in range(3))


def _slot_map(ln):
    return dict(zip(ln.slots, _corners(ln)))


def _t_corners_ok(r, e):
    """Ordered per slot when both lines name their slots (corner k of the
    retail line on slot k of the engine line; a GT3 fills 3 of 4), else the
    retail corners a subset of the engine corners (fallback, orientation not
    provable)."""
    if r.uv is None or e.uv is None:
        return r.uv is None and e.uv is None
    if r.slots is not None and e.slots is not None:
        return all(a[0] == b[0] for a, b in _common_slots(r, e))
    return set(r.uv) <= set(e.uv)


def _common_slots(r, e):
    """(retail corner, engine corner) per slot of the line with fewer slots
    (a GT3 against a quad, a quad layout against a triangle face of the
    engine, slots=0,1,2); a slot the other line lacks pairs with None."""
    rm, em = _slot_map(r), _slot_map(e)
    few = rm if len(rm) <= len(em) else em
    return [(rm.get(k, (None, None)), em.get(k, (None, None))) if k in rm and k in em
            else ((None, None), (-1, None)) for k in few]


def _tcol_view(r, e):
    """The engine line whose colours a retail line is compared with: for a
    tile of a wide frame block (wide=1) the engine's colw= when it has one."""
    if 'wide' not in r.flags or e.colw is None:
        return e
    v = Line()
    for k in Line.__slots__:
        setattr(v, k, getattr(e, k, None))
    v.col = e.colw
    return v


def _t_diffs(r, e, ctol):
    """Track line against an engine line of a face: material exact, the UV
    corners per slot (fallback: as a subset), colours paired by slot (fallback:
    by UV point)."""
    d = _mat_diffs(r, e)
    e = _tcol_view(r, e)
    if not _t_corners_ok(r, e):
        d.append('uv')
    elif r.uv is not None and r.col is not None and e.col is not None:
        if r.slots is not None and e.slots is not None:
            ok = all(a[1] is None or b[1] is None or max(abs(p - q) for p, q in zip(a[1], b[1])) <= ctol
                     for a, b in _common_slots(r, e))
        else:
            ok = _pair_cols_ok(_corners(r), _corners(e), ctol)
        if not ok:
            d.append('col')
    return [f for f in FEATURES if f in d]


def _mat_diffs(r, e):
    return [n for n, i in (('page', 0), ('depth', 1), ('clut', 2), ('blend', 3)) if r.mat()[i] != e.mat()[i]]


def _groups(src, rl, el):
    """Track lines split by their set (- = drawn this frame, load, anim):
    each set is compared on its own."""
    if src != 'T':
        return [('-', rl, el)]
    out = {}
    for ln in rl:
        out.setdefault(ln.set, ([], []))[0].append(ln)
    for ln in el:
        out.setdefault(ln.set, ([], []))[1].append(ln)
    return [(k, v[0], v[1]) for k, v in sorted(out.items())]


def unit_of(rec):
    """What a planted fault is checked by: a model material, or a track face."""
    if rec.src == 'T':
        return (rec.track, rec.object)
    return (rec.track, rec.object, rec.e.mat() if rec.e is not None else None)


def compare(frames, gte_tol=COL_TOL_GTE, vram_for=None, strict_winding=False):
    """Returns (records, stats). vram_for(frame) gives the frame's Vram or None."""
    records, stats = [], _new_stats()
    stats['frames'] = len(frames)
    assign_sections(frames)
    # cross references over the whole log: track faces the load block marks
    # degenerate, and faces with retail lines drawn in race frames
    degen_faces, drawn_faces = set(), set()
    for fr in frames:
        for (src, sid), sides in fr.sources.items():
            if src != 'T':
                continue
            for r in sides['R']:
                if (fr.load or r.set == 'load') and 'degenerate' in r.flags:
                    degen_faces.add((fr.track(), sid))
                elif not fr.load and r.set == '-':
                    drawn_faces.add((fr.track(), sid))
    per_track = {}            # track -> (src, model) -> object data (models)
    tcov = {}                 # track -> coverage sets (track faces)
    visible = set()           # (track, object, material) of model fault lines on faces retail draws
    entry_cell = {}           # (track, face, table entry) -> cell, from the retail load block

    def rec(**kw):
        records.append(Record(**kw))

    for fr in frames:
        tr = fr.track()
        stats['tracks'].add(tr)
        if 'menu' in fr.tag.lower():
            stats['menu_tracks'].add(tr)
        if fr.load:
            stats['load_frames'] += 1
        objs = per_track.setdefault(tr, {})
        cov = tcov.setdefault(tr, {'load_faces': set(), 'load_cells': set(), 'seen_faces': set(),
                                   'seen_cells': set(), 'mir_faces': set()})
        vram = vram_for(fr) if (vram_for is not None and fr.vram) else None
        if vram is not None:
            stats['vram_frames'] += 1
        elif not fr.load:
            stats['vram_missing'].append(fr.tag)
        for (src, sid), sides in sorted(fr.sources.items()):
            obj = '%s:%s' % (src, object_key(src, sid))
            for setname, rl, el in _groups(src, sides['R'], sides['E']):
                load = fr.load or setname == 'load'
                race = not fr.load and setname == '-'
                if src == 'T':
                    if load:
                        # the cell of every table entry of the face (retail load block)
                        for r in rl:
                            if 'entry' in r.extra and r.cell != '-':
                                entry_cell[(tr, sid, r.extra['entry'])] = r.cell
                    else:
                        # a tile without slots that names the table entry it was read from
                        # (src=<e>): its cell comes from that entry, not from where it lies
                        for r in rl:
                            if r.key is not None and r.key[0] == 'cell' and r.slots is None and 'src' in r.extra:
                                c = entry_cell.get((tr, sid, r.extra['src']))
                                if c is not None:
                                    r.cell, r.key = c, ('cell', c)
                                    _bump(stats['src_cell'], tr)
                p = _per(stats, tr, src)
                p['R'] += len(rl)
                p['E'] += len(el)
                if race:
                    p['Rrace'] += len(rl)
                    p['Erace'] += len(el)
                stats['R'] += len(rl)
                stats['E'] += len(el)
                stats['fault_lines'] += sum(1 for e in el if e.fault)
                stats['engine_no_uv'] += sum(1 for e in el if e.page is not None and e.uv is None)
                stats['r_no_col'] += sum(1 for r in rl if r.col is None)
                stats['e_no_col'] += sum(1 for e in el if e.col is None)
                stats['col_unpaired'] += sum(1 for x in rl + el if x.colbad)
                stats['raw_col'] += sum(1 for x in rl if x.rawcol)
                stats['e_no_box'] += sum(1 for e in el if e.box is None and e.page is not None)
                if src == 'T':
                    if load:
                        cov['load_faces'].update(sid for e in el)
                        cov['load_cells'].update((sid, e.cell) for e in el if e.cell != '-')
                        cov['mir_faces'].update(sid for r in rl if r.extra.get('mir', '0') not in ('0', ''))
                    elif race:
                        cov['seen_faces'].update(sid for r in rl)
                        cov['seen_cells'].update((sid, r.cell) for r in rl if r.cell != '-')
                    for kind in sorted(set(e.fault for e in el if e.fault)):
                        # a planted fault is checkable on a face when a faulted line differs
                        # from the retail line of its key as a corner set or a material; two
                        # swapped cells that differ only in orientation cannot be seen in UV
                        # corner sets (the named residuum) and are counted apart
                        rby = {}
                        for r in rl:
                            rby.setdefault(r.key, []).append(r)
                        seen = any(e.fault == kind and e.key in rby and
                                   all(_mat_diffs(r, e) or not _t_corners_ok(r, e) for r in rby[e.key])
                                   for e in el)
                        if rl and not seen:
                            stats['fault_orient'].add((kind, (tr, obj)))
                        stats['fault_units'].append((kind, (tr, obj), bool(rl) and seen))
                if any(r.route == 'retail' for r in rl):
                    stats['route_retail'].setdefault(tr, set()).add(obj)
                    continue
                es = EngineSet(el)
                ob = None
                if src != 'T':
                    ob = objs.setdefault((src, object_key(src, sid)),
                                         {'R': set(), 'E': {}, 'srcId': sid, 'rlod': set(), 'seen': set(),
                                          'used': set(), 'ruv': set(), 'rback': False, 'rtri': {}, 'etri': {}})
                    for e in es.lines:
                        ent = ob['E'].setdefault(e.mat(), {'e': e, 'frames': set(), 'fault': None,
                                                           'lod': set(), 'front': False})
                        if e.extra.get('side') != 'back':
                            ent['front'] = True
                        ent['frames'].add(id(fr))
                        ent['lod'].add(e.lod or '0')
                        if e.fault:
                            ent['fault'], ent['e'] = e.fault, e
                        ob['seen'].add(e.tuple_key())
                    for t in es.tris:
                        ob['E'][t.mat].setdefault('uvs', set()).add(t.skey)
                        ob['etri'].setdefault(t.skey, set()).add(t.mat)
                # retail lines, one per distinct tuple
                ret = {}
                for r in rl:
                    k = r.tuple_key()
                    if k in ret:
                        ret[k][1] += 1
                    else:
                        ret[k] = [r, 1]
                # a face whose table the engine refused (tablefail=1, retail reads the
                # level-3 word as a table pointer): its load-block table entries and the
                # engine lines without a retail partner are information (K-ADR/tablefail)
                tfail = src == 'T' and load and any('tablefail' in x.flags for x in rl + el)
                # track: engine entries without a retail line
                if src == 'T':
                    rk = set(r.key for r in rl)
                    for e in es.lines:
                        if e.key in rk:
                            continue
                        if tfail:
                            _bump(stats['tablefail'], tr)
                        elif load:
                            rec(kind='d2', frame=fr, track=tr, src=src, srcId=sid, features=['engine-only'],
                                e=e, fault=e.fault, pattern='%s %s' % e.key + _flags(None, e))
                        elif setname == 'anim':
                            stats['anim_gap'] += 1
                        else:
                            stats['gap'] += 1
                # what is compared: track lines without a place are counted only, levels retail
                # does not draw by default and blocks in no leaf are information without a partner
                work = []
                keys = es.keys()
                for r, count in ret.values():
                    if src == 'T':
                        if tfail and 'entry' in r.extra:
                            _bump(stats['tablefail'], tr, count)
                            continue
                        if 'zeroarea' in r.flags:
                            # a packet without area draws nothing: information, never uv
                            _bump(stats['info_lines'], (tr, 'zeroarea'), count)
                            continue
                        if 'nogeo' in r.flags:
                            _bump(stats['nogeo'], tr, count)
                            continue
                        if 'clip' in r.flags:
                            _bump(stats['clip'], tr, count)
                        why = [k for k in INFO_FLAGS_ANY + (INFO_FLAGS_LOAD if load else ()) if k in r.flags]
                        if why and r.key not in keys:
                            _bump(stats['info_lines'], (tr, why[0]), count)
                            continue
                    work.append((r, count))
                if not work:
                    continue
                if not es.lines:
                    if src == 'T' and setname == 'anim' and (tr, sid) in degen_faces and (tr, sid) not in drawn_faces:
                        # an AnimTex face the load block marks degenerate and no race frame
                        # draws: information
                        _bump(stats['anim_degenerate'], tr, sum(c for _, c in work))
                        continue
                    rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=['missing-source'],
                        r=work[0][0], r_count=sum(c for _, c in work))
                    continue
                # model fault lines on faces retail draws (for the control rule)
                if src != 'T':
                    rkeys = set()
                    for r, _ in work:
                        for opt in line_options(r):
                            for c in opt:
                                rc = _rot(c)
                                rkeys.add((r.page, r.depth, r.blend, tuple(x[0] for x in rc)))
                    for t in es.tris:
                        if t.fault and (t.mat[0], t.mat[1], t.mat[3], t.uvkey) in rkeys:
                            visible.add((tr, obj, t.mat))
                used_by = {}
                lod_count = {}
                for r, count in work:
                    rlod = r.lod or '0'
                    if ob is not None:
                        ob['R'].add(r.mat())
                        if r.extra.get('side') == 'back':
                            ob['rback'] = True
                        for opt in line_options(r):
                            for c in opt:
                                tk = tuple(sorted((x[0] for x in c), key=_uvs))
                                ob['ruv'].add(tk)
                                ob['rtri'].setdefault(tk, set()).add(r.mat())
                        ob['rlod'].add(rlod)
                    if src != 'T' and rlod not in es.lods:
                        lod_count[rlod] = lod_count.get(rlod, 0) + count
                        continue
                    p['compared'] += count
                    stats['compared'] += count
                    ctol = col_tol(r, gte_tol)
                    pre = []
                    if r.prim not in PLAIN_PRIMS:
                        pre.append('prim')
                    if r.prim == 'line':
                        # a world particle line: untextured, compared by blend and its colour set
                        # {c0, c1} against the corner colours of the engine's untextured line quad
                        cand = [e for e in es.lines if e.page is None]
                        same = [e for e in cand if e.blend == r.blend]
                        rc = set(r.col or ())
                        ec = set(c for e in same for c in (e.col or ()))

                        def near(a, b):
                            return max(abs(x - y) for x, y in zip(a, b)) <= ctol
                        if same and (not rc or not ec or (all(any(near(c, x) for x in ec) for c in rc) and
                                                          all(any(near(x, c) for c in rc) for x in ec))):
                            for e in same:
                                used_by.setdefault(id(e), (e, r))
                            continue
                        eng = (same or cand or es.lines)[0]
                        feats = ['col'] if same else (['blend'] if cand else _mat_diffs(r, eng) or ['uv'])
                        rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=feats, r=r, e=eng,
                            r_count=count, fault=eng.fault)
                        continue
                    if src == 'T' and r.stage == 'none':
                        _bump(stats['stage_none'], tr, count)
                        rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=['stage-none'], r=r,
                            r_count=count, pattern='stage none' + _flags(r, None))
                        continue
                    if src == 'T' and 'part' in r.flags:
                        # a piece (near-plane clip, subdivision without mosaic): page, depth, CLUT
                        # and blend exact against its key, every UV corner inside the key's box
                        _bump(stats['part'], tr, count)
                        same = [e for e in es.lines if e.key == r.key]
                        hit = next((e for e in same if not _mat_diffs(r, e) and _inside(r, e)), None)
                        if hit is not None:
                            used_by.setdefault(id(hit), (hit, r))
                            continue
                        other = next((e for e in es.lines if e.key != r.key and not _mat_diffs(r, e) and
                                      _inside(r, e)), None)
                        if other is not None:
                            used_by.setdefault(id(other), (other, r))
                            rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=[r.key[0]], r=r,
                                e=other, r_count=count, fault=other.fault,
                                pattern='%s %s>%s' % (r.key[0], r.key[1], other.key[1]) + _flags(r, other))
                            continue
                        if not same:
                            rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=[r.key[0]], r=r,
                                r_count=count, pattern='%s %s absent' % r.key + _flags(r, None))
                            continue
                        best = min(same, key=lambda e: (len(_mat_diffs(r, e)) + (0 if _inside(r, e) else 1),
                                                        e.lineno))
                        d = _mat_diffs(r, best) + ([] if _inside(r, best) else ['uv'])
                        if r.anim or best.anim:
                            pre.append('anim')
                        rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid,
                            features=pre + [f for f in FEATURES if f in d], r=r, e=best, r_count=count,
                            fault=best.fault)
                        continue
                    if src == 'T':
                        # track UV per slot (fallback without slot marker: as a corner set,
                        # the retail corners a subset of the engine corners; counted)
                        if 'foreign' in r.flags:
                            _bump(stats['foreign'], tr, count)
                        elif r.uv is not None and r.slots is None:
                            _bump(stats['t_set_rule'], tr, count)
                        same = [e for e in es.lines if e.key == r.key]
                        hit = next((e for e in same if not _t_diffs(r, e, ctol)), None)
                        if hit is not None:
                            used_by.setdefault(id(hit), (hit, r))
                            continue
                        others = [e for e in es.lines if e.key != r.key and not _t_diffs(r, e, ctol)]
                        stage = next((e for e in others if e.key[0] == 'stage'), None)
                        if stage is not None and r.key[0] == 'cell':
                            # the tile packet is exactly a whole-face stage layout of the face
                            # (a table entry may carry the same layout): drawn as that stage
                            used_by.setdefault(id(stage), (stage, r))
                            _bump(stats['stage_hit'], tr, count)
                            continue
                        other = others[0] if others else None
                        if other is not None:
                            used_by.setdefault(id(other), (other, r))
                            rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=[r.key[0]], r=r,
                                e=other, r_count=count, fault=other.fault,
                                pattern='%s %s>%s' % (r.key[0], r.key[1], other.key[1]) + _flags(r, other))
                            continue
                        if not same:
                            rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=[r.key[0]], r=r,
                                r_count=count, pattern='%s %s absent' % r.key + _flags(r, None))
                            continue
                        best = min(same, key=lambda e: (len(_t_diffs(r, e, ctol)), e.lineno))
                        d = _t_diffs(r, best, ctol)
                        if d == ['col'] and r.key[0] == 'cell' and r.slots is not None and best.slots is not None and \
                                max(max(abs(p - q) for p, q in zip(a[1], b[1])) for a, b in _common_slots(r, _tcol_view(r, best))
                                    if a[1] is not None and b[1] is not None) <= COL_MODEL_GAP:
                            # computed tile corner colours within a rounding step of the
                            # oracle's colour model: information, counted
                            _bump(stats['col_model'], tr, count)
                            used_by.setdefault(id(best), (best, r))
                            continue
                        if r.anim or best.anim:
                            pre.append('anim')
                        rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=pre + d, r=r, e=best,
                            r_count=count, fault=best.fault)
                        continue
                    pool = [t for t in es.tris if t.lod == rlod]
                    used, miss, nrev = match_line(r, es, rlod, None, ctol, pool, strict_winding)
                    if miss is not None and r.extra.get('func') in PIECE_FUNCS:
                        # a piece cut at the water or mirror line: page, depth, CLUT and blend
                        # exact, every UV corner inside the corner box of one engine triangle;
                        # its colour (the side's tint) is its own feature
                        _bump(stats['piece'], tr, count)
                        if r.uv is None:
                            # untextured on both sides: the piece lies in any engine triangle of
                            # its material; only its colour decides
                            inside = [t for t in pool if t.mat == r.mat() and all(x is None for x in t.uvkey)]
                        else:
                            inside = [t for t in pool if t.mat == r.mat() and t.uvkey and None not in t.uvkey and
                                      _in_box(r.uv, t.uvkey)]
                        if inside:
                            # the engine draws a triangle once per side tint (above/below the
                            # water, mirror): the piece is drawn when ANY of them fits its colour
                            fit = next((t for t in inside if r.col is None or
                                        not [c for c in t.cols if c is not None] or
                                        _cols_in_range(r.col, [c for c in t.cols if c is not None], ctol)), None)
                            t = fit or min(inside, key=lambda x: _range_gap(r.col, x.cols))
                            used_by.setdefault(id(t.e), (t.e, r))
                            if fit is not None:
                                continue
                            if r.col is not None and _range_gap(r.col, t.cols) <= COL_CUT_GAP:
                                # a computed cut colour within a rounding step: information
                                _bump(stats['cut_col'], tr, count)
                                continue
                            rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid,
                                features=pre + [PIECE_FUNCS[r.extra['func']]], r=r, e=t.e, r_count=count,
                                fault=t.fault)
                            continue
                    for e in used:
                        used_by.setdefault(id(e), (e, r))
                    if not miss:
                        if miss is None:
                            stats['winding'] += nrev * count
                            if r.prim not in PLAIN_PRIMS:
                                stats['prim_equal'] += count
                        continue
                    feats, eng, fault, wind = miss
                    if not feats:
                        # no engine triangle to measure against (only lines without UV)
                        other = es.lines[0]
                        feats = _mat_diffs(r, other) or ['uv']
                        eng, fault = other, other.fault
                    if eng is not None and (r.anim or eng.anim):
                        pre.append('anim')
                    rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=pre + feats, r=r, e=eng,
                        r_count=count, fault=fault, wind=wind)
                for lv, n in sorted(lod_count.items()):
                    rr = next(r for r, _ in work if (r.lod or '0') == lv)
                    _bump(stats['lod_lines'], (tr, obj), n)
                    rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=['lod'], r=rr, r_count=n,
                        pattern='lod %s>%s' % (lv, '|'.join(sorted(es.lods))))
                if ob is not None:
                    ob['used'].update(e.tuple_key() for e, _ in used_by.values())
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
                        rec(kind='d1', frame=fr, track=tr, src=src, srcId=sid, features=['sample-outside'],
                            r=r, e=e, r_count=1, fault=e.fault,
                            pattern=('ring0' if ring0 else 'ringf') + (' wrap' if wrapped else ''),
                            detail='box %s uv box %s ring0 %d ringf %d reach %d' % (
                                ','.join(map(str, e.box)), ','.join(map(str, bbox(e.uv))), ring0, ringf, reach))
    # direction 2, models
    reported = set((r.track, r.object, r.e.mat()) for r in records if r.kind == 'd1' and r.e is not None)
    for tr in sorted(per_track):
        for (src, ok), ob in sorted(per_track[tr].items()):
            obj = '%s:%s' % (src, ok)
            stats['unused'][(tr, obj)] = (len(ob['seen']), len(ob['seen'] & ob['used']))
            for mat, ent in ob['E'].items():
                if ent['fault']:
                    stats['fault_units'].append((ent['fault'], (tr, obj, mat), (tr, obj, mat) in visible))
            if not ob['R']:
                if ob['E']:
                    any_e = next(iter(ob['E'].values()))
                    rec(kind='info', track=tr, src=src, srcId=ob['srcId'], features=['engine-only-source'],
                        e=any_e['e'], frames=len(set().union(*[v['frames'] for v in ob['E'].values()])))
                continue
            for mat, ent in sorted(ob['E'].items(), key=lambda kv: kv[1]['e'].lineno):
                if mat in ob['R'] or (tr, obj, mat) in reported:
                    continue
                if not any(t in ob['rtri'] and not (ob['rtri'][t] & ob['etri'].get(t, set()))
                           for t in ent.get('uvs', ())):
                    # no triangle of this material that retail drew with another material
                    # the engine lacks for it: the engine material lies on triangles retail
                    # never drew, or on a copy of a triangle the engine also draws with
                    # retail's material (another face with the same UV) - coverage
                    stats['eom_coverage'].append((tr, obj, mat))
                    continue
                if not ent['front'] and not ob['rback']:
                    # only back-side copies (side=back: a two-sided triangle drawn as two
                    # one-sided ones) and retail never showed a back side of the model here
                    stats['back_coverage'].append((tr, obj, mat))
                    continue
                if not (ent['lod'] & ob['rlod']):
                    # retail drew the model only with another header on this track
                    stats['lod_never'].add((tr, obj))
                    continue
                rec(kind='d2', track=tr, src=src, srcId=ent['e'].srcId, features=['engine-only-material'],
                    e=ent['e'], frames=len(ent['frames']), fault=ent['fault'])
    for tr, cov in tcov.items():
        if cov['load_faces']:
            stats['coverage'][tr] = (len(cov['load_faces']), len(cov['load_faces'] & cov['seen_faces']),
                                     len(cov['load_cells']), len(cov['load_cells'] & cov['seen_cells']),
                                     len(cov['mir_faces']), len(cov['mir_faces'] & set(f for f, _ in cov['seen_cells'])))
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
    """(problems, one-sided): problems make a zero worthless (no H line, a
    dead T or I hook in the frames drawn on a track, a race frame without a
    VRAM dump); one-sided source kinds (effects only on the engine side,
    native drivers, sky and water only on the retail side) are listed."""
    out, one = [], []
    if not log.headers:
        out.append('no H line (exe commit and switches)')
    for tr in stats['tracks']:
        menu = tr in stats['menu_tracks']
        kinds = set(src for (t, src) in stats['per'] if t == tr)
        need = set() if menu else set(['T', 'I'])
        routed = set(o.split(':')[0] for o in stats['route_retail'].get(tr, ()))
        for src in sorted(kinds | need):
            p = stats['per'].get((tr, src), {'R': 0, 'E': 0, 'Rrace': 0, 'Erace': 0})
            if src in need:
                if p['Rrace'] == 0:
                    out.append('%s %s: no retail line in its race frames' % (tr, src))
                if p['Erace'] == 0 and src not in routed:
                    out.append('%s %s: no engine line in its race frames' % (tr, src))
            elif (p['R'] == 0) != (p['E'] == 0):
                one.append('%s %s: %s side only (%d line(s))%s' % (
                    tr, src, 'engine' if p['E'] else 'retail', p['E'] or p['R'], ' - menu scene' if menu else ''))
    if stats['vram_missing']:
        out.append('%d race frame(s) without a readable VRAM dump: %s' % (
            len(stats['vram_missing']), ' '.join(stats['vram_missing'][:10])))
    return out, one


def analyse(log, rows, gte_tol=COL_TOL_GTE, expect=None, vram_for=None, check_plausible=True,
            strict_winding=False):
    records, stats = compare(log.frames, gte_tol, vram_for, strict_winding)
    apply_exceptions(records, rows)
    faults, positives, _ = parse_expect(expect)
    res = Result()
    res.log, res.records, res.stats, res.rows = log, records, stats, rows
    devs = [r for r in records if r.kind in ('d1', 'd2')]
    res.open = [r for r in devs if r.excepted is None]
    res.covered = [r for r in devs if r.excepted is not None]

    def on_target(r):
        t = FAULT_TARGET.get(r.fault)
        return t is None or t in r.features
    # a control deviation points at a fault line AND has the fault's feature;
    # other deviations on fault lines stay deviations (listed as strays)
    res.control_open = [r for r in res.open if r.fault and on_target(r)]
    res.stray = [r for r in res.open if r.fault and not on_target(r)]
    res.control_covered = [r for r in res.covered if r.fault]
    res.others = [r for r in res.open if not (r.fault and on_target(r))]
    res.info = [r for r in records if r.kind == 'info']
    res.control_missing = []
    failed = False
    # controls, per fault kind: every faulted unit retail draws (a model material,
    # a track face) must be reported with the fault's feature
    units = stats['fault_units']
    kinds = sorted(set(k for k, _, _ in units) | set(faults))
    parts = []
    for kind in kinds:
        target = FAULT_TARGET.get(kind)
        want = sorted(set(u for k, u, vis in units if k == kind and vis), key=repr)
        orient = set(u for k, u in stats['fault_orient'] if k == kind) - set(want)
        blind = len(set(u for k, u, vis in units if k == kind and not vis) - set(want) - orient)
        mine = [r for r in res.control_open if r.fault == kind]
        reported = set(unit_of(r) for r in mine)
        missing = [u for u in want if u not in reported]
        res.control_missing += [(kind, u) for u in missing]
        strays = sum(1 for r in res.stray if r.fault == kind)
        if not any(k == kind for k, _, _ in units):
            parts.append('%s no - FAILED: expected, no fault=%s line in the log' % (kind, kind))
            failed = True
        elif mine and want and not missing:
            parts.append('%s yes (%d deviation(s), feature %s on %s; %d faulted unit(s) retail draws%s%s)' % (
                kind, len(mine), target or 'any', ' '.join(sorted(set(r.track for r in mine))), len(want),
                (', %d not drawn by retail' % blind if blind else '') +
                (', %d differ only in orientation (not provable from UV corner sets)' % len(orient) if orient else ''),
                ', %d other deviation(s) on fault lines (counted as deviations)' % strays if strays else ''))
        else:
            swallowed = sorted(set(r.excepted for r in res.control_covered if r.fault == kind))
            if not want:
                why = 'no faulted unit on a face retail draws'
            elif swallowed and not mine:
                why = 'covered by exception(s) %s' % ', '.join(swallowed)
            else:
                why = '%d of %d faulted unit(s) not reported with feature %s' % (len(missing), len(want), target or 'any')
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
    res.plausible, res.one_sided = plausibility(log, stats) if check_plausible else ([], [])
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
    for kind, u in res.control_missing[:20]:
        md.append('- not reported (%s): %s %s%s' % (kind, u[0], u[1], (' ' + f_matkey(u[2])) if len(u) > 2 else ''))
    if res.positives:
        md.append('Known positives: %s' % '; '.join('%s %s %s %s' % (t, o, f, ('yes (%d)' % n) if n else 'no - FAILED')
                                                for t, o, f, n in res.positives))
    md.append('Plausibility: %s' % ('ok' if not res.plausible else 'FAILED - ' + '; '.join(res.plausible[:12])))
    if res.one_sided:
        md.append('One-sided source kinds (by design, listed): %s' % '; '.join(res.one_sided))
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
    md.append('Colour: tolerance %d per channel for the normal writer, %d for GTE writers, %d for computed tile '
              'corners (col-model, information: %d line(s)), %d for computed cut colours (information: %d line(s)); '
              '%d retail / %d engine line(s) without col (not compared), of them %d with a col that does not '
              'pair with the corners; raw-texture colour not compared: %d retail line(s) (raw=1)' % (
                  COL_TOL_NORMAL, res.gte_tol if hasattr(res, 'gte_tol') else COL_TOL_GTE, COL_MODEL_GAP,
                  sum(s['col_model'].values()), COL_CUT_GAP, sum(s['cut_col'].values()),
                  s['r_no_col'], s['e_no_col'], s['col_unpaired'], s['raw_col']))
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
    md.append('- Model triangles found only with the other winding (corners as a set, winding not compared%s): %d'
              % (' - strict: a uv deviation' if getattr(res, 'strict_winding', False) else '', s['winding']))
    md.append('- Model pieces cut at the water or mirror line (func split/reflection/special) by the piece rule: %d'
              % sum(s['piece'].values()))
    md.append('- Engine materials on triangles retail never drew (coverage, not engine-only-material): %d'
              % len(s['eom_coverage']))
    md.append('- Engine back-side materials (side=back) of models retail never showed from the back (coverage): %d'
              % len(s['back_coverage']))
    for tr in sorted(s['tablefail']):
        md.append('- %s K-ADR/tablefail: %d load-block line(s) of faces whose table the engine refused (information)'
                  % (tr, s['tablefail'][tr]))
    for tr in sorted(s['anim_degenerate']):
        md.append('- %s set=anim lines of faces the load block marks degenerate and no race frame draws: %d '
                  '(information)' % (tr, s['anim_degenerate'][tr]))
    if s['stage_hit']:
        md.append('- Track tile packets that are exactly a stage layout of their face (counted as that stage): %d'
                  % sum(s['stage_hit'].values()))
    if s['foreign']:
        md.append('- Track stage lines laid on their owner face from another mid face (foreign=), corners as a '
                  'set: %d' % sum(s['foreign'].values()))
    if s['src_cell']:
        md.append('- Track tiles without slots placed by their table entry (src= against entry= of the load '
                  'block): %d' % sum(s['src_cell'].values()))
    if s['t_set_rule']:
        md.append('- Track lines without slot marker, compared as a corner set (orientation inside the face not '
                  'provable): %d (%s)' % (sum(s['t_set_rule'].values()),
                                         ' '.join('%s %d' % kv for kv in sorted(s['t_set_rule'].items()))))
    if s['coverage']:
        md.append('')
        md += _table(('track', 'faces (load block)', 'seen in race frames', 'cells (load block)', 'seen',
                      'mirrored faces', 'mirrored with race cells'),
                     [(tr,) + v for tr, v in sorted(s['coverage'].items())], limit=1000)
    md.append('')
    md += _table(('track', 'model', 'engine tuples', 'used', 'unused'),
                 [(t, o, a, b, a - b) for (t, o), (a, b) in sorted(s['unused'].items())])
    md.append('')
    md.append('## Information (not deviations)')
    md.append('')
    md.append('- Models the engine draws but retail never draws on the track: %d' % len(res.info))
    md.append('- Track set=anim: engine entries without a retail line: %d' % s['anim_gap'])
    for tr in sorted(set(t for t, _ in s['info_lines']) | set(s['part']) | set(s['clip']) | set(s['nogeo']) |
                     set(s['stage_none'])):
        md.append('- %s track lines: part %d (compared by the piece rule), clip %d, nogeo %d (counted only), '
                  'stage none %d; without engine partner (information): %s' % (
                      tr, s['part'].get(tr, 0), s['clip'].get(tr, 0), s['nogeo'].get(tr, 0),
                      s['stage_none'].get(tr, 0),
                      ', '.join('%s %d' % (k, s['info_lines'].get((tr, k), 0))
                                for k in INFO_FLAGS_ANY + INFO_FLAGS_LOAD)))
    for tr, obj in sorted(s['lod_never']):
        md.append('- %s: header 0 of %s never compared (retail drew it only with another header)' % (tr, obj))
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
    """vram= is tried relative to the log, as its file name next to the log,
    and relative to the current folder (the game writes it relative to its
    base folder)."""
    cache = {}

    def load(fr):
        tries = [os.path.join(fr.base_dir, fr.vram), os.path.join(fr.base_dir, os.path.basename(fr.vram)), fr.vram]
        path = next((t for t in tries if os.path.isfile(t)), tries[0])
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
    ap.add_argument('--strict-winding', action='store_true',
                    help='compare the winding of model triangles (a reversed one is a uv deviation)')
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
    res = analyse(log, rows, a.col_tol_gte, a.expect, file_vram_loader(), strict_winding=a.strict_winding)
    res.strict_winding = a.strict_winding
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

    def run(lines, expect=None, rws=None, vram=None, plaus=False, gte=COL_TOL_GTE, strict=False):
        log = parse_log('\n'.join(['H\tabc1234\t--engine --engine-oracle x'] + lines) + '\n', 'synthetic')
        if log.errors:
            print('parse errors: %s' % log.errors)
        rr = rws if rws is not None else rows
        for x in rr:
            x.hits = 0
        res = analyse(log, rr, gte, expect, (lambda fr: vram) if vram is not None else None, plaus, strict)
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
    res, _ = run(base_r + [L('E', 'I', 'tikka#0', uv=TRI_A_REV), L('E', 'I', 'tikka#0', uv=TRI_B)])
    check('winding: a triangle is a set of (UV, colour) corners, the reversed winding is counted apart',
          len(res.open) == 0 and res.stats['winding'] == 1)
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv=TRI_A_REV)], strict=True)
    check('winding: --strict-winding makes it a uv deviation', [r.pattern for r in res.open] == ['uv winding reversed'])
    res, _ = run([F(), L('R', 'I', 'b#0', uv=TRI_A, extra='col=10,10,10;20,20,20;30,30,30'),
                  L('E', 'I', 'b#0', uv='0,0,0,31,31,0', extra='col=10,10,10;30,30,30;20,20,20')])
    check('winding: reversed corners keep their colour (pairing by UV): 0', len(res.open) == 0)
    res, _ = run([F(), L('R', 'I', 'b#0', uv=TRI_A, extra='col=10,10,10;20,20,20;30,30,30'),
                  L('E', 'I', 'b#0', uv='0,0,0,31,31,0', extra='col=10,10,10;20,20,20;30,30,30')])
    check('winding: colours moved to other UV corners: col', feats(res) == ['col'])

    # world particles (X particle-gNN#slot): quad against the engine's two triangles (u0,u1,u3)/(u0,u3,u2),
    # line untextured by its colour set
    PQ = '16,16,31,16,16,31,31,31'
    pe = [L('E', 'X', 'particle-g03#7', uv='16,16,31,16,31,31', extra='prim=particle,col=200,100,50'),
          L('E', 'X', 'particle-g03#7', uv='16,16,31,31,16,31', extra='prim=particle,col=200,100,50')]
    res, _ = run([F(), L('R', 'X', 'particle-g03#7', uv=PQ, blend='1', extra='prim=particle,col=200,100,50')] +
                 [x.replace('\to\t', '\t1\t') for x in pe])
    check('particle quad against two engine triangles: 0, no draw-path class', len(res.open) == 0)
    res, _ = run([F(), L('R', 'X', 'particle-g03#7', uv=PQ, extra='prim=particle,col=200,100,50')] +
                 [x.replace('0,480', '0,481') for x in pe])
    check('particle quad with another CLUT: clut', feats(res) == ['clut'])
    res, _ = run([F(), L('R', 'X', 'particle-g03#7', uv=PQ, extra='prim=particle,col=201,100,50')] + pe)
    check('particle colour is copied by retail: tolerance 0, col', feats(res) == ['col'])
    pl = [L('E', 'X', 'particle-g05#2', page='-', clut='-', uv='-', blend='1', extra='prim=line,col=9,9,9;9,9,9;40,40,40'),
          L('E', 'X', 'particle-g05#2', page='-', clut='-', uv='-', blend='1', extra='prim=line,col=40,40,40;9,9,9;9,9,9')]
    res, _ = run([F(), L('R', 'X', 'particle-g05#2', page='-', clut='-', uv='-', blend='1',
                         extra='prim=line,col=9,9,9;40,40,40;40,40,40')] + pl)
    check('particle line: same colour set: 0', len(res.open) == 0)
    res, _ = run([F(), L('R', 'X', 'particle-g05#2', page='-', clut='-', uv='-', blend='1',
                         extra='prim=line,col=9,9,9;41,40,40;41,40,40')] + pl)
    check('particle line: another colour: col', feats(res) == ['col'])
    res, _ = run([F(), L('R', 'X', 'particle-g05#2', page='-', clut='-', uv='-', blend='2',
                         extra='prim=line,col=9,9,9;40,40,40;40,40,40')] + pl)
    check('particle line: another blend: blend', feats(res) == ['blend'])
    res, _ = run([F(), L('R', 'X', 'particle-g05#2', page='-', clut='-', uv='-', blend='1',
                         extra='prim=line,col=9,9,9;40,40,40;40,40,40')] +
                 [pl[0], pl[1].replace('col=40,40,40;9,9,9;9,9,9', 'col=40,40,40;9,9,9;70,70,70')])
    check('particle line: an engine colour retail does not have: col', feats(res) == ['col'])

    # model pieces cut at the water or mirror line (func=split|reflection|special)
    res, _ = run([F(), L('R', 'I', 'turtle#3', uv='4,4,20,4,4,20', extra='func=split col=85,0,0'),
                  L('E', 'I', 'turtle#3', uv=TRI_A, extra='col=85,0,0')])
    check('piece: UV corners inside an engine triangle, same material and colour: 0',
          len(res.open) == 0 and res.stats['piece'] == {'L3': 1})
    res, _ = run([F(), L('R', 'I', 'turtle#3', uv='4,4,20,4,4,20', extra='func=split col=64,0,0'),
                  L('E', 'I', 'turtle#3', uv=TRI_A, extra='col=85,0,0')])
    check('piece: the side colour (x0.75) is its own feature split-col', feats(res) == ['split-col'])
    res, _ = run([F(), L('R', 'I', 'crate#5', uv='4,4,20,4,4,20', extra='func=reflection col=48,48,48'),
                  L('E', 'I', 'crate#5', uv=TRI_A, extra='col=64,64,64')])
    check('piece: mirror side colour is mirror-col', feats(res) == ['mirror-col'])
    res, _ = run([F(), L('R', 'I', 'turtle#3', uv='4,4,20,4,4,20', extra='func=split col=64,0,0'),
                  L('E', 'I', 'turtle#3', uv=TRI_A, extra='col=85,0,0'),
                  L('E', 'I', 'turtle#3', uv=TRI_A, extra='col=64,0,0')])
    check('piece: the engine draws the triangle once per side tint; any fitting one draws the piece: 0',
          len(res.open) == 0)
    res, _ = run([F(), L('R', 'I', 'turtle#3', uv='4,4,40,4,4,20', extra='func=split'),
                  L('E', 'I', 'turtle#3', uv=TRI_A)])
    check('piece: a corner outside every engine triangle stays uv', feats(res) == ['uv'])
    res, _ = run([F(), L('R', 'I', 'turtle#3', page='-', clut='-', uv='-', extra='func=split col=60,60,60'),
                  L('E', 'I', 'turtle#3', page='-', clut='-', uv='-', extra='col=10,10,10'),
                  L('E', 'I', 'turtle#3', page='-', clut='-', uv='-', extra='col=60,60,60;80,80,80;60,60,60')])
    check('untextured piece: in any untextured engine triangle of its material, colour decides: 0', len(res.open) == 0)
    res, _ = run([F(), L('R', 'I', 'turtle#3', page='-', clut='-', uv='-', extra='func=split col=45,45,45'),
                  L('E', 'I', 'turtle#3', page='-', clut='-', uv='-', extra='col=60,60,60;80,80,80;60,60,60')])
    check('untextured piece outside every colour range: split-col', feats(res) == ['split-col'])
    res, _ = run([F(), L('R', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='func=split col=127,100,30;111,74,15;111,74,15'),
                  L('E', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='col=0,0,0;45,0,0;27,18,3'),
                  L('E', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='col=97,58,8;112,74,16;128,100,30')])
    check('untextured piece: the second engine triangle of the material fits: 0', len(res.open) == 0)
    res, _ = run([F(), L('R', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='func=split col=127,100,30;96,58,7;96,58,7'),
                  L('E', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='col=0,0,0;45,0,0;27,18,3'),
                  L('E', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='col=97,58,8;112,74,16;128,100,30')])
    check('untextured piece one step outside every range: cut colour information (no split-col)',
          len(res.open) == 0 and res.stats['cut_col'] == {'L3': 1})
    res, _ = run([F(), L('R', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='func=split col=127,100,30;94,58,7;94,58,7'),
                  L('E', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='col=0,0,0;45,0,0;27,18,3'),
                  L('E', 'I', 'turtle#31', page='-', clut='-', uv='-', extra='col=97,58,8;112,74,16;128,100,30')])
    check('untextured piece three steps outside every range: split-col naming the nearest triangle',
          feats(res) == ['split-col'] and res.open[0].e.col[0] == (97, 58, 8) and res.stats['cut_col'] == {})
    res, _ = run([F(), L('R', 'I', 'turtle#3', uv='4,4,20,4,4,20'), L('E', 'I', 'turtle#3', uv=TRI_A)])
    check('piece rule only for split/reflection/special: a normal line inside a triangle is uv', feats(res) == ['uv'])
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
    check('track coverage: faces and cells', res.stats['coverage'].get('L3') == (1, 1, 1, 1, 0, 0))
    res, _ = run([F('t-load'), T('R', '5', C00, 'cell=0,0', extra='mir=1'), T('E', '5', C00, 'cell=0,0'),
                  T('R', '6', C00, 'cell=0,0', extra='mir=1'), T('E', '6', C00, 'cell=0,0'),
                  F('t1'), T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0')])
    check('track coverage: mirrored faces and those with race cells', res.stats['coverage'].get('L3') == (2, 1, 2, 1, 2, 1))

    # track corners per slot (A9): corner k of the retail line on slot k of the engine line
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot'), T('E', '5', C00, 'cell=0,0')])
    check('slots: same corners in slot order: 0, no set rule', len(res.open) == 0 and res.stats['t_set_rule'] == {})
    res, _ = run([F(), T('R', '5', '7,0,0,0,7,7,0,7', 'cell=0,0', extra='ord=slot'), T('E', '5', C00, 'cell=0,0')])
    check('slots: a cell turned (same corner set, other slots): uv', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '5', '0,0,7,0,0,7', 'cell=0,0', extra='slots=0,1,2'), T('E', '5', C00, 'cell=0,0')])
    check('slots: a GT3 on 3 of the 4 slots: 0', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', '7,0,7,7,0,7', 'cell=0,0', extra='slots=1,3,2'), T('E', '5', C00, 'cell=0,0')])
    check('slots: a GT3 on the other diagonal: 0', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', '7,0,7,7,0,7', 'cell=0,0', extra='slots=0,3,2'), T('E', '5', C00, 'cell=0,0')])
    check('slots: a GT3 with a corner on the wrong slot: uv', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot,col=1,1,1;2,2,2;3,3,3;4,4,4'),
                  T('E', '5', C00, 'cell=0,0', extra='col=1,1,1;2,2,2;3,3,9;4,4,4')])
    check('slots: colours compared per slot', feats(res) == ['col'])
    TURN = '7,0,0,0,7,7,0,7'
    res, _ = run([F('L3-load'), T('R', '5', C00, 'cell=0,0', extra='ord=slot'), T('R', '5', TURN, 'cell=0,1', extra='ord=slot'),
                  T('E', '5', TURN, 'cell=0,0', extra='fault=cell'),
                  T('E', '5', C00, 'cell=0,1', extra='fault=cell')], expect=['fault:cell'])
    check('slots: swapping a tile with its turned copy is a cell deviation and the control fires',
          res.control.startswith('yes') and res.stats['fault_orient'] == set() and feats(res) == ['cell', 'cell'])
    check('parse: bad slots are an error',
          len(parse_log('\n'.join([F(), T('R', '5', C00, 'cell=0,0', extra='slots=0,1,1,2')]) + '\n', 's').errors) == 1)

    # zeroarea never uv, pieces against the box of the face, a stage before a tile
    S2 = '0,0,15,0,0,15,15,15'
    res, _ = run([F(), T('R', '5', '168,0,168,0,168,0,168,0', 'stage=2', extra='zeroarea=1'), T('E', '5', C00, 'stage=2')])
    check('zeroarea=1 with an engine partner: information, not uv', len(res.open) == 0 and
          res.stats['info_lines'] == {('L3', 'zeroarea'): 1})
    res, _ = run([F(), T('R', '5', '1,1,7,1,1,7,168,0', 'stage=2', extra='part=1'), T('E', '5', C00, 'stage=2')])
    check('part=1: three corners in the box of the face, a free fourth corner outside: 0', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', '1,1,9,1,1,7,168,0', 'stage=2', extra='part=1'), T('E', '5', C00, 'stage=2')])
    check('part=1: two corners outside the box: uv', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '5', '1,1,7,1,9,9', 'stage=2', extra='part=1'), T('E', '5', C00, 'stage=2')])
    check('part=1: a three-corner piece needs all corners in the box: uv', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '5', S2, 'cell=1,1'), T('E', '5', C11, 'cell=1,1'),
                  T('E', '5', S2, 'cell=0,1'), T('E', '5', S2, 'stage=2')])
    check('a tile packet that is exactly a stage layout of the face: that stage, not cell', len(res.open) == 0 and
          res.stats['stage_hit'] == {'L3': 1})
    res, _ = run([F(), T('R', '5', S2, 'cell=1,1'), T('E', '5', C11, 'cell=1,1'), T('E', '5', S2, 'cell=0,1')])
    check('without a matching stage the tile on another cell stays cell', feats(res) == ['cell'])

    # wide frame tiles (wide=1 against colw=), foreign stage lines (corners as a set)
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot,wide=1,col=10,10,10;20,20,20;30,30,30;40,40,40'),
                  T('E', '5', C00, 'cell=0,0', extra='col=10,10,10;20,20,20;30,30,30;90,90,90,'
                                                     'colw=10,10,10;20,20,20;30,30,30;40,40,40')])
    check('wide=1: tile colours against colw= of the engine line: 0', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot,col=10,10,10;20,20,20;30,30,30;40,40,40'),
                  T('E', '5', C00, 'cell=0,0', extra='col=10,10,10;20,20,20;30,30,30;90,90,90,'
                                                     'colw=10,10,10;20,20,20;30,30,30;40,40,40')])
    check('without wide=1 the tile colours are compared with col=: col', feats(res) == ['col'])
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot,wide=1,col=10,10,10;20,20,20;30,30,30;90,90,90'),
                  T('E', '5', C00, 'cell=0,0', extra='col=10,10,10;20,20,20;30,30,30;90,90,90')])
    check('wide=1 without colw= on the engine line falls back to col=: 0', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', '7,0,0,0,7,7,0,7', 'stage=2', extra='ord=slot,foreign=2'),
                  T('E', '5', C00, 'stage=2')])
    check('foreign=<place>: corners compared as a set, counted', len(res.open) == 0 and res.stats['foreign'] == {'L3': 1})

    # set=anim missing-source by cross reference, faces with a refused table
    res, _ = run([F('L3-load'), T('R', '9', C00, 'stage=2', extra='degenerate=1'),
                  F('t1'), T('R', '9', C00, 'stage=2', extra='set=anim,anim=1')])
    check('set=anim missing-source: degenerate in the load block and never drawn: information',
          len(res.open) == 0 and res.stats['anim_degenerate'] == {'L3': 1})
    res, _ = run([F('L3-load'), T('R', '9', C00, 'stage=2', extra='degenerate=1'),
                  F('t1'), T('R', '9', C00, 'stage=2', extra='set=anim,anim=1'), T('R', '9', C00, 'stage=2')])
    check('set=anim missing-source stays when a race frame draws the face', feats(res) == ['missing-source'] * 2)
    res, _ = run([F('L3-load'), T('R', '9', C00, 'stage=2'), F('t1'), T('R', '9', C00, 'stage=2', extra='set=anim,anim=1')])
    check('set=anim missing-source stays without degenerate in the load block', feats(res) == ['missing-source'] * 2 and res.stats['anim_degenerate'] == {})
    res, _ = run([F('L3-load'), T('R', '5', C00, 'cell=0,0', extra='entry=0'), T('R', '5', C01, 'cell=0,1', extra='entry=1'),
                  T('R', '5', '0,0,15,0,0,15,15,15', 'stage=2'),
                  T('E', '5', '0,0,15,0,0,15,15,15', 'stage=2', extra='tablefail=1'),
                  T('E', '5', '0,0,15,0,0,15,15,15', 'stage=3', extra='tablefail=1')])
    check('tablefail=1: table entries and unpartnered engine lines of the face are information (K-ADR/tablefail)',
          len(res.open) == 0 and res.stats['tablefail'] == {'L3': 3})
    res, _ = run([F('L3-load'), T('R', '5', C00, 'cell=0,0', extra='entry=0'), T('R', '5', '0,0,15,0,0,15,15,15', 'stage=2'),
                  T('E', '5', '0,0,15,0,0,15,15,15', 'stage=2')])
    check('without tablefail the table entry stays a deviation', feats(res) == ['cell'])

    # zeroarea and the cell of a tile from its table entry (src= against entry= of the load block)
    res, _ = run([F(), T('R', '9', '0,0,15,0,0,15,15,15', 'stage=2', extra='zeroarea=1')])
    check('zeroarea=1: a dynamic missing-source is information', len(res.open) == 0 and
          res.stats['info_lines'] == {('L3', 'zeroarea'): 1})
    blk = [F('L3-load'), T('R', '5', C00, 'cell=0,0', extra='entry=4'), T('R', '5', C11, 'cell=1,1', extra='entry=9'),
           T('E', '5', C00, 'cell=0,0'), T('E', '5', C11, 'cell=1,1')]
    res, _ = run(blk + [F('t1'), T('R', '5', C11, 'cell=0,1', extra='src=9'), T('E', '5', C11, 'cell=1,1')])
    check('src=<e>: a tile without slots takes the cell of its table entry: 0',
          len(res.open) == 0 and res.stats['src_cell'] == {'L3': 1})
    res, _ = run(blk + [F('t1'), T('R', '5', C11, 'cell=0,1'), T('E', '5', C11, 'cell=1,1')])
    check('src=<e>: without it the cell comes from where the tile lies (cell deviation)', feats(res) == ['cell'])
    res, _ = run(blk + [F('t1'), T('R', '5', C11, 'cell=0,1', extra='src=9,slots=0,1,2,3'), T('E', '5', C11, 'cell=1,1')])
    check('src=<e> is not used when the tile has slots', feats(res) == ['cell'] and res.stats['src_cell'] == {})

    # markers of lines without an engine partner, triangle faces, tile colour model
    S2 = '0,0,15,0,0,15,15,15'
    res, _ = run([F('L3-load'), T('R', '9', S2, 'stage=2', extra='degenerate=1'),
                  T('R', '8', S2, 'stage=2', extra='blank=1'), T('R', '7', S2, 'stage=2', extra='nolayout=1'),
                  T('R', '6', S2, 'stage=2', extra='dropped=1'), T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0')])
    check('load block: degenerate, blank, nolayout, dropped without partner are information',
          len(res.open) == 0 and sorted(k for _, k in res.stats['info_lines']) == ['blank', 'degenerate', 'dropped', 'nolayout'])
    res, _ = run([F(), T('R', '9', S2, 'stage=2', extra='degenerate=1,set=anim')])
    check('degenerate outside the load block stays missing-source', feats(res) == ['missing-source'])
    res, _ = run([F(), T('R', '9', S2, 'stage=2', extra='blank=1')])
    check('blank=1 is information in any set', len(res.open) == 0)
    res, _ = run([F('L3-load'), T('R', '5', '0,0,7,0,0,7,9,9', 'stage=2', extra='ord=slot'),
                  T('E', '5', '0,0,7,0,0,7', 'stage=2', extra='slots=0,1,2')])
    check('triangle face (slots=0,1,2) against a 4-corner layout: only its slots compared: 0', len(res.open) == 0)
    res, _ = run([F('L3-load'), T('R', '5', '0,0,7,0,0,8,9,9', 'stage=2', extra='ord=slot'),
                  T('E', '5', '0,0,7,0,0,7', 'stage=2', extra='slots=0,1,2')])
    check('triangle face: a differing corner on one of its slots: uv', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot,col=225,1,1'),
                  T('E', '5', C00, 'cell=0,0', extra='col=226,1,1')])
    check('tile corner colour off by 1 with slots on both sides: col-model information, no deviation',
          len(res.open) == 0 and res.stats['col_model'] == {'L3': 1})
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot,col=224,1,1'),
                  T('E', '5', C00, 'cell=0,0', extra='col=226,1,1')])
    check('tile corner colour off by 2: col-model information', len(res.open) == 0 and res.stats['col_model'] == {'L3': 1})
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='ord=slot,col=223,1,1'),
                  T('E', '5', C00, 'cell=0,0', extra='col=226,1,1')])
    check('tile corner colour off by 3: col', feats(res) == ['col'] and res.stats['col_model'] == {})
    res, _ = run([F(), T('R', '5', S2, 'stage=2', extra='ord=slot,col=225,1,1'),
                  T('E', '5', S2, 'stage=2', extra='col=226,1,1')])
    check('stage line colour off by 1: col (tolerance 0 stays)', feats(res) == ['col'])

    # fallback without slot marker: track UV as a corner set (counted)
    res, _ = run([F(), T('R', '5', '7,0,0,0,7,7,0,7', 'cell=0,0'), T('E', '5', C00, 'cell=0,0')])
    check('track corner set: a mirrored corner order of a cell: 0, counted as set rule',
          len(res.open) == 0 and res.stats['t_set_rule'] == {'L3': 1})
    res, _ = run([F(), T('R', '5', '0,0,7,0,0,7', 'cell=0,0'), T('E', '5', C00, 'cell=0,0')])
    check('track corner set: a GT3 with 3 of the 4 corners: 0', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', '0,0,7,0,0,8', 'cell=0,0'), T('E', '5', C00, 'cell=0,0')])
    check('track corner set: a corner not among the engine corners: uv', feats(res) == ['uv'])
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='col=1,1,1;2,2,2;3,3,3;4,4,4'),
                  T('E', '5', '7,0,0,0,7,7,0,7', 'cell=0,0', extra='col=2,2,2;1,1,1;4,4,4;3,3,3')])
    check('track corner set: colours paired by UV point: 0', len(res.open) == 0)
    res, _ = run([F(), T('R', '5', C00, 'cell=0,0', extra='col=1,1,1;2,2,2;3,3,3;4,4,4'),
                  T('E', '5', C00, 'cell=0,0', extra='col=2,2,2;1,1,1;4,4,4;3,3,3')])
    check('track corner set: a colour on another UV point: col', feats(res) == ['col'])

    # track markers (track oracle)
    S2 = '0,0,15,0,0,15,15,15'
    res, _ = run([F(), T('R', '5', '2,3,15,3,2,15,15,15', 'stage=2', extra='part=1'), T('E', '5', S2, 'stage=2')])
    check('part=1: corners inside the box of its key, same material: 0',
          len(res.open) == 0 and res.stats['part'] == {'L3': 1})
    res, _ = run([F(), T('R', '5', '2,3,16,3,2,15,16,15', 'stage=2', extra='part=1'), T('E', '5', S2, 'stage=2')])
    check('part=1: a corner outside the box: uv', feats(res) == ['uv'] and res.open[0].pattern.endswith(' part'))
    res, _ = run([F(), T('R', '5', '2,3,15,3,2,15,15,15', 'stage=2', extra='part=1'),
                  T('E', '5', S2, 'stage=2', clut='0,481')])
    check('part=1: other CLUT: clut', feats(res) == ['clut'])
    res, _ = run([F(), T('R', '5', '9,1,15,1,9,6,15,6', 'cell=0,0', extra='part=1')] + eng_cells[:4])
    check('part=1: inside the box of another cell: cell', feats(res) == ['cell'] and
          res.open[0].pattern == 'cell 0,0>0,1 part')
    res, _ = run([F('t-load'), T('R', '5', S2, 'stage=0', extra='lodonly=1'), T('R', '5', S2, 'stage=2'),
                  T('E', '5', S2, 'stage=2')])
    check('lodonly=1 without engine partner: information',
          len(res.open) == 0 and res.stats['info_lines'] == {('L3', 'lodonly'): 1})
    res, _ = run([F('t-load'), T('R', '5', C00, 'stage=1', extra='lodonly=1'), T('R', '5', S2, 'stage=2'),
                  T('E', '5', S2, 'stage=2'), T('E', '5', C00, 'stage=1', clut='0,481')])
    check('lodonly=1 with engine partner: compared', feats(res) == ['clut'])
    res, _ = run([F('t-load'), T('R', '9', S2, 'stage=2', extra='noleaf=1')])
    check('noleaf=1 without engine partner: information, not missing-source',
          len(res.open) == 0 and res.stats['info_lines'] == {('L3', 'noleaf'): 1})
    res, _ = run([F(), T('R', '5', S2, 'stage=none'), T('E', '5', S2, 'stage=2')])
    check('stage=none: own class', feats(res) == ['stage-none'] and res.stats['stage_none'] == {'L3': 1})
    res, _ = run([F(), L('R', 'T', '5', uv='192,0,192,63,255,63', extra='nogeo=1'), T('E', '5', S2, 'stage=2')])
    check('nogeo=1: counted only', len(res.open) == 0 and res.stats['nogeo'] == {'L3': 1})
    res, _ = run([F(), T('R', '5', S2, 'stage=2', extra='clip=1'), T('E', '5', S2, 'stage=2', clut='0,481')])
    check('clip=1: counted and compared', feats(res) == ['clut'] and res.stats['clip'] == {'L3': 1})
    res, _ = run([F(), T('R', '5', S2, 'stage=2'), T('E', '5', S2, 'stage=2'),
                  T('R', '5', S2, 'stage=1', extra='set=anim,anim=1'), T('E', '5', S2, 'stage=1', extra='set=anim,anim=1'),
                  T('E', '5', C00, 'cell=0,0', extra='set=anim,anim=1')])
    check('set=anim: compared on its own, engine entries without retail line are counted',
          len(res.open) == 0 and res.stats['anim_gap'] == 1 and res.stats['gap'] == 0)
    res, _ = run([F(), T('R', '5', S2, 'stage=2'), T('E', '5', S2, 'stage=2'),
                  T('R', '5', S2, 'stage=1', clut='0,481', extra='set=anim,anim=1'),
                  T('E', '5', S2, 'stage=1', extra='set=anim,anim=1,frozen=1')])
    check('set=anim: a frozen face whose animation moved on: anim+clut frozen',
          feats(res) == ['anim+clut'] and res.open[0].pattern.endswith('frozen'))

    # (e) missing source, route=retail
    res, _ = run(base_r + [L('E', 'I', 'tikka#1', uv=TRI_A)])
    check('(e) missing: one missing-source record', feats(res) == ['missing-source'] and len(res.open) == 1)
    res, _ = run([F(), L('R', 'I', 'crate#0', uv=QUAD, extra='route=retail')])
    check('(e) route=retail: no deviation, listed',
          len(res.open) == 0 and res.stats['route_retail'] == {'L3': set(['I:crate'])})

    # (f) direction 2
    res, _ = run([F(), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', clut='0,500', uv=TRI_A),
                  L('E', 'I', 'tikka#0', clut='0,501', uv=TRI_A)])
    check('(f) engine-only-material: retail drew the triangle with a material the engine lacks for it',
          feats(res, 'd2') == ['engine-only-material'] and feats(res) == ['clut'])
    res, _ = run(base_r + eng_ok + [L('E', 'I', 'tikka#0', clut='0,500', uv=TRI_A)])
    check('(f) a second engine copy of a triangle the engine also draws with retail material: coverage',
          len(res.open) == 0 and len(res.stats['eom_coverage']) == 1)
    res, _ = run([F(), L('R', 'I', 'crystal#0', uv=TRI_A, extra='prim=key'),
                  L('E', 'I', 'crystal#0', uv=TRI_A),
                  L('E', 'I', 'crystal#0', uv=TRI_A, blend='2', extra='side=back')])
    check('(f) back-side copy (side=back) of a model retail never showed from the back: coverage',
          len(res.open) == 0 and len(res.stats['back_coverage']) + len(res.stats['eom_coverage']) == 1)
    res, _ = run([F(), L('R', 'I', 'crystal#0', uv=TRI_A, extra='prim=key'),
                  L('R', 'I', 'crystal#0', uv='0,0,0,31,31,0', blend='1', extra='prim=key,side=back'),
                  L('E', 'I', 'crystal#0', uv=TRI_A),
                  L('E', 'I', 'crystal#0', uv=TRI_A, blend='2', extra='side=back')])
    check('(f) a back side retail showed with another blend stays a deviation',
          feats(res) == ['prim+blend'])
    res, _ = run(base_r + eng_ok + [L('E', 'I', 'tikka#0', clut='0,500', uv='64,0,95,0,64,31')])
    check('(f) an engine material on triangles retail never drew is coverage, no deviation',
          len(res.open) == 0 and res.stats['eom_coverage'] == [('L3', 'I:tikka', ((512, 0), 0, (0, 500), 'o'))])
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
    res, _ = run([F('L3-load'), T('R', '5', C00, 'cell=0,0'), T('R', '5', C01, 'cell=0,1'),
                  T('E', '5', '7,0,0,0,7,7,0,7', 'cell=0,0', extra='fault=cell'),
                  T('E', '5', C01, 'cell=0,1', extra='fault=cell'),
                  T('R', '6', C00, 'cell=0,0'), T('R', '6', C01, 'cell=0,1'),
                  T('E', '6', C01, 'cell=0,0', extra='fault=cell'), T('E', '6', C00, 'cell=0,1', extra='fault=cell')],
                 expect=['fault:cell'])
    check('(h) cell fault that only turns a cell (same corner set) is counted apart, not required',
          res.control.startswith('yes') and '1 differ only in orientation' in res.control and
          res.stats['fault_orient'] == set([('cell', ('L3', 'T:5'))]))
    res, _ = run([F('L3-load'), T('R', '5', C00, 'cell=0,0'), T('R', '5', C01, 'cell=0,1'), T('R', '5', C10, 'cell=1,0'),
                  T('E', '5', C00, 'cell=0,0', clut='0,481', extra='fault=cell'),
                  T('E', '5', C10, 'cell=0,1', extra='fault=cell'), T('E', '5', C01, 'cell=1,0', extra='fault=cell')],
                 expect=['fault:cell'])
    check('(h) cell fault marks the row: unswapped cells are no deviation, other features on fault lines stay deviations',
          len(res.control_open) == 2 and [r.feature_label() for r in res.stray] == ['clut'] and
          res.others == res.stray and 'other deviation(s) on fault lines' in res.control and res.exit == 1)

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
                  L('E', 'I', 'fruit#0', page='576,0,0', uv='0,0,7,0,0,7', extra='lod=0')])
    check('(j) lod=1: one LOD-header record, not compared',
          [(r.feature_label(), r.r_count, r.pattern) for r in res.open if r.kind == 'd1'] == [('lod', 2, 'lod 1>0')])
    check('(j) lod=1: header 0 never compared is information with the model, no deviation',
          feats(res, 'd2') == [] and res.stats['lod_never'] == set([('L3', 'I:fruit')]))
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
    res, _ = run([F(extra='vram=v.tga'), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv=TRI_A),
                  T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0'),
                  L('E', 'X', 'exhaust', uv=TRI_A), L('E', 'D', '2', page='-', clut='-', uv='-', extra='native=1'),
                  L('R', 'W', '379', uv=TRI_A), L('R', 'S', 'sky', uv=TRI_A)], plaus=True, vram=vr)
    check('plausibility: one-sided X, D native, W, S are listed, not a failure',
          res.plausible == [] and len(res.one_sided) == 4 and res.exit != 3)
    res, _ = run([F('L3-load'), T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0'),
                  F(extra='vram=v.tga'), L('R', 'I', 'tikka#0', uv=TRI_A), L('E', 'I', 'tikka#0', uv=TRI_A)],
                 plaus=True, vram=vr)
    check('plausibility: T only in the load block is a dead hook',
          res.exit == 3 and any('L3 T: no retail line in its race frames' in p for p in res.plausible))
    res, _ = run([F('menu', level=40, extra='vram=v.tga'), L('E', 'I', 'pimbel#0', uv=TRI_A)], plaus=True, vram=vr)
    check('plausibility: the menu scene needs no T, its one-sided kinds are listed',
          res.plausible == [] and len(res.one_sided) == 1)
    log = parse_log('\n'.join([F(), L('R', 'I', 'x#0', uv=TRI_A), L('E', 'I', 'x#0', uv=TRI_A)]) + '\n', 's')
    check('plausibility: missing H line', 'no H line' in ' '.join(plausibility(log, compare(log.frames)[1])[0]))

    # tracks are sections of the log: containers in the same level slot stay apart
    log2 = parse_log('\n'.join([
        F('L0-load', level=0), T('R', '5', C00, 'cell=0,0'), T('E', '5', C00, 'cell=0,0'),
        F('t01-L0-f300', level=0, extra='vram=v.tga'), L('R', 'I', 'door#0', uv=TRI_A), L('E', 'I', 'door#0', uv=TRI_A),
        F('L0-load', level=0, cont=1), T('R', '5', C01, 'cell=0,0'), T('E', '5', C01, 'cell=0,0'),
        F('t19-L65-start', level=0, cont=1), L('R', 'I', 'door#0', uv=TRI_A, clut='0,481'),
        L('E', 'I', 'door#0', uv=TRI_A, clut='0,481'),
        F('t19-L65-f300', level=0, cont=1), L('R', 'I', 'door#0', uv=TRI_A, clut='0,481'),
        L('E', 'I', 'door#0', uv=TRI_A, clut='0,481'),
        F('L0-load', level=0, cont=1), T('R', '5', C10, 'cell=0,0'), T('E', '5', C10, 'cell=0,0'),
        F('t20-L66-f300', level=0, cont=1), L('E', 'I', 'door#0', uv=TRI_A, clut='0,482'),
        F('menu', level=40)]) + '\n', 's')
    res2 = analyse(log2, rows, check_plausible=False)
    check('sections: each load block starts a track named by its first race frame (tNN-L<id>)',
          [f.track() for f in log2.frames] == ['t01-L0', 't01-L0', 't19-L65', 't19-L65', 't19-L65', 't20-L66', 't20-L66', 'L40']
          and res2.stats['tracks'] == ['L40', 't01-L0', 't19-L65', 't20-L66'])
    check('sections: containers of one slot do not mix (no engine-only-material across them)',
          len(res2.open) == 0 and sorted(r.track for r in res2.info) == ['t20-L66'])
    check('sections: a known positive names the section', parse_expect(['t04-L3:I:startbanner:sample-outside'])[1] ==
          [('t04-L3', 'I:startbanner', 'sample-outside')])

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
                     ([F(), L('R', 'I', 'x#0', extra='box=5,5,4,4')], 'box')):
        check('parse: %s is an error' % why, len(parse_log('\n'.join(bad) + '\n', 's').errors) == 1)
    log = parse_log('\n'.join(['H\tabc\t--engine', '# comment', '', F(extra='vram=a-1.tga'),
                               L('R', 'I', 'x#0', extra='anim=1;lod=2 hdr col=1,2,3;4,5,6;7,8,9;1,1,1 tp=0x7e,box=1,2,3,4'),
                               L('E', 'I', 'x#0', uv=TRI_A).rsplit('\t', 1)[0]]) + '\r\n', 's')
    ln = log.frames[0].sources[('I', 'x#0')]['R'][0] if log.frames else None
    log3 = parse_log('\n'.join([F(), L('R', 'T', '5', uv=QUAD, extra='col=1,2,3;4,5,6;7,8,9')]) + '\n', 's')
    ln3 = log3.frames[0].sources[('T', '5')]['R'][0]
    res, _ = run([F(), L('R', 'X', 'particle-g03#1', uv=TRI_A, extra='prim=particle,raw=1,col=40,90,200'),
                  L('E', 'X', 'particle-g03#1', uv=TRI_A, extra='prim=particle,col=128,128,128')])
    check('raw=1: the GPU does not modulate a raw texture, its colour is not compared (counted)',
          len(res.open) == 0 and res.stats['raw_col'] == 1)
    res, _ = run([F(), L('R', 'X', 'particle-g03#1', uv=TRI_A, extra='prim=particle,col=40,90,200'),
                  L('E', 'X', 'particle-g03#1', uv=TRI_A, extra='prim=particle,col=128,128,128')])
    check('without raw=1 the colour is compared: col', feats(res) == ['col'])
    res, _ = run([F(), L('R', 'X', 'particle-g03#1', uv=TRI_A, extra='prim=particle,raw=1,col=40,90,200'),
                  L('E', 'X', 'particle-g03#1', uv=TRI_A, clut='0,481', extra='prim=particle,col=128,128,128')])
    check('raw=1 still compares the CLUT', feats(res) == ['clut'])
    check('parse: three colours on a quad are kept unpaired (not compared)',
          not log3.errors and ln3.col is None and ln3.colbad and ln3.extra['col'] == '1,2,3;4,5,6;7,8,9')
    res, _ = run([F(), L('R', 'I', 'q#0', uv=QUAD, extra='col=1,2,3;4,5,6;7,8,9'),
                  L('E', 'I', 'q#0', uv=TRI_A, extra='col=9,9,9'), L('E', 'I', 'q#0', uv=TRI_B, extra='col=9,9,9')])
    check('unpaired colours: not compared, counted', len(res.open) == 0 and res.stats['col_unpaired'] == 1)
    check('parse: H, comments, CRLF, extra values with separators, 9-field line',
          not log.errors and log.headers == ['abc --engine'] and log.frames[0].vram == 'a-1.tga' and ln is not None
          and ln.extra == {'anim': '1', 'lod': '2', 'hdr': '1', 'col': '1,2,3;4,5,6;7,8,9;1,1,1', 'tp': '0x7e',
                           'box': '1,2,3,4'} and ln.col[3] == (1, 1, 1) and ln.box == (1, 2, 3, 4))

    # exception lists
    with open(DEFAULT_EXCEPTIONS, encoding='utf-8') as fh:
        repo_rows, repo_errs = parse_exceptions(fh.read(), DEFAULT_EXCEPTIONS)
    ids = set(x.id for x in repo_rows)
    check('repo exception list parses with exactly the agreed rows',
          not repo_errs and ids == set(['sky-look', 'water-look', 'hires-driver', 'depth-offset', 'lod-header',
                                        'track-geomorph']) and
          [x.id for x in repo_rows if x.doc] == ['depth-offset'])
    res, _ = run([F(), L('R', 'T', '5.0', uv=QUAD, extra='stage=2,morph=1,col=10,10,10'),
                  L('E', 'T', '5.0', uv=QUAD, extra='stage=2,col=20,20,20'),
                  L('R', 'T', '6.0', uv=QUAD, extra='stage=2,col=10,10,10'),
                  L('E', 'T', '6.0', uv=QUAD, extra='stage=2,col=20,20,20'),
                  L('R', 'T', '7.0', uv=QUAD, extra='stage=2,morph=1,col=10,10,10'),
                  L('E', 'T', '7.0', uv=QUAD, clut='0,481', extra='stage=2,col=20,20,20')], rws=repo_rows)
    check('repo exception list: track-geomorph covers only track colour with r.morph=1',
          [(r.srcId, r.excepted) for r in res.records if r.excepted] == [('5.0', 'track-geomorph')] and
          sorted((r.srcId, r.feature_label()) for r in res.open) == [('6.0', 'col'), ('7.0', 'clut+col')])
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
