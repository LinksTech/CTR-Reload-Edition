// am_cups.c - page "Cups" of the Alpha-Maker
//
// Four containers make a cup, at most four cups are in cups.txt in the
// game's track folder (ARCADE -> CUSTOM CUP). The rules for the file
// are the game's: NativeCup_Read in platform/native_assets.c and the
// comment at NATIVE_CUP_FILE in include/platform/native_assets.h.
//
// What a container can do (name, Race, nav paths, restart points) is said by
// `rldpack info --machine`; the page only reads its machine lines. It checks itself
// only what concerns cups.txt: count, lengths, characters, file names.
// Only cups.txt is written, and only on "Save" - track-ids.tsv never.

#include "alphamaker.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Controls
#define CUPS_ID_FOLDER       100
#define CUPS_ID_BROWSE       101
#define CUPS_ID_RELOAD       102
#define CUPS_ID_STATUS       103
#define CUPS_ID_CONTAINERS   104
#define CUPS_ID_CONTHINT     105
#define CUPS_ID_ADD          106
#define CUPS_ID_CUPLIST      107
#define CUPS_ID_NEWCUP       108
#define CUPS_ID_DELCUP       109
#define CUPS_ID_CUPUP        110
#define CUPS_ID_CUPDOWN      111
#define CUPS_ID_NAMELABEL    112
#define CUPS_ID_NAME         113
#define CUPS_ID_NAMEHINT     114
#define CUPS_ID_TRACKSLABEL  115
#define CUPS_ID_TRACKSCOUNT  116
#define CUPS_ID_TRACKLIST    117
#define CUPS_ID_REMOVE       118
#define CUPS_ID_TRACKUP      119
#define CUPS_ID_TRACKDOWN    120
#define CUPS_ID_MSGLIST      121
#define CUPS_ID_HEADLINE     122
#define CUPS_ID_COUNTS       123
#define CUPS_ID_SAVE         124

// Limits of the game (native_assets.c/.h)
#define CUPS_MAX_CONTAINERS  64    // NATIVE_TRACK_MAX
#define CUPS_MAX_CUPS        4     // NATIVE_CUP_MAX
#define CUPS_TRACKS          4     // NATIVE_CUP_TRACKS
#define CUPS_NAME_BYTES      47    // NATIVE_CUP_NAME_MAX - 1
#define CUPS_FILE_BYTES      127   // NativeCup.file[128]
#define CUPS_LINE_BYTES      510   // fgets with 512 bytes
#define CUPS_SCREEN_CELLS    12    // cup screen
#define CUPS_TITLE_CELLS     16    // race title

#define CUPS_TEXT_CAP        512
#define CUPS_SHOW_CAP        68    // shortened names in messages
#define CUPS_UNKEPT_TEXT     64
#define CUPS_MAX_UNKEPT      64
#define CUPS_MAX_LOADMSG     16
#define CUPS_MSG_MAX         128
#define CUPS_MSG_CAP         768

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum CupsInfo {
    CUPS_INFO_PENDING = 0,  // rldpack still running
    CUPS_INFO_OK,
    CUPS_INFO_REFUSED,      // the game does not read the container
    CUPS_INFO_NONE          // got no answer
};

enum CupsUnkeptKind {
    CUPS_UNKEPT_OTHER = 0,  // unknown key, no '=', too long, ...
    CUPS_UNKEPT_CUP,        // cup after the fourth and its tracks
    CUPS_UNKEPT_TRACK       // fifth and further track of a cup
};

struct CupsContainer {
    wchar_t file[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t name[128];
    wchar_t modes[64];
    wchar_t reason[256];
    int state;
    int race;               // 1 yes, 0 no, -1 unknown
    int sawRaceMode;
    int navPaths;           // -1 unknown
    int restartPoints;      // -1 unknown
};

struct CupsCup {
    wchar_t name[CUPS_TEXT_CAP];      // as in the field, untrimmed
    wchar_t track[CUPS_TRACKS][CUPS_TEXT_CAP];
    int trackCount;
};

// A line from cups.txt that the editor does not take over.
struct CupsUnkept {
    int line;
    int kind;
    wchar_t text[CUPS_UNKEPT_TEXT];
    wchar_t why[48];
};

struct CupsMsg {
    int sev;
    wchar_t text[CUPS_MSG_CAP];
    wchar_t detail[CUPS_MSG_CAP];
};

struct CupsState {
    HWND page;
    HWND folderEdit, browseBtn, reloadBtn, folderStatus;
    HWND contList, contHint, addBtn;
    HWND cupList, newBtn, deleteBtn, cupUpBtn, cupDownBtn;
    HWND nameLabel, nameEdit, nameHint, tracksLabel, tracksCount, trackList;
    HWND removeBtn, trackUpBtn, trackDownBtn;
    HWND msgList, headline, counts, saveBtn;

    int shown;
    int loaded;
    wchar_t folder[MAX_PATH];

    struct CupsContainer containers[CUPS_MAX_CONTAINERS];
    int containerCount;
    int tooMany;
    unsigned long folderStamp;
    int infoJob;
    int infoCur;
    wchar_t infoError[256];

    struct CupsCup cups[CUPS_MAX_CUPS];
    int cupCount;
    int selCup;

    // What came from cups.txt on loading.
    int fileExists;
    int fileUnreadable;
    int fileCups;
    struct CupsUnkept unkept[CUPS_MAX_UNKEPT];
    int unkeptCount;        // stored
    int unkeptTotal;        // all
    struct CupsMsg loadMsgs[CUPS_MAX_LOADMSG];
    int loadMsgCount;

