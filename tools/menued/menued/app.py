"""Tkinter front end: edit menus/*.menu while the game is running.

The workflow this window was built for: the game with menu reload on one
screen, this window on the other (this build of the game has no switch
that turns menu reload on). Save - the game rereads within
one second and writes menus/nitro-pit.ergebnis; this window reads
the file and shows next to it what came out.

NO REBUILT MENU. This window draws no font the way the
game draws it, and no frame styles and no colours. The sketch shows
the game picture that the load path writes next to the result file on every reload,
and on top of it rectangles with their rows. SOLID means measured,
DASHED means recomputed in the editor - an approximation, not the truth.

WHAT ONE CAN RELY ON IN THE SKETCH:

  The picture    - a capture of the state the game read last.
                   After a change it is outdated, and the marker says so.
  The rectangle  - measured, as long as the result file matches the declaration.
  The menu image - is cut out of the background while dragging and carried along
                   with the rectangle, so that there are not two boxes.
                   Its SIZES are those of the measurement, not those of the rows
                   currently in the file.

The format knows the three keys anker, schrift and ausrichtung, and this
window sets them. If anker is missing in the file, it is
guessed from the position as before - the third lines in the sketch show where the
answer flips.
"""
from __future__ import annotations

import io
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from . import ergebnis as ERG
from . import format as F
from . import masse as MASS
from . import ort as ORT
from . import tabellen as TAB

NICHTS = "(not named)"
POLL_MS = 500


def _lies(pfad: Path) -> str:
    with io.open(pfad, "r", encoding="utf-8", newline="") as f:
        return f.read()


