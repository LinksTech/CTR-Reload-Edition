// rs_tex.c - the native model's textures and colours for the 3D preview
//
// See rs_tex.h. Reads the blocks rldpack writes for the view (RLDPN3, or the
// older RLDPN2, behind the classic data of make-char --preview, RLDPW3 of
// char-wheel --preview),
// checks every length and count before it is used, and builds the mip levels
// of every texture with the game's code (include/rldmip.inc). Plain C: no
// Win32, no view, no float - the view draws, this file only answers.

#include "rs_tex.h"

#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#include <rldmip.inc>

#define RS_TEX_MAGIC_BYTES 8
#define RS_TEX_TEXTURE_HEAD 12          // u32 width, height, flags
#define RS_TEX_MATERIAL_BYTES 8         // u8 r, g, b, a, s16 texture, u8 alpha mode, u8 flags
#define RS_TEX_WHEEL_HEAD 20            // magic, width, height, flags
#define RS_TEX_FLAG_LINEAR 0x1u         // CTXT flags (RLDCHAR_TEX_*, include/rldchar.inc)
#define RS_TEX_WRAP_U_SHIFT 1
#define RS_TEX_WRAP_V_SHIFT 3

static const unsigned char s_rsTexNativeMagic[RS_TEX_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'N', '2', 0, 0 };
static const unsigned char s_rsTexNativeMagic3[RS_TEX_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'N', '3', 0, 0 };
static const unsigned char s_rsTexWheelMagic[RS_TEX_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'W', '3', 0, 0 };

static unsigned int RsTex_U32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int RsTex_S16(const unsigned char *p)
{
    const int v = (int)p[0] | ((int)p[1] << 8);
    return v >= 32768 ? v - 65536 : v;
}

static int RsTex_S32(const unsigned char *p)
{
    return (int)RsTex_U32(p);
}

// log2 of a power of two in min..max, -1 for anything else.
static int RsTex_Shift(unsigned int edge, unsigned int min, unsigned int max)
{
    int s = 0;
    if (edge < min || edge > max || (edge & (edge - 1u)) != 0u)
        return -1;
    while ((1u << s) < edge)
        s++;
    return s;
}

static void RsTex_FreeImage(struct RsTexImage *img)
{
    free(img->chain);
    memset(img, 0, sizeof(*img));
}

// Fills img for the texture w x h with CTXT flags at rgba (level 0) and builds
// levels 1.. with rldmip.inc: the bytes RldMip_BuildLevels gives, level 0
// left where it is. 0 = out of memory.
static int RsTex_Image(struct RsTexImage *img, unsigned int w, unsigned int h, unsigned int flags, const unsigned char *rgba)
{
    const int levels = RldMip_LevelCount((int)w, (int)h);
    const size_t level0 = (size_t)w * h * 4;
    const size_t total = RldMip_ChainBytes((int)w, (int)h, levels);
    size_t at = 0;
    int l;

    memset(img, 0, sizeof(*img));
    img->w = (int)w;
    img->h = (int)h;
    img->flags = flags;
    img->linear = (flags & RS_TEX_FLAG_LINEAR) != 0u;
    img->wrapU = (int)((flags >> RS_TEX_WRAP_U_SHIFT) & 3u);
    img->wrapV = (int)((flags >> RS_TEX_WRAP_V_SHIFT) & 3u);
    img->levels = levels;
    img->chainBytes = total;
    img->level[0] = rgba;
    img->shiftW[0] = RsTex_Shift(w, 1u, 1u << 30);
    img->shiftH[0] = RsTex_Shift(h, 1u, 1u << 30);
    if (levels > 1) {
        img->chain = (unsigned char *)malloc(total - level0);
        if (!img->chain) {
            memset(img, 0, sizeof(*img));
            return 0;
        }
    }
    for (l = 1; l < levels; l++) {
        const int sw = RldMip_LevelEdge((int)w, l - 1), sh = RldMip_LevelEdge((int)h, l - 1);
        img->level[l] = img->chain + at;
        img->shiftW[l] = img->shiftW[0] - l > 0 ? img->shiftW[0] - l : 0;
        img->shiftH[l] = img->shiftH[0] - l > 0 ? img->shiftH[0] - l : 0;
        RldMip_Halve(img->level[l - 1], sw, sh, img->linear, img->chain + at);
        at += (size_t)RldMip_LevelEdge((int)w, l) * (size_t)RldMip_LevelEdge((int)h, l) * 4;
    }
    return 1;
}

