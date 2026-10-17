// rs_hires_model.h - the model side of the page "Hi-Res (Preview)" (rs_hires.c)
//
// A hi-res driver model (.glb, the chunk CHRG of a .rldchar) checked with the
// game's own rules and turned into a picture of its bind pose for the 3D view.
// No window code: the console program rs_hires_selftest (rs_hires_test.c)
// links this file alone.
//
// THE CHECK IS NOT A SECOND ONE. RsHires_Check runs include/rldhires.inc -
// RldHires_Open and RldHires_CheckBudget, the very functions the game and
// rldpack hires-check run - in this process, so the list on the page shows the
// same findings with the same texts while the author picks files. Packing is
// rldpack's: the page starts `--rldpack hires-add` (RsHires_PackArgs), so the
// file the Studio writes is the file rldpack writes.
//
// THE PICTURE is a preview file "RLDPV2" in memory (the format rs_view.c reads,
// tools/reloadstudio/reloadstudio.h at "--preview"): every drawn triangle of
// the bind pose - the body nodes and, when all four are usable, the wheel
// nodes - with the glTF axes (+X the kart's left, +Y up, +Z forward), which
// are the preview's; 1 m = 64 model units = 1024 file units. Rigid nodes stand
// with their world matrix, skinned meshes as they are in mesh space (glTF
// ignores the node matrix of a skinned mesh; the bind pose is mesh space when
// the inverse bind matrices are those of the rest pose, as exporters write
// them). Behind it the same triangles TEXTURED ("RLDPN2", read by rs_tex.c,
// drawn by the view in place of the flat ones): per primitive its base colour
// image (sized up as the engine uploads it, bilinear like EngineStatic_ResampleUp,
// and sampled with the engine's hi-res filter - trilinear, anisotropy 16, in
// linear light: RS_TEX_MATERIAL_FILTERED, rs_tex.h), the colour the engine's "ns"
// program makes - texel x COLOR_0 x base colour factor, both factors on the
// display curve, COLOR_0 quantised to a byte as the engine holds it - lit by
// one fixed light from the front left above (half ambient, half diffuse): a
// picture to judge shape and paint by, not the engine's sun or normal maps.
// The flat RLDPV2 colours each corner by the texel at its UV (the fallback when
// the view cannot take the textures: more than RS_TEX_TEXTURES_MAX textures,
// RS_TEX_TRIANGLES_MAX triangles or RS_TEX_BUDGET of texture memory - then
// previewWhy says so). The first pose carries the triangles; the second holds
// the framing box (the bind pose's bounds and 0.25 m around them) as two
// triangles without area, so that the view frames every frame of a clip the
// same; the third is empty. The view draws pose 0 only.

#ifndef RS_HIRES_MODEL_H
#define RS_HIRES_MODEL_H

#include <stddef.h>
#include <wchar.h>

struct RsHiresAnim;   // the corners, skeleton and clips (rs_hires_model.c)

#define RS_HIRES_ROWS_MAX 64     // = RLDHIRES_REPORT_MAX
#define RS_HIRES_CLIPS 12        // = RLDHIRES_CLIP_COUNT
#define RS_HIRES_TEXT_MAX 256

// The state of one clip of the fixed list.
enum RsHiresClipState {
    RS_HIRES_CLIP_PRESENT = 0,   // in the file
    RS_HIRES_CLIP_FALLBACK,      // missing: clipVia is played (fullLock: at its end)
    RS_HIRES_CLIP_REST           // missing, no clip of its chain: the rest pose
};

struct RsHiresRow {
    int severity;                    // RS_SEV_ERROR / _WARNING / _NOTE (reloadstudio.h)
    wchar_t rule[8];                 // "HRG-16"
    wchar_t text[RS_HIRES_TEXT_MAX];
};

struct RsHiresCheck {
    int read;                        // 1 = the file was read (a .glb, or a .rldchar with a CHRG)
    int usable;                      // 1 = no error, the budget included: the game draws it hi-res
    int fromCharacter;               // 1 = read from the CHRG of a .rldchar (not a .glb to pack)
    int errors, warnings, notes;     // every finding (also those past the rows)
    int rowCount;
    int rowsDropped;
    struct RsHiresRow rows[RS_HIRES_ROWS_MAX];
    wchar_t why[RS_HIRES_TEXT_MAX];  // read == 0: why not

    // The summary.
    int clips;                       // clips in the file, of RS_HIRES_CLIPS
    int clipState[RS_HIRES_CLIPS];   // RS_HIRES_CLIP_*
    int clipVia[RS_HIRES_CLIPS];     // the clip played instead, -1 = rest pose
    int clipFullLock[RS_HIRES_CLIPS];
    float clipSeconds[RS_HIRES_CLIPS];
    int wheels;                      // usable wheel nodes found (0..4); 4 = the model's own wheels are drawn
    unsigned joints;                 // 0 = no skin
    unsigned long long triangles;
    unsigned long long vertices;
    int textures;                    // images a material uses
    unsigned long long textureBytes; // RGBA8 level 0, after the round-up
    unsigned textureEdge;            // the largest edge after the round-up
    float boundsMin[3], boundsMax[3];// bind pose, metres
    unsigned glbBytes;               // the size of the GLB