    int dirty;
    int savedNow;
    int errorCount;
    int warningCount;
    int updating;           // text is being set by the program, no EN_CHANGE
};

static struct CupsState s_cups;
static struct CupsMsg s_msgs[CUPS_MSG_MAX];
static int s_msgCount;

static void Cups_Layout(HWND page, int w, int h);

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

static void Cups_Copy(wchar_t *dst, int cap, const wchar_t *src)
{
    if (cap <= 0)
        return;
    wcsncpy(dst, src ? src : L"", (size_t)cap - 1);
    dst[cap - 1] = 0;
}

static void Cups_Cat(wchar_t *dst, size_t cap, const wchar_t *src)
{
    size_t n = wcslen(dst), m = wcslen(src);
    if (n + 1 >= cap)
        return;
    if (m > cap - 1 - n)
        m = cap - 1 - n;
    memcpy(dst + n, src, m * sizeof(wchar_t));
    dst[n + m] = 0;
}

static const wchar_t *Cups_S(int n)
{
    return n == 1 ? L"" : L"s";
}

static int Cups_Upper(wchar_t c)
{
    return (c >= L'a' && c <= L'z') ? (int)(c - L'a' + L'A') : (int)c;
}

// Like NativeTrack_CompareNames: only A-Z, ignoring upper/lower case.
static int Cups_CompareNames(const wchar_t *a, const wchar_t *b)
{
    while (*a && Cups_Upper(*a) == Cups_Upper(*b)) {
        a++;
        b++;
    }
    return Cups_Upper(*a) - Cups_Upper(*b);
}

static int Cups_IsBlank(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

// Like NativeCup_Trim: spaces and tabs at the front, at the back also CR and LF.
static wchar_t *Cups_Trim(wchar_t *s)
{
    wchar_t *end;
    while (*s == L' ' || *s == L'\t')
        s++;
    end = s + wcslen(s);
    while (end > s && Cups_IsBlank(end[-1]))
        end--;
    *end = 0;
    return s;
}

static void Cups_TrimCopy(wchar_t *dst, int cap, const wchar_t *src)
{
    const wchar_t *end;
    size_t n;
    if (cap <= 0)
        return;
    if (!src)
        src = L"";
    while (*src == L' ' || *src == L'\t')
        src++;
    end = src + wcslen(src);
    while (end > src && Cups_IsBlank(end[-1]))
        end--;
    n = (size_t)(end - src);
    if (n > (size_t)cap - 1)
        n = (size_t)cap - 1;
    memcpy(dst, src, n * sizeof(wchar_t));
    dst[n] = 0;
}

// Shortens for messages: at most cap - 4 characters and "...".
static void Cups_Short(wchar_t *dst, int cap, const wchar_t *src)
{
    size_t n;
    if (!src)
        src = L"";
    n = wcslen(src);
    if ((int)n < cap) {
        Cups_Copy(dst, cap, src);
        return;
    }
    memcpy(dst, src, ((size_t)cap - 4) * sizeof(wchar_t));
    wcscpy(dst + cap - 4, L"...");
}

// The game counts bytes (UTF-8), not characters.
static int Cups_Utf8Len(const wchar_t *s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    return n > 0 ? n - 1 : 0;
}

static int Cups_CharBytes(const wchar_t *p)
{
    if (*p < 0x80)
        return 1;
    if (*p < 0x800)
        return 2;
    if (*p >= 0xD800 && *p <= 0xDBFF)
        return 4;
    if (*p >= 0xDC00 && *p <= 0xDFFF)
        return 0;
    return 3;
}

// What the menu shows of a name: the first cells characters, upper case.
static void Cups_Preview(const wchar_t *name, int cells, wchar_t *out, int cap)
{
    const wchar_t *p;
    int used = 0, n = 0;
    for (p = name; *p && n < cap - 2; p++) {
        int w = Cups_CharBytes(p);
        if (used + w > cells)
            break;
        used += w;
        out[n++] = (wchar_t)Cups_Upper(*p);
    }
    out[n] = 0;
}

// Menu font: 0 = draws, 1 = button symbol, 2 = gap.
static int Cups_MenuChar(wchar_t c)
{
    if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L' ')
        return 0;
    if (c && wcschr(L"!%'+,-./:<=>?_", c))
        return 0;
    if (c && wcschr(L"@[^*", c))
        return 1;
    return 2;
}

// Collects the characters the menu does not draw, each once.
static int Cups_BadChars(const wchar_t *name, wchar_t *out, int cap, int *icons, int *gaps)
{
    wchar_t seen[24][3];
    const wchar_t *p;
    int n = 0, i, len = 0;

    out[0] = 0;
    *icons = 0;
    *gaps = 0;
    for (p = name; *p; p++) {
        wchar_t one[3];
        int kind = Cups_MenuChar(*p);
        if (kind == 0)
            continue;
        one[0] = *p;
        one[1] = 0;
        one[2] = 0;
        if (*p >= 0xD800 && *p <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF) {
            one[1] = p[1];
            p++;
        }
        for (i = 0; i < n; i++)
            if (wcscmp(seen[i], one) == 0)
                break;
        if (i < n || n >= 24)
            continue;
        wcscpy(seen[n++], one);
        if (kind == 1)
            *icons = 1;
        else
            *gaps = 1;
        if (len + 4 < cap) {
            if (len)
                out[len++] = L' ';
            out[len++] = one[0];
            if (one[1])
                out[len++] = one[1];
            out[len] = 0;
        }
    }
    return n;
}

// Extension .rldtrack, upper or lower case, and at least one character before it.
static int Cups_HasExtension(const wchar_t *name)
{
    size_t n = wcslen(name), e = wcslen(L".rldtrack");
    return n > e && Cups_CompareNames(name + n - e, L".rldtrack") == 0;
}

// "1".."999" -> number, everything else -> 0.
static int Cups_Index(const wchar_t *s)
{
    const wchar_t *p;
    if (!s || !*s || wcslen(s) > 3)
        return 0;
    for (p = s; *p; p++)
        if (*p < L'0' || *p > L'9')
            return 0;
    return _wtoi(s);
}

static int Cups_Number(const wchar_t *s)
{
    if (!s || *s < L'0' || *s > L'9')
        return -1;
    return _wtoi(s);
}

// "Cup 'Name'" for messages, "Cup 2 (no name)" without a name.
static void Cups_Who(const wchar_t *trimmedName, int index, wchar_t *out, int cap)
{
    wchar_t sn[44];
    if (!trimmedName[0]) {
        swprintf(out, cap, L"Cup %d (no name)", index + 1);
        return;
    }
    Cups_Short(sn, 44, trimmedName);
    swprintf(out, cap, L"Cup '%ls'", sn);
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

static void Cups_Add(int sev, const wchar_t *detail, const wchar_t *fmt, ...)
{
    struct CupsMsg *m;
    va_list ap;
    if (s_msgCount >= CUPS_MSG_MAX)
        return;
    m = &s_msgs[s_msgCount++];
    m->sev = sev;
    va_start(ap, fmt);
    vswprintf(m->text, CUPS_MSG_CAP, fmt, ap);
    va_end(ap);
    m->text[CUPS_MSG_CAP - 1] = 0;
    Cups_Copy(m->detail, CUPS_MSG_CAP, detail);
}

static void Cups_LoadMsg(struct CupsState *s, int sev, const wchar_t *detail, const wchar_t *fmt, ...)
{
    struct CupsMsg *m;
    va_list ap;
    if (s->loadMsgCount >= CUPS_MAX_LOADMSG)
        return;
    m = &s->loadMsgs[s->loadMsgCount++];
    m->sev = sev;
    va_start(ap, fmt);
    vswprintf(m->text, CUPS_MSG_CAP, fmt, ap);
    va_end(ap);
    m->text[CUPS_MSG_CAP - 1] = 0;
    Cups_Copy(m->detail, CUPS_MSG_CAP, detail);
}

static void Cups_Unkept(struct CupsState *s, int line, int kind, const wchar_t *text, const wchar_t *why)
{
    struct CupsUnkept *u;
    s->unkeptTotal++;
    if (s->unkeptCount >= CUPS_MAX_UNKEPT)
        return;
    u = &s->unkept[s->unkeptCount++];
    u->line = line;
    u->kind = kind;
    Cups_Copy(u->text, CUPS_UNKEPT_TEXT, text);
    Cups_Copy(u->why, 48, why);
}

// ---------------------------------------------------------------------------
// Containers and rldpack info
// ---------------------------------------------------------------------------

static int Cups_FindContainer(const struct CupsState *s, const wchar_t *file)
{
    int i;
    for (i = 0; i < s->containerCount; i++)
        if (Cups_CompareNames(s->containers[i].file, file) == 0)
            return i;
    return -1;
}

static const wchar_t *Cups_ContName(const struct CupsContainer *c)
{
    return (c->state == CUPS_INFO_OK && c->name[0]) ? c->name : c->file;
}

static int Cups_ModesHaveRace(const wchar_t *modes)
{
    const wchar_t *p = modes;
    while (*p) {
        wchar_t word[32];
        int n = 0;
        while (*p == L',' || *p == L' ')
            p++;
        while (*p && *p != L',' && *p != L' ') {
            if (n < 31)
                word[n++] = *p;
            p++;
        }
        word[n] = 0;
        if (n && Cups_CompareNames(word, L"race") == 0)
            return 1;
    }
    return 0;
}

static void Cups_AppendNote(wchar_t *notes, int cap, const wchar_t *text)
{
    if (notes[0])
        Cups_Cat(notes, (size_t)cap, L"; ");
    Cups_Cat(notes, (size_t)cap, text);
}

// The columns "Race" and "Notes" of a container.
static void Cups_ContTexts(const struct CupsContainer *c, wchar_t *race, int raceCap,
                           wchar_t *notes, int notesCap)
{
    notes[0] = 0;
    switch (c->state) {
    case CUPS_INFO_PENDING:
        Cups_Copy(race, raceCap, L"...");
        Cups_Copy(notes, notesCap, L"Reading...");
        return;
    case CUPS_INFO_NONE:
        Cups_Copy(race, raceCap, L"?");
        Cups_Copy(notes, notesCap, L"Not checked - the checker gave no answer");
        return;
    case CUPS_INFO_REFUSED:
        Cups_Copy(race, raceCap, L"Refused");
        swprintf(notes, notesCap, L"Refused: %ls", c->reason[0] ? c->reason : L"no reason given");
        return;
    }
    Cups_Copy(race, raceCap, c->race == 1 ? L"Yes" : (c->race == 0 ? L"No" : L"?"));
    if (c->race == 0)
        Cups_AppendNote(notes, notesCap, L"Does not offer Race");
    if (c->navPaths == 0)
        Cups_AppendNote(notes, notesCap, L"No nav paths - the bots do not drive");
    if (c->restartPoints == 0)
        Cups_AppendNote(notes, notesCap, L"No restart points - laps never count");
}

// ID of the folder content from names, sizes and times of the containers.
static unsigned long Cups_FolderStamp(const wchar_t *folder)
{
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    unsigned long stamp = 2166136261u;

    Am_PathJoin(pattern, MAX_PATH, folder, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        const wchar_t *p;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !Cups_HasExtension(fd.cFileName))
            continue;
        for (p = fd.cFileName; *p; p++)
            stamp = (stamp ^ (unsigned long)Cups_Upper(*p)) * 16777619u;
        stamp = (stamp ^ fd.nFileSizeLow) * 16777619u;
        stamp = (stamp ^ fd.ftLastWriteTime.dwLowDateTime) * 16777619u;
        stamp = (stamp ^ fd.ftLastWriteTime.dwHighDateTime) * 16777619u;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return stamp;
}

static int Cups_CompareContainers(const void *a, const void *b)
{
    return Cups_CompareNames(((const struct CupsContainer *)a)->file,
                             ((const struct CupsContainer *)b)->file);
}

// Lists the containers of the folder (flat, like the game's scan) and
// starts `rldpack info --machine`. Returns 1 if the job is running.
static int Cups_ScanContainers(struct CupsState *s)
{
    const wchar_t *args[2 + CUPS_MAX_CONTAINERS];
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int i;

    memset(s->containers, 0, sizeof(s->containers));
    s->containerCount = 0;
    s->tooMany = 0;
    s->infoJob = 0;
    s->infoCur = -1;
    s->infoError[0] = 0;
    s->folderStamp = Cups_FolderStamp(s->folder);

    Am_PathJoin(pattern, MAX_PATH, s->folder, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            struct CupsContainer *c;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !Cups_HasExtension(fd.cFileName))
                continue;
            if (s->containerCount >= CUPS_MAX_CONTAINERS) {
                s->tooMany = 1;
                continue;
            }
            c = &s->containers[s->containerCount++];
            Cups_Copy(c->file, MAX_PATH, fd.cFileName);
            Am_PathJoin(c->path, MAX_PATH, s->folder, fd.cFileName);
            c->state = CUPS_INFO_PENDING;
            c->race = -1;
            c->navPaths = -1;
            c->restartPoints = -1;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (s->containerCount == 0)
        return 0;
    // The same order as the list in the game.
    qsort(s->containers, (size_t)s->containerCount, sizeof(s->containers[0]), Cups_CompareContainers);

    args[0] = L"info";
    args[1] = L"--machine";
    for (i = 0; i < s->containerCount; i++)
        args[2 + i] = s->containers[i].path;
    s->infoJob = Am_RunRldpack(s->page, args, 2 + s->containerCount);
    if (!s->infoJob) {
        for (i = 0; i < s->containerCount; i++)
            s->containers[i].state = CUPS_INFO_NONE;
        Cups_Copy(s->infoError, 256,
                  L"The page could not start its container check, so it cannot tell which "
                  L"containers offer Race. Press Reload to try again.");
        return 0;
    }
    return 1;
}

static int Cups_MatchContainer(const struct CupsState *s, const wchar_t *path)
{
    int i;
    for (i = 0; i < s->containerCount; i++)
        if (_wcsicmp(s->containers[i].path, path) == 0)
            return i;
    return Cups_FindContainer(s, Am_PathName(path));
}

static int Cups_Yes(const wchar_t *f)
{
    return wcscmp(f, L"yes") == 0;
}

// One line of rldpack info. Only known machine lines count.
static void Cups_JobLine(struct CupsState *s, wchar_t *line)
{
    wchar_t *f[8];
    struct CupsContainer *c;
    int n = Am_SplitMachine(line, f, 8);

    if (n <= 0)
        return;
    if (wcscmp(f[0], L"container") == 0) {
        s->infoCur = Cups_MatchContainer(s, n > 1 ? f[1] : L"");
        if (s->infoCur < 0)
            return;
        c = &s->containers[s->infoCur];
        c->state = (n > 2 && wcscmp(f[2], L"ok") == 0) ? CUPS_INFO_OK : CUPS_INFO_REFUSED;
        Cups_Copy(c->reason, 256, n > 3 ? f[3] : L"");
        return;
    }
    if (wcscmp(f[0], L"end") == 0) {
        s->infoCur = -1;
        return;
    }
    if (s->infoCur < 0)
        return;
    c = &s->containers[s->infoCur];
    if (c->state != CUPS_INFO_OK)
        return;
    if (wcscmp(f[0], L"value") == 0 && n > 2) {
        if (wcscmp(f[1], L"name") == 0)
            Cups_Copy(c->name, 128, f[2]);
        else if (wcscmp(f[1], L"modes") == 0)
            Cups_Copy(c->modes, 64, f[2]);
    } else if (wcscmp(f[0], L"mode") == 0 && n > 3 && wcscmp(f[1], L"race") == 0) {
        // The game offers Race if the container declares it and the build plays it.
        int declared = Cups_Yes(f[3]);
        int playable = n > 4 ? Cups_Yes(f[4]) : declared;
        c->sawRaceMode = 1;
        c->race = declared && playable;
    } else if (wcscmp(f[0], L"lev") == 0 && n > 2) {
        if (wcscmp(f[1], L"restart_points") == 0)
            c->restartPoints = Cups_Number(f[2]);
        else if (wcscmp(f[1], L"nav_paths") == 0)
            c->navPaths = Cups_Number(f[2]);
    }
}

// ---------------------------------------------------------------------------
// Reading cups.txt - the same rules as NativeCup_Read
//
// The editor keeps every cup with a valid structure, even with too few
// tracks, so that the author can repair it. What it does not keep, it remembers
// for the warning before saving.
// ---------------------------------------------------------------------------

static void Cups_ReadCupsFile(struct CupsState *s)
{
    wchar_t path[MAX_PATH], shown[CUPS_UNKEPT_TEXT], detail[CUPS_MSG_CAP];
    wchar_t nm[CUPS_TEXT_CAP], who[64], one[200];
    wchar_t *text, *p;
    int lineNo = 0, cur = -1, extraCups = 0, other = 0, listed = 0, i;
    int extraTracks[CUPS_MAX_CUPS];

    memset(extraTracks, 0, sizeof(extraTracks));
    memset(s->cups, 0, sizeof(s->cups));
    s->cupCount = 0;
    s->unkeptCount = 0;
    s->unkeptTotal = 0;
    s->loadMsgCount = 0;
    s->fileExists = 0;
    s->fileUnreadable = 0;
    s->fileCups = 0;

    Am_PathJoin(path, MAX_PATH, s->folder, L"cups.txt");
    text = Am_ReadTextFile(path);
    if (!text) {
        if (Am_FileExists(path)) {
            s->fileExists = 1;
            s->fileUnreadable = 1;
            Cups_LoadMsg(s, AM_SEV_WARNING, NULL,
                         L"cups.txt is in this folder but could not be read. Saving replaces it.");
        } else {
            Cups_LoadMsg(s, AM_SEV_INFO, NULL, L"There is no cups.txt in this folder yet. Save creates it.");
        }
        return;
    }
    s->fileExists = 1;

    p = text;
    while (*p) {
        wchar_t *line = p, *nl = wcschr(p, L'\n'), *t, *eq, *key, *value;
        size_t n;
        if (nl) {
            *nl = 0;
            p = nl + 1;
        } else {
            p = line + wcslen(line);
        }
        lineNo++;
        n = wcslen(line);
        if (n > 0 && line[n - 1] == L'\r')
            line[n - 1] = 0;

        // As in the game, the length first, also for comments.
        if (Cups_Utf8Len(line) > CUPS_LINE_BYTES) {
            Cups_Short(shown, CUPS_UNKEPT_TEXT, Cups_Trim(line));
            Cups_Unkept(s, lineNo, CUPS_UNKEPT_OTHER, shown, L"longer than 510 characters");
            other++;
            continue;
        }
        t = Cups_Trim(line);
        if (!*t || *t == L'#')
            continue;
        Cups_Short(shown, CUPS_UNKEPT_TEXT, t);
        eq = wcschr(t, L'=');
        if (!eq) {
            Cups_Unkept(s, lineNo, CUPS_UNKEPT_OTHER, shown, L"no '='");
            other++;
            continue;
        }
        *eq = 0;
        key = Cups_Trim(t);
        value = Cups_Trim(eq + 1);

        if (Cups_CompareNames(key, L"cup") == 0) {
            s->fileCups++;
            if (s->cupCount >= CUPS_MAX_CUPS) {
                extraCups++;
                cur = -2;
                Cups_Unkept(s, lineNo, CUPS_UNKEPT_CUP, shown, L"a cup after the fourth");
                continue;
            }
            cur = s->cupCount++;
            Cups_Copy(s->cups[cur].name, CUPS_TEXT_CAP, value);
        } else if (Cups_CompareNames(key, L"track") == 0) {
            if (cur == -1) {
                Cups_Unkept(s, lineNo, CUPS_UNKEPT_OTHER, shown, L"a track before any cup");
                other++;
            } else if (cur == -2) {
                Cups_Unkept(s, lineNo, CUPS_UNKEPT_CUP, shown, L"a track of a cup after the fourth");
            } else if (!*value) {
                Cups_Unkept(s, lineNo, CUPS_UNKEPT_OTHER, shown, L"a track without a file name");
                other++;
            } else if (s->cups[cur].trackCount >= CUPS_TRACKS) {
                extraTracks[cur]++;
                Cups_Unkept(s, lineNo, CUPS_UNKEPT_TRACK, shown, L"a fifth track");
            } else {
                struct CupsCup *cup = &s->cups[cur];
                Cups_Copy(cup->track[cup->trackCount++], CUPS_TEXT_CAP, value);
            }
        } else {
            Cups_Unkept(s, lineNo, CUPS_UNKEPT_OTHER, shown, L"an unknown key");
            other++;
        }
    }
    Am_Free(text);

    if (extraCups > 0)
        Cups_LoadMsg(s, AM_SEV_WARNING, NULL,
                     L"cups.txt has more than 4 cups. The game shows only 4; the others were not "
                     L"loaded and are removed when you save.");
    for (i = 0; i < s->cupCount; i++) {
        if (!extraTracks[i])
            continue;
        Cups_TrimCopy(nm, CUPS_TEXT_CAP, s->cups[i].name);
        Cups_Who(nm, i, who, 64);
        Cups_LoadMsg(s, AM_SEV_WARNING, NULL,
                     L"cups.txt lists %d tracks for %ls, and the game leaves such a cup out. The "
                     L"editor keeps the first 4; the others are removed when you save.",
                     CUPS_TRACKS + extraTracks[i], who);
    }
    if (other > 0) {
        detail[0] = 0;
        for (i = 0; i < s->unkeptCount && listed < 3; i++) {
            if (s->unkept[i].kind != CUPS_UNKEPT_OTHER)
                continue;
            swprintf(one, 200, L"%lsLine %d: %ls (%ls)", listed ? L"  |  " : L"",
                     s->unkept[i].line, s->unkept[i].text, s->unkept[i].why);
            Cups_Cat(detail, CUPS_MSG_CAP, one);
            listed++;
        }
        if (other > listed) {
            swprintf(one, 200, L"  |  and %d more", other - listed);
            Cups_Cat(detail, CUPS_MSG_CAP, one);
        }
        Cups_LoadMsg(s, AM_SEV_WARNING, detail,
                     L"cups.txt has %d line%ls this page does not understand. The game skips such "
                     L"lines or leaves their cup out; saving removes %ls.",
                     other, Cups_S(other), other == 1 ? L"it" : L"them");
    }
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

// Sets the text only when it changes - otherwise the label flickers.
static void Cups_SetText(HWND h, const wchar_t *text)
{
    wchar_t *now = Am_GetText(h);
    if (wcscmp(now, text) != 0)
        Am_SetText(h, text);
    Am_Free(now);
}

static int Cups_HasCup(const struct CupsState *s)
{
    return s->selCup >= 0 && s->selCup < s->cupCount;
}

static int Cups_TrackSel(const struct CupsState *s)
{
    int t = (int)SendMessageW(s->trackList, LB_GETCURSEL, 0, 0);
    if (!Cups_HasCup(s) || t < 0 || t >= s->cups[s->selCup].trackCount)
        return -1;
    return t;
}

static int Cups_ContSel(const struct CupsState *s)
{
    int c = ListView_GetNextItem(s->contList, -1, LVNI_SELECTED);
    return (c >= 0 && c < s->containerCount) ? c : -1;
}

static void Cups_CupText(const struct CupsState *s, int i, wchar_t *out, int cap)
{
    wchar_t nm[CUPS_TEXT_CAP];
    Cups_TrimCopy(nm, CUPS_TEXT_CAP, s->cups[i].name);
    swprintf(out, cap, L"%d. %ls", i + 1, nm[0] ? nm : L"(no name)");
}

static void Cups_TrackText(const struct CupsState *s, const wchar_t *file, int pos, wchar_t *out, int cap)
{
    int c = Cups_FindContainer(s, file);
    const struct CupsContainer *k = c >= 0 ? &s->containers[c] : NULL;
    if (!k)
        swprintf(out, cap, L"%d. %ls - not in this folder", pos, file);
    else if (k->state == CUPS_INFO_REFUSED)
        swprintf(out, cap, L"%d. %ls - refused by the game", pos, file);
    else if (k->state == CUPS_INFO_OK && k->name[0])
        swprintf(out, cap, L"%d. %ls (%ls)", pos, k->name, file);
    else
        swprintf(out, cap, L"%d. %ls", pos, file);
}

static void Cups_SelectContainer(struct CupsState *s, int i)
{
    ListView_SetItemState(s->contList, -1, 0, LVIS_SELECTED);
    if (i >= 0) {
        ListView_SetItemState(s->contList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(s->contList, i, FALSE);
    }
}

// The chosen file, as long as list and field still match.
static void Cups_SelectedFile(const struct CupsState *s, wchar_t *out, int cap)
{
    int sel = Cups_ContSel(s);
    out[0] = 0;
    if (sel >= 0)
        Cups_Copy(out, cap, s->containers[sel].file);
}

// keep: file name that should stay selected (or NULL).
static void Cups_FillContainers(struct CupsState *s, const wchar_t *keep)
{
    int i, again = -1;

    SendMessageW(s->contList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(s->contList);
    for (i = 0; i < s->containerCount; i++) {
        struct CupsContainer *c = &s->containers[i];
        wchar_t race[32], notes[512];
        LVITEMW it;
        Cups_ContTexts(c, race, 32, notes, 512);
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (LPWSTR)Cups_ContName(c);
        ListView_InsertItem(s->contList, &it);
        ListView_SetItemText(s->contList, i, 1, c->file);
        ListView_SetItemText(s->contList, i, 2, race);
        ListView_SetItemText(s->contList, i, 3, notes);
        if (keep && keep[0] && Cups_CompareNames(keep, c->file) == 0)
            again = i;
    }
    if (again >= 0)
        Cups_SelectContainer(s, again);
    SendMessageW(s->contList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(s->contList, NULL, TRUE);
}

static void Cups_FillCups(struct CupsState *s)
{
    wchar_t buf[CUPS_TEXT_CAP + 16];
    int i;
    SendMessageW(s->cupList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < s->cupCount; i++) {
        Cups_CupText(s, i, buf, CUPS_TEXT_CAP + 16);
        SendMessageW(s->cupList, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    if (Cups_HasCup(s))
        SendMessageW(s->cupList, LB_SETCURSEL, (WPARAM)s->selCup, 0);
}

static void Cups_UpdateCupItem(struct CupsState *s, int i)
{
    wchar_t buf[CUPS_TEXT_CAP + 16];
    if (i < 0 || i >= s->cupCount)
        return;
    Cups_CupText(s, i, buf, CUPS_TEXT_CAP + 16);
    SendMessageW(s->cupList, LB_DELETESTRING, (WPARAM)i, 0);
    SendMessageW(s->cupList, LB_INSERTSTRING, (WPARAM)i, (LPARAM)buf);
    if (Cups_HasCup(s))
        SendMessageW(s->cupList, LB_SETCURSEL, (WPARAM)s->selCup, 0);
}

static void Cups_UpdateNameHint(struct CupsState *s)
{
    wchar_t nm[CUPS_TEXT_CAP], shown[40], text[128];
    if (!Cups_HasCup(s)) {
        Cups_SetText(s->nameHint, s->loaded ? L"Select a cup or press New cup." : L"");
        return;
    }
    Cups_TrimCopy(nm, CUPS_TEXT_CAP, s->cups[s->selCup].name);
    Cups_Preview(nm, CUPS_SCREEN_CELLS, shown, 40);
    if (shown[0])
        swprintf(text, 128, L"The cup screen shows 12 characters: %ls", shown);
    else
        Cups_Copy(text, 128, L"The cup screen shows 12 characters.");
    Cups_SetText(s->nameHint, text);
}

static void Cups_FillCup(struct CupsState *s, int trackSel)
{
    struct CupsCup *cup = Cups_HasCup(s) ? &s->cups[s->selCup] : NULL;
    wchar_t buf[768];
    wchar_t *now = Am_GetText(s->nameEdit);
    int t;

    // Only set when different: otherwise the cursor jumps while typing.
    if (wcscmp(now, cup ? cup->name : L"") != 0) {
        s->updating = 1;
        Am_SetText(s->nameEdit, cup ? cup->name : L"");
        s->updating = 0;
    }
    Am_Free(now);

    SendMessageW(s->trackList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(s->trackList, LB_RESETCONTENT, 0, 0);
    for (t = 0; cup && t < cup->trackCount; t++) {
        Cups_TrackText(s, cup->track[t], t + 1, buf, 768);
        SendMessageW(s->trackList, LB_ADDSTRING, 0, (LPARAM)buf);
    }
    if (cup && trackSel >= 0 && trackSel < cup->trackCount)
        SendMessageW(s->trackList, LB_SETCURSEL, (WPARAM)trackSel, 0);
    SendMessageW(s->trackList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(s->trackList, NULL, TRUE);

    Cups_UpdateNameHint(s);
    if (cup) {
        swprintf(buf, 768, cup->trackCount == CUPS_TRACKS ? L"4 of 4 - complete" : L"%d of 4",
                 cup->trackCount);
        Am_SetText(s->tracksCount, buf);
        Am_SetTextColor(s->tracksCount, cup->trackCount == CUPS_TRACKS ? AM_COL_OK : AM_COL_MUTED);
    } else {
        Am_SetText(s->tracksCount, L"");
    }
}

// Takes the focus away from a button before it turns grey.
static void Cups_Enable(HWND h, int on, HWND fallback)
{
    if (!on && GetFocus() == h && fallback)
        SetFocus(fallback);
    EnableWindow(h, on ? TRUE : FALSE);
}

static void Cups_UpdateButtons(struct CupsState *s)
{
    int cup = Cups_HasCup(s);
    int tracks = cup ? s->cups[s->selCup].trackCount : 0;
    int t = Cups_TrackSel(s);
    int c = Cups_ContSel(s);
    const wchar_t *hint;

    Cups_Enable(s->newBtn, s->loaded && s->cupCount < CUPS_MAX_CUPS, s->cupList);
    Cups_Enable(s->deleteBtn, cup, s->cupList);
    Cups_Enable(s->cupUpBtn, cup && s->selCup > 0, s->cupList);
    Cups_Enable(s->cupDownBtn, cup && s->selCup < s->cupCount - 1, s->cupList);
    Cups_Enable(s->nameEdit, cup, s->cupList);
    Cups_Enable(s->addBtn, cup && c >= 0 && tracks < CUPS_TRACKS, s->contList);
    Cups_Enable(s->removeBtn, t >= 0, s->trackList);
    Cups_Enable(s->trackUpBtn, t > 0, s->trackList);
    Cups_Enable(s->trackDownBtn, t >= 0 && t < tracks - 1, s->trackList);
    Cups_Enable(s->saveBtn, s->loaded, s->folderEdit);

    if (!s->loaded)
        hint = L"";
    else if (!cup)
        hint = L"Make or select a cup first.";
    else if (tracks >= CUPS_TRACKS)
        hint = L"The cup has 4 tracks.";
    else
        hint = L"or double-click a container";
    Cups_SetText(s->contHint, hint);
}

static void Cups_UpdateStatus(struct CupsState *s)
{
    wchar_t text[128];
    COLORREF col = AM_COL_MUTED;
    if (!s->loaded) {
        Cups_Copy(text, 128, L"Choose the tracks folder of the game.");
    } else if (s->fileUnreadable) {
        Cups_Copy(text, 128, L"cups.txt could not be read");
        col = AM_COL_ERROR;
    } else if (s->fileExists) {
        swprintf(text, 128, L"cups.txt found - %d cup%ls", s->fileCups, Cups_S(s->fileCups));
        col = AM_COL_OK;
    } else {
        Cups_Copy(text, 128, L"No cups.txt yet - Save creates it");
    }
    Am_SetText(s->folderStatus, text);
    Am_SetTextColor(s->folderStatus, col);
}

static void Cups_UpdateHeadline(struct CupsState *s)
{
    const wchar_t *text;
    COLORREF col = AM_COL_MUTED;
    wchar_t counts[160];

    if (!s->loaded) {
        text = L"No folder loaded";
    } else if (s->dirty) {
        text = L"Unsaved changes";
        col = AM_COL_WARNING;
    } else if (s->savedNow) {
        text = L"Saved - the game reads it at its next start";
        col = AM_COL_OK;
    } else if (s->fileUnreadable) {
        text = L"cups.txt could not be read";
        col = AM_COL_WARNING;
    } else if (!s->fileExists) {
        text = L"No cups.txt yet";
    } else {
        text = L"cups.txt is up to date";
    }
    Cups_SetText(s->headline, text);
    Am_SetTextColor(s->headline, col);

    counts[0] = 0;
    if (s->loaded)
        swprintf(counts, 160, L"%d cup%ls - %d error%ls, %d warning%ls",
                 s->cupCount, Cups_S(s->cupCount), s->errorCount, Cups_S(s->errorCount),
                 s->warningCount, Cups_S(s->warningCount));
    Cups_SetText(s->counts, counts);
}

// The check of a cup: everything the game rejects in cups.txt and in the
// containers, plus the nav paths (by design: Race without nav paths must be
// visible in the cup editor, because the bots never reach the finish there).
static void Cups_CheckCup(struct CupsState *s, int i)
{
    const struct CupsCup *cup = &s->cups[i];
    wchar_t nm[CUPS_TEXT_CAP], who[64], sn[44], chars[96], shown[CUPS_SHOW_CAP], tn[CUPS_SHOW_CAP];
    int bad = 0, pending = 0, bytes, t, u, j, icons, gaps, nbad;

    Cups_TrimCopy(nm, CUPS_TEXT_CAP, cup->name);
    Cups_Who(nm, i, who, 64);
    Cups_Short(sn, 44, nm);
    bytes = Cups_Utf8Len(nm);

    // Errors: the game leaves the cup out.
    if (!nm[0]) {
        Cups_Add(AM_SEV_ERROR, NULL, L"Cup %d has no name. A cup needs a name - the game leaves it out.", i + 1);
        bad++;
    } else if (bytes > CUPS_NAME_BYTES) {
        Cups_Add(AM_SEV_ERROR,
                 bytes != (int)wcslen(nm) ? L"Letters outside A-Z count as two or three characters." : NULL,
                 L"%ls: the name is longer than 47 characters. The game leaves the cup out.", who);
        bad++;
    }
    if (cup->trackCount != CUPS_TRACKS) {
        Cups_Add(AM_SEV_ERROR, NULL, L"%ls has %d track%ls. A cup needs exactly 4 - the game leaves it out.",
                 who, cup->trackCount, Cups_S(cup->trackCount));
        bad++;
    }
    for (t = 0; t < cup->trackCount; t++) {
        if (Cups_Utf8Len(cup->track[t]) <= CUPS_FILE_BYTES)
            continue;
        Cups_Short(shown, CUPS_SHOW_CAP, cup->track[t]);
        Cups_Add(AM_SEV_ERROR, NULL, L"%ls: the file name %ls is longer than 127 characters. The game leaves the cup out.",
                 who, shown);
        bad++;
    }

    // Warnings: characters the menu does not draw.
    nbad = Cups_BadChars(nm, chars, 96, &icons, &gaps);
    if (nbad > 0) {
        const wchar_t *how;
        if (nbad == 1)
            how = icons ? L"It shows as a button icon." : L"It shows as a gap.";
        else if (icons && gaps)
            how = L"They show as gaps or button icons.";
        else
            how = icons ? L"They show as button icons." : L"They show as gaps.";
        Cups_Add(AM_SEV_WARNING, L"The menu draws A-Z, 0-9, the space and ! % ' + , - . / : < = > ? _",
                 L"%ls: the menu cannot draw %ls. %ls", who, chars, how);
        bad++;
    }

    // Warnings: the cup is grey in the menu, or the bots do not drive.
    for (t = 0; t < cup->trackCount; t++) {
        const struct CupsContainer *k;
        int c;

        for (u = 0; u < t; u++)
            if (Cups_CompareNames(cup->track[u], cup->track[t]) == 0)
                break;
        if (u < t) {
            int seen = 0;
            for (j = 0; j < t; j++)
                if (Cups_CompareNames(cup->track[j], cup->track[t]) == 0)
                    seen++;
            if (seen == 1) {
                Cups_Short(shown, CUPS_SHOW_CAP, cup->track[t]);
                Cups_Add(AM_SEV_INFO, NULL, L"%ls uses %ls twice.", who, shown);
            }
            continue;
        }

        Cups_Short(shown, CUPS_SHOW_CAP, cup->track[t]);
        c = Cups_FindContainer(s, cup->track[t]);
        if (c < 0) {
            Cups_Add(AM_SEV_WARNING, NULL,
                     L"%ls: %ls is not in this folder. The game shows the cup grey and it cannot be chosen.",
                     who, shown);
            bad++;
            continue;
        }
        k = &s->containers[c];
        Cups_Short(tn, CUPS_SHOW_CAP, Cups_ContName(k));
        switch (k->state) {
        case CUPS_INFO_PENDING:
            pending = 1;
            break;
        case CUPS_INFO_NONE:
            Cups_Add(AM_SEV_WARNING, NULL, L"%ls: the page could not check %ls. Press Reload to try again.",
                     who, shown);
            bad++;
            break;
        case CUPS_INFO_REFUSED:
            Cups_Add(AM_SEV_WARNING, NULL, L"%ls: the game refuses %ls (%ls). The cup is grey.",
                     who, shown, k->reason[0] ? k->reason : L"no reason given");
            bad++;
            break;
        default:
            if (k->race == 0) {
                Cups_Add(AM_SEV_WARNING, NULL,
                         L"%ls: %ls does not offer Race. A cup is a race, so the cup is grey.", who, tn);
                bad++;
            }
            if (k->navPaths == 0) {
                Cups_Add(AM_SEV_WARNING, NULL,
                         L"%ls: there are no bots on this track (%ls) - it has no nav paths, "
                         L"so the game starts no bots there.", who, tn);
                bad++;
            }
            if (k->restartPoints == 0) {
                Cups_Add(AM_SEV_WARNING, NULL,
                         L"%ls: %ls has no restart points - laps never count and the race never ends.",
                         who, tn);
                bad++;
            }
            break;
        }
    }

    // Notes
    if (nm[0] && bytes > CUPS_SCREEN_CELLS && bytes <= CUPS_NAME_BYTES) {
        wchar_t a[40], b[40], detail[128];
        Cups_Preview(nm, CUPS_SCREEN_CELLS, a, 40);
        Cups_Preview(nm, CUPS_TITLE_CELLS, b, 40);
        swprintf(detail, 128, L"Cup screen: %ls    Race titles (16): %ls", a, b);
        Cups_Add(AM_SEV_NOTE, detail, L"%ls: the cup screen shows only 12 characters of the name.", who);
    }
    if (nm[0]) {
        for (j = 0; j < i; j++) {
            wchar_t other[CUPS_TEXT_CAP];
            Cups_TrimCopy(other, CUPS_TEXT_CAP, s->cups[j].name);
            if (Cups_CompareNames(other, nm) == 0) {
                Cups_Add(AM_SEV_INFO, NULL, L"Cups %d and %d are both named '%ls'. Players cannot tell them apart.",
                         j + 1, i + 1, sn);
                break;
            }
        }
    }

    if (!bad && !pending)
        Cups_Add(AM_SEV_OK, NULL, L"%ls: 4 tracks, ready.", who);
}

// Rechecks everything and fills the message list: errors first.
static void Cups_Evaluate(struct CupsState *s)
{
    static const int order[5] = { AM_SEV_ERROR, AM_SEV_WARNING, AM_SEV_NOTE, AM_SEV_INFO, AM_SEV_OK };
    int i, o;

    s_msgCount = 0;
    if (s->loaded) {
        for (i = 0; i < s->loadMsgCount; i++)
            Cups_Add(s->loadMsgs[i].sev, s->loadMsgs[i].detail, L"%ls", s->loadMsgs[i].text);
        if (s->infoError[0])
            Cups_Add(AM_SEV_WARNING, NULL, L"%ls", s->infoError);
        if (s->tooMany)
            Cups_Add(AM_SEV_WARNING, NULL,
                     L"This folder has more than 64 containers. The game reads at most 64; this list "
                     L"shows the first 64 found.");
        if (s->containerCount == 0)
            Cups_Add(AM_SEV_INFO, NULL,
                     L"There are no containers (.rldtrack files) in this folder. Pack a track on the "
                     L"Track page and put the container here.");
        if (s->cupCount == 0)
            Cups_Add(AM_SEV_INFO, NULL,
                     L"There are no cups yet. Press New cup to make one. Without a cup, CUSTOM CUP "
                     L"stays locked in the game.");
        for (i = 0; i < s->cupCount; i++)
            Cups_CheckCup(s, i);
    }

    s->errorCount = 0;
    s->warningCount = 0;
    for (i = 0; i < s_msgCount; i++) {
        if (s_msgs[i].sev == AM_SEV_ERROR)
            s->errorCount++;
        else if (s_msgs[i].sev == AM_SEV_WARNING)
            s->warningCount++;
    }

    Am_MsgListClear(s->msgList);
    Am_SetText(s->msgList, s->loaded ? L"Nothing to report."
                                     : L"Choose the tracks folder of the game. The check of your cups appears here.");
    if (s->infoJob)
        Am_MsgListAdd(s->msgList, AM_SEV_INFO, L"Reading the containers in this folder...", NULL);
    for (o = 0; o < 5; o++)
        for (i = 0; i < s_msgCount; i++)
            if (s_msgs[i].sev == order[o])
                Am_MsgListAdd(s->msgList, s_msgs[i].sev, s_msgs[i].text, s_msgs[i].detail);
    Cups_UpdateHeadline(s);
}

static void Cups_Relayout(struct CupsState *s)
{
    RECT rc;
    GetClientRect(s->page, &rc);
    Cups_Layout(s->page, rc.right, rc.bottom);
    InvalidateRect(s->page, NULL, TRUE);
}

static void Cups_RefreshAll(struct CupsState *s, int trackSel)
{
    Cups_FillCups(s);
    Cups_FillCup(s, trackSel);
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
}

static void Cups_JobDone(struct CupsState *s, int code)
{
    wchar_t keep[MAX_PATH];
    int i, missing = 0;

    Cups_SelectedFile(s, keep, MAX_PATH);
    s->infoJob = 0;
    s->infoCur = -1;
    for (i = 0; i < s->containerCount; i++) {
        struct CupsContainer *c = &s->containers[i];
        if (c->state == CUPS_INFO_PENDING) {
            c->state = CUPS_INFO_NONE;
            missing++;
        } else if (c->state == CUPS_INFO_OK && !c->sawRaceMode && c->modes[0]) {
            c->race = Cups_ModesHaveRace(c->modes);
        }
    }
    if (missing > 0)
        swprintf(s->infoError, 256,
                 L"%d container%ls could not be checked (the checker ended with code %d). Press Reload to try again.",
                 missing, Cups_S(missing), code);
    Cups_FillContainers(s, keep);
    Cups_FillCup(s, Cups_TrackSel(s));
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
    if (Am_Automating())
        Am_AutoLog(L"  cups: %d of %d container(s) checked", s->containerCount - missing, s->containerCount);
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

static void Cups_SetDirty(struct CupsState *s)
{
    s->dirty = 1;
    s->savedNow = 0;
}

static void Cups_SelectCup(struct CupsState *s, int i)
{
    if (i < 0 || i >= s->cupCount)
        return;
    s->selCup = i;
    SendMessageW(s->cupList, LB_SETCURSEL, (WPARAM)i, 0);
    Cups_FillCup(s, -1);
    Cups_UpdateButtons(s);
}

static int Cups_NameTaken(const struct CupsState *s, const wchar_t *name)
{
    wchar_t nm[CUPS_TEXT_CAP];
    int i;
    for (i = 0; i < s->cupCount; i++) {
        Cups_TrimCopy(nm, CUPS_TEXT_CAP, s->cups[i].name);
        if (Cups_CompareNames(nm, name) == 0)
            return 1;
    }
    return 0;
}

static int Cups_NewCup(struct CupsState *s, const wchar_t *name)
{
    struct CupsCup *cup;
    int k;
    if (!s->loaded || s->cupCount >= CUPS_MAX_CUPS)
        return 0;
    cup = &s->cups[s->cupCount];
    memset(cup, 0, sizeof(*cup));
    if (name && *name) {
        Cups_Copy(cup->name, CUPS_TEXT_CAP, name);
    } else {
        Cups_Copy(cup->name, CUPS_TEXT_CAP, L"New Cup");
        for (k = 2; k < 100 && Cups_NameTaken(s, cup->name); k++)
            swprintf(cup->name, CUPS_TEXT_CAP, L"New Cup %d", k);
    }
    s->selCup = s->cupCount++;
    Cups_SetDirty(s);
    Cups_RefreshAll(s, -1);
    return 1;
}

static int Cups_DeleteCup(struct CupsState *s)
{
    wchar_t nm[CUPS_TEXT_CAP], who[64], q[256];
    struct CupsCup *cup;
    int i;
    if (!Cups_HasCup(s))
        return 0;
    cup = &s->cups[s->selCup];
    if (cup->trackCount > 0) {
        Cups_TrimCopy(nm, CUPS_TEXT_CAP, cup->name);
        Cups_Who(nm, s->selCup, who, 64);
        swprintf(q, 256, L"Delete %ls and its list of %d track%ls?\n\nThe containers stay in the folder.",
                 who, cup->trackCount, Cups_S(cup->trackCount));
        if (!Am_AskYesNo(Am_MainWindow(), L"Delete cup?", q))
            return 0;
    }
    for (i = s->selCup; i < s->cupCount - 1; i++)
        s->cups[i] = s->cups[i + 1];
    s->cupCount--;
    memset(&s->cups[s->cupCount], 0, sizeof(s->cups[0]));
    if (s->selCup >= s->cupCount)
        s->selCup = s->cupCount - 1;
    Cups_SetDirty(s);
    Cups_RefreshAll(s, -1);
    return 1;
}

static int Cups_MoveCup(struct CupsState *s, int dir)
{
    struct CupsCup tmp;
    int to;
    if (!Cups_HasCup(s))
        return 0;
    to = s->selCup + dir;
    if (to < 0 || to >= s->cupCount)
        return 0;
    tmp = s->cups[to];
    s->cups[to] = s->cups[s->selCup];
    s->cups[s->selCup] = tmp;
    s->selCup = to;
    Cups_SetDirty(s);
    Cups_RefreshAll(s, Cups_TrackSel(s));
    return 1;
}

// The name field applies immediately, character by character.
static void Cups_NameEdited(struct CupsState *s)
{
    wchar_t *t;
    if (!Cups_HasCup(s))
        return;
    t = Am_GetText(s->nameEdit);
    if (wcscmp(t, s->cups[s->selCup].name) != 0) {
        Cups_Copy(s->cups[s->selCup].name, CUPS_TEXT_CAP, t);
        Cups_SetDirty(s);
        Cups_UpdateCupItem(s, s->selCup);
        Cups_UpdateNameHint(s);
        Cups_Evaluate(s);
    }
    Am_Free(t);
}

static int Cups_AddTrack(struct CupsState *s, int c)
{
    struct CupsCup *cup;
    if (!Cups_HasCup(s) || c < 0 || c >= s->containerCount)
        return 0;
    cup = &s->cups[s->selCup];
    if (cup->trackCount >= CUPS_TRACKS)
        return 0;
    Cups_Copy(cup->track[cup->trackCount++], CUPS_TEXT_CAP, s->containers[c].file);
    Cups_SetDirty(s);
    Cups_FillCup(s, cup->trackCount - 1);
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
    return 1;
}

static int Cups_RemoveTrack(struct CupsState *s, int t)
{
    struct CupsCup *cup;
    int i;
    if (!Cups_HasCup(s))
        return 0;
    cup = &s->cups[s->selCup];
    if (t < 0 || t >= cup->trackCount)
        return 0;
    for (i = t; i < cup->trackCount - 1; i++)
        wcscpy(cup->track[i], cup->track[i + 1]);
    cup->trackCount--;
    cup->track[cup->trackCount][0] = 0;
    Cups_SetDirty(s);
    Cups_FillCup(s, t < cup->trackCount ? t : cup->trackCount - 1);
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
    return 1;
}

static int Cups_MoveTrack(struct CupsState *s, int t, int dir)
{
    struct CupsCup *cup;
    wchar_t tmp[CUPS_TEXT_CAP];
    int to;
    if (!Cups_HasCup(s))
        return 0;
    cup = &s->cups[s->selCup];
    to = t + dir;
    if (t < 0 || t >= cup->trackCount || to < 0 || to >= cup->trackCount)
        return 0;
    wcscpy(tmp, cup->track[to]);
    wcscpy(cup->track[to], cup->track[t]);
    wcscpy(cup->track[t], tmp);
    Cups_SetDirty(s);
    Cups_FillCup(s, to);
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
    return 1;
}

// ---------------------------------------------------------------------------
// Folder
// ---------------------------------------------------------------------------

static void Cups_CleanPath(const wchar_t *in, wchar_t *out, int cap)
{
    wchar_t tmp[MAX_PATH];
    size_t n;
    DWORD r;

    out[0] = 0;
    Cups_TrimCopy(tmp, MAX_PATH, in);
    n = wcslen(tmp);
    if (n >= 2 && tmp[0] == L'"' && tmp[n - 1] == L'"') {
        memmove(tmp, tmp + 1, (n - 2) * sizeof(wchar_t));
        tmp[n - 2] = 0;
    }
    if (!tmp[0])
        return;
    r = GetFullPathNameW(tmp, (DWORD)cap, out, NULL);
    if (r == 0 || r >= (DWORD)cap)
        Cups_Copy(out, cap, tmp);
    n = wcslen(out);
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
}

// The game's track folder: the base is the first of (folder of the exe,
// its parent, its grandparent) with assets\BIGFILE.BIG or assets\ctr-u.bin.
static void Cups_DefaultFolder(wchar_t *out, int cap)
{
    wchar_t exe[MAX_PATH], dir[MAX_PATH], up[MAX_PATH], big[MAX_PATH], bin[MAX_PATH];
    int level;

    Am_ConfigGet(L"cups.folder", out, cap);
    if (out[0] && Am_DirExists(out))
        return;
    out[0] = 0;
    if (!Am_FindGameExe(exe, MAX_PATH))
        return;
    Am_PathDir(dir, MAX_PATH, exe);
    for (level = 0; level < 3 && dir[0]; level++) {
        Am_PathJoin(big, MAX_PATH, dir, L"assets\\BIGFILE.BIG");
        Am_PathJoin(bin, MAX_PATH, dir, L"assets\\ctr-u.bin");
        if (Am_FileExists(big) || Am_FileExists(bin)) {
            Am_PathJoin(big, MAX_PATH, dir, L"tracks");
            if (Am_DirExists(big))
                Cups_Copy(out, cap, big);
            return;
        }
        Am_PathDir(up, MAX_PATH, dir);
        if (!up[0] || wcscmp(up, dir) == 0)
            break;
        wcscpy(dir, up);
    }
}

// Return: -1 cancelled or error, 0 done, 1 rldpack running.
static int Cups_LoadFolder(struct CupsState *s, const wchar_t *folder)
{
    wchar_t dir[MAX_PATH], msg[MAX_PATH + 160];
    int r;

    Cups_CleanPath(folder, dir, MAX_PATH);
    if (!dir[0]) {
        Am_Tell(Am_MainWindow(), L"No folder", L"Choose the tracks folder of the game first.");
        return -1;
    }
    if (!Am_DirExists(dir)) {
        swprintf(msg, MAX_PATH + 160, L"The folder\n%ls\ndoes not exist. Choose the tracks folder of the game.", dir);
        Am_Tell(Am_MainWindow(), L"Folder not found", msg);
        return -1;
    }
    if (s->dirty && !Am_AskYesNo(Am_MainWindow(), L"Discard unsaved changes?",
                                 L"The cups have unsaved changes. Load the folder anyway and lose them?")) {
        if (s->loaded)
            Am_SetText(s->folderEdit, s->folder);
        return -1;
    }

    Cups_Copy(s->folder, MAX_PATH, dir);
    Am_SetText(s->folderEdit, dir);
    Am_ConfigSet(L"cups.folder", dir);
    s->loaded = 1;
    s->dirty = 0;
    s->savedNow = 0;
    Cups_ReadCupsFile(s);
    s->selCup = s->cupCount > 0 ? 0 : -1;
    r = Cups_ScanContainers(s);

    Cups_UpdateStatus(s);
    Cups_FillContainers(s, NULL);
    Cups_RefreshAll(s, -1);
    Cups_Relayout(s);
    if (Am_Automating())
        Am_AutoLog(L"  cups: %ls - %d container(s), %d cup(s) in cups.txt", dir, s->containerCount, s->cupCount);
    return r;
}

static int Cups_LoadFromEdit(struct CupsState *s)
{
    wchar_t *t = Am_GetText(s->folderEdit);
    int r = Cups_LoadFolder(s, t);
    Am_Free(t);
    return r;
}

static void Cups_Browse(struct CupsState *s)
{
    wchar_t out[MAX_PATH], cur[MAX_PATH];
    wchar_t *t = Am_GetText(s->folderEdit);
    Cups_CleanPath(t, cur, MAX_PATH);
    Am_Free(t);
    if (!cur[0] && s->loaded)
        Cups_Copy(cur, MAX_PATH, s->folder);
    if (Am_BrowseFolder(Am_MainWindow(), L"Choose the tracks folder of the game", cur, out, MAX_PATH))
        Cups_LoadFolder(s, out);
}

// Reread only the containers, the cups stay (e.g. after a build).
static void Cups_Rescan(struct CupsState *s)
{
    wchar_t keep[MAX_PATH];
    int t = Cups_TrackSel(s);
    Cups_SelectedFile(s, keep, MAX_PATH);
    Cups_ScanContainers(s);
    Cups_FillContainers(s, keep);
    Cups_FillCup(s, t);
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
    Cups_Relayout(s);
}

static void Cups_Shown(struct CupsState *s)
{
    wchar_t dir[MAX_PATH];
    if (s->shown) {
        // Back on the page: reread only if a container has changed.
        if (s->loaded && !s->infoJob && Cups_FolderStamp(s->folder) != s->folderStamp)
            Cups_Rescan(s);
        return;
    }
    s->shown = 1;
    Cups_DefaultFolder(dir, MAX_PATH);
    Am_SetText(s->folderEdit, dir);
    if (!dir[0] || Cups_LoadFolder(s, dir) < 0) {
        Cups_UpdateStatus(s);
        Cups_Evaluate(s);
        Cups_UpdateButtons(s);
    }
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

static wchar_t *Cups_BuildFile(const struct CupsState *s)
{
    size_t cap = 2048 + (size_t)s->cupCount * (CUPS_TRACKS + 1) * (CUPS_TEXT_CAP + 16);
    wchar_t *t = Am_Alloc(cap * sizeof(wchar_t));
    wchar_t nm[CUPS_TEXT_CAP];
    int i, k;

    Cups_Cat(t, cap, L"# cups.txt - custom cups for ARCADE -> CUSTOM CUP\r\n");
    Cups_Cat(t, cap, L"# Written by the CTR Reload Alpha-Maker.\r\n");
    Cups_Cat(t, cap, L"# One cup: a line \"cup = <name>\", then exactly four lines \"track = <file>\".\r\n");
    Cups_Cat(t, cap, L"# Track files are looked up in this folder; upper/lower case does not matter.\r\n");
    Cups_Cat(t, cap, L"# At most 4 cups. Lines starting with # are comments.\r\n");
    for (i = 0; i < s->cupCount; i++) {
        const struct CupsCup *cup = &s->cups[i];
        Cups_TrimCopy(nm, CUPS_TEXT_CAP, cup->name);
        Cups_Cat(t, cap, L"\r\ncup   = ");
        Cups_Cat(t, cap, nm);
        Cups_Cat(t, cap, L"\r\n");
        for (k = 0; k < cup->trackCount; k++) {
            Cups_Cat(t, cap, L"track = ");
            Cups_Cat(t, cap, cup->track[k]);
            Cups_Cat(t, cap, L"\r\n");
        }
    }
    return t;
}

static int Cups_AskOverwrite(const struct CupsState *s)
{
    wchar_t text[1400], one[200];
    int i;

    if (s->fileUnreadable && s->unkeptTotal == 0)
        return Am_AskYesNo(Am_MainWindow(), L"Replace cups.txt?",
                           L"cups.txt could not be read, so this page cannot keep what is in it. "
                           L"Saving replaces the whole file. Save anyway?");
    Cups_Copy(text, 1400, L"cups.txt has lines this page does not keep:\n\n");
    for (i = 0; i < s->unkeptCount && i < 3; i++) {
        swprintf(one, 200, L"Line %d: %ls (%ls)\n", s->unkept[i].line, s->unkept[i].text, s->unkept[i].why);
        Cups_Cat(text, 1400, one);
    }
    if (s->unkeptTotal > 3) {
        swprintf(one, 200, L"...and %d more\n", s->unkeptTotal - 3);
        Cups_Cat(text, 1400, one);
    }
    Cups_Cat(text, 1400, L"\nSaving removes them. Save anyway?");
    return Am_AskYesNo(Am_MainWindow(), L"Overwrite lines in cups.txt?", text);
}

static int Cups_Save(struct CupsState *s)
{
    wchar_t path[MAX_PATH], msg[MAX_PATH + 300];
    wchar_t *text;
    int ok;

    if (!s->loaded) {
        Am_Tell(Am_MainWindow(), L"No folder", L"Choose the tracks folder of the game first.");
        return 0;
    }
    Cups_Evaluate(s);
    if (s->errorCount > 0 &&
        !Am_AskYesNo(Am_MainWindow(), L"Save with problems?",
                     L"Some cups have problems (see Check). The game leaves such cups out. Save anyway?"))
        return 0;
    if ((s->unkeptTotal > 0 || s->fileUnreadable) && !Cups_AskOverwrite(s))
        return 0;

    text = Cups_BuildFile(s);
    Am_PathJoin(path, MAX_PATH, s->folder, L"cups.txt");
    ok = Am_WriteTextFile(path, text);
    Am_Free(text);
    if (!ok) {
        swprintf(msg, MAX_PATH + 300,
                 L"cups.txt could not be written to\n%ls\n\nThe folder may be write-protected, or another "
                 L"program has cups.txt open. Your cups are still here - fix the cause and press Save again.",
                 s->folder);
        Am_Tell(Am_MainWindow(), L"Could not save cups.txt", msg);
        return 0;
    }

    s->dirty = 0;
    s->savedNow = 1;
    s->fileExists = 1;
    s->fileUnreadable = 0;
    s->fileCups = s->cupCount;
    s->unkeptCount = 0;
    s->unkeptTotal = 0;
    s->loadMsgCount = 0;
    Cups_UpdateStatus(s);
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
    if (Am_Automating())
        Am_AutoLog(L"  cups: saved %ls", path);
    return 1;
}

// ---------------------------------------------------------------------------
// Report (automation "report"): the whole visible state as UTF-8
// ---------------------------------------------------------------------------

static void Cups_Put(FILE *f, const wchar_t *fmt, ...)
{
    wchar_t *buf = Am_Alloc(4096 * sizeof(wchar_t));
    char *u;
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 4095, fmt, ap);
    va_end(ap);
    buf[4095] = 0;
    u = Am_ToUtf8(buf);
    fputs(u, f);
    Am_Free(u);
    Am_Free(buf);
}

static void Cups_PutControl(FILE *f, const wchar_t *label, HWND h)
{
    wchar_t *t = Am_GetText(h);
    Cups_Put(f, L"%ls\t%ls%ls\n", label, t, IsWindowEnabled(h) ? L"" : L"\t(disabled)");
    Am_Free(t);
}

static int Cups_Report(struct CupsState *s, const wchar_t *path)
{
    HWND buttons[] = { s->browseBtn, s->reloadBtn, s->addBtn, s->newBtn, s->deleteBtn, s->cupUpBtn,
                       s->cupDownBtn, s->removeBtn, s->trackUpBtn, s->trackDownBtn, s->saveBtn };
    wchar_t cells[4][512], line[768];
    FILE *f;
    int i, k, rows, count;

    if (!path || !path[0])
        return 0;
    f = _wfopen(path, L"wb");
    if (!f) {
        Am_AutoLog(L"  cups: could not write %ls", path);
        return 0;
    }
    Cups_Put(f, L"Alpha-Maker - Cups page\n");
    Cups_Put(f, L"folder\t%ls\n", s->loaded ? s->folder : L"(none loaded)");
    Cups_PutControl(f, L"folder field", s->folderEdit);
    Cups_PutControl(f, L"cups.txt", s->folderStatus);
    Cups_Put(f, L"busy\t%ls\n", s->infoJob ? L"yes" : L"no");
    Cups_Put(f, L"unsaved changes\t%ls\n", s->dirty ? L"yes" : L"no");

    rows = ListView_GetItemCount(s->contList);
    Cups_Put(f, L"\ncontainers\t%d\n", rows);
    Cups_Put(f, L"#\tName\tFile\tRace\tNotes\n");
    for (i = 0; i < rows; i++) {
        for (k = 0; k < 4; k++) {
            cells[k][0] = 0;
            ListView_GetItemText(s->contList, i, k, cells[k], 512);
        }
        Cups_Put(f, L"%d\t%ls\t%ls\t%ls\t%ls%ls\n", i + 1, cells[0], cells[1], cells[2], cells[3],
                 i == Cups_ContSel(s) ? L"\t(selected)" : L"");
    }
    Cups_PutControl(f, L"container hint", s->contHint);

    Cups_Put(f, L"\ncups\t%d\n", s->cupCount);
    for (i = 0; i < s->cupCount; i++) {
        Cups_CupText(s, i, line, 768);
        Cups_Put(f, L"%ls%ls\n", line, i == s->selCup ? L"\t(selected)" : L"");
        for (k = 0; k < s->cups[i].trackCount; k++) {
            Cups_TrackText(s, s->cups[i].track[k], k + 1, line, 768);
            Cups_Put(f, L"\t%ls\n", line);
        }
    }
    if (Cups_HasCup(s))
        Cups_Put(f, L"selected cup\t%d\n", s->selCup + 1);
    else
        Cups_Put(f, L"selected cup\tnone\n");
    Cups_PutControl(f, L"name field", s->nameEdit);
    Cups_PutControl(f, L"name hint", s->nameHint);
    Cups_PutControl(f, L"tracks", s->tracksCount);
    count = (int)SendMessageW(s->trackList, LB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) {
        int n = (int)SendMessageW(s->trackList, LB_GETTEXTLEN, (WPARAM)i, 0);
        if (n < 0 || n >= 768)
            continue;
        SendMessageW(s->trackList, LB_GETTEXT, (WPARAM)i, (LPARAM)line);
        Cups_Put(f, L"track list\t%ls%ls\n", line, i == Cups_TrackSel(s) ? L"\t(selected)" : L"");
    }

    Cups_Put(f, L"\n");
    for (i = 0; i < (int)(sizeof(buttons) / sizeof(buttons[0])); i++)
        Cups_PutControl(f, L"button", buttons[i]);

    Cups_Put(f, L"\n");
    Cups_PutControl(f, L"headline", s->headline);
    Cups_PutControl(f, L"counts", s->counts);
    Cups_Put(f, L"\nmessages\t%d\n", Am_MsgListCount(s->msgList));
    Am_MsgListWrite(s->msgList, f);
    fclose(f);
    return 1;
}

// ---------------------------------------------------------------------------
// Callbacks of the page
// ---------------------------------------------------------------------------

static void Cups_AddStyle(HWND h, DWORD style)
{
    SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | (LONG_PTR)style);
}

static void Cups_Create(HWND page)
{
    static const wchar_t *const heads[4] = { L"Name", L"File", L"Race", L"Notes" };
    struct CupsState *s = &s_cups;
    LVCOLUMNW col;
    int i;

    s->page = page;
    s->selCup = -1;
    s->infoCur = -1;

    s->folderEdit = Am_Edit(page, CUPS_ID_FOLDER, L"", 0);
    SendMessageW(s->folderEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Choose the tracks folder of the game.");
    s->browseBtn = Am_Button(page, CUPS_ID_BROWSE, L"Browse...");
    s->reloadBtn = Am_Button(page, CUPS_ID_RELOAD, L"Reload");
    s->folderStatus = Am_Label(page, CUPS_ID_STATUS, L"", AM_FONT_BODY);
    Cups_AddStyle(s->folderStatus, SS_RIGHT | SS_ENDELLIPSIS);

    s->contList = Am_ListView(page, CUPS_ID_CONTAINERS, LVS_NOSORTHEADER);
    ListView_SetExtendedListViewStyleEx(s->contList, LVS_EX_INFOTIP, LVS_EX_INFOTIP);
    for (i = 0; i < 4; i++) {
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)heads[i];
        col.cx = Am_Px(100);
        col.iSubItem = i;
        ListView_InsertColumn(s->contList, i, &col);
    }
    s->addBtn = Am_Button(page, CUPS_ID_ADD, L"Add selected container");
    s->contHint = Am_Label(page, CUPS_ID_CONTHINT, L"", AM_FONT_SMALL);
    Cups_AddStyle(s->contHint, SS_ENDELLIPSIS);
    Am_SetTextColor(s->contHint, AM_COL_MUTED);

    s->cupList = Am_ListBox(page, CUPS_ID_CUPLIST, 0);
    s->newBtn = Am_Button(page, CUPS_ID_NEWCUP, L"New cup");
    s->deleteBtn = Am_Button(page, CUPS_ID_DELCUP, L"Delete");
    s->cupUpBtn = Am_Button(page, CUPS_ID_CUPUP, L"Move up");
    s->cupDownBtn = Am_Button(page, CUPS_ID_CUPDOWN, L"Move down");

    s->nameLabel = Am_Label(page, CUPS_ID_NAMELABEL, L"Name", AM_FONT_BOLD);
    s->nameEdit = Am_Edit(page, CUPS_ID_NAME, L"", 0);
    SendMessageW(s->nameEdit, EM_LIMITTEXT, CUPS_NAME_BYTES, 0);
    s->nameHint = Am_Label(page, CUPS_ID_NAMEHINT, L"", AM_FONT_SMALL);
    Am_SetTextColor(s->nameHint, AM_COL_MUTED);
    s->tracksLabel = Am_Label(page, CUPS_ID_TRACKSLABEL, L"Tracks", AM_FONT_BOLD);
    s->tracksCount = Am_Label(page, CUPS_ID_TRACKSCOUNT, L"", AM_FONT_SMALL);
    Cups_AddStyle(s->tracksCount, SS_RIGHT);
    Am_SetTextColor(s->tracksCount, AM_COL_MUTED);
    s->trackList = Am_ListBox(page, CUPS_ID_TRACKLIST, 0);
    s->removeBtn = Am_Button(page, CUPS_ID_REMOVE, L"Remove");
    s->trackUpBtn = Am_Button(page, CUPS_ID_TRACKUP, L"Move up");
    s->trackDownBtn = Am_Button(page, CUPS_ID_TRACKDOWN, L"Move down");

    s->msgList = Am_MsgList(page, CUPS_ID_MSGLIST);
    s->headline = Am_Label(page, CUPS_ID_HEADLINE, L"", AM_FONT_BOLD);
    s->counts = Am_Label(page, CUPS_ID_COUNTS, L"", AM_FONT_SMALL);
    Am_SetTextColor(s->counts, AM_COL_MUTED);
    s->saveBtn = Am_PrimaryButton(page, CUPS_ID_SAVE, L"Save cups.txt");

    Cups_UpdateStatus(s);
    Cups_Evaluate(s);
    Cups_UpdateButtons(s);
    Cups_UpdateNameHint(s);
}

static void Cups_SizeColumns(struct CupsState *s, int width)
{
    int w = width - GetSystemMetrics(SM_CXVSCROLL) - Am_Px(4);
    int race = Am_Px(60);
    int name = (w - race) * 30 / 100;
    int file = (w - race) * 33 / 100;
    ListView_SetColumnWidth(s->contList, 0, name);
    ListView_SetColumnWidth(s->contList, 1, file);
    ListView_SetColumnWidth(s->contList, 2, race);
    ListView_SetColumnWidth(s->contList, 3, w - race - name - file);
}

// At the top the folder, below it containers | cups | cup, at the bottom the check.
static void Cups_Layout(HWND page, int w, int h)
{
    struct CupsState *s = &s_cups;
    int left = Am_Px(32), right = w - Am_Px(32);
    int top = Am_PageTop(), bottom = h - Am_Px(24);
    int gap = Am_Px(16), g8 = Am_Px(8);
    int bh = Am_Px(32), eh = Am_Px(28), lh = Am_Px(20), sh = Am_Px(18);
    int bw = Am_Px(104), addW = Am_Px(172), labelW = Am_Px(56), panelW = Am_Px(260);
    int checkH, midTop, midBottom, avail, cupsW, cupW, contW, x, y, iw, half, third;
    RECT card, in;
    wchar_t title[48];

    Am_CardClear(page);

    // Track folder; the state of cups.txt is on the right in the title line.
    card.left = left;
    card.top = top;
    card.right = right;
    card.bottom = top + Am_Px(110);
    Am_CardAdd(page, &card, L"Tracks folder");
    in = Am_CardInner(&card, 1);
    y = in.top + (in.bottom - in.top - bh) / 2;
    MoveWindow(s->reloadBtn, in.right - bw, y, bw, bh, TRUE);
    MoveWindow(s->browseBtn, in.right - 2 * bw - g8, y, bw, bh, TRUE);
    MoveWindow(s->folderEdit, in.left, y + (bh - eh) / 2, in.right - 2 * bw - 2 * g8 - in.left, eh, TRUE);
    MoveWindow(s->folderStatus, in.left + Am_Px(160), card.top + Am_Px(17),
               in.right - in.left - Am_Px(160), lh, TRUE);

    checkH = (bottom - top) * 28 / 100;
    if (checkH < Am_Px(180))
        checkH = Am_Px(180);
    midTop = card.bottom + gap;
    midBottom = bottom - checkH - gap;

    avail = right - left - 2 * gap;
    cupsW = avail * 21 / 100;
    if (cupsW < Am_Px(196))
        cupsW = Am_Px(196);
    cupW = avail * 34 / 100;
    if (cupW < Am_Px(290))
        cupW = Am_Px(290);
    contW = avail - cupsW - cupW;

    // Containers
    card.left = left;
    card.top = midTop;
    card.right = left + contW;
    card.bottom = midBottom;
    if (s->loaded)
        swprintf(title, 48, L"Containers (%d)", s->containerCount);
    else
        Cups_Copy(title, 48, L"Containers");
    Am_CardAdd(page, &card, title);
    in = Am_CardInner(&card, 1);
    iw = in.right - in.left;
    MoveWindow(s->contList, in.left, in.top, iw, in.bottom - bh - g8 - in.top, TRUE);
    Cups_SizeColumns(s, iw);
    MoveWindow(s->addBtn, in.left, in.bottom - bh, addW, bh, TRUE);
    MoveWindow(s->contHint, in.left + addW + Am_Px(10), in.bottom - bh + (bh - sh) / 2,
               iw - addW - Am_Px(10), sh, TRUE);

    // Cups
    x = card.right + gap;
    card.left = x;
    card.right = x + cupsW;
    Am_CardAdd(page, &card, L"Cups");
    in = Am_CardInner(&card, 1);
    iw = in.right - in.left;
    half = (iw - g8) / 2;
    y = in.bottom - bh;
    MoveWindow(s->cupList, in.left, in.top, iw, y - bh - 2 * g8 - in.top, TRUE);
    MoveWindow(s->newBtn, in.left, y - bh - g8, half, bh, TRUE);
    MoveWindow(s->deleteBtn, in.left + half + g8, y - bh - g8, iw - half - g8, bh, TRUE);
    MoveWindow(s->cupUpBtn, in.left, y, half, bh, TRUE);
    MoveWindow(s->cupDownBtn, in.left + half + g8, y, iw - half - g8, bh, TRUE);

    // The chosen cup
    x = card.right + gap;
    card.left = x;
    card.right = right;
    Am_CardAdd(page, &card, L"Cup");
    in = Am_CardInner(&card, 1);
    iw = in.right - in.left;
    y = in.top;
    MoveWindow(s->nameLabel, in.left, y + Am_Px(4), labelW, lh, TRUE);
    MoveWindow(s->nameEdit, in.left + labelW, y, iw - labelW, eh, TRUE);
    y += eh + Am_Px(4);
    MoveWindow(s->nameHint, in.left + labelW, y, iw - labelW, Am_Px(32), TRUE);
    y += Am_Px(32) + Am_Px(6);
    MoveWindow(s->tracksLabel, in.left, y, iw / 2, lh, TRUE);
    MoveWindow(s->tracksCount, in.left + iw / 2, y + Am_Px(2), iw - iw / 2, sh, TRUE);
    y += lh + Am_Px(4);
    third = (iw - 2 * g8) / 3;
    MoveWindow(s->trackList, in.left, y, iw, in.bottom - bh - g8 - y, TRUE);
    MoveWindow(s->removeBtn, in.left, in.bottom - bh, third, bh, TRUE);
    MoveWindow(s->trackUpBtn, in.left + third + g8, in.bottom - bh, third, bh, TRUE);
    MoveWindow(s->trackDownBtn, in.left + 2 * (third + g8), in.bottom - bh, iw - 2 * (third + g8), bh, TRUE);

    // Check: messages on the left, state and saving on the right.
    card.left = left;
    card.top = bottom - checkH;
    card.right = right;
    card.bottom = bottom;
    Am_CardAdd(page, &card, L"Check");
    in = Am_CardInner(&card, 1);
    x = in.right - panelW;
    MoveWindow(s->msgList, in.left, in.top, x - Am_Px(24) - in.left, in.bottom - in.top, TRUE);
    MoveWindow(s->headline, x, in.top, panelW, Am_Px(40), TRUE);
    MoveWindow(s->counts, x, in.top + Am_Px(44), panelW, sh, TRUE);
    MoveWindow(s->saveBtn, x, in.bottom - bh, panelW, bh, TRUE);
}

// Enter (IDOK) confirms the field with the focus.
static void Cups_Confirm(struct CupsState *s)
{
    HWND focus = GetFocus();
    if (focus == s->folderEdit) {
        Cups_LoadFromEdit(s);
    } else if (focus == s->contList) {
        int c = Cups_ContSel(s);
        if (c >= 0 && !Cups_AddTrack(s, c))
            MessageBeep(MB_OK);
    } else if (focus == s->nameEdit) {
        // The name applies already while typing.
        SendMessageW(s->nameEdit, EM_SETSEL, 0, -1);
    }
}

static LRESULT Cups_Command(HWND page, WPARAM wParam, LPARAM lParam)
{
    struct CupsState *s = &s_cups;
    int id = LOWORD(wParam), code = HIWORD(wParam);
    (void)page;
    (void)lParam;

    switch (id) {
    case IDOK:
        Cups_Confirm(s);
        return 0;
    case IDCANCEL:
        return 0;
    case CUPS_ID_BROWSE:
        if (code == BN_CLICKED)
            Cups_Browse(s);
        return 0;
    case CUPS_ID_RELOAD:
        if (code == BN_CLICKED)
            Cups_LoadFromEdit(s);
        return 0;
    case CUPS_ID_ADD:
        if (code == BN_CLICKED && !Cups_AddTrack(s, Cups_ContSel(s)))
            MessageBeep(MB_OK);
        return 0;
    case CUPS_ID_CUPLIST:
        if (code == LBN_SELCHANGE) {
            int i = (int)SendMessageW(s->cupList, LB_GETCURSEL, 0, 0);
            if (i >= 0)
                Cups_SelectCup(s, i);
        }
        return 0;
    case CUPS_ID_NEWCUP:
        if (code == BN_CLICKED && Cups_NewCup(s, NULL)) {
            SetFocus(s->nameEdit);
            SendMessageW(s->nameEdit, EM_SETSEL, 0, -1);
        }
        return 0;
    case CUPS_ID_DELCUP:
        if (code == BN_CLICKED)
            Cups_DeleteCup(s);
        return 0;
    case CUPS_ID_CUPUP:
        if (code == BN_CLICKED)
            Cups_MoveCup(s, -1);
        return 0;
    case CUPS_ID_CUPDOWN:
        if (code == BN_CLICKED)
            Cups_MoveCup(s, 1);
        return 0;
    case CUPS_ID_NAME:
        if (code == EN_CHANGE && !s->updating)
            Cups_NameEdited(s);
        return 0;
    case CUPS_ID_TRACKLIST:
        if (code == LBN_SELCHANGE)
            Cups_UpdateButtons(s);
        return 0;
    case CUPS_ID_REMOVE:
        if (code == BN_CLICKED)
            Cups_RemoveTrack(s, Cups_TrackSel(s));
        return 0;
    case CUPS_ID_TRACKUP:
        if (code == BN_CLICKED)
            Cups_MoveTrack(s, Cups_TrackSel(s), -1);
        return 0;
    case CUPS_ID_TRACKDOWN:
        if (code == BN_CLICKED)
            Cups_MoveTrack(s, Cups_TrackSel(s), 1);
        return 0;
    case CUPS_ID_SAVE:
        if (code == BN_CLICKED)
            Cups_Save(s);
        return 0;
    }
    return 0;
}

static LRESULT Cups_Notify(HWND page, NMHDR *hdr)
{
    struct CupsState *s = &s_cups;
    (void)page;
    if (!hdr || hdr->idFrom != CUPS_ID_CONTAINERS)
        return 0;
    switch (hdr->code) {
    case LVN_ITEMCHANGED:
        Cups_UpdateButtons(s);
        return 0;
    case NM_DBLCLK: {
        NMITEMACTIVATE *act = (NMITEMACTIVATE *)hdr;
        if (act->iItem >= 0 && !Cups_AddTrack(s, act->iItem))
            MessageBeep(MB_OK);
        return 0;
    }
    case LVN_GETINFOTIPW: {
        // The whole line as a tooltip - the column "Notes" is often too narrow.
        NMLVGETINFOTIPW *tip = (NMLVGETINFOTIPW *)hdr;
        if (tip->iItem >= 0 && tip->iItem < s->containerCount && tip->pszText && tip->cchTextMax > 0) {
            const struct CupsContainer *c = &s->containers[tip->iItem];
            wchar_t race[32], notes[512];
            Cups_ContTexts(c, race, 32, notes, 512);
            swprintf(tip->pszText, (size_t)tip->cchTextMax, L"%ls\n%ls\nRace: %ls%ls%ls",
                     Cups_ContName(c), c->file, race, notes[0] ? L"\n" : L"", notes);
            tip->pszText[tip->cchTextMax - 1] = 0;
        }
        return 0;
    }
    case LVN_GETEMPTYMARKUP: {
        NMLVEMPTYMARKUP *em = (NMLVEMPTYMARKUP *)hdr;
        const wchar_t *text = !s->loaded ? L"Choose the tracks folder of the game."
                                         : L"No containers (.rldtrack files) in this folder.";
        em->dwFlags = EMF_CENTERED;
        Cups_Copy(em->szMarkup, L_MAX_URL_LENGTH, text);
        return TRUE;
    }
    }
    return 0;
}

static LRESULT Cups_Message(HWND page, UINT msg, WPARAM wParam, LPARAM lParam, int *handled)
{
    struct CupsState *s = &s_cups;
    (void)page;

    switch (msg) {
    case AM_WM_JOB_LINE: {
        wchar_t *line = (wchar_t *)lParam;
        if (s->infoJob && (int)wParam == s->infoJob && line)
            Cups_JobLine(s, line);
        Am_Free(line);
        *handled = 1;
        return 0;
    }
    case AM_WM_JOB_DONE:
        if (s->infoJob && (int)wParam == s->infoJob)
            Cups_JobDone(s, (int)lParam);
        *handled = 1;
        return 0;
    case AM_WM_PAGE_SHOWN:
        Cups_Shown(s);
        *handled = 1;
        return 0;
    case AM_WM_QUERY_CLOSE:
        if (s->dirty && !Am_AskYesNo(Am_MainWindow(), L"Close without saving?",
                                     L"The cups have unsaved changes. Close without saving them?")) {
            *handled = 1;
            return 0;
        }
        *handled = 0;
        return 0;
    }
    *handled = 0;
    return 0;
}

static int Cups_AutoResult(int loadResult)
{
    if (loadResult > 0)
        return AM_AUTO_WAIT;
    return loadResult == 0 ? AM_AUTO_DONE : AM_AUTO_FAIL;
}

static int Cups_AutoTrackIndex(struct CupsState *s, const wchar_t *arg)
{
    int t = Cups_Index(arg) - 1;
    if (!Cups_HasCup(s)) {
        Am_AutoLog(L"  cups: no cup is selected");
        return -1;
    }
    if (t < 0 || t >= s->cups[s->selCup].trackCount) {
        Am_AutoLog(L"  cups: the cup has no track %ls", arg);
        return -1;
    }
    SendMessageW(s->trackList, LB_SETCURSEL, (WPARAM)t, 0);
    Cups_UpdateButtons(s);
    return t;
}

static int Cups_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    struct CupsState *s = &s_cups;
    wchar_t a[CUPS_TEXT_CAP];
    int i, t;
    (void)page;

    Cups_TrimCopy(a, CUPS_TEXT_CAP, arg ? arg : L"");

    if (wcscmp(verb, L"dir") == 0) {
        Am_SetText(s->folderEdit, a);
        return Cups_AutoResult(Cups_LoadFromEdit(s));
    }
    if (wcscmp(verb, L"reload") == 0)
        return Cups_AutoResult(Cups_LoadFromEdit(s));
    if (wcscmp(verb, L"select") == 0) {
        i = Cups_Index(a) - 1;
        if (i < 0 || i >= s->cupCount) {
            wchar_t nm[CUPS_TEXT_CAP];
            for (i = 0; i < s->cupCount; i++) {
                Cups_TrimCopy(nm, CUPS_TEXT_CAP, s->cups[i].name);
                if (Cups_CompareNames(nm, a) == 0)
                    break;
            }
        }
        if (i < 0 || i >= s->cupCount) {
            Am_AutoLog(L"  cups: no cup '%ls'", a);
            return AM_AUTO_FAIL;
        }
        Cups_SelectCup(s, i);
        return AM_AUTO_DONE;
    }
    if (wcscmp(verb, L"new") == 0) {
        if (!s->loaded || s->cupCount >= CUPS_MAX_CUPS) {
            Am_AutoLog(L"  cups: %ls", !s->loaded ? L"no folder is loaded" : L"there are already 4 cups");
            return AM_AUTO_FAIL;
        }
        return Cups_NewCup(s, a) ? AM_AUTO_DONE : AM_AUTO_FAIL;
    }
    if (wcscmp(verb, L"rename") == 0) {
        if (!Cups_HasCup(s)) {
            Am_AutoLog(L"  cups: no cup is selected");
            return AM_AUTO_FAIL;
        }
        s->updating = 1;
        Am_SetText(s->nameEdit, a);
        s->updating = 0;
        Cups_NameEdited(s);
        return AM_AUTO_DONE;
    }
    if (wcscmp(verb, L"delete") == 0) {
        if (!Cups_HasCup(s)) {
            Am_AutoLog(L"  cups: no cup is selected");
            return AM_AUTO_FAIL;
        }
        return Cups_DeleteCup(s) ? AM_AUTO_DONE : AM_AUTO_FAIL;
    }
    if (wcscmp(verb, L"add") == 0) {
        int c = Cups_FindContainer(s, a);
        if (c < 0) {
            Am_AutoLog(L"  cups: no container '%ls' in the folder", a);
            return AM_AUTO_FAIL;
        }
        if (!Cups_HasCup(s)) {
            Am_AutoLog(L"  cups: no cup is selected");
            return AM_AUTO_FAIL;
        }
        if (s->cups[s->selCup].trackCount >= CUPS_TRACKS) {
            Am_AutoLog(L"  cups: the cup already has 4 tracks");
            return AM_AUTO_FAIL;
        }
        Cups_SelectContainer(s, c);
        return Cups_AddTrack(s, c) ? AM_AUTO_DONE : AM_AUTO_FAIL;
    }
    if (wcscmp(verb, L"remove") == 0) {
        t = Cups_AutoTrackIndex(s, a);
        return (t >= 0 && Cups_RemoveTrack(s, t)) ? AM_AUTO_DONE : AM_AUTO_FAIL;
    }
    if (wcscmp(verb, L"up") == 0 || wcscmp(verb, L"down") == 0) {
        int dir = verb[0] == L'u' ? -1 : 1;
        t = Cups_AutoTrackIndex(s, a);
        if (t < 0)
            return AM_AUTO_FAIL;
        if (!Cups_MoveTrack(s, t, dir)) {
            Am_AutoLog(L"  cups: track %d cannot move %ls", t + 1, dir < 0 ? L"up" : L"down");
            return AM_AUTO_FAIL;
        }
        return AM_AUTO_DONE;
    }
    if (wcscmp(verb, L"save") == 0)
        return Cups_Save(s) ? AM_AUTO_DONE : AM_AUTO_FAIL;
    if (wcscmp(verb, L"report") == 0)
        return Cups_Report(s, a) ? AM_AUTO_DONE : AM_AUTO_FAIL;
    return AM_AUTO_UNKNOWN;
}

static int Cups_Busy(HWND page)
{
    (void)page;
    return s_cups.infoJob != 0;
}

const struct AmPageDef g_amCupsPage = {
    L"Cups",
    L"Custom cups",
    L"Put four containers into a cup. The game reads the cups from cups.txt in its tracks folder.",
    Cups_Create,
    Cups_Layout,
    Cups_Command,
    Cups_Notify,
    Cups_Message,
    Cups_Automate,
    Cups_Busy
};
