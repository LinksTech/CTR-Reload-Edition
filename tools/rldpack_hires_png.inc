// rldpack_hires_png.inc - hires-png: a derived hi-res model whose JPEG
// textures are PNG.
//
// Included by tools/rldpack.c after rldpack_hires_upscale.inc (RldUp_Chunks,
// RldUp_Rewrite, struct RldUpImage) and rldpack_hires_test.inc (its self-test
// builds on HrTest_Glb). The JPEG decoder is RldImg_Decode (tools/rldpack_image.c,
// stb_image's scalar decoder - the one make-char reads JPEG textures with), the
// PNG writer RldMk_PngRgba (rldpack_native.inc).
//
//   hires-png <in.glb> -o <out.glb>
//       writes a NEW model: every image embedded in the BIN whose bytes are a
//       JPEG (they start FF D8 FF - what the rule HRG-16 refuses) is decoded
//       and written as an 8-bit RGBA PNG of exactly the decoded pixels; its
//       "mimeType" becomes "image/png". The source file is only read; the
//       output may not be the source.
//
// FORMAT DECISIONS 4 AND 10 STAY: CHRG of version 1 holds PNG textures only,
// and rldpack hires-add stores a GLB byte for byte. This command is the
// author's step BEFORE packing that makes another GLB, the way hires-upscale
// does - and with the same rewrite (RldUp_Rewrite): the JSON keeps every byte
// but the "bufferView" and "mimeType" values of each converted image and
// buffers[0].byteLength, one new bufferView per converted image is appended to
// bufferViews, the PNGs to the BIN. The JPEG bytes stay in the BIN, unused, so
// every other index and offset is the source's. A model without a JPEG comes
// back as a copy, byte for byte (as hires-upscale with nothing to enlarge).
// The same input gives the same bytes (the PNGs are written with stored
// deflate blocks, no time stamp; the decoder is scalar, the same pixels on
// every processor). The decoded pixels are kept as they are - no colour
// management, no EXIF rotation, as make-char reads the same file.
//
// The result must open as its source opens, minus the JPEG: the game's rules
// (RldHires_Open) may not find more errors in it than in the source, and none
// of its images may be a JPEG any more - otherwise nothing is written.

struct RldPngcResult
{
	int images;     // images in the file
	int jpeg;       // of them, JPEG bytes embedded in the BIN
	int converted;  // = jpeg when the result was made
	struct RldUpImage list[RLDUP_IMAGES_MAX];   // the converted images (factor 1)
	char why[200];  // the result is NULL: why
};

static int RldPngc_IsJpeg(const u8 *bytes, size_t size)
{
	return (size >= 3u) && (bytes[0] == 0xFFu) && (bytes[1] == 0xD8u) && (bytes[2] == 0xFFu);
}

static void RldPngc_FreeResult(struct RldPngcResult *r)
{
	int i;

	for (i = 0; i < r->converted; i++)
	{
		free(r->list[i].png);
		r->list[i].png = NULL;
	}
}

