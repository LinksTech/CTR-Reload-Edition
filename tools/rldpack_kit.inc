// rldpack_kit.inc - the Creator Kit on the command line: kit-extract,
// kit-standin, selftest-kit
//
// Included by tools/rldpack.c after rldpack_char.inc (the BIGFILE reader
// RldMk_BigEntry, the PNG writer RldMk_PngRgbaPacked of rldpack_native.inc) and
// rldpack_hires.inc (RldHiresPack_Write, RldHires_OpenParts for the check). The
// kart itself is made by include/rldkit.inc; this file finds the game data,
// writes the files and reports.
//
//   kit-extract [--game <folder | ctr-u.bin | BIGFILE.BIG>] [--driver 0..14]
//               [--segments 1|2|4|8] [--upscale 1|2|4] -o <folder>
//       reads the player's own game data (without --game: assets\BIGFILE.BIG or
//       assets\ctr-u.bin next to this program or up to two folders above it,
//       as the game looks) and writes into the existing <folder>:
//         kit-kart.glb        the kart of the driver: two root nodes, kart_body
//                             (the retail kart, smoothed, its textures upscaled)
//                             and kart_steer_wheel (made new at the retail ring,
//                             origin at the ring's centre, no rotation); vertex
//                             colours PSX (grey 128 neutral): pack with
//                             --vertex-colors psx. The wheels are kit-wheel.glb,
//                             placed at "wheels" of kit-info.json (the generator,
//                             templates/make_creator_kit.py, puts them there)
//         kit-kart-gltf.glb   the same with the colours for glTF mode (the PS1
//                             factor through include/rldhires_color.inc, at most 1)
//         kit-wheel.glb       one wheel (axle +X, outer side +X, centre at the
//                             origin), PSX; kit-wheel-gltf.glb for glTF mode
//         kit-info.json       measures in model units and the files' SHA-256, in
//                             the format templates/make_creator_kit.py reads (THE INFO)
//         kit-extract.txt     the report with the checks
//       Every node, mesh and material carries the kit mark (extras "ctr_kit",
//       material names "ctrkit_*"): rldpack hires-check, the game and Reload
//       Studio say HRG-41 for any model made from them. The files are made from
//       the player's game data: they stay on this computer (never share them).
//       Exit 0 = written, 1 = an error (nothing half written: each file goes
//       through a .part file), 2 = usage.
//   kit-standin -o <folder>
//       writes <folder>\BIGFILE.BIG made in code (no game data): 15 stand-in
//       racer models with one box kart, a moving figure and a steering ring,
//       shared.vrm with their tiles and two wheel sprites, the shared pack with
//       icon group 0. kit-extract --game <folder> reads it like the real one -
//       for tests (ctest, Reload Studio) on machines without the game.
//   selftest-kit
//       the stand-in through include/rldkit.inc and the writers, in memory.

#include <rldkit.inc>

#define RLDKIT_TOOL_FILES 6
#define RLDKIT_ENTRY_MAX (4u * 1024u * 1024u)

//========================================================================================
// A GROWABLE BUFFER
//========================================================================================

struct RldKitBuf
{
	u8 *p;
	size_t len;
	size_t cap;
	int fail;
};

static void RldKitBuf_Put(struct RldKitBuf *b, const void *data, size_t n)
{
	if (b->fail || (n == 0u))
	{
		return;
	}
	if (b->len + n > b->cap)
	{
		size_t cap = (b->cap != 0u) ? b->cap : 4096u;
		u8 *np;

		while (cap < b->len + n)
		{
			cap *= 2u;
		}
		np = (u8 *)realloc(b->p, cap);
		if (np == NULL)
		{
			b->fail = 1;
			return;
		}
		b->p = np;
		b->cap = cap;
	}
	memcpy(b->p + b->len, data, n);
	b->len += n;
}

static void RldKitBuf_Printf(struct RldKitBuf *b, const char *format, ...)
{
	char text[1024];
	va_list args;
	int n;

	va_start(args, format);
	n = vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	if ((n < 0) || ((size_t)n >= sizeof(text)))
	{
		b->fail = 1;
		return;
	}
	RldKitBuf_Put(b, text, (size_t)n);
}

static void RldKitBuf_Align4(struct RldKitBuf *b, u8 fill)
{
	while ((b->len & 3u) != 0u)
	{
		RldKitBuf_Put(b, &fill, 1u);
	}
}

static void RldKitBuf_U32(struct RldKitBuf *b, u32 v)
{
	u8 x[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)};

	RldKitBuf_Put(b, x, 4u);
}

static void RldKitBuf_U16(struct RldKitBuf *b, u32 v)
{
	u8 x[2] = {(u8)v, (u8)(v >> 8)};

	RldKitBuf_Put(b, x, 2u);
}

static void RldKitBuf_F32(struct RldKitBuf *b, float f)
{
	u32 v;

	memcpy(&v, &f, 4u);
	RldKitBuf_U32(b, v);
}

static void RldKitBuf_Free(struct RldKitBuf *b)
{
	free(b->p);
	memset(b, 0, sizeof(*b));
}

//========================================================================================
// THE GLB WRITER
//========================================================================================

// The vertex colour of a kit vertex as COLOR_0 (linear) for a file's mode: an
// untextured mesh shows its colour as it is in every mode (the sRGB byte); a
// textured one in PSX mode holds the PS1 byte, in glTF mode the PS1 factor
// (include/rldhires_color.inc RldHiresColor_Factor), at most 1.
static float RldKitGlb_Colour(u8 byte, int textured, int vcolMode)
{
	const float linear = RldHiresColor_Decode((float)byte / 255.0f);

	if (textured && (vcolMode == RLDHIRES_VCOL_GLTF))
	{
		const float f = RldHiresColor_Factor(RLDHIRES_VCOL_PSX, linear);

		return (f < 1.0f) ? f : 1.0f;
	}
	return linear;
}

struct RldKitGlb
{
	struct RldKitBuf bin;
	struct RldKitBuf views;
	struct RldKitBuf accs;
	u32 nViews;
	u32 nAccs;
};

