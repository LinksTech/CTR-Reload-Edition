// rs_char.c - page "Character": build a PLY model as .rldchar
//
// The page checks nothing itself. It starts rldpack make-char (with --check to
// check, without it to build), reads its machine lines (protocol in
// reloadstudio.h) and shows them. The Studio asks for no template: it always
// passes --template 14 (Fake Crash). The driving style is the --class switch.
//
// Flow: every change to a field starts a new check after 600 ms; choosing a
// model checks at once. A new check makes the running one outdated: it is
// ended, its lines are only freed. A check also asks rldpack for the converted
// model (--preview, shown by the RsModelView control of rs_view.c) and, with an
// icon, for the decoded and the converted picture (--icon-preview). These go to
// files in the folder of Rs_TempDir (%TEMP%\Reload Studio, with --settings the
// folder of the settings file) that are deleted once they are shown.
//
// Size: rldpack fits every model to the size of Crash with his kart (--fit
// crash, its default) and reports the factor (@char fit); 100 % on the slider
// is that size. It reports the sizes this model allows (@value size-range);
// the slider stays inside them. It is a visual size only - physics and
// collision follow the driving style. The preview draws the model on the
// reference dummy it was fitted onto (rs_view.c) and outlines Crash's size
// (@value crash-box) about it.
//
// Options, each a make-char switch; only the switches that differ from
// rldpack's default are passed:
//   "Repair the model" (default on, --repair auto; off = --repair off): split
//      corners welded, faces turned outward, small holes closed; @char
//      repaired says what changed, shown below the options when anything did
//   "Draw open parts from both sides" (default on, --open-parts two-sided;
//      off = --open-parts one-sided): parts still open after the repair are
//      drawn from both sides at no extra triangles; @char two-sided counts them
//   "Closed hull (remesh)" (default OFF, only on request: --remesh on): every
//      part becomes a closed hull that the reduction then brings back under
//      the limit; @char remeshed gives the counts. The preview shows the
//      result like every other build, so the author sees what it does. It
//      needs "Reduce automatically": greyed out (and not passed) while that
//      is off, with a line saying why
//   "Reduce automatically" (default on, --reduce auto: a model over the
//      game's draw memory loses triangles until it fits; @char reduced says
//      how many)
//   "Show kart wheels" (default on, --wheels; off = the game draws no kart
//      wheels for this driver, for models with wheels of their own; the
//      preview's dummy follows at once)
//
// Split: on the left the character and the build, on the right icon and voices
// and the 3D preview.

#include "reloadstudio.h"
#include "rs_view.h"
#include <shellapi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHAR_TIMER_CHECK   1
#define CHAR_CHECK_DELAY   600
#define CHAR_VAL           1024   // length of a value or path
#define CHAR_MAX_ARGS      40
#define CHAR_NAME_MAX      17     // RLDCHAR_NAME_MAX in include/rldchar.inc
#define CHAR_SIZE_MIN      50     // range of the --size switch
#define CHAR_SIZE_MAX      200
#define CHAR_SIZE_DEFAULT  100
#define CHAR_TEMPLATE      L"14"  // Fake Crash: the Studio asks for no template
#define CHAR_PENDING       16     // outdated checks whose temp files wait for their end
#define CHAR_POSES         3      // poses in a --preview file
#define CHAR_IMAGE_CLASS   L"RsCharImage"
#define CHAR_IMAGE_MAX     16384  // largest edge of a picture the page shows
#define CHAR_INFO_LINES    3      // lines of the model info at most
#define CHAR_QUALITY_LINES 6      // lines of the repair / remesh note at most
#define CHAR_REPAIR_FIELDS 10     // numbers of @char repaired
#define CHAR_REMESH_FIELDS 5      // numbers of @char remeshed

// Controls
#define CHAR_ID_MODEL_LABEL    100
#define CHAR_ID_MODEL          101
#define CHAR_ID_MODEL_BROWSE   102
#define CHAR_ID_MODEL_INFO     103
#define CHAR_ID_NAME_LABEL     104
#define CHAR_ID_NAME           105
#define CHAR_ID_NAME_NOTE      106
#define CHAR_ID_CLASS_LABEL    107
#define CHAR_ID_CLASS          108
#define CHAR_ID_CLASS_HELP     109
#define CHAR_ID_SIZE_LABEL     110
#define CHAR_ID_SIZE           111
#define CHAR_ID_SIZE_VALUE     112
#define CHAR_ID_SIZE_NOTE      113
#define CHAR_ID_SIZE_HINT      114
#define CHAR_ID_REDUCE         115
#define CHAR_ID_WHEELS         116
#define CHAR_ID_SIZE_FIT       117
#define CHAR_ID_SIZE_CRASH     118
#define CHAR_ID_OPTIONS_LABEL  119
#define CHAR_ID_ICON_LABEL     120
#define CHAR_ID_ICON           121
#define CHAR_ID_ICON_BROWSE    122
#define CHAR_ID_ICON_CLEAR     123
#define CHAR_ID_ICON_CAPTION   124   // 124..125
#define CHAR_ID_ICON_IMAGE     126   // 126..127
#define CHAR_ID_VOICES_LABEL   130
#define CHAR_ID_VOICES         131
#define CHAR_ID_VOICES_BROWSE  132
#define CHAR_ID_VOICES_CLEAR   133
#define CHAR_ID_VOICES_NOTE    134
#define CHAR_ID_VIEW           140
#define CHAR_ID_POSE           141
#define CHAR_ID_VIEW_NOTE      142
#define CHAR_ID_OUT_LABEL      150
#define CHAR_ID_OUT            151
#define CHAR_ID_OUT_BROWSE     152
#define CHAR_ID_CHECK          153
#define CHAR_ID_BUILD          154
#define CHAR_ID_HEADLINE       155
#define CHAR_ID_MESSAGES       156
#define CHAR_ID_RAW            157
#define CHAR_ID_RAW_TOGGLE     158
#define CHAR_ID_SHOW           159
#define CHAR_ID_REPAIR         160
#define CHAR_ID_OPEN_PARTS     161
#define CHAR_ID_REMESH         162
#define CHAR_ID_QUALITY        163

enum { CHAR_JOB_NONE = 0, CHAR_JOB_CHECK, CHAR_JOB_BUILD };
enum { CHAR_IMG_ORIGINAL = 0, CHAR_IMG_ICON, CHAR_IMG_COUNT };

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

struct CharClass {
    const wchar_t *word;    // this is how the value goes to rldpack (--class)
    const wchar_t *text;
};

// The four engine classes with their retail drivers (enum EngineClass,
// include/namespace_Vehicle.h; engineID in data.MetaDataCharacters).
static const struct CharClass g_charClasses[] = {
    { L"balanced",     L"Balanced - like Crash, Cortex, Komodo Joe, Fake Crash" },
    { L"acceleration", L"Acceleration - like Coco, N. Gin, Pinstripe" },
    { L"speed",        L"Speed - like Tiny, Dingodile, Papu Papu, N. Tropy" },
    { L"turning",      L"Turning - like Polar, Pura, Ripper Roo, Penta Penguin" },
};
#define CHAR_CLASS_COUNT ((int)(sizeof(g_charClasses) / sizeof(g_charClasses[0])))

// Poses of the preview, in the order of the --preview file: anim 0 frame 10
// (neutral), frame 0 (full steer left) and frame 20 (full steer right) - the
// game maps a left steer to frame 0 (game/Vehicle/VehFrame.c, VehPhysProc.c).
static const wchar_t *const g_charPoseWords[CHAR_POSES] = { L"neutral", L"left", L"right" };
static const wchar_t *const g_charPoseTexts[CHAR_POSES] = { L"Neutral", L"Steering left", L"Steering right" };
static const int g_charPoseView[CHAR_POSES] = { RS_VIEW_POSE_NEUTRAL, RS_VIEW_POSE_FRAME0, RS_VIEW_POSE_FRAME20 };

#define CHAR_SIZE_HINT_TEXT L"Visual size only - physics and collision follow the driving style."
#define CHAR_NAME_RULE_TEXT L"1 to 17 characters: A-Z 0-9 space ! % ' + , - . / : < = > ? _"
#define CHAR_VOICES_TEXT L"Voices are checked but not packed yet - the driver is silent in the game."
#define CHAR_FIT_WAIT_TEXT L"The model is fitted to Crash size when it is checked."
#define CHAR_REMESH_NEEDS_TEXT L"Closed hull needs Reduce automatically: the hulls have far more triangles than a driver may draw."

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct CharMsg {
    int severity;
    wchar_t *code;
    wchar_t *text;
    wchar_t *detail;
};

// A picture as 32-bit BGRA, top row first.
struct CharImage {
    int w, h;
    unsigned char *px;
};

// What the running (or last) rldpack run reported.
struct CharJobData {
    int protocolSeen, protocol;
    int plySeen;
    wchar_t plyState[16];
    long long plyBytes;
    int outSeen;
    wchar_t out[CHAR_VAL];
    int rangeSeen, rangeLo, rangeHi;
    int rangeNone;                  // "size-range 0 0": no size fits this model
    int kartSeen;
    int kart[6];                    // @value kart-box: the model's own kart
    int retailSeen;
    int retail[6];                  // @value retail-kart: the retail kart, the size reference
    int crashSeen;
    int crash[6];                   // @value crash-box: Crash with his kart, in tenths of game units
    wchar_t reduce[48];             // @value reduce: "<auto|off> <origin>"
    wchar_t fit[48];                // @value fit: "<crash|none> <origin>"
    wchar_t wheels[48];             // @value wheels: "<on|off> <origin>"
    wchar_t repair[48];             // @value repair: "<auto|off> <origin>"
    wchar_t openParts[48];          // @value open-parts: "<two-sided|one-sided> <origin>"
    wchar_t remesh[48];             // @value remesh: "<on|off> <origin>"
    int repairedSeen;
    long repaired[CHAR_REPAIR_FIELDS];  // @char repaired: welded, degenerate, duplicate, flipped, holes
                                        // closed, hole triangles, holes left, open edges before, after,
                                        // cracks split
    int twoSidedSeen;
    long twoSided;                  // @char two-sided: triangles of open parts drawn from both sides
    int remeshedSeen;
    long remeshed[CHAR_REMESH_FIELDS];  // @char remeshed: triangles of the source, of the hulls, after;
                                        // open edges before, after
    int reducedSeen;
    long reduced[4];                // @char reduced: triangles before, after; draw bytes before, after
    int fitSeen;
    wchar_t fitFactor[16];          // @char fit: factor, length before/after, height before/after
    wchar_t fitLength[2][16];
    wchar_t fitHeight[2][16];
    wchar_t fitBasis[16];           // kart | model: what was matched to Crash's length
    wchar_t triangles[32];          // @char triangles (or faces)
    wchar_t parts[160];             // @char parts: "kart 1, driver 2, steering wheel 3"
    wchar_t voices[32];             // @char voices: "<n> of 18"
    struct CharMsg *msgs;
    int msgCount, msgCap;
    int resultSeen;
    wchar_t resultState[16];
    wchar_t resultPath[CHAR_VAL];
    long long resultBytes;
    wchar_t resultSha[80];
    int endSeen, endCode;
};

static struct {
    HWND modelLabel, model, modelBrowse, modelInfo;
    HWND nameLabel, name, nameNote;
    HWND classLabel, cls, classHelp;
    HWND sizeLabel, size, sizeValue, sizeNote, sizeHint, sizeFit, sizeCrash;
    HWND optionsLabel, repair, openParts, remesh, reduce, wheels, quality;
    HWND iconLabel, icon, iconBrowse, iconClear, iconCaption[CHAR_IMG_COUNT], iconImage[CHAR_IMG_COUNT];
    HWND voicesLabel, voices, voicesBrowse, voicesClear, voicesNote;
    HWND view, pose, viewNote;
    HWND outLabel, out, outBrowse, check, build, headline, msgs, raw, rawToggle, show;

    // colours of the labels, for the report
    COLORREF headColor, nameColor, sizeColor, infoColor, viewColor, fitColor, qualityColor;
    wchar_t infoFull[CHAR_VAL];     // the model info before Char_FitLines

    int applying;                   // fields are being set: trigger no check
    int timer;                      // check waits for the timer
    int jobId, jobKind;             // running job, 0 = none
    int jobSeq;                     // temp files of the running check
    int seq;                        // last handed out temp number
    struct { int id, seq; } pending[CHAR_PENDING];   // ended checks, files still to delete
    int sizeNow;                    // slider position, CHAR_SIZE_MIN..CHAR_SIZE_MAX
    int rangeKnown, rangeLo, rangeHi;
    int rangeNone;                  // rldpack: no size fits this model
    wchar_t sizeWhy[2][512];        // reason of char-size: [0] below the range, [1] above it
    int sizeLockHigh;               // the slider went back to the upper bound
    int sizeLocked;                 // the slider was put back into the range
    int kartKnown;
    int kart[6];
    int retailKnown;
    int retail[6];
    int crashKnown;
    int crash[6];                   // tenths of game units
    int poseNow;
    int yawNow;
    int previewShown;               // the view shows a model
    int previewPoses;
    unsigned long previewTris[CHAR_POSES];
    struct CharImage image[CHAR_IMG_COUNT];
    int checked;                    // last check gave "checked"
    int checkAfterBuild;            // a check was asked for while building
    wchar_t checkedPath[CHAR_VAL];
    wchar_t built[CHAR_VAL];        // last built character
    long long builtBytes;
    wchar_t builtSha[80];
    int showRaw;
    wchar_t *rawText;               // all lines of the last run
    size_t rawLen, rawCap;
    int rawLines;
} g_char;

static struct CharJobData g_charJob;

static void Char_Layout(HWND page, int w, int h);
static int Char_Check(HWND page);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void Char_Copy(wchar_t *dst, int cap, const wchar_t *src)
{
    size_t n;
    if (cap <= 0)
        return;
    if (!src)
        src = L"";
    n = wcslen(src);
    if (n >= (size_t)cap)
        n = (size_t)cap - 1;
    memmove(dst, src, n * sizeof(wchar_t));
    dst[n] = 0;
}

static void Char_Append(wchar_t *dst, int cap, const wchar_t *src)
{
    int n = (int)wcslen(dst);
    if (n < cap - 1)
        Char_Copy(dst + n, cap - n, src);
}

static const wchar_t *Char_Field(wchar_t **fields, int count, int i)
{
    return (i < count && fields[i]) ? fields[i] : L"";
}

static void Char_Ellipsis(HWND label)
{
    LONG_PTR style = GetWindowLongPtrW(label, GWL_STYLE);
    SetWindowLongPtrW(label, GWL_STYLE, style | SS_ENDELLIPSIS);
}

static int Char_IsShown(HWND h)
{
    return (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0;
}

// Kilobytes of 1000 bytes, rounded; small files in bytes.
static void Char_SizeText(wchar_t *out, int cap, long long bytes)
{
    if (bytes < 1000)
        swprintf(out, cap, L"%lld bytes", bytes);
    else
        swprintf(out, cap, L"%lld KB", (bytes + 500) / 1000);
}

static const wchar_t *Char_ColorName(COLORREF c)
{
    if (c == RS_COL_OK) return L"green";
    if (c == RS_COL_ERROR) return L"red";
    if (c == RS_COL_WARNING) return L"amber";
    if (c == RS_COL_NOTE) return L"blue";
    if (c == RS_COL_MUTED) return L"grey";
    return L"normal";
}

static void Char_SetLabel(HWND label, const wchar_t *text, COLORREF color, COLORREF *store)
{
    Rs_SetText(label, text);
    Rs_SetTextColor(label, color);
    if (store)
        *store = color;
}

static void Char_Headline(const wchar_t *text, COLORREF color)
{
    Char_SetLabel(g_char.headline, text, color, &g_char.headColor);
}

// Text the message list shows as long as it is empty.
static void Char_EmptyText(const wchar_t *text)
{
    Rs_SetText(g_char.msgs, text);
}

// Spaces and quotation marks away, no slash at the end.
static void Char_CleanPath(wchar_t *out, int cap, const wchar_t *in)
{
    size_t n;
    if (!in)
        in = L"";
    while (*in == L' ' || *in == L'\t' || *in == L'"')
        in++;
    Char_Copy(out, cap, in);
    n = wcslen(out);
    while (n > 0 && (out[n - 1] == L' ' || out[n - 1] == L'\t' || out[n - 1] == L'"'))
        out[--n] = 0;
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
}

// Text of an edit field without quotation marks and spaces at the edges.
static void Char_FieldPath(HWND edit, wchar_t *out, int cap)
{
    wchar_t *text = Rs_GetText(edit);
    Char_CleanPath(out, cap, text);
    Rs_Free(text);
}

static int Char_EndsWith(const wchar_t *path, const wchar_t *ending)
{
    size_t n = wcslen(path), e = wcslen(ending);
    return n >= e && _wcsicmp(path + n - e, ending) == 0;
}

// Up to max numbers out of the text (separated by anything else), times
// scale, rounded to whole numbers and held to -32768..32767 times scale.
static int Char_NumbersScaled(const wchar_t *s, int *out, int max, int scale)
{
    int n = 0;
    while (*s && n < max) {
        wchar_t *end;
        double v;
        if (!((*s >= L'0' && *s <= L'9') || ((*s == L'-' || *s == L'+') && s[1] >= L'0' && s[1] <= L'9'))) {
            s++;
            continue;
        }
        v = wcstod(s, &end);
        if (end == s) {
            s++;
            continue;
        }
        v *= scale;
        if (v > 32767.0 * scale)
            v = 32767.0 * scale;
        if (v < -32768.0 * scale)
            v = -32768.0 * scale;
        out[n++] = (int)(v < 0 ? v - 0.5 : v + 0.5);
        s = end;
    }
    return n;
}

// Up to max whole numbers out of the text, rounded and held to -32768..32767.
static int Char_Numbers(const wchar_t *s, int *out, int max)
{
    return Char_NumbersScaled(s, out, max, 1);
}

// A whole number with a space between groups of three digits: "27 636".
static void Char_Grouped(wchar_t *out, int cap, long v)
{
    wchar_t digits[24];
    int n, i, o = 0;
    swprintf(digits, 24, L"%ld", v < 0 ? -v : v);
    n = (int)wcslen(digits);
    if (v < 0 && o < cap - 1)
        out[o++] = L'-';
    for (i = 0; i < n && o < cap - 1; i++) {
        if (i > 0 && (n - i) % 3 == 0 && o < cap - 1)
            out[o++] = L' ';
        if (o < cap - 1)
            out[o++] = digits[i];
    }
    out[o] = 0;
}

static int Char_IsChecked(HWND box)
{
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void Char_SetChecked(HWND box, int on)
{
    SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

// Height the text of a label takes wrapped at width, at most maxLines lines
// (the rest is cut off at a line boundary).
static int Char_TextHeight(HWND label, int width, int maxLines)
{
    wchar_t *text = Rs_GetText(label);
    HDC dc = GetDC(label);
    HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    TEXTMETRICW tm;
    RECT rc;
    int h = Rs_Px(18), lineH;

    if (dc) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_SMALL));
        GetTextMetricsW(dc, &tm);
        lineH = tm.tmHeight;
        rc.left = 0;
        rc.top = 0;
        rc.right = width;
        rc.bottom = 0;
        DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        h = rc.bottom;
        if (lineH > 0 && h > lineH * maxLines)
            h = lineH * maxLines;
        SelectObject(dc, old);
        ReleaseDC(label, dc);
    }
    Rs_Free(text);
    return h;
}

