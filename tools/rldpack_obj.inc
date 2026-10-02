// rldpack_obj.inc - make-char: OBJ models, their MTL materials and textures
//
// Part of the packer, never of the game. tools/rldpack_import.inc includes it;
// it fills the same struct RldMkPly as the PLY reader, so that the chain after
// step 1 never learns which format the model came in.
//
// WHAT IS READ. v (x y z, an optional w, or x y z r g b with vertex colors
// 0..1), vt (u, v, an optional w), vn (counted only), f in all four forms
// (v, v/vt, v//vn, v/vt/vn) with indices from 1 or negative from the end,
// faces of 3 to RLDMK_PLY_LIST_MAX corners, o and g (only listed), usemtl and
// mtllib; a backslash at the end of a line continues it, CR LF and LF, a
// UTF-8 byte order mark. The other keywords of the format (s, l, p, curves and
// surfaces) are read past with one note each. Anything else stops the reader
// with the line it stopped at - it never guesses.
//
// THE COLOR OF A CORNER, as bytes floor(c x 255 + 0.5) like the float colors
// of a PLY (sRGB bytes as they are, no conversion):
//   1. a vertex with a color: that color; Kd is not used (Blender writes
//      Kd 0.8 for every material, the model would be a fifth darker). With a
//      texture: the vertex color times the texture - for a PS1 rip the
//      vertex color x 255/128, at most 1, as the PS1 drew it (0x80 = the
//      texture as it is; RldObj_VertexColors, --vertex-colors). Without the
//      texture file the vertex color stays as it is.
//   2. otherwise the material: Kd times the texture; no Kd but a texture:
//      the texture; neither: grey.
//   3. grey is 0x80 per channel, like a PLY without colors.
// The texture is sampled at the corner's vt (map_Kd -o and -s: offset +
// scale x vt): bilinear, repeated (u and v modulo 1) - with the edges
// clamped on a face whose coordinates all lie in 0..1 or with -clamp on -,
// v = 0 at the bottom of the picture, each texel by its alpha (a corner on
// fully transparent texels keeps its color without the texture). PNG through
// include/rldpng.inc, JPEG and TGA through tools/rldpack_image.c. A texture
// that is not at its path is looked for at a fixed list of other places
// (RldObj_SearchTexture); @model texture names the file used.
//
// THE POINTS. The chain keeps one color per point, so the reader makes one
// point for each different pair (v, final color), in the order the faces first
// use it; a v no face uses is left out. Welding by position (step 3) joins
// the points again where the colors meet.
//
// Every number is read with strtod and every color computed in double in a
// fixed order: the same file gives the same points and bytes.

#define RLDOBJ_LINE_MAX (256u * 1024u)            // one line, continuations joined
#define RLDOBJ_CORNERS_MAX (4u * RLDMK_PLY_MAX)   // the corners of all faces (obj-big)
#define RLDOBJ_NAMES_MAX 65536u                   // usemtl names, materials of the MTL files
#define RLDOBJ_NAME_MAX 255u                      // bytes of one name kept
#define RLDOBJ_GROUPS_LISTED 1024u                // o/g lines listed as @model group
#define RLDOBJ_LIBRARIES 16u                      // mtllib files
#define RLDOBJ_MTL_BYTES (16u * 1024u * 1024u)
#define RLDOBJ_TEXTURE_BYTES (64u * 1024u * 1024u)
#define RLDOBJ_TEXTURES_SAID 16u                  // texture warnings said one by one, the rest counted
#define RLDOBJ_DETECT_BYTES (64u * 1024u)

// JPEG and TGA: tools/rldpack_image.c (stb_image). 0 = decoded into *rgba,
// width x height x 4 bytes with the rows from the top, freed with RldImg_Free.
int RldImg_Decode(const unsigned char *data, size_t size, unsigned int *width, unsigned int *height, unsigned char **rgba, const char **why);
void RldImg_Free(unsigned char *rgba);

//========================================================================================
// THE SWITCHES OF THE OBJ READER
//========================================================================================
//
// make-char hands its arguments to RldObj_Argument first; what it takes is
// the reader's, read by RldObj_Parse. RldObj_ResetOptions gives the defaults
// (make-char calls it before its arguments, the self-test after its cases).
//   --vertex-colors auto|modulate|color   how a vertex color meets a texture
//                                         (RldObj_ApplyTexture); default auto
//   --colors-from vertex|material         the vertex colors or the materials
//                                         (RldObj_UniformColors); default auto
//   --textures <folder>                   where else textures are looked for,
//                                         before the other places
//                                         (RldObj_SearchTexture); up to
//                                         RLDOBJ_TEXTURE_DIRS times

enum
{
	RLDOBJ_VERTEX_AUTO = 0,
	RLDOBJ_VERTEX_MODULATE,
	RLDOBJ_VERTEX_COLOR
};

static const char *const s_rldObjVertexWords[3] = {"auto", "modulate", "color"};

enum
{
	RLDOBJ_FROM_AUTO = 0,
	RLDOBJ_FROM_VERTEX,
	RLDOBJ_FROM_MATERIAL
};

static const char *const s_rldObjFromWords[3] = {"auto", "vertex", "material"};

#define RLDOBJ_TEXTURE_DIRS 8u

struct RldObjOptions
{
	int vertexColors;      // RLDOBJ_VERTEX_*
	int vertexColorsGiven;
	int colorsFrom;        // RLDOBJ_FROM_*
	int colorsFromGiven;
	char textureDirs[RLDOBJ_TEXTURE_DIRS][RLD_PATH_MAX];  // --textures, each with a separator at its end
	u32 textureDirCount;
};

static struct RldObjOptions s_rldObjOptions;

static void RldObj_ResetOptions(void)
{
	memset(&s_rldObjOptions, 0, sizeof(s_rldObjOptions));
}

// A make-char argument with its value (NULL = none follows). 1 = the
// reader's, the value used (a wrong one said as an error, the checks go on);
// 0 = not the reader's.
static int RldObj_Argument(const char *arg, const char *value)
{
	if ((strcmp(arg, "--vertex-colors") == 0) && (value != NULL))
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			if (strcmp(value, s_rldObjVertexWords[k]) == 0)
			{
				s_rldObjOptions.vertexColors = k;
				s_rldObjOptions.vertexColorsGiven = 1;
				return 1;
			}
		}
		RldMk_Say("error", "vertex-colors", value, "The vertex colors must be auto, modulate or color.");
		return 1;
	}
	if ((strcmp(arg, "--colors-from") == 0) && (value != NULL))
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			if (strcmp(value, s_rldObjFromWords[k]) == 0)
			{
				s_rldObjOptions.colorsFrom = k;
				s_rldObjOptions.colorsFromGiven = 1;
				return 1;
			}
		}
		RldMk_Say("error", "colors-from", value, "The colors must come from vertex or material (or auto).");
		return 1;
	}
	if ((strcmp(arg, "--textures") == 0) && (value != NULL))
	{
		const size_t length = strlen(value);
		const int separator = (length != 0u) && Rld_IsSeparator(value[length - 1u]);

		if ((length == 0u) || ((length + 2u) > RLD_PATH_MAX))
		{
			RldMk_Say("error", "textures", value, "The textures folder must be a folder name (at most %u characters).", RLD_PATH_MAX - 2u);
		}
		else if (s_rldObjOptions.textureDirCount >= RLDOBJ_TEXTURE_DIRS)
		{
			RldMk_Say("error", "textures", value, "At most %u textures folders can be given.", RLDOBJ_TEXTURE_DIRS);
		}
		else
		{
			snprintf(s_rldObjOptions.textureDirs[s_rldObjOptions.textureDirCount++], RLD_PATH_MAX, "%s%s", value, separator ? "" : "/");
		}
		return 1;
	}
	return 0;
}

// @value lines of the switches, after make-char's own.
static void RldObj_EmitValues(void)
{
	Rld_Emit("value", "vertex-colors", s_rldObjVertexWords[s_rldObjOptions.vertexColors], s_rldObjOptions.vertexColorsGiven ? "switch" : "default",
	         (const char *)NULL);
	Rld_Emit("value", "colors-from", s_rldObjFromWords[s_rldObjOptions.colorsFrom], s_rldObjOptions.colorsFromGiven ? "switch" : "default",
	         (const char *)NULL);
	if (s_rldObjOptions.textureDirCount == 0u)
	{
		Rld_Emit("value", "textures", "", "default", (const char *)NULL);
	}
	else
	{
		u32 i;

		for (i = 0; i < s_rldObjOptions.textureDirCount; i++)
		{
			Rld_Emit("value", "textures", s_rldObjOptions.textureDirs[i], "switch", (const char *)NULL);
		}
	}
}

// The help of the switches, in make-char's help.
static void RldObj_Usage(FILE *out)
{
	fprintf(out, "  --vertex-colors auto|modulate|color\n");
	fprintf(out, "                              OBJ: how a vertex color meets a texture.\n");
	fprintf(out, "                              modulate: as on the PS1, 0x80 shows the\n");
	fprintf(out, "                              texture as it is (x 255/128, at most 255);\n");
	fprintf(out, "                              color: texture x color/255; auto (default):\n");
	fprintf(out, "                              modulate when the model is a PS1 rip (the\n");
	fprintf(out, "                              colors of its textured corners have their\n");
	fprintf(out, "                              median at 0x70..0x90, at least 5 %% exactly\n");
	fprintf(out, "                              0x7F or 0x80 grey), else color\n");
	fprintf(out, "  --textures <folder>         OBJ: look for textures that are not at their\n");
	fprintf(out, "                              path in this folder first (up to %u times)\n", RLDOBJ_TEXTURE_DIRS);
	fprintf(out, "  --colors-from vertex|material\n");
	fprintf(out, "                              OBJ: the vertex colors or the materials' Kd\n");
	fprintf(out, "                              (times their textures); without it the vertex\n");
	fprintf(out, "                              colors count, unless all of them are exactly\n");
	fprintf(out, "                              white or exactly black while the materials\n");
	fprintf(out, "                              have colors (2 or more Kd, or a texture)\n");
}

// The keywords of the format that are read past (obj-ignored, one note each).
static const char *const s_rldObjIgnored[] = {"s",    "l",     "p",    "vp",  "cstype", "deg",      "bmat",     "step",   "curv",       "curv2",
                                              "surf", "parm",  "trim", "hole", "scrv",  "sp",       "end",      "con",    "mg",         "bevel",
                                              "c_interp", "d_interp", "lod", "usemap", "maplib", "shadow_obj", "trace_obj", "ctech", "stech", "call",
                                              "csh"};
#define RLDOBJ_IGNORED_COUNT (sizeof(s_rldObjIgnored) / sizeof(s_rldObjIgnored[0]))

//========================================================================================
// FILES: the disk, or the self-test's files in memory
//========================================================================================

struct RldObjMemFile
{
	const char *path;
	const u8 *data;
	size_t size;
};

// Set only by the self-test: then MTL files and textures are looked up here,
// never on the disk.
static const struct RldObjMemFile *s_rldObjFiles;
static u32 s_rldObjFileCount;

enum
{
	RLDOBJ_READ_OK = 0,
	RLDOBJ_READ_MISSING,
	RLDOBJ_READ_LARGE,
	RLDOBJ_READ_MEMORY
};

// A whole file, at most `max` bytes, with a 0 after it.
static int RldObj_ReadFile(const char *path, size_t max, u8 **dataOut, size_t *sizeOut)
{
	FILE *file;
	long size;
	u8 *data;

	*dataOut = NULL;
	*sizeOut = 0;

	if (s_rldObjFiles != NULL)
	{
		u32 i;

		for (i = 0; i < s_rldObjFileCount; i++)
		{
			if (strcmp(s_rldObjFiles[i].path, path) == 0)
			{
				if (s_rldObjFiles[i].size > max)
				{
					return RLDOBJ_READ_LARGE;
				}
				data = (u8 *)malloc(s_rldObjFiles[i].size + 1u);
				if (data == NULL)
				{
					return RLDOBJ_READ_MEMORY;
				}
				if (s_rldObjFiles[i].size != 0u)
				{
					memcpy(data, s_rldObjFiles[i].data, s_rldObjFiles[i].size);
				}
				data[s_rldObjFiles[i].size] = 0;
				*dataOut = data;
				*sizeOut = s_rldObjFiles[i].size;
				return RLDOBJ_READ_OK;
			}
		}
		return RLDOBJ_READ_MISSING;
	}

	file = fopen(path, "rb");
	if (file == NULL)
	{
		return RLDOBJ_READ_MISSING;
	}
	if ((fseek(file, 0, SEEK_END) != 0) || ((size = ftell(file)) < 0) || (fseek(file, 0, SEEK_SET) != 0))
	{
		fclose(file);
		return RLDOBJ_READ_MISSING;
	}
	if ((unsigned long)size > (unsigned long)max)
	{
		fclose(file);
		return RLDOBJ_READ_LARGE;
	}

	data = (u8 *)malloc((size_t)size + 1u);
	if (data == NULL)
	{
		fclose(file);
		return RLDOBJ_READ_MEMORY;
	}
	if ((size != 0) && (fread(data, 1, (size_t)size, file) != (size_t)size))
	{
		free(data);
		fclose(file);
		return RLDOBJ_READ_MISSING;
	}
	fclose(file);

	data[size] = 0;
	*dataOut = data;
	*sizeOut = (size_t)size;
	return RLDOBJ_READ_OK;
}

static int RldObj_IsAbsolute(const char *name)
{
	return Rld_IsSeparator(name[0]) || ((name[0] != '\0') && (name[1] == ':'));
}

