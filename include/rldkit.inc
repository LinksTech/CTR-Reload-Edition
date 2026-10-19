// rldkit.inc - the Creator Kit: the retail kart of a driver, made hi-res from
// the player's own game data
//
// AN INCLUDE, NOT A MODULE, header-only (static functions), like
// include/rldhires.inc. rldpack (kit-extract, kit-standin, selftest-kit) runs it;
// the game runs the same code for the in-game kart of a figure-only driver
// (CHRG parts bit 1, platform/engine_hires_char.c THE GAME'S KART). u8, u16,
// u32 and u64 come from the including file. Nothing here prints, reads or
// writes a file: the caller hands over the bytes of the BIGFILE entries and
// gets meshes and an atlas back.
//
// RIGHTS. Everything this file returns is made from the player's game data at
// run time and belongs only on the player's computer, in a folder the player
// chose. This file holds no byte, picture or measurement of the game data
// except the few placement numbers of the game's own code it names (the wheel
// points of game/DrawTires.c, the defaults of the steering wheel measured in
// tools/rldpack_dummy.inc); its self-test runs on code-made stand-ins.
//
// THE INPUT (BIGFILE.BIG entries, include/namespace_Load.h):
//   242 + n  the racer model HI of driver n (BI_RACERMODELHI), n = 0..14; all 15
//            are read, the kart of the donor is told from its figure with the
//            help of the other 14 (THE KART, below). 257 (N. Oxide) has no kart.
//   258      shared.vrm (BI_SHAREDMPKVRM): every driver texture and the wheel
//            sprites, in VRAM at boot (game/MAIN/MainMain.c, LOAD_VramFile 0x102)
//   259      the shared pack: its icon group 0 "tireAnim" are the 17 wheel
//            sprites (VehBirth_TireSprites, game/Vehicle/VehBirth.c)
//
// THE MODEL FILE (LT_GETADDR: u32 size, the data, a pointer map): offsets are
// relative to the data. struct Model (include/namespace_Instance.h): headers at
// 0x14; ModelHeader 0x40 bytes: scale s16 x3 at 0x18, command list 0x20, texture
// layouts 0x28, colours 0x2C, animations 0x34 (count) / 0x38. Decoded like the
// game's drawing (RB = game/RenderBucket/RenderBucket_QueueExecute.c): frames
// raw or as the delta bit stream (RB:513, RB:2617), the command list with its
// strip FIFO, fans and cached vertices (RB:4646), a triangle's layout
// ptrTexLayout[texIndex - 1] (RB:3065), its colours from the colour table.
// Positions in model units: (pos + byte) x 4 x scale / 16384 per axis, y up,
// z the kart's front, the axes of CHRG (include/rldhires.inc): 1 m = 64 units.
// A one-sided triangle faces out when it is listed as stored with command bit
// 29 set and reversed with it clear (the twin's rule, platform/native_twin.c);
// a two-sided one (bit 28 clear) is kept as listed and marked two-sided.
//
// THE KART is not marked in the data: kart and figure are one mesh with vertex
// animation. A vertex record that is the same in all frames of animation 0 (the
// turn, 21 frames) is still; a triangle of three still records is a candidate.
// A candidate is the kart when it is found (each corner within RLDKIT_MATCH) in
// at least RLDKIT_SHARED_MIN of the other 14 drivers, or within
// RLDKIT_MATCH_LOOSE in as many (the dashboard and the steering column differ
// a little per driver). Figure parts that do not move in the turn (a back, a
// pony tail, feet resting on the floor) are each driver's own and fall out.
//
// THE STEERING WHEEL is not cut out (the retail hands are part of its ring):
// it is made new as a ring with three spokes where the retail ring turns. The
// ring is measured from the records that turn on a circle between frames 0, 10
// and 20 of the turn (radius RLDKIT_RING_RMIN..RMAX): the median of their
// circle centres, axes (the column it turns about) and radii; its plane (the
// ring leans against its column, 18.4 degrees for Crash) from the same records
// in frame 10, in the order around the column. Without enough of them the
// defaults of tools/rldpack_dummy.inc (RLDDUM_WHEEL_*, the column +z).
//
// SMOOTHER, THE SAME SHAPE: PN triangles (curved point-normal triangles,
// Vlachos et al. 2001), interpolating: every retail corner stays where it is.
// Corner normals are averaged over the corner's smooth fan (neighbours across
// edges whose faces meet at RLDKIT_CREASE_DEG or less); a crease edge or an open
// edge stays straight. Each edge's bulge is limited to RLDKIT_BULGE_MAX model
// units, the inside of a triangle likewise. UV and vertex colour are linear
// over the retail triangle. RLDKIT_SEGMENTS_* segments per edge.
//
// THE TEXTURES: every (page, CLUT) the kart samples is one tile, its UV box
// decoded through its CLUT (4 and 8 bit) or as 15 bit, colour (c x 255 + 15) /
// 31, the word 0x0000 transparent; the tiles go into one atlas with
// RLDKIT_TILE_PAD texels of repeated edge around each, then Scale2x once or
// twice (EPX, the published rule; the same as tools/rldpack_hires_upscale.inc).
//
// THE WHEELS: the retail game has no wheel model, only sprites. A wheel is made
// here: a tyre turned from a short profile (an inner side, rounded shoulders,
// the tread, an outer side with a dished rim; RLDKIT_TIRE_SEG around), its
// radius from the side sprite. Its texture (K2b) are the two sprites
// themselves: the tread-side sprite (icon 0) stretched round the tread - a
// point at height y takes the sprite row at y, so the front and the back half
// show the sprite as the game shows it from the front - and the hub-side
// sprite (icon 16) laid flat on both sides (rim and hub as the game draws
// them); transparent texels filled from their opaque neighbours, Scale2x like
// the kart's atlas, the vertex colour grey 128 (neutral). Without the sprites
// the wheel has flat colours (medians of bands of the sprites, or defaults).
// THE WHEEL'S LOOK (RldKit_WheelLook): an unlit render of the wheel from the
// hub side and from the front against the two sprites, mean colour per ring
// (tyre, rim, hub) and per half of the tread.
//
// DETERMINISM: integer decoding, doubles in a fixed order, sine and cosine as
// written-out series (RldKit_SinCos), sqrt (exact in IEEE). The same game data
// and options give the same output everywhere.

#ifndef RLDKIT_INC
#define RLDKIT_INC

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RLDKIT_DRIVERS 15                 // retail drivers with a kart (0..14)
#define RLDKIT_BIG_MODEL_HI 242u          // BI_RACERMODELHI
#define RLDKIT_BIG_SHARED_VRM 258u        // BI_SHAREDMPKVRM
#define RLDKIT_BIG_SHARED_PACK 259u
#define RLDKIT_VRAM_W 1024
#define RLDKIT_VRAM_H 512
#define RLDKIT_UNITS_PER_METRE 64.0

#define RLDKIT_RECORDS_MAX 4096
#define RLDKIT_COMMANDS_MAX 16384
#define RLDKIT_FRAMES_MAX 64
#define RLDKIT_COLOURS_MAX 128
#define RLDKIT_NEUTRAL_FRAME 10           // frame 10 of the turn: neutral

#define RLDKIT_MATCH 2.0                  // model units per corner: the same triangle in another driver
#define RLDKIT_MATCH_LOOSE 4.0
#define RLDKIT_SHARED_MIN 3               // other drivers that must have it

#define RLDKIT_CREASE_DEG 40.0
#define RLDKIT_BULGE_MAX 0.8              // model units an edge or a face may leave the retail triangle
#define RLDKIT_SEGMENTS_MAX 8

// THE COCKPIT FLOOR: a downward-facing kart triangle in this box is the floor
// plate under the driver (tools/rldpack_dummy.inc: the plate y 5.6..10.9 from z
// -42 to the nose, the driver's seat zone).
#define RLDKIT_FLOOR_HALF_X 14.0
#define RLDKIT_FLOOR_Y_MAX 14.0
#define RLDKIT_FLOOR_HALF_Z 45.0

// THE FLOOR LINE (kit-info.json "floor"): the planes z of tools/rldpack_dummy.inc
// s_rldDumBottom, measured on the donor's kart.
#define RLDKIT_FLOOR_LINE 16
static const double s_rldKitFloorZ[RLDKIT_FLOOR_LINE] = {-53.0, -50.0, -46.0, -42.0, -40.0, -30.0, -20.0, -10.0, 0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 55.0, 58.0};

#define RLDKIT_TILE_PAD 2
#define RLDKIT_ATLAS_W 128                // source texels across (the atlas grows down)
#define RLDKIT_ATLAS_H_MAX 1024

#define RLDKIT_RING_RMIN 7.0
#define RLDKIT_RING_RMAX 14.0
#define RLDKIT_RING_TUBE 1.1
#define RLDKIT_RING_SEG 32
#define RLDKIT_RING_SIDES 8

// The wheel points of the game (game/DrawTires.c BuildWheelLocalPairs: the
// middles (+-0x90, 0x40, 0xc7 / -0x60) / 4 in model units, tools/rldpack_dummy.inc
// RLDDUM_TIRE_*), the kart's side (x 28.3, RLDDUM_KART_BOX). The tyre is as wide
// as the tread sprite shows it (22 texels = 22 units for the retail sprites: its
// inner side reaches into the kart's side as the retail sprites do), at most
// RLDKIT_TIRE_WIDTH_MAX.
#define RLDKIT_TIRE_X 36.0
#define RLDKIT_TIRE_Y 16.0
#define RLDKIT_TIRE_FRONT_Z 49.75
#define RLDKIT_TIRE_REAR_Z (-24.0)
#define RLDKIT_TIRE_HALF_SIZE 16.0
#define RLDKIT_KART_SIDE_X 28.5
#define RLDKIT_TIRE_WIDTH_MAX 24.0
#define RLDKIT_TIRE_SEG 24
#define RLDKIT_TIRE_RIBS 5

// The steering wheel's defaults (tools/rldpack_dummy.inc RLDDUM_WHEEL_*) and
// the seat above the floor (RLDDUM_SEAT_Y - RLDDUM_SEAT_FLOOR_Y).
#define RLDKIT_RING_DEFAULT_CENTER {0.0, 38.0, 24.0}
#define RLDKIT_RING_DEFAULT_NORMAL {0.0, -0.316, 0.949}
#define RLDKIT_RING_DEFAULT_RADIUS 10.5
#define RLDKIT_SEAT_ABOVE_FLOOR 4.4
#define RLDKIT_SEAT_Z 0.9

//========================================================================================
// TYPES
//========================================================================================

typedef struct RldKitSource
{
	const u8 *model[RLDKIT_DRIVERS]; // BIGFILE 242 + n, whole entry (u32 size first)
	u32 modelSize[RLDKIT_DRIVERS];
	const u8 *vrm;                   // BIGFILE 258
	u32 vrmSize;
	const u8 *pack;                  // BIGFILE 259
	u32 packSize;
} RldKitSource;

typedef struct RldKitOptions
{
	int driver;   // the donor 0..14
	int segments; // 1, 2, 4 or 8 per edge (1 = the retail triangles, flat)
	int upscale;  // 1, 2 or 4 (Scale2x passes: 0, 1, 2)
} RldKitOptions;

typedef struct RldKitVertex
{
	float p[3];  // metres, CHRG axes
	float n[3];
	float uv[2]; // the atlas, v from the top
	u8 c[4];     // the colour byte: the PS1 byte (a textured face: grey 128 neutral), else the sRGB colour
} RldKitVertex;

typedef struct RldKitMesh
{
	RldKitVertex *v;
	u32 vertices;
	u32 *index;
	u32 indices;
	int textured;   // 1 = uses the atlas
} RldKitMesh;

// One retail triangle of the kart (the donor's), as decoded.
typedef struct RldKitTri
{
	double p[3][3];  // model units, frame 10, in the facing order
	u8 c[3][3];      // RGB of the colour table per corner
	u8 uv[3][2];     // page texels
	u16 tpage;
	u16 clut;
	u8 textured;
	u8 twoSided;
	int tile;        // atlas tile, -1 untextured
} RldKitTri;

typedef struct RldKitTile
{
	u16 tpage;
	u16 clut;
	int u0, v0, u1, v1; // texel box, inclusive
	int ax, ay;         // the box's top left in the source atlas (inside the pad)
} RldKitTile;

typedef struct RldKitResult
{
	char model[17];
	u32 commands, records, triangles;   // the donor's whole model
	u32 stillTriangles;                 // candidates
	u32 kartShared;                     // kart by RLDKIT_MATCH
	u32 kartLoose;                      // kart by RLDKIT_MATCH_LOOSE
	u32 dropped;                        // still but the donor's own: figure
	u32 kartTriangles;                  // retail triangles of the kart
	u32 openEdges;                      // open edges of the retail kart (kept, never closed)

	RldKitMesh kart;    // the smoothed kart, metres
	RldKitMesh flat;    // the retail kart triangles as they are (for the checks), metres, the same atlas
	RldKitMesh steer;   // the steering wheel, about its centre (identity rotation), metres
	RldKitMesh wheel;   // one wheel, axle +X, outer side +X, centre at the origin, metres

	u8 *atlas;          // RGBA, atlasW x atlasH (after the upscale)
	u32 atlasW, atlasH;
	u8 *atlasSource;    // RGBA before the upscale, atlasW / s x atlasH / s
	u32 sourceW, sourceH;
	int atlasAlpha;     // a texel the kart uses is transparent
	int tiles;
	RldKitTile *tile;

	double kartMin[3], kartMax[3];      // metres
	double floorY;                      // metres: the kart's floor under the seat
	double seat[3];
	int ringMeasured;                   // 0 = the defaults
	u32 ringRecords;
	double ringCenter[3], ringNormal[3], ringRadius; // metres; the ring's plane normal (unit)
	double ringAxis[3];                 // the column the ring turns about (unit), measured from the circles
	double gripLeft[3], gripRight[3];
	double footLeft[3], footRight[3];
	int feetMeasured;
	double driverMin[3], driverMax[3];  // the figure of the donor (frame 10), metres
	double wheelPos[4][3];              // fl fr rl rr, metres
	double wheelRadius, wheelWidth;     // metres
	double steerMaxDeg;                 // the game's front wheel at full lock (wheelRotation 64 x 4)
	u8 tyre[3], rim[3], hub[3], ring[3];// sRGB colours measured
	int wheelMeasured;                  // 0 = no sprites found: defaults
	// THE WHEEL'S TEXTURE (K2b): the tread-side sprite and the hub-side sprite
	// side by side (source atlas, texels; transparent ones filled), upscaled
	// like the kart's atlas; NULL = no sprites, the wheel has flat colours.
	u8 *wheelAtlas;                     // RGBA, wheelAtlasW x wheelAtlasH (after the upscale)
	u32 wheelAtlasW, wheelAtlasH;
	u8 *wheelAtlasSource;               // RGBA before the upscale, wheelSourceW x wheelSourceH
	u32 wheelSourceW, wheelSourceH;
	u8 *wheelSprite[2];                 // the two sprites as decoded (alpha 0 = transparent), for the look
	u32 wheelSpriteW[2], wheelSpriteH[2];
	int wheelSpriteAt[2][2];            // their top left in the source atlas
	double treadBox[4];                 // the tread sprite's opaque extent in the source atlas: x0 y0 x1 y1 (edges)
	double sideCentre[2];               // the hub sprite's middle and radius in the source atlas (texel edges)
	double sideRadius;
	double maxDeviation;                // model units: a smoothed vertex from its retail triangle
	u32 bulgeLimited;                   // edges and faces whose bulge was limited
	u32 floorDoubled;                   // cockpit floor triangles given a turned copy (THE COCKPIT FLOOR)
	double floorLine[RLDKIT_FLOOR_LINE][2]; // {z, y} model units: the lowest kart surface in the plane z (|x| <= the kart's side)
	int floorCount;
} RldKitResult;

//========================================================================================
// SMALL HELPERS
//========================================================================================

static u32 RldKit_LE16(const u8 *at)
{
	return (u32)at[0] | ((u32)at[1] << 8);
}

static u32 RldKit_LE32(const u8 *at)
{
	return (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) | ((u32)at[3] << 24);
}

static int RldKit_S16(u32 v)
{
	v &= 0xffffu;
	return (v & 0x8000u) ? ((int)v - 0x10000) : (int)v;
}

static int RldKit_S8(int v)
{
	v &= 0xff;
	return (v & 0x80) ? (v - 0x100) : v;
}

static int RldKit_Sext(u32 v, int bits)
{
	const u32 mask = (bits >= 32) ? 0xffffffffu : ((1u << bits) - 1u);
	const u32 sign = 1u << (bits - 1);

	v &= mask;
	return (v & sign) ? (int)((long long)v - (1ll << bits)) : (int)v;
}

static int RldKit_Fail(char *why, size_t whySize, const char *text)
{
	if ((why != NULL) && (whySize > 0u))
	{
		size_t n = strlen(text);

		if (n >= whySize)
		{
			n = whySize - 1u;
		}
		memcpy(why, text, n);
		why[n] = '\0';
	}
	return 0;
}

// Sine and cosine of degrees, written out (the same digits everywhere).
static void RldKit_SinCos(double degrees, double *s, double *c)
{
	double x;
	double x2;
	double term;
	double ss = 0.0;
	double cc = 0.0;
	int k;

	while (degrees > 180.0)
	{
		degrees -= 360.0;
	}
	while (degrees < -180.0)
	{
		degrees += 360.0;
	}
	x = degrees * (3.14159265358979323846 / 180.0);
	x2 = x * x;
	term = x;
	for (k = 1; k <= 41; k += 2)
	{
		ss += term;
		term = -term * x2 / (double)((k + 1) * (k + 2));
	}
	term = 1.0;
	for (k = 0; k <= 40; k += 2)
	{
		cc += term;
		term = -term * x2 / (double)((k + 1) * (k + 2));
	}
	*s = ss;
	*c = cc;
}

