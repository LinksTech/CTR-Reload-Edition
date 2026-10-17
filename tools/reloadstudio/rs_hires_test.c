// rs_hires_test.c - the self-test of rs_hires_model.c (ctest reloadstudio_hires_selftest)
//
// A console program of its own, like rs_tex_selftest. Its folder holds what
// cmake/run_rs_hires_selftest.cmake made with rldpack: the self-test's models
// hires-good.glb, hires-warn.glb, hires-bad.glb (rldpack hires-test-files) and
// packer.rldchar (rldpack hires-add of hires-good.glb). It checks that the
// page's list and summary are rldpack's findings, that the picture of the bind
// pose is the model (its triangles, its corners in file units, a lit colour),
// that a character's CHRG reads as its .glb, and the arguments of the pack
// step (the byte identity of the packed file itself is the script's part).

#include "reloadstudio.h"
#include "rs_hires_model.h"

#include <stdio.h>
#include <string.h>

// cgltf's implementation, which Reload Studio has from rs_rldpack.c.
#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

static int s_checks;
static int s_failures;

static void Check(int ok, const char *what)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("  FAIL rs_hires: %s\n", what);
    }
}

static unsigned ReadU32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static short ReadS16(const unsigned char *p)
{
    return (short)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static void Path(wchar_t *out, const wchar_t *dir, const wchar_t *name)
{
    swprintf(out, 1024, L"%ls\\%ls", dir, name);
}

// The good model: no finding, every count, and its picture.
static void CheckGood(const struct RsHiresCheck *c, const char *what)
{
    char line[160];
    snprintf(line, sizeof(line), "%s: read, usable, no finding", what);
    Check(c->read == 1 && c->usable == 1 && c->rowCount == 0 && c->errors == 0 && c->warnings == 0 && c->notes == 0, line);
    snprintf(line, sizeof(line), "%s: clips 12, wheels 4, 2 joints, 5 triangles, 1 texture of 1024 bytes", what);
    Check(c->clips == 12 && c->wheels == 4 && c->joints == 2 && c->triangles == 5u && c->textures == 1 && c->textureBytes == 1024u &&
              c->textureEdge == 16u,
          line);
    snprintf(line, sizeof(line), "%s: the budget of include/rldhires.inc", what);
    Check(c->budgetTriangles > 0u && c->budgetEdge == 2048u && c->budgetTextureBytes == 64u * 1024u * 1024u, line);
    snprintf(line, sizeof(line), "%s: every clip present", what);
    {
        int i, all = 1;
        for (i = 0; i < RS_HIRES_CLIPS; i++)
            all &= c->clipState[i] == RS_HIRES_CLIP_PRESENT && c->clipVia[i] == i && c->clipSeconds[i] == 1.0f;
        Check(all, line);
    }
    snprintf(line, sizeof(line), "%s: the picture is RLDPV2 with 5 triangles in pose 0, poses 1 and 2 empty", what);
    Check(c->preview != NULL && c->previewTriangles == 5 && c->previewBytes == 16u + 5u * 30u + 8u &&
              memcmp(c->preview, "RLDPV2\0\0", 8) == 0 && ReadU32(c->preview + 8) == 3u && ReadU32(c->preview + 12) == 5u &&
              ReadU32(c->preview + 16 + 150) == 0u && ReadU32(c->preview + 20 + 150) == 0u,
          line);
    if (c->preview && c->previewBytes >= 16u + 30u) {
        const unsigned char *t = c->preview + 16;
        // The body triangle (0,0,0), (1,0,0), (0,1,0) m of the self-test model:
        // 1 m = 1024 file units. Its normals +Z, the light (0.4, 0.8, 0.6)
        // normalised: 0.5 + 0.5 x 0.5571 = 0.7786 of white (the test PNG has
        // no pixels to decode, so the base colour factor 1 alone) = 199.
        snprintf(line, sizeof(line), "%s: the body triangle in file units (1 m = 1024)", what);
        Check(ReadS16(t) == 0 && ReadS16(t + 2) == 0 && ReadS16(t + 4) == 0 && ReadS16(t + 10) == 1024 && ReadS16(t + 12) == 0 &&
                  ReadS16(t + 20) == 0 && ReadS16(t + 22) == 1024,
              line);
        snprintf(line, sizeof(line), "%s: the corner colour is lit white (199) and one-sided", what);
        Check(t[6] == 199 && t[7] == 199 && t[8] == 199 && t[9] == 0, line);
        // A wheel (node 3, wheel_fl at (0.5, 0.25, 0.5) m): its first corner.
        snprintf(line, sizeof(line), "%s: wheel_fl stands at its node (512, 256, 512)", what);
        Check(ReadS16(t + 30) == 512 && ReadS16(t + 32) == 256 && ReadS16(t + 34) == 512, line);
    }
}

int wmain(int argc, wchar_t **argv)
{
    struct RsHiresCheck c;
    wchar_t path[1024];
    const wchar_t *args[RS_HIRES_PACK_ARGS];

    if (argc != 2) {
        printf("usage: rs_hires_selftest <folder with hires-good.glb, hires-warn.glb, hires-bad.glb, packer.rldchar>\n");
        return 2;
    }

    Path(path, argv[1], L"hires-good.glb");
    Check(RsHires_CheckFile(path, &c) == 1, "hires-good.glb is read");
    CheckGood(&c, "hires-good.glb");
    Check(c.fromCharacter == 0, "hires-good.glb: not from a character");
    RsHires_CheckFree(&c);
    Check(c.preview == NULL, "RsHires_CheckFree drops the picture");

    Path(path, argv[1], L"packer.rldchar");
    Check(RsHires_CheckFile(path, &c) == 1, "the CHRG of packer.rldchar is read");
    CheckGood(&c, "packer.rldchar");
    Check(c.fromCharacter == 1, "packer.rldchar: marked as read from a character");
    RsHires_CheckFree(&c);

    Path(path, argv[1], L"hires-warn.glb");
    Check(RsHires_CheckFile(path, &c) == 1, "hires-warn.glb is read");
    Check(c.usable == 1 && c.errors == 0 && c.warnings == 2 && c.rowCount == 2, "hires-warn.glb: usable, 2 warnings");
    Check(c.rows[0].severity == RS_SEV_WARNING && wcscmp(c.rows[0].rule, L"HRG-20") == 0 &&
              wcscmp(c.rows[0].text, L"no wheel nodes (wheel_fl, wheel_fr, wheel_rl, wheel_rr) - standard wheels are used") == 0,
          "hires-warn.glb: the wheel warning, word for word as rldpack hires-check prints it");
    Check(wcscmp(c.rows[1].rule, L"HRG-19") == 0 && wcscmp(c.rows[1].text, L"clip 'hit' is missing - the kart uses 'idle' instead") == 0,
          "hires-warn.glb: the clip warning");
    Check(c.wheels == 0 && c.clips == 11 && c.clipState[6] == RS_HIRES_CLIP_FALLBACK && c.clipVia[6] == 0 && c.clipFullLock[6] == 0,
          "hires-warn.glb: no wheels, 'hit' falls back to 'idle'");
    Check(c.previewTriangles == 1, "hires-warn.glb: the picture has the body only (the found wheels are not drawn)");
    RsHires_CheckFree(&c);

    Path(path, argv[1], L"hires-bad.glb");
    Check(RsHires_CheckFile(path, &c) == 1, "hires-bad.glb is read");
    Check(c.usable == 0 && c.errors == 1 && c.rowCount == 1 && c.rows[0].severity == RS_SEV_ERROR && wcscmp(c.rows[0].rule, L"HRG-16") == 0 &&
              wcsstr(c.rows[0].text, L"JPEG") != NULL,
          "hires-bad.glb: refused, HRG-16 names the JPEG");
    RsHires_CheckFree(&c);

    Path(path, argv[1], L"no-such-file.glb");
    Check(RsHires_CheckFile(path, &c) == 0 && c.read == 0 && c.why[0] != 0 && c.preview == NULL, "a missing file says why");
    RsHires_CheckFree(&c);

    Check(RsHires_PackArgs(L"in.rldchar", L"model.glb", L"out.rldchar", args) == 6 && wcscmp(args[0], L"hires-add") == 0 &&
              wcscmp(args[1], L"in.rldchar") == 0 && wcscmp(args[2], L"model.glb") == 0 && wcscmp(args[3], L"-o") == 0 &&
              wcscmp(args[4], L"out.rldchar") == 0 && wcscmp(args[5], L"--machine") == 0,
          "the pack step is rldpack hires-add <character> <model> -o <out> --machine");
    Check(wcscmp(RsHires_ClipName(0), L"idle") == 0 && wcscmp(RsHires_ClipName(11), L"menu") == 0 && wcscmp(RsHires_ClipName(12), L"?") == 0,
          "clip names");

    printf("rs_hires selftest %s: %d checks, %d failures\n", s_failures ? "FAILED" : "passed", s_checks, s_failures);
    return s_failures != 0;
}
