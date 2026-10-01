// rldpack_char.inc - rldpack make-char: a character container (.rldchar) from a PLY model
//
// Part of the packer, never of the game. tools/rldpack.c includes this file after
// include/rldchar.inc (reader, CHRI parser and model check - the same lines the
// game runs) and after its own helpers that are used here: Rld_Emit, Rld_Say,
// Rld_ReadFile, Rld_IsUtf8, Rld_EndsWithNoCase, Rld_AddChunk, RLD_PATH_MAX,
// s_machine and s_machineResult. Rld_WriteEnvelope, Rld_OpenAs, Rld_ReadChunk
// and the SHA-256 come from include/rldtrack.inc.
//
// "RB" below is game/RenderBucket/RenderBucket_QueueExecute.c.
//
// THE CHAIN, one step after the other. Step 14, the portrait chunk CICN, and the
// values chunk CPRM are not written yet - both chunks are optional:
//
//    1  read the PLY: ascii 1.0 or binary_little_endian 1.0
//    2  faces -> triangles: quads split at the shorter diagonal, n-gons refused
//    3  weld the corners to positions (1e-5); the color stays per corner
//    4  parts: kart (required), driver, steering wheel
//    5  axes and scale -> game units: +Y up, +Z forward, 64 per Blender unit
//    6  draw memory per frame: 28 bytes per untextured triangle
//    7  colors -> a palette of 64 (or 128)
//    8  the 47 frames 21/7/15/4: still, or the automatic poses
//    9  quantization: one byte per axis, the up axis on even values
//   10  command list: strips and fans, the vertex cache, cull bits per triangle
//   11  raw frames, 12 model and header, 13 pointer map -> the CMDL chunk
//   15  the game's own check (RldChar_CheckModel), read back by the renderer's
//       rule, the cull side replayed per triangle, built twice
//   16  CHRI + CMDL -> .rldchar through the shared envelope
//
// DETERMINISM. The same input gives the same bytes: no time, no random numbers,
// every sort has a complete tie-break, and floating point only enters through
// fixed roundings (floor(x + 0.5)). Floor, square root, sine and cosine are
// written out below instead of taken from the C library, so that two runtimes
// cannot round differently.

//========================================================================================
// CONSTANTS
//========================================================================================

#define RLDMK_NONE 0xFFFFFFFFu

#define RLDMK_PLY_MAX 100000u     // vertices and faces each (ply-big): a guard, not a format limit
#define RLDMK_PLY_LIST_MAX 1024u  // entries of one list the reader accepts at all
#define RLDMK_TEMPLATE_COUNT 15   // 0..14, include/namespace_Vehicle.h:39-53; 15 (N. Oxide) is never one
#define RLDMK_CLASS_COUNT 4       // enum EngineClass, include/namespace_Vehicle.h:563-578
#define RLDMK_FRAMES_TOTAL 47     // 21 + 7 + 15 + 4 (s_rldCharFrames)
#define RLDMK_NEUTRAL_FRAME 10    // turn frame 10: no steering (game/Vehicle/VehFrame.c:36-38, n >> 1)
#define RLDMK_UNITS 64.0          // game units per Blender unit: the retail karts are 112 x 56 units, 1.8 x 0.9 m
#define RLDMK_HI_TRIANGLES_MAX 564u  // the most triangles of an original driver (N. Oxide, HI model)
#define RLDMK_GT3_BYTES 40u       // one textured triangle in draw memory, RB:3088-3090

// The layout of a model, written as bytes. struct Model, ModelHeader, ModelAnim
// and ModelFrame: include/namespace_Instance.h:304-421, offsets asserted in
// game/RenderBucket/RenderBucket_QueueExecute.c:47-70. Never through the game's
// structs: this program may be 64-bit, the pointers in the file are 32-bit
// offsets into the body.
#define RLDMK_MODEL_BYTES 0x18u
#define RLDMK_MODEL_ID 0x10u
#define RLDMK_MODEL_NUM_HEADERS 0x12u
#define RLDMK_MODEL_HEADERS 0x14u
#define RLDMK_HEADER_BYTES 0x40u
#define RLDMK_HEADER_LOD 0x14u
#define RLDMK_HEADER_FLAGS 0x16u
#define RLDMK_HEADER_SCALE 0x18u
#define RLDMK_HEADER_COMMANDS 0x20u
#define RLDMK_HEADER_FRAME_DATA 0x24u
#define RLDMK_HEADER_COLORS 0x2Cu
#define RLDMK_HEADER_NUM_ANIMS 0x34u
#define RLDMK_HEADER_ANIMS 0x38u
#define RLDMK_ANIM_BYTES 0x18u
#define RLDMK_ANIM_NUM_FRAMES 0x10u
#define RLDMK_ANIM_FRAME_SIZE 0x12u
#define RLDMK_ANIM_DELTA 0x14u
#define RLDMK_FRAME_HEADER 0x1Cu          // ModelFrame: pos, pad, unk16[16], vertexOffset
#define RLDMK_FRAME_VERTEX_OFFSET 0x18u
#define RLDMK_RECORD_BYTES 3u             // X, Z (forward), Y (up): RB:2437-2467, 2620-2626

// Where make-char puts each part of the body. Every part starts on a multiple
// of 4: pointer fields can only be listed in the map at such offsets.
#define RLDMK_BODY_HEADER RLDMK_MODEL_BYTES                                  // 0x18
#define RLDMK_BODY_ANIM_TABLE (RLDMK_MODEL_BYTES + RLDMK_HEADER_BYTES)       // 0x58
#define RLDMK_BODY_COMMANDS (RLDMK_BODY_ANIM_TABLE + 4u * RLDCHAR_ANIM_COUNT) // 0x68
#define RLDMK_MAP_ENTRIES 8u    // headers, ptrCommandList, ptrColors, ptrAnimations, 4 x ModelAnim*

// The internal model name: fixed, no author field, at most 12 characters (model-head).
// The header name is never read by the game.
#define RLDMK_MODEL_NAME_TEXT "rldchar"
#define RLDMK_HEADER_NAME_TEXT "rldchar_hi"

// THE COMMAND WORD (RB:4598-4600, 2884-2898, 2520-2531, 2588-2596).
#define RLDMK_CMD_STRIP 0x80000000u    // a new strip starts, RB:4635-4644
#define RLDMK_CMD_FAN 0x40000000u      // keep the first corner (fan), RB:4646-4654, 2826-2838
#define RLDMK_CMD_SIDE 0x20000000u     // cull side, XOR the instance flag 0x8000, RB:2888-2899
#define RLDMK_CMD_CULL 0x10000000u     // back face test (NCLIP) on, RB:2884-2903
#define RLDMK_CMD_SCRATCH 0x08000000u  // color from the scratch color cache, RB:2525-2527
#define RLDMK_CMD_CACHED 0x04000000u   // corner from its cache slot, no new record, RB:2588-2596, 4616-4619
#define RLDMK_CMD_END 0xFFFFFFFFu      // RB:4596
#define RLDMK_SLOT_SHIFT 16            // bits 23-16
#define RLDMK_COLOR_SHIFT 9            // bits 15-9

// THE CULL SIDE. The renderer draws a triangle with bit 0x10 only when the sign
// of NCLIP fits bit 0x20 (XOR the instance flag REVERSE_CULL_DIRECTION, which no
// kart carries, include/namespace_Instance.h:42): with 0x20 clear only NCLIP > 0,
// with 0x20 set only NCLIP < 0 (RB:2884-2903). Which sign a viewer on the side
// of the right-hand normal sees is fixed by the whole chain model -> screen.
//
// Measured on the 16 retail HI driver models (only this one bit of it is kept
// here): with the corners in the order the GTE window gives them (RB:4630-4672)
// and turned round wherever 0x20 is set, every model is one consistently wound
// surface whose signed volume is negative. Retail draws the outside, so a
// viewer on the side of the right-hand normal of the window order sees NCLIP < 0.
// RLDMK_CULL_SIGN 1 therefore means: a triangle whose window order winds like its
// source face (right-hand normal = front, as Blender writes it) gets bit 0x20.
// make-char sets 0x20 = (window order reversed against the source) XOR 1.
#define RLDMK_CULL_SIGN 1u

// The automatic poses turn the driver about the hip: 0.25 Blender units above
// the ground, on the center line (16 game units).
#define RLDMK_HIP_Y 16.0

// Kart detection (ply-no-kart) and the plausibility of the size (ply-axes), in
// Blender units: a retail kart is 112 x 56 units with its bottom at 6, so
// 1.75 x 0.875 m, 0.09 above the ground.
#define RLDMK_KART_BOTTOM_MIN (-0.15)
#define RLDMK_KART_BOTTOM_MAX 0.3
#define RLDMK_KART_LENGTH_MIN 1.25
#define RLDMK_KART_LENGTH_MAX 2.5
#define RLDMK_DRIVER_LENGTH 1.8
#define RLDMK_WHEEL_EXTENT 0.6    // the steering wheel: a small part in front of the driver

struct RldMkTemplate
{
	const char *name;
	u32 classId;
	u32 hiTriangles;
};

// The 15 template drivers. classId is MetaDataCharacters[id].engineID
// (game/zGlobal_DATA.c:5231-5350): 0 balanced, 1 acceleration, 2 speed,
// 3 turning (include/namespace_Vehicle.h:563-578). hiTriangles counts the
// triangles of the retail HI model, measured once on the disc - only the
// numbers are here. A custom driver replaces that model, so it costs
// draw_bytes - 40 x hiTriangles more per frame (the retail model is textured).
static const struct RldMkTemplate s_rldMkTemplates[RLDMK_TEMPLATE_COUNT] = {
	{"Crash Bandicoot", 0u, 272u}, {"Dr. Neo Cortex", 0u, 257u}, {"Tiny Tiger", 2u, 265u},
	{"Coco Bandicoot", 1u, 269u},  {"N. Gin", 1u, 256u},         {"Dingodile", 2u, 273u},
	{"Polar", 3u, 243u},           {"Pura", 3u, 261u},           {"Pinstripe", 1u, 260u},
	{"Papu Papu", 2u, 250u},       {"Ripper Roo", 3u, 254u},     {"Komodo Joe", 0u, 259u},
	{"N. Tropy", 2u, 269u},        {"Penta Penguin", 3u, 199u},  {"Fake Crash", 0u, 267u},
};

static const char *const s_rldMkClassNames[RLDMK_CLASS_COUNT] = {"balanced", "acceleration", "speed", "turning"};

// ModelAnim.name - read by no code, written for whoever looks at the bytes.
static const char *const s_rldMkAnimNames[RLDCHAR_ANIM_COUNT] = {"turn", "reverse", "bump", "jump"};

// The help is at the end of the file; a wrong switch already needs it.
static void RldChar_Usage(FILE *out);

//========================================================================================
// MESSAGES
//========================================================================================
//
// Every check speaks twice: a line on stderr for people and @msg for
// Reload Studio (Rld_Say), both from the same text, so that they cannot drift
// apart. The self-test runs the same checks quietly and only asks which codes
// were said.

static int s_rldMkQuiet;
static int s_rldMkErrors;
static char s_rldMkCodes[2048];

static void RldMk_ResetSaid(void)
{
	s_rldMkErrors = 0;
	s_rldMkCodes[0] = '\0';
}

static void RldMk_Say(const char *severity, const char *code, const char *technical, const char *format, ...)
{
	char text[1024];
	va_list list;
	size_t used = strlen(s_rldMkCodes);

	if (strcmp(severity, "error") == 0)
	{
		s_rldMkErrors++;
	}

	if ((used + strlen(code) + 2u) < sizeof(s_rldMkCodes))
	{
		snprintf(&s_rldMkCodes[used], sizeof(s_rldMkCodes) - used, "%s%s", (used != 0u) ? " " : "", code);
	}

	if (s_rldMkQuiet)
	{
		return;
	}

	va_start(list, format);
	vsnprintf(text, sizeof(text), format, list);
	va_end(list);

	if (strcmp(severity, "error") == 0)
	{
		fprintf(stderr, "rldpack: %s\n", text);
	}
	else if (strcmp(severity, "warning") == 0)
	{
		fprintf(stderr, "WARNING: %s\n", text);
	}
	else
	{
		fprintf(stderr, "%s: %s\n", severity, text);
	}

	if ((technical != NULL) && (technical[0] != '\0'))
	{
		fprintf(stderr, "         (%s: %s)\n", code, technical);
	}

	Rld_Say(severity, code, technical, "%s", text);
}

// Was this code said since the last RldMk_ResetSaid? For the self-test.
static int RldMk_Said(const char *code)
{
	const size_t length = strlen(code);
	const char *p = s_rldMkCodes;

	while (*p != '\0')
	{
		if ((strncmp(p, code, length) == 0) && ((p[length] == ' ') || (p[length] == '\0')))
		{
			return 1;
		}

		p = strchr(p, ' ');
		if (p == NULL)
		{
			return 0;
		}
		p++;
	}

	return 0;
}

// @char <fact> <value> - only Reload Studio reads these; the file carries no
// such numbers.
static void RldMk_Fact(const char *key, const char *format, ...)
{
	char text[256];
	va_list list;

	if (!s_machine || s_rldMkQuiet)
	{
		return;
	}

	va_start(list, format);
	vsnprintf(text, sizeof(text), format, list);
	va_end(list);
	Rld_Emit("char", key, text, (const char *)NULL);
}

//========================================================================================
// ARITHMETIC THAT ROUNDS THE SAME EVERYWHERE
//========================================================================================

static double RldMk_Floor(double value)
{
	s64 whole;

	// NaN, the infinities and huge values have no fraction left to cut off.
	if (!(value > -9.0e15) || !(value < 9.0e15))
	{
		return value;
	}

	whole = (s64)value;
	if ((double)whole > value)
	{
		whole--;
	}

	return (double)whole;
}

static double RldMk_Ceil(double value)
{
	return -RldMk_Floor(-value);
}

// The one rounding of the chain: halves go up.
static s64 RldMk_Round(double value)
{
	return (s64)RldMk_Floor(value + 0.5);
}

// Newton's method from above; only for shown numbers (color error).
static double RldMk_Sqrt(double value)
{
	double x;
	int i;

	if (!(value > 0.0))
	{
		return 0.0;
	}

	x = (value > 1.0) ? value : 1.0;
	for (i = 0; i < 80; i++)
	{
		x = 0.5 * (x + value / x);
	}

	return x;
}

// Taylor series; the poses use angles below 25 degrees, where 13 terms are far
// below the precision of a double.
static void RldMk_SinCos(double degrees, double *sine, double *cosine)
{
	const double x = degrees * (3.14159265358979323846 / 180.0);
	const double x2 = x * x;
	double term = x;
	double s = 0.0;
	double c = 0.0;
	int k;

	for (k = 1; k <= 25; k += 2)
	{
		s += term;
		term = -term * x2 / (double)((k + 1) * (k + 2));
	}

	term = 1.0;
	for (k = 0; k <= 24; k += 2)
	{
		c += term;
		term = -term * x2 / (double)((k + 1) * (k + 2));
	}

	*sine = s;
	*cosine = c;
}

static void RldMk_Cross(const double *a, const double *b, const double *c, double out[3])
{
	const double u0 = b[0] - a[0];
	const double u1 = b[1] - a[1];
	const double u2 = b[2] - a[2];
	const double v0 = c[0] - a[0];
	const double v1 = c[1] - a[1];
	const double v2 = c[2] - a[2];

	out[0] = u1 * v2 - u2 * v1;
	out[1] = u2 * v0 - u0 * v2;
	out[2] = u0 * v1 - u1 * v0;
}

