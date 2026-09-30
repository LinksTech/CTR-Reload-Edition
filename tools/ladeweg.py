"""WORK THROUGH THE CONTAINER LOAD PATH STATICALLY.

No game start. Every station gets the same question: what comes in, what
goes out, does it match - and does anything silently fall away.

Rebuilt line by line is what the tree does:

  Rld_Open            header, directory, offset checks
  Rld_ReadChunk       compression, size_stored == size_raw, SHA-256
  LOAD_VramFileCallback  the VRM as a chain of LoadImage rectangles
  Rld_AbrResolveGroups   quadblock -> IconGroup4, across AnimTex
  Rld_AbrVerdict         TextureLayout -> tpage, CLUT, UV rectangle

Usage:  python ladeweg.py <file.rldtrack> [...]
"""

import hashlib
import struct
import sys
import os

HDR = 40
DIRENT = 64
VRAM_W, VRAM_H = 1024, 512
QUADBLOCK = 0x5C
ICONGROUP4 = 0x30
TEXLAYOUT = 0x0C
BODY = 4

u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
u64 = lambda b, o: struct.unpack_from("<Q", b, o)[0]


def shalf(v):
    return v - 0x10000 if v >= 0x8000 else v


# --------------------------------------------------------------- Station 1+2

def open_container(path):
    raw = open(path, "rb").read()
    out = {"path": path, "size": len(raw), "raw": raw, "problems": []}
    if len(raw) < HDR or raw[:8] != b"RLDTRACK":
        out["problems"].append("no RLDTRACK magic")
        return out
    out["major"] = u16(raw, 8)
    out["minor"] = u16(raw, 10)
    out["flags"] = u32(raw, 12)
    out["chunkCount"] = u32(raw, 16)
    out["pad"] = u32(raw, 20)
    out["dirOffset"] = u64(raw, 24)
    out["declaredSize"] = u64(raw, 32)

    if out["declaredSize"] != len(raw):
        out["problems"].append("file_size %d != real size %d" % (out["declaredSize"], len(raw)))
    if out["flags"] != 0 or out["pad"] != 0:
        out["problems"].append("reserved fields not zero")

    chunks = []
    d = out["dirOffset"]
    for i in range(out["chunkCount"]):
        e = raw[d + i * DIRENT: d + (i + 1) * DIRENT]
        c = {
            "type": e[0:4].decode("latin1"),
            "method": u32(e, 4) & 0xF,
            "flagsHi": u32(e, 4) >> 4,
            "offset": u64(e, 8),
            "stored": u64(e, 16),
            "raw": u64(e, 24),
            "hash": e[32:64],
            "index": i,
        }
        c["tail"] = e[32 + 32:]
        chunks.append(c)
    out["chunks"] = chunks

    # Offset checks like Rld_Open, plus the two that are NOT there:
    # overlap and gap.
    spans = []
    for c in chunks:
        if c["offset"] < HDR or c["offset"] > out["dirOffset"] or c["stored"] > out["dirOffset"] - c["offset"]:
            out["problems"].append("%s lies outside the data area" % c["type"])
        if c["method"] not in (0, 1):
            out["problems"].append("%s: unknown compression %d" % (c["type"], c["method"]))
        if c["method"] == 0 and c["stored"] != c["raw"]:
            out["problems"].append("%s: stored != raw without compression" % c["type"])
        spans.append((c["offset"], c["offset"] + c["stored"], c["type"]))

    spans.sort()
    out["gaps"] = []
    out["overlaps"] = []
    at = HDR
    for lo, hi, t in spans:
        if lo < at:
            out["overlaps"].append((t, at - lo))
        elif lo > at:
            out["gaps"].append((t, lo - at))
        at = max(at, hi)
    out["tailGap"] = out["dirOffset"] - at
    return out


def read_chunk(cont, c):
    raw = cont["raw"]
    data = raw[c["offset"]: c["offset"] + c["stored"]]
    if c["method"] != 0:
        return None, "Deflate not implemented"
    if len(data) != c["raw"]:
        return None, "short read"
    if hashlib.sha256(data).digest() != c["hash"]:
        return None, "SHA-256 does not match"
    return data, None


# --------------------------------------------------------------- Station VRM

