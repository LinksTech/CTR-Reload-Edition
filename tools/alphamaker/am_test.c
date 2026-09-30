// am_test.c - page "Test in game": start CTR Reload with a container
//
// Starts ctr_native.exe with --dev --autoload-track <file>. The game loads
// the container like the NITRO-PIT row and jumps straight into the race (Arcade,
// 1 player, 3 laps). After the end the page reads the run's log (--log
// in %TEMP%\CTR Reload Alpha-Maker) and says whether the race started.
//
// If the container is not in <game folder>/tracks, the game gets
// --tracks-dir <folder> --settings-defaults: it then reads only this folder,
// keeps the level IDs in memory only (no track-ids.tsv there) and leaves
// its settings alone. The page itself writes neither track-ids.tsv
// nor cups.txt.
//
// The page looks for the game folder like the game does (NativeAssets_Init): folder of the
// exe, then parent, then grandparent - the first one whose subfolder assets
// contains BIGFILE.BIG or ctr-u.bin.

#include "alphamaker.h"
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Controls. Never 1 or 2: IsDialogMessage sends Enter as IDOK and Esc
// as IDCANCEL.
#define TEST_ID_EXE_LABEL    100
#define TEST_ID_EXE          101
#define TEST_ID_EXE_BROWSE   102
#define TEST_ID_VERSION      103
#define TEST_ID_BASE         104
#define TEST_ID_TRACKS       105
#define TEST_ID_COMBO        110
#define TEST_ID_CHOOSE       111
#define TEST_ID_INFO         112
#define TEST_ID_NAV          113
#define TEST_ID_WHERE1       114
#define TEST_ID_WHERE2       115
#define TEST_ID_WINDOWED     120
#define TEST_ID_DRIVER_LABEL 121
#define TEST_ID_DRIVER       122
#define TEST_ID_AUTOPILOT    123
#define TEST_ID_ARGS_LABEL   124
#define TEST_ID_ARGS         125
#define TEST_ID_START        130
#define TEST_ID_HEADLINE     131
#define TEST_ID_LIST         132
#define TEST_ID_OUTPUT       133
#define TEST_ID_SHOWLOG      134
#define TEST_ID_TOGGLE       135

#define TEST_TIMER_EXE    7001    // debounce of the program path
#define TEST_TIMER_PROBE  7002    // --version does not answer

#define TEST_PATH        (MAX_PATH * 2)
#define TEST_CMD_CAP     8192
#define TEST_MAX_FOLDER  64                     // containers from the tracks folder
#define TEST_MAX_ITEMS   (TEST_MAX_FOLDER + 1)  // plus one file from elsewhere
#define TEST_MAX_OUT     4000                   // remembered output lines of the game
#define TEST_MAX_LOG     2000                   // remembered log lines
#define TEST_VIEW_LINES  400                    // lines in the view "game output"
#define TEST_DRIVERS     16

// Card heights in 96-dpi pixels. Am_CardInner takes 50 at the top and 16 at the bottom.
#define TEST_CARD_CHROME  66
#define TEST_GAME_INNER   112
#define TEST_TRACK_INNER  128
#define TEST_OPT_INNER    146

enum TestInfoState {
    TEST_INFO_NONE = 0,     // no container
    TEST_INFO_RUNNING,      // rldpack info running
    TEST_INFO_OK,           // offers Race
    TEST_INFO_REFUSED,      // rldpack: refused
    TEST_INFO_NORACE,       // no Race
    TEST_INFO_UNKNOWN       // check did not work - the game decides
};

struct TestLines {
    wchar_t **v;
    int count;
    int cap;
    int dropped;
};

struct TestBuf {
    wchar_t *p;
    size_t len;
    size_t cap;
};

struct TestState {
    HWND exeLabel, exe, exeBrowse, version, base, tracks;
    HWND combo, choose, info, nav, where1, where2;
    HWND windowed, driverLabel, driver, autopilot, argsLabel, args;
    HWND start, headline, list, output, showLog, toggle;

    int quiet;                      // the program sets the text: ignore EN_CHANGE
    int exePending;                 // debounce running

    // Game
    wchar_t exePath[TEST_PATH];     // full path, empty = none
    wchar_t baseDir[TEST_PATH];     // game folder, empty = not found
    wchar_t tracksDir[TEST_PATH];   // <game folder>\tracks
    int noBase;                     // exe there, but no assets folder
    int versionJob;
    int exeOk;                      // --version: CTR Reload with the same build ID
    int exeMismatch;                // --version: other build or old version "CTR Native"
    wchar_t versionText[256];
    // Switch to the game next to the Alpha-Maker when the entered one comes from
    // another package (e.g. an old test.exe in the ini).
    int exeSwitchUsed;              // at most once per session, never back
    int exeStartup;                 // the check applies to the game from the ini at start
    int exeSwitchNote;              // the running check is the one after the switch
    wchar_t exeSwitchFrom[TEST_PATH];   // rejected program
    wchar_t exeSwitchFromVersion[256];  // its line from --version

    // Container
    wchar_t items[TEST_MAX_ITEMS][TEST_PATH];
    int itemCount;
    wchar_t container[TEST_PATH];   // chosen, full path
    int infoJob;
    int infoState;
    wchar_t infoPath[TEST_PATH];    // info last ran for this one
    FILETIME infoTime;
    int infoSawContainer;
    int infoRefused;
    int infoRace;                   // -1 unknown
    int infoNav;                    // -1 unknown
    wchar_t infoName[128];
    wchar_t infoAuthor[128];
    wchar_t infoModes[128];
    wchar_t infoReason[512];

    // Last start
    int gameJob;
    int ran;                        // at least one start attempted
    int startFailed;
    int haveExit;
    DWORD exitCode;
    int raceStarted;
    int logFound;
    wchar_t runExe[TEST_PATH];
    wchar_t runCmd[TEST_CMD_CAP];
    wchar_t logPath[TEST_PATH];
    struct TestLines out;           // stdout/stderr of the game
    struct TestLines log;           // log lines with the prefixes below
    int showOutput;
};

static struct TestState g_test;

static void Test_ApplyNav(void);

static const wchar_t *const g_testDrivers[TEST_DRIVERS] = {
    L"Crash Bandicoot", L"Dr. Neo Cortex", L"Tiny Tiger", L"Coco Bandicoot",
    L"N. Gin", L"Dingodile", L"Polar", L"Pura",
    L"Pinstripe", L"Papu Papu", L"Ripper Roo", L"Komodo Joe",
    L"N. Tropy", L"Penta Penguin", L"Fake Crash", L"N. Oxide"
};

static const wchar_t *const g_testLogPrefixes[] = {
    L"[CTR Tracks]", L"[CTR Debug]", L"[CTR Menu]", L"[CTR Race]", L"[CTR Cups]", L"[CTR Cup]"
};

// ---------------------------------------------------------------------------
// Text and paths
// ---------------------------------------------------------------------------

static void Test_Copy(wchar_t *dst, int cap, const wchar_t *src)
{
    if (cap <= 0)
        return;
    wcsncpy(dst, src ? src : L"", (size_t)cap - 1);
    dst[cap - 1] = 0;
}

// Whitespace away; with quotes = 1 also enclosing quotation marks.
static void Test_Trim(const wchar_t *in, wchar_t *out, int cap, int quotes)
{
    const wchar_t *s = in ? in : L"";
    const wchar_t *e;
    size_t n;

    if (cap <= 0)
        return;
    while (*s == L' ' || *s == L'\t' || *s == L'\r' || *s == L'\n')
        s++;
    e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n'))
        e--;
    if (quotes && e - s >= 2 && *s == L'"' && e[-1] == L'"') {
        s++;
        e--;
    }
    n = (size_t)(e - s);
    if (n >= (size_t)cap)
        n = (size_t)cap - 1;
    memcpy(out, s, n * sizeof(wchar_t));
    out[n] = 0;
}

static int Test_StartsWith(const wchar_t *s, const wchar_t *prefix)
{
    return wcsncmp(s, prefix, wcslen(prefix)) == 0;
}

// Text between from and to (to = NULL: up to the end). Empty if from is missing.
static void Test_Between(const wchar_t *line, const wchar_t *from, const wchar_t *to,
                         wchar_t *out, int cap)
{
    const wchar_t *p = wcsstr(line, from);
    const wchar_t *e;
    size_t n;

    out[0] = 0;
    if (!p)
        return;
    p += wcslen(from);
    e = to ? wcsstr(p, to) : NULL;
    n = e ? (size_t)(e - p) : wcslen(p);
    if (n >= (size_t)cap)
        n = (size_t)cap - 1;
    memcpy(out, p, n * sizeof(wchar_t));
    out[n] = 0;
}

static void Test_StripSep(wchar_t *path)
{
    size_t n = wcslen(path);
    while (n > 0 && (path[n - 1] == L'\\' || path[n - 1] == L'/'))
        path[--n] = 0;
}

// Full path without a trailing slash (a drive root stays C:\).
static void Test_FullPath(const wchar_t *in, wchar_t *out, int cap)
{
    wchar_t root[4];
    DWORD n;

    if (!in || !in[0]) {
        out[0] = 0;
        return;
    }
    // "C:" alone would mean "current folder on C:"
    if (in[1] == L':' && in[2] == 0) {
        root[0] = in[0];
        root[1] = L':';
        root[2] = L'\\';
        root[3] = 0;
        in = root;
    }
    n = GetFullPathNameW(in, (DWORD)cap, out, NULL);
    if (n == 0 || n >= (DWORD)cap)
        Test_Copy(out, cap, in);
    n = (DWORD)wcslen(out);
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
}

static int Test_SameDir(const wchar_t *a, const wchar_t *b)
{
    wchar_t fa[TEST_PATH];
    wchar_t fb[TEST_PATH];

    if (!a || !b || !a[0] || !b[0])
        return 0;
    Test_FullPath(a, fa, TEST_PATH);
    Test_FullPath(b, fb, TEST_PATH);
    Test_StripSep(fa);
    Test_StripSep(fb);
    return _wcsicmp(fa, fb) == 0;
}

// File name in the spelling on disk: the game compares with strcmp.
static void Test_DiskName(const wchar_t *path, wchar_t *out, int cap)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        Test_Copy(out, cap, fd.cFileName);
        FindClose(h);
    } else {
        Test_Copy(out, cap, Am_PathName(path));
    }
}

static void Test_FileTime(const wchar_t *path, FILETIME *out)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    memset(out, 0, sizeof(*out));
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        *out = data.ftLastWriteTime;
}

