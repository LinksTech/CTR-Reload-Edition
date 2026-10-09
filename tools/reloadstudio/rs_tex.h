// rs_tex.h - the native model's textures and colours for the 3D preview
//
// rldpack writes the native model of a character into the --preview file
// ("RLDPN3" - every frame, the end poses, corners indexed - or the older
// "RLDPN2" of the turn frames 10, 0, 20 with corners per triangle; THE NATIVE
// MODEL IN THE PREVIEW in tools/rldpack_native.inc) and
// an author's wheel into the preview file of char-wheel ("RLDPW3", the head of
// tools/rldpack_wheel.inc). rs_tex.c reads both into the structures below and
// gives the view (rs_view.c, which keeps the rasterizer) what the game's "nr"
// program does per pixel (platform/native_shaders.inc, the "nr" fragment
// shader; platform/native_char_gpu.c feeds it):
//
//   T = the texel of mip level L, nearest, wrapped as CTXT says
//   textured:   out = (T * C * M + 32512) / 65025          per channel
//   untextured: out = (C * M + 127) / 255
//   alpha mode mask: left out when 2 * T.a * C.a * M.a < 255^3
//                    (untextured: 2 * C.a * M.a < 255^2)
//
// C is the corner colour (COL0 of CNET, 255 without; a wheel's always 255),
// interpolated perspective correct by the caller; M the material's colour.
// No light, no factor 2, no dither, no gamma: the "nr" program has none of
// them. An sRGB texture comes back from the shader's round trip as its byte
// (platform/native_tex.c, the shader round trip of its self-test), so T is
// the stored byte. The game's float arithmetic can differ by 1 at an exact
// .5 of a channel (the test of rs_tex counts how often).
//
// THE LEVELS are the game's: include/rldmip.inc builds them from level 0 when
// a file is read - the same code platform/native_tex.c uploads them with
// (golden hashes in the self-tests of the game and of rldpack). The level of
// a pixel follows the Vulkan rule for VK_SAMPLER_MIPMAP_MODE_NEAREST, the
// game's default (nearest filter, nearest level): with rho the larger length
// of the screen derivatives of (u x width, v x height) in texels per pixel,
// level 0 while rho <= 2^0.5, else the k with 2^(k - 0.5) < rho <= 2^(k + 0.5),
// at most the last level. In integers: the largest k >= 1 with
// rho^2 > 2^(2k - 1), else 0 - no logarithm, no float.
//
// HOW THE RASTERIZER USES IT (one texel lookup and one shade per pixel):
//   once per file:     RsTex_ParseNative / RsTex_ParseWheel; RsTex_Free* at drop
//   once per triangle: the material (RsTex_NativeMaterialOf), its image
//                      (NULL without a texture) and the level:
//                      RsTex_TriangleSlopes with the projected corners, then
//                      RsTex_Level - the derivatives at the centroid (cheap,
//                      deterministic). The GPU takes them per 2 x 2 pixels;
//                      for that, the caller passes the differences of its own
//                      u and v (Q16) to the right and below the pixel to
//                      RsTex_Level instead - same rule, more work.
//   per pixel:         T = RsTex_Texel(img, level, u, v), then
//                      RsTex_Shade(T or NULL, C, material) -> 0x00RRGGBB or
//                      RS_TEX_DISCARD.
// u and v are Q16 of the texture (65536 = once across), v from the top as in
// CTXT; positions are 1/16 game units as in RLDPV2.
//
// MEMORY. Level 0 stays in the caller's bytes (the file as read; it must live
// as long as the structure); the levels below it are allocated here. A native
// model may need at most RS_TEX_BUDGET with all levels (the game's budget of
// one driver, CTXT-6); a file over it is refused with the numbers. Nothing in
// here prints, draws or depends on the view; no clock, no float.

#ifndef RS_TEX_H
#define RS_TEX_H

#include <stddef.h>
#include <wchar.h>

