#ifndef PLATFORM_NATIVE_TWIN_H
#define PLATFORM_NATIVE_TWIN_H

// THE RETAIL TWIN (renderer plan D.4 step 4d, stage Z0; platform/native_twin.c).
//
// A measuring tool: the model a seat draws through the retail path (a retail
// driver, or a custom CMDL - the same format) made into the CPU arrays the GPU
// set of a custom character is built from (struct RldCharNative, read by
// NativeCharGpu_Build and NativeCharGpu_PoseVertices, include/rldchar.inc), so
// it can be drawn by the native path of step 4c and compared with the retail
// picture of the same frame. Its textures come from the VRAM mirror, decoded to
// RGBA8 once at a loading screen, in host memory only.
//
// THE RULES: plain C on host memory. It reads the model (header 0, its frames,
// command list, colours and texture layouts) and, when asked, the VRAM words it
// is handed - nothing else: no Instance, GameTracker, scratchpad, primMem or
// otMem, no coprocessor, no random numbers, no clock, nothing written outside
// the source it fills.
//
// WHAT IT REBUILDS of RenderBucket_DrawFunc_Normal and its decoders
// (game/RenderBucket/RenderBucket_QueueExecute.c):
//   frames    every logical frame of every animation of header 0 (or the one
//             still frame), decoded as the retail decoder does: raw records or
//             the delta bit stream with its temporal words, packed into the
//             GTE input (x, y, z) with the same 32-bit arithmetic and mask, and
//             the half frames of an animation with bit 15 as the sum of two
//             stored frames shifted by one (the retail in-between pose);
//   strips    the command list with the vertex FIFO of three, the strip start
//             (bit 31), the fan reuse (bit 30), the cached vertex (bit 26),
//             colour-only commands and the colour FIFO, the first triangle of
//             a strip keyed by its first command;
//   winding   a one-sided triangle (bit 28) is written in the order the native
//             back-face cull keeps exactly when the retail sign test (NCLIP
//             against bit 29, instance flag REVERSE_CULL_DIRECTION clear) keeps
//             it; a two-sided one (bit 28 clear) is written twice, once in each
//             order;
//   colours   per corner from the colour table or the scratchpad copy of it
//             (bit 27), RGB as the low 24 bits, alpha 255;
//   textures  per texture layout the page (x, y, depth), CLUT and the three UV
//             words; each (page, depth, CLUT) is a 256 x 256 tile, up to 16
//             tiles share one RGBA8 texture (4 across); a texel goes through
//             its CLUT to 15 bits and then (c & 31) << 3 per channel, the word
//             0x0000 is transparent (alpha 0), every other alpha 255.
// One twin vertex per triangle corner (UV and colour are per corner in the
// retail data), every pose a full copy: poses x 3 x triangles vertices.
//
// POSITIONS are in the model units of CNET: g x scale / 16384 per axis, g the
// GTE input value (so (pos + byte) x scale / 4096 for a full frame), with scale
// {mh->scale.x, mh->scale.y, (u16)mh->scale.z} - the factors the render layer
// divides by again (16384 / scale, NRL RouteChar), so the native matrix sees g.

#include <stddef.h>

#include <macros.h>
#include <rldtrack.inc>
#include <rldchar.inc>

struct Model;
struct ModelHeader;

#define NATIVE_TWIN_ANIMS_MAX 8
#define NATIVE_TWIN_POSES_MAX 256
#define NATIVE_TWIN_TILES_MAX 64
#define NATIVE_TWIN_TILES_PER_TEXTURE 16 // 4 across, 4 down: 1024 x 1024 at most
#define NATIVE_TWIN_TILE_EDGE 256
#define NATIVE_TWIN_COMMANDS_MAX 65536
#define NATIVE_TWIN_STREAM_MAX 8192      // vertex records one frame decodes
#define NATIVE_TWIN_SLOTS 256            // the cache slot field (command bits 16-23)