void RsTex_FreeNative(struct RsTexNative *n)
{
    int t;
    if (!n)
        return;
    for (t = 0; t < RS_TEX_TEXTURES_MAX; t++)
        RsTex_FreeImage(&n->tex[t]);
    memset(n, 0, sizeof(*n));
}

void RsTex_FreeWheel(struct RsTexWheel *w)
{
    if (!w)
        return;
    RsTex_FreeImage(&w->tex);
    memset(w, 0, sizeof(*w));
}

// A material of 8 bytes; 0 = a texture index or alpha mode out of range.
static int RsTex_Material(const unsigned char *p, int textures, struct RsTexMaterial *m)
{
    memcpy(m->rgba, p, 4);
    m->texture = RsTex_S16(p + 4);
    m->alphaMode = p[6];
    m->nearest = (p[7] & 1u) != 0u;
    return m->texture >= -1 && m->texture < textures && m->alphaMode <= RS_TEX_ALPHA_BLEND;
}

// The geometry of RLDPN3 from at (behind the materials): vertices,
// triangles, the poses, the end poses, then per vertex u, v, COL0 and per
// triangle its three vertices and its material - to the end of the file.
// 0 = refused (why set); out keeps its textures for the caller to free.
static int RsTex_ParseNative3(const unsigned char *data, size_t bytes, size_t at, struct RsTexNative *out, wchar_t *why, int whyCap)
{
    unsigned int vertices, tris, poses, neutral, morphs, t;
    size_t perVertex;
    int p, e;

    if (bytes - at < 20) {
        swprintf(why, (size_t)whyCap, L"the native model ends before its triangles");
        return 0;
    }
    vertices = RsTex_U32(data + at);
    tris = RsTex_U32(data + at + 4);
    poses = RsTex_U32(data + at + 8);
    neutral = RsTex_U32(data + at + 12);
    morphs = RsTex_U32(data + at + 16);
    at += 20;
    if (vertices < 3 || vertices > RS_TEX_VERTICES_MAX || tris < 1 || tris > RS_TEX_TRIANGLES_MAX) {
        swprintf(why, (size_t)whyCap, L"the native model claims %u corners and %u triangles", vertices, tris);
        return 0;
    }
    if (!((poses == RS_TEX_FRAMES && neutral == RS_TEX_NEUTRAL_FRAME) || (poses == 1 && neutral == 0)) || morphs > 3u) {
        swprintf(why, (size_t)whyCap, L"the native model claims %u poses with %u as neutral and end poses 0x%x", poses, neutral, morphs);
        return 0;
    }
    // Every size below is at most 47 + 2 poses of 65535 corners: no wrap.
    perVertex = (size_t)(poses + (morphs & 1u) + (morphs >> 1)) * RS_TEX_NATIVE_POS3_BYTES + RS_TEX_NATIVE_VERTEX_BYTES;
    if (bytes - at != (size_t)vertices * perVertex + (size_t)tris * RS_TEX_NATIVE_INDEX_BYTES) {
        swprintf(why, (size_t)whyCap, L"the native model of %u corners and %u triangles is %u bytes long", vertices, tris, (unsigned)(bytes - at));
        return 0;
    }
    for (p = 0; p < (int)poses; p++) {
        out->pos[p] = data + at;
        at += (size_t)vertices * RS_TEX_NATIVE_POS3_BYTES;
    }
    for (e = 0; e < 2; e++)
        if (morphs & (1u << e)) {
            out->end[e] = data + at;
            at += (size_t)vertices * RS_TEX_NATIVE_POS3_BYTES;
        }
    out->vtx = data + at;
    at += (size_t)vertices * RS_TEX_NATIVE_VERTEX_BYTES;
    out->idx = data + at;
    for (t = 0; t < tris; t++) {
        const unsigned char *q = out->idx + (size_t)t * RS_TEX_NATIVE_INDEX_BYTES;
        const unsigned int a = (unsigned int)q[0] | ((unsigned int)q[1] << 8), b = (unsigned int)q[2] | ((unsigned int)q[3] << 8);
        const unsigned int c = (unsigned int)q[4] | ((unsigned int)q[5] << 8), m = (unsigned int)q[6] | ((unsigned int)q[7] << 8);
        if (a >= vertices || b >= vertices || c >= vertices || m >= (unsigned int)out->materialCount) {
            swprintf(why, (size_t)whyCap, L"triangle %u of the native model names corners %u, %u, %u and material %u", t, a, b, c, m);
            return 0;
        }
    }
    out->indexed = 1;
    out->vertices = (int)vertices;
    out->triangles = (int)tris;
    out->poses = (int)poses;
    out->neutral = (int)neutral;
    return 1;
}

