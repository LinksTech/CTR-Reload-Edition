// rldpack_hires_test.inc - the self-test of include/rldhires.inc (the hi-res
// driver, chunk CHRG): `rldpack selftest-hires`, ctest rldhires_selftest.
//
// Included by tools/rldpack.c after every helper it uses (Rld_AddChunk,
// Rld_WriteEnvelope, Sha256, s_rldCharFormat). PURE: no file is read or
// written. The GLBs are made here in code (HrTest_Glb), one base model that
// passes every rule and a variant per rule, so that each rule HRG-1..31 has
// at least one case that must NOT fire it and one that MUST. The budget is
// checked on counts set directly (RldHires_CheckBudget reads only the counts),
// and once on a real GLB. The last group proves that a container without CHRG
// is read exactly as before: CHRG is no known type of the envelope, and adding
// it leaves every other chunk's bytes as they were.

// One knob per rule; all 0 is the base model, which passes every rule.
struct HrTestSpec
{
	int clipsMissing;       // bit per RLDHIRES_CLIP_* that is NOT written
	int wheelsMissing;      // bit per RLDHIRES_WHEEL_* that is NOT written
	int skins;              // -1 = none, 0 = the default (1), 2 = two
	int joints;             // 0 = the default (2)
	int fewMatrices;        // the inverse bind matrices one short
	int noNormal;
	int noPosition;
	int floatPosition2;     // POSITION as float VEC2
	int mode;               // 0 = triangles (4), else the glTF mode
	int indexCount;         // 0 = 3
	int noJoints;
	int badJoint;           // a vertex uses joint = joint count
	const char *required;   // extensionsRequired
	int bufferUri;          // 1 = outside file, 2 = data URI (a second buffer)
	int image;              // 0 PNG RGBA, 1 JPEG, 2 GIF, 3 palette PNG, 4 outside file, 5 texture without image, 6 damaged PNG
	u32 pngW;               // 0 = 3
	u32 pngH;               // 0 = 2
	float extent;           // 0 = 1.0: the body triangle's legs in metres
	int nanPosition;
	int morph;
	int camera;
	int light;
	int blend;
	int emissive;
	int cubic;
	int unknownAnim;
	int twiceIdle;
	int sparse;
	int noBody;
	int wheelChild;
	int twiceWheel;
	int wheelNoMesh;        // wheel_rr has no mesh
	int wheelSkinned;       // wheel_rl carries the skin
	int glbVersion;         // 0 = 2
	int glbLength;          // added to the length in the GLB header
	int glbMagic;           // 1 = wrong magic
	int jsonBroken;
	int unusedImage;        // a second image no material uses (not a PNG either)
	int wrapCount;          // a POSITION count whose byte size wraps 32 bits
};

static int s_hrChecks;
static int s_hrFailures;

static void HrTest_Check(int ok, const char *what)
{
	s_hrChecks++;
	if (!ok)
	{
		s_hrFailures++;
		printf("  FAIL hires: %s\n", what);
	}
}

static void HrTest_PutF(u8 *at, float f)
{
	u32 v;

	memcpy(&v, &f, 4);
	RldHires_PutLE32(at, v);
}

static void HrTest_Append(char *json, size_t size, const char *format, ...)
{
	const size_t at = strlen(json);
	va_list list;

	if (at + 1u >= size)
	{
		return;
	}
	va_start(list, format);
	vsnprintf(json + at, size - at, format, list);
	va_end(list);
}

