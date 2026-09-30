// alphamaker.h - shared interface of the Alpha-Maker
//
// The Alpha-Maker is the front end for track authors: pack a track folder into
// a .rldtrack container, put together cups for cups.txt, test a
// track in the game. All texts on screen are English.
//
// GROUND RULE: converting and checking is done by rldpack make. The Alpha-Maker carries
// rldpack.c inside (am_rldpack.c) and starts itself as a child process with
// `--rldpack <arguments>`. It reads its machine lines (below) and shows
// them. It checks NOTHING itself: no modes, no model ID, no ABR,
// no music. It assigns no level ID and never touches track-ids.tsv.
//
// Files:
//   am_shell.c    window, sidebar, fonts, colours (light/dark),
//                 cards, buttons, message list, dialogs, settings,
//                 child processes, automation (--do) and screenshot (shot)
//   am_rldpack.c  rldpack.c with a renamed main
//   am_track.c    page "Track"
//   am_cups.c     page "Cups"
//   am_test.c     page "Test in game"
//
// Characters: UTF-16 in the front end (W functions), UTF-8 on the pipe to
// rldpack and in files. The manifest sets the ANSI code page to UTF-8,
// so that rldpack (fopen, argv) understands paths with umlauts.

#ifndef ALPHAMAKER_H
#define ALPHAMAKER_H

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
// Alpha-Maker do not belong together. "unknown" (build without git) does not check.
// ---------------------------------------------------------------------------
#include "ctr_build_id.h"
#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "Beta 0"
#endif
#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif
#define AM_WIDEN2(x) L##x
#define AM_WIDEN(x)  AM_WIDEN2(x)
#define AM_VERSION_W  AM_WIDEN(CTR_NATIVE_VERSION)     // L"Beta 0"
#define AM_BUILD_ID_W AM_WIDEN(CTR_NATIVE_BUILD_ID)    // L"<12 hex>" or L"<12 hex>-dirty-<6 hex>"

// Message when there is no game data next to the game (pages Track and Test).
#define AM_TEXT_NO_GAME_DATA \
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
// ---------------------------------------------------------------------------

#define AM_PROTOCOL 1

// Severity of a message, for the message list and the colour of labels.
enum AmSeverity {
    AM_SEV_OK = 0,      // green: built, present
    AM_SEV_INFO,        // grey: for information only
    AM_SEV_NOTE,        // blue: note, e.g. "not playable in CTR Reload yet"
    AM_SEV_WARNING,     // amber: builds, but take a look
    AM_SEV_ERROR,       // red: does not build
    AM_SEV_COUNT
};

// "@msg" severity from rldpack -> enum. Unknown -> AM_SEV_INFO.
int Am_SeverityFromText(const wchar_t *text);

// ---------------------------------------------------------------------------
// Pages
//
// Every page is a child window of the class the shell provides. The shell
// paints background and cards, colours labels, paints main buttons and
// passes everything else on to the page's callbacks. A page creates its
// controls in create() and positions them in layout().
// ---------------------------------------------------------------------------

// Return values of automate().
enum AmAuto {
    AM_AUTO_UNKNOWN = 0,   // verb does not belong to this page
    AM_AUTO_DONE,          // done
    AM_AUTO_WAIT,          // started; the shell waits until busy() says 0
    AM_AUTO_FAIL           // did not work; the shell notes it and continues
};

struct AmPageDef {
    const wchar_t *navName;    // entry in the sidebar, e.g. L"Track"
    const wchar_t *title;      // heading of the page
    const wchar_t *subtitle;   // one line below it

    // Create controls. page is the page window.
    void (*create)(HWND page);

    // Position controls. w/h in pixels of the page (without the heading - the
    // shell paints that above Am_PageTop()). Report the cards for this pass
    // with Am_CardClear/Am_CardAdd.
    void (*layout)(HWND page, int w, int h);

    // WM_COMMAND and WM_NOTIFY of the children. Return value as for the window procedure.
    LRESULT (*command)(HWND page, WPARAM wParam, LPARAM lParam);
    LRESULT (*notify)(HWND page, NMHDR *hdr);

    // All other messages to the page window, especially AM_WM_JOB_LINE,
    // AM_WM_JOB_DONE, WM_TIMER and AM_WM_PAGE_SHOWN. *handled = 1 if the
    // page processed them.
    LRESULT (*message)(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled);

    // Automation: a verb with an argument (rest of the line, may be empty).
    int (*automate)(HWND page, const wchar_t *verb, const wchar_t *arg);

    // 1 as long as the page is waiting for a child process.
    int (*busy)(HWND page);
};

extern const struct AmPageDef g_amTrackPage;   // am_track.c
extern const struct AmPageDef g_amCupsPage;    // am_cups.c
extern const struct AmPageDef g_amTestPage;    // am_test.c

