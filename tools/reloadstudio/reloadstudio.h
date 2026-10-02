// reloadstudio.h - shared interface of Reload Studio
//
// Reload Studio is the front end for track and character authors: pack a
// track folder into a .rldtrack container, put together cups for cups.txt,
// build a .rldchar character from a PLY or OBJ model, test a track in the game. All
// texts on screen are English.
//
// GROUND RULE: converting and checking is done by rldpack make. Reload Studio carries
// rldpack.c inside (rs_rldpack.c) and starts itself as a child process with
// `--rldpack <arguments>`. It reads its machine lines (below) and shows
// them. It checks NOTHING itself: no modes, no model ID, no ABR,
// no music. It assigns no level ID and never touches track-ids.tsv.
//
// Files:
//   rs_shell.c    window, sidebar, fonts, colours (light/dark),
//                 cards, buttons, message list, dialogs, settings,
//                 child processes, automation (--do) and screenshot (shot)
//   rs_rldpack.c  rldpack.c with a renamed main
//   rs_track.c    page "Track"
//   rs_cups.c     page "Cups"
//   rs_char.c     page "Character"
//   rs_view.c     3D model view beside the reference dummy (window class RsModelView, rs_view.h)
//   rs_wheels.c   card "Wheels" of the page "Character" (preview feature, rs_wheels.h)
//   rs_anim.c     card "Animations" of the page "Character" (preview feature, rs_anim.h)
//   rs_test.c     page "Test in game"
//
// Command line (details in rs_shell.c): `--rldpack <arguments>` (first
// argument: the rldpack face), `--do "<verb> <argument>"` (automation, any
// number of times), `--log <file>` (automation log), `--theme dark|light|system`,
// `--settings <ini>` (settings, logs and temporary files only there - below at
// Rs_ConfigGet), `--ui-scale <percent>` (scale of the window instead of the
// monitor's), `--screen <w>x<h>` (lay the window out as on a screen of that
// size), `--enable-preview-features` (unlock the fields marked "Coming soon",
// g_rsPreviewFeatures below; never stored), `--help`.
//
// Characters: UTF-16 in the front end (W functions), UTF-8 on the pipe to
// rldpack and in files. The manifest sets the ANSI code page to UTF-8,
// so that rldpack (fopen, argv) understands paths with umlauts.

#ifndef RELOADSTUDIO_H
#define RELOADSTUDIO_H

// As in rldpack.c: wcsncpy, _wfopen and co. without MSVC's _s warnings.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Version and build ID
//
// CMakeLists.txt sets the version, the build ID comes from ctr_build_id.h,
// generated on every build (cmake/CtrBuildId.cmake) - both as for the
// game (narrow strings). The game answers --version with
// "CTR Reload <version> (<build id>)"; the
// test page demands the same ID as here, otherwise game and
// Reload Studio do not belong together. "unknown" (build without git) does not check.
// ---------------------------------------------------------------------------
#include "ctr_build_id.h"
#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "Beta 0"
#endif
#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif
#define RS_WIDEN2(x) L##x
#define RS_WIDEN(x)  RS_WIDEN2(x)
#define RS_VERSION_W  RS_WIDEN(CTR_NATIVE_VERSION)     // L"Beta 0"
#define RS_BUILD_ID_W RS_WIDEN(CTR_NATIVE_BUILD_ID)    // L"<12 hex>" or L"<12 hex>-dirty-<6 hex>"

// Message when there is no game data next to the game (pages Track and Test).
#define RS_TEXT_NO_GAME_DATA \
    L"CTR Reload has no game data yet. Start ctr_native.exe once and drag your own " \
    L"Crash Team Racing disc image (NTSC-U, .cue/.bin) onto its window. It unpacks " \
    L"the data next to the game; then come back here."