// The folder of `path` with its separator, or "" next to the current folder.
static void RldObj_Folder(const char *path, char *out, size_t outSize)
{
	size_t n = (size_t)(Rld_LastComponent(path) - path);

	if (n >= outSize)
	{
		n = 0;
	}
	memcpy(out, path, n);
	out[n] = '\0';
}

// `name` relative to `folder`, or as it is when it is absolute. 0 = too long.
static int RldObj_Join(const char *folder, const char *name, char *out, size_t outSize)
{
	const int n = RldObj_IsAbsolute(name) ? snprintf(out, outSize, "%s", name) : snprintf(out, outSize, "%s%s", folder, name);

	return (n >= 0) && ((size_t)n < outSize);
}

// Is the last part written before path[o] (which follows its separator) ".."?
static int RldObj_LastIsUp(const char *path, size_t start, size_t o)
{
	const size_t end = o - 1u;
	size_t begin = end;

	while ((begin > start) && !Rld_IsSeparator(path[begin - 1u]))
	{
		begin--;
	}
	return ((end - begin) == 2u) && (path[begin] == '.') && (path[begin + 1u] == '.');
}

// A path tidied in place: "." parts and doubled separators dropped, a part
// followed by ".." dropped with it; the separators as written, a drive and
// leading separators kept. "./t.png", "a/../t.png" and "t.png" become one.
static void RldObj_TidyPath(char *path)
{
	size_t start = 0;
	size_t r;
	size_t o;

	if ((path[0] != '\0') && (path[1] == ':'))
	{
		start = 2;
	}
	while (Rld_IsSeparator(path[start]))
	{
		start++;
	}
	r = start;
	o = start;
	while (path[r] != '\0')
	{
		size_t end = r;
		size_t n;

		while ((path[end] != '\0') && !Rld_IsSeparator(path[end]))
		{
			end++;
		}
		n = end - r;
		if ((n == 1u) && (path[r] == '.'))
		{
			// "." is dropped.
		}
		else if ((n == 2u) && (path[r] == '.') && (path[r + 1u] == '.') && (o > start) && !RldObj_LastIsUp(path, start, o))
		{
			// ".." drops the part before it: back over its separator and it.
			o--;
			while ((o > start) && !Rld_IsSeparator(path[o - 1u]))
			{
				o--;
			}
		}
		else
		{
			memmove(&path[o], &path[r], n);
			o += n;
			if (path[end] != '\0')
			{
				path[o++] = path[end];
			}
		}
		r = end;
		while (Rld_IsSeparator(path[r]))
		{
			r++;
		}
	}
	path[o] = '\0';
}

// The same file? Paths compared as Windows does: in any letter case, with \
// and / alike.
static int RldObj_SamePath(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a;
		char y = *b;

		x = ((x >= 'A') && (x <= 'Z')) ? (char)(x - 'A' + 'a') : ((x == '\\') ? '/' : x);
		y = ((y >= 'A') && (y <= 'Z')) ? (char)(y - 'A' + 'a') : ((y == '\\') ? '/' : y);
		if (x != y)
		{
			return 0;
		}
		if (x == '\0')
		{
			return 1;
		}
	}
}

//========================================================================================
// LINES, WORDS AND NUMBERS
//========================================================================================

struct RldObjText
{
	const u8 *data;
	size_t size;
	size_t at;
	u32 line;      // the first line of the current one, from 1
	u32 next;
	char *buffer;  // RLDOBJ_LINE_MAX + 1 bytes
	size_t length;
};

static void RldObj_TextStart(struct RldObjText *t, const u8 *data, size_t size, char *buffer)
{
	t->data = data;
	t->size = size;
	t->at = ((size >= 3u) && (data[0] == 0xEFu) && (data[1] == 0xBBu) && (data[2] == 0xBFu)) ? 3u : 0u;
	t->line = 0;
	t->next = 1;
	t->buffer = buffer;
	t->length = 0;
}

// The next line, its continuations joined, without its line end. 1 = a line,
// 0 = the end of the file, -1 = longer than RLDOBJ_LINE_MAX.
static int RldObj_Line(struct RldObjText *t)
{
	size_t used = 0;

	if (t->at >= t->size)
	{
		return 0;
	}

	t->line = t->next;
	for (;;)
	{
		const u8 *start = &t->data[t->at];
		const u8 *end = (const u8 *)memchr(start, '\n', t->size - t->at);
		const size_t n = (end != NULL) ? (size_t)(end - start) : (t->size - t->at);

		if (n > (RLDOBJ_LINE_MAX - used))
		{
			return -1;
		}
		memcpy(&t->buffer[used], start, n);
		used += n;
		t->at += n + ((end != NULL) ? 1u : 0u);
		t->next++;

		while ((used > 0u) && (t->buffer[used - 1u] == '\r'))
		{
			used--;
		}
		if ((used > 0u) && (t->buffer[used - 1u] == '\\') && (t->at < t->size))
		{
			t->buffer[used - 1u] = ' ';
			continue;
		}
		break;
	}

	t->buffer[used] = '\0';
	t->length = used;
	return 1;
}

struct RldObjWord
{
	const char *text;
	size_t length;
};

static int RldObj_Space(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\v') || (c == '\f');
}

// The next word of a line from *at. 0 = none left.
static int RldObj_Word(const char *line, size_t length, size_t *at, struct RldObjWord *word)
{
	size_t i = *at;
	size_t start;

	while ((i < length) && RldObj_Space(line[i]))
	{
		i++;
	}
	if (i >= length)
	{
		*at = i;
		return 0;
	}

	start = i;
	while ((i < length) && !RldObj_Space(line[i]))
	{
		i++;
	}
	word->text = &line[start];
	word->length = i - start;
	*at = i;
	return 1;
}

static int RldObj_Is(const struct RldObjWord *word, const char *keyword)
{
	const size_t n = strlen(keyword);

	return (word->length == n) && (memcmp(word->text, keyword, n) == 0);
}

// The same in any letter case: the keywords of an MTL file (some exporters
// write map_kd or kd).
static int RldObj_IsNoCase(const struct RldObjWord *word, const char *keyword)
{
	const size_t n = strlen(keyword);
	size_t i;

	if (word->length != n)
	{
		return 0;
	}
	for (i = 0; i < n; i++)
	{
		char a = word->text[i];
		char b = keyword[i];

		a = ((a >= 'A') && (a <= 'Z')) ? (char)(a - 'A' + 'a') : a;
		b = ((b >= 'A') && (b <= 'Z')) ? (char)(b - 'A' + 'a') : b;
		if (a != b)
		{
			return 0;
		}
	}
	return 1;
}

// The rest of a line after *at, without the spaces around it.
static void RldObj_Rest(const char *line, size_t length, size_t at, struct RldObjWord *rest)
{
	while ((at < length) && RldObj_Space(line[at]))
	{
		at++;
	}
	while ((length > at) && RldObj_Space(line[length - 1u]))
	{
		length--;
	}
	rest->text = &line[at];
	rest->length = length - at;
}

// NaN fails both comparisons; the infinities and overflows fail one.
static int RldObj_Finite(double value)
{
	return (value > -1.0e300) && (value < 1.0e300);
}

// 1 = a finite number, 0 = not a number, -1 = not finite.
static int RldObj_Number(const struct RldObjWord *word, double *out)
{
	char token[64];
	char *end = NULL;

	*out = 0.0;
	if ((word->length == 0u) || (word->length >= sizeof(token)))
	{
		return 0;
	}
	memcpy(token, word->text, word->length);
	token[word->length] = '\0';

	*out = strtod(token, &end);
	if ((end == token) || (*end != '\0'))
	{
		return 0;
	}
	return RldObj_Finite(*out) ? 1 : -1;
}

// A corner index from 1, or negative from the end of the `count` read so far.
// 1 = *out from 0, 0 = not a whole number, -1 = 0 or outside.
static int RldObj_Index(const char *text, size_t length, u32 count, u32 *out)
{
	u64 value = 0;
	int negative = 0;
	size_t i = 0;

	if ((length > 0u) && ((text[0] == '-') || (text[0] == '+')))
	{
		negative = (text[0] == '-');
		i = 1;
	}
	if (i >= length)
	{
		return 0;
	}

	for (; i < length; i++)
	{
		if ((text[i] < '0') || (text[i] > '9'))
		{
			return 0;
		}
		// Saturates above 2^32: such an index is outside anyway.
		if (value < 0x100000000ull)
		{
			value = value * 10u + (u64)(text[i] - '0');
		}
	}

	if ((value == 0u) || (value > (u64)count))
	{
		return -1;
	}
	*out = negative ? (u32)((u64)count - value) : (u32)(value - 1u);
	return 1;
}

//========================================================================================
// THE MODEL AS READ
//========================================================================================

struct RldObjNames
{
	char **text;
	u32 count;
	u32 capacity;
	u32 *slots;     // open addressing, RLDMK_NONE = empty
	u32 slotCount;  // a power of two, at least twice count
};

struct RldObjGroup
{
	char *name;
	u32 faces;
};

struct RldObjMaterial
{
	int hasKd;
	double kd[3];
	double ke[3];    // Ke, the light of its own (0 without it)
	int transparent; // d below 1, Tr above 0 or a map_d
	char *map;       // map_Kd as written, its options removed; NULL = none
	double offset[2]; // map_Kd -o u v: the texture coordinates are offset + scale x vt
	double scale[2];  // map_Kd -s u v (1 1 without it)
	int clampOn;     // map_Kd -clamp on: vt clamped into 0..1, the edges clamped
	u32 library;     // the MTL file it came from
	u32 texture;     // into RldObjModel.textures, RLDMK_NONE = none
};

struct RldObjTexture
{
	char path[RLD_PATH_MAX];
	int state;       // RLDMK_TEXTURE_*
	u32 width;
	u32 height;
	u8 *rgba;
	u32 library;     // the MTL file of the first material that uses it
	char sha[65];    // SHA-256 of the file read, hex; "" = none read
};

struct RldObjModel
{
	double *xyz;       // 3 per v
	double *vrgb;      // 3 per v, 0..1
	u8 *vColored;      // per v: has a color
	u32 vCount;
	u32 vCapacity;
	double *uv;        // 2 per vt
	u32 vtCount;
	u32 vtCapacity;
	u32 vnCount;
	u32 *cornerV;
	u32 *cornerT;      // RLDMK_NONE = no vt
	u32 cornerCount;
	u32 cornerCapacity;
	u32 *faceFirst;
	u32 *faceCorners;
	u32 *faceName;     // into names, RLDMK_NONE = no usemtl
	u32 faceCount;
	u32 faceCapacity;
	struct RldObjNames names;          // usemtl, in the order of first use
	u32 *nameMaterial;                 // per name: the material, RLDMK_NONE = not defined
	struct RldObjGroup *groups;
	u32 groupCount;
	u32 groupCapacity;
	u32 groupsMore;                    // o/g lines past RLDOBJ_GROUPS_LISTED
	u32 group;                         // the current one, RLDMK_NONE = none yet
	char *libraries[RLDOBJ_LIBRARIES]; // mtllib as written
	u32 libraryCount;
	struct RldObjNames materialNames;  // newmtl of all MTL files
	struct RldObjMaterial *materials;  // per materialNames entry
	u32 materialCapacity;
	struct RldObjTexture *textures;
	u32 textureCount;
	u32 colored;                       // v lines with a color
	u32 clamped;                       // color values clamped into 0..1
	u32 ignored;                       // bit per s_rldObjIgnored entry already said
	int modulate;                      // vertex colors modulate textures (RldObj_VertexColors)
	int uniform;                       // the vertex colors are not used (RldObj_UniformColors)
	u32 modulated;                     // corners where they did
	char *line;                        // RLDOBJ_LINE_MAX + 1
};

static void RldObj_FreeNames(struct RldObjNames *names)
{
	u32 i;

	for (i = 0; i < names->count; i++)
	{
		free(names->text[i]);
	}
	free(names->text);
	free(names->slots);
	memset(names, 0, sizeof(*names));
}

static void RldObj_FreeModel(struct RldObjModel *m)
{
	u32 i;

	free(m->xyz);
	free(m->vrgb);
	free(m->vColored);
	free(m->uv);
	free(m->cornerV);
	free(m->cornerT);
	free(m->faceFirst);
	free(m->faceCorners);
	free(m->faceName);
	free(m->nameMaterial);
	RldObj_FreeNames(&m->names);
	for (i = 0; i < m->groupCount; i++)
	{
		free(m->groups[i].name);
	}
	free(m->groups);
	for (i = 0; i < m->libraryCount; i++)
	{
		free(m->libraries[i]);
	}
	for (i = 0; i < m->materialNames.count; i++)
	{
		free(m->materials[i].map);
	}
	RldObj_FreeNames(&m->materialNames);
	free(m->materials);
	for (i = 0; i < m->textureCount; i++)
	{
		free(m->textures[i].rgba);
	}
	free(m->textures);
	free(m->line);
	memset(m, 0, sizeof(*m));
}

// Room for one more item in an array of `size`-byte items. 0 = out of memory.
static int RldObj_Grow(void **items, u32 count, u32 *capacity, size_t size)
{
	u32 grown;
	void *bigger;

	if (count < *capacity)
	{
		return 1;
	}

	grown = (*capacity != 0u) ? (*capacity * 2u) : 1024u;
	if ((grown <= *capacity) || ((size_t)grown > (((size_t)-1) / size)))
	{
		return 0;
	}
	bigger = realloc(*items, (size_t)grown * size);
	if (bigger == NULL)
	{
		return 0;
	}
	*items = bigger;
	*capacity = grown;
	return 1;
}