// The derived GLB of glb (see the head of this file), not yet compared with
// its source. NULL with res->why when it cannot be made; a model without a
// JPEG comes back as a copy. res->list holds the PNGs (RldPngc_FreeResult).
static u8 *RldPngc_Glb(const u8 *glb, size_t size, size_t *outSize, struct RldPngcResult *res)
{
	cgltf_options options;
	cgltf_data *data = NULL;
	const u8 *json;
	const u8 *bin = NULL;
	u32 jsonLen;
	u32 binLen = 0;
	size_t restAt;
	int viewsCount;
	u8 *out;
	int i;

	memset(res, 0, sizeof(*res));
	*outSize = 0;
	if (!RldUp_Chunks(glb, size, &json, &jsonLen, &bin, &binLen, &restAt, res->why, sizeof(res->why)))
	{
		return NULL;
	}
	memset(&options, 0, sizeof(options));
	options.type = cgltf_file_type_glb;
	if ((cgltf_parse(&options, glb, size, &data) != cgltf_result_success) || (cgltf_validate(data) != cgltf_result_success))
	{
		cgltf_free(data);
		snprintf(res->why, sizeof(res->why), "the glTF does not parse or validate");
		return NULL;
	}
	res->images = (int)data->images_count;
	if (data->images_count > RLDUP_IMAGES_MAX)
	{
		cgltf_free(data);
		snprintf(res->why, sizeof(res->why), "%d images - at most %d", res->images, RLDUP_IMAGES_MAX);
		return NULL;
	}

	// Every image embedded in buffers[0] (the BIN) whose bytes are a JPEG.
	for (i = 0; i < (int)data->images_count; i++)
	{
		const cgltf_image *im = &data->images[i];
		struct RldUpImage *u;
		const u8 *bytes;
		unsigned w = 0;
		unsigned h = 0;
		u8 *rgba = NULL;
		const char *decodeWhy = NULL;
		char name[48];

		if ((im->buffer_view == NULL) || (bin == NULL) || (im->buffer_view->buffer != &data->buffers[0]) || (data->buffers[0].uri != NULL) ||
		    ((im->buffer_view->offset + im->buffer_view->size) > binLen))
		{
			continue;
		}
		bytes = &bin[im->buffer_view->offset];
		if (!RldPngc_IsJpeg(bytes, im->buffer_view->size))
		{
			continue;
		}
		res->jpeg++;
		RldHires_Name(name, sizeof(name), im->name, (size_t)i);
		if (RldImg_Decode(bytes, im->buffer_view->size, &w, &h, &rgba, &decodeWhy) != 0)
		{
			RldPngc_FreeResult(res);
			cgltf_free(data);
			snprintf(res->why, sizeof(res->why), "texture %d ('%s') is a JPEG that cannot be decoded: %s", i, name,
			         (decodeWhy != NULL) ? decodeWhy : "unknown");
			return NULL;
		}
		u = &res->list[res->converted];
		memset(u, 0, sizeof(*u));
		u->image = i;
		u->w = w;
		u->h = h;
		u->factor = 1u;
		u->png = RldMk_PngRgba(rgba, w, h, &u->pngSize);
		RldImg_Free(rgba);
		if (u->png == NULL)
		{
			RldPngc_FreeResult(res);
			cgltf_free(data);
			snprintf(res->why, sizeof(res->why), "out of memory (texture %d)", i);
			return NULL;
		}
		res->converted++;
	}
	viewsCount = (int)data->buffer_views_count;
	cgltf_free(data);

	if (res->converted == 0)
	{
		out = (u8 *)malloc(size);
		if (out == NULL)
		{
			snprintf(res->why, sizeof(res->why), "out of memory");
			return NULL;
		}
		memcpy(out, glb, size);
		*outSize = size;
		return out;
	}
	out = RldUp_Rewrite(glb, size, res->list, res->converted, viewsCount, "image/png", outSize, res->why, sizeof(res->why));
	if (out == NULL)
	{
		RldPngc_FreeResult(res);
		res->converted = 0;
	}
	return out;
}

// RldPngc_Glb, and the result compared with its source by the game's rules
// (the head of this file): NULL with res->why when it does not open as its
// source does. What hires-png writes and what Reload Studio checks.
static u8 *RldPngc_Make(const u8 *glb, size_t size, size_t *outSize, struct RldPngcResult *res)
{
	static RldHiresReport rep;
	RldHiresModel m;
	int sourceOk;
	int sourceErrors;
	int resultOk;
	int jpegLeft = 0;
	int i;
	u8 *out = RldPngc_Glb(glb, size, outSize, res);

	if ((out == NULL) || (res->converted == 0))
	{
		return out;
	}
	RldHires_ReportClear(&rep);
	sourceOk = RldHires_Open(glb, (u32)size, &m, &rep);
	RldHires_Close(&m);
	sourceErrors = rep.errors;
	RldHires_ReportClear(&rep);
	resultOk = RldHires_Open(out, (u32)*outSize, &m, &rep);
	for (i = 0; i < m.imageCount; i++)
	{
		u32 n = 0;
		const u8 *bytes = RldHires_ImageBytes(&m, i, &n);

		jpegLeft += (bytes != NULL) && RldPngc_IsJpeg(bytes, n);
	}
	RldHires_Close(&m);
	if ((rep.errors > sourceErrors) || (sourceOk && !resultOk) || (jpegLeft > 0))
	{
		snprintf(res->why, sizeof(res->why), "the converted model does not open as its source does (%d error(s) against %d%s)", rep.errors,
		         sourceErrors, (jpegLeft > 0) ? ", a JPEG left" : "");
		RldPngc_FreeResult(res);
		res->converted = 0;
		free(out);
		*outSize = 0;
		return NULL;
	}
	return out;
}

