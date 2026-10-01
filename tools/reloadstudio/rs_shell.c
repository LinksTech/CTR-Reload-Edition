// rs_shell.c - window, sidebar and the shared helpers of Reload Studio
//
// See reloadstudio.h. Here is everything that no page needs on its own:
// the main window with the sidebar, the page windows with cards
// and colours, the message list, the child processes (rldpack and the game), the
// dialogs, the settings and the automation.
//
// TWO FACES OF ONE EXE
//
// If the exe is started with `--rldpack <arguments>`, it is rldpack: the same
// source file tools/rldpack.c, only with a renamed main (rs_rldpack.c). That way
// it stays ONE file for the author and ONE implementation of the checks.
// For that the front end starts itself as a child process and reads its
// output through a pipe - an error in rldpack does not take the window down with it.
//
// AUTOMATION
//
// `--do "<verb> <argument>"` (any number of times) plays back steps as if
// someone had clicked: the same callbacks as the buttons. Meant so that acceptance
// and repacking run reproducibly, without a second implementation. Shell
// verbs: page, shot, wait, size <w> <h>, theme, quit. Everything else goes to the
// current page (also "size <n>" with one number). `--log <file>` writes the
// automation log. In automation
// no dialog asks (answer always yes) and the settings are neither
// read nor written.
//
// COLOUR SCHEME
//
// Light or dark. At start: `--theme dark|light|system` before the
// setting `theme=` in the ini before Windows (AppsUseLightTheme). In
// automation without --theme it is light, so that screenshots do not depend
// on the machine. At run time the entry at the bottom of the sidebar toggles it
// (the choice goes into the ini) or the automation verb `theme dark|light|system`.

#define COBJMACROS
#include "reloadstudio.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <corecrt_startup.h>

// rs_rldpack.c: the main of tools/rldpack.c.
int Rldpack_Main(int argc, char *argv[]);

#define RS_MAX_CARDS 24
#define RS_MAX_AUTO 512
#define RS_SIDEBAR_W 216
#define RS_TIMER_AUTO 1
#define RS_CMD_CAP 32768

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct RsCard {
    RECT rc;
    wchar_t title[80];
    int hasTitle;
};

struct RsPageState {
    int id;
    const struct RsPageDef *def;
    int ready;
    struct RsCard cards[RS_MAX_CARDS];
    int cardCount;
};

static HINSTANCE g_inst;
static HWND g_main;
static HWND g_sidebar;
static HWND g_pages[RS_PAGE_COUNT];
static const struct RsPageDef *const g_defs[RS_PAGE_COUNT] = {
    &g_rsTrackPage, &g_rsCupsPage, &g_rsCharPage, &g_rsTestPage
};
static int g_current = -1;
static int g_dpi = 96;
static HFONT g_fonts[RS_FONT_COUNT];
static HFONT g_iconFont;
static HBRUSH g_brPage;
static HBRUSH g_brCard;
static HBRUSH g_brInput;
static wchar_t g_exePath[MAX_PATH];
static wchar_t g_exeDir[MAX_PATH];
static wchar_t g_iniPath[MAX_PATH];
static int g_hoverNav = -1;
static int g_trackingMouse;

// Colour scheme. g_themeMode is the choice (system/light/dark), g_dark the
// result that drawing currently follows.
enum RsTheme { RS_THEME_SYSTEM = 0, RS_THEME_LIGHT, RS_THEME_DARK };
static int g_themeMode = RS_THEME_SYSTEM;
static int g_dark;

// The palettes, in the order of enum RsPalSlot. Light: white cards on a light
// grey page. Dark: no pure black; the status colours lighter, so that
// they are readable on a dark background. Within a palette every value
// occurs only once, and no value of the one stands in the other in a
// different slot - Rs_SetTextColor finds the slot by the value.
static const struct RsPalette g_rsPalettes[2] = {
    { {
        RGB(243, 244, 247),   // RS_PAL_PAGE
        RGB(255, 255, 255),   // RS_PAL_CARD
        RGB(222, 225, 231),   // RS_PAL_BORDER
        RGB( 28,  31,  38),   // RS_PAL_TEXT
        RGB(110, 116, 128),   // RS_PAL_MUTED
        RGB(234,  88,  12),   // RS_PAL_ACCENT
        RGB(194,  65,   8),   // RS_PAL_ACCENT_DK
        RGB( 22, 140,  74),   // RS_PAL_OK
        RGB( 37,  99, 235),   // RS_PAL_NOTE
        RGB(202, 138,   4),   // RS_PAL_WARNING
        RGB(220,  38,  38),   // RS_PAL_ERROR
        RGB( 24,  26,  33),   // RS_PAL_SIDEBAR
    } },
    { {
        RGB( 24,  26,  32),   // RS_PAL_PAGE
        RGB( 34,  37,  45),   // RS_PAL_CARD
        RGB( 55,  60,  72),   // RS_PAL_BORDER
        RGB(230, 232, 237),   // RS_PAL_TEXT
        RGB(150, 156, 168),   // RS_PAL_MUTED
        RGB(234,  88,  12),   // RS_PAL_ACCENT
        RGB(194,  65,   8),   // RS_PAL_ACCENT_DK
        RGB( 74, 201, 128),   // RS_PAL_OK
        RGB( 96, 165, 250),   // RS_PAL_NOTE
        RGB(250, 190,  60),   // RS_PAL_WARNING
        RGB(248, 113, 113),   // RS_PAL_ERROR
        RGB( 17,  19,  24),   // RS_PAL_SIDEBAR - darker than the page, otherwise the edge blurs
    } },
};
const struct RsPalette *g_rsPal = &g_rsPalettes[0];

// Colours only the shell needs, per scheme.
struct RsShellColors {
    COLORREF input;           // input fields and lists (only set in the dark scheme)
    COLORREF divider;         // divider line in the message list
    COLORREF primaryOff;      // main button disabled
    COLORREF primaryOffText;
};
static const struct RsShellColors g_rsShellColors[2] = {
    { RGB(255, 255, 255), RGB(236, 238, 242), RGB(206, 210, 218), RGB(255, 255, 255) },
    { RGB( 27,  29,  36), RGB( 48,  52,  63), RGB( 58,  62,  74), RGB(128, 134, 148) },
};

// Automation.
struct RsAutoStep {
    wchar_t *verb;
    wchar_t *arg;
};
static struct RsAutoStep g_auto[RS_MAX_AUTO];
static int g_autoCount;
static int g_autoNext;
static int g_autoWaiting;
static DWORD g_autoSettleUntil;
static int g_autoFailed;
static int g_automating;
static wchar_t g_autoLogPath[MAX_PATH];

// Child processes.
static CRITICAL_SECTION g_jobLock;
static LONG g_nextJob;
// Running processes per job ID, for Rs_KillJob. Entry and handle
// belong together under g_jobLock: the job thread removes the entry
// before it closes the handle.
#define RS_JOB_SLOTS 16
static struct { int id; HANDLE process; } g_jobSlots[RS_JOB_SLOTS];

// ---------------------------------------------------------------------------
// Memory, text, files
// ---------------------------------------------------------------------------

void *Rs_Alloc(size_t bytes)
{
    void *p = calloc(1, bytes ? bytes : 1);
    if (!p) {
        MessageBoxW(NULL, L"Reload Studio ran out of memory and has to close.",
                    L"Reload Studio", MB_ICONERROR);
        ExitProcess(3);
    }
    return p;
}

void Rs_Free(void *p)
{
    free(p);
}

wchar_t *Rs_Dup(const wchar_t *s)
{
    size_t n;
    wchar_t *d;
    if (!s)
        s = L"";
    n = wcslen(s);
    d = Rs_Alloc((n + 1) * sizeof(wchar_t));
    memcpy(d, s, n * sizeof(wchar_t));
    return d;
}

wchar_t *Rs_FromUtf8(const char *s, int bytes)
{
    int n;
    wchar_t *d;
    if (!s)
        return Rs_Dup(L"");
    if (bytes < 0)
        bytes = (int)strlen(s);
    n = MultiByteToWideChar(CP_UTF8, 0, s, bytes, NULL, 0);
    d = Rs_Alloc(((size_t)n + 1) * sizeof(wchar_t));
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s, bytes, d, n);
    return d;
}

char *Rs_ToUtf8(const wchar_t *s)
{
    int n;
    char *d;
    if (!s)
        s = L"";
    n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    d = Rs_Alloc((size_t)(n > 0 ? n : 1));
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, s, -1, d, n, NULL, NULL);
    return d;
}

#define RS_TEXT_FILE_MAX (16 * 1024 * 1024)

static unsigned long long Rs_HashBytes(const unsigned char *p, size_t n)
{
    unsigned long long h = 14695981039346656037ull;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// Reads the whole file. 1 = read (*buf with a NUL after *got bytes), 0 = not
// there, -1 = there but not read completely (*error says why).
static int Rs_ReadWholeFile(const wchar_t *path, char **buf, DWORD *got,
                            struct RsFileStamp *stamp, DWORD *error)
{
    HANDLE f;
    LARGE_INTEGER size;
    FILETIME wt;
    DWORD want, done = 0;

    *buf = NULL;
    *got = 0;
    *error = 0;
    memset(stamp, 0, sizeof(*stamp));
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return 0;
        *error = e ? e : ERROR_OPEN_FAILED;
        stamp->exists = 1;
        return -1;
    }
    stamp->exists = 1;
    if (!GetFileSizeEx(f, &size) || !GetFileTime(f, NULL, NULL, &wt)) {
        *error = GetLastError();
        if (!*error)
            *error = ERROR_READ_FAULT;
        CloseHandle(f);
        return -1;
    }
    if (size.QuadPart < 0 || size.QuadPart > RS_TEXT_FILE_MAX) {
        *error = ERROR_FILE_TOO_LARGE;
        CloseHandle(f);
        return -1;
    }
    want = (DWORD)size.QuadPart;
    *buf = Rs_Alloc((size_t)want + 1);
    while (done < want) {
        DWORD part = 0;
        if (!ReadFile(f, *buf + done, want - done, &part, NULL)) {
            *error = GetLastError();
            break;
        }
        if (part == 0) {
            *error = ERROR_HANDLE_EOF;   // shorter than its size said
            break;
        }
        done += part;
    }
    CloseHandle(f);
    if (done < want) {
        if (!*error)
            *error = ERROR_READ_FAULT;
        Rs_Free(*buf);
        *buf = NULL;
        return -1;
    }
    *got = done;
    stamp->size = (unsigned long long)size.QuadPart;
    stamp->writeTime = wt;
    stamp->hash = Rs_HashBytes((const unsigned char *)*buf, done);
    return 1;
}

wchar_t *Rs_ReadTextFileEx(const wchar_t *path, struct RsTextRead *info)
{
    struct RsTextRead dummy;
    char *buf;
    DWORD got;
    wchar_t *text;
    int skip = 0, r;

    if (!info)
        info = &dummy;
    memset(info, 0, sizeof(*info));
    r = Rs_ReadWholeFile(path, &buf, &got, &info->stamp, &info->error);
    if (r <= 0)
        return NULL;
    if (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF)
        skip = 3;
    if (got > (DWORD)skip &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf + skip, (int)got - skip, NULL, 0) <= 0)
        info->badUtf8 = 1;
    text = Rs_FromUtf8(buf + skip, (int)got - skip);
    Rs_Free(buf);
    return text;
}

wchar_t *Rs_ReadTextFile(const wchar_t *path)
{
    return Rs_ReadTextFileEx(path, NULL);
}

int Rs_FileStampNow(const wchar_t *path, struct RsFileStamp *out)
{
    char *buf;
    DWORD got, error;
    int r = Rs_ReadWholeFile(path, &buf, &got, out, &error);
    if (buf)
        Rs_Free(buf);
    return r >= 0;
}

int Rs_FileStampSame(const struct RsFileStamp *a, const struct RsFileStamp *b)
{
    if (!a->exists || !b->exists)
        return a->exists == b->exists;
    return a->size == b->size && a->hash == b->hash &&
           CompareFileTime(&a->writeTime, &b->writeTime) == 0;
}