static u32 RldKitGlb_View(struct RldKitGlb *g, u32 offset, u32 length)
{
	RldKitBuf_Printf(&g->views, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", (g->nViews != 0u) ? "," : "", (unsigned)offset, (unsigned)length);
	return g->nViews++;
}

// One mesh's accessors: POSITION, NORMAL, (TEXCOORD_0), COLOR_0, indices.
// ids[0..4]; ids[2] = -1 without UVs.
static void RldKitGlb_Mesh(struct RldKitGlb *g, const RldKitMesh *m, int vcolMode, int ids[5])
{
	float lo[3] = {1e30f, 1e30f, 1e30f};
	float hi[3] = {-1e30f, -1e30f, -1e30f};
	u32 at;
	u32 i;
	int k;
	int a;

	for (i = 0; i < m->vertices; i++)
	{
		for (k = 0; k < 3; k++)
		{
			lo[k] = (m->v[i].p[k] < lo[k]) ? m->v[i].p[k] : lo[k];
			hi[k] = (m->v[i].p[k] > hi[k]) ? m->v[i].p[k] : hi[k];
		}
	}
	for (a = 0; a < 5; a++)
	{
		ids[a] = -1;
		if ((a == 2) && !m->textured)
		{
			continue;
		}
		RldKitBuf_Align4(&g->bin, 0);
		at = (u32)g->bin.len;
		for (i = 0; i < ((a == 4) ? m->indices : m->vertices); i++)
		{
			const RldKitVertex *v = &m->v[(a == 4) ? 0u : i];

			switch (a)
			{
			case 0:
				RldKitBuf_F32(&g->bin, v->p[0]);
				RldKitBuf_F32(&g->bin, v->p[1]);
				RldKitBuf_F32(&g->bin, v->p[2]);
				break;
			case 1:
				RldKitBuf_F32(&g->bin, v->n[0]);
				RldKitBuf_F32(&g->bin, v->n[1]);
				RldKitBuf_F32(&g->bin, v->n[2]);
				break;
			case 2:
				RldKitBuf_F32(&g->bin, v->uv[0]);
				RldKitBuf_F32(&g->bin, v->uv[1]);
				break;
			case 3:
				RldKitBuf_F32(&g->bin, RldKitGlb_Colour(v->c[0], m->textured, vcolMode));
				RldKitBuf_F32(&g->bin, RldKitGlb_Colour(v->c[1], m->textured, vcolMode));
				RldKitBuf_F32(&g->bin, RldKitGlb_Colour(v->c[2], m->textured, vcolMode));
				break;
			default:
				RldKitBuf_U32(&g->bin, m->index[i]);
				break;
			}
		}
		{
			const u32 view = RldKitGlb_View(g, at, (u32)g->bin.len - at);

			if (a == 0)
			{
				RldKitBuf_Printf(&g->accs, "%s{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]}",
				                 (g->nAccs != 0u) ? "," : "", (unsigned)view, (unsigned)m->vertices, (double)lo[0], (double)lo[1], (double)lo[2], (double)hi[0],
				                 (double)hi[1], (double)hi[2]);
			}
			else
			{
				RldKitBuf_Printf(&g->accs, "%s{\"bufferView\":%u,\"componentType\":%u,\"count\":%u,\"type\":\"%s\"}", (g->nAccs != 0u) ? "," : "", (unsigned)view,
				                 (a == 4) ? 5125u : 5126u, (unsigned)((a == 4) ? m->indices : m->vertices), (a == 2) ? "VEC2" : ((a == 4) ? "SCALAR" : "VEC3"));
			}
			ids[a] = (int)g->nAccs++;
		}
	}
}

static void RldKitGlb_Primitive(struct RldKitBuf *json, const char *name, const char *mark, const int ids[5], int material)
{
	RldKitBuf_Printf(json, "{\"name\":\"%s\",\"extras\":{\"ctr_kit\":\"%s\"},\"primitives\":[{\"mode\":4,\"material\":%d,\"indices\":%d,\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,",
	                 name, mark, material, ids[4], ids[0], ids[1]);
	if (ids[2] >= 0)
	{
		RldKitBuf_Printf(json, "\"TEXCOORD_0\":%d,", ids[2]);
	}
	RldKitBuf_Printf(json, "\"COLOR_0\":%d}}]}", ids[3]);
}

static void RldKitGlb_Material(struct RldKitBuf *json, const char *name, const char *mark, int texture, int mask, int doubleSided)
{
	RldKitBuf_Printf(json, "{\"name\":\"%s\",\"extras\":{\"ctr_kit\":\"%s\"},\"pbrMetallicRoughness\":{", name, mark);
	if (texture >= 0)
	{
		RldKitBuf_Printf(json, "\"baseColorTexture\":{\"index\":%d},", texture);
	}
	RldKitBuf_Printf(json, "\"metallicFactor\":0,\"roughnessFactor\":1},\"alphaMode\":\"%s\"%s,\"doubleSided\":%s}", mask ? "MASK" : "OPAQUE",
	                 mask ? ",\"alphaCutoff\":0.5" : "", doubleSided ? "true" : "false");
}

// The GLB bytes from JSON and BIN (both padded: JSON with spaces, BIN with 0).
static u8 *RldKitGlb_Pack(struct RldKitBuf *json, struct RldKitBuf *bin, size_t *sizeOut)
{
	struct RldKitBuf out;

	memset(&out, 0, sizeof(out));
	RldKitBuf_Align4(json, ' ');
	RldKitBuf_Align4(bin, 0);
	RldKitBuf_U32(&out, 0x46546C67u);
	RldKitBuf_U32(&out, 2u);
	RldKitBuf_U32(&out, (u32)(12u + 8u + json->len + 8u + bin->len));
	RldKitBuf_U32(&out, (u32)json->len);
	RldKitBuf_U32(&out, 0x4E4F534Au);
	RldKitBuf_Put(&out, json->p, json->len);
	RldKitBuf_U32(&out, (u32)bin->len);
	RldKitBuf_U32(&out, 0x004E4942u);
	RldKitBuf_Put(&out, bin->p, bin->len);
	if (out.fail || json->fail || bin->fail)
	{
		RldKitBuf_Free(&out);
		*sizeOut = 0;
		return NULL;
	}
	*sizeOut = out.len;
	return out.p;
}

// kit-kart.glb (wholeKart 1) or kit-wheel.glb (wholeKart 0) in a vertex colour
// mode. malloc'ed, NULL on no memory.
static u8 *RldKit_WriteGlb(const RldKitResult *r, int wholeKart, int vcolMode, size_t *sizeOut)
{
	struct RldKitGlb g;
	struct RldKitBuf json;
	const char *suffix = (vcolMode == RLDHIRES_VCOL_PSX) ? "psx" : "gltf";
	int kartIds[5];
	int steerIds[5];
	int wheelIds[5];
	int imageView = -1;
	u8 *glb;
	int k;

	memset(&g, 0, sizeof(g));
	memset(&json, 0, sizeof(json));
	if (wholeKart)
	{
		u8 *png;
		size_t pngSize = 0;

		RldKitGlb_Mesh(&g, &r->kart, vcolMode, kartIds);
		RldKitGlb_Mesh(&g, &r->steer, vcolMode, steerIds);
		png = RldMk_PngRgbaPacked(r->atlas, r->atlasW, r->atlasH, &pngSize);
		if (png == NULL)
		{
			RldKitBuf_Free(&g.bin);
			RldKitBuf_Free(&g.views);
			RldKitBuf_Free(&g.accs);
			return NULL;
		}
		RldKitBuf_Align4(&g.bin, 0);
		imageView = (int)RldKitGlb_View(&g, (u32)g.bin.len, (u32)pngSize);
		RldKitBuf_Put(&g.bin, png, pngSize);
		free(png);
	}
	if (!wholeKart)
	{
		RldKitGlb_Mesh(&g, &r->wheel, vcolMode, wheelIds);
		// K2b: the wheel's own atlas (the two sprites) when it has one.
		if (r->wheel.textured && (r->wheelAtlas != NULL))
		{
			size_t pngSize = 0;
			u8 *png = RldMk_PngRgbaPacked(r->wheelAtlas, r->wheelAtlasW, r->wheelAtlasH, &pngSize);

			if (png == NULL)
			{
				RldKitBuf_Free(&g.bin);
				RldKitBuf_Free(&g.views);
				RldKitBuf_Free(&g.accs);
				return NULL;
			}
			RldKitBuf_Align4(&g.bin, 0);
			imageView = (int)RldKitGlb_View(&g, (u32)g.bin.len, (u32)pngSize);
			RldKitBuf_Put(&g.bin, png, pngSize);
			free(png);
		}
	}

	RldKitBuf_Printf(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"rldpack kit-extract (CTR Reload %s) - made from your own game data, keep it on this computer\"},",
	                 CTR_NATIVE_VERSION);
	RldKitBuf_Printf(&json, "\"scene\":0,\"scenes\":[{\"nodes\":[%s]}],\"nodes\":[", wholeKart ? "0,1" : "0");
	if (wholeKart)
	{
		RldKitBuf_Printf(&json, "{\"name\":\"kart_body\",\"extras\":{\"ctr_kit\":\"kart\"},\"mesh\":0},");
		RldKitBuf_Printf(&json, "{\"name\":\"kart_steer_wheel\",\"extras\":{\"ctr_kit\":\"steer_wheel\"},\"mesh\":1,\"translation\":[%.9g,%.9g,%.9g]}",
		                 r->ringCenter[0], r->ringCenter[1], r->ringCenter[2]);
		RldKitBuf_Printf(&json, "],\"meshes\":[");
		RldKitGlb_Primitive(&json, "kart_body", "kart", kartIds, 0);
		RldKitBuf_Printf(&json, ",");
		RldKitGlb_Primitive(&json, "kart_steer_wheel", "steer_wheel", steerIds, 1);
		RldKitBuf_Printf(&json, "],\"materials\":[");
		{
			char name[48];

			snprintf(name, sizeof(name), "ctrkit_kart_%s", suffix);
			RldKitGlb_Material(&json, name, "kart", 0, r->atlasAlpha, 0);
			snprintf(name, sizeof(name), "ctrkit_steer_wheel_%s", suffix);
			RldKitBuf_Printf(&json, ",");
			RldKitGlb_Material(&json, name, "steer_wheel", -1, 0, 0);
		}
		RldKitBuf_Printf(&json, "],\"textures\":[{\"source\":0,\"sampler\":0}],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":33071,\"wrapT\":33071}],");
		RldKitBuf_Printf(&json, "\"images\":[{\"name\":\"ctrkit_kart_atlas\",\"bufferView\":%d,\"mimeType\":\"image/png\"}],", imageView);
	}
	else
	{
		char name[48];

		RldKitBuf_Printf(&json, "{\"name\":\"wheel\",\"extras\":{\"ctr_kit\":\"wheel\"},\"mesh\":0}],\"meshes\":[");
		RldKitGlb_Primitive(&json, "wheel", "wheel", wheelIds, 0);
		snprintf(name, sizeof(name), "ctrkit_wheel_%s", suffix);
		RldKitBuf_Printf(&json, "],\"materials\":[");
		RldKitGlb_Material(&json, name, "wheel", (imageView >= 0) ? 0 : -1, 0, 0);
		RldKitBuf_Printf(&json, "],");
		if (imageView >= 0)
		{
			RldKitBuf_Printf(&json, "\"textures\":[{\"source\":0,\"sampler\":0}],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":33071,\"wrapT\":33071}],");
			RldKitBuf_Printf(&json, "\"images\":[{\"name\":\"ctrkit_wheel_atlas\",\"bufferView\":%d,\"mimeType\":\"image/png\"}],", imageView);
		}
	}
	RldKitBuf_Printf(&json, "\"buffers\":[{\"byteLength\":%u}],\"bufferViews\":[", (unsigned)((g.bin.len + 3u) & ~(size_t)3u));
	RldKitBuf_Put(&json, g.views.p, g.views.len);
	RldKitBuf_Printf(&json, "],\"accessors\":[");
	RldKitBuf_Put(&json, g.accs.p, g.accs.len);
	RldKitBuf_Printf(&json, "]}");
	glb = RldKitGlb_Pack(&json, &g.bin, sizeOut);
	RldKitBuf_Free(&json);
	RldKitBuf_Free(&g.bin);
	RldKitBuf_Free(&g.views);
	RldKitBuf_Free(&g.accs);
	return glb;
}

//========================================================================================
// THE INFO AND THE REPORT
//========================================================================================

struct RldKitChecks
{
	double iou[4];
	double colourMean;
	double colourMax;
	u32 triangles;      // the kit part: kart + steering wheel + 4 wheels
	u64 textureBytes;   // the atlas as the game holds it (RGBA8 level 0), the wheel's included
	int wheelLook;      // 1 = measured (RldKit_WheelLook)
	RldKitWheelLook look;
};

static void RldKit_Checks(const RldKitResult *r, struct RldKitChecks *c)
{
	int v;

	memset(c, 0, sizeof(*c));
	for (v = 0; v < 4; v++)
	{
		c->iou[v] = RldKit_Iou(&r->flat, &r->kart, v, 512);
	}
	RldKit_ColourCheck(r, &c->colourMean, &c->colourMax);
	c->triangles = (r->kart.indices + r->steer.indices + 4u * r->wheel.indices) / 3u;
	c->textureBytes = (u64)r->atlasW * r->atlasH * 4u + (u64)r->wheelAtlasW * r->wheelAtlasH * 4u;
	c->wheelLook = RldKit_WheelLook(r, &c->look);
}

// THE INFO: kit-info.json in the kit format the generator reads
// (templates/make_creator_kit.py read_kit_info): model units (64 = 1 m), glTF
// axes; "kit" 1, "seat", "steer" {center, axis, normal, radius}, "kart_box"
// (x0 y0 z0 x1 y1 z1), "floor" ([z, y] pairs), "wheels" {wheel_fl ..},
// "wheel_radius", "wheel_half_width", "files" {name: {vcol, sha256,
// triangles}}; then what else the kit measured (grips, feet, the figure, the
// budget, the checks).
static void RldKitJson_U3(struct RldKitBuf *b, const double v[3], double scale)
{
	RldKitBuf_Printf(b, "[%.4f, %.4f, %.4f]", v[0] * scale, v[1] * scale, v[2] * scale);
}

static void RldKitJson_Sha(struct RldKitBuf *b, const u8 *bytes, size_t size)
{
	u8 d[32];
	int k;

	Sha256(bytes, size, d);
	for (k = 0; k < 32; k++)
	{
		RldKitBuf_Printf(b, "%02x", d[k]);
	}
}

static u8 *RldKit_WriteInfo(const RldKitResult *r, const RldKitOptions *opt, const struct RldKitChecks *c, u8 *const glb[4], const size_t glbSize[4], size_t *sizeOut)
{
	static const char *const wheelKey[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
	static const char *const fileName[4] = {"kit-kart.glb", "kit-kart-gltf.glb", "kit-wheel.glb", "kit-wheel-gltf.glb"};
	const double U = RLDKIT_UNITS_PER_METRE;
	const u32 kartTris = (r->kart.indices + r->steer.indices) / 3u;
	const u32 wheelTris = r->wheel.indices / 3u;
	struct RldKitBuf b;
	int k;

	memset(&b, 0, sizeof(b));
	RldKitBuf_Printf(&b, "{\n \"kit\": 1,\n \"source\": \"game data\",\n \"driver\": %d,\n \"model\": \"%s\",\n \"units_per_metre\": 64,\n", opt->driver, r->model);
	RldKitBuf_Printf(&b, " \"note\": \"made from your own game data - keep these files on this computer, never share them\",\n");
	RldKitBuf_Printf(&b, " \"tool\": \"rldpack %s kit-extract\",\n \"segments\": %d,\n \"upscale\": %d,\n", CTR_NATIVE_VERSION, opt->segments, opt->upscale);
	RldKitBuf_Printf(&b, " \"seat\": ");
	RldKitJson_U3(&b, r->seat, U);
	RldKitBuf_Printf(&b, ",\n \"steer\": {\"center\": ");
	RldKitJson_U3(&b, r->ringCenter, U);
	RldKitBuf_Printf(&b, ", \"axis\": ");
	RldKitJson_U3(&b, r->ringAxis, 1.0);
	RldKitBuf_Printf(&b, ", \"normal\": ");
	RldKitJson_U3(&b, r->ringNormal, 1.0);
	RldKitBuf_Printf(&b, ", \"radius\": %.4f, \"measured\": %s, \"grip_left\": ", r->ringRadius * U, r->ringMeasured ? "true" : "false");
	RldKitJson_U3(&b, r->gripLeft, U);
	RldKitBuf_Printf(&b, ", \"grip_right\": ");
	RldKitJson_U3(&b, r->gripRight, U);
	RldKitBuf_Printf(&b, ", \"retail_turn_deg\": 45.2},\n \"kart_box\": [%.4f, %.4f, %.4f, %.4f, %.4f, %.4f],\n \"floor\": [", r->kartMin[0] * U, r->kartMin[1] * U,
	                 r->kartMin[2] * U, r->kartMax[0] * U, r->kartMax[1] * U, r->kartMax[2] * U);
	for (k = 0; k < r->floorCount; k++)
	{
		RldKitBuf_Printf(&b, "%s[%.4f, %.4f]", (k != 0) ? ", " : "", r->floorLine[k][0], r->floorLine[k][1]);
	}
	RldKitBuf_Printf(&b, "],\n \"floor_under_seat\": %.4f,\n \"wheels\": {", r->floorY * U);
	for (k = 0; k < 4; k++)
	{
		RldKitBuf_Printf(&b, "%s\"%s\": ", (k != 0) ? ", " : "", wheelKey[k]);
		RldKitJson_U3(&b, r->wheelPos[k], U);
	}
	RldKitBuf_Printf(&b, "},\n \"wheel_radius\": %.4f,\n \"wheel_half_width\": %.4f,\n \"wheel_steer_max_deg\": %.4f,\n \"wheel_measured\": %s,\n", r->wheelRadius * U,
	                 0.5 * r->wheelWidth * U, r->steerMaxDeg, r->wheelMeasured ? "true" : "false");
	RldKitBuf_Printf(&b, " \"feet\": {\"left\": ");
	RldKitJson_U3(&b, r->footLeft, U);
	RldKitBuf_Printf(&b, ", \"right\": ");
	RldKitJson_U3(&b, r->footRight, U);
	RldKitBuf_Printf(&b, ", \"measured\": %s},\n \"driver_box\": [%.4f, %.4f, %.4f, %.4f, %.4f, %.4f],\n", r->feetMeasured ? "true" : "false", r->driverMin[0] * U,
	                 r->driverMin[1] * U, r->driverMin[2] * U, r->driverMax[0] * U, r->driverMax[1] * U, r->driverMax[2] * U);
	RldKitBuf_Printf(&b, " \"kit_mark\": {\"extras\": \"ctr_kit\", \"material_prefix\": \"ctrkit_\", \"rule\": \"HRG-41\"},\n");
	RldKitBuf_Printf(&b, " \"budget\": {\"triangles\": %u, \"triangles_left\": %u, \"texture_bytes\": %llu, \"texture_bytes_left\": %llu},\n", (unsigned)c->triangles,
	                 (unsigned)((c->triangles < RLDHIRES_BUDGET_TRIANGLES) ? (RLDHIRES_BUDGET_TRIANGLES - c->triangles) : 0u), (unsigned long long)c->textureBytes,
	                 (unsigned long long)((c->textureBytes < RLDHIRES_BUDGET_TEXTURE_BYTES) ? (RLDHIRES_BUDGET_TEXTURE_BYTES - c->textureBytes) : 0u));
	RldKitBuf_Printf(&b, " \"checks\": {\"max_deviation_units\": %.4f, \"iou\": [%.4f, %.4f, %.4f, %.4f], \"colour_mean\": %.3f, \"colour_max\": %.3f, \"open_edges\": %u, "
	                     "\"retail_triangles\": %u},\n",
	                 r->maxDeviation, c->iou[0], c->iou[1], c->iou[2], c->iou[3], c->colourMean, c->colourMax, (unsigned)r->openEdges, (unsigned)r->kartTriangles);
	if (c->wheelLook)
	{
		const RldKitWheelLook *l = &c->look;

		RldKitBuf_Printf(&b, " \"wheel_look\": {\"texture\": [%u, %u], \"worst\": %.2f, \"worst_flat\": %.2f, \"limit\": %.1f,\n", (unsigned)r->wheelAtlasW,
		                 (unsigned)r->wheelAtlasH, l->worst, l->worstFlat, RLDKIT_LOOK_LIMIT);
		RldKitBuf_Printf(&b, "  \"side\": {\"tyre\": [[%.1f, %.1f, %.1f], [%.1f, %.1f, %.1f]], \"rim\": [[%.1f, %.1f, %.1f], [%.1f, %.1f, %.1f]], "
		                     "\"hub\": [[%.1f, %.1f, %.1f], [%.1f, %.1f, %.1f]]},\n",
		                 l->side[0][0][0], l->side[0][0][1], l->side[0][0][2], l->side[0][1][0], l->side[0][1][1], l->side[0][1][2], l->side[1][0][0], l->side[1][0][1],
		                 l->side[1][0][2], l->side[1][1][0], l->side[1][1][1], l->side[1][1][2], l->side[2][0][0], l->side[2][0][1], l->side[2][0][2], l->side[2][1][0],
		                 l->side[2][1][1], l->side[2][1][2]);
		RldKitBuf_Printf(&b, "  \"tread\": {\"upper\": [[%.1f, %.1f, %.1f], [%.1f, %.1f, %.1f]], \"lower\": [[%.1f, %.1f, %.1f], [%.1f, %.1f, %.1f]]}},\n",
		                 l->tread[0][0][0], l->tread[0][0][1], l->tread[0][0][2], l->tread[0][1][0], l->tread[0][1][1], l->tread[0][1][2], l->tread[1][0][0],
		                 l->tread[1][0][1], l->tread[1][0][2], l->tread[1][1][0], l->tread[1][1][1], l->tread[1][1][2]);
	}
	RldKitBuf_Printf(&b, " \"files\": {");
	for (k = 0; k < 4; k++)
	{
		RldKitBuf_Printf(&b, "%s\n  \"%s\": {\"vcol\": \"%s\", \"sha256\": \"", (k != 0) ? "," : "", fileName[k], (k & 1) ? "gltf" : "psx");
		RldKitJson_Sha(&b, glb[k], glbSize[k]);
		RldKitBuf_Printf(&b, "\", \"triangles\": %u}", (unsigned)((k < 2) ? kartTris : wheelTris));
	}
	RldKitBuf_Printf(&b, "\n }\n}\n");
	if (b.fail)
	{
		RldKitBuf_Free(&b);
		return NULL;
	}
	*sizeOut = b.len;
	return b.p;
}

static u8 *RldKit_WriteReport(const RldKitResult *r, const RldKitOptions *opt, const struct RldKitChecks *c, const char *source, size_t *sizeOut)
{
	struct RldKitBuf b;

	memset(&b, 0, sizeof(b));
	RldKitBuf_Printf(&b, "rldpack kit-extract - the Creator Kit's kart from your own game data\n");
	RldKitBuf_Printf(&b, "These files are made from your game data. Keep them on this computer; never share them.\n\n");
	RldKitBuf_Printf(&b, "game data      %s\n", source);
	RldKitBuf_Printf(&b, "driver         %d (%s): %u commands, %u vertex records, %u triangles\n", opt->driver, r->model, (unsigned)r->commands, (unsigned)r->records,
	                 (unsigned)r->triangles);
	RldKitBuf_Printf(&b, "kart           %u retail triangles: %u still, %u shared with 3+ drivers, %u close (4 units) in 3+ drivers, %u the driver's own (figure, left out)\n",
	                 (unsigned)r->kartTriangles, (unsigned)r->stillTriangles, (unsigned)r->kartShared, (unsigned)r->kartLoose, (unsigned)r->dropped);
	RldKitBuf_Printf(&b, "smoothed       %u triangles (%d segments per edge), largest deviation %.3f units (limit %.1f), %u bulges limited, %u open edges kept, %u cockpit floor triangles also turned up\n",
	                 (unsigned)(r->kart.indices / 3u), opt->segments, r->maxDeviation, RLDKIT_BULGE_MAX, (unsigned)r->bulgeLimited, (unsigned)r->openEdges, (unsigned)r->floorDoubled);
	RldKitBuf_Printf(&b, "silhouette     IoU retail vs smoothed: front %.4f, side %.4f, top %.4f, three quarters %.4f\n", c->iou[0], c->iou[1], c->iou[2], c->iou[3]);
	RldKitBuf_Printf(&b, "textures       %d tiles, atlas %ux%u from %ux%u (Scale2x x%d), colour difference mean %.3f max %.3f (0..255)\n", r->tiles, (unsigned)r->atlasW,
	                 (unsigned)r->atlasH, (unsigned)r->sourceW, (unsigned)r->sourceH, opt->upscale, c->colourMean, c->colourMax);
	RldKitBuf_Printf(&b, "steering wheel %s: centre (%.4f, %.4f, %.4f) m, radius %.4f m, %u ring records, colour %u %u %u\n",
	                 r->ringMeasured ? "measured" : "the defaults", r->ringCenter[0], r->ringCenter[1], r->ringCenter[2], r->ringRadius, (unsigned)r->ringRecords,
	                 (unsigned)r->ring[0], (unsigned)r->ring[1], (unsigned)r->ring[2]);
	RldKitBuf_Printf(&b, "wheels         %s: radius %.4f m, width %.4f m, tyre %u %u %u, rim %u %u %u, hub %u %u %u, %u triangles each\n",
	                 r->wheelMeasured ? "from the sprites" : "the defaults", r->wheelRadius, r->wheelWidth, (unsigned)r->tyre[0], (unsigned)r->tyre[1],
	                 (unsigned)r->tyre[2], (unsigned)r->rim[0], (unsigned)r->rim[1], (unsigned)r->rim[2], (unsigned)r->hub[0], (unsigned)r->hub[1], (unsigned)r->hub[2],
	                 (unsigned)(r->wheel.indices / 3u));
	if (c->wheelLook)
	{
		const RldKitWheelLook *l = &c->look;
		static const char *const ring[3] = {"tyre", "rim ", "hub "};
		int k;

		RldKitBuf_Printf(&b, "wheel texture  the tread sprite round the tread, the hub sprite on the sides: atlas %ux%u (Scale2x x%d)\n", (unsigned)r->wheelAtlasW,
		                 (unsigned)r->wheelAtlasH, opt->upscale);
		for (k = 0; k < 3; k++)
		{
			RldKitBuf_Printf(&b, "wheel look     hub side %s: render %5.1f %5.1f %5.1f, sprite %5.1f %5.1f %5.1f, flat colour %5.1f %5.1f %5.1f\n", ring[k],
			                 l->side[k][0][0], l->side[k][0][1], l->side[k][0][2], l->side[k][1][0], l->side[k][1][1], l->side[k][1][2], l->flatSide[k][0],
			                 l->flatSide[k][1], l->flatSide[k][2]);
		}
		for (k = 0; k < 2; k++)
		{
			RldKitBuf_Printf(&b, "wheel look     tread %s: render %5.1f %5.1f %5.1f, sprite %5.1f %5.1f %5.1f, flat colour %5.1f %5.1f %5.1f\n", (k == 0) ? "upper" : "lower",
			                 l->tread[k][0][0], l->tread[k][0][1], l->tread[k][0][2], l->tread[k][1][0], l->tread[k][1][1], l->tread[k][1][2], l->flatTread[0],
			                 l->flatTread[1], l->flatTread[2]);
		}
		RldKitBuf_Printf(&b, "wheel look     largest difference to the sprites %.1f (limit %.1f) - the flat colours %.1f\n", l->worst, RLDKIT_LOOK_LIMIT, l->worstFlat);
	}
	RldKitBuf_Printf(&b, "budget         %u triangles of %u, %llu texture bytes of %u\n", (unsigned)c->triangles, (unsigned)RLDHIRES_BUDGET_TRIANGLES,
	                 (unsigned long long)c->textureBytes, (unsigned)RLDHIRES_BUDGET_TEXTURE_BYTES);
	if (b.fail)
	{
		RldKitBuf_Free(&b);
		return NULL;
	}
	*sizeOut = b.len;
	return b.p;
}

//========================================================================================
// THE GAME DATA
//========================================================================================

// The BIGFILE of path: a folder (with BIGFILE.BIG or ctr-u.bin, itself or in
// assets\), ctr-u.bin or BIGFILE.BIG; without a path the game's own search
// (assets\ next to this program or up to two folders above it). 1 = open.
static int RldKit_OpenBig(const char *argv0, const char *path, struct RldMkBig *big, char *source, size_t sourceSize)
{
	char dir[RLD_PATH_MAX];
	int level;

	memset(big, 0, sizeof(*big));
	if (path != NULL)
	{
		const size_t n = strlen(path);
		char a[RLD_PATH_MAX + 48];
		const char *const names[4] = {RLD_SEP "BIGFILE.BIG", RLD_SEP "ctr-u.bin", RLD_SEP "assets" RLD_SEP "BIGFILE.BIG", RLD_SEP "assets" RLD_SEP "ctr-u.bin"};
		int i;

		if ((n >= 4u) && (Rld_FileExists(path)))
		{
			const int image = ((n >= 4u) && ((strcmp(path + n - 4u, ".bin") == 0) || (strcmp(path + n - 4u, ".BIN") == 0)));

			if (image)
			{
				snprintf(source, sourceSize, "%s (BIGFILE.BIG inside)", path);
				return NativeDiscImage_OpenImagePath(path) && NativeDiscImage_FindFile("BIGFILE.BIG", &big->inImage);
			}
			big->loose = fopen(path, "rb");
			snprintf(source, sourceSize, "%s", path);
			return big->loose != NULL;
		}
		for (i = 0; i < 4; i++)
		{
			snprintf(a, sizeof(a), "%s%s", path, names[i]);
			if (!Rld_FileExists(a))
			{
				continue;
			}
			if ((i & 1) != 0)
			{
				snprintf(source, sourceSize, "%s (BIGFILE.BIG inside)", a);
				return NativeDiscImage_OpenImagePath(a) && NativeDiscImage_FindFile("BIGFILE.BIG", &big->inImage);
			}
			big->loose = fopen(a, "rb");
			snprintf(source, sourceSize, "%s", a);
			return big->loose != NULL;
		}
		return 0;
	}
	if (!Rld_ExeDir(argv0, dir, sizeof(dir)))
	{
		return 0;
	}
	for (level = 0; level < 3; level++)
	{
		char loose[RLD_PATH_MAX + 32];
		char image[RLD_PATH_MAX + 32];
		char parent[RLD_PATH_MAX];

		snprintf(loose, sizeof(loose), "%s" RLD_SEP "assets" RLD_SEP "BIGFILE.BIG", dir);
		snprintf(image, sizeof(image), "%s" RLD_SEP "assets" RLD_SEP "ctr-u.bin", dir);
		if (Rld_FileExists(loose))
		{
			big->loose = fopen(loose, "rb");
			snprintf(source, sourceSize, "%s", loose);
			return big->loose != NULL;
		}
		if (Rld_FileExists(image))
		{
			snprintf(source, sourceSize, "%s (BIGFILE.BIG inside)", image);
			return NativeDiscImage_OpenImagePath(image) && NativeDiscImage_FindFile("BIGFILE.BIG", &big->inImage);
		}
		if (!Rld_ParentDir(dir, parent, sizeof(parent)))
		{
			break;
		}
		snprintf(dir, sizeof(dir), "%s", parent);
	}
	return 0;
}

// The entries the kit reads into src (malloc'ed, RldKit_SourceFree). 1 = all there.
static int RldKit_ReadSource(const struct RldMkBig *big, RldKitSource *src, char *why, size_t whySize)
{
	int d;

	memset(src, 0, sizeof(*src));
	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		src->model[d] = RldMk_BigEntry(big, RLDKIT_BIG_MODEL_HI + (u32)d, &src->modelSize[d]);
		if (src->model[d] == NULL)
		{
			snprintf(why, whySize, "BIGFILE entry %u (the racer model of driver %d) cannot be read", (unsigned)(RLDKIT_BIG_MODEL_HI + (u32)d), d);
			return 0;
		}
	}
	src->vrm = RldMk_BigEntry(big, RLDKIT_BIG_SHARED_VRM, &src->vrmSize);
	src->pack = RldMk_BigEntry(big, RLDKIT_BIG_SHARED_PACK, &src->packSize);
	if ((src->vrm == NULL) || (src->pack == NULL))
	{
		snprintf(why, whySize, "BIGFILE entries 258 / 259 (shared.vrm, the shared pack) cannot be read");
		return 0;
	}
	return 1;
}

static void RldKit_SourceFree(RldKitSource *src)
{
	int d;

	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		free((void *)src->model[d]);
	}
	free((void *)src->vrm);
	free((void *)src->pack);
	memset(src, 0, sizeof(*src));
}

