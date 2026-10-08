// rldpack_wheel.inc - rldpack char-wheel: the preview of an author's wheel model
//
// Part of the packer, never of the game. tools/rldpack.c includes this file
// after tools/rldpack_char.inc and uses its PLY reader (RldMk_ParsePly) and the
// OBJ reader of the native model (RldObj_Parse with its capture of the faces
// as written, tools/rldpack_obj.inc; the UV and color rules of
// tools/rldpack_native.inc) as they are; nothing there is changed for the
// wheel.
//
// WHAT IT DOES. The wheel model of make-char --wheel-model (WHLS version 2,
// docs/CONTAINER_FORMAT.md) for the 3D preview of Reload Studio, by the same
// rules as make-char (RldMk_UserWheelBuild of the wheel package; the message
// codes are its codes): a PLY or an OBJ with its MTL file and one texture of
// ONE wheel is read as it is - no repair, no reduction - and checked against
// the budget of an author's wheel. It is scaled so that its vertex farthest
// from the axle lies at the radius of the wheel the game draws
// (RLDDUM_TIRE_HALF_SIZE game units, tools/rldpack_dummy.inc; the Studio's
// slider "Wheel size" scales it from there) and centred on its box. The only
// output is the --preview file; char-wheel never writes a container and
// refuses --out: make-char packs the wheel.
//
// THE BUDGET (as WHLS version 2; over it is an error, never cut short): the
// faces as fans, triangles without area left out; at most
// RLDWHEEL_TRIANGLES_MAX triangles (wheel-triangles) and RLDWHEEL_POINTS_MAX
// vertices, one per corner with its own v, vt and vn (wheel-vertices); one
// material (wheel-materials) - its texture at most RLDWHEEL_TEXTURE_EDGE_MAX
// pixels on a side, each side a power of two from RLDCHAR_TEX_EDGE_MIN
// (wheel-texture-size), a file that is there and can be read
// (wheel-texture-file; only a missing one leaves the color, warning
// wheel-texture), every corner of a textured wheel with a vt (wheel-uv),
// or its color. The color: an OBJ's material Kd (white on a texture, mid grey
// without either), a PLY's the mean of its vertex colors - a wheel has one
// material, its vertex colors are not drawn.
//
// THE WHEEL'S AXES (one contract for the preview and the game): the centre in
// the origin, the axle along X, the outer side (the rim) toward +X, +Y up, +Z
// forward - the axes of make-char (--up, --forward as there: the body's). A
// wheel wider in X than across is refused (wheel-wide; CNET-10: halfWidth <=
// radius - its axle is probably another one); one that is not round about X
// (height and length differ by more than RLDWHEEL_ROUND) is a warning
// (wheel-off-axis). A wheel without extent across or along its axle is
// refused (wheel-flat).
//
// THE --preview FILE ("RLDPW2"), little endian:
//   "RLDPW2\0\0"               8 bytes
//   u32 width, u32 height,     the texture (0 0 0: none), rows from the top,
//   u32 flags                  straight alpha; the CTXT wrap bits
//                              (RLDCHAR_TEX_WRAP_*: repeat, or clamp for an MTL
//                              -clamp on); a side above RLDMK_NATIVE_PREVIEW_EDGE
//                              is halved until it fits, as in RLDPN1
//   width x height x 4 bytes   RGBA
//   u32 triangles              1 or more
//   per triangle 3 corners of
//     s16 x, s16 y, s16 z      1/16 game units (RLDWHEEL_PREVIEW_UNIT), the wheel's
//                              own axes above, centred on the axle
//     s32 u, s32 v             Q16 of the texture, v from the top (CTXT), clamped
//                              to +-64 (RLDMK_NATIVE_PREVIEW_UV_MAX); 0 without one
//     u8 r, u8 g, u8 b         the wheel's one color (above), the same in every corner
//     u8 pad                   0 (bit 0 = 1 would draw it from both sides)
// Nothing follows the last triangle. The corners of a triangle run
// counter-clockwise seen from outside, as the file has them.
//
// DETERMINISM as make-char: the same files give the same bytes. The file is
// built twice and compared before it is written.

#define RLDWHEEL_TRIANGLES_MAX RLDCHAR_WHEEL_TRIANGLES_MAX   // 1024: one mesh, drawn four times
#define RLDWHEEL_POINTS_MAX RLDCHAR_WHEEL_VERTICES_MAX        // 2048
#define RLDWHEEL_TEXTURE_EDGE_MAX 1024u   // an author's wheel (CNET-12, RLDCHAR_WHEEL_TEXTURE_EDGE_MAX of the format)
#define RLDWHEEL_ROUND 0.8            // height and length within this ratio: round about X
#define RLDWHEEL_PREVIEW_UNIT 16.0    // the file's unit: 1/16 game unit
#define RLDWHEEL_HEAD_BYTES 20u       // magic, width, height, flags
#define RLDWHEEL_READ_MAX (64u * RLDWHEEL_TRIANGLES_MAX)   // triangles taken in before the ones without area are left out
#define RLDWHEEL_CORNER_BYTES 18u
#define RLDWHEEL_ACROSS (2.0 * RLDDUM_TIRE_HALF_SIZE)   // the game's wheel, game units across

struct RldWheelOptions
{
	int upZ;        // --up z
	int backwards;  // --forward -z
};

// The wheel as read: triangles with their corners, before the fit.
struct RldWheelMesh
{
	u32 triangles;
	double *xyz;          // 9 per triangle: the game's axes, game units at scale 1
	double *uv;           // 6 per triangle: CTXT orientation (v from the top)
	u8 rgb[3];            // the wheel's one color
	u32 *key;             // 9 per triangle: v, vt, vn of each corner (RLDMK_NONE: none); key[0] RLDMK_NONE: left out
	u32 points;           // the vertices the triangles use (RLDWHEEL_POINTS_MAX)
	u32 materials;        // materials the faces use (an OBJ; a PLY 0)
	int obj;
	u32 texWidth;         // the texture as read, 0 = none
	u32 texHeight;
	u32 texFlags;         // RLDCHAR_TEX_WRAP_*
	u8 *rgba;             // malloc'd, texWidth x texHeight x 4
	char texture[RLD_PATH_MAX];
	char material[RLDOBJ_NAME_MAX + 1u];
};

struct RldWheelResult
{
	u32 vertices;          // a PLY's vertices, an OBJ's v
	u32 faces;
	u32 triangles;         // in the file
	u32 points;
	u32 materials;
	u32 dropped;           // triangles without area, left out
	int obj;
	int fitted;            // the factor below was found
	double factor;         // 4 significant digits, as --fit crash
	double acrossBefore;   // across (game units) at scale 1
	double across;         // across and wide after the factor
	double width;
	u32 texWidth;          // the texture as read (0 = none) and its file
	u32 texHeight;
	char texture[RLD_PATH_MAX];
	u8 *preview;           // the RLDPW2 file, malloc'ed
	size_t previewSize;
};

static void RldWheel_FreeResult(struct RldWheelResult *r)
{
	free(r->preview);
	memset(r, 0, sizeof(*r));
}

static void RldWheel_FreeMesh(struct RldWheelMesh *w)
{
	free(w->xyz);
	free(w->uv);
	free(w->key);
	free(w->rgba);
	memset(w, 0, sizeof(*w));
}

// Room for `triangles` triangles; 0 = out of memory (said).
static int RldWheel_MeshAlloc(struct RldWheelMesh *w, u32 triangles)
{
	w->triangles = triangles;
	w->xyz = (double *)calloc((size_t)triangles * 9u + 1u, sizeof(double));
	w->uv = (double *)calloc((size_t)triangles * 6u + 1u, sizeof(double));
	w->key = (u32 *)calloc((size_t)triangles * 9u + 3u, sizeof(u32));
	if ((w->xyz == NULL) || (w->uv == NULL) || (w->key == NULL))
	{
		RldMk_OutOfMemory();
		return 0;
	}
	return 1;
}