static int Test_HasAssets(const wchar_t *dir)
{
    wchar_t assets[TEST_PATH];
    wchar_t probe[TEST_PATH];

    Am_PathJoin(assets, TEST_PATH, dir, L"assets");
    Am_PathJoin(probe, TEST_PATH, assets, L"BIGFILE.BIG");
    if (Am_FileExists(probe))
        return 1;
    Am_PathJoin(probe, TEST_PATH, assets, L"ctr-u.bin");
    return Am_FileExists(probe);
}

// Like NativeAssets_Init in the game: folder of the exe, parent, grandparent.
static int Test_FindBase(const wchar_t *exe, wchar_t *out, int cap)
{
    wchar_t dir[TEST_PATH];
    wchar_t up[TEST_PATH];
    int i;

    Am_PathDir(dir, TEST_PATH, exe);
    for (i = 0; i < 3 && dir[0]; i++) {
        if (Test_HasAssets(dir)) {
            Test_Copy(out, cap, dir);
            return 1;
        }
        Am_PathDir(up, TEST_PATH, dir);
        if (wcscmp(up, dir) == 0)
            break;
        Test_Copy(dir, TEST_PATH, up);
    }
    out[0] = 0;
    return 0;
}

// Every test run gets its own log with a timestamp; the last
// five are kept (Am_RotatedLogPath).
static void Test_MakeLogPath(wchar_t *out, int cap)
{
    Am_RotatedLogPath(out, cap, L"game-test", 5);
}

static int Test_IsLogLine(const wchar_t *line)
{
    size_t i;
    for (i = 0; i < sizeof(g_testLogPrefixes) / sizeof(g_testLogPrefixes[0]); i++)
        if (Test_StartsWith(line, g_testLogPrefixes[i]))
            return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// Buffers and line lists
// ---------------------------------------------------------------------------

static void Test_BufAdd(struct TestBuf *b, const wchar_t *s)
{
    size_t n;
    if (!s)
        s = L"";
    n = wcslen(s);
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        wchar_t *p;
        while (cap < b->len + n + 1)
            cap *= 2;
        p = Am_Alloc(cap * sizeof(wchar_t));
        if (b->len)
            memcpy(p, b->p, b->len * sizeof(wchar_t));
        Am_Free(b->p);
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n * sizeof(wchar_t));
    b->len += n;
    b->p[b->len] = 0;
}

// Takes ownership of line; beyond max it is only counted.
static void Test_LinesPush(struct TestLines *l, wchar_t *line, int max)
{
    if (l->count >= max) {
        Am_Free(line);
        l->dropped++;
        return;
    }
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        wchar_t **v = Am_Alloc((size_t)cap * sizeof(wchar_t *));
        if (l->count)
            memcpy(v, l->v, (size_t)l->count * sizeof(wchar_t *));
        Am_Free(l->v);
        l->v = v;
        l->cap = cap;
    }
    l->v[l->count++] = line;
}

static void Test_LinesFree(struct TestLines *l)
{
    int i;
    for (i = 0; i < l->count; i++)
        Am_Free(l->v[i]);
    Am_Free(l->v);
    memset(l, 0, sizeof(*l));
}

// ---------------------------------------------------------------------------
// Small helpers for the front end
// ---------------------------------------------------------------------------

static void Test_SetLabel(HWND label, const wchar_t *text, COLORREF color)
{
    Am_SetTextColor(label, color);
    Am_SetText(label, text);
}

// Single line with "..." (SS_ENDELLIPSIS, SS_PATHELLIPSIS) or wrapping with 0.
static void Test_SetEllipsis(HWND label, LONG_PTR ellipsis)
{
    LONG_PTR style = GetWindowLongPtrW(label, GWL_STYLE);
    SetWindowLongPtrW(label, GWL_STYLE, (style & ~(LONG_PTR)SS_ELLIPSISMASK) | ellipsis);
    InvalidateRect(label, NULL, TRUE);
}

