// rldpack_gltf_test.inc - the self-tests of the glTF reader (tools/rldpack_gltf.inc).
// Included by tools/rldpack_obj_test.inc; RldMk_SelfTestObj runs them. Every
// GLB, .gltf, buffer and picture is made here, in memory; s_rldObjFiles
// stands in for the disk while they are read.

// What a test GLB holds. One mesh of one primitive on one node.
struct RldGltfTestTarget
{
	const char *name;
	const float *delta;   // 3 per vertex
	int sparse;           // written as a sparse accessor: only the vertices that move
};

struct RldGltfTestModel
{
	const float *xyz;     // 3 per vertex
	const float *rgb;     // COLOR_0, 3 floats per vertex (linear); NULL = none
	const float *uv;      // 2 per vertex; NULL = none
	u32 count;
	const u16 *index;     // 3 per triangle
	u32 triangles;
	const struct RldGltfTestTarget *targets;
	u32 targetCount;
	const float *factor;  // baseColorFactor; NULL = no material
	const u8 *png;        // the base color texture in the file; NULL = none
	size_t pngSize;
	const char *uri;      // the base color texture as a file next to it; NULL = none
	const char *node;     // more members of the node ("\"scale\":[2,2,2]," or "")
	int mode;             // 4 = triangles
	int skin;             // a skin on the node
	const char *bufferUri; // NULL = GLB with its binary chunk; else a .gltf whose buffer is this file
};

static void RldGltf_TestPad(struct RldMkBuffer *b, u8 with)
{
	while ((b->size % 4u) != 0u)
	{
		RldMk_BufferBytes(b, &with, 1u);
	}
}

