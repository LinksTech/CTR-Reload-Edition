// rs_module.c - the registry of the modules and the loader of module DLLs (rs_module.h)
//
// The built-in modules are the table below, in the order of the sidebar; a
// module's place in the registry is its page ID (enum RsPageId for the
// built-in ones). Rs_ModulesInit fills the registry once, before the shell
// makes the pages: first the built-in modules, then the DLLs of the modules
// folder (sorted by file name) that load and fit.
//
// A DLL module never reaches the shell directly: the registry holds a copy of
// its page whose callbacks are the trampolines below. Each calls the module
// inside __try/__except; an exception turns the module off (Ext_Fault): no
// callback of it is called again, the shell takes its page away
// (Rs_ModuleTurnedOff) and one line says where it crashed. rs_module.h says
// what that cannot catch.
//
// THE LOG: with a modules folder every start writes modules.log anew in the
// folder of Rs_TempDir, one line per DLL ("loaded" or "not loaded - <why>")
// and a summary; each line goes to the automation log as well. Without the
// folder no file is written, the automation log gets one line.

#include "reloadstudio.h"
#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define RS_CORE_VERSION RS_CORE_VERSION_OF(CTR_TOOLS_VERSION_MAJOR, CTR_TOOLS_VERSION_MINOR, \
                                           CTR_TOOLS_VERSION_PATCH)
#define RS_MODULE_NEEDS_MAX 8
#define RS_EXT_MAX (RS_MAX_MODULES * 4)   // DLL files looked at, loaded or not

static const wchar_t *const k_needsTest[] = { L"test", NULL };
static const wchar_t *const k_needsCharTest[] = { L"char", L"test", NULL };

// Track and Character start the game through the page Test in game
// (Rs_TestGameExe, Rs_TestCharacter), so they need it; the Creator Kit builds
// its demo driver as Character does (rs_charhires.c) and tests it there. The
// icons are those the sidebar always had: Folder, BulletedList, Contact, Play;
// the Creator Kit's is Package (it has no entry of its own any more), that of
// Expansions Puzzle.
static const struct RsModuleDef g_builtin[RS_PAGE_BUILTIN_COUNT] = {
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"track", L"\xE8B7", 0, k_needsTest, &g_rsTrackPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"cups",  L"\xE8FD", 0, NULL,        &g_rsCupsPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"char",  L"\xE77B", 0, k_needsTest, &g_rsCharPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"test",  L"\xE768", 0, NULL,        &g_rsTestPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"kit",   L"\xE7B8", 0, k_needsCharTest, &g_rsKitPage },
    { RS_MODULE_ABI, sizeof(struct RsModuleDef), L"expansions", L"\xEA86", 0, NULL,   &g_rsExpansionsPage },
};

// The place of each built-in module beside its page (rs_module.h, BUILT-IN
// MODULES), kept out of struct RsModuleDef so that the module interface of
// the DLLs stays as it is (RS_MODULE_ABI 1). The Creator Kit is embedded in
// Character (old scripts' "page kit" opens it there and says so, rs_shell.c);
// Expansions is the bottom group.
static const struct {
    int host;                   // page ID of the host, -1 = an entry of its own
    const wchar_t *where;       // embedded: how to reach it in the host
    int bottom;                 // 1 = the bottom group of the sidebar
    const wchar_t *tip;         // the tooltip of its entry, NULL = its name (narrow sidebar only)
} g_builtinPlace[RS_PAGE_BUILTIN_COUNT] = {
    { -1, NULL, 0, NULL },
    { -1, NULL, 0, NULL },
    { -1, NULL, 0, NULL },
    { -1, NULL, 0, NULL },
    { RS_PAGE_CHAR, L"Build a character -> Creator Kit", 0, NULL },
    { -1, NULL, 1, g_rsExpansionsTip },
};

// A module DLL.
struct RsExt {
    wchar_t file[MAX_PATH];          // its file name in the modules folder
    HMODULE lib;
    wchar_t word[RS_MODULE_WORD_MAX + 1];
    wchar_t needs[RS_MODULE_NEEDS_MAX][RS_MODULE_WORD_MAX + 1];
    int needCount;
    struct RsPageDef cb;             // the module's page as it gave it
    struct RsPageDef page;           // the same with the trampolines: what the shell calls
    struct RsModuleDef def;          // what the registry holds (page = &page)
    int alive;                       // loaded and every module it needs is there
    int id;                          // page ID, -1 until registered
    int off;                         // crashed: turned off until the next start
};

