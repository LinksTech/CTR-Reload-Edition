"""How big a box becomes - recomputed when no measurement is available.

THIS IS AN APPROXIMATION AND IT IS TO BE READ AS SUCH. The truth is the
rectangle from the result file; the game drew it. This computation is
a PORT of RECTMENU_GetWidth, RECTMENU_GetHeight, RECTMENU_DrawSelf and
DecalFont_GetLineWidthStrlen, and a port can deviate from its model
without anyone noticing - that is why the editor draws it dashed, and where
both are available, it states the difference.

WHAT THE COMPUTATION CANNOT KNOW:

  Language indices. "text lng 0x4c" is ADVENTURE, but the text is in the
  game data and not in the tree. A row with lng therefore adds nothing to the
  width, and the box is listed as "may be wider".

  --menu-scale. The scale is a run-time switch; the computation uses the
  default value from the source.

  drawStyle. It chooses the narrow or wide shadow and is in no
  declaration. A format box gets 0 (NativeMenuDecl_Commit), i.e. the
  wide one - this file computes with that.

ALL NUMBERS COME FROM THE TREE, none is copied here: the
font sizes from zGlobal_DATA.c, the spacings from g_rectMenuStyleRetail, the
minimum width and the scale from MM_NativeMenu.c.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

FONT_BIG = 1
FONT_SMALL = 2

FP_BITS = 12
FP_ONE = 1 << FP_BITS


def fp_mult(x: int, y: int) -> int:
    """FP_Mult from ctr_math.h: (x*y) >> 12."""
    return (x * y) >> FP_BITS


@dataclass
class Regeln:
    gefunden: bool = False
    hinweis: str = ""
    # per font (1 big, 2 small): width, height, punctuation, buttons
    breite: dict = field(default_factory=dict)
    hoehe: dict = field(default_factory=dict)
    punkt: dict = field(default_factory=dict)
    taste: dict = field(default_factory=dict)
    stil: dict = field(default_factory=dict)
    # The box that gets the minimum width of the native main menu.
    hauptmenue: Optional[str] = None
    breite_zeichen: int = 0
    massstab: int = 100


def _zahlenfeld(text: str, name: str):
    """.name = { 0, 17, 13, 14 }, comments in between allowed."""
    m = re.search(r"\." + re.escape(name) + r"\s*=\s*\{(.*?)\}", text, re.S)
    if not m:
        return None
    werte = re.findall(r"(0x[0-9a-fA-F]+|\d+)\s*,", m.group(1))
    return [int(w, 0) for w in werte]


def lade(wurzel) -> Regeln:
    r = Regeln()

    if wurzel is None:
        r.hinweis = "no tree - the sizes cannot be recomputed"
        return r

    wurzel = Path(wurzel)
    daten = wurzel / "game" / "zGlobal_DATA.c"
    zeichner = wurzel / "game" / "RECTMENU.c"
    nativ = wurzel / "game" / "230" / "MM_NativeMenu.c"

    for p in (daten, zeichner, nativ):
        if not p.is_file():
            r.hinweis = "%s is missing - the sizes cannot be recomputed" % p
            return r

    dtext = daten.read_text(encoding="utf-8", errors="replace")
    for feld, ziel in (
        ("font_charPixWidth", r.breite),
        ("font_charPixHeight", r.hoehe),
        ("font_puncPixWidth", r.punkt),
        ("font_buttonPixWidth", r.taste),
    ):
        werte = _zahlenfeld(dtext, feld)
        if werte is None or len(werte) < 3:
            r.hinweis = "%s not readable in %s" % (feld, daten.name)
            return r
        ziel[FONT_BIG] = werte[FONT_BIG]
        ziel[FONT_SMALL] = werte[FONT_SMALL]

    ztext = zeichner.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"g_rectMenuStyleRetail\s*=\s*\{(.*?)\n\};", ztext, re.S)
    if not m:
        r.hinweis = "g_rectMenuStyleRetail not readable"
        return r
    for name, wert in re.findall(r"\.(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,", m.group(1)):
        r.stil[name] = int(wert, 0)

    ntext = nativ.read_text(encoding="utf-8", errors="replace")
    # The game has no menu width or menu scale setting any more: fixed values.
    r.breite_zeichen = 0
    r.massstab = 100

    fehlend = [k for k in ("rowTopBig", "rowExtraBig", "titleAdvance", "titleHeightBig",
                           "onlyTitleShrink", "frameOffsetX", "frameOffsetY", "frameExtraW",
                           "frameExtraH", "shadowWNarrow", "shadowWWide", "shadowHNarrow",
                           "shadowHWide") if k not in r.stil]
    if fehlend:
        r.hinweis = "missing in the style: %s" % ", ".join(fehlend)
        return r

    r.gefunden = True
    return r


@dataclass
class Zeilenmass:
    text: str
    sicher: bool          # False: language index, the text is not in the tree
    y: int                # top edge of the text, absolute in the reference space
    hoehe: int


@dataclass
class Kastenmass:
    x0: int = 0
    y0: int = 0
    x1: int = 0
    y1: int = 0
    text_x: int = 0
    zentriert: bool = False
    titel: Optional[Zeilenmass] = None
    zeilen: list = field(default_factory=list)
    # Why the result must not count as the truth.
    vorbehalte: list = field(default_factory=list)

    @property
    def breite(self) -> int:
        return self.x1 - self.x0

    @property
    def hoehe(self) -> int:
        return self.y1 - self.y0


def _text_und_sicherheit(t):
    """(display text, whether its width is known)."""
    if t is None:
        return "", True
    if t.art == "lng":
        return "lng 0x%x" % t.wert, False
    return str(t.wert), True


def textbreite(text: str, breite: int, punkt: int, taste: int) -> int:
    """Port of DecalFont_GetLineWidthStrlen. The three additions are deliberately
    no else-if - '@' gets button width AND character width."""
    n = 0
    for c in text:
        if c in "@[^*":
            n += taste
        if c in ":.":
            n += punkt - breite
        if ord(c) > 2:
            n += breite
    return n


# ---------------------------------------------------------------------------
#  The widescreen mapper, as a port of CTR_UI_MapX and CTR_UI_AnchorForBox.
#
#  Needed as soon as the sketch shows the CANVAS and not the reference space:
#  a computed rectangle is in the reference space, but the picture is in the canvas.
#
#  The three edge anchors are pure SHIFTS, not stretches - that is why
#  a mouse drag on the canvas stays a one-to-one change of
#  x in the declaration. Only "leinwand" (canvas) stretches.
# ---------------------------------------------------------------------------

ANKER_MITTE = "mitte"
ANKER_LEINWAND = "leinwand"
ANKER_LINKS = "links"
ANKER_RECHTS = "rechts"


def anker_geraten(x0: int, x1: int, bezug: int) -> str:
    """Port of CTR_UI_AnchorForBox: the thirds rule."""
    if bezug <= 0:
        return ANKER_MITTE
    mitte = (x0 + x1) // 2
    if (mitte * 3) < bezug:
        return ANKER_LINKS
    if (mitte * 3) >= (bezug * 2):
        return ANKER_RECHTS
    return ANKER_MITTE


def bilde_ab(x: int, anker: str, leinwand: int, bezug: int) -> int:
    """Port of CTR_UI_MapX."""
    if leinwand == bezug or bezug <= 0:
        return x
    if anker == ANKER_LEINWAND:
        return (x * leinwand) // bezug
    if anker == ANKER_LINKS:
        return x
    if anker == ANKER_RECHTS:
        return x + (leinwand - bezug)
    return x + (leinwand // 2) - (bezug // 2)


def rechne(kasten, r: Regeln, hauptmenue: bool, kaskade: bool = False) -> Optional[Kastenmass]:
    """The rectangle the result file would report - recomputed.

    hauptmenue: this box gets the style of the native main menu, i.e.
    the scale from --menu-scale and the minimum width in characters.
    kaskade: its position is an OFFSET and not a location in the reference space
    (posIsCascadeOffset in s_slots).
    """
    if not r.gefunden:
        return None

    m = Kastenmass()
    s = r.stil

    # A DECLARED STYLE KNOCKS THE MAIN MENU'S STYLE OUT.
    # MM_NativeMenu_StyleFor first asks NativeMenuDecl_StyleFor, and whatever comes
    # from there is returned unseen - the two fields that set the
    # main menu apart from retail are never reached. A declared
    # style is a copy of g_rectMenuStyleRetail (native_menudecl.c:1074),
    # i.e. minWidthChars 0 and scalePercent 100.
    #
    # Whoever gives the main menu "nimmt-stil" thereby loses its minimum width
    # of twelve characters and its scale from --menu-scale. That is no
    # guess about the drawer but the order of two ifs.
    eigener_stil = kasten.nimmt_stil is not None
    wie_hauptmenue = hauptmenue and not eigener_stil

    skala = (FP_ONE * (r.massstab if wie_hauptmenue else s.get("scalePercent", 100))) // 100

    def S(v: int) -> int:
        return fp_mult(v, skala)

    if eigener_stil:
        m.vorbehalte.append("nimmt-stil '%s': own styles are not recomputed yet - "
                            "the retail spacings apply here" % kasten.nimmt_stil)
        if hauptmenue:
            m.vorbehalte.append(
                "on the main menu, nimmt-stil disables the minimum width of %d characters and the "
                "scale --menu-scale: MM_NativeMenu_StyleFor returns a "
                "declared style before it sets the two fields."
                % r.breite_zeichen)

    klein = kasten.schrift == "klein"
    f = FONT_SMALL if klein else FONT_BIG
    m.zentriert = kasten.ausrichtung == "mitte"

    zeilenhoehe = S(r.hoehe[FONT_SMALL]) if klein else S(r.hoehe[FONT_BIG] + s["rowExtraBig"])
    zeilen_oben = 0 if klein else S(s["rowTopBig"])

    # --- Width: the maximum of the row widths, then the minimum width --------
    cw, pw, bw = S(r.breite[f]), S(r.punkt[f]), S(r.taste[f])
    inhalt = 0
    unbekannt = 0

    eintraege = []
    for z in kasten.zeilen:
        txt, sicher = _text_und_sicherheit(z.text)
        eintraege.append((txt, sicher))
        if sicher:
            inhalt = max(inhalt, textbreite(txt, cw, pw, bw) + 1)
        else:
            unbekannt += 1

    titeltext = None
    if kasten.titel is not None and kasten.titel != "keiner":
        titeltext, titel_sicher = _text_und_sicherheit(kasten.titel)
        # The format does not know BIG_TEXT_IN_TITLE, so the same font as
        # the rows.
        if titel_sicher:
            inhalt = max(inhalt, textbreite(titeltext, cw, pw, bw) + 1)
        else:
            unbekannt += 1

    # THE MINIMUM WIDTH. If the file declares it, its number applies; otherwise the
    # main menu's built-in twelve characters. It is computed with
    # the same formula as in the drawer (RECTMENU_ProcessState), in FONT_BIG
    # even when the rows are set small - the drawer explicitly takes
    # font_charPixWidth[FONT_BIG] there.
    zeichen = kasten.mindestbreite
    if zeichen is None:
        zeichen = r.breite_zeichen if wie_hauptmenue else 0

    menubreite = inhalt
    if zeichen > 0:
        mindest = zeichen * fp_mult(r.breite[FONT_BIG], skala) + 1
        menubreite = max(menubreite, mindest)

    # WHERE THIS NUMBER REALLY APPLIES. RECTMENU draws a cascade with ONE
    # width, and it is clamped with the minimum width of the ACTIVE box,
    # i.e. the root of the chain. A child only raises it. The editor computes
    # every box on its own - what comes out in the cascade can only be seen
    # in the result file.
    if (kasten.mindestbreite is not None) and not hauptmenue:
        m.vorbehalte.append(
            "breite mindestens %d: in a cascade the minimum width of the ROOT counts, "
            "a child can only raise it. Here the box stands on its own."
            % kasten.mindestbreite)

    if unbekannt:
        if menubreite > inhalt:
            m.vorbehalte.append(
                "%d row(s) with a language index: their text is in the game data. The "
                "minimum width %d applies here - longer than 12 characters would be wider." % (unbekannt, menubreite))
        else:
            m.vorbehalte.append(
                "%d row(s) with a language index: their text is in the game data and does not "
                "count here. The box may be wider." % unbekannt)

    # --- Height --------------------------------------------------------------
    eigen = zeilenhoehe * len(kasten.zeilen)
    titel_vorschub = 0
    if titeltext is not None:
        # RECTMENU_GetHeight: one row height plus the advance. In DrawSelf
        # the same number is called local_48 + RM_S(titleAdvance).
        eigen += zeilenhoehe + S(s["titleAdvance"])
        titel_vorschub = zeilenhoehe + S(s["titleAdvance"])

    # --- Position ------------------------------------------------------------
    x = kasten.x or 0
    y = kasten.y or 0
    mitte_x = kasten.bezug in ("mitte", "mitte-x")
    mitte_y = kasten.bezug in ("mitte", "mitte-y")

    # C divides towards zero: -menuWidth / 2.
    versatz_x = -(menubreite // 2) if mitte_x else 0
    versatz_y = -(eigen // 2) if mitte_y else 0

    rand_x = versatz_x + x - S(s["frameOffsetX"])
    rand_y = versatz_y + y - S(s["frameOffsetY"])
    rand_w = menubreite + S(s["frameExtraW"])
    rand_h = eigen + S(s["frameExtraH"]) - (1 if klein else 0)

    # The shadow is attached at the right and at the bottom. It belongs to what is
    # drawn, and so also to the rectangle the result file reports - otherwise
    # we would compare two different things. drawStyle 0: the wide one.
    rand_w += S(s["shadowWWide"])
    rand_h += S(s["shadowHWide"])

    m.x0, m.y0 = rand_x, rand_y
    m.x1, m.y1 = rand_x + rand_w, rand_y + rand_h
    m.text_x = (versatz_x + x + menubreite // 2) if m.zentriert else (versatz_x + x + 1)

    # --- Rows ----------------------------------------------------------------
    py = zeilen_oben + versatz_y + y
    if titeltext is not None:
        m.titel = Zeilenmass(titeltext, True, py, r.hoehe[f])
        py += titel_vorschub

    for txt, sicher in eintraege:
        m.zeilen.append(Zeilenmass(txt, sicher, py, r.hoehe[f]))
        py += zeilenhoehe

    if kasten.x is None and kasten.y is None:
        m.vorbehalte.append("no x and no y: drawn at 0,0. In the cascade the drawer sets the "
                            "position, and the editor does not recompute it.")

    if kaskade:
        m.vorbehalte.append(
            "cascade box: RECTMENU_DrawSelf passes posX + posX_prev of the parent "
            "on to a child. x and y are an OFFSET here onto the position the "
            "drawer sets - the sketch shows them as a location, and that is what they are not.")

    if not kasten.zeilen:
        m.vorbehalte.append(
            "no zeile: the rows of this box are in the code and stay there. What stands "
            "here is the width from title and minimum width alone - in the game the box "
            "is as wide as its real rows.")

    return m
