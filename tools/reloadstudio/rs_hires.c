// rs_hires.c - page "Hi-Res (Preview)": check a hi-res driver model and pack it into a character
//
// The author chooses a glTF model (.glb) made for the hi-res driver, and the
// page says at once whether the game would draw it: the check runs IN THIS
// PROCESS through rs_hires_model.c, which compiles include/rldhires.inc - the
// very rules the game and `rldpack hires-check` use - so the findings are the
// same, with the same texts. Beside the findings the page shows a summary
// (clips, wheels, skin, budget), the table of the 12 clips (present, or what
// the game plays instead) and a picture of the bind pose in the 3D view.
//
// Packing is not done here: the button starts `rldpack hires-add` through
// Rs_RunRldpack (arguments from RsHires_PackArgs), so the file the Studio
// writes is byte for byte the file rldpack writes. The page only shows rldpack's
// machine lines (@msg, @written).
//
// "Upscale small textures" (off by default): the button first starts `rldpack
// hires-upscale` (RsHires_UpscaleArgs), which writes a NEW model
// <model name>-upscaled.glb beside the output (RsHires_UpscaledPath) with its
// small base colour textures enlarged by Scale2x, a pixel-art upscaler
// (tools/rldpack_hires_upscale.inc); then hires-add packs that model. The
// chosen model is only read; rldpack still stores the GLB it is given byte
// for byte.
//
// A preview feature: the game draws a hi-res driver only with
// --engine-hires-chars; the card "Files" carries the mark.
//
// Automation verbs are listed in reloadstudio.h.

#include "reloadstudio.h"
#include "rs_hires_model.h"
#include "rs_view.h"

#include <math.h>
#include <string.h>
#include <stdarg.h>

#define HIRES_PATH 1024
#define HIRES_MSG_MAX 192            // findings kept for the report (check rows and pack lines)
#define HIRES_REPORT_CAP 65536       // wide characters of the report

enum {
    HIRES_ID_MODEL_LABEL = 6000, HIRES_ID_MODEL, HIRES_ID_MODEL_BROWSE,
    HIRES_ID_CHAR_LABEL, HIRES_ID_CHAR, HIRES_ID_CHAR_BROWSE,
    HIRES_ID_OUT_LABEL, HIRES_ID_OUT, HIRES_ID_OUT_BROWSE,
    HIRES_ID_CHECK, HIRES_ID_PACK, HIRES_ID_STATUS, HIRES_ID_MARK,
    HIRES_ID_VERDICT, HIRES_ID_SUMMARY, HIRES_ID_MSGS, HIRES_ID_CLIPS,
    HIRES_ID_VIEW, HIRES_ID_NOTE,
    HIRES_ID_CLIP_LABEL, HIRES_ID_CLIP, HIRES_ID_PLAY, HIRES_ID_TIME,
    HIRES_ID_UPSCALE
};

#define HIRES_TIMER 61               // the playback tick on the page window
#define HIRES_TICK_MS 50
#define HIRES_TICK_CAP_MS 200        // the longest step of one tick
#define HIRES_WHEEL_DEG_PER_S 180.0f // slow spin of the wheels while a clip plays

// Card geometry in 96-dpi pixels. Rs_CardInner takes 50 at the top and 16 at the bottom.
#define HIRES_CHROME 66
#define HIRES_FILES_INNER 186
#define HIRES_VERDICT_H 40
#define HIRES_SUMMARY_H 40
#define HIRES_MSGS_H 190
#define HIRES_CLIPS_H 296
#define HIRES_VIEW_MIN 360
#define HIRES_NOTE_H 56
#define HIRES_BAR_H 64               // the row "Clip" above the view and the time line
#define HIRES_WIDE_MIN 1100

struct HiresMsg {
    int severity;
    wchar_t rule[16];
    wchar_t text[RS_HIRES_TEXT_MAX];
};

struct HiresState {
    HWND modelLabel, model, modelBrowse;
    HWND charLabel, character, charBrowse;
    HWND outLabel, out, outBrowse;
    HWND checkBtn, pack, upscale, status, mark;
    HWND verdict, summary, msgs, clips;
    HWND view, note;
    HWND clipLabel, clipCombo, playBtn, timeLabel;
    HWND page;

    // The clip shown: 0 = the bind pose, 1 + n = clip n of RsHires_ClipName.
    int clipSel;
    int playing;
    float seconds;
    float wheelDeg;
    DWORD lastTick;
    int shownTris;                   // triangles of the last picture handed to the view

    int quiet;                       // the program sets a field: ignore EN_CHANGE
    int checked;                     // a check ran (g_hires.check and checkedPath are valid)
    wchar_t checkedPath[HIRES_PATH]; // the model of the last check
    struct RsHiresCheck check;       // its result

    struct HiresMsg msg[HIRES_MSG_MAX];
    int msgCount;

    int job;                         // running rldpack job, 0 = none
    int jobUpscale;                  // 1 = the job is hires-upscale (hires-add follows), 0 = hires-add
    int upscaleWritten;              // the last pack wrote an upscaled model (upscaledPath, upscaledSha)
    wchar_t upscaledPath[HIRES_PATH];
    wchar_t upscaledSha[80];
    int packExit;                    // exit code of the last pack, valid when packDone
    int packDone;
    int packWritten;                 // an @written line came
    wchar_t packPath[HIRES_PATH];
    wchar_t packSha[80];

    // rldpack hires-add checks the model again with the same code: its HRG-*
    // findings are the ones the list shows already. They are kept here and
    // shown only when the pack fails (the file changed since the check).
    struct HiresMsg packRule[RS_HIRES_ROWS_MAX];
    int packRuleCount;
};

static struct HiresState g_hires;

static const wchar_t *const g_hiresPresetWords[RS_VIEW_PRESET_COUNT] = { L"front", L"side", L"back", L"top", L"34", L"race" };

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void Hires_Copy(wchar_t *dst, int cap, const wchar_t *src)
{
    wcsncpy(dst, src, (size_t)cap - 1);
    dst[cap - 1] = 0;
}

// The text of a field without blanks and quotes around it.
static void Hires_Field(HWND edit, wchar_t *out, int cap)
{
    wchar_t *p, *end;
    GetWindowTextW(edit, out, cap);
    out[cap - 1] = 0;
    p = out;
    while (*p == L' ' || *p == L'\t' || *p == L'"')
        p++;
    end = p + wcslen(p);
    while (end > p && (end[-1] == L' ' || end[-1] == L'\t' || end[-1] == L'"' || end[-1] == L'\r' || end[-1] == L'\n'))
        end--;
    *end = 0;
    if (p != out)
        memmove(out, p, ((size_t)(end - p) + 1) * sizeof(wchar_t));
}

static void Hires_SetField(HWND edit, const wchar_t *text)
{
    g_hires.quiet = 1;
    Rs_SetText(edit, text);
    g_hires.quiet = 0;
}

static void Hires_SetLabel(HWND label, const wchar_t *text, COLORREF color)
{
    Rs_SetText(label, text);
    Rs_SetTextColor(label, color);
}

static const wchar_t *Hires_SeverityWord(int severity)
{
    switch (severity) {
    case RS_SEV_ERROR: return L"error";
    case RS_SEV_WARNING: return L"warning";
    case RS_SEV_NOTE: return L"note";
    case RS_SEV_OK: return L"ok";
    }
    return L"info";
}