static double RldMk_Dot(const double *a, const double *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double RldMk_Distance2(const double *a, const double *b)
{
	const double d0 = a[0] - b[0];
	const double d1 = a[1] - b[1];
	const double d2 = a[2] - b[2];

	return d0 * d0 + d1 * d1 + d2 * d2;
}

static int RldMk_CompareU32(const void *a, const void *b)
{
	const u32 x = *(const u32 *)a;
	const u32 y = *(const u32 *)b;

	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

static int RldMk_CompareU64(const void *a, const void *b)
{
	const u64 x = *(const u64 *)a;
	const u64 y = *(const u64 *)b;

	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

//========================================================================================
// STEP 1: READING THE PLY
//========================================================================================
//
// format ascii 1.0 and binary_little_endian 1.0. Kept are x/y/z, the vertex
// colors and the corner lists; everything else (normals, s/t, foreign elements
// and properties) is read past. Blender writes "vertex_indices", other tools
// "vertex_index" - both are taken.

enum
{
	RLDMK_PLY_NONE = 0,
	RLDMK_PLY_INT8,
	RLDMK_PLY_UINT8,
	RLDMK_PLY_INT16,
	RLDMK_PLY_UINT16,
	RLDMK_PLY_INT32,
	RLDMK_PLY_UINT32,
	RLDMK_PLY_FLOAT32,
	RLDMK_PLY_FLOAT64
};

#define RLDMK_PLY_ELEMENTS 8
#define RLDMK_PLY_PROPERTIES 24
#define RLDMK_PLY_WORD 32
#define RLDMK_PLY_WORDS 8
#define RLDMK_PLY_HEADER_MAX (64u * 1024u)

struct RldMkPlyProperty
{
	int type;       // of the value, or of the entries of a list
	int countType;  // RLDMK_PLY_NONE for a single value
	char name[RLDMK_PLY_WORD];
};

struct RldMkPlyElement
{
	char name[RLDMK_PLY_WORD];
	u32 count;
	int propertyCount;
	struct RldMkPlyProperty properties[RLDMK_PLY_PROPERTIES];
};

struct RldMkPly
{
	u32 vertexCount;
	u32 faceCount;
	double *xyz;       // 3 per vertex, as written
	u8 *rgb;           // 3 per vertex; grey when the file has no colors
	u32 *corners;      // 4 per face: the first four corners
	u32 *cornerCount;  // per face, as written
	int binary;
	int hasColors;
	int hasUv;
	u32 alphaBelow;    // vertices whose alpha is below 255
};

static void RldMk_FreePly(struct RldMkPly *ply)
{
	free(ply->xyz);
	free(ply->rgb);
	free(ply->corners);
	free(ply->cornerCount);
	memset(ply, 0, sizeof(*ply));
}

static int RldMk_PlyType(const char *word)
{
	static const char *const names[16] = {"char", "int8", "uchar", "uint8", "short", "int16", "ushort", "uint16",
	                                      "int", "int32", "uint", "uint32", "float", "float32", "double", "float64"};
	int i;

	for (i = 0; i < 16; i++)
	{
		if (strcmp(word, names[i]) == 0)
		{
			return RLDMK_PLY_INT8 + i / 2;
		}
	}

	return RLDMK_PLY_NONE;
}

static u32 RldMk_PlyTypeSize(int type)
{
	if ((type == RLDMK_PLY_INT8) || (type == RLDMK_PLY_UINT8))
	{
		return 1u;
	}
	if ((type == RLDMK_PLY_INT16) || (type == RLDMK_PLY_UINT16))
	{
		return 2u;
	}
	if (type == RLDMK_PLY_FLOAT64)
	{
		return 8u;
	}
	return 4u;
}

static int RldMk_IsSpace(u8 c)
{
	return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') || (c == '\v') || (c == '\f');
}

// Splits one header line into words; longer words are cut (they then match
// nothing). Returns the number of words.
static int RldMk_PlyWords(const u8 *line, size_t length, char words[RLDMK_PLY_WORDS][RLDMK_PLY_WORD])
{
	size_t at = 0;
	int count = 0;

	while (at < length)
	{
		size_t used = 0;

		while ((at < length) && RldMk_IsSpace(line[at]))
		{
			at++;
		}
		if (at >= length)
		{
			break;
		}

		while ((at < length) && !RldMk_IsSpace(line[at]))
		{
			if ((count < RLDMK_PLY_WORDS) && (used < (RLDMK_PLY_WORD - 1u)))
			{
				words[count][used++] = (char)line[at];
			}
			at++;
		}

		if (count < RLDMK_PLY_WORDS)
		{
			words[count][used] = '\0';
		}
		count++;
	}

	return count;
}

struct RldMkPlyCursor
{
	const u8 *data;
	size_t size;
	size_t at;
	int binary;
};

// One value. 1 = read, 0 = the file ended, -1 = not a number.
static int RldMk_PlyValue(struct RldMkPlyCursor *cursor, int type, double *out)
{
	if (cursor->binary)
	{
		const u32 bytes = RldMk_PlyTypeSize(type);
		const u8 *p;
		u32 word;

		if ((cursor->size - cursor->at) < (size_t)bytes)
		{
			return 0;
		}

		p = &cursor->data[cursor->at];
		cursor->at += bytes;

		if (type == RLDMK_PLY_INT8)
		{
			*out = (double)(signed char)p[0];
		}
		else if (type == RLDMK_PLY_UINT8)
		{
			*out = (double)p[0];
		}
		else if (type == RLDMK_PLY_INT16)
		{
			word = Rld_ReadLE16(p);
			*out = (word >= 0x8000u) ? ((double)word - 65536.0) : (double)word;
		}
		else if (type == RLDMK_PLY_UINT16)
		{
			*out = (double)Rld_ReadLE16(p);
		}
		else if (type == RLDMK_PLY_INT32)
		{
			word = Rld_ReadLE32(p);
			*out = (word >= 0x80000000u) ? ((double)word - 4294967296.0) : (double)word;
		}
		else if (type == RLDMK_PLY_UINT32)
		{
			*out = (double)Rld_ReadLE32(p);
		}
		else if (type == RLDMK_PLY_FLOAT32)
		{
			float value;

			word = Rld_ReadLE32(p);
			memcpy(&value, &word, sizeof(value));
			*out = (double)value;
		}
		else
		{
			u64 wide = Rld_ReadLE64(p);
			double value;

			memcpy(&value, &wide, sizeof(value));
			*out = value;
		}

		return 1;
	}

	{
		char token[64];
		size_t length = 0;
		char *end = NULL;

		while ((cursor->at < cursor->size) && RldMk_IsSpace(cursor->data[cursor->at]))
		{
			cursor->at++;
		}
		if (cursor->at >= cursor->size)
		{
			return 0;
		}

		while ((cursor->at < cursor->size) && !RldMk_IsSpace(cursor->data[cursor->at]))
		{
			if (length >= (sizeof(token) - 1u))
			{
				return -1;
			}
			token[length++] = (char)cursor->data[cursor->at];
			cursor->at++;
		}
		token[length] = '\0';

		*out = strtod(token, &end);
		if ((end == token) || (*end != '\0'))
		{
			return -1;
		}

		return 1;
	}
}

// A color value: uchar as it is, float 0..1 times 255, everything else clamped.
static u8 RldMk_PlyColor(int type, double value)
{
	double scaled = value;

	if ((type == RLDMK_PLY_FLOAT32) || (type == RLDMK_PLY_FLOAT64))
	{
		scaled = RldMk_Floor(value * 255.0 + 0.5);
	}

	if (!(scaled > 0.0))
	{
		return 0;
	}
	if (scaled >= 255.0)
	{
		return 255;
	}

	return (u8)scaled;
}

static void RldMk_PlyFormatError(const char *technical)
{
	RldMk_Say("error", "ply-format", technical, "This is not a PLY file rldpack can read. Export it from Blender as PLY (ASCII or binary).");
}

static void RldMk_PlyValueError(int got)
{
	if (got == 0)
	{
		RldMk_Say("error", "ply-truncated", NULL, "The model file ends early - it was not saved completely.");
	}
	else
	{
		RldMk_PlyFormatError("a value in the data is not a number");
	}
}

static int RldMk_PlyFind(const struct RldMkPlyElement *element, const char *name, int list)
{
	int i;

	for (i = 0; i < element->propertyCount; i++)
	{
		if ((strcmp(element->properties[i].name, name) == 0) && ((element->properties[i].countType != RLDMK_PLY_NONE) == (list != 0)))
		{
			return i;
		}
	}

	return -1;
}

// Step 1. Says every error itself (ply-*) and returns 0 then.
static int RldMk_ParsePly(const u8 *data, size_t size, struct RldMkPly *ply)
{
	static struct RldMkPlyElement elements[RLDMK_PLY_ELEMENTS];
	char words[RLDMK_PLY_WORDS][RLDMK_PLY_WORD];
	struct RldMkPlyCursor cursor;
	size_t at = 0;
	int elementCount = 0;
	int format = 0; // 1 ascii, 2 binary little endian
	int ended = 0;
	int vertexElement = -1;
	int faceElement = -1;
	int faceList = -1;
	int px, py, pz, pr, pg, pb, pa;
	int e;

	memset(ply, 0, sizeof(*ply));
	memset(elements, 0, sizeof(elements));

	if ((size < 4u) || (memcmp(data, "ply", 3) != 0) || ((data[3] != '\n') && (data[3] != '\r')))
	{
		RldMk_PlyFormatError("the file does not start with the line \"ply\"");
		return 0;
	}

	while (!ended)
	{
		const size_t start = at;
		size_t end;
		int n;

		if ((at >= size) || (at > RLDMK_PLY_HEADER_MAX))
		{
			RldMk_PlyFormatError("the header has no end_header line");
			return 0;
		}

		while ((at < size) && (data[at] != '\n'))
		{
			at++;
		}
		end = at;
		if (at < size)
		{
			at++;
		}

		n = RldMk_PlyWords(&data[start], end - start, words);
		if (n == 0)
		{
			continue;
		}

		if ((strcmp(words[0], "ply") == 0) || (strcmp(words[0], "comment") == 0) || (strcmp(words[0], "obj_info") == 0))
		{
			continue;
		}

		if (strcmp(words[0], "format") == 0)
		{
			if ((n == 3) && (strcmp(words[1], "binary_big_endian") == 0))
			{
				RldMk_PlyFormatError("binary_big_endian is not read - export as ASCII or binary little endian");
				return 0;
			}
			if ((n != 3) || (strcmp(words[2], "1.0") != 0))
			{
				RldMk_PlyFormatError("the format line is not \"format ascii 1.0\" or \"format binary_little_endian 1.0\"");
				return 0;
			}
			if (strcmp(words[1], "ascii") == 0)
			{
				format = 1;
			}
			else if (strcmp(words[1], "binary_little_endian") == 0)
			{
				format = 2;
			}
			else
			{
				RldMk_PlyFormatError("the format line is not \"format ascii 1.0\" or \"format binary_little_endian 1.0\"");
				return 0;
			}
			continue;
		}

		if (strcmp(words[0], "element") == 0)
		{
			struct RldMkPlyElement *element;
			char *countEnd = NULL;
			unsigned long count;

			if ((n != 3) || (elementCount >= RLDMK_PLY_ELEMENTS))
			{
				RldMk_PlyFormatError("an element line is not \"element <name> <count>\", or there are too many elements");
				return 0;
			}

			count = strtoul(words[2], &countEnd, 10);
			if ((words[2][0] < '0') || (words[2][0] > '9') || (*countEnd != '\0') || (count > 0xFFFFFFFFuL))
			{
				RldMk_PlyFormatError("an element count is not a number");
				return 0;
			}

			element = &elements[elementCount++];
			snprintf(element->name, sizeof(element->name), "%s", words[1]);
			element->count = (u32)count;
			continue;
		}

		if (strcmp(words[0], "property") == 0)
		{
			struct RldMkPlyElement *element;
			struct RldMkPlyProperty *property;

			if ((elementCount == 0) || (elements[elementCount - 1].propertyCount >= RLDMK_PLY_PROPERTIES))
			{
				RldMk_PlyFormatError("a property line outside an element, or too many properties");
				return 0;
			}

			element = &elements[elementCount - 1];
			property = &element->properties[element->propertyCount];

			if ((n == 3) && (strcmp(words[1], "list") != 0))
			{
				property->type = RldMk_PlyType(words[1]);
				property->countType = RLDMK_PLY_NONE;
				snprintf(property->name, sizeof(property->name), "%s", words[2]);
			}
			else if ((n == 5) && (strcmp(words[1], "list") == 0))
			{
				property->countType = RldMk_PlyType(words[2]);
				property->type = RldMk_PlyType(words[3]);
				snprintf(property->name, sizeof(property->name), "%s", words[4]);

				if (property->countType == RLDMK_PLY_NONE)
				{
					RldMk_PlyFormatError("a list property has an unknown count type");
					return 0;
				}
			}
			else
			{
				RldMk_PlyFormatError("a property line is not \"property <type> <name>\" or \"property list <type> <type> <name>\"");
				return 0;
			}

			if (property->type == RLDMK_PLY_NONE)
			{
				RldMk_PlyFormatError("a property has an unknown type");
				return 0;
			}

			element->propertyCount++;
			continue;
		}

		if (strcmp(words[0], "end_header") == 0)
		{
			ended = 1;
			continue;
		}

		RldMk_PlyFormatError("the header has a line that is not part of PLY");
		return 0;
	}

	if (format == 0)
	{
		RldMk_PlyFormatError("the header has no format line");
		return 0;
	}

	for (e = 0; e < elementCount; e++)
	{
		if ((vertexElement < 0) && (strcmp(elements[e].name, "vertex") == 0))
		{
			vertexElement = e;
		}
		if ((faceElement < 0) && (strcmp(elements[e].name, "face") == 0))
		{
			faceElement = e;
		}
	}

	if (vertexElement < 0)
	{
		RldMk_Say("error", "ply-position", "no vertex element", "The model has no vertex positions (x, y, z).");
		return 0;
	}

	px = RldMk_PlyFind(&elements[vertexElement], "x", 0);
	py = RldMk_PlyFind(&elements[vertexElement], "y", 0);
	pz = RldMk_PlyFind(&elements[vertexElement], "z", 0);
	if ((px < 0) || (py < 0) || (pz < 0))
	{
		RldMk_Say("error", "ply-position", NULL, "The model has no vertex positions (x, y, z).");
		return 0;
	}

	pr = RldMk_PlyFind(&elements[vertexElement], "red", 0);
	pg = RldMk_PlyFind(&elements[vertexElement], "green", 0);
	pb = RldMk_PlyFind(&elements[vertexElement], "blue", 0);
	pa = RldMk_PlyFind(&elements[vertexElement], "alpha", 0);
	ply->hasColors = (pr >= 0) && (pg >= 0) && (pb >= 0);
	ply->hasUv = (RldMk_PlyFind(&elements[vertexElement], "s", 0) >= 0) || (RldMk_PlyFind(&elements[vertexElement], "t", 0) >= 0) ||
	             (RldMk_PlyFind(&elements[vertexElement], "u", 0) >= 0) || (RldMk_PlyFind(&elements[vertexElement], "v", 0) >= 0) ||
	             (RldMk_PlyFind(&elements[vertexElement], "texture_u", 0) >= 0) ||
	             (RldMk_PlyFind(&elements[vertexElement], "texture_v", 0) >= 0);

	if (faceElement >= 0)
	{
		faceList = RldMk_PlyFind(&elements[faceElement], "vertex_indices", 1);
		if (faceList < 0)
		{
			faceList = RldMk_PlyFind(&elements[faceElement], "vertex_index", 1);
		}
	}

	if ((faceElement < 0) || (faceList < 0) || (elements[faceElement].count == 0u))
	{
		RldMk_Say("error", "ply-no-faces", NULL, "The model has no faces. Export the mesh, not only its points.");
		return 0;
	}

	ply->vertexCount = elements[vertexElement].count;
	ply->faceCount = elements[faceElement].count;
	ply->binary = (format == 2);

	if (ply->vertexCount > RLDMK_PLY_MAX)
	{
		RldMk_Say("error", "ply-big", NULL, "The model has %u vertices; rldpack reads at most %u. Reduce it in Blender (Decimate modifier).",
		          ply->vertexCount, RLDMK_PLY_MAX);
		return 0;
	}
	if (ply->faceCount > RLDMK_PLY_MAX)
	{
		RldMk_Say("error", "ply-big", NULL, "The model has %u faces; rldpack reads at most %u. Reduce it in Blender (Decimate modifier).",
		          ply->faceCount, RLDMK_PLY_MAX);
		return 0;
	}

	ply->xyz = (double *)calloc((size_t)ply->vertexCount * 3u + 1u, sizeof(double));
	ply->rgb = (u8 *)malloc((size_t)ply->vertexCount * 3u + 1u);
	ply->corners = (u32 *)calloc((size_t)ply->faceCount * 4u + 1u, sizeof(u32));
	ply->cornerCount = (u32 *)calloc((size_t)ply->faceCount + 1u, sizeof(u32));
	if ((ply->xyz == NULL) || (ply->rgb == NULL) || (ply->corners == NULL) || (ply->cornerCount == NULL))
	{
		RldMk_Say("error", "memory", NULL, "rldpack ran out of memory while reading the model.");
		RldMk_FreePly(ply);
		return 0;
	}
	memset(ply->rgb, 0x80, (size_t)ply->vertexCount * 3u + 1u);

	cursor.data = data;
	cursor.size = size;
	cursor.at = at;
	cursor.binary = ply->binary;

	for (e = 0; e < elementCount; e++)
	{
		const struct RldMkPlyElement *element = &elements[e];
		const int isVertex = (e == vertexElement);
		const int isFace = (e == faceElement);
		u32 i;

		for (i = 0; i < element->count; i++)
		{
			int p;

			for (p = 0; p < element->propertyCount; p++)
			{
				const struct RldMkPlyProperty *property = &element->properties[p];
				double value = 0.0;
				int got;

				if (property->countType != RLDMK_PLY_NONE)
				{
					const int isCorners = isFace && (p == faceList);
					double countValue = 0.0;
					u32 count;
					u32 k;

					got = RldMk_PlyValue(&cursor, property->countType, &countValue);
					if (got <= 0)
					{
						RldMk_PlyValueError(got);
						RldMk_FreePly(ply);
						return 0;
					}
					if ((countValue < 0.0) || (countValue > (double)RLDMK_PLY_LIST_MAX) || (countValue != (double)(u32)countValue))
					{
						RldMk_PlyFormatError("a list has an impossible number of entries");
						RldMk_FreePly(ply);
						return 0;
					}

					count = (u32)countValue;
					if (isCorners)
					{
						ply->cornerCount[i] = count;
					}

					for (k = 0; k < count; k++)
					{
						got = RldMk_PlyValue(&cursor, property->type, &value);
						if (got <= 0)
						{
							RldMk_PlyValueError(got);
							RldMk_FreePly(ply);
							return 0;
						}

						if (isCorners)
						{
							if ((value < 0.0) || (value >= (double)ply->vertexCount) || (value != (double)(u32)value))
							{
								char technical[64];

								snprintf(technical, sizeof(technical), "index %.0f", value);
								RldMk_Say("error", "ply-index", technical, "Face %u points to vertex %u, but the model has only %u vertices.", i,
								          (value >= 0.0) && (value < 4294967295.0) ? (u32)value : 0u, ply->vertexCount);
								RldMk_FreePly(ply);
								return 0;
							}
							if (k < 4u)
							{
								ply->corners[4u * i + k] = (u32)value;
							}
						}
					}
					continue;
				}

				got = RldMk_PlyValue(&cursor, property->type, &value);
				if (got <= 0)
				{
					RldMk_PlyValueError(got);
					RldMk_FreePly(ply);
					return 0;
				}

				if (isVertex)
				{
					if (p == px)
					{
						ply->xyz[3u * i + 0u] = value;
					}
					else if (p == py)
					{
						ply->xyz[3u * i + 1u] = value;
					}
					else if (p == pz)
					{
						ply->xyz[3u * i + 2u] = value;
					}
					else if (ply->hasColors && (p == pr))
					{
						ply->rgb[3u * i + 0u] = RldMk_PlyColor(property->type, value);
					}
					else if (ply->hasColors && (p == pg))
					{
						ply->rgb[3u * i + 1u] = RldMk_PlyColor(property->type, value);
					}
					else if (ply->hasColors && (p == pb))
					{
						ply->rgb[3u * i + 2u] = RldMk_PlyColor(property->type, value);
					}
					else if ((p == pa) && (RldMk_PlyColor(property->type, value) < 255u))
					{
						ply->alphaBelow++;
					}
				}
			}

			if (isVertex)
			{
				int a;

				for (a = 0; a < 3; a++)
				{
					const double v = ply->xyz[3u * i + (u32)a];

					// NaN fails both comparisons.
					if (!(v > -1.0e6) || !(v < 1.0e6))
					{
						char technical[64];

						snprintf(technical, sizeof(technical), "vertex %u has a position that is not a usable number", i);
						RldMk_PlyFormatError(technical);
						RldMk_FreePly(ply);
						return 0;
					}
				}
			}
		}
	}

	return 1;
}

//========================================================================================
// THE MODEL IN THE MAKING
//========================================================================================

#define RLDMK_GROUP_KART 0
#define RLDMK_GROUP_DRIVER 1
#define RLDMK_GROUP_WHEEL 2
#define RLDMK_GROUPS 3

static const char *const s_rldMkGroupNames[RLDMK_GROUPS] = {"kart", "driver", "wheel"};

struct RldMkOptions
{
	u32 colorTarget;  // 64 or 128
	int upZ;          // --up z: the PLY has +Z up (Blender's own axes), its -Y side is the front
	int backwards;    // --forward -z: the model faces -Z and is turned by 180 degrees
	double scale;     // --scale
	int twoSided;     // --two-sided: no back face culling
	int still;        // --poses still
};

struct RldMkPart
{
	u32 triangles;
	double lo[3];
	double hi[3];
	int group;
};

struct RldMkBuild
{
	// the input as read
	u32 vertexCount;
	u32 faceCount;
	u32 quads;
	u32 degenerate;

	// welded positions (game units, neutral pose); only those a triangle uses
	u32 positionCount;
	double *position;          // 3 per position
	u32 *vertexPosition;       // per PLY vertex, RLDMK_NONE when no triangle uses it

	// triangles: three PLY vertices each, in the order of the source face
	u32 triangleCount;
	u32 *triangle;
	u32 openEdges;

	// parts
	u32 partCount;
	struct RldMkPart *parts;
	u32 *positionPart;
	u8 *positionGroup;
	int kartPart;
	int driverPart;
	u32 groupTriangles[RLDMK_GROUPS];
	int posed;                 // the automatic poses were made
	double size[4];            // length, width (kart), height, bottom - Blender units

	// the 47 frames: RLDMK_FRAMES_TOTAL x positionCount x 3, game units
	double *frame;
	int scale[3];
	int origin[3];             // ModelFrame.pos, the same in every frame
	u8 *quant;                 // RLDMK_FRAMES_TOTAL x positionCount x 3: X, Z, Y as stored
	u32 collapsed;

	// colors
	u32 colorsIn;
	u32 colorCount;
	u32 palette[RLDCHAR_COLORS_MAX];  // 0x00BBGGRR, the code byte 0
	u8 *cornerColor;                  // palette index, 3 per triangle
	double colorMean;
	double colorMax;

	// the command list
	u32 slotLow;
	u32 slotCount;
	u32 commandCount;
	u32 *command;
	u32 recordCount;
	u32 *record;               // the position of each record
	u32 drawCount;
	u32 *drawTriangle;         // per triangle in draw order: index into triangle[]
	u32 *drawOrder;            // 3 per triangle in draw order: positions in the order of the GTE window
	u32 strips;
	u32 fanSteps;

	// the result
	u8 *cmdl;
	size_t cmdlSize;
	u32 frameSize;
	u32 drawBytes;
};

static void RldMk_FreeBuild(struct RldMkBuild *b)
{
	free(b->position);
	free(b->vertexPosition);
	free(b->triangle);
	free(b->parts);
	free(b->positionPart);
	free(b->positionGroup);
	free(b->frame);
	free(b->quant);
	free(b->cornerColor);
	free(b->command);
	free(b->record);
	free(b->drawTriangle);
	free(b->drawOrder);
	free(b->cmdl);
	memset(b, 0, sizeof(*b));
}

static int RldMk_OutOfMemory(void)
{
	RldMk_Say("error", "memory", NULL, "rldpack ran out of memory while building the character.");
	return 0;
}

static const double *RldMk_At(const struct RldMkBuild *b, u32 frame, u32 position)
{
	return &b->frame[((size_t)frame * b->positionCount + position) * 3u];
}

//========================================================================================
// STEP 3 AND 5: WELDING, AXES, SCALE
//========================================================================================

struct RldMkWeldKey
{
	s64 key[3];
	u32 vertex;
};

static int RldMk_CompareWeldKey(const void *a, const void *b)
{
	const struct RldMkWeldKey *x = (const struct RldMkWeldKey *)a;
	const struct RldMkWeldKey *y = (const struct RldMkWeldKey *)b;
	int i;

	for (i = 0; i < 3; i++)
	{
		if (x->key[i] != y->key[i])
		{
			return (x->key[i] < y->key[i]) ? -1 : 1;
		}
	}

	return (x->vertex < y->vertex) ? -1 : ((x->vertex > y->vertex) ? 1 : 0);
}

// PLY axes -> game axes: +Y up, +Z forward, X so that the turn stays a proper
// rotation (a mirror would turn every face inside out).
static void RldMk_Axes(const struct RldMkOptions *opt, const double in[3], double out[3])
{
	double x = in[0];
	double y = in[1];
	double z = in[2];

	if (opt->upZ)
	{
		// Blender's own axes: +Z up, the front looks along -Y.
		const double up = z;

		z = -y;
		y = up;
	}

	if (opt->backwards)
	{
		x = -x;
		z = -z;
	}

	out[0] = x * RLDMK_UNITS * opt->scale;
	out[1] = y * RLDMK_UNITS * opt->scale;
	out[2] = z * RLDMK_UNITS * opt->scale;
}

// Positions on a 1e-5 grid of the PLY; every vertex gets its welded position.
// Positions are numbered in the order of their keys. The colors stay with the
// corners (seams keep both colors).
static int RldMk_StepWeld(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b, u32 *keyCount)
{
	struct RldMkWeldKey *keys;
	u32 count = 0;
	u32 i;

	keys = (struct RldMkWeldKey *)malloc(((size_t)ply->vertexCount + 1u) * sizeof(*keys));
	b->vertexPosition = (u32 *)malloc(((size_t)ply->vertexCount + 1u) * sizeof(u32));
	b->position = (double *)malloc(((size_t)ply->vertexCount + 1u) * 3u * sizeof(double));
	if ((keys == NULL) || (b->vertexPosition == NULL) || (b->position == NULL))
	{
		free(keys);
		return RldMk_OutOfMemory();
	}

	for (i = 0; i < ply->vertexCount; i++)
	{
		int a;

		for (a = 0; a < 3; a++)
		{
			keys[i].key[a] = RldMk_Round(ply->xyz[3u * i + (u32)a] * 100000.0);
		}
		keys[i].vertex = i;
	}

	qsort(keys, ply->vertexCount, sizeof(*keys), RldMk_CompareWeldKey);

	for (i = 0; i < ply->vertexCount; i++)
	{
		if ((i == 0u) || (memcmp(keys[i].key, keys[i - 1u].key, sizeof(keys[i].key)) != 0))
		{
			double grid[3];
			int a;

			for (a = 0; a < 3; a++)
			{
				grid[a] = (double)keys[i].key[a] / 100000.0;
			}
			RldMk_Axes(opt, grid, &b->position[3u * count]);
			count++;
		}
		b->vertexPosition[keys[i].vertex] = count - 1u;
	}

	free(keys);
	*keyCount = count;
	return 1;
}

//========================================================================================
// STEP 2: TRIANGLES (and dropping the positions no triangle uses)
//========================================================================================

static int RldMk_AddTriangle(struct RldMkBuild *b, u32 v0, u32 v1, u32 v2)
{
	const u32 p0 = b->vertexPosition[v0];
	const u32 p1 = b->vertexPosition[v1];
	const u32 p2 = b->vertexPosition[v2];
	double n[3];
	double e1;
	double e2;

	if ((p0 == p1) || (p1 == p2) || (p0 == p2))
	{
		b->degenerate++;
		return 0;
	}

	RldMk_Cross(&b->position[3u * p0], &b->position[3u * p1], &b->position[3u * p2], n);
	e1 = RldMk_Distance2(&b->position[3u * p0], &b->position[3u * p1]);
	e2 = RldMk_Distance2(&b->position[3u * p0], &b->position[3u * p2]);
	if (RldMk_Dot(n, n) <= (1.0e-20 * e1 * e2))
	{
		b->degenerate++;
		return 0;
	}

	b->triangle[3u * b->triangleCount + 0u] = v0;
	b->triangle[3u * b->triangleCount + 1u] = v1;
	b->triangle[3u * b->triangleCount + 2u] = v2;
	b->triangleCount++;
	return 1;
}

static int RldMk_StepTriangles(const struct RldMkPly *ply, struct RldMkBuild *b, u32 keyCount)
{
	u32 ngons = 0;
	u32 *renumber;
	double *kept;
	u32 f;
	u32 i;

	b->triangle = (u32 *)malloc(((size_t)ply->faceCount * 2u + 1u) * 3u * sizeof(u32));
	if (b->triangle == NULL)
	{
		return RldMk_OutOfMemory();
	}

	for (f = 0; f < ply->faceCount; f++)
	{
		const u32 n = ply->cornerCount[f];
		const u32 *c = &ply->corners[4u * f];

		if (n < 3u)
		{
			b->degenerate++;
		}
		else if (n == 3u)
		{
			RldMk_AddTriangle(b, c[0], c[1], c[2]);
		}
		else if (n == 4u)
		{
			// The shorter diagonal: a quad that is not flat bends less there.
			const double d02 = RldMk_Distance2(&b->position[3u * b->vertexPosition[c[0]]], &b->position[3u * b->vertexPosition[c[2]]]);
			const double d13 = RldMk_Distance2(&b->position[3u * b->vertexPosition[c[1]]], &b->position[3u * b->vertexPosition[c[3]]]);

			b->quads++;
			if (d02 <= d13)
			{
				RldMk_AddTriangle(b, c[0], c[1], c[2]);
				RldMk_AddTriangle(b, c[0], c[2], c[3]);
			}
			else
			{
				RldMk_AddTriangle(b, c[0], c[1], c[3]);
				RldMk_AddTriangle(b, c[1], c[2], c[3]);
			}
		}
		else
		{
			ngons++;
		}
	}

	if (ngons != 0u)
	{
		RldMk_Say("error", "ply-ngon", NULL, "%u faces have more than four corners. Triangulate the mesh before export (Blender: Triangulate modifier).",
		          ngons);
		return 0;
	}

	if (b->quads != 0u)
	{
		RldMk_Say("info", "ply-quads", NULL, "%u quads were split into triangles.", b->quads);
	}

	if (b->degenerate != 0u)
	{
		RldMk_Say("warning", "ply-degenerate", NULL, "%u faces have no area and are left out.", b->degenerate);
	}

	if (b->triangleCount == 0u)
	{
		RldMk_Say("error", "ply-no-faces", NULL, "The model has no faces. Export the mesh, not only its points.");
		return 0;
	}

	// Only the positions a triangle uses go on: the frames, the hull and the
	// parts know nothing of loose points.
	renumber = (u32 *)malloc(((size_t)keyCount + 1u) * sizeof(u32));
	kept = (double *)malloc(((size_t)keyCount + 1u) * 3u * sizeof(double));
	if ((renumber == NULL) || (kept == NULL))
	{
		free(renumber);
		free(kept);
		return RldMk_OutOfMemory();
	}

	for (i = 0; i < keyCount; i++)
	{
		renumber[i] = RLDMK_NONE;
	}
	for (i = 0; i < (3u * b->triangleCount); i++)
	{
		renumber[b->vertexPosition[b->triangle[i]]] = 0u;
	}

	b->positionCount = 0;
	for (i = 0; i < keyCount; i++)
	{
		if (renumber[i] != RLDMK_NONE)
		{
			renumber[i] = b->positionCount;
			memcpy(&kept[3u * b->positionCount], &b->position[3u * i], 3u * sizeof(double));
			b->positionCount++;
		}
	}

	for (i = 0; i < ply->vertexCount; i++)
	{
		b->vertexPosition[i] = renumber[b->vertexPosition[i]];
	}

	free(b->position);
	b->position = kept;
	free(renumber);
	return 1;
}

// Edges with only one triangle: holes the culled model is seen through.
static void RldMk_StepOpenEdges(struct RldMkBuild *b)
{
	u64 *edges = (u64 *)malloc(((size_t)b->triangleCount * 3u + 1u) * sizeof(u64));
	u32 t;
	u32 i;

	b->openEdges = 0;
	if (edges == NULL)
	{
		return;
	}

	for (t = 0; t < b->triangleCount; t++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			const u32 p = b->vertexPosition[b->triangle[3u * t + (u32)k]];
			const u32 q = b->vertexPosition[b->triangle[3u * t + (u32)((k + 1) % 3)]];

			edges[3u * t + (u32)k] = (p < q) ? (((u64)p << 32) | q) : (((u64)q << 32) | p);
		}
	}

	qsort(edges, (size_t)b->triangleCount * 3u, sizeof(u64), RldMk_CompareU64);

	i = 0;
	while (i < (3u * b->triangleCount))
	{
		u32 run = 1;

		while (((i + run) < (3u * b->triangleCount)) && (edges[i + run] == edges[i]))
		{
			run++;
		}
		if (run == 1u)
		{
			b->openEdges++;
		}
		i += run;
	}

	free(edges);
}

//========================================================================================
// STEP 4: PARTS - KART, DRIVER, STEERING WHEEL
//========================================================================================

static u32 RldMk_Root(u32 *parent, u32 i)
{
	while (parent[i] != i)
	{
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

static void RldMk_Join(u32 *parent, u32 a, u32 b)
{
	const u32 ra = RldMk_Root(parent, a);
	const u32 rb = RldMk_Root(parent, b);

	if (ra < rb)
	{
		parent[rb] = ra;
	}
	else if (rb < ra)
	{
		parent[ra] = rb;
	}
}

static void RldMk_Bounds(const struct RldMkBuild *b, int part, double lo[3], double hi[3])
{
	u32 p;
	int a;

	for (a = 0; a < 3; a++)
	{
		lo[a] = 1.0e30;
		hi[a] = -1.0e30;
	}

	for (p = 0; p < b->positionCount; p++)
	{
		if ((part >= 0) && (b->positionPart[p] != (u32)part))
		{
			continue;
		}
		for (a = 0; a < 3; a++)
		{
			const double v = b->position[3u * p + (u32)a];

			if (v < lo[a])
			{
				lo[a] = v;
			}
			if (v > hi[a])
			{
				hi[a] = v;
			}
		}
	}
}

static double RldMk_Extent(const struct RldMkPart *part)
{
	double extent = 0.0;
	int a;

	for (a = 0; a < 3; a++)
	{
		if ((part->hi[a] - part->lo[a]) > extent)
		{
			extent = part->hi[a] - part->lo[a];
		}
	}

	return extent;
}

// Steps 4 and 5: which part is the kart, which the driver and the steering
// wheel, and whether the model is the size and the way round of a driver.
static int RldMk_StepParts(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	u32 *parent;
	u32 *partOfRoot;
	double lo[3];
	double hi[3];
	double length;
	u32 p;
	u32 t;
	u32 i;
	int best = -1;

	parent = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	partOfRoot = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	b->positionPart = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	b->positionGroup = (u8 *)calloc((size_t)b->positionCount + 1u, 1u);
	b->parts = (struct RldMkPart *)calloc((size_t)b->positionCount + 1u, sizeof(struct RldMkPart));
	if ((parent == NULL) || (partOfRoot == NULL) || (b->positionPart == NULL) || (b->positionGroup == NULL) || (b->parts == NULL))
	{
		free(parent);
		free(partOfRoot);
		return RldMk_OutOfMemory();
	}

	for (p = 0; p < b->positionCount; p++)
	{
		parent[p] = p;
		partOfRoot[p] = RLDMK_NONE;
	}

	for (t = 0; t < b->triangleCount; t++)
	{
		const u32 p0 = b->vertexPosition[b->triangle[3u * t + 0u]];

		RldMk_Join(parent, p0, b->vertexPosition[b->triangle[3u * t + 1u]]);
		RldMk_Join(parent, p0, b->vertexPosition[b->triangle[3u * t + 2u]]);
	}

	// Parts numbered in the order of their first position.
	b->partCount = 0;
	for (p = 0; p < b->positionCount; p++)
	{
		const u32 root = RldMk_Root(parent, p);

		if (partOfRoot[root] == RLDMK_NONE)
		{
			partOfRoot[root] = b->partCount++;
		}
		b->positionPart[p] = partOfRoot[root];
	}
	free(parent);
	free(partOfRoot);

	for (t = 0; t < b->triangleCount; t++)
	{
		b->parts[b->positionPart[b->vertexPosition[b->triangle[3u * t]]]].triangles++;
	}
	for (i = 0; i < b->partCount; i++)
	{
		RldMk_Bounds(b, (int)i, b->parts[i].lo, b->parts[i].hi);
		b->parts[i].group = RLDMK_GROUP_DRIVER;
	}

	// The size first: a model exported in centimeters has no kart of 1.8 m,
	// and "no kart" would send the author looking in the wrong place.
	RldMk_Bounds(b, -1, lo, hi);
	length = (hi[2] - lo[2]) / RLDMK_UNITS;
	if ((length < (RLDMK_DRIVER_LENGTH / 5.0)) || (length > (RLDMK_DRIVER_LENGTH * 5.0)))
	{
		const double factor = (length > RLDMK_DRIVER_LENGTH) ? (length / RLDMK_DRIVER_LENGTH) : (RLDMK_DRIVER_LENGTH / ((length > 0.0) ? length : 1.0e-9));

		RldMk_Say("error", "model-scale", NULL,
		          "The model is %.1f Blender units long - about %.0f times too large (or small) for a driver. Export it in meters; a driver with kart is about 1.8 units long.",
		          length, factor);
		return 0;
	}

	// THE KART: a part at the bottom, as long as a kart. With one part the
	// whole mesh has to be it.
	for (i = 0; i < b->partCount; i++)
	{
		const struct RldMkPart *part = &b->parts[i];
		const double bottom = part->lo[1] / RLDMK_UNITS;
		const double partLength = (part->hi[2] - part->lo[2]) / RLDMK_UNITS;

		if ((bottom < RLDMK_KART_BOTTOM_MIN) || (bottom > RLDMK_KART_BOTTOM_MAX) || (partLength < RLDMK_KART_LENGTH_MIN) || (partLength > RLDMK_KART_LENGTH_MAX))
		{
			continue;
		}

		if ((best < 0) || ((part->hi[2] - part->lo[2]) > (b->parts[best].hi[2] - b->parts[best].lo[2])) ||
		    (((part->hi[2] - part->lo[2]) == (b->parts[best].hi[2] - b->parts[best].lo[2])) && (part->lo[1] < b->parts[best].lo[1])))
		{
			best = (int)i;
		}
	}

	if (best < 0)
	{
		RldMk_Say("error", "ply-no-kart", NULL, "No kart found. A character needs a kart: a part at the bottom of the model, about 1.8 Blender units long.");
		return 0;
	}

	b->kartPart = best;
	b->parts[best].group = RLDMK_GROUP_KART;

	// THE DRIVER: the largest other part. The steering wheel: small parts in
	// front of it. Everything else moves with the driver.
	b->driverPart = -1;
	for (i = 0; i < b->partCount; i++)
	{
		if (((int)i != best) && ((b->driverPart < 0) || (b->parts[i].triangles > b->parts[b->driverPart].triangles)))
		{
			b->driverPart = (int)i;
		}
	}

	if (b->driverPart >= 0)
	{
		const struct RldMkPart *driver = &b->parts[b->driverPart];
		const double driverZ = 0.5 * (driver->lo[2] + driver->hi[2]);
		u32 behind = 0;

		for (i = 0; i < b->partCount; i++)
		{
			struct RldMkPart *part = &b->parts[i];

			if (((int)i == best) || ((int)i == b->driverPart) || (RldMk_Extent(part) > (RLDMK_WHEEL_EXTENT * RLDMK_UNITS)))
			{
				continue;
			}

			if ((0.5 * (part->lo[2] + part->hi[2])) > driverZ)
			{
				part->group = RLDMK_GROUP_WHEEL;
			}
			else
			{
				behind++;
			}
		}

		// Small parts only behind the driver and none in front: the model
		// probably looks backwards.
		if (behind != 0u)
		{
			int front = 0;

			for (i = 0; i < b->partCount; i++)
			{
				if (b->parts[i].group == RLDMK_GROUP_WHEEL)
				{
					front = 1;
				}
			}

			if (!front)
			{
				RldMk_Say("warning", "ply-facing", NULL, "The model seems to face backwards. Rotate it by 180 degrees or set Forward to -Z.");
			}
		}
	}

	for (p = 0; p < b->positionCount; p++)
	{
		b->positionGroup[p] = (u8)b->parts[b->positionPart[p]].group;
	}
	for (t = 0; t < b->triangleCount; t++)
	{
		b->groupTriangles[b->parts[b->positionPart[b->vertexPosition[b->triangle[3u * t]]]].group]++;
	}

	b->posed = !opt->still && (b->driverPart >= 0);
	if (b->driverPart < 0)
	{
		RldMk_Say("warning", "ply-parts", NULL,
		          "Driver and kart are one connected piece, so the driver cannot lean when steering. All poses stay the same. Separate the kart in Blender (Edit Mode: select it, P > Selection) so that only the driver moves.");
	}

	// Wheels in the model: small parts low at the sides of the kart. The game
	// draws its own (game/DrawTires.c:333-362).
	for (i = 0; i < b->partCount; i++)
	{
		const struct RldMkPart *part = &b->parts[i];
		const double centerX = 0.5 * (part->lo[0] + part->hi[0]) / RLDMK_UNITS;

		if (((int)i != best) && (RldMk_Extent(part) <= (0.5 * RLDMK_UNITS)) && (part->hi[1] <= (b->parts[best].lo[1] + 0.3 * RLDMK_UNITS)) &&
		    ((centerX >= 0.25) || (centerX <= -0.25)))
		{
			RldMk_Say("note", "ply-wheels", NULL, "The game draws its own wheels; wheels in the model would show twice. Remove them from the model.");
			break;
		}
	}

	// Size, centering (step 5), in Blender units.
	b->size[0] = length;
	b->size[1] = (b->parts[best].hi[0] - b->parts[best].lo[0]) / RLDMK_UNITS;
	b->size[2] = (hi[1] - lo[1]) / RLDMK_UNITS;
	b->size[3] = lo[1] / RLDMK_UNITS;

	if ((b->size[0] < RLDMK_KART_LENGTH_MIN) || (b->size[0] > RLDMK_KART_LENGTH_MAX) || (b->size[1] < 0.8) || (b->size[1] > 1.0) || (b->size[2] < 0.6) ||
	    (b->size[2] > 2.7) || (b->size[3] < RLDMK_KART_BOTTOM_MIN) || (b->size[3] > RLDMK_KART_BOTTOM_MAX))
	{
		RldMk_Say("warning", "ply-axes", NULL,
		          "The model is %.2f long, %.2f wide and %.2f tall and starts %.2f above the ground (Blender units). A driver with kart is about 1.8 long, 0.9 wide and 0.9-2.3 tall. Check scale and axes (+Y up, +Z forward).",
		          b->size[0], b->size[1], b->size[2], b->size[3]);
	}

	{
		const double center = 0.5 * (b->parts[best].lo[0] + b->parts[best].hi[0]) / RLDMK_UNITS;

		if ((center > 0.08) || (center < -0.08))
		{
			RldMk_Say("warning", "ply-center", NULL, "The model is %.2f Blender units off-center to the side. Center the kart on x = 0.",
			          (center < 0.0) ? -center : center);
		}
	}

	return 1;
}

//========================================================================================
// STEP 6: DRAW MEMORY PER FRAME
//========================================================================================
//
// B = 28 x G3 + 40 x GT3 bytes per model and frame (RB:3063-3070, 3088-3090), at
// most 22560 (564 textured triangles, N. Oxide). make-char writes G3 only. The
// number is shown, never written into the file.

static int RldMk_StepBudget(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	const u32 bytes = b->triangleCount * RLDCHAR_DRAW_BYTES_G3;

	if ((b->triangleCount > (RLDCHAR_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3)) || (bytes > RLDCHAR_DRAW_BYTES_MAX))
	{
		RldMk_Say("error", "model-tris", NULL,
		          "The model needs %u bytes of draw memory per frame (%u triangles); a driver may use %u, no more than N. Oxide. Reduce it in Blender (Decimate modifier).",
		          bytes, b->triangleCount, RLDCHAR_DRAW_BYTES_MAX);
		return 0;
	}

	if (b->triangleCount > RLDMK_HI_TRIANGLES_MAX)
	{
		RldMk_Say("note", "model-tris-info", NULL,
		          "%u triangles - more than any original driver (564), but without textures they cost no more draw memory than N. Oxide.", b->triangleCount);
	}

	if (opt->twoSided)
	{
		RldMk_Say("note", "draw-both-sides", NULL, "Both sides are drawn, so the model always costs its full %u bytes per frame instead of about half.", bytes);
	}
	else if (b->openEdges != 0u)
	{
		RldMk_Say("note", "ply-open-edges", NULL,
		          "The mesh has %u open edges; you may see through it from some sides. Close the holes or tick 'Draw both sides'.", b->openEdges);
	}

	return 1;
}

//========================================================================================
// STEP 8: THE 47 FRAMES
//========================================================================================
//
// The frame counts are the templates' (21/7/15/4, s_rldCharFrames), counted
// without the half-frame bit. Still: every frame is the neutral pose. The
// automatic poses move rigid parts only, the kart never:
//
//   turn 0..20     driver and steering wheel roll as one block about the hip,
//                  frame 0 fully toward +X, frame 10 neutral, frame 20 mirrored;
//                  frames 0-1 and 19-20 are both at the stop, in between linear
//                  (as the retail models)
//   reverse 0..6   the driver alone: turns up to 10 degrees toward +X and leans
//                  8 degrees back; the steering wheel stays (beyond 10 degrees the
//                  hands would leave it)
//   bump 0..14     the driver nods along the retail course at about 2/3 of its
//                  swing, squashed to 0.93 at frame 8, ending near neutral
//   jump 0..3      the driver leans back 0, 5, 12, 15 degrees and stretches to 1.05
//
// The game sets the whole instance's matrix for bump and jump as well
// (game/Vehicle/VehPhysForce.c:895-918), so even the still model moves.

struct RldMkPose
{
	double roll;    // degrees about Z through the hip: + moves the top toward +X
	double yaw;     // degrees about Y: + turns the front (+Z) toward +X
	double pitch;   // degrees about X: + moves the top forward (+Z)
	double stretch; // height factor about the hip
	int wheel;      // the steering wheel moves with the driver
};

static const double s_rldMkBumpPitch[15] = {-9.0, -14.0, -20.0, -20.0, -19.0, -10.0, -2.0, 3.0, 8.0, -1.0, -10.0, -13.0, -16.0, -11.0, -6.0};
static const double s_rldMkBumpStretch[15] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.965, 0.93, 0.965, 1.0, 1.0, 1.0, 1.0, 1.0};
static const double s_rldMkJumpPitch[4] = {0.0, -5.0, -12.0, -15.0};
static const double s_rldMkJumpStretch[4] = {1.0, 1.0 + 0.05 / 3.0, 1.0 + 0.1 / 3.0, 1.05};

static void RldMk_FramePose(u32 frame, struct RldMkPose *pose)
{
	u32 first = 0;
	int anim;

	memset(pose, 0, sizeof(*pose));
	pose->stretch = 1.0;

	for (anim = 0; anim < RLDCHAR_ANIM_COUNT; anim++)
	{
		const u32 count = (u32)s_rldCharFrames[anim];

		if (frame < (first + count))
		{
			const u32 k = frame - first;

			if (anim == 0)
			{
				double t = ((double)RLDMK_NEUTRAL_FRAME - (double)k) / 9.0;

				if (t > 1.0)
				{
					t = 1.0;
				}
				if (t < -1.0)
				{
					t = -1.0;
				}
				pose->roll = 12.0 * t;
				pose->wheel = 1;
			}
			else if (anim == 1)
			{
				pose->yaw = 10.0 * (double)k / 6.0;
				pose->pitch = -8.0 * (double)k / 6.0;
			}
			else if (anim == 2)
			{
				pose->pitch = s_rldMkBumpPitch[(k < 15u) ? k : 14u];
				pose->stretch = s_rldMkBumpStretch[(k < 15u) ? k : 14u];
			}
			else
			{
				pose->pitch = s_rldMkJumpPitch[(k < 4u) ? k : 3u];
				pose->stretch = s_rldMkJumpStretch[(k < 4u) ? k : 3u];
			}
			return;
		}

		first += count;
	}
}

static void RldMk_ApplyPose(const struct RldMkPose *pose, const double in[3], double out[3])
{
	double sp;
	double cp;
	double sy;
	double cy;
	double sr;
	double cr;
	double x = in[0];
	double y = (in[1] - RLDMK_HIP_Y) * pose->stretch;
	double z = in[2];
	double t;

	RldMk_SinCos(pose->pitch, &sp, &cp);
	RldMk_SinCos(pose->yaw, &sy, &cy);
	RldMk_SinCos(pose->roll, &sr, &cr);

	// pitch about X: the top toward +Z
	t = y * cp - z * sp;
	z = y * sp + z * cp;
	y = t;

	// yaw about Y: the front toward +X
	t = x * cy + z * sy;
	z = -x * sy + z * cy;
	x = t;

	// roll about Z: the top toward +X
	t = x * cr + y * sr;
	y = -x * sr + y * cr;
	x = t;

	out[0] = x;
	out[1] = y + RLDMK_HIP_Y;
	out[2] = z;
}

static int RldMk_StepPoses(struct RldMkBuild *b)
{
	u32 f;
	u32 p;

	b->frame = (double *)malloc((size_t)RLDMK_FRAMES_TOTAL * ((size_t)b->positionCount + 1u) * 3u * sizeof(double));
	if (b->frame == NULL)
	{
		return RldMk_OutOfMemory();
	}

	for (f = 0; f < RLDMK_FRAMES_TOTAL; f++)
	{
		struct RldMkPose pose;

		RldMk_FramePose(f, &pose);

		for (p = 0; p < b->positionCount; p++)
		{
			const double *neutral = &b->position[3u * p];
			double *out = &b->frame[((size_t)f * b->positionCount + p) * 3u];
			const int group = b->positionGroup[p];

			if (!b->posed || (group == RLDMK_GROUP_KART) || ((group == RLDMK_GROUP_WHEEL) && !pose.wheel))
			{
				memcpy(out, neutral, 3u * sizeof(double));
			}
			else
			{
				RldMk_ApplyPose(&pose, neutral, out);
			}
		}
	}

	return 1;
}

//========================================================================================
// STEP 9: QUANTIZATION
//========================================================================================
//
// The renderer builds a vertex as (pos + byte) << 2 per axis (RB:2437-2467) and
// scales it by ModelHeader.scale / 4 in the GTE (RB:1583-1592): game units =
// (byte + pos) x scale / 4096. scale is per header, pos per frame
// (include/namespace_Instance.h:304-314, 357).
//
// THE UP AXIS HAS HALF THE RESOLUTION: the packed word is masked with 0xfff8ffff
// after the shift (RB:2446, 2461), which clears bit 0 of pos.y + byte. So pos.y
// and every Y byte are even, and scale.y spans 254 instead of 255.
//
// ONE ORIGIN FOR ALL FRAMES: the hull is taken over all 47 frames together,
// and every frame gets the same pos. The kart then stays byte for byte the same
// in every frame and cannot jitter while the driver leans (the retail models
// keep pos the same across turn and jump as well).

static int RldMk_StepQuantize(struct RldMkBuild *b)
{
	static const char *const axisNames[3] = {"wide", "tall", "long"};
	static const double spans[3] = {255.0, 254.0, 255.0};
	double lo[3];
	double hi[3];
	size_t i;
	const size_t total = (size_t)RLDMK_FRAMES_TOTAL * b->positionCount;
	int a;

	for (a = 0; a < 3; a++)
	{
		lo[a] = 1.0e30;
		hi[a] = -1.0e30;
	}

	for (i = 0; i < total; i++)
	{
		for (a = 0; a < 3; a++)
		{
			const double v = b->frame[3u * i + (size_t)a];

			if (v < lo[a])
			{
				lo[a] = v;
			}
			if (v > hi[a])
			{
				hi[a] = v;
			}
		}
	}

	for (a = 0; a < 3; a++)
	{
		const double needed = RldMk_Ceil((hi[a] - lo[a]) * 4096.0 / spans[a]);
		double low;

		// scale is s16 (include/namespace_Instance.h:357, include/ctr_math.h:14-19).
		if (needed > 32767.0)
		{
			RldMk_Say("error", "model-scale", NULL,
			          "The model is %.1f Blender units %s - too large for a driver. Export it in meters; a driver with kart is about 1.8 units long.",
			          (hi[a] - lo[a]) / RLDMK_UNITS, axisNames[a]);
			return 0;
		}

		b->scale[a] = (needed < 1.0) ? 1 : (int)needed;
		low = lo[a] * 4096.0 / (double)b->scale[a];

		if (a == 1)
		{
			b->origin[a] = 2 * (int)RldMk_Round(low / 2.0);
		}
		else
		{
			b->origin[a] = (int)RldMk_Round(low);
		}

		if ((b->origin[a] < RLDCHAR_COORD_MIN) || ((b->origin[a] + RLDCHAR_RECORD_SPAN) > RLDCHAR_COORD_MAX))
		{
			RldMk_Say("error", "model-coords", NULL, "The model is too large for the game's coordinates.");
			return 0;
		}
	}

	b->quant = (u8 *)malloc(total * 3u + 1u);
	if (b->quant == NULL)
	{
		return RldMk_OutOfMemory();
	}

	for (i = 0; i < total; i++)
	{
		for (a = 0; a < 3; a++)
		{
			const double u = b->frame[3u * i + (size_t)a] * 4096.0 / (double)b->scale[a] - (double)b->origin[a];
			s64 q;

			if (a == 1)
			{
				q = RldMk_Round(u / 2.0);
				q = (q < 0) ? 0 : ((q > 127) ? 127 : q);
				q *= 2;
			}
			else
			{
				q = RldMk_Round(u);
				q = (q < 0) ? 0 : ((q > 255) ? 255 : q);
			}

			// stored X, Z (forward), Y (up)
			b->quant[3u * i + ((a == 0) ? 0u : ((a == 1) ? 2u : 1u))] = (u8)q;
		}
	}

	return 1;
}

// A packed position as the renderer reads it back, in game units.
static void RldMk_Decoded(const struct RldMkBuild *b, u32 frame, u32 position, double out[3])
{
	const u8 *q = &b->quant[((size_t)frame * b->positionCount + position) * 3u];

	out[0] = (double)((int)q[0] + b->origin[0]) * (double)b->scale[0] / 4096.0;
	out[1] = (double)((int)q[2] + b->origin[1]) * (double)b->scale[1] / 4096.0;
	out[2] = (double)((int)q[1] + b->origin[2]) * (double)b->scale[2] / 4096.0;
}

// Triangles that lose their area (or turn over) in the packed neutral frame
// are left out: the renderer would not draw them (NCLIP 0, RB:2891-2896), or
// draw them from the wrong side.
static int RldMk_StepCollapse(struct RldMkBuild *b)
{
	u32 kept = 0;
	u32 t;

	for (t = 0; t < b->triangleCount; t++)
	{
		double packed[3][3];
		double exact[3];
		double n[3];
		int k;

		for (k = 0; k < 3; k++)
		{
			RldMk_Decoded(b, RLDMK_NEUTRAL_FRAME, b->vertexPosition[b->triangle[3u * t + (u32)k]], packed[k]);
		}
		RldMk_Cross(packed[0], packed[1], packed[2], n);
		RldMk_Cross(RldMk_At(b, RLDMK_NEUTRAL_FRAME, b->vertexPosition[b->triangle[3u * t + 0u]]),
		            RldMk_At(b, RLDMK_NEUTRAL_FRAME, b->vertexPosition[b->triangle[3u * t + 1u]]),
		            RldMk_At(b, RLDMK_NEUTRAL_FRAME, b->vertexPosition[b->triangle[3u * t + 2u]]), exact);

		if (RldMk_Dot(n, exact) <= 0.0)
		{
			b->collapsed++;
			continue;
		}

		if (kept != t)
		{
			memcpy(&b->triangle[3u * kept], &b->triangle[3u * t], 3u * sizeof(u32));
		}
		kept++;
	}

	b->triangleCount = kept;

	if (b->collapsed != 0u)
	{
		RldMk_Say("warning", "model-collapse", NULL, "%u triangles collapse to a line after packing and are left out.", b->collapsed);
	}

	if (kept == 0u)
	{
		RldMk_Say("error", "ply-no-faces", NULL, "The model has no faces. Export the mesh, not only its points.");
		return 0;
	}

	return 1;
}

//========================================================================================
// STEP 7: COLORS
//========================================================================================
//
// The color index has 7 bits (RB:2520-2532): at most 128 colors, 64 by default
// as the retail drivers have at most. More colors are merged by median cut
// (weighted by how many corners use a color), then two k-means rounds. All in
// integers with fixed tie-breaks: the lowest index wins.

struct RldMkColor
{
	u32 rgb;      // 0xRRGGBB
	u32 weight;   // corners with this color
	u32 palette;  // index it is mapped to
};

static int s_rldMkSortChannel;

static u32 RldMk_Channel(u32 rgb, int channel)
{
	return (rgb >> (16 - 8 * channel)) & 0xFFu;
}

static int RldMk_CompareColor(const void *a, const void *b)
{
	const struct RldMkColor *x = (const struct RldMkColor *)a;
	const struct RldMkColor *y = (const struct RldMkColor *)b;
	const u32 cx = RldMk_Channel(x->rgb, s_rldMkSortChannel);
	const u32 cy = RldMk_Channel(y->rgb, s_rldMkSortChannel);

	if (cx != cy)
	{
		return (cx < cy) ? -1 : 1;
	}

	return (x->rgb < y->rgb) ? -1 : ((x->rgb > y->rgb) ? 1 : 0);
}

static u32 RldMk_ColorDistance2(u32 a, u32 b)
{
	u32 sum = 0;
	int c;

	for (c = 0; c < 3; c++)
	{
		const int d = (int)RldMk_Channel(a, c) - (int)RldMk_Channel(b, c);

		sum += (u32)(d * d);
	}

	return sum;
}

static u32 RldMk_Nearest(const u32 *palette, u32 count, u32 rgb)
{
	u32 best = 0;
	u32 bestDistance = 0xFFFFFFFFu;
	u32 i;

	for (i = 0; i < count; i++)
	{
		const u32 d = RldMk_ColorDistance2(palette[i], rgb);

		if (d < bestDistance)
		{
			bestDistance = d;
			best = i;
		}
	}

	return best;
}

static int RldMk_StepColors(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b)
{
	const u32 corners = 3u * b->triangleCount;
	struct RldMkColor *colors;
	u32 *rgb;
	u32 palette[RLDCHAR_COLORS_MAX];
	u32 remap[RLDCHAR_COLORS_MAX];
	u32 unique = 0;
	u32 count = 0;
	u32 i;
	double sum = 0.0;
	double weights = 0.0;

	rgb = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	colors = (struct RldMkColor *)malloc(((size_t)corners + 1u) * sizeof(struct RldMkColor));
	b->cornerColor = (u8 *)malloc((size_t)corners + 1u);
	if ((rgb == NULL) || (colors == NULL) || (b->cornerColor == NULL))
	{
		free(rgb);
		free(colors);
		return RldMk_OutOfMemory();
	}

	for (i = 0; i < corners; i++)
	{
		const u8 *c = &ply->rgb[3u * b->triangle[i]];

		rgb[i] = ((u32)c[0] << 16) | ((u32)c[1] << 8) | (u32)c[2];
	}

	// The distinct colors with their weights, sorted by value.
	{
		u32 *sorted = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));

		if (sorted == NULL)
		{
			free(rgb);
			free(colors);
			return RldMk_OutOfMemory();
		}
		memcpy(sorted, rgb, (size_t)corners * sizeof(u32));
		qsort(sorted, corners, sizeof(u32), RldMk_CompareU32);
		for (i = 0; i < corners; i++)
		{
			if ((unique == 0u) || (colors[unique - 1u].rgb != sorted[i]))
			{
				colors[unique].rgb = sorted[i];
				colors[unique].weight = 0;
				colors[unique].palette = 0;
				unique++;
			}
			colors[unique - 1u].weight++;
		}
		free(sorted);
	}
	b->colorsIn = unique;

	if (unique <= opt->colorTarget)
	{
		for (i = 0; i < unique; i++)
		{
			palette[i] = colors[i].rgb;
			colors[i].palette = i;
		}
		count = unique;
	}
	else
	{
		// MEDIAN CUT: split the box with the widest channel range at the
		// weighted median of that channel, until there are colorTarget boxes.
		u32 boxFirst[RLDCHAR_COLORS_MAX];
		u32 boxCount[RLDCHAR_COLORS_MAX];
		u32 boxes = 1;
		int round;

		boxFirst[0] = 0;
		boxCount[0] = unique;

		while (boxes < opt->colorTarget)
		{
			int bestBox = -1;
			int bestChannel = 0;
			u32 bestRange = 0;
			u32 bestWeight = 0;
			u32 box;

			for (box = 0; box < boxes; box++)
			{
				u32 weight = 0;
				int c;

				if (boxCount[box] < 2u)
				{
					continue;
				}

				for (i = 0; i < boxCount[box]; i++)
				{
					weight += colors[boxFirst[box] + i].weight;
				}

				for (c = 0; c < 3; c++)
				{
					u32 low = 255;
					u32 high = 0;

					for (i = 0; i < boxCount[box]; i++)
					{
						const u32 v = RldMk_Channel(colors[boxFirst[box] + i].rgb, c);

						low = (v < low) ? v : low;
						high = (v > high) ? v : high;
					}

					if ((bestBox < 0) || ((high - low) > bestRange) || (((high - low) == bestRange) && (weight > bestWeight)))
					{
						bestBox = (int)box;
						bestChannel = c;
						bestRange = high - low;
						bestWeight = weight;
					}
				}
			}

			if (bestBox < 0)
			{
				break;
			}

			{
				const u32 first = boxFirst[bestBox];
				const u32 n = boxCount[bestBox];
				u32 half = 0;
				u32 split = n - 1u;

				s_rldMkSortChannel = bestChannel;
				qsort(&colors[first], n, sizeof(struct RldMkColor), RldMk_CompareColor);

				for (i = 0; i < (n - 1u); i++)
				{
					half += colors[first + i].weight;
					if ((2u * half) >= bestWeight)
					{
						split = i + 1u;
						break;
					}
				}

				boxCount[bestBox] = split;
				boxFirst[boxes] = first + split;
				boxCount[boxes] = n - split;
				boxes++;
			}
		}

		for (i = 0; i < boxes; i++)
		{
			u32 sums[3] = {0, 0, 0};
			u32 weight = 0;
			u32 k;
			int c;

			for (k = 0; k < boxCount[i]; k++)
			{
				const struct RldMkColor *color = &colors[boxFirst[i] + k];

				for (c = 0; c < 3; c++)
				{
					sums[c] += RldMk_Channel(color->rgb, c) * color->weight;
				}
				weight += color->weight;
			}

			palette[i] = (((sums[0] + weight / 2u) / weight) << 16) | (((sums[1] + weight / 2u) / weight) << 8) | ((sums[2] + weight / 2u) / weight);
		}
		count = boxes;

		// TWO K-MEANS ROUNDS. An entry nobody comes to keeps its value.
		for (round = 0; round < 2; round++)
		{
			u32 sums[RLDCHAR_COLORS_MAX][3];
			u32 weight[RLDCHAR_COLORS_MAX];
			int c;

			memset(sums, 0, sizeof(sums));
			memset(weight, 0, sizeof(weight));

			for (i = 0; i < unique; i++)
			{
				const u32 nearest = RldMk_Nearest(palette, count, colors[i].rgb);

				for (c = 0; c < 3; c++)
				{
					sums[nearest][c] += RldMk_Channel(colors[i].rgb, c) * colors[i].weight;
				}
				weight[nearest] += colors[i].weight;
			}

			for (i = 0; i < count; i++)
			{
				if (weight[i] != 0u)
				{
					palette[i] = (((sums[i][0] + weight[i] / 2u) / weight[i]) << 16) | (((sums[i][1] + weight[i] / 2u) / weight[i]) << 8) |
					             ((sums[i][2] + weight[i] / 2u) / weight[i]);
				}
			}
		}

		for (i = 0; i < unique; i++)
		{
			colors[i].palette = RldMk_Nearest(palette, count, colors[i].rgb);
		}
	}

	// Entries nobody uses go, the order stays.
	{
		u8 usedEntry[RLDCHAR_COLORS_MAX];
		u32 kept = 0;

		memset(usedEntry, 0, sizeof(usedEntry));
		for (i = 0; i < unique; i++)
		{
			usedEntry[colors[i].palette] = 1;
		}
		for (i = 0; i < count; i++)
		{
			if (usedEntry[i])
			{
				remap[i] = kept;
				palette[kept] = palette[i];
				kept++;
			}
		}
		for (i = 0; i < unique; i++)
		{
			colors[i].palette = remap[colors[i].palette];
		}
		count = kept;
	}

	b->colorCount = count;
	b->colorMax = 0.0;
	for (i = 0; i < count; i++)
	{
		// file order r, g, b, code 0: what the GTE takes as RGB0 (RB:3020-3022)
		b->palette[i] = ((palette[i] >> 16) & 0xFFu) | (palette[i] & 0xFF00u) | ((palette[i] & 0xFFu) << 16);
	}

	for (i = 0; i < unique; i++)
	{
		const double d = RldMk_Sqrt((double)RldMk_ColorDistance2(colors[i].rgb, palette[colors[i].palette]));

		sum += d * (double)colors[i].weight;
		weights += (double)colors[i].weight;
		if (d > b->colorMax)
		{
			b->colorMax = d;
		}
	}
	b->colorMean = (weights > 0.0) ? (sum / weights) : 0.0;

	// Back to the corners. The median cut has reordered colors[]; sorted by
	// the red channel and then by value is sorted by value (red is the top byte).
	s_rldMkSortChannel = 0;
	qsort(colors, unique, sizeof(struct RldMkColor), RldMk_CompareColor);
	for (i = 0; i < corners; i++)
	{
		u32 low = 0;
		u32 high = unique;

		while (low < high)
		{
			const u32 mid = low + (high - low) / 2u;

			if (colors[mid].rgb < rgb[i])
			{
				low = mid + 1u;
			}
			else
			{
				high = mid;
			}
		}
		b->cornerColor[i] = (u8)colors[low].palette;
	}

	if (!ply->hasColors)
	{
		RldMk_Say("warning", "ply-no-colors", NULL,
		          "The model has no vertex colors, so it is drawn in one grey. Paint it in Blender (Vertex Paint) and export with colors.");
	}
	else if (b->colorsIn > opt->colorTarget)
	{
		RldMk_Say("warning", "ply-colors", NULL,
		          "The model uses %u colors; the game keeps %u. Similar colors were merged (average change %.1f, largest %.1f of 255) - check the portrait.",
		          b->colorsIn, b->colorCount, b->colorMean, b->colorMax);
	}

	if (ply->hasUv)
	{
		RldMk_Say("note", "ply-uv-unused", NULL, "The model has texture coordinates, but textures are not supported yet - only the vertex colors are used.");
	}
	if (ply->alphaBelow != 0u)
	{
		RldMk_Say("note", "ply-alpha", NULL, "Vertex color transparency is not used.");
	}

	free(rgb);
	free(colors);
	return 1;
}

//========================================================================================
// STEP 10: THE COMMAND LIST
//========================================================================================
//
// WHAT THE RENDERER DOES WITH IT (RenderBucket_DrawFunc_Normal, RB:4584-4700):
// every command brings one corner; a strip starts at bit 31, its first two
// corners draw nothing, every further corner draws one triangle out of the GTE
// window. Without bit 30 the window moves on (RTPS: the last two corners and
// the new one, RB:2840-2843); with bit 30 the first corner stays (fan: corner 0,
// the last corner and the new one, RB:2828-2838). The cull bits of a triangle
// come from the strip's first command for the first triangle (primCommand,
// RB:4636-4639, 4665), from the new corner's command after that.
//
// So in a strip every second triangle runs the other way round than its
// neighbor, in a fan none does: bit 0x20 is set per triangle, from how its
// window order compares with its source face, see RLDMK_CULL_SIGN.
//
// Two triangles continue a strip only over an edge whose two corners have the
// same position AND the same color: the color is per command (RB:2629), so a
// shared corner shows one color. The vertex cache is per position: a cached
// corner keeps only its place (RB:2588-2596), its command brings the color.
//
// THE CACHE as retail uses it: bit 27 on every command, the slots ceil(N/2)..87
// (the slots below share the scratchpad with the N cached colors, RB:2504-2532
// against RB:678-686), filled first in, first out. A corner still in its slot
// is fetched with bit 26 and costs no record.

struct RldMkEdge
{
	u32 a;
	u32 b;
	u32 triangle;
};

static int RldMk_CompareEdge(const void *x, const void *y)
{
	const struct RldMkEdge *p = (const struct RldMkEdge *)x;
	const struct RldMkEdge *q = (const struct RldMkEdge *)y;

	if (p->a != q->a)
	{
		return (p->a < q->a) ? -1 : 1;
	}
	if (p->b != q->b)
	{
		return (p->b < q->b) ? -1 : 1;
	}
	return (p->triangle < q->triangle) ? -1 : ((p->triangle > q->triangle) ? 1 : 0);
}

struct RldMkStripper
{
	struct RldMkBuild *build;
	u32 *triangleCorner;   // 3 corners per triangle; a corner = position and color
	u32 *cornerPosition;
	u32 *cornerColor;
	struct RldMkEdge *edges;
	u32 edgeCount;
	u8 *used;
	u32 *positionSlot;     // per position: its slot, RLDMK_NONE when not cached
	u32 *slotPosition;     // per slot - slotLow: the position in it, RLDMK_NONE
	u32 next;              // first in, first out: the slot filled next
	int cull;
};

// The lowest unused triangle over the corner edge a-b, RLDMK_NONE when there is none.
static u32 RldMk_Neighbor(const struct RldMkStripper *s, u32 a, u32 b)
{
	const u32 ka = (a < b) ? a : b;
	const u32 kb = (a < b) ? b : a;
	u32 low = 0;
	u32 high = s->edgeCount;

	while (low < high)
	{
		const u32 mid = low + (high - low) / 2u;
		const struct RldMkEdge *e = &s->edges[mid];

		if ((e->a < ka) || ((e->a == ka) && (e->b < kb)))
		{
			low = mid + 1u;
		}
		else
		{
			high = mid;
		}
	}

	while ((low < s->edgeCount) && (s->edges[low].a == ka) && (s->edges[low].b == kb))
	{
		if (!s->used[s->edges[low].triangle])
		{
			return s->edges[low].triangle;
		}
		low++;
	}

	return RLDMK_NONE;
}

static u32 RldMk_Third(const struct RldMkStripper *s, u32 triangle, u32 a, u32 b)
{
	int k;

	for (k = 0; k < 3; k++)
	{
		const u32 corner = s->triangleCorner[3u * triangle + (u32)k];

		if ((corner != a) && (corner != b))
		{
			return corner;
		}
	}

	return RLDMK_NONE;
}

// 0 when (g0, g1, g2) runs the same way round as the source face, 1 when not.
static u32 RldMk_Parity(const u32 *source, u32 g0, u32 g1, u32 g2)
{
	int k;

	for (k = 0; k < 3; k++)
	{
		if ((source[k] == g0) && (source[(k + 1) % 3] == g1) && (source[(k + 2) % 3] == g2))
		{
			return 0u;
		}
	}

	return 1u;
}

static u32 RldMk_EmitCorner(struct RldMkStripper *s, u32 corner, u32 flags)
{
	struct RldMkBuild *b = s->build;
	const u32 position = s->cornerPosition[corner];
	u32 slot;

	if (s->positionSlot[position] != RLDMK_NONE)
	{
		slot = s->positionSlot[position];
		flags |= RLDMK_CMD_CACHED;
	}
	else
	{
		const u32 index = s->next;
		const u32 old = s->slotPosition[index];

		s->next = (s->next + 1u) % b->slotCount;
		if (old != RLDMK_NONE)
		{
			s->positionSlot[old] = RLDMK_NONE;
		}
		slot = b->slotLow + index;
		s->slotPosition[index] = position;
		s->positionSlot[position] = slot;
		b->record[b->recordCount++] = position;
	}

	b->command[b->commandCount] = flags | RLDMK_CMD_SCRATCH | (slot << RLDMK_SLOT_SHIFT) | (s->cornerColor[corner] << RLDMK_COLOR_SHIFT);
	return b->commandCount++;
}

// One triangle drawn by `command`, its corners in window order g0, g1, g2.
static void RldMk_DrawTriangle(struct RldMkStripper *s, u32 triangle, u32 command, u32 g0, u32 g1, u32 g2)
{
	struct RldMkBuild *b = s->build;
	const u32 parity = RldMk_Parity(&s->triangleCorner[3u * triangle], g0, g1, g2);
	const u32 d = b->drawCount++;

	s->used[triangle] = 1;

	if (s->cull)
	{
		b->command[command] |= RLDMK_CMD_CULL | (((parity ^ RLDMK_CULL_SIGN) != 0u) ? RLDMK_CMD_SIDE : 0u);
	}

	b->drawTriangle[d] = triangle;
	b->drawOrder[3u * d + 0u] = s->cornerPosition[g0];
	b->drawOrder[3u * d + 1u] = s->cornerPosition[g1];
	b->drawOrder[3u * d + 2u] = s->cornerPosition[g2];
}

static int RldMk_StepCommands(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	struct RldMkStripper s;
	u32 *keys;
	u32 cornerCount = 0;
	const u32 corners = 3u * b->triangleCount;
	u32 i;
	u32 t;
	int ok = 1;

	memset(&s, 0, sizeof(s));
	s.build = b;
	s.cull = !opt->twoSided;

	b->slotLow = (b->colorCount + 1u) / 2u;
	b->slotCount = (RLDCHAR_SLOT_MAX + 1u) - b->slotLow;

	keys = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	s.triangleCorner = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	s.cornerPosition = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	s.cornerColor = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	s.edges = (struct RldMkEdge *)malloc(((size_t)corners + 1u) * sizeof(struct RldMkEdge));
	s.used = (u8 *)calloc((size_t)b->triangleCount + 1u, 1u);
	s.positionSlot = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	s.slotPosition = (u32 *)malloc(((size_t)RLDCHAR_SLOT_MAX + 1u) * sizeof(u32));
	b->command = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	b->record = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	b->drawTriangle = (u32 *)malloc(((size_t)b->triangleCount + 1u) * sizeof(u32));
	b->drawOrder = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));

	if ((keys == NULL) || (s.triangleCorner == NULL) || (s.cornerPosition == NULL) || (s.cornerColor == NULL) || (s.edges == NULL) || (s.used == NULL) ||
	    (s.positionSlot == NULL) || (s.slotPosition == NULL) || (b->command == NULL) || (b->record == NULL) || (b->drawTriangle == NULL) ||
	    (b->drawOrder == NULL))
	{
		ok = RldMk_OutOfMemory();
		goto done;
	}

	// The corners: position x 128 + color, numbered in that order.
	for (i = 0; i < corners; i++)
	{
		keys[i] = b->vertexPosition[b->triangle[i]] * 128u + b->cornerColor[i];
	}
	qsort(keys, corners, sizeof(u32), RldMk_CompareU32);
	for (i = 0; i < corners; i++)
	{
		if ((i == 0u) || (keys[i] != keys[i - 1u]))
		{
			s.cornerPosition[cornerCount] = keys[i] / 128u;
			s.cornerColor[cornerCount] = keys[i] % 128u;
			keys[cornerCount] = keys[i];
			cornerCount++;
		}
	}
	for (i = 0; i < corners; i++)
	{
		const u32 key = b->vertexPosition[b->triangle[i]] * 128u + b->cornerColor[i];
		u32 low = 0;
		u32 high = cornerCount;

		while (low < high)
		{
			const u32 mid = low + (high - low) / 2u;

			if (keys[mid] < key)
			{
				low = mid + 1u;
			}
			else
			{
				high = mid;
			}
		}
		s.triangleCorner[i] = low;
	}

	for (t = 0; t < b->triangleCount; t++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			const u32 a = s.triangleCorner[3u * t + (u32)k];
			const u32 c = s.triangleCorner[3u * t + (u32)((k + 1) % 3)];
			struct RldMkEdge *e = &s.edges[3u * t + (u32)k];

			e->a = (a < c) ? a : c;
			e->b = (a < c) ? c : a;
			e->triangle = t;
		}
	}
	s.edgeCount = corners;
	qsort(s.edges, s.edgeCount, sizeof(struct RldMkEdge), RldMk_CompareEdge);

	for (i = 0; i < b->positionCount; i++)
	{
		s.positionSlot[i] = RLDMK_NONE;
	}
	for (i = 0; i <= (u32)RLDCHAR_SLOT_MAX; i++)
	{
		s.slotPosition[i] = RLDMK_NONE;
	}

	while (b->drawCount < b->triangleCount)
	{
		u32 start = RLDMK_NONE;
		int bestScore = -1;
		u32 rotation = 0;
		const u32 *source;
		u32 s0;
		u32 s1;
		u32 s2;
		u32 first;
		int r;

		// Start where the cache still holds the most corners; lowest index on a tie.
		for (t = 0; t < b->triangleCount; t++)
		{
			int score = 0;
			int k;

			if (s.used[t])
			{
				continue;
			}

			for (k = 0; k < 3; k++)
			{
				if (s.positionSlot[s.cornerPosition[s.triangleCorner[3u * t + (u32)k]]] != RLDMK_NONE)
				{
					score++;
				}
			}

			if (score > bestScore)
			{
				bestScore = score;
				start = t;
				if (score == 3)
				{
					break;
				}
			}
		}

		source = &s.triangleCorner[3u * start];
		s.used[start] = 1;

		// The source order turned so that its last edge leads on (a strip step),
		// else so that the fan edge does; the first triangle then always runs
		// like its source face.
		for (r = 2; r >= 0; r--)
		{
			if (RldMk_Neighbor(&s, source[(r + 1) % 3], source[(r + 2) % 3]) != RLDMK_NONE)
			{
				rotation = (u32)r;
			}
		}
		if (RldMk_Neighbor(&s, source[(rotation + 1u) % 3u], source[(rotation + 2u) % 3u]) == RLDMK_NONE)
		{
			for (r = 2; r >= 0; r--)
			{
				if (RldMk_Neighbor(&s, source[r], source[(r + 2) % 3]) != RLDMK_NONE)
				{
					rotation = (u32)r;
				}
			}
		}

		s0 = source[rotation];
		s1 = source[(rotation + 1u) % 3u];
		s2 = source[(rotation + 2u) % 3u];

		first = RldMk_EmitCorner(&s, s0, RLDMK_CMD_STRIP);
		RldMk_EmitCorner(&s, s1, 0u);
		RldMk_EmitCorner(&s, s2, 0u);
		RldMk_DrawTriangle(&s, start, first, s0, s1, s2);
		b->strips++;

		for (;;)
		{
			u32 next = RldMk_Neighbor(&s, s1, s2);
			u32 w;
			u32 command;

			if (next != RLDMK_NONE)
			{
				w = RldMk_Third(&s, next, s1, s2);
				command = RldMk_EmitCorner(&s, w, 0u);
				RldMk_DrawTriangle(&s, next, command, s1, s2, w);
				s0 = s1;
				s1 = s2;
				s2 = w;
				continue;
			}

			next = RldMk_Neighbor(&s, s0, s2);
			if (next != RLDMK_NONE)
			{
				w = RldMk_Third(&s, next, s0, s2);
				command = RldMk_EmitCorner(&s, w, RLDMK_CMD_FAN);
				RldMk_DrawTriangle(&s, next, command, s0, s2, w);
				b->fanSteps++;
				s1 = s2;
				s2 = w;
				continue;
			}

			break;
		}
	}