enum AmPageId { AM_PAGE_TRACK = 0, AM_PAGE_CUPS, AM_PAGE_TEST, AM_PAGE_COUNT };

// Own messages to page windows.
#define AM_WM_JOB_LINE    (WM_APP + 1)  // wParam = job, lParam = wchar_t* line (Am_Free)
#define AM_WM_JOB_DONE    (WM_APP + 2)  // wParam = job, lParam = exit code
#define AM_WM_PAGE_SHOWN  (WM_APP + 3)  // page became visible
#define AM_WM_OPEN_TEST   (WM_APP + 4)  // to the test page: lParam = wchar_t* container path (Am_Free)
#define AM_WM_QUERY_CLOSE (WM_APP + 5)  // window is to close: *handled = 1 and return 0 keeps it open

// Switch page (also from within a page, e.g. "Test in game" after the build).
void Am_ShowPage(int id);
HWND Am_PageWindow(int id);
HWND Am_MainWindow(void);

// ---------------------------------------------------------------------------
// Sizes, fonts, colours
// ---------------------------------------------------------------------------

// Scales a value in 96-dpi pixels to the current resolution.
int Am_Px(int px96);

enum AmFont {
    AM_FONT_BODY = 0,   // Segoe UI 10 pt - default for all controls
    AM_FONT_BOLD,       // Segoe UI Semibold 10 pt - labels of fields
    AM_FONT_SMALL,      // Segoe UI 9 pt - reasons and notes below fields
    AM_FONT_SECTION,    // Segoe UI Semibold 12 pt - card titles
    AM_FONT_TITLE,      // Segoe UI Semibold 20 pt - page titles (painted by the shell)
    AM_FONT_MONO,       // Consolas 9 pt - paths, raw output
    AM_FONT_COUNT
};
HFONT Am_Font(int font);

// Colours (COLORREF) from the active palette: light (white cards on a
// light grey background) or dark. The values are in am_shell.c (g_amPalettes).
// The macros read the palette on every call; a change of scheme at
// run time (sidebar, automation "theme") updates everything that is read when
// drawing. That is why AM_COL_* belongs in no static initialiser, in
// no case label and in no other constant expression. A colour that
// a page keeps as a COLORREF is that of the old palette after a change
// (Am_SetTextColor still maps it correctly, but a comparison with AM_COL_*
// no longer holds then).
enum AmPalSlot {
    AM_PAL_PAGE = 0,
    AM_PAL_CARD,
    AM_PAL_BORDER,
    AM_PAL_TEXT,
    AM_PAL_MUTED,
    AM_PAL_ACCENT,       // orange, the "Build" button - the same in both palettes
    AM_PAL_ACCENT_DK,
    AM_PAL_OK,
    AM_PAL_NOTE,
    AM_PAL_WARNING,
    AM_PAL_ERROR,
    AM_PAL_SIDEBAR,
    AM_PAL_COUNT
};
struct AmPalette {
    COLORREF c[AM_PAL_COUNT];
};
extern const struct AmPalette *g_amPal;   // am_shell.c; only the shell sets it

#define AM_COL_PAGE       (g_amPal->c[AM_PAL_PAGE])
#define AM_COL_CARD       (g_amPal->c[AM_PAL_CARD])
#define AM_COL_BORDER     (g_amPal->c[AM_PAL_BORDER])
#define AM_COL_TEXT       (g_amPal->c[AM_PAL_TEXT])
#define AM_COL_MUTED      (g_amPal->c[AM_PAL_MUTED])
#define AM_COL_ACCENT     (g_amPal->c[AM_PAL_ACCENT])
#define AM_COL_ACCENT_DK  (g_amPal->c[AM_PAL_ACCENT_DK])
#define AM_COL_OK         (g_amPal->c[AM_PAL_OK])
#define AM_COL_NOTE       (g_amPal->c[AM_PAL_NOTE])
#define AM_COL_WARNING    (g_amPal->c[AM_PAL_WARNING])
#define AM_COL_ERROR      (g_amPal->c[AM_PAL_ERROR])
#define AM_COL_SIDEBAR    (g_amPal->c[AM_PAL_SIDEBAR])

COLORREF Am_SeverityColor(int severity);

// ---------------------------------------------------------------------------
// Cards: white areas with a title that the shell paints onto the page window.
// A page reports them in layout(): first Am_CardClear, then Am_CardAdd per card.
// The title is at the top of the card; Am_CardInner returns the rectangle below it,
// where the controls belong.
// ---------------------------------------------------------------------------

