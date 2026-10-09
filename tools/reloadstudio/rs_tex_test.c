// rs_tex_test.c - the self-test of rs_tex.c (ctest reloadstudio_tex_selftest)
//
// The writer and the reader of the preview's native model belong together:
// this program builds the self-test's mini OBJ with rldpack's own make-char
// code (tools/rldpack.c is included as rs_rldpack.c includes it, its main
// renamed), writes RLDPN2 and RLDPW3 with it and reads them back with
// rs_tex.c. Then:
//   - the levels rs_tex builds are the game's: the fixed image of the game's
//     --native-tex-selftest gives its golden SHA-256, and every level of every
//     texture equals RldMip_BuildLevels (include/rldmip.inc, the code
//     platform/native_tex.c uploads);
//   - corners, colours, materials and positions come back as written;
//   - the colour formula of rs_tex against the "nr" program computed in
//     float32 as the GPU does, for every byte of texel, corner and material
//     colour: never more than 1 apart (counted), the mask counted too;
//   - texel wrap, the Vulkan level rule and the slopes of a triangle;
//   - refused files: a reason, nothing kept.
// One line on stdout; 0 = passed.

#define main Rldpack_Main
#include "../rldpack.c"
#undef main

#include "rs_tex.h"

#include <math.h>

static int s_rsTexTestFailed;

static void RsTexTest(int ok, const char *name)
{
    if (!ok) {
        printf("  FAIL rs_tex %s\n", name);
        s_rsTexTestFailed++;
    }
}

// The image of the game's NativeTex_TestImage (platform/native_tex.c).
static void RsTexTest_GameImage(u8 *p, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            u8 *q = p + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            q[0] = (u8)((x * 37 + y * 11) & 255);
            q[1] = (u8)((x * x * 3 + y * 29) & 255);
            q[2] = (u8)(((x ^ y) * 23) & 255);
            q[3] = (u8)((((x + y) % 4) == 0) ? 0 : ((x * y * 13 + 40) & 255));
        }
}

// Every level of img against RldMip_BuildLevels of its level 0.
static int RsTexTest_SameChain(const struct RsTexImage *img)
{
    const size_t bytes = RldMip_ChainBytes(img->w, img->h, img->levels);
    u8 *chain = (u8 *)malloc(bytes);
    size_t at = 0;
    int l, same = (chain != NULL) && (img->levels == RldMip_LevelCount(img->w, img->h)) && (img->chainBytes == bytes);
    if (same) {
        RldMip_BuildLevels(img->level[0], img->w, img->h, img->levels, img->linear, chain);
        for (l = 0; l < img->levels && same; l++) {
            const size_t n = (size_t)RldMip_LevelEdge(img->w, l) * (size_t)RldMip_LevelEdge(img->h, l) * 4u;
            same = ((1 << img->shiftW[l]) == RldMip_LevelEdge(img->w, l)) && ((1 << img->shiftH[l]) == RldMip_LevelEdge(img->h, l)) &&
                   (memcmp(chain + at, img->level[l], n) == 0);
            at += n;
        }
    }
    free(chain);
    return same;
}

// The sha256 of an image's whole chain, level after level.
static void RsTexTest_ChainHex(const struct RsTexImage *img, char hex[65])
{
    const size_t bytes = img->chainBytes;
    u8 *all = (u8 *)malloc(bytes);
    u8 hash[32];
    size_t at = 0;
    int l, i;
    hex[0] = 0;
    if (!all)
        return;
    for (l = 0; l < img->levels; l++) {
        const size_t n = (size_t)RldMip_LevelEdge(img->w, l) * (size_t)RldMip_LevelEdge(img->h, l) * 4u;
        memcpy(all + at, img->level[l], n);
        at += n;
    }
    Sha256(all, bytes, hash);
    for (i = 0; i < 32; i++)
        sprintf(hex + 2 * i, "%02x", hash[i]);
    free(all);
}

