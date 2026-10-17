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
// does.
//
// THE PNGs are compressed (RldMk_PngRgbaPacked, rldpack_native.inc: a filter
// per row, miniz's deflate at zlib's default level); the decoded pixels are
// kept as they are - no colour management, no EXIF rotation, as make-char
// reads the same file. The decoder is scalar, the same pixels on every
// processor; the writer has no thread and no time stamp: the same input
// gives the same bytes.
//
// THE NEW BIN (RldPngc_Relay) holds only bytes a bufferView covers. The PNG
// of a JPEG takes the place of the JPEG in that JPEG's own bufferView (the
// image keeps its "bufferView" index) when nothing but converted images refers
// to that bufferView (every "bufferView" key of the JSON is counted, the
// extensions' too) and no other bufferView overlaps it; the JPEG bytes are
// then gone. The data of the other bufferViews keeps its order; overlapping
// ones move together; each run of data starts at the same position modulo 4
// as before (every alignment glTF asks of accessors stays); a PNG starts at a
// 4-byte boundary; bytes no bufferView covers (padding, data of nothing) are
// dropped. The JSON keeps every byte but the "byteOffset" values of the
// bufferViews that moved (a missing one is inserted when it is not 0), the
// "byteLength" of each reused bufferView, the "mimeType" values of the
// converted images and buffers[0].byteLength; no bufferView, accessor or other
// index changes. A JPEG bufferView that something else refers to as well (or
// that overlaps another, or has a byteStride) stays as it is, JPEG bytes
// included; its images get one new bufferView appended at the end of
// bufferViews and their PNG at the end of the BIN (that image's "bufferView"
// value changes). A model that uses an extension not on the list
// s_rldPngcKnownExtensions (one could address BIN bytes in a way not counted
// here, e.g. EXT_meshopt_compression) gets that for every image: the source
// BIN is kept whole and the PNGs are appended (RldUp_Rewrite, as hires-upscale
// does) - the output says so. Further chunks after BIN are copied as they are.
// A model without a JPEG comes back as a copy, byte for byte.
//
// The result must open as its source opens, minus the JPEG: the game's rules
// (RldHires_Open) may not find more errors in it than in the source, none of
// its images may be a JPEG any more, every accessor reads the same values
// (cgltf: the elements' bytes and the unpacked floats, sparse ones included),
// every bufferView that was not converted holds the same bytes, the
// animations and skins use the same accessors, and every converted image
// decodes (include/rldpng.inc, the game's reader) to exactly the pixels of its
// JPEG (RldPngc_Same) - otherwise nothing is written.

struct RldPngcResult
{
	int images;     // images in the file
	int jpeg;       // of them, JPEG bytes embedded in the BIN
	int converted;  // = jpeg when the result was made
	int kept;       // of them, JPEG bufferViews kept in the BIN (referred to elsewhere, or an unknown extension)
	u32 binBefore;  // the BIN chunk's data of the source (bytes)
	u32 binAfter;   // and of the result
	struct RldUpImage list[RLDUP_IMAGES_MAX];   // the converted images (factor 1)
	char keptWhy[120]; // kept > 0: why (the first reason)
	char why[200];  // the result is NULL: why
};

// The extensions with which every reference to BIN data is a "bufferView" key
// (or none at all): any other one keeps the source BIN whole (the head of
// this file). An entry ending in '_' is a prefix.
static const char *const s_rldPngcKnownExtensions[] = {
	"KHR_materials_", "KHR_texture_transform", "KHR_mesh_quantization", "KHR_lights_punctual", "KHR_texture_basisu",
	"KHR_draco_mesh_compression", "KHR_animation_pointer", "KHR_xmp_json_ld", "KHR_node_visibility", "EXT_texture_webp",
	"EXT_texture_avif", "EXT_mesh_gpu_instancing",
};

static int RldPngc_KnownExtension(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(s_rldPngcKnownExtensions) / sizeof(s_rldPngcKnownExtensions[0]); i++)
	{
		const char *k = s_rldPngcKnownExtensions[i];
		const size_t n = strlen(k);

		if ((k[n - 1u] == '_') ? (strncmp(name, k, n) == 0) : (strcmp(name, k) == 0))
		{
			return 1;
		}
	}
	return 0;
}

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

// A JSON number token as an unsigned 32-bit value; 0 when it is not a plain one.
static int RldPngc_Uint(const jsmntok_t *t, const u8 *json, u32 *value)
{
	u32 v = 0;
	int i;

	if ((t->type != JSMN_PRIMITIVE) || (t->end <= t->start) || ((t->end - t->start) > 10))
	{
		return 0;
	}
	for (i = t->start; i < t->end; i++)
	{
		if ((json[i] < '0') || (json[i] > '9') || (v > 429496729u) || ((v == 429496729u) && (json[i] > '5')))
		{
			return 0;
		}
		v = (v * 10u) + (u32)(json[i] - '0');
	}
	*value = v;
	return 1;
}

// A bufferView in the relaid BIN (RldPngc_Relay).
struct RldPngcView
{
	int onBin;      // a bufferView of buffers[0], the BIN
	size_t from;    // its byteOffset and byteLength in the source
	size_t length;
	int inPlace;    // a JPEG bufferView that takes its PNG in place
	int png;        // inPlace: the entry of res->list whose PNG it holds
	int seg;        // not inPlace: the run of data it lies in
	size_t to;      // its byteOffset in the new BIN
};

static const struct RldPngcView *s_rldPngcSortViews;

