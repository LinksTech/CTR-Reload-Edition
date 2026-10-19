// rs_kit.c - the Creator Kit: a Blender template made from the user's own game
//
// ITS PLACE: a module of its own (word "kit", rs_module.c) without an entry in
// the sidebar - it is embedded in the page Character (Rs_ModuleHost): Build a
// character (step 0) has "Creator Kit" at its top right, and the step Model of
// every route, while no model is chosen, "No model yet? Start with the
// Creator Kit." (rs_char.c). Its page is then shown in place of Character's,
// whose entry stays marked; Back on its step 0 (and the verb "back" there)
// returns to Build a character (Rs_ShowHost, RS_WM_HOST_RETURN). "page kit" of
// old scripts opens it there and the log says where it is now (rs_shell.c).
//
// The kit is made ON THIS PC from the game files of the person using it, into
// a folder they choose: the original kart and its wheels (extracted from the
// game data), a neutral jointed puppet with its skeleton and its clips, and
// a template for each of the three ways of the page Character - Vanilla,
// Low-Poly, Hi-Res - with its export already set up, and on request a demo
// driver to try in the game. Nothing of it comes with Reload Studio
// (tools/content_guard.py refuses every one of these names in the repository).
//
// THE WAYS (K9): step 2 chooses "All three ways" (the default) or one of
// Vanilla, Low-Poly, Hi-Res; only the chosen ways are written. The page
// Character opens the kit from the step Model of a route with that route's way
// chosen (Rs_KitChooseWay). The demo driver is built from the Hi-Res template,
// so its option is there only while the ways chosen include Hi-Res.
//
// THE LAYOUT of a kit folder <out> (K4; KIT-OUTPUT-NAMES below):
//   <out>\NOTICE.txt, CREATOR_KIT.html (the guide), scripts\, the extracted
//         kit files (kit-kart.glb, kit-wheel.glb, kit-*-gltf.glb, kit-info.json,
//         kit-extract.txt), classic.glb (Start from an old character)
//   <out>\<way>\  creator-kit-<way>.blend, creator-kit-<way>-figure.glb (the
//         figure alone, without a kart: the game puts the kart of the
//         player's own game under it), NOTICE.txt, creator-kit-report.json;
//         hires\ also creator-kit-demo-local.glb (the figure on the kit's kart,
//         for a test on this PC) and, with the demo, demo-puppet.rldchar and
//         START-DEMO-PUPPE.bat
//   <way> is vanilla, lowpoly or hires. A way not chosen is left as it was.
//
// THE NOTICE (K1): templates/CREATOR_KIT_NOTICE.txt, built into this exe
// (RS_RES_KIT_NOTICE), is shown as plain text on the step Generate above the
// button "Generate demo files" - no icon, no warning colour - and written
// beside the scripts as scripts\CREATOR_KIT_NOTICE.txt, where the generator
// reads it; the generator writes it as NOTICE.txt into the kit folder and into
// every way folder.
//
// THE GUIDED FLOW, as on the page Character: five steps, each with one main
// action (the filled button), only what that step needs is shown.
//   0 What you get        what the kit holds; Next (the guide is open here too)
//   1 What the kit needs  the game data (found or not, with "How to fix") and
//                         Blender 5.2 (found, its version, or not); Next
//   2 Where the kit goes  the output folder (Documents\CTR Reload Creator Kit
//                         to start with), the ways, "Also build a demo
//                         driver" (with Hi-Res); Next
//   3 Generate            the notice; Generate demo files: the three phases
//                         with progress and Cancel, then the findings with
//                         "How to fix"
//   4 Your kit            the files of each way with "Open in Blender"; the
//                         guide, Show in folder, Test the demo (in game), Go to
//                         Character; the main action opens the template when
//                         one way was made, else shows the folder
//
// THE CHAIN (Generate): every step a job of the core, in <out>\kit.part:
//   1 the game data       rldpack kit-extract --game <base> -o <work> --machine
//                         (tools/rldpack_kit.inc: the kart and the wheel of the
//                         player's own game data, kit-info.json, kit-extract.txt)
//   2 the templates       one Blender start for every way chosen: --threads 1
//                         --background --factory-startup --python-exit-code 1
//                         --python <scripts>\make_creator_kit.py -- --kit <work>
//                         --routes <vanilla,lowpoly,hires | one of them>
//                         (templates/, built into this exe and written to
//                         <work>\scripts; the setting kit.scripts names a
//                         folder to take them from instead). --threads 1
//                         first: so the .blend has the same bytes every run.
//                         It writes <work>\<way>\ for every way chosen (THE
//                         LAYOUT). The colour mode of each way is the
//                         generator's (no --vcol).
//                         START FROM AN OLD CHARACTER (step 2, optional, the
//                         setting kit.classic): before Blender rldpack
//                         char-export <old.rldchar> -o <work>\classic.glb (its
//                         model, --source auto), and the generator gets
//                         --classic <work>\classic.glb (a hidden collection
//                         Classic in the template, templates/creator_kit_classic.py)
//   3 the demo driver     (on request, Hi-Res chosen) the build of the page
//                         Character, route Hi-Res, from
//                         hires\creator-kit-demo-local.glb: [hires-upscale]
//                         make-char -> hires-add, with the same switches and
//                         suggestions (the kit's kart is its kart part:
//                         hires-add --kart-part, the podium and the menu leave
//                         it out) into hires\; START-DEMO-PUPPE.bat with the
//                         command of "Test in game" (Rs_TestCharacterCommand)
// Only when every step is done do the files replace those of an earlier kit
// in <out>; a failed or cancelled run deletes kit.part and leaves an earlier
// kit as it was.
//
// RULES FOR THE OUTPUT FOLDER (the kit holds the user's game data): never in a
// git work tree (a .git in the folder or above it), never in the game's folder
// (the game would list the demo driver), never in the folder of Reload Studio.
//
// THE GUIDE (docs/CREATOR_KIT.html, built into this exe) is written next to the
// kit, or into Rs_TempDir before there is one, and opened in the browser.
//
// AUTOMATION (verbs of the page "kit"): step <0-4>, next, back (on step 0: back
// to Character, Build a character), game <folder>,
// blender <exe>, out <folder>, ways all|vanilla|lowpoly|hires, demo on|off,
// generate (waits), cancel, open guide|folder|template [<way>] (logs, opens
// nothing), test-demo (logs the command, starts nothing), report <file>. The
// report: one tab line per fact - step, game, game-text, blender,
// blender-text, out, out-text, ways, demo (on|off, and offered|not offered),
// notice (shown|hidden and its text, each line end written as \n), phase
// (x3), result, msg (each finding), file (each file of the kit; those of
// scripts\ and of the way folders with their folder before a backslash).

#include "reloadstudio.h"
#include "rs_charroute.h"
#include "rs_hires_model.h"
#include <shellapi.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define KIT_PATH 1024
#define KIT_TEXT 512
#define KIT_MSGS 32
#define KIT_RAW_MAX (512 * 1024)
#define KIT_STEPS 5
#define KIT_NAME L"DEMO PUPPET"
#define KIT_PAGE_MIN_W 760     // below: the view scrolls
#define KIT_PAGE_MIN_H 400
#define KIT_FULL_W 964         // below either: the compact heading
#define KIT_FULL_H 640

// The files the kit writes into its folder. tools/content_guard.py refuses
// every one of these names in the repository; the ctest reloadstudio_kit
// checks that (KIT-OUTPUT-NAMES, keep the markers).
// NOTICE.txt (the notice, no game data) is not among them.
// KIT-OUTPUT-NAMES BEGIN
static const wchar_t *const g_kitFiles[] = {
    L"kit-kart.glb", L"kit-wheel.glb", L"kit-kart-gltf.glb", L"kit-wheel-gltf.glb", L"kit-info.json",
    L"kit-extract.txt", L"creator-kit-vanilla.blend", L"creator-kit-vanilla-figure.glb",
    L"creator-kit-lowpoly.blend", L"creator-kit-lowpoly-figure.glb", L"creator-kit-hires.blend",
    L"creator-kit-hires-figure.glb", L"creator-kit-demo-local.glb", L"creator-kit-report.json",
    L"demo-puppet.rldchar", L"START-DEMO-PUPPE.bat", L"classic.glb",
};
// KIT-OUTPUT-NAMES END
#define KIT_WORK      L"kit.part"
#define KIT_BLEND_OLD L"creator-kit.blend"      // the one template of a kit before the ways
#define KIT_DEMO_GLB  L"creator-kit-demo-local.glb"
#define KIT_DEMO      L"demo-puppet.rldchar"
#define KIT_BAT       L"START-DEMO-PUPPE.bat"
#define KIT_GUIDE     L"CREATOR_KIT.html"
#define KIT_CLASSIC   L"classic.glb"
#define KIT_NOTICE    L"NOTICE.txt"

// THE WAYS: the folder word of each route (RS_ROUTE_*), the order the
// generator writes them in; their names as the page Character says them.
static const wchar_t *const g_wayWords[RS_ROUTES] = { L"vanilla", L"lowpoly", L"hires" };
static const wchar_t *const g_wayNames[RS_ROUTES] = { L"Vanilla", L"Low-Poly", L"Hi-Res" };
// The ways chosen: RS_KIT_WAYS_ALL or one route. Outside g_kit: the page
// Character may choose before the page is made (Rs_KitChooseWay).
static int g_kitWays = RS_KIT_WAYS_ALL;

// The scripts and the guide built into this exe (reloadstudio_kit.rc, made by
// CMakeLists.txt from templates/ and docs/).
static const struct { int id; const wchar_t *name; } g_kitScripts[] = {
    { RS_RES_KIT_MAKE, L"make_creator_kit.py" },
    { RS_RES_KIT_PUPPET, L"creator_kit_puppet.py" },
    { RS_RES_KIT_CLIPS, L"creator_kit_clips.py" },
    { RS_RES_KIT_STANDIN, L"creator_kit_standin.py" },
    { RS_RES_KIT_CLASSIC, L"creator_kit_classic.py" },
    { RS_RES_KIT_NOTICE, L"CREATOR_KIT_NOTICE.txt" },   // the generator reads it beside its script
};
#define KIT_SCRIPT_COUNT ((int)(sizeof(g_kitScripts) / sizeof(g_kitScripts[0])))

enum {
    KIT_ID_STEP = 100,          // .. +4
    KIT_ID_INTRO = 110,
    KIT_ID_GAME_HEAD, KIT_ID_GAME_STATE, KIT_ID_GAME_FIX, KIT_ID_GAME_BTN,
    KIT_ID_BL_HEAD, KIT_ID_BL_STATE, KIT_ID_BL_FIX, KIT_ID_BL_BTN,
    KIT_ID_OUT_HEAD, KIT_ID_OUT_EDIT, KIT_ID_OUT_BTN, KIT_ID_OUT_STATE, KIT_ID_OUT_FIX, KIT_ID_DEMO,
    KIT_ID_CLASSIC_HEAD, KIT_ID_CLASSIC_EDIT, KIT_ID_CLASSIC_BTN, KIT_ID_CLASSIC_NOTE,
    KIT_ID_PHASE,               // .. +2
    KIT_ID_PROGRESS = 140, KIT_ID_MSGS, KIT_ID_DETAILS_BOX, KIT_ID_DETAILS,
    KIT_ID_FILES, KIT_ID_NEXT_TEXT,
    KIT_ID_BACK = 160, KIT_ID_MAIN, KIT_ID_CANCEL, KIT_ID_GUIDE, KIT_ID_FOLDER, KIT_ID_TEST_DEMO, KIT_ID_TO_CHAR,
    KIT_ID_WAYS_HEAD = 170,
    KIT_ID_WAYS,                // the choice: All three ways, Vanilla, Low-Poly, Hi-Res
    KIT_ID_NOTICE = 175,        // the notice on the step Generate (K1)
    KIT_ID_WAY_FILES,           // .. +2: the files of a way on step 4
    KIT_ID_WAY_OPEN = 179       // .. +2: its "Open in Blender"
};

// What a job of the page is.
enum { KIT_JOB_NONE = 0, KIT_JOB_VERSION, KIT_JOB_EXTRACT, KIT_JOB_EXPORT, KIT_JOB_BLENDER, KIT_JOB_UPSCALE, KIT_JOB_CLASSIC,
       KIT_JOB_PACK };
enum { KIT_PH_EXTRACT = 0, KIT_PH_BLENDER, KIT_PH_DEMO, KIT_PHASES };
enum { KIT_ST_TODO = 0, KIT_ST_RUNNING, KIT_ST_OK, KIT_ST_FAILED, KIT_ST_CANCELLED, KIT_ST_SKIPPED };
enum { KIT_GAME_UNKNOWN = 0, KIT_GAME_OK, KIT_GAME_NO_EXE, KIT_GAME_NO_DATA };
enum { KIT_BL_UNKNOWN = 0, KIT_BL_CHECKING, KIT_BL_OK, KIT_BL_MISSING, KIT_BL_VERSION, KIT_BL_FAILED };
enum { KIT_OUT_OK = 0, KIT_OUT_EMPTY, KIT_OUT_GIT, KIT_OUT_GAME, KIT_OUT_STUDIO, KIT_OUT_FILE };
enum { KIT_RES_NONE = 0, KIT_RES_OK, KIT_RES_FAILED, KIT_RES_CANCELLED };

static const wchar_t *const g_stepTitles[KIT_STEPS] = {
    L"What you get", L"What the kit needs", L"Where the kit goes", L"Generate", L"Your kit"
};
static const wchar_t *const g_phaseTitles[KIT_PHASES] = {
    L"Reading your game data", L"Building the template in Blender", L"Building the demo driver"
};
static const wchar_t *const g_phaseWords[KIT_PHASES] = { L"extract", L"blender", L"demo" };
static const wchar_t *const g_stateWords[] = { L"todo", L"running", L"ok", L"failed", L"cancelled", L"skipped" };
static const wchar_t *const g_sevWords[RS_SEV_COUNT] = { L"ok", L"info", L"note", L"warning", L"error" };

struct KitMsg {
    int severity;
    wchar_t text[KIT_TEXT];
    wchar_t fix[KIT_TEXT];
};