static int RldHires_PngCommand(int argc, char **argv)
{
	static struct RldPngcResult res;
	const char *paths[1];
	const char *outPath;
	u8 *glb = NULL;
	size_t glbSize = 0;
	u8 *out;
	size_t outSize = 0;
	u8 digest[32];
	char hex[65];
	int i;

	if (!RldHiresPack_Args(argc, argv, 1, paths, &outPath))
	{
		fprintf(stderr, "usage: rldpack hires-png <in.glb> -o <out.glb>\n");
		return 2;
	}
#ifdef _WIN32
	if (_stricmp(paths[0], outPath) == 0)
#else
	if (strcmp(paths[0], outPath) == 0)
#endif
	{
		fprintf(stderr, "rldpack: hires-png writes a new file - the output may not be the source\n");
		Rld_Say("error", "png", outPath, "The converted model must be a new file; the source model is never changed.");
		return 2;
	}
	if (!Rld_ReadFile(paths[0], &glb, &glbSize))
	{
		fprintf(stderr, "rldpack: %s cannot be opened\n", paths[0]);
		Rld_Say("error", "png", paths[0], "%s cannot be opened", paths[0]);
		return 1;
	}
	printf("hires-png: %s\n", paths[0]);
	out = RldPngc_Make(glb, glbSize, &outSize, &res);
	free(glb);
	if (out == NULL)
	{
		fprintf(stderr, "rldpack: %s: %s\n", paths[0], res.why);
		Rld_Say("error", "png", paths[0], "The JPEG textures cannot be converted to PNG: %s", res.why);
		return 1;
	}
	for (i = 0; i < res.converted; i++)
	{
		const struct RldUpImage *u = &res.list[i];

		printf("  image %d: JPEG %ux%u -> PNG (RGBA, %u bytes)\n", u->image, u->w, u->h, (unsigned)u->pngSize);
	}
	if (res.converted == 0)
	{
		printf("hires-png: no JPEG among the %d image(s) - the model is written as it is\n", res.images);
	}
	else
	{
		printf("hires-png: %d of %d image(s) converted from JPEG to PNG\n", res.converted, res.images);
	}
	RldPngc_FreeResult(&res);
	if (!RldHiresPack_Write(outPath, out, outSize))
	{
		free(out);
		fprintf(stderr, "rldpack: %s cannot be written\n", outPath);
		Rld_Say("error", "write", outPath, "The converted model cannot be written. Is the folder write-protected, or is the file open in another program?");
		return 1;
	}
	Sha256(out, outSize, digest);
	for (i = 0; i < 32; i++)
	{
		snprintf(&hex[i * 2], 3, "%02x", digest[i]);
	}
	printf("wrote %s (%u bytes, sha256 %s)\n", outPath, (unsigned)outSize, hex);
	Rld_Emit("written", outPath, hex, (const char *)NULL);
	free(out);
	return 0;
}

// ---------------------------------------------------------------------------
// The self-test (rldpack selftest-hires runs it after the rules' own): a model
// made in code with one JPEG texture - the test JPEG of make-char's self-test
// for OBJ models (s_rldMkTestJpeg, 16 x 16, four colours) put into the 16 x 16 PNG model of
// HrTest_Glb by the same rewrite - converted, opened clean by the game's
// rules, its pixels those of the decoded JPEG; the same bytes twice; a second
// conversion and a model without a JPEG give their source back; a JPEG that
// cannot be decoded writes nothing.

static int s_pngcChecks;
static int s_pngcFailures;

static void RldPngc_Check(int ok, const char *what)
{
	s_pngcChecks++;
	if (!ok)
	{
		s_pngcFailures++;
		printf("  FAIL hires-png: %s\n", what);
	}
}

