/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gpu/PsyX_GPU.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include <ctr_subpixel.h>
#include "platform/native_gpu.h"

#include <platform.h>

#include <SDL3/SDL.h>

#include "platform/native_log.h"
#include "platform/native_perf.h"
#include "platform/native_renderer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Platform_PollHostEvents(void);
int Platform_GetVBlankCount(void);
void NativeGpu_ApplyUIMapping(void);
extern int g_cfg_bilinearFiltering;
extern int g_dbg_emulatorPaused;
extern int g_dbg_polygonSelected;

// How often the split table overflowed, and in how many batches.
//
// An overflowed primitive does not get a split of its own: it falls into
// the one before and is drawn with THAT one's state - wrong texture,
// wrong blend mode. That is a fault in the picture, not only a cost,
// so the total belongs in a place where somebody sees it.
global_variable u64 s_gpuSplitOverflows = 0;
global_variable u32 s_gpuSplitOverflowRuns = 0;
global_variable int s_gpuSplitOverflowSaid = 0;

#define NATIVE_GPU_LOG(fmt, ...)   Platform_Log("[CTR GPU] " fmt, __VA_ARGS__)
#define NATIVE_GPU_ERROR(fmt, ...) Platform_LogError("[CTR GPU] [%s] - " fmt, __func__, __VA_ARGS__)

// NOTE(aalhendi): Little-endian tag `CTRG` = CTR native GPU snapshot.
#define NATIVE_GPU_STATE_MAGIC     0x47525443
#define NATIVE_GPU_STATE_VERSION   1

#define GET_TPAGE_BLEND(tpage)     ((BlendMode)(((tpage >> 5) & 3) + 1))

#define GET_TPAGE_DITHER(tpage)    ((tpage >> 9) & 0x1)

#define GET_CLUT_X(clut)           ((clut & 0x3F) << 4)
#define GET_CLUT_Y(clut)           (clut >> 6)

internal TexFormat GetTPageFormat(int tpage)
{
	const int mode = (tpage >> 7) & 0x3;

	// NOTE(aalhendi): ctr-native local divergence from upstream PsyCross. PS1
	// mode 3 is reserved; TF_32_BIT_RGBA is only for explicit native override
	// textures, not raw retail TPAGE mode bits.
	return mode == 3 ? TF_16_BIT : (TexFormat)mode;
}

internal short GetTPageBase(int tpage)
{
	const u16 page = (u16)tpage;

	// NOTE(aalhendi): ctr-native local divergence for CTR retail emitters. The
	// shader wants xPage + yPage * 16, not raw draw-mode bits.
	return (s16)((page & 0xf) | ((page & 0x10) ? 0x10 : 0));
}

// THE FORMAT TRAVELS IN THE VERTEX.
//
// GrVertex.page is an s16 and carried the page base 0..31 - five bits, ten
// free. Bits 5-6 now carry the texture format (0 = 4 bit, 1 = 8 bit, 2 = 16
// bit), the vertex shader separates the two again (floor(z / 32)). With that the
// format is no longer a property of the split: AddSplit does not compare it, a
// split may carry all three, and the one fragment shader branches per
// triangle. Vertex size unchanged, ring and batches too.
//
// Counted per primitive: a zero for one format would be a silent failure of
// the route, not a track without such textures.
global_variable unsigned long long s_gpuPrimsByFormatWindow[3];

// MEASUREMENT: which PS1 blend mode the textured semi-transparent
// primitives use (0 average, 1 addition, 2 subtraction, 3 quarter). Counted per
// primitive, in AddSplit, where the mode is fixed.
global_variable unsigned long long s_gpuSemiByModeWindow[4];

// Whether the device has a second blend source (native_gfx_vk.c).
extern int g_gfx_dualSourceBlend;

// --semi-two-pass, the bit-identical two-pass form for the picture comparison
// (main.c). Default off.
int g_cfg_semiTwoPass = 0;

// ANTI-ALIASING: whether textured draws get sample shading.
// Set by native_gfx_vk.c with the level - 1 as long as the main target has more
// than one sample and the device can do sampleRateShading, otherwise 0. With "Off"
// the flag is false in every split, and DrawSplit calls nothing new.
extern int g_gfx_sampleShading;

internal short NativeGpu_VertexPageWord(int tpage)
{
	const int format = (int)GetTPageFormat(tpage);

	s_gpuPrimsByFormatWindow[format]++;

	return (short)(GetTPageBase(tpage) | (format << 5));
}

internal s16 NativeGpu_SignExtend11(u32 value)
{
	value &= 0x7ff;
	return (s16)((value ^ 0x400) - 0x400);
}

DISPENV activeDispEnv;
DRAWENV activeDrawEnv;
int g_GPUDisabledState = 0;

typedef struct
{
	DRAWENV drawenv;
	DISPENV dispenv;

	BlendMode blendMode;

	TexFormat texFormat;
	TextureID textureId;

	int drawPrimMode;
	bool psxTexturedSemiTrans;
	bool psxTextureOutputSTP;
	bool psxDrawMaskSet;

	// Anti-aliasing: this split draws with sample shading. Not in the
	// split comparison - it follows from textureId (textured or white texture)
	// and from g_gfx_sampleShading, which only changes at the frame boundary.
	bool sampleShading;

	u16 startVertex;
	u16 numVerts;

	// Box of the vertices of this split, in pixels - kept only for
	// textured semi-transparent splits, so that a neighbouring primitive
	// of the same state may join, if it does not overlap it
	// (NativeGpu_JoinSemiSplit). Empty means x0 > x1.
	s16 boxX0, boxY0, boxX1, boxY1;

	const char *debugText;
} GPUDrawSplit;

#define MAX_DRAW_SPLITS 4096

// WRITE LIMIT OF THE VERTEX BUFFER.
//
// The most that a single primitive writes: LINE_F4/LINE_G4 are three
// segments of six vertices each (ProcessFlatLines, ProcessGouraudLines). The
// check sits in ParsePrimitive, through which every write path runs. The
// last 18 slots of the buffer are a guard zone: that is where the one
// primitive writes that the check has just rejected, and none of it is
// drawn. A split therefore never begins behind 65,518 - startVertex is
// u16 and 65,536 does not fit into it. Before this check Sunset Vista
// wrote up to 5,222 vertices per batch past the end into the split
// table - the stray "offscreen rectangles" were vertex bytes.
#define NATIVE_GPU_MAX_VERTS_PER_PRIM 18
#define NATIVE_GPU_VERTEX_LIMIT       ((int)MAX_VERTEX_BUFFER_SIZE - NATIVE_GPU_MAX_VERTS_PER_PRIM)

// How far apart two boxes may sit and still be one thing on screen.
//
// Not a tuning knob. The 1P race HUD authors the fruit model at x=316 and its
// count at x=336 (data.hud_1P_P1), and the digits within a number abut - so the
// gap has to be small enough not to swallow a neighbouring element and large
// enough to hold a pair the game draws as one reading. Four is the smallest
// value that does both on the data this game ships.
#define NATIVE_UI_TOUCH_GAP      4

// A race frame notes on the order of a hundred and fifty primitives in a
// handful of elements, and the fullest menu stays well under a thousand. Both
// overruns are counted and said, never silently dropped: a HUD element left at
// its 4:3 position while everything around it moved is exactly the fault that
// reads as "fine" in a log.
#define NATIVE_UI_PRIM_CAPACITY  2048
#define NATIVE_UI_GROUP_CAPACITY 128

// One UI primitive while it waits to be mapped.
//
// The anchor is a property of an ELEMENT, and an element is not a triangle: the
// fruit counter is an apple drawn early in the frame and an "x8" drawn a hundred
// and forty primitives later, a few pixels apart on screen. Deciding per
// triangle puts two parts of one thing at two different fixed points. Deciding
// per triangle plus "inherit from the one before" does not help either, because
// the two are nowhere near each other in draw order.
//
// So nothing is mapped while the ordering table is walked. Every UI primitive is
// noted with its box, and when the walk is over the boxes that touch are merged
// into elements, each element gets ONE anchor, and only then do vertices move.
struct NativeGpuUiPrim
{
	int firstVertex;
	int lastVertex;
	int x0, y0, x1, y1;
	int group;
	int slot;

	// Which row of the declaration table claims this primitive on its own, or
	// -1. A primitive that has one is not grouped and not moved by the floor -
	// see the per-primitive pass in NativeGpu_ApplyUIMapping.
	int decl;

	// Which driver window of the character select claims this primitive as part of its
	// frame or name, or -1. Like a declaration: not
	// grouped, not moved by the floor, shifted by exactly the amount that
	// the 3D window also gets (game/native_menuscreen.c).
	int win;
};

typedef struct
{
	const char *currentSplitDebugText;
	TextureID overrideTexture;
	int overrideTextureWidth;
	int overrideTextureHeight;

	int drawPrimMode;
	bool psxDrawMaskSet;
	bool framebufferFeedbackRunActive;

	// Whether a split was already lost in THIS batch - so that the total
	// above can also say in how many batches it happened and not
	// only how often.
	bool splitOverflowActive;

	// The UI ordering table is a distinct authored 4:3 canvas. Once the linked
	// OT walk reaches it, every following primitive is noted here; world
	// vertices already in the buffer are never touched.
	bool uiViewActive;
	bool uiViewEnabled;
	int uiOtSlot;
	struct CTR_UIViewParameters uiView;

	struct NativeGpuUiPrim uiPrims[NATIVE_UI_PRIM_CAPACITY];
	int uiPrimCount;

	int uiGroups[NATIVE_UI_GROUP_CAPACITY][4];
	int uiGroupSlot[NATIVE_UI_GROUP_CAPACITY];
	int uiGroupCount;

	// Decided once per element, before any vertex moves. Having the anchor here
	// rather than inside the per-vertex loop is what lets a declaration override
	// it and the floor measure against it.
	int uiGroupAnchor[NATIVE_UI_GROUP_CAPACITY];
	int uiGroupMapped[NATIVE_UI_GROUP_CAPACITY][4];
	int uiGroupShift[NATIVE_UI_GROUP_CAPACITY];
	int uiGroupDecl[NATIVE_UI_GROUP_CAPACITY];

	// The shift for the flight over the 4:3 edge, in addition to the anchor
	// (CTR_UI_TransitShift). Only for elements of a menu screen whose
	// anchor is fixed; otherwise 0.
	int uiGroupTransit[NATIVE_UI_GROUP_CAPACITY];

	GrVertex vertexBuffer[MAX_VERTEX_BUFFER_SIZE];
	GPUDrawSplit splits[MAX_DRAW_SPLITS];
	bool vertexDropActive;
	int vertexIndex;
	int splitIndex;
} NativeGpuState;

global_variable NativeGpuState s_gpu;

struct NativeGpuSnapshot
{
	u32 magic;
	u32 version;
	u32 size;
	DRAWENV drawEnv;
	DISPENV dispEnv;
	s32 gpuDisabledState;
	s32 psxDrawMaskSet;
	u16 vram[VRAM_WIDTH * VRAM_HEIGHT];
};


// MEASUREMENT: WHY DOES AddSplit CREATE A NEW SPLIT?
//
// Sunset Vista runs with 4,094 splits in every batch, Inferno with a few
// hundred. Every split is a vkCmdDraw. Before anybody sorts, there is
// counting: per reason (the order of the checks below is the
// order of the comparison in AddSplit), per batch the number of
// DIFFERENT states against the number of changes, and in every 30th frame
// the box check: could a split have moved up to its predecessor of the same state
// without anything lying in between that overlaps it.
// Only counters and log lines every 300 frames, no intervention in the drawing.
enum
{
	NGPU_SR_FORCED_SEMI = 0, // textured + semi-transparent: a split of its own per primitive
	NGPU_SR_BLEND,
	NGPU_SR_TEXFMT, // since the format travels in the vertex: format change WITHIN a split, no longer a reason
	NGPU_SR_TEXID,
	NGPU_SR_PRIMMODE,
	NGPU_SR_SEMIFLAG,
	NGPU_SR_STP,
	NGPU_SR_MASK,
	NGPU_SR_CLIP,
	NGPU_SR_DFE,
	NGPU_SR_DBGTEXT,
	NGPU_SR_FIRST, // first split of a batch, against the sentinel 0
	NGPU_SR_COUNT
};

#define NGPU_STATE_TABLE   1024
#define NGPU_AUDIT_EVERY   30
#define NGPU_AUDIT_MAX_GAP 512

global_variable unsigned long long s_splitReason[NGPU_SR_COUNT];
global_variable TexFormat s_gpuLastPrimFormat = (TexFormat)0xFFFF;
global_variable unsigned long long s_gpuDrawsWindow = 0;
global_variable unsigned long long s_splitForcedSameState = 0;
global_variable unsigned long long s_splitTotal = 0;
global_variable unsigned long long s_splitBatches = 0;
global_variable unsigned long long s_splitDistinctSum = 0;
global_variable unsigned long long s_splitReturns = 0;
global_variable unsigned long long s_splitTableFull = 0;
global_variable int s_splitDistinctPeak = 0;
global_variable unsigned int s_splitPagesUsed[3];

global_variable unsigned long long s_auditBatches = 0;
global_variable unsigned long long s_auditSplits = 0;
global_variable unsigned long long s_auditFirstOfState = 0;
global_variable unsigned long long s_auditJoinable = 0;
global_variable unsigned long long s_auditJoinableTwinOverlap = 0;
global_variable unsigned long long s_auditBlocked = 0;
global_variable unsigned long long s_auditTooFar = 0;
global_variable unsigned long long s_auditUnknownState = 0;
global_variable unsigned long long s_auditOpaque = 0;
global_variable unsigned long long s_auditOpaqueJoinable = 0;
global_variable unsigned long long s_auditBlockedByOpaque = 0;
global_variable int s_auditFrameCounter = 0;
global_variable int s_auditThisFrame = 0;

// THE BOX CHECK IS A SWITCH. Unswitched it runs in every 30th frame over
// every batch: read all vertices of the batch once more (Sunset Vista up to
// 65,523 x 20 bytes = 1.3 MB) and up to 512 box comparisons per split
// (Vista 972 splits per batch) - a spike in every 30th frame, for an answer
// that is already known ("sorting problem, 64 percent could move up").
// --split-audit switches it on.
int g_cfg_splitAudit = 0;

// Per batch: table of the states seen (open addressing over
// a hash, comparison against the first split with this state) and per
// split the number of its state.
global_variable int s_batchStateFirstSplit[NGPU_STATE_TABLE];
global_variable int s_batchDistinct = 0;
global_variable short s_splitStateId[MAX_DRAW_SPLITS];

internal int NativeGpu_SplitStateEqual(const GPUDrawSplit *a, const GPUDrawSplit *b)
{
	return a->blendMode == b->blendMode && a->textureId == b->textureId && a->drawPrimMode == b->drawPrimMode &&
	       a->psxTexturedSemiTrans == b->psxTexturedSemiTrans && a->psxTextureOutputSTP == b->psxTextureOutputSTP && a->psxDrawMaskSet == b->psxDrawMaskSet &&
	       a->drawenv.clip.x == b->drawenv.clip.x && a->drawenv.clip.y == b->drawenv.clip.y && a->drawenv.clip.w == b->drawenv.clip.w &&
	       a->drawenv.clip.h == b->drawenv.clip.h && a->drawenv.dfe == b->drawenv.dfe && a->debugText == b->debugText;
}

internal unsigned int NativeGpu_SplitStateHash(const GPUDrawSplit *s)
{
	unsigned int h = 2166136261u;
	unsigned int words[12];
	int i;

	words[0] = (unsigned int)s->blendMode;
	words[1] = 0; // the texture format has not been a state since it travels in the vertex
	words[2] = (unsigned int)s->textureId;
	words[3] = (unsigned int)s->drawPrimMode;
	words[4] = (unsigned int)(s->psxTexturedSemiTrans | (s->psxTextureOutputSTP << 1) | (s->psxDrawMaskSet << 2) | (s->drawenv.dfe << 3));
	words[5] = (unsigned int)(u16)s->drawenv.clip.x;
	words[6] = (unsigned int)(u16)s->drawenv.clip.y;
	words[7] = (unsigned int)(u16)s->drawenv.clip.w;
	words[8] = (unsigned int)(u16)s->drawenv.clip.h;
	words[9] = (unsigned int)(size_t)s->debugText;
	words[10] = (unsigned int)((size_t)s->debugText >> 16);
	words[11] = 0;

	for (i = 0; i < 12; i++)
	{
		h ^= words[i];
		h *= 16777619u;
		h ^= h >> 13;
	}
	return h;
}

internal void NativeGpu_SplitBatchReset(void)
{
	int i;
	for (i = 0; i < NGPU_STATE_TABLE; i++)
	{
		s_batchStateFirstSplit[i] = -1;
	}
	s_batchDistinct = 0;
}

// After creating split splitIndex: register the state, count changes.
internal void NativeGpu_SplitNoteState(int splitIndex)
{
	const GPUDrawSplit *split = &s_gpu.splits[splitIndex];
	unsigned int slot = NativeGpu_SplitStateHash(split) & (NGPU_STATE_TABLE - 1);
	int probes = 0;
	int id = -1;

	while (probes < NGPU_STATE_TABLE)
	{
		const int first = s_batchStateFirstSplit[slot];
		if (first < 0)
		{
			s_batchStateFirstSplit[slot] = splitIndex;
			s_batchDistinct++;
			id = (int)slot;
			break;
		}
		if (NativeGpu_SplitStateEqual(&s_gpu.splits[first], split))
		{
			id = (int)slot;
			break;
		}
		slot = (slot + 1) & (NGPU_STATE_TABLE - 1);
		probes++;
	}

	if (id < 0)
	{
		s_splitTableFull++;
	}
	s_splitStateId[splitIndex] = (short)id;

	// State A, then something else, then A again: that is the change that
	// a sorting could save. The same state as the direct
	// predecessor is not a change (that is the forced single split).
	if (id >= 0 && splitIndex >= 2 && s_batchStateFirstSplit[id] != splitIndex && s_splitStateId[splitIndex - 1] != (short)id)
	{
		s_splitReturns++;
	}
}

typedef struct
{
	short x0, y0, x1, y1;
	int empty;
} NativeGpuBox;

internal void NativeGpu_VertexRangeBox(int first, int end, NativeGpuBox *box)
{
	int v;

	box->empty = (end <= first);
	box->x0 = 32767;
	box->y0 = 32767;
	box->x1 = -32768;
	box->y1 = -32768;
	for (v = first; v < end && v < (int)MAX_VERTEX_BUFFER_SIZE; v++)
	{
		const GrVertex *vert = &s_gpu.vertexBuffer[v];
		if (vert->x < box->x0) box->x0 = vert->x;
		if (vert->x > box->x1) box->x1 = vert->x;
		if (vert->y < box->y0) box->y0 = vert->y;
		if (vert->y > box->y1) box->y1 = vert->y;
	}
}

internal void NativeGpu_SplitBox(int i, NativeGpuBox *box)
{
	const GPUDrawSplit *split = &s_gpu.splits[i];
	const int end = (i < s_gpu.splitIndex) ? (int)s_gpu.splits[i + 1].startVertex : s_gpu.vertexIndex;

	NativeGpu_VertexRangeBox((int)split->startVertex, end, box);
}