// Puts full into label so that it takes at most maxLines lines at width: as
// it is when it fits, else cut after a word and ended with "..." - what does
// not fit is cut off visibly, never silently.
static void Char_FitLines(HWND label, const wchar_t *full, int width, int maxLines)
{
    HDC dc = GetDC(label);
    HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    TEXTMETRICW tm;
    wchar_t buf[CHAR_VAL + 4];
    size_t n = wcslen(full);

    Char_Copy(buf, CHAR_VAL, full);
    if (dc && width > 0) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_SMALL));
        GetTextMetricsW(dc, &tm);
        for (;;) {
            RECT rc = { 0, 0, width, 0 };
            DrawTextW(dc, buf, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
            if (rc.bottom <= tm.tmHeight * maxLines)
                break;
            // One word less (and the spaces and dashes before it).
            while (n > 0 && full[n - 1] != L' ')
                n--;
            while (n > 0 && (full[n - 1] == L' ' || full[n - 1] == L'-' || full[n - 1] == L','))
                n--;
            if (n == 0)
                break;
            memcpy(buf, full, n * sizeof(wchar_t));
            wcscpy(buf + n, L"...");
        }
        SelectObject(dc, old);
    }
    if (dc)
        ReleaseDC(label, dc);
    Rs_SetText(label, buf);
}

// Width a check box needs for its text in its font: box, gap, text, a margin.
static int Char_CheckWidth(HWND box)
{
    wchar_t text[128];
    HDC dc = GetDC(box);
    HFONT font = (HFONT)SendMessageW(box, WM_GETFONT, 0, 0);
    HGDIOBJ old;
    SIZE ext;
    int n = GetWindowTextW(box, text, 128);

    ext.cx = n * Rs_Px(8);
    if (dc) {
        old = SelectObject(dc, font ? font : Rs_Font(RS_FONT_BODY));
        GetTextExtentPoint32W(dc, text, n, &ext);
        SelectObject(dc, old);
        ReleaseDC(box, dc);
    }
    return Rs_Px(13 + 4 + 6) + ext.cx;
}

// The menu font of the game: A-Z, 0-9, space and ! % ' + , - . / : < = > ? _
// (RldChar_NameCharAllowed in include/rldchar.inc). Only for typing - rldpack
// checks the name.
static int Char_NameCharAllowed(wchar_t c)
{
    if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
        return 1;
    return c != 0 && wcschr(L" !%'+,-./:<=>?_", c) != NULL;
}

// ---------------------------------------------------------------------------
// Pictures: icon PNG as decoded and as converted (BMP files of --icon-preview)
// ---------------------------------------------------------------------------

static void Char_ImageFree(struct CharImage *img)
{
    Rs_Free(img->px);
    img->px = NULL;
    img->w = 0;
    img->h = 0;
}

static unsigned Char_Le16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long Char_Le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

// A channel through a bit mask, scaled to 0..255. mask 0 = def.
static unsigned Char_MaskChannel(unsigned long v, unsigned long mask, unsigned def)
{
    int shift = 0, bits = 0;
    unsigned long m;
    if (!mask)
        return def;
    while (!((mask >> shift) & 1))
        shift++;
    for (m = mask >> shift; m & 1; m >>= 1)
        bits++;
    v = (v & mask) >> shift;
    if (bits >= 8)
        return (unsigned)(v >> (bits - 8));
    return (unsigned)(v * 255 / ((1ul << bits) - 1));
}

// Reads a BMP (1/4/8 bit with palette, 16/24/32 bit, BI_RGB or bit fields,
// top-down or bottom-up). 32-bit pictures keep their alpha as written. 1 = read.
static int Char_LoadBmp(const wchar_t *path, struct CharImage *img)
{
    FILE *f = _wfopen(path, L"rb");
    unsigned char *data = NULL, *px = NULL;
    long size;
    unsigned long off, hsz, comp, used, masks[4] = { 0, 0, 0, 0 };
    long w, h;
    int bpp, topDown, y, x, ok = 0;
    size_t stride, palAt, palCount = 0;

    memset(img, 0, sizeof(*img));
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 54 && size <= 64L * 1024 * 1024 &&
        fseek(f, 0, SEEK_SET) == 0) {
        data = Rs_Alloc((size_t)size);
        if (fread(data, 1, (size_t)size, f) != (size_t)size) {
            Rs_Free(data);
            data = NULL;
        }
    }
    fclose(f);
    if (!data)
        return 0;

    if (data[0] != 'B' || data[1] != 'M')
        goto done;
    off = Char_Le32(data + 10);
    hsz = Char_Le32(data + 14);
    if (hsz < 40 || hsz > (unsigned long)size - 14)
        goto done;      // the header must lie inside the file (it covers the masks at 54..69)
    w = (long)Char_Le32(data + 18);
    h = (long)Char_Le32(data + 22);
    bpp = (int)Char_Le16(data + 28);
    comp = Char_Le32(data + 30);
    used = Char_Le32(data + 46);
    if (h < -CHAR_IMAGE_MAX)
        goto done;      // also keeps -h defined
    topDown = h < 0;
    if (h < 0)
        h = -h;
    if (w <= 0 || h <= 0 || w > CHAR_IMAGE_MAX || h > CHAR_IMAGE_MAX || (long long)w * h > 16LL * 1024 * 1024)
        goto done;
    if (comp != 0 && comp != 3 && comp != 6)
        goto done;      // compressed: not written by rldpack
    palAt = 14 + hsz;
    if (comp == 3 || comp == 6) {
        if (hsz >= 52) {
            masks[0] = Char_Le32(data + 54);
            masks[1] = Char_Le32(data + 58);
            masks[2] = Char_Le32(data + 62);
            if (hsz >= 56)
                masks[3] = Char_Le32(data + 66);
        } else {
            if (palAt + (comp == 6 ? 16u : 12u) > (size_t)size)
                goto done;
            masks[0] = Char_Le32(data + palAt);
            masks[1] = Char_Le32(data + palAt + 4);
            masks[2] = Char_Le32(data + palAt + 8);
            if (comp == 6)
                masks[3] = Char_Le32(data + palAt + 12);
            palAt += comp == 6 ? 16 : 12;
        }
    } else if (bpp == 16) {
        masks[0] = 0x7C00;
        masks[1] = 0x03E0;
        masks[2] = 0x001F;
    }
    if (bpp == 1 || bpp == 4 || bpp == 8) {
        palCount = used ? used : (1u << bpp);
        if (palCount > 256 || palAt + palCount * 4 > (size_t)size)
            goto done;
    } else if (bpp != 16 && bpp != 24 && bpp != 32) {
        goto done;
    }
    stride = (((size_t)w * (size_t)bpp + 31) / 32) * 4;
    if (off > (unsigned long)size || stride * (size_t)h > (size_t)size - off)
        goto done;

    px = Rs_Alloc((size_t)w * (size_t)h * 4);
    for (y = 0; y < h; y++) {
        const unsigned char *row = data + off + stride * (size_t)(topDown ? y : h - 1 - y);
        unsigned char *dst = px + (size_t)y * (size_t)w * 4;
        for (x = 0; x < w; x++, dst += 4) {
            unsigned b, g, r, a = 255;
            if (bpp <= 8) {
                unsigned bit = (unsigned)x * (unsigned)bpp;
                unsigned idx = (row[bit >> 3] >> (8 - bpp - (bit & 7))) & ((1u << bpp) - 1);
                const unsigned char *p = data + palAt + (size_t)(idx < palCount ? idx : 0) * 4;
                b = p[0];
                g = p[1];
                r = p[2];
            } else if (bpp == 24) {
                b = row[x * 3];
                g = row[x * 3 + 1];
                r = row[x * 3 + 2];
            } else {
                unsigned long v = bpp == 16 ? Char_Le16(row + x * 2) : Char_Le32(row + x * 4);
                if (bpp == 32 && comp == 0) {
                    b = v & 0xFF;
                    g = (v >> 8) & 0xFF;
                    r = (v >> 16) & 0xFF;
                    a = (v >> 24) & 0xFF;
                } else {
                    r = Char_MaskChannel(v, masks[0], 0);
                    g = Char_MaskChannel(v, masks[1], 0);
                    b = Char_MaskChannel(v, masks[2], 0);
                    a = Char_MaskChannel(v, masks[3], 255);
                }
            }
            dst[0] = (unsigned char)b;
            dst[1] = (unsigned char)g;
            dst[2] = (unsigned char)r;
            dst[3] = (unsigned char)a;
        }
    }
    // rldpack writes a valid alpha channel: a fully transparent icon
    // (icon-empty) shows as the checkerboard, not as a black block.
    img->w = (int)w;
    img->h = (int)h;
    img->px = px;
    px = NULL;
    ok = 1;
done:
    Rs_Free(px);
    Rs_Free(data);
    return ok;
}

// The picture box: the picture as large as fits, with whole steps when it is
// smaller than the box (pixels stay sharp), on a checkerboard where it is
// transparent. Without a picture a grey text (window text).
static void Char_ImagePaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    const struct CharImage *img = (const struct CharImage *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    RECT rc;
    HBRUSH br;

    GetClientRect(hwnd, &rc);
    br = CreateSolidBrush(RS_COL_PAGE);
    FillRect(dc, &rc, br);
    DeleteObject(br);
    if (img && img->px && img->w > 0 && img->h > 0) {
        int bw = rc.right - Rs_Px(8), bh = rc.bottom - Rs_Px(8);
        int dw, dh, cell, x, y;
        unsigned char *buf;
        BITMAPINFO bi;
        if (bw * img->h <= bh * img->w) {
            dw = bw;
            dh = (int)((long long)bw * img->h / img->w);
        } else {
            dh = bh;
            dw = (int)((long long)bh * img->w / img->h);
        }
        if (dw >= img->w && dh >= img->h) {
            int k = dw / img->w;
            if (dh / img->h < k)
                k = dh / img->h;
            dw = img->w * k;
            dh = img->h * k;
        }
        if (dw < 1)
            dw = 1;
        if (dh < 1)
            dh = 1;
        // Checker cells of about 8 screen pixels, counted in picture pixels.
        cell = img->w * Rs_Px(8) / dw;
        if (cell < 1)
            cell = 1;
        buf = Rs_Alloc((size_t)img->w * (size_t)img->h * 4);
        for (y = 0; y < img->h; y++) {
            for (x = 0; x < img->w; x++) {
                const unsigned char *s = img->px + ((size_t)y * img->w + x) * 4;
                unsigned char *d = buf + ((size_t)y * img->w + x) * 4;
                unsigned bg = (((x / cell) + (y / cell)) & 1) ? 204 : 240;
                unsigned a = s[3];
                d[0] = (unsigned char)((s[0] * a + bg * (255 - a) + 127) / 255);
                d[1] = (unsigned char)((s[1] * a + bg * (255 - a) + 127) / 255);
                d[2] = (unsigned char)((s[2] * a + bg * (255 - a) + 127) / 255);
                d[3] = 0;
            }
        }
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = img->w;
        bi.bmiHeader.biHeight = -img->h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, (rc.right - dw) / 2, (rc.bottom - dh) / 2, dw, dh, 0, 0, img->w, img->h,
                      buf, &bi, DIB_RGB_COLORS, SRCCOPY);
        Rs_Free(buf);
    } else {
        wchar_t text[128];
        HGDIOBJ old = SelectObject(dc, Rs_Font(RS_FONT_SMALL));
        GetWindowTextW(hwnd, text, 128);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RS_COL_MUTED);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
    }
    br = CreateSolidBrush(RS_COL_BORDER);
    FrameRect(dc, &rc, br);
    DeleteObject(br);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK Char_ImageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Char_ImagePaint(hwnd);
        return 0;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        return r;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HWND Char_ImageBox(HWND page, int id, struct CharImage *img)
{
    HWND h = CreateWindowExW(0, CHAR_IMAGE_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, page,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (h)
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)img);
    return h;
}

