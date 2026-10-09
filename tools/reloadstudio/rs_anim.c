// rs_anim.c - the card "Animations" of the page Character
//
// Animations of one's own are SHAPE KEYS (glTF morph targets) on the driver's
// mesh of a glTF model (.glb): the base mesh is the neutral pose, and a shape
// key named steer_left, steer_right, reverse, crash, jump, win or lose (case
// does not matter) is that pose. make-char bakes steering, reverse, crash and
// jump into the 47 frames of the driver (the classic model and the native
// one, so every build of the game shows them) and keeps win and lose for the
// native model, which blends to them after the finish (places 1-3 win, 4-8
// lose). A pose without its shape key stays automatic as before (win and lose
// neutral); one steering key alone is mirrored for the other side when the
// model is symmetric. OBJ and PLY carry no shape keys: their driver keeps the
// automatic poses, byte for byte as before. A glTF with a skin (a rig) is
// refused: rigs are not supported yet. How to make the keys in Blender:
// docs/ANIMATIONS.md.
//
// The card is open to everyone (no switch). It runs nothing itself: the page's
// check (make-char) says per pose what it found - `@pose <name>
// from-file|automatic|mirrored|neutral|error <reason>` - and names the shape
// keys of no pose (pose-unknown-key); the page collects the lines
// (CharAnim_PoseLine, CharAnim_PoseMsg) and hands them over after the check
// (CharAnim_ModelChecked). The list shows the seven poses with their state, the
// line below it the shape keys that were ignored and why a pose fell back.
//
// THE PREVIEW: the steering slider (left <-> right, frames 0..20 of animation
// 0), the tick boxes Jump, Crash, Reverse, Win and Lose (one at a time,
// unticked: back to steering) and Play (the animation shown, as a loop on the
// view's own timer in fixed steps) steer what the preview draws. Nothing of
// it is built or stored. A click on a pose in the list shows it.
//
// The pose folder of earlier versions (one PLY per pose, rldpack char-poses,
// shown before the reduction and never built) is gone from the card: the
// shape keys replace it. rldpack char-poses itself is unchanged.
//
// Automation verbs:
//   anim-steer <-10..10>                 the steering slider (-10 full left, 0 neutral)
//   anim-play <steer|jump|crash|reverse|win|lose> on|off
//                                        shows that animation, on = as a loop
//   anim-show <steer|jump|crash|reverse|win|lose|steer_left|steer_right>
//                                        as a click on its button (its strongest frame)
//   anim-end win|lose|none               the pose after the finish (none: steering again)

#include "rs_anim.h"
#include "rs_view.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Controls (340-379 belong to this card on the page Character)
#define AN_ID_NOTE          340
#define AN_ID_LIST          341
#define AN_ID_STATUS        342
#define AN_ID_STEER_LABEL   343
#define AN_ID_STEER_LEFT    344
#define AN_ID_STEER         345
#define AN_ID_STEER_RIGHT   346
#define AN_ID_SHOW          347     // 347..351: Jump, Crash, Reverse, Win, Lose (AN_SHOWS)
#define AN_ID_PLAY          352
#define AN_ID_FIRST         340
#define AN_ID_LAST          379

#define AN_VAL              1024
#define AN_LIST_ROWS        RS_ANIM_POSES   // rows of the pose list shown at once
#define AN_LIST_ROWS_COMPACT 3              // the same in the compact layout of the page
#define AN_NOTE_LINES       3
#define AN_STEER_MAX        RS_VIEW_STEER_MAX      // the slider: -10 (frame 0, full left) .. 10 (frame 20)

// What the preview shows (the buttons, in their order; steering is none of them).
enum { AN_WHAT_STEER = -1, AN_WHAT_JUMP = 0, AN_WHAT_CRASH, AN_WHAT_REVERSE, AN_WHAT_WIN, AN_WHAT_LOSE, AN_SHOWS };

#define AN_TEXT_NOTE        L"Shape keys on the driver of a glTF model (.glb) animate it: steer_left, steer_right, reverse, crash, " \
                            L"jump, win, lose. In Blender: Object Data, Shape Keys - one key per pose with that name, then " \
                            L"File, Export, glTF 2.0 with Shape Keys on. How-to: docs/ANIMATIONS.md."
#define AN_TEXT_NO_MODEL    L"Choose a model first: the poses are read from it."
#define AN_TEXT_NO_KEYS     L"OBJ and PLY carry no shape keys: the driver moves with the automatic poses. Export a glTF (.glb) for poses of your own."
#define AN_TEXT_NOT_SAID    L"rldpack said nothing about the poses of this model: the automatic poses are used."
#define AN_TEXT_RIG         L"Rigs are not supported yet - use shape keys (docs/ANIMATIONS.md)."