// A copy of at most `max` bytes, with a 0 after them.
static char *RldObj_Copy(const char *text, size_t length, size_t max)
{
	char *copy;

	if (length > max)
	{
		length = max;
	}
	copy = (char *)malloc(length + 1u);
	if (copy != NULL)
	{
		memcpy(copy, text, length);
		copy[length] = '\0';
	}
	return copy;
}

static u32 RldObj_Hash(const char *text, size_t length)
{
	u32 hash = 2166136261u;
	size_t i;

	for (i = 0; i < length; i++)
	{
		hash = (hash ^ (u8)text[i]) * 16777619u;
	}
	return hash;
}

// Finds a name, or adds it with add. 1 = *index, 0 = not there (without add),
// -1 = out of memory or too many names.
static int RldObj_Name(struct RldObjNames *names, const char *text, size_t length, int add, u32 *index)
{
	u32 mask;
	u32 slot;

	if (length > RLDOBJ_NAME_MAX)
	{
		length = RLDOBJ_NAME_MAX;
	}

	if (names->slotCount != 0u)
	{
		mask = names->slotCount - 1u;
		for (slot = RldObj_Hash(text, length) & mask; names->slots[slot] != RLDMK_NONE; slot = (slot + 1u) & mask)
		{
			const char *known = names->text[names->slots[slot]];

			if ((strlen(known) == length) && (memcmp(known, text, length) == 0))
			{
				*index = names->slots[slot];
				return 1;
			}
		}
	}

	if (!add)
	{
		return 0;
	}
	if (names->count >= RLDOBJ_NAMES_MAX)
	{
		return -1;
	}

	// The table at most half full.
	if ((2u * (names->count + 1u)) > names->slotCount)
	{
		const u32 slotCount = (names->slotCount != 0u) ? (2u * names->slotCount) : 256u;
		u32 *slots = (u32 *)malloc((size_t)slotCount * sizeof(u32));
		u32 i;

		if (slots == NULL)
		{
			return -1;
		}
		memset(slots, 0xFF, (size_t)slotCount * sizeof(u32));
		for (i = 0; i < names->count; i++)
		{
			for (slot = RldObj_Hash(names->text[i], strlen(names->text[i])) & (slotCount - 1u); slots[slot] != RLDMK_NONE;
			     slot = (slot + 1u) & (slotCount - 1u))
			{
			}
			slots[slot] = i;
		}
		free(names->slots);
		names->slots = slots;
		names->slotCount = slotCount;
	}

	if (!RldObj_Grow((void **)&names->text, names->count, &names->capacity, sizeof(char *)))
	{
		return -1;
	}
	names->text[names->count] = RldObj_Copy(text, length, RLDOBJ_NAME_MAX);
	if (names->text[names->count] == NULL)
	{
		return -1;
	}

	mask = names->slotCount - 1u;
	for (slot = RldObj_Hash(text, length) & mask; names->slots[slot] != RLDMK_NONE; slot = (slot + 1u) & mask)
	{
	}
	names->slots[slot] = names->count;
	*index = names->count++;
	return 1;
}

//========================================================================================
// MESSAGES OF THE READER
//========================================================================================

static void RldObj_Memory(void)
{
	RldMk_Say("error", "memory", NULL, "rldpack ran out of memory while reading the model.");
}

static void RldObj_Syntax(u32 line, const char *technical)
{
	RldMk_Say("error", "obj-syntax", technical, "Line %u of the OBJ file cannot be read. Export the model again from Blender (File > Export > Wavefront OBJ).",
	          line);
}

static void RldObj_NumberError(u32 line, const char *technical)
{
	RldMk_Say("error", "obj-number", technical, "Line %u of the OBJ file has a value that is not a usable number.", line);
}

static void RldObj_Big(const char *what, u32 limit)
{
	const int mesh = (strcmp(what, "material names") != 0);

	RldMk_Say("error", "obj-big", what, "The model has more than %u %s; rldpack reads at most that many.%s", limit, what,
	          mesh ? " Reduce it in Blender (Decimate modifier)." : "");
}

// "<what> \"<word>\"" as the technical line, at most 40 bytes of the word,
// control bytes shown as '?'.
static void RldObj_Technical(char *out, size_t size, const char *what, const struct RldObjWord *word)
{
	char shown[48];
	size_t i;

	for (i = 0; (i < word->length) && (i < 40u); i++)
	{
		shown[i] = ((u8)word->text[i] < 0x20u) ? '?' : word->text[i];
	}
	shown[i] = '\0';
	snprintf(out, size, "%s \"%s\"%s", what, shown, (word->length > 40u) ? "..." : "");
}

//========================================================================================
// STEP 1 FOR OBJ: THE LINES
//========================================================================================

// v x y z [w] | v x y z r g b. 0 = said.
static int RldObj_Vertex(struct RldObjModel *m, u32 line, const char *text, size_t length, size_t at)
{
	struct RldObjWord word;
	double value[7];
	char technical[96];
	int n = 0;
	int k;

	while (RldObj_Word(text, length, &at, &word))
	{
		int got;

		if (n >= 7)
		{
			n++;
			break;
		}
		got = RldObj_Number(&word, &value[n]);
		if (got == 0)
		{
			RldObj_Technical(technical, sizeof(technical), "v: not a number", &word);
			RldObj_Syntax(line, technical);
			return 0;
		}
		if (got < 0)
		{
			RldObj_Technical(technical, sizeof(technical), "v: not finite", &word);
			RldObj_NumberError(line, technical);
			return 0;
		}
		n++;
	}

	if ((n != 3) && (n != 4) && (n != 6))
	{
		snprintf(technical, sizeof(technical), "v with %d values (3, 4 or 6 are read)", n);
		RldObj_Syntax(line, technical);
		return 0;
	}
	for (k = 0; k < 3; k++)
	{
		if (!(value[k] > -1.0e6) || !(value[k] < 1.0e6))
		{
			RldObj_NumberError(line, "a position outside -1000000..1000000");
			return 0;
		}
	}

	if (m->vCount >= RLDMK_PLY_MAX)
	{
		RldObj_Big("vertices", RLDMK_PLY_MAX);
		return 0;
	}
	if (m->vCount >= m->vCapacity)
	{
		u32 capacity = m->vCapacity;
		u32 c2 = m->vCapacity;
		u32 c3 = m->vCapacity;

		if (!RldObj_Grow((void **)&m->xyz, m->vCount, &capacity, 3u * sizeof(double)) ||
		    !RldObj_Grow((void **)&m->vrgb, m->vCount, &c2, 3u * sizeof(double)) || !RldObj_Grow((void **)&m->vColored, m->vCount, &c3, 1u))
		{
			RldObj_Memory();
			return 0;
		}
		m->vCapacity = capacity;
	}

	m->xyz[3u * m->vCount + 0u] = value[0];
	m->xyz[3u * m->vCount + 1u] = value[1];
	m->xyz[3u * m->vCount + 2u] = value[2];
	m->vColored[m->vCount] = (n == 6) ? 1u : 0u;
	for (k = 0; k < 3; k++)
	{
		double c = (n == 6) ? value[3 + k] : 0.0;

		if ((c < 0.0) || (c > 1.0))
		{
			c = (c < 0.0) ? 0.0 : 1.0;
			m->clamped++;
		}
		m->vrgb[3u * m->vCount + (u32)k] = c;
	}
	m->colored += (n == 6) ? 1u : 0u;
	m->vCount++;
	return 1;
}

// vt u [v [w]] and vn x y z. 0 = said.
static int RldObj_Coordinates(struct RldObjModel *m, u32 line, const char *text, size_t length, size_t at, int normal)
{
	struct RldObjWord word;
	double value[4] = {0.0, 0.0, 0.0, 0.0};
	char technical[96];
	int n = 0;

	while (RldObj_Word(text, length, &at, &word))
	{
		int got;

		if (n >= 4)
		{
			n++;
			break;
		}
		got = RldObj_Number(&word, &value[n]);
		if (got <= 0)
		{
			RldObj_Technical(technical, sizeof(technical), normal ? "vn: not a number" : "vt: not a number", &word);
			if (got == 0)
			{
				RldObj_Syntax(line, technical);
			}
			else
			{
				RldObj_NumberError(line, technical);
			}
			return 0;
		}
		n++;
	}

	if (normal ? (n != 3) : ((n < 1) || (n > 3)))
	{
		snprintf(technical, sizeof(technical), "%s with %d values", normal ? "vn" : "vt", n);
		RldObj_Syntax(line, technical);
		return 0;
	}

	if (normal)
	{
		if (m->vnCount >= RLDMK_PLY_MAX)
		{
			RldObj_Big("normals", RLDMK_PLY_MAX);
			return 0;
		}
		m->vnCount++;
		return 1;
	}

	if (m->vtCount >= RLDMK_PLY_MAX)
	{
		RldObj_Big("texture coordinates", RLDMK_PLY_MAX);
		return 0;
	}
	if (!RldObj_Grow((void **)&m->uv, m->vtCount, &m->vtCapacity, 2u * sizeof(double)))
	{
		RldObj_Memory();
		return 0;
	}
	m->uv[2u * m->vtCount + 0u] = value[0];
	m->uv[2u * m->vtCount + 1u] = value[1];
	m->vtCount++;
	return 1;
}

// f v | v/vt | v//vn | v/vt/vn ... 0 = said.
static int RldObj_Face(struct RldObjModel *m, u32 line, const char *text, size_t length, size_t at, u32 name)
{
	struct RldObjWord word;
	char technical[96];
	const u32 first = m->cornerCount;
	u32 count = 0;

	if (m->faceCount >= RLDMK_PLY_MAX)
	{
		RldObj_Big("faces", RLDMK_PLY_MAX);
		return 0;
	}

	while (RldObj_Word(text, length, &at, &word))
	{
		size_t cut[2];
		int cuts = 0;
		size_t i;
		u32 v = 0;
		u32 t = RLDMK_NONE;
		u32 n = 0;
		int got;

		if (count >= RLDMK_PLY_LIST_MAX)
		{
			snprintf(technical, sizeof(technical), "more than %u corners", RLDMK_PLY_LIST_MAX);
			RldMk_Say("error", "obj-face", technical, "Line %u of the OBJ file has a face of more than %u corners. Triangulate it in Blender.", line,
			          RLDMK_PLY_LIST_MAX);
			m->cornerCount = first;
			return 0;
		}

		for (i = 0; i < word.length; i++)
		{
			if (word.text[i] == '/')
			{
				if (cuts >= 2)
				{
					cuts = 3;
					break;
				}
				cut[cuts++] = i;
			}
		}

		// v, v/vt, v//vn, v/vt/vn - the v always, vt only between the
		// slashes, vn only after the second.
		got = (cuts <= 2) ? RldObj_Index(word.text, (cuts > 0) ? cut[0] : word.length, m->vCount, &v) : 0;
		if ((got == 1) && (cuts >= 1))
		{
			const size_t vtAt = cut[0] + 1u;
			const size_t vtEnd = (cuts == 2) ? cut[1] : word.length;

			if (vtEnd > vtAt)
			{
				got = RldObj_Index(&word.text[vtAt], vtEnd - vtAt, m->vtCount, &t);
			}
			else if (cuts == 1)
			{
				got = 0;
			}
		}
		if ((got == 1) && (cuts == 2))
		{
			got = RldObj_Index(&word.text[cut[1] + 1u], word.length - cut[1] - 1u, m->vnCount, &n);
		}

		if (got == 0)
		{
			RldObj_Technical(technical, sizeof(technical), "f: not a corner", &word);
			RldObj_Syntax(line, technical);
			m->cornerCount = first;
			return 0;
		}
		if (got < 0)
		{
			RldObj_Technical(technical, sizeof(technical), "f: corner", &word);
			RldMk_Say("error", "obj-index", technical,
			          "Line %u of the OBJ file has a face corner that points to no point (%u v, %u vt and %u vn are defined above it).", line,
			          m->vCount, m->vtCount, m->vnCount);
			m->cornerCount = first;
			return 0;
		}

		if (m->cornerCount >= RLDOBJ_CORNERS_MAX)
		{
			RldObj_Big("face corners", RLDOBJ_CORNERS_MAX);
			m->cornerCount = first;
			return 0;
		}
		if (m->cornerCount >= m->cornerCapacity)
		{
			u32 capacity = m->cornerCapacity;
			u32 c2 = m->cornerCapacity;

			if (!RldObj_Grow((void **)&m->cornerV, m->cornerCount, &capacity, sizeof(u32)) ||
			    !RldObj_Grow((void **)&m->cornerT, m->cornerCount, &c2, sizeof(u32)))
			{
				RldObj_Memory();
				m->cornerCount = first;
				return 0;
			}
			m->cornerCapacity = capacity;
		}
		m->cornerV[m->cornerCount] = v;
		m->cornerT[m->cornerCount] = t;
		m->cornerCount++;
		count++;
	}

	if (count < 3u)
	{
		snprintf(technical, sizeof(technical), "a face of %u corners", count);
		RldMk_Say("error", "obj-face", technical, "Line %u of the OBJ file has a face of fewer than 3 corners.", line);
		m->cornerCount = first;
		return 0;
	}

	if (m->faceCount >= m->faceCapacity)
	{
		u32 capacity = m->faceCapacity;
		u32 c2 = m->faceCapacity;
		u32 c3 = m->faceCapacity;

		if (!RldObj_Grow((void **)&m->faceFirst, m->faceCount, &capacity, sizeof(u32)) ||
		    !RldObj_Grow((void **)&m->faceCorners, m->faceCount, &c2, sizeof(u32)) || !RldObj_Grow((void **)&m->faceName, m->faceCount, &c3, sizeof(u32)))
		{
			RldObj_Memory();
			return 0;
		}
		m->faceCapacity = capacity;
	}
	m->faceFirst[m->faceCount] = first;
	m->faceCorners[m->faceCount] = count;
	m->faceName[m->faceCount] = name;
	m->faceCount++;
	if (m->group != RLDMK_NONE)
	{
		m->groups[m->group].faces++;
	}
	return 1;
}

