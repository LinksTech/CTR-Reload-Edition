// rs_wheels.c - the card "Wheels" of the page Character (preview feature)
//
// Wheels of one's own instead of the game's kart wheels: a PLY of one wheel,
// its size, and whether the preview turns and steers it. It is unfinished:
// nothing of it reaches a character. rldpack has no switch for it in make-char,
// and this card never touches the arguments of the page's check and build
// (rs_char.c, Char_MakeArgs) - its only job is `rldpack char-wheel`, which
// writes the wheel for the 3D preview and refuses to write a container
// (tools/rldpack_wheel.inc).
//
// LOCKED. Without --enable-preview-features (g_rsPreviewFeatures) the card is
// shown with every field greyed out, "Coming soon" in its title line and a
// tooltip per field. Every path below checks the switch itself - a posted
// WM_COMMAND or an automation verb gets past EnableWindow: no job, no dialog,
// no call into the preview, no setting. Even with the switch the card stores
// nothing in the settings. The switch changes only the enabled state and the
// texts of hint and tooltips, never a rectangle.
//
// With the switch: choosing a PLY runs char-wheel (600 ms after the last
// change, like the page's own check); its preview file goes to the view
// (RsView_LoadWheelModel), which draws it at the dummy's four wheel points,
// and is deleted at once. Size, turn and animation are only view settings.
// "Show kart wheels" (rs_char.c) stays the master: off = no wheels at all.
//
// Automation verbs (all of them "locked - coming soon" without the switch):
//   wheel-model <ply|none>      (also "wheel") choose the wheel; waits for char-wheel
//   wheel-size <50..200>        percent of the game's wheel
//   wheel-turn <spin> <steer>   fixed angles in whole degrees; stops the animation
//   wheel-anim on|off           the animation of the preview (a timer of the view)

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

#define WH_TEXT_NOTE        L"Wheels of your own instead of the game's kart wheels."
#define WH_TEXT_WHEELS      L"'Show kart wheels' on the tab Model stays as it is: off = no wheels at all."
#define WH_TEXT_NONE        L"No wheel model chosen."

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
    COLORREF statusColor;
    wchar_t checked[WH_VAL];    // the PLY of the last char-wheel
    wchar_t statusText[512];
    // the running job
    int previewOk, triangles;
    wchar_t across[16], width[16], firstError[400];
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

static void Wh_Status(const wchar_t *text, COLORREF color)
{
    RECT rc;

    Wh_Copy(g_wh.statusText, 512, text);
    g_wh.statusColor = color;
    Rs_SetText(g_wh.status, text);
    Rs_SetTextColor(g_wh.status, color);
    GetWindowRect(g_wh.status, &rc);
    if (rc.right > rc.left && Wh_TextHeight(g_wh.status, rc.right - rc.left) != rc.bottom - rc.top)
        Wh_Relayout();
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
// The check: rldpack char-wheel (only with the switch)
// ---------------------------------------------------------------------------

// The wheel leaves the view: the game's wheels are drawn again.
static void Wh_Drop(void)
{
    if (g_wh.loaded)
        RsView_DropWheelModel(g_wh.view);
    g_wh.loaded = 0;
}

// Starts char-wheel for the PLY in the field. 1 = started,
// 0 = nothing to check (empty field: the wheel is dropped),
// -1 = rldpack could not be started. A char-wheel still running is ended
// first (a large wheel takes long); its file goes when it is gone.
static int Wh_Check(HWND page)
{
    wchar_t path[WH_VAL], preview[WH_VAL];
    const wchar_t *args[8];

    if (!g_rsPreviewFeatures)
        return 0;
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
    g_wh.previewOk = 0;
    g_wh.triangles = -1;
    g_wh.across[0] = 0;
    g_wh.width[0] = 0;
    g_wh.firstError[0] = 0;
    g_wh.jobId = Rs_RunRldpack(page, args, 6);
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
    if (!g_rsPreviewFeatures || g_wh.applying)
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
    } else if (wcscmp(f[0], L"file") == 0 && wcscmp(Wh_Field(f, n, 1), L"preview") == 0) {
        g_wh.previewOk = wcscmp(Wh_Field(f, n, 2), L"ok") == 0;
    } else if (wcscmp(f[0], L"msg") == 0 && wcscmp(Wh_Field(f, n, 1), L"error") == 0 && !g_wh.firstError[0]) {
        Wh_Copy(g_wh.firstError, 400, Wh_Field(f, n, 3));
    }
}

