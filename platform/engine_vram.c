#include <platform/engine.h>

// ENGINE VRAM - see include/platform/engine.h.
//
// The PS1 VRAM is 1024 x 512 16-bit words. A texture page is addressed from
// (pageX, pageY) in words; a texel (u, v) of the page is read by depth:
//   0 = 4 bit : word at (pageX + u/4, pageY + v), nibble (u & 3) counted from
//               the low bits up, index into the CLUT row at (clutX + index, clutY)
//   1 = 8 bit : word at (pageX + u/2, pageY + v), byte (u & 1) low first, CLUT
//   2 = 15 bit: word at (pageX + u, pageY + v), no CLUT
// The word is then a 1-5-5-5 colour (bit 15 = STP, B at 10..14, G at 5..9, R at
// 0..4). The rule below is the retail twin's (native_twin.c NativeTwin_Texel
// and NativeTwin_DecodeVram), bit for bit:
//   - word 0x0000 is transparent: RGBA 0, 0, 0, 0
//   - any other word is opaque: each 5-bit channel shifted left by 3 (no bit
//     replication: 31 -> 248), alpha 255
//   - the STP bit does NOT reach the alpha: the twin keeps it out of the RGBA
//     (it only counts STP texels under semi-transparent triangles). 0x8000 is
//     therefore black with alpha 255, not transparent.
// Out-of-range coordinates wrap the way the twin's do: x at 1024 words, y at 512
// rows. Engine_VramDecode refuses such windows instead.
//
// Difference to the PSX shader (native_shaders.inc): it reads the same words and
// CLUT rows, but turns the STP bit into a separate weight (stpWeight, from the
// green channel of its LUT) for the semi-transparent passes, and its texel 0 is
// discarded in the shader, not stored as alpha 0.

#define ENGINE_VRAM_PAGE_TEXELS 256 // u and v of a primitive are 8 bit

// The 16-bit word a texel resolves to (the twin's NativeTwin_Texel).
internal u16 EngineVram_Word(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u, int v)
{
	const u32 row = ((u32)pageY + (u32)v) & (ENGINE_VRAM_HEIGHT - 1);
	const u32 clutRow = ((u32)clutY & (ENGINE_VRAM_HEIGHT - 1)) * ENGINE_VRAM_WIDTH;
	const u32 uu = (u32)u;

	if (depth == 0)
	{
		const u16 word = vram[(row * ENGINE_VRAM_WIDTH) + (((u32)pageX + (uu >> 2)) & (ENGINE_VRAM_WIDTH - 1))];
		const u32 index = ((u32)word >> ((uu & 3u) * 4u)) & 0xfu;

		return vram[clutRow + (((u32)clutX + index) & (ENGINE_VRAM_WIDTH - 1))];
	}
	if (depth == 1)
	{
		const u16 word = vram[(row * ENGINE_VRAM_WIDTH) + (((u32)pageX + (uu >> 1)) & (ENGINE_VRAM_WIDTH - 1))];
		const u32 index = ((u32)word >> ((uu & 1u) * 8u)) & 0xffu;

		return vram[clutRow + (((u32)clutX + index) & (ENGINE_VRAM_WIDTH - 1))];
	}
	return vram[(row * ENGINE_VRAM_WIDTH) + (((u32)pageX + uu) & (ENGINE_VRAM_WIDTH - 1))];
}

// 1-5-5-5 word to RGBA8 (the twin's expansion).
internal void EngineVram_Expand(u16 c, u8 rgba[4])
{
	if (c == 0u)
	{
		rgba[0] = 0;
		rgba[1] = 0;
		rgba[2] = 0;
		rgba[3] = 0;
		return;
	}
	rgba[0] = (u8)((c & 31u) << 3);
	rgba[1] = (u8)(((c >> 5) & 31u) << 3);
	rgba[2] = (u8)(((c >> 10) & 31u) << 3);
	rgba[3] = 255;
}

void Engine_VramTexel(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u, int v, u8 rgbaOut[4])
{
	if ((vram == NULL) || (rgbaOut == NULL))
	{
		return;
	}
	if ((depth < 0) || (depth > 2))
	{
		rgbaOut[0] = 0;
		rgbaOut[1] = 0;
		rgbaOut[2] = 0;
		rgbaOut[3] = 0;
		return;
	}
	EngineVram_Expand(EngineVram_Word(vram, pageX, pageY, depth, clutX, clutY, u, v), rgbaOut);
}