//========================================================================================
// THE STAND-IN (code-made game data: no byte of the game)
//========================================================================================

#define RLDKIT_SI_SCALE 4096
#define RLDKIT_SI_ORIGIN (-128)
#define RLDKIT_SI_KART_TPAGE 0x6Fu                       // page (960, 0), 4 bit, abr 3
#define RLDKIT_SI_KART_CLUT ((112u / 16u) | (255u << 6)) // CLUT at (112, 255)
#define RLDKIT_SI_TIRE_TPAGE 0x67u                       // page (448, 0), 4 bit
#define RLDKIT_SI_TIRE_CLUT (0u | (259u << 6))           // CLUT at (0, 259)

// One stand-in vertex record: model units (integers; y even).
struct RldKitSiVert
{
	int x, y, z;
};

// The stand-in model of driver d: a box kart (still, textured, 12 triangles),
// a box figure that sways in the turn (untextured, 12), a ring of 6 records
// turning about +z at (0, 38, 24), radius 10 (3 triangles, 2 records each
// shared). 21 raw frames.
static u8 *RldKit_StandinModel(int d, u32 *sizeOut)
{
	static const int boxFaces[12][3] = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 1, 4}, {1, 5, 4}, {2, 6, 3}, {3, 6, 7}, {0, 4, 2}, {2, 4, 6}, {1, 3, 5}, {3, 7, 5}};
	struct RldKitSiVert verts[64];
	int tri[64][3];
	int triKind[64]; // 0 kart, 1 figure, 2 ring
	int nv = 0;
	int nt = 0;
	struct RldKitBuf b;
	u32 hdr;
	u32 cmdl;
	u32 lay;
	u32 table;
	u32 colours;
	u32 anims;
	u32 anim;
	u32 frameSize;
	int i;
	int f;

	memset(&b, 0, sizeof(b));
	// kart box x -28..28, y 6..30, z -54..58
	for (i = 0; i < 8; i++)
	{
		verts[nv].x = (i & 1) ? 28 : -28;
		verts[nv].y = (i & 2) ? 30 : 6;
		verts[nv].z = (i & 4) ? 58 : -54;
		nv++;
	}
	for (i = 0; i < 12; i++)
	{
		tri[nt][0] = boxFaces[i][0];
		tri[nt][1] = boxFaces[i][1];
		tri[nt][2] = boxFaces[i][2];
		triKind[nt++] = 0;
	}
	// figure box x -10..10, y 30..70 + d, z -10..10
	for (i = 0; i < 8; i++)
	{
		verts[nv].x = (i & 1) ? 10 : -10;
		verts[nv].y = (i & 2) ? (70 + 2 * d) : 30;
		verts[nv].z = (i & 4) ? 10 : -10;
		nv++;
	}
	for (i = 0; i < 12; i++)
	{
		tri[nt][0] = 8 + boxFaces[i][0];
		tri[nt][1] = 8 + boxFaces[i][1];
		tri[nt][2] = 8 + boxFaces[i][2];
		triKind[nt++] = 1;
	}
	// ring: 6 records (frame 10 positions), 3 triangles (0 1 2, 2 3 4, 4 5 0)
	for (i = 0; i < 6; i++)
	{
		double s;
		double c;

		RldKit_SinCos(60.0 * i, &s, &c);
		verts[nv].x = (int)floor(10.0 * c + 0.5);
		verts[nv].y = 38 + 2 * (int)floor(5.0 * s + 0.5); // even
		verts[nv].z = 24;
		nv++;
	}
	for (i = 0; i < 3; i++)
	{
		tri[nt][0] = 16 + 2 * i;
		tri[nt][1] = 16 + 2 * i + 1;
		tri[nt][2] = 16 + (2 * i + 2) % 6;
		triKind[nt++] = 2;
	}
	// Records: one per triangle corner (each triangle its own strip of 3, no
	// cache): records = 3 x triangles.
	{
		const u32 records = (u32)nt * 3u;

		frameSize = (0x1Cu + 3u * records + 3u) & ~3u;
		// the layout: header area, header, commands, layouts, table, colours, anim
		hdr = 0x18u;
		cmdl = hdr + 0x40u;
		lay = cmdl + 4u + 4u * records + 4u;
		table = lay + 12u * 2u;
		colours = table + 4u;
		anims = colours + 4u * 4u;
		anim = anims + 4u;
		{
			const u32 size = anim + 0x18u + 21u * frameSize;
			u8 *body = (u8 *)calloc(size + 4u, 1);
			u32 at;
			u32 k;

			if (body == NULL)
			{
				return NULL;
			}
			body[0] = (u8)(size & 0xffu);
			body[1] = (u8)((size >> 8) & 0xffu);
			body[2] = (u8)((size >> 16) & 0xffu);
#define SI_P32(o, v)                                  \
	do                                                \
	{                                                 \
		const u32 vv_ = (u32)(v);                     \
		body[4u + (o)] = (u8)(vv_ & 0xffu);           \
		body[4u + (o) + 1u] = (u8)((vv_ >> 8) & 0xffu);  \
		body[4u + (o) + 2u] = (u8)((vv_ >> 16) & 0xffu); \
		body[4u + (o) + 3u] = (u8)((vv_ >> 24) & 0xffu); \
	} while (0)
#define SI_P16(o, v)                                 \
	do                                               \
	{                                                \
		const u32 vv_ = (u32)(v);                    \
		body[4u + (o)] = (u8)(vv_ & 0xffu);          \
		body[4u + (o) + 1u] = (u8)((vv_ >> 8) & 0xffu); \
	} while (0)
			memcpy(&body[4], "standin_kit", 11);
			SI_P16(0x12u, 1u);
			SI_P32(0x14u, hdr);
			memcpy(&body[4u + hdr], "standin_kit_hi", 14);
			SI_P16(hdr + 0x18u, RLDKIT_SI_SCALE);
			SI_P16(hdr + 0x1Au, RLDKIT_SI_SCALE);
			SI_P16(hdr + 0x1Cu, RLDKIT_SI_SCALE);
			SI_P32(hdr + 0x20u, cmdl);
			SI_P32(hdr + 0x28u, table);
			SI_P32(hdr + 0x2Cu, colours);
			SI_P32(hdr + 0x34u, 1u);
			SI_P32(hdr + 0x38u, anims);
			// commands: per triangle three; the first starts the strip and carries
			// the look: bit 31 new strip, 28 one-sided, 29 listed order, colour
			// index (bits 9-15), texture index (bits 0-8, 1 = layout 0 kart).
			at = cmdl + 4u;
			for (k = 0; k < (u32)nt; k++)
			{
				u32 c;

				for (i = 0; i < 3; i++)
				{
					const u32 slot = 1u + ((k * 3u + (u32)i) % 255u);
					const u32 colour = (triKind[k] == 0) ? 0u : ((triKind[k] == 1) ? 1u : 2u);

					c = (slot << 16) | (colour << 9);
					if (i == 0)
					{
						c |= 0x80000000u | 0x10000000u | 0x20000000u | ((triKind[k] == 0) ? 1u : 0u);
					}
					SI_P32(at, c);
					at += 4u;
				}
			}
			SI_P32(at, 0xffffffffu);
			// layouts: 0 the kart tile (u 96..127, v 48..63), 1 unused
			SI_P32(lay, 96u | (48u << 8) | (RLDKIT_SI_KART_CLUT << 16));
			SI_P32(lay + 4u, 127u | (48u << 8) | (RLDKIT_SI_KART_TPAGE << 16));
			SI_P32(lay + 8u, 96u | (63u << 8));
			SI_P32(table, lay);
			// colours: grey 128, the figure's blue, the ring's dark grey, white
			SI_P32(colours, 0x808080u);
			SI_P32(colours + 4u, 0xc82828u);
			SI_P32(colours + 8u, 0x282828u);
			SI_P32(colours + 12u, 0xffffffu);
			// animation 0: 21 raw frames
			SI_P32(anims, anim);
			memcpy(&body[4u + anim], "turn", 4);
			SI_P16(anim + 0x10u, 21u);
			SI_P16(anim + 0x12u, frameSize);
			SI_P32(anim + 0x14u, 0u);
			for (f = 0; f < 21; f++)
			{
				const u32 frame = anim + 0x18u + (u32)f * frameSize;
				const double turn = 45.0 * (double)(10 - f) / 10.0;
				double s;
				double c;

				RldKit_SinCos(turn, &s, &c);
				SI_P16(frame, (u32)(RLDKIT_SI_ORIGIN & 0xffff));
				SI_P16(frame + 2u, 0u);
				SI_P16(frame + 4u, (u32)(RLDKIT_SI_ORIGIN & 0xffff));
				SI_P32(frame + 0x18u, 0x1Cu);
				for (k = 0; k < (u32)nt; k++)
				{
					for (i = 0; i < 3; i++)
					{
						const struct RldKitSiVert *v = &verts[tri[k][i]];
						int x = v->x;
						int y = v->y;
						int z = v->z;
						const u32 rec = k * 3u + (u32)i;
						const u32 o = frame + 0x1Cu + 3u * rec;

						if (triKind[k] == 1)
						{
							x += (f - 10) / 2; // the figure sways
						}
						else if (triKind[k] == 2)
						{
							const double dx = (double)v->x;
							const double dy = (double)(v->y - 38);

							x = (int)floor(dx * c - dy * s + 0.5);
							y = 38 + 2 * (int)floor((dx * s + dy * c) / 2.0 + 0.5);
						}
						body[4u + o] = (u8)(x - RLDKIT_SI_ORIGIN);
						body[4u + o + 1u] = (u8)(z - RLDKIT_SI_ORIGIN);
						body[4u + o + 2u] = (u8)y;
					}
				}
			}
#undef SI_P32
#undef SI_P16
			*sizeOut = size + 4u;
			return body;
		}
	}
}