internal int NativeGpu_BoxOverlap(const NativeGpuBox *a, const NativeGpuBox *b)
{
	if (a->empty || b->empty)
	{
		return 0;
	}
	return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

global_variable NativeGpuBox s_auditBox[MAX_DRAW_SPLITS];

// MERGE NEIGHBOURING SEMI-TRANSPARENT ONES WHERE THEY DO NOT OVERLAP.
//
// A textured semi-transparent primitive is drawn in two passes
// (DrawSplit: pass 1 the texels without STP bit, opaque; pass
// 2 those with STP bit, blended). Within ONE primitive the
// two passes touch separate pixels (discardForSemiTransPass, per texel
// exactly one of the two). Between TWO primitives A before B lies the
// fault: merged, it would run A1 B1 A2 B2, and where an STP texel of A lies on
// an opaque texel of B, A would come blended OVER B - the PS1 paints
// B over A. That can only happen where A and B hit the same
// pixel. Boxes that do not touch share no pixel: then
// A1 B1 A2 B2 and A1 A2 B1 B2 are the same picture.
//
// So: the primitive just parsed has got its own split
// (AddSplit enforces that). If the predecessor split is the same state, also
// semi-transparent, and its box does not overlap that of the primitive,
// the new split is withdrawn again and the predecessor grows by the primitive
// and its box. The box of the predecessor is the union of all
// its primitives - coarser than pairwise (an earlier measurement was pairwise),
// in exchange one check per primitive instead of per pair: one state comparison,
// three to six vertices, four comparisons. Nothing is reordered:
// the primitives stay in ordering table order in the vertex buffer.
global_variable unsigned long long s_semiJoined = 0;
global_variable unsigned long long s_semiKeptNoTwin = 0;
global_variable unsigned long long s_semiKeptState = 0;
global_variable unsigned long long s_semiKeptOverlap = 0;

// MEASUREMENT: OVERSIZED AND BLACK AREAS.
//
// Texture holes and flat black areas at the track edge and at the arch were
// traced to G3 opaque triangles above the PS1 size (308 in one run, 264 of
// them with a clamped vertex). Here every primitive is looked at AFTER its
// vertices are written:
// box in pixels, colour, page, format, CLUT, blend mode.
// Oversized (wider than 1023 or higher than 511 - the PS1 does not draw that)
// and large-and-black (box above 20,000 pixels, all colours
// below 9) are counted and the first 300 per class are logged with VBlank,
// so that a snapshot at exactly this point shows the area. No intervention.
//
// THE SIZE LIMIT OF THE PS1, AS A RULE.
//
// psx-spx, GPU Rendering Attributes: "The maximum distance between two
// vertices is 1023 horizontally, and 511 vertically. Polygons and lines that
// are exceeding that dimensions are NOT rendered." To the GPU a quad is
// two triangles, (x0,x1,x2) and (x1,x2,x3), each on its own - exactly the two
// that TriangulateQuad puts into slots 0..2 and 3..5 here, and exactly the
// two that NativeGpu_NotePolySize measures. The measurement is the rule:
// NotePolySize leaves a mask of which of the two triangles is too large, and
// ParsePrimitive takes it back out of the buffer after writing. Without the
// rule this port drew such triangles - as a wedge across the screen when a
// vertex clamps at +-0x3ff (sky of Sunset Vista: 308 in one run; tyre quads).
//
// Lines (0x40..0x5f) stay out of this: this port draws them as quads without
// knowing the raw end points here, and CTR draws none in a race.
// Rectangles (SPRT/TILE) cannot exceed the limit because of their
// encoding.
global_variable int s_gpuSizeDropMask = 0;
global_variable unsigned long long s_gpuSizeDroppedTrisWindow = 0;
global_variable unsigned long long s_gpuSizeDroppedTrisTotal = 0;
global_variable unsigned long long s_gpuSizeDroppedWholeWindow = 0;
global_variable unsigned long long s_gpuSizeDroppedWholeTotal = 0;
global_variable unsigned long long s_gpuNullTexSkippedWindow = 0;
global_variable unsigned long long s_gpuNullTexSkippedTotal = 0;
int g_cfg_skipNullTex = 0;

global_variable unsigned long long s_tBigWindow = 0;
global_variable unsigned long long s_tBigBlackWindow = 0;
global_variable unsigned long long s_tBigTexturedWindow = 0;
global_variable unsigned long long s_tLargeBlackWindow = 0;
global_variable int s_tLinesBig = 0;
global_variable int s_tLinesBlack = 0;

// --hole-probe V,X,Y (main.c): which primitives cover this pixel at
// this VBlank. Only what arrives is measured; the answer is the list.
int g_cfg_holeProbeCount = 0;
int g_cfg_holeProbe[4][3];

internal void NativeGpu_HoleProbe(int firstVertex, const P_TAG *polyTag)
{
	const int vblank = Platform_GetVBlankCount();
	const int end = s_gpu.vertexIndex;
	int k;

	for (k = 0; k < g_cfg_holeProbeCount; k++)
	{
		const int *pr = g_cfg_holeProbe[k];
		int x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768, u0 = 255, u1 = 0, w0 = 255, w1 = 0, v;

		if ((vblank < pr[0]) || (vblank > pr[0] + 4) || (end <= firstVertex))
		{
			continue;
		}
		for (v = firstVertex; v < end; v++)
		{
			const GrVertex *g = &s_gpu.vertexBuffer[v];
			if (g->x < x0) x0 = g->x;
			if (g->x > x1) x1 = g->x;
			if (g->y < y0) y0 = g->y;
			if (g->y > y1) y1 = g->y;
			if (g->u < u0) u0 = g->u;
			if (g->u > u1) u1 = g->u;
			if (g->v < w0) w0 = g->v;
			if (g->v > w1) w1 = g->v;
		}
		if ((pr[1] < x0) || (pr[1] > x1) || (pr[2] < y0) || (pr[2] > y1))
		{
			continue;
		}
		{
			const GrVertex *g0 = &s_gpu.vertexBuffer[firstVertex];
			const int blend = (int)s_gpu.splits[s_gpu.splitIndex].blendMode;
			// All corner colours - with six vertices (quad) the corners are
			// slots 0, 1, 2 and 5 (TriangulateQuad: x0,x1,x2 / x1,x2,x3).
			const int n = end - firstVertex;
			const GrVertex *g1 = &s_gpu.vertexBuffer[firstVertex + ((n > 1) ? 1 : 0)];
			const GrVertex *g2 = &s_gpu.vertexBuffer[firstVertex + ((n > 2) ? 2 : 0)];
			const GrVertex *g3 = &s_gpu.vertexBuffer[firstVertex + ((n > 5) ? 5 : ((n > 3) ? 3 : 0))];
			Platform_Log("[CTR Probe] vblank %d point (%d,%d): code 0x%02x blend %d page %d fmt %d clut 0x%04x (x %d, y %d) uv (%d..%d, %d..%d) box (%d,%d)-(%d,%d) rgb %d,%d,%d / %d,%d,%d / %d,%d,%d / %d,%d,%d bright %d ofs (%d,%d)\n",
			             vblank, pr[1], pr[2], polyTag->code, blend, g0->page & 31, (g0->page >> 5) & 3, (u16)g0->clut, ((u16)g0->clut & 0x3f) << 4, (u16)g0->clut >> 6,
			             u0, u1, w0, w1, x0, y0, x1, y1, g0->r, g0->g, g0->b, g1->r, g1->g, g1->b, g2->r, g2->g, g2->b, g3->r, g3->g, g3->b, g0->bright, activeDrawEnv.ofs[0], activeDrawEnv.ofs[1]);
		}
	}
}

// THE VERTEX DUMP OF A FRAME (for gaps in walls).
//
// CTR_VERTEX_DUMP=<vblank>[,<vblank>...] in the environment: every polygon that
// arrives at the parser in one of these VBlanks goes with all corners to
// vertex-dump-<vblank>.txt in the working directory - as the rasterizer
// gets them, in PS1 coordinates including offset. One line per polygon:
//   <code hex> <blend> <page> <clut> x,y,u,v,r,g,b ...
// Six corners are a quad as two triangles (0,1,2)(1,2,3), see
// TriangulateQuad. Only read, change nothing; without the variable one comparison
// per polygon. The answer to the gaps lies between the lines: two
// triangles that should share an edge and miss it by one pixel.
internal void NativeGpu_VertexDump(int firstVertex, const P_TAG *polyTag)
{
	local_persist int wanted[8];
	local_persist int wantedCount = -1; // -1: environment not read yet
	local_persist FILE *out = NULL;
	local_persist int outVblank = -1;
	const int vblank = Platform_GetVBlankCount();
	const int end = s_gpu.vertexIndex;
	int hit = 0;
	int k;

	if (wantedCount < 0)
	{
		// A developer tool: only with --dev.
		extern int g_cfg_dev;
		const char *env = g_cfg_dev ? getenv("CTR_VERTEX_DUMP") : NULL;

		wantedCount = 0;
		while ((env != NULL) && (*env != '\0') && (wantedCount < 8))
		{
			wanted[wantedCount++] = atoi(env);
			env = strchr(env, ',');
			if (env != NULL)
			{
				env++;
			}
		}
	}
	for (k = 0; k < wantedCount; k++)
	{
		if (wanted[k] == vblank)
		{
			hit = 1;
		}
	}
	if ((out != NULL) && (vblank != outVblank))
	{
		fclose(out);
		out = NULL;
	}
	if (!hit || (end <= firstVertex))
	{
		return;
	}
	if (out == NULL)
	{
		char name[64];

		snprintf(name, sizeof(name), "vertex-dump-%d.txt", vblank);
		out = fopen(name, "w");
		outVblank = vblank;
		if (out == NULL)
		{
			wantedCount = 0;
			return;
		}
		fprintf(out, "# vblank %d ofs %d %d\n", vblank, activeDrawEnv.ofs[0], activeDrawEnv.ofs[1]);
	}
	fprintf(out, "%02x %d %d %d", polyTag->code, (int)s_gpu.splits[s_gpu.splitIndex].blendMode, (int)s_gpu.vertexBuffer[firstVertex].page,
	        (int)(u16)s_gpu.vertexBuffer[firstVertex].clut);
	for (k = firstVertex; k < end; k++)
	{
		const GrVertex *g = &s_gpu.vertexBuffer[k];

		fprintf(out, " %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", g->x, g->y, g->u, g->v, g->r, g->g, g->b, g->tcx, g->tcy, g->bright, g->dither);
	}
	fputc('\n', out);
}

internal void NativeGpu_NoteOversizeAndBlack(int firstVertex, const P_TAG *polyTag)
{
	const int end = s_gpu.vertexIndex;
	int v;
	int x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768;
	int maxRgb = 0;
	int pinned = 0;

	if (end <= firstVertex || end > (int)MAX_VERTEX_BUFFER_SIZE)
	{
		return;
	}
	for (v = firstVertex; v < end; v++)
	{
		const GrVertex *g = &s_gpu.vertexBuffer[v];
		if (g->x < x0) x0 = g->x;
		if (g->x > x1) x1 = g->x;
		if (g->y < y0) y0 = g->y;
		if (g->y > y1) y1 = g->y;
		if (g->r > maxRgb) maxRgb = g->r;
		if (g->g > maxRgb) maxRgb = g->g;
		if (g->b > maxRgb) maxRgb = g->b;
		// clamped GTE vertex: +-0x3ff plus offset of the draw environment
		if ((g->x - activeDrawEnv.ofs[0] == 0x3ff) || (g->x - activeDrawEnv.ofs[0] == -0x3ff) || (g->y - activeDrawEnv.ofs[1] == 0x3ff) || (g->y - activeDrawEnv.ofs[1] == -0x3ff))
		{
			pinned = 1;
		}
	}
	{
		const int w = x1 - x0;
		const int h = y1 - y0;
		const long long area = (long long)w * (long long)h;
		const GrVertex *g0 = &s_gpu.vertexBuffer[firstVertex];
		const int textured = (polyTag->code & 0x04) != 0 && (polyTag->code & 0xe0) == 0x20; // POLY with texture bit; TILE/SPRT carry 0x60
		const int sprite = (polyTag->code & 0xe0) == 0x60;
		const int isTextured = textured || (sprite && ((polyTag->code & 0x04) != 0));
		const int black = (maxRgb <= 8);
		const int big = (w > 1023) || (h > 511);
		const int largeBlack = black && !isTextured && (area > 20000);
		const int blend = (int)s_gpu.splits[s_gpu.splitIndex].blendMode;

		if (big)
		{
			s_tBigWindow++;
			if (black && !isTextured) s_tBigBlackWindow++;
			if (isTextured) s_tBigTexturedWindow++;
			if (s_tLinesBig < 300)
			{
				s_tLinesBig++;
				Platform_Log("[CTR Hole] vblank %d: oversize code 0x%02x blend %d %s page %d fmt %d clut 0x%04x box (%d,%d)-(%d,%d) %dx%d maxRGB %d%s v0 rgb %d,%d,%d\n",
				             Platform_GetVBlankCount(), polyTag->code, blend, isTextured ? "textured" : "flat", g0->page & 31, (g0->page >> 5) & 3, (u16)g0->clut, x0, y0, x1, y1, w, h,
				             maxRgb, pinned ? " PINNED" : "", g0->r, g0->g, g0->b);
			}
		}
		else if (largeBlack)
		{
			s_tLargeBlackWindow++;
			if (s_tLinesBlack < 300)
			{
				s_tLinesBlack++;
				Platform_Log("[CTR Hole] vblank %d: large black flat code 0x%02x blend %d box (%d,%d)-(%d,%d) %dx%d area %lld%s\n", Platform_GetVBlankCount(), polyTag->code, blend,
				             x0, y0, x1, y1, w, h, area, pinned ? " PINNED" : "");
			}
		}
	}
}

internal void NativeGpu_SplitBoxOf(const GPUDrawSplit *split, NativeGpuBox *box)
{
	box->x0 = split->boxX0;
	box->y0 = split->boxY0;
	box->x1 = split->boxX1;
	box->y1 = split->boxY1;
	box->empty = (split->boxX0 > split->boxX1) || (split->boxY0 > split->boxY1);
}

internal void NativeGpu_JoinSemiSplit(int firstVertex)
{
	GPUDrawSplit *cur = &s_gpu.splits[s_gpu.splitIndex];
	NativeGpuBox box;

	// Only if the primitive just parsed has got its own semi-transparent
	// split that begins exactly at it.
	if ((s_gpu.splitIndex < 1) || !cur->psxTexturedSemiTrans || ((int)cur->startVertex != firstVertex) || (s_gpu.vertexIndex <= firstVertex))
	{
		return;
	}

	NativeGpu_VertexRangeBox(firstVertex, s_gpu.vertexIndex, &box);

	if (s_gpu.splitIndex >= 2)
	{
		GPUDrawSplit *prev = cur - 1;
		NativeGpuBox prevBox;

		NativeGpu_SplitBoxOf(prev, &prevBox);

		if (!prev->psxTexturedSemiTrans)
		{
			s_semiKeptNoTwin++;
		}
		else if (!NativeGpu_SplitStateEqual(prev, cur))
		{
			s_semiKeptState++;
		}
		else if (NativeGpu_BoxOverlap(&prevBox, &box))
		{
			s_semiKeptOverlap++;
		}
		else
		{
			// Merge: the predecessor grows, the new split drops out.
			// numVerts of the predecessor is set anew by the caller after every primitive
			// (end of the ordering table loop), as before.
			const int removed = s_gpu.splitIndex;
			const int id = s_splitStateId[removed];

			if (box.x0 < prev->boxX0) prev->boxX0 = box.x0;
			if (box.y0 < prev->boxY0) prev->boxY0 = box.y0;
			if (box.x1 > prev->boxX1) prev->boxX1 = box.x1;
			if (box.y1 > prev->boxY1) prev->boxY1 = box.y1;

			s_gpu.splitIndex--;
			s_semiJoined++;

			// Measurement bookkeeping: the split that AddSplit just counted
			// no longer exists. The predecessor has the same state,
			// so the state table may point to it.
			s_splitReason[NGPU_SR_FORCED_SEMI]--;
			s_splitForcedSameState--;
			if ((id >= 0) && (s_batchStateFirstSplit[id] == removed))
			{
				s_batchStateFirstSplit[id] = removed - 1;
			}
			return;
		}
	}
	else
	{
		s_semiKeptNoTwin++;
	}

	// Not merged: the new split carries the box of its primitive,
	// so that the next one can check against it.
	cur->boxX0 = box.x0;
	cur->boxY0 = box.y0;
	cur->boxX1 = box.x1;
	cur->boxY1 = box.y1;
}
global_variable int s_auditLastOfState[NGPU_STATE_TABLE];

// The box check, before drawing the batch, only in every 30th frame.
// Conservative: boxes instead of areas (a box overlaps more often than the
// triangle in it), and more than 512 splits back is not searched
// (counts as not movable).
internal void NativeGpu_SplitAuditBatch(void)
{
	int i;

	if (s_gpu.splitIndex < 1)
	{
		return;
	}
	s_auditBatches++;
	for (i = 0; i < NGPU_STATE_TABLE; i++)
	{
		s_auditLastOfState[i] = -1;
	}
	for (i = 1; i <= s_gpu.splitIndex; i++)
	{
		NativeGpu_SplitBox(i, &s_auditBox[i]);
	}
	for (i = 1; i <= s_gpu.splitIndex; i++)
	{
		const int id = s_splitStateId[i];
		const int opaque = (s_gpu.splits[i].blendMode == BM_NONE);
		int j;
		int k;
		int blocked = 0;
		int blockedOpaque = 0;

		s_auditSplits++;
		if (opaque)
		{
			s_auditOpaque++;
		}
		if (id < 0)
		{
			s_auditUnknownState++;
			continue;
		}
		j = s_auditLastOfState[id];
		s_auditLastOfState[id] = i;
		if (j < 0)
		{
			s_auditFirstOfState++;
			continue;
		}
		if (i - j > NGPU_AUDIT_MAX_GAP)
		{
			s_auditTooFar++;
			continue;
		}
		for (k = j + 1; k < i; k++)
		{
			if (NativeGpu_BoxOverlap(&s_auditBox[k], &s_auditBox[i]))
			{
				blocked = 1;
				if (s_gpu.splits[k].blendMode == BM_NONE)
				{
					blockedOpaque = 1;
				}
				break;
			}
		}
		if (blocked)
		{
			s_auditBlocked++;
			if (blockedOpaque)
			{
				s_auditBlockedByOpaque++;
			}
			continue;
		}
		s_auditJoinable++;
		if (opaque)
		{
			s_auditOpaqueJoinable++;
		}
		// The twin: a textured semi-transparent split is drawn in two
		// passes (DrawSplit). Merged with its
		// predecessor, pass 1 of the one would run before pass 2 of the other -
		// that only changes something where the two overlap.
		if (s_gpu.splits[i].psxTexturedSemiTrans && NativeGpu_BoxOverlap(&s_auditBox[j], &s_auditBox[i]))
		{
			s_auditJoinableTwinOverlap++;
		}
	}
}

internal int NativeGpu_PopCount(unsigned int v)
{
	int n = 0;
	while (v)
	{
		n += (int)(v & 1u);
		v >>= 1;
	}
	return n;
}

internal void NativeGpu_SplitPrintBlock(void)
{
	Platform_Log("[CTR Split] block: %llu split(s) in %llu batch(es); why a new split: textured semi-trans gets its own %llu (state equal to its predecessor %llu), blend %llu, texture format 0 (rides in the vertex; changed inside a split %llu time(s)), texture id %llu, prim mode %llu, semi flag %llu, stp %llu, mask %llu, clip %llu, dfe %llu, debug text %llu, first of batch %llu\n",
	             s_splitTotal, s_splitBatches, s_splitReason[NGPU_SR_FORCED_SEMI], s_splitForcedSameState, s_splitReason[NGPU_SR_BLEND], s_splitReason[NGPU_SR_TEXFMT],
	             s_splitReason[NGPU_SR_TEXID], s_splitReason[NGPU_SR_PRIMMODE], s_splitReason[NGPU_SR_SEMIFLAG], s_splitReason[NGPU_SR_STP], s_splitReason[NGPU_SR_MASK],
	             s_splitReason[NGPU_SR_CLIP], s_splitReason[NGPU_SR_DFE], s_splitReason[NGPU_SR_DBGTEXT], s_splitReason[NGPU_SR_FIRST]);
	Platform_Log("[CTR Split] block: semi-trans joined into its predecessor %llu time(s); kept its own split because the predecessor was not semi-trans %llu, another state %llu, boxes overlap %llu\n",
	             s_semiJoined, s_semiKeptNoTwin, s_semiKeptState, s_semiKeptOverlap);
	s_semiJoined = 0;
	s_semiKeptNoTwin = 0;
	s_semiKeptState = 0;
	s_semiKeptOverlap = 0;
	Platform_Log("[CTR Split] block: distinct states %llu summed over batches, peak %d in one batch, a state came back after an interlude %llu time(s), state table full %llu; texture pages used: 4-bit %d, 8-bit %d, 16-bit %d\n",
	             s_splitDistinctSum, s_splitDistinctPeak, s_splitReturns, s_splitTableFull, NativeGpu_PopCount(s_splitPagesUsed[0]), NativeGpu_PopCount(s_splitPagesUsed[1]),
	             NativeGpu_PopCount(s_splitPagesUsed[2]));
	Platform_Log("[CTR Split] audit (every %dth frame, boxes%s): %llu batch(es), %llu split(s): first of its state %llu, could join its predecessor of the same state %llu (of those semi-trans twins overlapping %llu), blocked by an overlapping split in between %llu (by an opaque one %llu), gap over %d not checked %llu, state unknown %llu; opaque %llu of which could join %llu\n",
	             NGPU_AUDIT_EVERY, g_cfg_splitAudit ? "" : ", off - --split-audit turns it on", s_auditBatches, s_auditSplits, s_auditFirstOfState, s_auditJoinable, s_auditJoinableTwinOverlap, s_auditBlocked, s_auditBlockedByOpaque,
	             NGPU_AUDIT_MAX_GAP, s_auditTooFar, s_auditUnknownState, s_auditOpaque, s_auditOpaqueJoinable);

	memset(s_splitReason, 0, sizeof(s_splitReason));
	s_splitForcedSameState = 0;
	s_splitTotal = 0;
	s_splitBatches = 0;
	s_splitDistinctSum = 0;
	s_splitReturns = 0;
	s_splitTableFull = 0;
	s_splitDistinctPeak = 0;
	memset(s_splitPagesUsed, 0, sizeof(s_splitPagesUsed));
	s_auditBatches = 0;
	s_auditSplits = 0;
	s_auditFirstOfState = 0;
	s_auditJoinable = 0;
	s_auditJoinableTwinOverlap = 0;
	s_auditBlocked = 0;
	s_auditTooFar = 0;
	s_auditUnknownState = 0;
	s_auditOpaque = 0;
	s_auditOpaqueJoinable = 0;
	s_auditBlockedByOpaque = 0;
}

int NativeGpu_HasPendingSplits(void)
{
	return s_gpu.splitIndex > 0;
}

void ClearSplits(void)
{
	// The vertex cursor goes back to zero, so every recorded UI vertex range
	// stops pointing at its own vertices. They are dropped with it. Anything
	// still worth mapping was mapped in DrawAllSplits just above this call;
	// leaving them behind would move somebody else geometry on the next frame.
	s_gpu.uiPrimCount = 0;
	s_gpu.uiGroupCount = 0;

	s_gpu.currentSplitDebugText = NULL;
	s_gpu.vertexIndex = 0;
	s_gpu.splitIndex = 0;
	NativeGpu_SplitBatchReset();
	s_gpu.splits[0].texFormat = (TexFormat)0xFFFF;
	s_gpu.splits[0].psxTexturedSemiTrans = false;
	s_gpu.splits[0].psxTextureOutputSTP = false;
	s_gpu.splits[0].psxDrawMaskSet = false;
	s_gpu.framebufferFeedbackRunActive = false;
	s_gpu.splitOverflowActive = false;
	s_gpu.vertexDropActive = false;
}

global_variable unsigned long long s_gpuVertexDrops = 0;
global_variable unsigned int s_gpuVertexDropRuns = 0;
global_variable int s_gpuSplitPeak = 0;
global_variable int s_gpuSplitPeakWindow = 0;
global_variable int s_gpuVertexPeakWindow = 0;
global_variable unsigned long long s_gpuCutsReported = 0;
// MEASUREMENT: at the batch cut the running split can carry vertices
// that its numVerts does not know yet (numVerts is only set at the end of the packet).
// Counted, not claimed: how often and how many, and whether the cut falls in the
// UI table.
global_variable unsigned long long s_gpuCutLostEvents = 0;
global_variable unsigned long long s_gpuCutLostVerts = 0;
global_variable unsigned long long s_gpuCutsInUI = 0;
// MEASUREMENT: class C polygons (page 0, CLUT 0, UV 0 at every vertex)
// are always counted, skipped only with --skip-null-tex.
global_variable unsigned long long s_gpuNullTexSeenWindow = 0;
global_variable unsigned long long s_gpuNullTexSeenTotal = 0;
global_variable int s_gpuNullTexBoxMaxWindow = 0;
global_variable int s_gpuNullTexBoxMaxTotal = 0;

internal void NativeGpu_NoteCutLoss(void)
{
	if (s_gpu.splitIndex > 0)
	{
		const GPUDrawSplit *last = &s_gpu.splits[s_gpu.splitIndex];
		const int lost = s_gpu.vertexIndex - (last->startVertex + (int)last->numVerts);

		if (lost > 0)
		{
			s_gpuCutLostEvents++;
			s_gpuCutLostVerts += (unsigned long long)lost;
		}
	}
	if (s_gpu.uiViewActive)
	{
		s_gpuCutsInUI++;
	}
}

global_variable unsigned long long s_gpuCutsForVertices = 0;
global_variable unsigned long long s_gpuCutsForSplits = 0;

// The menu part of the UI mapper, further down with its counters.
internal void NativeGpu_PrintMenuReport(void);

void NativeGpu_PrintSplitReport(void)
{
	// Registered through Platform_AtExitReport, like the disc report: runs in
	// Platform_Shutdown, before the log closes, on every way out -
	// Ctrl+Q goes through exit(0), and what stands at the end of main then
	// never runs.
	Platform_Log("[CTR GPU] at exit: vertex buffer full %llu time(s) in %u batch(es) - those primitives were dropped, the buffer holds %d vertices\n",
	             s_gpuVertexDrops, s_gpuVertexDropRuns, (int)MAX_VERTEX_BUFFER_SIZE);
	Platform_Log("[CTR GPU] at exit: split peak %d of %d in one batch\n", s_gpuSplitPeak, (int)MAX_DRAW_SPLITS);
	Platform_Log("[CTR GPU] at exit: batch cut early %llu time(s) because the vertex buffer was full and %llu time(s) because the split table was full - drawn and restarted, nothing dropped there\n",
	             s_gpuCutsForVertices, s_gpuCutsForSplits);
	Platform_Log("[CTR GPU] at exit: at %llu cut(s) the last split did not yet count %llu vertex/vertices (numVerts behind the cursor); %llu cut(s) fell inside the UI table\n",
	             s_gpuCutLostEvents, s_gpuCutLostVerts, s_gpuCutsInUI);
	NativeGpu_PrintMenuReport();

	if (s_gpuSplitOverflows == 0)
	{
		return;
	}

	Platform_LogError("[CTR GPU] MAX_DRAW_SPLITS (%d) reached %llu time(s) in %u batch(es)\n", MAX_DRAW_SPLITS,
		                  (unsigned long long)s_gpuSplitOverflows, s_gpuSplitOverflowRuns);
	Platform_LogError("%s\n", "[CTR GPU] every one of those primitives was drawn with the state of the split before it");
}

int NativeGpu_GetStateSize(void)
{
	return (int)sizeof(struct NativeGpuSnapshot);
}

int NativeGpu_CaptureState(void *dst, int dstSize)
{
	struct NativeGpuSnapshot *snapshot = (struct NativeGpuSnapshot *)dst;

	if ((dst == NULL) || (dstSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if (NativeRenderer_GetVRAMStateSize() != (int)sizeof(snapshot->vram))
	{
		return 0;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->magic = NATIVE_GPU_STATE_MAGIC;
	snapshot->version = NATIVE_GPU_STATE_VERSION;
	snapshot->size = sizeof(*snapshot);
	snapshot->drawEnv = activeDrawEnv;
	snapshot->dispEnv = activeDispEnv;
	snapshot->gpuDisabledState = g_GPUDisabledState;
	snapshot->psxDrawMaskSet = s_gpu.psxDrawMaskSet;

	return NativeRenderer_CaptureVRAMState(snapshot->vram, sizeof(snapshot->vram));
}

int NativeGpu_RestoreState(const void *src, int srcSize)
{
	const struct NativeGpuSnapshot *snapshot = (const struct NativeGpuSnapshot *)src;

	if ((src == NULL) || (srcSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->magic != NATIVE_GPU_STATE_MAGIC) || (snapshot->version != NATIVE_GPU_STATE_VERSION) || (snapshot->size != sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->gpuDisabledState < 0) || (snapshot->gpuDisabledState > 1) || (snapshot->psxDrawMaskSet < 0) || (snapshot->psxDrawMaskSet > 1))
	{
		return 0;
	}
	if (NativeRenderer_GetVRAMStateSize() != (int)sizeof(snapshot->vram))
	{
		return 0;
	}
	if (!NativeRenderer_RestoreVRAMState(snapshot->vram, sizeof(snapshot->vram)))
	{
		return 0;
	}

	activeDrawEnv = snapshot->drawEnv;
	activeDispEnv = snapshot->dispEnv;
	g_GPUDisabledState = snapshot->gpuDisabledState;
	s_gpu.psxDrawMaskSet = snapshot->psxDrawMaskSet;
	ClearSplits();
	return 1;
}

internal void DrawEnvDimensionsInt(int *width, int *height)
{
	if (activeDrawEnv.dfe)
	{
		*width = activeDispEnv.disp.w;
		*height = activeDispEnv.disp.h;
	}
	else
	{
		*width = activeDrawEnv.clip.w;
		*height = activeDrawEnv.clip.h;
	}
}

void DrawEnvOffset(float *ofsX, float *ofsY)
{
	if (activeDrawEnv.dfe)
	{
		int w, h;
		DrawEnvDimensionsInt(&w, &h);

		if (w <= 0)
		{
			w = 1;
		}

		// NOTE(aalhendi): Convert PS1 VRAM-page draw offsets into display-relative host-screen offsets.
		// CTR alternates draw pages at y=0 and y=0x128; using raw modulo VRAM offsets shifts every other native frame vertically.
		*ofsX = activeDrawEnv.ofs[0] - activeDispEnv.disp.x;
		*ofsY = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
	}
	else
	{
		*ofsX = activeDrawEnv.ofs[0] - activeDrawEnv.clip.x;
		*ofsY = activeDrawEnv.ofs[1] - activeDrawEnv.clip.y;
	}
}

void LineSwapSourceVerts(VERTTYPE **p0, VERTTYPE **p1, u8 **c0, u8 **c1)
{
	// swap line coordinates for left-to-right and up-to-bottom direction
	if (((*p0)[0] > (*p1)[0]) || ((*p0)[1] > (*p1)[1] && (*p0)[0] == (*p1)[0]))
	{
		VERTTYPE *tmp = *p0;
		*p0 = *p1;
		*p1 = tmp;

		u8 *tmpCol = *c0;
		*c0 = *c1;
		*c1 = tmpCol;
	}
}

void MakeLineArray(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1)
{
	const VERTTYPE dx = p1[0] - p0[0];
	const VERTTYPE dy = p1[1] - p0[1];

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	if (dx > abs((s16)dy))
	{ // horizontal
		vertex[0].x = p0[0] + ofsX;
		vertex[0].y = p0[1] + ofsY;

		vertex[1].x = p1[0] + ofsX + 1;
		vertex[1].y = p1[1] + ofsY;

		vertex[2].x = vertex[1].x;
		vertex[2].y = vertex[1].y + 1;

		vertex[3].x = vertex[0].x;
		vertex[3].y = vertex[0].y + 1;
	}
	else
	{ // vertical
		vertex[0].x = p0[0] + ofsX;
		vertex[0].y = p0[1] + ofsY;

		vertex[1].x = p1[0] + ofsX;
		vertex[1].y = p1[1] + ofsY + 1;

		vertex[2].x = vertex[1].x + 1;
		vertex[2].y = vertex[1].y;

		vertex[3].x = vertex[0].x + 1;
		vertex[3].y = vertex[0].y;
	} // TODO diagonal line alignment

	NativeSubpixel_NoteVertices(NATIVE_SUBPIXEL_VTX_LINE, 4);
}

// THE FRACTIONAL DIGITS FROM THE MIRROR TO THE VERTEX.
//
// _p0/_p1 are the two free bytes in GrVertex. They were already bound as a
// vertex attribute (a_extra.zw in platform/native_shaders.inc), so the route
// there costs no new binding and no second pipeline. x/y stay integers - so
// the UI mapper, the size count and the clip windows go on computing as
// before, and the dither point in the shader still sits on the PSX pixel,
// where it belongs.
//
// If there is no fractional part, the two bytes stay zero - the memset in the
// builder has already cleared them - and the shader adds a zero.

// The type of the primitive being built right now - NotePolySize sees it before
// every builder. Only for the subpixel count.
internal int s_subpixelPrimType = -1;

internal void NativeGpu_ApplySubpixel(GrVertex *vertex, VERTTYPE *src)
{
	int fx;
	int fy;

	if (!NativeSubpixel_Lookup(src, &fx, &fy, s_gpu.uiViewActive ? 1 : 0, s_subpixelPrimType))
	{
		return;
	}

	vertex->_p0 = (s8)fx;
	vertex->_p1 = (s8)fy;
	NativeSubpixel_NoteVertexCarried(1);
}

void MakeVertexTriangle(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2)
{
	assert(p0);
	assert(p1);
	assert(p2);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 3);

	vertex[0].x = p0[0] + ofsX;
	vertex[0].y = p0[1] + ofsY;

	vertex[1].x = p1[0] + ofsX;
	vertex[1].y = p1[1] + ofsY;

	vertex[2].x = p2[0] + ofsX;
	vertex[2].y = p2[1] + ofsY;

	NativeGpu_ApplySubpixel(&vertex[0], p0);
	NativeGpu_ApplySubpixel(&vertex[1], p1);
	NativeGpu_ApplySubpixel(&vertex[2], p2);

	NativeSubpixel_NoteVertices(NATIVE_SUBPIXEL_VTX_TRI, 3);
}

void MakeVertexQuad(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2, VERTTYPE *p3)
{
	assert(p0);
	assert(p1);
	assert(p2);
	assert(p3);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	vertex[0].x = p0[0] + ofsX;
	vertex[0].y = p0[1] + ofsY;

	vertex[1].x = p1[0] + ofsX;
	vertex[1].y = p1[1] + ofsY;

	vertex[2].x = p2[0] + ofsX;
	vertex[2].y = p2[1] + ofsY;

	vertex[3].x = p3[0] + ofsX;
	vertex[3].y = p3[1] + ofsY;

	NativeGpu_ApplySubpixel(&vertex[0], p0);
	NativeGpu_ApplySubpixel(&vertex[1], p1);
	NativeGpu_ApplySubpixel(&vertex[2], p2);
	NativeGpu_ApplySubpixel(&vertex[3], p3);

	NativeSubpixel_NoteVertices(NATIVE_SUBPIXEL_VTX_QUAD, 4);
}

void MakeVertexRect(GrVertex *vertex, VERTTYPE *p0, s16 w, s16 h)
{
	assert(p0);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	vertex[0].x = p0[0] + ofsX;
	vertex[0].y = p0[1] + ofsY;

	vertex[1].x = vertex[0].x;
	vertex[1].y = vertex[0].y + h;

	vertex[2].x = vertex[0].x + w;
	vertex[2].y = vertex[0].y + h;

	vertex[3].x = vertex[0].x + w;
	vertex[3].y = vertex[0].y;

	NativeSubpixel_NoteVertices(NATIVE_SUBPIXEL_VTX_RECT, 4);
}

// Which rows of a texture page a primitive is about to read.
//
// The page store keeps one tile per page and a tile is the whole 256-row page.
// The frame buffer shares page columns with the textures and covers the top 216
// rows of them, so packing the presented frame back into VRAM invalidated those
// tiles every frame - for rows nothing reads. Recording the rows that ARE read
// is what lets that be decided instead of assumed.
//
// The tpage word here is the same one AddSplit read a moment ago: the primitive
// parse assigns activeDrawEnv.tpage = poly->tpage before it opens the split. So
// the tile named there and the tile whose rows are recorded here are one tile,
// and that has to stay true - a mismatch would keep rows fresh on the wrong
// tile, which is the one way this can go stale.
internal void NativeGpu_NoteTexcoordRows(s16 tpage, int rowLo, int rowHi)
{
	const TexFormat format = GetTPageFormat(tpage);

	if ((format != TF_4_BIT) && (format != TF_8_BIT))
	{
		return;
	}

	NativeRenderer_NotePageRows(GetTPageBase(tpage), format == TF_4_BIT, rowLo, rowHi);
}

internal int NativeGpu_RowLo(int a, int b, int c, int d)
{
	int lo = a;

	if (b < lo)
	{
		lo = b;
	}
	if (c < lo)
	{
		lo = c;
	}
	if (d < lo)
	{
		lo = d;
	}

	return lo;
}

internal int NativeGpu_RowHi(int a, int b, int c, int d)
{
	int hi = a;

	if (b > hi)
	{
		hi = b;
	}
	if (c > hi)
	{
		hi = c;
	}
	if (d > hi)
	{
		hi = d;
	}

	return hi;
}

void MakeTexcoordQuad(GrVertex *vertex, u8 *uv0, u8 *uv1, u8 *uv2, u8 *uv3, s16 page, s16 clut, u8 dither)
{
	assert(uv0);
	assert(uv1);
	assert(uv2);
	assert(uv3);

	const u8 bright = 2;
	const short texPage = NativeGpu_VertexPageWord(page);

	vertex[0].u = uv0[0];
	vertex[0].v = uv0[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv1[0];
	vertex[1].v = uv1[1];
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv2[0];
	vertex[2].v = uv2[1];
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;

	vertex[3].u = uv3[0];
	vertex[3].v = uv3[1];
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = texPage;
	vertex[3].clut = clut;

	NativeGpu_NoteTexcoordRows(page, NativeGpu_RowLo(uv0[1], uv1[1], uv2[1], uv3[1]), NativeGpu_RowHi(uv0[1], uv1[1], uv2[1], uv3[1]));
}

void MakeTexcoordTriangle(GrVertex *vertex, u8 *uv0, u8 *uv1, u8 *uv2, s16 page, s16 clut, u8 dither)
{
	assert(uv0);
	assert(uv1);
	assert(uv2);

	const u8 bright = 2;
	const short texPage = NativeGpu_VertexPageWord(page);

	vertex[0].u = uv0[0];
	vertex[0].v = uv0[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv1[0];
	vertex[1].v = uv1[1];
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv2[0];
	vertex[2].v = uv2[1];
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;

	NativeGpu_NoteTexcoordRows(page, NativeGpu_RowLo(uv0[1], uv1[1], uv2[1], uv2[1]), NativeGpu_RowHi(uv0[1], uv1[1], uv2[1], uv2[1]));
}

void MakeTexcoordRect(GrVertex *vertex, u8 *uv, s16 page, s16 clut, s16 w, s16 h)
{
	assert(uv);

	// sim overflow
	if ((int)uv[0] + w > 255)
	{
		w = 255 - uv[0];
	}
	if ((int)uv[1] + h > 255)
	{
		h = 255 - uv[1];
	}

	const u8 bright = 2;
	const u8 dither = 0;
	const short texPage = NativeGpu_VertexPageWord(page);

	vertex[0].u = uv[0];
	vertex[0].v = uv[1];
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = texPage;
	vertex[0].clut = clut;

	vertex[1].u = uv[0];
	vertex[1].v = uv[1] + h;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = texPage;
	vertex[1].clut = clut;

	vertex[2].u = uv[0] + w;
	vertex[2].v = uv[1] + h;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = texPage;
	vertex[2].clut = clut;

	vertex[3].u = uv[0] + w;
	vertex[3].v = uv[1];
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = texPage;
	vertex[3].clut = clut;

	// The rect's own height, not just the texel it names: a sprite covers h
	// rows below its corner.
	NativeGpu_NoteTexcoordRows(page, uv[1], uv[1] + h);

	if (g_cfg_bilinearFiltering)
	{
		vertex[0].tcx = -1;
		vertex[0].tcy = -1;

		vertex[1].tcx = -1;
		vertex[1].tcy = -1;

		vertex[2].tcx = -1;
		vertex[2].tcy = -1;

		vertex[3].tcx = -1;
		vertex[3].tcy = -1;
	}
}

void MakeTexcoordLineZero(GrVertex *vertex, u8 dither)
{
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;

	vertex[3].u = 0;
	vertex[3].v = 0;
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = 0;
	vertex[3].clut = 0;
}

void MakeTexcoordTriangleZero(GrVertex *vertex, u8 dither)
{
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;
}

void MakeTexcoordQuadZero(GrVertex *vertex, u8 dither)
{
	const u8 bright = 1;

	vertex[0].u = 0;
	vertex[0].v = 0;
	vertex[0].bright = bright;
	vertex[0].dither = dither;
	vertex[0].page = 0;
	vertex[0].clut = 0;

	vertex[1].u = 0;
	vertex[1].v = 0;
	vertex[1].bright = bright;
	vertex[1].dither = dither;
	vertex[1].page = 0;
	vertex[1].clut = 0;

	vertex[2].u = 0;
	vertex[2].v = 0;
	vertex[2].bright = bright;
	vertex[2].dither = dither;
	vertex[2].page = 0;
	vertex[2].clut = 0;

	vertex[3].u = 0;
	vertex[3].v = 0;
	vertex[3].bright = bright;
	vertex[3].dither = dither;
	vertex[3].page = 0;
	vertex[3].clut = 0;
}

void MakeColourNoShade(GrVertex *vertex, int n)
{
	--n;
	while (n >= 0)
	{
		vertex[n].r = 128;
		vertex[n].g = 128;
		vertex[n].b = 128;
		vertex[n].a = 255;
		--n;
	}
}

void MakeColourLine(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}
	assert(col0);
	assert(col1);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col1[0];
	vertex[2].g = col1[1];
	vertex[2].b = col1[2];
	vertex[2].a = 255;

	vertex[3].r = col0[0];
	vertex[3].g = col0[1];
	vertex[3].b = col0[2];
	vertex[3].a = 255;
}

void MakeColourTriangle(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 3);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	vertex[2].a = 255;
}

void MakeColourQuad(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2, u8 *col3)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);
	assert(col3);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	vertex[2].a = 255;

	vertex[3].r = col3[0];
	vertex[3].g = col3[1];
	vertex[3].b = col3[2];
	vertex[3].a = 255;
}

void TriangulateQuad()
{
	/*
	Triangulate like this:

	v0--v1
	|  / |
	| /  |
	v2--v3

	NOTE: v2 is swapped with v3 during primitive parsing, which is not shown here
	*/

	s_gpu.vertexBuffer[s_gpu.vertexIndex + 4] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 3];

	s_gpu.vertexBuffer[s_gpu.vertexIndex + 5] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 2];
	s_gpu.vertexBuffer[s_gpu.vertexIndex + 2] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 3];
	s_gpu.vertexBuffer[s_gpu.vertexIndex + 3] = s_gpu.vertexBuffer[s_gpu.vertexIndex + 1];
}