static int RldWheel_CompareKey(const void *pa, const void *pb)
{
	const u32 *a = (const u32 *)pa;
	const u32 *b = (const u32 *)pb;
	int k;

	for (k = 0; k < 3; k++)
	{
		if (a[k] != b[k])
		{
			return (a[k] < b[k]) ? -1 : 1;
		}
	}
	return 0;
}

// Distinct keys of 3 u32 among `count` (sorted in place).
static u32 RldWheel_Distinct(u32 *keys, u32 count)
{
	u32 distinct = 0;
	u32 i;

	qsort(keys, count, 3u * sizeof(u32), RldWheel_CompareKey);
	for (i = 0; i < count; i++)
	{
		if ((i == 0u) || (RldWheel_CompareKey(&keys[3u * i], &keys[3u * (i - 1u)]) != 0))
		{
			distinct++;
		}
	}
	return distinct;
}

static void RldWheel_SayBig(u32 triangles)
{
	RldMk_Say("error", "wheel-triangles", NULL,
	          "The wheel has %u triangles; a wheel may have at most %u (one mesh, drawn four times). Reduce it in Blender (Decimate modifier) - it is never reduced for you.",
	          triangles, RLDWHEEL_TRIANGLES_MAX);
}

// A PLY: its faces as fans, the mean of its vertex colors. 0 = refused (said).
static int RldWheel_FromPly(const u8 *data, size_t size, const struct RldMkOptions *opt, struct RldWheelMesh *w, struct RldWheelResult *res)
{
	struct RldMkPly ply;
	u64 total[3] = {0, 0, 0};
	u32 triangles = 0;
	u32 at = 0;
	u32 f;
	int ok = 0;
	int a;

	memset(&ply, 0, sizeof(ply));
	if (!RldMk_ParsePly(data, size, &ply))
	{
		return 0;
	}
	res->vertices = ply.vertexCount;
	res->faces = ply.faceCount;
	for (f = 0; f < ply.faceCount; f++)
	{
		triangles += (ply.cornerCount[f] >= 3u) ? (ply.cornerCount[f] - 2u) : 0u;
	}
	if (triangles > RLDWHEEL_READ_MAX)
	{
		RldWheel_SayBig(triangles);
		goto done;
	}
	if (!RldWheel_MeshAlloc(w, triangles))
	{
		goto done;
	}
	for (f = 0; f < ply.vertexCount; f++)
	{
		for (a = 0; a < 3; a++)
		{
			total[a] += ply.rgb[3u * f + (u32)a];
		}
	}
	for (a = 0; a < 3; a++)
	{
		w->rgb[a] = (u8)((ply.vertexCount != 0u) ? ((total[a] + (ply.vertexCount / 2u)) / ply.vertexCount) : 128u);
	}
	for (f = 0; f < ply.faceCount; f++)
	{
		const u32 n = ply.cornerCount[f];
		u32 k;

		for (k = 1; (n >= 3u) && ((k + 1u) < n); k++)
		{
			const u32 pick[3] = {0u, k, k + 1u};
			int c;

			for (c = 0; c < 3; c++)
			{
				const u32 i = pick[c];
				const u32 v = (n > 4u) ? ply.more[ply.moreStart[f] + i] : ply.corners[4u * f + i];
				u32 *key = &w->key[9u * at + 3u * (u32)c];

				RldMk_Axes(opt, &ply.xyz[3u * v], &w->xyz[9u * at + 3u * (u32)c]);
				key[0] = v;
				key[1] = RLDMK_NONE;
				key[2] = RLDMK_NONE;
			}
			at++;
		}
	}
	ok = 1;

done:
	RldMk_FreePly(&ply);
	return ok;
}

// The texture of the wheel's material: read, its sides checked, kept in w.
// 0 = refused (said); a texture that the reader could not find is left out
// (warning wheel-texture) and the wheel is drawn in its color; one that is
// there but cannot be used (unreadable, not a picture the reader takes, over
// 4096 pixels) is refused as in make-char (wheel-texture-file).
static int RldWheel_Texture(const struct RldObjNative *n, const struct RldObjNativeMaterial *m, struct RldWheelMesh *w)
{
	const struct RldObjNativeTexture *t;
	struct RldObjTexture image;
	const char *why = NULL;
	u8 *data = NULL;
	size_t size = 0;
	int fromImg = 0;
	int state;
	u32 wrap;
	int ok = 1;

	if ((m == NULL) || (m->texture == RLDMK_NONE) || (m->texture >= n->textureCount))
	{
		return 1;
	}
	t = &n->textures[m->texture];
	if ((t->state != RLDMK_TEXTURE_OK) && (t->state != RLDMK_TEXTURE_MISSING))
	{
		RldMk_SayWheelTextureFile(t->path, t->state, m->name);
		return 0;
	}
	if ((t->state != RLDMK_TEXTURE_OK) || (RldObj_ReadFile(t->path, RLDOBJ_TEXTURE_BYTES, &data, &size) != RLDOBJ_READ_OK))
	{
		free(data);
		return 1;
	}
	memset(&image, 0, sizeof(image));
	state = RldObj_Decode(t->path, data, size, &image, &fromImg, &why);
	free(data);
	if ((state != RLDMK_TEXTURE_OK) || (image.rgba == NULL))
	{
		return 1;
	}

	if ((image.width > RLDWHEEL_TEXTURE_EDGE_MAX) || (image.height > RLDWHEEL_TEXTURE_EDGE_MAX))
	{
		RldMk_Say("error", "wheel-texture-size", t->path,
		          "The wheel's texture %s is %u x %u pixels; a wheel's texture may have at most %u on a side. It is never scaled: save it at %u x %u or smaller.",
		          Rld_LastComponent(t->path), image.width, image.height, RLDWHEEL_TEXTURE_EDGE_MAX, RLDWHEEL_TEXTURE_EDGE_MAX, RLDWHEEL_TEXTURE_EDGE_MAX);
		ok = 0;
	}
	else if ((image.width < RLDCHAR_TEX_EDGE_MIN) || (image.height < RLDCHAR_TEX_EDGE_MIN) || ((image.width & (image.width - 1u)) != 0u) ||
	         ((image.height & (image.height - 1u)) != 0u))
	{
		RldMk_Say("error", "wheel-texture-size", t->path,
		          "The wheel's texture %s is %u x %u pixels; each side must be a power of two from %u to %u (16, 32, 64 ... %u).", Rld_LastComponent(t->path), image.width,
		          image.height, RLDCHAR_TEX_EDGE_MIN, RLDWHEEL_TEXTURE_EDGE_MAX, RLDWHEEL_TEXTURE_EDGE_MAX);
		ok = 0;
	}
	else
	{
		w->rgba = (u8 *)malloc((size_t)image.width * image.height * 4u);
		if (w->rgba == NULL)
		{
			RldMk_OutOfMemory();
			ok = 0;
		}
		else
		{
			memcpy(w->rgba, image.rgba, (size_t)image.width * image.height * 4u);
			w->texWidth = image.width;
			w->texHeight = image.height;
			wrap = m->clampOn ? 1u : 0u;
			w->texFlags = (wrap << RLDCHAR_TEX_WRAP_U_SHIFT) | (wrap << RLDCHAR_TEX_WRAP_V_SHIFT);
			snprintf(w->texture, sizeof(w->texture), "%s", t->path);
		}
	}
	if (fromImg)
	{
		RldImg_Free(image.rgba);
	}
	else
	{
		free(image.rgba);
	}
	return ok;
}

