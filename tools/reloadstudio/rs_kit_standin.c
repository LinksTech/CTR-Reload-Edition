// rs_kit_standin.c - a stand-in for blender.exe in the test of the Creator Kit
// page of Reload Studio
//
// Built only with the tests (CMakeLists.txt, ctest reloadstudio_kit). It holds
// no game data and no Blender: every file it writes is made up here (the game
// data of the test is rldpack's kit-standin, read by the real kit-extract).
// The Studio is pointed at it as Blender (kit.blender), called as "--threads 1
// --background --factory-startup --python-exit-code 1 --python <script> --
// --kit <dir> [options]", or "--version". The options after "--kit <dir>" come
// in any order, each at most once: --routes <list> (a comma-separated list of
// vanilla, lowpoly, hires; at least one, no repeats; default all three),
// --vcol <psx|gltf|off> (default psx), --classic <glb> (the file must exist).
// Anything else is a wrong call (exit 2).
//
// Layout it writes (K4): <dir>\NOTICE.txt, a byte copy of CREATOR_KIT_NOTICE.txt
// from the folder of the script (missing: exit 4), and for each chosen way W
// (in the order vanilla, lowpoly, hires) a folder <dir>\W\ with
// creator-kit-W.blend, creator-kit-W-figure.glb, NOTICE.txt and
// creator-kit-report.json; hires also gets creator-kit-demo-local.glb.
// The environment says what it does:
//
//   RS_KIT_STANDIN_MODE       unset or ok | noversion | fail4 | hang
//   RS_KIT_STANDIN_VERSION    the first line of --version (default "Blender 5.2.2 LTS")
//   RS_KIT_STANDIN_DEMO       a file copied to hires\creator-kit-demo-local.glb
//   RS_KIT_STANDIN_LOG        every call appends its arguments here, one line
//   RS_KIT_STANDIN_ENVFILE    a Blender call writes TEMP, TMP and BLENDER_USER_*
//
// All output files are UTF-8 without a BOM, with "\n" line ends. Standard
// output goes through WriteFile at once: the Studio reads it from a pipe.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static void Utf8(const wchar_t *w, char *out, int cap)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL);
    if (n <= 0) out[0] = 0;
}

static void Say(const char *line)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD done = 0;
    WriteFile(h, line, (DWORD)strlen(line), &done, NULL);
    WriteFile(h, "\n", 1, &done, NULL);
    FlushFileBuffers(h);
}

static int Env(const wchar_t *name, wchar_t *out, int cap)
{
    DWORD n = GetEnvironmentVariableW(name, out, (DWORD)cap);
    if (n == 0 || n >= (DWORD)cap) { out[0] = 0; return 0; }
    return 1;
}

static int EnvIs(const wchar_t *name, const wchar_t *value)
{
    wchar_t buf[256];
    return Env(name, buf, 256) && wcscmp(buf, value) == 0;
}

static void MakeDirs(const wchar_t *dir)
{
    wchar_t path[1024];
    size_t i;
    wcsncpy(path, dir, 1023);
    path[1023] = 0;
    for (i = 1; path[i]; i++) {
        if ((path[i] == L'\\' || path[i] == L'/') && path[i - 1] != L':') {
            wchar_t c = path[i];
            path[i] = 0;
            CreateDirectoryW(path, NULL);
            path[i] = c;
        }
    }
    CreateDirectoryW(path, NULL);
}

// Writes bytes into <dir>\<name>.
static int WriteFileIn(const wchar_t *dir, const wchar_t *name, const void *data, size_t size)
{
    wchar_t path[1200];
    FILE *f;
    swprintf(path, 1200, L"%ls\\%ls", dir, name);
    f = _wfopen(path, L"wb");
    if (!f) return 0;
    fwrite(data, 1, size, f);
    fclose(f);
    return 1;
}

// A made-up glTF: "glTF", 12 made-up bytes, the text.
static int WriteGlb(const wchar_t *dir, const wchar_t *name, const char *text)
{
    char buf[128];
    size_t n;
    memcpy(buf, "glTF", 4);
    memcpy(buf + 4, "\x02\x00\x00\x00\x40\x00\x00\x00\x00\x00\x00\x00", 12);
    n = 16;
    memcpy(buf + n, text, strlen(text));
    n += strlen(text);
    return WriteFileIn(dir, name, buf, n);
}