static void Wh_JobDone(HWND page, int code)
{
    wchar_t preview[WH_VAL], text[512];
    int seq = g_wh.jobSeq;

    g_wh.jobId = 0;
    g_wh.jobSeq = 0;
    (void)page;
    Wh_TempPath(preview, WH_VAL, seq);
    if (code == 0 && g_wh.previewOk && RsView_LoadWheelModel(g_wh.view, preview)) {
        g_wh.loaded = 1;
        RsView_SetWheelScale(g_wh.view, g_wh.sizeNow);
        RsView_SetWheelTurn(g_wh.view, g_wh.spin, g_wh.steer);
        RsView_SetWheelAnimation(g_wh.view, g_wh.anim);
        swprintf(text, 512, L"%d triangles, %ls across, %ls wide (game units) - shown at the four wheel points of the kart.",
                 g_wh.triangles, g_wh.across[0] ? g_wh.across : L"?", g_wh.width[0] ? g_wh.width : L"?");
        Wh_Status(text, RS_COL_TEXT);
    } else {
        Wh_Drop();
        if (g_wh.firstError[0])
            swprintf(text, 512, L"Not shown: %ls", g_wh.firstError);
        else if (code == 0)
            swprintf(text, 512, L"Not shown: the preview file could not be read.");
        else
            swprintf(text, 512, L"Not shown: rldpack ended with code %d.", code);
        Wh_Status(text, RS_COL_ERROR);
    }
    Wh_TempDelete(seq);
}

// ---------------------------------------------------------------------------
// The card
// ---------------------------------------------------------------------------