// The binary buffer and the JSON of `t`; *bin and *json are the caller's.
static void RldGltf_TestParts(const struct RldGltfTestModel *t, struct RldMkBuffer *bin, struct RldMkBuffer *json)
{
	u32 views = 0;
	u32 accessors = 0;
	u32 position;
	u32 color = RLDMK_NONE;
	u32 texcoord = RLDMK_NONE;
	u32 indices;
	u32 targetAccessor[8];
	u32 image = RLDMK_NONE;
	struct RldMkBuffer v;
	struct RldMkBuffer a;
	u32 i;
	u32 k;

	memset(&v, 0, sizeof(v));
	memset(&a, 0, sizeof(a));

#define RLDGLTF_TEST_VIEW(bytes)                                                                                             \
	do                                                                                                                       \
	{                                                                                                                        \
		RldMk_BufferText(&v, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", (views != 0u) ? "," : "", (u32)bin->size, \
		                 (u32)(bytes));                                                                                      \
		views++;                                                                                                             \
	} while (0)

	RldGltf_TestPad(bin, 0);
	RLDGLTF_TEST_VIEW(t->count * 12u);
	RldMk_BufferBytes(bin, t->xyz, (size_t)t->count * 12u);
	RldMk_BufferText(&a, "{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}", views - 1u, t->count);
	position = accessors++;
	if (t->rgb != NULL)
	{
		RLDGLTF_TEST_VIEW(t->count * 12u);
		RldMk_BufferBytes(bin, t->rgb, (size_t)t->count * 12u);
		RldMk_BufferText(&a, ",{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}", views - 1u, t->count);
		color = accessors++;
	}
	if (t->uv != NULL)
	{
		RLDGLTF_TEST_VIEW(t->count * 8u);
		RldMk_BufferBytes(bin, t->uv, (size_t)t->count * 8u);
		RldMk_BufferText(&a, ",{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"}", views - 1u, t->count);
		texcoord = accessors++;
	}
	RLDGLTF_TEST_VIEW(t->triangles * 6u);
	RldMk_BufferBytes(bin, t->index, (size_t)t->triangles * 6u);
	RldMk_BufferText(&a, ",{\"bufferView\":%u,\"componentType\":5123,\"count\":%u,\"type\":\"SCALAR\"}", views - 1u, 3u * t->triangles);
	indices = accessors++;
	for (k = 0; (k < t->targetCount) && (k < 8u); k++)
	{
		const float *d = t->targets[k].delta;

		RldGltf_TestPad(bin, 0);
		if (!t->targets[k].sparse)
		{
			RLDGLTF_TEST_VIEW(t->count * 12u);
			RldMk_BufferBytes(bin, d, (size_t)t->count * 12u);
			RldMk_BufferText(&a, ",{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}", views - 1u, t->count);
		}
		else
		{
			u32 moving = 0;
			u32 at;

			for (i = 0; i < t->count; i++)
			{
				moving += ((d[3u * i] != 0.0f) || (d[3u * i + 1u] != 0.0f) || (d[3u * i + 2u] != 0.0f)) ? 1u : 0u;
			}
			RLDGLTF_TEST_VIEW(moving * 4u);
			for (i = 0; i < t->count; i++)
			{
				if ((d[3u * i] != 0.0f) || (d[3u * i + 1u] != 0.0f) || (d[3u * i + 2u] != 0.0f))
				{
					RldMk_BufferLE32(bin, i);
				}
			}
			at = views - 1u;
			RLDGLTF_TEST_VIEW(moving * 12u);
			for (i = 0; i < t->count; i++)
			{
				if ((d[3u * i] != 0.0f) || (d[3u * i + 1u] != 0.0f) || (d[3u * i + 2u] != 0.0f))
				{
					RldMk_BufferBytes(bin, &d[3u * i], 12u);
				}
			}
			RldMk_BufferText(&a,
			                 ",{\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"sparse\":{\"count\":%u,\"indices\":{\"bufferView\":%u,"
			                 "\"componentType\":5125},\"values\":{\"bufferView\":%u}}}",
			                 t->count, moving, at, views - 1u);
		}
		targetAccessor[k] = accessors++;
	}
	if (t->png != NULL)
	{
		RldGltf_TestPad(bin, 0);
		RLDGLTF_TEST_VIEW(t->pngSize);
		RldMk_BufferBytes(bin, t->png, t->pngSize);
		image = views - 1u;
	}
	RldGltf_TestPad(bin, 0);
#undef RLDGLTF_TEST_VIEW

	RldMk_BufferText(json, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"rldpack self-test\"},\"scene\":0,\"scenes\":[{\"nodes\":[0%s]}],",
	                 t->skin ? ",1" : "");
	RldMk_BufferText(json, "\"nodes\":[{\"name\":\"body\",%s\"mesh\":0%s}%s],", (t->node != NULL) ? t->node : "", t->skin ? ",\"skin\":0" : "",
	                 t->skin ? ",{\"name\":\"bone\"}" : "");
	if (t->skin)
	{
		RldMk_BufferText(json, "\"skins\":[{\"joints\":[1]}],");
	}
	RldMk_BufferText(json, "\"meshes\":[{\"name\":\"body\",\"primitives\":[{\"attributes\":{\"POSITION\":%u", position);
	if (color != RLDMK_NONE)
	{
		RldMk_BufferText(json, ",\"COLOR_0\":%u", color);
	}
	if (texcoord != RLDMK_NONE)
	{
		RldMk_BufferText(json, ",\"TEXCOORD_0\":%u", texcoord);
	}
	RldMk_BufferText(json, "},\"indices\":%u,\"mode\":%d%s", indices, t->mode, (t->factor != NULL) ? ",\"material\":0" : "");
	if (t->targetCount != 0u)
	{
		RldMk_BufferText(json, ",\"targets\":[");
		for (k = 0; (k < t->targetCount) && (k < 8u); k++)
		{
			RldMk_BufferText(json, "%s{\"POSITION\":%u}", (k != 0u) ? "," : "", targetAccessor[k]);
		}
		RldMk_BufferText(json, "]}],\"extras\":{\"targetNames\":[");
		for (k = 0; (k < t->targetCount) && (k < 8u); k++)
		{
			RldMk_BufferText(json, "%s\"%s\"", (k != 0u) ? "," : "", t->targets[k].name);
		}
		RldMk_BufferText(json, "]}}],");
	}
	else
	{
		RldMk_BufferText(json, "}]}],");
	}
	if (t->factor != NULL)
	{
		RldMk_BufferText(json, "\"materials\":[{\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.9g,%.9g,%.9g,%.9g]%s}}],",
		                 (double)t->factor[0], (double)t->factor[1], (double)t->factor[2], (double)t->factor[3],
		                 ((t->png != NULL) || (t->uri != NULL)) ? ",\"baseColorTexture\":{\"index\":0}" : "");
	}
	if (t->png != NULL)
	{
		RldMk_BufferText(json, "\"textures\":[{\"source\":0}],\"images\":[{\"bufferView\":%u,\"mimeType\":\"image/png\"}],", image);
	}
	else if (t->uri != NULL)
	{
		RldMk_BufferText(json, "\"textures\":[{\"source\":0}],\"images\":[{\"uri\":\"%s\"}],", t->uri);
	}
	RldMk_BufferBytes(json, "\"bufferViews\":[", 15u);
	RldMk_BufferBytes(json, v.data, v.size);
	RldMk_BufferBytes(json, "],\"accessors\":[", 15u);
	RldMk_BufferBytes(json, a.data, a.size);
	if (t->bufferUri != NULL)
	{
		RldMk_BufferText(json, "],\"buffers\":[{\"uri\":\"%s\",\"byteLength\":%u}]}", t->bufferUri, (u32)bin->size);
	}
	else
	{
		RldMk_BufferText(json, "],\"buffers\":[{\"byteLength\":%u}]}", (u32)bin->size);
	}
	free(v.data);
	free(a.data);
}

// `t` as a GLB (or, with bufferUri, the .gltf's JSON; *bin then its buffer file).
static u8 *RldGltf_TestFile(const struct RldGltfTestModel *t, size_t *sizeOut, struct RldMkBuffer *binOut)
{
	struct RldMkBuffer bin;
	struct RldMkBuffer json;
	struct RldMkBuffer glb;

	memset(&bin, 0, sizeof(bin));
	memset(&json, 0, sizeof(json));
	memset(&glb, 0, sizeof(glb));
	RldGltf_TestParts(t, &bin, &json);
	if (t->bufferUri != NULL)
	{
		*binOut = bin;
		*sizeOut = json.size;
		return json.data;
	}
	RldGltf_TestPad(&json, ' ');
	RldMk_BufferBytes(&glb, "glTF", 4u);
	RldMk_BufferLE32(&glb, 2u);
	RldMk_BufferLE32(&glb, (u32)(12u + 8u + json.size + 8u + bin.size));
	RldMk_BufferLE32(&glb, (u32)json.size);
	RldMk_BufferBytes(&glb, "JSON", 4u);
	RldMk_BufferBytes(&glb, json.data, json.size);
	RldMk_BufferLE32(&glb, (u32)bin.size);
	RldMk_BufferBytes(&glb, "BIN\0", 4u);
	RldMk_BufferBytes(&glb, bin.data, bin.size);
	free(bin.data);
	free(json.data);
	*sizeOut = glb.size;
	return glb.data;
}

// A linear value that the reader turns into the sRGB byte b.
static float RldGltf_TestLinear(u32 b)
{
	if (b == 0u)
	{
		return 0.0f;
	}
	if (b >= 255u)
	{
		return 1.0f;
	}
	return (float)((s_rldGltfSrgbEdge[b - 1u] + s_rldGltfSrgbEdge[b]) * 0.5);
}

// A test model read as `path`. 1 = read; the said codes stay for the test.
static int RldGltf_TestRead(const char *path, const struct RldGltfTestModel *t, struct RldMkPly *ply, struct RldMkModelInfo *info)
{
	size_t size = 0;
	struct RldMkBuffer bin;
	u8 *file;
	int read;

	memset(&bin, 0, sizeof(bin));
	file = RldGltf_TestFile(t, &size, &bin);
	read = (file != NULL) && RldMk_TestImport(path, (const char *)file, size, ply, info, RLDMK_ROLE_MAIN);
	free(file);
	free(bin.data);
	return read;
}

// The standard model, its faces as fans of triangles: as GLB and as OBJ
// text with the same float positions and sRGB colors - the same points,
// colors and faces, the same .rldchar.
static void RldGltf_SelfTestSame(struct RldMkTestMesh *mesh, const u8 *chri, size_t chriSize)
{
	float *xyz = (float *)malloc(RLDMK_TEST_VERTICES * 12u);
	float *rgb = (float *)malloc(RLDMK_TEST_VERTICES * 12u);
	u16 *index = (u16 *)malloc(RLDMK_TEST_FACES * 3u * 6u);
	struct RldGltfTestModel t;
	struct RldMkBuffer obj;
	struct RldMkPly fromGlb;
	struct RldMkPly fromObj;
	struct RldMkModelInfo info;
	struct RldMkOptions opt;
	struct RldMkBuild build[2];
	u8 *file[2] = {NULL, NULL};
	size_t fileSize[2] = {0, 0};
	u32 triangles = 0;
	u32 i;
	u32 k;
	int ok;

	memset(&t, 0, sizeof(t));
	memset(&obj, 0, sizeof(obj));
	memset(&fromGlb, 0, sizeof(fromGlb));
	memset(&fromObj, 0, sizeof(fromObj));
	memset(build, 0, sizeof(build));
	if ((xyz == NULL) || (rgb == NULL) || (index == NULL))
	{
		RldMk_Test(0, "glTF and OBJ", "out of memory");
		free(xyz);
		free(rgb);
		free(index);
		return;
	}
	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 1, 0.0, -1);
	RldMk_BufferText(&obj, "# made by the rldpack self-test\n");
	for (i = 0; i < mesh->vertexCount; i++)
	{
		for (k = 0; k < 3u; k++)
		{
			xyz[3u * i + k] = (float)mesh->xyz[i][k];
			rgb[3u * i + k] = RldGltf_TestLinear(mesh->rgb[i][k]);
		}
		RldMk_BufferText(&obj, "v %.17g %.17g %.17g %.9f %.9f %.9f\n", (double)xyz[3u * i], (double)xyz[3u * i + 1u], (double)xyz[3u * i + 2u],
		                 (double)mesh->rgb[i][0] / 255.0, (double)mesh->rgb[i][1] / 255.0, (double)mesh->rgb[i][2] / 255.0);
	}
	for (i = 0; i < mesh->faceCount; i++)
	{
		for (k = 1; (k + 1u) < mesh->cornerCount[i]; k++)
		{
			index[3u * triangles + 0u] = (u16)mesh->corners[i][0];
			index[3u * triangles + 1u] = (u16)mesh->corners[i][k];
			index[3u * triangles + 2u] = (u16)mesh->corners[i][k + 1u];
			RldMk_BufferText(&obj, "f %u %u %u\n", mesh->corners[i][0] + 1u, mesh->corners[i][k] + 1u, mesh->corners[i][k + 1u] + 1u);
			triangles++;
		}
	}
	t.xyz = xyz;
	t.rgb = rgb;
	t.count = mesh->vertexCount;
	t.index = index;
	t.triangles = triangles;
	t.mode = 4;

	ok = (mesh->vertexCount < 65536u) && RldGltf_TestRead("selftest/same.glb", &t, &fromGlb, &info);
	RldMk_Test(ok && (info.format == RLDMK_FORMAT_GLTF) && (info.colors == RLDMK_COLORS_VERTEX) && (info.groups == 1u) && !s_rldGltfMorphs.present &&
	               s_rldGltfMorphs.read,
	           "glTF: a GLB with COLOR_0 reads (format gltf, vertex colors, one group, no shape keys)", "read %d, format %d, colors %u, said: %s", ok,
	           info.format, info.colors, s_rldMkCodes);
	ok = ok && RldMk_TestImport("selftest/same.obj", (const char *)obj.data, obj.size, &fromObj, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && RldMk_TestSamePly(&fromGlb, &fromObj) && !s_rldGltfMorphs.read,
	           "glTF and OBJ of the same faces, float positions and sRGB colors: the same points, colors and faces (an OBJ read clears the shape keys)",
	           "read %d, %u/%u points, %u/%u faces", ok, fromGlb.vertexCount, fromObj.vertexCount, fromGlb.faceCount, fromObj.faceCount);
	ok = ok && RldMk_BuildModel(&fromGlb, &opt, &build[0]) && RldMk_BuildModel(&fromObj, &opt, &build[1]);
	if (ok)
	{
		file[0] = RldMk_Container(chri, chriSize, build[0].cmdl, build[0].cmdlSize, NULL, &fileSize[0]);
		file[1] = RldMk_Container(chri, chriSize, build[1].cmdl, build[1].cmdlSize, NULL, &fileSize[1]);
	}
	RldMk_Test(ok && (file[0] != NULL) && (file[1] != NULL) && (fileSize[0] == fileSize[1]) && (memcmp(file[0], file[1], fileSize[0]) == 0),
	           "glTF and OBJ of the same model: the same .rldchar", "built %d", ok);

	free(file[0]);
	free(file[1]);
	RldMk_FreeBuild(&build[0]);
	RldMk_FreeBuild(&build[1]);
	RldMk_FreePly(&fromGlb);
	RldMk_FreePly(&fromObj);
	free(obj.data);
	free(xyz);
	free(rgb);
	free(index);
}

// A square of two triangles, textured, on a grey tent below (closed).
static const float s_rldGltfTestXyz[5][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0.5f, 0.5f, -1}};
static const float s_rldGltfTestUv[5][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}, {0.5f, 0.5f}};
static const u16 s_rldGltfTestIndex[6][3] = {{0, 1, 2}, {0, 2, 3}, {1, 0, 4}, {2, 1, 4}, {3, 2, 4}, {0, 3, 4}};

