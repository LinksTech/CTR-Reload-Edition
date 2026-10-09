// rs_wheels.c - the card "Wheels" of the page Character (preview feature)
//
// Wheels of one's own instead of the game's kart wheels: a glTF (.glb,
// recommended), an OBJ (with its MTL file and one texture) or a PLY of one
// wheel, optionally another one for the rear wheels, their size, the place of
// each axle, whether the game draws them at every distance, and whether the
// preview turns and steers them. PREVIEW, open to everyone ("Preview feature"
// in the title line, with and without --enable-preview-features): the wheels
// belong to the native model, which is a preview itself.
//
// TWO JOBS. The preview: choosing a model runs `rldpack char-wheel` (600 ms
// after the last change, like the page's own check; tools/rldpack_wheel.inc),
// whose preview file (RLDPW3: the wheel with its UVs and its texture in full
// size, mip levels as in the game; as large as the game's wheel) goes to the
// view (RsView_LoadWheelModel) and is deleted at once; the view draws it at
// the kart's four wheel points, the right ones mirrored, textured, by depth
// against the model. The rear wheel model is read the same way by a char-wheel
// of its own (RsView_LoadRearWheelModel) and drawn at the two rear points;
// without one the rear wheels are the wheel model, as before. Size, axles,
// turn and animation are view settings. The messages of each (the budget: 1024
// triangles, 2048 points, a texture of at most 1024 x 1024; never cut short)
// are the status line under the fields: an error says why a wheel is not
// shown, a warning (a texture not found, an axle that is not X) follows the
// facts in the warning colour. Browse and Clear are clicks: char-wheel runs at
// once. A wheel exported again (its model file, its MTL, its texture; a
// texture put where rldpack looks for it) is read again when Reload Studio
// becomes the active program again or the page is shown again
// (CharWheels_FilesChanged): the card keeps a stamp (size and time of writing)
// of every file the last char-wheel of each wheel read, like the page
// (rs_char.c, Char_StampFields). The export: the page (rs_char.c,
// Char_MakeArgs) passes `--wheel-model <file>` and, when it is not 100 %,
// `--wheel-size <percent>` to make-char while the native model is built and
// Show kart wheels is on (CharWheels_ModelPath, CharWheels_SizePercent); with
// them, and only when set, `--rear-wheel-model <file>`, `--axle-front
// <dz>,<dy>,<dtrack>` and `--axle-rear ...` (not 0 0 0)
// (CharWheels_RearModelPath, CharWheels_Axle) - none of them set, the command
// is the one of before. A change of any makes the page
// check again (CharWheels_ExportChanged). The axes of the card Import (--up,
// --forward) are the wheels' too: char-wheel gets them, and a change reads the
// wheels again; so does the Textures folder of the page (--textures, an OBJ or
// glTF body only, CharWheels_SetTextures), where make-char looks for the
// wheels' textures as well. make-char then writes WHLS version 2 beside the
// native model (version 3 with a rear wheel or an axle moved);
// the classic model keeps the game's wheels as its fallback.
//
// THE AXLES: per axle (the choice Front axle / Rear axle) three whole numbers
// in model units (the retail wheel has a radius of 16): Forward (-32..32, +
// toward the front), Up (-16..32) and Track (-32..64, the whole track wider;
// each wheel moves by half of it); 0 0 0 = the retail wheel points. The view
// takes them at once (RsView_SetAxle: 1/16 units, its dtrack per wheel = half
// the track). There is no choice "Always draw": the game draws an author's
// wheels at every distance (a custom driver has one model head, the retail
// rule of the tyres never applies), so make-char's --wheels-always has no
// effect in the game and the card does not pass it.
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
//   wheel-model <glb|obj|ply|none>  (also "wheel") choose the wheel; waits for char-wheel
//   rear-wheel-model <glb|obj|ply|none>  (also "rear-wheel") the rear wheel; waits as well
//   wheel-size <50..200>        percent of the game's wheel
//   axle front|rear <forward> <up> <track>  whole model units (0 0 0 = retail)
//   wheels-always on|off        refused: it would change nothing in the game
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
#define WH_ID_REAR_LABEL    312
#define WH_ID_REAR          313
#define WH_ID_REAR_BROWSE   314
#define WH_ID_REAR_CLEAR    315
#define WH_ID_AXLE          316     // the choice Front axle / Rear axle
#define WH_ID_AXLE_CAPTION  317     // 317..319: Fwd, Up, Track
#define WH_ID_AXLE_VALUE    320     // 320..322: their fields
#define WH_ID_FIRST         300
#define WH_ID_LAST          339

#define WH_TIMER_CHECK      300     // timers of the page window, their own IDs: the wheel,
#define WH_TIMER_CHECK_REAR 301     // the rear wheel
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

// The two wheels of the card: their own field, job, stamp and status.
enum { WH_FRONT = 0, WH_REAR, WH_SLOTS };
// The axles and their three values (make-char --axle-front|--axle-rear dz,dy,dtrack).
enum { WH_AXLE_FORWARD = 0, WH_AXLE_UP, WH_AXLE_TRACK, WH_AXLE_VALUES };
static const int g_whAxleMin[WH_AXLE_VALUES] = { -32, -16, -32 };
static const int g_whAxleMax[WH_AXLE_VALUES] = { 32, 32, 64 };
static const wchar_t *const g_whAxleCaption[WH_AXLE_VALUES] = { L"Fwd", L"Up", L"Track" };

#define WH_TEXT_NOTE        L"Wheels of your own instead of the game's kart wheels, on the native model."
#define WH_TEXT_NONE        L"No wheel model chosen."
// The line under the options (Wh_WheelsText): what is built with the choices of the page.
#define WH_TEXT_OWN         L"Your wheels on the native model; its classic model keeps the game's wheels as the fallback."
#define WH_TEXT_OWN_OFF     L"Not used: Show kart wheels is off (tab Model) - no wheels at all. Turn it on for your wheels."
#define WH_TEXT_OWN_PLY     L"Not used: your wheels need the native model, which a PLY model has not (export the model as glTF or OBJ)."
#define WH_TEXT_OWN_TICK    L"Not used yet: your wheels need the native model - tick Native model (Extras, Import)."
// Appended to the two above while the preview draws the wheel all the same.
#define WH_TEXT_PREVIEW_ONLY L" The preview shows them, the character will not have them."
#define WH_TEXT_GAME        L"No wheel model: the kart keeps the game's wheels."
#define WH_TEXT_GAME_OFF    L"Show kart wheels is off (tab Model): no wheels at all."
#define WH_TEXT_REAR_ALONE  L"The rear wheel model needs a wheel model: choose that first."

