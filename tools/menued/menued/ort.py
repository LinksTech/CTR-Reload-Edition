"""Where the tree lies - the only question a packed exe has to ask.

ONE QUESTION, NOT THREE. The editor needs three things from the tree: the
verb tables (game/native_menudecl.c, include/namespace_MenuDecl.h), the
menu file (menus/nitro-pit.menu) and the result file next to it. All three
lie at fixed places BELOW the root - if the root is known, all
three are known. Three separate path settings would be the same fact
three times, and two of them would drift apart at the first move.

HOW IT IS FOUND, in this order:

  1. What is on the command line. An invocation is the most precise statement
     there is, and it must beat everything else - otherwise a
     second tree cannot be opened.
  2. What applied last time. Stored in menued.cfg.
  3. Upwards from the own location. That is the normal case: pyinstaller puts the
     exe into tools/menued/dist/, and from there it is three steps to the
     root. Started as a script, the same applies from menued/ort.py.
  4. Upwards from the working directory. For starting from a console that
     is already in the tree.

If none of the four finds anything, the window asks once and remembers the
answer. Nothing is guessed.

THE MEMO FILE DOES NOT LIE NEXT TO THE EXE. An exe can lie on the desktop,
in a folder without write permission or on a stick; writing next to it
is wrong in two out of three cases. It lies under LOCALAPPDATA,
where Windows expects a program's state.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Optional, Tuple

# How a tree is recognised. Not by a folder name - the same folder name exists
# elsewhere too -, but by the file the editor has to read anyway.
BAUMMARKE = Path("game") / "native_menudecl.c"

MENUEDATEI = Path("menus") / "nitro-pit.menu"


def gepackt() -> bool:
    return bool(getattr(sys, "frozen", False))


def eigener_ort() -> Path:
    """The own location on disk.

    For a packed exe the exe itself - __file__ then points into
    PyInstaller's unpack directory, which has nothing to do with the tree and
    disappears again after the run.
    """
    if gepackt():
        return Path(sys.executable).resolve()
    return Path(__file__).resolve()


def merkdatei() -> Path:
    basis = os.environ.get("LOCALAPPDATA") or os.environ.get("XDG_STATE_HOME")
    if basis:
        return Path(basis) / "CTR Reload" / "menued.cfg"
    return Path.home() / ".ctr-reload-menued.cfg"


def ist_baum(p) -> bool:
    try:
        return (Path(p) / BAUMMARKE).is_file()
    except OSError:
        return False


def suche_aufwaerts(start) -> Optional[Path]:
    start = Path(start)
    for p in [start] + list(start.parents):
        if ist_baum(p):
            return p
    return None


def gemerkte_wurzel() -> Optional[Path]:
    p = merkdatei()
    try:
        text = p.read_text(encoding="utf-8").strip()
    except OSError:
        return None
    if text and ist_baum(text):
        return Path(text)
    return None


def merke_wurzel(wurzel) -> bool:
    p = merkdatei()
    try:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(str(Path(wurzel)), encoding="utf-8")
        return True
    except OSError:
        # No write permission is no reason not to work. The editor
        # keeps running, it just asks again next time.
        return False


def aus_argument(arg) -> Tuple[Optional[Path], Optional[Path]]:
    """(root, menu file) from an invocation argument. Both may be None.

    Both are allowed: the tree itself or a .menu file in it. Whoever names a
    file means it - and its tree follows from it.
    """
    if not arg:
        return None, None

    p = Path(arg).expanduser()

    if p.is_file():
        return suche_aufwaerts(p.parent), p.resolve()

    if p.is_dir():
        return (p.resolve() if ist_baum(p) else suche_aufwaerts(p.resolve())), None

    return None, None


def finde_wurzel(arg=None) -> Tuple[Optional[Path], str]:
    """The root and the sentence that says where it comes from."""
    wurzel, _datei = aus_argument(arg)
    if wurzel is not None:
        return wurzel, "from the invocation"

    wurzel = gemerkte_wurzel()
    if wurzel is not None:
        return wurzel, "remembered in %s" % merkdatei()

    wurzel = suche_aufwaerts(eigener_ort().parent)
    if wurzel is not None:
        return wurzel, "upwards from %s" % eigener_ort().parent

    wurzel = suche_aufwaerts(Path.cwd())
    if wurzel is not None:
        return wurzel, "upwards from the working directory"

    return None, "not found"


def menuedatei(wurzel) -> Optional[Path]:
    if wurzel is None:
        return None
    p = Path(wurzel) / MENUEDATEI
    return p if p.is_file() else None
