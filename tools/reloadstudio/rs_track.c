// rs_track.c - page "Track": build a track folder as .rldtrack
//
// The page checks nothing itself. It starts rldpack make (with --check to
// check, without it to build), reads its machine lines (protocol in
// reloadstudio.h) and shows them.
//
// Flow: loading a folder is make --check without switches; rldpack then reports the
// values from track.txt or its defaults. The page enters them into the fields
// and checks immediately with all switches. After that every change to
// a field starts a new check after 600 ms. A new check invalidates the
// running one; its lines are only freed.
//
// Split: on the left folder/files/name and sound, on the right modes and build. The
// "Reverb" choice needs the full card width, that is why its
// labels stand above the fields and the card is on the left.

#include "reloadstudio.h"
#include <shellapi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TRACK_TIMER_CHECK  1
#define TRACK_CHECK_DELAY  600
#define TRACK_TIMER_PREVIEW 2         // emergency brake: preview game hangs (invisible)
#define TRACK_PREVIEW_LIMIT 120000    // ms, after that the game is ended
#define TRACK_VAL          1024   // length of a value or path
#define TRACK_FILES        4
#define TRACK_MODES        5
#define TRACK_LEV_FACTS    16
#define TRACK_MAX_ARGS     40
#define TRACK_CUSTOM_ITEM  1000   // item data of "Custom (...)"

// Controls
#define TRACK_ID_FOLDER         100
#define TRACK_ID_FOLDER_BROWSE  101
#define TRACK_ID_FILE_LABEL     102   // 102..105
#define TRACK_ID_FILE_VALUE     106   // 106..109
#define TRACK_ID_NAME_LABEL     110
#define TRACK_ID_NAME           111
#define TRACK_ID_AUTHOR_LABEL   112
#define TRACK_ID_AUTHOR         113
#define TRACK_ID_VERSION_LABEL  114
#define TRACK_ID_VERSION        115
#define TRACK_ID_COMBO_LABEL    116   // 116..118
#define TRACK_ID_COMBO          119   // 119..121: reverb, bots, ambient
#define TRACK_ID_AMBIENT_NOTE   122
#define TRACK_ID_MUSIC          123
#define TRACK_ID_MODE           130   // 130..134
#define TRACK_ID_MODE_NOTE      140   // 140..144
#define TRACK_ID_VIEW           150
#define TRACK_ID_OUT_LABEL      151
#define TRACK_ID_OUT            152
#define TRACK_ID_OUT_BROWSE     153
#define TRACK_ID_CHECK          154
#define TRACK_ID_BUILD          155
#define TRACK_ID_HEADLINE       156
#define TRACK_ID_MESSAGES       157
#define TRACK_ID_RAW            158
#define TRACK_ID_TEST           159
#define TRACK_ID_SHOW           160
#define TRACK_ID_ADVANCED       161
#define TRACK_ID_ADV_HELP       162   // 162..164
#define TRACK_ID_ADV_SUMMARY    165
#define TRACK_ID_MAP_LABEL      166
#define TRACK_ID_MAP_VALUE      167
#define TRACK_ID_BOT_LABEL      168
#define TRACK_ID_BOT_VALUE      169

#define TRACK_CMD_CAP      8192   // command line for the game

enum { TRACK_JOB_NONE = 0, TRACK_JOB_LOAD, TRACK_JOB_CHECK, TRACK_JOB_BUILD, TRACK_JOB_PREVIEW };

// Final line of the game with --record-preview; SKIPPED = not started at all
// after the build (game not checked, no game data ...)
enum { TRACK_PREVIEW_NONE = 0, TRACK_PREVIEW_WRITTEN, TRACK_PREVIEW_FAILED, TRACK_PREVIEW_SKIPPED };
// How far the game got with --record-preview, from its lines. If it ends without
// a final line (ended early, crash, older game), the stage says
// what goes into the heading.
enum {
    TRACK_STAGE_STARTED = 0,    // game running, track not chosen yet
    TRACK_STAGE_LOADING,        // "--autoload-track '<file>' jumps to level"
    TRACK_STAGE_CAMERA,         // "<name>: recording with the AI driver ..." / "... with the path camera ..."
    TRACK_STAGE_RECORDING       // "recording from a ..." / "<n> of <m> frames recorded"
};
#define TRACK_PREVIEW_FRAMES 150    // NATIVE_PREVIEW_FRAMES in the game

enum {
    TRACK_V_NAME = 0, TRACK_V_AUTHOR, TRACK_V_VERSION, TRACK_V_MODES, TRACK_V_REVERB,
    TRACK_V_BOTS, TRACK_V_AMBIENT, TRACK_V_MUSIC, TRACK_V_OUT, TRACK_V_COUNT
};

enum { TRACK_C_REVERB = 0, TRACK_C_BOTS, TRACK_C_AMBIENT, TRACK_C_COUNT };

enum { TRACK_F_LEV = 0, TRACK_F_VRM, TRACK_F_SCA, TRACK_F_TXT };

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

static const wchar_t *const g_trackValueKeys[TRACK_V_COUNT] = {
    L"name", L"author", L"track_version", L"modes", L"reverb", L"bots", L"ambient", L"music", L"out"
};

static const wchar_t *const g_trackFileKinds[TRACK_FILES] = { L"lev", L"vrm", L"sca", L"tracktxt" };
static const wchar_t *const g_trackFileLabels[TRACK_FILES] = {
    L"Geometry (.lev)", L"Textures (.vrm)", L"Music (.sca/.sndb)", L"Settings (track.txt)"
};

static const wchar_t *const g_trackModeWords[TRACK_MODES] = {
    L"race", L"time", L"ctr", L"crystal", L"battle"
};
static const wchar_t *const g_trackModeLabels[TRACK_MODES] = {
    L"Race", L"Time Trial", L"CTR Challenge", L"Crystal Challenge", L"Battle"
};

struct TrackChoice {
    const wchar_t *text;
    const wchar_t *value;   // this is how the value goes to rldpack
};

static const struct TrackChoice g_trackReverb[] = {
    { L"Game default (step 2)", L"default" },
    { L"Off", L"off" },
    { L"Step 0 - like Blizzard Bluff, Hot Air Skyway", L"0" },
    { L"Step 1 - like Crash Cove, Tiger Temple, Mystery Caves", L"1" },
    { L"Step 2 - like Dingo Canyon", L"2" },
    { L"Step 3 - like Roo's Tubes, Sewer Speedway, Oxide Station", L"3" },
    { L"Step 4 - like Polar Pass, Coco Park, Tiny Arena", L"4" },
};

static const struct TrackChoice g_trackBots[] = {
    { L"Game default (like Dingo Canyon)", L"default" },
    { L"Like Dingo Canyon", L"0" },
    { L"Like Dragon Mines", L"1" },
    { L"Like Blizzard Bluff", L"2" },
    { L"Like Crash Cove", L"3" },
    { L"Like Tiger Temple", L"4" },
    { L"Like Papu's Pyramid", L"5" },
    { L"Like Roo's Tubes", L"6" },
    { L"Like Hot Air Skyway", L"7" },
    { L"Like Sewer Speedway", L"8" },
    { L"Like Mystery Caves", L"9" },
    { L"Like Cortex Castle", L"10" },
    { L"Like N. Gin Labs", L"11" },
    { L"Like Polar Pass", L"12" },
    { L"Like Oxide Station", L"13" },
    { L"Like Coco Park", L"14" },
    { L"Like Tiny Arena", L"15" },
    { L"Like Slide Coliseum", L"16" },
    { L"Like Turbo Track", L"17" },
};

static const struct TrackChoice g_trackAmbient[] = {
    { L"None", L"default" },
    { L"Like Dingo Canyon (0x83)", L"0x83" },
    { L"Like Dragon Mines (0x84)", L"0x84" },
    { L"Like Crash Cove (0x83, 0x85)", L"0x83,0x85" },
    { L"Like Hot Air Skyway (0x89, 0x8a)", L"0x89,0x8a" },
    { L"Like Mystery Caves (0x86, 0x8c)", L"0x86,0x8c" },
    { L"Like Cortex Castle (0x89)", L"0x89" },
    { L"Like N. Gin Labs (0x8d, 0x8e)", L"0x8d,0x8e" },
    { L"Like Oxide Station (0x8f, 0x90)", L"0x8f,0x90" },
    { L"Like Tiny Arena (0x91, 0x92)", L"0x91,0x92" },
};

static const struct TrackChoice *const g_trackChoices[TRACK_C_COUNT] = {
    g_trackReverb, g_trackBots, g_trackAmbient
};
static const int g_trackChoiceCount[TRACK_C_COUNT] = {
    (int)(sizeof(g_trackReverb) / sizeof(g_trackReverb[0])),
    (int)(sizeof(g_trackBots) / sizeof(g_trackBots[0])),
    (int)(sizeof(g_trackAmbient) / sizeof(g_trackAmbient[0])),
};
static const wchar_t *const g_trackComboLabels[TRACK_C_COUNT] = { L"Reverb", L"Bots drive", L"Ambient sound" };
// One sentence per field under "Advanced".
static const wchar_t *const g_trackComboHelp[TRACK_C_COUNT] = {
    L"How much sounds echo on this track (caves echo more).",
    L"Opponent speed, taken from an original track.",
    L"Background sound of the track (water, wind, crowd)."
};
static const wchar_t *const g_trackComboVerbs[TRACK_C_COUNT] = { L"reverb", L"bots", L"ambient" };
static const wchar_t *const g_trackComboSwitches[TRACK_C_COUNT] = { L"--reverb", L"--bots", L"--ambient" };
static const int g_trackComboValues[TRACK_C_COUNT] = { TRACK_V_REVERB, TRACK_V_BOTS, TRACK_V_AMBIENT };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct TrackFileInfo {
    int ok, missing, extra, unused;
    int count;                  // reported file names
    wchar_t names[TRACK_VAL];   // "a.lev, b.lev"
    wchar_t first[MAX_PATH];
    long long bytes;            // size of the first file, -1 = unknown
};

struct TrackModeInfo {
    int known;
    int data, declared, playable;
    wchar_t reason[512];
};

struct TrackMsg {
    int severity;
    wchar_t *text;
    wchar_t *detail;
};

// What the running (or last) rldpack run reported.
struct TrackJobData {
    int protocolSeen, protocol;
    int anyFile;
    struct TrackFileInfo file[TRACK_FILES];
    int anyValue;
    int valueSeen[TRACK_V_COUNT];
    wchar_t value[TRACK_V_COUNT][TRACK_VAL];
    wchar_t origin[TRACK_V_COUNT][16];
    int musicSeen;
    wchar_t musicState[16];
    wchar_t musicText[512];
    int anyMode;
    struct TrackModeInfo mode[TRACK_MODES];
    int levCount;
    wchar_t levKey[TRACK_LEV_FACTS][48];
    wchar_t levValue[TRACK_LEV_FACTS][64];
    struct TrackMsg *msgs;
    int msgCount, msgCap;
    int resultSeen;
    wchar_t resultState[16];
    wchar_t resultPath[TRACK_VAL];
    long long resultBytes;
    wchar_t resultSha[80];
    int endSeen, endCode;
};

static struct {
    HWND folder, folderBrowse;
    HWND fileLabel[TRACK_FILES], fileValue[TRACK_FILES];
    HWND nameLabel, name, authorLabel, author, versionLabel, version;
    HWND comboLabel[TRACK_C_COUNT], combo[TRACK_C_COUNT];
    HWND ambientNote, music;
    HWND advanced, advHelp[TRACK_C_COUNT], advSummary;
    int advOpen;                    // "Advanced" expanded; closed at start
    HWND mapLabel, mapValue;        // minimap: yes / too big, scaled / no
    COLORREF mapColor;
    HWND botLabel, botValue;        // data for the bots: nav paths and start positions
    COLORREF botColor;
    HWND mode[TRACK_MODES], modeNote[TRACK_MODES];
    HWND view, outLabel, out, outBrowse, check, build, headline, msgs, raw, test, show;

    // colours of the labels, for the report
    COLORREF fileColor[TRACK_FILES], modeColor[TRACK_MODES], ambientColor, headColor;

    wchar_t loaded[TRACK_VAL];      // loaded folder, empty = none
    int shown;                      // RS_WM_PAGE_SHOWN already seen
    int applying;                   // fields are being set: trigger no check
    int timer;                      // check waits for the timer
    int jobId, jobKind;             // running job, 0 = none
    int valuesKnown, modesKnown;
    struct TrackModeInfo modeInfo[TRACK_MODES];
    wchar_t custom[TRACK_C_COUNT][TRACK_VAL];
    int customIndex[TRACK_C_COUNT];
    wchar_t adjust[TRACK_MODES][768];   // modes from track.txt that were dropped
    int adjustCount;
    int checked;                    // last check gave "checked"
    wchar_t checkedPath[TRACK_VAL];
    wchar_t built[TRACK_VAL];       // last built container
    long long builtBytes;
    wchar_t builtSha[80];
    int previewAfterBuild;          // preview right after "Build container": the build stays shown
    int previewKilled;              // emergency brake (TRACK_TIMER_PREVIEW) ended the game
    int previewResult;              // TRACK_PREVIEW_*, from the game's output
    int previewFrames;              // -1 = not reported
    wchar_t previewPath[TRACK_VAL]; // reported .rldprev
    wchar_t previewReason[TRACK_VAL];
    wchar_t previewLog[TRACK_VAL];  // --log of the preview run
    int previewStage;               // TRACK_STAGE_*, how far the game got
    int previewCaptured;            // recorded frames according to the game
    int previewTotal;               // frames of the whole preview according to the game
    wchar_t previewHint[TRACK_VAL]; // reason from "--autoload-track: ...", empty = none
    int showRaw;
    wchar_t *rawText;               // all lines of the last run
    size_t rawLen, rawCap;
    int rawLines;
} g_track;

static struct TrackJobData g_trackJob;

static void Track_Layout(HWND page, int w, int h);
static int Track_Check(HWND page);
static int Track_Preview(HWND page, int afterBuild);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void Track_Copy(wchar_t *dst, int cap, const wchar_t *src)
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

static void Track_Append(wchar_t *dst, int cap, const wchar_t *src)
{
    int n = (int)wcslen(dst);
    if (n < cap - 1)
        Track_Copy(dst + n, cap - n, src);
}

static const wchar_t *Track_Field(wchar_t **fields, int count, int i)
{
    return (i < count && fields[i]) ? fields[i] : L"";
}