// The poses as make-char names them (@pose), as the list shows them, and
// what the preview shows for them.
static const struct {
    const wchar_t *name;
    const wchar_t *text;
    int what, steer;
} g_anPoses[RS_ANIM_POSES] = {
    { L"steer_left", L"Steer left", AN_WHAT_STEER, -AN_STEER_MAX },
    { L"steer_right", L"Steer right", AN_WHAT_STEER, AN_STEER_MAX },
    { L"reverse", L"Reverse", AN_WHAT_REVERSE, 0 },
    { L"crash", L"Crash", AN_WHAT_CRASH, 0 },
    { L"jump", L"Jump", AN_WHAT_JUMP, 0 },
    { L"win", L"Win", AN_WHAT_WIN, 0 },
    { L"lose", L"Lose", AN_WHAT_LOSE, 0 },
};

// The buttons: their text, their verb word and the animation of the view
// with the frame shown while Play is off - the last frame of jump and
// reverse, the strongest nod of the crash (frame 2 of the retail course,
// tools/rldpack_char.inc s_rldMkBumpPitch); win and lose fully blended.
static const struct {
    const wchar_t *text;
    const wchar_t *word;
    int anim, frame;
} g_anShows[AN_SHOWS] = {
    { L"Jump", L"jump", RS_VIEW_ANIM_JUMP, 3 },
    { L"Crash", L"crash", RS_VIEW_ANIM_CRASH, 2 },
    { L"Reverse", L"reverse", RS_VIEW_ANIM_REVERSE, 6 },
    { L"Win", L"win", -1, 0 },
    { L"Lose", L"lose", -1, 0 },
};

static struct {
    HWND page, view;
    HWND note, list, status, steerLabel, steerLeft, steer, steerRight, show[AN_SHOWS], play;
    int created;
    int what;                   // AN_WHAT_*: what the preview shows
    int steerNow;               // -AN_STEER_MAX..AN_STEER_MAX
    int playing;                // Play: the animation shown as a loop
    COLORREF statusColor;
    wchar_t model[AN_VAL];      // the model of the page's last check, "" = none
    wchar_t format[8];          // its format (@model format)
    struct RsAnimPoses poses;   // what that check said
    wchar_t statusText[1024];
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

static void An_Append(wchar_t *out, int cap, const wchar_t *in)
{
    size_t n = wcslen(out);
    if ((int)n < cap - 1)
        wcsncat(out, in ? in : L"", (size_t)cap - 1 - n);
}

// Height of a wrapping label at this width: its lines, at least one, at most
// AN_NOTE_LINES.
// The compact layout of the page (rs_char.c, CharAnim_Layout): notes on one line, the
// whole text as the tooltip (Rs_LabelOneLine).
static int s_anCompact;

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
        Rs_LabelOneLine(label, s_anCompact && tm.tmHeight > 0 && h > tm.tmHeight);
        if (s_anCompact && tm.tmHeight > 0 && h > tm.tmHeight)
            h = tm.tmHeight;
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
    wchar_t *old = Rs_GetText(g_an.status);
    const int same = old && wcscmp(old, text) == 0;

    Rs_Free(old);
    An_Copy(g_an.statusText, 1024, text);
    g_an.statusColor = color;
    Rs_SetTextColor(g_an.status, color);
    if (same)
        return;
    Rs_SetText(g_an.status, text);
    GetWindowRect(g_an.status, &rc);
    if (rc.right > rc.left && An_TextHeight(g_an.status, rc.right - rc.left) != rc.bottom - rc.top)
        An_Relayout();
}

static int An_PoseIndex(const wchar_t *name)
{
    int p;
    for (p = 0; p < RS_ANIM_POSES; p++)
        if (_wcsicmp(name, g_anPoses[p].name) == 0)
            return p;
    return -1;
}

// The model carries shape keys at all: a glTF (by what the check read, else
// by its name).
static int An_Gltf(void)
{
    size_t n = wcslen(g_an.model);
    if (g_an.format[0])
        return wcscmp(g_an.format, L"glb") == 0 || wcscmp(g_an.format, L"gltf") == 0;
    return (n > 4 && _wcsicmp(g_an.model + n - 4, L".glb") == 0) || (n > 5 && _wcsicmp(g_an.model + n - 5, L".gltf") == 0);
}

// A pose whose key was not used because the model is not symmetric: make-char
// says "error", but the character is built (with the automatic lean) - a
// warning to the author, not an error.
static int An_NotSymmetric(int p)
{
    return wcscmp(g_an.poses.state[p], L"error") == 0 && wcscmp(g_an.poses.why[p], L"pose-not-symmetric") == 0;
}