def vram_build(vrm):
    """LOAD_VramFileCallback rebuilt. Returns the blocks and the coverage."""
    written = bytearray(VRAM_W * VRAM_H)
    pixels = [0] * (VRAM_W * VRAM_H)
    blocks = []
    problems = []
    if len(vrm) < 0x14:
        return blocks, written, pixels, ["VRM shorter than a header"]

    chained = u32(vrm, 0) == 0x20
    at = 4 if chained else 0
    guard = 0
    while at + 0x14 <= len(vrm):
        guard += 1
        if guard > 4096:
            problems.append("more than 4096 blocks - abort")
            break
        header = at
        end = len(vrm)
        if chained:
            bs = u32(vrm, at)
            if bs == 0:
                break
            if bs > len(vrm):
                problems.append("block size %d larger than the file" % bs)
                break
            header = at + 4
            end = header + (bs & ~3)
            if header + 0x14 > len(vrm) or end > len(vrm):
                problems.append("block runs past the end of the file")
                break
            at = header + (bs & ~3)

        mode = u32(vrm, header + 0x00)
        flag = u32(vrm, header + 0x04)
        length = u32(vrm, header + 0x08)
        x = shalf(u16(vrm, header + 0x0C))
        y = shalf(u16(vrm, header + 0x0E))
        w = shalf(u16(vrm, header + 0x10))
        h = shalf(u16(vrm, header + 0x12))

        blk = {"x": x, "y": y, "w": w, "h": h, "mode": mode, "flag": flag,
               "len": length, "at": header, "ok": True, "why": ""}

        if not (w > 0 and h > 0 and x >= 0 and y >= 0 and x + w <= VRAM_W and y + h <= VRAM_H):
            blk["ok"] = False
            blk["why"] = "rectangle outside VRAM or empty"
        else:
            need = 0x14 + w * h * 2
            if header + need > end:
                blk["ok"] = False
                blk["why"] = "pixels do not reach the end of the block (%d needed, %d present)" % (need, end - header)
            for row in range(h):
                line = header + 0x14 + row * w * 2
                if line + w * 2 > end:
                    break
                for i in range(w):
                    slot = (y + row) * VRAM_W + (x + i)
                    pixels[slot] = u16(vrm, line + i * 2)
                    written[slot] = 1
        blocks.append(blk)
        if not chained:
            break
    return blocks, written, pixels, problems


# --------------------------------------------------------------- Station LEV

def lev_walk(lev):
    out = {"problems": []}
    n = len(lev)
    if n < BODY + 0x200:
        out["problems"].append("LEV too small")
        return out
    mesh = u32(lev, BODY + 0)
    out["mesh"] = mesh
    if mesh == 0 or BODY + mesh + 0x20 > n:
        out["problems"].append("mesh_info does not lie in the file")
        return out
    blocks = u32(lev, BODY + mesh + 0x00)
    quadArray = u32(lev, BODY + mesh + 0x0C)
    out["blocks"] = blocks
    out["quadArray"] = quadArray
    if blocks == 0 or blocks > n // QUADBLOCK or BODY + quadArray + blocks * QUADBLOCK > n:
        out["problems"].append("quadblock array does not lie in the file")
        return out
    return out


def resolve_groups(lev, pointer):
    n = len(lev)
    if pointer == 0:
        return []
    if pointer & 1:
        anim = pointer - 1
        if BODY + anim + 12 > n:
            return []
        frames = u16(lev, BODY + anim + 4)
        if frames == 0 or frames > 4096 or BODY + anim + 12 + frames * 4 > n:
            return []
        got = []
        for i in range(frames):
            g = u32(lev, BODY + anim + 12 + i * 4)
            if g != 0 and BODY + g + ICONGROUP4 <= n:
                got.append(g)
        return got
    if BODY + pointer + ICONGROUP4 > n:
        return []
    return [pointer]