// struct NativeTwinMaterial.semi: how the retail packet is blended.
enum
{
	NATIVE_TWIN_SEMI_NONE = 0,  // POLY_G3 code 0x30, or POLY_GT3 code 0x34 (page mode 3): opaque
	NATIVE_TWIN_SEMI_MODE0 = 1, // POLY_GT3 code 0x36: texels with STP blended by page mode 0..3 (semi - 1)
};

// The retail look of one material (index = NativeCharRange.material), what the
// native draw needs beyond the CNET material: the shader input of step Z1.
struct NativeTwinMaterial
{
	u8 textured;   // 1 = POLY_GT3 (texture x colour x 2), 0 = POLY_G3 (colour)
	u8 semi;       // NATIVE_TWIN_SEMI_*: 0x36 packets are drawn opaque by the twin (V1) and counted
	u8 dither;     // POLY_G3: 1 always; POLY_GT3: bit 9 of its page word (the draw environment's dtd is OR'ed in at run time)
	u8 modulation; // 2 for textured, 1 for untextured (the PSX path: a_texcoord.z, platform/native_gpu.c)
	u8 animated;   // 1 = its layout table entry is cycled by the header's animtex (see NativeTwinSource.animatedLayouts)
};

// One (page, depth, CLUT) the model samples.
struct NativeTwinTile
{
	u16 tpage;      // the page word as the layout holds it
	u16 clut;       // the CLUT word (0 for 15 bits: not read)
	u8 depth;       // 0 4-bit, 1 8-bit, 2 15-bit
	u8 texture;     // which texture of the source
	u8 cellX;       // 256-texel cell inside it
	u8 cellY;
	u16 pageX;      // VRAM words
	u16 pageY;
	u16 clutX;
	u16 clutY;
	u32 triangles;      // twin triangles on this tile
	u32 semiTriangles;  // of them from 0x36 packets
	u32 stpTexels;      // texels with the STP bit under the UV triangles of those (after NativeTwin_DecodeVram)
	u32 clearTexels;    // words 0x0000 in the whole tile (transparent)
};

struct NativeTwinCount
{
	u32 commands;         // words between the count and 0xFFFFFFFF
	u32 colourOnly;       // commands with the upper 16 bits 0
	u32 records;          // vertices decoded per frame (commands without bit 26)
	u32 cachedVertices;   // commands with bit 26
	u32 strips;           // strip starts
	u32 events;           // places where the retail loop projects a triangle (before any cull)
	u32 oneSided;         // events with bit 28: one twin triangle
	u32 twoSided;         // events without it: two twin triangles
	u32 noTextureTable;   // events with a texture index but no table: drawn by nobody
	u32 untextured;       // events made POLY_G3 (index 0 or a null table entry)
	u32 textured;         // events made POLY_GT3
	u32 semi;             // of them 0x36 packets
	u32 colourCodeBits;   // events whose corner-0 colour word has bits 24-31 set (they reach the packet code)
	u32 cacheColours;     // colours read from the scratchpad copy (bit 27)
	u32 halfPastEnd;      // half frames whose next stored frame does not exist (the stored one is used)
	u32 animatedLayouts;  // texture layout table entries the header's animtex cycles
	u32 animatedOutside;  // animtex entries whose slot is not in the layout table (not followed)
	u32 animatedTriangles;// twin triangles drawn from a cycled entry: they show the build's layout only
	u32 animatedMaterials;// materials (and so material ranges) holding them
};

struct NativeTwinSource
{
	// The CPU arrays of a GPU set: state READY once the textures are decoded
	// (or at once without textures). No WHLS: the retail wheels stay on.
	// Materials: textured ones MASK (the transparent texel is not drawn),
	// untextured OPAQUE; every texture linear, nearest, clamped. The arrays
	// live in block.
	struct RldCharNative native;

	char model[17];       // the model's name, for the lines
	int fullHeight;       // as given: bit 0 of the height kept (NativeChar_ModelFullHeight)
	double unit[3];       // model units per GTE input unit
	u32 streamVertices;   // records per frame
	u32 *cornerVertex;    // native.vertexCount: the record behind each twin vertex
	u32 *triangleEvent;   // native.triangleCount: the event a twin triangle comes from
	u8 *triangleSide;     // native.triangleCount: 0 one-sided, 1 two-sided as listed, 2 two-sided reversed

