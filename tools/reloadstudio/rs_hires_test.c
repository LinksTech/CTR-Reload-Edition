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
//
// The guided flow of the page Character (rs_charhires.c, rs_charroute.h): the
// presets of the routes, the plain-English messages of the CHRG routes on
// karts made here (Kt_Glb) and the switches of the classic fallback. With
// --write <folder> it writes the karts and switches of the byte test of the
// routes (cmake/run_rs_charroute.cmake) instead.
//
// THE VERTEX COLOURS (include/rldhires_color.inc, rs_charroute.h): the
// suggestion of a mode on karts made here (PS1-like COLOR_0 -> PSX, white-ish
// -> glTF, none -> no switch, on untextured faces only -> the route's), the
// warning HRG-36 in glTF only, and the picture's colour in every mode against
// RldHiresColor_Apply of RldHiresColor_Factor - the preview's product within
// 2 levels of the shared function (RS_TEX_MATERIAL_LIT holds C and the light
// in steps of 1/128) - also through the view's own formula (RsTex_ShadeLit).
// The light: the engine's (RLDHIRES_LIGHT_*), after the product is clamped,
// as the engine's PSX blend does.
//
// THE UPSCALE (rs_charroute.h): "Upscale small textures" suggested for karts
// with a 16, 40 and 63 px texture, not for 64 px, no texture or a texture of
// unknown size (a JPEG), on Low-Poly and Hi-Res alike, never on Vanilla; and
// the line below the box.

#include "reloadstudio.h"
#include "rs_hires_model.h"
#include "rs_charroute.h"
#include "rs_tex.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

// The one way to read COLOR_0 (the same functions rs_hires_model.c calls):
// the self-test computes what the picture must show with them.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#include <rldhires_color.inc>
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
        // 1 m = 1024 file units. Its normals +Z: the engine's light (THE
        // PREVIEW LIGHT of include/rldhires_color.inc) is the ambient half way
        // (0.875) and the sun's 0.40 x 0.399 = 1.035 - above white, so white
        // (the test PNG has no pixels to decode, the base colour factor 1
        // alone, no COLOR_0) stays white: 255.
        snprintf(line, sizeof(line), "%s: the body triangle in file units (1 m = 1024)", what);
        Check(ReadS16(t) == 0 && ReadS16(t + 2) == 0 && ReadS16(t + 4) == 0 && ReadS16(t + 10) == 1024 && ReadS16(t + 12) == 0 &&
                  ReadS16(t + 20) == 0 && ReadS16(t + 22) == 1024,
              line);
        snprintf(line, sizeof(line), "%s: the corner colour is lit white (255: the engine's light 1.035) and one-sided", what);
        Check(t[6] == 255 && t[7] == 255 && t[8] == 255 && t[9] == 0, line);
        // A wheel (node 3, wheel_fl at (0.5, 0.25, 0.5) m): its first corner.
        snprintf(line, sizeof(line), "%s: wheel_fl stands at its node (512, 256, 512)", what);
        Check(ReadS16(t + 30) == 512 && ReadS16(t + 32) == 256 && ReadS16(t + 34) == 512, line);
    }
}

// ---------------------------------------------------------------------------
// THE GUIDED FLOW (rs_charhires.c, rs_charroute.h): the presets of the routes,
// the plain-English messages of the CHRG routes and the switches of the
// classic fallback - on karts made here, in memory (no community content).

// A growing byte buffer.
struct KtBuf {
    unsigned char *p;
    size_t n, cap;
};

