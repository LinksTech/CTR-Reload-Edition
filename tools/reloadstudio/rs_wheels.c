// rs_wheels.c - the card "Wheels" of the page Character (preview feature)
//
// Wheels of one's own instead of the game's kart wheels: an OBJ (with its MTL
// file and one texture) or a PLY of one wheel, its size, and whether the
// preview turns and steers it. PREVIEW, open to everyone ("Preview feature"
// in the title line, with and without --enable-preview-features): the wheels
// belong to the native model, which is a preview itself.
//
// TWO JOBS. The preview: choosing a model runs `rldpack char-wheel` (600 ms
// after the last change, like the page's own check; tools/rldpack_wheel.inc),
// whose preview file (RLDPW3: the wheel with its UVs and its texture in full
// size, mip levels as in the game; as large as the game's wheel) goes to the
// view (RsView_LoadWheelModel) and is deleted at once; the view draws it at
// the kart's four wheel points, the right ones mirrored, textured, by depth
// against the model. Size, turn and animation
// are view settings. Its messages (the budget: 1024 triangles, 2048 points, a
// texture of at most 1024 x 1024; never cut short) are the status line under
// the field: an error says why the wheel is not shown, a warning (a texture
// not found, an axle that is not X) follows the facts in the warning colour.
// Browse and Clear are clicks: char-wheel runs at once. A wheel exported
// again (its OBJ or PLY, its MTL, its texture; a texture put where rldpack
// looks for it) is read again when Reload Studio becomes the active program
// again or the page is shown again (CharWheels_FilesChanged): the card keeps
// a stamp (size and time of writing) of every file the last char-wheel read,
// like the page (rs_char.c, Char_StampFields). The export: the page
// (rs_char.c, Char_MakeArgs) passes
// `--wheel-model <file>` and, when it is not 100 %, `--wheel-size <percent>`
// to make-char while the native model is built and Show kart wheels is on
// (CharWheels_ModelPath, CharWheels_SizePercent); a change of either makes the
// page check again (CharWheels_ExportChanged). The axes of the card Import
// (--up, --forward) are the wheel's too: char-wheel gets them, and a change
// reads the wheel again; so does the Textures folder of the page (--textures,
// an OBJ body only, CharWheels_SetTextures), where make-char looks for the
// wheel's texture as well. make-char then writes WHLS
// version 2 beside the native model; the classic model keeps the game's
// wheels as its fallback.
//
// "Show kart wheels" (tab Model, rs_char.c) stays the master:
//   a wheel model, kart wheels on    your wheels on the native model; the
//                                    classic model drives with the game's wheels
//   no wheel model, kart wheels on   as before: the game's wheels (the classic
//                                    model only, or the native-wheels rule)
//   kart wheels off                  no wheels at all; a wheel model is not used
// The line under the options of this card says which, with what is missing
// (CharWheels_PageState, from the page after every change); while the
// preview shows wheels that are not built, it says so. Nothing of the card is
// stored in the settings.
//
// Automation verbs:
//   wheel-model <obj|ply|none>  (also "wheel") choose the wheel; waits for char-wheel
//   wheel-size <50..200>        percent of the game's wheel
//   wheel-turn <spin> <steer>   fixed angles in whole degrees; stops the animation
//   wheel-anim on|off           the animation of the preview (a timer of the view)
//   wheel-bench <pictures>      the preview drawn that often, one animation step
//                               each, timed (RsView_WheelBench) - into the log

#include "rs_wheels.h"
#include "rs_view.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Controls (300-339 belong to this card on the page Character)
#define WH_ID_HINT          300
#define WH_ID_NOTE          301
#define WH_ID_MODEL_LABEL   302
#define WH_ID_MODEL         303
#define WH_ID_BROWSE        304
#define WH_ID_CLEAR         305
#define WH_ID_SIZE_LABEL    306
#define WH_ID_SIZE          307
#define WH_ID_SIZE_VALUE    308
#define WH_ID_ANIMATE       309
#define WH_ID_WHEELS_NOTE   310
#define WH_ID_STATUS        311
#define WH_ID_FIRST         300
#define WH_ID_LAST          339

#define WH_TIMER_CHECK      300     // a timer of the page window, its own ID
#define WH_CHECK_DELAY      600
#define WH_VAL              1024
#define WH_SIZE_MIN         50
#define WH_SIZE_MAX         200
#define WH_SIZE_DEFAULT     100
#define WH_NOTE_LINES       3
#define WH_ENDED            8       // jobs ended by a newer input whose end is still to come
#define WH_BENCH_MAX        1000
#define WH_FILES            8       // files of a char-wheel kept for the stamp (the model, MTLs, textures)
#define WH_MISSING          4       // names of textures not found, kept for the notes
#define WH_STAMP_START      14695981039346656037ULL   // FNV-1a, as Char_StampFields

#define WH_TEXT_NOTE        L"Wheels of your own instead of the game's kart wheels, on the native model."
#define WH_TEXT_NONE        L"No wheel model chosen."
// The line under the options (Wh_WheelsText): what is built with the choices of the page.
#define WH_TEXT_OWN         L"Your wheels on the native model; its classic model keeps the game's wheels as the fallback."
#define WH_TEXT_OWN_OFF     L"Not used: Show kart wheels is off (tab Model) - no wheels at all. Turn it on for your wheels."
#define WH_TEXT_OWN_PLY     L"Not used: your wheels need the native model, which a PLY model has not (export the model as OBJ)."
#define WH_TEXT_OWN_TICK    L"Not used yet: your wheels need the native model - tick Native model (Extras, Import)."
// Appended to the two above while the preview draws the wheel all the same.
#define WH_TEXT_PREVIEW_ONLY L" The preview shows them, the character will not have them."
#define WH_TEXT_GAME        L"No wheel model: the kart keeps the game's wheels."
#define WH_TEXT_GAME_OFF    L"Show kart wheels is off (tab Model): no wheels at all."