// The state of a pose: make-char's word, else what it is without a key.
static const wchar_t *An_State(int p)
{
    if (g_an.poses.state[p][0])
        return g_an.poses.state[p];
    return p == RS_ANIM_WIN || p == RS_ANIM_LOSE ? L"neutral" : L"automatic";
}

// ---------------------------------------------------------------------------
// The lines of the page's check
// ---------------------------------------------------------------------------

void CharAnim_PoseLine(struct RsAnimPoses *poses, wchar_t **f, int n)
{
    int p, i;
    if (!poses || n < 3)
        return;
    p = An_PoseIndex(f[1]);
    if (p < 0)
        return;
    poses->seen = 1;
    An_Copy(poses->state[p], 16, f[2]);
    // The reason: the rest of the line ("-" = none).
    poses->why[p][0] = 0;
    for (i = 3; i < n; i++) {
        if (i == 3 && wcscmp(f[i], L"-") == 0)
            continue;
        if (poses->why[p][0])
            An_Append(poses->why[p], 200, L" ");
        An_Append(poses->why[p], 200, f[i]);
    }
}

// The reason of a pose in words: make-char gives the rule (its message id).
static const wchar_t *An_WhyText(const wchar_t *why)
{
    static const struct {
        const wchar_t *rule, *text;
    } rules[] = {
        { L"pose-not-symmetric", L"the model is not symmetric, the automatic lean is used" },
        { L"pose-remesh", L"not with Closed hull (remesh)" },
        { L"pose-no-driver", L"no driver found in the model" },
        { L"pose-still", L"the driver stands still (--poses still)" },
        { L"pose-kart-moved", L"the key moves the kart, which stays rigid" },
        { L"model", L"the model could not be read to the end" },
        { L"model-scale", L"the key makes the model too large" },
    };
    int i;
    for (i = 0; i < (int)(sizeof(rules) / sizeof(rules[0])); i++)
        if (wcscmp(why, rules[i].rule) == 0)
            return rules[i].text;
    return why;
}

// A shape key of no pose, once by its name.
static void An_Unknown(struct RsAnimPoses *poses, const wchar_t *name)
{
    const wchar_t *p = poses->unknownNames;
    size_t n = wcslen(name);

    if (!n)
        return;
    while ((p = wcsstr(p, name)) != NULL) {
        if ((p == poses->unknownNames || p[-1] == L' ') && (p[n] == 0 || p[n] == L','))
            return;
        p += n;
    }
    if (poses->unknown++)
        An_Append(poses->unknownNames, 400, L", ");
    An_Append(poses->unknownNames, 400, name);
}

void CharAnim_PoseMsg(struct RsAnimPoses *poses, const wchar_t *code, const wchar_t *text)
{
    if (!poses || !code)
        return;
    if (wcscmp(code, L"pose-unknown-key") == 0) {
        // Its name: the first text in quotation marks, else the whole text.
        wchar_t name[128];
        const wchar_t *a = text ? wcschr(text, L'"') : NULL;
        const wchar_t *b = a ? wcschr(a + 1, L'"') : NULL;
        if (a && b && b - a - 1 < 127) {
            memcpy(name, a + 1, (size_t)(b - a - 1) * sizeof(wchar_t));
            name[b - a - 1] = 0;
        } else {
            An_Copy(name, 128, text);
        }
        An_Unknown(poses, name);
    } else if (wcscmp(code, L"pose-not-symmetric") == 0) {
        An_Copy(poses->symmetric, 300, text);
    } else if (wcscmp(code, L"model-rig") == 0) {
        poses->rig = 1;
    }
}

void CharAnim_ShapeKey(struct RsAnimPoses *poses, const wchar_t *name, const wchar_t *pose)
{
    if (!poses || !name)
        return;
    if (pose && An_PoseIndex(pose) >= 0)
        poses->keys++;
    else
        An_Unknown(poses, name);
}

// ---------------------------------------------------------------------------
// The preview
// ---------------------------------------------------------------------------

