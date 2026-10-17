#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE HIRES TESTGLB - a code-made glTF 2.0 binary (GLB) builder, for the
// selftests of the hi-res driver loader (round of 2026-10-17).
//
// WHY IT EXISTS: the loader parses a glTF file with cgltf and must cope with
// every shape a driver artist may deliver: skinned or plain body, three or four
// wheels, any subset of the twelve clips, three interpolation modes, PNG or
// JPEG textures, extensions the loader refuses, an external buffer. Shipping a
// binary test file for each shape would put data into the repo and would hide
// what is in it. So the test file is MADE IN MEMORY by this builder, from a
// small description (EngHiresTestGlbDesc), and the selftests compare what the
// loader reads against numbers that are written down in this file.
//
// WHAT IS NOT HERE: no file is ever read or written, no state is kept (every
// function allocates and frees its own memory), nothing of the game or of the
// renderer is touched. The host is assumed little endian (x86): float, u16 and
// u32 arrays are copied into the binary chunk as they lie in memory, the
// integer fields of the GLB container and of the PNG chunks are written byte
// by byte so they are right on any host.
//
// THE MODEL (all numbers are exact; the selftests rely on them):
//   nodes    0 "kart" (children 1, 3, then the wheels), 1 "root" (translation
//            0,0,0, child 2), 2 "bone" (translation 0,0.5,0), 3 "body" (mesh 0,
//            skin 0 when skinned), 4..7 "wheel_fl", "wheel_fr", "wheel_rl",
//            "wheel_rr" (mesh 1), as many wheels as the description asks for
//   mesh 0   "body": 4 vertices, 2 triangles (0,1,2 and 1,3,2), uv, normal
//            (0,0,1), and when skinned joints and weights (v3 is half on each
//            joint)
//   mesh 1   "wheel": a fan of 4 triangles, 12 non-indexed vertices, no uv
//   skin 0   joints 1 and 2, inverse bind matrices identity and translation
//            (0,-0.5,0)
//   anims    one per set bit k of clipMask: a rotation of the bone about +Z by
//            (k + 1) * 10 degrees after D seconds (D = 0.5 for "jump", else 1)
//            and a small up-and-down translation of the root
//
// Everything is `internal`; the names are prefixed EngHiresTest_.

typedef struct
{
	int skinned;        // 1 = mesh "body" skinned to the 2 joints (skin 0); 0 = "body" is a plain mesh node (no skin, no JOINTS_0/WEIGHTS_0)
	int wheels;         // number of wheel nodes made: 0, 3 (wheel_fl, wheel_fr, wheel_rl) or 4 (all four)
	u32 clipMask;       // bit k = clip k of the name table below is written as a glTF animation
	int interpolation;  // 0 LINEAR, 1 STEP, 2 CUBICSPLINE (all samplers)
	int texture;        // 0 none, 1 a 16x16 RGBA PNG base colour texture, 2 a base colour image whose bytes start with the JPEG signature FF D8 FF E0 (20 bytes, mimeType image/jpeg)
	int normalMap;      // 1 = a 16x16 RGB PNG normalTexture (scale 1.0) on the material (needs texture == 1, else ignored)
	int requiredExt;    // 1 = "extensionsUsed" and "extensionsRequired" both list "KHR_draco_mesh_compression"
	int unlitExt;       // 1 = the material has extension KHR_materials_unlit (listed in extensionsUsed AND extensionsRequired)
	int externalUri;    // 1 = buffers[0] has "uri": "external.bin" instead of using the GLB BIN chunk (the BIN chunk is still written)
	float colour0;      // > 0: the body's four vertices get COLOR_0, a linear grey of this value (float VEC3, the last accessor)
} EngHiresTestGlbDesc;

// The clip names, index = bit of clipMask (the fixed list of the hi-res format).
static const char *const s_engHiresTestClipNames[12] = {"idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right", "hit", "podium_1", "podium_2", "podium_3", "lose", "menu"};

// ---------------------------------------------------------------------------
// Growable buffer (used for the JSON text and for every byte array).
// ---------------------------------------------------------------------------

typedef struct
{
	u8 *p;
	size_t len;
	size_t cap;
	int fail;   // set once an allocation failed; every later call is a no-op
} EngHiresTestBuf;

// Makes room for `extra` more bytes (the capacity doubles). On failure the old
// block stays valid (so EngHiresTest_BufFree still frees it) and `fail` is set.
internal int EngHiresTest_BufReserve(EngHiresTestBuf *b, size_t extra)
{
	size_t need;
	size_t newCap;
	u8 *np;

	if (b->fail)
	{
		return 0;
	}
	need = b->len + extra;
	if (need <= b->cap)
	{
		return 1;
	}
	newCap = (b->cap != 0) ? b->cap : 256;
	while (newCap < need)
	{
		newCap *= 2;
	}
	np = (u8 *)realloc(b->p, newCap);
	if (np == NULL)
	{
		b->fail = 1;
		return 0;
	}
	b->p = np;
	b->cap = newCap;
	return 1;
}

internal void EngHiresTest_BufFree(EngHiresTestBuf *b)
{
	free(b->p);
	b->p = NULL;
	b->len = 0;
	b->cap = 0;
}

internal void EngHiresTest_BufPut(EngHiresTestBuf *b, const void *gltf, size_t n)
{
	if (n == 0 || !EngHiresTest_BufReserve(b, n))
	{
		return;
	}
	memcpy(b->p + b->len, gltf, n);
	b->len += n;
}

internal void EngHiresTest_BufPutU8(EngHiresTestBuf *b, u32 v)
{
	u8 c = (u8)(v & 0xffu);
	EngHiresTest_BufPut(b, &c, 1);
}

internal void EngHiresTest_BufPutU16LE(EngHiresTestBuf *b, u32 v)
{
	EngHiresTest_BufPutU8(b, v);
	EngHiresTest_BufPutU8(b, v >> 8);
}

