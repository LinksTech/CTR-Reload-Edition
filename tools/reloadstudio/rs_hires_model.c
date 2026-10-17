// rs_hires_model.c - see rs_hires_model.h

#include "reloadstudio.h"
#include "rs_hires_model.h"
#include "rs_tex.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// The format code is written for the packer's and the game's types.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;

// The container reader (for the CHRG of a .rldchar) and the hi-res rules, the
// same files the game and rldpack include. All static: this translation unit
// has its own copy; cgltf's implementation is in rs_rldpack.c (with rldpack.c)
// or, for the self-test, in rs_hires_test.c.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4189 4244 4245 4267 4505)
#endif
#include <rldtrack.inc>
#include <rldchar.inc>
#include <rldhires.inc>
#include <rldhires_anim.inc>
#include <rldhires_hull.inc>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define RS_HIRES_FILE_MAX (64u * 1024u * 1024u + 4096u)   // a .glb as large as CHRG may hold, and some
#define RS_HIRES_UNITS 1024.0f                            // file units per metre: 64 model units x 16

static const wchar_t *const s_rsHiresClipNames[RS_HIRES_CLIPS] = {
    L"idle", L"steer_left", L"steer_right", L"jump", L"drift_left", L"drift_right",
    L"hit", L"podium_1", L"podium_2", L"podium_3", L"lose", L"menu"};

const wchar_t *RsHires_ClipName(int clip)
{
    return (clip >= 0 && clip < RS_HIRES_CLIPS) ? s_rsHiresClipNames[clip] : L"?";
}

static void RsHires_Widen(wchar_t *dst, int cap, const char *src)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cap);
    if (n <= 0)
        dst[0] = 0;
    dst[cap - 1] = 0;
}

static int RsHires_Severity(int level)
{
    return level == RLDHIRES_ERROR ? RS_SEV_ERROR : level == RLDHIRES_WARNING ? RS_SEV_WARNING : RS_SEV_NOTE;
}

// ---------------------------------------------------------------------------
// The picture of the bind pose and the clips (RLDPV2 + RLDPN2, see rs_hires_model.h)
//
// Every drawn triangle corner is kept once, in the space of its own node (a
// skinned mesh: mesh space) with its normal, UV, vertex colour, material,
// joints and weights, its node and its wheel: THE CORNERS. The bind pose
// picture stands every corner at the rest transform of its node (cgltf); a
// clip frame (RsHires_AnimFrame) poses the skeleton with
// include/rldhires_anim.inc and moves every corner with it - a skinned one by
// RldAnim_SkinVertex, a rigid one by its node's matrix - the functions the
// engine poses a hi-res driver with.
//
// THE COLOURS are the engine's ("ns", platform/engine_static_shader.inc): the
// base colour texel x the vertex colour x the material's base colour factor,
// both factors on the display curve (encodeSrgb), the texel as stored. The
// view's textured model (rs_tex.h: out = T x C x M / 255^2, per pixel, T through the engine's hi-res filter)
// is that product, so C = the corner's COLOR_0 - quantised to a byte as the
// engine stores it (EngHc_ReadPrimitive), then on the display curve - and M =
// the factor on the display curve. The preview's light (half ambient, half one
// light from the front left above) is folded into C; the engine lights with
// its sun and its normal maps, which the view has not.

// One record of RLDPV2: s16 x, y, z (1/16 game units), u8 r, g, b, pad.
#define RS_HIRES_RECORD 10
#define RS_HIRES_NATIVE_POS 18     // RLDPN2 per triangle and pose: 3 x s16 x, y, z
#define RS_HIRES_NATIVE_TRI 40     // RLDPN2 per triangle: 3 x {s32 u, s32 v, u8 r, g, b, a}, u16 material, u16 0
#define RS_HIRES_UV_MAX (64 * 65536)

struct RsHiresCorner {
    float p[3];            // in the space of its node (skinned: mesh space)
    float n[3];            // the same; (0, 0, 0) = flat: the triangle's normal is taken
    float uv[2];           // TEXCOORD_0, v from the top
    u8 colour[4];          // C before the light: COLOR_0 as the engine holds it, on the display curve; 255 without
    u8 texel[3];           // the base colour texel at this UV (nearest; 255 without) - the flat picture only
    u8 pad;                // bit 0: drawn from both sides (doubleSided)
    unsigned short material; // the preview material: the glTF material, or the default one after them
    u8 joint[4];
    float weight[4];
    int node;              // its node
    signed char wheel;     // RLDHIRES_WHEEL_* when it belongs to a wheel node, -1 else
    signed char skinned;
};

struct RsHiresAnim {
    RldAnimSkeleton skel;
    RldAnimClip clip[RS_HIRES_CLIPS];
    int has[RS_HIRES_CLIPS];         // clip built
    struct RsHiresCorner *corners;
    int cornerCount;                 // 3 per triangle
    float box[2][3];                 // the framing box, metres (pose 1 of every picture)
    unsigned char *blob;             // RLDPN2 head, textures and materials (NULL: the flat picture alone)
    size_t blobBytes;
    u8 (*matRgba)[4];                // M of every preview material (the flat picture)
    int materials;
    RldAnimTrs *pose;                // nodeCount each
    RldAnimTrs *overlay;
    float *world;                    // nodeCount x 16
    float *palette;                  // jointCount x 12
    float *outP;                     // cornerCount x 3: the corners of the last frame
    float *outN;
    unsigned char *frame;            // the last frame
    size_t frameBytes;
};

// RsHires_SetPointSampling: 1 = the next pictures sample nearest (the engine's
// --engine-hires-chars-filter nearest, for comparison images).
static int s_rsHiresPointSampling;

void RsHires_SetPointSampling(int on)
{
    s_rsHiresPointSampling = on != 0;
}

// A decoded image, sized to what the engine uploads (RldHires_UpEdge) the way
// the engine sizes it (RsHires_ResampleUp).
struct RsHiresImg {
    int tried;
    unsigned char *rgba;     // w x h x 4, NULL = none (not used, not a PNG, refused by the decoder, too large)
    int w, h;
    struct RldPngImage png;  // the decoded file (the corners' texels)
};

// A texture of the preview: an image with its wrap pair (the engine's upload of
// a base colour, EngHc_TextureOf).
struct RsHiresSlot {
    int image;
    int wrapU, wrapV;
};

struct RsHiresCorners {
    struct RsHiresCorner *c;
    int count;
    int cap;
    int failed;
};

static void RsHires_PutU32(u8 *at, u32 v)
{
    at[0] = (u8)v;
    at[1] = (u8)(v >> 8);
    at[2] = (u8)(v >> 16);
    at[3] = (u8)(v >> 24);
}

