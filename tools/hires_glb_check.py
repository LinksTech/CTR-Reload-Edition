#!/usr/bin/env python3
"""Standalone GLB checker for hi-res drivers (format: the hi-res driver format document, sections 3-5).

  python tools/hires_glb_check.py check <file.glb> [--expect-clips all] [--min-joints N]
                                  [--expect-retail-wheels] [--expect-alpha] [--json <out.json>]
  python tools/hires_glb_check.py selftest

Findings (one line each):
  ERROR <rule>   the file is refused by the game
  WARN <rule>    the file runs, but something falls back to a default
  EXPECT <rule>  an --expect-* / --min-joints expectation is not met
  INFO <text>    facts
Last line: "glb-check: <name> errors E warnings W expect X triangles T clips C/12 wheels N/4
            skins S joints J images <list> sha256 <hex>".
Exit code: 0 when E = 0 and X = 0, 1 otherwise, 2 on usage errors.

Standard library only (struct, json, zlib, hashlib, argparse, math).
Axes: +X = kart left, +Y up, +Z front, metres.
"""
import argparse
import hashlib
import json
import math
import struct
import sys
import zlib

CLIP_NAMES = ["idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right",
              "hit", "podium_1", "podium_2", "podium_3", "lose", "menu"]
WHEEL_NAMES = ["wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"]
RETAIL_WHEELS = {"wheel_fl": (0.5625, 0.25, 0.7773), "wheel_fr": (-0.5625, 0.25, 0.7773),
                 "wheel_rl": (0.5625, 0.25, -0.375), "wheel_rr": (-0.5625, 0.25, -0.375)}
RETAIL_RADIUS = 0.25
MAX_JOINTS = 64
MAX_EDGE = 2048
MAX_HULL = 4.0
PNG_SIG = b"\x89PNG\r\n\x1a\n"
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942
COMP = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NORM_DIV = {"b": 127.0, "B": 255.0, "h": 32767.0, "H": 65535.0}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
PNG_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


# ---------------------------------------------------------------- report

class Report:
    def __init__(self):
        self.items = []  # (level, rule, text)
        self.clips = []
        self.wheels = []
        self.images = []
        self.sum = {"triangles": 0, "clips": 0, "wheels": 0, "skins": 0, "joints": 0}

    def add(self, level, rule, text):
        self.items.append((level, rule, text))

    def count(self, level):
        return sum(1 for it in self.items if it[0] == level)

    def has(self, level, rule):
        return any(it[0] == level and it[1] == rule for it in self.items)

    def lines(self):
        out = []
        for level, rule, text in self.items:
            out.append("INFO " + text if level == "INFO" else "%s %s: %s" % (level, rule, text))
        return out


# ---------------------------------------------------------------- matrices (column-major 4x4)

IDENT = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def mat_mul(a, b):
    r = [0.0] * 16
    for c in range(4):
        for row in range(4):
            r[c * 4 + row] = sum(a[k * 4 + row] * b[c * 4 + k] for k in range(4))
    return r