static int Track_IsChecked(HWND box)
{
    return SendMessageW(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void Track_SetCheck(HWND box, int on)
{
    SendMessageW(box, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

static int Track_IsShown(HWND h)
{
    return (GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE) != 0;
}

static void Track_Ellipsis(HWND label)
{
    LONG_PTR style = GetWindowLongPtrW(label, GWL_STYLE);
    SetWindowLongPtrW(label, GWL_STYLE, style | SS_ENDELLIPSIS);
}

// Kilobytes of 1000 bytes, rounded; small files in bytes.
static void Track_SizeText(wchar_t *out, int cap, long long bytes)
{
    if (bytes < 1000)
        swprintf(out, cap, L"%lld bytes", bytes);
    else
        swprintf(out, cap, L"%lld KB", (bytes + 500) / 1000);
}

static const wchar_t *Track_ColorName(COLORREF c)
{
    if (c == RS_COL_OK) return L"green";
    if (c == RS_COL_ERROR) return L"red";
    if (c == RS_COL_WARNING) return L"amber";
    if (c == RS_COL_NOTE) return L"blue";
    if (c == RS_COL_MUTED) return L"grey";
    return L"normal";
}

static void Track_SetLabel(HWND label, const wchar_t *text, COLORREF color, COLORREF *store)
{
    Rs_SetText(label, text);
    Rs_SetTextColor(label, color);
    if (store)
        *store = color;
}

static void Track_Headline(const wchar_t *text, COLORREF color)
{
    Track_SetLabel(g_track.headline, text, color, &g_track.headColor);
}

// Text the message list shows as long as it is empty.
static void Track_EmptyText(const wchar_t *text)
{
    Rs_SetText(g_track.msgs, text);
}

// "0x83,0x0" -> {0x83}: numbers decimal or 0x..., trailing zeros
// do not count. -1 if the text is not a list of numbers.
static int Track_Numbers(const wchar_t *s, unsigned long *out, int max)
{
    int n = 0;
    while (*s == L' ')
        s++;
    if (!*s)
        return -1;
    while (*s) {
        wchar_t *end;
        unsigned long v;
        while (*s == L' ')
            s++;
        if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
            v = wcstoul(s + 2, &end, 16);
            if (end == s + 2)
                return -1;
        } else if (*s >= L'0' && *s <= L'9') {
            v = wcstoul(s, &end, 10);
        } else {
            return -1;
        }
        if (n >= max)
            return -1;
        out[n++] = v;
        s = end;
        while (*s == L' ')
            s++;
        if (*s == L',')
            s++;
        else if (*s)
            return -1;
    }
    while (n > 1 && out[n - 1] == 0)
        n--;
    return n;
}

// Same value? Case does not matter, "0x83,0x0" = "0x83" = "131".
static int Track_SameValue(const wchar_t *a, const wchar_t *b)
{
    unsigned long na[4], nb[4];
    int ca, cb, i;
    if (_wcsicmp(a, b) == 0)
        return 1;
    ca = Track_Numbers(a, na, 4);
    cb = Track_Numbers(b, nb, 4);
    if (ca <= 0 || ca != cb)
        return 0;
    for (i = 0; i < ca; i++)
        if (na[i] != nb[i])
            return 0;
    return 1;
}

static int Track_IsSep(wchar_t c)
{
    return c == L',' || c == L' ' || c == L';' || c == L'\t';
}

// Next word of a list "race,time". NULL at the end.
static const wchar_t *Track_NextWord(const wchar_t *p, wchar_t *word, int cap)
{
    int n = 0;
    while (*p && Track_IsSep(*p))
        p++;
    if (!*p)
        return NULL;
    while (*p && !Track_IsSep(*p)) {
        if (n < cap - 1)
            word[n++] = *p;
        p++;
    }
    word[n] = 0;
    return p;
}

static int Track_ModeIndex(const wchar_t *word)
{
    int i;
    for (i = 0; i < TRACK_MODES; i++)
        if (_wcsicmp(word, g_trackModeWords[i]) == 0)
            return i;
    return -1;
}

static int Track_ListHasMode(const wchar_t *list, int mode)
{
    wchar_t word[64];
    const wchar_t *p = list;
    while ((p = Track_NextWord(p, word, 64)) != NULL)
        if (Track_ModeIndex(word) == mode)
            return 1;
    return 0;
}

// Is name already in "a.lev, b.lev"?
static int Track_NameListHas(const wchar_t *list, const wchar_t *name)
{
    size_t n = wcslen(name);
    const wchar_t *p = list;
    while (*p) {
        const wchar_t *end = wcsstr(p, L", ");
        size_t len = end ? (size_t)(end - p) : wcslen(p);
        if (len == n && wcsncmp(p, name, n) == 0)
            return 1;
        if (!end)
            break;
        p = end + 2;
    }
    return 0;
}

// Spaces and quotation marks away, no slash at the end.
static void Track_CleanPath(wchar_t *out, int cap, const wchar_t *in)
{
    size_t n;
    if (!in)
        in = L"";
    while (*in == L' ' || *in == L'\t' || *in == L'"')
        in++;
    Track_Copy(out, cap, in);
    n = wcslen(out);
    while (n > 0 && (out[n - 1] == L' ' || out[n - 1] == L'\t' || out[n - 1] == L'"'))
        out[--n] = 0;
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
}

// ---------------------------------------------------------------------------
// Choice fields
// ---------------------------------------------------------------------------

static void Track_FillCombo(int c)
{
    HWND cb = g_track.combo[c];
    int i;
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_trackChoiceCount[c]; i++) {
        LRESULT at = SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)g_trackChoices[c][i].text);
        SendMessageW(cb, CB_SETITEMDATA, (WPARAM)at, (LPARAM)i);
    }
    SendMessageW(cb, CB_SETCURSEL, 0, 0);
    g_track.custom[c][0] = 0;
    g_track.customIndex[c] = -1;
}

static const wchar_t *Track_ComboItemValue(int c, int item)
{
    LRESULT d = SendMessageW(g_track.combo[c], CB_GETITEMDATA, (WPARAM)item, 0);
    if (d == TRACK_CUSTOM_ITEM)
        return g_track.custom[c];
    if (d >= 0 && d < g_trackChoiceCount[c])
        return g_trackChoices[c][d].value;
    return L"default";
}

static const wchar_t *Track_ComboValue(int c)
{
    LRESULT sel = SendMessageW(g_track.combo[c], CB_GETCURSEL, 0, 0);
    return sel < 0 ? L"default" : Track_ComboItemValue(c, (int)sel);
}

// Selects the item with this value; "" and "default" = first item.
// addCustom = 1: if it is missing, "Custom (<value>)" is added. Return: index or -1.
static int Track_ComboSelect(int c, const wchar_t *value, int addCustom)
{
    HWND cb = g_track.combo[c];
    wchar_t text[TRACK_VAL + 16];
    LRESULT at;
    int i, n;

    if (!value || !*value || _wcsicmp(value, L"default") == 0) {
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
        return 0;
    }
    n = (int)SendMessageW(cb, CB_GETCOUNT, 0, 0);
    for (i = 0; i < n; i++) {
        if (Track_SameValue(Track_ComboItemValue(c, i), value)) {
            SendMessageW(cb, CB_SETCURSEL, (WPARAM)i, 0);
            return i;
        }
    }
    if (!addCustom)
        return -1;
    // The own item is always at the end; there is at most one.
    if (g_track.customIndex[c] >= 0)
        SendMessageW(cb, CB_DELETESTRING, (WPARAM)g_track.customIndex[c], 0);
    Track_Copy(g_track.custom[c], TRACK_VAL, value);
    swprintf(text, TRACK_VAL + 16, L"Custom (%ls)", value);
    at = SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)text);
    SendMessageW(cb, CB_SETITEMDATA, (WPARAM)at, (LPARAM)TRACK_CUSTOM_ITEM);
    SendMessageW(cb, CB_SETCURSEL, (WPARAM)at, 0);
    g_track.customIndex[c] = (int)at;
    return (int)at;
}

// ---------------------------------------------------------------------------
// The pipe from rldpack
// ---------------------------------------------------------------------------

static void Track_JobReset(void)
{
    int i;
    for (i = 0; i < g_trackJob.msgCount; i++) {
        Rs_Free(g_trackJob.msgs[i].text);
        Rs_Free(g_trackJob.msgs[i].detail);
    }
    Rs_Free(g_trackJob.msgs);
    memset(&g_trackJob, 0, sizeof(g_trackJob));
    g_track.rawLen = 0;
    g_track.rawLines = 0;
    if (g_track.rawText)
        g_track.rawText[0] = 0;
}

static void Track_JobMsg(int severity, const wchar_t *text, const wchar_t *detail)
{
    struct TrackJobData *j = &g_trackJob;
    if (j->msgCount == j->msgCap) {
        int cap = j->msgCap ? j->msgCap * 2 : 16;
        struct TrackMsg *m = Rs_Alloc((size_t)cap * sizeof(*m));
        if (j->msgCount)
            memcpy(m, j->msgs, (size_t)j->msgCount * sizeof(*m));
        Rs_Free(j->msgs);
        j->msgs = m;
        j->msgCap = cap;
    }
    j->msgs[j->msgCount].severity = severity;
    j->msgs[j->msgCount].text = Rs_Dup(text);
    j->msgs[j->msgCount].detail = Rs_Dup(detail);
    j->msgCount++;
}

static void Track_RawAppend(const wchar_t *line)
{
    size_t n = wcslen(line);
    size_t need = g_track.rawLen + n + 3;
    if (need > g_track.rawCap) {
        size_t cap = g_track.rawCap ? g_track.rawCap : 4096;
        wchar_t *p;
        while (cap < need)
            cap *= 2;
        p = Rs_Alloc(cap * sizeof(wchar_t));
        if (g_track.rawLen)
            memcpy(p, g_track.rawText, g_track.rawLen * sizeof(wchar_t));
        Rs_Free(g_track.rawText);
        g_track.rawText = p;
        g_track.rawCap = cap;
    }
    if (g_track.rawLen) {
        g_track.rawText[g_track.rawLen++] = L'\r';
        g_track.rawText[g_track.rawLen++] = L'\n';
    }
    memcpy(g_track.rawText + g_track.rawLen, line, n * sizeof(wchar_t));
    g_track.rawLen += n;
    g_track.rawText[g_track.rawLen] = 0;
    g_track.rawLines++;
}

static void Track_RawRefresh(void)
{
    Rs_SetText(g_track.raw, g_track.rawText ? g_track.rawText : L"");
}

// One line of the current run. Human lines and unknown kinds stay
// only in the raw output. The line is split in the process.
static void Track_ParseLine(wchar_t *line)
{
    struct TrackJobData *j = &g_trackJob;
    wchar_t *f[12];
    int n = Rs_SplitMachine(line, f, 12);
    const wchar_t *kind;

    if (n <= 0)
        return;
    kind = f[0];
    if (wcscmp(kind, L"rldpack") == 0) {
        j->protocolSeen = 1;
        j->protocol = _wtoi(Track_Field(f, n, 1));
    } else if (wcscmp(kind, L"file") == 0) {
        const wchar_t *state = Track_Field(f, n, 2);
        const wchar_t *name = Track_Field(f, n, 3);
        const wchar_t *bytes = Track_Field(f, n, 4);
        struct TrackFileInfo *fi;
        int k;
        for (k = 0; k < TRACK_FILES; k++)
            if (wcscmp(Track_Field(f, n, 1), g_trackFileKinds[k]) == 0)
                break;
        // A finished .sndb is music like a .sca: the same
        // line. If both are in the folder, the line counts two files.
        if (wcscmp(Track_Field(f, n, 1), L"sndb") == 0)
            k = TRACK_F_SCA;
        if (k == TRACK_FILES)
            return;
        fi = &j->file[k];
        j->anyFile = 1;
        if (wcscmp(state, L"missing") == 0) {
            fi->missing = 1;
            return;
        }
        if (wcscmp(state, L"ok") == 0)
            fi->ok = 1;
        else if (wcscmp(state, L"extra") == 0)
            fi->extra = 1;
        else if (wcscmp(state, L"unused") == 0)
            fi->unused = 1;
        else
            return;
        if (!*name || Track_NameListHas(fi->names, name))
            return;
        if (fi->count == 0) {
            Track_Copy(fi->first, MAX_PATH, name);
            fi->bytes = *bytes ? wcstoll(bytes, NULL, 10) : -1;
        }
        if (fi->names[0])
            Track_Append(fi->names, TRACK_VAL, L", ");
        Track_Append(fi->names, TRACK_VAL, name);
        fi->count++;
    } else if (wcscmp(kind, L"value") == 0) {
        int v;
        for (v = 0; v < TRACK_V_COUNT; v++)
            if (wcscmp(Track_Field(f, n, 1), g_trackValueKeys[v]) == 0)
                break;
        if (v == TRACK_V_COUNT)
            return;
        j->anyValue = 1;
        j->valueSeen[v] = 1;
        Track_Copy(j->value[v], TRACK_VAL, Track_Field(f, n, 2));
        Track_Copy(j->origin[v], 16, Track_Field(f, n, 3));
    } else if (wcscmp(kind, L"music") == 0) {
        j->musicSeen = 1;
        Track_Copy(j->musicState, 16, Track_Field(f, n, 1));
        Track_Copy(j->musicText, 512, Track_Field(f, n, 2));
    } else if (wcscmp(kind, L"mode") == 0) {
        int m = Track_ModeIndex(Track_Field(f, n, 1));
        struct TrackModeInfo *mi;
        if (m < 0)
            return;
        mi = &j->mode[m];
        j->anyMode = 1;
        mi->known = 1;
        mi->data = wcscmp(Track_Field(f, n, 2), L"yes") == 0;
        mi->declared = wcscmp(Track_Field(f, n, 3), L"yes") == 0;
        mi->playable = wcscmp(Track_Field(f, n, 4), L"yes") == 0;
        Track_Copy(mi->reason, 512, Track_Field(f, n, 5));
    } else if (wcscmp(kind, L"lev") == 0) {
        const wchar_t *key = Track_Field(f, n, 1);
        int i;
        for (i = 0; i < j->levCount; i++)
            if (wcscmp(j->levKey[i], key) == 0)
                break;
        if (i == j->levCount) {
            if (j->levCount >= TRACK_LEV_FACTS)
                return;
            j->levCount++;
        }
        Track_Copy(j->levKey[i], 48, key);
        Track_Copy(j->levValue[i], 64, Track_Field(f, n, 2));
    } else if (wcscmp(kind, L"msg") == 0) {
        const wchar_t *text = Track_Field(f, n, 3);
        const wchar_t *detail = Track_Field(f, n, 4);
        if (!*text) {
            text = detail;
            detail = L"";
        }
        Track_JobMsg(Rs_SeverityFromText(Track_Field(f, n, 1)), text, detail);
    } else if (wcscmp(kind, L"result") == 0) {
        const wchar_t *bytes = Track_Field(f, n, 3);
        j->resultSeen = 1;
        Track_Copy(j->resultState, 16, Track_Field(f, n, 1));
        Track_Copy(j->resultPath, TRACK_VAL, Track_Field(f, n, 2));
        j->resultBytes = *bytes ? wcstoll(bytes, NULL, 10) : -1;
        Track_Copy(j->resultSha, 80, Track_Field(f, n, 4));
    } else if (wcscmp(kind, L"end") == 0) {
        j->endSeen = 1;
        j->endCode = _wtoi(Track_Field(f, n, 1));
    }
}

