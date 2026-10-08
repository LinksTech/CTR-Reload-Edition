// ===========================================================================
// THE NATIVE PROBE - A TEST BODY MADE IN CODE.
//
// With --native-preview --native-probe body, seat 0 of a one-player arcade
// race (or the seat --native-probe-seat names) draws this body instead of its
// retail model, through the native split of the render layer
// (platform/native_render_layer.c binds it and routes, platform/
// native_renderer.c holds the buffers and draws). With --native-probe texture
// the same body takes every colour from a texture made here as well. This
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
	if (strcmp(name, "pose") == 0)
	{
		return NATIVE_PROBE_POSE;
	}
	return NATIVE_PROBE_NONE;
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

const u8 *NativeProbe_TexturePixels(void)
{
	static const u8 green[4] = {0, 255, 0, 255};
	static const u8 blue[4] = {0, 0, 255, 255};
	static const u8 checkerA[4] = {128, 0, 255, 255};
	static const u8 checkerB[4] = {0, 255, 128, 255};
	static const u8 red[4] = {255, 0, 0, 255};
	static const u8 grey[4] = {128, 128, 128, 255};
	int x;
	int y;

	if (s_nativeProbeTextureMade)
	{
		return s_nativeProbeTexture;
	}

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

			memcpy(&s_nativeProbeTexture[((y * NATIVE_PROBE_TEXTURE_SIZE) + x) * 4], colour, 4);
		}
	}

	s_nativeProbeTextureMade = 1;
	return s_nativeProbeTexture;
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
	static struct NativeProbeVertex poses[NATIVE_PROBE_POSE_PHASES][NATIVE_PROBE_VERTEX_COUNT];
	int phase;

	memcpy(bytes, s_nativeProbeVertices, sizeof(s_nativeProbeVertices));
	memcpy(bytes + sizeof(s_nativeProbeVertices), s_nativeProbeIndices, sizeof(s_nativeProbeIndices));
	NativeProbe_HashHex(bytes, sizeof(bytes), body);

	memcpy(bytes, s_nativeProbeTexturedVertices, sizeof(s_nativeProbeTexturedVertices));
	memcpy(bytes + sizeof(s_nativeProbeTexturedVertices), s_nativeProbeIndices, sizeof(s_nativeProbeIndices));
	NativeProbe_HashHex(bytes, sizeof(bytes), textured);

	NativeProbe_HashHex(NativeProbe_TexturePixels(), NATIVE_PROBE_TEXTURE_BYTES, texture);

	for (phase = 0; phase < NATIVE_PROBE_POSE_PHASES; phase++)
	{
		NativeProbe_PoseVertices(phase, poses[phase]);
	}
	NativeProbe_HashHex(poses, sizeof(poses), pose);

	if ((strcmp(body, NATIVE_PROBE_GOLDEN) != 0) || (strcmp(textured, NATIVE_PROBE_TEXTURED_GOLDEN) != 0) ||
	    (strcmp(texture, NATIVE_PROBE_TEXTURE_GOLDEN) != 0) || (strcmp(pose, NATIVE_PROBE_POSE_GOLDEN) != 0))
	{
		printf("native probe selftest: sha256 %s, textured mesh %s, texture %s, pose meshes %s differ from the golden %s, %s, %s, %s\n", body,
		       textured, texture, pose, NATIVE_PROBE_GOLDEN, NATIVE_PROBE_TEXTURED_GOLDEN, NATIVE_PROBE_TEXTURE_GOLDEN, NATIVE_PROBE_POSE_GOLDEN);
		return 1;
	}

	printf("native probe selftest passed: %d vertices, %d indices, sha256 %s, textured mesh sha256 %s, texture %dx%d sha256 %s, "
	       "%d pose meshes sha256 %s\n",
	       NATIVE_PROBE_VERTEX_COUNT, NATIVE_PROBE_INDEX_COUNT, body, textured, NATIVE_PROBE_TEXTURE_SIZE, NATIVE_PROBE_TEXTURE_SIZE, texture,
	       NATIVE_PROBE_POSE_PHASES, pose);
	return 0;
}
