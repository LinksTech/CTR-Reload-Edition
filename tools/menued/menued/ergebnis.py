"""Reading menus/*.ergebnis - what the game really drew.

The file is the table that --ui-elements writes to the log, with the same
lines from the same hand (NativeGpu_PrintUIDecisions, one line, two
sinks). The game writes it on every successful reload.

WHAT FOR. Width and height of a box are in no declaration - they are
computed from font, row count and style (RECTMENU_GetWidth/GetHeight). Whoever
sets a position sees the consequence only here.

before and after: before is the rectangle in the 512-column reference space, after the
mapped one on the real canvas (512/682/918). The sketch shows before,
because it draws the reference space.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field

_KOPF = re.compile(
    r"vblank=(\d+)\s+aspect=(\d+):(\d+)\s+canvas=(\d+)x(\d+)\s+wide=(\d+)\s+shift=(-?\d+)"
)
_MENGEN = re.compile(r"elements=(\d+)\s+prims=(\d+)")
_RECHTECK = (
    r"anchor=(\S+)\s+decl=(.*?)\s+"
    r"before=\s*(-?\d+),\s*(-?\d+)\.\.\s*(-?\d+),\s*(-?\d+)\s+"
    r"shift=\s*([+-]?\d+)\s+"
    r"after=\s*(-?\d+),\s*(-?\d+)\.\.\s*(-?\d+),\s*(-?\d+)"
)
_EL = re.compile(r"\bel\s+(\d+)\s+slot=\s*(\d+)\s+" + _RECHTECK)
_PR = re.compile(r"\bpr\s+(\d+)\s+prims=(\d+)\s+" + _RECHTECK)


@dataclass
class Element:
    art: str  # "el" | "pr"
    nummer: int
    name: str
    anker: str
    vor: tuple = (0, 0, 0, 0)  # x0, y0, x1, y1 in the reference space
    nach: tuple = (0, 0, 0, 0)
    versatz: int = 0

    @property
    def breite(self) -> int:
        return self.vor[2] - self.vor[0]

    @property
    def hoehe(self) -> int:
        return self.vor[3] - self.vor[1]


@dataclass
class Messung:
    vblank: int = 0
    seiten: str = ""
    bezug_w: int = 512
    bezug_h: int = 216
    leinwand: int = 512
    elemente: int = 0
    primitive: int = 0
    liste: list = field(default_factory=list)

    def nach_name(self, name: str):
        for e in self.liste:
            if e.name == name:
                return e
        return None


def lade(text: str) -> Messung:
    m = Messung()

    for zeile in text.splitlines():
        t = _KOPF.search(zeile)
        if t:
            m.vblank = int(t.group(1))
            m.seiten = "%s:%s" % (t.group(2), t.group(3))
            m.bezug_w = int(t.group(4))
            m.bezug_h = int(t.group(5))
            m.leinwand = int(t.group(6))
            continue

        t = _MENGEN.search(zeile)
        if t:
            m.elemente = int(t.group(1))
            m.primitive = int(t.group(2))
            continue

        t = _EL.search(zeile)
        if t:
            g = t.groups()
            m.liste.append(
                Element(
                    "el", int(g[0]), g[3], g[2],
                    (int(g[4]), int(g[5]), int(g[6]), int(g[7])),
                    (int(g[9]), int(g[10]), int(g[11]), int(g[12])),
                    int(g[8]),
                )
            )
            continue

        t = _PR.search(zeile)
        if t:
            g = t.groups()
            m.liste.append(
                Element(
                    "pr", int(g[0]), g[3], g[2],
                    (int(g[4]), int(g[5]), int(g[6]), int(g[7])),
                    (int(g[9]), int(g[10]), int(g[11]), int(g[12])),
                    int(g[8]),
                )
            )

    return m