// The view draws what the card says: the steering frame, or the button's
// animation (its frame, or as a loop while Play is down). The view stops its
// playing at every frame set, so Play comes last.
static void An_ViewApply(void)
{
    static const int play[AN_SHOWS] = { RS_VIEW_PLAY_JUMP, RS_VIEW_PLAY_CRASH, RS_VIEW_PLAY_REVERSE, RS_VIEW_PLAY_WIN,
                                        RS_VIEW_PLAY_LOSE };

    if (!g_an.created || !g_an.view)
        return;
    if (g_an.what == AN_WHAT_STEER || g_anShows[g_an.what].anim < 0)
        RsView_SetSteer(g_an.view, g_an.steerNow);
    else
        RsView_SetAnimFrame(g_an.view, g_anShows[g_an.what].anim, g_anShows[g_an.what].frame);
    RsView_SetEndPose(g_an.view, g_an.what == AN_WHAT_WIN ? RS_VIEW_END_WIN : g_an.what == AN_WHAT_LOSE ? RS_VIEW_END_LOSE : RS_VIEW_END_NONE,
                      g_an.what == AN_WHAT_WIN || g_an.what == AN_WHAT_LOSE ? 100 : 0);
    if (g_an.playing)
        RsView_PlayAnim(g_an.view, g_an.what == AN_WHAT_STEER ? RS_VIEW_PLAY_STEER : play[g_an.what], 1);
}

// The buttons and the slider as the card's state.
static void An_ShowControls(void)
{
    int i;
    for (i = 0; i < AN_SHOWS; i++)
        SendMessageW(g_an.show[i], BM_SETCHECK, g_an.what == i ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_an.play, BM_SETCHECK, g_an.playing ? BST_CHECKED : BST_UNCHECKED, 0);
    if ((int)SendMessageW(g_an.steer, TBM_GETPOS, 0, 0) != g_an.steerNow)
        SendMessageW(g_an.steer, TBM_SETPOS, TRUE, g_an.steerNow);
}

static void An_Show(int what, int steer, int playing)
{
    g_an.what = what < AN_WHAT_STEER || what >= AN_SHOWS ? AN_WHAT_STEER : what;
    if (steer < -AN_STEER_MAX)
        steer = -AN_STEER_MAX;
    if (steer > AN_STEER_MAX)
        steer = AN_STEER_MAX;
    g_an.steerNow = steer;
    g_an.playing = playing != 0;
    An_ShowControls();
    An_ViewApply();
}

// ---------------------------------------------------------------------------
// The list of poses
// ---------------------------------------------------------------------------

static void An_StateText(int p, wchar_t *out, int cap)
{
    const wchar_t *s = An_State(p);
    const wchar_t *why = g_an.poses.why[p];

    if (An_NotSymmetric(p)) {
        An_Copy(out, cap, L"not used - the model is not symmetric, the automatic lean is used");
        return;
    }
    if (wcscmp(s, L"from-file") == 0)
        An_Copy(out, cap, p == RS_ANIM_WIN || p == RS_ANIM_LOSE ? L"from the file (native model)" : L"from the file");
    else if (wcscmp(s, L"mirrored") == 0)
        swprintf(out, cap, L"mirrored from %ls", p == RS_ANIM_STEER_LEFT ? L"steer_right" : L"steer_left");
    else if (wcscmp(s, L"automatic") == 0)
        An_Copy(out, cap, L"automatic");
    else if (wcscmp(s, L"neutral") == 0)
        An_Copy(out, cap, L"neutral (no key)");
    else if (wcscmp(s, L"error") == 0)
        swprintf(out, cap, L"error: %ls", why[0] ? An_WhyText(why) : L"rldpack gave no reason");
    else
        An_Copy(out, cap, s);
    // A reason given with any other state.
    if (wcscmp(s, L"error") != 0 && why[0]) {
        An_Append(out, cap, L" - ");
        An_Append(out, cap, An_WhyText(why));
    }
}

static void An_FillList(void)
{
    wchar_t row[600], state[400];
    int p, sel = (int)SendMessageW(g_an.list, LB_GETCURSEL, 0, 0);

    SendMessageW(g_an.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_an.list, LB_RESETCONTENT, 0, 0);
    for (p = 0; p < RS_ANIM_POSES; p++) {
        An_StateText(p, state, 400);
        swprintf(row, 600, L"%ls (%ls): %ls", g_anPoses[p].text, g_anPoses[p].name, state);
        SendMessageW(g_an.list, LB_ADDSTRING, 0, (LPARAM)row);
    }
    if (sel >= 0)
        SendMessageW(g_an.list, LB_SETCURSEL, (WPARAM)sel, 0);
    SendMessageW(g_an.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_an.list, NULL, TRUE);
}