// A PNG with the IHDR of w x h, the given colour type and bit depth, an empty
// IDAT and IEND - only the header is read by the checks, never the pixels.
static u32 HrTest_Png(u8 *out, u32 w, u32 h, u32 colorType, u32 bitDepth)
{
	static const u8 sig[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
	u32 n = 0;
	int i;
	const char *types[3] = {"IHDR", "IDAT", "IEND"};
	const u32 lengths[3] = {13u, 0u, 0u};

	memcpy(out, sig, 8);
	n = 8;
	for (i = 0; i < 3; i++)
	{
		u32 crc;

		out[n + 0] = (u8)(lengths[i] >> 24);
		out[n + 1] = (u8)(lengths[i] >> 16);
		out[n + 2] = (u8)(lengths[i] >> 8);
		out[n + 3] = (u8)lengths[i];
		memcpy(&out[n + 4], types[i], 4);
		if (i == 0)
		{
			const u8 ihdr[13] = {(u8)(w >> 24), (u8)(w >> 16), (u8)(w >> 8), (u8)w, (u8)(h >> 24), (u8)(h >> 16), (u8)(h >> 8), (u8)h,
			                     (u8)bitDepth, (u8)colorType, 0, 0, 0};

			memcpy(&out[n + 8], ihdr, 13);
		}
		crc = RldPng_Crc(&out[n + 4], 4u + lengths[i]);
		out[n + 8 + lengths[i] + 0] = (u8)(crc >> 24);
		out[n + 8 + lengths[i] + 1] = (u8)(crc >> 16);
		out[n + 8 + lengths[i] + 2] = (u8)(crc >> 8);
		out[n + 8 + lengths[i] + 3] = (u8)crc;
		n += 12u + lengths[i];
	}
	return n;
}

// The views of the BIN chunk, in this order; HrTest_Glb lays them out 4-aligned.
enum
{
	HRV_POS = 0,
	HRV_NRM,
	HRV_UV,
	HRV_JNT,
	HRV_WGT,
	HRV_IDX,
	HRV_IBM,
	HRV_AIN,
	HRV_AOUT,
	HRV_AOUTC,
	HRV_WPOS,
	HRV_WNRM,
	HRV_IMG,
	HRV_COUNT
};

#define HRTEST_JSON_MAX 32768
#define HRTEST_BIN_MAX 8192

// The GLB of a spec: malloc'ed, *sizeOut its size. NULL when out of memory.
//
// Nodes: 0 "body" (mesh 0, the skin), 1 .. J the joints ("joint_0" the root,
// the others its children), then the four wheels (mesh 1, at the retail-like
// middles (+-0.5, 0.25, +-0.5)), then the extras of the spec. The scene holds
// the body, the root joint and the wheels. Every clip turns the root joint
// (the body without a skin).
static u8 *HrTest_Glb(const struct HrTestSpec *sp, u32 *sizeOut)
{
	static char json[HRTEST_JSON_MAX];
	static u8 bin[HRTEST_BIN_MAX];
	u32 off[HRV_COUNT];
	u32 len[HRV_COUNT];
	const int skins = (sp->skins < 0) ? 0 : (sp->skins == 0) ? 1 : sp->skins;
	const int joints = (sp->joints > 0) ? sp->joints : 2;
	const float e = (sp->extent > 0.0f) ? sp->extent : 1.0f;
	const u32 pngW = (sp->pngW != 0u) ? sp->pngW : 3u;
	const u32 pngH = (sp->pngH != 0u) ? sp->pngH : 2u;
	const int indexCount = (sp->indexCount > 0) ? sp->indexCount : 3;
	const int ibmCount = sp->fewMatrices ? (joints - 1) : joints;
	const int wheelBase = 1 + joints;
	int nodeCount = wheelBase + RLDHIRES_WHEEL_COUNT;
	u32 at = 0;
	u32 binSize;
	int i;
	int k;
	u8 *glb;
	u32 jsonPad;
	u32 total;

	memset(bin, 0, sizeof(bin));
	json[0] = '\0';

	// The BIN chunk.
	len[HRV_POS] = 36;
	len[HRV_NRM] = 36;
	len[HRV_UV] = 24;
	len[HRV_JNT] = 12;
	len[HRV_WGT] = 48;
	len[HRV_IDX] = 2u * (u32)indexCount;
	len[HRV_IBM] = 64u * (u32)((ibmCount > 0) ? ibmCount : 1);
	len[HRV_AIN] = 8;
	len[HRV_AOUT] = 32;
	len[HRV_AOUTC] = 96;
	len[HRV_WPOS] = 36;
	len[HRV_WNRM] = 36;
	len[HRV_IMG] = 0;
	for (i = 0; i < HRV_COUNT; i++)
	{
		off[i] = at;
		if (i == HRV_IMG)
		{
			u8 *img = &bin[at];

			switch (sp->image)
			{
			case 1:
				img[0] = 0xFFu;
				img[1] = 0xD8u;
				img[2] = 0xFFu;
				img[3] = 0xE0u;
				len[i] = 16;
				break;
			case 2:
				memcpy(img, "GIF89a\x03\x00\x02\x00", 10);
				len[i] = 12;
				break;
			case 3:
				len[i] = HrTest_Png(img, pngW, pngH, 3u, 8u);
				break;
			case 6:
				len[i] = HrTest_Png(img, pngW, pngH, 6u, 8u);
				memcpy(&img[12], "IDAT", 4); // the first chunk is no IHDR
				break;
			default:
				len[i] = HrTest_Png(img, pngW, pngH, 6u, 8u);
				break;
			}
		}
		at += (len[i] + 3u) & ~3u;
	}
	binSize = at;

	// Body triangle: (0,0,0), (e,0,0), (0,e,0); normals +Z; UVs; joint 0, weight 1.
	HrTest_PutF(&bin[off[HRV_POS] + 12], e);
	HrTest_PutF(&bin[off[HRV_POS] + 28], e);
	if (sp->nanPosition)
	{
		const u32 nan = 0x7FC00000u;

		RldHires_PutLE32(&bin[off[HRV_POS] + 16], nan);
	}
	for (i = 0; i < 3; i++)
	{
		HrTest_PutF(&bin[off[HRV_NRM] + 12u * (u32)i + 8u], 1.0f);
		HrTest_PutF(&bin[off[HRV_WGT] + 16u * (u32)i], 1.0f);
		HrTest_PutF(&bin[off[HRV_WNRM] + 12u * (u32)i], 1.0f);
	}
	HrTest_PutF(&bin[off[HRV_UV] + 8], 1.0f);
	HrTest_PutF(&bin[off[HRV_UV] + 20], 1.0f);
	if (sp->badJoint)
	{
		bin[off[HRV_JNT] + 8] = (u8)joints;
	}
	for (i = 0; i < indexCount; i++)
	{
		RldHires_PutLE16(&bin[off[HRV_IDX] + 2u * (u32)i], (u32)(i % 3));
	}
	for (i = 0; i < ibmCount; i++)
	{
		for (k = 0; k < 4; k++)
		{
			HrTest_PutF(&bin[off[HRV_IBM] + 64u * (u32)i + 20u * (u32)k], 1.0f);
		}
	}
	HrTest_PutF(&bin[off[HRV_AIN] + 4], 1.0f);
	HrTest_PutF(&bin[off[HRV_AOUT] + 12], 1.0f);
	HrTest_PutF(&bin[off[HRV_AOUT] + 28], 1.0f);
	for (i = 0; i < 6; i++)
	{
		HrTest_PutF(&bin[off[HRV_AOUTC] + 16u * (u32)i + 12u], 1.0f);
	}
	// Wheel triangle: radius 0.25 about the X axis.
	HrTest_PutF(&bin[off[HRV_WPOS] + 16], 0.25f);
	HrTest_PutF(&bin[off[HRV_WPOS] + 32], 0.25f);

	// The JSON.
	HrTest_Append(json, sizeof(json), "{\"asset\":{\"version\":\"2.0\"}");
	if (sp->jsonBroken)
	{
		HrTest_Append(json, sizeof(json), ",\"nodes\":[{\"name\":");
	}
	if (sp->required != NULL)
	{
		HrTest_Append(json, sizeof(json), ",\"extensionsUsed\":[\"%s\"],\"extensionsRequired\":[\"%s\"]", sp->required, sp->required);
	}
	if (sp->light)
	{
		HrTest_Append(json, sizeof(json), ",\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[{\"type\":\"point\"}]}}");
	}
	if (sp->camera)
	{
		HrTest_Append(json, sizeof(json), ",\"cameras\":[{\"type\":\"perspective\",\"perspective\":{\"yfov\":1.0,\"znear\":0.1}}]");
	}

	HrTest_Append(json, sizeof(json), ",\"buffers\":[{\"byteLength\":%u}", (unsigned)binSize);
	if (sp->bufferUri == 1)
	{
		HrTest_Append(json, sizeof(json), ",{\"uri\":\"outside.bin\",\"byteLength\":4}");
	}
	else if (sp->bufferUri == 2)
	{
		HrTest_Append(json, sizeof(json), ",{\"uri\":\"data:application/octet-stream;base64,AAAAAA==\",\"byteLength\":4}");
	}
	HrTest_Append(json, sizeof(json), "]");

	HrTest_Append(json, sizeof(json), ",\"bufferViews\":[");
	for (i = 0; i < HRV_COUNT; i++)
	{
		HrTest_Append(json, sizeof(json), "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", (i != 0) ? "," : "", (unsigned)off[i],
		              (unsigned)((len[i] != 0u) ? len[i] : 4u));
	}
	HrTest_Append(json, sizeof(json), "]");

	// Accessors 0..11 (by view; the image has none).
	HrTest_Append(json, sizeof(json), ",\"accessors\":[");
	if (sp->sparse)
	{
		HrTest_Append(json, sizeof(json),
		              "{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":%d,\"componentType\":5121},\"values\":{\"bufferView\":%d}}}",
		              HRV_POS, HRV_JNT, HRV_NRM);
	}
	else
	{
		// wrapCount: 0x15555556 x 12 bytes wraps to 8 in a 32-bit cgltf_size and would pass cgltf_validate.
		HrTest_Append(json, sizeof(json), "{\"bufferView\":%d,\"componentType\":5126,\"count\":%u,\"type\":\"%s\"}", HRV_POS,
		              sp->wrapCount ? 0x15555556u : 3u, sp->floatPosition2 ? "VEC2" : "VEC3");
	}
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}", HRV_NRM);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}", HRV_UV);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"}", HRV_JNT);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"}", HRV_WGT);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5123,\"count\":%d,\"type\":\"SCALAR\"}", HRV_IDX, indexCount);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"MAT4\"}", HRV_IBM,
	              (ibmCount > 0) ? ibmCount : 1);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}", HRV_AIN);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}", HRV_AOUT);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":6,\"type\":\"VEC4\"}", HRV_AOUTC);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}", HRV_WPOS);
	HrTest_Append(json, sizeof(json), ",{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}", HRV_WNRM);
	HrTest_Append(json, sizeof(json), "]");

	// Image, texture, material.
	if (sp->image == 4)
	{
		HrTest_Append(json, sizeof(json), ",\"images\":[{\"name\":\"skin\",\"uri\":\"skin.png\"}]");
	}
	else
	{
		HrTest_Append(json, sizeof(json), ",\"images\":[{\"name\":\"skin\",\"bufferView\":%d,\"mimeType\":\"%s\"}]", HRV_IMG,
		              (sp->image == 1) ? "image/jpeg" : "image/png");
	}
	if (sp->unusedImage)
	{
		char *end = json + strlen(json) - 1u; // the ']' of images

		*end = '\0';
		HrTest_Append(json, sizeof(json), ",{\"name\":\"spare\",\"bufferView\":%d,\"mimeType\":\"image/jpeg\"}]", HRV_AOUT);
	}
	HrTest_Append(json, sizeof(json), (sp->image == 5) ? ",\"textures\":[{}]" : ",\"textures\":[{\"source\":0}]");
	HrTest_Append(json, sizeof(json), ",\"materials\":[{\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}%s%s%s}]",
	              sp->blend ? ",\"alphaMode\":\"BLEND\"" : "", sp->emissive ? ",\"emissiveFactor\":[1,0,0]" : "",
	              ((sp->required != NULL) && (strcmp(sp->required, "KHR_materials_unlit") == 0)) ? ",\"extensions\":{\"KHR_materials_unlit\":{}}" : "");

	// Meshes: 0 the body, 1 the wheel.
	HrTest_Append(json, sizeof(json), ",\"meshes\":[{\"name\":\"body\",\"primitives\":[{\"attributes\":{");
	{
		int first = 1;

		if (!sp->noPosition)
		{
			HrTest_Append(json, sizeof(json), "\"POSITION\":0");
			first = 0;
		}
		if (!sp->noNormal)
		{
			HrTest_Append(json, sizeof(json), "%s\"NORMAL\":1", first ? "" : ",");
			first = 0;
		}
		HrTest_Append(json, sizeof(json), "%s\"TEXCOORD_0\":2", first ? "" : ",");
		if ((skins > 0) && !sp->noJoints)
		{
			HrTest_Append(json, sizeof(json), ",\"JOINTS_0\":3");
		}
		if (skins > 0)
		{
			HrTest_Append(json, sizeof(json), ",\"WEIGHTS_0\":4");
		}
	}
	HrTest_Append(json, sizeof(json), "},\"indices\":5,\"material\":0%s", sp->morph ? ",\"targets\":[{\"POSITION\":1}]" : "");
	if (sp->mode != 0)
	{
		HrTest_Append(json, sizeof(json), ",\"mode\":%d", sp->mode);
	}
	HrTest_Append(json, sizeof(json), "}]%s}", sp->morph ? ",\"weights\":[0]" : "");
	HrTest_Append(json, sizeof(json), ",{\"name\":\"wheel\",\"primitives\":[{\"attributes\":{\"POSITION\":10,\"NORMAL\":11},\"material\":0}]}]");

	// Skins.
	if (skins > 0)
	{
		HrTest_Append(json, sizeof(json), ",\"skins\":[");
		for (k = 0; k < skins; k++)
		{
			HrTest_Append(json, sizeof(json), "%s{\"inverseBindMatrices\":6,\"joints\":[", (k != 0) ? "," : "");
			for (i = 0; i < joints; i++)
			{
				HrTest_Append(json, sizeof(json), "%s%d", (i != 0) ? "," : "", 1 + i);
			}
			HrTest_Append(json, sizeof(json), "]}");
		}
		HrTest_Append(json, sizeof(json), "]");
	}

	// Nodes.
	HrTest_Append(json, sizeof(json), ",\"nodes\":[");
	if (sp->noBody)
	{
		HrTest_Append(json, sizeof(json), "{\"name\":\"body\"}");
	}
	else
	{
		HrTest_Append(json, sizeof(json), "{\"name\":\"body\",\"mesh\":0%s}", (skins > 0) ? ",\"skin\":0" : "");
	}
	for (i = 0; i < joints; i++)
	{
		HrTest_Append(json, sizeof(json), ",{\"name\":\"joint_%d\"", i);
		if ((i == 0) && (joints > 1))
		{
			HrTest_Append(json, sizeof(json), ",\"children\":[");
			for (k = 1; k < joints; k++)
			{
				HrTest_Append(json, sizeof(json), "%s%d", (k != 1) ? "," : "", 1 + k);
			}
			HrTest_Append(json, sizeof(json), "]");
		}
		HrTest_Append(json, sizeof(json), "}");
	}
	for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
	{
		const float x = ((i & 1) == 0) ? 0.5f : -0.5f;
		const float z = (i < 2) ? 0.5f : -0.5f;
		const int noMesh = (sp->wheelNoMesh && (i == RLDHIRES_WHEEL_RR)) || ((sp->wheelsMissing >> i) & 1); // a missing wheel: an empty "spare" node
		const int skinned = sp->wheelSkinned && (i == RLDHIRES_WHEEL_RL) && (skins > 0);

		HrTest_Append(json, sizeof(json), ",{\"name\":\"%s\"%s%s,\"translation\":[%g,0.25,%g]%s}",
		              ((sp->wheelsMissing >> i) & 1) ? "spare" : s_rldHiresWheelName[i], noMesh ? "" : ",\"mesh\":1", skinned ? ",\"skin\":0" : "",
		              (double)x, (double)z, (sp->wheelChild && (i == 0)) ? ",\"children\":[__CHILD__]" : "");
	}
	if (sp->wheelChild)
	{
		char *mark = strstr(json, "__CHILD__");
		char number[16];

		snprintf(number, sizeof(number), "%9d", nodeCount);
		memcpy(mark, number, 9); // the same width, so the JSON keeps its length
		HrTest_Append(json, sizeof(json), ",{\"name\":\"hub\",\"mesh\":1}");
		nodeCount++;
	}
	if (sp->twiceWheel)
	{
		HrTest_Append(json, sizeof(json), ",{\"name\":\"wheel_fl\",\"mesh\":1}");
		nodeCount++;
	}
	HrTest_Append(json, sizeof(json), "]");

	// The scene.
	HrTest_Append(json, sizeof(json), ",\"scene\":0,\"scenes\":[{\"nodes\":[0,1");
	for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
	{
		HrTest_Append(json, sizeof(json), ",%d", wheelBase + i);
	}
	if (sp->twiceWheel)
	{
		HrTest_Append(json, sizeof(json), ",%d", nodeCount - 1);
	}
	HrTest_Append(json, sizeof(json), "]}]");

	// Animations: one per clip written, turning the root joint (or the body).
	HrTest_Append(json, sizeof(json), ",\"animations\":[");
	{
		int first = 1;
		const int target = (skins > 0) ? 1 : 0;

		for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
		{
			const int cubic = sp->cubic && (i == RLDHIRES_CLIP_IDLE);

			if ((sp->clipsMissing >> i) & 1)
			{
				continue;
			}
			HrTest_Append(json, sizeof(json),
			              "%s{\"name\":\"%s\",\"samplers\":[{\"input\":7,\"output\":%d,\"interpolation\":\"%s\"}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":%d,\"path\":\"rotation\"}}]}",
			              first ? "" : ",", s_rldHiresClipName[i], cubic ? 9 : 8, cubic ? "CUBICSPLINE" : "LINEAR", target);
			first = 0;
		}
		if (sp->unknownAnim)
		{
			HrTest_Append(json, sizeof(json),
			              "%s{\"name\":\"Walk\",\"samplers\":[{\"input\":7,\"output\":8}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":%d,\"path\":\"rotation\"}}]}",
			              first ? "" : ",", target);
			first = 0;
		}
		if (sp->twiceIdle)
		{
			HrTest_Append(json, sizeof(json),
			              "%s{\"name\":\"idle\",\"samplers\":[{\"input\":7,\"output\":8}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":%d,\"path\":\"rotation\"}}]}",
			              first ? "" : ",", target);
		}
	}
	HrTest_Append(json, sizeof(json), "]}");

	// The GLB: header, JSON (spaces), BIN.
	{
		const u32 jl = (u32)strlen(json);

		jsonPad = (jl + 3u) & ~3u;
		total = 12u + 8u + jsonPad + 8u + binSize;
		glb = (u8 *)calloc(1, total);
		if (glb == NULL)
		{
			return NULL;
		}
		RldHires_PutLE32(&glb[0], sp->glbMagic ? 0x46546C66u : 0x46546C67u);
		RldHires_PutLE32(&glb[4], (sp->glbVersion != 0) ? (u32)sp->glbVersion : 2u);
		RldHires_PutLE32(&glb[8], total + (u32)sp->glbLength);
		RldHires_PutLE32(&glb[12], jsonPad);
		RldHires_PutLE32(&glb[16], 0x4E4F534Au);
		memset(&glb[20], ' ', jsonPad);
		memcpy(&glb[20], json, jl);
		RldHires_PutLE32(&glb[20 + jsonPad], binSize);
		RldHires_PutLE32(&glb[24 + jsonPad], 0x004E4942u);
		memcpy(&glb[28 + jsonPad], bin, binSize);
	}
	*sizeOut = total;
	return glb;
}

