// rldpack_hires_upscale.inc - hires-upscale: a derived hi-res model whose small
// base colour textures are enlarged with a pixel-art upscaler.
//
// Included by tools/rldpack.c after rldpack_hires.inc (RldHiresPack_Args,
// RldHiresPack_Write) and after the cgltf implementation: the JSON tokenizer
// (jsmn_parse, cgltf_skip_json, cgltf_json_strcmp) is static in that same
// translation unit. The PNG writer is RldMk_PngRgba (rldpack_native.inc), the
// reader include/rldpng.inc - the decoder the game uses.
//
//   hires-upscale <in.glb> -o <out.glb>
//       writes a NEW model: every PNG a material uses as its base colour, with
//       its larger edge below RLDUP_EDGE_SMALL, enlarged 2x or 4x (doubled
//       while the larger edge is below RLDUP_EDGE_SMALL, at most twice).
//       The source file is only read; the output may not be the source.
//
// FORMAT DECISION 10 STAYS: rldpack hires-add stores a GLB byte for byte. This
// command only makes another GLB, which the author packs instead. Nothing of
// the source is rewritten: the JSON chunk keeps every byte except the
// "bufferView" value of each enlarged image and buffers[0].byteLength, and
// gets one new bufferView per enlarged image appended at the end of the
// bufferViews array; the BIN chunk keeps all its bytes and gets the new PNGs
// appended (each at a 4-byte boundary). The old PNGs stay in the BIN, unused
// (their bufferViews too), so every other index and offset is the source's.
// Further chunks after BIN are copied as they are. The same input gives the
// same bytes (the PNGs are written with stored deflate blocks, no time stamp).
//
// THE UPSCALER is Scale2x (the same rule is known as EPX, Eric Johnston,
// 1992), implemented here from the published description of the algorithm by
// Andrea Mazzoleni (the Scale2x project, "Scale2x algorithm"): no code of that
// project or of any other is used. Per source pixel E with the neighbours
// B above, D left, F right, H below (an edge pixel repeats itself), the four
// output pixels are
//   if (B != H && D != F):  E0 = D == B ? D : E   E1 = B == F ? F : E
//                           E2 = D == H ? D : E   E3 = H == F ? F : E
//   else:                   E0 = E1 = E2 = E3 = E
// (E0 top left, E1 top right, E2 bottom left, E3 bottom right), pixels equal
// when all four RGBA bytes are. Scale4x is Scale2x applied twice. It invents
// no colour (every output pixel is one of the source's), keeps straight edges
// sharp and steps diagonals by half a source pixel - pixel art stays pixel
// art when the engine filters it (rs_tex.h, THE ENGINE'S HI-RES FILTER).

#define RLDUP_EDGE_SMALL 128u     // the larger edge below which an image is enlarged
#define RLDUP_FACTOR_MAX 4u
#define RLDUP_IMAGES_MAX 256

static int RldUp_Same(const u8 *a, const u8 *b)
{
	return memcmp(a, b, 4) == 0;
}

