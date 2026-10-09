// ===========================================================================
// THE NATIVE TEXTURE MANAGER (renderer plan C.6) - see include/platform/native_tex.h.
//
// What lives here and nowhere else:
//   - the edge rule (powers of two, 16..2048) and the check against the
//     device's maxImageDimension2D - never in NativeGfx_CreateTexture, which
//     every texture goes through (renderer plan C.6.4);
//   - the upload of the levels, computed on the CPU in integers by
//     include/rldmip.inc, which rldpack and Reload Studio share (C.6.2);
//   - the sampling of the filter option and the material flag (C.6.3);
//   - the upload through the device's waited one-shot, and the staging buffer
//     given back after it (C.6.4);
//   - the count of uploads that happen in a race frame (the upload rule: textures
//     go up while a loading or menu screen is up, never during a race frame).
//
// The device side is NativeGfx_CreateTextureLevels and its neighbours
// (include/platform/native_gfx.h, block "Textures with levels"). The existing
// one-level functions stay untouched and carry every VRAM upload.
//
// Host memory only (SDL_malloc for the levels while they are made, a static
// table of live textures); never MEMPACK, primMem or otMem, never in a
// savestate. No clock, no random numbers; the only game state read is the
// race-frame question, and only to count.
// ===========================================================================

#include "platform/native_tex.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int g_cfg_nativeFilter = NATIVE_TEX_FILTER_NEAREST;

// --- The levels -------------------------------------------------------------
//
// The tables, the box of two by two and the chain live in include/rldmip.inc,
// shared with rldpack and Reload Studio (the 3D preview builds the same
// levels); this file uploads them.
#include <rldmip.inc>

// --- Pure functions ---------------------------------------------------------

internal int NativeTex_IsPowerOfTwo(int value)
{
	return (value > 0) && ((value & (value - 1)) == 0);
}

NativeTexResult NativeTex_CheckEdges(int width, int height, int maxDimension)
{
	if (!NativeTex_IsPowerOfTwo(width) || !NativeTex_IsPowerOfTwo(height) || (width < NATIVE_TEX_EDGE_MIN) || (height < NATIVE_TEX_EDGE_MIN) ||
	    (width > NATIVE_TEX_EDGE_MAX) || (height > NATIVE_TEX_EDGE_MAX))
	{
		return NATIVE_TEX_REFUSED_EDGE;
	}

	if ((maxDimension > 0) && ((width > maxDimension) || (height > maxDimension)))
	{
		return NATIVE_TEX_REFUSED_DEVICE_LIMIT;
	}

	return NATIVE_TEX_OK;
}

int NativeTex_LevelCount(int width, int height)
{
	return RldMip_LevelCount(width, height);
}

size_t NativeTex_ChainBytes(int width, int height, int levelCount)
{
	return RldMip_ChainBytes(width, height, levelCount);
}

void NativeTex_BuildLevels(const u8 *level0, int width, int height, int levelCount, int linearData, u8 *chain)
{
	RldMip_BuildLevels(level0, width, height, levelCount, linearData, chain);
}

int NativeTex_FilterFromName(const char *name)
{
	if (name == NULL)
	{
		return -1;
	}
	if (strcmp(name, "nearest") == 0)
	{
		return NATIVE_TEX_FILTER_NEAREST;
	}
	if (strcmp(name, "linear") == 0)
	{
		return NATIVE_TEX_FILTER_LINEAR;
	}

	return -1;
}

const char *NativeTex_FilterName(int filter)
{
	return (filter == NATIVE_TEX_FILTER_LINEAR) ? "linear" : "nearest";
}

const char *NativeTex_ResultName(NativeTexResult result)
{
	switch (result)
	{
	case NATIVE_TEX_OK:
		return "ok";
	case NATIVE_TEX_REFUSED_LOCKED:
		return "locked (no --native-preview)";
	case NATIVE_TEX_REFUSED_EDGE:
		return "edge not a power of two in 16..2048";
	case NATIVE_TEX_REFUSED_DEVICE_LIMIT:
		return "edge above maxImageDimension2D";
	case NATIVE_TEX_REFUSED_PIXELS:
		return "no pixels or levels that do not fit";
	case NATIVE_TEX_REFUSED_MEMORY:
		return "no host memory for the levels";
	case NATIVE_TEX_REFUSED_DEVICE:
		return "refused by the device";
	case NATIVE_TEX_REFUSED_FULL:
		return "table of live textures full";
	default:
		return "unknown";
	}
}

