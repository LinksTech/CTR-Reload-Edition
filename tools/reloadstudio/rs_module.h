// rs_module.h - the modules of Reload Studio
//
// For the user Reload Studio is ONE program with one window. Inside, every
// page of the sidebar is a module: a name, a page (struct RsPageDef below),
// the modules it needs and the oldest core it runs on. The core is
// everything that is no page: the window and the sidebar, the helpers of
// rs_shell.c, rldpack inside the exe (the checks and the packer), the preview
// (rs_view.c, rs_tex.c) and the version.
//
// BUILT-IN MODULES - Track, Cups, Character, Test in game - are a table in
// rs_module.c; their order is the order of the sidebar and their page IDs
// (enum RsPageId in reloadstudio.h).
//
// OTHER MODULES are 32-bit DLLs (Reload Studio is a 32-bit program) in the
// folder "modules" next to ReloadStudio.exe (for tests: --modules-dir
// <folder>; --no-modules loads none). Each exports one function,
//     RS_MODULE_EXPORT const struct RsModuleDef *__cdecl
//         ReloadStudioModule(const struct RsCoreApi *core);
// which gets the core's table of helpers (struct RsCoreApi) and gives its
// module. A module that cannot be loaded or does not fit - not a DLL,
// another module interface (RS_MODULE_ABI), a newer core than this one
// (minCore), a word that is not valid or taken, no page, a module it needs
// that is not there - is left out: Reload Studio runs as without it and says
// why in one line of modules.log (in its folder for temporary files) and of
// the automation log. A module whose code faults (an exception: a bad
// pointer, a division by zero) while it loads or in any callback of its page
// is turned off until Reload Studio starts again: its page goes, the line
// says where it crashed. That protection is limited - it is the same
// process: memory the module wrote over, a callback that never returns,
// threads, timers or hooks it made itself, or a module that ends the
// process are not caught.
//
// Memory: a line of RS_WM_JOB_LINE, a RsJobProgress and the text of getText
// are freed with the core's free(), never with the module's own runtime.
// This header needs nothing of Reload Studio's sources but itself.

#ifndef RS_MODULE_H
#define RS_MODULE_H

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
#include <wchar.h>

// The interface of a module: a change that an older module would read
// wrongly (this header's structures) raises it. A module made for another
// number is not loaded.
#define RS_MODULE_ABI 1

// The version of the core as one number: the tools version (CTR_TOOLS_VERSION,
// CMakeLists.txt) major.minor.patch.
#define RS_CORE_VERSION_OF(major, minor, patch) \
    (((unsigned)(major) << 16) | ((unsigned)(minor) << 8) | (unsigned)(patch))

// Pages of all modules together, built-in ones included.
#define RS_MAX_MODULES 16

// A page word: 1 to RS_MODULE_WORD_MAX characters out of a-z, 0-9 and '-'.
#define RS_MODULE_WORD_MAX 15

// What the module DLL exports.
#define RS_MODULE_ENTRY_NAME "ReloadStudioModule"
#define RS_MODULE_EXPORT __declspec(dllexport)

// ---------------------------------------------------------------------------
// The page of a module (the comment "Pages" in reloadstudio.h says how the
// shell lays it out)
// ---------------------------------------------------------------------------

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

// A clickable message list sends WM_COMMAND with this notification code
// (Rs_MsgListSetClickable, reloadstudio.h).
#define RS_MSGN_CLICK 1

// Severity of a message, for the message list and the colour of labels.
enum RsSeverity {
    RS_SEV_OK = 0,      // green: built, present
    RS_SEV_INFO,        // grey: for information only
    RS_SEV_NOTE,        // blue: note, e.g. "not playable in CTR Reload yet"
    RS_SEV_WARNING,     // amber: builds, but take a look
    RS_SEV_ERROR,       // red: does not build
    RS_SEV_COUNT
};

enum RsFont {
    RS_FONT_BODY = 0,   // Segoe UI 10 pt - default for all controls
    RS_FONT_BOLD,       // Segoe UI Semibold 10 pt - labels of fields
    RS_FONT_SMALL,      // Segoe UI 9 pt - reasons and notes below fields
    RS_FONT_SECTION,    // Segoe UI Semibold 12 pt - card titles
    RS_FONT_TITLE,      // Segoe UI Semibold 20 pt - page titles (painted by the shell)
    RS_FONT_MONO,       // Consolas 9 pt - paths, raw output
    RS_FONT_COUNT
};