	u32 animCount;        // 0 for a still model (one pose)
	u16 animFrames[NATIVE_TWIN_ANIMS_MAX]; // logical frames (numFrames & 0x7fff)
	u16 poseBase[NATIVE_TWIN_ANIMS_MAX];
	u8 animHalf[NATIVE_TWIN_ANIMS_MAX];    // bit 15 of numFrames
	int standardPoses;    // 4 animations of 21/7/15/4: NativeCharGpu_PoseIndex maps them alike

	struct NativeTwinMaterial material[RLDCHAR_NET_MATERIALS_MAX];

	u32 tileCount;
	struct NativeTwinTile tile[NATIVE_TWIN_TILES_MAX];
	u32 semiCount;        // UV triangles of the 0x36 triangles: tile, u0, v0, u1, v1, u2, v2
	u8 (*semiUv)[7];

	int texturesDecoded;
	u8 rgbaHash[32];      // SHA-256 over the RGBA8 of every texture in order

	struct NativeTwinCount count;
	u8 *block;            // owns every array above and native.cnet
};

// ANIMATED TEXTURES. Every frame CTR_CycleTex_AllModels (game/MAIN/MainFrame.c)
// writes, for a header with animtex and flags bit 1 clear, a new layout pointer
// into the header's layout table (game/CTR/CTR_CycleTex.c). The twin reads the
// table once, here: a triangle on such an entry keeps the layout of the build.
// They are not followed - they are counted (count.animated*), kept in
// materials of their own (material[m].animated) and named in the line, so a
// comparison can leave those ranges out or refuse.
//
// Header 0 of model (or mh): 1 and *out filled (native.state READY when the
// model samples no texture, else after NativeTwin_DecodeVram), or 0 with an
// English reason in why. Free with NativeTwin_Free either way. fullHeight: 1
// for a custom model whose file keeps odd heights (NativeChar_ModelFullHeight).
int NativeTwin_FromModel(const struct Model *model, int fullHeight, struct NativeTwinSource *out, char *why, size_t whySize);
int NativeTwin_FromHeader(const struct ModelHeader *mh, int fullHeight, struct NativeTwinSource *out, char *why, size_t whySize);

// The tiles of src from the VRAM mirror (VRAM_WIDTH x VRAM_HEIGHT u16, rows of
// 1024 words, platform/native_renderer.c s_vram.cpuPixels): RGBA8 into the
// textures of native, the STP and clear counts, the hash. 1 = native READY.
int NativeTwin_DecodeVram(const u16 *vram, struct NativeTwinSource *src);

void NativeTwin_Free(struct NativeTwinSource *src);

// The pose of a logical (animation, frame) as inst->animFrame names it:
// poseBase[anim] + frame, 0 for a still model. A frame past the end is held at
// the last one as retail does (RenderBucket_GetFrame); -1 for an unknown
// animation, a negative frame, or an odd frame past the end of a half-frame
// animation (retail would pair the last stored frame with what follows it).
int NativeTwin_PoseIndex(const struct NativeTwinSource *src, int anim, int frame);