struct WhSlot {
    HWND label, model, browse, clear;
    int timer;                  // the delayed check is pending
    int jobId, jobSeq;
    int endedId[WH_ENDED], endedSeq[WH_ENDED];   // ended by a newer input: their lines are dropped,
                                // their file deleted when they are gone
    int loaded;                 // the wheel model is in the view
    wchar_t checked[WH_VAL];    // the model of the last char-wheel
    wchar_t statusText[1024];   // its part of the status line
    int statusLevel;            // 0 fine, 1 warning, 2 error, -1 muted (nothing chosen, reading)
    // the files the last char-wheel read (CharWheels_FilesChanged): their stamp
    // when it ended; the folders where a texture not found is looked for
    unsigned long long stamp;
    int stampKnown;
    int fileCount, folderCount;
    wchar_t files[WH_FILES][WH_VAL], folders[WH_FILES][WH_VAL];
    // the textures of the last char-wheel that are not there (CharWheels_MissingTextures)
    int missingCount;
    wchar_t missing[WH_MISSING][96];
    // the running job
    int previewOk, triangles, points, texW, texH;
    double fitBefore;           // @char fit: the size across of the file, before char-wheel fits it (0 = not said)
    double loadedBefore;        // the same of the wheel in the view
    int warnings;               // warnings of the job
    wchar_t across[16], width[16], firstError[400], firstWarning[400], meshFormat[8];
    int jobFileCount, jobFolderCount, jobMissingCount;
    wchar_t jobFiles[WH_FILES][WH_VAL], jobFolders[WH_FILES][WH_VAL], jobMissing[WH_MISSING][96];
};

