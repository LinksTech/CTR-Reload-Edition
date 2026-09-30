// FEASIBILITY PROOF, THIRD STEP: WHAT A TILE SWITCH COSTS.
//
// NOT A FEATURE. The previous step showed that an RGBA host texture gets
// into the picture via DR_PSYX_TEX, with real alpha, without one byte of VRAM. It
// also showed the limit: GrVertex carries u and v as ONE byte each, so a
// quad sees at most 256 x 256 texels of the bound texture.
//
// From that followed the assumption that an atlas would have to be split into 256x256 tiles and
// one packet written per tile. This file measures what that costs - and
// puts next to it the second path, which gets by without tile switches.
//
// TWO MODES, THE SAME EIGHT IMAGES:
//
//   tile mode     (--psyx-test N)
//       Eight separate 256x256 textures. One DR_PSYX_TEX before each quad,
//       i.e. a different texture ID per quad - and with it a split
//       and a draw call of its own.
//
//   window mode   (--psyx-test N --psyx-uv-origin)
//       ONE texture of 1024 x 512, from which every quad reads its own
//       256x256 window. The origin travels in the CLUT value of the primitive,
//       which the 32-bit path does not use otherwise. ONE packet for all
//       quads, one texture, one split.
//
// Everything hangs on g_cfg_psyxTest. Without the switch no texture is created,
// no packet written and no primitive added.

#include <common.h>

#include <platform/native_gfx.h>
#include <platform/native_log.h>
#include <psx/libgpu.h>

// --psyx-test N: how many quads are drawn. 0 is off.
int g_cfg_psyxTest = 0;

// --psyx-uv-origin: window mode instead of tile mode.
int g_cfg_psyxUvOrigin = 0;

// THE TWO HUNDRED AND FIFTY-SIX, and why it is the limit.
//
// GrVertex.u and .v are one byte each (native_renderer_types.h:57), the
// 32-bit shader computes with them directly (native_shaders.inc:381). So a
// quad can only name the texels 0..255. That holds in BOTH modes -
// the window mode does not lift the limit, it moves it.
#define PSYX_TILE 256

// The atlas: four tiles side by side, two on top of each other.
//
// 1024 x 512 and not larger, and that is not convenience. The
// window origin travels in the CLUT value, and getClut puts x/16 into six bits and y
// above them (libgpu.h:208). GrVertex.clut is an s16, so y = 511 is the
// end: (511 << 6) | 63 is 32,767, one step more and the value turns
// negative. x reaches up to 1008 in steps of sixteen. So 1024 x 512 is the
// largest atlas this encoding reaches completely.
#define PSYX_ATLAS_W   1024
#define PSYX_ATLAS_H   512
#define PSYX_TILES_X   (PSYX_ATLAS_W / PSYX_TILE)
#define PSYX_TILES_Y   (PSYX_ATLAS_H / PSYX_TILE)
#define PSYX_TILE_MAX  (PSYX_TILES_X * PSYX_TILES_Y)

// At most this many quads per frame. Beyond that the eight images
// repeat - for the cost curve what counts is the number of quads, not the
// number of different images.
#define PSYX_QUADS_MAX 64

// The grid on the screen, in points of the reference space 512 x 216.
// Sixteen columns of 32 points are exactly the 512 of the reference width.
#define PSYX_GRID_COLS 16
#define PSYX_CELL      32
#define PSYX_GRID_X    0
#define PSYX_GRID_Y    40

global_variable u32 *s_psyxAtlas = NULL;
global_variable TextureID s_psyxAtlasTexture = (TextureID)-1;
global_variable TextureID s_psyxTileTexture[PSYX_TILE_MAX];
global_variable int s_psyxSaid = 0;

internal u32 NativePsyXTest_Rgba(int r, int g, int b, int a)
{
	return (u32)(r & 0xff) | ((u32)(g & 0xff) << 8) | ((u32)(b & 0xff) << 16) | ((u32)(a & 0xff) << 24);
}

// Eight easily distinguishable base colours, one per tile.
global_variable const u8 s_psyxTileHue[PSYX_TILE_MAX][3] = {
    {200, 60, 60},   // 0 red
    {200, 130, 50},  // 1 orange
    {190, 190, 60},  // 2 yellow
    {70, 180, 70},   // 3 green
    {60, 170, 190},  // 4 turquoise
    {70, 90, 200},   // 5 blue
    {160, 70, 190},  // 6 violet
    {200, 200, 200}, // 7 white
};