static struct {
    HWND page, view;
    HWND hint, note, modelLabel, model, browse, clear, sizeLabel, size, sizeValue, animate, wheelsNote, status;
    int created;
    int applying;               // the card sets a field itself: no check from its EN_CHANGE
    int timer;                  // the delayed check is pending
    int jobId, jobSeq, seq;
    int endedId[WH_ENDED], endedSeq[WH_ENDED];   // ended by a newer input: their lines are dropped,
                                // their file deleted when they are gone
    int sizeNow, spin, steer, anim;
    int loaded;                 // a wheel model is in the view
    int exportChanged;          // CharWheels_ExportChanged: 1 typed, 2 clicked, 0 nothing
    int obj, kartWheels, nativeOn, passed;   // CharWheels_PageState
    int upZ, backwards;         // the page's axes (card Import): the wheel's as well
    wchar_t textures[WH_VAL];   // the page's Textures folder as make-char gets it ("" = none)
    COLORREF statusColor;
    wchar_t checked[WH_VAL];    // the model of the last char-wheel
    wchar_t statusText[1024];
    // the files the last char-wheel read (CharWheels_FilesChanged): their stamp
    // when it ended; the folders where a texture not found is looked for
    unsigned long long stamp;
    int stampKnown;
    int generation;             // CharWheels_Generation: a file written since
    int fileCount, folderCount;
    wchar_t files[WH_FILES][WH_VAL], folders[WH_FILES][WH_VAL];
    // the textures of the last char-wheel that are not there (CharWheels_MissingTextures)
    int missingCount;
    wchar_t missing[WH_MISSING][96];
    // the running job
    int previewOk, triangles, points, texW, texH;
    int warnings;               // warnings of the job
    wchar_t across[16], width[16], firstError[400], firstWarning[400], meshFormat[8];
    int jobFileCount, jobFolderCount, jobMissingCount;
    wchar_t jobFiles[WH_FILES][WH_VAL], jobFolders[WH_FILES][WH_VAL], jobMissing[WH_MISSING][96];
} g_wh;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void Wh_Copy(wchar_t *out, int cap, const wchar_t *in)
{
    if (cap <= 0)
        return;
    wcsncpy(out, in ? in : L"", (size_t)cap - 1);
    out[cap - 1] = 0;
}

// Text of the field without quotation marks and spaces at the edges.
static void Wh_FieldPath(wchar_t *out, int cap)
{
    wchar_t *text = Rs_GetText(g_wh.model);
    const wchar_t *p = text ? text : L"";
    size_t n;

    while (*p == L' ' || *p == L'\t' || *p == L'"')
        p++;
    Wh_Copy(out, cap, p);
    n = wcslen(out);
    while (n > 0 && (out[n - 1] == L' ' || out[n - 1] == L'\t' || out[n - 1] == L'"'))
        out[--n] = 0;
    Rs_Free(text);
}

// Height of a wrapping label at this width: its lines, at least one, at most
// WH_NOTE_LINES.
// The compact layout of the page (rs_char.c, CharWheels_Layout): notes on one line, the
// whole text as the tooltip (Rs_LabelOneLine).
static int s_whCompact;

static int Wh_TextHeight(HWND label, int width)
{
    wchar_t *text = Rs_GetText(label);
    HDC dc = GetDC(label);
    HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    TEXTMETRICW tm;
    RECT rc;
    int h = Rs_Px(18);

    if (dc) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_SMALL));
        GetTextMetricsW(dc, &tm);
        rc.left = 0;
        rc.top = 0;
        rc.right = width;
        rc.bottom = 0;
        DrawTextW(dc, text ? text : L"", -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        h = rc.bottom;
        Rs_LabelOneLine(label, s_whCompact && tm.tmHeight > 0 && h > tm.tmHeight);
        if (s_whCompact && tm.tmHeight > 0 && h > tm.tmHeight)
            h = tm.tmHeight;
        if (tm.tmHeight > 0 && h > tm.tmHeight * WH_NOTE_LINES)
            h = tm.tmHeight * WH_NOTE_LINES;
        SelectObject(dc, old);
        ReleaseDC(label, dc);
    }
    Rs_Free(text);
    return h > Rs_Px(18) ? h : Rs_Px(18);
}

// The page laid out again (a note changed its number of lines).
static void Wh_Relayout(void)
{
    RECT rc;
    if (!g_wh.page)
        return;
    GetClientRect(g_wh.page, &rc);
    if (rc.right > 0 && rc.bottom > 0 && g_rsCharPage.layout) {
        g_rsCharPage.layout(g_wh.page, rc.right, rc.bottom);
        InvalidateRect(g_wh.page, NULL, TRUE);
    }
}

// A note's new text; the page is laid out again when its height changes
// (in the compact layout its one line and tooltip follow, Wh_TextHeight).
static void Wh_SetNote(HWND label, const wchar_t *text)
{
    RECT rc;
    wchar_t *old = Rs_GetText(label);
    const int same = old && wcscmp(old, text) == 0;
    Rs_Free(old);
    if (same)
        return;
    Rs_SetText(label, text);
    GetWindowRect(label, &rc);
    if (rc.right > rc.left && Wh_TextHeight(label, rc.right - rc.left) != rc.bottom - rc.top)
        Wh_Relayout();
}

static void Wh_Status(const wchar_t *text, COLORREF color)
{
    Wh_Copy(g_wh.statusText, 1024, text);
    g_wh.statusColor = color;
    Rs_SetTextColor(g_wh.status, color);
    Wh_SetNote(g_wh.status, text);
}

static void Wh_SizeShow(void)
{
    wchar_t text[16];
    swprintf(text, 16, L"%d %%", g_wh.sizeNow);
    Rs_SetText(g_wh.sizeValue, text);
}