def layout_probe(lev, off, written):
    """One TextureLayout against the reconstructed VRAM. Like Rld_AbrVerdict,
    but the question is not STP but ARRIVAL."""
    lay = lev[off: off + TEXLAYOUT]
    if len(lay) < TEXLAYOUT:
        return None
    tpage = u16(lay, 0x06)
    clut = u16(lay, 0x02)
    if tpage & 0xFE00:
        return {"why": "tpage has reserved bits set", "ok": None, "tpage": tpage, "clut": clut}
    depth = (tpage >> 7) & 3
    pageX = (tpage & 0xF) * 64
    pageY = ((tpage >> 4) & 1) * 256
    clutX = (clut & 0x3F) * 16
    clutY = clut >> 6
    corners = (0x00, 0x04, 0x08, 0x0A)
    us = [lay[c] for c in corners]
    vs = [lay[c + 1] for c in corners]
    uLo, uHi, vLo, vHi = min(us), max(us), min(vs), max(vs)

    res = {"tpage": tpage, "clut": clut, "depth": depth, "pageX": pageX, "pageY": pageY,
           "clutX": clutX, "clutY": clutY, "u": (uLo, uHi), "v": (vLo, vHi),
           "texMissing": 0, "texTotal": 0, "clutMissing": 0, "clutTotal": 0, "ok": True, "why": ""}

    if depth > 1:
        res["why"] = "16-bit or reserved - no CLUT"
    seen = set()
    for v in range(vLo, vHi + 1):
        py = pageY + v
        if py < 0 or py >= VRAM_H:
            res["ok"] = False
            res["why"] = "UV rectangle leaves VRAM"
            return res
        for u in range(uLo, uHi + 1):
            px = pageX + ((u >> 2) if depth == 0 else (u >> 1) if depth == 1 else u)
            if px < 0 or px >= VRAM_W:
                res["ok"] = False
                res["why"] = "UV rectangle leaves VRAM"
                return res
            slot = py * VRAM_W + px
            res["texTotal"] += 1
            if not written[slot]:
                res["texMissing"] += 1
            elif depth < 2:
                word = PIXELS[slot]
                seen.add((word >> ((u & 3) * 4)) & 0xF if depth == 0 else (word >> ((u & 1) * 8)) & 0xFF)
    if depth < 2:
        if clutY >= VRAM_H:
            res["ok"] = False
            res["why"] = "CLUT lies outside VRAM"
            return res
        for i in sorted(seen):
            if clutX + i >= VRAM_W:
                res["ok"] = False
                res["why"] = "CLUT entry outside VRAM"
                return res
            res["clutTotal"] += 1
            if not written[clutY * VRAM_W + clutX + i]:
                res["clutMissing"] += 1
    if res["texMissing"] or res["clutMissing"]:
        res["ok"] = False
    return res


# ---------------------------------------------------------------------- Run

def bar(n, total, width=40):
    if total == 0:
        return ""
    f = int(round(width * n / float(total)))
    return "#" * f + "." * (width - f)