int Engine_VramDecode(const u16 *vram, int pageX, int pageY, int depth, int clutX, int clutY, int u0, int v0, int w, int h, u8 *rgbaOut)
{
	// Texels one VRAM word holds, and CLUT words a depth reads.
	static const int texelsPerWord[3] = {4, 2, 1};
	static const int clutWords[3] = {16, 256, 0};
	int x;
	int y;

	if ((vram == NULL) || (rgbaOut == NULL) || (w <= 0) || (h <= 0) || (depth < 0) || (depth > 2))
	{
		return 0;
	}
	if ((u0 < 0) || (v0 < 0) || (u0 > ENGINE_VRAM_PAGE_TEXELS - w) || (v0 > ENGINE_VRAM_PAGE_TEXELS - h))
	{
		return 0;
	}
	// The page words the window touches stay inside VRAM (no wrapping here).
	if ((pageX < 0) || (pageY < 0) || (pageX >= ENGINE_VRAM_WIDTH) || (pageY >= ENGINE_VRAM_HEIGHT))
	{
		return 0;
	}
	if ((pageX + ((u0 + w + texelsPerWord[depth] - 1) / texelsPerWord[depth]) > ENGINE_VRAM_WIDTH) || (pageY + v0 + h > ENGINE_VRAM_HEIGHT))
	{
		return 0;
	}
	// A 15-bit page has no CLUT, so its CLUT arguments are not looked at.
	if ((depth != 2) && ((clutX < 0) || (clutY < 0) || (clutY >= ENGINE_VRAM_HEIGHT) || (clutX + clutWords[depth] > ENGINE_VRAM_WIDTH)))
	{
		return 0;
	}

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			EngineVram_Expand(EngineVram_Word(vram, pageX, pageY, depth, clutX, clutY, u0 + x, v0 + y), &rgbaOut[(((size_t)y * (size_t)w) + (size_t)x) * 4u]);
		}
	}
	return 1;
}

// --------------------------------------------------------------------------
// SELFTEST

global_variable u16 g_engineVramSynthetic[ENGINE_VRAM_WIDTH * ENGINE_VRAM_HEIGHT];

// Layout of the synthetic VRAM (all other words are a deterministic pattern).
#define EVT_P4_X 512
#define EVT_P4_Y 0
#define EVT_P8_X 640
#define EVT_P8_Y 0
#define EVT_P15_X 768
#define EVT_P15_Y 256
#define EVT_C4_X 0
#define EVT_C4_Y 500
#define EVT_C8_X 256
#define EVT_C8_Y 501

internal void EngineVramTest_Build(u16 *vram)
{
	static const u16 special[8] = {0x0000, 0x8000, 0x7FFF, 0x001F, 0x03E0, 0x7C00, 0x1234, 0x9234};
	int x;
	int y;
	int i;

	for (y = 0; y < ENGINE_VRAM_HEIGHT; y++)
	{
		for (x = 0; x < ENGINE_VRAM_WIDTH; x++)
		{
			vram[(y * ENGINE_VRAM_WIDTH) + x] = (u16)(((u32)x * 37u) + ((u32)y * 101u) + ((u32)x * (u32)y) + (((u32)x ^ (u32)y) << 3));
		}
	}
	// CLUT entries: both CLUT rows start with the special colours (0x0000, STP
	// only, white, pure red / green / blue, a mid colour, STP + mid colour); the
	// rest keeps the pattern, so the 8-bit CLUT is full of mixed words.
	for (i = 0; i < 8; i++)
	{
		vram[(EVT_C4_Y * ENGINE_VRAM_WIDTH) + EVT_C4_X + i] = special[i];
		vram[(EVT_C8_Y * ENGINE_VRAM_WIDTH) + EVT_C8_X + i] = special[i];
	}
	// Hand-made texel words.
	vram[((EVT_P4_Y + 10) * ENGINE_VRAM_WIDTH) + EVT_P4_X] = 0x3210;     // 4 bit u0..u3 -> CLUT index 0,1,2,3
	vram[((EVT_P4_Y + 11) * ENGINE_VRAM_WIDTH) + EVT_P4_X] = 0x7550;     // 4 bit u0..u3 -> CLUT index 0,5,5,7
	vram[((EVT_P8_Y + 10) * ENGINE_VRAM_WIDTH) + EVT_P8_X] = 0x0604;     // 8 bit u0 -> index 4, u1 -> index 6
	vram[((EVT_P15_Y + 10) * ENGINE_VRAM_WIDTH) + EVT_P15_X] = 0x1234;   // 15 bit u0
	vram[((EVT_P15_Y + 10) * ENGINE_VRAM_WIDTH) + EVT_P15_X + 1] = 0x7FFF;
}