// The texture in the file and as a file next to an OBJ: the same colors.
// A .gltf with its buffer in a file next to it. A texture not there: the
// warning of the OBJ reader, the model still read.
static void RldGltf_SelfTestTextures(void)
{
	static const float factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	static const char objText[] = "mtllib tex.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0.5 0.5 -1\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 0.5 0.5\n"
	                              "usemtl paint\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\nf 2/2 1/1 5/5\nf 3/3 2/2 5/5\nf 4/4 3/3 5/5\nf 1/1 4/4 5/5\n";
	static const char mtl[] = "newmtl paint\nmap_Kd tex.png\n";
	struct RldObjMemFile files[3];
	struct RldGltfTestModel t;
	struct RldMkPly a;
	struct RldMkPly b;
	struct RldMkModelInfo info;
	struct RldMkBuffer bin;
	size_t pngSize = 0;
	size_t size = 0;
	u8 *png = RldMk_TestPngRgb(2u, 2u, s_rldMkTestChecker, &pngSize);
	u8 *gltf;
	int ok;

	memset(&t, 0, sizeof(t));
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	memset(&bin, 0, sizeof(bin));
	t.xyz = &s_rldGltfTestXyz[0][0];
	t.uv = &s_rldGltfTestUv[0][0];
	t.count = 5u;
	t.index = &s_rldGltfTestIndex[0][0];
	t.triangles = 6u;
	t.factor = factor;
	t.png = png;
	t.pngSize = pngSize;
	t.mode = 4;

	files[0].path = "selftest/tex.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/tex.png";
	files[1].data = png;
	files[1].size = pngSize;
	s_rldObjFiles = files;
	s_rldObjFileCount = 2u;
	ok = (png != NULL) && RldGltf_TestRead("selftest/tex.glb", &t, &a, &info);
	RldMk_Test(ok && (info.textures == 1u) && (info.texturesOk == 1u) && (info.colors == RLDMK_COLORS_TEXTURE),
	           "glTF: a PNG in the GLB (bufferView) is read as the texture", "read %d, textures %u/%u, colors %u, said: %s", ok, info.texturesOk,
	           info.textures, info.colors, s_rldMkCodes);
	ok = ok && RldMk_TestImport("selftest/tex.obj", objText, sizeof(objText) - 1u, &b, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && RldMk_TestSamePly(&a, &b), "glTF and OBJ with the same texture: the same points, colors and faces", "read %d, %u/%u points", ok,
	           a.vertexCount, b.vertexCount);
	RldMk_FreePly(&a);
	RldMk_FreePly(&b);

	// A .gltf whose buffer is the file next to it.
	t.bufferUri = "tex.bin";
	gltf = RldGltf_TestFile(&t, &size, &bin);
	files[2].path = "selftest/tex.bin";
	files[2].data = bin.data;
	files[2].size = bin.size;
	s_rldObjFiles = files;
	s_rldObjFileCount = 3u;
	ok = (gltf != NULL) && RldMk_TestImport("selftest/tex.gltf", (const char *)gltf, size, &a, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && (info.format == RLDMK_FORMAT_GLTF) && (info.texturesOk == 1u) && (a.faceCount >= 6u),
	           "glTF: a .gltf with its buffer in a file next to it reads", "read %d, said: %s", ok, s_rldMkCodes);
	RldMk_FreePly(&a);
	s_rldObjFiles = files;
	s_rldObjFileCount = 2u;
	ok = (gltf != NULL) && !RldMk_TestImport("selftest/tex.gltf", (const char *)gltf, size, &a, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && RldMk_Said("model-gltf-broken"), "glTF: a .gltf whose buffer file is missing: model-gltf-broken", "refused %d, said: %s", ok,
	           s_rldMkCodes);
	RldMk_FreePly(&a);
	free(gltf);
	free(bin.data);
	memset(&bin, 0, sizeof(bin));

	// A texture file that is not there.
	t.bufferUri = NULL;
	t.png = NULL;
	t.uri = "not%20there.png";
	ok = RldGltf_TestRead("selftest/missing.glb", &t, &a, &info);
	RldMk_Test(ok && RldMk_Said("tex-missing") && (info.textures == 1u) && (info.texturesOk == 0u),
	           "glTF: a texture file that is not there - the model is read, tex-missing", "read %d, said: %s", ok, s_rldMkCodes);
	RldMk_FreePly(&a);

	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	RldGltf_FreeFiles();
	free(png);
}

