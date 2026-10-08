// ===========================================================================
// THE NATIVE PROBE - A TEST BODY MADE IN CODE.
//
// With --native-preview --native-probe body, seat 0 of a one-player arcade
// race (or the seat --native-probe-seat names) draws this body instead of its
// retail model, through the native split of the render layer
// (platform/native_render_layer.c binds it and routes, platform/
// native_renderer.c holds the buffers and draws). With --native-probe texture
// the same body takes every colour from a texture made here as well; with
// --native-probe wheels (the form pose) four wheels made here go with it. This
// file only owns the meshes, the texture, the switch values and the self-test
// of their bytes.
//
// THE UNIT BODY. x -1..1 (+X is left seen from behind), y 0..1 (up), z -1..1
// (+Z is the front) - the game axes of the character packer ("+Y up, +Z
// forward", tools/rldpack_char.inc) and what the asym check of the measuring
// tools expects. Nine quads of 4 vertices each, indices per quad q:
// 4q, 4q+1, 4q+2, 4q, 4q+2, 4q+3. The right-hand normal (v1-v0) x (v2-v0)
// of every quad points outwards; the inner plate points to -Z, towards the
// camera behind the kart.
//
// THE COLOURS. Seen from behind: green on the left (+X), blue on the right
// (-X), so a mirrored picture is told apart from a correct one. The bottom is
// grey, the front red. The red inner plate at z = 0 lies wholly inside the
// body and comes last: it only shows when the depth test or the back face
// rule does not work. Red on screen is a finding.
//
// THE BYTES. 36 vertices of 24 bytes (5 x float32, 4 x u8, no padding,
// little endian), then 54 u16 indices: 972 bytes. Every value is exact in
// float32. --native-probe-selftest hashes them against the golden SHA-256
// (ctest native_probe_selftest), so a changed table cannot pass unseen.
//
// Only static tables and functions: nothing here runs unless it is called,
// no clock, no random numbers, no game state.
// ===========================================================================

#include "platform/native_probe.h"

#include <macros.h>

#include <stdio.h>
#include <string.h>

int g_cfg_nativeProbe = NATIVE_PROBE_NONE;
int g_cfg_nativeProbeSeat = 0;
int g_cfg_nativeProbeWheels = 0;
int g_cfg_nativeProbeMips = 0;

CTR_STATIC_ASSERT(sizeof(struct NativeProbeVertex) == 24);

int NativeProbe_FormFromName(const char *name)
{
	if (name == NULL)
	{
		return NATIVE_PROBE_NONE;
	}
	if (strcmp(name, "body") == 0)
	{
		return NATIVE_PROBE_BODY;
	}
	if (strcmp(name, "texture") == 0)
	{
		return NATIVE_PROBE_TEXTURE;
	}
	if ((strcmp(name, "pose") == 0) || (strcmp(name, "wheels") == 0) || (strcmp(name, "mips") == 0))
	{
		return NATIVE_PROBE_POSE;
	}
	return NATIVE_PROBE_NONE;
}

int NativeProbe_FormHasWheels(const char *name)
{
	return (name != NULL) && (strcmp(name, "wheels") == 0);
}

int NativeProbe_FormHasMips(const char *name)
{
	return (name != NULL) && (strcmp(name, "mips") == 0);
}

#define NATIVE_PROBE_BYTES ((NATIVE_PROBE_VERTEX_COUNT * 24) + (NATIVE_PROBE_INDEX_COUNT * 2))

// SHA-256 of the 972 mesh bytes, computed apart from this file from the
// same table.
#define NATIVE_PROBE_GOLDEN "87ded398127ef569456a9760ad0d8d7938c96f2db1e0d66a818bcd7865971373"

// The same for the form texture (its 972 mesh bytes, the same indices) and for
// its 262144 texture bytes, computed apart from this file from the layout
// written down at s_nativeProbeTexturedVertices.
#define NATIVE_PROBE_TEXTURED_GOLDEN "1948ecd790f9518408f043329ce36461d52cc6bc11dccd939a5f5d6dd4e3241a"
#define NATIVE_PROBE_TEXTURE_GOLDEN  "91a84b2163c3ec26e128e4d17ebc15b063b496055f830afc28341c20252cea4f"

