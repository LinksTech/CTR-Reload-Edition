// rs_hires_model.h - the check and the picture of a hi-res model (page Character, routes Low-Poly and Hi-Res, rs_char.c)
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
// nodes, else the engine's standard wheels - with the glTF axes (+X the kart's left, +Y up, +Z forward), which
// are the preview's; 1 m = 64 model units = 1024 file units. Rigid nodes stand
// with their world matrix, skinned meshes as they are in mesh space (glTF
// ignores the node matrix of a skinned mesh; the bind pose is mesh space when
// the inverse bind matrices are those of the rest pose, as exporters write
// them). Behind it the same triangles TEXTURED ("RLDPN2", read by rs_tex.c,
// drawn by the view in place of the flat ones): per primitive its base colour
// image (sized up as the engine uploads it, bilinear like EngineStatic_ResampleUp,
// and sampled with the engine's hi-res filter - trilinear, anisotropy 16, in
// linear light: RS_TEX_MATERIAL_FILTERED, rs_tex.h), the colour the engine's "ns"
// program makes - texel x the vertex colour factor x base colour factor, both
// factors on the display curve - lit with the light the engine draws a hi-res
// driver with (THE LIGHT below), so that the preview is as bright as the game.
// THE VERTEX COLOUR FACTOR is RldHiresColor_Factor (include/rldhires_color.inc)
// of COLOR_0, quantised to a byte as the engine holds it, in the mode the
// picture is drawn in (vcolMode, RsHires_SetColourMode) - the one function the
// engine calls too; a primitive without COLOR_0 has the factor 1 in every mode.
// The textured materials carry RS_TEX_MATERIAL_LIT (rs_tex.h): per pixel
// min(texel x factor x base colour factor, white) x the light, as the engine
// clamps PSX blend before its light; the factor (up to 255/128 in PSX blend)
// and the light (up to about 1.35) are bytes of 1/128 per corner.
// THE LIGHT is the engine's for a hi-res driver (platform/engine_hires_char.c,
// EngHc_Light, the constants shared in include/rldhires_color.inc): a
// hemisphere ambient from below to above by 0.5 + 0.5 n.y, plus the sun's
// diffuse share x max(0, n . sun), the sun in the axes of the model (the kart
// faces +Z as on a straight to the north). Per corner (the engine per pixel),
// without the normal maps. A file in PSX blend is drawn unlit by the engine
// (its shading is in COLOR_0, as on the PS1): its picture has no light.
// THE STANDARD WHEELS: a model without its four wheel nodes carries the
// engine's standard 3D wheels in its picture (rs_hires_model.c), so the page
// shows no wheels of the view's own under it.
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
    // The base colour textures as the file has them, BEFORE the round-up (a
    // 40 x 40 texture is 40 here, 64 in textureEdge; normal maps are not
    // counted): what "Upscale small textures" is suggested by
    // (RsCharHires_SuggestUpscale, rs_charroute.h). The images are those
    // rldpack hires-upscale looks at: every image a material names as its
    // base colour texture.
    int baseTextures;                // images a material uses as base colour
    int baseUnsized;                 // of them, those whose size could not be read (not an embedded PNG)
    unsigned baseEdgeFile;           // the largest width or height of them in the file, 0 = none
    // The textures a material uses (base colour or normal map) whose bytes are
    // a JPEG - the ones rule HRG-16 refuses and "Convert JPEG textures to PNG"
    // converts (rldpack hires-png, rs_charroute.h THE JPEG TEXTURES).
    int jpegTextures;
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

    // THE VERTEX COLOURS (include/rldhires_color.inc). vcolPresent: a drawn
    // primitive has COLOR_0 (only then the page offers "Vertex color
    // blending"); vcolPs1: the check found them PS1-like (it said HRG-36,
    // its median of COLOR_0 on textured faces vcolMedian, -1 = none of them
    // there); vcolMode: the mode (RS_HIRES_VCOL_*) the picture is drawn in -
    // after a check of a .glb RS_HIRES_VCOL_GLTF (the mode of a pack without
    // the switch), of a .rldchar the mode in its CHRG flags.
    int vcolPresent;
    int vcolPs1;
    int vcolMedian;
    int vcolMode;
    int vcolChecked;                 // the mode the check ran in (HRG-36 is said in glTF only); vcolMode may change after it

    // THE KART (include/rldhires.inc, THE PARTS): how a .glb is packed.
    // kartCase RS_KART_PART: a node "kart" with a mesh in its subtree that can
    // be left out (no vertex weighted inside and outside it) - packed with
    // --kart-part, the podium, the menu and the driver select leave it out.
    // RS_KART_GAME: a node "kart" without any mesh in its subtree (the kart
    // joint of the Creator Kit's template) and no mesh under a node "kart",
    // "kart_*" or "wheel_*" anywhere - the suggestion "Kart: from the player's
    // game data" (--game-kart template). RS_KART_OWN: anything else (an own
    // kart without the convention, an old single mesh) - packed as before.
    // Of a .rldchar: what its parts field says. parts: the field the check
    // ran with (RS_HIRES_PARTS_*; 0 = none).
    int kartCase;
    unsigned parts;
};