// Opens the GLB of a spec. Returns RldHires_Open's answer; rep is cleared first.
// The model is closed again unless keep is given.
static int HrTest_Open(const struct HrTestSpec *sp, RldHiresReport *rep, RldHiresModel *keep, u8 **glbKeep)
{
	RldHiresModel m;
	u32 size = 0;
	u8 *glb = HrTest_Glb(sp, &size);
	int ok;

	RldHires_ReportClear(rep);
	if (glb == NULL)
	{
		HrTest_Check(0, "out of memory building a test GLB");
		return 0;
	}
	ok = RldHires_Open(glb, size, &m, rep);
	if (keep != NULL)
	{
		*keep = m;
		*glbKeep = glb;
		return ok;
	}
	RldHires_Close(&m);
	free(glb);
	return ok;
}

// The spec must fire rule (and refuse when the rule is an error), and the base
// model must not: the negative and the positive check of one rule.
static void HrTest_Rule(const char *rule, const struct HrTestSpec *bad, const char *what)
{
	static RldHiresReport rep;
	struct HrTestSpec base;
	char line[160];
	int ok;

	memset(&base, 0, sizeof(base));
	HrTest_Open(&base, &rep, NULL, NULL);
	snprintf(line, sizeof(line), "%s: the base model does not fire it", rule);
	HrTest_Check(!RldHires_ReportHas(&rep, rule), line);

	ok = HrTest_Open(bad, &rep, NULL, NULL);
	snprintf(line, sizeof(line), "%s: %s fires it", rule, what);
	HrTest_Check(RldHires_ReportHas(&rep, rule), line);
	if (RldHires_RuleLevel(rule) == RLDHIRES_ERROR)
	{
		snprintf(line, sizeof(line), "%s: %s refuses the model", rule, what);
		HrTest_Check(!ok, line);
	}
	else
	{
		snprintf(line, sizeof(line), "%s: %s keeps the model usable", rule, what);
		HrTest_Check(ok, line);
	}
}