#define RS_TEX_TEXTURES_MAX 16                  // RLDCHAR_TEX_COUNT_MAX
#define RS_TEX_MATERIALS_MAX 64                 // RLDCHAR_NET_MATERIALS_MAX
#define RS_TEX_TRIANGLES_MAX 30000              // RLDCHAR_NET_TRIANGLES_MAX
#define RS_TEX_VERTICES_MAX 65536               // the u16 corner index of RLDPN3 (the game's own limit is lower)
#define RS_TEX_WHEEL_TRIANGLES_MAX 1024         // RLDCHAR_WHEEL_TRIANGLES_MAX
#define RS_TEX_LEVELS_MAX 12                    // RLDMIP_LEVELS_MAX: 2048 down to 1
#define RS_TEX_EDGE_MIN 16                      // RLDCHAR_TEX_EDGE_MIN..MAX, powers of two
#define RS_TEX_EDGE_MAX 2048
#define RS_TEX_WHEEL_EDGE_MAX 1024              // RLDCHAR_WHEEL_TEXTURE_EDGE_MAX
#define RS_TEX_BUDGET (64u * 1024u * 1024u)     // RLDCHAR_TEX_GPU_BYTES_MAX: RGBA8 with every level
#define RS_TEX_POSES 3                          // RLDPN2: the turn frames 10, 0 and 20, as RLDPV2
#define RS_TEX_FRAMES 47                        // RLDPN3: every frame of the four animations
#define RS_TEX_POSES_MAX RS_TEX_FRAMES
#define RS_TEX_NEUTRAL_FRAME 10                 // turn frame 10: the pose the end poses start from
#define RS_TEX_END_WIN 0                        // the end poses (MRPH): RsTexNative.end
#define RS_TEX_END_LOSE 1

// The layout of a triangle in the files, for the accessors below.
#define RS_TEX_NATIVE_POS_BYTES 18              // RLDPN2 per triangle and pose: 3 x s16 x, y, z
#define RS_TEX_NATIVE_TRI_BYTES 40              // RLDPN2: 3 x {s32 u, s32 v, u8 r, g, b, a}, u16 material, u16 0
#define RS_TEX_NATIVE_POS3_BYTES 6              // per corner (RLDPN3: per vertex) and pose: s16 x, y, z
#define RS_TEX_NATIVE_VERTEX_BYTES 12           // RLDPN3 per vertex: s32 u, s32 v, u8 r, g, b, a
#define RS_TEX_NATIVE_INDEX_BYTES 8             // RLDPN3 per triangle: u16 a, b, c, material
#define RS_TEX_WHEEL_TRI_BYTES 44               // 3 x {s16 x, y, z, s32 u, s32 v}, u8 flags, u8 0

#define RS_TEX_NATIVE_BLEND 0x1u                // RsTexNative.flags: a blend material - the game draws the classic model
#define RS_TEX_DISCARD (-1)                     // RsTex_Shade: the mask leaves the pixel out

enum { RS_TEX_WRAP_REPEAT = 0, RS_TEX_WRAP_CLAMP = 1, RS_TEX_WRAP_MIRROR = 2 };
enum { RS_TEX_ALPHA_OPAQUE = 0, RS_TEX_ALPHA_MASK = 1, RS_TEX_ALPHA_BLEND = 2 };

// One texture with its levels. Every edge is a power of two, so a level's
// texel is found by shifts: level l is (w >> l) x (h >> l), at least 1.
struct RsTexImage {
    int w, h;                                   // level 0
    unsigned int flags;                         // CTXT flags (RLDCHAR_TEX_*)
    int linear;                                 // flag linear: the levels average the stored bytes, not light
    int wrapU, wrapV;                           // RS_TEX_WRAP_*
    int levels;                                 // the full chain: 1 + log2 of the longer edge
    const unsigned char *level[RS_TEX_LEVELS_MAX];   // RGBA, rows from the top; [0] in the file
    int shiftW[RS_TEX_LEVELS_MAX];              // log2 of each level's width and height
    int shiftH[RS_TEX_LEVELS_MAX];
    unsigned char *chain;                       // owned: levels 1 .. levels - 1
    size_t chainBytes;                          // every level, level 0 included (the budget's count)
};

// A material as MATL of CNET has it.
struct RsTexMaterial {
    unsigned char rgba[4];                      // the tint M
    int texture;                                // index into the textures, -1 none
    int alphaMode;                              // RS_TEX_ALPHA_*
    int nearest;                                // the material's flag "always nearest" (the preview is nearest anyway)
};