enum RsKartCase { RS_KART_OWN = 0, RS_KART_PART = 1, RS_KART_GAME = 2 };
// The parts field of a pack (RLDHIRES_PART_* of include/rldhires.inc):
// --kart-part, and --game-kart template (bit 1, donor 0xFF = the CHRI template).
#define RS_HIRES_PARTS_KART      0x1u
#define RS_HIRES_PARTS_GAME_KART (0x2u | (0xffu << 8))

// The vertex colour modes, RLDHIRES_VCOL_* of include/rldhires_color.inc
// (rs_hires_model.c checks that they are the same numbers).
#define RS_HIRES_VCOL_GLTF 0
#define RS_HIRES_VCOL_PSX 1
#define RS_HIRES_VCOL_OFF 2
#define RS_HIRES_VCOL_MODES 3

// The word of a mode for rldpack hires-add --vertex-colors and the automation
// ("gltf", "psx", "off"); NULL outside the modes. RsHires_ColourModeOf: the
// mode of a word, -1 when it is none.
const wchar_t *RsHires_ColourWord(int mode);
int RsHires_ColourModeOf(const wchar_t *word);

// Draws the picture of a check again in another vertex colour mode: the
// preview (the bind pose) is written anew with the colours of that mode, and
// so is every frame of a clip after it. The textures and materials stay the
// same bytes (previewKeepAt/Bytes), so the view takes the new picture with
// RsView_UpdatePreviewData. 1 = the mode changed (the picture is new), 0 =
// it was that mode already, or not a mode.
int RsHires_SetColourMode(struct RsHiresCheck *c, int mode);

// One channel of the picture as the engine makes it, for the self-test: the
// base colour texel (a byte on the display curve), COLOR_0 (linear 0..1; has0
// 0 = the primitive has none), the material's base colour factor (linear) and
// the light (glTF and off; PSX blend is drawn unlit, the light is not used),
// in a mode -> the byte of the picture (the flat RLDPV2 and the view's
// RsTex_ShadeLit alike), and the corner colour C of the textured one (the
// byte of RS_TEX_MATERIAL_LIT, C / 128).
int RsHires_PreviewChannel(int mode, int has0, float colour0, unsigned texel, float factor, float light, int *corner);

// The light of a corner with normal n (need not be unit; 0 0 0 = straight up)
// as the engine lights a hi-res driver (THE LIGHT above).
float RsHires_Light(const float n[3]);

// The name of a clip (L"idle" .. L"menu"), L"?" outside the list.
const wchar_t *RsHires_ClipName(int clip);

// Checks a file: a .glb, or the CHRG of a .rldchar (told by the first bytes).
// out is written in full; free it with RsHires_CheckFree. 1 = read (see
// usable for the verdict), 0 = not read (why).
int RsHires_CheckFile(const wchar_t *path, struct RsHiresCheck *out);

// THE LOOK OF A CHARACTER FILE: the shadow and the exhaust its CHRI holds,
// read as the game reads them (RldChar_ParseInfo, RldChar_ParseLook in
// include/rldchar.inc - a field the game does not take counts as retail).
// shadow and exhaust: RS_CHAR_LOOK_RETAIL, _OWN (shadow: the quad, exhaust:
// count points) or _OFF; quad x0 x1 z0 z1 and the points x y z in 1/16 model
// units. 1 = read; 0 = not a character that can be read, why says so.
#define RS_CHAR_LOOK_RETAIL 0
#define RS_CHAR_LOOK_OWN 1
#define RS_CHAR_LOOK_OFF 2
struct RsCharLook {
    int shadow, exhaust, count;
    int quad[4];
    int point[2][3];
};
int RsHires_CharLook(const wchar_t *path, struct RsCharLook *out, wchar_t *why, int whyCap);

