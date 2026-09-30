"""Reading and writing menus/*.menu - without Tkinter, without game code.

This module is a PORT of the load path from game/native_menudecl.c, not a
second interpretation of it. The tokenizer follows NativeMenuDecl_Tokenize character for
character, the key names are in the same order as there. What the
load path reports as an error with a line number, this module reports as
MenuFormatFehler with the same line number.

WHAT IS NOT HERE AND WHY.

The names of the verbs, the boxes and the style fields. They are in
game/native_menudecl.c in three tables, and a copied list would be
the same fact in a second place - exactly the mistake this project
has already collected several times. tabellen.py reads them from the C source.

THE COMMENTS SURVIVE. nitro-pit.menu carries forty lines of reasoning
why a box sits where it sits. An editor that rewrites canonically on saving
and eats the reasoning in the process is a tool that
one uses once. Every block and every row therefore carries its preamble -
the comment and blank lines directly above it - along unchanged.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional, Union

# A text value: either a language index ("lng 0x4c") or an own
# string ('"NITRO-PIT"'). Both in one place, because the load path accepts both
# in the same place (NativeMenuDecl_ParseText).
LNG = "lng"
EIGEN = "eigen"


@dataclass
class Text:
    art: str  # LNG | EIGEN
    wert: Union[int, str]

    def __str__(self) -> str:
        if self.art == LNG:
            # Hex, as in the source and in the existing file. The
            # language table is written in hexadecimal everywhere; decimal
            # would be the same value and a different language.
            return "lng 0x%x" % self.wert
        return '"%s"' % self.wert

    def anzeige(self) -> str:
        if self.art == LNG:
            return "lng 0x%x" % self.wert
        return str(self.wert)


class MenuFormatFehler(Exception):
    def __init__(self, zeile: int, was: str, detail: str) -> None:
        super().__init__("%d: %s '%s'" % (zeile, was, detail))
        self.zeile = zeile
        self.was = was
        self.detail = detail


# ---------------------------------------------------------------------------
#  The model.
# ---------------------------------------------------------------------------

BEZUG_WERTE = ("ecke", "mitte", "mitte-x", "mitte-y")
LAYOUT_WERTE = ("spalte",)
# State bits with a key. On loading they are set AND cleared under a mask
# - "gross" is therefore a statement and not an omission.
SCHRIFT_WERTE = ("gross", "klein")
AUSRICHTUNG_WERTE = ("links", "mitte")
# Which edge the box hangs on when the picture gets wider. No
# state bit - RECTMENU does not know the anchor, only the widescreen mapper
# asks for it. Without a value it guesses by the thirds rule.
ANKER_WERTE = ("mitte", "links", "rechts", "leinwand")

# NATIVE_MENU_DECL_MIN_WIDTH_MAX from game/native_menudecl.c. No second place:
# tabellen.py reads the limits from the tree, and this one is the
# fallback when no tree was found.
MINDESTBREITE_MAX = 64
OEFFNEN_WERTE = ("gestapelt", "ersetzen")



@dataclass
class Zeile:
    # The name is a pure label: NativeMenuDecl_ParseRow reads tok[1] only
    # for its error messages and stores it nowhere. It is in the
    # form anyway, because it names the row in the tree.
    name: str
    text: Optional[Text] = None
    text_gesperrt: Optional[Text] = None
    bedingung: Optional[str] = None
    negiert: bool = False
    wirkung: Optional[str] = None
    weiter: Optional[str] = None
    # None means "not named". The load path then takes gestapelt - and the
    # file does not get a word written into it on saving that it never had.
    oeffnen: Optional[str] = None
    vorspann: list = field(default_factory=list)


@dataclass
class Kasten:
    name: str
    x: Optional[int] = None
    y: Optional[int] = None
    bezug: Optional[str] = None
    layout: Optional[str] = None
    schrift: Optional[str] = None
    ausrichtung: Optional[str] = None
    anker: Optional[str] = None
    # Minimum width in CHARACTERS, or None for "not named". 0 is a
    # valid value and means "no minimum width" - in the main menu it takes
    # away the built-in twelve characters.
    mindestbreite: Optional[int] = None
    titel: Optional[Union[Text, str]] = None  # Text or "keiner"
    nimmt_stil: Optional[str] = None
    zurueck: Optional[str] = None
    zeilen: list = field(default_factory=list)
    vorspann: list = field(default_factory=list)
    # Comments that stood between the header line and the first key.
    kopf: list = field(default_factory=list)

    @property
    def art(self) -> str:
        return "kasten"


@dataclass
class Stil:
    name: str
    felder: dict = field(default_factory=dict)
    vorspann: list = field(default_factory=list)
    kopf: list = field(default_factory=list)

    @property
    def art(self) -> str:
        return "stil"


@dataclass
class Datei:
    bloecke: list = field(default_factory=list)
    nachspann: list = field(default_factory=list)

    def kasten(self, name: str) -> Optional[Kasten]:
        for b in self.bloecke:
            if b.art == "kasten" and b.name == name:
                return b
        return None

    def kaesten(self) -> list:
        return [b for b in self.bloecke if b.art == "kasten"]

    def stile(self) -> list:
        return [b for b in self.bloecke if b.art == "stil"]


# ---------------------------------------------------------------------------
#  Splitting a line. Port of NativeMenuDecl_Tokenize.
#
#  Whitespace separates, " holds together, # starts a comment - but only at
#  a field boundary: abc#def is ONE field, just as in the C tokenizer.
# ---------------------------------------------------------------------------

def zerlege(zeile: str) -> list:
    tok = []
    i = 0
    n = len(zeile)

    while i < n:
        while i < n and zeile[i] in " \t\r\n":
            i += 1

        if i >= n or zeile[i] == "#":
            break

        if zeile[i] == '"':
            i += 1
            start = i
            while i < n and zeile[i] != '"':
                i += 1
            tok.append(zeile[start:i])
            if i < n:
                i += 1
            continue

        start = i
        while i < n and zeile[i] not in " \t\r\n":
            i += 1
        tok.append(zeile[start:i])

    return tok


def lies_zahl(s: str):
    """Port of NativeMenuDecl_ParseNumber. None if it is not a number."""
    negativ = False
    if s.startswith("-"):
        negativ = True
        s = s[1:]

    basis = 10
    if s[:2].lower() == "0x":
        basis = 16
        s = s[2:]

    if not s:
        return None

    wert = 0
    for c in s:
        if "0" <= c <= "9":
            ziffer = ord(c) - ord("0")
        elif basis == 16 and c.lower() in "abcdef":
            ziffer = ord(c.lower()) - ord("a") + 10
        else:
            return None
        wert = wert * basis + ziffer

    return -wert if negativ else wert


# ---------------------------------------------------------------------------
#  Reading.
# ---------------------------------------------------------------------------

def _text_lesen(tok: list, at: int, nr: int):
    """Returns (Text, fields consumed). Port of ParseText."""
    if at >= len(tok):
        raise MenuFormatFehler(nr, "without a value", tok[at - 1])

    if tok[at] == "lng":
        if at + 1 >= len(tok):
            raise MenuFormatFehler(nr, "not a number after lng", tok[at])
        wert = lies_zahl(tok[at + 1])
        if wert is None:
            raise MenuFormatFehler(nr, "not a number after lng", tok[at])
        return Text(LNG, wert), 2

    return Text(EIGEN, tok[at]), 1


def _zeile_lesen(tok: list, nr: int, vorspann: list) -> Zeile:
    if len(tok) < 2:
        raise MenuFormatFehler(nr, "zeile without a name", tok[0])

    z = Zeile(name=tok[1], vorspann=vorspann)
    at = 2

    while at < len(tok):
        key = tok[at]

        if key == "text":
            z.text, benutzt = _text_lesen(tok, at + 1, nr)
            at += 1 + benutzt
        elif key == "text-gesperrt":
            z.text_gesperrt, benutzt = _text_lesen(tok, at + 1, nr)
            at += 1 + benutzt
        elif key in ("gesperrt-wenn", "gesperrt-wenn-nicht"):
            if at + 1 >= len(tok):
                raise MenuFormatFehler(nr, "without a value", key)
            z.bedingung = tok[at + 1]
            z.negiert = key == "gesperrt-wenn-nicht"
            at += 2
        elif key == "wirkung":
            if at + 1 >= len(tok):
                raise MenuFormatFehler(nr, "without a value", key)
            z.wirkung = tok[at + 1]
            at += 2
        elif key == "weiter":
            if at + 1 >= len(tok):
                raise MenuFormatFehler(nr, "without a value", key)
            z.weiter = tok[at + 1]
            at += 2
        elif key == "oeffnen":
            if at + 1 >= len(tok):
                raise MenuFormatFehler(nr, "without a value", key)
            if tok[at + 1] not in OEFFNEN_WERTE:
                raise MenuFormatFehler(nr, "neither gestapelt nor ersetzen", tok[at + 1])
            z.oeffnen = tok[at + 1]
            at += 2
        else:
            raise MenuFormatFehler(nr, "not a zeile key", key)

    if z.text is None:
        raise MenuFormatFehler(nr, "zeile without text", tok[1])

    return z


def _kastenschluessel_lesen(k: Kasten, tok: list, nr: int, vorspann: list) -> None:
    key = tok[0]

    if key == "zeile":
        k.zeilen.append(_zeile_lesen(tok, nr, vorspann))
        return

    if len(tok) < 2:
        raise MenuFormatFehler(nr, "without a value", key)

    if key in ("x", "y"):
        wert = lies_zahl(tok[1])
        if wert is None:
            raise MenuFormatFehler(nr, "not a number", tok[1])
        setattr(k, key, wert)
        return

    if key == "bezug":
        if tok[1] not in BEZUG_WERTE:
            raise MenuFormatFehler(nr, "no such bezug", tok[1])
        k.bezug = tok[1]
        return

    if key == "layout":
        if tok[1] not in LAYOUT_WERTE:
            raise MenuFormatFehler(nr, "no such layout", tok[1])
        k.layout = tok[1]
        return

    if key == "schrift":
        if tok[1] not in SCHRIFT_WERTE:
            raise MenuFormatFehler(nr, "neither klein nor gross", tok[1])
        k.schrift = tok[1]
        return

    if key == "ausrichtung":
        if tok[1] not in AUSRICHTUNG_WERTE:
            raise MenuFormatFehler(nr, "neither links nor mitte", tok[1])
        k.ausrichtung = tok[1]
        return

    if key == "anker":
        if tok[1] not in ANKER_WERTE:
            raise MenuFormatFehler(nr, "no such anker", tok[1])
        k.anker = tok[1]
        return

    if key == "breite":
        # Two words, because the load path expects two: "breite mindestens N".
        # The word in between leaves room for "breite fest N".
        if len(tok) < 3:
            raise MenuFormatFehler(nr, "breite needs 'mindestens' and a number", key)
        if tok[1] != "mindestens":
            raise MenuFormatFehler(nr, "no word after breite", tok[1])
        wert = lies_zahl(tok[2])
        if wert is None:
            raise MenuFormatFehler(nr, "not a number", tok[2])
        if wert < 0 or wert > MINDESTBREITE_MAX:
            raise MenuFormatFehler(nr, "outside 0..%d characters" % MINDESTBREITE_MAX, tok[2])
        k.mindestbreite = wert
        return

    if key == "titel":
        if tok[1] == "keiner":
            k.titel = "keiner"
            return
        k.titel, _ = _text_lesen(tok, 1, nr)
        return

    if key == "nimmt-stil":
        k.nimmt_stil = tok[1]
        return

    if key == "zurueck":
        k.zurueck = tok[1]
        return

    raise MenuFormatFehler(nr, "not a kasten key", key)


def lade(text: str) -> Datei:
    d = Datei()
    block = None
    vorspann = []

    for nr, roh in enumerate(text.splitlines(), start=1):
        tok = zerlege(roh)

        if not tok:
            # Blank and comment line: it belongs to what comes
            # next. If nothing follows, it becomes the epilogue.
            vorspann.append(roh.rstrip("\r\n"))
            continue

        if tok[0] == "kasten":
            if len(tok) < 2:
                raise MenuFormatFehler(nr, "kasten without a name", tok[0])
            if d.kasten(tok[1]) is not None:
                raise MenuFormatFehler(nr, "declared twice", tok[1])
            block = Kasten(name=tok[1], vorspann=vorspann)
            d.bloecke.append(block)
            vorspann = []
            continue

        if tok[0] == "stil":
            if len(tok) < 2:
                raise MenuFormatFehler(nr, "stil without a name", tok[0])
            if any(b.art == "stil" and b.name == tok[1] for b in d.bloecke):
                raise MenuFormatFehler(nr, "declared twice", tok[1])
            block = Stil(name=tok[1], vorspann=vorspann)
            d.bloecke.append(block)
            vorspann = []
            continue

        if block is None:
            raise MenuFormatFehler(nr, "outside any block", tok[0])

        if block.art == "kasten":
            if tok[0] == "zeile":
                _kastenschluessel_lesen(block, tok, nr, vorspann)
            else:
                # Comments before an ordinary key move into the
                # block header. They are kept, but not in their place -
                # because the keys are brought into a fixed order on writing
                # and a comment would otherwise stand before the wrong
                # one.
                block.kopf.extend(vorspann)
                _kastenschluessel_lesen(block, tok, nr, [])
            vorspann = []
            continue

        if len(tok) < 2:
            raise MenuFormatFehler(nr, "without a value", tok[0])
        wert = lies_zahl(tok[1])
        if wert is None:
            raise MenuFormatFehler(nr, "not a number", tok[1])
        block.kopf.extend(vorspann)
        block.felder[tok[0]] = wert
        vorspann = []

    d.nachspann = vorspann
    return d


# ---------------------------------------------------------------------------
#  Writing.
#
#  Canonical here means: fixed key order, one key per line,
#  two spaces as column separator, numbers unquoted, own strings
#  always in quotes. What the file never said, it does not say afterwards
#  either - the editor writes no default values into it that were not there
#  before.
#
#  ONE ROW STAYS ONE LINE. "zeile <name> text ... wirkung ..." cannot be
#  spread over several lines: NativeMenuDecl_ParseRow reads one line
#  as a complete row declaration, and a "text" on the next
#  line would be a box key for the load path that does not exist.
# ---------------------------------------------------------------------------

def _zeilenspalten(z: Zeile) -> list:
    sp = ["zeile " + z.name]

    if z.text is not None:
        sp.append("text " + str(z.text))
    if z.text_gesperrt is not None:
        sp.append("text-gesperrt " + str(z.text_gesperrt))
    if z.bedingung is not None:
        key = "gesperrt-wenn-nicht" if z.negiert else "gesperrt-wenn"
        sp.append(key + " " + z.bedingung)
    if z.wirkung is not None:
        sp.append("wirkung " + z.wirkung)
    if z.weiter is not None:
        sp.append("weiter " + z.weiter)
    if z.oeffnen is not None:
        sp.append("oeffnen " + z.oeffnen)

    return sp


def _kasten_schreiben(k: Kasten, aus: list) -> None:
    aus.append("kasten " + k.name)
    aus.extend(k.kopf)

    if k.x is not None:
        aus.append("  x %d" % k.x)
    if k.y is not None:
        aus.append("  y %d" % k.y)
    if k.bezug is not None:
        aus.append("  bezug " + k.bezug)
    if k.layout is not None:
        aus.append("  layout " + k.layout)
    if k.schrift is not None:
        aus.append("  schrift " + k.schrift)
    if k.ausrichtung is not None:
        aus.append("  ausrichtung " + k.ausrichtung)
    if k.anker is not None:
        aus.append("  anker " + k.anker)
    if k.mindestbreite is not None:
        aus.append("  breite mindestens %d" % k.mindestbreite)
    if k.titel is not None:
        aus.append("  titel " + (k.titel if isinstance(k.titel, str) else str(k.titel)))
    if k.nimmt_stil is not None:
        aus.append("  nimmt-stil " + k.nimmt_stil)
    if k.zurueck is not None:
        aus.append("  zurueck " + k.zurueck)

    # Pad columns to the widest of their kind in the box. Pure looks -
    # but a row table one can skim with the eye is the
    # reason why the file is more readable than the C array before at all.
    tabelle = [_zeilenspalten(z) for z in k.zeilen]
    breiten = []
    for sp in tabelle:
        for i, s in enumerate(sp):
            if i >= len(breiten):
                breiten.append(0)
            breiten[i] = max(breiten[i], len(s))

    for z, sp in zip(k.zeilen, tabelle):
        aus.extend(z.vorspann)
        teile = []
        for i, s in enumerate(sp):
            teile.append(s.ljust(breiten[i]) if i < len(sp) - 1 else s)
        aus.append("  " + "  ".join(teile))


def _stil_schreiben(s: Stil, aus: list) -> None:
    aus.append("stil " + s.name)
    aus.extend(s.kopf)
    for name, wert in s.felder.items():
        aus.append("  %s %d" % (name, wert))


def schreibe(d: Datei) -> str:
    aus = []

    for b in d.bloecke:
        aus.extend(b.vorspann)
        if b.art == "kasten":
            _kasten_schreiben(b, aus)
        else:
            _stil_schreiben(b, aus)

    aus.extend(d.nachspann)

    return "\n".join(aus) + "\n"