int Rs_WriteTextFile(const wchar_t *path, const wchar_t *text)
{
    char *utf8 = Rs_ToUtf8(text);
    size_t len = strlen(utf8), done = 0, n = wcslen(path);
    wchar_t *tmp = Rs_Alloc((n + 16) * sizeof(wchar_t));
    wchar_t *old = Rs_Alloc((n + 16) * sizeof(wchar_t));
    DWORD error = 0;
    HANDLE f;

    memcpy(tmp, path, n * sizeof(wchar_t));
    wcscpy(tmp + n, L".tmp");
    memcpy(old, path, n * sizeof(wchar_t));
    wcscpy(old + n, L".old.tmp");

    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto out;
    }
    while (done < len && !error) {
        DWORD put = 0, part = (DWORD)(len - done > 0x40000000u ? 0x40000000u : len - done);
        if (!WriteFile(f, utf8 + done, part, &put, NULL) || put == 0)
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        done += put;
    }
    if (!error && !FlushFileBuffers(f))
        error = GetLastError();
    if (!CloseHandle(f) && !error)
        error = GetLastError();
    if (error)
        goto out;

    if (Rs_FileExists(path)) {
        // Keeps attributes and permissions of the old file. Its old content goes
        // to "<path>.old.tmp" for the moment of the swap and is then removed.
        DeleteFileW(old);
        if (ReplaceFileW(path, tmp, old, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
            DeleteFileW(old);
            goto out;
        }
        error = GetLastError();
        if (error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            // The old file was already moved to its backup name: put it back.
            MoveFileExW(old, path, MOVEFILE_WRITE_THROUGH);
            goto out;
        }
        // ReplaceFileW left both files as they were; try the plain rename
        // (e.g. on drives without ReplaceFileW support).
        error = 0;
    }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error = GetLastError();

out:
    if (error)
        DeleteFileW(tmp);
    Rs_Free(tmp);
    Rs_Free(old);
    Rs_Free(utf8);
    if (error) {
        SetLastError(error);
        return 0;
    }
    return 1;
}

int Rs_FileExists(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int Rs_DirExists(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void Rs_PathJoin(wchar_t *out, int outCap, const wchar_t *dir, const wchar_t *name)
{
    size_t n;
    if (outCap <= 0)
        return;
    out[0] = 0;
    if (dir && *dir) {
        wcsncpy(out, dir, (size_t)outCap - 1);
        out[outCap - 1] = 0;
        n = wcslen(out);
        if (n > 0 && out[n - 1] != L'\\' && out[n - 1] != L'/' && (int)n + 1 < outCap) {
            out[n] = L'\\';
            out[n + 1] = 0;
        }
    }
    n = wcslen(out);
    if (name && (int)n < outCap - 1) {
        wcsncpy(out + n, name, (size_t)outCap - 1 - n);
        out[outCap - 1] = 0;
    }
}

const wchar_t *Rs_PathName(const wchar_t *path)
{
    const wchar_t *p = path, *name = path;
    if (!path)
        return L"";
    for (; *p; p++)
        if (*p == L'\\' || *p == L'/')
            name = p + 1;
    return name;
}

void Rs_PathDir(wchar_t *out, int outCap, const wchar_t *path)
{
    const wchar_t *name = Rs_PathName(path);
    size_t n = (size_t)(name - path);
    if (outCap <= 0)
        return;
    if (n > 0 && (path[n - 1] == L'\\' || path[n - 1] == L'/'))
        n--;
    if ((int)n >= outCap)
        n = (size_t)outCap - 1;
    memcpy(out, path, n * sizeof(wchar_t));
    out[n] = 0;
}

const wchar_t *Rs_ExeDir(void)
{
    return g_exeDir;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Rs_ConfigGet(const wchar_t *key, wchar_t *out, int outCap)
{
    if (outCap <= 0)
        return;
    out[0] = 0;
    if (g_automating || !g_iniPath[0])
        return;
    GetPrivateProfileStringW(L"reloadstudio", key, L"", out, (DWORD)outCap, g_iniPath);
}

void Rs_ConfigSet(const wchar_t *key, const wchar_t *value)
{
    if (g_automating || !g_iniPath[0])
        return;
    WritePrivateProfileStringW(L"reloadstudio", key, value ? value : L"", g_iniPath);
}

// Once: the tool was called Alpha-Maker before and kept its settings in
// alphamaker.ini, section [alphamaker]. If reloadstudio.ini does not exist
// yet, copy all keys of that section over. The old file is only read.
// Not in automation: there the ini is neither read nor written.
static void Rs_ConfigMigrate(const wchar_t *dir)
{
    enum { SECTION_CAP = 32767 };   // the limit of GetPrivateProfileSectionW
    wchar_t oldPath[MAX_PATH];
    wchar_t *section;
    DWORD n;

    if (g_automating || Rs_FileExists(g_iniPath))
        return;
    Rs_PathJoin(oldPath, MAX_PATH, dir, L"alphamaker.ini");
    if (!Rs_FileExists(oldPath))
        return;
    section = Rs_Alloc(SECTION_CAP * sizeof(wchar_t));
    n = GetPrivateProfileSectionW(L"alphamaker", section, SECTION_CAP, oldPath);
    if (n > 0 && WritePrivateProfileSectionW(L"reloadstudio", section, g_iniPath))
        Rs_AutoLog(L"settings: copied section [alphamaker] of %ls to %ls", oldPath, g_iniPath);
    Rs_Free(section);
}

static void Rs_ConfigInit(void)
{
    wchar_t dir[MAX_PATH];
    PWSTR appData = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appData)))
        return;
    Rs_PathJoin(dir, MAX_PATH, appData, L"CTR Reload");
    CoTaskMemFree(appData);
    CreateDirectoryW(dir, NULL);
    Rs_PathJoin(g_iniPath, MAX_PATH, dir, L"reloadstudio.ini");
    Rs_ConfigMigrate(dir);
}

int Rs_FindGameExe(wchar_t *out, int outCap)
{
    wchar_t dir[MAX_PATH];
    wchar_t probe[MAX_PATH];
    wchar_t sub[MAX_PATH];
    int up;

    wcsncpy(dir, g_exeDir, MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    for (up = 0; up <= 3; up++) {
        Rs_PathJoin(probe, MAX_PATH, dir, L"ctr_native.exe");
        if (Rs_FileExists(probe)) {
            wcsncpy(out, probe, (size_t)outCap - 1);
            out[outCap - 1] = 0;
            return 1;
        }
        Rs_PathJoin(sub, MAX_PATH, dir, L"build-msvc-x86\\Release");
        Rs_PathJoin(probe, MAX_PATH, sub, L"ctr_native.exe");
        if (Rs_FileExists(probe)) {
            wcsncpy(out, probe, (size_t)outCap - 1);
            out[outCap - 1] = 0;
            return 1;
        }
        Rs_PathDir(sub, MAX_PATH, dir);
        if (!sub[0] || wcscmp(sub, dir) == 0)
            break;
        wcscpy(dir, sub);
    }
    if (outCap > 0)
        out[0] = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// Game logs with timestamps
// ---------------------------------------------------------------------------

#define RS_LOG_DIR  (MAX_PATH * 2)
#define RS_LOG_FULL (MAX_PATH * 3)
#define RS_LOG_KEY  32

typedef struct {
    wchar_t key[RS_LOG_KEY];     // timestamp + number, sortable
    wchar_t name[MAX_PATH];
} RsLogFile;

// Does name match "<kind> YYYY-MM-DD HH-MM-SS.log" or "... (N).log"? 1 = yes,
// key gets timestamp and number (without suffix 1) so that the order
// of the keys is the chronological one - by the bare name " (2)" would come before ".log".
// The cleanup does not touch other files with the same beginning.
static int Rs_LogKey(const wchar_t *name, const wchar_t *kind, wchar_t *key, int keyCap)
{
    static const wchar_t form[] = L"0000-00-00 00-00-00";
    size_t k = wcslen(kind);
    const wchar_t *p;
    int i, nr = 1;

    if (_wcsnicmp(name, kind, k) != 0 || name[k] != L' ')
        return 0;
    p = name + k + 1;
    for (i = 0; form[i]; i++) {
        if (form[i] == L'0' ? (p[i] < L'0' || p[i] > L'9') : p[i] != form[i])
            return 0;
    }
    p += i;
    if (p[0] == L' ' && p[1] == L'(') {
        nr = 0;
        for (p += 2; *p >= L'0' && *p <= L'9' && nr < 100000; p++)
            nr = nr * 10 + (*p - L'0');
        if (*p != L')' || nr < 2 || nr >= 100000)
            return 0;
        p++;
    }
    if (_wcsicmp(p, L".log") != 0)
        return 0;
    swprintf(key, keyCap, L"%.19ls %06d", name + k + 1, nr);
    return 1;
}

static int Rs_LogCmp(const void *a, const void *b)
{
    return wcscmp(((const RsLogFile *)a)->key, ((const RsLogFile *)b)->key);
}

void Rs_RotatedLogPath(wchar_t *out, int cap, const wchar_t *kind, int keep)
{
    wchar_t tmp[MAX_PATH + 1];
    wchar_t dir[RS_LOG_DIR];
    wchar_t name[MAX_PATH];
    wchar_t full[RS_LOG_FULL];
    wchar_t key[RS_LOG_KEY];
    RsLogFile *list = NULL, *grown;
    int count = 0, room = 0, nr, i;
    SYSTEMTIME st;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    DWORD n;

    if (cap <= 0)
        return;
    out[0] = 0;
    if (keep < 1)
        keep = 1;
    n = GetTempPathW(MAX_PATH + 1, tmp);
    if (n == 0 || n > MAX_PATH) {
        wcsncpy(tmp, g_exeDir, MAX_PATH);
        tmp[MAX_PATH] = 0;
    }
    Rs_PathJoin(dir, RS_LOG_DIR, tmp, L"Reload Studio");
    CreateDirectoryW(dir, NULL);

    // Once: the file with the fixed name from earlier. Errors do not count.
    swprintf(name, MAX_PATH, L"%ls.log", kind);
    Rs_PathJoin(full, RS_LOG_FULL, dir, name);
    DeleteFileW(full);

    // New name with local time; if it exists already, " (2)", " (3)" ...
    GetLocalTime(&st);
    for (nr = 1; nr < 1000; nr++) {
        if (nr == 1)
            swprintf(name, MAX_PATH, L"%ls %04u-%02u-%02u %02u-%02u-%02u.log", kind,
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        else
            swprintf(name, MAX_PATH, L"%ls %04u-%02u-%02u %02u-%02u-%02u (%d).log", kind,
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, nr);
        Rs_PathJoin(out, cap, dir, name);
        if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES)
            break;
    }

    // Cleanup: of the existing files of this kind keep - 1 remain, the
    // new one (not yet written) makes keep full. A file that is still
    // open simply stays.
    swprintf(name, MAX_PATH, L"%ls *.log", kind);
    Rs_PathJoin(full, RS_LOG_FULL, dir, name);
    h = FindFirstFileW(full, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (!Rs_LogKey(fd.cFileName, kind, key, RS_LOG_KEY))
            continue;
        if (_wcsicmp(fd.cFileName, Rs_PathName(out)) == 0)
            continue;
        if (count == room) {
            room = room ? room * 2 : 16;
            grown = Rs_Alloc((size_t)room * sizeof(RsLogFile));
            if (count)
                memcpy(grown, list, (size_t)count * sizeof(RsLogFile));
            Rs_Free(list);
            list = grown;
        }
        wcscpy(list[count].key, key);
        wcsncpy(list[count].name, fd.cFileName, MAX_PATH - 1);
        list[count].name[MAX_PATH - 1] = 0;
        count++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (count > keep - 1) {
        qsort(list, (size_t)count, sizeof(RsLogFile), Rs_LogCmp);
        for (i = 0; i < count - (keep - 1); i++) {
            Rs_PathJoin(full, RS_LOG_FULL, dir, list[i].name);
            DeleteFileW(full);
        }
    }
    Rs_Free(list);
}

// ---------------------------------------------------------------------------
// Automation log and dialogs
// ---------------------------------------------------------------------------

int Rs_Automating(void)
{
    return g_automating;
}

void Rs_AutoLog(const wchar_t *fmt, ...)
{
    wchar_t line[2048];
    va_list ap;
    FILE *f;
    char *utf8;

    va_start(ap, fmt);
    vswprintf(line, 2047, fmt, ap);
    va_end(ap);
    line[2047] = 0;
    OutputDebugStringW(line);
    OutputDebugStringW(L"\n");
    if (!g_autoLogPath[0])
        return;
    f = _wfopen(g_autoLogPath, L"ab");
    if (!f)
        return;
    utf8 = Rs_ToUtf8(line);
    fputs(utf8, f);
    fputs("\n", f);
    fclose(f);
    Rs_Free(utf8);
}

int Rs_AskYesNo(HWND owner, const wchar_t *title, const wchar_t *text)
{
    if (g_automating) {
        Rs_AutoLog(L"  ask: %ls - answered Yes", text);
        return 1;
    }
    return MessageBoxW(owner ? owner : g_main, text, title, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

int Rs_AskChoice(HWND owner, const wchar_t *title, const wchar_t *text,
                 const wchar_t *const *buttons, int count, int autoAnswer)
{
    TASKDIALOG_BUTTON b[4];
    TASKDIALOGCONFIG cfg;
    int pressed = 0, i;

    if (count < 2)
        count = 2;
    if (count > 4)
        count = 4;
    if (autoAnswer < 0 || autoAnswer >= count)
        autoAnswer = count - 1;
    if (g_automating) {
        Rs_AutoLog(L"  ask: %ls - answered %ls", text, buttons[autoAnswer]);
        return autoAnswer;
    }
    memset(&cfg, 0, sizeof(cfg));
    for (i = 0; i < count; i++) {
        b[i].nButtonID = 100 + i;
        b[i].pszButtonText = buttons[i];
    }
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = owner ? owner : g_main;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.pszWindowTitle = title;
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszContent = text;
    cfg.cButtons = (UINT)count;
    cfg.pButtons = b;
    cfg.nDefaultButton = 100 + count - 1;
    if (SUCCEEDED(TaskDialogIndirect(&cfg, &pressed, NULL, NULL))) {
        if (pressed >= 100 && pressed < 100 + count)
            return pressed - 100;
        return count - 1;
    }
    // Without the task dialog: Yes / No (/ Cancel) with the meanings in the text.
    {
        size_t cap = wcslen(text) + 400;
        wchar_t *t = Rs_Alloc(cap * sizeof(wchar_t));
        static const wchar_t *const names[3] = { L"Yes", L"No", L"Cancel" };
        int r;
        wcscpy(t, text);
        wcscat(t, L"\n");
        for (i = 0; i < count && i < 3; i++) {
            wcscat(t, L"\n");
            wcscat(t, names[i]);
            wcscat(t, L" = ");
            wcsncat(t, buttons[i], 100);
        }
        r = MessageBoxW(owner ? owner : g_main, t, title,
                        (count >= 3 ? MB_YESNOCANCEL | MB_DEFBUTTON3 : MB_YESNO | MB_DEFBUTTON2) |
                            MB_ICONWARNING);
        Rs_Free(t);
        if (r == IDYES)
            return 0;
        if (r == IDNO && count >= 3)
            return 1;
        return count - 1;
    }
}

void Rs_Tell(HWND owner, const wchar_t *title, const wchar_t *text)
{
    if (g_automating) {
        Rs_AutoLog(L"  tell: %ls - %ls", title, text);
        return;
    }
    MessageBoxW(owner ? owner : g_main, text, title, MB_OK | MB_ICONINFORMATION);
}

int Rs_BrowseFolder(HWND owner, const wchar_t *title, const wchar_t *initial,
                    wchar_t *out, int outCap)
{
    IFileOpenDialog *dlg = NULL;
    IShellItem *item = NULL;
    DWORD opts = 0;
    int ok = 0;

    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dlg)))
        return 0;
    IFileOpenDialog_GetOptions(dlg, &opts);
    IFileOpenDialog_SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (title)
        IFileOpenDialog_SetTitle(dlg, title);
    if (initial && *initial && Rs_DirExists(initial) &&
        SUCCEEDED(SHCreateItemFromParsingName(initial, NULL, &IID_IShellItem, (void **)&item))) {
        IFileOpenDialog_SetFolder(dlg, item);
        IShellItem_Release(item);
        item = NULL;
    }
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, owner ? owner : g_main)) &&
        SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
        PWSTR path = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
            wcsncpy(out, path, (size_t)outCap - 1);
            out[outCap - 1] = 0;
            CoTaskMemFree(path);
            ok = 1;
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(dlg);
    return ok;
}