static void Wh_Tips(void)
{
    const int on = g_rsPreviewFeatures;
    const wchar_t *model = on ? L"A PLY of one wheel: axle along X, outer side toward +X. The preview places it at the four wheel "
                                L"points of the kart; it is not packed into the character yet."
                              : L"Later: a PLY of one wheel. It is placed at the four wheel points of the kart.";
    const wchar_t *size = on ? L"The size of your wheels in the preview, 100 % = the game's wheel."
                             : L"Later: the size of your wheels, 100 % = the game's wheel.";
    const wchar_t *anim = on ? L"Turns and steers the wheels in the preview." : L"Later: turns and steers the wheels in the preview.";

    Rs_SetTip(g_wh.hint, on ? L"Unfinished: shown in the preview only, nothing of it is written into a character."
                            : L"Start Reload Studio with --enable-preview-features to try it. Nothing of it is written into a character yet.");
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
    const int on = g_rsPreviewFeatures;
    HWND inputs[5];
    HWND muted[4];
    int i;

    memset(&g_wh, 0, sizeof(g_wh));
    g_wh.page = page;
    g_wh.view = view;
    g_wh.sizeNow = WH_SIZE_DEFAULT;

    g_wh.hint = Rs_ComingSoon(page, WH_ID_HINT);
    // right-aligned in a box as wide as the longer of its two texts: the same
    // rectangle with and without the switch
    SetWindowLongPtrW(g_wh.hint, GWL_STYLE, GetWindowLongPtrW(g_wh.hint, GWL_STYLE) | SS_RIGHT);
    g_wh.note = Rs_Label(page, WH_ID_NOTE, WH_TEXT_NOTE, RS_FONT_SMALL);
    g_wh.modelLabel = Rs_Label(page, WH_ID_MODEL_LABEL, L"Wheel model (PLY)", RS_FONT_BOLD);
    g_wh.model = Rs_Edit(page, WH_ID_MODEL, L"", 0);
    SendMessageW(g_wh.model, EM_SETCUEBANNER, FALSE, (LPARAM)(on ? L"Optional - a PLY of one wheel" : L"Coming soon"));
    g_wh.browse = Rs_Button(page, WH_ID_BROWSE, L"Browse...");
    g_wh.clear = Rs_Button(page, WH_ID_CLEAR, L"Clear");
    g_wh.status = Rs_Label(page, WH_ID_STATUS, WH_TEXT_NONE, RS_FONT_SMALL);
    g_wh.statusColor = RS_COL_MUTED;
    Rs_SetTextColor(g_wh.status, g_wh.statusColor);
    Wh_Copy(g_wh.statusText, 512, WH_TEXT_NONE);
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
    g_wh.wheelsNote = Rs_Label(page, WH_ID_WHEELS_NOTE, WH_TEXT_WHEELS, RS_FONT_SMALL);
    Rs_SetTextColor(g_wh.note, RS_COL_MUTED);
    Rs_SetTextColor(g_wh.wheelsNote, RS_COL_MUTED);

    // Locked: the inputs greyed out; the labels stay enabled (a disabled
    // static is drawn embossed) but muted.
    inputs[0] = g_wh.model;
    inputs[1] = g_wh.browse;
    inputs[2] = g_wh.clear;
    inputs[3] = g_wh.size;
    inputs[4] = g_wh.animate;
    muted[0] = g_wh.modelLabel;
    muted[1] = g_wh.sizeLabel;
    muted[2] = g_wh.sizeValue;
    muted[3] = g_wh.animate;
    if (!on) {
        for (i = 0; i < 5; i++)
            EnableWindow(inputs[i], FALSE);
        for (i = 0; i < 4; i++)
            Rs_SetTextColor(muted[i], RS_COL_MUTED);
    }
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

static void Wh_Browse(void)
{
    wchar_t start[WH_VAL];
    wchar_t pick[WH_VAL];
    Wh_FieldPath(start, WH_VAL);
    if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the wheel model", L"PLY models (*.ply)\0*.ply\0All files\0*.*\0\0", start, pick, WH_VAL))
        Rs_SetText(g_wh.model, pick);      // EN_CHANGE schedules the check
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
    // Locked: taken and dropped, whatever sent it.
    if (!g_rsPreviewFeatures || !g_wh.created)
        return 1;
    switch (id) {
    case WH_ID_MODEL:
        if (code == EN_CHANGE)
            Wh_Schedule(page);
        break;
    case WH_ID_BROWSE:
        if (code == BN_CLICKED)
            Wh_Browse();
        break;
    case WH_ID_CLEAR:
        if (code == BN_CLICKED)
            Rs_SetText(g_wh.model, L"");
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
            if (g_rsPreviewFeatures)
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
        wcscmp(verb, L"wheel-turn") != 0 && wcscmp(verb, L"wheel-anim") != 0)
        return RS_AUTO_UNKNOWN;
    if (!g_rsPreviewFeatures || !g_wh.created) {
        Rs_AutoLog(L"  %ls: locked - coming soon", verb);
        return RS_AUTO_FAIL;
    }

    if (wcscmp(verb, L"wheel-model") == 0 || wcscmp(verb, L"wheel") == 0) {
        int none = !arg[0] || _wcsicmp(arg, L"none") == 0;
        g_wh.applying = 1;
        Rs_SetText(g_wh.model, none ? L"" : arg);
        g_wh.applying = 0;
        r = Wh_Check(page);
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
    Wh_Put(f, L"wheels card: %ls", g_rsPreviewFeatures ? L"enabled (preview feature)" : L"locked - coming soon");
    text = Rs_GetText(g_wh.hint);
    Wh_Put(f, L"wheels hint: %ls", text);
    Rs_Free(text);
    Wh_Put(f, L"wheels fields: model %ls, browse %ls, clear %ls, size %ls, animate %ls", Wh_EnabledWord(g_wh.model),
           Wh_EnabledWord(g_wh.browse), Wh_EnabledWord(g_wh.clear), Wh_EnabledWord(g_wh.size), Wh_EnabledWord(g_wh.animate));
    text = Rs_GetText(g_wh.model);
    Wh_Put(f, L"wheel model: %ls", g_rsPreviewFeatures && text && text[0] ? text : L"none");
    Rs_Free(text);
    Wh_Put(f, L"wheel model (last run): %ls", g_wh.checked[0] ? g_wh.checked : L"(none)");
    Wh_Put(f, L"wheel status: %ls", g_wh.statusText);
    Wh_Put(f, L"wheel in the preview: %ls", g_wh.loaded ? L"yes" : L"no");
    Wh_Put(f, L"wheel size: %d %%", g_wh.sizeNow);
    Wh_Put(f, L"wheel preview: spin %d, steer %d, animation %ls", g_wh.spin, g_wh.steer, g_wh.anim ? L"on" : L"off");
}