// Shape keys: names in any letter case, a sparse target, the node's scale,
// an unknown name; and their deltas per point through the split of textured
// triangles (a delta linear in x stays linear in x at every point).
static void RldGltf_SelfTestKeys(void)
{
	enum
	{
		CELLS = 4,
		GRID = (CELLS + 1) * (CELLS + 1)
	};
	static float xyz[GRID + 1][3];
	static float uv[GRID + 1][2];
	static u16 index[2 * CELLS * CELLS + 4 * CELLS][3];
	static float left[GRID + 1][3];
	static float lean[GRID + 1][3];
	static float smile[GRID + 1][3];
	static float bump[GRID + 1][3];
	static const float factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	struct RldGltfTestTarget targets[4];
	struct RldGltfTestModel t;
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	u8 noise[16u * 16u * 3u];
	size_t pngSize = 0;
	u8 *png;
	u32 triangles = 0;
	u32 i;
	u32 j;
	u32 bad = 0;
	int ok;

	for (i = 0; i < (u32)sizeof(noise); i++)
	{
		noise[i] = (u8)((i * 2654435761u) >> 24);
	}
	png = RldMk_TestPngRgb(16u, 16u, noise, &pngSize);
	memset(left, 0, sizeof(left));
	memset(smile, 0, sizeof(smile));
	for (j = 0; j <= (u32)CELLS; j++)
	{
		for (i = 0; i <= (u32)CELLS; i++)
		{
			const u32 v = j * (CELLS + 1u) + i;

			xyz[v][0] = (float)i / (float)CELLS;
			xyz[v][1] = (float)j / (float)CELLS;
			xyz[v][2] = 0.0f;
			uv[v][0] = xyz[v][0];
			uv[v][1] = 1.0f - xyz[v][1];
			// steer_left moves the right column only (sparse); lean is 0.25 x.
			left[v][0] = (i == (u32)CELLS) ? 0.125f : 0.0f;
			lean[v][0] = 0.25f * xyz[v][0];
			lean[v][1] = 0.0f;
			lean[v][2] = 0.0f;
		}
	}
	xyz[GRID][0] = 0.5f;
	xyz[GRID][1] = 0.5f;
	xyz[GRID][2] = -1.0f;
	uv[GRID][0] = 0.5f;
	uv[GRID][1] = 0.5f;
	lean[GRID][0] = 0.125f;
	smile[0][1] = 0.5f;
	// crash moves the inner point (2, 2) alone, by 0.5 up.
	memset(bump, 0, sizeof(bump));
	bump[2u * (CELLS + 1u) + 2u][1] = 0.5f;
	for (j = 0; j < (u32)CELLS; j++)
	{
		for (i = 0; i < (u32)CELLS; i++)
		{
			const u16 a = (u16)(j * (CELLS + 1u) + i);
			const u16 c = (u16)(a + CELLS + 1u);

			index[triangles][0] = a;
			index[triangles][1] = (u16)(a + 1u);
			index[triangles++][2] = (u16)(c + 1u);
			index[triangles][0] = a;
			index[triangles][1] = (u16)(c + 1u);
			index[triangles++][2] = c;
		}
	}
	// The tent below: the rim to the point under the middle.
	for (i = 0; i < (u32)CELLS; i++)
	{
		const u16 bottom = (u16)i;
		const u16 top = (u16)(CELLS * (CELLS + 1u) + i);
		const u16 west = (u16)(i * (CELLS + 1u));
		const u16 east = (u16)(i * (CELLS + 1u) + CELLS);

		index[triangles][0] = (u16)(bottom + 1u);
		index[triangles][1] = bottom;
		index[triangles++][2] = (u16)GRID;
		index[triangles][0] = top;
		index[triangles][1] = (u16)(top + 1u);
		index[triangles++][2] = (u16)GRID;
		index[triangles][0] = west;
		index[triangles][1] = (u16)(west + CELLS + 1u);
		index[triangles++][2] = (u16)GRID;
		index[triangles][0] = (u16)(east + CELLS + 1u);
		index[triangles][1] = east;
		index[triangles++][2] = (u16)GRID;
	}

	targets[0].name = "Steer_Left";
	targets[0].delta = &left[0][0];
	targets[0].sparse = 1;
	targets[1].name = "JUMP";
	targets[1].delta = &lean[0][0];
	targets[1].sparse = 0;
	targets[2].name = "smile";
	targets[2].delta = &smile[0][0];
	targets[2].sparse = 0;
	targets[3].name = "Cr\\u0061sh";   // "Crash" as JSON may write it
	targets[3].delta = &bump[0][0];
	targets[3].sparse = 0;
	memset(&t, 0, sizeof(t));
	t.xyz = &xyz[0][0];
	t.uv = &uv[0][0];
	t.count = GRID + 1u;
	t.index = &index[0][0];
	t.triangles = triangles;
	t.targets = targets;
	t.targetCount = 4u;
	t.factor = factor;
	t.png = png;
	t.pngSize = pngSize;
	t.node = "\"scale\":[2,2,2],";
	t.mode = 4;

	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	{
		static const struct RldObjMemFile none = {"", NULL, 0};

		// The images in memory (an empty table stands for the disk).
		s_rldObjFiles = &none;
		s_rldObjFileCount = 0;
	}
	RldObj_ResetOptions();
	ok = (png != NULL) && RldObj_Argument("--texture-detail", "500") && RldGltf_TestRead("selftest/keys.glb", &t, &ply, &info);
	RldObj_ResetOptions();
	RldMk_Test(ok && s_rldGltfMorphs.read &&
	               (s_rldGltfMorphs.present == ((1u << RLDGLTF_KEY_STEER_LEFT) | (1u << RLDGLTF_KEY_JUMP) | (1u << RLDGLTF_KEY_CRASH))) && (s_rldGltfMorphs.unknownCount == 1u) &&
	               (strcmp(s_rldGltfMorphs.unknown[0], "smile") == 0) && RldMk_Said("pose-unknown-key") && (s_rldGltfMorphs.pointCount == ply.vertexCount) &&
	               (s_rldGltfMorphs.sourceCount == GRID + 1u) && (ply.faceCount > triangles) && (ply.faceSource != NULL),
	           "glTF shape keys: Steer_Left (sparse), JUMP and Cr\\u0061sh are keys, smile is pose-unknown-key; the textured square was split",
	           "read %d, present %x, unknown %u, %u points, %u faces, said: %s", ok, s_rldGltfMorphs.present, s_rldGltfMorphs.unknownCount, ply.vertexCount,
	           ply.faceCount, s_rldMkCodes);

	// Every point: jump = 0.25 x (the node scales positions and deltas by 2
	// alike), steer_left = 0.25 at x = 2 and 0 at x = 0.
	for (i = 0; ok && (i < ply.vertexCount); i++)
	{
		const double x = ply.xyz[3u * i];
		const double *jump = &s_rldGltfMorphs.point[RLDGLTF_KEY_JUMP][3u * i];
		const double *steer = &s_rldGltfMorphs.point[RLDGLTF_KEY_STEER_LEFT][3u * i];
		const double want = 0.25 * x;
		double d = jump[0] - want;

		d = (d < 0.0) ? -d : d;
		bad += ((d > 1e-9) || (jump[1] != 0.0) || (jump[2] != 0.0)) ? 1u : 0u;
		if ((x == 2.0) && (ply.xyz[3u * i + 2u] == 0.0))
		{
			bad += ((steer[0] < 0.25 - 1e-9) || (steer[0] > 0.25 + 1e-9)) ? 1u : 0u;
		}
		if (x == 0.0)
		{
			bad += ((steer[0] < -1e-9) || (steer[0] > 1e-9)) ? 1u : 0u;
		}
		// crash: the hat of the inner point over its six triangles (the
		// diagonals run along +x +y): 1 - max(|dx|, |dy|, |dx - dy|) in
		// cells, 0 outside and on the tent. A point of a split triangle
		// placed in the wrong triangle would miss it.
		{
			const double *crash = &s_rldGltfMorphs.point[RLDGLTF_KEY_CRASH][3u * i];
			const double dx = ply.xyz[3u * i] * (double)CELLS / 2.0 - 2.0;
			const double dy = ply.xyz[3u * i + 1u] * (double)CELLS / 2.0 - 2.0;
			const double ax = (dx < 0.0) ? -dx : dx;
			const double ay = (dy < 0.0) ? -dy : dy;
			const double axy = ((dx - dy) < 0.0) ? (dy - dx) : (dx - dy);
			double far = (ax > ay) ? ax : ay;
			double hat;

			far = (axy > far) ? axy : far;
			hat = (ply.xyz[3u * i + 2u] != 0.0) ? 0.0 : ((far >= 1.0) ? 0.0 : (1.0 - far));
			d = crash[1] - hat;
			d = (d < 0.0) ? -d : d;
			bad += ((d > 1e-9) || (crash[0] != 0.0) || (crash[2] != 0.0)) ? 1u : 0u;
		}
	}
	for (i = 0; ok && (i <= GRID); i++)
	{
		bad += (s_rldGltfMorphs.source[RLDGLTF_KEY_JUMP][3u * i] != 2.0 * (double)lean[i][0]) ? 1u : 0u;
	}
	RldMk_Test(ok && (bad == 0u), "glTF shape keys: the deltas of every point (split ones by their place) and every source v, scaled by the node",
	           "%u wrong", bad);
	RldMk_FreePly(&ply);

	// The poses read a glTF too; a mask does not touch the shape keys.
	ok = RldGltf_TestRead("selftest/keys.glb", &t, &ply, &info);
	RldMk_FreePly(&ply);
	{
		size_t size = 0;
		struct RldMkBuffer bin;
		u8 *glb;

		memset(&bin, 0, sizeof(bin));
		glb = RldGltf_TestFile(&t, &size, &bin);
		ok = ok && (glb != NULL) && RldMk_TestImport("selftest/keys.glb", (const char *)glb, size, &ply, &info, RLDMK_ROLE_MASK) && s_rldGltfMorphs.read;
		RldMk_FreePly(&ply);
		ok = ok && (glb != NULL) && RldMk_TestImport("selftest/keys.glb", (const char *)glb, size, &ply, &info, RLDMK_ROLE_POSE) && s_rldGltfMorphs.read &&
		     (s_rldGltfMorphs.pointCount == ply.vertexCount);
		RldMk_FreePly(&ply);
		free(glb);
	}
	RldMk_Test(ok, "glTF shape keys: read for the poses as well, a mask leaves them", "said: %s", s_rldMkCodes);
	RldGltf_FreeMorphs();

	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	RldGltf_FreeFiles();
	free(png);
}

