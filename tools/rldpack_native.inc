// rldpack_native.inc - CNET and CTXT, the native model of a character (PREVIEW)
//
// Part of the packer, never of the game. tools/rldpack_char.inc includes it
// before make-char. The layouts, the rules CNET-1..11 and CTXT-1..8 and the
// reader are include/rldchar.inc (section CNET AND CTXT); this file only
// writes.
//
// WHAT IS WRITTEN:
//   --native-probe <still|poses>  the test body of the native probe
//       (platform/native_probe.c, --native-probe texture/pose) as a
//       character's native model - the same 36 vertices, UVs and 256 x 256
//       texture, with a wheel of 64 triangles - fitted into the hulls of the
//       CMDL that make-char built.
//   --native-model on             the author's own OBJ, its materials and
//       texture files (renderer concept C.11), placed by the same chain as
//       its CMDL: THE NATIVE MODEL OF AN OBJ below.
// Without either switch no byte of a container changes (the golden hashes
// of the self-test); the two switches exclude each other.
//
// DETERMINISM: the texture and the body are tables and integer rules; the
// fit uses only the frame hulls, products and sums of doubles rounded once to
// float; sine and cosine are RldMk_SinCos, square roots RldMk_Sqrt. The same
// CMDL gives the same bytes, and the same OBJ with the same files the same
// native model.

#define RLDMK_NATIVE_VERTICES 36u
#define RLDMK_NATIVE_TRIANGLES 18u
#define RLDMK_NATIVE_TEXTURE 256u
#define RLDMK_NATIVE_WHEEL_SEGMENTS 16u
#define RLDMK_NATIVE_WHEEL_VERTICES (2u * RLDMK_NATIVE_WHEEL_SEGMENTS + 2u * (RLDMK_NATIVE_WHEEL_SEGMENTS + 1u))   // 66
#define RLDMK_NATIVE_WHEEL_TRIANGLES (4u * RLDMK_NATIVE_WHEEL_SEGMENTS)                                          // 64
#define RLDMK_NATIVE_WHEEL_RADIUS 12.0
#define RLDMK_NATIVE_WHEEL_HALF_WIDTH 5.0
#define RLDMK_NATIVE_INSET 0.1          // the body keeps this share of the hull free on every side

// The textured unit body of platform/native_probe.c (s_nativeProbeTexturedVertices):
// x -1..1 (+X left seen from behind), y 0..1, z -1..1 (+Z front); UV in texels
// of the 256 x 256 texture. Nine quads, the normal of each the right-hand
// normal of its corners (outwards; the inner plate faces -Z).
static const struct
{
	float position[3];
	u32 texel[2];
} s_rldMkNativeBody[RLDMK_NATIVE_VERTICES] = {
    {{1.0f, 0.0f, -1.0f}, {4, 4}},       {{1.0f, 1.0f, -1.0f}, {4, 124}},     {{1.0f, 1.0f, 1.0f}, {124, 124}},   {{1.0f, 0.0f, 1.0f}, {124, 4}},
    {{-1.0f, 0.0f, 1.0f}, {132, 4}},     {{-1.0f, 1.0f, 1.0f}, {132, 124}},   {{-1.0f, 1.0f, -1.0f}, {252, 124}}, {{-1.0f, 0.0f, -1.0f}, {252, 4}},
    {{0.0f, 1.0f, -1.0f}, {64, 132}},    {{0.0f, 1.0f, 1.0f}, {64, 252}},     {{1.0f, 1.0f, 1.0f}, {124, 252}},   {{1.0f, 1.0f, -1.0f}, {124, 132}},
    {{-1.0f, 1.0f, -1.0f}, {4, 132}},    {{-1.0f, 1.0f, 1.0f}, {4, 252}},     {{0.0f, 1.0f, 1.0f}, {64, 252}},    {{0.0f, 1.0f, -1.0f}, {64, 132}},
    {{1.0f, 0.0f, -1.0f}, {132, 196}},   {{1.0f, 0.0f, 1.0f}, {132, 252}},    {{-1.0f, 0.0f, 1.0f}, {252, 252}},  {{-1.0f, 0.0f, -1.0f}, {252, 196}},
    {{-1.0f, 0.0f, 1.0f}, {132, 132}},   {{1.0f, 0.0f, 1.0f}, {132, 188}},    {{1.0f, 1.0f, 1.0f}, {252, 188}},   {{-1.0f, 1.0f, 1.0f}, {252, 132}},
    {{1.0f, 0.0f, -1.0f}, {4, 4}},       {{0.0f, 0.0f, -1.0f}, {4, 124}},     {{0.0f, 1.0f, -1.0f}, {124, 124}},  {{1.0f, 1.0f, -1.0f}, {124, 4}},
    {{0.0f, 0.0f, -1.0f}, {132, 4}},     {{-1.0f, 0.0f, -1.0f}, {132, 124}},  {{-1.0f, 1.0f, -1.0f}, {252, 124}}, {{0.0f, 1.0f, -1.0f}, {252, 4}},
    {{0.5f, 0.25f, 0.0f}, {132, 132}},   {{-0.5f, 0.25f, 0.0f}, {132, 188}},  {{-0.5f, 0.75f, 0.0f}, {252, 188}}, {{0.5f, 0.75f, 0.0f}, {252, 132}},
};

static const float s_rldMkNativeQuadNormal[9][3] = {
    {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f},
};

// The texture of the probe (NativeProbe_TexturePixels): green, blue, a checker
// of two colors, red and grey regions. rgba holds 256 x 256 x 4 bytes.
static void RldMk_NativeTexture(u8 *rgba)
{
	static const u8 green[4] = {0, 255, 0, 255};
	static const u8 blue[4] = {0, 0, 255, 255};
	static const u8 checkerA[4] = {128, 0, 255, 255};
	static const u8 checkerB[4] = {0, 255, 128, 255};
	static const u8 red[4] = {255, 0, 0, 255};
	static const u8 grey[4] = {128, 128, 128, 255};
	u32 x;
	u32 y;

	for (y = 0; y < RLDMK_NATIVE_TEXTURE; y++)
	{
		for (x = 0; x < RLDMK_NATIVE_TEXTURE; x++)
		{
			const u8 *colour;

			if (y < 128u)
			{
				colour = (x < 128u) ? green : blue;
			}
			else if (x < 128u)
			{
				colour = ((x >= 64u) == (y >= 192u)) ? checkerA : checkerB;
			}
			else
			{
				colour = (y < 192u) ? red : grey;
			}
			memcpy(&rgba[((y * RLDMK_NATIVE_TEXTURE) + x) * 4u], colour, 4u);
		}
	}
}

// The height of a top corner in pose phase 0..7 (NativeProbe_PoseHeight): the
// corner in the direction of the phase sinks by 3/16, its neighbours by 3/32.
static float RldMk_NativePoseHeight(int direction, int phase)
{
	int distance = (direction - phase) & 7;

	if (distance > 4)
	{
		distance = 8 - distance;
	}
	return (distance == 0) ? (1.0f - (3.0f / 16.0f)) : ((distance == 1) ? (1.0f - (3.0f / 32.0f)) : 1.0f);
}

// The unit y of vertex v in phase (NativeProbe_PoseVertices); phase < 0: still.
static float RldMk_NativeUnitY(u32 v, int phase)
{
	const float x = s_rldMkNativeBody[v].position[0];
	const float y = s_rldMkNativeBody[v].position[1];
	const float z = s_rldMkNativeBody[v].position[2];

	if ((phase < 0) || (y != 1.0f))
	{
		return y;
	}
	if (z > 0.0f)
	{
		if (x > 0.0f)
		{
			return RldMk_NativePoseHeight(1, phase);
		}
		if (x < 0.0f)
		{
			return RldMk_NativePoseHeight(3, phase);
		}
		return (RldMk_NativePoseHeight(1, phase) + RldMk_NativePoseHeight(3, phase)) * 0.5f;
	}
	return RldMk_NativePoseHeight((x > 0.0f) ? 7 : ((x < 0.0f) ? 5 : 6), phase);
}

static void RldMk_NativeBE32(u8 *at, u32 value)
{
	at[0] = (u8)(value >> 24);
	at[1] = (u8)(value >> 16);
	at[2] = (u8)(value >> 8);
	at[3] = (u8)value;
}

static void RldMk_PutF32(u8 *at, double value)
{
	const float f = (float)value;
	u32 word;

	memcpy(&word, &f, sizeof(word));
	Rld_WriteLE32(at, word);
}

// A PNG of 8-bit RGBA, rows from the top: filter 0 on every row, the zlib
// stream in stored blocks (RFC 1950, RFC 1951 3.2.4), no ancillary chunk.
// Larger than a compressed one, readable by include/rldpng.inc and every other
// reader, and the same bytes everywhere. NULL = out of memory.
static u8 *RldMk_PngRgba(const u8 *rgba, u32 width, u32 height, size_t *sizeOut)
{
	static const u8 signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	const size_t rowBytes = 1u + ((size_t)width * 4u);
	const size_t raw = rowBytes * height;
	const size_t blocks = (raw + 65534u) / 65535u;
	const size_t zlib = 2u + raw + (5u * blocks) + 4u;
	const size_t size = 8u + (12u + 13u) + (12u + zlib) + 12u;
	u8 *png = (u8 *)malloc(size);
	u8 *rows = (u8 *)malloc(raw ? raw : 1u);
	size_t at = 0;
	size_t done = 0;
	u32 y;

	*sizeOut = 0;
	if ((png == NULL) || (rows == NULL))
	{
		free(png);
		free(rows);
		return NULL;
	}
	for (y = 0; y < height; y++)
	{
		rows[y * rowBytes] = 0;
		memcpy(&rows[(y * rowBytes) + 1u], &rgba[(size_t)y * width * 4u], (size_t)width * 4u);
	}

	memcpy(png, signature, 8u);
	at = 8u;

	RldMk_NativeBE32(&png[at], 13u);
	memcpy(&png[at + 4u], "IHDR", 4u);
	RldMk_NativeBE32(&png[at + 8u], width);
	RldMk_NativeBE32(&png[at + 12u], height);
	png[at + 16u] = 8;   // depth
	png[at + 17u] = 6;   // RGBA
	png[at + 18u] = 0;
	png[at + 19u] = 0;
	png[at + 20u] = 0;   // no interlace
	RldMk_NativeBE32(&png[at + 21u], RldPng_Crc(&png[at + 4u], 17u));
	at += 25u;

	RldMk_NativeBE32(&png[at], (u32)zlib);
	memcpy(&png[at + 4u], "IDAT", 4u);
	{
		u8 *z = &png[at + 8u];
		size_t zat = 0;

		z[zat++] = 0x78;   // deflate, 32 KiB window
		z[zat++] = 0x01;   // check bits: 0x7801 is a multiple of 31, no dictionary
		while (done < raw)
		{
			const size_t length = ((raw - done) < 65535u) ? (raw - done) : 65535u;

			z[zat++] = (u8)(((done + length) == raw) ? 1u : 0u);
			z[zat++] = (u8)(length & 0xFFu);
			z[zat++] = (u8)(length >> 8);
			z[zat++] = (u8)(~length & 0xFFu);
			z[zat++] = (u8)((~length >> 8) & 0xFFu);
			memcpy(&z[zat], &rows[done], length);
			zat += length;
			done += length;
		}
		RldMk_NativeBE32(&z[zat], RldPng_Adler(rows, raw));
	}
	RldMk_NativeBE32(&png[at + 8u + zlib], RldPng_Crc(&png[at + 4u], 4u + zlib));
	at += 12u + zlib;

	RldMk_NativeBE32(&png[at], 0u);
	memcpy(&png[at + 4u], "IEND", 4u);
	RldMk_NativeBE32(&png[at + 8u], RldPng_Crc(&png[at + 4u], 4u));
	at += 12u;

	free(rows);
	*sizeOut = at;
	return png;
}