internal int EngineVramTest_Hand(const u16 *vram, const char *name, int depth, int px, int py, int cx, int cy, int u, int v, u8 r, u8 g, u8 b, u8 a)
{
	u8 got[4];

	Engine_VramTexel(vram, px, py, depth, cx, cy, u, v, got);
	if ((got[0] != r) || (got[1] != g) || (got[2] != b) || (got[3] != a))
	{
		printf("engine selftest: vram hand texel %s: got %u,%u,%u,%u expected %u,%u,%u,%u\n", name, got[0], got[1], got[2], got[3], r, g, b, a);
		return 1;
	}
	return 0;
}

// Runs the twin's whole-tile decoder on one tile and returns the RGBA8 of the
// 256 x 256 tile in a malloc'd buffer (NULL on failure).
internal u8 *EngineVramTest_Twin(const u16 *vram, int depth, int px, int py, int cx, int cy)
{
	struct NativeTwinSource *src = (struct NativeTwinSource *)calloc(1u, sizeof(*src));
	u8 *copy = NULL;

	if (src == NULL)
	{
		return NULL;
	}
	src->block = (u8 *)src; // only has to be non-NULL; never freed through it
	src->native.textureCount = 1;
	src->native.texture[0].width = NATIVE_TWIN_TILE_EDGE;
	src->native.texture[0].height = NATIVE_TWIN_TILE_EDGE;
	src->tileCount = 1;
	src->tile[0].depth = (u8)depth;
	src->tile[0].pageX = (u16)px;
	src->tile[0].pageY = (u16)py;
	src->tile[0].clutX = (u16)cx;
	src->tile[0].clutY = (u16)cy;
	if (NativeTwin_DecodeVram(vram, src))
	{
		const size_t bytes = (size_t)NATIVE_TWIN_TILE_EDGE * NATIVE_TWIN_TILE_EDGE * 4u;

		copy = (u8 *)malloc(bytes);
		if (copy != NULL)
		{
			memcpy(copy, src->native.texture[0].rgba, bytes);
		}
	}
	free(src->native.texture[0].rgba);
	free(src);
	return copy;
}

