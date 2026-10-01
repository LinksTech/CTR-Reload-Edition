#!/usr/bin/env python3
"""Content guard: no game data in any commit.

    python tools/content_guard.py --history REV       every commit reachable from REV
    python tools/content_guard.py --range BASE HEAD   every commit in HEAD that is
                                                      not in BASE
    python tools/content_guard.py --github            the automatic check
                                                      (.github/workflows/guard.yml):
                                                      --history HEAD for a push or
                                                      a pull request
    python tools/content_guard.py --stdin             the files listed on standard
                                                      input, one per line as
                                                      "<type> <object id> <size>
                                                      <path>" (git ls-tree -l
                                                      without the mode); used by
                                                      tools/git-hooks/pre-push

Every file of every checked commit is looked at - the whole tree of each
commit, not only what it changed and not only the newest commit: a file that
one commit adds and a later one deletes is still in the history, and pushing
the history publishes it. A check fails on
  type      a path ending in a forbidden file type (disc images, game data,
            track and character containers, music data, patches, memory
            cards, save states and replays; 3D models, audio, pictures and
            archives, which are often made by other people), in any letter
            case, also with dots, spaces or control characters after it
  binary    a binary file larger than 1 MB: a NUL byte in its first 8000
            bytes, the same test git uses
The only exceptions are files of the bundled SDL copy (externals/SDL/): its
pictures and sounds, listed one by one in ALLOWED below with the exact path
and blob id. The same path with other content fails again.

On GitHub every run checks the whole history of the checked-out commit: the
pushed commit, or for a pull request the merge commit of the pull request into
its base. Push and pull request get the same check under the same name, a
direct push included, and a later commit that deletes a file never makes it
pass. A push that deletes a branch or tag has nothing to check and passes. Any
other event, a shallow clone or a git error fails the check.

tools/git-hooks/pre-push runs this check (--stdin) and a few more before
anything leaves the machine; this script is the check that GitHub runs on
every push and pull request. It needs only Python 3 and git, and it reads git
objects only.

Exit code 0 = clean, 1 = forbidden content, 2 = usage or git error. With
--stdin it prints one line "<path><TAB><reason>" per forbidden file and its
errors go to standard error.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import textwrap

# Forbidden file types, compared in lower case with the last extension of the
# file name after trailing dots, spaces and control characters are removed
# (TRAILING): the game's formats and the files made from them, and 3D models,
# audio, pictures and archives, which are often made by other people (a test
# generates its files in code).
# tools/git-hooks/pre-push uses this list too (--stdin).
GROUPS = (
    ('disc image', '.bin .cue .iso .img .chd .ecm .pbp .mdf .mds .nrg .ccd .sub .toc'),
    ('game data', '.lev .vrm .big .xa .str .hwl .ctr .mpk .xnf .vag .vab .vh .vb .tim .lng'),
    ('track container', '.rldtrack'),
    ('character container', '.rldchar'),
    ('track preview', '.rldprev'),
    ('music or sound data', '.sca .sndb .cseq'),
    ('patch', '.xdelta .ppf .ips .bps .vcdiff'),
    ('memory card or save file', '.mcr .mcd .gme .srm .sav'),
    ('save state', '.ctrstates .state'),
    ('replay', '.ctrreplay'),
    ('3D model', '.ply .obj .fbx .blend .blend1 .glb .gltf .dae .3ds .stl .abc .usd .usda'
                 ' .usdc .usdz .x3d .ma .mb .max .c4d .lwo .pmx .pmd .md2 .md3 .smd'),
    ('audio', '.wav .mp3 .ogg .flac .aif .aiff .m4a .opus .wma .mid .midi'
              ' .psf .psf2 .minipsf'),
    ('picture', '.png .jpg .jpeg .gif .webp .bmp .tga .ppm .tif .tiff .dds .psd .ico .icns'),
    ('archive', '.zip .rar .7z .tar .gz .tgz .bz2 .xz'),
)
TYPES = {ext: what for what, exts in GROUPS for ext in exts.split()}
TRAILING = re.compile(r'[\x00-\x20\x7f.]+$')

# The groups that an ALLOWED entry lifts: only those that the SDL copy needs.
# Game data, containers, patches, memory cards, save states, replays, 3D models
# and archives never pass, not even in externals/SDL/.
LIFTABLE = ('audio', 'picture')

LIMIT = 1024 * 1024     # a binary file larger than this fails
PROBE = 8000            # the bytes looked at for a NUL, as git does

# Files of the bundled SDL copy that may stay although their type is in a
# LIFTABLE group or they are binary and larger than LIMIT: the pictures and
# sounds of SDL's tests, examples and project files. One line per file
# content, "<blob id> <path>", exact: the same path with other content (an SDL
# update) fails until its new blob id is added here, and the old line stays,
# because the history keeps the old content. Only paths under ALLOWED_PREFIX
# count. The lines for files of a commit:
#   git ls-tree -r --format='%(objectname) %(path)' <commit> -- <files>
# No file above 1 MB needs a line: the only ones in the history,
# externals/SDL/src/video/directx/d3d12.h and
# externals/SDL/test/unifont-15.1.05.hex, are text.
ALLOWED_FILES = """
2d0333dbd273699a1513b8d2afaa40555bff986b externals/SDL/VisualC-GDK/logos/Logo100x100.png
046a8fb142c1bd3c161f5640e534c3215a348103 externals/SDL/VisualC-GDK/logos/Logo150x150.png
3ca25b565e0073f90af055eb44e449cf42aec262 externals/SDL/VisualC-GDK/logos/Logo44x44.png
11231500eb28e235b4b344ef60b4080c6bd38a52 externals/SDL/VisualC-GDK/logos/Logo480x480.png
def578f665e3ae62954bc864dd7f564641866681 externals/SDL/VisualC-GDK/logos/SplashScreenImage.png
d50bdaae06ee5a8d3f39911f81715abd3bf7b24d externals/SDL/android-project/app/src/main/res/mipmap-hdpi/ic_launcher.png
0a299eb3cc0273ad1fc260cf0b4a2c35f5d373f5 externals/SDL/android-project/app/src/main/res/mipmap-mdpi/ic_launcher.png
a336ad5c2baca59928871a9133e88435f31b402d externals/SDL/android-project/app/src/main/res/mipmap-xhdpi/ic_launcher.png
d423dac2624cf0b5dc90821a15362bc29e5a1e6b externals/SDL/android-project/app/src/main/res/mipmap-xxhdpi/ic_launcher.png
959c384b08eb45ef38765fecbfba3891c15132b8 externals/SDL/android-project/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png
4f8be2a82c65b65c08e85f96ab1b907856d04b9f externals/SDL/examples/asyncio/01-load-bitmaps/thumbnail.png
90880c9e4544826bd5afa597497eed33ea79a96e externals/SDL/examples/audio/05-planar-data/onmouseover.webp
3a040dfaef38a15faef6715e6bf25b4d0da53510 externals/SDL/examples/audio/05-planar-data/thumbnail.png
1f3ba4b20805627c4ba3f0ac1785a4ec3314f09e externals/SDL/examples/audio/onmouseover.webp
10bc6c830f2a43fbda985cc966739286a14ca687 externals/SDL/examples/audio/thumbnail.png
1d414e667147714794bc18e8d660d41c000d91df externals/SDL/examples/camera/01-read-and-draw/onmouseover.webp
98fe4e0754fd4840bb9fd69920177e3913c71a8d externals/SDL/examples/camera/01-read-and-draw/thumbnail.png
1757202c375d594b946bc55f3ccd782c30c22971 externals/SDL/examples/demo/01-snake/onmouseover.webp
f0e27c567516823028e791755c85af7a1a694ba3 externals/SDL/examples/demo/01-snake/thumbnail.png
2e7f44f9243178f9a7c6fb6f0eff887a7bfd3104 externals/SDL/examples/demo/02-woodeneye-008/onmouseover.webp
c8d1eface6900af048fbf226b848845a1beca056 externals/SDL/examples/demo/02-woodeneye-008/thumbnail.png
f522974770d54bf0f75104403dc3bc8368c1371b externals/SDL/examples/demo/03-infinite-monkeys/onmouseover.webp
418390be9064f82fc466814546d8ef2397b187f0 externals/SDL/examples/demo/03-infinite-monkeys/thumbnail.png
b99e7ddc46ff2705eb50955e1f76410fdac92454 externals/SDL/examples/demo/04-bytepusher/onmouseover.webp
891aa8f53223e45596a1df42e1a9a1588450f145 externals/SDL/examples/demo/04-bytepusher/thumbnail.png
484539c32ce1ce732c371d19e0f2a1fe1dfea6f9 externals/SDL/examples/input/01-joystick-polling/onmouseover.webp
4faebbad26b0d7f8d36e3e9c58e137486141a86e externals/SDL/examples/input/01-joystick-polling/thumbnail.png
08f40356f4220989b50424f3a6e9c999001226f1 externals/SDL/examples/input/02-joystick-events/onmouseover.webp
07f3ff153d9d14a9f0d4262f61b20420d19d3715 externals/SDL/examples/input/02-joystick-events/thumbnail.png
91c7bb0dcaaf1bebba5a1e8fdc7ed7cae0f89c82 externals/SDL/examples/input/03-gamepad-polling/onmouseover.webp
c5bc6e6431d09aeb419d0ba8053958f8d7c0cd10 externals/SDL/examples/input/03-gamepad-polling/thumbnail.png
b427739312f8104a60b84c2d94570beddaebb57a externals/SDL/examples/input/04-gamepad-events/onmouseover.webp
1c817a55780fba9a7ce91e8ffdfb4be7e4518c54 externals/SDL/examples/input/04-gamepad-events/thumbnail.png
a99b8d6f819c63d821f9904ceaeb7211d2b006ea externals/SDL/examples/misc/01-power/onmouseover.webp
970bdff6b00b53a7c5f5664d169301f58f952dcc externals/SDL/examples/misc/01-power/thumbnail.png
73476b475b41adfbf0f19baae157ddec8acf592b externals/SDL/examples/misc/02-clipboard/onmouseover.webp
76bc9d51cb71407901ae3c186267a520622612e8 externals/SDL/examples/misc/02-clipboard/thumbnail.png
f9c4d3dc98c9ae48592b6e661fe73a3dc2288ea4 externals/SDL/examples/pen/01-drawing-lines/onmouseover.webp
3403d3577f70b1c28a9a0657ca669e86d4fcac55 externals/SDL/examples/pen/01-drawing-lines/thumbnail.png
a0062fe0d9b984ea430310a8ea515f5de402b120 externals/SDL/examples/renderer/01-clear/onmouseover.webp
b255675da9db8b3cd28b5de85feb18640c1ac7fb externals/SDL/examples/renderer/01-clear/thumbnail.png
4ddf2ab86a17298feb41e3e9aa52cbba22db5687 externals/SDL/examples/renderer/02-primitives/thumbnail.png
5d3b3fce51f2ce636cd0cb3e1cd518b095365556 externals/SDL/examples/renderer/03-lines/onmouseover.webp
9d0ff10b9c5a15ee33a5a2a6a3671e9e43261eca externals/SDL/examples/renderer/03-lines/thumbnail.png
04582da55adf3d4958a48ab989e8113aebe664ed externals/SDL/examples/renderer/04-points/onmouseover.webp
56271136c991d1cdc3c20b1b373261d46559db6a externals/SDL/examples/renderer/04-points/thumbnail.png
cdfd376ee0e76519b3af2d8052120bb0efbf5c4d externals/SDL/examples/renderer/05-rectangles/onmouseover.webp
64e66882f96a90d91b3761bd8981976bc5aba9e0 externals/SDL/examples/renderer/05-rectangles/thumbnail.png
467afd85a413845285825251020c00e3070cf9b1 externals/SDL/examples/renderer/06-textures/onmouseover.webp
b33ba317db52eaeefd5589e139c88f8231bb14d8 externals/SDL/examples/renderer/06-textures/thumbnail.png
7c2969368e81889c45ff840f88d1c5c9575f6220 externals/SDL/examples/renderer/07-streaming-textures/onmouseover.webp
60c2a9f2acd01308aa067cf738c4ac01b687808e externals/SDL/examples/renderer/07-streaming-textures/thumbnail.png
69735cee846201ff90a734b8e443124a2555f4e1 externals/SDL/examples/renderer/08-rotating-textures/onmouseover.webp
12c51e17f5133f822448c5d2c7fb22d0720f6bf3 externals/SDL/examples/renderer/08-rotating-textures/thumbnail.png
bcc967c93fb2a9835ff359d792ec3f80f89e4d67 externals/SDL/examples/renderer/09-scaling-textures/onmouseover.webp
c0a24c20efcce8b7f14ec37f381834bbed2ba65d externals/SDL/examples/renderer/09-scaling-textures/thumbnail.png
37a518c316144d605e5af16ea49ba1a3f122e2e2 externals/SDL/examples/renderer/10-geometry/onmouseover.webp
89195fba89af7c0cbb9246531ee319dd6ea3753a externals/SDL/examples/renderer/10-geometry/thumbnail.png
2157063bc16ed81fa1e4e844fe302ef38bb5957b externals/SDL/examples/renderer/11-color-mods/onmouseover.webp
d471112868ea4c17ad2022beee648fb7cdf4fa50 externals/SDL/examples/renderer/11-color-mods/thumbnail.png
bad552148684e823dcd31cebbfd5f8913665d057 externals/SDL/examples/renderer/14-viewport/thumbnail.png
943eeef7a4f8f18da23ff65e8f1278cfa06a1028 externals/SDL/examples/renderer/15-cliprect/onmouseover.webp
127e6fa0954d1f71eb5fcf23374f5d983fe08d0e externals/SDL/examples/renderer/15-cliprect/thumbnail.png
bb4e5c4aebb9d5dc1cac49e903577f8a19d17f85 externals/SDL/examples/renderer/17-read-pixels/onmouseover.webp
8da02ac7687be6c5ae46bc433a2477ac32a472f8 externals/SDL/examples/renderer/17-read-pixels/thumbnail.png
f08e469187f419c2106c14e5ad8d0f4851e632aa externals/SDL/examples/renderer/18-debug-text/thumbnail.png
31e450d2e2a4e74aaa6ba1229b6f225ff1f9dee0 externals/SDL/examples/renderer/19-affine-textures/onmouseover.webp
a81265b884bee364ddaeead51f63fd37eee6cd5e externals/SDL/examples/renderer/19-affine-textures/thumbnail.png
202df9590186a530051a9c074db0a544a93f9304 externals/SDL/examples/template-placeholder.png
548abe8c1ea29d6a28fd94c1f29da043ded5b43c externals/SDL/src/hidapi/documentation/cmake-gui-drop-down.png
228838ff7cf8449e4c0fa1f47c12e297d9039eca externals/SDL/src/hidapi/documentation/cmake-gui-highlights.png
bb6b7bd5f9ed6c2ae2c91c202e5024b4811fc00b externals/SDL/src/hidapi/testgui/TestGUI.app.in/Contents/Resources/Signal11.icns
39e8f7cbaa13fe659017df38455c70ebb8bd8d7b externals/SDL/test/android/res/mipmap-hdpi/sdl-test.png
8f77eace9ed3481a7d62c9fe5b5964433541bc05 externals/SDL/test/android/res/mipmap-hdpi/sdl-test_round.png
06864eefb743adbf5a93370d116e070859a4d306 externals/SDL/test/android/res/mipmap-mdpi/sdl-test.png
1c3853620e8c982ace5c1788c1f72ae813d08a86 externals/SDL/test/android/res/mipmap-mdpi/sdl-test_round.png
9b16f171cc850ad6b1e2e04f0313a1e07cfac196 externals/SDL/test/android/res/mipmap-xhdpi/sdl-test.png
e64ff491846e5c2c043ccdaee97fd83965856e0b externals/SDL/test/android/res/mipmap-xhdpi/sdl-test_round.png
cb7d97712a6d1ac9cdf9527f9946ba4797fc29c5 externals/SDL/test/android/res/mipmap-xxhdpi/sdl-test.png
0eaa9ced6a2b063e7511461ac8133a2b451ef2bc externals/SDL/test/android/res/mipmap-xxhdpi/sdl-test_round.png
3e0713f18727b98066abb8b77810bc8532d51927 externals/SDL/test/android/res/mipmap-xxxhdpi/sdl-test.png
2a74e6bf208f1d76fea4cc6435032a80ef3858dc externals/SDL/test/android/res/mipmap-xxxhdpi/sdl-test_round.png
babb0b7b9a184cc556ed1de333ab0dc57a7ac2d2 externals/SDL/test/audiofile.png
c4f069ba503e44097a0b6d77d20edd0620a6fe6a externals/SDL/test/gamepad_axis.png
3648a8147ffb8ffb5232e3e2b4db106fef12d25c externals/SDL/test/gamepad_axis_arrow.png
426716972e20fb01546261364f82439030dd7348 externals/SDL/test/gamepad_back.png
a7a666380e3029e30d3e4315213ef7824bb18d7a externals/SDL/test/gamepad_battery.png
3fa9e8a8aedc7b3fad62946af2ba2cdf70cab95a externals/SDL/test/gamepad_battery_unknown.png
fde3b5a690b0591d21f6d127dee862b3fb7d7d65 externals/SDL/test/gamepad_battery_wired.png
e70d5f45d3f760eded485c4d8b277e85ee30a4b5 externals/SDL/test/gamepad_button.png
e82bf8e54dafd8ea73fc66b29116c6dcf6cb8c5c externals/SDL/test/gamepad_button_background.png
915e35629bbfae604bf59e41a916f2b02e77905a externals/SDL/test/gamepad_button_small.png
6840d2bb71a341409df9a4a22ca4dd3942a4c1a9 externals/SDL/test/gamepad_face_abxy.png
a1e69313125cd248d426577a2ff72a7e676200d7 externals/SDL/test/gamepad_face_axby.png
1672e579d6f8d14427bad19c4196adc48ceb66f7 externals/SDL/test/gamepad_face_bayx.png
bc0823ded129483d8baa4b2eb8e146a72f6cff76 externals/SDL/test/gamepad_face_sony.png
4fb52932d1afc222500dd438801131abb6cdd831 externals/SDL/test/gamepad_front.png
359342ef923375b8905d2023151ad5ef6946b2d2 externals/SDL/test/gamepad_touchpad.png
c3758b30abd4c53787d8fba1eab0b5165266de47 externals/SDL/test/gamepad_wired.png
0a94b1a0b329b87d7330ebfbe93c9ff785807fb3 externals/SDL/test/gamepad_wireless.png
14f55145cd36dee620ccd40d0c5225293305615b externals/SDL/test/glass.png
8bcb6e951a39d16d581033b7a8774485f0c1b497 externals/SDL/test/icon.png
f62ef23e0df2a45a1957c3b65510263881459d02 externals/SDL/test/icon2x.png
64f10c851373304e7c227470f8ac024ba5f88ca4 externals/SDL/test/logaudiodev.png
c28dd69a35fcce3a71d3467ffc1c077a2c18fb7b externals/SDL/test/msdf_font.png
fb3338dfab87de5ac4409fa8d3dc2a7405ee3764 externals/SDL/test/n3ds/logo48x48.png
95846b921d95ba308e2aef0c504d3668dde56aea externals/SDL/test/physaudiodev.png
375f01d700d11c80d90062a5b89bb70d7f079e32 externals/SDL/test/sample.png
002f8158151d600e52930f63b0e832508b6e2c90 externals/SDL/test/sample.wav
124ef9cb660961dd5ee8220cb491c30c531dbe55 externals/SDL/test/sdl-test_round.png
921d804b3ae93c3df315661a4351268fb2fa22d3 externals/SDL/test/soundboard.png
83e5acf2b08346c70798a9abeb3df02b9b382aee externals/SDL/test/soundboard_levels.png
40a803c5c63dc928ad8786c02f14794b8b9242b1 externals/SDL/test/speaker.png
193cf5432044cd03f3d7289dd0d8a8e1809b1c7c externals/SDL/test/sword.wav
d845b2c6d5343bd37fdce76827409a263b0b3339 externals/SDL/test/testyuv.png
e81f993c8f5608551e1841d1cafaed36c2a5c999 externals/SDL/test/trashcan.png
"""
ALLOWED = {tuple(line.split(' ', 1)[::-1]) for line in ALLOWED_FILES.split('\n') if line}
ALLOWED_PREFIX = 'externals/SDL/'

FIX = ('remove the file from the history of your branch; never commit game data'
       ' or content made by other people - see CONTRIBUTING.md')
HOW_TO_FIX = textwrap.fill(
    'How to fix: %s. A later commit that deletes the file is not enough, because'
    ' every commit is checked. For example: git rebase -i <base branch>, drop or'
    ' edit the commits named above, then push the branch again with'
    ' --force-with-lease.' % FIX, 78, break_long_words=False, break_on_hyphens=False)

CONTROL = re.compile(r'[\x00-\x1f\x7f]')


class Failure(Exception):
    """A usage or git error: the check cannot say "clean", so it fails."""


# Every git call: replace refs must not hide or swap objects.
ENV = dict(os.environ, GIT_NO_REPLACE_OBJECTS='1')


def git(*args):
    r = subprocess.run(('git',) + args, capture_output=True, env=ENV)
    if r.returncode != 0:
        raise Failure('git %s failed: %s' % (args[0], r.stderr.decode('utf-8', 'replace').strip()))
    return r.stdout


def resolve(rev):
    """The commit a name or object id stands for, or None if it is not here."""
    r = subprocess.run(['git', 'rev-parse', '--verify', '--quiet', '--end-of-options',
                        rev + '^{commit}'], capture_output=True, env=ENV)
    return r.stdout.decode('ascii').strip() if r.returncode == 0 else None


def commits(head, base):
    """The commits to check, oldest first: all of head's history, or only
    those that base does not have."""
    args = ['rev-list', '--topo-order', '--reverse', head]
    if base:
        args.append('^' + base)
    return git(*args, '--').decode('ascii').split()


def entries(commit):
    """(path, type, object id, size) for every file in the commit's tree."""
    for rec in git('ls-tree', '-r', '-l', '-z', '--full-tree', commit).split(b'\0'):
        if rec:
            meta, path = rec.split(b'\t', 1)
            _mode, kind, oid, size = meta.decode('ascii').split()
            path = path.decode('utf-8', 'surrogateescape')
            if kind == 'blob' and size == 'BAD':
                raise Failure('object %s (%s) is missing from this clone' % (oid, shown(path)))
            yield path, kind, oid, size