// An OBJ: its faces as written (the capture of the native model), as fans,
// with the UVs and the color of the wheel's one material. 0 = refused (said).
static int RldWheel_FromObj(const char *path, const u8 *data, size_t size, const struct RldMkOptions *opt, struct RldWheelMesh *w,
                            struct RldWheelResult *res)
{
	const struct RldObjNative *n = &s_rldObjNative;
	const struct RldObjNativeMaterial *m = NULL;
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	u32 *names = NULL;
	u32 triangles = 0;
	u32 at = 0;
	u32 f;
	int ok;
	int a;

	memset(&ply, 0, sizeof(ply));
	memset(&info, 0, sizeof(info));
	info.role = RLDMK_ROLE_MAIN;   // the capture is made for the main model only
	RldObj_FreeNative(&s_rldObjNative);
	s_rldObjNativeWant = 1;
	ok = RldObj_Parse(path, data, size, &ply, &info);
	s_rldObjNativeWant = 0;
	RldMk_FreePly(&ply);
	if (!ok || !n->captured)
	{
		RldObj_FreeNative(&s_rldObjNative);
		return 0;
	}
	ok = 0;
	w->obj = 1;
	res->obj = 1;
	res->vertices = n->vCount;
	res->faces = n->faceCount;

	for (f = 0; f < n->faceCount; f++)
	{
		triangles += (n->faceCorners[f] >= 3u) ? (n->faceCorners[f] - 2u) : 0u;
	}
	if (triangles > RLDWHEEL_READ_MAX)
	{
		RldWheel_SayBig(triangles);
		goto done;
	}

	// One material: the usemtl name of every face (no name counts as one).
	names = (u32 *)calloc((size_t)n->faceCount * 3u + 3u, sizeof(u32));
	if (names == NULL)
	{
		RldMk_OutOfMemory();
		goto done;
	}
	for (f = 0; f < n->faceCount; f++)
	{
		names[3u * f] = (n->faceName[f] != RLDMK_NONE) ? n->faceName[f] : n->nameCount;
	}
	w->materials = RldWheel_Distinct(names, n->faceCount);
	res->materials = w->materials;
	if (w->materials > 1u)
	{
		RldMk_Say("error", "wheel-materials", NULL,
		          "The wheel uses %u materials; a wheel has one (one texture, or one color). Join them in Blender into one material with one texture (a texture atlas).",
		          w->materials);
		goto done;
	}
	m = ((n->faceCount != 0u) && (n->faceName[0] != RLDMK_NONE) && (n->faceName[0] < n->nameCount)) ? &n->names[n->faceName[0]] : NULL;
	snprintf(w->material, sizeof(w->material), "%s", (m != NULL) ? m->name : "(none)");
	if (!RldWheel_Texture(n, m, w))
	{
		goto done;
	}
	if ((w->rgba == NULL) && (m != NULL) && m->hasMap)
	{
		RldMk_Say("warning", "wheel-texture", m->name, "The texture of the wheel's material \"%s\" is not there; the wheel is drawn in the material's color.",
		          m->name);
	}
	{
		double c[3];

		for (a = 0; a < 3; a++)
		{
			c[a] = ((m != NULL) && m->hasKd) ? m->kd[a] : ((w->rgba != NULL) ? 1.0 : (128.0 / 255.0));
		}
		RldObj_Store(w->rgb, c);
	}

	if (!RldWheel_MeshAlloc(w, triangles))
	{
		goto done;
	}
	for (f = 0; f < n->faceCount; f++)
	{
		const u32 count = n->faceCorners[f];
		u32 k;

		for (k = 1; (count >= 3u) && ((k + 1u) < count); k++)
		{
			const u32 pick[3] = {0u, k, k + 1u};
			int c;

			for (c = 0; c < 3; c++)
			{
				const u32 corner = n->faceFirst[f] + pick[c];
				const u32 v = n->cornerV[corner];
				u32 *key = &w->key[9u * at + 3u * (u32)c];

				RldMk_Axes(opt, &n->xyz[3u * v], &w->xyz[9u * at + 3u * (u32)c]);
				RldMk_NativeUv(n, m, n->cornerT[corner], &w->uv[6u * at + 2u * (u32)c]);
				key[0] = v;
				key[1] = n->cornerT[corner];
				key[2] = n->cornerN[corner];
			}
			at++;
		}
	}
	ok = 1;

done:
	free(names);
	RldObj_FreeNative(&s_rldObjNative);
	return ok;
}

// A position in the file's unit, kept in 16 bits.
static int RldWheel_Unit(double value)
{
	s64 unit = RldMk_Round(value * RLDWHEEL_PREVIEW_UNIT);

	unit = (unit < -32768) ? -32768 : ((unit > 32767) ? 32767 : unit);
	return (int)unit;
}

// The RLDPW2 bytes of the wheel, times factor and shifted by `shift`. NULL = out of memory.
static u8 *RldWheel_Preview(const struct RldWheelMesh *w, double factor, const double shift[3], struct RldWheelResult *res)
{
	struct RldCharTexture source;
	u8 *texture = NULL;
	u32 tw = 0;
	u32 th = 0;
	size_t size;
	size_t at;
	u8 *out;
	u32 count = 0;
	u32 t;

	if (w->rgba != NULL)
	{
		source.width = w->texWidth;
		source.height = w->texHeight;
		source.flags = w->texFlags;
		source.rgba = w->rgba;
		texture = RldMk_NativePreviewTexture(&source, &tw, &th);
		if (texture == NULL)
		{
			return NULL;
		}
	}
	size = RLDWHEEL_HEAD_BYTES + (size_t)tw * th * 4u + 4u + (size_t)w->triangles * 3u * RLDWHEEL_CORNER_BYTES;
	out = (u8 *)calloc(1, size);
	if (out == NULL)
	{
		free(texture);
		return NULL;
	}
	memcpy(out, "RLDPW2\0\0", 8u);
	Rld_WriteLE32(&out[8], tw);
	Rld_WriteLE32(&out[12], th);
	Rld_WriteLE32(&out[16], (texture != NULL) ? w->texFlags : 0u);
	at = RLDWHEEL_HEAD_BYTES;
	if (texture != NULL)
	{
		memcpy(&out[at], texture, (size_t)tw * th * 4u);
		at += (size_t)tw * th * 4u;
	}
	at += 4u;   // the count, below

	for (t = 0; t < w->triangles; t++)
	{
		int v[3][3];
		int k;
		int a;

		if (w->key[9u * t] == RLDMK_NONE)
		{
			continue;   // left out: no area (RldWheel_BuildOnce)
		}
		for (k = 0; k < 3; k++)
		{
			for (a = 0; a < 3; a++)
			{
				v[k][a] = RldWheel_Unit((w->xyz[9u * t + 3u * (u32)k + (u32)a] + shift[a]) * factor);
			}
		}

		for (k = 0; k < 3; k++)
		{
			for (a = 0; a < 3; a++)
			{
				Rld_WriteLE16(&out[at + 2u * (u32)a], (u32)v[k][a] & 0xFFFFu);
			}
			Rld_WriteLE32(&out[at + 6u], (u32)RldMk_NativePreviewQ16((texture != NULL) ? w->uv[6u * t + 2u * (u32)k] : 0.0));
			Rld_WriteLE32(&out[at + 10u], (u32)RldMk_NativePreviewQ16((texture != NULL) ? w->uv[6u * t + 2u * (u32)k + 1u] : 0.0));
			memcpy(&out[at + 14u], w->rgb, 3u);
			out[at + 17u] = 0u;
			at += RLDWHEEL_CORNER_BYTES;
		}
		count++;
	}
	free(texture);

	Rld_WriteLE32(&out[RLDWHEEL_HEAD_BYTES + (size_t)tw * th * 4u], count);
	res->triangles = count;
	res->previewSize = at;
	return out;
}