static void Kt_Put(struct KtBuf *b, const void *data, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        unsigned char *p;
        while (cap < b->n + n)
            cap *= 2;
        p = (unsigned char *)realloc(b->p, cap);
        if (!p) {
            printf("  FAIL rs_hires: out of memory\n");
            exit(3);
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
}

static void Kt_Pad(struct KtBuf *b, unsigned char with)
{
    while (b->n % 4u)
        Kt_Put(b, &with, 1);
}

static void Kt_Text(struct KtBuf *b, const char *format, ...)
{
    char text[1024];
    va_list list;
    int n;
    va_start(list, format);
    n = vsnprintf(text, sizeof(text), format, list);
    va_end(list);
    if (n > 0)
        Kt_Put(b, text, (size_t)n < sizeof(text) ? (size_t)n : sizeof(text) - 1);
}

static void Kt_U32(struct KtBuf *b, unsigned v)
{
    unsigned char x[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    Kt_Put(b, x, 4);
}

static void Kt_U32Be(struct KtBuf *b, unsigned v)
{
    unsigned char x[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    Kt_Put(b, x, 4);
}

static void Kt_F(struct KtBuf *b, float f)
{
    Kt_Put(b, &f, 4);   // little-endian IEEE on every target of the Studio
}

static unsigned Kt_Crc(const unsigned char *p, size_t n)
{
    unsigned crc = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void Kt_PngChunk(struct KtBuf *b, const char *type, const unsigned char *body, unsigned len)
{
    size_t at;
    Kt_U32Be(b, len);
    at = b->n;
    Kt_Put(b, type, 4);
    if (len)
        Kt_Put(b, body, len);
    Kt_U32Be(b, Kt_Crc(b->p + at, 4u + len));
}

// An RGBA PNG of edge x edge: a checker of orange and blue, 4 texels a square
// (one stored deflate block, so edge <= 100); header = 1: the IHDR only, an
// empty IDAT (the checks read only the header, never the pixels).
static void Kt_Png(struct KtBuf *b, unsigned edge, int header)
{
    static const unsigned char sig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    unsigned char ihdr[13] = { 0 };
    struct KtBuf raw = { 0 }, idat = { 0 };
    unsigned x, y, i, a = 1, s = 0;
    ihdr[0] = (unsigned char)(edge >> 24), ihdr[1] = (unsigned char)(edge >> 16), ihdr[2] = (unsigned char)(edge >> 8), ihdr[3] = (unsigned char)edge;
    memcpy(ihdr + 4, ihdr, 4);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 6;   // RGBA
    Kt_Put(b, sig, 8);
    Kt_PngChunk(b, "IHDR", ihdr, 13);
    if (!header) {
        for (y = 0; y < edge; y++) {
            unsigned char none = 0;
            Kt_Put(&raw, &none, 1);
            for (x = 0; x < edge; x++) {
                const int orange = ((x / 4u) + (y / 4u)) % 2u == 0;
                unsigned char px[4];
                px[0] = (unsigned char)(orange ? 230 : 40);
                px[1] = (unsigned char)(orange ? 120 : 80);
                px[2] = (unsigned char)(orange ? 20 : 200);
                px[3] = 255;
                Kt_Put(&raw, px, 4);
            }
        }
        for (i = 0; i < raw.n; i++) {
            a = (a + raw.p[i]) % 65521u;
            s = (s + a) % 65521u;
        }
        {
            unsigned char head[7] = { 0x78, 0x01, 0x01, (unsigned char)raw.n, (unsigned char)(raw.n >> 8), (unsigned char)~raw.n,
                                      (unsigned char)(~raw.n >> 8) };
            Kt_Put(&idat, head, 7);
        }
        Kt_Put(&idat, raw.p, raw.n);
        Kt_U32Be(&idat, (s << 16) | a);
    }
    Kt_PngChunk(b, "IDAT", idat.p, (unsigned)idat.n);
    Kt_PngChunk(b, "IEND", NULL, 0);
    free(raw.p);
    free(idat.p);
}

// What a kart of the self-test has.
struct KtSpec {
    int grid;       // quads along each edge of a box face (>= 1): 24 grid^2 body triangles
    int skin;       // 1 = an armature of one bone ("root"), the body skinned to it
    int clips;      // the first N clips of the fixed list, each turning the bone (the body without a skin)
    int wheels;     // wheel nodes: 0, 2 (the front ones) or 4, a box of 12 triangles each
    int png;        // the edge of a real RGBA PNG on every face (<= 100), 0 = a plain colour
    int pngHeader;  // the edge of a PNG with its header only (for the checks), 0 = none
    int vcol;       // COLOR_0 on the body: every corner this sRGB byte (grey), 0 = no COLOR_0
};

// A box of grid x grid quads per face into positions, normals, UVs and indices.
static void Kt_Box(struct KtBuf *pos, struct KtBuf *nrm, struct KtBuf *uv, struct KtBuf *idx, unsigned *verts, const float c[3], const float h[3], int grid)
{
    int axis, sign, i, j;
    for (axis = 0; axis < 3; axis++) {
        for (sign = -1; sign <= 1; sign += 2) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            const unsigned base = *verts;
            for (j = 0; j <= grid; j++) {
                for (i = 0; i <= grid; i++) {
                    float p[3], n[3] = { 0, 0, 0 };
                    p[axis] = c[axis] + (float)sign * h[axis];
                    p[u] = c[u] + h[u] * (2.0f * (float)i / (float)grid - 1.0f);
                    p[v] = c[v] + h[v] * (2.0f * (float)j / (float)grid - 1.0f);
                    n[axis] = (float)sign;
                    Kt_F(pos, p[0]), Kt_F(pos, p[1]), Kt_F(pos, p[2]);
                    Kt_F(nrm, n[0]), Kt_F(nrm, n[1]), Kt_F(nrm, n[2]);
                    Kt_F(uv, (float)i / (float)grid), Kt_F(uv, (float)j / (float)grid);
                    (*verts)++;
                }
            }
            for (j = 0; j < grid; j++) {
                for (i = 0; i < grid; i++) {
                    const unsigned v00 = base + (unsigned)(j * (grid + 1) + i), v10 = v00 + 1, v01 = v00 + (unsigned)(grid + 1), v11 = v01 + 1;
                    const unsigned t[6] = { v00, sign > 0 ? v10 : v11, sign > 0 ? v11 : v10, v00, sign > 0 ? v11 : v01, sign > 0 ? v01 : v11 };
                    int k;
                    for (k = 0; k < 6; k++)
                        Kt_U32(idx, t[k]);
                }
            }
        }
    }
}

// One view of the BIN chunk (4-aligned) and its accessor; returns the accessor.
static int Kt_View(struct KtBuf *bin, struct KtBuf *views, struct KtBuf *acc, int *viewCount, int *accCount, const struct KtBuf *data,
                   int componentType, unsigned count, const char *type, const char *extra)
{
    Kt_Pad(bin, 0);
    Kt_Text(views, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", *viewCount ? "," : "", (unsigned)bin->n, (unsigned)data->n);
    Kt_Put(bin, data->p, data->n);
    Kt_Text(acc, "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%u,\"type\":\"%s\"%s}", *accCount ? "," : "", *viewCount, componentType,
            count, type, extra ? extra : "");
    (*viewCount)++;
    return (*accCount)++;
}

// The position accessor's bounds (glTF asks for them).
static void Kt_Bounds(const struct KtBuf *pos, char *out, size_t cap)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    size_t i;
    int k;
    for (i = 0; i + 12 <= pos->n; i += 12)
        for (k = 0; k < 3; k++) {
            float f;
            memcpy(&f, pos->p + i + 4u * (unsigned)k, 4);
            lo[k] = f < lo[k] ? f : lo[k];
            hi[k] = f > hi[k] ? f : hi[k];
        }
    snprintf(out, cap, ",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
}

// The indices as 16 bits when they fit.
static int Kt_Indices(struct KtBuf *bin, struct KtBuf *views, struct KtBuf *acc, int *viewCount, int *accCount, const struct KtBuf *idx,
                      unsigned verts)
{
    unsigned count = (unsigned)(idx->n / 4u);
    if (verts <= 65535u) {
        struct KtBuf small = { 0 };
        unsigned i;
        int r;
        for (i = 0; i < count; i++) {
            unsigned v;
            memcpy(&v, idx->p + 4u * i, 4);
            Kt_Put(&small, &v, 2);
        }
        r = Kt_View(bin, views, acc, viewCount, accCount, &small, 5123, count, "SCALAR", NULL);
        free(small.p);
        return r;
    }
    return Kt_View(bin, views, acc, viewCount, accCount, idx, 5125, count, "SCALAR", NULL);
}

static const char *const s_ktClip[12] = { "idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right",
                                          "hit", "podium_1", "podium_2", "podium_3", "lose", "menu" };
static const char *const s_ktWheel[4] = { "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr" };
static const float s_ktWheelAt[4][3] = { { 0.42f, 0.15f, 0.42f }, { -0.42f, 0.15f, 0.42f }, { 0.42f, 0.15f, -0.42f }, { -0.42f, 0.15f, -0.42f } };

// A kart as GLB (malloc'ed): a kart box (0.7 x 0.25 x 1.2 m) and a driver box
// on it, +X left, +Y up, +Z forward, the wheels at the four corners.
static unsigned char *Kt_Glb(const struct KtSpec *sp, size_t *size)
{
    static const float kart[2][3] = { { 0.0f, 0.225f, 0.0f }, { 0.35f, 0.125f, 0.6f } };
    static const float driver[2][3] = { { 0.0f, 0.575f, -0.1f }, { 0.15f, 0.225f, 0.15f } };
    static const float wheel[2][3] = { { 0.0f, 0.0f, 0.0f }, { 0.06f, 0.15f, 0.15f } };
    struct KtBuf bin = { 0 }, views = { 0 }, acc = { 0 }, json = { 0 }, glb = { 0 };
    struct KtBuf pos = { 0 }, nrm = { 0 }, uv = { 0 }, idx = { 0 }, tmp = { 0 };
    int viewCount = 0, accCount = 0, aPos, aNrm, aUv, aIdx, aJnt = -1, aWgt = -1, aIbm = -1, aIn = -1, aOut = -1, image = -1;
    int wPos = -1, wNrm = -1, wUv = -1, wIdx = -1, aCol = -1, i;
    const int wheels = sp->wheels;
    const int rootNode = sp->skin ? 1 : -1;
    const int wheelNode = sp->skin ? 2 : 1;
    unsigned verts = 0;
    char bounds[160];

    Kt_Box(&pos, &nrm, &uv, &idx, &verts, kart[0], kart[1], sp->grid);
    Kt_Box(&pos, &nrm, &uv, &idx, &verts, driver[0], driver[1], sp->grid);
    Kt_Bounds(&pos, bounds, sizeof(bounds));
    aPos = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &pos, 5126, verts, "VEC3", bounds);
    aNrm = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &nrm, 5126, verts, "VEC3", NULL);
    aUv = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &uv, 5126, verts, "VEC2", NULL);
    aIdx = Kt_Indices(&bin, &views, &acc, &viewCount, &accCount, &idx, verts);
    if (sp->vcol > 0) {
        // The sRGB byte as the linear float an exporter writes.
        const float lin = RldHiresColor_Decode((float)sp->vcol / 255.0f), one = 1.0f;
        tmp.n = 0;
        for (i = 0; i < (int)verts; i++) {
            Kt_F(&tmp, lin), Kt_F(&tmp, lin), Kt_F(&tmp, lin);
            Kt_Put(&tmp, &one, 4);
        }
        aCol = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &tmp, 5126, verts, "VEC4", NULL);
    }
    if (sp->skin) {
        static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        static const float one[4] = { 1, 0, 0, 0 };
        static const unsigned char bone[4] = { 0, 0, 0, 0 };
        tmp.n = 0;
        for (i = 0; i < (int)verts; i++)
            Kt_Put(&tmp, bone, 4);
        aJnt = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &tmp, 5121, verts, "VEC4", NULL);
        tmp.n = 0;
        for (i = 0; i < (int)verts; i++)
            Kt_Put(&tmp, one, 16);
        aWgt = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &tmp, 5126, verts, "VEC4", NULL);
        tmp.n = 0;
        Kt_Put(&tmp, identity, 64);
        aIbm = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &tmp, 5126, 1, "MAT4", NULL);
    }
    if (wheels) {
        unsigned wv = 0;
        pos.n = nrm.n = uv.n = idx.n = 0;
        Kt_Box(&pos, &nrm, &uv, &idx, &wv, wheel[0], wheel[1], 1);
        Kt_Bounds(&pos, bounds, sizeof(bounds));
        wPos = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &pos, 5126, wv, "VEC3", bounds);
        wNrm = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &nrm, 5126, wv, "VEC3", NULL);
        wUv = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &uv, 5126, wv, "VEC2", NULL);
        wIdx = Kt_Indices(&bin, &views, &acc, &viewCount, &accCount, &idx, wv);
    }
    if (sp->clips) {
        tmp.n = 0;
        Kt_F(&tmp, 0.0f), Kt_F(&tmp, 1.0f);
        aIn = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &tmp, 5126, 2, "SCALAR", ",\"min\":[0],\"max\":[1]");
        tmp.n = 0;
        for (i = 0; i < 2; i++)
            Kt_F(&tmp, 0.0f), Kt_F(&tmp, 0.0f), Kt_F(&tmp, 0.0f), Kt_F(&tmp, 1.0f);
        aOut = Kt_View(&bin, &views, &acc, &viewCount, &accCount, &tmp, 5126, 2, "VEC4", NULL);
    }
    if (sp->png || sp->pngHeader) {
        tmp.n = 0;
        Kt_Png(&tmp, sp->png ? (unsigned)sp->png : (unsigned)sp->pngHeader, sp->png == 0);
        Kt_Pad(&bin, 0);
        Kt_Text(&views, ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", (unsigned)bin.n, (unsigned)tmp.n);
        Kt_Put(&bin, tmp.p, tmp.n);
        image = viewCount++;
    }
    Kt_Pad(&bin, 0);

    Kt_Text(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"rs_hires_selftest\"},\"scene\":0,\"scenes\":[{\"nodes\":[0");
    if (sp->skin)
        Kt_Text(&json, ",1");
    for (i = 0; i < wheels; i++)
        Kt_Text(&json, ",%d", wheelNode + i);
    Kt_Text(&json, "]}],\"nodes\":[{\"name\":\"body\",\"mesh\":0%s}", sp->skin ? ",\"skin\":0" : "");
    if (sp->skin)
        Kt_Text(&json, ",{\"name\":\"root\"}");
    for (i = 0; i < wheels; i++)
        Kt_Text(&json, ",{\"name\":\"%s\",\"mesh\":1,\"translation\":[%.9g,%.9g,%.9g]}", s_ktWheel[i], s_ktWheelAt[i][0], s_ktWheelAt[i][1], s_ktWheelAt[i][2]);
    Kt_Text(&json, "],\"meshes\":[{\"name\":\"body\",\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d", aPos, aNrm, aUv);
    if (sp->skin)
        Kt_Text(&json, ",\"JOINTS_0\":%d,\"WEIGHTS_0\":%d", aJnt, aWgt);
    if (aCol >= 0)
        Kt_Text(&json, ",\"COLOR_0\":%d", aCol);
    Kt_Text(&json, "},\"indices\":%d,\"material\":0}]}", aIdx);
    if (wheels)
        Kt_Text(&json, ",{\"name\":\"wheel\",\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,\"TEXCOORD_0\":%d},\"indices\":%d,\"material\":0}]}",
                wPos, wNrm, wUv, wIdx);
    Kt_Text(&json, "],\"materials\":[{\"name\":\"paint\",\"pbrMetallicRoughness\":{%s}}]",
            image >= 0 ? "\"baseColorTexture\":{\"index\":0}" : "\"baseColorFactor\":[0.8,0.3,0.1,1]");
    if (image >= 0)
        Kt_Text(&json, ",\"textures\":[{\"source\":0}],\"images\":[{\"name\":\"paint\",\"bufferView\":%d,\"mimeType\":\"image/png\"}]", image);
    if (sp->skin)
        Kt_Text(&json, ",\"skins\":[{\"joints\":[1],\"inverseBindMatrices\":%d}]", aIbm);
    for (i = 0; i < sp->clips && i < 12; i++)
        Kt_Text(&json, "%s{\"name\":\"%s\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":%d,\"path\":\"rotation\"}}],\"samplers\":[{\"input\":%d,\"output\":%d}]}",
                i ? "," : ",\"animations\":[", s_ktClip[i], sp->skin ? rootNode : 0, aIn, aOut);
    if (sp->clips)
        Kt_Text(&json, "]");
    Kt_Text(&json, ",\"bufferViews\":[");
    Kt_Put(&json, views.p, views.n);
    Kt_Text(&json, "],\"accessors\":[");
    Kt_Put(&json, acc.p, acc.n);
    Kt_Text(&json, "],\"buffers\":[{\"byteLength\":%u}]}", (unsigned)bin.n);
    Kt_Pad(&json, ' ');

    Kt_Put(&glb, "glTF", 4);
    Kt_U32(&glb, 2);
    Kt_U32(&glb, (unsigned)(12 + 8 + json.n + 8 + bin.n));
    Kt_U32(&glb, (unsigned)json.n);
    Kt_Put(&glb, "JSON", 4);
    Kt_Put(&glb, json.p, json.n);
    Kt_U32(&glb, (unsigned)bin.n);
    Kt_Put(&glb, "BIN\0", 4);
    Kt_Put(&glb, bin.p, bin.n);
    free(bin.p);
    free(views.p);
    free(acc.p);
    free(json.p);
    free(pos.p);
    free(nrm.p);
    free(uv.p);
    free(idx.p);
    free(tmp.p);
    *size = glb.n;
    return glb.p;
}