// ---------------------------------------------------------------------------
// The pipe from rldpack (protocol 1)
//
// With `--machine` rldpack writes machine lines to stdout in addition to its
// normal lines. Each starts with '@', a TAB separates the fields.
// No field contains a TAB or line end. The front end skips unknown kinds,
// missing fields are empty.
//
//   @rldpack  <protocol=1>  <command>                  always the first line
//   @file     <kind> <state> <file name> <bytes>
//             kind: lev vrm sca sndb tracktxt (sndb only if there is a .sndb in the
//             folder; sca then only if there is also a .sca)
//             state: ok | missing | extra (more than one) | unused (.sca/.sndb
//             with --no-music)
//   @value    <key> <value> <origin>
//             key: name author track_version modes reverb bots ambient
//                  music out (with info additionally format)
//             origin: switch | track.txt | default | folder | container
//             modes: words with commas, "race,time"
//             reverb: "0".."4" | "off" | "" (not set, game default 2)
//             bots:   "0".."17" | ""      (not set, game default 0)
//             ambient: "0x83,0x0" | ""   (not set, no sound)
//             music:  "on" | "off" (--no-music) | "none" (no .sca/.sndb)
//   @music    <ok | none | off | error> <text>
//   @mode     <mode> <data: yes|no> <declared: yes|no> <playable: yes|no> <reason>
//             mode: race time ctr crystal battle - always all five, in this
//             order. reason: English, for authors, only when data = no.
//   @lev      <fact> <value>
//             restart_points, nav_paths (0..3, paths with more than one point),
//             nav_points ("103,93,84", -1 = no path), start_spots (distinct
//             start positions out of 8, without 0,0,0), spawn_count, model_ids_fixed,
//             ambient_place_1 / ambient_place_2 ("yes" | "no"),
//             crystals, letters ("<C>,<T>,<R>" model counts),
//             map ("fits" | "scaled" | "none"), instances
//   @msg      <severity> <id> <text> <technical>
//             severity: error | warning | note | info
//             text: English, understandable without knowing the code
//             technical: the line as rldpack otherwise prints it (may be empty)
//   @result   <ok | failed | checked> <output path> <bytes> <sha256>
//             checked = --check without errors: this is how it would be built, nothing written
//   @container <file> <ok | refused> <reason>          (info, per file)
//   @end      <exit code>                              always the last line
//
// Commands the front end calls:
//   make <folder> --machine --check [switches]   check everything, write nothing
//   make <folder> --machine [switches] --out <f> build
//   info --machine <file> [<file> ...]           per file one block from @container:
//                                                @value name, author,
//                                                track_version, modes, format;
//                                                @mode x5; @lev as for make,
//                                                without model_ids_fixed, map
//                                                and instances
// Switches of make that the front end sets: --name --author --track-version
// --modes --reverb --bots --ambient --no-music --out. For reverb, bots and
// ambient the value "default" means: do not set, even if track.txt has one.
//
// make-char (page "Character") speaks the same protocol. It writes no @finding
// lines; its findings are @msg. Order: @rldpack, the @value block of the
// switches, the icon lines (with --icon), the voice lines (with --voices),
// @file ply (an OBJ: @file obj, then @file mtl per material file), the @model
// lines, @msg of reading the model, @value size-range, @msg of the chain,
// @char, @file preview and @value kart-box (with --preview and a built model),
// @result, @end.
//
//   @rldpack  1  make-char
//   @value    <key> <value> <origin>
//             origin: switch | default | template (class without --class)
//             key: name (in capitals, as written), author, char_version,
//                  template (0..14, -1 = not usable), class (0 balanced,
//                  1 acceleration, 2 speed, 3 turning, -1 = not usable), poses
//                  (auto | still), colors (64 | 128), up (y | z), forward
//                  (z | -z), scale, two_sided (yes | no), out ("none" with
//                  --diagnose), size (the --size percent used, 100 when
//                  missing or invalid), icon,
//                  voices ("" when not given), fit (crash | none), reduce
//                  (auto | off), wheels (on | off), repair (auto | off),
//                  open-parts (two-sided | one-sided), remesh (on | off),
//                  mask (aku | uka, "none" when the template is not usable;
//                  origin switch with --mask aku|uka, otherwise template),
//                  map-color (RRGGBB | template; origin switch with
//                  --map-color, otherwise template)
//   @value    size-range <lo> <hi>          (no origin)
//             the --size percentages this model takes, whole numbers inside
//             50..200, the run around 100; "0 0" = no size fits (the model
//             itself is too large). For every model whose parts were found.
//   @value    kart-box <x0> <y0> <z0> <x1> <y1> <z1>   (no origin, --preview only)
//             the model's kart in the neutral frame, in the units of the
//             preview file; the kart is never scaled by --size
//   @value    retail-kart <x0> <y0> <z0> <x1> <y1> <z1>   (no origin)
//             the fixed box of a retail kart in the same units; the page only
//             reports it - the preview draws the reference dummy of make-char
//             (tools/rldpack_dummy.inc) under the model instead
//   @value    crash-box <x0> <y0> <z0> <x1> <y1> <z1>   (no origin)
//             Crash with his kart (retail racer model, birth pose) in the same
//             units, with ONE DECIMAL ("-33.8"); --fit crash fits the model to
//             it, the page outlines it about the model
//   @file     <kind> <state> <name> <bytes>
//             ply           ok | missing          the model, a PLY
//             obj           ok | missing          the model, an OBJ (instead of ply)
//             mtl           ok | missing | bad    a material file of the OBJ (the page reads
//                                                 @model mtl instead)
//             icon          ok | missing | bad    --icon (bad: not a PNG rldpack reads)
//             icon-original ok | failed           <prefix>-original.bmp, the PNG as read
//             icon-preview  ok | failed           <prefix>-icon.bmp, 43 x 25 as the menu tile
//                                                 shows it (the CICN chunk stays 44 x 26)
//             voice         ok | bad | unused | unknown | ignored | cut   per file of the folder
//             (the page reads @voice below instead)
//             preview       ok | failed           --preview (failed: warning, build unaffected)
//             The BMPs are 32 bit BI_RGB, bottom-up, B G R A (A = 0 transparent).
//   @char     <fact> <value> [<detail>]       for the display, not in the file:
//             vertices faces quads triangles, parts <count> <"kart N, driver N,
//             steering wheel N">, size <length> <width> <height> <bottom>
//             (Blender units, after --size), scale <x> <y> <z>, colors_in,
//             colors_out, color_error <mean> <max>, part_colors <group> <ranges>,
//             records, slots, frames, draw_bytes, draw_delta, icon_source <w> <h>,
//             icon_crop <x> <y> <w> <h>, icon_scaled <w> <h> (the texels the cut
//             was scaled to: 43 x 25, or 44 x 26 for a picture of exactly that
//             size), icon_colors, icon_opaque (of 1144),
//             voices <clips> <events filled>: the clips packed as CVOI and how
//             many of the ten events have one (with --voices),
//             fit <factor> <length before> <after> <height before> <after>
//             <kart | model>: the --fit crash factor (4 significant digits,
//             %.4g), the lengths and heights in game units (1 decimal; below 1
//             3 significant digits, %.3g - a model in millimeters); kart = the
//             model's kart was matched to Crash's, model = the whole model (no
//             kart found, or --wheels off),
//             reduced <triangles before> <after> <draw bytes before> <after>:
//             only when --reduce auto lowered the triangle count,
//             dummy <shift x> <y> <z> <seat x> <y> <z> <wheel 0 | 1>: only with
//             --fit crash when the model was placed onto the reference dummy;
//             shift = added to every position, seat = the pivot of --size and
//             the poses (game units, %.2f), wheel 1 = it has a steering wheel,
//             repaired <welded> <degenerate> <duplicate> <flipped> <holes
//             closed> <hole triangles> <holes left> <open edges before>
//             <after> <cracks split>: with --repair auto (the default),
//             two-sided <n>: triangles of parts still open after the repair,
//             drawn from both sides at no extra cost (--open-parts two-sided,
//             the default); only the open parts,
//             two-sided-packing <n>: triangles that turn over by the packing in
//             some pose and are therefore drawn from both sides (always next to
//             two-sided),
//             remeshed <triangles of the source> <of the hulls> <after the
//             reduction> <open edges before> <after>: only with --remesh on
//   @model    what rldpack read of the model, after @file ply | obj:
//             format <ply | obj>     the format, told by the content (the file
//                                    ending only has to agree: model-misnamed)
//             mtl <ok | missing | none | bad> <path | ->   per mtllib of an OBJ;
//                                    none = the OBJ names no material file
//             texture <ok | missing | unreadable | unsupported> <material> <path>
//                                    per map_Kd of an OBJ (PNG, JPG, TGA)
//             group <faces> <name>   per o and g of an OBJ, in the order of the
//                                    file ("-" = no name, at most 1024 lines,
//                                    the rest only counted: info obj-groups);
//                                    only for information
//             colors <vertex | material | texture | grey | mixed>   where the
//                                    colours came from
//             The page shows them on the tab Model: a line (the format, the
//             MTL, the textures found, the groups, the colours) for every OBJ
//             and for a model whose file is not named .ply, and below it a list
//             of the mtl, texture and group lines (at most 1024 of each kind;
//             the line counts them all, with the groups of obj-groups) where
//             there is more than an OBJ without an MTL.
//   @voice    <file> <state> <event | -> <ms> <bytes> <rate> <channels> <peak> <preview | ->
//             with --voices, one per file of the folder, by name: state ok |
//             bad | unknown | ignored | unused | cut (packed, but cut to the
//             longest length); event the key it fills (below), - = none; ms the
//             length of the sound in the file; bytes the size of the file; rate and
//             channels as in the file; peak in per mille of full scale (ms, rate,
//             channels and peak 0 for a file that was not read); preview
//             the WAV of --voice-preview (22050 Hz mono as the game hears it),
//             - = none
//   @voiceevent <event> <clips>   with --voices, always all ten, in the order
//             boost hit spin bigair drop shield passing fire short-yes short-hit
//             (the events of CVOI, s_rldCharVoiceEvents in include/rldchar.inc)
//   @msg      as above. ids the page reads itself: char-size (error,
//             "Size N% is outside lo..hi% for this model: <why>. Choose a size in
//             that range."). Others of make-char, shown as they come: icon-file,
//             icon-roundtrip, icon-small, icon-empty, icon-colors, icon-preview,
//             voice-folder, voice-other, voice-unknown, voice-double, voice-wav,
//             voice-vag, voice-stereo, voice-silent, voice-clip,
//             voice-length-line, voice-length-short, voice-source, voice-same,
//             voice-missing, voice-too-many (error: more than 4 clips for an
//             event), voice-assign (error: --voice for a file that is no voice
//             or not in the folder), voice-preview (a --voice-preview file could
//             not be written), voice-pack (error: the packed CVOI fails
//             RldChar_CheckVoices - an internal error),
//             - all of them only with --voices; the page shows every voice-*
//             under the tab Voices -
//             preview, model-reduced (info, with every reduction),
//             model-reduce-fallback (info, the reduction reached its target only
//             without some of its guards), model-repaired (info),
//             model-remeshed (info), model-quant-flip (note), and the
//             ply-*, model-*, name*, usage ... of before; reading the model
//             adds the errors model-unknown, model-unsupported (a known other
//             format, named), model-open, model-ply-only, obj-syntax, obj-index,
//             obj-number, obj-face, obj-empty, obj-big (with the line), the
//             warnings model-misnamed, mtl-missing, mtl-bad, mtl-material,
//             tex-missing, tex-unreadable, tex-unsupported, tex-no-uv,
//             obj-no-colors, obj-color-range and the infos obj-ignored,
//             obj-vertex-colors, obj-groups, tex-found-nearby, tex-alpha. The
//             page puts every id that does not start with
//             icon, voice, name, mask, pose or wheel- (and is not one of
//             author, class, template, map-color, write) under the tab Model:
//             a click on one of these opens it; an error stops the build as
//             every error does
//   @result   <ok | failed | checked> <output path> <bytes> <sha256>
//             with --icon the container has a third chunk CICN (612 bytes), with
//             --voices and at least one clip a chunk CVOI (the voices,
//             include/rldchar.inc; the order of the chunks in docs/CONTAINER_FORMAT.md)
//   @end      <exit code>
//
// info --machine <file.rldchar> (rldpack; the page does not call it yet):
//   @container <file> <ok | refused> <reason>
//   @value    name, author, char_version, template, class, wheels (on | off),
//             flags (0x%08x, only when the CHRI flags carry bits this rldpack
//             does not know), format - all with origin "container"
//   @char     triangles, records, colors_out, frames, draw_bytes,
//             verdict <word> <rule> <detail>, icon <state> <why>
//
// Commands the page "Character" calls (always --template 14, Fake Crash):
//   make-char --machine --check --model <ply | obj> --name <n> --template 14
//             --class <balanced|acceleration|speed|turning> --size <percent>
//             [--repair off] [--open-parts one-sided] [--remesh on]
//             [--reduce off] [--wheels off] [--mask aku|uka]   (only when they
//             differ from the defaults --repair auto, --open-parts two-sided,
//             --remesh off, --reduce auto, --wheels on, --fit crash, the mask of
//             the template); --remesh on is passed
//             only with --reduce auto (the page greys the option out otherwise)
//             [--icon <png> --icon-preview <prefix>]
//             [--voices <dir> [--voice-normalize] [--voice <file>=<event|none> ...]
//             --voice-preview <prefix>]   (all of them only with a folder; without
//             one the command is that of a driver without voices. --voice-normalize
//             while "Normalize volume" is ticked, the default; one --voice per file
//             given an event of its own; event: boost hit spin bigair drop shield
//             passing fire short-yes short-hit, none = left out)
//             [--map-color RRGGBB]   (only when a colour was chosen)
//             [--up z] [--forward -z]   (when the card Import of the tab Extras
//             says Up Z or Forward -Z; +Y up and +Z forward are the defaults)
//             [--colors 64]   (when the card says Colors 64; 128 is the default)
//             [--vertex-colors modulate|color]   (an OBJ only, when the card
//             Import of the tab Extras does not say Auto)
//             --preview <file> [--out <f>]      check; writes only the preview files
//   make-char --machine --model ... --out <f>  build: the same switches without
//             --check, --preview, --icon-preview and --voice-preview
//   make-char --machine --check --model <temp folder>\char-<pid>-0-none.ply
//             --name <n> --template 14 --class <c> --mask <m> [--map-color RRGGBB]
//             --out <f>   when only name, class, mask, minimap colour or output
//             differ from a result the page keeps (the same model, switches and
//             files): the model file is never written, so rldpack reports these
//             switches and stops at the model; the page takes their @msg and
//             @value lines from this run and every other line from the result
//             kept (rs_char.c, Char_MetaDone). A check whose command and files
//             are those of a result kept runs nothing (Char_CacheFind).
// The raw output of the page ("Show rldpack output") starts with the command,
// "ReloadStudio.exe --rldpack make-char ...", quoted as it was passed.
//
// Automation verbs of the page "Character" (--do, besides those of the shell):
//   model <ply|obj|none>, drop <path>[|<path>...] (as dropping these files
//   onto the page together: a folder is the voices, a .ply or .obj the model,
//   a .png the icon only when no .obj came with it, another 3D file
//   the model only when neither came with it),
//   name <text>, class <word>, mask aku|uka, mapcolor
//   template|RRGGBB, size <percent>, icon <png|none>, icon-fit fit|fill|none,
//   icon-transparent on|off, icon-frame on|off, repair|open-parts|remesh|reduce|
//   wheels on|off, reduce-to-fit, out <file|none>, check, build, pose
//   neutral|left|right, turn <degrees>, tab <1..5|name>, extras
//   import|wheels|animations, up y|z, forward z|-z, colors 128|64, vertex-colors
//   auto|modulate|color (the card Import; the last passed for an OBJ only),
//   problem <n> (as a click on message n), report <file>, the verbs of the cards
//   Wheels and Animations (rs_wheels.c, rs_anim.c), and for the tab Voices:
//   voices <folder|none>          the folder (the check follows)
//   voice <file>=<event|none>     as choosing the file in the list and its event
//                                 (the file must be in the list of the last check)
//   voicenorm on|off              "Normalize volume"
//   voiceplay <file>              as choosing the file and pressing Play; in
//                                 automation only logged (preview path, rate,
//                                 channels, frames), never played
// The --preview file (little endian): "RLDPV1\0\0", u32 poses = 3 (turn frame 10
// neutral, frame 0 full steer left, frame 20 full steer right), per pose u32
// triangles and per triangle 3 corners of s16 x, y, z (game units, +Y up, +Z
// forward, +X the driver's left), u8 r, g, b, u8 pad (bit 0: drawn from both
// sides). Corners counter-clockwise seen from the side the game draws.
//
// char-poses (the card "Animations" of the page "Character", rs_anim.c; only
// with --enable-preview-features) - poses of one's own for the preview, never a
// container (tools/rldpack_anim.inc, details there):
//   char-poses --machine --model <ply> --pose-dir <folder> --size <percent>
//              [--wheels off] [--repair off] --preview <file>
//   @rldpack  1  char-poses
//   @value    pose-dir <folder> switch, then size, fit, wheels, repair, up and
//             forward as make-char says them
//   @file     ply ok | missing <model> <bytes>
//   @msg      as make-char; own ids pose-*, pose-model, pose-dir, pose-folder
//   @char     pose <name> <ok | missing | bad | unused | automatic> <file> <rule>
//             six, in the order turn_left turn_right reverse bump jump idle
//   @char     pose-unknown <file>                 per .ply that is no pose
//   @char     poses <found> <shown>
//   @file     preview ok | failed <name> <bytes>
//   @end      <exit code>   (no @result: nothing is built)
// The --preview file (little endian): "RLDPS1\0\0", u32 poses = 6 (in the order
// above, 0 triangles for a missing or refused one), per pose u32 triangles and
// the triangle records of RLDPV1 (the faces before repair and reduction, drawn
// from both sides).
//
// char-wheel (the card "Wheels" of the page "Character", rs_wheels.c; only with
// --enable-preview-features) - a wheel of one's own for the preview, never a
// container (tools/rldpack_wheel.inc):
//   char-wheel --machine --model <ply> --preview <file>
//   @rldpack  1  char-wheel
//   @value    model, up, forward, repair, reduce, preview  <value> <origin>
//   @file     ply ok | missing <name> <bytes>
//   @msg      as make-char; own ids: wheel-tris (error, more than 128 triangles
//             after the reduction), wheel-flat (error), wheel-off-axis (warning)
//   @char     repaired (as make-char), fit <factor> <across before> <after>,
//             reduced <before> <after>, wheel <triangles> <across> <wide> <two-sided>
//   @value    wheel-size <across> built
//   @file     preview ok | failed <name> <bytes>
//   @end      <exit code>   (no @result: nothing is built)
// The --preview file (little endian): "RLDPW1\0\0", u32 triangles, per triangle
// 3 corners as in RLDPV1 but in 1/16 game units, wheel-local: the axle centre in
// the origin, the axle along X, the outer side +X, 32 game units across.
// ---------------------------------------------------------------------------