// Read, check, fit, centre, the file - once. `path` names the model (an OBJ
// finds its MTL file and texture by it); the bytes are in data. 1 = the
// wheel is usable; every reason against it was said (RldMk_Say).
static int RldWheel_BuildOnce(const char *path, const u8 *data, size_t size, const struct RldWheelOptions *wo, struct RldWheelResult *res)
{
	struct RldMkOptions opt;
	struct RldWheelMesh w;
	double lo[3];
	double hi[3];
	double shift[3];
	double across;
	const int errors = s_rldMkErrors;
	const int obj = !((size >= 3u) && (memcmp(data, "ply", 3u) == 0)) && RldObj_Detect(data, size);
	u32 i;
	int ok = 0;
	int a;

	memset(res, 0, sizeof(*res));
	memset(&w, 0, sizeof(w));
	RldMk_DefaultOptions(&opt);
	opt.upZ = wo->upZ;
	opt.backwards = wo->backwards;
	opt.scale = 1.0;

	if (obj ? !RldWheel_FromObj(path, data, size, &opt, &w, res) : !RldWheel_FromPly(data, size, &opt, &w, res))
	{
		goto done;
	}
	// The triangles without area (two corners on one v, or no area at all)
	// are left out, as make-char leaves them out; the rest is counted.
	{
		u32 *keys = (u32 *)calloc((size_t)w.triangles * 9u + 3u, sizeof(u32));
		u32 kept = 0;
		u32 noUv = 0;
		u32 t;

		if (keys == NULL)
		{
			RldMk_OutOfMemory();
			goto done;
		}
		for (a = 0; a < 3; a++)
		{
			lo[a] = 1.0e300;
			hi[a] = -1.0e300;
		}
		for (t = 0; t < w.triangles; t++)
		{
			const u32 *key = &w.key[9u * t];
			const double *p = &w.xyz[9u * t];
			double n[3];

			RldMk_Cross(&p[0], &p[3], &p[6], n);
			if ((key[0] == key[3]) || (key[3] == key[6]) || (key[0] == key[6]) || (((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2])) == 0.0))
			{
				w.key[9u * t] = RLDMK_NONE;
				res->dropped++;
				continue;
			}
			memcpy(&keys[9u * kept], key, 9u * sizeof(u32));
			for (i = 0; i < 3u; i++)
			{
				noUv += ((w.rgba != NULL) && (key[3u * i + 1u] == RLDMK_NONE)) ? 1u : 0u;
				for (a = 0; a < 3; a++)
				{
					lo[a] = (p[3u * i + (u32)a] < lo[a]) ? p[3u * i + (u32)a] : lo[a];
					hi[a] = (p[3u * i + (u32)a] > hi[a]) ? p[3u * i + (u32)a] : hi[a];
				}
			}
			kept++;
		}
		w.points = RldWheel_Distinct(keys, 3u * kept);
		free(keys);
		res->triangles = kept;
		res->points = w.points;
		res->materials = w.materials;
		res->texWidth = w.texWidth;
		res->texHeight = w.texHeight;
		snprintf(res->texture, sizeof(res->texture), "%s", w.texture);
		if (kept > RLDWHEEL_TRIANGLES_MAX)
		{
			RldWheel_SayBig(kept);
			goto done;
		}
		if (w.points > RLDWHEEL_POINTS_MAX)
		{
			RldMk_Say("error", "wheel-vertices", NULL,
			          "The wheel has %u vertices (one per corner with its own v, vt and vn); a wheel may have at most %u. Reduce it in Blender (Decimate modifier), or merge its vertices (Merge by Distance).",
			          w.points, RLDWHEEL_POINTS_MAX);
			goto done;
		}
		if (noUv != 0u)
		{
			RldMk_Say("error", "wheel-uv", NULL, "%u corners of the textured wheel have no texture coordinates (vt). Unwrap the wheel (UV) in Blender.", noUv);
			goto done;
		}
		if ((kept == 0u) || (w.points < 3u))
		{
			RldMk_Say("error", "wheel-flat", NULL, "The wheel has no triangle with an area. Export the whole wheel, round about its axle.");
			goto done;
		}
	}

	// Centred on its box; the vertex farthest from the axle at the radius.
	for (a = 0; a < 3; a++)
	{
		shift[a] = -0.5 * (lo[a] + hi[a]);
	}
	{
		double far2 = 0.0;
		double halfX = 0.0;
		u32 t;

		for (t = 0; t < w.triangles; t++)
		{
			if (w.key[9u * t] == RLDMK_NONE)
			{
				continue;
			}
			for (i = 0; i < 3u; i++)
			{
				const double *p = &w.xyz[9u * t + 3u * i];
				const double y = p[1] + shift[1];
				const double z = p[2] + shift[2];
				const double x = (p[0] + shift[0] < 0.0) ? -(p[0] + shift[0]) : (p[0] + shift[0]);

				far2 = ((y * y) + (z * z) > far2) ? ((y * y) + (z * z)) : far2;
				halfX = (x > halfX) ? x : halfX;
			}
		}
		if (!(far2 > 0.0) || !(halfX > 0.0))
		{
			RldMk_Say("error", "wheel-flat", NULL, "The wheel has %s. Export the whole wheel: round about its axle, the axle along X (from left to right).",
			          (far2 > 0.0) ? "no width along its axle (X)" : "no height and no length - all its corners lie on the axle");
			goto done;
		}
		across = 2.0 * RldMk_Sqrt(far2);
		res->acrossBefore = across;
		res->fitted = 1;
		res->factor = RLDWHEEL_ACROSS / across;
		res->across = RLDWHEEL_ACROSS;
		res->width = 2.0 * halfX * res->factor;
	}
	if (res->width > res->across)
	{
		RldMk_Say("error", "wheel-wide", NULL,
		          "The wheel is %.1f wide but only %.1f across: its axle is probably not the X axis. Turn it in Blender so that the axle runs from left to right (a wheel is at most as wide as it is across).",
		          res->width, res->across);
		goto done;
	}
	if (((hi[1] - lo[1]) < (RLDWHEEL_ROUND * (hi[2] - lo[2]))) || ((hi[2] - lo[2]) < (RLDWHEEL_ROUND * (hi[1] - lo[1]))))
	{
		RldMk_Say("warning", "wheel-off-axis", NULL,
		          "The wheel is %.1f high but %.1f long: it is not round about the X axis, so its axle is probably another one. Turn it in Blender so that the axle runs from left to right.",
		          (hi[1] - lo[1]) * res->factor, (hi[2] - lo[2]) * res->factor);
	}

	res->preview = RldWheel_Preview(&w, res->factor, shift, res);
	if (res->preview == NULL)
	{
		RldMk_OutOfMemory();
		goto done;
	}
	if (res->triangles == 0u)
	{
		RldMk_Say("error", "ply-no-faces", NULL, "The wheel has no triangles left. Export the mesh, not only its points.");
		goto done;
	}
	ok = (s_rldMkErrors == errors);

done:
	RldWheel_FreeMesh(&w);
	return ok;
}

// Read twice; the second build silent. 1 = usable and both builds the same.
static int RldWheel_Build(const char *path, const u8 *data, size_t size, const struct RldWheelOptions *wo, struct RldWheelResult *res)
{
	struct RldWheelResult again;
	const int quiet = s_rldMkQuiet;
	const int errors = s_rldMkErrors;
	const int warnings = s_rldMkWarnings;
	int same;

	if (!RldWheel_BuildOnce(path, data, size, wo, res))
	{
		return 0;
	}

	s_rldMkQuiet = 1;
	same = RldWheel_BuildOnce(path, data, size, wo, &again) && (again.previewSize == res->previewSize) &&
	       (memcmp(again.preview, res->preview, res->previewSize) == 0);
	s_rldMkQuiet = quiet;
	s_rldMkErrors = errors;
	s_rldMkWarnings = warnings;
	RldWheel_FreeResult(&again);
	if (!same)
	{
		RldMk_Say("error", "model-roundtrip", "two builds of the same wheel differ", "Internal error: two builds of the same wheel differ. Please report it.");
		return 0;
	}
	return 1;
}