// A finding for the list and for the report.
static void Hires_MsgAdd(int severity, const wchar_t *rule, const wchar_t *text, const wchar_t *detail)
{
    Rs_MsgListAdd(g_hires.msgs, severity, text, detail);
    if (g_hires.msgCount < HIRES_MSG_MAX) {
        struct HiresMsg *m = &g_hires.msg[g_hires.msgCount++];
        m->severity = severity;
        Hires_Copy(m->rule, 16, rule && rule[0] ? rule : L"-");
        Hires_Copy(m->text, RS_HIRES_TEXT_MAX, text);
    }
}

static int Hires_Running(void)
{
    return g_hires.job != 0;
}

// 1 when the pack button may work: the model of the last check is usable, a
// .glb (not the CHRG of a character) and still the one in the field,
// character and output are filled, no job runs.
static int Hires_CanPack(void)
{
    wchar_t model[HIRES_PATH], ch[HIRES_PATH], out[HIRES_PATH];
    if (Hires_Running() || !g_hires.checked || !g_hires.check.usable || g_hires.check.fromCharacter)
        return 0;
    Hires_Field(g_hires.model, model, HIRES_PATH);
    Hires_Field(g_hires.character, ch, HIRES_PATH);
    Hires_Field(g_hires.out, out, HIRES_PATH);
    return model[0] && ch[0] && out[0] && wcscmp(model, g_hires.checkedPath) == 0;
}

// 1 when the clips of the checked model can be played.
static int Hires_AnimOk(void)
{
    return g_hires.checked && g_hires.check.read && g_hires.check.animReady;
}

static void Hires_UpdateButtons(void)
{
    EnableWindow(g_hires.pack, Hires_CanPack());
    EnableWindow(g_hires.checkBtn, !Hires_Running());
    EnableWindow(g_hires.upscale, !Hires_Running());
    EnableWindow(g_hires.clipCombo, Hires_AnimOk());
    EnableWindow(g_hires.playBtn, Hires_AnimOk() && g_hires.clipSel > 0);
}

// ---------------------------------------------------------------------------
// The clip shown: the combo, the time line and the playback
// ---------------------------------------------------------------------------