static double RldKit_Dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void RldKit_Cross(const double a[3], const double b[3], double out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static double RldKit_Len(const double a[3])
{
	return sqrt(RldKit_Dot(a, a));
}

static int RldKit_Normalize(double a[3])
{
	const double l = RldKit_Len(a);

	if (!(l > 1e-12))
	{
		return 0;
	}
	a[0] /= l;
	a[1] /= l;
	a[2] /= l;
	return 1;
}

static double RldKit_Dist(const double a[3], const double b[3])
{
	const double d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};

	return RldKit_Len(d);
}

static int RldKit_CmpDouble(const void *a, const void *b)
{
	const double x = *(const double *)a;
	const double y = *(const double *)b;

	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

// The median of n doubles (sorted in place); 0 for none.
static double RldKit_Median(double *v, u32 n)
{
	if (n == 0u)
	{
		return 0.0;
	}
	qsort(v, n, sizeof(double), RldKit_CmpDouble);
	return (n & 1u) ? v[n / 2u] : 0.5 * (v[n / 2u - 1u] + v[n / 2u]);
}

// 5-bit colour to a byte, rounded.
static u8 RldKit_From5(u32 c)
{
	return (u8)((c * 255u + 15u) / 31u);
}

//========================================================================================
// THE VRAM (shared.vrm)
//========================================================================================

// shared.vrm into a 1024 x 512 word VRAM (zeros where it puts nothing) and a
// mask of the words it wrote. 1 = at least one rectangle. The chain of TIMs
// (first word 0x20) or one TIM, as LOAD_VramFile reads them.
static int RldKit_Vram(const u8 *vrm, u32 size, u16 *vram, u8 *written)
{
	u32 at = 0;
	u32 next = 0;
	int chain = (size >= 8u) && (RldKit_LE32(&vrm[0]) == 0x20u);
	int rects = 0;

	memset(vram, 0, sizeof(u16) * RLDKIT_VRAM_W * RLDKIT_VRAM_H);
	memset(written, 0, (size_t)RLDKIT_VRAM_W * RLDKIT_VRAM_H);
	if (vrm == NULL)
	{
		return 0;
	}
	if (chain)
	{
		at = 8u;
		next = RldKit_LE32(&vrm[4]);
	}
	while ((u64)at + 0x14u <= (u64)size)
	{
		const u32 rx = RldKit_LE16(&vrm[at + 0x0Cu]);
		const u32 ry = RldKit_LE16(&vrm[at + 0x0Eu]);
		const u32 rw = RldKit_LE16(&vrm[at + 0x10u]);
		const u32 rh = RldKit_LE16(&vrm[at + 0x12u]);
		u32 x;
		u32 y;

		if (((u64)rx + rw <= RLDKIT_VRAM_W) && ((u64)ry + rh <= RLDKIT_VRAM_H) && ((u64)at + 0x14u + 2ull * rw * rh <= (u64)size))
		{
			for (y = 0; y < rh; y++)
			{
				for (x = 0; x < rw; x++)
				{
					vram[(ry + y) * RLDKIT_VRAM_W + rx + x] = (u16)RldKit_LE16(&vrm[at + 0x14u + 2u * (y * rw + x)]);
					written[(ry + y) * RLDKIT_VRAM_W + rx + x] = 1;
				}
			}
			rects++;
		}
		if (!chain || ((next & ~3u) == 0u) || ((u64)at + (next & ~3u) + 4u > (u64)size))
		{
			break;
		}
		at += next & ~3u;
		next = RldKit_LE32(&vrm[at]);
		at += 4u;
	}
	return rects > 0;
}

// The 15-bit word of texel (u, v) of a page through its CLUT; *ok 0 when VRAM
// was not written there.
static u32 RldKit_TexelWord(const u16 *vram, const u8 *written, u32 tpage, u32 clut, int u, int v, int *ok)
{
	const int px = (int)(tpage & 15u) * 64;
	const int py = (int)((tpage >> 4) & 1u) * 256;
	const u32 depth = (tpage >> 7) & 3u;
	const int cx = (int)(clut & 0x3fu) * 16;
	const int cy = (int)((clut >> 6) & 0x1ffu);
	int x;
	int y;
	u32 word;

	u &= 255;
	v &= 255;
	*ok = 0;
	if (depth == 0u)
	{
		x = px + u / 4;
		y = py + v;
		if ((x >= RLDKIT_VRAM_W) || (y >= RLDKIT_VRAM_H) || (cx + 15 >= RLDKIT_VRAM_W) || (cy >= RLDKIT_VRAM_H))
		{
			return 0u;
		}
		word = vram[y * RLDKIT_VRAM_W + x];
		*ok = written[y * RLDKIT_VRAM_W + x] && written[cy * RLDKIT_VRAM_W + cx];
		return vram[cy * RLDKIT_VRAM_W + cx + (int)((word >> (4 * (u & 3))) & 15u)];
	}
	if (depth == 1u)
	{
		x = px + u / 2;
		y = py + v;
		if ((x >= RLDKIT_VRAM_W) || (y >= RLDKIT_VRAM_H) || (cx + 255 >= RLDKIT_VRAM_W) || (cy >= RLDKIT_VRAM_H))
		{
			return 0u;
		}
		word = vram[y * RLDKIT_VRAM_W + x];
		*ok = written[y * RLDKIT_VRAM_W + x] && written[cy * RLDKIT_VRAM_W + cx];
		return vram[cy * RLDKIT_VRAM_W + cx + (int)((word >> (8 * (u & 1))) & 255u)];
	}
	x = px + u;
	y = py + v;
	if ((x >= RLDKIT_VRAM_W) || (y >= RLDKIT_VRAM_H))
	{
		return 0u;
	}
	*ok = written[y * RLDKIT_VRAM_W + x];
	return vram[y * RLDKIT_VRAM_W + x];
}

static void RldKit_WordRgba(u32 c, u8 *out)
{
	out[0] = RldKit_From5(c & 31u);
	out[1] = RldKit_From5((c >> 5) & 31u);
	out[2] = RldKit_From5((c >> 10) & 31u);
	out[3] = (c == 0u) ? 0u : 255u;
}

//========================================================================================
// THE RETAIL MODEL
//========================================================================================

typedef struct RldKitRetail
{
	char name[17];
	const u8 *body;      // the data (after the size word)
	u32 bodySize;
	int scale[3];
	u32 commands;
	u32 *command;
	u32 records;
	u32 frames;          // stored frames of animation 0
	int *gte;            // frames x records x 3 (GTE input units)
	u8 *still;           // records
	u32 texLayout;
	u32 colours;         // offset of the colour table
	u32 colourCount;
	// the triangles as drawn: corner records, colour indices, texture index, command
	u32 tris;
	u16 *triRec;         // 3 per triangle
	u8 *triCol;          // 3 per triangle
	u32 *triCmd;         // the command that decides texture and sides
} RldKitRetail;

static void RldKit_RetailFree(RldKitRetail *m)
{
	free(m->command);
	free(m->gte);
	free(m->still);
	free(m->triRec);
	free(m->triCol);
	free(m->triCmd);
	memset(m, 0, sizeof(*m));
}

static int RldKit_In(const RldKitRetail *m, u32 offset, u32 bytes)
{
	return ((u64)offset + bytes) <= (u64)m->bodySize;
}

static u32 RldKit_W(const RldKitRetail *m, u32 offset)
{
	return RldKit_In(m, offset, 4u) ? RldKit_LE32(&m->body[offset]) : 0u;
}

// One bit field of the delta stream (RB:513): MSB first inside little-endian words.
static int RldKit_Bits(const RldKitRetail *m, u32 at, u32 *bit, int bits)
{
	u32 v = 0;
	int i;

	for (i = 0; i < bits; i++)
	{
		const u32 word = RldKit_W(m, at + (*bit >> 5) * 4u);

		v = (v << 1) | ((word >> (31u - (*bit & 31u))) & 1u);
		(*bit)++;
	}
	return RldKit_Sext(v, bits);
}

// The command list replayed into triangles (RB:4646): the strip FIFO of three,
// fans (bit 30), cached vertices (bit 26), colour-only commands and the colour
// window. 1 = every record used exactly once in order.
static int RldKit_Replay(RldKitRetail *m)
{
	u32 slot[256];
	u32 window[4] = {0, 0, 0, 0};
	u32 wcol[4] = {0, 0, 0, 0};
	u32 fifo[3] = {0, 0, 0};
	u32 strip = 0;
	u32 prim = 0;
	u32 vi = 0;
	u32 k;
	u32 cap = m->commands + 4u;

	memset(slot, 0, sizeof(slot));
	m->triRec = (u16 *)malloc((size_t)cap * 3u * sizeof(u16));
	m->triCol = (u8 *)malloc((size_t)cap * 3u);
	m->triCmd = (u32 *)malloc((size_t)cap * sizeof(u32));
	if ((m->triRec == NULL) || (m->triCol == NULL) || (m->triCmd == NULL))
	{
		return 0;
	}
	m->tris = 0;
	for (k = 0; k < m->commands; k++)
	{
		const u32 c = m->command[k];
		const u32 flags = c >> 24;
		u32 rec;
		int useRtps;
		int reuse;
		u32 draw;

		if ((c >> 16) == 0u)
		{
			const u32 a = (c >> 9) & 0x7fu;
			const u32 b = (c >> 2) & 0x7fu;

			wcol[1] = a;
			wcol[2] = a;
			wcol[3] = b;
			continue;
		}
		{
			const u32 s = (c >> 16) & 0xffu;

			if (flags & 4u)
			{
				rec = slot[s];
			}
			else
			{
				rec = vi++;
				slot[s] = rec;
			}
		}
		window[0] = window[1];
		window[1] = window[2];
		window[2] = window[3];
		window[3] = rec;
		wcol[0] = wcol[1];
		wcol[1] = wcol[2];
		wcol[2] = wcol[3];
		wcol[3] = (c >> 9) & 0x7fu;
		if ((flags & 0x80u) || (strip == 0u))
		{
			prim = c;
		}
		if (flags & 0x80u)
		{
			strip = 0;
		}
		useRtps = strip > 2u;
		reuse = useRtps && ((flags & 0x40u) != 0u);
		if (reuse)
		{
			window[1] = window[0];
			wcol[1] = wcol[0];
		}
		if (strip < 2u)
		{
			strip++;
			continue;
		}
		draw = (strip == 2u) ? prim : c;
		if (!useRtps)
		{
			fifo[0] = window[1];
			fifo[1] = window[2];
			fifo[2] = window[3];
		}
		else if (reuse)
		{
			fifo[1] = fifo[2];
			fifo[2] = rec;
		}
		else
		{
			fifo[0] = fifo[1];
			fifo[1] = fifo[2];
			fifo[2] = rec;
		}
		strip++;
		if (m->tris >= cap)
		{
			return 0;
		}
		m->triRec[m->tris * 3u + 0u] = (u16)fifo[0];
		m->triRec[m->tris * 3u + 1u] = (u16)fifo[1];
		m->triRec[m->tris * 3u + 2u] = (u16)fifo[2];
		m->triCol[m->tris * 3u + 0u] = (u8)wcol[1];
		m->triCol[m->tris * 3u + 1u] = (u8)wcol[2];
		m->triCol[m->tris * 3u + 2u] = (u8)wcol[3];
		m->triCmd[m->tris] = draw;
		m->tris++;
	}
	if (vi != m->records)
	{
		return 0;
	}
	for (k = 0; k < m->tris * 3u; k++)
	{
		if (m->triRec[k] >= m->records)
		{
			return 0;
		}
	}
	return 1;
}

// Header 0 and animation 0 of a racer model file (the whole BIGFILE entry, or
// a CMDL: the same layout). fullHeight 1 keeps bit 0 of the height (a CMDL
// whose CHRI has RLDCHAR_FLAG_FULL_HEIGHT; the retail models: 0, RB:425).
// 1 = decoded; 0 with why.
static int RldKit_RetailDecodeAs(const u8 *file, u32 fileSize, int fullHeight, RldKitRetail *m, char *why, size_t whySize)
{
	u32 hdr;
	u32 cmdl;
	u32 o;
	u32 panims;
	u32 a;
	u32 raw;
	u32 fsize;
	u32 delta;
	u32 fr;
	u32 k;
	u32 maxColour = 0;

	memset(m, 0, sizeof(*m));
	if ((file == NULL) || (fileSize < 8u))
	{
		return RldKit_Fail(why, whySize, "the model file is missing or too short");
	}
	m->bodySize = RldKit_LE32(file);
	if ((m->bodySize < 0x18u) || ((u64)m->bodySize + 4u > fileSize))
	{
		return RldKit_Fail(why, whySize, "the model file's size word is wrong");
	}
	m->body = file + 4;
	memcpy(m->name, m->body, 16);
	m->name[16] = '\0';
	for (k = 0; k < 16u; k++)
	{
		if ((m->name[k] != '\0') && ((m->name[k] < 0x20) || (m->name[k] > 0x7e)))
		{
			m->name[k] = '?';
		}
	}
	if (RldKit_S16(RldKit_LE16(&m->body[0x12])) < 1)
	{
		return RldKit_Fail(why, whySize, "the model has no header");
	}
	hdr = RldKit_W(m, 0x14u);
	if (!RldKit_In(m, hdr, 0x40u))
	{
		return RldKit_Fail(why, whySize, "the model header lies outside the file");
	}
	for (k = 0; k < 3u; k++)
	{
		m->scale[k] = RldKit_S16(RldKit_LE16(&m->body[hdr + 0x18u + 2u * k]));
	}
	cmdl = RldKit_W(m, hdr + 0x20u);
	m->texLayout = RldKit_W(m, hdr + 0x28u);
	m->colours = RldKit_W(m, hdr + 0x2Cu);
	if (RldKit_W(m, hdr + 0x34u) < 1u)
	{
		return RldKit_Fail(why, whySize, "the model has no animation (a kart driver has four)");
	}
	panims = RldKit_W(m, hdr + 0x38u);

	// The command list: after its first word, up to 0xFFFFFFFF.
	m->command = (u32 *)malloc(RLDKIT_COMMANDS_MAX * sizeof(u32));
	if (m->command == NULL)
	{
		return RldKit_Fail(why, whySize, "out of memory");
	}
	o = cmdl + 4u;
	for (;;)
	{
		u32 c;

		if (!RldKit_In(m, o, 4u) || (m->commands >= RLDKIT_COMMANDS_MAX))
		{
			return RldKit_Fail(why, whySize, "the command list does not end inside the file");
		}
		c = RldKit_LE32(&m->body[o]);
		o += 4u;
		if (c == 0xffffffffu)
		{
			break;
		}
		m->command[m->commands++] = c;
		if ((c >> 16) != 0u)
		{
			if (!(c & 0x04000000u))
			{
				m->records++;
			}
			if (((c >> 9) & 0x7fu) > maxColour)
			{
				maxColour = (c >> 9) & 0x7fu;
			}
		}
		else
		{
			if (((c >> 9) & 0x7fu) > maxColour)
			{
				maxColour = (c >> 9) & 0x7fu;
			}
			if (((c >> 2) & 0x7fu) > maxColour)
			{
				maxColour = (c >> 2) & 0x7fu;
			}
		}
	}
	if ((m->records == 0u) || (m->records > RLDKIT_RECORDS_MAX))
	{
		return RldKit_Fail(why, whySize, "the model has no vertex records, or too many");
	}
	m->colourCount = maxColour + 1u;
	if (!RldKit_In(m, m->colours, 4u * m->colourCount))
	{
		return RldKit_Fail(why, whySize, "the colour table lies outside the file");
	}

	// Animation 0, every stored frame.
	a = RldKit_W(m, panims);
	if (!RldKit_In(m, a, 0x18u))
	{
		return RldKit_Fail(why, whySize, "animation 0 lies outside the file");
	}
	raw = RldKit_LE16(&m->body[a + 0x10u]);
	fsize = RldKit_LE16(&m->body[a + 0x12u]);
	delta = RldKit_W(m, a + 0x14u);
	m->frames = (raw & 0x8000u) ? (((raw & 0x7fffu) + 1u) / 2u) : (raw & 0x7fffu);
	if ((m->frames == 0u) || (m->frames > RLDKIT_FRAMES_MAX))
	{
		return RldKit_Fail(why, whySize, "animation 0 has no frames, or too many");
	}
	m->gte = (int *)malloc((size_t)m->frames * m->records * 3u * sizeof(int));
	m->still = (u8 *)malloc(m->records);
	if ((m->gte == NULL) || (m->still == NULL))
	{
		return RldKit_Fail(why, whySize, "out of memory");
	}
	for (fr = 0; fr < m->frames; fr++)
	{
		const u32 frame = a + 0x18u + fr * fsize;
		int pos[3];
		u32 voff;
		int acc[3] = {0, 0, 0};
		u32 bit = 0;
		u32 r;

		if (!RldKit_In(m, frame, 0x1Cu))
		{
			return RldKit_Fail(why, whySize, "a frame lies outside the file");
		}
		for (k = 0; k < 3u; k++)
		{
			pos[k] = RldKit_S16(RldKit_LE16(&m->body[frame + 2u * k]));
		}
		voff = RldKit_W(m, frame + 0x18u);
		for (r = 0; r < m->records; r++)
		{
			int b0;
			int b1;
			int b2;
			int X;
			int Y;
			int Z;
			int *g = &m->gte[((size_t)fr * m->records + r) * 3u];

			if (delta == 0u)
			{
				const u32 at = frame + voff + 3u * r;

				if (!RldKit_In(m, at, 3u))
				{
					return RldKit_Fail(why, whySize, "a frame's records lie outside the file");
				}
				b0 = m->body[at];
				b1 = m->body[at + 1u];
				b2 = m->body[at + 2u];
			}
			else
			{
				const u32 t = RldKit_W(m, delta + 4u * r);
				const int bitsK[3] = {(int)((t >> 6) & 7u), (int)((t >> 3) & 7u), (int)(t & 7u)};
				const int baseK[3] = {RldKit_Sext(t >> 25, 7) * 2, RldKit_Sext(t >> 17, 8), RldKit_Sext(t >> 9, 8)};

				if (!RldKit_In(m, delta + 4u * r, 4u))
				{
					return RldKit_Fail(why, whySize, "the delta table lies outside the file");
				}
				for (k = 0; k < 3u; k++)
				{
					const int fld = RldKit_Bits(m, frame + voff, &bit, bitsK[k] + 1);

					acc[k] = (bitsK[k] == 7) ? RldKit_S8(fld & 0xff) : RldKit_S8((acc[k] + fld + baseK[k]) & 0xff);
				}
				b0 = acc[0] & 0xff;
				b1 = acc[2] & 0xff;
				b2 = acc[1] & 0xff;
			}
			X = (int)((u32)((b0 + (pos[0] & 0x7fff)) << 2) & 0xffffu);
			Y = (int)((u32)(((b2 + pos[1]) & (fullHeight ? ~0 : ~1)) << 2) & 0xffffu);
			Z = (int)((u32)((b1 + pos[2]) << 2) & 0xffffu);
			g[0] = (X & 0x8000) ? (X - 0x10000) : X;
			g[1] = (Y & 0x8000) ? (Y - 0x10000) : Y;
			g[2] = (Z & 0x8000) ? (Z - 0x10000) : Z;
		}
	}
	for (k = 0; k < m->records; k++)
	{
		u32 f2;

		m->still[k] = 1;
		for (f2 = 1; f2 < m->frames; f2++)
		{
			const int *a0 = &m->gte[(size_t)k * 3u];
			const int *b = &m->gte[((size_t)f2 * m->records + k) * 3u];

			if ((a0[0] != b[0]) || (a0[1] != b[1]) || (a0[2] != b[2]))
			{
				m->still[k] = 0;
				break;
			}
		}
	}
	if (!RldKit_Replay(m))
	{
		return RldKit_Fail(why, whySize, "the command list does not replay (records out of order)");
	}
	return 1;
}

// RldKit_RetailDecodeAs with the retail height rule.
static int RldKit_RetailDecode(const u8 *file, u32 fileSize, RldKitRetail *m, char *why, size_t whySize)
{
	return RldKit_RetailDecodeAs(file, fileSize, 0, m, why, whySize);
}

// The model-unit position of a record in a stored frame.
static void RldKit_Pos(const RldKitRetail *m, u32 frame, u32 rec, double out[3])
{
	const int *g;
	int k;

	if (frame >= m->frames)
	{
		frame = m->frames - 1u;
	}
	g = &m->gte[((size_t)frame * m->records + rec) * 3u];
	for (k = 0; k < 3; k++)
	{
		out[k] = (double)g[k] * (double)m->scale[k] / 16384.0;
	}
}

// The colour table entry i as RGB.
static void RldKit_Colour(const RldKitRetail *m, u32 i, u8 out[3])
{
	const u32 w = (i < m->colourCount) ? RldKit_W(m, m->colours + 4u * i) : 0x808080u;

	out[0] = (u8)(w & 0xffu);
	out[1] = (u8)((w >> 8) & 0xffu);
	out[2] = (u8)((w >> 16) & 0xffu);
}

// The layout of triangle t: 1 = textured (words w0..w2 filled).
static int RldKit_Layout(const RldKitRetail *m, u32 t, u32 w[3])
{
	const u32 cmd = m->triCmd[t];
	const u32 ti = cmd & 0x1ffu;
	u32 p;

	if ((ti == 0u) || ((cmd >> 16) == 0u) || !RldKit_In(m, m->texLayout + 4u * (ti - 1u), 4u))
	{
		return 0;
	}
	p = RldKit_W(m, m->texLayout + 4u * (ti - 1u));
	if ((p == 0u) || !RldKit_In(m, p, 12u))
	{
		return 0;
	}
	w[0] = RldKit_W(m, p);
	w[1] = RldKit_W(m, p + 4u);
	w[2] = RldKit_W(m, p + 8u);
	return 1;
}

// The three corners of triangle t, frame 10, sorted (x, then y, then z): the
// key that finds the same triangle in another driver.
static void RldKit_SortedCorners(const RldKitRetail *m, u32 t, double out[3][3])
{
	int i;
	int j;

	for (i = 0; i < 3; i++)
	{
		RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, m->triRec[t * 3u + (u32)i], out[i]);
	}
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 2 - i; j++)
		{
			int swap = (out[j][0] > out[j + 1][0]) || ((out[j][0] == out[j + 1][0]) && ((out[j][1] > out[j + 1][1]) || ((out[j][1] == out[j + 1][1]) && (out[j][2] > out[j + 1][2]))));

			if (swap)
			{
				double tmp[3];

				memcpy(tmp, out[j], sizeof(tmp));
				memcpy(out[j], out[j + 1], sizeof(tmp));
				memcpy(out[j + 1], tmp, sizeof(tmp));
			}
		}
	}
}

