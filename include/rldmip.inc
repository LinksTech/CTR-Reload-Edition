// rldmip.inc - the mip levels of a native texture, once, for the game and the tools
//
// AN INCLUDE, NOT A MODULE. The game includes it in platform/native_tex.c (the
// levels it uploads); tools/rldpack.c (its self-test) and Reload Studio
// (tools/reloadstudio/rs_tex.c, the levels of the 3D preview) include the
// same file. u8, u16 and u32 come from the including file: the game has them
// from include/macros.h, the tools define them themselves. Every function is
// static; nothing here allocates, prints or reads a clock.
//
// THE LEVELS (renderer plan C.6.2). Computed on the CPU from level 0, each from
// the one before: a 2x2 box (2x1 or 1x2 once an edge is 1), in linear light
// through the two tables below for sRGB colour, with alpha as weight - the
// colours are averaged premultiplied and divided back, so a transparent texel
// lends no colour to its neighbours. Integers only, the tables are constants
// in the source: the same bytes on every machine. Rounding is half up
// throughout. Vulkan halves an edge as max(1, edge >> n); the textures of a
// character are powers of two (CTXT-4), so no level has an odd edge.
//
// The golden SHA-256 of a fixed image's chain pins these bytes: ctest
// native_tex_selftest (the game, platform/native_tex.c) and the self-test of
// rldpack (tools/rldpack_native.inc) check the same values, so the game, the
// packer and the preview build the same levels.

#ifndef RLDMIP_INC
#define RLDMIP_INC

#include <stddef.h>
#include <string.h>

// The deepest chain: an edge of 2048 (CTXT-4, NATIVE_TEX_EDGE_MAX) down to 1.
#define RLDMIP_LEVELS_MAX 12

// --- The tables -------------------------------------------------------------
//
// sRGB byte to linear light, 0..65535: round(65535 * EOTF(c / 255)), with the
// sRGB EOTF v / 12.92 for v <= 0.04045, ((v + 0.055) / 1.055)^2.4 above.
// Constants, computed once in 60-digit decimal arithmetic outside this tree;
// never at run time, so no libm can move a value.
static const u16 s_rldMipSrgbToLinear[256] = {
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
// s_rldMipLinearToSrgb[c - 1] <= L (0 when there is none). Entry c - 1 is
// ceil(65535 * EOTF((c - 0.5) / 255)), the linear value of the midpoint between
// the bytes c - 1 and c: rounding to the nearest byte in the encoded scale,
// decided exactly on the integer. Every byte survives the trip
// (s_rldMipSrgbToLinear[c] lies 9 or more steps inside its interval).
static const u16 s_rldMipLinearToSrgb[255] = {
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

static u32 RldMip_Decode(u8 c, int linearData)
{
	return linearData ? (u32)c : (u32)s_rldMipSrgbToLinear[c];
}

static u8 RldMip_Encode(u32 value, int linearData)
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

		if ((u32)s_rldMipLinearToSrgb[mid - 1] <= value)
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

// --- The chain ---------------------------------------------------------------

// The full chain down to 1x1: floor(log2(max(width, height))) + 1.
static int RldMip_LevelCount(int width, int height)
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

// The edge of a level: max(1, edge >> level).
static int RldMip_LevelEdge(int edge, int level)
{
	const int halved = edge >> level;

	return (halved > 0) ? halved : 1;
}

// Bytes of levels 0 .. levelCount - 1, tightly packed one after the other.
static size_t RldMip_ChainBytes(int width, int height, int levelCount)
{
	size_t bytes = 0;
	int level;

	for (level = 0; level < levelCount; level++)
	{
		bytes += (size_t)RldMip_LevelEdge(width, level) * (size_t)RldMip_LevelEdge(height, level) * 4u;
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
static void RldMip_Halve(const u8 *source, int sourceWidth, int sourceHeight, int linearData, u8 *destination)
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
						sum += RldMip_Decode(texels[i][channel], linearData) * (u32)texels[i][3];
					}
					value = (sum + alphaSum / 2u) / alphaSum;
				}
				else
				{
					for (i = 0; i < t; i++)
					{
						sum += RldMip_Decode(texels[i][channel], linearData);
					}
					value = (sum + count / 2u) / count;
				}

				out[channel] = RldMip_Encode(value, linearData);
			}

			out[3] = (u8)((alphaSum + count / 2u) / count);
		}
	}
}

// Writes levels 0 .. levelCount - 1 into chain (RldMip_ChainBytes bytes):
// level 0 copied, every further level from the one before. linearData: 1
// for data that is not colour (CTXT flag RLDCHAR_TEX_LINEAR, the game's
// NATIVE_TEX_FLAG_LINEAR_DATA): averaged in the stored values.
static void RldMip_BuildLevels(const u8 *level0, int width, int height, int levelCount, int linearData, u8 *chain)
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
		const int sourceWidth = RldMip_LevelEdge(width, level - 1);
		const int sourceHeight = RldMip_LevelEdge(height, level - 1);
		const size_t sourceBytes = (size_t)sourceWidth * (size_t)sourceHeight * 4u;

		RldMip_Halve(chain + offset - sourceBytes, sourceWidth, sourceHeight, linearData, chain + offset);
		offset += (size_t)RldMip_LevelEdge(width, level) * (size_t)RldMip_LevelEdge(height, level) * 4u;
	}
}

#endif