#define RS_PROTOCOL 1

// Severity of a message, for the message list and the colour of labels.
enum RsSeverity {
    RS_SEV_OK = 0,      // green: built, present
    RS_SEV_INFO,        // grey: for information only
    RS_SEV_NOTE,        // blue: note, e.g. "not playable in CTR Reload yet"
    RS_SEV_WARNING,     // amber: builds, but take a look
    RS_SEV_ERROR,       // red: does not build
    RS_SEV_COUNT
};

// "@msg" severity from rldpack -> enum. Unknown -> RS_SEV_INFO.
int Rs_SeverityFromText(const wchar_t *text);

// ---------------------------------------------------------------------------
// Pages
//
// Every page is a child window of the class the shell provides. The shell
// paints background and cards, colours labels, paints main buttons and
// passes everything else on to the page's callbacks. A page creates its
// controls in create() and positions them in layout().
//
// The page window is the whole page, not only the part that is visible: the
// shell puts it into a scrolling view. It is as large as the view, but never
// smaller than the page's minimum size (minWidth, minHeight) and never smaller
// than what layout() placed - a control or card that ends below or right of
// the page makes the page that much larger (plus the usual margin of 24 px at
// the bottom and 32 px at the right), with scroll bars, the mouse wheel and the
// keyboard focus (Tab) scrolling it into view. So a layout fills the size it is
// given, as before, and needs no scroll code.
// ---------------------------------------------------------------------------