static int RldKit_TriStill(const RldKitRetail *m, u32 t)
{
	return m->still[m->triRec[t * 3u]] && m->still[m->triRec[t * 3u + 1u]] && m->still[m->triRec[t * 3u + 2u]];
}

// 1 when driver o has a still triangle within tol of key at each corner.
static int RldKit_HasTriangle(const RldKitRetail *o, const double key[3][3], double tol)
{
	u32 t;

	for (t = 0; t < o->tris; t++)
	{
		double c[3][3];

		if (!RldKit_TriStill(o, t))
		{
			continue;
		}
		RldKit_SortedCorners(o, t, c);
		if ((RldKit_Dist(c[0], key[0]) <= tol) && (RldKit_Dist(c[1], key[1]) <= tol) && (RldKit_Dist(c[2], key[2]) <= tol))
		{
			return 1;
		}
	}
	return 0;
}

//========================================================================================
// THE STEERING WHEEL, THE FEET, THE FIGURE
//========================================================================================

// The circle through a, b, c: centre, normal (unit), radius. 0 = collinear.
static int RldKit_Circle(const double a[3], const double b[3], const double c[3], double centre[3], double normal[3], double *radius)
{
	const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const double ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
	double n[3];
	double l2;
	double t1[3];
	double t2[3];
	double s[3];
	int k;

	RldKit_Cross(ab, ac, n);
	l2 = RldKit_Dot(n, n);
	if (!(l2 > 1e-6))
	{
		return 0;
	}
	// centre = a + (|ac|^2 (n x ab) + |ab|^2 (ac x n)) / (2 |n|^2)
	RldKit_Cross(n, ab, t1);
	RldKit_Cross(ac, n, t2);
	for (k = 0; k < 3; k++)
	{
		s[k] = (RldKit_Dot(ac, ac) * t1[k] + RldKit_Dot(ab, ab) * t2[k]) / (2.0 * l2);
		centre[k] = a[k] + s[k];
		normal[k] = n[k];
	}
	*radius = RldKit_Len(s);
	return RldKit_Normalize(normal);
}

static void RldKit_Ring(const RldKitRetail *m, RldKitResult *r)
{
	const double defCentre[3] = RLDKIT_RING_DEFAULT_CENTER;
	const double defNormal[3] = RLDKIT_RING_DEFAULT_NORMAL;
	double *cx = (double *)malloc(m->records * 8u * sizeof(double));
	u32 n = 0;
	u32 rec;
	int k;

	memcpy(r->ringCenter, defCentre, sizeof(defCentre));
	memcpy(r->ringNormal, defNormal, sizeof(defNormal));
	RldKit_Normalize(r->ringNormal);
	r->ringAxis[0] = 0.0;
	r->ringAxis[1] = 0.0;
	r->ringAxis[2] = 1.0;
	r->ringRadius = RLDKIT_RING_DEFAULT_RADIUS;
	r->ringMeasured = 0;
	if ((cx != NULL) && (m->frames > 20u))
	{
		for (rec = 0; rec < m->records; rec++)
		{
			double p0[3];
			double p1[3];
			double p2[3];
			double c[3];
			double nn[3];
			double rad = 0.0;

			if (m->still[rec])
			{
				continue;
			}
			RldKit_Pos(m, 0u, rec, p0);
			RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, rec, p1);
			RldKit_Pos(m, 20u, rec, p2);
			if ((fabs(p1[0]) > 16.0) || (p1[1] < 25.0) || (p1[1] > 52.0) || (p1[2] < 8.0) || (p1[2] > 45.0))
			{
				continue;
			}
			if (!RldKit_Circle(p0, p1, p2, c, nn, &rad) || (rad < RLDKIT_RING_RMIN) || (rad > RLDKIT_RING_RMAX))
			{
				continue;
			}
			if (nn[2] < 0.0)
			{
				nn[0] = -nn[0];
				nn[1] = -nn[1];
				nn[2] = -nn[2];
			}
			if (nn[2] < 0.8) // the column runs about +z
			{
				continue;
			}
			for (k = 0; k < 3; k++)
			{
				cx[n * 8u + (u32)k] = c[k];
				cx[n * 8u + 3u + (u32)k] = nn[k];
			}
			cx[n * 8u + 6u] = rad;
			cx[n * 8u + 7u] = (double)rec;
			n++;
		}
	}
	r->ringRecords = n;
	if (n >= 3u)
	{
		double *col = (double *)malloc(n * sizeof(double));

		if (col != NULL)
		{
			for (k = 0; k < 7; k++)
			{
				u32 i;
				double med;

				for (i = 0; i < n; i++)
				{
					col[i] = cx[i * 8u + (u32)k];
				}
				med = RldKit_Median(col, n);
				if (k < 3)
				{
					r->ringCenter[k] = med;
				}
				else if (k < 6)
				{
					r->ringAxis[k - 3] = med;
				}
				else
				{
					r->ringRadius = med;
				}
			}
			free(col);
			if (!RldKit_Normalize(r->ringAxis))
			{
				r->ringAxis[0] = 0.0;
				r->ringAxis[1] = 0.0;
				r->ringAxis[2] = 1.0;
			}
			// The ring's plane: its records in frame 10, ordered by their angle
			// about the axis, the sum of the cross products of neighbours.
			{
				double e1[3];
				double e2[3];
				double *ang = (double *)malloc(n * 2u * sizeof(double));
				const double up[3] = {0.0, 1.0, 0.0};
				const double dd = RldKit_Dot(up, r->ringAxis);
				double sum[3] = {0.0, 0.0, 0.0};
				u32 i;
				u32 j;

				for (k = 0; k < 3; k++)
				{
					e1[k] = up[k] - dd * r->ringAxis[k];
				}
				RldKit_Normalize(e1);
				RldKit_Cross(r->ringAxis, e1, e2);
				if (ang != NULL)
				{
					for (i = 0; i < n; i++)
					{
						double p[3];
						double q[3];

						RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, (u32)cx[i * 8u + 7u], p);
						for (k = 0; k < 3; k++)
						{
							q[k] = p[k] - r->ringCenter[k];
						}
						ang[i * 2u] = atan2(RldKit_Dot(q, e2), RldKit_Dot(q, e1));
						ang[i * 2u + 1u] = cx[i * 8u + 7u];
					}
					// insertion sort by angle (n is small)
					for (i = 1; i < n; i++)
					{
						for (j = i; (j > 0u) && (ang[(j - 1u) * 2u] > ang[j * 2u]); j--)
						{
							double t0 = ang[j * 2u];
							double t1 = ang[j * 2u + 1u];

							ang[j * 2u] = ang[(j - 1u) * 2u];
							ang[j * 2u + 1u] = ang[(j - 1u) * 2u + 1u];
							ang[(j - 1u) * 2u] = t0;
							ang[(j - 1u) * 2u + 1u] = t1;
						}
					}
					for (i = 0; i < n; i++)
					{
						double a[3];
						double b[3];
						double c3[3];

						RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, (u32)ang[i * 2u + 1u], a);
						RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, (u32)ang[((i + 1u) % n) * 2u + 1u], b);
						for (k = 0; k < 3; k++)
						{
							a[k] -= r->ringCenter[k];
							b[k] -= r->ringCenter[k];
						}
						RldKit_Cross(a, b, c3);
						for (k = 0; k < 3; k++)
						{
							sum[k] += c3[k];
						}
					}
					free(ang);
				}
				if (RldKit_Normalize(sum))
				{
					if (RldKit_Dot(sum, r->ringAxis) < 0.0)
					{
						sum[0] = -sum[0];
						sum[1] = -sum[1];
						sum[2] = -sum[2];
					}
					memcpy(r->ringNormal, sum, sizeof(sum));
				}
			}
			r->ringMeasured = 1;
		}
	}
	free(cx);
}

//========================================================================================
// PN TRIANGLES
//========================================================================================

typedef struct RldKitPn
{
	double p[3][3];   // corners (model units)
	double n[3][3];   // corner normals (smooth fan)
	double b[6][3];   // edge control points b210 b120 b021 b012 b102 b201
	double b111[3];
	double face[3];   // unit face normal
	double uv[3][2];
	u8 c[3][4];
	int edgeHard[3];  // edge 0 = corner 0-1, 1 = 1-2, 2 = 2-0
} RldKitPn;

static int RldKit_SamePoint(const double a[3], const double b[3])
{
	return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]);
}

// Edge e of triangle a equals some edge of triangle b (by positions): 1 and *eb.
static int RldKit_SharedEdge(const RldKitPn *a, int e, const RldKitPn *b, int *eb)
{
	const double *p0 = a->p[e];
	const double *p1 = a->p[(e + 1) % 3];
	int f;

	for (f = 0; f < 3; f++)
	{
		const double *q0 = b->p[f];
		const double *q1 = b->p[(f + 1) % 3];

		if ((RldKit_SamePoint(p0, q0) && RldKit_SamePoint(p1, q1)) || (RldKit_SamePoint(p0, q1) && RldKit_SamePoint(p1, q0)))
		{
			*eb = f;
			return 1;
		}
	}
	return 0;
}

// A cubic edge from p0 (normal n0) to p1 (n1): its two control points, limited
// so that the curve leaves the chord by at most RLDKIT_BULGE_MAX. Symmetric in
// its ends (the neighbour across the edge computes the same curve). 1 = limited.
static int RldKit_EdgeControls(const double p0[3], const double n0[3], const double p1[3], const double n1[3], double c0[3], double c1[3])
{
	const double d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
	const double w0 = RldKit_Dot(d, n0);
	const double w1 = -RldKit_Dot(d, n1);
	double off0[3];
	double off1[3];
	double dev = 0.0;
	double scale = 1.0;
	int k;
	int s;

	for (k = 0; k < 3; k++)
	{
		off0[k] = -w0 * n0[k] / 3.0;
		off1[k] = -w1 * n1[k] / 3.0;
	}
	// The curve's offset from the chord at t: 3 t (1-t)^2 off0 + 3 t^2 (1-t) off1.
	for (s = 1; s < 8; s++)
	{
		const double t = (double)s / 8.0;
		double o[3];

		for (k = 0; k < 3; k++)
		{
			o[k] = 3.0 * t * (1.0 - t) * (1.0 - t) * off0[k] + 3.0 * t * t * (1.0 - t) * off1[k];
		}
		if (RldKit_Len(o) > dev)
		{
			dev = RldKit_Len(o);
		}
	}
	if (dev > RLDKIT_BULGE_MAX)
	{
		scale = RLDKIT_BULGE_MAX / dev;
	}
	for (k = 0; k < 3; k++)
	{
		c0[k] = (2.0 * p0[k] + p1[k]) / 3.0 + scale * off0[k];
		c1[k] = (2.0 * p1[k] + p0[k]) / 3.0 + scale * off1[k];
	}
	return dev > RLDKIT_BULGE_MAX;
}

// Builds the PN patches of the kart triangles: corner normals over smooth fans,
// hard edges, control points. 1 = done.
static int RldKit_PnBuild(RldKitPn *pn, u32 count, RldKitResult *r)
{
	const double creaseCos = cos(RLDKIT_CREASE_DEG * 3.14159265358979323846 / 180.0);
	int *nbTri = (int *)malloc((size_t)count * 3u * sizeof(int));
	u32 t;
	int e;

	if (nbTri == NULL)
	{
		return 0;
	}
	// Face normals and edge neighbours (the first one; an edge of three or more
	// faces counts as hard).
	for (t = 0; t < count; t++)
	{
		const double ab[3] = {pn[t].p[1][0] - pn[t].p[0][0], pn[t].p[1][1] - pn[t].p[0][1], pn[t].p[1][2] - pn[t].p[0][2]};
		const double ac[3] = {pn[t].p[2][0] - pn[t].p[0][0], pn[t].p[2][1] - pn[t].p[0][1], pn[t].p[2][2] - pn[t].p[0][2]};

		RldKit_Cross(ab, ac, pn[t].face);
		if (!RldKit_Normalize(pn[t].face))
		{
			pn[t].face[0] = 0.0;
			pn[t].face[1] = 1.0;
			pn[t].face[2] = 0.0;
		}
	}
	for (t = 0; t < count; t++)
	{
		for (e = 0; e < 3; e++)
		{
			u32 o;
			int found = 0;
			int nb = -1;

			for (o = 0; o < count; o++)
			{
				int eb;

				if ((o != t) && RldKit_SharedEdge(&pn[t], e, &pn[o], &eb))
				{
					found++;
					nb = (int)o;
				}
			}
			nbTri[t * 3u + (u32)e] = (found == 1) ? nb : -1;
			pn[t].edgeHard[e] = (found != 1) || (RldKit_Dot(pn[t].face, pn[nb].face) < creaseCos);
		}
	}
	r->openEdges = 0;
	for (t = 0; t < count; t++)
	{
		for (e = 0; e < 3; e++)
		{
			r->openEdges += (nbTri[t * 3u + (u32)e] < 0) ? 1u : 0u;
		}
	}
	// Corner normals: the faces reached from the corner over smooth edges that
	// touch it (a walk around the corner's fan).
	for (t = 0; t < count; t++)
	{
		int c;

		for (c = 0; c < 3; c++)
		{
			double sum[3] = {0.0, 0.0, 0.0};
			u32 *work = (u32 *)malloc((size_t)count * sizeof(u32));
			u8 *seen = (u8 *)calloc(count, 1);
			u32 top = 0;
			int k;

			if ((work == NULL) || (seen == NULL))
			{
				free(work);
				free(seen);
				free(nbTri);
				return 0;
			}
			work[top++] = t;
			seen[t] = 1;
			while (top > 0u)
			{
				const u32 f = work[--top];
				int cf = -1;
				int k2;

				for (k2 = 0; k2 < 3; k2++)
				{
					if (RldKit_SamePoint(pn[f].p[k2], pn[t].p[c]))
					{
						cf = k2;
					}
				}
				if (cf < 0)
				{
					continue;
				}
				{
					// angle-weighted: the face normal times the corner angle
					double u[3];
					double v[3];
					double w;

					for (k = 0; k < 3; k++)
					{
						u[k] = pn[f].p[(cf + 1) % 3][k] - pn[f].p[cf][k];
						v[k] = pn[f].p[(cf + 2) % 3][k] - pn[f].p[cf][k];
					}
					w = (RldKit_Normalize(u) && RldKit_Normalize(v)) ? acos(fmax(-1.0, fmin(1.0, RldKit_Dot(u, v)))) : 0.0;
					for (k = 0; k < 3; k++)
					{
						sum[k] += w * pn[f].face[k];
					}
				}
				// the two edges of f at the corner: (cf - 1) and cf
				for (k2 = 0; k2 < 3; k2++)
				{
					const int touches = (k2 == cf) || (((k2 + 1) % 3) == cf);
					const int nb = nbTri[f * 3u + (u32)k2];

					if (touches && !pn[f].edgeHard[k2] && (nb >= 0) && !seen[nb])
					{
						seen[nb] = 1;
						work[top++] = (u32)nb;
					}
				}
			}
			free(work);
			free(seen);
			if (!RldKit_Normalize(sum))
			{
				memcpy(sum, pn[t].face, sizeof(sum));
			}
			memcpy(pn[t].n[c], sum, sizeof(sum));
		}
	}
	// Control points: a hard edge is straight, a smooth one a limited cubic.
	r->bulgeLimited = 0;
	for (t = 0; t < count; t++)
	{
		double E[3] = {0.0, 0.0, 0.0};
		double V[3];
		int k;

		for (e = 0; e < 3; e++)
		{
			const int a = e;
			const int b = (e + 1) % 3;
			double *ca = pn[t].b[e * 2];
			double *cb = pn[t].b[e * 2 + 1];

			if (pn[t].edgeHard[e])
			{
				for (k = 0; k < 3; k++)
				{
					ca[k] = (2.0 * pn[t].p[a][k] + pn[t].p[b][k]) / 3.0;
					cb[k] = (2.0 * pn[t].p[b][k] + pn[t].p[a][k]) / 3.0;
				}
			}
			else
			{
				r->bulgeLimited += (u32)RldKit_EdgeControls(pn[t].p[a], pn[t].n[a], pn[t].p[b], pn[t].n[b], ca, cb);
			}
		}
		for (k = 0; k < 3; k++)
		{
			for (e = 0; e < 6; e++)
			{
				E[k] += pn[t].b[e][k] / 6.0;
			}
			V[k] = (pn[t].p[0][k] + pn[t].p[1][k] + pn[t].p[2][k]) / 3.0;
			pn[t].b111[k] = E[k] + (E[k] - V[k]) / 2.0;
		}
	}
	free(nbTri);
	return 1;
}