static void RsHires_PutS16(u8 *at, int v)
{
    at[0] = (u8)(v & 0xff);
    at[1] = (u8)((v >> 8) & 0xff);
}

static short RsHires_Coord(float metres)
{
    float f = metres * RS_HIRES_UNITS;
    if (!(f > -32767.0f))
        return -32767;
    if (f > 32767.0f)
        return 32767;
    return (short)(f >= 0.0f ? f + 0.5f : f - 0.5f);
}

static u8 RsHires_Byte(float v)
{
    if (!(v > 0.0f))
        return 0;
    if (v >= 1.0f)
        return 255;
    return (u8)(v * 255.0f + 0.5f);
}

static float RsHires_Encode(float lin)
{
    const float c = lin <= 0.0f ? 0.0f : (lin >= 1.0f ? 1.0f : lin);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static int RsHires_Q16(float v)
{
    double s = (double)v * 65536.0;
    s = s >= 0.0 ? floor(s + 0.5) : -floor(-s + 0.5);
    if (s > RS_HIRES_UV_MAX)
        s = RS_HIRES_UV_MAX;
    if (s < -RS_HIRES_UV_MAX)
        s = -RS_HIRES_UV_MAX;
    return (int)s;
}

// An image decoded once and sized as the engine uploads it.
// The engine's round-up of an image to its upload size, step for step
// (platform/engine_static_load.c, EngineStatic_ResampleUp): bilinear on the
// stored bytes, texel centres, the edges held, half up - an image already of
// that size is copied as it is.
static void RsHires_ResampleUp(const unsigned char *src, int sw, int sh, unsigned char *dst, int dw, int dh)
{
    int x, y, c;
    if (sw == dw && sh == dh) {
        memcpy(dst, src, (size_t)dw * (size_t)dh * 4u);
        return;
    }
    for (y = 0; y < dh; y++) {
        double sy = ((double)y + 0.5) * (double)sh / (double)dh - 0.5;
        int y0, y1;
        double fy;
        sy = (sy < 0.0) ? 0.0 : ((sy > (double)(sh - 1)) ? (double)(sh - 1) : sy);
        y0 = (int)sy;
        y1 = (y0 + 1 < sh) ? y0 + 1 : y0;
        fy = sy - (double)y0;
        for (x = 0; x < dw; x++) {
            double sx = ((double)x + 0.5) * (double)sw / (double)dw - 0.5;
            int x0, x1;
            double fx;
            sx = (sx < 0.0) ? 0.0 : ((sx > (double)(sw - 1)) ? (double)(sw - 1) : sx);
            x0 = (int)sx;
            x1 = (x0 + 1 < sw) ? x0 + 1 : x0;
            fx = sx - (double)x0;
            for (c = 0; c < 4; c++) {
                const double a = src[((size_t)y0 * sw + x0) * 4 + c];
                const double b = src[((size_t)y0 * sw + x1) * 4 + c];
                const double d = src[((size_t)y1 * sw + x0) * 4 + c];
                const double e = src[((size_t)y1 * sw + x1) * 4 + c];
                const double top = a * (1.0 - fx) + b * fx;
                const double bottom = d * (1.0 - fx) + e * fx;
                const int v = (int)(top * (1.0 - fy) + bottom * fy + 0.5);
                dst[((size_t)y * dw + x) * 4 + c] = (unsigned char)((v < 0) ? 0 : ((v > 255) ? 255 : v));
            }
        }
    }
}

static struct RsHiresImg *RsHires_Image(const RldHiresModel *m, struct RsHiresImg *img, int index)
{
    struct RsHiresImg *g;
    if (index < 0 || index >= m->imageCount || !m->images[index].isPng || m->images[index].width == 0)
        return NULL;
    g = &img[index];
    if (!g->tried) {
        const char *why = NULL;
        g->tried = 1;
        if (RldPng_Decode(m->images[index].png, m->images[index].pngSize, &g->png, &why) != RLDPNG_OK) {
            memset(&g->png, 0, sizeof(g->png));
        } else {
            const int w = (int)RldHires_UpEdge(g->png.width), h = (int)RldHires_UpEdge(g->png.height);
            g->rgba = (w > 0 && h > 0) ? (unsigned char *)malloc((size_t)w * (size_t)h * 4u) : NULL;
            if (g->rgba) {
                RsHires_ResampleUp(g->png.rgba, (int)g->png.width, (int)g->png.height, g->rgba, w, h);
                g->w = w;
                g->h = h;
            }
        }
    }
    return g->png.rgba ? g : NULL;
}

static struct RsHiresCorner *RsHires_NewCorner(struct RsHiresCorners *list)
{
    if (list->failed)
        return NULL;
    if (list->count == list->cap) {
        int cap = list->cap ? list->cap * 2 : 3072;
        struct RsHiresCorner *grown = (struct RsHiresCorner *)realloc(list->c, (size_t)cap * sizeof(*grown));
        if (!grown) {
            list->failed = 1;
            return NULL;
        }
        list->c = grown;
        list->cap = cap;
    }
    memset(&list->c[list->count], 0, sizeof(list->c[0]));
    return &list->c[list->count++];
}

// One primitive's triangle corners into the list. The rules of RldHires_Open
// decide what is drawn: triangles with a float VEC3 POSITION, nothing sparse.
static void RsHires_CollectPrimitive(const RldHiresModel *m, struct RsHiresImg *img, const cgltf_node *node, int nodeIndex, int wheel,
                                     const cgltf_primitive *prim, struct RsHiresCorners *list)
{
    const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
    const cgltf_accessor *nrm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
    const cgltf_accessor *uv = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
    const cgltf_accessor *col = cgltf_find_accessor(prim, cgltf_attribute_type_color, 0);
    const cgltf_accessor *joints = cgltf_find_accessor(prim, cgltf_attribute_type_joints, 0);
    const cgltf_accessor *weights = cgltf_find_accessor(prim, cgltf_attribute_type_weights, 0);
    const cgltf_material *mat = prim->material;
    const cgltf_texture *bt = mat ? mat->pbr_metallic_roughness.base_color_texture.texture : NULL;
    const struct RsHiresImg *g = (bt && bt->image) ? RsHires_Image(m, img, (int)cgltf_image_index(m->data, bt->image)) : NULL;
    const int skinned = node->skin != NULL && joints && weights && !joints->is_sparse && !weights->is_sparse;
    const unsigned short material = (unsigned short)(mat ? cgltf_material_index(m->data, mat) : m->data->materials_count);
    u8 pad = (mat && mat->double_sided) ? 1u : 0u;
    cgltf_size n, t;
    int c, k;

    if (prim->type != cgltf_primitive_type_triangles || !RldHires_IsFloat3(pos) || pos->is_sparse ||
        (prim->indices && prim->indices->is_sparse))
        return;
    if (nrm && (!RldHires_IsFloat3(nrm) || nrm->is_sparse))
        nrm = NULL;
    if (uv && (uv->type != cgltf_type_vec2 || uv->is_sparse || uv->count != pos->count))
        uv = NULL;
    // COLOR_0 as the engine reads it (EngHc_ReadPrimitive): the same count as POSITION, VEC3 or VEC4.
    if (col && (col->is_sparse || col->count != pos->count))
        col = NULL;
    n = prim->indices ? prim->indices->count : pos->count;
    if (n % 3u != 0u)
        return;

    for (t = 0; t < n; t += 3u) {
        for (c = 0; c < 3; c++) {
            cgltf_size vi = prim->indices ? cgltf_accessor_read_index(prim->indices, t + (cgltf_size)c) : t + (cgltf_size)c;
            struct RsHiresCorner *q = RsHires_NewCorner(list);
            float vc[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (!q)
                return;
            cgltf_accessor_read_float(pos, vi, q->p, 3);
            if (nrm)
                cgltf_accessor_read_float(nrm, vi, q->n, 3);
            if (uv)
                cgltf_accessor_read_float(uv, vi, q->uv, 2);
            if (col)
                cgltf_accessor_read_float(col, vi, vc, cgltf_num_components(col->type));
            for (k = 0; k < 4; k++) {
                // The engine's byte (c x 255 + 0.5, clamped), then the display curve (alpha as it is).
                const float x = vc[k] * 255.0f + 0.5f;
                const int b = (x <= 0.0f) ? 0 : ((x >= 255.0f) ? 255 : (int)x);
                q->colour[k] = (k < 3) ? RsHires_Byte(RsHires_Encode((float)b / 255.0f)) : (u8)b;
            }
            q->texel[0] = q->texel[1] = q->texel[2] = 255;
            if (g && uv) {
                float fu = q->uv[0] - floorf(q->uv[0]), fv = q->uv[1] - floorf(q->uv[1]);
                int x, y;
                if (!(fu >= 0.0f && fu < 1.0f))
                    fu = 0.0f;
                if (!(fv >= 0.0f && fv < 1.0f))
                    fv = 0.0f;
                x = (int)(fu * (float)g->png.width);
                y = (int)(fv * (float)g->png.height);
                if (x >= (int)g->png.width)
                    x = (int)g->png.width - 1;
                if (y >= (int)g->png.height)
                    y = (int)g->png.height - 1;
                for (k = 0; k < 3; k++)
                    q->texel[k] = g->png.rgba[((size_t)y * g->png.width + (size_t)x) * 4u + (size_t)k];
            }
            q->pad = pad;
            q->material = material;
            q->node = nodeIndex;
            q->wheel = (signed char)wheel;
            q->skinned = (signed char)skinned;
            if (skinned) {
                cgltf_uint j[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(joints, vi, j, 4);
                cgltf_accessor_read_float(weights, vi, q->weight, 4);
                // The weights renormalised to a sum of 1, all on the first joint when they
                // sum to 0 - the engine's rule (EngHc_ReadPrimitive), so both skin alike.
                {
                    const float sum = q->weight[0] + q->weight[1] + q->weight[2] + q->weight[3];
                    for (k = 0; k < 4; k++)
                        q->weight[k] = (sum > 0.0f) ? (q->weight[k] / sum) : ((k == 0) ? 1.0f : 0.0f);
                }
                for (k = 0; k < 4; k++)
                    q->joint[k] = (u8)(j[k] < 255u ? j[k] : 0u);   // RldHires_Open refused a joint past the skin (HRG-14)
            }
        }
    }
}

static void RsHires_CollectNode(const RldHiresModel *m, struct RsHiresImg *img, int nodeIndex, int wheel, struct RsHiresCorners *list)
{
    const cgltf_node *node = &m->data->nodes[nodeIndex];
    cgltf_size p;
    if (!node->mesh)
        return;
    for (p = 0; p < node->mesh->primitives_count; p++)
        RsHires_CollectPrimitive(m, img, node, nodeIndex, wheel, &node->mesh->primitives[p], list);
}

// The bytes of every mip level of a w x h texture (the view's budget count, RldMip_ChainBytes).
static size_t RsHires_ChainBytes(int w, int h)
{
    size_t bytes = 0;
    for (;;) {
        bytes += (size_t)w * (size_t)h * 4u;
        if (w == 1 && h == 1)
            return bytes;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
}

// The textured part of the picture (RLDPN2 up to its triangles): the textures
// - one per base colour image and wrap pair, as the engine uploads them - and
// the materials (the file's, then a white default). NULL with *why when the
// view could not take it (more textures, materials or texture memory than it
// holds); the picture is then the flat one alone.
static unsigned char *RsHires_Blob(const RldHiresModel *m, struct RsHiresImg *img, size_t *bytes, u8 (*matRgba)[4], int *matCount,
                                   wchar_t *why, int whyCap)
{
    const int materials = (int)m->data->materials_count + 1;
    struct RsHiresSlot slots[RS_TEX_TEXTURES_MAX];
    int matSlot[RS_TEX_MATERIALS_MAX];
    int slotCount = 0, i, k;
    size_t size, budget = 0;
    unsigned char *out, *at;

    *bytes = 0;
    *matCount = 0;
    if (materials > RS_TEX_MATERIALS_MAX) {
        swprintf(why, whyCap, L"%d materials - the textured preview shows at most %d", materials - 1, RS_TEX_MATERIALS_MAX - 1);
        return NULL;
    }
    for (i = 0; i < materials; i++) {
        const cgltf_material *mat = (i < materials - 1) ? &m->data->materials[i] : NULL;
        const cgltf_texture *t = mat ? mat->pbr_metallic_roughness.base_color_texture.texture : NULL;
        const float *f = mat ? mat->pbr_metallic_roughness.base_color_factor : NULL;
        struct RsHiresImg *g;
        struct RsHiresSlot s;
        matSlot[i] = -1;
        for (k = 0; k < 3; k++)
            matRgba[i][k] = f ? RsHires_Byte(RsHires_Encode(f[k])) : 255;
        matRgba[i][3] = f ? RsHires_Byte(f[3]) : 255;
        if (!t || !t->image || !mat->has_pbr_metallic_roughness)
            continue;
        g = RsHires_Image(m, img, (int)cgltf_image_index(m->data, t->image));
        if (!g || !g->rgba)
            continue;
        s.image = (int)cgltf_image_index(m->data, t->image);
        s.wrapU = t->sampler ? RldHires_Wrap((int)t->sampler->wrap_s) : 0;
        s.wrapV = t->sampler ? RldHires_Wrap((int)t->sampler->wrap_t) : 0;
        for (k = 0; k < slotCount; k++)
            if (slots[k].image == s.image && slots[k].wrapU == s.wrapU && slots[k].wrapV == s.wrapV)
                break;
        if (k == slotCount) {
            if (slotCount >= RS_TEX_TEXTURES_MAX) {
                swprintf(why, whyCap, L"more than %d textures - the textured preview shows at most %d", RS_TEX_TEXTURES_MAX, RS_TEX_TEXTURES_MAX);
                return NULL;
            }
            budget += RsHires_ChainBytes(g->w, g->h);
            slots[slotCount++] = s;
        }
        matSlot[i] = k;
    }
    if (budget > RS_TEX_BUDGET) {
        swprintf(why, whyCap, L"the textures need %u KiB with their mip levels - the textured preview holds %u KiB", (unsigned)(budget / 1024u),
                 (unsigned)(RS_TEX_BUDGET / 1024u));
        return NULL;
    }
    size = 8u + 4u + 4u + 4u + (size_t)materials * 8u;
    for (k = 0; k < slotCount; k++)
        size += 12u + (size_t)img[slots[k].image].w * (size_t)img[slots[k].image].h * 4u;
    out = (unsigned char *)malloc(size);
    if (!out) {
        swprintf(why, whyCap, L"out of memory for the textured preview");
        return NULL;
    }
    memcpy(out, "RLDPN2\0\0", 8);
    RsHires_PutU32(out + 8, 0u);
    RsHires_PutU32(out + 12, (u32)slotCount);
    at = out + 16;
    for (k = 0; k < slotCount; k++) {
        const struct RsHiresImg *g = &img[slots[k].image];
        RsHires_PutU32(at, (u32)g->w);
        RsHires_PutU32(at + 4, (u32)g->h);
        RsHires_PutU32(at + 8, ((u32)slots[k].wrapU << 1) | ((u32)slots[k].wrapV << 3));   // sRGB, wrap U and V (CTXT flags)
        memcpy(at + 12, g->rgba, (size_t)g->w * (size_t)g->h * 4u);
        at += 12u + (size_t)g->w * (size_t)g->h * 4u;
    }
    RsHires_PutU32(at, (u32)materials);
    at += 4;
    for (i = 0; i < materials; i++) {
        const cgltf_material *mat = (i < materials - 1) ? &m->data->materials[i] : NULL;
        memcpy(at, matRgba[i], 4);
        RsHires_PutS16(at + 4, matSlot[i]);
        at[6] = (u8)((mat && mat->alpha_mode != cgltf_alpha_mode_opaque) ? 1 : 0);   // BLEND drawn as MASK, as the engine does
        at[7] = (u8)(s_rsHiresPointSampling ? 0u : RS_TEX_MATERIAL_FILTERED);   // sampled as the engine samples a hi-res driver (rs_tex.h)
        at += 8;
    }
    *bytes = size;
    *matCount = materials;
    return out;
}

// The light of a corner (half ambient + half one light from the front left above).
static float RsHires_Shade(const float n[3])
{
    static const float light[3] = {0.3714f, 0.7428f, 0.5571f};   // (0.4, 0.8, 0.6) normalised
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    float d = (len > 0.0f) ? (n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / len : 0.0f;
    return 0.5f + 0.5f * (d > 0.0f ? d : 0.0f);
}

// The picture of corners at p (normals n; a corner without a normal takes its
// triangle's):
//   RLDPV2  pose 0 the corners flat-coloured (texel at the corner x C x M, the
//           view's formula), pose 1 the framing box as two triangles without
//           area (never drawn; it only fixes the view's framing, so that a
//           playing clip does not zoom), pose 2 empty;
//   RLDPN2  (when there is a blob) the same triangles textured: the blob, then
//           the triangles in the three turn frames (all the same) and per
//           triangle its UVs, lit C and material - drawn by the view in place
//           of the flat ones.
// malloc'ed; *keepAt / *keepBytes: the blob's place, the same in every picture
// of the model (RsView_UpdatePreviewData leaves it alone).
static unsigned char *RsHires_Write(const struct RsHiresAnim *a, const float *p, const float *n, size_t *size, size_t *keepAt, size_t *keepBytes)
{
    const size_t tris = (size_t)a->cornerCount / 3u;
    const size_t classic = 16u + tris * 3u * RS_HIRES_RECORD + 4u + 2u * 3u * RS_HIRES_RECORD + 4u;
    const size_t textured = a->blob ? a->blobBytes + 8u + 3u * tris * RS_HIRES_NATIVE_POS + tris * RS_HIRES_NATIVE_TRI : 0u;
    unsigned char *out = (unsigned char *)malloc(classic + textured);
    unsigned char *at, *npos = NULL, *ntri = NULL;
    size_t i;
    int c, k, pose;

    *size = 0;
    *keepAt = 0;
    *keepBytes = 0;
    if (!out)
        return NULL;
    memcpy(out, "RLDPV2\0\0", 8);
    RsHires_PutU32(out + 8, 3u);
    RsHires_PutU32(out + 12, (u32)tris);
    if (a->blob) {
        memcpy(out + classic, a->blob, a->blobBytes);
        RsHires_PutU32(out + classic + a->blobBytes, (u32)tris);
        RsHires_PutU32(out + classic + a->blobBytes + 4u, 3u);
        npos = out + classic + a->blobBytes + 8u;
        ntri = npos + 3u * tris * RS_HIRES_NATIVE_POS;
        *keepAt = classic;
        *keepBytes = a->blobBytes;
    }
    at = out + 16;
    for (i = 0; i < tris; i++) {
        const float *tp = &p[i * 9u];
        float face[3], e1[3], e2[3];
        for (k = 0; k < 3; k++) {
            e1[k] = tp[3 + k] - tp[k];
            e2[k] = tp[6 + k] - tp[k];
        }
        face[0] = e1[1] * e2[2] - e1[2] * e2[1];
        face[1] = e1[2] * e2[0] - e1[0] * e2[2];
        face[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for (c = 0; c < 3; c++) {
            const struct RsHiresCorner *q = &a->corners[i * 3u + (size_t)c];
            const float *cn = &n[(i * 3u + (size_t)c) * 3u];
            const int flat = q->n[0] == 0.0f && q->n[1] == 0.0f && q->n[2] == 0.0f;
            const float shade = RsHires_Shade(flat ? face : cn);
            const u8 *mm = (q->material < a->materials && a->matRgba) ? a->matRgba[q->material] : NULL;
            u8 lit[3];
            for (k = 0; k < 3; k++) {
                const short s = RsHires_Coord(tp[c * 3 + k]);
                lit[k] = RsHires_Byte((float)q->colour[k] / 255.0f * shade);
                RsHires_PutS16(at + k * 2, s);
                // The view's textured formula, with the corner's texel: (T x C x M + 32512) / 65025.
                at[6 + k] = (u8)(((unsigned)q->texel[k] * lit[k] * (mm ? mm[k] : 255u) + 32512u) / 65025u);
            }
            at[9] = q->pad;
            at += RS_HIRES_RECORD;
            if (npos) {
                unsigned char *r = ntri + i * RS_HIRES_NATIVE_TRI + (size_t)c * 12u;
                for (pose = 0; pose < 3; pose++)
                    for (k = 0; k < 3; k++)
                        RsHires_PutS16(npos + (size_t)pose * tris * RS_HIRES_NATIVE_POS + i * RS_HIRES_NATIVE_POS + (size_t)c * 6u + (size_t)k * 2u,
                                       RsHires_Coord(tp[c * 3 + k]));
                RsHires_PutU32(r, (u32)RsHires_Q16(q->uv[0]));
                RsHires_PutU32(r + 4, (u32)RsHires_Q16(q->uv[1]));
                r[8] = lit[0];
                r[9] = lit[1];
                r[10] = lit[2];
                r[11] = q->colour[3];
                if (c == 2) {
                    RsHires_PutS16(ntri + i * RS_HIRES_NATIVE_TRI + 36u, q->material);
                    RsHires_PutS16(ntri + i * RS_HIRES_NATIVE_TRI + 38u, 0);
                }
            }
        }
    }
    RsHires_PutU32(at, 2u);
    at += 4;
    for (i = 0; i < 2; i++)
        for (c = 0; c < 3; c++) {
            memset(at, 0, RS_HIRES_RECORD);
            for (k = 0; k < 3; k++)
                RsHires_PutS16(at + k * 2, RsHires_Coord(a->box[i][k]));
            at += RS_HIRES_RECORD;
        }
    RsHires_PutU32(at, 0u);
    *size = classic + textured;
    return out;
}

static void RsHires_AnimFree(struct RsHiresAnim *a)
{
    int i;
    if (!a)
        return;
    for (i = 0; i < RS_HIRES_CLIPS; i++)
        if (a->has[i])
            RldAnim_FreeClip(&a->clip[i]);
    if (a->skel.block)
        RldAnim_FreeSkeleton(&a->skel);
    free(a->corners);
    free(a->blob);
    free(a->matRgba);
    free(a->pose);
    free(a->overlay);
    free(a->world);
    free(a->palette);
    free(a->outP);
    free(a->outN);
    free(a->frame);
    free(a);
}

// The corners, the bind pose picture and, when the skeleton and the clips can
// be built, the animation of a model RldHires_Open read.
static void RsHires_Picture(const RldHiresModel *m, struct RsHiresCheck *out)
{
    struct RsHiresCorners list;
    struct RsHiresImg *img;
    struct RsHiresAnim *a;
    float *p, *n;
    int i, k;
    const char *why = "";

    // drawNodes is made only after the ranges and cgltf_validate passed: the
    // data can be read.
    if (!m->data || !m->drawNodes)
        return;
    memset(&list, 0, sizeof(list));
    a = (struct RsHiresAnim *)calloc(1, sizeof(*a));
    img = (struct RsHiresImg *)calloc((size_t)(m->imageCount > 0 ? m->imageCount : 1), sizeof(*img));
    if (!a || !img) {
        free(a);
        free(img);
        return;
    }
    for (i = 0; i < m->drawNodeCount; i++)
        RsHires_CollectNode(m, img, m->drawNodes[i], -1, &list);
    if (m->wheelNode[0] >= 0)
        for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
            RsHires_CollectNode(m, img, m->wheelNode[i], i, &list);
    a->matRgba = (u8(*)[4])calloc(RS_TEX_MATERIALS_MAX, 4);
    if (a->matRgba && list.count / 3 <= RS_TEX_TRIANGLES_MAX)
        a->blob = RsHires_Blob(m, img, &a->blobBytes, a->matRgba, &a->materials, out->previewWhy, RS_HIRES_TEXT_MAX);
    else if (list.count / 3 > RS_TEX_TRIANGLES_MAX)
        swprintf(out->previewWhy, RS_HIRES_TEXT_MAX, L"%d triangles - the textured preview shows at most %d", list.count / 3, RS_TEX_TRIANGLES_MAX);
    if (!a->blob && a->matRgba) {
        // The flat picture still multiplies the factors in.
        const int materials = (int)m->data->materials_count + 1 <= RS_TEX_MATERIALS_MAX ? (int)m->data->materials_count + 1 : 0;
        for (i = 0; i < materials; i++) {
            const float *f = (i < materials - 1) ? m->data->materials[i].pbr_metallic_roughness.base_color_factor : NULL;
            for (k = 0; k < 3; k++)
                a->matRgba[i][k] = f ? RsHires_Byte(RsHires_Encode(f[k])) : 255;
            a->matRgba[i][3] = 255;
        }
        a->materials = materials;
    }
    for (i = 0; i < m->imageCount; i++) {
        RldPng_Free(&img[i].png);
        free(img[i].rgba);
    }
    free(img);
    if (list.failed || list.count == 0) {
        free(list.c);
        RsHires_AnimFree(a);
        return;
    }

    p = (float *)malloc((size_t)list.count * 3u * sizeof(float));
    n = (float *)malloc((size_t)list.count * 3u * sizeof(float));
    a->corners = list.c;
    a->cornerCount = list.count;
    a->outP = p;
    a->outN = n;
    if (!p || !n) {
        RsHires_AnimFree(a);
        return;
    }
    for (k = 0; k < 3; k++) {
        a->box[0][k] = m->boundsMin[k] - 0.25f;
        a->box[1][k] = m->boundsMax[k] + 0.25f;
    }

    // The bind pose: every corner at the rest transform of its node (cgltf, as
    // RldHires_Open measured the bounds); a skinned one as it is.
    {
        int last = -1;
        float world[16];
        for (i = 0; i < list.count; i++) {
            const struct RsHiresCorner *q = &list.c[i];
            if (q->skinned || m->data->nodes[q->node].skin) {
                memcpy(&p[i * 3], q->p, sizeof(q->p));
                memcpy(&n[i * 3], q->n, sizeof(q->n));
                continue;
            }
            if (q->node != last) {
                cgltf_node_transform_world(&m->data->nodes[q->node], world);
                last = q->node;
            }
            for (k = 0; k < 3; k++) {
                p[i * 3 + k] = world[k] * q->p[0] + world[4 + k] * q->p[1] + world[8 + k] * q->p[2] + world[12 + k];
                n[i * 3 + k] = world[k] * q->n[0] + world[4 + k] * q->n[1] + world[8 + k] * q->n[2];
            }
        }
    }
    out->preview = RsHires_Write(a, p, n, &out->previewBytes, &out->previewKeepAt, &out->previewKeepBytes);
    out->previewTriangles = out->preview ? list.count / 3 : 0;
    out->previewTextured = (a->blob != NULL);

    // The animation: the skeleton of every node (and the skin's joints) and the
    // clips of the file, copied out of cgltf (rldhires_anim.inc).
    if (!RldAnim_SkeletonFromGltf(m->data, m->skin >= 0 ? &m->data->skins[0] : NULL, &a->skel, &why)) {
        RsHires_Widen(out->animWhy, RS_HIRES_TEXT_MAX, why);
    } else {
        for (i = 0; i < RS_HIRES_CLIPS; i++) {
            u32 skipped = 0;
            if (m->clipAnim[i] < 0)
                continue;
            if (!RldAnim_ClipFromGltf(m->data, &m->data->animations[m->clipAnim[i]], &a->clip[i], &skipped, &why)) {
                wchar_t w[160];
                RsHires_Widen(w, 160, why);
                swprintf(out->animWhy, RS_HIRES_TEXT_MAX, L"clip '%ls' cannot be played: %ls", RsHires_ClipName(i), w);
                continue;
            }
            a->has[i] = 1;
        }
        a->pose = (RldAnimTrs *)malloc((size_t)a->skel.nodeCount * sizeof(RldAnimTrs));
        a->overlay = (RldAnimTrs *)malloc((size_t)a->skel.nodeCount * sizeof(RldAnimTrs));
        a->world = (float *)malloc((size_t)a->skel.nodeCount * 16u * sizeof(float));
        a->palette = (float *)malloc((size_t)(a->skel.jointCount ? a->skel.jointCount : 1u) * 12u * sizeof(float));
        if (a->pose && a->overlay && a->world && a->palette)
            out->animReady = 1;
        else
            swprintf(out->animWhy, RS_HIRES_TEXT_MAX, L"out of memory for the clips");
    }
    out->anim = a;
}

// ---------------------------------------------------------------------------
// Clip playback

// Preview seconds -> the clip's local seconds, by how the game plays the clip
// (the rule of EngHc_LocalTime, platform/engine_hires_pose.c): a loop wraps; a
// clip played once runs to its end and holds - here it starts again after a
// held second, so that the preview keeps showing it; a pose scrub (steering)
// is fraction x length.
static float RsHires_LocalTime(int play, float seconds, float fraction, float length, int full)
{
    if (play == RLDHIRES_PLAY_POSE) {
        const float f = full ? 1.0f : (fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction));
        return f * length;
    }
    if (play == RLDHIRES_PLAY_ONCE) {
        double local = fmod((double)seconds, (double)length + 1.0);
        return (float)(local > (double)length ? (double)length : local);
    }
    if (!(length > 0.0f))
        return 0.0f;
    {
        double local = fmod((double)seconds, (double)length);
        return (float)(local < 0.0 ? local + (double)length : local);
    }
}

// The steer share of the preview: 0 -> 1 -> 0 over 2 s.
static float RsHires_SteerShare(float seconds)
{
    double f = fmod((double)seconds, 2.0);
    if (f < 0.0)
        f += 2.0;
    return (float)(f <= 1.0 ? f : 2.0 - f);
}

// A clip (resolved, built) sampled into pose (which holds the rest pose).
static void RsHires_Sample(struct RsHiresAnim *a, int clip, int play, float seconds, float fraction, int full, RldAnimTrs *pose)
{
    float local;
    if (clip < 0 || clip >= RS_HIRES_CLIPS || !a->has[clip])
        return;
    local = RsHires_LocalTime(play, seconds, fraction, RldAnim_ClipLength(&a->clip[clip]), full);
    RldAnim_SampleClip(&a->clip[clip], RldAnim_ClipTime(&a->clip[clip], local, RLDANIM_PLAY_ONCE), pose, a->skel.nodeCount);
}

int RsHires_AnimFrame(struct RsHiresCheck *c, int clip, float seconds, float wheelDegrees, const unsigned char **bytes, size_t *size)
{
    struct RsHiresAnim *a = c->anim;
    const double spin = (double)wheelDegrees * 3.14159265358979 / 180.0;
    const float cs = (float)cos(spin), sn = (float)sin(spin);
    u32 j;
    int i, k;

    if (bytes)
        *bytes = NULL;
    if (size)
        *size = 0;
    if (!a || !c->animReady)
        return 0;

    // The pose, as the game draws the state of the clip: the resolved clip at
    // its play kind; a steer clip as the BASE state does - idle looped and the
    // steer clip scrubbed by the steer share, blended over it by that share;
    // a drift that falls back to its steer clip at full lock.
    RldAnim_PoseRest(&a->skel, a->pose);
    if (clip >= 0 && clip < RS_HIRES_CLIPS) {
        const int via = c->clipVia[clip];
        const int full = c->clipFullLock[clip];
        if (RldHires_ClipPlay(clip) == RLDHIRES_PLAY_POSE) {
            const float share = RsHires_SteerShare(seconds);
            const int idle = c->clipVia[RLDHIRES_CLIP_IDLE];
            RsHires_Sample(a, idle, RldHires_ClipPlay(idle), seconds, 0.0f, 0, a->pose);
            RldAnim_PoseRest(&a->skel, a->overlay);
            RsHires_Sample(a, idle, RldHires_ClipPlay(idle), seconds, 0.0f, 0, a->overlay);
            RsHires_Sample(a, via, RldHires_ClipPlay(via), seconds, share, 0, a->overlay);
            RldAnim_BlendPose(a->pose, a->overlay, share, a->pose, a->skel.nodeCount);
        } else {
            RsHires_Sample(a, via, RldHires_ClipPlay(via), seconds, 1.0f, full, a->pose);
        }
    }
    RldAnim_NodeMatrices(&a->skel, a->pose, a->world);
    for (j = 0; j < a->skel.jointCount; j++) {
        float jm[16];
        RldAnim_JointMatrix(&a->skel, a->world, j, jm);
        RldAnim_PaletteEntry(NULL, jm, &a->palette[(size_t)j * 12u]);
    }

    for (i = 0; i < a->cornerCount; i++) {
        const struct RsHiresCorner *q = &a->corners[i];
        float *op = &a->outP[i * 3], *on = &a->outN[i * 3];
        if (q->skinned && a->skel.jointCount) {
            RldAnim_SkinVertex(a->palette, q->joint, q->weight, q->p, q->n, op, on);
            if (q->n[0] == 0.0f && q->n[1] == 0.0f && q->n[2] == 0.0f)
                on[0] = on[1] = on[2] = 0.0f;
            continue;
        }
        {
            const float *w = (q->node >= 0 && (u32)q->node < a->skel.nodeCount) ? &a->world[(size_t)q->node * 16u] : NULL;
            float lp[3], ln[3];
            memcpy(lp, q->p, sizeof(lp));
            memcpy(ln, q->n, sizeof(ln));
            if (q->wheel >= 0) {
                // A wheel turns about its node's X axis; + rolls forward (its top toward +Z).
                lp[1] = q->p[1] * cs - q->p[2] * sn;
                lp[2] = q->p[1] * sn + q->p[2] * cs;
                ln[1] = q->n[1] * cs - q->n[2] * sn;
                ln[2] = q->n[1] * sn + q->n[2] * cs;
            }
            if (!w) {
                memcpy(op, lp, sizeof(lp));
                memcpy(on, ln, sizeof(ln));
                continue;
            }
            for (k = 0; k < 3; k++) {
                op[k] = w[k] * lp[0] + w[4 + k] * lp[1] + w[8 + k] * lp[2] + w[12 + k];
                on[k] = w[k] * ln[0] + w[4 + k] * ln[1] + w[8 + k] * ln[2];
            }
        }
    }

    free(a->frame);
    {
        size_t keepAt = 0, keepBytes = 0;
        a->frame = RsHires_Write(a, a->outP, a->outN, &a->frameBytes, &keepAt, &keepBytes);
    }
    if (!a->frame)
        return 0;
    if (bytes)
        *bytes = a->frame;
    if (size)
        *size = a->frameBytes;
    return a->cornerCount / 3;
}

int RsHires_AnimCorner(const struct RsHiresCheck *c, int index, float p[3], float n[3])
{
    const struct RsHiresAnim *a = c->anim;
    if (!a || index < 0 || index >= a->cornerCount)
        return 0;
    memcpy(p, &a->outP[index * 3], 3 * sizeof(float));
    memcpy(n, &a->outN[index * 3], 3 * sizeof(float));
    return 1;
}

float RsHires_AnimLocal(const struct RsHiresCheck *c, int clip, float seconds, float *share)
{
    const struct RsHiresAnim *a = c->anim;
    int via;
    if (share)
        *share = -1.0f;
    if (!a || clip < 0 || clip >= RS_HIRES_CLIPS)
        return 0.0f;
    via = c->clipVia[clip];
    if (via < 0 || !a->has[via])
        return 0.0f;
    if (RldHires_ClipPlay(clip) == RLDHIRES_PLAY_POSE) {
        const float f = RsHires_SteerShare(seconds);
        if (share)
            *share = f;
        return RsHires_LocalTime(RldHires_ClipPlay(via), seconds, f, RldAnim_ClipLength(&a->clip[via]), 0);
    }
    return RsHires_LocalTime(RldHires_ClipPlay(via), seconds, 1.0f, RldAnim_ClipLength(&a->clip[via]), c->clipFullLock[clip]);
}

float RsHires_AnimLength(const struct RsHiresCheck *c, int clip)
{
    const struct RsHiresAnim *a = c->anim;
    int via;
    if (!a || clip < 0 || clip >= RS_HIRES_CLIPS)
        return 0.0f;
    via = c->clipVia[clip];
    return (via >= 0 && a->has[via]) ? RldAnim_ClipLength(&a->clip[via]) : 0.0f;
}

// ---------------------------------------------------------------------------
// The check

static void RsHires_Fill(const RldHiresReport *rep, const RldHiresModel *m, struct RsHiresCheck *out)
{
    int i;
    out->errors = rep->errors;
    out->warnings = rep->warnings;
    out->notes = rep->notes;
    out->rowsDropped = rep->dropped;
    out->rowCount = rep->count < RS_HIRES_ROWS_MAX ? rep->count : RS_HIRES_ROWS_MAX;
    for (i = 0; i < out->rowCount; i++) {
        out->rows[i].severity = RsHires_Severity(rep->msg[i].level);
        RsHires_Widen(out->rows[i].rule, 8, rep->msg[i].rule);
        RsHires_Widen(out->rows[i].text, RS_HIRES_TEXT_MAX, rep->msg[i].text);
    }
    out->budgetTriangles = RLDHIRES_BUDGET_TRIANGLES;
    out->budgetEdge = RLDHIRES_BUDGET_TEXTURE_EDGE;
    out->budgetTextureBytes = RLDHIRES_BUDGET_TEXTURE_BYTES;
    if (!m)
        return;
    for (i = 0; i < RS_HIRES_CLIPS; i++) {
        int via = 0, lock = 0;
        int got = RldHires_ClipResolve(m, i, &via, &lock);
        out->clipState[i] = m->clipAnim[i] >= 0 ? RS_HIRES_CLIP_PRESENT : got >= 0 ? RS_HIRES_CLIP_FALLBACK : RS_HIRES_CLIP_REST;
        out->clipVia[i] = got;
        out->clipFullLock[i] = lock;
        out->clipSeconds[i] = m->clipSeconds[i];
        out->clips += m->clipAnim[i] >= 0;
    }
    out->wheels = m->wheelsFound;
    out->joints = m->joints;
    out->triangles = m->triangles;
    out->vertices = m->vertices;
    for (i = 0; i < m->imageCount; i++)
        out->textures += m->images[i].used != 0;
    out->textureBytes = m->textureBytes;
    out->textureEdge = m->textureEdgeMax;
    memcpy(out->boundsMin, m->boundsMin, sizeof(out->boundsMin));
    memcpy(out->boundsMax, m->boundsMax, sizeof(out->boundsMax));
    out->glbBytes = m->glbSize;
}

void RsHires_Budget(unsigned long long *triangles, unsigned *edge, unsigned long long *textureBytes)
{
    if (triangles)
        *triangles = RLDHIRES_BUDGET_TRIANGLES;
    if (edge)
        *edge = RLDHIRES_BUDGET_TEXTURE_EDGE;
    if (textureBytes)
        *textureBytes = RLDHIRES_BUDGET_TEXTURE_BYTES;
}

int RsHires_CheckGlb(const unsigned char *glb, size_t size, struct RsHiresCheck *out)
{
    static RldHiresReport rep;   // 13 KB; the page checks on its own thread only
    RldHiresModel m;
    int ok;

    memset(out, 0, sizeof(*out));
    RldHires_ReportClear(&rep);
    memset(&m, 0, sizeof(m));
    for (ok = 0; ok < RS_HIRES_CLIPS; ok++)
        out->clipVia[ok] = -1;
    if (size > (size_t)(RLDHIRES_CHUNK_LIMIT - RLDHIRES_CHUNK_HEADER_BYTES)) {
        RldHires_Say(&rep, "HRG-5", "the model is %u bytes - with its 16-byte header CHRG may be 64 MiB at most", (unsigned)size);
        RsHires_Fill(&rep, NULL, out);
        out->read = 1;
        return 1;
    }
    // The same order as rldpack hires-check (RldHiresPack_CheckGlb): the model,
    // then the budget when it opened.
    ok = RldHires_Open(glb, (u32)size, &m, &rep);
    if (m.data)
        ok = RldHires_CheckBudget(&m, &rep) && ok;
    if (m.usable) {
        // The poses of every clip (include/rldhires_hull.inc): what rldpack hires-check adds.
        float lo[3], hi[3];
        ok = RldHires_CheckClipHull(&m, &rep, lo, hi, NULL) && ok;
    }
    RsHires_Fill(&rep, &m, out);
    RsHires_Picture(&m, out);
    out->read = 1;
    out->usable = ok && rep.errors == 0;
    RldHires_Close(&m);
    return 1;
}

static unsigned char *RsHires_ReadAll(const wchar_t *path, size_t limit, size_t *size, wchar_t *why, int whyCap)
{
    FILE *f = _wfopen(path, L"rb");
    unsigned char *bytes;
    long n;
    *size = 0;
    if (!f) {
        swprintf(why, whyCap, L"The file cannot be opened.");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || (unsigned long)n > limit || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        swprintf(why, whyCap, L"The file is too large to be a hi-res model (at most 64 MiB).");
        return NULL;
    }
    bytes = (unsigned char *)malloc((size_t)n + 1u);
    if (!bytes || (n > 0 && fread(bytes, 1, (size_t)n, f) != (size_t)n)) {
        free(bytes);
        fclose(f);
        swprintf(why, whyCap, L"The file cannot be read.");
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return bytes;
}

int RsHires_CheckFile(const wchar_t *path, struct RsHiresCheck *out)
{
    unsigned char head[8];
    unsigned char *bytes;
    size_t size = 0;
    FILE *f;
    int isChar = 0;

    memset(out, 0, sizeof(*out));
    f = _wfopen(path, L"rb");
    if (!f) {
        swprintf(out->why, RS_HIRES_TEXT_MAX, L"The file cannot be opened.");
        return 0;
    }
    isChar = fread(head, 1, 8, f) == 8 && memcmp(head, RLDCHAR_MAGIC, 8) == 0;
    fclose(f);

    if (isChar) {
        // The CHRG of a character, read the way the game reads it.
        struct RldReader reader;
        const char *why;
        char *utf8;
        int need = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
        RldHiresChunk c;
        const char *rule = NULL;
        char detail[RLDHIRES_TEXT_MAX];
        int index, r;

        utf8 = (char *)malloc(need > 0 ? (size_t)need : 1u);
        if (!utf8)
            return 0;
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, need, NULL, NULL);
        why = Rld_OpenAs(&reader, utf8, &s_rldCharFormat);
        free(utf8);
        if (why) {
            wchar_t w[200];
            RsHires_Widen(w, 200, why);
            swprintf(out->why, RS_HIRES_TEXT_MAX, L"The character cannot be read: %ls.", w);
            return 0;
        }
        index = RldHires_FindChunk(&reader);
        if (index < 0) {
            Rld_Close(&reader);
            swprintf(out->why, RS_HIRES_TEXT_MAX, L"This character has no hi-res model (no CHRG chunk). Choose its .glb, or pack one into it below.");
            return 0;
        }
        bytes = RldHires_ReadChunk(&reader, index, &size, &why);
        Rld_Close(&reader);
        if (!bytes || !RldHires_ParseChunk(bytes, size, &c, &rule, detail, (int)sizeof(detail))) {
            static RldHiresReport rep;
            int k;
            RldHires_ReportClear(&rep);
            if (!bytes)
                RldHires_Say(&rep, "HRG-5", "%s", why ? why : "CHRG cannot be read");
            else
                RldHires_Say(&rep, rule ? rule : "HRG-2", "%s", detail);
            free(bytes);
            RsHires_Fill(&rep, NULL, out);
            for (k = 0; k < RS_HIRES_CLIPS; k++)
                out->clipVia[k] = -1;
            out->read = 1;
            out->fromCharacter = 1;
            return 1;
        }
        r = RsHires_CheckGlb(c.glb, c.glbSize, out);
        out->fromCharacter = 1;
        free(bytes);
        return r;
    }

    bytes = RsHires_ReadAll(path, RS_HIRES_FILE_MAX, &size, out->why, RS_HIRES_TEXT_MAX);
    if (!bytes)
        return 0;
    {
        int r = RsHires_CheckGlb(bytes, size, out);
        free(bytes);
        return r;
    }
}

void RsHires_CheckFree(struct RsHiresCheck *c)
{
    if (!c)
        return;
    RsHires_AnimFree(c->anim);
    c->anim = NULL;
    c->animReady = 0;
    free(c->preview);
    c->preview = NULL;
    c->previewBytes = 0;
    c->previewTriangles = 0;
}

int RsHires_PackArgs(const wchar_t *character, const wchar_t *model, const wchar_t *out, const wchar_t **args)
{
    args[0] = L"hires-add";
    args[1] = character;
    args[2] = model;
    args[3] = L"-o";
    args[4] = out;
    args[5] = L"--machine";
    return RS_HIRES_PACK_ARGS;
}

int RsHires_UpscaleArgs(const wchar_t *model, const wchar_t *derived, const wchar_t **args)
{
    args[0] = L"hires-upscale";
    args[1] = model;
    args[2] = L"-o";
    args[3] = derived;
    args[4] = L"--machine";
    return RS_HIRES_UPSCALE_ARGS;
}

int RsHires_UpscaledPath(const wchar_t *model, const wchar_t *out, wchar_t *path, int cap)
{
    const wchar_t *name = model;
    const wchar_t *p;
    size_t dirLen = 0, nameLen;
    for (p = model; *p; p++)
        if (*p == L'\\' || *p == L'/' || *p == L':')
            name = p + 1;
    for (p = out; *p; p++)
        if (*p == L'\\' || *p == L'/' || *p == L':')
            dirLen = (size_t)(p - out) + 1u;
    nameLen = wcslen(name);
    if (nameLen >= 4u && _wcsicmp(name + nameLen - 4u, L".glb") == 0)
        nameLen -= 4u;
    if (nameLen == 0u || cap <= 0 || dirLen + nameLen + 14u > (size_t)cap) {
        if (cap > 0)
            path[0] = 0;
        return 0;
    }
    memcpy(path, out, dirLen * sizeof(wchar_t));
    memcpy(path + dirLen, name, nameLen * sizeof(wchar_t));
    wcscpy(path + dirLen + nameLen, L"-upscaled.glb");
    return 1;
}