static const struct RsModuleDef *g_modules[RS_MAX_MODULES];
static int g_moduleCount;
static struct RsExt g_ext[RS_EXT_MAX];
static int g_extCount;
static wchar_t g_logPath[MAX_PATH];  // modules.log, "" = no file this start
static DWORD g_faultCode;

// ---------------------------------------------------------------------------
// The log
// ---------------------------------------------------------------------------

static void Rs_ModuleLogV(const wchar_t *fmt, va_list ap)
{
    wchar_t line[1024];
    FILE *f;
    char *utf8;

    vswprintf(line, 1023, fmt, ap);
    line[1023] = 0;
    Rs_AutoLog(L"%ls", line);
    if (!g_logPath[0])
        return;
    f = _wfopen(g_logPath, L"ab");
    if (!f)
        return;
    utf8 = Rs_ToUtf8(line);
    fputs(utf8, f);
    fputs("\n", f);
    fclose(f);
    Rs_Free(utf8);
}

void __cdecl Rs_ModuleLog(const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Rs_ModuleLogV(fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// The core's table for module DLLs
// ---------------------------------------------------------------------------

static int Rs_ShowPageByWord(const wchar_t *word)
{
    int id = Rs_ModuleByWord(word);
    if (id < 0 || !Rs_ModuleOn(id))
        return 0;
    Rs_ShowPage(id);
    return 1;
}

static const struct RsCoreApi g_core = {
    RS_MODULE_ABI, sizeof(struct RsCoreApi), RS_CORE_VERSION, RS_VERSION_W, RS_BUILD_ID_W,
    Rs_ModuleLog, Rs_Alloc, Rs_Free, Rs_Dup,
    Rs_Px, Rs_Font, Rs_SeverityColor, Rs_PageTop, Rs_PageHeadBottom, Rs_PageViewSize,
    Rs_CardClear, Rs_CardAdd, Rs_CardInner,
    Rs_Label, Rs_Edit, Rs_Button, Rs_PrimaryButton, Rs_Check, Rs_Combo, Rs_ListBox,
    Rs_SetText, Rs_GetText, Rs_SetTextColor, Rs_SetTip, Rs_MsgList, Rs_MsgListClear, Rs_MsgListAdd,
    Rs_ConfigGet, Rs_ConfigSet, Rs_TempDir, Rs_ExeDir, Rs_FileExists, Rs_DirExists, Rs_PathJoin,
    Rs_BrowseFolder, Rs_BrowseOpenFile, Rs_Tell, Rs_AskYesNo, Rs_Automating,
    Rs_RunRldpack, Rs_RunProcess, Rs_JobCancel, Rs_SplitMachine, Rs_AppendArg,
    Rs_ShowPageByWord
};

// ---------------------------------------------------------------------------
// The trampolines
// ---------------------------------------------------------------------------

static struct RsExt *Ext_OfPage(HWND page)
{
    int i, id = Rs_PageIdOf(page);
    for (i = 0; i < g_extCount; i++)
        if (g_ext[i].id >= 0 && g_ext[i].id == id)
            return &g_ext[i];
    return NULL;
}

static int Ext_Filter(DWORD code)
{
    g_faultCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

static void Ext_Fault(struct RsExt *e, const wchar_t *where)
{
    if (g_faultCode == EXCEPTION_STACK_OVERFLOW)
        _resetstkoflw();
    e->off = 1;
    Rs_ModuleLog(L"module %ls: crashed in %ls (exception 0x%08lX) - turned off until Reload Studio starts again",
                 e->word, where, (unsigned long)g_faultCode);
    Rs_ModuleTurnedOff(e->id);
}

static void Tr_Create(HWND page)
{
    struct RsExt *e = Ext_OfPage(page);
    if (!e || e->off || !e->cb.create)
        return;
    __try {
        e->cb.create(page);
    } __except (Ext_Filter(GetExceptionCode())) {
        Ext_Fault(e, L"create");
    }
}

static void Tr_Layout(HWND page, int w, int h)
{
    struct RsExt *e = Ext_OfPage(page);
    if (!e || e->off || !e->cb.layout)
        return;
    __try {
        e->cb.layout(page, w, h);
    } __except (Ext_Filter(GetExceptionCode())) {
        Ext_Fault(e, L"layout");
    }
}

static LRESULT Tr_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    struct RsExt *e = Ext_OfPage(page);
    LRESULT r = 0;
    if (!e || e->off || !e->cb.command)
        return 0;
    __try {
        r = e->cb.command(page, wParam, lParam);
    } __except (Ext_Filter(GetExceptionCode())) {
        Ext_Fault(e, L"command");
        r = 0;
    }
    return r;
}

static LRESULT Tr_Notify(HWND page, NMHDR *hdr)
{
    struct RsExt *e = Ext_OfPage(page);
    LRESULT r = 0;
    if (!e || e->off || !e->cb.notify)
        return 0;
    __try {
        r = e->cb.notify(page, hdr);
    } __except (Ext_Filter(GetExceptionCode())) {
        Ext_Fault(e, L"notify");
        r = 0;
    }
    return r;
}

static LRESULT Tr_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    struct RsExt *e = Ext_OfPage(page);
    LRESULT r = 0;
    *handled = 0;
    if (!e || e->off || !e->cb.message)
        return 0;
    __try {
        r = e->cb.message(page, msg, wParam, lParam, handled);
    } __except (Ext_Filter(GetExceptionCode())) {
        Ext_Fault(e, L"message");
        *handled = 0;
        r = 0;
    }
    return r;
}

static int Tr_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    struct RsExt *e = Ext_OfPage(page);
    int r = RS_AUTO_UNKNOWN;
    if (!e || e->off)
        return RS_AUTO_FAIL;
    if (!e->cb.automate)
        return RS_AUTO_UNKNOWN;
    __try {
        r = e->cb.automate(page, verb, arg);
    } __except (Ext_Filter(GetExceptionCode())) {
        Ext_Fault(e, L"automate");
        r = RS_AUTO_FAIL;
    }
    return r;
}