int RsTex_ParseNative(const unsigned char *data, size_t bytes, size_t at, struct RsTexNative *out, wchar_t *why, int whyCap)
{
    unsigned int count, materials, tris, poses, t;
    size_t budget = 0;
    int p, three;

    memset(out, 0, sizeof(*out));
    three = at <= bytes && bytes - at >= RS_TEX_MAGIC_BYTES + 8 && memcmp(data + at, s_rsTexNativeMagic3, RS_TEX_MAGIC_BYTES) == 0;
    if (!three && (at > bytes || bytes - at < RS_TEX_MAGIC_BYTES + 8 || memcmp(data + at, s_rsTexNativeMagic, RS_TEX_MAGIC_BYTES) != 0)) {
        if (at <= bytes && bytes - at >= RS_TEX_MAGIC_BYTES && memcmp(data + at, "RLDPN", 5) == 0)
            swprintf(why, (size_t)whyCap, L"the native model is in another format (%.6hs) - make the preview again with this version",
                     (const char *)(data + at));
        else
            swprintf(why, (size_t)whyCap, L"%u bytes follow after the last pose", (unsigned)(bytes - at));
        return 0;
    }
    at += RS_TEX_MAGIC_BYTES;
    out->flags = RsTex_U32(data + at);
    count = RsTex_U32(data + at + 4);
    at += 8;
    if ((out->flags & ~RS_TEX_NATIVE_BLEND) != 0u) {
        swprintf(why, (size_t)whyCap, L"the native model has flags 0x%x this version does not know", out->flags);
        goto refused;
    }
    if (count > RS_TEX_TEXTURES_MAX) {
        swprintf(why, (size_t)whyCap, L"the native model has %u textures - at most %d", count, RS_TEX_TEXTURES_MAX);
        goto refused;
    }
    for (t = 0; t < count; t++) {
        unsigned int w, h, flags;
        if (bytes - at < RS_TEX_TEXTURE_HEAD) {
            swprintf(why, (size_t)whyCap, L"the native model ends in texture %u", t);
            goto refused;
        }
        w = RsTex_U32(data + at);
        h = RsTex_U32(data + at + 4);
        flags = RsTex_U32(data + at + 8);
        if (RsTex_Shift(w, RS_TEX_EDGE_MIN, RS_TEX_EDGE_MAX) < 0 || RsTex_Shift(h, RS_TEX_EDGE_MIN, RS_TEX_EDGE_MAX) < 0) {
            swprintf(why, (size_t)whyCap, L"texture %u of the native model is %u x %u - each side a power of two from %d to %d", t, w, h,
                     RS_TEX_EDGE_MIN, RS_TEX_EDGE_MAX);
            goto refused;
        }
        if (((flags >> RS_TEX_WRAP_U_SHIFT) & 3u) == 3u || ((flags >> RS_TEX_WRAP_V_SHIFT) & 3u) == 3u) {
            swprintf(why, (size_t)whyCap, L"texture %u of the native model has an unknown wrap (flags 0x%x)", t, flags);
            goto refused;
        }
        if ((size_t)w * h * 4 > bytes - at - RS_TEX_TEXTURE_HEAD) {
            swprintf(why, (size_t)whyCap, L"the native model ends in texture %u (%u x %u)", t, w, h);
            goto refused;
        }
        // The game's budget (CTXT-6), counted before anything is allocated.
        budget += RldMip_ChainBytes((int)w, (int)h, RldMip_LevelCount((int)w, (int)h));
        if (budget > RS_TEX_BUDGET) {
            swprintf(why, (size_t)whyCap, L"the textures of the native model need %u KiB with their mip levels - the game takes at most %u KiB",
                     (unsigned)(budget / 1024u), (unsigned)(RS_TEX_BUDGET / 1024u));
            goto refused;
        }
        if (!RsTex_Image(&out->tex[t], w, h, flags, data + at + RS_TEX_TEXTURE_HEAD)) {
            swprintf(why, (size_t)whyCap, L"there is no memory for the mip levels of texture %u (%u x %u)", t, w, h);
            goto refused;
        }
        out->textureCount = (int)t + 1;
        at += RS_TEX_TEXTURE_HEAD + (size_t)w * h * 4;
    }
    out->bytes = budget;

    if (bytes - at < 4) {
        swprintf(why, (size_t)whyCap, L"the native model ends before its materials");
        goto refused;
    }
    materials = RsTex_U32(data + at);
    at += 4;
    if (materials < 1 || materials > RS_TEX_MATERIALS_MAX || (size_t)materials * RS_TEX_MATERIAL_BYTES > bytes - at) {
        swprintf(why, (size_t)whyCap, L"the native model claims %u materials - 1 to %d, inside the file", materials, RS_TEX_MATERIALS_MAX);
        goto refused;
    }
    for (t = 0; t < materials; t++) {
        if (!RsTex_Material(data + at, (int)count, &out->mat[t])) {
            swprintf(why, (size_t)whyCap, L"material %u of the native model names texture %d or alpha mode %u", t, RsTex_S16(data + at + 4),
                     (unsigned)data[at + 6]);
            goto refused;
        }
        if (out->mat[t].alphaMode == RS_TEX_ALPHA_BLEND && !(out->flags & RS_TEX_NATIVE_BLEND)) {
            swprintf(why, (size_t)whyCap, L"material %u of the native model blends, but the file does not say so", t);
            goto refused;
        }
        at += RS_TEX_MATERIAL_BYTES;
    }
    out->materialCount = (int)materials;
    if (three) {
        if (RsTex_ParseNative3(data, bytes, at, out, why, whyCap))
            return 1;
        goto refused;
    }

    if (bytes - at < 8) {
        swprintf(why, (size_t)whyCap, L"the native model ends before its triangles");
        goto refused;
    }
    tris = RsTex_U32(data + at);
    poses = RsTex_U32(data + at + 4);
    at += 8;
    // Compared by division: a damaged count could wrap a product.
    if (poses != RS_TEX_POSES || tris > RS_TEX_TRIANGLES_MAX ||
        tris > (bytes - at) / (RS_TEX_POSES * RS_TEX_NATIVE_POS_BYTES + RS_TEX_NATIVE_TRI_BYTES)) {
        swprintf(why, (size_t)whyCap, L"the native model claims %u triangles in %u poses", tris, poses);
        goto refused;
    }
    for (p = 0; p < RS_TEX_POSES; p++) {
        out->pos[p] = data + at;
        at += (size_t)tris * RS_TEX_NATIVE_POS_BYTES;
    }
    out->tris = data + at;
    at += (size_t)tris * RS_TEX_NATIVE_TRI_BYTES;
    if (at != bytes) {
        swprintf(why, (size_t)whyCap, L"%u bytes follow after the native model", (unsigned)(bytes - at));
        goto refused;
    }
    for (t = 0; t < tris; t++) {
        const unsigned int m = (unsigned int)out->tris[(size_t)t * RS_TEX_NATIVE_TRI_BYTES + 36] |
                               ((unsigned int)out->tris[(size_t)t * RS_TEX_NATIVE_TRI_BYTES + 37] << 8);
        if (m >= materials) {
            swprintf(why, (size_t)whyCap, L"triangle %u of the native model names material %u", t, m);
            goto refused;
        }
    }
    out->triangles = (int)tris;
    out->vertices = 3 * (int)tris;
    out->poses = RS_TEX_POSES;
    out->neutral = 0;   // the turn frames 10, 0, 20
    return 1;

refused:
    RsTex_FreeNative(out);
    return 0;
}