// ONE TILE, UNAMBIGUOUSLY MARKED.
//
// The tile number is in it twice, so that a slip cannot pass as
// chance:
//
//   base colour    one per tile, from the table above.
//   three dots     the number in binary, 0..7. Filled means one.
//   corner notch   a white square at the TOP LEFT and nowhere else. Every
//                  rotation and every mirroring shows up with it.
//   frame          two texels of black all round, so that the tile border is
//                  visible in the screenshot and an offset of one texel shows up.
internal void NativePsyXTest_PaintTile(int tile, int ox, int oy)
{
	const u8 *hue = s_psyxTileHue[tile];

	for (int y = 0; y < PSYX_TILE; y++)
	{
		for (int x = 0; x < PSYX_TILE; x++)
		{
			int r = hue[0];
			int g = hue[1];
			int b = hue[2];
			int a = 255;

			// checkerboard of 32 texels, so that the scale stays readable
			if ((((x / 32) + (y / 32)) & 1) != 0)
			{
				r = (r * 5) / 8;
				g = (g * 5) / 8;
				b = (b * 5) / 8;
			}

			// frame
			if ((x < 2) || (y < 2) || (x >= PSYX_TILE - 2) || (y >= PSYX_TILE - 2))
			{
				r = g = b = 0;
			}

			// corner notch top left
			if ((x >= 8) && (x < 40) && (y >= 8) && (y < 40))
			{
				r = g = b = 255;
			}

			// Three dots, bit 2 on the left. Middle of the tile, 40 texels each.
			for (int bit = 0; bit < 3; bit++)
			{
				const int px = 56 + (bit * 48);
				const int py = 108;

				if ((x >= px) && (x < px + 40) && (y >= py) && (y < py + 40))
				{
					const int on = ((tile >> (2 - bit)) & 1) != 0;
					r = g = b = on ? 0 : 255;
					if (on)
					{
						b = 0;
					}
				}
			}

			// A semi-transparent stripe at the bottom, so that the previous step stays
			// covered too: alpha must arrive here as well.
			if (y >= PSYX_TILE - 48)
			{
				a = 128;
			}

			s_psyxAtlas[((oy + y) * PSYX_ATLAS_W) + (ox + x)] = NativePsyXTest_Rgba(r, g, b, a);
		}
	}
}

// The atlas once, then the eight single textures as cut-outs from it.
//
// BOTH MODES SHOW THE SAME PICTURE. That is the whole purpose of this setup:
// if the screenshot differs, it is due to the addressing path and nothing else.
internal int NativePsyXTest_Prepare(void)
{
	if (s_psyxAtlasTexture != (TextureID)-1)
	{
		return 1;
	}

	s_psyxAtlas = (u32 *)malloc((size_t)PSYX_ATLAS_W * PSYX_ATLAS_H * sizeof(u32));

	if (s_psyxAtlas == NULL)
	{
		return 0;
	}

	for (int ty = 0; ty < PSYX_TILES_Y; ty++)
	{
		for (int tx = 0; tx < PSYX_TILES_X; tx++)
		{
			NativePsyXTest_PaintTile((ty * PSYX_TILES_X) + tx, tx * PSYX_TILE, ty * PSYX_TILE);
		}
	}

	const NativeGfxTextureDesc atlasDesc = {
	    .width = PSYX_ATLAS_W,
	    .height = PSYX_ATLAS_H,
	    .format = NATIVE_GFX_TEXFMT_RGBA8,
	    .filter = NATIVE_GFX_FILTER_NEAREST,
	    .wrap = NATIVE_GFX_WRAP_CLAMP,
	    .pixels = s_psyxAtlas,
	};

	s_psyxAtlasTexture = NativeGfx_CreateTexture(&atlasDesc);

	// The eight single tiles. rowPixels tells NativeGfx_UpdateTexture how wide
	// the source is from which the cut-out is taken - the atlas does not have to
	// be unpacked for that (native_gfx.h:117).
	for (int tile = 0; tile < PSYX_TILE_MAX; tile++)
	{
		const int ox = (tile % PSYX_TILES_X) * PSYX_TILE;
		const int oy = (tile / PSYX_TILES_X) * PSYX_TILE;

		const NativeGfxTextureDesc tileDesc = {
		    .width = PSYX_TILE,
		    .height = PSYX_TILE,
		    .format = NATIVE_GFX_TEXFMT_RGBA8,
		    .filter = NATIVE_GFX_FILTER_NEAREST,
		    .wrap = NATIVE_GFX_WRAP_CLAMP,
		    .pixels = NULL,
		};

		s_psyxTileTexture[tile] = NativeGfx_CreateTexture(&tileDesc);

		NativeGfx_UpdateTexture(s_psyxTileTexture[tile], 0, 0, PSYX_TILE, PSYX_TILE, NATIVE_GFX_TEXFMT_RGBA8, &s_psyxAtlas[(oy * PSYX_ATLAS_W) + ox],
		                        PSYX_ATLAS_W);
	}

	Platform_Log("[CTR PsyX] atlas %dx%d RGBA (%d KB), ID %u; plus %d single tiles %dx%d\n", PSYX_ATLAS_W, PSYX_ATLAS_H,
	             (PSYX_ATLAS_W * PSYX_ATLAS_H * 4) / 1024, (unsigned)s_psyxAtlasTexture, PSYX_TILE_MAX, PSYX_TILE, PSYX_TILE);

	return 1;
}