internal void EngHiresTest_BufPutU32LE(EngHiresTestBuf *b, u32 v)
{
	EngHiresTest_BufPutU8(b, v);
	EngHiresTest_BufPutU8(b, v >> 8);
	EngHiresTest_BufPutU8(b, v >> 16);
	EngHiresTest_BufPutU8(b, v >> 24);
}

internal void EngHiresTest_BufPutU32BE(EngHiresTestBuf *b, u32 v)
{
	EngHiresTest_BufPutU8(b, v >> 24);
	EngHiresTest_BufPutU8(b, v >> 16);
	EngHiresTest_BufPutU8(b, v >> 8);
	EngHiresTest_BufPutU8(b, v);
}

// Pads with `fill` until the length is a multiple of 4.
internal void EngHiresTest_BufAlign4(EngHiresTestBuf *b, u32 fill)
{
	while ((b->len & 3u) != 0 && !b->fail)
	{
		EngHiresTest_BufPutU8(b, fill);
	}
}

// snprintf-style append of text (the JSON writer). A single call writes at most
// 1023 characters; all pieces used here are far shorter.
internal void EngHiresTest_JsonPrintf(EngHiresTestBuf *b, const char *fmt, ...)
{
	char tmp[1024];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof(tmp))
	{
		b->fail = 1;
		return;
	}
	EngHiresTest_BufPut(b, tmp, (size_t)n);
}

// Appends the whole content of `src` (a JSON fragment) to `dst`.
internal void EngHiresTest_JsonPutBuf(EngHiresTestBuf *dst, const EngHiresTestBuf *src)
{
	if (src->fail)
	{
		dst->fail = 1;
		return;
	}
	EngHiresTest_BufPut(dst, src->p, src->len);
}

// ---------------------------------------------------------------------------
// PNG writer: stored (uncompressed) deflate blocks, so no compressor is needed.
// ---------------------------------------------------------------------------

// CRC-32 of the PNG standard (polynomial 0xEDB88320, reflected), bit by bit.
internal u32 EngHiresTest_Crc32(const u8 *p, size_t n)
{
	u32 crc = 0xffffffffu;
	size_t i;
	int bit;

	for (i = 0; i < n; i++)
	{
		crc ^= p[i];
		for (bit = 0; bit < 8; bit++)
		{
			crc = (crc >> 1) ^ (((crc & 1u) != 0) ? 0xedb88320u : 0u);
		}
	}
	return crc ^ 0xffffffffu;
}

// Adler-32 of the zlib trailer.
internal u32 EngHiresTest_Adler32(const u8 *p, size_t n)
{
	u32 a = 1;
	u32 b = 0;
	size_t i;

	for (i = 0; i < n; i++)
	{
		a = (a + p[i]) % 65521u;
		b = (b + a) % 65521u;
	}
	return (b << 16) | a;
}

// One PNG chunk: length (big endian), 4 type characters, data, CRC over type+data.
internal void EngHiresTest_PngChunk(EngHiresTestBuf *out, const char *type, const u8 *gltf, size_t n)
{
	size_t typeAt;

	EngHiresTest_BufPutU32BE(out, (u32)n);
	typeAt = out->len;
	EngHiresTest_BufPut(out, type, 4);
	EngHiresTest_BufPut(out, gltf, n);
	if (!out->fail)
	{
		EngHiresTest_BufPutU32BE(out, EngHiresTest_Crc32(out->p + typeAt, n + 4));
	}
}

// Writes a 16 x 16 PNG, 8 bits per channel, no interlace.
//   colorType 6: RGBA, pixel (x,y) = (x*16, y*16, 128, (x == 0 && y == 0) ? 64 : 255)
//   colorType 2: RGB, every pixel (128,128,255)
internal void EngHiresTest_Png16(EngHiresTestBuf *out, int colorType)
{
	static const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	u8 raw[16 * (1 + 16 * 4)];
	u8 ihdr[13];
	EngHiresTestBuf z;
	size_t rawLen = 0;
	size_t pos;
	u32 x;
	u32 y;

	memset(&z, 0, sizeof(z));
	// Scanlines, each prefixed with the filter byte 0 (none).
	for (y = 0; y < 16; y++)
	{
		raw[rawLen++] = 0;
		for (x = 0; x < 16; x++)
		{
			if (colorType == 6)
			{
				raw[rawLen++] = (u8)(x * 16);
				raw[rawLen++] = (u8)(y * 16);
				raw[rawLen++] = 128;
				raw[rawLen++] = (x == 0 && y == 0) ? 64 : 255;
			}
			else
			{
				raw[rawLen++] = 128;
				raw[rawLen++] = 128;
				raw[rawLen++] = 255;
			}
		}
	}

	// zlib stream: CMF 0x78, FLG 0x01 (0x7801 is a multiple of 31), stored blocks, Adler-32.
	EngHiresTest_BufPutU8(&z, 0x78);
	EngHiresTest_BufPutU8(&z, 0x01);
	pos = 0;
	while (pos < rawLen)
	{
		size_t n = rawLen - pos;
		int last;

		if (n > 65535)
		{
			n = 65535;
		}
		last = (pos + n == rawLen);
		EngHiresTest_BufPutU8(&z, last ? 1u : 0u);   // BFINAL, BTYPE 00 (stored)
		EngHiresTest_BufPutU16LE(&z, (u32)n);
		EngHiresTest_BufPutU16LE(&z, (u32)(~(u32)n & 0xffffu));
		EngHiresTest_BufPut(&z, raw + pos, n);
		pos += n;
	}
	EngHiresTest_BufPutU32BE(&z, EngHiresTest_Adler32(raw, rawLen));

	ihdr[0] = 0; ihdr[1] = 0; ihdr[2] = 0; ihdr[3] = 16;   // width
	ihdr[4] = 0; ihdr[5] = 0; ihdr[6] = 0; ihdr[7] = 16;   // height
	ihdr[8] = 8;                                           // bit depth
	ihdr[9] = (u8)colorType;
	ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;              // deflate, filter 0, no interlace

	EngHiresTest_BufPut(out, sig, 8);
	EngHiresTest_PngChunk(out, "IHDR", ihdr, 13);
	if (z.fail)
	{
		out->fail = 1;
	}
	else
	{
		EngHiresTest_PngChunk(out, "IDAT", z.p, z.len);
	}
	EngHiresTest_PngChunk(out, "IEND", NULL, 0);
	EngHiresTest_BufFree(&z);
}

