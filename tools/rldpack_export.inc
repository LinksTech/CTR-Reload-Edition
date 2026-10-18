// rldpack_export.inc - char-export: the model of a .rldchar as a glTF binary
//
// Included by tools/rldpack.c after rldpack_kit.inc (its GLB helpers
// RldKitBuf_*, RldKitGlb_Mesh, RldKitGlb_Pack and the retail decoder of
// include/rldkit.inc) and rldpack_char.inc (the envelope, CMDL check, the
// native model reader of include/rldchar.inc, RldMk_PngRgbaPacked).
//
//   char-export <file.rldchar> -o <file.glb> [--source auto|classic|native] [--pose <0..46>]
//       writes the driver's model as a GLB, so that an existing driver can be
//       taken into the Creator Kit's template as a starting point:
//         classic  the CMDL (the model every game draws): vertex colours, no
//                  texture (format 1.0 has none)
//         native   the CNET with its CTXT textures (the native model of the
//                  preview), its materials and texture coordinates
//         auto     (default) native when the file has a usable one, else classic
//       --pose: a frame of the turn (animation 0, frames 0..20; 21..46 take the
//       neutral 10 for the classic model, any of the 47 for the native one);
//       default 10, the neutral one of the turn. Axes and units as CHRG
//       (include/rldhires.inc): glTF +Y up, +Z the kart's front, +X its left,
//       1 m = 64 game units, the origin on the ground under the kart.
//       Not written: the rig (a classic model has none: its poses are vertex
//       frames), the wheels (the game draws them), the mask, the voices.
//       A one-sided triangle faces out as the game culls it; the two-sided
//       ones go into a material of their own (doubleSided).
//       Exit 0 = written, 1 = the file cannot be read or its model is refused
//       by the game's check, 2 = usage.

#define RLDEXP_POSE_NEUTRAL 10u

// One primitive being collected: triangles of one material.
struct RldExpPart
{
	RldKitMesh mesh;
	u32 capacity;      // triangles
	int texture;       // the image index in the GLB, -1 none
	u8 rgba[4];        // the material colour (sRGB)
	int doubleSided;
	int mask;
};

static int RldExp_Add(struct RldExpPart *p, const double pos[3][3], const double uv[3][2], const u8 col[3][4])
{
	double e1[3];
	double e2[3];
	double n[3];
	u32 c;
	int k;

	if (p->mesh.vertices + 3u > 3u * p->capacity)
	{
		const u32 cap = (p->capacity != 0u) ? 2u * p->capacity : 256u;
		RldKitVertex *v = (RldKitVertex *)realloc(p->mesh.v, (size_t)cap * 3u * sizeof(RldKitVertex));
		u32 *ix = (u32 *)realloc(p->mesh.index, (size_t)cap * 3u * sizeof(u32));

		if (v != NULL)
		{
			p->mesh.v = v;
		}
		if (ix != NULL)
		{
			p->mesh.index = ix;
		}
		if ((v == NULL) || (ix == NULL))
		{
			return 0;
		}
		p->capacity = cap;
	}
	for (k = 0; k < 3; k++)
	{
		e1[k] = pos[1][k] - pos[0][k];
		e2[k] = pos[2][k] - pos[0][k];
	}
	RldKit_Cross(e1, e2, n);
	if (!RldKit_Normalize(n))
	{
		n[0] = 0.0;
		n[1] = 1.0;
		n[2] = 0.0;
	}
	for (c = 0; c < 3u; c++)
	{
		RldKit_SetVertex(&p->mesh.v[p->mesh.vertices], pos[c], RLDKIT_UNITS_PER_METRE, n, (uv != NULL) ? uv[c][0] : 0.0, (uv != NULL) ? uv[c][1] : 0.0, col[c]);
		p->mesh.index[p->mesh.indices++] = p->mesh.vertices++;
	}
	return 1;
}