static void Hires_FillCombo(void)
{
    const struct RsHiresCheck *c = &g_hires.check;
    int i, sel = g_hires.clipSel;
    SendMessageW(g_hires.clipCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_hires.clipCombo, CB_ADDSTRING, 0, (LPARAM)L"Bind pose");
    for (i = 0; i < RS_HIRES_CLIPS; i++) {
        wchar_t text[160];
        if (!g_hires.checked || !c->read || c->clipState[i] == RS_HIRES_CLIP_PRESENT)
            Hires_Copy(text, 160, RsHires_ClipName(i));
        else if (c->clipState[i] == RS_HIRES_CLIP_FALLBACK)
            swprintf(text, 160, L"%ls (missing - plays %ls%ls)", RsHires_ClipName(i), RsHires_ClipName(c->clipVia[i]),
                     c->clipFullLock[i] ? L" at full lock" : L"");
        else
            swprintf(text, 160, L"%ls (missing - rest pose)", RsHires_ClipName(i));
        SendMessageW(g_hires.clipCombo, CB_ADDSTRING, 0, (LPARAM)text);
    }
    if (sel < 0 || sel > RS_HIRES_CLIPS)
        sel = 0;
    g_hires.clipSel = sel;
    SendMessageW(g_hires.clipCombo, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void Hires_UpdateTime(void)
{
    const struct RsHiresCheck *c = &g_hires.check;
    wchar_t text[160];
    text[0] = 0;
    if (g_hires.checked && c->read && !c->animReady) {
        Hires_Copy(text, 160, c->animWhy[0] ? c->animWhy : L"Clips cannot be played for this model.");
    } else if (Hires_AnimOk() && g_hires.clipSel > 0) {
        int clip = g_hires.clipSel - 1;
        float len = RsHires_AnimLength(c, clip);
        float share = -1.0f;
        float shown = RsHires_AnimLocal(c, clip, g_hires.seconds, &share);
        if (len > 0.0f) {
            // The clip's own time, as the model samples it (a steer clip: share x length).
            swprintf(text, 160, L"%.2f s of %.2f s", (double)shown, (double)len);
            if (share >= 0.0f)
                swprintf(text + wcslen(text), 160 - wcslen(text), L" - steer share %.2f", (double)share);
        }
    }
    Hires_SetLabel(g_hires.timeLabel, text, RS_COL_MUTED);
}

// The picture of the selected clip at the current time; returns its triangles (0 = could not).
static int Hires_ShowClipFrame(void)
{
    const unsigned char *bytes = NULL;
    size_t size = 0;
    int n = RsHires_AnimFrame(&g_hires.check, g_hires.clipSel - 1, g_hires.seconds, g_hires.wheelDeg, &bytes, &size);
    if (n <= 0 || !bytes)
        return 0;
    // A frame has the layout of the bind picture: copied in place, the textures
    // and their mip levels stay (no new read of 30 MB of textures per tick).
    if (!RsView_UpdatePreviewData(g_hires.view, bytes, size, g_hires.check.previewKeepAt, g_hires.check.previewKeepBytes)) {
        RsView_SetModelKey(g_hires.view, g_hires.checkedPath);
        RsView_LoadPreviewData(g_hires.view, bytes, size);
    }
    RsView_SetPose(g_hires.view, RS_VIEW_POSE_NEUTRAL);
    RsView_SetWheels(g_hires.view, 0);   // the picture carries the standard wheels (rs_hires_model.c)
    g_hires.shownTris = n;
    return n;
}

static void Hires_ShowView(void);

static void Hires_SetPlaying(int on)
{
    if (on && !(Hires_AnimOk() && g_hires.clipSel > 0))
        on = 0;
    if (on == g_hires.playing)
        return;
    g_hires.playing = on;
    if (on) {
        g_hires.lastTick = GetTickCount();
        SetTimer(g_hires.page, HIRES_TIMER, HIRES_TICK_MS, NULL);
    } else {
        KillTimer(g_hires.page, HIRES_TIMER);
    }
    Rs_SetText(g_hires.playBtn, on ? L"Pause" : L"Play");
}

static void Hires_Tick(void)
{
    DWORD now = GetTickCount();
    DWORD dt = now - g_hires.lastTick;
    if (!g_hires.playing)
        return;
    if (!IsWindowVisible(g_hires.page)) {
        Hires_SetPlaying(0);
        return;
    }
    if (dt > HIRES_TICK_CAP_MS)
        dt = HIRES_TICK_CAP_MS;
    g_hires.lastTick = now;
    g_hires.seconds += (float)dt / 1000.0f;
    g_hires.wheelDeg = (float)fmod((double)(g_hires.wheelDeg + HIRES_WHEEL_DEG_PER_S * (float)dt / 1000.0f), 360.0);
    Hires_ShowView();
    Hires_UpdateTime();
}

// Picks "Bind pose" (0) or a clip (1 + n) as the combo does.
static void Hires_SelectClip(int sel)
{
    g_hires.clipSel = sel;
    SendMessageW(g_hires.clipCombo, CB_SETCURSEL, (WPARAM)sel, 0);
    if (sel == 0)
        Hires_SetPlaying(0);
    Hires_ShowView();
    Hires_UpdateTime();
    Hires_UpdateButtons();
}

// ---------------------------------------------------------------------------
// Showing a check
// ---------------------------------------------------------------------------

static void Hires_ShowVerdict(void)
{
    const struct RsHiresCheck *c = &g_hires.check;
    wchar_t text[RS_HIRES_TEXT_MAX + 64];
    if (!g_hires.checked) {
        Hires_SetLabel(g_hires.verdict, L"Choose a hi-res model (.glb).", RS_COL_MUTED);
    } else if (!c->read) {
        Hires_SetLabel(g_hires.verdict, c->why, RS_COL_ERROR);
    } else if (c->usable) {
        Hires_SetLabel(g_hires.verdict, L"Usable as a hi-res driver - the game draws this model.", RS_COL_OK);
    } else {
        swprintf(text, RS_HIRES_TEXT_MAX + 64,
                 L"Refused - the game draws the classic model instead (%d error(s)).", c->errors);
        Hires_SetLabel(g_hires.verdict, text, RS_COL_ERROR);
    }
}

static void Hires_ShowSummary(void)
{
    const struct RsHiresCheck *c = &g_hires.check;
    wchar_t line[640], skin[48];
    if (!g_hires.checked || !c->read) {
        Rs_SetText(g_hires.summary, L"");
        return;
    }
    if (c->joints)
        swprintf(skin, 48, L"%u joints", c->joints);
    else
        wcscpy(skin, L"no skin");
    swprintf(line, 640,
             L"Clips %d/%d - Wheels %d/4 (%ls) - Skin %ls\r\n"
             L"Triangles %llu of %llu - Textures %d, %.1f MiB of %.1f MiB (largest edge %u of %u)",
             c->clips, RS_HIRES_CLIPS, c->wheels, c->wheels == 4 ? L"own wheels" : L"standard wheels", skin,
             c->triangles, c->budgetTriangles, c->textures,
             (double)c->textureBytes / 1048576.0, (double)c->budgetTextureBytes / 1048576.0,
             c->textureEdge, c->budgetEdge);
    Rs_SetText(g_hires.summary, line);
}

static void Hires_ClipText(int clip, wchar_t *state, int cap, wchar_t *length, int lcap)
{
    const struct RsHiresCheck *c = &g_hires.check;
    state[0] = 0;
    length[0] = 0;
    if (!g_hires.checked || !c->read)
        return;
    switch (c->clipState[clip]) {
    case RS_HIRES_CLIP_PRESENT:
        Hires_Copy(state, cap, L"yes");
        swprintf(length, lcap, L"%.2f s", (double)c->clipSeconds[clip]);
        break;
    case RS_HIRES_CLIP_FALLBACK:
        swprintf(state, cap, L"missing - plays %ls%ls", RsHires_ClipName(c->clipVia[clip]),
                 c->clipFullLock[clip] ? L" at full lock" : L"");
        break;
    default:
        Hires_Copy(state, cap, L"missing - rest pose");
        break;
    }
}

static void Hires_ShowClips(void)
{
    int i;
    for (i = 0; i < RS_HIRES_CLIPS; i++) {
        wchar_t state[96], length[32];
        Hires_ClipText(i, state, 96, length, 32);
        ListView_SetItemText(g_hires.clips, i, 1, state);
        ListView_SetItemText(g_hires.clips, i, 2, length);
    }
}

static void Hires_ShowList(void)
{
    const struct RsHiresCheck *c = &g_hires.check;
    int i;
    Rs_MsgListClear(g_hires.msgs);
    g_hires.msgCount = 0;
    if (!g_hires.checked || !c->read)
        return;
    for (i = 0; i < c->rowCount; i++)
        Hires_MsgAdd(c->rows[i].severity, c->rows[i].rule, c->rows[i].text, c->rows[i].rule);
    if (c->rowsDropped > 0) {
        wchar_t text[64];
        swprintf(text, 64, L"%d more findings not shown", c->rowsDropped);
        Hires_MsgAdd(RS_SEV_INFO, L"-", text, NULL);
    }
    if (c->rowCount == 0 && c->usable)
        Hires_MsgAdd(RS_SEV_OK, L"-", L"No finding: every rule of the format holds.", NULL);
}

static void Hires_ShowView(void)
{
    const struct RsHiresCheck *c = &g_hires.check;
    wchar_t msg[RS_HIRES_TEXT_MAX + 32];
    int i;
    if (!g_hires.checked) {
        RsView_Clear(g_hires.view, NULL);
        return;
    }
    if (c->read && c->animReady && g_hires.clipSel > 0 && Hires_ShowClipFrame())
        return;
    if (c->read && c->preview) {
        g_hires.shownTris = c->previewTriangles;
        RsView_SetModelKey(g_hires.view, g_hires.checkedPath);
        RsView_LoadPreviewData(g_hires.view, c->preview, c->previewBytes);
        RsView_SetPose(g_hires.view, RS_VIEW_POSE_NEUTRAL);
        RsView_SetWheels(g_hires.view, 0);
        return;
    }
    if (!c->read) {
        swprintf(msg, RS_HIRES_TEXT_MAX + 32, L"Nothing to draw: %ls", c->why);
    } else {
        const wchar_t *first = L"the file has no triangle to draw";
        for (i = 0; i < c->rowCount; i++)
            if (c->rows[i].severity == RS_SEV_ERROR) {
                first = c->rows[i].text;
                break;
            }
        swprintf(msg, RS_HIRES_TEXT_MAX + 32, L"Nothing to draw: %ls", first);
    }
    RsView_Clear(g_hires.view, msg);
}

static void Hires_ShowAll(void)
{
    Hires_FillCombo();
    Hires_ShowVerdict();
    Hires_ShowSummary();
    Hires_ShowClips();
    Hires_ShowList();
    Hires_ShowView();
    Hires_UpdateTime();
    Hires_UpdateButtons();
}

// The check of the model field, synchronously. 1 = read, 0 = not read or no model.
static int Hires_RunCheck(void)
{
    wchar_t model[HIRES_PATH];
    Hires_Field(g_hires.model, model, HIRES_PATH);
    // A new check stops the playing and starts at 0; the clip stays chosen
    // for the same model, "Bind pose" for another one.
    Hires_SetPlaying(0);
    g_hires.seconds = 0.0f;
    g_hires.wheelDeg = 0.0f;
    if (!g_hires.checked || wcscmp(model, g_hires.checkedPath) != 0)
        g_hires.clipSel = 0;
    RsHires_CheckFree(&g_hires.check);
    memset(&g_hires.check, 0, sizeof(g_hires.check));
    g_hires.packDone = 0;
    g_hires.packWritten = 0;
    g_hires.packPath[0] = 0;
    g_hires.packSha[0] = 0;
    Rs_SetText(g_hires.status, L"");
    if (!model[0]) {
        g_hires.checked = 0;
        g_hires.checkedPath[0] = 0;
        Hires_ShowAll();
        return 0;
    }
    g_hires.checked = 1;
    Hires_Copy(g_hires.checkedPath, HIRES_PATH, model);
    RsHires_CheckFile(model, &g_hires.check);
    Hires_ShowAll();
    if (g_hires.check.read && g_hires.check.fromCharacter)
        Hires_SetLabel(g_hires.status, L"This is the hi-res model inside a character. To pack, choose its .glb.", RS_COL_MUTED);
    return g_hires.check.read;
}

// "<folder of the character>\<name without .rldchar>-hires.rldchar" into an empty output field.
static void Hires_FillOutput(void)
{
    wchar_t ch[HIRES_PATH], out[HIRES_PATH], dir[HIRES_PATH], name[HIRES_PATH], full[HIRES_PATH];
    wchar_t *dot;
    Hires_Field(g_hires.out, out, HIRES_PATH);
    Hires_Field(g_hires.character, ch, HIRES_PATH);
    if (out[0] || !ch[0])
        return;
    Rs_PathDir(dir, HIRES_PATH, ch);
    Hires_Copy(name, HIRES_PATH, Rs_PathName(ch));
    dot = wcsrchr(name, L'.');
    if (dot && _wcsicmp(dot, L".rldchar") == 0)
        *dot = 0;
    wcscat(name, L"-hires.rldchar");
    if (dir[0])
        Rs_PathJoin(full, HIRES_PATH, dir, name);
    else
        Hires_Copy(full, HIRES_PATH, name);
    Hires_SetField(g_hires.out, full);
}

// ---------------------------------------------------------------------------
// Packing
// ---------------------------------------------------------------------------

// 1 when "Upscale small textures" is ticked.
static int Hires_UpscaleOn(void)
{
    return SendMessageW(g_hires.upscale, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// The pack step: hires-add of model (the chosen one, or the upscaled one).
static int Hires_StartAdd(HWND page, const wchar_t *model)
{
    wchar_t ch[HIRES_PATH], out[HIRES_PATH];
    const wchar_t *args[RS_HIRES_PACK_ARGS];
    int n;
    Hires_Field(g_hires.character, ch, HIRES_PATH);
    Hires_Field(g_hires.out, out, HIRES_PATH);
    // The page Hi-Res (automation and self-tests only) packs without the
    // switch --vertex-colors: mode glTF, the bytes of before (its ctest).
    n = RsHires_PackArgs(ch, model, out, -1, args);
    g_hires.jobUpscale = 0;
    g_hires.job = Rs_RunRldpack(page, args, n);
    if (!g_hires.job) {
        Hires_SetLabel(g_hires.status, L"Packing could not be started.", RS_COL_ERROR);
        Hires_UpdateButtons();
        return 0;
    }
    Hires_SetLabel(g_hires.status, L"Packing ...", RS_COL_MUTED);
    Hires_UpdateButtons();
    return 1;
}

static int Hires_StartPack(HWND page)
{
    wchar_t model[HIRES_PATH], out[HIRES_PATH];
    const wchar_t *args[RS_HIRES_UPSCALE_ARGS];
    int n;
    if (!Hires_CanPack())
        return 0;
    Hires_Field(g_hires.model, model, HIRES_PATH);
    Hires_Field(g_hires.out, out, HIRES_PATH);
    g_hires.packDone = 0;
    g_hires.packWritten = 0;
    g_hires.packPath[0] = 0;
    g_hires.packSha[0] = 0;
    g_hires.packRuleCount = 0;
    g_hires.upscaleWritten = 0;
    g_hires.upscaledPath[0] = 0;
    g_hires.upscaledSha[0] = 0;
    if (!Hires_UpscaleOn())
        return Hires_StartAdd(page, model);
    if (!RsHires_UpscaledPath(model, out, g_hires.upscaledPath, HIRES_PATH) || _wcsicmp(g_hires.upscaledPath, model) == 0) {
        g_hires.upscaledPath[0] = 0;
        Hires_SetLabel(g_hires.status, L"The upscaled model has no place beside the output - choose another output.", RS_COL_ERROR);
        return 0;
    }
    n = RsHires_UpscaleArgs(model, g_hires.upscaledPath, args);
    g_hires.jobUpscale = 1;
    g_hires.job = Rs_RunRldpack(page, args, n);
    if (!g_hires.job) {
        g_hires.jobUpscale = 0;
        Hires_SetLabel(g_hires.status, L"Upscaling could not be started.", RS_COL_ERROR);
        Hires_UpdateButtons();
        return 0;
    }
    Hires_SetLabel(g_hires.status, L"Upscaling small textures ...", RS_COL_MUTED);
    Hires_UpdateButtons();
    return 1;
}

static void Hires_PackLine(wchar_t *line)
{
    wchar_t *f[8];
    int n = Rs_SplitMachine(line, f, 8);
    if (n < 1)
        return;
    if (wcscmp(f[0], L"msg") == 0 && n >= 4) {
        if (wcsncmp(f[2], L"HRG-", 4) == 0) {
            if (g_hires.packRuleCount < RS_HIRES_ROWS_MAX) {
                struct HiresMsg *m = &g_hires.packRule[g_hires.packRuleCount++];
                m->severity = Rs_SeverityFromText(f[1]);
                Hires_Copy(m->rule, 16, f[2]);
                Hires_Copy(m->text, RS_HIRES_TEXT_MAX, f[3]);
            }
        } else {
            Hires_MsgAdd(Rs_SeverityFromText(f[1]), f[2], f[3], f[2]);
        }
    } else if (wcscmp(f[0], L"written") == 0 && n >= 3) {
        if (g_hires.jobUpscale) {
            Hires_Copy(g_hires.upscaledPath, HIRES_PATH, f[1]);
            Hires_Copy(g_hires.upscaledSha, 80, f[2]);
            g_hires.upscaleWritten = 1;
            return;
        }
        Hires_Copy(g_hires.packPath, HIRES_PATH, f[1]);
        Hires_Copy(g_hires.packSha, 80, f[2]);
        g_hires.packWritten = 1;
    }
}

static void Hires_PackDone(HWND page, int code)
{
    wchar_t text[HIRES_PATH + 160];
    g_hires.job = 0;
    if (g_hires.jobUpscale) {
        // The upscaled model is written: pack it. Else stop here.
        g_hires.jobUpscale = 0;
        if (code == 0 && g_hires.upscaleWritten && Hires_StartAdd(page, g_hires.upscaledPath))
            return;
        g_hires.packDone = 1;
        g_hires.packExit = code != 0 ? code : 1;
        swprintf(text, 160, L"Upscaling failed (exit %d) - see the findings above.", code);
        Hires_SetLabel(g_hires.status, text, RS_COL_ERROR);
        Hires_UpdateButtons();
        return;
    }
    g_hires.packDone = 1;
    g_hires.packExit = code;
    if (code != 0) {
        int i;
        for (i = 0; i < g_hires.packRuleCount; i++)
            Hires_MsgAdd(g_hires.packRule[i].severity, g_hires.packRule[i].rule, g_hires.packRule[i].text, g_hires.packRule[i].rule);
    }
    if (code == 0 && g_hires.packWritten) {
        wchar_t sha[20];
        Hires_Copy(sha, 17, g_hires.packSha);
        swprintf(text, HIRES_PATH + 160,
                 g_hires.upscaleWritten ? L"Packed into %ls with %ls - sha256 %ls... (the same bytes as rldpack hires-add)"
                                        : L"Packed into %ls%ls - sha256 %ls... (the same bytes as rldpack hires-add)",
                 Rs_PathName(g_hires.packPath), g_hires.upscaleWritten ? Rs_PathName(g_hires.upscaledPath) : L"", sha);
        Hires_SetLabel(g_hires.status, text, RS_COL_OK);
    } else {
        swprintf(text, 160, L"Packing failed (exit %d) - see the findings above.", code);
        Hires_SetLabel(g_hires.status, text, RS_COL_ERROR);
    }
    Hires_UpdateButtons();
}

// ---------------------------------------------------------------------------
// Report (automation)
// ---------------------------------------------------------------------------

static void Hires_Add(wchar_t *buf, int *len, const wchar_t *fmt, ...)
{
    va_list ap;
    int n;
    if (*len >= HIRES_REPORT_CAP - 1)
        return;
    va_start(ap, fmt);
    n = _vsnwprintf(buf + *len, (size_t)(HIRES_REPORT_CAP - *len - 1), fmt, ap);
    va_end(ap);
    if (n < 0)
        n = HIRES_REPORT_CAP - 1 - *len;
    *len += n;
    buf[*len] = 0;
}

static int Hires_Report(const wchar_t *path)
{
    const struct RsHiresCheck *c = &g_hires.check;
    wchar_t *buf = (wchar_t *)Rs_Alloc((size_t)HIRES_REPORT_CAP * sizeof(wchar_t));
    wchar_t model[HIRES_PATH], ch[HIRES_PATH], out[HIRES_PATH];
    const wchar_t *verdict;
    int len = 0, i, ok;

    Hires_Field(g_hires.model, model, HIRES_PATH);
    Hires_Field(g_hires.character, ch, HIRES_PATH);
    Hires_Field(g_hires.out, out, HIRES_PATH);
    Hires_Add(buf, &len, L"model: %ls\n", model[0] ? model : L"none");
    Hires_Add(buf, &len, L"character: %ls\n", ch[0] ? ch : L"none");
    Hires_Add(buf, &len, L"output: %ls\n", out[0] ? out : L"none");
    verdict = !g_hires.checked ? L"none" : !c->read ? L"unread" : c->usable ? L"usable" : L"refused";
    Hires_Add(buf, &len, L"verdict: %ls\n", verdict);
    if (g_hires.checked && c->read) {
        Hires_Add(buf, &len, L"summary: clips %d/%d wheels %d/4 joints %u triangles %llu textures %d bytes %llu edge %u\n",
                  c->clips, RS_HIRES_CLIPS, c->wheels, c->joints, c->triangles, c->textures, c->textureBytes,
                  c->textureEdge);
    } else if (g_hires.checked) {
        Hires_Add(buf, &len, L"why: %ls\n", c->why);
    }
    for (i = 0; i < g_hires.msgCount; i++)
        Hires_Add(buf, &len, L"row: %ls %ls %ls\n", Hires_SeverityWord(g_hires.msg[i].severity), g_hires.msg[i].rule,
                  g_hires.msg[i].text);
    if (g_hires.checked && c->read) {
        for (i = 0; i < RS_HIRES_CLIPS; i++) {
            if (c->clipState[i] == RS_HIRES_CLIP_PRESENT)
                Hires_Add(buf, &len, L"clip: %ls present %.2f\n", RsHires_ClipName(i), (double)c->clipSeconds[i]);
            else if (c->clipState[i] == RS_HIRES_CLIP_FALLBACK)
                Hires_Add(buf, &len, L"clip: %ls fallback %ls%ls\n", RsHires_ClipName(i), RsHires_ClipName(c->clipVia[i]),
                          c->clipFullLock[i] ? L" full-lock" : L"");
            else
                Hires_Add(buf, &len, L"clip: %ls rest\n", RsHires_ClipName(i));
        }
    }
    Hires_Add(buf, &len, L"clip-shown: %ls %.3f %.1f %ls\n",
              g_hires.clipSel > 0 ? RsHires_ClipName(g_hires.clipSel - 1) : L"bind", (double)g_hires.seconds,
              (double)g_hires.wheelDeg, g_hires.playing ? L"playing" : L"paused");
    Hires_Add(buf, &len, L"anim: %ls\n", Hires_AnimOk() ? L"ready" : c->animWhy[0] ? c->animWhy : L"unavailable");
    Hires_Add(buf, &len, L"preview: %d\n", g_hires.checked && c->read ? c->previewTriangles : 0);
    Hires_Add(buf, &len, L"preview-textured: %ls%ls%ls\n", (g_hires.checked && c->read && c->previewTextured) ? L"yes" : L"no",
              (g_hires.checked && c->read && !c->previewTextured && c->previewWhy[0]) ? L" - " : L"",
              (g_hires.checked && c->read && !c->previewTextured) ? c->previewWhy : L"");
    Hires_Add(buf, &len, L"upscale: %ls\n", Hires_UpscaleOn() ? L"on" : L"off");
    if (g_hires.upscaleWritten)
        Hires_Add(buf, &len, L"upscaled: %ls %ls\n", g_hires.upscaledPath, g_hires.upscaledSha);
    else
        Hires_Add(buf, &len, L"upscaled: none\n");
    if (g_hires.packWritten)
        Hires_Add(buf, &len, L"packed: %ls %ls\n", g_hires.packPath, g_hires.packSha);
    else
        Hires_Add(buf, &len, L"packed: none\n");
    if (g_hires.packDone)
        Hires_Add(buf, &len, L"pack-exit: %d\n", g_hires.packExit);
    else
        Hires_Add(buf, &len, L"pack-exit: none\n");
    ok = Rs_WriteTextFile(path, buf);
    Rs_Free(buf);
    return ok;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

static void Hires_LayoutRow(const RECT *in, int *y, HWND label, HWND edit, HWND browse)
{
    int x = in->left, w = in->right - in->left;
    int lw = Rs_Px(112), bw = Rs_Px(104);
    MoveWindow(label, x, *y + Rs_Px(6), lw - Rs_Px(8), Rs_Px(20), TRUE);
    MoveWindow(edit, x + lw, *y + Rs_Px(2), w - lw - bw - Rs_Px(8), Rs_Px(28), TRUE);
    MoveWindow(browse, in->right - bw, *y, bw, Rs_Px(32), TRUE);
    *y += Rs_Px(40);
}

static void Hires_LayoutFiles(const RECT *outer, const RECT *in)
{
    int x = in->left, y = in->top;
    int markW = Rs_TextWidth(g_hires.mark, L"Preview feature") + Rs_Px(4);
    MoveWindow(g_hires.mark, outer->right - Rs_Px(20) - markW, outer->top + Rs_Px(20), markW, Rs_Px(18), TRUE);
    Hires_LayoutRow(in, &y, g_hires.modelLabel, g_hires.model, g_hires.modelBrowse);
    Hires_LayoutRow(in, &y, g_hires.charLabel, g_hires.character, g_hires.charBrowse);
    Hires_LayoutRow(in, &y, g_hires.outLabel, g_hires.out, g_hires.outBrowse);
    y += Rs_Px(4);
    MoveWindow(g_hires.checkBtn, x, y, Rs_Px(104), Rs_Px(32), TRUE);
    MoveWindow(g_hires.pack, x + Rs_Px(112), y, Rs_Px(200), Rs_Px(32), TRUE);
    MoveWindow(g_hires.upscale, x + Rs_Px(328), y + Rs_Px(4), in->right - x - Rs_Px(328), Rs_Px(24), TRUE);
    y += Rs_Px(40);
    MoveWindow(g_hires.status, x, y, in->right - x, Rs_Px(20), TRUE);
}

static void Hires_LayoutCheck(const RECT *in)
{
    int x = in->left, y = in->top, w = in->right - in->left;
    int cw1, cw3;
    MoveWindow(g_hires.verdict, x, y, w, Rs_Px(HIRES_VERDICT_H), TRUE);
    y += Rs_Px(HIRES_VERDICT_H);
    MoveWindow(g_hires.summary, x, y, w, Rs_Px(HIRES_SUMMARY_H), TRUE);
    y += Rs_Px(HIRES_SUMMARY_H + 8);
    MoveWindow(g_hires.msgs, x, y, w, Rs_Px(HIRES_MSGS_H), TRUE);
    y += Rs_Px(HIRES_MSGS_H + 12);
    MoveWindow(g_hires.clips, x, y, w, Rs_Px(HIRES_CLIPS_H), TRUE);
    cw1 = Rs_Px(90);
    cw3 = Rs_Px(90);
    ListView_SetColumnWidth(g_hires.clips, 0, cw1);
    ListView_SetColumnWidth(g_hires.clips, 1, w - cw1 - cw3 - Rs_Metric(g_hires.clips, SM_CXVSCROLL) - Rs_Px(4));
    ListView_SetColumnWidth(g_hires.clips, 2, cw3);
}

static void Hires_LayoutPreview(const RECT *in, int viewH)
{
    int x = in->left, y = in->top, w = in->right - in->left;
    int lw = Rs_Px(48), bw = Rs_Px(88);
    MoveWindow(g_hires.clipLabel, x, y + Rs_Px(6), lw - Rs_Px(8), Rs_Px(20), TRUE);
    SendMessageW(g_hires.clipCombo, CB_SETITEMHEIGHT, (WPARAM)-1, Rs_Px(22));
    MoveWindow(g_hires.clipCombo, x + lw, y + Rs_Px(2), w - lw - bw - Rs_Px(8), Rs_Px(300), TRUE);
    MoveWindow(g_hires.playBtn, in->right - bw, y, bw, Rs_Px(32), TRUE);
    MoveWindow(g_hires.timeLabel, x + lw, y + Rs_Px(36), w - lw, Rs_Px(20), TRUE);
    y += Rs_Px(HIRES_BAR_H);
    MoveWindow(g_hires.view, x, y, w, viewH, TRUE);
    MoveWindow(g_hires.note, x, y + viewH + Rs_Px(6), w, Rs_Px(HIRES_NOTE_H - 6), TRUE);
}

static void Hires_Layout(HWND page, int w, int h)
{
    int left = Rs_Px(32), right = w - Rs_Px(32);
    int top = Rs_PageTop();
    int gap = Rs_Px(16);
    int filesH = Rs_Px(HIRES_CHROME + HIRES_FILES_INNER);
    int checkH = Rs_Px(HIRES_CHROME + HIRES_VERDICT_H + HIRES_SUMMARY_H + 8 + HIRES_MSGS_H + 12 + HIRES_CLIPS_H);
    int viewH, viewMax, previewH;
    int vw = 0, vh = 0;
    RECT rc, in;
    (void)h;

    Rs_PageViewSize(page, &vw, &vh);
    Rs_CardClear(page);

    if (w >= Rs_Px(HIRES_WIDE_MIN)) {
        int leftW = (right - left - gap) * 11 / 20;
        int rx = left + leftW + gap;
        int columnH = filesH + gap + checkH;

        SetRect(&rc, left, top, left + leftW, top + filesH);
        Rs_CardAdd(page, &rc, L"Files");
        in = Rs_CardInner(&rc, 1);
        Hires_LayoutFiles(&rc, &in);

        SetRect(&rc, left, top + filesH + gap, left + leftW, top + columnH);
        Rs_CardAdd(page, &rc, L"Check");
        in = Rs_CardInner(&rc, 1);
        Hires_LayoutCheck(&in);

        // The view fills the right column but is no taller than what can be seen at once.
        SetRect(&rc, rx, top, right, top + columnH);
        in = Rs_CardInner(&rc, 1);
        viewH = (in.bottom - in.top) - Rs_Px(HIRES_NOTE_H + HIRES_BAR_H);
        viewMax = vh - top - Rs_Px(HIRES_CHROME + HIRES_NOTE_H + HIRES_BAR_H) - Rs_Px(8);
        if (viewH > viewMax)
            viewH = viewMax;
        if (viewH < Rs_Px(HIRES_VIEW_MIN))
            viewH = Rs_Px(HIRES_VIEW_MIN);
        previewH = Rs_Px(HIRES_CHROME + HIRES_NOTE_H + HIRES_BAR_H) + viewH;
        SetRect(&rc, rx, top, right, top + previewH);
        Rs_CardAdd(page, &rc, L"Preview");
        in = Rs_CardInner(&rc, 1);
        Hires_LayoutPreview(&in, viewH);
    } else {
        int y = top;
        SetRect(&rc, left, y, right, y + filesH);
        Rs_CardAdd(page, &rc, L"Files");
        in = Rs_CardInner(&rc, 1);
        Hires_LayoutFiles(&rc, &in);
        y += filesH + gap;

        SetRect(&rc, left, y, right, y + checkH);
        Rs_CardAdd(page, &rc, L"Check");
        in = Rs_CardInner(&rc, 1);
        Hires_LayoutCheck(&in);
        y += checkH + gap;

        viewH = Rs_Px(HIRES_VIEW_MIN);
        previewH = Rs_Px(HIRES_CHROME + HIRES_NOTE_H + HIRES_BAR_H) + viewH;
        SetRect(&rc, left, y, right, y + previewH);
        Rs_CardAdd(page, &rc, L"Preview");
        in = Rs_CardInner(&rc, 1);
        Hires_LayoutPreview(&in, viewH);
    }
}

// ---------------------------------------------------------------------------
// Create, commands, messages
// ---------------------------------------------------------------------------

static void Hires_Create(HWND page)
{
    static const wchar_t *const heads[3] = { L"Clip", L"In the file", L"Length" };
    LVCOLUMNW col;
    int i;

    memset(&g_hires, 0, sizeof(g_hires));
    RsView_Register(GetModuleHandleW(NULL));

    g_hires.mark = Rs_PreviewMark(page, HIRES_ID_MARK);
    SetWindowLongPtrW(g_hires.mark, GWL_STYLE, GetWindowLongPtrW(g_hires.mark, GWL_STYLE) | SS_RIGHT);

    g_hires.modelLabel = Rs_Label(page, HIRES_ID_MODEL_LABEL, L"Hi-res model", RS_FONT_BOLD);
    g_hires.model = Rs_Edit(page, HIRES_ID_MODEL, L"", ES_AUTOHSCROLL);
    SendMessageW(g_hires.model, EM_SETCUEBANNER, FALSE, (LPARAM)L"A glTF (.glb) made for the hi-res driver");
    g_hires.modelBrowse = Rs_Button(page, HIRES_ID_MODEL_BROWSE, L"Browse...");
    g_hires.charLabel = Rs_Label(page, HIRES_ID_CHAR_LABEL, L"Character", RS_FONT_BOLD);
    g_hires.character = Rs_Edit(page, HIRES_ID_CHAR, L"", ES_AUTOHSCROLL);
    SendMessageW(g_hires.character, EM_SETCUEBANNER, FALSE, (LPARAM)L"The .rldchar that gets the hi-res model");
    g_hires.charBrowse = Rs_Button(page, HIRES_ID_CHAR_BROWSE, L"Browse...");
    g_hires.outLabel = Rs_Label(page, HIRES_ID_OUT_LABEL, L"Output", RS_FONT_BOLD);
    g_hires.out = Rs_Edit(page, HIRES_ID_OUT, L"", ES_AUTOHSCROLL);
    SendMessageW(g_hires.out, EM_SETCUEBANNER, FALSE, (LPARAM)L"The new .rldchar (the character itself stays as it is)");
    g_hires.outBrowse = Rs_Button(page, HIRES_ID_OUT_BROWSE, L"Browse...");
    g_hires.checkBtn = Rs_Button(page, HIRES_ID_CHECK, L"Check");
    g_hires.pack = Rs_PrimaryButton(page, HIRES_ID_PACK, L"Pack into .rldchar");
    g_hires.upscale = Rs_Check(page, HIRES_ID_UPSCALE, L"Upscale small textures (pixel art)");
    g_hires.status = Rs_Label(page, HIRES_ID_STATUS, L"", RS_FONT_SMALL);
    SetWindowLongPtrW(g_hires.status, GWL_STYLE, GetWindowLongPtrW(g_hires.status, GWL_STYLE) | SS_ENDELLIPSIS);

    g_hires.verdict = Rs_Label(page, HIRES_ID_VERDICT, L"", RS_FONT_BOLD);
    g_hires.summary = Rs_Label(page, HIRES_ID_SUMMARY, L"", RS_FONT_SMALL);
    g_hires.msgs = Rs_MsgList(page, HIRES_ID_MSGS);
    g_hires.clips = Rs_ListView(page, HIRES_ID_CLIPS, LVS_NOSORTHEADER);
    for (i = 0; i < 3; i++) {
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
        col.fmt = LVCFMT_LEFT;
        col.pszText = (LPWSTR)heads[i];
        col.cx = Rs_Px(90);
        col.iSubItem = i;
        ListView_InsertColumn(g_hires.clips, i, &col);
    }
    for (i = 0; i < RS_HIRES_CLIPS; i++) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (LPWSTR)RsHires_ClipName(i);
        ListView_InsertItem(g_hires.clips, &it);
    }

    g_hires.view = CreateWindowExW(0, RS_VIEW_CLASS, L"No model yet", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, page,
                                   (HMENU)(INT_PTR)HIRES_ID_VIEW, GetModuleHandleW(NULL), NULL);
    g_hires.page = page;
    g_hires.clipLabel = Rs_Label(page, HIRES_ID_CLIP_LABEL, L"Clip", RS_FONT_BOLD);
    g_hires.clipCombo = Rs_Combo(page, HIRES_ID_CLIP);
    g_hires.playBtn = Rs_Button(page, HIRES_ID_PLAY, L"Play");
    g_hires.timeLabel = Rs_Label(page, HIRES_ID_TIME, L"", RS_FONT_SMALL);
    SetWindowLongPtrW(g_hires.timeLabel, GWL_STYLE, GetWindowLongPtrW(g_hires.timeLabel, GWL_STYLE) | SS_ENDELLIPSIS);
    g_hires.note = Rs_Label(page, HIRES_ID_NOTE,
                            L"Bind pose, or a clip posed with the engine's own animation code (include/rldhires_anim.inc). "
                            L"Wheels turn slowly while a clip plays.",
                            RS_FONT_SMALL);
    Rs_SetTextColor(g_hires.note, RS_COL_MUTED);
    Rs_SetTextColor(g_hires.status, RS_COL_MUTED);

    Hires_ShowAll();
}

static void Hires_Browse(HWND page, int which)
{
    wchar_t out[HIRES_PATH], initial[HIRES_PATH];
    int ok = 0;
    if (which == 0) {
        Hires_Field(g_hires.model, initial, HIRES_PATH);
        ok = Rs_BrowseOpenFile(page, L"Choose a hi-res model",
                               L"Hi-res models (*.glb, *.rldchar)\0*.glb;*.rldchar\0All files\0*.*\0\0",
                               initial, out, HIRES_PATH);
        if (ok) {
            Hires_SetField(g_hires.model, out);
            Hires_RunCheck();
        }
    } else if (which == 1) {
        Hires_Field(g_hires.character, initial, HIRES_PATH);
        ok = Rs_BrowseOpenFile(page, L"Choose the character",
                               L"Characters (*.rldchar)\0*.rldchar\0All files\0*.*\0\0",
                               initial, out, HIRES_PATH);
        if (ok) {
            Hires_SetField(g_hires.character, out);
            Hires_FillOutput();
            Hires_UpdateButtons();
        }
    } else {
        Hires_Field(g_hires.out, initial, HIRES_PATH);
        ok = Rs_BrowseSaveFile(page, L"Save the character with the hi-res model",
                               L"Characters (*.rldchar)\0*.rldchar\0\0", L"rldchar", initial, out, HIRES_PATH);
        if (ok) {
            Hires_SetField(g_hires.out, out);
            Hires_UpdateButtons();
        }
    }
}

static LRESULT Hires_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);
    (void)lParam;

    switch (id) {
    case HIRES_ID_MODEL:
    case HIRES_ID_CHAR:
    case HIRES_ID_OUT:
        if (code == EN_CHANGE && !g_hires.quiet)
            Hires_UpdateButtons();
        break;
    case HIRES_ID_MODEL_BROWSE:
        if (code == BN_CLICKED)
            Hires_Browse(page, 0);
        break;
    case HIRES_ID_CHAR_BROWSE:
        if (code == BN_CLICKED)
            Hires_Browse(page, 1);
        break;
    case HIRES_ID_OUT_BROWSE:
        if (code == BN_CLICKED)
            Hires_Browse(page, 2);
        break;
    case HIRES_ID_CLIP:
        if (code == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_hires.clipCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0)
                Hires_SelectClip(sel);
        }
        break;
    case HIRES_ID_PLAY:
        if (code == BN_CLICKED)
            Hires_SetPlaying(!g_hires.playing);
        break;
    case HIRES_ID_CHECK:
        if (code == BN_CLICKED)
            Hires_RunCheck();
        break;
    case HIRES_ID_PACK:
        if (code == BN_CLICKED)
            Hires_StartPack(page);
        break;
    }
    return 0;
}

static LRESULT Hires_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    switch (msg) {
    case WM_TIMER:
        if (wParam == HIRES_TIMER) {
            Hires_Tick();
            *handled = 1;
        }
        return 0;
    case RS_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        *handled = 1;
        if (!line)
            return 0;
        if ((int)wParam == g_hires.job && g_hires.job != 0)
            Hires_PackLine(line);
        Rs_Free(line);
        return 0;
    }
    case RS_WM_JOB_DONE:
        *handled = 1;
        if ((int)wParam == g_hires.job && g_hires.job != 0)
            Hires_PackDone(page, (int)lParam);
        return 0;
    }
    return 0;
}