static int Kt_Check(const struct KtSpec *sp, struct RsHiresCheck *c)
{
    size_t size = 0;
    unsigned char *glb = Kt_Glb(sp, &size);
    int r = RsHires_CheckGlb(glb, size, c);
    free(glb);
    return r;
}

// How many lines of a place; how many name a single clip ("clip '").
static int Kt_Count(const struct RsWizMsg *m, int n, int place)
{
    int i, k = 0;
    for (i = 0; i < n; i++)
        k += place < 0 || m[i].place == place;
    return k;
}

static int Kt_ClipLines(const struct RsWizMsg *m, int n)
{
    int i, k = 0;
    for (i = 0; i < n; i++)
        k += wcsstr(m[i].text, L"clip '") != NULL;
    return k;
}

// Every line: a sentence; every error and warning a "How to fix"; most severe first.
static int Kt_WellFormed(const struct RsWizMsg *m, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (m[i].text[0] == 0 || (m[i].severity >= RS_SEV_WARNING && m[i].fix[0] == 0))
            return 0;
        if (i > 0 && m[i - 1].severity < m[i].severity)
            return 0;
    }
    return 1;
}

static int Kt_Has(const struct RsWizMsg *m, int n, int severity, int place, const wchar_t *rule, const wchar_t *text, const wchar_t *fix)
{
    int i;
    for (i = 0; i < n; i++)
        if (m[i].severity == severity && m[i].place == place && wcscmp(m[i].rule, rule) == 0 && (!text || wcscmp(m[i].text, text) == 0) &&
            (!fix || wcscmp(m[i].fix, fix) == 0))
            return 1;
    return 0;
}

// A line of that rule, of any severity and place.
static int Kt_HasRule(const struct RsWizMsg *m, int n, const wchar_t *rule)
{
    int i;
    for (i = 0; i < n; i++)
        if (wcscmp(m[i].rule, rule) == 0)
            return 1;
    return 0;
}

static int Kt_RowsOf(const struct RsHiresCheck *c, const wchar_t *rule)
{
    int i, k = 0;
    for (i = 0; i < c->rowCount; i++)
        k += wcscmp(c->rows[i].rule, rule) == 0;
    return k;
}

#define KT_NO_CLIPS L"Your model has no animations yet - it will stand still. Optional."
#define KT_NO_WHEELS L"Your model has no wheels of its own - the game adds standard wheels. Optional."
#define KT_TARGET_FIX L"Use Hi-Res instead, or reduce the model in Blender"