int Engine_SelfTestVram(int *checks)
{
	static const int depthPx[3] = {EVT_P4_X, EVT_P8_X, EVT_P15_X};
	static const int depthPy[3] = {EVT_P4_Y, EVT_P8_Y, EVT_P15_Y};
	static const int depthCx[3] = {EVT_C4_X, EVT_C8_X, 0};
	static const int depthCy[3] = {EVT_C4_Y, EVT_C8_Y, 0};
	static const char *const depthName[3] = {"4 bit", "8 bit", "15 bit"};
	const u16 *vram = g_engineVramSynthetic;
	u8 *window = (u8 *)malloc(256u * 256u * 4u);
	int failures = 0;
	int d;
	int x;
	int y;

	EngineVramTest_Build(g_engineVramSynthetic);
	if (window == NULL)
	{
		printf("engine selftest: vram out of memory\n");
		(*checks)++;
		return 1;
	}

	// (1) a 64 x 64 window equals the per-texel function, every depth.
	for (d = 0; d < 3; d++)
	{
		int bad = 0;

		(*checks)++;
		if (!Engine_VramDecode(vram, depthPx[d], depthPy[d], d, depthCx[d], depthCy[d], 5, 7, 64, 64, window))
		{
			printf("engine selftest: vram window %s refused\n", depthName[d]);
			failures++;
			continue;
		}
		for (y = 0; y < 64; y++)
		{
			for (x = 0; x < 64; x++)
			{
				u8 t[4];

				Engine_VramTexel(vram, depthPx[d], depthPy[d], d, depthCx[d], depthCy[d], 5 + x, 7 + y, t);
				bad += (memcmp(t, &window[((y * 64) + x) * 4], 4u) != 0);
			}
		}
		if (bad != 0)
		{
			printf("engine selftest: vram window %s differs from Engine_VramTexel in %d texels\n", depthName[d], bad);
			failures++;
		}
	}

	// (2) bit-identity to the twin: the whole 256 x 256 tile (65536 texels per
	// depth) through NativeTwin_DecodeVram.
	for (d = 0; d < 3; d++)
	{
		u8 *twin = EngineVramTest_Twin(vram, d, depthPx[d], depthPy[d], depthCx[d], depthCy[d]);

		(*checks)++;
		if (twin == NULL)
		{
			printf("engine selftest: vram twin tile %s not decoded\n", depthName[d]);
			failures++;
			continue;
		}
		if (!Engine_VramDecode(vram, depthPx[d], depthPy[d], d, depthCx[d], depthCy[d], 0, 0, 256, 256, window))
		{
			printf("engine selftest: vram full tile %s refused\n", depthName[d]);
			failures++;
		}
		else if (memcmp(twin, window, 256u * 256u * 4u) != 0)
		{
			printf("engine selftest: vram tile %s differs from the retail twin\n", depthName[d]);
			failures++;
		}
		free(twin);
	}
	// The twin wraps at the VRAM edges; Engine_VramTexel must too (x at 1024, y at 512).
	for (d = 0; d < 3; d++)
	{
		const int wrapX = (d == 2) ? 900 : 1000;
		const int wrapY = 300;
		const int wrapCx = (d == 1) ? 800 : 1000;
		u8 *twin = EngineVramTest_Twin(vram, d, wrapX, wrapY, wrapCx, 499);
		int bad = 0;

		(*checks)++;
		if (twin == NULL)
		{
			printf("engine selftest: vram twin wrap tile %s not decoded\n", depthName[d]);
			failures++;
			continue;
		}
		for (y = 0; y < 256; y++)
		{
			for (x = 0; x < 256; x++)
			{
				u8 t[4];

				Engine_VramTexel(vram, wrapX, wrapY, d, wrapCx, 499, x, y, t);
				bad += (memcmp(t, &twin[((y * 256) + x) * 4], 4u) != 0);
			}
		}
		if (bad != 0)
		{
			printf("engine selftest: vram wrapped %s differs from the retail twin in %d texels\n", depthName[d], bad);
			failures++;
		}
		free(twin);
	}

	// (3) hand-computed texels. CLUT: 0 = 0x0000, 1 = 0x8000, 2 = 0x7FFF,
	// 3 = 0x001F, 4 = 0x03E0, 5 = 0x7C00, 6 = 0x1234, 7 = 0x9234.
	// Channel = 5 bits << 3 (31 -> 248).
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "4 bit 0x0000", 0, EVT_P4_X, EVT_P4_Y, EVT_C4_X, EVT_C4_Y, 0, 10, 0, 0, 0, 0);               // transparent
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "4 bit 0x8000", 0, EVT_P4_X, EVT_P4_Y, EVT_C4_X, EVT_C4_Y, 1, 10, 0, 0, 0, 255);             // black, STP not in alpha
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "4 bit 0x7FFF", 0, EVT_P4_X, EVT_P4_Y, EVT_C4_X, EVT_C4_Y, 2, 10, 248, 248, 248, 255);       // white
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "4 bit red", 0, EVT_P4_X, EVT_P4_Y, EVT_C4_X, EVT_C4_Y, 3, 10, 248, 0, 0, 255);              // 0x001F
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "8 bit green", 1, EVT_P8_X, EVT_P8_Y, EVT_C8_X, EVT_C8_Y, 0, 10, 0, 248, 0, 255);           // 0x03E0
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "8 bit mid", 1, EVT_P8_X, EVT_P8_Y, EVT_C8_X, EVT_C8_Y, 1, 10, 160, 136, 32, 255);          // 0x1234: R 20, G 17, B 4
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "15 bit mid", 2, EVT_P15_X, EVT_P15_Y, 0, 0, 0, 10, 160, 136, 32, 255);                     // 0x1234
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "15 bit white", 2, EVT_P15_X, EVT_P15_Y, 0, 0, 1, 10, 248, 248, 248, 255);                  // 0x7FFF
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "4 bit blue", 0, EVT_P4_X, EVT_P4_Y, EVT_C4_X, EVT_C4_Y, 1, 11, 0, 0, 248, 255);            // 0x7C00
	(*checks)++;
	failures += EngineVramTest_Hand(vram, "4 bit STP colour", 0, EVT_P4_X, EVT_P4_Y, EVT_C4_X, EVT_C4_Y, 3, 11, 160, 136, 32, 255);  // 0x9234

	// (4) refusals.
	{
		struct Refusal
		{
			const u16 *vram;
			int px;
			int py;
			int depth;
			int cx;
			int cy;
			int u0;
			int v0;
			int w;
			int h;
			int useOut;
			const char *what;
		};
		const struct Refusal refusal[] = {
			{NULL, 512, 0, 0, 0, 500, 0, 0, 8, 8, 1, "NULL vram"},
			{g_engineVramSynthetic, 512, 0, 0, 0, 500, 0, 0, 8, 8, 0, "NULL output"},
			{g_engineVramSynthetic, 512, 0, 0, 0, 500, 0, 0, 0, 8, 1, "zero width"},
			{g_engineVramSynthetic, 512, 0, 0, 0, 500, 0, 0, 8, -1, 1, "negative height"},
			{g_engineVramSynthetic, 512, 0, 3, 0, 500, 0, 0, 8, 8, 1, "depth 3"},
			{g_engineVramSynthetic, 512, 0, -1, 0, 500, 0, 0, 8, 8, 1, "depth -1"},
			{g_engineVramSynthetic, 1024, 0, 0, 0, 500, 0, 0, 8, 8, 1, "page x 1024"},
			{g_engineVramSynthetic, 512, 512, 0, 0, 500, 0, 0, 8, 8, 1, "page y 512"},
			{g_engineVramSynthetic, -1, 0, 0, 0, 500, 0, 0, 8, 8, 1, "page x -1"},
			{g_engineVramSynthetic, 512, 0, 0, 1010, 500, 0, 0, 8, 8, 1, "4-bit CLUT past the right edge"},
			{g_engineVramSynthetic, 512, 0, 1, 800, 500, 0, 0, 8, 8, 1, "8-bit CLUT past the right edge"},
			{g_engineVramSynthetic, 512, 0, 0, 0, 512, 0, 0, 8, 8, 1, "CLUT y 512"},
			{g_engineVramSynthetic, 512, 0, 0, 0, 500, 250, 0, 8, 8, 1, "window past u 255"},
			{g_engineVramSynthetic, 512, 0, 0, 0, 500, 0, -1, 8, 8, 1, "negative v0"},
			{g_engineVramSynthetic, 512, 300, 0, 0, 500, 0, 0, 8, 256, 1, "window past the VRAM bottom"},
			{g_engineVramSynthetic, 1000, 0, 2, 0, 0, 0, 0, 64, 8, 1, "15-bit window past the VRAM right edge"},
		};
		size_t r;

		for (r = 0; r < sizeof(refusal) / sizeof(refusal[0]); r++)
		{
			(*checks)++;
			if (Engine_VramDecode(refusal[r].vram, refusal[r].px, refusal[r].py, refusal[r].depth, refusal[r].cx, refusal[r].cy, refusal[r].u0,
			                      refusal[r].v0, refusal[r].w, refusal[r].h, refusal[r].useOut ? window : NULL))
			{
				printf("engine selftest: vram accepted bad arguments (%s)\n", refusal[r].what);
				failures++;
			}
		}
		// A 15-bit page ignores its CLUT arguments.
		(*checks)++;
		if (!Engine_VramDecode(vram, EVT_P15_X, EVT_P15_Y, 2, -5, 9999, 0, 0, 8, 8, window))
		{
			printf("engine selftest: vram 15-bit window refused for unused CLUT arguments\n");
			failures++;
		}
	}

	free(window);
	return failures;
}
