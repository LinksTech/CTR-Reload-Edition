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
#    named CTR-Reload-<version without spaces>-<build id>. The tools version
#    (Reload Studio and rldpack) is CTR_TOOLS_VERSION from the same file.
# 3. Configure and build ctr_native and ReloadStudio (Release) in BUILD_DIR.
#    The build output goes to dist-build.log in the tree root.
# 4. Pairing check: ctr_native.exe --version reports
#    "CTR Reload <version> (<build id>)", and ReloadStudio.exe carries the same
#    build ID and its sidebar line "<tools version> (<build id>)". The build ID
#    is created on every build (cmake/CtrBuildId.cmake); a "dirty" or "unknown"
#    ID aborts without --allow-dirty, like a tree with uncommitted changes.
# 5. Package by ALLOW LIST, never the build folder as a whole:
#      ctr_native.exe, ctr_native.pdb, ReloadStudio.exe,
#      LICENSE, THIRD_PARTY_NOTICES.md, README.txt, RELEASE-NOTES.txt,
#      docs/ANIMATIONS.md (the guide to animations of one's own, which
#      Reload Studio and README.txt name) and templates/ (LICENSE, README.md,
#      make_driver_template.py: the script that builds the Blender template;
#      the .blend itself is no file of the repository; make_creator_kit.py and
#      creator_kit_*.py: the scripts of the Creator Kit, which Reload Studio
#      also carries inside), docs/CREATOR_KIT.html (the guide of the Creator
#      Kit; its source docs/CREATOR_KIT.md and its pictures
#      docs/assets/kit-guide-*.svg only in the source zip),
#      <name>-source.zip (GPL: the source code of this commit, git archive by
#      allow list - everything the build needs and the guide with the
#      template's script, nothing else).
#    rldpack is not shipped on its own: Reload Studio carries it
#    (ReloadStudio.exe --rldpack ...).
# 6. Check: python tools/content_guard.py --package (the same rules as ever:
#    no file type that tools/content_guard.py refuses - disc content, game
#    formats, containers, 3D models, audio, pictures, archives ...; the SDL
#    files it lists by path and blob id pass - no logs, no test data, no
#    measurement or reference folders, no user files in the package and in
#    the source zip; plus creator kit names and content signatures on every
#    file and zip member). A hit aborts, and no zip is created.
#
# Output: dist/<name>.zip with the folder <name>/ inside, which holds the files
# of step 5 directly (<name>/ctr_native.exe, <name>/ReloadStudio.exe, ...,
# <name>/docs/ANIMATIONS.md, <name>/templates/...).
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
# The tools version (Reload Studio and rldpack), from its one line as well.
TOOLS_VERSION=$(tr -d '\r' < CMakeLists.txt | sed -n 's/^set(CTR_TOOLS_VERSION "\([0-9]*\.[0-9]*\.[0-9]*\)")$/\1/p')
if [ -z "$TOOLS_VERSION" ] || [ "$(printf '%s\n' "$TOOLS_VERSION" | wc -l)" -ne 1 ]; then
	echo "ABORT: no single set(CTR_TOOLS_VERSION \"<major>.<minor>.<patch>\") line found in CMakeLists.txt"
	exit 4
fi
echo "== Tools version (Reload Studio, rldpack): $TOOLS_VERSION"

BUILD_DIR=${BUILD_DIR:-build-msvc-x86}
echo "== Configure and build (Release) in $BUILD_DIR"
MSYS_NO_PATHCONV=1 BUILD_DIR="$BUILD_DIR" powershell -NoProfile -Command '
	cmake --preset windows-msvc-x86 -B $env:BUILD_DIR | Out-Null; if ($LASTEXITCODE) { exit 1 }
	cmake --build $env:BUILD_DIR --config Release --target ctr_native ReloadStudio; exit $LASTEXITCODE' > dist-build.log 2>&1 || {
	echo "ABORT: build failed, see dist-build.log"; exit 3; }

R=$BUILD_DIR/Release
for f in ctr_native.exe ctr_native.pdb ReloadStudio.exe; do
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