// A switch of two words (--up): *out = 1 for `yes`, 0 for `no`; anything
// else is said with `code` and `text`.
static void RldWheel_Word(const char *arg, const char *yes, const char *no, int *out, const char *code, const char *text)
{
	char word[16];
	const int known = RldMk_Lower(arg, word, sizeof(word));

	if (known && (strcmp(word, yes) == 0))
	{
		*out = 1;
	}
	else if (known && (strcmp(word, no) == 0))
	{
		*out = 0;
	}
	else
	{
		RldMk_Say("error", code, arg, "%s", text);
	}
}

static int RldWheel_Command(int argc, char **argv)
{
	const char *modelArg = NULL;
	const char *previewArg = NULL;
	const char *upArg = NULL;
	const char *forwardArg = NULL;
	struct RldWheelOptions wo;
	struct RldWheelResult res;
	u8 *data = NULL;
	size_t size = 0;
	char number[4][32];
	int built;
	int i;

	memset(&wo, 0, sizeof(wo));
	memset(&res, 0, sizeof(res));
	RldMk_ResetSaid();
	RldObj_ResetOptions();

	for (i = 0; i < argc; i++)
	{
		const char *arg = argv[i];
		const int hasValue = ((i + 1) < argc);

		if ((strcmp(arg, "--model") == 0) && hasValue)
		{
			modelArg = argv[++i];
		}
		else if ((strcmp(arg, "--preview") == 0) && hasValue)
		{
			previewArg = argv[++i];
		}
		else if ((strcmp(arg, "--up") == 0) && hasValue)
		{
			upArg = argv[++i];
		}
		else if ((strcmp(arg, "--forward") == 0) && hasValue)
		{
			forwardArg = argv[++i];
		}
		else if (strcmp(arg, "--out") == 0)
		{
			RldMk_Say("error", "usage", arg, "char-wheel writes no container: make-char --wheel-model packs the wheel. It only writes --preview <file>.");
			return 2;
		}
		else
		{
			RldMk_Say("error", "usage", arg, "rldpack does not know the argument \"%s\".", arg);
			if (!s_rldMkQuiet)
			{
				fprintf(stderr, "\n");
				RldWheel_Usage(stderr);
			}
			return 2;
		}
	}

	if (modelArg == NULL)
	{
		RldMk_Say("error", "usage", NULL, "No wheel model was given (--model <obj|ply>).");
		if (!s_rldMkQuiet)
		{
			fprintf(stderr, "\n");
			RldWheel_Usage(stderr);
		}
		return 2;
	}

	// A wrong value is said and the other checks go on, as with make-char.
	if (upArg != NULL)
	{
		RldWheel_Word(upArg, "z", "y", &wo.upZ, "up", "The up axis must be y or z.");
	}
	if ((forwardArg != NULL) && (strcmp(forwardArg, "z") != 0) && (strcmp(forwardArg, "+z") != 0))
	{
		if (strcmp(forwardArg, "-z") == 0)
		{
			wo.backwards = 1;
		}
		else
		{
			RldMk_Say("error", "forward", forwardArg, "Forward must be z or -z.");
		}
	}

	Rld_Emit("value", "model", modelArg, "switch", (const char *)NULL);
	Rld_Emit("value", "up", wo.upZ ? "z" : "y", (upArg != NULL) ? "switch" : "default", (const char *)NULL);
	Rld_Emit("value", "forward", wo.backwards ? "-z" : "z", (forwardArg != NULL) ? "switch" : "default", (const char *)NULL);
	Rld_Emit("value", "preview", (previewArg != NULL) ? previewArg : "", (previewArg != NULL) ? "switch" : "default", (const char *)NULL);
	if (s_rldMkErrors != 0)
	{
		return 2;
	}

	if (!Rld_ReadFile(modelArg, &data, &size))
	{
		Rld_Emit("file", "wheel", "missing", modelArg, "0", (const char *)NULL);
		if (!s_rldReadTooLarge)  // file-too-large said why already
		{
			RldMk_Say("error", "ply-open", modelArg, "The wheel model file %s cannot be opened.", modelArg);
		}
		return 1;
	}
	snprintf(number[0], sizeof(number[0]), "%u", (u32)size);
	Rld_Emit("file", "wheel", "ok", modelArg, number[0], (const char *)NULL);
	if (!s_rldMkQuiet)
	{
		printf("rldpack char-wheel %s\n", modelArg);
	}

	built = RldWheel_Build(modelArg, data, size, &wo, &res);
	free(data);

	// The facts for Reload Studio (@char), as far as the reading came.
	if (s_machine && !s_rldMkQuiet)
	{
		snprintf(number[0], sizeof(number[0]), "%u", res.points);
		snprintf(number[1], sizeof(number[1]), "%u", res.materials);
		Rld_Emit("char", "wheel-mesh", res.obj ? "obj" : "ply", number[0], number[1], (const char *)NULL);
		if (res.texWidth != 0u)
		{
			snprintf(number[0], sizeof(number[0]), "%u", res.texWidth);
			snprintf(number[1], sizeof(number[1]), "%u", res.texHeight);
			Rld_Emit("char", "wheel-texture", number[0], number[1], res.texture, (const char *)NULL);
		}
		else
		{
			Rld_Emit("char", "wheel-texture", "none", (const char *)NULL);
		}
		if (res.fitted)
		{
			snprintf(number[0], sizeof(number[0]), "%.4g", res.factor);
			snprintf(number[1], sizeof(number[1]), "%.1f", res.acrossBefore);
			snprintf(number[2], sizeof(number[2]), "%.1f", res.across);
			Rld_Emit("char", "fit", number[0], number[1], number[2], (const char *)NULL);
		}
	}
	if (!s_rldMkQuiet)
	{
		printf("  read          %s, %u vertices, %u faces, %u points, %u material(s)\n", res.obj ? "OBJ" : "PLY", res.vertices, res.faces, res.points,
		       res.materials);
		if (res.texWidth != 0u)
		{
			printf("  texture       %s, %u x %u\n", res.texture, res.texWidth, res.texHeight);
		}
	}

	if (!built)
	{
		RldWheel_FreeResult(&res);
		return 1;
	}

	snprintf(number[0], sizeof(number[0]), "%u", res.triangles);
	snprintf(number[1], sizeof(number[1]), "%.1f", res.across);
	snprintf(number[2], sizeof(number[2]), "%.1f", res.width);
	if (s_machine && !s_rldMkQuiet)
	{
		Rld_Emit("char", "wheel", number[0], number[1], number[2], "0", (const char *)NULL);
		Rld_Emit("value", "wheel-size", number[1], "built", (const char *)NULL);
	}
	if (!s_rldMkQuiet)
	{
		printf("  wheel         %u triangles, %.1f across, %.1f wide in game units\n", res.triangles, res.across, res.width);
		printf("                x%.4g: %.1f -> %.1f across (the game's wheel: %.0f); centred on the axle; built twice\n", res.factor, res.acrossBefore,
		       res.across, RLDWHEEL_ACROSS);
	}

	if (previewArg != NULL)
	{
		FILE *file = fopen(previewArg, "wb");
		const int openError = (file == NULL) ? errno : 0;

		if ((file == NULL) || (fwrite(res.preview, 1, res.previewSize, file) != res.previewSize) || (fclose(file) != 0))
		{
			RldMk_EmitFile("preview", "failed", previewArg, 0u);
			if (!RldMk_SayMissingFolder("error", "preview", previewArg, "The preview file", openError))
			{
				RldMk_Say("error", "preview", previewArg, "The preview file %s cannot be written. Is the folder write-protected?", previewArg);
			}
			RldWheel_FreeResult(&res);
			return 1;
		}
		RldMk_EmitFile("preview", "ok", previewArg, res.previewSize);
		if (!s_rldMkQuiet)
		{
			printf("  preview       %s, %u bytes\n", previewArg, (u32)res.previewSize);
		}
	}

	RldWheel_FreeResult(&res);
	return 0;
}