// CTXT with one texture: the probe texture as PNG, sRGB, repeat, opaque.
static u8 *RldMk_NativeCtxt(size_t *sizeOut)
{
	u8 *rgba = (u8 *)malloc((size_t)RLDMK_NATIVE_TEXTURE * RLDMK_NATIVE_TEXTURE * 4u);
	size_t pngSize = 0;
	u8 *png;
	u8 *ctxt;
	const u32 dataAt = RLDCHAR_TEX_HEAD_BYTES + RLDCHAR_TEX_ENTRY_BYTES;

	*sizeOut = 0;
	if (rgba == NULL)
	{
		return NULL;
	}
	RldMk_NativeTexture(rgba);
	png = RldMk_PngRgba(rgba, RLDMK_NATIVE_TEXTURE, RLDMK_NATIVE_TEXTURE, &pngSize);
	free(rgba);
	if (png == NULL)
	{
		return NULL;
	}

	ctxt = (u8 *)calloc(1, dataAt + pngSize);
	if (ctxt == NULL)
	{
		free(png);
		return NULL;
	}
	Rld_WriteLE16(&ctxt[0x00], RLDCHAR_TEX_VERSION);
	Rld_WriteLE16(&ctxt[0x04], 1u);
	Rld_WriteLE16(&ctxt[0x06], RLDCHAR_TEX_ENTRY_BYTES);
	{
		u8 *e = &ctxt[RLDCHAR_TEX_HEAD_BYTES];

		e[0x00] = RLDCHAR_TEX_FORMAT_PNG;
		Rld_WriteLE16(&e[0x02], 0u);
		Rld_WriteLE16(&e[0x04], RLDMK_NATIVE_TEXTURE);
		Rld_WriteLE16(&e[0x06], RLDMK_NATIVE_TEXTURE);
		Rld_WriteLE32(&e[0x08], dataAt);
		Rld_WriteLE32(&e[0x0C], (u32)pngSize);
		Sha256(png, pngSize, &e[0x10]);
	}
	memcpy(&ctxt[dataAt], png, pngSize);
	free(png);

	*sizeOut = dataAt + pngSize;
	return ctxt;
}

// One section entry of the CNET table.
static void RldMk_NativeSection(u8 *entry, const char tag[4], u32 stride, u32 offset, u32 size)
{
	memcpy(entry, tag, 4u);
	Rld_WriteLE16(&entry[4], 1u);
	Rld_WriteLE16(&entry[6], stride);
	Rld_WriteLE32(&entry[8], offset);
	Rld_WriteLE32(&entry[12], size);
}

// WHLS of the test wheel (the probe's, 3b): a cylinder of RLDMK_NATIVE_WHEEL_SEGMENTS
// sides, radius and half width as above, at the retail wheel points, its
// triangles of `material`. w holds RLDMK_NATIVE_WHEEL_BYTES, zeroed.
#define RLDMK_NATIVE_WHEEL_BYTES (RLDCHAR_WHEEL_HEAD_BYTES + (RLDMK_NATIVE_WHEEL_VERTICES * RLDCHAR_WHEEL_VERTEX_BYTES) + (RLDMK_NATIVE_WHEEL_TRIANGLES * 6u))

static void RldMk_NativeWheel(u8 *w, u32 material)
{
	u8 *vert = &w[RLDCHAR_WHEEL_HEAD_BYTES];
	u8 *tri = &vert[RLDMK_NATIVE_WHEEL_VERTICES * RLDCHAR_WHEEL_VERTEX_BYTES];
	const u32 n = RLDMK_NATIVE_WHEEL_SEGMENTS;
	const u32 capPlus = 2u * n;
	const u32 capMinus = capPlus + n + 1u;
	u32 t = 0;
	u32 k;

	// The retail wheel points in model units (DrawTires: x 0x90, y 0x40,
	// z 0xc7 / -0x60, a quarter each).
	Rld_WriteLE32(&w[0x00], RLDMK_NATIVE_WHEEL_VERTICES);
	Rld_WriteLE32(&w[0x04], RLDMK_NATIVE_WHEEL_TRIANGLES);
	Rld_WriteLE16(&w[0x08], material);
	RldMk_PutF32(&w[0x0C], RLDMK_NATIVE_WHEEL_RADIUS);
	RldMk_PutF32(&w[0x10], RLDMK_NATIVE_WHEEL_HALF_WIDTH);
	RldMk_PutF32(&w[0x14], 36.0);
	RldMk_PutF32(&w[0x18], 16.0);
	RldMk_PutF32(&w[0x1C], 49.75);
	RldMk_PutF32(&w[0x20], 36.0);
	RldMk_PutF32(&w[0x24], 16.0);
	RldMk_PutF32(&w[0x28], -24.0);

	for (k = 0; k < n; k++)
	{
		double s;
		double c;
		u32 side;

		RldMk_SinCos((360.0 * (double)k) / (double)n, &s, &c);
		for (side = 0; side < 2u; side++)
		{
			const double x = side ? -RLDMK_NATIVE_WHEEL_HALF_WIDTH : RLDMK_NATIVE_WHEEL_HALF_WIDTH;
			u8 *a = &vert[((2u * k) + side) * RLDCHAR_WHEEL_VERTEX_BYTES];
			u8 *r = &vert[((side ? capMinus : capPlus) + 1u + k) * RLDCHAR_WHEEL_VERTEX_BYTES];

			RldMk_PutF32(&a[0], x);
			RldMk_PutF32(&a[4], RLDMK_NATIVE_WHEEL_RADIUS * c);
			RldMk_PutF32(&a[8], RLDMK_NATIVE_WHEEL_RADIUS * s);
			RldMk_PutF32(&a[16], c);
			RldMk_PutF32(&a[20], s);
			RldMk_PutF32(&r[0], x);
			RldMk_PutF32(&r[4], RLDMK_NATIVE_WHEEL_RADIUS * c);
			RldMk_PutF32(&r[8], RLDMK_NATIVE_WHEEL_RADIUS * s);
			RldMk_PutF32(&r[12], side ? -1.0 : 1.0);
		}
	}
	RldMk_PutF32(&vert[(capPlus * RLDCHAR_WHEEL_VERTEX_BYTES) + 0u], RLDMK_NATIVE_WHEEL_HALF_WIDTH);
	RldMk_PutF32(&vert[(capPlus * RLDCHAR_WHEEL_VERTEX_BYTES) + 12u], 1.0);
	RldMk_PutF32(&vert[(capMinus * RLDCHAR_WHEEL_VERTEX_BYTES) + 0u], -RLDMK_NATIVE_WHEEL_HALF_WIDTH);
	RldMk_PutF32(&vert[(capMinus * RLDCHAR_WHEEL_VERTEX_BYTES) + 12u], -1.0);

	// Counter-clockwise from outside: the tread (a, b, c) and (a, c, d)
	// with a = +X at k, b = -X at k, c = -X at k + 1, d = +X at k + 1;
	// the caps as fans about their centers.
	for (k = 0; k < n; k++)
	{
		const u32 next = (k + 1u) % n;
		const u32 tri6[4][3] = {{2u * k, (2u * k) + 1u, (2u * next) + 1u},
		                        {2u * k, (2u * next) + 1u, 2u * next},
		                        {capPlus, capPlus + 1u + k, capPlus + 1u + next},
		                        {capMinus, capMinus + 1u + next, capMinus + 1u + k}};
		u32 j;

		for (j = 0; j < 4u; j++)
		{
			Rld_WriteLE16(&tri[(t * 6u) + 0u], tri6[j][0]);
			Rld_WriteLE16(&tri[(t * 6u) + 2u], tri6[j][1]);
			Rld_WriteLE16(&tri[(t * 6u) + 4u], tri6[j][2]);
			t++;
		}
	}
}

// CNET of the probe body, fitted into the hulls of a built CMDL: the box
// spans the common part of all 47 frame hulls less RLDMK_NATIVE_INSET on
// every side. poses 0: one still pose; 1: 47 poses, the top shaped by phase
// frame & 7 like --native-probe pose. wheels 0: no WHLS (CHRI hides the
// wheels). NULL with *why on a CMDL whose hulls leave no room.
static u8 *RldMk_NativeCnet(const u8 *cmdl, size_t cmdlSize, int poses, int wheels, size_t *sizeOut, const char **why)
{
	struct RldCharHull hulls[RLDCHAR_NET_POSES];
	double lo[3];
	double hi[3];
	double base[3];
	double span[3];
	float hullMin[3];
	float hullMax[3];
	const u32 sectionCount = wheels ? 6u : 5u;
	const u32 slots = poses ? RLDCHAR_NET_POSES : 1u;
	const u32 tableAt = RLDCHAR_NET_HEAD_BYTES;
	const u32 posnAt = tableAt + (sectionCount * RLDCHAR_NET_SECTION_BYTES);
	const u32 posnSize = slots * RLDMK_NATIVE_VERTICES * RLDCHAR_NET_VERTEX_BYTES;
	const u32 uvAt = posnAt + posnSize;
	const u32 uvSize = RLDMK_NATIVE_VERTICES * 8u;
	const u32 trisAt = uvAt + uvSize;
	const u32 trisSize = RLDMK_NATIVE_TRIANGLES * 6u;
	const u32 tmatAt = (trisAt + trisSize + 3u) & ~3u;
	const u32 tmatSize = RLDMK_NATIVE_TRIANGLES * 2u;
	const u32 matlAt = (tmatAt + tmatSize + 3u) & ~3u;
	const u32 matlSize = 2u * RLDCHAR_NET_MATERIAL_BYTES;
	const u32 whlsAt = matlAt + matlSize;
	const u32 whlsSize = wheels ? RLDMK_NATIVE_WHEEL_BYTES : 0u;
	const u32 size = whlsAt + whlsSize;
	u8 *cnet;
	u32 p;
	u32 v;
	u32 q;
	int axis;

	*sizeOut = 0;
	*why = NULL;
	if ((cmdlSize < 8u) || !RldChar_FrameHulls(&cmdl[4], Rld_ReadLE32(&cmdl[0]), 0u, hulls))
	{
		*why = "the frames of the model cannot be read";
		return NULL;
	}

	for (axis = 0; axis < 3; axis++)
	{
		lo[axis] = hulls[0].lo[axis];
		hi[axis] = hulls[0].hi[axis];
		for (p = 1; p < RLDCHAR_NET_POSES; p++)
		{
			lo[axis] = (hulls[p].lo[axis] > lo[axis]) ? hulls[p].lo[axis] : lo[axis];
			hi[axis] = (hulls[p].hi[axis] < hi[axis]) ? hulls[p].hi[axis] : hi[axis];
		}
		if (hi[axis] <= lo[axis])
		{
			*why = "the frames of the model have no common room";
			return NULL;
		}
		base[axis] = lo[axis] + (RLDMK_NATIVE_INSET * (hi[axis] - lo[axis]));
		span[axis] = (1.0 - (2.0 * RLDMK_NATIVE_INSET)) * (hi[axis] - lo[axis]);
		hullMin[axis] = 1.0e30f;
		hullMax[axis] = -1.0e30f;
	}

	cnet = (u8 *)calloc(1, size);
	if (cnet == NULL)
	{
		*why = "out of memory";
		return NULL;
	}

	// The poses: unit x -1..1 and z -1..1 to base..base + span, unit y 0..1 the same.
	for (p = 0; p < slots; p++)
	{
		for (v = 0; v < RLDMK_NATIVE_VERTICES; v++)
		{
			u8 *at = &cnet[posnAt + (((p * RLDMK_NATIVE_VERTICES) + v) * RLDCHAR_NET_VERTEX_BYTES)];
			const double unit[3] = {(s_rldMkNativeBody[v].position[0] + 1.0) * 0.5, (double)RldMk_NativeUnitY(v, poses ? (int)(p & 7u) : -1),
			                        (s_rldMkNativeBody[v].position[2] + 1.0) * 0.5};

			for (axis = 0; axis < 3; axis++)
			{
				const float value = (float)(base[axis] + (unit[axis] * span[axis]));

				RldMk_PutF32(&at[4 * axis], value);
				RldMk_PutF32(&at[12 + (4 * axis)], s_rldMkNativeQuadNormal[v / 4u][axis]);
				hullMin[axis] = (value < hullMin[axis]) ? value : hullMin[axis];
				hullMax[axis] = (value > hullMax[axis]) ? value : hullMax[axis];
			}
		}
	}

	// The head.
	Rld_WriteLE16(&cnet[0x00], RLDCHAR_NET_VERSION);
	Rld_WriteLE16(&cnet[0x04], RLDCHAR_NET_HEAD_BYTES);
	Rld_WriteLE16(&cnet[0x06], sectionCount);
	Rld_WriteLE32(&cnet[0x08], RLDMK_NATIVE_VERTICES);
	Rld_WriteLE32(&cnet[0x0C], RLDMK_NATIVE_TRIANGLES);
	Rld_WriteLE16(&cnet[0x10], 2u);
	Rld_WriteLE16(&cnet[0x12], 2u);
	Rld_WriteLE16(&cnet[0x14], poses ? RLDCHAR_NET_POSES : 0u);
	Rld_WriteLE16(&cnet[0x16], RLDCHAR_NET_POSE_FLOAT);
	for (axis = 0; axis < 3; axis++)
	{
		RldMk_PutF32(&cnet[0x18 + (4 * axis)], hullMin[axis]);
		RldMk_PutF32(&cnet[0x24 + (4 * axis)], hullMax[axis]);
	}
	Rld_WriteLE16(&cnet[0x30], 1u);

	RldMk_NativeSection(&cnet[tableAt + 0x00], "POSN", RLDCHAR_NET_VERTEX_BYTES, posnAt, posnSize);
	RldMk_NativeSection(&cnet[tableAt + 0x10], "UV00", 8u, uvAt, uvSize);
	RldMk_NativeSection(&cnet[tableAt + 0x20], "TRIS", 6u, trisAt, trisSize);
	RldMk_NativeSection(&cnet[tableAt + 0x30], "TMAT", 2u, tmatAt, tmatSize);
	RldMk_NativeSection(&cnet[tableAt + 0x40], "MATL", RLDCHAR_NET_MATERIAL_BYTES, matlAt, matlSize);
	if (wheels)
	{
		RldMk_NativeSection(&cnet[tableAt + 0x50], "WHLS", 0u, whlsAt, whlsSize);
	}

	for (v = 0; v < RLDMK_NATIVE_VERTICES; v++)
	{
		RldMk_PutF32(&cnet[uvAt + (8u * v)], (double)s_rldMkNativeBody[v].texel[0] / (double)RLDMK_NATIVE_TEXTURE);
		RldMk_PutF32(&cnet[uvAt + (8u * v) + 4u], (double)s_rldMkNativeBody[v].texel[1] / (double)RLDMK_NATIVE_TEXTURE);
	}
	for (q = 0; q < 9u; q++)
	{
		static const u32 corner[6] = {0, 1, 2, 0, 2, 3};
		u32 c;

		for (c = 0; c < 6u; c++)
		{
			Rld_WriteLE16(&cnet[trisAt + (12u * q) + (2u * c)], (4u * q) + corner[c]);
		}
	}
	// TMAT: every triangle material 0 (calloc). MATL: 0 white, texture 0;
	// 1 dark grey without texture, for the wheels.
	memset(&cnet[matlAt], 255, 4u);
	Rld_WriteLE16(&cnet[matlAt + 4u], 0u);
	cnet[matlAt + 16u] = 64;
	cnet[matlAt + 17u] = 64;
	cnet[matlAt + 18u] = 64;
	cnet[matlAt + 19u] = 255;
	Rld_WriteLE16(&cnet[matlAt + 20u], 0xFFFFu);

	if (wheels)
	{
		RldMk_NativeWheel(&cnet[whlsAt], 1u);
	}

	*sizeOut = size;
	return cnet;
}

