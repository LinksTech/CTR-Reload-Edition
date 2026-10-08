// ===========================================================================
// THE NATIVE TEXTURE MANAGER (renderer plan C.6) - see include/platform/native_tex.h.
//
// What lives here and nowhere else:
//   - the edge rule (powers of two, 16..2048) and the check against the
//     device's maxImageDimension2D - never in NativeGfx_CreateTexture, which
//     every texture goes through (renderer plan C.6.4);
//   - the levels, computed on the CPU in integers (renderer plan C.6.2);
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

// --- The tables -------------------------------------------------------------
//
// sRGB byte to linear light, 0..65535: round(65535 * EOTF(c / 255)), with the
// sRGB EOTF v / 12.92 for v <= 0.04045, ((v + 0.055) / 1.055)^2.4 above.
// Constants, computed once in 60-digit decimal arithmetic outside this tree;
// never at run time, so no libm can move a value.
global_variable const u16 s_nativeTexSrgbToLinear[256] = {
	    0,    20,    40,    60,    80,    99,   119,   139,   159,   179,   199,   219,
	  241,   264,   288,   313,   340,   367,   396,   427,   458,   491,   526,   562,
	  599,   637,   677,   718,   761,   805,   851,   898,   947,   997,  1048,  1101,
	 1156,  1212,  1270,  1330,  1391,  1453,  1517,  1583,  1651,  1720,  1790,  1863,
	 1937,  2013,  2090,  2170,  2250,  2333,  2418,  2504,  2592,  2681,  2773,  2866,
	 2961,  3058,  3157,  3258,  3360,  3464,  3570,  3678,  3788,  3900,  4014,  4129,
	 4247,  4366,  4488,  4611,  4736,  4864,  4993,  5124,  5257,  5392,  5530,  5669,
	 5810,  5953,  6099,  6246,  6395,  6547,  6700,  6856,  7014,  7174,  7335,  7500,
	 7666,  7834,  8004,  8177,  8352,  8528,  8708,  8889,  9072,  9258,  9445,  9635,
	 9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090,
	12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874,
	15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
	18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481,
	21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325,
	25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542,
	29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
	34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138,
	39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534,
	45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341,
	50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
	57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221,
	63795, 64372, 64952, 65535,
};

// Linear light back to the sRGB byte: the byte is the largest c with
// s_nativeTexLinearToSrgb[c - 1] <= L (0 when there is none). Entry c - 1 is
// ceil(65535 * EOTF((c - 0.5) / 255)), the linear value of the midpoint between
// the bytes c - 1 and c: rounding to the nearest byte in the encoded scale,
// decided exactly on the integer. Every byte survives the trip
// (s_nativeTexSrgbToLinear[c] lies 9 or more steps inside its interval).
global_variable const u16 s_nativeTexLinearToSrgb[255] = {
	   10,    30,    50,    70,    90,   110,   130,   150,   170,   189,   209,   230,
	  253,   276,   301,   327,   354,   382,   412,   443,   475,   509,   544,   580,
	  618,   657,   698,   740,   783,   828,   875,   923,   972,  1023,  1075,  1129,
	 1185,  1242,  1300,  1360,  1422,  1486,  1551,  1617,  1685,  1755,  1827,  1900,
	 1975,  2052,  2130,  2210,  2292,  2376,  2461,  2548,  2637,  2727,  2820,  2914,
	 3010,  3108,  3208,  3309,  3412,  3518,  3625,  3734,  3844,  3957,  4072,  4188,
	 4307,  4427,  4550,  4674,  4800,  4928,  5059,  5191,  5325,  5461,  5599,  5740,
	 5882,  6026,  6173,  6321,  6471,  6624,  6778,  6935,  7094,  7255,  7418,  7583,
	 7750,  7919,  8091,  8265,  8440,  8618,  8798,  8981,  9165,  9352,  9541,  9732,
	 9925, 10121, 10318, 10518, 10720, 10925, 11132, 11341, 11552, 11765, 11981, 12199,
	12420, 12643, 12868, 13095, 13325, 13557, 13791, 14028, 14267, 14508, 14752, 14998,
	15247, 15498, 15751, 16007, 16265, 16525, 16788, 17054, 17321, 17592, 17864, 18139,
	18417, 18697, 18980, 19264, 19552, 19842, 20134, 20429, 20727, 21027, 21329, 21634,
	21942, 22252, 22564, 22880, 23197, 23518, 23840, 24166, 24494, 24824, 25158, 25493,
	25832, 26173, 26516, 26862, 27211, 27563, 27917, 28273, 28633, 28995, 29359, 29727,
	30097, 30469, 30845, 31223, 31603, 31987, 32373, 32762, 33153, 33547, 33944, 34344,
	34747, 35152, 35560, 35970, 36384, 36800, 37219, 37640, 38065, 38492, 38922, 39355,
	39790, 40229, 40670, 41114, 41561, 42011, 42463, 42918, 43377, 43838, 44301, 44768,
	45238, 45710, 46185, 46663, 47144, 47628, 48115, 48605, 49097, 49593, 50091, 50592,
	51096, 51604, 52114, 52627, 53142, 53661, 54183, 54708, 55235, 55766, 56300, 56836,
	57376, 57918, 58464, 59012, 59564, 60118, 60675, 61236, 61799, 62366, 62935, 63508,
	64083, 64662, 65244,
};

internal u32 NativeTex_Decode(u8 c, int linearData)
{
	return linearData ? (u32)c : (u32)s_nativeTexSrgbToLinear[c];
}

