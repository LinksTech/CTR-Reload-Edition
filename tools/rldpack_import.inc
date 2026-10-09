// rldpack_import.inc - make-char: the model file in front of the chain
//
// Part of the packer, never of the game. tools/rldpack_char.inc includes it
// right after the PLY reader. One table of readers: each knows its format by
// the first bytes of the file and fills the same struct RldMkPly, so the chain
// after step 1 stays the same for every format. A new format is one more entry
// in s_rldMkReaders and one reader; nothing else changes.
//
// THE FORMAT IS TAKEN FROM THE CONTENT. The name of the file is only compared:
// a PLY named .obj is read as PLY and said (model-misnamed). Files of other 3D
// programs and pictures are named and refused (model-unsupported), anything
// else is model-unknown - except a file named .ply, which goes to the PLY
// reader as before and gets its message. A PLY named .ply says nothing new:
// its output stays as it was, apart from the @model lines below.
//
// MACHINE LINES (--machine), for the model and the own mask (--mask-model):
//   @file  ply|obj|gltf <state> <path> <bytes> as before (the mask: @file mask-model)
//   @model format ply|obj|gltf
//   @model mtl ok|missing|bad|none <path|->    OBJ: one per MTL file, none = no mtllib
//   @file  mtl ok|missing|bad <path> <bytes>   OBJ: the same files
//   @model texture ok|missing|unreadable|unsupported <material> <path> <sha256|->
//                                              OBJ: one per material in use with map_Kd;
//                                              the SHA-256 of the file read, - = none
//   @model group <faces> <name>                OBJ: one per o/g line, in file order
//   @model colors vertex|material|texture|grey|mixed
//                                              where the colors of the corners came from
//   glTF (tools/rldpack_gltf.inc): @model texture, group and colors as for
//   an OBJ (a group per node with a mesh), no mtl lines, and
//   @model shape-key <name> <key|unknown>       one per shape key name
// char-poses keeps its lines (@file ply, no @model); an OBJ there is
// model-ply-only, a glTF is read for its shape keys (s_rldGltfMorphs).
// char-wheel reads PLY or OBJ by itself (tools/rldpack_wheel.inc, @file
// wheel). make-char --wheel-model reads PLY, OBJ or glTF
// (RLDMK_ROLE_CHAR_WHEEL): @file wheel-model <state> <path> <bytes>, no
// @model lines (they are about the model itself).

enum
{
	RLDMK_FORMAT_UNKNOWN = 0,
	RLDMK_FORMAT_PLY,
	RLDMK_FORMAT_OBJ,
	RLDMK_FORMAT_GLTF
};

// Who reads a model: make-char and --diagnose (main), --mask-model (mask);
// the poses read PLY and glTF; make-char --wheel-model (a wheel of one's own
// for the native model) PLY, OBJ or glTF. RLDMK_ROLE_WHEEL is no longer passed by
// anyone (char-wheel reads PLY or OBJ by itself, tools/rldpack_wheel.inc); it
// stays only in the table of readers below.
#define RLDMK_ROLE_MAIN 1u
#define RLDMK_ROLE_MASK 2u
#define RLDMK_ROLE_POSE 4u
#define RLDMK_ROLE_WHEEL 8u
#define RLDMK_ROLE_CHAR_WHEEL 16u

// Set while make-char reads its --wheel-model: no @model lines.
static int s_rldMkEmitModelOff;

// Where the color of a corner came from (bits: a model may mix them).
#define RLDMK_COLORS_VERTEX 1u
#define RLDMK_COLORS_MATERIAL 2u
#define RLDMK_COLORS_TEXTURE 4u
#define RLDMK_COLORS_GREY 8u

enum
{
	RLDMK_MTL_NONE = 0,
	RLDMK_MTL_OK,
	RLDMK_MTL_MISSING,
	RLDMK_MTL_BAD
};

enum
{
	RLDMK_TEXTURE_OK = 0,
	RLDMK_TEXTURE_MISSING,
	RLDMK_TEXTURE_UNREADABLE,
	RLDMK_TEXTURE_UNSUPPORTED
};

