// rs_anim.c - the card "Animations" of the page Character (preview feature)
//
// Poses of the author's own for the driver's animations: a folder with one
// PLY per pose (turn_left, turn_right, reverse, bump, jump, idle), each the
// model's own mesh with moved vertices. It is unfinished: nothing of it
// reaches a character. make-char has no switch for poses, and this card never
// touches the arguments of the page's check and build (rs_char.c,
// Char_MakeArgs) - its only job is `rldpack char-poses`, which checks the
// poses against the model, places them as the model is placed and writes
// them for the 3D preview; it refuses to write a container
// (tools/rldpack_anim.inc).
//
// LOCKED. Without --enable-preview-features (g_rsPreviewFeatures) the card is
// shown with every field greyed out, "Coming soon" in its title line and a
// tooltip per field. Every path below checks the switch itself - a posted
// WM_COMMAND or an automation verb gets past EnableWindow: no job, no dialog,
// no call into the preview, no setting. Even with the switch the card stores
// nothing in the settings. The switch changes only the enabled state and the
// texts of hint, cue and tooltips, never a rectangle. "Mark body parts..."
// stays greyed out with the switch too: it is a later step.
//
// With the switch: choosing a folder runs char-poses for the model of the
// page's last check (with its size and its options "Show kart wheels" and
// "Repair the model", which move the model); every later check of the page
// runs it again. The list shows the six poses with their animation slot and
// what rldpack found; the file goes to the view (RsView_LoadPoseSet) and is
// deleted at once. "Show pose" (or a click into the list) shows one of them
// instead of the built model - before the reduction, as read.
//
// Automation verbs (all of them "locked - coming soon" without the switch):
//   anim-folder <folder|none>   (also "poses") choose the folder; waits for char-poses
//   anim-show <pose|model>      a pose by name (turn_left ...) or number (0..5),
//                               "model" = the built model again

#include "rs_anim.h"
#include "rs_view.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Controls (340-379 belong to this card on the page Character)
#define AN_ID_HINT          340
#define AN_ID_NOTE          341
#define AN_ID_FOLDER_LABEL  342
#define AN_ID_FOLDER        343
#define AN_ID_BROWSE        344
#define AN_ID_CLEAR         345
#define AN_ID_LIST          346
#define AN_ID_SHOW_LABEL    347
#define AN_ID_SHOW          348
#define AN_ID_MARK          349
#define AN_ID_BEFORE        350
#define AN_ID_STATUS        351
#define AN_ID_FIRST         340
#define AN_ID_LAST          379

// The check boxes of the page (rs_char.c) whose switches move the model.
#define AN_PAGE_ID_WHEELS   116     // "Show kart wheels": --wheels off when clear
#define AN_PAGE_ID_REPAIR   160     // "Repair the model": --repair off when clear
#define AN_PAGE_ID_REMESH   162     // "Closed hull (remesh)": --remesh on when ticked and usable

#define AN_VAL              1024
#define AN_POSES            6       // in the order of the preview file (RLDPS1)
#define AN_LIST_ROWS        4       // rows of the pose list shown at once
#define AN_NOTE_LINES       3

#define AN_TEXT_NOTE        L"Poses of your own for steering and the other animations."
#define AN_TEXT_BEFORE      L"Poses are shown before the reduction and are not packed yet."
#define AN_TEXT_NONE        L"No pose folder: the automatic poses are used."
#define AN_TEXT_NO_MODEL    L"Choose a model first: the poses are checked against it."

// The poses as char-poses names them, their animation slot (the slots of a
// driver model: turn 21 frames, reverse 7, bump 15, jump 4) and their entry in "Show pose".
static const struct {
    const wchar_t *name;
    const wchar_t *slot;
    const wchar_t *show;
} g_anPoses[AN_POSES] = {
    { L"turn_left", L"steering, turn frame 0 of 21 (full left)", L"Steer left (turn_left)" },
    { L"turn_right", L"steering, turn frame 20 of 21 (full right)", L"Steer right (turn_right)" },
    { L"reverse", L"reverse, its last frame of 7", L"Reverse (reverse)" },
    { L"bump", L"bump, held through 15 frames", L"Bump (bump)" },
    { L"jump", L"jump, its last frame of 4", L"Jump (jump)" },
    { L"idle", L"no animation of the game yet", L"Idle (idle, not used yet)" },
};