internal u8 NativeTex_Encode(u32 value, int linearData)
{
	int lo = 0;
	int hi = 255;

	if (linearData)
	{
		return (u8)((value > 255u) ? 255u : value);
	}

	while (lo < hi)
	{
		const int mid = (lo + hi + 1) / 2;

		if ((u32)s_nativeTexLinearToSrgb[mid - 1] <= value)
		{
			lo = mid;
		}
		else
		{
			hi = mid - 1;
		}
	}

	return (u8)lo;
}

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
	int edge = (width > height) ? width : height;
	int levels = 1;

	while (edge > 1)
	{
		edge >>= 1;
		levels++;
	}

	return levels;
}

internal int NativeTex_LevelEdge(int edge, int level)
{
	const int halved = edge >> level;

	return (halved > 0) ? halved : 1;
}

size_t NativeTex_ChainBytes(int width, int height, int levelCount)
{
	size_t bytes = 0;
	int level;

	for (level = 0; level < levelCount; level++)
	{
		bytes += (size_t)NativeTex_LevelEdge(width, level) * (size_t)NativeTex_LevelEdge(height, level) * 4u;
	}

	return bytes;
}

// One level from the one before. Each texel of the result is the box of the
// two by two texels under it - two by one or one by two once an edge of the
// source is 1, one by one at 1x1. With alpha weights a_i:
//   colour  L = (sum L_i * a_i + A / 2) / A, A = sum a_i > 0,
//           L = (sum L_i + n / 2) / n when every a_i is 0 (no weight left; the
//           plain mean keeps a sensible colour under the transparent texel),
//   alpha   (A + n / 2) / n,
// L in linear light (sRGB) or the stored byte (linear data). At most
// 65535 * 255 * 4 in a sum: u32.
internal void NativeTex_Halve(const u8 *source, int sourceWidth, int sourceHeight, int linearData, u8 *destination)
{
	const int width = (sourceWidth > 1) ? (sourceWidth / 2) : 1;
	const int height = (sourceHeight > 1) ? (sourceHeight / 2) : 1;
	const int columns = (sourceWidth > 1) ? 2 : 1;
	const int rows = (sourceHeight > 1) ? 2 : 1;
	const u32 count = (u32)(columns * rows);
	int x;
	int y;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			const u8 *texels[4];
			u32 alphaSum = 0;
			int t = 0;
			int dy;
			int dx;
			int channel;
			u8 *out = destination + ((size_t)y * (size_t)width + (size_t)x) * 4u;

			for (dy = 0; dy < rows; dy++)
			{
				for (dx = 0; dx < columns; dx++)
				{
					texels[t] = source + ((size_t)(y * rows + dy) * (size_t)sourceWidth + (size_t)(x * columns + dx)) * 4u;
					alphaSum += texels[t][3];
					t++;
				}
			}

			for (channel = 0; channel < 3; channel++)
			{
				u32 sum = 0;
				u32 value;
				int i;

				if (alphaSum > 0u)
				{
					for (i = 0; i < t; i++)
					{
						sum += NativeTex_Decode(texels[i][channel], linearData) * (u32)texels[i][3];
					}
					value = (sum + alphaSum / 2u) / alphaSum;
				}
				else
				{
					for (i = 0; i < t; i++)
					{
						sum += NativeTex_Decode(texels[i][channel], linearData);
					}
					value = (sum + count / 2u) / count;
				}

				out[channel] = NativeTex_Encode(value, linearData);
			}

			out[3] = (u8)((alphaSum + count / 2u) / count);
		}
	}
}

void NativeTex_BuildLevels(const u8 *level0, int width, int height, int levelCount, int linearData, u8 *chain)
{
	size_t offset;
	int level;

	if ((level0 == NULL) || (chain == NULL) || (levelCount < 1))
	{
		return;
	}

	offset = (size_t)width * (size_t)height * 4u;
	memcpy(chain, level0, offset);

	for (level = 1; level < levelCount; level++)
	{
		const int sourceWidth = NativeTex_LevelEdge(width, level - 1);
		const int sourceHeight = NativeTex_LevelEdge(height, level - 1);
		const size_t sourceBytes = (size_t)sourceWidth * (size_t)sourceHeight * 4u;

		NativeTex_Halve(chain + offset - sourceBytes, sourceWidth, sourceHeight, linearData, chain + offset);
		offset += (size_t)NativeTex_LevelEdge(width, level) * (size_t)NativeTex_LevelEdge(height, level) * 4u;
	}
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
		offset += (size_t)NativeTex_LevelEdge(desc->width, level) * (size_t)NativeTex_LevelEdge(desc->height, level) * 4u;
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
		if ((c > 0) && (s_nativeTexSrgbToLinear[c] <= s_nativeTexSrgbToLinear[c - 1]))
		{
			failures++;
		}
		if ((c > 1) && (s_nativeTexLinearToSrgb[c - 1] <= s_nativeTexLinearToSrgb[c - 2]))
		{
			failures++;
		}
		if (NativeTex_Encode(s_nativeTexSrgbToLinear[c], 0) == (u8)c)
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
		tables[c * 2 + 0] = (u8)(s_nativeTexSrgbToLinear[c] & 255u);
		tables[c * 2 + 1] = (u8)(s_nativeTexSrgbToLinear[c] >> 8);
	}
	for (c = 0; c < 255; c++)
	{
		tables[512 + c * 2 + 0] = (u8)(s_nativeTexLinearToSrgb[c] & 255u);
		tables[512 + c * 2 + 1] = (u8)(s_nativeTexLinearToSrgb[c] >> 8);
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