// ---------------------------------------------------------------------------
// Scene builder: the binary chunk plus the JSON text of bufferViews and accessors.
// ---------------------------------------------------------------------------

typedef struct
{
	EngHiresTestBuf bin;
	EngHiresTestBuf views;
	EngHiresTestBuf accs;
	u32 nViews;
	u32 nAccs;
} EngHiresTestScene;

// Appends `n` bytes as a new 4-byte aligned bufferView, returns its index.
internal u32 EngHiresTest_AddView(EngHiresTestScene *sc, const void *gltf, size_t n)
{
	EngHiresTest_BufAlign4(&sc->bin, 0);
	if (sc->nViews > 0)
	{
		EngHiresTest_JsonPrintf(&sc->views, ",");
	}
	EngHiresTest_JsonPrintf(&sc->views, "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", (u32)sc->bin.len, (u32)n);
	EngHiresTest_BufPut(&sc->bin, gltf, n);
	EngHiresTest_BufAlign4(&sc->bin, 0);
	return sc->nViews++;
}

// Appends an accessor on `view`, returns its index. `extra` is a JSON fragment
// that starts with a comma (min/max) or is empty.
internal u32 EngHiresTest_AddAcc(EngHiresTestScene *sc, u32 view, u32 componentType, u32 count, const char *type, const char *extra)
{
	if (sc->nAccs > 0)
	{
		EngHiresTest_JsonPrintf(&sc->accs, ",");
	}
	EngHiresTest_JsonPrintf(&sc->accs, "{\"bufferView\":%u,\"componentType\":%u,\"count\":%u,\"type\":\"%s\"%s}", view, componentType, count, type, extra);
	return sc->nAccs++;
}

// A float accessor from `count` elements of `comps` floats each (1 SCALAR, 2
// VEC2, 3 VEC3, 4 VEC4, 16 MAT4). withMinMax writes "min" and "max" per component
// (glTF requires them for POSITION and for animation inputs).
internal u32 EngHiresTest_AddFloatAcc(EngHiresTestScene *sc, const float *v, u32 count, u32 comps, int withMinMax)
{
	static const char *const typeNames[17] = {"", "SCALAR", "VEC2", "VEC3", "VEC4", "", "", "", "", "", "", "", "", "", "", "", "MAT4"};
	EngHiresTestBuf mm;
	const char *extra;
	u32 view;
	u32 acc;
	u32 c;
	u32 i;

	memset(&mm, 0, sizeof(mm));
	if (withMinMax && count > 0)
	{
		EngHiresTest_JsonPrintf(&mm, ",\"min\":[");
		for (c = 0; c < comps; c++)
		{
			float lo = v[c];
			for (i = 1; i < count; i++)
			{
				if (v[i * comps + c] < lo)
				{
					lo = v[i * comps + c];
				}
			}
			EngHiresTest_JsonPrintf(&mm, "%s%.9g", (c > 0) ? "," : "", (double)lo);
		}
		EngHiresTest_JsonPrintf(&mm, "],\"max\":[");
		for (c = 0; c < comps; c++)
		{
			float hi = v[c];
			for (i = 1; i < count; i++)
			{
				if (v[i * comps + c] > hi)
				{
					hi = v[i * comps + c];
				}
			}
			EngHiresTest_JsonPrintf(&mm, "%s%.9g", (c > 0) ? "," : "", (double)hi);
		}
		EngHiresTest_JsonPrintf(&mm, "]");
	}
	EngHiresTest_BufPutU8(&mm, 0);   // terminating zero for use as a C string
	extra = (mm.fail || mm.p == NULL) ? "" : (const char *)mm.p;

	view = EngHiresTest_AddView(sc, v, (size_t)count * comps * sizeof(float));
	acc = EngHiresTest_AddAcc(sc, view, 5126, count, typeNames[comps], extra);
	EngHiresTest_BufFree(&mm);
	return acc;
}

// ---------------------------------------------------------------------------
// The builder.
// ---------------------------------------------------------------------------

internal const char *EngHiresTest_InterpName(int interpolation)
{
	if (interpolation == 1)
	{
		return "STEP";
	}
	if (interpolation == 2)
	{
		return "CUBICSPLINE";
	}
	return "LINEAR";
}