// One Scale2x pass: src w x h RGBA, dst (2w) x (2h) RGBA.
static void RldUp_Scale2x(const u8 *src, u32 w, u32 h, u8 *dst)
{
	const size_t dstRow = (size_t)w * 2u * 4u;
	u32 x;
	u32 y;

	for (y = 0; y < h; y++)
	{
		const u32 yUp = (y > 0u) ? (y - 1u) : y;
		const u32 yDown = ((y + 1u) < h) ? (y + 1u) : y;

		for (x = 0; x < w; x++)
		{
			const u32 xLeft = (x > 0u) ? (x - 1u) : x;
			const u32 xRight = ((x + 1u) < w) ? (x + 1u) : x;
			const u8 *e = &src[((size_t)y * w + x) * 4u];
			const u8 *b = &src[((size_t)yUp * w + x) * 4u];
			const u8 *hh = &src[((size_t)yDown * w + x) * 4u];
			const u8 *d = &src[((size_t)y * w + xLeft) * 4u];
			const u8 *f = &src[((size_t)y * w + xRight) * 4u];
			u8 *o0 = &dst[(size_t)(2u * y) * dstRow + (size_t)(2u * x) * 4u];
			u8 *o2 = o0 + dstRow;

			if (!RldUp_Same(b, hh) && !RldUp_Same(d, f))
			{
				memcpy(o0, RldUp_Same(d, b) ? d : e, 4);
				memcpy(o0 + 4, RldUp_Same(b, f) ? f : e, 4);
				memcpy(o2, RldUp_Same(d, hh) ? d : e, 4);
				memcpy(o2 + 4, RldUp_Same(hh, f) ? f : e, 4);
			}
			else
			{
				memcpy(o0, e, 4);
				memcpy(o0 + 4, e, 4);
				memcpy(o2, e, 4);
				memcpy(o2 + 4, e, 4);
			}
		}
	}
}

// The factor for an image of w x h: 1 (left as it is), 2 or 4.
static u32 RldUp_Factor(u32 w, u32 h)
{
	const u32 edge = (w > h) ? w : h;
	u32 factor = 1u;

	while ((factor < RLDUP_FACTOR_MAX) && ((edge * factor) < RLDUP_EDGE_SMALL))
	{
		factor *= 2u;
	}
	return factor;
}

// Scale2x applied log2(factor) times (factor 1, 2 or 4). NULL = out of memory.
static u8 *RldUp_Scale(const u8 *src, u32 w, u32 h, u32 factor)
{
	u8 *cur = (u8 *)malloc((size_t)w * h * 4u);
	u32 f;

	if (cur == NULL)
	{
		return NULL;
	}
	memcpy(cur, src, (size_t)w * h * 4u);
	for (f = 1u; f < factor; f *= 2u)
	{
		u8 *next = (u8 *)malloc((size_t)w * h * 16u);

		if (next == NULL)
		{
			free(cur);
			return NULL;
		}
		RldUp_Scale2x(cur, w, h, next);
		free(cur);
		cur = next;
		w *= 2u;
		h *= 2u;
	}
	return cur;
}

struct RldUpImage
{
	int image;      // the glTF image
	u32 w, h;       // the source size
	u32 factor;     // 2 or 4
	u8 *png;        // the new PNG
	size_t pngSize;
	u32 view;       // its new bufferView
	u32 offset;     // its offset in the new BIN
};

struct RldUpResult
{
	int images;     // images in the file
	int baseColour; // of them used as a base colour
	int upscaled;
	int skipped;    // used as a base colour but left: not embedded, not a PNG the game reads, or large enough
	struct RldUpImage list[RLDUP_IMAGES_MAX];
	char why[200];  // the result is NULL: why
};

struct RldUpEdit
{
	size_t at;      // in the source JSON
	size_t cut;     // bytes of the source replaced (0: an insertion)
	char text[96];
};

static int RldUp_EditOrder(const void *a, const void *b)
{
	const struct RldUpEdit *x = (const struct RldUpEdit *)a;
	const struct RldUpEdit *y = (const struct RldUpEdit *)b;

	return (x->at < y->at) ? -1 : ((x->at > y->at) ? 1 : 0);
}

// The value token of key in the object at token i (-1 = none).
static int RldUp_Member(const jsmntok_t *tok, int count, int i, const u8 *json, const char *key)
{
	int k;
	int at = i + 1;

	if ((i < 0) || (i >= count) || (tok[i].type != JSMN_OBJECT))
	{
		return -1;
	}
	for (k = 0; k < tok[i].size; k++)
	{
		if ((at + 1) >= count)
		{
			return -1;
		}
		if (cgltf_json_strcmp(&tok[at], json, key) == 0)
		{
			return at + 1;
		}
		at = cgltf_skip_json(tok, at + 1);
		if (at < 0)
		{
			return -1;
		}
	}
	return -1;
}