void Am_CardClear(HWND page);
void Am_CardAdd(HWND page, const RECT *outer, const wchar_t *title);
RECT Am_CardInner(const RECT *outer, int hasTitle);
int  Am_PageTop(void);    // first free y line below the page heading

// ---------------------------------------------------------------------------
// Create controls - all with AM_FONT_BODY, visible, as a child of page.
// id is the ID for WM_COMMAND. Position with MoveWindow in layout().
// ---------------------------------------------------------------------------

HWND Am_Label(HWND page, int id, const wchar_t *text, int font);
HWND Am_Edit(HWND page, int id, const wchar_t *text, DWORD extraStyle);
HWND Am_Button(HWND page, int id, const wchar_t *text);
HWND Am_PrimaryButton(HWND page, int id, const wchar_t *text);  // filled, accent colour
HWND Am_Check(HWND page, int id, const wchar_t *text);
HWND Am_Combo(HWND page, int id);                               // CBS_DROPDOWNLIST
HWND Am_ListBox(HWND page, int id, DWORD extraStyle);           // LBS_NOTIFY
HWND Am_ListView(HWND page, int id, DWORD extraStyle);          // LVS_REPORT, full row

// Text colour of a label (default AM_COL_TEXT). Check boxes ignore it
// in the light scheme. The shell remembers a palette colour (AM_COL_*) as a
// slot, not as a value: after a change of scheme the label gets
// the equivalent colour of the new palette.
void Am_SetTextColor(HWND control, COLORREF color);

// Convenience: set/read text. Am_GetText returns a buffer (Am_Free).
void     Am_SetText(HWND control, const wchar_t *text);
wchar_t *Am_GetText(HWND control);

// ---------------------------------------------------------------------------
// Message list: own control with wrapping. Every line has a
// coloured dot (severity), a text and optionally a grey second line.
// ---------------------------------------------------------------------------

HWND Am_MsgList(HWND page, int id);
void Am_MsgListClear(HWND list);
void Am_MsgListAdd(HWND list, int severity, const wchar_t *text, const wchar_t *detail);
int  Am_MsgListCount(HWND list);
// Writes all entries as "<severity>\t<text>\t<detail>" appended to f (UTF-8).
void Am_MsgListWrite(HWND list, FILE *f);

// ---------------------------------------------------------------------------
// Child processes
// ---------------------------------------------------------------------------

// Starts this exe as rldpack with the arguments args[0..argc-1] (without
// "--rldpack"). Every output line (stdout and stderr, UTF-8 -> UTF-16, without
// line end) arrives as AM_WM_JOB_LINE at notify, at the end AM_WM_JOB_DONE.
// Return: job ID > 0, or 0 if the start failed.
int  Am_RunRldpack(HWND notify, const wchar_t *const *args, int argc);

// Starts another program (the game). cmdline is the whole command line
// without the program name. capture = 0: no redirection, only AM_WM_JOB_DONE.
int  Am_RunProcess(HWND notify, const wchar_t *exe, const wchar_t *cmdline,
                   const wchar_t *cwd, int capture);

// Kills the process of a running job (Am_RunProcess, Am_RunRldpack)
// hard; AM_WM_JOB_DONE arrives afterwards as usual. 1 = ended, 0 = no
// running job with this ID.
int  Am_KillJob(int id);

// Splits a machine line "@kind\tf1\tf2..." IN PLACE. fields[0] is the kind
// without '@'. Return: number of fields; 0 if the line is not a machine line.
int  Am_SplitMachine(wchar_t *line, wchar_t **fields, int maxFields);

// Appends an argument, correctly quoted, to a command line (for Am_RunProcess).
void Am_AppendArg(wchar_t *cmdline, size_t cap, const wchar_t *arg);

// ---------------------------------------------------------------------------
// Dialogs, paths, settings, memory
// ---------------------------------------------------------------------------

// Returns 1 if chosen; out gets the path.
int Am_BrowseFolder(HWND owner, const wchar_t *title, const wchar_t *initial,
                    wchar_t *out, int outCap);
// filter as for OPENFILENAME: L"Track containers\0*.rldtrack\0\0"
int Am_BrowseOpenFile(HWND owner, const wchar_t *title, const wchar_t *filter,
                      const wchar_t *initial, wchar_t *out, int outCap);
int Am_BrowseSaveFile(HWND owner, const wchar_t *title, const wchar_t *filter,
                      const wchar_t *defExt, const wchar_t *initial,
                      wchar_t *out, int outCap);

// Yes/no question. In automation the answer is always yes, without a dialog.
int  Am_AskYesNo(HWND owner, const wchar_t *title, const wchar_t *text);
// Question with count (2..4) buttons labelled buttons[0..]; returns the index of
// the button pressed, the last one (meant as Cancel) when the dialog is closed.
// In automation the answer is autoAnswer, without a dialog, logged.
int  Am_AskChoice(HWND owner, const wchar_t *title, const wchar_t *text,
                  const wchar_t *const *buttons, int count, int autoAnswer);