static void Char_ImagesClear(void)
{
    int i;
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        Char_ImageFree(&g_char.image[i]);
        InvalidateRect(g_char.iconImage[i], NULL, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Temp files of a check: <Rs_TempDir>\char-<process>-<number>... (Rs_TempDir:
// %TEMP%\Reload Studio, with --settings the folder of the settings file)
// ---------------------------------------------------------------------------

static void Char_TempBase(wchar_t *out, int cap, int seq)
{
    wchar_t dir[CHAR_VAL];
    wchar_t name[64];

    Rs_TempDir(dir, CHAR_VAL);   // created there
    swprintf(name, 64, L"char-%lu-%d", (unsigned long)GetCurrentProcessId(), seq);
    Rs_PathJoin(out, cap, dir, name);
}

// kind: L".rldpv" (model preview), L"-original.bmp", L"-icon.bmp", L"" (the
// --icon-preview prefix).
static void Char_TempPath(wchar_t *out, int cap, int seq, const wchar_t *kind)
{
    Char_TempBase(out, cap, seq);
    Char_Append(out, cap, kind);
}

// Every file a check leaves: the --preview file and the two of --icon-preview.
static const wchar_t *const g_charTempKinds[3] = { L".rldpv", L"-original.bmp", L"-icon.bmp" };

static void Char_TempDelete(int seq)
{
    wchar_t path[CHAR_VAL];
    int i;
    if (seq <= 0)
        return;
    for (i = 0; i < 3; i++) {
        Char_TempPath(path, CHAR_VAL, seq, g_charTempKinds[i]);
        DeleteFileW(path);
    }
}

// 1 if name is exactly one of the page's own temp files,
// "char-<digits>-<digits><kind>" with a kind of g_charTempKinds; *pid = the
// first number. Nothing else in the folder is the page's to delete - with
// --settings it is a folder of the user.
static int Char_TempOwnName(const wchar_t *name, unsigned long *pid)
{
    const wchar_t *p = name + 5;
    const wchar_t *digits;
    int i;

    if (wcsncmp(name, L"char-", 5) != 0)
        return 0;
    digits = p;
    while (*p >= L'0' && *p <= L'9')
        p++;
    if (p == digits || p - digits > 10 || *p != L'-')
        return 0;
    *pid = wcstoul(digits, NULL, 10);
    digits = ++p;
    while (*p >= L'0' && *p <= L'9')
        p++;
    if (p == digits || p - digits > 10)
        return 0;
    for (i = 0; i < 3; i++)
        if (wcscmp(p, g_charTempKinds[i]) == 0)
            return 1;
    return 0;
}

// An outdated check is ended; its files are deleted when it has gone.
static void Char_Abandon(void)
{
    int i;
    if (!g_char.jobId)
        return;
    if (g_char.jobKind == CHAR_JOB_CHECK) {
        Rs_KillJob(g_char.jobId);
        for (i = 0; i < CHAR_PENDING; i++)
            if (!g_char.pending[i].id)
                break;
        if (i == CHAR_PENDING) {
            Char_TempDelete(g_char.pending[0].seq);
            memmove(&g_char.pending[0], &g_char.pending[1], sizeof(g_char.pending[0]) * (CHAR_PENDING - 1));
            i = CHAR_PENDING - 1;
        }
        g_char.pending[i].id = g_char.jobId;
        g_char.pending[i].seq = g_char.jobSeq;
    }
    g_char.jobId = 0;
    g_char.jobKind = CHAR_JOB_NONE;
    g_char.jobSeq = 0;
}

// Leftovers of earlier runs: the page's own files "char-<pid>-<number><kind>"
// (Char_TempOwnName) of processes that are no longer running (a check ended
// on closing may still have held its files). Files of running Reload Studios
// and every other file stay.
static void Char_TempSweep(void)
{
    wchar_t pattern[CHAR_VAL];
    wchar_t dir[CHAR_VAL];
    wchar_t path[CHAR_VAL];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    Char_TempBase(pattern, CHAR_VAL, 0);
    Rs_PathDir(dir, CHAR_VAL, pattern);
    Rs_PathJoin(pattern, CHAR_VAL, dir, L"char-*");
    find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        unsigned long pid = 0;
        HANDLE proc;
        int alive = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (!Char_TempOwnName(fd.cFileName, &pid))
            continue;
        if (pid == 0 || pid == GetCurrentProcessId())
            continue;
        proc = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (proc) {
            alive = WaitForSingleObject(proc, 0) == WAIT_TIMEOUT;
            CloseHandle(proc);
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            alive = 1;      // there, but not ours to ask
        }
        if (alive)
            continue;
        Rs_PathJoin(path, CHAR_VAL, dir, fd.cFileName);
        DeleteFileW(path);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

static void Char_PendingDone(int id)
{
    int i;
    for (i = 0; i < CHAR_PENDING; i++) {
        if (g_char.pending[i].id == id) {
            Char_TempDelete(g_char.pending[i].seq);
            g_char.pending[i].id = 0;
            g_char.pending[i].seq = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// The pipe from rldpack
// ---------------------------------------------------------------------------

static void Char_JobReset(void)
{
    int i;
    for (i = 0; i < g_charJob.msgCount; i++) {
        Rs_Free(g_charJob.msgs[i].code);
        Rs_Free(g_charJob.msgs[i].text);
        Rs_Free(g_charJob.msgs[i].detail);
    }
    Rs_Free(g_charJob.msgs);
    memset(&g_charJob, 0, sizeof(g_charJob));
    g_char.rawLen = 0;
    g_char.rawLines = 0;
    if (g_char.rawText)
        g_char.rawText[0] = 0;
}

static void Char_JobMsg(int severity, const wchar_t *code, const wchar_t *text, const wchar_t *detail)
{
    struct CharJobData *j = &g_charJob;
    if (j->msgCount == j->msgCap) {
        int cap = j->msgCap ? j->msgCap * 2 : 16;
        struct CharMsg *m = Rs_Alloc((size_t)cap * sizeof(*m));
        if (j->msgCount)
            memcpy(m, j->msgs, (size_t)j->msgCount * sizeof(*m));
        Rs_Free(j->msgs);
        j->msgs = m;
        j->msgCap = cap;
    }
    j->msgs[j->msgCount].severity = severity;
    j->msgs[j->msgCount].code = Rs_Dup(code);
    j->msgs[j->msgCount].text = Rs_Dup(text);
    j->msgs[j->msgCount].detail = Rs_Dup(detail);
    j->msgCount++;
}

static void Char_RawAppend(const wchar_t *line)
{
    size_t n = wcslen(line);
    size_t need = g_char.rawLen + n + 3;
    if (need > g_char.rawCap) {
        size_t cap = g_char.rawCap ? g_char.rawCap : 4096;
        wchar_t *p;
        while (cap < need)
            cap *= 2;
        p = Rs_Alloc(cap * sizeof(wchar_t));
        if (g_char.rawLen)
            memcpy(p, g_char.rawText, g_char.rawLen * sizeof(wchar_t));
        Rs_Free(g_char.rawText);
        g_char.rawText = p;
        g_char.rawCap = cap;
    }
    if (g_char.rawLen) {
        g_char.rawText[g_char.rawLen++] = L'\r';
        g_char.rawText[g_char.rawLen++] = L'\n';
    }
    memcpy(g_char.rawText + g_char.rawLen, line, n * sizeof(wchar_t));
    g_char.rawLen += n;
    g_char.rawText[g_char.rawLen] = 0;
    g_char.rawLines++;
}

static void Char_RawRefresh(void)
{
    Rs_SetText(g_char.raw, g_char.rawText ? g_char.rawText : L"");
}

// The value fields from index 2 on as one text "a b c" - numbers that come as
// separate fields or in one field read the same.
static void Char_JoinFields(wchar_t **f, int n, wchar_t *out, int cap)
{
    int i;
    out[0] = 0;
    for (i = 2; i < n; i++) {
        if (out[0])
            Char_Append(out, cap, L" ");
        Char_Append(out, cap, Char_Field(f, n, i));
    }
}

// One line of the current run. Human lines and unknown kinds stay only in
// the raw output. The line is split in the process.
static void Char_ParseLine(wchar_t *line)
{
    struct CharJobData *j = &g_charJob;
    wchar_t *f[16];
    int n = Rs_SplitMachine(line, f, 16);
    const wchar_t *kind;

    if (n <= 0)
        return;
    kind = f[0];
    if (wcscmp(kind, L"rldpack") == 0) {
        j->protocolSeen = 1;
        j->protocol = _wtoi(Char_Field(f, n, 1));
    } else if (wcscmp(kind, L"file") == 0) {
        if (wcscmp(Char_Field(f, n, 1), L"ply") == 0) {
            const wchar_t *bytes = Char_Field(f, n, 4);
            j->plySeen = 1;
            Char_Copy(j->plyState, 16, Char_Field(f, n, 2));
            j->plyBytes = *bytes ? wcstoll(bytes, NULL, 10) : -1;
        }
    } else if (wcscmp(kind, L"value") == 0) {
        const wchar_t *key = Char_Field(f, n, 1);
        wchar_t all[256];
        int v[6];
        if (wcscmp(key, L"out") == 0) {
            j->outSeen = 1;
            Char_Copy(j->out, CHAR_VAL, Char_Field(f, n, 2));
        } else if (wcscmp(key, L"size-range") == 0) {
            Char_JoinFields(f, n, all, 256);
            if (Char_Numbers(all, v, 2) == 2 && v[0] == 0 && v[1] == 0) {
                j->rangeNone = 1;
            } else if (Char_Numbers(all, v, 2) == 2) {
                int lo = v[0] < CHAR_SIZE_MIN ? CHAR_SIZE_MIN : v[0];
                int hi = v[1] > CHAR_SIZE_MAX ? CHAR_SIZE_MAX : v[1];
                if (lo <= hi) {
                    j->rangeSeen = 1;
                    j->rangeLo = lo;
                    j->rangeHi = hi;
                } else {
                    j->rangeNone = 1;
                }
            }
        } else if (wcscmp(key, L"kart-box") == 0) {
            Char_JoinFields(f, n, all, 256);
            if (Char_Numbers(all, v, 6) == 6) {
                j->kartSeen = 1;
                memcpy(j->kart, v, sizeof(j->kart));
            }
        } else if (wcscmp(key, L"retail-kart") == 0) {
            Char_JoinFields(f, n, all, 256);
            if (Char_Numbers(all, v, 6) == 6) {
                j->retailSeen = 1;
                memcpy(j->retail, v, sizeof(j->retail));
            }
        } else if (wcscmp(key, L"crash-box") == 0) {
            // Decimals ("-33.8"): kept in tenths.
            Char_JoinFields(f, n, all, 256);
            if (Char_NumbersScaled(all, v, 6, 10) == 6) {
                j->crashSeen = 1;
                memcpy(j->crash, v, sizeof(j->crash));
            }
        } else if (wcscmp(key, L"reduce") == 0 || wcscmp(key, L"fit") == 0 || wcscmp(key, L"wheels") == 0) {
            wchar_t *dst = key[0] == L'r' ? j->reduce : key[0] == L'f' ? j->fit : j->wheels;
            Char_JoinFields(f, n, dst, 48);
        } else if (wcscmp(key, L"repair") == 0) {
            Char_JoinFields(f, n, j->repair, 48);
        } else if (wcscmp(key, L"open-parts") == 0) {
            Char_JoinFields(f, n, j->openParts, 48);
        } else if (wcscmp(key, L"remesh") == 0) {
            Char_JoinFields(f, n, j->remesh, 48);
        }
    } else if (wcscmp(kind, L"char") == 0) {
        const wchar_t *key = Char_Field(f, n, 1);
        if (wcscmp(key, L"triangles") == 0 || (wcscmp(key, L"faces") == 0 && !j->triangles[0]))
            Char_Copy(j->triangles, 32, Char_Field(f, n, 2));
        else if (wcscmp(key, L"parts") == 0)
            Char_Copy(j->parts, 160, Char_Field(f, n, 3));
        else if (wcscmp(key, L"voices") == 0)
            Char_Copy(j->voices, 32, Char_Field(f, n, 2));
        else if (wcscmp(key, L"reduced") == 0 && n >= 6) {
            // <triangles before> <after> <draw bytes before> <after>
            int i;
            for (i = 0; i < 4; i++)
                j->reduced[i] = wcstol(Char_Field(f, n, 2 + i), NULL, 10);
            j->reducedSeen = 1;
        } else if (wcscmp(key, L"repaired") == 0 && n >= 2 + CHAR_REPAIR_FIELDS) {
            // the ten counts of the repair (RldMk_EmitFacts in tools/rldpack_char.inc)
            int i;
            for (i = 0; i < CHAR_REPAIR_FIELDS; i++)
                j->repaired[i] = wcstol(Char_Field(f, n, 2 + i), NULL, 10);
            j->repairedSeen = 1;
        } else if (wcscmp(key, L"two-sided") == 0 && n >= 3) {
            j->twoSided = wcstol(Char_Field(f, n, 2), NULL, 10);
            j->twoSidedSeen = 1;
        } else if (wcscmp(key, L"remeshed") == 0 && n >= 2 + CHAR_REMESH_FIELDS) {
            int i;
            for (i = 0; i < CHAR_REMESH_FIELDS; i++)
                j->remeshed[i] = wcstol(Char_Field(f, n, 2 + i), NULL, 10);
            j->remeshedSeen = 1;
        } else if (wcscmp(key, L"fit") == 0 && n >= 8) {
            // <factor> <length before> <after> <height before> <after> <kart|model>
            j->fitSeen = 1;
            Char_Copy(j->fitFactor, 16, Char_Field(f, n, 2));
            Char_Copy(j->fitLength[0], 16, Char_Field(f, n, 3));
            Char_Copy(j->fitLength[1], 16, Char_Field(f, n, 4));
            Char_Copy(j->fitHeight[0], 16, Char_Field(f, n, 5));
            Char_Copy(j->fitHeight[1], 16, Char_Field(f, n, 6));
            Char_Copy(j->fitBasis, 16, Char_Field(f, n, 7));
        }
    } else if (wcscmp(kind, L"msg") == 0) {
        const wchar_t *text = Char_Field(f, n, 3);
        const wchar_t *detail = Char_Field(f, n, 4);
        if (!*text) {
            text = detail;
            detail = L"";
        }
        Char_JobMsg(Rs_SeverityFromText(Char_Field(f, n, 1)), Char_Field(f, n, 2), text, detail);
    } else if (wcscmp(kind, L"result") == 0) {
        const wchar_t *bytes = Char_Field(f, n, 3);
        j->resultSeen = 1;
        Char_Copy(j->resultState, 16, Char_Field(f, n, 1));
        Char_Copy(j->resultPath, CHAR_VAL, Char_Field(f, n, 2));
        j->resultBytes = *bytes ? wcstoll(bytes, NULL, 10) : -1;
        Char_Copy(j->resultSha, 80, Char_Field(f, n, 4));
    } else if (wcscmp(kind, L"end") == 0) {
        j->endSeen = 1;
        j->endCode = _wtoi(Char_Field(f, n, 1));
    }
}

static const struct CharMsg *Char_FindMsg(const wchar_t *code)
{
    int i;
    for (i = 0; i < g_charJob.msgCount; i++)
        if (g_charJob.msgs[i].code && wcscmp(g_charJob.msgs[i].code, code) == 0)
            return &g_charJob.msgs[i];
    return NULL;
}

// ---------------------------------------------------------------------------
// Size slider
// ---------------------------------------------------------------------------

static int Char_SizePos(void)
{
    return (int)SendMessageW(g_char.size, TBM_GETPOS, 0, 0);
}

static void Char_SizeShow(void)
{
    wchar_t t[16];
    swprintf(t, 16, L"%d %%", g_char.sizeNow);
    Rs_SetText(g_char.sizeValue, t);
}

// The line below the slider: the allowed range, or why the slider went back.
static void Char_SizeNote(void)
{
    wchar_t t[768];
    if (g_char.rangeNone) {
        Char_SetLabel(g_char.sizeNote, L"No size fits this model - make the model itself smaller (see the messages).",
                      RS_COL_ERROR, &g_char.sizeColor);
        return;
    }
    if (!g_char.rangeKnown) {
        Char_SetLabel(g_char.sizeNote, L"The sizes this model allows show up after the first check.",
                      RS_COL_MUTED, &g_char.sizeColor);
        return;
    }
    if (g_char.sizeLocked) {
        const wchar_t *why = g_char.sizeWhy[g_char.sizeLockHigh ? 1 : 0];
        swprintf(t, 768, L"Locked at %d %% - this model allows %d..%d %%%ls%ls", g_char.sizeNow,
                 g_char.rangeLo, g_char.rangeHi, why[0] ? L": " : L".", why);
        Char_SetLabel(g_char.sizeNote, t, RS_COL_WARNING, &g_char.sizeColor);
        return;
    }
    if (g_char.rangeLo > CHAR_SIZE_MIN || g_char.rangeHi < CHAR_SIZE_MAX)
        swprintf(t, 768, L"This model allows %d..%d %% - the rest of the bar is locked.", g_char.rangeLo,
                 g_char.rangeHi);
    else
        swprintf(t, 768, L"This model allows every size from %d to %d %%.", g_char.rangeLo, g_char.rangeHi);
    Char_SetLabel(g_char.sizeNote, t, RS_COL_MUTED, &g_char.sizeColor);
}

// Marks the allowed range on the bar (or nothing while it is unknown).
static void Char_SizeRangeShow(void)
{
    if (g_char.rangeKnown)
        SendMessageW(g_char.size, TBM_SETSEL, TRUE, MAKELPARAM(g_char.rangeLo, g_char.rangeHi));
    else
        SendMessageW(g_char.size, TBM_CLEARSEL, TRUE, 0);
}

// Puts the slider to value, inside the allowed range. Returns 1 if the value
// had to be moved into the range.
static int Char_SizeSet(int value)
{
    int locked = 0;
    if (value < CHAR_SIZE_MIN)
        value = CHAR_SIZE_MIN;
    if (value > CHAR_SIZE_MAX)
        value = CHAR_SIZE_MAX;
    if (g_char.rangeKnown) {
        if (value < g_char.rangeLo) {
            value = g_char.rangeLo;
            locked = 1;
            g_char.sizeLockHigh = 0;
        } else if (value > g_char.rangeHi) {
            value = g_char.rangeHi;
            locked = 1;
            g_char.sizeLockHigh = 1;
        }
    }
    if (Char_SizePos() != value)
        SendMessageW(g_char.size, TBM_SETPOS, TRUE, value);
    g_char.sizeNow = value;
    g_char.sizeLocked = locked;
    Char_SizeShow();
    Char_SizeNote();
    return locked;
}

// The model changed: its range is no longer known, the slider is free.
static void Char_SizeForget(void)
{
    g_char.rangeKnown = 0;
    g_char.rangeNone = 0;
    g_char.sizeWhy[0][0] = 0;
    g_char.sizeWhy[1][0] = 0;
    g_char.sizeLocked = 0;
    Char_SizeRangeShow();
    Char_SizeNote();
    Char_SetLabel(g_char.sizeFit, CHAR_FIT_WAIT_TEXT, RS_COL_MUTED, &g_char.fitColor);
}

// ---------------------------------------------------------------------------
// Display of the results
// ---------------------------------------------------------------------------

static int Char_CanBuild(void)
{
    return g_char.checked && !g_char.jobId && !g_char.timer;
}

static void Char_UpdateButtons(void)
{
    int building = g_char.jobId && g_char.jobKind == CHAR_JOB_BUILD;
    wchar_t model[CHAR_VAL];

    Char_FieldPath(g_char.model, model, CHAR_VAL);
    EnableWindow(g_char.check, model[0] && !building);
    EnableWindow(g_char.build, Char_CanBuild());
    EnableWindow(g_char.modelBrowse, !building);
    EnableWindow(g_char.iconBrowse, !building);
    EnableWindow(g_char.voicesBrowse, !building);
    EnableWindow(g_char.outBrowse, !building);
    EnableWindow(g_char.show, !building);
}

static void Char_ShowView(void)
{
    if (g_char.showRaw)
        Char_RawRefresh();
    ShowWindow(g_char.raw, g_char.showRaw ? SW_SHOW : SW_HIDE);
    ShowWindow(g_char.msgs, g_char.showRaw ? SW_HIDE : SW_SHOW);
    Rs_SetText(g_char.rawToggle, g_char.showRaw ? L"Show messages" : L"Show rldpack output");
}

static void Char_Relayout(HWND page)
{
    RECT rc;
    GetClientRect(page, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
        Char_Layout(page, rc.right, rc.bottom);
        InvalidateRect(page, NULL, TRUE);
    }
}

// The model info wraps to at most CHAR_INFO_LINES lines (Char_FitLines); the
// page is laid out again only when its height changes.
static void Char_SetInfo(const wchar_t *text, COLORREF color)
{
    HWND page = GetParent(g_char.modelInfo);
    RECT rc;
    int need;

    Char_Copy(g_char.infoFull, CHAR_VAL, text);
    Char_SetLabel(g_char.modelInfo, text, color, &g_char.infoColor);
    GetClientRect(g_char.modelInfo, &rc);
    if (rc.right <= 0)
        return;
    Char_FitLines(g_char.modelInfo, g_char.infoFull, rc.right, CHAR_INFO_LINES);
    need = Char_TextHeight(g_char.modelInfo, rc.right, CHAR_INFO_LINES);
    if (need < Rs_Px(18))
        need = Rs_Px(18);
    if (need != rc.bottom)
        Char_Relayout(page);
}

static int Char_CountMsgs(int severity)
{
    int i, n = 0;
    for (i = 0; i < g_charJob.msgCount; i++)
        if (g_charJob.msgs[i].severity == severity)
            n++;
    return n;
}

// All @msg of one severity, in rldpack's order.
static void Char_AddMsgs(int severity)
{
    int i;
    for (i = 0; i < g_charJob.msgCount; i++)
        if (g_charJob.msgs[i].severity == severity)
            Rs_MsgListAdd(g_char.msgs, severity, g_charJob.msgs[i].text, g_charJob.msgs[i].detail);
}

// What rldpack could not report itself: no result, failure without a
// reason, foreign protocol. Return: number of errors that were added.
static int Char_AddRunProblems(int exitCode, int ok)
{
    struct CharJobData *j = &g_charJob;
    wchar_t t[256];

    if (j->protocolSeen && j->protocol != RS_PROTOCOL) {
        swprintf(t, 256, L"rldpack reports in format %d, but this Reload Studio reads format %d.",
                 j->protocol, RS_PROTOCOL);
        Rs_MsgListAdd(g_char.msgs, RS_SEV_WARNING, t,
                      L"Some results may be missing. rldpack is built into this Reload Studio, so both should always "
                      L"match - this build looks inconsistent.");
    }
    if (!j->resultSeen) {
        swprintf(t, 256, L"rldpack stopped without a result (exit code %d).", exitCode);
        Rs_MsgListAdd(g_char.msgs, RS_SEV_ERROR, t,
                      L"Click \"Show rldpack output\" to see everything it printed.");
        return 1;
    }
    if (!ok && Char_CountMsgs(RS_SEV_ERROR) == 0) {
        swprintf(t, 256, L"rldpack did not accept the character, but gave no reason (exit code %d).", exitCode);
        Rs_MsgListAdd(g_char.msgs, RS_SEV_ERROR, t,
                      L"Click \"Show rldpack output\" to see everything it printed.");
        return 1;
    }
    return 0;
}

// The line below the model field: what rldpack found in the PLY.
static void Char_ApplyModelInfo(void)
{
    struct CharJobData *j = &g_charJob;
    wchar_t t[512];
    wchar_t size[32];

    if (j->plySeen && wcscmp(j->plyState, L"ok") != 0) {
        Char_SetInfo(L"The model file cannot be opened - check the path.", RS_COL_ERROR);
        return;
    }
    if (!j->plySeen) {
        Char_SetInfo(L"-", RS_COL_MUTED);
        return;
    }
    Char_SizeText(size, 32, j->plyBytes < 0 ? 0 : j->plyBytes);
    if (j->reducedSeen) {
        // Reduced: what it changed first, then the parts as without it.
        wchar_t n[4][24];
        int i;
        for (i = 0; i < 4; i++)
            Char_Grouped(n[i], 24, j->reduced[i]);
        swprintf(t, 512, L"Triangles %ls -> %ls, draw memory %ls -> %ls bytes - reduced automatically%ls%ls (%ls)",
                 n[0], n[1], n[2], n[3], j->parts[0] ? L" - " : L"", j->parts, size);
    } else if (j->triangles[0] && j->parts[0])
        swprintf(t, 512, L"%ls triangles - %ls  (%ls)", j->triangles, j->parts, size);
    else if (j->triangles[0])
        swprintf(t, 512, L"%ls triangles  (%ls)", j->triangles, size);
    else
        swprintf(t, 512, L"read, but not converted - see the messages  (%ls)", size);
    Char_SetInfo(t, j->parts[0] ? RS_COL_TEXT : RS_COL_WARNING);
}

// The line below the slider: what 100 % is for this model (@char fit).
static void Char_ApplyFit(void)
{
    struct CharJobData *j = &g_charJob;
    wchar_t t[256];

    if (j->fitSeen) {
        // With a kart its length was matched to Crash's (the driver may stick
        // out); without one the whole model. With --wheels off rldpack always
        // takes the whole model, so "model" means "no kart" only while the
        // wheels are on. The factor as received (4 significant digits: a
        // model in millimeters is not "x0.00").
        const int wheelsOff = wcsncmp(j->wheels, L"off", 3) == 0;
        const wchar_t *basis = (wcscmp(j->fitBasis, L"model") == 0 && !wheelsOff) ? L" - no kart" : L"";
        swprintf(t, 256, L"Fitted to Crash size: x%ls (%ls -> %ls long)%ls", j->fitFactor, j->fitLength[0],
                 j->fitLength[1], basis);
        Char_SetLabel(g_char.sizeFit, t, RS_COL_TEXT, &g_char.fitColor);
    } else if (wcsncmp(j->fit, L"none", 4) == 0) {
        Char_SetLabel(g_char.sizeFit, L"Not fitted - the model keeps the size it was exported in.", RS_COL_MUTED,
                      &g_char.fitColor);
    } else if (j->plySeen && wcscmp(j->plyState, L"ok") == 0) {
        Char_SetLabel(g_char.sizeFit, L"Not fitted - see the messages.", RS_COL_MUTED, &g_char.fitColor);
    } else {
        Char_SetLabel(g_char.sizeFit, CHAR_FIT_WAIT_TEXT, RS_COL_MUTED, &g_char.fitColor);
    }
}

// The line below the voices folder: how many places rldpack filled.
static void Char_ApplyVoices(void)
{
    wchar_t t[256];
    wchar_t voices[CHAR_VAL];

    Char_FieldPath(g_char.voices, voices, CHAR_VAL);
    if (voices[0] && g_charJob.voices[0])
        swprintf(t, 256, L"%ls voice places filled - checked but not packed yet, the driver is silent in the game.",
                 g_charJob.voices);
    else
        Char_Copy(t, 256, CHAR_VOICES_TEXT);
    Rs_SetText(g_char.voicesNote, t);
}

// "Closed hull (remesh)" works only with "Reduce automatically" (make-char:
// --remesh on needs --reduce auto). While that is off the box is greyed out
// and --remesh is not passed, whatever its tick says.
static int Char_RemeshAllowed(void)
{
    return Char_IsChecked(g_char.reduce);
}

static int Char_RemeshOn(void)
{
    return Char_RemeshAllowed() && Char_IsChecked(g_char.remesh);
}

static void Char_UpdateOptions(void)
{
    EnableWindow(g_char.remesh, Char_RemeshAllowed());
}

// "<n> <singular|plural>" with the number grouped, after ", " when out has text.
static void Char_CountPart(wchar_t *out, int cap, long n, const wchar_t *one, const wchar_t *many)
{
    wchar_t num[24];
    wchar_t part[128];
    Char_Grouped(num, 24, n);
    swprintf(part, 128, L"%ls%ls %ls", out[0] ? L", " : L"", num, n == 1 ? one : many);
    Char_Append(out, cap, part);
}

// The lines below the options: what the repair changed (only when it changed
// anything), the open parts drawn from both sides (only when there are such)
// and the remesh - each only as rldpack reported it (@char repaired,
// two-sided, remeshed) - and why "Closed hull" is greyed out. Lines are
// separated by CR LF.
static void Char_QualityText(wchar_t *out, int cap)
{
    struct CharJobData *j = &g_charJob;
    wchar_t line[512];
    wchar_t n[5][24];
    int i;

    out[0] = 0;
    if (j->repairedSeen) {
        const long *r = j->repaired;
        line[0] = 0;
        if (r[0])
            Char_CountPart(line, 512, r[0], L"corner welded", L"corners welded");
        if (r[1] + r[2])
            Char_CountPart(line, 512, r[1] + r[2], L"empty or doubled triangle dropped",
                           L"empty or doubled triangles dropped");
        if (r[3])
            Char_CountPart(line, 512, r[3], L"face turned outward", L"faces turned outward");
        if (r[9])
            Char_CountPart(line, 512, r[9], L"crack split", L"cracks split");
        if (r[4])
            Char_CountPart(line, 512, r[4], L"hole closed", L"holes closed");
        if (r[7] != r[8]) {
            wchar_t part[96];
            Char_Grouped(n[0], 24, r[7]);
            Char_Grouped(n[1], 24, r[8]);
            swprintf(part, 96, L"%lsopen edges %ls -> %ls", line[0] ? L", " : L"", n[0], n[1]);
            Char_Append(line, 512, part);
        }
        if (line[0]) {
            Char_Append(out, cap, L"Repaired: ");
            Char_Append(out, cap, line);
        }
    }
    // Only when it says something: the option on and parts drawn so.
    if (j->twoSidedSeen && j->twoSided > 0 && wcsncmp(j->openParts, L"one-sided", 9) != 0) {
        Char_Grouped(n[0], 24, j->twoSided);
        swprintf(line, 512, L"Two-sided open parts: %ls %ls (+0 triangles)", n[0],
                 j->twoSided == 1 ? L"triangle" : L"triangles");
        if (out[0])
            Char_Append(out, cap, L"\r\n");
        Char_Append(out, cap, line);
    }
    if (j->remeshedSeen) {
        for (i = 0; i < CHAR_REMESH_FIELDS; i++)
            Char_Grouped(n[i], 24, j->remeshed[i]);
        swprintf(line, 512, L"Remeshed: %ls triangles -> closed hulls of %ls -> %ls triangles, open edges %ls -> %ls",
                 n[0], n[1], n[2], n[3], n[4]);
        if (out[0])
            Char_Append(out, cap, L"\r\n");
        Char_Append(out, cap, line);
    }
    if (!Char_RemeshAllowed()) {
        if (out[0])
            Char_Append(out, cap, L"\r\n");
        Char_Append(out, cap, CHAR_REMESH_NEEDS_TEXT);
    }
}

// Height of the note below the options: 0 while it is empty.
static int Char_QualityHeight(int width)
{
    wchar_t *text = Rs_GetText(g_char.quality);
    int empty = !text[0];
    Rs_Free(text);
    return empty ? 0 : Char_TextHeight(g_char.quality, width, CHAR_QUALITY_LINES);
}

// Into the label; the page is laid out again when its height changes.
static void Char_ApplyQuality(void)
{
    wchar_t t[CHAR_VAL];
    RECT rc;
    int shownH, need;

    GetClientRect(g_char.quality, &rc);
    shownH = Char_IsShown(g_char.quality) ? rc.bottom : 0;
    Char_QualityText(t, CHAR_VAL);
    Char_SetLabel(g_char.quality, t, RS_COL_TEXT, &g_char.qualityColor);
    need = Char_QualityHeight(rc.right > 0 ? rc.right : Rs_Px(300));
    if (need != shownH)
        Char_Relayout(GetParent(g_char.quality));
}

static void Char_ViewNote(const wchar_t *text, COLORREF color)
{
    Char_SetLabel(g_char.viewNote, text, color, &g_char.viewColor);
}

// The converted model of the check into the 3D view, on the dummy it was
// fitted onto. Without a preview file the view shows why instead.
static void Char_ApplyPreview(int seq)
{
    wchar_t path[CHAR_VAL];
    struct CharJobData *j = &g_charJob;
    int i;

    if (j->kartSeen) {
        g_char.kartKnown = 1;
        memcpy(g_char.kart, j->kart, sizeof(g_char.kart));
    }
    // The retail kart box is only reported (the dummy under the model is the
    // retail kart itself).
    if (j->retailSeen) {
        g_char.retailKnown = 1;
        memcpy(g_char.retail, j->retail, sizeof(g_char.retail));
    }
    // Crash's size as an outline about the model; the game's wheels on the
    // dummy as the check box says.
    if (j->crashSeen) {
        g_char.crashKnown = 1;
        memcpy(g_char.crash, j->crash, sizeof(g_char.crash));
    }
    if (g_char.crashKnown)
        RsView_SetCrashBox(g_char.view, g_char.crash[0], g_char.crash[1], g_char.crash[2], g_char.crash[3],
                           g_char.crash[4], g_char.crash[5]);
    else
        RsView_SetCrashBox(g_char.view, 0, 0, 0, 0, 0, 0);
    RsView_SetWheels(g_char.view, Char_IsChecked(g_char.wheels));
    g_char.previewShown = 0;
    g_char.previewPoses = 0;
    memset(g_char.previewTris, 0, sizeof(g_char.previewTris));
    Char_TempPath(path, CHAR_VAL, seq, L".rldpv");
    if (!Rs_FileExists(path)) {
        RsView_Clear(g_char.view, L"No preview - rldpack could not convert the model.");
        Char_ViewNote(L"No preview - fix the errors in the message list first.", RS_COL_WARNING);
        return;
    }
    // The view reads the file into memory; it is deleted right after.
    RsView_LoadPreview(g_char.view, path);
    if (!RsView_Loaded(g_char.view)) {
        Char_ViewNote(L"The preview file could not be shown - check the model again.", RS_COL_WARNING);
        return;
    }
    g_char.previewShown = 1;
    g_char.previewPoses = CHAR_POSES;
    for (i = 0; i < CHAR_POSES; i++)
        g_char.previewTris[i] = (unsigned long)RsView_TriangleCount(g_char.view, i);
    RsView_SetPose(g_char.view, g_charPoseView[g_char.poseNow]);
    Char_ViewNote(g_char.crashKnown ? L"Drag to turn. Grey: Crash's kart the model is fitted onto. Dashed box: Crash's size."
                                    : L"Drag to turn. Grey: Crash's kart the model is fitted onto.",
                  RS_COL_MUTED);
}

// The icon as decoded and as converted, from the --icon-preview files.
static void Char_ApplyIcon(int seq)
{
    static const wchar_t *const kinds[CHAR_IMG_COUNT] = { L"-original.bmp", L"-icon.bmp" };
    wchar_t icon[CHAR_VAL];
    wchar_t path[CHAR_VAL];
    int i;

    Char_ImagesClear();
    Char_FieldPath(g_char.icon, icon, CHAR_VAL);
    Rs_SetText(g_char.iconImage[CHAR_IMG_ORIGINAL], icon[0] ? L"cannot be read" : L"no PNG chosen");
    Rs_SetText(g_char.iconImage[CHAR_IMG_ICON], icon[0] ? L"not converted" : L"the template's icon");
    if (!icon[0])
        return;
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        Char_TempPath(path, CHAR_VAL, seq, kinds[i]);
        if (Rs_FileExists(path))
            Char_LoadBmp(path, &g_char.image[i]);
        InvalidateRect(g_char.iconImage[i], NULL, FALSE);
    }
}

static void Char_ShowCheckResult(int exitCode)
{
    struct CharJobData *j = &g_charJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"checked") == 0;
    wchar_t t[256];
    int errors;

    g_char.checked = ok;
    Char_Copy(g_char.checkedPath, CHAR_VAL, j->resultPath);
    Rs_MsgListClear(g_char.msgs);
    errors = Char_AddRunProblems(exitCode, ok);
    errors += Char_CountMsgs(RS_SEV_ERROR);
    Char_AddMsgs(RS_SEV_ERROR);
    Char_AddMsgs(RS_SEV_WARNING);
    Char_AddMsgs(RS_SEV_NOTE);
    Char_AddMsgs(RS_SEV_INFO);
    Char_AddMsgs(RS_SEV_OK);
    Char_EmptyText(L"rldpack reported nothing.");
    if (ok) {
        Char_Headline(L"Ready to build", RS_COL_OK);
    } else {
        swprintf(t, 256, L"Cannot build yet - %d problem(s)", errors);
        Char_Headline(t, RS_COL_ERROR);
    }
    Char_UpdateButtons();
    if (Rs_Automating()) {
        if (ok)
            Rs_AutoLog(L"  check: ready to build, %d warning(s), %d note(s)",
                       Char_CountMsgs(RS_SEV_WARNING), Char_CountMsgs(RS_SEV_NOTE));
        else
            Rs_AutoLog(L"  check: cannot build yet, %d problem(s)", errors);
        if (g_char.rangeNone)
            Rs_AutoLog(L"  size: %d %%, no size fits this model", g_char.sizeNow);
        else if (g_char.rangeKnown)
            Rs_AutoLog(L"  size: %d %%, this model allows %d..%d %%", g_char.sizeNow, g_char.rangeLo,
                       g_char.rangeHi);
        else
            Rs_AutoLog(L"  size: %d %%, allowed range not reported", g_char.sizeNow);
        if (g_charJob.fitSeen)
            Rs_AutoLog(L"  fit: x%ls, %ls -> %ls long, %ls -> %ls tall, by the %ls", g_charJob.fitFactor,
                       g_charJob.fitLength[0], g_charJob.fitLength[1], g_charJob.fitHeight[0],
                       g_charJob.fitHeight[1], g_charJob.fitBasis);
        if (g_charJob.reducedSeen)
            Rs_AutoLog(L"  reduced: %ld -> %ld triangles, draw memory %ld -> %ld bytes", g_charJob.reduced[0],
                       g_charJob.reduced[1], g_charJob.reduced[2], g_charJob.reduced[3]);
        if (g_charJob.repairedSeen)
            Rs_AutoLog(L"  repaired: %ld welded, %ld degenerate, %ld doubled, %ld turned, %ld holes closed with %ld "
                       L"triangles, %ld left, open edges %ld -> %ld, %ld cracks split",
                       g_charJob.repaired[0], g_charJob.repaired[1], g_charJob.repaired[2], g_charJob.repaired[3],
                       g_charJob.repaired[4], g_charJob.repaired[5], g_charJob.repaired[6], g_charJob.repaired[7],
                       g_charJob.repaired[8], g_charJob.repaired[9]);
        if (g_charJob.twoSidedSeen)
            Rs_AutoLog(L"  two-sided open parts: %ld triangles", g_charJob.twoSided);
        if (g_charJob.remeshedSeen)
            Rs_AutoLog(L"  remeshed: %ld -> %ld -> %ld triangles, open edges %ld -> %ld", g_charJob.remeshed[0],
                       g_charJob.remeshed[1], g_charJob.remeshed[2], g_charJob.remeshed[3], g_charJob.remeshed[4]);
        if (g_char.previewShown)
            Rs_AutoLog(L"  preview: %d pose(s), %lu/%lu/%lu triangles", g_char.previewPoses,
                       g_char.previewTris[0], g_char.previewTris[1], g_char.previewTris[2]);
        else
            Rs_AutoLog(L"  preview: none");
        if (g_char.image[CHAR_IMG_ORIGINAL].px || g_char.image[CHAR_IMG_ICON].px)
            Rs_AutoLog(L"  icon: original %dx%d, converted %dx%d", g_char.image[CHAR_IMG_ORIGINAL].w,
                       g_char.image[CHAR_IMG_ORIGINAL].h, g_char.image[CHAR_IMG_ICON].w,
                       g_char.image[CHAR_IMG_ICON].h);
    }
}

static void Char_StartFailed(const wchar_t *headline)
{
    Rs_MsgListClear(g_char.msgs);
    Rs_MsgListAdd(g_char.msgs, RS_SEV_ERROR, L"Reload Studio could not start rldpack.",
                  L"rldpack runs as a second copy of Reload Studio. Try again; if it keeps failing, "
                  L"check that no security program blocks it.");
    Char_Headline(headline, RS_COL_ERROR);
    Char_UpdateButtons();
    if (Rs_Automating())
        Rs_AutoLog(L"  rldpack could not be started");
}

// ---------------------------------------------------------------------------
// Commands to rldpack
// ---------------------------------------------------------------------------

struct CharArgs {
    const wchar_t *v[CHAR_MAX_ARGS];
    int n;
    wchar_t model[CHAR_VAL];
    wchar_t icon[CHAR_VAL];
    wchar_t voices[CHAR_VAL];
    wchar_t out[CHAR_VAL];
    wchar_t preview[CHAR_VAL];
    wchar_t iconPrefix[CHAR_VAL];
    wchar_t size[16];
    wchar_t *name;          // Rs_Free
};

static void Char_ArgsAdd(struct CharArgs *a, const wchar_t *s)
{
    if (a->n < CHAR_MAX_ARGS)
        a->v[a->n++] = s;
}

static void Char_ArgsFree(struct CharArgs *a)
{
    Rs_Free(a->name);
    a->name = NULL;
}

static const wchar_t *Char_ClassWord(void)
{
    LRESULT sel = SendMessageW(g_char.cls, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= CHAR_CLASS_COUNT)
        sel = 0;
    return g_charClasses[sel].word;
}

// make-char with the values of the fields. check = 1: with --check and the
// preview files of number seq. out: output path (NULL = from the field).
// Returns 0 if there is no model.
static int Char_MakeArgs(struct CharArgs *a, int check, const wchar_t *out, int seq)
{
    memset(a, 0, sizeof(*a));
    Char_FieldPath(g_char.model, a->model, CHAR_VAL);
    if (!a->model[0])
        return 0;
    Char_FieldPath(g_char.icon, a->icon, CHAR_VAL);
    Char_FieldPath(g_char.voices, a->voices, CHAR_VAL);
    a->name = Rs_GetText(g_char.name);
    swprintf(a->size, 16, L"%d", g_char.sizeNow);

    Char_ArgsAdd(a, L"make-char");
    Char_ArgsAdd(a, L"--machine");
    if (check)
        Char_ArgsAdd(a, L"--check");
    Char_ArgsAdd(a, L"--model");
    Char_ArgsAdd(a, a->model);
    Char_ArgsAdd(a, L"--name");
    Char_ArgsAdd(a, a->name);
    Char_ArgsAdd(a, L"--template");
    Char_ArgsAdd(a, CHAR_TEMPLATE);
    Char_ArgsAdd(a, L"--class");
    Char_ArgsAdd(a, Char_ClassWord());
    Char_ArgsAdd(a, L"--size");
    Char_ArgsAdd(a, a->size);
    // Only what differs from rldpack's defaults (--repair auto, --open-parts
    // two-sided, --remesh off, --reduce auto, --wheels on).
    if (!Char_IsChecked(g_char.repair)) {
        Char_ArgsAdd(a, L"--repair");
        Char_ArgsAdd(a, L"off");
    }
    if (!Char_IsChecked(g_char.openParts)) {
        Char_ArgsAdd(a, L"--open-parts");
        Char_ArgsAdd(a, L"one-sided");
    }
    if (Char_RemeshOn()) {
        Char_ArgsAdd(a, L"--remesh");
        Char_ArgsAdd(a, L"on");
    }
    if (!Char_IsChecked(g_char.reduce)) {
        Char_ArgsAdd(a, L"--reduce");
        Char_ArgsAdd(a, L"off");
    }
    if (!Char_IsChecked(g_char.wheels)) {
        Char_ArgsAdd(a, L"--wheels");
        Char_ArgsAdd(a, L"off");
    }
    if (a->icon[0]) {
        Char_ArgsAdd(a, L"--icon");
        Char_ArgsAdd(a, a->icon);
        if (check) {
            Char_TempPath(a->iconPrefix, CHAR_VAL, seq, L"");
            Char_ArgsAdd(a, L"--icon-preview");
            Char_ArgsAdd(a, a->iconPrefix);
        }
    }
    if (a->voices[0]) {
        Char_ArgsAdd(a, L"--voices");
        Char_ArgsAdd(a, a->voices);
    }
    if (check) {
        Char_TempPath(a->preview, CHAR_VAL, seq, L".rldpv");
        Char_ArgsAdd(a, L"--preview");
        Char_ArgsAdd(a, a->preview);
    }
    if (out)
        Char_CleanPath(a->out, CHAR_VAL, out);
    else
        Char_FieldPath(g_char.out, a->out, CHAR_VAL);
    if (a->out[0]) {
        Char_ArgsAdd(a, L"--out");
        Char_ArgsAdd(a, a->out);
    }
    return 1;
}

static int Char_StartJob(HWND page, int kind, const wchar_t *const *args, int argc, int seq)
{
    Char_JobReset();
    g_char.jobKind = kind;
    g_char.jobSeq = seq;
    g_char.jobId = Rs_RunRldpack(page, args, argc);
    if (!g_char.jobId) {
        g_char.jobKind = CHAR_JOB_NONE;
        g_char.jobSeq = 0;
    }
    Char_UpdateButtons();
    return g_char.jobId;
}

// Nothing to check: no model.
static void Char_NoModel(void)
{
    Rs_MsgListClear(g_char.msgs);
    Char_JobReset();
    if (g_char.showRaw)
        Char_RawRefresh();
    g_char.checked = 0;
    g_char.previewShown = 0;
    g_char.previewPoses = 0;
    RsView_Clear(g_char.view, NULL);
    Char_ViewNote(L"Grey: Crash's kart, seat and steering wheel - a model is fitted onto it. Choose a PLY model.",
                  RS_COL_MUTED);
    Char_ApplyQuality();
    Char_SetInfo(L"-", RS_COL_MUTED);
    Char_Headline(L"Choose a PLY model to start", RS_COL_MUTED);
    Char_EmptyText(L"Choose a PLY model. What rldpack finds shows up here.");
    Char_UpdateButtons();
}

// Checks with the values of the fields. Return: 1 = rldpack running,
// 0 = done without rldpack (the message is already there), -1 = nothing to check.
static int Char_Check(HWND page)
{
    struct CharArgs a;
    int started, seq;

    // While building: the fields no longer match the last check. Build stays
    // locked, the check follows as soon as the build has ended (Char_JobDone).
    if (g_char.jobId && g_char.jobKind == CHAR_JOB_BUILD) {
        g_char.checked = 0;
        g_char.checkAfterBuild = 1;
        Char_UpdateButtons();
        return -1;
    }
    KillTimer(page, CHAR_TIMER_CHECK);
    g_char.timer = 0;
    g_char.checkAfterBuild = 0;
    Char_Abandon();             // a running check is thereby outdated
    g_char.checked = 0;
    seq = ++g_char.seq;
    if (!Char_MakeArgs(&a, 1, NULL, seq)) {
        Char_ArgsFree(&a);
        Char_NoModel();
        return -1;
    }
    Char_Headline(L"Checking...", RS_COL_MUTED);
    started = Char_StartJob(page, CHAR_JOB_CHECK, a.v, a.n, seq) != 0;
    Char_ArgsFree(&a);
    if (!started) {
        Char_StartFailed(L"Cannot build yet - 1 problem(s)");
        return 0;
    }
    return 1;
}

// Return: 1 = rldpack builds, 0 = refused (replacing declined), -1 = not possible.
static int Char_Build(HWND page)
{
    struct CharArgs a;
    wchar_t path[CHAR_VAL];
    wchar_t field[CHAR_VAL];
    int started;

    if (!Char_CanBuild())
        return -1;
    Char_FieldPath(g_char.out, field, CHAR_VAL);
    Char_CleanPath(path, CHAR_VAL, field[0] ? field : g_char.checkedPath);
    if (path[0] && Rs_FileExists(path)) {
        wchar_t question[CHAR_VAL + 64];
        swprintf(question, CHAR_VAL + 64, L"%ls exists. Replace it?", path);
        if (!Rs_AskYesNo(Rs_MainWindow(), L"Replace character?", question))
            return 0;
    }
    if (!Char_MakeArgs(&a, 0, path[0] ? path : NULL, 0)) {
        Char_ArgsFree(&a);
        return -1;
    }
    Char_Headline(L"Building...", RS_COL_MUTED);
    started = Char_StartJob(page, CHAR_JOB_BUILD, a.v, a.n, 0) != 0;
    Char_ArgsFree(&a);
    if (!started) {
        Char_StartFailed(L"Not built - 1 problem(s)");
        return -1;
    }
    return 1;
}

// A field has changed: check in 600 ms.
static void Char_Changed(HWND page)
{
    if (g_char.applying)
        return;
    g_char.checked = 0;
    SetTimer(page, CHAR_TIMER_CHECK, CHAR_CHECK_DELAY, NULL);
    g_char.timer = 1;
    Char_UpdateButtons();
}

// End of a check: range, model facts, preview and icon into the page. If the
// size is outside the range rldpack reported, the slider goes to the nearest
// allowed size and the check runs again at once.
static void Char_CheckDone(HWND page, int exitCode, int seq)
{
    struct CharJobData *j = &g_charJob;
    const struct CharMsg *sizeMsg = Char_FindMsg(L"char-size");
    wchar_t sizeWhy[512];

    sizeWhy[0] = 0;
    if (sizeMsg && sizeMsg->text) {
        // "Size N% is outside lo..hi% for this model: <why>. Choose a size in that range."
        const wchar_t *why = wcsstr(sizeMsg->text, L"for this model: ");
        wchar_t *advice;
        Char_Copy(sizeWhy, 512, why ? why + 16 : L"");
        advice = wcsstr(sizeWhy, L" Choose a size");
        if (advice)
            *advice = 0;
    }
    if (j->outSeen) {
        wchar_t cue[CHAR_VAL + 32];
        swprintf(cue, CHAR_VAL + 32, L"Next to the model: %ls", Rs_PathName(j->out));
        SendMessageW(g_char.out, EM_SETCUEBANNER, FALSE, (LPARAM)cue);
    }
    g_char.rangeNone = j->rangeNone;
    if (j->rangeNone) {
        g_char.rangeKnown = 0;
        g_char.sizeLocked = 0;
        Char_SizeRangeShow();
        Char_SizeNote();
    } else if (j->rangeSeen) {
        int was = g_char.sizeNow;
        int lockedBefore = g_char.sizeLocked;
        g_char.rangeKnown = 1;
        g_char.rangeLo = j->rangeLo;
        g_char.rangeHi = j->rangeHi;
        // The reason belongs to the bound the checked size went past.
        if (sizeWhy[0] && (was < g_char.rangeLo || was > g_char.rangeHi))
            Char_Copy(g_char.sizeWhy[was > g_char.rangeHi ? 1 : 0], 512, sizeWhy);
        Char_SizeRangeShow();
        g_char.applying = 1;
        if (!Char_SizeSet(was)) {
            // In the range: an earlier "Locked at" stays until the slider moves.
            g_char.sizeLocked = lockedBefore;
            Char_SizeNote();
        } else {
            g_char.applying = 0;
            if (Rs_Automating())
                Rs_AutoLog(L"  size: %d %% is outside %d..%d %% for this model - set to %d %%", was,
                           g_char.rangeLo, g_char.rangeHi, g_char.sizeNow);
            Char_TempDelete(seq);
            if (Char_Check(page) >= 0) {
                Char_SizeNote();
                return;
            }
        }
        g_char.applying = 0;
    } else if (j->plySeen && wcscmp(j->plyState, L"ok") != 0) {
        Char_SizeForget();
    }
    Char_ApplyModelInfo();
    Char_ApplyFit();
    Char_ApplyVoices();
    Char_ApplyQuality();
    Char_ApplyPreview(seq);
    Char_ApplyIcon(seq);
    Char_TempDelete(seq);
    Char_ShowCheckResult(exitCode);
}

static void Char_BuildDone(HWND page, int exitCode)
{
    struct CharJobData *j = &g_charJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"ok") == 0;
    wchar_t text[CHAR_VAL + 64];
    wchar_t detail[128];
    wchar_t size[32];

    Rs_MsgListClear(g_char.msgs);
    Char_EmptyText(L"rldpack reported nothing.");
    if (ok) {
        Char_Copy(g_char.built, CHAR_VAL, j->resultPath);
        g_char.builtBytes = j->resultBytes;
        Char_Copy(g_char.builtSha, 80, j->resultSha);
        Char_SizeText(size, 32, j->resultBytes < 0 ? 0 : j->resultBytes);
        swprintf(text, CHAR_VAL + 64, L"Character built: %ls (%ls)", Rs_PathName(g_char.built), size);
        Char_Headline(text, RS_COL_OK);
        swprintf(text, CHAR_VAL + 64, L"Built %ls", g_char.built);
        swprintf(detail, 128, L"SHA-256 %ls", g_char.builtSha);
        Rs_MsgListAdd(g_char.msgs, RS_SEV_OK, text, detail);
        Char_AddRunProblems(exitCode, 1);
        Char_AddMsgs(RS_SEV_WARNING);
        Char_AddMsgs(RS_SEV_NOTE);
        Rs_ConfigSet(L"char.out", g_char.built);
        if (Rs_Automating())
            Rs_AutoLog(L"  build: ok %ls %lld bytes sha256 %ls", g_char.built, g_char.builtBytes, g_char.builtSha);
    } else {
        int errors;
        g_char.checked = 0;
        g_char.built[0] = 0;
        errors = Char_AddRunProblems(exitCode, 0);
        errors += Char_CountMsgs(RS_SEV_ERROR);
        Char_AddMsgs(RS_SEV_ERROR);
        Char_AddMsgs(RS_SEV_WARNING);
        Char_AddMsgs(RS_SEV_NOTE);
        swprintf(text, CHAR_VAL + 64, L"Not built - %d problem(s)", errors);
        Char_Headline(text, RS_COL_ERROR);
        if (Rs_Automating())
            Rs_AutoLog(L"  build: failed, %d error(s)", errors);
    }
    Char_Relayout(page);        // show or hide "Show in folder"
}

static void Char_JobDone(HWND page, int exitCode)
{
    int kind = g_char.jobKind;
    int seq = g_char.jobSeq;

    g_char.jobId = 0;
    g_char.jobKind = CHAR_JOB_NONE;
    g_char.jobSeq = 0;
    if (g_char.showRaw)
        Char_RawRefresh();
    if (kind == CHAR_JOB_BUILD) {
        Char_BuildDone(page, exitCode);
        if (g_char.checkAfterBuild)
            Char_Check(page);
    }
    else
        Char_CheckDone(page, exitCode, seq);
    Char_UpdateButtons();
}

// ---------------------------------------------------------------------------
// Fields
// ---------------------------------------------------------------------------

// A new model: the size range of the old one no longer counts.
static void Char_ModelChanged(HWND page)
{
    if (g_char.applying)
        return;
    Char_SizeForget();
    Char_Changed(page);
}

// Sets the model and checks at once. Returns 1 if rldpack is running.
static int Char_SetModel(HWND page, const wchar_t *path)
{
    wchar_t clean[CHAR_VAL];
    wchar_t dir[CHAR_VAL];

    Char_CleanPath(clean, CHAR_VAL, path);
    g_char.applying = 1;
    Rs_SetText(g_char.model, clean);
    g_char.applying = 0;
    Char_SizeForget();
    if (clean[0]) {
        Rs_PathDir(dir, CHAR_VAL, clean);
        Rs_ConfigSet(L"char.folder", dir);
    }
    return Char_Check(page) > 0;
}

// Only the characters of the menu font, capitals, at most 17. rldpack checks
// the name all the same; this only keeps typing on the right path.
static void Char_NameFilter(void)
{
    wchar_t *text = Rs_GetText(g_char.name);
    wchar_t clean[CHAR_NAME_MAX + 1];
    int n = 0, dropped = 0;
    const wchar_t *p;

    for (p = text; *p; p++) {
        wchar_t c = *p;
        if (c >= L'a' && c <= L'z')
            c = (wchar_t)(c - L'a' + L'A');
        if (!Char_NameCharAllowed(c)) {
            dropped = 1;
            continue;
        }
        if (n >= CHAR_NAME_MAX) {
            dropped = 1;
            continue;
        }
        clean[n++] = c;
    }
    clean[n] = 0;
    if (wcscmp(clean, text) != 0) {
        int applying = g_char.applying;
        g_char.applying = 1;
        Rs_SetText(g_char.name, clean);
        SendMessageW(g_char.name, EM_SETSEL, (WPARAM)n, (LPARAM)n);
        g_char.applying = applying;
    }
    Rs_Free(text);
    if (dropped)
        Char_SetLabel(g_char.nameNote, L"Left out what the game cannot show - " CHAR_NAME_RULE_TEXT,
                      RS_COL_WARNING, &g_char.nameColor);
    else
        Char_SetLabel(g_char.nameNote, CHAR_NAME_RULE_TEXT, RS_COL_MUTED, &g_char.nameColor);
}

static void Char_SizeScrolled(HWND page)
{
    int before = g_char.sizeNow;
    Char_SizeSet(Char_SizePos());
    if (g_char.sizeNow != before)
        Char_Changed(page);
}

static void Char_SetPose(int pose)
{
    if (pose < 0 || pose >= CHAR_POSES)
        pose = 0;
    g_char.poseNow = pose;
    if (SendMessageW(g_char.pose, CB_GETCURSEL, 0, 0) != pose)
        SendMessageW(g_char.pose, CB_SETCURSEL, (WPARAM)pose, 0);
    RsView_SetPose(g_char.view, g_charPoseView[pose]);
}

// ---------------------------------------------------------------------------
// Buttons, dialogs, drag and drop
// ---------------------------------------------------------------------------

// Start folder of the dialogs: the field, else the folder of the last model.
static void Char_DialogStart(HWND field, wchar_t *out, int cap)
{
    Char_FieldPath(field, out, cap);
    if (!out[0])
        Rs_ConfigGet(L"char.folder", out, cap);
}

static void Char_BrowseModel(HWND page)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_DialogStart(g_char.model, start, CHAR_VAL);
    if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the character model",
                          L"PLY models (*.ply)\0*.ply\0All files\0*.*\0\0", start, pick, CHAR_VAL))
        Char_SetModel(page, pick);
}

static void Char_BrowseIcon(void)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_DialogStart(g_char.icon, start, CHAR_VAL);
    if (Rs_BrowseOpenFile(Rs_MainWindow(), L"Choose the icon picture",
                          L"PNG pictures (*.png)\0*.png\0All files\0*.*\0\0", start, pick, CHAR_VAL))
        Rs_SetText(g_char.icon, pick);      // EN_CHANGE schedules the check
}