// The classic model (CMDL) in pose `pose` into parts[0] (one-sided) and
// parts[1] (two-sided). 1 = done; 0 with why.
static int RldExp_Classic(const u8 *cmdl, size_t size, u32 pose, int fullHeight, struct RldExpPart parts[2], char *why, size_t whySize)
{
	static RldKitRetail m;
	static const int order[2][3] = {{0, 2, 1}, {0, 1, 2}};
	u32 frame;
	u32 t;
	int ok = 1;

	if (!RldKit_RetailDecodeAs(cmdl, (u32)size, fullHeight, &m, why, whySize))
	{
		RldKit_RetailFree(&m);
		return 0;
	}
	// The pose: the stored frames of animation 0 only (the decoder reads that
	// one); a pose beyond the turn falls back to the neutral frame.
	frame = (pose < m.frames) ? pose : ((RLDEXP_POSE_NEUTRAL < m.frames) ? RLDEXP_POSE_NEUTRAL : 0u);
	for (t = 0; ok && (t < m.tris); t++)
	{
		const int twoSided = (m.triCmd[t] & 0x10000000u) == 0u;
		const int listed = (m.triCmd[t] & 0x20000000u) != 0u;
		const int *fr = order[(twoSided || listed) ? 1 : 0];
		double pos[3][3];
		u8 col[3][4];
		int c;

		for (c = 0; c < 3; c++)
		{
			RldKit_Pos(&m, frame, m.triRec[t * 3u + (u32)fr[c]], pos[c]);
			RldKit_Colour(&m, m.triCol[t * 3u + (u32)fr[c]], col[c]);
			col[c][3] = 255;
		}
		ok = RldExp_Add(&parts[twoSided ? 1 : 0], pos, NULL, col);
	}
	RldKit_RetailFree(&m);
	if (!ok)
	{
		snprintf(why, whySize, "out of memory");
	}
	return ok;
}

// The native model (CNET) in pose `pose`: one part per material.
static int RldExp_Native(const struct RldCharNative *n, u32 pose, struct RldExpPart *parts, u32 partCount, char *why, size_t whySize)
{
	const u32 p = (n->poseCount == 0u) ? 0u : ((pose < n->poseCount) ? pose : 0u);
	const u8 *posn = n->poses + (size_t)p * n->vertexCount * 24u;
	u32 t;

	for (t = 0; t < n->triangleCount; t++)
	{
		const u32 material = Rld_ReadLE16(&n->triangleMaterials[t * 2u]);
		double pos[3][3];
		double uv[3][2];
		u8 col[3][4];
		int c;

		if (material >= partCount)
		{
			snprintf(why, whySize, "triangle %u names material %u of %u", (unsigned)t, (unsigned)material, (unsigned)partCount);
			return 0;
		}
		for (c = 0; c < 3; c++)
		{
			const u8 *ix = &n->indices[((size_t)t * 3u + (u32)c) * n->indexSize];
			const u32 v = (n->indexSize == 2u) ? Rld_ReadLE16(ix) : Rld_ReadLE32(ix);
			int k;

			if (v >= n->vertexCount)
			{
				snprintf(why, whySize, "triangle %u names vertex %u of %u", (unsigned)t, (unsigned)v, (unsigned)n->vertexCount);
				return 0;
			}
			for (k = 0; k < 3; k++)
			{
				float f;

				memcpy(&f, &posn[(size_t)v * 24u + 4u * (u32)k], 4u);
				pos[c][k] = (double)f;
			}
			for (k = 0; k < 2; k++)
			{
				float f = 0.0f;

				if (n->uv != NULL)
				{
					memcpy(&f, &n->uv[(size_t)v * 8u + 4u * (u32)k], 4u);
				}
				uv[c][k] = (double)f;
			}
			if (n->colors != NULL)
			{
				memcpy(col[c], &n->colors[(size_t)v * 4u], 4u);
			}
			else
			{
				col[c][0] = col[c][1] = col[c][2] = col[c][3] = 255;
			}
		}
		if (!RldExp_Add(&parts[material], pos, uv, col))
		{
			snprintf(why, whySize, "out of memory");
			return 0;
		}
	}
	return 1;
}