static int Test_Checked(HWND box)
{
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static int Test_Driver(void)
{
    int d = (int)SendMessageW(g_test.driver, CB_GETCURSEL, 0, 0);
    return (d >= 0 && d < TEST_DRIVERS) ? d : 0;
}

static int Test_OnOff(const wchar_t *arg)
{
    if (_wcsicmp(arg, L"on") == 0 || wcscmp(arg, L"1") == 0)
        return 1;
    if (_wcsicmp(arg, L"off") == 0 || wcscmp(arg, L"0") == 0)
        return 0;
    return -1;
}

// "<exe>" <command line> of the last start (Am_Free).
static wchar_t *Test_CmdText(void)
{
    size_t cap = wcslen(g_test.runExe) * 2 + wcslen(g_test.runCmd) + 8;
    wchar_t *t = Am_Alloc(cap * sizeof(wchar_t));
    Am_AppendArg(t, cap, g_test.runExe);
    if (g_test.runCmd[0]) {
        size_t n = wcslen(t);
        t[n] = L' ';
        wcscpy(t + n + 1, g_test.runCmd);
    }
    return t;
}

// Why "Start game" does not work; NULL = it works.
static const wchar_t *Test_WhyNot(void)
{
    if (g_test.gameJob)
        return L"The game is already running.";
    if (!g_test.exePath[0])
        return L"Choose the game program (ctr_native.exe) first.";
    if (!Am_FileExists(g_test.exePath))
        return L"The game program was not found - check the Game card.";
    if (g_test.versionJob || g_test.exePending)
        return L"Checking the game program...";
    if (g_test.exeMismatch)
        return L"The game program is not from the same package as this Alpha-Maker - check the Game card.";
    if (!g_test.exeOk)
        return L"The game program did not answer like CTR Reload - check the Game card.";
    if (!g_test.baseDir[0])
        return L"CTR Reload has no game data yet - see the Game card.";
    if (!g_test.container[0])
        return L"Choose a track container.";
    if (!Am_FileExists(g_test.container))
        return L"The track container was not found.";
    if (g_test.infoJob)
        return L"Checking the container...";
    if (g_test.infoState == TEST_INFO_REFUSED || g_test.infoState == TEST_INFO_NORACE)
        return L"This container cannot be started - see the Track card.";
    return NULL;
}

static void Test_UpdateReady(void)
{
    const wchar_t *why = Test_WhyNot();
    EnableWindow(g_test.start, why == NULL);
    // Before the first test the heading says what is still missing.
    if (!g_test.ran && !g_test.gameJob)
        Test_SetLabel(g_test.headline, why ? why : L"Ready - press Start game.",
                      why ? AM_COL_MUTED : AM_COL_TEXT);
}

static void Test_UpdateButtons(void)
{
    EnableWindow(g_test.showLog, g_test.ran && !g_test.startFailed);
    EnableWindow(g_test.toggle, g_test.ran);
}

// ---------------------------------------------------------------------------
// Layout
//
// On the left Game, Track, Options on top of each other (Track takes the rest), on the right
// Result over the whole height. If the height is not enough for that, Options moves
// to the right above Result.
// ---------------------------------------------------------------------------

// Height a wrapping label needs for its current text at width w,
// at least minPx96 (96-dpi pixels).
static int Test_WrapHeight(HWND label, int w, int minPx96)
{
    wchar_t *text = Am_GetText(label);
    int h = Am_Px(minPx96);

    if (text[0] && w > 0) {
        HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
        HDC dc = GetDC(label);
        HGDIOBJ old = SelectObject(dc, font ? font : Am_Font(AM_FONT_SMALL));
        RECT r;
        SetRect(&r, 0, 0, w, 0);
        DrawTextW(dc, text, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(label, dc);
        if (r.bottom > h)
            h = r.bottom;
    }
    Am_Free(text);
    return h;
}

// Height of the "version" line and of the game folder label. Both messages
// (foreign build, no game data) are long and wrap; the card Game
// then grows by the rest (Test_GameExtra).
static void Test_GameHeights(int w, int *versionH, int *baseH)
{
    *versionH = Test_WrapHeight(g_test.version, w, 18);
    // Without a game folder the error message is in both places (base, tracks).
    *baseH = g_test.noBase ? Test_WrapHeight(g_test.base, w, 36) : Am_Px(18);
}

static int Test_GameExtra(int w)
{
    int vh, bh;
    Test_GameHeights(w, &vh, &bh);
    return (vh - Am_Px(18)) + (g_test.noBase ? bh - Am_Px(36) : 0);
}

static void Test_LayoutGame(const RECT *in)
{
    int x = in->left, y = in->top, w = in->right - in->left;
    int bw = Am_Px(104);
    int vh, bh;

    Test_GameHeights(w, &vh, &bh);
    MoveWindow(g_test.exeLabel, x, y, w, Am_Px(20), TRUE);
    y += Am_Px(22);
    MoveWindow(g_test.exe, x, y + Am_Px(2), w - bw - Am_Px(8), Am_Px(28), TRUE);
    MoveWindow(g_test.exeBrowse, in->right - bw, y, bw, Am_Px(32), TRUE);
    y += Am_Px(36);
    MoveWindow(g_test.version, x, y, w, vh, TRUE);
    y += vh;
    MoveWindow(g_test.base, x, y, w, bh, TRUE);
    MoveWindow(g_test.tracks, x, y + Am_Px(18), w, Am_Px(18), TRUE);
}

static void Test_LayoutTrack(const RECT *in)
{
    int x = in->left, y = in->top, w = in->right - in->left;
    int cw = Am_Px(124);

    SendMessageW(g_test.combo, CB_SETITEMHEIGHT, (WPARAM)-1, Am_Px(22));
    MoveWindow(g_test.combo, x, y + Am_Px(2), w - cw - Am_Px(8), Am_Px(300), TRUE);
    MoveWindow(g_test.choose, in->right - cw, y, cw, Am_Px(32), TRUE);
    y += Am_Px(38);
    MoveWindow(g_test.info, x, y, w, Am_Px(36), TRUE);
    y += Am_Px(36);
    MoveWindow(g_test.nav, x, y, w, Am_Px(18), TRUE);
    y += Am_Px(18);
    MoveWindow(g_test.where1, x, y, w, Am_Px(18), TRUE);
    y += Am_Px(18);
    MoveWindow(g_test.where2, x, y, w, Am_Px(18), TRUE);
}

static void Test_LayoutOptions(const RECT *in)
{
    int x = in->left, y = in->top, w = in->right - in->left;
    int dw = w - Am_Px(72);

    if (dw > Am_Px(260))
        dw = Am_Px(260);
    MoveWindow(g_test.windowed, x, y, w, Am_Px(24), TRUE);
    y += Am_Px(30);
    MoveWindow(g_test.driverLabel, x, y + Am_Px(4), Am_Px(64), Am_Px(20), TRUE);
    SendMessageW(g_test.driver, CB_SETITEMHEIGHT, (WPARAM)-1, Am_Px(22));
    MoveWindow(g_test.driver, x + Am_Px(72), y, dw, Am_Px(300), TRUE);
    y += Am_Px(34);
    MoveWindow(g_test.autopilot, x, y, w, Am_Px(24), TRUE);
    y += Am_Px(32);
    MoveWindow(g_test.argsLabel, x, y, w, Am_Px(20), TRUE);
    y += Am_Px(22);
    MoveWindow(g_test.args, x, y, w, Am_Px(28), TRUE);
}

static void Test_LayoutResult(const RECT *in)
{
    int x = in->left, y = in->top, w = in->right - in->left;
    int bottomY = in->bottom - Am_Px(32);
    int listH;

    MoveWindow(g_test.start, x, y, Am_Px(160), Am_Px(32), TRUE);
    y += Am_Px(44);
    MoveWindow(g_test.headline, x, y, w, Am_Px(40), TRUE);
    y += Am_Px(46);
    listH = bottomY - Am_Px(12) - y;
    if (listH < Am_Px(40))
        listH = Am_Px(40);
    MoveWindow(g_test.list, x, y, w, listH, TRUE);
    MoveWindow(g_test.output, x, y, w, listH, TRUE);
    // After a dpi change the shell has set the default font.
    if ((HFONT)SendMessageW(g_test.output, WM_GETFONT, 0, 0) != Am_Font(AM_FONT_MONO))
        SendMessageW(g_test.output, WM_SETFONT, (WPARAM)Am_Font(AM_FONT_MONO), TRUE);
    MoveWindow(g_test.showLog, x, bottomY, Am_Px(128), Am_Px(32), TRUE);
    MoveWindow(g_test.toggle, x + Am_Px(136), bottomY, Am_Px(156), Am_Px(32), TRUE);
}

static void Test_Layout(HWND page, int w, int h)
{
    int left = Am_Px(32), right = w - Am_Px(32);
    int top = Am_PageTop(), bottom = h - Am_Px(24);
    int gap = Am_Px(16);
    int colW = (right - left - gap) / 2;
    int rx = left + colW + gap;
    int gameH = Am_Px(TEST_CARD_CHROME + TEST_GAME_INNER);
    int trackH = Am_Px(TEST_CARD_CHROME + TEST_TRACK_INNER);
    int optH = Am_Px(TEST_CARD_CHROME + TEST_OPT_INNER);
    int stacked;
    RECT rc, in;

    // Long messages on the card Game make it taller (Track takes less).
    SetRect(&rc, left, top, left + colW, top + gameH);
    in = Am_CardInner(&rc, 1);
    gameH += Test_GameExtra(in.right - in.left);
    stacked = bottom - top >= gameH + gap + trackH + gap + optH;

    Am_CardClear(page);

    SetRect(&rc, left, top, left + colW, top + gameH);
    Am_CardAdd(page, &rc, L"Game");
    in = Am_CardInner(&rc, 1);
    Test_LayoutGame(&in);

    SetRect(&rc, left, top + gameH + gap, left + colW, stacked ? bottom - optH - gap : bottom);
    Am_CardAdd(page, &rc, L"Track");
    in = Am_CardInner(&rc, 1);
    Test_LayoutTrack(&in);

    if (stacked)
        SetRect(&rc, left, bottom - optH, left + colW, bottom);
    else
        SetRect(&rc, rx, top, right, top + optH);
    Am_CardAdd(page, &rc, L"Options");
    in = Am_CardInner(&rc, 1);
    Test_LayoutOptions(&in);

    SetRect(&rc, rx, stacked ? top : top + optH + gap, right, bottom);
    Am_CardAdd(page, &rc, L"Result");
    in = Am_CardInner(&rc, 1);
    Test_LayoutResult(&in);
}

static void Test_Relayout(HWND page)
{
    RECT rc;
    GetClientRect(page, &rc);
    if (rc.right >= Am_Px(400) && rc.bottom >= Am_Px(300))
        Test_Layout(page, rc.right, rc.bottom);
}

// ---------------------------------------------------------------------------
// Container: list, check with rldpack info
// ---------------------------------------------------------------------------

static int __cdecl Test_CompareItems(const void *a, const void *b)
{
    return _wcsicmp(Am_PathName((const wchar_t *)a), Am_PathName((const wchar_t *)b));
}

// Rebuilds the list: *.rldtrack from the tracks folder (flat, at most 64),
// plus the chosen container if it lies elsewhere.
static void Test_FillCombo(void)
{
    wchar_t pattern[TEST_PATH];
    wchar_t text[TEST_PATH * 2];
    wchar_t dir[TEST_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int i, sel = -1, folderCount;

    g_test.itemCount = 0;
    if (g_test.tracksDir[0]) {
        Am_PathJoin(pattern, TEST_PATH, g_test.tracksDir, L"*.rldtrack");
        h = FindFirstFileW(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !ext || _wcsicmp(ext, L".rldtrack") != 0)
                    continue;
                Am_PathJoin(g_test.items[g_test.itemCount], TEST_PATH, g_test.tracksDir, fd.cFileName);
                g_test.itemCount++;
            } while (g_test.itemCount < TEST_MAX_FOLDER && FindNextFileW(h, &fd));
            FindClose(h);
        }
        qsort(g_test.items, (size_t)g_test.itemCount, sizeof(g_test.items[0]), Test_CompareItems);
    }
    folderCount = g_test.itemCount;

    if (g_test.container[0] && !Am_FileExists(g_test.container))
        g_test.container[0] = 0;
    for (i = 0; i < folderCount; i++)
        if (_wcsicmp(g_test.items[i], g_test.container) == 0)
            sel = i;
    if (g_test.container[0] && sel < 0) {
        Test_Copy(g_test.items[g_test.itemCount], TEST_PATH, g_test.container);
        sel = g_test.itemCount++;
    }
    if (!g_test.container[0] && g_test.itemCount > 0) {
        Test_Copy(g_test.container, TEST_PATH, g_test.items[0]);
        sel = 0;
    }

    SendMessageW(g_test.combo, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_test.itemCount; i++) {
        if (i < folderCount) {
            Test_Copy(text, TEST_PATH * 2, Am_PathName(g_test.items[i]));
        } else {
            Am_PathDir(dir, TEST_PATH, g_test.items[i]);
            swprintf(text, TEST_PATH * 2, L"%ls - %ls", Am_PathName(g_test.items[i]), dir);
        }
        SendMessageW(g_test.combo, CB_ADDSTRING, 0, (LPARAM)text);
    }
    SendMessageW(g_test.combo, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void Test_UpdateWhere(void)
{
    wchar_t dir[TEST_PATH];
    wchar_t line[TEST_PATH + 64];

    if (!g_test.container[0]) {
        Test_SetLabel(g_test.where1, L"", AM_COL_MUTED);
        Test_SetLabel(g_test.where2, L"", AM_COL_MUTED);
        return;
    }
    Am_PathDir(dir, TEST_PATH, g_test.container);
    if (Test_SameDir(dir, g_test.tracksDir)) {
        Test_SetLabel(g_test.where1, L"The game loads it from its own tracks folder.", AM_COL_MUTED);
        Test_SetLabel(g_test.where2, L"", AM_COL_MUTED);
    } else {
        swprintf(line, TEST_PATH + 64, L"The game loads it from %ls", dir);
        Test_SetLabel(g_test.where1, line, AM_COL_MUTED);
        Test_SetLabel(g_test.where2, L"as a test folder. Your game settings and level ids stay untouched.",
                      AM_COL_MUTED);
    }
}

static int Test_RunInfo(HWND page)
{
    const wchar_t *args[3];

    g_test.infoSawContainer = 0;
    g_test.infoRefused = 0;
    g_test.infoRace = -1;
    g_test.infoNav = -1;
    g_test.infoName[0] = 0;
    g_test.infoAuthor[0] = 0;
    g_test.infoModes[0] = 0;
    g_test.infoReason[0] = 0;
    Test_Copy(g_test.infoPath, TEST_PATH, g_test.container);
    Test_FileTime(g_test.container, &g_test.infoTime);

    args[0] = L"info";
    args[1] = L"--machine";
    args[2] = g_test.container;
    g_test.infoJob = Am_RunRldpack(page, args, 3);
    Test_ApplyNav();
    if (!g_test.infoJob) {
        g_test.infoState = TEST_INFO_UNKNOWN;
        Test_SetLabel(g_test.info, L"The container could not be checked - the checker did not start.",
                      AM_COL_WARNING);
        return 0;
    }
    g_test.infoState = TEST_INFO_RUNNING;
    Test_SetLabel(g_test.info, L"Checking the container...", AM_COL_MUTED);
    return 1;
}

// Checks the container if it is new or has changed since the last check
// (or force). Return 1 = info started.
static int Test_EnsureInfo(HWND page, int force)
{
    FILETIME t;
    int started = 0;

    if (!g_test.container[0]) {
        g_test.infoJob = 0;
        g_test.infoState = TEST_INFO_NONE;
        g_test.infoPath[0] = 0;
        const wchar_t *empty = L"There is no track container in the game's tracks folder. Use Choose file...";
        Test_SetLabel(g_test.info, g_test.tracksDir[0] ? empty : L"", AM_COL_MUTED);
        g_test.infoNav = -1;
        Test_ApplyNav();
    } else {
        Test_FileTime(g_test.container, &t);
        if (force || g_test.infoState == TEST_INFO_NONE || _wcsicmp(g_test.infoPath, g_test.container) != 0 ||
            CompareFileTime(&t, &g_test.infoTime) != 0)
            started = Test_RunInfo(page);
    }
    Test_UpdateReady();
    return started;
}

// refill = 1: rebuild the list (not from within CBN_SELCHANGE).
static int Test_SetContainer(HWND page, const wchar_t *path, int refill, int force)
{
    Test_FullPath(path, g_test.container, TEST_PATH);
    Am_ConfigSet(L"test.container", g_test.container);
    if (refill)
        Test_FillCombo();
    Test_UpdateWhere();
    return Test_EnsureInfo(page, force);
}

// Nav paths: without them the game creates no opponent
// (BOTS_Driver_Init, BOTS.c:3112-3135), and --autopilot cannot convert the
// seat. Both are stated before the start; "Auto drive" is then locked.
static void Test_ApplyNav(void)
{
    int none = g_test.infoNav == 0 &&
               (g_test.infoState == TEST_INFO_OK || g_test.infoState == TEST_INFO_NORACE);
    if (none)
        Test_SetLabel(g_test.nav, L"No nav paths: no bots, you drive alone; auto drive is locked.", AM_COL_WARNING);
    else
        Test_SetLabel(g_test.nav, L"", AM_COL_WARNING);
    EnableWindow(g_test.autopilot, !none);
    Am_SetText(g_test.autopilot, none ? L"Let the kart drive itself (needs nav paths - this track has none)"
                                      : L"Let the kart drive itself (autopilot)");
}

static void Test_InfoLine(wchar_t *line)
{
    wchar_t *f[8];
    int n = Am_SplitMachine(line, f, 8);

    if (n < 1)
        return;
    if (wcscmp(f[0], L"container") == 0) {
        g_test.infoSawContainer = 1;
        if (n >= 3 && wcscmp(f[2], L"refused") == 0) {
            g_test.infoRefused = 1;
            Test_Copy(g_test.infoReason, 512, n >= 4 ? f[3] : L"");
        }
    } else if (wcscmp(f[0], L"value") == 0 && n >= 3) {
        if (wcscmp(f[1], L"name") == 0)
            Test_Copy(g_test.infoName, 128, f[2]);
        else if (wcscmp(f[1], L"author") == 0)
            Test_Copy(g_test.infoAuthor, 128, f[2]);
        else if (wcscmp(f[1], L"modes") == 0)
            Test_Copy(g_test.infoModes, 128, f[2]);
    } else if (wcscmp(f[0], L"mode") == 0 && n >= 5 && wcscmp(f[1], L"race") == 0) {
        // Fields: data, declared, playable - the game offers Race only when declared
        g_test.infoRace = wcscmp(f[3], L"yes") == 0 && wcscmp(f[4], L"yes") == 0;
    } else if (wcscmp(f[0], L"lev") == 0 && n >= 3 && wcscmp(f[1], L"nav_paths") == 0) {
        g_test.infoNav = _wtoi(f[2]);
    }
}

static void Test_InfoDone(int code)
{
    wchar_t text[768];
    COLORREF color = AM_COL_OK;
    int race;

    g_test.infoJob = 0;
    if (g_test.infoRefused) {
        g_test.infoState = TEST_INFO_REFUSED;
        swprintf(text, 768, L"The game refuses this container: %ls",
                 g_test.infoReason[0] ? g_test.infoReason : L"no reason given");
        color = AM_COL_ERROR;
    } else if (!g_test.infoSawContainer) {
        g_test.infoState = TEST_INFO_UNKNOWN;
        swprintf(text, 768,
                 L"The container could not be checked (the checker ended with code %d). The game will tell more.",
                 code);
        color = AM_COL_WARNING;
    } else {
        race = g_test.infoRace;
        if (race < 0)
            race = wcsstr(g_test.infoModes, L"race") != NULL;
        if (!race) {
            g_test.infoState = TEST_INFO_NORACE;
            Test_Copy(text, 768, L"This container does not offer Race, so the game cannot start it.");
            color = AM_COL_ERROR;
        } else {
            const wchar_t *name = g_test.infoName[0] ? g_test.infoName : Am_PathName(g_test.container);
            g_test.infoState = TEST_INFO_OK;
            if (g_test.infoAuthor[0])
                swprintf(text, 768, L"%ls by %ls - offers Race", name, g_test.infoAuthor);
            else
                swprintf(text, 768, L"%ls - offers Race", name);
        }
    }
    Test_SetLabel(g_test.info, text, color);
    Test_ApplyNav();
    Test_UpdateReady();
    if (Am_Automating()) {
        Am_AutoLog(L"  container check: %ls", text);
        if (g_test.infoNav == 0)
            Am_AutoLog(L"  container check: No nav paths - no bots, auto drive locked.");
    }
}

// ---------------------------------------------------------------------------
// Game program
// ---------------------------------------------------------------------------

// Game folder for the program, labels, container list.
static void Test_ApplyBase(HWND page)
{
    wchar_t line[TEST_PATH + 80];

    g_test.baseDir[0] = 0;
    g_test.tracksDir[0] = 0;
    g_test.noBase = 0;
    if (g_test.exePath[0] && Am_FileExists(g_test.exePath)) {
        if (Test_FindBase(g_test.exePath, g_test.baseDir, TEST_PATH))
            Am_PathJoin(g_test.tracksDir, TEST_PATH, g_test.baseDir, L"tracks");
        else
            g_test.noBase = 1;
    }
    if (g_test.baseDir[0]) {
        Test_SetEllipsis(g_test.base, SS_PATHELLIPSIS);
        swprintf(line, TEST_PATH + 80, L"Game folder: %ls", g_test.baseDir);
        Test_SetLabel(g_test.base, line, AM_COL_MUTED);
        swprintf(line, TEST_PATH + 80, L"Tracks folder of the game: %ls%ls", g_test.tracksDir,
                 Am_DirExists(g_test.tracksDir) ? L"" : L" (not there yet)");
        Test_SetLabel(g_test.tracks, line, AM_COL_MUTED);
    } else if (g_test.noBase) {
        Test_SetEllipsis(g_test.base, 0);
        Test_SetLabel(g_test.base, AM_TEXT_NO_GAME_DATA, AM_COL_ERROR);
        Test_SetLabel(g_test.tracks, L"", AM_COL_MUTED);
    } else {
        Test_SetLabel(g_test.base, L"", AM_COL_MUTED);
        Test_SetLabel(g_test.tracks, L"", AM_COL_MUTED);
    }
    ShowWindow(g_test.tracks, g_test.noBase ? SW_HIDE : SW_SHOW);
    Test_Relayout(page);
    Test_FillCombo();
    Test_UpdateWhere();
    Test_EnsureInfo(page, 0);
}

// Reads the program path from the field and asks --version. 1 = question started.
static int Test_ExeApply(HWND page)
{
    wchar_t *text = Am_GetText(g_test.exe);
    wchar_t raw[TEST_PATH];
    wchar_t dir[TEST_PATH];
    int started = 0;

    Test_Trim(text, raw, TEST_PATH, 1);
    Am_Free(text);
    KillTimer(page, TEST_TIMER_PROBE);
    g_test.versionJob = 0;
    g_test.exeOk = 0;
    g_test.exeMismatch = 0;
    g_test.exeSwitchNote = 0;       // Test_SwitchToNeighbour sets it after the call
    // Only the game from the ini at start may switch automatically; whoever
    // chooses a game themselves (Browse, typing, automation) keeps it.
    if (!g_test.exeStartup)
        g_test.exeSwitchUsed = 1;
    g_test.exeStartup = 0;
    g_test.versionText[0] = 0;
    Test_FullPath(raw, g_test.exePath, TEST_PATH);
    if (g_test.exePath[0])
        Am_ConfigSet(L"test.exe", g_test.exePath);

    if (!g_test.exePath[0]) {
        Test_SetLabel(g_test.version, L"Choose ctr_native.exe, the CTR Reload game program.", AM_COL_MUTED);
    } else if (Am_DirExists(g_test.exePath)) {
        Test_SetLabel(g_test.version, L"This is a folder. Choose ctr_native.exe inside it.", AM_COL_ERROR);
    } else if (!Am_FileExists(g_test.exePath)) {
        Test_SetLabel(g_test.version, L"This file does not exist.", AM_COL_ERROR);
    } else {
        Am_PathDir(dir, TEST_PATH, g_test.exePath);
        g_test.versionJob = Am_RunProcess(page, g_test.exePath, L"--version", dir, 1);
        if (g_test.versionJob) {
            Test_SetLabel(g_test.version, L"Checking the program...", AM_COL_MUTED);
            SetTimer(page, TEST_TIMER_PROBE, 15000, NULL);
            started = 1;
        } else {
            Test_SetLabel(g_test.version, L"This program could not be started.", AM_COL_ERROR);
        }
    }
    Test_ApplyBase(page);
    Test_UpdateReady();
    return started;
}

// First line of --version: "CTR Reload Beta 0 (<build id>)". Old builds
// write "CTR Native 0.1.0-... (<id>)" - the line is remembered so that
// the message can name it, but it does not count.
static void Test_VersionLine(const wchar_t *line)
{
    if (!g_test.versionText[0] &&
        (Test_StartsWith(line, L"CTR Reload ") || Test_StartsWith(line, L"CTR Native")))
        Test_Copy(g_test.versionText, 256, line);
}

// 1 = the remembered line comes from the same package as this Alpha-Maker:
// "CTR Reload ..." and the ID in the last brackets equals its own.
// An Alpha-Maker without an ID ("unknown", build without git) does not compare.
static int Test_VersionMatches(const wchar_t *line)
{
    const wchar_t *paren;
    wchar_t id[64];

    if (!Test_StartsWith(line, L"CTR Reload "))
        return 0;
    if (wcscmp(AM_BUILD_ID_W, L"unknown") == 0)
        return 1;
    paren = wcsrchr(line, L'(');
    if (!paren)
        return 0;
    Test_Between(paren, L"(", L")", id, 64);
    return _wcsicmp(id, AM_BUILD_ID_W) == 0;
}

// The entered game comes from another package: if there is another
// ctr_native.exe next to the Alpha-Maker, switch to it once per session -
// only for the game from the ini at start (Test_ExeApply otherwise sets
// exeSwitchUsed) - and check again. Loop-free: exeSwitchUsed is set before the switch
// and never reset; if the neighbouring game does not fit either, the
// current message stays. 1 = switched.
static int Test_SwitchToNeighbour(HWND page)
{
    wchar_t probe[TEST_PATH];
    wchar_t full[TEST_PATH];

    if (g_test.exeSwitchUsed || !g_test.exeMismatch || !g_test.exePath[0])
        return 0;
    Am_PathJoin(probe, TEST_PATH, Am_ExeDir(), L"ctr_native.exe");
    if (!Am_FileExists(probe))
        return 0;
    Test_FullPath(probe, full, TEST_PATH);
    if (!full[0] || _wcsicmp(full, g_test.exePath) == 0)
        return 0;

    g_test.exeSwitchUsed = 1;
    Test_Copy(g_test.exeSwitchFrom, TEST_PATH, g_test.exePath);
    Test_Copy(g_test.exeSwitchFromVersion, 256, g_test.versionText);
    if (Am_Automating())
        Am_AutoLog(L"  game program: switching to %ls (next to this Alpha-Maker)", full);

    // Like Test_BrowseExe: set the text without EN_CHANGE debounce, then check.
    // Test_ExeApply writes test.exe (except in automation) and starts --version.
    g_test.quiet = 1;
    Am_SetText(g_test.exe, full);
    g_test.quiet = 0;
    KillTimer(page, TEST_TIMER_EXE);
    g_test.exePending = 0;
    if (Test_ExeApply(page))
        g_test.exeSwitchNote = 1;
    return 1;
}

static void Test_VersionDone(HWND page, DWORD code)
{
    // Room for the note after the switch: two version lines and a path.
    wchar_t text[TEST_PATH + 1024];
    int note = g_test.exeSwitchNote;

    KillTimer(page, TEST_TIMER_PROBE);
    g_test.versionJob = 0;
    g_test.exeOk = 0;
    g_test.exeMismatch = 0;
    g_test.exeSwitchNote = 0;
    if (g_test.versionText[0] && code == 0) {
        g_test.exeOk = Test_VersionMatches(g_test.versionText);
        g_test.exeMismatch = !g_test.exeOk;
    }
    if (g_test.exeOk && note) {
        swprintf(text, TEST_PATH + 1024,
                 L"Found: %ls. Switched from %ls (%ls) - that game was from another package.",
                 g_test.versionText, g_test.exeSwitchFrom, g_test.exeSwitchFromVersion);
        Test_SetLabel(g_test.version, text, AM_COL_OK);
    } else if (g_test.exeOk) {
        swprintf(text, TEST_PATH + 1024, L"Found: %ls", g_test.versionText);
        Test_SetLabel(g_test.version, text, AM_COL_OK);
    } else if (g_test.exeMismatch) {
        swprintf(text, TEST_PATH + 1024,
                 L"This game (%ls) is not from the same package as this Alpha-Maker (%ls (%ls)). "
                 L"Use ctr_native.exe from the same folder/package.",
                 g_test.versionText, AM_VERSION_W, AM_BUILD_ID_W);
        Test_SetLabel(g_test.version, text, AM_COL_ERROR);
    } else {
        Test_Copy(text, TEST_PATH + 1024, L"This program did not answer like CTR Reload.");
        Test_SetLabel(g_test.version, text, AM_COL_ERROR);
    }
    // The message about the foreign build (and the note after the switch)
    // wraps: lay out the card Game again.
    Test_Relayout(page);
    Test_UpdateReady();
    if (Am_Automating())
        Am_AutoLog(L"  game program: %ls", text);
    if (g_test.exeMismatch)
        Test_SwitchToNeighbour(page);
}

const wchar_t *Am_TestGameProblem(void)
{
    if (!g_test.exePath[0])
        return L"no game is chosen on the Test page.";
    if (!Am_FileExists(g_test.exePath))
        return L"the game program on the Test page was not found.";
    if (g_test.versionJob || g_test.exePending)
        return L"the game check on the Test page is not finished yet.";
    if (g_test.exeMismatch)
        return L"the game on the Test page is not from the same package as this Alpha-Maker.";
    if (!g_test.exeOk)
        return L"the game on the Test page did not answer like CTR Reload.";
    return NULL;
}

// For the page Track (preview): the entered game. In automation
// the Alpha-Maker does not write the ini (Am_ConfigSet), otherwise it would still
// contain the game of the last session.
const wchar_t *Am_TestGameExe(void)
{
    return g_test.exePath;
}

// For the page Track (preview): does the result from here apply to exePath?
// Only a finished, matching --version run for the same path counts.
int Am_TestGameVerified(const wchar_t *exePath)
{
    wchar_t full[TEST_PATH];

    if (!exePath || !exePath[0] || !g_test.exeOk || g_test.versionJob || g_test.exePending)
        return 0;
    Test_FullPath(exePath, full, TEST_PATH);
    return full[0] && _wcsicmp(full, g_test.exePath) == 0;
}

static void Test_BrowseExe(HWND page)
{
    wchar_t out[TEST_PATH];
    if (!Am_BrowseOpenFile(page, L"Choose the CTR Reload game program",
                           L"CTR Reload (ctr_native.exe)\0ctr_native.exe\0Programs (*.exe)\0*.exe\0\0",
                           g_test.exePath, out, TEST_PATH))
        return;
    g_test.quiet = 1;
    Am_SetText(g_test.exe, out);
    g_test.quiet = 0;
    KillTimer(page, TEST_TIMER_EXE);
    g_test.exePending = 0;
    Test_ExeApply(page);
}

static void Test_ChooseFile(HWND page)
{
    wchar_t out[TEST_PATH];
    const wchar_t *initial = g_test.container[0] ? g_test.container : g_test.tracksDir;
    if (Am_BrowseOpenFile(page, L"Choose a track container",
                          L"Track containers (*.rldtrack)\0*.rldtrack\0\0", initial, out, TEST_PATH))
        Test_SetContainer(page, out, 1, 1);
}

static void Test_SaveOptions(void)
{
    wchar_t v[16];
    wchar_t *args;

    Am_ConfigSet(L"test.windowed", Test_Checked(g_test.windowed) ? L"1" : L"0");
    swprintf(v, 16, L"%d", Test_Driver());
    Am_ConfigSet(L"test.driver", v);
    Am_ConfigSet(L"test.autopilot", Test_Checked(g_test.autopilot) ? L"1" : L"0");
    args = Am_GetText(g_test.args);
    Am_ConfigSet(L"test.args", args);
    Am_Free(args);
}

// ---------------------------------------------------------------------------
// View "game output"
// ---------------------------------------------------------------------------

static void Test_BuildOutput(void)
{
    struct TestBuf b;
    wchar_t note[160];
    wchar_t *cmdText;
    int i, used, shown = 0, hidden = 0, budget, logShow;

    if (!g_test.ran) {
        Am_SetText(g_test.output, L"No test yet. After Start game, the command line, the game's output "
                                  L"and its log lines show up here.");
        return;
    }
    memset(&b, 0, sizeof(b));
    cmdText = Test_CmdText();
    Test_BufAdd(&b, L"Command line:\r\n");
    Test_BufAdd(&b, cmdText);
    Test_BufAdd(&b, L"\r\n\r\nGame output:\r\n");
    Am_Free(cmdText);
    used = 4;

    // Keep room for up to 200 log lines; what the output does not
    // need goes to the log.
    logShow = g_test.log.count < TEST_VIEW_LINES / 2 ? g_test.log.count : TEST_VIEW_LINES / 2;
    budget = TEST_VIEW_LINES - used - logShow - 4;
    for (i = 0; i < g_test.out.count; i++) {
        // These lines at the bottom come from the log file.
        if (g_test.logFound && Test_IsLogLine(g_test.out.v[i]))
            continue;
        if (shown >= budget) {
            hidden++;
            continue;
        }
        Test_BufAdd(&b, g_test.out.v[i]);
        Test_BufAdd(&b, L"\r\n");
        shown++;
    }
    hidden += g_test.out.dropped;
    if (!shown && !hidden)
        Test_BufAdd(&b, g_test.gameJob ? L"(nothing yet)\r\n" : L"(nothing)\r\n");
    if (hidden) {
        swprintf(note, 160, L"... %d more line(s) not shown\r\n", hidden);
        Test_BufAdd(&b, note);
    }
    used += (shown ? shown : 1) + (hidden ? 1 : 0);
    Test_BufAdd(&b, L"\r\n");

    if (g_test.gameJob) {
        Test_BufAdd(&b, L"Game log: the game is still running.\r\n");
    } else if (g_test.startFailed) {
        Test_BufAdd(&b, L"Game log: the game did not start.\r\n");
    } else if (!g_test.logFound) {
        Test_BufAdd(&b, L"Game log: the game wrote none (");
        Test_BufAdd(&b, g_test.logPath);
        Test_BufAdd(&b, L").\r\n");
    } else {
        Test_BufAdd(&b, L"Game log lines (");
        Test_BufAdd(&b, g_test.logPath);
        Test_BufAdd(&b, L"):\r\n");
        budget = TEST_VIEW_LINES - used - 3;
        for (i = 0; i < g_test.log.count && i < budget; i++) {
            Test_BufAdd(&b, g_test.log.v[i]);
            Test_BufAdd(&b, L"\r\n");
        }
        if (g_test.log.count > i || g_test.log.dropped) {
            swprintf(note, 160, L"... %d more line(s) - see the game log\r\n",
                     g_test.log.count - i + g_test.log.dropped);
            Test_BufAdd(&b, note);
        }
        if (!g_test.log.count)
            Test_BufAdd(&b, L"(none)\r\n");
    }
    Am_SetText(g_test.output, b.p ? b.p : L"");
    Am_Free(b.p);
}

static void Test_ShowOutput(int on)
{
    g_test.showOutput = on;
    if (on)
        Test_BuildOutput();
    ShowWindow(g_test.output, on ? SW_SHOW : SW_HIDE);
    ShowWindow(g_test.list, on ? SW_HIDE : SW_SHOW);
    Am_SetText(g_test.toggle, on ? L"Show messages" : L"Show game output");
}

// ---------------------------------------------------------------------------
// Start, end, evaluation of the log
// ---------------------------------------------------------------------------

static void Test_ResetRun(void)
{
    Test_LinesFree(&g_test.out);
    Test_LinesFree(&g_test.log);
    g_test.startFailed = 0;
    g_test.haveExit = 0;
    g_test.exitCode = 0;
    g_test.raceStarted = 0;
    g_test.logFound = 0;
}

static void Test_AddMsg(int severity, const wchar_t *text, const wchar_t *detail)
{
    static const wchar_t *const names[AM_SEV_COUNT] = { L"ok", L"info", L"note", L"warning", L"error" };
    Am_MsgListAdd(g_test.list, severity, text, detail);
    if (Am_Automating())
        Am_AutoLog(L"  %ls: %ls", (severity >= 0 && severity < AM_SEV_COUNT) ? names[severity] : L"info", text);
}

// Builds the command line and starts the game. 1 = running.
static int Test_Start(HWND page)
{
    wchar_t dir[TEST_PATH];
    wchar_t name[TEST_PATH];
    wchar_t extra[TEST_PATH * 2];
    wchar_t num[16];
    wchar_t *text;
    wchar_t *cmdText;
    wchar_t *cmd = g_test.runCmd;
    int outside;

    if (Test_WhyNot())
        return 0;
    Test_MakeLogPath(g_test.logPath, TEST_PATH);
    Am_PathDir(dir, TEST_PATH, g_test.container);
    Test_DiskName(g_test.container, name, TEST_PATH);
    outside = !Test_SameDir(dir, g_test.tracksDir);

    cmd[0] = 0;
    Am_AppendArg(cmd, TEST_CMD_CAP, L"--dev");
    if (Test_Checked(g_test.windowed)) {
        Am_AppendArg(cmd, TEST_CMD_CAP, L"--windowed");
        Am_AppendArg(cmd, TEST_CMD_CAP, L"1280x720");
    }
    if (outside) {
        Am_AppendArg(cmd, TEST_CMD_CAP, L"--tracks-dir");
        Am_AppendArg(cmd, TEST_CMD_CAP, dir);
        Am_AppendArg(cmd, TEST_CMD_CAP, L"--settings-defaults");
    }
    Am_AppendArg(cmd, TEST_CMD_CAP, L"--autoload-track");
    Am_AppendArg(cmd, TEST_CMD_CAP, name);
    if (Test_Driver() > 0) {
        swprintf(num, 16, L"%d", Test_Driver());
        Am_AppendArg(cmd, TEST_CMD_CAP, L"--driver");
        Am_AppendArg(cmd, TEST_CMD_CAP, num);
    }
    if (Test_Checked(g_test.autopilot) && IsWindowEnabled(g_test.autopilot))
        Am_AppendArg(cmd, TEST_CMD_CAP, L"--autopilot");
    Am_AppendArg(cmd, TEST_CMD_CAP, L"--log");
    Am_AppendArg(cmd, TEST_CMD_CAP, g_test.logPath);
    // Extra arguments unchanged, with a space after them.
    text = Am_GetText(g_test.args);
    Test_Trim(text, extra, TEST_PATH * 2, 0);
    Am_Free(text);
    if (extra[0]) {
        size_t n = wcslen(cmd);
        if (n + 1 + wcslen(extra) < TEST_CMD_CAP) {
            cmd[n] = L' ';
            wcscpy(cmd + n + 1, extra);
        }
    }

    Test_Copy(g_test.runExe, TEST_PATH, g_test.exePath);
    Test_ResetRun();
    g_test.ran = 1;
    Test_SaveOptions();
    cmdText = Test_CmdText();
    if (Am_Automating())
        Am_AutoLog(L"  game command line: %ls", cmdText);
    Am_MsgListClear(g_test.list);
    g_test.gameJob = Am_RunProcess(page, g_test.exePath, cmd, g_test.baseDir, 1);
    if (!g_test.gameJob) {
        g_test.startFailed = 1;
        Test_SetLabel(g_test.headline, L"Test failed - see below", AM_COL_ERROR);
        Test_AddMsg(AM_SEV_ERROR, L"The game could not be started.", cmdText);
    } else {
        Test_SetLabel(g_test.headline, L"The game is running - close it to see the result here.", AM_COL_NOTE);
        Test_AddMsg(AM_SEV_INFO, L"The game was started with this command line:", cmdText);
        if (g_test.infoNav == 0)
            Test_AddMsg(AM_SEV_WARNING, L"The track has no nav paths, so there are no bots in this race - you drive alone.",
                        L"rldpack info: nav_paths 0");
    }
    Am_Free(cmdText);
    Test_UpdateButtons();
    Test_UpdateReady();
    if (g_test.showOutput)
        Test_BuildOutput();
    return g_test.gameJob != 0;
}

// Remembers the log lines with the known prefixes.
static void Test_ReadLog(void)
{
    wchar_t *text = Am_ReadTextFile(g_test.logPath);
    wchar_t *p, *line;
    size_t n;

    Test_LinesFree(&g_test.log);
    g_test.logFound = text != NULL;
    if (!text)
        return;
    p = text;
    while (*p) {
        line = p;
        while (*p && *p != L'\n')
            p++;
        if (*p)
            *p++ = 0;
        n = wcslen(line);
        if (n > 0 && line[n - 1] == L'\r')
            line[n - 1] = 0;
        if (Test_IsLogLine(line))
            Test_LinesPush(&g_test.log, Am_Dup(line), TEST_MAX_LOG);
    }
    Am_Free(text);
}

static void Test_Analyze(DWORD code)
{
    const wchar_t *race = NULL, *nitro = NULL, *notIn = NULL, *notOffered = NULL;
    const wchar_t *notLoaded = NULL, *didNot = NULL;
    wchar_t part[512];
    wchar_t text[768];
    wchar_t *cmdText;
    int i, warnings = 0, moreWarnings = 0, failed;

    for (i = 0; i < g_test.log.count; i++) {
        const wchar_t *l = g_test.log.v[i];
        if (wcsstr(l, L"[CTR Race] level ") && wcsstr(l, L"container")) {
            if (!race)
                race = l;
        } else if (wcsstr(l, L"[CTR Menu] NITRO-PIT: '")) {
            if (!nitro)
                nitro = l;
        } else if (wcsstr(l, L"--autoload-track: ")) {
            if (!notIn && wcsstr(l, L"is not in tracks/"))
                notIn = l;
            else if (!notOffered && wcsstr(l, L"is not offered - "))
                notOffered = l;
            else if (!didNot && wcsstr(l, L"did not load"))
                didNot = l;
        } else if (Test_StartsWith(l, L"[CTR Tracks]") && wcsstr(l, L": NOT LOADED - ")) {
            if (!notLoaded)
                notLoaded = l;
        }
    }

    Am_MsgListClear(g_test.list);
    g_test.raceStarted = race != NULL;
    if (race) {
        Test_Between(race, L" container '", L"', driver", part, 512);
        if (!part[0])
            Test_Between(race, L" container '", L"'", part, 512);
        swprintf(text, 768, L"The race started on '%ls'.", part);
        Test_AddMsg(AM_SEV_OK, text, race);
        // ", bots N": older games do not write it.
        {
            const wchar_t *b = wcsstr(race, L", bots ");
            if (b) {
                int bots = _wtoi(b + wcslen(L", bots "));
                if (bots > 0) {
                    swprintf(text, 768, L"%d bot(s) raced against you.", bots);
                    Test_AddMsg(AM_SEV_OK, text, race);
                } else {
                    Test_AddMsg(AM_SEV_WARNING, L"No bots were in the race - the track has no nav paths for them.", race);
                }
            }
        }
    }
    for (i = 0; i < g_test.log.count; i++) {
        const wchar_t *l = g_test.log.v[i];
        if (!wcsstr(l, L"[CTR Debug] --autopilot: level "))
            continue;
        if (wcsstr(l, L" - OFF"))
            Test_AddMsg(AM_SEV_WARNING, L"Auto drive could not take over: the track has no nav paths. You had the kart.", l);
        else if (wcsstr(l, L"drives as a bot"))
            Test_AddMsg(AM_SEV_OK, L"Auto drive took over the kart.", l);
        break;
    }
    if (nitro) {
        const wchar_t *seat = wcsstr(nitro, L"-> donor slot ");
        if (seat)
            swprintf(text, 768, L"The game loaded the container into level slot %d.",
                     _wtoi(seat + wcslen(L"-> donor slot ")));
        else
            Test_Copy(text, 768, L"The game loaded the container.");
        Test_AddMsg(AM_SEV_OK, text, nitro);
    }

    if (notIn)
        Test_AddMsg(AM_SEV_ERROR, L"The game did not find the container in the tracks folder it read.", notIn);
    if (notOffered) {
        Test_Between(notOffered, L"is not offered - ", L" - nothing loaded", part, 512);
        swprintf(text, 768, L"The game does not offer this container: %ls", part);
        Test_AddMsg(AM_SEV_ERROR, text, notOffered);
    }
    if (notLoaded) {
        Test_Between(notLoaded, L": NOT LOADED - ", NULL, part, 512);
        swprintf(text, 768, L"The game could not load the container: %ls", part);
        Test_AddMsg(AM_SEV_ERROR, text, notLoaded);
    } else if (didNot) {
        Test_AddMsg(AM_SEV_ERROR, L"The game could not load the container - see the game log.", didNot);
    }
    failed = notIn || notOffered || notLoaded || didNot;
    // At 64 the game rejected the command line; the line below says so.
    if (code != 64) {
        if (!g_test.logFound)
            Test_AddMsg(AM_SEV_ERROR, L"The game wrote no log. It may not have started - check the game program.",
                        g_test.logPath);
        else if (!race && !failed)
            Test_AddMsg(AM_SEV_ERROR, L"The race did not start before the game closed.",
                        L"The game log has no [CTR Race] line for a container.");
    }

    for (i = 0; i < g_test.log.count; i++) {
        const wchar_t *l = g_test.log.v[i];
        if (!Test_StartsWith(l, L"[CTR Tracks]") || (!wcsstr(l, L"REJECTED") && !wcsstr(l, L"WARNING")))
            continue;
        if (warnings >= 20) {
            moreWarnings++;
            continue;
        }
        warnings++;
        if (wcsstr(l, L"REJECTED")) {
            Test_Between(l, L"[CTR Tracks] ", L": REJECTED", part, 512);
            if (part[0])
                swprintf(text, 768, L"The game rejected the container '%ls'.", part);
            else
                Test_Copy(text, 768, L"The game rejected a track container.");
        } else {
            Test_Copy(text, 768, L"The game logged a warning about the tracks.");
        }
        Test_AddMsg(AM_SEV_WARNING, text, l);
    }
    if (moreWarnings) {
        swprintf(text, 768, L"%d more warning(s) - see the game log.", moreWarnings);
        Test_AddMsg(AM_SEV_WARNING, text, NULL);
    }

    cmdText = Test_CmdText();
    if (code == 64) {
        struct TestBuf b;
        memset(&b, 0, sizeof(b));
        for (i = 0; i < g_test.out.count && i < 30; i++) {
            if (i)
                Test_BufAdd(&b, L"\n");
            Test_BufAdd(&b, g_test.out.v[i]);
        }
        Test_AddMsg(AM_SEV_ERROR, L"The game did not accept its command line (exit code 64).",
                    (b.p && b.p[0]) ? b.p : L"The game printed nothing.");
        Am_Free(b.p);
    } else if (code >= 0xC0000000u) {
        swprintf(text, 768, L"The game crashed (exit code 0x%08lX).", code);
        Test_AddMsg(AM_SEV_ERROR, text, cmdText);
    } else {
        swprintf(text, 768, L"The game closed (exit code %lu).", code);
        Test_AddMsg(AM_SEV_INFO, text, cmdText);
    }
    Am_Free(cmdText);

    {
        const wchar_t *head = g_test.raceStarted ? L"Test finished - the race started" : L"Test failed - see below";
        Test_SetLabel(g_test.headline, head, g_test.raceStarted ? AM_COL_OK : AM_COL_ERROR);
        if (Am_Automating())
            Am_AutoLog(L"  result: %ls", head);
    }
}

static void Test_GameDone(DWORD code)
{
    g_test.gameJob = 0;
    g_test.haveExit = 1;
    g_test.exitCode = code;
    Test_ReadLog();
    Test_Analyze(code);
    Test_UpdateButtons();
    Test_UpdateReady();
    if (g_test.showOutput)
        Test_BuildOutput();
}

static void Test_OpenLog(HWND page)
{
    wchar_t text[TEST_PATH + 80];

    if (!g_test.logPath[0] || !Am_FileExists(g_test.logPath)) {
        Am_Tell(page, L"Game log", L"The game has not written a log for this test.");
        return;
    }
    if ((INT_PTR)ShellExecuteW(NULL, L"open", g_test.logPath, NULL, NULL, SW_SHOWNORMAL) <= 32) {
        swprintf(text, TEST_PATH + 80, L"Windows could not open the game log. It is here:\n%ls", g_test.logPath);
        Am_Tell(page, L"Game log", text);
    }
}

// ---------------------------------------------------------------------------
// Report (automation: report <file>)
// ---------------------------------------------------------------------------

static void Test_Put(FILE *f, const wchar_t *text)
{
    char *utf8 = Am_ToUtf8(text);
    fputs(utf8, f);
    Am_Free(utf8);
}

static void Test_PutKV(FILE *f, const wchar_t *key, const wchar_t *value)
{
    Test_Put(f, key);
    Test_Put(f, L": ");
    Test_Put(f, value);
    Test_Put(f, L"\n");
}

static void Test_PutCtl(FILE *f, const wchar_t *key, HWND control)
{
    wchar_t *text = Am_GetText(control);
    Test_PutKV(f, key, text);
    Am_Free(text);
}

static int Test_Report(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"wb");
    wchar_t v[128];
    wchar_t *text;
    const wchar_t *why;
    int i, d;

    if (!f)
        return 0;
    Test_Put(f, L"Alpha-Maker report - page \"Test in game\"\n\n");
    Test_PutKV(f, L"Game program", g_test.exePath[0] ? g_test.exePath : L"(none)");
    Test_PutCtl(f, L"Game program check", g_test.version);
    Test_PutKV(f, L"Game folder", g_test.baseDir[0] ? g_test.baseDir : L"(not found)");
    Test_PutKV(f, L"Tracks folder of the game", g_test.tracksDir[0] ? g_test.tracksDir : L"(none)");
    Test_PutCtl(f, L"Game folder label", g_test.base);
    Test_PutCtl(f, L"Tracks folder label", g_test.tracks);
    Test_PutKV(f, L"Container", g_test.container[0] ? g_test.container : L"(none)");
    Test_PutCtl(f, L"Container list entry", g_test.combo);
    swprintf(v, 128, L"%d", g_test.itemCount);
    Test_PutKV(f, L"Containers in the list", v);
    Test_PutCtl(f, L"Container check", g_test.info);
    Test_PutCtl(f, L"Nav paths", g_test.nav);
    Test_PutCtl(f, L"Loads from (line 1)", g_test.where1);
    Test_PutCtl(f, L"Loads from (line 2)", g_test.where2);
    Test_PutKV(f, L"Option play in a window (1280 x 720)", Test_Checked(g_test.windowed) ? L"on" : L"off");
    d = Test_Driver();
    swprintf(v, 128, L"%d - %ls", d, g_testDrivers[d]);
    Test_PutKV(f, L"Option driver", v);
    Test_PutKV(f, L"Option autopilot", !IsWindowEnabled(g_test.autopilot) ? L"locked (no nav paths)"
                                       : Test_Checked(g_test.autopilot) ? L"on" : L"off");
    Test_PutCtl(f, L"Option extra arguments", g_test.args);
    Test_PutKV(f, L"Start game button", IsWindowEnabled(g_test.start) ? L"enabled" : L"disabled");
    why = Test_WhyNot();
    Test_PutKV(f, L"Start game needs", why ? why : L"nothing - ready");

    if (g_test.ran) {
        text = Test_CmdText();
        Test_PutKV(f, L"Command line of the last start", text);
        Am_Free(text);
        if (g_test.gameJob)
            Test_PutKV(f, L"Last start", L"the game is still running");
        else if (g_test.startFailed)
            Test_PutKV(f, L"Last start", L"the game could not be started");
        else {
            swprintf(v, 128, L"the game closed with exit code %lu", g_test.exitCode);
            Test_PutKV(f, L"Last start", v);
        }
        Test_PutKV(f, L"Game log", g_test.logPath);
        Test_PutKV(f, L"Game log found", g_test.logFound ? L"yes" : L"no");
    } else {
        Test_PutKV(f, L"Command line of the last start", L"(not started)");
    }
    Test_PutCtl(f, L"Headline", g_test.headline);
    Test_PutKV(f, L"View", g_test.showOutput ? L"game output" : L"messages");

    Test_Put(f, L"\nMessages (severity, text, detail):\n");
    Am_MsgListWrite(g_test.list, f);

    Test_Put(f, L"\nGame log lines of the last run:\n");
    for (i = 0; i < g_test.log.count; i++) {
        Test_Put(f, g_test.log.v[i]);
        Test_Put(f, L"\n");
    }
    if (!g_test.log.count)
        Test_Put(f, L"(none)\n");
    if (g_test.log.dropped) {
        swprintf(v, 128, L"... %d more line(s)\n", g_test.log.dropped);
        Test_Put(f, v);
    }

    Test_Put(f, L"\nGame output of the last run:\n");
    for (i = 0; i < g_test.out.count && i < TEST_VIEW_LINES; i++) {
        Test_Put(f, g_test.out.v[i]);
        Test_Put(f, L"\n");
    }
    if (!g_test.out.count)
        Test_Put(f, L"(none)\n");
    if (g_test.out.count - i + g_test.out.dropped > 0) {
        swprintf(v, 128, L"... %d more line(s)\n", g_test.out.count - i + g_test.out.dropped);
        Test_Put(f, v);
    }
    fclose(f);
    return 1;
}

// ---------------------------------------------------------------------------
// Callbacks of the page
// ---------------------------------------------------------------------------

static void Test_Create(HWND page)
{
    wchar_t cfg[TEST_PATH];
    wchar_t found[TEST_PATH];
    int i;

    memset(&g_test, 0, sizeof(g_test));

    // Card "Game"
    g_test.exeLabel = Am_Label(page, TEST_ID_EXE_LABEL, L"Game program", AM_FONT_BOLD);
    g_test.exe = Am_Edit(page, TEST_ID_EXE, L"", 0);
    g_test.exeBrowse = Am_Button(page, TEST_ID_EXE_BROWSE, L"Browse...");
    g_test.version = Am_Label(page, TEST_ID_VERSION, L"", AM_FONT_SMALL);
    g_test.base = Am_Label(page, TEST_ID_BASE, L"", AM_FONT_SMALL);
    g_test.tracks = Am_Label(page, TEST_ID_TRACKS, L"", AM_FONT_SMALL);
    // version wraps (message about a foreign build), Test_LayoutGame measures.
    Test_SetEllipsis(g_test.base, SS_PATHELLIPSIS);
    Test_SetEllipsis(g_test.tracks, SS_PATHELLIPSIS);

    // Card "Track"
    g_test.combo = Am_Combo(page, TEST_ID_COMBO);
    g_test.choose = Am_Button(page, TEST_ID_CHOOSE, L"Choose file...");
    g_test.info = Am_Label(page, TEST_ID_INFO, L"", AM_FONT_SMALL);
    g_test.nav = Am_Label(page, TEST_ID_NAV, L"", AM_FONT_SMALL);
    g_test.where1 = Am_Label(page, TEST_ID_WHERE1, L"", AM_FONT_SMALL);
    g_test.where2 = Am_Label(page, TEST_ID_WHERE2, L"", AM_FONT_SMALL);
    Test_SetEllipsis(g_test.nav, SS_ENDELLIPSIS);
    Test_SetEllipsis(g_test.where1, SS_PATHELLIPSIS);
    Test_SetEllipsis(g_test.where2, SS_ENDELLIPSIS);

    // Card "Options"
    g_test.windowed = Am_Check(page, TEST_ID_WINDOWED, L"Play in a window (1280 x 720)");
    g_test.driverLabel = Am_Label(page, TEST_ID_DRIVER_LABEL, L"Driver", AM_FONT_BOLD);
    g_test.driver = Am_Combo(page, TEST_ID_DRIVER);
    for (i = 0; i < TEST_DRIVERS; i++)
        SendMessageW(g_test.driver, CB_ADDSTRING, 0, (LPARAM)g_testDrivers[i]);
    g_test.autopilot = Am_Check(page, TEST_ID_AUTOPILOT, L"Let the kart drive itself (autopilot)");
    g_test.argsLabel = Am_Label(page, TEST_ID_ARGS_LABEL, L"Extra arguments (for developers)", AM_FONT_BOLD);
    g_test.args = Am_Edit(page, TEST_ID_ARGS, L"", 0);

    // Card "Result"
    g_test.start = Am_PrimaryButton(page, TEST_ID_START, L"Start game");
    g_test.headline = Am_Label(page, TEST_ID_HEADLINE, L"", AM_FONT_BOLD);
    g_test.list = Am_MsgList(page, TEST_ID_LIST);
    Am_SetText(g_test.list, L"Press Start game. What the game did with the container shows up here.");
    g_test.output = Am_Edit(page, TEST_ID_OUTPUT, L"",
                            ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL);
    SendMessageW(g_test.output, WM_SETFONT, (WPARAM)Am_Font(AM_FONT_MONO), FALSE);
    ShowWindow(g_test.output, SW_HIDE);
    g_test.showLog = Am_Button(page, TEST_ID_SHOWLOG, L"Show game log");
    g_test.toggle = Am_Button(page, TEST_ID_TOGGLE, L"Show game output");
    Test_UpdateButtons();

    // Settings
    g_test.quiet = 1;
    Am_ConfigGet(L"test.windowed", cfg, TEST_PATH);
    SendMessageW(g_test.windowed, BM_SETCHECK, wcscmp(cfg, L"0") == 0 ? BST_UNCHECKED : BST_CHECKED, 0);
    Am_ConfigGet(L"test.driver", cfg, TEST_PATH);
    i = _wtoi(cfg);
    SendMessageW(g_test.driver, CB_SETCURSEL, (WPARAM)((i >= 0 && i < TEST_DRIVERS) ? i : 0), 0);
    Am_ConfigGet(L"test.autopilot", cfg, TEST_PATH);
    SendMessageW(g_test.autopilot, BM_SETCHECK, wcscmp(cfg, L"1") == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    Am_ConfigGet(L"test.args", cfg, TEST_PATH);
    Am_SetText(g_test.args, cfg);
    Am_ConfigGet(L"test.container", cfg, TEST_PATH);
    if (cfg[0] && Am_FileExists(cfg))
        Test_FullPath(cfg, g_test.container, TEST_PATH);
    Am_ConfigGet(L"test.exe", cfg, TEST_PATH);
    if ((!cfg[0] || !Am_FileExists(cfg)) && Am_FindGameExe(found, TEST_PATH))
        Test_Copy(cfg, TEST_PATH, found);
    Am_SetText(g_test.exe, cfg);
    g_test.quiet = 0;
    g_test.exeStartup = 1;
    Test_ExeApply(page);
}

static LRESULT Test_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);
    (void)lParam;

    switch (id) {
    case TEST_ID_EXE:
        if (code == EN_CHANGE && !g_test.quiet) {
            g_test.exePending = 1;
            SetTimer(page, TEST_TIMER_EXE, 400, NULL);
            Test_UpdateReady();
        }
        break;
    case TEST_ID_EXE_BROWSE:
        if (code == BN_CLICKED)
            Test_BrowseExe(page);
        break;
    case TEST_ID_COMBO:
        if (code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_test.combo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < g_test.itemCount)
                Test_SetContainer(page, g_test.items[sel], 0, 0);
        }
        break;
    case TEST_ID_CHOOSE:
        if (code == BN_CLICKED)
            Test_ChooseFile(page);
        break;
    case TEST_ID_WINDOWED:
    case TEST_ID_AUTOPILOT:
        if (code == BN_CLICKED)
            Test_SaveOptions();
        break;
    case TEST_ID_DRIVER:
        if (code == CBN_SELCHANGE)
            Test_SaveOptions();
        break;
    case TEST_ID_ARGS:
        if (code == EN_CHANGE && !g_test.quiet)
            Test_SaveOptions();
        break;
    case TEST_ID_START:
        if (code == BN_CLICKED)
            Test_Start(page);
        break;
    case TEST_ID_SHOWLOG:
        if (code == BN_CLICKED)
            Test_OpenLog(page);
        break;
    case TEST_ID_TOGGLE:
        if (code == BN_CLICKED)
            Test_ShowOutput(!g_test.showOutput);
        break;
    }
    return 0;
}