static const wchar_t *Track_LevFact(const wchar_t *key)
{
    int i;
    for (i = 0; i < g_trackJob.levCount; i++)
        if (wcscmp(g_trackJob.levKey[i], key) == 0)
            return g_trackJob.levValue[i];
    return NULL;
}

// Row "Minimap" from @lev map (fits | scaled | none). Without a map that is a
// note, not an error; rldpack sends the reason for authors as a note.
static void Track_ApplyMap(void)
{
    const wchar_t *map = Track_LevFact(L"map");
    if (!map)
        return;
    if (wcscmp(map, L"fits") == 0)
        Track_SetLabel(g_track.mapValue, L"yes", RS_COL_OK, &g_track.mapColor);
    else if (wcscmp(map, L"scaled") == 0)
        Track_SetLabel(g_track.mapValue, L"yes - too big for the menu, the game scales it down", RS_COL_OK, &g_track.mapColor);
    else
        Track_SetLabel(g_track.mapValue, L"no - the menu and the race show no map (see the note)", RS_COL_NOTE, &g_track.mapColor);
}

// Row "Bot data" from @lev nav_paths, nav_points and
// start_spots. Without a nav path the game creates no opponent; start positions
// on the same point make karts start inside each other.
static void Track_ApplyBots(void)
{
    const wchar_t *nav = Track_LevFact(L"nav_paths");
    const wchar_t *points = Track_LevFact(L"nav_points");
    const wchar_t *spots = Track_LevFact(L"start_spots");
    wchar_t t[256];
    int paths, starts;

    if (!nav)
        return;
    paths = _wtoi(nav);
    starts = spots ? _wtoi(spots) : 8;
    // Short, so that the row fits into the card; the sentences for authors are
    // in rldpack's messages (no-nav, start-spots).
    if (paths > 0 && starts >= 8) {
        wchar_t list[48];
        size_t k;
        Track_Copy(list, 48, points ? points : L"?");
        for (k = 0; list[k]; k++)
            if (list[k] == L',')
                list[k] = L'/';
        swprintf(t, 256, L"yes - %d nav paths (%ls points)", paths, list);
        Track_SetLabel(g_track.botValue, t, RS_COL_OK, &g_track.botColor);
    } else if (paths > 0) {
        swprintf(t, 256, L"partly - %d nav paths, only %d of 8 start spots", paths, starts);
        Track_SetLabel(g_track.botValue, t, RS_COL_WARNING, &g_track.botColor);
    } else {
        if (starts < 8)
            swprintf(t, 256, L"no - no nav paths, no bots; %d of 8 start spots", starts);
        else
            Track_Copy(t, 256, L"no - no nav paths, so no bots race");
        Track_SetLabel(g_track.botValue, t, RS_COL_WARNING, &g_track.botColor);
    }
}

// Collapsed, "Advanced" shows in one line what is chosen, so that a value
// from track.txt does not become invisible.
static void Track_AdvSummary(void)
{
    wchar_t t[TRACK_VAL];
    int i;
    t[0] = 0;
    for (i = 0; i < TRACK_C_COUNT; i++) {
        wchar_t *text = Rs_GetText(g_track.combo[i]);
        if (i)
            Track_Append(t, TRACK_VAL, L"  \x00b7  ");
        Track_Append(t, TRACK_VAL, g_trackComboLabels[i]);
        Track_Append(t, TRACK_VAL, L": ");
        Track_Append(t, TRACK_VAL, (text && text[0]) ? text : L"-");
        Rs_Free(text);
    }
    Rs_SetText(g_track.advSummary, t);
}

static void Track_AdvShow(void)
{
    int i, show = g_track.advOpen ? SW_SHOW : SW_HIDE;
    Rs_SetText(g_track.advanced, g_track.advOpen ? L"Advanced  \x25be" : L"Advanced  \x25b8");
    for (i = 0; i < TRACK_C_COUNT; i++) {
        ShowWindow(g_track.comboLabel[i], show);
        ShowWindow(g_track.advHelp[i], show);
        ShowWindow(g_track.combo[i], show);
    }
    ShowWindow(g_track.ambientNote, show);
    ShowWindow(g_track.advSummary, g_track.advOpen ? SW_HIDE : SW_SHOW);
    Track_AdvSummary();
}

// ---------------------------------------------------------------------------
// Display of the results
// ---------------------------------------------------------------------------

// Text and colour of a file row.
static void Track_FileText(int k, wchar_t *out, int cap, COLORREF *color)
{
    const struct TrackFileInfo *fi = &g_trackJob.file[k];
    wchar_t size[32];

    *color = RS_COL_TEXT;
    size[0] = 0;
    if (fi->bytes >= 0)
        Track_SizeText(size, 32, fi->bytes);
    if (fi->extra || fi->count > 1) {
        swprintf(out, cap, L"%d files - keep only one: %ls", fi->count, fi->names);
        *color = RS_COL_ERROR;
    } else if (fi->count == 1) {
        if (size[0])
            swprintf(out, cap, L"%ls  (%ls)", fi->first, size);
        else
            Track_Copy(out, cap, fi->first);
        if (fi->unused) {
            Track_Append(out, cap, L" - not used, the music is switched off");
            *color = RS_COL_MUTED;
        }
    } else if (fi->missing) {
        if (k == TRACK_F_SCA) {
            Track_Copy(out, cap, L"none - the track keeps the music of its seat");
            *color = RS_COL_MUTED;
        } else if (k == TRACK_F_TXT) {
            Track_Copy(out, cap, L"none - values below come from the defaults");
            *color = RS_COL_MUTED;
        } else {
            Track_Copy(out, cap, L"missing - the folder needs exactly one");
            *color = RS_COL_ERROR;
        }
    } else {
        Track_Copy(out, cap, L"-");
        *color = RS_COL_MUTED;
    }
}

static const wchar_t *Track_FileWord(int k)
{
    const struct TrackFileInfo *fi = &g_trackJob.file[k];
    if (fi->extra || fi->count > 1) return L"more than one";
    if (fi->count == 1) return fi->unused ? L"unused" : L"ok";
    if (fi->missing) return L"missing";
    return L"not reported";
}

static void Track_ApplyFiles(void)
{
    wchar_t text[TRACK_VAL + 64];
    COLORREF color;
    int k;
    if (!g_trackJob.anyFile)
        return;
    for (k = 0; k < TRACK_FILES; k++) {
        Track_FileText(k, text, TRACK_VAL + 64, &color);
        Track_SetLabel(g_track.fileValue[k], text, color, &g_track.fileColor[k]);
    }
}

// Values from track.txt or the defaults into the fields (only after loading).
static void Track_ApplyValues(void)
{
    struct TrackJobData *j = &g_trackJob;
    const wchar_t *sca = j->file[TRACK_F_SCA].first;
    const wchar_t *music;
    wchar_t text[MAX_PATH + 64];
    int c;

    if (j->valueSeen[TRACK_V_NAME])
        Rs_SetText(g_track.name, j->value[TRACK_V_NAME]);
    if (j->valueSeen[TRACK_V_AUTHOR])
        Rs_SetText(g_track.author, j->value[TRACK_V_AUTHOR]);
    if (j->valueSeen[TRACK_V_VERSION])
        Rs_SetText(g_track.version, j->value[TRACK_V_VERSION]);
    if (j->valueSeen[TRACK_V_OUT])
        Rs_SetText(g_track.out, j->value[TRACK_V_OUT]);
    for (c = 0; c < TRACK_C_COUNT; c++)
        if (j->valueSeen[g_trackComboValues[c]])
            Track_ComboSelect(c, j->value[g_trackComboValues[c]], 1);

    if (j->valueSeen[TRACK_V_MUSIC])
        music = j->value[TRACK_V_MUSIC];
    else
        music = sca[0] ? L"on" : L"none";
    if (wcscmp(music, L"none") == 0) {
        Track_SetCheck(g_track.music, 0);
        EnableWindow(g_track.music, FALSE);
        Rs_SetText(g_track.music, L"No music file (.sca or .sndb) in the folder");
    } else {
        if (sca[0])
            swprintf(text, MAX_PATH + 64, L"Use the music in the folder (%ls)", sca);
        else
            Track_Copy(text, MAX_PATH + 64, L"Use the music in the folder");
        Rs_SetText(g_track.music, text);
        EnableWindow(g_track.music, TRUE);
        Track_SetCheck(g_track.music, wcscmp(music, L"off") != 0);
    }
}

// Without a place for an ambient sound the choice stays on "None".
// Returns 1 if the choice changed because of that.
static int Track_ApplyLev(void)
{
    const wchar_t *place = Track_LevFact(L"ambient_place_1");
    HWND cb = g_track.combo[TRACK_C_AMBIENT];
    int changed = 0;

    if (!place)
        return 0;
    if (wcscmp(place, L"no") == 0) {
        changed = SendMessageW(cb, CB_GETCURSEL, 0, 0) != 0;
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
        EnableWindow(cb, FALSE);
        Track_SetLabel(g_track.ambientNote,
                       L"This track has no spot for an ambient sound, so none would play.",
                       RS_COL_MUTED, &g_track.ambientColor);
    } else {
        EnableWindow(cb, TRUE);
        Track_SetLabel(g_track.ambientNote, L"", RS_COL_MUTED, &g_track.ambientColor);
    }
    return changed;
}

// TIME TRIAL and BATTLE: always grey in Reload Studio, whatever
// data the track has, and never in the container - neither through track.txt
// nor through a switch from the front end: Track_MakeArgs only takes
// free, ticked boxes. rldpack on the command line stays as it is.
#define TRACK_COMING_SOON_TEXT L"Coming soon in Beta 2"

static int Track_ModeComingSoon(int mode)
{
    return wcscmp(g_trackModeWords[mode], L"time") == 0 || wcscmp(g_trackModeWords[mode], L"battle") == 0;
}

// The text for a mode with data: Race, Crystal and CTR are playable in Beta 0.
// One text for all three; mode stays for later.
static const wchar_t *Track_ModeReady(int mode, int playable)
{
    (void)mode;
    return playable ? L"Playable" : L"Not playable in CTR Reload yet";
}

// Rows of the card "Modes". Returns 1 if a tick had to be dropped.
static int Track_ApplyModes(void)
{
    int loading = g_track.jobId && g_track.jobKind == TRACK_JOB_LOAD;
    int i, removed = 0;

    for (i = 0; i < TRACK_MODES; i++) {
        const struct TrackModeInfo *m = &g_track.modeInfo[i];
        HWND box = g_track.mode[i];
        const wchar_t *note;
        COLORREF color = RS_COL_MUTED;
        int enable = 0;

        if (Track_ModeComingSoon(i)) {
            note = TRACK_COMING_SOON_TEXT;
        } else if (!g_track.modesKnown || !m->known) {
            if (!g_track.loaded[0])
                note = L"Choose a track folder first";
            else if (loading)
                note = L"Reading the folder...";
            else
                note = L"Needs a .lev and a .vrm that rldpack can read";
        } else if (!m->data) {
            note = m->reason[0] ? m->reason : L"This track has no data for this mode.";
        } else {
            // No colour of its own for "not playable yet" - blue read
            // like an error.
            enable = 1;
            note = Track_ModeReady(i, m->playable);
        }
        if (!enable && Track_IsChecked(box)) {
            Track_SetCheck(box, 0);
            removed = 1;
        }
        EnableWindow(box, enable);
        Track_SetLabel(g_track.modeNote[i], note, color, &g_track.modeColor[i]);
    }
    return removed;
}

// "track.txt asks for Time Trial, but needs ... Time Trial is left out."
static void Track_AddAdjustment(int mode)
{
    const struct TrackModeInfo *m = &g_track.modeInfo[mode];
    wchar_t reason[512];
    size_t n;

    if (g_track.adjustCount >= TRACK_MODES)
        return;
    if (Track_ModeComingSoon(mode))
        Track_Copy(reason, 512, L"it is coming soon in Beta 2.");
    else
        Track_Copy(reason, 512, m->reason[0] ? m->reason : L"this track has no data for it.");
    // "Needs" -> "needs"; "N. Tropy" and "CTR" stay
    if (reason[0] >= L'A' && reason[0] <= L'Z' && reason[1] >= L'a' && reason[1] <= L'z')
        reason[0] = (wchar_t)(reason[0] - L'A' + L'a');
    n = wcslen(reason);
    swprintf(g_track.adjust[g_track.adjustCount++], 768, L"track.txt asks for %ls, but %ls%ls %ls is left out.",
             g_trackModeLabels[mode], reason, (n > 0 && reason[n - 1] == L'.') ? L"" : L".",
             g_trackModeLabels[mode]);
}

// Take over the modes from the run. The first time after loading, set the ticks from
// track.txt; modes without data stay off and go into the notes.
// If neither track.txt nor a switch names the modes (@value modes ... default),
// the maker ticks every mode for which the track has the data; the
// rldpack default "race" alone would leave a pure crystal track without a mode.
// Returns 1 if a tick had to be dropped.
static int Track_TakeModes(void)
{
    struct TrackJobData *j = &g_trackJob;
    int first = !g_track.modesKnown;
    int i;

    if (!j->anyMode)
        return Track_ApplyModes();
    memcpy(g_track.modeInfo, j->mode, sizeof(g_track.modeInfo));
    g_track.modesKnown = 1;
    if (first) {
        const wchar_t *list = j->valueSeen[TRACK_V_MODES] ? j->value[TRACK_V_MODES] : NULL;
        int fromTxt = list ? wcscmp(j->origin[TRACK_V_MODES], L"track.txt") == 0
                           : j->file[TRACK_F_TXT].count > 0;
        int fromData = list && wcscmp(j->origin[TRACK_V_MODES], L"default") == 0;
        g_track.adjustCount = 0;
        for (i = 0; i < TRACK_MODES; i++) {
            const struct TrackModeInfo *m = &g_track.modeInfo[i];
            int want = fromData ? m->data : (list ? Track_ListHasMode(list, i) : m->declared);
            if (want && Track_ModeComingSoon(i)) {
                want = 0;
                if (fromTxt)
                    Track_AddAdjustment(i);
            } else if (want && !m->data) {
                want = 0;
                if (fromTxt)
                    Track_AddAdjustment(i);
            }
            Track_SetCheck(g_track.mode[i], want);
        }
    }
    return Track_ApplyModes();
}

static int Track_CanBuild(void)
{
    return g_track.loaded[0] && g_track.checked && !g_track.jobId && !g_track.timer;
}

static void Track_UpdateButtons(void)
{
    int running = g_track.jobId != 0;
    int building = running && g_track.jobKind == TRACK_JOB_BUILD;
    int loading = running && g_track.jobKind == TRACK_JOB_LOAD;
    int previewing = running && g_track.jobKind == TRACK_JOB_PREVIEW;

    EnableWindow(g_track.check, g_track.loaded[0] && !building && !loading && !previewing);
    EnableWindow(g_track.build, Track_CanBuild());
    EnableWindow(g_track.folderBrowse, !building && !previewing);
    EnableWindow(g_track.outBrowse, !building && !previewing);
    EnableWindow(g_track.test, !building && !previewing);
    EnableWindow(g_track.show, !building);
}