// The patch at barycentric (w0, w1, w2) for corners 0, 1, 2. Edge e runs from
// corner e to corner e+1 with controls b[2e] (near e) and b[2e+1] (near e+1).
static void RldKit_PnPoint(const RldKitPn *t, double w0, double w1, double w2, double out[3])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		out[k] = t->p[0][k] * w0 * w0 * w0 + t->p[1][k] * w1 * w1 * w1 + t->p[2][k] * w2 * w2 * w2 + 3.0 * t->b[0][k] * w0 * w0 * w1 +
		         3.0 * t->b[1][k] * w0 * w1 * w1 + 3.0 * t->b[2][k] * w1 * w1 * w2 + 3.0 * t->b[3][k] * w1 * w2 * w2 + 3.0 * t->b[4][k] * w2 * w2 * w0 +
		         3.0 * t->b[5][k] * w2 * w0 * w0 + 6.0 * t->b111[k] * w0 * w1 * w2;
	}
}

// The distance of q from triangle (a, b, c) (Ericson, closest point on a triangle).
static double RldKit_PointTriangle(const double q[3], const double a[3], const double b[3], const double c[3])
{
	double ab[3];
	double ac[3];
	double ap[3];
	double bp[3];
	double cp[3];
	double d1;
	double d2;
	double d3;
	double d4;
	double d5;
	double d6;
	double va;
	double vb;
	double vc;
	double v;
	double w;
	double x[3];
	int k;

	for (k = 0; k < 3; k++)
	{
		ab[k] = b[k] - a[k];
		ac[k] = c[k] - a[k];
		ap[k] = q[k] - a[k];
		bp[k] = q[k] - b[k];
		cp[k] = q[k] - c[k];
	}
	d1 = RldKit_Dot(ab, ap);
	d2 = RldKit_Dot(ac, ap);
	if ((d1 <= 0.0) && (d2 <= 0.0))
	{
		return RldKit_Dist(q, a);
	}
	d3 = RldKit_Dot(ab, bp);
	d4 = RldKit_Dot(ac, bp);
	if ((d3 >= 0.0) && (d4 <= d3))
	{
		return RldKit_Dist(q, b);
	}
	vc = d1 * d4 - d3 * d2;
	if ((vc <= 0.0) && (d1 >= 0.0) && (d3 <= 0.0))
	{
		v = d1 / (d1 - d3);
		for (k = 0; k < 3; k++)
		{
			x[k] = a[k] + v * ab[k];
		}
		return RldKit_Dist(q, x);
	}
	d5 = RldKit_Dot(ab, cp);
	d6 = RldKit_Dot(ac, cp);
	if ((d6 >= 0.0) && (d5 <= d6))
	{
		return RldKit_Dist(q, c);
	}
	vb = d5 * d2 - d1 * d6;
	if ((vb <= 0.0) && (d2 >= 0.0) && (d6 <= 0.0))
	{
		w = d2 / (d2 - d6);
		for (k = 0; k < 3; k++)
		{
			x[k] = a[k] + w * ac[k];
		}
		return RldKit_Dist(q, x);
	}
	va = d3 * d6 - d5 * d4;
	if ((va <= 0.0) && ((d4 - d3) >= 0.0) && ((d5 - d6) >= 0.0))
	{
		w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		for (k = 0; k < 3; k++)
		{
			x[k] = b[k] + w * (c[k] - b[k]);
		}
		return RldKit_Dist(q, x);
	}
	{
		const double denom = 1.0 / (va + vb + vc);

		v = vb * denom;
		w = vc * denom;
		for (k = 0; k < 3; k++)
		{
			x[k] = a[k] + ab[k] * v + ac[k] * w;
		}
	}
	return RldKit_Dist(q, x);
}

//========================================================================================
// MESHES
//========================================================================================

static void RldKit_MeshFree(RldKitMesh *m)
{
	free(m->v);
	free(m->index);
	memset(m, 0, sizeof(*m));
}

static int RldKit_MeshAlloc(RldKitMesh *m, u32 vertices, u32 indices)
{
	memset(m, 0, sizeof(*m));
	m->v = (RldKitVertex *)calloc(vertices ? vertices : 1u, sizeof(RldKitVertex));
	m->index = (u32 *)calloc(indices ? indices : 1u, sizeof(u32));
	return (m->v != NULL) && (m->index != NULL);
}

static void RldKit_SetVertex(RldKitVertex *v, const double p[3], double unitsPerOut, const double n[3], double u, double vv, const u8 c[4])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		v->p[k] = (float)(p[k] / unitsPerOut);
		v->n[k] = (float)n[k];
	}
	v->uv[0] = (float)u;
	v->uv[1] = (float)vv;
	memcpy(v->c, c, 4);
}

//========================================================================================
// SCALE2X (EPX; the published rule, as tools/rldpack_hires_upscale.inc)
//========================================================================================

static void RldKit_Scale2x(const u8 *src, u32 w, u32 h, u8 *dst)
{
	u32 x;
	u32 y;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			const u8 *E = &src[(y * w + x) * 4u];
			const u8 *B = &src[(((y > 0u) ? y - 1u : y) * w + x) * 4u];
			const u8 *H = &src[(((y + 1u < h) ? y + 1u : y) * w + x) * 4u];
			const u8 *D = &src[(y * w + ((x > 0u) ? x - 1u : x)) * 4u];
			const u8 *F = &src[(y * w + ((x + 1u < w) ? x + 1u : x)) * 4u];
			u8 *o0 = &dst[((2u * y) * (2u * w) + 2u * x) * 4u];
			u8 *o1 = o0 + 4;
			u8 *o2 = &dst[((2u * y + 1u) * (2u * w) + 2u * x) * 4u];
			u8 *o3 = o2 + 4;

			if ((memcmp(B, H, 4) != 0) && (memcmp(D, F, 4) != 0))
			{
				memcpy(o0, (memcmp(D, B, 4) == 0) ? D : E, 4);
				memcpy(o1, (memcmp(B, F, 4) == 0) ? F : E, 4);
				memcpy(o2, (memcmp(D, H, 4) == 0) ? D : E, 4);
				memcpy(o3, (memcmp(H, F, 4) == 0) ? F : E, 4);
			}
			else
			{
				memcpy(o0, E, 4);
				memcpy(o1, E, 4);
				memcpy(o2, E, 4);
				memcpy(o3, E, 4);
			}
		}
	}
}

//========================================================================================
// THE WHEEL
//========================================================================================

// Icon group 0 of the shared pack: icon k's layout words. 1 = found.
static int RldKit_TireIcon(const u8 *pack, u32 packSize, int k, u32 layout[3])
{
	const u8 *body;
	u32 size;
	u32 ltl;
	u32 groups;
	u32 first;
	u32 g;

	if ((pack == NULL) || (packSize < 8u))
	{
		return 0;
	}
	size = RldKit_LE32(pack);
	if ((u64)size + 4u > packSize)
	{
		size = packSize - 4u;
	}
	body = pack + 4;
	ltl = RldKit_LE32(body);
	if ((u64)ltl + 16u > size)
	{
		return 0;
	}
	groups = RldKit_LE32(&body[ltl + 8u]);
	first = RldKit_LE32(&body[ltl + 12u]);
	for (g = 0; (g < groups) && (g < 64u) && ((u64)first + 4u * g + 4u <= size); g++)
	{
		const u32 gp = RldKit_LE32(&body[first + 4u * g]);
		u32 ip;

		if ((u64)gp + 0x14u > size)
		{
			continue;
		}
		if ((RldKit_S16(RldKit_LE16(&body[gp + 0x10u])) != 0) || (RldKit_S16(RldKit_LE16(&body[gp + 0x12u])) <= k))
		{
			continue;
		}
		if ((u64)gp + 0x14u + 4u * (u32)k + 4u > size)
		{
			return 0;
		}
		ip = RldKit_LE32(&body[gp + 0x14u + 4u * (u32)k]);
		if ((u64)ip + 0x20u > size)
		{
			return 0;
		}
		layout[0] = RldKit_LE32(&body[ip + 0x14u]);
		layout[1] = RldKit_LE32(&body[ip + 0x18u]);
		layout[2] = RldKit_LE32(&body[ip + 0x1Cu]);
		return 1;
	}
	return 0;
}

// The median colour of the opaque texels of a sprite in a band of distances
// from its middle (fractions of its radius) and of brightness (0..255 mean), or
// the rows' and columns' extent. 1 = enough texels.
static int RldKit_SpriteBand(const u16 *vram, const u8 *written, const u32 layout[3], double r0, double r1, int lumLo, int lumHi, u8 out[3], double *radius,
                             double *across)
{
	const u32 clut = layout[0] >> 16;
	const u32 tpage = layout[1] >> 16;
	const int u0 = (int)(layout[0] & 0xffu);
	const int v0 = (int)((layout[0] >> 8) & 0xffu);
	const int u1 = (int)(layout[1] & 0xffu);
	const int v2 = (int)((layout[2] >> 8) & 0xffu);
	const int w = u1 - u0 + 1;
	const int h = v2 - v0 + 1;
	double ch[3][1024];
	u32 n = 0;
	int x;
	int y;
	int cols0 = 1 << 30;
	int cols1 = -1;
	int rows0 = 1 << 30;
	int rows1 = -1;
	int k;

	if ((w <= 0) || (h <= 0) || (w * h > 1024))
	{
		return 0;
	}
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			int ok = 0;
			const u32 c = RldKit_TexelWord(vram, written, tpage, clut, u0 + x, v0 + y, &ok);

			if (ok && (c != 0u))
			{
				cols0 = (x < cols0) ? x : cols0;
				cols1 = (x > cols1) ? x : cols1;
				rows0 = (y < rows0) ? y : rows0;
				rows1 = (y > rows1) ? y : rows1;
			}
		}
	}
	if (cols1 < 0)
	{
		return 0;
	}
	if (radius != NULL)
	{
		*radius = 0.5 * (double)(rows1 - rows0 + 1);
	}
	if (across != NULL)
	{
		*across = (double)(cols1 - cols0 + 1);
	}
	{
		const double mx = 0.5 * (double)(cols0 + cols1);
		const double my = 0.5 * (double)(rows0 + rows1);
		const double R = 0.5 * (double)(rows1 - rows0 + 1);

		for (y = 0; y < h; y++)
		{
			for (x = 0; x < w; x++)
			{
				int ok = 0;
				const u32 c = RldKit_TexelWord(vram, written, tpage, clut, u0 + x, v0 + y, &ok);
				const double d = sqrt(((double)x - mx) * ((double)x - mx) + ((double)y - my) * ((double)y - my)) / ((R > 0.0) ? R : 1.0);
				u8 rgba[4];
				int lum;

				if (!ok || (c == 0u) || (d < r0) || (d > r1))
				{
					continue;
				}
				RldKit_WordRgba(c, rgba);
				lum = ((int)rgba[0] + (int)rgba[1] + (int)rgba[2]) / 3;
				if ((lum < lumLo) || (lum > lumHi))
				{
					continue;
				}
				ch[0][n] = rgba[0];
				ch[1][n] = rgba[1];
				ch[2][n] = rgba[2];
				n++;
			}
		}
	}
	if (n < 4u)
	{
		return 0;
	}
	for (k = 0; k < 3; k++)
	{
		const double m = RldKit_Median(ch[k], n);

		out[k] = (u8)(m + 0.5);
	}
	return 1;
}

static void RldKit_Shade(const u8 in[3], double f, u8 out[4])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		const double v = (double)in[k] * f + 0.5;

		out[k] = (u8)((v >= 255.0) ? 255 : ((v <= 0.0) ? 0 : (int)v));
	}
	out[3] = 255;
}

// One sprite decoded to RGBA (alpha 0 = the transparent word 0), malloc'ed,
// with its opaque extent ext = x0 y0 x1 y1 (inclusive). NULL when it is empty.
static u8 *RldKit_SpriteRgba(const u16 *vram, const u8 *written, const u32 layout[3], u32 *w, u32 *h, int ext[4])
{
	const u32 clut = layout[0] >> 16;
	const u32 tpage = layout[1] >> 16;
	const int u0 = (int)(layout[0] & 0xffu);
	const int v0 = (int)((layout[0] >> 8) & 0xffu);
	const int u1 = (int)(layout[1] & 0xffu);
	const int v2 = (int)((layout[2] >> 8) & 0xffu);
	const int sw = u1 - u0 + 1;
	const int sh = v2 - v0 + 1;
	u8 *out;
	int x;
	int y;

	if ((sw <= 0) || (sh <= 0) || (sw > 64) || (sh > 64))
	{
		return NULL;
	}
	out = (u8 *)calloc((size_t)sw * (size_t)sh, 4u);
	if (out == NULL)
	{
		return NULL;
	}
	ext[0] = ext[1] = 1 << 30;
	ext[2] = ext[3] = -1;
	for (y = 0; y < sh; y++)
	{
		for (x = 0; x < sw; x++)
		{
			int ok = 0;
			const u32 c = RldKit_TexelWord(vram, written, tpage, clut, u0 + x, v0 + y, &ok);
			u8 *px = &out[((size_t)y * (size_t)sw + (size_t)x) * 4u];

			RldKit_WordRgba(ok ? c : 0u, px);
			if (px[3] != 0u)
			{
				ext[0] = (x < ext[0]) ? x : ext[0];
				ext[1] = (y < ext[1]) ? y : ext[1];
				ext[2] = (x > ext[2]) ? x : ext[2];
				ext[3] = (y > ext[3]) ? y : ext[3];
			}
		}
	}
	if (ext[2] < 0)
	{
		free(out);
		return NULL;
	}
	*w = (u32)sw;
	*h = (u32)sh;
	return out;
}