static struct {
    HWND page, view;
    HWND hint, note, sizeLabel, size, sizeValue, animate, wheelsNote, status;
    HWND axle, axleCaption[WH_AXLE_VALUES], axleValue[WH_AXLE_VALUES];
    struct WhSlot slot[WH_SLOTS];
    int created;
    int applying;               // the card sets a field itself: no check from its EN_CHANGE
    int seq;
    int sizeNow, spin, steer, anim;
    int axleShown;              // the axle the fields show: 0 front, 1 rear
    int axleNow[2][WH_AXLE_VALUES];
    int exportChanged;          // CharWheels_ExportChanged: 1 typed, 2 clicked, 0 nothing
    int obj, kartWheels, nativeOn, passed;   // CharWheels_PageState
    int upZ, backwards;         // the page's axes (card Import): the wheel's as well
    wchar_t textures[WH_VAL];   // the page's Textures folder as make-char gets it ("" = none)
    COLORREF statusColor;
    wchar_t statusText[1024];
    int generation;             // CharWheels_Generation: a file written since
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

static void Wh_Append(wchar_t *out, int cap, const wchar_t *in)
{
    size_t n = wcslen(out);
    if ((int)n < cap - 1)
        wcsncat(out, in ? in : L"", (size_t)cap - 1 - n);
}

// Text of a field without quotation marks and spaces at the edges.
static void Wh_EditPath(HWND field, wchar_t *out, int cap)
{
    wchar_t *text = Rs_GetText(field);
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

static void Wh_FieldPath(int slot, wchar_t *out, int cap)
{
    Wh_EditPath(g_wh.slot[slot].model, out, cap);
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

// The status line: the wheel's part, and the rear wheel's when one is chosen
// (or read); the colour of the worse, which comes first.
static void Wh_StatusShow(void)
{
    const struct WhSlot *f = &g_wh.slot[WH_FRONT], *r = &g_wh.slot[WH_REAR];
    wchar_t rear[WH_VAL], text[1024];
    int level;

    Wh_FieldPath(WH_REAR, rear, WH_VAL);
    if (!rear[0] && !r->jobId) {
        Wh_Copy(text, 1024, f->statusText);
        level = f->statusLevel;
    } else {
        // The worse first: in the compact layout the line is cut short.
        if (r->statusLevel > f->statusLevel)
            swprintf(text, 1024, L"Rear: %ls Wheel: %ls", r->statusText, f->statusText);
        else
            swprintf(text, 1024, L"Wheel: %ls Rear: %ls", f->statusText, r->statusText);
        level = f->statusLevel > r->statusLevel ? f->statusLevel : r->statusLevel;
    }
    Wh_Copy(g_wh.statusText, 1024, text);
    g_wh.statusColor = level >= 2 ? RS_COL_ERROR : level == 1 ? RS_COL_WARNING : level == 0 ? RS_COL_TEXT : RS_COL_MUTED;
    Rs_SetTextColor(g_wh.status, g_wh.statusColor);
    Wh_SetNote(g_wh.status, text);
}

static void Wh_Status(int slot, const wchar_t *text, int level)
{
    Wh_Copy(g_wh.slot[slot].statusText, 1024, text);
    g_wh.slot[slot].statusLevel = level;
    Wh_StatusShow();
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

// The files of the last char-wheel of a wheel as they are now.
static unsigned long long Wh_StampNow(const struct WhSlot *s)
{
    unsigned long long h = Wh_StampFile(WH_STAMP_START, s->checked);
    int i;
    for (i = 0; i < s->fileCount; i++)
        h = Wh_StampFile(h, s->files[i]);
    for (i = 0; i < s->folderCount; i++)
        h = Wh_StampFolder(h, s->folders[i]);
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
    Wh_FieldPath(WH_FRONT, path, WH_VAL);
    if (!path[0]) {
        Wh_FieldPath(WH_REAR, path, WH_VAL);
        if (path[0] && g_wh.kartWheels)
            return WH_TEXT_REAR_ALONE;
        return g_wh.kartWheels ? WH_TEXT_GAME : WH_TEXT_GAME_OFF;
    }
    if (!g_wh.kartWheels)
        return WH_TEXT_OWN_OFF;
    if (g_wh.passed)
        return WH_TEXT_OWN;
    Wh_Copy(text, 256, !g_wh.obj ? WH_TEXT_OWN_PLY : WH_TEXT_OWN_TICK);
    if (g_wh.slot[WH_FRONT].loaded)
        Wh_Append(text, 256, WH_TEXT_PREVIEW_ONLY);
    return text;
}

static void Wh_WheelsNote(void)
{
    if (g_wh.created)
        Wh_SetNote(g_wh.wheelsNote, Wh_WheelsText());
}

// ---------------------------------------------------------------------------
// The check: rldpack char-wheel, one per wheel
// ---------------------------------------------------------------------------

// A wheel leaves the view: the game's wheels (the wheel model for the rear
// ones) are drawn again.
static void Wh_Drop(int slot)
{
    struct WhSlot *s = &g_wh.slot[slot];
    if (s->loaded) {
        if (slot == WH_FRONT)
            RsView_DropWheelModel(g_wh.view);
        else
            RsView_DropRearWheelModel(g_wh.view);
    }
    s->loaded = 0;
    Wh_WheelsNote();
}

// Starts char-wheel for the model in the field of a wheel. 1 = started,
// 0 = nothing to check (empty field: the wheel is dropped),
// -1 = rldpack could not be started. A char-wheel still running is ended
// first; its file goes when it is gone.
static int Wh_Check(HWND page, int slot)
{
    struct WhSlot *s = &g_wh.slot[slot];
    wchar_t path[WH_VAL], preview[WH_VAL];
    const wchar_t *args[14];
    int n;

    if (s->timer) {
        KillTimer(page, slot == WH_FRONT ? WH_TIMER_CHECK : WH_TIMER_CHECK_REAR);
        s->timer = 0;
    }
    if (s->jobId) {
        int i;
        for (i = 0; i < WH_ENDED && s->endedId[i]; i++)
            ;
        if (i == WH_ENDED) {
            Wh_TempDelete(s->endedSeq[0]);
            memmove(&s->endedId[0], &s->endedId[1], sizeof(s->endedId[0]) * (WH_ENDED - 1));
            memmove(&s->endedSeq[0], &s->endedSeq[1], sizeof(s->endedSeq[0]) * (WH_ENDED - 1));
            i = WH_ENDED - 1;
        }
        Rs_KillJob(s->jobId);
        s->endedId[i] = s->jobId;
        s->endedSeq[i] = s->jobSeq;
        s->jobId = 0;
        s->jobSeq = 0;
    }
    Wh_FieldPath(slot, path, WH_VAL);
    Wh_Copy(s->checked, WH_VAL, path);
    // The stamp is that of the run to come (Wh_JobDone); none while it runs.
    s->stampKnown = 0;
    s->fileCount = 0;
    s->folderCount = 0;
    s->missingCount = 0;
    if (!path[0]) {
        Wh_Drop(slot);
        Wh_Status(slot, slot == WH_FRONT ? WH_TEXT_NONE : L"none - the rear wheels are the wheel model.", -1);
        return 0;
    }

    s->jobSeq = ++g_wh.seq;
    Wh_TempPath(preview, WH_VAL, s->jobSeq);
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
    s->previewOk = 0;
    s->fitBefore = 0.0;
    s->triangles = -1;
    s->points = -1;
    s->texW = 0;
    s->texH = 0;
    s->across[0] = 0;
    s->width[0] = 0;
    s->meshFormat[0] = 0;
    s->firstError[0] = 0;
    s->firstWarning[0] = 0;
    s->warnings = 0;
    s->jobFileCount = 0;
    s->jobFolderCount = 0;
    s->jobMissingCount = 0;
    s->jobId = Rs_RunRldpack(page, args, n);
    if (!s->jobId) {
        s->jobSeq = 0;
        Wh_Status(slot, L"rldpack could not be started.", 2);
        return -1;
    }
    Wh_Status(slot, slot == WH_FRONT ? L"Reading the wheel model..." : L"reading the rear wheel model...", -1);
    return 1;
}

static void Wh_Schedule(HWND page, int slot)
{
    const UINT_PTR id = slot == WH_FRONT ? WH_TIMER_CHECK : WH_TIMER_CHECK_REAR;
    if (g_wh.applying)
        return;
    KillTimer(page, id);
    g_wh.slot[slot].timer = SetTimer(page, id, WH_CHECK_DELAY, NULL) != 0;
}

static const wchar_t *Wh_Field(wchar_t **f, int n, int i)
{
    return i < n && f[i] ? f[i] : L"";
}

static void Wh_ParseLine(struct WhSlot *s, wchar_t *line)
{
    wchar_t *f[16];
    int n = Rs_SplitMachine(line, f, 16);

    if (n <= 0)
        return;
    if (wcscmp(f[0], L"char") == 0 && wcscmp(Wh_Field(f, n, 1), L"wheel") == 0) {
        s->triangles = _wtoi(Wh_Field(f, n, 2));
        Wh_Copy(s->across, 16, Wh_Field(f, n, 3));
        Wh_Copy(s->width, 16, Wh_Field(f, n, 4));
    } else if (wcscmp(f[0], L"char") == 0 && wcscmp(Wh_Field(f, n, 1), L"fit") == 0) {
        // @char fit <factor> <across before> <after>
        s->fitBefore = wcstod(Wh_Field(f, n, 3), NULL);
    } else if (wcscmp(f[0], L"char") == 0 && wcscmp(Wh_Field(f, n, 1), L"wheel-mesh") == 0) {
        Wh_Copy(s->meshFormat, 8, Wh_Field(f, n, 2));
        s->points = _wtoi(Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"char") == 0 && wcscmp(Wh_Field(f, n, 1), L"wheel-texture") == 0) {
        s->texW = _wtoi(Wh_Field(f, n, 2));
        s->texH = _wtoi(Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"file") == 0 && wcscmp(Wh_Field(f, n, 1), L"preview") == 0) {
        s->previewOk = wcscmp(Wh_Field(f, n, 2), L"ok") == 0;
    } else if (wcscmp(f[0], L"file") == 0 && wcscmp(Wh_Field(f, n, 1), L"mtl") == 0) {
        // @file mtl <state> <path> <bytes>: a file of the stamp
        Wh_JobListAdd(s->jobFiles, &s->jobFileCount, Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"model") == 0 && wcscmp(Wh_Field(f, n, 1), L"texture") == 0) {
        // @model texture <state> <material> <path> <sha256>: a file of the
        // stamp; one not found also that name in its folder, in that of the
        // model and in the Textures folder, where it may be put (only that
        // name: other files written there, an export for one, change nothing)
        const wchar_t *path = Wh_Field(f, n, 4);
        Wh_JobListAdd(s->jobFiles, &s->jobFileCount, path);
        if (wcscmp(Wh_Field(f, n, 2), L"ok") != 0 && path[0]) {
            wchar_t dir[WH_VAL], there[WH_VAL];
            int i;
            Rs_PathDir(dir, WH_VAL, s->checked);
            Rs_PathJoin(there, WH_VAL, dir, Rs_PathName(path));
            Wh_JobListAdd(s->jobFiles, &s->jobFileCount, there);
            if (g_wh.textures[0]) {
                Rs_PathJoin(there, WH_VAL, g_wh.textures, Rs_PathName(path));
                Wh_JobListAdd(s->jobFiles, &s->jobFileCount, there);
            }
            for (i = 0; i < s->jobMissingCount && _wcsicmp(s->jobMissing[i], Rs_PathName(path)) != 0; i++)
                ;
            if (i == s->jobMissingCount && i < WH_MISSING)
                Wh_Copy(s->jobMissing[s->jobMissingCount++], 96, Rs_PathName(path));
        }
    } else if (wcscmp(f[0], L"msg") == 0 && wcscmp(Wh_Field(f, n, 1), L"error") == 0 && !s->firstError[0]) {
        Wh_Copy(s->firstError, 400, Wh_Field(f, n, 3));
    } else if (wcscmp(f[0], L"msg") == 0 && wcscmp(Wh_Field(f, n, 1), L"warning") == 0) {
        // A texture not found is said by the facts (Wh_JobDone, its name);
        // any other warning (wheel-off-axis) in rldpack's words.
        s->warnings++;
        if (!s->firstWarning[0] && wcscmp(Wh_Field(f, n, 2), L"tex-missing") != 0 &&
            wcscmp(Wh_Field(f, n, 2), L"wheel-texture") != 0)
            Wh_Copy(s->firstWarning, 400, Wh_Field(f, n, 3));
    }
}

// The rear wheel keeps its size against the wheel model, as make-char builds
// it (WHLS version 3: rear radius = radius x across of the rear file /
// across of the wheel file); char-wheel fits every file to the game's wheel,
// so the view gets that ratio (per mille; the same size while one is not known).
static void Wh_RearSize(void)
{
    const double front = g_wh.slot[WH_FRONT].loadedBefore, rear = g_wh.slot[WH_REAR].loadedBefore;
    int size = RS_VIEW_REAR_SIZE_SAME;
    if (front > 0.0 && rear > 0.0) {
        const double r = 1000.0 * rear / front + 0.5;
        size = r < RS_VIEW_REAR_SIZE_MIN ? RS_VIEW_REAR_SIZE_MIN : r > RS_VIEW_REAR_SIZE_MAX ? RS_VIEW_REAR_SIZE_MAX : (int)r;
    }
    RsView_SetRearWheelSize(g_wh.view, size);
}

static void Wh_JobDone(HWND page, int slot, int code)
{
    struct WhSlot *s = &g_wh.slot[slot];
    wchar_t preview[WH_VAL], text[1024], tex[48];
    int seq = s->jobSeq, loaded;

    s->jobId = 0;
    s->jobSeq = 0;
    (void)page;
    // The files it read, and their stamp now (CharWheels_FilesChanged).
    memcpy(s->files, s->jobFiles, sizeof(s->files));
    memcpy(s->folders, s->jobFolders, sizeof(s->folders));
    memcpy(s->missing, s->jobMissing, sizeof(s->missing));
    s->fileCount = s->jobFileCount;
    s->folderCount = s->jobFolderCount;
    s->missingCount = s->jobMissingCount;
    s->stamp = Wh_StampNow(s);
    s->stampKnown = 1;
    Wh_TempPath(preview, WH_VAL, seq);
    loaded = code == 0 && s->previewOk &&
             (slot == WH_FRONT ? RsView_LoadWheelModel(g_wh.view, preview) : RsView_LoadRearWheelModel(g_wh.view, preview));
    if (loaded) {
        wchar_t warn[440];
        s->loaded = 1;
        s->loadedBefore = s->fitBefore;
        Wh_RearSize();
        if (slot == WH_FRONT) {
            RsView_SetWheelScale(g_wh.view, g_wh.sizeNow);
            RsView_SetWheelTurn(g_wh.view, g_wh.spin, g_wh.steer);
            RsView_SetWheelAnimation(g_wh.view, g_wh.anim);
        }
        if (s->texW > 0)
            swprintf(tex, 48, L"texture %d x %d", s->texW, s->texH);
        else if (s->missingCount > 0)
            swprintf(tex, 48, L"texture not found");
        else
            swprintf(tex, 48, L"no texture");
        // What a warning says first: the texture not found (by its name), else
        // rldpack's first other warning (an axle that is not X).
        warn[0] = 0;
        if (s->missingCount > 0)
            swprintf(warn, 440, L" Warning: %ls was not found - the wheel is drawn in its colour. Put it next to the %ls file.",
                     s->missing[0], wcscmp(s->meshFormat, L"obj") == 0 ? L"MTL" : L"model");
        else if (s->firstWarning[0])
            swprintf(warn, 440, L" Warning: %ls", s->firstWarning);
        if (slot == WH_FRONT)
            swprintf(text, 1024, L"%d triangles, %d vertices, %ls - in the preview at the four wheel points, the right ones mirrored.%ls",
                     s->triangles, s->points, tex, warn);
        else
            swprintf(text, 1024, L"%d triangles, %d vertices, %ls - at the two rear wheel points.%ls", s->triangles, s->points, tex, warn);
        Wh_Status(slot, text, warn[0] ? 1 : 0);
        Wh_WheelsNote();
    } else {
        Wh_Drop(slot);
        if (s->firstError[0])
            swprintf(text, 1024, L"Not shown: %ls", s->firstError);
        else if (code == 0)
            swprintf(text, 1024, L"Not shown: the preview file could not be read.");
        else
            swprintf(text, 1024, L"Not shown: rldpack ended with code %d.", code);
        Wh_Status(slot, text, 2);
    }
    Wh_TempDelete(seq);
}

// ---------------------------------------------------------------------------
// The axles
// ---------------------------------------------------------------------------

// The view takes the axles in 1/16 model units: forward and up x 16. Its
// track is per wheel, make-char's (--axle-*, WHLS version 3) the whole track
// in model units, of which each wheel moves half: x 16 / 2 = x 8.
static void Wh_AxleView(int axle)
{
    const int *v = g_wh.axleNow[axle];
    RsView_SetAxle(g_wh.view, axle == 0 ? RS_VIEW_AXLE_FRONT : RS_VIEW_AXLE_REAR, v[WH_AXLE_FORWARD] * 16, v[WH_AXLE_UP] * 16,
                   v[WH_AXLE_TRACK] * 8);
}

// The fields show the axle chosen (not a change: no check).
static void Wh_AxleFields(void)
{
    wchar_t text[16];
    int i;
    g_wh.applying = 1;
    SendMessageW(g_wh.axle, CB_SETCURSEL, (WPARAM)g_wh.axleShown, 0);
    for (i = 0; i < WH_AXLE_VALUES; i++) {
        swprintf(text, 16, L"%d", g_wh.axleNow[g_wh.axleShown][i]);
        Rs_SetText(g_wh.axleValue[i], text);
    }
    g_wh.applying = 0;
}

// A value of an axle, clamped; the view at once, the page checks again
// (typed: after 600 ms, as a field).
static void Wh_AxleSet(int axle, int which, int value, int typed)
{
    if (value < g_whAxleMin[which])
        value = g_whAxleMin[which];
    if (value > g_whAxleMax[which])
        value = g_whAxleMax[which];
    if (g_wh.axleNow[axle][which] != value) {
        g_wh.axleNow[axle][which] = value;
        g_wh.exportChanged = typed ? (g_wh.exportChanged ? g_wh.exportChanged : 1) : 2;
    }
    Wh_AxleView(axle);
}

// ---------------------------------------------------------------------------
// The card
// ---------------------------------------------------------------------------

static void Wh_Tips(void)
{
    const wchar_t *model = L"A glTF (.glb, recommended), an OBJ (with its MTL file and one texture of at most 1024 x 1024) or a PLY "
                           L"of one wheel: axle along X, outer side toward +X, in the axes of the model (Up and Forward on the card "
                           L"Import), like the body; at most 1024 triangles - never reduced. Built with the native model; the right "
                           L"wheels are its mirror image.";
    const wchar_t *rear = L"Optional: another wheel for the two rear wheels (the same rules as the wheel model). Empty: the rear "
                          L"wheels are the wheel model, as in the game.";
    const wchar_t *size = L"The size of your wheels, 100 % = the game's wheel; the bottom stays on the ground. In the preview and "
                          L"in the character (make-char --wheel-size).";
    const wchar_t *anim = L"Turns and steers the wheels in the preview.";
    const wchar_t *axle = L"Moves the wheels of one axle, in model units (the game's wheel has a radius of 16): Fwd toward the "
                          L"front (-32..32), Up (-16..32), Track the whole track wider or narrower (-32..64). 0 0 0 = the "
                          L"game's wheel points. In the preview at once.";
    int i;

    Rs_SetTip(g_wh.hint, L"Preview: wheels of your own on the native model; the game draws them with NATIVE DRIVERS set to "
                         L"PREVIEW (OPTIONS, GRAPHICS). " WH_TEXT_NOTE);
    Rs_SetTip(g_wh.slot[WH_FRONT].label, model);
    Rs_SetTip(g_wh.slot[WH_FRONT].model, model);
    Rs_SetTip(g_wh.slot[WH_FRONT].browse, model);
    Rs_SetTip(g_wh.slot[WH_FRONT].clear, model);
    Rs_SetTip(g_wh.slot[WH_REAR].label, rear);
    Rs_SetTip(g_wh.slot[WH_REAR].model, rear);
    Rs_SetTip(g_wh.slot[WH_REAR].browse, rear);
    Rs_SetTip(g_wh.slot[WH_REAR].clear, rear);
    Rs_SetTip(g_wh.sizeLabel, size);
    Rs_SetTip(g_wh.size, size);
    Rs_SetTip(g_wh.sizeValue, size);
    Rs_SetTip(g_wh.animate, anim);
    Rs_SetTip(g_wh.axle, axle);
    for (i = 0; i < WH_AXLE_VALUES; i++) {
        Rs_SetTip(g_wh.axleCaption[i], axle);
        Rs_SetTip(g_wh.axleValue[i], axle);
    }
}

static void Wh_CreateSlot(HWND page, int slot)
{
    struct WhSlot *s = &g_wh.slot[slot];
    const int base = slot == WH_FRONT ? WH_ID_MODEL_LABEL : WH_ID_REAR_LABEL;

    s->label = Rs_Label(page, base, slot == WH_FRONT ? L"Wheel model (GLB/OBJ/PLY)" : L"Rear wheel model (GLB/OBJ/PLY)", RS_FONT_BOLD);
    s->model = Rs_Edit(page, base + 1, L"", 0);
    SendMessageW(s->model, EM_SETCUEBANNER, FALSE,
                 (LPARAM)(slot == WH_FRONT ? L"Optional - a GLB, OBJ or PLY of one wheel" : L"Optional"));
    s->browse = Rs_Button(page, base + 2, L"Browse...");
    s->clear = Rs_Button(page, base + 3, L"Clear");
    Wh_Copy(s->statusText, 1024, slot == WH_FRONT ? WH_TEXT_NONE : L"none - the rear wheels are the wheel model.");
    s->statusLevel = -1;
}

void CharWheels_Create(HWND page, HWND view)
{
    int i;

    memset(&g_wh, 0, sizeof(g_wh));
    g_wh.page = page;
    g_wh.view = view;
    g_wh.sizeNow = WH_SIZE_DEFAULT;
    g_wh.kartWheels = 1;

    g_wh.hint = Rs_PreviewMark(page, WH_ID_HINT);
    // right-aligned in a box as wide as its text (CharWheels_Layout)
    SetWindowLongPtrW(g_wh.hint, GWL_STYLE, GetWindowLongPtrW(g_wh.hint, GWL_STYLE) | SS_RIGHT);
    g_wh.note = Rs_Label(page, WH_ID_NOTE, WH_TEXT_NOTE, RS_FONT_SMALL);
    Wh_CreateSlot(page, WH_FRONT);
    Wh_CreateSlot(page, WH_REAR);
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
    // The axles: the choice in the label column, the three values beside it.
    g_wh.axle = Rs_Combo(page, WH_ID_AXLE);
    SendMessageW(g_wh.axle, CB_ADDSTRING, 0, (LPARAM)L"Front axle");
    SendMessageW(g_wh.axle, CB_ADDSTRING, 0, (LPARAM)L"Rear axle");
    for (i = 0; i < WH_AXLE_VALUES; i++) {
        g_wh.axleCaption[i] = Rs_Label(page, WH_ID_AXLE_CAPTION + i, g_whAxleCaption[i], RS_FONT_SMALL);
        Rs_SetTextColor(g_wh.axleCaption[i], RS_COL_MUTED);
        g_wh.axleValue[i] = Rs_Edit(page, WH_ID_AXLE_VALUE + i, L"0", 0);
        SendMessageW(g_wh.axleValue[i], EM_SETLIMITTEXT, 4, 0);
    }
    Wh_AxleFields();
    g_wh.animate = Rs_Check(page, WH_ID_ANIMATE, L"Animate in the preview (spin and steer)");
    g_wh.wheelsNote = Rs_Label(page, WH_ID_WHEELS_NOTE, WH_TEXT_GAME, RS_FONT_SMALL);
    Rs_SetTextColor(g_wh.note, RS_COL_MUTED);
    Rs_SetTextColor(g_wh.wheelsNote, RS_COL_MUTED);
    Wh_Tips();
    g_wh.created = 1;
}

// A row of a wheel: its label, the field, Browse and Clear.
static void Wh_LaySlot(int slot, int left, int x, int right, int y, int labelW, int fieldW)
{
    struct WhSlot *s = &g_wh.slot[slot];
    const int browseW = Rs_Px(100), clearW = Rs_Px(72);
    int w = fieldW - browseW - clearW - Rs_Px(16);

    MoveWindow(s->label, left, y + Rs_Px(6), labelW, Rs_Px(20), TRUE);
    MoveWindow(s->model, x, y + Rs_Px(2), w > Rs_Px(24) ? w : Rs_Px(24), Rs_Px(28), TRUE);
    MoveWindow(s->browse, right - browseW - clearW - Rs_Px(8), y, browseW, Rs_Px(32), TRUE);
    MoveWindow(s->clear, right - clearW, y, clearW, Rs_Px(32), TRUE);
}

// Card "Wheels" from top between left and right, its fields beside a label
// column at least labelW wide (the page's). Returns the bottom of the card.
// The compact layout leaves out the line at its top (the tooltip of its
// hint says it) and puts the two tick boxes on one row where they fit.
int CharWheels_Layout(HWND page, int left, int right, int top, int labelW, int compact)
{
    RECT card, in;
    int x, y, h, w, fieldW, width, i, cx, valueW, gap;
    HWND labels[3];
    wchar_t *text;

    s_whCompact = compact;
    if (!g_wh.created)
        return top;
    // Nothing is cut short: the label column holds this card's labels too.
    labels[0] = g_wh.slot[WH_FRONT].label;
    labels[1] = g_wh.slot[WH_REAR].label;
    labels[2] = g_wh.sizeLabel;
    for (i = 0; i < 3; i++) {
        text = Rs_GetText(labels[i]);
        w = Rs_TextWidth(labels[i], text) + Rs_Px(4);
        Rs_Free(text);
        if (w > labelW)
            labelW = w;
    }

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
    w = Rs_TextWidth(g_wh.hint, L"Preview feature") + Rs_Px(4);
    MoveWindow(g_wh.hint, in.right - w, card.top + Rs_Px(16), w, Rs_Px(18), TRUE);

    y = in.top;
    if (!compact) {
        h = Wh_TextHeight(g_wh.note, width);
        MoveWindow(g_wh.note, in.left, y, width, h, TRUE);
        y += h + Rs_Px(10);
    }
    Wh_LaySlot(WH_FRONT, in.left, x, in.right, y, labelW, fieldW);
    y += Rs_Px(compact ? 33 : 38);
    Wh_LaySlot(WH_REAR, in.left, x, in.right, y, labelW, fieldW);
    y += Rs_Px(compact ? 33 : 36);
    h = Wh_TextHeight(g_wh.status, fieldW);
    MoveWindow(g_wh.status, x, y, fieldW, h, TRUE);
    y += h + Rs_Px(compact ? 4 : 10);
    MoveWindow(g_wh.sizeLabel, in.left, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(g_wh.size, x - Rs_Px(4), y, fieldW - Rs_Px(64), Rs_Px(30), TRUE);
    MoveWindow(g_wh.sizeValue, in.right - Rs_Px(60), y + Rs_Px(4), Rs_Px(60), Rs_Px(20), TRUE);
    y += Rs_Px(compact ? 30 : 36);

    // The axle: the choice in the label column, "Fwd [ ] Up [ ] Track [ ]".
    MoveWindow(g_wh.axle, in.left, y, labelW, Rs_Px(200), TRUE);
    gap = Rs_Px(6);
    w = 0;
    for (i = 0; i < WH_AXLE_VALUES; i++)
        w += Rs_TextWidth(g_wh.axleCaption[i], g_whAxleCaption[i]) + Rs_Px(4) + gap;
    valueW = (fieldW - w - 2 * Rs_Px(10)) / WH_AXLE_VALUES;
    if (valueW > Rs_Px(64))
        valueW = Rs_Px(64);
    if (valueW < Rs_Px(36))
        valueW = Rs_Px(36);
    cx = x;
    for (i = 0; i < WH_AXLE_VALUES; i++) {
        w = Rs_TextWidth(g_wh.axleCaption[i], g_whAxleCaption[i]) + Rs_Px(4);
        MoveWindow(g_wh.axleCaption[i], cx, y + Rs_Px(6), w, Rs_Px(20), TRUE);
        cx += w + gap;
        MoveWindow(g_wh.axleValue[i], cx, y + Rs_Px(1), valueW, Rs_Px(28), TRUE);
        cx += valueW + Rs_Px(10);
    }
    y += Rs_Px(compact ? 32 : 36);

    // The tick box from the label column.
    w = Rs_CheckBoxWidth(g_wh.animate);
    MoveWindow(g_wh.animate, in.left, y, w < width ? w : width, Rs_Px(24), TRUE);
    y += Rs_Px(compact ? 26 : 30);
    h = Wh_TextHeight(g_wh.wheelsNote, width);
    MoveWindow(g_wh.wheelsNote, in.left, y, width, h, TRUE);
    y += h;
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Wheels");
    return card.bottom;
}

// A click (Browse, Clear) or automation set a wheel's field: char-wheel at
// once, and the page checks after a click (CharWheels_ExportChanged 2).
// Returns as Wh_Check.
static int Wh_SetModelNow(HWND page, int slot, const wchar_t *path)
{
    g_wh.applying = 1;
    Rs_SetText(g_wh.slot[slot].model, path);
    g_wh.applying = 0;
    g_wh.exportChanged = 2;
    Wh_WheelsNote();
    return Wh_Check(page, slot);
}

static void Wh_Browse(HWND page, int slot)
{
    wchar_t start[WH_VAL];
    wchar_t pick[WH_VAL];
    Wh_FieldPath(slot, start, WH_VAL);
    if (!start[0] && slot == WH_REAR)
        Wh_FieldPath(WH_FRONT, start, WH_VAL);
    if (Rs_BrowseOpenFile(Rs_MainWindow(), slot == WH_FRONT ? L"Choose the wheel model" : L"Choose the rear wheel model",
                          L"glTF binary (*.glb) - recommended\0*.glb\0OBJ models (*.obj)\0*.obj\0PLY models (*.ply)\0*.ply\0"
                          L"glTF (*.gltf)\0*.gltf\0All wheel models (*.glb;*.gltf;*.obj;*.ply)\0*.glb;*.gltf;*.obj;*.ply\0"
                          L"All files\0*.*\0\0",
                          start, pick, WH_VAL))
        Wh_SetModelNow(page, slot, pick);
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
    int slot;

    if (id < WH_ID_FIRST || id > WH_ID_LAST)
        return 0;
    if (!g_wh.created)
        return 1;
    slot = id >= WH_ID_REAR_LABEL && id <= WH_ID_REAR_CLEAR ? WH_REAR : WH_FRONT;
    switch (id) {
    case WH_ID_MODEL:
    case WH_ID_REAR:
        if (code == EN_CHANGE && !g_wh.applying) {
            Wh_Schedule(page, slot);
            g_wh.exportChanged = g_wh.exportChanged ? g_wh.exportChanged : 1;
            Wh_WheelsNote();
        }
        break;
    case WH_ID_BROWSE:
    case WH_ID_REAR_BROWSE:
        if (code == BN_CLICKED)
            Wh_Browse(page, slot);
        break;
    case WH_ID_CLEAR:
    case WH_ID_REAR_CLEAR:
        if (code == BN_CLICKED)
            Wh_SetModelNow(page, slot, L"");
        break;
    case WH_ID_ANIMATE:
        if (code == BN_CLICKED)
            Wh_SetAnimation(Wh_IsChecked(g_wh.animate));
        break;
    case WH_ID_AXLE:
        if (code == CBN_SELCHANGE) {
            LRESULT sel = SendMessageW(g_wh.axle, CB_GETCURSEL, 0, 0);
            g_wh.axleShown = sel == 1 ? 1 : 0;
            Wh_AxleFields();
        }
        break;
    default:
        if (id >= WH_ID_AXLE_VALUE && id < WH_ID_AXLE_VALUE + WH_AXLE_VALUES && code == EN_CHANGE && !g_wh.applying) {
            wchar_t *text = Rs_GetText(g_wh.axleValue[id - WH_ID_AXLE_VALUE]);
            wchar_t *end;
            long v = wcstol(text ? text : L"", &end, 10);
            // Half typed ("", "-") changes nothing yet.
            if (text && end != text && !*end)
                Wh_AxleSet(g_wh.axleShown, id - WH_ID_AXLE_VALUE, (int)v, 1);
            Rs_Free(text);
        } else if (id >= WH_ID_AXLE_VALUE && id < WH_ID_AXLE_VALUE + WH_AXLE_VALUES && code == EN_KILLFOCUS) {
            Wh_AxleFields();        // the value clamped, or 0 back in an empty field
        }
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
static int Wh_Ended(struct WhSlot *s, int id, int done)
{
    int i;
    for (i = 0; i < WH_ENDED && s->endedId[i]; i++) {
        if (s->endedId[i] != id)
            continue;
        if (done) {
            Wh_TempDelete(s->endedSeq[i]);
            memmove(&s->endedId[i], &s->endedId[i + 1], sizeof(s->endedId[0]) * (WH_ENDED - 1 - i));
            memmove(&s->endedSeq[i], &s->endedSeq[i + 1], sizeof(s->endedSeq[0]) * (WH_ENDED - 1 - i));
            s->endedId[WH_ENDED - 1] = 0;
            s->endedSeq[WH_ENDED - 1] = 0;
        }
        return 1;
    }
    return 0;
}

LRESULT CharWheels_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    int i, slot;

    *handled = 0;
    switch (msg) {
    case RS_WM_JOB_LINE:
        // Only lines of its own jobs; the page frees all others.
        for (slot = 0; slot < WH_SLOTS; slot++) {
            struct WhSlot *s = &g_wh.slot[slot];
            if (s->jobId && (int)wParam == s->jobId) {
                wchar_t *line = (wchar_t *)lParam;
                if (line)
                    Wh_ParseLine(s, line);
                Rs_Free(line);
                *handled = 1;
                return 0;
            }
            if (Wh_Ended(s, (int)wParam, 0)) {
                Rs_Free((wchar_t *)lParam);
                *handled = 1;
                return 0;
            }
        }
        return 0;
    case RS_WM_JOB_DONE:
        for (slot = 0; slot < WH_SLOTS; slot++) {
            struct WhSlot *s = &g_wh.slot[slot];
            if (s->jobId && (int)wParam == s->jobId) {
                Wh_JobDone(page, slot, (int)lParam);
                *handled = 1;
                return 0;
            }
            if (Wh_Ended(s, (int)wParam, 1)) {
                *handled = 1;
                return 0;
            }
        }
        return 0;
    case WM_TIMER:
        if (wParam == WH_TIMER_CHECK || wParam == WH_TIMER_CHECK_REAR) {
            slot = wParam == WH_TIMER_CHECK ? WH_FRONT : WH_REAR;
            KillTimer(page, wParam);
            g_wh.slot[slot].timer = 0;
            Wh_Check(page, slot);
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
        for (slot = 0; slot < WH_SLOTS; slot++) {
            struct WhSlot *s = &g_wh.slot[slot];
            if (s->timer)
                KillTimer(page, slot == WH_FRONT ? WH_TIMER_CHECK : WH_TIMER_CHECK_REAR);
            if (s->jobId)
                Rs_KillJob(s->jobId);
            Wh_TempDelete(s->jobSeq);
            for (i = 0; i < WH_ENDED; i++)
                Wh_TempDelete(s->endedSeq[i]);
            s->timer = 0;
            s->jobId = 0;
            s->jobSeq = 0;
        }
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
    long v[3];
    int on, r, i;

    if (wcscmp(verb, L"wheel-model") != 0 && wcscmp(verb, L"wheel") != 0 && wcscmp(verb, L"wheel-size") != 0 &&
        wcscmp(verb, L"wheel-turn") != 0 && wcscmp(verb, L"wheel-anim") != 0 && wcscmp(verb, L"wheel-bench") != 0 &&
        wcscmp(verb, L"rear-wheel-model") != 0 && wcscmp(verb, L"rear-wheel") != 0 && wcscmp(verb, L"axle") != 0 &&
        wcscmp(verb, L"wheels-always") != 0)
        return RS_AUTO_UNKNOWN;
    if (!g_wh.created) {
        Rs_AutoLog(L"  %ls: the card Wheels is not there", verb);
        return RS_AUTO_FAIL;
    }

    if (wcscmp(verb, L"wheel-model") == 0 || wcscmp(verb, L"wheel") == 0 || wcscmp(verb, L"rear-wheel-model") == 0 ||
        wcscmp(verb, L"rear-wheel") == 0) {
        const int slot = verb[0] == L'r' ? WH_REAR : WH_FRONT;
        int none = !arg[0] || _wcsicmp(arg, L"none") == 0;
        r = Wh_SetModelNow(page, slot, none ? L"" : arg);
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
    if (wcscmp(verb, L"axle") == 0) {
        int axle;
        if (_wcsnicmp(arg, L"front ", 6) == 0)
            axle = 0;
        else if (_wcsnicmp(arg, L"rear ", 5) == 0)
            axle = 1;
        else
            axle = -1;
        if (axle < 0 || Wh_AutoNumbers(arg + (axle ? 5 : 6), v, 3) != 3) {
            Rs_AutoLog(L"  %ls: say front|rear <forward> <up> <track> in whole model units", verb);
            return RS_AUTO_FAIL;
        }
        for (i = 0; i < WH_AXLE_VALUES; i++)
            Wh_AxleSet(axle, i, (int)v[i], 0);
        g_wh.axleShown = axle;
        Wh_AxleFields();
        UpdateWindow(g_wh.view);      // painted before a following "shot"
        Rs_AutoLog(L"  %ls: %ls %d %d %d (forward, up, track; clamped to %d..%d, %d..%d, %d..%d)", verb, axle ? L"rear" : L"front",
                   g_wh.axleNow[axle][0], g_wh.axleNow[axle][1], g_wh.axleNow[axle][2], g_whAxleMin[0], g_whAxleMax[0],
                   g_whAxleMin[1], g_whAxleMax[1], g_whAxleMin[2], g_whAxleMax[2]);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"wheels-always") == 0) {
        // Gone from the card: it would change nothing in the game.
        Rs_AutoLog(L"  %ls: refused - it has no effect: the game draws your wheels at every distance (nothing is passed)", verb);
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
                   verb, n, pw, ph, g_wh.slot[WH_FRONT].triangles, drawn ? L"" : L" (not drawn: no model or Show kart wheels off)",
                   wholeAvg, wholeMax, wheelsAvg, wheelsMax);
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
    int slot;
    for (slot = 0; slot < WH_SLOTS; slot++)
        if (g_wh.slot[slot].jobId != 0 || g_wh.slot[slot].timer)
            return 1;
    return 0;
}

// The card does not depend on the character model: its wheels stay in the
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
    int slot, changed = 0;
    if (!g_wh.created)
        return 0;
    for (slot = 0; slot < WH_SLOTS; slot++) {
        struct WhSlot *s = &g_wh.slot[slot];
        if (!s->stampKnown || s->jobId || s->timer || !s->checked[0])
            continue;
        if (Wh_StampNow(s) == s->stamp)
            continue;
        g_wh.generation++;
        if (Rs_Automating())
            Rs_AutoLog(L"  %ls files: written since the last reading (%ls) - reading the %ls again", slot == WH_FRONT ? L"wheel" : L"rear wheel",
                       why, slot == WH_FRONT ? L"wheel" : L"rear wheel");
        // As a click: the wheel is read at once, the page checks again.
        g_wh.exportChanged = 2;
        Wh_Check(page, slot);
        changed = 1;
    }
    return changed;
}

int CharWheels_Generation(void)
{
    return g_wh.generation;
}

int CharWheels_MissingTextures(wchar_t *out, int cap)
{
    int i, slot, count = 0;
    if (out && cap > 0)
        out[0] = 0;
    for (slot = 0; slot < WH_SLOTS; slot++) {
        const struct WhSlot *s = &g_wh.slot[slot];
        // The rear wheel's only while it is in the view.
        if (slot == WH_REAR && !s->loaded)
            continue;
        for (i = 0; i < s->missingCount; i++, count++) {
            if (out && cap > 0) {
                if (count)
                    Wh_Append(out, cap, L", ");
                Wh_Append(out, cap, s->missing[i]);
            }
        }
    }
    return g_wh.created ? count : 0;
}

int CharWheels_Hidden(HWND control)
{
    return g_wh.created && control == g_wh.note && s_whCompact;
}

int CharWheels_Shown(void)
{
    return g_wh.created && g_wh.slot[WH_FRONT].loaded;
}

int CharWheels_ModelPath(wchar_t *out, int cap)
{
    wchar_t path[WH_VAL];
    if (!g_wh.created)
        return 0;
    Wh_FieldPath(WH_FRONT, path, WH_VAL);
    if (out && cap > 0)
        Wh_Copy(out, cap, path);
    return path[0] != 0;
}

int CharWheels_RearModelPath(wchar_t *out, int cap)
{
    wchar_t path[WH_VAL];
    if (!g_wh.created)
        return 0;
    Wh_FieldPath(WH_REAR, path, WH_VAL);
    if (out && cap > 0)
        Wh_Copy(out, cap, path);
    return path[0] != 0;
}

int CharWheels_SizePercent(void)
{
    return g_wh.sizeNow;
}

int CharWheels_Axle(int axle, int value[3])
{
    int i, set = 0;
    for (i = 0; i < WH_AXLE_VALUES; i++) {
        const int v = g_wh.created && (axle == 0 || axle == 1) ? g_wh.axleNow[axle][i] : 0;
        if (value)
            value[i] = v;
        set |= v != 0;
    }
    return set;
}

int CharWheels_ExportChanged(void)
{
    const int changed = g_wh.exportChanged;
    g_wh.exportChanged = 0;
    return changed;
}

void CharWheels_PageState(int obj, int kartWheels, int nativeOn, int passed, int upZ, int backwards)
{
    int slot;
    // New axes turn the wheels as well: read them again.
    if (g_wh.created && ((upZ != 0) != g_wh.upZ || (backwards != 0) != g_wh.backwards)) {
        g_wh.upZ = upZ != 0;
        g_wh.backwards = backwards != 0;
        for (slot = 0; slot < WH_SLOTS; slot++)
            if (g_wh.slot[slot].checked[0] || g_wh.slot[slot].jobId)
                Wh_Schedule(g_wh.page, slot);
    }
    g_wh.obj = obj != 0;
    g_wh.kartWheels = kartWheels != 0;
    g_wh.nativeOn = nativeOn != 0;
    g_wh.passed = passed != 0;
    Wh_WheelsNote();
}

void CharWheels_SetTextures(const wchar_t *dir)
{
    int slot;
    if (!g_wh.created || wcscmp(dir ? dir : L"", g_wh.textures) == 0)
        return;
    Wh_Copy(g_wh.textures, WH_VAL, dir);
    // Another folder may hold the wheels' textures: read them again.
    for (slot = 0; slot < WH_SLOTS; slot++) {
        wchar_t path[WH_VAL];
        Wh_FieldPath(slot, path, WH_VAL);
        if (path[0])
            Wh_Schedule(g_wh.page, slot);
    }
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
    const struct WhSlot *fr = &g_wh.slot[WH_FRONT], *re = &g_wh.slot[WH_REAR];
    wchar_t *text;
    int a;

    if (!f || !g_wh.created)
        return;
    Wh_Put(f, L"wheels card: enabled (preview feature)");
    text = Rs_GetText(g_wh.hint);
    Wh_Put(f, L"wheels hint: %ls", text);
    Rs_Free(text);
    Wh_Put(f, L"wheels fields: model %ls, browse %ls, clear %ls, size %ls, animate %ls, rear model %ls, axle %ls",
           Wh_EnabledWord(fr->model), Wh_EnabledWord(fr->browse), Wh_EnabledWord(fr->clear), Wh_EnabledWord(g_wh.size),
           Wh_EnabledWord(g_wh.animate), Wh_EnabledWord(re->model), Wh_EnabledWord(g_wh.axleValue[0]));
    text = Rs_GetText(fr->model);
    Wh_Put(f, L"wheel model: %ls", text && text[0] ? text : L"none");
    Rs_Free(text);
    Wh_Put(f, L"wheel model (last run): %ls", fr->checked[0] ? fr->checked : L"(none)");
    text = Rs_GetText(re->model);
    Wh_Put(f, L"rear wheel model: %ls", text && text[0] ? text : L"none");
    Rs_Free(text);
    Wh_Put(f, L"rear wheel model (last run): %ls", re->checked[0] ? re->checked : L"(none)");
    Wh_Put(f, L"wheel textures folder: %ls", g_wh.textures[0] ? g_wh.textures : L"(none)");
    Wh_Put(f, L"wheel status: %ls", g_wh.statusText);
    Wh_Put(f, L"wheel warnings: %d", fr->warnings + re->warnings);
    {
        wchar_t names[400];
        int n = CharWheels_MissingTextures(names, 400);
        Wh_Put(f, L"wheel textures missing: %d%ls%ls", n, n ? L" " : L"", names);
    }
    Wh_Put(f, L"wheel files stamped: %d files, %d folders%ls", fr->fileCount + (fr->checked[0] ? 1 : 0), fr->folderCount,
           fr->stampKnown ? L"" : L" (no stamp)");
    Wh_Put(f, L"rear wheel files stamped: %d files, %d folders%ls", re->fileCount + (re->checked[0] ? 1 : 0), re->folderCount,
           re->stampKnown ? L"" : L" (no stamp)");
    Wh_Put(f, L"wheel in the preview: %ls", fr->loaded ? L"yes" : L"no");
    Wh_Put(f, L"rear wheel in the preview: %ls", re->loaded ? L"yes" : L"no");
    Wh_Put(f, L"wheel size: %d %%", g_wh.sizeNow);
    for (a = 0; a < 2; a++) {
        int vd = 0, vy = 0, vt = 0;
        RsView_GetAxle(g_wh.view, a == 0 ? RS_VIEW_AXLE_FRONT : RS_VIEW_AXLE_REAR, &vd, &vy, &vt);
        Wh_Put(f, L"axle %ls: %d %d %d (forward, up, track; model units) - preview %d %d %d (1/16, track per wheel)",
               a == 0 ? L"front" : L"rear", g_wh.axleNow[a][0], g_wh.axleNow[a][1], g_wh.axleNow[a][2], vd, vy, vt);
    }
    Wh_Put(f, L"axle shown: %ls", g_wh.axleShown ? L"rear" : L"front");
    Wh_Put(f, L"wheel preview: spin %d, steer %d, animation %ls", g_wh.spin, g_wh.steer, g_wh.anim ? L"on" : L"off");
    Wh_Put(f, L"wheel export: %ls", g_wh.passed ? L"passed as --wheel-model (with the native model)" : L"not passed");
    {
        wchar_t extra[200];
        int v[3];
        extra[0] = 0;
        if (g_wh.passed && CharWheels_RearModelPath(NULL, 0))
            Wh_Append(extra, 200, L" --rear-wheel-model");
        if (g_wh.passed && CharWheels_Axle(0, v))
            Wh_Append(extra, 200, L" --axle-front");
        if (g_wh.passed && CharWheels_Axle(1, v))
            Wh_Append(extra, 200, L" --axle-rear");
        Wh_Put(f, L"wheel export extras: %ls", extra[0] ? extra + 1 : L"none");
    }
    Wh_Put(f, L"wheel in the preview only: %ls", fr->loaded && g_wh.kartWheels && !g_wh.passed ? L"yes (shown, not built)" : L"no");
    text = Rs_GetText(g_wh.wheelsNote);
    Wh_Put(f, L"wheels note: %ls", text ? text : L"");
    Rs_Free(text);
}
