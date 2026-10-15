#include <platform/engine.h>
#include <platform/native_tex.h>

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
// ENGINE TEXTURES - see include/platform/engine.h. The levels, the edge rule
// and the upload are the native texture manager's (platform/native_tex.c);
// this part only chooses the sampling, which there follows --native-filter
// (default nearest, and anisotropy only with linear) and here follows the
// texture's own wish.

// The sampling of an engine texture. Pure (the device's anisotropy comes in as
// arguments), so the selftest checks it without a device.
internal NativeGfxSampling EngTex_Sampling(const EngineTextureDesc *desc, int levels, int anisotropyEnabled, float deviceLimit)
{
	NativeGfxSampling sampling;
	const int pixelated = (desc->filter == ENGINE_TEXTURE_PIXELATED);

	memset(&sampling, 0, sizeof(sampling));
	sampling.magFilter = pixelated ? NATIVE_GFX_FILTER_NEAREST : NATIVE_GFX_FILTER_LINEAR;
	sampling.minFilter = sampling.magFilter;
	sampling.mipMode = (levels <= 1) ? NATIVE_GFX_MIP_NONE : (pixelated ? NATIVE_GFX_MIP_NEAREST : NATIVE_GFX_MIP_LINEAR);
	sampling.wrapU = (NativeGfxWrap)desc->wrapU;
	sampling.wrapV = (NativeGfxWrap)desc->wrapV;

	// The wish, then the device limit and 16 (the device clamps again; a value of
	// 0 or 1 is off).
	if (!pixelated && anisotropyEnabled && (desc->maxAnisotropy != 1))
	{
		int granted = (int)deviceLimit;

		if (granted > 16)
		{
			granted = 16;
		}
		if ((desc->maxAnisotropy > 1) && (desc->maxAnisotropy < granted))
		{
			granted = desc->maxAnisotropy;
		}
		sampling.anisotropy = (granted > 1) ? granted : 0;
	}

	return sampling;
}

u32 Engine_TextureCreate(const EngineTextureDesc *desc)
{
	NativeTexDesc texDesc;
	NativeTexResult result = NATIVE_TEX_OK;
	NativeGfxTextureLimits limits;
	NativeGfxSampling sampling;
	TextureID texture;
	int levels;

	// Without the engine nothing of it runs: no texture, no log line.
	if (!g_cfg_engine)
	{
		return NATIVE_GFX_INVALID;
	}
	if ((desc == NULL) || (desc->rgba == NULL) || (desc->wrapU < NATIVE_GFX_WRAP_REPEAT) || (desc->wrapU > NATIVE_GFX_WRAP_MIRROR) ||
	    (desc->wrapV < NATIVE_GFX_WRAP_REPEAT) || (desc->wrapV > NATIVE_GFX_WRAP_MIRROR) ||
	    ((desc->filter != ENGINE_TEXTURE_FILTERED) && (desc->filter != ENGINE_TEXTURE_PIXELATED)))
	{
		Platform_Log("[CTR Engine] texture '%s' refused: no pixels or a bad wrap or filter\n",
		             ((desc != NULL) && (desc->name != NULL)) ? desc->name : "(unnamed)");
		return NATIVE_GFX_INVALID;
	}

	memset(&texDesc, 0, sizeof(texDesc));
	texDesc.width = desc->width;
	texDesc.height = desc->height;
	texDesc.rgba = desc->rgba;
	texDesc.flags = (desc->linearData ? NATIVE_TEX_FLAG_LINEAR_DATA : 0u) | (desc->oneLevel ? NATIVE_TEX_FLAG_ONE_LEVEL : 0u);
	texDesc.wrapU = (NativeGfxWrap)desc->wrapU;
	texDesc.wrapV = (NativeGfxWrap)desc->wrapV;
	texDesc.name = desc->name;
	texture = NativeTex_Create(&texDesc, &result);
	if (texture == NATIVE_GFX_INVALID)
	{
		// The manager has logged the edge or device reason already (silent only when locked).
		Platform_Log("[CTR Engine] texture '%s' %dx%d refused: %s\n", (desc->name != NULL) ? desc->name : "(unnamed)", desc->width, desc->height,
		             NativeTex_ResultName(result));
		return NATIVE_GFX_INVALID;
	}

	levels = desc->oneLevel ? 1 : NativeTex_LevelCount(desc->width, desc->height);
	memset(&limits, 0, sizeof(limits));
	NativeGfx_TextureLimits(&limits);
	sampling = EngTex_Sampling(desc, levels, limits.anisotropyEnabled, limits.maxSamplerAnisotropy);
	NativeGfx_SetTextureSampling(texture, &sampling);

	Platform_Log("[CTR Engine] texture '%s' %dx%d: %d level(s), %s%s, anisotropy %d (device %s, limit %.1f)\n",
	             (desc->name != NULL) ? desc->name : "(unnamed)", desc->width, desc->height, levels,
	             (desc->filter == ENGINE_TEXTURE_PIXELATED) ? "pixelated" : ((levels > 1) ? "trilinear" : "linear"),
	             desc->linearData ? " (linear data)" : "", sampling.anisotropy, limits.anisotropyEnabled ? "on" : "off",
	             (double)limits.maxSamplerAnisotropy);

	return (u32)texture;
}