// The line below the list: what the poses come from, the keys ignored and
// why a pose fell back.
static void An_StatusUpdate(void)
{
    wchar_t text[1024], t[600];
    int p, files = 0, errors = 0, unused = 0;

    if (!g_an.model[0]) {
        An_Status(AN_TEXT_NO_MODEL, RS_COL_MUTED);
        return;
    }
    if (g_an.poses.rig) {
        An_Status(AN_TEXT_RIG, RS_COL_ERROR);
        return;
    }
    if (!An_Gltf()) {
        An_Status(AN_TEXT_NO_KEYS, RS_COL_MUTED);
        return;
    }
    if (!g_an.poses.seen && !g_an.poses.unknown) {
        An_Status(AN_TEXT_NOT_SAID, RS_COL_MUTED);
        return;
    }
    for (p = 0; p < RS_ANIM_POSES; p++) {
        files += wcscmp(An_State(p), L"from-file") == 0 || wcscmp(An_State(p), L"mirrored") == 0;
        unused += An_NotSymmetric(p);
        errors += wcscmp(An_State(p), L"error") == 0 && !An_NotSymmetric(p);
    }
    if (errors)
        swprintf(text, 1024, L"%d of %d poses with an error - see the list and the messages.", errors, RS_ANIM_POSES);
    else if (files == RS_ANIM_POSES)
        swprintf(text, 1024, L"All %d poses from your shape keys.", RS_ANIM_POSES);
    else if (files)
        swprintf(text, 1024, L"%d of %d poses from your shape keys, the others automatic.", files, RS_ANIM_POSES);
    else if (unused || g_an.poses.keys)
        An_Copy(text, 1024, L"Your shape keys are not used: the automatic poses are used.");
    else
        An_Copy(text, 1024, L"No shape key of a pose found: the automatic poses are used.");
    if (g_an.poses.symmetric[0]) {
        // rldpack's words as a sentence: a capital at its start, a full stop at its end.
        size_t k;
        swprintf(t, 600, L" %ls", g_an.poses.symmetric);
        if (t[1] >= L'a' && t[1] <= L'z')
            t[1] = (wchar_t)(t[1] - L'a' + L'A');
        k = wcslen(t);
        if (k > 1 && t[k - 1] != L'.' && k < 599) {
            t[k] = L'.';
            t[k + 1] = 0;
        }
        An_Append(text, 1024, t);
    }
    if (g_an.poses.unknown) {
        swprintf(t, 600, L" Ignored (no pose of that name): %ls.", g_an.poses.unknownNames);
        An_Append(text, 1024, t);
    }
    An_Status(text, errors ? RS_COL_ERROR : (g_an.poses.unknown || unused || g_an.poses.symmetric[0]) ? RS_COL_WARNING : RS_COL_TEXT);
}

// ---------------------------------------------------------------------------
// The card
// ---------------------------------------------------------------------------

static void An_Tips(void)
{
    const wchar_t *steer = L"Steering in the preview: full left to full right (the 21 frames of the steering animation).";
    const wchar_t *play = L"Plays the animation shown as a loop (steering: from left to right and back).";
    int i;

    Rs_SetTip(g_an.note, AN_TEXT_NOTE L" " AN_TEXT_RIG);
    Rs_SetTip(g_an.list, L"The seven poses and where each comes from: from the file (your shape key), mirrored (the other "
                         L"steering key, mirrored), automatic (as before), neutral (win and lose without a key). Click one "
                         L"to see it in the preview.");
    Rs_SetTip(g_an.steerLabel, steer);
    Rs_SetTip(g_an.steerLeft, steer);
    Rs_SetTip(g_an.steer, steer);
    Rs_SetTip(g_an.steerRight, steer);
    for (i = 0; i < AN_SHOWS; i++)
        Rs_SetTip(g_an.show[i], i == AN_WHAT_WIN || i == AN_WHAT_LOSE
                                    ? L"The pose after the finish (the native model blends to it: win at places 1-3, lose at "
                                      L"4-8). Untick for steering."
                                    : L"Shows this animation in the preview (with Play as a loop). Untick for steering.");
    Rs_SetTip(g_an.play, play);
}