static void Char_BrowseVoices(void)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_DialogStart(g_char.voices, start, CHAR_VAL);
    if (Rs_BrowseFolder(Rs_MainWindow(), L"Choose the voices folder", start, pick, CHAR_VAL))
        Rs_SetText(g_char.voices, pick);
}

static void Char_BrowseOut(void)
{
    wchar_t start[CHAR_VAL];
    wchar_t pick[CHAR_VAL];
    Char_FieldPath(g_char.out, start, CHAR_VAL);
    if (!start[0])
        Rs_ConfigGet(L"char.out", start, CHAR_VAL);
    if (!start[0])
        Char_Copy(start, CHAR_VAL, g_char.checkedPath);
    if (Rs_BrowseSaveFile(Rs_MainWindow(), L"Save the character as",
                          L"Characters (*.rldchar)\0*.rldchar\0\0", L"rldchar", start, pick, CHAR_VAL))
        Rs_SetText(g_char.out, pick);
}

static void Char_ShowInFolder(void)
{
    wchar_t params[CHAR_VAL + 16];
    if (!g_char.built[0])
        return;
    swprintf(params, CHAR_VAL + 16, L"/select,\"%ls\"", g_char.built);
    ShellExecuteW(NULL, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
}

// Files dropped onto the page: a .ply is the model, a .png the icon, a folder
// the voices folder.
static void Char_Drop(HWND page, HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    wchar_t path[CHAR_VAL];
    wchar_t model[CHAR_VAL];
    UINT i;

    model[0] = 0;
    for (i = 0; i < count; i++) {
        if (!DragQueryFileW(drop, i, path, CHAR_VAL))
            continue;
        if (Rs_DirExists(path))
            Rs_SetText(g_char.voices, path);
        else if (Char_EndsWith(path, L".png"))
            Rs_SetText(g_char.icon, path);
        else if (Char_EndsWith(path, L".ply"))
            Char_Copy(model, CHAR_VAL, path);
    }
    DragFinish(drop);
    if (model[0])
        Char_SetModel(page, model);
}

// Enter (IDOK): on a button press it, otherwise check immediately.
static void Char_Enter(HWND page)
{
    HWND focus = GetFocus();
    if (focus == g_char.modelBrowse || focus == g_char.iconBrowse || focus == g_char.iconClear ||
        focus == g_char.voicesBrowse || focus == g_char.voicesClear || focus == g_char.outBrowse ||
        focus == g_char.check || focus == g_char.build || focus == g_char.rawToggle || focus == g_char.show) {
        SendMessageW(focus, BM_CLICK, 0, 0);
    } else if (focus && GetParent(focus) == page) {
        Char_Check(page);
    }
}

// ---------------------------------------------------------------------------
// Report (automation "report")
// ---------------------------------------------------------------------------

static void Char_Put(FILE *f, const wchar_t *fmt, ...)
{
    wchar_t line[4096];
    va_list ap;
    char *utf8;

    va_start(ap, fmt);
    vswprintf(line, 4095, fmt, ap);
    va_end(ap);
    line[4095] = 0;
    utf8 = Rs_ToUtf8(line);
    fputs(utf8, f);
    fputs("\n", f);
    Rs_Free(utf8);
}

static void Char_PutText(FILE *f, const wchar_t *what, HWND control)
{
    wchar_t *text = Rs_GetText(control);
    Char_Put(f, L"%ls: %ls", what, text);
    Rs_Free(text);
}

static void Char_PutLabel(FILE *f, const wchar_t *what, HWND control, COLORREF color)
{
    wchar_t *text = Rs_GetText(control);
    Char_Put(f, L"%ls: %ls [%ls]", what, text, Char_ColorName(color));
    Rs_Free(text);
}

static const wchar_t *Char_EnabledWord(HWND h)
{
    return IsWindowEnabled(h) ? L"enabled" : L"greyed out";
}

static int Char_WriteReport(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"wb");
    wchar_t *text;
    int i;

    if (!f)
        return 0;
    Char_Put(f, L"Reload Studio - Character page");
    Char_PutText(f, L"model", g_char.model);
    Char_PutLabel(f, L"model info (shown)", g_char.modelInfo, g_char.infoColor);
    Char_Put(f, L"model info (full): %ls", g_char.infoFull);
    Char_PutText(f, L"name", g_char.name);
    Char_PutLabel(f, L"name note", g_char.nameNote, g_char.nameColor);
    text = Rs_GetText(g_char.cls);
    Char_Put(f, L"driving style: %ls (passed as %ls)", text, Char_ClassWord());
    Rs_Free(text);
    Char_Put(f, L"template: %ls (fixed)", CHAR_TEMPLATE);
    Char_Put(f, L"size: %d %%", g_char.sizeNow);
    if (g_char.rangeNone)
        Char_Put(f, L"size range: none fits this model");
    else if (g_char.rangeKnown)
        Char_Put(f, L"size range: %d..%d %%", g_char.rangeLo, g_char.rangeHi);
    else
        Char_Put(f, L"size range: (not reported)");
    Char_PutLabel(f, L"size note", g_char.sizeNote, g_char.sizeColor);
    Char_PutText(f, L"size hint", g_char.sizeHint);
    Char_PutText(f, L"size label", g_char.sizeCrash);
    Char_PutLabel(f, L"fit line", g_char.sizeFit, g_char.fitColor);
    Char_Put(f, L"fit (last run): %ls", g_charJob.fit[0] ? g_charJob.fit : L"(not reported)");
    if (g_charJob.fitSeen)
        Char_Put(f, L"fitted: x%ls, %ls -> %ls long, %ls -> %ls tall (game units), by the %ls", g_charJob.fitFactor,
                 g_charJob.fitLength[0], g_charJob.fitLength[1], g_charJob.fitHeight[0], g_charJob.fitHeight[1],
                 g_charJob.fitBasis);
    else
        Char_Put(f, L"fitted: (not reported)");
    Char_Put(f, L"option Reduce automatically: %ls (%ls)", Char_IsChecked(g_char.reduce) ? L"on" : L"off",
             Char_IsChecked(g_char.reduce) ? L"rldpack's default" : L"passed as --reduce off");
    Char_Put(f, L"reduce (last run): %ls", g_charJob.reduce[0] ? g_charJob.reduce : L"(not reported)");
    if (g_charJob.reducedSeen)
        Char_Put(f, L"reduced: %ld -> %ld triangles, draw memory %ld -> %ld bytes", g_charJob.reduced[0],
                 g_charJob.reduced[1], g_charJob.reduced[2], g_charJob.reduced[3]);
    else
        Char_Put(f, L"reduced: (not reported)");
    Char_Put(f, L"option Show kart wheels: %ls (%ls)", Char_IsChecked(g_char.wheels) ? L"on" : L"off",
             Char_IsChecked(g_char.wheels) ? L"rldpack's default" : L"passed as --wheels off");
    Char_Put(f, L"wheels (last run): %ls", g_charJob.wheels[0] ? g_charJob.wheels : L"(not reported)");
    Char_Put(f, L"option Repair the model: %ls (%ls)", Char_IsChecked(g_char.repair) ? L"on" : L"off",
             Char_IsChecked(g_char.repair) ? L"rldpack's default" : L"passed as --repair off");
    Char_Put(f, L"repair (last run): %ls", g_charJob.repair[0] ? g_charJob.repair : L"(not reported)");
    if (g_charJob.repairedSeen)
        Char_Put(f, L"repaired: %ld welded, %ld degenerate, %ld doubled, %ld turned outward, %ld holes closed with %ld "
                    L"triangles, %ld holes left, open edges %ld -> %ld, %ld cracks split",
                 g_charJob.repaired[0], g_charJob.repaired[1], g_charJob.repaired[2], g_charJob.repaired[3],
                 g_charJob.repaired[4], g_charJob.repaired[5], g_charJob.repaired[6], g_charJob.repaired[7],
                 g_charJob.repaired[8], g_charJob.repaired[9]);
    else
        Char_Put(f, L"repaired: (not reported)");
    Char_Put(f, L"option Draw open parts from both sides: %ls (%ls)", Char_IsChecked(g_char.openParts) ? L"on" : L"off",
             Char_IsChecked(g_char.openParts) ? L"rldpack's default" : L"passed as --open-parts one-sided");
    Char_Put(f, L"open parts (last run): %ls", g_charJob.openParts[0] ? g_charJob.openParts : L"(not reported)");
    if (g_charJob.twoSidedSeen)
        Char_Put(f, L"two-sided: %ld triangles", g_charJob.twoSided);
    else
        Char_Put(f, L"two-sided: (not reported)");
    Char_Put(f, L"option Closed hull (remesh): %ls (%ls)", Char_IsChecked(g_char.remesh) ? L"on" : L"off",
             !Char_RemeshAllowed() ? L"greyed out, not passed - needs Reduce automatically"
             : Char_RemeshOn()     ? L"passed as --remesh on"
                                   : L"rldpack's default");
    Char_Put(f, L"remesh (last run): %ls", g_charJob.remesh[0] ? g_charJob.remesh : L"(not reported)");
    if (g_charJob.remeshedSeen)
        Char_Put(f, L"remeshed: %ld -> %ld -> %ld triangles, open edges %ld -> %ld", g_charJob.remeshed[0],
                 g_charJob.remeshed[1], g_charJob.remeshed[2], g_charJob.remeshed[3], g_charJob.remeshed[4]);
    else
        Char_Put(f, L"remeshed: (not reported)");
    {
        // The note below the options, its lines joined by " | ".
        wchar_t *note = Rs_GetText(g_char.quality);
        wchar_t line[CHAR_VAL];
        const wchar_t *p;
        line[0] = 0;
        for (p = note; *p; p++) {
            wchar_t one[2] = { *p, 0 };
            if (*p == L'\r')
                continue;
            Char_Append(line, CHAR_VAL, *p == L'\n' ? L" | " : one);
        }
        Char_Put(f, L"quality note: %ls [%ls]", line[0] ? line : L"(none)", Char_ColorName(g_char.qualityColor));
        Rs_Free(note);
    }
    Char_PutText(f, L"icon", g_char.icon);
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        const struct CharImage *img = &g_char.image[i];
        if (img->px)
            Char_Put(f, L"icon %ls: %dx%d", i == CHAR_IMG_ORIGINAL ? L"original" : L"converted", img->w, img->h);
        else
            Char_Put(f, L"icon %ls: (none)", i == CHAR_IMG_ORIGINAL ? L"original" : L"converted");
    }
    Char_PutText(f, L"voices", g_char.voices);
    Char_PutText(f, L"voices note", g_char.voicesNote);
    Char_PutText(f, L"output", g_char.out);
    if (g_char.kartKnown)
        Char_Put(f, L"kart box: %d %d %d %d %d %d", g_char.kart[0], g_char.kart[1], g_char.kart[2],
                 g_char.kart[3], g_char.kart[4], g_char.kart[5]);
    else
        Char_Put(f, L"kart box: (not reported)");
    if (g_char.retailKnown)
        Char_Put(f, L"retail kart: %d %d %d %d %d %d", g_char.retail[0], g_char.retail[1], g_char.retail[2],
                 g_char.retail[3], g_char.retail[4], g_char.retail[5]);
    else
        Char_Put(f, L"retail kart: (not reported)");
    if (g_char.crashKnown)
        Char_Put(f, L"crash box: %.1f %.1f %.1f %.1f %.1f %.1f", g_char.crash[0] / 10.0, g_char.crash[1] / 10.0,
                 g_char.crash[2] / 10.0, g_char.crash[3] / 10.0, g_char.crash[4] / 10.0, g_char.crash[5] / 10.0);
    else
        Char_Put(f, L"crash box: (not reported)");
    Char_Put(f, L"preview dummy: shown, wheels %ls", Char_IsChecked(g_char.wheels) ? L"shown" : L"hidden");
    if (g_char.previewShown)
        Char_Put(f, L"preview: %d pose(s), triangles %lu/%lu/%lu", g_char.previewPoses, g_char.previewTris[0],
                 g_char.previewTris[1], g_char.previewTris[2]);
    else
        Char_Put(f, L"preview: (none)");
    Char_Put(f, L"pose: %ls", g_charPoseWords[g_char.poseNow]);
    Char_Put(f, L"turn: %d degrees", g_char.yawNow);
    Char_PutLabel(f, L"preview note", g_char.viewNote, g_char.viewColor);
    Char_PutLabel(f, L"headline", g_char.headline, g_char.headColor);
    Char_Put(f, L"button Check: %ls", Char_EnabledWord(g_char.check));
    Char_Put(f, L"button Build character: %ls", Char_EnabledWord(g_char.build));
    Char_Put(f, L"button Show in folder: %ls", Char_IsShown(g_char.show) ? L"shown" : L"hidden");
    if (g_char.built[0])
        Char_Put(f, L"last built: %ls, %lld bytes, SHA-256 %ls", g_char.built, g_char.builtBytes, g_char.builtSha);
    else
        Char_Put(f, L"last built: (none)");
    Char_Put(f, L"view: %ls", g_char.showRaw ? L"rldpack output" : L"messages");
    Char_Put(f, L"messages: %d", Rs_MsgListCount(g_char.msgs));
    Rs_MsgListWrite(g_char.msgs, f);
    Char_Put(f, L"rldpack output of the last run: %d line(s)", g_char.rawLines);
    if (g_char.rawText && g_char.rawLen) {
        char *utf8 = Rs_ToUtf8(g_char.rawText);
        const char *p;
        for (p = utf8; *p; p++)
            if (*p != '\r')
                fputc(*p, f);
        fputc('\n', f);
        Rs_Free(utf8);
    }
    fclose(f);
    return 1;
}