// --- The race frame ---------------------------------------------------------
//
// A race frame is a frame of the gameplay state with no loading screen and no
// menu up:
//   sdata->mainGameState == 3   the gameplay update (game/MAIN/MainMain.c:228,
//                               "case 3"); 0 is the start before the first load
//                               (StateZero, MainMain.c:124, which sets 3 at :773
//                               after it), 1 and 2 are the steps between a
//                               finished load or a restart and the race
//                               (MainMain.c:128, :205; state 1 sets 3 at :200).
//   gGT->gameMode1 & LOADING    the loading screen: set when the checkered flag
//                               covers the screen (MainMain.c:235) and at the
//                               first load (MainMain.c:794), cleared when the
//                               ten stages are done (MainMain.c:348) or a
//                               restart is (MainMain.c:275).
//   sdata->Loading.stage >= 0   the ten stages are running (LOAD_TenStages,
//                               called at MainMain.c:324; stage 5 among them) -
//                               a loading screen even in a frame the flag does
//                               not cover yet. -1 is idle; -4 and -5 wait for
//                               the flag with the race still on screen and are
//                               left to the LOADING bit.
//   gGT->gameMode1 & MAIN_MENU  a menu (include/namespace_Main.h:20), and the
//   levelID == MAIN_MENU_LEVEL  main menu level itself (character select
//                               included).
// Everything else counts as a race frame - a cutscene too, which is stricter
// than the rule needs and keeps the count from missing anything.
int NativeTex_InRaceFrame(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	if ((gGT == NULL) || (sdata->mainGameState != 3))
	{
		return 0;
	}
	if (((((u32)gGT->gameMode1) & (u32)LOADING) != 0u) || (sdata->Loading.stage >= LOAD_TEN_STAGES_0))
	{
		return 0;
	}
	if (((((u32)gGT->gameMode1) & (u32)MAIN_MENU) != 0u) || (gGT->levelID == MAIN_MENU_LEVEL))
	{
		return 0;
	}

	return 1;
}

// --- The manager ------------------------------------------------------------

// Live textures: what NativeTex_IsSrgb and NativeTex_ApplyFilter need to know.
// 256 is far beyond one driver per seat with a handful of textures each.
#define NATIVE_TEX_MAX_LIVE 256

struct NativeTexLive
{
	TextureID texture;
	u32 flags;
	int levels;
	NativeGfxWrap wrapU;
	NativeGfxWrap wrapV;
};

global_variable struct NativeTexLive s_nativeTexLive[NATIVE_TEX_MAX_LIVE];

global_variable struct
{
	int reportRegistered;
	u32 made;
	u32 refused;
	u32 destroyed;
	u32 levels;
	u64 bytes;
	u32 uploads;
	u32 uploadsInRaceFrame;
	u32 stagingGivenBack;
	u64 stagingGivenBackBytes;
} s_nativeTex;

internal void NativeTex_ReportAtExit(void)
{
	u32 live = 0;
	int i;

	for (i = 0; i < NATIVE_TEX_MAX_LIVE; i++)
	{
		if (s_nativeTexLive[i].texture != 0)
		{
			live++;
		}
	}

	Platform_Log("[CTR NativeTex] at exit: textures made %u, refused %u, destroyed %u, live %u, levels %u, %u KB; uploads %u, uploads during a "
	             "race frame %u; staging given back %u time(s), %u KB; filter %s\n",
	             s_nativeTex.made, s_nativeTex.refused, s_nativeTex.destroyed, live, s_nativeTex.levels, (unsigned int)(s_nativeTex.bytes / 1024u),
	             s_nativeTex.uploads, s_nativeTex.uploadsInRaceFrame, s_nativeTex.stagingGivenBack,
	             (unsigned int)(s_nativeTex.stagingGivenBackBytes / 1024u), NativeTex_FilterName(g_cfg_nativeFilter));
}

// Only with --native-preview, so a run without it has neither the line nor the
// slot in the exit table.
internal void NativeTex_NoteUse(void)
{
	if (!s_nativeTex.reportRegistered)
	{
		s_nativeTex.reportRegistered = 1;
		Platform_AtExitReport(NativeTex_ReportAtExit);
	}
}