// What the picture must show for one channel: the shared rule -
// RldHiresColor_Apply of RldHiresColor_Factor (the display product, clamped)
// - times the light, clamped, as a byte.
static int Kt_Expect(int mode, float colour0, unsigned texel, float light)
{
    // PSX blend is drawn unlit (the engine draws such a file as the PS1 drew its drivers).
    const float lit = RldHiresColor_Apply(RldHiresColor_Factor(mode, colour0), (float)texel / 255.0f) * (mode == RS_HIRES_VCOL_PSX ? 1.0f : light);
    const float v = (lit < 1.0f ? lit : 1.0f) * 255.0f + 0.5f;
    return (int)v;
}

// THE VERTEX COLOURS: the suggestion, the messages, the picture per mode.
static void Kt_SelfTestColours(void)
{
    static const float c0s[] = { 0.0f, 0.02f, 0.2159f, 0.5f, 0.8f, 1.0f };
    static const unsigned texels[] = { 0u, 37u, 90u, 128u, 200u, 255u };
    static const float lights[] = { 0.8f, 1.0f, 1.35f };
    struct RsHiresCheck c;
    struct RsWizMsg m[96];
    struct RsTexMaterial mat;
    int mode, i, j, worst = 0, worstView = 0, why = -1, n;
    char line[200];

    // 1. The picture's channel against the shared rule, every mode, three
    // lights (below, level, the sun's side): the byte of the picture and the
    // view's own product (RsTex_ShadeLit, a LIT material) within 2.
    memset(&mat, 0, sizeof(mat));
    mat.rgba[0] = mat.rgba[1] = mat.rgba[2] = mat.rgba[3] = 255;
    mat.texture = 0;
    mat.lit = 1;
    for (mode = 0; mode < RS_HIRES_VCOL_MODES; mode++)
        for (i = 0; i < (int)(sizeof(c0s) / sizeof(c0s[0])); i++)
            for (j = 0; j < (int)(sizeof(texels) / sizeof(texels[0])); j++) {
                int k;
                for (k = 0; k < 3; k++) {
                    int corner = 0, got, view, d;
                    const unsigned char t[4] = { (unsigned char)texels[j], (unsigned char)texels[j], (unsigned char)texels[j], 255 };
                    const int want = Kt_Expect(mode, c0s[i], texels[j], lights[k]);
                    const int l = mode == RS_HIRES_VCOL_PSX ? 128 : (int)(lights[k] * 128.0f + 0.5f);   // PSX: unlit
                    got = RsHires_PreviewChannel(mode, 1, c0s[i], texels[j], 1.0f, lights[k], &corner);
                    view = RsTex_ShadeLit(t, corner, corner, corner, 255, l, &mat) & 0xFF;
                    d = abs(got - want);
                    worst = d > worst ? d : worst;
                    d = abs(view - want);
                    worstView = d > worstView ? d : worstView;
                }
            }
    snprintf(line, sizeof(line), "the picture's colour in every mode = Apply(Factor, texel) x light, clamped: within 2 (picture %d, view %d)",
             worst, worstView);
    Check(worst <= 2 && worstView <= 2, line);
    printf("  rs_hires: the picture against RldHiresColor_Apply x Factor x light, 3 modes x 6 colours x 6 texels x 3 lights: %d level(s) "
           "at most (view %d)\n",
           worst, worstView);
    {
        int a = 0, b = 0, k = 0;
        RsHires_PreviewChannel(RS_HIRES_VCOL_PSX, 1, RldHiresColor_Decode(128.0f / 255.0f), 100u, 1.0f, 1.0f, &a);
        RsHires_PreviewChannel(RS_HIRES_VCOL_GLTF, 0, 0.0f, 100u, 1.0f, 1.0f, &b);
        RsHires_PreviewChannel(RS_HIRES_VCOL_PSX, 0, 0.0f, 100u, 1.0f, 1.0f, &k);
        Check(a == 128 && b == 128 && k == 128, "PSX grey 128 and no COLOR_0 (in every mode) are neutral: C = 128 (x 1)");
        RsHires_PreviewChannel(RS_HIRES_VCOL_PSX, 1, 1.0f, 100u, 1.0f, 1.0f, &a);
        Check(a == 255, "PSX white lights up to twice: C = 255 (255/128)");
        RsHires_PreviewChannel(RS_HIRES_VCOL_OFF, 1, 0.0f, 100u, 1.0f, 1.0f, &a);
        Check(a == 128, "off: black COLOR_0 is ignored");
        Check(RsHires_PreviewChannel(RS_HIRES_VCOL_PSX, 1, 1.0f, 200u, 1.0f, 0.8f, NULL) == 255 &&
                  RsHires_PreviewChannel(RS_HIRES_VCOL_PSX, 1, RldHiresColor_Decode(128.0f / 255.0f), 200u, 1.0f, 1.35f, NULL) == 200,
              "PSX blend is unlit: texel 200 x 255/128 clamps to white, grey 128 shows the texel as it is, whatever the light");
        Check(abs(RsHires_PreviewChannel(RS_HIRES_VCOL_GLTF, 1, 1.0f, 200u, 1.0f, 0.8f, NULL) - 160) <= 1,
              "glTF stays lit: texel 200 x white x 0.8 = 160");
    }

    // 2. The suggestion and the switch, on karts made here (16 x 16 texture).
    {
        static const struct KtSpec ps1 = { 1, 0, 0, 0, 16, 0, 128 }, white = { 1, 0, 0, 0, 16, 0, 235 }, none = { 1, 0, 0, 0, 16, 0, 0 },
                                   flat = { 1, 0, 0, 0, 0, 0, 128 };
        unsigned char before[64];
        size_t keepAt;
        int r;
        Kt_Check(&ps1, &c);
        r = RsCharHires_SuggestColours(&c, RS_ROUTE_HIRES, &why);
        Check(c.vcolPresent == 1 && c.vcolPs1 == 1 && c.vcolMedian == 128 && c.vcolMode == RS_HIRES_VCOL_GLTF,
              "PS1-like kart: COLOR_0 there, HRG-36 said (median 128), checked in glTF");
        Check(r == RS_HIRES_VCOL_PSX && why == RS_VCOL_WHY_PS1, "PS1-like kart: PSX suggested (Hi-Res too)");
        n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
        Check(Kt_Has(m, n, RS_SEV_WARNING, RS_WIZ_PLACE_MODEL, L"HRG-36", NULL, L"Switch Vertex color blending on."),
              "PS1-like kart in glTF: the warning HRG-36 with its fix");
        keepAt = c.previewKeepAt;
        memcpy(before, c.preview + keepAt, sizeof(before));
        {
            const int glTFbyte = c.preview[16 + 6];
            Check(RsHires_SetColourMode(&c, RS_HIRES_VCOL_PSX) == 1 && RsHires_SetColourMode(&c, RS_HIRES_VCOL_PSX) == 0 &&
                      c.vcolMode == RS_HIRES_VCOL_PSX,
                  "RsHires_SetColourMode: 1 when the mode changes, 0 when it is that mode already");
            Check(c.preview && c.previewKeepAt == keepAt && memcmp(before, c.preview + keepAt, sizeof(before)) == 0,
                  "another mode: the textures and materials stay where and what they were (the view keeps them)");
            snprintf(line, sizeof(line), "PSX blend makes the PS1-like kart brighter than glTF (corner %d against %d)", c.preview[16 + 6], glTFbyte);
            Check(c.preview[16 + 6] > glTFbyte + 40, line);
        }
        n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
        Check(!Kt_HasRule(m, n, L"HRG-36"), "PS1-like kart in PSX blend: no HRG-36");
        Check(RsCharHires_SuggestColours(&c, RS_ROUTE_HIRES, &why) == RS_HIRES_VCOL_PSX && why == RS_VCOL_WHY_PS1,
              "the suggestion does not change with the mode drawn");
        RsHires_CheckFree(&c);

        Kt_Check(&white, &c);
        r = RsCharHires_SuggestColours(&c, RS_ROUTE_LOWPOLY, &why);
        Check(c.vcolPresent == 1 && c.vcolPs1 == 0 && r == RS_HIRES_VCOL_GLTF && why == RS_VCOL_WHY_WHITE,
              "white-ish COLOR_0 (sRGB 235): glTF suggested (Low-Poly too)");
        RsHires_SetColourMode(&c, RS_HIRES_VCOL_PSX);
        Check(RsCharHires_SuggestColours(&c, RS_ROUTE_LOWPOLY, &why) == RS_HIRES_VCOL_GLTF && why == RS_VCOL_WHY_WHITE,
              "white-ish COLOR_0 drawn in PSX blend: the suggestion stays glTF");
        RsHires_CheckFree(&c);

        Kt_Check(&none, &c);
        r = RsCharHires_SuggestColours(&c, RS_ROUTE_LOWPOLY, &why);
        Check(c.vcolPresent == 0 && r == -1 && why == RS_VCOL_WHY_NONE, "no COLOR_0: no switch (no suggestion)");
        RsHires_CheckFree(&c);

        Kt_Check(&flat, &c);
        Check(c.vcolPresent == 1 && c.vcolMedian == -1 && RsCharHires_SuggestColours(&c, RS_ROUTE_LOWPOLY, &why) == RS_HIRES_VCOL_PSX &&
                  why == RS_VCOL_WHY_ROUTE && RsCharHires_SuggestColours(&c, RS_ROUTE_HIRES, &why) == RS_HIRES_VCOL_GLTF,
              "COLOR_0 on untextured faces only: not decidable - Low-Poly PSX, Hi-Res glTF");
        RsHires_CheckFree(&c);
    }

    // 3. The line below the switch.
    {
        wchar_t text[RS_WIZ_TEXT_MAX];
        RsCharHires_ColourLine(RS_HIRES_VCOL_PSX, RS_HIRES_VCOL_PSX, RS_VCOL_WHY_PS1, RS_ROUTE_HIRES, text, RS_WIZ_TEXT_MAX);
        Check(wcscmp(text, L"Your vertex colours look like PS1 shading - blending is on.") == 0, "the line of a PS1-like model");
        RsCharHires_ColourLine(RS_HIRES_VCOL_OFF, RS_HIRES_VCOL_PSX, RS_VCOL_WHY_PS1, RS_ROUTE_HIRES, text, RS_WIZ_TEXT_MAX);
        Check(wcsstr(text, L"Your choice - suggested: on.") != NULL, "the author's other mode: said, and the suggestion");
        RsCharHires_ColourLine(RS_HIRES_VCOL_PSX, -1, RS_VCOL_WHY_NONE, RS_ROUTE_HIRES, text, RS_WIZ_TEXT_MAX);
        Check(text[0] == 0, "no COLOR_0: no line");
    }

    // 4. The pack step with the mode.
    {
        const wchar_t *a[RS_HIRES_PACK_ARGS];
        Check(RsHires_PackArgs(L"in.rldchar", L"model.glb", L"out.rldchar", RS_HIRES_VCOL_PSX, a) == 8 && wcscmp(a[5], L"--vertex-colors") == 0 &&
                  wcscmp(a[6], L"psx") == 0 && wcscmp(a[7], L"--machine") == 0,
              "the pack step with a mode: hires-add ... -o <out> --vertex-colors psx --machine");
        Check(wcscmp(RsHires_ColourWord(RS_HIRES_VCOL_GLTF), L"gltf") == 0 && wcscmp(RsHires_ColourWord(RS_HIRES_VCOL_OFF), L"off") == 0 &&
                  RsHires_ColourWord(3) == NULL && RsHires_ColourModeOf(L"psx") == RS_HIRES_VCOL_PSX && RsHires_ColourModeOf(L"on") == -1,
              "the words of the modes are rldpack's (gltf, psx, off)");
    }
}