// The visible part of the page in the scrolling view, in pixels: at most the
// page's own size. Valid inside layout() (the shell knows it before), e.g. to
// keep a preview no taller than what can be seen at once.
void Rs_PageViewSize(HWND page, int *w, int *h);

// The lower edge of this page's own heading at the page width w: below its
// subtitle, or below its title when the subtitle is "". Rs_PageTop is the same
// line for every page (the deepest heading of all); a page without a subtitle
// may start its cards higher, at this line plus a margin.
int Rs_PageHeadBottom(HWND page, int w);

// Minimum size of a page in 96-dpi pixels when its definition says 0.
#define RS_PAGE_MIN_W 964
#define RS_PAGE_MIN_H 760

// Return values of automate().
enum RsAuto {
    RS_AUTO_UNKNOWN = 0,   // verb does not belong to this page
    RS_AUTO_DONE,          // done
    RS_AUTO_WAIT,          // started; the shell waits until busy() says 0
    RS_AUTO_FAIL           // did not work; the shell notes it and continues
};

struct RsPageDef {
    const wchar_t *navName;    // entry in the sidebar, e.g. L"Track"
    const wchar_t *title;      // heading of the page
    const wchar_t *subtitle;   // one line below it

    // Create controls. page is the page window.
    void (*create)(HWND page);