static int Wh_IsChecked(HWND box)
{
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// Its own temporary file: <Rs_TempDir>\wheel-<process>-<number>.rldpw.
static void Wh_TempPath(wchar_t *out, int cap, int seq)
{
    wchar_t dir[WH_VAL];
    wchar_t name[64];

    Rs_TempDir(dir, WH_VAL);
    swprintf(name, 64, L"wheel-%lu-%d.rldpw", (unsigned long)GetCurrentProcessId(), seq);
    Rs_PathJoin(out, cap, dir, name);
}

static void Wh_TempDelete(int seq)
{
    wchar_t path[WH_VAL];
    if (seq <= 0)
        return;
    Wh_TempPath(path, WH_VAL, seq);
    DeleteFileW(path);
}

// ---------------------------------------------------------------------------
// Stamps of the files char-wheel read: a 64-bit FNV-1a hash over the path (in
// small letters), the size and the time of writing of each file - the same as
// the page's (rs_char.c, Char_StampFile, Char_StampFolder).
// ---------------------------------------------------------------------------

static unsigned long long Wh_StampBytes(unsigned long long h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static unsigned long long Wh_StampPath(unsigned long long h, const wchar_t *path)
{
    for (; *path; path++) {
        wchar_t c = towlower(*path);
        if (c == L'/')
            c = L'\\';
        h = Wh_StampBytes(h, &c, sizeof(c));
    }
    return Wh_StampBytes(h, L"", sizeof(wchar_t));
}

static unsigned long long Wh_StampFile(unsigned long long h, const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    h = Wh_StampPath(h, path);
    if (path[0] && GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) {
        h = Wh_StampBytes(h, &fa.nFileSizeHigh, sizeof(fa.nFileSizeHigh));
        h = Wh_StampBytes(h, &fa.nFileSizeLow, sizeof(fa.nFileSizeLow));
        h = Wh_StampBytes(h, &fa.ftLastWriteTime, sizeof(fa.ftLastWriteTime));
    } else {
        h = Wh_StampBytes(h, "none", 4);
    }
    return h;
}

// A folder: every file in it (not below it), in any order - where a texture
// that was not found may be put.
static unsigned long long Wh_StampFolder(unsigned long long h, const wchar_t *dir)
{
    wchar_t pattern[WH_VAL];
    WIN32_FIND_DATAW fd;
    HANDLE find;
    unsigned long long sum = 0, count = 0;

    h = Wh_StampPath(h, dir);
    if (!dir[0])
        return h;
    Rs_PathJoin(pattern, WH_VAL, dir, L"*");
    find = FindFirstFileW(pattern, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            unsigned long long one;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            one = Wh_StampPath(WH_STAMP_START, fd.cFileName);
            one = Wh_StampBytes(one, &fd.nFileSizeHigh, sizeof(fd.nFileSizeHigh));
            one = Wh_StampBytes(one, &fd.nFileSizeLow, sizeof(fd.nFileSizeLow));
            one = Wh_StampBytes(one, &fd.ftLastWriteTime, sizeof(fd.ftLastWriteTime));
            sum += one;
            count++;
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    h = Wh_StampBytes(h, &sum, sizeof(sum));
    return Wh_StampBytes(h, &count, sizeof(count));
}

// The files of the last char-wheel as they are now.
static unsigned long long Wh_StampNow(void)
{
    unsigned long long h = Wh_StampFile(WH_STAMP_START, g_wh.checked);
    int i;
    for (i = 0; i < g_wh.fileCount; i++)
        h = Wh_StampFile(h, g_wh.files[i]);
    for (i = 0; i < g_wh.folderCount; i++)
        h = Wh_StampFolder(h, g_wh.folders[i]);
    return h;
}

// A path into a list of the running job, once.
static void Wh_JobListAdd(wchar_t list[][WH_VAL], int *count, const wchar_t *path)
{
    int i;
    if (!path[0] || *count >= WH_FILES)
        return;
    for (i = 0; i < *count; i++)
        if (_wcsicmp(list[i], path) == 0)
            return;
    Wh_Copy(list[(*count)++], WH_VAL, path);
}

// The line under the options: what the page builds with the wheels.
// The view draws a wheel model loaded wherever the kart wheels are shown -
// also on a classic model, which is built without it (CharWheels_Shown).
static const wchar_t *Wh_WheelsText(void)
{
    static wchar_t text[256];
    wchar_t path[WH_VAL];
    Wh_FieldPath(path, WH_VAL);
    if (!path[0])
        return g_wh.kartWheels ? WH_TEXT_GAME : WH_TEXT_GAME_OFF;
    if (!g_wh.kartWheels)
        return WH_TEXT_OWN_OFF;
    if (g_wh.passed)
        return WH_TEXT_OWN;
    Wh_Copy(text, 256, !g_wh.obj ? WH_TEXT_OWN_PLY : WH_TEXT_OWN_TICK);
    if (g_wh.loaded)
        wcsncat(text, WH_TEXT_PREVIEW_ONLY, 255 - wcslen(text));
    return text;
}

static void Wh_WheelsNote(void)
{
    if (g_wh.created)
        Wh_SetNote(g_wh.wheelsNote, Wh_WheelsText());
}

// ---------------------------------------------------------------------------
// The check: rldpack char-wheel
// ---------------------------------------------------------------------------

// The wheel leaves the view: the game's wheels are drawn again.
static void Wh_Drop(void)
{
    if (g_wh.loaded)
        RsView_DropWheelModel(g_wh.view);
    g_wh.loaded = 0;
    Wh_WheelsNote();
}

// Starts char-wheel for the model in the field. 1 = started,
// 0 = nothing to check (empty field: the wheel is dropped),
// -1 = rldpack could not be started. A char-wheel still running is ended
// first; its file goes when it is gone.
static int Wh_Check(HWND page)
{
    wchar_t path[WH_VAL], preview[WH_VAL];
    const wchar_t *args[14];
    int n;

    if (g_wh.timer) {
        KillTimer(page, WH_TIMER_CHECK);
        g_wh.timer = 0;
    }
    if (g_wh.jobId) {
        int i;
        for (i = 0; i < WH_ENDED && g_wh.endedId[i]; i++)
            ;
        if (i == WH_ENDED) {
            Wh_TempDelete(g_wh.endedSeq[0]);
            memmove(&g_wh.endedId[0], &g_wh.endedId[1], sizeof(g_wh.endedId[0]) * (WH_ENDED - 1));
            memmove(&g_wh.endedSeq[0], &g_wh.endedSeq[1], sizeof(g_wh.endedSeq[0]) * (WH_ENDED - 1));
            i = WH_ENDED - 1;
        }
        Rs_KillJob(g_wh.jobId);
        g_wh.endedId[i] = g_wh.jobId;
        g_wh.endedSeq[i] = g_wh.jobSeq;
        g_wh.jobId = 0;
        g_wh.jobSeq = 0;
    }
    Wh_FieldPath(path, WH_VAL);
    Wh_Copy(g_wh.checked, WH_VAL, path);
    // The stamp is that of the run to come (Wh_JobDone); none while it runs.
    g_wh.stampKnown = 0;
    g_wh.fileCount = 0;
    g_wh.folderCount = 0;
    g_wh.missingCount = 0;
    if (!path[0]) {
        Wh_Drop();
        Wh_Status(WH_TEXT_NONE, RS_COL_MUTED);
        return 0;
    }

    g_wh.jobSeq = ++g_wh.seq;
    Wh_TempPath(preview, WH_VAL, g_wh.jobSeq);
    DeleteFileW(preview);
    args[0] = L"char-wheel";
    args[1] = L"--machine";
    args[2] = L"--model";
    args[3] = path;
    args[4] = L"--preview";
    args[5] = preview;
    n = 6;
    // The axes of the body are the wheel's (make-char turns both by them).
    if (g_wh.upZ) {
        args[n++] = L"--up";
        args[n++] = L"z";
    }
    if (g_wh.backwards) {
        args[n++] = L"--forward";
        args[n++] = L"-z";
    }
    // The Textures folder of the page, where make-char looks for the wheel's
    // texture too (CharWheels_SetTextures).
    if (g_wh.textures[0]) {
        args[n++] = L"--textures";
        args[n++] = g_wh.textures;
    }
    g_wh.previewOk = 0;
    g_wh.triangles = -1;
    g_wh.points = -1;
    g_wh.texW = 0;
    g_wh.texH = 0;
    g_wh.across[0] = 0;
    g_wh.width[0] = 0;
    g_wh.meshFormat[0] = 0;
    g_wh.firstError[0] = 0;
    g_wh.firstWarning[0] = 0;
    g_wh.warnings = 0;
    g_wh.jobFileCount = 0;
    g_wh.jobFolderCount = 0;
    g_wh.jobMissingCount = 0;
    g_wh.jobId = Rs_RunRldpack(page, args, n);
    if (!g_wh.jobId) {
        g_wh.jobSeq = 0;
        Wh_Status(L"rldpack could not be started.", RS_COL_ERROR);
        return -1;
    }
    Wh_Status(L"Reading the wheel model...", RS_COL_MUTED);
    return 1;
}

static void Wh_Schedule(HWND page)
{
    if (g_wh.applying)
        return;
    KillTimer(page, WH_TIMER_CHECK);
    g_wh.timer = SetTimer(page, WH_TIMER_CHECK, WH_CHECK_DELAY, NULL) != 0;
}

static const wchar_t *Wh_Field(wchar_t **f, int n, int i)
{
    return i < n && f[i] ? f[i] : L"";
}

static void Wh_ParseLine(wchar_t *line)
{
    wchar_t *f[16];
    int n = Rs_SplitMachine(line, f, 16);

    if (n <= 0)
        return;
    if (wcscmp(f[0], L"char") == 0 && wcscmp(Wh_Field(f, n, 1), L"wheel") == 0) {
        g_wh.triangles = _wtoi(Wh_Field(f, n, 2));
        Wh_Copy(g_wh.across, 16, Wh_Field(f, n, 3));
        Wh_Copy(g_wh.width, 16, Wh_Field(f, n, 4));
    } else if (wcscmp(f[0], L"char") == 0 && wcscmp(Wh_Field(f, n, 1), L"wheel-mesh") == 0) {
        Wh_Copy(g_wh.meshFormat, 8, Wh_Field(f, n, 2));
        g_wh.points = _wtoi(Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"char") == 0 && wcscmp(Wh_Field(f, n, 1), L"wheel-texture") == 0) {
        g_wh.texW = _wtoi(Wh_Field(f, n, 2));
        g_wh.texH = _wtoi(Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"file") == 0 && wcscmp(Wh_Field(f, n, 1), L"preview") == 0) {
        g_wh.previewOk = wcscmp(Wh_Field(f, n, 2), L"ok") == 0;
    } else if (wcscmp(f[0], L"file") == 0 && wcscmp(Wh_Field(f, n, 1), L"mtl") == 0) {
        // @file mtl <state> <path> <bytes>: a file of the stamp
        Wh_JobListAdd(g_wh.jobFiles, &g_wh.jobFileCount, Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"model") == 0 && wcscmp(Wh_Field(f, n, 1), L"texture") == 0) {
        // @model texture <state> <material> <path> <sha256>: a file of the
        // stamp; one not found also its folder and that of the model, where
        // it may be put
        const wchar_t *path = Wh_Field(f, n, 4);
        Wh_JobListAdd(g_wh.jobFiles, &g_wh.jobFileCount, path);
        if (wcscmp(Wh_Field(f, n, 2), L"ok") != 0 && path[0]) {
            wchar_t dir[WH_VAL];
            int i;
            Rs_PathDir(dir, WH_VAL, path);
            Wh_JobListAdd(g_wh.jobFolders, &g_wh.jobFolderCount, dir);
            Rs_PathDir(dir, WH_VAL, g_wh.checked);
            Wh_JobListAdd(g_wh.jobFolders, &g_wh.jobFolderCount, dir);
            Wh_JobListAdd(g_wh.jobFolders, &g_wh.jobFolderCount, g_wh.textures);
            for (i = 0; i < g_wh.jobMissingCount && _wcsicmp(g_wh.jobMissing[i], Rs_PathName(path)) != 0; i++)
                ;
            if (i == g_wh.jobMissingCount && i < WH_MISSING)
                Wh_Copy(g_wh.jobMissing[g_wh.jobMissingCount++], 96, Rs_PathName(path));
        }
    } else if (wcscmp(f[0], L"msg") == 0 && wcscmp(Wh_Field(f, n, 1), L"error") == 0 && !g_wh.firstError[0]) {
        Wh_Copy(g_wh.firstError, 400, Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"msg") == 0 && wcscmp(Wh_Field(f, n, 1), L"warning") == 0) {
        // A texture not found is said by the facts (Wh_JobDone, its name);
        // any other warning (wheel-off-axis) in rldpack's words.
        g_wh.warnings++;
        if (!g_wh.firstWarning[0] && wcscmp(Wh_Field(f, n, 2), L"tex-missing") != 0 &&
            wcscmp(Wh_Field(f, n, 2), L"wheel-texture") != 0)
            Wh_Copy(g_wh.firstWarning, 400, Wh_Field(f, n, 3));
    }
}

static void Wh_JobDone(HWND page, int code)
{
    wchar_t preview[WH_VAL], text[1024], tex[48];
    int seq = g_wh.jobSeq;

    g_wh.jobId = 0;
    g_wh.jobSeq = 0;
    (void)page;
    // The files it read, and their stamp now (CharWheels_FilesChanged).
    memcpy(g_wh.files, g_wh.jobFiles, sizeof(g_wh.files));
    memcpy(g_wh.folders, g_wh.jobFolders, sizeof(g_wh.folders));
    memcpy(g_wh.missing, g_wh.jobMissing, sizeof(g_wh.missing));
    g_wh.fileCount = g_wh.jobFileCount;
    g_wh.folderCount = g_wh.jobFolderCount;
    g_wh.missingCount = g_wh.jobMissingCount;
    g_wh.stamp = Wh_StampNow();
    g_wh.stampKnown = 1;
    Wh_TempPath(preview, WH_VAL, seq);
    if (code == 0 && g_wh.previewOk && RsView_LoadWheelModel(g_wh.view, preview)) {
        wchar_t warn[440];
        g_wh.loaded = 1;
        RsView_SetWheelScale(g_wh.view, g_wh.sizeNow);
        RsView_SetWheelTurn(g_wh.view, g_wh.spin, g_wh.steer);
        RsView_SetWheelAnimation(g_wh.view, g_wh.anim);
        if (g_wh.texW > 0)
            swprintf(tex, 48, L"texture %d x %d", g_wh.texW, g_wh.texH);
        else if (g_wh.missingCount > 0)
            swprintf(tex, 48, L"texture not found");
        else
            swprintf(tex, 48, L"no texture");
        // What a warning says first: the texture not found (by its name), else
        // rldpack's first other warning (an axle that is not X).
        warn[0] = 0;
        if (g_wh.missingCount > 0)
            swprintf(warn, 440, L" Warning: %ls was not found - the wheel is drawn in its colour. Put it next to the MTL file.",
                     g_wh.missing[0]);
        else if (g_wh.firstWarning[0])
            swprintf(warn, 440, L" Warning: %ls", g_wh.firstWarning);
        swprintf(text, 1024, L"%d triangles, %d vertices, %ls - in the preview at the four wheel points, the right ones mirrored.%ls",
                 g_wh.triangles, g_wh.points, tex, warn);
        Wh_Status(text, warn[0] ? RS_COL_WARNING : RS_COL_TEXT);
        Wh_WheelsNote();
    } else {
        Wh_Drop();
        if (g_wh.firstError[0])
            swprintf(text, 1024, L"Not shown: %ls", g_wh.firstError);
        else if (code == 0)
            swprintf(text, 1024, L"Not shown: the preview file could not be read.");
        else
            swprintf(text, 1024, L"Not shown: rldpack ended with code %d.", code);
        Wh_Status(text, RS_COL_ERROR);
    }
    Wh_TempDelete(seq);
}

// ---------------------------------------------------------------------------
// The card
// ---------------------------------------------------------------------------

static void Wh_Tips(void)
{
    const wchar_t *model = L"An OBJ (with its MTL file and one texture of at most 1024 x 1024) or a PLY of one wheel: axle along X, "
                           L"outer side toward +X, in the axes of the model (Up and Forward on the card Import), like the body; "
                           L"at most 1024 triangles - never reduced. Built with the native model; the right wheels are its "
                           L"mirror image.";
    const wchar_t *size = L"The size of your wheels, 100 % = the game's wheel; the bottom stays on the ground. In the preview and "
                          L"in the character (make-char --wheel-size).";
    const wchar_t *anim = L"Turns and steers the wheels in the preview.";

    Rs_SetTip(g_wh.hint, L"Preview: wheels of your own on the native model; the game draws them with NATIVE DRIVERS set to "
                         L"PREVIEW (OPTIONS, GRAPHICS).");
    Rs_SetTip(g_wh.modelLabel, model);
    Rs_SetTip(g_wh.model, model);
    Rs_SetTip(g_wh.browse, model);
    Rs_SetTip(g_wh.clear, model);
    Rs_SetTip(g_wh.sizeLabel, size);
    Rs_SetTip(g_wh.size, size);
    Rs_SetTip(g_wh.sizeValue, size);
    Rs_SetTip(g_wh.animate, anim);
}

void CharWheels_Create(HWND page, HWND view)
{
    memset(&g_wh, 0, sizeof(g_wh));
    g_wh.page = page;
    g_wh.view = view;
    g_wh.sizeNow = WH_SIZE_DEFAULT;
    g_wh.kartWheels = 1;

    g_wh.hint = Rs_PreviewMark(page, WH_ID_HINT);
    // right-aligned in a box as wide as the longer of "Coming soon" and
    // "Preview feature" (CharWheels_Layout)
    SetWindowLongPtrW(g_wh.hint, GWL_STYLE, GetWindowLongPtrW(g_wh.hint, GWL_STYLE) | SS_RIGHT);
    g_wh.note = Rs_Label(page, WH_ID_NOTE, WH_TEXT_NOTE, RS_FONT_SMALL);
    g_wh.modelLabel = Rs_Label(page, WH_ID_MODEL_LABEL, L"Wheel model (OBJ/PLY)", RS_FONT_BOLD);
    g_wh.model = Rs_Edit(page, WH_ID_MODEL, L"", 0);
    SendMessageW(g_wh.model, EM_SETCUEBANNER, FALSE, (LPARAM)L"Optional - an OBJ or PLY of one wheel");
    g_wh.browse = Rs_Button(page, WH_ID_BROWSE, L"Browse...");
    g_wh.clear = Rs_Button(page, WH_ID_CLEAR, L"Clear");
    g_wh.status = Rs_Label(page, WH_ID_STATUS, WH_TEXT_NONE, RS_FONT_SMALL);
    g_wh.statusColor = RS_COL_MUTED;
    Rs_SetTextColor(g_wh.status, g_wh.statusColor);
    Wh_Copy(g_wh.statusText, 1024, WH_TEXT_NONE);
    g_wh.sizeLabel = Rs_Label(page, WH_ID_SIZE_LABEL, L"Wheel size", RS_FONT_BOLD);
    g_wh.size = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 0, 0, 10, 10, page,
                                (HMENU)(INT_PTR)WH_ID_SIZE, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_wh.size, TBM_SETRANGE, FALSE, MAKELPARAM(WH_SIZE_MIN, WH_SIZE_MAX));
    SendMessageW(g_wh.size, TBM_SETLINESIZE, 0, 5);
    SendMessageW(g_wh.size, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(g_wh.size, TBM_SETPOS, TRUE, WH_SIZE_DEFAULT);
    g_wh.sizeValue = Rs_Label(page, WH_ID_SIZE_VALUE, L"", RS_FONT_BODY);
    Wh_SizeShow();
    g_wh.animate = Rs_Check(page, WH_ID_ANIMATE, L"Animate in the preview (spin and steer)");
    g_wh.wheelsNote = Rs_Label(page, WH_ID_WHEELS_NOTE, WH_TEXT_GAME, RS_FONT_SMALL);
    Rs_SetTextColor(g_wh.note, RS_COL_MUTED);
    Rs_SetTextColor(g_wh.wheelsNote, RS_COL_MUTED);
    Wh_Tips();
    g_wh.created = 1;
}

// Card "Wheels" from top between left and right, its fields beside a label
// column at least labelW wide (the page's). Returns the bottom of the card.
int CharWheels_Layout(HWND page, int left, int right, int top, int labelW, int compact)
{
    RECT card, in;
    int x, y, h, w, fieldW, width;
    const int browseW = Rs_Px(100), clearW = Rs_Px(72);
    wchar_t *text;

    s_whCompact = compact;
    if (!g_wh.created)
        return top;
    // Nothing is cut short: the label column holds this card's labels too.
    text = Rs_GetText(g_wh.modelLabel);
    w = Rs_TextWidth(g_wh.modelLabel, text) + Rs_Px(4);
    Rs_Free(text);
    if (w > labelW)
        labelW = w;
    text = Rs_GetText(g_wh.sizeLabel);
    w = Rs_TextWidth(g_wh.sizeLabel, text) + Rs_Px(4);
    Rs_Free(text);
    if (w > labelW)
        labelW = w;

    card.left = left;
    card.top = top;
    card.right = right;
    card.bottom = top;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    x = in.left + labelW + Rs_Px(8);
    fieldW = in.right - x;
    if (fieldW < Rs_Px(40))
        fieldW = Rs_Px(40);

    // The hint in the title line, right-aligned.
    w = Rs_TextWidth(g_wh.hint, L"Coming soon");
    h = Rs_TextWidth(g_wh.hint, L"Preview feature");
    w = (h > w ? h : w) + Rs_Px(4);
    MoveWindow(g_wh.hint, in.right - w, card.top + Rs_Px(16), w, Rs_Px(18), TRUE);

    y = in.top;
    h = Wh_TextHeight(g_wh.note, width);
    MoveWindow(g_wh.note, in.left, y, width, h, TRUE);
    y += h + Rs_Px(10);
    MoveWindow(g_wh.modelLabel, in.left, y + Rs_Px(6), labelW, Rs_Px(20), TRUE);
    w = fieldW - browseW - clearW - Rs_Px(16);
    MoveWindow(g_wh.model, x, y + Rs_Px(2), w > Rs_Px(24) ? w : Rs_Px(24), Rs_Px(28), TRUE);
    MoveWindow(g_wh.browse, in.right - browseW - clearW - Rs_Px(8), y, browseW, Rs_Px(32), TRUE);
    MoveWindow(g_wh.clear, in.right - clearW, y, clearW, Rs_Px(32), TRUE);
    y += Rs_Px(36);
    h = Wh_TextHeight(g_wh.status, fieldW);
    MoveWindow(g_wh.status, x, y, fieldW, h, TRUE);
    y += h + Rs_Px(10);
    MoveWindow(g_wh.sizeLabel, in.left, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(g_wh.size, x - Rs_Px(4), y, fieldW - Rs_Px(64), Rs_Px(30), TRUE);
    MoveWindow(g_wh.sizeValue, in.right - Rs_Px(60), y + Rs_Px(4), Rs_Px(60), Rs_Px(20), TRUE);
    y += Rs_Px(36);
    w = Rs_CheckBoxWidth(g_wh.animate);
    MoveWindow(g_wh.animate, x, y, w < fieldW ? w : fieldW, Rs_Px(24), TRUE);
    y += Rs_Px(30);
    h = Wh_TextHeight(g_wh.wheelsNote, fieldW);
    MoveWindow(g_wh.wheelsNote, x, y, fieldW, h, TRUE);
    y += h;
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Wheels");
    return card.bottom;
}

// A click (Browse, Clear) or automation set the field: char-wheel at once,
// and the page checks after a click (CharWheels_ExportChanged 2). Returns as
// Wh_Check.
static int Wh_SetModelNow(HWND page, const wchar_t *path)
{
    g_wh.applying = 1;
    Rs_SetText(g_wh.model, path);
    g_wh.applying = 0;
    g_wh.exportChanged = 2;
    Wh_WheelsNote();
    return Wh_Check(page);
}

static void Wh_Browse(HWND page)
{
    wchar_t start[WH_VAL];
    wchar_t pick[WH_VAL];
    Wh_FieldPath(start, WH_VAL);
    if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the wheel model",
                          L"Wheel models (*.obj, *.ply)\0*.obj;*.ply\0OBJ models (*.obj)\0*.obj\0PLY models (*.ply)\0*.ply\0All files\0*.*\0\0",
                          start, pick, WH_VAL))
        Wh_SetModelNow(page, pick);
}

static void Wh_SetAnimation(int on)
{
    g_wh.anim = on != 0;
    SendMessageW(g_wh.animate, BM_SETCHECK, g_wh.anim ? BST_CHECKED : BST_UNCHECKED, 0);
    RsView_SetWheelAnimation(g_wh.view, g_wh.anim);
    // Stopped: back to the angles set last, so that a picture is the same each time.
    if (!g_wh.anim)
        RsView_SetWheelTurn(g_wh.view, g_wh.spin, g_wh.steer);
}

int CharWheels_Command(HWND page, int id, int code)
{
    if (id < WH_ID_FIRST || id > WH_ID_LAST)
        return 0;
    if (!g_wh.created)
        return 1;
    switch (id) {
    case WH_ID_MODEL:
        if (code == EN_CHANGE && !g_wh.applying) {
            Wh_Schedule(page);
            g_wh.exportChanged = g_wh.exportChanged ? g_wh.exportChanged : 1;
            Wh_WheelsNote();
        }
        break;
    case WH_ID_BROWSE:
        if (code == BN_CLICKED)
            Wh_Browse(page);
        break;
    case WH_ID_CLEAR:
        if (code == BN_CLICKED)
            Wh_SetModelNow(page, L"");
        break;
    case WH_ID_ANIMATE:
        if (code == BN_CLICKED)
            Wh_SetAnimation(Wh_IsChecked(g_wh.animate));
        break;
    }
    return 1;
}

LRESULT CharWheels_Notify(HWND page, NMHDR *hdr, int *handled)
{
    (void)page;
    (void)hdr;
    *handled = 0;
    return 0;
}

static void Wh_SizeSet(int percent)
{
    if (percent < WH_SIZE_MIN)
        percent = WH_SIZE_MIN;
    if (percent > WH_SIZE_MAX)
        percent = WH_SIZE_MAX;
    if (percent != g_wh.sizeNow)
        g_wh.exportChanged = 2;
    g_wh.sizeNow = percent;
    if ((int)SendMessageW(g_wh.size, TBM_GETPOS, 0, 0) != percent)
        SendMessageW(g_wh.size, TBM_SETPOS, TRUE, percent);
    Wh_SizeShow();
    RsView_SetWheelScale(g_wh.view, percent);
}

// A job this card ended (Wh_Check)? done = 1: it is gone, its file is deleted.
static int Wh_Ended(int id, int done)
{
    int i;
    for (i = 0; i < WH_ENDED && g_wh.endedId[i]; i++) {
        if (g_wh.endedId[i] != id)
            continue;
        if (done) {
            Wh_TempDelete(g_wh.endedSeq[i]);
            memmove(&g_wh.endedId[i], &g_wh.endedId[i + 1], sizeof(g_wh.endedId[0]) * (WH_ENDED - 1 - i));
            memmove(&g_wh.endedSeq[i], &g_wh.endedSeq[i + 1], sizeof(g_wh.endedSeq[0]) * (WH_ENDED - 1 - i));
            g_wh.endedId[WH_ENDED - 1] = 0;
            g_wh.endedSeq[WH_ENDED - 1] = 0;
        }
        return 1;
    }
    return 0;
}

LRESULT CharWheels_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    int i;

    *handled = 0;
    switch (msg) {
    case RS_WM_JOB_LINE:
        // Only lines of its own job; the page frees all others.
        if (g_wh.jobId && (int)wParam == g_wh.jobId) {
            wchar_t *line = (wchar_t *)lParam;
            if (line)
                Wh_ParseLine(line);
            Rs_Free(line);
            *handled = 1;
        } else if (Wh_Ended((int)wParam, 0)) {
            Rs_Free((wchar_t *)lParam);
            *handled = 1;
        }
        return 0;
    case RS_WM_JOB_DONE:
        if (g_wh.jobId && (int)wParam == g_wh.jobId) {
            Wh_JobDone(page, (int)lParam);
            *handled = 1;
        } else if (Wh_Ended((int)wParam, 1)) {
            *handled = 1;
        }
        return 0;
    case WM_TIMER:
        if (wParam == WH_TIMER_CHECK) {
            KillTimer(page, WH_TIMER_CHECK);
            g_wh.timer = 0;
            Wh_Check(page);
            *handled = 1;
        }
        return 0;
    case WM_HSCROLL:
        if (g_wh.size && (HWND)lParam == g_wh.size) {
            Wh_SizeSet((int)SendMessageW(g_wh.size, TBM_GETPOS, 0, 0));
            *handled = 1;
        }
        return 0;
    case WM_DESTROY:
        // Not handled: the page cleans up after this.
        if (g_wh.timer)
            KillTimer(page, WH_TIMER_CHECK);
        if (g_wh.jobId)
            Rs_KillJob(g_wh.jobId);
        Wh_TempDelete(g_wh.jobSeq);
        for (i = 0; i < WH_ENDED; i++)
            Wh_TempDelete(g_wh.endedSeq[i]);
        g_wh.timer = 0;
        g_wh.jobId = 0;
        g_wh.jobSeq = 0;
        g_wh.created = 0;
        return 0;
    }
    return 0;
}

static int Wh_AutoOnOff(const wchar_t *verb, const wchar_t *arg, int *on)
{
    if (_wcsicmp(arg, L"on") == 0)
        *on = 1;
    else if (_wcsicmp(arg, L"off") == 0)
        *on = 0;
    else {
        Rs_AutoLog(L"  %ls: say on or off", verb);
        return 0;
    }
    return 1;
}

// Whole numbers out of arg, at most max; returns how many, -1 when something
// else is in it.
static int Wh_AutoNumbers(const wchar_t *arg, long *out, int max)
{
    const wchar_t *p = arg;
    int n = 0;

    for (;;) {
        wchar_t *end;
        long v;
        while (*p == L' ')
            p++;
        if (!*p)
            return n;
        v = wcstol(p, &end, 10);
        if (end == p || n >= max || (*end && *end != L' '))
            return -1;
        out[n++] = v;
        p = end;
    }
}

int CharWheels_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    long v[2];
    int on, r;

    if (wcscmp(verb, L"wheel-model") != 0 && wcscmp(verb, L"wheel") != 0 && wcscmp(verb, L"wheel-size") != 0 &&
        wcscmp(verb, L"wheel-turn") != 0 && wcscmp(verb, L"wheel-anim") != 0 && wcscmp(verb, L"wheel-bench") != 0)
        return RS_AUTO_UNKNOWN;
    if (!g_wh.created) {
        Rs_AutoLog(L"  %ls: the card Wheels is not there", verb);
        return RS_AUTO_FAIL;
    }

    if (wcscmp(verb, L"wheel-model") == 0 || wcscmp(verb, L"wheel") == 0) {
        int none = !arg[0] || _wcsicmp(arg, L"none") == 0;
        r = Wh_SetModelNow(page, none ? L"" : arg);
        if (r > 0) {
            Rs_AutoLog(L"  %ls: %ls", verb, arg);
            return RS_AUTO_WAIT;
        }
        if (r == 0) {
            Rs_AutoLog(L"  %ls: (none)", verb);
            return RS_AUTO_DONE;
        }
        Rs_AutoLog(L"  %ls: rldpack could not be started", verb);
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"wheel-size") == 0) {
        if (Wh_AutoNumbers(arg, v, 1) != 1 || v[0] < WH_SIZE_MIN || v[0] > WH_SIZE_MAX) {
            Rs_AutoLog(L"  %ls: '%ls' is not a whole number from %d to %d", verb, arg, WH_SIZE_MIN, WH_SIZE_MAX);
            return RS_AUTO_FAIL;
        }
        Wh_SizeSet((int)v[0]);
        UpdateWindow(g_wh.view);      // painted before a following "shot"
        Rs_AutoLog(L"  %ls: %d %%", verb, g_wh.sizeNow);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"wheel-turn") == 0) {
        if (Wh_AutoNumbers(arg, v, 2) != 2) {
            Rs_AutoLog(L"  %ls: say <spin> <steer> in whole degrees", verb);
            return RS_AUTO_FAIL;
        }
        v[0] %= 360;
        if (v[0] < 0)
            v[0] += 360;
        if (v[1] < -RS_VIEW_WHEEL_STEER_MAX)
            v[1] = -RS_VIEW_WHEEL_STEER_MAX;
        if (v[1] > RS_VIEW_WHEEL_STEER_MAX)
            v[1] = RS_VIEW_WHEEL_STEER_MAX;
        g_wh.spin = (int)v[0];
        g_wh.steer = (int)v[1];
        Wh_SetAnimation(0);           // a fixed picture: the animation stops
        RsView_SetWheelTurn(g_wh.view, g_wh.spin, g_wh.steer);
        UpdateWindow(g_wh.view);
        Rs_AutoLog(L"  %ls: spin %d, steer %d degrees", verb, g_wh.spin, g_wh.steer);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"wheel-bench") == 0) {
        int wholeAvg = 0, wholeMax = 0, wheelsAvg = 0, wheelsMax = 0, pw = 0, ph = 0, drawn, n;
        if (Wh_AutoNumbers(arg, v, 1) != 1 || v[0] < 1 || v[0] > WH_BENCH_MAX) {
            Rs_AutoLog(L"  %ls: say how many pictures, 1 to %d", verb, WH_BENCH_MAX);
            return RS_AUTO_FAIL;
        }
        drawn = RsView_WheelPixels(g_wh.view, &pw, &ph);
        n = RsView_WheelBench(g_wh.view, (int)v[0], &wholeAvg, &wholeMax, &wheelsAvg, &wheelsMax);
        if (n <= 0) {
            Rs_AutoLog(L"  %ls: no wheel model in the preview", verb);
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  %ls: %d pictures of %d x %d pixels, %d triangles per wheel x 4%ls: picture %d us on average (longest %d us), "
                   L"the four wheels %d us (longest %d us); the animation ticks every 33 ms",
                   verb, n, pw, ph, g_wh.triangles, drawn ? L"" : L" (not drawn: no model or Show kart wheels off)", wholeAvg, wholeMax,
                   wheelsAvg, wheelsMax);
        return RS_AUTO_DONE;
    }
    // wheel-anim
    if (!Wh_AutoOnOff(verb, arg, &on))
        return RS_AUTO_FAIL;
    Wh_SetAnimation(on);
    Rs_AutoLog(L"  %ls: %ls", verb, on ? L"on" : L"off");
    return RS_AUTO_DONE;
}

