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
// The picture of the bind pose (RLDPV2, see rs_hires_model.h)

struct RsHiresTex {
    int tried;
    struct RldPngImage png;   // rgba NULL = no texture (not used, not a PNG, refused by the decoder)
};

struct RsHiresOut {
    u8 *bytes;
    size_t used;
    size_t cap;
    int triangles;
    int failed;
};

static void RsHires_Put(struct RsHiresOut *o, const void *data, size_t n)
{
    if (o->failed)
        return;
    if (o->used + n > o->cap) {
        size_t cap = o->cap ? o->cap : 65536u;
        u8 *grown;
        while (cap < o->used + n)
            cap *= 2u;
        grown = (u8 *)realloc(o->bytes, cap);
        if (!grown) {
            o->failed = 1;
            return;
        }
        o->bytes = grown;
        o->cap = cap;
    }
    memcpy(o->bytes + o->used, data, n);
    o->used += n;
}

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

// One primitive's triangles into the picture. world: the node's matrix, NULL
// = mesh space (skinned).
static void RsHires_DrawPrimitive(const RldHiresModel *m, struct RsHiresTex *tex, const cgltf_primitive *prim, const float *world,
                                  struct RsHiresOut *o)
{
    static const float light[3] = {0.3714f, 0.7428f, 0.5571f};   // (0.4, 0.8, 0.6) normalised: front left above
    const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
    const cgltf_accessor *nrm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
    const cgltf_accessor *uv = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
    const cgltf_material *mat = prim->material;
    const struct RldPngImage *img = RsHires_Texture(m, tex, mat);
    float factor[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    u8 pad = (mat && mat->double_sided) ? 1u : 0u;
    cgltf_size n, t;

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
        float p[3][3], q[3][3], nn[3][3];
        u8 rec[30];
        int c, k;

        for (c = 0; c < 3; c++) {
            cgltf_size vi = prim->indices ? cgltf_accessor_read_index(prim->indices, t + (cgltf_size)c) : t + (cgltf_size)c;
            float a[3] = {0, 0, 0};
            cgltf_accessor_read_float(pos, vi, a, 3);
            for (k = 0; k < 3; k++)
                p[c][k] = world ? world[k] * a[0] + world[4 + k] * a[1] + world[8 + k] * a[2] + world[12 + k] : a[k];
            if (nrm) {
                float b[3] = {0, 0, 0};
                cgltf_accessor_read_float(nrm, vi, b, 3);
                for (k = 0; k < 3; k++)
                    nn[c][k] = world ? world[k] * b[0] + world[4 + k] * b[1] + world[8 + k] * b[2] : b[k];
            }
            q[c][0] = 1.0f;
            q[c][1] = q[c][2] = 1.0f;
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
                    q[c][k] = (float)img->rgba[((size_t)y * img->width + (size_t)x) * 4u + (size_t)k] / 255.0f;
            }
        }
        if (!nrm) {
            // Flat normals, as the game makes them for a mesh without NORMAL.
            float e1[3], e2[3], f[3];
            for (k = 0; k < 3; k++) {
                e1[k] = p[1][k] - p[0][k];
                e2[k] = p[2][k] - p[0][k];
            }
            f[0] = e1[1] * e2[2] - e1[2] * e2[1];
            f[1] = e1[2] * e2[0] - e1[0] * e2[2];
            f[2] = e1[0] * e2[1] - e1[1] * e2[0];
            for (c = 0; c < 3; c++)
                memcpy(nn[c], f, sizeof(f));
        }
        for (c = 0; c < 3; c++) {
            float len = sqrtf(nn[c][0] * nn[c][0] + nn[c][1] * nn[c][1] + nn[c][2] * nn[c][2]);
            float d = (len > 0.0f) ? (nn[c][0] * light[0] + nn[c][1] * light[1] + nn[c][2] * light[2]) / len : 0.0f;
            float shade = 0.5f + 0.5f * (d > 0.0f ? d : 0.0f);
            u8 *r = &rec[c * 10];
            short s;
            for (k = 0; k < 3; k++) {
                s = RsHires_Coord(p[c][k]);
                r[k * 2] = (u8)(s & 0xff);
                r[k * 2 + 1] = (u8)((s >> 8) & 0xff);
            }
            for (k = 0; k < 3; k++)
                r[6 + k] = RsHires_Byte(q[c][k] * factor[k] * shade);
            r[9] = pad;
        }
        RsHires_Put(o, rec, sizeof(rec));
        o->triangles++;
    }
}

static void RsHires_DrawNode(const RldHiresModel *m, struct RsHiresTex *tex, int nodeIndex, struct RsHiresOut *o)
{
    const cgltf_node *node = &m->data->nodes[nodeIndex];
    float world[16];
    cgltf_size p;
    if (!node->mesh)
        return;
    if (!node->skin)
        cgltf_node_transform_world(node, world);
    for (p = 0; p < node->mesh->primitives_count; p++)
        RsHires_DrawPrimitive(m, tex, &node->mesh->primitives[p], node->skin ? NULL : world, o);
}

static void RsHires_Picture(const RldHiresModel *m, struct RsHiresCheck *out)
{
    static const u8 magic[8] = {'R', 'L', 'D', 'P', 'V', '2', 0, 0};
    struct RsHiresOut o;
    struct RsHiresTex *tex;
    u8 head[16];
    int i;

    // drawNodes is made only after the ranges and cgltf_validate passed: the
    // data can be read.
    if (!m->data || !m->drawNodes)
        return;
    memset(&o, 0, sizeof(o));
    tex = (struct RsHiresTex *)calloc((size_t)(m->imageCount > 0 ? m->imageCount : 1), sizeof(*tex));
    if (!tex)
        return;
    memcpy(head, magic, 8);
    RsHires_PutU32(head + 8, 3u);
    RsHires_PutU32(head + 12, 0u);   // the triangles of pose 0, written below
    RsHires_Put(&o, head, sizeof(head));
    for (i = 0; i < m->drawNodeCount; i++)
        RsHires_DrawNode(m, tex, m->drawNodes[i], &o);
    if (m->wheelNode[0] >= 0)
        for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
            RsHires_DrawNode(m, tex, m->wheelNode[i], &o);
    RsHires_PutU32(head, 0u);
    RsHires_Put(&o, head, 4);   // pose 1: empty
    RsHires_Put(&o, head, 4);   // pose 2: empty
    for (i = 0; i < m->imageCount; i++)
        RldPng_Free(&tex[i].png);
    free(tex);
    if (o.failed || o.triangles == 0) {
        free(o.bytes);
        return;
    }
    RsHires_PutU32(o.bytes + 12, (u32)o.triangles);
    out->preview = o.bytes;
    out->previewBytes = o.used;
    out->previewTriangles = o.triangles;
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