// The eight pose meshes of the form pose, phase 0 to 7, their vertex bytes one
// after the other (8 x 864 bytes), computed apart from this file from the rule
// at NativeProbe_PoseVertices.
#define NATIVE_PROBE_POSE_GOLDEN     "1033d715c90b852b4bd487a81b4f8b1c8002f635ffc70fc65e3814788ff55e6b"

// The two halves of the wheel mesh, their vertex bytes one after the other
// (2 x 864 bytes), and the 262144 texture bytes of the form wheels, computed
// apart from this file from the rules at s_nativeProbeDecagon and
// NativeProbe_MakeTexture.
#define NATIVE_PROBE_WHEEL_GOLDEN         "136fb282e11273f06882a4217c755c2a033d8c4871bfddbfef3746a2a80e2190"
#define NATIVE_PROBE_WHEEL_TEXTURE_GOLDEN "21d43021f3bad68dcbb515b38b82f00b12ed7e2fdbcda0ebd3ecc9f8871740a6"

// The 9 levels of the mips texture, their bytes one after the other (349524
// bytes), computed apart from this file from the colour table in
// include/platform/native_probe.h.
#define NATIVE_PROBE_MIPS_GOLDEN "f581d2705016ddd01ec899b3eef7c82886f9730c2b5a92ef4ddd69c66382ad1a"

#define NATIVE_PROBE_GREEN {0, 255, 0, 255}
#define NATIVE_PROBE_BLUE  {0, 0, 255, 255}
#define NATIVE_PROBE_RED   {255, 0, 0, 255}
#define NATIVE_PROBE_GREY  {128, 128, 128, 255}