//------------------------------------------------------------------------------------------------------------------------

internal bool NativeGpu_RectOverlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
	return (aw > 0) && (ah > 0) && (bw > 0) && (bh > 0) && (ax < bx + bw) && (bx < ax + aw) && (ay < by + bh) && (by < ay + ah);
}

internal bool NativeGpu_TPageOverlapsActiveDrawPage(int tpage)
{
	const int pageX = (tpage & 0xf) << 6;
	const int pageY = (tpage & 0x10) ? 0x100 : 0;
	const int pageW = 0x100;
	const int pageH = 0x100;

	if (!activeDrawEnv.dfe)
	{
		return false;
	}

	if (GetTPageFormat(tpage) != TF_16_BIT)
	{
		return false;
	}

	return NativeGpu_RectOverlaps(pageX, pageY, pageW, pageH, activeDrawEnv.clip.x, activeDrawEnv.clip.y, activeDrawEnv.clip.w, activeDrawEnv.clip.h);
}

// --feedback-report (main.c): one log line per feedback pack with VBlank,
// rectangle and tpage, at most 500 - so that a snapshot can prove that its
// frame had a feedback, and from where. Default off.
int g_cfg_feedbackReport = 0;

internal void NativeGpu_PrepareFramebufferFeedback(int tpage)
{
	if (!NativeGpu_TPageOverlapsActiveDrawPage(tpage))
	{
		return;
	}

	if (s_gpu.framebufferFeedbackRunActive)
	{
		return;
	}

	// NOTE(aalhendi): PS1 can draw into VRAM and immediately texture from that
	// same draw page. Native batches primitives, so screen-feedback effects
	// like heat warp need an explicit barrier before their framebuffer-sampling
	// polygons consume the VRAM texture.
	if (NativeGpu_HasPendingSplits())
	{
		DrawAllSplits();
	}

	NativeRenderer_StoreFrameBuffer(activeDrawEnv.clip.x, activeDrawEnv.clip.y, activeDrawEnv.clip.w, activeDrawEnv.clip.h);
	s_gpu.framebufferFeedbackRunActive = true;

	if (g_cfg_feedbackReport)
	{
		static int s_feedbackLines = 0;

		if (s_feedbackLines < 500)
		{
			s_feedbackLines++;
			Platform_Log("[CTR Feedback] pack #%d at vblank %d: clip %d,%d %dx%d, tpage 0x%04x\n", s_feedbackLines, Platform_GetVBlankCount(),
			             activeDrawEnv.clip.x, activeDrawEnv.clip.y, activeDrawEnv.clip.w, activeDrawEnv.clip.h, tpage & 0xffff);
		}
	}
}