static struct {
    HWND page, view;
    HWND hint, note, folderLabel, folder, browse, clear, list, showLabel, show, mark, before, status;
    int created;
    int jobId, jobSeq, seq;
    int again;                  // folder or model changed while char-poses ran: run again after it
    int loaded;                 // a pose set is in the view
    int showPose;               // the pose the view shows, -1 = the built model
    int sizeNow;
    COLORREF statusColor;
    wchar_t model[AN_VAL];      // the model of the page's last check, "" = none
    wchar_t checked[AN_VAL];    // the folder of the last char-poses
    wchar_t statusText[512];
    // per pose: what the last char-poses said
    wchar_t state[AN_POSES][16];
    wchar_t file[AN_POSES][260];
    wchar_t rule[AN_POSES][32];
    int found, shown;
    // the running job
    int previewOk, errors, warnings;
    wchar_t firstError[400], firstWarning[400];
} g_an;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void An_Copy(wchar_t *out, int cap, const wchar_t *in)
{
    if (cap <= 0)
        return;
    wcsncpy(out, in ? in : L"", (size_t)cap - 1);
    out[cap - 1] = 0;
}

// Text of the folder field without quotation marks and spaces at the edges.
static void An_FieldPath(wchar_t *out, int cap)
{
    wchar_t *text = Rs_GetText(g_an.folder);
    const wchar_t *p = text ? text : L"";
    size_t n;

    while (*p == L' ' || *p == L'\t' || *p == L'"')
        p++;
    An_Copy(out, cap, p);
    n = wcslen(out);
    while (n > 0 && (out[n - 1] == L' ' || out[n - 1] == L'\t' || out[n - 1] == L'"'))
        out[--n] = 0;
    Rs_Free(text);
}

// Height of a wrapping label at this width: its lines, at least one, at most
// AN_NOTE_LINES.
static int An_TextHeight(HWND label, int width)
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
        if (tm.tmHeight > 0 && h > tm.tmHeight * AN_NOTE_LINES)
            h = tm.tmHeight * AN_NOTE_LINES;
        SelectObject(dc, old);
        ReleaseDC(label, dc);
    }
    Rs_Free(text);
    return h > Rs_Px(18) ? h : Rs_Px(18);
}

// The page laid out again (a note changed its number of lines).
static void An_Relayout(void)
{
    RECT rc;
    if (!g_an.page)
        return;
    GetClientRect(g_an.page, &rc);
    if (rc.right > 0 && rc.bottom > 0 && g_rsCharPage.layout) {
        g_rsCharPage.layout(g_an.page, rc.right, rc.bottom);
        InvalidateRect(g_an.page, NULL, TRUE);
    }
}

static void An_Status(const wchar_t *text, COLORREF color)
{
    RECT rc;

    An_Copy(g_an.statusText, 512, text);
    g_an.statusColor = color;
    Rs_SetText(g_an.status, text);
    Rs_SetTextColor(g_an.status, color);
    GetWindowRect(g_an.status, &rc);
    if (rc.right > rc.left && An_TextHeight(g_an.status, rc.right - rc.left) != rc.bottom - rc.top)
        An_Relayout();
}

static int An_PageChecked(int id, int otherwise)
{
    HWND box = g_an.page ? GetDlgItem(g_an.page, id) : NULL;
    return box ? SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED : otherwise;
}

// Its own temporary file: <Rs_TempDir>\anim-<process>-<number>.rldps.
static void An_TempPath(wchar_t *out, int cap, int seq)
{
    wchar_t dir[AN_VAL];
    wchar_t name[64];

    Rs_TempDir(dir, AN_VAL);
    swprintf(name, 64, L"anim-%lu-%d.rldps", (unsigned long)GetCurrentProcessId(), seq);
    Rs_PathJoin(out, cap, dir, name);
}

static void An_TempDelete(int seq)
{
    wchar_t path[AN_VAL];
    if (seq <= 0)
        return;
    An_TempPath(path, AN_VAL, seq);
    DeleteFileW(path);
}

// A pose the preview file carries: ok, unused (idle) or automatic (a turn
// pose without its partner - still shown).
static int An_Shown(int pose)
{
    const wchar_t *s = g_an.state[pose];
    return wcscmp(s, L"ok") == 0 || wcscmp(s, L"unused") == 0 || wcscmp(s, L"automatic") == 0;
}

// ---------------------------------------------------------------------------
// The list of poses and the choice of the view
// ---------------------------------------------------------------------------