// THE WHEEL'S ATLAS (K2b): the opaque extent of the tread sprite at the left,
// the hub sprite's at its right, RLDKIT_TILE_PAD texels apart and from the
// edges, the edges powers of two; every transparent texel takes the mean of its opaque neighbours, round
// after round, until none is left (alpha 255 everywhere: the wheel is opaque);
// then `passes` Scale2x. 1 = made (or no sprites: 1 with no atlas); 0 = out of memory.
static int RldKit_WheelAtlas(RldKitResult *r, const u16 *vram, const u8 *written, const u32 tread[3], const u32 side[3], int passes)
{
	int ext[2][4];
	u32 W = 16u;
	u32 H = 16u;
	u32 x;
	u32 y;
	int k;
	int changed = 1;
	u8 *cur;

	r->wheelSprite[0] = RldKit_SpriteRgba(vram, written, tread, &r->wheelSpriteW[0], &r->wheelSpriteH[0], ext[0]);
	r->wheelSprite[1] = RldKit_SpriteRgba(vram, written, side, &r->wheelSpriteW[1], &r->wheelSpriteH[1], ext[1]);
	if ((r->wheelSprite[0] == NULL) || (r->wheelSprite[1] == NULL))
	{
		free(r->wheelSprite[0]);
		free(r->wheelSprite[1]);
		r->wheelSprite[0] = r->wheelSprite[1] = NULL;
		return 1;
	}
	// Only the opaque extent of each sprite goes in (wheelSpriteAt: where the
	// sprite's own texel 0, 0 lands - outside the atlas when it has a margin).
	{
		const int ow0 = ext[0][2] - ext[0][0] + 1;
		const int ow1 = ext[1][2] - ext[1][0] + 1;
		const int oh0 = ext[0][3] - ext[0][1] + 1;
		const int oh1 = ext[1][3] - ext[1][1] + 1;

		r->wheelSpriteAt[0][0] = RLDKIT_TILE_PAD - ext[0][0];
		r->wheelSpriteAt[0][1] = RLDKIT_TILE_PAD - ext[0][1];
		r->wheelSpriteAt[1][0] = 3 * RLDKIT_TILE_PAD + ow0 - ext[1][0];
		r->wheelSpriteAt[1][1] = RLDKIT_TILE_PAD - ext[1][1];
		while (W < (u32)(4 * RLDKIT_TILE_PAD + ow0 + ow1))
		{
			W *= 2u;
		}
		while (H < (u32)(((oh0 > oh1) ? oh0 : oh1) + 2 * RLDKIT_TILE_PAD))
		{
			H *= 2u;
		}
	}
	r->wheelSourceW = W;
	r->wheelSourceH = H;
	r->wheelAtlasSource = (u8 *)calloc((size_t)W * H, 4u);
	if (r->wheelAtlasSource == NULL)
	{
		return 0;
	}
	for (k = 0; k < 2; k++)
	{
		for (y = (u32)ext[k][1]; y <= (u32)ext[k][3]; y++)
		{
			memcpy(&r->wheelAtlasSource[((size_t)(r->wheelSpriteAt[k][1] + (int)y) * W + (size_t)(r->wheelSpriteAt[k][0] + ext[k][0])) * 4u],
			       &r->wheelSprite[k][((size_t)y * r->wheelSpriteW[k] + (size_t)ext[k][0]) * 4u], (size_t)(ext[k][2] - ext[k][0] + 1) * 4u);
		}
	}
	r->treadBox[0] = (double)(r->wheelSpriteAt[0][0] + ext[0][0]);
	r->treadBox[1] = (double)(r->wheelSpriteAt[0][1] + ext[0][1]);
	r->treadBox[2] = (double)(r->wheelSpriteAt[0][0] + ext[0][2] + 1);
	r->treadBox[3] = (double)(r->wheelSpriteAt[0][1] + ext[0][3] + 1);
	r->sideCentre[0] = (double)r->wheelSpriteAt[1][0] + 0.5 * (double)(ext[1][0] + ext[1][2] + 1);
	r->sideCentre[1] = (double)r->wheelSpriteAt[1][1] + 0.5 * (double)(ext[1][1] + ext[1][3] + 1);
	r->sideRadius = 0.5 * (double)(ext[1][3] - ext[1][1] + 1);
	// The transparent texels filled (a fixed order: the same everywhere).
	while (changed)
	{
		u8 *next = (u8 *)malloc((size_t)W * H * 4u);

		if (next == NULL)
		{
			return 0;
		}
		memcpy(next, r->wheelAtlasSource, (size_t)W * H * 4u);
		changed = 0;
		for (y = 0; y < H; y++)
		{
			for (x = 0; x < W; x++)
			{
				static const int dx[4] = {-1, 1, 0, 0};
				static const int dy[4] = {0, 0, -1, 1};
				u32 sum[3] = {0u, 0u, 0u};
				u32 n = 0;
				int d;

				if (r->wheelAtlasSource[((size_t)y * W + x) * 4u + 3u] != 0u)
				{
					continue;
				}
				for (d = 0; d < 4; d++)
				{
					const int nx = (int)x + dx[d];
					const int ny = (int)y + dy[d];
					const u8 *q;

					if ((nx < 0) || (ny < 0) || (nx >= (int)W) || (ny >= (int)H))
					{
						continue;
					}
					q = &r->wheelAtlasSource[((size_t)ny * W + (size_t)nx) * 4u];
					if (q[3] != 0u)
					{
						sum[0] += q[0];
						sum[1] += q[1];
						sum[2] += q[2];
						n++;
					}
				}
				if (n > 0u)
				{
					u8 *o = &next[((size_t)y * W + x) * 4u];

					o[0] = (u8)((sum[0] + n / 2u) / n);
					o[1] = (u8)((sum[1] + n / 2u) / n);
					o[2] = (u8)((sum[2] + n / 2u) / n);
					o[3] = 255u;
					changed = 1;
				}
			}
		}
		free(r->wheelAtlasSource);
		r->wheelAtlasSource = next;
	}
	cur = (u8 *)malloc((size_t)W * H * 4u);
	if (cur == NULL)
	{
		return 0;
	}
	memcpy(cur, r->wheelAtlasSource, (size_t)W * H * 4u);
	for (k = 0; k < passes; k++)
	{
		u8 *next = (u8 *)malloc((size_t)W * H * 16u);

		if (next == NULL)
		{
			free(cur);
			return 0;
		}
		RldKit_Scale2x(cur, W, H, next);
		free(cur);
		cur = next;
		W *= 2u;
		H *= 2u;
	}
	r->wheelAtlas = cur;
	r->wheelAtlasW = W;
	r->wheelAtlasH = H;
	return 1;
}

// The wheel: a profile turned about X (positions in model units here, the mesh
// in metres): the inner side (x -h, a disc to 0.9 R), a rounded shoulder, the
// tread (|x| <= 0.8 h, radius R), the outer shoulder, the outer side down to
// 0.6 R, the rim dished in by one unit to 0.15 R, the hub. With the wheel's
// atlas: the tread and the shoulders take the tread sprite by height (THE
// WHEELS), the sides the hub sprite laid flat (z, y); grey 128. Without it
// flat colours by part. A ring that ends at the axis is a fan. 1 = made.
static int RldKit_WheelMesh(RldKitResult *r)
{
	enum
	{
		K_SIDE = 0,
		K_SHOULDER,
		K_TREAD,
		K_RIMEDGE,
		K_RIM,
		K_HUB
	};
	const double R = r->wheelRadius * RLDKIT_UNITS_PER_METRE;
	const double h = 0.5 * r->wheelWidth * RLDKIT_UNITS_PER_METRE;
	const double px[9] = {-h, -h, -0.8 * h, 0.8 * h, h, h, h - 1.0, h - 1.0, h - 1.0};
	const double pr[9] = {0.0, 0.9 * R, R, R, 0.9 * R, 0.6 * R, 0.5 * R, 0.15 * R, 0.0};
	const int pk[9] = {K_SIDE, K_SIDE, K_SHOULDER, K_TREAD, K_SHOULDER, K_SIDE, K_RIMEDGE, K_RIM, K_HUB};
	const int textured = (r->wheelAtlas != NULL);
	const u32 segs = 8u;
	const u32 verts = segs * (RLDKIT_TIRE_SEG + 1u) * 2u;
	const u32 idx = segs * RLDKIT_TIRE_SEG * 6u;
	const u8 grey[4] = {128, 128, 128, 255};
	u32 vi = 0;
	u32 ii = 0;
	u32 seg;
	int s;

	if (!RldKit_MeshAlloc(&r->wheel, verts, idx))
	{
		return 0;
	}
	for (seg = 0; seg < segs; seg++)
	{
		const double dx = px[seg + 1u] - px[seg];
		const double dr = pr[seg + 1u] - pr[seg];
		double nx = dr; // the profile's outward normal: (dr, -dx) in (x, r)
		double nr = -dx;
		const double l = sqrt(nx * nx + nr * nr);
		const int kind = pk[seg + 1u];
		// the tread sprite on the shoulders and the tread, the hub sprite on the sides
		const int treadMap = (seg >= 1u) && (seg <= 3u);
		u8 col[4];
		const u32 base = vi;

		if (l > 0.0)
		{
			nx /= l;
			nr /= l;
		}
		switch (kind)
		{
		case K_SIDE:
			RldKit_Shade(r->tyre, 0.85, col);
			break;
		case K_SHOULDER:
			RldKit_Shade(r->tyre, 0.92, col);
			break;
		case K_RIMEDGE:
			RldKit_Shade(r->rim, 0.80, col);
			break;
		case K_RIM:
			RldKit_Shade(r->rim, 1.0, col);
			break;
		case K_HUB:
			RldKit_Shade(r->hub, 1.0, col);
			break;
		default:
			RldKit_Shade(r->tyre, 1.0, col);
			break;
		}
		for (s = 0; s <= RLDKIT_TIRE_SEG; s++)
		{
			double sn;
			double cs;
			int end;

			RldKit_SinCos(360.0 * (double)(s % RLDKIT_TIRE_SEG) / (double)RLDKIT_TIRE_SEG, &sn, &cs);
			for (end = 0; end < 2; end++)
			{
				const double p[3] = {px[seg + (u32)end], pr[seg + (u32)end] * cs, pr[seg + (u32)end] * sn};
				const double n[3] = {nx, nr * cs, nr * sn};
				double u = 0.0;
				double v = 0.0;

				if (textured && treadMap)
				{
					// across the axle -> the sprite's columns, the height -> its rows
					u = r->treadBox[0] + (p[0] + h) / (2.0 * h) * (r->treadBox[2] - r->treadBox[0]);
					v = r->treadBox[1] + (R - p[1]) / (2.0 * R) * (r->treadBox[3] - r->treadBox[1]);
				}
				else if (textured)
				{
					// the hub sprite laid flat: z -> columns, y -> rows (up is the top)
					u = r->sideCentre[0] + p[2] / R * r->sideRadius;
					v = r->sideCentre[1] - p[1] / R * r->sideRadius;
				}
				RldKit_SetVertex(&r->wheel.v[vi++], p, RLDKIT_UNITS_PER_METRE, n, textured ? (u / (double)r->wheelSourceW) : 0.0,
				                 textured ? (v / (double)r->wheelSourceH) : 0.0, textured ? grey : col);
			}
		}
		for (s = 0; s < RLDKIT_TIRE_SEG; s++)
		{
			const u32 a = base + 2u * (u32)s;
			const u32 b = a + 1u;
			const u32 c = a + 2u;
			const u32 d = a + 3u;

			// faces out: (seg start, seg end) along the profile, then around +X;
			// a ring that starts or ends at the axis gives one triangle per step
			if (pr[seg] > 0.0)
			{
				r->wheel.index[ii++] = a;
				r->wheel.index[ii++] = c;
				r->wheel.index[ii++] = b;
			}
			if (pr[seg + 1u] > 0.0)
			{
				r->wheel.index[ii++] = b;
				r->wheel.index[ii++] = c;
				r->wheel.index[ii++] = d;
			}
		}
	}
	r->wheel.vertices = vi;
	r->wheel.indices = ii;
	r->wheel.textured = textured;
	return 1;
}

// The steering wheel: a ring (torus) about its centre with three spokes and a
// hub; the ring's axis is ringNormal, positions relative to the centre.
static int RldKit_SteerMesh(RldKitResult *r)
{
	const double Rr = r->ringRadius * RLDKIT_UNITS_PER_METRE;
	const double tube = RLDKIT_RING_TUBE;
	const u32 ringV = (RLDKIT_RING_SEG + 1u) * (RLDKIT_RING_SIDES + 1u);
	const u32 ringI = RLDKIT_RING_SEG * RLDKIT_RING_SIDES * 6u;
	const u32 spokeV = 3u * 24u;
	const u32 spokeI = 3u * 36u;
	double n[3];
	double e1[3];
	double e2[3];
	const double xAxis[3] = {1.0, 0.0, 0.0};
	u32 vi = 0;
	u32 ii = 0;
	int i;
	int j;
	u8 ringCol[4];
	u8 spokeCol[4];

	memcpy(n, r->ringNormal, sizeof(n));
	{
		const double d = RldKit_Dot(xAxis, n);

		for (i = 0; i < 3; i++)
		{
			e1[i] = xAxis[i] - d * n[i];
		}
	}
	if (!RldKit_Normalize(e1))
	{
		e1[0] = 1.0;
		e1[1] = 0.0;
		e1[2] = 0.0;
	}
	RldKit_Cross(n, e1, e2);
	RldKit_Shade(r->ring, 1.0, ringCol);
	RldKit_Shade(r->ring, 1.6, spokeCol);
	if (!RldKit_MeshAlloc(&r->steer, ringV + spokeV, ringI + spokeI))
	{
		return 0;
	}
	for (i = 0; i <= RLDKIT_RING_SEG; i++)
	{
		double sa;
		double ca;

		RldKit_SinCos(360.0 * (double)(i % RLDKIT_RING_SEG) / (double)RLDKIT_RING_SEG, &sa, &ca);
		for (j = 0; j <= RLDKIT_RING_SIDES; j++)
		{
			double sb;
			double cb;
			double radial[3];
			double p[3];
			double nn[3];
			int k;

			RldKit_SinCos(360.0 * (double)(j % RLDKIT_RING_SIDES) / (double)RLDKIT_RING_SIDES, &sb, &cb);
			for (k = 0; k < 3; k++)
			{
				radial[k] = ca * e1[k] + sa * e2[k];
				nn[k] = cb * radial[k] + sb * n[k];
				p[k] = (Rr + tube * cb) * radial[k] + tube * sb * n[k];
			}
			RldKit_SetVertex(&r->steer.v[vi++], p, RLDKIT_UNITS_PER_METRE, nn, 0.0, 0.0, ringCol);
		}
	}
	for (i = 0; i < RLDKIT_RING_SEG; i++)
	{
		for (j = 0; j < RLDKIT_RING_SIDES; j++)
		{
			const u32 a = (u32)i * (RLDKIT_RING_SIDES + 1u) + (u32)j;
			const u32 b = a + RLDKIT_RING_SIDES + 1u;

			r->steer.index[ii++] = a;
			r->steer.index[ii++] = b;
			r->steer.index[ii++] = a + 1u;
			r->steer.index[ii++] = a + 1u;
			r->steer.index[ii++] = b;
			r->steer.index[ii++] = b + 1u;
		}
	}
	// Three spokes (boxes) from the middle to the ring at 90, 210 and 330 degrees.
	for (i = 0; i < 3; i++)
	{
		static const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
		double sa;
		double ca;
		double dir[3];
		double side[3];
		double corner[8][3];
		int f;
		int k;

		RldKit_SinCos(90.0 + 120.0 * (double)i, &sa, &ca);
		for (k = 0; k < 3; k++)
		{
			dir[k] = ca * e1[k] + sa * e2[k];
		}
		RldKit_Cross(n, dir, side);
		for (j = 0; j < 8; j++)
		{
			const double along = (j & 1) ? (Rr - 0.5 * tube) : 0.0;
			const double across = (j & 2) ? 0.6 : -0.6;
			const double up = (j & 4) ? 0.5 : -0.5;

			for (k = 0; k < 3; k++)
			{
				corner[j][k] = along * dir[k] + across * side[k] + up * n[k];
			}
		}
		for (f = 0; f < 6; f++)
		{
			double fn[3];
			double ab[3];
			double ac[3];
			const u32 b0 = vi;

			for (k = 0; k < 3; k++)
			{
				ab[k] = corner[faces[f][1]][k] - corner[faces[f][0]][k];
				ac[k] = corner[faces[f][3]][k] - corner[faces[f][0]][k];
			}
			RldKit_Cross(ab, ac, fn);
			RldKit_Normalize(fn);
			for (k = 0; k < 4; k++)
			{
				RldKit_SetVertex(&r->steer.v[vi++], corner[faces[f][k]], RLDKIT_UNITS_PER_METRE, fn, 0.0, 0.0, spokeCol);
			}
			// orient so the normal points away from the spoke's middle
			{
				double mid[3];
				double fc[3];
				int out;

				for (k = 0; k < 3; k++)
				{
					mid[k] = 0.5 * (Rr - 0.5 * tube) * dir[k];
					fc[k] = 0.25 * (corner[faces[f][0]][k] + corner[faces[f][1]][k] + corner[faces[f][2]][k] + corner[faces[f][3]][k]) - mid[k];
				}
				out = RldKit_Dot(fc, fn) >= 0.0;
				if (!out)
				{
					for (k = 0; k < 4; k++)
					{
						r->steer.v[b0 + (u32)k].n[0] = -r->steer.v[b0 + (u32)k].n[0];
						r->steer.v[b0 + (u32)k].n[1] = -r->steer.v[b0 + (u32)k].n[1];
						r->steer.v[b0 + (u32)k].n[2] = -r->steer.v[b0 + (u32)k].n[2];
					}
				}
				r->steer.index[ii++] = b0;
				r->steer.index[ii++] = out ? b0 + 1u : b0 + 2u;
				r->steer.index[ii++] = out ? b0 + 2u : b0 + 1u;
				r->steer.index[ii++] = b0;
				r->steer.index[ii++] = out ? b0 + 2u : b0 + 3u;
				r->steer.index[ii++] = out ? b0 + 3u : b0 + 2u;
			}
		}
	}
	r->steer.vertices = vi;
	r->steer.indices = ii;
	r->steer.textured = 0;
	return 1;
}

//========================================================================================
// THE WHOLE KIT
//========================================================================================

static void RldKit_ResultFree(RldKitResult *r)
{
	RldKit_MeshFree(&r->kart);
	RldKit_MeshFree(&r->flat);
	RldKit_MeshFree(&r->steer);
	RldKit_MeshFree(&r->wheel);
	free(r->wheelAtlas);
	free(r->wheelAtlasSource);
	free(r->wheelSprite[0]);
	free(r->wheelSprite[1]);
	free(r->atlas);
	free(r->atlasSource);
	free(r->tile);
	memset(r, 0, sizeof(*r));
}

// The tile of a (page, CLUT) in the list, added when new.
static int RldKit_TileOf(RldKitTile *tiles, int *count, int max, u32 tpage, u32 clut)
{
	int i;

	for (i = 0; i < *count; i++)
	{
		if ((tiles[i].tpage == (u16)tpage) && (tiles[i].clut == (u16)clut))
		{
			return i;
		}
	}
	if (*count >= max)
	{
		return -1;
	}
	tiles[*count].tpage = (u16)tpage;
	tiles[*count].clut = (u16)clut;
	tiles[*count].u0 = tiles[*count].v0 = 1 << 20;
	tiles[*count].u1 = tiles[*count].v1 = -1;
	return (*count)++;
}