done:
	free(keys);
	free(s.triangleCorner);
	free(s.cornerPosition);
	free(s.cornerColor);
	free(s.edges);
	free(s.used);
	free(s.positionSlot);
	free(s.slotPosition);
	return ok;
}

//========================================================================================
// STEPS 11-13: FRAMES, MODEL, HEADER, POINTER MAP -> CMDL
//========================================================================================
//
// The body, every part on a multiple of 4:
//   0x00 Model, 0x18 the one ModelHeader, 0x58 the table of 4 ModelAnim pointers,
//   0x68 the command list (word 0 = color count, the commands, 0xFFFFFFFF),
//   the colors, then per animation its ModelAnim and its raw frames.
// A raw frame (ptrDeltaArray 0, RB:2619-2626) is the 0x1C-byte ModelFrame head
// and 3 bytes per record, padded to a multiple of 4 (game/LOAD/LOAD_Assets.c:69,
// :100 need the frame heads aligned; the padding is never read, RB:2202-2204).
// The pointer map lists the 8 fields that are not 0, each once.

struct RldMkLayout
{
	u32 colorCount;
	const u32 *colors;      // 0x00BBGGRR
	u32 commandCount;
	const u32 *commands;
	u32 recordCount;
	int scale[3];
	const int *origin;      // 3 per frame, RLDMK_FRAMES_TOTAL frames
	const u8 *records;      // per frame recordCount x 3 bytes: X, Z, Y
};