    // Position controls. w/h in pixels of the page (without the heading - the
    // shell paints that above Rs_PageTop()). Report the cards for this pass
    // with Rs_CardClear/Rs_CardAdd.
    void (*layout)(HWND page, int w, int h);

    // WM_COMMAND and WM_NOTIFY of the children. Return value as for the window procedure.
    LRESULT (*command)(HWND page, WPARAM wParam, LPARAM lParam);
    LRESULT (*notify)(HWND page, NMHDR *hdr);

    // All other messages to the page window, especially RS_WM_JOB_LINE,
    // RS_WM_JOB_DONE, WM_TIMER and RS_WM_PAGE_SHOWN. *handled = 1 if the
    // page processed them.
    LRESULT (*message)(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled);

    // Automation: a verb with an argument (rest of the line, may be empty).
    int (*automate)(HWND page, const wchar_t *verb, const wchar_t *arg);

    // 1 as long as the page is waiting for a child process.
    int (*busy)(HWND page);

    // Smallest size of the page in 96-dpi pixels (the shell scales it): below
    // it the view scrolls instead of handing layout() less. 0 = RS_PAGE_MIN_W,
    // RS_PAGE_MIN_H. A definition that ends with busy keeps the defaults.
    int minWidth;
    int minHeight;
};

extern const struct RsPageDef g_rsTrackPage;   // rs_track.c
extern const struct RsPageDef g_rsCupsPage;    // rs_cups.c
extern const struct RsPageDef g_rsCharPage;    // rs_char.c
extern const struct RsPageDef g_rsTestPage;    // rs_test.c

enum RsPageId { RS_PAGE_TRACK = 0, RS_PAGE_CUPS, RS_PAGE_CHAR, RS_PAGE_TEST, RS_PAGE_COUNT };

// Own messages to page windows.
#define RS_WM_JOB_LINE    (WM_APP + 1)  // wParam = job, lParam = wchar_t* line (Rs_Free)
#define RS_WM_JOB_DONE    (WM_APP + 2)  // wParam = job, lParam = exit code
#define RS_WM_PAGE_SHOWN  (WM_APP + 3)  // page became visible
#define RS_WM_OPEN_TEST   (WM_APP + 4)  // to the test page: lParam = wchar_t* container path (Rs_Free)
#define RS_WM_QUERY_CLOSE (WM_APP + 5)  // window is to close: *handled = 1 and return 0 keeps it open
#define RS_WM_STEP_TAB    (WM_APP + 6)  // Ctrl+Tab (wParam 1) or Ctrl+Shift+Tab (wParam -1): a page with tabs
                                        // of its own steps through them, *handled = 1 and returns 1
#define RS_WM_JOB_PROGRESS (WM_APP + 7) // wParam = job, lParam = struct RsJobProgress* (Rs_Free)
#define RS_WM_JOB_BUSY     (WM_APP + 8) // a job could not start, all slots taken: wParam = jobs
                                        // running, lParam = wchar_t* "busy: N jobs running" (Rs_Free)
// WM_APP + 64 and up: a page's own messages to itself (rs_char.c: CHAR_WM_ACTIVATED, posted by its
// subclass of the main window when Reload Studio becomes the active program again).

// Switch page (also from within a page, e.g. "Test in game" after the build).
void Rs_ShowPage(int id);
HWND Rs_PageWindow(int id);
HWND Rs_MainWindow(void);

// ---------------------------------------------------------------------------
// Sizes, fonts, colours
// ---------------------------------------------------------------------------

// Scales a value in 96-dpi pixels to the current resolution: the dpi of the
// monitor the window is on (it follows a move to another monitor), or the
// --ui-scale of the command line.
int Rs_Px(int px96);