// ---------------------------------------------------------------------------
// The module
// ---------------------------------------------------------------------------

struct RsModuleDef {
    unsigned abi;                   // RS_MODULE_ABI the module was made for
    unsigned size;                  // sizeof(struct RsModuleDef) of the module
    const wchar_t *word;            // the page's word: automation "page <word>", the setting "page="
    const wchar_t *icon;            // one character of Segoe MDL2 Assets for the sidebar, NULL = a default
    unsigned minCore;               // RS_CORE_VERSION_OF(...) the module needs at least, 0 = any
    const wchar_t *const *needs;    // words of the modules it needs, ending with NULL; NULL = none
    const struct RsPageDef *page;   // the page: its navName is the module's name
};

// The core's helpers for a module DLL: the functions of reloadstudio.h with
// the same name (Rs_Label -> label, ...) and the same meaning. log writes one
// line to modules.log and the automation log; showPageByWord shows the page of
// a module (0 = no such page, or turned off).
struct RsCoreApi {
    unsigned abi;                   // RS_MODULE_ABI
    unsigned size;                  // sizeof(struct RsCoreApi) of the core
    unsigned coreVersion;           // RS_CORE_VERSION_OF(...) of this Reload Studio
    const wchar_t *versionText;     // L"0.9.0"
    const wchar_t *buildId;         // the build ID (as in the title bar)

    void (__cdecl *log)(const wchar_t *fmt, ...);
    void *(*alloc)(size_t bytes);
    void (*free)(void *p);
    wchar_t *(*dup)(const wchar_t *s);

    int (*px)(int px96);
    HFONT (*font)(int font);
    COLORREF (*severityColor)(int severity);
    int (*pageTop)(void);
    int (*pageHeadBottom)(HWND page, int w);
    void (*pageViewSize)(HWND page, int *w, int *h);
    void (*cardClear)(HWND page);
    void (*cardAdd)(HWND page, const RECT *outer, const wchar_t *title);
    RECT (*cardInner)(const RECT *outer, int hasTitle);

    HWND (*label)(HWND page, int id, const wchar_t *text, int font);
    HWND (*edit)(HWND page, int id, const wchar_t *text, DWORD extraStyle);
    HWND (*button)(HWND page, int id, const wchar_t *text);
    HWND (*primaryButton)(HWND page, int id, const wchar_t *text);
    HWND (*check)(HWND page, int id, const wchar_t *text);
    HWND (*combo)(HWND page, int id);
    HWND (*listBox)(HWND page, int id, DWORD extraStyle);
    void (*setText)(HWND control, const wchar_t *text);
    wchar_t *(*getText)(HWND control);
    void (*setTextColor)(HWND control, COLORREF color);
    void (*setTip)(HWND control, const wchar_t *text);
    HWND (*msgList)(HWND page, int id);
    void (*msgListClear)(HWND list);
    void (*msgListAdd)(HWND list, int severity, const wchar_t *text, const wchar_t *detail);

    void (*configGet)(const wchar_t *key, wchar_t *out, int outCap);
    void (*configSet)(const wchar_t *key, const wchar_t *value);
    void (*tempDir)(wchar_t *out, int cap);
    const wchar_t *(*exeDir)(void);
    int (*fileExists)(const wchar_t *path);
    int (*dirExists)(const wchar_t *path);
    void (*pathJoin)(wchar_t *out, int outCap, const wchar_t *dir, const wchar_t *name);
    int (*browseFolder)(HWND owner, const wchar_t *title, const wchar_t *initial, wchar_t *out, int outCap);
    int (*browseOpenFile)(HWND owner, const wchar_t *title, const wchar_t *filter,
                          const wchar_t *initial, wchar_t *out, int outCap);
    void (*tell)(HWND owner, const wchar_t *title, const wchar_t *text);
    int (*askYesNo)(HWND owner, const wchar_t *title, const wchar_t *text);
    int (*automating)(void);

    int (*runRldpack)(HWND notify, const wchar_t *const *args, int argc);
    int (*runProcess)(HWND notify, const wchar_t *exe, const wchar_t *cmdline, const wchar_t *cwd, int capture);
    int (*jobCancel)(int id);
    int (*splitMachine)(wchar_t *line, wchar_t **fields, int maxFields);
    void (*appendArg)(wchar_t *cmdline, size_t cap, const wchar_t *arg);
    int (*showPageByWord)(const wchar_t *word);
};

typedef const struct RsModuleDef *(__cdecl *RsModuleEntry)(const struct RsCoreApi *core);

#endif