static void RldWheel_Usage(FILE *out)
{
	fprintf(out, "CHAR-WHEEL - THE PREVIEW OF AN AUTHOR'S WHEEL MODEL\n");
	fprintf(out, "  rldpack char-wheel --model <wheel.obj|wheel.ply> [--preview <file>] [switches]\n");
	fprintf(out, "  Reads one wheel as make-char --wheel-model takes it - an OBJ with its MTL\n");
	fprintf(out, "  file and one texture, or a PLY with vertex colors - as it is (never\n");
	fprintf(out, "  repaired or reduced), checks the budget of a wheel (at most %u triangles,\n", RLDWHEEL_TRIANGLES_MAX);
	fprintf(out, "  %u points, a texture of at most %u x %u), scales it to the size of the\n", RLDWHEEL_POINTS_MAX, RLDWHEEL_TEXTURE_EDGE_MAX,
	        RLDWHEEL_TEXTURE_EDGE_MAX);
	fprintf(out, "  game's wheel (%.0f game units across) and centres it on its axle. The\n", RLDWHEEL_ACROSS);
	fprintf(out, "  wheel: axle along X, outer side +X, +Y up, +Z forward. It writes only the\n");
	fprintf(out, "  preview file for Reload Studio (RLDPW2, tools/rldpack_wheel.inc), never a\n");
	fprintf(out, "  container: --out is refused.\n");
	fprintf(out, "  --preview <file>            the wheel for the 3D preview of Reload Studio\n");
	fprintf(out, "  --up y|z, --forward z|-z    the axes of the model, as with make-char\n");
	fprintf(out, "\n");
	fprintf(out, "  machine lines (--machine): @value model, up, forward, preview; @file wheel;\n");
	fprintf(out, "  @msg; @char wheel-mesh <obj|ply> <points> <materials>, wheel-texture\n");
	fprintf(out, "  <width> <height> <file> | none, fit <factor> <across before> <after>,\n");
	fprintf(out, "  wheel <triangles> <across> <wide> 0; @value wheel-size <across> built;\n");
	fprintf(out, "  @file preview; @end. No @result: nothing is built.\n");
	fprintf(out, "\n");
}

//========================================================================================
// SELF-TEST
//========================================================================================
//
// Every wheel is made here, in memory: PLY text (RldMk_TestPly), and an OBJ
// with its MTL file and PNG through s_rldObjFiles (as the OBJ reader's own
// tests do); no file is read or written. The cases: size and centre, scale,
// the axes, the triangle and point limits (never reduced), a wheel too wide
// and one on the Y axle, a flat one, an OBJ with its texture and UVs, a
// texture too large and one of a wrong size, two materials, two builds the
// same, and --out refused.

static int s_rldWheelTestFailed;

static void RldWheel_Test(int ok, const char *name, const char *format, ...)
{
	char text[320];
	va_list list;

	if (ok)
	{
		printf("  ok   char-wheel %s\n", name);
		return;
	}

	va_start(list, format);
	vsnprintf(text, sizeof(text), format, list);
	va_end(list);
	printf("  FAIL char-wheel %s: %s\n", name, text);
	s_rldWheelTestFailed++;
}

// A closed wheel: `segments` around the axle X, `radius` and half `width` in
// PLY units, its centre at c; quads on the tread, triangle fans on the sides,
// wound so that the right-hand normal points out. upZ: written with +Z up and
// the front along -Y (Blender's axes, RldMk_Axes), the axle still X.
static void RldWheel_TestWheel(struct RldMkTestMesh *mesh, u32 segments, double radius, double width, const double c[3], int upZ)
{
	u32 ring[2];
	u32 hub[2];
	u32 i;
	int side;

	memset(mesh, 0, sizeof(*mesh));
	for (side = 0; side < 2; side++)
	{
		const double x = c[0] + (side ? -width : width);

		ring[side] = mesh->vertexCount;
		for (i = 0; i < segments; i++)
		{
			double s;
			double co;

			RldMk_SinCos(360.0 * (double)i / (double)segments, &s, &co);
			(void)RldMk_TestVertex(mesh, x, upZ ? (c[1] - radius * s) : (c[1] + radius * co), upZ ? (c[2] + radius * co) : (c[2] + radius * s),
			                       (u32)side * 64u + (i % 8u));
		}
	}
	for (side = 0; side < 2; side++)
	{
		hub[side] = RldMk_TestVertex(mesh, c[0] + (side ? -width : width), c[1], c[2], 200u + (u32)side);
	}

	for (i = 0; i < segments; i++)
	{
		const u32 j = (i + 1u) % segments;

		RldMk_TestFace(mesh, 4u, ring[0] + i, ring[1] + i, ring[1] + j, ring[0] + j, 0u);
		RldMk_TestFace(mesh, 3u, hub[0], ring[0] + i, ring[0] + j, 0u, 0u);
		RldMk_TestFace(mesh, 3u, hub[1], ring[1] + j, ring[1] + i, 0u, 0u);
	}
}

// Build a test wheel from PLY text; 1 = built. res is freed by the caller.
static int RldWheel_TestBuild(const struct RldMkTestMesh *mesh, const struct RldWheelOptions *wo, struct RldWheelResult *res)
{
	size_t size = 0;
	u8 *data = RldMk_TestPly(mesh, 0, 1, 0, &size);
	int ok;

	memset(res, 0, sizeof(*res));
	RldMk_ResetSaid();
	if (data == NULL)
	{
		return 0;
	}
	ok = RldWheel_Build("selftest/wheel.ply", data, size, wo, res);
	free(data);
	return ok;
}

// The same mesh as an OBJ "selftest/wheel.obj" with the MTL file
// "selftest/wheel.mtl": material "tire" (Kd 1 1 1, map_Kd wheel.png) on
// every face, the second half "rim" (Kd only) with RLDWHEEL_TEST_TWO; the UV
// of a point is its PLY y and z plus one half, none on the first face with
// RLDWHEEL_TEST_NO_VT. The texture is a PNG of tw x th pixels, red at the top
// left, green elsewhere; cut to its first half with RLDWHEEL_TEST_TEX_CUT,
// not there with RLDWHEEL_TEST_TEX_GONE.
#define RLDWHEEL_TEST_TWO 1
#define RLDWHEEL_TEST_NO_VT 2
#define RLDWHEEL_TEST_TEX_CUT 4
#define RLDWHEEL_TEST_TEX_GONE 8
static int RldWheel_TestBuildObj(const struct RldMkTestMesh *mesh, u32 tw, u32 th, int flags, struct RldWheelResult *res)
{
	static const char mtl[] = "newmtl tire\nKd 1 1 1\nmap_Kd wheel.png\nnewmtl rim\nKd 0.5 0.5 0.5\n";
	struct RldObjMemFile files[2];
	struct RldMkBuffer b;
	struct RldWheelOptions wo;
	size_t pngSize = 0;
	u8 *rgb = (u8 *)calloc((size_t)tw * th * 3u + 3u, 1u);
	u8 *png;
	u32 i;
	u32 k;
	int ok = 0;

	memset(res, 0, sizeof(*res));
	memset(&b, 0, sizeof(b));
	memset(&wo, 0, sizeof(wo));
	RldMk_ResetSaid();
	RldObj_ResetOptions();
	if (rgb == NULL)
	{
		return 0;
	}
	for (i = 0; i < tw * th; i++)
	{
		rgb[3u * i + 1u] = 255u;
	}
	rgb[0] = 255u;
	rgb[1] = 0u;
	png = RldMk_TestPngRgb(tw, th, rgb, &pngSize);
	free(rgb);

	RldMk_BufferText(&b, "mtllib wheel.mtl\n");
	for (i = 0; i < mesh->vertexCount; i++)
	{
		RldMk_BufferText(&b, "v %.6f %.6f %.6f\nvt %.6f %.6f\n", mesh->xyz[i][0], mesh->xyz[i][1], mesh->xyz[i][2], mesh->xyz[i][1] + 0.5,
		                 mesh->xyz[i][2] + 0.5);
	}
	for (i = 0; i < mesh->faceCount; i++)
	{
		if (i == 0u)
		{
			RldMk_BufferText(&b, "usemtl tire\n");
		}
		else if ((flags & RLDWHEEL_TEST_TWO) && (i == mesh->faceCount / 2u))
		{
			RldMk_BufferText(&b, "usemtl rim\n");
		}
		RldMk_BufferText(&b, "f");
		for (k = 0; k < mesh->cornerCount[i]; k++)
		{
			if ((flags & RLDWHEEL_TEST_NO_VT) && (i == 0u))
			{
				RldMk_BufferText(&b, " %u", mesh->corners[i][k] + 1u);
			}
			else
			{
				RldMk_BufferText(&b, " %u/%u", mesh->corners[i][k] + 1u, mesh->corners[i][k] + 1u);
			}
		}
		RldMk_BufferText(&b, "\n");
	}

	files[0].path = "selftest/wheel.mtl";
	files[0].data = (const u8 *)mtl;
	files[0].size = sizeof(mtl) - 1u;
	files[1].path = "selftest/wheel.png";
	files[1].data = png;
	files[1].size = (flags & RLDWHEEL_TEST_TEX_CUT) ? (pngSize / 2u) : pngSize;
	s_rldObjFiles = files;
	s_rldObjFileCount = (flags & RLDWHEEL_TEST_TEX_GONE) ? 1u : 2u;
	if ((png != NULL) && (b.data != NULL))
	{
		ok = RldWheel_Build("selftest/wheel.obj", b.data, b.size, &wo, res);
	}
	s_rldObjFiles = NULL;
	s_rldObjFileCount = 0;
	free(png);
	free(b.data);
	return ok;
}