// bufferView indices by their source byteOffset, then by index.
static int RldPngc_ViewOrder(const void *a, const void *b)
{
	const int x = *(const int *)a;
	const int y = *(const int *)b;
	const struct RldPngcView *v = s_rldPngcSortViews;

	if (v[x].from != v[y].from)
	{
		return (v[x].from < v[y].from) ? -1 : 1;
	}
	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

// The relaid GLB (the head of this file, THE NEW BIN) of glb, whose JPEG
// images res->list[0..res->converted) hold their PNGs; data is glb as cgltf
// parsed and validated it. Sets res->kept, keptWhy, binBefore, binAfter and
// each list entry's view and offset. NULL with res->why when it cannot be made.
static u8 *RldPngc_Relay(const u8 *glb, size_t size, const cgltf_data *data, struct RldPngcResult *res, size_t *outSize)
{
	const u8 *json;
	const u8 *bin;
	u32 jsonLen;
	u32 binLen;
	size_t restAt;
	jsmn_parser parser;
	jsmntok_t *tok = NULL;
	int count = 0;
	int images;
	int viewsTok;
	int buffers;
	const int nViews = (int)data->buffer_views_count;
	struct RldPngcView *v = NULL;
	int *refs = NULL;
	int *own = NULL;
	int *order = NULL;
	size_t *segFrom = NULL;
	size_t *segEnd = NULL;
	size_t *segTo = NULL;
	struct RldUpEdit *edits = NULL;
	int editCount = 0;
	int appended = 0;
	int segs = 0;
	u8 *out = NULL;
	size_t cursor = 0;
	size_t newJsonLen;
	size_t newJsonPad;
	size_t newBinPad;
	size_t at;
	int i;
	int k;

	*outSize = 0;
	if (!RldUp_Chunks(glb, size, &json, &jsonLen, &bin, &binLen, &restAt, res->why, sizeof(res->why)))
	{
		return NULL;
	}
	res->binBefore = binLen;

	// An extension not on the list: the source BIN stays whole, the PNGs are
	// appended (the rewrite of hires-upscale).
	for (i = 0; i < (int)data->extensions_used_count; i++)
	{
		const char *e = data->extensions_used[i];

		if ((e != NULL) && !RldPngc_KnownExtension(e))
		{
			u8 *whole;
			int a;
			int b;

			// Kept: every JPEG bufferView, each once.
			for (a = 0; a < res->converted; a++)
			{
				for (b = 0; (b < a) && (data->images[res->list[b].image].buffer_view != data->images[res->list[a].image].buffer_view); b++)
				{
				}
				res->kept += (b == a);
			}
			snprintf(res->keptWhy, sizeof(res->keptWhy), "the model uses the extension '%.60s'", e);
			whole = RldUp_Rewrite(glb, size, res->list, res->converted, nViews, "image/png", outSize, res->why, sizeof(res->why));
			if (whole != NULL)
			{
				res->binAfter = Rld_ReadLE32(&whole[20u + Rld_ReadLE32(&whole[12])]);
			}
			return whole;
		}
	}

	jsmn_init(&parser);
	count = jsmn_parse(&parser, (const char *)json, jsonLen, NULL, 0);
	tok = (count > 0) ? (jsmntok_t *)malloc((size_t)count * sizeof(*tok)) : NULL;
	v = (struct RldPngcView *)calloc((size_t)nViews + 1u, sizeof(*v));
	refs = (int *)calloc((size_t)nViews + 1u, sizeof(*refs));
	own = (int *)calloc((size_t)nViews + 1u, sizeof(*own));
	order = (int *)calloc((size_t)nViews + 1u, sizeof(*order));
	segFrom = (size_t *)calloc((size_t)nViews + 1u, sizeof(*segFrom));
	segEnd = (size_t *)calloc((size_t)nViews + 1u, sizeof(*segEnd));
	segTo = (size_t *)calloc((size_t)nViews + 1u, sizeof(*segTo));
	edits = (struct RldUpEdit *)calloc((2u * (size_t)nViews) + (2u * (size_t)res->converted) + 2u, sizeof(*edits));
	if ((tok == NULL) || (v == NULL) || (refs == NULL) || (own == NULL) || (order == NULL) || (segFrom == NULL) || (segEnd == NULL) ||
	    (segTo == NULL) || (edits == NULL))
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
	viewsTok = RldUp_Member(tok, count, 0, json, "bufferViews");
	buffers = RldUp_Member(tok, count, 0, json, "buffers");
	if ((images < 0) || (viewsTok < 0) || (buffers < 0) || (tok[viewsTok].type != JSMN_ARRAY) || (tok[viewsTok].size != nViews) || (nViews == 0) ||
	    (json[tok[viewsTok].end - 1] != ']'))
	{
		snprintf(res->why, sizeof(res->why), "images, bufferViews or buffers not found in the JSON as cgltf read them");
		goto fail;
	}

	// Every "bufferView" key of the JSON, wherever it stands (a key is a
	// string token with one value; a string value has none).
	for (k = 0; (k + 1) < count; k++)
	{
		u32 n = 0;

		if ((tok[k].type == JSMN_STRING) && (tok[k].size == 1) && (cgltf_json_strcmp(&tok[k], json, "bufferView") == 0) &&
		    RldPngc_Uint(&tok[k + 1], json, &n) && (n < (u32)nViews))
		{
			refs[n]++;
		}
	}

	// The bufferViews, and which JPEG bufferViews take their PNG in place.
	for (i = 0; i < nViews; i++)
	{
		const cgltf_buffer_view *bv = &data->buffer_views[i];

		v[i].png = -1;
		v[i].seg = -1;
		v[i].onBin = (bv->buffer == &data->buffers[0]);
		v[i].from = bv->offset;
		v[i].length = bv->size;
		if (v[i].onBin && ((bv->offset > binLen) || (bv->size > (binLen - bv->offset))))
		{
			snprintf(res->why, sizeof(res->why), "bufferView %d runs past the end of the BIN", i);
			goto fail;
		}
	}
	for (i = 0; i < res->converted; i++)
	{
		own[(int)(data->images[res->list[i].image].buffer_view - data->buffer_views)]++;
	}
	for (i = 0; i < nViews; i++)
	{
		int alone;

		if (own[i] == 0)
		{
			continue;
		}
		alone = (refs[i] == own[i]) && v[i].onBin && (data->buffer_views[i].stride == 0u);
		for (k = 0; alone && (k < nViews); k++)
		{
			alone = (k == i) || !v[k].onBin || ((v[k].from + v[k].length) <= v[i].from) || (v[k].from >= (v[i].from + v[i].length));
		}
		v[i].inPlace = alone;
		if (!alone)
		{
			if (res->kept == 0)
			{
				if (refs[i] != own[i])
				{
					snprintf(res->keptWhy, sizeof(res->keptWhy), "bufferView %d is used by more than its JPEG image(s)", i);
				}
				else
				{
					snprintf(res->keptWhy, sizeof(res->keptWhy), "bufferView %d overlaps another one or has a byteStride", i);
				}
			}
			res->kept++;
		}
	}
	for (i = 0; i < res->converted; i++)
	{
		struct RldUpImage *u = &res->list[i];
		const int j = (int)(data->images[u->image].buffer_view - data->buffer_views);

		if (v[j].inPlace)
		{
			if (v[j].png < 0)
			{
				v[j].png = i;
			}
			u->view = (u32)j;
		}
		else
		{
			u->view = (u32)(nViews + appended);
			appended++;
		}
	}

	// The new places, in the order of the source data: runs of overlapping
	// bufferViews at the same position modulo 4, the PNGs at 4-byte boundaries.
	for (i = 0; i < nViews; i++)
	{
		order[i] = i;
	}
	s_rldPngcSortViews = v;
	qsort(order, (size_t)nViews, sizeof(*order), RldPngc_ViewOrder);
	for (k = 0; k < nViews; k++)
	{
		const int j = order[k];

		if (!v[j].onBin || v[j].inPlace)
		{
			continue;
		}
		if ((segs > 0) && (v[j].from < segEnd[segs - 1]))
		{
			v[j].seg = segs - 1;
			if ((v[j].from + v[j].length) > segEnd[segs - 1])
			{
				segEnd[segs - 1] = v[j].from + v[j].length;
			}
			continue;
		}
		segFrom[segs] = v[j].from;
		segEnd[segs] = v[j].from + v[j].length;
		v[j].seg = segs;
		segs++;
	}
	{
		int s2 = 0;

		// Runs and PNGs interleaved by their source position (an in-place
		// bufferView overlaps no run).
		for (k = 0; k < nViews; k++)
		{
			const int j = order[k];

			if (!v[j].onBin || !v[j].inPlace)
			{
				continue;
			}
			for (; (s2 < segs) && (segFrom[s2] < v[j].from); s2++)
			{
				segTo[s2] = cursor + (((segFrom[s2] & 3u) - (cursor & 3u)) & 3u);
				cursor = segTo[s2] + (segEnd[s2] - segFrom[s2]);
			}
			cursor = (cursor + 3u) & ~(size_t)3u;
			v[j].to = cursor;
			cursor += res->list[v[j].png].pngSize;
		}
		for (; s2 < segs; s2++)
		{
			segTo[s2] = cursor + (((segFrom[s2] & 3u) - (cursor & 3u)) & 3u);
			cursor = segTo[s2] + (segEnd[s2] - segFrom[s2]);
		}
	}
	for (i = 0; i < nViews; i++)
	{
		if (v[i].onBin && !v[i].inPlace)
		{
			v[i].to = segTo[v[i].seg] + (v[i].from - segFrom[v[i].seg]);
		}
	}
	for (i = 0; i < res->converted; i++)
	{
		struct RldUpImage *u = &res->list[i];

		if (u->view >= (u32)nViews)
		{
			cursor = (cursor + 3u) & ~(size_t)3u;
			u->offset = (u32)cursor;
			cursor += u->pngSize;
		}
		else
		{
			u->offset = (u32)v[u->view].to;
		}
	}
	if (cursor > 0xFFFFFF00u)
	{
		snprintf(res->why, sizeof(res->why), "the result would be larger than 4 GiB");
		goto fail;
	}

	// The JSON's edits.
	for (i = 0; i < nViews; i++)
	{
		const int vt = RldUp_Element(tok, count, viewsTok, i);
		const int off = RldUp_Member(tok, count, vt, json, "byteOffset");
		u32 old = 0;

		if (!v[i].onBin)
		{
			continue;
		}
		if ((vt < 0) || (tok[vt].type != JSMN_OBJECT) || ((off >= 0) && !RldPngc_Uint(&tok[off], json, &old)) || (old != (u32)v[i].from))
		{
			snprintf(res->why, sizeof(res->why), "bufferViews[%d].byteOffset not found as cgltf read it", i);
			goto fail;
		}
		if (v[i].to != v[i].from)
		{
			struct RldUpEdit *e = &edits[editCount++];

			if (off >= 0)
			{
				e->at = (size_t)tok[off].start;
				e->cut = (size_t)(tok[off].end - tok[off].start);
				snprintf(e->text, sizeof(e->text), "%u", (unsigned)v[i].to);
			}
			else
			{
				e->at = (size_t)tok[vt].start + 1u;
				e->cut = 0;
				snprintf(e->text, sizeof(e->text), "\"byteOffset\":%u,", (unsigned)v[i].to);
			}
		}
		if (v[i].inPlace)
		{
			const int len = RldUp_Member(tok, count, vt, json, "byteLength");
			struct RldUpEdit *e = &edits[editCount++];

			if ((len < 0) || (tok[len].type != JSMN_PRIMITIVE))
			{
				snprintf(res->why, sizeof(res->why), "bufferViews[%d].byteLength not found", i);
				goto fail;
			}
			e->at = (size_t)tok[len].start;
			e->cut = (size_t)(tok[len].end - tok[len].start);
			snprintf(e->text, sizeof(e->text), "%u", (unsigned)res->list[v[i].png].pngSize);
		}
	}
	for (i = 0; i < res->converted; i++)
	{
		const struct RldUpImage *u = &res->list[i];
		const int im = RldUp_Element(tok, count, images, u->image);
		const int bv = RldUp_Member(tok, count, im, json, "bufferView");
		const int mt = RldUp_Member(tok, count, im, json, "mimeType");

		if ((bv < 0) || (tok[bv].type != JSMN_PRIMITIVE) || ((mt >= 0) && (tok[mt].type != JSMN_STRING)))
		{
			snprintf(res->why, sizeof(res->why), "images[%d].bufferView or mimeType not found", u->image);
			goto fail;
		}
		if (u->view >= (u32)nViews)
		{
			struct RldUpEdit *e = &edits[editCount++];

			e->at = (size_t)tok[bv].start;
			e->cut = (size_t)(tok[bv].end - tok[bv].start);
			snprintf(e->text, sizeof(e->text), "%u", u->view);
		}
		if (mt >= 0)
		{
			struct RldUpEdit *e = &edits[editCount++];

			e->at = (size_t)tok[mt].start;
			e->cut = (size_t)(tok[mt].end - tok[mt].start);
			snprintf(e->text, sizeof(e->text), "image/png");
		}
	}
	{
		const int b0 = RldUp_Element(tok, count, buffers, 0);
		const int length = RldUp_Member(tok, count, b0, json, "byteLength");

		if ((length < 0) || (tok[length].type != JSMN_PRIMITIVE))
		{
			snprintf(res->why, sizeof(res->why), "buffers[0].byteLength not found");
			goto fail;
		}
		edits[editCount].at = (size_t)tok[length].start;
		edits[editCount].cut = (size_t)(tok[length].end - tok[length].start);
		snprintf(edits[editCount].text, sizeof(edits[editCount].text), "%u", (unsigned)cursor);
		editCount++;
	}
	if (appended > 0)
	{
		// The new bufferViews go in front of the closing bracket of bufferViews.
		edits[editCount].at = (size_t)tok[viewsTok].end - 1u;
		edits[editCount].cut = 0;
		edits[editCount].text[0] = 0;
		editCount++;
	}
	qsort(edits, (size_t)editCount, sizeof(*edits), RldUp_EditOrder);

	// Sizes, then the file.
	newJsonLen = jsonLen;
	for (k = 0; k < editCount; k++)
	{
		newJsonLen = newJsonLen - edits[k].cut + strlen(edits[k].text);
	}
	for (i = 0; i < res->converted; i++)
	{
		if (res->list[i].view >= (u32)nViews)
		{
			char view[96];

			newJsonLen += (size_t)snprintf(view, sizeof(view), ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", res->list[i].offset,
			                               (unsigned)res->list[i].pngSize);
		}
	}
	newJsonPad = (newJsonLen + 3u) & ~(size_t)3u;
	newBinPad = (cursor + 3u) & ~(size_t)3u;
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
				for (i = 0; i < res->converted; i++)
				{
					if (res->list[i].view >= (u32)nViews)
					{
						char view[96];
						const int n = snprintf(view, sizeof(view), ",{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}", res->list[i].offset,
						                       (unsigned)res->list[i].pngSize);

						memcpy(&j[at], view, (size_t)n);
						at += (size_t)n;
					}
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
	{
		u8 *nb = &out[at + 8u];

		// The runs where they were placed, then the PNGs (zeros between).
		for (k = 0; k < segs; k++)
		{
			memcpy(&nb[segTo[k]], &bin[segFrom[k]], segEnd[k] - segFrom[k]);
		}
		for (i = 0; i < res->converted; i++)
		{
			memcpy(&nb[res->list[i].offset], res->list[i].png, res->list[i].pngSize);
		}
	}
	at += 8u + newBinPad;
	memcpy(&out[at], &glb[restAt], size - restAt);
	res->binAfter = (u32)newBinPad;
	free(tok);
	free(v);
	free(refs);
	free(own);
	free(order);
	free(segFrom);
	free(segEnd);
	free(segTo);
	free(edits);
	return out;

fail:
	free(tok);
	free(v);
	free(refs);
	free(own);
	free(order);
	free(segFrom);
	free(segEnd);
	free(segTo);
	free(edits);
	free(out);
	*outSize = 0;
	return NULL;
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
		u->png = RldMk_PngRgbaPacked(rgba, w, h, &u->pngSize);
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
	if (res->converted == 0)
	{
		cgltf_free(data);
		out = (u8 *)malloc(size);
		if (out == NULL)
		{
			snprintf(res->why, sizeof(res->why), "out of memory");
			return NULL;
		}
		memcpy(out, glb, size);
		*outSize = size;
		res->binBefore = binLen;
		res->binAfter = binLen;
		return out;
	}
	out = RldPngc_Relay(glb, size, data, res, outSize);
	cgltf_free(data);
	if (out == NULL)
	{
		RldPngc_FreeResult(res);
		res->converted = 0;
	}
	return out;
}

// A GLB as cgltf parses and validates it, buffers[0] on its BIN (as
// cgltf_load_buffers would set it; nothing else is loaded). NULL = it does not.
static cgltf_data *RldPngc_Parse(const u8 *glb, size_t size)
{
	cgltf_options options;
	cgltf_data *d = NULL;

	memset(&options, 0, sizeof(options));
	options.type = cgltf_file_type_glb;
	if ((cgltf_parse(&options, glb, size, &d) != cgltf_result_success) || (cgltf_validate(d) != cgltf_result_success))
	{
		cgltf_free(d);
		return NULL;
	}
	if ((d->buffers_count > 0u) && (d->buffers[0].uri == NULL) && (d->bin != NULL) && (d->bin_size >= d->buffers[0].size))
	{
		d->buffers[0].data = (void *)d->bin;
	}
	return d;
}

// The index of p in an array starting at base, -1 for NULL.
#define RLDPNGC_INDEX(p, base) (((p) == NULL) ? (ptrdiff_t)-1 : ((p) - (base)))

// "Loads identically" (the head of this file): out, the result of src with
// res, reads the same accessor values, holds the same bytes in every
// bufferView that was not converted, has the same animations and skins, and
// every converted image decodes to the pixels of its JPEG. 0 with why when not.
static int RldPngc_Same(const u8 *src, size_t srcSize, const u8 *out, size_t outSize, const struct RldPngcResult *res, char *why, size_t cap)
{
	cgltf_data *a = RldPngc_Parse(src, srcSize);
	cgltf_data *b = RldPngc_Parse(out, outSize);
	float *fa = NULL;
	float *fb = NULL;
	int ok = 0;
	size_t i;
	size_t k;
	int c;

	if ((a == NULL) || (b == NULL))
	{
		snprintf(why, cap, "the %s does not parse", (a == NULL) ? "source" : "result");
		goto done;
	}
	if ((a->accessors_count != b->accessors_count) || (a->images_count != b->images_count) || (a->animations_count != b->animations_count) ||
	    (a->skins_count != b->skins_count) || (a->meshes_count != b->meshes_count) || (a->nodes_count != b->nodes_count) ||
	    (a->materials_count != b->materials_count) || (a->textures_count != b->textures_count) || (a->buffers_count != b->buffers_count) ||
	    (b->buffer_views_count < a->buffer_views_count))
	{
		snprintf(why, cap, "the numbers of accessors, images, animations, skins, meshes, nodes, materials, textures or buffers differ");
		goto done;
	}

	// Every bufferView not taken by a PNG: the same bytes.
	for (i = 0; i < a->buffer_views_count; i++)
	{
		const cgltf_buffer_view *va = &a->buffer_views[i];
		const cgltf_buffer_view *vb = &b->buffer_views[i];
		const u8 *pa = cgltf_buffer_view_data(va);
		const u8 *pb = cgltf_buffer_view_data(vb);
		int png = 0;

		for (c = 0; c < res->converted; c++)
		{
			png |= (res->list[c].view == (u32)i);
		}
		if (png)
		{
			continue;
		}
		if ((va->size != vb->size) || (va->stride != vb->stride) || (RLDPNGC_INDEX(va->buffer, a->buffers) != RLDPNGC_INDEX(vb->buffer, b->buffers)) ||
		    ((pa == NULL) != (pb == NULL)) || ((pa != NULL) && (memcmp(pa, pb, va->size) != 0)))
		{
			snprintf(why, cap, "bufferView %u does not hold the same bytes", (unsigned)i);
			goto done;
		}
	}

	// Every accessor: the same description, element bytes and values.
	for (i = 0; i < a->accessors_count; i++)
	{
		const cgltf_accessor *x = &a->accessors[i];
		const cgltf_accessor *y = &b->accessors[i];
		cgltf_size n;

		if ((x->component_type != y->component_type) || (x->type != y->type) || (x->count != y->count) || (x->normalized != y->normalized) ||
		    (x->offset != y->offset) || (x->stride != y->stride) || (x->is_sparse != y->is_sparse) ||
		    (RLDPNGC_INDEX(x->buffer_view, a->buffer_views) != RLDPNGC_INDEX(y->buffer_view, b->buffer_views)) ||
		    (x->is_sparse && (x->sparse.count != y->sparse.count)))
		{
			snprintf(why, cap, "accessor %u is described differently", (unsigned)i);
			goto done;
		}
		if ((x->buffer_view != NULL) && !x->is_sparse)
		{
			const u8 *pa = cgltf_buffer_view_data(x->buffer_view);
			const u8 *pb = cgltf_buffer_view_data(y->buffer_view);
			const cgltf_size e = cgltf_calc_size(x->type, x->component_type);

			if ((pa == NULL) != (pb == NULL))
			{
				snprintf(why, cap, "accessor %u: its data is missing", (unsigned)i);
				goto done;
			}
			for (k = 0; (pa != NULL) && (k < x->count); k++)
			{
				if (memcmp(&pa[x->offset + (k * x->stride)], &pb[y->offset + (k * y->stride)], e) != 0)
				{
					snprintf(why, cap, "accessor %u: element %u differs", (unsigned)i, (unsigned)k);
					goto done;
				}
			}
		}
		n = cgltf_accessor_unpack_floats(x, NULL, 0);
		free(fa);
		free(fb);
		fa = (float *)malloc((n + 1u) * sizeof(float));
		fb = (float *)malloc((n + 1u) * sizeof(float));
		if ((fa == NULL) || (fb == NULL))
		{
			snprintf(why, cap, "out of memory");
			goto done;
		}
		if ((cgltf_accessor_unpack_floats(x, fa, n) != cgltf_accessor_unpack_floats(y, fb, n)) || (memcmp(fa, fb, n * sizeof(float)) != 0))
		{
			snprintf(why, cap, "accessor %u does not read the same values", (unsigned)i);
			goto done;
		}
	}

	// Animations and skins: the same accessors and nodes.
	for (i = 0; i < a->animations_count; i++)
	{
		const cgltf_animation *x = &a->animations[i];
		const cgltf_animation *y = &b->animations[i];
		int same = (x->samplers_count == y->samplers_count) && (x->channels_count == y->channels_count);

		for (k = 0; same && (k < x->samplers_count); k++)
		{
			same = (RLDPNGC_INDEX(x->samplers[k].input, a->accessors) == RLDPNGC_INDEX(y->samplers[k].input, b->accessors)) &&
			       (RLDPNGC_INDEX(x->samplers[k].output, a->accessors) == RLDPNGC_INDEX(y->samplers[k].output, b->accessors)) &&
			       (x->samplers[k].interpolation == y->samplers[k].interpolation);
		}
		for (k = 0; same && (k < x->channels_count); k++)
		{
			same = (RLDPNGC_INDEX(x->channels[k].sampler, x->samplers) == RLDPNGC_INDEX(y->channels[k].sampler, y->samplers)) &&
			       (RLDPNGC_INDEX(x->channels[k].target_node, a->nodes) == RLDPNGC_INDEX(y->channels[k].target_node, b->nodes)) &&
			       (x->channels[k].target_path == y->channels[k].target_path);
		}
		if (!same)
		{
			snprintf(why, cap, "animation %u differs", (unsigned)i);
			goto done;
		}
	}
	for (i = 0; i < a->skins_count; i++)
	{
		const cgltf_skin *x = &a->skins[i];
		const cgltf_skin *y = &b->skins[i];
		int same = (x->joints_count == y->joints_count) &&
		           (RLDPNGC_INDEX(x->inverse_bind_matrices, a->accessors) == RLDPNGC_INDEX(y->inverse_bind_matrices, b->accessors));

		for (k = 0; same && (k < x->joints_count); k++)
		{
			same = (RLDPNGC_INDEX(x->joints[k], a->nodes) == RLDPNGC_INDEX(y->joints[k], b->nodes));
		}
		if (!same)
		{
			snprintf(why, cap, "skin %u differs", (unsigned)i);
			goto done;
		}
	}

	// Every converted image: the PNG's pixels are its JPEG's.
	for (c = 0; c < res->converted; c++)
	{
		const cgltf_image *x = &a->images[res->list[c].image];
		const cgltf_image *y = &b->images[res->list[c].image];
		const u8 *jp = (x->buffer_view != NULL) ? cgltf_buffer_view_data(x->buffer_view) : NULL;
		const u8 *pn = (y->buffer_view != NULL) ? cgltf_buffer_view_data(y->buffer_view) : NULL;
		struct RldPngImage got;
		const char *pngWhy = NULL;
		const char *jpegWhy = NULL;
		unsigned w = 0;
		unsigned h = 0;
		u8 *want = NULL;
		int match = 0;

		memset(&got, 0, sizeof(got));
		if ((jp != NULL) && (pn != NULL) && (RldPng_Decode(pn, y->buffer_view->size, &got, &pngWhy) == RLDPNG_OK) &&
		    (RldImg_Decode(jp, x->buffer_view->size, &w, &h, &want, &jpegWhy) == 0))
		{
			match = (got.width == w) && (got.height == h) && (memcmp(got.rgba, want, (size_t)w * h * 4u) == 0);
		}
		RldPng_Free(&got);
		if (want != NULL)
		{
			RldImg_Free(want);
		}
		if (!match)
		{
			snprintf(why, cap, "the PNG of texture %d does not decode to the pixels of its JPEG", res->list[c].image);
			goto done;
		}
	}
	ok = 1;

done:
	free(fa);
	free(fb);
	cgltf_free(a);
	cgltf_free(b);
	return ok;
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
	{
		char sameWhy[160];

		if (!RldPngc_Same(glb, size, out, *outSize, res, sameWhy, sizeof(sameWhy)))
		{
			snprintf(res->why, sizeof(res->why), "the converted model does not load as its source does: %s", sameWhy);
			RldPngc_FreeResult(res);
			res->converted = 0;
			free(out);
			*outSize = 0;
			return NULL;
		}
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
		if (res.kept > 0)
		{
			printf("hires-png: the bytes of %d JPEG(s) stay in the BIN: %s\n", res.kept, res.keptWhy);
		}
		printf("hires-png: BIN %u -> %u bytes\n", (unsigned)res.binBefore, (unsigned)res.binAfter);
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
// HrTest_Glb by the rewrite of hires-upscale - converted, opened clean by the
// game's rules, its pixels those of the decoded JPEG, its accessors reading
// the same values, no JPEG byte left; the same bytes twice; a second
// conversion and a model without a JPEG give their source back; a JPEG that
// cannot be decoded writes nothing. Then the compressed PNG writer
// (RldPngc_SelfTestWriter) and the relaid BIN on models made here
// (RldPngc_SelfTestRelay).

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

// The first place of what (n bytes) in bytes, -1 = none.
static long RldPngc_Find(const u8 *bytes, size_t size, const u8 *what, size_t n)
{
	size_t i;

	for (i = 0; (n > 0u) && ((i + n) <= size); i++)
	{
		if (memcmp(&bytes[i], what, n) == 0)
		{
			return (long)i;
		}
	}
	return -1;
}

// Every accessor of a and b reads the same floats (cgltf_accessor_unpack_floats).
static int RldPngc_SameFloats(const u8 *a, size_t aSize, const u8 *b, size_t bSize)
{
	cgltf_data *x = RldPngc_Parse(a, aSize);
	cgltf_data *y = RldPngc_Parse(b, bSize);
	int same = (x != NULL) && (y != NULL) && (x->accessors_count == y->accessors_count) && (x->accessors_count > 0u);
	size_t i;

	for (i = 0; same && (i < x->accessors_count); i++)
	{
		const cgltf_size n = cgltf_accessor_unpack_floats(&x->accessors[i], NULL, 0);
		float *fx = (float *)calloc(n + 1u, sizeof(float));
		float *fy = (float *)calloc(n + 1u, sizeof(float));

		same = (fx != NULL) && (fy != NULL) && (cgltf_accessor_unpack_floats(&x->accessors[i], fx, n) == n) &&
		       (cgltf_accessor_unpack_floats(&y->accessors[i], fy, n) == n) && (memcmp(fx, fy, n * sizeof(float)) == 0);
		free(fx);
		free(fy);
	}
	cgltf_free(x);
	cgltf_free(y);
	return same;
}

// A GLB of json (padded with spaces) and bin (padded with zeros).
static u8 *RldPngc_TestGlb(const char *json, const u8 *bin, size_t binLen, size_t *sizeOut)
{
	const size_t jl = strlen(json);
	const size_t jp = (jl + 3u) & ~(size_t)3u;
	const size_t bp = (binLen + 3u) & ~(size_t)3u;
	const size_t total = 12u + 8u + jp + 8u + bp;
	u8 *glb = (u8 *)calloc(1, total);

	*sizeOut = 0;
	if (glb == NULL)
	{
		return NULL;
	}
	RldHires_PutLE32(&glb[0], 0x46546C67u);
	RldHires_PutLE32(&glb[4], 2u);
	RldHires_PutLE32(&glb[8], (u32)total);
	RldHires_PutLE32(&glb[12], (u32)jp);
	RldHires_PutLE32(&glb[16], 0x4E4F534Au);
	memcpy(&glb[20], json, jl);
	memset(&glb[20u + jl], ' ', jp - jl);
	RldHires_PutLE32(&glb[20u + jp], (u32)bp);
	RldHires_PutLE32(&glb[24u + jp], 0x004E4942u);
	memcpy(&glb[28u + jp], bin, binLen);
	*sizeOut = total;
	return glb;
}

// bufferView j's byteOffset and image i's bufferView in a GLB (cgltf), -1 = none.
static long RldPngc_ViewOffset(const u8 *glb, size_t size, int j)
{
	cgltf_data *d = RldPngc_Parse(glb, size);
	long at = ((d != NULL) && (j < (int)d->buffer_views_count)) ? (long)d->buffer_views[j].offset : -1;

	cgltf_free(d);
	return at;
}

static int RldPngc_ImageView(const u8 *glb, size_t size, int i)
{
	cgltf_data *d = RldPngc_Parse(glb, size);
	int v = ((d != NULL) && (i < (int)d->images_count) && (d->images[i].buffer_view != NULL)) ? (int)(d->images[i].buffer_view - d->buffer_views) : -1;

	cgltf_free(d);
	return v;
}

// The compressed PNG writer: decodes to its pixels, smaller than the stored
// one, the same bytes twice, and RldMk_PngRgba (stored) unchanged next to it.
static void RldPngc_SelfTestWriter(void)
{
	const u32 w = 64u;
	const u32 h = 48u;
	u8 *rgba = (u8 *)malloc((size_t)w * h * 4u);
	u8 *packed = NULL;
	u8 *packed2 = NULL;
	u8 *stored = NULL;
	size_t packedSize = 0;
	size_t packed2Size = 0;
	size_t storedSize = 0;
	struct RldPngImage got;
	const char *why = NULL;
	u32 x;
	u32 y;

	memset(&got, 0, sizeof(got));
	if (rgba != NULL)
	{
		for (y = 0; y < h; y++)
		{
			for (x = 0; x < w; x++)
			{
				u8 *p = &rgba[((size_t)y * w + x) * 4u];

				p[0] = (u8)(x * 4u);
				p[1] = (u8)(y * 5u);
				p[2] = (u8)((x ^ y) * 3u);
				p[3] = (u8)(((x >= 16u) && (x < 32u) && (y >= 8u) && (y < 24u)) ? 128u : 255u);
			}
		}
		packed = RldMk_PngRgbaPacked(rgba, w, h, &packedSize);
		packed2 = RldMk_PngRgbaPacked(rgba, w, h, &packed2Size);
		stored = RldMk_PngRgba(rgba, w, h, &storedSize);
	}
	RldPngc_Check(packed != NULL && packed2 != NULL && packedSize == packed2Size && memcmp(packed, packed2, packedSize) == 0,
	              "PNG writer (compressed): the same picture gives the same bytes");
	RldPngc_Check(packed != NULL && stored != NULL && (packedSize * 4u) < storedSize,
	              "PNG writer (compressed): under a quarter of the stored PNG (64 x 48 gradient)");
	RldPngc_Check(packed != NULL && RldPng_Decode(packed, packedSize, &got, &why) == RLDPNG_OK && got.width == w && got.height == h &&
	                  memcmp(got.rgba, rgba, (size_t)w * h * 4u) == 0,
	              "PNG writer (compressed): the game's PNG reader gives back exactly the pixels");
	RldPng_Free(&got);
	free(packed);
	free(packed2);
	free(stored);
	free(rgba);
}

// The relaid BIN (RldPngc_Relay) on models made here around the test JPEG:
// views before and after it, bytes no bufferView covers, a view at an odd
// position, an animation; a JPEG view used by an accessor as well; an
// unknown extension; two images on one JPEG view.
static void RldPngc_SelfTestRelay(void)
{
	static char json[4096];
	static u8 bin[2048];
	static struct RldPngcResult res;
	static struct RldPngcResult res2;
	const size_t jpegLen = sizeof(s_rldMkTestJpeg);
	const size_t oJpeg = 44u;
	const size_t oJunk = (oJpeg + jpegLen + 3u) & ~(size_t)3u;
	const size_t oTime = oJunk + 4u;
	const size_t oRot = oTime + 8u;
	const size_t oU8 = oRot + 33u;
	const size_t binLen = oU8 + 3u;
	static const float pos[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
	static const u16 idx[3] = {0u, 1u, 2u};
	static const float times[2] = {0.0f, 1.0f};
	static const float rot[8] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.70710677f, 0.0f, 0.70710677f};
	static const u8 junk[4] = {0xDE, 0xAD, 0xBE, 0xEF};
	int variant;

	if ((binLen > sizeof(bin)) || (jpegLen < 16u))
	{
		RldPngc_Check(0, "relay: the test model fits");
		return;
	}
	memset(bin, 0xEE, sizeof(bin));
	memcpy(&bin[0], pos, sizeof(pos));
	memcpy(&bin[36], idx, sizeof(idx));
	memcpy(&bin[oJpeg], s_rldMkTestJpeg, jpegLen);
	memcpy(&bin[oJunk], junk, sizeof(junk));
	memcpy(&bin[oTime], times, sizeof(times));
	memcpy(&bin[oRot], rot, sizeof(rot));
	bin[oU8] = 7u;
	bin[oU8 + 1u] = 8u;
	bin[oU8 + 2u] = 9u;

	// 0: plain; 1: an accessor reads the JPEG's bufferView too; 2: an unknown
	// extension; 3: two images on the JPEG's bufferView.
	for (variant = 0; variant < 4; variant++)
	{
		u8 *src;
		size_t srcSize = 0;
		u8 *out;
		size_t outSize = 0;
		u8 *again;
		size_t againSize = 0;
		u32 jl;
		u32 bl;
		u32 jl2;
		u32 bl2;
		const u8 *newBin;

		snprintf(json, sizeof(json),
		         "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
		         "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}],"
		         "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],\"textures\":[{\"source\":0}],"
		         "\"images\":[{\"bufferView\":2,\"mimeType\":\"image/jpeg\"}%s],"
		         "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"max\":[1,1,0],\"min\":[0,0,0]},"
		         "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
		         "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"max\":[1],\"min\":[0]},"
		         "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
		         "{\"bufferView\":5,\"componentType\":5121,\"count\":3,\"type\":\"SCALAR\"}%s],"
		         "\"animations\":[{\"samplers\":[{\"input\":2,\"output\":3}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"rotation\"}}]}],"
		         "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
		         "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u},{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":8},"
		         "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":3}],"
		         "\"buffers\":[{\"byteLength\":%u}]%s}",
		         (variant == 3) ? ",{\"bufferView\":2,\"mimeType\":\"image/jpeg\"}" : "",
		         (variant == 1) ? ",{\"bufferView\":2,\"componentType\":5121,\"count\":4,\"type\":\"SCALAR\"}" : "", (unsigned)oJpeg,
		         (unsigned)jpegLen, (unsigned)oTime, (unsigned)oRot, (unsigned)oU8, (unsigned)binLen,
		         (variant == 2) ? ",\"extensionsUsed\":[\"EXT_meshopt_compression\"]" : "");
		src = RldPngc_TestGlb(json, bin, binLen, &srcSize);
		out = (src != NULL) ? RldPngc_Make(src, srcSize, &outSize, &res) : NULL;
		again = (src != NULL) ? RldPngc_Make(src, srcSize, &againSize, &res2) : NULL;
		RldPngc_FreeResult(&res2);
		if ((out == NULL) || (again == NULL))
		{
			printf("       relay variant %d: %s\n", variant, (out == NULL) ? res.why : res2.why);
			RldPngc_Check(0, "relay: the test model is converted");
			RldPngc_FreeResult(&res);
			free(src);
			free(out);
			free(again);
			continue;
		}
		jl = Rld_ReadLE32(&src[12]);
		bl = Rld_ReadLE32(&src[20u + jl]);
		jl2 = Rld_ReadLE32(&out[12]);
		bl2 = Rld_ReadLE32(&out[20u + jl2]);
		newBin = &out[28u + jl2];
		RldPngc_Check(outSize == againSize && memcmp(out, again, outSize) == 0, "relay: the same model gives the same bytes");
		RldPngc_Check(RldPngc_SameFloats(src, srcSize, out, outSize), "relay: every accessor reads the same values (positions, indices, animation, bytes)");
		RldPngc_Check(RldPngc_ViewOffset(out, outSize, 5) >= 0 && (RldPngc_ViewOffset(out, outSize, 5) & 3) == 1 &&
		                  (RldPngc_ViewOffset(out, outSize, 3) & 3) == 0 && (RldPngc_ViewOffset(out, outSize, 4) & 3) == 0 &&
		                  (RldPngc_ViewOffset(out, outSize, 1) & 3) == 0,
		              "relay: every bufferView keeps its position modulo 4");
		if (variant == 0)
		{
			const size_t pngAt = (size_t)RldPngc_ViewOffset(out, outSize, 2);
			const size_t want = ((44u + res.list[0].pngSize + 3u) & ~(size_t)3u) + 8u + 36u;
			u8 *broken = (u8 *)malloc(outSize);
			char why[160];

			RldPngc_Check(res.converted == 1 && res.kept == 0 && RldPngc_ImageView(out, outSize, 0) == 2 && RldPngc_ViewCount(out, outSize) == 6 &&
			                  pngAt == 44u && memcmp(&newBin[pngAt], "\x89PNG", 4u) == 0,
			              "relay: the PNG sits in the JPEG's bufferView 2, at its place");
			RldPngc_Check(bl2 == (u32)want && RldPngc_Find(newBin, bl2, (const u8 *)"\xFF\xD8\xFF", 3u) < 0 && RldPngc_Find(newBin, bl2, junk, 4u) < 0 &&
			                  bl2 < bl + (u32)res.list[0].pngSize,
			              "relay: only covered bytes are left - no JPEG (FF D8 FF), no stray bytes, the BIN exactly as long as laid out");
			RldPngc_Check(memcmp(newBin, pos, sizeof(pos)) == 0 && memcmp(&newBin[36], idx, sizeof(idx)) == 0,
			              "relay: the data in front of the JPEG stays where it was");
			if (broken != NULL)
			{
				memcpy(broken, out, outSize);
				broken[28u + jl2 + 4u] ^= 0x01u;
				RldPngc_Check(!RldPngc_Same(src, srcSize, broken, outSize, &res, why, sizeof(why)) && strstr(why, "bufferView 0") != NULL,
				              "relay: the check finds a changed byte of the geometry");
				free(broken);
			}
			{
				u8 *twice;
				size_t twiceSize = 0;

				twice = RldPngc_Make(out, outSize, &twiceSize, &res2);
				RldPngc_Check(twice != NULL && res2.converted == 0 && twiceSize == outSize && memcmp(twice, out, outSize) == 0,
				              "relay: converting the result again gives it back byte for byte");
				RldPngc_FreeResult(&res2);
				free(twice);
			}
		}
		else if (variant == 1)
		{
			RldPngc_Check(res.kept == 1 && strstr(res.keptWhy, "bufferView 2") != NULL && RldPngc_ImageView(out, outSize, 0) == 6 &&
			                  RldPngc_ViewCount(out, outSize) == 7 && RldPngc_Find(newBin, bl2, s_rldMkTestJpeg, jpegLen) >= 0 &&
			                  RldPngc_Find(newBin, bl2, junk, 4u) < 0,
			              "relay: a JPEG bufferView an accessor reads too stays, bytes and all; the PNG gets a new bufferView; stray bytes go");
		}
		else if (variant == 2)
		{
			RldPngc_Check(res.kept == 1 && strstr(res.keptWhy, "EXT_meshopt_compression") != NULL && bl2 > bl && memcmp(&src[28u + jl], newBin, bl) == 0 &&
			                  RldPngc_ImageView(out, outSize, 0) == 6,
			              "relay: an unknown extension keeps the source BIN whole, the PNG appended");
		}
		else
		{
			RldPngc_Check(res.converted == 2 && res.kept == 0 && RldPngc_ImageView(out, outSize, 0) == 2 && RldPngc_ImageView(out, outSize, 1) == 2 &&
			                  RldPngc_ViewCount(out, outSize) == 6 && RldPngc_Find(newBin, bl2, (const u8 *)"\xFF\xD8\xFF", 3u) < 0,
			              "relay: two images on one JPEG bufferView share its one PNG");
		}
		RldPngc_FreeResult(&res);
		free(src);
		free(out);
		free(again);
	}
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

		const u32 bl2 = Rld_ReadLE32(&out[20u + jl2]);

		RldPngc_Check(res.kept == 0 && res.binBefore == bl && res.binAfter == bl2 && bl2 < (bl - (u32)sizeof(s_rldMkTestJpeg) + (u32)res.list[0].pngSize + 8u) &&
		                  RldPngc_Find(&out[28u + jl2], bl2, (const u8 *)"\xFF\xD8\xFF", 3u) < 0,
		              "hires-png: the JPEG's bytes are gone from the BIN (no FF D8 FF), the BIN no larger than without them");
		RldPngc_Check(RldPngc_ViewCount(out, outSize) == RldPngc_ViewCount(jpeg, jpegSize) && res.list[0].view == (u32)(RldPngc_ViewCount(jpeg, jpegSize) - 1),
		              "hires-png: the PNG takes the JPEG's own bufferView, no bufferView more");
		RldPngc_Check(RldPngc_SameFloats(jpeg, jpegSize, out, outSize), "hires-png: every accessor reads the same values (cgltf, unpacked)");
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

	RldPngc_SelfTestWriter();
	RldPngc_SelfTestRelay();

	printf("hires-png selftest %s: %d checks, %d failures\n", s_pngcFailures ? "FAILED" : "passed", s_pngcChecks, s_pngcFailures);
	return s_pngcFailures != 0;
}