// ---------------------------------------------------------------------------
// Automation: the same paths as typing and clicking. No verb starts the game.
// ---------------------------------------------------------------------------

// allowNone: "none" empties the field (icon, voices, out).
static int Char_AutoText(HWND edit, const wchar_t *verb, const wchar_t *arg, int allowNone)
{
    const wchar_t *value = (allowNone && _wcsicmp(arg, L"none") == 0) ? L"" : arg;
    wchar_t *now;
    Rs_SetText(edit, value);    // EN_CHANGE schedules the check as when typing
    now = Rs_GetText(edit);
    Rs_AutoLog(L"  %ls: %ls", verb, now[0] ? now : L"(none)");
    Rs_Free(now);
    return RS_AUTO_WAIT;
}

static int Char_AutoClass(HWND page, const wchar_t *arg)
{
    int i;
    for (i = 0; i < CHAR_CLASS_COUNT; i++) {
        if (_wcsicmp(arg, g_charClasses[i].word) == 0) {
            SendMessageW(g_char.cls, CB_SETCURSEL, (WPARAM)i, 0);
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(CHAR_ID_CLASS, CBN_SELCHANGE), (LPARAM)g_char.cls);
            Rs_AutoLog(L"  class: %ls", g_charClasses[i].text);
            return RS_AUTO_WAIT;
        }
    }
    Rs_AutoLog(L"  class: '%ls' is not a driving style - use balanced, acceleration, speed or turning", arg);
    return RS_AUTO_FAIL;
}

