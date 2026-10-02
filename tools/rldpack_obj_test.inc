// rldpack_obj_test.inc - the self-tests of the import layer and the OBJ reader
// (tools/rldpack_import.inc, tools/rldpack_obj.inc). Included by
// tools/rldpack_char.inc before RldChar_SelfTest; every OBJ, MTL file and
// picture is made here, in memory.

// --- OBJ: the import layer and the OBJ reader -----------------------------------------
//
// Every OBJ, MTL file and picture below is made here, in memory; s_rldObjFiles
// stands in for the disk while they are read.

// GOLDEN OBJ: the model of RLDMK_GOLDEN_PLAIN written as OBJ with four
// materials - a Kd, a PNG without Kd, a TGA times a Kd, and a JPEG at an
// absolute path that is found next to the MTL file - built with --fit none.
// It follows deliberate changes of the chain like the hashes above, and of
// the color rule of the OBJ reader (tools/rldpack_obj.inc); nothing else may
// change it.
#define RLDMK_GOLDEN_OBJ "03f73080168ecc7ac50076496400f594c5b27da5cdc86f3792f577c06b586c5d"

// A JPEG of 16 x 16 pixels in four squares of 8 x 8: (200, 40, 40) and
// (40, 200, 40) at the top, (40, 40, 200) and (230, 230, 230) below; quality
// 90, no chroma subsampling.
static const u8 s_rldMkTestJpeg[303] = {
	0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03,
	0x03, 0x03, 0x03, 0x04, 0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0A, 0x07,
	0x07, 0x06, 0x08, 0x0C, 0x0A, 0x0C, 0x0C, 0x0B, 0x0A, 0x0B, 0x0B, 0x0D, 0x0E, 0x12, 0x10, 0x0D,
	0x0E, 0x11, 0x0E, 0x0B, 0x0B, 0x10, 0x16, 0x10, 0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0C, 0x0F,
	0x17, 0x18, 0x16, 0x14, 0x18, 0x12, 0x14, 0x15, 0x14, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x03, 0x04,
	0x04, 0x05, 0x04, 0x05, 0x09, 0x05, 0x05, 0x09, 0x14, 0x0D, 0x0B, 0x0D, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xFF, 0xC0,
	0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03, 0x01, 0x11, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
	0x01, 0xFF, 0xC4, 0x00, 0x16, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x08, 0x09, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
	0xC4, 0x00, 0x17, 0x01, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x07, 0x08, 0x09, 0x06, 0xFF, 0xC4, 0x00, 0x14, 0x11, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA,
	0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00, 0x14, 0x0D, 0xCE, 0xA9,
	0xEC, 0x3A, 0x47, 0x74, 0x68, 0xAC, 0x46, 0x41, 0xB9, 0x64, 0xA9, 0xA3, 0x7F, 0xFF, 0xD9,
};

// A PNG of width x height RGB pixels (rows from the top): stored blocks, filter 0.
static u8 *RldMk_TestPngRgb(u32 width, u32 height, const u8 *rgb, size_t *sizeOut)
{
	const size_t rowBytes = (size_t)width * 3u + 1u;
	struct RldMkBuffer idat;
	struct RldMkBuffer png;
	u8 head[13];
	u8 *raw = (u8 *)malloc(rowBytes * height + 1u);
	u32 y;

	*sizeOut = 0;
	if (raw == NULL)
	{
		return NULL;
	}
	for (y = 0; y < height; y++)
	{
		raw[y * rowBytes] = 0;
		memcpy(&raw[y * rowBytes + 1u], &rgb[(size_t)y * width * 3u], (size_t)width * 3u);
	}

	memset(&idat, 0, sizeof(idat));
	memset(&png, 0, sizeof(png));
	RldMk_TestDeflate(&idat, raw, rowBytes * height, 0, rowBytes, 0);
	free(raw);

	RldMk_BufferBytes(&png, s_rldPngSignature, 8u);
	RldMk_PutBE32(&head[0], width);
	RldMk_PutBE32(&head[4], height);
	head[8] = 8;
	head[9] = 2;
	head[10] = 0;
	head[11] = 0;
	head[12] = 0;
	RldMk_TestChunk(&png, "IHDR", head, 13u);
	RldMk_TestChunk(&png, "IDAT", idat.data, (u32)idat.size);
	RldMk_TestChunk(&png, "IEND", NULL, 0u);
	free(idat.data);

	*sizeOut = png.size;
	return png.data;
}

// A TGA of width x height RGB pixels (rows given from the top): uncompressed,
// 24 bits, stored from the bottom row up as most programs write it.
static u8 *RldMk_TestTga(u32 width, u32 height, const u8 *rgb, size_t *sizeOut)
{
	struct RldMkBuffer tga;
	u8 head[18];
	u32 x;
	u32 y;

	memset(&tga, 0, sizeof(tga));
	memset(head, 0, sizeof(head));
	head[2] = 2;
	head[12] = (u8)width;
	head[13] = (u8)(width >> 8);
	head[14] = (u8)height;
	head[15] = (u8)(height >> 8);
	head[16] = 24;
	RldMk_BufferBytes(&tga, head, sizeof(head));
	for (y = height; y-- > 0u;)
	{
		for (x = 0; x < width; x++)
		{
			const u8 *p = &rgb[((size_t)y * width + x) * 3u];
			const u8 bgr[3] = {p[2], p[1], p[0]};

			RldMk_BufferBytes(&tga, bgr, 3u);
		}
	}
	*sizeOut = tga.size;
	return tga.data;
}

// A model in memory read as `path`. 1 = read; the said codes stay for the test.
static int RldMk_TestImport(const char *path, const char *text, size_t size, struct RldMkPly *ply, struct RldMkModelInfo *info, u32 role)
{
	RldMk_ResetSaid();
	return RldMk_ImportModel(path, (const u8 *)text, size, ply, info, role);
}

// An OBJ text that reads as an error: does it say `code`?
static int RldMk_TestObjError(const char *path, const char *text, size_t size, const char *code)
{
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	const int read = RldMk_TestImport(path, text, size, &ply, &info, RLDMK_ROLE_MAIN);

	RldMk_FreePly(&ply);
	return !read && RldMk_Said(code);
}

// The color of corner k of face f as 0xRRGGBB.
static u32 RldMk_TestCornerRgb(const struct RldMkPly *ply, u32 f, u32 k)
{
	const u32 p = ply->corners[4u * f + k];

	return ((u32)ply->rgb[3u * p] << 16) | ((u32)ply->rgb[3u * p + 1u] << 8) | ply->rgb[3u * p + 2u];
}

// A test mesh as OBJ text with its vertex colors: a byte order mark, CR LF,
// one line continued, every face form and every third face with negative
// indices, groups for kart, driver and steering wheel of RldMk_TestStandard.
static u8 *RldMk_TestObjText(const struct RldMkTestMesh *mesh, size_t *sizeOut)
{
	struct RldMkBuffer b;
	u32 i;
	u32 k;

	memset(&b, 0, sizeof(b));
	RldMk_BufferText(&b, "\xEF\xBB\xBF# made by the rldpack self-test\r\n");
	for (i = 0; i < mesh->vertexCount; i++)
	{
		const double *p = mesh->xyz[i];
		const u8 *c = mesh->rgb[i];

		RldMk_BufferText(&b, (i == 0u) ? "v %.6f %.6f \\\r\n   %.6f %.9f %.9f %.9f\r\n" : "v %.6f %.6f %.6f %.9f %.9f %.9f\r\n", p[0], p[1], p[2],
		                 (double)c[0] / 255.0, (double)c[1] / 255.0, (double)c[2] / 255.0);
	}
	RldMk_BufferText(&b, "vt 0.25 0.75\r\nvn 0 1 0\r\ns off\r\n");

	for (i = 0; i < mesh->faceCount; i++)
	{
		const int negative = ((i % 3u) == 1u);
		const int other = negative ? -1 : 1;

		if ((i == 0u) || (i == 6u) || (i == 30u))
		{
			RldMk_BufferText(&b, "%s\r\n", (i == 0u) ? "o kart" : ((i == 6u) ? "g driver" : "g steering wheel"));
		}
		RldMk_BufferText(&b, "f");
		for (k = 0; k < mesh->cornerCount[i]; k++)
		{
			const u32 v = mesh->corners[i][k];
			const long index = negative ? -(long)(mesh->vertexCount - v) : ((long)v + 1L);

			switch (i % 4u)
			{
			case 0u:
				RldMk_BufferText(&b, " %ld", index);
				break;
			case 1u:
				RldMk_BufferText(&b, " %ld/%d", index, other);
				break;
			case 2u:
				RldMk_BufferText(&b, " %ld//%d", index, other);
				break;
			default:
				RldMk_BufferText(&b, " %ld/%d/%d", index, other, other);
				break;
			}
		}
		RldMk_BufferText(&b, "\r\n");
	}

	*sizeOut = b.size;
	return b.data;
}