// A system metric (SM_CXVSCROLL, ...) for the monitor of the window h. Windows
// draws scroll bars, check boxes and frames in that size, also under
// --ui-scale; GetSystemMetrics alone answers for the main monitor only.
int Rs_Metric(HWND h, int index);

enum RsFont {
    RS_FONT_BODY = 0,   // Segoe UI 10 pt - default for all controls
    RS_FONT_BOLD,       // Segoe UI Semibold 10 pt - labels of fields
    RS_FONT_SMALL,      // Segoe UI 9 pt - reasons and notes below fields
    RS_FONT_SECTION,    // Segoe UI Semibold 12 pt - card titles
    RS_FONT_TITLE,      // Segoe UI Semibold 20 pt - page titles (painted by the shell)
    RS_FONT_MONO,       // Consolas 9 pt - paths, raw output
    RS_FONT_COUNT
};
HFONT Rs_Font(int font);

// Colours (COLORREF) from the active palette: light (white cards on a
// light grey background) or dark. The values are in rs_shell.c (g_rsPalettes).
// The macros read the palette on every call; a change of scheme at
// run time (sidebar, automation "theme") updates everything that is read when
// drawing. That is why RS_COL_* belongs in no static initialiser, in
// no case label and in no other constant expression. A colour that
// a page keeps as a COLORREF is that of the old palette after a change
// (Rs_SetTextColor still maps it correctly, but a comparison with RS_COL_*
// no longer holds then).
enum RsPalSlot {
    RS_PAL_PAGE = 0,
    RS_PAL_CARD,
    RS_PAL_BORDER,
    RS_PAL_TEXT,
    RS_PAL_MUTED,
    RS_PAL_ACCENT,       // orange, the "Build" button - the same in both palettes
    RS_PAL_ACCENT_DK,
    RS_PAL_OK,
    RS_PAL_NOTE,
    RS_PAL_WARNING,
    RS_PAL_ERROR,
    RS_PAL_SIDEBAR,
    RS_PAL_COUNT
};
struct RsPalette {
    COLORREF c[RS_PAL_COUNT];
};
extern const struct RsPalette *g_rsPal;   // rs_shell.c; only the shell sets it

#define RS_COL_PAGE       (g_rsPal->c[RS_PAL_PAGE])
#define RS_COL_CARD       (g_rsPal->c[RS_PAL_CARD])
#define RS_COL_BORDER     (g_rsPal->c[RS_PAL_BORDER])
#define RS_COL_TEXT       (g_rsPal->c[RS_PAL_TEXT])
#define RS_COL_MUTED      (g_rsPal->c[RS_PAL_MUTED])
#define RS_COL_ACCENT     (g_rsPal->c[RS_PAL_ACCENT])
#define RS_COL_ACCENT_DK  (g_rsPal->c[RS_PAL_ACCENT_DK])
#define RS_COL_OK         (g_rsPal->c[RS_PAL_OK])
#define RS_COL_NOTE       (g_rsPal->c[RS_PAL_NOTE])
#define RS_COL_WARNING    (g_rsPal->c[RS_PAL_WARNING])
#define RS_COL_ERROR      (g_rsPal->c[RS_PAL_ERROR])
#define RS_COL_SIDEBAR    (g_rsPal->c[RS_PAL_SIDEBAR])

COLORREF Rs_SeverityColor(int severity);

// ---------------------------------------------------------------------------
// Cards: white areas with a title that the shell paints onto the page window.
// A page reports them in layout(): first Rs_CardClear, then Rs_CardAdd per card.
// The title is at the top of the card; Rs_CardInner returns the rectangle below it,
// where the controls belong.
// ---------------------------------------------------------------------------

void Rs_CardClear(HWND page);
void Rs_CardAdd(HWND page, const RECT *outer, const wchar_t *title);
RECT Rs_CardInner(const RECT *outer, int hasTitle);
int  Rs_PageTop(void);    // first free y line below the page heading

// ---------------------------------------------------------------------------
// Create controls - all with RS_FONT_BODY, visible, as a child of page.
// id is the ID for WM_COMMAND. Position with MoveWindow in layout().
// ---------------------------------------------------------------------------

HWND Rs_Label(HWND page, int id, const wchar_t *text, int font);
HWND Rs_Edit(HWND page, int id, const wchar_t *text, DWORD extraStyle);
HWND Rs_Button(HWND page, int id, const wchar_t *text);
HWND Rs_PrimaryButton(HWND page, int id, const wchar_t *text);  // filled, accent colour
HWND Rs_Check(HWND page, int id, const wchar_t *text);
HWND Rs_Combo(HWND page, int id);                               // CBS_DROPDOWNLIST
HWND Rs_ListBox(HWND page, int id, DWORD extraStyle);           // LBS_NOTIFY
HWND Rs_ListView(HWND page, int id, DWORD extraStyle);          // LVS_REPORT, full row

// Text colour of a label (default RS_COL_TEXT). Check boxes ignore it
// in the light scheme. The shell remembers a palette colour (RS_COL_*) as a
// slot, not as a value: after a change of scheme the label gets
// the equivalent colour of the new palette.
void Rs_SetTextColor(HWND control, COLORREF color);

// Convenience: set/read text. Rs_GetText returns a buffer (Rs_Free).
void     Rs_SetText(HWND control, const wchar_t *text);
wchar_t *Rs_GetText(HWND control);

// Measuring for layout(): the width a check box needs for its text (box, gap,
// text, a margin), and the width of a text in the font of control h.
int Rs_CheckBoxWidth(HWND box);
int Rs_TextWidth(HWND h, const wchar_t *text);

// ---------------------------------------------------------------------------
// Preview features: fields of a page that are shown but not finished yet.
// Without --enable-preview-features they are visible, greyed out and marked
// "Coming soon"; nothing of them is ever written into a container.
// ---------------------------------------------------------------------------