// One corner as RsTex_NativeCorner and RsTex_WheelCorner give it.
struct RsTexCorner {
    long long u, v;                             // Q16 of the texture, v from the top
    int r, g, b, a;                             // C: COL0, or 255 (a wheel: always 255)
};

// The native model of a preview file (RLDPN3 or RLDPN2). Positions are
// per corner: a vertex of RLDPN3, or corner c of triangle t of RLDPN2 as the
// vertex 3 t + c (RsTex_NativeVertex) - so pos[p] and end[e] hold vertices x
// RS_TEX_NATIVE_POS3_BYTES either way.
struct RsTexNative {
    unsigned int flags;                         // RS_TEX_NATIVE_BLEND
    int textureCount;
    struct RsTexImage tex[RS_TEX_TEXTURES_MAX];
    int materialCount;
    struct RsTexMaterial mat[RS_TEX_MATERIALS_MAX];
    int triangles;
    int vertices;                               // RLDPN3: its vertices; RLDPN2: 3 x triangles
    int indexed;                                // 1 = RLDPN3
    int poses;                                  // RLDPN3: RS_TEX_FRAMES, or 1 (a still model); RLDPN2: RS_TEX_POSES
    int neutral;                                // the pose of turn frame 10 (RLDPN3: 10 or 0; RLDPN2: 0)
    const unsigned char *pos[RS_TEX_POSES_MAX]; // vertices x RS_TEX_NATIVE_POS3_BYTES each, in the file
    const unsigned char *end[2];                // win, lose (absolute positions as pos); NULL = none
    const unsigned char *tris;                  // RLDPN2: triangles x RS_TEX_NATIVE_TRI_BYTES, in the file
    const unsigned char *vtx;                   // RLDPN3: vertices x RS_TEX_NATIVE_VERTEX_BYTES
    const unsigned char *idx;                   // RLDPN3: triangles x RS_TEX_NATIVE_INDEX_BYTES
    size_t bytes;                               // the textures with all their levels
};

// An author's wheel (RLDPW3): one material, its texture or none.
struct RsTexWheel {
    int textured;
    struct RsTexImage tex;
    struct RsTexMaterial mat;                   // texture 0 when textured, else -1
    int triangles;
    const unsigned char *tris;                  // triangles x RS_TEX_WHEEL_TRI_BYTES, in the file
};

// The native model behind the classic data: data[at..bytes) must be exactly
// one RLDPN3 or RLDPN2 block. 1 = read, the levels built; 0 = refused, why says why in
// English (lower case, no full stop, as the view's other reasons) and out
// holds nothing. data must outlive out.
int RsTex_ParseNative(const unsigned char *data, size_t bytes, size_t at, struct RsTexNative *out, wchar_t *why, int whyCap);
void RsTex_FreeNative(struct RsTexNative *n);

// A whole RLDPW3 file. As RsTex_ParseNative.
int RsTex_ParseWheel(const unsigned char *data, size_t bytes, struct RsTexWheel *out, wchar_t *why, int whyCap);
void RsTex_FreeWheel(struct RsTexWheel *w);

// Triangle tri of the native model: its material (an index into mat), the
// vertex of its corner c (0..2; the index into pos), that corner's UV and
// colour, and its position in pose (0..poses - 1), 1/16 game units.
int RsTex_NativeMaterialOf(const struct RsTexNative *n, int tri);
int RsTex_NativeVertex(const struct RsTexNative *n, int tri, int c);
void RsTex_NativeCorner(const struct RsTexNative *n, int tri, int c, struct RsTexCorner *out);
void RsTex_NativePosition(const struct RsTexNative *n, int pose, int tri, int c, int xyz[3]);
// The same of an end pose (RS_TEX_END_*), which must be there (end[e] != NULL).
void RsTex_NativeEndPosition(const struct RsTexNative *n, int e, int tri, int c, int xyz[3]);

// Corner c of wheel triangle tri: position (1/16 game units, the wheel's own
// axes) and UV; the colour is 255. TwoSided: flags bit 0 (char-wheel writes 0).
void RsTex_WheelCorner(const struct RsTexWheel *w, int tri, int c, int xyz[3], struct RsTexCorner *out);
int RsTex_WheelTwoSided(const struct RsTexWheel *w, int tri);

