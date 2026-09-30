# -*- mode: python ; coding: utf-8 -*-
#
# NOTE. The packed file does not find the tree it was built in -
# it reads the verb tables from game/native_menudecl.c relative to its
# own location. If it lies outside the tree, it says so in the status line
# and no longer checks names. For work on the tree, "python -m menued" is
# the shorter way.

a = Analysis(
    ['menued_gui.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='menued-gui',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