// The sampling of a texture: the material flag first, then the filter option.
internal NativeGfxSampling NativeTex_Sampling(u32 flags, NativeGfxWrap wrapU, NativeGfxWrap wrapV, int levels)
{
	NativeGfxSampling sampling;
	const int pixelated = ((flags & NATIVE_TEX_FLAG_ALWAYS_NEAREST) != 0u) || (g_cfg_nativeFilter != NATIVE_TEX_FILTER_LINEAR);

	memset(&sampling, 0, sizeof(sampling));
	sampling.magFilter = pixelated ? NATIVE_GFX_FILTER_NEAREST : NATIVE_GFX_FILTER_LINEAR;
	sampling.minFilter = sampling.magFilter;
	sampling.mipMode = (levels <= 1) ? NATIVE_GFX_MIP_NONE : (pixelated ? NATIVE_GFX_MIP_NEAREST : NATIVE_GFX_MIP_LINEAR);
	sampling.wrapU = wrapU;
	sampling.wrapV = wrapV;

	if (!pixelated)
	{
		NativeGfxTextureLimits limits;

		memset(&limits, 0, sizeof(limits));
		NativeGfx_TextureLimits(&limits);

		// Up to the device limit; the device clamps again and grants nothing
		// when it was made without the feature.
		sampling.anisotropy = limits.anisotropyEnabled ? (int)limits.maxSamplerAnisotropy : 0;
	}

	return sampling;
}

internal void NativeTex_Refuse(const NativeTexDesc *desc, NativeTexResult why, NativeTexResult *result)
{
	if (result != NULL)
	{
		*result = why;
	}

	if (why == NATIVE_TEX_REFUSED_LOCKED)
	{
		// Silent and uncounted: without the switch the manager does not exist.
		return;
	}

	s_nativeTex.refused++;
	Platform_Log("[CTR NativeTex] '%s' %dx%d refused: %s\n", ((desc != NULL) && (desc->name != NULL)) ? desc->name : "(unnamed)",
	             (desc != NULL) ? desc->width : 0, (desc != NULL) ? desc->height : 0, NativeTex_ResultName(why));
}

// The shared end of both creates: hands the levels to the device, counts,
// gives the staging buffer back, logs one line.
internal TextureID NativeTex_Upload(const NativeTexDesc *desc, const u8 *const *levels, int levelCount, NativeTexResult *result)
{
	NativeGfxTextureLevelsDesc device;
	int slot;
	int level;
	int inRaceFrame;
	TextureID texture;
	u32 stagingBefore;
	u32 freed;
	size_t bytes;

	for (slot = 0; slot < NATIVE_TEX_MAX_LIVE; slot++)
	{
		if (s_nativeTexLive[slot].texture == 0)
		{
			break;
		}
	}

	if (slot >= NATIVE_TEX_MAX_LIVE)
	{
		NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_FULL, result);
		return NATIVE_GFX_INVALID;
	}

	memset(&device, 0, sizeof(device));
	device.width = desc->width;
	device.height = desc->height;
	device.format = ((desc->flags & NATIVE_TEX_FLAG_LINEAR_DATA) != 0u) ? NATIVE_GFX_TEXFMT_RGBA8 : NATIVE_GFX_TEXFMT_RGBA8_SRGB;
	device.levelCount = levelCount;
	for (level = 0; level < levelCount; level++)
	{
		device.levels[level] = levels[level];
	}
	device.sampling = NativeTex_Sampling(desc->flags, desc->wrapU, desc->wrapV, levelCount);

	// Counted before the device sees it: an upload attempted in a race frame
	// breaks the rule whether or not the device then takes it.
	inRaceFrame = NativeTex_InRaceFrame();
	s_nativeTex.uploads++;
	if (inRaceFrame)
	{
		s_nativeTex.uploadsInRaceFrame++;
	}

	stagingBefore = NativeGfx_StagingBytes();
	texture = NativeGfx_CreateTextureLevels(&device);

	// Back to the size it had before this upload, whether the device took the
	// texture or not: only what this upload grew goes back. A buffer the VRAM
	// fallback had grown stays, so no later fallback - in a race frame, perhaps
	// - has to make it again.
	freed = NativeGfx_ShrinkStaging(stagingBefore);
	if (freed > 0u)
	{
		s_nativeTex.stagingGivenBack++;
		s_nativeTex.stagingGivenBackBytes += freed;
	}

	if (texture == NATIVE_GFX_INVALID)
	{
		NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_DEVICE, result);
		return NATIVE_GFX_INVALID;
	}

	bytes = NativeTex_ChainBytes(desc->width, desc->height, levelCount);

	s_nativeTexLive[slot].texture = texture;
	s_nativeTexLive[slot].flags = desc->flags;
	s_nativeTexLive[slot].levels = levelCount;
	s_nativeTexLive[slot].wrapU = desc->wrapU;
	s_nativeTexLive[slot].wrapV = desc->wrapV;

	s_nativeTex.made++;
	s_nativeTex.levels += (u32)levelCount;
	s_nativeTex.bytes += (u64)bytes;

	Platform_Log("[CTR NativeTex] '%s' %dx%d %s, %d level(s), %u KB, sampling %s%s, uploaded %s; staging %u KB given back\n",
	             (desc->name != NULL) ? desc->name : "(unnamed)", desc->width, desc->height,
	             ((desc->flags & NATIVE_TEX_FLAG_LINEAR_DATA) != 0u) ? "RGBA8 UNORM" : "RGBA8 SRGB", levelCount, (unsigned int)(bytes / 1024u),
	             ((desc->flags & NATIVE_TEX_FLAG_ALWAYS_NEAREST) != 0u) ? "nearest (material)" : NativeTex_FilterName(g_cfg_nativeFilter),
	             (device.sampling.anisotropy > 1) ? " with anisotropy" : "", inRaceFrame ? "IN A RACE FRAME" : "outside a race frame",
	             (unsigned int)(freed / 1024u));

	if (result != NULL)
	{
		*result = NATIVE_TEX_OK;
	}

	return texture;
}