// Where the triangles start in a RLDPW2 file.
static size_t RldWheel_TestTris(const struct RldWheelResult *res)
{
	return RLDWHEEL_HEAD_BYTES + (size_t)Rld_ReadLE32(&res->preview[8]) * Rld_ReadLE32(&res->preview[12]) * 4u + 4u;
}

// The box of the file's corners (x0 y0 z0 x1 y1 z1, 1/16 game units), and the
// triangles whose normal points toward the centre (inward).
static void RldWheel_TestMeasure(const struct RldWheelResult *res, int box[6], u32 *inward)
{
	const size_t first = RldWheel_TestTris(res);
	const u32 count = Rld_ReadLE32(&res->preview[first - 4u]);
	u32 t;
	int a;

	for (a = 0; a < 3; a++)
	{
		box[a] = 32767;
		box[3 + a] = -32768;
	}
	*inward = 0;
	for (t = 0; t < count; t++)
	{
		double v[3][3];
		double n[3];
		double mid[3];
		int k;

		for (k = 0; k < 3; k++)
		{
			for (a = 0; a < 3; a++)
			{
				const int unit = (int)(s16)Rld_ReadLE16(&res->preview[first + (3u * t + (u32)k) * RLDWHEEL_CORNER_BYTES + 2u * (u32)a]);

				v[k][a] = (double)unit;
				box[a] = (unit < box[a]) ? unit : box[a];
				box[3 + a] = (unit > box[3 + a]) ? unit : box[3 + a];
			}
		}
		RldMk_Cross(v[0], v[1], v[2], n);
		for (a = 0; a < 3; a++)
		{
			mid[a] = (v[0][a] + v[1][a] + v[2][a]) / 3.0;
		}
		if (RldMk_Dot(n, mid) < 0.0)
		{
			(*inward)++;
		}
	}
}