// What the reader found besides the mesh - for the machine lines and the
// self-test; the chain reads none of it.
struct RldMkModelInfo
{
	int format;       // RLDMK_FORMAT_*
	u32 colors;       // RLDMK_COLORS_* of all corners
	int mtl;          // RLDMK_MTL_*: the worst of the MTL files
	u32 libraries;    // MTL files named
	u32 materials;    // materials the faces use
	u32 textures;     // materials in use with a texture
	u32 texturesOk;   // ... whose texture was read
	u32 noUv;         // corners of textured faces without vt
	u32 groups;       // o/g lines
	u32 points;       // points made (OBJ: one per vertex and color)
	u32 role;         // RLDMK_ROLE_* it is read for (set before the reader runs)
};

// @model <key> <fields> (up to four, the first NULL ends them) - not while
// the self-test runs quietly.
static void RldMk_EmitModel(const char *key, const char *a, const char *b, const char *c, const char *d)
{
	if (!s_machine || s_rldMkQuiet || s_rldMkEmitModelOff)
	{
		return;
	}
	if (b == NULL)
	{
		Rld_Emit("model", key, a, (const char *)NULL);
	}
	else if (c == NULL)
	{
		Rld_Emit("model", key, a, b, (const char *)NULL);
	}
	else if (d == NULL)
	{
		Rld_Emit("model", key, a, b, c, (const char *)NULL);
	}
	else
	{
		Rld_Emit("model", key, a, b, c, d, (const char *)NULL);
	}
}

#include "rldpack_obj.inc"
#include "rldpack_gltf.inc"

//========================================================================================
// THE READERS
//========================================================================================

static int RldMk_DetectPly(const u8 *data, size_t size)
{
	return (size >= 4u) && (memcmp(data, "ply", 3) == 0) && ((data[3] == '\n') || (data[3] == '\r'));
}

static int RldMk_ReadPlyModel(const char *path, const u8 *data, size_t size, struct RldMkPly *ply, struct RldMkModelInfo *info)
{
	(void)path;
	if (!RldMk_ParsePly(data, size, ply))
	{
		return 0;
	}
	info->colors = ply->hasColors ? RLDMK_COLORS_VERTEX : RLDMK_COLORS_GREY;
	info->points = ply->vertexCount;
	return 1;
}

struct RldMkReader
{
	int format;                  // RLDMK_FORMAT_*
	const char *name;            // in messages
	const char *article;         // "a PLY", "an OBJ"
	const char *word;            // in machine lines
	const char *extensions[4];   // compared with the file name, NULL-terminated
	u32 roles;                   // RLDMK_ROLE_* that may read it
	int (*detect)(const u8 *data, size_t size);
	int (*read)(const char *path, const u8 *data, size_t size, struct RldMkPly *ply, struct RldMkModelInfo *info);
};

static const struct RldMkReader s_rldMkReaders[] = {
	{RLDMK_FORMAT_PLY, "PLY", "a", "ply", {".ply", NULL, NULL, NULL},
	 RLDMK_ROLE_MAIN | RLDMK_ROLE_MASK | RLDMK_ROLE_POSE | RLDMK_ROLE_WHEEL | RLDMK_ROLE_CHAR_WHEEL, RldMk_DetectPly, RldMk_ReadPlyModel},
	{RLDMK_FORMAT_OBJ, "OBJ", "an", "obj", {".obj", NULL, NULL, NULL}, RLDMK_ROLE_MAIN | RLDMK_ROLE_MASK | RLDMK_ROLE_CHAR_WHEEL, RldObj_Detect,
	 RldObj_Parse},
	{RLDMK_FORMAT_GLTF, "glTF", "a", "gltf", {".glb", ".gltf", NULL, NULL}, RLDMK_ROLE_MAIN | RLDMK_ROLE_MASK | RLDMK_ROLE_POSE | RLDMK_ROLE_CHAR_WHEEL,
	 RldGltf_Detect, RldGltf_Parse},
};

#define RLDMK_READER_COUNT (sizeof(s_rldMkReaders) / sizeof(s_rldMkReaders[0]))