// The checks both creates share: the switch, the pixels, the edges, the device.
internal int NativeTex_Admit(const NativeTexDesc *desc, NativeTexResult *result)
{
	extern int g_cfg_nativePreview;
	NativeGfxTextureLimits limits;
	NativeTexResult edges;

	if (!g_cfg_nativePreview)
	{
		NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_LOCKED, result);
		return 0;
	}

	NativeTex_NoteUse();

	if (desc == NULL)
	{
		NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_PIXELS, result);
		return 0;
	}

	memset(&limits, 0, sizeof(limits));
	NativeGfx_TextureLimits(&limits);

	edges = NativeTex_CheckEdges(desc->width, desc->height, limits.maxImageDimension2D);
	if (edges != NATIVE_TEX_OK)
	{
		NativeTex_Refuse(desc, edges, result);
		return 0;
	}

	return 1;
}

TextureID NativeTex_Create(const NativeTexDesc *desc, NativeTexResult *result)
{
	const u8 *levels[NATIVE_GFX_MAX_TEXTURE_LEVELS];
	TextureID texture;
	u8 *chain;
	size_t offset;
	int levelCount;
	int level;

	if (!NativeTex_Admit(desc, result))
	{
		return NATIVE_GFX_INVALID;
	}

	if (desc->rgba == NULL)
	{
		NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_PIXELS, result);
		return NATIVE_GFX_INVALID;
	}

	levelCount = ((desc->flags & NATIVE_TEX_FLAG_ONE_LEVEL) != 0u) ? 1 : NativeTex_LevelCount(desc->width, desc->height);

	chain = (u8 *)SDL_malloc(NativeTex_ChainBytes(desc->width, desc->height, levelCount));
	if (chain == NULL)
	{
		NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_MEMORY, result);
		return NATIVE_GFX_INVALID;
	}

	NativeTex_BuildLevels(desc->rgba, desc->width, desc->height, levelCount, ((desc->flags & NATIVE_TEX_FLAG_LINEAR_DATA) != 0u) ? 1 : 0, chain);

	offset = 0;
	for (level = 0; level < levelCount; level++)
	{
		levels[level] = chain + offset;
		offset += (size_t)RldMip_LevelEdge(desc->width, level) * (size_t)RldMip_LevelEdge(desc->height, level) * 4u;
	}

	texture = NativeTex_Upload(desc, levels, levelCount, result);

	// The levels are on the device now (or refused); the host copy goes.
	SDL_free(chain);

	return texture;
}