// A VRM chain block: 0x14 header bytes (rect at 0x0C) and the words.
static void RldKit_StandinBlock(struct RldKitBuf *b, u32 x, u32 y, u32 w, u32 h, const u16 *words)
{
	u32 i;

	RldKitBuf_U32(b, 0x14u + 2u * w * h);
	for (i = 0; i < 12u; i++)
	{
		const u8 z = 0;

		RldKitBuf_Put(b, &z, 1u);
	}
	RldKitBuf_U16(b, x);
	RldKitBuf_U16(b, y);
	RldKitBuf_U16(b, w);
	RldKitBuf_U16(b, h);
	for (i = 0; i < w * h; i++)
	{
		RldKitBuf_U16(b, words[i]);
	}
}

// shared.vrm stand-in: the kart tile on page (960, 0) (a 4-bit pattern), its
// CLUT, the two wheel sprites on page (448, 0) rows 216..247 and their CLUT.
static u8 *RldKit_StandinVrm(u32 *sizeOut)
{
	struct RldKitBuf b;
	u16 page[64 * 256];
	u16 clut[16];
	u16 tire[16 * 32];
	u16 tclut[16];
	u32 x;
	u32 y;
	u32 i;

	memset(&b, 0, sizeof(b));
	for (y = 0; y < 256u; y++)
	{
		for (x = 0; x < 64u; x++)
		{
			u32 w = 0;
			u32 n;

			for (n = 0; n < 4u; n++)
			{
				const u32 u = x * 4u + n;

				w |= (1u + (((u / 4u) + (y / 4u)) % 15u)) << (4u * n);
			}
			page[y * 64u + x] = (u16)w;
		}
	}
	for (i = 0; i < 16u; i++)
	{
		clut[i] = (u16)((i == 0u) ? 0u : (0x8000u | (i * 2u) | ((31u - i) << 5) | (16u << 10)));
	}
	// sprites: tread (texels 0..31) a 22 x 28 block, side (32..63) a disc r 15:
	// tyre 1, rim 2 (r 3..8), hub 3 (r < 2), outside 0
	for (y = 0; y < 32u; y++)
	{
		for (x = 0; x < 16u; x++)
		{
			u32 w = 0;
			u32 n;

			for (n = 0; n < 4u; n++)
			{
				const int u = (int)(x * 4u + n);
				u32 idx = 0;

				if (u < 32)
				{
					idx = ((u >= 5) && (u <= 26) && (y >= 2u) && (y <= 29u)) ? 1u : 0u;
				}
				else
				{
					const double dx = (double)(u - 32) - 15.5;
					const double dy = (double)y - 15.5;
					const double r = sqrt(dx * dx + dy * dy);

					idx = (r > 15.0) ? 0u : ((r < 2.0) ? 3u : ((r < 8.0) ? 2u : 1u));
				}
				w |= idx << (4u * n);
			}
			tire[y * 16u + x] = (u16)w;
		}
	}
	memset(tclut, 0, sizeof(tclut));
	tclut[1] = (u16)(0x8000u | 4u | (3u << 5) | (3u << 10));     // dark tyre
	tclut[2] = (u16)(0x8000u | 18u | (17u << 5) | (17u << 10));  // silver rim
	tclut[3] = (u16)(0x8000u | 2u | (2u << 5) | (2u << 10));     // hub
	RldKitBuf_U32(&b, 0x20u);
	RldKitBuf_U32(&b, 0x14u + 2u * 64u * 256u);
	// block 0 (its size above), then "next" + block for each further one
	{
		u32 j;

		for (j = 0; j < 12u; j++)
		{
			const u8 z = 0;

			RldKitBuf_Put(&b, &z, 1u);
		}
		RldKitBuf_U16(&b, 960u);
		RldKitBuf_U16(&b, 0u);
		RldKitBuf_U16(&b, 64u);
		RldKitBuf_U16(&b, 256u);
		for (j = 0; j < 64u * 256u; j++)
		{
			RldKitBuf_U16(&b, page[j]);
		}
	}
	RldKit_StandinBlock(&b, 112u, 255u, 16u, 1u, clut);
	RldKit_StandinBlock(&b, 448u, 216u, 16u, 32u, tire);
	RldKit_StandinBlock(&b, 0u, 259u, 16u, 1u, tclut);
	if (b.fail)
	{
		RldKitBuf_Free(&b);
		return NULL;
	}
	*sizeOut = (u32)b.len;
	return b.p;
}