static int Hires_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    wchar_t path[HIRES_PATH];
    int i;

    if (wcscmp(verb, L"hires-model") == 0 || wcscmp(verb, L"hires-char") == 0 || wcscmp(verb, L"hires-out") == 0) {
        HWND field = wcscmp(verb, L"hires-model") == 0 ? g_hires.model
                     : wcscmp(verb, L"hires-char") == 0 ? g_hires.character : g_hires.out;
        Hires_Copy(path, HIRES_PATH, arg);
        for (i = 0; path[i] == L' ' || path[i] == L'"'; i++)
            ;
        if (i)
            memmove(path, path + i, (wcslen(path + i) + 1) * sizeof(wchar_t));
        for (i = (int)wcslen(path); i > 0 && (path[i - 1] == L' ' || path[i - 1] == L'"'); i--)
            path[i - 1] = 0;
        if (_wcsicmp(path, L"none") == 0)
            path[0] = 0;
        Hires_SetField(field, path);
        if (field == g_hires.model) {
            int read = Hires_RunCheck();
            Rs_AutoLog(L"  %ls: %ls - %ls", verb, path[0] ? path : L"none",
                       !path[0] ? L"cleared" : !read ? g_hires.check.why
                       : g_hires.check.usable ? L"usable" : L"refused");
            return (path[0] && !read) ? RS_AUTO_FAIL : RS_AUTO_DONE;
        }
        if (field == g_hires.character)
            Hires_FillOutput();
        Hires_UpdateButtons();
        Rs_AutoLog(L"  %ls: %ls", verb, path[0] ? path : L"none");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"hires-check") == 0) {
        int read = Hires_RunCheck();
        Rs_AutoLog(L"  hires-check: %ls", !g_hires.checked ? L"no model"
                   : !read ? g_hires.check.why : g_hires.check.usable ? L"usable" : L"refused");
        return (g_hires.checked && !read) ? RS_AUTO_FAIL : RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"hires-pack") == 0) {
        if (!Hires_CanPack()) {
            Rs_AutoLog(L"  hires-pack: the button is disabled (%ls)",
                       Hires_Running() ? L"a pack is running"
                       : !g_hires.checked ? L"no check yet"
                       : !g_hires.check.usable ? L"the model is not usable"
                       : g_hires.check.fromCharacter ? L"the model is the CHRG of a character - choose its .glb"
                       : L"model, character or output is empty, or the model changed since the check");
            return RS_AUTO_FAIL;
        }
        if (!Hires_StartPack(page)) {
            Rs_AutoLog(L"  hires-pack: rldpack could not be started");
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  hires-pack: started");
        return RS_AUTO_WAIT;
    }
    if (wcscmp(verb, L"hires-view") == 0) {
        for (i = 0; i < RS_VIEW_PRESET_COUNT && _wcsicmp(arg, g_hiresPresetWords[i]) != 0; i++)
            ;
        if (i >= RS_VIEW_PRESET_COUNT) {
            Rs_AutoLog(L"  hires-view: say front, side, back, top, 34 or race");
            return RS_AUTO_FAIL;
        }
        RsView_SetPreset(g_hires.view, i);
        Rs_AutoLog(L"  hires-view: %ls", g_hiresPresetWords[i]);
        return RS_AUTO_DONE;
    }
    // "hires-camera <yaw> <pitch> <zoom %>": the view's camera, as dragging and
    // the wheel would (screenshots of a pose from near).
    if (wcscmp(verb, L"hires-camera") == 0) {
        int yaw = 0, pitch = 0, zoom = 0, panX = 0, panY = 0;
        const int n = swscanf(arg, L"%d %d %d %d %d", &yaw, &pitch, &zoom, &panX, &panY);
        if (n != 3 && n != 5) {
            Rs_AutoLog(L"  hires-camera: say <yaw> <pitch> <zoom %%> [<pan x> <pan y>]");
            return RS_AUTO_FAIL;
        }
        RsView_SetCamera(g_hires.view, yaw, pitch, zoom, panX, panY);
        Rs_AutoLog(L"  hires-camera: %d %d %d %d %d", yaw, pitch, zoom, panX, panY);
        return RS_AUTO_DONE;
    }
    // "hires-filter engine|nearest": how the preview samples the textures - the
    // engine's hi-res filter (the default) or point sampling, for comparison
    // images; the check of the model field runs again.
    if (wcscmp(verb, L"hires-filter") == 0) {
        if (_wcsicmp(arg, L"engine") != 0 && _wcsicmp(arg, L"nearest") != 0) {
            Rs_AutoLog(L"  hires-filter: say engine or nearest");
            return RS_AUTO_FAIL;
        }
        RsHires_SetPointSampling(_wcsicmp(arg, L"nearest") == 0);
        Hires_RunCheck();
        Rs_AutoLog(L"  hires-filter: %ls", arg);
        return RS_AUTO_DONE;
    }
    // "hires-upscale on|off": the option "Upscale small textures".
    if (wcscmp(verb, L"hires-upscale") == 0) {
        if (_wcsicmp(arg, L"on") != 0 && _wcsicmp(arg, L"off") != 0) {
            Rs_AutoLog(L"  hires-upscale: say on or off");
            return RS_AUTO_FAIL;
        }
        SendMessageW(g_hires.upscale, BM_SETCHECK, _wcsicmp(arg, L"on") == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        Rs_AutoLog(L"  hires-upscale: %ls", Hires_UpscaleOn() ? L"on" : L"off");
        return RS_AUTO_DONE;
    }
    // "hires-crash on|off": the reference dummy "Crash size" beside the model
    // (the View menu of the preview does the same).
    if (wcscmp(verb, L"hires-crash") == 0) {
        if (_wcsicmp(arg, L"on") != 0 && _wcsicmp(arg, L"off") != 0) {
            Rs_AutoLog(L"  hires-crash: say on or off");
            return RS_AUTO_FAIL;
        }
        RsView_SetCrashRef(g_hires.view, _wcsicmp(arg, L"on") == 0);
        Rs_AutoLog(L"  hires-crash: %ls", arg);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"hires-clip") == 0) {
        int sel = -1;
        if (!Hires_AnimOk()) {
            Rs_AutoLog(L"  hires-clip: clips cannot be played (%ls)",
                       g_hires.check.animWhy[0] ? g_hires.check.animWhy : L"no usable model checked");
            return RS_AUTO_FAIL;
        }
        if (_wcsicmp(arg, L"bind") == 0) {
            sel = 0;
        } else {
            for (i = 0; i < RS_HIRES_CLIPS; i++)
                if (_wcsicmp(arg, RsHires_ClipName(i)) == 0)
                    sel = i + 1;
        }
        if (sel < 0) {
            Rs_AutoLog(L"  hires-clip: unknown clip '%ls' (bind, idle .. menu)", arg);
            return RS_AUTO_FAIL;
        }
        Hires_SelectClip(sel);
        Rs_AutoLog(L"  hires-clip: %ls", sel > 0 ? RsHires_ClipName(sel - 1) : L"bind");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"hires-play") == 0) {
        int on;
        if (_wcsicmp(arg, L"on") == 0)
            on = 1;
        else if (_wcsicmp(arg, L"off") == 0)
            on = 0;
        else {
            Rs_AutoLog(L"  hires-play: say on or off");
            return RS_AUTO_FAIL;
        }
        if (on && !(Hires_AnimOk() && g_hires.clipSel > 0)) {
            Rs_AutoLog(L"  hires-play: nothing to play (choose a clip first)");
            return RS_AUTO_FAIL;
        }
        Hires_SetPlaying(on);
        Rs_AutoLog(L"  hires-play: %ls", on ? L"on" : L"off");
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"hires-time") == 0) {
        wchar_t *end = NULL;
        double secs = wcstod(arg, &end);
        double deg = 0.0;
        if (!Hires_AnimOk() || end == arg || secs < 0.0) {
            Rs_AutoLog(L"  hires-time: say <seconds> [<wheel degrees>] with a clip-ready model");
            return RS_AUTO_FAIL;
        }
        deg = wcstod(end, &end);
        Hires_SetPlaying(0);
        g_hires.seconds = (float)secs;
        g_hires.wheelDeg = (float)deg;
        Hires_ShowView();
        Hires_UpdateTime();
        Rs_AutoLog(L"  hires-time: %ls %.3f -> %d triangles", g_hires.clipSel > 0 ? RsHires_ClipName(g_hires.clipSel - 1) : L"bind",
                   secs, g_hires.shownTris);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"hires-report") == 0) {
        Hires_Copy(path, HIRES_PATH, arg);
        for (i = (int)wcslen(path); i > 0 && (path[i - 1] == L' ' || path[i - 1] == L'"'); i--)
            path[i - 1] = 0;
        if (!path[0] || !Hires_Report(path)) {
            Rs_AutoLog(L"  could not write the report '%ls'", path);
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  report written: %ls", path);
        return RS_AUTO_DONE;
    }
    return RS_AUTO_UNKNOWN;
}

static int Hires_Busy(HWND page)
{
    (void)page;
    return Hires_Running();
}

const struct RsPageDef g_rsHiresPage = {
    L"Hi-Res",
    L"Hi-Res driver (Preview)",
    L"Check a glTF model (.glb) for a hi-res driver and pack it into a character. "
    L"Preview: the game draws it only with --engine-hires-chars.",
    Hires_Create,
    Hires_Layout,
    Hires_Command,
    NULL,
    Hires_Message,
    Hires_Automate,
    Hires_Busy,
    900,   // one column below 1100 px; 900 keeps a 1000 px window free of a horizontal scroll bar
    0
};