// Files that are not models rldpack reads, by their first bytes: "a" or "an",
// and the name in the message. NULL = none of them.
static const char *RldMk_OtherFormat(const u8 *d, size_t size, const char **article)
{
	const size_t window = (size < RLDOBJ_DETECT_BYTES) ? size : RLDOBJ_DETECT_BYTES;
	size_t at = 0;

	*article = "a";
	if (((size >= 18u) && (memcmp(d, "Kaydara FBX Binary", 18) == 0)) || ((size >= 5u) && (memcmp(d, "; FBX", 5) == 0)))
	{
		*article = "an";
		return "FBX";
	}
	if ((size >= 7u) && (memcmp(d, "BLENDER", 7) == 0))
	{
		return "Blender";
	}
	if ((size >= 6u) && (d[0] == 0x4Du) && (d[1] == 0x4Du) && (Rld_ReadLE32(&d[2]) == (u32)size))
	{
		return "3DS";
	}
	if ((size >= 84u) && ((u64)Rld_ReadLE32(&d[80]) * 50u + 84u == (u64)size))
	{
		*article = "an";
		return "STL";
	}
	if ((size >= 8u) && (d[0] == 0x89u) && (memcmp(&d[1], "PNG", 3) == 0))
	{
		return "PNG";
	}
	if ((size >= 3u) && (d[0] == 0xFFu) && (d[1] == 0xD8u) && (d[2] == 0xFFu))
	{
		return "JPEG";
	}
	if ((size >= 4u) && (memcmp(d, "GIF8", 4) == 0))
	{
		return "GIF";
	}
	if ((size >= 6u) && (d[0] == 'B') && (d[1] == 'M') && (Rld_ReadLE32(&d[2]) == (u32)size))
	{
		return "BMP";
	}
	if ((size >= 4u) && (memcmp(d, "PK\x03\x04", 4) == 0))
	{
		return "ZIP";
	}
	if ((size >= 6u) && (memcmp(d, "7z\xBC\xAF\x27\x1C", 6) == 0))
	{
		return "7-Zip";
	}
	if ((size >= 4u) && (memcmp(d, "Rar!", 4) == 0))
	{
		return "RAR";
	}
	if ((size >= 2u) && (d[0] == 0x1Fu) && (d[1] == 0x8Bu))
	{
		return "gzip";
	}
	if ((size >= 4u) && (memcmp(d, "%PDF", 4) == 0))
	{
		return "PDF";
	}
	if ((size >= 2u) && (d[0] == 'M') && (d[1] == 'Z'))
	{
		return "Windows program";
	}
	if (((size >= 8u) && (memcmp(d, "PXR-USDC", 8) == 0)) || ((size >= 5u) && (memcmp(d, "#usda", 5) == 0)))
	{
		*article = "an";
		return "USD";
	}

	// Text formats: by their first word.
	if ((size >= 3u) && (d[0] == 0xEFu) && (d[1] == 0xBBu) && (d[2] == 0xBFu))
	{
		at = 3;
	}
	while ((at < window) && RldMk_IsSpace(d[at]))
	{
		at++;
	}
	if (((window - at) >= 6u) && (memcmp(&d[at], "solid", 5) == 0) && RldMk_IsSpace(d[at + 5u]))
	{
		*article = "an";
		return "STL";
	}
	if (((window - at) >= 5u) && (memcmp(&d[at], "<?xml", 5) == 0))
	{
		size_t i;

		for (i = at; (i + 7u) <= window; i++)
		{
			if (memcmp(&d[i], "COLLADA", 7) == 0)
			{
				return "COLLADA (.dae)";
			}
		}
	}
	return NULL;
}

// The extension of the file name: from its last dot, or "".
static const char *RldMk_Extension(const char *path)
{
	const char *name = Rld_LastComponent(path);
	const char *dot = strrchr(name, '.');

	return ((dot != NULL) && (dot != name)) ? dot : "";
}

static int RldMk_ReaderHasExtension(const struct RldMkReader *reader, const char *path)
{
	int i;

	for (i = 0; (i < 4) && (reader->extensions[i] != NULL); i++)
	{
		if (Rld_EndsWithNoCase(path, reader->extensions[i]))
		{
			return 1;
		}
	}
	return 0;
}

// What a file is: the reader its content takes, the reader its name
// points to, or the other format it is. Says nothing.
struct RldMkChoice
{
	const struct RldMkReader *reader;  // by the content, NULL = none
	const struct RldMkReader *named;   // by the name, NULL = none
	const char *other;                 // RldMk_OtherFormat, NULL = none
	const char *article;
};