internal void AddSplit(bool semiTrans, bool textured, bool framebufferFeedback)
{
	int tpage = activeDrawEnv.tpage;

	if (framebufferFeedback)
	{
		NativeGpu_PrepareFramebufferFeedback(tpage);
	}
	else
	{
		s_gpu.framebufferFeedbackRunActive = false;
	}

	GPUDrawSplit *curSplit = &s_gpu.splits[s_gpu.splitIndex];

	BlendMode blendMode = semiTrans ? GET_TPAGE_BLEND(tpage) : BM_NONE;
	TexFormat texFormat = GetTPageFormat(tpage);
	TextureID textureId = textured ? NativeRenderer_GetVRAMTexture() : NativeRenderer_GetWhiteTexture();
	bool psxTexturedSemiTrans = semiTrans && textured && s_gpu.overrideTexture == 0;
	// NOTE(aalhendi): PS1 framebuffer bit 15 follows sampled texture STP for
	// textured draws unless E6 forces it. Recursive screen-copy effects depend
	// on this bit surviving after the blended textured pass.
	bool psxTextureOutputSTP = textured && s_gpu.overrideTexture == 0;

	if (textured && s_gpu.overrideTexture != 0)
	{
		// override texture format, zero tpage
		texFormat = TF_32_BIT_RGBA;
		textureId = s_gpu.overrideTexture;
		psxTexturedSemiTrans = false;

		// A HOST TEXTURE IS A REAL PICTURE, SO ITS TRANSPARENCY
		// ALSO MEANS SOMETHING ELSE.
		//
		// GET_TPAGE_BLEND further up gives one of the four PSX modes, and all
		// four weight with a CONSTANT - that is what the hardware
		// could do. An RGBA picture brings its weight per texel along itself, and
		// BM_ALPHA is the mode that exists exactly for that
		// (native_renderer_types.h, blend state in native_gfx_vk.c).
		//
		// Nothing else chooses it: the blend mode is otherwise set
		// exclusively from the tpage bits, and those do not know BM_ALPHA.
		//
		// Without transparency it stays BM_NONE - an opaque picture is meant to stay
		// opaque, even if its texture has an alpha channel.
		if (semiTrans)
		{
			blendMode = BM_ALPHA;
		}
	}
	else if (textured && ((texFormat == TF_4_BIT) || (texFormat == TF_8_BIT)))
	{
		// The one place that knows which texture page a draw is about to read
		// from. The page store needs a tile for it, and needs it now: this runs
		// while primitives are parsed, and the fill happens in the
		// NativeRenderer_UpdateVRAM that opens the draw batch.
		//
		// GetTPageBase is the same page number the vertex stage encodes, so the
		// tile the fill writes and the tile the shader reads are worked out from
		// one value rather than two.
		NativeRenderer_UsePageTile(GetTPageBase(tpage), texFormat == TF_4_BIT);
	}

	if (psxTexturedSemiTrans && (blendMode >= BM_AVERAGE) && (blendMode <= BM_ADD_QUATER_SOURCE))
	{
		s_gpuSemiByModeWindow[(int)blendMode - (int)BM_AVERAGE]++;
	}

	// ONE DRAW INSTEAD OF TWO. Modes 0, 1 and 3 blend per texel through
	// the second blend source (BM_*_TEXEL, see native_renderer_types.h); with that
	// such a primitive is an ordinary split participant and the
	// painter's order between primitives is right by itself - each one is
	// drawn once and whole. Only mode 2 (subtraction) keeps the two
	// passes and with them its own split per primitive (and the
	// box merge of NativeGpu_JoinSemiSplit).
	if (psxTexturedSemiTrans && g_gfx_dualSourceBlend && !g_cfg_semiTwoPass && (blendMode != BM_SUBTRACT))
	{
		blendMode = (blendMode == BM_AVERAGE) ? BM_AVERAGE_TEXEL : (blendMode == BM_ADD) ? BM_ADD_TEXEL : BM_ADD_QUATER_SOURCE_TEXEL;
		psxTexturedSemiTrans = false;
	}

	// ANTI-ALIASING: sample shading for every textured
	// draw, host textures included. Every sample is then shaded
	// individually, and the discard of a transparent texel only hits its
	// sample - that way the edges of cut-out textures are smoothed too,
	// not only polygon edges.
	const bool sampleShading = textured && (g_gfx_sampleShading != 0);

	// MEASUREMENT: which pages are used, and WHY a new split
	// comes into being right away. The comparison below is unchanged; the
	// measurement only reads along, in the same order.
	if (textured && s_gpu.overrideTexture == 0)
	{
		s_splitPagesUsed[(texFormat == TF_4_BIT) ? 0 : (texFormat == TF_8_BIT) ? 1 : 2] |= 1u << (GetTPageBase(tpage) & 31);
	}
	int splitReason = -1;
	int splitForcedSame = 0;
	// ONE comparison for measurement and decision: the chain of twelve fields
	// is evaluated once per primitive, and the measured value decides - the
	// same fields, the same order.
	const int sameState = curSplit->blendMode == blendMode && curSplit->textureId == textureId &&
	                      curSplit->drawPrimMode == s_gpu.drawPrimMode && curSplit->psxTexturedSemiTrans == psxTexturedSemiTrans &&
	                      curSplit->psxTextureOutputSTP == psxTextureOutputSTP && curSplit->psxDrawMaskSet == s_gpu.psxDrawMaskSet &&
	                      curSplit->drawenv.clip.x == activeDrawEnv.clip.x && curSplit->drawenv.clip.y == activeDrawEnv.clip.y &&
	                      curSplit->drawenv.clip.w == activeDrawEnv.clip.w && curSplit->drawenv.clip.h == activeDrawEnv.clip.h &&
	                      curSplit->drawenv.dfe == activeDrawEnv.dfe && curSplit->debugText == s_gpu.currentSplitDebugText;
	{
		// No longer a reason since the format travels in the vertex, but counted: how often the format
		// changes while the split runs on - those are the draws that
		// no longer exist.
		if (textured && s_gpu.overrideTexture == 0 && texFormat != s_gpuLastPrimFormat && s_gpuLastPrimFormat != (TexFormat)0xFFFF && !psxTexturedSemiTrans && sameState)
		{
			s_splitReason[NGPU_SR_TEXFMT]++;
		}
		if (textured && s_gpu.overrideTexture == 0)
		{
			s_gpuLastPrimFormat = texFormat;
		}
		if (psxTexturedSemiTrans)
		{
			splitReason = NGPU_SR_FORCED_SEMI;
			splitForcedSame = sameState;
		}
		else if (sameState)
		{
			splitReason = -1;
		}
		else if (s_gpu.splitIndex == 0)
		{
			splitReason = NGPU_SR_FIRST;
		}
		else if (curSplit->blendMode != blendMode)
		{
			splitReason = NGPU_SR_BLEND;
		}
		else if (curSplit->textureId != textureId)
		{
			splitReason = NGPU_SR_TEXID;
		}
		else if (curSplit->drawPrimMode != s_gpu.drawPrimMode)
		{
			splitReason = NGPU_SR_PRIMMODE;
		}
		else if (curSplit->psxTexturedSemiTrans != psxTexturedSemiTrans)
		{
			splitReason = NGPU_SR_SEMIFLAG;
		}
		else if (curSplit->psxTextureOutputSTP != psxTextureOutputSTP)
		{
			splitReason = NGPU_SR_STP;
		}
		else if (curSplit->psxDrawMaskSet != s_gpu.psxDrawMaskSet)
		{
			splitReason = NGPU_SR_MASK;
		}
		else if (curSplit->drawenv.clip.x != activeDrawEnv.clip.x || curSplit->drawenv.clip.y != activeDrawEnv.clip.y ||
		         curSplit->drawenv.clip.w != activeDrawEnv.clip.w || curSplit->drawenv.clip.h != activeDrawEnv.clip.h)
		{
			splitReason = NGPU_SR_CLIP;
		}
		else if (curSplit->drawenv.dfe != activeDrawEnv.dfe)
		{
			splitReason = NGPU_SR_DFE;
		}
		else
		{
			splitReason = NGPU_SR_DBGTEXT;
		}
	}

	// No texture format in the comparison: it travels in the vertex. The split
	// remembers below the format of its first primitive, and only so that
	// DrawSplit keeps the VRAM program and the 32-bit override apart -
	// the override has its own textureId anyway.
	// FIXME: compare drawing environment too?
	if (!psxTexturedSemiTrans && sameState)
	{
		return;
	}

	curSplit->numVerts = s_gpu.vertexIndex - curSplit->startVertex;

	if (s_gpu.splitIndex + 1 >= MAX_DRAW_SPLITS)
	{
		// REPORT ONCE, THEN COUNT.
		//
		// One log line per overflowed split, in the middle of the OT walk,
		// was measured on a heavy custom track at 29,224 formatted lines in
		// one run - about 37 per frame, into a 4.2 MB file. draw_otag_parse
		// rose from 0.053 ms to 1.633 ms, thirty-one times as much, while the
		// primitives only quadrupled.
		//
		// A counter that floods exactly when things are going badly anyway
		// measures itself. So: say it once, then count and name the total at
		// the end.
		s_gpuSplitOverflows++;

		if (!s_gpu.splitOverflowActive)
		{
			s_gpu.splitOverflowActive = true;
			s_gpuSplitOverflowRuns++;
		}

		if (!s_gpuSplitOverflowSaid)
		{
			s_gpuSplitOverflowSaid = 1;
			NATIVE_GPU_ERROR("%s\n", "MAX_DRAW_SPLITS reached (too many blend modes, texture formats, drawEnv clip rects, dfe switches), expect rendering errors");
			NATIVE_GPU_ERROR("%s\n", "said once - the total follows at exit");
		}

		return;
	}

	GPUDrawSplit *split = &s_gpu.splits[++s_gpu.splitIndex];
	split->blendMode = blendMode;
	split->texFormat = texFormat;
	split->textureId = textureId;
	split->drawPrimMode = s_gpu.drawPrimMode;
	split->psxTexturedSemiTrans = psxTexturedSemiTrans;
	split->psxTextureOutputSTP = psxTextureOutputSTP;
	split->psxDrawMaskSet = s_gpu.psxDrawMaskSet;
	split->sampleShading = sampleShading;
	split->drawenv = activeDrawEnv;
	split->dispenv = activeDispEnv;
	split->debugText = s_gpu.currentSplitDebugText;

	split->drawenv.tw.w = s_gpu.overrideTextureWidth;
	split->drawenv.tw.h = s_gpu.overrideTextureHeight;

	split->startVertex = s_gpu.vertexIndex;
	split->numVerts = 0;
	split->boxX0 = 32767;
	split->boxY0 = 32767;
	split->boxX1 = -32768;
	split->boxY1 = -32768;

	// MEASUREMENT: the split is created, now it counts.
	if (splitReason >= 0)
	{
		s_splitReason[splitReason]++;
	}
	s_splitForcedSameState += (unsigned long long)splitForcedSame;
	NativeGpu_SplitNoteState(s_gpu.splitIndex);
}

void DrawSplit(const GPUDrawSplit *split)
{
	if (split->debugText)
	{
		NativeRenderer_PushDebugLabel(split->debugText);
	}

	const bool drawOnScreen = split->drawenv.dfe;
	if ((split->drawenv.clip.w <= 0) || (split->drawenv.clip.h <= 0))
	{
		// NOTE(aalhendi): VS/Battle end previews can shrink a losing viewport
		// into an empty retail draw area. Empty clips should consume no pixels,
		// and must not leak stale native offscreen/scissor state.
		NativeRenderer_SetupClipMode(&split->drawenv.clip, &split->dispenv, drawOnScreen);
		NativeRenderer_SetOffscreenState(&split->drawenv.clip, 0);
		if (split->debugText)
		{
			NativeRenderer_PopDebugLabel();
		}
		return;
	}

	NativeRenderer_SetTexture(split->textureId, split->texFormat);

	if (split->texFormat == TF_32_BIT_RGBA)
	{
		NativeRenderer_SetOverrideTextureSize(split->drawenv.tw.w, split->drawenv.tw.h);
	}

	NativeRenderer_SetPSXDrawMaskSet(split->psxDrawMaskSet);
	NativeRenderer_SetPSXTextureOutputSTP(split->psxTextureOutputSTP);

	// Anti-aliasing: applies only to the draws of this split; switched off again at the end,
	// so that fill rectangles and blits in between do not inherit it.
	if (split->sampleShading)
	{
		NativeRenderer_SetSampleShading(1);
	}

	NativeRenderer_SetupClipMode(&split->drawenv.clip, &split->dispenv, drawOnScreen);
	NativeRenderer_SetOffscreenState(&split->drawenv.clip, !drawOnScreen);
	NativeRenderer_SetProjection(&split->drawenv.clip, &split->dispenv, !drawOnScreen);

	// The split's start vertex counts from the batch it was recorded in, and a
	// batch no longer always begins at the front of the buffer - see the note on
	// NativeRenderer_UpdateVertexBuffer. The two are added here, at the one place
	// that draws split geometry.
	const int firstVertex = NativeRenderer_VertexUploadBase() + split->startVertex;

	if (split->psxTexturedSemiTrans)
	{
		// NOTE(aalhendi): CTR native renderer divergence from upstream PsyCross.
		// PS1 textured ABE only blends texels whose sampled 16-bit color has STP
		// set; non-STP texels remain opaque. Native split state is per draw,
		// so draw this primitive-sized split twice with shader-side STP masks.
		NativeRenderer_SetBlendMode(BM_NONE);
		NativeRenderer_SetPSXTextureSemiTransPass(1);
		NativeRenderer_DrawTriangles(firstVertex, split->numVerts / 3);
		s_gpuDrawsWindow++;

		NativeRenderer_SetBlendMode(split->blendMode);
		NativeRenderer_SetPSXTextureSemiTransPass(2);
		NativeRenderer_DrawTriangles(firstVertex, split->numVerts / 3);
		s_gpuDrawsWindow++;

		NativeRenderer_SetPSXTextureSemiTransPass(0);
	}
	else
	{
		// The texel-weighted modes tell the shader which weight
		// it writes into the second output (3 = average, 4 = addition, 5 = quarter).
		const int texelPass = (split->blendMode == BM_AVERAGE_TEXEL) ? 3 : (split->blendMode == BM_ADD_TEXEL) ? 4 : (split->blendMode == BM_ADD_QUATER_SOURCE_TEXEL) ? 5 : 0;

		NativeRenderer_SetBlendMode(split->blendMode);
		NativeRenderer_SetPSXTextureSemiTransPass(texelPass);
		NativeRenderer_DrawTriangles(firstVertex, split->numVerts / 3);
		s_gpuDrawsWindow++;
		if (texelPass != 0)
		{
			NativeRenderer_SetPSXTextureSemiTransPass(0);
		}
	}

	if (split->sampleShading)
	{
		NativeRenderer_SetSampleShading(0);
	}

	if (split->debugText)
	{
		NativeRenderer_PopDebugLabel();
	}
}

internal void SetPSXMaskState(u32 code)
{
	s_gpu.psxDrawMaskSet = (code & 1) != 0;
}

//
// Draws all polygons after AggregatePTAG
//
void DrawAllSplits()
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS);
	// MEASURING LINE: how many vertices this batch has written. The buffer
	// has MAX_VERTEX_BUFFER_SIZE slots and directly behind it lies the
	// split table; every vertex beyond that stands in a split.
	{
		static int s_overflowLines = 0;
		if (s_gpu.splitIndex > s_gpuSplitPeak)
		{
			s_gpuSplitPeak = s_gpu.splitIndex;
		}
		if (s_gpu.splitIndex > s_gpuSplitPeakWindow)
		{
			s_gpuSplitPeakWindow = s_gpu.splitIndex;
		}
		if (s_gpu.vertexIndex > s_gpuVertexPeakWindow)
		{
			s_gpuVertexPeakWindow = s_gpu.vertexIndex;
		}
		if ((s_gpu.vertexIndex > (int)MAX_VERTEX_BUFFER_SIZE) && (s_overflowLines < 400))
		{
			s_overflowLines++;
			Platform_Log("[CTR GPU] batch wrote %d vertices, %d past the %d-vertex buffer - into the split table (%d splits)\n", s_gpu.vertexIndex,
			             s_gpu.vertexIndex - (int)MAX_VERTEX_BUFFER_SIZE, (int)MAX_VERTEX_BUFFER_SIZE, s_gpu.splitIndex);
		}
	}
	// CPU-originated LoadImage, MoveImage, and fill commands are GPU-visible
	// before the next draw batch, matching PS1 command ordering.
	NativeRenderer_UpdateVRAM();
#ifdef CTR_INTERNAL
	if (g_dbg_emulatorPaused)
	{
		for (int i = 0; i < 3; i++)
		{
			GrVertex *vert = &s_gpu.vertexBuffer[g_dbg_polygonSelected + i];
			vert->r = 255;
			vert->g = 0;
			vert->b = 0;

			NATIVE_GPU_LOG("%s\n", "==========================================");
			NATIVE_GPU_LOG("POLYGON: %d\n", g_dbg_polygonSelected);
			NATIVE_GPU_LOG("X: %d Y: %d\n", vert->x, vert->y);
			NATIVE_GPU_LOG("U: %d V: %d\n", vert->u, vert->v);
			NATIVE_GPU_LOG("TP: %d CLT: %d\n", vert->page, vert->clut);

			NATIVE_GPU_LOG("%s\n", "==========================================");
		}

		Platform_PollHostEvents();
	}
#endif

	// Every UI primitive noted so far is grouped into elements, each element
	// gets one anchor, and only then do vertices move. Here rather than at the
	// end of the ordering-table walk, because a FILL or a MoveImage packet
	// inside a table flushes through this function mid-walk and ClearSplits
	// resets the vertex cursor: the recorded ranges are valid up to here and
	// not past it.
	NativeGpu_ApplyUIMapping();

	// next code ideally should be called before EndScene
	NativeRenderer_UpdateVertexBuffer(s_gpu.vertexBuffer, s_gpu.vertexIndex);

	// MEASUREMENT: states per batch, and in every 30th frame the box check.
	if (s_gpu.splitIndex > 0)
	{
		s_splitBatches++;
		s_splitTotal += (unsigned long long)s_gpu.splitIndex;
		s_splitDistinctSum += (unsigned long long)s_batchDistinct;
		if (s_batchDistinct > s_splitDistinctPeak)
		{
			s_splitDistinctPeak = s_batchDistinct;
		}
		if (s_auditThisFrame)
		{
			NativeGpu_SplitAuditBatch();
		}
	}

	for (int i = 1; i <= s_gpu.splitIndex; i++)
	{
		DrawSplit(&s_gpu.splits[i]);
	}

	ClearSplits();
	NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_ALL_SPLITS);
}

// forward declarations
int ParsePrimitive(P_TAG *polyTag);
int ParseTaglessPrimitive(u32 *command);

internal bool NativeGpu_IsValidOTLink(uintptr_t link)
{
	if (NativeGpuLinks_IsRegisteredHostPointer((const void *)link))
	{
		return (link & (sizeof(u32) - 1)) == 0;
	}

	return false;
}

internal u32 NativeGpu_ReadPacketWordForLog(uintptr_t packet, int wordIndex)
{
	const struct PlatformMempackArena *arena = Platform_GetMempackArena();
	const uintptr_t word = packet + (uintptr_t)wordIndex * sizeof(u32);
	const uintptr_t end = word + sizeof(u32);

	if ((NativeGpuLinks_IsRegisteredHostRange((const void *)word, sizeof(u32))) && ((word & (sizeof(u32) - 1)) == 0))
	{
		return *(const u32 *)word;
	}

	if ((word < (uintptr_t)arena->base) || (end > (uintptr_t)arena->endOfMemory) || ((word & (sizeof(u32) - 1)) != 0))
	{
		return 0xffffffffu;
	}

	return *(const u32 *)word;
}

internal void NativeGpu_FormatPointerRegion(char *dst, size_t dstSize, uintptr_t ptr)
{
	if ((sdata == NULL) || (sdata->gGT == NULL))
	{
		snprintf(dst, dstSize, "no-gGT");
		return;
	}

	struct GameTracker *gGT = sdata->gGT;

	for (int playerIndex = 0; playerIndex < 4; playerIndex++)
	{
		struct PushBuffer *pb = &gGT->pushBuffer[playerIndex];
		const uintptr_t start = (uintptr_t)pb->ptrOT;
		const uintptr_t end = (uintptr_t)pb->renderBucketOTRangeEnd;
		if ((start != 0) && (end != 0) && (ptr >= start) && (ptr <= end))
		{
			snprintf(dst, dstSize, "pb%d.ot+0x%zx", playerIndex, (size_t)(ptr - start));
			return;
		}
	}

	struct PushBuffer *uiPB = &gGT->pushBuffer_UI;
	const uintptr_t uiStart = (uintptr_t)uiPB->ptrOT;
	const uintptr_t uiEnd = (uintptr_t)uiPB->renderBucketOTRangeEnd;
	if ((uiStart != 0) && (uiEnd != 0) && (ptr >= uiStart) && (ptr <= uiEnd))
	{
		snprintf(dst, dstSize, "ui.ot+0x%zx", (size_t)(ptr - uiStart));
		return;
	}

	for (int dbIndex = 0; dbIndex < 2; dbIndex++)
	{
		struct DB *db = &gGT->db[dbIndex];
		const uintptr_t primStart = (uintptr_t)db->primMem.start;
		const uintptr_t primEnd = (uintptr_t)db->primMem.end;
		if ((primStart != 0) && (ptr >= primStart) && (ptr < primEnd))
		{
			snprintf(dst, dstSize, "db%d.prim+0x%zx", dbIndex, (size_t)(ptr - primStart));
			return;
		}

		const uintptr_t otStart = (uintptr_t)db->otMem.start;
		const uintptr_t otEnd = (uintptr_t)db->otMem.end;
		if ((otStart != 0) && (ptr >= otStart) && (ptr < otEnd))
		{
			snprintf(dst, dstSize, "db%d.ot+0x%zx", dbIndex, (size_t)(ptr - otStart));
			return;
		}
	}

	snprintf(dst, dstSize, "unknown");
}

//----------------------------------------------------------------------------------------
// THE UI SAFE AREA
//
// The world opens Hor+ and the presentation shows the 512-column picture in a
// wider box. For anything drawn straight into that buffer without going through
// the world projection - which is the whole HUD - those two together are a
// horizontal stretch by exactly the aspect fraction. So the UI is pulled in by
// the same fraction here, and the presentation stretch puts it back at its
// authored proportions.
//
// WHEN, and why not at the end of the walk.
//
// The mapping runs from DrawAllSplits, immediately before the vertex buffer is
// uploaded. That is not where it reads most naturally - the walk is the thing
// that produced these primitives - but it is the only place that is correct.
// A FILL or a MoveImage packet INSIDE an ordering table flushes the pending
// splits mid-walk (see ParseTaglessPrimitive), and a flush calls ClearSplits,
// which resets the vertex cursor to zero. Every firstVertex/lastVertex noted
// before such a flush would then point at somebody else's vertices. Mapping at
// the upload means the recorded ranges are always the ones still in the buffer.

int g_cfg_uiSafeArea = 1;
int g_cfg_uiAnchorLegacy = 0;

// 1 = a declaration is only ever looked up for a whole element, the way it was
// before a flicker showed why that is not enough. See the note over the
// per-primitive pass in NativeGpu_ApplyUIMapping.
int g_cfg_uiAnchorPerGroup = 0;

// Watch the DECISIONS, not the boxes.
//
// A one-frame table cannot see a flicker. That is not a small gap - it is the
// whole question: an element that sits still in every single frame and in a
// different place in each of them reads as perfect in a one-frame table and as
// broken on screen. Boxes move every frame by design (a timer counts, a model
// turns); anchors, declarations and floor shifts must not. So this fingerprints
// only those three per element and prints the table when the fingerprint
// changes. Silence means nothing decided differently.
int g_cfg_uiWatch = 0;

// The VBlank the watch starts at, and it exists because the first run of it was
// useless.
//
// A boot sequence walks through a dozen different screens, and every one of them
// legitimately decides different things - that is not a flicker, that is a
// different screen. The watch spent its whole budget on the logos and the intro
// and stopped at VBlank 970, before the race it was pointed at even began. A
// budget spent before the question is asked is no measurement at all.
int g_cfg_uiWatchFrom = 0;

// Every early return is counted apart, because "it did not run" is not an
// answer - which of the conditions stopped it is.
int g_uiCalls = 0;
int g_uiSkipInactive = 0;
int g_uiSkipDisabled = 0;
int g_uiSkipRange = 0;
int g_uiSkipFullCanvas = 0;
int g_uiPrimsDropped = 0;
int g_uiGroupsDropped = 0;
int g_uiElementCount = 0;
int g_uiVerticesMapped = 0;
int g_uiVerticesMoved = 0;
int g_uiMaxShift = 0;

// The player menus, counted over the whole run and said at exit:
// how often the menu branch ran, how many primitives a driver window
// claimed, how many groups were merged into rows and columns,
// how many elements got a screen anchor - and how many TEXTURED
// primitives were stretched over the whole width. The last is the
// proof for the stretching rule: an area may be stretched, a picture may not.
unsigned long long g_uiMenuPasses = 0;
unsigned long long g_uiMenuWindowPrims = 0;
unsigned long long g_uiMenuMerges = 0;
unsigned long long g_uiMenuPolicyGroups = 0;
unsigned long long g_uiMenuFullCanvasPrims = 0;
unsigned long long g_uiMenuFullCanvasTextured = 0;
unsigned long long g_uiMenuFullCanvasGroups = 0;

// The composition zone (native_view.c, CTR_UI_BuildSafeArea): in how many
// passes it was narrower than the canvas, where it last lay, and how
// often the transition over the 4:3 edge pushed an element towards the picture edge (per
// element and pass).
// At 4:3 and 16:9 all three stay 0 - that is the proof in the log that the
// zone did not take effect there.
unsigned long long g_uiMenuZonePasses = 0;
unsigned long long g_uiMenuEdgeTransits = 0;
int g_uiMenuZoneLeft = 0;
int g_uiMenuZoneWidth = 0;

internal void NativeGpu_PrintMenuReport(void)
{
	Platform_Log("[CTR UIElem] at exit: menu mapping %llu pass(es); %llu primitive(s) claimed by a character-select window; %llu merge(s) into lines and columns; "
	             "%llu element(s) anchored by a screen rule; stretched across the canvas: %llu primitive(s), %llu of them textured, and %llu group(s)\n",
	             g_uiMenuPasses, g_uiMenuWindowPrims, g_uiMenuMerges, g_uiMenuPolicyGroups, g_uiMenuFullCanvasPrims, g_uiMenuFullCanvasTextured,
	             g_uiMenuFullCanvasGroups);
	if (g_uiMenuZonePasses == 0)
	{
		Platform_Log("[CTR UIElem] at exit: menu composition zone never narrower than the canvas; an element moved over the 4:3 edge %llu time(s)\n",
		             g_uiMenuEdgeTransits);
		return;
	}

	Platform_Log("[CTR UIElem] at exit: menu composition zone narrower than the canvas in %llu pass(es), last %d..%d; an element moved over the 4:3 edge "
	             "towards the canvas edge %llu time(s)\n",
	             g_uiMenuZonePasses, g_uiMenuZoneLeft, g_uiMenuZoneLeft + g_uiMenuZoneWidth - 1, g_uiMenuEdgeTransits);
}

// What the menu branch decided in this pass, computed once and
// kept for the element table.
global_variable struct
{
	int active;
	const char *policyName;
	int policy;
	int winActive[4];
	int winCore[4][4];
	int winRegion[4][4];
	int winHasBand[4];
	int winBand[4][4];
	int winShift[4];
} s_uiMenu;


// Which of the five UI ordering-table slots a packet sits in, or -1.
//
// The slot is the statement the game itself makes about layering: a screen
// dimming layer and the box drawn on top of it are different slots. Grouping has
// to respect that, or a background and the thing in front of it become one
// element because they touch.
//
// Five words, and the number is not chosen here: MainFrame_ResetDB puts the UI
// ordering table at otSwapchainDB+4 and the first player table at +0x18, so the
// UI owns exactly the twenty bytes between them.
internal int NativeGpu_UIOTSlotIndex(const void *packet)
{
	uintptr_t address;
	uintptr_t uiStart;
	uintptr_t uiEnd;

	if ((sdata == NULL) || (sdata->gGT == NULL) || (sdata->gGT->pushBuffer_UI.ptrOT == NULL))
	{
		return -1;
	}

	address = (uintptr_t)packet;
	uiStart = (uintptr_t)sdata->gGT->pushBuffer_UI.ptrOT;
	uiEnd = uiStart + 5 * sizeof(u32);

	if ((address < uiStart) || (address >= uiEnd))
	{
		return -1;
	}

	return (int)((address - uiStart) / sizeof(u32));
}