// Writes the animation for clip k into `anims` (JSON) and its data into `sc`.
internal void EngHiresTest_AddClip(EngHiresTestScene *sc, EngHiresTestBuf *anims, u32 k, int interpolation, int *nAnims)
{
	float D = (k == 3) ? 0.5f : 1.0f;
	double halfRad = ((double)(k + 1) * 10.0 * 3.14159265358979323846 / 180.0) * 0.5;
	float qz = sinf((float)halfRad);
	float qw = cosf((float)halfRad);
	float up = 0.01f * (float)(k + 1);
	int cubic = (interpolation == 2);
	float tRot[2];
	float tTr[3];
	float rot[24];
	float tr[27];
	u32 nRot = 0;
	u32 nTr = 0;
	u32 key;
	u32 j;
	u32 aTimeRot;
	u32 aTimeTr;
	u32 aRot;
	u32 aTr;

	tRot[0] = 0.0f;
	tRot[1] = D;
	tTr[0] = 0.0f;
	tTr[1] = D * 0.5f;
	tTr[2] = D;

	// Rotation outputs: key 0 identity, key 1 the quaternion; for CUBICSPLINE
	// every key is (in-tangent, value, out-tangent) with zero tangents.
	for (key = 0; key < 2; key++)
	{
		if (cubic)
		{
			for (j = 0; j < 4; j++)
			{
				rot[nRot++] = 0.0f;
			}
		}
		rot[nRot++] = 0.0f;
		rot[nRot++] = 0.0f;
		rot[nRot++] = (key == 0) ? 0.0f : qz;
		rot[nRot++] = (key == 0) ? 1.0f : qw;
		if (cubic)
		{
			for (j = 0; j < 4; j++)
			{
				rot[nRot++] = 0.0f;
			}
		}
	}
	// Translation outputs: (0,0,0), (0,up,0), (0,0,0).
	for (key = 0; key < 3; key++)
	{
		if (cubic)
		{
			for (j = 0; j < 3; j++)
			{
				tr[nTr++] = 0.0f;
			}
		}
		tr[nTr++] = 0.0f;
		tr[nTr++] = (key == 1) ? up : 0.0f;
		tr[nTr++] = 0.0f;
		if (cubic)
		{
			for (j = 0; j < 3; j++)
			{
				tr[nTr++] = 0.0f;
			}
		}
	}

	aTimeRot = EngHiresTest_AddFloatAcc(sc, tRot, 2, 1, 1);
	aTimeTr = EngHiresTest_AddFloatAcc(sc, tTr, 3, 1, 1);
	aRot = EngHiresTest_AddFloatAcc(sc, rot, cubic ? 6u : 2u, 4, 0);
	aTr = EngHiresTest_AddFloatAcc(sc, tr, cubic ? 9u : 3u, 3, 0);

	if (*nAnims > 0)
	{
		EngHiresTest_JsonPrintf(anims, ",");
	}
	EngHiresTest_JsonPrintf(anims, "{\"name\":\"%s\",\"samplers\":[", s_engHiresTestClipNames[k]);
	EngHiresTest_JsonPrintf(anims, "{\"input\":%u,\"output\":%u,\"interpolation\":\"%s\"},", aTimeRot, aRot, EngHiresTest_InterpName(interpolation));
	EngHiresTest_JsonPrintf(anims, "{\"input\":%u,\"output\":%u,\"interpolation\":\"%s\"}],", aTimeTr, aTr, EngHiresTest_InterpName(interpolation));
	EngHiresTest_JsonPrintf(anims, "\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"rotation\"}},{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"translation\"}}]}");
	(*nAnims)++;
}

// Writes the extension name list "[...]" for extensionsUsed / extensionsRequired.
internal void EngHiresTest_JsonExtList(EngHiresTestBuf *json, const EngHiresTestGlbDesc *d)
{
	EngHiresTest_JsonPrintf(json, "[");
	if (d->requiredExt)
	{
		EngHiresTest_JsonPrintf(json, "\"KHR_draco_mesh_compression\"%s", d->unlitExt ? "," : "");
	}
	if (d->unlitExt)
	{
		EngHiresTest_JsonPrintf(json, "\"KHR_materials_unlit\"");
	}
	EngHiresTest_JsonPrintf(json, "]");
}