static int Tr_Busy(HWND page)
{
    struct RsExt *e = Ext_OfPage(page);
    int r = 0;
    if (!e || e->off || !e->cb.busy)
        return 0;
    __try {
        r = e->cb.busy(page);
    } __except (Ext_Filter(GetExceptionCode())) {
        Ext_Fault(e, L"busy");
        r = 0;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

static int Word_Valid(const wchar_t *w)
{
    size_t i, n = wcslen(w);
    if (n < 1 || n > RS_MODULE_WORD_MAX)
        return 0;
    for (i = 0; i < n; i++)
        if (!((w[i] >= L'a' && w[i] <= L'z') || (w[i] >= L'0' && w[i] <= L'9') || w[i] == L'-'))
            return 0;
    return 1;
}

// Copies a string of the module (which may be a bad pointer: the caller is in
// __try) into out; 0 if it is NULL or longer than cap - 1.
static int Copy_Text(wchar_t *out, int cap, const wchar_t *s)
{
    int n = 0;
    out[0] = 0;
    if (!s)
        return 0;
    while (s[n] && n < cap - 1) {
        out[n] = s[n];
        n++;
    }
    out[n] = 0;
    return s[n] == 0;
}

// Who has this word already: a built-in page or an earlier DLL; NULL = free.
static const wchar_t *Word_Owner(const wchar_t *word, int before)
{
    int i;
    for (i = 0; i < RS_PAGE_BUILTIN_COUNT; i++)
        if (wcscmp(word, g_builtin[i].word) == 0)
            return g_builtin[i].page->navName;
    for (i = 0; i < before; i++)
        if (g_ext[i].alive && wcscmp(word, g_ext[i].word) == 0)
            return g_ext[i].file;
    return NULL;
}

// The text of a Windows error code, on one line.
static void Error_Text(DWORD err, wchar_t *out, int cap)
{
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0,
                             out, (DWORD)cap, NULL);
    out[n < (DWORD)cap ? n : (DWORD)cap - 1] = 0;
    while (n > 0 && (out[n - 1] == L'\r' || out[n - 1] == L'\n' || out[n - 1] == L' ' || out[n - 1] == L'.'))
        out[--n] = 0;
}

static int File_Cmp(const void *a, const void *b)
{
    return _wcsicmp((const wchar_t *)a, (const wchar_t *)b);
}

// LoadLibraryExW, an exception in DllMain caught. 0 = loaded.
static int Ext_LoadLib(const wchar_t *path, HMODULE *lib, DWORD *err)
{
    *lib = NULL;
    *err = 0;
    __try {
        *lib = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!*lib)
            *err = GetLastError();
    } __except (Ext_Filter(GetExceptionCode())) {
        return -1;
    }
    return *lib ? 0 : 1;
}