// The shared pack stand-in: icon group 0 "tireAnim" of 17 icons; icon 0 the
// tread sprite, the others the side sprite.
static u8 *RldKit_StandinPack(u32 *sizeOut)
{
	struct RldKitBuf d;
	struct RldKitBuf out;
	u32 i;
	const u32 ltl = 4u;
	const u32 gptr = ltl + 16u;
	const u32 group = gptr + 4u;
	const u32 icons = group + 0x14u + 4u * 17u;

	memset(&d, 0, sizeof(d));
	memset(&out, 0, sizeof(out));
	RldKitBuf_U32(&d, ltl);
	RldKitBuf_U32(&d, 17u);   // numIcon
	RldKitBuf_U32(&d, icons); // firstIcon
	RldKitBuf_U32(&d, 1u);    // numIconGroup
	RldKitBuf_U32(&d, gptr);  // firstIconGroupPtr
	RldKitBuf_U32(&d, group);
	RldKitBuf_Put(&d, "tireAnim\0\0\0\0\0\0\0\0", 16u);
	RldKitBuf_U16(&d, 0u);
	RldKitBuf_U16(&d, 17u);
	for (i = 0; i < 17u; i++)
	{
		RldKitBuf_U32(&d, icons + 0x20u * i);
	}
	for (i = 0; i < 17u; i++)
	{
		const u32 u0 = (i == 0u) ? 0u : 32u;

		RldKitBuf_Put(&d, "tire\0\0\0\0\0\0\0\0\0\0\0\0", 16u);
		RldKitBuf_U32(&d, 0xffffffffu);
		RldKitBuf_U32(&d, u0 | (216u << 8) | (RLDKIT_SI_TIRE_CLUT << 16));
		RldKitBuf_U32(&d, (u0 + 31u) | (216u << 8) | (RLDKIT_SI_TIRE_TPAGE << 16));
		RldKitBuf_U32(&d, u0 | (247u << 8) | ((u0 + 31u) << 16) | (247u << 24));
	}
	RldKitBuf_U32(&out, (u32)d.len);
	RldKitBuf_Put(&out, d.p, d.len);
	RldKitBuf_Free(&d);
	if (out.fail)
	{
		RldKitBuf_Free(&out);
		return NULL;
	}
	*sizeOut = (u32)out.len;
	return out.p;
}