static void Track_ShowView(void)
{
    if (g_track.showRaw)
        Track_RawRefresh();
    ShowWindow(g_track.raw, g_track.showRaw ? SW_SHOW : SW_HIDE);
    ShowWindow(g_track.msgs, g_track.showRaw ? SW_HIDE : SW_SHOW);
    Rs_SetText(g_track.view, g_track.showRaw ? L"Show messages" : L"Show rldpack output");
}

static void Track_Relayout(HWND page)
{
    RECT rc;
    GetClientRect(page, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
        Track_Layout(page, rc.right, rc.bottom);
        InvalidateRect(page, NULL, TRUE);
    }
}

static int Track_CountMsgs(int severity)
{
    int i, n = 0;
    for (i = 0; i < g_trackJob.msgCount; i++)
        if (g_trackJob.msgs[i].severity == severity)
            n++;
    return n;
}

// All @msg of one severity, in rldpack's order.
static void Track_AddMsgs(int severity)
{
    int i;
    for (i = 0; i < g_trackJob.msgCount; i++)
        if (g_trackJob.msgs[i].severity == severity)
            Rs_MsgListAdd(g_track.msgs, severity, g_trackJob.msgs[i].text, g_trackJob.msgs[i].detail);
}

static void Track_AddAdjustments(void)
{
    int i;
    for (i = 0; i < g_track.adjustCount; i++)
        Rs_MsgListAdd(g_track.msgs, RS_SEV_WARNING, g_track.adjust[i], NULL);
}

// What rldpack could not report itself: no result, failure without a
// reason, foreign protocol. Return: number of errors that were added.
static int Track_AddRunProblems(int exitCode, int ok)
{
    struct TrackJobData *j = &g_trackJob;
    wchar_t t[256];

    if (j->protocolSeen && j->protocol != RS_PROTOCOL) {
        swprintf(t, 256, L"rldpack reports in format %d, but this Reload Studio reads format %d.",
                 j->protocol, RS_PROTOCOL);
        Rs_MsgListAdd(g_track.msgs, RS_SEV_WARNING, t,
                      L"Some results may be missing. rldpack is built into this Reload Studio, so both should always "
                      L"match - this build looks inconsistent.");
    }
    if (!j->resultSeen) {
        swprintf(t, 256, L"rldpack stopped without a result (exit code %d).", exitCode);
        Rs_MsgListAdd(g_track.msgs, RS_SEV_ERROR, t,
                      L"Click \"Show rldpack output\" to see everything it printed.");
        return 1;
    }
    if (!ok && Track_CountMsgs(RS_SEV_ERROR) == 0) {
        swprintf(t, 256, L"rldpack did not accept the track, but gave no reason (exit code %d).", exitCode);
        Rs_MsgListAdd(g_track.msgs, RS_SEV_ERROR, t,
                      L"Click \"Show rldpack output\" to see everything it printed.");
        return 1;
    }
    return 0;
}

static void Track_ShowCheckResult(int exitCode)
{
    struct TrackJobData *j = &g_trackJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"checked") == 0;
    wchar_t t[640];
    int errors;

    g_track.checked = ok;
    Track_Copy(g_track.checkedPath, TRACK_VAL, j->resultPath);
    Rs_MsgListClear(g_track.msgs);
    Track_AddAdjustments();
    errors = Track_AddRunProblems(exitCode, ok);
    errors += Track_CountMsgs(RS_SEV_ERROR);
    Track_AddMsgs(RS_SEV_ERROR);
    Track_AddMsgs(RS_SEV_WARNING);
    Track_AddMsgs(RS_SEV_NOTE);
    Track_AddMsgs(RS_SEV_INFO);
    Track_AddMsgs(RS_SEV_OK);
    if (j->musicSeen) {
        swprintf(t, 640, L"Music: %ls", j->musicText[0] ? j->musicText : j->musicState);
        Rs_MsgListAdd(g_track.msgs, RS_SEV_INFO, t, NULL);
    }
    Track_EmptyText(L"rldpack reported nothing.");
    if (ok) {
        Track_Headline(L"Ready to build", RS_COL_OK);
    } else {
        swprintf(t, 640, L"Cannot build yet - %d problem(s)", errors);
        Track_Headline(t, RS_COL_ERROR);
    }
    Track_UpdateButtons();
    if (Rs_Automating()) {
        if (ok)
            Rs_AutoLog(L"  check: ready to build, %d warning(s), %d note(s)",
                       Track_CountMsgs(RS_SEV_WARNING) + g_track.adjustCount, Track_CountMsgs(RS_SEV_NOTE));
        else
            Rs_AutoLog(L"  check: cannot build yet, %d problem(s)", errors);
    }
}

static void Track_StartFailed(const wchar_t *headline)
{
    Rs_MsgListClear(g_track.msgs);
    Rs_MsgListAdd(g_track.msgs, RS_SEV_ERROR, L"Reload Studio could not start rldpack.",
                  L"rldpack runs as a second copy of Reload Studio. Try again; if it keeps failing, "
                  L"check that no security program blocks it.");
    Track_Headline(headline, RS_COL_ERROR);
    Track_UpdateButtons();
    if (Rs_Automating())
        Rs_AutoLog(L"  rldpack could not be started");
}

// ---------------------------------------------------------------------------
// Commands to rldpack
// ---------------------------------------------------------------------------

struct TrackArgs {
    const wchar_t *v[TRACK_MAX_ARGS];
    int n;
    wchar_t *own[8];        // texts from fields, free with Rs_Free
    int owned;
    wchar_t modes[80];
};

static void Track_ArgsAdd(struct TrackArgs *a, const wchar_t *s)
{
    if (a->n < TRACK_MAX_ARGS)
        a->v[a->n++] = s;
}

static const wchar_t *Track_ArgsText(struct TrackArgs *a, HWND edit)
{
    wchar_t *t = Rs_GetText(edit);
    if (a->owned < 8)
        a->own[a->owned++] = t;
    return t;
}

static void Track_ArgsFree(struct TrackArgs *a)
{
    int i;
    for (i = 0; i < a->owned; i++)
        Rs_Free(a->own[i]);
    a->owned = 0;
}

// make with the values of the fields. check = 1: with --check. out: output path
// (NULL = from the field). As long as rldpack has reported no values, without
// switches. Returns 0 if there are choosable modes but none is ticked.
static int Track_MakeArgs(struct TrackArgs *a, int check, const wchar_t *out)
{
    int i;

    memset(a, 0, sizeof(*a));
    Track_ArgsAdd(a, L"make");
    Track_ArgsAdd(a, g_track.loaded);
    Track_ArgsAdd(a, L"--machine");
    if (check)
        Track_ArgsAdd(a, L"--check");
    // Build only with known modes: without --modes rldpack would take those from
    // track.txt, Time Trial and Battle included.
    if (!check && (!g_track.valuesKnown || !g_track.modesKnown))
        return 0;
    if (!g_track.valuesKnown) {
        if (out && *out) {
            Track_ArgsAdd(a, L"--out");
            Track_ArgsAdd(a, out);
        }
        return 1;
    }
    Track_ArgsAdd(a, L"--name");
    Track_ArgsAdd(a, Track_ArgsText(a, g_track.name));
    Track_ArgsAdd(a, L"--author");
    Track_ArgsAdd(a, Track_ArgsText(a, g_track.author));
    Track_ArgsAdd(a, L"--track-version");
    Track_ArgsAdd(a, Track_ArgsText(a, g_track.version));
    if (g_track.modesKnown) {
        for (i = 0; i < TRACK_MODES; i++) {
            if (!IsWindowEnabled(g_track.mode[i]) || !Track_IsChecked(g_track.mode[i]))
                continue;
            if (a->modes[0])
                Track_Append(a->modes, 80, L",");
            Track_Append(a->modes, 80, g_trackModeWords[i]);
        }
        if (!a->modes[0])
            return 0;
        Track_ArgsAdd(a, L"--modes");
        Track_ArgsAdd(a, a->modes);
    }
    for (i = 0; i < TRACK_C_COUNT; i++) {
        Track_ArgsAdd(a, g_trackComboSwitches[i]);
        Track_ArgsAdd(a, Track_ComboValue(i));
    }
    if (IsWindowEnabled(g_track.music) && !Track_IsChecked(g_track.music))
        Track_ArgsAdd(a, L"--no-music");
    if (!out) {
        // as for the build: without quotation marks and spaces at the edges
        wchar_t *text = Rs_GetText(g_track.out);
        wchar_t *clean = Rs_Alloc(TRACK_VAL * sizeof(wchar_t));
        Track_CleanPath(clean, TRACK_VAL, text);
        Rs_Free(text);
        if (a->owned < 8)
            a->own[a->owned++] = clean;
        out = clean;
    }
    if (*out) {
        Track_ArgsAdd(a, L"--out");
        Track_ArgsAdd(a, out);
    }
    return 1;
}

static int Track_StartJob(HWND page, int kind, const wchar_t *const *args, int argc)
{
    Track_JobReset();
    g_track.jobKind = kind;
    g_track.jobId = Rs_RunRldpack(page, args, argc);
    if (!g_track.jobId)
        g_track.jobKind = TRACK_JOB_NONE;
    Track_UpdateButtons();
    return g_track.jobId;
}

// Reset everything to "no folder" (fields, modes, files, messages).
static void Track_ResetState(void)
{
    int i;

    g_track.applying = 1;
    g_track.valuesKnown = 0;
    g_track.modesKnown = 0;
    memset(g_track.modeInfo, 0, sizeof(g_track.modeInfo));
    g_track.adjustCount = 0;
    g_track.checked = 0;
    g_track.checkedPath[0] = 0;
    g_track.built[0] = 0;
    g_track.builtBytes = 0;
    g_track.builtSha[0] = 0;
    for (i = 0; i < TRACK_FILES; i++)
        Track_SetLabel(g_track.fileValue[i], L"-", RS_COL_MUTED, &g_track.fileColor[i]);
    Track_SetLabel(g_track.mapValue, L"-", RS_COL_MUTED, &g_track.mapColor);
    Track_SetLabel(g_track.botValue, L"-", RS_COL_MUTED, &g_track.botColor);
    Rs_SetText(g_track.name, L"");
    Rs_SetText(g_track.author, L"");
    Rs_SetText(g_track.version, L"");
    Rs_SetText(g_track.out, L"");
    for (i = 0; i < TRACK_C_COUNT; i++) {
        Track_FillCombo(i);
        EnableWindow(g_track.combo[i], TRUE);
    }
    Track_SetLabel(g_track.ambientNote, L"", RS_COL_MUTED, &g_track.ambientColor);
    Track_SetCheck(g_track.music, 0);
    EnableWindow(g_track.music, FALSE);
    Rs_SetText(g_track.music, L"Use the music in the folder");
    for (i = 0; i < TRACK_MODES; i++)
        Track_SetCheck(g_track.mode[i], 0);
    Rs_MsgListClear(g_track.msgs);
    Track_JobReset();
    if (g_track.showRaw)
        Track_RawRefresh();
    g_track.applying = 0;
}

// Load folder: reset everything and start make --check without switches.
// Returns 1 if rldpack is running.
static int Track_Load(HWND page, const wchar_t *folder)
{
    const wchar_t *args[4];
    wchar_t clean[TRACK_VAL];
    wchar_t *current;

    Track_CleanPath(clean, TRACK_VAL, folder);
    KillTimer(page, TRACK_TIMER_CHECK);
    g_track.timer = 0;
    g_track.jobId = 0;          // running jobs are outdated from here on
    g_track.jobKind = TRACK_JOB_NONE;
    Track_Copy(g_track.loaded, TRACK_VAL, clean);
    Track_ResetState();
    current = Rs_GetText(g_track.folder);
    if (wcscmp(current, clean) != 0)
        Rs_SetText(g_track.folder, clean);
    Rs_Free(current);

    if (!clean[0]) {
        Track_Headline(L"Choose a track folder to start", RS_COL_MUTED);
        Track_EmptyText(L"Choose a track folder. What rldpack finds shows up here.");
        Track_ApplyModes();
        Track_UpdateButtons();
        Track_Relayout(page);
        return 0;
    }
    Rs_ConfigSet(L"track.folder", clean);
    args[0] = L"make";
    args[1] = clean;
    args[2] = L"--machine";
    args[3] = L"--check";
    Track_Headline(L"Reading the folder...", RS_COL_MUTED);
    Track_EmptyText(L"rldpack is reading the folder...");
    if (!Track_StartJob(page, TRACK_JOB_LOAD, args, 4)) {
        Track_ApplyModes();
        Track_StartFailed(L"Cannot build yet - 1 problem(s)");
        Track_Relayout(page);
        return 0;
    }
    Track_ApplyModes();
    Track_Relayout(page);
    return 1;
}

// Checks with the values of the fields. Return: 1 = rldpack running,
// 0 = done without rldpack (the message is already there), -1 = nothing to check.
static int Track_Check(HWND page)
{
    struct TrackArgs a;
    int started;

    KillTimer(page, TRACK_TIMER_CHECK);
    g_track.timer = 0;
    if (!g_track.loaded[0] || (g_track.jobId && g_track.jobKind != TRACK_JOB_CHECK)) {
        Track_UpdateButtons();
        return -1;
    }
    g_track.jobId = 0;          // a running check is thereby outdated
    g_track.jobKind = TRACK_JOB_NONE;
    g_track.checked = 0;
    if (!Track_MakeArgs(&a, 1, NULL)) {
        Track_ArgsFree(&a);
        Rs_MsgListClear(g_track.msgs);
        Track_AddAdjustments();
        Rs_MsgListAdd(g_track.msgs, RS_SEV_ERROR, L"Tick at least one mode.",
                      L"The Modes card shows which modes this track has the data for.");
        Track_Headline(L"Cannot build yet - 1 problem(s)", RS_COL_ERROR);
        Track_UpdateButtons();
        if (Rs_Automating())
            Rs_AutoLog(L"  check: cannot build yet, 1 problem(s) - no mode is ticked");
        return 0;
    }
    Track_Headline(L"Checking...", RS_COL_MUTED);
    started = Track_StartJob(page, TRACK_JOB_CHECK, a.v, a.n) != 0;
    Track_ArgsFree(&a);
    if (!started) {
        Track_StartFailed(L"Cannot build yet - 1 problem(s)");
        return 0;
    }
    return 1;
}