TextureID NativeTex_CreateFromLevels(const NativeTexDesc *desc, const u8 *const *levels, int levelCount, NativeTexResult *result)
{
	int level;

	if (!NativeTex_Admit(desc, result))
	{
		return NATIVE_GFX_INVALID;
	}

	if ((levels == NULL) || (levelCount < 1) || (levelCount > NativeTex_LevelCount(desc->width, desc->height)) ||
	    (levelCount > NATIVE_GFX_MAX_TEXTURE_LEVELS))
	{
		NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_PIXELS, result);
		return NATIVE_GFX_INVALID;
	}

	for (level = 0; level < levelCount; level++)
	{
		if (levels[level] == NULL)
		{
			NativeTex_Refuse(desc, NATIVE_TEX_REFUSED_PIXELS, result);
			return NATIVE_GFX_INVALID;
		}
	}

	return NativeTex_Upload(desc, levels, levelCount, result);
}

void NativeTex_Destroy(TextureID texture)
{
	int i;

	if ((texture == 0) || (texture == NATIVE_GFX_INVALID))
	{
		return;
	}

	for (i = 0; i < NATIVE_TEX_MAX_LIVE; i++)
	{
		if (s_nativeTexLive[i].texture == texture)
		{
			memset(&s_nativeTexLive[i], 0, sizeof(s_nativeTexLive[i]));
			s_nativeTex.destroyed++;
			NativeGfx_DestroyTexture(texture);
			return;
		}
	}
}

int NativeTex_IsSrgb(TextureID texture)
{
	int i;

	if ((texture == 0) || (texture == NATIVE_GFX_INVALID))
	{
		return 0;
	}

	for (i = 0; i < NATIVE_TEX_MAX_LIVE; i++)
	{
		if (s_nativeTexLive[i].texture == texture)
		{
			return ((s_nativeTexLive[i].flags & NATIVE_TEX_FLAG_LINEAR_DATA) != 0u) ? 0 : 1;
		}
	}

	return 0;
}

u32 NativeTex_UploadsInRaceFrame(void)
{
	return s_nativeTex.uploadsInRaceFrame;
}

void NativeTex_ApplyFilter(int filter)
{
	int i;

	g_cfg_nativeFilter = (filter == NATIVE_TEX_FILTER_LINEAR) ? NATIVE_TEX_FILTER_LINEAR : NATIVE_TEX_FILTER_NEAREST;

	for (i = 0; i < NATIVE_TEX_MAX_LIVE; i++)
	{
		if (s_nativeTexLive[i].texture != 0)
		{
			const NativeGfxSampling sampling = NativeTex_Sampling(s_nativeTexLive[i].flags, s_nativeTexLive[i].wrapU, s_nativeTexLive[i].wrapV,
			                                                      s_nativeTexLive[i].levels);

			NativeGfx_SetTextureSampling(s_nativeTexLive[i].texture, &sampling);
		}
	}
}

// --- Self-test --------------------------------------------------------------

// The levels of this image are pinned below. 32x16, so the chain ends in two
// levels with an edge of 1 (2x1, 1x1). Every fourth texel on a diagonal is
// fully transparent, the rest has alpha 40..255 - the weights do real work.
internal void NativeTex_TestImage(u8 *pixels, int width, int height)
{
	int x;
	int y;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			u8 *p = pixels + ((size_t)y * (size_t)width + (size_t)x) * 4u;

			p[0] = (u8)((x * 37 + y * 11) & 255);
			p[1] = (u8)((x * x * 3 + y * 29) & 255);
			p[2] = (u8)(((x ^ y) * 23) & 255);
			p[3] = (u8)((((x + y) % 4) == 0) ? 0 : ((x * y * 13 + 40) & 255));
		}
	}
}

// SHA-256 of the chain bytes of the test image (6 levels, 2732 bytes), computed
// apart from this file by an independent implementation of the same rule.
#define NATIVE_TEX_GOLDEN_SRGB   "a5a29bd35b804143de9d5d2d906be0dce6d804724b5f1a1206d1477590dee862"
#define NATIVE_TEX_GOLDEN_LINEAR "35f614f4e3eb7d1eab2b6fd0fe77469e628aaf1d3c2e6d45f289b36a89e18680"
// SHA-256 of both tables, little-endian u16, first then second.
#define NATIVE_TEX_GOLDEN_TABLES "f0af3130ba0f9051eb58eb97eca390622a3ab0c7633d2d3deba5c54a9572598b"