static u32 RldMk_FrameSize(u32 records)
{
	return (RLDMK_FRAME_HEADER + RLDMK_RECORD_BYTES * records + 3u) & ~3u;
}

static u8 *RldMk_WriteCmdl(const struct RldMkLayout *in, size_t *sizeOut)
{
	static const u32 mapEntries[RLDMK_MAP_ENTRIES] = {
		RLDMK_MODEL_HEADERS,
		RLDMK_BODY_HEADER + RLDMK_HEADER_COMMANDS,
		RLDMK_BODY_HEADER + RLDMK_HEADER_COLORS,
		RLDMK_BODY_HEADER + RLDMK_HEADER_ANIMS,
		RLDMK_BODY_ANIM_TABLE + 0u,
		RLDMK_BODY_ANIM_TABLE + 4u,
		RLDMK_BODY_ANIM_TABLE + 8u,
		RLDMK_BODY_ANIM_TABLE + 12u,
	};
	const u32 frameSize = RldMk_FrameSize(in->recordCount);
	const u32 colorsAt = RLDMK_BODY_COMMANDS + 4u * (in->commandCount + 2u);
	u32 animAt[RLDCHAR_ANIM_COUNT + 1];
	u32 bodySize;
	u32 frameIndex = 0;
	size_t size;
	u8 *out;
	u8 *body;
	u8 *header;
	u8 *map;
	u32 i;
	int a;

	*sizeOut = 0;

	animAt[0] = colorsAt + 4u * in->colorCount;
	for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
	{
		animAt[a + 1] = animAt[a] + RLDMK_ANIM_BYTES + (u32)s_rldCharFrames[a] * frameSize;
	}
	bodySize = animAt[RLDCHAR_ANIM_COUNT];

	size = 8u + (size_t)bodySize + 4u * RLDMK_MAP_ENTRIES;
	out = (u8 *)calloc(1, size);
	if (out == NULL)
	{
		return NULL;
	}

	Rld_WriteLE32(&out[0], bodySize);
	body = &out[4];

	// Model (model-head): a name of its own, id -1, one header.
	memcpy(&body[0], RLDMK_MODEL_NAME_TEXT, strlen(RLDMK_MODEL_NAME_TEXT));
	Rld_WriteLE16(&body[RLDMK_MODEL_ID], 0xFFFFu);
	Rld_WriteLE16(&body[RLDMK_MODEL_NUM_HEADERS], 1u);
	Rld_WriteLE32(&body[RLDMK_MODEL_HEADERS], RLDMK_BODY_HEADER);

	// ModelHeader (model-headers, model-lod, model-zero): maxDistanceLOD 8192 as the retail drivers,
	// flags 0, no static frame, no texture table, no texture animation.
	header = &body[RLDMK_BODY_HEADER];
	memcpy(&header[0], RLDMK_HEADER_NAME_TEXT, strlen(RLDMK_HEADER_NAME_TEXT));
	Rld_WriteLE16(&header[RLDMK_HEADER_LOD], (u32)RLDCHAR_LOD_MIN);
	for (a = 0; a < 3; a++)
	{
		Rld_WriteLE16(&header[RLDMK_HEADER_SCALE + 2u * (u32)a], (u32)in->scale[a]);
	}
	Rld_WriteLE32(&header[RLDMK_HEADER_COMMANDS], RLDMK_BODY_COMMANDS);
	Rld_WriteLE32(&header[RLDMK_HEADER_COLORS], colorsAt);
	Rld_WriteLE32(&header[RLDMK_HEADER_NUM_ANIMS], (u32)RLDCHAR_ANIM_COUNT);
	Rld_WriteLE32(&header[RLDMK_HEADER_ANIMS], RLDMK_BODY_ANIM_TABLE);

	for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
	{
		Rld_WriteLE32(&body[RLDMK_BODY_ANIM_TABLE + 4u * (u32)a], animAt[a]);
	}

	Rld_WriteLE32(&body[RLDMK_BODY_COMMANDS], in->colorCount);
	for (i = 0; i < in->commandCount; i++)
	{
		Rld_WriteLE32(&body[RLDMK_BODY_COMMANDS + 4u + 4u * i], in->commands[i]);
	}
	Rld_WriteLE32(&body[RLDMK_BODY_COMMANDS + 4u + 4u * in->commandCount], RLDMK_CMD_END);

	for (i = 0; i < in->colorCount; i++)
	{
		Rld_WriteLE32(&body[colorsAt + 4u * i], in->colors[i]);
	}

	for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
	{
		u8 *anim = &body[animAt[a]];
		u32 k;

		memcpy(anim, s_rldMkAnimNames[a], strlen(s_rldMkAnimNames[a]));
		Rld_WriteLE16(&anim[RLDMK_ANIM_NUM_FRAMES], (u32)s_rldCharFrames[a]);
		Rld_WriteLE16(&anim[RLDMK_ANIM_FRAME_SIZE], frameSize);

		for (k = 0; k < (u32)s_rldCharFrames[a]; k++)
		{
			u8 *frame = &anim[RLDMK_ANIM_BYTES + k * frameSize];
			const int *origin = &in->origin[3u * frameIndex];

			Rld_WriteLE16(&frame[0], (u32)origin[0]);
			Rld_WriteLE16(&frame[2], (u32)origin[1]);
			Rld_WriteLE16(&frame[4], (u32)origin[2]);
			Rld_WriteLE32(&frame[RLDMK_FRAME_VERTEX_OFFSET], RLDMK_FRAME_HEADER);
			if (in->recordCount != 0u)
			{
				memcpy(&frame[RLDMK_FRAME_HEADER], &in->records[(size_t)frameIndex * in->recordCount * 3u], (size_t)in->recordCount * 3u);
			}
			frameIndex++;
		}
	}

	map = &out[4u + (size_t)bodySize];
	Rld_WriteLE32(&map[0], 4u * RLDMK_MAP_ENTRIES);
	for (i = 0; i < RLDMK_MAP_ENTRIES; i++)
	{
		Rld_WriteLE32(&map[4u + 4u * i], mapEntries[i]);
	}

	*sizeOut = size;
	return out;
}

static int RldMk_StepWrite(struct RldMkBuild *b)
{
	struct RldMkLayout layout;
	int origins[3 * RLDMK_FRAMES_TOTAL];
	u8 *records;
	u32 f;
	u32 r;

	b->frameSize = RldMk_FrameSize(b->recordCount);
	if (b->frameSize > 0xFFFFu)
	{
		RldMk_Say("error", "model-frame", NULL, "One animation frame needs %u bytes; at most 65535 fit. Reduce the model.", b->frameSize);
		return 0;
	}

	records = (u8 *)malloc((size_t)RLDMK_FRAMES_TOTAL * b->recordCount * 3u + 1u);
	if (records == NULL)
	{
		return RldMk_OutOfMemory();
	}

	for (f = 0; f < RLDMK_FRAMES_TOTAL; f++)
	{
		origins[3u * f + 0u] = b->origin[0];
		origins[3u * f + 1u] = b->origin[1];
		origins[3u * f + 2u] = b->origin[2];

		for (r = 0; r < b->recordCount; r++)
		{
			memcpy(&records[((size_t)f * b->recordCount + r) * 3u], &b->quant[((size_t)f * b->positionCount + b->record[r]) * 3u], 3u);
		}
	}

	memset(&layout, 0, sizeof(layout));
	layout.colorCount = b->colorCount;
	layout.colors = b->palette;
	layout.commandCount = b->commandCount;
	layout.commands = b->command;
	layout.recordCount = b->recordCount;
	layout.scale[0] = b->scale[0];
	layout.scale[1] = b->scale[1];
	layout.scale[2] = b->scale[2];
	layout.origin = origins;
	layout.records = records;

	b->cmdl = RldMk_WriteCmdl(&layout, &b->cmdlSize);
	free(records);
	if (b->cmdl == NULL)
	{
		return RldMk_OutOfMemory();
	}

	b->drawBytes = b->drawCount * RLDCHAR_DRAW_BYTES_G3;

	if (b->cmdlSize > RLDCHAR_LIMIT_CMDL)
	{
		RldMk_Say("error", "mem-model", NULL, "The model is %u KB; a character model may be at most 256 KB. Reduce the triangles.",
		          (u32)((b->cmdlSize + 1023u) / 1024u));
		return 0;
	}

	return 1;
}