// Return: 1 = rldpack builds, 0 = refused (replacing declined), -1 = not possible.
static int Track_Build(HWND page)
{
    struct TrackArgs a;
    wchar_t path[TRACK_VAL];
    wchar_t *text;
    int started;

    if (!Track_CanBuild())
        return -1;
    text = Rs_GetText(g_track.out);
    Track_CleanPath(path, TRACK_VAL, text[0] ? text : g_track.checkedPath);
    Rs_Free(text);
    if (path[0] && Rs_FileExists(path)) {
        wchar_t question[TRACK_VAL + 64];
        swprintf(question, TRACK_VAL + 64, L"%ls exists. Replace it?", path);
        if (!Rs_AskYesNo(Rs_MainWindow(), L"Replace container?", question))
            return 0;
    }
    if (!Track_MakeArgs(&a, 0, path[0] ? path : NULL)) {
        Track_ArgsFree(&a);
        return -1;
    }
    Track_Headline(L"Building...", RS_COL_MUTED);
    started = Track_StartJob(page, TRACK_JOB_BUILD, a.v, a.n) != 0;
    Track_ArgsFree(&a);
    if (!started) {
        Track_StartFailed(L"Not built - 1 problem(s)");
        return -1;
    }
    return 1;
}

// A field has changed: check in 600 ms.
static void Track_Changed(HWND page)
{
    if (g_track.applying || !g_track.loaded[0])
        return;
    SetTimer(page, TRACK_TIMER_CHECK, TRACK_CHECK_DELAY, NULL);
    g_track.timer = 1;
    Track_UpdateButtons();
}

// End of loading or checking. Loading is a check without switches; what
// is reported for the first time (values, modes) goes into the fields, and then
// a check with all switches follows immediately.
static void Track_CheckDone(HWND page, int exitCode, int wasLoad)
{
    struct TrackJobData *j = &g_trackJob;
    int firstValues = !g_track.valuesKnown && j->anyValue;
    int firstModes = !g_track.modesKnown && j->anyMode;
    int again;

    if (wasLoad && Rs_Automating())
        Rs_AutoLog(L"  folder: lev %ls, vrm %ls, sca %ls, track.txt %ls",
                   Track_FileWord(TRACK_F_LEV), Track_FileWord(TRACK_F_VRM),
                   Track_FileWord(TRACK_F_SCA), Track_FileWord(TRACK_F_TXT));
    g_track.applying = 1;
    Track_ApplyFiles();
    Track_ApplyMap();
    Track_ApplyBots();
    if (firstValues) {
        Track_ApplyValues();
        g_track.valuesKnown = 1;
    }
    again = Track_ApplyLev();
    Track_AdvSummary();
    again |= Track_TakeModes();
    g_track.applying = 0;
    if ((firstValues || firstModes || again) && Track_Check(page) >= 0)
        return;
    Track_ShowCheckResult(exitCode);
}

static void Track_BuildDone(HWND page, int exitCode)
{
    struct TrackJobData *j = &g_trackJob;
    int ok = j->resultSeen && wcscmp(j->resultState, L"ok") == 0;
    wchar_t text[TRACK_VAL + 64];
    wchar_t detail[128];
    wchar_t size[32];

    Rs_MsgListClear(g_track.msgs);
    Track_EmptyText(L"rldpack reported nothing.");
    if (ok) {
        Track_Copy(g_track.built, TRACK_VAL, j->resultPath);
        g_track.builtBytes = j->resultBytes;
        Track_Copy(g_track.builtSha, 80, j->resultSha);
        Track_SizeText(size, 32, j->resultBytes < 0 ? 0 : j->resultBytes);
        swprintf(text, TRACK_VAL + 64, L"Container built: %ls (%ls)", Rs_PathName(g_track.built), size);
        Track_Headline(text, RS_COL_OK);
        swprintf(text, TRACK_VAL + 64, L"Built %ls", g_track.built);
        swprintf(detail, 128, L"SHA-256 %ls", g_track.builtSha);
        Rs_MsgListAdd(g_track.msgs, RS_SEV_OK, text, detail);
        Track_AddRunProblems(exitCode, 1);
        Track_AddMsgs(RS_SEV_WARNING);
        Track_AddMsgs(RS_SEV_NOTE);
        if (Rs_Automating())
            Rs_AutoLog(L"  build: ok %ls %lld bytes", g_track.built, g_track.builtBytes);
    } else {
        int errors;
        g_track.checked = 0;
        g_track.built[0] = 0;
        errors = Track_AddRunProblems(exitCode, 0);
        errors += Track_CountMsgs(RS_SEV_ERROR);
        Track_AddMsgs(RS_SEV_ERROR);
        Track_AddMsgs(RS_SEV_WARNING);
        Track_AddMsgs(RS_SEV_NOTE);
        swprintf(text, TRACK_VAL + 64, L"Not built - %d problem(s)", errors);
        Track_Headline(text, RS_COL_ERROR);
        if (Rs_Automating())
            Rs_AutoLog(L"  build: failed, %d error(s)", errors);
    }
    Track_Relayout(page);       // show or hide "Test in game" and "Show in folder"
    // Record the preview right after. jobId is already 0 here
    // (Track_JobDone), the new job can start; the messages from the build stay.
    if (ok)
        Track_Preview(page, 1);
}

// ---------------------------------------------------------------------------
// Preview: start the game once with --record-preview
// ---------------------------------------------------------------------------

// Like Test_FindBase on the test page (NativeAssets_Init in the game): folder of the
// exe, parent, grandparent; the first with assets\BIGFILE.BIG or assets\ctr-u.bin.
static int Track_FindBase(const wchar_t *exe, wchar_t *out, int cap)
{
    wchar_t dir[TRACK_VAL];
    wchar_t up[TRACK_VAL];
    wchar_t assets[TRACK_VAL];
    wchar_t probe[TRACK_VAL];
    int i;

    Rs_PathDir(dir, TRACK_VAL, exe);
    for (i = 0; i < 3 && dir[0]; i++) {
        Rs_PathJoin(assets, TRACK_VAL, dir, L"assets");
        Rs_PathJoin(probe, TRACK_VAL, assets, L"BIGFILE.BIG");
        if (!Rs_FileExists(probe))
            Rs_PathJoin(probe, TRACK_VAL, assets, L"ctr-u.bin");
        if (Rs_FileExists(probe)) {
            Track_Copy(out, cap, dir);
            return 1;
        }
        Rs_PathDir(up, TRACK_VAL, dir);
        if (wcscmp(up, dir) == 0)
            break;
        Track_Copy(dir, TRACK_VAL, up);
    }
    out[0] = 0;
    return 0;
}

// The game program as on the test page: the one entered there, otherwise
// what Rs_FindGameExe finds (the setting test.exe, then ctr_native.exe next
// to Reload Studio). 1 = found.
static int Track_GameExe(wchar_t *out, int cap)
{
    const wchar_t *page = Rs_TestGameExe();

    // First the game of the page Test - the same one it checked.
    if (page && page[0] && Rs_FileExists(page)) {
        Track_Copy(out, cap, page);
        return 1;
    }
    return Rs_FindGameExe(out, cap);
}

// Like Test_MakeLogPath, its own kind: a test run can run alongside. Every
// preview gets its own log, the last five are kept.
static void Track_PreviewLogPath(wchar_t *out, int cap)
{
    Rs_RotatedLogPath(out, cap, L"game-preview", 5);
}

// The preview did not work. Right after the build (previewAfterBuild) the
// container is built anyway: heading amber with "Container built - ", message
// as a warning. The build must never look failed because of the preview.
static void Track_PreviewFailed(const wchar_t *reason, const wchar_t *detail)
{
    wchar_t text[TRACK_VAL + 64];
    wchar_t head[TRACK_VAL + 96];
    int soft = g_track.previewAfterBuild;

    if (reason != g_track.previewReason)
        Track_Copy(g_track.previewReason, TRACK_VAL, reason);
    g_track.previewResult = TRACK_PREVIEW_FAILED;
    swprintf(text, TRACK_VAL + 64, L"Preview failed: %ls", reason);
    if (soft) {
        swprintf(head, TRACK_VAL + 96, L"Container built - preview failed: %ls", reason);
        Track_Headline(head, RS_COL_WARNING);
    } else {
        Track_Headline(text, RS_COL_ERROR);
    }
    Rs_MsgListAdd(g_track.msgs, soft ? RS_SEV_WARNING : RS_SEV_ERROR, text, detail);
    if (Rs_Automating())
        Rs_AutoLog(L"  preview: failed - %ls", reason);
}

// Right after the build something is missing for the preview (game not checked,
// no game data ...): the container is built, the preview is dropped.
// No error - heading amber, message as a warning.
static void Track_PreviewSkipped(const wchar_t *reason, const wchar_t *detail)
{
    wchar_t text[TRACK_VAL + 64];
    wchar_t size[32];

    Track_Copy(g_track.previewReason, TRACK_VAL, reason);
    g_track.previewResult = TRACK_PREVIEW_SKIPPED;
    Track_SizeText(size, 32, g_track.builtBytes < 0 ? 0 : g_track.builtBytes);
    swprintf(text, TRACK_VAL + 64, L"Container built: %ls (%ls) - preview skipped", Rs_PathName(g_track.built), size);
    Track_Headline(text, RS_COL_WARNING);
    swprintf(text, TRACK_VAL + 64, L"Preview skipped: %ls", reason);
    Rs_MsgListAdd(g_track.msgs, RS_SEV_WARNING, text, detail);
    if (Rs_Automating())
        Rs_AutoLog(L"  preview: skipped - %ls", reason);
}

// Preview not started at all: skipped after the build (skipDetail, NULL =
// detail), otherwise failed.
static void Track_PreviewNotStarted(const wchar_t *reason, const wchar_t *detail, const wchar_t *skipDetail)
{
    if (g_track.previewAfterBuild)
        Track_PreviewSkipped(reason, skipDetail ? skipDetail : detail);
    else
        Track_PreviewFailed(reason, detail);
    Track_UpdateButtons();
}

// Starts the game with the last built container. afterBuild = 1: right
// after "Build container" (Track_BuildDone); the messages and the output of
// rldpack stay, what is missing is "skipped", not an error.
// afterBuild = 0: automation "preview", records anew. Return: 1 = the
// game is running, 0 = not started (the message is already there).
static int Track_Preview(HWND page, int afterBuild)
{
    wchar_t full[TRACK_VAL];
    wchar_t dir[TRACK_VAL];
    wchar_t name[TRACK_VAL];
    wchar_t exe[TRACK_VAL];
    wchar_t base[TRACK_VAL];
    wchar_t *cmd;
    wchar_t *shown;
    size_t cap, n;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    DWORD len;

    // If a job is already running (rldpack or a preview), its display
    // stays; that only affects automation.
    if (g_track.jobId) {
        if (Rs_Automating())
            Rs_AutoLog(L"  preview: not started - rldpack or the game is still running");
        return 0;
    }
    g_track.previewAfterBuild = afterBuild;
    g_track.previewKilled = 0;
    if (!afterBuild) {
        Rs_MsgListClear(g_track.msgs);
        Track_EmptyText(L"The game reported nothing.");
    }
    g_track.previewResult = TRACK_PREVIEW_NONE;
    g_track.previewFrames = -1;
    g_track.previewPath[0] = 0;
    g_track.previewReason[0] = 0;
    g_track.previewStage = TRACK_STAGE_STARTED;
    g_track.previewCaptured = 0;
    g_track.previewTotal = TRACK_PREVIEW_FRAMES;
    g_track.previewHint[0] = 0;

    // Only the automation "preview" gets here without a build; without a build the
    // page does not know which container is meant.
    if (!g_track.built[0]) {
        Track_PreviewNotStarted(L"build the container first.",
                                L"The preview is recorded right after Build container, for the container "
                                L"built last in this session. Check and build the track, then try again.", NULL);
        return 0;
    }

    len = GetFullPathNameW(g_track.built, TRACK_VAL, full, NULL);
    if (len == 0 || len >= TRACK_VAL)
        Track_Copy(full, TRACK_VAL, g_track.built);
    if (!Rs_FileExists(full)) {
        Track_PreviewNotStarted(L"the container is not there any more.", full, NULL);
        return 0;
    }
    if (!Track_GameExe(exe, TRACK_VAL)) {
        Track_PreviewNotStarted(L"the game program (ctr_native.exe) was not found.",
                                RS_TEXT_NO_GAME_EXE, RS_TEXT_NO_GAME_EXE);
        return 0;
    }
    // Only a game from the same package: the page Test asks --version and
    // compares the build ID; its result applies here, an own question does not.
    // The reason is named by the page Test (Rs_TestGameProblem).
    if (!Rs_TestGameVerified(exe)) {
        wchar_t why[TRACK_VAL + 160];
        const wchar_t *reason = L"check the game on the Test page first.";
        if (afterBuild && Rs_TestGameProblem())
            reason = Rs_TestGameProblem();
        swprintf(why, TRACK_VAL + 160,
                 L"The Test page checks that the game comes from the same package as this "
                 L"Reload Studio. Game program: %ls", exe);
        Track_PreviewNotStarted(reason, why, L"Choose and check the game on the Test page, then build again.");
        return 0;
    }
    if (!Track_FindBase(exe, base, TRACK_VAL)) {
        Track_PreviewNotStarted(L"CTR Reload has no game data yet.", RS_TEXT_NO_GAME_DATA, NULL);
        return 0;
    }
    Rs_PathDir(dir, TRACK_VAL, full);
    // File name in the spelling on disk: the game compares with strcmp.
    h = FindFirstFileW(full, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        Track_Copy(name, TRACK_VAL, fd.cFileName);
        FindClose(h);
    } else {
        Track_Copy(name, TRACK_VAL, Rs_PathName(full));
    }
    Track_PreviewLogPath(g_track.previewLog, TRACK_VAL);

    cmd = Rs_Alloc(TRACK_CMD_CAP * sizeof(wchar_t));
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--dev");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--deterministic");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--settings-defaults");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--windowed");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"1280x540");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--aspect");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"43:18");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--tracks-dir");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, dir);
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--autoload-track");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, name);
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--record-preview");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, L"--log");
    Rs_AppendArg(cmd, TRACK_CMD_CAP, g_track.previewLog);

    // "<exe>" <command line> for the messages, like Test_CmdText
    cap = wcslen(exe) * 2 + wcslen(cmd) + 8;
    shown = Rs_Alloc(cap * sizeof(wchar_t));
    Rs_AppendArg(shown, cap, exe);
    n = wcslen(shown);
    shown[n] = L' ';
    wcscpy(shown + n + 1, cmd);

    // The raw output then shows what the game writes: after the build below
    // rldpack's output (blank line in between), otherwise alone. The data
    // of the last rldpack run (g_trackJob) stays.
    if (afterBuild) {
        Track_RawAppend(L"");
    } else {
        g_track.rawLen = 0;
        g_track.rawLines = 0;
        if (g_track.rawText)
            g_track.rawText[0] = 0;
    }
    Track_RawAppend(shown);

    g_track.jobKind = TRACK_JOB_PREVIEW;
    g_track.jobId = Rs_RunProcess(page, exe, cmd, base, 1);
    if (!g_track.jobId) {
        g_track.jobKind = TRACK_JOB_NONE;
        Track_PreviewFailed(L"the game could not be started.", shown);
    } else {
        // With --record-preview the game opens no visible window; the
        // AI drives the lap. If it hangs, TRACK_TIMER_PREVIEW ends it.
        SetTimer(page, TRACK_TIMER_PREVIEW, TRACK_PREVIEW_LIMIT, NULL);
        Track_Headline(afterBuild
                           ? L"Container built - recording the preview in the background (about 20 s)..."
                           : L"Recording the preview in the background (about 20 s)...",
                       RS_COL_MUTED);
        Rs_MsgListAdd(g_track.msgs, RS_SEV_INFO,
                      L"The game drives the track with the AI as an invisible driver; no window opens.",
                      L"It records one lap and ends by itself; the result shows up here.");
        Rs_MsgListAdd(g_track.msgs, RS_SEV_INFO, L"The game was started with this command line:", shown);
        if (Rs_Automating())
            Rs_AutoLog(L"  preview: game started for %ls", name);
    }
    Rs_Free(cmd);
    Rs_Free(shown);
    if (g_track.showRaw)
        Track_RawRefresh();
    Track_UpdateButtons();
    return g_track.jobId != 0;
}