def trs_matrix(t, q, s):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w)
    if n > 0:
        x, y, z, w = x / n, y / n, z / n, w / n
    rot = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
           [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
           [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    m = [0.0] * 16
    for c in range(3):
        for r in range(3):
            m[c * 4 + r] = rot[r][c] * s[c]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m


def node_local(node):
    if "matrix" in node and len(node["matrix"]) == 16:
        return [float(v) for v in node["matrix"]]
    return trs_matrix(node.get("translation", [0, 0, 0]), node.get("rotation", [0, 0, 0, 1]),
                      node.get("scale", [1, 1, 1]))


def xform_point(m, p):
    return (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14])


# ---------------------------------------------------------------- PNG

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def unfilter_rows(raw, w, h, bpp, rowbytes):
    """Return the unfiltered pixel bytes, or None when a filter type is invalid."""
    out = bytearray(h * rowbytes)
    prev = bytes(rowbytes)
    pos = 0
    for y in range(h):
        ft = raw[pos]
        row = bytearray(raw[pos + 1:pos + 1 + rowbytes])
        pos += 1 + rowbytes
        if ft == 0:
            pass
        elif ft == 1:
            for i in range(bpp, rowbytes):
                row[i] = (row[i] + row[i - bpp]) & 255
        elif ft == 2:
            row = bytearray((row[i] + prev[i]) & 255 for i in range(rowbytes))
        elif ft == 3:
            for i in range(rowbytes):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(rowbytes):
                left = row[i - bpp] if i >= bpp else 0
                ul = prev[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + paeth(left, prev[i], ul)) & 255
        else:
            return None
        out[y * rowbytes:(y + 1) * rowbytes] = row
        prev = row
    return out


def decode_png(b, keep_raw=False):
    """Decode a PNG (8-bit RGB / RGBA fully, other formats only the header)."""
    info = {"ok": False, "err": None, "w": 0, "h": 0, "depth": 0, "ctype": -1, "interlace": 0,
            "min_alpha": None, "decoded": False}
    if b[:8] != PNG_SIG:
        info["err"] = "bad PNG signature"
        return info
    pos = 8
    idat = bytearray()
    ihdr = None
    while pos + 8 <= len(b):
        ln, ty = struct.unpack_from(">I4s", b, pos)
        body = b[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if ty == b"IHDR" and len(body) >= 13:
            ihdr = struct.unpack(">IIBBBBB", body[:13])
        elif ty == b"IDAT":
            idat += body
        elif ty == b"IEND":
            break
    if ihdr is None:
        info["err"] = "no IHDR chunk"
        return info
    w, h, depth, ctype, _comp, _filt, interlace = ihdr
    info.update(w=w, h=h, depth=depth, ctype=ctype, interlace=interlace, ok=True)
    if depth != 8 or ctype not in (2, 6) or interlace != 0:
        return info
    if w * h > 64 * 1024 * 1024:
        info["err"] = "image too large to decode"
        return info
    ch = PNG_CHANNELS[ctype]
    rowbytes = w * ch
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as e:
        info["err"] = "IDAT zlib error: %s" % e
        return info
    if len(raw) != h * (1 + rowbytes):
        info["err"] = "IDAT length %d, expected %d" % (len(raw), h * (1 + rowbytes))
        return info
    pix = unfilter_rows(raw, w, h, ch, rowbytes)
    if pix is None:
        info["err"] = "invalid PNG filter type"
        return info
    info["decoded"] = True
    if ctype == 6 and len(pix):
        info["min_alpha"] = min(pix[3::4])
    if keep_raw:
        info["raw"] = bytes(pix)
    return info


# ---------------------------------------------------------------- GLB container

def parse_container(data, rep):
    """Return (gltf dict, bin bytes or None) or (None, None) when unusable."""
    if len(data) < 20:
        rep.add("ERROR", "glb-header", "file shorter than a GLB header (%d bytes)" % len(data))
        return None, None
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        rep.add("ERROR", "glb-header", "magic is %r, expected b'glTF'" % magic)
        return None, None
    if version != 2:
        rep.add("ERROR", "glb-header", "GLB version %d, expected 2" % version)
    if length != len(data):
        rep.add("ERROR", "glb-length", "header length %d differs from file size %d" % (length, len(data)))
    pos = 12
    chunks = []
    while pos + 8 <= len(data):
        clen, ctype = struct.unpack_from("<II", data, pos)
        if pos + 8 + clen > len(data):
            rep.add("ERROR", "glb-header", "chunk %d (%d bytes) reaches past the end of the file" % (len(chunks), clen))
            break
        if clen % 4:
            rep.add("ERROR", "glb-align", "chunk %d length %d is not 4-byte aligned" % (len(chunks), clen))
        chunks.append((ctype, data[pos + 8:pos + 8 + clen]))
        pos += 8 + clen
    if not chunks or chunks[0][0] != CHUNK_JSON:
        rep.add("ERROR", "glb-header", "chunk 0 is not JSON")
        return None, None
    try:
        gltf = json.loads(chunks[0][1].decode("utf-8").rstrip(" \x00"))
    except (ValueError, UnicodeDecodeError) as e:
        rep.add("ERROR", "glb-json", "JSON chunk does not parse: %s" % e)
        return None, None
    if not isinstance(gltf, dict):
        rep.add("ERROR", "glb-json", "JSON root is not an object")
        return None, None
    binchunk = None
    if len(chunks) > 1:
        if chunks[1][0] != CHUNK_BIN:
            rep.add("ERROR", "glb-header", "chunk 1 is not BIN")
        else:
            binchunk = chunks[1][1]
    return gltf, binchunk


# ---------------------------------------------------------------- analysis context

class Ctx:
    def __init__(self, gltf, binbytes, rep):
        self.g = gltf
        self.bin = binbytes if binbytes is not None else b""
        self.has_bin = binbytes is not None
        self.rep = rep
        self.bad = set()
        self.cache = {}

    def lst(self, key):
        v = self.g.get(key, [])
        return v if isinstance(v, list) else []

    def validate(self):
        rep = self.rep
        bufs = self.lst("buffers")
        limit = len(self.bin)
        for i, b in enumerate(bufs):
            if "uri" in b:
                uri = str(b["uri"])
                rep.add("ERROR", "glb-uri", "buffer %d has a %s uri; only the embedded BIN chunk is allowed"
                        % (i, "data:" if uri.startswith("data:") else "external"))
            elif i == 0:
                if not self.has_bin:
                    rep.add("ERROR", "glb-header", "buffers[0] has no uri but the file has no BIN chunk")
                elif b.get("byteLength", 0) > len(self.bin):
                    rep.add("ERROR", "buf-range", "buffers[0].byteLength %d exceeds the BIN chunk (%d)"
                            % (b.get("byteLength", 0), len(self.bin)))
                else:
                    limit = len(self.bin)
            else:
                rep.add("ERROR", "glb-header", "buffer %d has no uri (only buffers[0] may use the BIN chunk)" % i)
        views = self.lst("bufferViews")
        self.view_ok = []
        for i, v in enumerate(views):
            ok = True
            if v.get("buffer", 0) != 0 or not self.has_bin:
                rep.add("ERROR", "buf-range", "bufferView %d does not use the BIN buffer" % i)
                ok = False
            elif v.get("byteOffset", 0) + v.get("byteLength", 0) > limit:
                rep.add("ERROR", "buf-range", "bufferView %d (offset %d length %d) reaches past the buffer (%d)"
                        % (i, v.get("byteOffset", 0), v.get("byteLength", 0), limit))
                ok = False
            self.view_ok.append(ok)
        for i, a in enumerate(self.lst("accessors")):
            if "sparse" in a:
                rep.add("WARN", "acc-sparse", "accessor %d is sparse; sparse data is not read" % i)
            ct = COMP.get(a.get("componentType"))
            nc = NCOMP.get(a.get("type"))
            if ct is None or nc is None:
                rep.add("ERROR", "acc-type", "accessor %d has componentType %r / type %r"
                        % (i, a.get("componentType"), a.get("type")))
                self.bad.add(i)
                continue
            if "bufferView" not in a:
                continue
            bvi = a["bufferView"]
            if not isinstance(bvi, int) or bvi < 0 or bvi >= len(views) or not self.view_ok[bvi]:
                rep.add("ERROR", "acc-range", "accessor %d uses a missing or invalid bufferView %r" % (i, bvi))
                self.bad.add(i)
                continue
            esize = ct[1] * nc
            stride = views[bvi].get("byteStride") or esize
            count = a.get("count", 0)
            need = a.get("byteOffset", 0) + (stride * (count - 1) + esize if count > 0 else 0)
            if need > views[bvi].get("byteLength", 0):
                rep.add("ERROR", "acc-range", "accessor %d (count %d) needs %d bytes, its bufferView %d has %d"
                        % (i, count, need, bvi, views[bvi].get("byteLength", 0)))
                self.bad.add(i)

    def read(self, idx):
        """Accessor -> list of tuples (floats for float/normalized, ints otherwise), or None."""
        if idx in self.cache:
            return self.cache[idx]
        res = None
        accs = self.lst("accessors")
        if isinstance(idx, int) and 0 <= idx < len(accs) and idx not in self.bad:
            a = accs[idx]
            code, csize = COMP[a["componentType"]]
            nc = NCOMP[a["type"]]
            count = a.get("count", 0)
            if "bufferView" not in a:
                res = [tuple([0.0 if code == "f" else 0] * nc) for _ in range(count)]
            else:
                bv = self.lst("bufferViews")[a["bufferView"]]
                base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
                stride = bv.get("byteStride") or csize * nc
                fmt = "<" + code * nc
                norm = a.get("normalized", False) and code in NORM_DIV
                res = []
                for k in range(count):
                    t = struct.unpack_from(fmt, self.bin, base + k * stride)
                    if norm:
                        t = tuple(max(x / NORM_DIV[code], -1.0) for x in t)
                    res.append(t)
        self.cache[idx] = res
        return res

    def bounds(self, idx):
        """(min, max) of a VEC3 accessor from its min/max, else computed; None if unavailable."""
        accs = self.lst("accessors")
        if not (isinstance(idx, int) and 0 <= idx < len(accs)) or idx in self.bad:
            return None
        a = accs[idx]
        mn, mx = a.get("min"), a.get("max")
        if isinstance(mn, list) and isinstance(mx, list) and len(mn) >= 3 and len(mx) >= 3:
            return [float(v) for v in mn[:3]], [float(v) for v in mx[:3]]
        pts = self.read(idx)
        if not pts or len(pts[0]) < 3:
            return None
        return ([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)])


def fmt_vec(v):
    return "(%.4f, %.4f, %.4f)" % tuple(v)


# ---------------------------------------------------------------- the checks

def analyze(data, name, opts):
    rep = Report()
    sha = hashlib.sha256(data).hexdigest()
    gltf, binbytes = parse_container(data, rep)
    if gltf is not None:
        run_checks(gltf, binbytes, rep, opts)
    return rep, name, sha


def run_checks(gltf, binbytes, rep, opts):
    ctx = Ctx(gltf, binbytes, rep)
    for ext in gltf.get("extensionsRequired", []) or []:
        if ext != "KHR_materials_unlit":
            rep.add("ERROR", "glb-ext-required", "extensionsRequired contains %s (only KHR_materials_unlit is allowed)" % ext)
    ctx.validate()
    nodes = ctx.lst("nodes")
    meshes = ctx.lst("meshes")
    skins = ctx.lst("skins")

    # ---- scene tree
    scenes = ctx.lst("scenes")
    if scenes:
        si = gltf.get("scene", 0)
        if not isinstance(si, int) or si < 0 or si >= len(scenes):
            rep.add("WARN", "glb-scene", "scene index %r is invalid, using scenes[0]" % si)
            si = 0
        roots = list(scenes[si].get("nodes", []))
    else:
        children = {c for n in nodes for c in n.get("children", [])}
        roots = [i for i in range(len(nodes)) if i not in children]
        if nodes:
            rep.add("WARN", "glb-scene", "no scenes; using all parentless nodes as roots")
    world = {}
    order = []
    stack = [(r, IDENT) for r in reversed(roots)]
    while stack:
        ni, pm = stack.pop()
        if not isinstance(ni, int) or ni < 0 or ni >= len(nodes):
            rep.add("ERROR", "node-index", "scene references missing node %r" % (ni,))
            continue
        if ni in world:
            rep.add("ERROR", "node-cycle", "node %d is reached twice (cycle or shared child)" % ni)
            continue
        wm = mat_mul(pm, node_local(nodes[ni]))
        world[ni] = wm
        order.append(ni)
        for c in reversed(nodes[ni].get("children", [])):
            stack.append((c, wm))

    def nname(i):
        return nodes[i].get("name", "#%d" % i) if 0 <= i < len(nodes) else "#%d" % i

    # ---- skins
    total_joints = 0
    skin_joint_counts = []
    if len(skins) > 1:
        rep.add("ERROR", "skin-count", "%d skins; at most one is allowed" % len(skins))
    for si, sk in enumerate(skins):
        js = sk.get("joints", [])
        skin_joint_counts.append(len(js))
        total_joints += len(js)
        if len(js) > MAX_JOINTS:
            rep.add("ERROR", "skin-joints", "skin %d has %d joints; at most %d are allowed" % (si, len(js), MAX_JOINTS))
        for j in js:
            if not isinstance(j, int) or j < 0 or j >= len(nodes):
                rep.add("ERROR", "skin-joints", "skin %d lists missing joint node %r" % (si, j))
    rep.sum["skins"] = len(skins)
    rep.sum["joints"] = total_joints
    if opts.get("min_joints") is not None and total_joints < opts["min_joints"]:
        rep.add("EXPECT", "skin-minjoints", "%d joints, expected at least %d" % (total_joints, opts["min_joints"]))

    # ---- meshes
    minfo = {}

    def mesh_info(mi):
        if mi in minfo:
            return minfo[mi]
        info = {"tris": 0, "bmin": None, "bmax": None}
        minfo[mi] = info
        if not (isinstance(mi, int) and 0 <= mi < len(meshes)):
            rep.add("ERROR", "mesh-index", "node references missing mesh %r" % (mi,))
            return info
        accs = ctx.lst("accessors")
        for pi, p in enumerate(meshes[mi].get("primitives", [])):
            tag = "mesh %d primitive %d" % (mi, pi)
            mode = p.get("mode", 4)
            attrs = p.get("attributes", {})
            if mode != 4:
                rep.add("ERROR", "prim-mode", "%s uses mode %r; only triangles (4) are allowed" % (tag, mode))
            pos = attrs.get("POSITION")
            if pos is None:
                rep.add("ERROR", "prim-position", "%s has no POSITION attribute" % tag)
            elif not (isinstance(pos, int) and 0 <= pos < len(accs)):
                rep.add("ERROR", "prim-position", "%s POSITION accessor %r does not exist" % (tag, pos))
                pos = None
            if "JOINTS_1" in attrs or "WEIGHTS_1" in attrs:
                rep.add("ERROR", "skin-weights4", "%s has JOINTS_1/WEIGHTS_1; only 4 weights per vertex are allowed" % tag)
            if mode == 4 and pos is not None:
                ind = p.get("indices")
                cnt = accs[ind].get("count", 0) if isinstance(ind, int) and 0 <= ind < len(accs) else accs[pos].get("count", 0)
                info["tris"] += cnt // 3
                if isinstance(ind, int) and 0 <= ind < len(accs) and "max" in accs[ind] and accs[ind]["max"]:
                    if accs[ind]["max"][0] >= accs[pos].get("count", 0):
                        rep.add("ERROR", "prim-index", "%s index max %s >= vertex count %d"
                                % (tag, accs[ind]["max"][0], accs[pos].get("count", 0)))
            if pos is not None:
                b = ctx.bounds(pos)
                if b:
                    if info["bmin"] is None:
                        info["bmin"], info["bmax"] = list(b[0]), list(b[1])
                    else:
                        info["bmin"] = [min(x, y) for x, y in zip(info["bmin"], b[0])]
                        info["bmax"] = [max(x, y) for x, y in zip(info["bmax"], b[1])]
        return info

    instances = [ni for ni in order if "mesh" in nodes[ni]]
    tris = 0
    checked_skinned = set()
    for ni in instances:
        mi = nodes[ni]["mesh"]
        tris += mesh_info(mi)["tris"]
        if "skin" in nodes[ni] and isinstance(mi, int) and 0 <= mi < len(meshes):
            sk = nodes[ni]["skin"]
            if (mi, sk) in checked_skinned:
                continue
            checked_skinned.add((mi, sk))
            njoints = skin_joint_counts[sk] if isinstance(sk, int) and 0 <= sk < len(skin_joint_counts) else 0
            check_skinned_mesh(ctx, rep, mi, njoints)
    rep.sum["triangles"] = tris

    # ---- images and materials
    images = ctx.lst("images")
    views = ctx.lst("bufferViews")
    img_info = []
    for ii, im in enumerate(images):
        tag = "image %d" % ii
        if "uri" in im:
            uri = str(im["uri"])
            rep.add("ERROR", "glb-uri", "%s has a %s uri; images must be embedded via bufferView"
                    % (tag, "data:" if uri.startswith("data:") else "external"))
            img_info.append(None)
            continue
        bvi = im.get("bufferView")
        if not (isinstance(bvi, int) and 0 <= bvi < len(views) and ctx.view_ok[bvi]):
            rep.add("ERROR", "img-source", "%s has no valid bufferView" % tag)
            img_info.append(None)
            continue
        bv = views[bvi]
        raw = ctx.bin[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv.get("byteLength", 0)]
        mime = im.get("mimeType", "")
        if mime == "image/jpeg" or raw[:3] == b"\xff\xd8\xff":
            rep.add("ERROR", "tex-jpeg", "%s is JPEG; only PNG is allowed" % tag)
            img_info.append(None)
            continue
        if mime != "image/png":
            rep.add("ERROR", "tex-format", "%s has mimeType %r; expected image/png" % (tag, mime))
        d = decode_png(raw)
        if not d["ok"]:
            rep.add("ERROR", "tex-format", "%s: %s" % (tag, d["err"]))
            img_info.append(None)
            continue
        if max(d["w"], d["h"]) > MAX_EDGE:
            rep.add("ERROR", "tex-edge", "%s is %dx%d; the edge limit is %d" % (tag, d["w"], d["h"], MAX_EDGE))
        if d["depth"] != 8 or d["ctype"] not in (2, 6) or d["interlace"]:
            rep.add("WARN", "tex-format", "%s is %d-bit colour type %d%s; only 8-bit RGB/RGBA is supported"
                    % (tag, d["depth"], d["ctype"], " interlaced" if d["interlace"] else ""))
        elif not d["decoded"]:
            rep.add("WARN", "tex-decode", "%s could not be decoded: %s" % (tag, d["err"]))
        label = "%dx%dct%d" % (d["w"], d["h"], d["ctype"])
        if d["min_alpha"] is not None:
            label += "a%d" % d["min_alpha"]
        rep.images.append({"index": ii, "w": d["w"], "h": d["h"], "depth": d["depth"], "colour_type": d["ctype"],
                           "min_alpha": d["min_alpha"], "label": label})
        img_info.append(d)
    textures = ctx.lst("textures")
    mask_ok = False
    for mi, m in enumerate(ctx.lst("materials")):
        mode = m.get("alphaMode", "OPAQUE")
        if mode == "BLEND":
            rep.add("WARN", "alpha-blend", "material %d (%s) uses BLEND; it is drawn as MASK" % (mi, m.get("name", "")))
        if mode == "MASK":
            bct = (m.get("pbrMetallicRoughness", {}) or {}).get("baseColorTexture", {})
            ti = bct.get("index") if isinstance(bct, dict) else None
            if isinstance(ti, int) and 0 <= ti < len(textures):
                src = textures[ti].get("source")
                if isinstance(src, int) and 0 <= src < len(img_info) and img_info[src]:
                    d = img_info[src]
                    if d["ctype"] == 6 and d["min_alpha"] is not None and d["min_alpha"] < 255:
                        mask_ok = True
    if opts.get("expect_alpha") and not mask_ok:
        rep.add("EXPECT", "alpha-mask", "no MASK material with a colour-type-6 base texture having alpha < 255")

    # ---- animations
    seen_names = {}
    outputs_seen = {}
    for ai, an in enumerate(ctx.lst("animations")):
        cname = an.get("name", "")
        samplers = an.get("samplers", [])
        chans = an.get("channels", [])
        dur = 0.0
        interps = set()
        targets = []
        static = True
        digest = hashlib.sha1()
        for ch in chans:
            sm = ch.get("sampler")
            if not (isinstance(sm, int) and 0 <= sm < len(samplers)):
                rep.add("ERROR", "clip-sampler", "clip %r channel uses missing sampler %r" % (cname, sm))
                static = False
                continue
            s = samplers[sm]
            interp = s.get("interpolation", "LINEAR")
            interps.add(interp)
            tn = ch.get("target", {}).get("node")
            if not (isinstance(tn, int) and 0 <= tn < len(nodes)):
                rep.add("ERROR", "clip-target", "clip %r targets missing node %r" % (cname, tn))
            else:
                if nname(tn) not in targets:
                    targets.append(nname(tn))
            ia = s.get("input")
            accs = ctx.lst("accessors")
            if isinstance(ia, int) and 0 <= ia < len(accs) and ia not in ctx.bad:
                mx = accs[ia].get("max")
                if isinstance(mx, list) and mx:
                    dur = max(dur, float(mx[0]))
                else:
                    pts = ctx.read(ia)
                    if pts:
                        dur = max(dur, max(p[0] for p in pts))
            out = ctx.read(s.get("output"))
            if out is None:
                static = False
                continue
            digest.update(repr(out).encode())
            if any(any(abs(a - b) > 1e-9 for a, b in zip(row, out[0])) for row in out):
                static = False
        for iname in interps:
            if iname == "CUBICSPLINE":
                rep.add("WARN", "clip-cubic", "clip %r uses CUBICSPLINE; it is read as linear between the key values" % cname)
        if cname in CLIP_NAMES:
            if cname in seen_names:
                rep.add("WARN", "clip-dupname", "clip name %r appears twice" % cname)
            seen_names[cname] = True
        else:
            rep.add("WARN", "clip-unknown", "clip %r is not one of the 12 clip names; ignored" % cname)
        if static:
            rep.add("WARN", "clip-static", "clip %r has constant output on every channel" % cname)
        if chans:
            key = digest.hexdigest()
            if key in outputs_seen:
                rep.add("WARN", "clip-duplicate", "clip %r has the same sampler output as clip %r" % (cname, outputs_seen[key]))
            else:
                outputs_seen[key] = cname
        short = targets[:3]
        tstr = ",".join(short) + ("+%d" % (len(targets) - 3) if len(targets) > 3 else "")
        rep.clips.append({"name": cname, "duration": dur, "channels": len(chans),
                          "interpolation": sorted(interps), "targets": targets, "static": static})
        rep.add("INFO", "", "clip %s duration %.3fs channels %d interp %s targets %s"
                % (cname, dur, len(chans), "/".join(sorted(interps)) or "-", tstr or "-"))
    present = [c for c in CLIP_NAMES if c in seen_names]
    rep.sum["clips"] = len(present)
    missing = [c for c in CLIP_NAMES if c not in seen_names]
    if missing:
        if opts.get("expect_clips") == "all":
            rep.add("EXPECT", "clip-missing", "missing clips: %s" % ", ".join(missing))
        else:
            rep.add("WARN", "clip-missing", "missing clips (fall back to defaults): %s" % ", ".join(missing))

    # ---- wheels
    found = {}
    for ni in order:
        nm = nodes[ni].get("name")
        if nm in WHEEL_NAMES and nm not in found:
            found[nm] = ni
    for nm in WHEEL_NAMES:
        if nm not in found:
            continue
        ni = found[nm]
        n = nodes[ni]
        w = world[ni]
        pos = (w[12], w[13], w[14])
        radius = None
        has_mesh = "mesh" in n
        if not has_mesh:
            rep.add("WARN", "wheel-nomesh", "%s has no mesh" % nm)
        else:
            mi = mesh_info(n["mesh"])
            if mi["bmin"]:
                radius = max(abs(mi["bmin"][1]), abs(mi["bmax"][1]), abs(mi["bmin"][2]), abs(mi["bmax"][2]))
        if "skin" in n:
            rep.add("WARN", "wheel-skinned", "%s is skinned; wheels must not be skinned" % nm)
        rep.wheels.append({"name": nm, "position": list(pos), "radius": radius, "mesh": has_mesh, "skinned": "skin" in n})
        rep.add("INFO", "", "wheel %s at %s radius %s" % (nm, fmt_vec(pos), "%.4f" % radius if radius is not None else "-"))
    rep.sum["wheels"] = len(found)
    if 0 < len(found) < 4:
        rep.add("WARN", "wheel-partial", "only %d of 4 wheel nodes found (%s); all four or none"
                % (len(found), ", ".join(sorted(found))))
    if opts.get("retail_wheels"):
        wd = {w["name"]: w for w in rep.wheels}
        for nm in WHEEL_NAMES:
            w = wd.get(nm)
            if w is None or w["radius"] is None:
                rep.add("EXPECT", "wheel-retail", "%s missing or without mesh" % nm)
                continue
            exp = RETAIL_WHEELS[nm]
            dist = max(abs(a - b) for a, b in zip(w["position"], exp))
            if dist > 0.01:
                rep.add("EXPECT", "wheel-retail", "%s at %s is %.3f m off the retail centre %s"
                        % (nm, fmt_vec(w["position"]), dist, fmt_vec(exp)))
            if abs(w["radius"] - RETAIL_RADIUS) > 0.02:
                rep.add("EXPECT", "wheel-retail", "%s radius %.4f differs from retail %.2f" % (nm, w["radius"], RETAIL_RADIUS))

    # ---- hull
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    for ni in instances:
        mi = mesh_info(nodes[ni]["mesh"])
        if mi["bmin"] is None:
            continue
        for cx in (mi["bmin"][0], mi["bmax"][0]):
            for cy in (mi["bmin"][1], mi["bmax"][1]):
                for cz in (mi["bmin"][2], mi["bmax"][2]):
                    p = xform_point(world[ni], (cx, cy, cz))
                    for k in range(3):
                        lo[k] = min(lo[k], p[k])
                        hi[k] = max(hi[k], p[k])
    if lo[0] != float("inf"):
        ext = [hi[k] - lo[k] for k in range(3)]
        rep.add("INFO", "", "hull bounds min %s max %s size %s" % (fmt_vec(lo), fmt_vec(hi), fmt_vec(ext)))
        for k, ax in enumerate("XYZ"):
            if ext[k] > MAX_HULL:
                rep.add("ERROR", "hull-size", "hull extent along %s is %.3f m; the limit is %.1f m" % (ax, ext[k], MAX_HULL))
    else:
        rep.add("INFO", "", "no mesh bounds, hull not measured")
    rep.add("INFO", "", "nodes %d meshes %d materials %d textures %d images %d"
            % (len(nodes), len(meshes), len(ctx.lst("materials")), len(textures), len(images)))


def check_skinned_mesh(ctx, rep, mi, njoints):
    for pi, p in enumerate(ctx.lst("meshes")[mi].get("primitives", [])):
        tag = "mesh %d primitive %d" % (mi, pi)
        attrs = p.get("attributes", {})
        if "JOINTS_0" not in attrs or "WEIGHTS_0" not in attrs:
            rep.add("ERROR", "skin-attrs", "%s is skinned but lacks JOINTS_0/WEIGHTS_0" % tag)
            continue
        js = ctx.read(attrs["JOINTS_0"])
        ws = ctx.read(attrs["WEIGHTS_0"])
        if js is None or ws is None:
            continue
        badj = 0
        bads = 0
        for j, w in zip(js, ws):
            if any(wi > 0 and ji >= njoints for ji, wi in zip(j, w)):
                badj += 1
            if abs(sum(w) - 1.0) > 0.01:
                bads += 1
        if badj:
            rep.add("ERROR", "skin-joint-index", "%s: %d vertices use a joint index >= joint count %d" % (tag, badj, njoints))
        if bads:
            rep.add("WARN", "skin-weight-sum", "%s: %d vertices have a weight sum not within 0.01 of 1" % (tag, bads))


# ---------------------------------------------------------------- output

def summary_line(rep, name, sha):
    imgs = ",".join(i["label"] for i in rep.images) or "none"
    return ("glb-check: %s errors %d warnings %d expect %d triangles %d clips %d/12 wheels %d/4 skins %d joints %d images %s sha256 %s"
            % (name, rep.count("ERROR"), rep.count("WARN"), rep.count("EXPECT"), rep.sum["triangles"],
               rep.sum["clips"], rep.sum["wheels"], rep.sum["skins"], rep.sum["joints"], imgs, sha))


def report_dict(rep, name, sha):
    return {"file": name, "sha256": sha,
            "errors": rep.count("ERROR"), "warnings": rep.count("WARN"), "expect": rep.count("EXPECT"),
            "triangles": rep.sum["triangles"], "clips_found": rep.sum["clips"], "wheels_found": rep.sum["wheels"],
            "skins": rep.sum["skins"], "joints": rep.sum["joints"],
            "findings": [{"level": l, "rule": r, "text": t} for l, r, t in rep.items],
            "clips": rep.clips, "wheels": rep.wheels, "images": rep.images}


def cmd_check(args):
    import os
    try:
        with open(args.file, "rb") as f:
            data = f.read()
    except OSError as e:
        print("cannot read %s: %s" % (args.file, e), file=sys.stderr)
        return 2
    opts = {"expect_clips": args.expect_clips, "min_joints": args.min_joints,
            "retail_wheels": args.expect_retail_wheels, "expect_alpha": args.expect_alpha}
    name = os.path.basename(args.file)
    rep, name, sha = analyze(data, name, opts)
    for line in rep.lines():
        print(line)
    print(summary_line(rep, name, sha))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report_dict(rep, name, sha), f, indent=1)
    return 0 if rep.count("ERROR") == 0 and rep.count("EXPECT") == 0 else 1