// Index 0 of the swapchain table: the word directly before the first UI slot.
// See the explanation at the chain walk in ParsePrimitivesLinkedList.
internal bool NativeGpu_IsUIOTRoot(const void *packet)
{
	if ((sdata == NULL) || (sdata->gGT == NULL) || (sdata->gGT->pushBuffer_UI.ptrOT == NULL))
	{
		return false;
	}

	return (uintptr_t)packet == ((uintptr_t)sdata->gGT->pushBuffer_UI.ptrOT - sizeof(u32));
}

internal void NativeGpu_NoteUIVertexRange(int firstVertex, const void *packet)
{
	struct NativeGpuUiPrim *prim;
	int i;

	(void)packet;

	g_uiCalls++;

	if (!s_gpu.uiViewActive)
	{
		g_uiSkipInactive++;
		return;
	}

	if (!s_gpu.uiViewEnabled)
	{
		g_uiSkipDisabled++;
		return;
	}

	if ((firstVertex < 0) || (firstVertex >= s_gpu.vertexIndex))
	{
		g_uiSkipRange++;
		return;
	}

	// A primitive drawn across the whole canvas IS the canvas.
	//
	// A fade, a dim, a separator between split-screen viewports: pulling it in
	// leaves the picture showing through unshaded on both sides. It has nothing
	// to be anchored to, which is what FULL_CANVAS means, and it is read off the
	// geometry rather than registered by the producer - a registration is a
	// second source that can fall behind the thing it describes.
	{
		int wholeX0 = (int)s_gpu.vertexBuffer[firstVertex].x;
		int wholeX1 = wholeX0;

		for (i = firstVertex; i < s_gpu.vertexIndex; i++)
		{
			const int vx = (int)s_gpu.vertexBuffer[i].x;

			if (vx < wholeX0) { wholeX0 = vx; }
			if (vx > wholeX1) { wholeX1 = vx; }
		}

		if ((wholeX0 <= NATIVE_UI_TOUCH_GAP) && (wholeX1 >= (s_gpu.uiView.virtualWidth - NATIVE_UI_TOUCH_GAP)))
		{
			// Counted in the menu, and whether it carries a texture: a stretched
			// area is the same area wider, a stretched picture is
			// distorted (the stretching rule). Underlaid areas carry page, CLUT and UV 0
			// (MakeTexcoord*Zero further up); everything else counts as a texture.
			if (CTR_UI_MenuMode())
			{
				int textured = 0;

				for (i = firstVertex; i < s_gpu.vertexIndex; i++)
				{
					const GrVertex *v = &s_gpu.vertexBuffer[i];

					if ((v->page != 0) || (v->clut != 0) || (v->u != 0) || (v->v != 0))
					{
						textured = 1;
					}
				}

				g_uiMenuFullCanvasPrims++;
				if (textured)
				{
					g_uiMenuFullCanvasTextured++;
				}
			}

			// STRETCHED AND FINISHED HERE, instead of skipped.
			//
			// While the display still stretched the picture, skipping was right: the display pulled the
			// whole 512 picture apart, and a fade that went from edge to edge in the authored
			// picture then also went from edge to edge in the one shown.
			// Since the canvas itself is wide, it only covers the
			// first 512 of 918 columns - the fade on the left, the picture on the right.
			//
			// No entry in the element table, and that is not economy: a
			// layer over the whole picture has no anchor to choose, no
			// declaration line to look for and no edge distance to keep. It
			// needs exactly one mapping, and that is in CTR_UI_MapX under
			// FULL_CANVAS - the same one that a tiled darkening gets over its
			// group.
			for (i = firstVertex; i < s_gpu.vertexIndex; i++)
			{
				const int beforeX = (int)s_gpu.vertexBuffer[i].x;
				const int afterX = CTR_UI_MapX(&s_gpu.uiView, beforeX, (int)CTR_UI_ANCHOR_FULL_CANVAS);

				g_uiVerticesMapped++;

				if (afterX != beforeX)
				{
					const int moved = (afterX > beforeX) ? (afterX - beforeX) : (beforeX - afterX);

					g_uiVerticesMoved++;
					if (moved > g_uiMaxShift) { g_uiMaxShift = moved; }

					s_gpu.vertexBuffer[i].x = (VERTTYPE)afterX;
				}
			}

			g_uiSkipFullCanvas++;
			return;
		}
	}

	if (s_gpu.uiPrimCount >= NATIVE_UI_PRIM_CAPACITY)
	{
		// Said, not skipped.
		g_uiPrimsDropped++;
		return;
	}

	prim = &s_gpu.uiPrims[s_gpu.uiPrimCount];

	prim->firstVertex = firstVertex;
	prim->lastVertex = s_gpu.vertexIndex;
	prim->x0 = prim->x1 = (int)s_gpu.vertexBuffer[firstVertex].x;
	prim->y0 = prim->y1 = (int)s_gpu.vertexBuffer[firstVertex].y;
	prim->group = -1;
	prim->decl = -1;
	prim->win = -1;
	prim->slot = s_gpu.uiOtSlot;

	for (i = firstVertex; i < s_gpu.vertexIndex; i++)
	{
		const int vx = (int)s_gpu.vertexBuffer[i].x;
		const int vy = (int)s_gpu.vertexBuffer[i].y;

		if (vx < prim->x0) { prim->x0 = vx; }
		if (vx > prim->x1) { prim->x1 = vx; }
		if (vy < prim->y0) { prim->y0 = vy; }
		if (vy > prim->y1) { prim->y1 = vy; }
	}

	s_gpu.uiPrimCount++;
}

internal int NativeGpu_UiBoxesTouch(const int *a, const int *b)
{
	const int gap = NATIVE_UI_TOUCH_GAP;

	return (a[0] <= (b[2] + gap)) && (b[0] <= (a[2] + gap)) && (a[1] <= (b[3] + gap)) && (b[1] <= (a[3] + gap));
}

internal void NativeGpu_UiGrowGroup(int *box, const int *other)
{
	if (other[0] < box[0]) { box[0] = other[0]; }
	if (other[1] < box[1]) { box[1] = other[1]; }
	if (other[2] > box[2]) { box[2] = other[2]; }
	if (other[3] > box[3]) { box[3] = other[3]; }
}

// One frame of elements, printed as a list of boxes in pixels.
//
// This is the only way a UI layout can be checked as numbers rather than as
// an impression. For each element it prints the authored box, the anchor, which
// row of the declaration table claimed it (or none), what the floor did to it,
// and the box it ended up with. The mapped box can then be recomputed OUTSIDE
// the build from the authored box, the anchor and the scale on the header line -
// so the check never compares the log against itself.
//
// The FIRST frame at or after the asked-for VBlank, not that exact one. The
// VBlank counter moves in steps and skips values, and "no UI element recorded"
// reads exactly like a screen with no UI on it. The header says which VBlank it
// actually landed on next to the one that was asked for.
int g_cfg_uiElementsAt = 0;
global_variable int s_uiElementsPrinted = 0;

global_variable const char *const s_uiAnchorNames[] = {"centre", "canvas", "left", "right"};

internal const char *NativeGpu_UiAnchorName(int anchor)
{
	return ((anchor >= 0) && (anchor <= 3)) ? s_uiAnchorNames[anchor] : "?";
}

// One frame of decisions, printed as a table.
//
// Two callers with two reasons: --ui-elements prints it once at a named VBlank,
// and --ui-watch prints it whenever a decision differs from the frame before.
// One printer, because a table that is read two ways has to say the same thing
// both times.
//
// Elements come first, then the primitives a row claimed on its own - those are
// no longer elements at all and would otherwise be missing from a table that
// claims to list the frame.
internal void NativeGpu_EmitUILine(const char *line)
{
	Platform_Log("%s\n", line);
}

#define NATIVE_GPU_UI_LINE 256

// The name of a group: its row in the declaration table of the UI, or "-".
internal const char *NativeGpu_UiGroupName(const struct NativeUiDecl *d)
{
	return (d != NULL) ? d->name : "-";
}

internal void NativeGpu_PrintUIDecisions(const char *why)
{
	char line[NATIVE_GPU_UI_LINE];
	int aspectW = 4;
	int aspectH = 3;
	int g;
	int row;
	int rowCount;

	CTR_View_GetWorldAspect(&aspectW, &aspectH);
	rowCount = NativeUiDecl_Count();

	snprintf(line, sizeof(line), "[CTR UIElem] %s vblank=%d aspect=%d:%d canvas=%dx%d wide=%d shift=%d touchgap=%d", why,
	         Platform_GetVBlankCount(), aspectW, aspectH, s_gpu.uiView.virtualWidth, s_gpu.uiView.virtualHeight,
	         s_gpu.uiView.canvasWidth, s_gpu.uiView.canvasWidth - s_gpu.uiView.virtualWidth, NATIVE_UI_TOUCH_GAP);
	NativeGpu_EmitUILine(line);

	snprintf(line, sizeof(line),
	         "[CTR UIElem] elements=%d prims=%d declPrim=%d declElem=%d/%d collisions=%d floor=%d/%dpx unsettled=%d "
	         "dropped=%d/%d",
	         s_gpu.uiGroupCount, s_gpu.uiPrimCount, g_uiDeclPrimHits, g_uiDeclHits, g_uiDeclHits + g_uiDeclMisses,
	         g_uiDeclCollisions, g_uiFloorClamps, g_uiFloorMaxShift, g_uiFloorUnsettled, g_uiPrimsDropped, g_uiGroupsDropped);
	NativeGpu_EmitUILine(line);

	// The menu branch: which screen anchor applied and where each
	// driver window went. Only in the menu, so that a race table looks as before.
	if (s_uiMenu.active)
	{
		int w;

		snprintf(line, sizeof(line), "[CTR UIElem] menu rule=edge screen=%s zone=%d..%d", (s_uiMenu.policyName != NULL) ? s_uiMenu.policyName : "-",
		         s_gpu.uiView.zoneLeft, s_gpu.uiView.zoneLeft + s_gpu.uiView.zoneWidth - 1);
		NativeGpu_EmitUILine(line);

		for (w = 0; w < 4; w++)
		{
			if (!s_uiMenu.winActive[w])
			{
				continue;
			}

			snprintf(line, sizeof(line), "[CTR UIElem] wn %d frame=%4d,%3d..%4d,%3d name=%s%4d..%4d shift=%+d", w, s_uiMenu.winRegion[w][0],
			         s_uiMenu.winRegion[w][1], s_uiMenu.winRegion[w][2], s_uiMenu.winRegion[w][3], s_uiMenu.winHasBand[w] ? "" : "-",
			         s_uiMenu.winHasBand[w] ? s_uiMenu.winBand[w][0] : 0, s_uiMenu.winHasBand[w] ? s_uiMenu.winBand[w][2] : 0, s_uiMenu.winShift[w]);
			NativeGpu_EmitUILine(line);
		}
	}

	for (g = 0; g < s_gpu.uiGroupCount; g++)
	{
		const struct NativeUiDecl *d = NativeUiDecl_At(s_gpu.uiGroupDecl[g]);
		const int anchor = s_gpu.uiGroupAnchor[g];

		snprintf(line, sizeof(line),
		         "[CTR UIElem] el %3d slot=%2d anchor=%-6s decl=%-22s before=%4d,%3d..%4d,%3d shift=%+3d after=%4d,%3d..%4d,%3d", g,
		         s_gpu.uiGroupSlot[g], NativeGpu_UiAnchorName(anchor), NativeGpu_UiGroupName(d),
		         s_gpu.uiGroups[g][0], s_gpu.uiGroups[g][1], s_gpu.uiGroups[g][2], s_gpu.uiGroups[g][3], s_gpu.uiGroupShift[g],
		         s_gpu.uiGroupMapped[g][0] + s_gpu.uiGroupShift[g], s_gpu.uiGroupMapped[g][1],
		         s_gpu.uiGroupMapped[g][2] + s_gpu.uiGroupShift[g], s_gpu.uiGroupMapped[g][3]);
		NativeGpu_EmitUILine(line);
	}

	// The union of what each row claimed, and how many primitives that was. The
	// union rather than one line per primitive: a row claiming three glyphs of a
	// number is ONE decision, and three lines that always move together would
	// read as three things to check.
	for (row = 0; row < rowCount; row++)
	{
		const struct NativeUiDecl *d = NativeUiDecl_At(row);
		int box[4] = {0, 0, 0, 0};
		int claimed = 0;
		int p;

		for (p = 0; p < s_gpu.uiPrimCount; p++)
		{
			const struct NativeGpuUiPrim *prim = &s_gpu.uiPrims[p];

			if (prim->decl != row)
			{
				continue;
			}

			if (claimed == 0)
			{
				box[0] = prim->x0; box[1] = prim->y0; box[2] = prim->x1; box[3] = prim->y1;
			}
			else
			{
				if (prim->x0 < box[0]) { box[0] = prim->x0; }
				if (prim->y0 < box[1]) { box[1] = prim->y0; }
				if (prim->x1 > box[2]) { box[2] = prim->x1; }
				if (prim->y1 > box[3]) { box[3] = prim->y1; }
			}

			claimed++;
		}

		if (claimed == 0)
		{
			continue;
		}

		snprintf(line, sizeof(line),
		         "[CTR UIElem] pr %3d prims=%-3d anchor=%-6s decl=%-22s before=%4d,%3d..%4d,%3d shift=%+3d after=%4d,%3d..%4d,%3d", row,
		         claimed, NativeGpu_UiAnchorName(d->anchor), d->name, box[0], box[1], box[2], box[3], 0,
		         CTR_UI_MapX(&s_gpu.uiView, box[0], d->anchor), box[1], CTR_UI_MapX(&s_gpu.uiView, box[2], d->anchor), box[3]);
		NativeGpu_EmitUILine(line);
	}
}

internal void NativeGpu_ReportUIElements(void)
{
	if ((g_cfg_uiElementsAt <= 0) || s_uiElementsPrinted || (Platform_GetVBlankCount() < g_cfg_uiElementsAt))
	{
		return;
	}

	s_uiElementsPrinted = 1;
	NativeGpu_PrintUIDecisions("once");
}

// How many tables the watch may print before it stops.
//
// It stops rather than throttling, and says that it stopped. A watch that
// quietly thinned out would turn "it settled" and "it is still flickering, you
// just stopped being told" into the same silence.
#define NATIVE_UI_WATCH_MAX 40

// The fingerprint of one frame's DECISIONS.
//
// Boxes are deliberately not in it. A timer counts down, a model turns, a number
// gains a digit - boxes move every frame and always will. What must not move is
// the answer to "which fixed point does this hold, which row claimed it, and how
// far did the floor push it". Those three per element, plus how many elements
// there are, and nothing else.
//
// Sorted before hashing, because the order of the group table is an artefact of
// draw order and of which merges happened, not a decision. Two frames that
// decided the same things in a different order have not flickered.
internal u64 NativeGpu_UiDecisionFingerprint(void)
{
	u32 keys[NATIVE_UI_GROUP_CAPACITY + 16];
	int keyCount = 0;
	u64 hash = 1469598103934665603ull; // FNV-1a offset basis
	int i;
	int j;
	int row;
	const int rowCount = NativeUiDecl_Count();

	for (i = 0; (i < s_gpu.uiGroupCount) && (keyCount < (int)(sizeof(keys) / sizeof(keys[0]))); i++)
	{
		int shift = s_gpu.uiGroupShift[i];

		if (shift < -2047) { shift = -2047; }
		if (shift > 2047) { shift = 2047; }

		keys[keyCount++] = ((u32)(s_gpu.uiGroupAnchor[i] & 3) << 28) | ((u32)(s_gpu.uiGroupDecl[i] + 2) << 16) | (u32)(shift + 2048);
	}

	// The shift of every driver window is a decision too.
	for (i = 0; (i < 4) && (keyCount < (int)(sizeof(keys) / sizeof(keys[0]))); i++)
	{
		if (s_uiMenu.active && s_uiMenu.winActive[i])
		{
			int shift = s_uiMenu.winShift[i];

			if (shift < -2047) { shift = -2047; }
			if (shift > 2047) { shift = 2047; }

			keys[keyCount++] = 0x40000000u | ((u32)i << 16) | (u32)(shift + 2048);
		}
	}

	// One key per row that claimed anything, not one per claimed primitive: the
	// glyph count of a number is content, not a decision.
	for (row = 0; (row < rowCount) && (keyCount < (int)(sizeof(keys) / sizeof(keys[0]))); row++)
	{
		int claimed = 0;

		for (i = 0; i < s_gpu.uiPrimCount; i++)
		{
			if (s_gpu.uiPrims[i].decl == row)
			{
				claimed = 1;
				break;
			}
		}

		if (claimed)
		{
			keys[keyCount++] = 0x80000000u | ((u32)(NativeUiDecl_At(row)->anchor & 3) << 28) | (u32)(row + 2);
		}
	}

	for (i = 1; i < keyCount; i++)
	{
		const u32 key = keys[i];

		for (j = i; (j > 0) && (keys[j - 1] > key); j--)
		{
			keys[j] = keys[j - 1];
		}

		keys[j] = key;
	}

	for (i = 0; i < keyCount; i++)
	{
		int byte;

		for (byte = 0; byte < 4; byte++)
		{
			hash ^= (u64)((keys[i] >> (byte * 8)) & 0xFF);
			hash *= 1099511628211ull;
		}
	}

	return hash;
}

internal void NativeGpu_WatchUIDecisions(void)
{
	local_persist u64 s_previous = 0;
	local_persist int s_havePrevious = 0;
	local_persist int s_printed = 0;

	u64 fingerprint;

	if (!g_cfg_uiWatch || (s_gpu.uiPrimCount == 0) || (Platform_GetVBlankCount() < g_cfg_uiWatchFrom))
	{
		return;
	}

	fingerprint = NativeGpu_UiDecisionFingerprint();

	if (s_havePrevious && (fingerprint == s_previous))
	{
		return;
	}

	s_previous = fingerprint;
	s_havePrevious = 1;

	if (s_printed >= NATIVE_UI_WATCH_MAX)
	{
		return;
	}

	s_printed++;

	if (s_printed == NATIVE_UI_WATCH_MAX)
	{
		NativeGpu_PrintUIDecisions("changed (last)");
		Platform_Log("[CTR UIElem] watch stops here - %d changes seen. Anything after this is not reported.\n", NATIVE_UI_WATCH_MAX);
		return;
	}

	NativeGpu_PrintUIDecisions(s_printed == 1 ? "first" : "changed");
}

// WHAT AN ELEMENT IS IN A MENU.
//
// The touch grouping with 4 points is made for the race HUD. In
// the menus it breaks up what the game draws as ONE thing (measured on
// the menu screens in widescreen):
//
//   a text line into words - a space leaves 12 to 19 points of air
//   ("PLEASE INSERT ANOTHER MEMORY CARD:" in five pieces, "SELECT CHARACTER"
//   in two), and every word got its own anchor;
//
//   a table row into symbol and name (high score: 32..75 and 92..189, 17
//   points of air);
//
//   a column into its rows (high score: label, entry, video, menu
//   with 5 to 8 points of air above each other).
//
// That is why in a menu groups of the same slot are then
// merged that form a row - same top and bottom edge, that is
// the same font on the same baseline, and at most one word gap
// in between - or a column - overlapping horizontally and at most one
// line gap above each other. Both are distances that occur within one thing in the measured
// menus and not between two things: the
// high score columns stand 38 points apart, title and columns 14.
//
// Groups over the whole width are already marked at this point (slot
// -2) and do not take part: an area over the picture is not part of a
// row, and what would be merged with it would get its stretching.
#define NATIVE_UI_MENU_WORD_GAP   24
#define NATIVE_UI_MENU_COLUMN_GAP 8


internal int NativeGpu_UiMenuGroupsJoin(const int *a, const int *b)
{
	const int sameLine = (a[1] == b[1]) && (a[3] == b[3]);

	if (sameLine && (a[0] <= (b[2] + NATIVE_UI_MENU_WORD_GAP)) && (b[0] <= (a[2] + NATIVE_UI_MENU_WORD_GAP)))
	{
		return 1;
	}

	return (a[0] <= b[2]) && (b[0] <= a[2]) && (a[1] <= (b[3] + NATIVE_UI_MENU_COLUMN_GAP)) && (b[1] <= (a[3] + NATIVE_UI_MENU_COLUMN_GAP));
}

internal void NativeGpu_UiMergeMenuGroups(void)
{
	int root[NATIVE_UI_GROUP_CAPACITY];
	int box[NATIVE_UI_GROUP_CAPACITY][4];
	int newIndex[NATIVE_UI_GROUP_CAPACITY];
	int count = s_gpu.uiGroupCount;
	int changed = 1;
	int g;
	int h;
	int p;
	int kept = 0;

	for (g = 0; g < count; g++)
	{
		root[g] = g;
		box[g][0] = s_gpu.uiGroups[g][0];
		box[g][1] = s_gpu.uiGroups[g][1];
		box[g][2] = s_gpu.uiGroups[g][2];
		box[g][3] = s_gpu.uiGroups[g][3];
	}

	// Until nothing more joins: a merge makes a box larger,
	// and the larger one can reach the next.
	while (changed)
	{
		changed = 0;

		for (g = 0; g < count; g++)
		{
			if ((root[g] != g) || (s_gpu.uiGroupSlot[g] == -2))
			{
				continue;
			}

			for (h = g + 1; h < count; h++)
			{
				if ((root[h] != h) || (s_gpu.uiGroupSlot[h] != s_gpu.uiGroupSlot[g]))
				{
					continue;
				}

				if (!NativeGpu_UiMenuGroupsJoin(box[g], box[h]))
				{
					continue;
				}

				root[h] = g;
				NativeGpu_UiGrowGroup(box[g], box[h]);
				changed = 1;
			}
		}
	}

	// Every group points to its root; a root itself can only point to
	// itself, because only roots take in others. A chain comes into being anyway
	// when a later root is taken in later - so walk it up.
	for (g = 0; g < count; g++)
	{
		int r = g;

		while (root[r] != r)
		{
			r = root[r];
		}

		root[g] = r;
	}

	for (g = 0; g < count; g++)
	{
		newIndex[g] = -1;
	}

	for (g = 0; g < count; g++)
	{
		if (root[g] != g)
		{
			continue;
		}

		newIndex[g] = kept;
		s_gpu.uiGroups[kept][0] = box[g][0];
		s_gpu.uiGroups[kept][1] = box[g][1];
		s_gpu.uiGroups[kept][2] = box[g][2];
		s_gpu.uiGroups[kept][3] = box[g][3];
		s_gpu.uiGroupSlot[kept] = s_gpu.uiGroupSlot[g];
		kept++;
	}

	for (p = 0; p < s_gpu.uiPrimCount; p++)
	{
		if (s_gpu.uiPrims[p].group >= 0)
		{
			s_gpu.uiPrims[p].group = newIndex[root[s_gpu.uiPrims[p].group]];
		}
	}

	g_uiMenuMerges += (unsigned long long)(count - kept);
	s_gpu.uiGroupCount = kept;
}