// The stand-in source in memory (RldKit_SourceFree).
static int RldKit_StandinSource(RldKitSource *src)
{
	int d;

	memset(src, 0, sizeof(*src));
	for (d = 0; d < RLDKIT_DRIVERS; d++)
	{
		src->model[d] = RldKit_StandinModel(d, &src->modelSize[d]);
		if (src->model[d] == NULL)
		{
			return 0;
		}
	}
	src->vrm = RldKit_StandinVrm(&src->vrmSize);
	src->pack = RldKit_StandinPack(&src->packSize);
	return (src->vrm != NULL) && (src->pack != NULL);
}

// The stand-in as a BIGFILE.BIG: 260 entries, 242..256, 258 and 259 filled,
// each at a sector of its own.
static u8 *RldKit_StandinBigfile(const RldKitSource *src, size_t *sizeOut)
{
	const u32 count = 260u;
	const u32 tableSectors = (8u + 8u * count + RLDMK_SECTOR - 1u) / RLDMK_SECTOR;
	struct RldKitBuf b;
	u32 sector = tableSectors;
	u32 i;
	u8 *table;

	memset(&b, 0, sizeof(b));
	table = (u8 *)calloc(tableSectors, RLDMK_SECTOR);
	if (table == NULL)
	{
		return NULL;
	}
	Rld_WriteLE32(&table[4], count);
	for (i = 0; i < count; i++)
	{
		const u8 *data = NULL;
		u32 size = 0;

		if ((i >= RLDKIT_BIG_MODEL_HI) && (i < RLDKIT_BIG_MODEL_HI + RLDKIT_DRIVERS))
		{
			data = src->model[i - RLDKIT_BIG_MODEL_HI];
			size = src->modelSize[i - RLDKIT_BIG_MODEL_HI];
		}
		else if (i == RLDKIT_BIG_SHARED_VRM)
		{
			data = src->vrm;
			size = src->vrmSize;
		}
		else if (i == RLDKIT_BIG_SHARED_PACK)
		{
			data = src->pack;
			size = src->packSize;
		}
		if (data != NULL)
		{
			Rld_WriteLE32(&table[8u + 8u * i], sector);
			Rld_WriteLE32(&table[12u + 8u * i], size);
			sector += (size + RLDMK_SECTOR - 1u) / RLDMK_SECTOR;
		}
	}
	RldKitBuf_Put(&b, table, (size_t)tableSectors * RLDMK_SECTOR);
	free(table);
	for (i = 0; i < count; i++)
	{
		const u8 *data = NULL;
		u32 size = 0;
		static const u8 zero[RLDMK_SECTOR];

		if ((i >= RLDKIT_BIG_MODEL_HI) && (i < RLDKIT_BIG_MODEL_HI + RLDKIT_DRIVERS))
		{
			data = src->model[i - RLDKIT_BIG_MODEL_HI];
			size = src->modelSize[i - RLDKIT_BIG_MODEL_HI];
		}
		else if (i == RLDKIT_BIG_SHARED_VRM)
		{
			data = src->vrm;
			size = src->vrmSize;
		}
		else if (i == RLDKIT_BIG_SHARED_PACK)
		{
			data = src->pack;
			size = src->packSize;
		}
		if (data != NULL)
		{
			RldKitBuf_Put(&b, data, size);
			if ((size % RLDMK_SECTOR) != 0u)
			{
				RldKitBuf_Put(&b, zero, RLDMK_SECTOR - (size % RLDMK_SECTOR));
			}
		}
	}
	if (b.fail)
	{
		RldKitBuf_Free(&b);
		return NULL;
	}
	*sizeOut = b.len;
	return b.p;
}

//========================================================================================
// THE COMMANDS
//========================================================================================

// Every output in memory: names[i] gets bytes[i] (malloc'ed). 1 = all made.
static int RldKit_MakeFiles(const RldKitResult *r, const RldKitOptions *opt, const char *source, u8 *bytes[RLDKIT_TOOL_FILES], size_t sizes[RLDKIT_TOOL_FILES])
{
	struct RldKitChecks c;
	int i;

	RldKit_Checks(r, &c);
	bytes[0] = RldKit_WriteGlb(r, 1, RLDHIRES_VCOL_PSX, &sizes[0]);
	bytes[1] = RldKit_WriteGlb(r, 1, RLDHIRES_VCOL_GLTF, &sizes[1]);
	bytes[2] = RldKit_WriteGlb(r, 0, RLDHIRES_VCOL_PSX, &sizes[2]);
	bytes[3] = RldKit_WriteGlb(r, 0, RLDHIRES_VCOL_GLTF, &sizes[3]);
	bytes[4] = ((bytes[0] != NULL) && (bytes[1] != NULL) && (bytes[2] != NULL) && (bytes[3] != NULL)) ? RldKit_WriteInfo(r, opt, &c, bytes, sizes, &sizes[4]) : NULL;
	bytes[5] = RldKit_WriteReport(r, opt, &c, source, &sizes[5]);
	for (i = 0; i < RLDKIT_TOOL_FILES; i++)
	{
		if (bytes[i] == NULL)
		{
			return 0;
		}
	}
	return 1;
}

static const char *const s_rldKitNames[RLDKIT_TOOL_FILES] = {"kit-kart.glb", "kit-kart-gltf.glb", "kit-wheel.glb", "kit-wheel-gltf.glb", "kit-info.json",
                                                              "kit-extract.txt"};

static int RldKit_IntArg(const char *text, int lo, int hi, int *out)
{
	char *end = NULL;
	const long v = strtol(text, &end, 10);

	if ((end == text) || (*end != '\0') || (v < lo) || (v > hi))
	{
		return 0;
	}
	*out = (int)v;
	return 1;
}

static int RldKit_ExtractCommand(int argc, char **argv, const char *argv0)
{
	static RldKitResult r;
	RldKitOptions opt;
	RldKitSource src;
	struct RldMkBig big;
	const char *game = NULL;
	const char *outDir = NULL;
	char source[RLD_PATH_MAX + 64];
	char why[256];
	u8 *bytes[RLDKIT_TOOL_FILES];
	u8 *again[RLDKIT_TOOL_FILES];
	size_t sizes[RLDKIT_TOOL_FILES];
	size_t sizes2[RLDKIT_TOOL_FILES];
	int i;
	int result = 1;

	memset(&opt, 0, sizeof(opt));
	memset(bytes, 0, sizeof(bytes));
	memset(again, 0, sizeof(again));
	opt.driver = 0;
	opt.segments = 4;
	opt.upscale = 4;
	for (i = 0; i < argc; i++)
	{
		const int more = (i + 1) < argc;

		if ((strcmp(argv[i], "--game") == 0) && more && (game == NULL))
		{
			game = argv[++i];
		}
		else if ((strcmp(argv[i], "--driver") == 0) && more && RldKit_IntArg(argv[i + 1], 0, RLDKIT_DRIVERS - 1, &opt.driver))
		{
			i++;
		}
		else if ((strcmp(argv[i], "--segments") == 0) && more && RldKit_IntArg(argv[i + 1], 1, 8, &opt.segments) &&
		         ((opt.segments == 1) || (opt.segments == 2) || (opt.segments == 4) || (opt.segments == 8)))
		{
			i++;
		}
		else if ((strcmp(argv[i], "--upscale") == 0) && more && RldKit_IntArg(argv[i + 1], 1, 4, &opt.upscale) && (opt.upscale != 3))
		{
			i++;
		}
		else if ((strcmp(argv[i], "-o") == 0) && more && (outDir == NULL))
		{
			outDir = argv[++i];
		}
		else
		{
			outDir = NULL;
			break;
		}
	}
	if (outDir == NULL)
	{
		fprintf(stderr, "usage: rldpack kit-extract [--game <folder | ctr-u.bin | BIGFILE.BIG>] [--driver 0..14] [--segments 1|2|4|8] [--upscale 1|2|4] -o <folder>\n");
		return 2;
	}
	source[0] = '\0';
	if (!RldKit_OpenBig(argv0, game, &big, source, sizeof(source)))
	{
		printf("kit-extract: the game data cannot be found or opened%s%s\n", (game != NULL) ? ": " : "", (game != NULL) ? game : "");
		Rld_Say("error", "kit-no-data", game,
		        "The game data was not found. Give the folder of your game (with assets\\BIGFILE.BIG or assets\\ctr-u.bin) with --game.");
		return 1;
	}
	printf("kit-extract: driver %d from %s\n", opt.driver, source);
	if (!RldKit_ReadSource(&big, &src, why, sizeof(why)))
	{
		if (big.loose != NULL)
		{
			fclose(big.loose);
		}
		RldKit_SourceFree(&src);
		printf("kit-extract: %s\n", why);
		Rld_Say("error", "kit-no-data", source, "%s. Is this the game's data?", why);
		return 1;
	}
	if (big.loose != NULL)
	{
		fclose(big.loose);
	}
	if (!RldKit_Build(&src, &opt, &r, why, sizeof(why)))
	{
		RldKit_SourceFree(&src);
		printf("kit-extract: %s\n", why);
		Rld_Say("error", "kit-bad-model", source, "The kart cannot be made from this game data: %s", why);
		return 1;
	}
	RldKit_SourceFree(&src);
	// Built twice: the same bytes (the files are the kit's identity).
	if (!RldKit_MakeFiles(&r, &opt, source, bytes, sizes) || !RldKit_MakeFiles(&r, &opt, source, again, sizes2))
	{
		printf("kit-extract: out of memory\n");
		goto done;
	}
	for (i = 0; i < RLDKIT_TOOL_FILES; i++)
	{
		if ((sizes[i] != sizes2[i]) || (memcmp(bytes[i], again[i], sizes[i]) != 0))
		{
			printf("kit-extract: %s came out different twice - nothing written (a bug of rldpack)\n", s_rldKitNames[i]);
			goto done;
		}
	}
	for (i = 0; i < RLDKIT_TOOL_FILES; i++)
	{
		char path[RLD_PATH_MAX + 32];

		snprintf(path, sizeof(path), "%s" RLD_SEP "%s", outDir, s_rldKitNames[i]);
		if (!RldHiresPack_Write(path, bytes[i], sizes[i]))
		{
			printf("kit-extract: %s cannot be written\n", path);
			Rld_Say("error", "kit-write", path, "%s cannot be written. Does the folder exist, and may it be written to?", path);
			goto done;
		}
		printf("wrote %s (%u bytes)\n", path, (unsigned)sizes[i]);
		Rld_Emit("written", path, (const char *)NULL);
	}
	fwrite(bytes[5], 1, sizes[5], stdout);
	result = 0;

done:
	for (i = 0; i < RLDKIT_TOOL_FILES; i++)
	{
		free(bytes[i]);
		free(again[i]);
	}
	RldKit_ResultFree(&r);
	return result;
}

