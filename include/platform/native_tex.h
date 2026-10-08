#ifndef PLATFORM_NATIVE_TEX_H
#define PLATFORM_NATIVE_TEX_H

// THE NATIVE TEXTURE MANAGER (renderer plan C.6).
//
// Textures of the native layer with their levels: the edge rule, the device
// limit, the levels computed on the CPU, the sampling of the filter option, the
// upload at a loading or menu screen and the staging buffer given back after
// it. Beside the VRAM mirror and the page store, never inside them, and never
// in a savestate: what it holds is device memory and a host table, rebuilt from
// files, not game state.
//
// LOCKED without --native-preview: every create answers NATIVE_GFX_INVALID with
// NATIVE_TEX_REFUSED_LOCKED, nothing is allocated, nothing is logged, no exit
// report is registered. The pure functions (edges, level count, levels,
// self-test) work without it and without a device.
//
// THE LEVELS. Computed on the CPU from level 0, each from the one before: a 2x2
// box (2x1 or 1x2 once an edge is 1), in linear light through two tables for
// sRGB colour, with alpha as weight - the colours are averaged premultiplied
// and divided back, so a transparent texel lends no colour to its neighbours
// and no dark rim appears when filtered (renderer plan C.6.2). Integers only, the
// tables are constants in the source: the same bytes on every machine.
// Rounding is half up throughout. A box of four equal texels gives that texel
// again, for every value (the self-test checks all 256).

#include <macros.h>
#include <platform/native_gfx.h>

#include <stddef.h>

// Every edge a power of two in this range, each on its own (16x2048 is fine).
// Vulkan halves an edge as max(1, edge >> n); with powers of two no level has
// an odd edge, so there is no rule for a leftover row or column (renderer plan C.6.1).
#define NATIVE_TEX_EDGE_MIN 16
#define NATIVE_TEX_EDGE_MAX 2048

// NativeTexDesc.flags (renderer plan C.6.1, "flags").
#define NATIVE_TEX_FLAG_LINEAR_DATA    0x1u // not colour: RGBA8 UNORM, levels averaged in the stored values
#define NATIVE_TEX_FLAG_ALWAYS_NEAREST 0x2u // material flag: pixelated whatever the filter option says
#define NATIVE_TEX_FLAG_ONE_LEVEL      0x4u // level 0 only, no levels computed

// THE FILTER OPTION (renderer plan C.6.3), apart from g_cfg_bilinearFiltering, which
// keeps acting on the PSX path alone. Pixelated: NEAREST/NEAREST, level
// NEAREST. Filtered: LINEAR/LINEAR, level LINEAR, plus anisotropy up to the
// device limit - granted only when the device was made with --native-preview.
// NATIVE_TEX_FLAG_ALWAYS_NEAREST beats it. A development switch
// --native-filter nearest|linear sets it for the run (main.c); the default is
// nearest. Never saved.
enum
{
	NATIVE_TEX_FILTER_NEAREST = 0,
	NATIVE_TEX_FILTER_LINEAR = 1,
};

extern int g_cfg_nativeFilter;

// NATIVE_TEX_FILTER_* for "nearest" / "linear", -1 for anything else.
int NativeTex_FilterFromName(const char *name);
const char *NativeTex_FilterName(int filter);

typedef enum
{
	NATIVE_TEX_OK = 0,
	NATIVE_TEX_REFUSED_LOCKED,       // no --native-preview
	NATIVE_TEX_REFUSED_EDGE,         // an edge not a power of two in 16..2048
	NATIVE_TEX_REFUSED_DEVICE_LIMIT, // an edge above maxImageDimension2D
	NATIVE_TEX_REFUSED_PIXELS,       // no pixels, or levels that do not fit
	NATIVE_TEX_REFUSED_MEMORY,       // no host memory for the levels
	NATIVE_TEX_REFUSED_DEVICE,       // the device would not make it
	NATIVE_TEX_REFUSED_FULL,         // the manager's table is full
} NativeTexResult;

const char *NativeTex_ResultName(NativeTexResult result);

typedef struct
{
	int width; // of level 0
	int height;

	// Level 0, RGBA8 with straight (not premultiplied) alpha, tightly packed.
	// sRGB encoded colour unless NATIVE_TEX_FLAG_LINEAR_DATA.
	const u8 *rgba;

	u32 flags;
	NativeGfxWrap wrapU;
	NativeGfxWrap wrapV;

	// For the log only; may be NULL.
	const char *name;
} NativeTexDesc;

// A texture with its levels computed here. Upload through the waited one-shot
// (NativeGfx_CreateTextureLevels), then the staging buffer is given back.
// Call it while a loading or menu screen is up, never in a race frame - every
// call made in one is counted ("uploads during a race frame", exit line).
// result may be NULL.
TextureID NativeTex_Create(const NativeTexDesc *desc, NativeTexResult *result);

// The same with levels the caller made - for test textures whose levels must
// differ on purpose (the probe form mips: one colour per level). levels[n] is
// max(1, width >> n) by max(1, height >> n) texels; levelCount 1 up to the full
// chain. desc->rgba is ignored.
TextureID NativeTex_CreateFromLevels(const NativeTexDesc *desc, const u8 *const *levels, int levelCount, NativeTexResult *result);

void NativeTex_Destroy(TextureID texture);

// 1 when the texture is sRGB: whoever draws it with the "nr" program writes
// 1.0 into the block's params[1], so the shader turns the sample back to gamma.
// 0 for NATIVE_TEX_FLAG_LINEAR_DATA and for any texture not made here.
int NativeTex_IsSrgb(TextureID texture);

// Sets the filter option and gives every live texture its new sampling. The
// anisotropy is fixed at device creation; a run without --native-preview at
// start filters without it.
void NativeTex_ApplyFilter(int filter);

// THE RACE FRAME, the frame the upload rule keeps uploads out of: the game is in
// its gameplay state, no loading screen is up and no menu. See native_tex.c for
// the fields and where the game sets them.
int NativeTex_InRaceFrame(void);

// How many uploads through the manager came in a race frame (the number in
// its exit line). 0 in a run without --native-preview.
u32 NativeTex_UploadsInRaceFrame(void);

// --- Pure functions, no device ----------------------------------------------

// NATIVE_TEX_OK, NATIVE_TEX_REFUSED_EDGE or NATIVE_TEX_REFUSED_DEVICE_LIMIT.
// maxDimension <= 0 skips the device limit.
NativeTexResult NativeTex_CheckEdges(int width, int height, int maxDimension);

// The full chain down to 1x1: floor(log2(max(width, height))) + 1.
int NativeTex_LevelCount(int width, int height);

// Bytes of levels 0 .. levelCount - 1, tightly packed one after the other.
size_t NativeTex_ChainBytes(int width, int height, int levelCount);

// Writes levels 0 .. levelCount - 1 into chain (NativeTex_ChainBytes bytes):
// level 0 copied, every further level from the one before. linearData: 1 for
// NATIVE_TEX_FLAG_LINEAR_DATA.
void NativeTex_BuildLevels(const u8 *level0, int width, int height, int levelCount, int linearData, u8 *chain);

// --native-tex-selftest (ctest native_tex_selftest): tables, the shader round
// trip, the edge rule and the levels of a fixed image against their golden
// SHA-256. One line on stdout, 0 = passed. No window, no device.
int NativeTex_SelfTest(void);

#endif