// The bufferViews of a GLB as cgltf reads them, -1 = it does not parse.
static int RldPngc_ViewCount(const u8 *glb, size_t size)
{
	cgltf_options options;
	cgltf_data *data = NULL;
	int n = -1;

	memset(&options, 0, sizeof(options));
	options.type = cgltf_file_type_glb;
	if (cgltf_parse(&options, glb, size, &data) == cgltf_result_success)
	{
		n = (int)data->buffer_views_count;
	}
	cgltf_free(data);
	return n;
}

// The mimeType of image 0, "" when none or it does not parse.
static void RldPngc_Mime(const u8 *glb, size_t size, char *out, size_t cap)
{
	cgltf_options options;
	cgltf_data *data = NULL;

	out[0] = 0;
	memset(&options, 0, sizeof(options));
	options.type = cgltf_file_type_glb;
	if ((cgltf_parse(&options, glb, size, &data) == cgltf_result_success) && (data->images_count > 0u) && (data->images[0].mime_type != NULL))
	{
		snprintf(out, cap, "%s", data->images[0].mime_type);
	}
	cgltf_free(data);
}

static int RldPngc_SelfTest(void)
{
	static RldHiresReport rep;
	static struct RldPngcResult res;
	static struct RldPngcResult res2;
	struct HrTestSpec sp;
	struct RldUpImage put;
	RldHiresModel m;
	u32 baseSize = 0;
	u8 *base;
	u8 *jpeg = NULL;
	size_t jpegSize = 0;
	u8 *out = NULL;
	size_t outSize = 0;
	u8 *again = NULL;
	size_t againSize = 0;
	u8 *twice = NULL;
	size_t twiceSize = 0;
	char why[200];
	char mime[32];

	s_pngcChecks = 0;
	s_pngcFailures = 0;
	printf("rldpack hires-png self-test\n");

	// The model with a JPEG: the 16 x 16 PNG model, its image 0 replaced by
	// the test JPEG (mimeType image/jpeg).
	memset(&sp, 0, sizeof(sp));
	sp.image = 7;
	base = HrTest_Glb(&sp, &baseSize);
	memset(&put, 0, sizeof(put));
	put.image = 0;
	put.factor = 1u;
	put.png = (u8 *)s_rldMkTestJpeg;
	put.pngSize = sizeof(s_rldMkTestJpeg);
	why[0] = 0;
	if (base != NULL)
	{
		jpeg = RldUp_Rewrite(base, baseSize, &put, 1, RldPngc_ViewCount(base, baseSize), "image/jpeg", &jpegSize, why, sizeof(why));
	}
	RldPngc_Check(jpeg != NULL, "the model with a JPEG texture is made");
	if (jpeg == NULL)
	{
		printf("       %s\n", why);
		free(base);
		printf("hires-png selftest FAILED: %d checks, %d failures\n", s_pngcChecks, s_pngcFailures);
		return 1;
	}
	RldPngc_Mime(jpeg, jpegSize, mime, sizeof(mime));
	RldHires_ReportClear(&rep);
	RldPngc_Check(RldHires_Open(jpeg, (u32)jpegSize, &m, &rep) == 0 && rep.errors == 1 && strcmp(rep.msg[0].rule, "HRG-16") == 0 &&
	                  strstr(rep.msg[0].text, "JPEG") != NULL && strcmp(mime, "image/jpeg") == 0,
	              "the source: refused by HRG-16 for its JPEG, mimeType image/jpeg");
	RldHires_Close(&m);

	// The conversion.
	out = RldPngc_Make(jpeg, jpegSize, &outSize, &res);
	RldPngc_Check(out != NULL && res.images == 1 && res.jpeg == 1 && res.converted == 1 && res.list[0].image == 0 && res.list[0].w == 16u &&
	                  res.list[0].h == 16u,
	              "hires-png: the one JPEG (16 x 16) is converted");
	again = RldPngc_Make(jpeg, jpegSize, &againSize, &res2);
	RldPngc_Check(out != NULL && again != NULL && outSize == againSize && memcmp(out, again, outSize) == 0, "hires-png: the same model gives the same bytes");
	RldPngc_FreeResult(&res2);
	free(again);
	again = NULL;
	if (out != NULL)
	{
		const u32 jl = Rld_ReadLE32(&jpeg[12]);
		const u32 bl = Rld_ReadLE32(&jpeg[20u + jl]);
		const u32 jl2 = Rld_ReadLE32(&out[12]);

		RldPngc_Check(memcmp(&jpeg[28u + jl], &out[28u + jl2], bl) == 0, "hires-png: the BIN of the source is the start of the new BIN, byte for byte");
		RldPngc_Check(RldPngc_ViewCount(out, outSize) == RldPngc_ViewCount(jpeg, jpegSize) + 1, "hires-png: one bufferView more");
		RldPngc_Mime(out, outSize, mime, sizeof(mime));
		RldPngc_Check(strcmp(mime, "image/png") == 0, "hires-png: the image's mimeType is image/png");
		RldHires_ReportClear(&rep);
		RldPngc_Check(RldHires_Open(out, (u32)outSize, &m, &rep) == 1 && rep.count == 0 && m.images[0].isPng && m.images[0].fileWidth == 16u &&
		                  m.images[0].fileHeight == 16u && m.images[0].colorType == 6u,
		              "hires-png: the game's rules open it clean (no HRG-16), the texture a 16 x 16 RGBA PNG");
		{
			u32 n = 0;
			const u8 *png = RldHires_ImageBytes(&m, 0, &n);
			struct RldPngImage got;
			const char *pngWhy = NULL;
			unsigned w = 0;
			unsigned h = 0;
			u8 *want = NULL;
			const char *jpegWhy = NULL;
			int match = 0;

			memset(&got, 0, sizeof(got));
			if ((png != NULL) && (RldPng_Decode(png, n, &got, &pngWhy) == RLDPNG_OK) &&
			    (RldImg_Decode(s_rldMkTestJpeg, sizeof(s_rldMkTestJpeg), &w, &h, &want, &jpegWhy) == 0))
			{
				match = (got.width == w) && (got.height == h) && (memcmp(got.rgba, want, (size_t)w * h * 4u) == 0);
			}
			RldPngc_Check(match, "hires-png: the PNG's pixels are the decoded JPEG's, byte for byte (the game's PNG reader)");
			RldPng_Free(&got);
			if (want != NULL)
			{
				RldImg_Free(want);
			}
		}
		RldHires_Close(&m);

		// Idempotent: the converted model has no JPEG left - a copy.
		twice = RldPngc_Make(out, outSize, &twiceSize, &res2);
		RldPngc_Check(twice != NULL && res2.jpeg == 0 && res2.converted == 0 && twiceSize == outSize && memcmp(twice, out, outSize) == 0,
		              "hires-png of the converted model: nothing to convert, the copy is the source byte for byte");
		RldPngc_FreeResult(&res2);
		free(twice);
	}
	RldPngc_FreeResult(&res);
	free(out);

	// A model without a JPEG: the copy is the source.
	out = RldPngc_Make(base, baseSize, &outSize, &res);
	RldPngc_Check(out != NULL && res.jpeg == 0 && res.converted == 0 && outSize == baseSize && memcmp(out, base, baseSize) == 0,
	              "hires-png of a model without JPEG: the copy is the source byte for byte");
	RldPngc_FreeResult(&res);
	free(out);

	// A JPEG that cannot be decoded (the rules' test model: FF D8 FF and no more):
	// nothing written, the reason names the texture.
	memset(&sp, 0, sizeof(sp));
	sp.image = 1;
	free(base);
	base = HrTest_Glb(&sp, &baseSize);
	out = (base != NULL) ? RldPngc_Make(base, baseSize, &outSize, &res) : NULL;
	RldPngc_Check(base != NULL && out == NULL && outSize == 0u && res.converted == 0 && strstr(res.why, "texture 0") != NULL &&
	                  strstr(res.why, "cannot be decoded") != NULL,
	              "hires-png: a JPEG that cannot be decoded - nothing made, the texture named");
	free(out);
	free(base);
	free(jpeg);

	printf("hires-png selftest %s: %d checks, %d failures\n", s_pngcFailures ? "FAILED" : "passed", s_pngcChecks, s_pngcFailures);
	return s_pngcFailures != 0;
}
