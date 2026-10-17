// rs_hires_model.c - see rs_hires_model.h

#include "reloadstudio.h"
#include "rs_hires_model.h"

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
// The picture of the bind pose and the clips (RLDPV2, see rs_hires_model.h)
//
// Every drawn triangle corner is kept once, in the space of its own node (a
// skinned mesh: mesh space) with its normal, its unlit base colour, its joints
// and weights, its node and its wheel: THE CORNERS. The bind pose picture
// stands every corner at the rest transform of its node (cgltf); a clip frame
// (RsHires_AnimFrame) poses the skeleton with include/rldhires_anim.inc and
// moves every corner with it - a skinned one by RldAnim_SkinVertex, a rigid one
// by its node's matrix - the functions the engine poses a hi-res driver with.

// One record of the file: s16 x, y, z (1/16 game units), u8 r, g, b, pad.
#define RS_HIRES_RECORD 10

struct RsHiresCorner {
    float p[3];            // in the space of its node (skinned: mesh space)
    float n[3];            // the same; (0, 0, 0) = flat: the triangle's normal is taken
    u8 rgb[3];             // base colour factor x texture, unlit
    u8 pad;                // bit 0: drawn from both sides (doubleSided)
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
    RldAnimTrs *pose;                // nodeCount each
    RldAnimTrs *overlay;
    float *world;                    // nodeCount x 16
    float *palette;                  // jointCount x 12
    float *outP;                     // cornerCount x 3: the corners of the last frame
    float *outN;
    unsigned char *frame;            // the last frame as RLDPV2
    size_t frameBytes;
};