static void An_StateText(int pose, wchar_t *out, int cap)
{
    const wchar_t *s = g_an.state[pose];

    if (wcscmp(s, L"ok") == 0)
        swprintf(out, cap, L"from %ls", g_an.file[pose]);
    else if (wcscmp(s, L"unused") == 0)
        swprintf(out, cap, L"%ls checked, not used yet", g_an.file[pose]);
    else if (wcscmp(s, L"automatic") == 0)
        swprintf(out, cap, L"automatic - %ls has no partner", g_an.file[pose]);
    else if (wcscmp(s, L"bad") == 0)
        swprintf(out, cap, L"error in %ls: %ls", g_an.file[pose], g_an.rule[pose]);
    else
        An_Copy(out, cap, L"automatic");
}

static void An_FillList(void)
{
    wchar_t row[600], state[400];
    int p;

    SendMessageW(g_an.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_an.list, LB_RESETCONTENT, 0, 0);
    for (p = 0; p < AN_POSES; p++) {
        An_StateText(p, state, 400);
        swprintf(row, 600, L"%ls - %ls: %ls", g_anPoses[p].name, g_anPoses[p].slot, state);
        SendMessageW(g_an.list, LB_ADDSTRING, 0, (LPARAM)row);
    }
    SendMessageW(g_an.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_an.list, NULL, TRUE);
}

// "Show pose": the built model, then every pose the view has; the item data
// is the pose (-1 = the built model).
static void An_FillShow(void)
{
    int p, i, at = 0;

    SendMessageW(g_an.show, CB_RESETCONTENT, 0, 0);
    i = (int)SendMessageW(g_an.show, CB_ADDSTRING, 0, (LPARAM)L"The built model");
    SendMessageW(g_an.show, CB_SETITEMDATA, (WPARAM)i, (LPARAM)-1);
    for (p = 0; g_an.loaded && p < AN_POSES; p++) {
        if (!An_Shown(p))
            continue;
        i = (int)SendMessageW(g_an.show, CB_ADDSTRING, 0, (LPARAM)g_anPoses[p].show);
        SendMessageW(g_an.show, CB_SETITEMDATA, (WPARAM)i, (LPARAM)p);
        if (p == g_an.showPose)
            at = i;
    }
    SendMessageW(g_an.show, CB_SETCURSEL, (WPARAM)at, 0);
}

// Shows a pose in the view (-1 = the built model). 0 = that pose is not there.
static int An_ShowPose(int pose)
{
    if (!g_rsPreviewFeatures)
        return 0;
    if (pose >= 0 && (pose >= AN_POSES || !g_an.loaded || !An_Shown(pose)))
        return 0;
    g_an.showPose = pose;
    if (g_an.loaded)
        RsView_ShowPoseSet(g_an.view, pose);
    An_FillShow();
    if (pose >= 0)
        SendMessageW(g_an.list, LB_SETCURSEL, (WPARAM)pose, 0);
    else
        SendMessageW(g_an.list, LB_SETCURSEL, (WPARAM)-1, 0);
    return 1;
}

// The poses leave the view: the built model is drawn again.
static void An_Drop(void)
{
    if (g_an.loaded)
        RsView_DropPoseSet(g_an.view);
    g_an.loaded = 0;
    g_an.showPose = -1;
}

static void An_Forget(void)
{
    int p;
    for (p = 0; p < AN_POSES; p++) {
        g_an.state[p][0] = 0;
        g_an.file[p][0] = 0;
        g_an.rule[p][0] = 0;
    }
    g_an.found = 0;
    g_an.shown = 0;
}

// ---------------------------------------------------------------------------
// The check: rldpack char-poses (only with the switch)
// ---------------------------------------------------------------------------