//========================================================================================
// THE NATIVE MODEL OF AN OBJ (make-char --native-model on, PREVIEW)
//========================================================================================
//
// CNET and CTXT from the author's own OBJ, its materials and texture files
// (renderer concept C.11), next to the CMDL the chain built from the same
// file. The OBJ reader keeps the faces as written (RldObj_CaptureNative); the
// chain builds the CMDL exactly as without the switch; then this part reads
// both and changes neither.
//
// THE PLACE of every vertex is the chain's own: the OBJ v is found by its
// weld key (RldMk_StepWeld: x y z on a 1e-5 grid) among the PLY points, and
// its welded position (after the repair: RldMkBuild.plyPosition; points the
// repair welds share it, a triangle that loses its area so is left out)
// gives the neutral pose and the 47 frames of the build
// (RldMkBuild.frame: the axes of --up and --forward, the factor of --fit or
// --scale, the shift onto the reference dummy, --size, the automatic poses)
// in game units, the units of CMDL without the instance scale: +X left seen
// from behind, +Y up, +Z forward. Those turns are proper rotations, so the
// winding of the OBJ (counter-clockwise seen from outside) stays.
//   poses 0   every frame of the build is the neutral pose at every vertex
//             (a vehicle that stands still, --poses still): one still pose
//   poses 47  otherwise: frame by frame as the CMDL moves (21/7/15/4)
// A coordinate may lie up to two steps of the CMDL (its hull / 255) outside
// the hull of its frame, because the CMDL rounds to those steps; it is moved
// onto the hull's edge (counted, rldpack says how many). Further out is an
// internal error.
//
// TRIANGLES: each face of the OBJ as a fan (0, k, k + 1), the material that
// of its usemtl name. A triangle is left out (counted) when a corner has no
// position in the CMDL (the chain dropped every triangle on it), when two
// corners share a position, or when it has no area.
// THE REPAIR (--repair auto, the default): the game culls back faces, so the
// native triangles go through the same repair as the CMDL's (RldRep_Repair,
// tools/rldpack_repair.inc, with the chain's weld distance and hole limit), on
// the chain's positions: turned outward where the chain turns, the same holes
// closed (the lids cover the same loops; their diagonals may lie otherwise
// than the CMDL's, which was repaired on its own triangles), degenerate and
// doubled triangles dropped. A lid has the material of its corner 0 and, at
// every corner, the UV of corner 0 - one spot of the texture, nothing smeared
// across the hole; each corner keeps its position, normal and color (a corner
// whose position differs from its vertex's gets a vertex of its own). Parts still open afterwards are
// drawn two-sided in the CMDL (--open-parts two-sided, the default); CNET has
// no two-sided material, so such a triangle gets a second one, wound the other
// way, with vertices of their own (their normals point the other way). With
// --two-sided every triangle gets one; with --open-parts one-sided none.
// --repair off: the faces as written, no second triangles but those of
// --two-sided. Reduction and remesh of the chain are not followed: a model the
// chain reduced or remeshed is refused, because its native vertices would not
// find their positions.
//
// VERTICES: one per different (v, vt, vn, material), in the order the faces
// first use them. Normals per pose from the geometry: the area-weighted sum
// of the faces around a vertex that share its v and its vn (one vn, or no
// vn at all, is one smooth group; a hard edge of the OBJ has two vn),
// normalized; a sum of length 0 gives (0, 1, 0), counted. UV00: offset +
// scale x vt of the material's map_Kd (-o, -s; -clamp on clamps into 0..1),
// v turned (1 - v): CTXT rows run from the top, an OBJ's v from the bottom.
//
// COLORS by the rules of the OBJ reader for the CMDL (tools/rldpack_obj.inc,
// THE COLOR OF A CORNER), without the texture itself: when a vertex of the
// native mesh has a color, COL0 holds every vertex's color - its own (times
// 255/128, at most 1, on a textured face of a PS1 rip), else Kd, else white
// on a textured and grey 0x80 on an untextured face - and every material is
// white. Otherwise there is no COL0, and a material is Kd, else white with a
// texture, else grey 0x80. Alpha 255 everywhere: d and Tr are not used (the
// CMDL is drawn opaque too).
//
// TEXTURES: each file a material in use names and the reader read (state
// ok) is one CTXT entry, in the order of first use; files with the same bytes
// (SHA-256) share one entry. Each side must be a
// power of two from 16 to 2048 - never scaled, refused with a hint. A PNG of
// 8-bit RGBA keeps its bytes; any other file (another PNG, JPEG, TGA, BMP)
// is decoded and written as a PNG of 8-bit RGBA (RldMk_PngRgba). Flags:
// sRGB; wrap clamp on both axes when every corner of every face on it lies
// in 0..1, else repeat; alpha mode mask when a texel has alpha below 255,
// else opaque, and its materials the same. A material whose texture the
// reader could not use (missing, unreadable) is drawn untextured in its
// color, as the CMDL draws it (warning native-texture).
//
// WHEELS: CHRI hides the wheels (--wheels off): no WHLS. Otherwise the test
// wheel of the probe at the retail wheel points (RldMk_NativeWheel) in dark
// grey, because the game never draws a native body with the sprite wheels
// (CNET-10, renderer concept C.12) and an OBJ has no wheel mesh of its own.
//
// LIMITS (renderer concept C.10; the reader's RLDCHAR_*): 20000 vertices,
// 30000 triangles, 64 materials (the wheel's included), 16 textures, 64 MiB
// of RGBA8 with every mip level, 32 MiB per chunk. Over one: refused with the
// numbers, nothing written.

struct RldMkNativeFacts
{
	u32 vertices;
	u32 triangles;
	u32 poses;
	u32 materials;
	u32 textures;
	u32 dropped;               // triangles left out
	u32 clamped;               // coordinates moved onto a frame's hull
	u32 flat;                  // vertices whose normal is (0, 1, 0) for want of area
	u32 untextured;            // materials whose texture the reader could not use
	int repaired;              // the repair ran on the native triangles (--repair auto)
	u32 flipped;               // triangles the repair turned outward
	u32 lids;                  // triangles the repair added to close holes
	u32 openEdges;             // edges with one triangle after the repair (as the CMDL's "open edges")
	u32 backFaces;             // second triangles, wound the other way (open parts, --two-sided)
	u32 shared;                // texture files that share a CTXT entry with an earlier one (the same bytes)
	int colors;                // COL0 written
	int wheels;                // WHLS written
	u32 width[RLDCHAR_TEX_COUNT_MAX];
	u32 height[RLDCHAR_TEX_COUNT_MAX];
	int converted[RLDCHAR_TEX_COUNT_MAX];   // decoded and written again
	size_t cnetSize;
	size_t ctxtSize;
};

struct RldMkNativeVertex
{
	u32 v;
	u32 vt;                    // RLDMK_NONE = none
	u32 slot;                  // the usemtl name, or the slot of no material
	u32 position;              // the chain's welded position
	u32 group;                 // the smooth group (v, vn)
	u32 vn;                    // RLDMK_NONE = none
};

struct RldMkNativeTexture
{
	u32 width;
	u32 height;
	int alpha;                 // a texel below 255
	int outside;               // a corner on it lies outside 0..1
	int converted;
	u8 *png;
	size_t pngSize;
};

#define RLDMK_NATIVE_KEY 6u

// An open-addressing table of indices into its own array of keys.
struct RldMkNativeTable
{
	u32 *slot;
	u32 mask;
	u32 (*keys)[RLDMK_NATIVE_KEY];
	u32 count;
	u32 capacity;
};