// THE KART of driver `donor` among the 15 decoded models: isKart[t] = 1 for
// each of its triangles that is still in the turn and found in at least
// RLDKIT_SHARED_MIN of the other drivers (RLDKIT_MATCH, else RLDKIT_MATCH_LOOSE).
// The counts go to r (stillTriangles, kartShared, kartLoose, dropped).
static void RldKit_KartMask(const RldKitRetail *all, int donor, u8 *isKart, RldKitResult *r)
{
	const RldKitRetail *m = &all[donor];
	u32 t;
	int d;

	for (t = 0; t < m->tris; t++)
	{
		double key[3][3];
		int shared = 0;
		int loose = 0;

		if (!RldKit_TriStill(m, t))
		{
			continue;
		}
		r->stillTriangles++;
		RldKit_SortedCorners(m, t, key);
		for (d = 0; d < RLDKIT_DRIVERS; d++)
		{
			if (d == donor)
			{
				continue;
			}
			if (RldKit_HasTriangle(&all[d], key, RLDKIT_MATCH))
			{
				shared++;
				loose++;
			}
			else if (RldKit_HasTriangle(&all[d], key, RLDKIT_MATCH_LOOSE))
			{
				loose++;
			}
		}
		if (shared >= RLDKIT_SHARED_MIN)
		{
			isKart[t] = 1;
			r->kartShared++;
		}
		else if (loose >= RLDKIT_SHARED_MIN)
		{
			isKart[t] = 1;
			r->kartLoose++;
		}
		else
		{
			r->dropped++;
		}
	}
}

// The kit of driver opt->driver. 1 = r filled (free with RldKit_ResultFree),
// 0 with why (r is empty).
static int RldKit_Build(const RldKitSource *src, const RldKitOptions *opt, RldKitResult *r, char *why, size_t whySize)
{
	static RldKitRetail all[RLDKIT_DRIVERS];
	RldKitRetail *m;
	u16 *vram = NULL;
	u8 *written = NULL;
	RldKitTri *tri = NULL;
	RldKitPn *pn = NULL;
	u8 *isKart = NULL;
	int segments;
	int passes;
	int d;
	u32 t;
	u32 nk = 0;
	int ok = 0;
	u32 wheelTread[3];
	u32 wheelSide[3];
	int wheelSprites = 0;

	memset(r, 0, sizeof(*r));
	if ((src == NULL) || (opt == NULL) || (opt->driver < 0) || (opt->driver >= RLDKIT_DRIVERS))
	{
		return RldKit_Fail(why, whySize, "the driver must be 0..14 (15, N. Oxide, has no kart)");
	}
	segments = (opt->segments == 1 || opt->segments == 2 || opt->segments == 4 || opt->segments == 8) ? opt->segments : 4;
	passes = (opt->upscale >= 4) ? 2 : ((opt->upscale >= 2) ? 1 : 0);
	memset(all, 0, sizeof(all));
	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		char w[160];

		if (!RldKit_RetailDecode(src->model[d], src->modelSize[d], &all[d], w, sizeof(w)))
		{
			char text[200];

			snprintf(text, sizeof(text), "driver %d (BIGFILE entry %u): %s", d, (unsigned)(RLDKIT_BIG_MODEL_HI + (u32)d), w);
			RldKit_Fail(why, whySize, text);
			goto done;
		}
	}
	m = &all[opt->driver];
	if (m->frames <= 20u)
	{
		RldKit_Fail(why, whySize, "the donor's turn has fewer than 21 frames - no kart can be told apart");
		goto done;
	}
	memcpy(r->model, m->name, sizeof(r->model));
	r->commands = m->commands;
	r->records = m->records;
	r->triangles = m->tris;

	vram = (u16 *)malloc(sizeof(u16) * RLDKIT_VRAM_W * RLDKIT_VRAM_H);
	written = (u8 *)malloc((size_t)RLDKIT_VRAM_W * RLDKIT_VRAM_H);
	tri = (RldKitTri *)calloc(m->tris ? m->tris : 1u, sizeof(RldKitTri));
	isKart = (u8 *)calloc(m->tris ? m->tris : 1u, 1);
	r->tile = (RldKitTile *)calloc(64, sizeof(RldKitTile));
	if ((vram == NULL) || (written == NULL) || (tri == NULL) || (isKart == NULL) || (r->tile == NULL))
	{
		RldKit_Fail(why, whySize, "out of memory");
		goto done;
	}
	if (!RldKit_Vram(src->vrm, src->vrmSize, vram, written))
	{
		RldKit_Fail(why, whySize, "shared.vrm (BIGFILE entry 258) holds no picture");
		goto done;
	}

	// THE KART: still triangles shared with other drivers.
	RldKit_KartMask(all, opt->driver, isKart, r);

	// The kart triangles with their looks; the tiles' UV boxes.
	for (t = 0; t < m->tris; t++)
	{
		RldKitTri *k = &tri[nk];
		u32 w[3];
		int c;
		const int listed = (m->triCmd[t] & 0x20000000u) != 0u;
		// the facing order (see the top of the file): as stored, or reversed
		static const int order[2][3] = {{0, 2, 1}, {0, 1, 2}};
		const int *fr;

		if (!isKart[t])
		{
			continue;
		}
		k->twoSided = (m->triCmd[t] & 0x10000000u) == 0u;
		fr = order[(k->twoSided || listed) ? 1 : 0];
		for (c = 0; c < 3; c++)
		{
			const int from = fr[c];

			RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, m->triRec[t * 3u + (u32)from], k->p[c]);
			RldKit_Colour(m, m->triCol[t * 3u + (u32)from], k->c[c]);
		}
		k->tile = -1;
		if (RldKit_Layout(m, t, w))
		{
			const u32 uvw[3] = {w[0] & 0xffffu, w[1] & 0xffffu, w[2] & 0xffffu};
			RldKitTile *tl;

			k->textured = 1;
			k->clut = (u16)(w[0] >> 16);
			k->tpage = (u16)((w[1] >> 16) & 0x1ffu);
			k->tile = RldKit_TileOf(r->tile, &r->tiles, 64, k->tpage, k->clut);
			if (k->tile < 0)
			{
				RldKit_Fail(why, whySize, "the kart samples more than 64 (page, CLUT) tiles");
				goto done;
			}
			tl = &r->tile[k->tile];
			for (c = 0; c < 3; c++)
			{
				k->uv[c][0] = (u8)(uvw[fr[c]] & 0xffu);
				k->uv[c][1] = (u8)((uvw[fr[c]] >> 8) & 0xffu);
				tl->u0 = (k->uv[c][0] < tl->u0) ? k->uv[c][0] : tl->u0;
				tl->v0 = (k->uv[c][1] < tl->v0) ? k->uv[c][1] : tl->v0;
				tl->u1 = (k->uv[c][0] > tl->u1) ? k->uv[c][0] : tl->u1;
				tl->v1 = (k->uv[c][1] > tl->v1) ? k->uv[c][1] : tl->v1;
			}
		}
		nk++;
	}
	r->kartTriangles = nk;
	if (nk == 0u)
	{
		RldKit_Fail(why, whySize, "no kart triangle was found");
		goto done;
	}

	// THE ATLAS: shelves of RLDKIT_ATLAS_W texels, each tile with its pad.
	{
		int x = 0;
		int y = 0;
		int shelf = 0;
		int i;
		u32 H;
		u32 X;
		u32 Y;

		for (i = 0; i < r->tiles; i++)
		{
			RldKitTile *tl = &r->tile[i];
			const int w = tl->u1 - tl->u0 + 1 + 2 * RLDKIT_TILE_PAD;
			const int h = tl->v1 - tl->v0 + 1 + 2 * RLDKIT_TILE_PAD;

			if (w > RLDKIT_ATLAS_W)
			{
				RldKit_Fail(why, whySize, "a kart tile is wider than the atlas");
				goto done;
			}
			if (x + w > RLDKIT_ATLAS_W)
			{
				x = 0;
				y += shelf;
				shelf = 0;
			}
			tl->ax = x + RLDKIT_TILE_PAD;
			tl->ay = y + RLDKIT_TILE_PAD;
			x += w;
			shelf = (h > shelf) ? h : shelf;
		}
		H = 16u;
		while (H < (u32)(y + shelf))
		{
			H *= 2u;
		}
		if (H > RLDKIT_ATLAS_H_MAX)
		{
			RldKit_Fail(why, whySize, "the kart's textures do not fit the atlas");
			goto done;
		}
		r->sourceW = RLDKIT_ATLAS_W;
		r->sourceH = H;
		r->atlasSource = (u8 *)calloc((size_t)r->sourceW * r->sourceH, 4u);
		if (r->atlasSource == NULL)
		{
			RldKit_Fail(why, whySize, "out of memory");
			goto done;
		}
		for (i = 0; i < r->tiles; i++)
		{
			const RldKitTile *tl = &r->tile[i];
			int yy;
			int xx;

			for (yy = -RLDKIT_TILE_PAD; yy <= tl->v1 - tl->v0 + RLDKIT_TILE_PAD; yy++)
			{
				for (xx = -RLDKIT_TILE_PAD; xx <= tl->u1 - tl->u0 + RLDKIT_TILE_PAD; xx++)
				{
					const int su = tl->u0 + ((xx < 0) ? 0 : ((xx > tl->u1 - tl->u0) ? (tl->u1 - tl->u0) : xx));
					const int sv = tl->v0 + ((yy < 0) ? 0 : ((yy > tl->v1 - tl->v0) ? (tl->v1 - tl->v0) : yy));
					int okT = 0;
					const u32 word = RldKit_TexelWord(vram, written, tl->tpage, tl->clut, su, sv, &okT);
					u8 *px = &r->atlasSource[((size_t)(tl->ay + yy) * r->sourceW + (u32)(tl->ax + xx)) * 4u];

					RldKit_WordRgba(okT ? word : 0x7fffu, px);
					if ((px[3] == 0u) && (xx >= 0) && (yy >= 0) && (xx <= tl->u1 - tl->u0) && (yy <= tl->v1 - tl->v0))
					{
						r->atlasAlpha = 1;
					}
				}
			}
		}
		// the upscale
		X = r->sourceW;
		Y = r->sourceH;
		{
			u8 *cur = (u8 *)malloc((size_t)X * Y * 4u);
			int p;

			if (cur == NULL)
			{
				RldKit_Fail(why, whySize, "out of memory");
				goto done;
			}
			memcpy(cur, r->atlasSource, (size_t)X * Y * 4u);
			for (p = 0; p < passes; p++)
			{
				u8 *next = (u8 *)malloc((size_t)X * Y * 16u);

				if (next == NULL)
				{
					free(cur);
					RldKit_Fail(why, whySize, "out of memory");
					goto done;
				}
				RldKit_Scale2x(cur, X, Y, next);
				free(cur);
				cur = next;
				X *= 2u;
				Y *= 2u;
			}
			r->atlas = cur;
			r->atlasW = X;
			r->atlasH = Y;
		}
	}

	// THE PN PATCHES and the meshes (kart smoothed, kart flat).
	pn = (RldKitPn *)calloc(nk, sizeof(RldKitPn));
	if (pn == NULL)
	{
		RldKit_Fail(why, whySize, "out of memory");
		goto done;
	}
	for (t = 0; t < nk; t++)
	{
		int c;

		for (c = 0; c < 3; c++)
		{
			memcpy(pn[t].p[c], tri[t].p[c], sizeof(pn[t].p[c]));
			pn[t].c[c][0] = tri[t].c[c][0];
			pn[t].c[c][1] = tri[t].c[c][1];
			pn[t].c[c][2] = tri[t].c[c][2];
			pn[t].c[c][3] = 255;
			if (tri[t].tile >= 0)
			{
				const RldKitTile *tl = &r->tile[tri[t].tile];

				pn[t].uv[c][0] = ((double)tl->ax + (double)(tri[t].uv[c][0] - tl->u0)) / (double)r->sourceW;
				pn[t].uv[c][1] = ((double)tl->ay + (double)(tri[t].uv[c][1] - tl->v0)) / (double)r->sourceH;
			}
		}
	}
	if (!RldKit_PnBuild(pn, nk, r))
	{
		RldKit_Fail(why, whySize, "out of memory");
		goto done;
	}
	{
		const u32 perTri = (u32)((segments + 1) * (segments + 2) / 2);
		const u32 trisPer = (u32)(segments * segments);
		u32 vi = 0;
		u32 ii = 0;

		// room for a second, turned copy of every cockpit floor triangle (below)
		if (!RldKit_MeshAlloc(&r->kart, 2u * nk * perTri, 2u * nk * trisPer * 3u) || !RldKit_MeshAlloc(&r->flat, nk * 3u, nk * 3u))
		{
			RldKit_Fail(why, whySize, "out of memory");
			goto done;
		}
		r->maxDeviation = 0.0;
		for (t = 0; t < nk; t++)
		{
			const u32 base = vi;
			double flatOff = 0.0;
			int i;
			int j;
			int c;
			double inner = 1.0;

			// the inside's bulge (beyond the edges' curve), limited
			for (i = 1; i < segments; i++)
			{
				for (j = 1; i + j < segments; j++)
				{
					const double w1 = (double)i / segments;
					const double w2 = (double)j / segments;
					const double w0 = 1.0 - w1 - w2;
					double q[3];
					const double dist = (RldKit_PnPoint(&pn[t], w0, w1, w2, q), RldKit_PointTriangle(q, pn[t].p[0], pn[t].p[1], pn[t].p[2]));

					flatOff = (dist > flatOff) ? dist : flatOff;
				}
			}
			if (flatOff > RLDKIT_BULGE_MAX)
			{
				inner = RLDKIT_BULGE_MAX / flatOff;
				r->bulgeLimited++;
			}
			for (i = 0; i <= segments; i++)
			{
				for (j = 0; i + j <= segments; j++)
				{
					const double w1 = (double)i / segments;
					const double w2 = (double)j / segments;
					const double w0 = 1.0 - w1 - w2;
					double q[3];
					double lin[3];
					double nn[3];
					double uv[2];
					u8 col[4];
					int k;

					RldKit_PnPoint(&pn[t], w0, w1, w2, q);
					for (k = 0; k < 3; k++)
					{
						lin[k] = w0 * pn[t].p[0][k] + w1 * pn[t].p[1][k] + w2 * pn[t].p[2][k];
						nn[k] = w0 * pn[t].n[0][k] + w1 * pn[t].n[1][k] + w2 * pn[t].n[2][k];
					}
					if ((i > 0) && (j > 0) && (i + j < segments))
					{
						for (k = 0; k < 3; k++)
						{
							q[k] = lin[k] + inner * (q[k] - lin[k]);
						}
					}
					if ((i == 0) && (j == 0))
					{
						memcpy(q, pn[t].p[0], sizeof(q));
					}
					else if ((i == segments) && (j == 0))
					{
						memcpy(q, pn[t].p[1], sizeof(q));
					}
					else if ((i == 0) && (j == segments))
					{
						memcpy(q, pn[t].p[2], sizeof(q));
					}
					if (!RldKit_Normalize(nn))
					{
						memcpy(nn, pn[t].face, sizeof(nn));
					}
					{
						const double dev = RldKit_PointTriangle(q, pn[t].p[0], pn[t].p[1], pn[t].p[2]);

						r->maxDeviation = (dev > r->maxDeviation) ? dev : r->maxDeviation;
					}
					uv[0] = w0 * pn[t].uv[0][0] + w1 * pn[t].uv[1][0] + w2 * pn[t].uv[2][0];
					uv[1] = w0 * pn[t].uv[0][1] + w1 * pn[t].uv[1][1] + w2 * pn[t].uv[2][1];
					for (k = 0; k < 3; k++)
					{
						const double v = w0 * pn[t].c[0][k] + w1 * pn[t].c[1][k] + w2 * pn[t].c[2][k] + 0.5;

						col[k] = (u8)((v >= 255.0) ? 255 : (int)v);
					}
					col[3] = 255;
					RldKit_SetVertex(&r->kart.v[vi++], q, RLDKIT_UNITS_PER_METRE, nn, uv[0], uv[1], col);
				}
			}
			// the grid's triangles: row i (corner 1's weight), column j (corner 2's)
			for (i = 0; i < segments; i++)
			{
				for (j = 0; i + j < segments; j++)
				{
					// index of (i, j) in the triangle grid
#define RLDKIT_GI(a, b) (base + (u32)((a) * (segments + 1) - ((a) * ((a)-1)) / 2 + (b)))
					const u32 a0 = RLDKIT_GI(i, j);
					const u32 a1 = RLDKIT_GI(i + 1, j);
					const u32 a2 = RLDKIT_GI(i, j + 1);

					r->kart.index[ii++] = a0;
					r->kart.index[ii++] = a1;
					r->kart.index[ii++] = a2;
					if (i + j + 1 < segments)
					{
						const u32 a3 = RLDKIT_GI(i + 1, j + 1);

						r->kart.index[ii++] = a1;
						r->kart.index[ii++] = a3;
						r->kart.index[ii++] = a2;
					}
#undef RLDKIT_GI
				}
			}
			// THE COCKPIT FLOOR: the retail floor plate under the driver faces
			// down only (the driver's body hid its top). Without the figure the kit
			// shows the same plate from above too: a turned copy of its triangles
			// (the same surface, normals flipped); the shape does not change.
			{
				const double cx = (pn[t].p[0][0] + pn[t].p[1][0] + pn[t].p[2][0]) / 3.0;
				const double cy = (pn[t].p[0][1] + pn[t].p[1][1] + pn[t].p[2][1]) / 3.0;
				const double cz = (pn[t].p[0][2] + pn[t].p[1][2] + pn[t].p[2][2]) / 3.0;

				if ((pn[t].face[1] < -0.5) && (fabs(cx) < RLDKIT_FLOOR_HALF_X) && (cy < RLDKIT_FLOOR_Y_MAX) && (fabs(cz) < RLDKIT_FLOOR_HALF_Z))
				{
					const u32 count = vi - base;
					const u32 firstIndex = ii - trisPer * 3u;
					u32 q;

					for (q = 0; q < count; q++)
					{
						r->kart.v[vi + q] = r->kart.v[base + q];
						r->kart.v[vi + q].n[0] = -r->kart.v[vi + q].n[0];
						r->kart.v[vi + q].n[1] = -r->kart.v[vi + q].n[1];
						r->kart.v[vi + q].n[2] = -r->kart.v[vi + q].n[2];
					}
					for (q = 0; q < trisPer * 3u; q += 3u)
					{
						r->kart.index[ii + q] = r->kart.index[firstIndex + q] - base + vi;
						r->kart.index[ii + q + 1u] = r->kart.index[firstIndex + q + 2u] - base + vi;
						r->kart.index[ii + q + 2u] = r->kart.index[firstIndex + q + 1u] - base + vi;
					}
					vi += count;
					ii += trisPer * 3u;
					r->floorDoubled++;
				}
			}
			// the flat retail triangle
			for (c = 0; c < 3; c++)
			{
				RldKit_SetVertex(&r->flat.v[t * 3u + (u32)c], pn[t].p[c], RLDKIT_UNITS_PER_METRE, pn[t].face, pn[t].uv[c][0], pn[t].uv[c][1], pn[t].c[c]);
				r->flat.index[t * 3u + (u32)c] = t * 3u + (u32)c;
			}
		}
		r->kart.vertices = vi;
		r->kart.indices = ii;
		r->kart.textured = 1;
		r->flat.vertices = nk * 3u;
		r->flat.indices = nk * 3u;
		r->flat.textured = 1;
	}

	// The floor line: per plane z the lowest point of the retail kart's surface.
	{
		int zi;

		r->floorCount = 0;
		for (zi = 0; zi < RLDKIT_FLOOR_LINE; zi++)
		{
			const double z = s_rldKitFloorZ[zi];
			double low = 1e30;

			for (t = 0; t < nk; t++)
			{
				int e;

				for (e = 0; e < 3; e++)
				{
					const double *a = tri[t].p[e];
					const double *b = tri[t].p[(e + 1) % 3];

					if (((a[2] - z) * (b[2] - z) <= 0.0) && (a[2] != b[2]))
					{
						const double f = (z - a[2]) / (b[2] - a[2]);
						const double x = a[0] + f * (b[0] - a[0]);
						const double y = a[1] + f * (b[1] - a[1]);

						if ((fabs(x) <= RLDKIT_KART_SIDE_X) && (y < low))
						{
							low = y;
						}
					}
				}
			}
			if (low < 1e29)
			{
				r->floorLine[r->floorCount][0] = z;
				r->floorLine[r->floorCount][1] = low;
				r->floorCount++;
			}
		}
	}

	// The kart's box, the floor under the seat, the seat.
	{
		u32 i;
		int k;

		for (k = 0; k < 3; k++)
		{
			r->kartMin[k] = 1e30;
			r->kartMax[k] = -1e30;
		}
		for (i = 0; i < r->kart.vertices; i++)
		{
			for (k = 0; k < 3; k++)
			{
				const double v = (double)r->kart.v[i].p[k];

				r->kartMin[k] = (v < r->kartMin[k]) ? v : r->kartMin[k];
				r->kartMax[k] = (v > r->kartMax[k]) ? v : r->kartMax[k];
			}
		}
		// a ray down at (0, 20, seat z): the highest kart face below
		r->floorY = -1.0;
		for (t = 0; t < nk; t++)
		{
			const double *a = tri[t].p[0];
			const double *b = tri[t].p[1];
			const double *c = tri[t].p[2];
			const double qx = 0.0;
			const double qz = RLDKIT_SEAT_Z;
			const double den = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);

			if (fabs(den) < 1e-9)
			{
				continue;
			}
			{
				const double l1 = ((b[2] - c[2]) * (qx - c[0]) + (c[0] - b[0]) * (qz - c[2])) / den;
				const double l2 = ((c[2] - a[2]) * (qx - c[0]) + (a[0] - c[0]) * (qz - c[2])) / den;
				const double l3 = 1.0 - l1 - l2;
				const double y = l1 * a[1] + l2 * b[1] + l3 * c[1];

				if ((l1 >= 0.0) && (l2 >= 0.0) && (l3 >= 0.0) && (y < 20.0) && (y > r->floorY))
				{
					r->floorY = y;
				}
			}
		}
		if (r->floorY < 0.0)
		{
			r->floorY = 5.8; // the dummy's floor (tools/rldpack_dummy.inc RLDDUM_SEAT_FLOOR_Y)
		}
		r->seat[0] = 0.0;
		r->seat[1] = (r->floorY + RLDKIT_SEAT_ABOVE_FLOOR) / RLDKIT_UNITS_PER_METRE;
		r->seat[2] = RLDKIT_SEAT_Z / RLDKIT_UNITS_PER_METRE;
		r->floorY /= RLDKIT_UNITS_PER_METRE;
	}

	// The steering wheel ring (measured), its colour, the grips; the feet; the figure.
	RldKit_Ring(m, r);
	{
		double ch[3][512];
		u32 n = 0;
		int k;

		for (t = 0; (t < m->tris) && (n < 500u); t++)
		{
			int c;
			int near = 0;

			for (c = 0; c < 3; c++)
			{
				double p[3];

				RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, m->triRec[t * 3u + (u32)c], p);
				near += ((fabs(RldKit_Dist(p, r->ringCenter) - r->ringRadius) < 2.5) && !m->still[m->triRec[t * 3u + (u32)c]]) ? 1 : 0;
			}
			if (near == 3)
			{
				u8 rgb[3];
				u32 w[3];

				RldKit_Colour(m, m->triCol[t * 3u], rgb);
				if (RldKit_Layout(m, t, w))
				{
					int okT = 0;
					u8 rgba[4];
					const u32 word = RldKit_TexelWord(vram, written, (w[1] >> 16) & 0x1ffu, w[0] >> 16, (int)(w[0] & 0xffu), (int)((w[0] >> 8) & 0xffu), &okT);

					RldKit_WordRgba(word, rgba);
					for (k = 0; k < 3; k++)
					{
						const int v = okT ? ((int)rgba[k] * (int)rgb[k]) / 128 : (int)rgb[k];

						rgb[k] = (u8)((v > 255) ? 255 : v);
					}
				}
				for (k = 0; k < 3; k++)
				{
					ch[k][n] = rgb[k];
				}
				n++;
			}
		}
		for (k = 0; k < 3; k++)
		{
			r->ring[k] = (n >= 1u) ? (u8)(RldKit_Median(ch[k], n) + 0.5) : (u8)40;
		}
	}
	{
		double e1[3];
		const double xAxis[3] = {1.0, 0.0, 0.0};
		const double dd = RldKit_Dot(xAxis, r->ringNormal);
		int k;

		for (k = 0; k < 3; k++)
		{
			e1[k] = xAxis[k] - dd * r->ringNormal[k];
		}
		RldKit_Normalize(e1);
		for (k = 0; k < 3; k++)
		{
			r->gripLeft[k] = (r->ringCenter[k] + r->ringRadius * e1[k]) / RLDKIT_UNITS_PER_METRE;
			r->gripRight[k] = (r->ringCenter[k] - r->ringRadius * e1[k]) / RLDKIT_UNITS_PER_METRE;
			r->ringCenter[k] /= RLDKIT_UNITS_PER_METRE;
		}
		r->ringRadius /= RLDKIT_UNITS_PER_METRE;
	}
	{
		double sum[2][3] = {{0, 0, 0}, {0, 0, 0}};
		u32 cnt[2] = {0, 0};
		u32 rec;
		int k;

		for (k = 0; k < 3; k++)
		{
			r->driverMin[k] = 1e30;
			r->driverMax[k] = -1e30;
		}
		for (t = 0; t < m->tris; t++)
		{
			int c;

			if (isKart[t])
			{
				continue;
			}
			for (c = 0; c < 3; c++)
			{
				double p[3];

				RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, m->triRec[t * 3u + (u32)c], p);
				for (k = 0; k < 3; k++)
				{
					r->driverMin[k] = (p[k] < r->driverMin[k]) ? p[k] : r->driverMin[k];
					r->driverMax[k] = (p[k] > r->driverMax[k]) ? p[k] : r->driverMax[k];
				}
			}
		}
		for (rec = 0; rec < m->records; rec++)
		{
			double p[3];

			if (m->still[rec])
			{
				continue;
			}
			RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, rec, p);
			if ((p[1] >= 6.0) && (p[1] <= 18.0) && (p[2] >= 22.0) && (p[2] <= 45.0) && (fabs(p[0]) <= 22.0) && (fabs(p[0]) >= 2.0))
			{
				const int side = (p[0] > 0.0) ? 0 : 1;

				for (k = 0; k < 3; k++)
				{
					sum[side][k] += p[k];
				}
				cnt[side]++;
			}
		}
		r->feetMeasured = (cnt[0] > 0u) && (cnt[1] > 0u);
		for (k = 0; k < 3; k++)
		{
			const double defL[3] = {12.0, 11.2, 35.0};
			const double defR[3] = {-12.0, 11.2, 35.0};

			r->footLeft[k] = (r->feetMeasured ? sum[0][k] / (double)cnt[0] : defL[k]) / RLDKIT_UNITS_PER_METRE;
			r->footRight[k] = (r->feetMeasured ? sum[1][k] / (double)cnt[1] : defR[k]) / RLDKIT_UNITS_PER_METRE;
			if (r->driverMin[k] > r->driverMax[k])
			{
				r->driverMin[k] = r->driverMax[k] = 0.0;
			}
			r->driverMin[k] /= RLDKIT_UNITS_PER_METRE;
			r->driverMax[k] /= RLDKIT_UNITS_PER_METRE;
		}
	}

	// THE WHEELS: sizes and colours from the sprites, the game's points.
	memset(wheelTread, 0, sizeof(wheelTread));
	memset(wheelSide, 0, sizeof(wheelSide));
	{
		u32 *tread = wheelTread;
		u32 *side = wheelSide;
		double R = RLDKIT_TIRE_HALF_SIZE - 1.0;
		double across = 22.0;
		const double widthMax = RLDKIT_TIRE_WIDTH_MAX;
		int k;
		int okTread = RldKit_TireIcon(src->pack, src->packSize, 0, tread);
		int okSide = RldKit_TireIcon(src->pack, src->packSize, 16, side);
		u8 dummy[3];
		const u8 defTyre[3] = {37, 26, 22};
		const u8 defRim[3] = {142, 133, 132};
		const u8 defHub[3] = {19, 14, 18};

		memcpy(r->tyre, defTyre, 3);
		memcpy(r->rim, defRim, 3);
		memcpy(r->hub, defHub, 3);
		r->wheelMeasured = 0;
		wheelSprites = okTread && okSide;
		if (okSide && RldKit_SpriteBand(vram, written, side, 0.62, 0.95, 0, 255, r->tyre, &R, NULL))
		{
			r->wheelMeasured = 1;
			(void)RldKit_SpriteBand(vram, written, side, 0.20, 0.52, 60, 255, r->rim, NULL, NULL);
			(void)RldKit_SpriteBand(vram, written, side, 0.0, 0.12, 0, 255, r->hub, NULL, NULL);
		}
		if (okTread && RldKit_SpriteBand(vram, written, tread, 0.0, 1.0, 15, 120, dummy, NULL, &across))
		{
			memcpy(r->tyre, dummy, 3);
		}
		if (R < 10.0)
		{
			R = 10.0;
		}
		if (R > RLDKIT_TIRE_HALF_SIZE)
		{
			R = RLDKIT_TIRE_HALF_SIZE;
		}
		r->wheelRadius = R / RLDKIT_UNITS_PER_METRE;
		r->wheelWidth = ((across < widthMax) ? across : widthMax) / RLDKIT_UNITS_PER_METRE;
		for (k = 0; k < 4; k++)
		{
			r->wheelPos[k][0] = ((k & 1) ? -RLDKIT_TIRE_X : RLDKIT_TIRE_X) / RLDKIT_UNITS_PER_METRE;
			r->wheelPos[k][1] = RLDKIT_TIRE_Y / RLDKIT_UNITS_PER_METRE;
			r->wheelPos[k][2] = ((k < 2) ? RLDKIT_TIRE_FRONT_Z : RLDKIT_TIRE_REAR_Z) / RLDKIT_UNITS_PER_METRE;
		}
		r->steerMaxDeg = 64.0 * 4.0 * 360.0 / 4096.0;
	}
	if ((wheelSprites && !RldKit_WheelAtlas(r, vram, written, wheelTread, wheelSide, passes)) || !RldKit_WheelMesh(r) || !RldKit_SteerMesh(r))
	{
		RldKit_Fail(why, whySize, "out of memory");
		goto done;
	}
	ok = 1;