// Starts char-poses for the folder in the field and the page's model. 1 =
// started (or queued behind the running one), 0 = nothing to check (no
// folder or no model: the poses leave the view), -1 = rldpack could not be
// started.
static int An_Check(HWND page)
{
    wchar_t folder[AN_VAL], preview[AN_VAL], size[16];
    const wchar_t *args[16];
    int n = 0;

    if (!g_rsPreviewFeatures || !g_an.created)
        return 0;
    if (g_an.jobId) {
        g_an.again = 1;
        return 1;
    }
    An_FieldPath(folder, AN_VAL);
    An_Copy(g_an.checked, AN_VAL, folder);
    if (!folder[0] || !g_an.model[0]) {
        An_Drop();
        An_Forget();
        An_FillList();
        An_FillShow();
        An_Status(folder[0] ? AN_TEXT_NO_MODEL : AN_TEXT_NONE, RS_COL_MUTED);
        return 0;
    }

    g_an.jobSeq = ++g_an.seq;
    An_TempPath(preview, AN_VAL, g_an.jobSeq);
    DeleteFileW(preview);
    swprintf(size, 16, L"%d", g_an.sizeNow);
    args[n++] = L"char-poses";
    args[n++] = L"--machine";
    args[n++] = L"--model";
    args[n++] = g_an.model;
    args[n++] = L"--pose-dir";
    args[n++] = folder;
    args[n++] = L"--size";
    args[n++] = size;
    if (!An_PageChecked(AN_PAGE_ID_WHEELS, 1)) {
        args[n++] = L"--wheels";
        args[n++] = L"off";
    }
    if (!An_PageChecked(AN_PAGE_ID_REPAIR, 1)) {
        args[n++] = L"--repair";
        args[n++] = L"off";
    }
    // As the page passes it: only while the box is usable (it needs "Reduce
    // automatically"). char-poses then refuses the poses (pose-remesh).
    if (An_PageChecked(AN_PAGE_ID_REMESH, 0) && IsWindowEnabled(GetDlgItem(page, AN_PAGE_ID_REMESH))) {
        args[n++] = L"--remesh";
        args[n++] = L"on";
    }
    args[n++] = L"--preview";
    args[n++] = preview;
    g_an.previewOk = 0;
    g_an.errors = 0;
    g_an.warnings = 0;
    g_an.firstError[0] = 0;
    g_an.firstWarning[0] = 0;
    An_Forget();
    g_an.jobId = Rs_RunRldpack(page, args, n);
    if (!g_an.jobId) {
        g_an.jobSeq = 0;
        An_Status(L"rldpack could not be started.", RS_COL_ERROR);
        return -1;
    }
    An_Status(L"Checking the poses...", RS_COL_MUTED);
    return 1;
}

static const wchar_t *An_Field(wchar_t **f, int n, int i)
{
    return i < n && f[i] ? f[i] : L"";
}

static void An_ParseLine(wchar_t *line)
{
    wchar_t *f[16];
    int n = Rs_SplitMachine(line, f, 16);
    int p;

    if (n <= 0)
        return;
    if (wcscmp(f[0], L"char") == 0 && wcscmp(An_Field(f, n, 1), L"pose") == 0) {
        for (p = 0; p < AN_POSES; p++) {
            if (wcscmp(An_Field(f, n, 2), g_anPoses[p].name) == 0) {
                An_Copy(g_an.state[p], 16, An_Field(f, n, 3));
                An_Copy(g_an.file[p], 260, An_Field(f, n, 4));
                An_Copy(g_an.rule[p], 32, An_Field(f, n, 5));
            }
        }
    } else if (wcscmp(f[0], L"char") == 0 && wcscmp(An_Field(f, n, 1), L"poses") == 0) {
        g_an.found = _wtoi(An_Field(f, n, 2));
        g_an.shown = _wtoi(An_Field(f, n, 3));
    } else if (wcscmp(f[0], L"file") == 0 && wcscmp(An_Field(f, n, 1), L"preview") == 0) {
        g_an.previewOk = wcscmp(An_Field(f, n, 2), L"ok") == 0;
    } else if (wcscmp(f[0], L"msg") == 0) {
        int severity = Rs_SeverityFromText(An_Field(f, n, 1));
        if (severity == RS_SEV_ERROR) {
            if (!g_an.errors++)
                An_Copy(g_an.firstError, 400, An_Field(f, n, 3));
        } else if (severity == RS_SEV_WARNING) {
            if (!g_an.warnings++)
                An_Copy(g_an.firstWarning, 400, An_Field(f, n, 3));
        }
    }
}