// The image a material samples, NULL without one.
const struct RsTexImage *RsTex_NativeImage(const struct RsTexNative *n, int material);

// The mip level (Vulkan NEAREST rule above) from the screen derivatives of u
// and v, Q16 of the texture per pixel: d(u)/dx, d(v)/dx, d(u)/dy, d(v)/dy.
// 0 for NULL or a texture of one level.
int RsTex_Level(const struct RsTexImage *img, long long dudx, long long dvdx, long long dudy, long long dvdy);

// The four derivatives of a triangle at its centroid, perspective correct:
// corners x, y in 1/16 pixels (within +-2^20), z the depth weight the
// rasterizer interpolates u and v with (proportional to 1/distance, > 0), u and
// v in Q16. slope = {du/dx, dv/dx, du/dy, dv/dy} in Q16 per pixel; all 0 for a
// triangle without area. For RsTex_Level.
void RsTex_TriangleSlopes(const long long x[3], const long long y[3], const long long z[3], const long long u[3], const long long v[3],
                          long long slope[4]);

// One axis of a texel coordinate: q in Q16 of the texture, the level's edge
// 1 << shift, the wrap of CTXT. Nearest: floor(q x edge / 65536), then repeat,
// clamp to the edge or mirror (Vulkan's REPEAT, CLAMP_TO_EDGE,
// MIRRORED_REPEAT).
static inline int RsTex_Coord(long long q, int shift, int wrap)
{
    const long long n = 1LL << shift;
    const long long t = q * n;
    const long long i = t >= 0 ? t >> 16 : -((-t + 65535) >> 16);   // floor
    if (wrap == RS_TEX_WRAP_CLAMP)
        return i < 0 ? 0 : (i >= n ? (int)(n - 1) : (int)i);
    if (wrap == RS_TEX_WRAP_MIRROR) {
        const long long m = (long long)((unsigned long long)i & (unsigned long long)(2 * n - 1));
        return (int)(m < n ? m : 2 * n - 1 - m);
    }
    return (int)((unsigned long long)i & (unsigned long long)(n - 1));
}

// The texel (RGBA) at u, v (Q16) in level (0 .. img->levels - 1, clamped).
static inline const unsigned char *RsTex_Texel(const struct RsTexImage *img, int level, long long u, long long v)
{
    int x, y;
    if (level < 0)
        level = 0;
    if (level >= img->levels)
        level = img->levels - 1;
    x = RsTex_Coord(u, img->shiftW[level], img->wrapU);
    y = RsTex_Coord(v, img->shiftH[level], img->wrapV);
    return img->level[level] + (((size_t)y << img->shiftW[level]) + (size_t)x) * 4;
}

// The "nr" program for one pixel: texel T (NULL untextured), corner colour C
// (0..255 each, interpolated), material m. 0x00RRGGBB, or RS_TEX_DISCARD when
// the material is a mask (or blend, which the game never draws natively) and
// the alpha product is below one half.
static inline int RsTex_Shade(const unsigned char *texel, int cr, int cg, int cb, int ca, const struct RsTexMaterial *m)
{
    unsigned int r, g, b;
    if (texel) {
        if (m->alphaMode != RS_TEX_ALPHA_OPAQUE && 2u * texel[3] * (unsigned int)ca * m->rgba[3] < 16581375u)
            return RS_TEX_DISCARD;
        r = (texel[0] * (unsigned int)cr * m->rgba[0] + 32512u) / 65025u;
        g = (texel[1] * (unsigned int)cg * m->rgba[1] + 32512u) / 65025u;
        b = (texel[2] * (unsigned int)cb * m->rgba[2] + 32512u) / 65025u;
    } else {
        if (m->alphaMode != RS_TEX_ALPHA_OPAQUE && 2u * (unsigned int)ca * m->rgba[3] < 65025u)
            return RS_TEX_DISCARD;
        r = ((unsigned int)cr * m->rgba[0] + 127u) / 255u;
        g = ((unsigned int)cg * m->rgba[1] + 127u) / 255u;
        b = ((unsigned int)cb * m->rgba[2] + 127u) / 255u;
    }
    return (int)((r << 16) | (g << 8) | b);
}

#endif
