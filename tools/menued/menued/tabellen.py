"""The known names - read, not copied.

Which conditions, actions, boxes and style fields exist is stated in
game/native_menudecl.c in four tables and in include/namespace_MenuDecl.h in
an enum. This module reads them from there.

WHY NOT SIMPLY A LIST HERE. Because it would then be in two places and
one of the two falls behind - the editor would offer an action that
the load path does not know, or conceal one that exists. Both only show up
when the game rejects the file.

If the source is not found - the editor lies elsewhere, or it runs
as a PyInstaller file without the tree next to it -, then it says so and no longer
checks names. An editor that admits it does not know is better than
one that contradicts from an old copy.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from . import ort


@dataclass
class Kastenplatz:
    name: str
    # 1: the file describes this box. 0: it is only a target that
    # "weiter" may name - a "kasten" block on it is an error.
    deklarierbar: bool
    # The expression from s_slots, e.g. "D230.menuMainMenu". It decides which
    # box gets the style of the native main menu (MM_NativeMenu_StyleFor
    # compares against &D230.menuMainMenu) - a list of names here would be
    # the same fact in a second place.
    ausdruck: str = ""
    # posIsCascadeOffset from s_slots: for this box x and y are a
    # shift against the position the drawer sets in the cascade, and
    # not a location in the reference space.
    kaskade: bool = False
    # generic from s_slots: a pure format box that gets the generic proc.
    # 0 means the box already belongs to someone - then its
    # rows stay with the code if the file names none.
    generisch: bool = False


@dataclass
class Tabellen:
    quelle: Path = None
    gefunden: bool = False
    hinweis: str = ""
    bedingungen: list = field(default_factory=list)
    wirkungen: list = field(default_factory=list)
    quellen: list = field(default_factory=list)
    plaetze: list = field(default_factory=list)
    stilfelder: list = field(default_factory=list)
    grenzen: dict = field(default_factory=dict)

    def deklarierbare(self) -> list:
        return [p.name for p in self.plaetze if p.deklarierbar]

    def alle_plaetze(self) -> list:
        return [p.name for p in self.plaetze]

    def hauptmenue(self):
        """The box that gets the style of the native main menu."""
        for p in self.plaetze:
            if p.ausdruck.endswith("menuMainMenu"):
                return p.name
        return None


def _block(text: str, name: str) -> str:
    m = re.search(re.escape(name) + r"\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    return m.group(1) if m else ""


def _namen(block: str) -> list:
    return re.findall(r'\{\s*"([^"]+)"', block)


# The search itself is in ort.py: a packed exe does not find the tree
# through __file__, and where it lies is a question of its own.
def suche_wurzel(start: Path):
    return ort.suche_aufwaerts(start)


def lade(wurzel: Path = None) -> Tabellen:
    t = Tabellen()

    if wurzel is None:
        wurzel, _woher = ort.finde_wurzel()

    if wurzel is None:
        t.hinweis = "game/native_menudecl.c not found - names are not checked"
        return t

    quelle = Path(wurzel) / "game" / "native_menudecl.c"
    t.quelle = quelle

    try:
        text = quelle.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        t.hinweis = "%s: %s - names are not checked" % (quelle, e)
        return t

    t.bedingungen = _namen(_block(text, "s_conditions"))
    t.wirkungen = _namen(_block(text, "s_actions"))
    t.quellen = _namen(_block(text, "s_sources"))
    t.stilfelder = _namen(_block(text, "s_styleFields"))

    # Six fields: name, box, declared, generic, posInTitleAnchor,
    # posIsCascadeOffset. The number is stated here because a table with five
    # fields would silently no longer be found - better a notice than an
    # empty list of names.
    gefunden_slots = re.findall(
        r'\{\s*"([^"]+)",\s*&([^,]+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', _block(text, "s_slots")
    )
    for name, box, deklariert, generisch, _titel, kaskade in gefunden_slots:
        t.plaetze.append(Kastenplatz(name, deklariert == "1", box.strip(),
                                     kaskade == "1", generisch == "1"))

    m = re.search(r"#define\s+NATIVE_MENU_DECL_MIN_WIDTH_MAX\s+(\d+)", text)
    if m:
        t.grenzen["MIN_WIDTH_MAX"] = int(m.group(1))

    kopf = Path(wurzel) / "include" / "namespace_MenuDecl.h"
    if kopf.is_file():
        ktext = kopf.read_text(encoding="utf-8", errors="replace")
        for name in ("POOL", "BOXES", "ROWS", "STRINGS", "STRING_LEN", "NAME_LEN"):
            m = re.search(r"NATIVE_MENU_DECL_" + name + r"\s*=\s*(\d+)", ktext)
            if m:
                t.grenzen[name] = int(m.group(1))

    t.gefunden = bool(t.plaetze)
    if not t.gefunden:
        t.hinweis = "%s read, but no box table in it - names are not checked" % quelle

    return t