def is_binary(oid):
    """Only asked for files larger than LIMIT, so a short read is an error."""
    p = subprocess.Popen(['git', 'cat-file', 'blob', oid], env=ENV,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    head = p.stdout.read(PROBE)
    p.stdout.close()
    p.kill()
    p.wait()
    if len(head) < PROBE:
        raise Failure('cannot read file content %s' % oid)
    return b'\0' in head


def allowed(path, oid):
    """True for a file of the SDL copy that ALLOWED lists with this content."""
    return path.startswith(ALLOWED_PREFIX) and (path, oid) in ALLOWED


def forbidden_type(path, oid):
    """The reason if the file type is forbidden, or None. oid is the blob id
    of the content (None if not known: then no ALLOWED entry matches)."""
    name = TRAILING.sub('', path.rsplit('/', 1)[-1].lower())
    dot = name.rfind('.')
    if dot < 0 or name[dot:] not in TYPES:
        return None
    what = TYPES[name[dot:]]
    if what in LIFTABLE and allowed(path, oid):
        return None
    return 'forbidden file type %s (%s)' % (name[dot:], what)


def judge(path, kind, oid, size, binary):
    """Why the file is forbidden, or None."""
    reason = forbidden_type(path, oid)
    if reason:
        return reason
    if kind == 'blob' and int(size) > LIMIT:
        if allowed(path, oid):
            return None
        if oid not in binary:
            binary[oid] = is_binary(oid)
        if binary[oid]:
            return 'binary file larger than 1 MB (%s bytes)' % size
    return None


def check(todo):
    """{(path, reason): [commits]} for everything forbidden in the commits."""
    verdict = {}
    binary = {}
    found = {}
    for commit in todo:
        for path, kind, oid, size in entries(commit):
            if (path, oid) not in verdict:
                verdict[path, oid] = judge(path, kind, oid, size, binary)
            reason = verdict[path, oid]
            if reason:
                found.setdefault((path, reason), []).append(commit)
    return found


def shown(text):
    """Text as it is printed: control characters escaped, so a file name or a
    commit subject can neither break the report nor pass for a runner
    command (every printed line also starts with fixed text)."""
    return CONTROL.sub(lambda m: '\\x%02x' % ord(m.group(0)), text)


def label(commit):
    """Short commit id and subject."""
    return shown(git('log', '-1', '--format=%h %s', commit, '--')
                 .decode('utf-8', 'replace').strip())


def annotate(text):
    """An error annotation on the run page when running on GitHub."""
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        text = text.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
        print('::error title=Content guard::' + text)


def report(found, todo, what):
    if not found:
        print('content_guard: %s: %d commit(s) checked, clean' % (what, len(todo)))
        return 0
    print('content_guard: FAILED - %s: forbidden content in %d of %d checked commit(s):'
          % (what, len({c for cs in found.values() for c in cs}), len(todo)))
    print()
    for (path, reason), cs in sorted(found.items()):
        names = [label(c) for c in cs[:5]]
        print('  - %s' % shown(path))
        print('      %s' % reason)
        for name in names:
            print('      in commit %s' % name)
        if len(cs) > 5:
            print('      ... and in %d more commit(s)' % (len(cs) - 5))
        annotate('%s: %s, in commit %s - %s' % (shown(path), reason, names[0], FIX))
    print()
    print(HOW_TO_FIX)
    return 1


def from_stdin():
    """The --stdin check: one line "<path><TAB><reason>" for every forbidden
    file in the list on standard input, exit code 1 if there is one."""
    binary = {}
    found = 0
    for line in sys.stdin.buffer.read().split(b'\n'):
        if line:
            kind, oid, size, path = line.decode('utf-8', 'surrogateescape').split(' ', 3)
            if kind == 'blob' and size == 'BAD':
                raise Failure('object %s (%s) is missing from this clone' % (oid, shown(path)))
            reason = judge(path, kind, oid, size, binary)
            if reason:
                print('%s\t%s' % (shown(path), reason))
                found += 1
    return 1 if found else 0


def from_github():
    """What this run checks, or None if the push deleted a branch or tag."""
    event = os.environ.get('GITHUB_EVENT_NAME', '')
    if event not in ('push', 'pull_request'):
        raise Failure('unsupported event "%s" (push and pull_request only)' % shown(event))
    path = os.environ.get('GITHUB_EVENT_PATH', '')
    if not path:
        raise Failure('--github needs GITHUB_EVENT_NAME and GITHUB_EVENT_PATH')
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise Failure('unexpected event file %s' % path)
    ref = shown(str(data.get('ref') or os.environ.get('GITHUB_REF', '')))
    if event == 'push' and data.get('deleted') is True:
        print('content_guard: push to %s deletes the ref - nothing to check' % ref)
        return None
    return 'history of HEAD (%s, %s)' % (event.replace('_', ' '), ref)


def main(argv):
    try:
        sys.stdout.reconfigure(errors='backslashreplace')
    except AttributeError:
        pass
    ap = argparse.ArgumentParser(description='No game data in any commit.')
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--history', metavar='REV', help='every commit reachable from REV')
    mode.add_argument('--range', nargs=2, metavar=('BASE', 'HEAD'),
                      help='every commit in HEAD that is not in BASE')
    mode.add_argument('--github', action='store_true',
                      help='the automatic check: --history HEAD for a push or pull request')
    mode.add_argument('--stdin', action='store_true',
                      help='the files listed on standard input (tools/git-hooks/pre-push)')
    args = ap.parse_args(argv)
    out = sys.stderr if args.stdin else sys.stdout
    try:
        base = None
        if args.stdin:
            return from_stdin()
        if args.github:
            what = from_github()
            if what is None:
                return 0
            head = 'HEAD'
        elif args.range:
            base, head = args.range
            what = '%s..%s' % (base, head)
        else:
            head = args.history
            what = 'history of %s' % head
        if git('rev-parse', '--is-shallow-repository').strip() != b'false':
            raise Failure('this clone is shallow, so its history is incomplete '
                          '(check out with fetch-depth: 0)')
        head_id = resolve(head)
        if not head_id:
            raise Failure('%s is not a commit in this clone' % head)
        base_id = None
        if base is not None:
            base_id = resolve(base)
            if not base_id:
                raise Failure('%s is not a commit in this clone' % base)
        todo = commits(head_id, base_id)
        return report(check(todo), todo, shown(what))
    except (Failure, OSError, ValueError, KeyError) as e:
        print('content_guard: ERROR - %s' % shown(str(e)), file=out)
        print('content_guard: the check could not run, so it fails.', file=out)
        annotate('the check could not run: %s' % shown(str(e)))
        return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