int CharWheels_Busy(void)
{
    return g_wh.jobId != 0 || g_wh.timer;
}

// The card does not depend on the character model: its wheel stays in the
// view through every check of the page.
void CharWheels_ModelChecked(HWND page, const wchar_t *model, int sizePercent, int ok)
{
    (void)page;
    (void)model;
    (void)sizePercent;
    (void)ok;
}

int CharWheels_FilesChanged(HWND page, const wchar_t *why)
{
    if (!g_wh.created || !g_wh.stampKnown || g_wh.jobId || g_wh.timer || !g_wh.checked[0])
        return 0;
    if (Wh_StampNow() == g_wh.stamp)
        return 0;
    g_wh.generation++;
    if (Rs_Automating())
        Rs_AutoLog(L"  wheel files: written since the last reading (%ls) - reading the wheel again", why);
    // As a click: the wheel is read at once, the page checks again.
    g_wh.exportChanged = 2;
    Wh_Check(page);
    return 1;
}

int CharWheels_Generation(void)
{
    return g_wh.generation;
}

int CharWheels_MissingTextures(wchar_t *out, int cap)
{
    int i;
    if (out && cap > 0) {
        out[0] = 0;
        for (i = 0; i < g_wh.missingCount; i++) {
            if (i)
                wcsncat(out, L", ", (size_t)cap - 1 - wcslen(out));
            wcsncat(out, g_wh.missing[i], (size_t)cap - 1 - wcslen(out));
        }
    }
    return g_wh.created ? g_wh.missingCount : 0;
}