static LRESULT Test_Notify(HWND page, NMHDR *hdr)
{
    (void)page;
    (void)hdr;
    return 0;
}

static LRESULT Test_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    switch (msg) {
    case AM_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        int job = (int)wParam;
        *handled = 1;
        if (!line)
            return 0;
        if (job != 0 && job == g_test.gameJob) {
            Test_LinesPush(&g_test.out, line, TEST_MAX_OUT);   // takes the line
            return 0;
        }
        if (job != 0 && job == g_test.versionJob)
            Test_VersionLine(line);
        else if (job != 0 && job == g_test.infoJob)
            Test_InfoLine(line);
        Am_Free(line);
        return 0;
    }
    case AM_WM_JOB_DONE: {
        int job = (int)wParam;
        *handled = 1;
        if (job == 0)
            return 0;
        if (job == g_test.gameJob)
            Test_GameDone((DWORD)lParam);
        else if (job == g_test.versionJob)
            Test_VersionDone(page, (DWORD)lParam);
        else if (job == g_test.infoJob)
            Test_InfoDone((int)lParam);
        return 0;
    }
    case WM_TIMER:
        if (wParam == TEST_TIMER_EXE) {
            KillTimer(page, TEST_TIMER_EXE);
            g_test.exePending = 0;
            Test_ExeApply(page);
            *handled = 1;
        } else if (wParam == TEST_TIMER_PROBE) {
            // --version did not come back: stop waiting.
            KillTimer(page, TEST_TIMER_PROBE);
            if (g_test.versionJob) {
                g_test.versionJob = 0;
                g_test.exeOk = 0;
                Test_SetLabel(g_test.version, L"This program did not answer like CTR Reload.", AM_COL_ERROR);
                Test_UpdateReady();
                if (Am_Automating())
                    Am_AutoLog(L"  game program: no answer to --version within 15 seconds");
            }
            *handled = 1;
        }
        return 0;
    case AM_WM_PAGE_SHOWN:
        // The folder may have changed (rebuilt on the page Track).
        Test_ApplyBase(page);
        *handled = 1;
        return 0;
    case AM_WM_OPEN_TEST: {
        wchar_t *path = (wchar_t *)lParam;
        *handled = 1;
        if (path) {
            if (Am_FileExists(path))
                Test_SetContainer(page, path, 1, 1);
            Am_Free(path);
        }
        return 0;
    }
    case AM_WM_QUERY_CLOSE:
        if (g_test.gameJob) {
            *handled = 1;
            return Am_AskYesNo(page, L"Test in game",
                               L"The game is still running. Close the Alpha-Maker anyway? The game stays open.")
                       ? 1 : 0;
        }
        return 0;
    }
    return 0;
}