# ---------------------------------------------------------------- selftest: GLB / PNG writers

def png_filter_row(ft, row, prev, bpp):
    out = bytearray(len(row))
    for i in range(len(row)):
        a = row[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        if ft == 0:
            pred = 0
        elif ft == 1:
            pred = a
        elif ft == 2:
            pred = b
        elif ft == 3:
            pred = (a + b) >> 1
        else:
            pred = paeth(a, b, c)
        out[i] = (row[i] - pred) & 255
    return bytes(out)


def png_chunk(ty, body):
    return struct.pack(">I", len(body)) + ty + body + struct.pack(">I", zlib.crc32(ty + body) & 0xFFFFFFFF)


def make_png(w, h, ctype, pixels, filters=(0, 1, 2, 3, 4), depth=8, ihdr_w=None, ihdr_h=None):
    ch = PNG_CHANNELS[ctype]
    rowbytes = (w * ch * depth + 7) // 8
    bpp = max(1, ch * depth // 8)
    raw = bytearray()
    prev = bytes(rowbytes)
    for y in range(h):
        row = pixels[y * rowbytes:(y + 1) * rowbytes]
        ft = filters[y % len(filters)]
        raw.append(ft)
        raw += png_filter_row(ft, row, prev, bpp)
        prev = row
    ihdr = struct.pack(">IIBBBBB", ihdr_w or w, ihdr_h or h, depth, ctype, 0, 0, 0)
    return PNG_SIG + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", zlib.compress(bytes(raw))) + png_chunk(b"IEND", b"")


def pattern_pixels(w, h, ch, seed):
    return bytes(((x * 37 + y * 91 + c * 53 + seed * 17 + (x * y) % 7) & 255)
                 for y in range(h) for x in range(w) for c in range(ch))


def make_glb(gltf, binbytes, length_delta=0):
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    b = bytes(binbytes) + b"\x00" * (-len(binbytes) % 4)
    body = struct.pack("<II", len(js), CHUNK_JSON) + js
    if b:
        body += struct.pack("<II", len(b), CHUNK_BIN) + b
    return struct.pack("<4sII", b"glTF", 2, 12 + len(body) + length_delta) + body


class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accs = []

    def view(self, data):
        self.bin += b"\x00" * (-len(self.bin) % 4)
        self.views.append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)})
        self.bin += data
        return len(self.views) - 1

    def acc(self, data, ctype, typ, count, mn=None, mx=None, normalized=False):
        a = {"bufferView": self.view(data), "componentType": ctype, "type": typ, "count": count}
        if mn is not None:
            a["min"], a["max"] = mn, mx
        if normalized:
            a["normalized"] = True
        self.accs.append(a)
        return len(self.accs) - 1