static int RldMk_NativeTableInit(struct RldMkNativeTable *t, u32 items)
{
	u32 slots = 1024u;

	while (slots < (2u * items + 2u))
	{
		slots *= 2u;
	}
	memset(t, 0, sizeof(*t));
	t->slot = (u32 *)malloc((size_t)slots * sizeof(u32));
	t->keys = (u32(*)[RLDMK_NATIVE_KEY])malloc(((size_t)items + 1u) * sizeof(*t->keys));
	t->mask = slots - 1u;
	t->capacity = items;
	if (t->slot != NULL)
	{
		memset(t->slot, 0xFF, (size_t)slots * sizeof(u32));
	}
	return (t->slot != NULL) && (t->keys != NULL);
}

static void RldMk_NativeTableFree(struct RldMkNativeTable *t)
{
	free(t->slot);
	free(t->keys);
	memset(t, 0, sizeof(*t));
}

// The index of key; a new one is added as the next index when add is set
// (*added 1), else RLDMK_NONE. At most `items` keys (the table is then at
// most half full).
static u32 RldMk_NativeFind(struct RldMkNativeTable *t, const u32 key[RLDMK_NATIVE_KEY], int add, int *added)
{
	u32 h = 0x811C9DC5u;
	u32 at;
	u32 i;

	for (i = 0; i < RLDMK_NATIVE_KEY; i++)
	{
		h = (h ^ key[i]) * 0x01000193u;
		h ^= h >> 15;
	}
	*added = 0;
	for (at = h & t->mask;; at = (at + 1u) & t->mask)
	{
		const u32 index = t->slot[at];

		if (index == RLDMK_NONE)
		{
			if (!add || (t->count >= t->capacity))
			{
				return RLDMK_NONE;
			}
			memcpy(t->keys[t->count], key, RLDMK_NATIVE_KEY * sizeof(u32));
			t->slot[at] = t->count;
			*added = 1;
			return t->count++;
		}
		if (memcmp(t->keys[index], key, RLDMK_NATIVE_KEY * sizeof(u32)) == 0)
		{
			return index;
		}
	}
}


// Room for `items` keys; the keys keep their indices. 0 = out of memory.
static int RldMk_NativeTableGrow(struct RldMkNativeTable *t, u32 items)
{
	struct RldMkNativeTable grown;
	u32 i;
	int added;

	if (!RldMk_NativeTableInit(&grown, items))
	{
		RldMk_NativeTableFree(&grown);
		return 0;
	}
	for (i = 0; i < t->count; i++)
	{
		(void)RldMk_NativeFind(&grown, t->keys[i], 1, &added);
	}
	RldMk_NativeTableFree(t);
	*t = grown;
	return 1;
}

// The weld key of x y z (RldMk_StepWeld) as a key of the table.
static void RldMk_NativeWeldKey(const double xyz[3], u32 key[RLDMK_NATIVE_KEY])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		const u64 value = (u64)RldMk_Round(xyz[axis] * 100000.0);

		key[2 * axis] = (u32)value;
		key[2 * axis + 1] = (u32)(value >> 32);
	}
}

// The next float up or down (finite values only).
static float RldMk_NativeFloatStep(float f, int up)
{
	u32 bits;

	memcpy(&bits, &f, sizeof(bits));
	if ((bits & 0x7FFFFFFFu) == 0u)
	{
		bits = up ? 1u : 0x80000001u;
	}
	else if (((bits >> 31) == 0u) == (up != 0))
	{
		bits++;
	}
	else
	{
		bits--;
	}
	memcpy(&f, &bits, sizeof(f));
	return f;
}

// A coordinate as a float inside lo..hi (the reader compares the float as a double).
static float RldMk_NativeInside(double value, double lo, double hi)
{
	float f = (float)value;

	while ((double)f < lo)
	{
		f = RldMk_NativeFloatStep(f, 1);
	}
	while ((double)f > hi)
	{
		f = RldMk_NativeFloatStep(f, 0);
	}
	return f;
}

// The power of two nearest to n within 16..2048, for a hint.
static u32 RldMk_NativePowerNear(u32 n)
{
	u32 p = RLDCHAR_TEX_EDGE_MIN;

	while ((p < RLDCHAR_TEX_EDGE_MAX) && ((p * 2u) <= n))
	{
		p *= 2u;
	}
	if ((p < RLDCHAR_TEX_EDGE_MAX) && (n > p) && ((n - p) > ((p * 2u) - n)))
	{
		p *= 2u;
	}
	return p;
}

static void RldMk_NativeFreePicture(struct RldObjTexture *t, int fromImg)
{
	if (fromImg)
	{
		RldImg_Free(t->rgba);
	}
	else
	{
		free(t->rgba);
	}
	t->rgba = NULL;
}

// One texture file for CTXT: read again - the bytes the reader used, its
// digest must match -, its sides checked, its PNG bytes kept or made. 0 = said.
static int RldMk_NativeTextureFile(const struct RldObjNativeTexture *file, struct RldMkNativeTexture *out)
{
	static const u8 signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	struct RldObjTexture decoded;
	const char *why = NULL;
	char digest[65];
	u8 *data = NULL;
	size_t size = 0;
	size_t i;
	int fromImg = 0;

	memset(&decoded, 0, sizeof(decoded));
	if (RldObj_ReadFile(file->path, RLDOBJ_TEXTURE_BYTES, &data, &size) != RLDOBJ_READ_OK)
	{
		RldMk_Say("error", "native-model", file->path, "The texture %s cannot be read again for the native model.", Rld_LastComponent(file->path));
		return 0;
	}
	RldObj_Digest(data, size, digest);
	if (strcmp(digest, file->sha) != 0)
	{
		free(data);
		RldMk_Say("error", "native-model", file->path, "The texture %s changed while the character was built. Build it again.",
		          Rld_LastComponent(file->path));
		return 0;
	}
	if (RldObj_Decode(file->path, data, size, &decoded, &fromImg, &why) != RLDMK_TEXTURE_OK)
	{
		free(data);
		RldMk_Say("error", "native-model", (why != NULL) ? why : file->path, "The texture %s cannot be decoded for the native model (%s).",
		          Rld_LastComponent(file->path), (why != NULL) ? why : "unreadable");
		return 0;
	}

	out->width = decoded.width;
	out->height = decoded.height;
	if (!RldChar_TexEdge(decoded.width) || !RldChar_TexEdge(decoded.height))
	{
		RldMk_Say("error", "native-texture-size", file->path,
		          "The texture %s is %u x %u pixels; the native model takes each side as a power of two from %u to %u (16, 32, 64, 128, 256, 512, "
		          "1024, 2048). Scale it in an image editor, for example to %u x %u, and build again - rldpack never scales a texture.",
		          Rld_LastComponent(file->path), decoded.width, decoded.height, RLDCHAR_TEX_EDGE_MIN, RLDCHAR_TEX_EDGE_MAX,
		          RldMk_NativePowerNear(decoded.width), RldMk_NativePowerNear(decoded.height));
		free(data);
		RldMk_NativeFreePicture(&decoded, fromImg);
		return 0;
	}
	for (i = 0; i < (size_t)decoded.width * decoded.height; i++)
	{
		if (decoded.rgba[4u * i + 3u] != 255u)
		{
			out->alpha = 1;
			break;
		}
	}

	// A PNG of 8-bit RGBA as it is; anything else as one again.
	if ((size >= 33u) && (memcmp(data, signature, 8u) == 0) && (memcmp(&data[12], "IHDR", 4u) == 0) && (data[24] == 8u) && (data[25] == 6u))
	{
		out->png = data;
		out->pngSize = size;
		data = NULL;
	}
	else
	{
		out->png = RldMk_PngRgba(decoded.rgba, decoded.width, decoded.height, &out->pngSize);
		out->converted = 1;
	}
	free(data);
	RldMk_NativeFreePicture(&decoded, fromImg);
	if (out->png == NULL)
	{
		return RldMk_OutOfMemory();
	}
	return 1;
}

// The material of a slot: that of usemtl name `slot`, NULL for the slot of
// faces without a name.
static const struct RldObjNativeMaterial *RldMk_NativeSlot(const struct RldObjNative *src, u32 slot)
{
	return (slot < src->nameCount) ? &src->names[slot] : NULL;
}

// The UV of a corner as its material places it, in CTXT's orientation (v from the top).
static void RldMk_NativeUv(const struct RldObjNative *src, const struct RldObjNativeMaterial *m, u32 vt, double uv[2])
{
	int k;

	uv[0] = 0.0;
	uv[1] = 0.0;
	if (vt == RLDMK_NONE)
	{
		return;
	}
	for (k = 0; k < 2; k++)
	{
		double value = src->uv[2u * vt + (u32)k];

		if (m != NULL)
		{
			value = m->offset[k] + m->scale[k] * value;
			if (m->clampOn)
			{
				value = (value < 0.0) ? 0.0 : ((value > 1.0) ? 1.0 : value);
			}
		}
		uv[k] = value;
	}
	uv[1] = 1.0 - uv[1];
}

// The color of a vertex by the OBJ reader's rules, without the texture.
static void RldMk_NativeColor(const struct RldObjNative *src, const struct RldObjNativeMaterial *m, int textured, u32 v, u8 rgba[4])
{
	double c[3];
	int k;

	for (k = 0; k < 3; k++)
	{
		if (src->vColored[v])
		{
			c[k] = src->vrgb[3u * v + (u32)k];
			if (textured && src->modulate)
			{
				c[k] = c[k] * (255.0 / 128.0);
				c[k] = (c[k] > 1.0) ? 1.0 : c[k];
			}
		}
		else if ((m != NULL) && m->hasKd)
		{
			c[k] = m->kd[k];
		}
		else
		{
			c[k] = textured ? 1.0 : (128.0 / 255.0);
		}
	}
	RldObj_Store(rgba, c);
	rgba[3] = 255u;
}