static void Kt_SelfTestRoutes(const wchar_t *folder)
{
    static struct RsHiresCheck c;
    struct RsWizMsg m[96];
    wchar_t path[1024];
    int n, route;

    // THE PRESETS (decision E7): Hi-Res is the game's budget.
    {
        const struct RsRoutePreset *v = RsRoute_Preset(RS_ROUTE_VANILLA), *l = RsRoute_Preset(RS_ROUTE_LOWPOLY), *h = RsRoute_Preset(RS_ROUTE_HIRES);
        unsigned long long tris = 0, bytes = 0;
        unsigned edge = 0;
        RsHires_Budget(&tris, &edge, &bytes);
        Check(tris == 64000u && edge == 2048u && bytes == 32u * 1024u * 1024u, "the game's budget: 64000 triangles, edge 2048, 32 MiB");
        Check(v && v->chrg == 0 && v->triangleTarget == 0 && v->textureEdgeTarget == 0 && v->upscaleDefault == 0 && wcscmp(v->word, L"vanilla") == 0,
              "preset Vanilla: no CHRG, make-char's own limits, upscale off");
        Check(l && l->chrg == 1 && l->triangleTarget == 8000u && l->textureEdgeTarget == 512u && l->upscaleDefault == 0 && wcscmp(l->word, L"lowpoly") == 0,
              "preset Low-Poly: CHRG, 8000 triangles, edge 512, upscale off (suggested per model only)");
        Check(h && h->chrg == 1 && h->triangleTarget == tris && h->textureEdgeTarget == edge && h->upscaleDefault == 0 && wcscmp(h->word, L"hires") == 0,
              "preset Hi-Res: CHRG, the game's budget (RLDHIRES_BUDGET_*), upscale off");
        Check(RsRoute_Preset(RS_ROUTE_NONE) == NULL && RsRoute_Preset(RS_ROUTES) == NULL, "no preset outside the routes");
        Check(RsRoute_FromWord(L"vanilla") == RS_ROUTE_VANILLA && RsRoute_FromWord(L"lowpoly") == RS_ROUTE_LOWPOLY &&
                  RsRoute_FromWord(L"hires") == RS_ROUTE_HIRES && RsRoute_FromWord(L"none") == RS_ROUTE_NONE && RsRoute_FromWord(L"HiRes") == RS_ROUTE_NONE &&
                  RsRoute_FromWord(L"") == RS_ROUTE_NONE && RsRoute_FromWord(NULL) == RS_ROUTE_NONE,
              "RsRoute_FromWord: the three words, anything else none");
    }
    // THE FALLBACK'S SWITCHES.
    for (route = RS_ROUTE_VANILLA; route < RS_ROUTES; route++) {
        const wchar_t *a[RS_CHARHIRES_FALLBACK_ARGS] = { 0 };
        n = RsCharHires_FallbackArgs(route, a);
        if (route == RS_ROUTE_VANILLA)
            Check(n == 0, "the fallback switches: none for Vanilla");
        else
            Check(n == 4 && wcscmp(a[0], L"--reduce") == 0 && wcscmp(a[1], L"auto") == 0 && wcscmp(a[2], L"--rig") == 0 && wcscmp(a[3], L"bind-pose") == 0,
                  "the fallback switches of a CHRG route: --reduce auto --rig bind-pose");
    }

    // A5: no clip and no wheel node - exactly one line each, no line per clip.
    {
        struct KtSpec sp = { 2, 1, 0, 0, 16, 0 };
        Check(Kt_Check(&sp, &c) == 1 && c.usable == 1 && c.clips == 0 && c.wheels == 0, "bare kart (skinned, no clips, no wheels): usable");
        for (route = RS_ROUTE_LOWPOLY; route <= RS_ROUTE_HIRES; route++) {
            n = RsCharHires_Messages(&c, route, m, 96);
            Check(n == 2 && Kt_Count(m, n, RS_WIZ_PLACE_ANIMATIONS) == 1 && Kt_Count(m, n, RS_WIZ_PLACE_WHEELS) == 1 && Kt_ClipLines(m, n) == 0,
                  "A5 bare kart: exactly 1 line about animations, 1 about wheels, 0 lines per clip");
            Check(Kt_Has(m, n, RS_SEV_NOTE, RS_WIZ_PLACE_ANIMATIONS, L"HRG-19", KT_NO_CLIPS, NULL) &&
                      Kt_Has(m, n, RS_SEV_NOTE, RS_WIZ_PLACE_WHEELS, L"HRG-20", KT_NO_WHEELS, NULL) && Kt_WellFormed(m, n) && m[0].fix[0] && m[1].fix[0],
                  "A5 bare kart: the two lines word for word (E8), notes with their rule ID and a fix");
        }
        Check(RsCharHires_Messages(&c, RS_ROUTE_VANILLA, m, 96) == 0 && RsCharHires_Messages(&c, RS_ROUTE_NONE, m, 96) == 0 &&
                  RsCharHires_Messages(NULL, RS_ROUTE_HIRES, m, 96) == 0 && RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 0) == 0,
              "no CHRG messages for Vanilla, no route, no check or no room");
        Check(RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 1) == 1 && m[0].place == RS_WIZ_PLACE_ANIMATIONS, "cap: the first lines only");
        RsHires_CheckFree(&c);
    }
    // Some clips: one line, the per-clip rows stay in the check for Show details.
    {
        struct KtSpec sp = { 1, 1, 5, 4, 16, 0 };
        Check(Kt_Check(&sp, &c) == 1 && c.usable == 1 && c.clips == 5 && c.wheels == 4 && Kt_RowsOf(&c, L"HRG-19") == 7,
              "kart with 5 clips and 4 wheels: usable, the check keeps its 7 clip rows");
        n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
        Check(n == 1 && Kt_Has(m, n, RS_SEV_NOTE, RS_WIZ_PLACE_ANIMATIONS, L"HRG-19",
                               L"5 of 12 animations present - the others use their stand-ins. Optional.", NULL) &&
                  m[0].fix[0] && Kt_ClipLines(m, n) == 0,
              "A5 5 of 12 clips: one line, no line per clip, no wheel line");
        RsHires_CheckFree(&c);
    }
    // Every clip and wheel: nothing to say.
    {
        struct KtSpec sp = { 1, 1, 12, 4, 16, 0 };
        Check(Kt_Check(&sp, &c) == 1 && c.usable == 1 && c.rowCount == 0 && RsCharHires_Messages(&c, RS_ROUTE_LOWPOLY, m, 96) == 0,
              "full kart (12 clips, 4 wheels): no finding, no line");
        RsHires_CheckFree(&c);
    }
    // Two wheel nodes: one warning that names the missing ones.
    {
        struct KtSpec sp = { 1, 0, 12, 2, 0, 0 };
        Check(Kt_Check(&sp, &c) == 1 && c.usable == 1 && c.wheels == 2, "kart with 2 wheel nodes: usable");
        n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
        Check(n == 1 && m[0].severity == RS_SEV_WARNING && m[0].place == RS_WIZ_PLACE_WHEELS && wcscmp(m[0].rule, L"HRG-20") == 0 &&
                  wcsncmp(m[0].text, L"Only 2 of 4 wheel nodes usable", 30) == 0 && wcsstr(m[0].text, L"wheel_rl") && m[0].fix[0],
              "2 of 4 wheels: one warning at the wheels, the missing ones named, a fix");
        RsHires_CheckFree(&c);
    }
    // The Low-Poly targets: over 8000 triangles and an edge over 512 - a warning
    // each with the fix of E7; Hi-Res says nothing (within the game's budget).
    {
        struct KtSpec sp = { 19, 1, 12, 4, 0, 1024 };
        Check(Kt_Check(&sp, &c) == 1 && c.usable == 1 && c.triangles == 24u * 19u * 19u + 48u && c.textureEdge == 1024u,
              "big kart: 8712 triangles, edge 1024, usable");
        n = RsCharHires_Messages(&c, RS_ROUTE_LOWPOLY, m, 96);
        Check(n == 2 && Kt_Has(m, n, RS_SEV_WARNING, RS_WIZ_PLACE_SIZE, L"", L"The model has 8712 triangles - Low-Poly aims for 8000 or fewer.", KT_TARGET_FIX) &&
                  Kt_Has(m, n, RS_SEV_WARNING, RS_WIZ_PLACE_SIZE, L"", L"The largest texture is 1024 pixels across - Low-Poly aims for 512 or less.", KT_TARGET_FIX),
              "Low-Poly over its targets: two warnings with 'Use Hi-Res instead, or reduce the model in Blender'");
        Check(RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96) == 0, "the same kart on Hi-Res: nothing (within the game's budget)");
        RsHires_CheckFree(&c);
    }
    // Over the game's budget (HRG-27): an error as the game, first, with a fix.
    {
        struct KtSpec sp = { 52, 0, 12, 4, 16, 0 };
        Check(Kt_Check(&sp, &c) == 1 && c.usable == 0 && c.triangles == 24u * 52u * 52u + 48u && Kt_RowsOf(&c, L"HRG-27") == 1,
              "huge kart: 64944 triangles, refused by the budget (HRG-27)");
        for (route = RS_ROUTE_LOWPOLY; route <= RS_ROUTE_HIRES; route++) {
            n = RsCharHires_Messages(&c, route, m, 96);
            Check(n == 1 && m[0].severity == RS_SEV_ERROR && m[0].place == RS_WIZ_PLACE_SIZE && wcscmp(m[0].rule, L"HRG-27") == 0 &&
                      wcscmp(m[0].text, L"Over budget: 64944 triangles, the limit is 64000.") == 0 && m[0].fix[0],
                  "over the budget: one error HRG-27 at the size, a fix, no Low-Poly target line besides");
        }
        RsHires_CheckFree(&c);
    }
    // A file of rldpack's self-test: a JPEG texture (HRG-16), the check's names kept.
    swprintf(path, 1024, L"%ls\\hires-bad.glb", folder);
    Check(RsHires_CheckFile(path, &c) == 1, "hires-bad.glb is read");
    n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
    Check(n >= 1 && m[0].severity == RS_SEV_ERROR && wcscmp(m[0].rule, L"HRG-16") == 0 && m[0].place == RS_WIZ_PLACE_MODEL &&
              wcscmp(m[0].text, L"Texture 0 ('skin') is JPEG.") == 0 && wcsstr(m[0].fix, L"PNG") && Kt_WellFormed(m, n),
          "hires-bad.glb: HRG-16 first, 'Texture 0 ('skin') is JPEG.' with the PNG fix");
    // Its base colour texture is a JPEG: its size is not read, so it is not
    // known to be small - no upscale suggested.
    Check(c.baseTextures == 1 && c.baseUnsized == 1 && c.baseEdgeFile == 0u && RsCharHires_SuggestUpscale(&c, RS_ROUTE_LOWPOLY, NULL) == 0,
          "hires-bad.glb: a base colour texture of unknown size (JPEG) - upscale not suggested");
    RsHires_CheckFree(&c);
    // A file that is not there: one error with a fix.
    swprintf(path, 1024, L"%ls\\no-such-file.glb", folder);
    RsHires_CheckFile(path, &c);
    n = RsCharHires_Messages(&c, RS_ROUTE_LOWPOLY, m, 96);
    Check(n == 1 && m[0].severity == RS_SEV_ERROR && m[0].place == RS_WIZ_PLACE_MODEL && m[0].text[0] && m[0].fix[0],
          "a model that cannot be read: one error with a fix");
    RsHires_CheckFree(&c);

    // EVERY RULE has its sentence and fix: a check with one row of each rule
    // (HRG-19 and HRG-20 aside, they are folded), as errors, a second HRG-16
    // folded into the first, two HRG-27 kept apart.
    {
        int rule, own = 0, lines = 0, fixes = 0, kept = 0;
        memset(&c, 0, sizeof(c));
        c.read = 1;
        c.clips = RS_HIRES_CLIPS;
        c.wheels = 4;
        c.budgetTriangles = 64000u;
        c.budgetEdge = 2048u;
        for (rule = 1; rule <= 36 && c.rowCount < RS_HIRES_ROWS_MAX; rule++) {
            if (rule == 19 || rule == 20)
                continue;
            c.rows[c.rowCount].severity = RS_SEV_ERROR;
            swprintf(c.rows[c.rowCount].rule, 8, L"HRG-%d", rule);
            swprintf(c.rows[c.rowCount].text, RS_HIRES_TEXT_MAX, L"finding of rule %d - what the check says to do", rule);
            c.rowCount++;
        }
        c.rows[c.rowCount].severity = c.rows[c.rowCount + 1].severity = RS_SEV_ERROR;
        wcscpy(c.rows[c.rowCount].rule, L"HRG-16");
        wcscpy(c.rows[c.rowCount++].text, L"texture 3 ('decal') is JPEG - export textures as PNG");
        wcscpy(c.rows[c.rowCount].rule, L"HRG-27");
        wcscpy(c.rows[c.rowCount++].text, L"over budget: a texture edge of 4096, the limit is 2048");
        n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
        for (rule = 0; rule < n; rule++) {
            lines += m[rule].severity == RS_SEV_ERROR;
            fixes += m[rule].fix[0] != 0 && wcsncmp(m[rule].fix, L"Open Show details", 17) != 0;
            own += wcsstr(m[rule].text, L"inding of rule") == NULL && wcsstr(m[rule].text, L"ver budget") == NULL;
            kept += wcsncmp(m[rule].rule, L"HRG-", 4) == 0;
        }
        // HRG-36 (PS1-like vertex colours) is a warning in glTF mode (vcolMode 0 here), whatever the row says.
        Check(n == 35 && lines == 34 && fixes == 35 && kept == 35, "every rule HRG-1..36 but the folded 19 and 20: a line with its rule ID and its own fix");
        Check(Kt_Has(m, n, RS_SEV_WARNING, RS_WIZ_PLACE_MODEL, L"HRG-36", NULL, L"Switch Vertex color blending on."),
              "HRG-36 in glTF mode: a warning, the fix switches Vertex color blending on");
        Check(Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_MODEL, L"HRG-16", L"Finding of rule 16 (and 1 more).", NULL) &&
                  Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_SIZE, L"HRG-27", L"Finding of rule 27.", NULL) &&
                  Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_SIZE, L"HRG-27", L"Over budget: a texture edge of 4096, the limit is 2048.", NULL),
              "one rule twice is one line (and 1 more); the budget keeps a line per limit");
        Check(own == 6 && Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_MODEL, L"HRG-12", L"The model has more than one armature.", NULL),
              "the container rules HRG-1..4, HRG-12 and HRG-36 say the Studio's own sentence");
        c.vcolMode = RS_HIRES_VCOL_PSX;
        n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
        Check(n == 34 && !Kt_HasRule(m, n, L"HRG-36"), "HRG-36 in PSX blend: no line");
        c.vcolMode = RS_HIRES_VCOL_OFF;
        n = RsCharHires_Messages(&c, RS_ROUTE_HIRES, m, 96);
        Check(n == 34 && !Kt_HasRule(m, n, L"HRG-36"), "HRG-36 with the vertex colours off: no line");
        Check(Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_WHEELS, L"HRG-31", NULL, NULL) && Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_ANIMATIONS, L"HRG-21", NULL, NULL) &&
                  Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_SIZE, L"HRG-17", NULL, NULL) && Kt_Has(m, n, RS_SEV_ERROR, RS_WIZ_PLACE_SIZE, L"HRG-35", NULL, NULL),
              "the places: wheel details at the wheels, clip names at the animations, textures and limits at the size");
        // A row of a rule the Studio does not know: said as the check says it, with the general fix.
        memset(&c, 0, sizeof(c));
        c.read = 1;
        c.clips = RS_HIRES_CLIPS;
        c.wheels = 4;
        c.rowCount = 1;
        c.rows[0].severity = RS_SEV_WARNING;
        wcscpy(c.rows[0].rule, L"HRG-99");
        wcscpy(c.rows[0].text, L"something new - do this");
        n = RsCharHires_Messages(&c, RS_ROUTE_LOWPOLY, m, 96);
        Check(n == 1 && wcscmp(m[0].text, L"Something new.") == 0 && m[0].fix[0] && wcscmp(m[0].rule, L"HRG-99") == 0,
              "an unknown rule: its own text, its rule ID, the general fix");
    }
}