// The same for GLB bytes in memory (the self-test). The bytes are not kept.
int RsHires_CheckGlb(const unsigned char *glb, size_t size, struct RsHiresCheck *out);

void RsHires_CheckFree(struct RsHiresCheck *c);

// The game's budget of a hi-res driver (RLDHIRES_BUDGET_* of
// include/rldhires.inc): triangles, the largest texture edge, texture bytes.
// Any pointer may be NULL.
void RsHires_Budget(unsigned long long *triangles, unsigned *edge, unsigned long long *textureBytes);

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
// <model> -o <out> [--vertex-colors <word>] --machine (--machine changes no
// byte of the file). vertexColors: a mode (RS_HIRES_VCOL_*) for the switch,
// -1 = no switch (rldpack then writes mode glTF, the bytes of before). args
// needs room for RS_HIRES_PACK_ARGS; returns the count.
#define RS_HIRES_PACK_ARGS 10
int RsHires_PackArgs(const wchar_t *character, const wchar_t *model, const wchar_t *out, int vertexColors, const wchar_t **args);
// The same with the parts field: RS_HIRES_PARTS_KART adds --kart-part,
// RS_HIRES_PARTS_GAME_KART --game-kart template (before --machine), 0 nothing -
// the arguments of RsHires_PackArgs.
int RsHires_PackArgsParts(const wchar_t *character, const wchar_t *model, const wchar_t *out, int vertexColors,
                          unsigned parts, const wchar_t **args);
// The parts field a check's kart case packs with (RS_KART_PART: the kart part,
// RS_KART_GAME: the game's kart, else 0), or the game's kart when gameKart is
// 1 (the switch "Kart: from the player's game data") and the model has no kart part.
unsigned RsHires_PartsFor(int kartCase, int gameKart);
// A .glb checked with this parts field (the page's choice: the game's kart on
// a model without the kit's joint); kartCase stays what the model is.
int RsHires_CheckFileParts(const wchar_t *path, unsigned parts, struct RsHiresCheck *out);

// THE OPTION "Upscale small textures": before packing, rldpack hires-upscale
// <model> -o <derived> --machine writes a new model (the source is only read;
// tools/rldpack_hires_upscale.inc), and the pack step packs that one.
// RsHires_UpscaledPath: <folder of out>\<model name without .glb>-upscaled.glb;
// 0 when it does not fit cap (path is then empty).
#define RS_HIRES_UPSCALE_ARGS 5
int RsHires_UpscaleArgs(const wchar_t *model, const wchar_t *derived, const wchar_t **args);
int RsHires_UpscaledPath(const wchar_t *model, const wchar_t *out, wchar_t *path, int cap);

// THE OPTION "Convert JPEG textures to PNG": before packing (and before the
// upscale), rldpack hires-png <model> -o <derived> --machine writes a new
// model whose JPEG textures are PNG (the source is only read;
// tools/rldpack_hires_png.inc); the next step takes that one.
// RsHires_PngPath: <folder of out>\<model name without .glb>-png.glb; 0 when
// it does not fit cap (path is then empty).
#define RS_HIRES_PNG_ARGS 5
int RsHires_PngArgs(const wchar_t *model, const wchar_t *derived, const wchar_t **args);
int RsHires_PngPath(const wchar_t *model, const wchar_t *out, wchar_t *path, int cap);

// The same conversion in this process, for the check of the page Character
// (only Reload Studio has it: rs_rldpack.c, where rldpack's own code is): reads
// in, writes the bytes rldpack hires-png writes to out. 1 = written; jpeg
// (may be NULL) = the JPEG textures found, converted (may be NULL) = those
// converted (0 for a model without JPEG: then out is a copy). 0 = nothing
// written, why says why in rldpack's words.
int Rs_HiresPngFile(const wchar_t *in, const wchar_t *out, int *jpeg, int *converted, wchar_t *why, int whyCap);

#endif