static struct {
    HWND page;
    int step;
    HWND stepLbl[KIT_STEPS];
    HWND intro;
    HWND gameHead, gameState, gameFix, gameBtn;
    HWND blHead, blState, blFix, blBtn;
    HWND outHead, outEdit, outBtn, outState, outFix, demoBox;
    HWND classicHead, classicEdit, classicBtn, classicNote;
    HWND waysHead, ways;                     // step 2: the choice All three ways, Vanilla, Low-Poly, Hi-Res
    HWND notice;                             // step 3: the notice (K1), plain text
    HWND phaseLbl[KIT_PHASES], progress, msgs, detailsBox, details;
    HWND files, nextText;                    // step 4: the files of the kit folder itself, what next
    HWND wayFiles[RS_ROUTES], wayOpen[RS_ROUTES];  // step 4: the files of each way made, "Open in Blender"
    HWND back, main, cancel, guide, folder, testDemo, toChar;
    int quiet;                       // the page itself sets a field: no check

    // Game data.
    int game;
    wchar_t gameBase[KIT_PATH];
    wchar_t gameSource[32];
    wchar_t gameText[KIT_TEXT], gameFixText[KIT_TEXT];
    // Blender.
    int blender;
    wchar_t blenderPath[KIT_PATH];
    wchar_t blenderVersion[128];
    wchar_t blText[KIT_TEXT], blFixText[KIT_TEXT];
    int versionJob;
    int generateAfterCheck;          // "generate" waits for the version check
    // The output folder.
    int out;
    wchar_t outPath[KIT_PATH];
    wchar_t outText[KIT_TEXT], outFixText[KIT_TEXT];
    int demo;
    int runWays;                     // the ways of this run / of the kit made last (RS_KIT_WAYS_ALL or a route)
    wchar_t classic[KIT_PATH];       // "Start from an old character": the .rldchar, "" = none
    int classicUsed;                 // this run exported it (classic.glb in the kit)

    // The run.
    int job, jobKind;
    int phase[KIT_PHASES];
    int result;
    int cancelled;
    wchar_t work[KIT_PATH];
    wchar_t scripts[KIT_PATH];
    wchar_t demoClassic[KIT_PATH];   // make-char's classic fallback (in work)
    wchar_t demoModel[KIT_PATH];     // the model hires-add takes (the GLB, or its upscaled copy)
    wchar_t demoDerived[KIT_PATH];   // the upscaled copy, in Rs_TempDir; "" = none
    int demoWheelsOff, demoVcol, demoUpscale;
    unsigned demoParts;              // the parts field of the pack (THE KART: the kit's kart part, --kart-part)
    wchar_t lastCreator[KIT_TEXT];   // the last line "creator kit: ..." of Blender
    wchar_t resultState[16];         // @result of rldpack
    struct KitMsg msg[KIT_MSGS];
    int msgCount;
    wchar_t *raw;                    // everything the jobs printed (Show details)
    size_t rawLen;
    wchar_t progressText[KIT_TEXT];
} g_kit;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void Kit_Copy(wchar_t *dst, int cap, const wchar_t *src)
{
    if (cap <= 0)
        return;
    wcsncpy(dst, src ? src : L"", (size_t)cap - 1);
    dst[cap - 1] = 0;
}

static void Kit_Show(HWND h, int on)
{
    if (h)
        ShowWindow(h, on ? SW_SHOW : SW_HIDE);
}

static void Kit_Enable(HWND h, int on)
{
    if (h)
        EnableWindow(h, on ? TRUE : FALSE);
}