static int RldWheel_SelfTest(void)
{
	struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	static const double origin[3] = {0.0, 0.0, 0.0};
	static const double away[3] = {1.0, 2.0, -3.0};
	const int quietBefore = s_rldMkQuiet;
	struct RldWheelOptions wo;
	struct RldWheelResult res;
	struct RldWheelResult again;
	int box[6] = {0, 0, 0, 0, 0, 0};
	u32 inward = 0;
	int ok;

	s_rldWheelTestFailed = 0;
	if (mesh == NULL)
	{
		printf("  FAIL char-wheel: out of memory\n");
		return 1;
	}
	s_rldMkQuiet = 1;
	memset(&wo, 0, sizeof(wo));

	// 1. The game's size: radius 0.25 = 16 game units (256 in the file), 10 wide;
	// 16 segments = 64 triangles, 34 points, no texture.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	if (ok)
	{
		RldWheel_TestMeasure(&res, box, &inward);
	}
	RldWheel_Test(ok && (res.triangles == 64u) && (res.points == 34u) && (res.factor > 0.9999) && (res.factor < 1.0001) && (box[1] == -256) &&
	                  (box[4] == 256) &&
	                  (box[2] == -256) && (box[5] == 256) && (box[0] == -80) && (box[3] == 80) && (inward == 0u) &&
	                  (memcmp(res.preview, "RLDPW2\0\0", 8u) == 0) && (Rld_ReadLE32(&res.preview[8]) == 0u) &&
	                  (res.previewSize == RLDWHEEL_HEAD_BYTES + 4u + 64u * 3u * RLDWHEEL_CORNER_BYTES),
	              "game size", "built %d, %u triangles, %u points, x%.4g, box %d %d %d %d %d %d, %u inward", ok, res.triangles, res.points, res.factor,
	              box[0], box[1], box[2], box[3], box[4], box[5], inward);
	RldWheel_FreeResult(&res);

	// 2. Four times as large and away from the origin: scaled down, centred.
	RldWheel_TestWheel(mesh, 16u, 1.0, 0.3125, away, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	if (ok)
	{
		RldWheel_TestMeasure(&res, box, &inward);
	}
	RldWheel_Test(ok && (res.factor > 0.2499) && (res.factor < 0.2501) && (box[1] == -256) && (box[4] == 256) && (box[0] == -80) && (box[3] == 80) &&
	                  ((box[2] + box[5]) == 0) &&
	                  (inward == 0u),
	              "fit and centre", "built %d, x%.4g, box %d %d %d %d %d %d, %u inward", ok, res.factor, box[0], box[1], box[2], box[3], box[4], box[5],
	              inward);
	RldWheel_FreeResult(&res);

	// 3. Written with +Z up (--up z): the same wheel.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 1);
	wo.upZ = 1;
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	wo.upZ = 0;
	if (ok)
	{
		RldWheel_TestMeasure(&res, box, &inward);
	}
	RldWheel_Test(ok && (box[0] == -80) && (box[3] == 80) && (box[1] == -256) && (box[4] == 256) && (inward == 0u), "up z",
	              "built %d, box %d %d %d %d %d %d, %u inward", ok, box[0], box[1], box[2], box[3], box[4], box[5], inward);
	RldWheel_FreeResult(&res);

	// 4. 256 segments = 1024 triangles: the limit, taken as it is; 257 = 1028: refused, never reduced.
	RldWheel_TestWheel(mesh, 256u, 0.25, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(ok && (res.triangles == 1024u) && !RldMk_Said("model-reduced"), "1024 triangles", "built %d, %u triangles", ok, res.triangles);
	RldWheel_FreeResult(&res);
	RldWheel_TestWheel(mesh, 257u, 0.25, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-triangles") && (res.preview == NULL), "over 1024 triangles", "built %d, wheel-triangles said %d", ok,
	              RldMk_Said("wheel-triangles"));
	RldWheel_FreeResult(&res);

	// 4b. 700 loose triangles (2100 points): over the points, refused.
	{
		u32 t;

		memset(mesh, 0, sizeof(*mesh));
		for (t = 0; t < 700u; t++)
		{
			double s;
			double co;
			u32 a;
			u32 b;
			u32 c;

			RldMk_SinCos(360.0 * (double)t / 700.0, &s, &co);
			a = RldMk_TestVertex(mesh, 0.05, 0.25 * co, 0.25 * s, t);
			b = RldMk_TestVertex(mesh, -0.05, 0.25 * co, 0.25 * s, t);
			c = RldMk_TestVertex(mesh, 0.0, 0.2 * co, 0.2 * s, t);
			RldMk_TestFace(mesh, 3u, a, b, c, 0u, 0u);
		}
	}
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-vertices") && (res.points == 2100u), "over 2048 vertices", "built %d, %u vertices, wheel-vertices said %d",
	              ok, res.points, RldMk_Said("wheel-vertices"));
	RldWheel_FreeResult(&res);

	// 5. Four times wider than across: refused (the game takes no wheel wider than round).
	RldWheel_TestWheel(mesh, 16u, 0.25, 1.0, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-wide"), "too wide", "built %d, wheel-wide said %d", ok, RldMk_Said("wheel-wide"));
	RldWheel_FreeResult(&res);

	// 5b. The axle along Y (turned by 90 degrees about Z): as wide as across, not round about X.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 0);
	{
		u32 v;

		for (v = 0; v < mesh->vertexCount; v++)
		{
			const double x = mesh->xyz[v][0];

			mesh->xyz[v][0] = -mesh->xyz[v][1];
			mesh->xyz[v][1] = x;
		}
	}
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(RldMk_Said("wheel-off-axis") || RldMk_Said("wheel-wide"), "axle along Y", "built %d, wheel-off-axis said %d", ok,
	              RldMk_Said("wheel-off-axis"));
	RldWheel_FreeResult(&res);

	// 5c. All corners on the axle (radius 0): wheel-flat.
	RldWheel_TestWheel(mesh, 16u, 0.0, 0.078125, origin, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-flat"), "flat", "built %d, wheel-flat said %d", ok, RldMk_Said("wheel-flat"));
	RldWheel_FreeResult(&res);

	// 6. An OBJ with a 16 x 16 texture: textured, UVs v from the top, the
	// texture in the file, white corners (Kd 1 1 1), one material.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, origin, 0);
	ok = RldWheel_TestBuildObj(mesh, 16u, 16u, 0, &res);
	{
		const size_t first = ok ? RldWheel_TestTris(&res) : 0u;
		const int textured = ok && (Rld_ReadLE32(&res.preview[8]) == 16u) && (Rld_ReadLE32(&res.preview[12]) == 16u) &&
		                     (res.preview[RLDWHEEL_HEAD_BYTES] == 255u) && (res.preview[RLDWHEEL_HEAD_BYTES + 1u] == 0u) &&
		                     (res.preview[RLDWHEEL_HEAD_BYTES + 5u] == 255u);
		// corner 0 of triangle 0: the ring's first point (y 0.25, z 0): u = 0.75, v = 1 - 0.5
		const int uv = ok && ((s32)Rld_ReadLE32(&res.preview[first + 6u]) == 49152) && ((s32)Rld_ReadLE32(&res.preview[first + 10u]) == 32768) &&
		               (res.preview[first + 14u] == 255u);

		RldWheel_Test(ok && res.obj && textured && uv && (res.triangles == 64u) && (res.materials == 1u) && !RldMk_Said("wheel-materials"),
		              "OBJ with texture", "built %d, obj %d, textured %d, uv %d, %u triangles, %u materials", ok, res.obj, textured, uv, res.triangles,
		              res.materials);
	}
	RldWheel_FreeResult(&res);

	// 7. A texture 2048 wide: refused (never scaled); 24 x 16: not a power of two.
	ok = RldWheel_TestBuildObj(mesh, 2048u, 16u, 0, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-texture-size"), "texture over 1024", "built %d, wheel-texture-size said %d", ok,
	              RldMk_Said("wheel-texture-size"));
	RldWheel_FreeResult(&res);
	ok = RldWheel_TestBuildObj(mesh, 24u, 16u, 0, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-texture-size"), "texture 24 x 16", "built %d, wheel-texture-size said %d", ok,
	              RldMk_Said("wheel-texture-size"));
	RldWheel_FreeResult(&res);

	// 7b. A texture there but not usable - over 4096 pixels, or cut short -
	// is refused (never the color); only a missing one leaves the color.
	ok = RldWheel_TestBuildObj(mesh, 4097u, 16u, 0, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-texture-file") && !RldMk_Said("wheel-texture") && (res.preview == NULL), "texture over 4096",
	              "built %d, wheel-texture-file said %d, wheel-texture said %d", ok, RldMk_Said("wheel-texture-file"), RldMk_Said("wheel-texture"));
	RldWheel_FreeResult(&res);
	ok = RldWheel_TestBuildObj(mesh, 16u, 16u, RLDWHEEL_TEST_TEX_CUT, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-texture-file") && (res.preview == NULL), "texture cut short", "built %d, wheel-texture-file said %d", ok,
	              RldMk_Said("wheel-texture-file"));
	RldWheel_FreeResult(&res);
	ok = RldWheel_TestBuildObj(mesh, 16u, 16u, RLDWHEEL_TEST_TEX_GONE, &res);
	RldWheel_Test(ok && RldMk_Said("wheel-texture") && !RldMk_Said("wheel-texture-file") && (Rld_ReadLE32(&res.preview[8]) == 0u),
	              "texture missing: the color", "built %d, wheel-texture said %d, wheel-texture-file said %d", ok, RldMk_Said("wheel-texture"),
	              RldMk_Said("wheel-texture-file"));
	RldWheel_FreeResult(&res);

	// 8. Two materials: refused (a wheel has one); a textured face without vt: refused.
	ok = RldWheel_TestBuildObj(mesh, 16u, 16u, RLDWHEEL_TEST_TWO, &res);
	RldWheel_Test(!ok && (res.materials == 2u) && RldMk_Said("wheel-materials"), "two materials", "built %d, %u materials, said %d", ok,
	              res.materials, RldMk_Said("wheel-materials"));
	RldWheel_FreeResult(&res);
	ok = RldWheel_TestBuildObj(mesh, 16u, 16u, RLDWHEEL_TEST_NO_VT, &res);
	RldWheel_Test(!ok && RldMk_Said("wheel-uv"), "textured without vt", "built %d, wheel-uv said %d", ok, RldMk_Said("wheel-uv"));
	RldWheel_FreeResult(&res);

	// 9. Two builds, the same bytes.
	RldWheel_TestWheel(mesh, 16u, 0.25, 0.078125, away, 0);
	ok = RldWheel_TestBuild(mesh, &wo, &res);
	ok = RldWheel_TestBuild(mesh, &wo, &again) && ok;
	RldWheel_Test(ok && (res.previewSize == again.previewSize) && (memcmp(res.preview, again.preview, res.previewSize) == 0), "same bytes",
	              "built %d, %u / %u bytes", ok, (u32)res.previewSize, (u32)again.previewSize);
	RldWheel_FreeResult(&res);
	RldWheel_FreeResult(&again);

	// 10. --out is refused before anything is read.
	{
		char *args[5];
		int code;

		args[0] = (char *)"--model";
		args[1] = (char *)"wheel.ply";
		args[2] = (char *)"--out";
		args[3] = (char *)"wheel.rldchar";
		args[4] = NULL;
		code = RldWheel_Command(4, args);
		RldWheel_Test((code == 2) && RldMk_Said("usage"), "no container", "exit code %d, usage said %d", code, RldMk_Said("usage"));
	}

	s_rldMkQuiet = quietBefore;
	RldMk_ResetSaid();
	free(mesh);
	return s_rldWheelTestFailed;
}
