// rs_kit_standin.c - a stand-in for blender.exe and for the game-data
// extractor in the test of the Creator Kit page of Reload Studio
//
// Built only with the tests (CMakeLists.txt, ctest reloadstudio_kit). It holds
// no game data and no Blender: every file it writes is made up here. The
// Studio is pointed at it twice - as the extractor (kit.extract, called as
// "--game <folder> -o <dir>") and as Blender (kit.blender, called as
// "--background --factory-startup --python-exit-code 1 --python <script> --
// --kit <dir> [--vcol <psx|gltf|off>]", or "--version"). The environment says
// what it does:
//
//   RS_KIT_STANDIN_MODE       unset or ok | noversion | extract-fail | fail4 | hang
//   RS_KIT_STANDIN_VERSION    the first line of --version (default "Blender 5.2.2 LTS")
//   RS_KIT_STANDIN_DEMO       a file copied to creator-kit-demo.glb
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

static int Extract(const wchar_t *dir)
{
    if (EnvIs(L"RS_KIT_STANDIN_MODE", L"extract-fail")) {
        Say("@msg\terror\tkit-data\tThe game data could not be read.\tstand-in");
        return 1;
    }
    MakeDirs(dir);
    Say("@progress\tkit\t1\t2");
    WriteGlb(dir, L"kit-kart.glb", "stand-in kart");
    WriteGlb(dir, L"kit-wheel.glb", "stand-in wheel");
    WriteFileIn(dir, L"kit-info.json", "{\"kit\": 1, \"source\": \"stand-in\"}\n", 33);
    Say("@progress\tkit\t2\t2");
    return 0;
}

static int Blender(int argc, wchar_t **argv)
{
    static const wchar_t *fixed[] = { L"--background", L"--factory-startup", L"--python-exit-code", L"1",
                                      L"--python", NULL, L"--", L"--kit" };
    int i;
    const wchar_t *script, *dir, *vcol = L"psx";
    wchar_t demo[1024], dst[1200];
    char u[2048], line[2300];
    DWORD attr;
    if (argc != 10 && argc != 12) goto wrong;
    for (i = 0; i < 8; i++) {
        if (fixed[i] && wcscmp(argv[i + 1], fixed[i]) != 0) goto wrong;
    }
    script = argv[6];
    dir = argv[9];
    if (argc == 12) {
        if (wcscmp(argv[10], L"--vcol") != 0) goto wrong;
        vcol = argv[11];
        if (wcscmp(vcol, L"psx") && wcscmp(vcol, L"gltf") && wcscmp(vcol, L"off")) goto wrong;
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
    MakeDirs(dir);
    WriteFileIn(dir, L"creator-kit.blend", "BLENDER-v502 made-up stand-in file\n", 35);
    if (Env(L"RS_KIT_STANDIN_DEMO", demo, 1024)) {
        swprintf(dst, 1200, L"%ls\\creator-kit-demo.glb", dir);
        if (!CopyFileW(demo, dst, FALSE)) {
            Say("creator kit: the demo model could not be copied (stand-in)");
            return 3;
        }
    } else {
        WriteGlb(dir, L"creator-kit-demo.glb", "stand-in demo");
    }
    Utf8(vcol, u, sizeof u);
    snprintf(line, sizeof line, "{\"blender\": \"5.2.2 stand-in\", \"vcol\": \"%s\", \"messages\": []}\n", u);
    WriteFileIn(dir, L"creator-kit-report.json", line, strlen(line));
    Say("@progress\tblender\t1\t1");
    snprintf(line, sizeof line, "creator kit: ok vcol=%s source=stand-in (stand-in)", u);
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
    if (argc == 5 && wcscmp(argv[1], L"--game") == 0 && wcscmp(argv[3], L"-o") == 0) return Extract(argv[4]);
    return Blender(argc, argv);
}