static int Kit_Checked(HWND h)
{
    return h && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static int Kit_Busy(HWND page)
{
    (void)page;
    return g_kit.job != 0 || g_kit.versionJob != 0;
}

// 1 if path is dir itself or inside it (case-insensitive, whole folder names).
static int Kit_Inside(const wchar_t *path, const wchar_t *dir)
{
    size_t n = wcslen(dir);
    while (n > 0 && (dir[n - 1] == L'\\' || dir[n - 1] == L'/'))
        n--;
    if (n == 0 || _wcsnicmp(path, dir, n) != 0)
        return 0;
    return path[n] == 0 || path[n] == L'\\' || path[n] == L'/';
}

static void Kit_Full(const wchar_t *in, wchar_t *out, int cap)
{
    DWORD n = in && in[0] ? GetFullPathNameW(in, (DWORD)cap, out, NULL) : 0;
    if (n == 0 || n >= (DWORD)cap)
        Kit_Copy(out, cap, in);
    n = (DWORD)wcslen(out);
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
}

// Deletes a folder of the page's own making (kit.part) with everything in it.
static void Kit_RemoveTree(const wchar_t *dir)
{
    wchar_t pattern[KIT_PATH], child[KIT_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (!dir[0] || !Rs_DirExists(dir))
        return;
    Rs_PathJoin(pattern, KIT_PATH, dir, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            Rs_PathJoin(child, KIT_PATH, dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                Kit_RemoveTree(child);
            else
                DeleteFileW(child);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

// Writes a resource of this exe to a file. 1 = written.
static int Kit_WriteResource(int id, const wchar_t *path)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    const void *p = g ? LockResource(g) : NULL;
    DWORD n = r ? SizeofResource(NULL, r) : 0, put = 0;
    HANDLE f;
    int ok;
    if (!p || !n)
        return 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(f, p, n, &put, NULL) && put == n;
    CloseHandle(f);
    if (!ok)
        DeleteFileW(path);
    return ok;
}

// THE NOTICE: the text built into this exe (templates/CREATOR_KIT_NOTICE.txt,
// UTF-8), its line ends as the controls want them ("\r\n"), the line ends at
// its end left out. Never NULL (Rs_Free it); "" if the resource is missing.
static wchar_t *Kit_NoticeText(void)
{
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(RS_RES_KIT_NOTICE), MAKEINTRESOURCEW(10)); // RT_RCDATA
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    const char *p = g ? (const char *)LockResource(g) : NULL;
    DWORD n = r ? SizeofResource(NULL, r) : 0;
    wchar_t *raw, *out;
    size_t i, k = 0, len;
    if (!p || !n)
        return Rs_Dup(L"");
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
        n -= 3;
    }
    raw = Rs_FromUtf8(p, (int)n);
    len = wcslen(raw);
    out = Rs_Alloc((len * 2 + 1) * sizeof(wchar_t));
    for (i = 0; i < len; i++) {
        if (raw[i] == L'\r')
            continue;
        if (raw[i] == L'\n')
            out[k++] = L'\r';
        out[k++] = raw[i];
    }
    while (k > 0 && (out[k - 1] == L'\r' || out[k - 1] == L'\n' || out[k - 1] == L' '))
        k--;
    out[k] = 0;
    Rs_Free(raw);
    return out;
}

// THE WAYS.
static int Kit_WayOn(int ways, int route)
{
    return ways == RS_KIT_WAYS_ALL || ways == route;
}

// The demo driver is built from the Hi-Res template: offered while it is chosen.
static int Kit_DemoOffered(void)
{
    return Kit_WayOn(g_kitWays, RS_ROUTE_HIRES);
}

// <dir>\<way>\<name>; name NULL or "" = the way's folder.
static void Kit_WayPath(wchar_t *out, int cap, const wchar_t *dir, int route, const wchar_t *name)
{
    wchar_t sub[KIT_PATH];
    Rs_PathJoin(sub, KIT_PATH, dir, g_wayWords[route]);
    if (name && name[0])
        Rs_PathJoin(out, cap, sub, name);
    else
        Kit_Copy(out, cap, sub);
}

// creator-kit-<way>.blend, creator-kit-<way>-figure.glb.
static void Kit_WayBlend(wchar_t *out, int cap, int route)
{
    swprintf(out, cap, L"creator-kit-%ls.blend", g_wayWords[route]);
}

static void Kit_WayFigure(wchar_t *out, int cap, int route)
{
    swprintf(out, cap, L"creator-kit-%ls-figure.glb", g_wayWords[route]);
}

// The ways as the generator takes them (--routes) and as the report says them.
static void Kit_WaysList(int ways, wchar_t *out, int cap)
{
    int r;
    out[0] = 0;
    for (r = 0; r < RS_ROUTES; r++)
        if (Kit_WayOn(ways, r)) {
            if (out[0])
                wcsncat(out, L",", (size_t)cap - wcslen(out) - 1);
            wcsncat(out, g_wayWords[r], (size_t)cap - wcslen(out) - 1);
        }
}

static void Kit_MsgClear(void)
{
    g_kit.msgCount = 0;
    if (g_kit.msgs)
        Rs_MsgListClear(g_kit.msgs);
}

static void Kit_Msg(int severity, const wchar_t *text, const wchar_t *fix)
{
    struct KitMsg *m;
    if (g_kit.msgCount >= KIT_MSGS)
        return;
    m = &g_kit.msg[g_kit.msgCount++];
    m->severity = severity;
    Kit_Copy(m->text, KIT_TEXT, text);
    Kit_Copy(m->fix, KIT_TEXT, fix);
    if (g_kit.msgs)
        Rs_MsgListAdd(g_kit.msgs, severity, text, fix && fix[0] ? fix : NULL);
    if (Rs_Automating())
        Rs_AutoLog(L"  kit: %ls: %ls%ls%ls", g_sevWords[severity], text, fix && fix[0] ? L" | " : L"",
                   fix ? fix : L"");
}

// A line "<what>. How to fix: <fix>" split in two.
static void Kit_MsgSplit(int severity, const wchar_t *line)
{
    wchar_t text[KIT_TEXT];
    const wchar_t *at = wcsstr(line, L"How to fix:");
    Kit_Copy(text, KIT_TEXT, line);
    if (at) {
        size_t n = (size_t)(at - line);
        if (n < KIT_TEXT)
            text[n] = 0;
        n = wcslen(text);
        while (n > 0 && text[n - 1] == L' ')
            text[--n] = 0;
        at += 11;
        while (*at == L' ')
            at++;
        Kit_Msg(severity, text, at);
    } else {
        Kit_Msg(severity, text, L"Click Show details to see everything the step printed.");
    }
}

static void Kit_RawAdd(const wchar_t *line)
{
    size_t n = wcslen(line);
    if (!g_kit.raw)
        g_kit.raw = Rs_Alloc(KIT_RAW_MAX * sizeof(wchar_t));
    if (g_kit.rawLen + n + 3 >= KIT_RAW_MAX)
        return;
    memcpy(g_kit.raw + g_kit.rawLen, line, n * sizeof(wchar_t));
    g_kit.rawLen += n;
    g_kit.raw[g_kit.rawLen++] = L'\r';
    g_kit.raw[g_kit.rawLen++] = L'\n';
    g_kit.raw[g_kit.rawLen] = 0;
}

// ---------------------------------------------------------------------------
// What the kit needs: the game data, Blender, the output folder
// ---------------------------------------------------------------------------

static void Kit_CheckGame(void)
{
    wchar_t chosen[KIT_PATH], exe[KIT_PATH], dir[KIT_PATH];
    int unpacked = 0, found;

    g_kit.gameBase[0] = g_kit.gameSource[0] = g_kit.gameFixText[0] = 0;
    Rs_ConfigGet(L"kit.game", chosen, KIT_PATH);
    if (chosen[0]) {
        Kit_Full(chosen, dir, KIT_PATH);
        found = Rs_FindGameBase(dir, g_kit.gameBase, KIT_PATH, &unpacked);
    } else if (Rs_FindGameExe(exe, KIT_PATH)) {
        Rs_PathDir(dir, KIT_PATH, exe);
        found = Rs_FindGameBase(dir, g_kit.gameBase, KIT_PATH, &unpacked);
    } else {
        g_kit.game = KIT_GAME_NO_EXE;
        Kit_Copy(g_kit.gameText, KIT_TEXT, RS_TEXT_NO_GAME_EXE);
        Kit_Copy(g_kit.gameFixText, KIT_TEXT, L"Or choose the folder of your game with Choose game.");
        return;
    }
    if (!found) {
        g_kit.game = KIT_GAME_NO_DATA;
        Kit_Copy(g_kit.gameText, KIT_TEXT, RS_TEXT_NO_GAME_DATA);
        Kit_Copy(g_kit.gameFixText, KIT_TEXT, L"Or choose the folder of your game (the one with the folder assets) with Choose game.");
        return;
    }
    g_kit.game = KIT_GAME_OK;
    Kit_Copy(g_kit.gameSource, 32, unpacked ? L"BIGFILE.BIG" : L"ctr-u.bin");
    swprintf(g_kit.gameText, KIT_TEXT, L"Found: %ls\\assets (%ls).", g_kit.gameBase, g_kit.gameSource);
}

// The Blender to use: the setting, else the newest "Blender x.y" in Program
// Files\Blender Foundation (the 64-bit one: ProgramW6432, which a 32-bit
// program does not see as %ProgramFiles%). "" = none found.
static void Kit_FindBlender(wchar_t *out, int cap)
{
    static const wchar_t *const roots[] = { L"ProgramW6432", L"ProgramFiles" };
    wchar_t base[KIT_PATH], pattern[KIT_PATH], probe[KIT_PATH], best[KIT_PATH];
    int r, bestMajor = -1, bestMinor = -1;
    WIN32_FIND_DATAW fd;
    HANDLE h;

    out[0] = 0;
    Rs_ConfigGet(L"kit.blender", probe, KIT_PATH);
    if (probe[0]) {
        Kit_Full(probe, out, cap);
        return;
    }
    best[0] = 0;
    for (r = 0; r < 2; r++) {
        DWORD n = GetEnvironmentVariableW(roots[r], base, KIT_PATH);
        if (n == 0 || n >= KIT_PATH)
            continue;
        Rs_PathJoin(pattern, KIT_PATH, base, L"Blender Foundation\\Blender *");
        h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do {
            int major = 0, minor = 0;
            wchar_t dir[KIT_PATH];
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                swscanf(fd.cFileName + 8, L"%d.%d", &major, &minor) != 2)
                continue;
            Rs_PathJoin(dir, KIT_PATH, base, L"Blender Foundation");
            Rs_PathJoin(probe, KIT_PATH, dir, fd.cFileName);
            Rs_PathJoin(dir, KIT_PATH, probe, L"blender.exe");
            if (!Rs_FileExists(dir))
                continue;
            if (major > bestMajor || (major == bestMajor && minor > bestMinor)) {
                bestMajor = major;
                bestMinor = minor;
                Kit_Copy(best, KIT_PATH, dir);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (best[0])
            break;
    }
    Kit_Copy(out, cap, best);
}

static void Kit_Update(void);

// Starts the check of Blender (its --version); the answer comes as a job.
static void Kit_CheckBlender(HWND page)
{
    wchar_t dir[KIT_PATH];
    if (g_kit.versionJob)
        return;
    Kit_FindBlender(g_kit.blenderPath, KIT_PATH);
    g_kit.blenderVersion[0] = g_kit.blFixText[0] = 0;
    if (!g_kit.blenderPath[0] || !Rs_FileExists(g_kit.blenderPath)) {
        g_kit.blender = KIT_BL_MISSING;
        Kit_Copy(g_kit.blText, KIT_TEXT, L"Blender was not found.");
        Kit_Copy(g_kit.blFixText, KIT_TEXT, L"Install Blender 5.2 from blender.org, or choose blender.exe with Choose Blender.");
        return;
    }
    Rs_PathDir(dir, KIT_PATH, g_kit.blenderPath);
    g_kit.versionJob = Rs_RunTool(page, g_kit.blenderPath, L"--factory-startup --version", dir);
    if (!g_kit.versionJob) {
        g_kit.blender = KIT_BL_FAILED;
        Kit_Copy(g_kit.blText, KIT_TEXT, L"Blender could not be started.");
        Kit_Copy(g_kit.blFixText, KIT_TEXT, L"Choose blender.exe with Choose Blender, or install Blender 5.2 from blender.org.");
        return;
    }
    g_kit.blender = KIT_BL_CHECKING;
    Kit_Copy(g_kit.blText, KIT_TEXT, L"Checking Blender...");
}

// The version check ended: "Blender 5.2.2 LTS" is what the kit needs.
static void Kit_VersionDone(int exitCode)
{
    int major = 0, minor = 0, patch = 0;
    g_kit.versionJob = 0;
    if (exitCode != 0 || swscanf(g_kit.blenderVersion, L"Blender %d.%d.%d", &major, &minor, &patch) < 2) {
        g_kit.blender = KIT_BL_FAILED;
        Kit_Copy(g_kit.blText, KIT_TEXT, L"This program did not answer like Blender.");
        Kit_Copy(g_kit.blFixText, KIT_TEXT, L"Choose blender.exe with Choose Blender, or install Blender 5.2 from blender.org.");
        return;
    }
    if (major != 5 || minor != 2) {
        wchar_t v[64];
        swscanf(g_kit.blenderVersion, L"Blender %63ls", v);
        g_kit.blender = KIT_BL_VERSION;
        swprintf(g_kit.blText, KIT_TEXT, L"This is Blender %ls; the Creator Kit needs Blender 5.2.", v);
        Kit_Copy(g_kit.blFixText, KIT_TEXT, L"Install Blender 5.2 from blender.org.");
        return;
    }
    g_kit.blender = KIT_BL_OK;
    swprintf(g_kit.blText, KIT_TEXT, L"Found: %ls (%ls).", g_kit.blenderPath, g_kit.blenderVersion);
}

// 1 if dir is one of git's ceiling folders (GIT_CEILING_DIRECTORIES, ';'
// between them): git does not look for a repository above them, nor does this.
static int Kit_Ceiling(const wchar_t *dir)
{
    wchar_t list[KIT_PATH * 2], one[KIT_PATH], full[KIT_PATH];
    wchar_t *at, *next;
    DWORD n = GetEnvironmentVariableW(L"GIT_CEILING_DIRECTORIES", list, KIT_PATH * 2);
    if (n == 0 || n >= KIT_PATH * 2)
        return 0;
    for (at = list; at && *at; at = next) {
        next = wcschr(at, L';');
        if (next)
            *next++ = 0;
        Kit_Copy(one, KIT_PATH, at);
        Kit_Full(one, full, KIT_PATH);
        if (full[0] && _wcsicmp(full, dir) == 0)
            return 1;
    }
    return 0;
}

// 1 if path is in a git work tree: a .git (folder or file) in it or above it,
// as far up as git itself would look.
static int Kit_HasGit(const wchar_t *path)
{
    wchar_t dir[KIT_PATH], probe[KIT_PATH], up[KIT_PATH];
    int i;
    Kit_Copy(dir, KIT_PATH, path);
    for (i = 0; i < 64 && dir[0]; i++) {
        Rs_PathJoin(probe, KIT_PATH, dir, L".git");
        if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES)
            return 1;
        Rs_PathDir(up, KIT_PATH, dir);
        if (!up[0] || wcscmp(up, dir) == 0 || Kit_Ceiling(up))
            break;
        Kit_Copy(dir, KIT_PATH, up);
    }
    return 0;
}

// A kit is in the folder: its extracted files, a template of a way, or the
// one template of a kit made before the ways.
static int Kit_HasKit(const wchar_t *dir)
{
    wchar_t probe[KIT_PATH], name[64];
    int r;
    Rs_PathJoin(probe, KIT_PATH, dir, KIT_BLEND_OLD);
    if (Rs_FileExists(probe))
        return 1;
    for (r = 0; r < RS_ROUTES; r++) {
        Kit_WayBlend(name, 64, r);
        Kit_WayPath(probe, KIT_PATH, dir, r, name);
        if (Rs_FileExists(probe))
            return 1;
    }
    Rs_PathJoin(probe, KIT_PATH, dir, L"kit-info.json");
    return Rs_FileExists(probe);
}

static void Kit_CheckOut(void)
{
    wchar_t *raw = g_kit.outEdit ? Rs_GetText(g_kit.outEdit) : Rs_Dup(L"");
    Kit_Full(raw, g_kit.outPath, KIT_PATH);
    Rs_Free(raw);
    g_kit.outFixText[0] = 0;
    g_kit.outText[0] = 0;
    if (!g_kit.outPath[0]) {
        g_kit.out = KIT_OUT_EMPTY;
        Kit_Copy(g_kit.outText, KIT_TEXT, L"Choose a folder for the kit.");
        Kit_Copy(g_kit.outFixText, KIT_TEXT, L"Type a folder or choose one with Browse; it is made when it does not exist.");
    } else if (Rs_FileExists(g_kit.outPath) && !Rs_DirExists(g_kit.outPath)) {
        g_kit.out = KIT_OUT_FILE;
        Kit_Copy(g_kit.outText, KIT_TEXT, L"This is a file, not a folder.");
        Kit_Copy(g_kit.outFixText, KIT_TEXT, L"Choose a folder.");
    } else if (Kit_HasGit(g_kit.outPath)) {
        g_kit.out = KIT_OUT_GIT;
        Kit_Copy(g_kit.outText, KIT_TEXT,
                 L"This folder is inside a git repository. The kit holds files from your game, so it is made outside of repositories.");
        Kit_Copy(g_kit.outFixText, KIT_TEXT, L"Choose a folder outside it.");
    } else if (g_kit.game == KIT_GAME_OK && Kit_Inside(g_kit.outPath, g_kit.gameBase)) {
        g_kit.out = KIT_OUT_GAME;
        Kit_Copy(g_kit.outText, KIT_TEXT, L"This folder is inside the game's folder.");
        Kit_Copy(g_kit.outFixText, KIT_TEXT, L"Choose a folder outside the game, for example in Documents.");
    } else if (Kit_Inside(g_kit.outPath, Rs_ExeDir())) {
        g_kit.out = KIT_OUT_STUDIO;
        Kit_Copy(g_kit.outText, KIT_TEXT, L"This folder is inside the folder of Reload Studio.");
        Kit_Copy(g_kit.outFixText, KIT_TEXT, L"Choose a folder outside it, for example in Documents.");
    } else {
        g_kit.out = KIT_OUT_OK;
        if (Kit_HasKit(g_kit.outPath))
            Kit_Copy(g_kit.outText, KIT_TEXT, L"There is a kit in this folder already: its files are replaced.");
        else
            Kit_Copy(g_kit.outText, KIT_TEXT, L"The kit is made in this folder.");
    }
}

static int Kit_Ready(void)
{
    return g_kit.game == KIT_GAME_OK && g_kit.blender == KIT_BL_OK && g_kit.out == KIT_OUT_OK;
}

// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------

static void Kit_SetPhase(int ph, int state)
{
    g_kit.phase[ph] = state;
}

static void Kit_EndRun(HWND page, int result);
static int Kit_Generate(HWND page);

static int Kit_StartRldpack(HWND page, int kind, const wchar_t *const *args, int n)
{
    g_kit.resultState[0] = 0;
    g_kit.jobKind = kind;
    g_kit.job = Rs_RunRldpack(page, args, n);
    return g_kit.job != 0;
}

static int Kit_StartExtract(HWND page)
{
    const wchar_t *args[] = { L"kit-extract", L"--game", g_kit.gameBase, L"-o", g_kit.work, L"--machine" };
    Kit_SetPhase(KIT_PH_EXTRACT, KIT_ST_RUNNING);
    Kit_Copy(g_kit.progressText, KIT_TEXT, L"Reading your game data...");
    return Kit_StartRldpack(page, KIT_JOB_EXTRACT, args, 6);
}

static int Kit_StartBlender(HWND page)
{
    wchar_t script[KIT_PATH], cmd[KIT_PATH * 3], ways[64];
    Kit_SetPhase(KIT_PH_BLENDER, KIT_ST_RUNNING);
    Kit_Copy(g_kit.progressText, KIT_TEXT, g_kit.runWays == RS_KIT_WAYS_ALL ? L"Building the templates in Blender..."
                                                                             : L"Building the template in Blender...");
    Rs_PathJoin(script, KIT_PATH, g_kit.scripts, L"make_creator_kit.py");
    cmd[0] = 0;
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--threads");
    Rs_AppendArg(cmd, KIT_PATH * 3, L"1");
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--background");
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--factory-startup");
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--python-exit-code");
    Rs_AppendArg(cmd, KIT_PATH * 3, L"1");
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--python");
    Rs_AppendArg(cmd, KIT_PATH * 3, script);
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--");
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--kit");
    Rs_AppendArg(cmd, KIT_PATH * 3, g_kit.work);
    // The ways chosen, always named (K5); the colour mode of each is the generator's.
    Kit_WaysList(g_kit.runWays, ways, 64);
    Rs_AppendArg(cmd, KIT_PATH * 3, L"--routes");
    Rs_AppendArg(cmd, KIT_PATH * 3, ways);
    if (g_kit.classicUsed) {
        wchar_t glb[KIT_PATH];
        Rs_PathJoin(glb, KIT_PATH, g_kit.work, KIT_CLASSIC);
        Rs_AppendArg(cmd, KIT_PATH * 3, L"--classic");
        Rs_AppendArg(cmd, KIT_PATH * 3, glb);
    }
    g_kit.jobKind = KIT_JOB_BLENDER;
    g_kit.lastCreator[0] = 0;
    g_kit.job = Rs_RunTool(page, g_kit.blenderPath, cmd, g_kit.work);
    return g_kit.job != 0;
}

// The demo driver: the check of the page Character on the demo GLB decides the
// switches as there (the model's own four wheels: --wheels off; the vertex
// colour suggestion; the upscale suggestion), and its findings are said.
static int Kit_StartDemo(HWND page)
{
    struct RsHiresCheck c;
    struct RsWizMsg m[48];
    wchar_t glb[KIT_PATH], beside[KIT_PATH], dir[KIT_PATH];
    int i, n, errors = 0, read;

    Kit_SetPhase(KIT_PH_DEMO, KIT_ST_RUNNING);
    Kit_Copy(g_kit.progressText, KIT_TEXT, L"Building the demo driver...");
    Kit_WayPath(glb, KIT_PATH, g_kit.work, RS_ROUTE_HIRES, KIT_DEMO_GLB);
    memset(&c, 0, sizeof(c));
    RsHires_CheckFile(glb, &c);
    // As the page Character: the suggested vertex colour mode is the check's
    // mode before its messages are said (HRG-36 is a finding of glTF mode only).
    g_kit.demoVcol = c.read ? RsCharHires_SuggestColours(&c, RS_ROUTE_HIRES, NULL) : -1;
    if (g_kit.demoVcol >= 0)
        RsHires_SetColourMode(&c, g_kit.demoVcol);
    n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 48);
    for (i = 0; i < n; i++) {
        if (m[i].severity == RS_SEV_ERROR || m[i].severity == RS_SEV_WARNING)
            Kit_Msg(m[i].severity, m[i].text, m[i].fix);
        errors += m[i].severity == RS_SEV_ERROR;
    }
    g_kit.demoWheelsOff = c.read && c.wheels == 4;
    g_kit.demoUpscale = c.read && RsCharHires_SuggestUpscale(&c, RS_ROUTE_HIRES, NULL);
    g_kit.demoParts = c.read ? c.parts : 0u;
    read = c.read;
    RsHires_CheckFree(&c);
    if (errors || !read)
        return 0;
    Kit_WayPath(g_kit.demoClassic, KIT_PATH, g_kit.work, RS_ROUTE_HIRES, KIT_DEMO L".classic.part");
    Kit_Copy(g_kit.demoModel, KIT_PATH, glb);
    g_kit.demoDerived[0] = 0;
    if (g_kit.demoUpscale) {
        const wchar_t *args[RS_HIRES_UPSCALE_ARGS];
        Rs_TempDir(dir, KIT_PATH);
        Rs_PathJoin(beside, KIT_PATH, dir, KIT_DEMO);
        if (RsHires_UpscaledPath(glb, beside, g_kit.demoDerived, KIT_PATH) && _wcsicmp(g_kit.demoDerived, glb) != 0) {
            n = RsHires_UpscaleArgs(glb, g_kit.demoDerived, args);
            return Kit_StartRldpack(page, KIT_JOB_UPSCALE, args, n);
        }
        g_kit.demoDerived[0] = 0;
    }
    {
        // make-char as the page Character builds a Hi-Res driver from it
        // (Char_MakeArgs with its defaults, then RsCharHires_FallbackArgs).
        const wchar_t *args[40];
        const wchar_t *fallback[RS_CHARHIRES_FALLBACK_ARGS];
        int k = 0, f, j;
        args[k++] = L"make-char";
        args[k++] = L"--machine";
        args[k++] = L"--model";
        args[k++] = glb;
        args[k++] = L"--name";
        args[k++] = KIT_NAME;
        args[k++] = L"--template";
        args[k++] = L"14";
        args[k++] = L"--class";
        args[k++] = L"balanced";
        args[k++] = L"--size";
        args[k++] = L"100";
        args[k++] = L"--mask";
        args[k++] = L"uka";
        args[k++] = L"--out";
        args[k++] = g_kit.demoClassic;
        f = RsCharHires_FallbackArgs(RS_ROUTE_HIRES, fallback);
        for (j = 0; j < f; j++)
            args[k++] = fallback[j];
        if (g_kit.demoWheelsOff) {
            args[k++] = L"--wheels";
            args[k++] = L"off";
        }
        return Kit_StartRldpack(page, KIT_JOB_CLASSIC, args, k);
    }
}

static int Kit_StartPack(HWND page)
{
    const wchar_t *args[RS_HIRES_PACK_ARGS];
    wchar_t out[KIT_PATH];
    int n;
    Kit_WayPath(out, KIT_PATH, g_kit.work, RS_ROUTE_HIRES, KIT_DEMO);
    n = RsHires_PackArgsParts(g_kit.demoClassic, g_kit.demoModel, out, g_kit.demoVcol, g_kit.demoParts, args);
    return Kit_StartRldpack(page, KIT_JOB_PACK, args, n);
}

// make-char of the demo after the upscale (when the upscale ran).
static int Kit_StartClassicAfterUpscale(HWND page)
{
    const wchar_t *args[40];
    const wchar_t *fallback[RS_CHARHIRES_FALLBACK_ARGS];
    wchar_t glb[KIT_PATH];
    int k = 0, f, j;
    Kit_WayPath(glb, KIT_PATH, g_kit.work, RS_ROUTE_HIRES, KIT_DEMO_GLB);
    Kit_Copy(g_kit.demoModel, KIT_PATH, g_kit.demoDerived);
    args[k++] = L"make-char";
    args[k++] = L"--machine";
    args[k++] = L"--model";
    args[k++] = glb;
    args[k++] = L"--name";
    args[k++] = KIT_NAME;
    args[k++] = L"--template";
    args[k++] = L"14";
    args[k++] = L"--class";
    args[k++] = L"balanced";
    args[k++] = L"--size";
    args[k++] = L"100";
    args[k++] = L"--mask";
    args[k++] = L"uka";
    args[k++] = L"--out";
    args[k++] = g_kit.demoClassic;
    f = RsCharHires_FallbackArgs(RS_ROUTE_HIRES, fallback);
    for (j = 0; j < f; j++)
        args[k++] = fallback[j];
    if (g_kit.demoWheelsOff) {
        args[k++] = L"--wheels";
        args[k++] = L"off";
    }
    return Kit_StartRldpack(page, KIT_JOB_CLASSIC, args, k);
}

// START-DEMO-PUPPE.bat: the command of "Test the demo".
static int Kit_WriteBat(void)
{
    wchar_t demo[KIT_PATH], bat[KIT_PATH], why[KIT_TEXT];
    wchar_t *cmd = Rs_Alloc(8192 * sizeof(wchar_t)), *text = Rs_Alloc(9000 * sizeof(wchar_t));
    int ok;
    Kit_WayPath(demo, KIT_PATH, g_kit.outPath, RS_ROUTE_HIRES, KIT_DEMO);   // where it will be
    ok = Rs_TestCharacterCommand(demo, 14, cmd, 8192);
    Kit_Copy(why, KIT_TEXT, L"");
    if (ok) {
        swprintf(text, 9000,
                 L"@echo off\r\nrem The demo driver of the Creator Kit, made by Reload Studio from your own game data.\r\n"
                 L"rem Starts CTR Reload with it in a race. It never appears in the driver select.\r\n"
                 L"chcp 65001 >nul\r\n%ls\r\n", cmd);
        Kit_WayPath(bat, KIT_PATH, g_kit.work, RS_ROUTE_HIRES, KIT_BAT);
        ok = Rs_WriteTextFile(bat, text);
    } else {
        Kit_Msg(RS_SEV_WARNING, L"START-DEMO-PUPPE.bat was not written.", why);
    }
    Rs_Free(cmd);
    Rs_Free(text);
    return ok;
}

// Moves every file of kit.part, of its scripts and of its way folders into the
// output folder (a way folder of the output that this run did not make stays
// as it was).
static int Kit_MoveOut(void)
{
    wchar_t pattern[KIT_PATH], src[KIT_PATH], dst[KIT_PATH], sub[KIT_PATH], subOut[KIT_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int pass, ok = 1;
    // Pass 0 the folder itself, 1 scripts, 2.. the way folders.
    for (pass = 0; pass < 2 + RS_ROUTES; pass++) {
        const wchar_t *from = pass ? sub : g_kit.work, *to = pass ? subOut : g_kit.outPath;
        if (pass) {
            const wchar_t *word = pass == 1 ? L"scripts" : g_wayWords[pass - 2];
            Rs_PathJoin(sub, KIT_PATH, g_kit.work, word);
            Rs_PathJoin(subOut, KIT_PATH, g_kit.outPath, word);
            if (!Rs_DirExists(sub))
                continue;
            CreateDirectoryW(subOut, NULL);
        }
        Rs_PathJoin(pattern, KIT_PATH, from, L"*");
        h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do {
            size_t n = wcslen(fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (n > 5 && _wcsicmp(fd.cFileName + n - 5, L".part") == 0)
                continue;
            Rs_PathJoin(src, KIT_PATH, from, fd.cFileName);
            Rs_PathJoin(dst, KIT_PATH, to, fd.cFileName);
            if (!MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
                ok = 0;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return ok;
}

static void Kit_Relayout(void);

// The next step of the chain after a job ended well; 0 = nothing more to start.
static int Kit_Next(HWND page)
{
    wchar_t probe[KIT_PATH];
    switch (g_kit.jobKind) {
    case KIT_JOB_EXTRACT:
        Kit_SetPhase(KIT_PH_EXTRACT, KIT_ST_OK);
        if (g_kit.classicUsed) {
            // The old character's model first (rldpack char-export), then Blender.
            const wchar_t *args[] = { L"char-export", g_kit.classic, L"-o", probe, L"--machine" };
            Rs_PathJoin(probe, KIT_PATH, g_kit.work, KIT_CLASSIC);
            Kit_SetPhase(KIT_PH_BLENDER, KIT_ST_RUNNING);
            Kit_Copy(g_kit.progressText, KIT_TEXT, L"Reading your old character...");
            if (!Kit_StartRldpack(page, KIT_JOB_EXPORT, args, 5)) {
                Kit_Msg(RS_SEV_ERROR, L"Reading the old character could not be started.", L"Generate again.");
                return -1;
            }
            return 1;
        }
        if (!Kit_StartBlender(page)) {
            Kit_Msg(RS_SEV_ERROR, L"Blender could not be started.", L"Choose blender.exe again on the step What the kit needs.");
            return -1;
        }
        return 1;
    case KIT_JOB_EXPORT:
        if (!Kit_StartBlender(page)) {
            Kit_Msg(RS_SEV_ERROR, L"Blender could not be started.", L"Choose blender.exe again on the step What the kit needs.");
            return -1;
        }
        return 1;
    case KIT_JOB_BLENDER: {
        // Every way chosen has its template and its figure export.
        int r;
        for (r = 0; r < RS_ROUTES; r++) {
            wchar_t name[64], text[KIT_TEXT];
            if (!Kit_WayOn(g_kit.runWays, r))
                continue;
            Kit_WayBlend(name, 64, r);
            Kit_WayPath(probe, KIT_PATH, g_kit.work, r, name);
            if (!Rs_FileExists(probe)) {
                swprintf(text, KIT_TEXT, L"Blender ended without writing %ls\\%ls.", g_wayWords[r], name);
                Kit_Msg(RS_SEV_ERROR, text, L"Click Show details to see everything Blender printed.");
                Kit_SetPhase(KIT_PH_BLENDER, KIT_ST_FAILED);
                return -1;
            }
        }
        Kit_SetPhase(KIT_PH_BLENDER, KIT_ST_OK);
        if (!g_kit.demo || !Kit_WayOn(g_kit.runWays, RS_ROUTE_HIRES)) {
            Kit_SetPhase(KIT_PH_DEMO, KIT_ST_SKIPPED);
            return 0;
        }
        if (!Kit_StartDemo(page)) {
            if (g_kit.msgCount == 0)
                Kit_Msg(RS_SEV_ERROR, L"The demo driver could not be built.", L"Click Show details to see everything rldpack printed.");
            Kit_SetPhase(KIT_PH_DEMO, KIT_ST_FAILED);
            return -1;
        }
        return 1;
    }
    case KIT_JOB_UPSCALE:
        return Kit_StartClassicAfterUpscale(page) ? 1 : -1;
    case KIT_JOB_CLASSIC:
        if (wcscmp(g_kit.resultState, L"ok") != 0) {
            Kit_SetPhase(KIT_PH_DEMO, KIT_ST_FAILED);
            return -1;
        }
        return Kit_StartPack(page) ? 1 : -1;
    case KIT_JOB_PACK:
        Kit_WayPath(probe, KIT_PATH, g_kit.work, RS_ROUTE_HIRES, KIT_DEMO);
        if (!Rs_FileExists(probe)) {
            Kit_SetPhase(KIT_PH_DEMO, KIT_ST_FAILED);
            return -1;
        }
        Kit_WriteBat();
        Kit_SetPhase(KIT_PH_DEMO, KIT_ST_OK);
        return 0;
    }
    return 0;
}

static void Kit_EndRun(HWND page, int result)
{
    int i;
    (void)page;
    g_kit.job = 0;
    g_kit.jobKind = KIT_JOB_NONE;
    if (g_kit.demoDerived[0]) {
        DeleteFileW(g_kit.demoDerived);
        g_kit.demoDerived[0] = 0;
    }
    if (result == KIT_RES_OK && !Kit_MoveOut()) {
        Kit_Msg(RS_SEV_ERROR, L"The files of the kit could not be moved into the folder.",
                L"Close every program that has a file of the kit open (Blender, the game) and generate again.");
        result = KIT_RES_FAILED;
    }
    for (i = 0; i < KIT_PHASES; i++) {
        if (g_kit.phase[i] == KIT_ST_RUNNING)
            g_kit.phase[i] = result == KIT_RES_CANCELLED ? KIT_ST_CANCELLED : KIT_ST_FAILED;
    }
    Kit_RemoveTree(g_kit.work);
    g_kit.result = result;
    if (result == KIT_RES_OK) {
        Kit_Msg(RS_SEV_OK, L"Your kit is ready.", L"");
        Kit_Copy(g_kit.progressText, KIT_TEXT, L"Done.");
        g_kit.step = 4;
    } else if (result == KIT_RES_CANCELLED) {
        Kit_Msg(RS_SEV_INFO, L"Cancelled. The folder is as it was before.", L"");
        Kit_Copy(g_kit.progressText, KIT_TEXT, L"Cancelled.");
    } else {
        Kit_Copy(g_kit.progressText, KIT_TEXT, L"Not made - see the findings.");
    }
    if (Rs_Automating())
        Rs_AutoLog(L"  kit: generate %ls", result == KIT_RES_OK ? L"ok" : result == KIT_RES_CANCELLED ? L"cancelled" : L"failed");
    Kit_Update();
    Kit_Relayout();
}

// A job ended.
static void Kit_JobDone(HWND page, int job, int exitCode)
{
    int r;
    if (job == g_kit.versionJob) {
        Kit_VersionDone(exitCode);
        if (g_kit.generateAfterCheck) {
            g_kit.generateAfterCheck = 0;
            if (!Kit_Ready() || !Kit_Generate(page))
                Rs_AutoFail(g_kit.blender != KIT_BL_OK ? g_kit.blText : L"generate did not start");
        }
        Kit_Update();
        return;
    }
    if (job != g_kit.job)
        return;
    g_kit.job = 0;
    if (g_kit.cancelled || exitCode == RS_JOB_CANCELLED) {
        Kit_EndRun(page, KIT_RES_CANCELLED);
        return;
    }
    if (exitCode != 0) {
        int ph = g_kit.jobKind == KIT_JOB_EXTRACT ? KIT_PH_EXTRACT
                 : (g_kit.jobKind == KIT_JOB_BLENDER || g_kit.jobKind == KIT_JOB_EXPORT) ? KIT_PH_BLENDER : KIT_PH_DEMO;
        if (g_kit.jobKind == KIT_JOB_BLENDER && g_kit.lastCreator[0])
            Kit_MsgSplit(RS_SEV_ERROR, g_kit.lastCreator);
        else if (g_kit.msgCount == 0 || g_kit.msg[g_kit.msgCount - 1].severity != RS_SEV_ERROR) {
            wchar_t text[KIT_TEXT];
            swprintf(text, KIT_TEXT, L"%ls failed (exit code %d).", g_phaseTitles[ph], exitCode);
            Kit_Msg(RS_SEV_ERROR, text, L"Click Show details to see everything the step printed.");
        }
        Kit_SetPhase(ph, KIT_ST_FAILED);
        Kit_EndRun(page, KIT_RES_FAILED);
        return;
    }
    r = Kit_Next(page);
    if (r < 0)
        Kit_EndRun(page, KIT_RES_FAILED);
    else if (r == 0)
        Kit_EndRun(page, KIT_RES_OK);
    else
        Kit_Update();
}

static void Kit_JobLine(int job, wchar_t *line)
{
    wchar_t copy[2048];
    wchar_t *f[8];
    int n;
    if (job == g_kit.versionJob) {
        if (!g_kit.blenderVersion[0] && wcsncmp(line, L"Blender ", 8) == 0)
            Kit_Copy(g_kit.blenderVersion, 128, line);
        return;
    }
    if (job != g_kit.job)
        return;
    Kit_RawAdd(line);
    if (wcsncmp(line, L"creator kit:", 12) == 0)
        Kit_Copy(g_kit.lastCreator, KIT_TEXT, line + 12 + (line[12] == L' '));
    Kit_Copy(copy, 2048, line);
    n = Rs_SplitMachine(copy, f, 8);
    if (n >= 4 && wcscmp(f[0], L"msg") == 0) {
        // "@msg <severity> <code> <text> <technical>": the fix by the code of
        // kit-extract (tools/rldpack_kit.inc), the technical field is a path.
        int sev = Rs_SeverityFromText(f[1]);
        const wchar_t *fix = L"Click Show details to see everything the step printed.";
        if (wcscmp(f[2], L"kit-no-data") == 0)
            fix = L"Start the game once so that it has its data, or choose the folder of your game with Choose game.";
        else if (wcscmp(f[2], L"kit-bad-model") == 0)
            fix = L"Use the game data of the disc CTR Reload plays (NTSC-U); start the game once with it.";
        else if (wcscmp(f[2], L"export-file") == 0 || wcscmp(f[2], L"export-model") == 0)
            fix = L"Choose another character file under Start from an old character, or clear the field.";
        else if (wcscmp(f[2], L"kit-write") == 0 || wcscmp(f[2], L"export-write") == 0)
            fix = L"Choose a folder you can write to, for example in Documents.";
        if (sev == RS_SEV_ERROR || (sev == RS_SEV_WARNING && g_kit.jobKind == KIT_JOB_EXTRACT))
            Kit_Msg(sev, f[3], fix);
    } else if (n >= 2 && wcscmp(f[0], L"result") == 0) {
        Kit_Copy(g_kit.resultState, 16, f[1]);
    }
}

// "Start from an old character": the field as a full path ("" = none).
static void Kit_ClassicRead(void)
{
    wchar_t *raw = g_kit.classicEdit ? Rs_GetText(g_kit.classicEdit) : Rs_Dup(L"");
    if (raw[0])
        Kit_Full(raw, g_kit.classic, KIT_PATH);
    else
        g_kit.classic[0] = 0;
    Rs_Free(raw);
}

static void Kit_SetClassic(const wchar_t *path)
{
    g_kit.quiet = 1;
    Rs_SetText(g_kit.classicEdit, path);
    g_kit.quiet = 0;
    Kit_Copy(g_kit.classic, KIT_PATH, path);
    Rs_ConfigSet(L"kit.classic", path);
}

// 1 if the file begins as a character container does (RLDCHAR); rldpack
// char-export checks the rest.
static int Kit_IsCharacter(const wchar_t *path)
{
    static const char magic[8] = { 'R', 'L', 'D', 'C', 'H', 'A', 'R', 0 };
    char head[8];
    FILE *f = _wfopen(path, L"rb");
    int ok;
    if (!f)
        return 0;
    ok = fread(head, 1, 8, f) == 8 && memcmp(head, magic, 8) == 0;
    fclose(f);
    return ok;
}

// Generate: the checks once more, kit.part made, the scripts written, step 1.
static int Kit_Generate(HWND page)
{
    wchar_t chosen[KIT_PATH], probe[KIT_PATH], why[KIT_TEXT];
    int i;

    if (g_kit.job)
        return 0;
    Kit_CheckGame();
    Kit_CheckOut();
    Kit_MsgClear();
    g_kit.rawLen = 0;
    if (g_kit.raw)
        g_kit.raw[0] = 0;
    g_kit.result = KIT_RES_NONE;
    g_kit.cancelled = 0;
    for (i = 0; i < KIT_PHASES; i++)
        g_kit.phase[i] = KIT_ST_TODO;
    if (g_kit.game != KIT_GAME_OK) {
        Kit_Msg(RS_SEV_ERROR, g_kit.gameText, g_kit.gameFixText);
        return 0;
    }
    if (g_kit.blender != KIT_BL_OK) {
        Kit_Msg(RS_SEV_ERROR, g_kit.blText, g_kit.blFixText);
        return 0;
    }
    if (g_kit.out != KIT_OUT_OK) {
        Kit_Msg(RS_SEV_ERROR, g_kit.outText, g_kit.outFixText);
        return 0;
    }
    // The ways of this run: those chosen now (step 4 lists what it made).
    g_kit.runWays = g_kitWays;
    Kit_ClassicRead();
    g_kit.classicUsed = 0;
    if (g_kit.classic[0]) {
        if (!Kit_IsCharacter(g_kit.classic)) {
            Kit_Msg(RS_SEV_ERROR, L"The old character cannot be read: it is no .rldchar file.",
                    L"Choose a character file (.rldchar) under Start from an old character, or clear the field.");
            return 0;
        }
        g_kit.classicUsed = 1;
    }
    if (Kit_HasKit(g_kit.outPath)) {
        swprintf(why, KIT_TEXT, L"Replace the kit in %ls?", g_kit.outPath);
        if (!Rs_AskYesNo(Rs_MainWindow(), L"Creator Kit", why))
            return 0;
    }
    if (SHCreateDirectoryExW(NULL, g_kit.outPath, NULL) != ERROR_SUCCESS && !Rs_DirExists(g_kit.outPath)) {
        Kit_Msg(RS_SEV_ERROR, L"The folder could not be made.", L"Choose a folder you can write to, for example in Documents.");
        return 0;
    }
    Rs_PathJoin(g_kit.work, KIT_PATH, g_kit.outPath, KIT_WORK);
    Kit_RemoveTree(g_kit.work);
    if (!CreateDirectoryW(g_kit.work, NULL)) {
        Kit_Msg(RS_SEV_ERROR, L"The folder could not be written to.", L"Choose a folder you can write to, for example in Documents.");
        return 0;
    }
    // The scripts: from kit.scripts (developers, tests), else this exe's own.
    Rs_ConfigGet(L"kit.scripts", chosen, KIT_PATH);
    if (chosen[0]) {
        Kit_Full(chosen, g_kit.scripts, KIT_PATH);
        Rs_PathJoin(probe, KIT_PATH, g_kit.scripts, L"make_creator_kit.py");
        if (!Rs_FileExists(probe)) {
            Kit_Msg(RS_SEV_ERROR, L"The setting kit.scripts names a folder without make_creator_kit.py.",
                    L"Remove kit.scripts from the settings, or point it at the folder templates.");
            Kit_RemoveTree(g_kit.work);
            return 0;
        }
    } else {
        Rs_PathJoin(g_kit.scripts, KIT_PATH, g_kit.work, L"scripts");
        CreateDirectoryW(g_kit.scripts, NULL);
        for (i = 0; i < KIT_SCRIPT_COUNT; i++) {
            Rs_PathJoin(probe, KIT_PATH, g_kit.scripts, g_kitScripts[i].name);
            if (!Kit_WriteResource(g_kitScripts[i].id, probe)) {
                Kit_Msg(RS_SEV_ERROR, L"The scripts of the kit could not be written.",
                        L"Choose a folder you can write to, for example in Documents.");
                Kit_RemoveTree(g_kit.work);
                return 0;
            }
        }
    }
    // The guide beside the kit.
    Rs_PathJoin(probe, KIT_PATH, g_kit.work, KIT_GUIDE);
    Kit_WriteResource(RS_RES_KIT_GUIDE, probe);
    g_kit.step = 3;
    if (!Kit_StartExtract(page)) {
        Kit_Msg(RS_SEV_ERROR, L"Reading the game data could not be started.", L"Generate again; if it stays, restart Reload Studio.");
        Kit_EndRun(page, KIT_RES_FAILED);
        return 0;
    }
    Kit_Update();
    Kit_Relayout();
    return 1;
}

// ---------------------------------------------------------------------------
// Opening things
// ---------------------------------------------------------------------------

// The guide: next to the kit when there is one, else in Rs_TempDir.
static void Kit_OpenGuide(void)
{
    wchar_t path[KIT_PATH], dir[KIT_PATH];
    int written;
    if (g_kit.out == KIT_OUT_OK && Rs_DirExists(g_kit.outPath))
        Kit_Copy(dir, KIT_PATH, g_kit.outPath);
    else
        Rs_TempDir(dir, KIT_PATH);
    Rs_PathJoin(path, KIT_PATH, dir, KIT_GUIDE);
    written = Kit_WriteResource(RS_RES_KIT_GUIDE, path);
    if (Rs_Automating()) {
        Rs_AutoLog(L"  open: guide %ls (%ls) - written from this exe", path,
                   written && Rs_FileExists(path) ? L"exists" : L"missing");
        return;
    }
    if (!written || (INT_PTR)ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL) <= 32)
        Rs_Tell(Rs_MainWindow(), L"Creator Kit", L"The guide could not be opened. It is CREATOR_KIT.html in the folder of your kit.");
}

// The template of a way (RS_ROUTE_*) in Blender.
static void Kit_OpenTemplate(int route)
{
    wchar_t blend[KIT_PATH], name[64], dir[KIT_PATH], params[KIT_PATH + 8], text[KIT_TEXT];
    Kit_WayBlend(name, 64, route);
    Kit_WayPath(blend, KIT_PATH, g_kit.outPath, route, name);
    Kit_WayPath(dir, KIT_PATH, g_kit.outPath, route, NULL);
    if (Rs_Automating()) {
        Rs_AutoLog(L"  open: template %ls (%ls) with %ls", blend, Rs_FileExists(blend) ? L"exists" : L"missing",
                   g_kit.blenderPath);
        return;
    }
    swprintf(params, KIT_PATH + 8, L"\"%ls\"", blend);
    if (!Rs_FileExists(blend) || !g_kit.blenderPath[0] ||
        (INT_PTR)ShellExecuteW(NULL, L"open", g_kit.blenderPath, params, dir, SW_SHOWNORMAL) <= 32) {
        swprintf(text, KIT_TEXT, L"Blender could not be started with the template. Open %ls\\%ls in Blender yourself.",
                 g_wayWords[route], name);
        Rs_Tell(Rs_MainWindow(), L"Creator Kit", text);
    }
}

// The way the main action of step 4 opens: the one way made, else none (-1:
// it shows the folder).
static int Kit_OneWay(void)
{
    return g_kit.runWays == RS_KIT_WAYS_ALL ? -1 : g_kit.runWays;
}

static void Kit_OpenFolder(void)
{
    wchar_t blend[KIT_PATH], params[KIT_PATH + 16];
    // The folder, with NOTICE.txt marked.
    Rs_PathJoin(blend, KIT_PATH, g_kit.outPath, KIT_NOTICE);
    if (Rs_Automating()) {
        Rs_AutoLog(L"  open: folder %ls (%ls)", g_kit.outPath, Rs_DirExists(g_kit.outPath) ? L"exists" : L"missing");
        return;
    }
    swprintf(params, KIT_PATH + 16, L"/select,\"%ls\"", blend);
    ShellExecuteW(NULL, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
}

static void Kit_TestDemo(void)
{
    wchar_t demo[KIT_PATH], why[KIT_TEXT];
    Kit_WayPath(demo, KIT_PATH, g_kit.outPath, RS_ROUTE_HIRES, KIT_DEMO);
    if (!Rs_TestCharacter(demo, RS_ROUTE_HIRES, why, KIT_TEXT))
        Kit_Msg(RS_SEV_ERROR, L"The demo driver could not be started.", why);
}

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------

static void Kit_SetLabel(HWND h, const wchar_t *text, COLORREF color)
{
    Rs_SetText(h, text);
    Rs_SetTextColor(h, color);
}

// Texts, states and what is shown, from the state.
static void Kit_Update(void)
{
    int s = g_kit.step, i, running = g_kit.job != 0;
    wchar_t text[KIT_TEXT];
    static const wchar_t *const marks[] = { L"\x25CB", L"\x25CF", L"\x2713", L"\x2717", L"\x2014", L"\x2014" };

    for (i = 0; i < KIT_STEPS; i++) {
        swprintf(text, KIT_TEXT, L"%d  %ls", i + 1, g_stepTitles[i]);
        Rs_SetText(g_kit.stepLbl[i], text);
        SendMessageW(g_kit.stepLbl[i], WM_SETFONT, (WPARAM)Rs_Font(i == s ? RS_FONT_BOLD : RS_FONT_BODY), TRUE);
        Rs_SetTextColor(g_kit.stepLbl[i], i == s ? RS_COL_ACCENT : i < s ? RS_COL_TEXT : RS_COL_MUTED);
    }
    // Step 1
    Kit_SetLabel(g_kit.gameState, g_kit.game == KIT_GAME_UNKNOWN ? L"Not checked yet." : g_kit.gameText,
                 g_kit.game == KIT_GAME_OK ? RS_COL_OK : g_kit.game == KIT_GAME_UNKNOWN ? RS_COL_MUTED : RS_COL_ERROR);
    swprintf(text, KIT_TEXT, L"%ls%ls", g_kit.gameFixText[0] ? L"How to fix: " : L"", g_kit.gameFixText);
    Kit_SetLabel(g_kit.gameFix, text, RS_COL_MUTED);
    Kit_SetLabel(g_kit.blState, g_kit.blender == KIT_BL_UNKNOWN ? L"Not checked yet." : g_kit.blText,
                 g_kit.blender == KIT_BL_OK ? RS_COL_OK
                 : (g_kit.blender == KIT_BL_CHECKING || g_kit.blender == KIT_BL_UNKNOWN) ? RS_COL_MUTED
                                                                                          : RS_COL_ERROR);
    swprintf(text, KIT_TEXT, L"%ls%ls", g_kit.blFixText[0] ? L"How to fix: " : L"", g_kit.blFixText);
    Kit_SetLabel(g_kit.blFix, text, RS_COL_MUTED);
    // Step 2
    Kit_SetLabel(g_kit.outState, g_kit.outText, g_kit.out == KIT_OUT_OK ? RS_COL_OK : RS_COL_ERROR);
    swprintf(text, KIT_TEXT, L"%ls%ls", g_kit.outFixText[0] ? L"How to fix: " : L"", g_kit.outFixText);
    Kit_SetLabel(g_kit.outFix, text, RS_COL_MUTED);
    // Step 3
    for (i = 0; i < KIT_PHASES; i++) {
        int st = g_kit.phase[i];
        swprintf(text, KIT_TEXT, L"%ls  %d/3  %ls%ls", marks[st], i + 1, g_phaseTitles[i],
                 st == KIT_ST_SKIPPED ? L" - not asked for" : st == KIT_ST_CANCELLED ? L" - cancelled" : L"");
        Kit_SetLabel(g_kit.phaseLbl[i], text, st == KIT_ST_OK ? RS_COL_OK : st == KIT_ST_FAILED ? RS_COL_ERROR
                                           : st == KIT_ST_RUNNING ? RS_COL_ACCENT : RS_COL_MUTED);
    }
    Kit_SetLabel(g_kit.progress, g_kit.progressText, RS_COL_MUTED);
    if (Kit_Checked(g_kit.detailsBox) && g_kit.details)
        Rs_SetText(g_kit.details, g_kit.raw ? g_kit.raw : L"");
    // Step 4: the files of the kit folder itself, then those of each way made.
    {
        wchar_t list[KIT_PATH * 2];
        const int made = g_kit.result == KIT_RES_OK;
        // What only that way has; the files every way has are said above them.
        static const wchar_t *const what[RS_ROUTES] = {
            L"within the limits of the classic model; the game poses it by itself",
            L"within the limits of Low-Poly, with the clips the route plays",
            L"12 clips + 2 optional finish clips; creator-kit-demo-local.glb - your figure on the kart from your game "
            L"files, for a test on this PC"
        };
        swprintf(list, KIT_PATH * 2,
                 L"In %ls:\r\n"
                 L"  NOTICE.txt - the notice; CREATOR_KIT.html - the guide; scripts - how the templates were made\r\n"
                 L"  kit-kart.glb, kit-wheel.glb, kit-info.json, kit-extract.txt - the kart and its wheels from your game files\r\n"
                 L"%ls"
                 L"In the folder of each way: creator-kit-<way>.blend - the template (the puppet, its skeleton, the kart from "
                 L"your game files); creator-kit-<way>-figure.glb - your figure without the kart, every player's game adds the "
                 L"kart from their own game; NOTICE.txt; creator-kit-report.json - what the generator checked",
                 g_kit.outPath,
                 g_kit.classicUsed && made ? L"  classic.glb - your old character, in the hidden collection Classic of the templates\r\n"
                                           : L"");
        Rs_SetText(g_kit.files, list);
        for (i = 0; i < RS_ROUTES; i++) {
            swprintf(list, KIT_PATH * 2, L"%ls - %ls\\ - %ls%ls", g_wayNames[i], g_wayWords[i], what[i],
                     i == RS_ROUTE_HIRES && g_kit.phase[KIT_PH_DEMO] == KIT_ST_OK
                         ? L"; demo-puppet.rldchar, START-DEMO-PUPPE.bat - the demo driver and its start"
                         : L"");
            Rs_SetText(g_kit.wayFiles[i], list);
        }
    }
    // The bar.
    // Back on step 0 leaves the kit for its host (Character, Build a character).
    Kit_Show(g_kit.back, (s > 0 || Rs_ModuleHost(Rs_PageIdOf(g_kit.page)) >= 0) && !running);
    Kit_Show(g_kit.cancel, running);
    Kit_Show(g_kit.guide, s == 0 || s == 4);
    // Step 4: the main action opens the template when one way was made, else
    // it shows the folder (and the button of its own goes).
    Kit_Show(g_kit.folder, s == 4 && Kit_OneWay() >= 0);
    Kit_Show(g_kit.testDemo, s == 4 && g_kit.phase[KIT_PH_DEMO] == KIT_ST_OK);
    Kit_Show(g_kit.toChar, s == 4);
    Rs_SetText(g_kit.main, s == 3 ? L"Generate demo files"
                           : s == 4 ? (Kit_OneWay() >= 0 ? L"Open the template in Blender" : L"Show in folder") : L"Next");
    Kit_Enable(g_kit.main, !running && (s == 0 || (s == 1 && g_kit.game == KIT_GAME_OK && g_kit.blender == KIT_BL_OK) ||
                                        (s == 2 && g_kit.out == KIT_OUT_OK) || (s == 3 && Kit_Ready()) || s == 4));
    // What each step shows.
    Kit_Show(g_kit.intro, s == 0);
    Kit_Show(g_kit.gameHead, s == 1);
    Kit_Show(g_kit.gameState, s == 1);
    Kit_Show(g_kit.gameFix, s == 1 && g_kit.gameFixText[0]);
    Kit_Show(g_kit.gameBtn, s == 1);
    Kit_Show(g_kit.blHead, s == 1);
    Kit_Show(g_kit.blState, s == 1);
    Kit_Show(g_kit.blFix, s == 1 && g_kit.blFixText[0]);
    Kit_Show(g_kit.blBtn, s == 1);
    Kit_Show(g_kit.outHead, s == 2);
    Kit_Show(g_kit.outEdit, s == 2);
    Kit_Show(g_kit.outBtn, s == 2);
    Kit_Show(g_kit.outState, s == 2);
    Kit_Show(g_kit.outFix, s == 2 && g_kit.outFixText[0]);
    Kit_Show(g_kit.waysHead, s == 2);
    Kit_Show(g_kit.ways, s == 2);
    if (g_kit.ways && (int)SendMessageW(g_kit.ways, CB_GETCURSEL, 0, 0) != (g_kitWays == RS_KIT_WAYS_ALL ? 0 : g_kitWays + 1))
        SendMessageW(g_kit.ways, CB_SETCURSEL, (WPARAM)(g_kitWays == RS_KIT_WAYS_ALL ? 0 : g_kitWays + 1), 0);
    Kit_Show(g_kit.demoBox, s == 2 && Kit_DemoOffered());
    Kit_Show(g_kit.classicHead, s == 2);
    Kit_Show(g_kit.classicEdit, s == 2);
    Kit_Show(g_kit.classicBtn, s == 2);
    Kit_Show(g_kit.classicNote, s == 2);
    for (i = 0; i < KIT_PHASES; i++)
        Kit_Show(g_kit.phaseLbl[i], s == 3);
    Kit_Show(g_kit.progress, s == 3);
    Kit_Show(g_kit.msgs, s == 3 && !Kit_Checked(g_kit.detailsBox));
    Kit_Show(g_kit.detailsBox, s == 3);
    Kit_Show(g_kit.details, s == 3 && Kit_Checked(g_kit.detailsBox));
    Kit_Show(g_kit.notice, s == 3);
    Kit_Show(g_kit.files, s == 4);
    for (i = 0; i < RS_ROUTES; i++) {
        const int shown = s == 4 && Kit_WayOn(g_kit.runWays, i);
        Kit_Show(g_kit.wayFiles[i], shown);
        Kit_Show(g_kit.wayOpen[i], shown);
    }
    Kit_Show(g_kit.nextText, s == 4);
}

static void Kit_Relayout(void)
{
    RECT rc;
    if (!g_kit.page)
        return;
    GetClientRect(g_kit.page, &rc);
    SendMessageW(g_kit.page, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
}

static void Kit_Create(HWND page)
{
    int i;
    wchar_t cfg[KIT_PATH];
    memset(&g_kit, 0, sizeof(g_kit));
    g_kit.page = page;
    g_kit.demo = 1;
    g_kit.demoVcol = -1;
    for (i = 0; i < KIT_STEPS; i++)
        g_kit.stepLbl[i] = Rs_Label(page, KIT_ID_STEP + i, g_stepTitles[i], RS_FONT_BODY);
    g_kit.runWays = g_kitWays;
    g_kit.intro = Rs_Label(page, KIT_ID_INTRO,
        L"The Creator Kit gives you ready Blender templates for a driver of your own, made on this PC from your own game files:\r\n\r\n"
        L"  \x2022  a template for each way of the page Character - Vanilla, Low-Poly and Hi-Res - with its export already set up\r\n"
        L"  \x2022  a neutral jointed puppet with a skeleton and its clips to start from\r\n"
        L"  \x2022  the original kart and its wheels from your game files, to build around\r\n"
        L"  \x2022  if you like, a demo driver to try in the game right away\r\n\r\n"
        L"The kart and the wheels are generated on this PC from your own game files. You need Blender 5.2 and your "
        L"own copy of the original game, set up for CTR Reload.",
        RS_FONT_BODY);
    g_kit.gameHead = Rs_Label(page, KIT_ID_GAME_HEAD, L"Game data", RS_FONT_BOLD);
    g_kit.gameState = Rs_Label(page, KIT_ID_GAME_STATE, L"", RS_FONT_BODY);
    g_kit.gameFix = Rs_Label(page, KIT_ID_GAME_FIX, L"", RS_FONT_SMALL);
    g_kit.gameBtn = Rs_Button(page, KIT_ID_GAME_BTN, L"Choose game...");
    g_kit.blHead = Rs_Label(page, KIT_ID_BL_HEAD, L"Blender 5.2", RS_FONT_BOLD);
    g_kit.blState = Rs_Label(page, KIT_ID_BL_STATE, L"", RS_FONT_BODY);
    g_kit.blFix = Rs_Label(page, KIT_ID_BL_FIX, L"", RS_FONT_SMALL);
    g_kit.blBtn = Rs_Button(page, KIT_ID_BL_BTN, L"Choose Blender...");
    g_kit.outHead = Rs_Label(page, KIT_ID_OUT_HEAD, L"Folder of the kit", RS_FONT_BOLD);
    cfg[0] = 0;
    Rs_ConfigGet(L"kit.out", cfg, KIT_PATH);
    if (!cfg[0] && !Rs_Automating()) {
        PWSTR docs = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Documents, 0, NULL, &docs)) && docs)
            Rs_PathJoin(cfg, KIT_PATH, docs, L"CTR Reload Creator Kit");
        if (docs)
            CoTaskMemFree(docs);
    }
    g_kit.outEdit = Rs_Edit(page, KIT_ID_OUT_EDIT, cfg, ES_AUTOHSCROLL);
    g_kit.outBtn = Rs_Button(page, KIT_ID_OUT_BTN, L"Browse...");
    g_kit.outState = Rs_Label(page, KIT_ID_OUT_STATE, L"", RS_FONT_BODY);
    g_kit.outFix = Rs_Label(page, KIT_ID_OUT_FIX, L"", RS_FONT_SMALL);
    // THE WAYS (K9): All three ways (the default) or one of them.
    g_kit.waysHead = Rs_Label(page, KIT_ID_WAYS_HEAD, L"Templates", RS_FONT_BOLD);
    g_kit.ways = Rs_Combo(page, KIT_ID_WAYS);
    SendMessageW(g_kit.ways, CB_ADDSTRING, 0, (LPARAM)L"All three ways");
    for (i = 0; i < RS_ROUTES; i++)
        SendMessageW(g_kit.ways, CB_ADDSTRING, 0, (LPARAM)g_wayNames[i]);
    SendMessageW(g_kit.ways, CB_SETCURSEL, (WPARAM)(g_kitWays == RS_KIT_WAYS_ALL ? 0 : g_kitWays + 1), 0);
    Rs_SetTip(g_kit.ways, L"Which templates the kit makes: one for each way of the page Character, or the one "
                                L"you build for.");
    g_kit.demoBox = Rs_Check(page, KIT_ID_DEMO, L"Also build a demo driver to try in the game (from the Hi-Res template)");
    SendMessageW(g_kit.demoBox, BM_SETCHECK, BST_CHECKED, 0);
    // "Start from an old character" (C's guide, section 11): optional.
    g_kit.classicHead = Rs_Label(page, KIT_ID_CLASSIC_HEAD, L"Start from an old character", RS_FONT_BOLD);
    cfg[0] = 0;
    Rs_ConfigGet(L"kit.classic", cfg, KIT_PATH);
    g_kit.classicEdit = Rs_Edit(page, KIT_ID_CLASSIC_EDIT, cfg, ES_AUTOHSCROLL);
    g_kit.classicBtn = Rs_Button(page, KIT_ID_CLASSIC_BTN, L"Browse...");
    g_kit.classicNote = Rs_Label(page, KIT_ID_CLASSIC_NOTE,
        L"Optional: a character file (.rldchar) of yours. Its model goes into the template as a starting point "
        L"(the hidden collection Classic). Leave it empty to start from the puppet.",
        RS_FONT_SMALL);
    Rs_SetTextColor(g_kit.classicNote, RS_COL_MUTED);
    for (i = 0; i < KIT_PHASES; i++)
        g_kit.phaseLbl[i] = Rs_Label(page, KIT_ID_PHASE + i, g_phaseTitles[i], RS_FONT_BODY);
    g_kit.progress = Rs_Label(page, KIT_ID_PROGRESS, L"", RS_FONT_SMALL);
    g_kit.msgs = Rs_MsgList(page, KIT_ID_MSGS);
    g_kit.detailsBox = Rs_Check(page, KIT_ID_DETAILS_BOX, L"Show details");
    g_kit.details = Rs_Edit(page, KIT_ID_DETAILS, L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
    SendMessageW(g_kit.details, WM_SETFONT, (WPARAM)Rs_Font(RS_FONT_MONO), FALSE);
    // THE NOTICE (K1): plain text in the text colour, no icon.
    {
        wchar_t *notice = Kit_NoticeText();
        g_kit.notice = Rs_Label(page, KIT_ID_NOTICE, notice, RS_FONT_BODY);
        Rs_Free(notice);
    }
    g_kit.files = Rs_Label(page, KIT_ID_FILES, L"", RS_FONT_SMALL);
    for (i = 0; i < RS_ROUTES; i++) {
        wchar_t tip[KIT_TEXT], name[64];
        g_kit.wayFiles[i] = Rs_Label(page, KIT_ID_WAY_FILES + i, L"", RS_FONT_SMALL);
        g_kit.wayOpen[i] = Rs_Button(page, KIT_ID_WAY_OPEN + i, L"Open in Blender");
        Kit_WayBlend(name, 64, i);
        swprintf(tip, KIT_TEXT, L"Opens %ls\\%ls in Blender.", g_wayWords[i], name);
        Rs_SetTip(g_kit.wayOpen[i], tip);
    }
    g_kit.nextText = Rs_Label(page, KIT_ID_NEXT_TEXT,
        L"Next: open a template in Blender, make your driver on the puppet (the guide shows how), export it with the "
        L"stored export (creator-kit-<way>-figure.glb in the folder of the way), and build it on the page Character "
        L"with the same way.",
        RS_FONT_BODY);
    g_kit.back = Rs_Button(page, KIT_ID_BACK, L"Back");
    g_kit.main = Rs_PrimaryButton(page, KIT_ID_MAIN, L"Next");
    g_kit.cancel = Rs_Button(page, KIT_ID_CANCEL, L"Cancel");
    g_kit.guide = Rs_Button(page, KIT_ID_GUIDE, L"Guide");
    g_kit.folder = Rs_Button(page, KIT_ID_FOLDER, L"Show in folder");
    g_kit.testDemo = Rs_Button(page, KIT_ID_TEST_DEMO, L"Test the demo");
    g_kit.toChar = Rs_Button(page, KIT_ID_TO_CHAR, L"Go to Character");
}

// The height a label needs at width w in its own font (wrapped), at least one line.
static int Kit_TextHeight(HWND label, int w)
{
    RECT r = { 0, 0, w > 0 ? w : 1, 0 };
    wchar_t *text = Rs_GetText(label);
    HDC dc = GetDC(label);
    HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(label, WM_GETFONT, 0, 0));
    DrawTextW(dc, text[0] ? text : L"X", -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, old);
    ReleaseDC(label, dc);
    Rs_Free(text);
    return r.bottom + Rs_Px(2);
}

void Rs_KitChooseWay(int way)
{
    if (way != RS_KIT_WAYS_ALL && (way < 0 || way >= RS_ROUTES))
        return;
    g_kitWays = way;
    if (g_kit.page) {
        Kit_Update();
        Kit_Relayout();
    }
}

static void Kit_Layout(HWND page, int w, int h)
{
    // In little room (1366 x 768 at 150 %) the heading is one small line, as
    // on the page Character, so that the step and its bar fit without scrolling.
    const int compact = w < Rs_Px(KIT_FULL_W) || h < Rs_Px(KIT_FULL_H);
    // The right and bottom margins are those the scrolling view adds to what a
    // layout placed (Rs_PageExtent): less would make the page grow and scroll.
    int left = Rs_Px(compact ? 16 : 32), right = w - Rs_Px(32), top, bottom = h - Rs_Px(24);
    int bh = Rs_Px(32), lh = Rs_Px(22), gap = Rs_Px(compact ? 8 : 12), bw = Rs_Px(200);
    int i, x, y, iw, stepW, barTop;
    RECT card, in;

    Rs_PageSetCompactHead(page, compact);
    top = compact ? Rs_PageHeadBottom(page, w) + Rs_Px(6) : Rs_PageTop();
    stepW = (right - left) / KIT_STEPS;
    Rs_CardClear(page);
    for (i = 0; i < KIT_STEPS; i++)
        MoveWindow(g_kit.stepLbl[i], left + i * stepW, top, stepW - Rs_Px(8), lh, TRUE);
    barTop = bottom - bh;
    card.left = left;
    card.top = top + lh + gap;
    card.right = right;
    card.bottom = barTop - gap;
    Rs_CardAdd(page, &card, g_stepTitles[g_kit.step]);
    in = Rs_CardInner(&card, 1);
    iw = in.right - in.left;
    y = in.top;
    switch (g_kit.step) {
    case 0:
        MoveWindow(g_kit.intro, in.left, y, iw, Rs_Px(220), TRUE);
        break;
    case 1:
        MoveWindow(g_kit.gameHead, in.left, y, iw, lh, TRUE);
        y += lh + Rs_Px(4);
        MoveWindow(g_kit.gameState, in.left, y, iw - Rs_Px(180), Rs_Px(44), TRUE);
        MoveWindow(g_kit.gameBtn, in.right - Rs_Px(168), y, Rs_Px(168), bh, TRUE);
        y += Rs_Px(46);
        MoveWindow(g_kit.gameFix, in.left, y, iw - Rs_Px(180), Rs_Px(40), TRUE);
        y += Rs_Px(48);
        MoveWindow(g_kit.blHead, in.left, y, iw, lh, TRUE);
        y += lh + Rs_Px(4);
        MoveWindow(g_kit.blState, in.left, y, iw - Rs_Px(180), Rs_Px(44), TRUE);
        MoveWindow(g_kit.blBtn, in.right - Rs_Px(168), y, Rs_Px(168), bh, TRUE);
        y += Rs_Px(46);
        MoveWindow(g_kit.blFix, in.left, y, iw - Rs_Px(180), Rs_Px(40), TRUE);
        break;
    case 2:
        MoveWindow(g_kit.outHead, in.left, y, iw, lh, TRUE);
        y += lh + Rs_Px(4);
        MoveWindow(g_kit.outEdit, in.left, y, iw - Rs_Px(128), Rs_Px(28), TRUE);
        MoveWindow(g_kit.outBtn, in.right - Rs_Px(116), y - Rs_Px(2), Rs_Px(116), bh, TRUE);
        y += Rs_Px(40);
        MoveWindow(g_kit.outState, in.left, y, iw, Rs_Px(22), TRUE);
        y += Rs_Px(24);
        MoveWindow(g_kit.outFix, in.left, y, iw, Rs_Px(20), TRUE);
        y += Rs_Px(g_kit.outFixText[0] ? 26 : 4);
        // The ways: "Templates" with its choice beside it.
        MoveWindow(g_kit.waysHead, in.left, y + Rs_Px(4), Rs_Px(120), lh, TRUE);
        MoveWindow(g_kit.ways, in.left + Rs_Px(128), y, Rs_Px(220), Rs_Px(300), TRUE);
        y += Rs_Px(38);
        if (Kit_DemoOffered()) {
            MoveWindow(g_kit.demoBox, in.left, y, iw, Rs_Px(24), TRUE);
            y += Rs_Px(36);
        } else {
            y += Rs_Px(6);
        }
        MoveWindow(g_kit.classicHead, in.left, y, iw, lh, TRUE);
        y += lh + Rs_Px(4);
        MoveWindow(g_kit.classicEdit, in.left, y, iw - Rs_Px(128), Rs_Px(28), TRUE);
        MoveWindow(g_kit.classicBtn, in.right - Rs_Px(116), y - Rs_Px(2), Rs_Px(116), bh, TRUE);
        y += Rs_Px(34);
        MoveWindow(g_kit.classicNote, in.left, y, iw, Rs_Px(36), TRUE);
        break;
    case 3:
        for (i = 0; i < KIT_PHASES; i++) {
            MoveWindow(g_kit.phaseLbl[i], in.left, y, iw, lh, TRUE);
            y += lh + Rs_Px(4);
        }
        MoveWindow(g_kit.progress, in.left, y, iw, lh, TRUE);
        y += lh + gap;
        MoveWindow(g_kit.detailsBox, in.left, y, Rs_Px(200), Rs_Px(24), TRUE);
        y += Rs_Px(32);
        {
            // The notice at the bottom of the card, right above the bar with
            // "Generate demo files"; the findings take the room between.
            int nh = Kit_TextHeight(g_kit.notice, iw), ntop, mh;
            mh = in.bottom - nh - gap - y;
            if (mh < Rs_Px(60))
                mh = Rs_Px(60);
            ntop = y + mh + gap;
            MoveWindow(g_kit.msgs, in.left, y, iw, mh, TRUE);
            MoveWindow(g_kit.details, in.left, y, iw, mh, TRUE);
            MoveWindow(g_kit.notice, in.left, ntop, iw, nh, TRUE);
        }
        break;
    case 4: {
        int fh = Kit_TextHeight(g_kit.files, iw), ow = Rs_Px(156), wh;
        MoveWindow(g_kit.files, in.left, y, iw, fh, TRUE);
        y += fh + gap;
        // One row per way made: what only it has, "Open in Blender" at its end.
        for (i = 0; i < RS_ROUTES; i++) {
            if (!Kit_WayOn(g_kit.runWays, i))
                continue;
            wh = Kit_TextHeight(g_kit.wayFiles[i], iw - ow - gap);
            if (wh < bh)
                wh = bh;
            MoveWindow(g_kit.wayFiles[i], in.left, y, iw - ow - gap, wh, TRUE);
            MoveWindow(g_kit.wayOpen[i], in.right - ow, y, ow, bh, TRUE);
            y += wh + Rs_Px(6);
        }
        y += gap;
        MoveWindow(g_kit.nextText, in.left, y, iw, Kit_TextHeight(g_kit.nextText, iw), TRUE);
        break;
    }
    }
    // The bar: Back on the left; the main action on the right, beside it the others.
    MoveWindow(g_kit.back, left, barTop, Rs_Px(104), bh, TRUE);
    x = right - Rs_Px(240);
    MoveWindow(g_kit.main, x, barTop, Rs_Px(240), bh, TRUE);
    MoveWindow(g_kit.cancel, x - gap - Rs_Px(104), barTop, Rs_Px(104), bh, TRUE);
    x -= gap;
    if (g_kit.step == 4) {
        // As many as there are, as wide as the room between Back and the main action allows.
        HWND row[4] = { g_kit.toChar, g_kit.testDemo, g_kit.folder, g_kit.guide };
        int n = g_kit.phase[KIT_PH_DEMO] == KIT_ST_OK ? 4 : 3;
        int rw = (x - (left + Rs_Px(104) + gap) - (n - 1) * gap) / n;
        if (rw > Rs_Px(176))
            rw = Rs_Px(176);
        for (i = 0; i < 4; i++) {
            if (i == 1 && n == 3)
                continue;
            x -= rw;
            MoveWindow(row[i], x, barTop, rw, bh, TRUE);
            x -= gap;
        }
    } else {
        MoveWindow(g_kit.guide, x - bw, barTop, bw, bh, TRUE);
    }
}

static void Kit_GoStep(HWND page, int step)
{
    (void)page;
    if (step < 0 || step >= KIT_STEPS)
        return;
    g_kit.step = step;
    if (step >= 1) {
        Kit_CheckGame();
        if (g_kit.blender == KIT_BL_UNKNOWN)
            Kit_CheckBlender(g_kit.page);
    }
    if (step >= 2)
        Kit_CheckOut();
    Kit_Update();
    Kit_Relayout();
}

static void Kit_SetOut(const wchar_t *path)
{
    g_kit.quiet = 1;
    Rs_SetText(g_kit.outEdit, path);
    g_kit.quiet = 0;
    Rs_ConfigSet(L"kit.out", path);
    Kit_CheckGame();
    Kit_CheckOut();
}

static LRESULT Kit_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam), code = HIWORD(wParam);
    wchar_t path[KIT_PATH];
    (void)lParam;
    if (id == KIT_ID_CLASSIC_EDIT && code == EN_CHANGE && !g_kit.quiet) {
        Kit_ClassicRead();
        Rs_ConfigSet(L"kit.classic", g_kit.classic);
        return 0;
    }
    if (id == KIT_ID_OUT_EDIT && code == EN_CHANGE && !g_kit.quiet) {
        wchar_t *t = Rs_GetText(g_kit.outEdit);
        Rs_ConfigSet(L"kit.out", t);
        Rs_Free(t);
        Kit_CheckOut();
        Kit_Update();
        return 0;
    }
    if (id == KIT_ID_WAYS && code == CBN_SELCHANGE) {
        int sel = (int)SendMessageW(g_kit.ways, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel <= RS_ROUTES)
            Rs_KitChooseWay(sel == 0 ? RS_KIT_WAYS_ALL : sel - 1);
        return 0;
    }
    if (code != BN_CLICKED)
        return 0;
    if (id >= KIT_ID_WAY_OPEN && id < KIT_ID_WAY_OPEN + RS_ROUTES) {
        Kit_OpenTemplate(id - KIT_ID_WAY_OPEN);
        return 0;
    }
    switch (id) {
    case KIT_ID_MAIN:
        if (g_kit.step < 3)
            Kit_GoStep(page, g_kit.step + 1);
        else if (g_kit.step == 3)
            Kit_Generate(page);
        else if (Kit_OneWay() >= 0)
            Kit_OpenTemplate(Kit_OneWay());
        else
            Kit_OpenFolder();
        Kit_Update();
        break;
    case KIT_ID_BACK:
        if (g_kit.step == 0)
            Rs_ShowHost(page);
        else
            Kit_GoStep(page, g_kit.step - 1);
        break;
    case KIT_ID_CANCEL:
        if (g_kit.job) {
            g_kit.cancelled = 1;
            Rs_JobCancel(g_kit.job);
        }
        break;
    case KIT_ID_GAME_BTN:
        if (Rs_BrowseFolder(Rs_MainWindow(), L"The folder of your game (with the folder assets)", g_kit.gameBase, path,
                            KIT_PATH)) {
            Rs_ConfigSet(L"kit.game", path);
            Kit_CheckGame();
            Kit_Update();
        }
        break;
    case KIT_ID_BL_BTN:
        if (Rs_BrowseOpenFile(Rs_MainWindow(), L"blender.exe", L"Blender\0blender.exe\0Programs\0*.exe\0\0",
                              g_kit.blenderPath, path, KIT_PATH)) {
            Rs_ConfigSet(L"kit.blender", path);
            g_kit.blender = KIT_BL_UNKNOWN;
            Kit_CheckBlender(page);
            Kit_Update();
        }
        break;
    case KIT_ID_OUT_BTN:
        if (Rs_BrowseFolder(Rs_MainWindow(), L"The folder of the kit", g_kit.outPath, path, KIT_PATH)) {
            Kit_SetOut(path);
            Kit_Update();
        }
        break;
    case KIT_ID_DEMO:
        g_kit.demo = Kit_Checked(g_kit.demoBox);
        break;
    case KIT_ID_CLASSIC_BTN:
        if (Rs_BrowseOpenFile(Rs_MainWindow(), L"An old character", L"Characters (*.rldchar)\0*.rldchar\0\0",
                              g_kit.classic, path, KIT_PATH)) {
            Kit_SetClassic(path);
            Kit_Update();
        }
        break;
    case KIT_ID_DETAILS_BOX:
        Kit_Update();
        break;
    case KIT_ID_GUIDE:
        Kit_OpenGuide();
        break;
    case KIT_ID_FOLDER:
        Kit_OpenFolder();
        break;
    case KIT_ID_TEST_DEMO:
        Kit_TestDemo();
        break;
    case KIT_ID_TO_CHAR:
        Rs_ShowPage(RS_PAGE_CHAR);
        break;
    }
    return 0;
}

static LRESULT Kit_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    switch (msg) {
    case RS_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        Kit_JobLine((int)wParam, line);
        Rs_Free(line);
        *handled = 1;
        return 0;
    }
    case RS_WM_JOB_PROGRESS: {
        struct RsJobProgress *p = (struct RsJobProgress *)lParam;
        if ((int)wParam == g_kit.job && p->total) {
            int ph = g_kit.jobKind == KIT_JOB_EXTRACT ? 0 : g_kit.jobKind == KIT_JOB_BLENDER ? 1 : 2;
            swprintf(g_kit.progressText, KIT_TEXT, L"%ls... %lu of %lu", g_phaseTitles[ph], p->done, p->total);
            Kit_SetLabel(g_kit.progress, g_kit.progressText, RS_COL_MUTED);
        }
        Rs_Free(p);
        *handled = 1;
        return 0;
    }
    case RS_WM_JOB_DONE:
        Kit_JobDone(page, (int)wParam, (int)lParam);
        *handled = 1;
        return 0;
    case RS_WM_PAGE_SHOWN:
        // What the kit needs, checked when the page is opened (not at the
        // start of Reload Studio: Blender is only asked when someone looks).
        if (!g_kit.job) {
            Kit_CheckGame();
            if (g_kit.blender == KIT_BL_UNKNOWN)
                Kit_CheckBlender(page);
            Kit_CheckOut();
        }
        Kit_Update();
        return 0;
    case RS_WM_QUERY_CLOSE:
        if (g_kit.job && !Rs_AskYesNo(Rs_MainWindow(), L"Creator Kit", L"The kit is being made. Stop it and close?"))
            {
                *handled = 1;
                return 0;
            }
        if (g_kit.job) {
            g_kit.cancelled = 1;
            Rs_JobCancel(g_kit.job);
            Kit_RemoveTree(g_kit.work);
        }
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Automation
// ---------------------------------------------------------------------------

static int Kit_OnOff(const wchar_t *arg)
{
    if (wcscmp(arg, L"on") == 0)
        return 1;
    if (wcscmp(arg, L"off") == 0)
        return 0;
    return -1;
}

static void Kit_ReportLine(FILE *f, const wchar_t *fmt, ...)
{
    wchar_t line[KIT_PATH * 3];
    char *utf8;
    va_list ap;
    va_start(ap, fmt);
    vswprintf(line, KIT_PATH * 3, fmt, ap);
    va_end(ap);
    utf8 = Rs_ToUtf8(line);
    fputs(utf8, f);
    fputs("\n", f);
    Rs_Free(utf8);
}

static void Kit_ReportFiles(FILE *f, const wchar_t *dir, const wchar_t *prefix)
{
    wchar_t pattern[KIT_PATH], name[KIT_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    Rs_PathJoin(pattern, KIT_PATH, dir, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // scripts and the way folders, one level down.
            int r, known = _wcsicmp(fd.cFileName, L"scripts") == 0;
            for (r = 0; r < RS_ROUTES; r++)
                known |= _wcsicmp(fd.cFileName, g_wayWords[r]) == 0;
            if (!prefix[0] && known) {
                wchar_t sub[KIT_PATH], pre[64];
                Rs_PathJoin(sub, KIT_PATH, dir, fd.cFileName);
                swprintf(pre, 64, L"%ls\\", fd.cFileName);
                Kit_ReportFiles(f, sub, pre);
            }
            continue;
        }
        swprintf(name, KIT_PATH, L"%ls%ls", prefix, fd.cFileName);
        Kit_ReportLine(f, L"file\t%ls\t%llu", name,
                       ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int Kit_Report(const wchar_t *path)
{
    static const wchar_t *const games[] = { L"unknown", L"ok", L"missing-exe", L"missing-data" };
    static const wchar_t *const bls[] = { L"unknown", L"checking", L"ok", L"missing", L"version", L"failed" };
    static const wchar_t *const outs[] = { L"ok", L"empty", L"git", L"game", L"studio", L"file" };
    static const wchar_t *const results[] = { L"none", L"ok", L"failed", L"cancelled" };
    FILE *f;
    int i;
    if (!g_kit.job) {
        Kit_CheckGame();
        Kit_CheckOut();
    }
    f = _wfopen(path, L"wb");
    if (!f)
        return 0;
    Kit_ReportLine(f, L"step\t%d", g_kit.step);
    Kit_ReportLine(f, L"game\t%ls\t%ls\t%ls", games[g_kit.game], g_kit.gameBase, g_kit.gameSource);
    Kit_ReportLine(f, L"game-text\t%ls\t%ls", g_kit.gameText, g_kit.gameFixText);
    Kit_ReportLine(f, L"blender\t%ls\t%ls\t%ls", bls[g_kit.blender], g_kit.blenderPath, g_kit.blenderVersion);
    Kit_ReportLine(f, L"blender-text\t%ls\t%ls", g_kit.blText, g_kit.blFixText);
    Kit_ReportLine(f, L"out\t%ls\t%ls", outs[g_kit.out], g_kit.outPath);
    Kit_ReportLine(f, L"out-text\t%ls\t%ls", g_kit.outText, g_kit.outFixText);
    {
        wchar_t ways[64], *notice = Rs_GetText(g_kit.notice), *line;
        size_t k, n = 0;
        Kit_WaysList(g_kitWays, ways, 64);
        Kit_ReportLine(f, L"ways\t%ls\t%ls", g_kitWays == RS_KIT_WAYS_ALL ? L"all" : g_wayWords[g_kitWays], ways);
        Kit_ReportLine(f, L"demo\t%ls\t%ls", g_kit.demo ? L"on" : L"off",
                       Kit_DemoOffered() ? L"offered" : L"not offered");
        // The notice as the control holds it, every line end written as \n.
        line = Rs_Alloc((wcslen(notice) * 2 + 1) * sizeof(wchar_t));
        for (k = 0; notice[k]; k++) {
            if (notice[k] == L'\r')
                continue;
            if (notice[k] == L'\n') {
                line[n++] = L'\\';
                line[n++] = L'n';
            } else {
                line[n++] = notice[k];
            }
        }
        line[n] = 0;
        fputs("notice\t", f);
        Kit_ReportLine(f, L"%ls\t%ls", (GetWindowLongPtrW(g_kit.notice, GWL_STYLE) & WS_VISIBLE) ? L"shown" : L"hidden", line);
        Rs_Free(line);
        Rs_Free(notice);
    }
    Kit_ClassicRead();
    Kit_ReportLine(f, L"classic\t%ls", g_kit.classic);
    for (i = 0; i < KIT_PHASES; i++)
        Kit_ReportLine(f, L"phase\t%ls\t%ls", g_phaseWords[i], g_stateWords[g_kit.phase[i]]);
    Kit_ReportLine(f, L"result\t%ls", results[g_kit.result]);
    for (i = 0; i < g_kit.msgCount; i++)
        Kit_ReportLine(f, L"msg\t%ls\t%ls\t%ls", g_sevWords[g_kit.msg[i].severity], g_kit.msg[i].text, g_kit.msg[i].fix);
    if (g_kit.out == KIT_OUT_OK && Rs_DirExists(g_kit.outPath))
        Kit_ReportFiles(f, g_kit.outPath, L"");
    fclose(f);
    return 1;
}

static int Kit_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    wchar_t path[KIT_PATH];
    if (wcscmp(verb, L"step") == 0) {
        int n = _wtoi(arg);
        if (!arg[0] || n < 0 || n >= KIT_STEPS) {
            Rs_AutoLog(L"  step wants 0 to 4");
            return RS_AUTO_FAIL;
        }
        Kit_GoStep(page, n);
        return Kit_Busy(page) ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"back") == 0 && g_kit.step == 0 && Rs_ModuleHost(Rs_PageIdOf(page)) >= 0) {
        if (g_kit.job) {
            Rs_AutoLog(L"  back: the kit is being made - Cancel first");
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  back: to Character, Build a character");
        Rs_ShowHost(page);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"next") == 0 || wcscmp(verb, L"back") == 0) {
        Kit_GoStep(page, g_kit.step + (verb[0] == L'n' ? 1 : -1));
        return Kit_Busy(page) ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"game") == 0) {
        Kit_Full(arg, path, KIT_PATH);
        Rs_ConfigSet(L"kit.game", path);
        Kit_CheckGame();
        Kit_CheckOut();
        Kit_Update();
        Rs_AutoLog(L"  game: %ls", g_kit.gameText);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"blender") == 0) {
        Kit_Full(arg, path, KIT_PATH);
        Rs_ConfigSet(L"kit.blender", path);
        g_kit.blender = KIT_BL_UNKNOWN;
        Kit_CheckBlender(page);
        Kit_Update();
        return Kit_Busy(page) ? RS_AUTO_WAIT : RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"out") == 0) {
        Kit_Full(arg, path, KIT_PATH);
        Kit_SetOut(path);
        Kit_Update();
        Rs_AutoLog(L"  out: %ls", g_kit.outText);
        return RS_AUTO_DONE;
    }
    // "classic <file.rldchar>" or "classic none": Start from an old character.
    if (wcscmp(verb, L"classic") == 0) {
        if (!arg[0]) {
            Rs_AutoLog(L"  classic wants a .rldchar or none");
            return RS_AUTO_FAIL;
        }
        if (wcscmp(arg, L"none") == 0)
            path[0] = 0;
        else
            Kit_Full(arg, path, KIT_PATH);
        Kit_SetClassic(path);
        Kit_Update();
        Rs_AutoLog(L"  classic: %ls", path[0] ? path : L"none");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"demo") == 0) {
        int on = Kit_OnOff(arg);
        if (on < 0) {
            Rs_AutoLog(L"  demo wants on or off");
            return RS_AUTO_FAIL;
        }
        SendMessageW(g_kit.demoBox, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        g_kit.demo = on;
        return RS_AUTO_DONE;
    }
    // "ways all|vanilla|lowpoly|hires": the choice of step 2.
    if (wcscmp(verb, L"ways") == 0) {
        int r, way = wcscmp(arg, L"all") == 0 ? RS_KIT_WAYS_ALL : -2;
        for (r = 0; r < RS_ROUTES; r++)
            if (wcscmp(arg, g_wayWords[r]) == 0)
                way = r;
        if (way == -2) {
            Rs_AutoLog(L"  ways wants all, vanilla, lowpoly or hires");
            return RS_AUTO_FAIL;
        }
        if (g_kit.job) {
            Rs_AutoLog(L"  ways: the kit is being made - Cancel first");
            return RS_AUTO_FAIL;
        }
        Rs_KitChooseWay(way);
        Rs_AutoLog(L"  ways: %ls", way == RS_KIT_WAYS_ALL ? L"All three ways" : g_wayNames[way]);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"generate") == 0) {
        Kit_CheckGame();
        Kit_CheckOut();
        if (g_kit.game != KIT_GAME_OK || g_kit.out != KIT_OUT_OK) {
            g_kit.step = 3;
            Kit_Generate(page);         // says why, starts nothing
            Kit_Update();
            Kit_Relayout();
            Rs_AutoLog(L"  generate did not start");
            return RS_AUTO_FAIL;
        }
        if (g_kit.blender == KIT_BL_UNKNOWN)
            Kit_CheckBlender(page);
        if (g_kit.versionJob) {
            g_kit.step = 3;
            g_kit.generateAfterCheck = 1;
            Kit_Update();
            Kit_Relayout();
            return RS_AUTO_WAIT;
        }
        g_kit.step = 3;
        if (!Kit_Generate(page)) {
            Kit_Update();
            Kit_Relayout();
            Rs_AutoLog(L"  generate did not start");
            return RS_AUTO_FAIL;
        }
        return RS_AUTO_WAIT;
    }
    if (wcscmp(verb, L"cancel") == 0) {
        if (!g_kit.job) {
            Rs_AutoLog(L"  cancel: nothing runs");
            return RS_AUTO_FAIL;
        }
        g_kit.cancelled = 1;
        Rs_JobCancel(g_kit.job);
        return RS_AUTO_WAIT;
    }
    if (wcscmp(verb, L"open") == 0) {
        if (wcscmp(arg, L"guide") == 0)
            Kit_OpenGuide();
        else if (wcsncmp(arg, L"template", 8) == 0 && (arg[8] == 0 || arg[8] == L' ')) {
            // "open template <way>", or "open template" for the one way made.
            const wchar_t *word = arg + 8;
            int r, way = -1;
            while (*word == L' ')
                word++;
            for (r = 0; r < RS_ROUTES; r++)
                if (wcscmp(word, g_wayWords[r]) == 0)
                    way = r;
            if (!word[0])
                way = Kit_OneWay();
            if (way < 0) {
                Rs_AutoLog(L"  open template wants vanilla, lowpoly or hires%ls",
                           word[0] ? L"" : L" (more than one way was made)");
                return RS_AUTO_FAIL;
            }
            Kit_OpenTemplate(way);
        } else if (wcscmp(arg, L"folder") == 0)
            Kit_OpenFolder();
        else {
            Rs_AutoLog(L"  open guide, open template [<way>] or open folder");
            return RS_AUTO_FAIL;
        }
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"test-demo") == 0) {
        wchar_t demo[KIT_PATH], why[KIT_TEXT];
        Kit_WayPath(demo, KIT_PATH, g_kit.outPath, RS_ROUTE_HIRES, KIT_DEMO);
        if (!Rs_TestCharacter(demo, RS_ROUTE_HIRES, why, KIT_TEXT)) {
            Rs_AutoLog(L"  test-demo refused: %ls", why);
            return RS_AUTO_FAIL;
        }
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"report") == 0) {
        if (!arg[0] || !Kit_Report(arg)) {
            Rs_AutoLog(L"  report wants a file it can write");
            return RS_AUTO_FAIL;
        }
        return RS_AUTO_DONE;
    }
    return RS_AUTO_UNKNOWN;
}

const struct RsPageDef g_rsKitPage = {
    L"Creator Kit",
    L"Creator Kit",
    L"A Blender template for a driver of your own, made from your game files on this PC.",
    Kit_Create,
    Kit_Layout,
    Kit_Command,
    NULL,
    Kit_Message,
    Kit_Automate,
    Kit_Busy,
    KIT_PAGE_MIN_W,
    KIT_PAGE_MIN_H
};