// The module of the DLL, copied while an exception is caught: the struct, the
// word, the needs and the page. Returns a reason (a format with no argument,
// or one %ls/%u filled in by the caller) or NULL when the copy is complete;
// *crashed = 1 when the module faulted.
static const wchar_t *Ext_Take(struct RsExt *e, RsModuleEntry entry, unsigned *abiOut, unsigned *coreOut,
                               int *crashed)
{
    const struct RsModuleDef *m = NULL;
    const wchar_t *why = NULL;
    *crashed = 0;
    __try {
        m = entry(&g_core);
        if (!m) {
            why = L"ReloadStudioModule gave no module";
        } else if (m->abi != RS_MODULE_ABI) {
            *abiOut = m->abi;
            why = L"abi";
        } else if (m->size < sizeof(struct RsModuleDef)) {
            *abiOut = m->size;
            why = L"size";
        } else if (m->minCore > RS_CORE_VERSION) {
            *coreOut = m->minCore;
            why = L"core";
        } else if (!Copy_Text(e->word, RS_MODULE_WORD_MAX + 1, m->word) || !Word_Valid(e->word)) {
            if (!m->word)
                e->word[0] = 0;
            why = L"word";
        } else if (!m->page || !m->page->navName || !m->page->navName[0] || !m->page->title ||
                   !m->page->subtitle) {
            why = L"it has no page or no name";
        } else {
            int i;
            e->cb = *m->page;
            e->needCount = 0;
            for (i = 0; m->needs && m->needs[i]; i++) {
                if (i >= RS_MODULE_NEEDS_MAX ||
                    !Copy_Text(e->needs[i], RS_MODULE_WORD_MAX + 1, m->needs[i])) {
                    why = L"it needs more modules than there can be, or a module word that is not valid";
                    break;
                }
                e->needCount++;
            }
            if (!why)
                e->def = *m;
        }
    } __except (Ext_Filter(GetExceptionCode())) {
        *crashed = 1;
        return L"crashed";
    }
    return why;
}

// One DLL: loaded and its module taken (e->alive = 1), or one line why not.
static void Ext_Load(struct RsExt *e, const wchar_t *dir, int before)
{
    wchar_t path[MAX_PATH * 2];
    wchar_t text[512];
    const wchar_t *why, *owner;
    unsigned abi = 0, core = 0;
    int crashed = 0, r;
    DWORD err = 0;
    RsModuleEntry entry;

    Rs_PathJoin(path, MAX_PATH * 2, dir, e->file);
    r = Ext_LoadLib(path, &e->lib, &err);
    if (r < 0) {
        Rs_ModuleLog(L"module %ls: not loaded - crashed while loading (exception 0x%08lX)", e->file,
                     (unsigned long)g_faultCode);
        return;
    }
    if (r > 0) {
        Error_Text(err, text, 512);
        if (err == ERROR_BAD_EXE_FORMAT)
            Rs_ModuleLog(L"module %ls: not loaded - it is not a 32-bit Windows DLL (error %lu: %ls)", e->file,
                         (unsigned long)err, text);
        else
            Rs_ModuleLog(L"module %ls: not loaded - Windows could not load it (error %lu: %ls)", e->file,
                         (unsigned long)err, text);
        return;
    }
    entry = (RsModuleEntry)(void *)GetProcAddress(e->lib, RS_MODULE_ENTRY_NAME);
    if (!entry) {
        Rs_ModuleLog(L"module %ls: not loaded - it has no function ReloadStudioModule", e->file);
        FreeLibrary(e->lib);
        e->lib = NULL;
        return;
    }
    why = Ext_Take(e, entry, &abi, &core, &crashed);
    if (crashed) {
        // Left loaded: unloading it would run its code once more.
        Rs_ModuleLog(L"module %ls: not loaded - crashed while loading (exception 0x%08lX)", e->file,
                     (unsigned long)g_faultCode);
        return;
    }
    if (!why && (owner = Word_Owner(e->word, before)) != NULL) {
        Rs_ModuleLog(L"module %ls: not loaded - its page word '%ls' is taken by %ls", e->file, e->word, owner);
    } else if (why && wcscmp(why, L"abi") == 0) {
        Rs_ModuleLog(L"module %ls: not loaded - made for module interface %u, this Reload Studio has %u",
                     e->file, abi, (unsigned)RS_MODULE_ABI);
    } else if (why && wcscmp(why, L"size") == 0) {
        Rs_ModuleLog(L"module %ls: not loaded - its module data has %u bytes, module interface %u needs %u",
                     e->file, abi, (unsigned)RS_MODULE_ABI, (unsigned)sizeof(struct RsModuleDef));
    } else if (why && wcscmp(why, L"core") == 0) {
        Rs_ModuleLog(L"module %ls: not loaded - needs Reload Studio %u.%u.%u or newer, this is %ls", e->file,
                     core >> 16, (core >> 8) & 255u, core & 255u, RS_VERSION_W);
    } else if (why && wcscmp(why, L"word") == 0) {
        Rs_ModuleLog(L"module %ls: not loaded - its page word '%ls' is not valid (a-z, 0-9 and '-', at most %d)",
                     e->file, e->word, RS_MODULE_WORD_MAX);
    } else if (why) {
        Rs_ModuleLog(L"module %ls: not loaded - %ls", e->file, why);
    } else {
        e->alive = 1;
        return;
    }
    FreeLibrary(e->lib);
    e->lib = NULL;
}

