// reloadstudio.h - shared interface of Reload Studio
//
// Reload Studio is the front end for track and character authors: pack a
// track folder into a .rldtrack container, put together cups for cups.txt,
// build a .rldchar character from a PLY model, test a track in the game. All
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
//   rs_view.c     3D model view (window class RsModelView, rs_view.h)
//   rs_test.c     page "Test in game"
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
// @file ply, @msg of reading the PLY, @value size-range, @msg of the chain,
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
//                  (z | -z), scale, two_sided (yes | no), out, size (the
//                  --size percent used, 100 when missing or invalid), icon,
//                  voices ("" when not given), fit (crash | none), reduce
//                  (auto | off), wheels (on | off)
//   @value    size-range <lo> <hi>          (no origin)
//             the --size percentages this model takes, whole numbers inside
//             50..200, the run around 100; "0 0" = no size fits (the model
//             itself is too large). For every model whose parts were found.
//   @value    kart-box <x0> <y0> <z0> <x1> <y1> <z1>   (no origin, --preview only)
//             the model's kart in the neutral frame, in the units of the
//             preview file; the kart is never scaled by --size
//   @value    retail-kart <x0> <y0> <z0> <x1> <y1> <z1>   (no origin)
//             the fixed box of a retail kart in the same units; the page draws
//             it as the grey size reference next to the model
//   @value    crash-box <x0> <y0> <z0> <x1> <y1> <z1>   (no origin)
//             Crash with his kart (retail racer model, birth pose) in the same
//             units, with ONE DECIMAL ("-33.8"); --fit crash fits the model to
//             it, the page outlines it about the model
//   @file     <kind> <state> <name> <bytes>
//             ply           ok | missing          the model
//             icon          ok | missing | bad    --icon (bad: not a PNG rldpack reads)
//             icon-original ok | failed           <prefix>-original.bmp, the PNG as read
//             icon-preview  ok | failed           <prefix>-icon.bmp, 44 x 26 as in the game
//             voice         ok | bad | unused | unknown | ignored   per file of the folder
//             preview       ok | failed           --preview (failed: warning, build unaffected)
//             The BMPs are 32 bit BI_RGB, bottom-up, B G R A (A = 0 transparent).
//   @char     <fact> <value> [<detail>]       for the display, not in the file:
//             vertices faces quads triangles, parts <count> <"kart N, driver N,
//             steering wheel N">, size <length> <width> <height> <bottom>
//             (Blender units, after --size), scale <x> <y> <z>, colors_in,
//             colors_out, color_error <mean> <max>, part_colors <group> <ranges>,
//             records, slots, frames, draw_bytes, draw_delta, icon_source <w> <h>,
//             icon_crop <x> <y> <w> <h>, icon_colors, icon_opaque (of 1144),
//             voices <"n of 18">,
//             fit <factor> <length before> <after> <height before> <after>
//             <kart | model>: the --fit crash factor (4 significant digits,
//             %.4g), the lengths and heights in game units (1 decimal; below 1
//             3 significant digits, %.3g - a model in millimeters); kart = the
//             model's kart was matched to Crash's, model = the whole model (no
//             kart found, or --wheels off),
//             reduced <triangles before> <after> <draw bytes before> <after>:
//             only when --reduce auto lowered the triangle count
//   @msg      as above. ids the page reads itself: char-size (error,
//             "Size N% is outside lo..hi% for this model: <why>. Choose a size in
//             that range."). Others of make-char, shown as they come: icon-file,
//             icon-roundtrip, icon-small, icon-empty, icon-colors, icon-preview,
//             voice-folder, voice-other, voice-unknown, voice-double, voice-wav,
//             voice-vag, voice-stereo, voice-silent, voice-clip,
//             voice-length-line, voice-length-short, voice-source, voice-same,
//             voice-template, voice-missing, voice-later (always with --voices),
//             preview, model-reduced (info, with every reduction), and the
//             ply-*, model-*, name*, usage ... of before
//   @result   <ok | failed | checked> <output path> <bytes> <sha256>
//             with --icon the container has a third chunk CICN (612 bytes)
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
//   make-char --machine --check --model <ply> --name <n> --template 14
//             --class <balanced|acceleration|speed|turning> --size <percent>
//             [--reduce off] [--wheels off]   (only when unchecked; the
//             defaults are --reduce auto, --wheels on, --fit crash)
//             [--icon <png> --icon-preview <prefix>] [--voices <dir>]
//             --preview <file> [--out <f>]      check; writes only the preview files
//   make-char --machine --model ... --out <f>  build: the same switches without
//             --check, --preview and --icon-preview
// The --preview file (little endian): "RLDPV1\0\0", u32 poses = 3 (turn frame 10
// neutral, frame 0 full steer left, frame 20 full steer right), per pose u32
// triangles and per triangle 3 corners of s16 x, y, z (game units, +Y up, +Z
// forward, +X the driver's left), u8 r, g, b, u8 pad (bit 0: drawn from both
// sides). Corners counter-clockwise seen from the side the game draws.
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
// ---------------------------------------------------------------------------

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