static void An_JobDone(HWND page, int code)
{
    wchar_t preview[AN_VAL], text[600];
    int seq = g_an.jobSeq;

    g_an.jobId = 0;
    g_an.jobSeq = 0;
    An_TempPath(preview, AN_VAL, seq);
    if (g_an.again) {
        // Folder or model changed meanwhile: this result is outdated.
        g_an.again = 0;
        An_TempDelete(seq);
        An_Check(page);
        return;
    }
    if (g_an.previewOk && RsView_LoadPoseSet(g_an.view, preview)) {
        g_an.loaded = 1;
        if (g_an.showPose >= 0 && !An_Shown(g_an.showPose))
            g_an.showPose = -1;
        RsView_ShowPoseSet(g_an.view, g_an.showPose);
    } else {
        An_Drop();
    }
    An_TempDelete(seq);
    An_FillList();
    An_FillShow();
    SendMessageW(g_an.list, LB_SETCURSEL, (WPARAM)g_an.showPose, 0);

    if (g_an.errors > 1) {
        swprintf(text, 600, L"%ls (and %d more errors)", g_an.firstError, g_an.errors - 1);
        An_Status(text, RS_COL_ERROR);
    } else if (g_an.errors) {
        An_Status(g_an.firstError, RS_COL_ERROR);
    } else if (!g_an.loaded && g_an.previewOk) {
        An_Status(L"Not shown: the preview cannot read the poses.", RS_COL_ERROR);
    } else if (!g_an.loaded && g_an.warnings) {
        swprintf(text, 600, L"Not shown: %ls", g_an.firstWarning);
        An_Status(text, RS_COL_ERROR);
    } else if (!g_an.loaded) {
        swprintf(text, 600, L"Not shown: rldpack ended with code %d.", code);
        An_Status(text, RS_COL_ERROR);
    } else if (g_an.warnings) {
        swprintf(text, 600, L"%d of %d poses shown. %ls", g_an.shown, AN_POSES, g_an.firstWarning);
        An_Status(text, RS_COL_WARNING);
    } else {
        swprintf(text, 600, L"%d of %d poses shown; the character still uses the automatic poses.", g_an.shown, AN_POSES);
        An_Status(text, RS_COL_TEXT);
    }
}

// ---------------------------------------------------------------------------
// The card
// ---------------------------------------------------------------------------

static void An_Tips(void)
{
    const int on = g_rsPreviewFeatures;
    const wchar_t *folder = on ? L"A folder of PLY files, one per pose (turn_left, turn_right, reverse, bump, jump, idle), each the model's "
                                 L"own mesh with moved vertices. They are shown in the preview only."
                               : L"Later: a folder of PLY files, one per pose, each with the same mesh as the model.";
    const wchar_t *list = on ? L"The poses found for each animation slot, and whether they match the model. Click one to show it."
                             : L"Later: the poses found for each animation slot, and whether they match the model.";
    const wchar_t *show = on ? L"Shows that pose in the preview instead of the built model." : L"Later: shows that pose in the preview.";

    Rs_SetTip(g_an.hint, on ? L"Unfinished: shown in the preview only, nothing of it is written into a character."
                            : L"Coming soon: driver animations from pose PLYs (turn_left, turn_right, reverse, bump, jump). Start Reload Studio "
                              L"with --enable-preview-features to try it.");
    Rs_SetTip(g_an.folderLabel, folder);
    Rs_SetTip(g_an.folder, folder);
    Rs_SetTip(g_an.browse, folder);
    Rs_SetTip(g_an.clear, folder);
    Rs_SetTip(g_an.list, list);
    Rs_SetTip(g_an.showLabel, show);
    Rs_SetTip(g_an.show, show);
    Rs_SetTip(g_an.mark, L"Later: mark which parts of the driver bend in each pose.");
}