static int Rs_BrowseFile(HWND owner, const wchar_t *title, const wchar_t *filter,
                         const wchar_t *defExt, const wchar_t *initial,
                         wchar_t *out, int outCap, int save)
{
    OPENFILENAMEW ofn;
    wchar_t file[MAX_PATH * 2];
    wchar_t dir[MAX_PATH * 2];

    file[0] = 0;
    dir[0] = 0;
    if (initial && *initial) {
        if (Rs_DirExists(initial)) {
            wcsncpy(dir, initial, MAX_PATH * 2 - 1);
            dir[MAX_PATH * 2 - 1] = 0;
        } else {
            wcsncpy(file, initial, MAX_PATH * 2 - 1);
            file[MAX_PATH * 2 - 1] = 0;
            Rs_PathDir(dir, MAX_PATH * 2, initial);
            wcsncpy(file, Rs_PathName(initial), MAX_PATH * 2 - 1);
        }
    }
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner ? owner : g_main;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (save) {
        ofn.Flags |= OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn))
            return 0;
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn))
            return 0;
    }
    wcsncpy(out, file, (size_t)outCap - 1);
    out[outCap - 1] = 0;
    return 1;
}

int Rs_BrowseOpenFile(HWND owner, const wchar_t *title, const wchar_t *filter,
                      const wchar_t *initial, wchar_t *out, int outCap)
{
    return Rs_BrowseFile(owner, title, filter, NULL, initial, out, outCap, 0);
}

int Rs_BrowseSaveFile(HWND owner, const wchar_t *title, const wchar_t *filter,
                      const wchar_t *defExt, const wchar_t *initial,
                      wchar_t *out, int outCap)
{
    return Rs_BrowseFile(owner, title, filter, defExt, initial, out, outCap, 1);
}

// ---------------------------------------------------------------------------
// Sizes, fonts, colours
// ---------------------------------------------------------------------------

int Rs_Px(int px96)
{
    return MulDiv(px96, g_dpi, 96);
}

HFONT Rs_Font(int font)
{
    if (font < 0 || font >= RS_FONT_COUNT)
        font = RS_FONT_BODY;
    return g_fonts[font];
}