static void RldMk_Detect(const char *path, const u8 *data, size_t size, struct RldMkChoice *c)
{
	u32 r;

	memset(c, 0, sizeof(*c));
	c->article = "a";
	for (r = 0; r < (u32)RLDMK_READER_COUNT; r++)
	{
		if ((c->reader == NULL) && s_rldMkReaders[r].detect(data, size))
		{
			c->reader = &s_rldMkReaders[r];
		}
		if ((c->named == NULL) && RldMk_ReaderHasExtension(&s_rldMkReaders[r], path))
		{
			c->named = &s_rldMkReaders[r];
		}
	}
	if (c->reader == NULL)
	{
		c->other = RldMk_OtherFormat(data, size, &c->article);
	}
}

// The reader that reads the file for `role`, or NULL - then it was said why
// (model-unsupported, model-unknown, model-ply-only). A file named for another
// format is read as what it is (model-misnamed).
static const struct RldMkReader *RldMk_Choose(const char *path, const struct RldMkChoice *c, u32 role)
{
	const char *file = Rld_LastComponent(path);
	const struct RldMkReader *reader = c->reader;

	if (reader == NULL)
	{
		// A file named .ply goes to the PLY reader, which says what is wrong
		// with it - as before there was a choice.
		if ((c->other == NULL) && (c->named != NULL) && (c->named->format == RLDMK_FORMAT_PLY))
		{
			reader = c->named;
		}
		else if (c->other != NULL)
		{
			RldMk_Say("error", "model-unsupported", c->other, "%s is %s %s file. rldpack and Reload Studio read glTF, OBJ and PLY models.", file, c->article,
			          c->other);
			return NULL;
		}
		else
		{
			RldMk_Say("error", "model-unknown", NULL,
			          "%s is not a model file Reload Studio can read. Export the model from Blender as glTF 2.0 (.glb), Wavefront OBJ or PLY.", file);
			return NULL;
		}
	}
	else if (c->named != reader)
	{
		const char *extension = RldMk_Extension(path);

		if (extension[0] != '\0')
		{
			RldMk_Say("warning", "model-misnamed", extension, "%s is named %s but is %s %s file; read as %s.", file, extension, reader->article, reader->name,
			          reader->name);
		}
		else
		{
			RldMk_Say("warning", "model-misnamed", NULL, "%s has no file extension but is %s %s file; read as %s.", file, reader->article, reader->name,
			          reader->name);
		}
	}

	if ((reader->roles & role) == 0u)
	{
		RldMk_Say("error", "model-ply-only", reader->name, "Poses are read from PLY only for now.");
		return NULL;
	}
	return reader;
}

// For a caller that reads the file itself (char-wheel, RldWheel_Build): may
// `role` read this file? 0 = no, said why. A PLY named .ply says nothing.
static int RldMk_ModelAllowed(const char *path, const u8 *data, size_t size, u32 role)
{
	struct RldMkChoice choice;

	RldMk_Detect(path, data, size, &choice);
	return RldMk_Choose(path, &choice, role) != NULL;
}