// THE UPSCALE (rs_charroute.h): "Upscale small textures" is suggested only
// when every base colour texture is under 64 px on its larger edge, measured
// in the file before the round-up; the same rule on Low-Poly and Hi-Res,
// never on Vanilla. Karts made here, a real PNG of the edge given.
static void Kt_SelfTestUpscale(void)
{
    static struct RsHiresCheck c;
    static const struct {
        int edge;           // the PNG's edge, 0 = no texture (a plain colour)
        int suggested;
        const char *what;
    } cases[] = {
        { 16, 1, "16 x 16 texture: upscale suggested on" },
        { 40, 1, "40 x 40 texture (64 after the round-up): suggested on - the edge before the round-up counts" },
        { 63, 1, "63 x 63 texture: suggested on" },
        { 64, 0, "64 x 64 texture: not suggested (off)" },
        { 0, 0, "no texture: not suggested (off)" },
    };
    wchar_t line[RS_WIZ_TEXT_MAX];
    unsigned largest = 99u;
    int i;

    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        struct KtSpec sp = { 2, 1, 0, 0, cases[i].edge, 0 };
        int ok = Kt_Check(&sp, &c) == 1 && c.read == 1;
        ok = ok && c.baseTextures == (cases[i].edge ? 1 : 0) && c.baseUnsized == 0 && c.baseEdgeFile == (unsigned)cases[i].edge;
        ok = ok && RsCharHires_SuggestUpscale(&c, RS_ROUTE_LOWPOLY, &largest) == cases[i].suggested && largest == (unsigned)cases[i].edge;
        ok = ok && RsCharHires_SuggestUpscale(&c, RS_ROUTE_HIRES, NULL) == cases[i].suggested;
        ok = ok && RsCharHires_SuggestUpscale(&c, RS_ROUTE_VANILLA, NULL) == 0;
        if (cases[i].edge == 40)
            ok = ok && c.textureEdge == 64u;
        Check(ok, cases[i].what);
        RsHires_CheckFree(&c);
    }
    Check(RsCharHires_SuggestUpscale(NULL, RS_ROUTE_LOWPOLY, &largest) == 0 && largest == 0u, "no check: not suggested");

    // The line below the box.
    RsCharHires_UpscaleLine(1, 1, 16u, line, RS_WIZ_TEXT_MAX);
    Check(wcscmp(line, L"Your textures are small (largest 16 px) - Upscale small textures is on.") == 0, "the line of the suggestion");
    RsCharHires_UpscaleLine(0, 1, 16u, line, RS_WIZ_TEXT_MAX);
    Check(wcsstr(line, L"is off") != NULL && wcsstr(line, L"Your choice - suggested: on") != NULL, "the line of the author's choice against it (off)");
    RsCharHires_UpscaleLine(1, 0, 512u, line, RS_WIZ_TEXT_MAX);
    Check(wcsstr(line, L"is on") != NULL && wcsstr(line, L"Your choice - suggested: off") != NULL, "the line of the author's choice against it (on)");
    RsCharHires_UpscaleLine(0, 0, 512u, line, RS_WIZ_TEXT_MAX);
    Check(line[0] == 0, "off and not suggested: no line");
}