// The same OBJ and PLY: the PLY with its vertices in the order the faces
// first use them (the order of the OBJ reader's points) gives the same
// .rldchar; the OBJ built twice gives the same bytes.
static void RldMk_SelfTestObjSame(struct RldMkTestMesh *mesh, const u8 *chri, size_t chriSize)
{
	struct RldMkTestMesh *ordered = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	u32 *map = (u32 *)malloc(RLDMK_TEST_VERTICES * sizeof(u32));
	struct RldMkOptions opt;
	struct RldMkPly fromPly;
	struct RldMkPly fromObj;
	struct RldMkPly again;
	struct RldMkModelInfo info;
	struct RldMkBuild buildPly;
	struct RldMkBuild buildObj;
	struct RldMkBuild buildAgain;
	u8 *obj = NULL;
	u8 *ply = NULL;
	u8 *file[3] = {NULL, NULL, NULL};
	size_t fileSize[3] = {0, 0, 0};
	size_t objSize = 0;
	size_t plySize = 0;
	u32 i;
	u32 k;
	int ok;

	memset(&fromPly, 0, sizeof(fromPly));
	memset(&fromObj, 0, sizeof(fromObj));
	memset(&again, 0, sizeof(again));
	memset(&buildPly, 0, sizeof(buildPly));
	memset(&buildObj, 0, sizeof(buildObj));
	memset(&buildAgain, 0, sizeof(buildAgain));
	if ((ordered == NULL) || (map == NULL))
	{
		RldMk_Test(0, "OBJ and PLY", "out of memory");
		free(ordered);
		free(map);
		return;
	}

	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 1, 0.0, -1);

	for (i = 0; i < mesh->vertexCount; i++)
	{
		map[i] = RLDMK_NONE;
	}
	for (i = 0; i < mesh->faceCount; i++)
	{
		for (k = 0; k < mesh->cornerCount[i]; k++)
		{
			const u32 v = mesh->corners[i][k];

			if (map[v] == RLDMK_NONE)
			{
				map[v] = ordered->vertexCount;
				memcpy(ordered->xyz[ordered->vertexCount], mesh->xyz[v], sizeof(mesh->xyz[v]));
				memcpy(ordered->rgb[ordered->vertexCount], mesh->rgb[v], sizeof(mesh->rgb[v]));
				ordered->vertexCount++;
			}
			ordered->corners[i][k] = map[v];
		}
		ordered->cornerCount[i] = mesh->cornerCount[i];
	}
	ordered->faceCount = mesh->faceCount;

	obj = RldMk_TestObjText(mesh, &objSize);
	ply = RldMk_TestPly(ordered, 0, 1, 0, &plySize);
	ok = (obj != NULL) && (ply != NULL);

	RldMk_ResetSaid();
	ok = ok && RldMk_ParsePly(ply, plySize, &fromPly) && RldMk_BuildModel(&fromPly, &opt, &buildPly);
	ok = ok && RldMk_TestImport("selftest/same.obj", (const char *)obj, objSize, &fromObj, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && (info.format == RLDMK_FORMAT_OBJ) && (info.colors == RLDMK_COLORS_VERTEX) && (info.groups == 3u) && (info.mtl == RLDMK_MTL_NONE) &&
	               (fromObj.vertexCount == fromPly.vertexCount) && (fromObj.faceCount == fromPly.faceCount) && RldMk_Said("obj-ignored") &&
	               (memcmp(fromObj.xyz, fromPly.xyz, (size_t)fromPly.vertexCount * 3u * sizeof(double)) == 0) &&
	               (memcmp(fromObj.rgb, fromPly.rgb, (size_t)fromPly.vertexCount * 3u) == 0) &&
	               (memcmp(fromObj.corners, fromPly.corners, (size_t)fromPly.faceCount * 4u * sizeof(u32)) == 0),
	           "OBJ (BOM, CR LF, a continued line, all face forms, negative indices, groups, vertex colors) reads as its PLY",
	           "read %d, format %d, colors %u, groups %u, %u/%u points, said: %s", ok, info.format, info.colors, info.groups, fromObj.vertexCount,
	           fromPly.vertexCount, s_rldMkCodes);

	ok = ok && RldMk_BuildModel(&fromObj, &opt, &buildObj);
	ok = ok && RldMk_TestImport("selftest/same.obj", (const char *)obj, objSize, &again, &info, RLDMK_ROLE_MAIN) &&
	     RldMk_BuildModel(&again, &opt, &buildAgain);
	if (ok)
	{
		file[0] = RldMk_Container(chri, chriSize, buildPly.cmdl, buildPly.cmdlSize, NULL, &fileSize[0]);
		file[1] = RldMk_Container(chri, chriSize, buildObj.cmdl, buildObj.cmdlSize, NULL, &fileSize[1]);
		file[2] = RldMk_Container(chri, chriSize, buildAgain.cmdl, buildAgain.cmdlSize, NULL, &fileSize[2]);
	}
	RldMk_Test(ok && (file[0] != NULL) && (file[1] != NULL) && (fileSize[0] == fileSize[1]) && (memcmp(file[0], file[1], fileSize[0]) == 0),
	           "OBJ and PLY of the same model and colors: the same .rldchar", "built %d", ok);
	RldMk_Test(ok && (file[2] != NULL) && (fileSize[1] == fileSize[2]) && (memcmp(file[1], file[2], fileSize[1]) == 0),
	           "OBJ read and built twice: the same bytes", "built %d", ok);

	for (i = 0; i < 3u; i++)
	{
		free(file[i]);
	}
	RldMk_FreeBuild(&buildPly);
	RldMk_FreeBuild(&buildObj);
	RldMk_FreeBuild(&buildAgain);
	RldMk_FreePly(&fromPly);
	RldMk_FreePly(&fromObj);
	RldMk_FreePly(&again);
	free(obj);
	free(ply);
	free(ordered);
	free(map);
}

