/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/include/PsyX/PsyX_render.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef NATIVE_RENDERER_TYPES_H
#define NATIVE_RENDERER_TYPES_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>

#define LUT_WIDTH              (256)
#define LUT_HEIGHT             (256)

#define VRAM_WIDTH             (1024)
#define VRAM_HEIGHT            (512)

#define TPAGE_WIDTH            (256)
#define TPAGE_HEIGHT           (256)

#define MAX_VERTEX_BUFFER_SIZE (1u << 16)

// TWO NUMBERS FOR TWO THINGS.
//
// MAX_VERTEX_BUFFER_SIZE is the size of ONE BATCH: that many vertices
// the parser collects (s_gpu.vertexBuffer) before it draws. The ring per
// frame is something else: ALL batches of a frame land there one after another,
// because the draws only run at submission and nothing may be overwritten
// that a recorded draw still needs. That used to be
// one number - and a frame with more vertices than one batch (Sunset Vista:
// 67,659 to 75,500) had no room in the ring for its second batch.
// Then two batches per frame fitted, the third wrapped and was
// counted.
//
// Now FOUR. A measured run (Inferno + Vista, 43:18)
// said "vertex ring on, peak 125259 of 131072 vertices in a frame, 6
// upload(s) in a frame at most" - 4.4 percent of headroom, and a wrap is not a
// counted error but a draw that draws geometry that another
// draw is overwriting right now. A ring is 20 bytes per vertex, so 5 MiB per
// frame in flight instead of 2.5 - in mapped host memory, not in VRAM.
#define NATIVE_VERTEX_RING_SIZE (4u * MAX_VERTEX_BUFFER_SIZE)

typedef struct
{
	s16 x, y, page, clut;

	u8 u, v, bright, dither;
	u8 r, g, b, a;

	s8 tcx, tcy, _p0, _p1;
} GrVertex;

CTR_STATIC_ASSERT(sizeof(GrVertex) == 20);

typedef enum
{
	a_position,
	a_texcoord,
	a_color,
	a_extra,
} ShaderAttrib;

typedef enum
{
	BM_NONE,
	BM_AVERAGE,
	BM_ADD,
	BM_SUBTRACT,
	BM_ADD_QUATER_SOURCE,

	// Straight source-alpha blending. Not a PSX mode - the four above are, and
	// they all weight by a constant because that is what the hardware did. This
	// one exists for overlays drawn on top of the emulated frame, which have a
	// real alpha channel to honour. Last in the list so the PSX modes keep
	// their numbering.
	BM_ALPHA,

	// TEXEL-WEIGHTED VERSIONS OF THE PSX MODES 0, 1 AND 3.
	//
	// The PS1 blends a textured primitive only where the texel carries the
	// STP bit; the other texels are opaque. Before this change that was one
	// draw per primitive in TWO passes (opaque without STP, blended with STP)
	// and therefore a split of its own per primitive. Here the weight per
	// fragment comes from the second shader output (dual source, SRC1): a1 = 0 for
	// a texel without STP, otherwise the weight of the mode. One draw, the
	// painter's order between primitives is right by itself.
	//   mode 0  B/2 + F/2 :  (1 - a1) F + a1 B,          a1 = 0.5
	//   mode 1  B + F     :  F + a1 B,                    a1 = 1
	//   mode 3  B + F/4   :  c1 F + a1 B,  c1 = 0.25,     a1 = 1
	// Mode 2 (B - F) has no such version: the blend operator (subtraction
	// against addition) is fixed per pipeline, a draw cannot switch per fragment
	// between B - F and F. It stays with two passes.
	BM_AVERAGE_TEXEL,
	BM_ADD_TEXEL,
	BM_ADD_QUATER_SOURCE_TEXEL
} BlendMode;

typedef enum
{
	TF_4_BIT,
	TF_8_BIT,
	TF_16_BIT,

	TF_32_BIT_RGBA
} TexFormat;

typedef u32 TextureID;
typedef u32 ShaderID;

#endif