// Switch page (also from within a page, e.g. "Test in game" after the build).
void Rs_ShowPage(int id);
HWND Rs_PageWindow(int id);
HWND Rs_MainWindow(void);

// ---------------------------------------------------------------------------
// Sizes, fonts, colours
// ---------------------------------------------------------------------------

// Scales a value in 96-dpi pixels to the current resolution.
int Rs_Px(int px96);

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

// ---------------------------------------------------------------------------
// Message list: own control with wrapping. Every line has a
// coloured dot (severity), a text and optionally a grey second line.
// ---------------------------------------------------------------------------

HWND Rs_MsgList(HWND page, int id);
void Rs_MsgListClear(HWND list);
void Rs_MsgListAdd(HWND list, int severity, const wchar_t *text, const wchar_t *detail);
int  Rs_MsgListCount(HWND list);
// Writes all entries as "<severity>\t<text>\t<detail>" appended to f (UTF-8).
void Rs_MsgListWrite(HWND list, FILE *f);

// ---------------------------------------------------------------------------
// Child processes
// ---------------------------------------------------------------------------

// Starts this exe as rldpack with the arguments args[0..argc-1] (without
// "--rldpack"). Every output line (stdout and stderr, UTF-8 -> UTF-16, without
// line end) arrives as RS_WM_JOB_LINE at notify, at the end RS_WM_JOB_DONE.
// Return: job ID > 0, or 0 if the start failed.
int  Rs_RunRldpack(HWND notify, const wchar_t *const *args, int argc);

// Starts another program (the game). cmdline is the whole command line
// without the program name. capture = 0: no redirection, only RS_WM_JOB_DONE.
int  Rs_RunProcess(HWND notify, const wchar_t *exe, const wchar_t *cmdline,
                   const wchar_t *cwd, int capture);

// Kills the process of a running job (Rs_RunProcess, Rs_RunRldpack)
// hard; RS_WM_JOB_DONE arrives afterwards as usual. 1 = ended, 0 = no
// running job with this ID.
int  Rs_KillJob(int id);

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
// the old file is only read, never changed or deleted.
void Rs_ConfigGet(const wchar_t *key, wchar_t *out, int outCap);
void Rs_ConfigSet(const wchar_t *key, const wchar_t *value);

// Directory of this exe (without a trailing slash).
const wchar_t *Rs_ExeDir(void);

// Looks for ctr_native.exe: next to this exe, then up to three folders higher,
// each time also in build-msvc-x86\Release. 1 = found.
int Rs_FindGameExe(wchar_t *out, int outCap);

// Test page (rs_test.c): 1 if exactly this game program was checked there via
// --version and comes from the same package as Reload Studio.
// 0 = not checked, check still running, another program or wrong build.
int Rs_TestGameVerified(const wchar_t *exePath);
// Why the entered game does not count there (yet), as a phrase for
// "Preview skipped: ..."; NULL = it counts.
const wchar_t *Rs_TestGameProblem(void);
// The game the test page has just entered (full path), or "".
const wchar_t *Rs_TestGameExe(void);

// New path for a game log (--log): in the folder %TEMP%\Reload Studio
// the file "<kind> YYYY-MM-DD HH-MM-SS.log" in local time, taken -> " (2)", " (3)" ...
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