static HFONT Rs_MakeFont(const wchar_t *face, int points, int weight)
{
    return CreateFontW(-MulDiv(points, g_dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static void Rs_MakeFonts(void)
{
    int i;
    for (i = 0; i < RS_FONT_COUNT; i++)
        if (g_fonts[i])
            DeleteObject(g_fonts[i]);
    if (g_iconFont)
        DeleteObject(g_iconFont);
    g_fonts[RS_FONT_BODY] = Rs_MakeFont(L"Segoe UI", 10, FW_NORMAL);
    g_fonts[RS_FONT_BOLD] = Rs_MakeFont(L"Segoe UI", 10, FW_SEMIBOLD);
    g_fonts[RS_FONT_SMALL] = Rs_MakeFont(L"Segoe UI", 9, FW_NORMAL);
    g_fonts[RS_FONT_SECTION] = Rs_MakeFont(L"Segoe UI", 12, FW_SEMIBOLD);
    g_fonts[RS_FONT_TITLE] = Rs_MakeFont(L"Segoe UI", 20, FW_SEMIBOLD);
    g_fonts[RS_FONT_MONO] = Rs_MakeFont(L"Consolas", 9, FW_NORMAL);
    g_iconFont = Rs_MakeFont(L"Segoe MDL2 Assets", 12, FW_NORMAL);
}

COLORREF Rs_SeverityColor(int severity)
{
    switch (severity) {
    case RS_SEV_OK: return RS_COL_OK;
    case RS_SEV_NOTE: return RS_COL_NOTE;
    case RS_SEV_WARNING: return RS_COL_WARNING;
    case RS_SEV_ERROR: return RS_COL_ERROR;
    default: return RS_COL_MUTED;
    }
}

int Rs_SeverityFromText(const wchar_t *text)
{
    if (!text)
        return RS_SEV_INFO;
    if (wcscmp(text, L"error") == 0)
        return RS_SEV_ERROR;
    if (wcscmp(text, L"warning") == 0)
        return RS_SEV_WARNING;
    if (wcscmp(text, L"note") == 0)
        return RS_SEV_NOTE;
    if (wcscmp(text, L"ok") == 0)
        return RS_SEV_OK;
    return RS_SEV_INFO;
}

static void Rs_FillRound(HDC dc, const RECT *rc, int radius, COLORREF fill, COLORREF border)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

// ---------------------------------------------------------------------------
// Colour scheme
//
// In the dark scheme the standard controls get the dark themes of
// Windows 10/11 (DarkMode_Explorer, DarkMode_CFD for input fields and
// combo boxes, DarkMode_ItemsView for the header of a list) and their
// colours via WM_CTLCOLOR*. In the light scheme they keep Windows'
// defaults: no own theme, the list "Explorer". At start in the light
// scheme the shell calls nothing here.
// ---------------------------------------------------------------------------

// How a control keeps its text colour (property "RsColor"):
// palette colours as a slot, everything else as a value. 0 means: no own colour.
#define RS_COLOR_SLOT 0x02000000u
#define RS_COLOR_RAW  0x01000000u

static UINT_PTR Rs_ColorTag(COLORREF color)
{
    int p, i;
    // First the active palette, then the other one: a page that kept a colour from
    // before the switch and sets it again hits the same slot that way.
    for (p = 0; p < 2; p++) {
        const struct RsPalette *pal = p == 0 ? g_rsPal : &g_rsPalettes[g_dark ? 0 : 1];
        for (i = 0; i < RS_PAL_COUNT; i++)
            if (pal->c[i] == color)
                return RS_COLOR_SLOT | (UINT_PTR)i;
    }
    return RS_COLOR_RAW | (color & 0x00FFFFFFu);
}

static COLORREF Rs_ColorResolve(UINT_PTR tag, COLORREF fallback)
{
    if ((tag & 0xFF000000u) == RS_COLOR_SLOT && (tag & 0xFFu) < (UINT_PTR)RS_PAL_COUNT)
        return g_rsPal->c[tag & 0xFFu];
    if ((tag & 0xFF000000u) == RS_COLOR_RAW)
        return (COLORREF)(tag & 0x00FFFFFFu);
    return fallback;
}

static int Rs_ThemeParse(const wchar_t *s)
{
    if (!s)
        return -1;
    if (_wcsicmp(s, L"dark") == 0)
        return RS_THEME_DARK;
    if (_wcsicmp(s, L"light") == 0)
        return RS_THEME_LIGHT;
    if (_wcsicmp(s, L"system") == 0)
        return RS_THEME_SYSTEM;
    return -1;
}

// 1 if Windows has the dark scheme set for programs.
static int Rs_SystemDark(void)
{
    HKEY key;
    DWORD value = 1, size = (DWORD)sizeof(value), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
        value = 1;
    RegCloseKey(key);
    return value == 0;
}

static int Rs_ThemeWantsDark(int mode)
{
    if (mode == RS_THEME_DARK)
        return 1;
    if (mode == RS_THEME_LIGHT)
        return 0;
    return Rs_SystemDark();
}

// Title bar light or dark. dwmapi only comes in here (LoadLibrary), the
// exe needs no further library for it. Attribute 20 is
// DWMWA_USE_IMMERSIVE_DARK_MODE from Windows 10 20H1 on, 19 the value before.
typedef HRESULT (WINAPI *RsDwmSetAttr)(HWND, DWORD, LPCVOID, DWORD);

static void Rs_TitleBarTheme(HWND hwnd)
{
    static int tried;
    static RsDwmSetAttr setAttr;
    BOOL on = g_dark ? TRUE : FALSE;
    if (!tried) {
        HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        tried = 1;
        if (dwm)
            setAttr = (RsDwmSetAttr)GetProcAddress(dwm, "DwmSetWindowAttribute");
    }
    if (!setAttr || !hwnd)
        return;
    if (FAILED(setAttr(hwnd, 20, &on, (DWORD)sizeof(on))))
        setAttr(hwnd, 19, &on, (DWORD)sizeof(on));
}

// Brushes for page, card and input fields from the active palette. The old ones
// are only freed when the window class already has the new one.
static void Rs_MakeBrushes(void)
{
    HBRUSH oldPage = g_brPage, oldCard = g_brCard, oldInput = g_brInput;
    g_brPage = CreateSolidBrush(RS_COL_PAGE);
    g_brCard = CreateSolidBrush(RS_COL_CARD);
    g_brInput = CreateSolidBrush(g_rsShellColors[g_dark].input);
    if (g_main)
        SetClassLongPtrW(g_main, GCLP_HBRBACKGROUND, (LONG_PTR)g_brPage);
    if (oldPage)
        DeleteObject(oldPage);
    if (oldCard)
        DeleteObject(oldCard);
    if (oldInput)
        DeleteObject(oldInput);
}

static void Rs_SetPalette(int dark)
{
    g_dark = dark ? 1 : 0;
    g_rsPal = &g_rsPalettes[g_dark];
    Rs_MakeBrushes();
}

// Theme and colours of a control according to the active scheme. Labels
// and the main button paint through the shell and need nothing.
static void Rs_ThemeControl(HWND h)
{
    wchar_t cls[64];
    const wchar_t *dark = L"DarkMode_Explorer";
    const wchar_t *light = NULL;
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);

    if (!GetClassNameW(h, cls, 64))
        return;
    if (_wcsicmp(cls, L"Button") == 0) {
        if ((style & BS_TYPEMASK) == BS_OWNERDRAW)
            return;
    } else if (_wcsicmp(cls, L"Edit") == 0) {
        // With scroll bars DarkMode_Explorer, otherwise the scroll bars would stay light.
        if (!(style & (WS_VSCROLL | WS_HSCROLL)))
            dark = L"DarkMode_CFD";
    } else if (_wcsicmp(cls, L"ComboBox") == 0) {
        COMBOBOXINFO cbi;
        dark = L"DarkMode_CFD";
        memset(&cbi, 0, sizeof(cbi));
        cbi.cbSize = sizeof(cbi);
        if (GetComboBoxInfo(h, &cbi) && cbi.hwndList)
            SetWindowTheme(cbi.hwndList, g_dark ? L"DarkMode_Explorer" : NULL, NULL);
    } else if (_wcsicmp(cls, WC_LISTVIEWW) == 0) {
        HWND header = ListView_GetHeader(h);
        light = L"Explorer";
        if (header)
            SetWindowTheme(header, g_dark ? L"DarkMode_ItemsView" : NULL, NULL);
        if (g_dark) {
            COLORREF input = g_rsShellColors[1].input;
            ListView_SetBkColor(h, input);
            ListView_SetTextBkColor(h, input);
            ListView_SetTextColor(h, RS_COL_TEXT);
        } else if (GetPropW(h, L"RsLvSaved")) {
            ListView_SetBkColor(h, (COLORREF)(UINT_PTR)GetPropW(h, L"RsLvBk"));
            ListView_SetTextBkColor(h, (COLORREF)(UINT_PTR)GetPropW(h, L"RsLvTextBk"));
            ListView_SetTextColor(h, (COLORREF)(UINT_PTR)GetPropW(h, L"RsLvText"));
        }
    } else if (_wcsicmp(cls, L"ListBox") != 0 && _wcsicmp(cls, L"RsMsgList") != 0) {
        // Labels, pages, sidebar, headers (via the list).
        return;
    }
    SetWindowTheme(h, g_dark ? dark : light, NULL);
    InvalidateRect(h, NULL, TRUE);
}

// On creation: only in the dark scheme; in the light scheme the controls keep
// Windows' defaults.
static HWND Rs_Themed(HWND h)
{
    if (h && g_dark)
        Rs_ThemeControl(h);
    return h;
}

static BOOL CALLBACK Rs_ThemeChild(HWND child, LPARAM unused)
{
    (void)unused;
    Rs_ThemeControl(child);
    return TRUE;
}

// Switch scheme: palette, brushes, title bar, all controls, redraw.
// save = 1 writes the choice into the ini (never in automation).
static void Rs_ApplyTheme(int dark, int save)
{
    Rs_SetPalette(dark);
    if (g_main) {
        Rs_TitleBarTheme(g_main);
        EnumChildWindows(g_main, Rs_ThemeChild, 0);
        SetWindowPos(g_main, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RedrawWindow(g_main, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    if (save)
        Rs_ConfigSet(L"theme", g_dark ? L"dark" : L"light");
}

static void Rs_ToggleTheme(void)
{
    g_themeMode = g_dark ? RS_THEME_LIGHT : RS_THEME_DARK;
    Rs_ApplyTheme(!g_dark, 1);
}

// Check boxes in the dark scheme. With visual styles Windows paints the text of a
// check box in the theme's colour and ignores SetTextColor from
// WM_CTLCOLORSTATIC - on a dark background it would be black, even under
// DarkMode_Explorer. Without a theme (SetWindowTheme "", "") the colour would get through,
// but Windows then painted the box as a white 3D field and disabled text
// embossed (light with a shadow): disabled modes would look lighter than free ones.
// That is why the shell paints itself here: the box via the theme of the
// control (DarkMode_Explorer::Button, where missing Button), background and
// text in the colours of the page's WM_CTLCOLORSTATIC, disabled RS_COL_MUTED.
// Clicking, keys and state stay with Windows' button. In the light scheme
// everything goes to Windows unchanged.
static void Rs_CheckPaint(HWND h, HDC target)
{
    RECT rc, g, t;
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ oldBmp, oldFont;
    HBRUSH br;
    HFONT font;
    HTHEME theme;
    SIZE glyph;
    wchar_t text[256];
    LRESULT check = SendMessageW(h, BM_GETCHECK, 0, 0);
    LRESULT state = SendMessageW(h, BM_GETSTATE, 0, 0);
    LRESULT ui = SendMessageW(h, WM_QUERYUISTATE, 0, 0);
    int enabled = IsWindowEnabled(h) ? 1 : 0;
    int part;
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS;

    GetClientRect(h, &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
        return;
    mem = CreateCompatibleDC(target);
    bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    oldBmp = SelectObject(mem, bmp);
    font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
    oldFont = SelectObject(mem, font ? font : Rs_Font(RS_FONT_BODY));
    br = (HBRUSH)SendMessageW(GetParent(h), WM_CTLCOLORSTATIC, (WPARAM)mem, (LPARAM)h);
    FillRect(mem, &rc, br ? br : g_brPage);
    SetBkMode(mem, TRANSPARENT);
    if (!enabled)
        SetTextColor(mem, RS_COL_MUTED);

    part = check == BST_CHECKED ? CBS_CHECKEDNORMAL
         : check == BST_INDETERMINATE ? CBS_MIXEDNORMAL : CBS_UNCHECKEDNORMAL;
    if (!enabled)
        part += 3;      // ..DISABLED
    else if (state & BST_PUSHED)
        part += 2;      // ..PRESSED
    else if (state & BST_HOT)
        part += 1;      // ..HOT

    glyph.cx = Rs_Px(13);
    glyph.cy = Rs_Px(13);
    theme = OpenThemeData(h, L"Button");
    if (theme)
        GetThemePartSize(theme, mem, BP_CHECKBOX, part, NULL, TS_DRAW, &glyph);
    g.left = 0;
    g.top = (rc.bottom - glyph.cy) / 2;
    g.right = glyph.cx;
    g.bottom = g.top + glyph.cy;
    if (theme) {
        DrawThemeBackground(theme, mem, BP_CHECKBOX, part, &g, NULL);
        CloseThemeData(theme);
    } else {
        UINT dfc = DFCS_BUTTONCHECK;
        if (check == BST_CHECKED)
            dfc |= DFCS_CHECKED;
        if (!enabled)
            dfc |= DFCS_INACTIVE;
        else if (state & BST_PUSHED)
            dfc |= DFCS_PUSHED;
        DrawFrameControl(mem, &g, DFC_BUTTON, dfc);
    }

    GetWindowTextW(h, text, 255);
    text[255] = 0;
    t = rc;
    t.left = g.right + Rs_Px(4);
    if (ui & UISF_HIDEACCEL)
        flags |= DT_HIDEPREFIX;
    DrawTextW(mem, text, -1, &t, flags);
    if (GetFocus() == h && !(ui & UISF_HIDEFOCUS) && text[0]) {
        RECT f = t;
        int th;
        DrawTextW(mem, text, -1, &f, flags | DT_CALCRECT);
        th = f.bottom - f.top;
        f.top = (rc.bottom - th) / 2;
        f.bottom = f.top + th;
        if (f.right > rc.right)
            f.right = rc.right;
        f.left -= 1;
        DrawFocusRect(mem, &f);
    }

    BitBlt(target, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldFont);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static LRESULT CALLBACK Rs_CheckSub(HWND h, UINT msg, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(h, Rs_CheckSub, id);
        return DefSubclassProc(h, msg, wParam, lParam);
    }
    if (!g_dark)
        return DefSubclassProc(h, msg, wParam, lParam);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        Rs_CheckPaint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        Rs_CheckPaint(h, (HDC)wParam);
        return 0;
    // State change: Windows may draw immediately by itself during it.
    // After that the shell paints once more.
    case BM_SETCHECK:
    case BM_SETSTATE:
    case BM_SETSTYLE:
    case WM_ENABLE:
    case WM_SETTEXT:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_UPDATEUISTATE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CAPTURECHANGED: {
        LRESULT r = DefSubclassProc(h, msg, wParam, lParam);
        InvalidateRect(h, NULL, FALSE);
        return r;
    }
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

// List: in the dark scheme DarkMode_ItemsView paints the header dark, but its
// text black. The header reports NM_CUSTOMDRAW to the list, not to
// the page - that is why the colour sits here.
static LRESULT CALLBACK Rs_ListViewSub(HWND h, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(h, Rs_ListViewSub, id);
    } else if (msg == WM_NOTIFY && g_dark && lParam) {
        NMHDR *hdr = (NMHDR *)lParam;
        if (hdr->code == NM_CUSTOMDRAW && hdr->hwndFrom == ListView_GetHeader(h)) {
            NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lParam;
            if (cd->dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, RS_COL_TEXT);
                return CDRF_DODEFAULT;
            }
        }
    }
    return DefSubclassProc(h, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------

static struct RsPageState *Rs_State(HWND page)
{
    return (struct RsPageState *)GetWindowLongPtrW(page, GWLP_USERDATA);
}

void Rs_CardClear(HWND page)
{
    struct RsPageState *st = Rs_State(page);
    if (st)
        st->cardCount = 0;
}

void Rs_CardAdd(HWND page, const RECT *outer, const wchar_t *title)
{
    struct RsPageState *st = Rs_State(page);
    struct RsCard *c;
    if (!st || st->cardCount >= RS_MAX_CARDS)
        return;
    c = &st->cards[st->cardCount++];
    c->rc = *outer;
    c->hasTitle = title && *title;
    c->title[0] = 0;
    if (c->hasTitle) {
        wcsncpy(c->title, title, 79);
        c->title[79] = 0;
    }
}

RECT Rs_CardInner(const RECT *outer, int hasTitle)
{
    RECT r = *outer;
    r.left += Rs_Px(18);
    r.right -= Rs_Px(18);
    r.top += hasTitle ? Rs_Px(50) : Rs_Px(16);
    r.bottom -= Rs_Px(16);
    return r;
}

int Rs_PageTop(void)
{
    return Rs_Px(104);
}

static int Rs_InCard(struct RsPageState *st, HWND page, HWND ctl)
{
    RECT r;
    POINT pt;
    int i;
    if (!st)
        return 0;
    GetWindowRect(ctl, &r);
    pt.x = (r.left + r.right) / 2;
    pt.y = (r.top + r.bottom) / 2;
    ScreenToClient(page, &pt);
    for (i = 0; i < st->cardCount; i++)
        if (PtInRect(&st->cards[i].rc, pt))
            return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

static HWND Rs_Make(HWND page, int id, const wchar_t *cls, const wchar_t *text,
                    DWORD style, DWORD exStyle, int font)
{
    HWND h = CreateWindowExW(exStyle, cls, text ? text : L"", WS_CHILD | WS_VISIBLE | style,
                             0, 0, 10, 10, page, (HMENU)(INT_PTR)id, g_inst, NULL);
    if (h) {
        SetPropW(h, L"RsFont", (HANDLE)(INT_PTR)(font + 1));
        SendMessageW(h, WM_SETFONT, (WPARAM)Rs_Font(font), FALSE);
    }
    return h;
}

HWND Rs_Label(HWND page, int id, const wchar_t *text, int font)
{
    return Rs_Make(page, id, L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, font);
}

HWND Rs_Edit(HWND page, int id, const wchar_t *text, DWORD extraStyle)
{
    return Rs_Themed(Rs_Make(page, id, L"EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
                             WS_EX_CLIENTEDGE, RS_FONT_BODY));
}

HWND Rs_Button(HWND page, int id, const wchar_t *text)
{
    return Rs_Themed(Rs_Make(page, id, L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, 0, RS_FONT_BODY));
}

HWND Rs_PrimaryButton(HWND page, int id, const wchar_t *text)
{
    HWND h = Rs_Make(page, id, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, 0, RS_FONT_BOLD);
    if (h)
        SetPropW(h, L"RsPrimary", (HANDLE)1);
    return h;
}

HWND Rs_Check(HWND page, int id, const wchar_t *text)
{
    HWND h = Rs_Make(page, id, L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, 0, RS_FONT_BODY);
    // The subclass only paints in the dark scheme (Rs_CheckPaint).
    if (h)
        SetWindowSubclass(h, Rs_CheckSub, 1, 0);
    return Rs_Themed(h);
}

HWND Rs_Combo(HWND page, int id)
{
    return Rs_Themed(Rs_Make(page, id, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                             0, RS_FONT_BODY));
}

HWND Rs_ListBox(HWND page, int id, DWORD extraStyle)
{
    return Rs_Themed(Rs_Make(page, id, L"LISTBOX", L"",
                             WS_TABSTOP | WS_VSCROLL | WS_BORDER | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | extraStyle,
                             0, RS_FONT_BODY));
}

HWND Rs_ListView(HWND page, int id, DWORD extraStyle)
{
    HWND h = Rs_Make(page, id, WC_LISTVIEWW, L"",
                     WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL | extraStyle,
                     0, RS_FONT_BODY);
    if (h) {
        ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(h, L"Explorer", NULL);
        // Remember Windows' colours: the light scheme restores them after a
        // switch (Rs_ThemeControl).
        SetPropW(h, L"RsLvBk", (HANDLE)(UINT_PTR)ListView_GetBkColor(h));
        SetPropW(h, L"RsLvTextBk", (HANDLE)(UINT_PTR)ListView_GetTextBkColor(h));
        SetPropW(h, L"RsLvText", (HANDLE)(UINT_PTR)ListView_GetTextColor(h));
        SetPropW(h, L"RsLvSaved", (HANDLE)1);
        SetWindowSubclass(h, Rs_ListViewSub, 1, 0);
    }
    return Rs_Themed(h);
}

void Rs_SetTextColor(HWND control, COLORREF color)
{
    SetPropW(control, L"RsColor", (HANDLE)Rs_ColorTag(color));
    InvalidateRect(control, NULL, TRUE);
}

void Rs_SetText(HWND control, const wchar_t *text)
{
    SetWindowTextW(control, text ? text : L"");
}

wchar_t *Rs_GetText(HWND control)
{
    int n = GetWindowTextLengthW(control);
    wchar_t *t = Rs_Alloc(((size_t)n + 2) * sizeof(wchar_t));
    GetWindowTextW(control, t, n + 1);
    return t;
}

static void Rs_DrawPrimary(DRAWITEMSTRUCT *di)
{
    wchar_t text[128];
    RECT rc = di->rcItem;
    COLORREF fill = RS_COL_ACCENT;
    HGDIOBJ oldFont;
    int radius = Rs_Px(8);

    if (di->itemState & ODS_DISABLED)
        fill = g_rsShellColors[g_dark].primaryOff;
    else if (di->itemState & ODS_SELECTED)
        fill = RS_COL_ACCENT_DK;
    FillRect(di->hDC, &rc, g_brCard);
    Rs_FillRound(di->hDC, &rc, radius, fill, fill);
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT)) {
        RECT f = rc;
        InflateRect(&f, -Rs_Px(3), -Rs_Px(3));
        Rs_FillRound(di->hDC, &f, radius, fill, RGB(255, 255, 255));
    }
    GetWindowTextW(di->hwndItem, text, 127);
    text[127] = 0;
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, (di->itemState & ODS_DISABLED) ? g_rsShellColors[g_dark].primaryOffText
                                                         : RGB(255, 255, 255));
    oldFont = SelectObject(di->hDC, Rs_Font(RS_FONT_BOLD));
    DrawTextW(di->hDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(di->hDC, oldFont);
}

// ---------------------------------------------------------------------------
// Message list
// ---------------------------------------------------------------------------

struct RsMsgItem {
    int severity;
    wchar_t *text;
    wchar_t *detail;
    int y;
    int h;
};

struct RsMsgList {
    struct RsMsgItem *items;
    int count;
    int cap;
    int scroll;
    int total;
    int laidWidth;
};

static struct RsMsgList *Rs_MsgData(HWND list)
{
    return (struct RsMsgList *)GetWindowLongPtrW(list, GWLP_USERDATA);
}

static void Rs_MsgLayout(HWND list)
{
    struct RsMsgList *m = Rs_MsgData(list);
    RECT rc;
    HDC dc;
    int i, y = 0, width;
    HGDIOBJ old;
    SCROLLINFO si;

    if (!m)
        return;
    GetClientRect(list, &rc);
    width = rc.right - Rs_Px(44) - GetSystemMetrics(SM_CXVSCROLL);
    if (width < Rs_Px(60))
        width = Rs_Px(60);
    dc = GetDC(list);
    old = SelectObject(dc, Rs_Font(RS_FONT_BODY));
    for (i = 0; i < m->count; i++) {
        struct RsMsgItem *it = &m->items[i];
        RECT t = { 0, 0, width, 0 };
        int h;
        SelectObject(dc, Rs_Font(RS_FONT_BODY));
        DrawTextW(dc, it->text, -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        h = t.bottom;
        if (it->detail && *it->detail) {
            RECT d = { 0, 0, width, 0 };
            SelectObject(dc, Rs_Font(RS_FONT_SMALL));
            DrawTextW(dc, it->detail, -1, &d, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            h += Rs_Px(2) + d.bottom;
        }
        it->y = y;
        it->h = h + Rs_Px(14);
        y += it->h;
    }
    SelectObject(dc, old);
    ReleaseDC(list, dc);
    m->total = y;
    m->laidWidth = rc.right;
    if (m->scroll > m->total - rc.bottom)
        m->scroll = m->total - rc.bottom;
    if (m->scroll < 0)
        m->scroll = 0;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = m->total > 0 ? m->total - 1 : 0;
    si.nPage = (UINT)rc.bottom;
    si.nPos = m->scroll;
    SetScrollInfo(list, SB_VERT, &si, TRUE);
}

static void Rs_MsgPaint(HWND list)
{
    struct RsMsgList *m = Rs_MsgData(list);
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc, mem;
    HBITMAP bmp;
    HGDIOBJ oldBmp, oldFont;
    int i;

    dc = BeginPaint(list, &ps);
    GetClientRect(list, &rc);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1);
    oldBmp = SelectObject(mem, bmp);
    FillRect(mem, &rc, g_brCard);
    SetBkMode(mem, TRANSPARENT);
    oldFont = SelectObject(mem, Rs_Font(RS_FONT_BODY));

    if (m && m->count == 0) {
        wchar_t empty[256];
        RECT t = rc;
        GetWindowTextW(list, empty, 255);
        empty[255] = 0;
        InflateRect(&t, -Rs_Px(8), -Rs_Px(8));
        SetTextColor(mem, RS_COL_MUTED);
        DrawTextW(mem, empty, -1, &t, DT_WORDBREAK | DT_NOPREFIX);
    }
    for (i = 0; m && i < m->count; i++) {
        struct RsMsgItem *it = &m->items[i];
        int top = it->y - m->scroll;
        int dot = Rs_Px(10);
        RECT t;
        HBRUSH br;
        HGDIOBJ oldBr, oldPen;
        if (top + it->h < 0 || top > rc.bottom)
            continue;
        br = CreateSolidBrush(Rs_SeverityColor(it->severity));
        oldBr = SelectObject(mem, br);
        oldPen = SelectObject(mem, GetStockObject(NULL_PEN));
        Ellipse(mem, Rs_Px(12), top + Rs_Px(12), Rs_Px(12) + dot + 1, top + Rs_Px(12) + dot + 1);
        SelectObject(mem, oldBr);
        SelectObject(mem, oldPen);
        DeleteObject(br);

        t.left = Rs_Px(32);
        t.right = rc.right - Rs_Px(8);
        t.top = top + Rs_Px(7);
        t.bottom = top + it->h;
        SelectObject(mem, Rs_Font(RS_FONT_BODY));
        SetTextColor(mem, it->severity == RS_SEV_ERROR ? RS_COL_ERROR : RS_COL_TEXT);
        {
            RECT calc = t;
            DrawTextW(mem, it->text, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            DrawTextW(mem, it->text, -1, &t, DT_WORDBREAK | DT_NOPREFIX);
            t.top = calc.bottom + Rs_Px(2);
        }
        if (it->detail && *it->detail) {
            SelectObject(mem, Rs_Font(RS_FONT_SMALL));
            SetTextColor(mem, RS_COL_MUTED);
            DrawTextW(mem, it->detail, -1, &t, DT_WORDBREAK | DT_NOPREFIX);
        }
        if (i + 1 < m->count) {
            RECT line = { Rs_Px(32), top + it->h - 1, rc.right - Rs_Px(8), top + it->h };
            HBRUSH lb = CreateSolidBrush(g_rsShellColors[g_dark].divider);
            FillRect(mem, &line, lb);
            DeleteObject(lb);
        }
    }
    SelectObject(mem, oldFont);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(list, &ps);
}

static void Rs_MsgScrollTo(HWND list, int pos)
{
    struct RsMsgList *m = Rs_MsgData(list);
    RECT rc;
    if (!m)
        return;
    GetClientRect(list, &rc);
    if (pos > m->total - rc.bottom)
        pos = m->total - rc.bottom;
    if (pos < 0)
        pos = 0;
    m->scroll = pos;
    SetScrollPos(list, SB_VERT, pos, TRUE);
    InvalidateRect(list, NULL, FALSE);
}

static LRESULT CALLBACK Rs_MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct RsMsgList *m = Rs_MsgData(hwnd);
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)Rs_Alloc(sizeof(struct RsMsgList)));
        return 0;
    case WM_DESTROY:
        if (m) {
            int i;
            for (i = 0; i < m->count; i++) {
                Rs_Free(m->items[i].text);
                Rs_Free(m->items[i].detail);
            }
            Rs_Free(m->items);
            Rs_Free(m);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    case WM_SIZE:
        Rs_MsgLayout(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Rs_MsgPaint(hwnd);
        return 0;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        return r;
    }
    case WM_MOUSEWHEEL:
        if (m)
            Rs_MsgScrollTo(hwnd, m->scroll - GET_WHEEL_DELTA_WPARAM(wParam) * Rs_Px(48) / WHEEL_DELTA);
        return 0;
    case WM_VSCROLL:
        if (m) {
            RECT rc;
            int pos = m->scroll;
            GetClientRect(hwnd, &rc);
            switch (LOWORD(wParam)) {
            case SB_LINEUP: pos -= Rs_Px(24); break;
            case SB_LINEDOWN: pos += Rs_Px(24); break;
            case SB_PAGEUP: pos -= rc.bottom; break;
            case SB_PAGEDOWN: pos += rc.bottom; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO si;
                memset(&si, 0, sizeof(si));
                si.cbSize = sizeof(si);
                si.fMask = SIF_TRACKPOS;
                GetScrollInfo(hwnd, SB_VERT, &si);
                pos = si.nTrackPos;
                break;
            }
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = m->total; break;
            }
            Rs_MsgScrollTo(hwnd, pos);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND Rs_MsgList(HWND page, int id)
{
    HWND h = CreateWindowExW(0, L"RsMsgList", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                             0, 0, 10, 10, page, (HMENU)(INT_PTR)id, g_inst, NULL);
    return Rs_Themed(h);   // the scroll bar
}

void Rs_MsgListClear(HWND list)
{
    struct RsMsgList *m = Rs_MsgData(list);
    int i;
    if (!m)
        return;
    for (i = 0; i < m->count; i++) {
        Rs_Free(m->items[i].text);
        Rs_Free(m->items[i].detail);
    }
    // The scroll position stays: a page rebuilds its list on every input,
    // and the list should not jump to the top then. Rs_MsgLayout
    // limits it to the new content.
    m->count = 0;
    Rs_MsgLayout(list);
    InvalidateRect(list, NULL, FALSE);
}

void Rs_MsgListAdd(HWND list, int severity, const wchar_t *text, const wchar_t *detail)
{
    struct RsMsgList *m = Rs_MsgData(list);
    struct RsMsgItem *it;
    if (!m)
        return;
    if (m->count == m->cap) {
        int cap = m->cap ? m->cap * 2 : 16;
        struct RsMsgItem *n = Rs_Alloc((size_t)cap * sizeof(*n));
        if (m->count)
            memcpy(n, m->items, (size_t)m->count * sizeof(*n));
        Rs_Free(m->items);
        m->items = n;
        m->cap = cap;
    }
    it = &m->items[m->count++];
    it->severity = severity;
    it->text = Rs_Dup(text);
    it->detail = detail && *detail ? Rs_Dup(detail) : NULL;
    Rs_MsgLayout(list);
    InvalidateRect(list, NULL, FALSE);
}

int Rs_MsgListCount(HWND list)
{
    struct RsMsgList *m = Rs_MsgData(list);
    return m ? m->count : 0;
}

void Rs_MsgListWrite(HWND list, FILE *f)
{
    static const char *const names[RS_SEV_COUNT] = { "ok", "info", "note", "warning", "error" };
    struct RsMsgList *m = Rs_MsgData(list);
    int i;
    if (!m || !f)
        return;
    for (i = 0; i < m->count; i++) {
        char *t = Rs_ToUtf8(m->items[i].text);
        char *d = Rs_ToUtf8(m->items[i].detail ? m->items[i].detail : L"");
        int s = m->items[i].severity;
        fprintf(f, "%s\t%s\t%s\n", (s >= 0 && s < RS_SEV_COUNT) ? names[s] : "info", t, d);
        Rs_Free(t);
        Rs_Free(d);
    }
}

// ---------------------------------------------------------------------------
// Child processes
// ---------------------------------------------------------------------------

struct RsJob {
    int id;
    HWND notify;
    HANDLE process;
    HANDLE read;
};

static void Rs_PostLine(struct RsJob *job, const char *line, int len)
{
    wchar_t *w;
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
        len--;
    w = Rs_FromUtf8(line, len);
    if (!PostMessageW(job->notify, RS_WM_JOB_LINE, (WPARAM)job->id, (LPARAM)w))
        Rs_Free(w);
}

static void Rs_JobSlotAdd(int id, HANDLE process)
{
    int i;

    EnterCriticalSection(&g_jobLock);
    for (i = 0; i < RS_JOB_SLOTS; i++) {
        if (!g_jobSlots[i].id) {
            g_jobSlots[i].id = id;
            g_jobSlots[i].process = process;
            break;
        }
    }
    LeaveCriticalSection(&g_jobLock);
}

static void Rs_JobSlotRemove(int id)
{
    int i;

    EnterCriticalSection(&g_jobLock);
    for (i = 0; i < RS_JOB_SLOTS; i++) {
        if (g_jobSlots[i].id == id) {
            g_jobSlots[i].id = 0;
            g_jobSlots[i].process = NULL;
        }
    }
    LeaveCriticalSection(&g_jobLock);
}

int Rs_KillJob(int id)
{
    int i, killed = 0;

    if (id <= 0)
        return 0;
    EnterCriticalSection(&g_jobLock);
    for (i = 0; i < RS_JOB_SLOTS; i++) {
        if (g_jobSlots[i].id == id && g_jobSlots[i].process) {
            killed = TerminateProcess(g_jobSlots[i].process, 1) ? 1 : 0;
            break;
        }
    }
    LeaveCriticalSection(&g_jobLock);
    return killed;
}

static DWORD WINAPI Rs_JobThread(LPVOID param)
{
    struct RsJob *job = param;
    DWORD code = 0;

    if (job->read) {
        size_t cap = 8192, len = 0;
        char *acc = Rs_Alloc(cap);
        char buf[4096];
        DWORD got;
        while (ReadFile(job->read, buf, sizeof(buf), &got, NULL) && got > 0) {
            DWORD i;
            for (i = 0; i < got; i++) {
                if (buf[i] == '\n') {
                    Rs_PostLine(job, acc, (int)len);
                    len = 0;
                    continue;
                }
                if (len + 1 >= cap) {
                    char *n = Rs_Alloc(cap * 2);
                    memcpy(n, acc, len);
                    Rs_Free(acc);
                    acc = n;
                    cap *= 2;
                }
                acc[len++] = buf[i];
            }
        }
        if (len > 0)
            Rs_PostLine(job, acc, (int)len);
        Rs_Free(acc);
        CloseHandle(job->read);
    }
    WaitForSingleObject(job->process, INFINITE);
    GetExitCodeProcess(job->process, &code);
    Rs_JobSlotRemove(job->id);
    CloseHandle(job->process);
    PostMessageW(job->notify, RS_WM_JOB_DONE, (WPARAM)job->id, (LPARAM)code);
    Rs_Free(job);
    return 0;
}

void Rs_AppendArg(wchar_t *cmdline, size_t cap, const wchar_t *arg)
{
    size_t n = wcslen(cmdline);
    const wchar_t *p;
    int quote = !arg || !*arg || wcspbrk(arg, L" \t\"") != NULL;

#define RS_PUT(ch) do { if (n + 1 < cap) cmdline[n++] = (ch); } while (0)
    if (n > 0)
        RS_PUT(L' ');
    if (!arg)
        arg = L"";
    if (!quote) {
        for (p = arg; *p; p++)
            RS_PUT(*p);
        cmdline[n] = 0;
        return;
    }
    RS_PUT(L'"');
    for (p = arg;; p++) {
        int slashes = 0;
        while (*p == L'\\') {
            slashes++;
            p++;
        }
        if (!*p) {
            while (slashes-- > 0) {
                RS_PUT(L'\\');
                RS_PUT(L'\\');
            }
            break;
        }
        if (*p == L'"') {
            while (slashes-- > 0) {
                RS_PUT(L'\\');
                RS_PUT(L'\\');
            }
            RS_PUT(L'\\');
            RS_PUT(L'"');
        } else {
            while (slashes-- > 0)
                RS_PUT(L'\\');
            RS_PUT(*p);
        }
    }
    RS_PUT(L'"');
    cmdline[n] = 0;
#undef RS_PUT
}

static int Rs_Launch(HWND notify, const wchar_t *exe, wchar_t *cmd, const wchar_t *cwd,
                     int capture, int noWindow)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE rd = NULL, wr = NULL, nul = INVALID_HANDLE_VALUE;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    BOOL ok;
    struct RsJob *job;
    HANDLE thread;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;

    // The lock keeps the inheritable ends of one job away from the children of
    // another: otherwise the first reader would only get its end of file when
    // the second child is done too.
    EnterCriticalSection(&g_jobLock);
    if (capture) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) {
            LeaveCriticalSection(&g_jobLock);
            return 0;
        }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          OPEN_EXISTING, 0, NULL);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = nul;
        si.hStdOutput = wr;
        si.hStdError = wr;
    }
    ok = CreateProcessW(exe, cmd, NULL, NULL, capture ? TRUE : FALSE,
                        noWindow ? CREATE_NO_WINDOW : 0, NULL, cwd, &si, &pi);
    if (wr)
        CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    LeaveCriticalSection(&g_jobLock);
    if (!ok) {
        if (rd)
            CloseHandle(rd);
        return 0;
    }
    CloseHandle(pi.hThread);

    job = Rs_Alloc(sizeof(*job));
    job->id = (int)InterlockedIncrement(&g_nextJob);
    job->notify = notify;
    job->process = pi.hProcess;
    job->read = rd;
    {
        int id = job->id;
        Rs_JobSlotAdd(id, pi.hProcess);
        thread = CreateThread(NULL, 0, Rs_JobThread, job, 0, NULL);
        if (!thread) {
            Rs_JobSlotRemove(id);
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            if (rd)
                CloseHandle(rd);
            Rs_Free(job);
            return 0;
        }
        CloseHandle(thread);
        return id;
    }
}

int Rs_RunRldpack(HWND notify, const wchar_t *const *args, int argc)
{
    wchar_t *cmd = Rs_Alloc(RS_CMD_CAP * sizeof(wchar_t));
    int i, id;
    Rs_AppendArg(cmd, RS_CMD_CAP, g_exePath);
    Rs_AppendArg(cmd, RS_CMD_CAP, L"--rldpack");
    for (i = 0; i < argc; i++)
        Rs_AppendArg(cmd, RS_CMD_CAP, args[i]);
    if (g_automating) {
        wchar_t *shown = Rs_Alloc(RS_CMD_CAP * sizeof(wchar_t));
        for (i = 0; i < argc; i++)
            Rs_AppendArg(shown, RS_CMD_CAP, args[i]);
        Rs_AutoLog(L"  rldpack %ls", shown);
        Rs_Free(shown);
    }
    id = Rs_Launch(notify, g_exePath, cmd, NULL, 1, 1);
    Rs_Free(cmd);
    return id;
}

int Rs_RunProcess(HWND notify, const wchar_t *exe, const wchar_t *cmdline,
                  const wchar_t *cwd, int capture)
{
    wchar_t *cmd = Rs_Alloc(RS_CMD_CAP * sizeof(wchar_t));
    int id;
    Rs_AppendArg(cmd, RS_CMD_CAP, exe);
    if (cmdline && *cmdline) {
        size_t n = wcslen(cmd);
        if (n + 1 + wcslen(cmdline) + 1 < RS_CMD_CAP) {
            cmd[n] = L' ';
            wcscpy(cmd + n + 1, cmdline);
        }
    }
    if (g_automating)
        Rs_AutoLog(L"  run: %ls", cmd);
    // With redirection without a console window: the game is a console program,
    // and without a window it also does not wait for Enter on an error exit
    // (NativeConsole_ShouldPauseOnError in main.c asks GetConsoleWindow).
    id = Rs_Launch(notify, exe, cmd, cwd, capture, capture);
    Rs_Free(cmd);
    return id;
}

int Rs_SplitMachine(wchar_t *line, wchar_t **fields, int maxFields)
{
    int n = 0;
    wchar_t *p;
    if (!line || line[0] != L'@' || maxFields <= 0)
        return 0;
    p = line + 1;
    fields[n++] = p;
    while (*p && n < maxFields) {
        if (*p == L'\t') {
            *p = 0;
            fields[n++] = p + 1;
        }
        p++;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Page windows
// ---------------------------------------------------------------------------

static void Rs_PagePaint(HWND hwnd)
{
    struct RsPageState *st = Rs_State(hwnd);
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(hwnd, &ps);
    HGDIOBJ oldFont;
    int i;

    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, g_brPage);
    SetBkMode(dc, TRANSPARENT);
    if (st && st->def) {
        RECT t = { Rs_Px(32), Rs_Px(24), rc.right - Rs_Px(32), Rs_Px(68) };
        oldFont = SelectObject(dc, Rs_Font(RS_FONT_TITLE));
        SetTextColor(dc, RS_COL_TEXT);
        DrawTextW(dc, st->def->title, -1, &t, DT_SINGLELINE | DT_NOPREFIX);
        t.top = Rs_Px(68);
        t.bottom = Rs_Px(92);
        SelectObject(dc, Rs_Font(RS_FONT_BODY));
        SetTextColor(dc, RS_COL_MUTED);
        DrawTextW(dc, st->def->subtitle, -1, &t, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        for (i = 0; i < st->cardCount; i++) {
            struct RsCard *c = &st->cards[i];
            Rs_FillRound(dc, &c->rc, Rs_Px(12), RS_COL_CARD, RS_COL_BORDER);
            if (c->hasTitle) {
                RECT ct = { c->rc.left + Rs_Px(18), c->rc.top + Rs_Px(14),
                            c->rc.right - Rs_Px(18), c->rc.top + Rs_Px(42) };
                SelectObject(dc, Rs_Font(RS_FONT_SECTION));
                SetTextColor(dc, RS_COL_TEXT);
                DrawTextW(dc, c->title, -1, &ct, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }
        SelectObject(dc, oldFont);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK Rs_PageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct RsPageState *st = Rs_State(hwnd);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Rs_PagePaint(hwnd);
        return 0;
    case WM_SIZE:
        if (st && st->ready && st->def->layout) {
            st->def->layout(hwnd, LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        UINT_PTR col = (UINT_PTR)GetPropW(ctl, L"RsColor");
        int card = Rs_InCard(st, hwnd, ctl);
        SetTextColor(dc, Rs_ColorResolve(col, RS_COL_TEXT));
        SetBkColor(dc, card ? RS_COL_CARD : RS_COL_PAGE);
        return (LRESULT)(card ? g_brCard : g_brPage);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        // Input fields, lists, the dropped-down combo list. In the light
        // scheme Windows' default colours.
        if (g_dark) {
            HDC dc = (HDC)wParam;
            SetTextColor(dc, RS_COL_TEXT);
            SetBkColor(dc, g_rsShellColors[1].input);
            return (LRESULT)g_brInput;
        }
        break;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lParam;
        if (di->CtlType == ODT_BUTTON && GetPropW(di->hwndItem, L"RsPrimary")) {
            Rs_DrawPrimary(di);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (st && st->ready && st->def->command)
            return st->def->command(hwnd, wParam, lParam);
        return 0;
    case WM_NOTIFY:
        if (st && st->ready && st->def->notify)
            return st->def->notify(hwnd, (NMHDR *)lParam);
        return 0;
    }
    if (st && st->ready && st->def->message) {
        int handled = 0;
        LRESULT r = st->def->message(hwnd, msg, wParam, lParam, &handled);
        if (handled)
            return r;
    }
    if (msg == RS_WM_JOB_LINE) {
        Rs_Free((void *)lParam);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The pages by name: the automation verb "page <name>" and the setting "page=".
static const wchar_t *const g_pageWords[RS_PAGE_COUNT] = { L"track", L"cups", L"char", L"test" };

// RS_PAGE_* of a name, -1 if unknown.
static int Rs_PageByWord(const wchar_t *word)
{
    int i;
    for (i = 0; i < RS_PAGE_COUNT; i++)
        if (wcscmp(word, g_pageWords[i]) == 0)
            return i;
    return -1;
}

HWND Rs_PageWindow(int id)
{
    return (id >= 0 && id < RS_PAGE_COUNT) ? g_pages[id] : NULL;
}

HWND Rs_MainWindow(void)
{
    return g_main;
}

void Rs_ShowPage(int id)
{
    int i;
    if (id < 0 || id >= RS_PAGE_COUNT)
        return;
    for (i = 0; i < RS_PAGE_COUNT; i++)
        if (i != id && g_pages[i])
            ShowWindow(g_pages[i], SW_HIDE);
    g_current = id;
    ShowWindow(g_pages[id], SW_SHOW);
    InvalidateRect(g_sidebar, NULL, FALSE);
    SendMessageW(g_pages[id], RS_WM_PAGE_SHOWN, 0, 0);
    Rs_ConfigSet(L"page", g_pageWords[id]);
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------

static const wchar_t *const g_navIcons[RS_PAGE_COUNT] = {
    L"\xE8B7",   // Folder
    L"\xE8FD",   // BulletedList
    L"\xE77B",   // Contact
    L"\xE768",   // Play
};

static RECT Rs_NavRect(int i)
{
    RECT r;
    RECT client;
    GetClientRect(g_sidebar, &client);
    r.left = Rs_Px(12);
    r.right = client.right - Rs_Px(12);
    r.top = Rs_Px(112) + i * Rs_Px(46);
    r.bottom = r.top + Rs_Px(40);
    return r;
}

// The entry "Dark mode" / "Light mode" at the bottom of the sidebar, above the
// footer. For Rs_NavHit and g_hoverNav it has the number RS_NAV_THEME.
#define RS_NAV_THEME RS_PAGE_COUNT

static RECT Rs_ThemeRect(void)
{
    RECT r;
    RECT client;
    GetClientRect(g_sidebar, &client);
    r.left = Rs_Px(12);
    r.right = client.right - Rs_Px(12);
    r.bottom = client.bottom - Rs_Px(72);
    r.top = r.bottom - Rs_Px(36);
    return r;
}

static void Rs_SidebarPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    RECT rc;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ oldBmp, oldFont;
    HBRUSH bg;
    int i;

    GetClientRect(hwnd, &rc);
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, rc.right > 0 ? rc.right : 1, rc.bottom > 0 ? rc.bottom : 1);
    oldBmp = SelectObject(mem, bmp);
    bg = CreateSolidBrush(RS_COL_SIDEBAR);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    {
        RECT t = { Rs_Px(24), Rs_Px(28), rc.right - Rs_Px(12), Rs_Px(60) };
        RECT mark = { Rs_Px(24), Rs_Px(30), Rs_Px(30), Rs_Px(56) };
        HBRUSH acc = CreateSolidBrush(RS_COL_ACCENT);
        FillRect(mem, &mark, acc);
        DeleteObject(acc);
        t.left = Rs_Px(40);
        oldFont = SelectObject(mem, Rs_Font(RS_FONT_SECTION));
        SetTextColor(mem, RGB(255, 255, 255));
        DrawTextW(mem, L"Reload Studio", -1, &t, DT_SINGLELINE | DT_NOPREFIX);
        t.top = Rs_Px(56);
        t.bottom = Rs_Px(80);
        SelectObject(mem, Rs_Font(RS_FONT_SMALL));
        SetTextColor(mem, RGB(150, 156, 170));
        DrawTextW(mem, L"CTR Reload track tools", -1, &t, DT_SINGLELINE | DT_NOPREFIX);
        // Version and build ID, free up to the navigation (from 112)
        t.top = Rs_Px(76);
        t.bottom = Rs_Px(96);
        SetTextColor(mem, RGB(120, 126, 140));
        DrawTextW(mem, RS_VERSION_W L" (" RS_BUILD_ID_W L")", -1, &t,
                  DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    for (i = 0; i < RS_PAGE_COUNT; i++) {
        RECT r = Rs_NavRect(i);
        RECT t;
        int sel = i == g_current;
        if (sel || i == g_hoverNav)
            Rs_FillRound(mem, &r, Rs_Px(8), sel ? RGB(46, 50, 62) : RGB(36, 39, 49),
                         sel ? RGB(46, 50, 62) : RGB(36, 39, 49));
        if (sel) {
            RECT bar = { r.left, r.top + Rs_Px(10), r.left + Rs_Px(3), r.bottom - Rs_Px(10) };
            HBRUSH acc = CreateSolidBrush(RS_COL_ACCENT);
            FillRect(mem, &bar, acc);
            DeleteObject(acc);
        }
        t = r;
        t.left += Rs_Px(16);
        SelectObject(mem, g_iconFont);
        SetTextColor(mem, sel ? RGB(255, 255, 255) : RGB(150, 156, 170));
        DrawTextW(mem, g_navIcons[i], -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        t.left += Rs_Px(30);
        SelectObject(mem, Rs_Font(sel ? RS_FONT_BOLD : RS_FONT_BODY));
        SetTextColor(mem, sel ? RGB(255, 255, 255) : RGB(200, 204, 214));
        DrawTextW(mem, g_defs[i]->navName, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    {
        // Toggle scheme: moon for "Dark mode", sun for "Light mode".
        RECT r = Rs_ThemeRect();
        RECT t = r;
        if (g_hoverNav == RS_NAV_THEME)
            Rs_FillRound(mem, &r, Rs_Px(8), RGB(36, 39, 49), RGB(36, 39, 49));
        t.left += Rs_Px(16);
        SelectObject(mem, g_iconFont);
        SetTextColor(mem, RGB(150, 156, 170));
        DrawTextW(mem, g_dark ? L"\xE706" : L"\xE708", -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        t.left += Rs_Px(30);
        SelectObject(mem, Rs_Font(RS_FONT_BODY));
        SetTextColor(mem, RGB(200, 204, 214));
        DrawTextW(mem, g_dark ? L"Light mode" : L"Dark mode", -1, &t,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    {
        RECT t = { Rs_Px(24), rc.bottom - Rs_Px(60), rc.right - Rs_Px(12), rc.bottom - Rs_Px(40) };
        SelectObject(mem, Rs_Font(RS_FONT_SMALL));
        SetTextColor(mem, RGB(120, 126, 140));
        DrawTextW(mem, L"Container format 4.1", -1, &t, DT_SINGLELINE | DT_NOPREFIX);
        t.top += Rs_Px(18);
        t.bottom += Rs_Px(18);
        DrawTextW(mem, L"Packing and checks: rldpack", -1, &t, DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(mem, oldFont);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static int Rs_NavHit(LPARAM lParam)
{
    POINT pt;
    int i;
    pt.x = (short)LOWORD(lParam);
    pt.y = (short)HIWORD(lParam);
    for (i = 0; i < RS_PAGE_COUNT; i++) {
        RECT r = Rs_NavRect(i);
        if (PtInRect(&r, pt))
            return i;
    }
    {
        RECT r = Rs_ThemeRect();
        if (PtInRect(&r, pt))
            return RS_NAV_THEME;
    }
    return -1;
}

static LRESULT CALLBACK Rs_SidebarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Rs_SidebarPaint(hwnd);
        return 0;
    case WM_LBUTTONDOWN: {
        int hit = Rs_NavHit(lParam);
        if (hit == RS_NAV_THEME)
            Rs_ToggleTheme();
        else if (hit >= 0 && hit != g_current)
            Rs_ShowPage(hit);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int hit = Rs_NavHit(lParam);
        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme;
            memset(&tme, 0, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            g_trackingMouse = 1;
        }
        if (hit != g_hoverNav) {
            g_hoverNav = hit;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trackingMouse = 0;
        g_hoverNav = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (Rs_NavHit(MAKELPARAM(pt.x, pt.y)) >= 0) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

// A 24-bit DIB in window size; bits points to the rows (from the bottom).
static HBITMAP Rs_ShotBitmap(HDC screen, int w, int h, BITMAPINFO *bi, void **bits)
{
    memset(bi, 0, sizeof(*bi));
    bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi->bmiHeader.biWidth = w;
    bi->bmiHeader.biHeight = h;
    bi->bmiHeader.biPlanes = 1;
    bi->bmiHeader.biBitCount = 24;
    bi->bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(screen, bi, DIB_RGB_COLORS, bits, NULL, 0);
}

// 1 if all pixels are equal - then the capture has seen nothing.
static int Rs_ShotFlat(const unsigned char *bits, int stride, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y += 7)
        for (x = 0; x < w; x += 5) {
            const unsigned char *p = bits + (size_t)y * stride + (size_t)x * 3;
            if (p[0] != bits[0] || p[1] != bits[1] || p[2] != bits[2])
                return 0;
        }
    return 1;
}

// The screenshot shows the client area of the window (without the title bar).
//
// First PrintWindow with PW_RENDERFULLCONTENT over the whole window and the
// client area cut out of it; PW_CLIENTONLY together with PW_RENDERFULLCONTENT
// gave a black picture here. If the picture stays single-coloured, the
// screen is read at the window's location, with the window briefly
// topmost for that.
static int Rs_Shot(const wchar_t *path)
{
    RECT wr, cr;
    POINT origin = { 0, 0 };
    int w, h, ww, wh, stride, wstride, y, ok = 0;
    HDC screen, mem;
    BITMAPINFO bi, wbi;
    void *bits = NULL, *wbits = NULL;
    HBITMAP bmp, wbmp;
    HGDIOBJ old;
    FILE *f;
    BITMAPFILEHEADER fh;

    RedrawWindow(g_main, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    GetWindowRect(g_main, &wr);
    GetClientRect(g_main, &cr);
    ClientToScreen(g_main, &origin);
    w = cr.right;
    h = cr.bottom;
    ww = wr.right - wr.left;
    wh = wr.bottom - wr.top;
    if (w <= 0 || h <= 0 || ww <= 0 || wh <= 0)
        return 0;

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    bmp = Rs_ShotBitmap(screen, w, h, &bi, &bits);
    wbmp = Rs_ShotBitmap(screen, ww, wh, &wbi, &wbits);
    stride = (w * 3 + 3) & ~3;
    wstride = (ww * 3 + 3) & ~3;
    if (bmp && wbmp) {
        int dx = origin.x - wr.left, dy = origin.y - wr.top;
        old = SelectObject(mem, wbmp);
        PrintWindow(g_main, mem, 2 /* PW_RENDERFULLCONTENT */);
        GdiFlush();
        SelectObject(mem, old);
        // Rows lie from bottom to top: client row y is in window row dy + y.
        for (y = 0; y < h && dy + y < wh; y++) {
            unsigned char *dst = (unsigned char *)bits + (size_t)(h - 1 - y) * stride;
            const unsigned char *src = (const unsigned char *)wbits + (size_t)(wh - 1 - (dy + y)) * wstride + (size_t)dx * 3;
            memcpy(dst, src, (size_t)((w <= ww - dx) ? w : ww - dx) * 3);
        }
        if (Rs_ShotFlat((const unsigned char *)bits, stride, w, h)) {
            Rs_AutoLog(L"  shot: PrintWindow gave a flat picture - reading the screen instead");
            SetWindowPos(g_main, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            RedrawWindow(g_main, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            Sleep(300);
            old = SelectObject(mem, bmp);
            BitBlt(mem, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY);
            GdiFlush();
            SelectObject(mem, old);
            SetWindowPos(g_main, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
        memset(&fh, 0, sizeof(fh));
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fh.bfSize = fh.bfOffBits + (DWORD)(stride * h);
        f = _wfopen(path, L"wb");
        if (f) {
            fwrite(&fh, sizeof(fh), 1, f);
            fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
            fwrite(bits, (size_t)stride, (size_t)h, f);
            fclose(f);
            ok = 1;
        }
    }
    if (bmp)
        DeleteObject(bmp);
    if (wbmp)
        DeleteObject(wbmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return ok;
}

// ---------------------------------------------------------------------------
// Automation
// ---------------------------------------------------------------------------

static void Rs_AutoQueue(const wchar_t *step)
{
    const wchar_t *sp;
    struct RsAutoStep *s;
    if (g_autoCount >= RS_MAX_AUTO)
        return;
    while (*step == L' ')
        step++;
    s = &g_auto[g_autoCount++];
    sp = wcschr(step, L' ');
    if (sp) {
        size_t n = (size_t)(sp - step);
        s->verb = Rs_Alloc((n + 1) * sizeof(wchar_t));
        memcpy(s->verb, step, n * sizeof(wchar_t));
        while (*sp == L' ')
            sp++;
        s->arg = Rs_Dup(sp);
    } else {
        s->verb = Rs_Dup(step);
        s->arg = Rs_Dup(L"");
    }
}

// How many whole numbers "%d %d" reads from an automation argument (0..2).
static int Rs_AutoNumbers(const wchar_t *arg)
{
    int a = 0, b = 0;
    int n = swscanf(arg, L"%d %d", &a, &b);
    return n < 0 ? 0 : n;
}

static int Rs_PageBusy(int id)
{
    const struct RsPageDef *def = g_defs[id];
    return def->busy ? def->busy(g_pages[id]) : 0;
}

static void Rs_AutoTick(void)
{
    struct RsAutoStep *s;
    int i, busy = 0;

    for (i = 0; i < RS_PAGE_COUNT; i++)
        busy |= Rs_PageBusy(i);
    if (g_autoWaiting) {
        if (busy)
            return;
        g_autoWaiting = 0;
        g_autoSettleUntil = GetTickCount() + 200;
        return;
    }
    if (busy || GetTickCount() < g_autoSettleUntil)
        return;
    if (g_autoNext >= g_autoCount) {
        KillTimer(g_main, RS_TIMER_AUTO);
        Rs_AutoLog(L"automation finished, %d step(s) failed", g_autoFailed);
        return;
    }
    s = &g_auto[g_autoNext++];
    Rs_AutoLog(L"> %ls %ls", s->verb, s->arg);

    if (wcscmp(s->verb, L"page") == 0) {
        int id = Rs_PageByWord(s->arg);
        if (id < 0) {
            Rs_AutoLog(L"  FAIL: unknown page '%ls'", s->arg);
            g_autoFailed++;
        } else {
            Rs_ShowPage(id);
        }
        g_autoSettleUntil = GetTickCount() + 200;
        return;
    }
    if (wcscmp(s->verb, L"shot") == 0) {
        if (!Rs_Shot(s->arg)) {
            Rs_AutoLog(L"  FAIL: could not write %ls", s->arg);
            g_autoFailed++;
        }
        return;
    }
    if (wcscmp(s->verb, L"wait") == 0) {
        g_autoSettleUntil = GetTickCount() + (DWORD)_wtoi(s->arg);
        return;
    }
    // "size <w> <h>" is the window size. With exactly one number the verb belongs
    // to the page (the character size on the page "Character").
    if (wcscmp(s->verb, L"size") == 0 && Rs_AutoNumbers(s->arg) != 1) {
        int w = 0, h = 0;
        RECT r;
        if (swscanf(s->arg, L"%d %d", &w, &h) == 2 && w > 0 && h > 0) {
            r.left = 0;
            r.top = 0;
            r.right = w;
            r.bottom = h;
            AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
            SetWindowPos(g_main, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                         SWP_NOMOVE | SWP_NOZORDER);
        }
        g_autoSettleUntil = GetTickCount() + 200;
        return;
    }
    if (wcscmp(s->verb, L"theme") == 0) {
        int mode = Rs_ThemeParse(s->arg);
        if (mode < 0) {
            Rs_AutoLog(L"  FAIL: unknown theme '%ls' (dark, light or system)", s->arg);
            g_autoFailed++;
        } else {
            g_themeMode = mode;
            Rs_ApplyTheme(Rs_ThemeWantsDark(mode), 0);
        }
        g_autoSettleUntil = GetTickCount() + 200;
        return;
    }
    if (wcscmp(s->verb, L"quit") == 0) {
        KillTimer(g_main, RS_TIMER_AUTO);
        Rs_AutoLog(L"automation finished, %d step(s) failed", g_autoFailed);
        DestroyWindow(g_main);
        return;
    }
    if (g_current < 0) {
        g_autoFailed++;
        return;
    }
    {
        const struct RsPageDef *def = g_defs[g_current];
        int r = def->automate ? def->automate(g_pages[g_current], s->verb, s->arg) : RS_AUTO_UNKNOWN;
        if (r == RS_AUTO_UNKNOWN) {
            Rs_AutoLog(L"  FAIL: the page does not know '%ls'", s->verb);
            g_autoFailed++;
        } else if (r == RS_AUTO_FAIL) {
            Rs_AutoLog(L"  FAIL");
            g_autoFailed++;
        } else if (r == RS_AUTO_WAIT) {
            g_autoWaiting = 1;
        }
        g_autoSettleUntil = GetTickCount() + 150;
    }
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------

static void Rs_Layout(void)
{
    RECT rc;
    int side, i;
    GetClientRect(g_main, &rc);
    side = Rs_Px(RS_SIDEBAR_W);
    MoveWindow(g_sidebar, 0, 0, side, rc.bottom, TRUE);
    for (i = 0; i < RS_PAGE_COUNT; i++)
        if (g_pages[i])
            MoveWindow(g_pages[i], side, 0, rc.right - side, rc.bottom, TRUE);
}

static BOOL CALLBACK Rs_RefontChild(HWND child, LPARAM unused)
{
    INT_PTR font = (INT_PTR)GetPropW(child, L"RsFont");
    (void)unused;
    if (font > 0)
        SendMessageW(child, WM_SETFONT, (WPARAM)Rs_Font((int)font - 1), TRUE);
    if (GetWindowLongPtrW(child, GWLP_WNDPROC) == (LONG_PTR)Rs_MsgProc)
        Rs_MsgLayout(child);
    return TRUE;
}

static LRESULT CALLBACK Rs_MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_SIZE:
        Rs_Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        // The minimum size applies to the client area: sidebar plus a
        // page of 964 x 760 (96 dpi). Frame and title bar are added.
        MINMAXINFO *mm = (MINMAXINFO *)lParam;
        RECT r = { 0, 0, Rs_Px(1180), Rs_Px(760) };
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)g_dpi);
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_DPICHANGED: {
        RECT *r = (RECT *)lParam;
        g_dpi = HIWORD(wParam);
        Rs_MakeFonts();
        EnumChildWindows(hwnd, Rs_RefontChild, 0);
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Rs_Layout();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == RS_TIMER_AUTO)
            Rs_AutoTick();
        return 0;
    case WM_SETTINGCHANGE:
        // Windows switches light/dark: follow, as long as nobody has chosen explicitly.
        if (g_themeMode == RS_THEME_SYSTEM && lParam &&
            wcscmp((const wchar_t *)lParam, L"ImmersiveColorSet") == 0) {
            int dark = Rs_SystemDark();
            if (dark != g_dark)
                Rs_ApplyTheme(dark, 0);
        }
        break;
    case WM_CLOSE: {
        int i;
        if (!g_automating) {
            for (i = 0; i < RS_PAGE_COUNT; i++) {
                int handled = 0;
                LRESULT keep;
                if (!g_defs[i]->message)
                    continue;
                keep = g_defs[i]->message(g_pages[i], RS_WM_QUERY_CLOSE, 0, 0, &handled);
                if (handled && keep == 0)
                    return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(g_autoFailed ? 1 : 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Rs_RegisterClasses(void)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    wc.lpfnWndProc = Rs_MainProc;
    wc.lpszClassName = L"RsMain";
    wc.hbrBackground = g_brPage;
    wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon)
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    wc.hIcon = NULL;
    wc.hIconSm = NULL;
    wc.lpfnWndProc = Rs_SidebarProc;
    wc.lpszClassName = L"RsSidebar";
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = Rs_PageProc;
    wc.lpszClassName = L"RsPage";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = Rs_MsgProc;
    wc.lpszClassName = L"RsMsgList";
    RegisterClassExW(&wc);
}

// rldpack face: the arguments after "--rldpack" as UTF-8 to rldpack's main.
static int Rs_RunAsRldpack(int argc, wchar_t **wargv)
{
    char **argv = Rs_Alloc(((size_t)argc + 1) * sizeof(char *));
    int i, n = 0, r;

    // The runtime's narrow program path (_pgmptr) is only set by a program
    // with a narrow main. rldpack asks for it via _get_pgmptr to find the game data
    // for the music next to itself (Rld_ExeDir); unset, _get_pgmptr aborts
    // the program (0xC0000409). Here it is set as in the standalone rldpack,
    // under the manifest's UTF-8 code page.
    _configure_narrow_argv(_crt_argv_unexpanded_arguments);
    argv[n++] = Rs_ToUtf8(wargv[0]);
    for (i = 2; i < argc; i++)
        argv[n++] = Rs_ToUtf8(wargv[i]);
    argv[n] = NULL;
    r = Rldpack_Main(n, argv);
    fflush(stdout);
    fflush(stderr);
    return r;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdLine, int show)
{
    int argc = 0, i;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    INITCOMMONCONTROLSEX icc;
    MSG msg;
    wchar_t cfg[16];
    int startPage = RS_PAGE_TRACK;
    int cmdTheme = -1;
    RECT wr;

    (void)prev;
    (void)cmdLine;
    g_inst = inst;
    GetModuleFileNameW(NULL, g_exePath, MAX_PATH);
    Rs_PathDir(g_exeDir, MAX_PATH, g_exePath);

    if (argv && argc >= 2 && wcscmp(argv[1], L"--rldpack") == 0)
        return Rs_RunAsRldpack(argc, argv);

    for (i = 1; argv && i < argc; i++) {
        if (wcscmp(argv[i], L"--do") == 0 && i + 1 < argc) {
            Rs_AutoQueue(argv[++i]);
            g_automating = 1;
        } else if (wcscmp(argv[i], L"--log") == 0 && i + 1 < argc) {
            wcsncpy(g_autoLogPath, argv[++i], MAX_PATH - 1);
            g_autoLogPath[MAX_PATH - 1] = 0;
        } else if (wcscmp(argv[i], L"--theme") == 0 && i + 1 < argc) {
            cmdTheme = Rs_ThemeParse(argv[++i]);
        }
    }

    InitializeCriticalSection(&g_jobLock);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    memset(&icc, 0, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    Rs_ConfigInit();

    {
        HDC dc = GetDC(NULL);
        g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(NULL, dc);
    }
    Rs_MakeFonts();

    // Colour scheme: command line before ini before Windows. In automation
    // Rs_ConfigGet reads nothing; without --theme it is light there.
    if (cmdTheme >= 0) {
        g_themeMode = cmdTheme;
    } else if (g_automating) {
        g_themeMode = RS_THEME_LIGHT;
    } else {
        int mode;
        Rs_ConfigGet(L"theme", cfg, 16);
        mode = Rs_ThemeParse(cfg);
        g_themeMode = mode >= 0 ? mode : RS_THEME_SYSTEM;
    }
    Rs_SetPalette(Rs_ThemeWantsDark(g_themeMode));
    Rs_RegisterClasses();

    wr.left = 0;
    wr.top = 0;
    wr.right = Rs_Px(1280);
    wr.bottom = Rs_Px(820);
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
    g_main = CreateWindowExW(0, L"RsMain",
                             L"Reload Studio - " RS_VERSION_W L" (" RS_BUILD_ID_W L")",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                             NULL, NULL, inst, NULL);
    if (!g_main)
        return 1;
    if (g_dark)
        Rs_TitleBarTheme(g_main);   // before the first showing, otherwise it flashes light
    {
        UINT dpi = GetDpiForWindow(g_main);
        if (dpi && (int)dpi != g_dpi) {
            g_dpi = (int)dpi;
            Rs_MakeFonts();
        }
    }
    g_sidebar = CreateWindowExW(0, L"RsSidebar", L"", WS_CHILD | WS_VISIBLE,
                                0, 0, 10, 10, g_main, NULL, inst, NULL);
    for (i = 0; i < RS_PAGE_COUNT; i++) {
        struct RsPageState *st = Rs_Alloc(sizeof(*st));
        st->id = i;
        st->def = g_defs[i];
        g_pages[i] = CreateWindowExW(WS_EX_CONTROLPARENT, L"RsPage", g_defs[i]->navName,
                                     WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10,
                                     g_main, NULL, inst, st);
        if (st->def->create)
            st->def->create(g_pages[i]);
        st->ready = 1;
    }
    Rs_Layout();

    // "page=" holds the name of the page; a digit is the number of earlier
    // versions, from before the page "Character" (0 track, 1 cups, 2 test).
    Rs_ConfigGet(L"page", cfg, 16);
    if (cfg[0] >= L'0' && cfg[0] <= L'2' && cfg[1] == 0) {
        static const int oldPages[3] = { RS_PAGE_TRACK, RS_PAGE_CUPS, RS_PAGE_TEST };
        startPage = oldPages[cfg[0] - L'0'];
    } else if (Rs_PageByWord(cfg) >= 0) {
        startPage = Rs_PageByWord(cfg);
    }
    Rs_ShowPage(startPage);
    ShowWindow(g_main, g_automating ? SW_SHOWNORMAL : show);
    UpdateWindow(g_main);

    if (g_automating) {
        if (g_autoLogPath[0])
            DeleteFileW(g_autoLogPath);
        Rs_AutoLog(L"Reload Studio automation, %d step(s)", g_autoCount);
        g_autoSettleUntil = GetTickCount() + 300;
        SetTimer(g_main, RS_TIMER_AUTO, 50, NULL);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND page = g_current >= 0 ? g_pages[g_current] : NULL;
        if (page && IsDialogMessageW(page, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    LocalFree(argv);
    CoUninitialize();
    return (int)msg.wParam;
}