static int Char_AutoSize(HWND page, const wchar_t *arg)
{
    wchar_t *end;
    long v = wcstol(arg, &end, 10);
    int r;
    while (*end == L' ' || *end == L'%')
        end++;
    if (end == arg || *end || v < CHAR_SIZE_MIN || v > CHAR_SIZE_MAX) {
        Rs_AutoLog(L"  size: '%ls' is not a size - use a whole number from %d to %d", arg, CHAR_SIZE_MIN,
                   CHAR_SIZE_MAX);
        return RS_AUTO_FAIL;
    }
    if (g_char.rangeKnown && (v < g_char.rangeLo || v > g_char.rangeHi)) {
        const wchar_t *why = g_char.sizeWhy[v > g_char.rangeHi ? 1 : 0];
        Rs_AutoLog(L"  size: %ld %% is locked - this model allows %d..%d %%%ls%ls", v, g_char.rangeLo,
                   g_char.rangeHi, why[0] ? L": " : L"", why);
        return RS_AUTO_FAIL;
    }
    // Slider and value as when dragging, then the check at once (not after
    // 600 ms): a following "shot" shows the preview of this size.
    Char_SizeSet((int)v);
    Rs_AutoLog(L"  size: %d %%", g_char.sizeNow);
    r = Char_Check(page);
    if (r > 0)
        return RS_AUTO_WAIT;
    if (r < 0)
        Rs_AutoLog(L"  size: %ls", g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
    return RS_AUTO_DONE;
}

// "repair on|off", "open-parts on|off" (on = from both sides), "remesh on|off",
// "reduce on|off", "wheels on|off": the check box as when clicked, then the
// check at once (as for "size"), so that a following "shot" shows its result.
static int Char_AutoOption(HWND page, HWND box, const wchar_t *verb, const wchar_t *arg)
{
    int on, r;
    if (_wcsicmp(arg, L"on") == 0)
        on = 1;
    else if (_wcsicmp(arg, L"off") == 0)
        on = 0;
    else {
        Rs_AutoLog(L"  %ls: say on or off", verb);
        return RS_AUTO_FAIL;
    }
    Char_SetChecked(box, on);
    if (box == g_char.wheels)
        RsView_SetWheels(g_char.view, on);
    Char_UpdateOptions();
    if (box == g_char.reduce)
        Char_ApplyQuality();
    Rs_AutoLog(L"  %ls: %ls", verb, on ? L"on" : L"off");
    if (box == g_char.remesh && on && !Char_RemeshAllowed())
        Rs_AutoLog(L"  %ls: greyed out - needs Reduce automatically, not passed", verb);
    r = Char_Check(page);
    if (r > 0)
        return RS_AUTO_WAIT;
    if (r < 0)
        Rs_AutoLog(L"  %ls: %ls", verb,
                   g_char.jobId ? L"checked after the running build" : L"not checked - no model is chosen");
    return RS_AUTO_DONE;
}

static int Char_AutoPose(const wchar_t *arg)
{
    int i;
    for (i = 0; i < CHAR_POSES; i++) {
        if (_wcsicmp(arg, g_charPoseWords[i]) == 0) {
            Char_SetPose(i);
            UpdateWindow(g_char.view);      // painted before a following "shot"
            Rs_AutoLog(L"  pose: %ls", g_charPoseTexts[i]);
            return RS_AUTO_DONE;
        }
    }
    Rs_AutoLog(L"  pose: say neutral, left or right");
    return RS_AUTO_FAIL;
}

static int Char_AutoTurn(const wchar_t *arg)
{
    wchar_t *end;
    long v = wcstol(arg, &end, 10);
    while (*end == L' ')
        end++;
    if (end == arg || *end) {
        Rs_AutoLog(L"  turn: '%ls' is not a whole number of degrees", arg);
        return RS_AUTO_FAIL;
    }
    v %= 360;
    if (v < 0)
        v += 360;
    RsView_SetYaw(g_char.view, (int)v);
    g_char.yawNow = RsView_GetYaw(g_char.view);
    UpdateWindow(g_char.view);      // painted before a following "shot"
    Rs_AutoLog(L"  turn: %d degrees", g_char.yawNow);
    return RS_AUTO_DONE;
}

// ---------------------------------------------------------------------------
// Callbacks of the page
// ---------------------------------------------------------------------------

static void Char_Create(HWND page)
{
    WNDCLASSEXW wc;
    HINSTANCE inst = GetModuleHandleW(NULL);
    int i;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpfnWndProc = Char_ImageProc;
    wc.lpszClassName = CHAR_IMAGE_CLASS;
    RegisterClassExW(&wc);
    RsView_Register(inst);

    g_char.modelLabel = Rs_Label(page, CHAR_ID_MODEL_LABEL, L"Model (PLY)", RS_FONT_BOLD);
    g_char.model = Rs_Edit(page, CHAR_ID_MODEL, L"", 0);
    g_char.modelBrowse = Rs_Button(page, CHAR_ID_MODEL_BROWSE, L"Browse...");
    g_char.modelInfo = Rs_Label(page, CHAR_ID_MODEL_INFO, L"-", RS_FONT_SMALL);   // wraps (Char_Layout)

    g_char.nameLabel = Rs_Label(page, CHAR_ID_NAME_LABEL, L"Name", RS_FONT_BOLD);
    g_char.name = Rs_Edit(page, CHAR_ID_NAME, L"", ES_UPPERCASE);
    SendMessageW(g_char.name, EM_LIMITTEXT, CHAR_NAME_MAX, 0);
    g_char.nameNote = Rs_Label(page, CHAR_ID_NAME_NOTE, L"", RS_FONT_SMALL);
    Char_Ellipsis(g_char.nameNote);

    g_char.classLabel = Rs_Label(page, CHAR_ID_CLASS_LABEL, L"Driving style", RS_FONT_BOLD);
    g_char.cls = Rs_Combo(page, CHAR_ID_CLASS);
    for (i = 0; i < CHAR_CLASS_COUNT; i++)
        SendMessageW(g_char.cls, CB_ADDSTRING, 0, (LPARAM)g_charClasses[i].text);
    SendMessageW(g_char.cls, CB_SETCURSEL, 0, 0);
    g_char.classHelp = Rs_Label(page, CHAR_ID_CLASS_HELP,
                                L"How the kart drives: speed, acceleration and turning as the drivers named.",
                                RS_FONT_SMALL);
    Rs_SetTextColor(g_char.classHelp, RS_COL_MUTED);
    Char_Ellipsis(g_char.classHelp);

    g_char.sizeLabel = Rs_Label(page, CHAR_ID_SIZE_LABEL, L"Size", RS_FONT_BOLD);
    g_char.size = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS | TBS_ENABLESELRANGE,
                                  0, 0, 10, 10, page, (HMENU)(INT_PTR)CHAR_ID_SIZE, inst, NULL);
    SendMessageW(g_char.size, TBM_SETRANGE, FALSE, MAKELPARAM(CHAR_SIZE_MIN, CHAR_SIZE_MAX));
    SendMessageW(g_char.size, TBM_SETLINESIZE, 0, 1);
    SendMessageW(g_char.size, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(g_char.size, TBM_SETPOS, TRUE, CHAR_SIZE_DEFAULT);
    g_char.sizeValue = Rs_Label(page, CHAR_ID_SIZE_VALUE, L"", RS_FONT_BODY);
    g_char.sizeNote = Rs_Label(page, CHAR_ID_SIZE_NOTE, L"", RS_FONT_SMALL);
    Char_Ellipsis(g_char.sizeNote);
    g_char.sizeHint = Rs_Label(page, CHAR_ID_SIZE_HINT, CHAR_SIZE_HINT_TEXT, RS_FONT_SMALL);
    Rs_SetTextColor(g_char.sizeHint, RS_COL_MUTED);
    Char_Ellipsis(g_char.sizeHint);
    g_char.sizeCrash = Rs_Label(page, CHAR_ID_SIZE_CRASH, L"100 % = Crash size", RS_FONT_SMALL);
    Rs_SetTextColor(g_char.sizeCrash, RS_COL_MUTED);
    Char_Ellipsis(g_char.sizeCrash);
    g_char.sizeFit = Rs_Label(page, CHAR_ID_SIZE_FIT, CHAR_FIT_WAIT_TEXT, RS_FONT_SMALL);
    Char_Ellipsis(g_char.sizeFit);
    g_char.fitColor = RS_COL_MUTED;
    Rs_SetTextColor(g_char.sizeFit, g_char.fitColor);
    g_char.sizeNow = CHAR_SIZE_DEFAULT;

    g_char.optionsLabel = Rs_Label(page, CHAR_ID_OPTIONS_LABEL, L"Options", RS_FONT_BOLD);
    g_char.repair = Rs_Check(page, CHAR_ID_REPAIR, L"Repair the model");
    g_char.openParts = Rs_Check(page, CHAR_ID_OPEN_PARTS, L"Draw open parts from both sides");
    g_char.remesh = Rs_Check(page, CHAR_ID_REMESH, L"Closed hull (remesh)");
    g_char.reduce = Rs_Check(page, CHAR_ID_REDUCE, L"Reduce automatically");
    g_char.wheels = Rs_Check(page, CHAR_ID_WHEELS, L"Show kart wheels");
    Char_SetChecked(g_char.repair, 1);
    Char_SetChecked(g_char.openParts, 1);
    Char_SetChecked(g_char.remesh, 0);      // only on request
    Char_SetChecked(g_char.reduce, 1);
    Char_SetChecked(g_char.wheels, 1);
    Char_UpdateOptions();
    g_char.quality = Rs_Label(page, CHAR_ID_QUALITY, L"", RS_FONT_SMALL);   // wraps (Char_Layout)
    g_char.qualityColor = RS_COL_TEXT;

    g_char.iconLabel = Rs_Label(page, CHAR_ID_ICON_LABEL, L"Icon (PNG)", RS_FONT_BOLD);
    g_char.icon = Rs_Edit(page, CHAR_ID_ICON, L"", 0);
    SendMessageW(g_char.icon, EM_SETCUEBANNER, FALSE, (LPARAM)L"Optional - without it the game shows the template's icon");
    g_char.iconBrowse = Rs_Button(page, CHAR_ID_ICON_BROWSE, L"Browse...");
    g_char.iconClear = Rs_Button(page, CHAR_ID_ICON_CLEAR, L"Clear");
    g_char.iconCaption[CHAR_IMG_ORIGINAL] = Rs_Label(page, CHAR_ID_ICON_CAPTION, L"Your picture", RS_FONT_SMALL);
    g_char.iconCaption[CHAR_IMG_ICON] = Rs_Label(page, CHAR_ID_ICON_CAPTION + 1, L"In the game (44 x 26, 16 colours)",
                                                 RS_FONT_SMALL);
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        Rs_SetTextColor(g_char.iconCaption[i], RS_COL_MUTED);
        g_char.iconImage[i] = Char_ImageBox(page, CHAR_ID_ICON_IMAGE + i, &g_char.image[i]);
    }

    g_char.voicesLabel = Rs_Label(page, CHAR_ID_VOICES_LABEL, L"Voices", RS_FONT_BOLD);
    g_char.voices = Rs_Edit(page, CHAR_ID_VOICES, L"", 0);
    SendMessageW(g_char.voices, EM_SETCUEBANNER, FALSE, (LPARAM)L"Optional - a folder with WAV or VAG files");
    g_char.voicesBrowse = Rs_Button(page, CHAR_ID_VOICES_BROWSE, L"Browse...");
    g_char.voicesClear = Rs_Button(page, CHAR_ID_VOICES_CLEAR, L"Clear");
    g_char.voicesNote = Rs_Label(page, CHAR_ID_VOICES_NOTE, CHAR_VOICES_TEXT, RS_FONT_SMALL);
    Rs_SetTextColor(g_char.voicesNote, RS_COL_MUTED);
    Char_Ellipsis(g_char.voicesNote);

    g_char.view = CreateWindowExW(0, RS_VIEW_CLASS, L"No model yet", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, page,
                                  (HMENU)(INT_PTR)CHAR_ID_VIEW, inst, NULL);
    g_char.yawNow = RS_VIEW_DEFAULT_YAW;
    RsView_SetYaw(g_char.view, g_char.yawNow);
    g_char.pose = Rs_Combo(page, CHAR_ID_POSE);
    for (i = 0; i < CHAR_POSES; i++)
        SendMessageW(g_char.pose, CB_ADDSTRING, 0, (LPARAM)g_charPoseTexts[i]);
    SendMessageW(g_char.pose, CB_SETCURSEL, 0, 0);
    g_char.viewNote = Rs_Label(page, CHAR_ID_VIEW_NOTE, L"", RS_FONT_SMALL);
    Char_Ellipsis(g_char.viewNote);

    g_char.rawToggle = Rs_Button(page, CHAR_ID_RAW_TOGGLE, L"Show rldpack output");
    g_char.outLabel = Rs_Label(page, CHAR_ID_OUT_LABEL, L"Output", RS_FONT_BOLD);
    g_char.out = Rs_Edit(page, CHAR_ID_OUT, L"", 0);
    SendMessageW(g_char.out, EM_SETCUEBANNER, FALSE, (LPARAM)L"Next to the model (.rldchar)");
    g_char.outBrowse = Rs_Button(page, CHAR_ID_OUT_BROWSE, L"Browse...");
    g_char.check = Rs_Button(page, CHAR_ID_CHECK, L"Check");
    g_char.build = Rs_PrimaryButton(page, CHAR_ID_BUILD, L"Build character");
    g_char.headline = Rs_Label(page, CHAR_ID_HEADLINE, L"", RS_FONT_BOLD);
    Char_Ellipsis(g_char.headline);
    g_char.msgs = Rs_MsgList(page, CHAR_ID_MESSAGES);
    g_char.raw = Rs_Edit(page, CHAR_ID_RAW, L"",
                         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
    SendMessageW(g_char.raw, WM_SETFONT, (WPARAM)Rs_Font(RS_FONT_MONO), FALSE);
    SendMessageW(g_char.raw, EM_LIMITTEXT, 0, 0);
    ShowWindow(g_char.raw, SW_HIDE);
    g_char.show = Rs_Button(page, CHAR_ID_SHOW, L"Show in folder");
    ShowWindow(g_char.show, SW_HIDE);

    DragAcceptFiles(page, TRUE);
    Char_TempSweep();

    g_char.applying = 1;
    Char_NameFilter();
    Char_SizeSet(CHAR_SIZE_DEFAULT);
    Char_SizeForget();
    g_char.applying = 0;
    Char_ApplyIcon(0);
    Char_NoModel();
}

// Label left, field right of it.
static void Char_PlaceField(HWND label, HWND field, int x, int labelW, int y, int fieldW)
{
    MoveWindow(label, x, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(field, x + labelW + Rs_Px(8), y, fieldW, Rs_Px(28), TRUE);
}

static void Char_Layout(HWND page, int w, int h)
{
    int left = Rs_Px(32), right = w - Rs_Px(32);
    int top = Rs_PageTop(), bottom = h - Rs_Px(24);
    int gap = Rs_Px(16);
    int leftW = (right - left - gap) / 2;
    int browseW = Rs_Px(100), clearW = Rs_Px(72), labelW = Rs_Px(112);
    int built = g_char.built[0] != 0;
    RECT card, in;
    int x, y, width, fieldW, listBottom, factor, boxW, boxH, i, infoH, qualityH;
    HWND options[5];

    Rs_CardClear(page);

    // Top left: model, name, driving style, size
    card.left = left;
    card.top = top;
    card.right = left + leftW;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    x = in.left + labelW + Rs_Px(8);
    fieldW = in.right - x;
    y = in.top;
    Char_PlaceField(g_char.modelLabel, g_char.model, in.left, labelW, y + Rs_Px(2), fieldW - browseW - Rs_Px(8));
    MoveWindow(g_char.modelBrowse, in.right - browseW, y, browseW, Rs_Px(32), TRUE);
    y += Rs_Px(34);
    // One line, or up to CHAR_INFO_LINES when the text needs them.
    Char_FitLines(g_char.modelInfo, g_char.infoFull, fieldW, CHAR_INFO_LINES);
    infoH = Char_TextHeight(g_char.modelInfo, fieldW, CHAR_INFO_LINES);
    MoveWindow(g_char.modelInfo, x, y, fieldW, infoH > Rs_Px(18) ? infoH : Rs_Px(18), TRUE);
    y += Rs_Px(26) + (infoH > Rs_Px(18) ? infoH - Rs_Px(18) : 0);
    Char_PlaceField(g_char.nameLabel, g_char.name, in.left, labelW, y, Rs_Px(220) < fieldW ? Rs_Px(220) : fieldW);
    y += Rs_Px(30);
    MoveWindow(g_char.nameNote, x, y, fieldW, Rs_Px(18), TRUE);
    y += Rs_Px(26);
    MoveWindow(g_char.classLabel, in.left, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.cls, x, y, fieldW, Rs_Px(300), TRUE);
    y += Rs_Px(30);
    MoveWindow(g_char.classHelp, x, y, fieldW, Rs_Px(18), TRUE);
    y += Rs_Px(26);
    MoveWindow(g_char.sizeLabel, in.left, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(g_char.size, x - Rs_Px(4), y, fieldW - Rs_Px(64), Rs_Px(30), TRUE);
    MoveWindow(g_char.sizeValue, in.right - Rs_Px(60), y + Rs_Px(4), Rs_Px(60), Rs_Px(20), TRUE);
    y += Rs_Px(32);
    // What 100 % is: in the label column under "Size", the fit beside it.
    MoveWindow(g_char.sizeCrash, in.left, y, labelW, Rs_Px(18), TRUE);
    MoveWindow(g_char.sizeFit, x, y, fieldW, Rs_Px(18), TRUE);
    y += Rs_Px(20);
    MoveWindow(g_char.sizeNote, x, y, fieldW, Rs_Px(18), TRUE);
    y += Rs_Px(20);
    MoveWindow(g_char.sizeHint, x, y, fieldW, Rs_Px(18), TRUE);
    y += Rs_Px(26);
    // The check boxes left to right, a new row where the next one does not fit.
    options[0] = g_char.repair;
    options[1] = g_char.openParts;
    options[2] = g_char.remesh;
    options[3] = g_char.reduce;
    options[4] = g_char.wheels;
    MoveWindow(g_char.optionsLabel, in.left, y + Rs_Px(2), labelW, Rs_Px(20), TRUE);
    {
        int cx = x;
        for (i = 0; i < 5; i++) {
            int bw = Char_CheckWidth(options[i]);
            if (bw > fieldW)
                bw = fieldW;
            if (cx > x && cx + bw > x + fieldW) {
                cx = x;
                y += Rs_Px(26);
            }
            MoveWindow(options[i], cx, y, bw, Rs_Px(24), TRUE);
            cx += bw + Rs_Px(12);
        }
    }
    y += Rs_Px(24);
    // What the repair, the open parts and the remesh did (Char_ApplyQuality).
    MoveWindow(g_char.quality, x, y + Rs_Px(4), fieldW, Rs_Px(18), FALSE);
    qualityH = Char_QualityHeight(fieldW);
    ShowWindow(g_char.quality, qualityH > 0 ? SW_SHOW : SW_HIDE);
    if (qualityH > 0) {
        MoveWindow(g_char.quality, x, y + Rs_Px(4), fieldW, qualityH, TRUE);
        y += Rs_Px(4) + qualityH;
    }
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Character");

    // Bottom left: build; the toggle is in the title line of the card
    card.top = card.bottom + gap;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    MoveWindow(g_char.rawToggle, in.right - Rs_Px(168), card.top + Rs_Px(10), Rs_Px(168), Rs_Px(30), TRUE);
    y = in.top;
    MoveWindow(g_char.outLabel, in.left, y + Rs_Px(6), Rs_Px(64), Rs_Px(20), TRUE);
    MoveWindow(g_char.out, in.left + Rs_Px(72), y + Rs_Px(2), width - Rs_Px(72) - browseW - Rs_Px(8),
               Rs_Px(28), TRUE);
    MoveWindow(g_char.outBrowse, in.right - browseW, y, browseW, Rs_Px(32), TRUE);
    y += Rs_Px(44);
    MoveWindow(g_char.check, in.left, y, Rs_Px(96), Rs_Px(32), TRUE);
    MoveWindow(g_char.build, in.left + Rs_Px(104), y, Rs_Px(156), Rs_Px(32), TRUE);
    MoveWindow(g_char.show, in.right - Rs_Px(136), y, Rs_Px(136), Rs_Px(32), TRUE);
    ShowWindow(g_char.show, built ? SW_SHOW : SW_HIDE);
    y += Rs_Px(44);
    MoveWindow(g_char.headline, in.left, y, width, Rs_Px(22), TRUE);
    y += Rs_Px(28);
    listBottom = in.bottom;
    if (listBottom < y + Rs_Px(40))
        listBottom = y + Rs_Px(40);
    MoveWindow(g_char.msgs, in.left, y, width, listBottom - y, TRUE);
    MoveWindow(g_char.raw, in.left, y, width, listBottom - y, TRUE);
    Rs_CardAdd(page, &card, L"Build");

    // Top right: icon (picture and conversion side by side, enlarged), voices
    card.left = left + leftW + gap;
    card.right = right;
    card.top = top;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    x = in.left + labelW + Rs_Px(8);
    fieldW = in.right - x;
    y = in.top;
    Char_PlaceField(g_char.iconLabel, g_char.icon, in.left, labelW, y + Rs_Px(2),
                    fieldW - browseW - clearW - Rs_Px(16));
    MoveWindow(g_char.iconBrowse, in.right - browseW - clearW - Rs_Px(8), y, browseW, Rs_Px(32), TRUE);
    MoveWindow(g_char.iconClear, in.right - clearW, y, clearW, Rs_Px(32), TRUE);
    y += Rs_Px(40);
    // Whole steps of the 44 x 26 icon, at most 4; the picture box beside it as large.
    for (factor = 4; factor > 1 && 2 * Rs_Px(44 * factor + 8) + gap > width; factor--)
        ;
    boxW = Rs_Px(44 * factor + 8);
    boxH = Rs_Px(26 * factor + 8);
    for (i = 0; i < CHAR_IMG_COUNT; i++) {
        int bx = in.left + i * (boxW + gap);
        MoveWindow(g_char.iconCaption[i], bx, y, boxW + gap, Rs_Px(18), TRUE);
        MoveWindow(g_char.iconImage[i], bx, y + Rs_Px(20), boxW, boxH, TRUE);
    }
    y += Rs_Px(20) + boxH + Rs_Px(12);
    Char_PlaceField(g_char.voicesLabel, g_char.voices, in.left, labelW, y + Rs_Px(2),
                    fieldW - browseW - clearW - Rs_Px(16));
    MoveWindow(g_char.voicesBrowse, in.right - browseW - clearW - Rs_Px(8), y, browseW, Rs_Px(32), TRUE);
    MoveWindow(g_char.voicesClear, in.right - clearW, y, clearW, Rs_Px(32), TRUE);
    y += Rs_Px(36);
    MoveWindow(g_char.voicesNote, x, y, fieldW, Rs_Px(18), TRUE);
    y += Rs_Px(18);
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Icon and voices");

    // Bottom right: 3D preview; the pose choice is in the title line of the card
    card.top = card.bottom + gap;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    MoveWindow(g_char.pose, in.right - Rs_Px(168), card.top + Rs_Px(10), Rs_Px(168), Rs_Px(200), TRUE);
    y = in.top;
    listBottom = in.bottom - Rs_Px(24);
    if (listBottom < y + Rs_Px(80))
        listBottom = y + Rs_Px(80);
    MoveWindow(g_char.view, in.left, y, width, listBottom - y, TRUE);
    MoveWindow(g_char.viewNote, in.left, listBottom + Rs_Px(6), width, Rs_Px(18), TRUE);
    Rs_CardAdd(page, &card, L"Preview");

    // After a DPI change the shell sets the base font; the raw output
    // stays in a fixed-width font, though.
    if ((HFONT)SendMessageW(g_char.raw, WM_GETFONT, 0, 0) != Rs_Font(RS_FONT_MONO))
        SendMessageW(g_char.raw, WM_SETFONT, (WPARAM)Rs_Font(RS_FONT_MONO), TRUE);
}

static LRESULT Char_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);
    (void)lParam;

    switch (id) {
    case IDOK:
        Char_Enter(page);
        break;
    case IDCANCEL:
        break;
    case CHAR_ID_MODEL:
        if (code == EN_CHANGE)
            Char_ModelChanged(page);
        break;
    case CHAR_ID_NAME:
        if (code == EN_CHANGE && !g_char.applying) {
            Char_NameFilter();
            Char_Changed(page);
        }
        break;
    case CHAR_ID_ICON:
    case CHAR_ID_VOICES:
    case CHAR_ID_OUT:
        if (code == EN_CHANGE)
            Char_Changed(page);
        break;
    case CHAR_ID_CLASS:
        if (code == CBN_SELCHANGE)
            Char_Changed(page);
        break;
    case CHAR_ID_REDUCE:
        if (code == BN_CLICKED) {
            Char_UpdateOptions();
            Char_ApplyQuality();
            Char_Changed(page);
        }
        break;
    case CHAR_ID_REPAIR:
    case CHAR_ID_OPEN_PARTS:
    case CHAR_ID_REMESH:
        if (code == BN_CLICKED)
            Char_Changed(page);
        break;
    case CHAR_ID_WHEELS:
        // The preview follows at once; the check follows as for every field.
        if (code == BN_CLICKED) {
            RsView_SetWheels(g_char.view, Char_IsChecked(g_char.wheels));
            Char_Changed(page);
        }
        break;
    case CHAR_ID_VIEW:
        if (code == RS_VIEW_N_YAW)
            g_char.yawNow = RsView_GetYaw(g_char.view);
        break;
    case CHAR_ID_POSE:
        if (code == CBN_SELCHANGE) {
            LRESULT sel = SendMessageW(g_char.pose, CB_GETCURSEL, 0, 0);
            Char_SetPose(sel < 0 ? 0 : (int)sel);
        }
        break;
    case CHAR_ID_MODEL_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseModel(page);
        break;
    case CHAR_ID_ICON_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseIcon();
        break;
    case CHAR_ID_ICON_CLEAR:
        if (code == BN_CLICKED)
            Rs_SetText(g_char.icon, L"");
        break;
    case CHAR_ID_VOICES_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseVoices();
        break;
    case CHAR_ID_VOICES_CLEAR:
        if (code == BN_CLICKED)
            Rs_SetText(g_char.voices, L"");
        break;
    case CHAR_ID_OUT_BROWSE:
        if (code == BN_CLICKED)
            Char_BrowseOut();
        break;
    case CHAR_ID_CHECK:
        if (code == BN_CLICKED)
            Char_Check(page);
        break;
    case CHAR_ID_BUILD:
        if (code == BN_CLICKED)
            Char_Build(page);
        break;
    case CHAR_ID_RAW_TOGGLE:
        if (code == BN_CLICKED) {
            g_char.showRaw = !g_char.showRaw;
            Char_ShowView();
        }
        break;
    case CHAR_ID_SHOW:
        if (code == BN_CLICKED)
            Char_ShowInFolder();
        break;
    }
    return 0;
}

static LRESULT Char_Notify(HWND page, NMHDR *hdr)
{
    (void)page;
    (void)hdr;
    return 0;
}

static LRESULT Char_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    switch (msg) {
    case RS_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        if (line && g_char.jobId && (int)wParam == g_char.jobId) {
            Char_RawAppend(line);
            Char_ParseLine(line);
        }
        Rs_Free(line);
        *handled = 1;
        return 0;
    }
    case RS_WM_JOB_DONE:
        if (g_char.jobId && (int)wParam == g_char.jobId)
            Char_JobDone(page, (int)lParam);
        else
            Char_PendingDone((int)wParam);
        *handled = 1;
        return 0;
    case WM_TIMER:
        if (wParam != CHAR_TIMER_CHECK)
            return 0;
        *handled = 1;
        // Building is running: the timer keeps running and asks again later
        if (g_char.jobId && g_char.jobKind == CHAR_JOB_BUILD)
            return 0;
        Char_Check(page);
        return 0;
    case WM_HSCROLL:
        if ((HWND)lParam == g_char.size) {
            Char_SizeScrolled(page);
            *handled = 1;
        }
        return 0;
    case WM_DROPFILES:
        Char_Drop(page, (HDROP)wParam);
        *handled = 1;
        return 0;
    case WM_DESTROY: {
        int i;
        Char_Abandon();
        for (i = 0; i < CHAR_PENDING; i++)
            Char_TempDelete(g_char.pending[i].seq);
        Char_ImagesClear();
        return 0;
    }
    }
    return 0;
}