int RsTex_ParseWheel(const unsigned char *data, size_t bytes, struct RsTexWheel *out, wchar_t *why, int whyCap)
{
    size_t at = RS_TEX_WHEEL_HEAD;
    unsigned int tw, th, flags, count;

    memset(out, 0, sizeof(*out));
    if (bytes < RS_TEX_WHEEL_HEAD || memcmp(data, s_rsTexWheelMagic, RS_TEX_MAGIC_BYTES) != 0) {
        if (bytes >= RS_TEX_MAGIC_BYTES && memcmp(data, "RLDPW", 5) == 0)
            swprintf(why, (size_t)whyCap, L"the wheel model is in another format (%.6hs) - read it again with this version", (const char *)data);
        else
            swprintf(why, (size_t)whyCap, L"it is not a wheel model (it does not start with RLDPW3)");
        return 0;
    }
    tw = RsTex_U32(data + 8);
    th = RsTex_U32(data + 12);
    flags = RsTex_U32(data + 16);
    if (tw != 0 || th != 0) {
        if (RsTex_Shift(tw, RS_TEX_EDGE_MIN, RS_TEX_WHEEL_EDGE_MAX) < 0 || RsTex_Shift(th, RS_TEX_EDGE_MIN, RS_TEX_WHEEL_EDGE_MAX) < 0) {
            swprintf(why, (size_t)whyCap, L"its texture is %u x %u - each side a power of two from %d to %d", tw, th, RS_TEX_EDGE_MIN,
                     RS_TEX_WHEEL_EDGE_MAX);
            return 0;
        }
        if (((flags >> RS_TEX_WRAP_U_SHIFT) & 3u) == 3u || ((flags >> RS_TEX_WRAP_V_SHIFT) & 3u) == 3u) {
            swprintf(why, (size_t)whyCap, L"its texture has an unknown wrap (flags 0x%x)", flags);
            return 0;
        }
    }
    if ((size_t)tw * th * 4 > bytes - at || bytes - at - (size_t)tw * th * 4 < RS_TEX_MATERIAL_BYTES + 4) {
        swprintf(why, (size_t)whyCap, L"it ends in its texture (%u x %u)", tw, th);
        return 0;
    }
    if (tw != 0 && !RsTex_Image(&out->tex, tw, th, flags, data + at)) {
        swprintf(why, (size_t)whyCap, L"there is no memory for the mip levels of its texture (%u x %u)", tw, th);
        return 0;
    }
    out->textured = tw != 0;
    at += (size_t)tw * th * 4;
    // The one material: texture 0 when there is one, opaque or mask.
    if (!RsTex_Material(data + at, out->textured ? 1 : 0, &out->mat) || out->mat.texture != (out->textured ? 0 : -1) ||
        out->mat.alphaMode > RS_TEX_ALPHA_MASK) {
        swprintf(why, (size_t)whyCap, L"its material names texture %d with alpha mode %u", RsTex_S16(data + at + 4), (unsigned)data[at + 6]);
        RsTex_FreeWheel(out);
        return 0;
    }
    at += RS_TEX_MATERIAL_BYTES;
    count = RsTex_U32(data + at);
    at += 4;
    if (count < 1 || count > RS_TEX_WHEEL_TRIANGLES_MAX) {
        swprintf(why, (size_t)whyCap, L"it has %u triangles, 1..%d are possible", count, RS_TEX_WHEEL_TRIANGLES_MAX);
        RsTex_FreeWheel(out);
        return 0;
    }
    if (bytes - at != (size_t)count * RS_TEX_WHEEL_TRI_BYTES) {
        swprintf(why, (size_t)whyCap, L"its length does not match its triangle count");
        RsTex_FreeWheel(out);
        return 0;
    }
    out->tris = data + at;
    out->triangles = (int)count;
    return 1;
}