// The GLB of the parts; images[k] (PNG bytes) for the parts' texture k.
static u8 *RldExp_Glb(const char *name, struct RldExpPart *parts, u32 partCount, u8 *const *images, const size_t *imageSize, u32 imageCount, size_t *sizeOut)
{
	struct RldKitGlb g;
	struct RldKitBuf json;
	int ids[64][5];
	int imageView[RLDCHAR_TEX_COUNT_MAX];
	u32 i;
	u32 used = 0;
	int first = 1;
	u8 *glb;

	memset(&g, 0, sizeof(g));
	memset(&json, 0, sizeof(json));
	if (partCount > 64u)
	{
		return NULL;
	}
	for (i = 0; i < partCount; i++)
	{
		if (parts[i].mesh.indices != 0u)
		{
			// a part with a texture keeps COLOR_0 as it is (glTF white neutral)
			parts[i].mesh.textured = (parts[i].texture >= 0);
			RldKitGlb_Mesh(&g, &parts[i].mesh, RLDHIRES_VCOL_GLTF, ids[i]);
			if (parts[i].texture < 0)
			{
				ids[i][2] = -1;
			}
		}
	}
	for (i = 0; i < imageCount; i++)
	{
		RldKitBuf_Align4(&g.bin, 0);
		imageView[i] = (int)RldKitGlb_View(&g, (u32)g.bin.len, (u32)imageSize[i]);
		RldKitBuf_Put(&g.bin, images[i], imageSize[i]);
	}
	RldKitBuf_Printf(&json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"rldpack char-export (CTR Reload %s)\"},", CTR_NATIVE_VERSION);
	RldKitBuf_Printf(&json, "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"%s\",\"mesh\":0}],\"meshes\":[{\"name\":\"%s\",\"primitives\":[", name, name);
	for (i = 0; i < partCount; i++)
	{
		if (parts[i].mesh.indices == 0u)
		{
			continue;
		}
		RldKitBuf_Printf(&json, "%s{\"mode\":4,\"material\":%u,\"indices\":%d,\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d,", first ? "" : ",", (unsigned)used, ids[i][4],
		                 ids[i][0], ids[i][1]);
		if (ids[i][2] >= 0)
		{
			RldKitBuf_Printf(&json, "\"TEXCOORD_0\":%d,", ids[i][2]);
		}
		RldKitBuf_Printf(&json, "\"COLOR_0\":%d}}", ids[i][3]);
		first = 0;
		used++;
	}
	RldKitBuf_Printf(&json, "]}],\"materials\":[");
	first = 1;
	used = 0;
	for (i = 0; i < partCount; i++)
	{
		const struct RldExpPart *p = &parts[i];

		if (p->mesh.indices == 0u)
		{
			continue;
		}
		RldKitBuf_Printf(&json, "%s{\"name\":\"%s_%u\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.6f,%.6f,%.6f,1],", first ? "" : ",", name, (unsigned)used,
		                 (double)RldHiresColor_Decode((float)p->rgba[0] / 255.0f), (double)RldHiresColor_Decode((float)p->rgba[1] / 255.0f),
		                 (double)RldHiresColor_Decode((float)p->rgba[2] / 255.0f));
		if (p->texture >= 0)
		{
			RldKitBuf_Printf(&json, "\"baseColorTexture\":{\"index\":%d},", p->texture);
		}
		RldKitBuf_Printf(&json, "\"metallicFactor\":0,\"roughnessFactor\":1},\"alphaMode\":\"%s\"%s,\"doubleSided\":%s}", p->mask ? "MASK" : "OPAQUE",
		                 p->mask ? ",\"alphaCutoff\":0.5" : "", p->doubleSided ? "true" : "false");
		first = 0;
		used++;
	}
	RldKitBuf_Printf(&json, "],");
	if (imageCount != 0u)
	{
		RldKitBuf_Printf(&json, "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987}],\"textures\":[");
		for (i = 0; i < imageCount; i++)
		{
			RldKitBuf_Printf(&json, "%s{\"source\":%u,\"sampler\":0}", (i != 0u) ? "," : "", (unsigned)i);
		}
		RldKitBuf_Printf(&json, "],\"images\":[");
		for (i = 0; i < imageCount; i++)
		{
			RldKitBuf_Printf(&json, "%s{\"bufferView\":%d,\"mimeType\":\"image/png\"}", (i != 0u) ? "," : "", imageView[i]);
		}
		RldKitBuf_Printf(&json, "],");
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

// A name for the GLB from the driver's name: letters and digits, others '_'.
static void RldExp_Name(const char *in, char *out, size_t outSize)
{
	size_t i;
	size_t n = 0;

	for (i = 0; (in[i] != '\0') && (n + 1u < outSize); i++)
	{
		const char c = in[i];

		out[n++] = (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'))) ? c : '_';
	}
	if (n == 0u)
	{
		snprintf(out, outSize, "driver");
		return;
	}
	out[n] = '\0';
}

static int RldExp_Command(int argc, char **argv)
{
	static struct RldCharNative native;
	struct RldReader reader;
	struct RldCharInfo info;
	struct RldExpPart parts[64];
	struct RldCharFinding finding;
	const char *in = NULL;
	const char *outPath = NULL;
	const char *source = "auto";
	u32 pose = RLDEXP_POSE_NEUTRAL;
	u8 *images[RLDCHAR_TEX_COUNT_MAX];
	size_t imageSize[RLDCHAR_TEX_COUNT_MAX];
	u32 imageCount = 0;
	u32 partCount = 0;
	u8 *cmdl = NULL;
	u8 *chri = NULL;
	size_t cmdlSize = 0;
	size_t chriSize = 0;
	int index = -1;
	const char *why = NULL;
	char text[256];
	char name[32];
	int useNative = 0;
	int result = 1;
	u8 *glb = NULL;
	size_t glbSize = 0;
	u32 tris = 0;
	int i;

	memset(parts, 0, sizeof(parts));
	memset(images, 0, sizeof(images));
	memset(&native, 0, sizeof(native));
	for (i = 0; i < argc; i++)
	{
		if ((strcmp(argv[i], "-o") == 0) && ((i + 1) < argc) && (outPath == NULL))
		{
			outPath = argv[++i];
		}
		else if ((strcmp(argv[i], "--source") == 0) && ((i + 1) < argc) &&
		         ((strcmp(argv[i + 1], "auto") == 0) || (strcmp(argv[i + 1], "classic") == 0) || (strcmp(argv[i + 1], "native") == 0)))
		{
			source = argv[++i];
		}
		else if ((strcmp(argv[i], "--pose") == 0) && ((i + 1) < argc))
		{
			int v = 0;

			if (!RldKit_IntArg(argv[i + 1], 0, (int)RLDCHAR_NET_POSES - 1, &v))
			{
				in = NULL;
				break;
			}
			pose = (u32)v;
			i++;
		}
		else if ((argv[i][0] != '-') && (in == NULL))
		{
			in = argv[i];
		}
		else
		{
			in = NULL;
			break;
		}
	}
	if ((in == NULL) || (outPath == NULL))
	{
		fprintf(stderr, "usage: rldpack char-export <file.rldchar> -o <file.glb> [--source auto|classic|native] [--pose <0..46>]\n");
		return 2;
	}

	why = Rld_OpenAs(&reader, in, &s_rldCharFormat);
	if (why != NULL)
	{
		printf("char-export: %s is no readable character: %s\n", in, why);
		Rld_Say("error", "export-file", in, "%s is no readable character: %s", in, why);
		return 1;
	}
	if ((Rld_FindEntry(&reader, "CHRI", &index) == NULL) || ((chri = Rld_ReadChunk(&reader, index, &chriSize, &why)) == NULL) ||
	    (RldChar_ParseInfo(&info, chri, chriSize) != NULL))
	{
		printf("char-export: %s: CHRI cannot be read\n", in);
		Rld_Close(&reader);
		free(chri);
		return 1;
	}
	free(chri);
	if ((Rld_FindEntry(&reader, "CMDL", &index) == NULL) || ((cmdl = Rld_ReadChunk(&reader, index, &cmdlSize, &why)) == NULL))
	{
		printf("char-export: %s: CMDL cannot be read\n", in);
		Rld_Close(&reader);
		return 1;
	}
	if (RldChar_CheckModel(cmdl, cmdlSize, &finding, NULL, NULL, NULL) != RLDCHAR_VERDICT_OK)
	{
		printf("char-export: %s: the model does not pass the game's check (%s)\n", in, finding.rule);
		Rld_Say("error", "export-model", in, "The model of %s does not pass the game's check (%s).", in, finding.rule);
		Rld_Close(&reader);
		free(cmdl);
		return 1;
	}
	if ((strcmp(source, "classic") != 0) && ((Rld_FindEntry(&reader, "CNET", NULL) != NULL)))
	{
		struct RldCharHull hulls[RLDCHAR_NET_POSES];

		if (RldChar_FrameHulls(&cmdl[4], Rld_ReadLE32(&cmdl[0]), 0u, hulls))
		{
			RldChar_ReadNative(&reader, info.flags, hulls, &native);
		}
		useNative = (native.state == RLDCHAR_NATIVE_READY);
	}
	Rld_Close(&reader);
	if ((strcmp(source, "native") == 0) && !useNative)
	{
		printf("char-export: %s has no usable native model (CNET)%s%s - use --source classic\n", in, (native.rule != NULL) ? ": " : "",
		       (native.rule != NULL) ? native.detail : "");
		RldChar_FreeNative(&native);
		free(cmdl);
		return 1;
	}
	RldExp_Name(info.name, name, sizeof(name));

	text[0] = '\0';
	if (useNative)
	{
		u32 m;

		partCount = (native.materialCount <= 64u) ? native.materialCount : 0u;
		for (m = 0; m < partCount; m++)
		{
			const u8 *mat = &native.materials[m * 16u];
			const int tex = (int)(s16)Rld_ReadLE16(&mat[4]);

			memcpy(parts[m].rgba, mat, 4u);
			parts[m].texture = ((tex >= 0) && ((u32)tex < native.textureCount)) ? tex : -1;
			parts[m].mask = (mat[6] != 0u);
		}
		for (m = 0; m < native.textureCount; m++)
		{
			images[m] = RldMk_PngRgbaPacked(native.texture[m].rgba, native.texture[m].width, native.texture[m].height, &imageSize[m]);
			if (images[m] == NULL)
			{
				snprintf(text, sizeof(text), "out of memory");
				break;
			}
			imageCount++;
		}
		if ((text[0] == '\0') && ((partCount == 0u) || !RldExp_Native(&native, pose, parts, partCount, text, sizeof(text))))
		{
			if (text[0] == '\0')
			{
				snprintf(text, sizeof(text), "%u materials", (unsigned)native.materialCount);
			}
		}
	}
	else
	{
		int k;

		partCount = 2u;
		for (k = 0; k < 2; k++)
		{
			parts[k].texture = -1;
			parts[k].rgba[0] = parts[k].rgba[1] = parts[k].rgba[2] = parts[k].rgba[3] = 255;
		}
		parts[1].doubleSided = 1;
		(void)RldExp_Classic(cmdl, cmdlSize, pose, (info.flags & RLDCHAR_FLAG_FULL_HEIGHT) != 0u, parts, text, sizeof(text));
	}
	for (i = 0; i < (int)partCount; i++)
	{
		tris += parts[i].mesh.indices / 3u;
	}
	if ((text[0] == '\0') && (tris == 0u))
	{
		snprintf(text, sizeof(text), "no triangle");
	}
	if (text[0] == '\0')
	{
		glb = RldExp_Glb(name, parts, partCount, images, imageSize, imageCount, &glbSize);
		if (glb == NULL)
		{
			snprintf(text, sizeof(text), "out of memory");
		}
	}
	if (text[0] != '\0')
	{
		printf("char-export: %s: %s\n", in, text);
		Rld_Say("error", "export-model", in, "The model of %s cannot be exported: %s", in, text);
	}
	else if (!RldHiresPack_Write(outPath, glb, glbSize))
	{
		printf("char-export: %s cannot be written\n", outPath);
		Rld_Say("error", "export-write", outPath, "%s cannot be written. Does the folder exist, and may it be written to?", outPath);
	}
	else
	{
		printf("char-export: %s (%s, the %s model, pose %u): %u triangles, %u texture(s) -> %s (%u bytes)\n", in, info.name, useNative ? "native" : "classic",
		       (unsigned)pose, (unsigned)tris, (unsigned)imageCount, outPath, (unsigned)glbSize);
		Rld_Emit("written", outPath, (const char *)NULL);
		result = 0;
	}
	free(glb);
	for (i = 0; i < 64; i++)
	{
		RldKit_MeshFree(&parts[i].mesh);
	}
	for (i = 0; i < (int)RLDCHAR_TEX_COUNT_MAX; i++)
	{
		free(images[i]);
	}
	RldChar_FreeNative(&native);
	free(cmdl);
	return result;
}

static void RldExp_Usage(FILE *out)
{
	fprintf(out, "CHARACTER EXPORT\n");
	fprintf(out, "  rldpack char-export <file.rldchar> -o <file.glb> [--source auto|classic|native] [--pose <0..46>]\n");
	fprintf(out, "  Writes the driver's model as a glTF binary (glTF axes, 1 m = 64 game units):\n");
	fprintf(out, "  the classic model (vertex colours) or the native one with its textures\n");
	fprintf(out, "  (auto: native when the file has one). The pose is one of the 47 frames,\n");
	fprintf(out, "  default 10 (neutral). No rig, wheels, mask or voices. A starting point for\n");
	fprintf(out, "  the Creator Kit's template.\n\n");
}