//========================================================================================
// STEP 15: THE COUNTER-CHECK
//========================================================================================

// Read back by the renderer's rule (RB:2437-2467): X = (byte0 + (pos.x & 0x7fff))
// << 2 in 16 bits, Y = ((byte2 + pos.y) << 2) with bit 0 of the sum cleared
// (mask 0xfff8ffff), Z = (pos.z + byte1) << 2; game units = that x scale / 16384.
// Each record against the position it stands for, in every frame: X and Z within
// half a step, Y within half a double step. 0 = a record is off, *badOut says which.
static int RldMk_ReadBack(const struct RldMkBuild *b, u32 *badOut)
{
	const u8 *body = &b->cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 table = Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS]);
	int scale[3];
	u32 frameIndex = 0;
	int a;

	*badOut = 0;
	for (a = 0; a < 3; a++)
	{
		const u32 raw = Rld_ReadLE16(&body[header + RLDMK_HEADER_SCALE + 2u * (u32)a]);

		scale[a] = (raw >= 0x8000u) ? ((int)raw - 0x10000) : (int)raw;
	}

	for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
	{
		const u32 anim = Rld_ReadLE32(&body[table + 4u * (u32)a]);
		const u32 frames = Rld_ReadLE16(&body[anim + RLDMK_ANIM_NUM_FRAMES]) & 0x7FFFu;
		const u32 frameSize = Rld_ReadLE16(&body[anim + RLDMK_ANIM_FRAME_SIZE]);
		u32 k;

		for (k = 0; k < frames; k++)
		{
			const u8 *frame = &body[anim + RLDMK_ANIM_BYTES + k * frameSize];
			const u32 posX = Rld_ReadLE16(&frame[0]);
			const u32 posY = Rld_ReadLE16(&frame[2]);
			const u32 posZ = Rld_ReadLE16(&frame[4]);
			const u8 *records = &frame[Rld_ReadLE32(&frame[RLDMK_FRAME_VERTEX_OFFSET])];
			u32 r;

			for (r = 0; r < b->recordCount; r++)
			{
				const u8 *q = &records[3u * r];
				const u32 originXY = (posX & 0x7FFFu) | (posY << 16);
				const u32 vertexXZ = (u32)q[0] | ((u32)q[2] << 16);
				const u32 packedXY = ((vertexXZ + originXY) << 2) & 0xFFF8FFFFu;
				const u32 packedZ = (posZ + (u32)q[1]) << 2;
				const int vx = (int)(packedXY & 0xFFFFu) - (((packedXY & 0x8000u) != 0u) ? 0x10000 : 0);
				const int vy = (int)(packedXY >> 16) - (((packedXY & 0x80000000u) != 0u) ? 0x10000 : 0);
				const int vz = (int)(packedZ & 0xFFFFu) - (((packedZ & 0x8000u) != 0u) ? 0x10000 : 0);
				const double *exact = RldMk_At(b, frameIndex, b->record[r]);
				double got[3];
				double tolerance[3];
				int axis;

				got[0] = (double)vx * (double)scale[0] / 16384.0;
				got[1] = (double)vy * (double)scale[1] / 16384.0;
				got[2] = (double)vz * (double)scale[2] / 16384.0;
				tolerance[0] = (0.5 + 1.0e-6) * (double)scale[0] / 4096.0;
				tolerance[1] = (1.0 + 1.0e-6) * (double)scale[1] / 4096.0;
				tolerance[2] = (0.5 + 1.0e-6) * (double)scale[2] / 4096.0;

				for (axis = 0; axis < 3; axis++)
				{
					const double d = got[axis] - exact[axis];

					if ((d > tolerance[axis]) || (d < -tolerance[axis]))
					{
						*badOut = frameIndex * b->recordCount + r;
						return 0;
					}
				}
			}

			frameIndex++;
		}
	}

	return 1;
}

// model-winding: the GTE window replayed from the written command words
// exactly as RenderBucket_DrawFunc_Normal moves it (RB:4630-4672, 2826-2844),
// with the positions the records and the cache slots stand for. Every triangle
// has to be the one the builder meant, carry the cull bit (unless both sides are
// drawn), and the side its cull bits let through (RB:2884-2899, RLDMK_CULL_SIGN)
// has to be the side the normal of its source face points to. 0 = not so,
// *badOut is the triangle in draw order.
static int RldMk_Winding(const struct RldMkBuild *b, int twoSided, u32 *badOut)
{
	const u8 *body = &b->cmdl[4];
	const u32 bodySize = Rld_ReadLE32(b->cmdl);
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	u32 at = Rld_ReadLE32(&body[header + RLDMK_HEADER_COMMANDS]) + 4u;
	u32 slotPosition[256];
	u32 window[4];
	u32 fifo[3];
	u32 stripLength = 0;
	u32 prim = 0;
	u32 vertexIndex = 0;
	u32 drawn = 0;
	int ended = 0;
	u32 i;

	*badOut = 0;
	for (i = 0; i < 256u; i++)
	{
		slotPosition[i] = RLDMK_NONE;
	}
	for (i = 0; i < 4u; i++)
	{
		window[i] = RLDMK_NONE;
	}
	fifo[0] = RLDMK_NONE;
	fifo[1] = RLDMK_NONE;
	fifo[2] = RLDMK_NONE;

	while ((at + 4u) <= bodySize)
	{
		const u32 command = Rld_ReadLE32(&body[at]);
		const u32 flags = command >> 24;
		const u32 slot = (command >> 16) & 0xFFu;
		u32 position;
		u32 draw;
		int useRtps;
		int reuse;

		at += 4u;
		if (command == RLDMK_CMD_END)
		{
			ended = 1;
			break;
		}
		if ((command >> 16) == 0u)
		{
			continue;
		}

		if ((flags & 0x04u) != 0u)
		{
			position = slotPosition[slot];
		}
		else
		{
			if (vertexIndex >= b->recordCount)
			{
				*badOut = drawn;
				return 0;
			}
			position = b->record[vertexIndex++];
			slotPosition[slot] = position;
		}

		if (position == RLDMK_NONE)
		{
			*badOut = drawn;
			return 0;
		}

		window[0] = window[1];
		window[1] = window[2];
		window[2] = window[3];
		window[3] = position;

		if (((flags & 0x80u) != 0u) || (stripLength == 0u))
		{
			prim = command;
		}
		if ((flags & 0x80u) != 0u)
		{
			stripLength = 0;
		}

		useRtps = (stripLength > 2u);
		reuse = useRtps && ((flags & 0x40u) != 0u);
		if (reuse)
		{
			window[1] = window[0];
		}

		if (stripLength < 2u)
		{
			stripLength++;
			continue;
		}

		draw = (stripLength == 2u) ? prim : command;
		if (!useRtps)
		{
			// RTPT: the strip's three corners (RB:2812-2824)
			fifo[0] = window[1];
			fifo[1] = window[2];
			fifo[2] = window[3];
		}
		else if (reuse)
		{
			// SXY1 := SXY0, then RTPS shifts: corner 0 stays (RB:2828-2838)
			fifo[1] = fifo[2];
			fifo[2] = position;
		}
		else
		{
			fifo[0] = fifo[1];
			fifo[1] = fifo[2];
			fifo[2] = position;
		}
		stripLength++;

		if ((drawn >= b->drawCount) || (fifo[0] != b->drawOrder[3u * drawn + 0u]) || (fifo[1] != b->drawOrder[3u * drawn + 1u]) ||
		    (fifo[2] != b->drawOrder[3u * drawn + 2u]))
		{
			*badOut = drawn;
			return 0;
		}

		if (!twoSided)
		{
			const u32 *source = &b->triangle[3u * b->drawTriangle[drawn]];
			const u32 side = ((draw & RLDMK_CMD_SIDE) != 0u) ? 1u : 0u;
			double n[3];
			double m[3];
			double facing;

			if ((draw & RLDMK_CMD_CULL) == 0u)
			{
				*badOut = drawn;
				return 0;
			}

			RldMk_Cross(RldMk_At(b, RLDMK_NEUTRAL_FRAME, fifo[0]), RldMk_At(b, RLDMK_NEUTRAL_FRAME, fifo[1]), RldMk_At(b, RLDMK_NEUTRAL_FRAME, fifo[2]), n);
			RldMk_Cross(RldMk_At(b, RLDMK_NEUTRAL_FRAME, b->vertexPosition[source[0]]), RldMk_At(b, RLDMK_NEUTRAL_FRAME, b->vertexPosition[source[1]]),
			            RldMk_At(b, RLDMK_NEUTRAL_FRAME, b->vertexPosition[source[2]]), m);

			// The side the triangle is drawn from: its window normal when 0x20
			// equals RLDMK_CULL_SIGN, the other side when not.
			facing = RldMk_Dot(n, m);
			if (side != RLDMK_CULL_SIGN)
			{
				facing = -facing;
			}
			if (facing <= 0.0)
			{
				*badOut = drawn;
				return 0;
			}
		}

		drawn++;
	}

	if (!ended || (drawn != b->drawCount) || (vertexIndex != b->recordCount))
	{
		*badOut = drawn;
		return 0;
	}

	return 1;
}

static int RldMk_ReportFinding(void *context, const struct RldCharFinding *finding)
{
	(void)context;
	RldMk_Say("error", finding->rule, finding->detail, "Internal error: the built model breaks the game's rule %s (%s). Please report it.", finding->rule,
	          RldChar_VerdictWord(finding->verdict));
	return 1;
}

// Step 15 on a built model. 0 = something is wrong, it was said.
static int RldMk_CheckBuilt(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	struct RldCharFinding first;
	struct RldCharModelFacts facts;
	u32 bad = 0;
	int ok = 1;

	if (RldChar_CheckModel(b->cmdl, b->cmdlSize, &first, RldMk_ReportFinding, NULL, &facts) != RLDCHAR_VERDICT_OK)
	{
		ok = 0;
	}
	else if ((facts.triangles != b->drawCount) || (facts.records != b->recordCount) || (facts.drawBytes != b->drawBytes) ||
	         (facts.colorCount != b->colorCount))
	{
		char technical[160];

		snprintf(technical, sizeof(technical), "the check counts %u triangles, %u records, %u colors; the builder %u, %u, %u", facts.triangles,
		         facts.records, facts.colorCount, b->drawCount, b->recordCount, b->colorCount);
		RldMk_Say("error", "model-roundtrip", technical, "Internal error: the packed model does not read back the same (vertex %u). Please report it.", 0u);
		ok = 0;
	}

	if (!RldMk_ReadBack(b, &bad))
	{
		RldMk_Say("error", "model-roundtrip", NULL, "Internal error: the packed model does not read back the same (vertex %u). Please report it.", bad);
		ok = 0;
	}

	if (!RldMk_Winding(b, opt->twoSided, &bad))
	{
		RldMk_Say("error", "model-winding", NULL, "Internal error: triangle %u would be culled from the wrong side. Please report it.", bad);
		ok = 0;
	}

	return ok;
}

//========================================================================================
// THE CHAIN
//========================================================================================

// Steps 2-13 and 15 on a PLY that was read. 0 = the model cannot be built; every
// reason was said.
static int RldMk_BuildModel(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b)
{
	u32 keyCount = 0;

	memset(b, 0, sizeof(*b));
	b->vertexCount = ply->vertexCount;
	b->faceCount = ply->faceCount;
	b->kartPart = -1;
	b->driverPart = -1;

	if (!RldMk_StepWeld(ply, opt, b, &keyCount) || !RldMk_StepTriangles(ply, b, keyCount))
	{
		return 0;
	}

	RldMk_StepOpenEdges(b);

	if (!RldMk_StepParts(b, opt) || !RldMk_StepBudget(b, opt) || !RldMk_StepPoses(b) || !RldMk_StepQuantize(b) || !RldMk_StepCollapse(b) ||
	    !RldMk_StepColors(ply, opt, b) || !RldMk_StepCommands(b, opt) || !RldMk_StepWrite(b))
	{
		return 0;
	}

	return RldMk_CheckBuilt(b, opt);
}

// "0,3,5-9": the palette indices a group's triangles use.
static void RldMk_PartColors(const struct RldMkBuild *b, int group, char *out, size_t size)
{
	u8 used[RLDCHAR_COLORS_MAX];
	size_t at = 0;
	u32 t;
	u32 i;

	memset(used, 0, sizeof(used));
	out[0] = '\0';

	for (t = 0; t < b->triangleCount; t++)
	{
		if (b->positionGroup[b->vertexPosition[b->triangle[3u * t]]] == (u8)group)
		{
			used[b->cornerColor[3u * t + 0u]] = 1;
			used[b->cornerColor[3u * t + 1u]] = 1;
			used[b->cornerColor[3u * t + 2u]] = 1;
		}
	}

	i = 0;
	while (i < b->colorCount)
	{
		u32 end = i;

		if (!used[i])
		{
			i++;
			continue;
		}

		while (((end + 1u) < b->colorCount) && used[end + 1u])
		{
			end++;
		}

		if (at < size)
		{
			if (end == i)
			{
				snprintf(&out[at], size - at, "%s%u", (at != 0u) ? "," : "", i);
			}
			else
			{
				snprintf(&out[at], size - at, "%s%u-%u", (at != 0u) ? "," : "", i, end);
			}
			at = strlen(out);
		}

		i = end + 1u;
	}
}

//========================================================================================
// STEP 16: CHRI AND THE CONTAINER
//========================================================================================

// CHRI 1.0 (include/rldchar.inc, RldChar_ParseInfo reads it): fixedSize 0x1C,
// template, class, the 20-byte name, charVersion, then one string: the author.
static u8 *RldMk_BuildChri(u32 templateId, u32 classId, const u8 name[RLDCHAR_NAME_FIELD], u32 charVersion, const char *author, size_t *sizeOut)
{
	const size_t length = strlen(author);
	const size_t size = RLDCHAR_INFO_FIXED_SIZE + 4u + 4u + length;
	u8 *data = (u8 *)calloc(1, size);

	*sizeOut = 0;
	if (data == NULL)
	{
		return NULL;
	}

	Rld_WriteLE16(&data[0x00], RLDCHAR_INFO_FIXED_SIZE);
	data[0x02] = (u8)templateId;
	data[0x03] = (u8)classId;
	memcpy(&data[0x04], name, RLDCHAR_NAME_FIELD);
	Rld_WriteLE32(&data[0x18], charVersion);
	Rld_WriteLE32(&data[RLDCHAR_INFO_FIXED_SIZE], 1u);
	Rld_WriteLE32(&data[RLDCHAR_INFO_FIXED_SIZE + 4u], (u32)length);
	memcpy(&data[RLDCHAR_INFO_FIXED_SIZE + 8u], author, length);

	*sizeOut = size;
	return data;
}

// CHRI and CMDL through the shared envelope (Rld_WriteEnvelope, rldtrack.inc).
static u8 *RldMk_Container(const u8 *chri, size_t chriSize, const u8 *cmdl, size_t cmdlSize, size_t *sizeOut)
{
	struct RldContainer container;
	u8 *out;
	int i;

	memset(&container, 0, sizeof(container));
	Rld_AddChunk(&container, "CHRI", chri, chriSize, RLD_COMPRESSION_NONE);
	Rld_AddChunk(&container, "CMDL", cmdl, cmdlSize, RLD_COMPRESSION_NONE);

	out = Rld_WriteEnvelope(&s_rldCharFormat, 0u, &container, sizeOut);

	for (i = 0; i < container.chunkCount; i++)
	{
		free(container.chunks[i].stored);
	}

	return out;
}

//========================================================================================
// THE COMMAND: rldpack make-char
//========================================================================================

// @result failed for Reload Studio, once, whatever went wrong.
static int RldMk_Fail(int code)
{
	if (s_machine && !s_machineResult)
	{
		Rld_Emit("result", "failed", "", "0", "", (const char *)NULL);
		s_machineResult = 1;
	}
	return code;
}

// The display name: lower case becomes capitals (name-upper), then the game's
// own rule (RldChar_NameValid). 1 = usable, the 20-byte field is filled.
static int RldMk_Name(const char *text, u8 field[RLDCHAR_NAME_FIELD], char *shown, size_t shownSize)
{
	const size_t length = strlen(text);
	char bad[96];
	size_t badAt = 0;
	int lower = 0;
	const char *why;
	size_t i;

	memset(field, 0, RLDCHAR_NAME_FIELD);
	snprintf(shown, shownSize, "%s", text);
	bad[0] = '\0';

	for (i = 0; shown[i] != '\0'; i++)
	{
		if ((shown[i] >= 'a') && (shown[i] <= 'z'))
		{
			shown[i] = (char)(shown[i] - 'a' + 'A');
			lower = 1;
		}
	}

	if (lower)
	{
		RldMk_Say("info", "name-upper", NULL, "The name is shown in capitals: %s.", shown);
	}

	if ((length == 0u) || (length > (size_t)RLDCHAR_NAME_MAX))
	{
		RldMk_Say("error", "name", NULL, "The name must be 1 to 17 characters long (it has %u).", (u32)length);
		return 0;
	}

	for (i = 0; i < length; i++)
	{
		const u8 c = (u8)shown[i];
		char one[8];

		if (RldChar_NameCharAllowed(c))
		{
			continue;
		}

		if ((c >= 0x21u) && (c < 0x7Fu))
		{
			snprintf(one, sizeof(one), "%c", (char)c);
		}
		else
		{
			snprintf(one, sizeof(one), "\\x%02X", (u32)c);
		}

		if ((strstr(bad, one) == NULL) && ((badAt + strlen(one) + 2u) < sizeof(bad)))
		{
			snprintf(&bad[badAt], sizeof(bad) - badAt, "%s%s", (badAt != 0u) ? " " : "", one);
			badAt = strlen(bad);
		}
	}

	if (badAt != 0u)
	{
		RldMk_Say("error", "name-chars", NULL,
		          "The menu cannot draw these characters of the name: %s. Use A-Z, 0-9, spaces and ! %% ' + , - . / : < = > ? _", bad);
		return 0;
	}

	memcpy(field, shown, length);
	why = RldChar_NameValid(field);
	if (why != NULL)
	{
		RldMk_Say("error", "name", why, "The name must be 1 to 17 characters long (it has %u).", (u32)length);
		return 0;
	}

	if (length > 15u)
	{
		RldMk_Say("note", "name-long", NULL, "Names longer than 15 characters may not fit everywhere.");
	}

	return 1;
}

static int RldMk_ParseWhole(const char *text, u32 max, u32 *out)
{
	char *end = NULL;
	unsigned long value;

	if ((text == NULL) || (text[0] < '0') || (text[0] > '9'))
	{
		return 0;
	}

	value = strtoul(text, &end, 10);
	if ((*end != '\0') || (value > (unsigned long)max))
	{
		return 0;
	}

	*out = (u32)value;
	return 1;
}

// What the build found, as @char lines (Reload Studio shows them; the file
// carries none of these numbers). As far as the chain got.
static void RldMk_EmitFacts(const struct RldMkBuild *b, u32 templateId)
{
	char text[160];
	int g;

	if (!s_machine || s_rldMkQuiet)
	{
		return;
	}

	RldMk_Fact("vertices", "%u", b->vertexCount);
	RldMk_Fact("faces", "%u", b->faceCount);
	RldMk_Fact("quads", "%u", b->quads);
	if (b->triangleCount != 0u)
	{
		RldMk_Fact("triangles", "%u", b->triangleCount);
	}

	if (b->partCount != 0u)
	{
		char count[16];

		snprintf(count, sizeof(count), "%u", b->partCount);
		snprintf(text, sizeof(text), "kart %u, driver %u, steering wheel %u", b->groupTriangles[RLDMK_GROUP_KART], b->groupTriangles[RLDMK_GROUP_DRIVER],
		         b->groupTriangles[RLDMK_GROUP_WHEEL]);
		Rld_Emit("char", "parts", count, text, (const char *)NULL);
		RldMk_Fact("size", "%.2f %.2f %.2f %.2f", b->size[0], b->size[1], b->size[2], b->size[3]);
	}

	if (b->quant != NULL)
	{
		RldMk_Fact("scale", "%d %d %d", b->scale[0], b->scale[1], b->scale[2]);
	}

	if (b->cornerColor != NULL)
	{
		RldMk_Fact("colors_in", "%u", b->colorsIn);
		RldMk_Fact("colors_out", "%u", b->colorCount);
		RldMk_Fact("color_error", "%.1f %.1f", b->colorMean, b->colorMax);

		for (g = 0; g < RLDMK_GROUPS; g++)
		{
			if (b->groupTriangles[g] != 0u)
			{
				RldMk_PartColors(b, g, text, sizeof(text));
				Rld_Emit("char", "part_colors", s_rldMkGroupNames[g], text, (const char *)NULL);
			}
		}
	}

	if (b->cmdl != NULL)
	{
		RldMk_Fact("records", "%u", b->recordCount);
		RldMk_Fact("slots", "%u", b->slotCount);
		RldMk_Fact("frames", "%u,%u,%u,%u", (u32)s_rldCharFrames[0], (u32)s_rldCharFrames[1], (u32)s_rldCharFrames[2], (u32)s_rldCharFrames[3]);
		RldMk_Fact("draw_bytes", "%u", b->drawBytes);
		if (templateId < RLDMK_TEMPLATE_COUNT)
		{
			RldMk_Fact("draw_delta", "%d", (int)b->drawBytes - (int)(RLDMK_GT3_BYTES * s_rldMkTemplates[templateId].hiTriangles));
		}
	}
}

// The same, for people.
static void RldMk_PrintFacts(const struct RldMkBuild *b, const struct RldMkOptions *opt, u32 templateId)
{
	char text[160];
	int g;

	if (s_rldMkQuiet)
	{
		return;
	}

	printf("  model         %u vertices, %u faces (%u quads split), %u triangles\n", b->vertexCount, b->faceCount, b->quads, b->triangleCount);

	if (b->partCount != 0u)
	{
		printf("  parts         %u - kart %u, driver %u, steering wheel %u triangles\n", b->partCount, b->groupTriangles[RLDMK_GROUP_KART],
		       b->groupTriangles[RLDMK_GROUP_DRIVER], b->groupTriangles[RLDMK_GROUP_WHEEL]);
		printf("  size          %.2f long, %.2f wide, %.2f tall, %.2f above the ground (Blender units)\n", b->size[0], b->size[1], b->size[2], b->size[3]);
		printf("  poses         %s\n", b->posed ? "automatic - the driver leans when steering" : "still - all 47 frames the same");
	}

	if (b->cornerColor != NULL)
	{
		if (b->colorsIn > b->colorCount)
		{
			printf("  colors        %u -> %u (average change %.1f, largest %.1f of 255)\n", b->colorsIn, b->colorCount, b->colorMean, b->colorMax);
		}
		else
		{
			printf("  colors        %u, kept as they are\n", b->colorCount);
		}

		for (g = 0; g < RLDMK_GROUPS; g++)
		{
			if (b->groupTriangles[g] != 0u)
			{
				RldMk_PartColors(b, g, text, sizeof(text));
				printf("  part colors   %-7s %s\n", s_rldMkGroupNames[g], text);
			}
		}
	}

	if (b->cmdl != NULL)
	{
		printf("  scale         %d %d %d, origin %d %d %d (the same in every frame)\n", b->scale[0], b->scale[1], b->scale[2], b->origin[0], b->origin[1],
		       b->origin[2]);
		printf("  commands      %u in %u strips (%u fan steps), %u records, cache slots %u..87\n", b->commandCount, b->strips, b->fanSteps, b->recordCount,
		       b->slotLow);
		printf("  frames        %u,%u,%u,%u, %u bytes each\n", (u32)s_rldCharFrames[0], (u32)s_rldCharFrames[1], (u32)s_rldCharFrames[2],
		       (u32)s_rldCharFrames[3], b->frameSize);
		printf("  cull          %s\n", opt->twoSided ? "none - both sides are drawn" : "back faces, side per triangle");
		printf("  draw memory   %u bytes per frame (%u triangles x 28), at most %u\n", b->drawBytes, b->drawCount, RLDCHAR_DRAW_BYTES_MAX);
		if (templateId < RLDMK_TEMPLATE_COUNT)
		{
			printf("                %+d against %s (%u textured triangles)\n",
			       (int)b->drawBytes - (int)(RLDMK_GT3_BYTES * s_rldMkTemplates[templateId].hiTriangles), s_rldMkTemplates[templateId].name,
			       s_rldMkTemplates[templateId].hiTriangles);
		}
	}
}