// o and g: a new group in the list. 0 = said.
static int RldObj_Group(struct RldObjModel *m, const struct RldObjWord *name)
{
	if (m->groupCount >= RLDOBJ_GROUPS_LISTED)
	{
		m->groupsMore++;
		m->group = RLDMK_NONE;
		return 1;
	}
	if (!RldObj_Grow((void **)&m->groups, m->groupCount, &m->groupCapacity, sizeof(struct RldObjGroup)))
	{
		RldObj_Memory();
		return 0;
	}
	m->groups[m->groupCount].name = RldObj_Copy(name->text, name->length, RLDOBJ_NAME_MAX);
	m->groups[m->groupCount].faces = 0;
	if (m->groups[m->groupCount].name == NULL)
	{
		RldObj_Memory();
		return 0;
	}
	m->group = m->groupCount++;
	return 1;
}

// All lines of the OBJ file. 0 = said.
static int RldObj_ReadLines(struct RldObjModel *m, const u8 *data, size_t size)
{
	struct RldObjText text;
	u32 name = RLDMK_NONE;
	const u8 *zero = (const u8 *)memchr(data, 0, size);
	int got;

	// An OBJ is text: a 0 byte anywhere (RldObj_Detect looks only at the
	// start) stops the reader at its line, before a name is cut at it.
	if (zero != NULL)
	{
		const u8 *p;
		u32 line = 1;

		for (p = data; p < zero; p++)
		{
			line += (*p == '\n') ? 1u : 0u;
		}
		RldObj_Syntax(line, "a 0 byte: not text");
		return 0;
	}

	RldObj_TextStart(&text, data, size, m->line);
	while ((got = RldObj_Line(&text)) > 0)
	{
		const char *line = text.buffer;
		const size_t length = text.length;
		struct RldObjWord keyword;
		struct RldObjWord rest;
		size_t at = 0;
		u32 k;

		if (!RldObj_Word(line, length, &at, &keyword) || (keyword.text[0] == '#'))
		{
			continue;
		}

		if (RldObj_Is(&keyword, "v"))
		{
			if (!RldObj_Vertex(m, text.line, line, length, at))
			{
				return 0;
			}
			continue;
		}
		if (RldObj_Is(&keyword, "vt") || RldObj_Is(&keyword, "vn"))
		{
			if (!RldObj_Coordinates(m, text.line, line, length, at, keyword.text[1] == 'n'))
			{
				return 0;
			}
			continue;
		}
		if (RldObj_Is(&keyword, "f") || RldObj_Is(&keyword, "fo"))
		{
			if (!RldObj_Face(m, text.line, line, length, at, name))
			{
				return 0;
			}
			continue;
		}

		RldObj_Rest(line, length, at, &rest);
		if (RldObj_Is(&keyword, "o") || RldObj_Is(&keyword, "g"))
		{
			if (!RldObj_Group(m, &rest))
			{
				return 0;
			}
			continue;
		}
		if (RldObj_Is(&keyword, "usemtl"))
		{
			name = RLDMK_NONE;
			if ((rest.length != 0u) && (RldObj_Name(&m->names, rest.text, rest.length, 1, &name) < 0))
			{
				RldObj_Big("material names", RLDOBJ_NAMES_MAX);
				return 0;
			}
			continue;
		}
		if (RldObj_Is(&keyword, "mtllib"))
		{
			// A name in quotes, as for map_Kd.
			if ((rest.length >= 2u) && (rest.text[0] == '"') && (rest.text[rest.length - 1u] == '"'))
			{
				rest.text++;
				rest.length -= 2u;
			}
			if ((rest.length != 0u) && (m->libraryCount < RLDOBJ_LIBRARIES))
			{
				m->libraries[m->libraryCount] = RldObj_Copy(rest.text, rest.length, RLD_PATH_MAX - 1u);
				if (m->libraries[m->libraryCount] == NULL)
				{
					RldObj_Memory();
					return 0;
				}
				m->libraryCount++;
			}
			continue;
		}

		for (k = 0; k < (u32)RLDOBJ_IGNORED_COUNT; k++)
		{
			if (RldObj_Is(&keyword, s_rldObjIgnored[k]))
			{
				break;
			}
		}
		if (k < (u32)RLDOBJ_IGNORED_COUNT)
		{
			if ((m->ignored & (1u << k)) == 0u)
			{
				m->ignored |= 1u << k;
				RldMk_Say("info", "obj-ignored", s_rldObjIgnored[k], "The OBJ lines \"%s\" are not used (first at line %u).", s_rldObjIgnored[k], text.line);
			}
			continue;
		}

		{
			char technical[96];

			RldObj_Technical(technical, sizeof(technical), "not an OBJ keyword:", &keyword);
			RldObj_Syntax(text.line, technical);
		}
		return 0;
	}

	if (got < 0)
	{
		char technical[64];

		snprintf(technical, sizeof(technical), "a line longer than %u bytes", RLDOBJ_LINE_MAX);
		RldObj_Syntax(text.line, technical);
		return 0;
	}

	if (m->faceCount == 0u)
	{
		RldMk_Say("error", "obj-empty", NULL, "The OBJ file has no faces. Export the mesh, not only its points.");
		return 0;
	}
	return 1;
}

// Is this the start of an OBJ file? Text without a 0 byte in its first
// RLDOBJ_DETECT_BYTES; there the first statement and most statements are OBJ
// keywords; and somewhere a v with three numbers. A single broken line is
// then the reader's to name (obj-syntax with its line), not a reason to take
// the file for something else.
static int RldObj_Detect(const u8 *data, size_t size)
{
	const size_t window = (size < RLDOBJ_DETECT_BYTES) ? size : RLDOBJ_DETECT_BYTES;
	size_t at = ((size >= 3u) && (data[0] == 0xEFu) && (data[1] == 0xBBu) && (data[2] == 0xBFu)) ? 3u : 0u;
	u32 known = 0;
	u32 unknown = 0;
	int vertices = 0;
	int continued = 0;

	if (memchr(data, 0, window) != NULL)
	{
		return 0;
	}

	while ((at < size) && (!vertices || (at < window)))
	{
		struct RldObjWord keyword;
		const u8 *end = (const u8 *)memchr(&data[at], '\n', size - at);
		const size_t lineEnd = (end != NULL) ? (size_t)(end - data) : size;
		const char *line = (const char *)&data[at];
		const int continues = continued;
		const int inWindow = (at < window);
		size_t last = lineEnd;
		size_t word = 0;
		u32 k;
		int isKeyword;

		// A line that ends with a backslash goes on in the next one.
		while ((last > at) && (data[last - 1u] == '\r'))
		{
			last--;
		}
		continued = (last > at) && (data[last - 1u] == '\\');

		if (!continues && RldObj_Word(line, lineEnd - at, &word, &keyword) && (keyword.text[0] != '#'))
		{
			isKeyword = RldObj_Is(&keyword, "v") || RldObj_Is(&keyword, "vt") || RldObj_Is(&keyword, "vn") || RldObj_Is(&keyword, "f") ||
			            RldObj_Is(&keyword, "fo") || RldObj_Is(&keyword, "o") || RldObj_Is(&keyword, "g") || RldObj_Is(&keyword, "usemtl") ||
			            RldObj_Is(&keyword, "mtllib");
			for (k = 0; !isKeyword && (k < (u32)RLDOBJ_IGNORED_COUNT); k++)
			{
				isKeyword = RldObj_Is(&keyword, s_rldObjIgnored[k]);
			}
			if (inWindow)
			{
				if (!isKeyword && (known == 0u) && (unknown == 0u))
				{
					return 0;
				}
				known += isKeyword ? 1u : 0u;
				unknown += isKeyword ? 0u : 1u;
			}

			if (RldObj_Is(&keyword, "v"))
			{
				struct RldObjWord number;
				double value;
				int numbers = 0;

				while ((numbers < 3) && RldObj_Word(line, lineEnd - at, &word, &number) && (RldObj_Number(&number, &value) != 0))
				{
					numbers++;
				}
				vertices |= (numbers == 3) || continued;
			}
		}
		at = lineEnd + 1u;
	}

	return vertices && (known > unknown);
}

//========================================================================================
// MTL: THE MATERIALS
//========================================================================================

// The values an option of map_Kd takes: -o, -s and -t up to three numbers,
// -mm two, the others one; 0 = not an option of the format.
static u32 RldObj_OptionValues(const struct RldObjWord *option, int *numbers)
{
	static const char *const one[] = {"-blendu", "-blendv", "-bm", "-boost", "-cc", "-clamp", "-imfchan", "-texres", "-type"};
	u32 i;

	*numbers = 1;
	if (RldObj_Is(option, "-o") || RldObj_Is(option, "-s") || RldObj_Is(option, "-t"))
	{
		return 3u;
	}
	if (RldObj_Is(option, "-mm"))
	{
		return 2u;
	}
	*numbers = 0;
	for (i = 0; i < (u32)(sizeof(one) / sizeof(one[0])); i++)
	{
		if (RldObj_Is(option, one[i]))
		{
			return 1u;
		}
	}
	return 0u;
}

// The file name of a map_Kd line: the options before it skipped, the rest of
// the line (spaces allowed), without quotes around it. A word that starts
// with '-' but is no option of the format starts the name (-face.png). -o,
// -s (u and v; w is not used) and -clamp go into *material.
static void RldObj_MapName(const char *line, size_t length, size_t at, struct RldObjWord *name, struct RldObjMaterial *material)
{
	struct RldObjWord word;

	material->offset[0] = 0.0;
	material->offset[1] = 0.0;
	material->scale[0] = 1.0;
	material->scale[1] = 1.0;
	material->clampOn = 0;

	for (;;)
	{
		struct RldObjWord option;
		size_t before = at;
		int numbers;
		u32 values;
		u32 i;

		if (!RldObj_Word(line, length, &at, &option) || (option.text[0] != '-') || (option.length < 2u))
		{
			at = before;
			break;
		}

		values = RldObj_OptionValues(&option, &numbers);
		if (values == 0u)
		{
			at = before;
			break;
		}
		for (i = 0; i < values; i++)
		{
			double value = 0.0;

			before = at;
			if (!RldObj_Word(line, length, &at, &word) || (numbers && (RldObj_Number(&word, &value) != 1)))
			{
				at = before;
				break;
			}
			if (RldObj_Is(&option, "-o") && (i < 2u))
			{
				material->offset[i] = value;
			}
			else if (RldObj_Is(&option, "-s") && (i < 2u))
			{
				material->scale[i] = value;
			}
			else if (RldObj_Is(&option, "-clamp"))
			{
				material->clampOn = RldObj_IsNoCase(&word, "on");
			}
		}
	}

	RldObj_Rest(line, length, at, name);
	if ((name->length >= 2u) && (name->text[0] == '"') && (name->text[name->length - 1u] == '"'))
	{
		name->text++;
		name->length -= 2u;
	}
}