// The walk is over: group, decide, move.
void NativeGpu_ApplyUIMapping(void)
{
	int p;
	int g;

	if (s_gpu.uiPrimCount == 0)
	{
		return;
	}

	s_gpu.uiGroupCount = 0;
	g_uiDeclHits = 0;
	g_uiDeclMisses = 0;
	g_uiDeclCollisions = 0;
	g_uiDeclPrimHits = 0;

	// ---- the player menus: what applies to the whole pass ----
	//
	// Outside MAIN_MENU s_uiMenu.active stays 0, and every menu branch
	// below is skipped: race, pause, loading and result screens run
	// step by step as before.
	s_uiMenu.active = CTR_UI_MenuMode();
	s_uiMenu.policyName = NULL;
	s_uiMenu.policy = -1;

	{
		int w;

		for (w = 0; w < 4; w++)
		{
			s_uiMenu.winActive[w] = 0;
			s_uiMenu.winHasBand[w] = 0;
			s_uiMenu.winShift[w] = 0;
		}

		if (s_uiMenu.active)
		{
			g_uiMenuPasses++;
			s_uiMenu.policy = NativeUiDecl_MenuPolicy(&s_uiMenu.policyName);

			if (s_gpu.uiView.zoneLeft > 0)
			{
				g_uiMenuZonePasses++;
				g_uiMenuZoneLeft = s_gpu.uiView.zoneLeft;
				g_uiMenuZoneWidth = s_gpu.uiView.zoneWidth;
			}

			for (w = 0; w < 4; w++)
			{
				s_uiMenu.winActive[w] = NativeMenuWindow_FrameRegion(w, s_uiMenu.winRegion[w]) && NativeMenuWindow_CoreRegion(w, s_uiMenu.winCore[w]);

				if (s_uiMenu.winActive[w])
				{
					s_uiMenu.winHasBand[w] = NativeMenuWindow_NameBand(w, s_uiMenu.winBand[w]);
					s_uiMenu.winShift[w] = NativeMenuWindow_Shift(&s_gpu.uiView, w);
				}
			}
		}
	}

	// ---- arming: which rows are about THIS frame at all ----
	//
	// A region is a rectangle, and a rectangle on a 512x216 canvas will sooner or
	// later have something else inside it. Measured, not feared: on a boot screen
	// the row for the fruit counter claimed three strangers whose boxes merely
	// fell inside its rectangle, and the row for the debug panel claimed a
	// full-screen background because COVERS matches anything that spans it.
	//
	// So a row may state what must be true of the frame before it claims
	// anything, and it is worked out here - once, over the whole frame, before a
	// single primitive is matched. Doing it per primitive would make the answer
	// depend on which one asked first.
	{
		u8 originsFound[NATIVE_UI_DECL_CAPACITY];
		const int rowCount = NativeUiDecl_Count();
		int row;

		for (row = 0; row < NATIVE_UI_DECL_CAPACITY; row++)
		{
			originsFound[row] = 0;
		}

		for (row = 0; (row < rowCount) && (row < NATIVE_UI_DECL_CAPACITY); row++)
		{
			int region[4];

			if (!NativeUiDecl_Region(NativeUiDecl_At(row), region))
			{
				continue;
			}

			for (p = 0; p < s_gpu.uiPrimCount; p++)
			{
				const struct NativeGpuUiPrim *prim = &s_gpu.uiPrims[p];

				if ((prim->x0 == region[0]) && (prim->y0 >= region[1]) && (prim->y1 <= region[3]))
				{
					originsFound[row] = 1;
					break;
				}
			}
		}

		NativeUiDecl_BeginFrame(originsFound);
	}

	// ---- pass 0: a declaration claims a PRIMITIVE, before anything is grouped
	//
	// THE ANCHOR OF A DECLARED THING MAY NOT DEPEND ON THIS FRAME'S GEOMETRY, and
	// the grouping IS this frame's geometry: two primitives become one element
	// when their DRAWN boxes come within NATIVE_UI_TOUCH_GAP of each other.
	//
	// The fruit model of the race HUD is a 3D model. It turns, so its drawn width
	// changes from frame to frame, so the distance between it and the count next
	// to it changes - and the pair is one element at one rotation and two at the
	// next. One element gets one anchor; two elements get two, and each is
	// decided from a different box. Nothing about the design changed between
	// those two frames. The picture moved anyway.
	//
	// Three separate things flip on that same boundary, and all three are the
	// same mistake:
	//   - which box the thirds rule reads (the pair's, or the count's alone),
	//   - whether a row keyed on the count still matches (the merged box is
	//     bigger than the region and does not),
	//   - whether the floor sees a gap to defend at all (there is no gap inside
	//     one element).
	//
	// So a declaration is looked up against the primitive's OWN box first, and a
	// primitive a row claims is neither grouped nor moved by the floor. Its
	// position becomes a function of its own coordinates and its declared fixed
	// point, and of nothing else in the frame. That is what "from the authoring,
	// not from the momentary geometry" means in code.
	//
	// Rows that key on a MERGED box are unaffected: an opaque overlay is only
	// ever itself plus whatever it was laid over, no single primitive of it
	// covers the declared rectangle, and it falls through to the group lookup
	// below exactly as before.
	for (p = 0; p < s_gpu.uiPrimCount; p++)
	{
		struct NativeGpuUiPrim *prim = &s_gpu.uiPrims[p];
		const int box[4] = {prim->x0, prim->y0, prim->x1, prim->y1};

		prim->decl = -1;

		if (g_cfg_uiAnchorPerGroup)
		{
			continue;
		}

		prim->decl = NativeUiDecl_Find(box, prim->slot);

		if (prim->decl >= 0)
		{
			g_uiDeclPrimHits++;
		}
	}

	// ---- the driver windows claim frame and name, like a declaration ----
	//
	// A primitive that lies entirely in the frame of a window or in its name band
	// belongs to that window and is shifted with it - by the same
	// amount that the 3D picture gets in PushBuffer_SetDrawEnv_Normal. A
	// table row that has already claimed it goes first. First the core
	// of every window, then shadow and name: where two windows touch,
	// the shadow of the one lies in the frame of the other.
	//
	// THE SHADOW ONLY COUNTS IN SLOT 3. There the character select draws frame
	// and shadow (RECTMENU_DrawInnerRect type 9, MM_Characters.c:1184); everything
	// else lies in slot 0. Measured in 2P: the icon grid begins at
	// y 125, the shadow strip of the window above reaches to 128 - with the
	// shadow for all slots, the top edges of the cursor frames around
	// the driver icons stayed stuck to the window instead of going with the grid. In the name band only
	// what is at least half as high as the font counts, that is letters and no
	// lines.
	if (s_uiMenu.active)
	{
		for (p = 0; p < s_gpu.uiPrimCount; p++)
		{
			struct NativeGpuUiPrim *prim = &s_gpu.uiPrims[p];
			int pass;
			int w;

			prim->win = -1;

			if (prim->decl >= 0)
			{
				continue;
			}

			for (pass = 0; (pass < 2) && (prim->win < 0); pass++)
			{
				for (w = 0; w < 4; w++)
				{
					const int *r = (pass == 0) ? s_uiMenu.winCore[w] : s_uiMenu.winRegion[w];
					const int *b = s_uiMenu.winBand[w];

					if (!s_uiMenu.winActive[w])
					{
						continue;
					}

					const int inRegion = (prim->x0 >= r[0]) && (prim->y0 >= r[1]) && (prim->x1 <= r[2]) && (prim->y1 <= r[3]);
					const int inBand = (pass == 1) && s_uiMenu.winHasBand[w] && (prim->x0 >= b[0]) && (prim->y0 >= b[1]) && (prim->x1 <= b[2]) &&
					                   (prim->y1 <= b[3]) && ((prim->y1 - prim->y0) * 2 >= (b[3] - b[1]));

					if ((inRegion && ((pass == 0) || (prim->slot == 3))) || inBand)
					{
						prim->win = w;
						g_uiMenuWindowPrims++;
						break;
					}
				}
			}
		}
	}

	for (p = 0; p < s_gpu.uiPrimCount; p++)
	{
		struct NativeGpuUiPrim *prim = &s_gpu.uiPrims[p];
		const int box[4] = {prim->x0, prim->y0, prim->x1, prim->y1};
		int found = -1;

		// Claimed on its own: it is its own element and joins nothing. Left in
		// the group table it would hand its neighbours a box they did not have,
		// which is the flicker one step further along. A character-select
		// window's claim counts the same.
		if ((prim->decl >= 0) || (prim->win >= 0))
		{
			prim->group = -1;
			continue;
		}

		for (g = 0; g < s_gpu.uiGroupCount; g++)
		{
			int h;
			int q;

			if (s_gpu.uiGroupSlot[g] != prim->slot)
			{
				continue;
			}

			if (!NativeGpu_UiBoxesTouch(s_gpu.uiGroups[g], box))
			{
				continue;
			}

			if (found < 0)
			{
				found = g;
				NativeGpu_UiGrowGroup(s_gpu.uiGroups[g], box);
				continue;
			}

			// This primitive bridges two groups that were separate. Fold the
			// later one into the earlier and close the hole it leaves, so the
			// group list stays dense and every primitive index stays valid.
			NativeGpu_UiGrowGroup(s_gpu.uiGroups[found], s_gpu.uiGroups[g]);

			for (q = 0; q < p; q++)
			{
				if (s_gpu.uiPrims[q].group == g) { s_gpu.uiPrims[q].group = found; }
				else if (s_gpu.uiPrims[q].group > g) { s_gpu.uiPrims[q].group--; }
			}

			for (h = g; h < (s_gpu.uiGroupCount - 1); h++)
			{
				s_gpu.uiGroups[h][0] = s_gpu.uiGroups[h + 1][0];
				s_gpu.uiGroups[h][1] = s_gpu.uiGroups[h + 1][1];
				s_gpu.uiGroups[h][2] = s_gpu.uiGroups[h + 1][2];
				s_gpu.uiGroups[h][3] = s_gpu.uiGroups[h + 1][3];
				s_gpu.uiGroupSlot[h] = s_gpu.uiGroupSlot[h + 1];
			}

			s_gpu.uiGroupCount--;
			if (found > g) { found--; }
			g--;
		}

		if (found < 0)
		{
			if (s_gpu.uiGroupCount >= NATIVE_UI_GROUP_CAPACITY)
			{
				// Out of groups: this primitive becomes its own element, which
				// is the per-triangle behaviour, and it is counted.
				g_uiGroupsDropped++;
				prim->group = -1;
				continue;
			}

			found = s_gpu.uiGroupCount;
			s_gpu.uiGroups[found][0] = box[0];
			s_gpu.uiGroups[found][1] = box[1];
			s_gpu.uiGroups[found][2] = box[2];
			s_gpu.uiGroups[found][3] = box[3];
			s_gpu.uiGroupSlot[found] = prim->slot;
			s_gpu.uiGroupCount++;
		}

		prim->group = found;
	}

	// A group that spans the whole canvas IS the canvas.
	//
	// The single-primitive case is caught while the primitive is noted, but a
	// dim drawn as a grid of tiles has no single primitive that spans anything -
	// its GROUP does, and one element gets one anchor. Marked with a slot value
	// no ordering table can produce, so the marking cannot be confused with a
	// real slot.
	for (g = 0; g < s_gpu.uiGroupCount; g++)
	{
		if ((s_gpu.uiGroups[g][0] <= NATIVE_UI_TOUCH_GAP) &&
		    (s_gpu.uiGroups[g][2] >= (s_gpu.uiView.virtualWidth - NATIVE_UI_TOUCH_GAP)))
		{
			s_gpu.uiGroupSlot[g] = -2;

			if (s_uiMenu.active)
			{
				g_uiMenuFullCanvasGroups++;
			}
		}
	}

	// ---- in the menu: rows and columns are one element ----
	//
	// Not on a screen with a fixed anchor: there every element gets
	// the same anchor, and the single piece is meant to keep its own measure for the flight over the edge
	// - merged, a part that is still
	// flying in would drag the ones already at rest along with it.
	if (s_uiMenu.active && (s_uiMenu.policy < 0))
	{
		NativeGpu_UiMergeMenuGroups();
	}

	// ---- anchor, declaration and floor: decided ONCE, per element ----
	for (g = 0; g < s_gpu.uiGroupCount; g++)
	{
		int anchor = (int)CTR_UI_ANCHOR_CENTRE;
		int decl = -1;
		int transit = 0;

		if (s_gpu.uiGroupSlot[g] == -2)
		{
			anchor = (int)CTR_UI_ANCHOR_FULL_CANVAS;
		}
		else
		{
			decl = NativeUiDecl_Find(s_gpu.uiGroups[g], s_gpu.uiGroupSlot[g]);

			if (decl >= 0)
			{
				anchor = NativeUiDecl_At(decl)->anchor;
				g_uiDeclHits++;
			}
			else
			{
				g_uiDeclMisses++;

				if (s_uiMenu.active && (s_uiMenu.policy >= 0))
				{
					// The screen dictates the anchor (native_uidecl.c,
					// g_nativeUiMenuPolicies), and the flight over the edge
					// is added.
					anchor = s_uiMenu.policy;
					transit = CTR_UI_TransitShift(&s_gpu.uiView, CTR_UI_AnchorShift(&s_gpu.uiView, anchor), s_gpu.uiGroups[g][0],
					                              s_gpu.uiGroups[g][2]) -
					          CTR_UI_AnchorShift(&s_gpu.uiView, anchor);
					g_uiMenuPolicyGroups++;
				}
				else if (s_uiMenu.active)
				{
					// The edge rule of the menus (native_view.c) - on the
					// rest position when the high score is paging
					// (native_menuscreen.c), and then the page glides
					// by its offset times W/512; otherwise on the position in
					// this frame.
					//
					// In both cases plus the transition over the
					// 4:3 edge that the zone makes necessary
					// (CTR_UI_MenuEdgeTransit): what is outside in 4:3
					// stays outside on the canvas. While paging computed on
					// the rest position, so that the band ends of a
					// page glide with it. Without a zone it is 0.
					int rest[4];
					int pageOffset = 0;
					int edge;

					if (NativeMenuHighScore_RestBox(s_gpu.uiGroups[g], rest, &pageOffset))
					{
						anchor = CTR_UI_AnchorForBoxEdge(rest[0], rest[2], s_gpu.uiView.virtualWidth);
						edge = CTR_UI_MenuEdgeTransit(&s_gpu.uiView, anchor, rest[0], rest[2]);
						transit = (int)(((s64)pageOffset * (s_gpu.uiView.canvasWidth - s_gpu.uiView.virtualWidth)) / s_gpu.uiView.virtualWidth) + edge;
					}
					else
					{
						anchor = CTR_UI_AnchorForBoxEdge(s_gpu.uiGroups[g][0], s_gpu.uiGroups[g][2], s_gpu.uiView.virtualWidth);
						edge = CTR_UI_MenuEdgeTransit(&s_gpu.uiView, anchor, s_gpu.uiGroups[g][0], s_gpu.uiGroups[g][2]);
						transit = edge;
					}

					if (edge != 0)
					{
						g_uiMenuEdgeTransits++;
					}
				}
				else if (!g_cfg_uiAnchorLegacy)
				{
					anchor = CTR_UI_AnchorForBox(s_gpu.uiGroups[g][0], s_gpu.uiGroups[g][2], s_gpu.uiView.virtualWidth);
				}
			}
		}

		s_gpu.uiGroupAnchor[g] = anchor;
		s_gpu.uiGroupDecl[g] = decl;
		s_gpu.uiGroupTransit[g] = transit;

		// The element mapped box. Mapping the two corners is the same thing as
		// mapping every primitive and taking the union: the map is monotone in
		// x, so the smallest mapped edge belongs to the smallest authored edge.
		s_gpu.uiGroupMapped[g][0] = CTR_UI_MapX(&s_gpu.uiView, s_gpu.uiGroups[g][0], anchor) + transit;
		s_gpu.uiGroupMapped[g][1] = s_gpu.uiGroups[g][1];
		s_gpu.uiGroupMapped[g][2] = CTR_UI_MapX(&s_gpu.uiView, s_gpu.uiGroups[g][2], anchor) + transit;
		s_gpu.uiGroupMapped[g][3] = s_gpu.uiGroups[g][3];
	}

	NativeUiDecl_Floor((const int (*)[4])s_gpu.uiGroups, (const int (*)[4])s_gpu.uiGroupMapped, s_gpu.uiGroupAnchor,
	                   s_gpu.uiGroupCount, s_gpu.uiView.virtualWidth, s_gpu.uiView.canvasWidth, s_gpu.uiGroupShift);

	g_uiFloorClamps = 0;
	g_uiFloorMaxShift = 0;

	for (g = 0; g < s_gpu.uiGroupCount; g++)
	{
		const int d = s_gpu.uiGroupShift[g];

		if (d != 0)
		{
			g_uiFloorClamps++;
			if (((d < 0) ? -d : d) > g_uiFloorMaxShift) { g_uiFloorMaxShift = (d < 0) ? -d : d; }
		}
	}

	g_uiElementCount = s_gpu.uiGroupCount;

	// ---- and only now do vertices move ----
	for (p = 0; p < s_gpu.uiPrimCount; p++)
	{
		struct NativeGpuUiPrim *prim = &s_gpu.uiPrims[p];
		int anchor = (int)CTR_UI_ANCHOR_CENTRE;
		int floorShift = 0;
		int i;

		// Declared on its own: its fixed point, its own coordinates, no shift.
		if (prim->decl >= 0)
		{
			anchor = NativeUiDecl_At(prim->decl)->anchor;
		}
		else if (prim->win >= 0)
		{
			// Frame or name of a driver window: exactly the shift of the
			// window, over the left anchor as a surcharge. With the composition
			// zone LEFT is not the identity - in a menu at 918 columns it is
			// +118 -, so its own shift is subtracted. Otherwise the frame would
			// stand 118 columns beside its 3D picture (measured, character
			// select 43:18).
			anchor = (int)CTR_UI_ANCHOR_LEFT;
			floorShift = s_uiMenu.winShift[prim->win] - CTR_UI_AnchorShift(&s_gpu.uiView, (int)CTR_UI_ANCHOR_LEFT);
		}
		else if (prim->group >= 0)
		{
			anchor = s_gpu.uiGroupAnchor[prim->group];
			floorShift = s_gpu.uiGroupShift[prim->group] + s_gpu.uiGroupTransit[prim->group];
		}
		else if (s_uiMenu.active)
		{
			// No element in a menu: the screen's anchor, or the menu's own rule
			// on the primitive's box - with the edge transit the zone needs,
			// exactly as for a group.
			if (s_uiMenu.policy >= 0)
			{
				anchor = s_uiMenu.policy;
			}
			else
			{
				anchor = CTR_UI_AnchorForBoxEdge(prim->x0, prim->x1, s_gpu.uiView.virtualWidth);
				floorShift = CTR_UI_MenuEdgeTransit(&s_gpu.uiView, anchor, prim->x0, prim->x1);

				if (floorShift != 0)
				{
					g_uiMenuEdgeTransits++;
				}
			}
		}
		else if (!g_cfg_uiAnchorLegacy)
		{
			// No element: the group table ran out. The primitive is its own
			// element and gets its own anchor, and no floor - a distance needs
			// two things and this one is not in the table that holds them.
			anchor = CTR_UI_AnchorForBox(prim->x0, prim->x1, s_gpu.uiView.virtualWidth);
		}

		for (i = prim->firstVertex; i < prim->lastVertex; i++)
		{
			const int beforeX = (int)s_gpu.vertexBuffer[i].x;
			const int afterX = CTR_UI_MapX(&s_gpu.uiView, beforeX, anchor) + floorShift;

			g_uiVerticesMapped++;

			if (afterX != beforeX)
			{
				const int shift = (afterX > beforeX) ? (afterX - beforeX) : (beforeX - afterX);

				g_uiVerticesMoved++;
				if (shift > g_uiMaxShift) { g_uiMaxShift = shift; }
			}

			// Y is untouched. The vertical field of view does not change under
			// Hor+ and the presentation does not stretch vertically, so there is
			// nothing to undo.
			s_gpu.vertexBuffer[i].x = (float)afterX;
		}
	}

	NativeGpu_ReportUIElements();
	NativeGpu_WatchUIDecisions();

	s_gpu.uiPrimCount = 0;
	s_gpu.uiGroupCount = 0;
}

void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive)
{
	if (!p)
	{
		return;
	}

	NativePerf_BeginScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);

#ifdef CTR_NATIVE
	if (!singlePrimitive && !NativeGpuLinks_IsRegisteredHostPointer(p) && !isendprim(p))
	{
		char packetRegion[64];
		NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)p);
		NATIVE_GPU_ERROR("unregistered linked DrawOTag packet: packet=%p region=%s addr=%06x len=%d code=%02x words=%08x %08x %08x %08x\n", (void *)p,
		                 packetRegion, getaddr(p), getlen(p), getcode(p), NativeGpu_ReadPacketWordForLog((uintptr_t)p, 0),
		                 NativeGpu_ReadPacketWordForLog((uintptr_t)p, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)p, 2),
		                 NativeGpu_ReadPacketWordForLog((uintptr_t)p, 3));
		NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);
		return;
	}