// A spec that must NOT fire rule (the positive side at a threshold).
static void HrTest_Clean(const char *rule, const struct HrTestSpec *sp, const char *what)
{
	static RldHiresReport rep;
	char line[160];

	HrTest_Open(sp, &rep, NULL, NULL);
	snprintf(line, sizeof(line), "%s: %s does not fire it", rule, what);
	HrTest_Check(!RldHires_ReportHas(&rep, rule), line);
}

static int RldHires_SelfTest(void)
{
	static RldHiresReport rep;
	struct HrTestSpec sp;
	RldHiresModel m;
	u8 *glb = NULL;
	u32 glbSize = 0;
	int i;

	s_hrChecks = 0;
	s_hrFailures = 0;
	printf("rldpack hires self-test\n");

	// THE BASE MODEL: usable, no message at all, counted right.
	memset(&sp, 0, sizeof(sp));
	HrTest_Check(HrTest_Open(&sp, &rep, &m, &glb) == 1, "base model usable");
	HrTest_Check((rep.count == 0) && (rep.errors == 0) && (rep.warnings == 0) && (rep.notes == 0), "base model: no message");
	if (rep.count != 0)
	{
		for (i = 0; i < rep.count; i++)
		{
			printf("       %s %s: %s\n", rep.msg[i].rule, RldHires_LevelName(rep.msg[i].level), rep.msg[i].text);
		}
	}
	HrTest_Check(m.drawNodeCount == 1 && m.drawNodes[0] == 0 && m.skinnedNodeCount == 1, "base model: the body is node 0, skinned");
	HrTest_Check(m.skin == 0 && m.joints == 2, "base model: one skin, 2 joints");
	HrTest_Check(m.triangles == 5u && m.vertices == 15u && m.primitives == 5u, "base model: 5 triangles, 15 vertices (body + 4 wheels)");
	HrTest_Check(m.wheelNode[0] == 3 && m.wheelNode[1] == 4 && m.wheelNode[2] == 5 && m.wheelNode[3] == 6, "base model: wheel nodes 3..6");
	HrTest_Check(fabsf(m.wheelRadius[0] - 0.25f) < 1e-6f, "base model: wheel radius 0.25 m");
	for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
	{
		HrTest_Check(m.clipAnim[i] == i && m.clipSeconds[i] == 1.0f, "base model: every clip present, 1 s");
	}
	HrTest_Check(m.imageCount == 1 && m.images[0].isPng && m.images[0].fileWidth == 3u && m.images[0].fileHeight == 2u &&
	                 m.images[0].colorType == 6u && m.images[0].width == 16u && m.images[0].height == 16u && m.images[0].mip0Bytes == 1024u,
	             "base model: 3x2 RGBA PNG, held as 16x16 (1024 bytes)");
	HrTest_Check(m.textureBytes == 1024u && m.textureEdgeMax == 16u, "base model: texture budget counts");
	HrTest_Check(m.boundsMin[0] == -0.5f && m.boundsMax[0] == 1.0f && m.boundsMin[1] == 0.0f && m.boundsMax[1] == 1.0f &&
	                 m.boundsMin[2] == -0.5f && m.boundsMax[2] == 0.75f,
	             "base model: bind pose bounds");
	HrTest_Check(RldHires_CheckBudget(&m, &rep) == 1 && rep.errors == 0, "base model: within the budget");
	{
		u32 n = 0;
		const u8 *png = RldHires_ImageBytes(&m, 0, &n);

		HrTest_Check(png != NULL && n == m.images[0].pngSize && png[1] == 'P', "RldHires_ImageBytes: the PNG inside the GLB");
	}
	RldHires_Close(&m);
	HrTest_Check(m.data == NULL && m.drawNodes == NULL && m.images == NULL, "RldHires_Close clears the model");

	// DETERMINISM: the same GLB gives the same report and counts.
	{
		static RldHiresReport a;
		static RldHiresReport b;

		memset(&sp, 0, sizeof(sp));
		sp.clipsMissing = 0x0FF0;
		sp.noNormal = 1;
		sp.blend = 1;
		HrTest_Open(&sp, &a, NULL, NULL);
		HrTest_Open(&sp, &b, NULL, NULL);
		HrTest_Check(a.count > 0 && memcmp(&a, &b, sizeof(a)) == 0, "the same GLB gives the same report");
	}
	free(glb);
	glb = NULL;

	// THE CHUNK (HRG-1..5).
	{
		u8 *chunk;
		size_t size = 0;
		RldHiresChunk c;
		const char *rule = NULL;
		char detail[RLDHIRES_TEXT_MAX];

		memset(&sp, 0, sizeof(sp));
		glb = HrTest_Glb(&sp, &glbSize);
		chunk = RldHires_BuildChunk(glb, glbSize, &size);
		HrTest_Check(chunk != NULL && size == glbSize + 16u, "BuildChunk: 16 bytes + the GLB");
		if (chunk != NULL)
		{
			static const u8 head[12] = {1, 0, 16, 0, 0, 0, 0, 0, 16, 0, 0, 0};

			HrTest_Check(memcmp(chunk, head, 12) == 0 && RldHires_LE32(&chunk[12]) == glbSize && memcmp(&chunk[16], glb, glbSize) == 0,
			             "BuildChunk: version 1, header 16, flags 0, offset 16, size, the GLB unchanged");
			HrTest_Check(RldHires_ParseChunk(chunk, size, &c, &rule, detail, (int)sizeof(detail)) == 1 && rule == NULL && c.glb == chunk + 16 &&
			                 c.glbSize == glbSize && c.version == 1u && c.headerBytes == 16u && c.flags == 0u,
			             "ParseChunk: the built chunk");
			{
				u8 *again;
				size_t size2 = 0;

				again = RldHires_BuildChunk(glb, glbSize, &size2);
				HrTest_Check(again != NULL && size2 == size && memcmp(again, chunk, size) == 0, "BuildChunk: the same GLB gives the same bytes");
				free(again);
			}

			// HRG-1: version.
			RldHires_PutLE16(&chunk[0], 2u);
			HrTest_Check(RldHires_ParseChunk(chunk, size, &c, &rule, detail, (int)sizeof(detail)) == 0 && rule != NULL && strcmp(rule, "HRG-1") == 0,
			             "HRG-1: version 2 refused");
			RldHires_PutLE16(&chunk[0], 1u);

			// HRG-2: header size.
			RldHires_PutLE16(&chunk[2], 12u);
			HrTest_Check(RldHires_ParseChunk(chunk, size, &c, &rule, NULL, 0) == 0 && strcmp(rule, "HRG-2") == 0, "HRG-2: header 12 refused");
			RldHires_PutLE16(&chunk[2], 18u);
			HrTest_Check(RldHires_ParseChunk(chunk, size, &c, &rule, NULL, 0) == 0 && strcmp(rule, "HRG-2") == 0, "HRG-2: header 18 refused");
			HrTest_Check(RldHires_ParseChunk(chunk, 15u, &c, &rule, NULL, 0) == 0 && strcmp(rule, "HRG-2") == 0, "HRG-2: a 15-byte chunk refused");
			RldHires_PutLE16(&chunk[2], 16u);

			// HRG-3: flags.
			RldHires_PutLE32(&chunk[4], 1u);
			HrTest_Check(RldHires_ParseChunk(chunk, size, &c, &rule, NULL, 0) == 0 && strcmp(rule, "HRG-3") == 0, "HRG-3: flag bit 0 refused");
			RldHires_PutLE32(&chunk[4], 0u);

			// HRG-4: offset and size.
			RldHires_PutLE32(&chunk[8], 20u);
			HrTest_Check(RldHires_ParseChunk(chunk, size, &c, &rule, NULL, 0) == 0 && strcmp(rule, "HRG-4") == 0, "HRG-4: glbOffset 20 refused");
			RldHires_PutLE32(&chunk[8], 16u);
			RldHires_PutLE32(&chunk[12], glbSize + 1u);
			HrTest_Check(RldHires_ParseChunk(chunk, size, &c, &rule, NULL, 0) == 0 && strcmp(rule, "HRG-4") == 0, "HRG-4: glbSize one too many refused");
			RldHires_PutLE32(&chunk[12], glbSize);

			// A longer header of a newer writer is read past (version 1, header 20).
			{
				u8 *longer = (u8 *)calloc(1, size + 4u);

				if (longer != NULL)
				{
					memcpy(longer, chunk, 16);
					memcpy(longer + 20, chunk + 16, glbSize);
					RldHires_PutLE16(&longer[2], 20u);
					RldHires_PutLE32(&longer[8], 20u);
					HrTest_Check(RldHires_ParseChunk(longer, size + 4u, &c, &rule, NULL, 0) == 1 && c.glb == longer + 20,
					             "HRG-2/4: a 20-byte header is skipped to the GLB");
					free(longer);
				}
			}

			// HRG-5: the chunk in the envelope.
			HrTest_Check(RldHires_ParseChunk(chunk, (size_t)RLDHIRES_CHUNK_LIMIT + 1u, &c, &rule, NULL, 0) == 0 && strcmp(rule, "HRG-5") == 0,
			             "HRG-5: a chunk over 64 MiB refused");
			{
				u8 entry[RLD_DIR_ENTRY_SIZE];

				memset(entry, 0, sizeof(entry));
				memcpy(entry, "CHRG", 4);
				Rld_WriteLE64(&entry[0x10], (u64)size);
				Rld_WriteLE64(&entry[0x18], (u64)size);
				HrTest_Check(RldHires_EntryCheck(entry) == NULL, "HRG-5: a stored CHRG entry passes");
				Rld_WriteLE32(&entry[0x04], 1u);
				HrTest_Check(RldHires_EntryCheck(entry) != NULL, "HRG-5: a compressed CHRG refused");
				Rld_WriteLE32(&entry[0x04], 0u);
				Rld_WriteLE64(&entry[0x10], (u64)size - 1u);
				HrTest_Check(RldHires_EntryCheck(entry) != NULL, "HRG-5: size_stored != size_raw refused");
				Rld_WriteLE64(&entry[0x10], (u64)RLDHIRES_CHUNK_LIMIT + 1u);
				Rld_WriteLE64(&entry[0x18], (u64)RLDHIRES_CHUNK_LIMIT + 1u);
				HrTest_Check(RldHires_EntryCheck(entry) != NULL, "HRG-5: a CHRG over 64 MiB refused");
				Rld_WriteLE64(&entry[0x10], (u64)RLDHIRES_CHUNK_LIMIT);
				Rld_WriteLE64(&entry[0x18], (u64)RLDHIRES_CHUNK_LIMIT);
				HrTest_Check(RldHires_EntryCheck(entry) == NULL, "HRG-5: exactly 64 MiB passes");
			}
			free(chunk);
		}
		free(glb);
		glb = NULL;
	}

	// THE GLB HEADER (HRG-6) and its parse (HRG-7).
	memset(&sp, 0, sizeof(sp));
	sp.glbMagic = 1;
	HrTest_Rule("HRG-6", &sp, "a wrong magic");
	memset(&sp, 0, sizeof(sp));
	sp.glbVersion = 1;
	HrTest_Rule("HRG-6", &sp, "GLB version 1");
	memset(&sp, 0, sizeof(sp));
	sp.glbLength = 4;
	HrTest_Rule("HRG-6", &sp, "a length past the bytes");
	memset(&sp, 0, sizeof(sp));
	sp.jsonBroken = 1;
	HrTest_Rule("HRG-7", &sp, "broken JSON");
	memset(&sp, 0, sizeof(sp));
	sp.indexCount = 6;
	{
		// An index past the vertices: cgltf_validate refuses it.
		u32 size = 0;
		u8 *g = HrTest_Glb(&sp, &size);

		if (g != NULL)
		{
			RldHires_ReportClear(&rep);
			{
				// The JSON says 6 indices; write a 9 into the last one in the BIN chunk.
				const u32 jsonPad = RldHires_LE32(&g[12]);
				const u32 binAt = 28u + jsonPad;
				u32 off = 0;
				int v;

				for (v = 0; v < HRV_IDX; v++)
				{
					static const u32 lens[HRV_IDX] = {36, 36, 24, 12, 48};

					off += (lens[v] + 3u) & ~3u;
				}
				RldHires_PutLE16(&g[binAt + off + 10u], 9u);
			}
			HrTest_Check(RldHires_Open(g, size, &m, &rep) == 0 && RldHires_ReportHas(&rep, "HRG-7"), "HRG-7: an index past the vertices refused (validate)");
			RldHires_Close(&m);
			free(g);
		}
	}
	memset(&sp, 0, sizeof(sp));
	sp.indexCount = 6;
	HrTest_Clean("HRG-7", &sp, "6 indices in range");
	memset(&sp, 0, sizeof(sp));
	sp.wrapCount = 1;
	HrTest_Rule("HRG-7", &sp, "an accessor count that wraps 32 bits");

	// HRG-8: extensionsRequired.
	memset(&sp, 0, sizeof(sp));
	sp.required = "KHR_draco_mesh_compression";
	HrTest_Rule("HRG-8", &sp, "Draco required");
	memset(&sp, 0, sizeof(sp));
	sp.required = "KHR_materials_unlit";
	HrTest_Clean("HRG-8", &sp, "KHR_materials_unlit required");

	// HRG-9: outside buffers and images.
	memset(&sp, 0, sizeof(sp));
	sp.bufferUri = 1;
	HrTest_Rule("HRG-9", &sp, "an outside buffer");
	memset(&sp, 0, sizeof(sp));
	sp.bufferUri = 2;
	HrTest_Rule("HRG-9", &sp, "a data URI buffer");
	memset(&sp, 0, sizeof(sp));
	sp.image = 4;
	HrTest_Rule("HRG-9", &sp, "an outside image");

	// HRG-10: triangles.
	memset(&sp, 0, sizeof(sp));
	sp.mode = 1;
	HrTest_Rule("HRG-10", &sp, "lines");
	memset(&sp, 0, sizeof(sp));
	sp.indexCount = 4;
	HrTest_Rule("HRG-10", &sp, "4 indices");

	// HRG-11: POSITION.
	memset(&sp, 0, sizeof(sp));
	sp.noPosition = 1;
	HrTest_Rule("HRG-11", &sp, "no POSITION");
	memset(&sp, 0, sizeof(sp));
	sp.floatPosition2 = 1;
	HrTest_Rule("HRG-11", &sp, "POSITION as VEC2");

	// HRG-12, HRG-13: skins and joints.
	memset(&sp, 0, sizeof(sp));
	sp.skins = 2;
	HrTest_Rule("HRG-12", &sp, "two skins");
	memset(&sp, 0, sizeof(sp));
	sp.skins = -1;
	HrTest_Clean("HRG-12", &sp, "no skin");
	memset(&sp, 0, sizeof(sp));
	sp.joints = 65;
	HrTest_Rule("HRG-13", &sp, "65 joints");
	memset(&sp, 0, sizeof(sp));
	sp.joints = 64;
	HrTest_Clean("HRG-13", &sp, "64 joints");
	memset(&sp, 0, sizeof(sp));
	sp.joints = 3;
	sp.fewMatrices = 1;
	HrTest_Rule("HRG-13", &sp, "one inverse bind matrix short");

	// HRG-14: JOINTS_0 / WEIGHTS_0.
	memset(&sp, 0, sizeof(sp));
	sp.noJoints = 1;
	HrTest_Rule("HRG-14", &sp, "a skinned mesh without JOINTS_0");
	memset(&sp, 0, sizeof(sp));
	sp.badJoint = 1;
	HrTest_Rule("HRG-14", &sp, "joint 2 of a 2-joint skin");

	// HRG-15: normals.
	memset(&sp, 0, sizeof(sp));
	sp.noNormal = 1;
	HrTest_Rule("HRG-15", &sp, "no NORMAL");

	// HRG-16: PNG only.
	memset(&sp, 0, sizeof(sp));
	sp.image = 1;
	HrTest_Rule("HRG-16", &sp, "a JPEG");
	memset(&sp, 0, sizeof(sp));
	sp.image = 2;
	HrTest_Rule("HRG-16", &sp, "a GIF");
	memset(&sp, 0, sizeof(sp));
	sp.image = 5;
	HrTest_Rule("HRG-16", &sp, "a texture without image");
	memset(&sp, 0, sizeof(sp));
	sp.image = 6;
	HrTest_Rule("HRG-16", &sp, "a PNG without IHDR");
	memset(&sp, 0, sizeof(sp));
	sp.unusedImage = 1;
	HrTest_Clean("HRG-16", &sp, "a second, unused image that is no PNG");
	HrTest_Open(&sp, &rep, &m, &glb);
	HrTest_Check(m.imageCount == 2 && m.images[0].used == 1 && m.images[1].used == 0 && m.textureBytes == 1024u, "an unused image is not counted");
	RldHires_Close(&m);
	free(glb);
	glb = NULL;
	memset(&sp, 0, sizeof(sp));
	sp.image = 1;
	HrTest_Open(&sp, &rep, NULL, NULL);
	HrTest_Check(rep.count >= 1 && strstr(rep.msg[0].text, "JPEG") != NULL && strstr(rep.msg[0].text, "PNG") != NULL, "HRG-16: the JPEG message names JPEG and PNG");

	// HRG-17: the edge.
	memset(&sp, 0, sizeof(sp));
	sp.pngW = 2049;
	HrTest_Rule("HRG-17", &sp, "a 2049-wide PNG");
	memset(&sp, 0, sizeof(sp));
	sp.pngW = 2048;
	sp.pngH = 2048;
	HrTest_Clean("HRG-17", &sp, "a 2048x2048 PNG");

	// HRG-18: the bind pose (wheels reach x = -0.5, so legs of 3.5 m make 4.0 m).
	memset(&sp, 0, sizeof(sp));
	sp.extent = 3.51f;
	HrTest_Rule("HRG-18", &sp, "4.01 m");
	memset(&sp, 0, sizeof(sp));
	sp.extent = 3.5f;
	HrTest_Clean("HRG-18", &sp, "exactly 4.0 m");
	memset(&sp, 0, sizeof(sp));
	sp.nanPosition = 1;
	HrTest_Rule("HRG-18", &sp, "a NaN position");

	// HRG-19: missing clips and their fallbacks.
	memset(&sp, 0, sizeof(sp));
	sp.clipsMissing = 1 << RLDHIRES_CLIP_HIT;
	HrTest_Rule("HRG-19", &sp, "no hit");
	HrTest_Open(&sp, &rep, NULL, NULL);
	HrTest_Check(rep.count == 1 && strcmp(rep.msg[0].text, "clip 'hit' is missing - the kart uses 'idle' instead") == 0, "HRG-19: the hit message");
	memset(&sp, 0, sizeof(sp));
	sp.clipsMissing = 1 << RLDHIRES_CLIP_DRIFT_LEFT;
	HrTest_Open(&sp, &rep, NULL, NULL);
	HrTest_Check(rep.count == 1 && strcmp(rep.msg[0].text, "clip 'drift_left' is missing - the kart uses 'steer_left' at full lock instead") == 0,
	             "HRG-19: the drift message says full lock");
	memset(&sp, 0, sizeof(sp));
	sp.clipsMissing = 0x0FFF;
	HrTest_Open(&sp, &rep, NULL, NULL);
	HrTest_Check(rep.warnings == 12 && strcmp(rep.msg[rep.count - 1].text, "clip 'menu' is missing - the kart keeps its rest pose") == 0,
	             "HRG-19: no clip at all: 12 warnings, the rest pose");

	// The chains themselves (section 4 of the format).
	{
		static const signed char want[RLDHIRES_CLIP_COUNT] = {-1, 0, 0, 0, 1, 2, 0, 0, 7, 8, 0, 0};
		static const u8 play[RLDHIRES_CLIP_COUNT] = {0, 1, 1, 2, 0, 0, 2, 0, 0, 0, 0, 0};
		RldHiresModel fake;
		int via = 0;
		int lock = 0;

		for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
		{
			HrTest_Check(RldHires_ClipFallback(i) == want[i], "ClipFallback: the table of the format");
			HrTest_Check(RldHires_ClipPlay(i) == play[i], "ClipPlay: the table of the format");
			HrTest_Check(RldHires_ClipByName(RldHires_ClipName(i)) == i, "ClipByName: every name");
		}
		HrTest_Check(RldHires_ClipFallback(-1) == -1 && RldHires_ClipFallback(12) == -1 && RldHires_ClipByName("Idle") == -1, "clips outside the list");
		memset(&fake, 0, sizeof(fake));
		for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
		{
			fake.clipAnim[i] = -1;
		}
		HrTest_Check(RldHires_ClipResolve(&fake, RLDHIRES_CLIP_PODIUM_3, &via, &lock) == -1 && via == 1 && lock == 0, "ClipResolve: nothing -> rest pose");
		fake.clipAnim[RLDHIRES_CLIP_IDLE] = 0;
		HrTest_Check(RldHires_ClipResolve(&fake, RLDHIRES_CLIP_PODIUM_3, &via, &lock) == RLDHIRES_CLIP_IDLE && via == 1 && lock == 0,
		             "ClipResolve: podium_3 -> podium_2 -> podium_1 -> idle");
		fake.clipAnim[RLDHIRES_CLIP_PODIUM_1] = 1;
		HrTest_Check(RldHires_ClipResolve(&fake, RLDHIRES_CLIP_PODIUM_3, &via, &lock) == RLDHIRES_CLIP_PODIUM_1 && via == 1, "ClipResolve: podium_3 -> podium_1");
		HrTest_Check(RldHires_ClipResolve(&fake, RLDHIRES_CLIP_DRIFT_RIGHT, &via, &lock) == RLDHIRES_CLIP_IDLE && lock == 0,
		             "ClipResolve: drift_right without steer_right -> idle, no full lock");
		fake.clipAnim[RLDHIRES_CLIP_STEER_RIGHT] = 2;
		HrTest_Check(RldHires_ClipResolve(&fake, RLDHIRES_CLIP_DRIFT_RIGHT, &via, &lock) == RLDHIRES_CLIP_STEER_RIGHT && via == 1 && lock == 1,
		             "ClipResolve: drift_right -> steer_right at full lock");
		HrTest_Check(RldHires_ClipResolve(&fake, RLDHIRES_CLIP_STEER_RIGHT, &via, &lock) == RLDHIRES_CLIP_STEER_RIGHT && via == 0 && lock == 0,
		             "ClipResolve: steer_right itself, no full lock");
		fake.clipAnim[RLDHIRES_CLIP_DRIFT_RIGHT] = 3;
		HrTest_Check(RldHires_ClipResolve(&fake, RLDHIRES_CLIP_DRIFT_RIGHT, &via, &lock) == RLDHIRES_CLIP_DRIFT_RIGHT && via == 0 && lock == 0,
		             "ClipResolve: drift_right itself");
		HrTest_Check(RldHires_ClipResolve(NULL, RLDHIRES_CLIP_IDLE, NULL, NULL) == -1 && RldHires_ClipResolve(&fake, 99, NULL, NULL) == -1,
		             "ClipResolve: no model, a clip outside the list");
	}

	// HRG-20: the wheels, all or none.
	memset(&sp, 0, sizeof(sp));
	sp.wheelsMissing = 1 << RLDHIRES_WHEEL_RR;
	HrTest_Rule("HRG-20", &sp, "3 of 4 wheels");
	HrTest_Open(&sp, &rep, &m, &glb);
	HrTest_Check(m.wheelNode[0] == -1 && m.wheelNode[1] == -1 && m.wheelNode[2] == -1 && m.wheelNode[3] == -1 && m.drawNodeCount == 1 &&
	                 m.triangles == 1u,
	             "HRG-20: 3 of 4: no wheel used, the found ones are not drawn");
	HrTest_Check(rep.count == 1 && strstr(rep.msg[0].text, "only 3 of 4") != NULL && strstr(rep.msg[0].text, "wheel_rr missing") != NULL,
	             "HRG-20: the message names wheel_rr");
	RldHires_Close(&m);
	free(glb);
	glb = NULL;
	memset(&sp, 0, sizeof(sp));
	sp.wheelsMissing = 0xF;
	HrTest_Rule("HRG-20", &sp, "no wheels");
	memset(&sp, 0, sizeof(sp));
	sp.wheelNoMesh = 1;
	HrTest_Rule("HRG-20", &sp, "wheel_rr without a mesh");
	memset(&sp, 0, sizeof(sp));
	sp.wheelSkinned = 1;
	HrTest_Rule("HRG-20", &sp, "a skinned wheel_rl");

	// HRG-21: names.
	memset(&sp, 0, sizeof(sp));
	sp.unknownAnim = 1;
	HrTest_Rule("HRG-21", &sp, "an animation 'Walk'");
	memset(&sp, 0, sizeof(sp));
	sp.twiceIdle = 1;
	HrTest_Rule("HRG-21", &sp, "'idle' twice");

	// HRG-22: CUBICSPLINE.
	memset(&sp, 0, sizeof(sp));
	sp.cubic = 1;
	HrTest_Rule("HRG-22", &sp, "a CUBICSPLINE idle");

	// HRG-23: morph targets.
	memset(&sp, 0, sizeof(sp));
	sp.morph = 1;
	HrTest_Rule("HRG-23", &sp, "a morph target");

	// HRG-24: cameras and lights.
	memset(&sp, 0, sizeof(sp));
	sp.camera = 1;
	HrTest_Rule("HRG-24", &sp, "a camera");
	memset(&sp, 0, sizeof(sp));
	sp.light = 1;
	HrTest_Rule("HRG-24", &sp, "a light");

	// HRG-25, HRG-26: materials.
	memset(&sp, 0, sizeof(sp));
	sp.blend = 1;
	HrTest_Rule("HRG-25", &sp, "alpha BLEND");
	memset(&sp, 0, sizeof(sp));
	sp.emissive = 1;
	HrTest_Rule("HRG-26", &sp, "an emissive factor");

	// HRG-27: the budget - counts set directly, at and one past each limit.
	{
		RldHiresModel b;

		memset(&b, 0, sizeof(b));
		b.triangles = RLDHIRES_BUDGET_TRIANGLES;
		b.textureEdgeMax = RLDHIRES_BUDGET_TEXTURE_EDGE;
		b.textureBytes = RLDHIRES_BUDGET_TEXTURE_BYTES;
		RldHires_ReportClear(&rep);
		HrTest_Check(RldHires_CheckBudget(&b, &rep) == 1 && rep.count == 0, "HRG-27: exactly at every limit passes");
		b.triangles = RLDHIRES_BUDGET_TRIANGLES + 1u;
		HrTest_Check(RldHires_CheckBudget(&b, &rep) == 0 && RldHires_ReportHas(&rep, "HRG-27") && rep.errors == 1, "HRG-27: one triangle over");
		{
			char want[96];

			snprintf(want, sizeof(want), "over budget: %u triangles, the limit is %u", (unsigned)(RLDHIRES_BUDGET_TRIANGLES + 1u), (unsigned)RLDHIRES_BUDGET_TRIANGLES);
			HrTest_Check(strcmp(rep.msg[0].text, want) == 0, "HRG-27: the triangle message");
		}
		b.triangles = RLDHIRES_BUDGET_TRIANGLES;
		b.textureEdgeMax = RLDHIRES_BUDGET_TEXTURE_EDGE * 2u;
		RldHires_ReportClear(&rep);
		HrTest_Check(RldHires_CheckBudget(&b, &rep) == 0 && rep.errors == 1, "HRG-27: a texture edge over");
		b.textureEdgeMax = RLDHIRES_BUDGET_TEXTURE_EDGE;
		b.textureBytes = (u64)RLDHIRES_BUDGET_TEXTURE_BYTES + 1u;
		RldHires_ReportClear(&rep);
		HrTest_Check(RldHires_CheckBudget(&b, &rep) == 0 && rep.errors == 1, "HRG-27: one texture byte over");
	}

	// HRG-28: nothing to draw.
	memset(&sp, 0, sizeof(sp));
	sp.noBody = 1;
	HrTest_Rule("HRG-28", &sp, "a body node without mesh");

	// HRG-29: sparse.
	memset(&sp, 0, sizeof(sp));
	sp.sparse = 1;
	HrTest_Rule("HRG-29", &sp, "a sparse POSITION");

	// HRG-30: PNG kinds.
	memset(&sp, 0, sizeof(sp));
	sp.image = 3;
	HrTest_Rule("HRG-30", &sp, "a palette PNG");

	// HRG-31: wheel details.
	memset(&sp, 0, sizeof(sp));
	sp.wheelChild = 1;
	HrTest_Rule("HRG-31", &sp, "a child under wheel_fl");
	HrTest_Open(&sp, &rep, &m, &glb);
	HrTest_Check(m.drawNodeCount == 1 && m.triangles == 5u, "HRG-31: the child of a wheel is not drawn");
	RldHires_Close(&m);
	free(glb);
	glb = NULL;
	memset(&sp, 0, sizeof(sp));
	sp.twiceWheel = 1;
	HrTest_Rule("HRG-31", &sp, "wheel_fl twice");

	// Every rule of the table has a level and a known id; every rule fired above.
	for (i = 0; i < RLDHIRES_RULE_COUNT; i++)
	{
		char id[8];

		snprintf(id, sizeof(id), "HRG-%d", i + 1);
		HrTest_Check(strcmp(s_rldHiresRules[i].id, id) == 0 && RldHires_RuleLevel(id) == s_rldHiresRules[i].level, "the rule table is HRG-1..31 in order");
	}

	// UpEdge: the game's rule (EngineStatic_UpEdge).
	HrTest_Check(RldHires_UpEdge(1) == 16u && RldHires_UpEdge(16) == 16u && RldHires_UpEdge(17) == 32u && RldHires_UpEdge(800) == 1024u &&
	                 RldHires_UpEdge(2048) == 2048u && RldHires_UpEdge(2049) == 0u && RldHires_UpEdge(0) == 0u && RldHires_UpEdge(0xFFFFFFFFu) == 0u,
	             "UpEdge: 1, 16, 17, 800, 2048, 2049, 0, u32 max");

	// A REPORT keeps at most RLDHIRES_REPORT_MAX messages and counts the rest.
	RldHires_ReportClear(&rep);
	for (i = 0; i < RLDHIRES_REPORT_MAX + 3; i++)
	{
		RldHires_Say(&rep, "HRG-24", "note %d", i);
	}
	HrTest_Check(rep.count == RLDHIRES_REPORT_MAX && rep.dropped == 3 && rep.notes == RLDHIRES_REPORT_MAX + 3, "the report keeps 64 and counts the rest");

	// A CONTAINER WITHOUT CHRG READS EXACTLY AS BEFORE.
	{
		struct RldContainer before;
		struct RldContainer after;
		const u8 info[32] = {1, 2, 3, 4, 5, 6, 7, 8};
		const u8 model[48] = {9, 8, 7, 6, 5, 4, 3, 2, 1};
		u8 *a;
		u8 *b;
		size_t sizeA = 0;
		size_t sizeB = 0;
		u8 *chunk;
		size_t chunkSize = 0;

		memset(&sp, 0, sizeof(sp));
		glb = HrTest_Glb(&sp, &glbSize);
		chunk = RldHires_BuildChunk(glb, glbSize, &chunkSize);

		memset(&before, 0, sizeof(before));
		memset(&after, 0, sizeof(after));
		Rld_AddChunk(&before, "CHRI", info, sizeof(info), RLD_COMPRESSION_NONE);
		Rld_AddChunk(&before, "CMDL", model, sizeof(model), RLD_COMPRESSION_NONE);
		Rld_AddChunk(&after, "CHRI", info, sizeof(info), RLD_COMPRESSION_NONE);
		Rld_AddChunk(&after, "CMDL", model, sizeof(model), RLD_COMPRESSION_NONE);
		Rld_AddChunk(&after, RLDHIRES_CHUNK_TYPE, chunk, chunkSize, RLD_COMPRESSION_NONE);
		a = Rld_WriteEnvelope(&s_rldCharFormat, 0u, &before, &sizeA);
		b = Rld_WriteEnvelope(&s_rldCharFormat, 0u, &after, &sizeB);

		HrTest_Check(RldChar_ChunkLimit((const u8 *)RLDHIRES_CHUNK_TYPE) == 0u, "CHRG is no known type of the envelope (skipped like any unknown chunk)");
		if ((a != NULL) && (b != NULL))
		{
			const u32 countA = Rld_ReadLE32(&a[0x10]);
			const u32 countB = Rld_ReadLE32(&b[0x10]);
			const u64 dirA = Rld_ReadLE64(&a[0x18]);
			const u64 dirB = Rld_ReadLE64(&b[0x18]);
			u32 e;
			int sameOthers = 1;
			int chrgInA = 0;
			int chrgHashed = 0;

			HrTest_Check(countA == 2u && countB == 3u && Rld_ReadLE32(&a[0x0c]) == 0u && Rld_ReadLE32(&b[0x0c]) == 0u,
			             "CHRG adds one entry and no header flag");
			for (e = 0; e < countA; e++)
			{
				const u8 *ea = &a[(size_t)dirA + (size_t)e * RLD_DIR_ENTRY_SIZE];
				const u8 *eb = &b[(size_t)dirB + (size_t)e * RLD_DIR_ENTRY_SIZE];
				const u64 oa = Rld_ReadLE64(&ea[0x08]);
				const u64 ob = Rld_ReadLE64(&eb[0x08]);
				const u64 na = Rld_ReadLE64(&ea[0x10]);

				chrgInA |= (memcmp(ea, RLDHIRES_CHUNK_TYPE, 4) == 0);
				if ((memcmp(ea, eb, 4) != 0) || (memcmp(&ea[0x10], &eb[0x10], 0x30) != 0) || (memcmp(&a[(size_t)oa], &b[(size_t)ob], (size_t)na) != 0))
				{
					sameOthers = 0;
				}
			}
			for (e = 0; e < countB; e++)
			{
				const u8 *eb = &b[(size_t)dirB + (size_t)e * RLD_DIR_ENTRY_SIZE];

				if (memcmp(eb, RLDHIRES_CHUNK_TYPE, 4) == 0)
				{
					u8 digest[32];

					Sha256(&b[(size_t)Rld_ReadLE64(&eb[0x08])], (size_t)Rld_ReadLE64(&eb[0x18]), digest);
					chrgHashed = (RldHires_EntryCheck(eb) == NULL) && (memcmp(digest, &eb[RLD_DIR_HASH_OFFSET], 32) == 0);
				}
			}
			HrTest_Check(!chrgInA, "a container without CHRG has no CHRG entry (the hi-res reader finds none)");
			HrTest_Check(sameOthers, "adding CHRG leaves CHRI and CMDL byte for byte, their sizes and hashes the same");
			HrTest_Check(chrgHashed, "the CHRG entry is stored, sized and hashed like every chunk");
			HrTest_Check((sizeA == 40u + 32u + 48u + 2u * 64u) && (memcmp(a, b, 8) == 0), "the container without CHRG is the size it always was");
		}
		else
		{
			HrTest_Check(0, "out of memory writing the test containers");
		}
		free(a);
		free(b);
		for (i = 0; i < before.chunkCount; i++)
		{
			free(before.chunks[i].stored);
		}
		for (i = 0; i < after.chunkCount; i++)
		{
			free(after.chunks[i].stored);
		}
		free(chunk);
		free(glb);
		glb = NULL;
	}

	if (s_hrFailures == 0)
	{
		printf("  ok   hires: %d checks (HRG-1..%d each fired and not fired, clip chains, wheels, budget, CHRG in the envelope)\n", s_hrChecks,
		       RLDHIRES_RULE_COUNT);
	}
	printf("hires selftest %s: %d checks, %d failures\n", (s_hrFailures == 0) ? "passed" : "FAILED", s_hrChecks, s_hrFailures);
	return s_hrFailures != 0;
}