static int Test_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    wchar_t raw[TEST_PATH];
    wchar_t path[TEST_PATH];

    if (wcscmp(verb, L"exe") == 0) {
        Test_Trim(arg, raw, TEST_PATH, 1);
        g_test.quiet = 1;
        Am_SetText(g_test.exe, raw);
        g_test.quiet = 0;
        KillTimer(page, TEST_TIMER_EXE);
        g_test.exePending = 0;
        if (!Test_ExeApply(page)) {
            wchar_t *label = Am_GetText(g_test.version);
            Am_AutoLog(L"  the game program '%ls' cannot be checked: %ls", raw, label);
            Am_Free(label);
            return AM_AUTO_FAIL;
        }
        return AM_AUTO_WAIT;
    }
    if (wcscmp(verb, L"container") == 0) {
        Test_Trim(arg, raw, TEST_PATH, 1);
        if (!raw[0]) {
            Am_AutoLog(L"  container wants a path or a file name");
            return AM_AUTO_FAIL;
        }
        // Bare file name: from the game's tracks folder.
        if (!wcschr(raw, L'\\') && !wcschr(raw, L'/') && raw[1] != L':') {
            if (!g_test.tracksDir[0]) {
                Am_AutoLog(L"  there is no tracks folder of the game to find '%ls' in", raw);
                return AM_AUTO_FAIL;
            }
            Am_PathJoin(path, TEST_PATH, g_test.tracksDir, raw);
        } else {
            Test_Copy(path, TEST_PATH, raw);
        }
        if (!Am_FileExists(path)) {
            Am_AutoLog(L"  %ls does not exist", path);
            return AM_AUTO_FAIL;
        }
        if (!Test_SetContainer(page, path, 1, 1)) {
            Am_AutoLog(L"  the container check did not start");
            return AM_AUTO_FAIL;
        }
        return AM_AUTO_WAIT;
    }
    if (wcscmp(verb, L"windowed") == 0 || wcscmp(verb, L"autopilot") == 0) {
        HWND box = wcscmp(verb, L"windowed") == 0 ? g_test.windowed : g_test.autopilot;
        int on = Test_OnOff(arg);
        if (on < 0) {
            Am_AutoLog(L"  %ls wants on or off", verb);
            return AM_AUTO_FAIL;
        }
        SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        Test_SaveOptions();
        return AM_AUTO_DONE;
    }
    if (wcscmp(verb, L"driver") == 0) {
        wchar_t *end = NULL;
        long n = wcstol(arg, &end, 10);
        if (!arg[0] || (end && *end) || n < 0 || n >= TEST_DRIVERS) {
            Am_AutoLog(L"  driver wants a number from 0 to 15");
            return AM_AUTO_FAIL;
        }
        SendMessageW(g_test.driver, CB_SETCURSEL, (WPARAM)n, 0);
        Test_SaveOptions();
        Am_AutoLog(L"  driver %ld: %ls", n, g_testDrivers[n]);
        return AM_AUTO_DONE;
    }
    if (wcscmp(verb, L"args") == 0) {
        g_test.quiet = 1;
        Am_SetText(g_test.args, arg);
        g_test.quiet = 0;
        Test_SaveOptions();
        return AM_AUTO_DONE;
    }
    if (wcscmp(verb, L"start") == 0) {
        const wchar_t *why = Test_WhyNot();
        if (why) {
            Am_AutoLog(L"  Start game is not possible: %ls", why);
            return AM_AUTO_FAIL;
        }
        if (!Test_Start(page)) {
            Am_AutoLog(L"  the game could not be started");
            return AM_AUTO_FAIL;
        }
        return AM_AUTO_WAIT;
    }
    if (wcscmp(verb, L"report") == 0) {
        Test_Trim(arg, path, TEST_PATH, 1);
        if (!path[0] || !Test_Report(path)) {
            Am_AutoLog(L"  could not write the report '%ls'", path);
            return AM_AUTO_FAIL;
        }
        Am_AutoLog(L"  report written: %ls", path);
        return AM_AUTO_DONE;
    }
    return AM_AUTO_UNKNOWN;
}

static int Test_Busy(HWND page)
{
    (void)page;
    return (g_test.versionJob || g_test.infoJob || g_test.gameJob || g_test.exePending) ? 1 : 0;
}

const struct AmPageDef g_amTestPage = {
    L"Test in game",
    L"Test in game",
    L"Start CTR Reload with a container. The game jumps straight into a race on it.",
    Test_Create,
    Test_Layout,
    Test_Command,
    Test_Notify,
    Test_Message,
    Test_Automate,
    Test_Busy
};