static int RldChar_MakeCommand(int argc, char **argv)
{
	static char outPath[RLD_PATH_MAX + 16];
	static char shownName[64];
	const char *modelArg = NULL;
	const char *nameArg = NULL;
	const char *authorArg = NULL;
	const char *versionArg = NULL;
	const char *templateArg = NULL;
	const char *classArg = NULL;
	const char *posesArg = NULL;
	const char *colorsArg = NULL;
	const char *upArg = NULL;
	const char *forwardArg = NULL;
	const char *scaleArg = NULL;
	const char *outArg = NULL;
	int check = 0;
	int authorGiven;
	int built;
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild build;
	u8 nameField[RLDCHAR_NAME_FIELD];
	u32 templateId = RLDMK_NONE;
	u32 classId = RLDMK_NONE;
	u32 charVersion = 1;
	u8 *plyData = NULL;
	size_t plySize = 0;
	u8 *chri = NULL;
	size_t chriSize = 0;
	u8 *container = NULL;
	size_t containerSize = 0;
	char digestText[65];
	int i;

	memset(&opt, 0, sizeof(opt));
	memset(&ply, 0, sizeof(ply));
	memset(&build, 0, sizeof(build));
	memset(nameField, 0, sizeof(nameField));
	opt.colorTarget = 64u;
	opt.scale = 1.0;
	shownName[0] = '\0';
	digestText[0] = '\0';
	RldMk_ResetSaid();

	for (i = 0; i < argc; i++)
	{
		const char *arg = argv[i];
		const int hasValue = ((i + 1) < argc);

		if ((strcmp(arg, "--model") == 0) && hasValue)
		{
			modelArg = argv[++i];
		}
		else if ((strcmp(arg, "--name") == 0) && hasValue)
		{
			nameArg = argv[++i];
		}
		else if ((strcmp(arg, "--author") == 0) && hasValue)
		{
			authorArg = argv[++i];
		}
		else if ((strcmp(arg, "--char-version") == 0) && hasValue)
		{
			versionArg = argv[++i];
		}
		else if ((strcmp(arg, "--template") == 0) && hasValue)
		{
			templateArg = argv[++i];
		}
		else if ((strcmp(arg, "--class") == 0) && hasValue)
		{
			classArg = argv[++i];
		}
		else if ((strcmp(arg, "--poses") == 0) && hasValue)
		{
			posesArg = argv[++i];
		}
		else if ((strcmp(arg, "--colors") == 0) && hasValue)
		{
			colorsArg = argv[++i];
		}
		else if ((strcmp(arg, "--up") == 0) && hasValue)
		{
			upArg = argv[++i];
		}
		else if ((strcmp(arg, "--forward") == 0) && hasValue)
		{
			forwardArg = argv[++i];
		}
		else if ((strcmp(arg, "--scale") == 0) && hasValue)
		{
			scaleArg = argv[++i];
		}
		else if ((strcmp(arg, "--out") == 0) && hasValue)
		{
			outArg = argv[++i];
		}
		else if (strcmp(arg, "--two-sided") == 0)
		{
			opt.twoSided = 1;
		}
		else if (strcmp(arg, "--check") == 0)
		{
			check = 1;
		}
		else
		{
			RldMk_Say("error", "usage", arg, "rldpack does not know the argument \"%s\".", arg);
			fprintf(stderr, "\n");
			RldChar_Usage(stderr);
			return RldMk_Fail(2);
		}
	}

	if (modelArg == NULL)
	{
		RldMk_Say("error", "usage", NULL, "No model file was given.");
		fprintf(stderr, "\n");
		RldChar_Usage(stderr);
		return RldMk_Fail(2);
	}

	// THE SWITCHES. A wrong value is said and the checks go on, so that --check
	// shows everything at once; nothing is written at the end.
	if (colorsArg != NULL)
	{
		if (strcmp(colorsArg, "64") == 0)
		{
			opt.colorTarget = 64u;
		}
		else if (strcmp(colorsArg, "128") == 0)
		{
			opt.colorTarget = 128u;
		}
		else
		{
			RldMk_Say("error", "colors", colorsArg, "The number of colors must be 64 or 128.");
		}
	}

	if ((upArg != NULL) && (strcmp(upArg, "y") != 0) && (strcmp(upArg, "Y") != 0))
	{
		if ((strcmp(upArg, "z") == 0) || (strcmp(upArg, "Z") == 0))
		{
			opt.upZ = 1;
		}
		else
		{
			RldMk_Say("error", "up", upArg, "The up axis must be y or z.");
		}
	}

	if ((forwardArg != NULL) && (strcmp(forwardArg, "z") != 0) && (strcmp(forwardArg, "+z") != 0))
	{
		if (strcmp(forwardArg, "-z") == 0)
		{
			opt.backwards = 1;
		}
		else
		{
			RldMk_Say("error", "forward", forwardArg, "Forward must be z or -z.");
		}
	}

	if (scaleArg != NULL)
	{
		char *end = NULL;
		const double value = strtod(scaleArg, &end);

		if ((end == scaleArg) || (*end != '\0') || !(value > 0.0) || !(value <= 1000.0))
		{
			RldMk_Say("error", "scale", scaleArg, "The scale \"%s\" must be a number above 0 (1 = meters as exported).", scaleArg);
		}
		else
		{
			opt.scale = value;
		}
	}

	if ((posesArg != NULL) && (strcmp(posesArg, "auto") != 0))
	{
		if (strcmp(posesArg, "still") == 0)
		{
			opt.still = 1;
		}
		else
		{
			RldMk_Say("error", "poses", posesArg, "The poses must be auto or still.");
		}
	}

	if (templateArg == NULL)
	{
		RldMk_Say("error", "template", NULL, "Choose a template driver (Crash to Fake Crash).");
	}
	else if (!RldMk_ParseWhole(templateArg, 255u, &templateId) || (templateId >= RLDMK_TEMPLATE_COUNT))
	{
		if (templateId == (u32)RLDMK_TEMPLATE_COUNT)
		{
			RldMk_Say("error", "template", templateArg, "N. Oxide cannot be a template driver.");
		}
		else
		{
			RldMk_Say("error", "template", templateArg, "Choose a template driver (Crash to Fake Crash).");
		}
		templateId = RLDMK_NONE;
	}

	if ((classArg == NULL) || (strcmp(classArg, "like") == 0))
	{
		classId = (templateId < RLDMK_TEMPLATE_COUNT) ? s_rldMkTemplates[templateId].classId : RLDMK_NONE;
	}
	else
	{
		int c;

		for (c = 0; c < RLDMK_CLASS_COUNT; c++)
		{
			if (strcmp(classArg, s_rldMkClassNames[c]) == 0)
			{
				classId = (u32)c;
			}
		}

		if ((classId == RLDMK_NONE) && (!RldMk_ParseWhole(classArg, (u32)RLDCHAR_CLASS_MAX, &classId)))
		{
			classId = RLDMK_NONE;
			RldMk_Say("error", "class", classArg, "The class \"%s\" is not known. Use balanced, acceleration, speed or turning.", classArg);
		}
	}

	if (nameArg == NULL)
	{
		RldMk_Say("error", "name", NULL, "The name must be 1 to 17 characters long (it has %u).", 0u);
	}
	else
	{
		RldMk_Name(nameArg, nameField, shownName, sizeof(shownName));
	}

	authorGiven = (authorArg != NULL);
	if (authorArg == NULL)
	{
		authorArg = "";
	}
	if ((strlen(authorArg) > RLDCHAR_AUTHOR_MAX) || !Rld_IsUtf8(authorArg))
	{
		RldMk_Say("error", "author", authorArg, "The author name is longer than 64 bytes, or not valid text.");
	}

	if ((versionArg != NULL) && (!RldMk_ParseWhole(versionArg, 0xFFFFFFFFu, &charVersion) || (charVersion == 0u)))
	{
		RldMk_Say("error", "version", versionArg, "The version \"%s\" must be a whole number, 1 or higher.", versionArg);
		charVersion = 1;
	}

	// Next to the model, not into the game folder: make-char installs nothing.
	if (outArg != NULL)
	{
		snprintf(outPath, sizeof(outPath), "%s", outArg);
	}
	else
	{
		size_t length = strlen(modelArg);

		if (Rld_EndsWithNoCase(modelArg, ".ply") && (length > 4u))
		{
			length -= 4u;
		}
		if (length > (sizeof(outPath) - 9u))
		{
			length = sizeof(outPath) - 9u;
		}
		memcpy(outPath, modelArg, length);
		snprintf(&outPath[length], sizeof(outPath) - length, ".rldchar");
	}

	{
		char number[24];

		Rld_Emit("value", "name", shownName, (nameArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "author", authorArg, authorGiven ? "switch" : "default", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", charVersion);
		Rld_Emit("value", "char_version", number, (versionArg != NULL) ? "switch" : "default", (const char *)NULL);
		snprintf(number, sizeof(number), "%d", (templateId != RLDMK_NONE) ? (int)templateId : -1);
		Rld_Emit("value", "template", number, (templateArg != NULL) ? "switch" : "default", (const char *)NULL);
		snprintf(number, sizeof(number), "%d", (classId != RLDMK_NONE) ? (int)classId : -1);
		Rld_Emit("value", "class", number, ((classArg != NULL) && (strcmp(classArg, "like") != 0)) ? "switch" : "template", (const char *)NULL);
		Rld_Emit("value", "poses", opt.still ? "still" : "auto", (posesArg != NULL) ? "switch" : "default", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", opt.colorTarget);
		Rld_Emit("value", "colors", number, (colorsArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "up", opt.upZ ? "z" : "y", (upArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "forward", opt.backwards ? "-z" : "z", (forwardArg != NULL) ? "switch" : "default", (const char *)NULL);
		snprintf(number, sizeof(number), "%g", opt.scale);
		Rld_Emit("value", "scale", number, (scaleArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "two_sided", opt.twoSided ? "yes" : "no", opt.twoSided ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "out", outPath, (outArg != NULL) ? "switch" : "default", (const char *)NULL);
	}

	// THE MODEL.
	if (!Rld_ReadFile(modelArg, &plyData, &plySize))
	{
		Rld_Emit("file", "ply", "missing", modelArg, "0", (const char *)NULL);
		RldMk_Say("error", "ply-open", modelArg, "The model file %s cannot be opened.", modelArg);
		return RldMk_Fail(1);
	}

	{
		char bytes[24];

		snprintf(bytes, sizeof(bytes), "%u", (u32)plySize);
		Rld_Emit("file", "ply", "ok", modelArg, bytes, (const char *)NULL);
	}

	printf("rldpack make-char %s\n", modelArg);

	if (!RldMk_ParsePly(plyData, plySize, &ply))
	{
		free(plyData);
		return RldMk_Fail(1);
	}
	free(plyData);
	plyData = NULL;

	built = RldMk_BuildModel(&ply, &opt, &build);
	RldMk_EmitFacts(&build, templateId);
	RldMk_PrintFacts(&build, &opt, templateId);

	// BUILT TWICE: the same input must give the same bytes.
	if (built)
	{
		struct RldMkBuild again;
		const int quiet = s_rldMkQuiet;
		const int errorsBefore = s_rldMkErrors;
		int same;

		s_rldMkQuiet = 1;
		same = RldMk_BuildModel(&ply, &opt, &again) && (again.cmdlSize == build.cmdlSize) && (memcmp(again.cmdl, build.cmdl, build.cmdlSize) == 0);
		s_rldMkQuiet = quiet;
		s_rldMkErrors = errorsBefore;
		RldMk_FreeBuild(&again);

		if (!same)
		{
			RldMk_Say("error", "model-roundtrip", "two builds of the same model differ",
			          "Internal error: the packed model does not read back the same (vertex %u). Please report it.", 0u);
			built = 0;
		}
		else
		{
			printf("  checks        the game's model rules, read back, cull sides, built twice: all the same\n");
		}
	}

	if (!s_rldMkQuiet)
	{
		printf("  name          \"%s\"\n", shownName);
		printf("  author        \"%s\"\n", authorArg);
		printf("  version       %u\n", charVersion);
		if (templateId < RLDMK_TEMPLATE_COUNT)
		{
			printf("  template      %u %s\n", templateId, s_rldMkTemplates[templateId].name);
		}
		if (classId <= (u32)RLDCHAR_CLASS_MAX)
		{
			printf("  class         %u %s%s\n", classId, s_rldMkClassNames[classId],
			       ((classArg == NULL) || (strcmp(classArg, "like") == 0)) ? " (like the template)" : "");
		}
	}

	if (!built || (s_rldMkErrors != 0))
	{
		printf("\nnothing written - see the errors above\n");
		RldMk_FreeBuild(&build);
		RldMk_FreePly(&ply);
		return RldMk_Fail(1);
	}

	// CHRI, checked with the reader the game runs, and the container.
	chri = RldMk_BuildChri(templateId, classId, nameField, charVersion, authorArg, &chriSize);
	if (chri != NULL)
	{
		struct RldCharInfo back;
		const char *why = RldChar_ParseInfo(&back, chri, chriSize);

		if (why != NULL)
		{
			RldMk_Say("error", "chri", why, "Internal error: the character info does not read back (%s). Please report it.", why);
			free(chri);
			chri = NULL;
		}
	}

	if (chri != NULL)
	{
		container = RldMk_Container(chri, chriSize, build.cmdl, build.cmdlSize, &containerSize);
	}

	if (container == NULL)
	{
		if (s_rldMkErrors == 0)
		{
			RldMk_OutOfMemory();
		}
		free(chri);
		RldMk_FreeBuild(&build);
		RldMk_FreePly(&ply);
		return RldMk_Fail(1);
	}

	{
		u8 digest[32];
		int k;

		Sha256(container, containerSize, digest);
		for (k = 0; k < 32; k++)
		{
			snprintf(&digestText[k * 2], 3, "%02x", digest[k]);
		}
	}

	if (!check)
	{
		FILE *file = fopen(outPath, "wb");

		if ((file == NULL) || (fwrite(container, 1, containerSize, file) != containerSize) || (fclose(file) != 0))
		{
			RldMk_Say("error", "write", outPath,
			          "The character file cannot be written. Is the folder write-protected, or is the file open in another program?");
			free(chri);
			free(container);
			RldMk_FreeBuild(&build);
			RldMk_FreePly(&ply);
			return RldMk_Fail(1);
		}
	}

	printf("\n%s%s\n", outPath, check ? " (check only - nothing written)" : "");
	printf("  size          %u bytes\n", (u32)containerSize);
	printf("  CHRI          %u bytes\n", (u32)chriSize);
	printf("  CMDL          %u bytes\n", (u32)build.cmdlSize);
	printf("  sha256        %s\n", digestText);
	printf("  format        %d.%d%s\n", RLDCHAR_VERSION_MAJOR, RLDCHAR_VERSION_MINOR, (RLDCHAR_VERSION_MINOR == 0) ? " (draft)" : "");

	{
		char bytes[24];

		snprintf(bytes, sizeof(bytes), "%u", (u32)containerSize);
		Rld_Emit("result", check ? "checked" : "ok", outPath, bytes, digestText, (const char *)NULL);
		s_machineResult = 1;
	}

	free(chri);
	free(container);
	RldMk_FreeBuild(&build);
	RldMk_FreePly(&ply);
	return 0;
}

//========================================================================================
// info AND verify FOR .rldchar
//========================================================================================

struct RldMkListing
{
	int printed;
};

static int RldMk_ListFinding(void *context, const struct RldCharFinding *finding)
{
	struct RldMkListing *listing = (struct RldMkListing *)context;

	if (listing->printed < 32)
	{
		printf("  %-11s %s: %s\n", RldChar_VerdictWord(finding->verdict), finding->rule, finding->detail);
	}
	listing->printed++;
	return 1;
}

static int RldMk_Inspect(const char *path, int full, int machine)
{
	struct RldReader reader;
	struct RldCharInfo info;
	struct RldCharModelFacts facts;
	struct RldCharFinding first;
	struct RldMkListing listing;
	enum RldCharVerdict verdict;
	const char *error = NULL;
	u8 *chri = NULL;
	u8 *cmdl = NULL;
	size_t chriSize = 0;
	size_t cmdlSize = 0;
	int chriIndex = -1;
	int cmdlIndex = -1;
	int failed = 0;
	char text[256];
	int i;

	memset(&reader, 0, sizeof(reader));
	memset(&facts, 0, sizeof(facts));
	memset(&first, 0, sizeof(first));
	listing.printed = 0;

	error = Rld_OpenAs(&reader, path, &s_rldCharFormat);
	if (error != NULL)
	{
		if (machine)
		{
			Rld_Emit("container", path, "refused", error, (const char *)NULL);
		}
		else
		{
			fprintf(stderr, "rldpack: %s\n", error);
			if ((reader.refusal == RLD_REFUSAL_NEWER) || (reader.refusal == RLD_REFUSAL_OLDER))
			{
				fprintf(stderr, "         character format %u.%u, this rldpack reads %d.x", reader.major, reader.minor, RLDCHAR_VERSION_MAJOR);
				if (reader.unknownFlags != 0u)
				{
					fprintf(stderr, ", unknown feature bits 0x%08x", reader.unknownFlags);
				}
				fprintf(stderr, "\n");
			}
		}
		return 1;
	}

	// Rld_OpenAs has demanded both required chunks.
	Rld_FindEntry(&reader, "CHRI", &chriIndex);
	Rld_FindEntry(&reader, "CMDL", &cmdlIndex);

	chri = Rld_ReadChunk(&reader, chriIndex, &chriSize, &error);
	if (chri == NULL)
	{
		snprintf(text, sizeof(text), "CHRI: %s", (error != NULL) ? error : "cannot be read");
	}
	else if ((error = RldChar_ParseInfo(&info, chri, chriSize)) != NULL)
	{
		snprintf(text, sizeof(text), "DAMAGED - CHRI: %s", error);
	}

	if ((chri == NULL) || (error != NULL))
	{
		if (machine)
		{
			Rld_Emit("container", path, "refused", text, (const char *)NULL);
		}
		else
		{
			fprintf(stderr, "rldpack: %s\n", text);
		}
		free(chri);
		Rld_Close(&reader);
		return 1;
	}

	cmdl = Rld_ReadChunk(&reader, cmdlIndex, &cmdlSize, &error);
	if (cmdl == NULL)
	{
		snprintf(text, sizeof(text), "CMDL: %s", (error != NULL) ? error : "cannot be read");
		if (machine)
		{
			Rld_Emit("container", path, "refused", text, (const char *)NULL);
		}
		else
		{
			fprintf(stderr, "rldpack: %s\n", text);
		}
		free(chri);
		Rld_Close(&reader);
		return 1;
	}

	if (!machine)
	{
		printf("%s\n", path);
		printf("  format        %u.%u%s\n", reader.major, reader.minor, (reader.minor == 0u) ? "   (draft)" : "");
		printf("  name          %s\n", info.name);
		printf("  author        %s\n", (info.author[0] != '\0') ? info.author : "(not set)");
		printf("  char_version  %u\n", info.charVersion);
		printf("  template      %u %s\n", info.templateId, (info.templateId < RLDMK_TEMPLATE_COUNT) ? s_rldMkTemplates[info.templateId].name : "");
		printf("  class         %u %s\n", info.classId, (info.classId < RLDMK_CLASS_COUNT) ? s_rldMkClassNames[info.classId] : "");
		printf("  chunks        ");
		for (i = 0; i < reader.chunkCount; i++)
		{
			const u8 *entry = &reader.directory[i * RLD_DIR_ENTRY_SIZE];

			printf("%.4s%s%s", (const char *)entry, (RldChar_ChunkLimit(entry) != 0u) ? "" : " (skipped)", ((i + 1) < reader.chunkCount) ? " " : "\n");
		}
		if (full)
		{
			printf("  model check\n");
		}
	}

	verdict = RldChar_CheckModel(cmdl, cmdlSize, &first, (full && !machine) ? RldMk_ListFinding : NULL, &listing, &facts);

	if (machine)
	{
		char number[32];

		if (verdict == RLDCHAR_VERDICT_OK)
		{
			Rld_Emit("container", path, "ok", "", (const char *)NULL);
		}
		else
		{
			snprintf(text, sizeof(text), "%s (%s) %s", RldChar_VerdictWord(verdict), (first.rule != NULL) ? first.rule : "", first.detail);
			Rld_Emit("container", path, "refused", text, (const char *)NULL);
		}

		Rld_Emit("value", "name", info.name, "container", (const char *)NULL);
		Rld_Emit("value", "author", info.author, "container", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", info.charVersion);
		Rld_Emit("value", "char_version", number, "container", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", info.templateId);
		Rld_Emit("value", "template", number, "container", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", info.classId);
		Rld_Emit("value", "class", number, "container", (const char *)NULL);
		snprintf(number, sizeof(number), "%u.%u", reader.major, reader.minor);
		Rld_Emit("value", "format", number, "container", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", facts.triangles);
		Rld_Emit("char", "triangles", number, (const char *)NULL);
		snprintf(number, sizeof(number), "%u", facts.records);
		Rld_Emit("char", "records", number, (const char *)NULL);
		snprintf(number, sizeof(number), "%u", facts.colorCount);
		Rld_Emit("char", "colors_out", number, (const char *)NULL);
		snprintf(number, sizeof(number), "%u,%u,%u,%u", facts.frames[0], facts.frames[1], facts.frames[2], facts.frames[3]);
		Rld_Emit("char", "frames", number, (const char *)NULL);
		snprintf(number, sizeof(number), "%u", facts.drawBytes);
		Rld_Emit("char", "draw_bytes", number, (const char *)NULL);
		Rld_Emit("char", "verdict", RldChar_VerdictWord(verdict), (first.rule != NULL) ? first.rule : "", first.detail, (const char *)NULL);
	}
	else
	{
		printf("  model         %u triangles, %u records, %u colors, frames %u,%u,%u,%u of %u bytes, scale %u %u %u\n", facts.triangles, facts.records,
		       facts.colorCount, facts.frames[0], facts.frames[1], facts.frames[2], facts.frames[3], facts.frameSize[0], facts.scale[0], facts.scale[1],
		       facts.scale[2]);
		printf("  draw memory   %u bytes per frame, at most %u\n", facts.drawBytes, RLDCHAR_DRAW_BYTES_MAX);
		if (verdict == RLDCHAR_VERDICT_OK)
		{
			printf("  verdict       OK - the game loads it\n");
		}
		else
		{
			printf("  verdict       %s (%s) %s\n", RldChar_VerdictWord(verdict), (first.rule != NULL) ? first.rule : "", first.detail);
		}
	}

	if (verdict != RLDCHAR_VERDICT_OK)
	{
		failed = 1;
	}

	if (full && !machine)
	{
		for (i = 0; i < reader.chunkCount; i++)
		{
			const u8 *entry = &reader.directory[i * RLD_DIR_ENTRY_SIZE];
			u8 *data;
			size_t size = 0;

			if (i == chriIndex)
			{
				printf("  ok     CHRI   %u bytes\n", (u32)chriSize);
				continue;
			}
			if (i == cmdlIndex)
			{
				printf("  ok     CMDL   %u bytes\n", (u32)cmdlSize);
				continue;
			}
			if (RldChar_ChunkLimit(entry) == 0u)
			{
				printf("  skip   %.4s   not a chunk type of this format - left alone, as the game does\n", (const char *)entry);
				continue;
			}

			data = Rld_ReadChunk(&reader, i, &size, &error);
			if (data == NULL)
			{
				printf("  FAILED %.4s   %s\n", (const char *)entry, (error != NULL) ? error : "cannot be read");
				failed = 1;
				continue;
			}

			printf("  ok     %.4s   %u bytes\n", (const char *)entry, (u32)size);
			free(data);
		}

		if (failed)
		{
			printf("\nTHIS CHARACTER IS NOT SOUND. The lines above say which part.\n");
			printf("Do not load it, do not pass it on.\n");
		}
		else
		{
			printf("\nEvery chunk matches its hash, and the model passes the game's check.\n");
		}
	}

	free(chri);
	free(cmdl);
	Rld_Close(&reader);
	return failed;
}

static int RldChar_InfoCommand(const char *path, int machine)
{
	return RldMk_Inspect(path, 0, machine);
}

static int RldChar_VerifyCommand(const char *path)
{
	return RldMk_Inspect(path, 1, 0);
}

static void RldChar_Usage(FILE *out)
{
	int t;

	fprintf(out, "MAKE-CHAR\n");
	fprintf(out, "  rldpack make-char --model <ply> --name <text> --template <0..14> [switches]\n");
	fprintf(out, "  Builds a character (.rldchar) from a PLY model with vertex colors, ASCII\n");
	fprintf(out, "  or binary, in meters, +Y up and +Z forward. The model brings its kart: a\n");
	fprintf(out, "  part at the bottom, about 1.8 units long; the game draws the wheels.\n");
	fprintf(out, "  The file lands NEXT TO the model as <model>.rldchar.\n");
	fprintf(out, "\n");
	fprintf(out, "  --model <ply>               the model (required)\n");
	fprintf(out, "  --name <text>               the name in the menu (required), 1 to 17 of\n");
	fprintf(out, "                              A-Z 0-9 space ! %% ' + , - . / : < = > ? _\n");
	fprintf(out, "  --template <0..14>          the driver the character stands in for\n");
	fprintf(out, "                              (required) - voice, portrait and the kart's\n");
	fprintf(out, "                              values come from it:\n");
	for (t = 0; t < RLDMK_TEMPLATE_COUNT; t++)
	{
		fprintf(out, "%s%2d %-16s%s", ((t % 3) == 0) ? "                              " : "", t, s_rldMkTemplates[t].name, ((t % 3) == 2) ? "\n" : "");
	}
	fprintf(out, "  --class like|0..3           like the template (default), 0 balanced,\n");
	fprintf(out, "                              1 acceleration, 2 speed, 3 turning\n");
	fprintf(out, "  --author <text>             your name or handle, 64 bytes at most\n");
	fprintf(out, "  --char-version <n>          counts your own revisions (default 1)\n");
	fprintf(out, "  --poses auto|still          auto: the driver leans when steering (default);\n");
	fprintf(out, "                              still: all 47 frames the same\n");
	fprintf(out, "  --colors 64|128             the palette (default 64)\n");
	fprintf(out, "  --up y|z                    the up axis of the PLY (default y); z for\n");
	fprintf(out, "                              Blender's own axes, with the front at -Y\n");
	fprintf(out, "  --forward z|-z              -z: the model looks backwards and is turned\n");
	fprintf(out, "  --scale <f>                 multiply the model (default 1)\n");
	fprintf(out, "  --two-sided                 draw both sides of every triangle\n");
	fprintf(out, "  --out <file>                somewhere else than next to the model\n");
	fprintf(out, "  --check                     run every check and build in memory, write nothing\n");
	fprintf(out, "\n");
	fprintf(out, "  rldpack make-char --model driver.ply --name \"MY DRIVER\" --template 14\n");
	fprintf(out, "\n");
}

//========================================================================================
// SELF-TEST
//========================================================================================
//
// Every input is made here: boxes and cones as PLY text or bytes, and models
// written straight into a CMDL. No file is read or written, no game data used.
// The model cases change one thing in a good CMDL so that exactly one rule of
// RldChar_CheckModel breaks, and expect that rule and its verdict as the FIRST
// finding (the order of the rules in include/rldchar.inc).

static int s_rldMkTestFailed;

static void RldMk_Test(int ok, const char *name, const char *format, ...)
{
	char text[320];
	va_list list;

	if (ok)
	{
		printf("  ok   make-char %s\n", name);
		return;
	}

	va_start(list, format);
	vsnprintf(text, sizeof(text), format, list);
	va_end(list);
	printf("  FAIL make-char %s: %s\n", name, text);
	s_rldMkTestFailed++;
}

struct RldMkBuffer
{
	u8 *data;
	size_t size;
	size_t capacity;
};

static void RldMk_BufferBytes(struct RldMkBuffer *buffer, const void *bytes, size_t count)
{
	if ((buffer->size + count + 1u) > buffer->capacity)
	{
		size_t capacity = (buffer->capacity != 0u) ? buffer->capacity : 4096u;
		u8 *grown;

		while ((buffer->size + count + 1u) > capacity)
		{
			capacity *= 2u;
		}

		grown = (u8 *)realloc(buffer->data, capacity);
		if (grown == NULL)
		{
			return;
		}
		buffer->data = grown;
		buffer->capacity = capacity;
	}

	memcpy(&buffer->data[buffer->size], bytes, count);
	buffer->size += count;
	buffer->data[buffer->size] = 0;
}

static void RldMk_BufferText(struct RldMkBuffer *buffer, const char *format, ...)
{
	char text[512];
	va_list list;
	int n;

	va_start(list, format);
	n = vsnprintf(text, sizeof(text), format, list);
	va_end(list);

	if (n > 0)
	{
		RldMk_BufferBytes(buffer, text, ((size_t)n < sizeof(text)) ? (size_t)n : (sizeof(text) - 1u));
	}
}

static void RldMk_BufferLE32(struct RldMkBuffer *buffer, u32 value)
{
	u8 bytes[4];

	Rld_WriteLE32(bytes, value);
	RldMk_BufferBytes(buffer, bytes, 4u);
}

static void RldMk_BufferFloat(struct RldMkBuffer *buffer, double value)
{
	const float f = (float)value;
	u32 word;

	memcpy(&word, &f, sizeof(word));
	RldMk_BufferLE32(buffer, word);
}

#define RLDMK_TEST_VERTICES 8192
#define RLDMK_TEST_FACES 8192

struct RldMkTestMesh
{
	u32 vertexCount;
	u32 faceCount;
	double xyz[RLDMK_TEST_VERTICES][3];
	u8 rgb[RLDMK_TEST_VERTICES][3];
	u32 corners[RLDMK_TEST_FACES][5];
	u32 cornerCount[RLDMK_TEST_FACES];
};

// Colors that differ for every k below 4096.
static void RldMk_TestColor(u32 k, u8 rgb[3])
{
	rgb[0] = (u8)((k % 64u) * 4u);
	rgb[1] = (u8)(((k / 64u) % 64u) * 4u);
	rgb[2] = (u8)(128u + (k % 7u) * 16u);
}

static u32 RldMk_TestVertex(struct RldMkTestMesh *mesh, double x, double y, double z, u32 color)
{
	const u32 v = mesh->vertexCount;

	if (v >= RLDMK_TEST_VERTICES)
	{
		return 0;
	}

	// Through float, as the binary file carries it: ascii and binary then say
	// the same.
	mesh->xyz[v][0] = (double)(float)x;
	mesh->xyz[v][1] = (double)(float)y;
	mesh->xyz[v][2] = (double)(float)z;
	RldMk_TestColor(color, mesh->rgb[v]);
	mesh->vertexCount++;
	return v;
}

static void RldMk_TestFace(struct RldMkTestMesh *mesh, u32 count, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	const u32 f = mesh->faceCount;

	if (f >= RLDMK_TEST_FACES)
	{
		return;
	}

	mesh->corners[f][0] = a;
	mesh->corners[f][1] = b;
	mesh->corners[f][2] = c;
	mesh->corners[f][3] = d;
	mesh->corners[f][4] = e;
	mesh->cornerCount[f] = count;
	mesh->faceCount++;
}

// A box from lo to hi; every side split into n x n quads with corners of its
// own (the sides meet at seams, as an exporter writes them), wound so that the
// right-hand normal points out. skipSide leaves one side open (-1: none).
static void RldMk_TestBox(struct RldMkTestMesh *mesh, const double lo[3], const double hi[3], u32 n, u32 color, int perVertex, int skipSide)
{
	// fixed axis, side (0 lo, 1 hi), in-plane axes u and v with u x v pointing out
	static const int sides[6][4] = {{0, 1, 1, 2}, {0, 0, 2, 1}, {1, 1, 2, 0}, {1, 0, 0, 2}, {2, 1, 0, 1}, {2, 0, 1, 0}};
	int s;

	for (s = 0; s < 6; s++)
	{
		const int axis = sides[s][0];
		const int ua = sides[s][2];
		const int va = sides[s][3];
		const u32 first = mesh->vertexCount;
		u32 i;
		u32 j;

		if (s == skipSide)
		{
			continue;
		}

		for (j = 0; j <= n; j++)
		{
			for (i = 0; i <= n; i++)
			{
				double p[3];

				p[axis] = sides[s][1] ? hi[axis] : lo[axis];
				p[ua] = lo[ua] + (hi[ua] - lo[ua]) * (double)i / (double)n;
				p[va] = lo[va] + (hi[va] - lo[va]) * (double)j / (double)n;
				RldMk_TestVertex(mesh, p[0], p[1], p[2], perVertex ? (color + mesh->vertexCount) : (color + (u32)s));
			}
		}

		for (j = 0; j < n; j++)
		{
			for (i = 0; i < n; i++)
			{
				const u32 v = first + j * (n + 1u) + i;

				RldMk_TestFace(mesh, 4u, v, v + 1u, v + n + 2u, v + n + 1u, 0u);
			}
		}
	}
}

// A cone: apex up, a ring of 12 and the center of its base. The sides and the
// base are fans around apex and center.
static void RldMk_TestCone(struct RldMkTestMesh *mesh, double x, double y, double z, double radius, double height)
{
	const u32 apex = RldMk_TestVertex(mesh, x, y + height, z, 40u);
	const u32 center = RldMk_TestVertex(mesh, x, y, z, 41u);
	u32 ring[12];
	int i;

	for (i = 0; i < 12; i++)
	{
		double s;
		double c;

		RldMk_SinCos(30.0 * (double)i, &s, &c);
		ring[i] = RldMk_TestVertex(mesh, x + radius * c, y, z + radius * s, 42u + (u32)(i & 1));
	}

	for (i = 0; i < 12; i++)
	{
		RldMk_TestFace(mesh, 3u, apex, ring[(i + 1) % 12], ring[i], 0u, 0u);
		RldMk_TestFace(mesh, 3u, center, ring[i], ring[(i + 1) % 12], 0u, 0u);
	}
}

// Kart, driver and steering wheel as three boxes - a model every check passes.
static void RldMk_TestStandard(struct RldMkTestMesh *mesh, u32 driverSplit, int perVertex, double lift, int skipSide)
{
	double kartLo[3] = {-0.45, 0.10, -0.90};
	double kartHi[3] = {0.45, 0.35, 0.90};
	double driverLo[3] = {-0.25, 0.40, -0.40};
	double driverHi[3] = {0.25, 1.00, 0.10};
	double wheelLo[3] = {-0.15, 0.50, 0.30};
	double wheelHi[3] = {0.15, 0.60, 0.36};

	kartLo[1] += lift;
	kartHi[1] += lift;
	driverLo[1] += lift;
	driverHi[1] += lift;
	wheelLo[1] += lift;
	wheelHi[1] += lift;

	mesh->vertexCount = 0;
	mesh->faceCount = 0;
	RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
	RldMk_TestBox(mesh, driverLo, driverHi, driverSplit, 10u, perVertex, skipSide);
	RldMk_TestBox(mesh, wheelLo, wheelHi, 1u, 20u, 0, -1);
}

// mode 0 ascii, 1 binary little endian, 2 binary big endian (only the header
// says so - it is refused before the data).
static u8 *RldMk_TestPly(const struct RldMkTestMesh *mesh, int mode, int colors, int floatColors, size_t *sizeOut)
{
	static const char *const formats[3] = {"ascii", "binary_little_endian", "binary_big_endian"};
	struct RldMkBuffer buffer;
	u32 i;
	u32 k;

	memset(&buffer, 0, sizeof(buffer));
	RldMk_BufferText(&buffer, "ply\nformat %s 1.0\ncomment made by the rldpack self-test\nelement vertex %u\n", formats[mode], mesh->vertexCount);
	RldMk_BufferText(&buffer, "property float x\nproperty float y\nproperty float z\n");
	if (colors)
	{
		const char *type = floatColors ? "float" : "uchar";

		RldMk_BufferText(&buffer, "property %s red\nproperty %s green\nproperty %s blue\n", type, type, type);
	}
	RldMk_BufferText(&buffer, "element face %u\nproperty list uchar uint vertex_indices\nend_header\n", mesh->faceCount);

	for (i = 0; i < mesh->vertexCount; i++)
	{
		if (mode == 0)
		{
			RldMk_BufferText(&buffer, "%.6f %.6f %.6f", mesh->xyz[i][0], mesh->xyz[i][1], mesh->xyz[i][2]);
			for (k = 0; colors && (k < 3u); k++)
			{
				if (floatColors)
				{
					RldMk_BufferText(&buffer, " %.6f", (double)mesh->rgb[i][k] / 255.0);
				}
				else
				{
					RldMk_BufferText(&buffer, " %u", (u32)mesh->rgb[i][k]);
				}
			}
			RldMk_BufferText(&buffer, "\n");
		}
		else
		{
			for (k = 0; k < 3u; k++)
			{
				RldMk_BufferFloat(&buffer, mesh->xyz[i][k]);
			}
			for (k = 0; colors && (k < 3u); k++)
			{
				if (floatColors)
				{
					RldMk_BufferFloat(&buffer, (double)mesh->rgb[i][k] / 255.0);
				}
				else
				{
					RldMk_BufferBytes(&buffer, &mesh->rgb[i][k], 1u);
				}
			}
		}
	}

	for (i = 0; i < mesh->faceCount; i++)
	{
		if (mode == 0)
		{
			RldMk_BufferText(&buffer, "%u", mesh->cornerCount[i]);
			for (k = 0; k < mesh->cornerCount[i]; k++)
			{
				RldMk_BufferText(&buffer, " %u", mesh->corners[i][k]);
			}
			RldMk_BufferText(&buffer, "\n");
		}
		else
		{
			const u8 count = (u8)mesh->cornerCount[i];

			RldMk_BufferBytes(&buffer, &count, 1u);
			for (k = 0; k < mesh->cornerCount[i]; k++)
			{
				RldMk_BufferLE32(&buffer, mesh->corners[i][k]);
			}
		}
	}

	*sizeOut = buffer.size;
	return buffer.data;
}

static void RldMk_TestOptions(struct RldMkOptions *opt)
{
	memset(opt, 0, sizeof(*opt));
	opt->colorTarget = 64u;
	opt->scale = 1.0;
}

// Write, read and build a test mesh. 1 = built; ply and build are freed by the caller.
static int RldMk_TestBuild(const struct RldMkTestMesh *mesh, int mode, int colors, const struct RldMkOptions *opt, struct RldMkPly *ply,
                           struct RldMkBuild *build)
{
	size_t size = 0;
	u8 *data = RldMk_TestPly(mesh, mode, colors, 0, &size);
	int ok;

	memset(ply, 0, sizeof(*ply));
	memset(build, 0, sizeof(*build));
	RldMk_ResetSaid();

	if (data == NULL)
	{
		return 0;
	}

	ok = RldMk_ParsePly(data, size, ply) && RldMk_BuildModel(ply, opt, build);
	free(data);
	return ok;
}

// A PLY given as text: does reading it say `code`?
static int RldMk_TestPlyText(const char *text, const char *code)
{
	struct RldMkPly ply;
	int read;

	RldMk_ResetSaid();
	read = RldMk_ParsePly((const u8 *)text, strlen(text), &ply);
	RldMk_FreePly(&ply);
	return !read && RldMk_Said(code);
}

// The body offset of frame `frame` of animation `anim` in a CMDL.
static u32 RldMk_TestFrame(const u8 *cmdl, int anim, u32 frame)
{
	const u8 *body = &cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 table = Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS]);
	const u32 at = Rld_ReadLE32(&body[table + 4u * (u32)anim]);

	return at + RLDMK_ANIM_BYTES + frame * Rld_ReadLE16(&body[at + RLDMK_ANIM_FRAME_SIZE]);
}

static u32 RldMk_TestAnim(const u8 *cmdl, int anim)
{
	const u8 *body = &cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 table = Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS]);

	return Rld_ReadLE32(&body[table + 4u * (u32)anim]);
}

// The body offset of command `index` (0 = the first after the color count).
static u32 RldMk_TestCommand(const u8 *cmdl, u32 index)
{
	const u8 *body = &cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);

	return Rld_ReadLE32(&body[header + RLDMK_HEADER_COMMANDS]) + 4u + 4u * index;
}

static u8 *RldMk_TestCopy(const u8 *cmdl, size_t size)
{
	u8 *copy = (u8 *)calloc(1, size + 64u);

	if (copy != NULL)
	{
		memcpy(copy, cmdl, size);
	}
	return copy;
}

static void RldMk_TestMapAppend(u8 *cmdl, size_t *size, u32 entry)
{
	const u32 g = Rld_ReadLE32(cmdl);
	const u32 mapBytes = Rld_ReadLE32(&cmdl[4u + g]);

	Rld_WriteLE32(&cmdl[*size], entry);
	Rld_WriteLE32(&cmdl[4u + g], mapBytes + 4u);
	*size += 4u;
}

static void RldMk_TestMapRemove(u8 *cmdl, size_t *size, u32 entry)
{
	const u32 g = Rld_ReadLE32(cmdl);
	const u32 mapBytes = Rld_ReadLE32(&cmdl[4u + g]);
	u32 i;

	for (i = 0; i < (mapBytes / 4u); i++)
	{
		u8 *at = &cmdl[8u + g + 4u * i];

		if (Rld_ReadLE32(at) == entry)
		{
			memmove(at, at + 4, (size_t)(mapBytes - 4u * (i + 1u)));
			Rld_WriteLE32(&cmdl[4u + g], mapBytes - 4u);
			*size -= 4u;
			return;
		}
	}
}

static void RldMk_TestRule(const char *name, const u8 *cmdl, size_t size, const char *rule, enum RldCharVerdict verdict)
{
	struct RldCharFinding first;
	enum RldCharVerdict got;
	int ok;

	if (cmdl == NULL)
	{
		RldMk_Test(0, name, "out of memory");
		return;
	}

	got = RldChar_CheckModel(cmdl, size, &first, NULL, NULL, NULL);
	ok = (got == verdict) && ((rule == NULL) ? (first.rule == NULL) : ((first.rule != NULL) && (strcmp(rule, first.rule) == 0)));

	RldMk_Test(ok, name, "expected %s (%s), got %s (%s) %s", RldChar_VerdictWord(verdict), (rule != NULL) ? rule : "-", RldChar_VerdictWord(got),
	           (first.rule != NULL) ? first.rule : "-", first.detail);
}

// One long strip written straight into a CMDL: `commands` corners, each a new
// record, the slots 1..87 in turn, one color; the frames carry `records` records.
static u8 *RldMk_TestStrip(u32 commands, u32 records, size_t *sizeOut)
{
	struct RldMkLayout layout;
	int origins[3 * RLDMK_FRAMES_TOTAL];
	const u32 color = 0x00808080u;
	u32 *list = (u32 *)malloc(((size_t)commands + 1u) * sizeof(u32));
	u8 *data = (u8 *)calloc((size_t)RLDMK_FRAMES_TOTAL * records * 3u + 1u, 1u);
	u8 *out = NULL;
	u32 i;

	*sizeOut = 0;
	if ((list != NULL) && (data != NULL))
	{
		for (i = 0; i < commands; i++)
		{
			list[i] = ((i == 0u) ? RLDMK_CMD_STRIP : 0u) | RLDMK_CMD_SCRATCH | RLDMK_CMD_CULL | ((1u + (i % 87u)) << RLDMK_SLOT_SHIFT);
		}
		memset(origins, 0, sizeof(origins));

		memset(&layout, 0, sizeof(layout));
		layout.colorCount = 1u;
		layout.colors = &color;
		layout.commandCount = commands;
		layout.commands = list;
		layout.recordCount = records;
		layout.scale[0] = 100;
		layout.scale[1] = 100;
		layout.scale[2] = 100;
		layout.origin = origins;
		layout.records = data;
		out = RldMk_WriteCmdl(&layout, sizeOut);
	}

	free(list);
	free(data);
	return out;
}

// The PLY reader.
static void RldMk_SelfTestPly(struct RldMkTestMesh *mesh)
{
	struct RldMkPly ascii;
	struct RldMkPly binary;
	struct RldMkPly floats;
	u8 *data;
	size_t size = 0;
	int ok;
	u32 i;

	memset(&ascii, 0, sizeof(ascii));
	memset(&binary, 0, sizeof(binary));
	memset(&floats, 0, sizeof(floats));
	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);

	data = RldMk_TestPly(mesh, 0, 1, 0, &size);
	RldMk_ResetSaid();
	ok = (data != NULL) && RldMk_ParsePly(data, size, &ascii);
	RldMk_Test(ok && (ascii.vertexCount == 72u) && (ascii.faceCount == 18u) && ascii.hasColors && !ascii.binary, "PLY ascii", "%u vertices, %u faces",
	           ascii.vertexCount, ascii.faceCount);
	if (data != NULL)
	{
		struct RldMkPly cut;

		RldMk_ResetSaid();
		ok = RldMk_ParsePly(data, size - 12u, &cut);
		RldMk_FreePly(&cut);
		RldMk_Test(!ok && RldMk_Said("ply-truncated"), "PLY ascii cut short", "said: %s", s_rldMkCodes);
	}
	free(data);

	data = RldMk_TestPly(mesh, 1, 1, 0, &size);
	RldMk_ResetSaid();
	ok = (data != NULL) && RldMk_ParsePly(data, size, &binary);
	if (ok)
	{
		ok = binary.binary && (binary.vertexCount == ascii.vertexCount) && (binary.faceCount == ascii.faceCount) &&
		     (memcmp(binary.rgb, ascii.rgb, (size_t)ascii.vertexCount * 3u) == 0) &&
		     (memcmp(binary.corners, ascii.corners, (size_t)ascii.faceCount * 4u * sizeof(u32)) == 0);
		for (i = 0; ok && (i < (3u * ascii.vertexCount)); i++)
		{
			const double d = binary.xyz[i] - ascii.xyz[i];

			ok = (d < 1.0e-6) && (d > -1.0e-6);
		}
	}
	RldMk_Test(ok, "PLY binary little endian, the same as ascii", "the binary file reads differently");
	if (data != NULL)
	{
		struct RldMkPly cut;

		RldMk_ResetSaid();
		ok = RldMk_ParsePly(data, size - 3u, &cut);
		RldMk_FreePly(&cut);
		RldMk_Test(!ok && RldMk_Said("ply-truncated"), "PLY binary cut short", "said: %s", s_rldMkCodes);
	}
	free(data);
	RldMk_FreePly(&binary);

	data = RldMk_TestPly(mesh, 0, 1, 1, &size);
	RldMk_ResetSaid();
	ok = (data != NULL) && RldMk_ParsePly(data, size, &floats);
	RldMk_Test(ok && (memcmp(floats.rgb, ascii.rgb, (size_t)ascii.vertexCount * 3u) == 0), "PLY colors as float 0..1", "colors differ");
	RldMk_FreePly(&floats);
	free(data);
	RldMk_FreePly(&ascii);

	data = RldMk_TestPly(mesh, 2, 1, 0, &size);
	RldMk_Test((data != NULL) && RldMk_TestPlyText((const char *)data, "ply-format"), "PLY big endian refused", "said: %s", s_rldMkCodes);
	free(data);

	RldMk_Test(RldMk_TestPlyText("hello\nworld\n", "ply-format"), "not a PLY refused", "said: %s", s_rldMkCodes);
	RldMk_Test(RldMk_TestPlyText("ply\nformat ascii 1.0\nelement vertex 3\nproperty float a\nproperty float b\nproperty float c\n"
	                             "element face 1\nproperty list uchar uint vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n",
	                             "ply-position"),
	           "PLY without x y z refused", "said: %s", s_rldMkCodes);
	RldMk_Test(RldMk_TestPlyText("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
	                             "element face 1\nproperty list uchar uint vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 7\n",
	                             "ply-index"),
	           "PLY face index out of range refused", "said: %s", s_rldMkCodes);
	RldMk_Test(RldMk_TestPlyText("ply\nformat ascii 1.0\nelement vertex 100001\nproperty float x\nproperty float y\nproperty float z\n"
	                             "element face 1\nproperty list uchar uint vertex_indices\nend_header\n",
	                             "ply-big"),
	           "PLY with too many vertices refused", "said: %s", s_rldMkCodes);
}

// Steps 2-9 on whole models.
static void RldMk_SelfTestChain(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	int ok;
	u32 i;
	u32 f;

	RldMk_TestOptions(&opt);

	// The standard model: welding, quads, parts.
	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && (b.quads == 18u) && (b.triangleCount == 36u) && RldMk_Said("ply-quads"), "quads split in two", "%u quads, %u triangles",
	           b.quads, b.triangleCount);
	RldMk_Test(ok && (b.positionCount == 24u), "corners welded to positions", "%u positions from 72 corners", b.positionCount);
	RldMk_Test(ok && (b.partCount == 3u) && (b.groupTriangles[RLDMK_GROUP_KART] == 12u) && (b.groupTriangles[RLDMK_GROUP_DRIVER] == 12u) &&
	               (b.groupTriangles[RLDMK_GROUP_WHEEL] == 12u) && b.posed,
	           "kart, driver and steering wheel found", "%u parts: kart %u, driver %u, wheel %u", b.partCount, b.groupTriangles[0], b.groupTriangles[1],
	           b.groupTriangles[2]);
	RldMk_Test(ok && (s_rldMkErrors == 0) && !RldMk_Said("ply-axes") && !RldMk_Said("ply-center") && !RldMk_Said("ply-facing") &&
	               !RldMk_Said("ply-open-edges"),
	           "a good model says nothing else", "said: %s", s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// n-gon, degenerate face, shorter diagonal.
	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
	{
		const u32 v = RldMk_TestVertex(mesh, 0.0, 0.5, -0.7, 50u);

		RldMk_TestVertex(mesh, 0.1, 0.5, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.1, 0.6, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.05, 0.65, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.0, 0.6, -0.7, 50u);
		RldMk_TestFace(mesh, 5u, v, v + 1u, v + 2u, v + 3u, v + 4u);
	}
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(!ok && RldMk_Said("ply-ngon"), "five corners refused", "said: %s", s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
	{
		const u32 v = RldMk_TestVertex(mesh, 0.0, 0.40, -0.6, 51u);
		u32 found = 0;

		RldMk_TestVertex(mesh, 0.3, 0.40, -0.6, 51u);
		RldMk_TestVertex(mesh, 0.1, 0.45, -0.6, 51u);
		RldMk_TestVertex(mesh, 0.0, 0.45, -0.6, 51u);
		RldMk_TestFace(mesh, 4u, v, v + 1u, v + 2u, v + 3u, 0u);

		// two corners on one position: no area
		RldMk_TestVertex(mesh, -0.3, 0.42, -0.6, 52u);
		RldMk_TestVertex(mesh, -0.3, 0.42, -0.6, 53u);
		RldMk_TestVertex(mesh, -0.2, 0.47, -0.6, 52u);
		RldMk_TestFace(mesh, 3u, v + 4u, v + 5u, v + 6u, 0u, 0u);

		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		for (i = 0; ok && (i < b.triangleCount); i++)
		{
			const u32 *t = &b.triangle[3u * i];

			if (((t[0] == v) && (t[1] == (v + 1u)) && (t[2] == (v + 2u))) || ((t[0] == v) && (t[1] == (v + 2u)) && (t[2] == (v + 3u))))
			{
				found++;
			}
		}
		RldMk_Test(ok && (found == 2u), "a quad is split at its shorter diagonal", "%u of 2 triangles found", found);
		RldMk_Test(ok && (b.degenerate == 1u) && RldMk_Said("ply-degenerate"), "a face without area is left out", "%u left out", b.degenerate);
	}
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// An open side, no colors.
	RldMk_TestStandard(mesh, 1u, 0, 0.0, 2);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && (b.openEdges == 4u) && RldMk_Said("ply-open-edges"), "open edges are counted", "%u open edges", b.openEdges);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 0, &opt, &ply, &b);
	RldMk_Test(ok && (b.colorCount == 1u) && RldMk_Said("ply-no-colors"), "no colors: one grey", "%u colors", b.colorCount);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// No kart, one part, wrong size.
	RldMk_TestStandard(mesh, 1u, 0, 1.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(!ok && RldMk_Said("ply-no-kart"), "no kart refused", "said: %s", s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	{
		const double lo[3] = {-0.45, 0.10, -0.90};
		const double hi[3] = {0.45, 0.80, 0.90};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, lo, hi, 2u, 0u, 0, -1);
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		for (f = 1; ok && (f < RLDMK_FRAMES_TOTAL); f++)
		{
			ok = (memcmp(&b.quant[0], &b.quant[(size_t)f * b.positionCount * 3u], (size_t)b.positionCount * 3u) == 0);
		}
		RldMk_Test(ok && !b.posed && RldMk_Said("ply-parts"), "one piece: a loud warning and still poses", "said: %s", s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
	opt.scale = 10.0;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(!ok && RldMk_Said("model-scale"), "ten times too large refused", "said: %s", s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	opt.scale = 1.0;

	{
		const double lo[3] = {-0.25, 0.40, -0.40};
		const double hi[3] = {0.25, 40.0, 0.10};

		RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
		RldMk_TestBox(mesh, lo, hi, 1u, 30u, 0, -1);
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(!ok && RldMk_Said("model-scale"), "scale beyond 16 bits refused", "said: %s", s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// Too many triangles for the draw memory.
	RldMk_TestStandard(mesh, 9u, 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(!ok && RldMk_Said("model-tris"), "more draw memory than allowed refused", "said: %s", s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// Colors: some 300 of them down to 64 and to 128; the vertex cache.
	RldMk_TestStandard(mesh, 6u, 1, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && (b.colorsIn > 256u) && (b.colorCount <= 64u) && (b.colorCount >= 32u) && RldMk_Said("ply-colors"), "colors reduced to 64",
	           "%u -> %u", b.colorsIn, b.colorCount);
	{
		u32 hits = 0;
		int inRange = ok;

		for (i = 0; ok && (i < b.commandCount); i++)
		{
			const u32 slot = (b.command[i] >> RLDMK_SLOT_SHIFT) & 0xFFu;

			if ((slot < b.slotLow) || (slot > (u32)RLDCHAR_SLOT_MAX) || ((b.command[i] & RLDMK_CMD_SCRATCH) == 0u))
			{
				inRange = 0;
			}
			if ((b.command[i] & RLDMK_CMD_CACHED) != 0u)
			{
				hits++;
			}
		}
		RldMk_Test(inRange && (hits != 0u) && ((b.recordCount + hits) == b.commandCount), "cache slots ceil(N/2)..87, hits cost no record",
		           "slots from %u, %u hits, %u records, %u commands", b.slotLow, hits, b.recordCount, b.commandCount);
		RldMk_Test(ok && (b.strips < b.triangleCount), "triangles in strips", "%u strips for %u triangles", b.strips, b.triangleCount);
	}
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	opt.colorTarget = 128u;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && (b.colorCount <= 128u) && (b.colorCount > 64u), "colors reduced to 128", "%u -> %u", b.colorsIn, b.colorCount);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	opt.colorTarget = 64u;

	// Fans: a cone on the driver.
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	RldMk_TestCone(mesh, 0.0, 1.05, -0.15, 0.15, 0.2);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && (b.fanSteps != 0u), "fans (bit 30) built and their cull sides right", "%u fan steps, said: %s", b.fanSteps, s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// The written model: frames, quantization, poses, map, the counter-checks.
static void RldMk_SelfTestModel(struct RldMkTestMesh *mesh, u8 **goodOut, size_t *goodSizeOut)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	struct RldMkBuild again;
	u32 bad = 0;
	int ok;
	int a;
	u32 i;
	u32 f;

	*goodOut = NULL;
	*goodSizeOut = 0;
	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);

	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && (s_rldMkErrors == 0), "the standard model is built", "said: %s", s_rldMkCodes);
	if (!ok)
	{
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
		return;
	}

	{
		const u8 *body = &b.cmdl[4];
		const u32 bodySize = Rld_ReadLE32(b.cmdl);
		const u32 mapBytes = Rld_ReadLE32(&body[bodySize]);
		int mapOk = (mapBytes == (4u * RLDMK_MAP_ENTRIES)) && ((8u + bodySize + mapBytes) == (u32)b.cmdlSize);
		u32 previous = 0;
		static const u32 zeroFields[4] = {RLDMK_HEADER_FRAME_DATA, 0x28u, 0x30u, 0x3Cu};

		for (i = 0; mapOk && (i < (mapBytes / 4u)); i++)
		{
			const u32 entry = Rld_ReadLE32(&body[bodySize + 4u + 4u * i]);
			const u32 value = Rld_ReadLE32(&body[entry]);

			mapOk = ((entry & 3u) == 0u) && ((i == 0u) || (entry > previous)) && (value != 0u) && (value <= bodySize);
			previous = entry;
		}
		for (i = 0; i < 4u; i++)
		{
			if (Rld_ReadLE32(&body[RLDMK_BODY_HEADER + zeroFields[i]]) != 0u)
			{
				mapOk = 0;
			}
		}
		RldMk_Test(mapOk, "pointer map: 8 fields, each once, none NULL", "map of %u bytes", mapBytes);

		ok = ((bodySize & 3u) == 0u) && ((b.frameSize & 3u) == 0u) && ((Rld_ReadLE32(&body[RLDMK_BODY_HEADER + RLDMK_HEADER_COLORS]) & 3u) == 0u);
		for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
		{
			const u32 anim = RldMk_TestAnim(b.cmdl, a);

			if (((anim & 3u) != 0u) || (Rld_ReadLE16(&body[anim + RLDMK_ANIM_NUM_FRAMES]) != (u32)s_rldCharFrames[a]) ||
			    (Rld_ReadLE16(&body[anim + RLDMK_ANIM_FRAME_SIZE]) != b.frameSize) || (Rld_ReadLE32(&body[anim + RLDMK_ANIM_DELTA]) != 0u))
			{
				ok = 0;
			}
		}
		RldMk_Test(ok, "frames 21/7/15/4, raw, every part on 4 bytes", "frame size %u", b.frameSize);

		ok = ((b.origin[1] & 1) == 0);
		for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
		{
			for (f = 0; f < (u32)s_rldCharFrames[a]; f++)
			{
				const u32 frame = RldMk_TestFrame(b.cmdl, a, f);

				if ((Rld_ReadLE16(&body[frame + 2u]) & 1u) != 0u)
				{
					ok = 0;
				}
				for (i = 0; i < b.recordCount; i++)
				{
					if ((body[frame + RLDMK_FRAME_HEADER + 3u * i + 2u] & 1u) != 0u)
					{
						ok = 0;
					}
				}
			}
		}
		RldMk_Test(ok, "the up axis on even values", "pos.y %d", b.origin[1]);
	}

	// Poses: the kart stays byte for byte, the driver leans toward +X in frame 0.
	{
		double x0 = 0.0;
		double x10 = 0.0;
		double x20 = 0.0;
		int kartStill = 1;
		int wheelStill = 1;

		for (i = 0; i < b.recordCount; i++)
		{
			const u32 position = b.record[i];
			const int group = b.positionGroup[position];
			double p[3];

			for (f = 1; f < RLDMK_FRAMES_TOTAL; f++)
			{
				const int same = (memcmp(&b.quant[(size_t)position * 3u], &b.quant[((size_t)f * b.positionCount + position) * 3u], 3u) == 0);

				if ((group == RLDMK_GROUP_KART) && !same)
				{
					kartStill = 0;
				}
				if ((group == RLDMK_GROUP_WHEEL) && (f >= (u32)s_rldCharFrames[0]) &&
				    (memcmp(&b.quant[((size_t)RLDMK_NEUTRAL_FRAME * b.positionCount + position) * 3u], &b.quant[((size_t)f * b.positionCount + position) * 3u],
				            3u) != 0))
				{
					wheelStill = 0;
				}
			}

			if (group == RLDMK_GROUP_DRIVER)
			{
				RldMk_Decoded(&b, 0u, position, p);
				x0 += p[0];
				RldMk_Decoded(&b, RLDMK_NEUTRAL_FRAME, position, p);
				x10 += p[0];
				RldMk_Decoded(&b, 20u, position, p);
				x20 += p[0];
			}
		}
		RldMk_Test(kartStill, "the kart is the same in all 47 frames", "the kart moves");
		RldMk_Test(wheelStill, "the steering wheel moves only when steering", "the wheel moves in reverse, bump or jump");
		RldMk_Test((x0 > x10) && (x10 > x20), "turn frame 0 leans toward +X, frame 20 the other way", "driver x %.2f / %.2f / %.2f", x0, x10, x20);
	}

	// The counter-checks are not empty: they notice a wrong side and a wrong record.
	RldMk_Test(RldMk_Winding(&b, 0, &bad), "cull sides replayed: strips", "triangle %u", bad);
	{
		u8 *word = &b.cmdl[4u + RldMk_TestCommand(b.cmdl, 0u)];

		word[3] ^= (u8)(RLDMK_CMD_SIDE >> 24);
		RldMk_Test(!RldMk_Winding(&b, 0, &bad), "a wrong cull side is noticed", "not noticed");
		word[3] ^= (u8)(RLDMK_CMD_SIDE >> 24);
	}
	RldMk_Test(RldMk_ReadBack(&b, &bad), "read back by the renderer's rule, within half a step", "record %u", bad);
	{
		u8 *q = &b.cmdl[4u + RldMk_TestFrame(b.cmdl, 0, 0u) + RLDMK_FRAME_HEADER];
		const u8 kept = q[0];

		q[0] = (u8)((kept < 128u) ? (kept + 2u) : (kept - 2u));
		RldMk_Test(!RldMk_ReadBack(&b, &bad), "a wrong record is noticed", "not noticed");
		q[0] = kept;
	}

	// Built twice, ascii and binary: the same bytes.
	{
		struct RldMkPly plyAgain;

		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
		RldMk_Test(ok, "built twice: the same bytes", "two builds differ");
		RldMk_FreeBuild(&again);
		RldMk_FreePly(&plyAgain);

		ok = RldMk_TestBuild(mesh, 1, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
		RldMk_Test(ok, "ascii and binary PLY: the same bytes", "they differ");
		RldMk_FreeBuild(&again);
		RldMk_FreePly(&plyAgain);
	}

	// Still poses: every frame the neutral one.
	opt.still = 1;
	{
		struct RldMkPly plyStill;

		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &plyStill, &again) && !again.posed;
		for (a = 0; ok && (a < RLDCHAR_ANIM_COUNT); a++)
		{
			for (f = 0; ok && (f < (u32)s_rldCharFrames[a]); f++)
			{
				ok = (memcmp(&again.cmdl[4u + RldMk_TestFrame(again.cmdl, 0, 0u)], &again.cmdl[4u + RldMk_TestFrame(again.cmdl, a, f)], again.frameSize) == 0);
			}
		}
		RldMk_Test(ok, "--poses still: 47 times the same frame", "frames differ");
		RldMk_FreeBuild(&again);
		RldMk_FreePly(&plyStill);
	}

	*goodOut = RldMk_TestCopy(b.cmdl, b.cmdlSize);
	*goodSizeOut = b.cmdlSize;
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// The rules of RldChar_CheckModel, one broken at a time; each case is named
// after what it breaks (bad-<what>).
static void RldMk_SelfTestRules(const u8 *good, size_t goodSize)
{
	const u32 g = Rld_ReadLE32(good);
	const u32 header = RLDMK_BODY_HEADER;
	const u32 colorCount = Rld_ReadLE32(&good[4u + RLDMK_BODY_COMMANDS]);
	u8 *m;
	size_t size;
	u32 at;

	RldMk_TestRule("good model passes", good, goodSize, NULL, RLDCHAR_VERDICT_OK);

	// stage 1
	m = RldMk_TestCopy(good, goodSize);
	size = goodSize + 4u;
	RldMk_TestRule("bad-bounds", m, size, "model-bounds", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	size = goodSize;
	if (m != NULL)
	{
		RldMk_TestMapAppend(m, &size, RLDMK_MODEL_HEADERS);
	}
	RldMk_TestRule("bad-map-dup", m, size, "model-map", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	size = goodSize;
	if (m != NULL)
	{
		RldMk_TestMapAppend(m, &size, header + RLDMK_HEADER_FRAME_DATA);
	}
	RldMk_TestRule("bad-map-null", m, size, "model-map", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	size = goodSize;
	if (m != NULL)
	{
		RldMk_TestMapRemove(m, &size, RLDMK_BODY_ANIM_TABLE + 12u);
	}
	RldMk_TestRule("bad-map-unlisted", m, size, "model-map", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	size = goodSize;
	if (m != NULL)
	{
		RldMk_TestMapAppend(m, &size, g);
	}
	RldMk_TestRule("bad-map-out", m, size, "model-map", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE16(&m[4u + RLDMK_MODEL_NUM_HEADERS], 2u);
	}
	RldMk_TestRule("bad-headers-2", m, goodSize, "model-head", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE16(&m[4u + RLDMK_MODEL_ID], 0u);
	}
	RldMk_TestRule("bad-model-id", m, goodSize, "model-head", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		memcpy(&m[4], "abcdefghijklm", 13u);
	}
	RldMk_TestRule("bad-modelname-13", m, goodSize, "model-head", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	size = goodSize;
	if (m != NULL)
	{
		Rld_WriteLE32(&m[4u + header + RLDMK_HEADER_COMMANDS], 0u);
		RldMk_TestMapRemove(m, &size, header + RLDMK_HEADER_COMMANDS);
	}
	RldMk_TestRule("bad-no-cmdlist", m, size, "model-headers", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE16(&m[4u + header + RLDMK_HEADER_FLAGS], 1u);
	}
	RldMk_TestRule("bad-nonzero", m, goodSize, "model-zero", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE16(&m[4u + header + RLDMK_HEADER_LOD], 100u);
	}
	RldMk_TestRule("bad-lod", m, goodSize, "model-lod", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	size = goodSize;
	if (m != NULL)
	{
		Rld_WriteLE32(&m[4u + header + RLDMK_HEADER_NUM_ANIMS], 3u);
		Rld_WriteLE32(&m[4u + RLDMK_BODY_ANIM_TABLE + 12u], 0u);
		RldMk_TestMapRemove(m, &size, RLDMK_BODY_ANIM_TABLE + 12u);
	}
	RldMk_TestRule("bad-anims-3", m, size, "model-anims", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	size = goodSize;
	if (m != NULL)
	{
		at = RldMk_TestAnim(m, 0);
		Rld_WriteLE32(&m[4u + at + RLDMK_ANIM_DELTA], RLDMK_BODY_HEADER);
		RldMk_TestMapAppend(m, &size, at + RLDMK_ANIM_DELTA);
	}
	RldMk_TestRule("bad-delta", m, size, "model-anims", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = RldMk_TestAnim(m, 1);
		Rld_WriteLE16(&m[4u + at + RLDMK_ANIM_NUM_FRAMES], 0x8000u | (u32)s_rldCharFrames[1]);
	}
	RldMk_TestRule("bad-halfframe", m, goodSize, "model-anims", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	// stage 2
	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		const u32 count = (Rld_ReadLE32(&m[4u + header + RLDMK_HEADER_COLORS]) - RLDMK_BODY_COMMANDS) / 4u - 2u;
		const u32 last = RldMk_TestCommand(m, count);
		int clean = 1;

		// The end word becomes a corner; nothing behind the list may then look
		// like an end word, or the case would test something else.
		Rld_WriteLE32(&m[4u + last], Rld_ReadLE32(&m[4u + last - 4u]));
		for (at = last; at < g; at += 4u)
		{
			if (Rld_ReadLE32(&m[4u + at]) == RLDMK_CMD_END)
			{
				clean = 0;
			}
		}
		if (clean)
		{
			RldMk_TestRule("bad-noterm", m, goodSize, "model-term", RLDCHAR_VERDICT_BAD_MODEL);
		}
		else
		{
			RldMk_Test(0, "bad-noterm", "the test model has an end word behind its list");
		}
	}
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE32(&m[4u + RLDMK_BODY_COMMANDS], 129u);
	}
	RldMk_TestRule("bad-colors-129", m, goodSize, "model-colors-max", RLDCHAR_VERDICT_MODEL_LIMIT);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = 4u + RldMk_TestCommand(m, 1u);
		Rld_WriteLE32(&m[at], (Rld_ReadLE32(&m[at]) & ~0x0000FE00u) | (colorCount << RLDMK_COLOR_SHIFT));
	}
	RldMk_TestRule("bad-colors-index", m, goodSize, "model-colors-range", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE32(&m[4u + RldMk_TestCommand(m, 1u)], 0u);
	}
	RldMk_TestRule("bad-colorcmd", m, goodSize, "model-colorcmd", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = 4u + RldMk_TestCommand(m, 0u);
		Rld_WriteLE32(&m[at], Rld_ReadLE32(&m[at]) | 1u);
	}
	RldMk_TestRule("bad-tex-1", m, goodSize, "model-tex", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = 4u + RldMk_TestCommand(m, 0u);
		Rld_WriteLE32(&m[at], Rld_ReadLE32(&m[at]) | RLDMK_CMD_CACHED);
	}
	RldMk_TestRule("bad-cache-order", m, goodSize, "model-cache-order", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = 4u + RldMk_TestCommand(m, 0u);
		Rld_WriteLE32(&m[at], (Rld_ReadLE32(&m[at]) & ~0x00FF0000u) | (88u << RLDMK_SLOT_SHIFT));
	}
	RldMk_TestRule("bad-slot-88", m, goodSize, "model-slots", RLDCHAR_VERDICT_MODEL_LIMIT);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = 4u + RldMk_TestCommand(m, 0u);
		Rld_WriteLE32(&m[at], Rld_ReadLE32(&m[at]) & ~0x00FF0000u);
	}
	RldMk_TestRule("bad-slot-color", m, goodSize, "model-slot-color", RLDCHAR_VERDICT_MODEL_LIMIT);
	free(m);

	// stage 3
	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = RldMk_TestAnim(m, 0);
		Rld_WriteLE16(&m[4u + at + RLDMK_ANIM_FRAME_SIZE], Rld_ReadLE16(&m[4u + at + RLDMK_ANIM_FRAME_SIZE]) - 1u);
	}
	RldMk_TestRule("bad-frame-unaligned", m, goodSize, "model-frame-layout", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		at = RldMk_TestAnim(m, 0);
		Rld_WriteLE16(&m[4u + at + RLDMK_ANIM_NUM_FRAMES], 20u);
	}
	RldMk_TestRule("bad-frames-20", m, goodSize, "model-anim-frames", RLDCHAR_VERDICT_MODEL_LIMIT);
	free(m);

	m = RldMk_TestCopy(good, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE16(&m[4u + RldMk_TestFrame(m, 0, 0u)], 8000u);
	}
	RldMk_TestRule("bad-coords", m, goodSize, "model-coords", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	// The body 4 bytes short: the last frame of the jump reaches past it.
	m = (u8 *)calloc(1, goodSize);
	if (m != NULL)
	{
		Rld_WriteLE32(&m[0], g - 4u);
		memcpy(&m[4], &good[4], (size_t)g - 4u);
		memcpy(&m[g], &good[4u + g], goodSize - 4u - (size_t)g);
	}
	RldMk_TestRule("bad-verts", m, goodSize - 4u, "model-verts", RLDCHAR_VERDICT_BAD_MODEL);
	free(m);

	// Straight through the writer: 21837 records need a frame of 65540 bytes.
	m = RldMk_TestStrip(21837u, 1u, &size);
	RldMk_TestRule("bad-frame-64k", m, size, "model-frame-size", RLDCHAR_VERDICT_MODEL_LIMIT);
	free(m);

	// stage 4: 805 untextured triangles are 22540 bytes, 806 are 22568.
	m = RldMk_TestStrip(807u, 807u, &size);
	RldMk_TestRule("good-drawbytes-max (805 triangles)", m, size, NULL, RLDCHAR_VERDICT_OK);
	free(m);

	m = RldMk_TestStrip(808u, 808u, &size);
	RldMk_TestRule("bad-drawbytes (806 triangles)", m, size, "model-draw", RLDCHAR_VERDICT_MODEL_LIMIT);
	free(m);
}

// CHRI, the container, and the name as the maker takes it.
static void RldMk_SelfTestChri(const u8 *good, size_t goodSize)
{
	static const struct
	{
		const char *name;
		const char *rule;
	} cases[8] = {
		{"bad-chri-size", "CHRI-1"}, {"bad-template-15", "CHRI-2"}, {"bad-class-4", "CHRI-3"}, {"bad-name-0", "CHRI-4"},
		{"bad-name-18", "CHRI-4"},   {"bad-name-lower", "CHRI-4"},  {"bad-name-at", "CHRI-4"}, {"bad-author-65", "CHRI-5"},
	};
	u8 field[RLDCHAR_NAME_FIELD];
	char shown[64];
	struct RldCharInfo info;
	const char *why;
	u8 *chri;
	size_t chriSize = 0;
	int c;

	memset(field, 0, sizeof(field));
	memcpy(field, "SELF TEST", 9u);
	chri = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", &chriSize);
	why = (chri != NULL) ? RldChar_ParseInfo(&info, chri, chriSize) : "out of memory";
	RldMk_Test((why == NULL) && (info.templateId == 14u) && (info.classId == 0u) && (strcmp(info.name, "SELF TEST") == 0) && (info.charVersion == 3u) &&
	               (strcmp(info.author, "self-test") == 0),
	           "CHRI there and back", "%s", (why != NULL) ? why : "a field differs");

	// The container: header, directory, hashes, and the model still checks.
	if ((chri != NULL) && (good != NULL))
	{
		size_t size = 0;
		size_t sizeAgain = 0;
		u8 *file = RldMk_Container(chri, chriSize, good, goodSize, &size);
		u8 *again = RldMk_Container(chri, chriSize, good, goodSize, &sizeAgain);
		int ok = (file != NULL) && (again != NULL) && (size == sizeAgain) && (memcmp(file, again, size) == 0);

		if (ok)
		{
			const u64 dirAt = Rld_ReadLE64(&file[0x18]);

			ok = (memcmp(file, "RLDCHAR\0", 8) == 0) && (Rld_ReadLE16(&file[0x08]) == (u32)RLDCHAR_VERSION_MAJOR) &&
			     (Rld_ReadLE16(&file[0x0A]) == (u32)RLDCHAR_VERSION_MINOR) && (Rld_ReadLE32(&file[0x0C]) == 0u) && (Rld_ReadLE32(&file[0x10]) == 2u) &&
			     (Rld_ReadLE64(&file[0x20]) == (u64)size) && ((dirAt + 2u * RLD_DIR_ENTRY_SIZE) == (u64)size);

			for (c = 0; ok && (c < 2); c++)
			{
				const u8 *entry = &file[(size_t)dirAt + (size_t)c * RLD_DIR_ENTRY_SIZE];
				const u64 offset = Rld_ReadLE64(&entry[0x08]);
				const u64 stored = Rld_ReadLE64(&entry[0x10]);
				u8 digest[32];

				Sha256(&file[(size_t)offset], (size_t)stored, digest);
				ok = (memcmp(entry, (c == 0) ? "CHRI" : "CMDL", 4) == 0) && (memcmp(&entry[RLD_DIR_HASH_OFFSET], digest, 32) == 0);
				if (ok && (c == 1))
				{
					ok = (RldChar_CheckModel(&file[(size_t)offset], (size_t)stored, NULL, NULL, NULL, NULL) == RLDCHAR_VERDICT_OK);
				}
			}
		}
		RldMk_Test(ok, "container: RLDCHAR 1.0, CHRI + CMDL, hashes, twice the same", "the container is not as written");
		free(file);
		free(again);
	}

	for (c = 0; (chri != NULL) && (c < 8); c++)
	{
		u8 *m = RldMk_TestCopy(chri, chriSize);
		size_t size = chriSize;

		if (m == NULL)
		{
			continue;
		}

		if (c == 0)
		{
			Rld_WriteLE16(&m[0x00], 0x1Eu);
		}
		else if (c == 1)
		{
			m[0x02] = 15u;
		}
		else if (c == 2)
		{
			m[0x03] = 4u;
		}
		else if (c == 3)
		{
			memset(&m[0x04], 0, RLDCHAR_NAME_FIELD);
		}
		else if (c == 4)
		{
			memset(&m[0x04], 0, RLDCHAR_NAME_FIELD);
			memset(&m[0x04], 'A', 18u);
		}
		else if (c == 5)
		{
			memset(&m[0x04], 0, RLDCHAR_NAME_FIELD);
			memcpy(&m[0x04], "Test", 4u);
		}
		else if (c == 6)
		{
			memset(&m[0x04], 0, RLDCHAR_NAME_FIELD);
			memcpy(&m[0x04], "A@B", 3u);
		}
		else
		{
			char author[RLDCHAR_AUTHOR_MAX + 2];

			free(m);
			memset(author, 'x', RLDCHAR_AUTHOR_MAX + 1);
			author[RLDCHAR_AUTHOR_MAX + 1] = '\0';
			m = RldMk_BuildChri(14u, 0u, field, 1u, author, &size);
			if (m == NULL)
			{
				continue;
			}
		}

		why = RldChar_ParseInfo(&info, m, size);
		RldMk_Test((why != NULL) && (strncmp(why, cases[c].rule, 6) == 0), cases[c].name, "expected %s (DAMAGED), got %s", cases[c].rule,
		           (why != NULL) ? why : "accepted");
		free(m);
	}
	free(chri);

	// The name as typed: capitals, the menu font, the length.
	RldMk_ResetSaid();
	RldMk_Test(RldMk_Name("test driver", field, shown, sizeof(shown)) && (strcmp(shown, "TEST DRIVER") == 0) && RldMk_Said("name-upper"),
	           "name in lower case: shown in capitals", "got \"%s\"", shown);
	RldMk_ResetSaid();
	RldMk_Test(!RldMk_Name("A@B", field, shown, sizeof(shown)) && RldMk_Said("name-chars"), "name with @ refused", "said: %s", s_rldMkCodes);
	RldMk_ResetSaid();
	RldMk_Test(!RldMk_Name("ABCDEFGHIJKLMNOPQR", field, shown, sizeof(shown)) && RldMk_Said("name"), "name of 18 characters refused", "said: %s",
	           s_rldMkCodes);
	RldMk_ResetSaid();
	RldMk_Test(RldMk_Name("ABCDEFGHIJKLMNOPQ", field, shown, sizeof(shown)) && RldMk_Said("name-long") && (RldChar_NameValid(field) == NULL),
	           "name of 17 characters taken, with a note", "said: %s", s_rldMkCodes);
}

// The number of failed cases; 0 = passed.
static int RldChar_SelfTest(void)
{
	struct RldMkTestMesh *mesh = (struct RldMkTestMesh *)calloc(1, sizeof(struct RldMkTestMesh));
	const int quietBefore = s_rldMkQuiet;
	u8 *good = NULL;
	size_t goodSize = 0;

	s_rldMkTestFailed = 0;
	if (mesh == NULL)
	{
		printf("  FAIL make-char: out of memory\n");
		return 1;
	}

	s_rldMkQuiet = 1;
	RldMk_SelfTestPly(mesh);
	RldMk_SelfTestChain(mesh);
	RldMk_SelfTestModel(mesh, &good, &goodSize);
	if (good != NULL)
	{
		RldMk_SelfTestRules(good, goodSize);
	}
	else
	{
		RldMk_Test(0, "model rules", "no good model to break");
	}
	RldMk_SelfTestChri(good, goodSize);
	s_rldMkQuiet = quietBefore;
	RldMk_ResetSaid();

	free(good);
	free(mesh);
	return s_rldMkTestFailed;
}