static int Char_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    int r;

    if (wcscmp(verb, L"model") == 0) {
        if (!arg[0] || _wcsicmp(arg, L"none") == 0) {
            Char_SetModel(page, L"");
            Rs_AutoLog(L"  model: (none)");
            return RS_AUTO_DONE;
        }
        Rs_AutoLog(L"  model: %ls", arg);
        if (Char_SetModel(page, arg))
            return RS_AUTO_WAIT;
        Rs_AutoLog(L"  model: rldpack could not be started");
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"name") == 0)
        return Char_AutoText(g_char.name, verb, arg, 0);
    if (wcscmp(verb, L"class") == 0)
        return Char_AutoClass(page, arg);
    if (wcscmp(verb, L"size") == 0)
        return Char_AutoSize(page, arg);
    if (wcscmp(verb, L"icon") == 0)
        return Char_AutoText(g_char.icon, verb, arg, 1);
    if (wcscmp(verb, L"voices") == 0)
        return Char_AutoText(g_char.voices, verb, arg, 1);
    if (wcscmp(verb, L"out") == 0)
        return Char_AutoText(g_char.out, verb, arg, 1);
    if (wcscmp(verb, L"check") == 0) {
        r = Char_Check(page);
        if (r > 0)
            return RS_AUTO_WAIT;
        if (r == 0)
            return RS_AUTO_DONE;
        Rs_AutoLog(L"  check: %ls", g_char.jobId ? L"a build is running" : L"no model is chosen");
        return RS_AUTO_FAIL;
    }
    // Like the button "Build character". Only rldpack runs; the game does not.
    if (wcscmp(verb, L"build") == 0) {
        if (!Char_CanBuild()) {
            Rs_AutoLog(L"  build: not possible - the last check did not pass");
            return RS_AUTO_FAIL;
        }
        r = Char_Build(page);
        if (r > 0)
            return RS_AUTO_WAIT;
        Rs_AutoLog(L"  build: not started");
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"repair") == 0)
        return Char_AutoOption(page, g_char.repair, verb, arg);
    if (wcscmp(verb, L"open-parts") == 0)
        return Char_AutoOption(page, g_char.openParts, verb, arg);
    if (wcscmp(verb, L"remesh") == 0)
        return Char_AutoOption(page, g_char.remesh, verb, arg);
    if (wcscmp(verb, L"reduce") == 0)
        return Char_AutoOption(page, g_char.reduce, verb, arg);
    if (wcscmp(verb, L"wheels") == 0)
        return Char_AutoOption(page, g_char.wheels, verb, arg);
    if (wcscmp(verb, L"pose") == 0)
        return Char_AutoPose(arg);
    if (wcscmp(verb, L"turn") == 0)
        return Char_AutoTurn(arg);
    if (wcscmp(verb, L"report") == 0) {
        if (!arg[0] || !Char_WriteReport(arg)) {
            Rs_AutoLog(L"  report: could not write '%ls'", arg);
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  report: %ls", arg);
        return RS_AUTO_DONE;
    }
    return RS_AUTO_UNKNOWN;
}

static int Char_Busy(HWND page)
{
    (void)page;
    return g_char.jobId != 0 || g_char.timer;
}

const struct RsPageDef g_rsCharPage = {
    L"Character",
    L"Build a character",
    L"Pick a PLY model of driver, steering wheel and kart. rldpack converts and checks it and builds the .rldchar.",
    Char_Create,
    Char_Layout,
    Char_Command,
    Char_Notify,
    Char_Message,
    Char_Automate,
    Char_Busy
};
