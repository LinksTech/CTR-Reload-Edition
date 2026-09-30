#!/usr/bin/env bash
# Builds the release package (Windows only, run from Git Bash).
#
#   [BUILD_DIR=<folder>] bash tools/package/package.sh [--allow-dirty]
#
# BUILD_DIR is the CMake build folder, relative to the tree root
# (default: build-msvc-x86, the folder of the windows-msvc-x86 preset).
#
# 1. Only from a clean tree (without --allow-dirty): the programs and the
#    source code in the package must come from the same commit.
# 2. The version is CTR_NATIVE_VERSION from CMakeLists.txt. The package is
#    named CTR-Reload-<version without spaces>-<build id>.
# 3. Configure and build ctr_native and alphamaker (Release) in BUILD_DIR.
#    The build output goes to dist-build.log in the tree root.
# 4. Pairing check: ctr_native.exe --version reports
#    "CTR Reload <version> (<build id>)", and alphamaker.exe carries the same
#    build ID. The build ID is created on every build (cmake/CtrBuildId.cmake).
# 5. Package by ALLOW LIST, never the build folder as a whole:
#      ctr_native.exe, ctr_native.pdb, alphamaker.exe,
#      LICENSE, THIRD_PARTY_NOTICES.md, README.txt, RELEASE-NOTES.txt,
#      <name>-source.zip (GPL: the source code of this commit, git archive by
#      allow list - everything the build needs, nothing else).
#    rldpack is not shipped on its own: the Alpha-Maker carries it
#    (alphamaker.exe --rldpack ...).
# 6. Check: no disc content, no containers, no test data, no measurement or
#    reference folders, no user files in the package and in the source zip.
#    A hit aborts, and no zip is created.
#
# Output: dist/<name>.zip with the folder <name>/ inside, which holds the files
# of step 5 directly (<name>/ctr_native.exe, <name>/alphamaker.exe, ...).
# The build ID is the part of <name> after the last "-" (for a clean tree).
set -eu
cd "$(dirname "$0")/../.."

ALLOW_DIRTY=0
[ "${1:-}" = "--allow-dirty" ] && ALLOW_DIRTY=1

if [ -n "$(git status --porcelain --untracked-files=no)" ] && [ "$ALLOW_DIRTY" = 0 ]; then
	echo "ABORT: the tree has uncommitted changes. Commit first (or --allow-dirty for a trial package)."
	exit 2
fi

# The version, from the one line that defines it.
VERSION=$(tr -d '\r' < CMakeLists.txt | sed -n 's/^set(CTR_NATIVE_VERSION "\([^"]*\)")$/\1/p')
if [ -z "$VERSION" ] || [ "$(printf '%s\n' "$VERSION" | wc -l)" -ne 1 ]; then
	echo "ABORT: no single set(CTR_NATIVE_VERSION \"...\") line found in CMakeLists.txt"
	exit 4
fi
case "$VERSION" in
	*[!A-Za-z0-9\ ._-]*) echo "ABORT: version '$VERSION' has characters that do not belong in a file name"; exit 4 ;;
esac
VERSION_TAG=$(printf '%s' "$VERSION" | tr -d ' ')
echo "== Version: $VERSION"

BUILD_DIR=${BUILD_DIR:-build-msvc-x86}
echo "== Configure and build (Release) in $BUILD_DIR"
MSYS_NO_PATHCONV=1 BUILD_DIR="$BUILD_DIR" powershell -NoProfile -Command '
	cmake --preset windows-msvc-x86 -B $env:BUILD_DIR | Out-Null; if ($LASTEXITCODE) { exit 1 }
	cmake --build $env:BUILD_DIR --config Release --target ctr_native alphamaker; exit $LASTEXITCODE' > dist-build.log 2>&1 || {
	echo "ABORT: build failed, see dist-build.log"; exit 3; }

R=$BUILD_DIR/Release
for f in ctr_native.exe ctr_native.pdb alphamaker.exe; do
	[ -f "$R/$f" ] || { echo "ABORT: $R/$f is missing"; exit 3; }
done

LINE=$("$R/ctr_native.exe" --version 2>/dev/null | tr -d '\r' | head -1)
case "$LINE" in
	"CTR Reload $VERSION ("*")") ;;
	*) echo "ABORT: --version reports '$LINE', expected 'CTR Reload $VERSION (<build id>)'"; exit 4 ;;
esac
ID=${LINE##*(}; ID=${ID%)}
case "$ID" in
	*dirty*|unknown*) [ "$ALLOW_DIRTY" = 1 ] || { echo "ABORT: build ID '$ID'"; exit 4; } ;;
esac

# Pairing: the same ID as text in the Alpha-Maker (narrow or UTF-16).
python - "$ID" "$R/alphamaker.exe" <<'PY' || { echo "ABORT: alphamaker.exe does not carry the build ID of ctr_native.exe"; exit 4; }
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

NAME="CTR-Reload-$VERSION_TAG-$ID"
OUT="dist/$NAME"
rm -rf "$OUT" "dist/$NAME.zip"
mkdir -p "$OUT"
cp "$R/ctr_native.exe" "$R/ctr_native.pdb" "$R/alphamaker.exe" "$OUT/"
cp LICENSE THIRD_PARTY_NOTICES.md "$OUT/"
for t in README RELEASE-NOTES; do
	sed -e "s/@VERSION@/$VERSION/g" -e "s/@BUILD@/$ID/g" -e "s/@NAME@/$NAME/g" \
		"tools/package/$t.txt" > "$OUT/$t.txt"
done

echo "== Source code (GPL) by allow list"
git archive --format=zip --prefix="$NAME-source/" -o "$OUT/$NAME-source.zip" HEAD -- \
	CMakeLists.txt CMakePresets.json build-msvc.bat main.c LICENSE THIRD_PARTY_NOTICES.md README.md BUILDING.md \
	.gitignore .clang-format .clang-tidy .clangd \
	cmake game include platform tools metadata externals/SDL

echo "== Check for forbidden content"
python - "$OUT" <<'PY' || { echo "ABORT: forbidden content, no package"; exit 5; }
import os, sys, zipfile
root = sys.argv[1]
BAD_EXT = ('.bin', '.cue', '.iso', '.img', '.lev', '.vrm', '.sca', '.sndb', '.rldtrack', '.rldprev', '.xa', '.str',
           '.tga', '.ctrstates', '.log', '.ppm', '.bmp')
BAD_NAME = ('bigfile.big', 'kart.hwl', 'ctr-data.cfg', 'ctr-settings.cfg', 'ctr-view.cfg')
# Game data, user files, and local measurement/reference/test-data folders
# (literal folder names, the same ones .gitignore keeps out of the repository).
BAD_DIR = ('concept/', 'messung-', 'baseline-', 'testdata/', 'test-bats/', 'memcards/', 'assets/', 'tracks/', 'tracks_archive/', 'debug/')
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
# The release job takes these two out of the zip by exactly these paths.
with zipfile.ZipFile(out) as z:
    names = z.namelist()
missing = [base + '/' + f for f in ('ctr_native.exe', 'alphamaker.exe') if base + '/' + f not in names]
if missing:
    print('  missing in the zip:', ' '.join(missing))
    os.remove(out)
    sys.exit(1)
h = hashlib.sha256(open(out, 'rb').read()).hexdigest()
print('  %s  %d bytes  sha256 %s' % (out, os.path.getsize(out), h))
PY
echo "== done: dist/$NAME.zip"