// --write <folder>: the karts of the byte test of the routes
// (cmake/run_rs_charroute.cmake) and the fallback's switches, a CMake list per
// CHRG route (fallback-lowpoly.txt, fallback-hires.txt).
static int Kt_Write(const wchar_t *folder)
{
    static const struct {
        const wchar_t *name;
        struct KtSpec sp;
    } karts[] = {
        { L"route-bare.glb", { 2, 1, 0, 0, 16, 0 } },     // skinned, no clips, no wheels, a 16 x 16 texture (the page suggests the upscale)
        { L"route-tex64.glb", { 2, 1, 0, 0, 64, 0 } },    // as route-bare with a 64 x 64 texture: no upscale suggested
        { L"route-wheels.glb", { 2, 1, 12, 4, 16, 0 } },  // skinned, 12 clips, its own 4 wheels (the page adds --wheels off)
        { L"route-vcol.glb", { 2, 1, 0, 0, 16, 0, 128 } }, // as route-bare, with PS1-like COLOR_0 (grey 128): the page suggests PSX
    };
    int i, route, ok = 1;
    for (i = 0; i < (int)(sizeof(karts) / sizeof(karts[0])); i++) {
        wchar_t path[1024];
        size_t size = 0;
        unsigned char *glb = Kt_Glb(&karts[i].sp, &size);
        FILE *f;
        swprintf(path, 1024, L"%ls\\%ls", folder, karts[i].name);
        f = _wfopen(path, L"wb");
        ok &= f != NULL && fwrite(glb, 1, size, f) == size;
        if (f)
            fclose(f);
        free(glb);
    }
    for (route = RS_ROUTE_LOWPOLY; route <= RS_ROUTE_HIRES; route++) {
        const wchar_t *a[RS_CHARHIRES_FALLBACK_ARGS];
        wchar_t path[1024];
        FILE *f;
        int n = RsCharHires_FallbackArgs(route, a), k;
        swprintf(path, 1024, L"%ls\\fallback-%ls.txt", folder, RsRoute_Preset(route)->word);
        f = _wfopen(path, L"wb");
        ok &= f != NULL;
        for (k = 0; f && k < n; k++)
            fprintf(f, "%s%ls", k ? ";" : "", a[k]);
        if (f)
            fclose(f);
    }
    printf("rs_hires_selftest --write: %s\n", ok ? "written" : "FAILED");
    return !ok;
}