// What is refused: a rig, a broken file, no mesh, lines.
static void RldGltf_SelfTestRefused(void)
{
	static const char garbage[] = "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":7}}]}]}";
	static const char empty[] = "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[]}]}";
	struct RldGltfTestModel t;
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	struct RldMkBuffer bin;
	size_t size = 0;
	u8 *glb;
	int ok;

	memset(&t, 0, sizeof(t));
	memset(&bin, 0, sizeof(bin));
	t.xyz = &s_rldGltfTestXyz[0][0];
	t.count = 5u;
	t.index = &s_rldGltfTestIndex[0][0];
	t.triangles = 6u;
	t.mode = 4;
	t.skin = 1;
	ok = !RldGltf_TestRead("selftest/rig.glb", &t, &ply, &info);
	RldMk_Test(ok && RldMk_Said("model-rig") && (ply.xyz == NULL) && !s_rldGltfMorphs.read, "glTF: a skin is refused (model-rig)", "said: %s", s_rldMkCodes);
	RldMk_FreePly(&ply);

	t.skin = 0;
	t.mode = 1;
	ok = !RldGltf_TestRead("selftest/lines.glb", &t, &ply, &info);
	RldMk_Test(ok && RldMk_Said("model-gltf-mode"), "glTF: lines are refused (model-gltf-mode)", "said: %s", s_rldMkCodes);
	RldMk_FreePly(&ply);

	t.mode = 4;
	glb = RldGltf_TestFile(&t, &size, &bin);
	ok = (glb != NULL) && !RldMk_TestImport("selftest/cut.glb", (const char *)glb, size / 2u, &ply, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && RldMk_Said("model-gltf-broken"), "glTF: a GLB cut in half is refused (model-gltf-broken)", "said: %s", s_rldMkCodes);
	RldMk_FreePly(&ply);
	free(glb);

	ok = !RldMk_TestImport("selftest/garbage.gltf", garbage, sizeof(garbage) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && RldMk_Said("model-gltf-broken"), "glTF: an accessor that is not there is refused (model-gltf-broken)", "said: %s", s_rldMkCodes);
	RldMk_FreePly(&ply);

	ok = !RldMk_TestImport("selftest/empty.gltf", empty, sizeof(empty) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && RldMk_Said("model-gltf-empty"), "glTF: a scene without a mesh is refused (model-gltf-empty)", "said: %s", s_rldMkCodes);
	RldMk_FreePly(&ply);

	{
		char shown[8];
		u32 clamped = 0;
		u32 b;
		int same = 1;

		for (b = 0; b < 256u; b++)
		{
			same &= (RldMk_PlyColor(RLDMK_PLY_FLOAT64, RldGltf_Srgb((double)RldGltf_TestLinear(b), &clamped)) == (u8)b);
		}
		snprintf(shown, sizeof(shown), "%u", clamped);
		RldMk_Test(same && (clamped == 0u) && (RldGltf_Srgb(0.214041140f, &clamped) == 128.0 / 255.0),
		           "glTF: COLOR_0 linear -> the sRGB byte of every value (0.2140 -> 128)", "clamped %s", shown);
	}
	RldGltf_FreeFiles();
}