// 1 if a module with this word is there: built in, or a DLL still alive.
static int Word_There(const wchar_t *word)
{
    int i;
    for (i = 0; i < RS_PAGE_BUILTIN_COUNT; i++)
        if (wcscmp(word, g_builtin[i].word) == 0)
            return 1;
    for (i = 0; i < g_extCount; i++)
        if (g_ext[i].alive && wcscmp(word, g_ext[i].word) == 0)
            return 1;
    return 0;
}

static void Rs_ModulesLoad(const wchar_t *dir)
{
    static wchar_t names[RS_EXT_MAX][MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t pattern[MAX_PATH * 2];
    wchar_t tempDir[MAX_PATH];
    int count = 0, skipped = 0, i, j, changed, loaded = 0, failed = 0, alive = 0;
    int room = RS_MAX_MODULES - RS_PAGE_BUILTIN_COUNT;

    Rs_TempDir(tempDir, MAX_PATH);
    Rs_PathJoin(g_logPath, MAX_PATH, tempDir, L"modules.log");
    DeleteFileW(g_logPath);
    Rs_PathJoin(pattern, MAX_PATH * 2, dir, L"*.dll");
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            size_t n = wcslen(fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || n < 5 ||
                _wcsicmp(fd.cFileName + n - 4, L".dll") != 0)
                continue;
            if (count < RS_EXT_MAX) {
                wcsncpy(names[count], fd.cFileName, MAX_PATH - 1);
                names[count][MAX_PATH - 1] = 0;
                count++;
            } else {
                skipped++;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    qsort(names, (size_t)count, sizeof(names[0]), File_Cmp);
    Rs_ModuleLog(L"modules: folder %ls - %d file(s)", dir, count + skipped);

    for (i = 0; i < count; i++) {
        struct RsExt *e;
        if (alive >= room) {
            Rs_ModuleLog(L"module %ls: not loaded - too many modules (at most %d besides the built-in ones)",
                         names[i], room);
            failed++;
            continue;
        }
        e = &g_ext[g_extCount];
        memset(e, 0, sizeof(*e));
        e->id = -1;
        wcsncpy(e->file, names[i], MAX_PATH - 1);
        Ext_Load(e, dir, g_extCount);
        if (e->alive)
            alive++;
        else
            failed++;
        g_extCount++;
    }
    for (i = 0; i < skipped; i++)
        failed++;

    // What a module needs must be there; a module left out takes those that
    // need it along (as many rounds as it takes).
    do {
        changed = 0;
        for (i = 0; i < g_extCount; i++) {
            struct RsExt *e = &g_ext[i];
            if (!e->alive)
                continue;
            for (j = 0; j < e->needCount; j++) {
                if (!Word_There(e->needs[j])) {
                    Rs_ModuleLog(L"module %ls: not loaded - needs the module '%ls', which is not there", e->file,
                                 e->needs[j]);
                    e->alive = 0;
                    FreeLibrary(e->lib);
                    e->lib = NULL;
                    failed++;
                    changed = 1;
                    break;
                }
            }
        }
    } while (changed);

    for (i = 0; i < g_extCount; i++) {
        struct RsExt *e = &g_ext[i];
        if (!e->alive)
            continue;
        e->page = e->cb;
        e->page.create = Tr_Create;
        e->page.layout = Tr_Layout;
        e->page.command = Tr_Command;
        e->page.notify = Tr_Notify;
        e->page.message = Tr_Message;
        e->page.automate = Tr_Automate;
        e->page.busy = Tr_Busy;
        e->def.word = e->word;
        e->def.needs = NULL;
        e->def.page = &e->page;
        e->id = g_moduleCount;
        g_modules[g_moduleCount++] = &e->def;
        loaded++;
        Rs_ModuleLog(L"module %ls: loaded \"%ls\" (page %ls)", e->file, e->cb.navName, e->word);
    }
    Rs_ModuleLog(L"modules: %d loaded, %d not loaded (details in %ls)", loaded, failed, g_logPath);
}

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

void Rs_ModulesInit(const wchar_t *folder, int loadDlls)
{
    wchar_t dir[MAX_PATH];
    int i;
    if (g_moduleCount)
        return;
    for (i = 0; i < RS_PAGE_BUILTIN_COUNT; i++)
        g_modules[g_moduleCount++] = &g_builtin[i];
    if (!loadDlls) {
        Rs_AutoLog(L"modules: external modules not loaded (--no-modules)");
        return;
    }
    if (folder && folder[0]) {
        // A full path with backslashes: LoadLibraryExW with
        // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR takes nothing else.
        DWORD n = GetFullPathNameW(folder, MAX_PATH, dir, NULL);
        if (n == 0 || n >= MAX_PATH) {
            wcsncpy(dir, folder, MAX_PATH - 1);
            dir[MAX_PATH - 1] = 0;
        }
    } else {
        Rs_PathJoin(dir, MAX_PATH, Rs_ExeDir(), L"modules");
    }
    if (!Rs_DirExists(dir)) {
        Rs_AutoLog(L"modules: no folder %ls - built-in modules only", dir);
        return;
    }
    Rs_ModulesLoad(dir);
}

int Rs_ModuleCount(void)
{
    return g_moduleCount;
}

int Rs_ModuleBuiltinCount(void)
{
    return RS_PAGE_BUILTIN_COUNT;
}

const struct RsModuleDef *Rs_ModuleAt(int id)
{
    return (id >= 0 && id < g_moduleCount) ? g_modules[id] : NULL;
}

int Rs_ModuleByWord(const wchar_t *word)
{
    int i;
    if (!word)
        return -1;
    for (i = 0; i < g_moduleCount; i++)
        if (wcscmp(word, g_modules[i]->word) == 0)
            return i;
    return -1;
}

int Rs_ModuleHost(int id)
{
    return id >= 0 && id < RS_PAGE_BUILTIN_COUNT && id < g_moduleCount ? g_builtinPlace[id].host : -1;
}

const wchar_t *Rs_ModuleWhere(int id)
{
    return id >= 0 && id < RS_PAGE_BUILTIN_COUNT && id < g_moduleCount ? g_builtinPlace[id].where : NULL;
}

int Rs_ModuleBottom(int id)
{
    return id >= 0 && id < RS_PAGE_BUILTIN_COUNT && id < g_moduleCount ? g_builtinPlace[id].bottom : 0;
}

const wchar_t *Rs_ModuleNavTip(int id)
{
    return id >= 0 && id < RS_PAGE_BUILTIN_COUNT && id < g_moduleCount ? g_builtinPlace[id].tip : NULL;
}

int Rs_ModuleOn(int id)
{
    int i;
    if (id < 0 || id >= g_moduleCount)
        return 0;
    for (i = 0; i < g_extCount; i++)
        if (g_ext[i].id == id)
            return !g_ext[i].off;
    return 1;
}