// CNET and CTXT (NULL and 0 when no material has a texture) of the OBJ read
// with --native-model on, for the CMDL of build b made from ply; wheels 0:
// CHRI hides the wheels. 0 = refused, every reason said, nothing returned.
static int RldMk_NativeModel(const struct RldObjNative *src, const struct RldMkPly *ply, const struct RldMkBuild *b, const struct RldMkOptions *opt,
                             int wheels, u8 **cnetOut, size_t *cnetSizeOut, u8 **ctxtOut, size_t *ctxtSizeOut, struct RldMkNativeFacts *facts)
{
	struct RldCharHull hulls[RLDCHAR_NET_POSES];
	struct RldMkNativeTable points;
	struct RldMkNativeTable vertices;
	struct RldMkNativeTable groups;
	struct RldMkNativeTexture texture[RLDCHAR_TEX_COUNT_MAX];
	u32 *pointPosition = NULL;  // per weld key: the position
	u32 *vPosition = NULL;      // per OBJ v: the position, RLDMK_NONE = none
	struct RldMkNativeVertex *vertex = NULL;
	u32 *tri = NULL;            // 4 per triangle: three vertices and the slot
	u32 *slotMaterial = NULL;   // per slot: the CNET material, RLDMK_NONE = unused
	u32 *slotTexture = NULL;    // per slot: the CNET texture, RLDMK_NONE = none
	u32 *sourceTexture = NULL;  // per source texture: the CNET texture, RLDMK_NONE = not yet
	u32 *materialSlot = NULL;   // per CNET material: its slot
	float *place = NULL;        // slots x vertices x 3, as written
	double *sum = NULL;         // groups x 3
	u8 *cnet = NULL;
	u8 *ctxt = NULL;
	size_t cnetSize = 0;
	size_t ctxtSize = 0;
	u32 vertexCount = 0;
	u32 triCount = 0;
	u32 materialCount = 0;
	u32 textureCount = 0;
	u32 noUv = 0;
	u32 slotCount;
	u32 poses = 0;
	u32 slots = 1;
	u32 i;
	u32 f;
	u32 s;
	int axis;
	int ok = 1;
	int textured = 0;
	int colors = 0;
	float hullMin[3];
	float hullMax[3];

	*cnetOut = NULL;
	*cnetSizeOut = 0;
	*ctxtOut = NULL;
	*ctxtSizeOut = 0;
	memset(facts, 0, sizeof(*facts));
	memset(texture, 0, sizeof(texture));
	memset(&points, 0, sizeof(points));
	memset(&vertices, 0, sizeof(vertices));
	memset(&groups, 0, sizeof(groups));

	if ((src == NULL) || !src->captured)
	{
		RldMk_Say("error", "native-model", NULL,
		          "--native-model needs an OBJ model with its materials; a PLY has no texture coordinates. Export the model as OBJ.");
		return 0;
	}
	if (b->reduced || b->remeshed)
	{
		RldMk_Say("error", "native-model", b->reduced ? "reduced" : "remeshed",
		          "The model was %s for the game's own model (%u -> %u triangles); the native model keeps the faces as written and cannot follow "
		          "that yet. Use a model the game's own model takes as it is (up to about 7000 triangles), or build without --native-model.",
		          b->reduced ? "reduced" : "remeshed", b->reduced ? b->reduceFrom : b->remeshFrom, b->triangleCount);
		return 0;
	}
	if ((b->cmdl == NULL) || (b->cmdlSize < 8u) || (b->frame == NULL) || ((b->plyPosition == NULL) && (b->vertexPosition == NULL)) ||
	    (b->framesWritten != RLDMK_FRAMES_TOTAL) ||
	    !RldChar_FrameHulls(&b->cmdl[4], Rld_ReadLE32(&b->cmdl[0]), 0u, hulls))
	{
		RldMk_Say("error", "native-model", NULL, "Internal error: the frames of the built model cannot be read. Please report it.");
		return 0;
	}

	slotCount = src->nameCount + 1u;
	pointPosition = (u32 *)malloc(((size_t)ply->vertexCount + 1u) * sizeof(u32));
	vPosition = (u32 *)malloc(((size_t)src->vCount + 1u) * sizeof(u32));
	vertex = (struct RldMkNativeVertex *)malloc(((size_t)src->cornerCount + 1u) * sizeof(*vertex));
	tri = (u32 *)malloc(((size_t)src->cornerCount + 1u) * 4u * sizeof(u32));
	slotMaterial = (u32 *)malloc((size_t)slotCount * sizeof(u32));
	slotTexture = (u32 *)malloc((size_t)slotCount * sizeof(u32));
	materialSlot = (u32 *)malloc(((size_t)slotCount + 1u) * sizeof(u32));
	sourceTexture = (u32 *)malloc(((size_t)src->textureCount + 1u) * sizeof(u32));
	if ((pointPosition == NULL) || (vPosition == NULL) || (vertex == NULL) || (tri == NULL) || (slotMaterial == NULL) || (slotTexture == NULL) ||
	    (materialSlot == NULL) || (sourceTexture == NULL) || !RldMk_NativeTableInit(&points, ply->vertexCount) ||
	    !RldMk_NativeTableInit(&vertices, src->cornerCount) || !RldMk_NativeTableInit(&groups, src->cornerCount))
	{
		ok = RldMk_OutOfMemory();
	}

	// 1. The welded position of every OBJ v: that of the PLY points with its weld key.
	for (i = 0; ok && (i < ply->vertexCount); i++)
	{
		const u32 position = (b->plyPosition != NULL) ? b->plyPosition[i] : b->vertexPosition[i];
		u32 key[RLDMK_NATIVE_KEY];
		int added;
		u32 at;

		if ((position == RLDMK_NONE) || (position >= b->positionCount))
		{
			continue;
		}
		RldMk_NativeWeldKey(&ply->xyz[3u * i], key);
		at = RldMk_NativeFind(&points, key, 1, &added);
		if (added)
		{
			pointPosition[at] = position;
		}
	}
	for (i = 0; ok && (i < src->vCount); i++)
	{
		u32 key[RLDMK_NATIVE_KEY];
		int added;
		u32 at;

		RldMk_NativeWeldKey(&src->xyz[3u * i], key);
		at = RldMk_NativeFind(&points, key, 0, &added);
		vPosition[i] = (at != RLDMK_NONE) ? pointPosition[at] : RLDMK_NONE;
	}

	// 2. The slots, the textures they use and the triangles.
	for (s = 0; ok && (s < slotCount); s++)
	{
		slotMaterial[s] = RLDMK_NONE;
		slotTexture[s] = RLDMK_NONE;
	}
	for (i = 0; ok && (i < src->textureCount); i++)
	{
		sourceTexture[i] = RLDMK_NONE;
	}
	for (f = 0; ok && (f < src->faceCount); f++)
	{
		const u32 slot = (src->faceName[f] != RLDMK_NONE) ? src->faceName[f] : src->nameCount;
		const struct RldObjNativeMaterial *m = RldMk_NativeSlot(src, slot);
		const u32 first = src->faceFirst[f];
		u32 k;

		if (slotMaterial[slot] == RLDMK_NONE)
		{
			slotMaterial[slot] = materialCount;
			materialSlot[materialCount++] = slot;
			if ((m != NULL) && (m->texture != RLDMK_NONE) && (m->texture < src->textureCount) && (src->textures[m->texture].state == RLDMK_TEXTURE_OK))
			{
				if (sourceTexture[m->texture] == RLDMK_NONE)
				{
					if (textureCount >= RLDCHAR_TEX_COUNT_MAX)
					{
						RldMk_Say("error", "native-model", NULL, "The model uses more than %u texture files; the native model takes at most %u.",
						          RLDCHAR_TEX_COUNT_MAX, RLDCHAR_TEX_COUNT_MAX);
						ok = 0;
						break;
					}
					u32 same;

					// A file with the same bytes as an earlier one shares its entry.
					for (same = 0; same < src->textureCount; same++)
					{
						if ((sourceTexture[same] != RLDMK_NONE) && (strcmp(src->textures[same].sha, src->textures[m->texture].sha) == 0))
						{
							break;
						}
					}
					if (same < src->textureCount)
					{
						sourceTexture[m->texture] = sourceTexture[same];
						facts->shared++;
					}
					else
					{
						sourceTexture[m->texture] = textureCount;
						ok = RldMk_NativeTextureFile(&src->textures[m->texture], &texture[textureCount]);
						textureCount++;
						if (!ok)
						{
							break;
						}
					}
				}
				slotTexture[slot] = sourceTexture[m->texture];
				textured = 1;
			}
			else if ((m != NULL) && m->hasMap)
			{
				facts->untextured++;
				RldMk_Say("warning", "native-texture", m->name,
				          "The texture of the material \"%s\" cannot be used; the native model draws its faces in their color, as the game's own model does.",
				          m->name);
			}
		}

		for (k = 1; ok && ((k + 1u) < src->faceCorners[f]); k++)
		{
			const u32 corner[3] = {first, first + k, first + k + 1u};
			u32 index[3];
			u32 p[3];
			int c;
			int skip = 0;

			for (c = 0; c < 3; c++)
			{
				p[c] = vPosition[src->cornerV[corner[c]]];
				skip |= (p[c] == RLDMK_NONE);
			}
			if (!skip)
			{
				const double *a = &b->position[3u * p[0]];
				const double *bb = &b->position[3u * p[1]];
				const double *cc = &b->position[3u * p[2]];
				const double e1[3] = {bb[0] - a[0], bb[1] - a[1], bb[2] - a[2]};
				const double e2[3] = {cc[0] - a[0], cc[1] - a[1], cc[2] - a[2]};
				const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};

				skip = (p[0] == p[1]) || (p[1] == p[2]) || (p[0] == p[2]) || (((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2])) == 0.0);
			}
			if (skip)
			{
				facts->dropped++;
				continue;
			}

			for (c = 0; c < 3; c++)
			{
				const u32 v = src->cornerV[corner[c]];
				const u32 vt = src->cornerT[corner[c]];
				const u32 vn = src->cornerN[corner[c]];
				const u32 vertexKey[RLDMK_NATIVE_KEY] = {v, vt, vn, slot, 0u, 0u};
				const u32 groupKey[RLDMK_NATIVE_KEY] = {v, vn, 0u, 0u, 0u, 0u};
				int added;
				int groupAdded;
				const u32 at = RldMk_NativeFind(&vertices, vertexKey, 1, &added);
				const u32 group = RldMk_NativeFind(&groups, groupKey, 1, &groupAdded);

				(void)groupAdded;
				if (added)
				{
					vertex[at].v = v;
					vertex[at].vt = vt;
					vertex[at].slot = slot;
					vertex[at].position = p[c];
					vertex[at].group = group;
					vertex[at].vn = vn;
					colors |= src->vColored[v];
				}
				index[c] = at;
				if (slotTexture[slot] != RLDMK_NONE)
				{
					double uv[2];

					if (vt == RLDMK_NONE)
					{
						noUv++;
					}
					RldMk_NativeUv(src, m, vt, uv);
					texture[slotTexture[slot]].outside |= !((uv[0] >= 0.0) && (uv[0] <= 1.0) && (uv[1] >= 0.0) && (uv[1] <= 1.0));
				}
			}
			if ((index[0] == index[1]) || (index[1] == index[2]) || (index[0] == index[2]))
			{
				facts->dropped++;
				continue;
			}
			tri[4u * triCount + 0u] = index[0];
			tri[4u * triCount + 1u] = index[1];
			tri[4u * triCount + 2u] = index[2];
			tri[4u * triCount + 3u] = slot;
			triCount++;
		}
	}
	// 2b. The repair of the chain on these triangles, and second triangles
	//     for what stays open (THE REPAIR above).
	if (ok && (opt != NULL) && (triCount > 0u) && (opt->repair || opt->twoSided))
	{
		RldRepInput in;
		RldRepResult r;
		char why[160];
		u32 *corner = (u32 *)malloc((size_t)triCount * 3u * sizeof(u32) + 4u);
		u32 *repaired = NULL;
		u32 count = triCount;
		u32 backs = 0;
		u32 i;

		memset(&r, 0, sizeof(r));
		ok = (corner != NULL) || RldMk_OutOfMemory();
		for (i = 0; ok && (i < (3u * triCount)); i++)
		{
			corner[i] = vertex[tri[4u * (i / 3u) + (i % 3u)]].position;
		}
		if (ok && opt->repair)
		{
			memset(&in, 0, sizeof(in));
			in.positionCount = b->positionCount;
			in.position = b->position;
			in.triangleCount = triCount;
			in.triangle = corner;
			in.cornerColor = NULL;
			in.weldDistance = RldRep_DefaultWeld(b->positionCount, b->position);
			in.holeMaxEdges = RLDREP_HOLE_MAX_EDGES_DEFAULT;
			why[0] = '\0';
			if (!RldRep_Repair(&in, &r, why, sizeof(why)))
			{
				RldMk_Say("error", "native-model", why, "The native model cannot be repaired (%s). Build it with --repair off.", why);
				ok = 0;
			}
			else
			{
				count = r.triangleCount;
				facts->repaired = 1;
				facts->flipped = r.flipped;
				facts->lids = r.holeTriangles;
				facts->openEdges = r.openEdgesAfter;
				facts->dropped += r.degenerate + r.duplicate;
			}
		}
		for (i = 0; ok && (i < count); i++)
		{
			backs += (opt->twoSided || (opt->repair && !opt->openOneSided && r.open[i])) ? 1u : 0u;
		}
		if (ok)
		{
			repaired = (u32 *)malloc(((size_t)count + backs + 1u) * 4u * sizeof(u32));
			ok = (repaired != NULL) && RldMk_NativeTableGrow(&vertices, vertices.count + 6u * (count + backs) + 3u) &&
			     RldMk_NativeTableGrow(&groups, groups.count + 6u * (count + backs) + 3u);
			if (ok)
			{
				struct RldMkNativeVertex *more = (struct RldMkNativeVertex *)realloc(vertex, ((size_t)vertices.capacity + 1u) * sizeof(*vertex));

				ok = (more != NULL);
				vertex = (more != NULL) ? more : vertex;
			}
			if (!ok)
			{
				ok = RldMk_OutOfMemory();
			}
		}
		triCount = 0;
		for (i = 0; ok && (i < count); i++)
		{
			const u32 first = opt->repair ? r.sourceCorner[3u * i] : (3u * i);
			const u32 slot = tri[4u * (first / 3u) + 3u];
			u32 index[3];
			u32 sv0;
			int c;

			for (c = 0; c < 3; c++)
			{
				const u32 sc = opt->repair ? r.sourceCorner[3u * i + (u32)c] : (3u * i + (u32)c);
				const u32 sv = tri[4u * (sc / 3u) + (sc % 3u)];
				const u32 position = opt->repair ? r.triangle[3u * i + (u32)c] : vertex[sv].position;

				index[c] = sv;
				if (position != vertex[sv].position)
				{
					// A corner moved by the repair (a weld, a crack): the vertex's attributes at the new position.
					const u32 key[RLDMK_NATIVE_KEY] = {position, sv, 0u, 0u, 1u, 0u};
					const u32 groupKey[RLDMK_NATIVE_KEY] = {position, vertex[sv].vn, 0u, 0u, 1u, 0u};
					int added;
					int groupAdded;

					index[c] = RldMk_NativeFind(&vertices, key, 1, &added);
					if (added)
					{
						vertex[index[c]] = vertex[sv];
						vertex[index[c]].position = position;
						vertex[index[c]].group = RldMk_NativeFind(&groups, groupKey, 1, &groupAdded);
					}
				}
				// A lid takes UV and material of its corner 0 at every corner: one
				// spot of one texture, no texture smeared across the hole (the
				// CMDL colors its lids with the corners' colors, untextured).
				if ((c > 0) && opt->repair && r.added[i] &&
				    ((vertex[index[c]].vt != vertex[index[0]].vt) || (vertex[index[c]].slot != vertex[index[0]].slot)))
				{
					const u32 key[RLDMK_NATIVE_KEY] = {index[c], index[0], 0u, 0u, 3u, 0u};
					int added;

					sv0 = index[c];
					index[c] = RldMk_NativeFind(&vertices, key, 1, &added);
					if (added)
					{
						vertex[index[c]] = vertex[sv0];
						vertex[index[c]].vt = vertex[index[0]].vt;
						vertex[index[c]].slot = vertex[index[0]].slot;
					}
				}
			}
			repaired[4u * triCount + 0u] = index[0];
			repaired[4u * triCount + 1u] = index[1];
			repaired[4u * triCount + 2u] = index[2];
			repaired[4u * triCount + 3u] = slot;
			triCount++;
			if (opt->twoSided || (opt->repair && !opt->openOneSided && r.open[i]))
			{
				// The other side: the same corners as vertices of their own, wound the other way.
				u32 back[3];

				for (c = 0; c < 3; c++)
				{
					const u32 key[RLDMK_NATIVE_KEY] = {index[c], 0u, 0u, 0u, 2u, 0u};
					const u32 groupKey[RLDMK_NATIVE_KEY] = {vertex[index[c]].group, 0u, 0u, 0u, 2u, 0u};
					int added;
					int groupAdded;

					back[c] = RldMk_NativeFind(&vertices, key, 1, &added);
					if (added)
					{
						vertex[back[c]] = vertex[index[c]];
						vertex[back[c]].group = RldMk_NativeFind(&groups, groupKey, 1, &groupAdded);
					}
				}
				repaired[4u * triCount + 0u] = back[0];
				repaired[4u * triCount + 1u] = back[2];
				repaired[4u * triCount + 2u] = back[1];
				repaired[4u * triCount + 3u] = slot;
				triCount++;
				facts->backFaces++;
			}
		}
		if (ok)
		{
			free(tri);
			tri = repaired;
			repaired = NULL;
		}
		free(repaired);
		free(corner);
		RldRep_Free(&r);
	}
	vertexCount = vertices.count;

	// 3. The limits.
	if (ok && (noUv != 0u))
	{
		RldMk_Say("error", "native-model", NULL,
		          "%u corners of textured faces have no texture coordinates (vt); the native model needs them. Unwrap the model (UV) in Blender.", noUv);
		ok = 0;
	}
	if (ok && ((triCount == 0u) || (vertexCount < 3u)))
	{
		RldMk_Say("error", "native-model", NULL, "No triangle of the model is left for the native model.");
		ok = 0;
	}
	if (ok && ((vertexCount > RLDCHAR_NET_VERTICES_MAX) || (triCount > RLDCHAR_NET_TRIANGLES_MAX)))
	{
		RldMk_Say("error", "native-model", NULL,
		          "The native model has %u vertices and %u triangles; it may have %u and %u (one vertex per corner with its own v, vt, vn and "
		          "material). Reduce the model in Blender (Decimate modifier).",
		          vertexCount, triCount, RLDCHAR_NET_VERTICES_MAX, RLDCHAR_NET_TRIANGLES_MAX);
		ok = 0;
	}
	if (ok && ((materialCount + (wheels ? 1u : 0u)) > RLDCHAR_NET_MATERIALS_MAX))
	{
		RldMk_Say("error", "native-model", NULL, "The native model has %u materials%s; it may have %u. Join materials in Blender.",
		          materialCount + (wheels ? 1u : 0u), wheels ? " with the wheels'" : "", RLDCHAR_NET_MATERIALS_MAX);
		ok = 0;
	}
	if (ok)
	{
		u64 budget = 0;

		for (i = 0; i < textureCount; i++)
		{
			budget += RldChar_TexMipBytes(texture[i].width, texture[i].height);
		}
		if (budget > RLDCHAR_TEX_GPU_BYTES_MAX)
		{
			RldMk_Say("error", "native-model", NULL,
			          "The textures need %llu bytes with their mip levels; the native model may use %u (64 MiB). Make textures smaller.",
			          (unsigned long long)budget, (unsigned)RLDCHAR_TEX_GPU_BYTES_MAX);
			ok = 0;
		}
	}

	// 4. Still or 47 poses: every frame of the build the neutral pose at every vertex?
	if (ok)
	{
		int moves = 0;

		for (i = 0; !moves && (i < vertexCount); i++)
		{
			const u32 p = vertex[i].position;

			for (f = 1; !moves && (f < RLDMK_FRAMES_TOTAL); f++)
			{
				moves = (memcmp(&b->frame[((size_t)f * b->positionCount + p) * 3u], &b->frame[(size_t)p * 3u], 3u * sizeof(double)) != 0);
			}
		}
		poses = moves ? RLDCHAR_NET_POSES : 0u;
		slots = moves ? RLDCHAR_NET_POSES : 1u;
		place = (float *)malloc((size_t)slots * vertexCount * 3u * sizeof(float) + 4u);
		sum = (double *)malloc(((size_t)groups.count + 1u) * 3u * sizeof(double));
		if ((place == NULL) || (sum == NULL))
		{
			ok = RldMk_OutOfMemory();
		}
	}

	// 5. The positions of every pose, inside the hulls of their frames.
	for (axis = 0; axis < 3; axis++)
	{
		hullMin[axis] = 1.0e30f;
		hullMax[axis] = -1.0e30f;
	}
	for (s = 0; ok && (s < slots); s++)
	{
		double lo[3];
		double hi[3];
		double step[3];

		for (axis = 0; axis < 3; axis++)
		{
			const u32 from = (slots == 1u) ? 0u : s;
			const u32 to = (slots == 1u) ? RLDCHAR_NET_POSES : (s + 1u);

			lo[axis] = -1.0e300;
			hi[axis] = 1.0e300;
			step[axis] = 1.0e300;
			for (f = from; f < to; f++)
			{
				const double width = (hulls[f].hi[axis] - hulls[f].lo[axis]) / 255.0;

				lo[axis] = (hulls[f].lo[axis] > lo[axis]) ? hulls[f].lo[axis] : lo[axis];
				hi[axis] = (hulls[f].hi[axis] < hi[axis]) ? hulls[f].hi[axis] : hi[axis];
				step[axis] = (width < step[axis]) ? width : step[axis];
			}
		}
		for (i = 0; ok && (i < vertexCount); i++)
		{
			const double *at = &b->frame[((size_t)s * b->positionCount + vertex[i].position) * 3u];

			for (axis = 0; axis < 3; axis++)
			{
				double value = at[axis];

				if ((value < lo[axis]) || (value > hi[axis]))
				{
					const double off = (value < lo[axis]) ? (lo[axis] - value) : (value - hi[axis]);

					if (off > (2.0 * step[axis]))
					{
						RldMk_Say("error", "native-model", NULL,
						          "Internal error: vertex %u of the native model lies %.3f units outside frame %u of the game's own model (axis %d). "
						          "Please report it.",
						          i, off, s, axis);
						ok = 0;
						break;
					}
					value = (value < lo[axis]) ? lo[axis] : hi[axis];
					facts->clamped++;
				}
				place[((size_t)s * vertexCount + i) * 3u + (u32)axis] = RldMk_NativeInside(value, lo[axis], hi[axis]);
				hullMin[axis] = (place[((size_t)s * vertexCount + i) * 3u + (u32)axis] < hullMin[axis]) ? place[((size_t)s * vertexCount + i) * 3u + (u32)axis]
				                                                                                       : hullMin[axis];
				hullMax[axis] = (place[((size_t)s * vertexCount + i) * 3u + (u32)axis] > hullMax[axis]) ? place[((size_t)s * vertexCount + i) * 3u + (u32)axis]
				                                                                                       : hullMax[axis];
			}
		}
	}

	// 6. CNET.
	if (ok)
	{
		const u32 materials = materialCount + (wheels ? 1u : 0u);
		const u32 sectionCount = 4u + (textured ? 1u : 0u) + (colors ? 1u : 0u) + (wheels ? 1u : 0u);
		const u32 tableAt = RLDCHAR_NET_HEAD_BYTES;
		const u32 posnAt = tableAt + (sectionCount * RLDCHAR_NET_SECTION_BYTES);
		const u32 posnSize = slots * vertexCount * RLDCHAR_NET_VERTEX_BYTES;
		const u32 uvAt = posnAt + posnSize;
		const u32 uvSize = textured ? (vertexCount * 8u) : 0u;
		const u32 colAt = uvAt + uvSize;
		const u32 colSize = colors ? (vertexCount * 4u) : 0u;
		const u32 trisAt = colAt + colSize;
		const u32 trisSize = triCount * 6u;
		const u32 tmatAt = (trisAt + trisSize + 3u) & ~3u;
		const u32 tmatSize = triCount * 2u;
		const u32 matlAt = (tmatAt + tmatSize + 3u) & ~3u;
		const u32 matlSize = materials * RLDCHAR_NET_MATERIAL_BYTES;
		const u32 whlsAt = matlAt + matlSize;
		const u64 size = (u64)whlsAt + (wheels ? RLDMK_NATIVE_WHEEL_BYTES : 0u);
		u32 table = tableAt;

		if (size > RLDCHAR_LIMIT_CNET)
		{
			RldMk_Say("error", "native-model", NULL, "The native model needs %llu bytes; CNET may have %u (32 MiB). Reduce the model in Blender.",
			          (unsigned long long)size, (unsigned)RLDCHAR_LIMIT_CNET);
			ok = 0;
		}
		cnet = ok ? (u8 *)calloc(1, (size_t)size) : NULL;
		if (ok && (cnet == NULL))
		{
			ok = RldMk_OutOfMemory();
		}
		if (ok)
		{
			cnetSize = (size_t)size;
			Rld_WriteLE16(&cnet[0x00], RLDCHAR_NET_VERSION);
			Rld_WriteLE16(&cnet[0x04], RLDCHAR_NET_HEAD_BYTES);
			Rld_WriteLE16(&cnet[0x06], sectionCount);
			Rld_WriteLE32(&cnet[0x08], vertexCount);
			Rld_WriteLE32(&cnet[0x0C], triCount);
			Rld_WriteLE16(&cnet[0x10], 2u);
			Rld_WriteLE16(&cnet[0x12], materials);
			Rld_WriteLE16(&cnet[0x14], poses);
			Rld_WriteLE16(&cnet[0x16], RLDCHAR_NET_POSE_FLOAT);
			for (axis = 0; axis < 3; axis++)
			{
				RldMk_PutF32(&cnet[0x18 + (4 * axis)], hullMin[axis]);
				RldMk_PutF32(&cnet[0x24 + (4 * axis)], hullMax[axis]);
			}
			Rld_WriteLE16(&cnet[0x30], textureCount);

			RldMk_NativeSection(&cnet[table], "POSN", RLDCHAR_NET_VERTEX_BYTES, posnAt, posnSize);
			table += RLDCHAR_NET_SECTION_BYTES;
			if (textured)
			{
				RldMk_NativeSection(&cnet[table], "UV00", 8u, uvAt, uvSize);
				table += RLDCHAR_NET_SECTION_BYTES;
			}
			if (colors)
			{
				RldMk_NativeSection(&cnet[table], "COL0", 4u, colAt, colSize);
				table += RLDCHAR_NET_SECTION_BYTES;
			}
			RldMk_NativeSection(&cnet[table], "TRIS", 6u, trisAt, trisSize);
			RldMk_NativeSection(&cnet[table + RLDCHAR_NET_SECTION_BYTES], "TMAT", 2u, tmatAt, tmatSize);
			RldMk_NativeSection(&cnet[table + (2u * RLDCHAR_NET_SECTION_BYTES)], "MATL", RLDCHAR_NET_MATERIAL_BYTES, matlAt, matlSize);
			if (wheels)
			{
				RldMk_NativeSection(&cnet[table + (3u * RLDCHAR_NET_SECTION_BYTES)], "WHLS", 0u, whlsAt, RLDMK_NATIVE_WHEEL_BYTES);
				RldMk_NativeWheel(&cnet[whlsAt], materialCount);
			}

			// POSN: the places, the normals of their smooth groups.
			for (s = 0; s < slots; s++)
			{
				const float *at = &place[(size_t)s * vertexCount * 3u];

				memset(sum, 0, ((size_t)groups.count + 1u) * 3u * sizeof(double));
				for (i = 0; i < triCount; i++)
				{
					const float *pa = &at[tri[4u * i + 0u] * 3u];
					const float *pb = &at[tri[4u * i + 1u] * 3u];
					const float *pc = &at[tri[4u * i + 2u] * 3u];
					const double e1[3] = {(double)pb[0] - pa[0], (double)pb[1] - pa[1], (double)pb[2] - pa[2]};
					const double e2[3] = {(double)pc[0] - pa[0], (double)pc[1] - pa[1], (double)pc[2] - pa[2]};
					const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
					int c;

					for (c = 0; c < 3; c++)
					{
						double *g = &sum[(size_t)vertex[tri[4u * i + (u32)c]].group * 3u];

						g[0] += n[0];
						g[1] += n[1];
						g[2] += n[2];
					}
				}
				for (i = 0; i < vertexCount; i++)
				{
					u8 *out = &cnet[posnAt + (((s * vertexCount) + i) * RLDCHAR_NET_VERTEX_BYTES)];
					const double *g = &sum[(size_t)vertex[i].group * 3u];
					const double length = RldMk_Sqrt((g[0] * g[0]) + (g[1] * g[1]) + (g[2] * g[2]));

					for (axis = 0; axis < 3; axis++)
					{
						RldMk_PutF32(&out[4 * axis], at[i * 3u + (u32)axis]);
					}
					if (length > 1.0e-12)
					{
						RldMk_PutF32(&out[12], g[0] / length);
						RldMk_PutF32(&out[16], g[1] / length);
						RldMk_PutF32(&out[20], g[2] / length);
					}
					else
					{
						RldMk_PutF32(&out[16], 1.0);
						facts->flat += (s == 0u) ? 1u : 0u;
					}
				}
			}

			// UV00, COL0.
			for (i = 0; i < vertexCount; i++)
			{
				const struct RldObjNativeMaterial *m = RldMk_NativeSlot(src, vertex[i].slot);

				if (textured)
				{
					double uv[2];

					RldMk_NativeUv(src, m, vertex[i].vt, uv);
					RldMk_PutF32(&cnet[uvAt + (8u * i)], uv[0]);
					RldMk_PutF32(&cnet[uvAt + (8u * i) + 4u], uv[1]);
				}
				if (colors)
				{
					RldMk_NativeColor(src, m, slotTexture[vertex[i].slot] != RLDMK_NONE, vertex[i].v, &cnet[colAt + (4u * i)]);
				}
			}

			// TRIS, TMAT.
			for (i = 0; i < triCount; i++)
			{
				Rld_WriteLE16(&cnet[trisAt + (6u * i) + 0u], tri[4u * i + 0u]);
				Rld_WriteLE16(&cnet[trisAt + (6u * i) + 2u], tri[4u * i + 1u]);
				Rld_WriteLE16(&cnet[trisAt + (6u * i) + 4u], tri[4u * i + 2u]);
				Rld_WriteLE16(&cnet[tmatAt + (2u * i)], slotMaterial[tri[4u * i + 3u]]);
			}

			// MATL; the wheels' last.
			for (i = 0; i < materialCount; i++)
			{
				const u32 slot = materialSlot[i];
				const struct RldObjNativeMaterial *m = RldMk_NativeSlot(src, slot);
				const u32 tex = slotTexture[slot];
				u8 *out = &cnet[matlAt + (i * RLDCHAR_NET_MATERIAL_BYTES)];

				if (colors)
				{
					memset(out, 255, 4u);
				}
				else
				{
					double c[3];
					int k;

					for (k = 0; k < 3; k++)
					{
						c[k] = ((m != NULL) && m->hasKd) ? m->kd[k] : ((tex != RLDMK_NONE) ? 1.0 : (128.0 / 255.0));
					}
					RldObj_Store(out, c);
					out[3] = 255u;
				}
				Rld_WriteLE16(&out[4], (tex != RLDMK_NONE) ? tex : 0xFFFFu);
				out[6] = (u8)(((tex != RLDMK_NONE) && texture[tex].alpha) ? 1u : 0u);
			}
			if (wheels)
			{
				u8 *out = &cnet[matlAt + (materialCount * RLDCHAR_NET_MATERIAL_BYTES)];

				out[0] = 64;
				out[1] = 64;
				out[2] = 64;
				out[3] = 255;
				Rld_WriteLE16(&out[4], 0xFFFFu);
			}
		}
	}

	// 7. CTXT.
	if (ok && (textureCount != 0u))
	{
		const u32 tableEnd = RLDCHAR_TEX_HEAD_BYTES + (textureCount * RLDCHAR_TEX_ENTRY_BYTES);
		u64 size = tableEnd;
		u32 at;

		for (i = 0; i < textureCount; i++)
		{
			size = ((size + 3u) & ~(u64)3u) + texture[i].pngSize;
		}
		if (size > RLDCHAR_LIMIT_CTXT)
		{
			RldMk_Say("error", "native-model", NULL,
			          "The textures need %llu bytes as PNG; CTXT may have %u (32 MiB). A PNG of 8-bit RGBA is taken as it is (compressed); every other "
			          "picture is written again uncompressed - save the textures as PNG with alpha (RGBA, 8 bits), or make them smaller.",
			          (unsigned long long)size, (unsigned)RLDCHAR_LIMIT_CTXT);
			ok = 0;
		}
		ctxt = ok ? (u8 *)calloc(1, (size_t)size) : NULL;
		if (ok && (ctxt == NULL))
		{
			ok = RldMk_OutOfMemory();
		}
		if (ok)
		{
			ctxtSize = (size_t)size;
			Rld_WriteLE16(&ctxt[0x00], RLDCHAR_TEX_VERSION);
			Rld_WriteLE16(&ctxt[0x04], textureCount);
			Rld_WriteLE16(&ctxt[0x06], RLDCHAR_TEX_ENTRY_BYTES);
			at = tableEnd;
			for (i = 0; i < textureCount; i++)
			{
				u8 *e = &ctxt[RLDCHAR_TEX_HEAD_BYTES + (i * RLDCHAR_TEX_ENTRY_BYTES)];
				const u32 wrap = texture[i].outside ? 0u : 1u;
				const u32 flags = (wrap << RLDCHAR_TEX_WRAP_U_SHIFT) | (wrap << RLDCHAR_TEX_WRAP_V_SHIFT) | ((texture[i].alpha ? 1u : 0u) << RLDCHAR_TEX_ALPHA_SHIFT);

				at = (at + 3u) & ~3u;
				e[0x00] = RLDCHAR_TEX_FORMAT_PNG;
				Rld_WriteLE16(&e[0x02], flags);
				Rld_WriteLE16(&e[0x04], texture[i].width);
				Rld_WriteLE16(&e[0x06], texture[i].height);
				Rld_WriteLE32(&e[0x08], at);
				Rld_WriteLE32(&e[0x0C], (u32)texture[i].pngSize);
				Sha256(texture[i].png, texture[i].pngSize, &e[0x10]);
				memcpy(&ctxt[at], texture[i].png, texture[i].pngSize);
				at += (u32)texture[i].pngSize;
			}
		}
	}

	if (ok)
	{
		facts->vertices = vertexCount;
		facts->triangles = triCount;
		facts->poses = poses;
		facts->materials = materialCount + (wheels ? 1u : 0u);
		facts->textures = textureCount;
		facts->colors = colors;
		facts->wheels = wheels;
		facts->cnetSize = cnetSize;
		facts->ctxtSize = ctxtSize;
		for (i = 0; i < textureCount; i++)
		{
			facts->width[i] = texture[i].width;
			facts->height[i] = texture[i].height;
			facts->converted[i] = texture[i].converted;
		}
		*cnetOut = cnet;
		*cnetSizeOut = cnetSize;
		*ctxtOut = ctxt;
		*ctxtSizeOut = ctxtSize;
	}
	else
	{
		free(cnet);
		free(ctxt);
	}

	for (i = 0; i < RLDCHAR_TEX_COUNT_MAX; i++)
	{
		free(texture[i].png);
	}
	RldMk_NativeTableFree(&points);
	RldMk_NativeTableFree(&vertices);
	RldMk_NativeTableFree(&groups);
	free(pointPosition);
	free(vPosition);
	free(vertex);
	free(tri);
	free(slotMaterial);
	free(slotTexture);
	free(sourceTexture);
	free(materialSlot);
	free(place);
	free(sum);
	return ok;
}