internal void NativeTex_HashHex(const void *bytes, size_t count, char hex[65])
{
	static const char hexDigits[] = "0123456789abcdef";
	u8 hash[32];
	int i;

	Sha256(bytes, count, hash);
	for (i = 0; i < 32; i++)
	{
		hex[i * 2 + 0] = hexDigits[hash[i] >> 4];
		hex[i * 2 + 1] = hexDigits[hash[i] & 15];
	}
	hex[64] = '\0';
}

// The shader's way back from linear light to gamma (the "nr" program,
// platform/native_shaders.inc), in float32 as the GPU computes it, fed with the
// exact decode of every byte and stored as a UNORM target stores it (round to
// nearest). Returns how many of the 256 bytes come back unchanged; *margin is
// the smallest distance to the rounding edge, in steps (0.5 = dead centre).
internal int NativeTex_ShaderRoundTrip(double *margin)
{
	int exact = 0;
	int c;

	*margin = 0.5;

	for (c = 0; c < 256; c++)
	{
		const double v = (double)c / 255.0;
		const float linear = (float)((v <= 0.04045) ? (v / 12.92) : pow((v + 0.055) / 1.055, 2.4));
		const float encoded = (linear < 0.0031308f) ? (linear * 12.92f) : ((1.055f * powf(linear, 1.0f / 2.4f)) - 0.055f);
		const double scaled = (double)encoded * 255.0;
		const int stored = (int)floor(scaled + 0.5);
		const double edge = 0.5 - fabs(scaled - (double)c);

		if (stored == c)
		{
			exact++;
		}
		if (edge < *margin)
		{
			*margin = edge;
		}
	}

	return exact;
}