// --messages <route> <file>: the lines a CHRG route shows for a model, for a
// look by hand (the self-test does not use it).
static int Kt_Messages(const wchar_t *word, const wchar_t *file)
{
    static const char *const places[] = { "model", "animations", "wheels", "size", "build" };
    static const char *const sev[] = { "ok", "info", "note", "warning", "error" };
    static struct RsHiresCheck c;
    struct RsWizMsg m[96];
    int route = RsRoute_FromWord(word), n, i;
    RsHires_CheckFile(file, &c);
    n = RsCharHires_Messages(&c, route, m, 96);
    printf("%ls: check rows %d (errors %d, warnings %d, notes %d), clips %d/12, wheels %d/4, %llu triangles, edge %u\n", file, c.rowCount, c.errors,
           c.warnings, c.notes, c.clips, c.wheels, c.triangles, c.textureEdge);
    printf("route %ls: %d line(s)\n", word, n);
    for (i = 0; i < n; i++)
        printf("  [%s] %-10s %-7ls %ls\n      How to fix: %ls\n", sev[m[i].severity], places[m[i].place], m[i].rule[0] ? m[i].rule : L"-", m[i].text,
               m[i].fix[0] ? m[i].fix : L"-");
    RsHires_CheckFree(&c);
    return route == RS_ROUTE_NONE;
}

int wmain(int argc, wchar_t **argv)
{
    struct RsHiresCheck c;
    wchar_t path[1024];
    const wchar_t *args[RS_HIRES_PACK_ARGS];

    if (argc == 3 && wcscmp(argv[1], L"--write") == 0)
        return Kt_Write(argv[2]);
    if (argc == 4 && wcscmp(argv[1], L"--messages") == 0)
        return Kt_Messages(argv[2], argv[3]);
    if (argc != 2) {
        printf("usage: rs_hires_selftest <folder with hires-good.glb, hires-warn.glb, hires-bad.glb, packer.rldchar>\n");
        printf("       rs_hires_selftest --write <folder>   (the karts of cmake/run_rs_charroute.cmake)\n");
        printf("       rs_hires_selftest --messages lowpoly|hires <model>   (the lines of a route, by hand)\n");
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
        // COLOR_0 at sRGB 128 in glTF mode: the factor 0.2159, on the display
        // curve 128/255, x the light of a +Z normal (1.0346): 132.
        const float lightZ = RsHires_Light((const float[3]){ 0.0f, 0.0f, 1.0f });
        const int want = Kt_Expect(RS_HIRES_VCOL_GLTF, RldHiresColor_Decode(128.0f / 255.0f), 255u, lightZ);
        Check(lightZ > 1.034f && lightZ < 1.035f, "the engine's light on a +Z normal: ambient 0.875 + sun 0.40 x 0.399 = 1.0346");
        Check(abs(flat[6] - want) <= 2 && flat[7] == 0 && abs(flat[16] - want) <= 2 && flat[17] == 0 && abs(flat[26] - want) <= 2 && flat[27] == 0 &&
                  want == 132,
              "flat picture: the three corners of the body are red (texel 255 x 128/255 x 1.0346 = 132, within 2), no green");
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
            Check(k0.r == 64 && k0.g == 64 && k0.b == 64 && k0.a == 255 && k0.l == 132,
                  "COLOR_0 as the engine applies it: linear 0.2159 -> display curve 128/255 = C 64 of 128, the light 1.0346 = 132 of 128");
            Check(nat.mat[RsTex_NativeMaterialOf(&nat, 0)].lit == 1 && (nat.flags & RS_TEX_NATIVE_LIT) && nat.light != NULL,
                  "textured model: a LIT material with a light per corner (RS_TEX_MATERIAL_LIT, RS_TEX_NATIVE_LIT)");
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
    Check(c.previewTriangles == 1 + 4 * 80, "hires-warn.glb: the picture has the body and the engine's 4 standard wheels of 80 triangles (not the found ones)");
    if (c.preview && c.previewTriangles == 321) {
        // The first standard wheel's first corner (a tread vertex, grey 38 shown
        // as the display value) in glTF mode, lit: no texel, the white default material.
        const unsigned char *t = c.preview + 16 + 30;   // after the body triangle (3 records of 10 bytes)
        float n[3] = { 0.0f, 1.0f, 0.0f };
        const int want = (int)(38.0f / 255.0f * RsHires_Light(n) * 255.0f + 0.5f);
        char what[160];
        snprintf(what, sizeof(what), "a standard wheel's tread: its byte 38 as the display value, lit in glTF (%d, want %d)", t[6], want);
        Check(abs(t[6] - want) <= 2 && t[6] == t[7] && t[7] == t[8] && t[9] == 1, what);
        Check(RsHires_SetColourMode(&c, RS_HIRES_VCOL_PSX) == 1 && c.preview[16 + 30 + 6] == 38,
              "a standard wheel in PSX blend: unlit, its byte 38 as it is");
    }
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

    Check(RsHires_PackArgs(L"in.rldchar", L"model.glb", L"out.rldchar", -1, args) == 6 && wcscmp(args[0], L"hires-add") == 0 &&
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
              c.errors == 0 && c.warnings == 1,
          "hires-tex-upscaled.glb (the Studio's upscale step): usable, no error, one warning (HRG-36: its COLOR_0 128 read in glTF mode), its texture 64 x 64, textured preview");
    RsHires_CheckFree(&c);

    Check(wcscmp(RsHires_ClipName(0), L"idle") == 0 && wcscmp(RsHires_ClipName(11), L"menu") == 0 && wcscmp(RsHires_ClipName(12), L"?") == 0,
          "clip names");

    Kt_SelfTestRoutes(argv[1]);
    Kt_SelfTestColours();
    Kt_SelfTestUpscale();

    printf("rs_hires selftest %s: %d checks, %d failures\n", s_failures ? "FAILED" : "passed", s_checks, s_failures);
    return s_failures != 0;
}