int RsTex_NativeMaterialOf(const struct RsTexNative *n, int tri)
{
    const unsigned char *p = n->indexed ? n->idx + (size_t)tri * RS_TEX_NATIVE_INDEX_BYTES + 6
                                        : n->tris + (size_t)tri * RS_TEX_NATIVE_TRI_BYTES + 36;
    return (int)p[0] | ((int)p[1] << 8);
}

int RsTex_NativeVertex(const struct RsTexNative *n, int tri, int c)
{
    const unsigned char *p;
    if (!n->indexed)
        return 3 * tri + c;
    p = n->idx + (size_t)tri * RS_TEX_NATIVE_INDEX_BYTES + (size_t)c * 2;
    return (int)p[0] | ((int)p[1] << 8);
}

void RsTex_NativeCorner(const struct RsTexNative *n, int tri, int c, struct RsTexCorner *out)
{
    const unsigned char *p = n->indexed ? n->vtx + (size_t)RsTex_NativeVertex(n, tri, c) * RS_TEX_NATIVE_VERTEX_BYTES
                                        : n->tris + (size_t)tri * RS_TEX_NATIVE_TRI_BYTES + (size_t)c * 12;
    out->u = RsTex_S32(p);
    out->v = RsTex_S32(p + 4);
    out->r = p[8];
    out->g = p[9];
    out->b = p[10];
    out->a = p[11];
}