def vec_bounds(pts):
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def build_model(**o):
    """A good hi-res driver GLB; keyword options switch one defect on."""
    b = Builder()
    nj = o.get("joints", 8)
    zs = o.get("hull_scale", 1.0)
    # body: 8 corners, 12 triangles, skinned
    verts = [((0.5 if i & 1 else -0.5), (0.7 if i & 2 else 0.2), (0.75 if i & 4 else -0.75) * zs) for i in range(8)]
    tri = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 2, 6), (0, 6, 4), (1, 5, 7), (1, 7, 3),
           (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6)]
    mn, mx = vec_bounds(verts)
    pos_a = b.acc(b"".join(struct.pack("<3f", *v) for v in verts), 5126, "VEC3", 8, mn, mx)
    idx_a = b.acc(b"".join(struct.pack("<3H", *t) for t in tri), 5123, "SCALAR", 36)
    jbad = o.get("joint_index_bad")
    jd = b"".join(struct.pack("<4B", (nj + 3) if (jbad and i == 0) else i % nj, (i + 1) % nj, 0, 0) for i in range(8))
    j_a = b.acc(jd, 5121, "VEC4", 8)
    if o.get("weights_bad_sum"):
        wd = b"".join(struct.pack("<4B", 128, 76, 0, 0) for _ in range(8))  # sum 204/255 = 0.8
    else:
        wd = b"".join(struct.pack("<4B", 153, 102, 0, 0) for _ in range(8))  # 0.6 + 0.4
    w_a = b.acc(wd, 5121, "VEC4", 8, normalized=True)
    battrs = {"POSITION": pos_a, "JOINTS_0": j_a, "WEIGHTS_0": w_a}
    if o.get("joints1"):
        battrs["JOINTS_1"] = b.acc(bytes(32), 5121, "VEC4", 8)
        battrs["WEIGHTS_1"] = b.acc(struct.pack("<32f", *([0.0] * 32)), 5126, "VEC4", 8)
    if o.get("no_position"):
        del battrs["POSITION"]
    bprim = {"attributes": battrs, "indices": idx_a, "material": 0}
    if "mode" in o:
        bprim["mode"] = o["mode"]
    # wheel: 8-segment cylinder along X, radius 0.25, width 0.2
    ring = []
    for s in range(8):
        a = s * math.pi / 4
        ring.append((math.cos(a) * 0.25, math.sin(a) * 0.25))  # (z, y) -> rotates about X
    wv = [(x, y, z) for x in (-0.1, 0.1) for (z, y) in ring]
    wmn, wmx = vec_bounds(wv)
    wpos = b.acc(b"".join(struct.pack("<3f", *v) for v in wv), 5126, "VEC3", 16, wmn, wmx)
    wt = []
    for s in range(8):
        n = (s + 1) % 8
        wt += [(s, n, 8 + s), (n, 8 + n, 8 + s)]
    widx = b.acc(b"".join(struct.pack("<3H", *t) for t in wt), 5123, "SCALAR", 48)
    wattrs = {"POSITION": wpos}
    if o.get("wheel_skinned"):
        wattrs["JOINTS_0"] = b.acc(bytes(64), 5121, "VEC4", 16)
        wattrs["WEIGHTS_0"] = b.acc(struct.pack("<64f", *([1.0, 0.0, 0.0, 0.0] * 16)), 5126, "VEC4", 16)
    meshes = [{"name": "body", "primitives": [bprim]},
              {"name": "wheel", "primitives": [{"attributes": wattrs, "indices": widx, "material": 1}]}]
    # images / textures / materials
    rgb = pattern_pixels(4, 4, 3, 1)
    rgba = bytearray(pattern_pixels(4, 4, 4, 2))
    for i in range(3, len(rgba), 4):
        rgba[i] = 255
    rgba[3 + 4 * 5] = 0  # one pixel with alpha 0
    pngs = [make_png(4, 4, 2, rgb, (0, 1, 4, 2)),
            make_png(4, 4, 6, bytes(rgba), (1, 4, 3, 0)) if not o.get("mask_rgb") else make_png(4, 4, 2, rgb, (4, 1, 2, 0))]
    if o.get("edge4096"):
        pngs[0] = make_png(4096, 1, 2, pattern_pixels(4096, 1, 3, 3), (1,))
    images = []
    for i, p in enumerate(pngs):
        images.append({"bufferView": b.view(p), "mimeType": "image/png", "name": "tex%d" % i})
    if o.get("jpeg"):
        images[0] = {"bufferView": b.view(b"\xff\xd8\xff\xe0" + bytes(60)), "mimeType": "image/jpeg"}
    if o.get("image_datauri"):
        images[0] = {"uri": "data:image/png;base64,iVBORw0KGgo="}
    textures = [{"source": 0}, {"source": 1}]
    materials = [{"name": "opaque", "alphaMode": "OPAQUE", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
                 {"name": "mask", "alphaMode": "BLEND" if o.get("blend") else "MASK", "alphaCutoff": 0.5,
                  "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}}}]
    # nodes
    nodes = [{"name": "kart", "children": []}, {"name": "body", "mesh": 0, "skin": 0}, {"name": "armature", "children": []}]
    nodes[0]["children"] = [1, 2]
    joint_ids = []
    for j in range(nj):
        nodes.append({"name": "j%d" % j, "translation": [0, 0.1, 0], "children": []})
        joint_ids.append(len(nodes) - 1)
        parent = 2 if j == 0 else joint_ids[j - 1]
        nodes[parent]["children"].append(len(nodes) - 1)
    wheel_pos = dict(RETAIL_WHEELS)
    if o.get("wheel_off"):
        wheel_pos["wheel_fl"] = (0.5625, 0.35, 0.7773)
    for nm in WHEEL_NAMES:
        if nm in o.get("drop_wheels", ()):
            continue
        n = {"name": nm, "mesh": 1, "translation": list(wheel_pos[nm])}
        if o.get("wheel_skinned"):
            n["skin"] = 0
        nodes.append(n)
        nodes[0]["children"].append(len(nodes) - 1)
    ibm = b.acc(struct.pack("<16f", *IDENT) * nj, 5126, "MAT4", nj)
    skins = [{"joints": joint_ids, "inverseBindMatrices": ibm, "skeleton": 2}]
    if o.get("two_skins"):
        skins.append(dict(skins[0]))
    # animations
    inp = b.acc(struct.pack("<3f", 0.0, 0.5, 1.0), 5126, "SCALAR", 3, [0.0], [1.0])
    anims = []
    names = list(CLIP_NAMES)
    if "drop_clip" in o:
        names.remove(o["drop_clip"])
    if "rename_clip" in o:
        names[names.index(o["rename_clip"][0])] = o["rename_clip"][1]
    for k, cn in enumerate(names):
        chans, samps = [], []
        for c in range(2):
            ang = 0.05 * (k + 1) * (c + 1)
            axis = c
            quats = []
            for step in (0.0, 1.0, 0.5):
                a = ang * step
                q = [0.0, 0.0, 0.0, math.cos(a / 2)]
                q[axis] = math.sin(a / 2)
                quats.append(q)
            out = b.acc(b"".join(struct.pack("<4f", *q) for q in quats), 5126, "VEC4", 3)
            samps.append({"input": inp, "output": out,
                          "interpolation": "CUBICSPLINE" if (o.get("cubic") == cn and c == 0) else "LINEAR"})
            chans.append({"sampler": c, "target": {"node": joint_ids[(k + c) % nj], "path": "rotation"}})
        anims.append({"name": cn, "samplers": samps, "channels": chans})
    gltf = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": nodes,
            "meshes": meshes, "skins": skins, "materials": materials, "textures": textures, "images": images,
            "animations": anims, "buffers": [{"byteLength": len(b.bin)}], "bufferViews": b.views, "accessors": b.accs}
    if o.get("ext_required"):
        gltf["extensionsRequired"] = [o["ext_required"]]
    if o.get("buffer_uri"):
        gltf["buffers"][0]["uri"] = "model.bin"
    if o.get("acc_past"):
        gltf["accessors"][idx_a]["count"] = 100000
    return gltf, b.bin


def build_dup():
    """Model whose second clip (steer_left) repeats the output of the first (idle)."""
    gltf, binb = build_model()
    anims = gltf["animations"]
    for c in range(2):
        anims[1]["samplers"][c]["output"] = anims[0]["samplers"][c]["output"]
    return make_glb(gltf, binb)


def build_static():
    gltf, binb = build_model()
    # make both channels of "jump" constant: point their outputs at one constant accessor
    b = Builder()
    b.bin = bytearray(binb)
    b.views = gltf["bufferViews"]
    b.accs = gltf["accessors"]
    q = struct.pack("<4f", 0.0, 0.0, 0.0, 1.0) * 3
    const = b.acc(q, 5126, "VEC4", 3)
    jump = [a for a in gltf["animations"] if a["name"] == "jump"][0]
    for s in jump["samplers"]:
        s["output"] = const
    gltf["buffers"][0]["byteLength"] = len(b.bin)
    return make_glb(gltf, b.bin)


def glb_of(**o):
    gltf, binb = build_model(**o)
    return make_glb(gltf, binb, o.get("length_delta", 0))


# ---------------------------------------------------------------- selftest

ALL_OPTS = {"expect_clips": "all", "min_joints": 8, "retail_wheels": True, "expect_alpha": True}
NO_OPTS = {"expect_clips": None, "min_joints": None, "retail_wheels": False, "expect_alpha": False}


def selftest():
    results = []

    def record(name, ok, detail=""):
        results.append((name, ok))
        if not ok:
            print("FAIL %s %s" % (name, detail))

    def case(name, data, opts, expect, absent=()):
        rep, _n, _s = analyze(data, "case.glb", opts)
        missing = [e for e in expect if not rep.has(*e)]
        unexpected = [e for e in absent if rep.has(*e)]
        record(name, not missing and not unexpected,
               "missing %s unexpected %s\n  got: %s" % (missing, unexpected, "; ".join(rep.lines())))

    # PNG decoder: every filter type round-trips
    for ct in (2, 6):
        ch = PNG_CHANNELS[ct]
        pix = pattern_pixels(7, 6, ch, 5)
        for fs in ((0,), (1,), (2,), (3,), (4,), (4, 3, 2, 1, 0)):
            d = decode_png(make_png(7, 6, ct, pix, fs), keep_raw=True)
            record("png roundtrip ct%d filters %s" % (ct, fs), d["decoded"] and d["raw"] == pix, str(d.get("err")))

    # the good file
    good = glb_of()
    rep, _n, sha = analyze(good, "good.glb", ALL_OPTS)
    line = summary_line(rep, "good.glb", sha)
    record("good: no findings above INFO", rep.count("ERROR") == 0 and rep.count("WARN") == 0
           and rep.count("EXPECT") == 0, "; ".join(rep.lines()))
    record("good: summary values", ("triangles 76 clips 12/12 wheels 4/4 skins 1 joints 8 images 4x4ct2,4x4ct6a0 sha256 %s" % sha) in line, line)
    record("good: exit-code rule", (rep.count("ERROR") == 0 and rep.count("EXPECT") == 0))
    rep2, _n, _s = analyze(good, "good.glb", NO_OPTS)
    record("good: plain run clean", rep2.count("ERROR") + rep2.count("WARN") + rep2.count("EXPECT") == 0)
    record("good: json serialisable", bool(json.dumps(report_dict(rep, "good.glb", sha))))

    # container
    case("bad magic", b"XXXX" + good[4:], NO_OPTS, [("ERROR", "glb-header")])
    case("wrong GLB length", glb_of(length_delta=8), NO_OPTS, [("ERROR", "glb-length")])
    case("truncated file", good[:200], NO_OPTS, [("ERROR", "glb-header")])
    case("buffer uri", glb_of(buffer_uri=True), NO_OPTS, [("ERROR", "glb-uri")])
    case("image data: uri", glb_of(image_datauri=True), NO_OPTS, [("ERROR", "glb-uri")])
    case("extensionsRequired draco", glb_of(ext_required="KHR_draco_mesh_compression"), NO_OPTS, [("ERROR", "glb-ext-required")])
    case("extensionsRequired unlit allowed", glb_of(ext_required="KHR_materials_unlit"), NO_OPTS, [],
         [("ERROR", "glb-ext-required")])
    case("accessor past buffer", glb_of(acc_past=True), NO_OPTS, [("ERROR", "acc-range")])
    # primitives
    case("mode 1", glb_of(mode=1), NO_OPTS, [("ERROR", "prim-mode")])
    case("POSITION missing", glb_of(no_position=True), NO_OPTS, [("ERROR", "prim-position")])
    # skin
    case("two skins", glb_of(two_skins=True), NO_OPTS, [("ERROR", "skin-count")])
    case("65 joints", glb_of(joints=65), NO_OPTS, [("ERROR", "skin-joints")])
    case("64 joints allowed", glb_of(joints=64), NO_OPTS, [], [("ERROR", "skin-joints")])
    case("JOINTS_1", glb_of(joints1=True), NO_OPTS, [("ERROR", "skin-weights4")])
    case("joint index out of range", glb_of(joint_index_bad=True), NO_OPTS, [("ERROR", "skin-joint-index")])
    case("weight sum 0.8", glb_of(weights_bad_sum=True), NO_OPTS, [("WARN", "skin-weight-sum")])
    case("--min-joints 9 with 8 joints", good, dict(NO_OPTS, min_joints=9), [("EXPECT", "skin-minjoints")])
    # textures
    case("JPEG mime", glb_of(jpeg=True), NO_OPTS, [("ERROR", "tex-jpeg")])
    case("edge 4096 (real 4096x1 PNG)", glb_of(edge4096=True), NO_OPTS, [("ERROR", "tex-edge")],
         [("WARN", "tex-decode"), ("ERROR", "tex-format")])
    case("BLEND material", glb_of(blend=True), NO_OPTS, [("WARN", "alpha-blend")])
    case("colour type 2 under --expect-alpha", glb_of(mask_rgb=True), dict(NO_OPTS, expect_alpha=True), [("EXPECT", "alpha-mask")])
    case("16-bit PNG", rebuild_with_depth16(), NO_OPTS, [("WARN", "tex-format")])
    # clips
    case("clip missing with --expect-clips all", glb_of(drop_clip="menu"), dict(NO_OPTS, expect_clips="all"),
         [("EXPECT", "clip-missing")], [("WARN", "clip-missing")])
    case("clip missing without expectation", glb_of(drop_clip="menu"), NO_OPTS, [("WARN", "clip-missing")])
    case("unknown clip name", glb_of(rename_clip=("menu", "dance")), NO_OPTS, [("WARN", "clip-unknown"), ("WARN", "clip-missing")])
    case("CUBICSPLINE", glb_of(cubic="idle"), NO_OPTS, [("WARN", "clip-cubic")])
    case("static clip", build_static(), NO_OPTS, [("WARN", "clip-static")])
    case("duplicate clip", build_dup(), NO_OPTS, [("WARN", "clip-duplicate")])
    # wheels
    case("one wheel missing (partial)", glb_of(drop_wheels=("wheel_rr",)), NO_OPTS, [("WARN", "wheel-partial")])
    case("no wheels at all is fine", glb_of(drop_wheels=WHEEL_NAMES), NO_OPTS, [], [("WARN", "wheel-partial")])
    case("skinned wheel", glb_of(wheel_skinned=True), NO_OPTS, [("WARN", "wheel-skinned")])
    case("wheel 0.1 m off under --expect-retail-wheels", glb_of(wheel_off=True), dict(NO_OPTS, retail_wheels=True),
         [("EXPECT", "wheel-retail")])
    case("missing wheel under --expect-retail-wheels", glb_of(drop_wheels=("wheel_fl",)), dict(NO_OPTS, retail_wheels=True),
         [("EXPECT", "wheel-retail")])
    # hull
    case("hull 5 m", glb_of(hull_scale=3.4), NO_OPTS, [("ERROR", "hull-size")])

    fails = sum(1 for _n, ok in results if not ok)
    print("hires_glb_check selftest: %d checks, %d failures" % (len(results), fails))
    return 1 if fails else 0


def rebuild_with_depth16():
    """Good model whose first image is a 16-bit RGB PNG (header only matters)."""
    gltf, binb = build_model()
    b = Builder()
    b.bin = bytearray(binb)
    b.views = gltf["bufferViews"]
    png = make_png(2, 2, 2, pattern_pixels(2, 2, 6, 1), (0,), depth=16)
    gltf["images"][0]["bufferView"] = b.view(png)
    gltf["buffers"][0]["byteLength"] = len(b.bin)
    return make_glb(gltf, b.bin)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="hires_glb_check.py", description="GLB checker for hi-res drivers")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="check one GLB file")
    c.add_argument("file")
    c.add_argument("--expect-clips", choices=["all"], default=None)
    c.add_argument("--min-joints", type=int, default=None)
    c.add_argument("--expect-retail-wheels", action="store_true")
    c.add_argument("--expect-alpha", action="store_true")
    c.add_argument("--json", default=None)
    sub.add_parser("selftest", help="run the built-in checks")
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return selftest()
    return cmd_check(args)


if __name__ == "__main__":
    sys.exit(main())