static void LogCall(int argc, wchar_t **argv)
{
    wchar_t path[1024];
    FILE *f;
    int i;
    char u[2048];
    if (!Env(L"RS_KIT_STANDIN_LOG", path, 1024)) return;
    f = _wfopen(path, L"ab");
    if (!f) return;
    for (i = 1; i < argc; i++) {
        Utf8(argv[i], u, sizeof u);
        fputs(u, f);
        if (i + 1 < argc) fputs(" | ", f);
    }
    fputs("\n", f);
    fclose(f);
}

static void WriteEnvFile(void)
{
    static const wchar_t *names[] = { L"TEMP", L"TMP", L"BLENDER_USER_RESOURCES", L"BLENDER_USER_CONFIG",
                                      L"BLENDER_USER_SCRIPTS", L"BLENDER_USER_DATAFILES", L"BLENDER_USER_EXTENSIONS" };
    wchar_t path[1024], val[2048];
    char n8[64], v8[4096];
    FILE *f;
    size_t i;
    if (!Env(L"RS_KIT_STANDIN_ENVFILE", path, 1024)) return;
    f = _wfopen(path, L"wb");
    if (!f) return;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        Env(names[i], val, 2048);
        Utf8(names[i], n8, sizeof n8);
        Utf8(val, v8, sizeof v8);
        fprintf(f, "%s=%s\n", n8, v8);
    }
    fclose(f);
}

static int Version(void)
{
    wchar_t v[256];
    char u[600];
    if (EnvIs(L"RS_KIT_STANDIN_MODE", L"noversion")) return 1;
    if (!Env(L"RS_KIT_STANDIN_VERSION", v, 256)) wcscpy(v, L"Blender 5.2.2 LTS");
    Utf8(v, u, sizeof u);
    Say(u);
    Say("\tbuild date: test");
    return 0;
}