// 1 with --enable-preview-features on the command line (rs_shell.c). Never
// stored in the settings. Every command and automation path of a locked field
// checks it first - EnableWindow alone does not stop a posted WM_COMMAND.
extern int g_rsPreviewFeatures;

// Tooltip for a control of a page, NULL or "" removes it. The tool sits on the
// page window over the control's rectangle, so it also shows for a disabled
// control (those get no mouse messages). The shell moves the rectangles after
// every layout() and lists the tips in the automation verb "controls".
void Rs_SetTip(HWND control, const wchar_t *text);

// The small hint at the right of a card's title line: "Coming soon" (muted),
// with --enable-preview-features "Preview feature" (note colour). Position it in
// layout() like any label.
HWND Rs_ComingSoon(HWND page, int id);

// ---------------------------------------------------------------------------
// Message list: own control with wrapping. Every line has a
// coloured dot (severity), a text and optionally a grey second line.
// ---------------------------------------------------------------------------

HWND Rs_MsgList(HWND page, int id);
void Rs_MsgListClear(HWND list);
void Rs_MsgListAdd(HWND list, int severity, const wchar_t *text, const wchar_t *detail);
int  Rs_MsgListCount(HWND list);
// A clickable list shows the hand over its entries; a click on one sends
// WM_COMMAND with the notification code RS_MSGN_CLICK to the page, and
// Rs_MsgListClicked then says which entry it was (0 = the first, -1 = none).
// It is a stop of the key Tab: up and down frame an entry, Enter or Space
// click it.
#define RS_MSGN_CLICK 1
void Rs_MsgListSetClickable(HWND list, int on);
int  Rs_MsgListClicked(HWND list);
// Whole entries only: less space around each, an entry that would be cut is
// left out for a line "N more below - scroll", and the list scrolls by entries.
void Rs_MsgListSetWhole(HWND list, int on);
// Writes all entries as "<severity>\t<text>\t<detail>" appended to f (UTF-8).
void Rs_MsgListWrite(HWND list, FILE *f);

// ---------------------------------------------------------------------------
// Child processes
// ---------------------------------------------------------------------------

// Starts this exe as rldpack with the arguments args[0..argc-1] (without
// "--rldpack"). Every output line (stdout and stderr, UTF-8 -> UTF-16, without
// line end) arrives as RS_WM_JOB_LINE at notify, at the end RS_WM_JOB_DONE.
// Return: job ID > 0, or 0 if the start failed. When all job slots are
// taken, RS_WM_JOB_BUSY arrives at notify as well.
int  Rs_RunRldpack(HWND notify, const wchar_t *const *args, int argc);

// Starts another program (the game). cmdline is the whole command line
// without the program name. capture = 0: no redirection, only RS_WM_JOB_DONE.
int  Rs_RunProcess(HWND notify, const wchar_t *exe, const wchar_t *cmdline,
                   const wchar_t *cwd, int capture);

// Kills the process of a running job (Rs_RunProcess, Rs_RunRldpack)
// hard; RS_WM_JOB_DONE arrives afterwards as usual. 1 = ended, 0 = no
// running job with this ID.
int  Rs_KillJob(int id);

// A line "@progress\t<step>\t<done>\t<total>" of rldpack does not arrive as
// RS_WM_JOB_LINE but as RS_WM_JOB_PROGRESS. step as rldpack wrote it
// ("repair", "remesh", "reduce", "write"); total 0 = not known.
struct RsJobProgress {
    wchar_t step[16];
    unsigned long done;
    unsigned long total;
};

// Exit code in RS_WM_JOB_DONE of a job ended by Rs_JobCancel (the value of
// STATUS_CONTROL_C_EXIT).
#define RS_JOB_CANCELLED (-1073741510)

// Cancels a running job: its process ends (an rldpack job together with
// every process it started), with the exit code RS_JOB_CANCELLED.
// RS_WM_JOB_DONE arrives afterwards as usual, after all lines of the job;
// only then are the files of the process closed (delete "<out>.part" there).
// A job that ended by itself just before keeps its own exit code.
// 1 = ended, 0 = no running job with this ID.
int  Rs_JobCancel(int id);

// Splits a machine line "@kind\tf1\tf2..." IN PLACE. fields[0] is the kind
// without '@'. Return: number of fields; 0 if the line is not a machine line.
int  Rs_SplitMachine(wchar_t *line, wchar_t **fields, int maxFields);

// Appends an argument, correctly quoted, to a command line (for Rs_RunProcess).
void Rs_AppendArg(wchar_t *cmdline, size_t cap, const wchar_t *arg);

// ---------------------------------------------------------------------------
// Dialogs, paths, settings, memory
// ---------------------------------------------------------------------------

// Returns 1 if chosen; out gets the path.
int Rs_BrowseFolder(HWND owner, const wchar_t *title, const wchar_t *initial,
                    wchar_t *out, int outCap);
// filter as for OPENFILENAME: L"Track containers\0*.rldtrack\0\0"
int Rs_BrowseOpenFile(HWND owner, const wchar_t *title, const wchar_t *filter,
                      const wchar_t *initial, wchar_t *out, int outCap);
int Rs_BrowseSaveFile(HWND owner, const wchar_t *title, const wchar_t *filter,
                      const wchar_t *defExt, const wchar_t *initial,
                      wchar_t *out, int outCap);

// Yes/no question. In automation the answer is always yes, without a dialog.
int  Rs_AskYesNo(HWND owner, const wchar_t *title, const wchar_t *text);
// Question with count (2..4) buttons labelled buttons[0..]; returns the index of
// the button pressed, the last one (meant as Cancel) when the dialog is closed.
// In automation the answer is autoAnswer, without a dialog, logged.
int  Rs_AskChoice(HWND owner, const wchar_t *title, const wchar_t *text,
                  const wchar_t *const *buttons, int count, int autoAnswer);
// Notice with OK. In automation only into the automation log.
void Rs_Tell(HWND owner, const wchar_t *title, const wchar_t *text);
int  Rs_Automating(void);