int CharWheels_Shown(void)
{
    return g_wh.created && g_wh.loaded;
}

int CharWheels_ModelPath(wchar_t *out, int cap)
{
    wchar_t path[WH_VAL];
    if (!g_wh.created)
        return 0;
    Wh_FieldPath(path, WH_VAL);
    if (out && cap > 0)
        Wh_Copy(out, cap, path);
    return path[0] != 0;
}

int CharWheels_SizePercent(void)
{
    return g_wh.sizeNow;
}

int CharWheels_ExportChanged(void)
{
    const int changed = g_wh.exportChanged;
    g_wh.exportChanged = 0;
    return changed;
}

void CharWheels_PageState(int obj, int kartWheels, int nativeOn, int passed, int upZ, int backwards)
{
    // New axes turn the wheel as well: read it again.
    if (g_wh.created && ((upZ != 0) != g_wh.upZ || (backwards != 0) != g_wh.backwards)) {
        g_wh.upZ = upZ != 0;
        g_wh.backwards = backwards != 0;
        if (CharWheels_ModelPath(NULL, 0))
            Wh_Schedule(g_wh.page);
    }
    g_wh.obj = obj != 0;
    g_wh.kartWheels = kartWheels != 0;
    g_wh.nativeOn = nativeOn != 0;
    g_wh.passed = passed != 0;
    Wh_WheelsNote();
}