void CharAnim_Create(HWND page, HWND view)
{
    int i;

    memset(&g_an, 0, sizeof(g_an));
    g_an.page = page;
    g_an.view = view;
    g_an.what = AN_WHAT_STEER;

    g_an.note = Rs_Label(page, AN_ID_NOTE, AN_TEXT_NOTE, RS_FONT_SMALL);
    g_an.list = Rs_ListBox(page, AN_ID_LIST, 0);
    g_an.status = Rs_Label(page, AN_ID_STATUS, AN_TEXT_NO_MODEL, RS_FONT_SMALL);
    g_an.statusColor = RS_COL_MUTED;
    An_Copy(g_an.statusText, 1024, AN_TEXT_NO_MODEL);
    g_an.steerLabel = Rs_Label(page, AN_ID_STEER_LABEL, L"Steering", RS_FONT_BOLD);
    g_an.steerLeft = Rs_Label(page, AN_ID_STEER_LEFT, L"Left", RS_FONT_SMALL);
    g_an.steer = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, 0, 10, 10,
                                 page, (HMENU)(INT_PTR)AN_ID_STEER, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_an.steer, TBM_SETRANGE, FALSE, MAKELPARAM(-AN_STEER_MAX, AN_STEER_MAX));
    SendMessageW(g_an.steer, TBM_SETTICFREQ, AN_STEER_MAX, 0);
    SendMessageW(g_an.steer, TBM_SETLINESIZE, 0, 1);
    SendMessageW(g_an.steer, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(g_an.steer, TBM_SETPOS, TRUE, 0);
    g_an.steerRight = Rs_Label(page, AN_ID_STEER_RIGHT, L"Right", RS_FONT_SMALL);
    // The buttons are tick boxes of the page (its colours in both schemes):
    // ticked = shown; one at a time.
    for (i = 0; i < AN_SHOWS; i++)
        g_an.show[i] = Rs_Check(page, AN_ID_SHOW + i, g_anShows[i].text);
    g_an.play = Rs_Check(page, AN_ID_PLAY, L"Play (loop)");
    Rs_SetTextColor(g_an.note, RS_COL_MUTED);
    Rs_SetTextColor(g_an.steerLeft, RS_COL_MUTED);
    Rs_SetTextColor(g_an.steerRight, RS_COL_MUTED);
    Rs_SetTextColor(g_an.status, g_an.statusColor);
    An_FillList();
    An_Tips();
    g_an.created = 1;
}