done:
	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		RldKit_RetailFree(&all[d]);
	}
	free(vram);
	free(written);
	free(tri);
	free(pn);
	free(isKart);
	if (!ok)
	{
		RldKit_ResultFree(r);
	}
	return ok;
}

//========================================================================================
// THE CLASSIC KART (CHRI bit 5, include/rldchar.inc THE GAME KART)
//========================================================================================
//
// The retail kart triangles of a donor AS THEY ARE, for the classic model of a
// figure-only file: the same kart as RldKit_Build finds (RldKit_KartMask), but
// not smoothed and not on an atlas - each triangle with its corners in the order
// the retail model draws them (frame 10 of the turn, model units), its colours,
// its TextureLayout words (they point into shared.vrm, which is in VRAM from the
// boot on) and the cull bits of the command that draws it. The game splices
// them into the file's CMDL (RldChar_SpliceKart). Only the 15 models are read
// (BIGFILE 242..256): no picture is decoded. Everything stays in memory.

#define RLDKIT_CLASSIC_TRIS_MAX 1024u

typedef struct RldKitClassicTri
{
	double p[3][3];  // model units, frame 10, in the retail drawing order
	u8 rgb[3][3];
	u32 layout[3];   // the 12 bytes of its TextureLayout, when textured
	u32 cull;        // bits 28-29 of the command that draws it
	int textured;
} RldKitClassicTri;

typedef struct RldKitClassic
{
	int ok;                 // made
	char why[160];          // why not
	u32 count;
	RldKitClassicTri *tri;  // malloc'ed
	u32 retailTriangles;    // the donor's whole model
} RldKitClassic;

static void RldKit_ClassicFree(RldKitClassic *k)
{
	free(k->tri);
	memset(k, 0, sizeof(*k));
}

// The classic kart of every donor whose bit is set in `donors` (bit d = driver
// d, 0..14), from ONE decoding of the 15 models. out[d] is filled for each
// such d (ok, or why). 0 = the models could not be decoded (why); then every
// asked out[d] carries the same reason.
static int RldKit_ClassicKarts(const RldKitSource *src, u32 donors, RldKitClassic out[RLDKIT_DRIVERS], char *why, size_t whySize)
{
	static RldKitRetail all[RLDKIT_DRIVERS];
	int d;
	int ok = 1;

	memset(all, 0, sizeof(all));
	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		if ((donors >> (u32)d) & 1u)
		{
			RldKit_ClassicFree(&out[d]);
		}
	}
	for (d = 0; ok && (d < RLDKIT_DRIVERS); d++)
	{
		char w[160];

		if ((src == NULL) || !RldKit_RetailDecode(src->model[d], src->modelSize[d], &all[d], w, sizeof(w)))
		{
			char text[200];

			snprintf(text, sizeof(text), "driver %d (BIGFILE entry %u): %s", d, (unsigned)(RLDKIT_BIG_MODEL_HI + (u32)d), (src == NULL) ? "no game data" : w);
			RldKit_Fail(why, whySize, text);
			ok = 0;
		}
	}
	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		RldKitClassic *k = &out[d];
		const RldKitRetail *m = &all[d];
		RldKitResult counts;
		u8 *isKart;
		u32 t;

		if (((donors >> (u32)d) & 1u) == 0u)
		{
			continue;
		}
		if (!ok)
		{
			snprintf(k->why, sizeof(k->why), "%s", (why != NULL) ? why : "the game data cannot be read");
			continue;
		}
		if (m->frames <= 20u)
		{
			snprintf(k->why, sizeof(k->why), "the donor's turn has fewer than 21 frames - no kart can be told apart");
			continue;
		}
		memset(&counts, 0, sizeof(counts));
		isKart = (u8 *)calloc(m->tris ? m->tris : 1u, 1);
		k->tri = (RldKitClassicTri *)calloc(m->tris ? m->tris : 1u, sizeof(RldKitClassicTri));
		if ((isKart == NULL) || (k->tri == NULL))
		{
			free(isKart);
			RldKit_ClassicFree(k);
			snprintf(k->why, sizeof(k->why), "out of memory");
			continue;
		}
		RldKit_KartMask(all, d, isKart, &counts);
		k->retailTriangles = m->tris;
		for (t = 0; t < m->tris; t++)
		{
			RldKitClassicTri *c = &k->tri[k->count];
			int i;

			if (!isKart[t])
			{
				continue;
			}
			for (i = 0; i < 3; i++)
			{
				RldKit_Pos(m, RLDKIT_NEUTRAL_FRAME, m->triRec[t * 3u + (u32)i], c->p[i]);
				RldKit_Colour(m, m->triCol[t * 3u + (u32)i], c->rgb[i]);
			}
			c->textured = RldKit_Layout(m, t, c->layout);
			if (!c->textured)
			{
				memset(c->layout, 0, sizeof(c->layout));
			}
			c->cull = m->triCmd[t] & 0x30000000u;
			k->count++;
		}
		free(isKart);
		if ((k->count == 0u) || (k->count > RLDKIT_CLASSIC_TRIS_MAX))
		{
			const u32 n = k->count;

			RldKit_ClassicFree(k);
			if (n == 0u)
			{
				snprintf(k->why, sizeof(k->why), "no kart triangle was found");
			}
			else
			{
				snprintf(k->why, sizeof(k->why), "more than %u kart triangles", (unsigned)RLDKIT_CLASSIC_TRIS_MAX);
			}
			continue;
		}
		k->ok = 1;
	}
	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		RldKit_RetailFree(&all[d]);
	}
	return ok;
}

//========================================================================================
// THE CHECKS (kit-extract's report; DONE K1 #3..#6)
//========================================================================================

