#!/usr/bin/env bash
# Beta 0 package for track builders (2026-09-30).
#
#   [BAU=<folder>] bash tools/paket/paket-beta0.sh [--allow-dirty]
#   (from the tree root, Git Bash; Windows only - no Linux package, decided 2026-09-30)
#
# 1. Only from a clean state (without --allow-dirty): the build and the
#    source code in the package must be the same commit.
# 2. Configure and build: ctr_native, rldpack, alphamaker. The build ID
#    (CTR_NATIVE_BUILD_ID) has been created on every build since 2026-09-29
#    (cmake/CtrBuildId.cmake), no longer only at configure time.
# 3. Pairing check: all three exe carry the same ID.
# 4. Package by ALLOW LIST, never the release folder as a whole:
#      ctr_native.exe, ctr_native.pdb, alphamaker.exe, rldpack.exe,
#      LICENSE, THIRD_PARTY_NOTICES.md, README.txt, RELEASE-NOTES.txt,
#      <name>-source.zip (GPL: the source code of this commit, git archive by
#      allow list - without measurement folders, references, concept/, test data and
#      pictures from the game).
# 5. Check: no disc content, no containers, no test data, no
#    measurement/reference folders, no user files in the package and the source zip.
#    A hit aborts, and no zip is created.
# Output: dist/CTR-Reload-Beta0-<id>.zip and its SHA-256.
set -eu
cd "$(dirname "$0")/../.."

ALLOW_DIRTY=0
[ "${1:-}" = "--allow-dirty" ] && ALLOW_DIRTY=1

if [ -n "$(git status --porcelain --untracked-files=no)" ] && [ "$ALLOW_DIRTY" = 0 ]; then
	echo "ABORT: the tree has uncommitted changes. Commit first (or --allow-dirty for a trial build)."
	exit 2
fi

# BAU=<folder> builds outside of build-msvc-x86 (2026-09-29: an orphaned
# cl.exe held main.obj locked there). The preset stays the same.
BAU=${BAU:-build-msvc-x86}
echo "== Configure and build (Release) in $BAU"
MSYS_NO_PATHCONV=1 BAU="$BAU" powershell -NoProfile -Command '
	cmake --preset windows-msvc-x86 -B $env:BAU | Out-Null; if ($LASTEXITCODE) { exit 1 }
	cmake --build $env:BAU --config Release --target ctr_native rldpack alphamaker; exit $LASTEXITCODE' > dist-bau.log 2>&1 || {
	echo "ABORT: build failed, see dist-bau.log"; exit 3; }

R=$BAU/Release
for f in ctr_native.exe ctr_native.pdb rldpack.exe alphamaker.exe; do
	[ -f "$R/$f" ] || { echo "ABORT: $R/$f is missing"; exit 3; }
done

LINE=$("$R/ctr_native.exe" --version 2>/dev/null | tr -d '\r' | head -1)
case "$LINE" in
	"CTR Reload Beta 0 ("*")") ;;
	*) echo "ABORT: --version reports '$LINE'"; exit 4 ;;
esac
ID=${LINE##*(}; ID=${ID%)}
case "$ID" in
	*dirty*|unknown*) [ "$ALLOW_DIRTY" = 1 ] || { echo "ABORT: build ID '$ID'"; exit 4; } ;;
esac

# Pairing: the same ID as text in rldpack (narrow) and in the Alpha-Maker
# (narrow or UTF-16).
python - "$ID" "$R/rldpack.exe" "$R/alphamaker.exe" <<'PY' || { echo "ABORT: the exe do not carry the same ID"; exit 4; }
import sys
bid = sys.argv[1].encode('ascii')
for p in sys.argv[2:]:
    d = open(p, 'rb').read()
    # Whole, not as a substring: narrow with NUL after it, UTF-16 with NUL or ')'
    # after it (title of the Alpha-Maker) - otherwise abc... would also match abc...-dirty-...
    wide = bid.decode().encode('utf-16-le')
    if bid + b'\0' not in d and wide + b'\0\0' not in d and wide + ')'.encode('utf-16-le') not in d:
        print('  without ID:', p)
        sys.exit(1)
print('  pair ok:', sys.argv[1])
PY

NAME="CTR-Reload-Beta0-$ID"
OUT="dist/$NAME"
rm -rf "$OUT" "dist/$NAME.zip"
mkdir -p "$OUT"
cp "$R/ctr_native.exe" "$R/ctr_native.pdb" "$R/rldpack.exe" "$R/alphamaker.exe" "$OUT/"
cp LICENSE THIRD_PARTY_NOTICES.md "$OUT/"
sed "s/@BUILD@/$ID/g" tools/paket/README-Beta0.txt > "$OUT/README.txt"
sed "s/@BUILD@/$ID/g" tools/paket/RELEASE-NOTES-Beta0.txt > "$OUT/RELEASE-NOTES.txt"

echo "== Source code (GPL) by allow list"
git archive --format=zip --prefix="$NAME-source/" -o "$OUT/$NAME-source.zip" HEAD -- \
	CMakeLists.txt CMakePresets.json build-msvc.bat main.c LICENSE THIRD_PARTY_NOTICES.md README.md BUILDING.md \
	cmake game include platform tools menus metadata externals/SDL

echo "== Check for forbidden content"
python - "$OUT" <<'PY' || { echo "ABORT: forbidden content, no package"; exit 5; }
import os, sys, zipfile
root = sys.argv[1]
BAD_EXT = ('.bin', '.cue', '.iso', '.img', '.lev', '.vrm', '.sca', '.sndb', '.rldtrack', '.rldprev', '.xa', '.str',
           '.tga', '.ctrstates', '.log', '.ppm', '.bmp')
BAD_NAME = ('bigfile.big', 'kart.hwl', 'ctr-data.cfg', 'ctr-settings.cfg', 'ctr-view.cfg')
BAD_DIR = ('concept/', 'messung-', 'baseline-', 'testdaten/', 'test-bats/', 'memcards/', 'assets/', 'tracks/', 'tracks_archiv/', 'debug/')
bad = []
def check(name):
    n = name.replace('\\', '/').lower()
    base = n.rsplit('/', 1)[-1]
    if base.endswith(BAD_EXT) or base in BAD_NAME:
        bad.append(name)
    elif any(('/' + d) in ('/' + n) for d in BAD_DIR):
        bad.append(name)
    elif base.endswith(('.png', '.jpg')) and '/externals/sdl/' not in ('/' + n):
        bad.append(name)
for f in os.listdir(root):
    check(f)
    if f.endswith('-source.zip'):
        with zipfile.ZipFile(os.path.join(root, f)) as z:
            for n in z.namelist():
                check(n.split('/', 1)[1] if '/' in n else n)
if bad:
    for b in bad[:40]:
        print('  forbidden:', b)
    sys.exit(1)
print('  nothing forbidden')
PY

python - "$OUT" "dist/$NAME.zip" <<'PY'
import os, sys, zipfile, hashlib
root, out = sys.argv[1], sys.argv[2]
base = os.path.basename(root)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(root)):
        z.write(os.path.join(root, f), base + '/' + f)
h = hashlib.sha256(open(out, 'rb').read()).hexdigest()
print('  %s  %d bytes  sha256 %s' % (out, os.path.getsize(out), h))
PY
echo "== done: dist/$NAME.zip"