// Reads a whole file into a malloc buffer; NULL when it cannot be read.
static unsigned char *ReadAll(const wchar_t *path, size_t *size)
{
    FILE *f = _wfopen(path, L"rb");
    long n;
    unsigned char *buf;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = (unsigned char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

// Makes <dir>\<way>\ with the four files of one way (and the demo for hires).
// Returns 0 or the exit code.
static int WriteWay(const wchar_t *dir, const wchar_t *way, const wchar_t *vcol,
                    const unsigned char *notice, size_t noticeSize)
{
    wchar_t sub[1200], name[256], demo[1024], dst[1300];
    char w8[64], v8[64], text[300], line[600];
    Utf8(way, w8, sizeof w8);
    Utf8(vcol, v8, sizeof v8);
    swprintf(sub, 1200, L"%ls\\%ls", dir, way);
    MakeDirs(sub);
    swprintf(name, 256, L"creator-kit-%ls.blend", way);
    snprintf(text, sizeof text, "BLENDER-v502 made-up stand-in file %s\n", w8);
    WriteFileIn(sub, name, text, strlen(text));
    swprintf(name, 256, L"creator-kit-%ls-figure.glb", way);
    snprintf(text, sizeof text, "stand-in figure %s", w8);
    WriteGlb(sub, name, text);
    WriteFileIn(sub, L"NOTICE.txt", notice, noticeSize);
    snprintf(line, sizeof line, "{\"blender\": \"5.2.2 stand-in\", \"route\": \"%s\", \"vcol\": \"%s\", \"messages\": []}\n", w8, v8);
    WriteFileIn(sub, L"creator-kit-report.json", line, strlen(line));
    if (wcscmp(way, L"hires") == 0) {
        if (Env(L"RS_KIT_STANDIN_DEMO", demo, 1024)) {
            swprintf(dst, 1300, L"%ls\\creator-kit-demo-local.glb", sub);
            if (!CopyFileW(demo, dst, FALSE)) {
                Say("creator kit: the demo model could not be copied (stand-in)");
                return 3;
            }
        } else {
            WriteGlb(sub, L"creator-kit-demo-local.glb", "stand-in demo");
        }
    }
    return 0;
}

static int Blender(int argc, wchar_t **argv)
{
    static const wchar_t *fixed[] = { L"--threads", L"1", L"--background", L"--factory-startup",
                                      L"--python-exit-code", L"1", L"--python", NULL, L"--", L"--kit" };
    static const wchar_t *ways[3] = { L"vanilla", L"lowpoly", L"hires" };
    int i, k, rc, chosen[3] = { 0, 0, 0 };
    int haveRoutes = 0, haveVcol = 0, haveClassic = 0;
    const wchar_t *script, *dir, *vcol = L"psx", *classic = NULL;
    wchar_t notice[1200], list[256], *p, *next;
    char u[2048], line[2300], joined[64];
    unsigned char *nbuf;
    size_t nsize = 0, slash;
    DWORD attr;
    if (argc < 12) goto wrong;
    for (i = 0; i < 10; i++) {
        if (fixed[i] && wcscmp(argv[i + 1], fixed[i]) != 0) goto wrong;
    }
    script = argv[8];
    dir = argv[11];
    // Options after "--kit <dir>", in any order, each at most once.
    for (i = 12; i < argc; i += 2) {
        if (i + 1 >= argc) goto wrong;
        if (wcscmp(argv[i], L"--vcol") == 0 && !haveVcol) {
            haveVcol = 1;
            vcol = argv[i + 1];
            if (wcscmp(vcol, L"psx") && wcscmp(vcol, L"gltf") && wcscmp(vcol, L"off")) goto wrong;
        } else if (wcscmp(argv[i], L"--classic") == 0 && !haveClassic) {
            haveClassic = 1;
            classic = argv[i + 1];
        } else if (wcscmp(argv[i], L"--routes") == 0 && !haveRoutes) {
            haveRoutes = 1;
            if (wcslen(argv[i + 1]) >= 256) goto wrong;
            wcscpy(list, argv[i + 1]);
            for (p = list; ; p = next + 1) {
                next = wcschr(p, L',');
                if (next) *next = 0;
                for (k = 0; k < 3; k++) if (wcscmp(p, ways[k]) == 0) break;
                if (k == 3 || chosen[k]) goto wrong;
                chosen[k] = 1;
                if (!next) break;
            }
        } else {
            goto wrong;
        }
    }
    if (!haveRoutes) chosen[0] = chosen[1] = chosen[2] = 1;
    if (classic) {
        attr = GetFileAttributesW(classic);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            Say("creator kit: --classic file missing (stand-in)");
            return 4;
        }
    }
    attr = GetFileAttributesW(script);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        Say("creator kit: script missing (stand-in)");
        return 2;
    }
    WriteEnvFile();
    if (EnvIs(L"RS_KIT_STANDIN_MODE", L"fail4")) {
        Utf8(dir, u, sizeof u);
        snprintf(line, sizeof line, "creator kit: kit-info.json is missing in %s. How to fix: generate the kit files again.", u);
        Say(line);
        return 4;
    }
    if (EnvIs(L"RS_KIT_STANDIN_MODE", L"hang")) {
        Say("creator kit: working (stand-in)");
        Sleep(120000);
        return 0;
    }
    // The notice lies beside the script.
    slash = wcslen(script);
    while (slash > 0 && script[slash - 1] != L'\\' && script[slash - 1] != L'/') slash--;
    if (slash == 0) swprintf(notice, 1200, L"CREATOR_KIT_NOTICE.txt");
    else swprintf(notice, 1200, L"%.*ls%ls", (int)slash, script, L"CREATOR_KIT_NOTICE.txt");
    nbuf = ReadAll(notice, &nsize);
    if (!nbuf) {
        Say("creator kit: CREATOR_KIT_NOTICE.txt is missing beside the script (stand-in)");
        return 4;
    }
    MakeDirs(dir);
    WriteFileIn(dir, L"NOTICE.txt", nbuf, nsize);
    joined[0] = 0;
    for (k = 0; k < 3; k++) {
        if (!chosen[k]) continue;
        rc = WriteWay(dir, ways[k], vcol, nbuf, nsize);
        if (rc) { free(nbuf); return rc; }
        Utf8(ways[k], u, sizeof u);
        if (joined[0]) strcat(joined, ",");
        strcat(joined, u);
    }
    free(nbuf);
    Utf8(vcol, u, sizeof u);
    Say("@progress\tblender\t1\t1");
    snprintf(line, sizeof line, "creator kit: ok routes=%s vcol=%s source=stand-in (stand-in)", joined, u);
    Say(line);
    return 0;
wrong:
    Say("creator kit: wrong call (stand-in)");
    return 2;
}

int wmain(int argc, wchar_t **argv)
{
    int i;
    LogCall(argc, argv);
    for (i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--version") == 0) return Version();
    }
    return Blender(argc, argv);
}