// Notice with OK. In automation only into the automation log.
void Am_Tell(HWND owner, const wchar_t *title, const wchar_t *text);
int  Am_Automating(void);

// Settings in %APPDATA%\CTR Reload\alphamaker.ini, section [alphamaker].
void Am_ConfigGet(const wchar_t *key, wchar_t *out, int outCap);
void Am_ConfigSet(const wchar_t *key, const wchar_t *value);

// Directory of this exe (without a trailing slash).
const wchar_t *Am_ExeDir(void);

// Looks for ctr_native.exe: next to this exe, then up to three folders higher,
// each time also in build-msvc-x86\Release. 1 = found.
int Am_FindGameExe(wchar_t *out, int outCap);

// Test page (am_test.c): 1 if exactly this game program was checked there via
// --version and comes from the same package as the Alpha-Maker.
// 0 = not checked, check still running, another program or wrong build.
int Am_TestGameVerified(const wchar_t *exePath);
// Why the entered game does not count there (yet), as a phrase for
// "Preview skipped: ..."; NULL = it counts.
const wchar_t *Am_TestGameProblem(void);
// The game the test page has just entered (full path), or "".
const wchar_t *Am_TestGameExe(void);

// New path for a game log (--log): in the folder %TEMP%\CTR Reload Alpha-Maker
// the file "<kind> YYYY-MM-DD HH-MM-SS.log" in local time, taken -> " (2)", " (3)" ...
// Creates the folder, deletes the old file "<kind>.log" and cleans up older
// files of the same kind, so that at most keep remain including the new one.
// Delete errors (file still open) do not count.
void Am_RotatedLogPath(wchar_t *out, int cap, const wchar_t *kind, int keep);

// Path helpers.
int  Am_FileExists(const wchar_t *path);
int  Am_DirExists(const wchar_t *path);
void Am_PathJoin(wchar_t *out, int outCap, const wchar_t *dir, const wchar_t *name);
const wchar_t *Am_PathName(const wchar_t *path);   // pointer to the file name
void Am_PathDir(wchar_t *out, int outCap, const wchar_t *path);  // its folder

// Memory and conversion.
void    *Am_Alloc(size_t bytes);         // zeroed; aborts on shortage
void     Am_Free(void *p);
wchar_t *Am_Dup(const wchar_t *s);
wchar_t *Am_FromUtf8(const char *s, int bytes);   // bytes < 0: up to NUL
char    *Am_ToUtf8(const wchar_t *s);

// Which content of a file was read: size, last write time and a hash
// (64-bit FNV-1a) of its bytes. exists = 0: the file was not there.
struct AmFileStamp {
    int exists;
    unsigned long long size;
    FILETIME writeTime;
    unsigned long long hash;
};

// What Am_ReadTextFileEx found besides the text.
struct AmTextRead {
    struct AmFileStamp stamp;
    DWORD error;        // Windows error if the file is there but could not be read, else 0
    int badUtf8;        // the bytes are not valid UTF-8; the text has U+FFFD for the bad ones
};

// Reads a text file (UTF-8, with or without BOM) as UTF-16. NULL if it is not
// there or could not be read completely (read error, short read, over 16 MB) -
// never a partial text. Invalid UTF-8 comes back with U+FFFD in its place.
wchar_t *Am_ReadTextFile(const wchar_t *path);
// The same, and says in info (may be NULL) whether the file exists, why reading
// failed, whether the UTF-8 was invalid, and the stamp of what was read.
wchar_t *Am_ReadTextFileEx(const wchar_t *path, struct AmTextRead *info);
// The stamp of the file as it is on disk now (reads it). 1 = known (also
// "not there"), 0 = there but could not be read (then *out is unknown).
int      Am_FileStampNow(const wchar_t *path, struct AmFileStamp *out);
// 1 if both stamps describe the same content (or both "not there").
int      Am_FileStampSame(const struct AmFileStamp *a, const struct AmFileStamp *b);
// Writes UTF-16 text as UTF-8 without BOM, line ends as passed. Atomically:
// the bytes go to "<path>.tmp", are flushed to disk and only then replace the
// file (ReplaceFileW, or MoveFileExW if it was not there). On failure the old
// file is unchanged, the temp file is removed, and GetLastError() says why.
// 1 = ok.
int      Am_WriteTextFile(const wchar_t *path, const wchar_t *text);

// Automation log: one line to the automation's stdout (--log <file>).
void Am_AutoLog(const wchar_t *fmt, ...);

#endif