void CharAnim_Create(HWND page, HWND view)
{
    const int on = g_rsPreviewFeatures;
    HWND inputs[5];
    HWND muted[2];
    int i;

    memset(&g_an, 0, sizeof(g_an));
    g_an.page = page;
    g_an.view = view;
    g_an.showPose = -1;
    g_an.sizeNow = 100;

    g_an.hint = Rs_ComingSoon(page, AN_ID_HINT);
    // right-aligned in a box as wide as the longer of its two texts: the same
    // rectangle with and without the switch
    SetWindowLongPtrW(g_an.hint, GWL_STYLE, GetWindowLongPtrW(g_an.hint, GWL_STYLE) | SS_RIGHT);
    g_an.note = Rs_Label(page, AN_ID_NOTE, AN_TEXT_NOTE, RS_FONT_SMALL);
    g_an.folderLabel = Rs_Label(page, AN_ID_FOLDER_LABEL, L"Pose folder", RS_FONT_BOLD);
    g_an.folder = Rs_Edit(page, AN_ID_FOLDER, L"", ES_READONLY);
    SendMessageW(g_an.folder, EM_SETCUEBANNER, TRUE, (LPARAM)(on ? L"Optional - a folder with one PLY per pose" : L"Coming soon"));
    g_an.browse = Rs_Button(page, AN_ID_BROWSE, L"Browse...");
    g_an.clear = Rs_Button(page, AN_ID_CLEAR, L"Clear");
    g_an.list = Rs_ListBox(page, AN_ID_LIST, on ? 0 : LBS_NOSEL);
    g_an.showLabel = Rs_Label(page, AN_ID_SHOW_LABEL, L"Show pose", RS_FONT_BOLD);
    g_an.show = Rs_Combo(page, AN_ID_SHOW);
    g_an.mark = Rs_Button(page, AN_ID_MARK, L"Mark body parts...");
    g_an.before = Rs_Label(page, AN_ID_BEFORE, AN_TEXT_BEFORE, RS_FONT_SMALL);
    g_an.status = Rs_Label(page, AN_ID_STATUS, AN_TEXT_NONE, RS_FONT_SMALL);
    g_an.statusColor = RS_COL_MUTED;
    An_Copy(g_an.statusText, 512, AN_TEXT_NONE);
    Rs_SetTextColor(g_an.note, RS_COL_MUTED);
    Rs_SetTextColor(g_an.before, RS_COL_MUTED);
    Rs_SetTextColor(g_an.status, g_an.statusColor);
    An_FillList();
    An_FillShow();

    // Locked: the inputs greyed out; the labels stay enabled (a disabled
    // static is drawn embossed) but muted. "Mark body parts..." is always
    // greyed out: it is a later step.
    inputs[0] = g_an.folder;
    inputs[1] = g_an.browse;
    inputs[2] = g_an.clear;
    inputs[3] = g_an.list;
    inputs[4] = g_an.show;
    muted[0] = g_an.folderLabel;
    muted[1] = g_an.showLabel;
    if (!on) {
        for (i = 0; i < 5; i++)
            EnableWindow(inputs[i], FALSE);
        for (i = 0; i < 2; i++)
            Rs_SetTextColor(muted[i], RS_COL_MUTED);
    }
    EnableWindow(g_an.mark, FALSE);
    An_Tips();
    g_an.created = 1;
}