// A RLDPN2 block by hand: the textures given (w, h, flags, rgba), one
// material per texture (white, opaque) and one triangle. NULL = no memory.
static u8 *RsTexTest_Block(int count, const u32 *w, const u32 *h, const u32 *flags, u8 *const *rgba, size_t *sizeOut)
{
    size_t size = 8 + 4 + 4 + 4 + (size_t)(count > 0 ? count : 1) * 8 + 8 + 3 * 18 + 40;
    u8 *out;
    size_t at;
    int t;
    for (t = 0; t < count; t++)
        size += 12 + (size_t)w[t] * h[t] * 4u;
    out = (u8 *)calloc(1, size);
    if (!out)
        return NULL;
    memcpy(out, "RLDPN2", 6);
    Rld_WriteLE32(&out[12], (u32)count);
    at = 16;
    for (t = 0; t < count; t++) {
        Rld_WriteLE32(&out[at], w[t]);
        Rld_WriteLE32(&out[at + 4], h[t]);
        Rld_WriteLE32(&out[at + 8], flags[t]);
        if (rgba && rgba[t])
            memcpy(&out[at + 12], rgba[t], (size_t)w[t] * h[t] * 4u);
        at += 12 + (size_t)w[t] * h[t] * 4u;
    }
    Rld_WriteLE32(&out[at], (u32)(count > 0 ? count : 1));
    at += 4;
    for (t = 0; t < (count > 0 ? count : 1); t++) {
        memset(&out[at], 255, 4);
        Rld_WriteLE16(&out[at + 4], count > 0 ? (u32)t : 0xFFFFu);
        at += 8;
    }
    Rld_WriteLE32(&out[at], 1);
    Rld_WriteLE32(&out[at + 4], 3);
    *sizeOut = size;
    return out;
}

// The "nr" program in float32 (platform/native_shaders.inc): v_color =
// a_color * nrTint in the vertex stage, colour = v_color * texel, the texel
// of an sRGB texture decoded by the sampler and encoded again by the shader,
// stored round to nearest. T < 0: untextured.
static int RsTexTest_ShaderChannel(int t, int c, int m, int srgb)
{
    const float vc = ((float)c / 255.0f) * ((float)m / 255.0f);
    float col = vc;
    if (t >= 0) {
        float tex = (float)t / 255.0f;
        if (srgb) {
            const double v = (double)t / 255.0;
            const float linear = (float)((v <= 0.04045) ? (v / 12.92) : pow((v + 0.055) / 1.055, 2.4));
            tex = (linear < 0.0031308f) ? (linear * 12.92f) : ((1.055f * powf(linear, 1.0f / 2.4f)) - 0.055f);
        }
        col = vc * tex;
    }
    return (int)floor((double)col * 255.0 + 0.5);
}

static int RsTexTest_ShaderKeeps(int ta, int ca, int ma)
{
    const float a = (((float)ca / 255.0f) * ((float)ma / 255.0f)) * (ta >= 0 ? (float)ta / 255.0f : 1.0f);
    return !(a < 0.5f);
}