// The game's final line: "[CTR Preview] written <path> (<n> frames)"
// or "[CTR Preview] FAILED: <reason>". The last one counts.
//
// Plus the lines on the way, for the case without a final line (ended
// early, crash, game from an older package without a final report):
//   "[CTR Debug] --autoload-track: <reason>"           track not loaded
//   "[CTR Debug] --autoload-track '<file>' jumps to"   track loads
//   "[CTR Preview] <name>: recording with the AI driver"  AI starts driving
//   "[CTR Preview] <name>: no nav path - ... recording with the path camera"
//                                                      path camera stands
//   (older games: "<name>: recording one lap from")
//   "[CTR Preview] recording from a <w>x<h> main target"  first frame
//   "[CTR Preview] <n> of <m> frames recorded"         progress
static void Track_PreviewLine(const wchar_t *line)
{
    static const wchar_t written[] = L"[CTR Preview] written ";
    static const wchar_t failed[] = L"[CTR Preview] FAILED: ";
    static const wchar_t autoFail[] = L"[CTR Debug] --autoload-track: ";
    static const wchar_t autoJump[] = L"[CTR Debug] --autoload-track '";
    static const wchar_t tag[] = L"[CTR Preview] ";
    static const wchar_t firstFrame[] = L"recording from a ";
    const wchar_t *p;
    int got, total;

    if ((p = wcsstr(line, autoFail)) != NULL) {
        Track_Copy(g_track.previewHint, TRACK_VAL, p + wcslen(autoFail));
        return;
    }
    if (wcsstr(line, autoJump) && wcsstr(line, L"' jumps to level id ")) {
        if (g_track.previewStage < TRACK_STAGE_LOADING)
            g_track.previewStage = TRACK_STAGE_LOADING;
        return;
    }
    if ((p = wcsstr(line, tag)) != NULL && !wcsstr(line, written) && !wcsstr(line, failed)) {
        p += wcslen(tag);
        if (wcsstr(p, L": recording with the AI driver ") ||
            wcsstr(p, L"recording with the path camera ") ||
            wcsstr(p, L": recording one lap from ")) {
            if (g_track.previewStage < TRACK_STAGE_CAMERA)
                g_track.previewStage = TRACK_STAGE_CAMERA;
        } else if (wcsncmp(p, firstFrame, wcslen(firstFrame)) == 0) {
            g_track.previewStage = TRACK_STAGE_RECORDING;
            if (g_track.previewCaptured < 1)
                g_track.previewCaptured = 1;
        } else if (swscanf(p, L"%d of %d frames recorded", &got, &total) == 2 && got >= 0 && total > 0) {
            g_track.previewStage = TRACK_STAGE_RECORDING;
            g_track.previewCaptured = got;
            g_track.previewTotal = total;
        }
        return;
    }

    if ((p = wcsstr(line, written)) != NULL) {
        const wchar_t *q, *end = NULL;
        size_t len;
        p += wcslen(written);
        // the last " (" - the path itself may contain one
        for (q = wcsstr(p, L" ("); q; q = wcsstr(q + 1, L" ("))
            end = q;
        len = end ? (size_t)(end - p) : wcslen(p);
        if (len >= TRACK_VAL)
            len = TRACK_VAL - 1;
        memcpy(g_track.previewPath, p, len * sizeof(wchar_t));
        g_track.previewPath[len] = 0;
        g_track.previewFrames = end ? _wtoi(end + 2) : -1;
        g_track.previewReason[0] = 0;
        g_track.previewResult = TRACK_PREVIEW_WRITTEN;
    } else if ((p = wcsstr(line, failed)) != NULL) {
        Track_Copy(g_track.previewReason, TRACK_VAL, p + wcslen(failed));
        g_track.previewPath[0] = 0;
        g_track.previewFrames = -1;
        g_track.previewResult = TRACK_PREVIEW_FAILED;
    }
}

static void Track_PreviewDone(HWND page, int exitCode)
{
    wchar_t text[TRACK_VAL + 64];
    wchar_t detail[TRACK_VAL + 160];
    DWORD code = (DWORD)exitCode;

    KillTimer(page, TRACK_TIMER_PREVIEW);
    // After the build its messages are still there; the result is added.
    if (!g_track.previewAfterBuild)
        Rs_MsgListClear(g_track.msgs);
    if (g_track.previewResult == TRACK_PREVIEW_WRITTEN) {
        if (g_track.previewAfterBuild) {
            swprintf(text, TRACK_VAL + 64, L"Container built, preview written: %ls", Rs_PathName(g_track.previewPath));
            Track_Headline(text, RS_COL_OK);
        }
        swprintf(text, TRACK_VAL + 64, L"Preview written: %ls", g_track.previewPath);
        if (!g_track.previewAfterBuild)
            Track_Headline(text, RS_COL_OK);
        if (g_track.previewFrames >= 0)
            swprintf(detail, TRACK_VAL + 160,
                     L"%d frames. Copy it to tracks\\vorschau\\ together with the container.",
                     g_track.previewFrames);
        else
            Track_Copy(detail, TRACK_VAL + 160, L"Copy it to tracks\\vorschau\\ together with the container.");
        Rs_MsgListAdd(g_track.msgs, RS_SEV_OK, text, detail);
        if (Rs_Automating())
            Rs_AutoLog(L"  preview: written %ls (%d frames)", g_track.previewPath, g_track.previewFrames);
    } else if (g_track.previewResult == TRACK_PREVIEW_FAILED) {
        if (g_track.previewHint[0])
            swprintf(detail, TRACK_VAL + 160, L"The game did not load the track: %ls. Game log: %ls",
                     g_track.previewHint, g_track.previewLog);
        else
            swprintf(detail, TRACK_VAL + 160, L"Game log: %ls", g_track.previewLog);
        Track_PreviewFailed(g_track.previewReason[0] ? g_track.previewReason : L"(no reason given)", detail);
    } else if (g_track.previewKilled) {
        // Emergency brake (TRACK_TIMER_PREVIEW): the invisible game ran too long.
        swprintf(detail, TRACK_VAL + 160,
                 L"The recording takes about 20 seconds; the game was stopped after 2 minutes. Game log: %ls",
                 g_track.previewLog);
        Track_PreviewFailed(L"the game did not finish within 2 minutes and was stopped.", detail);
    } else {
        // No final line. The reason comes from what the game said on the way
        // (Track_PreviewLine) and from the exit code. A game that has the
        // final report in platform/native_preview.c writes a FAILED line itself
        // on closing; older games, crashes and a game that never loaded the
        // track end up here.
        int crashed = code >= 0xC0000000u;
        wchar_t exitText[48];
        wchar_t where[96];

        if (crashed)
            swprintf(exitText, 48, L"exit 0x%08lX", code);
        else
            swprintf(exitText, 48, L"exit %lu", code);

        if (g_track.previewStage >= TRACK_STAGE_RECORDING)
            swprintf(where, 96, L"after %d of %d frames", g_track.previewCaptured, g_track.previewTotal);
        else if (g_track.previewStage == TRACK_STAGE_CAMERA)
            Track_Copy(where, 96, L"before the recording started");
        else if (g_track.previewStage == TRACK_STAGE_LOADING)
            Track_Copy(where, 96, L"while the track was loading");
        else
            Track_Copy(where, 96, L"before the track was loaded");

        if (g_track.previewHint[0]) {
            swprintf(text, TRACK_VAL + 64, L"the game did not load the track: %ls", g_track.previewHint);
            swprintf(detail, TRACK_VAL + 160,
                     L"The game was started with --autoload-track and stayed in the main menu (%ls). Game log: %ls",
                     exitText, g_track.previewLog);
        } else if (crashed) {
            swprintf(text, TRACK_VAL + 64, L"the game crashed %ls (%ls).", where, exitText);
            swprintf(detail, TRACK_VAL + 160, L"The end of the game log says where. Game log: %ls", g_track.previewLog);
        } else if (g_track.previewStage >= TRACK_STAGE_LOADING) {
            // The game has no visible window; nobody "closed" it,
            // it only ended before the end.
            if (g_track.previewStage >= TRACK_STAGE_RECORDING)
                swprintf(text, TRACK_VAL + 64, L"the game ended before the preview was finished (%d of %d frames).",
                         g_track.previewCaptured, g_track.previewTotal);
            else
                swprintf(text, TRACK_VAL + 64, L"the game ended %ls - no preview was written.", where);
            swprintf(detail, TRACK_VAL + 160,
                     L"The game records in the background and ends by itself once the preview is written; "
                     L"this time it ended earlier. (%ls) Game log: %ls",
                     exitText, g_track.previewLog);
        } else {
            swprintf(text, TRACK_VAL + 64, L"the game ended %ls without a result (%ls).", where, exitText);
            swprintf(detail, TRACK_VAL + 160,
                     L"Click \"Show rldpack output\" to see what the game printed. Game log: %ls", g_track.previewLog);
        }
        Track_PreviewFailed(text, detail);
    }
    g_track.previewKilled = 0;
}