// The pictures of the tests: a 2 x 2 checker (red, green / blue, white) as
// PNG and as TGA, and s_rldMkTestJpeg.
static const u8 s_rldMkTestChecker[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

// The golden OBJ: materials, a PNG, a TGA and a JPEG on the standard model.
static void RldMk_SelfTestObjGolden(struct RldMkTestMesh *mesh, const u8 *chri, size_t chriSize)
{
	static const char mtl[] = "# made by the rldpack self-test\n"
	                          "newmtl kart\nKd 0.8 0.2 0.1\n"
	                          "newmtl driver\nmap_Kd -s 1 1 1 -bm 0.5 checker.png\n"
	                          "newmtl face\nKd 0.5 0.5 1\nmap_Kd checker.tga\n"
	                          "newmtl wheel\nKd 0.9 0.9 0.9\nmap_Kd C:/nowhere/quads.jpg\n";
	struct RldObjMemFile files[4];
	struct RldMkOptions opt;
	struct RldMkBuffer b;
	struct RldMkPly ply[2];
	struct RldMkModelInfo info;
	struct RldMkBuild build[2];
	u8 *png;
	u8 *tga;
	u8 *file[2] = {NULL, NULL};
	size_t fileSize[2] = {0, 0};
	size_t pngSize = 0;
	size_t tgaSize = 0;
	u32 i;
	u32 k;
	int ok = 1;
	int n;

	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	png = RldMk_TestPngRgb(2u, 2u, s_rldMkTestChecker, &pngSize);
	tga = RldMk_TestTga(2u, 2u, s_rldMkTestChecker, &tgaSize);

	memset(&b, 0, sizeof(b));
	RldMk_BufferText(&b, "# made by the rldpack self-test\nmtllib golden.mtl\n");
	for (i = 0; i < mesh->vertexCount; i++)
	{
		RldMk_BufferText(&b, "v %.6f %.6f %.6f\nvt %.6f %.6f\n", mesh->xyz[i][0], mesh->xyz[i][1], mesh->xyz[i][2], mesh->xyz[i][0] * 1.7 + 0.1,
		                 mesh->xyz[i][1] * 1.3 - mesh->xyz[i][2]);
	}
	// Kart 6 faces, driver 24 (its last side "face"), steering wheel 6.
	for (i = 0; i < mesh->faceCount; i++)
	{
		if ((i == 0u) || (i == 6u) || (i == 26u) || (i == 30u))
		{
			RldMk_BufferText(&b, "usemtl %s\n", (i == 0u) ? "kart" : ((i == 6u) ? "driver" : ((i == 26u) ? "face" : "wheel")));
		}
		RldMk_BufferText(&b, "f");
		for (k = 0; k < mesh->cornerCount[i]; k++)
		{
			RldMk_BufferText(&b, " %u/%u", mesh->corners[i][k] + 1u, mesh->corners[i][k] + 1u);
		}
		RldMk_BufferText(&b, "\n");
	}

	files[0].path = "selftest/golden.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/checker.png";
	files[1].data = png;
	files[1].size = pngSize;
	files[2].path = "selftest/checker.tga";
	files[2].data = tga;
	files[2].size = tgaSize;
	files[3].path = "selftest/quads.jpg";
	files[3].data = s_rldMkTestJpeg;
	files[3].size = sizeof(s_rldMkTestJpeg);
	s_rldObjFiles = files;
	s_rldObjFileCount = 4u;

	memset(ply, 0, sizeof(ply));
	memset(build, 0, sizeof(build));
	for (n = 0; n < 2; n++)
	{
		ok = ok && (png != NULL) && (tga != NULL) && (b.data != NULL) &&
		     RldMk_TestImport("selftest/golden.obj", (const char *)b.data, b.size, &ply[n], &info, RLDMK_ROLE_MAIN);
		if (n == 0)
		{
			RldMk_Test(ok && (info.mtl == RLDMK_MTL_OK) && (info.materials == 4u) && (info.textures == 3u) && (info.texturesOk == 3u) &&
			               (info.colors == (RLDMK_COLORS_MATERIAL | RLDMK_COLORS_TEXTURE)) && RldMk_Said("tex-found-nearby") && (s_rldMkWarnings == 0),
			           "OBJ with an MTL file: Kd, PNG, TGA, JPEG (an absolute path found next to the MTL file)",
			           "read %d, mtl %d, materials %u, textures %u/%u, colors %u, said: %s", ok, info.mtl, info.materials, info.texturesOk, info.textures,
			           info.colors, s_rldMkCodes);
		}
		ok = ok && RldMk_BuildModel(&ply[n], &opt, &build[n]);
		if (ok)
		{
			file[n] = RldMk_Container(chri, chriSize, build[n].cmdl, build[n].cmdlSize, NULL, &fileSize[n]);
		}
	}
	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;

	RldMk_Test(ok && (file[0] != NULL) && (file[1] != NULL) && (fileSize[0] == fileSize[1]) && (memcmp(file[0], file[1], fileSize[0]) == 0),
	           "golden OBJ built twice: the same bytes", "built %d", ok);
	if (Rld_CheckGolden("make-char --fit none from OBJ with materials and textures", file[0], fileSize[0], RLDMK_GOLDEN_OBJ) != 0)
	{
		s_rldMkTestFailed++;
	}

	for (n = 0; n < 2; n++)
	{
		free(file[n]);
		RldMk_FreeBuild(&build[n]);
		RldMk_FreePly(&ply[n]);
	}
	free(b.data);
	free(png);
	free(tga);
}

// The color rule, corner by corner, and the pictures as decoded.
static void RldMk_SelfTestObjColors(void)
{
	static const char mtl[] = "newmtl tex\nmap_Kd checker.png\n"
	                          "newmtl kdtex\nKd 0.5 0.25 1\nmap_Kd checker.png\n"
	                          "newmtl kd\nKd 0.2 0.4 0.6\n"
	                          "newmtl tga\nmap_Kd checker.tga\n"
	                          "newmtl jpeg\nmap_Kd quads.jpg\n";
	static const char obj[] = "mtllib \"rules.mtl\"\n"
	                          "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1 0.5 0.5 0.5\n"
	                          "vt 0.25 0.75\nvt 0.75 0.25\nvt 0.5 0.75\nvt 1.25 -0.25\n"
	                          "f 1 2 3\n"                    // 0: no material - grey
	                          "usemtl tex\nf 1/1 2/1 3/1\n"  // 1: the red texel, Kd 1
	                          "usemtl kdtex\nf 1/2 2/2 3/2\n" // 2: white times Kd
	                          "usemtl kd\nf 1 2 3\n"         // 3: Kd
	                          "usemtl tex\nf 4/1 2/1 3/1\n"  // 4: a vertex color times red
	                          "f 1 2 3\n"                    // 5: no vt - the color without the texture: grey
	                          "f 1/3 2/3 3/3\n"              // 6: halfway between red and green
	                          "f 1/4 2/4 3/4\n"              // 7: repeated: the red texel again
	                          "usemtl tga\nf 1/1 2/2 3/1\n"  // 8: TGA, red and white
	                          "usemtl jpeg\nf 1/1 2/2 3/1\n"; // 9: JPEG, about (200, 40, 40) and (230, 230, 230)
	static const u32 want[8][3] = {{0x808080u, 0x808080u, 0x808080u}, {0xFF0000u, 0xFF0000u, 0xFF0000u}, {0x8040FFu, 0x8040FFu, 0x8040FFu},
	                               {0x336699u, 0x336699u, 0x336699u}, {0x800000u, 0xFF0000u, 0xFF0000u}, {0x808080u, 0x808080u, 0x808080u},
	                               {0x808000u, 0x808000u, 0x808000u}, {0xFF0000u, 0xFF0000u, 0xFF0000u}};
	struct RldObjMemFile files[4];
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	u8 *png;
	u8 *tga;
	size_t pngSize = 0;
	size_t tgaSize = 0;
	u32 f;
	u32 k;
	int ok;
	int same = 1;

	png = RldMk_TestPngRgb(2u, 2u, s_rldMkTestChecker, &pngSize);
	tga = RldMk_TestTga(2u, 2u, s_rldMkTestChecker, &tgaSize);
	files[0].path = "selftest/rules.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/checker.png";
	files[1].data = png;
	files[1].size = pngSize;
	files[2].path = "selftest/checker.tga";
	files[2].data = tga;
	files[2].size = tgaSize;
	files[3].path = "selftest/quads.jpg";
	files[3].data = s_rldMkTestJpeg;
	files[3].size = sizeof(s_rldMkTestJpeg);
	s_rldObjFiles = files;
	s_rldObjFileCount = 4u;

	ok = (png != NULL) && (tga != NULL) && RldMk_TestImport("selftest/rules.obj", obj, sizeof(obj) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
	for (f = 0; ok && (f < 8u); f++)
	{
		for (k = 0; k < 3u; k++)
		{
			same = same && (RldMk_TestCornerRgb(&ply, f, k) == want[f][k]);
		}
	}
	RldMk_Test(ok && same && (info.colors == (RLDMK_COLORS_MATERIAL | RLDMK_COLORS_TEXTURE | RLDMK_COLORS_GREY)) && (info.noUv == 3u) &&
	               RldMk_Said("tex-no-uv") && RldMk_Said("obj-vertex-colors") && ply.hasColors && !ply.hasUv,
	           "OBJ colors: Kd, texture (bilinear, repeated), Kd x texture, vertex color x texture, grey, no vt",
	           "read %d, same %d, colors %u, no vt %u, said: %s", ok, same, info.colors, info.noUv, s_rldMkCodes);

	// TGA exactly; JPEG within its loss.
	if (ok)
	{
		const u32 jpegRed = RldMk_TestCornerRgb(&ply, 9u, 0u);
		const u32 jpegWhite = RldMk_TestCornerRgb(&ply, 9u, 1u);
		const int r = (int)(jpegRed >> 16);
		const int g = (int)((jpegRed >> 8) & 0xFFu);
		const int w = (int)(jpegWhite & 0xFFu);

		ok = (RldMk_TestCornerRgb(&ply, 8u, 0u) == 0xFF0000u) && (RldMk_TestCornerRgb(&ply, 8u, 1u) == 0xFFFFFFu) && (r > 188) && (r < 212) && (g > 28) &&
		     (g < 52) && (w > 218) && (w < 242);
	}
	RldMk_Test(ok, "OBJ textures TGA (bottom-up rows) and JPEG", "TGA %06X %06X, JPEG %06X %06X", RldMk_TestCornerRgb(&ply, 8u, 0u),
	           RldMk_TestCornerRgb(&ply, 8u, 1u), RldMk_TestCornerRgb(&ply, 9u, 0u), RldMk_TestCornerRgb(&ply, 9u, 1u));
	RldMk_FreePly(&ply);

	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	free(png);
	free(tga);
}

// The edges of a texture of 4 x 4 (left column red, right column green, the
// rest white): a face whose vt all lie in 0..1 is clamped, u = 0 is red and
// u = 1 green, also half a texel inside; a face tiled over u 0..2 repeats,
// u = 0 and u = 2 mix both edges as before. map_Kd -o 0.75 moves u 0.125 to
// the green column, -s 0.25 u 0.5 to the red one, -clamp on u 1.2..1.4 to
// the right edge.
static void RldMk_SelfTestObjEdges(void)
{
	static const char mtl[] = "newmtl edge\nmap_Kd edge.png\nnewmtl offs\nmap_Kd -o 0.75 0 0 edge.png\nnewmtl scal\nmap_Kd -s 0.25 1 1 edge.png\n"
	                          "newmtl clmp\nmap_Kd -clamp on edge.png\n";
	static const char obj[] = "mtllib edge.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
	                          "vt 0 0.5\nvt 1 0.5\nvt 0.5 0.5\nvt 0.1 0.5\nvt 0.9 0.5\nvt 2 0.5\nvt 1.5 0.5\nvt 0.125 0.5\nvt 1.2 0.5\nvt 1.4 0.5\n"
	                          "usemtl edge\nf 1/1 2/2 3/3\nf 1/4 2/5 3/3\nf 1/1 2/6 3/7\n"
	                          "usemtl offs\nf 1/8 2/8 3/8\nusemtl scal\nf 1/3 2/3 3/3\nusemtl clmp\nf 1/9 2/10 3/9\n";
	static const u32 want[6][3] = {{0xFF0000u, 0x00FF00u, 0xFFFFFFu}, {0xFF0000u, 0x00FF00u, 0xFFFFFFu}, {0x808000u, 0x808000u, 0xFFFFFFu},
	                               {0x00FF00u, 0x00FF00u, 0x00FF00u}, {0xFF0000u, 0xFF0000u, 0xFF0000u}, {0x00FF00u, 0x00FF00u, 0x00FF00u}};
	struct RldObjMemFile files[2];
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	u8 rgb[48];
	u8 *png;
	size_t pngSize = 0;
	u32 f;
	u32 k;
	int ok;
	int same = 1;

	for (k = 0; k < 16u; k++)
	{
		const u32 x = k % 4u;

		rgb[3u * k + 0u] = (x == 3u) ? 0u : 255u;
		rgb[3u * k + 1u] = (x == 0u) ? 0u : 255u;
		rgb[3u * k + 2u] = ((x == 0u) || (x == 3u)) ? 0u : 255u;
	}
	png = RldMk_TestPngRgb(4u, 4u, rgb, &pngSize);
	files[0].path = "selftest/edge.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/edge.png";
	files[1].data = png;
	files[1].size = pngSize;
	s_rldObjFiles = files;
	s_rldObjFileCount = 2u;

	ok = (png != NULL) && RldMk_TestImport("selftest/edge.obj", obj, sizeof(obj) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
	for (f = 0; ok && (f < 6u); f++)
	{
		for (k = 0; k < 3u; k++)
		{
			same = same && (RldMk_TestCornerRgb(&ply, f, k) == want[f][k]);
		}
	}
	RldMk_Test(ok && same, "OBJ texture edges: vt in 0..1 clamped (u 0 red, u 1 green, also near the edge), tiled vt repeated; map_Kd -o, -s, -clamp on",
	           "read %d, %06X %06X | %06X %06X | %06X %06X | -o %06X -s %06X -clamp %06X", ok, ok ? RldMk_TestCornerRgb(&ply, 0u, 0u) : 0u,
	           ok ? RldMk_TestCornerRgb(&ply, 0u, 1u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 1u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 1u, 1u) : 0u,
	           ok ? RldMk_TestCornerRgb(&ply, 2u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 2u, 1u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 3u, 0u) : 0u,
	           ok ? RldMk_TestCornerRgb(&ply, 4u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 5u, 1u) : 0u);
	RldMk_FreePly(&ply);

	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	free(png);
}

// --vertex-colors: a PS1 rip (vertex colors 0x80 on textured faces) shows its
// texture as it is (texel 0xC0 x 0x80 = 0xC0), a face without texture and a
// texture not found keep the vertex color; color gives texel x color/255;
// a model with 0.8 grey (painted in Blender) stays color unless modulate is
// asked for.
static void RldMk_SelfTestObjVertexColors(void)
{
	static const char mtl[] = "newmtl tex\nmap_Kd c0.png\nnewmtl kd\nKd 0.2 0.4 0.6\nnewmtl gone\nmap_Kd missing.png\n";
	static const char faces[] = "vt 0.5 0.5\nusemtl tex\nf 1/1 2/1 3/1\nusemtl kd\nf 1 2 3\nusemtl gone\nf 1/1 2/1 3/1\nusemtl tex\nf 4/1 2/1 3/1\n";
	static const u8 grey[12] = {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0};
	// mode (-1 = not given), the color of v 1..3, the corners of faces 0..3,
	// modulation said
	static const struct
	{
		const char *mode;
		double v;
		u32 want[4];
		int said;
	} cases[5] = {
	    {NULL, 128.0 / 255.0, {0xC0C0C0u, 0x808080u, 0x808080u, 0xC0BF99u}, 1},
	    {"color", 128.0 / 255.0, {0x606060u, 0x808080u, 0x808080u, 0x73604Du}, 0},
	    {"modulate", 128.0 / 255.0, {0xC0C0C0u, 0x808080u, 0x808080u, 0xC0BF99u}, 1},
	    {NULL, 0.8, {0x9A9A9Au, 0xCCCCCCu, 0xCCCCCCu, 0x73604Du}, 0},
	    {"modulate", 0.8, {0xC0C0C0u, 0xCCCCCCu, 0xCCCCCCu, 0xC0BF99u}, 1},
	};
	struct RldObjMemFile files[2];
	size_t pngSize = 0;
	u8 *png = RldMk_TestPngRgb(2u, 2u, grey, &pngSize);
	u32 c;

	files[0].path = "selftest/vc.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/c0.png";
	files[1].data = png;
	files[1].size = pngSize;
	s_rldObjFiles = files;
	s_rldObjFileCount = 2u;

	for (c = 0; c < 5u; c++)
	{
		struct RldMkBuffer b;
		struct RldMkPly ply;
		struct RldMkModelInfo info;
		char name[160];
		int ok;
		int same = 1;
		u32 f;

		memset(&b, 0, sizeof(b));
		RldMk_BufferText(&b, "mtllib vc.mtl\nv 0 0 0 %.6f %.6f %.6f\nv 1 0 0 %.6f %.6f %.6f\nv 0 1 0 %.6f %.6f %.6f\nv 0 0 1 0.6 0.5 0.4\n%s", cases[c].v,
		                 cases[c].v, cases[c].v, cases[c].v, cases[c].v, cases[c].v, cases[c].v, cases[c].v, cases[c].v, faces);
		RldObj_ResetOptions();
		RldMk_ResetSaid();
		ok = (cases[c].mode == NULL) || RldObj_Argument("--vertex-colors", cases[c].mode);
		ok = ok && (png != NULL) && (b.data != NULL) && RldMk_TestImport("selftest/vc.obj", (const char *)b.data, b.size, &ply, &info, RLDMK_ROLE_MAIN);
		for (f = 0; ok && (f < 4u); f++)
		{
			same = same && (RldMk_TestCornerRgb(&ply, f, 0u) == cases[c].want[f]);
		}
		snprintf(name, sizeof(name), "OBJ --vertex-colors %s, vertex colors %.2f: %06X %06X %06X %06X", (cases[c].mode != NULL) ? cases[c].mode : "(auto)",
		         cases[c].v, cases[c].want[0], cases[c].want[1], cases[c].want[2], cases[c].want[3]);
		RldMk_Test(ok && same && (RldMk_Said("obj-vertex-modulation") == cases[c].said), name, "read %d, %06X %06X %06X %06X, said: %s", ok,
		           ok ? RldMk_TestCornerRgb(&ply, 0u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 1u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 2u, 0u) : 0u,
		           ok ? RldMk_TestCornerRgb(&ply, 3u, 0u) : 0u, s_rldMkCodes);
		RldMk_FreePly(&ply);
		free(b.data);
	}

	RldMk_ResetSaid();
	RldObj_ResetOptions();
	RldMk_Test(RldObj_Argument("--vertex-colors", "bright") && RldMk_Said("vertex-colors") && !RldObj_Argument("--colors", "64") &&
	               !RldObj_Argument("--vertex-colors", NULL),
	           "--vertex-colors with a wrong value: vertex-colors; other arguments are not the OBJ reader's", "said: %s", s_rldMkCodes);
	RldObj_ResetOptions();
	RldObj_EmitValues();

	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	free(png);
}

// One color for all vertices: exactly black or white while the materials have
// colors (2 Kd, or a texture) gives the materials (obj-vertex-colors-uniform);
// gold, or black with one Kd only, stays; --colors-from decides over it.
static void RldMk_SelfTestObjUniform(void)
{
	static const char mtl[] = "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\nnewmtl tex\nKd 0 1 0\nmap_Kd white.png\n";
	static const u8 white[12] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
	// the vertex color, the second face's material, the switch, the colors
	// of faces 0 and 1, warned
	static const struct
	{
		const char *vertex;
		const char *second;
		const char *from;
		u32 want[2];
		int warned;
	} cases[6] = {
	    {"0 0 0", "blue", NULL, {0xFF0000u, 0x0000FFu}, 1},
	    {"1 1 1", "tex", NULL, {0xFF0000u, 0x00FF00u}, 1},
	    {"0.9 0.75 0.2", "blue", NULL, {0xE6BF33u, 0xE6BF33u}, 0},
	    {"0 0 0", "red", NULL, {0x000000u, 0x000000u}, 0},
	    {"0 0 0", "blue", "vertex", {0x000000u, 0x000000u}, 0},
	    {"0.9 0.75 0.2", "blue", "material", {0xFF0000u, 0x0000FFu}, 0},
	};
	struct RldObjMemFile files[2];
	size_t pngSize = 0;
	u8 *png = RldMk_TestPngRgb(2u, 2u, white, &pngSize);
	u32 c;

	files[0].path = "selftest/uniform.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/white.png";
	files[1].data = png;
	files[1].size = pngSize;
	s_rldObjFiles = files;
	s_rldObjFileCount = 2u;

	for (c = 0; c < 6u; c++)
	{
		struct RldMkBuffer b;
		struct RldMkPly ply;
		struct RldMkModelInfo info;
		char name[160];
		int ok;

		memset(&b, 0, sizeof(b));
		RldMk_BufferText(&b, "mtllib uniform.mtl\nv 0 0 0 %s\nv 1 0 0 %s\nv 0 1 0 %s\nvt 0.5 0.5\nusemtl red\nf 1/1 2/1 3/1\nusemtl %s\nf 1/1 2/1 3/1\n",
		                 cases[c].vertex, cases[c].vertex, cases[c].vertex, cases[c].second);
		RldObj_ResetOptions();
		RldMk_ResetSaid();
		ok = (cases[c].from == NULL) || RldObj_Argument("--colors-from", cases[c].from);
		ok = ok && (png != NULL) && (b.data != NULL) && RldMk_TestImport("selftest/uniform.obj", (const char *)b.data, b.size, &ply, &info, RLDMK_ROLE_MAIN);
		snprintf(name, sizeof(name), "OBJ vertex colors all %s, faces red and %s%s%s: %06X %06X", cases[c].vertex, cases[c].second,
		         (cases[c].from != NULL) ? ", --colors-from " : "", (cases[c].from != NULL) ? cases[c].from : "", cases[c].want[0], cases[c].want[1]);
		RldMk_Test(ok && (RldMk_TestCornerRgb(&ply, 0u, 0u) == cases[c].want[0]) && (RldMk_TestCornerRgb(&ply, 1u, 0u) == cases[c].want[1]) &&
		               (RldMk_Said("obj-vertex-colors-uniform") == cases[c].warned),
		           name, "read %d, %06X %06X, said: %s", ok, ok ? RldMk_TestCornerRgb(&ply, 0u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 1u, 0u) : 0u,
		           s_rldMkCodes);
		RldMk_FreePly(&ply);
		free(b.data);
	}

	RldMk_ResetSaid();
	RldMk_Test(RldObj_Argument("--colors-from", "both") && RldMk_Said("colors-from"), "--colors-from with a wrong value: colors-from", "said: %s",
	           s_rldMkCodes);
	RldObj_ResetOptions();
	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	free(png);
}

// Ke: the brighter of Kd and Ke per channel (Ke alone without Kd); d, Tr and
// map_d: one obj-transparency, the colors as without them.
static void RldMk_SelfTestObjEmission(void)
{
	static const char mtl[] = "newmtl glow\nKd 0 0 0\nKe 1 0.5 0\nnewmtl ke\nKe 0 1 0\nnewmtl dim\nKd 0.5 0.5 0.5\nKe 0.2 0.8 0.1\n"
	                          "newmtl ghost\nKd 1 0 0\nd 0.3\nnewmtl tr\nTr 0.7\nKd 0 0 1\nnewmtl cut\nKd 1 1 0\nmap_d cut.png\n";
	static const char obj[] = "mtllib emission.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
	                          "usemtl glow\nf 1 2 3\nusemtl ke\nf 1 2 3\nusemtl dim\nf 1 2 3\nusemtl ghost\nf 1 2 3\nusemtl tr\nf 1 2 3\nusemtl cut\nf 1 2 3\n";
	static const u32 want[6] = {0xFF8000u, 0x00FF00u, 0x80CC80u, 0xFF0000u, 0x0000FFu, 0xFFFF00u};
	struct RldObjMemFile files[1];
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	u32 f;
	int ok;
	int same = 1;

	files[0].path = "selftest/emission.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	s_rldObjFiles = files;
	s_rldObjFileCount = 1u;
	ok = RldMk_TestImport("selftest/emission.obj", obj, sizeof(obj) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
	for (f = 0; ok && (f < 6u); f++)
	{
		same = same && (RldMk_TestCornerRgb(&ply, f, 0u) == want[f]);
	}
	RldMk_Test(ok && same && (RldMk_TestSaidCount("obj-transparency") == 1u) && (s_rldMkWarnings == 0),
	           "OBJ Ke: FF8000, Ke alone 00FF00, max(Kd, Ke) 80CC80; d, Tr, map_d: one obj-transparency", "read %d, %06X %06X %06X, said: %s", ok,
	           ok ? RldMk_TestCornerRgb(&ply, 0u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 1u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 2u, 0u) : 0u,
	           s_rldMkCodes);
	RldMk_FreePly(&ply);
	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
}

// Texture alpha, and names that start with '#' (not a comment after mtllib
// and map_Kd): a 4 x 4 texture with its outer columns transparent black and
// the inner ones red. Between a transparent and a red texel the corner is
// red, not dark red; on transparent texels only it keeps Kd (yellow).
static void RldMk_SelfTestObjAlpha(void)
{
	static const char mtl[] = "newmtl cut\nKd 1 1 0\nmap_Kd #cut.png\n";
	static const char obj[] = "mtllib #alpha.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.25 0.5\nvt 0.75 0.5\nvt 0.5 0.5\nvt 0.125 0.5\n"
	                          "usemtl cut\nf 1/1 2/2 3/3\nf 1/4 2/4 3/4\n";
	struct RldObjMemFile files[2];
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	struct RldMkBuffer idat;
	struct RldMkBuffer png;
	u8 raw[4u * (1u + 16u)];
	u8 head[13];
	u32 x;
	u32 y;
	int ok;

	// The PNG: RGBA, rows with filter 0.
	for (y = 0; y < 4u; y++)
	{
		raw[y * 17u] = 0;
		for (x = 0; x < 4u; x++)
		{
			const int inner = (x == 1u) || (x == 2u);

			raw[y * 17u + 1u + 4u * x + 0u] = inner ? 255u : 0u;
			raw[y * 17u + 1u + 4u * x + 1u] = 0u;
			raw[y * 17u + 1u + 4u * x + 2u] = 0u;
			raw[y * 17u + 1u + 4u * x + 3u] = inner ? 255u : 0u;
		}
	}
	memset(&idat, 0, sizeof(idat));
	memset(&png, 0, sizeof(png));
	RldMk_TestDeflate(&idat, raw, sizeof(raw), 0, 17u, 0);
	RldMk_BufferBytes(&png, s_rldPngSignature, 8u);
	RldMk_PutBE32(&head[0], 4u);
	RldMk_PutBE32(&head[4], 4u);
	head[8] = 8;
	head[9] = 6;
	head[10] = 0;
	head[11] = 0;
	head[12] = 0;
	RldMk_TestChunk(&png, "IHDR", head, 13u);
	RldMk_TestChunk(&png, "IDAT", idat.data, (u32)idat.size);
	RldMk_TestChunk(&png, "IEND", NULL, 0u);
	free(idat.data);

	files[0].path = "selftest/#alpha.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/#cut.png";
	files[1].data = png.data;
	files[1].size = png.size;
	s_rldObjFiles = files;
	s_rldObjFileCount = 2u;
	ok = (png.data != NULL) && RldMk_TestImport("selftest/alpha.obj", obj, sizeof(obj) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
	RldMk_Test(ok && (info.mtl == RLDMK_MTL_OK) && (info.texturesOk == 1u) && (RldMk_TestCornerRgb(&ply, 0u, 0u) == 0xFF0000u) &&
	               (RldMk_TestCornerRgb(&ply, 0u, 1u) == 0xFF0000u) && (RldMk_TestCornerRgb(&ply, 1u, 0u) == 0xFFFF00u) && RldMk_Said("tex-alpha") &&
	               (s_rldMkWarnings == 0),
	           "OBJ #alpha.mtl and #cut.png read; texture alpha: red beside transparent texels, Kd on transparent ones", "read %d, %06X %06X %06X, said: %s",
	           ok, ok ? RldMk_TestCornerRgb(&ply, 0u, 0u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 0u, 1u) : 0u, ok ? RldMk_TestCornerRgb(&ply, 1u, 0u) : 0u,
	           s_rldMkCodes);
	RldMk_FreePly(&ply);
	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	free(png.data);
}

// What falls back with a warning, what fails, and which format a file is.
static void RldMk_SelfTestObjCases(struct RldMkTestMesh *mesh)
{
	static const char tri[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
	static const char badPicture[] = "BM this is no picture rldpack reads";
	static const char binaryMtl[] = "newmtl a\0\0\0";
	struct RldObjMemFile files[8];
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	struct RldMkBuffer b;
	u8 big[64];
	u8 *png;
	size_t pngSize = 0;
	int ok;

	// A PNG head of 100000 x 1 pixels: too large, refused before its data.
	png = RldMk_TestPngRgb(2u, 2u, s_rldMkTestChecker, &pngSize);
	memset(big, 0, sizeof(big));
	if ((png != NULL) && (pngSize >= 33u))
	{
		memcpy(big, png, 33u);
		RldMk_PutBE32(&big[16], 100000u);
		RldMk_PutBE32(&big[20], 1u);
		RldMk_PutBE32(&big[29], RldPng_Crc(&big[12], 17u));
	}

	// Fallbacks: a warning, and the model is read.
	{
		static const char fallbacks[] = "mtllib gone.mtl\nmtllib bad.mtl\nmtllib good.mtl\n"
		                                "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1 1.5 0.5 0.5\nvt 0 0\n"
		                                "usemtl unknown\nf 1 2 3\n"
		                                "usemtl gone\nf 1/1 2/1 3/1\n"
		                                "usemtl empty\nf 1/1 2/1 3/1\n"
		                                "usemtl bmp\nf 1/1 2/1 3/1\n"
		                                "usemtl huge\nf 4/1 2/1 3/1\n";
		static const char good[] = "newmtl gone\nmap_Kd gone.png\nnewmtl empty\nmap_Kd empty.png\n"
		                           "newmtl bmp\nmap_Kd picture.bmp\nnewmtl huge\nmap_Kd huge.png\nKd 2 0.5 0.5\n";

		files[0].path = "selftest/good.mtl";
		files[0].data = (const u8 *)good;
		files[0].size = sizeof(good) - 1u;
		files[1].path = "selftest/bad.mtl";
		files[1].data = (const u8 *)binaryMtl;
		files[1].size = sizeof(binaryMtl) - 1u;
		files[2].path = "selftest/empty.png";
		files[2].data = (const u8 *)"";
		files[2].size = 0;
		files[3].path = "selftest/picture.bmp";
		files[3].data = (const u8 *)badPicture;
		files[3].size = sizeof(badPicture) - 1u;
		files[4].path = "selftest/huge.png";
		files[4].data = big;
		files[4].size = 33u;
		s_rldObjFiles = files;
		s_rldObjFileCount = 5u;

		ok = RldMk_TestImport("selftest/fallbacks.obj", fallbacks, sizeof(fallbacks) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
		RldMk_Test(ok && (info.mtl == RLDMK_MTL_BAD) && (info.libraries == 3u) && (info.textures == 4u) && (info.texturesOk == 0u) &&
		               RldMk_Said("mtl-missing") && RldMk_Said("mtl-bad") && RldMk_Said("mtl-material") && RldMk_Said("tex-missing") &&
		               RldMk_Said("tex-unreadable") && RldMk_Said("tex-unsupported") && RldMk_Said("obj-color-range") && (s_rldMkErrors == 0),
		           "OBJ fallbacks: MTL missing, not MTL, material unknown, texture missing, empty, BMP, 100000 x 1 - warnings, read",
		           "read %d, mtl %d, %u files, textures %u/%u, said: %s", ok, info.mtl, info.libraries, info.texturesOk, info.textures, s_rldMkCodes);
		RldMk_FreePly(&ply);
		s_rldObjFiles = NULL;
		s_rldObjFileCount = 0;
	}

	// Three textures not found: one tex-missing for all of them, the faces
	// keep their colors (Kd red, grey).
	{
		static const char three[] = "mtllib three.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\n"
		                            "usemtl a\nf 1/1 2/1 3/1\nusemtl b\nf 1/1 2/1 3/1\nf 1/1 2/1 3/1\nusemtl c\nf 1/1 2/1 3/1\nusemtl d\nf 1 2 3\n";
		static const char mtl[] = "newmtl a\nKd 1 0 0\nmap_Kd C:/elsewhere/a.png\nnewmtl b\nmap_Kd b.png\nnewmtl c\nmap_Kd sub/c.png\nnewmtl d\nKd 0 0 1\n";

		files[0].path = "selftest/three.mtl";
		files[0].data = (const u8 *)mtl;
		files[0].size = sizeof(mtl) - 1u;
		s_rldObjFiles = files;
		s_rldObjFileCount = 1u;
		ok = RldMk_TestImport("selftest/three.obj", three, sizeof(three) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
		RldMk_Test(ok && (info.textures == 3u) && (info.texturesOk == 0u) && (RldMk_TestSaidCount("tex-missing") == 1u) && (s_rldMkWarnings == 1) &&
		               (RldMk_TestCornerRgb(&ply, 0u, 0u) == 0xFF0000u) && (RldMk_TestCornerRgb(&ply, 1u, 0u) == 0x808080u) &&
		               (RldMk_TestCornerRgb(&ply, 4u, 0u) == 0x0000FFu),
		           "OBJ with three textures not found: one tex-missing for all, the faces keep Kd or grey", "read %d, textures %u/%u, said: %s", ok,
		           info.texturesOk, info.textures, s_rldMkCodes);
		RldMk_FreePly(&ply);
		s_rldObjFiles = NULL;
		s_rldObjFileCount = 0;
	}

	// Textures not at their path, found at the other places: in textures/
	// next to the MTL file, with another file type, the last two parts of a
	// foreign path before a file of the same name next to the MTL file (blue),
	// next to the OBJ file when the MTL file is elsewhere.
	{
		static const char found[] = "mtllib ../lib/found.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.5 0.5\n"
		                            "usemtl a\nf 1/1 2/1 3/1\nusemtl b\nf 1/1 2/1 3/1\nusemtl c\nf 1/1 2/1 3/1\nusemtl d\nf 1/1 2/1 3/1\n";
		static const char mtl[] = "newmtl a\nmap_Kd a.png\nnewmtl b\nmap_Kd b.tga\nnewmtl c\nmap_Kd C:/other/project/textures/c.png\nnewmtl d\nmap_Kd d.png\n";
		static const u8 red[12] = {255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0};
		static const u8 blue[12] = {0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255};
		size_t redSize = 0;
		size_t blueSize = 0;
		u8 *redPng = RldMk_TestPngRgb(2u, 2u, red, &redSize);
		u8 *bluePng = RldMk_TestPngRgb(2u, 2u, blue, &blueSize);
		u32 f;
		int same = 1;

		files[0].path = "selftest/obj/../lib/found.mtl";
		files[0].data = (const u8 *)mtl;
		files[0].size = sizeof(mtl) - 1u;
		files[1].path = "selftest/obj/../lib/textures/a.png";
		files[2].path = "selftest/obj/../lib/b.png";
		files[3].path = "selftest/obj/../lib/textures/c.png";
		files[4].path = "selftest/obj/d.png";
		for (f = 1; f < 5u; f++)
		{
			files[f].data = redPng;
			files[f].size = redSize;
		}
		files[5].path = "selftest/obj/../lib/c.png";
		files[5].data = bluePng;
		files[5].size = blueSize;
		s_rldObjFiles = files;
		s_rldObjFileCount = 6u;
		ok = (redPng != NULL) && (bluePng != NULL) && RldMk_TestImport("selftest/obj/found.obj", found, sizeof(found) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
		for (f = 0; ok && (f < 4u); f++)
		{
			same = same && (RldMk_TestCornerRgb(&ply, f, 0u) == 0xFF0000u);
		}
		RldMk_Test(ok && same && (info.texturesOk == 4u) && (RldMk_TestSaidCount("tex-found-nearby") == 4u) && (s_rldMkWarnings == 0),
		           "OBJ textures found elsewhere: in textures/, as .png for .tga, the last two parts of a foreign path first, next to the OBJ",
		           "read %d, same %d, textures %u/%u, said: %s", ok, same, info.texturesOk, info.textures, s_rldMkCodes);
		RldMk_FreePly(&ply);
		s_rldObjFiles = NULL;
		s_rldObjFileCount = 0;
		free(redPng);
		free(bluePng);
	}

	// MTL keywords in any letter case, a texture whose name starts with '-',
	// map_Kd without a name: mtl-bad, the material keeps its Kd.
	{
		static const char cases[] = "mtllib cases.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.5 0.5\n"
		                            "usemtl a\nf 1/1 2/1 3/1\nusemtl b\nf 1/1 2/1 3/1\nusemtl c\nf 1/1 2/1 3/1\n";
		static const char mtl[] = "NEWMTL a\nkd 1 0 0\nNewMtl b\nMAP_KD -face.png\nnewmtl c\nKD 0 0 1\nmap_Kd -s 1 1 1\n";
		static const u8 green[12] = {0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0};
		size_t greenSize = 0;
		u8 *greenPng = RldMk_TestPngRgb(2u, 2u, green, &greenSize);

		files[0].path = "selftest/cases.mtl";
		files[0].data = (const u8 *)mtl;
		files[0].size = sizeof(mtl) - 1u;
		files[1].path = "selftest/-face.png";
		files[1].data = greenPng;
		files[1].size = greenSize;
		s_rldObjFiles = files;
		s_rldObjFileCount = 2u;
		ok = (greenPng != NULL) && RldMk_TestImport("selftest/cases.obj", cases, sizeof(cases) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
		RldMk_Test(ok && (info.texturesOk == 1u) && (RldMk_TestCornerRgb(&ply, 0u, 0u) == 0xFF0000u) && (RldMk_TestCornerRgb(&ply, 1u, 0u) == 0x00FF00u) &&
		               (RldMk_TestCornerRgb(&ply, 2u, 0u) == 0x0000FFu) && (RldMk_TestSaidCount("mtl-bad") == 1u) && (s_rldMkWarnings == 1),
		           "MTL keywords in any letter case, a texture named -face.png, map_Kd without a name (mtl-bad)", "read %d, textures %u/%u, said: %s", ok,
		           info.texturesOk, info.textures, s_rldMkCodes);
		RldMk_FreePly(&ply);
		s_rldObjFiles = NULL;
		s_rldObjFileCount = 0;
		free(greenPng);
	}

	// Nothing but grey: obj-no-colors; a face of 5 and of 6 corners through more[].
	{
		static const char grey[] = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0.5 1.5 0\nv 0 1 0\nv -0.5 0.5 0\n"
		                           "f 1 2 3 4 5\nf -6 -5 -4 -3 -2 -1\n";

		ok = RldMk_TestImport("selftest/grey.obj", grey, sizeof(grey) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
		RldMk_Test(ok && (info.colors == RLDMK_COLORS_GREY) && ply.hasColors && RldMk_Said("obj-no-colors") && (ply.cornerCount[0] == 5u) &&
		               (ply.cornerCount[1] == 6u) && (ply.moreCount == 11u) && (ply.more[ply.moreStart[1] + 5u] == 5u) && (ply.vertexCount == 6u),
		           "OBJ without colors (grey) and faces of 5 and 6 corners", "read %d, colors %u, more %u, said: %s", ok, info.colors, ply.moreCount,
		           s_rldMkCodes);
		RldMk_FreePly(&ply);
	}

	// Errors: one case per code, each with its line.
	{
		static const char *const cases[][2] = {
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 x\n", "obj-syntax"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nfrob 1 2\nf 1 2 3\n", "obj-syntax"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1/1/1 2 3\n", "obj-syntax"},
		    {"v 0 0 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "obj-syntax"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n", "obj-index"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n", "obj-index"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 -4\n", "obj-index"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 1099511627776\n", "obj-index"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/2 2 3\n", "obj-index"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 nan\nf 1 2 3\n", "obj-number"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 1e999\nf 1 2 3\n", "obj-number"},
		    {"v 0 0 0\nv 2000000 0 0\nv 0 1 0\nf 1 2 3\n", "obj-number"},
		    {"v 0 0 0\nv 1 0 0\nf 1 2\n", "obj-face"},
		    {"v 0 0 0\nv 1 0 0\nv 0 1 0\n", "obj-empty"},
		};
		u32 c;

		for (c = 0; c < (u32)(sizeof(cases) / sizeof(cases[0])); c++)
		{
			char name[64];

			snprintf(name, sizeof(name), "OBJ refused: %s (case %u)", cases[c][1], c);
			RldMk_Test(RldMk_TestObjError("selftest/bad.obj", cases[c][0], strlen(cases[c][0]), cases[c][1]), name, "said: %s", s_rldMkCodes);
		}
	}

	// An unknown keyword past the lines that make a file OBJ; a face of 1025
	// corners; a line of 300000 bytes; one vertex more than RLDMK_PLY_MAX.
	{
		struct RldMkPly cut;
		u32 i;

		memset(&b, 0, sizeof(b));
		RldMk_BufferText(&b, "%s", tri);
		RldMk_BufferText(&b, "f 1 2 3\nbogus 1 2 3\n");
		RldMk_ResetSaid();
		memset(&info, 0, sizeof(info));
		ok = (b.data != NULL) && RldObj_Parse("selftest/bogus.obj", b.data, b.size, &cut, &info);
		RldMk_FreePly(&cut);
		RldMk_Test(!ok && RldMk_Said("obj-syntax"), "OBJ refused: an unknown keyword", "said: %s", s_rldMkCodes);
		free(b.data);

		memset(&b, 0, sizeof(b));
		RldMk_BufferText(&b, "%sf", tri);
		for (i = 0; i < 1025u; i++)
		{
			RldMk_BufferText(&b, " %u", 1u + (i % 3u));
		}
		RldMk_BufferText(&b, "\n");
		RldMk_Test((b.data != NULL) && RldMk_TestObjError("selftest/many.obj", (const char *)b.data, b.size, "obj-face"), "OBJ refused: a face of 1025 corners",
		           "said: %s", s_rldMkCodes);
		free(b.data);

		memset(&b, 0, sizeof(b));
		RldMk_BufferText(&b, "%s# ", tri);
		for (i = 0; i < 3000u; i++)
		{
			RldMk_BufferText(&b, "%s", "....................................................................................................");
		}
		RldMk_BufferText(&b, "\nf 1 2 3\n");
		RldMk_Test((b.data != NULL) && RldMk_TestObjError("selftest/long.obj", (const char *)b.data, b.size, "obj-syntax"), "OBJ refused: a line of 300000 bytes",
		           "said: %s", s_rldMkCodes);
		free(b.data);

		memset(&b, 0, sizeof(b));
		for (i = 0; i <= RLDMK_PLY_MAX; i++)
		{
			RldMk_BufferBytes(&b, "v 0 0 0\n", 8u);
		}
		RldMk_Test((b.data != NULL) && RldMk_TestObjError("selftest/huge.obj", (const char *)b.data, b.size, "obj-big"), "OBJ refused: more than 1000000 vertices",
		           "said: %s", s_rldMkCodes);
		free(b.data);
	}

	// The format by the content, the name only compared.
	{
		static const char obj[] = "# an OBJ\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
		static const char stl[] = "solid part\nfacet normal 0 0 1\n";
		size_t plySize = 0;
		u8 *plyText;

		RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
		plyText = RldMk_TestPly(mesh, 0, 1, 0, &plySize);

		ok = (plyText != NULL) && RldMk_TestImport("selftest/named.obj", (const char *)plyText, plySize, &ply, &info, RLDMK_ROLE_MAIN);
		RldMk_Test(ok && (info.format == RLDMK_FORMAT_PLY) && RldMk_Said("model-misnamed"), "a PLY named .obj: read as PLY, model-misnamed", "said: %s",
		           s_rldMkCodes);
		RldMk_FreePly(&ply);

		ok = (plyText != NULL) && RldMk_TestImport("selftest/named.ply", (const char *)plyText, plySize, &ply, &info, RLDMK_ROLE_MAIN);
		RldMk_Test(ok && (info.format == RLDMK_FORMAT_PLY) && (s_rldMkCodes[0] == '\0'), "a PLY named .ply: nothing new said", "said: %s", s_rldMkCodes);
		RldMk_FreePly(&ply);
		free(plyText);

		ok = RldMk_TestImport("selftest/named.ply", obj, sizeof(obj) - 1u, &ply, &info, RLDMK_ROLE_MAIN);
		RldMk_Test(ok && (info.format == RLDMK_FORMAT_OBJ) && RldMk_Said("model-misnamed"), "an OBJ named .ply: read as OBJ, model-misnamed", "said: %s",
		           s_rldMkCodes);
		RldMk_FreePly(&ply);

		RldMk_Test(RldMk_TestObjError("selftest/notes.dat", "just some words\n", 16u, "model-unknown"), "unknown content: model-unknown", "said: %s",
		           s_rldMkCodes);
		RldMk_Test(RldMk_TestObjError("selftest/notes.obj", "o dear\nthis is just\nsome text\nv 1 2 3\n", 38u, "model-unknown"),
		           "text with a few OBJ words, not most: model-unknown", "said: %s", s_rldMkCodes);
		{
			struct RldObjTexture cut;
			const char *why = NULL;
			int fromImg = 0;
			size_t tgaSize = 0;
			u8 rgb[48];
			u8 *tga;

			memset(rgb, 0x40, sizeof(rgb));
			memset(&cut, 0, sizeof(cut));
			tga = RldMk_TestTga(4u, 4u, rgb, &tgaSize);
			RldMk_Test((tga != NULL) && (RldObj_Decode("selftest/cut.tga", tga, tgaSize - 10u, &cut, &fromImg, &why) == RLDMK_TEXTURE_UNREADABLE) &&
			               (cut.rgba == NULL),
			           "a TGA cut short: tex-unreadable", "why: %s", (why != NULL) ? why : "-");
			free(tga);
		}
		RldMk_Test(RldMk_TestObjError("selftest/notes.ply", "just some words\n", 16u, "ply-format"), "unknown content named .ply: the PLY reader's ply-format",
		           "said: %s", s_rldMkCodes);
		RldMk_Test((png != NULL) && RldMk_TestObjError("selftest/picture.obj", (const char *)png, pngSize, "model-unsupported"),
		           "a PNG named .obj: model-unsupported", "said: %s", s_rldMkCodes);
		RldMk_Test(RldMk_TestObjError("selftest/part.stl", stl, sizeof(stl) - 1u, "model-unsupported"), "an STL: model-unsupported", "said: %s", s_rldMkCodes);

		ok = RldMk_TestImport("selftest/pose.obj", obj, sizeof(obj) - 1u, &ply, &info, RLDMK_ROLE_POSE);
		RldMk_Test(!ok && RldMk_Said("model-ply-only"), "an OBJ as a pose: model-ply-only", "said: %s", s_rldMkCodes);
		RldMk_FreePly(&ply);
	}

	free(png);
}

// --wheels off and a vehicle shorter than a kart (ply-kart-short): a vehicle
// 1.0 long under a driver 1.6 tall - the fit caps the height, the vehicle
// stays under 1.25 long. Wheels off: it is the kart, the model builds.
// Wheels on: ply-no-kart as before. Figures without a vehicle are no kart
// either way, and the error does not send them to --wheels off: on two
// shoes, of one piece 1.8 tall and 0.5 deep, of one piece 1.0 tall and 0.3
// deep, legs of one piece with a head apart.
static void RldMk_SelfTestObj(struct RldMkTestMesh *mesh)
{
	u8 field[RLDCHAR_NAME_FIELD];
	size_t chriSize = 0;
	u8 *chri;

	memset(field, 0, sizeof(field));
	memcpy(field, "SELF TEST", 9u);
	chri = RldMk_BuildChri(14u, 0u, field, 1u, "", 0u, &chriSize);
	if (chri == NULL)
	{
		RldMk_Test(0, "OBJ", "out of memory");
		return;
	}

	{
		char sha[65];

		RldObj_Digest((const u8 *)"abc", 3u, sha);
		RldMk_Test(strcmp(sha, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "OBJ texture digest (@model texture): SHA-256 of abc",
		           "%s", sha);
	}
	RldMk_SelfTestObjSame(mesh, chri, chriSize);
	RldMk_SelfTestObjGolden(mesh, chri, chriSize);
	RldMk_SelfTestObjColors();
	RldMk_SelfTestObjVertexColors();
	RldMk_SelfTestObjEdges();
	RldMk_SelfTestObjUniform();
	RldMk_SelfTestObjEmission();
	RldMk_SelfTestObjAlpha();
	RldMk_SelfTestObjCases(mesh);
	free(chri);
}

// The number of failed cases; 0 = passed.