int main(void)
{
    struct RldMkNativeObjCase k;
    struct RldMkNativeObjResult a;
    struct RldCharNative native;
    struct RsTexNative rn;
    wchar_t why[256];
    char hexSrgb[65], hexLinear[65];
    long long formulaOff = 0, formulaMax = 0, maskOff = 0, srgbOff = 0;
    int tri, c, i, t;

    s_rldMkQuiet = 1;
    memset(&rn, 0, sizeof(rn));
    hexSrgb[0] = 0;
    hexLinear[0] = 0;

    // 1. The levels of the game's test image, sRGB and linear: its goldens.
    {
        static u8 image[32 * 16 * 4];
        const u32 w[2] = { 32, 32 }, h[2] = { 16, 16 }, flags[2] = { 0, RLDCHAR_TEX_LINEAR };
        u8 *rgba[2];
        size_t size = 0;
        u8 *block;
        RsTexTest_GameImage(image, 32, 16);
        rgba[0] = image;
        rgba[1] = image;
        block = RsTexTest_Block(2, w, h, flags, rgba, &size);
        RsTexTest(block && RsTex_ParseNative(block, size, 0, &rn, why, 256), "hand-made block with the game's test image");
        if (rn.textureCount == 2) {
            RsTexTest_ChainHex(&rn.tex[0], hexSrgb);
            RsTexTest_ChainHex(&rn.tex[1], hexLinear);
            RsTexTest(rn.tex[0].levels == 6 && rn.tex[0].chainBytes == 2732u && strcmp(hexSrgb, RLDMK_GOLDEN_MIP_SRGB) == 0 &&
                          strcmp(hexLinear, RLDMK_GOLDEN_MIP_LINEAR) == 0,
                      "levels of the game's test image = the game's golden (native_tex_selftest)");
        }
        RsTex_FreeNative(&rn);
        free(block);
    }

    // 2. The mini OBJ of the self-test through make-char, as RLDPN2.
    memset(&k, 0, sizeof(k));
    k.want = 1;
    memset(&native, 0, sizeof(native));
    if (!RldMk_NativeObjMake(&k, &a) || !a.made) {
        printf("rs_tex selftest FAILED: the mini OBJ cannot be made\n");
        return 1;
    }
    RldMk_CheckNativeBytes(a.cmdl, a.cmdlSize, a.chriFlags, a.cnet, a.cnetSize, a.ctxt, a.ctxtSize, &native);
    RsTexTest(native.state == RLDCHAR_NATIVE_READY, "the mini OBJ passes the game's rules");
    if (native.state == RLDCHAR_NATIVE_READY) {
        const u8 *materialsBefore = native.materials;
        const u8 *colorsBefore = native.colors;
        u8 *colors = (u8 *)malloc((size_t)native.vertexCount * 4u + 4u);
        u8 *materials = (u8 *)malloc((size_t)native.materialCount * RLDCHAR_NET_MATERIAL_BYTES);
        size_t size = 0;
        u8 *block;

        // COL0 of its own and the kart's material as a blend: both must
        // come through as they are.
        if (colors && materials) {
            for (i = 0; i < (int)native.vertexCount * 4; i++)
                colors[i] = (u8)(i * 7 + 3);
            memcpy(materials, native.materials, (size_t)native.materialCount * RLDCHAR_NET_MATERIAL_BYTES);
            native.colors = colors;
            native.materials = materials;
        }
        for (int pass = 0; pass < 3; pass++) {
            if (pass == 1 && colors && materials)
                materials[6] = 2;   // material 0, the kart: blend
            if (pass == 2) {
                native.colors = colorsBefore;
                native.materials = materialsBefore;
            }
            block = RldMk_NativePreviewBlock(&native, &size);
            if (!block || !RsTex_ParseNative(block, size, 0, &rn, why, 256)) {
                RsTexTest(0, "RLDPN2 of the mini OBJ is read");
                free(block);
                continue;
            }
            RsTexTest(rn.textureCount == (int)native.textureCount && rn.materialCount == (int)native.materialCount &&
                          rn.triangles == (int)native.triangleCount && (rn.flags == (pass == 1 ? RS_TEX_NATIVE_BLEND : 0u)),
                      "RLDPN2 counts and the blend flag");
            for (t = 0; t < rn.textureCount; t++)
                RsTexTest(rn.tex[t].w == (int)native.texture[t].width && rn.tex[t].h == (int)native.texture[t].height &&
                              rn.tex[t].flags == native.texture[t].flags &&
                              memcmp(rn.tex[t].level[0], native.texture[t].rgba, (size_t)rn.tex[t].w * rn.tex[t].h * 4u) == 0 &&
                              RsTexTest_SameChain(&rn.tex[t]),
                          "texture whole, flags, every level = RldMip_BuildLevels");
            for (i = 0; i < rn.materialCount; i++) {
                const u8 *m = &native.materials[(size_t)i * RLDCHAR_NET_MATERIAL_BYTES];
                RsTexTest(memcmp(rn.mat[i].rgba, m, 4) == 0 && rn.mat[i].texture == (int)(s16)Rld_ReadLE16(&m[4]) &&
                              rn.mat[i].alphaMode == m[6] && rn.mat[i].nearest == (m[7] & 1),
                          "material as MATL");
            }
            for (tri = 0; tri < rn.triangles; tri++) {
                int same = RsTex_NativeMaterialOf(&rn, tri) == (int)Rld_ReadLE16(&native.triangleMaterials[2u * (u32)tri]);
                for (c = 0; c < 3; c++) {
                    const u8 *ix = &native.indices[((size_t)(3 * tri) + (size_t)c) * native.indexSize];
                    const u32 vertex = (native.indexSize == 2u) ? Rld_ReadLE16(ix) : Rld_ReadLE32(ix);
                    struct RsTexCorner corner;
                    int xyz[3], p, ax;
                    RsTex_NativeCorner(&rn, tri, c, &corner);
                    same = same && corner.u == RldMk_NativePreviewQ16(RldChar_F32(&native.uv[8u * vertex])) &&
                           corner.v == RldMk_NativePreviewQ16(RldChar_F32(&native.uv[8u * vertex + 4u]));
                    if (native.colors)
                        same = same && corner.r == native.colors[4u * vertex] && corner.g == native.colors[4u * vertex + 1u] &&
                               corner.b == native.colors[4u * vertex + 2u] && corner.a == native.colors[4u * vertex + 3u];
                    else
                        same = same && corner.r == 255 && corner.g == 255 && corner.b == 255 && corner.a == 255;
                    for (p = 0; p < RS_TEX_POSES; p++) {
                        const u32 slot = (native.poseCount == 0u) ? 0u : s_rldMkNativePreviewFrames[p];
                        const u8 *pose = &native.poses[(size_t)slot * native.vertexCount * RLDCHAR_NET_VERTEX_BYTES];
                        RsTex_NativePosition(&rn, p, tri, c, xyz);
                        for (ax = 0; ax < 3; ax++)
                            same = same && xyz[ax] == RldMk_NativePreviewSixteenth(RldChar_F32(&pose[vertex * RLDCHAR_NET_VERTEX_BYTES + 4u * (u32)ax]));
                    }
                }
                if (!same) {
                    RsTexTest(0, "corners, colours, materials and positions as written");
                    break;
                }
            }
            // The driver's material masks; texel (1, 1) of its texture is transparent.
            if (pass == 2) {
                int driver = -1;
                for (i = 0; i < rn.materialCount; i++)
                    if (rn.mat[i].alphaMode == RS_TEX_ALPHA_MASK && rn.mat[i].texture >= 0)
                        driver = i;
                RsTexTest(driver >= 0, "a mask material");
                if (driver >= 0) {
                    const struct RsTexImage *img = RsTex_NativeImage(&rn, driver);
                    const long long u1 = (1LL * 65536 + 32768) / img->w, v1 = (1LL * 65536 + 32768) / img->h;
                    const unsigned char *hole = RsTex_Texel(img, 0, u1, v1);
                    const unsigned char *solid = RsTex_Texel(img, 0, 0, 0);
                    RsTexTest(hole[3] == 0 && RsTex_Shade(hole, 255, 255, 255, 255, &rn.mat[driver]) == RS_TEX_DISCARD &&
                                  RsTex_Shade(solid, 255, 255, 255, 255, &rn.mat[driver]) ==
                                      (int)(((u32)solid[0] << 16) | ((u32)solid[1] << 8) | solid[2]),
                              "the mask leaves the transparent texel out, white keeps the texel");
                }
            }
            RsTex_FreeNative(&rn);
            free(block);
        }
        native.colors = colorsBefore;
        native.materials = materialsBefore;
        free(colors);
        free(materials);
    }
    RldChar_FreeNative(&native);
    RldMk_NativeObjFree(&a);

    // 3. The formula against the float32 program: every texel, corner and
    // material byte; the mask for every alpha triple.
    for (t = -1; t < 256; t++)
        for (c = 0; c < 256; c++)
            for (i = 0; i < 256; i++) {
                struct RsTexMaterial m;
                unsigned char texel[4];
                int ours, gpu, d;
                memset(&m, 0, sizeof(m));
                m.rgba[0] = (unsigned char)i;
                m.rgba[3] = 255;
                texel[0] = (unsigned char)(t < 0 ? 0 : t);
                texel[3] = 255;
                ours = RsTex_Shade(t < 0 ? NULL : texel, c, 0, 0, 255, &m) >> 16;
                gpu = RsTexTest_ShaderChannel(t, c, i, 0);
                d = ours > gpu ? ours - gpu : gpu - ours;
                formulaOff += d != 0;
                formulaMax = d > formulaMax ? d : formulaMax;
                if (t >= 0) {
                    gpu = RsTexTest_ShaderChannel(t, c, i, 1);
                    d = ours > gpu ? ours - gpu : gpu - ours;
                    srgbOff += d != 0;
                    formulaMax = d > formulaMax ? d : formulaMax;
                }
                m.alphaMode = RS_TEX_ALPHA_MASK;
                m.rgba[3] = (unsigned char)i;
                texel[3] = (unsigned char)(t < 0 ? 0 : t);
                maskOff += (RsTex_Shade(t < 0 ? NULL : texel, 0, 0, 0, c, &m) != RS_TEX_DISCARD) != RsTexTest_ShaderKeeps(t, c, i);
            }
    RsTexTest(formulaMax <= 1, "formula within 1 of the float32 program");

    // 4. Texel coordinates: repeat, clamp, mirror against the plain rule.
    {
        int bad = 0;
        long long q;
        for (q = -3 * 65536 - 77; q <= 3 * 65536 + 77; q += 97) {
            const long long f = (long long)floor((double)q * 16.0 / 65536.0);
            const long long rep = ((f % 16) + 16) % 16;
            const long long mm = ((f % 32) + 32) % 32;
            const long long mir = mm < 16 ? mm : 31 - mm;
            const long long cl = f < 0 ? 0 : (f > 15 ? 15 : f);
            bad += RsTex_Coord(q, 4, RS_TEX_WRAP_REPEAT) != rep;
            bad += RsTex_Coord(q, 4, RS_TEX_WRAP_MIRROR) != mir;
            bad += RsTex_Coord(q, 4, RS_TEX_WRAP_CLAMP) != cl;
        }
        RsTexTest(bad == 0, "wrap repeat, clamp and mirror");
    }

    // 5. The level rule (Vulkan, mipmap mode nearest) against log2 in double,
    // a texture of 2048 (12 levels); x and y, either axis.
    {
        struct RsTexImage img;
        int bad = 0;
        long long q;
        memset(&img, 0, sizeof(img));
        img.w = img.h = 2048;
        img.levels = 12;
        img.shiftW[0] = img.shiftH[0] = 11;
        for (q = 1; q <= (1LL << 20); q += 1 + q / 512) {
            const double rho = (double)q * 2048.0 / 65536.0;
            const double lambda = log2(rho);
            int want = lambda <= 0.5 ? 0 : (int)ceil(lambda + 0.5) - 1;
            want = want > 11 ? 11 : want;
            bad += RsTex_Level(&img, q, 0, 0, 0) != want;
            bad += RsTex_Level(&img, 0, 0, 0, -q) != want;
            bad += RsTex_Level(&img, q / 2, q / 3, q, q / 5) < RsTex_Level(&img, q / 2, q / 3, 0, 0);
        }
        // Just below and above 2^0.5 texels per pixel (rho^2 = 2^33 in Q32) on
        // a texture of 16: q / 4096 texels, 2^0.5 x 4096 = 5792.6.
        img.w = img.h = 16;
        img.levels = 5;
        img.shiftW[0] = img.shiftH[0] = 4;
        bad += RsTex_Level(&img, 5792, 0, 0, 0) != 0;
        bad += RsTex_Level(&img, 5793, 0, 0, 0) != 1;
        bad += RsTex_Level(&img, 0, 5792, 5792, 0) != 0;
        bad += RsTex_Level(&img, 1LL << 30, 0, 0, 0) != 4;   // far beyond: the last level
        RsTexTest(bad == 0, "mip level by the Vulkan rule");
    }

    // 6. The slopes: an affine triangle exactly, a tilted one against the
    // perspective formula differentiated in double.
    {
        const long long x[3] = { 0, 1600, 0 }, y[3] = { 0, 0, 1600 }, zf[3] = { 4096, 4096, 4096 }, zt[3] = { 4096, 2048, 4096 };
        const long long u[3] = { 0, 65536, 0 }, v[3] = { 0, 0, 65536 };
        long long s[4];
        double ref[4];
        int ok;
        RsTex_TriangleSlopes(x, y, zf, u, v, s);
        ok = s[0] == 655 && s[1] == 0 && s[2] == 0 && s[3] == 655;
        RsTex_TriangleSlopes(x, y, zt, u, v, s);
        {
            // u at a screen point (sx, sy in pixels) with weights l_i and depth z_i
            const double cx = 1600.0 / 16.0 / 3.0, cy = cx, hstep = 1e-3;
            double val[3][2];
            int p;
            for (p = 0; p < 3; p++) {
                const double sx = cx + (p == 1 ? hstep : 0.0), sy = cy + (p == 2 ? hstep : 0.0);
                const double l1 = sx / 100.0, l2 = sy / 100.0, l0 = 1.0 - l1 - l2;
                const double b = l0 * zt[0] + l1 * zt[1] + l2 * zt[2];
                val[p][0] = (l1 * zt[1] * 65536.0) / b;
                val[p][1] = (l2 * zt[2] * 65536.0) / b;
            }
            ref[0] = (val[1][0] - val[0][0]) / hstep;
            ref[1] = (val[1][1] - val[0][1]) / hstep;
            ref[2] = (val[2][0] - val[0][0]) / hstep;
            ref[3] = (val[2][1] - val[0][1]) / hstep;
            for (p = 0; p < 4; p++)
                ok = ok && fabs((double)s[p] - ref[p]) <= 2.0 + fabs(ref[p]) * 0.01;
        }
        RsTex_TriangleSlopes(x, x, zf, u, v, s);   // no area
        ok = ok && s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0;
        RsTexTest(ok, "slopes of a triangle at its centroid");
    }

    // 7. Refused: cut short, the old format, an unknown wrap, a material
    // naming a missing texture, over the game's budget. Nothing is kept.
    {
        static u8 image[16 * 16 * 4];
        const u32 w[4] = { 2048, 2048, 2048, 2048 }, h[4] = { 2048, 2048, 2048, 2048 }, flags[4] = { 0, 0, 0, 0 };
        const u32 w1[1] = { 16 }, h1[1] = { 16 }, wrapBad[1] = { 3u << 1 };
        u8 *one[1] = { image };
        size_t size = 0;
        u8 *block = RsTexTest_Block(1, w1, h1, flags, one, &size);
        int refused = 0;
        if (block) {
            refused += !RsTex_ParseNative(block, size - 1, 0, &rn, why, 256) && rn.textureCount == 0 && why[0] != 0;
            block[5] = '1';
            refused += !RsTex_ParseNative(block, size, 0, &rn, why, 256) && wcsstr(why, L"another format") != NULL;
            block[5] = '2';
            Rld_WriteLE16(&block[16 + 12 + 16 * 16 * 4 + 4 + 4], 1);   // material 0 names texture 1 of 1
            refused += !RsTex_ParseNative(block, size, 0, &rn, why, 256) && wcsstr(why, L"names texture") != NULL;
            free(block);
        }
        block = RsTexTest_Block(1, w1, h1, wrapBad, one, &size);
        if (block) {
            refused += !RsTex_ParseNative(block, size, 0, &rn, why, 256) && wcsstr(why, L"wrap") != NULL;
            free(block);
        }
        block = RsTexTest_Block(4, w, h, flags, NULL, &size);
        if (block) {
            refused += !RsTex_ParseNative(block, size, 0, &rn, why, 256) && rn.textureCount == 0 && wcsstr(why, L"at most 65536 KiB") != NULL;
            free(block);
        }
        RsTexTest(refused == 5, "refused files say why and keep nothing");
    }

    // 8. The wheel of char-wheel, RLDPW3: an OBJ with a texture, a PLY without.
    {
        static const double origin[3] = { 0.0, 0.0, 0.0 };
        struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
        struct RldWheelOptions wo;
        struct RldWheelResult res;
        struct RsTexWheel wheel;
        int ok = 0;
        memset(&wo, 0, sizeof(wo));
        memset(&wheel, 0, sizeof(wheel));
        if (mesh) {
            RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 0);
            if (RldWheel_TestBuildObj(mesh, 64u, 32u, 0, &res) && RsTex_ParseWheel(res.preview, res.previewSize, &wheel, why, 256)) {
                struct RsTexCorner corner;
                int xyz[3];
                RsTex_WheelCorner(&wheel, 0, 0, xyz, &corner);
                ok = wheel.textured && wheel.tex.w == 64 && wheel.tex.h == 32 && wheel.tex.levels == 7 && RsTexTest_SameChain(&wheel.tex) &&
                     wheel.tex.wrapU == RS_TEX_WRAP_CLAMP && wheel.mat.texture == 0 && wheel.mat.alphaMode == RS_TEX_ALPHA_OPAQUE &&
                     wheel.mat.rgba[0] == 255 && wheel.mat.rgba[3] == 255 && wheel.triangles == 64 && corner.u == 49152 && corner.v == 32768 &&
                     corner.r == 255 && corner.a == 255 && !RsTex_WheelTwoSided(&wheel, 0);
            }
            RsTex_FreeWheel(&wheel);
            RldWheel_FreeResult(&res);
            if (ok && RldWheel_TestBuild(mesh, &wo, &res) && RsTex_ParseWheel(res.preview, res.previewSize, &wheel, why, 256))
                ok = !wheel.textured && wheel.mat.texture == -1 && wheel.triangles == 64;
            else
                ok = 0;
            RsTex_FreeWheel(&wheel);
            RldWheel_FreeResult(&res);
            free(mesh);
        }
        RsTexTest(ok, "RLDPW3 of char-wheel: textured OBJ and plain PLY");
    }

    if (s_rsTexTestFailed) {
        printf("rs_tex selftest FAILED: %d case(s)\n", s_rsTexTestFailed);
        return 1;
    }
    printf("rs_tex selftest passed: levels = the game's golden srgb %.16s linear %.16s, RLDPN2 and RLDPW3 read back, formula against float32: "
           "%lld of %d off by 1 (linear), %lld of %d (sRGB round trip), max %lld, mask %lld differ\n",
           hexSrgb, hexLinear, formulaOff, 257 * 256 * 256, srgbOff, 256 * 256 * 256, formulaMax, maskOff);
    return 0;
}