#endif

	// setup single primitive flag (needed for AddSplits)
	s_gpu.drawPrimMode = singlePrimitive;
	s_gpu.uiViewActive = false;
	s_gpu.uiOtSlot = -1;

	// Built per walk from the push buffer own rectangle, which PushBuffer_Init
	// sets to 0x200 x 0xD8 - 512x216. Not written down here: the canvas is the
	// buffer the HUD is authored into, and a second copy of its size is a second
	// thing to keep in step.
	s_gpu.uiViewEnabled =
	    !singlePrimitive && g_cfg_uiSafeArea && (sdata != NULL) && (sdata->gGT != NULL) &&
	    CTR_UI_BuildSafeArea(&s_gpu.uiView, sdata->gGT->pushBuffer_UI.rect.w, sdata->gGT->pushBuffer_UI.rect.h);

	if (singlePrimitive)
	{
		P_TAG *polyTag = (P_TAG *)p;
		ParsePrimitive(polyTag);

		GPUDrawSplit *lastSplit = &s_gpu.splits[s_gpu.splitIndex];
		lastSplit->numVerts = s_gpu.vertexIndex - lastSplit->startVertex;
	}
	else
	{
		// walk OT_TAG linked list
		u8 *basePacket = (u8 *)p;
		while (true)
		{
			// uiViewActive is exactly "the walk is inside the UI ordering
			// table". It is the split between world and UI, not a second guess
			// at it.
			//
			// THE UI TABLE IS NOT THE END OF THE CHAIN, so the flag must not
			// stay set for the rest of the walk. ClearOTagR links entry
			// i after i-1, and the table lies on indices 1..5 - after it
			// comes index 0, the swapchain root (MainFrame.c: ptrOT_UI = root
			// + 4). Three producers hang on the root on purpose, to be drawn
			// LAST: the loading flag and its letters
			// (RaceFlag.c, RaceFlag_GetOT gives the root as "otDrawLast"), the
			// blur and the fade (Display.c). None of them is
			// authored in 512 columns; they compute in canvas or VRAM columns.
			//
			// With a sticky flag they would be recorded as UI anyway.
			// Measured at 43:18: 455 quads of the flag in one
			// group, edge authored 973, stretched as FULL_CANVAS to 1744 -
			// 1.79 times as wide as the picture. At 4:3 the stretch is the
			// identity, which is why it never shows there.
			//
			// So the UI ends exactly where the root is visited. That is
			// an address equality, not a threshold: index 0 lies one word before
			// the first slot of the table, and the same source supplies both.
			{
				const int uiSlot = NativeGpu_UIOTSlotIndex(basePacket);

				if (uiSlot >= 0)
				{
					s_gpu.uiViewActive = true;
					s_gpu.uiOtSlot = uiSlot;
				}
				else if (s_gpu.uiViewActive && NativeGpu_IsUIOTRoot(basePacket))
				{
					s_gpu.uiViewActive = false;
				}
			}

			const int tagLength = getlen(basePacket);
			if (tagLength > 0)
			{
				if (tagLength > 32)
				{
					char packetRegion[64];
					NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
					NATIVE_GPU_ERROR("got invalid tag length %d, code %d packet=%p region=%s words=%08x %08x %08x %08x\n", tagLength,
					                 ((P_TAG *)basePacket)->code, (void *)basePacket, packetRegion, NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
					break;
				}

				u8 *currentPacket = basePacket;
				u8 *endPacket = basePacket + (tagLength + P_LEN) * sizeof(u32);
				int primLength = 0;
				if (currentPacket < endPacket)
				{
					const int uiVertexStart = s_gpu.vertexIndex;

					primLength = ParsePrimitive((P_TAG *)currentPacket);
					NativeGpu_NoteUIVertexRange(uiVertexStart, currentPacket);
					currentPacket += (primLength + P_LEN) * sizeof(u32);
				}

				while (currentPacket < endPacket)
				{
					const int uiVertexStart = s_gpu.vertexIndex;

					primLength = ParseTaglessPrimitive((u32 *)currentPacket);
					NativeGpu_NoteUIVertexRange(uiVertexStart, currentPacket);
					currentPacket += primLength * sizeof(u32);
				}

				if (currentPacket != endPacket)
				{
					char packetRegion[64];
					NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
					NATIVE_GPU_ERROR("did not output valid primitive or ptag length is not valid (diff=%d packet=%p region=%s words=%08x %08x %08x %08x)\n",
					                 endPacket - currentPacket, (void *)basePacket, packetRegion, NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2),
					                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
				}
			}

			GPUDrawSplit *lastSplit = &s_gpu.splits[s_gpu.splitIndex];
			lastSplit->numVerts = s_gpu.vertexIndex - lastSplit->startVertex;

			if (isendprim(basePacket))
			{
				break;
			}

			u8 *nextPacket = nextPrim(basePacket);
			if (!NativeGpu_IsValidOTLink((uintptr_t)nextPacket))
			{
				char packetRegion[64];
				char nextRegion[64];
				NativeGpu_FormatPointerRegion(packetRegion, sizeof(packetRegion), (uintptr_t)basePacket);
				NativeGpu_FormatPointerRegion(nextRegion, sizeof(nextRegion), (uintptr_t)nextPacket);
				NATIVE_GPU_ERROR("invalid OT link: packet=%p region=%s addr=%06x next=%p nextRegion=%s len=%d code=%02x words=%08x %08x %08x %08x\n",
				                 (void *)basePacket, packetRegion, getaddr(basePacket), (void *)nextPacket, nextRegion, getlen(basePacket), getcode(basePacket),
				                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 0), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 1),
				                 NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 2), NativeGpu_ReadPacketWordForLog((uintptr_t)basePacket, 3));
				break;
			}

			basePacket = nextPacket;
		}
	}

	NativePerf_EndScope(NATIVE_PERF_BUCKET_DRAW_OTAG_PARSE);
}

internal inline int IsNull(POLY_FT3 *poly)
{
	return poly->x0 == -1 && poly->y0 == -1 && poly->x1 == -1 && poly->y1 == -1 && poly->x2 == -1 && poly->y2 == -1;
}

internal int ProcessFlatLines(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		LINE_F2 *poly = (LINE_F2 *)polyTag;

		AddSplit(semiTrans, false, false);

		VERTTYPE *p0 = &poly->x0;
		VERTTYPE *p1 = &poly->x1;
		u8 *c0 = &poly->r0;
		u8 *c1 = c0;

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		LineSwapSourceVerts(&p0, &p1, &c0, &c1);
		MakeLineArray(firstVertex, p0, p1);
		MakeTexcoordLineZero(firstVertex, 0);
		MakeColourLine(firstVertex, shadeTexOn, c0, c1);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x8: // TODO (unused)
	{
		LINE_F3 *poly = (LINE_F3 *)polyTag;

		AddSplit(semiTrans, false, false);

		{
			VERTTYPE *p0 = &poly->x0;
			VERTTYPE *p1 = &poly->x1;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x1;
			VERTTYPE *p1 = &poly->x2;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		return 5;
	}
	case 0xc:
	{
		LINE_F4 *poly = (LINE_F4 *)polyTag;

		AddSplit(semiTrans, false, false);

		{
			VERTTYPE *p0 = &poly->x0;
			VERTTYPE *p1 = &poly->x1;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x1;
			VERTTYPE *p1 = &poly->x2;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		{
			VERTTYPE *p0 = &poly->x2;
			VERTTYPE *p1 = &poly->x3;
			u8 *c0 = &poly->r0;
			u8 *c1 = c0;

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			LineSwapSourceVerts(&p0, &p1, &c0, &c1);
			MakeLineArray(firstVertex, p0, p1);
			MakeTexcoordLineZero(firstVertex, 0);
			MakeColourLine(firstVertex, shadeTexOn, c0, c1);

			TriangulateQuad();

			s_gpu.vertexIndex += 6;
		}

		return 6;
	}
	}
	return 0;
}

internal int ProcessGouraudLines(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		LINE_G2 *poly = (LINE_G2 *)polyTag;

		AddSplit(semiTrans, false, false);

		VERTTYPE *p0 = &poly->x0;
		VERTTYPE *p1 = &poly->x1;
		u8 *c0 = &poly->r0;
		u8 *c1 = &poly->r1;

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		LineSwapSourceVerts(&p0, &p1, &c0, &c1);
		MakeLineArray(firstVertex, p0, p1);
		MakeTexcoordLineZero(firstVertex, 0);
		MakeColourLine(firstVertex, shadeTexOn, c0, c1);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 4;
	}
	case 0x8:
	{
		// TODO: LINE_G3
		return 7;
	}
	case 0xC:
	{
		// TODO: LINE_G4
		return 9;
	}
	}
	return 0;
}

// THE SIZE LIMIT OF THE PS1 GPU - THE CENSUS.
//
// The GPU of the PS1 draws no triangle whose vertices lie more than
// 1023 pixels apart horizontally or more than 511 vertically. To it a quad
// is two triangles, (0,1,2) and (1,2,3), each decided on its own.
// MakeVertexTriangle and MakeVertexQuad take every coordinate as it comes;
// the limit is applied afterwards, through the mask NotePolySize leaves for
// ParsePrimitive (see "THE SIZE LIMIT OF THE PS1, AS A RULE" further up).
//
// Why an oversized triangle is a picture fault and not merely a deviation:
// the vertex that the GTE delivers from a clamped division stands at +-0x3ff
// (the screen-coordinate clamp in native_gte_core.c). A polygon with such a
// vertex is invisible on the PS1, because it is too wide - drawn anyway it
// is pulled across the screen. A subtractive shadow quad (VehGroundShadow.c,
// FT4, blend mode 2) would become a black bar, a textured area a "wrong
// texture" that is gone again with the next movement.
//
// What the census found over one drive: 4001 polygons over the limit, 4001
// of them with a vertex at +-0x3ff, 3998 of them tyre quads (FT4, blend mode
// 0, DrawTires.c) - not the shadows, those were 0. The cause is NO LOD: this
// port draws the wheels of distant karts that retail drops behind the header
// threshold; with stock LOD it is 0 of 2,290,860. The real fix for the tyres
// belongs at the LOD choice. Dropping every polygon with a clamped vertex was
// tried and also hit 389 track faces with a vertex in the picture, which is
// why the rule looks at the size and not at the clamp.
//
// Broken down by primitive type and blend mode, because the question is
// WHICH polygons these are - and whether they carry the signature of the clamped division,
// a vertex at 0x3ff or -0x400. The first of its kind is
// reported with vertices and VBlank, at most forty lines over the whole
// run; after that the table every 300 frames, in the same windows as the
// page store, and the total at exit. F3 and G3 carry the blend mode
// of the draw environment, as in AddSplit.
#define NATIVE_GPU_SIZE_TYPES  8
#define NATIVE_GPU_SIZE_BLENDS 5
#define NATIVE_GPU_SIZE_WINDOW 300
#define NATIVE_GPU_SIZE_MAX_W  1023
#define NATIVE_GPU_SIZE_MAX_H  511
#define NATIVE_GPU_SIZE_SAT_HI 0x3ff
#define NATIVE_GPU_SIZE_SAT_LO (-0x400)

enum
{
	NATIVE_GPU_SIZE_F3 = 0,
	NATIVE_GPU_SIZE_FT3,
	NATIVE_GPU_SIZE_G3,
	NATIVE_GPU_SIZE_GT3,
	NATIVE_GPU_SIZE_F4,
	NATIVE_GPU_SIZE_FT4,
	NATIVE_GPU_SIZE_G4,
	NATIVE_GPU_SIZE_GT4,
};

struct NativeGpuSizeCensus
{
	u64 seen[NATIVE_GPU_SIZE_TYPES][NATIVE_GPU_SIZE_BLENDS];    // all polygons
	u64 over[NATIVE_GPU_SIZE_TYPES][NATIVE_GPU_SIZE_BLENDS];    // too large for the PS1
	u64 overSat[NATIVE_GPU_SIZE_TYPES][NATIVE_GPU_SIZE_BLENDS]; // of them with a vertex at +-0x3ff
	u64 sat[NATIVE_GPU_SIZE_TYPES][NATIVE_GPU_SIZE_BLENDS];     // vertex at +-0x3ff, large or not
	u64 overTriangles;
	int frames;
	int framesWithOver;
	int vblankFirst;
};

global_variable struct NativeGpuSizeCensus s_sizeWindow;
global_variable struct NativeGpuSizeCensus s_sizeTotal;
global_variable int s_sizeThisFrame = 0;
global_variable u8 s_sizeFirstSaid[NATIVE_GPU_SIZE_TYPES][NATIVE_GPU_SIZE_BLENDS];

global_variable const char *const s_sizeTypeName[NATIVE_GPU_SIZE_TYPES] = {"F3", "FT3", "G3", "GT3", "F4", "FT4", "G4", "GT4"};
global_variable const char *const s_sizeBlendName[NATIVE_GPU_SIZE_BLENDS] = {"opaque", "semi 0 average", "semi 1 add", "semi 2 subtract",
                                                                            "semi 3 add-quarter"};

// THE LIMIT APPLIES IN REFERENCE WIDTH, NOT IN CANVAS PIXELS.
//
// At 43:18 the world canvas is 918 columns wide (game/native_view.c), the
// vertices arrive in these columns. 1023 canvas pixels are only 1.11
// picture widths there - at 4:3 they are two. Measured in canvas pixels:
// disc 0 drops at 43:18 85 polygons, at 4:3 zero; Inferno 283 instead of
// 169, Vista 594 instead of 62 triangles - a widescreen-only regression that
// no 4:3 check can see. That is why the horizontal extent is converted
// to the reference width 512 before the comparison (x * 512 / canvas) -
// at 4:3 the identity, so not one bit different there. The UI table is authored in 512
// and stays unconverted. The height is 216
// rows in both formats and needs no conversion.
//
// CTR_Canvas_ToReferenceWidth is in game/native_view.c and has no
// header. It is declared here, so that the call has a type.
int CTR_Canvas_ToReferenceWidth(int width);

// --size-rule-canvas applies the rule in canvas pixels instead, so that both
// rules can be compared in the same build on the same pictures.
// Default 0 = reference width.
int g_cfg_sizeRuleCanvas = 0;

internal int NativeGpu_TriangleTooBig(const VERTTYPE *a, const VERTTYPE *b, const VERTTYPE *c)
{
	int minX = a[0];
	int maxX = a[0];
	int minY = a[1];
	int maxY = a[1];

	if (b[0] < minX)
	{
		minX = b[0];
	}
	if (b[0] > maxX)
	{
		maxX = b[0];
	}
	if (c[0] < minX)
	{
		minX = c[0];
	}
	if (c[0] > maxX)
	{
		maxX = c[0];
	}
	if (b[1] < minY)
	{
		minY = b[1];
	}
	if (b[1] > maxY)
	{
		maxY = b[1];
	}
	if (c[1] < minY)
	{
		minY = c[1];
	}
	if (c[1] > maxY)
	{
		maxY = c[1];
	}

	{
		int spanX = maxX - minX;

		if (!s_gpu.uiViewActive && !g_cfg_sizeRuleCanvas)
		{
			spanX = CTR_Canvas_ToReferenceWidth(spanX);
		}

		return (spanX > NATIVE_GPU_SIZE_MAX_W) || ((maxY - minY) > NATIVE_GPU_SIZE_MAX_H);
	}
}

internal int NativeGpu_VertexSaturated(const VERTTYPE *p)
{
	return (p[0] == NATIVE_GPU_SIZE_SAT_HI) || (p[0] == NATIVE_GPU_SIZE_SAT_LO) || (p[1] == NATIVE_GPU_SIZE_SAT_HI) || (p[1] == NATIVE_GPU_SIZE_SAT_LO);
}

// p3 is NULL for a triangle. The blend mode number comes from the same word
// as in AddSplit: activeDrawEnv.tpage, which for textured primitives was set
// from the primitive a moment before. The vertices are the raw ones
// of the primitive, without the offset of the draw environment - the signature of the
// clamped division is only there.
internal void NativeGpu_NotePolySize(int type, bool semiTrans, const VERTTYPE *p0, const VERTTYPE *p1, const VERTTYPE *p2, const VERTTYPE *p3)
{
	s_subpixelPrimType = type;
	const int blend = semiTrans ? (((activeDrawEnv.tpage >> 5) & 3) + 1) : 0;
	int bigTriangles = NativeGpu_TriangleTooBig(p0, p1, p2);
	int saturated = NativeGpu_VertexSaturated(p0) || NativeGpu_VertexSaturated(p1) || NativeGpu_VertexSaturated(p2);

	// The mask for ParsePrimitive - bit 0 the first triangle
	// (x0,x1,x2), bit 1 the second (x1,x2,x3).
	s_gpuSizeDropMask = bigTriangles ? 1 : 0;

	if (p3 != NULL)
	{
		const int secondBig = NativeGpu_TriangleTooBig(p1, p2, p3);

		bigTriangles += secondBig;
		s_gpuSizeDropMask |= secondBig ? 2 : 0;
		saturated = saturated || NativeGpu_VertexSaturated(p3);
	}

	s_sizeWindow.seen[type][blend]++;
	s_sizeTotal.seen[type][blend]++;

	if (saturated)
	{
		s_sizeWindow.sat[type][blend]++;
		s_sizeTotal.sat[type][blend]++;
	}

	if (bigTriangles == 0)
	{
		return;
	}

	s_sizeWindow.over[type][blend]++;
	s_sizeTotal.over[type][blend]++;
	s_sizeWindow.overTriangles += (u64)bigTriangles;
	s_sizeTotal.overTriangles += (u64)bigTriangles;
	s_sizeThisFrame++;

	if (saturated)
	{
		s_sizeWindow.overSat[type][blend]++;
		s_sizeTotal.overSat[type][blend]++;
	}

	if (!s_sizeFirstSaid[type][blend])
	{
		s_sizeFirstSaid[type][blend] = 1;

		if (p3 != NULL)
		{
			Platform_Log("[CTR Size] first %s %s too big for the PS1 GPU, vblank %d: (%d,%d) (%d,%d) (%d,%d) (%d,%d) - %d of 2 triangle(s) over %d wide or %d tall%s\n",
			             s_sizeTypeName[type], s_sizeBlendName[blend], Platform_GetVBlankCount(), p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], p3[0], p3[1],
			             bigTriangles, NATIVE_GPU_SIZE_MAX_W, NATIVE_GPU_SIZE_MAX_H, saturated ? ", a vertex sits at +-0x3ff" : "");
		}
		else
		{
			Platform_Log("[CTR Size] first %s %s too big for the PS1 GPU, vblank %d: (%d,%d) (%d,%d) (%d,%d) - over %d wide or %d tall%s\n", s_sizeTypeName[type],
			             s_sizeBlendName[blend], Platform_GetVBlankCount(), p0[0], p0[1], p1[0], p1[1], p2[0], p2[1], NATIVE_GPU_SIZE_MAX_W, NATIVE_GPU_SIZE_MAX_H,
			             saturated ? ", a vertex sits at +-0x3ff" : "");
		}
	}
}

internal void NativeGpu_PrintSizeCensus(const struct NativeGpuSizeCensus *census, const char *span, int vblankNow)
{
	u64 seen = 0;
	u64 over = 0;
	u64 overSat = 0;
	u64 sat = 0;

	for (int type = 0; type < NATIVE_GPU_SIZE_TYPES; type++)
	{
		for (int blend = 0; blend < NATIVE_GPU_SIZE_BLENDS; blend++)
		{
			seen += census->seen[type][blend];
			over += census->over[type][blend];
			overSat += census->overSat[type][blend];
			sat += census->sat[type][blend];
		}
	}

	// Silent when there was nothing - like the split report.
	if ((over == 0) && (sat == 0))
	{
		return;
	}

	Platform_Log("[CTR Size] %s %d frame(s), vblank %d..%d: %llu of %llu polygon(s) too big for the PS1 GPU (over %d wide or %d tall), %llu triangle(s), in %d frame(s); "
	             "%llu of them with a vertex at +-0x3ff; %llu polygon(s) had such a vertex at all\n",
	             span, census->frames, census->vblankFirst, vblankNow, (unsigned long long)over, (unsigned long long)seen, NATIVE_GPU_SIZE_MAX_W,
	             NATIVE_GPU_SIZE_MAX_H, (unsigned long long)census->overTriangles, census->framesWithOver, (unsigned long long)overSat,
	             (unsigned long long)sat);

	for (int type = 0; type < NATIVE_GPU_SIZE_TYPES; type++)
	{
		for (int blend = 0; blend < NATIVE_GPU_SIZE_BLENDS; blend++)
		{
			if (census->over[type][blend] == 0)
			{
				continue;
			}

			Platform_Log("[CTR Size]   %-3s %-18s %9llu too big of %10llu seen, %9llu with a vertex at +-0x3ff\n", s_sizeTypeName[type],
			             s_sizeBlendName[blend], (unsigned long long)census->over[type][blend], (unsigned long long)census->seen[type][blend],
			             (unsigned long long)census->overSat[type][blend]);
		}
	}
}

// Once per frame, from the renderer's frame boundary, directly next to the
// page store report - the same windows of 300 frames, so that the
// two tables in the log match each other.
void NativeGpu_EndFrameSizeCensus(void)
{
	const int vblank = Platform_GetVBlankCount();

	s_auditFrameCounter++;
	s_auditThisFrame = g_cfg_splitAudit && ((s_auditFrameCounter % NGPU_AUDIT_EVERY) == 0);

	if (s_sizeThisFrame > 0)
	{
		s_sizeWindow.framesWithOver++;
		s_sizeTotal.framesWithOver++;
	}
	s_sizeThisFrame = 0;

	if (s_sizeWindow.frames == 0)
	{
		s_sizeWindow.vblankFirst = vblank;
	}
	if (s_sizeTotal.frames == 0)
	{
		s_sizeTotal.vblankFirst = vblank;
	}

	s_sizeWindow.frames++;
	s_sizeTotal.frames++;

	if (s_sizeWindow.frames < NATIVE_GPU_SIZE_WINDOW)
	{
		return;
	}

	NativeGpu_PrintSizeCensus(&s_sizeWindow, "over the last", vblank);
	Platform_Log("[CTR Size] block: %llu triangle(s) not drawn by the PS1 size rule (%llu polygon(s) whole)%s%llu null-texture polygon(s) skipped by --skip-null-tex; %llu null-texture polygon(s) seen, largest box %d px\n",
	             s_gpuSizeDroppedTrisWindow, s_gpuSizeDroppedWholeWindow, g_cfg_skipNullTex ? ", " : ", switch off, ", s_gpuNullTexSkippedWindow,
	             s_gpuNullTexSeenWindow, s_gpuNullTexBoxMaxWindow);
	s_gpuNullTexSeenTotal += s_gpuNullTexSeenWindow;
	if (s_gpuNullTexBoxMaxWindow > s_gpuNullTexBoxMaxTotal)
	{
		s_gpuNullTexBoxMaxTotal = s_gpuNullTexBoxMaxWindow;
	}
	s_gpuNullTexSeenWindow = 0;
	s_gpuNullTexBoxMaxWindow = 0;
	s_gpuSizeDroppedTrisTotal += s_gpuSizeDroppedTrisWindow;
	s_gpuSizeDroppedWholeTotal += s_gpuSizeDroppedWholeWindow;
	s_gpuNullTexSkippedTotal += s_gpuNullTexSkippedWindow;
	s_gpuSizeDroppedTrisWindow = 0;
	s_gpuSizeDroppedWholeWindow = 0;
	s_gpuNullTexSkippedWindow = 0;
	Platform_Log("[CTR GPU] block: split peak %d of %d, vertex peak %d of %d per batch, %llu cut(s) in this block\n", s_gpuSplitPeakWindow, (int)MAX_DRAW_SPLITS,
	             s_gpuVertexPeakWindow, (int)MAX_VERTEX_BUFFER_SIZE, (s_gpuCutsForVertices + s_gpuCutsForSplits) - s_gpuCutsReported);
	Platform_Log("[CTR GPU] block: %llu draw call(s), textured primitives by format 4-bit %llu, 8-bit %llu, 16-bit %llu\n", s_gpuDrawsWindow,
	             s_gpuPrimsByFormatWindow[0], s_gpuPrimsByFormatWindow[1], s_gpuPrimsByFormatWindow[2]);
	Platform_Log("[CTR Hole] block: oversize %llu (flat black %llu, textured %llu), large black flat %llu\n", s_tBigWindow, s_tBigBlackWindow, s_tBigTexturedWindow, s_tLargeBlackWindow);
	s_tBigWindow = 0;
	s_tBigBlackWindow = 0;
	s_tBigTexturedWindow = 0;
	s_tLargeBlackWindow = 0;
	Platform_Log("[CTR GPU] block: textured semi-trans primitives by blend mode: 0 average %llu, 1 add %llu, 2 subtract %llu, 3 add-quarter %llu\n",
	             s_gpuSemiByModeWindow[0], s_gpuSemiByModeWindow[1], s_gpuSemiByModeWindow[2], s_gpuSemiByModeWindow[3]);
	memset(s_gpuSemiByModeWindow, 0, sizeof(s_gpuSemiByModeWindow));
	s_gpuDrawsWindow = 0;
	memset(s_gpuPrimsByFormatWindow, 0, sizeof(s_gpuPrimsByFormatWindow));
	s_gpuCutsReported = s_gpuCutsForVertices + s_gpuCutsForSplits;
	s_gpuSplitPeakWindow = 0;
	s_gpuVertexPeakWindow = 0;
	NativeGpu_SplitPrintBlock();
	memset(&s_sizeWindow, 0, sizeof(s_sizeWindow));
}

// At exit, before the log is closed (Platform_AtExitReport).
void NativeGpu_PrintSizeReport(void)
{
	NativeGpu_PrintSizeCensus(&s_sizeTotal, "since start,", Platform_GetVBlankCount());
	Platform_Log("[CTR Size] at exit: %llu triangle(s) not drawn by the PS1 size rule (%llu polygon(s) whole); %llu null-texture polygon(s) skipped (--skip-null-tex %s); %llu null-texture polygon(s) seen, largest box %d px\n",
	             s_gpuSizeDroppedTrisTotal + s_gpuSizeDroppedTrisWindow, s_gpuSizeDroppedWholeTotal + s_gpuSizeDroppedWholeWindow,
	             s_gpuNullTexSkippedTotal + s_gpuNullTexSkippedWindow, g_cfg_skipNullTex ? "on" : "off",
	             s_gpuNullTexSeenTotal + s_gpuNullTexSeenWindow,
	             (s_gpuNullTexBoxMaxWindow > s_gpuNullTexBoxMaxTotal) ? s_gpuNullTexBoxMaxWindow : s_gpuNullTexBoxMaxTotal);
}