// A hand-made .gltf at selftest/h/m.gltf: three points, one triangle, its
// buffer t.bin (44 bytes) or what `uri` names. The parts given are put in.
static int RldGltf_TestHand(const char *prim, const char *top, const char *viewOffset, const char *indexCount, const char *accessor, const char *uri,
                            struct RldMkPly *ply)
{
	struct RldMkModelInfo info;
	struct RldMkBuffer json;
	int read;

	memset(&json, 0, sizeof(json));
	// In pieces: RldMk_BufferText makes at most a line of text at a time.
	RldMk_BufferText(&json, "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],");
	RldMk_BufferText(&json, "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1");
	RldMk_BufferBytes(&json, prim, strlen(prim));
	RldMk_BufferText(&json, "}]}],");
	RldMk_BufferBytes(&json, top, strlen(top));
	RldMk_BufferText(&json, "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%s,\"byteLength\":36},", viewOffset);
	RldMk_BufferText(&json, "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":0,\"byteLength\":8}],");
	RldMk_BufferText(&json, "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},");
	RldMk_BufferText(&json, "{\"bufferView\":1,\"componentType\":5123,\"count\":%s,\"type\":\"SCALAR\"}", indexCount);
	RldMk_BufferBytes(&json, accessor, strlen(accessor));
	RldMk_BufferText(&json, "],\"buffers\":[{\"uri\":\"%s\",\"byteLength\":44}]}", uri);
	read = (json.data != NULL) && RldMk_TestImport("selftest/h/m.gltf", (const char *)json.data, json.size, ply, &info, RLDMK_ROLE_MAIN);
	free(json.data);
	return read;
}