//========================================================================================
// THE NATIVE MODEL IN THE PREVIEW (make-char --preview with --native-model on)
//========================================================================================
//
// For the 3D view of Reload Studio (tools/reloadstudio/rs_view.c, preview
// feature): when make-char made a native model and writes --preview, the
// native model follows the RLDPV2 data in the same file, already checked by
// the game's rules (RldMk_CheckNativeBytes) and decoded - the view decodes
// nothing and checks nothing. Without a native model the file is RLDPV2 as
// before, byte for byte. Little endian, no padding:
//   "RLDPN1\0\0"
//   u32 textures, per texture: u32 width, u32 height, u32 flags (CTXT,
//       RLDCHAR_TEX_*), width x height x 4 bytes RGBA, rows from the top;
//       a side above RLDMK_NATIVE_PREVIEW_EDGE is halved (2 x 2 box, rounded)
//       until it fits - a preview, not the game's texture
//   u32 triangles T, u32 poses 3: per pose T x 3 corners of s16 x, y, z in
//       1/16 game units (rounded half away from zero, as RLDPV2) - the turn
//       frames 10, 0 and 20 of RLDPV2; a still model three times its pose
//   per triangle: 3 corners of {s32 u, s32 v (Q16, v from the top as in CTXT,
//       clamped to +-64), u8 r, g, b (COL0 or white, times the material's
//       color), u8 0}, then s16 texture (-1 none), u8 alpha mode, u8 0
// The corners run as CNET has them: counter-clockwise seen from outside.