// One MTL file. 1 = read (at least one newmtl), 0 = not a material file.
static int RldObj_ReadLibrary(struct RldObjModel *m, u32 library, const u8 *data, size_t size, const char *path)
{
	struct RldObjText text;
	u32 current = RLDMK_NONE;
	u32 defined = 0;
	u32 badLine = 0;
	u32 badMap = 0;
	int got;

	if (memchr(data, 0, size) != NULL)
	{
		return 0;
	}

	RldObj_TextStart(&text, data, size, m->line);
	while ((got = RldObj_Line(&text)) > 0)
	{
		struct RldObjWord keyword;
		struct RldObjWord rest;
		size_t at = 0;

		if (!RldObj_Word(text.buffer, text.length, &at, &keyword) || (keyword.text[0] == '#'))
		{
			continue;
		}

		if (RldObj_IsNoCase(&keyword, "newmtl"))
		{
			const u32 before = m->materialNames.count;
			u32 index;

			RldObj_Rest(text.buffer, text.length, at, &rest);
			current = RLDMK_NONE;
			if (rest.length == 0u)
			{
				continue;
			}
			// Room for a new material first: every name has its material.
			if (!RldObj_Grow((void **)&m->materials, m->materialNames.count, &m->materialCapacity, sizeof(struct RldObjMaterial)) ||
			    (RldObj_Name(&m->materialNames, rest.text, rest.length, 1, &index) < 0))
			{
				break;
			}
			defined++;
			// The first definition of a name counts.
			if (m->materialNames.count != before)
			{
				struct RldObjMaterial *material = &m->materials[index];

				memset(material, 0, sizeof(*material));
				material->library = library;
				material->texture = RLDMK_NONE;
				material->scale[0] = 1.0;
				material->scale[1] = 1.0;
				current = index;
			}
			continue;
		}

		if (current == RLDMK_NONE)
		{
			continue;
		}

		if (RldObj_IsNoCase(&keyword, "Kd"))
		{
			struct RldObjMaterial *material = &m->materials[current];
			struct RldObjWord word;
			double value[3] = {0.0, 0.0, 0.0};
			int n = 0;
			int ok = 1;

			while (ok && RldObj_Word(text.buffer, text.length, &at, &word))
			{
				if ((n == 0) && (RldObj_IsNoCase(&word, "spectral") || RldObj_IsNoCase(&word, "xyz")))
				{
					n = -1;
					break;
				}
				ok = (n < 3) && (RldObj_Number(&word, &value[n]) == 1);
				n++;
			}
			if (n < 0)
			{
				continue;
			}
			if (!ok || ((n != 1) && (n != 3)))
			{
				badLine = (badLine != 0u) ? badLine : text.line;
				continue;
			}
			if (n == 1)
			{
				value[1] = value[0];
				value[2] = value[0];
			}
			for (n = 0; n < 3; n++)
			{
				if ((value[n] < 0.0) || (value[n] > 1.0))
				{
					value[n] = (value[n] < 0.0) ? 0.0 : 1.0;
					m->clamped++;
				}
				material->kd[n] = value[n];
			}
			material->hasKd = 1;
			continue;
		}

		if (RldObj_IsNoCase(&keyword, "Ke") || RldObj_IsNoCase(&keyword, "d") || RldObj_IsNoCase(&keyword, "Tr"))
		{
			struct RldObjMaterial *material = &m->materials[current];
			struct RldObjWord word;
			double value[3] = {0.0, 0.0, 0.0};
			int n = 0;

			while ((n < 3) && RldObj_Word(text.buffer, text.length, &at, &word) && (RldObj_Number(&word, &value[n]) == 1))
			{
				n++;
			}
			if (RldObj_IsNoCase(&keyword, "Ke"))
			{
				// Ke r g b, or one value for all three; spectral and xyz are
				// not read.
				for (n = (n == 1) ? 1 : 3; n < 3; n++)
				{
					value[n] = value[0];
				}
				for (n = 0; n < 3; n++)
				{
					material->ke[n] = (value[n] < 0.0) ? 0.0 : ((value[n] > 1.0) ? 1.0 : value[n]);
				}
			}
			else if (n >= 1)
			{
				material->transparent |= ((keyword.text[0] == 'd') || (keyword.text[0] == 'D')) ? (value[0] < 1.0) : (value[0] > 0.0);
			}
			continue;
		}

		if (RldObj_IsNoCase(&keyword, "map_d"))
		{
			m->materials[current].transparent = 1;
			continue;
		}

		if (RldObj_IsNoCase(&keyword, "map_Kd"))
		{
			struct RldObjMaterial *material = &m->materials[current];

			RldObj_MapName(text.buffer, text.length, at, &rest, material);
			free(material->map);
			material->map = NULL;
			if (rest.length != 0u)
			{
				material->map = RldObj_Copy(rest.text, rest.length, RLD_PATH_MAX - 1u);
			}
			else
			{
				badMap = (badMap != 0u) ? badMap : text.line;
			}
			continue;
		}
	}

	if (badLine != 0u)
	{
		RldMk_Say("warning", "mtl-bad", "a Kd line that is not 1 or 3 numbers", "Line %u of the material file %s cannot be read; that color is not used.",
		          badLine, path);
	}
	if (badMap != 0u)
	{
		RldMk_Say("warning", "mtl-bad", "map_Kd without a file name",
		          "Line %u of the material file %s names no texture after map_Kd; that material is drawn without a texture.", badMap, path);
	}

	// A material that lights itself (Ke): the game draws without light, so
	// its color is the brighter of Kd and Ke per channel (Ke without Kd: Ke).
	{
		u32 i;

		for (i = 0; i < m->materialNames.count; i++)
		{
			struct RldObjMaterial *material = &m->materials[i];
			int k;

			if ((material->library != library) || !((material->ke[0] > 0.0) || (material->ke[1] > 0.0) || (material->ke[2] > 0.0)))
			{
				continue;
			}
			for (k = 0; k < 3; k++)
			{
				material->kd[k] = (!material->hasKd || (material->ke[k] > material->kd[k])) ? material->ke[k] : material->kd[k];
			}
			material->hasKd = 1;
		}
	}
	return (got >= 0) && (defined != 0u);
}

static const char *const s_rldObjMtlWords[4] = {"none", "ok", "missing", "bad"};

// mtllib: every named file read; a name that is not found and holds spaces is
// also taken as several file names. The materials of an MTL file that cannot
// be read are not used. Says mtl-missing and mtl-bad; fails only for memory.
static int RldObj_Libraries(struct RldObjModel *m, const char *objPath, char (*libraryPath)[RLD_PATH_MAX], u32 *libraryCount,
                            struct RldMkModelInfo *info)
{
	char folder[RLD_PATH_MAX];
	u32 i;

	RldObj_Folder(objPath, folder, sizeof(folder));
	*libraryCount = 0;
	info->mtl = RLDMK_MTL_NONE;

	if (m->libraryCount == 0u)
	{
		RldMk_EmitModel("mtl", "none", "-", NULL, NULL);
		return 1;
	}

	for (i = 0; i < m->libraryCount; i++)
	{
		const char *written = m->libraries[i];
		const size_t writtenLength = strlen(written);
		size_t at = 0;
		int split = 0;

		for (;;)
		{
			struct RldObjWord name;
			char path[RLD_PATH_MAX];
			char nameText[RLD_PATH_MAX];
			u8 *data = NULL;
			size_t size = 0;
			int read;
			int state;
			u32 k;

			if (!split)
			{
				name.text = written;
				name.length = writtenLength;
			}
			else if (!RldObj_Word(written, writtenLength, &at, &name))
			{
				break;
			}
			memcpy(nameText, name.text, name.length);
			nameText[name.length] = '\0';

			if (!RldObj_Join(folder, nameText, path, sizeof(path)))
			{
				snprintf(path, sizeof(path), "%s", nameText);
			}
			for (k = 0; k < *libraryCount; k++)
			{
				if (strcmp(libraryPath[k], path) == 0)
				{
					break;
				}
			}
			if (k < *libraryCount)
			{
				if (!split)
				{
					break;
				}
				continue;
			}

			read = RldObj_ReadFile(path, RLDOBJ_MTL_BYTES, &data, &size);
			if (read == RLDOBJ_READ_MEMORY)
			{
				RldObj_Memory();
				return 0;
			}
			// Several names on one line - only when one of them is there,
			// otherwise the whole line is the name that is missing.
			if ((read == RLDOBJ_READ_MISSING) && !split && (strchr(written, ' ') != NULL))
			{
				struct RldObjWord probe;
				size_t probeAt = 0;
				int found = 0;

				while (!found && RldObj_Word(written, writtenLength, &probeAt, &probe))
				{
					char probePath[RLD_PATH_MAX];
					u8 *probeData = NULL;
					size_t probeSize = 0;

					memcpy(nameText, probe.text, probe.length);
					nameText[probe.length] = '\0';
					found = RldObj_Join(folder, nameText, probePath, sizeof(probePath)) &&
					        (RldObj_ReadFile(probePath, RLDOBJ_MTL_BYTES, &probeData, &probeSize) != RLDOBJ_READ_MISSING);
					free(probeData);
				}
				if (found)
				{
					split = 1;
					continue;
				}
			}
			if (*libraryCount >= RLDOBJ_LIBRARIES)
			{
				free(data);
				break;
			}

			snprintf(libraryPath[*libraryCount], RLD_PATH_MAX, "%s", path);
			if (read == RLDOBJ_READ_MISSING)
			{
				state = RLDMK_MTL_MISSING;
				RldMk_EmitFile("mtl", "missing", path, 0u);
				RldMk_Say("warning", "mtl-missing", path, "The material file %s was not found. The model keeps its vertex colors; faces without them are grey.",
				          path);
			}
			else if ((read == RLDOBJ_READ_LARGE) || !RldObj_ReadLibrary(m, *libraryCount, data, size, path))
			{
				state = RLDMK_MTL_BAD;
				RldMk_EmitFile("mtl", "bad", path, size);
				RldMk_Say("warning", "mtl-bad", (read == RLDOBJ_READ_LARGE) ? "larger than 16 MiB" : "no newmtl line, or not text",
				          "The material file %s cannot be read as MTL; its materials are not used.", path);
			}
			else
			{
				state = RLDMK_MTL_OK;
				RldMk_EmitFile("mtl", "ok", path, size);
			}
			free(data);
			RldMk_EmitModel("mtl", s_rldObjMtlWords[state], path, NULL, NULL);
			(*libraryCount)++;
			info->libraries++;

			// The worst state counts: bad, then missing, then ok.
			if ((info->mtl == RLDMK_MTL_NONE) || (state == RLDMK_MTL_BAD) || ((state == RLDMK_MTL_MISSING) && (info->mtl != RLDMK_MTL_BAD)))
			{
				info->mtl = state;
			}

			if (!split)
			{
				break;
			}
		}
	}
	return 1;
}

//========================================================================================
// TEXTURES
//========================================================================================

// The size in the first frame header of a JPEG. 0 = not found.
static int RldObj_JpegSize(const u8 *data, size_t size, u32 *width, u32 *height)
{
	size_t at = 2;

	while ((at + 4u) <= size)
	{
		u32 marker;
		u32 length;

		if (data[at] != 0xFFu)
		{
			return 0;
		}
		marker = data[at + 1u];
		if (marker == 0xFFu)
		{
			at++;
			continue;
		}
		if ((marker == 0xD8u) || (marker == 0x01u) || ((marker >= 0xD0u) && (marker <= 0xD7u)))
		{
			at += 2u;
			continue;
		}
		length = ((u32)data[at + 2u] << 8) | data[at + 3u];
		if (length < 2u)
		{
			return 0;
		}
		if ((marker >= 0xC0u) && (marker <= 0xCFu) && (marker != 0xC4u) && (marker != 0xC8u) && (marker != 0xCCu))
		{
			if ((at + 9u) > size)
			{
				return 0;
			}
			*height = ((u32)data[at + 5u] << 8) | data[at + 6u];
			*width = ((u32)data[at + 7u] << 8) | data[at + 8u];
			return 1;
		}
		at += 2u + length;
	}
	return 0;
}

// Pictures that are not read, by their first bytes.
static const char *RldObj_OtherPicture(const u8 *d, size_t size)
{
	if ((size >= 2u) && (d[0] == 'B') && (d[1] == 'M'))
	{
		return "a BMP picture";
	}
	if ((size >= 4u) && (memcmp(d, "GIF8", 4) == 0))
	{
		return "a GIF picture";
	}
	if ((size >= 4u) && (memcmp(d, "DDS ", 4) == 0))
	{
		return "a DDS texture";
	}
	if ((size >= 4u) && (memcmp(d, "8BPS", 4) == 0))
	{
		return "a Photoshop file";
	}
	if ((size >= 4u) && ((memcmp(d, "II*\0", 4) == 0) || (memcmp(d, "MM\0*", 4) == 0)))
	{
		return "a TIFF picture";
	}
	if ((size >= 12u) && (memcmp(d, "RIFF", 4) == 0) && (memcmp(&d[8], "WEBP", 4) == 0))
	{
		return "a WebP picture";
	}
	if ((size >= 4u) && (d[0] == 0x76u) && (d[1] == 0x2Fu) && (d[2] == 0x31u) && (d[3] == 0x01u))
	{
		return "an OpenEXR picture";
	}
	if ((size >= 2u) && (d[0] == '#') && (d[1] == '?'))
	{
		return "a Radiance HDR picture";
	}
	return "not a PNG, JPEG or TGA picture";
}