// A model in memory (`path` names it and finds its MTL files and textures)
// -> *ply. Says every error itself and returns 0 then; *ply is then freed.
// The @model lines only for the model and the own mask; the poses and the
// wheel keep their lines as they were.
static int RldMk_ImportModel(const char *path, const u8 *data, size_t size, struct RldMkPly *ply, struct RldMkModelInfo *info, u32 role)
{
	const int described = (role == RLDMK_ROLE_MAIN) || (role == RLDMK_ROLE_MASK);
	const struct RldMkReader *reader;
	struct RldMkChoice choice;
	char bytes[24];

	memset(ply, 0, sizeof(*ply));
	memset(info, 0, sizeof(*info));
	// The shape keys belong to the last model read for make-char or the poses.
	if ((role == RLDMK_ROLE_MAIN) || (role == RLDMK_ROLE_POSE))
	{
		RldGltf_FreeMorphs();
	}
	RldMk_Detect(path, data, size, &choice);

	// The file line: the format found, else the one of the name, else PLY.
	snprintf(bytes, sizeof(bytes), "%u", (u32)size);
	if (!s_rldMkQuiet)
	{
		const char *kind = (role == RLDMK_ROLE_MASK)         ? "mask-model"
		                   : (role == RLDMK_ROLE_CHAR_WHEEL) ? "wheel-model"
		                   : !described                      ? "ply"
		                   : (choice.reader != NULL)         ? choice.reader->word
		                   : (choice.named != NULL)          ? choice.named->word
		                                                     : "ply";

		Rld_Emit("file", kind, "ok", path, bytes, (const char *)NULL);
		if (described && (choice.reader != NULL))
		{
			RldMk_EmitModel("format", choice.reader->word, NULL, NULL, NULL);
		}
		if (role == RLDMK_ROLE_MASK)
		{
			printf("rldpack make-char --mask-model %s\n", path);
		}
		else if (role == RLDMK_ROLE_MAIN)
		{
			printf("rldpack make-char %s\n", path);
		}
		else if (role == RLDMK_ROLE_POSE)
		{
			printf("rldpack char-poses %s\n", path);
		}
		else if (role == RLDMK_ROLE_CHAR_WHEEL)
		{
			printf("rldpack make-char --wheel-model %s\n", path);
		}
	}

	reader = RldMk_Choose(path, &choice, role);
	if (reader == NULL)
	{
		return 0;
	}

	info->format = reader->format;
	info->role = role;
	if (!reader->read(path, data, size, ply, info))
	{
		RldMk_FreePly(ply);
		return 0;
	}

	if (described)
	{
		const u32 c = info->colors;
		const char *word = (c == RLDMK_COLORS_VERTEX)     ? "vertex"
		                   : (c == RLDMK_COLORS_MATERIAL) ? "material"
		                   : (c == RLDMK_COLORS_TEXTURE)  ? "texture"
		                   : (c == RLDMK_COLORS_GREY)     ? "grey"
		                                                  : "mixed";

		RldMk_EmitModel("colors", word, NULL, NULL, NULL);
	}
	return 1;
}

// Step 1 for every caller: the file read, its format found, read into *ply.
// Says every error itself and returns 0 then.
static int RldMk_LoadModel(const char *path, struct RldMkPly *ply, struct RldMkModelInfo *info, u32 role)
{
	u8 *data = NULL;
	size_t size = 0;
	int ok;

	memset(ply, 0, sizeof(*ply));
	memset(info, 0, sizeof(*info));

	if (!Rld_ReadFile(path, &data, &size))
	{
		const int named = (role == RLDMK_ROLE_MAIN) && (Rld_EndsWithNoCase(path, ".obj") || Rld_EndsWithNoCase(path, ".glb") ||
		                                                Rld_EndsWithNoCase(path, ".gltf"));
		const char *kind = (role == RLDMK_ROLE_MASK)                         ? "mask-model"
		                   : (role == RLDMK_ROLE_CHAR_WHEEL)                 ? "wheel-model"
		                   : (named && Rld_EndsWithNoCase(path, ".obj"))     ? "obj"
		                   : named                                           ? "gltf"
		                                                                     : "ply";

		if (s_rldReadTooLarge)
		{
			// Rld_ReadFile said why (file-too-large): no "cannot be opened"
			// and no @file missing - the file is there - but counted like
			// every error of make-char.
			s_rldMkErrors++;
		}
		else if (role == RLDMK_ROLE_MASK)
		{
			Rld_Emit("file", kind, "missing", path, "0", (const char *)NULL);
			RldMk_Say("error", "mask-open", path, "The mask model file %s cannot be opened.", path);
		}
		else if (named)
		{
			Rld_Emit("file", kind, "missing", path, "0", (const char *)NULL);
			RldMk_Say("error", "model-open", path, "The model file %s cannot be opened.", path);
		}
		else if (role == RLDMK_ROLE_CHAR_WHEEL)
		{
			Rld_Emit("file", kind, "missing", path, "0", (const char *)NULL);
			RldMk_Say("error", "wheel-open", path, "The wheel model file %s cannot be opened.", path);
		}
		else
		{
			Rld_Emit("file", kind, "missing", path, "0", (const char *)NULL);
			RldMk_Say("error", "ply-open", path, "The model file %s cannot be opened.", path);
		}
		return 0;
	}

	ok = RldMk_ImportModel(path, data, size, ply, info, role);
	free(data);
	return ok;
}