// clang-format off
static const struct NativeProbeVertex s_nativeProbeVertices[NATIVE_PROBE_VERTEX_COUNT] = {
	// 0: +X side, green
	{{ 1.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 1.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 1.0f, 0.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	// 1: -X side, blue
	{{-1.0f, 0.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{-1.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{-1.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	// 2: top, +X half, green
	{{ 0.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 0.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 1.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	// 3: top, -X half, blue
	{{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{-1.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{ 0.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{ 0.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	// 4: bottom, grey
	{{ 1.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREY},
	{{ 1.0f, 0.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREY},
	{{-1.0f, 0.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREY},
	{{-1.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREY},
	// 5: front (+Z), red
	{{-1.0f, 0.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
	{{ 1.0f, 0.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
	{{ 1.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
	{{-1.0f, 1.0f,  1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
	// 6: back, +X half, green
	{{ 1.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 0.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	{{ 1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_GREEN},
	// 7: back, -X half, blue
	{{ 0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{-1.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	{{ 0.0f, 1.0f, -1.0f}, {0.0f, 0.0f}, NATIVE_PROBE_BLUE},
	// 8: inner plate at z = 0, red, drawn last
	{{ 0.5f, 0.25f, 0.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
	{{-0.5f, 0.25f, 0.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
	{{-0.5f, 0.75f, 0.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
	{{ 0.5f, 0.75f, 0.0f}, {0.0f, 0.0f}, NATIVE_PROBE_RED},
};

static const u16 s_nativeProbeIndices[NATIVE_PROBE_INDEX_COUNT] = {
	 0,  1,  2,   0,  2,  3,
	 4,  5,  6,   4,  6,  7,
	 8,  9, 10,   8, 10, 11,
	12, 13, 14,  12, 14, 15,
	16, 17, 18,  16, 18, 19,
	20, 21, 22,  20, 22, 23,
	24, 25, 26,  24, 26, 27,
	28, 29, 30,  28, 30, 31,
	32, 33, 34,  32, 34, 35,
};
// clang-format on

// THE FORM TEXTURE. The positions and indices of the body above; every vertex
// white, so the colour on screen is the texel itself (white times the texel is
// the texel, exactly, in 8-bit UNORM). The texture, 256 x 256, in texels
// (x to the right, y down), each region with its colour:
//   x   0..127, y   0..127   green 0,255,0      +X side, back +X half
//   x 128..255, y   0..127   blue 0,0,255       -X side, back -X half
//   x   0..127, y 128..255   checker of 2 x 2 fields of 64 texels:
//                            A = 128,0,255 where (x >= 64) == (y >= 192),
//                            B = 0,255,128 elsewhere                   top
//   x 128..255, y 128..191   red 255,0,0        front, inner plate
//   x 128..255, y 192..255   grey 128,128,128   bottom
// Alpha 255 everywhere. The coordinates stay 4 texels inside every region, so
// a nearest sample at the rim of a face never reaches the neighbour region.
// The top maps x -1..1 to texel x 4..124 and z -1..1 to texel y 132..252, so
// the middle of the top (x = 0, z = 0) is the corner where the four checker
// fields meet: seen from above, two fields of A and two of B. Every value
// below is a whole number of texels over 256, exact in float32.
#define NP_UV(x, y) {(float)(x) / 256.0f, (float)(y) / 256.0f}
#define NATIVE_PROBE_WHITE {255, 255, 255, 255}

// clang-format off
static const struct NativeProbeVertex s_nativeProbeTexturedVertices[NATIVE_PROBE_VERTEX_COUNT] = {
	// 0: +X side, green region
	{{ 1.0f, 0.0f, -1.0f}, NP_UV(  4,   4), NATIVE_PROBE_WHITE},
	{{ 1.0f, 1.0f, -1.0f}, NP_UV(  4, 124), NATIVE_PROBE_WHITE},
	{{ 1.0f, 1.0f,  1.0f}, NP_UV(124, 124), NATIVE_PROBE_WHITE},
	{{ 1.0f, 0.0f,  1.0f}, NP_UV(124,   4), NATIVE_PROBE_WHITE},
	// 1: -X side, blue region
	{{-1.0f, 0.0f,  1.0f}, NP_UV(132,   4), NATIVE_PROBE_WHITE},
	{{-1.0f, 1.0f,  1.0f}, NP_UV(132, 124), NATIVE_PROBE_WHITE},
	{{-1.0f, 1.0f, -1.0f}, NP_UV(252, 124), NATIVE_PROBE_WHITE},
	{{-1.0f, 0.0f, -1.0f}, NP_UV(252,   4), NATIVE_PROBE_WHITE},
	// 2: top, +X half: texel x 64 + 60 x, texel y 192 + 60 z
	{{ 0.0f, 1.0f, -1.0f}, NP_UV( 64, 132), NATIVE_PROBE_WHITE},
	{{ 0.0f, 1.0f,  1.0f}, NP_UV( 64, 252), NATIVE_PROBE_WHITE},
	{{ 1.0f, 1.0f,  1.0f}, NP_UV(124, 252), NATIVE_PROBE_WHITE},
	{{ 1.0f, 1.0f, -1.0f}, NP_UV(124, 132), NATIVE_PROBE_WHITE},
	// 3: top, -X half, the same mapping
	{{-1.0f, 1.0f, -1.0f}, NP_UV(  4, 132), NATIVE_PROBE_WHITE},
	{{-1.0f, 1.0f,  1.0f}, NP_UV(  4, 252), NATIVE_PROBE_WHITE},
	{{ 0.0f, 1.0f,  1.0f}, NP_UV( 64, 252), NATIVE_PROBE_WHITE},
	{{ 0.0f, 1.0f, -1.0f}, NP_UV( 64, 132), NATIVE_PROBE_WHITE},
	// 4: bottom, grey region
	{{ 1.0f, 0.0f, -1.0f}, NP_UV(132, 196), NATIVE_PROBE_WHITE},
	{{ 1.0f, 0.0f,  1.0f}, NP_UV(132, 252), NATIVE_PROBE_WHITE},
	{{-1.0f, 0.0f,  1.0f}, NP_UV(252, 252), NATIVE_PROBE_WHITE},
	{{-1.0f, 0.0f, -1.0f}, NP_UV(252, 196), NATIVE_PROBE_WHITE},
	// 5: front (+Z), red region
	{{-1.0f, 0.0f,  1.0f}, NP_UV(132, 132), NATIVE_PROBE_WHITE},
	{{ 1.0f, 0.0f,  1.0f}, NP_UV(132, 188), NATIVE_PROBE_WHITE},
	{{ 1.0f, 1.0f,  1.0f}, NP_UV(252, 188), NATIVE_PROBE_WHITE},
	{{-1.0f, 1.0f,  1.0f}, NP_UV(252, 132), NATIVE_PROBE_WHITE},
	// 6: back, +X half, green region
	{{ 1.0f, 0.0f, -1.0f}, NP_UV(  4,   4), NATIVE_PROBE_WHITE},
	{{ 0.0f, 0.0f, -1.0f}, NP_UV(  4, 124), NATIVE_PROBE_WHITE},
	{{ 0.0f, 1.0f, -1.0f}, NP_UV(124, 124), NATIVE_PROBE_WHITE},
	{{ 1.0f, 1.0f, -1.0f}, NP_UV(124,   4), NATIVE_PROBE_WHITE},
	// 7: back, -X half, blue region
	{{ 0.0f, 0.0f, -1.0f}, NP_UV(132,   4), NATIVE_PROBE_WHITE},
	{{-1.0f, 0.0f, -1.0f}, NP_UV(132, 124), NATIVE_PROBE_WHITE},
	{{-1.0f, 1.0f, -1.0f}, NP_UV(252, 124), NATIVE_PROBE_WHITE},
	{{ 0.0f, 1.0f, -1.0f}, NP_UV(252,   4), NATIVE_PROBE_WHITE},
	// 8: inner plate at z = 0, red region, drawn last
	{{ 0.5f, 0.25f, 0.0f}, NP_UV(132, 132), NATIVE_PROBE_WHITE},
	{{-0.5f, 0.25f, 0.0f}, NP_UV(132, 188), NATIVE_PROBE_WHITE},
	{{-0.5f, 0.75f, 0.0f}, NP_UV(252, 188), NATIVE_PROBE_WHITE},
	{{ 0.5f, 0.75f, 0.0f}, NP_UV(252, 132), NATIVE_PROBE_WHITE},
};
// clang-format on

#define NATIVE_PROBE_TEXTURE_BYTES (NATIVE_PROBE_TEXTURE_SIZE * NATIVE_PROBE_TEXTURE_SIZE * 4)

// Static host memory, written once at the first call - in a run without the
// form texture never.
static u8 s_nativeProbeTexture[NATIVE_PROBE_TEXTURE_BYTES];
static int s_nativeProbeTextureMade = 0;

const struct NativeProbeVertex *NativeProbe_TexturedVertices(void)
{
	return s_nativeProbeTexturedVertices;
}

// The texels of the form texture into out; wheels != 0 makes the one wheel
// texel (NATIVE_PROBE_WHEEL_TEXEL, NATIVE_PROBE_WHEEL_TEXEL) white. That texel
// lies in the green region, two texels past the last one a face of the body can
// reach (the body samples texel x and y 4 to 124 there), and two before the
// blue and the checker regions begin - the body samples the same texels as in
// every other form. Without wheels every texel is the one it always was.
internal void NativeProbe_MakeTexture(u8 *out, int wheels)
{
	static const u8 green[4] = {0, 255, 0, 255};
	static const u8 blue[4] = {0, 0, 255, 255};
	static const u8 checkerA[4] = {128, 0, 255, 255};
	static const u8 checkerB[4] = {0, 255, 128, 255};
	static const u8 red[4] = {255, 0, 0, 255};
	static const u8 grey[4] = {128, 128, 128, 255};
	static const u8 white[4] = {255, 255, 255, 255};
	int x;
	int y;

	for (y = 0; y < NATIVE_PROBE_TEXTURE_SIZE; y++)
	{
		for (x = 0; x < NATIVE_PROBE_TEXTURE_SIZE; x++)
		{
			const u8 *colour;

			if (y < 128)
			{
				colour = (x < 128) ? green : blue;
			}
			else if (x < 128)
			{
				colour = ((x >= 64) == (y >= 192)) ? checkerA : checkerB;
			}
			else
			{
				colour = (y < 192) ? red : grey;
			}

			if (wheels && (x == NATIVE_PROBE_WHEEL_TEXEL) && (y == NATIVE_PROBE_WHEEL_TEXEL))
			{
				colour = white;
			}

			memcpy(&out[((y * NATIVE_PROBE_TEXTURE_SIZE) + x) * 4], colour, 4);
		}
	}
}

// THE MIPS TEXTURE: the 9 levels one after the other, each filled with its
// colour. Static host memory, written at the first call.
#define NATIVE_PROBE_MIPS_BYTES 349524 // 4 * (256^2 + 128^2 + ... + 1^2)

static u8 s_nativeProbeMips[NATIVE_PROBE_MIPS_BYTES];
static int s_nativeProbeMipsMade = 0;

internal void NativeProbe_MakeMips(u8 *out)
{
	static const u8 colours[NATIVE_PROBE_MIPS_LEVELS][4] = {
	    {255, 64, 255, 255}, {0, 255, 64, 255}, {160, 64, 255, 255}, {224, 32, 160, 255}, {192, 0, 96, 255},
	    {64, 0, 255, 255},   {0, 192, 96, 255}, {0, 255, 192, 255},  {160, 32, 192, 255},
	};
	size_t at = 0;
	int level;

	for (level = 0; level < NATIVE_PROBE_MIPS_LEVELS; level++)
	{
		const int edge = NATIVE_PROBE_TEXTURE_SIZE >> level;
		int texel;

		for (texel = 0; texel < edge * edge; texel++)
		{
			memcpy(&out[at], colours[level], 4);
			at += 4;
		}
	}
}

const u8 *NativeProbe_MipsLevel(int level)
{
	size_t at = 0;
	int n;

	if ((level < 0) || (level >= NATIVE_PROBE_MIPS_LEVELS))
	{
		return NULL;
	}

	if (!s_nativeProbeMipsMade)
	{
		NativeProbe_MakeMips(s_nativeProbeMips);
		s_nativeProbeMipsMade = 1;
	}

	for (n = 0; n < level; n++)
	{
		const size_t edge = (size_t)(NATIVE_PROBE_TEXTURE_SIZE >> n);

		at += edge * edge * 4u;
	}

	return &s_nativeProbeMips[at];
}

const u8 *NativeProbe_TexturePixels(void)
{
	if (!s_nativeProbeTextureMade)
	{
		NativeProbe_MakeTexture(s_nativeProbeTexture, g_cfg_nativeProbeWheels);
		s_nativeProbeTextureMade = 1;
	}
	return s_nativeProbeTexture;
}

// THE WHEEL MESH. The ten corners of the rim, cosine and sine of k times 36
// degrees, written down to six places (not computed, so the bytes are the same
// with every compiler and C library); a point is (x, cosine, sine), x = 1 the
// outer side. Built by placing these numbers only - no arithmetic on them - so
// the hash below depends on the table alone.
//
// Every quad keeps the rule of the body: its right-hand normal (v1 - v0) x
// (v2 - v0) points outwards. Tread quad k runs from corner k to corner k + 1 as
// (+1, k), (-1, k), (-1, k + 1), (+1, k + 1); the outer cap is the fan of quads
// (0, 2j + 1, 2j + 2, 2j + 3), j = 0..3, at x = +1, the inner cap the same four
// quads in the opposite order at x = -1 - together they close the ten-sided
// disc. Stripes: tread quads 0 and 5 (opposite each other, so rolling shows from
// any side) and the first quad of either cap. The texture coordinate is the
// middle of the white wheel texel, 126.5 / 256, exact in float32.
#define NATIVE_PROBE_WHEEL_CORNERS 10
#define NATIVE_PROBE_WHEEL_UV      (((float)NATIVE_PROBE_WHEEL_TEXEL + 0.5f) / (float)NATIVE_PROBE_TEXTURE_SIZE)

// clang-format off
static const float s_nativeProbeDecagon[NATIVE_PROBE_WHEEL_CORNERS][2] = {
	{ 1.0f,       0.0f},
	{ 0.809017f,  0.587785f},
	{ 0.309017f,  0.951057f},
	{-0.309017f,  0.951057f},
	{-0.809017f,  0.587785f},
	{-1.0f,       0.0f},
	{-0.809017f, -0.587785f},
	{-0.309017f, -0.951057f},
	{ 0.309017f, -0.951057f},
	{ 0.809017f, -0.587785f},
};
// clang-format on

static struct NativeProbeVertex s_nativeProbeWheel[NATIVE_PROBE_WHEEL_HALVES][NATIVE_PROBE_VERTEX_COUNT];
static int s_nativeProbeWheelMade = 0;

internal void NativeProbe_WheelPoint(struct NativeProbeVertex *v, float x, int corner, int stripe)
{
	static const u8 tyre[4] = {255, 0, 128, 255};
	static const u8 stripes[4] = {160, 0, 80, 255};

	corner %= NATIVE_PROBE_WHEEL_CORNERS;
	v->position[0] = x;
	v->position[1] = s_nativeProbeDecagon[corner][0];
	v->position[2] = s_nativeProbeDecagon[corner][1];
	v->texcoord[0] = NATIVE_PROBE_WHEEL_UV;
	v->texcoord[1] = NATIVE_PROBE_WHEEL_UV;
	memcpy(v->color, stripe ? stripes : tyre, 4);
}

const struct NativeProbeVertex *NativeProbe_WheelVertices(int half)
{
	int h;
	int q;

	if ((half < 0) || (half >= NATIVE_PROBE_WHEEL_HALVES))
	{
		return NULL;
	}

	if (!s_nativeProbeWheelMade)
	{
		for (h = 0; h < NATIVE_PROBE_WHEEL_HALVES; h++)
		{
			struct NativeProbeVertex *out = s_nativeProbeWheel[h];
			const float capX = (h == 0) ? 1.0f : -1.0f;

			// Five tread quads: corners 0..5 in half 0, 5..10 (10 is 0) in half 1.
			for (q = 0; q < 5; q++)
			{
				const int k = (h * 5) + q;
				const int stripe = (q == 0);

				NativeProbe_WheelPoint(&out[(q * 4) + 0], 1.0f, k, stripe);
				NativeProbe_WheelPoint(&out[(q * 4) + 1], -1.0f, k, stripe);
				NativeProbe_WheelPoint(&out[(q * 4) + 2], -1.0f, k + 1, stripe);
				NativeProbe_WheelPoint(&out[(q * 4) + 3], 1.0f, k + 1, stripe);
			}

			// Four cap quads: the outer cap in half 0, the inner one in half 1.
			for (q = 0; q < 4; q++)
			{
				struct NativeProbeVertex *quad = &out[(5 + q) * 4];
				const int stripe = (q == 0);

				NativeProbe_WheelPoint(&quad[0], capX, 0, stripe);
				NativeProbe_WheelPoint(&quad[1], capX, (h == 0) ? ((2 * q) + 1) : ((2 * q) + 3), stripe);
				NativeProbe_WheelPoint(&quad[2], capX, (2 * q) + 2, stripe);
				NativeProbe_WheelPoint(&quad[3], capX, (h == 0) ? ((2 * q) + 3) : ((2 * q) + 1), stripe);
			}
		}
		s_nativeProbeWheelMade = 1;
	}

	return s_nativeProbeWheel[half];
}

// THE FORM POSE. The top of the textured body is lowered as a wedge. Of the
// four top corners (y = 1, x = +-1, z = +-1) and the middle of the back edge
// (x = 0, z = -1) the one that lies in the direction of the phase sinks by
// 3/16, its neighbours by 3/32, the rest stays. The directions in steps of 45
// degrees, x to the right and z to the front seen from above: (1,1) is 1,
// (-1,1) 3, (-1,-1) 5, (0,-1) 6, (1,-1) 7. So the wedge goes once round the
// top in eight phases.
//
// THE MIDDLE OF THE FRONT EDGE (x = 0, z = 1) takes the mean of the two front
// corners: the front face is one quad from corner to corner, so a middle point
// of its own height would leave a crack between the top and the front (a
// T-junction). The back face is split at x = 0 into two quads that share the
// middle point with the top, so the back edge may bend there and stays closed.
// The sides run from corner to corner like the top edges they meet. So the
// body is closed in every phase.
//
// Every height is 1, 29/32 or 13/16 (and in the front middle a mean of two of
// them, a multiple of 1/64) - exact in float32, and the lowest top (13/16)
// stays above the inner plate (at most 3/4 high). Positions x and z and every
// texture coordinate stay as they are.
internal float NativeProbe_PoseHeight(int direction, int phase)
{
	int distance = (direction - phase) & (NATIVE_PROBE_POSE_PHASES - 1);

	if (distance > (NATIVE_PROBE_POSE_PHASES / 2))
	{
		distance = NATIVE_PROBE_POSE_PHASES - distance;
	}

	if (distance == 0)
	{
		return 1.0f - (3.0f / 16.0f);
	}
	if (distance == 1)
	{
		return 1.0f - (3.0f / 32.0f);
	}
	return 1.0f;
}

void NativeProbe_PoseVertices(int phase, struct NativeProbeVertex out[NATIVE_PROBE_VERTEX_COUNT])
{
	int v;

	phase &= (NATIVE_PROBE_POSE_PHASES - 1);
	memcpy(out, s_nativeProbeTexturedVertices, sizeof(s_nativeProbeTexturedVertices));

	for (v = 0; v < NATIVE_PROBE_VERTEX_COUNT; v++)
	{
		const float x = out[v].position[0];
		const float z = out[v].position[2];

		if (out[v].position[1] != 1.0f)
		{
			continue;
		}

		if (z > 0.0f)
		{
			if (x > 0.0f)
			{
				out[v].position[1] = NativeProbe_PoseHeight(1, phase);
			}
			else if (x < 0.0f)
			{
				out[v].position[1] = NativeProbe_PoseHeight(3, phase);
			}
			else
			{
				out[v].position[1] = (NativeProbe_PoseHeight(1, phase) + NativeProbe_PoseHeight(3, phase)) * 0.5f;
			}
		}
		else
		{
			out[v].position[1] = NativeProbe_PoseHeight((x > 0.0f) ? 7 : ((x < 0.0f) ? 5 : 6), phase);
		}
	}
}

const struct NativeProbeVertex *NativeProbe_Vertices(void)
{
	return s_nativeProbeVertices;
}

const u16 *NativeProbe_Indices(void)
{
	return s_nativeProbeIndices;
}

// The hash of some bytes as 64 lower-case hex digits. Sha256 comes from
// rldtrack.inc, which platform/native_assets.c pulls into this build.
static void NativeProbe_HashHex(const void *bytes, size_t count, char hex[65])
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

// The vertices as they lie in memory, then the indices: the same bytes the
// renderer copies into its two buffers - for the body and for the form texture.
// Then the texture bytes as the renderer uploads them. One line either way.
int NativeProbe_SelfTest(void)
{
	u8 bytes[NATIVE_PROBE_BYTES];
	char body[65];
	char textured[65];
	char texture[65];
	char pose[65];
	char wheel[65];
	char wheelTexture[65];
	char mips[65];
	static struct NativeProbeVertex poses[NATIVE_PROBE_POSE_PHASES][NATIVE_PROBE_VERTEX_COUNT];
	static struct NativeProbeVertex wheels[NATIVE_PROBE_WHEEL_HALVES][NATIVE_PROBE_VERTEX_COUNT];
	static u8 textures[2][NATIVE_PROBE_TEXTURE_BYTES];
	int phase;
	int half;

	memcpy(bytes, s_nativeProbeVertices, sizeof(s_nativeProbeVertices));
	memcpy(bytes + sizeof(s_nativeProbeVertices), s_nativeProbeIndices, sizeof(s_nativeProbeIndices));
	NativeProbe_HashHex(bytes, sizeof(bytes), body);

	memcpy(bytes, s_nativeProbeTexturedVertices, sizeof(s_nativeProbeTexturedVertices));
	memcpy(bytes + sizeof(s_nativeProbeTexturedVertices), s_nativeProbeIndices, sizeof(s_nativeProbeIndices));
	NativeProbe_HashHex(bytes, sizeof(bytes), textured);

	// Both textures made here, apart from the one a run uploads: the hash of
	// the form texture is the one without the wheel texel in any run.
	NativeProbe_MakeTexture(textures[0], 0);
	NativeProbe_MakeTexture(textures[1], 1);
	NativeProbe_HashHex(textures[0], NATIVE_PROBE_TEXTURE_BYTES, texture);
	NativeProbe_HashHex(textures[1], NATIVE_PROBE_TEXTURE_BYTES, wheelTexture);

	for (half = 0; half < NATIVE_PROBE_WHEEL_HALVES; half++)
	{
		memcpy(wheels[half], NativeProbe_WheelVertices(half), sizeof(wheels[half]));
	}
	NativeProbe_HashHex(wheels, sizeof(wheels), wheel);

	for (phase = 0; phase < NATIVE_PROBE_POSE_PHASES; phase++)
	{
		NativeProbe_PoseVertices(phase, poses[phase]);
	}
	NativeProbe_HashHex(poses, sizeof(poses), pose);

	// The mips texture made here as well, apart from the one a run uploads.
	{
		static u8 levels[NATIVE_PROBE_MIPS_BYTES];

		NativeProbe_MakeMips(levels);
		NativeProbe_HashHex(levels, sizeof(levels), mips);
	}

	if ((strcmp(body, NATIVE_PROBE_GOLDEN) != 0) || (strcmp(textured, NATIVE_PROBE_TEXTURED_GOLDEN) != 0) ||
	    (strcmp(texture, NATIVE_PROBE_TEXTURE_GOLDEN) != 0) || (strcmp(pose, NATIVE_PROBE_POSE_GOLDEN) != 0) ||
	    (strcmp(wheel, NATIVE_PROBE_WHEEL_GOLDEN) != 0) || (strcmp(wheelTexture, NATIVE_PROBE_WHEEL_TEXTURE_GOLDEN) != 0) ||
	    (strcmp(mips, NATIVE_PROBE_MIPS_GOLDEN) != 0))
	{
		printf("native probe selftest: sha256 %s, textured mesh %s, texture %s, pose meshes %s, wheel halves %s, wheel texture %s, mips texture %s "
		       "differ from the golden %s, %s, %s, %s, %s, %s, %s\n",
		       body, textured, texture, pose, wheel, wheelTexture, mips, NATIVE_PROBE_GOLDEN, NATIVE_PROBE_TEXTURED_GOLDEN, NATIVE_PROBE_TEXTURE_GOLDEN,
		       NATIVE_PROBE_POSE_GOLDEN, NATIVE_PROBE_WHEEL_GOLDEN, NATIVE_PROBE_WHEEL_TEXTURE_GOLDEN, NATIVE_PROBE_MIPS_GOLDEN);
		return 1;
	}

	// The line of before first, word for word, then the mips texture.
	printf("native probe selftest passed: %d vertices, %d indices, sha256 %s, textured mesh sha256 %s, texture %dx%d sha256 %s, "
	       "%d pose meshes sha256 %s, %d wheel halves sha256 %s, wheel texture sha256 %s, mips texture %d levels sha256 %s\n",
	       NATIVE_PROBE_VERTEX_COUNT, NATIVE_PROBE_INDEX_COUNT, body, textured, NATIVE_PROBE_TEXTURE_SIZE, NATIVE_PROBE_TEXTURE_SIZE, texture,
	       NATIVE_PROBE_POSE_PHASES, pose, NATIVE_PROBE_WHEEL_HALVES, wheel, wheelTexture, NATIVE_PROBE_MIPS_LEVELS, mips);
	return 0;
}