int NativeTex_SelfTest(void)
{
	enum
	{
		TEST_W = 32,
		TEST_H = 16,
	};
	static u8 image[TEST_W * TEST_H * 4];
	static u8 chainSrgb[TEST_W * TEST_H * 4 * 2];
	static u8 chainLinear[TEST_W * TEST_H * 4 * 2];
	static u8 tables[(256 + 255) * 2];
	static u8 uniform[16 * 16 * 4];
	static u8 uniformChain[16 * 16 * 4 * 2];
	char srgbHex[65];
	char linearHex[65];
	char tablesHex[65];
	int failures = 0;
	int roundTrip = 0;
	int shaderExact;
	double margin;
	int edgeChecks = 0;
	int uniformChecks = 0;
	int levels;
	size_t bytes;
	int c;
	int i;

	// The tables: strictly rising, and every byte survives the trip.
	for (c = 0; c < 256; c++)
	{
		if ((c > 0) && (s_rldMipSrgbToLinear[c] <= s_rldMipSrgbToLinear[c - 1]))
		{
			failures++;
		}
		if ((c > 1) && (s_rldMipLinearToSrgb[c - 1] <= s_rldMipLinearToSrgb[c - 2]))
		{
			failures++;
		}
		if (RldMip_Encode(s_rldMipSrgbToLinear[c], 0) == (u8)c)
		{
			roundTrip++;
		}
	}
	if (roundTrip != 256)
	{
		failures++;
	}

	for (c = 0; c < 256; c++)
	{
		tables[c * 2 + 0] = (u8)(s_rldMipSrgbToLinear[c] & 255u);
		tables[c * 2 + 1] = (u8)(s_rldMipSrgbToLinear[c] >> 8);
	}
	for (c = 0; c < 255; c++)
	{
		tables[512 + c * 2 + 0] = (u8)(s_rldMipLinearToSrgb[c] & 255u);
		tables[512 + c * 2 + 1] = (u8)(s_rldMipLinearToSrgb[c] >> 8);
	}
	NativeTex_HashHex(tables, sizeof(tables), tablesHex);

	shaderExact = NativeTex_ShaderRoundTrip(&margin);
	if (shaderExact != 256)
	{
		failures++;
	}

	// The edge rule and the device limit.
	{
		static const struct
		{
			int w;
			int h;
			int limit;
			NativeTexResult want;
		} cases[] = {
		    {16, 16, 0, NATIVE_TEX_OK},
		    {2048, 2048, 0, NATIVE_TEX_OK},
		    {16, 2048, 0, NATIVE_TEX_OK},
		    {8, 16, 0, NATIVE_TEX_REFUSED_EDGE},
		    {4096, 16, 0, NATIVE_TEX_REFUSED_EDGE},
		    {24, 32, 0, NATIVE_TEX_REFUSED_EDGE},
		    {0, 16, 0, NATIVE_TEX_REFUSED_EDGE},
		    {-16, 16, 0, NATIVE_TEX_REFUSED_EDGE},
		    {2048, 1024, 1024, NATIVE_TEX_REFUSED_DEVICE_LIMIT},
		    {1024, 1024, 1024, NATIVE_TEX_OK},
		};

		for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
		{
			edgeChecks++;
			if (NativeTex_CheckEdges(cases[i].w, cases[i].h, cases[i].limit) != cases[i].want)
			{
				failures++;
			}
		}

		if ((NativeTex_LevelCount(2048, 2048) != 12) || (NativeTex_LevelCount(2048, 16) != 12) || (NativeTex_LevelCount(16, 16) != 5) ||
		    (NativeTex_LevelCount(32, 16) != 6))
		{
			failures++;
		}
	}

	// A box of equal texels is that texel again, at every level - for the
	// sRGB and the linear rule, opaque, half and fully transparent.
	{
		static const u8 colours[][4] = {
		    {0, 0, 0, 255}, {255, 255, 255, 255}, {1, 2, 3, 255}, {128, 64, 200, 128}, {200, 10, 90, 0}, {254, 253, 252, 1},
		};
		int linearData;

		levels = NativeTex_LevelCount(16, 16);
		bytes = NativeTex_ChainBytes(16, 16, levels);

		for (linearData = 0; linearData < 2; linearData++)
		{
			for (i = 0; i < (int)(sizeof(colours) / sizeof(colours[0])); i++)
			{
				size_t at;

				for (at = 0; at < 16u * 16u; at++)
				{
					memcpy(uniform + at * 4u, colours[i], 4u);
				}
				NativeTex_BuildLevels(uniform, 16, 16, levels, linearData, uniformChain);

				uniformChecks++;
				for (at = 0; at < bytes; at += 4u)
				{
					if (memcmp(uniformChain + at, colours[i], 4u) != 0)
					{
						failures++;
						break;
					}
				}
			}
		}
	}

	// The fixed image against its golden, both rules.
	NativeTex_TestImage(image, TEST_W, TEST_H);
	levels = NativeTex_LevelCount(TEST_W, TEST_H);
	bytes = NativeTex_ChainBytes(TEST_W, TEST_H, levels);
	NativeTex_BuildLevels(image, TEST_W, TEST_H, levels, 0, chainSrgb);
	NativeTex_BuildLevels(image, TEST_W, TEST_H, levels, 1, chainLinear);
	NativeTex_HashHex(chainSrgb, bytes, srgbHex);
	NativeTex_HashHex(chainLinear, bytes, linearHex);

	if ((strcmp(srgbHex, NATIVE_TEX_GOLDEN_SRGB) != 0) || (strcmp(linearHex, NATIVE_TEX_GOLDEN_LINEAR) != 0) ||
	    (strcmp(tablesHex, NATIVE_TEX_GOLDEN_TABLES) != 0))
	{
		failures++;
	}

	if (failures != 0)
	{
		printf("native tex selftest: %d failure(s): table round trip %d of 256, shader round trip %d of 256 (margin %.2f), %d edge checks, "
		       "%d uniform chains, tables sha256 %s, %dx%d chain %d levels %u bytes sha256 srgb %s linear %s, golden %s, %s, %s\n",
		       failures, roundTrip, shaderExact, margin, edgeChecks, uniformChecks, tablesHex, TEST_W, TEST_H, levels, (unsigned int)bytes, srgbHex,
		       linearHex, NATIVE_TEX_GOLDEN_TABLES, NATIVE_TEX_GOLDEN_SRGB, NATIVE_TEX_GOLDEN_LINEAR);
		return 1;
	}

	printf("native tex selftest passed: table round trip 256 of 256, shader round trip 256 of 256 margin %.2f of 0.50, %d edge checks, "
	       "%d uniform chains, tables sha256 %s, %dx%d chain %d levels %u bytes sha256 srgb %s linear %s\n",
	       margin, edgeChecks, uniformChecks, tablesHex, TEST_W, TEST_H, levels, (unsigned int)bytes, srgbHex, linearHex);
	return 0;
}