struct RsHiresTex {
    int tried;
    struct RldPngImage png;   // rgba NULL = no texture (not used, not a PNG, refused by the decoder)
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

// The base colour texture of a material, decoded once (only images a material
// uses are; RldHires_Open read their headers).
static const struct RldPngImage *RsHires_Texture(const RldHiresModel *m, struct RsHiresTex *tex, const cgltf_material *mat)
{
    const cgltf_texture *t;
    int index;
    if (!mat || !(t = mat->pbr_metallic_roughness.base_color_texture.texture) || !t->image)
        return NULL;
    index = (int)cgltf_image_index(m->data, t->image);
    if (index < 0 || index >= m->imageCount || !m->images[index].used || !m->images[index].isPng || m->images[index].width == 0)
        return NULL;
    if (!tex[index].tried) {
        const char *why = NULL;
        tex[index].tried = 1;
        if (RldPng_Decode(m->images[index].png, m->images[index].pngSize, &tex[index].png, &why) != RLDPNG_OK)
            memset(&tex[index].png, 0, sizeof(tex[index].png));
    }
    return tex[index].png.rgba ? &tex[index].png : NULL;
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
static void RsHires_CollectPrimitive(const RldHiresModel *m, struct RsHiresTex *tex, const cgltf_node *node, int nodeIndex, int wheel,
                                     const cgltf_primitive *prim, struct RsHiresCorners *list)
{
    const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
    const cgltf_accessor *nrm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
    const cgltf_accessor *uv = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
    const cgltf_accessor *joints = cgltf_find_accessor(prim, cgltf_attribute_type_joints, 0);
    const cgltf_accessor *weights = cgltf_find_accessor(prim, cgltf_attribute_type_weights, 0);
    const cgltf_material *mat = prim->material;
    const struct RldPngImage *img = RsHires_Texture(m, tex, mat);
    const int skinned = node->skin != NULL && joints && weights && !joints->is_sparse && !weights->is_sparse;
    float factor[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    u8 pad = (mat && mat->double_sided) ? 1u : 0u;
    cgltf_size n, t;
    int c, k;

    if (prim->type != cgltf_primitive_type_triangles || !RldHires_IsFloat3(pos) || pos->is_sparse ||
        (prim->indices && prim->indices->is_sparse))
        return;
    if (nrm && (!RldHires_IsFloat3(nrm) || nrm->is_sparse))
        nrm = NULL;
    if (uv && (uv->type != cgltf_type_vec2 || uv->is_sparse))
        uv = NULL;
    n = prim->indices ? prim->indices->count : pos->count;
    if (n % 3u != 0u)
        return;
    if (mat)
        memcpy(factor, mat->pbr_metallic_roughness.base_color_factor, sizeof(factor));

    for (t = 0; t < n; t += 3u) {
        for (c = 0; c < 3; c++) {
            cgltf_size vi = prim->indices ? cgltf_accessor_read_index(prim->indices, t + (cgltf_size)c) : t + (cgltf_size)c;
            struct RsHiresCorner *q = RsHires_NewCorner(list);
            float col[3] = {1.0f, 1.0f, 1.0f};
            if (!q)
                return;
            cgltf_accessor_read_float(pos, vi, q->p, 3);
            if (nrm)
                cgltf_accessor_read_float(nrm, vi, q->n, 3);
            if (img && uv) {
                float u[2] = {0, 0};
                float fu, fv;
                int x, y;
                cgltf_accessor_read_float(uv, vi, u, 2);
                fu = u[0] - floorf(u[0]);
                fv = u[1] - floorf(u[1]);
                if (!(fu >= 0.0f && fu < 1.0f))
                    fu = 0.0f;
                if (!(fv >= 0.0f && fv < 1.0f))
                    fv = 0.0f;
                x = (int)(fu * (float)img->width);
                y = (int)(fv * (float)img->height);
                if (x >= (int)img->width)
                    x = (int)img->width - 1;
                if (y >= (int)img->height)
                    y = (int)img->height - 1;
                for (k = 0; k < 3; k++)
                    col[k] = (float)img->rgba[((size_t)y * img->width + (size_t)x) * 4u + (size_t)k] / 255.0f;
            }
            for (k = 0; k < 3; k++)
                q->rgb[k] = RsHires_Byte(col[k] * factor[k]);
            q->pad = pad;
            q->node = nodeIndex;
            q->wheel = (signed char)wheel;
            q->skinned = (signed char)skinned;
            if (skinned) {
                cgltf_uint j[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(joints, vi, j, 4);
                cgltf_accessor_read_float(weights, vi, q->weight, 4);
                for (k = 0; k < 4; k++)
                    q->joint[k] = (u8)(j[k] < 255u ? j[k] : 0u);   // RldHires_Open refused a joint past the skin (HRG-14)
            }
        }
    }
}

static void RsHires_CollectNode(const RldHiresModel *m, struct RsHiresTex *tex, int nodeIndex, int wheel, struct RsHiresCorners *list)
{
    const cgltf_node *node = &m->data->nodes[nodeIndex];
    cgltf_size p;
    if (!node->mesh)
        return;
    for (p = 0; p < node->mesh->primitives_count; p++)
        RsHires_CollectPrimitive(m, tex, node, nodeIndex, wheel, &node->mesh->primitives[p], list);
}

// The lit colour of a corner: base x (half ambient + half diffuse of one light
// from the front left above).
static void RsHires_Lit(const u8 rgb[3], const float n[3], u8 out[3])
{
    static const float light[3] = {0.3714f, 0.7428f, 0.5571f};   // (0.4, 0.8, 0.6) normalised
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    float d = (len > 0.0f) ? (n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / len : 0.0f;
    float shade = 0.5f + 0.5f * (d > 0.0f ? d : 0.0f);
    int k;
    for (k = 0; k < 3; k++)
        out[k] = RsHires_Byte(((float)rgb[k] / 255.0f) * shade);
}

// The picture: header, pose 0 = the corners at p (with normals n; a corner
// without a normal takes its triangle's), pose 1 = the framing box as two
// triangles without area (never drawn; it only fixes the view's framing, so
// that a playing clip does not zoom), pose 2 empty. malloc'ed.
static unsigned char *RsHires_Write(const struct RsHiresCorner *corners, int count, const float *p, const float *n,
                                    const float box[2][3], size_t *size)
{
    const size_t tris = (size_t)count / 3u;
    const size_t bytes = 16u + tris * 3u * RS_HIRES_RECORD + 4u + 2u * 3u * RS_HIRES_RECORD + 4u;
    unsigned char *out = (unsigned char *)malloc(bytes);
    unsigned char *at;
    size_t i;
    int c, k;

    *size = 0;
    if (!out)
        return NULL;
    memcpy(out, "RLDPV2\0\0", 8);
    RsHires_PutU32(out + 8, 3u);
    RsHires_PutU32(out + 12, (u32)tris);
    at = out + 16;
    for (i = 0; i < tris; i++) {
        const float *tp = &p[i * 9u];
        float face[3];
        float e1[3], e2[3];
        for (k = 0; k < 3; k++) {
            e1[k] = tp[3 + k] - tp[k];
            e2[k] = tp[6 + k] - tp[k];
        }
        face[0] = e1[1] * e2[2] - e1[2] * e2[1];
        face[1] = e1[2] * e2[0] - e1[0] * e2[2];
        face[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for (c = 0; c < 3; c++) {
            const struct RsHiresCorner *q = &corners[i * 3u + (size_t)c];
            const float *cn = &n[(i * 3u + (size_t)c) * 3u];
            const int flat = q->n[0] == 0.0f && q->n[1] == 0.0f && q->n[2] == 0.0f;
            for (k = 0; k < 3; k++) {
                short s = RsHires_Coord(tp[c * 3 + k]);
                at[k * 2] = (u8)(s & 0xff);
                at[k * 2 + 1] = (u8)((s >> 8) & 0xff);
            }
            RsHires_Lit(q->rgb, flat ? face : cn, at + 6);
            at[9] = q->pad;
            at += RS_HIRES_RECORD;
        }
    }
    RsHires_PutU32(at, 2u);
    at += 4;
    for (i = 0; i < 2; i++)
        for (c = 0; c < 3; c++) {
            memset(at, 0, RS_HIRES_RECORD);
            for (k = 0; k < 3; k++) {
                short s = RsHires_Coord(box[i][k]);
                at[k * 2] = (u8)(s & 0xff);
                at[k * 2 + 1] = (u8)((s >> 8) & 0xff);
            }
            at += RS_HIRES_RECORD;
        }
    RsHires_PutU32(at, 0u);
    *size = bytes;
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
    struct RsHiresTex *tex;
    struct RsHiresAnim *a;
    float *p, *n;
    int i, k;
    const char *why = "";

    // drawNodes is made only after the ranges and cgltf_validate passed: the
    // data can be read.
    if (!m->data || !m->drawNodes)
        return;
    memset(&list, 0, sizeof(list));
    tex = (struct RsHiresTex *)calloc((size_t)(m->imageCount > 0 ? m->imageCount : 1), sizeof(*tex));
    if (!tex)
        return;
    for (i = 0; i < m->drawNodeCount; i++)
        RsHires_CollectNode(m, tex, m->drawNodes[i], -1, &list);
    if (m->wheelNode[0] >= 0)
        for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
            RsHires_CollectNode(m, tex, m->wheelNode[i], i, &list);
    for (i = 0; i < m->imageCount; i++)
        RldPng_Free(&tex[i].png);
    free(tex);
    if (list.failed || list.count == 0) {
        free(list.c);
        return;
    }

    a = (struct RsHiresAnim *)calloc(1, sizeof(*a));
    p = (float *)malloc((size_t)list.count * 3u * sizeof(float));
    n = (float *)malloc((size_t)list.count * 3u * sizeof(float));
    if (!a || !p || !n) {
        free(a);
        free(p);
        free(n);
        free(list.c);
        return;
    }
    a->corners = list.c;
    a->cornerCount = list.count;
    a->outP = p;
    a->outN = n;
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
    out->preview = RsHires_Write(list.c, list.count, p, n, a->box, &out->previewBytes);
    out->previewTriangles = out->preview ? list.count / 3 : 0;

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
    a->frame = RsHires_Write(a->corners, a->cornerCount, a->outP, a->outN, a->box, &a->frameBytes);
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