    // The budget (include/rldhires.inc).
    unsigned long long budgetTriangles;
    unsigned budgetEdge;
    unsigned long long budgetTextureBytes;

    // The picture of the bind pose (RLDPV2, malloc'ed; NULL when nothing could
    // be drawn). previewTriangles: triangles in pose 0.
    unsigned char *preview;
    size_t previewBytes;
    int previewTriangles;
    int previewTextured;             // 1 = the picture carries the textured model (RLDPN2), drawn by the view
    wchar_t previewWhy[RS_HIRES_TEXT_MAX];   // previewTextured 0: why only the flat picture
    size_t previewKeepAt;            // the part of every picture of this model that never changes (textures,
    size_t previewKeepBytes;         // materials): RsView_UpdatePreviewData leaves it alone

    // The clips (RsHires_AnimFrame): animReady 1 = the skeleton and the clips
    // were built; animWhy says why one could not be (a clip that cannot be
    // built plays as missing - its fallback).
    struct RsHiresAnim *anim;
    int animReady;
    wchar_t animWhy[RS_HIRES_TEXT_MAX];
};

// The name of a clip (L"idle" .. L"menu"), L"?" outside the list.
const wchar_t *RsHires_ClipName(int clip);

// Checks a file: a .glb, or the CHRG of a .rldchar (told by the first bytes).
// out is written in full; free it with RsHires_CheckFree. 1 = read (see
// usable for the verdict), 0 = not read (why).
int RsHires_CheckFile(const wchar_t *path, struct RsHiresCheck *out);

// The same for GLB bytes in memory (the self-test). The bytes are not kept.
int RsHires_CheckGlb(const unsigned char *glb, size_t size, struct RsHiresCheck *out);

void RsHires_CheckFree(struct RsHiresCheck *c);

// THE CLIPS. A frame of clip (RLDHIRES_CLIP_* order, -1 = the rest pose) at
// `seconds` of the preview, the wheels turned by wheelDegrees about their axle
// (+ rolls forward). The pose is made as the game makes it, with the
// functions of include/rldhires_anim.inc (RldAnim_SampleClip, _BlendPose,
// _NodeMatrices, _JointMatrix, _PaletteEntry, _SkinVertex): a missing clip
// plays its fallback (clipVia), a looped clip wraps, a clip played once runs
// and holds its end (the preview starts it again after a held second), a steer
// clip is the race's base state - idle looped, the steer clip scrubbed by a
// steer share that goes 0 -> 1 -> 0 every 2 s, blended over idle by that share
// - and a drift that falls back to its steer clip shows it at full lock.
// Writes the frame as RLDPV2 into a buffer of the check (valid until the next
// frame or RsHires_CheckFree); returns its triangles, 0 when the clips cannot
// play (animReady 0).
int RsHires_AnimFrame(struct RsHiresCheck *c, int clip, float seconds, float wheelDegrees, const unsigned char **bytes, size_t *size);
// A corner of the last frame (3 per triangle, in the order of the picture),
// metres and its normal. 1 = there is one.
int RsHires_AnimCorner(const struct RsHiresCheck *c, int index, float p[3], float n[3]);
// Where in what it plays a clip stands at `seconds` (the rule of
// RsHires_AnimFrame): its local seconds; *share (may be NULL) the steer share
// for a steer clip, -1 for any other. 0 for the rest pose.
float RsHires_AnimLocal(const struct RsHiresCheck *c, int clip, float seconds, float *share);
// The length in seconds of what a clip plays (its fallback's when it is
// missing); 0 for the rest pose.
float RsHires_AnimLength(const struct RsHiresCheck *c, int clip);

// 1 = the pictures of the next checks sample their textures nearest (point
// sampling, as the engine with --engine-hires-chars-filter nearest), 0 = the
// engine's hi-res filter (the default). For comparison images only.
void RsHires_SetPointSampling(int on);

// The arguments of the pack step, for Rs_RunRldpack: hires-add <character>
// <model> -o <out> --machine (--machine changes no byte of the file). args
// needs room for RS_HIRES_PACK_ARGS; returns the count.
#define RS_HIRES_PACK_ARGS 6
int RsHires_PackArgs(const wchar_t *character, const wchar_t *model, const wchar_t *out, const wchar_t **args);

// THE OPTION "Upscale small textures": before packing, rldpack hires-upscale
// <model> -o <derived> --machine writes a new model (the source is only read;
// tools/rldpack_hires_upscale.inc), and the pack step packs that one.
// RsHires_UpscaledPath: <folder of out>\<model name without .glb>-upscaled.glb;
// 0 when it does not fit cap (path is then empty).
#define RS_HIRES_UPSCALE_ARGS 5
int RsHires_UpscaleArgs(const wchar_t *model, const wchar_t *derived, const wchar_t **args);
int RsHires_UpscaledPath(const wchar_t *model, const wchar_t *out, wchar_t *path, int cap);

#endif