def _schreib(pfad: Path, text: str) -> None:
    # newline="\n": the .menu files in the tree have LF, and an editor that
    # turns that into CRLF produces a difference in every line.
    with io.open(pfad, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


class Fenster(ttk.Frame):
    def __init__(self, master, arg=None) -> None:
        super().__init__(master, padding=6)
        self.grid(row=0, column=0, sticky="nsew")
        master.rowconfigure(0, weight=1)
        master.columnconfigure(0, weight=1)

        # THE ROOT FIRST. Verb tables, menu file and result file all
        # three lie below it - one question, not three.
        self.wurzel, self.woher = ORT.finde_wurzel(arg)
        self.tab = TAB.lade(self.wurzel)
        # The drawing rules for the recomputation: font sizes, spacings,
        # minimum width. All from the tree, none copied.
        self.regeln = MASS.lade(self.wurzel)
        self.datei = F.Datei()
        self.pfad = None
        self.messung = ERG.Messung()
        self.mtime_menu = None
        self.mtime_erg = None
        self.mtime_bild = None
        # The picture stays attached to the window: Tkinter by itself holds no
        # reference to a PhotoImage, and a collected one is drawn as an empty
        # rectangle.
        self.bild = None
        self.bild_w = 0
        self.bild_h = 0
        # The menu pieces cut out of the picture, by source and
        # scale. The same piece is needed again on every mouse event
        # and only changes when a new picture comes.
        self._schnipsel = {}
        self.geaendert = False
        self._fuellt = False
        self._zieht = None
        # Grid spacing in reference points. 0 means no snapping.
        self._raster = 0
        self._hilfslinien = True
        self._skala = 1.0
        self._ursprung = (0.0, 0.0)
        self._rechtecke = {}
        # Tkinter only remembers the NAME of a variable. If the
        # Python object falls victim to the collector, its __del__ deletes the
        # variable in the interpreter, and the field silently becomes empty. That is why
        # the window holds every variable of the form currently shown.
        self._halten = []

        self._bauen()

        _w, genannt = ORT.aus_argument(arg)

        if self.wurzel is None:
            # Nothing guessed: ask once, then remember.
            self._wurzel_erfragen(zuerst=True)
        elif genannt is not None:
            self.oeffnen(genannt)
        else:
            vorschlag = ORT.menuedatei(self.wurzel)
            if vorschlag is not None:
                self.oeffnen(vorschlag)
            else:
                self._sagen("tree %s (%s), but %s is missing" % (self.wurzel, self.woher, ORT.MENUEDATEI))

        self.after(POLL_MS, self._takt)

    # -- Construction -------------------------------------------------------

    def _bauen(self) -> None:
        self.rowconfigure(1, weight=3)
        self.rowconfigure(2, weight=2)
        self.columnconfigure(0, weight=1)

        kopf = ttk.Frame(self)
        kopf.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        kopf.columnconfigure(1, weight=1)
        ttk.Label(kopf, text="File").grid(row=0, column=0, padx=(0, 6))
        self.v_pfad = tk.StringVar()
        ttk.Entry(kopf, textvariable=self.v_pfad, state="readonly").grid(row=0, column=1, sticky="ew")
        ttk.Button(kopf, text="Tree", command=self._wurzel_erfragen).grid(row=0, column=2, padx=3)
        ttk.Button(kopf, text="Open", command=self._oeffnen_dialog).grid(row=0, column=3, padx=3)
        ttk.Button(kopf, text="Reload", command=self._neu_laden).grid(row=0, column=4, padx=3)
        ttk.Button(kopf, text="Check", command=self._pruefen_zeigen).grid(row=0, column=5, padx=3)
        self.b_speichern = ttk.Button(kopf, text="Save", command=self.speichern)
        self.b_speichern.grid(row=0, column=6, padx=3)

        self.l_wurzel = ttk.Label(kopf, text="", foreground="#888")
        self.l_wurzel.grid(row=1, column=0, columnspan=7, sticky="w", pady=(2, 0))

        mitte = ttk.PanedWindow(self, orient="horizontal")
        mitte.grid(row=1, column=0, sticky="nsew")

        links = ttk.Frame(mitte)
        links.rowconfigure(0, weight=1)
        links.columnconfigure(0, weight=1)
        mitte.add(links, weight=1)

        self.baum = ttk.Treeview(links, show="tree", selectmode="browse")
        self.baum.grid(row=0, column=0, sticky="nsew")
        sb = ttk.Scrollbar(links, orient="vertical", command=self.baum.yview)
        sb.grid(row=0, column=1, sticky="ns")
        self.baum.configure(yscrollcommand=sb.set)
        self.baum.bind("<<TreeviewSelect>>", lambda e: self._auswahl())

        knoepfe = ttk.Frame(links)
        knoepfe.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(4, 0))
        for text, cmd in (
            ("Row +", self._zeile_neu),
            ("Row -", self._zeile_weg),
            ("up", lambda: self._zeile_schieben(-1)),
            ("down", lambda: self._zeile_schieben(1)),
        ):
            ttk.Button(knoepfe, text=text, width=8, command=cmd).pack(side="left", padx=1)
        knoepfe2 = ttk.Frame(links)
        knoepfe2.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(2, 0))
        self.b_kasten = ttk.Button(knoepfe2, text="Box +", width=10, command=self._kasten_neu)
        self.b_kasten.pack(side="left", padx=1)
        ttk.Button(knoepfe2, text="Style +", width=10, command=self._stil_neu).pack(side="left", padx=1)
        ttk.Button(knoepfe2, text="Block -", width=10, command=self._block_weg).pack(side="left", padx=1)

        rechts = ttk.Frame(mitte)
        rechts.rowconfigure(0, weight=1)
        rechts.columnconfigure(0, weight=1)
        mitte.add(rechts, weight=2)

        self.formular = ttk.LabelFrame(rechts, text="Selection", padding=6)
        self.formular.grid(row=0, column=0, sticky="nsew")

        unten = ttk.PanedWindow(self, orient="horizontal")
        unten.grid(row=2, column=0, sticky="nsew", pady=(6, 0))

        skizze = ttk.LabelFrame(unten, text="Sketch - drag to move", padding=4)
        skizze.rowconfigure(2, weight=1)
        skizze.columnconfigure(0, weight=1)
        unten.add(skizze, weight=3)

        leiste = ttk.Frame(skizze)
        leiste.grid(row=0, column=0, sticky="ew", pady=(0, 3))
        ttk.Label(leiste, text="Grid").pack(side="left")
        self.v_raster = tk.StringVar(value="off")
        raster = ttk.Combobox(leiste, textvariable=self.v_raster, width=6, state="readonly",
                              values=("off", "2", "4", "8", "16", "32", "64"))
        raster.pack(side="left", padx=(4, 10))
        self.v_raster.trace_add("write", lambda *a: self._raster_setzen())
        self.v_hilfslinien = tk.BooleanVar(value=True)
        hl = ttk.Checkbutton(leiste, text="Guides", variable=self.v_hilfslinien)
        hl.configure(command=self._hilfslinien_setzen)
        hl.pack(side="left")
        ttk.Label(leiste, text="centre, thirds (the anchor boundary), margins",
                  foreground="#888").pack(side="left", padx=8)

        # WHAT IS ALIGNED IS THE RECTANGLE, NOT THE NUMBER. What a button sets
        # is x or y in the declaration - where it sets it follows
        # from the rectangle that results from it. That way none of these
        # buttons needs to know whether the box has "bezug mitte" or "bezug ecke"
        # and whether its anchor hangs left, centred or right: half the width
        # and the shift are already in the rectangle.
        zweite = ttk.Frame(skizze)
        zweite.grid(row=1, column=0, sticky="ew", pady=(0, 3))
        ttk.Label(zweite, text="Align").pack(side="left")
        self._knoepfe = []
        for beschriftung, was, luft in (
            ("left edge", "links", (4, 1)),
            ("centre horizontally", "mitte-x", (1, 1)),
            ("right edge", "rechts", (1, 10)),
            ("top edge", "oben", (1, 1)),
            ("centre vertically", "mitte-y", (1, 1)),
            ("bottom edge", "unten", (1, 10)),
            ("centre both", "mitte", (1, 1)),
        ):
            b = ttk.Button(zweite, text=beschriftung, width=len(beschriftung) + 1,
                           command=lambda w=was: self._ausrichten(w))
            b.pack(side="left", padx=luft)
            self._knoepfe.append(b)

        self.leinwand = tk.Canvas(skizze, background="#20242b", highlightthickness=0)
        self.leinwand.grid(row=2, column=0, sticky="nsew")
        self.leinwand.bind("<Configure>", lambda e: self._skizze())
        self.leinwand.bind("<Button-1>", self._greifen)
        self.leinwand.bind("<B1-Motion>", self._ziehen)
        self.leinwand.bind("<ButtonRelease-1>", self._loslassen)

        gemessen = ttk.LabelFrame(unten, text="Wanted and measured", padding=4)
        gemessen.rowconfigure(1, weight=1)
        gemessen.columnconfigure(0, weight=1)
        unten.add(gemessen, weight=2)
        self.l_messung = ttk.Label(gemessen, text="no result file", foreground="#888")
        self.l_messung.grid(row=0, column=0, sticky="w")
        self.tabelle = ttk.Treeview(gemessen, columns=("gewollt", "gerechnet", "gemessen"),
                                    show="tree headings", height=9)
        self.tabelle.heading("#0", text="")
        self.tabelle.heading("gewollt", text="wanted")
        self.tabelle.heading("gerechnet", text="computed")
        self.tabelle.heading("gemessen", text="measured")
        self.tabelle.column("#0", width=92, stretch=False)
        self.tabelle.column("gewollt", width=78, anchor="e", stretch=False)
        self.tabelle.column("gerechnet", width=118, anchor="e", stretch=False)
        self.tabelle.column("gemessen", width=118, anchor="e")
        self.tabelle.tag_configure("abweichung", foreground="#c80")
        self.tabelle.tag_configure("vorbehalt", foreground="#7a6")
        self.tabelle.grid(row=1, column=0, sticky="nsew")

        # Keyboard shortcuts. Ctrl+S is what every hand at an editor presses blindly -
        # and it makes saving controllable from outside, which would otherwise not
        # be possible without a mouse.
        top = self.winfo_toplevel()
        top.bind("<Control-s>", lambda e: self.speichern())
        top.bind("<Control-S>", lambda e: self.speichern())
        top.bind("<Control-r>", lambda e: self._neu_laden())
        top.bind("<F5>", lambda e: self._neu_laden())

        self.status = ttk.Label(self, text="", anchor="w")
        self.status.grid(row=3, column=0, sticky="ew", pady=(4, 0))

        self._wurzel_zeigen()

        if not self.tab.gefunden:
            self._sagen(self.tab.hinweis)

    # -- File ---------------------------------------------------------------

    def _wurzel_zeigen(self) -> None:
        if self.wurzel is None:
            self.l_wurzel.configure(text="Tree: not found - names are not checked",
                                    foreground="#c80")
            return
        self.l_wurzel.configure(text="Tree: %s  (%s)" % (self.wurzel, self.woher), foreground="#888")

    def _wurzel_erfragen(self, zuerst: bool = False) -> None:
        """Ask once and remember the answer - not anew on every start."""
        if zuerst:
            messagebox.showinfo(
                "Menu editor",
                "The CTR Reload tree was not found.\n\n"
                "The search was for %s - upwards from the own location and from the\n"
                "working directory. Please choose the folder in which game/ lies." % ORT.BAUMMARKE,
            )

        name = filedialog.askdirectory(
            title="Root of CTR Reload (the folder with game/)",
            initialdir=str(self.wurzel) if self.wurzel else None,
        )

        if not name:
            self._wurzel_zeigen()
            return

        gewaehlt = ORT.suche_aufwaerts(Path(name))

        if gewaehlt is None:
            messagebox.showerror("Menu editor", "In %s there is no %s." % (name, ORT.BAUMMARKE))
            return

        self.wurzel = gewaehlt
        self.woher = "remembered" if ORT.merke_wurzel(gewaehlt) else "chosen, cannot be remembered"
        self.tab = TAB.lade(self.wurzel)
        self.regeln = MASS.lade(self.wurzel)
        self._wurzel_zeigen()

        vorschlag = ORT.menuedatei(self.wurzel)
        if vorschlag is not None:
            self.oeffnen(vorschlag)
        else:
            self._sagen("tree set, but %s is missing in it" % ORT.MENUEDATEI)

    def _oeffnen_dialog(self) -> None:
        name = filedialog.askopenfilename(
            title="Menu file",
            initialdir=str(self.wurzel / "menus") if self.wurzel else None,
            filetypes=[("Menu file", "*.menu"), ("All", "*.*")],
        )
        if name:
            self.oeffnen(Path(name))

    def oeffnen(self, pfad: Path) -> None:
        try:
            roh = _lies(pfad)
        except OSError as e:
            messagebox.showerror("Menu editor", str(e))
            return

        try:
            self.datei = F.lade(roh)
        except F.MenuFormatFehler as e:
            messagebox.showerror("Menu editor", "%s:%s" % (pfad, e))
            return

        self.pfad = pfad
        self.v_pfad.set(str(pfad))
        self.mtime_menu = pfad.stat().st_mtime
        self.geaendert = False
        self.messung = ERG.Messung()
        self.mtime_erg = None
        self.mtime_bild = None
        self._ergebnis_lesen(zwingen=True)
        self._bild_lesen(zwingen=True)
        self._baum_fuellen()
        self._sagen("read: %d boxes, %d styles" % (len(self.datei.kaesten()), len(self.datei.stile())))

    def _neu_laden(self) -> None:
        if self.pfad is None:
            return
        if self.geaendert and not messagebox.askokcancel(
            "Menu editor", "Discard unsaved changes?"
        ):
            return
        self.oeffnen(self.pfad)

    def speichern(self) -> None:
        if self.pfad is None:
            return

        fehler, warnungen = self.pruefen()
        if fehler:
            messagebox.showerror(
                "Menu editor",
                "The load path would reject this file:\n\n" + "\n".join(fehler),
            )
            return

        if self.mtime_menu is not None and self.pfad.is_file():
            if self.pfad.stat().st_mtime > self.mtime_menu + 0.001:
                if not messagebox.askokcancel(
                    "Menu editor",
                    "The file was changed from outside since it was read.\nOverwrite?",
                ):
                    return

        try:
            _schreib(self.pfad, F.schreibe(self.datei))
        except OSError as e:
            messagebox.showerror("Menu editor", str(e))
            return

        self.mtime_menu = self.pfad.stat().st_mtime
        self.geaendert = False
        self._sagen("saved" + (" - %d warning(s)" % len(warnungen) if warnungen else ""))
        if warnungen:
            messagebox.showwarning("Menu editor", "\n".join(warnungen))

    # -- Check --------------------------------------------------------------

    def pruefen(self):
        """What the load path would reject (fehler), and what it does silently (warnungen)."""
        fehler = []
        warnungen = []
        g = self.tab.grenzen
        eigene = 0

        for k in self.datei.kaesten():
            if self.tab.gefunden:
                if k.name not in self.tab.alle_plaetze():
                    fehler.append("kasten '%s': no box of this name" % k.name)
                elif k.name not in self.tab.deklarierbare():
                    fehler.append("kasten '%s': is only a target and is not described" % k.name)
            if k.zurueck and self.tab.gefunden and k.zurueck not in self.tab.wirkungen:
                fehler.append("kasten '%s': no action '%s'" % (k.name, k.zurueck))
            if k.nimmt_stil and k.nimmt_stil not in [s.name for s in self.datei.stile()]:
                fehler.append("kasten '%s': no style '%s'" % (k.name, k.nimmt_stil))
            if "ROWS" in g and len(k.zeilen) > g["ROWS"]:
                fehler.append("kasten '%s': %d rows, at most %d" % (k.name, len(k.zeilen), g["ROWS"]))

            for z in k.zeilen:
                wo = "%s/%s" % (k.name, z.name)
                if z.text is None:
                    fehler.append("%s: zeile without text" % wo)
                if z.bedingung and self.tab.gefunden and z.bedingung not in self.tab.bedingungen:
                    fehler.append("%s: no condition '%s'" % (wo, z.bedingung))
                if z.wirkung and self.tab.gefunden and z.wirkung not in self.tab.wirkungen:
                    fehler.append("%s: no action '%s'" % (wo, z.wirkung))
                if z.weiter and self.tab.gefunden and z.weiter not in self.tab.alle_plaetze():
                    fehler.append("%s: no box '%s'" % (wo, z.weiter))
                for t in (z.text, z.text_gesperrt):
                    if t is None or t.art != F.EIGEN:
                        continue
                    eigene += 1
                    if '"' in t.wert:
                        fehler.append('%s: a text must not contain " - the tokenizer knows no escape' % wo)
                    if "#" in t.wert.split(" ")[0][:1]:
                        warnungen.append("%s: text starts with # - when in doubt it quotes itself" % wo)
                    grenze = g.get("STRING_LEN", 24) - 1
                    if len(t.wert) > grenze:
                        warnungen.append(
                            "%s: text '%s' is %d characters - the load path cuts it off at %d"
                            % (wo, t.wert, len(t.wert), grenze)
                        )

        for s in self.datei.stile():
            for name in s.felder:
                if self.tab.gefunden and name not in self.tab.stilfelder:
                    fehler.append("stil '%s': not a style key '%s'" % (s.name, name))

        if "STRINGS" in g and eigene > g["STRINGS"]:
            fehler.append("%d own strings, at most %d" % (eigene, g["STRINGS"]))
        if "POOL" in g and len(self.datei.stile()) > g["POOL"]:
            fehler.append("%d styles, at most %d" % (len(self.datei.stile()), g["POOL"]))

        return fehler, warnungen

    def _pruefen_zeigen(self) -> None:
        fehler, warnungen = self.pruefen()
        if not fehler and not warnungen:
            messagebox.showinfo("Menu editor", "No complaints.")
            return
        text = ""
        if fehler:
            text += "The load path would reject:\n" + "\n".join(fehler) + "\n\n"
        if warnungen:
            text += "The load path accepts it silently:\n" + "\n".join(warnungen)
        messagebox.showwarning("Menu editor", text)

    # -- Tree ---------------------------------------------------------------

    def _baum_fuellen(self, waehle: str = None) -> None:
        offen = {i for i in self.baum.get_children("") if self.baum.item(i, "open")}
        self.baum.delete(*self.baum.get_children(""))

        for bi, b in enumerate(self.datei.bloecke):
            kennung = "b%d" % bi
            marke = ("Box     " if b.art == "kasten" else "Style   ") + b.name
            self.baum.insert("", "end", kennung, text=marke, open=(kennung in offen or not offen))
            if b.art == "kasten":
                for zi, z in enumerate(b.zeilen):
                    self.baum.insert(kennung, "end", "b%d.z%d" % (bi, zi), text="  " + z.name)

        if waehle and self.baum.exists(waehle):
            self.baum.selection_set(waehle)
            self.baum.see(waehle)
        elif self.baum.get_children(""):
            self.baum.selection_set(self.baum.get_children("")[0])

        frei = [n for n in self.tab.deklarierbare() if self.datei.kasten(n) is None]
        self.b_kasten.state(["!disabled"] if (frei or not self.tab.gefunden) else ["disabled"])

    def _gewaehlt(self):
        """(block, row or None, ID) of the selection."""
        sel = self.baum.selection()
        if not sel:
            return None, None, None
        kennung = sel[0]
        teile = kennung.split(".")
        bi = int(teile[0][1:])
        if bi >= len(self.datei.bloecke):
            return None, None, None
        block = self.datei.bloecke[bi]
        if len(teile) == 1:
            return block, None, kennung
        zi = int(teile[1][1:])
        if zi >= len(block.zeilen):
            return block, None, kennung
        return block, block.zeilen[zi], kennung

    def _auswahl(self) -> None:
        block, zeile, _ = self._gewaehlt()
        for w in self.formular.winfo_children():
            w.destroy()
        self._halten = []
        if block is None:
            return
        if zeile is not None:
            self.formular.configure(text="Row  %s / %s" % (block.name, zeile.name))
            self._form_zeile(zeile)
        elif block.art == "kasten":
            self.formular.configure(text="Box  " + block.name)
            self._form_kasten(block)
        else:
            self.formular.configure(text="Style  " + block.name)
            self._form_stil(block)
        self._tabelle_fuellen()
        self._skizze()

    # -- Forms --------------------------------------------------------------

    def _zeile_im_form(self, eltern, r, beschriftung):
        ttk.Label(eltern, text=beschriftung).grid(row=r, column=0, sticky="w", pady=1)
        rahmen = ttk.Frame(eltern)
        rahmen.grid(row=r, column=1, sticky="ew", pady=1)
        return rahmen

    def _var(self, wert, rueck):
        v = tk.StringVar(value="" if wert is None else str(wert))
        self._halten.append(v)

        def geaendert(*_):
            if self._fuellt:
                return
            rueck(v.get())
            self._markieren()

        v.trace_add("write", geaendert)
        return v

    def _mindestbreite(self, k, text: str, grenze: int) -> None:
        """The value from the field. Empty means "not named", not zero."""
        t = text.strip()
        if t == "":
            k.mindestbreite = None
            return
        try:
            wert = int(t, 0)
        except ValueError:
            self._sagen("breite: '%s' is not a number" % t)
            return
        if wert < 0 or wert > grenze:
            self._sagen("breite: %d lies outside 0..%d - the load path would reject the file"
                        % (wert, grenze))
            return
        k.mindestbreite = wert

    def _markieren(self) -> None:
        self.geaendert = True
        self._sagen("changed - not saved yet")
        self._baum_beschriften()
        self._tabelle_fuellen()
        self._skizze()

    def _baum_beschriften(self) -> None:
        for bi, b in enumerate(self.datei.bloecke):
            if self.baum.exists("b%d" % bi):
                self.baum.item(
                    "b%d" % bi, text=("Box     " if b.art == "kasten" else "Style   ") + b.name
                )
            if b.art != "kasten":
                continue
            for zi, z in enumerate(b.zeilen):
                if self.baum.exists("b%d.z%d" % (bi, zi)):
                    self.baum.item("b%d.z%d" % (bi, zi), text="  " + z.name)

    def _form_kasten(self, k: F.Kasten) -> None:
        self._fuellt = True
        f = self.formular
        f.columnconfigure(1, weight=1)
        r = 0

        namen = sorted(set(self.tab.deklarierbare() + [k.name])) if self.tab.gefunden else [k.name]
        z = self._zeile_im_form(f, r, "Name")
        v = self._var(k.name, lambda s: self._kasten_namen(k, s))
        ttk.Combobox(z, textvariable=v, values=namen, width=24).pack(side="left")
        r += 1

        for feld, text in (("x", "x"), ("y", "y")):
            z = self._zeile_im_form(f, r, text)
            gesetzt = tk.BooleanVar(value=getattr(k, feld) is not None)
            self._halten.append(gesetzt)
            vv = self._var(getattr(k, feld), lambda s, fd=feld: self._zahl(k, fd, s))

            def um(fd=feld, gv=None, ev=None):
                if self._fuellt:
                    return
                if gv.get():
                    if getattr(k, fd) is None:
                        setattr(k, fd, 0)
                        self._fuellt = True
                        ev.set("0")
                        self._fuellt = False
                else:
                    setattr(k, fd, None)
                    self._fuellt = True
                    ev.set("")
                    self._fuellt = False
                self._markieren()

            cb = ttk.Checkbutton(z, text="declared", variable=gesetzt)
            cb.configure(command=lambda fd=feld, gv=gesetzt, ev=vv: um(fd, gv, ev))
            cb.pack(side="left")
            ttk.Entry(z, textvariable=vv, width=8).pack(side="left", padx=(6, 0))
            r += 1

        z = self._zeile_im_form(f, r, "bezug")
        v = self._var(k.bezug or NICHTS, lambda s: self._setzen(k, "bezug", None if s == NICHTS else s))
        ttk.Combobox(z, textvariable=v, values=[NICHTS] + list(F.BEZUG_WERTE), state="readonly", width=14).pack(
            side="left"
        )
        ttk.Label(z, text="if not given: corner", foreground="#888").pack(side="left", padx=6)
        r += 1

        z = self._zeile_im_form(f, r, "layout")
        v = self._var(k.layout or NICHTS, lambda s: self._setzen(k, "layout", None if s == NICHTS else s))
        ttk.Combobox(z, textvariable=v, values=[NICHTS] + list(F.LAYOUT_WERTE), state="readonly", width=14).pack(
            side="left"
        )
        r += 1

        z = self._zeile_im_form(f, r, "schrift")
        v = self._var(k.schrift or NICHTS, lambda s: self._setzen(k, "schrift", None if s == NICHTS else s))
        ttk.Combobox(z, textvariable=v, values=[NICHTS] + list(F.SCHRIFT_WERTE), state="readonly", width=14).pack(
            side="left"
        )
        ttk.Label(z, text="USE_SMALL_FONT - title and rows", foreground="#888").pack(side="left", padx=6)
        r += 1

        z = self._zeile_im_form(f, r, "ausrichtung")
        v = self._var(
            k.ausrichtung or NICHTS, lambda s: self._setzen(k, "ausrichtung", None if s == NICHTS else s)
        )
        ttk.Combobox(z, textvariable=v, values=[NICHTS] + list(F.AUSRICHTUNG_WERTE), state="readonly",
                     width=14).pack(side="left")
        ttk.Label(z, text="CENTER_MENU_TEXT - title and rows", foreground="#888").pack(side="left", padx=6)
        r += 1

        z = self._zeile_im_form(f, r, "anker")
        v = self._var(k.anker or NICHTS, lambda s: self._setzen(k, "anker", None if s == NICHTS else s))
        ttk.Combobox(z, textvariable=v, values=[NICHTS] + list(F.ANKER_WERTE), state="readonly",
                     width=14).pack(side="left")
        ttk.Label(z, text="if not given: the mapper guesses by the thirds rule",
                  foreground="#888").pack(side="left", padx=6)
        r += 1

        z = self._zeile_im_form(f, r, "breite")
        grenze = self.tab.grenzen.get("MIN_WIDTH_MAX", F.MINDESTBREITE_MAX)
        gesetzt = tk.BooleanVar(value=k.mindestbreite is not None)
        self._halten.append(gesetzt)
        vb = self._var(k.mindestbreite, lambda t: self._mindestbreite(k, t, grenze))

        def um_breite(gv=gesetzt, ev=None):
            if self._fuellt:
                return
            if gv.get():
                if k.mindestbreite is None:
                    k.mindestbreite = 12
                    self._fuellt = True
                    ev.set("12")
                    self._fuellt = False
            else:
                k.mindestbreite = None
                self._fuellt = True
                ev.set("")
                self._fuellt = False
            self._markieren()

        cb = ttk.Checkbutton(z, text="mindestens", variable=gesetzt)
        cb.configure(command=lambda gv=gesetzt, ev=vb: um_breite(gv, ev))
        cb.pack(side="left")
        ttk.Entry(z, textvariable=vb, width=6).pack(side="left", padx=(6, 0))
        ttk.Label(z, text="characters, 0..%d  -  0 takes the built-in twelve from the main menu"
                          % grenze, foreground="#888").pack(side="left", padx=6)
        r += 1

        z = self._zeile_im_form(f, r, "titel")
        self._text_widget(z, k.titel if isinstance(k.titel, F.Text) else None,
                          lambda t: self._setzen(k, "titel", t),
                          zusatz=("keiner", k.titel == "keiner",
                                  lambda an: self._setzen(k, "titel", "keiner" if an else None)))
        r += 1

        z = self._zeile_im_form(f, r, "nimmt-stil")
        stile = [NICHTS] + [s.name for s in self.datei.stile()]
        v = self._var(k.nimmt_stil or NICHTS, lambda s: self._setzen(k, "nimmt_stil", None if s == NICHTS else s))
        ttk.Combobox(z, textvariable=v, values=stile, state="readonly", width=20).pack(side="left")
        r += 1

        z = self._zeile_im_form(f, r, "zurueck")
        v = self._var(k.zurueck or NICHTS, lambda s: self._setzen(k, "zurueck", None if s == NICHTS else s))
        ttk.Combobox(z, textvariable=v, values=[NICHTS] + self.tab.wirkungen, width=24).pack(side="left")
        ttk.Label(z, text="action of the way back", foreground="#888").pack(side="left", padx=6)
        r += 1

        ttk.Separator(f, orient="horizontal").grid(row=r, column=0, columnspan=2, sticky="ew", pady=6)
        r += 1
        ttk.Label(
            f,
            justify="left",
            foreground="#7a6",
            text=(
                "The anchor can now be declared. If not given, the mapper guesses\n"
                "  by the thirds rule - what it guessed is shown on the right.\n"
                "  At 4:3 the mapping is the identity, so an anchor changes\n"
                "  nothing there; it only becomes visible at 16:9 and 43:18.\n"
                "\n"
                "breite is a minimum width in CHARACTERS, not a fixed width:\n"
                "  the box may grow when a row needs more. In\n"
                "  a cascade the minimum width of the ROOT counts - a child\n"
                "  can only raise it, because the whole chain is drawn with\n"
                "  ONE width."
            ),
        ).grid(row=r, column=0, columnspan=2, sticky="w")
        self._fuellt = False

    def _form_zeile(self, z: F.Zeile) -> None:
        self._fuellt = True
        f = self.formular
        f.columnconfigure(1, weight=1)
        r = 0

        w = self._zeile_im_form(f, r, "Name")
        v = self._var(z.name, lambda s: self._setzen(z, "name", s))
        ttk.Entry(w, textvariable=v, width=26).pack(side="left")
        ttk.Label(w, text="label only - the load path does not store it", foreground="#888").pack(
            side="left", padx=6
        )
        r += 1

        w = self._zeile_im_form(f, r, "text")
        self._text_widget(w, z.text, lambda t: self._setzen(z, "text", t), pflicht=True)
        r += 1

        w = self._zeile_im_form(f, r, "text-gesperrt")
        self._text_widget(w, z.text_gesperrt, lambda t: self._setzen(z, "text_gesperrt", t))
        r += 1

        w = self._zeile_im_form(f, r, "Lock")
        arten = ["(none)", "gesperrt-wenn", "gesperrt-wenn-nicht"]
        jetzt = "(none)" if z.bedingung is None else (arten[2] if z.negiert else arten[1])
        va = self._var(jetzt, lambda s: self._sperrart(z, s))
        ttk.Combobox(w, textvariable=va, values=arten, state="readonly", width=20).pack(side="left")
        vb = self._var(z.bedingung or "", lambda s: self._setzen(z, "bedingung", s or None))
        ttk.Combobox(w, textvariable=vb, values=[""] + self.tab.bedingungen, width=22).pack(side="left", padx=6)
        r += 1

        w = self._zeile_im_form(f, r, "wirkung")
        v = self._var(z.wirkung or NICHTS, lambda s: self._setzen(z, "wirkung", None if s == NICHTS else s))
        ttk.Combobox(w, textvariable=v, values=[NICHTS] + self.tab.wirkungen, width=24).pack(side="left")
        ttk.Label(w, text="changes the state, does not navigate", foreground="#888").pack(side="left", padx=6)
        r += 1

        w = self._zeile_im_form(f, r, "weiter")
        v = self._var(z.weiter or NICHTS, lambda s: self._setzen(z, "weiter", None if s == NICHTS else s))
        ttk.Combobox(w, textvariable=v, values=[NICHTS] + self.tab.alle_plaetze(), width=24).pack(side="left")
        ttk.Label(w, text="the target - here and not in the code", foreground="#888").pack(side="left", padx=6)
        r += 1

        w = self._zeile_im_form(f, r, "oeffnen")
        v = self._var(z.oeffnen or NICHTS, lambda s: self._setzen(z, "oeffnen", None if s == NICHTS else s))
        ttk.Combobox(w, textvariable=v, values=[NICHTS] + list(F.OEFFNEN_WERTE), state="readonly", width=16).pack(
            side="left"
        )
        ttk.Label(w, text="if not given: gestapelt", foreground="#888").pack(side="left", padx=6)
        self._fuellt = False

    def _form_stil(self, s: F.Stil) -> None:
        self._fuellt = True
        f = self.formular
        f.columnconfigure(0, weight=1)
        f.rowconfigure(1, weight=1)

        kopf = ttk.Frame(f)
        kopf.grid(row=0, column=0, sticky="ew")
        ttk.Label(kopf, text="Name").pack(side="left")
        v = self._var(s.name, lambda t: self._stil_namen(s, t))
        ttk.Entry(kopf, textvariable=v, width=24).pack(side="left", padx=6)
        ttk.Label(
            kopf, text="what is not named comes from retail", foreground="#888"
        ).pack(side="left", padx=6)

        aussen = ttk.Frame(f)
        aussen.grid(row=1, column=0, sticky="nsew", pady=(6, 0))
        aussen.rowconfigure(0, weight=1)
        aussen.columnconfigure(0, weight=1)
        dose = tk.Canvas(aussen, highlightthickness=0, height=200)
        dose.grid(row=0, column=0, sticky="nsew")
        sb = ttk.Scrollbar(aussen, orient="vertical", command=dose.yview)
        sb.grid(row=0, column=1, sticky="ns")
        dose.configure(yscrollcommand=sb.set)
        innen = ttk.Frame(dose)
        dose.create_window((0, 0), window=innen, anchor="nw")
        innen.bind("<Configure>", lambda e: dose.configure(scrollregion=dose.bbox("all")))

        felder = self.tab.stilfelder or sorted(s.felder)
        for i, name in enumerate(felder):
            gesetzt = tk.BooleanVar(value=name in s.felder)
            self._halten.append(gesetzt)
            ev = self._var(s.felder.get(name), lambda t, nm=name: self._stilfeld(s, nm, t))

            def um(nm=name, gv=gesetzt, e=ev):
                if self._fuellt:
                    return
                if gv.get():
                    s.felder.setdefault(nm, 0)
                    self._fuellt = True
                    e.set(str(s.felder[nm]))
                    self._fuellt = False
                else:
                    s.felder.pop(nm, None)
                    self._fuellt = True
                    e.set("")
                    self._fuellt = False
                self._markieren()

            cb = ttk.Checkbutton(innen, variable=gesetzt)
            cb.configure(command=um)
            cb.grid(row=i, column=0)
            ttk.Label(innen, text=name, width=26, anchor="w").grid(row=i, column=1, sticky="w")
            ttk.Entry(innen, textvariable=ev, width=8).grid(row=i, column=2, padx=4)
        self._fuellt = False

    def _text_widget(self, eltern, wert, rueck, pflicht=False, zusatz=None):
        """A text value: lng number or own string, switchable."""
        arten = (["lng", "eigen"] if pflicht else [NICHTS, "lng", "eigen"])
        jetzt = NICHTS if wert is None else wert.art
        if pflicht and wert is None:
            jetzt = "eigen"

        va = tk.StringVar(value=jetzt)
        self._halten.append(va)
        vw = tk.StringVar(value="" if wert is None else (("0x%x" % wert.wert) if wert.art == F.LNG else wert.wert))

        def uebernehmen(*_):
            if self._fuellt:
                return
            art = va.get()
            if art == NICHTS:
                rueck(None)
            elif art == "lng":
                zahl = F.lies_zahl(vw.get().strip()) if vw.get().strip() else None
                rueck(F.Text(F.LNG, zahl) if zahl is not None else None)
            else:
                rueck(F.Text(F.EIGEN, vw.get()))
            self._markieren()

        self._halten.append(vw)
        va.trace_add("write", uebernehmen)
        vw.trace_add("write", uebernehmen)
        ttk.Combobox(eltern, textvariable=va, values=arten, state="readonly", width=8).pack(side="left")
        ttk.Entry(eltern, textvariable=vw, width=26).pack(side="left", padx=6)

        if zusatz is not None:
            beschriftung, an, rueck2 = zusatz
            vb = tk.BooleanVar(value=an)
            self._halten.append(vb)

            def um():
                if self._fuellt:
                    return
                rueck2(vb.get())
                self._markieren()
                self._auswahl()

            cb = ttk.Checkbutton(eltern, text=beschriftung, variable=vb)
            cb.configure(command=um)
            cb.pack(side="left", padx=6)

    # -- Changes ------------------------------------------------------------

    def _setzen(self, obj, feld, wert) -> None:
        setattr(obj, feld, wert)

    def _zahl(self, k, feld, s) -> None:
        wert = F.lies_zahl(s.strip()) if s.strip() else None
        if wert is not None or not s.strip():
            setattr(k, feld, wert)

    def _sperrart(self, z, s) -> None:
        if s == "(none)":
            z.bedingung = None
            z.negiert = False
        else:
            z.negiert = s == "gesperrt-wenn-nicht"
            if z.bedingung is None and self.tab.bedingungen:
                z.bedingung = self.tab.bedingungen[0]

    def _kasten_namen(self, k, s) -> None:
        k.name = s

    def _stil_namen(self, s, t) -> None:
        alt = s.name
        s.name = t
        for k in self.datei.kaesten():
            if k.nimmt_stil == alt:
                k.nimmt_stil = t

    def _stilfeld(self, s, name, t) -> None:
        if name not in s.felder:
            return
        wert = F.lies_zahl(t.strip())
        if wert is not None:
            s.felder[name] = wert

    def _zeile_neu(self) -> None:
        block, zeile, kennung = self._gewaehlt()
        if block is None or block.art != "kasten":
            self._sagen("no box selected")
            return
        grenze = self.tab.grenzen.get("ROWS")
        if grenze and len(block.zeilen) >= grenze:
            self._sagen("at most %d rows per box" % grenze)
            return
        neu = F.Zeile(name="new", text=F.Text(F.EIGEN, "NEW"), vorspann=[])
        wo = block.zeilen.index(zeile) + 1 if zeile is not None else len(block.zeilen)
        block.zeilen.insert(wo, neu)
        bi = self.datei.bloecke.index(block)
        self.geaendert = True
        self._baum_fuellen("b%d.z%d" % (bi, wo))
        self._auswahl()

    def _zeile_weg(self) -> None:
        block, zeile, _ = self._gewaehlt()
        if zeile is None:
            self._sagen("no row selected")
            return
        i = block.zeilen.index(zeile)
        # The preamble of the deleted row goes to the next one - otherwise
        # deleting a row also removes the comment above it, which often
        # does not belong to it but to the group.
        if zeile.vorspann and i + 1 < len(block.zeilen):
            block.zeilen[i + 1].vorspann = zeile.vorspann + block.zeilen[i + 1].vorspann
        block.zeilen.pop(i)
        bi = self.datei.bloecke.index(block)
        self.geaendert = True
        self._baum_fuellen("b%d" % bi)
        self._auswahl()

    def _zeile_schieben(self, richtung: int) -> None:
        block, zeile, _ = self._gewaehlt()
        if zeile is None:
            self._sagen("no row selected")
            return
        i = block.zeilen.index(zeile)
        j = i + richtung
        if j < 0 or j >= len(block.zeilen):
            return
        # The preambles stay in their place, not with their row: a
        # comment as a rule describes the position in the list.
        va, vb = block.zeilen[i].vorspann, block.zeilen[j].vorspann
        block.zeilen[i], block.zeilen[j] = block.zeilen[j], block.zeilen[i]
        block.zeilen[i].vorspann, block.zeilen[j].vorspann = va, vb
        bi = self.datei.bloecke.index(block)
        self.geaendert = True
        self._baum_fuellen("b%d.z%d" % (bi, j))
        self._auswahl()

    def _kasten_neu(self) -> None:
        frei = [n for n in self.tab.deklarierbare() if self.datei.kasten(n) is None]
        if self.tab.gefunden and not frei:
            messagebox.showinfo(
                "Menu editor",
                "No free slot. A box needs an entry in s_slots\n"
                "(game/native_menudecl.c) with declared=1 - the file cannot\n"
                "invent one, because the slot has to exist in the game.",
            )
            return
        name = frei[0] if frei else "new"
        self.datei.bloecke.append(F.Kasten(name=name, layout="spalte", vorspann=[""]))
        self.geaendert = True
        self._baum_fuellen("b%d" % (len(self.datei.bloecke) - 1))
        self._auswahl()

    def _stil_neu(self) -> None:
        grenze = self.tab.grenzen.get("POOL")
        if grenze and len(self.datei.stile()) >= grenze:
            self._sagen("at most %d styles" % grenze)
            return
        n = 1
        while any(s.name == "stil%d" % n for s in self.datei.stile()):
            n += 1
        self.datei.bloecke.append(F.Stil(name="stil%d" % n, vorspann=[""]))
        self.geaendert = True
        self._baum_fuellen("b%d" % (len(self.datei.bloecke) - 1))
        self._auswahl()

    def _block_weg(self) -> None:
        block, zeile, _ = self._gewaehlt()
        if block is None or zeile is not None:
            self._sagen("no block selected")
            return
        if not messagebox.askokcancel("Menu editor", "Delete block '%s'?" % block.name):
            return
        self.datei.bloecke.remove(block)
        self.geaendert = True
        self._baum_fuellen()
        self._auswahl()

    # -- Measured -----------------------------------------------------------

    def _ergebnis_pfad(self):
        if self.pfad is None:
            return None
        return self.pfad.with_suffix(".ergebnis")

    def _bild_pfad(self):
        if self.pfad is None:
            return None
        return self.pfad.with_suffix(".bild.ppm")

    def _bild_lesen(self, zwingen=False) -> bool:
        """The game picture next to the result file. PPM, because Tkinter reads it without
        any additional dependency."""
        p = self._bild_pfad()

        if p is None or not p.is_file():
            if self.bild is not None:
                self.bild = None
                return True
            return False

        m = p.stat().st_mtime
        if not zwingen and self.mtime_bild is not None and m <= self.mtime_bild:
            return False

        self.mtime_bild = m
        self._schnipsel = {}
        try:
            neu = tk.PhotoImage(file=str(p))
        except tk.TclError as e:
            self.bild = None
            self._sagen("picture not readable: %s" % e)
            return True

        self.bild = neu
        self.bild_w = neu.width()
        self.bild_h = neu.height()
        return True

    def _ergebnis_lesen(self, zwingen=False) -> bool:
        p = self._ergebnis_pfad()
        if p is None or not p.is_file():
            return False
        m = p.stat().st_mtime
        if not zwingen and self.mtime_erg is not None and m <= self.mtime_erg:
            return False
        self.mtime_erg = m
        try:
            self.messung = ERG.lade(p.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            return False
        return True

    def _tabelle_fuellen(self) -> None:
        self.tabelle.delete(*self.tabelle.get_children(""))
        block, _zeile, _ = self._gewaehlt()

        if self.messung.vblank:
            veraltet = ""
            p = self._ergebnis_pfad()
            if p is not None and self.pfad.is_file() and p.is_file():
                if p.stat().st_mtime < self.pfad.stat().st_mtime - 0.001:
                    veraltet = "  -  older than the menu file"
            self.l_messung.configure(
                text="VBlank %d, %s, reference space %dx%d, canvas %d%s"
                % (
                    self.messung.vblank,
                    self.messung.seiten,
                    self.messung.bezug_w,
                    self.messung.bezug_h,
                    self.messung.leinwand,
                    veraltet,
                ),
                foreground="#c80" if veraltet else "#888",
            )
        else:
            self.l_messung.configure(text="no result file - the game writes it only with menu reload", foreground="#888")

        if block is None or block.art != "kasten":
            return

        e = self.messung.nach_name(block.name)
        g = self._gerechnet(block)

        def zeile(name, gewollt, gerechnet, gemessen, tag=""):
            self.tabelle.insert("", "end", text=name, values=(gewollt, gerechnet, gemessen),
                                tags=(tag,) if tag else ())

        def mg(wert):
            return "-" if e is None else str(wert)

        def gr(wert):
            return "-" if g is None else str(wert)

        zeile("x", "-" if block.x is None else str(block.x), gr(g.x0 if g else 0), mg(e.vor[0] if e else 0))
        zeile("y", "-" if block.y is None else str(block.y), gr(g.y0 if g else 0), mg(e.vor[1] if e else 0))
        zeile("bezug", block.bezug or "ecke", "-", "-")
        zeile("Width", "-", gr(g.breite if g else 0), mg(e.breite if e else 0))
        zeile("Height", "-", gr(g.hoehe if g else 0), mg(e.hoehe if e else 0))
        zeile("Rectangle", "-",
              "-" if g is None else "%d,%d..%d,%d" % (g.x0, g.y0, g.x1, g.y1),
              "-" if e is None else "%d,%d..%d,%d" % e.vor)
        zeile("Anchor", block.anker or "guessed", "-", "-" if e is None else e.anker)
        zeile("Shift", "-", "-", "-" if e is None else "%+d" % e.versatz)

        # IF BOTH ARE AVAILABLE AND DIFFER, IT IS SAID. The recomputation is
        # a port; where it is off, that is a finding and not an
        # inaccuracy one may push aside.
        if g is not None and e is not None:
            d = (g.x0 - e.vor[0], g.y0 - e.vor[1], g.x1 - e.vor[2], g.y1 - e.vor[3])
            if any(d):
                zeile("Deviation", "-", "%+d,%+d..%+d,%+d" % d, "computed against measured",
                      "abweichung")
            else:
                zeile("Deviation", "-", "none", "spot on")

        for v in (g.vorbehalte if g is not None else []):
            self.tabelle.insert("", "end", text="Caveat", values=("", v, ""), tags=("vorbehalt",))

        if g is None and self.regeln.hinweis:
            self.tabelle.insert("", "end", text="Caveat", values=("", self.regeln.hinweis, ""),
                                tags=("vorbehalt",))

    # -- Sketch -------------------------------------------------------------

    def _raster_setzen(self) -> None:
        wert = self.v_raster.get()
        self._raster = 0 if wert == "off" else int(wert)
        self._skizze()

    def _hilfslinien_setzen(self) -> None:
        self._hilfslinien = bool(self.v_hilfslinien.get())
        self._skizze()

    def _fangen(self, wert: int) -> int:
        """Snap to the grid. Unchanged without a grid.

        What is snapped is the DECLARATION, not the rectangle: x and y are what is in
        the file, and a rectangle whose edge sits on a round number
        would be a different promise than a round position.
        """
        if self._raster <= 0:
            return wert

        # Python divides ROUNDING DOWN, also for negative numbers - that is why
        # (wert + half) // raster rounds to the nearest grid line in both directions.
        # Sign handling would not be needed here but wrong: it
        # pushed -5 at grid 8 to -16 instead of -8.
        return ((wert + (self._raster // 2)) // self._raster) * self._raster

    def _fangen_kasten(self, kasten, x: int, y: int):
        """x and y such that the RECTANGLE sits on the grid.

        NOT THE DECLARATION NUMBER. Between x and the edge of the drawn
        rectangle lie half the box, the frame offset and the shadow -
        a round number in the file gives no round edge in the picture, and
        alignment is against the picture.

        WHICH FEATURE SNAPS IS DECIDED BY "bezug". On the axis on which the
        box is centred, the CENTRE snaps - exactly that is what "bezug
        mitte" means, and the box then sits symmetrically around the grid point. On
        the other axis the leading EDGE snaps.

        Snapping happens on the CANVAS, i.e. in the space one sees. The
        mapping is a shift, so the same difference also applies
        to x in the file.
        """
        if self._raster <= 0:
            return x, y

        mitte_x = kasten.bezug in ("mitte", "mitte-x")
        mitte_y = kasten.bezug in ("mitte", "mitte-y")

        def merkmale(px, py):
            merk = (kasten.x, kasten.y)
            kasten.x, kasten.y = px, py
            g = self._gerechnet(kasten)
            ab = self._abbilden(g, kasten) if g is not None else None
            kasten.x, kasten.y = merk
            if g is None:
                return None, None
            return (((ab[0] + ab[1]) // 2) if mitte_x else ab[0],
                    ((g.y0 + g.y1) // 2) if mitte_y else g.y0)

        # WHY A LOOP AND NOT A FORMULA.
        #
        # The position of the feature depends on the anchor, and the
        # thirds rule guesses that from the position - so a shift can make it
        # flip, and then the feature jumps by the difference of the
        # two shifts. A single pass lands beside it. Four
        # passes are enough; if it oscillates, the grid point lies in the jump
        # and cannot be reached from any x.
        #
        # With a declared "anker" there is no jump: then the
        # shift is fixed, and the first pass hits.
        gesehen = set()

        for _runde in range(4):
            mx, my = merkmale(x, y)

            if mx is None:
                return self._fangen(x), self._fangen(y)

            if (mx % self._raster) == 0 and (my % self._raster) == 0:
                return x, y

            neu = (x + (self._fangen(mx) - mx), y + (self._fangen(my) - my))

            if neu in gesehen:
                # The grid point cannot be reached from any x: it lies in
                # the jump the anchor change tears open. Instead of oscillating,
                # the declaration number is snapped and the case is reported.
                self._sagen("grid point lies in the jump of the guessed anchor - "
                            "with 'anker' in the form the jump disappears")
                return self._fangen(x), self._fangen(y)

            gesehen.add(neu)
            x, y = neu

        return x, y

    def _hinaus(self, kasten, g, lh: int) -> str:
        """How far a box sticks out of the CANVAS. Empty text: not at all.

        AGAINST THE CANVAS AND NOT AGAINST THE REFERENCE SPACE. The reference space is
        512 columns wide, because that is where declaring happens; drawing, however, goes into
        the canvas, and that is 918 columns wide at 43:18. A box that
        stands at column 600 is perfectly fine there.

        It used to be measured against 512. The mistake went unnoticed for a long time, because
        it gives the same number for right-anchored boxes: the mapping
        shifts them by exactly 918 minus 512, so "x1 over 512" in the
        reference space is the same as "mapped over 918". For centre- or left-
        anchored boxes it is not true - and there it reported boxes as
        outside that stood in the middle of the picture.
        """
        if g is None:
            return ""

        x0, x1, _a = self._abbilden(g, kasten)
        lw = self._leinwand_breite()

        teile = []
        if x0 < 0:
            teile.append("left %d" % -x0)
        if x1 > lw:
            teile.append("right %d" % (x1 - lw))
        if g.y0 < 0:
            teile.append("top %d" % -g.y0)
        if g.y1 > lh:
            teile.append("bottom %d" % (g.y1 - lh))

        return ", ".join(teile)

    def _klemmen(self, kasten, x: int, y: int):
        """x and y such that the mapped rectangle stays on the canvas.

        THE POINTER IS HELD, not only labelled: when aligning one wants
        to be able to move to the edge without sliding past it. What
        the file allows does not change - whoever wants to go further enters
        the number in the form.
        """
        merk_x, merk_y = kasten.x, kasten.y
        kasten.x, kasten.y = x, y
        g = self._gerechnet(kasten)
        kasten.x, kasten.y = merk_x, merk_y

        if g is None:
            return x, y

        lw = self._leinwand_breite()
        lh = self._leinwand_hoehe()
        x0, x1, _a = self._abbilden(g, kasten)

        # Does the box fit in at all? If not, the left edge is
        # held - otherwise the clamping would pull it back and forth.
        if (x1 - x0) <= lw:
            if x0 < 0:
                x += -x0
            elif x1 > lw:
                x -= (x1 - lw)
        elif x0 < 0:
            x += -x0

        if (g.y1 - g.y0) <= lh:
            if g.y0 < 0:
                y += -g.y0
            elif g.y1 > lh:
                y -= (g.y1 - lh)
        elif g.y0 < 0:
            y += -g.y0

        return x, y

    def _bild_gilt(self) -> bool:
        """Does the picture still show the state that is in the file?

        The same question as for the measurement, and it was the reason why picture
        and rectangle drifted apart: after a drag the box stands where it
        is meant to be, and the picture still shows where it was. Both are right, and
        without a sentence about it, it looks like a computation error.
        """
        if self.geaendert:
            return False
        p = self._bild_pfad()
        if p is None or not p.is_file() or self.pfad is None or not self.pfad.is_file():
            return False
        return p.stat().st_mtime >= (self.pfad.stat().st_mtime - 0.001)

    def _messung_gilt(self) -> bool:
        if self.geaendert:
            return False
        p = self._ergebnis_pfad()
        if p is None or not p.is_file() or self.pfad is None or not self.pfad.is_file():
            return False
        return p.stat().st_mtime >= (self.pfad.stat().st_mtime - 0.001)

    def _leinwand_breite(self) -> int:
        """The number of columns the sketch draws in.

        THE CHOICE. The game picture IS the canvas - 512, 682 or 918 columns.
        Squeezing it into the 512 reference space would mean shifting it against every element
        that is not anchored at the centre, and the anchor is exactly
        what one wants to see here. So the sketch shows the canvas, and
        the computed rectangles are mapped there with CTR_UI_MapX.

        That costs nothing when dragging: left, centre and right are pure
        shifts, so a drag is a one-to-one change of x.
        """
        # THE MEASUREMENT TAKES PRECEDENCE OVER THE PICTURE. Both say the same as long as
        # they come from the same run - but a picture from a 43:18 run
        # next to a result file from a 4:3 run would draw the sketch in
        # a space in which the measured rectangles do not stand.
        # If the two contradict each other, _bild_passt says so.
        if self.messung.leinwand > 0:
            return self.messung.leinwand
        if self.bild is not None and self.bild_w > 0:
            return self.bild_w
        return self.messung.bezug_w or 512

    def _leinwand_hoehe(self) -> int:
        if self.bild is not None and self.bild_h > 0:
            return self.bild_h
        return self.messung.bezug_h or 216

    def _bild_passt(self) -> bool:
        if self.bild is None or self.messung.leinwand <= 0:
            return True
        return self.bild_w == self.messung.leinwand

    def _bild_paar(self) -> bool:
        """Do picture and result file show the same moment?

        WHAT IS CHECKED HERE IS NOT WHETHER THE MEASUREMENT MATCHES THE
        DECLARATION. Exactly the case in which it does not - the file has been dragged,
        the game has not reloaded yet -, is the case the
        carrying-along is built for. The question is whether the measured rectangle says WHERE IN THE
        PICTURE the menu stands. For that the two files must come from the same
        reload.

        By design they do: the same line in native_menudecl.c
        sets both requests, one is fulfilled on submission, the
        other after showing the same frame. What can separate them is a
        failed write or a file copied by hand - hence the
        time check. Two seconds are generous for two writes
        of one frame, and tight compared to two runs.
        """
        if self.bild is None or not self.messung.liste:
            return False
        if not self._bild_passt():
            return False
        pb, pe = self._bild_pfad(), self._ergebnis_pfad()
        if pb is None or pe is None or not pb.is_file() or not pe.is_file():
            return False
        return abs(pb.stat().st_mtime - pe.stat().st_mtime) <= 2.0

    def _ausschnitt(self, r, zoom: int, unter: int):
        """The piece of picture that a measured rectangle covers.

        Returns (image, x0, y0, x1, y1): the piece and the rectangle of the
        canvas it comes from - not quite the measured one, see below.

        Cutting uses the Tcl command 'copy -from', i.e. the same
        C code that also draws the picture; Python touches no pixel.
        Enlarging and shrinking use the same integer factors as the
        background, otherwise the piece sits fractions off.

        THE GRID OF SHRINKING. subsample(n) takes the columns 0, n, 2n and so on
        from its source. The background is shrunk as a whole,
        so it takes the columns of the CANVAS that are divisible by n.
        A piece that starts at column 351 takes 351, 353, 355 -
        the other ones. Measured, that gave 230 of 460 samples deviating, and
        in the picture a seam offset by half a pixel.
        Cutting therefore starts at the next column divisible by n; the
        up to n-1 additional pixels at the edge are background and do not
        stand out.
        """
        if self.bild is None:
            return None

        x0 = max(0, min(int(r[0]), self.bild_w))
        y0 = max(0, min(int(r[1]), self.bild_h))
        x1 = max(x0, min(int(r[2]), self.bild_w))
        y1 = max(y0, min(int(r[3]), self.bild_h))
        if x1 <= x0 or y1 <= y0:
            return None

        if unter > 1:
            x0 -= x0 % unter
            y0 -= y0 % unter
            x1 = min(self.bild_w, x1 + (-x1) % unter)
            y1 = min(self.bild_h, y1 + (-y1) % unter)

        schluessel = (x0, y0, x1, y1, zoom, unter)
        fertig = self._schnipsel.get(schluessel)
        if fertig is not None:
            return fertig

        try:
            stueck = tk.PhotoImage(width=x1 - x0, height=y1 - y0)
            stueck.tk.call(stueck, "copy", self.bild,
                           "-from", x0, y0, x1, y1, "-to", 0, 0)
            if zoom > 1:
                stueck = stueck.zoom(zoom, zoom)
            if unter > 1:
                stueck = stueck.subsample(unter, unter)
        except tk.TclError:
            return None

        if len(self._schnipsel) > 32:
            self._schnipsel.clear()
        self._schnipsel[schluessel] = (stueck, x0, y0, x1, y1)
        return self._schnipsel[schluessel]

    def _abbilden(self, g, kasten):
        """A computed rectangle into the canvas. (x0, x1, anchor)."""
        bezug = self.messung.bezug_w or 512
        leinwand = self._leinwand_breite()
        anker = kasten.anker or MASS.anker_geraten(g.x0, g.x1, bezug)
        return (MASS.bilde_ab(g.x0, anker, leinwand, bezug),
                MASS.bilde_ab(g.x1, anker, leinwand, bezug),
                anker)

    def _skizze(self) -> None:
        """The game picture, and on it the rectangles with their rows.

        SOLID means measured, DASHED means recomputed.
        """
        c = self.leinwand
        c.delete("all")
        self._rechtecke = {}

        bezug = self.messung.bezug_w or 512
        lw = self._leinwand_breite()
        lh = self._leinwand_hoehe()
        breite = max(c.winfo_width(), 40)
        hoehe = max(c.winfo_height(), 40)

        # IS THE MEASUREMENT STILL VALID? As soon as the declaration is newer than the
        # result file, the measured rectangle describes a different state
        # than the rows in it. A frame from yesterday with today's content
        # is worse than a picture computed throughout - so the computed one
        # applies then, dashed, and that also says that the game
        # has not caught up yet.
        gilt = self._messung_gilt()

        gemalt = []
        for k in self.datei.kaesten():
            e = self.messung.nach_name(k.name) if gilt else None
            g = self._gerechnet(k)
            if e is not None:
                # after: the mapped rectangle, i.e. already in the canvas.
                r = (e.nach[0], e.nach[1], e.nach[2], e.nach[3])
                gemalt.append((k, r, True, g))
            elif g is not None:
                x0, x1, _a = self._abbilden(g, k)
                gemalt.append((k, (x0, g.y0, x1, g.y1), False, g))

        if self.bild is not None:
            # INTEGER FACTORS. Tkinter can only enlarge (zoom) or shrink (subsample)
            # a PhotoImage by integers. Instead of resampling the picture in
            # Python - 200,000 pixels per drawing - the scale of the sketch
            # follows what the picture can do. Then
            # the background sits exactly under the rectangles instead of almost.
            zoom, unter = 1, 1
            for k in (4, 3, 2):
                if (lw * k) <= (breite - 16) and (lh * k) <= (hoehe - 16):
                    zoom = k
                    break
            if zoom == 1:
                while (lw / float(unter)) > (breite - 16) or (lh / float(unter)) > (hoehe - 16):
                    unter += 1
            s = zoom / float(unter)
            gezeigt = self.bild.zoom(zoom, zoom) if zoom > 1 else self.bild
            if unter > 1:
                gezeigt = gezeigt.subsample(unter, unter)
            self._gezeigtes_bild = gezeigt  # keep a reference
            rand = 8
            self._skala = s
            self._ursprung = (rand, rand)
            c.create_image(rand, rand, image=gezeigt, anchor="nw")
        else:
            x0, y0, x1, y1 = 0, 0, lw, lh
            for _k, r, _m, _g in gemalt:
                x0, y0 = min(x0, r[0]), min(y0, r[1])
                x1, y1 = max(x1, r[2]), max(y1, r[3])
            rand = 8
            s = min((breite - 2 * rand) / float(x1 - x0), (hoehe - 2 * rand) / float(y1 - y0))
            self._skala = s
            self._ursprung = (rand - x0 * s, rand - y0 * s)

        def px(x, y):
            return self._ursprung[0] + x * s, self._ursprung[1] + y * s

        # THE MENU IMAGE MOVES ALONG.
        #
        # The background is a capture: if one drags a box, the
        # menu stays in it while the rectangle wanders. Then there are two
        # boxes in the picture, both the same, and nothing can be aligned
        # against that.
        #
        # So the piece of picture the measured box covers is cut out of the
        # background and placed where the box now
        # stands. The place it comes from is darkened - otherwise
        # the old box would remain and there would be two again.
        #
        # THIS IS THE OLD PICTURE IN A NEW PLACE, not what the game
        # will draw. Width and height come from the measurement; if a row has
        # changed since then, they are wrong, and the dashed
        # frame above states the computed ones. It is placed at the top left, because
        # that is the one corner both rectangles have in common.
        zieht_mit = self.bild is not None and self._bild_paar()
        if zieht_mit:
            for k, r, _m, _g in gemalt:
                e = self.messung.nach_name(k.name)
                if e is None:
                    continue
                geschnitten = self._ausschnitt(e.nach, zoom, unter)
                if geschnitten is None:
                    continue
                stueck, sx, sy, sx1, sy1 = geschnitten
                # The source is darkened as far as it was cut.
                q0, q1 = px(sx, sy), px(sx1, sy1)
                c.create_rectangle(q0[0], q0[1], q1[0], q1[1],
                                   fill="#000000", stipple="gray50", outline="")
                # And placed at the new location, offset by exactly the distance
                # the box has moved.
                z0 = px(sx + (r[0] - e.nach[0]), sy + (r[1] - e.nach[1]))
                c.create_image(z0[0], z0[1], image=stueck, anchor="nw")

        a = px(0, 0)
        b = px(lw, lh)
        if self.bild is None:
            c.create_rectangle(a[0], a[1], b[0], b[1], outline="#4a5568", fill="#171a20")
        if self._raster > 0:
            x = 0
            while x <= lw:
                q0, q1 = px(x, 0), px(x, lh)
                c.create_line(q0[0], q0[1], q1[0], q1[1], fill="#333a46")
                x += self._raster
            y = 0
            while y <= lh:
                q0, q1 = px(0, y), px(lw, y)
                c.create_line(q0[0], q0[1], q1[0], q1[1], fill="#333a46")
                y += self._raster

        if self._hilfslinien:
            # THE THIRDS ARE NOT DECORATION. CTR_UI_AnchorForBox guesses the anchor
            # by them: if the centre of a box lies left of the first third,
            # it hangs left, right of the second one right, in between centred.
            # Whoever moves a box without "anker" sees here when it changes the
            # answer.
            for x, farbe, marke in ((bezug // 3, "#5f8f4f", "1/3"),
                                    ((bezug * 2) // 3, "#5f8f4f", "2/3")):
                q0, q1 = px(x, 0), px(x, lh)
                c.create_line(q0[0], q0[1], q1[0], q1[1], fill=farbe, dash=(5, 3))
                c.create_text(q0[0] + 2, q0[1] + 2, anchor="nw", fill=farbe,
                              font=("TkDefaultFont", 7), text=marke)

            q0, q1 = px(lw // 2, 0), px(lw // 2, lh)
            c.create_line(q0[0], q0[1], q1[0], q1[1], fill="#4a6f9f")
            q0, q1 = px(0, lh // 2), px(lw, lh // 2)
            c.create_line(q0[0], q0[1], q1[0], q1[1], fill="#4a6f9f")
        # TWO EDGES, AND ONLY ONE IS HARD.
        #
        # The CANVAS is the end: nothing is drawn beyond it, and
        # the drag clamps to it. At 4:3 that is 512 columns, at 43:18 918.
        #
        # The 512 REFERENCE SPACE is not a limit but information: declaring happens
        # in it, and in it the thirds rule decides the anchor.
        # At 43:18 it lies in the middle of the picture.
        k0, k1 = px(0, 0), px(lw, lh)
        c.create_rectangle(k0[0], k0[1], k1[0], k1[1], outline="#e0603a", width=2)
        c.create_text(k1[0] - 3, k1[1] - 3, anchor="se", fill="#e0603a", font=("TkDefaultFont", 8),
                      text="canvas %d" % lw)

        if lw != bezug:
            b0, b1 = px(bezug, 0), px(bezug, lh)
            c.create_line(b0[0], b0[1], b1[0], b1[1], fill="#a8703a", dash=(3, 3))
            c.create_text(b0[0] - 3, b0[1] + 3, anchor="ne", fill="#a8703a", font=("TkDefaultFont", 7),
                          text="reference space %d" % bezug)

        marke = "%dx%d%s" % (lw, lh, "" if lw == bezug else "  canvas, reference space %d" % bezug)
        if not gilt and self.messung.vblank:
            marke += "   measurement outdated, everything computed"
        if self.bild is not None and not self._bild_gilt():
            marke += "   PICTURE OUTDATED - the game has not caught up yet"
        if not self._bild_passt():
            marke += "   PICTURE %d WIDE, MEASUREMENT %d - different runs" % (self.bild_w, self.messung.leinwand)
        if zieht_mit:
            marke += "   menu image moves along, sizes from the measurement"
        elif self.bild is not None:
            marke += "   picture is fixed - no measurement for it that could cut it"
        c.create_text(a[0] + 5, a[1] + 5, anchor="nw", fill="#000000", font=("TkDefaultFont", 8), text=marke)
        c.create_text(a[0] + 4, a[1] + 4, anchor="nw", fill="#e8ecf2", font=("TkDefaultFont", 8), text=marke)

        block, _z, _k = self._gewaehlt()
        gewaehlt = block.name if (block is not None and block.art == "kasten") else None

        for k, r, ist_gemessen, g in gemalt:
            aktiv = k.name == gewaehlt
            farbe = "#ffd257" if aktiv else "#7fb0e8"
            p0 = px(r[0], r[1])
            p1 = px(r[2], r[3])

            # OUTSIDE. Dragging MAY go beyond it - an edge that
            # holds the pointer conceals that the declaration can take the value
            # anyway. Instead it is said: red colour,
            # red outline, and the amount stands next to it.
            hinaus = self._hinaus(k, g, lh)
            if hinaus:
                farbe = "#ff6b5a"

            c.create_rectangle(p0[0], p0[1], p1[0], p1[1], outline=farbe, width=2 if aktiv else 1,
                               dash=() if ist_gemessen else (4, 3))
            self._rechtecke[k.name] = (p0[0], p0[1], p1[0], p1[1])

            c.create_text(p0[0] + 3, p0[1] - 2, anchor="sw", fill=farbe, font=("TkDefaultFont", 8),
                          text=(k.name if ist_gemessen else k.name + "  ~") + ("   OUTSIDE " + hinaus if hinaus else ""))
            # WHAT THIS RECTANGLE IS WORTH IS WRITTEN NEXT TO IT.
            #
            # For a cascade box x and y are an offset onto the position
            # the drawer sets - the sketch shows them as a location anyway,
            # because it cannot show anything else. And a box without "zeile"
            # carries its rows in the code: its width here is the one from title
            # and minimum width alone.
            anmerkung = "measured" if ist_gemessen else "computed"
            if not ist_gemessen:
                if self._ist_kaskade(k.name):
                    anmerkung += ", OFFSET"
                if not k.zeilen:
                    anmerkung += ", rows in code"
            c.create_text(p1[0] - 3, p0[1] - 2, anchor="se", fill="#9aa3b2", font=("TkDefaultFont", 8),
                          text="%dx%d %s" % (r[2] - r[0], r[3] - r[1], anmerkung))

            if g is None:
                continue

            schrift = max(6, int(round(g.zeilen[0].hoehe * s * 0.72))) if g.zeilen else 8
            tx = px(r[0] + (g.text_x - g.x0), 0)[0]
            for eintrag in ([g.titel] if g.titel is not None else []) + list(g.zeilen):
                ty = px(0, eintrag.y)[1]
                c.create_text(tx, ty, text=eintrag.text, anchor="n" if g.zentriert else "nw",
                              fill=("#f2f4f8" if eintrag.sicher else "#9aa3b2"),
                              font=("TkDefaultFont", schrift, "bold" if eintrag is g.titel else "normal"))

    def _platz(self, name: str):
        """The entry from s_slots, or None."""
        for p in self.tab.plaetze:
            if p.name == name:
                return p
        return None

    def _ist_kaskade(self, name: str) -> bool:
        p = self._platz(name)
        return bool(p is not None and p.kaskade)

    def _gerechnet(self, k):
        """The recomputed rectangle of this box, or None."""
        if not self.regeln.gefunden:
            return None
        return MASS.rechne(k, self.regeln,
                           hauptmenue=(k.name == self.tab.hauptmenue()),
                           kaskade=self._ist_kaskade(k.name))

    def _rechteck_fuer(self, k, x: int, y: int):
        """The rectangle on the canvas that x and y would give. Or None."""
        merk = (k.x, k.y)
        k.x, k.y = x, y
        g = self._gerechnet(k)
        k.x, k.y = merk
        if g is None:
            return None
        x0, x1, _a = self._abbilden(g, k)
        return (x0, g.y0, x1, g.y1)

    def _formkonstanten(self, k):
        """The rectangle at x=0 and y=0: (c0, c1, d0, d1) in the reference space.

        IN x AND y THE COMPUTATION IS A PURE SHIFT - in RECTMENU
        x only appears in a sum (rand_x = versatz_x + x - RM_S(frameOffsetX)), the
        width depends on the rows alone. So every position can be derived from this one
        evaluation, and aligning becomes solving instead of
        probing. The assumption is checked anyway, against the current position.
        """
        merk = (k.x, k.y)
        k.x, k.y = 0, 0
        null = self._gerechnet(k)
        k.x, k.y = merk
        if null is None:
            return None

        jetzt = self._gerechnet(k)
        if jetzt is None:
            return None
        if (jetzt.x0 - null.x0, jetzt.y0 - null.y0) != ((k.x or 0), (k.y or 0)) or \
           (jetzt.x1 - jetzt.x0) != (null.x1 - null.x0):
            return None

        return null.x0, null.x1, null.y0, null.y1

    def _x_fuer(self, was: str, ziel: int, c0: int, c1: int,
                anker: str, lw: int, bezug: int):
        """The x that hits the desired edge or centre.

        TWO KINDS OF ANCHOR. links, rechts and mitte shift - there one
        point of declaration is one point of canvas, and the equation solves directly.
        leinwand STRETCHES: (x * canvas) / reference space. A drag of one point
        becomes 918/512 points there, and whoever simply adds the canvas difference to
        x overshoots further with every pass instead of getting closer
        - measured: 0 became 93, 918 became 835.

        That is why the difference is converted back first, and then
        seven neighbours are tried: the mapping divides in integers, so the
        arithmetic hit need not be the best one.
        """
        verschiebung = MASS.bilde_ab(0, anker, lw, bezug)
        faktor = (float(bezug) / lw) if (anker == MASS.ANKER_LEINWAND and lw > 0) else 1.0

        if was == "links":
            geraten = (ziel - verschiebung) * faktor - c0
        elif was == "rechts":
            geraten = (ziel - verschiebung) * faktor - c1
        else:
            geraten = ((ziel - 2 * verschiebung) * faktor - c0 - c1) / 2.0

        bester, bester_fehler = None, None
        for x in range(int(geraten) - 3, int(geraten) + 4):
            x0 = MASS.bilde_ab(x + c0, anker, lw, bezug)
            x1 = MASS.bilde_ab(x + c1, anker, lw, bezug)
            if was == "links":
                fehler = abs(2 * (x0 - ziel))
            elif was == "rechts":
                fehler = abs(2 * (x1 - ziel))
            else:
                fehler = abs(x0 + x1 - ziel)
            if bester_fehler is None or fehler < bester_fehler:
                bester, bester_fehler = x, fehler

        return bester, bester_fehler

    def _ausrichten(self, was: str) -> None:
        """Put a box at an edge or in the middle of the canvas.

        WHAT IS SET IS x OR y, WHAT IS ALIGNED IS THE RECTANGLE. That way
        no button needs to know whether the box has "bezug mitte" or "bezug ecke":
        with mitte half the width is already in the rectangle, with ecke the
        whole. The same applies to the anchor.

        THE ANCHOR IS NOT GUESSED BUT ENUMERATED. If it is declared in the file,
        there is exactly one equation. If it is not, the
        thirds rule applies, and that depends on the position one is still searching for - so
        it is solved for EACH of the three answers and then checked whether the
        position found gives the same answer again. Only such positions count.

        Probing does not do it: with "bezug ecke" on 918 columns
        the answer jumps back and forth between left and right, and the iteration
        oscillates between 53 and 865 instead of finding the solution at 459 - the
        centre lies between two jump widths. Measured before this was written.
        """
        block, _z, _k = self._gewaehlt()
        if block is None or block.art != "kasten":
            self._sagen("Align: no box selected.")
            return

        k = block
        if k.x is None or k.y is None:
            self._sagen("'%s' declares no position - declare x and y in the form."
                        % k.name)
            return

        konstanten = self._formkonstanten(k)
        if konstanten is None:
            self._sagen("Align: not possible without the recomputation - %s"
                        % (self.regeln.hinweis or "the rectangle does not follow x and y"))
            return
        c0, c1, d0, d1 = konstanten

        # THE OFFSET OF THE COMPUTATION. If a valid measurement is available, it is the
        # truth; the port can be off. The difference is a
        # shift and applies to every position of the same box, so the
        # target is set back by it instead of switching between two sources.
        lw, lh = self._leinwand_breite(), self._leinwand_hoehe()
        bezug = self.messung.bezug_w or 512
        vx = vy = 0
        quelle = "computed"
        e = self.messung.nach_name(k.name) if self._messung_gilt() else None
        if e is not None:
            r = self._rechteck_fuer(k, k.x, k.y)
            if r is not None:
                vx, vy = e.nach[0] - r[0], e.nach[1] - r[1]
            quelle = "measured" if (vx == 0 and vy == 0) \
                else "measured, computation off by %d,%d" % (vx, vy)

        x, y = k.x, k.y
        rest = ""

        if was in ("links", "rechts", "mitte-x", "mitte"):
            achse = "mitte-x" if was == "mitte" else was
            ziel = {"links": 0, "rechts": lw, "mitte-x": lw}[achse]
            ziel -= (vx if achse != "mitte-x" else 2 * vx)

            kandidaten = [k.anker] if k.anker else [MASS.ANKER_LINKS, MASS.ANKER_MITTE,
                                                    MASS.ANKER_RECHTS]
            treffer = []
            for anker in kandidaten:
                kx, fehler = self._x_fuer(achse, ziel, c0, c1, anker, lw, bezug)
                if kx is None:
                    continue
                if not k.anker and MASS.anker_geraten(kx + c0, kx + c1, bezug) != anker:
                    continue        # the position contradicts its own assumption
                treffer.append((fehler, abs(kx - k.x), kx, anker))

            if not treffer:
                self._sagen("Align: '%s' cannot stand there - the guessed anchor "
                            "flips every time. Declare it with 'anker', then it works."
                            % k.name)
                return
            treffer.sort()
            fehler, _weg, x, anker = treffer[0]
            if fehler:
                rest = "   %s%s off" % ("half a point" if fehler == 1
                                        else "%.1f points" % (fehler / 2.0),
                                        " (odd width)" if fehler == 1 else "")

        if was in ("oben", "unten", "mitte-y", "mitte"):
            # VERTICALLY THERE IS NO ANCHOR. CTR_UI_MapX only maps x; y
            # stands at the same place in the reference space as on the canvas.
            if was == "oben":
                y = -d0 - vy
            elif was == "unten":
                y = lh - d1 - vy
            else:
                y = int(round((lh - d0 - d1) / 2.0)) - vy

        if (x, y) == (k.x, k.y):
            self._sagen("%s is already there - x %d y %d (%s)%s" % (k.name, x, y, quelle, rest))
            return

        k.x, k.y = x, y
        self.geaendert = True
        self._auswahl()

        r = self._rechteck_fuer(k, x, y)
        lage = ""
        if r is not None:
            lage = "   rectangle %d,%d..%d,%d in %dx%d" % (r[0] + vx, r[1] + vy,
                                                           r[2] + vx, r[3] + vy, lw, lh)
        self._sagen("%s aligned - x %d  y %d  (%s)%s%s"
                    % (k.name, x, y, quelle, lage, rest))

    def _kasten_unter(self, x, y):
        for name, (ax, ay, bx, by) in self._rechtecke.items():
            if ax <= x <= bx and ay <= y <= by:
                return name
        return None

    def _greifen(self, ev) -> None:
        name = self._kasten_unter(ev.x, ev.y)
        if name is None:
            return
        k = self.datei.kasten(name)
        if k is None:
            return
        if k.x is None or k.y is None:
            self._sagen(
                "'%s' declares no position - declare x and y in the form. "
                "In the cascade the drawer sets them anyway." % name
            )
            return
        for bi, b in enumerate(self.datei.bloecke):
            if b is k:
                self.baum.selection_set("b%d" % bi)
                break
        self._zieht = (name, ev.x, ev.y, k.x, k.y)

    def _ziehen(self, ev) -> None:
        if self._zieht is None:
            return
        name, ax, ay, kx, ky = self._zieht
        k = self.datei.kasten(name)
        if k is None:
            return
        neu_x, neu_y = self._fangen_kasten(k,
                                           int(round(kx + (ev.x - ax) / self._skala)),
                                           int(round(ky + (ev.y - ay) / self._skala)))
        k.x, k.y = self._klemmen(k, neu_x, neu_y)

        g = self._gerechnet(k)
        lh = self._leinwand_hoehe()
        hinaus = self._hinaus(k, g, lh)

        self._sagen("%s  x %d  y %d%s" % (name, k.x, k.y, ("   OUTSIDE " + hinaus) if hinaus else ""))
        self._skizze()

    def _loslassen(self, ev) -> None:
        if self._zieht is None:
            return
        name = self._zieht[0]
        self._zieht = None
        self.geaendert = True
        self._auswahl()
        self._sagen("%s moved - save, the game rereads within one second" % name)

    # -- Clock --------------------------------------------------------------

    def _takt(self) -> None:
        try:
            neu_erg = self._ergebnis_lesen()
            neu_bild = self._bild_lesen()

            if neu_erg or neu_bild:
                self._tabelle_fuellen()
                self._skizze()
                self._sagen("result reread - VBlank %d%s"
                            % (self.messung.vblank, ", with picture" if self.bild is not None else ""))
            elif self.pfad is not None and self.pfad.is_file() and not self.geaendert:
                m = self.pfad.stat().st_mtime
                if self.mtime_menu is not None and m > self.mtime_menu + 0.001:
                    self.mtime_menu = m
                    self.oeffnen(self.pfad)
                    self._sagen("file changed from outside - reread")
        finally:
            self.after(POLL_MS, self._takt)

    def _sagen(self, text: str) -> None:
        self.status.configure(text=text)


def main() -> None:
    import sys

    fenster = tk.Tk()
    fenster.title("CTR Reload - menu editor")
    fenster.geometry("1180x900")
    Fenster(fenster, sys.argv[1] if len(sys.argv) > 1 else None)
    fenster.mainloop()