void RsTex_NativePosition(const struct RsTexNative *n, int pose, int tri, int c, int xyz[3])
{
    const unsigned char *p = n->pos[pose] + (size_t)RsTex_NativeVertex(n, tri, c) * RS_TEX_NATIVE_POS3_BYTES;
    xyz[0] = RsTex_S16(p);
    xyz[1] = RsTex_S16(p + 2);
    xyz[2] = RsTex_S16(p + 4);
}

void RsTex_NativeEndPosition(const struct RsTexNative *n, int e, int tri, int c, int xyz[3])
{
    const unsigned char *p = n->end[e] + (size_t)RsTex_NativeVertex(n, tri, c) * RS_TEX_NATIVE_POS3_BYTES;
    xyz[0] = RsTex_S16(p);
    xyz[1] = RsTex_S16(p + 2);
    xyz[2] = RsTex_S16(p + 4);
}

void RsTex_WheelCorner(const struct RsTexWheel *w, int tri, int c, int xyz[3], struct RsTexCorner *out)
{
    const unsigned char *p = w->tris + (size_t)tri * RS_TEX_WHEEL_TRI_BYTES + (size_t)c * 14;
    xyz[0] = RsTex_S16(p);
    xyz[1] = RsTex_S16(p + 2);
    xyz[2] = RsTex_S16(p + 4);
    out->u = RsTex_S32(p + 6);
    out->v = RsTex_S32(p + 10);
    out->r = out->g = out->b = out->a = 255;
}

int RsTex_WheelTwoSided(const struct RsTexWheel *w, int tri)
{
    return (w->tris[(size_t)tri * RS_TEX_WHEEL_TRI_BYTES + 42] & 1u) != 0u;
}

const struct RsTexImage *RsTex_NativeImage(const struct RsTexNative *n, int material)
{
    int t;
    if (material < 0 || material >= n->materialCount)
        return NULL;
    t = n->mat[material].texture;
    return t >= 0 && t < n->textureCount ? &n->tex[t] : NULL;
}

// |d| x edge, in Q16 texels per pixel, held at 2^28 (4096 texels per pixel:
// beyond the last level of a texture of 2048, which starts above 2^10.5).
// Squared and summed it stays below 2^58.
static unsigned long long RsTex_Texels(long long d, int shift)
{
    unsigned long long a = d < 0 ? (d < -(1LL << 28) ? (1ULL << 28) : (unsigned long long)-d) : (unsigned long long)d;
    if (a > (1ULL << 28))
        a = 1ULL << 28;
    a <<= shift;
    return a > (1ULL << 28) ? (1ULL << 28) : a;
}