void CharWheels_SetTextures(const wchar_t *dir)
{
    if (!g_wh.created || wcscmp(dir ? dir : L"", g_wh.textures) == 0)
        return;
    Wh_Copy(g_wh.textures, WH_VAL, dir);
    // Another folder may hold the wheel's texture: read the wheel again.
    if (CharWheels_ModelPath(NULL, 0))
        Wh_Schedule(g_wh.page);
}

static void Wh_Put(FILE *f, const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    char *utf8;
    va_list ap;

    va_start(ap, fmt);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
    utf8 = Rs_ToUtf8(buf);
    fputs(utf8, f);
    fputs("\n", f);
    Rs_Free(utf8);
}

static const wchar_t *Wh_EnabledWord(HWND h)
{
    return IsWindowEnabled(h) ? L"enabled" : L"greyed out";
}

void CharWheels_Report(FILE *f)
{
    wchar_t *text;

    if (!f || !g_wh.created)
        return;
    Wh_Put(f, L"wheels card: enabled (preview feature)");
    text = Rs_GetText(g_wh.hint);
    Wh_Put(f, L"wheels hint: %ls", text);
    Rs_Free(text);
    Wh_Put(f, L"wheels fields: model %ls, browse %ls, clear %ls, size %ls, animate %ls", Wh_EnabledWord(g_wh.model),
           Wh_EnabledWord(g_wh.browse), Wh_EnabledWord(g_wh.clear), Wh_EnabledWord(g_wh.size), Wh_EnabledWord(g_wh.animate));
    text = Rs_GetText(g_wh.model);
    Wh_Put(f, L"wheel model: %ls", text && text[0] ? text : L"none");
    Rs_Free(text);
    Wh_Put(f, L"wheel model (last run): %ls", g_wh.checked[0] ? g_wh.checked : L"(none)");
    Wh_Put(f, L"wheel textures folder: %ls", g_wh.textures[0] ? g_wh.textures : L"(none)");
    Wh_Put(f, L"wheel status: %ls", g_wh.statusText);
    Wh_Put(f, L"wheel warnings: %d", g_wh.warnings);
    {
        wchar_t names[400];
        int n = CharWheels_MissingTextures(names, 400);
        Wh_Put(f, L"wheel textures missing: %d%ls%ls", n, n ? L" " : L"", names);
    }
    Wh_Put(f, L"wheel files stamped: %d files, %d folders%ls", g_wh.fileCount + (g_wh.checked[0] ? 1 : 0), g_wh.folderCount,
           g_wh.stampKnown ? L"" : L" (no stamp)");
    Wh_Put(f, L"wheel in the preview: %ls", g_wh.loaded ? L"yes" : L"no");
    Wh_Put(f, L"wheel size: %d %%", g_wh.sizeNow);
    Wh_Put(f, L"wheel preview: spin %d, steer %d, animation %ls", g_wh.spin, g_wh.steer, g_wh.anim ? L"on" : L"off");
    Wh_Put(f, L"wheel export: %ls", g_wh.passed ? L"passed as --wheel-model (with the native model)" : L"not passed");
    Wh_Put(f, L"wheel in the preview only: %ls", g_wh.loaded && g_wh.kartWheels && !g_wh.passed ? L"yes (shown, not built)" : L"no");
    text = Rs_GetText(g_wh.wheelsNote);
    Wh_Put(f, L"wheels note: %ls", text ? text : L"");
    Rs_Free(text);
}