internal int ProcessFlatPoly(P_TAG *polyTag)
{
	const bool shadeTexOn = (polyTag->code & 1) == 0;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		POLY_F3 *poly = (POLY_F3 *)polyTag;

		AddSplit(semiTrans, false, false);
		NativeGpu_NotePolySize(NATIVE_GPU_SIZE_F3, semiTrans, &poly->x0, &poly->x1, &poly->x2, NULL);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangleZero(firstVertex, 0);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0);

		s_gpu.vertexIndex += 3;

		return 4;
	}
	case 0x4:
	{
		POLY_FT3 *poly = (POLY_FT3 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		// It is an official hack from SCE devs to not use DR_TPAGE and instead use null polygon
		if (!IsNull(poly))
		{
			AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));
			NativeGpu_NotePolySize(NATIVE_GPU_SIZE_FT3, semiTrans, &poly->x0, &poly->x1, &poly->x2, NULL);

			GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
			MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
			MakeTexcoordTriangle(firstVertex, &poly->u0, &poly->u1, &poly->u2, poly->tpage, poly->clut,
			                     GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
			MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0);

			s_gpu.vertexIndex += 3;
		}
		return 7;
	}
	case 0x8:
	{
		POLY_F4 *poly = (POLY_F4 *)polyTag;

		AddSplit(semiTrans, false, false);
		NativeGpu_NotePolySize(NATIVE_GPU_SIZE_F4, semiTrans, &poly->x0, &poly->x1, &poly->x2, &poly->x3);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;
		return 5;
	}
	case 0xC:
	{
		POLY_FT4 *poly = (POLY_FT4 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));
		NativeGpu_NotePolySize(NATIVE_GPU_SIZE_FT4, semiTrans, &poly->x0, &poly->x1, &poly->x2, &poly->x3);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuad(firstVertex, &poly->u0, &poly->u1, &poly->u3, &poly->u2, poly->tpage, poly->clut,
		                 GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 9;
	}
	}
	return 0;
}

internal int ProcessGouraudPoly(P_TAG *polyTag)
{
	const bool shadeTexOn = true;
	const bool semiTrans = (polyTag->code & 2);
	const int primSubType = polyTag->code & 0x0C;

	switch (primSubType)
	{
	case 0x0:
	{
		POLY_G3 *poly = (POLY_G3 *)polyTag;

		AddSplit(semiTrans, false, false);
		NativeGpu_NotePolySize(NATIVE_GPU_SIZE_G3, semiTrans, &poly->x0, &poly->x1, &poly->x2, NULL);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangleZero(firstVertex, 1);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r2);

		s_gpu.vertexIndex += 3;

		return 6;
	}
	case 0x4:
	{
		POLY_GT3 *poly = (POLY_GT3 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));
		NativeGpu_NotePolySize(NATIVE_GPU_SIZE_GT3, semiTrans, &poly->x0, &poly->x1, &poly->x2, NULL);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexTriangle(firstVertex, &poly->x0, &poly->x1, &poly->x2);
		MakeTexcoordTriangle(firstVertex, &poly->u0, &poly->u1, &poly->u2, poly->tpage, poly->clut, GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
		MakeColourTriangle(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r2);

		s_gpu.vertexIndex += 3;

		return 9;
	}
	case 0x8:
	{
		POLY_G4 *poly = (POLY_G4 *)polyTag;

		AddSplit(semiTrans, false, false);
		NativeGpu_NotePolySize(NATIVE_GPU_SIZE_G4, semiTrans, &poly->x0, &poly->x1, &poly->x2, &poly->x3);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuadZero(firstVertex, 1);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r3, &poly->r2);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 8;
	}
	case 0xC:
	{
		POLY_GT4 *poly = (POLY_GT4 *)polyTag;
		activeDrawEnv.tpage = poly->tpage;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(poly->tpage));
		NativeGpu_NotePolySize(NATIVE_GPU_SIZE_GT4, semiTrans, &poly->x0, &poly->x1, &poly->x2, &poly->x3);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexQuad(firstVertex, &poly->x0, &poly->x1, &poly->x3, &poly->x2);
		MakeTexcoordQuad(firstVertex, &poly->u0, &poly->u1, &poly->u3, &poly->u2, poly->tpage, poly->clut,
		                 GET_TPAGE_DITHER(activeDrawEnv.tpage) || activeDrawEnv.dtd);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r1, &poly->r3, &poly->r2);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 12;
	}
	}
	return 0;
}

internal int ProcessTileAndSprt(P_TAG *polyTag)
{
	// NOTE: TILE does not support switching shadeTex on real PSX
	const bool shadeTexOn = (polyTag->code & 1) == 0;
	const bool semiTrans = (polyTag->code & 2);

	switch (polyTag->code & 0xFD)
	{
	case 0x60:
	{
		TILE *poly = (TILE *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, poly->w, poly->h);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x64:
	{
		SPRT *poly = (SPRT *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, poly->w, poly->h);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, poly->w, poly->h);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 4;
	}
	case 0x68:
	{
		TILE_1 *poly = (TILE_1 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 1, 1);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x70:
	{
		TILE_8 *poly = (TILE_8 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 8, 8);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x74:
	{
		SPRT_8 *poly = (SPRT_8 *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 8, 8);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, 8, 8);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	case 0x78:
	{
		TILE_16 *poly = (TILE_16 *)polyTag;

		AddSplit(semiTrans, false, false);

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 16, 16);
		MakeTexcoordQuadZero(firstVertex, 0);
		MakeColourQuad(firstVertex, true, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 2;
	}
	case 0x7C:
	{
		SPRT_16 *poly = (SPRT_16 *)polyTag;

		AddSplit(semiTrans, true, NativeGpu_TPageOverlapsActiveDrawPage(activeDrawEnv.tpage));

		GrVertex *firstVertex = &s_gpu.vertexBuffer[s_gpu.vertexIndex];
		MakeVertexRect(firstVertex, &poly->x0, 16, 16);
		MakeTexcoordRect(firstVertex, &poly->u0, activeDrawEnv.tpage, poly->clut, 16, 16);
		MakeColourQuad(firstVertex, shadeTexOn, &poly->r0, &poly->r0, &poly->r0, &poly->r0);

		TriangulateQuad();

		s_gpu.vertexIndex += 6;

		return 3;
	}
	}
	return 0;
}

internal int ProcessDrawEnv(P_TAG *polyTag)
{
	const u32 *codePtr = (u32 *)&polyTag->pad0;
	int processedLongs = 0;
	bool fullDrawEnvPacket = false;
	for (int i = 0; i < (int)polyTag->len; ++i)
	{
		const u32 code = codePtr[i];
		const int primType = code >> 24 & 0xF0;
		const int primSubType = code >> 24 & 0x0F;

		// NOTE(aalhendi): CTR can pack draw-env commands, tagless geometry,
		// and more draw-env commands into one OT entry. Stop at the first
		// non-E command so ParseTaglessPrimitive owns the geometry payload.
		if (primType != 0xE0)
		{
			return processedLongs;
		}

		switch (primSubType)
		{
		case 0x1:
		{
			// DR_TPAGE
			activeDrawEnv.tpage = (code & 0x1FF);
			activeDrawEnv.dtd = (code >> 9) & 1;
			// NOTE(aalhendi): Standalone DR_TPAGE packets use the same E1 word
			// for blend changes; only full DRAWENV packets retarget native
			// on-screen/offscreen rendering.
			if (fullDrawEnvPacket)
			{
				activeDrawEnv.dfe = (code >> 10) & 1;
			}
			break;
		}
		case 0x2:
		{
			// DR_TWIN
			activeDrawEnv.tw.w = (code & 0x1F);
			activeDrawEnv.tw.h = ((code >> 5) & 0x1F);
			activeDrawEnv.tw.x = ((code >> 10) & 0x1F);
			activeDrawEnv.tw.y = ((code >> 15) & 0x1F);
			break;
		}
		case 0x3:
		{
			// DR_AREA
			activeDrawEnv.clip.x = code & 1023;
			activeDrawEnv.clip.y = (code >> 10) & 1023;
			fullDrawEnvPacket = true;
			break;
		}
		case 0x4:
		{
			// DR_AREA (second part)
			activeDrawEnv.clip.w = code & 1023;
			activeDrawEnv.clip.h = (code >> 10) & 1023;

			activeDrawEnv.clip.w = activeDrawEnv.clip.w - activeDrawEnv.clip.x + 1;
			activeDrawEnv.clip.h = activeDrawEnv.clip.h - activeDrawEnv.clip.y + 1;
			fullDrawEnvPacket = true;
			break;
		}
		case 0x5:
		{
			// DR_OFFSET
			activeDrawEnv.ofs[0] = NativeGpu_SignExtend11(code);
			activeDrawEnv.ofs[1] = NativeGpu_SignExtend11(code >> 11);
			fullDrawEnvPacket = true;
			break;
		}
		case 0x6:
		{
			SetPSXMaskState(code);
			break;
		}
		case 0:
			// NOTE(aalhendi): ctr-native local divergence for CTR OTs. A zero
			// word can be terminal draw-env padding, or the tag word for the
			// next primitive packed into the same OT entry.
			// return processedLongs;
			if (i + 1 != (int)polyTag->len)
			{
				return processedLongs;
			}
			break;
		}
		++processedLongs;
	}

	return processedLongs;
}

internal void ProcessDrawEnvCommand(u32 code)
{
	const int primSubType = code >> 24 & 0x0F;

	switch (primSubType)
	{
	case 0x1:
		activeDrawEnv.tpage = (code & 0x1FF);
		activeDrawEnv.dtd = (code >> 9) & 1;
		break;
	case 0x2:
		activeDrawEnv.tw.w = (code & 0x1F);
		activeDrawEnv.tw.h = ((code >> 5) & 0x1F);
		activeDrawEnv.tw.x = ((code >> 10) & 0x1F);
		activeDrawEnv.tw.y = ((code >> 15) & 0x1F);
		break;
	case 0x3:
		activeDrawEnv.clip.x = code & 1023;
		activeDrawEnv.clip.y = (code >> 10) & 1023;
		break;
	case 0x4:
		activeDrawEnv.clip.w = code & 1023;
		activeDrawEnv.clip.h = (code >> 10) & 1023;
		activeDrawEnv.clip.w = activeDrawEnv.clip.w - activeDrawEnv.clip.x + 1;
		activeDrawEnv.clip.h = activeDrawEnv.clip.h - activeDrawEnv.clip.y + 1;
		break;
	case 0x5:
		activeDrawEnv.ofs[0] = NativeGpu_SignExtend11(code);
		activeDrawEnv.ofs[1] = NativeGpu_SignExtend11(code >> 11);
		break;
	case 0x6:
		SetPSXMaskState(code);
		break;
	}
}

internal int ProcessPsyXPrims(P_TAG *polyTag)
{
	const int primSubType = polyTag->code & 0x0F;

	switch (primSubType)
	{
	case 0x01:
	{
		DR_PSYX_TEX *psytex = (DR_PSYX_TEX *)polyTag;
		s_gpu.overrideTexture = psytex->code[0] & 0xFFFFFF;
		s_gpu.overrideTextureWidth = psytex->code[1] & 0xFFF;
		s_gpu.overrideTextureHeight = psytex->code[1] >> 16 & 0xFFF;
		return 2;
	}
	case 0x02:
	{
		// [A] Psy-X custom debug marker packet
		DR_PSYX_DBGMARKER *psydbg = (DR_PSYX_DBGMARKER *)polyTag;
		s_gpu.currentSplitDebugText = psydbg->text;
		return 2;
	}
	}

	return 0;
}

// Processes primitive
// returns processed primitive primLength in longs
internal int ParsePrimitiveBody(P_TAG *polyTag);

// The one place where the write limit applies. Every path that writes vertices into
// s_gpu.vertexBuffer (ProcessFlatPoly, ProcessGouraudPoly,
// ProcessFlatLines, ProcessGouraudLines, ProcessTileAndSprt) is only called by
// ParsePrimitiveBody, and that only from here. If there is no more room for
// the largest primitive, the primitive writes into the guard zone and is
// dropped afterwards: the pointer goes back to the end of the buffer, a split that
// it created begins there and is empty. The last primitive accepted
// in the guard zone drops out with it. The length of the primitive still comes
// from the body, so that the walk through a packed packet is right.
// Geometry is lost this way, memory is not - drawing a batch and starting
// anew needs an upload route that does not wrap (native_renderer.c).
int ParsePrimitive(P_TAG *polyTag)
{
	// BATCH FULL: DRAW AND START ANEW, DO NOT DROP.
	//
	// Two limits, one cut. No more room for the largest primitive,
	// or the split table is so full that AddSplit would refuse the next split
	// (splitIndex + 1 >= MAX_DRAW_SPLITS) - then the
	// batch goes to the renderer now, and the drawing state stays: activeDrawEnv,
	// blend, texture override, mask bit are global, ClearSplits does not touch
	// them, and the first AddSplit afterwards creates against the sentinel split 0
	// a fresh split with exactly this state. The order stays, because
	// the ordering table is only submitted earlier - the route that DR_MOVE
	// has always taken. The ring per frame holds two batches (NATIVE_VERTEX_RING_SIZE).
	if (s_gpu.vertexIndex > NATIVE_GPU_VERTEX_LIMIT)
	{
		s_gpuCutsForVertices++;
		NativeGpu_NoteCutLoss();
		DrawAllSplits();
	}
	else if ((s_gpu.splitIndex + 2) >= MAX_DRAW_SPLITS)
	{
		s_gpuCutsForSplits++;
		NativeGpu_NoteCutLoss();
		DrawAllSplits();
	}

	// Safety behind the cut: should the buffer be full anyway,
	// what follows drops and counts as before. After a cut the
	// pointer is zero, so this is the case that never happens - and is measured.
	const bool full = s_gpu.vertexIndex > NATIVE_GPU_VERTEX_LIMIT;
	int primLength;

	if (full)
	{
		s_gpu.vertexIndex = NATIVE_GPU_VERTEX_LIMIT;
	}

	const int vertexBefore = s_gpu.vertexIndex;

	s_gpuSizeDropMask = 0;
	primLength = ParsePrimitiveBody(polyTag);

	if (!full)
	{
		// THE PS1 SIZE RULE. A polygon (0x20..0x3f) has three
		// vertices here (triangle) or six (quad, two triangles in
		// PS1 order). What NotePolySize measured as too large
		// comes back out of the buffer - the triangle individually, like the GPU.
		const int isPoly = (polyTag->code & 0xe0) == 0x20;
		const int written = s_gpu.vertexIndex - vertexBefore;

		if (isPoly && (s_gpuSizeDropMask != 0) && ((written == 3) || (written == 6)))
		{
			if (written == 3)
			{
				s_gpu.vertexIndex = vertexBefore;
				s_gpuSizeDroppedTrisWindow++;
				s_gpuSizeDroppedWholeWindow++;
			}
			else if (s_gpuSizeDropMask == 3)
			{
				s_gpu.vertexIndex = vertexBefore;
				s_gpuSizeDroppedTrisWindow += 2;
				s_gpuSizeDroppedWholeWindow++;
			}
			else if (s_gpuSizeDropMask == 1)
			{
				// only (x0,x1,x2) too large: the second triangle moves forward
				s_gpu.vertexBuffer[vertexBefore + 0] = s_gpu.vertexBuffer[vertexBefore + 3];
				s_gpu.vertexBuffer[vertexBefore + 1] = s_gpu.vertexBuffer[vertexBefore + 4];
				s_gpu.vertexBuffer[vertexBefore + 2] = s_gpu.vertexBuffer[vertexBefore + 5];
				s_gpu.vertexIndex = vertexBefore + 3;
				s_gpuSizeDroppedTrisWindow++;
			}
			else
			{
				s_gpu.vertexIndex = vertexBefore + 3;
				s_gpuSizeDroppedTrisWindow++;
			}
		}

		// MEASURING SWITCH --skip-null-tex: textured polygon with
		// empty texture reference - page 0, CLUT 0, all UV 0 at every vertex.
		if (isPoly && ((polyTag->code & 0x04) != 0) && (s_gpu.vertexIndex > vertexBefore))
		{
			int v;
			int empty = 1;
			int bx0 = 32767, by0 = 32767, bx1 = -32768, by1 = -32768;

			for (v = vertexBefore; v < s_gpu.vertexIndex; v++)
			{
				const GrVertex *g = &s_gpu.vertexBuffer[v];

				if ((g->page != 0) || (g->clut != 0) || (g->u != 0) || (g->v != 0))
				{
					empty = 0;
					break;
				}
				if (g->x < bx0) bx0 = g->x;
				if (g->x > bx1) bx1 = g->x;
				if (g->y < by0) by0 = g->y;
				if (g->y > by1) by1 = g->y;
			}
			if (empty)
			{
				// Counted in every run, including the largest box (area in
				// PSX pixels); skipped only with the measuring switch.
				const int area = (bx1 - bx0 + 1) * (by1 - by0 + 1);

				s_gpuNullTexSeenWindow++;
				if (area > s_gpuNullTexBoxMaxWindow)
				{
					s_gpuNullTexBoxMaxWindow = area;
				}
				if (g_cfg_skipNullTex)
				{
					s_gpu.vertexIndex = vertexBefore;
					s_gpuNullTexSkippedWindow++;
				}
			}
		}

		NativeGpu_JoinSemiSplit(vertexBefore);
		NativeGpu_NoteOversizeAndBlack(vertexBefore, polyTag);
		if (g_cfg_holeProbeCount > 0)
		{
			NativeGpu_HoleProbe(vertexBefore, polyTag);
		}
		NativeGpu_VertexDump(vertexBefore, polyTag);
	}

	if (full)
	{
		GPUDrawSplit *curSplit = &s_gpu.splits[s_gpu.splitIndex];

		if (s_gpu.vertexIndex > NATIVE_GPU_VERTEX_LIMIT)
		{
			// It has written - into the guard zone. Dropped, counted.
			s_gpuVertexDrops++;
			if (!s_gpu.vertexDropActive)
			{
				s_gpu.vertexDropActive = true;
				s_gpuVertexDropRuns++;
			}
		}

		s_gpu.vertexIndex = NATIVE_GPU_VERTEX_LIMIT;
		if ((int)curSplit->startVertex > NATIVE_GPU_VERTEX_LIMIT)
		{
			curSplit->startVertex = (u16)NATIVE_GPU_VERTEX_LIMIT;
		}
	}

	return primLength;
}

internal int ParsePrimitiveBody(P_TAG *polyTag)
{
	const int primType = polyTag->code & 0xF0;

	int primLength = 0;
	bool handledZeroLength = false;

	switch (primType)
	{
	case 0x00:
	{
		const int primSubType = polyTag->code & 0x0F;
		const u32 *codePtr = (u32 *)&polyTag->pad0;
		// NOTE(aalhendi): ctr-native local divergence. CTR RenderWeather can
		// emit a retail length-2 zero packet when weather is enabled but the
		// level has no fill-mode payload. The PSX consumes it by tag length;
		// the native parser must advance past it too.
		if (polyTag->len == 2 && codePtr[0] == 0 && codePtr[1] == 0)
		{
			primLength = 2;
		}
		else if (polyTag->len == 0 && *(u32 *)polyTag == 0)
		{
			// CTR ghost transparency packets include raw GPU NOP words between
			// draw-mode changes and triangle commands. They consume exactly one
			// command word; ParsePrimitivesLinkedList adds P_LEN to the return.
			handledZeroLength = true;
		}
		else if (primSubType == 0x0)
		{
			primLength = 3;
		}
		else if (primSubType == 0x1)
		{
			DR_MOVE *drmove = (DR_MOVE *)polyTag;
			const u32 rectPos = drmove->code[2];
			const u32 rectSize = drmove->code[4];

			const int y = drmove->code[3] >> 0x10 & 0xFFFF;
			const int x = drmove->code[3] & 0xFFFF;

			RECT16 rect;
			rect.x = (s16)(rectPos & 0xffff);
			rect.y = (s16)(rectPos >> 16);
			rect.w = (s16)(rectSize & 0xffff);
			rect.h = (s16)(rectSize >> 16);

			if (NativeGpu_HasPendingSplits())
			{
				DrawAllSplits();
			}
			MoveImage(&rect, x, y);
			primLength = 5;
		}
		else if (primSubType == 0x2)
		{
			// NOTE(aalhendi): ctr-native local divergence. CTR emits retail
			// FILL packets in OTs; the old PsyCross parser did not consume them, which caused
			// zero-length primitive spam.
			TILE *fill = (TILE *)polyTag;
			RECT16 rect;

			rect.x = fill->x0;
			rect.y = fill->y0;
			rect.w = fill->w;
			rect.h = fill->h;

			if (NativeGpu_HasPendingSplits())
			{
				DrawAllSplits();
			}
			ClearImage(&rect, fill->r0, fill->g0, fill->b0);
			primLength = 3;
		}
		break;
	}
	case 0x20:
		// Flat polygons
		primLength = ProcessFlatPoly(polyTag);
		break;
	case 0x30:
		// Gouraud shaded polygons
		primLength = ProcessGouraudPoly(polyTag);
		break;
	case 0x40:
		// Flat (single colour) Lines
		primLength = ProcessFlatLines(polyTag);
		break;
	case 0x50:
		// Gouraud lines
		primLength = ProcessGouraudLines(polyTag);
		break;
	case 0x60:
	case 0x70:
		// TILE and SPRT
		primLength = ProcessTileAndSprt(polyTag);
		break;
	case 0xA0:
		// DR_LOAD
		{
			DR_LOAD *drload = (DR_LOAD *)polyTag;
			const u32 rectPos = drload->code[1];
			const u32 rectSize = drload->code[2];

			RECT16 rect;
			rect.x = (s16)(rectPos & 0xffff);
			rect.y = (s16)(rectPos >> 16);
			rect.w = (s16)(rectSize & 0xffff);
			rect.h = (s16)(rectSize >> 16);

			LoadImage(&rect, (uint32_t *)drload->p);

			// TODO(aalhendi): Audit whether CTR ever appends additional GPU
			// commands after a DR_LOAD payload in the same packet.
		}
		primLength = getlen(polyTag);
		break;
	case 0xB0:
		// [A] Psy-X custom primitives
		primLength = ProcessPsyXPrims(polyTag);
		break;
	case 0xE0:
		// Draw Env setup
		primLength = ProcessDrawEnv(polyTag);
		break;
		// default:
		//	NATIVE_GPU_ERROR("got %0x primitive\n", primType);
	}

	if (primLength == 0 && !handledZeroLength)
	{
		NATIVE_GPU_ERROR("Unhandled zero length %0x primitive\n", primType);
	}

	return primLength;
}

int ParseTaglessPrimitive(u32 *command)
{
	const u32 code = *command;
	const int primType = (code >> 24) & 0xF0;

	if (code == 0)
	{
		return 1;
	}

	if (primType == 0xE0)
	{
		ProcessDrawEnvCommand(code);
		return 1;
	}

	P_TAG *polyTag = (P_TAG *)(command - P_LEN);
	int primLength = ParsePrimitive(polyTag);

	if (primLength == 0)
	{
		NATIVE_GPU_ERROR("Unhandled tagless primitive %08x\n", code);
		return 1;
	}

	return primLength;
}
