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
#include "rs_tex.h"

#include <stdio.h>
#include <math.h>
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

static int Near(double a, double b)
{
    return fabs(a - b) < 1e-5;
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
    // The values themselves are include/rldhires.inc's (measured); only that the page gets them.
    Check(c->budgetTriangles >= c->triangles && c->budgetEdge >= c->textureEdge && c->budgetTextureBytes >= c->textureBytes && c->budgetTriangles > 0u,
          line);
    snprintf(line, sizeof(line), "%s: every clip present", what);
    {
        int i, all = 1;
        for (i = 0; i < RS_HIRES_CLIPS; i++)
            all &= c->clipState[i] == RS_HIRES_CLIP_PRESENT && c->clipVia[i] == i && c->clipSeconds[i] == 1.0f;
        Check(all, line);
    }
    snprintf(line, sizeof(line), "%s: the picture is RLDPV2 (5 triangles, the framing box, an empty pose) and the textured RLDPN2 after it", what);
    Check(c->preview != NULL && c->previewTriangles == 5 && c->previewBytes > 234u && c->previewTextured == 1 && c->previewKeepAt == 234u &&
              memcmp(c->preview, "RLDPV2\0\0", 8) == 0 && ReadU32(c->preview + 8) == 3u && ReadU32(c->preview + 12) == 5u &&
              ReadU32(c->preview + 16 + 150) == 2u && ReadU32(c->preview + 20 + 150 + 60) == 0u && memcmp(c->preview + 234, "RLDPN2\0\0", 8) == 0,
          line);
    if (c->preview && c->previewBytes > 234u) {
        // The box: the bind bounds (-0.5, 0, -0.5) .. (1, 1, 0.75) m and 0.25 m around them.
        const unsigned char *b = c->preview + 20 + 150;
        snprintf(line, sizeof(line), "%s: the framing box is the bounds and 0.25 m", what);
        Check(ReadS16(b) == -768 && ReadS16(b + 2) == -256 && ReadS16(b + 4) == -768 && ReadS16(b + 30) == 1280 &&
                  ReadS16(b + 32) == 1280 && ReadS16(b + 34) == 1024,
              line);
    }
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

    // THE CLIPS on the same model: the rest pose through the skeleton is the
    // bind pose picture byte for byte; the wheels turn about their axle.
    Path(path, argv[1], L"hires-good.glb");
    RsHires_CheckFile(path, &c);
    {
        const unsigned char *frame = NULL;
        size_t size = 0;
        float p[3], n[3];
        Check(c.animReady == 1 && c.anim != NULL, "hires-good.glb: the clips can play");
        Check(RsHires_AnimFrame(&c, -1, 0.0f, 0.0f, &frame, &size) == 5 && frame != NULL && size == c.previewBytes &&
                  memcmp(frame, c.preview, size) == 0,
              "the rest pose through rldhires_anim.inc is the bind pose picture, byte for byte");
        Check(RsHires_AnimFrame(&c, 0, 0.5f, 0.0f, &frame, &size) == 5 && memcmp(frame, c.preview, size) == 0,
              "idle of hires-good.glb turns nothing (its keys are the identity)");
        // wheel_fl (corners 3..5), its corner (0, 0.25, 0) at its node (0.5, 0.25, 0.5):
        // a quarter turn forward brings it to (0, 0, 0.25) -> (0.5, 0.25, 0.75).
        RsHires_AnimFrame(&c, -1, 0.0f, 90.0f, &frame, &size);
        Check(RsHires_AnimCorner(&c, 4, p, n) && Near(p[0], 0.5) && Near(p[1], 0.25) && Near(p[2], 0.75),
              "a wheel turned 90 degrees rolls forward about its axle");
        Check(RsHires_AnimCorner(&c, 15, p, n) == 0, "no corner past the model");
    }
    RsHires_CheckFree(&c);
    Check(c.anim == NULL && c.animReady == 0, "RsHires_CheckFree drops the clips");

    // THE ENGINE'S CASE (platform/engine_hires_selftest.c, "gltf chain skinned
    // vertex 3 at t=1"): joint_1 at (0, 0.5, 0) turned 10 degrees about Z, the
    // vertex (0.1, 0.5, 0) half on each joint: (0.5 (0.1 + 0.1 cos 10),
    // 0.5 (0.5 + 0.5 + 0.1 sin 10), 0), its normal (0, 0, 1) unchanged.
    Path(path, argv[1], L"hires-anim.glb");
    Check(RsHires_CheckFile(path, &c) == 1 && c.usable == 1 && c.animReady == 1, "hires-anim.glb: usable, the clips can play");
    {
        const double d10 = 10.0 * 3.14159265358979 / 180.0, d5 = 5.0 * 3.14159265358979 / 180.0;
        float p[3], n[3];
        RsHires_AnimFrame(&c, 3, 1.0f, 0.0f, NULL, NULL);   // jump: once, at its end
        Check(RsHires_AnimCorner(&c, 1, p, n) && Near(p[0], 0.5 * (0.1 + 0.1 * cos(d10))) && Near(p[1], 0.5 * (1.0 + 0.1 * sin(d10))) &&
                  Near(p[2], 0.0) && Near(n[0], 0.0) && Near(n[1], 0.0) && Near(n[2], 1.0),
              "jump at 1 s: the skinned vertex of the engine's selftest");
        RsHires_AnimFrame(&c, 6, 1.5f, 0.0f, NULL, NULL);   // hit: once, held a second after its end
        Check(RsHires_AnimCorner(&c, 1, p, n) && Near(p[0], 0.5 * (0.1 + 0.1 * cos(d10))), "hit held at its end");
        RsHires_AnimFrame(&c, 0, 0.5f, 0.0f, NULL, NULL);   // idle: looped, half way
        Check(RsHires_AnimCorner(&c, 1, p, n) && Near(p[0], 0.5 * (0.1 + 0.1 * cos(d5))) && Near(p[1], 0.5 * (1.0 + 0.1 * sin(d5))),
              "idle at 0.5 s: 5 degrees");
        RsHires_AnimFrame(&c, -1, 0.5f, 0.0f, NULL, NULL);  // the rest pose
        Check(RsHires_AnimCorner(&c, 1, p, n) && Near(p[0], 0.1) && Near(p[1], 0.5), "the rest pose keeps the vertex");
        // The third vertex (0, 1, 0) has one weight of 0.5: renormalised to 1 as the engine does
        // (EngHc_ReadPrimitive), it stays at (0, 1, 0); unrenormalised it would stand at (0, 0.5, 0).
        RsHires_AnimFrame(&c, 3, 1.0f, 0.0f, NULL, NULL);
        Check(RsHires_AnimCorner(&c, 2, p, n) && Near(p[0], 0.0) && Near(p[1], 1.0) && Near(p[2], 0.0),
              "weights renormalised as the engine does: a lone weight of 0.5 moves the vertex fully");
        Check(RsHires_AnimLength(&c, 0) == 1.0f && RsHires_AnimLength(&c, -1) == 0.0f, "clip lengths");
        {
            float share = 0.0f;
            Check(Near(RsHires_AnimLocal(&c, 0, 1.25f, &share), 0.25) && share == -1.0f, "idle at 1.25 s stands at 0.25 s (looped)");
            Check(Near(RsHires_AnimLocal(&c, 3, 1.5f, &share), 1.0) && share == -1.0f, "jump at 1.5 s holds its end");
            Check(Near(RsHires_AnimLocal(&c, 3, 2.25f, &share), 0.25), "jump starts again after a held second");
            Check(Near(RsHires_AnimLocal(&c, 1, 1.5f, &share), 0.5) && Near(share, 0.5), "steer_left at 1.5 s: share 0.5, half its length");
        }
    }
    RsHires_CheckFree(&c);

    // THE TEXTURED PREVIEW (hires-tex.glb: a 16 x 16 texture, red with a green
    // middle, on the body triangle with UVs (0,0), (1,0), (0,1); COLOR_0 at
    // sRGB 128). The flat picture can only colour the corners - all three red;
    // the textured model the view draws shows the green middle, as the engine
    // does (the texel x the vertex colour x the factor, all on the display curve).
    Path(path, argv[1], L"hires-tex.glb");
    Check(RsHires_CheckFile(path, &c) == 1 && c.usable == 1 && c.previewTextured == 1, "hires-tex.glb: a textured preview");
    if (c.preview && c.previewTextured) {
        struct RsTexNative nat;
        wchar_t why[256];
        const unsigned char *flat = c.preview + 16;
        Check(flat[6] == 100 && flat[7] == 0 && flat[16] == 100 && flat[17] == 0 && flat[26] == 100 && flat[27] == 0,
              "flat picture: the three corners of the body are red (texel 255 x C 100 = 100), no green");
        Check(RsTex_ParseNative(c.preview, c.previewBytes, c.previewKeepAt, &nat, why, 256) == 1, "the view reads the textured model");
        if (nat.triangles == 5) {
            const struct RsTexImage *img = RsTex_NativeImage(&nat, RsTex_NativeMaterialOf(&nat, 0));
            struct RsTexCorner k0, k1, k2;
            const unsigned char *mid;
            const unsigned char *corner;
            RsTex_NativeCorner(&nat, 0, 0, &k0);
            RsTex_NativeCorner(&nat, 0, 1, &k1);
            RsTex_NativeCorner(&nat, 0, 2, &k2);
            Check(nat.textureCount == 1 && img != NULL && img->w == 16 && img->h == 16, "textured model: the body's material samples the 16 x 16 texture");
            Check(nat.mat[RsTex_NativeMaterialOf(&nat, 0)].filtered == 1 && nat.mat[RsTex_NativeMaterialOf(&nat, 0)].nearest == 0,
                  "textured model: the material asks for the engine's hi-res filter (trilinear, anisotropic), not nearest");
            Check(k0.r == 100 && k0.g == 100 && k0.b == 100 && k0.a == 255,
                  "COLOR_0 as the engine applies it: linear 0.2159 -> byte 55 -> display curve 128, lit 0.7786: C = 100");
            if (img) {
                mid = RsTex_Texel(img, 0, (k0.u + k1.u + k2.u) / 3, (k0.v + k1.v + k2.v) / 3);
                corner = RsTex_Texel(img, 0, k0.u, k0.v);
                Check(mid[0] == 0 && mid[1] == 255 && corner[0] == 255 && corner[1] == 0,
                      "textured model: the middle of the triangle shows the green texel, its corner the red one");
            }
        }
        RsTex_FreeNative(&nat);
    }
    RsHires_CheckFree(&c);

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

    // The clip hull (include/rldhires_hull.inc) in the Studio's check: clips that
    // take the model to 4.05 m are refused as rldpack refuses them.
    Path(path, argv[1], L"hires-hull.glb");
    Check(RsHires_CheckFile(path, &c) == 1 && c.usable == 0 && c.errors == 1 && c.rowCount == 1 && wcscmp(c.rows[0].rule, L"HRG-18") == 0 &&
              wcsstr(c.rows[0].text, L"idle") != NULL,
          "hires-hull.glb: the Studio refuses the clip hull of 4.05 m (HRG-18, clip 'idle')");
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
    {
        const wchar_t *up[RS_HIRES_UPSCALE_ARGS];
        wchar_t derived[64];
        Check(RsHires_UpscaleArgs(L"model.glb", L"model-upscaled.glb", up) == 5 && wcscmp(up[0], L"hires-upscale") == 0 &&
                  wcscmp(up[1], L"model.glb") == 0 && wcscmp(up[2], L"-o") == 0 && wcscmp(up[3], L"model-upscaled.glb") == 0 &&
                  wcscmp(up[4], L"--machine") == 0,
              "the upscale step is rldpack hires-upscale <model> -o <derived> --machine");
        Check(RsHires_UpscaledPath(L"C:\\a\\b\\Kart.GLB", L"D:\\out\\x.rldchar", derived, 64) == 1 &&
                  wcscmp(derived, L"D:\\out\\Kart-upscaled.glb") == 0,
              "the upscaled model: <folder of the output>\\<model name>-upscaled.glb");
        Check(RsHires_UpscaledPath(L"m.glb", L"x.rldchar", derived, 64) == 1 && wcscmp(derived, L"m-upscaled.glb") == 0 &&
                  RsHires_UpscaledPath(L"a/model.glb", L"o/x.rldchar", derived, 64) == 1 && wcscmp(derived, L"o/model-upscaled.glb") == 0 &&
                  RsHires_UpscaledPath(L"m.glb", L"x.rldchar", derived, 8) == 0 && derived[0] == 0,
              "the upscaled model without folders, with forward slashes, and refused when it does not fit");
    }
    Path(path, argv[1], L"hires-tex-upscaled.glb");
    Check(RsHires_CheckFile(path, &c) == 1 && c.usable == 1 && c.previewTextured == 1 && c.textures == 1 && c.textureEdge == 64u &&
              c.errors == 0 && c.warnings == 0,
          "hires-tex-upscaled.glb (the Studio's upscale step): usable, no error or warning, its texture 64 x 64, textured preview");
    RsHires_CheckFree(&c);

    Check(wcscmp(RsHires_ClipName(0), L"idle") == 0 && wcscmp(RsHires_ClipName(11), L"menu") == 0 && wcscmp(RsHires_ClipName(12), L"?") == 0,
          "clip names");

    printf("rs_hires selftest %s: %d checks, %d failures\n", s_failures ? "FAILED" : "passed", s_checks, s_failures);
    return s_failures != 0;
}