def run(path):
    global PIXELS
    print("=" * 78)
    print(os.path.basename(path))
    print("=" * 78)

    cont = open_container(path)
    print("\n-- STATION 1  Open container ---------------------------------------")
    if cont["problems"]:
        for p in cont["problems"]:
            print("   PROBLEM: %s" % p)
    print("   version %d.%d, %d chunks, directory at %d, file %d bytes"
          % (cont.get("major", -1), cont.get("minor", -1), cont.get("chunkCount", 0),
             cont.get("dirOffset", 0), cont["size"]))
    if cont["overlaps"]:
        print("   OVERLAP: %s" % cont["overlaps"])
    if cont["gaps"]:
        print("   gaps between the chunks: %s" % cont["gaps"])
    else:
        print("   no gap between the chunks")
    print("   between last chunk and directory: %d bytes" % cont["tailGap"])

    print("\n-- STATION 2  Unpack chunks ----------------------------------------")
    data = {}
    for c in cont["chunks"]:
        d, err = read_chunk(cont, c)
        data[c["type"]] = d
        method = {0: "raw", 1: "deflate"}.get(c["method"], "?%d" % c["method"])
        print("   %-4s  %9d bytes %-8s  offset %9d  %s"
              % (c["type"], c["raw"], method, c["offset"], "SHA-256 ok" if err is None else "ERROR: " + err))

    lev = data.get("LEVD")
    vrm = data.get("VRMD")
    if lev is None or vrm is None:
        print("\n   LEVD or VRMD missing - end.")
        return

    print("\n-- STATION 3  LEV to the track loader ------------------------------")
    L = lev_walk(lev)
    for p in L["problems"]:
        print("   PROBLEM: %s" % p)
    if "blocks" in L:
        print("   mesh_info at %d, %d quadblocks, array at %d, %d bytes used"
              % (L["mesh"], L["blocks"], L["quadArray"], L["blocks"] * QUADBLOCK))

    print("\n-- STATION 4  VRM into VRAM ----------------------------------------")
    blocks, written, PIXELS, vprobs = vram_build(vrm)
    for p in vprobs:
        print("   PROBLEM: %s" % p)
    good = [b for b in blocks if b["ok"]]
    bad = [b for b in blocks if not b["ok"]]
    print("   %d block(s), of which %d loaded, %d rejected" % (len(blocks), len(good), len(bad)))
    for b in bad:
        print("      REJECTED   %d,%d %dx%d - %s" % (b["x"], b["y"], b["w"], b["h"], b["why"]))
    px = sum(written)
    print("   %d of %d VRAM texels written (%.1f %%)" % (px, VRAM_W * VRAM_H, 100.0 * px / (VRAM_W * VRAM_H)))

    # Texels written twice: two blocks on the same spot.
    cover = {}
    dbl = 0
    for b in good:
        for row in range(b["h"]):
            for i in range(b["w"]):
                k = (b["y"] + row) * VRAM_W + b["x"] + i
                if k in cover:
                    dbl += 1
                cover[k] = 1
    print("   %d texels are written by more than one block" % dbl)

    print("\n   The blocks, in load order:")
    for b in good:
        print("      %4d,%-4d  %4dx%-4d  %8d bytes" % (b["x"], b["y"], b["w"], b["h"], b["w"] * b["h"] * 2))

    print("\n   Occupancy per texture page (16 columns x 2 rows of 64x256):")
    for py in range(2):
        row = []
        for pxi in range(16):
            n = 0
            for yy in range(py * 256, py * 256 + 256):
                base = yy * VRAM_W + pxi * 64
                n += sum(written[base:base + 64])
            row.append(n * 100 // (64 * 256))
        print("      row %d: %s" % (py, " ".join("%3d" % v for v in row)))

    print("\n-- STATION 5  Textures: does each one arrive? ----------------------")
    if "blocks" not in L:
        print("   quadblocks not readable - skipped.")
        return
    seenGroups = {}
    faceRefs = 0
    nullRefs = 0
    animRefs = 0
    for i in range(L["blocks"]):
        q = BODY + L["quadArray"] + i * QUADBLOCK
        for face in range(5):
            ptr = u32(lev, q + 0x1C + face * 4) if face < 4 else u32(lev, q + 0x40)
            if ptr == 0:
                nullRefs += 1
                continue
            faceRefs += 1
            if ptr & 1:
                animRefs += 1
            for g in resolve_groups(lev, ptr):
                seenGroups.setdefault(g, 0)
                seenGroups[g] += 1
    print("   %d quadblocks, %d texture references, of which %d via AnimTex, %d empty"
          % (L["blocks"], faceRefs, animRefs, nullRefs))
    print("   %d different IconGroup4 reachable" % len(seenGroups))

    pagesUsed = {}
    clutsUsed = {}
    okN = badN = unkN = 0
    missTex = missClut = 0
    worst = []
    for g in sorted(seenGroups):
        for lod in range(4):
            off = BODY + g + lod * TEXLAYOUT
            r = layout_probe(lev, off, written)
            if r is None:
                continue
            if r.get("ok") is None:
                unkN += 1
                continue
            pagesUsed.setdefault(r["tpage"] & 0x1F, 0)
            pagesUsed[r["tpage"] & 0x1F] += 1
            clutsUsed.setdefault(r["clut"], 0)
            clutsUsed[r["clut"]] += 1
            if r["ok"]:
                okN += 1
            else:
                badN += 1
                missTex += r["texMissing"]
                missClut += r["clutMissing"]
                if len(worst) < 8:
                    worst.append((g, lod, r))
    print("   %d TextureLayouts checked: %d fully present, %d incomplete, %d undecidable"
          % (okN + badN + unkN, okN, badN, unkN))
    if badN:
        print("   MISSING: %d texture texels and %d CLUT entries that no VRM block writes" % (missTex, missClut))
        for g, lod, r in worst:
            print("      group %#x LOD %d: tpage %#x (page %d), CLUT %#x at %d,%d - %d/%d texels, %d/%d CLUT missing  %s"
                  % (g, lod, r["tpage"], r["tpage"] & 0x1F, r["clut"], r["clutX"], r["clutY"],
                     r["texMissing"], r["texTotal"], r["clutMissing"], r["clutTotal"], r["why"]))
    print("   texture pages the geometry names: %s" % sorted(pagesUsed))
    print("   different CLUTs: %d" % len(clutsUsed))

    # Which written pages does nobody name?
    writtenPages = set()
    for py in range(2):
        for pxi in range(16):
            n = 0
            for yy in range(py * 256, py * 256 + 256):
                base = yy * VRAM_W + pxi * 64
                n += sum(written[base:base + 64])
            if n:
                writtenPages.add(py * 16 + pxi)
    named = set(pagesUsed)
    print("   written but named by no face: %s" % sorted(writtenPages - named))
    print("   named but not written:        %s" % sorted(named - writtenPages))
    print()


if __name__ == "__main__":
    for a in sys.argv[1:]:
        run(a)


# ----------------------------------------------------- Station META / memory

META_FIXED = 0x38


def parse_meta(b):
    out = {}
    if len(b) < META_FIXED:
        return {"error": "META too short"}
    out["metaVersion"] = u32(b, 0x00)
    out["publicKey"] = b[0x04:0x24]
    out["trackVersion"] = u32(b, 0x24)
    out["modes"] = u32(b, 0x28)
    out["primBytes"] = u32(b, 0x2C)
    out["memTotal"] = u32(b, 0x30)
    out["stringCount"] = u32(b, 0x34)
    at = META_FIXED
    strings = []
    for i in range(out["stringCount"]):
        if at + 4 > len(b):
            out["error"] = "string table runs past the end"
            break
        ln = u32(b, at)
        at += 4
        if ln > len(b) - at:
            out["error"] = "string runs past the end"
            break
        strings.append(b[at:at + ln].decode("latin1"))
        at += ln
    out["strings"] = strings
    out["tail"] = len(b) - at
    return out


def mem_need(lev):
    out = {"levBytes": len(lev), "primBytes": 0, "skyBytes": 0, "skySegs": []}
    n = len(lev)
    if n < BODY + 0x200:
        return out
    at = u32(lev, BODY + 0x00)
    if at != 0 and BODY + at + 8 <= n:
        blocks = u32(lev, BODY + at + 0x00)
        if blocks <= n // QUADBLOCK:
            out["primBytes"] = blocks * 4 * 0x34
    at = u32(lev, BODY + 0x04)
    out["skyOffset"] = at
    if at != 0 and BODY + at + 56 <= n:
        segs = [u16(lev, BODY + at + 8 + a * 2) for a in range(8)]
        out["skySegs"] = segs
        out["skyVerts"] = u32(lev, BODY + at + 0x00)
        out["skyVertPtr"] = u32(lev, BODY + at + 0x04)
        out["skyFacePtrs"] = [u32(lev, BODY + at + 24 + a * 4) for a in range(8)]
        s = sorted(segs, reverse=True)
        out["skyBytes"] = sum(s[:4]) * 28
    out["total"] = out["levBytes"] + 2 * (out["primBytes"] + out["skyBytes"])
    return out


def model_pages(lev, written):
    """Which texture pages the MODELS of the LEV name - the rest of the VRM
    does not automatically belong to nobody."""
    n = len(lev)
    pages = {}
    total = 0
    if n < BODY + 0x200:
        return pages, 0, 0
    numModels = u32(lev, BODY + 0x14)
    arr = u32(lev, BODY + 0x18)
    if numModels == 0 or numModels > 4096 or BODY + arr + numModels * 4 > n:
        return pages, 0, 0
    # The path from a model to its TextureLayouts is deeper than the
    # quadblock side; here only the LEV's IconGroup table is read,
    # which lists EVERY texture of the track - levTexLookup at 0x3c.
    return pages, numModels, arr


def texlookup_pages(lev, written):
    """levTexLookup at Level+0x3c: numIcon, firstIcon, numIconGroup,
    firstIconGroupPtr. That is the complete texture list of the LEV -
    regardless of who uses it."""
    n = len(lev)
    res = {"ok": False}
    if n < BODY + 0x200:
        return res
    lt = u32(lev, BODY + 0x3C)
    if lt == 0 or BODY + lt + 16 > n:
        return res
    numIcon = u32(lev, BODY + lt + 0x00)
    firstIcon = u32(lev, BODY + lt + 0x04)
    numGroup = u32(lev, BODY + lt + 0x08)
    firstGroupPtr = u32(lev, BODY + lt + 0x0C)
    res.update(ok=True, numIcon=numIcon, firstIcon=firstIcon,
               numGroup=numGroup, firstGroupPtr=firstGroupPtr)
    if numGroup == 0 or numGroup > 65536 or BODY + firstGroupPtr + numGroup * 4 > n:
        return res
    pages = {}
    cluts = {}
    miss = 0
    checked = 0
    for i in range(numGroup):
        g = u32(lev, BODY + firstGroupPtr + i * 4)
        if g == 0 or BODY + g + ICONGROUP4 > n:
            continue
        for lod in range(4):
            r = layout_probe(lev, BODY + g + lod * TEXLAYOUT, written)
            if r is None or r.get("ok") is None:
                continue
            checked += 1
            pages[r["tpage"] & 0x1F] = pages.get(r["tpage"] & 0x1F, 0) + 1
            cluts[r["clut"]] = cluts.get(r["clut"], 0) + 1
            if not r["ok"]:
                miss += 1
    res.update(pages=pages, cluts=cluts, missing=miss, checked=checked)
    return res


def run2(path):
    global PIXELS
    cont = open_container(path)
    data = {}
    for c in cont["chunks"]:
        d, err = read_chunk(cont, c)
        data[c["type"]] = d
    lev, vrm, meta = data.get("LEVD"), data.get("VRMD"), data.get("META")
    if lev is None or vrm is None or meta is None:
        print("   chunks incomplete")
        return
    blocks, written, PIXELS, _ = vram_build(vrm)

    M = parse_meta(meta)
    N = mem_need(lev)
    print("=" * 78)
    print("%s   '%s' by '%s'" % (os.path.basename(path),
                                  M["strings"][0] if M.get("strings") else "?",
                                  M["strings"][1] if len(M.get("strings", [])) > 1 else "?"))
    print("=" * 78)
    print("-- STATION 6  Allocate memory --------------------------------------")
    print("   META says   primBytes %8d   memTotal %9d" % (M["primBytes"], M["memTotal"]))
    print("   own count   primBytes %8d   memTotal %9d   %s"
          % (N["primBytes"], N["total"], "equal" if (N["primBytes"] == M["primBytes"] and N["total"] == M["memTotal"]) else "DEVIATION"))
    print("   of which    LEV %d, draw memory 2x%d, sky 2x%d"
          % (N["levBytes"], N["primBytes"], N["skyBytes"]))
    print("   META remainder after the string table: %d bytes" % M.get("tail", -1))

    print("-- STATION 7  Sky --------------------------------------------------")
    if not N.get("skySegs"):
        print("   NO sky: ptr_skybox = %s" % N.get("skyOffset"))
    else:
        print("   %d points, segments %s" % (N.get("skyVerts", -1), N["skySegs"]))
        print("   face pointers: %s" % [hex(x) for x in N.get("skyFacePtrs", [])])
        uniq = len(set(N.get("skyFacePtrs", [])))
        print("   %d of 8 segment pointers differ" % uniq)

    print("-- STATION 8  Texture table of the LEV -----------------------------")
    T = texlookup_pages(lev, written)
    if not T["ok"]:
        print("   levTexLookup not readable")
    else:
        print("   %d Icons, %d IconGroups" % (T.get("numIcon", -1), T.get("numGroup", -1)))
        if "pages" in T:
            print("   %d layouts checked, %d incomplete" % (T["checked"], T["missing"]))
            print("   pages of the WHOLE texture table: %s" % sorted(T["pages"]))
            print("   CLUTs: %d different" % len(T["cluts"]))
    print()