// What a file must not do: a buffer file shorter than it says, sizes that
// wrap in 32 bits, URIs out of the model's folder, deltas that are no
// numbers or far too large; the tables of v for any count of points.
static void RldGltf_SelfTestGuards(void)
{
	static const char sparseWrap[] = ",{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":1073741825,\"indices\":{\"bufferView\":2,"
	                                 "\"componentType\":5125},\"values\":{\"bufferView\":0}}}";
	static const char image[] = "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],\"textures\":[{\"source\":0}],"
	                            "\"images\":[{\"uri\":\"%s\"}],";
	static const char *const outside[] = {"../t.bin", "%2e%2e/t.bin", "a/../../t.bin", "C:/t.bin", "/t.bin", "//server/share/t.bin", "file:t.bin"};
	u8 bin[44];
	struct RldObjMemFile files[2];
	struct RldMkPly ply;
	struct RldGltfTable table;
	char top[256];
	u32 i;
	int ok;

	memset(bin, 0, sizeof(bin));
	{
		static const float xyz[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
		static const u16 index[3] = {0, 1, 2};

		memcpy(bin, xyz, sizeof(xyz));
		memcpy(&bin[36], index, sizeof(index));
	}
	files[0].path = "selftest/h/t.bin";
	files[0].data = bin;
	files[0].size = sizeof(bin);
	files[1].path = "selftest/h/short.bin";
	files[1].data = bin;
	files[1].size = 20u;
	s_rldObjFiles = files;
	s_rldObjFileCount = 2u;

	ok = RldGltf_TestHand("", "", "0", "3", "", "t.bin", &ply) && (ply.faceCount == 1u);
	RldMk_FreePly(&ply);
	RldMk_Test(ok, "glTF guards: the hand-made .gltf itself reads", "said: %s", s_rldMkCodes);

	ok = !RldGltf_TestHand("", "", "0", "3", "", "short.bin", &ply) && RldMk_Said("model-gltf-broken");
	RldMk_FreePly(&ply);
	RldMk_Test(ok, "glTF guards: a buffer file shorter than its byteLength is refused (model-gltf-broken)", "said: %s", s_rldMkCodes);

	ok = !RldGltf_TestHand("", "", "0", "2147483649", "", "t.bin", &ply) && RldMk_Said("model-gltf-broken");
	RldMk_FreePly(&ply);
	ok = ok && !RldGltf_TestHand(",\"targets\":[{\"POSITION\":2}]", "", "0", "3", sparseWrap, "t.bin", &ply) && RldMk_Said("model-gltf-broken");
	RldMk_FreePly(&ply);
	ok = ok && !RldGltf_TestHand("", "", "4294963200", "3", "", "t.bin", &ply) && RldMk_Said("model-gltf-broken");
	RldMk_FreePly(&ply);
	RldMk_Test(ok, "glTF guards: an index count, a sparse count and a view offset that wrap in 32 bits are refused (model-gltf-broken)", "said: %s",
	           s_rldMkCodes);

	for (i = 0; i < (u32)(sizeof(outside) / sizeof(outside[0])); i++)
	{
		const int refused = !RldGltf_TestHand("", "", "0", "3", "", outside[i], &ply) && RldMk_Said("model-gltf-uri");

		RldMk_FreePly(&ply);
		if (!refused)
		{
			RldMk_Test(0, "glTF guards: a buffer outside the model's folder", "%s read, said: %s", outside[i], s_rldMkCodes);
		}
		ok = ok && refused;
	}
	snprintf(top, sizeof(top), image, "../x.png");
	ok = ok && !RldGltf_TestHand(",\"material\":0", top, "0", "3", "", "t.bin", &ply) && RldMk_Said("model-gltf-uri");
	RldMk_FreePly(&ply);
	snprintf(top, sizeof(top), image, "C:/x.png");
	ok = ok && !RldGltf_TestHand(",\"material\":0", top, "0", "3", "", "t.bin", &ply) && RldMk_Said("model-gltf-uri");
	RldMk_FreePly(&ply);
	snprintf(top, sizeof(top), image, "maps/x%20y.png");
	ok = ok && RldGltf_TestHand(",\"material\":0", top, "0", "3", "", "t.bin", &ply) && RldMk_Said("tex-missing") && !RldMk_Said("model-gltf-uri");
	RldMk_FreePly(&ply);
	RldMk_Test(ok, "glTF guards: buffer and image URIs out of the model's folder are refused (model-gltf-uri), one below it is read", "said: %s",
	           s_rldMkCodes);

	// Deltas that are no number, or far larger than the model.
	{
		static float delta[5][3];
		const u32 nanBits = 0x7FC00000u;
		struct RldGltfTestTarget target;
		struct RldGltfTestModel t;
		struct RldMkModelInfo info;

		memset(delta, 0, sizeof(delta));
		memcpy(&delta[1][0], &nanBits, sizeof(float));
		target.name = "jump";
		target.delta = &delta[0][0];
		target.sparse = 0;
		memset(&t, 0, sizeof(t));
		t.xyz = &s_rldGltfTestXyz[0][0];
		t.count = 5u;
		t.index = &s_rldGltfTestIndex[0][0];
		t.triangles = 6u;
		t.targets = &target;
		t.targetCount = 1u;
		t.mode = 4;
		ok = !RldGltf_TestRead("selftest/nan.glb", &t, &ply, &info) && RldMk_Said("model-gltf-key") && !s_rldGltfMorphs.read;
		RldMk_FreePly(&ply);
		delta[1][0] = 1e30f;
		ok = ok && !RldGltf_TestRead("selftest/far.glb", &t, &ply, &info) && RldMk_Said("model-gltf-key");
		RldMk_FreePly(&ply);
		delta[1][0] = 3.0f;
		ok = ok && RldGltf_TestRead("selftest/near.glb", &t, &ply, &info) && !RldMk_Said("model-gltf-key");
		RldMk_FreePly(&ply);
		RldMk_Test(ok, "glTF guards: a shape key that is not a number or moves points by more than four times the model is refused (model-gltf-key)",
		           "said: %s", s_rldMkCodes);
	}

	// The table of v: sized in 64 bits, capped where obj-big begins, always
	// twice the points it can get - no count of points wraps it into a
	// table too small to find a free slot.
	memset(&table, 0, sizeof(table));
	ok = RldGltf_TableStart(&table, 0xFFFFFFFFull * 4296u) && (table.slotCount >= 2u * RLDMK_PLY_MAX) && ((table.slotCount & (table.slotCount - 1u)) == 0u);
	free(table.slots);
	memset(&table, 0, sizeof(table));
	ok = ok && RldGltf_TableStart(&table, 0x80000001u) && (table.slotCount >= 2u * RLDMK_PLY_MAX);
	free(table.slots);
	RldMk_Test(ok, "glTF guards: the table of points stays twice as large as the points it can hold for any count", "slots %u", table.slotCount);

	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	RldGltf_FreeFiles();
}

static void RldGltf_SelfTest(struct RldMkTestMesh *mesh, const u8 *chri, size_t chriSize)
{
	RldGltf_SelfTestSame(mesh, chri, chriSize);
	RldGltf_SelfTestTextures();
	RldGltf_SelfTestKeys();
	RldGltf_SelfTestRefused();
	RldGltf_SelfTestGuards();
	RldGltf_FreeMorphs();
}