int RsTex_Level(const struct RsTexImage *img, long long dudx, long long dvdx, long long dudy, long long dvdy)
{
    unsigned long long ax, bx, ay, by, rx, ry, rho2;
    int level = 0;

    if (!img || img->levels <= 1)
        return 0;
    ax = RsTex_Texels(dudx, img->shiftW[0]);
    bx = RsTex_Texels(dvdx, img->shiftH[0]);
    ay = RsTex_Texels(dudy, img->shiftW[0]);
    by = RsTex_Texels(dvdy, img->shiftH[0]);
    rx = ax * ax + bx * bx;      // Q32 texels^2, below 2^58
    ry = ay * ay + by * by;
    rho2 = rx > ry ? rx : ry;
    // Level k from lambda > k - 0.5: rho^2 > 2^(2k - 1), in Q32 2^(2k + 31).
    while (level + 1 < img->levels && rho2 > (1ULL << (2 * (level + 1) + 31)))
        level++;
    return level;
}

static long long RsTex_Clamp(long long v, long long limit)
{
    return v > limit ? limit : (v < -limit ? -limit : v);
}

// At the centroid every corner weighs 1/3, so with s the sum of the depth
// weights and ubar the perspective-correct u there:
//   du/dx = 3 x sum (dw_i/dx x z_i x (u_i - ubar)) / s
// dw_i/dx the change of corner i's screen weight per pixel (an edge function
// over the area). The depth weights are taken relative to the largest (Q10):
// only their ratios matter, and the products stay below 2^63.
void RsTex_TriangleSlopes(const long long x[3], const long long y[3], const long long z[3], const long long u[3], const long long v[3],
                          long long slope[4])
{
    const long long lim = 1LL << 21;
    long long px[3], py[3], zn[3], gx[3], gy[3];
    long long area, zmax, s, ubar, vbar, den;
    long long sux = 0, svx = 0, suy = 0, svy = 0;
    int i;

    slope[0] = slope[1] = slope[2] = slope[3] = 0;
    for (i = 0; i < 3; i++) {
        px[i] = RsTex_Clamp(x[i], lim);
        py[i] = RsTex_Clamp(y[i], lim);
    }
    area = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
    zmax = 0;
    for (i = 0; i < 3; i++) {
        zn[i] = z[i] > 0 ? RsTex_Clamp(z[i], 1LL << 40) : 0;
        zmax = zn[i] > zmax ? zn[i] : zmax;
    }
    if (area == 0 || zmax <= 0)
        return;
    s = 0;
    ubar = 0;
    vbar = 0;
    for (i = 0; i < 3; i++) {
        zn[i] = (zn[i] << 10) / zmax;
        if (zn[i] < 1)
            zn[i] = 1;
        s += zn[i];
        ubar += zn[i] * RsTex_Clamp(u[i], 1LL << 22);
        vbar += zn[i] * RsTex_Clamp(v[i], 1LL << 22);
    }
    ubar /= s;
    vbar /= s;
    // Weight changes per pixel (16 sub-pixels): edge i is the one opposite corner i.
    gx[0] = -(py[2] - py[1]) * 16;
    gy[0] = (px[2] - px[1]) * 16;
    gx[1] = -(py[0] - py[2]) * 16;
    gy[1] = (px[0] - px[2]) * 16;
    gx[2] = -(py[1] - py[0]) * 16;
    gy[2] = (px[1] - px[0]) * 16;
    for (i = 0; i < 3; i++) {
        const long long du = RsTex_Clamp(u[i], 1LL << 22) - ubar, dv = RsTex_Clamp(v[i], 1LL << 22) - vbar;
        sux += gx[i] * zn[i] * du;
        svx += gx[i] * zn[i] * dv;
        suy += gy[i] * zn[i] * du;
        svy += gy[i] * zn[i] * dv;
    }
    den = area * s;
    slope[0] = 3 * sux / den;
    slope[1] = 3 * svx / den;
    slope[2] = 3 * suy / den;
    slope[3] = 3 * svy / den;
}