// Card "Animations" from top between left and right, its fields beside a
// label column at least labelW wide (the page's). Returns the bottom of the card.
int CharAnim_Layout(HWND page, int left, int right, int top, int labelW)
{
    RECT card, in;
    int x, y, h, w, fieldW, width, itemH;
    const int browseW = Rs_Px(100), clearW = Rs_Px(72);
    HWND labels[2];
    wchar_t *text;
    int i;

    if (!g_an.created)
        return top;
    // Nothing is cut short: the label column holds this card's labels too.
    labels[0] = g_an.folderLabel;
    labels[1] = g_an.showLabel;
    for (i = 0; i < 2; i++) {
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
    w = Rs_TextWidth(g_an.hint, L"Coming soon");
    h = Rs_TextWidth(g_an.hint, L"Preview feature");
    w = (h > w ? h : w) + Rs_Px(4);
    MoveWindow(g_an.hint, in.right - w, card.top + Rs_Px(16), w, Rs_Px(18), TRUE);

    y = in.top;
    h = An_TextHeight(g_an.note, width);
    MoveWindow(g_an.note, in.left, y, width, h, TRUE);
    y += h + Rs_Px(10);
    MoveWindow(g_an.folderLabel, in.left, y + Rs_Px(6), labelW, Rs_Px(20), TRUE);
    w = fieldW - browseW - clearW - Rs_Px(16);
    MoveWindow(g_an.folder, x, y + Rs_Px(2), w > Rs_Px(24) ? w : Rs_Px(24), Rs_Px(28), TRUE);
    MoveWindow(g_an.browse, in.right - browseW - clearW - Rs_Px(8), y, browseW, Rs_Px(32), TRUE);
    MoveWindow(g_an.clear, in.right - clearW, y, clearW, Rs_Px(32), TRUE);
    y += Rs_Px(40);

    // The six poses, one line each, the whole width of the card; AN_LIST_ROWS
    // of them at once, the list scrolls (the card fits beside the bar of the page).
    itemH = (int)SendMessageW(g_an.list, LB_GETITEMHEIGHT, 0, 0);
    if (itemH <= 0)
        itemH = Rs_Px(18);
    h = AN_LIST_ROWS * itemH + 2 * Rs_Metric(g_an.list, SM_CYBORDER) + Rs_Px(4);
    MoveWindow(g_an.list, in.left, y, width, h, TRUE);
    y += h + Rs_Px(10);

    // The pose choice, "Mark body parts..." beside it where it fits.
    MoveWindow(g_an.showLabel, in.left, y + Rs_Px(6), labelW, Rs_Px(20), TRUE);
    w = Rs_Px(240) < fieldW ? Rs_Px(240) : fieldW;
    MoveWindow(g_an.show, x, y + Rs_Px(2), w, Rs_Px(200), TRUE);
    text = Rs_GetText(g_an.mark);
    h = Rs_TextWidth(g_an.mark, text) + Rs_Px(32);
    Rs_Free(text);
    if (x + w + Rs_Px(12) + h <= in.right) {
        MoveWindow(g_an.mark, x + w + Rs_Px(12), y, h, Rs_Px(32), TRUE);
    } else {
        y += Rs_Px(38);
        MoveWindow(g_an.mark, x, y, h < fieldW ? h : fieldW, Rs_Px(32), TRUE);
    }
    y += Rs_Px(40);

    h = An_TextHeight(g_an.before, width);
    MoveWindow(g_an.before, in.left, y, width, h, TRUE);
    y += h + Rs_Px(6);
    h = An_TextHeight(g_an.status, width);
    MoveWindow(g_an.status, in.left, y, width, h, TRUE);
    y += h;
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Animations");
    return card.bottom;
}

static void An_SetFolder(HWND page, const wchar_t *folder)
{
    Rs_SetText(g_an.folder, folder);
    if (!folder[0])
        g_an.showPose = -1;
    An_Check(page);
}

static void An_Browse(HWND page)
{
    wchar_t start[AN_VAL];
    wchar_t pick[AN_VAL];

    An_FieldPath(start, AN_VAL);
    if (!start[0] && g_an.model[0])
        Rs_PathDir(start, AN_VAL, g_an.model);
    if (Rs_BrowseFolder(Rs_MainWindow(), L"Choose the folder with the pose PLYs", start, pick, AN_VAL))
        An_SetFolder(page, pick);
}

int CharAnim_Command(HWND page, int id, int code)
{
    int i;

    if (id < AN_ID_FIRST || id > AN_ID_LAST)
        return 0;
    // Locked: taken and dropped, whatever sent it.
    if (!g_rsPreviewFeatures || !g_an.created)
        return 1;
    switch (id) {
    case AN_ID_BROWSE:
        if (code == BN_CLICKED)
            An_Browse(page);
        break;
    case AN_ID_CLEAR:
        if (code == BN_CLICKED)
            An_SetFolder(page, L"");
        break;
    case AN_ID_SHOW:
        if (code == CBN_SELCHANGE) {
            i = (int)SendMessageW(g_an.show, CB_GETCURSEL, 0, 0);
            if (i >= 0)
                An_ShowPose((int)SendMessageW(g_an.show, CB_GETITEMDATA, (WPARAM)i, 0));
        }
        break;
    case AN_ID_LIST:
        if (code == LBN_SELCHANGE) {
            i = (int)SendMessageW(g_an.list, LB_GETCURSEL, 0, 0);
            if (!An_ShowPose(i))
                SendMessageW(g_an.list, LB_SETCURSEL, (WPARAM)g_an.showPose, 0);
        }
        break;
    }
    return 1;
}

LRESULT CharAnim_Notify(HWND page, NMHDR *hdr, int *handled)
{
    (void)page;
    (void)hdr;
    *handled = 0;
    return 0;
}

LRESULT CharAnim_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    *handled = 0;
    switch (msg) {
    case RS_WM_JOB_LINE:
        // Only lines of its own job; the page frees all others.
        if (g_an.jobId && (int)wParam == g_an.jobId) {
            wchar_t *line = (wchar_t *)lParam;
            if (line)
                An_ParseLine(line);
            Rs_Free(line);
            *handled = 1;
        }
        return 0;
    case RS_WM_JOB_DONE:
        if (g_an.jobId && (int)wParam == g_an.jobId) {
            An_JobDone(page, (int)lParam);
            *handled = 1;
        }
        return 0;
    case WM_DESTROY:
        // Not handled: the page cleans up after this.
        if (g_an.jobId)
            Rs_KillJob(g_an.jobId);
        An_TempDelete(g_an.jobSeq);
        g_an.jobId = 0;
        g_an.jobSeq = 0;
        g_an.created = 0;
        return 0;
    }
    return 0;
}