// Settings in %APPDATA%\CTR Reload\reloadstudio.ini, section [reloadstudio].
// If that file is missing at start (not in automation), all keys of the section
// [alphamaker] in alphamaker.ini (the tool's earlier name) are copied over once;
// the old file is only read, never changed or deleted. In automation (--do)
// the settings are neither read nor written.
//
// Command line `--settings <ini>` (for automation and tests): the settings are
// read and written only in that file, also in automation (theme and start page
// stay fixed there: light, page Track), nothing is copied from alphamaker.ini,
// and logs and temporary files go to the folder of that file (Rs_TempDir).
// Reload Studio then touches neither %APPDATA% nor %TEMP%. `--rldpack` ignores it.
// Missing levels of the folder are created. A missing value, a folder (existing,
// or ending in a slash) or a folder that cannot be created is an error: no
// settings, logs next to this exe, a message box (in automation: the log).
void Rs_ConfigGet(const wchar_t *key, wchar_t *out, int outCap);
void Rs_ConfigSet(const wchar_t *key, const wchar_t *value);
// Full path of the settings file in use, "" = none.
const wchar_t *Rs_SettingsPath(void);

// Folder for logs and temporary files, created: %TEMP%\Reload Studio, with
// --settings the folder of the settings file. Every page takes its temporary
// files from here.
void Rs_TempDir(wchar_t *out, int cap);

// Directory of this exe (without a trailing slash).
const wchar_t *Rs_ExeDir(void);

// The game program, the same rule for every page: the chosen one (setting
// test.exe, chosen on the page "Test in game") if that file exists, otherwise
// ctr_native.exe in the folder of this exe. No other folder is searched.
// 1 = found; 0 = out is "" (show RS_TEXT_NO_GAME_EXE).
int Rs_FindGameExe(wchar_t *out, int outCap);

// Message when Rs_FindGameExe finds nothing (pages other than Test).
#define RS_TEXT_NO_GAME_EXE \
    L"The game program (ctr_native.exe) was not found next to Reload Studio. " \
    L"Put Reload Studio into the folder of the game, or choose the game on the " \
    L"page Test in game (Browse) - Reload Studio remembers the choice."

// Test page (rs_test.c): 1 if exactly this game program was checked there via
// --version and comes from the same package as Reload Studio.
// 0 = not checked, check still running, another program or wrong build.
int Rs_TestGameVerified(const wchar_t *exePath);
// Why the entered game does not count there (yet), as a phrase for
// "Preview skipped: ..."; NULL = it counts.
const wchar_t *Rs_TestGameProblem(void);
// The game the test page has just entered (full path), or "".
const wchar_t *Rs_TestGameExe(void);

// New path for a game log (--log): in the folder Rs_TempDir (%TEMP%\Reload
// Studio, or the folder of --settings) the file "<kind> YYYY-MM-DD HH-MM-SS.log"
// in local time, taken -> " (2)", " (3)" ...
// Creates the folder, deletes the old file "<kind>.log" and cleans up older
// files of the same kind, so that at most keep remain including the new one.
// Delete errors (file still open) do not count.
void Rs_RotatedLogPath(wchar_t *out, int cap, const wchar_t *kind, int keep);

// Path helpers.
int  Rs_FileExists(const wchar_t *path);
int  Rs_DirExists(const wchar_t *path);
void Rs_PathJoin(wchar_t *out, int outCap, const wchar_t *dir, const wchar_t *name);
const wchar_t *Rs_PathName(const wchar_t *path);   // pointer to the file name
void Rs_PathDir(wchar_t *out, int outCap, const wchar_t *path);  // its folder

// Memory and conversion.
void    *Rs_Alloc(size_t bytes);         // zeroed; aborts on shortage
void     Rs_Free(void *p);
wchar_t *Rs_Dup(const wchar_t *s);
wchar_t *Rs_FromUtf8(const char *s, int bytes);   // bytes < 0: up to NUL
char    *Rs_ToUtf8(const wchar_t *s);

// Which content of a file was read: size, last write time and a hash
// (64-bit FNV-1a) of its bytes. exists = 0: the file was not there.
struct RsFileStamp {
    int exists;
    unsigned long long size;
    FILETIME writeTime;
    unsigned long long hash;
};

// What Rs_ReadTextFileEx found besides the text.
struct RsTextRead {
    struct RsFileStamp stamp;
    DWORD error;        // Windows error if the file is there but could not be read, else 0
    int badUtf8;        // the bytes are not valid UTF-8; the text has U+FFFD for the bad ones
};

// Reads a text file (UTF-8, with or without BOM) as UTF-16. NULL if it is not
// there or could not be read completely (read error, short read, over 16 MB) -
// never a partial text. Invalid UTF-8 comes back with U+FFFD in its place.
wchar_t *Rs_ReadTextFile(const wchar_t *path);
// The same, and says in info (may be NULL) whether the file exists, why reading
// failed, whether the UTF-8 was invalid, and the stamp of what was read.
wchar_t *Rs_ReadTextFileEx(const wchar_t *path, struct RsTextRead *info);
// The stamp of the file as it is on disk now (reads it). 1 = known (also
// "not there"), 0 = there but could not be read (then *out is unknown).
int      Rs_FileStampNow(const wchar_t *path, struct RsFileStamp *out);
// 1 if both stamps describe the same content (or both "not there").
int      Rs_FileStampSame(const struct RsFileStamp *a, const struct RsFileStamp *b);
// Writes UTF-16 text as UTF-8 without BOM, line ends as passed. Atomically:
// the bytes go to "<path>.tmp", are flushed to disk and only then replace the
// file (ReplaceFileW, or MoveFileExW if it was not there). On failure the old
// file is unchanged, the temp file is removed, and GetLastError() says why.
// 1 = ok.
int      Rs_WriteTextFile(const wchar_t *path, const wchar_t *text);

// Automation log: one line to the automation's stdout (--log <file>).
void Rs_AutoLog(const wchar_t *fmt, ...);

#endif