// Returns malloc'd GLB bytes (*size set), NULL when out of memory. The caller frees with free().
internal u8 *EngHiresTest_Glb(const EngHiresTestGlbDesc *d, size_t *size)
{
	static const float bodyPos[12] = {0.0f, 0.0f, 0.0f,  0.1f, 0.0f, 0.0f,  0.0f, 0.5f, 0.0f,  0.1f, 0.5f, 0.0f};
	static const float bodyNrm[12] = {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f};
	static const float bodyUv[8] = {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f};
	static const u8 bodyJoints[16] = {0, 0, 0, 0,  0, 0, 0, 0,  1, 0, 0, 0,  0, 1, 0, 0};
	static const float bodyWeights[16] = {1.0f, 0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 0.0f,  0.5f, 0.5f, 0.0f, 0.0f};
	static const u16 bodyIdx[6] = {0, 1, 2, 1, 3, 2};
	static const float ibm[32] = {
		1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f,  0.0f, -0.5f, 0.0f, 1.0f};
	static const char *const wheelNames[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
	static const float wheelX[4] = {0.5625f, -0.5625f, 0.5625f, -0.5625f};
	static const float wheelZ[4] = {0.7773f, 0.7773f, -0.375f, -0.375f};
	static const float pt[5][3] = {{0.05f, 0.0f, 0.0f}, {0.0f, 0.25f, 0.0f}, {0.0f, 0.0f, 0.25f}, {0.0f, -0.25f, 0.0f}, {0.0f, 0.0f, -0.25f}};
	static const int tri[4][3] = {{0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 1}};
	static const u8 jpg[20] = {0xff, 0xd8, 0xff, 0xe0, 0, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
	static const char magic[4] = {'g', 'l', 'T', 'F'};

	EngHiresTestScene sc;
	EngHiresTestBuf json;
	EngHiresTestBuf anims;
	EngHiresTestBuf png;
	EngHiresTestBuf norm;
	EngHiresTestBuf glb;
	float wheelPos[36];
	float wheelNrm[36];
	u32 aPos;
	u32 aNrm;
	u32 aUv;
	u32 aJoints = 0;
	u32 aWeights = 0;
	u32 aIdx;
	u32 aIbm = 0;
	u32 aWPos = 0;
	u32 aWNrm = 0;
	u32 vJoints;
	u32 vIdx;
	u32 vImg0 = 0;
	u32 vImg1 = 0;
	u32 aCol = 0;
	u32 wheels;
	u32 texture;
	int normalMap;
	int nAnims = 0;
	u32 k;
	u32 i;
	u32 jsonLen;
	u32 binLen;
	u32 total;
	u8 *result = NULL;

	memset(&sc, 0, sizeof(sc));
	memset(&json, 0, sizeof(json));
	memset(&anims, 0, sizeof(anims));
	memset(&png, 0, sizeof(png));
	memset(&norm, 0, sizeof(norm));
	memset(&glb, 0, sizeof(glb));

	wheels = (d->wheels >= 4) ? 4u : ((d->wheels >= 3) ? 3u : 0u);
	texture = (d->texture == 1 || d->texture == 2) ? (u32)d->texture : 0u;
	normalMap = (d->normalMap != 0 && texture == 1);

	// ----- binary chunk: the body mesh -----
	aPos = EngHiresTest_AddFloatAcc(&sc, bodyPos, 4, 3, 1);
	aNrm = EngHiresTest_AddFloatAcc(&sc, bodyNrm, 4, 3, 0);
	aUv = EngHiresTest_AddFloatAcc(&sc, bodyUv, 4, 2, 0);
	if (d->skinned)
	{
		vJoints = EngHiresTest_AddView(&sc, bodyJoints, sizeof(bodyJoints));
		aJoints = EngHiresTest_AddAcc(&sc, vJoints, 5121, 4, "VEC4", "");
		aWeights = EngHiresTest_AddFloatAcc(&sc, bodyWeights, 4, 4, 0);
	}
	vIdx = EngHiresTest_AddView(&sc, bodyIdx, sizeof(bodyIdx));
	aIdx = EngHiresTest_AddAcc(&sc, vIdx, 5123, 6, "SCALAR", "");
	if (d->skinned)
	{
		aIbm = EngHiresTest_AddFloatAcc(&sc, ibm, 2, 16, 0);
	}

	// ----- the wheel mesh: a fan of 4 triangles around c, 12 vertices -----
	if (wheels > 0)
	{
		for (i = 0; i < 4; i++)
		{
			u32 c;
			for (c = 0; c < 3; c++)
			{
				u32 v = i * 3 + c;
				wheelPos[v * 3 + 0] = pt[tri[i][c]][0];
				wheelPos[v * 3 + 1] = pt[tri[i][c]][1];
				wheelPos[v * 3 + 2] = pt[tri[i][c]][2];
				wheelNrm[v * 3 + 0] = 1.0f;
				wheelNrm[v * 3 + 1] = 0.0f;
				wheelNrm[v * 3 + 2] = 0.0f;
			}
		}
		aWPos = EngHiresTest_AddFloatAcc(&sc, wheelPos, 12, 3, 1);
		aWNrm = EngHiresTest_AddFloatAcc(&sc, wheelNrm, 12, 3, 0);
	}

	// ----- images -----
	if (texture == 1)
	{
		EngHiresTest_Png16(&png, 6);
		vImg0 = EngHiresTest_AddView(&sc, png.p, png.len);
		if (normalMap)
		{
			EngHiresTest_Png16(&norm, 2);
			vImg1 = EngHiresTest_AddView(&sc, norm.p, norm.len);
		}
	}
	else if (texture == 2)
	{
		// 20 bytes that start with the JPEG signature (not a decodable picture).
		vImg0 = EngHiresTest_AddView(&sc, jpg, sizeof(jpg));
	}

	// ----- animations -----
	for (k = 0; k < 12; k++)
	{
		if ((d->clipMask & (1u << k)) != 0)
		{
			EngHiresTest_AddClip(&sc, &anims, k, d->interpolation, &nAnims);
		}
	}

	// ----- COLOR_0 of the body, last: a model without it keeps every index -----
	if (d->colour0 > 0.0f)
	{
		float col[12];

		for (i = 0; i < 12; i++)
		{
			col[i] = d->colour0;
		}
		aCol = EngHiresTest_AddFloatAcc(&sc, col, 4, 3, 0);
	}

	// ----- JSON -----
	EngHiresTest_JsonPrintf(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"engine_hires_testglb\"},");
	if (d->requiredExt || d->unlitExt)
	{
		EngHiresTest_JsonPrintf(&json, "\"extensionsUsed\":");
		EngHiresTest_JsonExtList(&json, d);
		EngHiresTest_JsonPrintf(&json, ",\"extensionsRequired\":");
		EngHiresTest_JsonExtList(&json, d);
		EngHiresTest_JsonPrintf(&json, ",");
	}
	EngHiresTest_JsonPrintf(&json, "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],");

	// nodes
	EngHiresTest_JsonPrintf(&json, "\"nodes\":[{\"name\":\"kart\",\"children\":[1,3");
	for (i = 0; i < wheels; i++)
	{
		EngHiresTest_JsonPrintf(&json, ",%u", 4 + i);
	}
	EngHiresTest_JsonPrintf(&json, "]},");
	EngHiresTest_JsonPrintf(&json, "{\"name\":\"root\",\"translation\":[0,0,0],\"children\":[2]},");
	EngHiresTest_JsonPrintf(&json, "{\"name\":\"bone\",\"translation\":[0,0.5,0]},");
	EngHiresTest_JsonPrintf(&json, "{\"name\":\"body\",\"mesh\":0%s}", d->skinned ? ",\"skin\":0" : "");
	for (i = 0; i < wheels; i++)
	{
		EngHiresTest_JsonPrintf(&json, ",{\"name\":\"%s\",\"translation\":[%.9g,0.25,%.9g],\"mesh\":1}", wheelNames[i], (double)wheelX[i], (double)wheelZ[i]);
	}
	EngHiresTest_JsonPrintf(&json, "],");

	if (d->skinned)
	{
		EngHiresTest_JsonPrintf(&json, "\"skins\":[{\"joints\":[1,2],\"inverseBindMatrices\":%u,\"skeleton\":1}],", aIbm);
	}

	// meshes
	EngHiresTest_JsonPrintf(&json, "\"meshes\":[{\"name\":\"body\",\"primitives\":[{\"mode\":4,\"material\":0,\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u,\"TEXCOORD_0\":%u", aPos, aNrm, aUv);
	if (d->skinned)
	{
		EngHiresTest_JsonPrintf(&json, ",\"JOINTS_0\":%u,\"WEIGHTS_0\":%u", aJoints, aWeights);
	}
	if (d->colour0 > 0.0f)
	{
		EngHiresTest_JsonPrintf(&json, ",\"COLOR_0\":%u", aCol);
	}
	EngHiresTest_JsonPrintf(&json, "},\"indices\":%u}]}", aIdx);
	if (wheels > 0)
	{
		EngHiresTest_JsonPrintf(&json, ",{\"name\":\"wheel\",\"primitives\":[{\"mode\":4,\"material\":0,\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u}}]}", aWPos, aWNrm);
	}
	EngHiresTest_JsonPrintf(&json, "],");

	// material
	EngHiresTest_JsonPrintf(&json, "\"materials\":[{\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.5,0.25,1]");
	if (texture != 0)
	{
		EngHiresTest_JsonPrintf(&json, ",\"baseColorTexture\":{\"index\":0}");
	}
	EngHiresTest_JsonPrintf(&json, "}");
	if (normalMap)
	{
		EngHiresTest_JsonPrintf(&json, ",\"normalTexture\":{\"index\":1,\"scale\":1.0}");
	}
	EngHiresTest_JsonPrintf(&json, ",\"doubleSided\":true");
	if (d->unlitExt)
	{
		EngHiresTest_JsonPrintf(&json, ",\"extensions\":{\"KHR_materials_unlit\":{}}");
	}
	EngHiresTest_JsonPrintf(&json, "}],");

	// samplers, textures, images
	if (texture != 0)
	{
		const char *mime = (texture == 1) ? "image/png" : "image/jpeg";

		EngHiresTest_JsonPrintf(&json, "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":33071,\"wrapT\":33071}],");
		EngHiresTest_JsonPrintf(&json, "\"textures\":[{\"sampler\":0,\"source\":0}%s],", normalMap ? ",{\"sampler\":0,\"source\":1}" : "");
		EngHiresTest_JsonPrintf(&json, "\"images\":[{\"bufferView\":%u,\"mimeType\":\"%s\"}", vImg0, mime);
		if (normalMap)
		{
			EngHiresTest_JsonPrintf(&json, ",{\"bufferView\":%u,\"mimeType\":\"image/png\"}", vImg1);
		}
		EngHiresTest_JsonPrintf(&json, "],");
	}

	// animations
	if (nAnims > 0)
	{
		EngHiresTest_JsonPrintf(&json, "\"animations\":[");
		EngHiresTest_JsonPutBuf(&json, &anims);
		EngHiresTest_JsonPrintf(&json, "],");
	}

	// accessors, bufferViews, buffers
	EngHiresTest_JsonPrintf(&json, "\"accessors\":[");
	EngHiresTest_JsonPutBuf(&json, &sc.accs);
	EngHiresTest_JsonPrintf(&json, "],\"bufferViews\":[");
	EngHiresTest_JsonPutBuf(&json, &sc.views);
	EngHiresTest_BufAlign4(&sc.bin, 0);
	EngHiresTest_JsonPrintf(&json, "],\"buffers\":[{%s\"byteLength\":%u}]}", d->externalUri ? "\"uri\":\"external.bin\"," : "", (u32)sc.bin.len);

	// ----- GLB container -----
	if (!json.fail && !sc.bin.fail && !sc.views.fail && !sc.accs.fail && !anims.fail && !png.fail && !norm.fail)
	{
		EngHiresTest_BufAlign4(&json, 0x20);
		jsonLen = (u32)json.len;
		binLen = (u32)sc.bin.len;
		total = 12u + 8u + jsonLen + 8u + binLen;
		EngHiresTest_BufPut(&glb, magic, 4);
		EngHiresTest_BufPutU32LE(&glb, 2);
		EngHiresTest_BufPutU32LE(&glb, total);
		EngHiresTest_BufPutU32LE(&glb, jsonLen);
		EngHiresTest_BufPutU32LE(&glb, 0x4E4F534Au);
		EngHiresTest_BufPut(&glb, json.p, json.len);
		EngHiresTest_BufPutU32LE(&glb, binLen);
		EngHiresTest_BufPutU32LE(&glb, 0x004E4942u);
		EngHiresTest_BufPut(&glb, sc.bin.p, sc.bin.len);
		if (!glb.fail && glb.len == (size_t)total)
		{
			result = glb.p;
			glb.p = NULL;
			*size = glb.len;
		}
	}

	EngHiresTest_BufFree(&glb);
	EngHiresTest_BufFree(&norm);
	EngHiresTest_BufFree(&png);
	EngHiresTest_BufFree(&anims);
	EngHiresTest_BufFree(&json);
	EngHiresTest_BufFree(&sc.accs);
	EngHiresTest_BufFree(&sc.views);
	EngHiresTest_BufFree(&sc.bin);
	if (result == NULL)
	{
		*size = 0;
	}
	return result;
}

// ---------------------------------------------------------------------------
// Self-check.
// ---------------------------------------------------------------------------

// The file reader handed to cgltf: no file access is ever allowed in a selftest.
internal cgltf_result EngHiresTest_NoFileRead(const struct cgltf_memory_options *mem, const struct cgltf_file_options *file, const char *path, cgltf_size *sz, void **gltf)
{
	(void)mem;
	(void)file;
	(void)path;
	(void)sz;
	(void)gltf;
	return cgltf_result_io_error;
}

internal void EngHiresTest_Check(int *checks, int *fails, int ok, const char *what)
{
	(*checks)++;
	if (!ok)
	{
		(*fails)++;
		printf("engine selftest: hires testglb %s FAILED\n", what);
	}
}

internal int EngHiresTest_StrEq(const char *a, const char *b)
{
	return a != NULL && b != NULL && strcmp(a, b) == 0;
}

// The accessor of attribute `type` on the first primitive of mesh `mesh`, or NULL.
internal const cgltf_accessor *EngHiresTest_FindAttr(const cgltf_data *gltf, cgltf_size mesh, cgltf_attribute_type type)
{
	const cgltf_primitive *prim;
	cgltf_size i;

	if (mesh >= gltf->meshes_count || gltf->meshes[mesh].primitives_count < 1)
	{
		return NULL;
	}
	prim = &gltf->meshes[mesh].primitives[0];
	for (i = 0; i < prim->attributes_count; i++)
	{
		if (prim->attributes[i].type == type)
		{
			return prim->attributes[i].data;
		}
	}
	return NULL;
}

// Builds and parses the description; returns the cgltf data (the caller does
// cgltf_free and free(*glbOut)), NULL when the build or the parse failed.
internal cgltf_data *EngHiresTest_BuildParse(const EngHiresTestGlbDesc *d, u8 **glbOut, cgltf_options *opt, int *checks, int *fails, const char *what)
{
	size_t size = 0;
	cgltf_data *gltf = NULL;
	cgltf_result r;

	*glbOut = EngHiresTest_Glb(d, &size);
	EngHiresTest_Check(checks, fails, *glbOut != NULL, what);
	if (*glbOut == NULL)
	{
		return NULL;
	}
	memset(opt, 0, sizeof(*opt));
	opt->file.read = EngHiresTest_NoFileRead;
	r = cgltf_parse(opt, *glbOut, (cgltf_size)size, &gltf);
	EngHiresTest_Check(checks, fails, r == cgltf_result_success && gltf != NULL, what);
	if (r != cgltf_result_success)
	{
		gltf = NULL;
	}
	return gltf;
}

// 0 failures = the builder's output parses with cgltf and has the content above. *checks += number of checks.
internal int EngHiresTest_GlbSelfTest(int *checks)
{
	static const char *const nodeNames[8] = {"kart", "root", "bone", "body", "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
	static const u8 pngSig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	int fails = 0;
	int n = 0;
	EngHiresTestGlbDesc d;
	cgltf_options opt;
	cgltf_data *gltf;
	u8 *glb = NULL;
	cgltf_size i;

	// ===== (a) skinned, 4 wheels, all clips, LINEAR, PNG texture + normal map =====
	memset(&d, 0, sizeof(d));
	d.skinned = 1;
	d.wheels = 4;
	d.clipMask = 0xfffu;
	d.interpolation = 0;
	d.texture = 1;
	d.normalMap = 1;
	gltf = EngHiresTest_BuildParse(&d, &glb, &opt, &n, &fails, "(a) build+parse");
	if (gltf != NULL)
	{
		const cgltf_accessor *acc;
		const cgltf_animation *jump;
		cgltf_float w[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
		cgltf_float q[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		double half = 20.0 * 3.14159265358979323846 / 180.0;   // jump = clip 3: (3 + 1) * 10 = 40 degrees, half angle 20
		int namesOk = 1;
		int clipsOk = 1;
		cgltf_result lr;

		EngHiresTest_Check(&n, &fails, gltf->nodes_count == 8, "(a) node count 8");
		for (i = 0; i < 8 && i < gltf->nodes_count; i++)
		{
			if (!EngHiresTest_StrEq(gltf->nodes[i].name, nodeNames[i]))
			{
				namesOk = 0;
			}
		}
		EngHiresTest_Check(&n, &fails, namesOk, "(a) node names");
		EngHiresTest_Check(&n, &fails, gltf->nodes_count > 3 && gltf->skins_count > 0 && gltf->nodes[3].skin == &gltf->skins[0], "(a) body node has skin 0");
		EngHiresTest_Check(&n, &fails, gltf->skins_count == 1 && gltf->skins[0].joints_count == 2, "(a) skin has 2 joints");
		EngHiresTest_Check(&n, &fails, gltf->meshes_count == 2, "(a) 2 meshes");
		EngHiresTest_Check(&n, &fails, gltf->animations_count == 12, "(a) 12 animations");
		for (i = 0; i < 12 && i < gltf->animations_count; i++)
		{
			if (!EngHiresTest_StrEq(gltf->animations[i].name, s_engHiresTestClipNames[i]))
			{
				clipsOk = 0;
			}
		}
		EngHiresTest_Check(&n, &fails, clipsOk, "(a) animation names");
		EngHiresTest_Check(&n, &fails, gltf->materials_count == 1 && gltf->materials[0].pbr_metallic_roughness.base_color_texture.texture != NULL, "(a) base colour texture");
		EngHiresTest_Check(&n, &fails, gltf->materials_count == 1 && gltf->materials[0].normal_texture.texture != NULL, "(a) normal texture");

		acc = EngHiresTest_FindAttr(gltf, 0, cgltf_attribute_type_position);
		EngHiresTest_Check(&n, &fails, acc != NULL && acc->count == 4, "(a) body POSITION count 4");
		acc = EngHiresTest_FindAttr(gltf, 1, cgltf_attribute_type_position);
		EngHiresTest_Check(&n, &fails, acc != NULL && acc->count == 12, "(a) wheel POSITION count 12");
		EngHiresTest_Check(&n, &fails, EngHiresTest_FindAttr(gltf, 1, cgltf_attribute_type_texcoord) == NULL, "(a) wheel has no TEXCOORD_0");
		EngHiresTest_Check(&n, &fails, gltf->meshes_count == 2 && gltf->meshes[1].primitives[0].indices == NULL, "(a) wheel not indexed");

		lr = cgltf_load_buffers(&opt, gltf, NULL);
		EngHiresTest_Check(&n, &fails, lr == cgltf_result_success, "(a) load_buffers success");
		EngHiresTest_Check(&n, &fails, gltf->buffers_count == 1 && gltf->buffers[0].data != NULL, "(a) buffer 0 gltf");

		acc = EngHiresTest_FindAttr(gltf, 0, cgltf_attribute_type_weights);
		EngHiresTest_Check(&n, &fails, acc != NULL && cgltf_accessor_read_float(acc, 3, w, 4) != 0
			&& w[0] == 0.5f && w[1] == 0.5f && w[2] == 0.0f && w[3] == 0.0f, "(a) v3 weights 0.5 0.5 0 0");

		jump = (gltf->animations_count > 3) ? &gltf->animations[3] : NULL;
		EngHiresTest_Check(&n, &fails, jump != NULL && jump->channels_count == 2 && jump->samplers_count == 2, "(a) jump has 2 channels, 2 samplers");
		if (jump != NULL && jump->channels_count == 2 && jump->samplers_count == 2)
		{
			const cgltf_animation_channel *ch = &jump->channels[0];
			const cgltf_accessor *in = ch->sampler->input;

			EngHiresTest_Check(&n, &fails, ch->target_node == &gltf->nodes[2] && ch->target_path == cgltf_animation_path_type_rotation, "(a) jump channel 0 rotates bone");
			EngHiresTest_Check(&n, &fails, ch->sampler->interpolation == cgltf_interpolation_type_linear, "(a) jump LINEAR");
			EngHiresTest_Check(&n, &fails, in->count == 2 && in->has_min && in->has_max && in->min[0] == 0.0f && in->max[0] == 0.5f, "(a) jump input 2 keys, min 0, max 0.5");
			EngHiresTest_Check(&n, &fails, jump->channels[1].target_node == &gltf->nodes[1] && jump->channels[1].target_path == cgltf_animation_path_type_translation, "(a) jump channel 1 translates root");
			EngHiresTest_Check(&n, &fails, cgltf_accessor_read_float(ch->sampler->output, 1, q, 4) != 0
				&& fabs((double)q[0]) < 1e-6 && fabs((double)q[1]) < 1e-6
				&& fabs((double)q[2] - sin(half)) < 1e-6
				&& fabs((double)q[3] - cos(half)) < 1e-6, "(a) jump rotation key 1 is sin/cos of 20 degrees");
		}
		EngHiresTest_Check(&n, &fails, gltf->animations_count > 0 && gltf->animations[0].channels[0].sampler->input->max[0] == 1.0f, "(a) idle duration 1");

		EngHiresTest_Check(&n, &fails, gltf->images_count == 2 && gltf->images[0].buffer_view != NULL, "(a) 2 images, image 0 in a buffer view");
		if (gltf->images_count == 2 && gltf->images[0].buffer_view != NULL)
		{
			const cgltf_buffer_view *bv = gltf->images[0].buffer_view;
			const uint8_t *bytes = cgltf_buffer_view_data(bv);

			EngHiresTest_Check(&n, &fails, bytes != NULL && bv->size > 8 && memcmp(bytes, pngSig, 8) == 0, "(a) image 0 starts with the PNG signature");
			EngHiresTest_Check(&n, &fails, EngHiresTest_StrEq(gltf->images[0].mime_type, "image/png"), "(a) image 0 mime png");
		}
		cgltf_free(gltf);
	}
	free(glb);

	// ===== (b) plain body, 3 wheels, clips idle + jump, CUBICSPLINE, no texture =====
	memset(&d, 0, sizeof(d));
	d.skinned = 0;
	d.wheels = 3;
	d.clipMask = (1u << 0) | (1u << 3);
	d.interpolation = 2;
	d.texture = 0;
	glb = NULL;
	gltf = EngHiresTest_BuildParse(&d, &glb, &opt, &n, &fails, "(b) build+parse");
	if (gltf != NULL)
	{
		const cgltf_accessor *acc;

		EngHiresTest_Check(&n, &fails, gltf->skins_count == 0, "(b) skins_count 0");
		EngHiresTest_Check(&n, &fails, gltf->nodes_count == 7, "(b) node count 7");
		EngHiresTest_Check(&n, &fails, gltf->nodes_count > 3 && gltf->nodes[3].skin == NULL && gltf->nodes[3].mesh == &gltf->meshes[0], "(b) body is a plain mesh node");
		EngHiresTest_Check(&n, &fails, EngHiresTest_FindAttr(gltf, 0, cgltf_attribute_type_joints) == NULL && EngHiresTest_FindAttr(gltf, 0, cgltf_attribute_type_weights) == NULL, "(b) no JOINTS_0/WEIGHTS_0");
		EngHiresTest_Check(&n, &fails, gltf->animations_count == 2 && EngHiresTest_StrEq(gltf->animations[0].name, "idle") && EngHiresTest_StrEq(gltf->animations[1].name, "jump"), "(b) animations idle, jump");
		EngHiresTest_Check(&n, &fails, gltf->images_count == 0 && gltf->textures_count == 0, "(b) no images, no textures");
		if (gltf->animations_count == 2 && gltf->animations[1].channels_count == 2)
		{
			const cgltf_animation_sampler *s0 = gltf->animations[1].channels[0].sampler;
			const cgltf_animation_sampler *s1 = gltf->animations[1].channels[1].sampler;

			EngHiresTest_Check(&n, &fails, s0->interpolation == cgltf_interpolation_type_cubic_spline, "(b) CUBICSPLINE");
			EngHiresTest_Check(&n, &fails, s0->output->count == 3 * s0->input->count, "(b) rotation output count 3 x input");
			EngHiresTest_Check(&n, &fails, s1->output->count == 3 * s1->input->count && s1->input->count == 3, "(b) translation output count 3 x input");
		}
		acc = EngHiresTest_FindAttr(gltf, 1, cgltf_attribute_type_position);
		EngHiresTest_Check(&n, &fails, acc != NULL && acc->count == 12, "(b) wheel POSITION count 12");
		cgltf_free(gltf);
	}
	free(glb);

	// ===== (c) external uri: parses, the uri is kept, no buffer load =====
	memset(&d, 0, sizeof(d));
	d.skinned = 1;
	d.wheels = 4;
	d.externalUri = 1;
	glb = NULL;
	gltf = EngHiresTest_BuildParse(&d, &glb, &opt, &n, &fails, "(c) build+parse");
	if (gltf != NULL)
	{
		EngHiresTest_Check(&n, &fails, gltf->buffers_count == 1 && EngHiresTest_StrEq(gltf->buffers[0].uri, "external.bin"), "(c) buffer 0 uri external.bin");
		cgltf_free(gltf);
	}
	free(glb);

	*checks += n;
	return fails;
}