#define RLDMK_NATIVE_PREVIEW_EDGE 512u
#define RLDMK_NATIVE_PREVIEW_UV_MAX (64 * 65536)

static const u32 s_rldMkNativePreviewFrames[3] = {10u, 0u, 20u};

static int RldMk_NativePreviewSixteenth(double value)
{
	const double scaled = value * 16.0;
	long whole = (scaled >= 0.0) ? (long)(scaled + 0.5) : -(long)(-scaled + 0.5);

	return (whole > 32767L) ? 32767 : ((whole < -32768L) ? -32768 : (int)whole);
}

static s32 RldMk_NativePreviewQ16(double value)
{
	const double scaled = value * 65536.0;
	double rounded = (scaled >= 0.0) ? (double)(s64)(scaled + 0.5) : -(double)(s64)(-scaled + 0.5);

	rounded = (rounded > (double)RLDMK_NATIVE_PREVIEW_UV_MAX) ? (double)RLDMK_NATIVE_PREVIEW_UV_MAX : rounded;
	rounded = (rounded < -(double)RLDMK_NATIVE_PREVIEW_UV_MAX) ? -(double)RLDMK_NATIVE_PREVIEW_UV_MAX : rounded;
	return (s32)rounded;
}

// The texture for the preview: halved while a side is above the edge.
// Returns a malloc'd copy (or NULL) and its size.
static u8 *RldMk_NativePreviewTexture(const struct RldCharTexture *t, u32 *widthOut, u32 *heightOut)
{
	u32 w = t->width;
	u32 h = t->height;
	u8 *rgba = (u8 *)malloc((size_t)w * h * 4u + 4u);

	if (rgba == NULL)
	{
		return NULL;
	}
	memcpy(rgba, t->rgba, (size_t)w * h * 4u);
	while ((w > RLDMK_NATIVE_PREVIEW_EDGE) || (h > RLDMK_NATIVE_PREVIEW_EDGE))
	{
		const u32 nw = (w > RLDMK_NATIVE_PREVIEW_EDGE) ? (w / 2u) : w;
		const u32 nh = (h > RLDMK_NATIVE_PREVIEW_EDGE) ? (h / 2u) : h;
		const u32 sx = w / nw;
		const u32 sy = h / nh;
		u32 x;
		u32 y;

		for (y = 0; y < nh; y++)
		{
			for (x = 0; x < nw; x++)
			{
				u32 c;

				for (c = 0; c < 4u; c++)
				{
					u32 sum = 0;
					u32 dx;
					u32 dy;

					for (dy = 0; dy < sy; dy++)
					{
						for (dx = 0; dx < sx; dx++)
						{
							sum += rgba[(((size_t)(y * sy + dy) * w) + (x * sx + dx)) * 4u + c];
						}
					}
					rgba[(((size_t)y * nw) + x) * 4u + c] = (u8)((sum + ((sx * sy) / 2u)) / (sx * sy));
				}
			}
		}
		w = nw;
		h = nh;
	}
	*widthOut = w;
	*heightOut = h;
	return rgba;
}