int CharAnim_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    int r, p;

    if (wcscmp(verb, L"anim-folder") != 0 && wcscmp(verb, L"poses") != 0 && wcscmp(verb, L"anim-show") != 0)
        return RS_AUTO_UNKNOWN;
    if (!g_rsPreviewFeatures || !g_an.created) {
        Rs_AutoLog(L"  %ls: locked - coming soon", verb);
        return RS_AUTO_FAIL;
    }

    if (wcscmp(verb, L"anim-show") == 0) {
        wchar_t *end;
        long n = wcstol(arg, &end, 10);
        int pose = -2;

        if (_wcsicmp(arg, L"model") == 0)
            pose = -1;
        else if (end != arg && !*end)
            pose = (int)n;
        for (p = 0; pose == -2 && p < AN_POSES; p++)
            if (_wcsicmp(arg, g_anPoses[p].name) == 0)
                pose = p;
        if (pose < -1 || !An_ShowPose(pose)) {
            Rs_AutoLog(L"  %ls: '%ls' is not a pose the preview has - say model, a pose name or 0..5", verb, arg);
            return RS_AUTO_FAIL;
        }
        UpdateWindow(g_an.view);      // painted before a following "shot"
        Rs_AutoLog(L"  %ls: %ls", verb, pose < 0 ? L"the built model" : g_anPoses[pose].name);
        return RS_AUTO_DONE;
    }

    // anim-folder, poses
    r = 0;
    if (!arg[0] || _wcsicmp(arg, L"none") == 0) {
        An_SetFolder(page, L"");
    } else {
        Rs_SetText(g_an.folder, arg);
        r = An_Check(page);
    }
    if (r > 0) {
        Rs_AutoLog(L"  %ls: %ls", verb, arg);
        return RS_AUTO_WAIT;
    }
    if (r == 0) {
        Rs_AutoLog(L"  %ls: %ls", verb, g_an.statusText);
        return RS_AUTO_DONE;
    }
    Rs_AutoLog(L"  %ls: rldpack could not be started", verb);
    return RS_AUTO_FAIL;
}

int CharAnim_Busy(void)
{
    return g_an.jobId != 0;
}

// After every check of the page: the poses follow its model and its size.
void CharAnim_ModelChecked(HWND page, const wchar_t *model, int sizePercent, int ok)
{
    (void)ok;   // char-poses builds the model itself and says when it cannot
    if (!g_rsPreviewFeatures || !g_an.created)
        return;
    An_Copy(g_an.model, AN_VAL, model);
    g_an.sizeNow = sizePercent;
    if (!model[0]) {
        if (g_an.jobId)
            g_an.again = 1;
        else
            An_Check(page);     // drops the poses and says why
        return;
    }
    if (g_an.folder && GetWindowTextLengthW(g_an.folder) > 0)
        An_Check(page);
}

static void An_Put(FILE *f, const wchar_t *fmt, ...)
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

static const wchar_t *An_EnabledWord(HWND h)
{
    return IsWindowEnabled(h) ? L"enabled" : L"greyed out";
}

void CharAnim_Report(FILE *f)
{
    wchar_t *text;
    int p;

    if (!f || !g_an.created)
        return;
    An_Put(f, L"animations card: %ls", g_rsPreviewFeatures ? L"enabled (preview feature)" : L"locked - coming soon");
    text = Rs_GetText(g_an.hint);
    An_Put(f, L"animations hint: %ls", text);
    Rs_Free(text);
    An_Put(f, L"animations fields: folder %ls, browse %ls, clear %ls, list %ls, show %ls, mark body parts %ls", An_EnabledWord(g_an.folder),
           An_EnabledWord(g_an.browse), An_EnabledWord(g_an.clear), An_EnabledWord(g_an.list), An_EnabledWord(g_an.show),
           An_EnabledWord(g_an.mark));
    // Locked, the field carries nothing that counts (a text put into it from
    // outside starts nothing).
    text = Rs_GetText(g_an.folder);
    An_Put(f, L"pose folder: %ls", g_rsPreviewFeatures && text && text[0] ? text : L"none");
    Rs_Free(text);
    An_Put(f, L"pose folder (last run): %ls", g_an.checked[0] ? g_an.checked : L"(none)");
    An_Put(f, L"poses: %d found, %d shown", g_an.found, g_an.shown);
    for (p = 0; p < AN_POSES; p++)
        An_Put(f, L"pose %ls: %ls %ls %ls", g_anPoses[p].name, g_an.state[p][0] ? g_an.state[p] : L"-", g_an.file[p][0] ? g_an.file[p] : L"-",
               g_an.rule[p][0] ? g_an.rule[p] : L"-");
    An_Put(f, L"poses in the preview: %ls, showing %ls", g_an.loaded ? L"yes" : L"no",
           g_an.showPose >= 0 ? g_anPoses[g_an.showPose].name : L"the built model");
    An_Put(f, L"animations status: %ls", g_an.statusText);
}