static int RldKit_StandinCommand(int argc, char **argv)
{
	RldKitSource src;
	u8 *big;
	size_t size = 0;
	char path[RLD_PATH_MAX + 32];
	int ok;

	if ((argc != 2) || (strcmp(argv[0], "-o") != 0))
	{
		fprintf(stderr, "usage: rldpack kit-standin -o <folder>\n");
		return 2;
	}
	if (!RldKit_StandinSource(&src))
	{
		RldKit_SourceFree(&src);
		fprintf(stderr, "rldpack: out of memory\n");
		return 1;
	}
	big = RldKit_StandinBigfile(&src, &size);
	RldKit_SourceFree(&src);
	if (big == NULL)
	{
		fprintf(stderr, "rldpack: out of memory\n");
		return 1;
	}
	snprintf(path, sizeof(path), "%s" RLD_SEP "BIGFILE.BIG", argv[1]);
	ok = RldHiresPack_Write(path, big, size);
	free(big);
	printf(ok ? "wrote %s (a code-made stand-in, no game data)\n" : "kit-standin: %s cannot be written\n", path);
	return ok ? 0 : 1;
}

//========================================================================================
// THE SELF-TEST
//========================================================================================

static int s_kitChecks;
static int s_kitFailures;

static void KitTest_Check(int ok, const char *what)
{
	s_kitChecks++;
	if (!ok)
	{
		s_kitFailures++;
		printf("  FAIL kit: %s\n", what);
	}
}