// One quad. In tile mode it carries its own texture, in window mode
// the window origin in the CLUT value.
internal void NativePsyXTest_Quad(u32 *ot, int slot, int tile, int windowMode)
{
	POLY_FT4 *quad;

	GetPrimMem(quad);

	if (quad == NULL)
	{
		return;
	}

	const int cx = PSYX_GRID_X + ((slot % PSYX_GRID_COLS) * PSYX_CELL);
	const int cy = PSYX_GRID_Y + ((slot / PSYX_GRID_COLS) * PSYX_CELL);

	setPolyFT4(quad);

	// 128 is the neutral colour, not 255: the vertex shader multiplies by
	// a_texcoord.z, and MakeTexcoordQuad sets that to a fixed 2
	// (native_gpu.c:1324). Measured in the previous step.
	setRGB0(quad, 128, 128, 128);
	setShadeTex(quad, 0);
	setSemiTrans(quad, 1);

	setXY4(quad, cx, cy, cx + PSYX_CELL, cy, cx, cy + PSYX_CELL, cx + PSYX_CELL, cy + PSYX_CELL);
	setUV4(quad, 0, 0, PSYX_TILE - 1, 0, 0, PSYX_TILE - 1, PSYX_TILE - 1, PSYX_TILE - 1);

	// Page 8 is VRAM x 512..575 and cannot intersect the draw area x 0..511
	// - otherwise NativeGpu_TPageOverlapsActiveDrawPage would trigger a
	// feedback. The tpage is not needed for the texture.
	setTPage(quad, 2, 0, 512, 0);

	if (windowMode)
	{
		// THE WINDOW ORIGIN, in the encoding getClut has anyway:
		// x/16 in the lower six bits, y above them. The vertex shader reads
		// it back exactly that way (native_shaders.inc, GTE_VERTEX_SHADER).
		setClut(quad, (tile % PSYX_TILES_X) * PSYX_TILE, (tile / PSYX_TILES_X) * PSYX_TILE);
	}
	else
	{
		setClut(quad, 0, 0);
	}

	AddPrimitive(quad, ot);
}

void NativePsyXTest_Draw(void)
{
	if (g_cfg_psyxTest <= 0)
	{
		return;
	}

	struct GameTracker *gGT = sdata->gGT;

	if ((gGT == NULL) || (gGT->backBuffer == NULL))
	{
		return;
	}

	if (!NativePsyXTest_Prepare())
	{
		return;
	}

	const int windowMode = (g_cfg_psyxUvOrigin != 0);
	int quads = g_cfg_psyxTest;

	if (quads > PSYX_QUADS_MAX)
	{
		quads = PSYX_QUADS_MAX;
	}

	u32 *ot = gGT->backBuffer->otMem.uiOT;

	// ENTRIES MUST BE ADDED IN REVERSE ORDER.
	//
	// AddPrimitive links at the front (prim.c:20-27): added last means
	// processed first. What is to be executed is
	//
	//     set texture -> quad -> [set texture -> quad] ... -> back
	//
	// so it is added from back to front: first the reset packet,
	// then the quads in reverse order, each with its packet added AFTER it
	// and thus executed before it.
	{
		DR_PSYX_TEX *restore;

		GetPrimMem(restore);

		if (restore == NULL)
		{
			return;
		}

		SetPsyXTexture(restore, 0, 0, 0);
		AddPrimitive(restore, ot);
	}

	if (windowMode)
	{
		// ONE packet for all quads. First add all quads (they
		// are then drawn in reverse order, which does not matter here -
		// they do not overlap), then ONE packet before them.
		for (int i = 0; i < quads; i++)
		{
			NativePsyXTest_Quad(ot, i, i % PSYX_TILE_MAX, 1);
		}

		DR_PSYX_TEX *select;

		GetPrimMem(select);

		if (select != NULL)
		{
			SetPsyXTexture(select, (u32)s_psyxAtlasTexture, PSYX_ATLAS_W, PSYX_ATLAS_H);
			AddPrimitive(select, ot);
		}
	}
	else
	{
		// ONE PACKET PER QUAD. Added backwards, so that every packet is executed before
		// its quad.
		for (int i = quads - 1; i >= 0; i--)
		{
			const int tile = i % PSYX_TILE_MAX;

			NativePsyXTest_Quad(ot, i, tile, 0);

			DR_PSYX_TEX *select;

			GetPrimMem(select);

			if (select == NULL)
			{
				break;
			}

			SetPsyXTexture(select, (u32)s_psyxTileTexture[tile], PSYX_TILE, PSYX_TILE);
			AddPrimitive(select, ot);
		}
	}

	if (!s_psyxSaid)
	{
		s_psyxSaid = 1;
		Platform_Log("[CTR PsyX] %s, %d quad(s), %d packet(s), %d primitives per frame\n", windowMode ? "window mode" : "tile mode", quads,
		             windowMode ? 2 : (quads + 1), windowMode ? (quads + 2) : (2 * quads + 1));
	}
}