static void Track_JobDone(HWND page, int exitCode)
{
    int kind = g_track.jobKind;

    g_track.jobId = 0;
    g_track.jobKind = TRACK_JOB_NONE;
    if (g_track.showRaw)
        Track_RawRefresh();
    if (kind == TRACK_JOB_PREVIEW)
        Track_PreviewDone(page, exitCode);
    else if (kind == TRACK_JOB_BUILD)
        Track_BuildDone(page, exitCode);
    else
        Track_CheckDone(page, exitCode, kind == TRACK_JOB_LOAD);
    Track_UpdateButtons();
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

static void Track_OpenTest(void)
{
    HWND test;
    wchar_t *path;

    if (!g_track.built[0])
        return;
    Rs_ShowPage(RS_PAGE_TEST);
    test = Rs_PageWindow(RS_PAGE_TEST);
    path = Rs_Dup(g_track.built);
    if (!test || !PostMessageW(test, RS_WM_OPEN_TEST, 0, (LPARAM)path))
        Rs_Free(path);
}

static void Track_ShowInFolder(void)
{
    wchar_t params[TRACK_VAL + 16];
    if (!g_track.built[0])
        return;
    swprintf(params, TRACK_VAL + 16, L"/select,\"%ls\"", g_track.built);
    ShellExecuteW(NULL, L"open", L"explorer.exe", params, NULL, SW_SHOWNORMAL);
}

static void Track_BrowseFolder(HWND page)
{
    wchar_t *current = Rs_GetText(g_track.folder);
    wchar_t pick[TRACK_VAL];
    if (Rs_BrowseFolder(Rs_MainWindow(), L"Choose the track folder", current, pick, TRACK_VAL))
        Track_Load(page, pick);
    Rs_Free(current);
}

static void Track_BrowseOut(void)
{
    wchar_t *current = Rs_GetText(g_track.out);
    wchar_t pick[TRACK_VAL];
    if (Rs_BrowseSaveFile(Rs_MainWindow(), L"Save the track container as",
                          L"Track containers (*.rldtrack)\0*.rldtrack\0\0", L"rldtrack",
                          current[0] ? current : g_track.loaded, pick, TRACK_VAL))
        Rs_SetText(g_track.out, pick);      // EN_CHANGE schedules the check
    Rs_Free(current);
}

// Enter (IDOK): in the folder field load, on a button press it, otherwise
// check immediately.
static void Track_Enter(HWND page)
{
    HWND focus = GetFocus();
    if (focus == g_track.folder) {
        wchar_t *text = Rs_GetText(g_track.folder);
        Track_Load(page, text);
        Rs_Free(text);
    } else if (focus == g_track.folderBrowse || focus == g_track.outBrowse || focus == g_track.check ||
               focus == g_track.build || focus == g_track.view || focus == g_track.test ||
               focus == g_track.show) {
        SendMessageW(focus, BM_CLICK, 0, 0);
    } else if (g_track.loaded[0] && focus && GetParent(focus) == page) {
        Track_Check(page);
    }
}

// ---------------------------------------------------------------------------
// Report (automation "report")
// ---------------------------------------------------------------------------

static void Track_Put(FILE *f, const wchar_t *fmt, ...)
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

static void Track_PutText(FILE *f, const wchar_t *what, HWND control)
{
    wchar_t *text = Rs_GetText(control);
    Track_Put(f, L"%ls: %ls", what, text);
    Rs_Free(text);
}

static const wchar_t *Track_EnabledWord(HWND h)
{
    return IsWindowEnabled(h) ? L"enabled" : L"greyed out";
}

static int Track_WriteReport(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"wb");
    wchar_t *text;
    int i;

    if (!f)
        return 0;
    Track_Put(f, L"Reload Studio - Track page");
    Track_PutText(f, L"folder field", g_track.folder);
    Track_Put(f, L"loaded folder: %ls", g_track.loaded[0] ? g_track.loaded : L"(none)");
    for (i = 0; i < TRACK_FILES; i++) {
        text = Rs_GetText(g_track.fileValue[i]);
        Track_Put(f, L"file %ls: %ls [%ls]", g_trackFileKinds[i], text, Track_ColorName(g_track.fileColor[i]));
        Rs_Free(text);
    }
    text = Rs_GetText(g_track.mapValue);
    Track_Put(f, L"minimap: %ls [%ls]", text, Track_ColorName(g_track.mapColor));
    Rs_Free(text);
    text = Rs_GetText(g_track.botValue);
    Track_Put(f, L"bot data: %ls [%ls]", text, Track_ColorName(g_track.botColor));
    Rs_Free(text);
    text = Rs_GetText(g_track.advSummary);
    Track_Put(f, L"advanced: %ls - %ls", g_track.advOpen ? L"open" : L"closed", text);
    Rs_Free(text);
    Track_PutText(f, L"name", g_track.name);
    Track_PutText(f, L"author", g_track.author);
    Track_PutText(f, L"version", g_track.version);
    for (i = 0; i < TRACK_C_COUNT; i++) {
        text = Rs_GetText(g_track.combo[i]);
        Track_Put(f, L"%ls: %ls (passed as %ls, %ls)", g_trackComboVerbs[i], text, Track_ComboValue(i),
                  Track_EnabledWord(g_track.combo[i]));
        Rs_Free(text);
    }
    text = Rs_GetText(g_track.ambientNote);
    Track_Put(f, L"ambient note: %ls [%ls]", text, Track_ColorName(g_track.ambientColor));
    Rs_Free(text);
    text = Rs_GetText(g_track.music);
    Track_Put(f, L"music: %ls (%ls, %ls)", text, Track_IsChecked(g_track.music) ? L"ticked" : L"not ticked",
              Track_EnabledWord(g_track.music));
    Rs_Free(text);
    for (i = 0; i < TRACK_MODES; i++) {
        text = Rs_GetText(g_track.modeNote[i]);
        Track_Put(f, L"mode %ls: %ls, %ls - %ls [%ls]", g_trackModeWords[i],
                  Track_IsChecked(g_track.mode[i]) ? L"ticked" : L"not ticked",
                  Track_EnabledWord(g_track.mode[i]), text, Track_ColorName(g_track.modeColor[i]));
        Rs_Free(text);
    }
    Track_PutText(f, L"output", g_track.out);
    text = Rs_GetText(g_track.headline);
    Track_Put(f, L"headline: %ls [%ls]", text, Track_ColorName(g_track.headColor));
    Rs_Free(text);
    Track_Put(f, L"button Check: %ls", Track_EnabledWord(g_track.check));
    Track_Put(f, L"button Build container: %ls", Track_EnabledWord(g_track.build));
    Track_Put(f, L"buttons Test in game / Show in folder: %ls",
              Track_IsShown(g_track.test) ? L"shown" : L"hidden");
    if (g_track.built[0])
        Track_Put(f, L"last built: %ls, %lld bytes, SHA-256 %ls", g_track.built, g_track.builtBytes,
                  g_track.builtSha);
    else
        Track_Put(f, L"last built: (none)");
    if (g_track.jobId && g_track.jobKind == TRACK_JOB_PREVIEW)
        Track_Put(f, L"last preview: recording");
    else if (g_track.previewResult == TRACK_PREVIEW_WRITTEN)
        Track_Put(f, L"last preview: written %ls", g_track.previewPath);
    else if (g_track.previewResult == TRACK_PREVIEW_FAILED)
        Track_Put(f, L"last preview: failed %ls", g_track.previewReason);
    else if (g_track.previewResult == TRACK_PREVIEW_SKIPPED)
        Track_Put(f, L"last preview: skipped %ls", g_track.previewReason);
    else
        Track_Put(f, L"last preview: (none)");
    Track_Put(f, L"view: %ls", g_track.showRaw ? L"rldpack output" : L"messages");
    for (i = 0; i < g_trackJob.levCount; i++)
        Track_Put(f, L"lev %ls: %ls", g_trackJob.levKey[i], g_trackJob.levValue[i]);
    Track_Put(f, L"messages: %d", Rs_MsgListCount(g_track.msgs));
    Rs_MsgListWrite(g_track.msgs, f);
    Track_Put(f, L"rldpack output of the last run: %d line(s)", g_track.rawLines);
    if (g_track.rawText && g_track.rawLen) {
        char *utf8 = Rs_ToUtf8(g_track.rawText);
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
// Automation: the same paths as typing and clicking
// ---------------------------------------------------------------------------

static int Track_AutoText(HWND edit, const wchar_t *verb, const wchar_t *arg)
{
    Rs_SetText(edit, arg);      // EN_CHANGE schedules the check as when typing
    Rs_AutoLog(L"  %ls: %ls", verb, arg);
    return RS_AUTO_WAIT;
}

static int Track_AutoCombo(HWND page, int c, const wchar_t *arg)
{
    HWND cb = g_track.combo[c];
    wchar_t *text;

    if (!IsWindowEnabled(cb)) {
        wchar_t *why;
        if (!arg[0] || _wcsicmp(arg, L"default") == 0) {
            Rs_AutoLog(L"  %ls: stays on the game default", g_trackComboVerbs[c]);
            return RS_AUTO_DONE;
        }
        why = Rs_GetText(g_track.ambientNote);
        Rs_AutoLog(L"  %ls: cannot be changed - %ls", g_trackComboVerbs[c],
                   why[0] ? why : L"the choice is greyed out");
        Rs_Free(why);
        return RS_AUTO_FAIL;
    }
    if (Track_ComboSelect(c, arg, 0) < 0) {
        Rs_AutoLog(L"  %ls: '%ls' is not one of the choices", g_trackComboVerbs[c], arg);
        return RS_AUTO_FAIL;
    }
    SendMessageW(page, WM_COMMAND, MAKEWPARAM(TRACK_ID_COMBO + c, CBN_SELCHANGE), (LPARAM)cb);
    text = Rs_GetText(cb);
    Rs_AutoLog(L"  %ls: %ls", g_trackComboVerbs[c], text);
    Rs_Free(text);
    return RS_AUTO_WAIT;
}

// Tick exactly these modes. A mode without data cannot be ticked.
static int Track_AutoModes(HWND page, const wchar_t *arg)
{
    int want[TRACK_MODES] = { 0 };
    wchar_t word[64];
    wchar_t list[80];
    const wchar_t *p = arg;
    int i;

    while ((p = Track_NextWord(p, word, 64)) != NULL) {
        int m = Track_ModeIndex(word);
        if (_wcsicmp(word, L"none") == 0)
            continue;
        if (m < 0) {
            Rs_AutoLog(L"  modes: '%ls' is not a mode - use race, time, ctr, crystal, battle", word);
            return RS_AUTO_FAIL;
        }
        want[m] = 1;
    }
    for (i = 0; i < TRACK_MODES; i++) {
        if (want[i] && !IsWindowEnabled(g_track.mode[i])) {
            wchar_t *why = Rs_GetText(g_track.modeNote[i]);
            Rs_AutoLog(L"  modes: %ls cannot be ticked - %ls", g_trackModeLabels[i], why);
            Rs_Free(why);
            return RS_AUTO_FAIL;
        }
    }
    list[0] = 0;
    for (i = 0; i < TRACK_MODES; i++) {
        if (want[i]) {
            if (list[0])
                Track_Append(list, 80, L",");
            Track_Append(list, 80, g_trackModeWords[i]);
        }
        if (Track_IsChecked(g_track.mode[i]) != want[i]) {
            Track_SetCheck(g_track.mode[i], want[i]);
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(TRACK_ID_MODE + i, BN_CLICKED), (LPARAM)g_track.mode[i]);
        }
    }
    Rs_AutoLog(L"  modes: %ls", list[0] ? list : L"none ticked");
    return RS_AUTO_WAIT;
}

static int Track_AutoMusic(HWND page, const wchar_t *arg)
{
    int on;
    if (_wcsicmp(arg, L"on") == 0) {
        on = 1;
    } else if (_wcsicmp(arg, L"off") == 0) {
        on = 0;
    } else {
        Rs_AutoLog(L"  music: say on or off");
        return RS_AUTO_FAIL;
    }
    if (!IsWindowEnabled(g_track.music)) {
        Rs_AutoLog(L"  music: cannot be changed - no music file (.sca or .sndb) in the folder");
        return RS_AUTO_FAIL;
    }
    Track_SetCheck(g_track.music, on);
    SendMessageW(page, WM_COMMAND, MAKEWPARAM(TRACK_ID_MUSIC, BN_CLICKED), (LPARAM)g_track.music);
    Rs_AutoLog(L"  music: %ls", on ? L"on" : L"off");
    return RS_AUTO_WAIT;
}

// ---------------------------------------------------------------------------
// Callbacks of the page
// ---------------------------------------------------------------------------

static void Track_Create(HWND page)
{
    wchar_t folder[TRACK_VAL];
    int i;

    g_track.folder = Rs_Edit(page, TRACK_ID_FOLDER, L"", 0);
    g_track.folderBrowse = Rs_Button(page, TRACK_ID_FOLDER_BROWSE, L"Browse...");
    for (i = 0; i < TRACK_FILES; i++) {
        g_track.fileLabel[i] = Rs_Label(page, TRACK_ID_FILE_LABEL + i, g_trackFileLabels[i], RS_FONT_BOLD);
        g_track.fileValue[i] = Rs_Label(page, TRACK_ID_FILE_VALUE + i, L"-", RS_FONT_BODY);
        Track_Ellipsis(g_track.fileValue[i]);
    }
    g_track.mapLabel = Rs_Label(page, TRACK_ID_MAP_LABEL, L"Minimap", RS_FONT_BOLD);
    g_track.mapValue = Rs_Label(page, TRACK_ID_MAP_VALUE, L"-", RS_FONT_BODY);
    Track_Ellipsis(g_track.mapValue);
    g_track.botLabel = Rs_Label(page, TRACK_ID_BOT_LABEL, L"Bot data", RS_FONT_BOLD);
    g_track.botValue = Rs_Label(page, TRACK_ID_BOT_VALUE, L"-", RS_FONT_BODY);
    Track_Ellipsis(g_track.botValue);
    g_track.nameLabel = Rs_Label(page, TRACK_ID_NAME_LABEL, L"Name", RS_FONT_BOLD);
    g_track.name = Rs_Edit(page, TRACK_ID_NAME, L"", 0);
    SendMessageW(g_track.name, EM_LIMITTEXT, 64, 0);
    g_track.authorLabel = Rs_Label(page, TRACK_ID_AUTHOR_LABEL, L"Author", RS_FONT_BOLD);
    g_track.author = Rs_Edit(page, TRACK_ID_AUTHOR, L"", 0);
    SendMessageW(g_track.author, EM_LIMITTEXT, 64, 0);
    g_track.versionLabel = Rs_Label(page, TRACK_ID_VERSION_LABEL, L"Version", RS_FONT_BOLD);
    g_track.version = Rs_Edit(page, TRACK_ID_VERSION, L"", ES_NUMBER);
    SendMessageW(g_track.version, EM_LIMITTEXT, 9, 0);

    for (i = 0; i < TRACK_C_COUNT; i++) {
        g_track.comboLabel[i] = Rs_Label(page, TRACK_ID_COMBO_LABEL + i, g_trackComboLabels[i], RS_FONT_BOLD);
        g_track.advHelp[i] = Rs_Label(page, TRACK_ID_ADV_HELP + i, g_trackComboHelp[i], RS_FONT_SMALL);
        Rs_SetTextColor(g_track.advHelp[i], RS_COL_MUTED);
        g_track.combo[i] = Rs_Combo(page, TRACK_ID_COMBO + i);
        if (i == TRACK_C_AMBIENT)
            g_track.ambientNote = Rs_Label(page, TRACK_ID_AMBIENT_NOTE, L"", RS_FONT_SMALL);
    }
    g_track.music = Rs_Check(page, TRACK_ID_MUSIC, L"Use the music in the folder");
    g_track.advanced = Rs_Button(page, TRACK_ID_ADVANCED, L"");
    g_track.advSummary = Rs_Label(page, TRACK_ID_ADV_SUMMARY, L"", RS_FONT_SMALL);
    Rs_SetTextColor(g_track.advSummary, RS_COL_MUTED);
    Track_Ellipsis(g_track.advSummary);
    g_track.advOpen = 0;

    for (i = 0; i < TRACK_MODES; i++) {
        g_track.mode[i] = Rs_Check(page, TRACK_ID_MODE + i, g_trackModeLabels[i]);
        g_track.modeNote[i] = Rs_Label(page, TRACK_ID_MODE_NOTE + i, L"", RS_FONT_SMALL);
    }

    g_track.view = Rs_Button(page, TRACK_ID_VIEW, L"Show rldpack output");
    g_track.outLabel = Rs_Label(page, TRACK_ID_OUT_LABEL, L"Output", RS_FONT_BOLD);
    g_track.out = Rs_Edit(page, TRACK_ID_OUT, L"", 0);
    g_track.outBrowse = Rs_Button(page, TRACK_ID_OUT_BROWSE, L"Browse...");
    g_track.check = Rs_Button(page, TRACK_ID_CHECK, L"Check");
    g_track.build = Rs_PrimaryButton(page, TRACK_ID_BUILD, L"Build container");
    g_track.headline = Rs_Label(page, TRACK_ID_HEADLINE, L"", RS_FONT_BOLD);
    Track_Ellipsis(g_track.headline);
    g_track.msgs = Rs_MsgList(page, TRACK_ID_MESSAGES);
    g_track.raw = Rs_Edit(page, TRACK_ID_RAW, L"",
                          ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
    SendMessageW(g_track.raw, WM_SETFONT, (WPARAM)Rs_Font(RS_FONT_MONO), FALSE);
    SendMessageW(g_track.raw, EM_LIMITTEXT, 0, 0);
    ShowWindow(g_track.raw, SW_HIDE);
    g_track.test = Rs_Button(page, TRACK_ID_TEST, L"Test in game");
    g_track.show = Rs_Button(page, TRACK_ID_SHOW, L"Show in folder");
    ShowWindow(g_track.test, SW_HIDE);
    ShowWindow(g_track.show, SW_HIDE);

    Track_ResetState();
    Track_AdvShow();
    Track_Headline(L"Choose a track folder to start", RS_COL_MUTED);
    Track_EmptyText(L"Choose a track folder. What rldpack finds shows up here.");
    Track_ApplyModes();
    Track_UpdateButtons();

    // Only enter the last folder; it is loaded on first showing.
    Rs_ConfigGet(L"track.folder", folder, TRACK_VAL);
    Rs_SetText(g_track.folder, folder);
}

static void Track_PlaceField(HWND label, HWND edit, int x, int labelW, int y, int editW)
{
    MoveWindow(label, x, y + Rs_Px(4), labelW, Rs_Px(20), TRUE);
    MoveWindow(edit, x + labelW + Rs_Px(8), y, editW, Rs_Px(28), TRUE);
}

static void Track_Layout(HWND page, int w, int h)
{
    int left = Rs_Px(32), right = w - Rs_Px(32);
    int top = Rs_PageTop(), bottom = h - Rs_Px(24);
    int gap = Rs_Px(16);
    int leftW = (right - left - gap) * 52 / 100;
    int browseW = Rs_Px(100), labelW = Rs_Px(140);
    int built = g_track.built[0] != 0;
    // The left column needs 616 px. If the page is lower (the shell
    // limits the window size instead of the client size), it moves closer together.
    int tight = bottom - top < Rs_Px(616);
    int rowGap = Rs_Px(tight ? 6 : 10);
    int filePitch = Rs_Px(tight ? 20 : 22);
    int fieldPitch = Rs_Px(tight ? 32 : 36);
    int labelPitch = Rs_Px(tight ? 20 : 22);
    int groupGap = Rs_Px(tight ? 4 : 10);
    RECT card, in;
    int x, y, i, width, listBottom;

    Rs_CardClear(page);

    // Top left: folder, files, name, author, version
    card.left = left;
    card.top = top;
    card.right = left + leftW;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    y = in.top;
    MoveWindow(g_track.folder, in.left, y + Rs_Px(2), width - browseW - Rs_Px(8), Rs_Px(28), TRUE);
    MoveWindow(g_track.folderBrowse, in.right - browseW, y, browseW, Rs_Px(32), TRUE);
    y += Rs_Px(32) + rowGap;
    x = in.left + labelW + Rs_Px(8);
    for (i = 0; i < TRACK_FILES; i++) {
        MoveWindow(g_track.fileLabel[i], in.left, y, labelW, Rs_Px(20), TRUE);
        MoveWindow(g_track.fileValue[i], x, y, in.right - x, Rs_Px(20), TRUE);
        y += filePitch;
    }
    MoveWindow(g_track.mapLabel, in.left, y, labelW, Rs_Px(20), TRUE);
    MoveWindow(g_track.mapValue, x, y, in.right - x, Rs_Px(20), TRUE);
    y += filePitch;
    MoveWindow(g_track.botLabel, in.left, y, labelW, Rs_Px(20), TRUE);
    MoveWindow(g_track.botValue, x, y, in.right - x, Rs_Px(20), TRUE);
    y += filePitch;
    y += rowGap + Rs_Px(2);
    Track_PlaceField(g_track.nameLabel, g_track.name, in.left, labelW, y, in.right - x);
    y += fieldPitch;
    Track_PlaceField(g_track.authorLabel, g_track.author, in.left, labelW, y, in.right - x);
    y += fieldPitch;
    Track_PlaceField(g_track.versionLabel, g_track.version, in.left, labelW, y, Rs_Px(80));
    y += Rs_Px(28);
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Track");

    // Bottom left: music, below it "Advanced" (reverb, bots, ambient sound),
    // collapsed by default. Per field the label,
    // an explaining sentence, then the field.
    card.top = card.bottom + gap;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    y = in.top;
    MoveWindow(g_track.music, in.left, y, width, Rs_Px(24), TRUE);
    y += Rs_Px(24) + rowGap;
    MoveWindow(g_track.advanced, in.left, y, Rs_Px(132), Rs_Px(30), TRUE);
    y += Rs_Px(30) + Rs_Px(6);
    MoveWindow(g_track.advSummary, in.left, y, width, Rs_Px(18), TRUE);
    // Label and sentence in one line, otherwise the card is not enough for
    // all three fields at 820 px window height.
    for (i = 0; i < TRACK_C_COUNT; i++) {
        int helpX = Rs_Px(118);
        MoveWindow(g_track.comboLabel[i], in.left, y, helpX, Rs_Px(20), TRUE);
        MoveWindow(g_track.advHelp[i], in.left + helpX, y + Rs_Px(2), width - helpX, Rs_Px(18), TRUE);
        y += labelPitch;
        MoveWindow(g_track.combo[i], in.left, y, width, Rs_Px(300), TRUE);
        y += Rs_Px(28);
        if (i == TRACK_C_AMBIENT) {
            MoveWindow(g_track.ambientNote, in.left, y + Rs_Px(2), width, Rs_Px(18), TRUE);
            y += labelPitch;
        }
        y += groupGap;
    }
    Rs_CardAdd(page, &card, L"Sound");

    // Top right: modes, note to the right of the box (up to two lines)
    card.left = left + leftW + gap;
    card.right = right;
    card.top = top;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    y = in.top;
    for (i = 0; i < TRACK_MODES; i++) {
        int boxW = Rs_Px(140);
        MoveWindow(g_track.mode[i], in.left, y, boxW, Rs_Px(24), TRUE);
        MoveWindow(g_track.modeNote[i], in.left + boxW + Rs_Px(8), y + Rs_Px(4),
                   width - boxW - Rs_Px(8), Rs_Px(32), TRUE);
        y += Rs_Px(36);
    }
    card.bottom = y + Rs_Px(16);
    Rs_CardAdd(page, &card, L"Modes");

    // Bottom right: build; the toggle is in the title line of the card
    card.top = card.bottom + gap;
    card.bottom = bottom;
    in = Rs_CardInner(&card, 1);
    width = in.right - in.left;
    MoveWindow(g_track.view, in.right - Rs_Px(168), card.top + Rs_Px(10), Rs_Px(168), Rs_Px(30), TRUE);
    y = in.top;
    MoveWindow(g_track.outLabel, in.left, y + Rs_Px(6), Rs_Px(64), Rs_Px(20), TRUE);
    MoveWindow(g_track.out, in.left + Rs_Px(72), y + Rs_Px(2), width - Rs_Px(72) - browseW - Rs_Px(8),
               Rs_Px(28), TRUE);
    MoveWindow(g_track.outBrowse, in.right - browseW, y, browseW, Rs_Px(32), TRUE);
    y += Rs_Px(44);
    MoveWindow(g_track.check, in.left, y, Rs_Px(96), Rs_Px(32), TRUE);
    MoveWindow(g_track.build, in.left + Rs_Px(104), y, Rs_Px(156), Rs_Px(32), TRUE);
    y += Rs_Px(44);
    MoveWindow(g_track.headline, in.left, y, width, Rs_Px(22), TRUE);
    y += Rs_Px(28);
    listBottom = in.bottom - (built ? Rs_Px(44) : 0);
    if (listBottom < y + Rs_Px(40))
        listBottom = y + Rs_Px(40);
    MoveWindow(g_track.msgs, in.left, y, width, listBottom - y, TRUE);
    MoveWindow(g_track.raw, in.left, y, width, listBottom - y, TRUE);
    MoveWindow(g_track.test, in.left, in.bottom - Rs_Px(32), Rs_Px(128), Rs_Px(32), TRUE);
    MoveWindow(g_track.show, in.left + Rs_Px(136), in.bottom - Rs_Px(32), Rs_Px(136), Rs_Px(32), TRUE);
    ShowWindow(g_track.test, built ? SW_SHOW : SW_HIDE);
    ShowWindow(g_track.show, built ? SW_SHOW : SW_HIDE);
    Rs_CardAdd(page, &card, L"Build");

    // After a DPI change the shell sets the base font; the raw output
    // stays in a fixed-width font, though.
    if ((HFONT)SendMessageW(g_track.raw, WM_GETFONT, 0, 0) != Rs_Font(RS_FONT_MONO))
        SendMessageW(g_track.raw, WM_SETFONT, (WPARAM)Rs_Font(RS_FONT_MONO), TRUE);
}

static LRESULT Track_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);
    (void)lParam;

    if (id >= TRACK_ID_MODE && id < TRACK_ID_MODE + TRACK_MODES) {
        if (code == BN_CLICKED)
            Track_Changed(page);
        return 0;
    }
    if (id >= TRACK_ID_COMBO && id < TRACK_ID_COMBO + TRACK_C_COUNT) {
        if (code == CBN_SELCHANGE) {
            Track_AdvSummary();
            Track_Changed(page);
        }
        return 0;
    }
    if (id == TRACK_ID_ADVANCED) {
        if (code == BN_CLICKED) {
            g_track.advOpen = !g_track.advOpen;
            Track_AdvShow();
            Track_Relayout(page);
        }
        return 0;
    }
    switch (id) {
    case IDOK:
        Track_Enter(page);
        break;
    case IDCANCEL:
        break;
    case TRACK_ID_FOLDER_BROWSE:
        if (code == BN_CLICKED)
            Track_BrowseFolder(page);
        break;
    case TRACK_ID_NAME:
    case TRACK_ID_AUTHOR:
    case TRACK_ID_VERSION:
    case TRACK_ID_OUT:
        if (code == EN_CHANGE)
            Track_Changed(page);
        break;
    case TRACK_ID_MUSIC:
        if (code == BN_CLICKED)
            Track_Changed(page);
        break;
    case TRACK_ID_OUT_BROWSE:
        if (code == BN_CLICKED)
            Track_BrowseOut();
        break;
    case TRACK_ID_CHECK:
        if (code == BN_CLICKED)
            Track_Check(page);
        break;
    case TRACK_ID_BUILD:
        if (code == BN_CLICKED)
            Track_Build(page);
        break;
    case TRACK_ID_VIEW:
        if (code == BN_CLICKED) {
            g_track.showRaw = !g_track.showRaw;
            Track_ShowView();
        }
        break;
    case TRACK_ID_TEST:
        if (code == BN_CLICKED)
            Track_OpenTest();
        break;
    case TRACK_ID_SHOW:
        if (code == BN_CLICKED)
            Track_ShowInFolder();
        break;
    }
    return 0;
}

