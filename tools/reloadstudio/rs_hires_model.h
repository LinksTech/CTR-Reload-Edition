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
// them). A corner's colour is the base colour factor times the base colour
// texture at its UV (nearest texel, repeat), lit by one fixed light from the
// front left above (half ambient, half diffuse) - a picture to judge shape and
// paint by, not the engine's shading. The first pose carries the triangles,
// the other two of the format are empty: the view shows pose 0 only.

#ifndef RS_HIRES_MODEL_H
#define RS_HIRES_MODEL_H

#include <stddef.h>
#include <wchar.h>

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

// The arguments of the pack step, for Rs_RunRldpack: hires-add <character>
// <model> -o <out> --machine (--machine changes no byte of the file). args
// needs room for RS_HIRES_PACK_ARGS; returns the count.
#define RS_HIRES_PACK_ARGS 6
int RsHires_PackArgs(const wchar_t *character, const wchar_t *model, const wchar_t *out, const wchar_t **args);

#endif