// Card "Animations" from top between left and right, its fields beside a
// label column at least labelW wide (the page's). Returns the bottom of the card.
int CharAnim_Layout(HWND page, int left, int right, int top, int labelW, int compact)
{
    RECT card, in;
    int x, y, h, w, fieldW, width, itemH, i, endW, gap, bw;
    wchar_t *text;

    s_anCompact = compact;
    if (!g_an.created)
        return top;
    // Nothing is cut short: the label column holds this card's label too.
    text = Rs_GetText(g_an.steerLabel);
    w = Rs_TextWidth(g_an.steerLabel, text) + Rs_Px(4);
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

    y = in.top;
    h = An_TextHeight(g_an.note, width);
    MoveWindow(g_an.note, in.left, y, width, h, TRUE);
    y += h + Rs_Px(compact ? 6 : 10);

    // The seven poses, one line each, the whole width of the card; in the
    // compact layout AN_LIST_ROWS_COMPACT of them at once, the list scrolls.
    itemH = (int)SendMessageW(g_an.list, LB_GETITEMHEIGHT, 0, 0);
    if (itemH <= 0)
        itemH = Rs_Px(18);
    h = (compact ? AN_LIST_ROWS_COMPACT : AN_LIST_ROWS) * itemH + 2 * Rs_Metric(g_an.list, SM_CYBORDER) + Rs_Px(4);
    MoveWindow(g_an.list, in.left, y, width, h, TRUE);
    y += h + Rs_Px(4);
    h = An_TextHeight(g_an.status, width);
    MoveWindow(g_an.status, in.left, y, width, h, TRUE);
    y += h + Rs_Px(compact ? 6 : 10);

    // Steering: Left [slider] Right.
    MoveWindow(g_an.steerLabel, in.left, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    text = Rs_GetText(g_an.steerRight);
    endW = Rs_TextWidth(g_an.steerRight, text) + Rs_Px(4);
    Rs_Free(text);
    text = Rs_GetText(g_an.steerLeft);
    w = Rs_TextWidth(g_an.steerLeft, text) + Rs_Px(4);
    Rs_Free(text);
    if (w > endW)
        endW = w;
    MoveWindow(g_an.steerLeft, x, y + Rs_Px(6), endW, Rs_Px(20), TRUE);
    w = fieldW - 2 * endW - Rs_Px(8);
    MoveWindow(g_an.steer, x + endW, y, w > Rs_Px(40) ? w : Rs_Px(40), Rs_Px(30), TRUE);
    MoveWindow(g_an.steerRight, in.right - endW, y + Rs_Px(6), endW, Rs_Px(20), TRUE);
    SetWindowLongPtrW(g_an.steerRight, GWL_STYLE, GetWindowLongPtrW(g_an.steerRight, GWL_STYLE) | SS_RIGHT);
    y += Rs_Px(compact ? 32 : 38);

    // The tick boxes in one row the whole width: Jump, Crash, Reverse, Win,
    // Lose spread evenly, Play at the right end.
    bw = Rs_CheckBoxWidth(g_an.play);
    w = 0;
    for (i = 0; i < AN_SHOWS; i++)
        w += Rs_CheckBoxWidth(g_an.show[i]);
    gap = (width - bw - w) / (AN_SHOWS + 1);
    if (gap < Rs_Px(4))
        gap = Rs_Px(4);
    x = in.left;
    for (i = 0; i < AN_SHOWS; i++) {
        w = Rs_CheckBoxWidth(g_an.show[i]);
        MoveWindow(g_an.show[i], x, y, w, Rs_Px(24), TRUE);
        x += w + gap;
    }
    MoveWindow(g_an.play, in.right - bw, y, bw, Rs_Px(24), TRUE);
    y += Rs_Px(24);
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Animations");
    return card.bottom;
}

int CharAnim_Command(HWND page, int id, int code)
{
    int i;

    (void)page;
    if (id < AN_ID_FIRST || id > AN_ID_LAST)
        return 0;
    if (!g_an.created)
        return 1;
    if (id >= AN_ID_SHOW && id < AN_ID_SHOW + AN_SHOWS && code == BN_CLICKED) {
        i = id - AN_ID_SHOW;
        // Pressed again: back to steering.
        An_Show(g_an.what == i ? AN_WHAT_STEER : i, g_an.steerNow, g_an.playing);
    } else if (id == AN_ID_PLAY && code == BN_CLICKED) {
        An_Show(g_an.what, g_an.steerNow, SendMessageW(g_an.play, BM_GETCHECK, 0, 0) == BST_CHECKED);
    } else if (id == AN_ID_LIST && code == LBN_SELCHANGE) {
        i = (int)SendMessageW(g_an.list, LB_GETCURSEL, 0, 0);
        if (i >= 0 && i < RS_ANIM_POSES)
            An_Show(g_anPoses[i].what, g_anPoses[i].what == AN_WHAT_STEER ? g_anPoses[i].steer : g_an.steerNow, 0);
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
    (void)page;
    (void)wParam;
    *handled = 0;
    switch (msg) {
    case WM_HSCROLL:
        // The steering slider: steering is shown (Play stays as it is).
        if (g_an.steer && (HWND)lParam == g_an.steer) {
            An_Show(AN_WHAT_STEER, (int)SendMessageW(g_an.steer, TBM_GETPOS, 0, 0), g_an.playing);
            *handled = 1;
        }
        return 0;
    case WM_DESTROY:
        // Not handled: the page cleans up after this.
        g_an.created = 0;
        return 0;
    }
    return 0;
}

static int An_AutoWhat(const wchar_t *word)
{
    int i;
    if (_wcsicmp(word, L"steer") == 0 || _wcsicmp(word, L"steering") == 0)
        return AN_WHAT_STEER;
    for (i = 0; i < AN_SHOWS; i++)
        if (_wcsicmp(word, g_anShows[i].word) == 0)
            return i;
    return -2;
}

static const wchar_t *An_WhatWord(int what)
{
    return what >= 0 && what < AN_SHOWS ? g_anShows[what].word : L"steer";
}

int CharAnim_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    wchar_t word[32];
    const wchar_t *rest;
    int what, i;

    (void)page;
    if (wcscmp(verb, L"anim-steer") != 0 && wcscmp(verb, L"anim-play") != 0 && wcscmp(verb, L"anim-show") != 0 &&
        wcscmp(verb, L"anim-end") != 0)
        return RS_AUTO_UNKNOWN;
    if (!g_an.created) {
        Rs_AutoLog(L"  %ls: the card Animations is not there", verb);
        return RS_AUTO_FAIL;
    }

    if (wcscmp(verb, L"anim-steer") == 0) {
        wchar_t *end;
        long v = wcstol(arg, &end, 10);
        if (end == arg || *end || v < -AN_STEER_MAX || v > AN_STEER_MAX) {
            Rs_AutoLog(L"  %ls: '%ls' is not a whole number from %d to %d", verb, arg, -AN_STEER_MAX, AN_STEER_MAX);
            return RS_AUTO_FAIL;
        }
        An_Show(AN_WHAT_STEER, (int)v, g_an.playing);
        UpdateWindow(g_an.view);      // painted before a following "shot"
        Rs_AutoLog(L"  %ls: %d (frame %d of the steering)", verb, g_an.steerNow, AN_STEER_MAX + g_an.steerNow);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"anim-end") == 0) {
        if (_wcsicmp(arg, L"none") == 0)
            what = AN_WHAT_STEER;
        else if (_wcsicmp(arg, L"win") == 0)
            what = AN_WHAT_WIN;
        else if (_wcsicmp(arg, L"lose") == 0)
            what = AN_WHAT_LOSE;
        else {
            Rs_AutoLog(L"  %ls: say win, lose or none", verb);
            return RS_AUTO_FAIL;
        }
        An_Show(what, g_an.steerNow, 0);
        UpdateWindow(g_an.view);
        Rs_AutoLog(L"  %ls: %ls", verb, what == AN_WHAT_STEER ? L"none (steering)" : An_WhatWord(what));
        return RS_AUTO_DONE;
    }
    // anim-play <what> on|off, anim-show <what>
    for (i = 0; arg[i] && arg[i] != L' ' && i < 31; i++)
        word[i] = arg[i];
    word[i] = 0;
    rest = arg + i;
    while (*rest == L' ')
        rest++;
    if (wcscmp(verb, L"anim-show") == 0) {
        int p = An_PoseIndex(word);
        if (p >= 0 && g_anPoses[p].what == AN_WHAT_STEER) {
            An_Show(AN_WHAT_STEER, g_anPoses[p].steer, 0);
        } else {
            what = p >= 0 ? g_anPoses[p].what : An_AutoWhat(word);
            if (what < -1 || *rest) {
                Rs_AutoLog(L"  %ls: say steer, jump, crash, reverse, win, lose, steer_left or steer_right", verb);
                return RS_AUTO_FAIL;
            }
            An_Show(what, g_an.steerNow, 0);
        }
        UpdateWindow(g_an.view);
        Rs_AutoLog(L"  %ls: %ls, steering %d", verb, An_WhatWord(g_an.what), g_an.steerNow);
        return RS_AUTO_DONE;
    }
    what = An_AutoWhat(word);
    if (what < -1 || (_wcsicmp(rest, L"on") != 0 && _wcsicmp(rest, L"off") != 0)) {
        Rs_AutoLog(L"  %ls: say <steer|jump|crash|reverse|win|lose> on|off", verb);
        return RS_AUTO_FAIL;
    }
    An_Show(what, g_an.steerNow, _wcsicmp(rest, L"on") == 0);
    UpdateWindow(g_an.view);
    Rs_AutoLog(L"  %ls: %ls %ls", verb, An_WhatWord(g_an.what), g_an.playing ? L"on (a loop)" : L"off");
    return RS_AUTO_DONE;
}