static int RldKit_SelfTest(void)
{
	static RldKitResult r;
	static RldKitResult r2;
	RldKitSource src;
	RldKitOptions opt;
	char why[256];
	u8 *bytes[RLDKIT_TOOL_FILES];
	u8 *again[RLDKIT_TOOL_FILES];
	size_t sizes[RLDKIT_TOOL_FILES];
	size_t sizes2[RLDKIT_TOOL_FILES];
	int i;

	s_kitChecks = 0;
	s_kitFailures = 0;
	memset(bytes, 0, sizeof(bytes));
	memset(again, 0, sizeof(again));
	printf("rldpack kit self-test (a code-made stand-in, no game data)\n");

	// The pure helpers.
	{
		double s;
		double c;
		u8 a[4 * 4] = {0};
		u8 out[16 * 4];

		RldKit_SinCos(90.0, &s, &c);
		KitTest_Check((fabs(s - 1.0) < 1e-12) && (fabs(c) < 1e-12), "sine and cosine of 90 degrees");
		a[0] = 255;
		a[3] = 255; // a 2 x 2: one red texel, three black (alpha 0 except the first)
		RldKit_Scale2x(a, 2u, 2u, out);
		KitTest_Check((out[0] == 255) && (out[4] == 255) && (out[2 * 4 * 2] == 255) && (out[(4 * 1 + 1) * 4] == 0),
		              "Scale2x: a corner texel keeps three of its four, the corner toward its unlike neighbours goes to them");
	}

	KitTest_Check(RldKit_StandinSource(&src), "the stand-in is made");
	memset(&opt, 0, sizeof(opt));
	opt.driver = 3;
	opt.segments = 4;
	opt.upscale = 4;
	KitTest_Check(RldKit_Build(&src, &opt, &r, why, sizeof(why)), "the stand-in builds");
	if (r.kart.v != NULL)
	{
		struct RldKitChecks c;

		RldKit_Checks(&r, &c);
		KitTest_Check((r.triangles == 27u) && (r.records == 81u) && (r.commands == 81u), "decoded: 27 triangles, 81 records and commands");
		KitTest_Check((r.kartTriangles == 12u) && (r.kartShared == 12u) && (r.stillTriangles == 12u) && (r.dropped == 0u),
		              "the kart: the 12 still box triangles, shared by every driver; the swaying figure and the ring are not");
		KitTest_Check(r.kart.indices / 3u == (12u + r.floorDoubled) * 16u, "smoothed: 16 triangles each (4 segments), a cockpit floor triangle twice");
		KitTest_Check(r.maxDeviation < 1e-6, "a box has only hard edges: the smoothing keeps it flat");
		KitTest_Check((c.iou[0] > 0.999) && (c.iou[1] > 0.999) && (c.iou[2] > 0.999) && (c.iou[3] > 0.999), "silhouette IoU 1 in all four views");
		KitTest_Check((c.colourMean <= 2.0) && (c.colourMax <= 8.0), "colour difference within mean 2, max 8");
		KitTest_Check((r.tiles == 1) && (r.atlasW == 4u * r.sourceW) && (r.atlasH == 4u * r.sourceH) && !r.atlasAlpha, "one tile, atlas x4, no transparent texel");
		// (the stand-in's heights are even, as the game stores them: its ring
		// records sit up to 1 unit off the circle, so the circles' centres scatter)
		KitTest_Check(r.ringMeasured && (fabs(r.ringCenter[0] * 64.0) < 1.5) && (fabs(r.ringCenter[1] * 64.0 - 38.0) < 1.5) && (fabs(r.ringCenter[2] * 64.0 - 24.0) < 0.1) &&
		                  (fabs(r.ringRadius * 64.0 - 10.0) < 1.0) && (r.ringNormal[2] > 0.99),
		              "the ring measured: centre (0, 38, 24), radius 10, plane normal +z (within the stand-in's rounding)");
		KitTest_Check(r.ringAxis[2] > 0.99, "the ring turns about +z");
		if (!r.ringMeasured || (r.ringNormal[2] <= 0.99))
		{
			printf("       ring: measured %d, %u records, centre %.3f %.3f %.3f, normal %.3f %.3f %.3f, radius %.3f\n", r.ringMeasured, (unsigned)r.ringRecords,
			       r.ringCenter[0] * 64.0, r.ringCenter[1] * 64.0, r.ringCenter[2] * 64.0, r.ringNormal[0], r.ringNormal[1], r.ringNormal[2], r.ringRadius * 64.0);
		}
		KitTest_Check(r.wheelMeasured && (fabs(r.wheelRadius * 64.0 - 15.0) < 0.6) && (fabs(r.wheelWidth * 64.0 - 22.0) < 1e-6) && (r.tyre[0] < 40u) && (r.rim[0] > 120u),
		              "the wheel from the sprites: radius 15, width 22 (the tread sprite), dark tyre, light rim");
		KitTest_Check((r.wheel.indices / 3u <= 400u) && (r.wheel.indices / 3u >= 200u) && r.wheel.textured && (r.wheelAtlas != NULL) &&
		                  (r.wheelAtlasW == 4u * r.wheelSourceW) && (r.wheelAtlasH == 4u * r.wheelSourceH),
		              "the wheel (K2b): 200..400 triangles, textured with its own atlas x4 (the two sprites)");
		{
			u32 t;
			int opaque = 1;

			for (t = 0; (r.wheelAtlasSource != NULL) && (t < r.wheelSourceW * r.wheelSourceH); t++)
			{
				opaque &= (r.wheelAtlasSource[t * 4u + 3u] == 255u);
			}
			KitTest_Check(opaque && (r.wheelAtlasSource != NULL), "the wheel's atlas: every transparent texel filled from its neighbours");
		}
		KitTest_Check(c.wheelLook && (c.look.worst <= RLDKIT_LOOK_LIMIT) && (c.look.worstFlat > c.look.worst),
		              "the wheel's look: render vs sprites within 12 per channel and ring, closer than the flat colours");
		if (!c.wheelLook || (c.look.worst > RLDKIT_LOOK_LIMIT) || (c.look.worstFlat <= c.look.worst))
		{
			printf("       wheel look: measured %d, worst %.2f, flat %.2f; tyre %.1f/%.1f rim %.1f/%.1f hub %.1f/%.1f upper %.1f/%.1f lower %.1f/%.1f (red, render/sprite)\n",
			       c.wheelLook, c.look.worst, c.look.worstFlat, c.look.side[0][0][0], c.look.side[0][1][0], c.look.side[1][0][0], c.look.side[1][1][0],
			       c.look.side[2][0][0], c.look.side[2][1][0], c.look.tread[0][0][0], c.look.tread[0][1][0], c.look.tread[1][0][0], c.look.tread[1][1][0]);
		}
		KitTest_Check((fabs(r.wheelPos[0][0] * 64.0 - 36.0) < 1e-9) && (fabs(r.wheelPos[1][0] * 64.0 + 36.0) < 1e-9) && (fabs(r.wheelPos[2][2] * 64.0 + 24.0) < 1e-9),
		              "the wheel points of the game");
		KitTest_Check(fabs(r.seat[1] * 64.0 - (6.0 + RLDKIT_SEAT_ABOVE_FLOOR)) < 1e-6, "the seat: 4.4 above the floor found under it (the box bottom at 6; its top at 30 is above the ray)");
		KitTest_Check((c.triangles < 9000u) && (c.textureBytes <= 2u * 1024u * 1024u), "the kit part within 9000 triangles and 2 MiB");
		KitTest_Check(RldKit_MakeFiles(&r, &opt, "stand-in", bytes, sizes) && RldKit_MakeFiles(&r, &opt, "stand-in", again, sizes2), "the files are made");
		for (i = 0; i < RLDKIT_TOOL_FILES; i++)
		{
			KitTest_Check((bytes[i] != NULL) && (again[i] != NULL) && (sizes[i] == sizes2[i]) && (memcmp(bytes[i], again[i], sizes[i]) == 0), "the same bytes twice");
		}
		// The GLBs through the game's rules: the kart part usable, the kit mark said.
		if (bytes[0] != NULL)
		{
			static RldHiresReport rep;
			RldHiresModel m;
			int ok;

			RldHires_ReportClear(&rep);
			ok = RldHires_OpenAs(bytes[0], (u32)sizes[0], RLDHIRES_VCOL_PSX, &m, &rep);
			KitTest_Check(ok && (rep.errors == 0) && (m.drawNodeCount == 2) && (m.wheelsFound == 0) && RldHires_ReportHas(&rep, "HRG-41") &&
			                  RldHires_CheckBudget(&m, &rep),
			              "kit-kart.glb: usable, two mesh nodes (kart_body, kart_steer_wheel), no wheel node, HRG-41, within the budget");
			for (i = 0; i < rep.count; i++)
			{
				if ((strcmp(rep.msg[i].rule, "HRG-41") != 0) && (strcmp(rep.msg[i].rule, "HRG-19") != 0) && (strcmp(rep.msg[i].rule, "HRG-20") != 0))
				{
					printf("       %s: %s\n", rep.msg[i].rule, rep.msg[i].text);
					KitTest_Check(0, "kit-kart.glb: no finding but HRG-41, the missing clips (HRG-19) and the wheels in their own file (HRG-20)");
				}
			}
			RldHires_Close(&m);
		}
		if (bytes[1] != NULL)
		{
			static RldHiresReport rep;
			RldHiresModel m;

			RldHires_ReportClear(&rep);
			KitTest_Check(RldHires_OpenAs(bytes[1], (u32)sizes[1], RLDHIRES_VCOL_GLTF, &m, &rep) && !RldHires_ReportHas(&rep, "HRG-36"),
			              "kit-kart-gltf.glb: usable in glTF mode, no HRG-36");
			RldHires_Close(&m);
		}
		if (bytes[4] != NULL)
		{
			char *json = (char *)malloc(sizes[4] + 1u);

			if (json != NULL)
			{
				memcpy(json, bytes[4], sizes[4]);
				json[sizes[4]] = '\0';
				KitTest_Check((strstr(json, "\"kit\": 1,") != NULL) && (strstr(json, "\"steer\": {\"center\"") != NULL) && (strstr(json, "\"axis\"") != NULL) &&
				                  (strstr(json, "\"kart_box\"") != NULL) && (strstr(json, "\"floor\": [[") != NULL) && (strstr(json, "\"wheel_fl\"") != NULL) &&
				                  (strstr(json, "\"wheel_radius\"") != NULL) && (strstr(json, "\"vcol\": \"psx\", \"sha256\": \"") != NULL) &&
				                  (strstr(json, "\"budget\"") != NULL),
				              "kit-info.json has the generator's fields (kit, seat, steer, kart_box, floor, wheels, wheel_radius, files with sha256)");
				free(json);
			}
		}
		// Determinism of the whole build.
		KitTest_Check(RldKit_Build(&src, &opt, &r2, why, sizeof(why)) && (r2.kart.vertices == r.kart.vertices) &&
		                  (memcmp(r2.kart.v, r.kart.v, r.kart.vertices * sizeof(RldKitVertex)) == 0) &&
		                  (memcmp(r2.atlas, r.atlas, (size_t)r.atlasW * r.atlasH * 4u) == 0),
		              "a second build gives the same mesh and atlas");
		RldKit_ResultFree(&r2);
	}
	else
	{
		printf("       %s\n", why);
	}
	// THE CLASSIC KART (CHRI bit 5): the kit's kart as retail triangles, from one
	// decoding of the 15 models; without game data, or with a broken model, a
	// reason and no kart; spliced into the figure-only test model of make-char
	// as the game does it (platform/native_chars.c, THE GAME KART).
	{
		static RldKitClassic karts[RLDKIT_DRIVERS];
		static RldKitClassic none[RLDKIT_DRIVERS];
		static struct RldMkTestMesh mesh;
		RldKitSource broken = src;
		struct RldMkOptions mo;
		struct RldMkPly ply;
		struct RldMkBuild b;
		struct RldCharGameKart gk;
		struct RldCharSpliceFacts facts;
		struct RldCharModelFacts mf;
		struct RldCharFinding finding;
		struct RldCharKartTri *tri = NULL;
		u8 *shadow = NULL;
		u8 *real = NULL;
		size_t shadowSize = 0;
		size_t realSize = 0;
		const char *splice = "not made";
		u32 t;
		int textured = 1;

		const int quietBefore = s_rldMkQuiet;

		memset(karts, 0, sizeof(karts));
		memset(none, 0, sizeof(none));
		memset(&ply, 0, sizeof(ply));
		memset(&b, 0, sizeof(b));
		memset(&facts, 0, sizeof(facts));
		s_rldMkQuiet = 1;
		KitTest_Check(RldKit_ClassicKarts(&src, (1u << 3) | 1u, karts, why, sizeof(why)) && karts[3].ok && karts[0].ok && !karts[1].ok &&
		                  (karts[3].count == 12u) && (karts[0].count == 12u) && (karts[3].retailTriangles == 27u),
		              "classic kart: donors 0 and 3 from one decoding - the 12 still box triangles each (the kit's kart), driver 1 not asked");
		for (t = 0; t < karts[3].count; t++)
		{
			textured &= karts[3].tri[t].textured;
		}
		KitTest_Check((karts[3].count != 0u) && textured, "classic kart: every triangle with its texture layout (the stand-in's kart is textured)");
		KitTest_Check(!RldKit_ClassicKarts(NULL, 1u, none, why, sizeof(why)) && !none[0].ok && (strstr(none[0].why, "no game data") != NULL),
		              "classic kart: without game data no kart, the reason 'no game data'");
		broken.modelSize[5] = 6u;
		KitTest_Check(!RldKit_ClassicKarts(&broken, 1u, none, why, sizeof(why)) && !none[0].ok && (strstr(none[0].why, "driver 5") != NULL),
		              "classic kart: a broken model gives no kart, naming its driver");

		if (karts[3].ok && RldMk_GameKartTestModel(&mesh, &ply, &b, &mo) && RldMk_GameKartField(&b, &mo, &gk))
		{
			tri = (struct RldCharKartTri *)calloc(karts[3].count, sizeof(*tri));
			for (t = 0; (tri != NULL) && (t < karts[3].count); t++)
			{
				memcpy(tri[t].p, karts[3].tri[t].p, sizeof(tri[t].p));
				memcpy(tri[t].rgb, karts[3].tri[t].rgb, sizeof(tri[t].rgb));
				memcpy(tri[t].layout, karts[3].tri[t].layout, sizeof(tri[t].layout));
				tri[t].cull = karts[3].tri[t].cull;
				tri[t].textured = karts[3].tri[t].textured;
			}
			splice = (tri == NULL) ? "out of memory" : RldChar_SpliceKart(b.cmdl, b.cmdlSize, &gk, tri, karts[3].count, 0, 0, &shadow, &shadowSize, &facts);
			if ((splice == NULL) && (RldChar_CheckModel(shadow, shadowSize, &finding, NULL, NULL, &mf) != RLDCHAR_VERDICT_OK))
			{
				splice = finding.detail;
			}
			if (splice == NULL)
			{
				splice = RldChar_SpliceKart(b.cmdl, b.cmdlSize, &gk, tri, karts[3].count, 0, 1, &real, &realSize, &facts);
			}
		}
		KitTest_Check((splice == NULL) && (facts.kartTriangles == 12u) && (facts.standIn == 228u) && (facts.layouts >= 1u) && (realSize > shadowSize),
		              "classic kart: donor 3 spliced into the figure-only model in place of the stand-in (228), through the model check, with its texture layouts");
		if (splice != NULL)
		{
			printf("       splice: %s\n", splice);
		}
		s_rldMkQuiet = quietBefore;
		RldMk_ResetSaid();
		free(tri);
		free(shadow);
		free(real);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
		for (t = 0; t < RLDKIT_DRIVERS; t++)
		{
			RldKit_ClassicFree(&karts[t]);
			RldKit_ClassicFree(&none[t]);
		}
	}
	// Refusals: N. Oxide's slot, a broken model.
	opt.driver = 15;
	KitTest_Check(!RldKit_Build(&src, &opt, &r2, why, sizeof(why)), "driver 15 is refused");
	opt.driver = 0;
	{
		RldKitSource broken = src;

		broken.modelSize[5] = 6u;
		KitTest_Check(!RldKit_Build(&broken, &opt, &r2, why, sizeof(why)) && (strstr(why, "driver 5") != NULL), "a broken model is refused, naming its driver");
	}
	for (i = 0; i < RLDKIT_TOOL_FILES; i++)
	{
		free(bytes[i]);
		free(again[i]);
	}
	RldKit_ResultFree(&r);
	RldKit_SourceFree(&src);
	printf("kit selftest %s: %d checks, %d failures\n", (s_kitFailures == 0) ? "passed" : "FAILED", s_kitChecks, s_kitFailures);
	return (s_kitFailures == 0) ? 0 : 1;
}

static void RldKit_Usage(FILE *out)
{
	fprintf(out, "CREATOR KIT (the kart of your own game data, hi-res)\n");
	fprintf(out, "  rldpack kit-extract [--game <folder | ctr-u.bin | BIGFILE.BIG>] [--driver 0..14] [--segments 1|2|4|8] [--upscale 1|2|4] -o <folder>\n");
	fprintf(out, "  Reads the racer model of the driver (default 0) and its textures from your\n");
	fprintf(out, "  game data and writes kit-kart.glb (the kart, smoothed and upscaled, with a\n");
	fprintf(out, "  steering wheel and four wheels), kit-kart-gltf.glb, kit-wheel.glb,\n");
	fprintf(out, "  kit-wheel-gltf.glb, kit-info.json and kit-extract.txt into the folder.\n");
	fprintf(out, "  These files are made from your game data: keep them on this computer.\n");
	fprintf(out, "  rldpack kit-standin -o <folder>\n");
	fprintf(out, "  Writes a code-made stand-in BIGFILE.BIG (no game data) for tests.\n\n");
}