# Pairing: the same ID as text in Reload Studio (narrow or UTF-16), and the
# Studio's sidebar line "<tools version> (<build id>)" (UTF-16): the Studio in
# the package is the tools version of this tree, built in the same run.
python - "$ID" "$TOOLS_VERSION" "$R/ReloadStudio.exe" <<'PY' || { echo "ABORT: ReloadStudio.exe does not carry the build ID of ctr_native.exe and the tools version"; exit 4; }
import sys
bid = sys.argv[1].encode('ascii')
side = ('%s (%s)' % (sys.argv[2], sys.argv[1])).encode('utf-16-le')
for p in sys.argv[3:]:
    if side not in open(p, 'rb').read():
        print('  without "%s (%s)":' % (sys.argv[2], sys.argv[1]), p)
        sys.exit(1)
    d = open(p, 'rb').read()
    # Whole, not as a substring: narrow with NUL after it, UTF-16 with NUL or ')'
    # after it (title of Reload Studio) - otherwise abc... would also match abc...-dirty-...
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
cp "$R/ctr_native.exe" "$R/ctr_native.pdb" "$R/ReloadStudio.exe" "$OUT/"
cp LICENSE THIRD_PARTY_NOTICES.md "$OUT/"
mkdir -p "$OUT/docs" "$OUT/templates"
cp docs/ANIMATIONS.md docs/CREATOR_KIT.html "$OUT/docs/"
cp templates/LICENSE templates/README.md templates/make_driver_template.py templates/make_creator_kit.py \
	templates/creator_kit_puppet.py templates/creator_kit_clips.py templates/creator_kit_standin.py \
	templates/creator_kit_classic.py "$OUT/templates/"
for t in README RELEASE-NOTES; do
	sed -e "s/@VERSION@/$VERSION/g" -e "s/@BUILD@/$ID/g" -e "s/@NAME@/$NAME/g" \
		"tools/package/$t.txt" > "$OUT/$t.txt"
done

echo "== Source code (GPL) by allow list"
git archive --format=zip --prefix="$NAME-source/" -o "$OUT/$NAME-source.zip" HEAD -- \
	CMakeLists.txt CMakePresets.json build-msvc.bat main.c LICENSE THIRD_PARTY_NOTICES.md README.md BUILDING.md \
	.gitignore .clang-format .clang-tidy .clangd \
	cmake game include platform tools metadata externals/SDL externals/cgltf \
	docs/ANIMATIONS.md docs/CREATOR_KIT.md docs/CREATOR_KIT.html 'docs/assets/kit-guide-*.svg' \
	templates/LICENSE templates/README.md templates/make_driver_template.py templates/make_creator_kit.py \
	templates/creator_kit_puppet.py templates/creator_kit_clips.py templates/creator_kit_standin.py \
	templates/creator_kit_classic.py

echo "== Check for forbidden content"
python tools/content_guard.py --package "$OUT" || { echo "ABORT: forbidden content, no package"; exit 5; }

python - "$OUT" "dist/$NAME.zip" <<'PY'
import os, sys, zipfile, hashlib
root, out = sys.argv[1], sys.argv[2]
base = os.path.basename(root)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for top, dirs, files in os.walk(root):
        dirs.sort()
        for f in sorted(files):
            rel = os.path.relpath(os.path.join(top, f), root).replace(os.sep, '/')
            z.write(os.path.join(top, f), base + '/' + rel)
# The release jobs take these four out of the zip by exactly these paths.
with zipfile.ZipFile(out) as z:
    names = z.namelist()
missing = [base + '/' + f for f in ('ctr_native.exe', 'ReloadStudio.exe', 'LICENSE', 'THIRD_PARTY_NOTICES.md')
           if base + '/' + f not in names]
if missing:
    print('  missing in the zip:', ' '.join(missing))
    os.remove(out)
    sys.exit(1)
h = hashlib.sha256(open(out, 'rb').read()).hexdigest()
print('  %s  %d bytes  sha256 %s' % (out, os.path.getsize(out), h))
PY
echo "== done: dist/$NAME.zip"