void Engine_TextureDestroy(u32 texture)
{
	if (texture != NATIVE_GFX_INVALID)
	{
		NativeTex_Destroy((TextureID)texture);
	}
}

void Engine_TextureMaterial(EngineMaterial *m, u32 texture)
{
	if (m == NULL)
	{
		return;
	}
	m->texSource = ENGINE_TEX_NATIVE;
	m->nativeTexture = texture;
	if (NativeTex_IsSrgb((TextureID)texture))
	{
		m->flags |= ENGINE_MATERIAL_SRGB;
	}
	else
	{
		m->flags &= (u16)~ENGINE_MATERIAL_SRGB;
	}
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

// The engine textures without a device: the sampling of every wish, and the
// levels the manager computes for them (a box of 2x2, 2x1 once an edge is 1,
// in linear light for colour, alpha as weight).
internal int EngTexTest_Run(int *checks)
{
	struct SamplingCase
	{
		int filter;
		int maxAnisotropy;
		int levels;
		int enabled;
		float limit;
		NativeGfxFilter mag;
		NativeGfxMipMode mip;
		int anisotropy;
	};
	static const struct SamplingCase cases[] = {
		{ENGINE_TEXTURE_FILTERED, 0, 12, 1, 16.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_LINEAR, 16},   // the default: device limit
		{ENGINE_TEXTURE_FILTERED, 0, 12, 1, 8.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_LINEAR, 8},     // a smaller device limit
		{ENGINE_TEXTURE_FILTERED, 0, 12, 1, 32.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_LINEAR, 16},   // never above 16
		{ENGINE_TEXTURE_FILTERED, 4, 12, 1, 16.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_LINEAR, 4},    // the wish below the limit
		{ENGINE_TEXTURE_FILTERED, 32, 12, 1, 16.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_LINEAR, 16},  // the wish above it
		{ENGINE_TEXTURE_FILTERED, 1, 12, 1, 16.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_LINEAR, 0},    // 1 = off
		{ENGINE_TEXTURE_FILTERED, 0, 12, 0, 0.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_LINEAR, 0},     // a device without it
		{ENGINE_TEXTURE_FILTERED, 0, 1, 1, 16.0f, NATIVE_GFX_FILTER_LINEAR, NATIVE_GFX_MIP_NONE, 16},      // one level
		{ENGINE_TEXTURE_PIXELATED, 0, 12, 1, 16.0f, NATIVE_GFX_FILTER_NEAREST, NATIVE_GFX_MIP_NEAREST, 0}, // pixelated
		{ENGINE_TEXTURE_PIXELATED, 0, 1, 1, 16.0f, NATIVE_GFX_FILTER_NEAREST, NATIVE_GFX_MIP_NONE, 0},
	};
	int failures = 0;
	size_t c;

	for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
	{
		EngineTextureDesc desc;
		NativeGfxSampling s;

		memset(&desc, 0, sizeof(desc));
		desc.filter = cases[c].filter;
		desc.maxAnisotropy = cases[c].maxAnisotropy;
		desc.wrapU = NATIVE_GFX_WRAP_MIRROR;
		desc.wrapV = NATIVE_GFX_WRAP_CLAMP;
		s = EngTex_Sampling(&desc, cases[c].levels, cases[c].enabled, cases[c].limit);
		(*checks)++;
		if ((s.magFilter != cases[c].mag) || (s.minFilter != cases[c].mag) || (s.mipMode != cases[c].mip) || (s.anisotropy != cases[c].anisotropy) ||
		    (s.wrapU != NATIVE_GFX_WRAP_MIRROR) || (s.wrapV != NATIVE_GFX_WRAP_CLAMP))
		{
			printf("engine selftest: texture sampling case %d: mag %d min %d mip %d anisotropy %d wrap %d/%d, expected %d %d %d %d 2/1\n", (int)c,
			       (int)s.magFilter, (int)s.minFilter, (int)s.mipMode, s.anisotropy, (int)s.wrapU, (int)s.wrapV, (int)cases[c].mag, (int)cases[c].mag,
			       (int)cases[c].mip, cases[c].anisotropy);
			failures++;
		}
	}

	// The chain of a 32 x 8 linear-data texture: 6 levels (32x8, 16x4, 8x2, 4x1,
	// 2x1, 1x1), every texel the rounded box of the texels under it.
	{
		enum
		{
			W = 32,
			H = 8
		};
		u8 level0[W * H * 4];
		u8 *chain;
		const int count = NativeTex_LevelCount(W, H);
		const u8 *src = level0;
		int sw = W;
		int sh = H;
		int bad = 0;
		int level;
		int i;

		for (i = 0; i < W * H; i++)
		{
			level0[i * 4 + 0] = (u8)((i * 37) & 255);
			level0[i * 4 + 1] = (u8)((i * 91 + 13) & 255);
			level0[i * 4 + 2] = (u8)((i * i) & 255);
			level0[i * 4 + 3] = 255;
		}
		(*checks)++;
		chain = (u8 *)malloc(NativeTex_ChainBytes(W, H, 6));
		if ((count != 6) || (chain == NULL))
		{
			printf("engine selftest: texture chain of 32x8: %d levels (expected 6) or no memory\n", count);
			free(chain);
			return failures + 1;
		}
		NativeTex_BuildLevels(level0, W, H, count, 1, chain);
		bad += (memcmp(chain, level0, sizeof(level0)) != 0);
		src = chain;
		for (level = 1; level < count; level++)
		{
			const int dw = (sw > 1) ? (sw / 2) : 1;
			const int dh = (sh > 1) ? (sh / 2) : 1;
			const int cols = (sw > 1) ? 2 : 1;
			const int rows = (sh > 1) ? 2 : 1;
			const u8 *dst = src + (size_t)sw * (size_t)sh * 4u;
			int x;
			int y;
			int ch;

			for (y = 0; y < dh; y++)
			{
				for (x = 0; x < dw; x++)
				{
					for (ch = 0; ch < 4; ch++)
					{
						int sum = 0;
						int dy;
						int dx;

						for (dy = 0; dy < rows; dy++)
						{
							for (dx = 0; dx < cols; dx++)
							{
								sum += src[((y * rows + dy) * sw + (x * cols + dx)) * 4 + ch];
							}
						}
						bad += (dst[(y * dw + x) * 4 + ch] != (u8)((sum + (rows * cols) / 2) / (rows * cols)));
					}
				}
			}
			src = dst;
			sw = dw;
			sh = dh;
		}
		if (bad != 0)
		{
			printf("engine selftest: texture chain of 32x8 differs from the box in %d values\n", bad);
			failures++;
		}
		free(chain);
	}

	// Colour is averaged in linear light: two black and two white sRGB texels give
	// the sRGB of half the light (about 188), not the byte mean 128. And alpha is a
	// weight: one opaque red among three transparent greens gives red, alpha 64.
	{
		static const u8 blackWhite[16] = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
		static const u8 oneOpaque[16] = {255, 0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0};
		u8 chain[(4 + 1) * 4];

		(*checks)++;
		NativeTex_BuildLevels(blackWhite, 2, 2, 2, 0, chain);
		if ((chain[16] < 186) || (chain[16] > 189) || (chain[16] != chain[17]) || (chain[16] != chain[18]) || (chain[19] != 255))
		{
			printf("engine selftest: texture level of black and white is %u,%u,%u,%u, expected about 188 in linear light\n", chain[16], chain[17],
			       chain[18], chain[19]);
			failures++;
		}
		(*checks)++;
		NativeTex_BuildLevels(oneOpaque, 2, 2, 2, 0, chain);
		if ((chain[16] != 255) || (chain[17] != 0) || (chain[18] != 0) || (chain[19] != 64))
		{
			printf("engine selftest: texture level of one opaque texel is %u,%u,%u,%u, expected 255,0,0,64\n", chain[16], chain[17], chain[18],
			       chain[19]);
			failures++;
		}
	}

	return failures;
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
	return failures + EngTexTest_Run(checks);
}