static LRESULT Track_Notify(HWND page, NMHDR *hdr)
{
    (void)page;
    (void)hdr;
    return 0;
}

static LRESULT Track_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    switch (msg) {
    case RS_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        if (line && g_track.jobId && (int)wParam == g_track.jobId) {
            Track_RawAppend(line);
            if (g_track.jobKind == TRACK_JOB_PREVIEW)
                Track_PreviewLine(line);
            else
                Track_ParseLine(line);
        }
        Rs_Free(line);
        *handled = 1;
        return 0;
    }
    case RS_WM_JOB_DONE:
        if (g_track.jobId && (int)wParam == g_track.jobId)
            Track_JobDone(page, (int)lParam);
        *handled = 1;
        return 0;
    case WM_TIMER:
        // Emergency brake: the preview game (without a window) is still running after
        // TRACK_PREVIEW_LIMIT. End it; RS_WM_JOB_DONE arrives as usual,
        // Track_PreviewDone reports it.
        if (wParam == TRACK_TIMER_PREVIEW) {
            KillTimer(page, TRACK_TIMER_PREVIEW);
            if (g_track.jobId && g_track.jobKind == TRACK_JOB_PREVIEW && Rs_KillJob(g_track.jobId))
                g_track.previewKilled = 1;
            *handled = 1;
            return 0;
        }
        if (wParam != TRACK_TIMER_CHECK)
            return 0;
        *handled = 1;
        // Loading or building is running: the timer keeps running and asks again later
        if (g_track.jobId && g_track.jobKind != TRACK_JOB_CHECK)
            return 0;
        Track_Check(page);
        return 0;
    case RS_WM_PAGE_SHOWN:
        if (!g_track.shown) {
            wchar_t *folder = Rs_GetText(g_track.folder);
            g_track.shown = 1;
            if (folder[0] && !g_track.loaded[0] && Rs_DirExists(folder))
                Track_Load(page, folder);
            Rs_Free(folder);
        }
        *handled = 1;
        return 0;
    }
    return 0;
}

static int Track_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    int c, r;

    if (wcscmp(verb, L"folder") == 0) {
        if (Track_Load(page, arg))
            return RS_AUTO_WAIT;
        Rs_AutoLog(L"  folder: %ls", arg[0] ? L"rldpack could not be started" : L"no folder given");
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"name") == 0)
        return Track_AutoText(g_track.name, verb, arg);
    if (wcscmp(verb, L"author") == 0)
        return Track_AutoText(g_track.author, verb, arg);
    if (wcscmp(verb, L"version") == 0)
        return Track_AutoText(g_track.version, verb, arg);
    if (wcscmp(verb, L"out") == 0)
        return Track_AutoText(g_track.out, verb, arg);
    if (wcscmp(verb, L"modes") == 0)
        return Track_AutoModes(page, arg);
    for (c = 0; c < TRACK_C_COUNT; c++)
        if (wcscmp(verb, g_trackComboVerbs[c]) == 0)
            return Track_AutoCombo(page, c, arg);
    if (wcscmp(verb, L"music") == 0)
        return Track_AutoMusic(page, arg);
    if (wcscmp(verb, L"check") == 0) {
        r = Track_Check(page);
        if (r > 0)
            return RS_AUTO_WAIT;
        if (r == 0)
            return RS_AUTO_DONE;
        Rs_AutoLog(L"  check: no track folder is loaded");
        return RS_AUTO_FAIL;
    }
    // Like the button "Build container". The automation waits for rldpack and
    // for the preview that Track_BuildDone starts right after (Track_Busy:
    // the preview job already exists when rldpack has reported done). The
    // automation log says "build: ..." and after it "preview: written ...",
    // "preview: failed - ..." or "preview: skipped - ...".
    if (wcscmp(verb, L"build") == 0) {
        if (!Track_CanBuild()) {
            Rs_AutoLog(L"  build: not possible - %ls",
                       !g_track.loaded[0] ? L"no track folder is loaded" : L"the last check did not pass");
            return RS_AUTO_FAIL;
        }
        r = Track_Build(page);
        if (r > 0)
            return RS_AUTO_WAIT;
        Rs_AutoLog(L"  build: not started");
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"test") == 0) {
        if (!g_track.built[0]) {
            Rs_AutoLog(L"  test: no container built yet");
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  test: %ls", g_track.built);
        Track_OpenTest();
        return RS_AUTO_DONE;
    }
    // Records the preview of the last built container anew (there is no longer a
    // button for it, "build" already records it). The automation
    // waits until the game has ended; the result is then in the automation log as
    // "preview: written ..." or "preview: failed - ...".
    if (wcscmp(verb, L"preview") == 0) {
        if (Track_Preview(page, 0))
            return RS_AUTO_WAIT;
        return RS_AUTO_FAIL;
    }
    if (wcscmp(verb, L"report") == 0) {
        if (!arg[0] || !Track_WriteReport(arg)) {
            Rs_AutoLog(L"  report: could not write '%ls'", arg);
            return RS_AUTO_FAIL;
        }
        Rs_AutoLog(L"  report: %ls", arg);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"advanced") == 0) {
        int open;
        if (_wcsicmp(arg, L"open") == 0)
            open = 1;
        else if (_wcsicmp(arg, L"close") == 0)
            open = 0;
        else {
            Rs_AutoLog(L"  advanced: say open or close");
            return RS_AUTO_FAIL;
        }
        if (g_track.advOpen != open)
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(TRACK_ID_ADVANCED, BN_CLICKED), (LPARAM)g_track.advanced);
        return RS_AUTO_DONE;
    }
    if (wcscmp(verb, L"view") == 0) {
        int raw;
        if (_wcsicmp(arg, L"output") == 0)
            raw = 1;
        else if (_wcsicmp(arg, L"messages") == 0)
            raw = 0;
        else {
            Rs_AutoLog(L"  view: say messages or output");
            return RS_AUTO_FAIL;
        }
        if (g_track.showRaw != raw) {
            g_track.showRaw = raw;
            Track_ShowView();
        }
        return RS_AUTO_DONE;
    }
    return RS_AUTO_UNKNOWN;
}

static int Track_Busy(HWND page)
{
    (void)page;
    return g_track.jobId != 0 || g_track.timer;
}

const struct RsPageDef g_rsTrackPage = {
    L"Track",
    L"Build a track container",
    L"Pick a track folder with a .lev, a .vrm and optionally music (.sca or .sndb). rldpack checks everything and builds the .rldtrack.",
    Track_Create,
    Track_Layout,
    Track_Command,
    Track_Notify,
    Track_Message,
    Track_Automate,
    Track_Busy,
    RS_PAGE_MIN_W,
    RS_PAGE_MIN_H
};