int CharAnim_Busy(void)
{
    return 0;
}

void CharAnim_ModelChecked(HWND page, const wchar_t *model, const wchar_t *format, const struct RsAnimPoses *poses, int ok)
{
    (void)page;
    (void)ok;
    if (!g_an.created)
        return;
    An_Copy(g_an.model, AN_VAL, model);
    An_Copy(g_an.format, 8, model && model[0] ? format : L"");
    if (poses && model && model[0])
        g_an.poses = *poses;
    else
        memset(&g_an.poses, 0, sizeof(g_an.poses));
    An_FillList();
    An_StatusUpdate();
}

void CharAnim_PreviewLoaded(void)
{
    An_ViewApply();
}

void CharAnim_SteerFromPage(int steer)
{
    if (g_an.created)
        An_Show(AN_WHAT_STEER, steer, 0);
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

void CharAnim_Report(FILE *f)
{
    wchar_t state[400];
    int p;

    if (!f || !g_an.created)
        return;
    An_Put(f, L"animations card: enabled");
    An_Put(f, L"animations model: %ls, format %ls, shape keys %ls, poses said %ls", g_an.model[0] ? g_an.model : L"(none)",
           g_an.format[0] ? g_an.format : L"(not said)", An_Gltf() ? L"possible (glTF)" : L"none (OBJ or PLY)",
           g_an.poses.seen ? L"yes" : L"no");
    for (p = 0; p < RS_ANIM_POSES; p++) {
        An_StateText(p, state, 400);
        An_Put(f, L"animation pose %ls: %ls%ls - %ls", g_anPoses[p].name, An_State(p), g_an.poses.state[p][0] ? L"" : L" (not said)",
               state);
    }
    An_Put(f, L"animation keys ignored: %d%ls%ls", g_an.poses.unknown, g_an.poses.unknown ? L" " : L"", g_an.poses.unknownNames);
    An_Put(f, L"animation rig refused: %ls", g_an.poses.rig ? L"yes" : L"no");
    An_Put(f, L"animation preview: %ls, steering %d (frame %d), play %ls", An_WhatWord(g_an.what), g_an.steerNow,
           AN_STEER_MAX + g_an.steerNow, g_an.playing ? L"on" : L"off");
    An_Put(f, L"animations status: %ls", g_an.statusText);
}