// The block above for a native part that passed (READY), in memory
// (malloc'd, *sizeOut bytes). NULL = not READY or out of memory.
static u8 *RldMk_NativePreviewBlock(const struct RldCharNative *n, size_t *sizeOut)
{
	static const u8 magic[8] = {'R', 'L', 'D', 'P', 'N', '1', 0, 0};
	u8 *texture[RLDCHAR_TEX_COUNT_MAX];
	u32 width[RLDCHAR_TEX_COUNT_MAX];
	u32 height[RLDCHAR_TEX_COUNT_MAX];
	size_t size = 8u + 4u + 8u + ((size_t)n->triangleCount * (3u * 3u * 6u + 3u * 12u + 4u));
	u8 *out = NULL;
	size_t at = 0;
	u32 t;
	u32 i;
	u32 p;
	int ok = (n->state == RLDCHAR_NATIVE_READY);

	*sizeOut = 0;

	memset(texture, 0, sizeof(texture));
	for (t = 0; ok && (t < n->textureCount); t++)
	{
		texture[t] = RldMk_NativePreviewTexture(&n->texture[t], &width[t], &height[t]);
		ok = (texture[t] != NULL);
		size += 12u + ((size_t)width[t] * height[t] * 4u);
	}
	out = ok ? (u8 *)malloc(size) : NULL;
	ok = (out != NULL);
	if (ok)
	{
		memcpy(out, magic, 8u);
		at = 8u;
		Rld_WriteLE32(&out[at], n->textureCount);
		at += 4u;
		for (t = 0; t < n->textureCount; t++)
		{
			Rld_WriteLE32(&out[at], width[t]);
			Rld_WriteLE32(&out[at + 4u], height[t]);
			Rld_WriteLE32(&out[at + 8u], n->texture[t].flags);
			memcpy(&out[at + 12u], texture[t], (size_t)width[t] * height[t] * 4u);
			at += 12u + ((size_t)width[t] * height[t] * 4u);
		}
		Rld_WriteLE32(&out[at], n->triangleCount);
		Rld_WriteLE32(&out[at + 4u], 3u);
		at += 8u;
		for (p = 0; p < 3u; p++)
		{
			const u32 slot = (n->poseCount == 0u) ? 0u : s_rldMkNativePreviewFrames[p];
			const u8 *pose = &n->poses[(size_t)slot * n->vertexCount * RLDCHAR_NET_VERTEX_BYTES];

			for (i = 0; i < (3u * n->triangleCount); i++)
			{
				const u8 *ix = &n->indices[(size_t)i * n->indexSize];
				const u32 vertex = (n->indexSize == 2u) ? Rld_ReadLE16(ix) : Rld_ReadLE32(ix);
				int axis;

				for (axis = 0; axis < 3; axis++)
				{
					Rld_WriteLE16(&out[at], (u32)RldMk_NativePreviewSixteenth(RldChar_F32(&pose[(vertex * RLDCHAR_NET_VERTEX_BYTES) + (4u * (u32)axis)])) &
					                            0xFFFFu);
					at += 2u;
				}
			}
		}
		for (i = 0; i < n->triangleCount; i++)
		{
			const u8 *m = &n->materials[(size_t)Rld_ReadLE16(&n->triangleMaterials[2u * i]) * RLDCHAR_NET_MATERIAL_BYTES];
			u32 c;

			for (c = 0; c < 3u; c++)
			{
				const u8 *ix = &n->indices[((size_t)(3u * i) + c) * n->indexSize];
				const u32 vertex = (n->indexSize == 2u) ? Rld_ReadLE16(ix) : Rld_ReadLE32(ix);
				int k;

				Rld_WriteLE32(&out[at], (u32)RldMk_NativePreviewQ16((n->uv != NULL) ? (double)RldChar_F32(&n->uv[8u * vertex]) : 0.0));
				Rld_WriteLE32(&out[at + 4u], (u32)RldMk_NativePreviewQ16((n->uv != NULL) ? (double)RldChar_F32(&n->uv[8u * vertex + 4u]) : 0.0));
				for (k = 0; k < 3; k++)
				{
					const u32 base = (n->colors != NULL) ? n->colors[4u * vertex + (u32)k] : 255u;

					out[at + 8u + (u32)k] = (u8)(((base * m[k]) + 127u) / 255u);
				}
				out[at + 11u] = 0u;
				at += 12u;
			}
			Rld_WriteLE16(&out[at], Rld_ReadLE16(&m[4]));
			out[at + 2u] = m[6];
			out[at + 3u] = 0u;
			at += 4u;
		}
		ok = (at == size);
	}

	for (t = 0; t < RLDCHAR_TEX_COUNT_MAX; t++)
	{
		free(texture[t]);
	}
	if (!ok)
	{
		free(out);
		return NULL;
	}
	*sizeOut = size;
	return out;
}

// RldMk_NativePreviewBlock appended to the preview file at path. 1 = written.
static int RldMk_NativePreviewAppend(const char *path, const struct RldCharNative *n)
{
	size_t size = 0;
	u8 *block = RldMk_NativePreviewBlock(n, &size);
	FILE *file = (block != NULL) ? fopen(path, "ab") : NULL;
	int ok = (file != NULL) && (fwrite(block, 1, size, file) == size);

	if (file != NULL)
	{
		ok = (fclose(file) == 0) && ok;
	}
	free(block);
	return ok;
}

// The native part in memory, through the game's own rules (include/rldchar.inc):
// what make-char writes must pass them, the way the game reads it.
static void RldMk_CheckNativeBytes(const u8 *cmdl, size_t cmdlSize, u32 chriFlags, const u8 *cnet, size_t cnetSize, const u8 *ctxt, size_t ctxtSize,
                                   struct RldCharNative *out)
{
	struct RldCharHull hulls[RLDCHAR_NET_POSES];

	memset(out, 0, sizeof(*out));
	if ((cmdlSize < 8u) || !RldChar_FrameHulls(&cmdl[4], Rld_ReadLE32(&cmdl[0]), 0u, hulls))
	{
		RldChar_NativeFail(out, "native-file", "the frames of the model cannot be read");
		return;
	}
	if (cnet == NULL)
	{
		return;
	}
	out->cnet = (u8 *)malloc(cnetSize ? cnetSize : 1u);
	if (out->cnet == NULL)
	{
		RldChar_NativeFail(out, "native-chunk", "out of memory");
		return;
	}
	memcpy(out->cnet, cnet, cnetSize);
	out->cnetSize = cnetSize;
	if (RldChar_NativeNet(out, chriFlags, hulls))
	{
		RldChar_NativeTextures(out, ctxt, ctxtSize, ctxt != NULL);
	}
}

// CHRI, CMDL, then CICN, CMSK, CVOI, CNET, CTXT when there are any, in this
// order. Without CNET and CTXT the bytes of RldMk_ContainerVoices.
static u8 *RldMk_ContainerNative(const u8 *chri, size_t chriSize, const u8 *cmdl, size_t cmdlSize, const u8 *cicn, const u8 *cmsk, size_t cmskSize,
                                 const u8 *cvoi, size_t cvoiSize, const u8 *cnet, size_t cnetSize, const u8 *ctxt, size_t ctxtSize, size_t *sizeOut)
{
	struct RldContainer container;
	u8 *out;
	int i;

	memset(&container, 0, sizeof(container));
	Rld_AddChunk(&container, "CHRI", chri, chriSize, RLD_COMPRESSION_NONE);
	Rld_AddChunk(&container, "CMDL", cmdl, cmdlSize, RLD_COMPRESSION_NONE);
	if (cicn != NULL)
	{
		Rld_AddChunk(&container, "CICN", cicn, RLDCHAR_ICON_BYTES, RLD_COMPRESSION_NONE);
	}
	if (cmsk != NULL)
	{
		Rld_AddChunk(&container, "CMSK", cmsk, cmskSize, RLD_COMPRESSION_NONE);
	}
	if (cvoi != NULL)
	{
		Rld_AddChunk(&container, "CVOI", cvoi, cvoiSize, RLD_COMPRESSION_NONE);
	}
	if (cnet != NULL)
	{
		Rld_AddChunk(&container, "CNET", cnet, cnetSize, RLD_COMPRESSION_NONE);
	}
	if (ctxt != NULL)
	{
		Rld_AddChunk(&container, "CTXT", ctxt, ctxtSize, RLD_COMPRESSION_NONE);
	}

	out = Rld_WriteEnvelope(&s_rldCharFormat, 0u, &container, sizeOut);

	for (i = 0; i < container.chunkCount; i++)
	{
		free(container.chunks[i].stored);
	}

	return out;
}