// Element e of the array at token i (-1 = none).
static int RldUp_Element(const jsmntok_t *tok, int count, int i, int e)
{
	int k;
	int at = i + 1;

	if ((i < 0) || (i >= count) || (tok[i].type != JSMN_ARRAY) || (e < 0) || (e >= tok[i].size))
	{
		return -1;
	}
	for (k = 0; k < e; k++)
	{
		at = cgltf_skip_json(tok, at);
		if ((at < 0) || (at >= count))
		{
			return -1;
		}
	}
	return at;
}

static void RldUp_FreeResult(struct RldUpResult *r)
{
	int i;

	for (i = 0; i < r->upscaled; i++)
	{
		free(r->list[i].png);
		r->list[i].png = NULL;
	}
}

// The derived GLB of glb (see the head of this file). NULL with res->why when
// it cannot be made; a model with nothing to enlarge comes back as a copy.
// res->list holds the enlarged images (free with RldUp_FreeResult).
static u8 *RldUp_Glb(const u8 *glb, size_t size, size_t *outSize, struct RldUpResult *res)
{
	cgltf_options options;
	cgltf_data *data = NULL;
	const u8 *json;
	const u8 *bin = NULL;
	u32 jsonLen;
	u32 binLen = 0;
	size_t restAt;
	jsmn_parser parser;
	jsmntok_t *tok = NULL;
	int count;
	int images;
	int views;
	int buffers;
	int viewsCount;
	struct RldUpEdit *edits = NULL;
	int editCount = 0;
	u8 *out = NULL;
	size_t newJsonLen;
	size_t newJsonPad;
	size_t newBinLen;
	size_t newBinPad;
	size_t at;
	int i;
	int k;

	memset(res, 0, sizeof(*res));
	*outSize = 0;
	if ((size < 20u) || (Rld_ReadLE32(&glb[0]) != 0x46546C67u) || (Rld_ReadLE32(&glb[4]) != 2u) || (Rld_ReadLE32(&glb[8]) != size) ||
	    (Rld_ReadLE32(&glb[16]) != 0x4E4F534Au))
	{
		snprintf(res->why, sizeof(res->why), "not a glTF binary of version 2 (header, length or first chunk JSON)");
		return NULL;
	}
	jsonLen = Rld_ReadLE32(&glb[12]);
	if ((size_t)jsonLen > (size - 20u))
	{
		snprintf(res->why, sizeof(res->why), "the JSON chunk runs past the end of the file");
		return NULL;
	}
	json = &glb[20];
	restAt = 20u + jsonLen;
	if (((restAt + 8u) <= size) && (Rld_ReadLE32(&glb[restAt + 4u]) == 0x004E4942u))
	{
		binLen = Rld_ReadLE32(&glb[restAt]);
		if ((size_t)binLen > (size - restAt - 8u))
		{
			snprintf(res->why, sizeof(res->why), "the BIN chunk runs past the end of the file");
			return NULL;
		}
		bin = &glb[restAt + 8u];
		restAt += 8u + binLen;
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

	// Which images to enlarge: used as a base colour, embedded in the BIN, a PNG
	// the game's decoder reads, the larger edge below RLDUP_EDGE_SMALL.
	{
		int used[RLDUP_IMAGES_MAX];

		memset(used, 0, sizeof(used));
		for (i = 0; i < (int)data->materials_count; i++)
		{
			const cgltf_texture *t = data->materials[i].pbr_metallic_roughness.base_color_texture.texture;

			if ((t != NULL) && (t->image != NULL))
			{
				used[cgltf_image_index(data, t->image)] = 1;
			}
		}
		for (i = 0; i < (int)data->images_count; i++)
		{
			const cgltf_image *im = &data->images[i];
			struct RldPngImage png;
			const char *pngWhy = NULL;
			u32 factor;

			if (!used[i])
			{
				continue;
			}
			res->baseColour++;
			if ((im->buffer_view == NULL) || (bin == NULL) || (im->buffer_view->buffer != &data->buffers[0]) || (data->buffers[0].uri != NULL) ||
			    ((im->buffer_view->offset + im->buffer_view->size) > binLen))
			{
				res->skipped++;
				continue;
			}
			memset(&png, 0, sizeof(png));
			if (RldPng_Decode(&bin[im->buffer_view->offset], im->buffer_view->size, &png, &pngWhy) != RLDPNG_OK)
			{
				res->skipped++;
				continue;
			}
			factor = RldUp_Factor(png.width, png.height);
			if (factor == 1u)
			{
				RldPng_Free(&png);
				res->skipped++;
				continue;
			}
			{
				struct RldUpImage *u = &res->list[res->upscaled];
				u8 *big = RldUp_Scale(png.rgba, png.width, png.height, factor);

				u->image = i;
				u->w = png.width;
				u->h = png.height;
				u->factor = factor;
				u->png = (big != NULL) ? RldMk_PngRgba(big, png.width * factor, png.height * factor, &u->pngSize) : NULL;
				free(big);
				RldPng_Free(&png);
				if (u->png == NULL)
				{
					RldUp_FreeResult(res);
					cgltf_free(data);
					snprintf(res->why, sizeof(res->why), "out of memory (image %d)", i);
					return NULL;
				}
				res->upscaled++;
			}
		}
	}
	viewsCount = (int)data->buffer_views_count;
	cgltf_free(data);

	if (res->upscaled == 0)
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

	// The JSON's tokens: images[i].bufferView, the end of bufferViews, buffers[0].byteLength.
	jsmn_init(&parser);
	count = jsmn_parse(&parser, (const char *)json, jsonLen, NULL, 0);
	tok = (count > 0) ? (jsmntok_t *)malloc((size_t)count * sizeof(*tok)) : NULL;
	edits = (struct RldUpEdit *)calloc((size_t)res->upscaled + 2u, sizeof(*edits));
	if ((tok == NULL) || (edits == NULL))
	{
		snprintf(res->why, sizeof(res->why), (count > 0) ? "out of memory" : "the JSON does not tokenize");
		goto fail;
	}
	jsmn_init(&parser);
	if ((jsmn_parse(&parser, (const char *)json, jsonLen, tok, (size_t)count) != count) || (tok[0].type != JSMN_OBJECT))
	{
		snprintf(res->why, sizeof(res->why), "the JSON does not tokenize");
		goto fail;
	}
	images = RldUp_Member(tok, count, 0, json, "images");
	views = RldUp_Member(tok, count, 0, json, "bufferViews");
	buffers = RldUp_Member(tok, count, 0, json, "buffers");
	if ((images < 0) || (views < 0) || (buffers < 0) || (tok[views].type != JSMN_ARRAY) || (tok[views].size != viewsCount) || (tok[views].size == 0))
	{
		snprintf(res->why, sizeof(res->why), "images, bufferViews or buffers not found in the JSON as cgltf read them");
		goto fail;
	}
	{
		const int b0 = RldUp_Element(tok, count, buffers, 0);
		const int length = RldUp_Member(tok, count, b0, json, "byteLength");
		size_t binAt = binLen;

		if ((length < 0) || (tok[length].type != JSMN_PRIMITIVE))
		{
			snprintf(res->why, sizeof(res->why), "buffers[0].byteLength not found");
			goto fail;
		}
		for (i = 0; i < res->upscaled; i++)
		{
			struct RldUpImage *u = &res->list[i];
			const int im = RldUp_Element(tok, count, images, u->image);
			const int bv = RldUp_Member(tok, count, im, json, "bufferView");
			struct RldUpEdit *e = &edits[editCount++];

			if ((bv < 0) || (tok[bv].type != JSMN_PRIMITIVE))
			{
				snprintf(res->why, sizeof(res->why), "images[%d].bufferView not found", u->image);
				goto fail;
			}
			binAt = (binAt + 3u) & ~(size_t)3u;
			u->offset = (u32)binAt;
			u->view = (u32)(viewsCount + i);
			binAt += u->pngSize;
			e->at = (size_t)tok[bv].start;
			e->cut = (size_t)(tok[bv].end - tok[bv].start);
			snprintf(e->text, sizeof(e->text), "%u", u->view);
		}
		newBinLen = binAt;
		edits[editCount].at = (size_t)tok[length].start;
		edits[editCount].cut = (size_t)(tok[length].end - tok[length].start);
		snprintf(edits[editCount].text, sizeof(edits[editCount].text), "%u", (unsigned)newBinLen);
		editCount++;
	}
	// The new bufferViews go in front of the closing bracket of bufferViews.
	edits[editCount].at = (size_t)tok[views].end - 1u;
	edits[editCount].cut = 0;
	edits[editCount].text[0] = 0;
	editCount++;
	if (json[tok[views].end - 1] != ']')
	{
		snprintf(res->why, sizeof(res->why), "bufferViews does not end in ]");
		goto fail;
	}
	qsort(edits, (size_t)editCount, sizeof(*edits), RldUp_EditOrder);

	// Sizes: the JSON grows by the edits and the appended views.
	newJsonLen = jsonLen;
	for (k = 0; k < editCount; k++)
	{
		newJsonLen = newJsonLen - edits[k].cut + strlen(edits[k].text);
	}
	for (i = 0; i < res->upscaled; i++)
	{
		char view[96];

		newJsonLen += (size_t)snprintf(view, sizeof(view), ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", res->list[i].offset,
		                               (unsigned)res->list[i].pngSize);
	}
	newJsonPad = (newJsonLen + 3u) & ~(size_t)3u;
	newBinPad = (newBinLen + 3u) & ~(size_t)3u;
	*outSize = 12u + 8u + newJsonPad + 8u + newBinPad + (size - restAt);
	if (*outSize > 0xFFFFFFFFu)
	{
		snprintf(res->why, sizeof(res->why), "the result would be larger than 4 GiB");
		goto fail;
	}
	out = (u8 *)calloc(1, *outSize);
	if (out == NULL)
	{
		snprintf(res->why, sizeof(res->why), "out of memory");
		goto fail;
	}
	RldHires_PutLE32(&out[0], 0x46546C67u);
	RldHires_PutLE32(&out[4], 2u);
	RldHires_PutLE32(&out[8], (u32)*outSize);
	RldHires_PutLE32(&out[12], (u32)newJsonPad);
	RldHires_PutLE32(&out[16], 0x4E4F534Au);
	{
		u8 *j = &out[20];
		size_t from = 0;

		at = 0;
		for (k = 0; k < editCount; k++)
		{
			memcpy(&j[at], &json[from], edits[k].at - from);
			at += edits[k].at - from;
			if ((edits[k].cut == 0u) && (edits[k].text[0] == 0))
			{
				for (i = 0; i < res->upscaled; i++)
				{
					char view[96];
					const int n = snprintf(view, sizeof(view), ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", res->list[i].offset,
					                       (unsigned)res->list[i].pngSize);

					memcpy(&j[at], view, (size_t)n);
					at += (size_t)n;
				}
			}
			else
			{
				memcpy(&j[at], edits[k].text, strlen(edits[k].text));
				at += strlen(edits[k].text);
			}
			from = edits[k].at + edits[k].cut;
		}
		memcpy(&j[at], &json[from], jsonLen - from);
		at += jsonLen - from;
		// The source's padding (spaces or NULs) was copied with it; pad to 4 with spaces.
		memset(&j[at], ' ', newJsonPad - at);
	}
	at = 20u + newJsonPad;
	RldHires_PutLE32(&out[at], (u32)newBinPad);
	RldHires_PutLE32(&out[at + 4u], 0x004E4942u);
	memcpy(&out[at + 8u], bin, binLen);
	for (i = 0; i < res->upscaled; i++)
	{
		memcpy(&out[at + 8u + res->list[i].offset], res->list[i].png, res->list[i].pngSize);
	}
	at += 8u + newBinPad;
	memcpy(&out[at], &glb[restAt], size - restAt);
	free(tok);
	free(edits);
	return out;

fail:
	free(tok);
	free(edits);
	free(out);
	RldUp_FreeResult(res);
	res->upscaled = 0;
	*outSize = 0;
	return NULL;
}

static int RldHires_UpscaleCommand(int argc, char **argv)
{
	static RldHiresReport rep;
	static struct RldUpResult res;
	const char *paths[1];
	const char *outPath;
	RldHiresModel m;
	u8 *glb = NULL;
	size_t glbSize = 0;
	u8 *out;
	size_t outSize = 0;
	u8 digest[32];
	char hex[65];
	int i;
	int ok;

	if (!RldHiresPack_Args(argc, argv, 1, paths, &outPath))
	{
		fprintf(stderr, "usage: rldpack hires-upscale <in.glb> -o <out.glb>\n");
		return 2;
	}
#ifdef _WIN32
	if (_stricmp(paths[0], outPath) == 0)
#else
	if (strcmp(paths[0], outPath) == 0)
#endif
	{
		fprintf(stderr, "rldpack: hires-upscale writes a new file - the output may not be the source\n");
		Rld_Say("error", "upscale", outPath, "The upscaled model must be a new file; the source model is never changed.");
		return 2;
	}
	if (!Rld_ReadFile(paths[0], &glb, &glbSize))
	{
		fprintf(stderr, "rldpack: %s cannot be opened\n", paths[0]);
		Rld_Say("error", "upscale", paths[0], "%s cannot be opened", paths[0]);
		return 1;
	}
	printf("hires-upscale: %s\n", paths[0]);
	out = RldUp_Glb(glb, glbSize, &outSize, &res);
	if (out == NULL)
	{
		fprintf(stderr, "rldpack: %s: %s\n", paths[0], res.why);
		Rld_Say("error", "upscale", paths[0], "The model cannot be upscaled: %s", res.why);
		free(glb);
		return 1;
	}
	for (i = 0; i < res.upscaled; i++)
	{
		const struct RldUpImage *u = &res.list[i];

		printf("  image %d: %ux%u -> %ux%u (Scale%ux)\n", u->image, u->w, u->h, u->w * u->factor, u->h * u->factor, u->factor);
	}
	printf("hires-upscale: %d of %d images enlarged (%d used as a base colour, %d of them left as they are)\n", res.upscaled, res.images,
	       res.baseColour, res.skipped);

	// The result must open as the source opens: the same verdict of the game's rules.
	RldHires_ReportClear(&rep);
	ok = RldHires_Open(glb, (u32)glbSize, &m, &rep);
	RldHires_Close(&m);
	{
		const int sourceErrors = rep.errors;

		RldHires_ReportClear(&rep);
		if ((RldHires_Open(out, (u32)outSize, &m, &rep) != ok) || ((rep.errors != 0) && (sourceErrors == 0)))
		{
			RldHires_Close(&m);
			fprintf(stderr, "rldpack: the upscaled model does not open as its source does - nothing written\n");
			Rld_Say("error", "upscale", paths[0], "The upscaled model does not open as its source does - nothing written.");
			RldUp_FreeResult(&res);
			free(out);
			free(glb);
			return 1;
		}
		RldHires_Close(&m);
	}
	RldUp_FreeResult(&res);
	free(glb);
	if (!RldHiresPack_Write(outPath, out, outSize))
	{
		free(out);
		fprintf(stderr, "rldpack: %s cannot be written\n", outPath);
		Rld_Say("error", "write", outPath, "The upscaled model cannot be written. Is the folder write-protected, or is the file open in another program?");
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