// A texture file -> RGBA. RLDMK_TEXTURE_*, and *why for the message.
static int RldObj_Decode(const char *path, const u8 *data, size_t size, struct RldObjTexture *t, int *fromImg, const char **why)
{
	static const u8 png[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
	const int jpeg = (size >= 3u) && (data[0] == 0xFFu) && (data[1] == 0xD8u) && (data[2] == 0xFFu);
	const int tga = Rld_EndsWithNoCase(path, ".tga") || ((size >= 26u) && (memcmp(&data[size - 18u], "TRUEVISION-XFILE.", 17) == 0));

	*fromImg = 0;
	*why = NULL;

	if (size == 0u)
	{
		*why = "the file is empty";
		return RLDMK_TEXTURE_UNREADABLE;
	}

	if ((size >= 8u) && (memcmp(data, png, 8) == 0))
	{
		struct RldPngImage image;
		const int result = RldPng_Decode(data, size, &image, why);

		if (result == RLDPNG_OK)
		{
			t->width = image.width;
			t->height = image.height;
			t->rgba = image.rgba;
			return RLDMK_TEXTURE_OK;
		}
		if (result == RLDPNG_TOO_LARGE)
		{
			*why = "larger than 4096 pixels on a side";
			return RLDMK_TEXTURE_UNSUPPORTED;
		}
		if (*why == NULL)
		{
			*why = (result == RLDPNG_MEMORY) ? "out of memory" : "a broken PNG";
		}
		return RLDMK_TEXTURE_UNREADABLE;
	}

	if (jpeg || (tga && (size >= 18u)))
	{
		u32 width = 0;
		u32 height = 0;
		unsigned int w = 0;
		unsigned int h = 0;
		unsigned char *rgba = NULL;
		const int sized = jpeg ? RldObj_JpegSize(data, size, &width, &height) : 1;

		if (!jpeg)
		{
			width = (u32)data[12] | ((u32)data[13] << 8);
			height = (u32)data[14] | ((u32)data[15] << 8);
		}
		if (sized && ((width > RLDPNG_SIDE_MAX) || (height > RLDPNG_SIDE_MAX)))
		{
			*why = "larger than 4096 pixels on a side";
			return RLDMK_TEXTURE_UNSUPPORTED;
		}
		// An uncompressed TGA cut short: stb_image fills the rest with 0.
		if (!jpeg && (data[2] >= 1u) && (data[2] <= 3u))
		{
			const u64 palette = (data[1] != 0u) ? ((u64)((u32)data[5] | ((u32)data[6] << 8)) * (((u32)data[7] + 7u) / 8u)) : 0u;
			const u64 need = 18u + (u64)data[0] + palette + (u64)width * height * (((u32)data[16] + 7u) / 8u);

			if ((u64)size < need)
			{
				*why = "the file ends early";
				return RLDMK_TEXTURE_UNREADABLE;
			}
		}
		if (RldImg_Decode(data, size, &w, &h, &rgba, why) != 0)
		{
			return RLDMK_TEXTURE_UNREADABLE;
		}
		t->width = w;
		t->height = h;
		t->rgba = rgba;
		*fromImg = 1;
		return RLDMK_TEXTURE_OK;
	}

	*why = RldObj_OtherPicture(data, size);
	return RLDMK_TEXTURE_UNSUPPORTED;
}

// The texture at (u, v): bilinear between the four nearest texels, v = 0 at
// the bottom row. 0..1 per channel. Repeated (u and v modulo 1), or with
// clamp (u and v in 0..1) the texels at the edge are the last ones: within
// half a texel of the edge the other side of the picture is never mixed in.
// Each texel counts by its alpha: a transparent one (often black in its RGB)
// adds nothing. 0 = all four are transparent, *out not set.
static int RldObj_Sample(const struct RldObjTexture *t, double u, double v, int clamp, double out[3])
{
	const s64 w = (s64)t->width;
	const s64 h = (s64)t->height;
	double fu = u - RldMk_Floor(u);
	double fv = v - RldMk_Floor(v);
	double x;
	double y;
	double ax;
	double ay;
	s64 x0;
	s64 y0;
	u32 x1;
	u32 y1;
	int c;

	// A value too large to have a fraction left, or a rounding up to 1.
	if (!(fu >= 0.0) || (fu >= 1.0))
	{
		fu = 0.0;
	}
	if (!(fv >= 0.0) || (fv >= 1.0))
	{
		fv = 0.0;
	}

	if (clamp)
	{
		x = u * (double)w - 0.5;
		y = (1.0 - v) * (double)h - 0.5;
		x = (x < 0.0) ? 0.0 : ((x > (double)(w - 1)) ? (double)(w - 1) : x);
		y = (y < 0.0) ? 0.0 : ((y > (double)(h - 1)) ? (double)(h - 1) : y);
		x0 = (s64)RldMk_Floor(x);
		y0 = (s64)RldMk_Floor(y);
		ax = x - (double)x0;
		ay = y - (double)y0;
		x1 = (u32)((x0 + 1 < w) ? (x0 + 1) : x0);
		y1 = (u32)((y0 + 1 < h) ? (y0 + 1) : y0);
	}
	else
	{
		x = fu * (double)w - 0.5;
		y = (1.0 - fv) * (double)h - 0.5;
		ax = x - RldMk_Floor(x);
		ay = y - RldMk_Floor(y);
		x0 = (((s64)RldMk_Floor(x) % w) + w) % w;
		y0 = (((s64)RldMk_Floor(y) % h) + h) % h;
		x1 = (u32)((x0 + 1) % w);
		y1 = (u32)((y0 + 1) % h);
	}

	{
		const u8 *q00 = &t->rgba[4u * ((u32)y0 * t->width + (u32)x0)];
		const u8 *q10 = &t->rgba[4u * ((u32)y0 * t->width + x1)];
		const u8 *q01 = &t->rgba[4u * (y1 * t->width + (u32)x0)];
		const u8 *q11 = &t->rgba[4u * (y1 * t->width + x1)];

		if ((q00[3] & q10[3] & q01[3] & q11[3]) != 255u)
		{
			const double w00 = (1.0 - ax) * (1.0 - ay) * (double)q00[3];
			const double w10 = ax * (1.0 - ay) * (double)q10[3];
			const double w01 = (1.0 - ax) * ay * (double)q01[3];
			const double w11 = ax * ay * (double)q11[3];
			const double total = w00 + w10 + w01 + w11;

			if (!(total > 0.0))
			{
				return 0;
			}
			for (c = 0; c < 3; c++)
			{
				out[c] = (w00 * (double)q00[c] + w10 * (double)q10[c] + w01 * (double)q01[c] + w11 * (double)q11[c]) / total / 255.0;
			}
			return 1;
		}
		for (c = 0; c < 3; c++)
		{
			const double p00 = (double)q00[c];
			const double p10 = (double)q10[c];
			const double p01 = (double)q01[c];
			const double p11 = (double)q11[c];
			const double top = p00 + (p10 - p00) * ax;
			const double bottom = p01 + (p11 - p01) * ax;

			out[c] = (top + (bottom - top) * ay) / 255.0;
		}
	}
	return 1;
}

static const char *const s_rldObjTextureWords[4] = {"ok", "missing", "unreadable", "unsupported"};

//========================================================================================
// THE COLORS OF THE CORNERS
//========================================================================================

// 0..1 -> bytes, the rounding of a PLY's float colors.
static void RldObj_Store(u8 *rgb, const double c[3])
{
	int k;

	for (k = 0; k < 3; k++)
	{
		rgb[k] = RldMk_PlyColor(RLDMK_PLY_FLOAT64, c[k]);
	}
}

// The material of a face, or NULL.
static const struct RldObjMaterial *RldObj_FaceMaterial(const struct RldObjModel *m, u32 face)
{
	const u32 name = m->faceName[face];
	const u32 material = (name != RLDMK_NONE) ? m->nameMaterial[name] : RLDMK_NONE;

	return (material != RLDMK_NONE) ? &m->materials[material] : NULL;
}

// The rules without textures: every corner's color and where it came from.
static void RldObj_BaseColors(const struct RldObjModel *m, u8 *cornerRgb, u8 *cornerSource)
{
	u32 f;

	for (f = 0; f < m->faceCount; f++)
	{
		const struct RldObjMaterial *material = RldObj_FaceMaterial(m, f);
		u32 k;

		for (k = 0; k < m->faceCorners[f]; k++)
		{
			const u32 corner = m->faceFirst[f] + k;
			const u32 v = m->cornerV[corner];

			if (m->vColored[v])
			{
				RldObj_Store(&cornerRgb[3u * corner], &m->vrgb[3u * v]);
				cornerSource[corner] = (u8)RLDMK_COLORS_VERTEX;
			}
			else if ((material != NULL) && material->hasKd)
			{
				RldObj_Store(&cornerRgb[3u * corner], material->kd);
				cornerSource[corner] = (u8)RLDMK_COLORS_MATERIAL;
			}
			else
			{
				memset(&cornerRgb[3u * corner], 0x80, 3u);
				cornerSource[corner] = (u8)RLDMK_COLORS_GREY;
			}
		}
	}
}

// Are the vertex colors only a fill? All vertices the faces use have the same
// color, exactly white or exactly black (what an unpainted color attribute
// holds), while the materials in use have colors of their own: 2 or more
// different Kd, or a texture. Then the vertex colors are dropped and the
// materials count (obj-vertex-colors-uniform); --colors-from decides over
// it. Any other color, even one for all vertices, stays.
static void RldObj_UniformColors(struct RldObjModel *m, const u8 *nameUsed)
{
	u8 first[3] = {0, 0, 0};
	u8 kd[2][3];
	u32 kds = 0;
	int colored = 0;
	int same = 1;
	int textured = 0;
	u32 c;
	u32 i;

	if (s_rldObjOptions.colorsFrom != RLDOBJ_FROM_AUTO)
	{
		m->uniform = (s_rldObjOptions.colorsFrom == RLDOBJ_FROM_MATERIAL) && (m->colored != 0u);
		return;
	}

	for (c = 0; same && (c < m->cornerCount); c++)
	{
		const u32 v = m->cornerV[c];
		u8 rgb[3];

		if (!m->vColored[v])
		{
			continue;
		}
		RldObj_Store(rgb, &m->vrgb[3u * v]);
		if (!colored)
		{
			memcpy(first, rgb, 3u);
			colored = 1;
		}
		same = (memcmp(first, rgb, 3u) == 0);
	}
	if (!colored || !same || !(((first[0] == 0xFFu) && (first[1] == 0xFFu) && (first[2] == 0xFFu)) || ((first[0] | first[1] | first[2]) == 0u)))
	{
		return;
	}

	for (i = 0; i < m->names.count; i++)
	{
		const struct RldObjMaterial *material = (nameUsed[i] && (m->nameMaterial[i] != RLDMK_NONE)) ? &m->materials[m->nameMaterial[i]] : NULL;
		u8 rgb[3];

		if (material == NULL)
		{
			continue;
		}
		textured |= (material->map != NULL);
		if (material->hasKd && (kds < 2u))
		{
			RldObj_Store(rgb, material->kd);
			if ((kds == 0u) || (memcmp(kd[0], rgb, 3u) != 0))
			{
				memcpy(kd[kds], rgb, 3u);
				kds++;
			}
		}
	}
	if (textured || (kds >= 2u))
	{
		m->uniform = 1;
		RldMk_Say("warning", "obj-vertex-colors-uniform", (first[0] != 0u) ? "white" : "black",
		          "All vertex colors of the model are exactly %s, as an unpainted color attribute is filled; the materials' colors are used instead "
		          "(--colors-from vertex keeps the vertex colors).",
		          (first[0] != 0u) ? "white" : "black");
	}
}

// The texture coordinates of a corner as its material's map_Kd places them:
// offset + scale x vt, clamped into 0..1 with -clamp on.
static void RldObj_TextureUv(const struct RldObjModel *m, const struct RldObjMaterial *material, u32 vt, double uv[2])
{
	int k;

	for (k = 0; k < 2; k++)
	{
		uv[k] = material->offset[k] + material->scale[k] * m->uv[2u * vt + (u32)k];
		if (material->clampOn)
		{
			uv[k] = (uv[k] < 0.0) ? 0.0 : ((uv[k] > 1.0) ? 1.0 : uv[k]);
		}
	}
}

// Is the model a PS1 rip, whose vertex colors on textured faces modulate the
// texture (0x80 = the texture as it is), and is that used? The textured
// corners are those of faces whose material has a map_Kd, found or not;
// their vertex colors (the brightest channel; a corner without one counts as
// white) have their median at 0x70..0x90 and at least 5 % of them are
// exactly 0x7F or 0x80 grey. A model painted in Blender has white (0xFF)
// there. --vertex-colors decides over
// it. Says @model vertex-colors <mode> <rip|plain>.
static void RldObj_VertexColors(struct RldObjModel *m)
{
	u32 histogram[256];
	u32 corners = 0;
	u32 grey = 0;
	u32 f;
	int rip = 0;

	memset(histogram, 0, sizeof(histogram));
	for (f = 0; f < m->faceCount; f++)
	{
		const struct RldObjMaterial *material = RldObj_FaceMaterial(m, f);
		u32 k;

		if ((material == NULL) || (material->map == NULL))
		{
			continue;
		}
		for (k = 0; k < m->faceCorners[f]; k++)
		{
			const u32 v = m->cornerV[m->faceFirst[f] + k];
			u8 rgb[3];
			u8 top;

			// A corner without a vertex color counts as white.
			if (!m->vColored[v])
			{
				histogram[255]++;
				corners++;
				continue;
			}
			RldObj_Store(rgb, &m->vrgb[3u * v]);
			top = (rgb[0] > rgb[1]) ? rgb[0] : rgb[1];
			top = (rgb[2] > top) ? rgb[2] : top;
			histogram[top]++;
			corners++;
			grey += ((rgb[0] == rgb[1]) && (rgb[1] == rgb[2]) && ((rgb[0] == 0x7Fu) || (rgb[0] == 0x80u))) ? 1u : 0u;
		}
	}

	if (corners != 0u)
	{
		// The lower median: the value of the ((corners - 1) / 2)-th corner.
		const u32 middle = (corners - 1u) / 2u;
		u32 below = 0;
		u32 value;

		for (value = 0; value < 255u; value++)
		{
			if ((below + histogram[value]) > middle)
			{
				break;
			}
			below += histogram[value];
		}
		rip = (value >= 0x70u) && (value <= 0x90u) && ((20u * grey) >= corners);
	}

	m->modulate = (s_rldObjOptions.vertexColors == RLDOBJ_VERTEX_MODULATE) || ((s_rldObjOptions.vertexColors == RLDOBJ_VERTEX_AUTO) && rip);
	RldMk_EmitModel("vertex-colors", s_rldObjVertexWords[s_rldObjOptions.vertexColors], rip ? "rip" : "plain", NULL, NULL);
}

// One texture onto the corners of the faces whose material uses it: the
// vertex color, or else Kd (1 without Kd), times the texture; a vertex color
// x 255/128 (at most 1) when it modulates (RldObj_VertexColors). Returns the
// corners that have no vt and keep their color; *clear counts those on fully
// transparent texels, which keep it as well.
static u32 RldObj_ApplyTexture(struct RldObjModel *m, u32 texture, u8 *cornerRgb, u8 *cornerSource, u32 *clear)
{
	const struct RldObjTexture *t = &m->textures[texture];
	u32 noUv = 0;
	u32 f;
	int clamp;

	for (f = 0; f < m->faceCount; f++)
	{
		const struct RldObjMaterial *material = RldObj_FaceMaterial(m, f);
		u32 k;

		if ((material == NULL) || (material->texture != texture))
		{
			continue;
		}

		// A face whose texture coordinates all lie in 0..1 shows one piece
		// of the picture: sampled with its edges clamped, not repeated.
		clamp = 1;
		for (k = 0; k < m->faceCorners[f]; k++)
		{
			const u32 vt = m->cornerT[m->faceFirst[f] + k];
			double uv[2];

			if (vt == RLDMK_NONE)
			{
				continue;
			}
			RldObj_TextureUv(m, material, vt, uv);
			if (!((uv[0] >= 0.0) && (uv[0] <= 1.0) && (uv[1] >= 0.0) && (uv[1] <= 1.0)))
			{
				clamp = 0;
			}
		}

		for (k = 0; k < m->faceCorners[f]; k++)
		{
			const u32 corner = m->faceFirst[f] + k;
			const u32 v = m->cornerV[corner];
			const u32 vt = m->cornerT[corner];
			double sample[3];
			double uv[2];
			double c[3];
			int n;

			if (vt == RLDMK_NONE)
			{
				noUv++;
				continue;
			}

			RldObj_TextureUv(m, material, vt, uv);
			if (!RldObj_Sample(t, uv[0], uv[1], clamp, sample))
			{
				(*clear)++;
				continue;
			}
			for (n = 0; n < 3; n++)
			{
				double base = m->vColored[v] ? m->vrgb[3u * v + (u32)n] : (material->hasKd ? material->kd[n] : 1.0);

				// As on the PS1: 0x80 = the texture as it is.
				if (m->vColored[v] && m->modulate)
				{
					base = base * (255.0 / 128.0);
					base = (base > 1.0) ? 1.0 : base;
				}
				c[n] = base * sample[n];
			}
			m->modulated += (m->vColored[v] && m->modulate) ? 1u : 0u;
			RldObj_Store(&cornerRgb[3u * corner], c);
			cornerSource[corner] = (u8)RLDMK_COLORS_TEXTURE;
		}
	}
	return noUv;
}

// A texture that is not at its path is looked for at a fixed list of other
// places, never by listing a folder. In each folder of `folders` (the MTL
// file's, then the OBJ file's): the last two parts of the path (a texture
// in textures/ of a project moved to another computer), the file name, the
// file name in one of these folders, then the name with another of these
// file types. The first file there counts.
static const char *const s_rldObjTextureFolders[] = {"textures", "tex", "images", "maps"};
static const char *const s_rldObjTextureTypes[] = {".png", ".jpg", ".jpeg", ".tga"};
#define RLDOBJ_TEXTURE_FOLDERS (sizeof(s_rldObjTextureFolders) / sizeof(s_rldObjTextureFolders[0]))
#define RLDOBJ_TEXTURE_TYPES (sizeof(s_rldObjTextureTypes) / sizeof(s_rldObjTextureTypes[0]))

// The places for one folder in the order above: place `k` -> out. 0 = no such
// place (or too long), -1 = past the last one.
static int RldObj_TexturePlace(const char *folder, const char *given, u32 k, char *out, size_t outSize)
{
	const char *name = Rld_LastComponent(given);
	const char *tail = name;
	const char *dot = strrchr(name, '.');
	const size_t stem = ((dot != NULL) && (dot != name)) ? (size_t)(dot - name) : strlen(name);
	int n;

	// The last two parts: back over the separators before the name, then
	// over the folder name before them.
	{
		const char *p = name;
		const char *end;

		while ((p > given) && Rld_IsSeparator(p[-1]))
		{
			p--;
		}
		end = p;
		while ((p > given) && !Rld_IsSeparator(p[-1]) && (p[-1] != ':'))
		{
			p--;
		}
		tail = (p < end) ? p : name;
	}

	if (k == 0u)
	{
		n = (tail != name) ? snprintf(out, outSize, "%s%s", folder, tail) : -1;
	}
	else if (k == 1u)
	{
		n = snprintf(out, outSize, "%s%s", folder, name);
	}
	else if (k < (2u + RLDOBJ_TEXTURE_FOLDERS))
	{
		n = snprintf(out, outSize, "%s%s/%s", folder, s_rldObjTextureFolders[k - 2u], name);
	}
	else if (k < (2u + RLDOBJ_TEXTURE_FOLDERS + (1u + RLDOBJ_TEXTURE_FOLDERS) * RLDOBJ_TEXTURE_TYPES))
	{
		// The other file types: next to it, then in the folders.
		const u32 at = k - 2u - RLDOBJ_TEXTURE_FOLDERS;
		const u32 sub = at / RLDOBJ_TEXTURE_TYPES;
		const char *type = s_rldObjTextureTypes[at % RLDOBJ_TEXTURE_TYPES];

		if ((dot != NULL) && (dot != name) && Rld_EndsWithNoCase(name, type) && (strlen(dot) == strlen(type)))
		{
			return 0;
		}
		n = (sub == 0u) ? snprintf(out, outSize, "%s%.*s%s", folder, (int)stem, name, type)
		                : snprintf(out, outSize, "%s%s/%.*s%s", folder, s_rldObjTextureFolders[sub - 1u], (int)stem, name, type);
	}
	else
	{
		return -1;
	}
	return (n > 0) && ((size_t)n < outSize);
}

// The texture `given` (not found there) at the other places. RLDOBJ_READ_* of
// the file found, its path in found; RLDOBJ_READ_MISSING = none.
static int RldObj_SearchTexture(const char *given, const char *const *folders, u32 folderCount, u8 **data, size_t *size, char *found, size_t foundSize)
{
	u32 k;

	for (k = 0;; k++)
	{
		int more = 0;
		u32 f;

		for (f = 0; f < folderCount; f++)
		{
			const int place = RldObj_TexturePlace(folders[f], given, k, found, foundSize);
			int read;

			if (place < 0)
			{
				continue;
			}
			more = 1;
			if ((place == 0) || (strcmp(found, given) == 0))
			{
				continue;
			}
			read = RldObj_ReadFile(found, RLDOBJ_TEXTURE_BYTES, data, size);
			if (read != RLDOBJ_READ_MISSING)
			{
				return read;
			}
		}
		if (!more)
		{
			break;
		}
	}
	found[0] = '\0';
	return RLDOBJ_READ_MISSING;
}

// All textures that were not found in one warning (tex-missing): which ones,
// how many faces show their color without them, where they were looked for
// and where to put them. The path of each is in its @model texture line.
static void RldObj_SayMissing(const struct RldObjModel *m, char (*libraryPath)[RLD_PATH_MAX])
{
	u32 first = RLDMK_NONE;
	char names[400];
	char where[RLD_PATH_MAX + 160];
	char more[32];
	size_t used = 0;
	u32 missing = 0;
	u32 listed = 0;
	u32 faces = 0;
	u32 i;

	names[0] = '\0';
	more[0] = '\0';
	for (i = 0; i < m->textureCount; i++)
	{
		if (m->textures[i].state != RLDMK_TEXTURE_MISSING)
		{
			continue;
		}
		first = (first == RLDMK_NONE) ? i : first;
		missing++;
		if (listed < 3u)
		{
			const int n = snprintf(&names[used], sizeof(names) - used, "%s%s", (used != 0u) ? ", " : "", Rld_LastComponent(m->textures[i].path));

			if ((n > 0) && ((size_t)n < (sizeof(names) - used)))
			{
				used += (size_t)n;
				listed++;
			}
			else
			{
				names[used] = '\0';
			}
		}
	}
	if (first == RLDMK_NONE)
	{
		return;
	}
	for (i = 0; i < m->faceCount; i++)
	{
		const struct RldObjMaterial *material = RldObj_FaceMaterial(m, i);

		if ((material != NULL) && (material->texture != RLDMK_NONE) && (m->textures[material->texture].state == RLDMK_TEXTURE_MISSING))
		{
			faces++;
		}
	}

	// The technical line: where the first one was looked for.
	snprintf(where, sizeof(where), "looked for at %s, %snext to the MTL and OBJ files, in their folders textures, tex, images and maps, also as .png, .jpg, "
	         ".jpeg and .tga", m->textures[first].path, (s_rldObjOptions.textureDirCount != 0u) ? "in the --textures folders, " : "");

	if (missing == 1u)
	{
		RldMk_Say("warning", "tex-missing", where,
		          "The texture %s was not found: %u of %u faces %s only the vertex or material color. Put %s next to %s and check again.", names, faces,
		          m->faceCount, (faces == 1u) ? "shows" : "show", names, Rld_LastComponent(libraryPath[m->textures[first].library]));
		return;
	}
	if (missing > listed)
	{
		snprintf(more, sizeof(more), " and %u more", missing - listed);
	}
	RldMk_Say("warning", "tex-missing", where,
	          "%u of %u textures were not found (%s%s): %u of %u faces %s only the vertex or material color. Put the files next to %s and check again.",
	          missing, m->textureCount, names, more, faces, m->faceCount, (faces == 1u) ? "shows" : "show",
	          Rld_LastComponent(libraryPath[m->textures[first].library]));
}

// SHA-256 of a file as 64 hex digits.
static void RldObj_Digest(const u8 *data, size_t size, char out[65])
{
	static const char hex[] = "0123456789abcdef";
	u8 digest[32];
	u32 k;

	Sha256(data, size, digest);
	for (k = 0; k < 32u; k++)
	{
		out[2u * k] = hex[digest[k] >> 4];
		out[2u * k + 1u] = hex[digest[k] & 15u];
	}
	out[64] = '\0';
}

// The texture of a material in use: the file it names, found, read, applied
// and freed again, one after the other. 0 = out of memory (said).
static int RldObj_Textures(struct RldObjModel *m, const char *objPath, char (*libraryPath)[RLD_PATH_MAX], const u8 *nameUsed, u8 *cornerRgb,
                           u8 *cornerSource, struct RldMkModelInfo *info)
{
	u32 said = 0;
	u32 unsaid = 0;
	u32 i;

	// One texture per file, in the order the faces first use the materials.
	m->textures = (struct RldObjTexture *)calloc((size_t)m->names.count + 1u, sizeof(struct RldObjTexture));
	if (m->textures == NULL)
	{
		RldObj_Memory();
		return 0;
	}
	for (i = 0; i < m->names.count; i++)
	{
		struct RldObjMaterial *material;
		char folder[RLD_PATH_MAX];
		char path[RLD_PATH_MAX];
		u32 k;

		if (!nameUsed[i] || (m->nameMaterial[i] == RLDMK_NONE) || (m->materials[m->nameMaterial[i]].map == NULL))
		{
			continue;
		}
		material = &m->materials[m->nameMaterial[i]];
		if (material->texture != RLDMK_NONE)
		{
			continue;
		}
		RldObj_Folder(libraryPath[material->library], folder, sizeof(folder));
		if (!RldObj_Join(folder, material->map, path, sizeof(path)))
		{
			snprintf(path, sizeof(path), "%s", material->map);
		}
		// One texture per file however its path is written (./t.png,
		// a/../t.png, T.PNG): read and applied once.
		RldObj_TidyPath(path);
		for (k = 0; k < m->textureCount; k++)
		{
			if (RldObj_SamePath(m->textures[k].path, path))
			{
				break;
			}
		}
		if (k == m->textureCount)
		{
			snprintf(m->textures[k].path, sizeof(m->textures[k].path), "%s", path);
			m->textures[k].library = material->library;
			m->textureCount++;
		}
		material->texture = k;
	}

	for (i = 0; i < m->textureCount; i++)
	{
		struct RldObjTexture *t = &m->textures[i];
		const char *why = NULL;
		const char *shown = t->path;
		char nearby[RLD_PATH_MAX];
		u8 *data = NULL;
		size_t size = 0;
		int fromImg = 0;
		int read = RldObj_ReadFile(t->path, RLDOBJ_TEXTURE_BYTES, &data, &size);
		u32 n;

		// Not at its path: the other places (RldObj_SearchTexture).
		if (read == RLDOBJ_READ_MISSING)
		{
			char folder[2][RLD_PATH_MAX];
			const char *folders[2];

			const char *dirs[RLDOBJ_TEXTURE_DIRS];
			u32 k;

			// The folders of --textures first, then the MTL and OBJ files'.
			for (k = 0; k < s_rldObjOptions.textureDirCount; k++)
			{
				dirs[k] = s_rldObjOptions.textureDirs[k];
			}
			read = RldObj_SearchTexture(t->path, dirs, s_rldObjOptions.textureDirCount, &data, &size, nearby, sizeof(nearby));
			RldObj_Folder(libraryPath[t->library], folder[0], sizeof(folder[0]));
			RldObj_Folder(objPath, folder[1], sizeof(folder[1]));
			folders[0] = folder[0];
			folders[1] = folder[1];
			if (read == RLDOBJ_READ_MISSING)
			{
				read = RldObj_SearchTexture(t->path, folders, (strcmp(folder[0], folder[1]) != 0) ? 2u : 1u, &data, &size, nearby, sizeof(nearby));
			}
			if ((read == RLDOBJ_READ_OK) || (read == RLDOBJ_READ_LARGE))
			{
				RldMk_Say("info", "tex-found-nearby", t->path, "The texture %s is not at the path the material file gives; %s is used.",
				          Rld_LastComponent(t->path), nearby);
				shown = nearby;
			}
		}

		if (read == RLDOBJ_READ_MEMORY)
		{
			RldObj_Memory();
			return 0;
		}
		if (read == RLDOBJ_READ_MISSING)
		{
			t->state = RLDMK_TEXTURE_MISSING;
		}
		else if (read == RLDOBJ_READ_LARGE)
		{
			t->state = RLDMK_TEXTURE_UNSUPPORTED;
			why = "larger than 64 MiB";
		}
		else
		{
			// Which file was read: the bytes depend on the files next to
			// the model, the digest in @model texture makes that visible.
			RldObj_Digest(data, size, t->sha);
			t->state = RldObj_Decode(shown, data, size, t, &fromImg, &why);
		}
		free(data);

		if (t->state == RLDMK_TEXTURE_OK)
		{
			const size_t texels = (size_t)t->width * t->height;
			size_t p;
			u32 clear = 0;

			info->noUv += RldObj_ApplyTexture(m, i, cornerRgb, cornerSource, &clear);
			for (p = 0; p < texels; p++)
			{
				if (t->rgba[4u * p + 3u] != 255u)
				{
					if (clear == 0u)
					{
						RldMk_Say("info", "tex-alpha", shown, "The texture %s has transparency: transparent texels add nothing to the colors.",
						          Rld_LastComponent(shown));
					}
					else
					{
						RldMk_Say("info", "tex-alpha", shown,
						          "The texture %s has transparency: transparent texels add nothing to the colors; %u corners on fully transparent "
						          "texels keep the vertex or material color.",
						          Rld_LastComponent(shown), clear);
					}
					break;
				}
			}
			if (fromImg)
			{
				RldImg_Free(t->rgba);
			}
			else
			{
				free(t->rgba);
			}
			t->rgba = NULL;
		}

		// One line and one message per material that uses it.
		for (n = 0; n < m->names.count; n++)
		{
			const u32 material = m->nameMaterial[n];

			if (!nameUsed[n] || (material == RLDMK_NONE) || (m->materials[material].texture != i))
			{
				continue;
			}
			info->textures++;
			info->texturesOk += (t->state == RLDMK_TEXTURE_OK) ? 1u : 0u;
			RldMk_EmitModel("texture", s_rldObjTextureWords[t->state], m->names.text[n], shown, (t->sha[0] != '\0') ? t->sha : "-");
			if (t->state == RLDMK_TEXTURE_OK)
			{
				continue;
			}
			if (said >= RLDOBJ_TEXTURES_SAID)
			{
				unsaid++;
				continue;
			}
			if (t->state == RLDMK_TEXTURE_MISSING)
			{
				continue;   // all of them in one message (RldObj_SayMissing)
			}
			said++;
			if (t->state == RLDMK_TEXTURE_UNREADABLE)
			{
				RldMk_Say("warning", "tex-unreadable", why,
				          "The texture %s of the material \"%s\" cannot be read (%s); its faces keep the vertex or material color.", Rld_LastComponent(shown),
				          m->names.text[n], why);
			}
			else
			{
				RldMk_Say("warning", "tex-unsupported", why,
				          "The texture %s of the material \"%s\" is %s - PNG, JPEG and TGA up to 4096 pixels are read; its faces keep the vertex or material color.",
				          Rld_LastComponent(shown), m->names.text[n], why);
			}
		}
	}

	if (unsaid != 0u)
	{
		RldMk_Say("warning", "tex-missing", NULL, "%u more textures cannot be used (see the list of textures).", unsaid);
	}
	RldObj_SayMissing(m, libraryPath);
	return 1;
}

//========================================================================================
// THE POINTS
//========================================================================================

static u32 RldObj_PointHash(u32 v, u32 rgb)
{
	u32 h = v * 0x9E3779B1u;

	h ^= rgb * 0x85EBCA77u;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 13;
	return h;
}

// One point per different pair (v, color), in the order the faces first use
// it; then the faces as the PLY reader leaves them. 0 = said.
static int RldObj_Points(const struct RldObjModel *m, const u8 *cornerRgb, struct RldMkPly *ply)
{
	u32 slotCount = 1024u;
	u32 *slots;
	u32 *pointV;
	u32 *pointRgb;
	u32 *cornerPoint;
	u32 points = 0;
	u32 c;
	u32 f;
	int ok = 1;

	while (slotCount < (2u * m->cornerCount))
	{
		slotCount *= 2u;
	}
	slots = (u32 *)malloc((size_t)slotCount * sizeof(u32));
	pointV = (u32 *)malloc((size_t)m->cornerCount * sizeof(u32) + 4u);
	pointRgb = (u32 *)malloc((size_t)m->cornerCount * sizeof(u32) + 4u);
	cornerPoint = (u32 *)malloc((size_t)m->cornerCount * sizeof(u32) + 4u);
	if ((slots == NULL) || (pointV == NULL) || (pointRgb == NULL) || (cornerPoint == NULL))
	{
		RldObj_Memory();
		ok = 0;
	}

	if (ok)
	{
		memset(slots, 0xFF, (size_t)slotCount * sizeof(u32));
		for (c = 0; c < m->cornerCount; c++)
		{
			const u32 v = m->cornerV[c];
			const u32 rgb = ((u32)cornerRgb[3u * c] << 16) | ((u32)cornerRgb[3u * c + 1u] << 8) | cornerRgb[3u * c + 2u];
			u32 slot = RldObj_PointHash(v, rgb) & (slotCount - 1u);

			while ((slots[slot] != RLDMK_NONE) && ((pointV[slots[slot]] != v) || (pointRgb[slots[slot]] != rgb)))
			{
				slot = (slot + 1u) & (slotCount - 1u);
			}
			if (slots[slot] == RLDMK_NONE)
			{
				slots[slot] = points;
				pointV[points] = v;
				pointRgb[points] = rgb;
				points++;
			}
			cornerPoint[c] = slots[slot];
		}

		if (points > RLDMK_PLY_MAX)
		{
			RldObj_Big("points (one per vertex and color)", RLDMK_PLY_MAX);
			ok = 0;
		}
	}

	if (ok)
	{
		ply->vertexCount = points;
		ply->faceCount = m->faceCount;
		ply->xyz = (double *)calloc((size_t)points * 3u + 1u, sizeof(double));
		ply->rgb = (u8 *)malloc((size_t)points * 3u + 1u);
		ply->corners = (u32 *)calloc((size_t)m->faceCount * 4u + 1u, sizeof(u32));
		ply->cornerCount = (u32 *)calloc((size_t)m->faceCount + 1u, sizeof(u32));
		ply->moreStart = (u32 *)calloc((size_t)m->faceCount + 1u, sizeof(u32));
		if ((ply->xyz == NULL) || (ply->rgb == NULL) || (ply->corners == NULL) || (ply->cornerCount == NULL) || (ply->moreStart == NULL))
		{
			RldObj_Memory();
			ok = 0;
		}
	}

	if (ok)
	{
		u32 p;

		for (p = 0; p < points; p++)
		{
			memcpy(&ply->xyz[3u * p], &m->xyz[3u * pointV[p]], 3u * sizeof(double));
			ply->rgb[3u * p + 0u] = (u8)(pointRgb[p] >> 16);
			ply->rgb[3u * p + 1u] = (u8)(pointRgb[p] >> 8);
			ply->rgb[3u * p + 2u] = (u8)pointRgb[p];
		}
		ply->rgb[3u * points] = 0x80u;

		for (f = 0; ok && (f < m->faceCount); f++)
		{
			const u32 count = m->faceCorners[f];
			u32 k;

			ply->cornerCount[f] = count;
			ply->moreStart[f] = ply->moreCount;
			for (k = 0; k < count; k++)
			{
				const u32 point = cornerPoint[m->faceFirst[f] + k];

				if (k < 4u)
				{
					ply->corners[4u * f + k] = point;
				}
				if ((count > 4u) && !RldMk_PlyMore(ply, point))
				{
					RldObj_Memory();
					ok = 0;
					break;
				}
			}
		}
	}

	free(slots);
	free(pointV);
	free(pointRgb);
	free(cornerPoint);
	return ok;
}

//========================================================================================
// STEP 1 FOR OBJ
//========================================================================================

// The OBJ file `data` at `path` (MTL files and textures relative to it) ->
// *ply, the facts into *info. Says every error itself and returns 0 then.
static int RldObj_Parse(const char *path, const u8 *data, size_t size, struct RldMkPly *ply, struct RldMkModelInfo *info)
{
	static char libraryPath[RLDOBJ_LIBRARIES][RLD_PATH_MAX];
	struct RldObjModel m;
	u32 libraryCount = 0;
	u8 *cornerRgb = NULL;
	u8 *cornerSource = NULL;
	u8 *nameUsed = NULL;
	int ok;

	memset(&m, 0, sizeof(m));
	memset(ply, 0, sizeof(*ply));
	m.group = RLDMK_NONE;
	m.line = (char *)malloc(RLDOBJ_LINE_MAX + 1u);
	ok = (m.line != NULL);
	if (!ok)
	{
		RldObj_Memory();
	}

	ok = ok && RldObj_ReadLines(&m, data, size);
	ok = ok && RldObj_Libraries(&m, path, libraryPath, &libraryCount, info);

	// The material of every usemtl name, and which names the faces use.
	if (ok)
	{
		u32 i;

		m.nameMaterial = (u32 *)malloc((size_t)m.names.count * sizeof(u32) + 4u);
		nameUsed = (u8 *)calloc((size_t)m.names.count + 1u, 1u);
		cornerRgb = (u8 *)malloc((size_t)m.cornerCount * 3u + 1u);
		cornerSource = (u8 *)malloc((size_t)m.cornerCount + 1u);
		ok = (m.nameMaterial != NULL) && (nameUsed != NULL) && (cornerRgb != NULL) && (cornerSource != NULL);
		if (!ok)
		{
			RldObj_Memory();
		}
		for (i = 0; ok && (i < m.faceCount); i++)
		{
			if (m.faceName[i] != RLDMK_NONE)
			{
				nameUsed[m.faceName[i]] = 1u;
			}
		}
		for (i = 0; ok && (i < m.names.count); i++)
		{
			u32 index = RLDMK_NONE;

			if (RldObj_Name(&m.materialNames, m.names.text[i], strlen(m.names.text[i]), 0, &index) != 1)
			{
				index = RLDMK_NONE;
			}
			m.nameMaterial[i] = index;
			info->materials += nameUsed[i];
		}
	}

	// A name in use that no MTL file defines (only said when one was read).
	if (ok && (m.materialNames.count != 0u))
	{
		u32 missing = 0;
		u32 i;

		for (i = 0; i < m.names.count; i++)
		{
			if (nameUsed[i] && (m.nameMaterial[i] == RLDMK_NONE))
			{
				if (missing < RLDOBJ_TEXTURES_SAID)
				{
					RldMk_Say("warning", "mtl-material", m.names.text[i],
					          "The material \"%s\" is not in the material file; its faces are drawn as faces without a material.", m.names.text[i]);
				}
				missing++;
			}
		}
		if (missing > RLDOBJ_TEXTURES_SAID)
		{
			RldMk_Say("warning", "mtl-material", NULL, "%u more materials are not in the material file.", missing - RLDOBJ_TEXTURES_SAID);
		}
	}

	if (ok)
	{
		RldObj_UniformColors(&m, nameUsed);
		if (m.uniform)
		{
			memset(m.vColored, 0, m.vCount);
		}
		RldObj_VertexColors(&m);
		RldObj_BaseColors(&m, cornerRgb, cornerSource);
		ok = RldObj_Textures(&m, path, libraryPath, nameUsed, cornerRgb, cornerSource, info);
	}
	ok = ok && RldObj_Points(&m, cornerRgb, ply);

	if (ok)
	{
		int kdIgnored = 0;
		u32 i;

		for (i = 0; i < m.cornerCount; i++)
		{
			info->colors |= cornerSource[i];
		}
		for (i = 0; i < m.names.count; i++)
		{
			kdIgnored |= nameUsed[i] && (m.nameMaterial[i] != RLDMK_NONE) && m.materials[m.nameMaterial[i]].hasKd;
		}

		if ((m.colored != 0u) && kdIgnored && !m.uniform)
		{
			RldMk_Say("info", "obj-vertex-colors", NULL, "The model has vertex colors: they are used, and the materials' Kd colors are ignored.");
		}
		{
			u32 transparent = 0;

			for (i = 0; i < m.names.count; i++)
			{
				transparent += (nameUsed[i] && (m.nameMaterial[i] != RLDMK_NONE) && m.materials[m.nameMaterial[i]].transparent) ? 1u : 0u;
			}
			if (transparent != 0u)
			{
				RldMk_Say("info", "obj-transparency", NULL,
				          "%u material%s in use %s transparent (d, Tr or map_d); the game draws the model opaque.",
				          transparent, (transparent == 1u) ? "" : "s", (transparent == 1u) ? "is" : "are");
			}
		}
		if (m.modulated != 0u)
		{
			RldMk_Say("info", "obj-vertex-modulation", s_rldObjVertexWords[s_rldObjOptions.vertexColors],
			          "On %u textured corners the vertex color modulates the texture as on the PS1: 0x80 shows the texture as it is (--vertex-colors color; "
			          "Reload Studio: Vertex colors: Plain color).",
			          m.modulated);
		}
		if (m.clamped != 0u)
		{
			RldMk_Say("warning", "obj-color-range", NULL, "%u color values of the model were outside 0..1 and were clamped.", m.clamped);
		}
		if (info->noUv != 0u)
		{
			RldMk_Say("warning", "tex-no-uv", NULL, "%u corners of textured faces have no texture coordinates (vt); they keep the vertex or material color.",
			          info->noUv);
		}
		if (info->colors == RLDMK_COLORS_GREY)
		{
			RldMk_Say("warning", "obj-no-colors", NULL, "The model has no vertex colors, no material colors and no textures, so it is drawn in one grey.");
		}

		for (i = 0; i < m.groupCount; i++)
		{
			char faces[16];

			snprintf(faces, sizeof(faces), "%u", m.groups[i].faces);
			RldMk_EmitModel("group", faces, (m.groups[i].name[0] != '\0') ? m.groups[i].name : "-", NULL, NULL);
		}
		if (m.groupsMore != 0u)
		{
			RldMk_Say("info", "obj-groups", NULL, "%u more groups (o, g) are not listed.", m.groupsMore);
		}

		info->groups = m.groupCount + m.groupsMore;
		info->points = ply->vertexCount;
		// Every corner has its color by the rule, grey included: an OBJ all
		// grey was said above (obj-no-colors), the chain need not say it
		// again in the words of a PLY (ply-no-colors).
		ply->hasColors = 1;
	}

	free(cornerRgb);
	free(cornerSource);
	free(nameUsed);
	RldObj_FreeModel(&m);
	if (!ok)
	{
		RldMk_FreePly(ply);
	}
	return ok;
}