// THE PAINT ORDER (step 4d, the twin only). Retail has no depth buffer: every
// triangle of the model goes into the ordering table of the instance, in the
// bin MAC0 >> 17 with MAC0 = ZSF3 x (SZ1 + SZ2 + SZ3), ZSF3 = 0x555
// (RenderBucket_CheckProjectedPrim gte_avsz3, RenderBucket_GetNormalOTEntry,
// RenderBucket_InitDepthGTE), linked in at the head of that bin
// (RenderBucket_LinkPrimRaw). The table is walked from the far end of the
// range to the near one (RenderBucket_AllocateOTRange links entry i + 1 to
// entry i), and a bin from its head: the far bins first, and in one bin the
// later command first. So the triangle that covers a pixel last is the one in
// the nearest bin, and among those the one earliest in the command list -
// whatever the true depths say.
//
// NativeTwin_PaintBin: that bin for three SZ values (each limited to 0..0xffff
// first, as the coprocessor does).
// NativeTwin_PaintOrder: the triangles of src in that order for pose pose and
// the screen matrix of the draw (struct NativeMeshDraw.screenFromModel, column
// major: w = elements 3, 7, 11, 15), one key per triangle into keys, the first
// to draw first; NATIVE_TWIN_PAINT_PLACE(key) is the triangle's place in the
// index buffer of NativeCharGpu_Build (its 3 indices start at 3 x place).
// Returns the number of keys (the triangle count), 0 when it cannot (no
// source, keyMax too small, a pose out of range, a bad material, a depth scale
// that is not above 0).
// SZ is floor(w x depthScale): depthScale = 2^mvpShift of the item, the
// queue's near (x 4) and DRAW_HUGE (/ 4) scale, handed over rather than told
// from the depth of the origin - for the mirror item of step 4e the last
// element of the matrix is not the view depth of the instance, while the
// queue scales both copies by that one. binLow <= binHigh: every bin is held
// to that range, as RenderBucket_GetClampedOTEntry does for the writer
// CLAMP_DEPTH (depthOffset of the view); binLow > binHigh: not held, as
// RenderBucket_GetNormalOTEntry. Only read; host memory only.
#define NATIVE_TWIN_ZSF3 0x555u
#define NATIVE_TWIN_PAINT_PLACE_MASK 0xFFFFFu
#define NATIVE_TWIN_PAINT_PLACE(key) ((u32)((key) & (u64)NATIVE_TWIN_PAINT_PLACE_MASK))
u32 NativeTwin_PaintBin(int sz0, int sz1, int sz2);
u32 NativeTwin_PaintOrder(const struct NativeTwinSource *src, u32 pose, const float screenFromModel[16], double depthScale, int binLow, int binHigh,
                          u64 *keys, u32 keyMax);

// THE CELLS OF A PAINT ORDER (one marker per occupied bin, the twin only): the
// keys of NativeTwin_PaintOrder cut into runs of one ordering-table cell each.
// The cell of a key is its bin held to [low, high] - the depthOffset of the
// view, the cells the instance owns in its range (retail writes a bin outside
// them unheld into the table memory around the range, which a marker cannot
// follow). The bins never rise along the keys, so every cell is one run and
// the runs come in paint order, far first. Per run: its first key, its key
// count and its cell; at most runMax runs are written. Returns the run count,
// runMax + 1 when there are more, 0 for no keys, low > high, or keys whose bins
// rise. *held: the keys whose bin lies outside [low, high].
u32 NativeTwin_PaintRuns(const u64 *keys, u32 keyCount, int low, int high, u32 *runFirst, u32 *runCount, s16 *runCell, u32 runMax, u32 *held);

// NativeTexDesc.flags every twin texture needs: linear data, nearest, and one
// level - the PSX samples no level, and with levels a texture shrunk at x1
// would read a mip. RldCharTexture.flags has no bit for the last one, so the
// upload has to add it (native_char_gpu.c takes the CTXT flags only).
u32 NativeTwin_TextureFlags(void);

// One English line (no newline) with the counts, poses, materials, tiles and,
// once decoded, the hash: "[CTR Twin] ...".
void NativeTwin_Describe(const struct NativeTwinSource *src, char *line, size_t size);

// --native-twin-selftest <folder> (ctest native_twin_selftest; main.c wants the
// folder, which holds the files of rldpack make-native-tests): a retail
// model made in memory with a VRAM made in memory through FromModel and
// DecodeVram (golden SHA-256 of the vertex bytes of every pose and of the
// RGBA8, and of the vertex bytes with fullHeight 1), the winding against a
// second walk of the list with the GTE FIFO rules, two refusals, an animtex
// case, NativeCharGpu_Build on the result; then old_plain.rldchar of the
// folder: every pose point equal to (pos + byte) x scale / 4096 of its record
// and inside RldChar_FrameHulls.
// 0 = passed.
int NativeTwin_SelfTest(const char *dir);

#endif