// The silhouette IoU of two meshes (metres) in an orthographic view: view 0
// front (x, y), 1 side (z, y), 2 top (x, z), 3 three quarters (turned 45
// degrees about y). size x size pixels over the union box. -1 on no memory.
static double RldKit_Iou(const RldKitMesh *a, const RldKitMesh *b, int view, int size)
{
	const RldKitMesh *mesh[2] = {a, b};
	u8 *mask = (u8 *)calloc((size_t)size * size, 1);
	double lo[2] = {1e30, 1e30};
	double hi[2] = {-1e30, -1e30};
	double s45;
	double c45;
	u32 inter = 0;
	u32 uni = 0;
	int mi;
	u32 i;

	if (mask == NULL)
	{
		return -1.0;
	}
	RldKit_SinCos(45.0, &s45, &c45);
#define RLDKIT_PROJ(v, out)                                                       \
	do                                                                            \
	{                                                                             \
		const double X = (v)->p[0], Y = (v)->p[1], Z = (v)->p[2];                  \
		(out)[0] = (view == 0) ? X : (view == 1) ? Z : (view == 2) ? X : (c45 * X + s45 * Z); \
		(out)[1] = (view == 2) ? Z : Y;                                           \
	} while (0)
	for (mi = 0; mi < 2; mi++)
	{
		for (i = 0; i < mesh[mi]->vertices; i++)
		{
			double q[2];
			int k;

			RLDKIT_PROJ(&mesh[mi]->v[i], q);
			for (k = 0; k < 2; k++)
			{
				lo[k] = (q[k] < lo[k]) ? q[k] : lo[k];
				hi[k] = (q[k] > hi[k]) ? q[k] : hi[k];
			}
		}
	}
	{
		const double span = ((hi[0] - lo[0]) > (hi[1] - lo[1])) ? (hi[0] - lo[0]) : (hi[1] - lo[1]);
		const double scale = (span > 0.0) ? ((double)(size - 2) / span) : 1.0;

		for (mi = 0; mi < 2; mi++)
		{
			for (i = 0; i + 2u < mesh[mi]->indices; i += 3u)
			{
				double q[3][2];
				int k;
				int x0;
				int x1;
				int y0;
				int y1;
				int x;
				int y;
				double area;

				for (k = 0; k < 3; k++)
				{
					RLDKIT_PROJ(&mesh[mi]->v[mesh[mi]->index[i + (u32)k]], q[k]);
					q[k][0] = (q[k][0] - lo[0]) * scale + 1.0;
					q[k][1] = (q[k][1] - lo[1]) * scale + 1.0;
				}
				area = (q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (q[1][1] - q[0][1]);
				if (fabs(area) < 1e-12)
				{
					continue;
				}
				x0 = (int)floor(fmin(q[0][0], fmin(q[1][0], q[2][0])));
				x1 = (int)ceil(fmax(q[0][0], fmax(q[1][0], q[2][0])));
				y0 = (int)floor(fmin(q[0][1], fmin(q[1][1], q[2][1])));
				y1 = (int)ceil(fmax(q[0][1], fmax(q[1][1], q[2][1])));
				for (y = (y0 < 0 ? 0 : y0); (y <= y1) && (y < size); y++)
				{
					for (x = (x0 < 0 ? 0 : x0); (x <= x1) && (x < size); x++)
					{
						const double px = (double)x + 0.5;
						const double py = (double)y + 0.5;
						const double e0 = (q[1][0] - q[0][0]) * (py - q[0][1]) - (q[1][1] - q[0][1]) * (px - q[0][0]);
						const double e1 = (q[2][0] - q[1][0]) * (py - q[1][1]) - (q[2][1] - q[1][1]) * (px - q[1][0]);
						const double e2 = (q[0][0] - q[2][0]) * (py - q[2][1]) - (q[0][1] - q[2][1]) * (px - q[2][0]);

						if (((e0 >= 0.0) && (e1 >= 0.0) && (e2 >= 0.0)) || ((e0 <= 0.0) && (e1 <= 0.0) && (e2 <= 0.0)))
						{
							mask[y * size + x] |= (u8)(1 << mi);
						}
					}
				}
			}
		}
	}
#undef RLDKIT_PROJ
	for (i = 0; i < (u32)(size * size); i++)
	{
		inter += (mask[i] == 3u) ? 1u : 0u;
		uni += (mask[i] != 0u) ? 1u : 0u;
	}
	free(mask);
	return (uni > 0u) ? ((double)inter / (double)uni) : 1.0;
}

// THE WHEEL'S LOOK (K2b): the wheel rendered unlit and orthographic (size x
// size pixels over -R..R; the nearest texel of the upscaled atlas times the
// vertex colour / 128, or the vertex colour without a texture; the nearest
// surface wins) from the hub side (+x, pixels z y) and from the front (+z,
// pixels x y), against the sprites as decoded: the mean colour of the rings
// tyre 0.62-0.95, rim 0.20-0.52 and hub 0-0.12 of the radius (hub side), and
// of the upper and the lower half (front). The flat colours (the wheel without
// its texture: tyre x 0.85 on the side, rim, hub, tyre on the tread) against
// the same sprite means, for comparison. 1 = measured; 0 = no sprites or memory.
#define RLDKIT_LOOK_SIZE 64
#define RLDKIT_LOOK_LIMIT 12.0 // the largest mean difference of a channel (0..255), DONE K2b #1/#2
typedef struct RldKitWheelLook
{
	double side[3][2][3];  // ring (tyre, rim, hub) x (render, sprite) x RGB
	double tread[2][2][3]; // half (upper, lower) x (render, sprite) x RGB
	double flatSide[3][3];
	double flatTread[3];
	double worst;          // the largest |render - sprite| of a channel
	double worstFlat;      // the largest |flat - sprite|
} RldKitWheelLook;

static const double s_rldKitLookRing[3][2] = {{0.62, 0.95}, {0.20, 0.52}, {0.0, 0.12}};

static void RldKit_WheelRender(const RldKitResult *r, int view, double *rgb, u8 *cov)
{
	const int size = RLDKIT_LOOK_SIZE;
	const RldKitMesh *m = &r->wheel;
	const double R = r->wheelRadius;
	double *depth = (double *)malloc((size_t)size * size * sizeof(double));
	u32 i;
	int k;

	memset(cov, 0, (size_t)size * size);
	memset(rgb, 0, (size_t)size * size * 3u * sizeof(double));
	if (depth == NULL)
	{
		return;
	}
	for (k = 0; k < size * size; k++)
	{
		depth[k] = -1e30;
	}
	for (i = 0; i + 2u < m->indices; i += 3u)
	{
		const RldKitVertex *v[3] = {&m->v[m->index[i]], &m->v[m->index[i + 1u]], &m->v[m->index[i + 2u]]};
		double q[3][3]; // screen x, screen y (pixels), depth
		double area;
		int x0;
		int x1;
		int y0;
		int y1;
		int x;
		int y;

		for (k = 0; k < 3; k++)
		{
			const double su = (view == 0) ? (double)v[k]->p[2] : (double)v[k]->p[0];

			q[k][0] = (su + R) / (2.0 * R) * (double)size;
			q[k][1] = (R - (double)v[k]->p[1]) / (2.0 * R) * (double)size;
			q[k][2] = (view == 0) ? (double)v[k]->p[0] : (double)v[k]->p[2];
		}
		area = (q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (q[1][1] - q[0][1]);
		if (fabs(area) < 1e-12)
		{
			continue;
		}
		x0 = (int)floor(fmin(q[0][0], fmin(q[1][0], q[2][0])));
		x1 = (int)ceil(fmax(q[0][0], fmax(q[1][0], q[2][0])));
		y0 = (int)floor(fmin(q[0][1], fmin(q[1][1], q[2][1])));
		y1 = (int)ceil(fmax(q[0][1], fmax(q[1][1], q[2][1])));
		for (y = (y0 < 0) ? 0 : y0; (y <= y1) && (y < size); y++)
		{
			for (x = (x0 < 0) ? 0 : x0; (x <= x1) && (x < size); x++)
			{
				const double px = (double)x + 0.5;
				const double py = (double)y + 0.5;
				const double w0 = ((q[1][0] - px) * (q[2][1] - py) - (q[2][0] - px) * (q[1][1] - py)) / area;
				const double w1 = ((q[2][0] - px) * (q[0][1] - py) - (q[0][0] - px) * (q[2][1] - py)) / area;
				const double w2 = 1.0 - w0 - w1;
				const double d = w0 * q[0][2] + w1 * q[1][2] + w2 * q[2][2];
				const size_t at = (size_t)y * (size_t)size + (size_t)x;

				if ((w0 < 0.0) || (w1 < 0.0) || (w2 < 0.0) || (d <= depth[at]))
				{
					continue;
				}
				depth[at] = d;
				cov[at] = 1;
				for (k = 0; k < 3; k++)
				{
					const double c = w0 * v[0]->c[k] + w1 * v[1]->c[k] + w2 * v[2]->c[k];

					if (m->textured && (r->wheelAtlas != NULL))
					{
						const double u = w0 * v[0]->uv[0] + w1 * v[1]->uv[0] + w2 * v[2]->uv[0];
						const double vv = w0 * v[0]->uv[1] + w1 * v[1]->uv[1] + w2 * v[2]->uv[1];
						u32 ax = (u32)fmax(0.0, u * (double)r->wheelAtlasW);
						u32 ay = (u32)fmax(0.0, vv * (double)r->wheelAtlasH);

						ax = (ax >= r->wheelAtlasW) ? r->wheelAtlasW - 1u : ax;
						ay = (ay >= r->wheelAtlasH) ? r->wheelAtlasH - 1u : ay;
						rgb[at * 3u + (u32)k] = fmin(255.0, (double)r->wheelAtlas[((size_t)ay * r->wheelAtlasW + ax) * 4u + (u32)k] * c / 128.0);
					}
					else
					{
						rgb[at * 3u + (u32)k] = c;
					}
				}
			}
		}
	}
	free(depth);
}

static int RldKit_WheelLook(const RldKitResult *r, RldKitWheelLook *out)
{
	const int size = RLDKIT_LOOK_SIZE;
	double *rgb = (double *)malloc((size_t)size * size * 3u * sizeof(double));
	u8 *cov = (u8 *)malloc((size_t)size * size);
	double n[2][3][2];
	int view;
	int k;
	int a;
	int c;

	memset(out, 0, sizeof(*out));
	memset(n, 0, sizeof(n));
	if ((r->wheelSprite[0] == NULL) || (r->wheelSprite[1] == NULL) || (rgb == NULL) || (cov == NULL))
	{
		free(rgb);
		free(cov);
		return 0;
	}
	for (view = 0; view < 2; view++)
	{
		int x;
		int y;
		const u8 *sp = r->wheelSprite[view == 0 ? 1 : 0];
		const u32 sw = r->wheelSpriteW[view == 0 ? 1 : 0];
		const u32 sh = r->wheelSpriteH[view == 0 ? 1 : 0];
		const double cx = (view == 0) ? (r->sideCentre[0] - r->wheelSpriteAt[1][0]) : 0.0;
		const double cy = (view == 0) ? (r->sideCentre[1] - r->wheelSpriteAt[1][1]) : (0.5 * (r->treadBox[1] + r->treadBox[3]) - r->wheelSpriteAt[0][1]);

		RldKit_WheelRender(r, view, rgb, cov);
		// the render
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
			{
				const size_t at = (size_t)y * (size_t)size + (size_t)x;
				const double u = ((double)x + 0.5) / (double)size * 2.0 - 1.0;
				const double v = 1.0 - ((double)y + 0.5) / (double)size * 2.0;
				int region = -1;

				if (!cov[at])
				{
					continue;
				}
				if (view == 0)
				{
					const double d = sqrt(u * u + v * v);

					for (a = 0; a < 3; a++)
					{
						region = ((d >= s_rldKitLookRing[a][0]) && (d <= s_rldKitLookRing[a][1])) ? a : region;
					}
				}
				else
				{
					region = (v > 0.0) ? 0 : 1;
				}
				if (region < 0)
				{
					continue;
				}
				for (c = 0; c < 3; c++)
				{
					if (view == 0)
					{
						out->side[region][0][c] += rgb[at * 3u + (u32)c];
					}
					else
					{
						out->tread[region][0][c] += rgb[at * 3u + (u32)c];
					}
				}
				n[view][region][0] += 1.0;
			}
		}
		// the sprite
		for (y = 0; y < (int)sh; y++)
		{
			for (x = 0; x < (int)sw; x++)
			{
				const u8 *t = &sp[((size_t)y * sw + (size_t)x) * 4u];
				int region = -1;

				if (t[3] == 0u)
				{
					continue;
				}
				if (view == 0)
				{
					const double dx = (double)x + 0.5 - cx;
					const double dy = (double)y + 0.5 - cy;
					const double d = sqrt(dx * dx + dy * dy) / ((r->sideRadius > 0.0) ? r->sideRadius : 1.0);

					for (a = 0; a < 3; a++)
					{
						region = ((d >= s_rldKitLookRing[a][0]) && (d <= s_rldKitLookRing[a][1])) ? a : region;
					}
				}
				else
				{
					region = (((double)y + 0.5) < cy) ? 0 : 1;
				}
				if (region < 0)
				{
					continue;
				}
				for (c = 0; c < 3; c++)
				{
					if (view == 0)
					{
						out->side[region][1][c] += (double)t[c];
					}
					else
					{
						out->tread[region][1][c] += (double)t[c];
					}
				}
				n[view][region][1] += 1.0;
			}
		}
	}
	for (a = 0; a < 3; a++)
	{
		for (k = 0; k < 2; k++)
		{
			for (c = 0; c < 3; c++)
			{
				out->side[a][k][c] /= (n[0][a][k] > 0.0) ? n[0][a][k] : 1.0;
				if (a < 2)
				{
					out->tread[a][k][c] /= (n[1][a][k] > 0.0) ? n[1][a][k] : 1.0;
				}
			}
		}
	}
	for (c = 0; c < 3; c++)
	{
		out->flatSide[0][c] = fmin(255.0, floor((double)r->tyre[c] * 0.85 + 0.5));
		out->flatSide[1][c] = (double)r->rim[c];
		out->flatSide[2][c] = (double)r->hub[c];
		out->flatTread[c] = (double)r->tyre[c];
		for (a = 0; a < 3; a++)
		{
			out->worst = fmax(out->worst, fabs(out->side[a][0][c] - out->side[a][1][c]));
			out->worstFlat = fmax(out->worstFlat, fabs(out->flatSide[a][c] - out->side[a][1][c]));
		}
		for (a = 0; a < 2; a++)
		{
			out->worst = fmax(out->worst, fabs(out->tread[a][0][c] - out->tread[a][1][c]));
			out->worstFlat = fmax(out->worstFlat, fabs(out->flatTread[c] - out->tread[a][1][c]));
		}
	}
	free(rgb);
	free(cov);
	return 1;
}

// The colour check: at 45 points of every flat triangle (a grid of 9 steps)
// the source atlas texel (the retail texel as decoded) and the upscaled atlas at
// the same UV, both times the vertex colour / 128 (display bytes, clamped),
// averaged per triangle: *mean the mean difference of the triangles' averages
// per channel, *worst the largest. (Point by point Scale2x moves a colour edge
// by half a source texel; the averages show whether a triangle keeps its
// colour.) 1 = done.
#define RLDKIT_COLOUR_STEPS 9
static int RldKit_ColourCheck(const RldKitResult *r, double *mean, double *worst)
{
	double sum = 0.0;
	u32 n = 0;
	u32 t;

	*mean = 0.0;
	*worst = 0.0;
	if ((r->atlas == NULL) || (r->atlasSource == NULL))
	{
		return 0;
	}
	for (t = 0; t + 2u < r->flat.indices; t += 3u)
	{
		const RldKitVertex *v[3] = {&r->flat.v[r->flat.index[t]], &r->flat.v[r->flat.index[t + 1u]], &r->flat.v[r->flat.index[t + 2u]]};
		double ta[3] = {0.0, 0.0, 0.0};
		double tb[3] = {0.0, 0.0, 0.0};
		u32 samples = 0;
		int i;
		int j;
		int k;

		for (i = 0; i < RLDKIT_COLOUR_STEPS; i++)
		{
			for (j = 0; i + j < RLDKIT_COLOUR_STEPS; j++)
			{
				// the middles of the small triangles of the grid
				const double w1 = ((double)i + 1.0 / 3.0) / (double)RLDKIT_COLOUR_STEPS;
				const double w2 = ((double)j + 1.0 / 3.0) / (double)RLDKIT_COLOUR_STEPS;
				const double w0 = 1.0 - w1 - w2;
				const double u = w0 * v[0]->uv[0] + w1 * v[1]->uv[0] + w2 * v[2]->uv[0];
				const double vv = w0 * v[0]->uv[1] + w1 * v[1]->uv[1] + w2 * v[2]->uv[1];
				u32 sx = (u32)(u * (double)r->sourceW);
				u32 sy = (u32)(vv * (double)r->sourceH);
				u32 ax = (u32)(u * (double)r->atlasW);
				u32 ay = (u32)(vv * (double)r->atlasH);

				sx = (sx >= r->sourceW) ? r->sourceW - 1u : sx;
				sy = (sy >= r->sourceH) ? r->sourceH - 1u : sy;
				ax = (ax >= r->atlasW) ? r->atlasW - 1u : ax;
				ay = (ay >= r->atlasH) ? r->atlasH - 1u : ay;
				for (k = 0; k < 3; k++)
				{
					const double vc = w0 * v[0]->c[k] + w1 * v[1]->c[k] + w2 * v[2]->c[k];

					ta[k] += fmin(255.0, (double)r->atlasSource[((size_t)sy * r->sourceW + sx) * 4u + (u32)k] * vc / 128.0);
					tb[k] += fmin(255.0, (double)r->atlas[((size_t)ay * r->atlasW + ax) * 4u + (u32)k] * vc / 128.0);
				}
				samples++;
			}
		}
		for (k = 0; k < 3; k++)
		{
			const double d = fabs(ta[k] - tb[k]) / (double)samples;

			sum += d;
			n++;
			*worst = (d > *worst) ? d : *worst;
		}
	}
	*mean = (n > 0u) ? (sum / (double)n) : 0.0;
	return 1;
}

#endif // RLDKIT_INC
